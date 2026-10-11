#include "ride_log.h"
#include "logger.h"
#include "util.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cmath>
#include <cstring>

RideLog rideLog;

static constexpr const char* SNAPSHOT_PATH = "/ride_snapshot.json";
static constexpr const char* RIDES_PATH = "/rides.json";
static constexpr const char* LEGACY_DEBUG_PATH = "/ride_debug.json";
static constexpr float MIN_DISTANCE_DELTA_KM = 0.1f;
static constexpr int MIN_DURATION_DELTA_SEC = 60;

void RideLog::begin() {
    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();
    }
    Lock lock(_mutex);
    loadSnapshot();
    loadRides();
    // Remove the trace file left behind by older debug firmware.
    if (LittleFS.exists(LEGACY_DEBUG_PATH)) {
        LittleFS.remove(LEGACY_DEBUG_PATH);
    }
}

void RideLog::setMaxRides(size_t maxRides) {
    Lock lock(_mutex);
    if (maxRides < 10) {
        maxRides = 10;
    } else if (maxRides > MAX_RIDES_CAP) {
        maxRides = MAX_RIDES_CAP;
    }

    _maxRides = maxRides;
    trimToLimit();
}

void RideLog::clear() {
    Lock lock(_mutex);
    _snapshot = Snapshot();
    _rideCount = 0;
    LittleFS.remove(SNAPSHOT_PATH);
    LittleFS.remove(RIDES_PATH);
    LOGLN("[RIDE] Ride log cleared");
}

