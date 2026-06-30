#include "config.h"
#include <ArduinoJson.h>
#include "logger.h"

Config config;

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

static float clampPollInterval(float value) {
    if (value < 0.5f) return 0.5f;
    if (value > 10.0f) return 10.0f;
    return value;
}

static uint16_t clampMaxRides(int value) {
    if (value < 10) return 10;
    if (value > 100) return 100;
    return (uint16_t)value;
}

static bool validMqttPrefix(const char* prefix) {
    if (!prefix || prefix[0] == '\0' || prefix[0] == '/') {
        return false;
    }
    size_t len = 0;
    for (; prefix[len] != '\0'; ++len) {
        const char c = prefix[len];
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '/';
        if (!ok) {
            return false;
        }
    }
    return len > 0 && prefix[len - 1] != '/';
}

void Config::load() {
    Preferences prefs;
    prefs.begin("cgo600", true);
    prefs.getString("mqtt_broker", mqtt_broker, sizeof(mqtt_broker));
    mqtt_port = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_user", mqtt_user, sizeof(mqtt_user));
    prefs.getString("mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
    prefs.getString("mqtt_prefix", mqtt_prefix, sizeof(mqtt_prefix));
    if (!validMqttPrefix(mqtt_prefix)) copyString(mqtt_prefix, sizeof(mqtt_prefix), DEFAULT_MQTT_PREFIX);
    prefs.getString("device_name", device_name, sizeof(device_name));
    if (device_name[0] == '\0') copyString(device_name, sizeof(device_name), DEFAULT_DEVICE_NAME);
    poll_interval = clampPollInterval(prefs.getFloat("poll_interval", 1.0f));
    max_rides = clampMaxRides(prefs.getUShort("max_rides", 20));
    prefs.end();
}

void Config::save() {
    Preferences prefs;
    prefs.begin("cgo600", false);

    bool changed = false;

    if (prefs.getString("mqtt_broker", "") != String(mqtt_broker)) {
        prefs.putString("mqtt_broker", mqtt_broker);
        changed = true;
    }
    if (prefs.getUShort("mqtt_port", 1883) != mqtt_port) {
        prefs.putUShort("mqtt_port", mqtt_port);
        changed = true;
    }
    if (prefs.getString("mqtt_user", "") != String(mqtt_user)) {
        prefs.putString("mqtt_user", mqtt_user);
        changed = true;
    }
    if (prefs.getString("mqtt_pass", "") != String(mqtt_pass)) {
        prefs.putString("mqtt_pass", mqtt_pass);
        changed = true;
    }
    if (prefs.getString("mqtt_prefix", "") != String(mqtt_prefix)) {
        prefs.putString("mqtt_prefix", mqtt_prefix);
        changed = true;
    }
    if (prefs.getString("device_name", "") != String(device_name)) {
        prefs.putString("device_name", device_name);
        changed = true;
    }
    if (prefs.getFloat("poll_interval", 1.0f) != poll_interval) {
        prefs.putFloat("poll_interval", poll_interval);
        changed = true;
    }
    if (prefs.getUShort("max_rides", 20) != max_rides) {
        prefs.putUShort("max_rides", max_rides);
        changed = true;
    }
    prefs.end();

    if (changed) {
        LOGLN("[CFG] Saved updated config to NVS");
    } else {
        LOGLN("[CFG] Config unchanged, skipping NVS writes");
    }
}

String Config::toJson() const {
    JsonDocument doc;
    doc["mqtt_broker"] = mqtt_broker;
    doc["mqtt_port"] = mqtt_port;
    doc["mqtt_user"] = mqtt_user;
    doc["mqtt_pass_set"] = (mqtt_pass[0] != '\0');
    doc["mqtt_prefix"] = mqtt_prefix;
    doc["device_name"] = device_name;
    doc["poll_interval"] = poll_interval;
    doc["max_rides"] = max_rides;
    String out;
    serializeJson(doc, out);
    return out;
}

bool Config::fromJson(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    if (doc["mqtt_broker"].is<const char*>())
        copyString(mqtt_broker, sizeof(mqtt_broker), doc["mqtt_broker"]);
    if (doc["mqtt_port"].is<int>()) {
        const int port = doc["mqtt_port"];
        if (port > 0 && port <= 65535) {
            mqtt_port = (uint16_t)port;
        }
    }
    if (doc["mqtt_user"].is<const char*>())
        copyString(mqtt_user, sizeof(mqtt_user), doc["mqtt_user"]);
    if (doc["mqtt_pass"].is<const char*>()) {
        const char* p = doc["mqtt_pass"];
        if (p[0] != '\0')  // Only overwrite if a new password is provided
            copyString(mqtt_pass, sizeof(mqtt_pass), p);
    }
    if (doc["mqtt_prefix"].is<const char*>())
        copyString(mqtt_prefix, sizeof(mqtt_prefix), doc["mqtt_prefix"]);
    if (!validMqttPrefix(mqtt_prefix))
        copyString(mqtt_prefix, sizeof(mqtt_prefix), DEFAULT_MQTT_PREFIX);
    if (doc["device_name"].is<const char*>())
        copyString(device_name, sizeof(device_name), doc["device_name"]);
    if (device_name[0] == '\0')
        copyString(device_name, sizeof(device_name), DEFAULT_DEVICE_NAME);
    if (doc["poll_interval"].is<float>())
        poll_interval = clampPollInterval(doc["poll_interval"]);
    if (doc["max_rides"].is<int>())
        max_rides = clampMaxRides(doc["max_rides"]);

    return true;
}
