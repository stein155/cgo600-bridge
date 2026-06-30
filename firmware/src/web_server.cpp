#include "web_server.h"
#include "config.h"
#include "ble_client.h"
#include "mqtt_ha.h"
#include "ride_log.h"
#include "logger.h"
#include "reset_reason.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <cstdarg>
#include <cstring>
#include <esp_heap_caps.h>

WebUI webUI;

static uint32_t s_crashCount = 0;
static esp_reset_reason_t s_lastCrashReason = ESP_RST_UNKNOWN;
static bool s_restartRequested = false;
static unsigned long s_restartAt = 0;
static portMUX_TYPE s_webMux = portMUX_INITIALIZER_UNLOCKED;
static constexpr size_t MAX_CONFIG_BODY_SIZE = 1024;
static constexpr size_t MAX_LIGHT_BODY_SIZE = 128;
static constexpr unsigned long WS_MIN_BROADCAST_INTERVAL_MS = 1000;
static constexpr unsigned long WS_KEEPALIVE_INTERVAL_MS = 30000;

static void loadCrashInfo() {
    Preferences crashPrefs;
    crashPrefs.begin("crash", true);
    s_crashCount = crashPrefs.getUInt("count", 0);
    s_lastCrashReason = (esp_reset_reason_t)crashPrefs.getUChar("reason", 0);
    crashPrefs.end();
}

static bool appendJson(char* buffer, size_t bufferSize, size_t& used, const char* format, ...) {
    if (used >= bufferSize) {
        return false;
    }

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer + used, bufferSize - used, format, args);
    va_end(args);

    if (written < 0) {
        return false;
    }
    if ((size_t)written >= bufferSize - used) {
        used = bufferSize - 1;
        buffer[used] = '\0';
        return false;
    }

    used += (size_t)written;
    return true;
}

static size_t buildStateJson(char* buffer, size_t bufferSize) {
    const BikeState& state = bleClient.getState();
    size_t used = 0;
    bool firstField = true;

    if (!appendJson(buffer, bufferSize, used, "{")) {
        return 0;
    }

    auto appendFieldPrefix = [&](const char* key) -> bool {
        if (!appendJson(buffer, bufferSize, used, "%s\"%s\":", firstField ? "" : ",", key)) {
            return false;
        }
        firstField = false;
        return true;
    };

    if (state.battery >= 0) {
        if (!appendFieldPrefix("battery") || !appendJson(buffer, bufferSize, used, "%d", state.battery)) return 0;
    }
    if (state.trip_duration >= 0) {
        if (!appendFieldPrefix("trip_duration") || !appendJson(buffer, bufferSize, used, "%d", state.trip_duration)) return 0;
    }
    if (state.speed_kmh >= 0) {
        if (!appendFieldPrefix("speed") || !appendJson(buffer, bufferSize, used, "%.1f", state.speed_kmh)) return 0;
    }
    if (state.max_speed_kmh >= 0) {
        if (!appendFieldPrefix("max_speed") || !appendJson(buffer, bufferSize, used, "%.1f", state.max_speed_kmh)) return 0;
    }
    if (state.avg_speed_kmh >= 0) {
        if (!appendFieldPrefix("avg_speed") || !appendJson(buffer, bufferSize, used, "%.1f", state.avg_speed_kmh)) return 0;
    }
    if (state.trip_distance_km >= 0) {
        if (!appendFieldPrefix("trip_distance") || !appendJson(buffer, bufferSize, used, "%.1f", state.trip_distance_km)) return 0;
    }
    if (state.remaining_range_km >= 0) {
        if (!appendFieldPrefix("remaining_range") || !appendJson(buffer, bufferSize, used, "%.1f", state.remaining_range_km)) return 0;
    }
    if (state.total_range_km >= 0) {
        if (!appendFieldPrefix("range") || !appendJson(buffer, bufferSize, used, "%.1f", state.total_range_km)) return 0;
    }
    if (state.light_valid) {
        if (!appendFieldPrefix("light") || !appendJson(buffer, bufferSize, used, "\"%s\"", state.light_on ? "on" : "off")) return 0;
    }
    if (state.serial_number[0]) {
        if (!appendFieldPrefix("serial") || !appendJson(buffer, bufferSize, used, "\"%s\"", state.serial_number)) return 0;
    }
    if (state.last_connected_at[0]) {
        if (!appendFieldPrefix("last_connected") || !appendJson(buffer, bufferSize, used, "\"%s\"", state.last_connected_at)) return 0;
    }

    if (!appendFieldPrefix("ble_status") || !appendJson(buffer, bufferSize, used, "\"%s\"", bleClient.getStatusStr())) return 0;
    if (!appendFieldPrefix("mqtt_connected") || !appendJson(buffer, bufferSize, used, "%s", mqttHA.isConnected() ? "true" : "false")) return 0;
    if (!appendJson(buffer, bufferSize, used, "}")) {
        return 0;
    }

    return used;
}

