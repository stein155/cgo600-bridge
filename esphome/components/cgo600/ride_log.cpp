#include "ride_log.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::cgo600 {

static const char *const TAG = "cgo600.rides";

// Bump when the Store layout changes; older data is then discarded.
static constexpr uint32_t STORE_MAGIC = 0x43473601;
static constexpr float MIN_DISTANCE_DELTA_KM = 0.1f;
static constexpr int32_t MIN_DURATION_DELTA_S = 60;
// The idle branch runs on every poll while parked, so throttle flash writes;
// losing a few minutes of anchor precision across a reboot is harmless.
static constexpr uint32_t IDLE_SAVE_INTERVAL_MS = 5 * 60 * 1000;

void RideLog::setup(size_t max_rides) {
    this->max_rides_ = std::min(std::max<size_t>(max_rides, 1), MAX_RIDES_CAP);
    this->pref_ = global_preferences->make_preference<Store>(fnv1_hash("cgo600_rides"));
    if (!this->pref_.load(&this->store_) || this->store_.magic != STORE_MAGIC) {
        this->store_ = Store();
        this->store_.magic = STORE_MAGIC;
    }
    if (this->store_.count > this->max_rides_) {
        this->store_.count = this->max_rides_;
    }
    ESP_LOGD(TAG, "Loaded %u rides", this->store_.count);
}

bool RideLog::observe(const protocol::BikeState &state, uint32_t data_at, RideRecord &ride) {
    LockGuard guard(this->mutex_);
    const float distance = state.trip_distance_km;
    const int32_t duration = state.trip_duration;
    const bool has_distance = distance >= 0.0f;
    const bool has_duration = duration >= 0;
    if (data_at == 0 || (!has_distance && !has_duration)) {
        return false;
    }

    auto &snap = this->store_.snapshot;
    if (!snap.valid) {
        this->rebase_(state, data_at);
        return false;
    }

    const float distance_delta = (has_distance && snap.distance_km >= 0.0f) ? distance - snap.distance_km : 0.0f;
    const int32_t duration_delta = (has_duration && snap.duration_s >= 0) ? duration - snap.duration_s : 0;
    // A real ride always covers ground; fall back to duration only when the
    // bike does not report distance.
    const bool moved = has_distance ? distance_delta >= MIN_DISTANCE_DELTA_KM : duration_delta >= MIN_DURATION_DELTA_S;
    const bool counters_reset = distance_delta < -0.05f || duration_delta < -5;

    if (counters_reset) {
        ESP_LOGD(TAG, "Trip counters reset, rebasing");
        this->rebase_(state, data_at);
        return false;
    }

    if (moved) {
        ride = RideRecord();
        ride.start = snap.seen_at;
        ride.end = data_at;
        ride.distance_km = distance_delta > 0.0f ? distance_delta : 0.0f;
        ride.duration_s = duration_delta > 0 ? duration_delta : 0;
        ride.battery_start = snap.battery;
        ride.battery_end = (int8_t) state.battery;
        // The bike resets its speed figures when switched on. A zero top speed
        // means this reading is a departure and the arrival was missed, so the
        // ride's speeds are gone.
        if (state.max_speed_kmh > 0.0f) {
            ride.avg_speed_x10 = state.avg_speed_kmh >= 0 ? (int16_t) (state.avg_speed_kmh * 10 + 0.5f) : -1;
            ride.max_speed_x10 = (int16_t) (state.max_speed_kmh * 10 + 0.5f);
        } else {
            ride.avg_speed_x10 = -1;
            ride.max_speed_x10 = -1;
        }
        this->insert_(ride);
        this->rebase_(state, data_at);
        global_preferences->sync();
        ESP_LOGI(TAG, "Ride logged: %.1f km, %d s", ride.distance_km, (int) ride.duration_s);
        return true;
    }

    // Parked: advance only the time anchor and battery baseline, so the next
    // ride's start tracks the last moment the bike was seen stationary.
    snap.battery = (int8_t) state.battery;
    snap.seen_at = data_at;
    const uint32_t now = millis();
    if (this->last_idle_save_ms_ == 0 || now - this->last_idle_save_ms_ >= IDLE_SAVE_INTERVAL_MS) {
        this->last_idle_save_ms_ = now;
        this->save_();
    }
    return false;
}

