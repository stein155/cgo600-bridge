#pragma once

// Scan coordination between BLE bike components on one ESP32. Scanning costs
// connection events while WiFi shares the radio, so it pauses once no bike is
// searching, and runs as long as any bike still is. Other components that ship
// this same file join the same registry: the linker merges the inline
// functions and their statics.
//
// Keep this file identical wherever it is shipped; bump the namespace version
// when its layout changes.

#ifdef USE_ESP32

#include <algorithm>
#include <vector>

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

namespace esphome::bike_scan_v1 {

class ScanUser {
 public:
    // True while this bike still needs advertisements: not connected, or
    // resting and watching for the bike to leave.
    virtual bool needs_scan() const = 0;

 protected:
    ~ScanUser() = default;
};

inline std::vector<const ScanUser *> &users() {
    static std::vector<const ScanUser *> list;
    return list;
}

inline bool &paused() {
    static bool value = false;
    return value;
}

inline void add_user(const ScanUser *user) {
    auto &list = users();
    if (std::find(list.begin(), list.end(), user) == list.end())
        list.push_back(user);
}

// Called by a bike that starts connecting: stops the scan unless another bike
// still needs it.
inline void pause_unless_needed(const ScanUser *self) {
    auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
    if (tracker == nullptr || paused())
        return;
    for (const auto *user : users()) {
        if (user != self && user->needs_scan())
            return;
    }
    paused() = true;
    tracker->stop_scan();
}

// Called by a bike that lost its connection.
inline void resume() {
    auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
    if (tracker == nullptr || !paused())
        return;
    paused() = false;
    // The tracker restarts the scan from its loop once the scanner is idle.
    tracker->set_scan_continuous(true);
}

}  // namespace esphome::bike_scan_v1

#endif  // USE_ESP32
