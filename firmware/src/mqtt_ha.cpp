#include "mqtt_ha.h"
#include "ble_client.h"
#include <ArduinoJson.h>
#include "logger.h"
#include <WiFi.h>
#include <math.h>
#include <cstring>

static constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
static constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long MQTT_STATE_PUBLISH_INTERVAL_MS = 1000;
static constexpr const char* HA_PREFIX = "homeassistant";
static constexpr const char* HA_DEVICE_ID = "cgo600";

static void clearRetainedTopic(AsyncMqttClient& client, const char* topic) {
    client.publish(topic, 1, true, "");
}

static bool sameFloat(float a, float b) {
    if (a < 0 && b < 0) {
        return true;
    }
    return fabsf(a - b) < 0.05f;
}

static bool sameText(const char* a, const char* b) {
    return strncmp(a, b, 32) == 0;
}

static void copyString(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

MqttHA mqttHA;

void MqttHA::begin(const char* broker, uint16_t port,
                   const char* user, const char* pass,
                   const char* prefix) {
    if (!broker || broker[0] == '\0') {
        _enabled = false;
        return;
    }
    _enabled = true;
    copyString(_broker, sizeof(_broker), broker);
    _port = port;
    copyString(_prefix, sizeof(_prefix), prefix && prefix[0] ? prefix : "cgo600");
    copyString(_user, sizeof(_user), user);
    copyString(_pass, sizeof(_pass), pass);
    _discoverySerial[0] = '\0';
    _discoveryDone = false;
    _state = ConnectionState::DISCONNECTED;
    _connectStartedAt = 0;
    _lastReconnect = 0;
    snprintf(_clientId, sizeof(_clientId), "cgo600-%06X", (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF));

    // All pointers passed to AsyncMqttClient must remain valid (it stores pointers, not copies)
    snprintf(_lwtTopic, sizeof(_lwtTopic), "%s/status", _prefix);
    snprintf(_lightStateTopic, sizeof(_lightStateTopic), "%s/light/state", _prefix);
    snprintf(_lightCommandTopic, sizeof(_lightCommandTopic), "%s/light/set", _prefix);
    _client.setWill(_lwtTopic, 1, true, "offline");

    _client.setClientId(_clientId);
    _client.setServer(_broker, port);
    if (_user[0]) {
        _client.setCredentials(_user, _pass[0] ? _pass : nullptr);
    }

    _client.onConnect([this](bool sp) { onConnect(sp); });
    _client.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { onDisconnect(reason); });
    _client.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        (void)properties;
        onMessage(topic, payload, len, index, total);
    });

    startConnect();
}

void MqttHA::disconnect() {
    if (!_enabled) return;
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/status", _prefix);
    _client.publish(topic, 1, true, "offline");
    _client.disconnect();
    _enabled = false;
    _state = ConnectionState::DISCONNECTED;
    _connectStartedAt = 0;
}

void MqttHA::loop() {
    if (!_enabled) return;

    const unsigned long now = millis();

    if (_state == ConnectionState::CONNECTING && now - _connectStartedAt >= MQTT_CONNECT_TIMEOUT_MS) {
        LOGF("[MQTT] Connect timeout after %lu ms, resetting client (wifi=%d ip=%s)\n",
            now - _connectStartedAt,
            WiFi.status(),
            WiFi.localIP().toString().c_str());
        _client.disconnect(true);
        _state = ConnectionState::DISCONNECTED;
        _connectStartedAt = 0;
        _lastReconnect = now;
        return;
    }

    if (_state == ConnectionState::DISCONNECTED && now - _lastReconnect >= MQTT_RECONNECT_INTERVAL_MS) {
        startConnect();
        return;
    }

    if (_state == ConnectionState::CONNECTED && _statePublishPending && now - _lastStatePublish >= MQTT_STATE_PUBLISH_INTERVAL_MS) {
        publishStateTopics(_cachedState);
        _lastStatePublish = now;
        _statePublishPending = false;
    }
}

bool MqttHA::isConnected() const {
    return _enabled && _client.connected();
}

void MqttHA::onConnect(bool sessionPresent) {
    (void)sessionPresent;
    _state = ConnectionState::CONNECTED;
    _connectStartedAt = 0;
    _hasPublishedState = false;
    LOGF("[MQTT] Connected to %s\n", _broker);
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/status", _prefix);
    _client.publish(topic, 1, true, "online");
    _client.subscribe(_lightCommandTopic, 1);

    if (_hasCachedState) {
        _discoveryDone = false;
        publishDiscovery(_cachedState.getDisplaySerial());
        publishStateTopics(_cachedState);
        _lastStatePublish = millis();
        _statePublishPending = false;
    }
}

