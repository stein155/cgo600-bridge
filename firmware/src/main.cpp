#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <esp_system.h>

#include "config.h"
#include "ble_client.h"
#include "logger.h"
#include "mqtt_ha.h"
#include "ride_log.h"
#include "reset_reason.h"
#include "web_server.h"

static portMUX_TYPE rideStateMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool pendingRideState = false;
static BikeState rideStateSnapshot;

// Pending light command — set from MQTT callback (AsyncTCP task, Core 0),
// consumed in loop() (main task, Core 1). Avoids calling BLE from wrong task.
static portMUX_TYPE lightCmdMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool pendingLightCmd = false;
static volatile bool pendingLightOn  = false;

static void logAndRecordCrash() {
    const esp_reset_reason_t reason = esp_reset_reason();
    LOGF("[BOOT] Reset reason: %s\n", resetReasonToStr(reason));

    Preferences crashPrefs;
    crashPrefs.begin("crash", false);
    const uint32_t prevCount  = crashPrefs.getUInt("count", 0);
    const uint8_t  prevReason = crashPrefs.getUChar("reason", 0);

    if (prevCount > 0 || prevReason != 0) {
        LOGF("[BOOT] Previous crash: reason=%s count=%u\n",
            resetReasonToStr((esp_reset_reason_t)prevReason), prevCount);
    }

    const bool isCrash = (reason == ESP_RST_PANIC ||
                          reason == ESP_RST_INT_WDT ||
                          reason == ESP_RST_TASK_WDT ||
                          reason == ESP_RST_WDT);
    if (isCrash) {
        crashPrefs.putUInt("count", prevCount + 1);
    } else {
        crashPrefs.putUInt("count", 0);
    }
    crashPrefs.putUChar("reason", (uint8_t)reason);
    crashPrefs.end();
}

static void initClock() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    LOGLN("[BOOT] NTP sync started (UTC)");
}

// Callback: BLE state changed → push to MQTT.
// Called from NimBLE task (Core 0). Must not call MQTT directly.
// mqttHA.publishState() is safe: it only sets a flag, publishes from loop().
void onBikeStateChange(const BikeState& state) {
    mqttHA.publishState(state);

    portENTER_CRITICAL(&rideStateMux);
    rideStateSnapshot = state;
    pendingRideState = true;
    portEXIT_CRITICAL(&rideStateMux);
}

void setup() {
    Serial.begin(115200);
    logger.begin();
    LOGLN("=== CGO600 ESP32 Bridge ===");

    logAndRecordCrash();

    // Load config from NVS
    config.load();
    LOGLN("[BOOT] Config loaded");

    // WiFiManager — creates AP "CGO600-Setup" if no WiFi configured.
    // WiFi must come before BLE so ESP32 coexistence controller is ready.
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(15);

    if (!wm.autoConnect("CGO600-Setup")) {
        LOGLN("[BOOT] WiFi failed, restarting...");
        delay(3000);
        ESP.restart();
    }

    LOGF("[BOOT] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    initClock();

    // mDNS
    if (MDNS.begin("cgo600")) {
        LOGLN("[BOOT] mDNS: cgo600.local");
        MDNS.addService("http", "tcp", 80);
    }

    // OTA
    ArduinoOTA.setHostname("cgo600");
    ArduinoOTA.onStart([]() { LOGLN("[OTA] Start"); });
    ArduinoOTA.onEnd([]() { LOGLN("[OTA] Done"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        LOGF("[OTA] %u%%", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        LOGF("[OTA] Error %u", error);
    });
    ArduinoOTA.begin();
    LOGLN("[OTA] Enabled");

    // Web server
    webUI.begin();
    rideLog.begin();
    rideLog.setMaxRides(config.max_rides);

    // MQTT (only if configured)
    if (config.hasMqtt()) {
        // Light command callback: only set a flag here.
        // Actual BLE write happens in loop() on the main task.
        // Calling bleClient.setLight() from AsyncTCP task (Core 0) races
        // with BLE operations on Core 1 → panic.
        mqttHA.setLightCommandCallback([](bool on) -> bool {
            portENTER_CRITICAL(&lightCmdMux);
            pendingLightOn  = on;
            pendingLightCmd = true;
            portEXIT_CRITICAL(&lightCmdMux);
            return true;
        });
        mqttHA.begin(config.mqtt_broker, config.mqtt_port,
                     config.mqtt_user, config.mqtt_pass,
                     config.mqtt_prefix);
    }

    // BLE — after WiFi so coexistence controller is fully initialised
    bleClient.setCallback(onBikeStateChange);
    bleClient.setPollInterval(config.poll_interval);
    bleClient.setWifiReady(true);
    bleClient.begin(config.device_name);
}

void loop() {
    ArduinoOTA.handle();
    webUI.loop();
    bleClient.loop();
    mqttHA.loop();

    if (pendingRideState) {
        BikeState snapshot;
        portENTER_CRITICAL(&rideStateMux);
        snapshot = rideStateSnapshot;
        pendingRideState = false;
        portEXIT_CRITICAL(&rideStateMux);
        rideLog.observe(snapshot);
    }

    // Execute deferred light command from MQTT (must run on main task)
    if (pendingLightCmd) {
        portENTER_CRITICAL(&lightCmdMux);
        const bool lightOn = pendingLightOn;
        pendingLightCmd = false;
        portEXIT_CRITICAL(&lightCmdMux);
        bleClient.setLight(lightOn);
    }

    bool webLightOn = false;
    if (webUI.takeLightCommand(webLightOn)) {
        bleClient.setLight(webLightOn);
    }

    // Deduplicated inside MqttHA; calling every loop also re-sends after an
    // MQTT reconnect.
    mqttHA.publishBleStatus(bleClient.getStatusStr());
}
