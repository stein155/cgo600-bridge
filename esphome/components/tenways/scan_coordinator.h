#pragma once

// Scan coordination between BLE bike components on one ESP32.
//
// Bikes are often in range for only a few seconds, so the scan runs at its
// full configured window as long as any bike is searching. Two things switch
// it off:
// - A bike is being set up (open, service discovery, subscribe, handshake).
//   Scanning starves that link, so the scan stays off until the last bike in
//   setup is ready or has dropped, even when other bikes are still searching.
// - Every bike is connected: nothing is left to find, and the radio is left
//   to the connections and WiFi.
//
// Other components that ship this same file join the same registry: the
// linker merges the inline functions and their statics.
//
// Keep this file identical wherever it is shipped; bump the namespace version
// when its layout changes.

#ifdef USE_ESP32

#include <algorithm>
#include <vector>

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

namespace esphome::bike_scan_v2 {

class ScanUser {
 public:
    // True while this bike still needs advertisements: it is searching, or
    // resting and watching for the bike to leave. A bike that is being set up
    // is never asked.
    virtual bool needs_scan() const = 0;

 protected:
    ~ScanUser() = default;
};

inline std::vector<const ScanUser *> &users() {
    static std::vector<const ScanUser *> list;
    return list;
}

// The bikes between connect() and the end of their setup.
inline std::vector<const ScanUser *> &in_setup() {
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

// Applies the scan state the bikes want now. The tracker restarts a wanted
// scan from its loop, which only looks again after a client changed state: a
// caller whose client state did not change has to notify the tracker itself.
inline void update() {
    auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
    if (tracker == nullptr)
        return;
    bool wanted = false;
    if (in_setup().empty()) {
        for (const auto *user : users()) {
            if (user->needs_scan()) {
                wanted = true;
                break;
            }
        }
    }
    if (wanted != paused())
        return;
    paused() = !wanted;
    if (wanted) {
        tracker->set_scan_continuous(true);
    } else {
        // Also keeps the tracker from restarting the scan once the client is
        // connected.
        tracker->stop_scan();
    }
}

// Called by a bike that starts connecting: the scan stays off until its
// end_setup().
inline void begin_setup(const ScanUser *user) {
    auto &list = in_setup();
    if (std::find(list.begin(), list.end(), user) == list.end())
        list.push_back(user);
    update();
}

// Called by a bike whose setup ended, ready or not, and whenever its
// needs_scan() may have changed. Safe to call for a bike that is not in setup.
inline void end_setup(const ScanUser *user) {
    auto &list = in_setup();
    list.erase(std::remove(list.begin(), list.end(), user), list.end());
    update();
}

}  // namespace esphome::bike_scan_v2

#endif  // USE_ESP32