void MqttHA::onMessage(char* topic, char* payload, size_t len, size_t index, size_t total) {
    if (!_enabled || !_client.connected() || !_lightCommandCallback) return;
    if (index != 0 || total != len) {
        LOGLN("[MQTT] Ignoring fragmented message");
        return;
    }
    if (!topic || strcmp(topic, _lightCommandTopic) != 0) return;

    String body;
    body.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        body += (char)payload[i];
    }
    body.trim();
    body.toLowerCase();

    bool on;
    if (body == "on" || body == "1" || body == "true") {
        on = true;
    } else if (body == "off" || body == "0" || body == "false") {
        on = false;
    } else {
        LOGF("[MQTT] Ignoring unsupported light payload: %s\n", body.c_str());
        return;
    }

    LOGF("[MQTT] RX light command: %s\n", on ? "on" : "off");
    if (!_lightCommandCallback(on)) {
        LOGLN("[MQTT] Light command rejected by BLE layer");
    }
}

void MqttHA::onDisconnect(AsyncMqttClientDisconnectReason reason) {
    _state = ConnectionState::DISCONNECTED;
    _connectStartedAt = 0;
    _lastReconnect = millis();
    LOGF("[MQTT] Disconnected (reason=%d, wifi=%d, ip=%s)\n",
        (int)reason,
        WiFi.status(),
        WiFi.localIP().toString().c_str());
}

void MqttHA::startConnect() {
    if (!_enabled || _state == ConnectionState::CONNECTING || _state == ConnectionState::CONNECTED) {
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        _lastReconnect = millis();
        return;
    }

    _state = ConnectionState::CONNECTING;
    _connectStartedAt = millis();
    _lastReconnect = _connectStartedAt;
    LOGF("[MQTT] Connecting to %s:%d (wifi=%d ip=%s)...\n",
        _broker,
        _port,
        WiFi.status(),
        WiFi.localIP().toString().c_str());
    _client.connect();
}

