#pragma once

#include <Arduino.h>
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

    // TEMP DEBUG: rolling trace of observe() decisions for diagnosing ride windows.
    String toDebugJson() const;
    void clearDebug();

private:
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
    static bool hasUsableMetrics(const BikeState& state);
    static float distanceMetric(const BikeState& state);
    static void copyText(char* dest, size_t destSize, const char* src);
    void trimToLimit();

    // TEMP DEBUG: capture each observe() decision so we can correlate BLE
    // connection timestamps with raw odometer values over the last few days.
    static constexpr size_t DEBUG_CAP = 40;
    struct DebugTrace {
        char at[21] = {0};        // last_data_at (ride-window anchor) at observe time
        char conn_at[21] = {0};   // last_connected_at (BLE session) at observe time
        float distance_km = -1.0f;
        int duration_sec = -1;
        int battery = -1;
        float distance_delta = 0.0f;
        int duration_delta = 0;
        bool moved = false;
        bool counters_reset = false;
        char action[12] = {0};    // first/logged/rebased/battery/ignored
    };
    void traceDebug(const char* action, const BikeState& state, float distanceDelta,
                    int durationDelta, bool moved, bool countersReset);
    void loadDebug();
    void saveDebug() const;

    DebugTrace _debug[DEBUG_CAP];
    size_t _debugCount = 0;
    size_t _debugHead = 0;        // index of next write (oldest when full)

    static constexpr unsigned long IDLE_SAVE_INTERVAL_MS = 300000UL; // 5 min

    Snapshot _snapshot;
    RideEntry _rides[MAX_RIDES_CAP];
    size_t _rideCount = 0;
    size_t _maxRides = 20;
    unsigned long _lastIdleSaveMs = 0;
};

extern RideLog rideLog;