void RideLog::observe(const BikeState& state) {
    // The ride window is anchored to last_data_at, the timestamp of the
    // telemetry frame that delivered these odometer counters. Anchoring to
    // last_connected_at (the BLE session handshake) was the root cause of the
    // "5 to 5" windows: on reconnect the session time jumped to the return
    // moment while the odometer was still the morning value, so the start
    // anchor collapsed onto the end. last_data_at can never run ahead of the
    // counters it is paired with, which keeps the window honest.
    if (!hasUsableMetrics(state) || !state.last_data_at[0]) {
        return;
    }

    Lock lock(_mutex);

    const float distance = distanceMetric(state);
    const int duration = state.trip_duration;
    const bool hasDistance = distance >= 0.0f;
    const bool hasDuration = duration >= 0;

    if (!_snapshot.valid) {
        _snapshot.valid = true;
        copyString(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
        _snapshot.distance_km = distance;
        _snapshot.duration_sec = duration;
        _snapshot.battery = state.battery;
        saveSnapshot();
        return;
    }

    const float distanceDelta = (hasDistance && _snapshot.distance_km >= 0.0f) ? distance - _snapshot.distance_km : 0.0f;
    const int durationDelta = (hasDuration && _snapshot.duration_sec >= 0) ? duration - _snapshot.duration_sec : 0;
    // A real ride always covers ground. Requiring distance (not just a duration
    // bump) suppresses the phantom "0.0 km / 60 s" entries that the duration
    // threshold alone produced. Fall back to duration only when distance is
    // unavailable from the bike.
    const bool moved = hasDistance ? distanceDelta >= MIN_DISTANCE_DELTA_KM
                                   : durationDelta >= MIN_DURATION_DELTA_SEC;
    const bool countersReset = distanceDelta < -0.05f || durationDelta < -5;

    if (countersReset) {
        // The bike reset its trip counters; rebase the baseline to now without
        // logging a ride.
        rebaseSnapshot(state, distance, duration);
        return;
    }

    if (moved) {
        RideEntry entry;
        copyString(entry.window_start, sizeof(entry.window_start), _snapshot.seen_at);
        copyString(entry.window_end, sizeof(entry.window_end), state.last_data_at);
        entry.distance_km = distanceDelta > 0.0f ? distanceDelta : 0.0f;
        entry.ride_duration_sec = durationDelta > 0 ? durationDelta : 0;
        entry.battery_start = _snapshot.battery;
        entry.battery_end = state.battery;
        entry.avg_speed_kmh = state.avg_speed_kmh;
        entry.max_speed_kmh = state.max_speed_kmh;

        if (_rideCount < _maxRides) {
            ++_rideCount;
        }
        for (size_t i = _rideCount - 1; i > 0; --i) {
            _rides[i] = _rides[i - 1];
        }
        _rides[0] = entry;
        saveRides();
        rebaseSnapshot(state, distance, duration);
        LOGF("[RIDE] Ride logged: %.1f km, %d sec\n", entry.distance_km, entry.ride_duration_sec);
        return;
    }

    // Connected but the odometer has not advanced (e.g. parked at the dock).
    // Advance only the time anchor and battery baseline so the next ride's
    // window_start tracks the last moment the bike was seen stationary — this
    // is the best proxy for the departure time. The odometer baseline stays
    // put, so the next real movement is measured against the correct value.
    _snapshot.battery = state.battery;
    copyString(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
    persistIdle();
}

void RideLog::rebaseSnapshot(const BikeState& state, float distance, int duration) {
    _snapshot.valid = true;
    copyString(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
    _snapshot.distance_km = distance;
    _snapshot.duration_sec = duration;
    _snapshot.battery = state.battery;
    saveSnapshot();
}

void RideLog::persistIdle() {
    // The idle branch runs on every poll while parked, so throttle flash writes;
    // losing a few minutes of anchor precision across a reboot is harmless.
    const unsigned long now = millis();
    if (_lastIdleSaveMs != 0 && now - _lastIdleSaveMs < IDLE_SAVE_INTERVAL_MS) {
        return;
    }
    _lastIdleSaveMs = now;
    saveSnapshot();
}

String RideLog::toJson() const {
    Lock lock(_mutex);
    return buildJson();
}

String RideLog::buildJson() const {
    JsonDocument doc;
    JsonArray rides = doc["rides"].to<JsonArray>();

    for (size_t i = 0; i < _rideCount; ++i) {
        JsonObject ride = rides.add<JsonObject>();
        ride["window_start"] = _rides[i].window_start;
        ride["window_end"] = _rides[i].window_end;
        ride["distance_km"] = serialized(String(_rides[i].distance_km, 1));
        ride["ride_duration_sec"] = _rides[i].ride_duration_sec;
        if (_rides[i].battery_start >= 0) ride["battery_start"] = _rides[i].battery_start;
        if (_rides[i].battery_end >= 0) ride["battery_end"] = _rides[i].battery_end;
        if (_rides[i].avg_speed_kmh >= 0.0f) ride["avg_speed_kmh"] = serialized(String(_rides[i].avg_speed_kmh, 1));
        if (_rides[i].max_speed_kmh >= 0.0f) ride["max_speed_kmh"] = serialized(String(_rides[i].max_speed_kmh, 1));
    }

    String json;
    serializeJson(doc, json);
    return json;
}

bool RideLog::loadSnapshot() {
    File file = LittleFS.open(SNAPSHOT_PATH, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        file.close();
        return false;
    }
    file.close();

    _snapshot.valid = doc["valid"] | false;
    copyString(_snapshot.seen_at, sizeof(_snapshot.seen_at), doc["seen_at"] | "");
    _snapshot.distance_km = doc["distance_km"] | -1.0f;
    _snapshot.duration_sec = doc["duration_sec"] | -1;
    _snapshot.battery = doc["battery"] | -1;
    return _snapshot.valid;
}

void RideLog::saveSnapshot() const {
    File file = LittleFS.open(SNAPSHOT_PATH, "w");
    if (!file) {
        LOGLN("[RIDE] Failed to open ride snapshot for writing");
        return;
    }

    JsonDocument doc;
    doc["valid"] = _snapshot.valid;
    doc["seen_at"] = _snapshot.seen_at;
    doc["distance_km"] = _snapshot.distance_km;
    doc["duration_sec"] = _snapshot.duration_sec;
    doc["battery"] = _snapshot.battery;
    serializeJson(doc, file);
    file.close();
}

bool RideLog::loadRides() {
    File file = LittleFS.open(RIDES_PATH, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        file.close();
        return false;
    }
    file.close();

    _rideCount = 0;
    JsonArray rides = doc["rides"].as<JsonArray>();
    for (JsonObject ride : rides) {
        if (_rideCount >= MAX_RIDES_CAP) {
            break;
        }
        RideEntry& entry = _rides[_rideCount++];
        copyString(entry.window_start, sizeof(entry.window_start), ride["window_start"] | "");
        copyString(entry.window_end, sizeof(entry.window_end), ride["window_end"] | "");
        entry.distance_km = ride["distance_km"] | 0.0f;
        entry.ride_duration_sec = ride["ride_duration_sec"] | 0;
        entry.battery_start = ride["battery_start"] | -1;
        entry.battery_end = ride["battery_end"] | -1;
        entry.avg_speed_kmh = ride["avg_speed_kmh"] | -1.0f;
        entry.max_speed_kmh = ride["max_speed_kmh"] | -1.0f;
    }
    return true;
}

void RideLog::saveRides() const {
    File file = LittleFS.open(RIDES_PATH, "w");
    if (!file) {
        LOGLN("[RIDE] Failed to open ride log for writing");
        return;
    }

    file.print(buildJson());
    file.close();
}

void RideLog::trimToLimit() {
    if (_rideCount <= _maxRides) {
        return;
    }

    _rideCount = _maxRides;
    saveRides();
}

bool RideLog::hasUsableMetrics(const BikeState& state) {
    return state.trip_distance_km >= 0.0f || state.trip_duration >= 0;
}

float RideLog::distanceMetric(const BikeState& state) {
    return state.trip_distance_km;
}