static size_t buildStatusJson(char* buffer, size_t bufferSize) {
    const IPAddress ip = WiFi.localIP();
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    size_t used = 0;
    if (!appendJson(
            buffer,
            bufferSize,
            used,
            "{\"uptime\":%lu,\"free_heap\":%u,\"min_free_heap\":%u,\"largest_free_block\":%u,"
            "\"ble_status\":\"%s\",\"mqtt_connected\":%s,\"wifi_rssi\":%d,\"ip\":\"%s\","
            "\"reset_reason\":\"%s\",\"crash_count\":%lu,\"last_crash_reason\":\"%s\","
            "\"ble_reconnect_delay_ms\":%lu,\"ble_last_reconnect_ms\":%lu,"
            "\"ble_last_notify_ms\":%lu,\"ble_scan_active\":%s,\"ble_last_scan_start_ok\":%s,"
            "\"ble_last_scan_end_reason\":%d,\"ble_last_scan_started_ms\":%lu,\"ble_last_scan_ended_ms\":%lu,"
            "\"ble_last_connect_error\":%d,\"ble_last_disconnect_reason\":%d,\"ble_last_target_connectable\":%s,"
            "\"ble_last_seen_ms\":%lu,\"ble_seen_connectable\":%lu,\"ble_seen_non_connectable\":%lu,"
            "\"ble_connect_attempts\":%lu,\"ble_connect_failures\":%lu}",
            millis() / 1000,
            ESP.getFreeHeap(),
            ESP.getMinFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            bleClient.getStatusStr(),
            mqttHA.isConnected() ? "true" : "false",
            WiFi.RSSI(),
            ipBuf,
            resetReasonToStr(esp_reset_reason()),
            (unsigned long)s_crashCount,
            resetReasonToStr(s_lastCrashReason),
            bleClient.getReconnectDelayMs(),
            bleClient.getLastReconnectMs(),
            bleClient.getLastNotifyMs(),
            NimBLEDevice::getScan()->isScanning() ? "true" : "false",
            bleClient.getLastScanStartOk() ? "true" : "false",
            bleClient.getLastScanEndReason(),
            bleClient.getLastScanStartedMs(),
            bleClient.getLastScanEndedMs(),
            bleClient.getLastConnectError(),
            bleClient.getLastDisconnectReason(),
            bleClient.getLastTargetConnectable() ? "true" : "false",
            bleClient.getLastSeenMs(),
            (unsigned long)bleClient.getSeenConnectableCount(),
            (unsigned long)bleClient.getSeenNonConnectableCount(),
            (unsigned long)bleClient.getConnectAttemptCount(),
            (unsigned long)bleClient.getConnectFailCount())) {
        return 0;
    }

    return used;
}

