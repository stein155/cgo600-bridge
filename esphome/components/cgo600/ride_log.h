#pragma once

#include <cstdint>
#include <string>

#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "cgo600_protocol.h"

namespace esphome::cgo600 {

// One inferred ride: the window between two check-ins in which the trip
// counters advanced. Times are UTC epoch seconds.
struct RideRecord {
    uint32_t start{0};
    uint32_t end{0};
    float distance_km{0};
    int32_t duration_s{0};
    int8_t battery_start{-1};
    int8_t battery_end{-1};
    int16_t avg_speed_x10{-1};
    int16_t max_speed_x10{-1};
};

// Infers rides from successive check-ins and keeps them in flash (NVS).
// The bike is only seen on departure and arrival, so a ride is the counter
// difference between the last stationary reading and the next one that moved.
// Public methods lock, because the web UI reads the log from the HTTP task.
class RideLog {
 public:
    static constexpr size_t MAX_RIDES_CAP = 50;

    void setup(size_t max_rides);
    // Feeds a reading whose trip counters were received at data_at. Returns
    // true and fills ride when a ride was logged.
    bool observe(const protocol::BikeState &state, uint32_t data_at, RideRecord &ride);
    void clear();
    // Adds a ride from an import, newest first; duplicates are ignored.
    bool import_ride(const RideRecord &ride);

    size_t count() const {
        LockGuard guard(this->mutex_);
        return this->store_.count;
    }
    size_t max_rides() const { return this->max_rides_; }
    std::string to_json() const;

 protected:
    struct Snapshot {
        uint8_t valid{0};
        int8_t battery{-1};
        uint32_t seen_at{0};
        float distance_km{-1};
        int32_t duration_s{-1};
    };
    struct Store {
        uint32_t magic{0};
        uint8_t count{0};
        Snapshot snapshot;
        RideRecord rides[MAX_RIDES_CAP];
    };

    void rebase_(const protocol::BikeState &state, uint32_t data_at);
    void insert_(const RideRecord &ride);
    void save_();

    mutable Mutex mutex_;
    Store store_;
    ESPPreferenceObject pref_;
    size_t max_rides_{20};
    uint32_t last_idle_save_ms_{0};
};

}  // namespace esphome::cgo600