void MqttHA::publishDiscovery(const char* serial) {
    const bool hasSerial = serial && serial[0];
    if (_discoveryDone) {
        if (!hasSerial) return;
        if (strncmp(_discoverySerial, serial, sizeof(_discoverySerial)) == 0) return;
    }

    static const char* sensorSuffixes[] = {
        "battery",
        "speed",
        "max_speed",
        "avg_speed",
        "trip_distance",
        "trip_duration",
        "remaining_range",
        "range",
        "serial_number",
        "last_connected",
    };

    // Sensor definitions — identical to Python mqtt.py _SENSORS
    struct SensorDef {
        const char* suffix;
        const char* component;
        const char* devClass;
        const char* unit;
        const char* icon;
    };
    const SensorDef sensors[] = {
        {"battery",   "sensor",        "battery",  "%",    nullptr},
        {"speed",     "sensor",        nullptr,    "km/h", "mdi:speedometer"},
        {"max_speed", "sensor",        nullptr,    "km/h", "mdi:speedometer-slow"},
        {"avg_speed", "sensor",        nullptr,    "km/h", "mdi:speedometer-medium"},
        {"trip_distance", "sensor",   "distance", "km",   "mdi:map-marker-distance"},
        {"trip_duration", "sensor",   "duration", "s",   "mdi:timer-outline"},
        {"remaining_range", "sensor", "distance", "km",   "mdi:map-marker-radius"},
        {"range",     "sensor",        "distance", "km",   "mdi:map-marker-distance"},
        {"serial_number", "sensor",   nullptr,    nullptr,  "mdi:identifier"},
        {"last_connected", "sensor",   "timestamp", nullptr, "mdi:clock-check-outline"},
    };

    auto formatSensorName = [](const char* suffix, char* out, size_t outSize) {
        size_t j = 0;
        bool capitalize = true;
        for (size_t i = 0; suffix[i] != '\0' && j + 1 < outSize; ++i) {
            char c = suffix[i];
            if (c == '_') {
                out[j++] = ' ';
                capitalize = true;
                continue;
            }
            if (capitalize && c >= 'a' && c <= 'z') {
                c = (char)(c - ('a' - 'A'));
            }
            out[j++] = c;
            capitalize = false;
        }
        out[j] = '\0';
    };

    char legacyTopic[160];
    if (hasSerial) {
        for (const char* suffix : sensorSuffixes) {
            snprintf(legacyTopic, sizeof(legacyTopic), "%s/sensor/%s_%s_%s/config", HA_PREFIX, HA_DEVICE_ID, serial, suffix);
            clearRetainedTopic(_client, legacyTopic);
        }
        snprintf(legacyTopic, sizeof(legacyTopic), "%s/light/%s_%s_light/config", HA_PREFIX, HA_DEVICE_ID, serial);
        clearRetainedTopic(_client, legacyTopic);
        snprintf(legacyTopic, sizeof(legacyTopic), "%s/binary_sensor/%s_%s_light/config", HA_PREFIX, HA_DEVICE_ID, serial);
        clearRetainedTopic(_client, legacyTopic);
        LOGF("[MQTT] Cleared legacy HA discovery topics for serial %s\n", serial);
    }
    snprintf(legacyTopic, sizeof(legacyTopic), "%s/sensor/%s_meter_fw_ver/config", HA_PREFIX, HA_DEVICE_ID);
    clearRetainedTopic(_client, legacyTopic);
    if (hasSerial) {
        snprintf(legacyTopic, sizeof(legacyTopic), "%s/sensor/%s_%s_meter_fw_ver/config", HA_PREFIX, HA_DEVICE_ID, serial);
        clearRetainedTopic(_client, legacyTopic);
    }
    snprintf(legacyTopic, sizeof(legacyTopic), "%s/sensor/meter_fw_ver/state", _prefix);
    clearRetainedTopic(_client, legacyTopic);

    for (auto& s : sensors) {
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_%s", HA_DEVICE_ID, s.suffix);

        char configTopic[128];
        snprintf(configTopic, sizeof(configTopic), "%s/%s/%s/config", HA_PREFIX, s.component, uniqueId);

        char stateTopic[96];
        snprintf(stateTopic, sizeof(stateTopic), "%s/sensor/%s/state", _prefix, s.suffix);

        char availTopic[64];
        snprintf(availTopic, sizeof(availTopic), "%s/status", _prefix);

        JsonDocument doc;
        char name[48];
        formatSensorName(s.suffix, name, sizeof(name));
        doc["name"] = name;
        doc["state_topic"] = stateTopic;
        doc["unique_id"] = uniqueId;
        doc["availability_topic"] = availTopic;

        JsonObject device = doc["device"].to<JsonObject>();
        JsonArray ids = device["identifiers"].to<JsonArray>();
        ids.add(HA_DEVICE_ID);
        device["name"] = "Tenways CGO600";
        device["manufacturer"] = "Tenways";
        device["model"] = "CGO600";
        if (hasSerial) device["serial_number"] = serial;

        if (s.devClass) doc["device_class"] = s.devClass;
        if (s.unit) doc["unit_of_measurement"] = s.unit;
        if (s.icon) doc["icon"] = s.icon;

        if (strcmp(s.component, "binary_sensor") == 0) {
            doc["payload_on"] = "on";
            doc["payload_off"] = "off";
        }

        String payload;
        serializeJson(doc, payload);
        _client.publish(configTopic, 1, true, payload.c_str());
    }

    {
        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "%s_light", HA_DEVICE_ID);

        char configTopic[128];
        snprintf(configTopic, sizeof(configTopic), "%s/light/%s/config", HA_PREFIX, uniqueId);

        JsonDocument doc;
        doc["name"] = "Light";
        doc["unique_id"] = uniqueId;
        doc["state_topic"] = _lightStateTopic;
        doc["command_topic"] = _lightCommandTopic;
        doc["availability_topic"] = _lwtTopic;
        doc["payload_on"] = "on";
        doc["payload_off"] = "off";
        doc["icon"] = "mdi:lightbulb";

        JsonObject device = doc["device"].to<JsonObject>();
        JsonArray ids = device["identifiers"].to<JsonArray>();
        ids.add(HA_DEVICE_ID);
        device["name"] = "Tenways CGO600";
        device["manufacturer"] = "Tenways";
        device["model"] = "CGO600";
        if (hasSerial) device["serial_number"] = serial;

        String payload;
        serializeJson(doc, payload);
        _client.publish(configTopic, 1, true, payload.c_str());
    }

    {
        char oldConfigTopic[128];
        snprintf(oldConfigTopic, sizeof(oldConfigTopic), "%s/binary_sensor/%s_light/config", HA_PREFIX, HA_DEVICE_ID);
        _client.publish(oldConfigTopic, 1, true, "");
    }

    if (hasSerial) {
        copyString(_discoverySerial, sizeof(_discoverySerial), serial);
    } else {
        _discoverySerial[0] = '\0';
    }
    _discoveryDone = true;
    LOGF("[MQTT] HA discovery published for %s\n", HA_DEVICE_ID);
}

