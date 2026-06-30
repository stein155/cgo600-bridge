#include "ride_log.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cmath>
#include <cstring>

RideLog rideLog;

static constexpr const char* SNAPSHOT_PATH = "/ride_snapshot.json";
static constexpr const char* RIDES_PATH = "/rides.json";
static constexpr const char* DEBUG_PATH = "/ride_debug.json"; // TEMP DEBUG
static constexpr float MIN_DISTANCE_DELTA_KM = 0.1f;
static constexpr int MIN_DURATION_DELTA_SEC = 60;

void RideLog::begin() {
    loadSnapshot();
    loadRides();
    loadDebug();
}

void RideLog::setMaxRides(size_t maxRides) {
    if (maxRides < 10) {
        maxRides = 10;
    } else if (maxRides > MAX_RIDES_CAP) {
        maxRides = MAX_RIDES_CAP;
    }

    _maxRides = maxRides;
    trimToLimit();
}

void RideLog::clear() {
    _snapshot = Snapshot();
    _rideCount = 0;
    LittleFS.remove(SNAPSHOT_PATH);
    LittleFS.remove(RIDES_PATH);
    LOGLN("[BLE] Ride log cleared");
}

void RideLog::clearDebug() {
    _debugCount = 0;
    _debugHead = 0;
    LittleFS.remove(DEBUG_PATH);
    LOGLN("[BLE] Ride debug trace cleared");
}

void RideLog::traceDebug(const char* action, const BikeState& state, float distanceDelta,
                         int durationDelta, bool moved, bool countersReset) {
    DebugTrace& t = _debug[_debugHead];
    t = DebugTrace();
    copyText(t.at, sizeof(t.at), state.last_data_at);
    copyText(t.conn_at, sizeof(t.conn_at), state.last_connected_at);
    t.distance_km = state.trip_distance_km;
    t.duration_sec = state.trip_duration;
    t.battery = state.battery;
    t.distance_delta = distanceDelta;
    t.duration_delta = durationDelta;
    t.moved = moved;
    t.counters_reset = countersReset;
    copyText(t.action, sizeof(t.action), action);

    _debugHead = (_debugHead + 1) % DEBUG_CAP;
    if (_debugCount < DEBUG_CAP) {
        ++_debugCount;
    }
    saveDebug();
}

String RideLog::toDebugJson() const {
    JsonDocument doc;
    doc["snapshot_seen_at"] = _snapshot.seen_at;
    doc["snapshot_distance_km"] = serialized(String(_snapshot.distance_km, 1));
    doc["snapshot_duration_sec"] = _snapshot.duration_sec;
    doc["snapshot_battery"] = _snapshot.battery;
    JsonArray events = doc["events"].to<JsonArray>();

    // Emit newest-first by walking backwards from the most recent write.
    for (size_t i = 0; i < _debugCount; ++i) {
        const size_t idx = (_debugHead + DEBUG_CAP - 1 - i) % DEBUG_CAP;
        const DebugTrace& t = _debug[idx];
        JsonObject e = events.add<JsonObject>();
        e["at"] = t.at;
        e["conn_at"] = t.conn_at;
        e["action"] = t.action;
        e["distance_km"] = serialized(String(t.distance_km, 1));
        e["duration_sec"] = t.duration_sec;
        e["battery"] = t.battery;
        e["distance_delta"] = serialized(String(t.distance_delta, 2));
        e["duration_delta"] = t.duration_delta;
        e["moved"] = t.moved;
        e["counters_reset"] = t.counters_reset;
    }

    String json;
    serializeJson(doc, json);
    return json;
}

void RideLog::loadDebug() {
    File file = LittleFS.open(DEBUG_PATH, "r");
    if (!file) {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        file.close();
        return;
    }
    file.close();

    _debugCount = 0;
    _debugHead = 0;
    JsonArray events = doc["events"].as<JsonArray>();
    // Stored oldest-first; replay so the ring ends with the newest entry.
    for (JsonObject e : events) {
        if (_debugCount >= DEBUG_CAP) {
            break;
        }
        DebugTrace& t = _debug[_debugHead];
        t = DebugTrace();
        copyText(t.at, sizeof(t.at), e["at"] | "");
        copyText(t.conn_at, sizeof(t.conn_at), e["conn_at"] | "");
        copyText(t.action, sizeof(t.action), e["action"] | "");
        t.distance_km = e["distance_km"] | -1.0f;
        t.duration_sec = e["duration_sec"] | -1;
        t.battery = e["battery"] | -1;
        t.distance_delta = e["distance_delta"] | 0.0f;
        t.duration_delta = e["duration_delta"] | 0;
        t.moved = e["moved"] | false;
        t.counters_reset = e["counters_reset"] | false;
        _debugHead = (_debugHead + 1) % DEBUG_CAP;
        ++_debugCount;
    }
}

