#pragma once
#include <Arduino.h>

// Field IDs inferred from the Tenways app protocol
#define FIELD_SN                      0x07
#define FIELD_SPEED                   0x11
#define FIELD_MAX_SPEED               0x12
#define FIELD_AVG_SPEED               0x13
#define FIELD_TRIP_DISTANCE           0x14
#define FIELD_TRIP_DURATION           0x15
#define FIELD_REMAINING_RANGE         0x16
#define FIELD_TOTAL_RANGE             0x17
#define FIELD_BATTERY                 0x1A
#define FIELD_LIGHT                   0x21

struct BikeState {
    int battery = -1;
    int trip_duration = -1;
    float speed_kmh = -1;
    float max_speed_kmh = -1;
    float avg_speed_kmh = -1;
    float trip_distance_km = -1;
    float remaining_range_km = -1;
    float total_range_km = -1;
    bool light_on = false;
    bool light_valid = false;
    char serial_number[32] = {0};
    char last_connected_at[21] = {0};
    // Timestamp of the most recent telemetry frame that carried the trip
    // counters. Unlike last_connected_at (set once per BLE session at
    // handshake), this advances with every fresh odometer reading, so it is the
    // correct time anchor for ride windows — it can never be ahead of the
    // odometer value it is paired with.
    char last_data_at[21] = {0};

    bool hasBattery() const { return battery >= 0; }

    const char* getDisplaySerial() const;
    bool updateField(uint8_t fieldId, const uint8_t* data, uint8_t len);
    bool setLastConnectedNow();
    bool setLastDataNow();
};