void WebUI::begin() {
    if (!LittleFS.begin(true)) {
        LOGLN("[WEB] LittleFS mount failed, formatting...");
    }

    loadCrashInfo();

    // Serve static files from LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    _ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        (void)server;
        (void)arg;
        (void)data;
        (void)len;

        if (type == WS_EVT_CONNECT) {
            char json[512];
            const size_t total = buildStateJson(json, sizeof(json));
            if (total > 0) {
                client->text(json, total);
            }
            LOGF("[WEB] WebSocket client #%u connected\n", client->id());
        } else if (type == WS_EVT_DISCONNECT) {
            LOGF("[WEB] WebSocket client #%u disconnected\n", client->id());
        }
    });
    _server.addHandler(&_ws);

    // API: get current state
    _server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
        char json[512];
        const size_t total = buildStateJson(json, sizeof(json));
        if (total == 0) {
            req->send(500, "application/json", "{\"error\":\"state json overflow\"}");
            return;
        }
        req->send(200, "application/json", json);
    });

    // API: get config
    _server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", config.toJson());
    });

    _server.on("/api/rides", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", rideLog.toJson());
    });

    _server.on("/api/rides", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        rideLog.clear();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // TEMP DEBUG: rolling trace of observe() decisions (connection times + odo).
    _server.on("/api/debug/rides", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", rideLog.toDebugJson());
    });

    _server.on("/api/debug/rides", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        rideLog.clearDebug();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // API: save config
    _server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > MAX_CONFIG_BODY_SIZE) {
                req->send(413, "application/json", "{\"error\":\"request too large\"}");
                return;
            }
            if (index != 0 || len != total) {
                req->send(413, "application/json", "{\"error\":\"fragmented request unsupported\"}");
                return;
            }
            String body;
            body.reserve(total);
            body = String((char*)data, len);
            Config updated = config;
            if (updated.fromJson(body)) {
                config = updated;
                config.save();
                req->send(200, "application/json", "{\"ok\":true}");
            } else {
                req->send(400, "application/json", "{\"error\":\"invalid json\"}");
            }
        }
    );

    _server.on("/api/ble/paired", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["paired"] = bleClient.hasStoredAddress();
        AsyncResponseStream* response = req->beginResponseStream("application/json");
        serializeJson(doc, *response);
        req->send(response);
    });

    _server.on("/api/ble/select", HTTP_POST, [](AsyncWebServerRequest* req) {
        bleClient.enable();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/ble/forget", HTTP_POST, [](AsyncWebServerRequest* req) {
        bleClient.forgetPairedDevice();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/light", HTTP_POST, [](AsyncWebServerRequest* req) {},
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total > MAX_LIGHT_BODY_SIZE) {
                req->send(413, "application/json", "{\"error\":\"request too large\"}");
                return;
            }
            if (index != 0 || len != total) {
                req->send(413, "application/json", "{\"error\":\"fragmented request unsupported\"}");
                return;
            }

            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            if (!doc["on"].is<bool>()) {
                req->send(400, "application/json", "{\"error\":\"missing boolean 'on'\"}");
                return;
            }

            const bool on = doc["on"].as<bool>();
            webUI.queueLightCommand(on);
            req->send(202, "application/json", on ? "{\"ok\":true,\"queued\":true,\"light\":\"on\"}" : "{\"ok\":true,\"queued\":true,\"light\":\"off\"}");
        }
    );

    // API: system status
    _server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        char json[1024];
        const size_t total = buildStatusJson(json, sizeof(json));
        if (total == 0) {
            req->send(500, "application/json", "{\"error\":\"status json overflow\"}");
            return;
        }
        req->send(200, "application/json", json);
    });

    // API: restart
    _server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        s_restartRequested = true;
        s_restartAt = millis() + 500;
    });

    _server.begin();
    LOGLN("[WEB] Server started on port 80");
}

void WebUI::loop() {
    _ws.cleanupClients();
    broadcastStateIfChanged();

    if (s_restartRequested && millis() - s_restartAt < 0x80000000UL) {
        LOGLN("[WEB] Restarting by API request");
        ESP.restart();
    }
}

void WebUI::queueLightCommand(bool on) {
    portENTER_CRITICAL(&s_webMux);
    _pendingLightOn = on;
    _pendingLightCmd = true;
    portEXIT_CRITICAL(&s_webMux);
}

bool WebUI::takeLightCommand(bool& on) {
    bool pending;
    portENTER_CRITICAL(&s_webMux);
    pending = _pendingLightCmd;
    if (pending) {
        on = _pendingLightOn;
        _pendingLightCmd = false;
    }
    portEXIT_CRITICAL(&s_webMux);
    return pending;
}

void WebUI::broadcastStateIfChanged() {
    if (_ws.count() == 0) {
        return;
    }

    const unsigned long now = millis();
    if (now - _lastWsBroadcastMs < WS_MIN_BROADCAST_INTERVAL_MS) {
        return;
    }

    char json[sizeof(_lastWsPayload)];
    const size_t total = buildStateJson(json, sizeof(json));
    if (total == 0) {
        return;
    }

    const bool changed = strncmp(json, _lastWsPayload, sizeof(_lastWsPayload)) != 0;
    const bool keepalive = now - _lastWsBroadcastMs >= WS_KEEPALIVE_INTERVAL_MS;
    if (!changed && !keepalive) {
        return;
    }

    strncpy(_lastWsPayload, json, sizeof(_lastWsPayload) - 1);
    _lastWsPayload[sizeof(_lastWsPayload) - 1] = '\0';
    _ws.textAll(json, total);
    _lastWsBroadcastMs = now;
}