void RideLog::rebase_(const protocol::BikeState &state, uint32_t data_at) {
    auto &snap = this->store_.snapshot;
    snap.valid = 1;
    snap.seen_at = data_at;
    snap.distance_km = state.trip_distance_km;
    snap.duration_s = state.trip_duration;
    snap.battery = (int8_t) state.battery;
    this->save_();
}

void RideLog::insert_(const RideRecord &ride) {
    // Rides are kept newest first; keep the order when importing older ones.
    size_t pos = 0;
    while (pos < this->store_.count && this->store_.rides[pos].end > ride.end) {
        ++pos;
    }
    if (pos >= this->max_rides_) {
        return;  // older than everything we keep
    }
    size_t last = std::min<size_t>(this->store_.count, this->max_rides_ - 1);
    for (size_t i = last; i > pos; --i) {
        this->store_.rides[i] = this->store_.rides[i - 1];
    }
    this->store_.rides[pos] = ride;
    if (this->store_.count < this->max_rides_) {
        ++this->store_.count;
    }
}

bool RideLog::import_ride(const RideRecord &ride) {
    LockGuard guard(this->mutex_);
    if (ride.end == 0 || ride.start > ride.end) {
        return false;
    }
    for (size_t i = 0; i < this->store_.count; ++i) {
        if (this->store_.rides[i].start == ride.start && this->store_.rides[i].end == ride.end) {
            return false;
        }
    }
    this->insert_(ride);
    this->save_();
    return true;
}

void RideLog::clear() {
    LockGuard guard(this->mutex_);
    this->store_ = Store();
    this->store_.magic = STORE_MAGIC;
    this->save_();
    global_preferences->sync();
    ESP_LOGI(TAG, "Ride log cleared");
}

void RideLog::save_() {
    if (!this->pref_.save(&this->store_)) {
        ESP_LOGW(TAG, "Saving ride log failed");
    }
}

static void format_iso(uint32_t epoch, char *out, size_t size) {
    const time_t t = epoch;
    struct tm utc;
    gmtime_r(&t, &utc);
    strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

std::string RideLog::to_json() const {
    LockGuard guard(this->mutex_);
    std::string json = "{\"rides\":[";
    char buf[256];
    char start[24];
    char end[24];
    for (size_t i = 0; i < this->store_.count; ++i) {
        const auto &r = this->store_.rides[i];
        format_iso(r.start, start, sizeof(start));
        format_iso(r.end, end, sizeof(end));
        int n = snprintf(buf, sizeof(buf), "%s{\"window_start\":\"%s\",\"window_end\":\"%s\",\"distance_km\":%.1f,"
                         "\"ride_duration_sec\":%d",
                         i == 0 ? "" : ",", start, end, r.distance_km, (int) r.duration_s);
        if (r.battery_start >= 0)
            n += snprintf(buf + n, sizeof(buf) - n, ",\"battery_start\":%d", r.battery_start);
        if (r.battery_end >= 0)
            n += snprintf(buf + n, sizeof(buf) - n, ",\"battery_end\":%d", r.battery_end);
        if (r.avg_speed_x10 >= 0)
            n += snprintf(buf + n, sizeof(buf) - n, ",\"avg_speed_kmh\":%.1f", r.avg_speed_x10 / 10.0f);
        if (r.max_speed_x10 >= 0)
            n += snprintf(buf + n, sizeof(buf) - n, ",\"max_speed_kmh\":%.1f", r.max_speed_x10 / 10.0f);
        snprintf(buf + n, sizeof(buf) - n, "}");
        json += buf;
    }
    json += "]}";
    return json;
}

}  // namespace esphome::cgo600