void MqttHA::publishStateTopics(const BikeState& state) {
    if (!_enabled || !_client.connected()) return;

    const char* displaySerial = state.getDisplaySerial();
    const BikeState* prev = _hasPublishedState ? &_publishedState : nullptr;

    if (!_discoveryDone || (displaySerial && strncmp(_discoverySerial, displaySerial, sizeof(_discoverySerial)) != 0)) {
        publishDiscovery(displaySerial);
    }

    char topic[96];
    char value[16];

    if (state.battery >= 0 && (!prev || prev->battery != state.battery)) {
        snprintf(topic, sizeof(topic), "%s/sensor/battery/state", _prefix);
        snprintf(value, sizeof(value), "%d", state.battery);
        _client.publish(topic, 0, false, value);
    }
    if (state.speed_kmh >= 0 && (!prev || !sameFloat(prev->speed_kmh, state.speed_kmh))) {
        snprintf(topic, sizeof(topic), "%s/sensor/speed/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.speed_kmh);
        _client.publish(topic, 0, false, value);
    }
    if (state.max_speed_kmh >= 0 && (!prev || !sameFloat(prev->max_speed_kmh, state.max_speed_kmh))) {
        snprintf(topic, sizeof(topic), "%s/sensor/max_speed/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.max_speed_kmh);
        _client.publish(topic, 0, false, value);
    }
    if (state.avg_speed_kmh >= 0 && (!prev || !sameFloat(prev->avg_speed_kmh, state.avg_speed_kmh))) {
        snprintf(topic, sizeof(topic), "%s/sensor/avg_speed/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.avg_speed_kmh);
        _client.publish(topic, 0, false, value);
    }
    if (state.trip_distance_km >= 0 && (!prev || !sameFloat(prev->trip_distance_km, state.trip_distance_km))) {
        snprintf(topic, sizeof(topic), "%s/sensor/trip_distance/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.trip_distance_km);
        _client.publish(topic, 0, false, value);
    }
    if (state.trip_duration >= 0 && (!prev || prev->trip_duration != state.trip_duration)) {
        snprintf(topic, sizeof(topic), "%s/sensor/trip_duration/state", _prefix);
        snprintf(value, sizeof(value), "%d", state.trip_duration);
        _client.publish(topic, 0, false, value);
    }
    if (state.remaining_range_km >= 0 && (!prev || !sameFloat(prev->remaining_range_km, state.remaining_range_km))) {
        snprintf(topic, sizeof(topic), "%s/sensor/remaining_range/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.remaining_range_km);
        _client.publish(topic, 0, false, value);
    }
    if (state.total_range_km >= 0 && (!prev || !sameFloat(prev->total_range_km, state.total_range_km))) {
        snprintf(topic, sizeof(topic), "%s/sensor/range/state", _prefix);
        snprintf(value, sizeof(value), "%.1f", state.total_range_km);
        _client.publish(topic, 0, false, value);
    }
    if (state.light_valid && (!prev || !prev->light_valid || prev->light_on != state.light_on)) {
        strncpy(topic, _lightStateTopic, sizeof(topic) - 1);
        topic[sizeof(topic) - 1] = '\0';
        _client.publish(topic, 0, false, state.light_on ? "on" : "off");
    }
    if (displaySerial && (!prev || !sameText(prev->serial_number, state.serial_number))) {
        snprintf(topic, sizeof(topic), "%s/sensor/serial_number/state", _prefix);
        _client.publish(topic, 1, true, displaySerial);
    }
    if (state.last_connected_at[0] && (!prev || strncmp(prev->last_connected_at, state.last_connected_at, sizeof(state.last_connected_at)) != 0)) {
        snprintf(topic, sizeof(topic), "%s/sensor/last_connected/state", _prefix);
        _client.publish(topic, 1, true, state.last_connected_at);
    }

    _publishedState = state;
    _hasPublishedState = true;
}

void MqttHA::publishBleStatus(const char* status) {
    if (!_enabled || _state != ConnectionState::CONNECTED) return;
    if (strncmp(_lastBleStatus, status, sizeof(_lastBleStatus)) == 0) return;
    copyString(_lastBleStatus, sizeof(_lastBleStatus), status);
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/sensor/ble_status/state", _prefix);
    _client.publish(topic, 0, false, status);
}

void MqttHA::publishState(const BikeState& state) {
    // Only cache + set flag — never call publishStateTopics() here.
    // This function may be called from the NimBLE task (Core 0), while
    // AsyncMqttClient is not safe to call from that context.
    // The actual publish happens from loop() on the main task.
    _cachedState = state;
    _hasCachedState = true;
    _statePublishPending = true;
}
