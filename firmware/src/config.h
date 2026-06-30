#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Default values, referenced from both the in-class initializers and the
// fallback paths in config.cpp so there is a single source of truth.
#define DEFAULT_MQTT_PREFIX "cgo600"
#define DEFAULT_DEVICE_NAME "CGO600"

struct Config {
    // MQTT
    char mqtt_broker[64] = {0};
    uint16_t mqtt_port = 1883;
    char mqtt_user[32] = {0};
    char mqtt_pass[64] = {0};
    char mqtt_prefix[32] = DEFAULT_MQTT_PREFIX;

    // BLE
    char device_name[32] = DEFAULT_DEVICE_NAME;
    float poll_interval = 1.0f;
    uint16_t max_rides = 20;

    bool hasMqtt() const { return mqtt_broker[0] != '\0'; }

    void load();
    void save();
    String toJson() const;
    bool fromJson(const String& json);
};

extern Config config;