void RideLog::saveDebug() const {
    File file = LittleFS.open(DEBUG_PATH, "w");
    if (!file) {
        LOGLN("[BLE] Failed to open ride debug for writing");
        return;
    }

    JsonDocument doc;
    JsonArray events = doc["events"].to<JsonArray>();
    // Persist oldest-first so loadDebug() can replay in order.
    for (size_t i = 0; i < _debugCount; ++i) {
        const size_t idx = (_debugHead + DEBUG_CAP - _debugCount + i) % DEBUG_CAP;
        const DebugTrace& t = _debug[idx];
        JsonObject e = events.add<JsonObject>();
        e["at"] = t.at;
        e["conn_at"] = t.conn_at;
        e["action"] = t.action;
        e["distance_km"] = t.distance_km;
        e["duration_sec"] = t.duration_sec;
        e["battery"] = t.battery;
        e["distance_delta"] = t.distance_delta;
        e["duration_delta"] = t.duration_delta;
        e["moved"] = t.moved;
        e["counters_reset"] = t.counters_reset;
    }
    serializeJson(doc, file);
    file.close();
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

    const float distance = distanceMetric(state);
    const int duration = state.trip_duration;
    const bool hasDistance = distance >= 0.0f;
    const bool hasDuration = duration >= 0;

    if (!_snapshot.valid) {
        _snapshot.valid = true;
        copyText(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
        _snapshot.distance_km = distance;
        _snapshot.duration_sec = duration;
        _snapshot.battery = state.battery;
        saveSnapshot();
        traceDebug("first", state, 0.0f, 0, false, false);
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
        traceDebug("rebased", state, distanceDelta, durationDelta, moved, countersReset);
        return;
    }

    if (moved) {
        RideEntry entry;
        copyText(entry.window_start, sizeof(entry.window_start), _snapshot.seen_at);
        copyText(entry.window_end, sizeof(entry.window_end), state.last_data_at);
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
        traceDebug("logged", state, distanceDelta, durationDelta, moved, countersReset);
        LOGF("[BLE] Ride logged: %.1f km, %d sec\n", entry.distance_km, entry.ride_duration_sec);
        return;
    }

    // Connected but the odometer has not advanced (e.g. parked at the dock).
    // Advance only the time anchor and battery baseline so the next ride's
    // window_start tracks the last moment the bike was seen stationary — this
    // is the best proxy for the departure time. The odometer baseline stays
    // put, so the next real movement is measured against the correct value.
    _snapshot.battery = state.battery;
    copyText(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
    persistIdle();
    traceDebug("idle", state, distanceDelta, durationDelta, moved, countersReset);
}

void RideLog::rebaseSnapshot(const BikeState& state, float distance, int duration) {
    _snapshot.valid = true;
    copyText(_snapshot.seen_at, sizeof(_snapshot.seen_at), state.last_data_at);
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
    copyText(_snapshot.seen_at, sizeof(_snapshot.seen_at), doc["seen_at"] | "");
    _snapshot.distance_km = doc["distance_km"] | -1.0f;
    _snapshot.duration_sec = doc["duration_sec"] | -1;
    _snapshot.battery = doc["battery"] | -1;
    return _snapshot.valid;
}

void RideLog::saveSnapshot() const {
    File file = LittleFS.open(SNAPSHOT_PATH, "w");
    if (!file) {
        LOGLN("[BLE] Failed to open ride snapshot for writing");
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
        copyText(entry.window_start, sizeof(entry.window_start), ride["window_start"] | "");
        copyText(entry.window_end, sizeof(entry.window_end), ride["window_end"] | "");
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
        LOGLN("[BLE] Failed to open ride log for writing");
        return;
    }

    file.print(toJson());
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

void RideLog::copyText(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}
