#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "bike_state.h"

struct RideEntry {
    char window_start[21] = {0};
    char window_end[21] = {0};
    float distance_km = 0.0f;
    int ride_duration_sec = 0;
    int battery_start = -1;
    int battery_end = -1;
    float avg_speed_kmh = -1.0f;
    float max_speed_kmh = -1.0f;
};

class RideLog {
public:
    static constexpr size_t MAX_RIDES_CAP = 100;

    void begin();
    void observe(const BikeState& state);
    void clear();
    void setMaxRides(size_t maxRides);
    String toJson() const;

private:
    // observe() runs on the main loop while the web API (AsyncTCP task) reads
    // and clears the log, so all public entry points take this mutex.
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : _mutex(mutex) { if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY); }
        ~Lock() { if (_mutex) xSemaphoreGive(_mutex); }
    private:
        SemaphoreHandle_t _mutex;
    };

    struct Snapshot {
        bool valid = false;
        char seen_at[21] = {0};
        float distance_km = -1.0f;
        int duration_sec = -1;
        int battery = -1;
    };

    bool loadSnapshot();
    void saveSnapshot() const;
    void rebaseSnapshot(const BikeState& state, float distance, int duration);
    void persistIdle();
    bool loadRides();
    void saveRides() const;
    String buildJson() const;
    static bool hasUsableMetrics(const BikeState& state);
    static float distanceMetric(const BikeState& state);
    void trimToLimit();

    static constexpr unsigned long IDLE_SAVE_INTERVAL_MS = 300000UL; // 5 min

    Snapshot _snapshot;
    RideEntry _rides[MAX_RIDES_CAP];
    size_t _rideCount = 0;
    size_t _maxRides = 20;
    unsigned long _lastIdleSaveMs = 0;
    SemaphoreHandle_t _mutex = nullptr;
};

extern RideLog rideLog;
