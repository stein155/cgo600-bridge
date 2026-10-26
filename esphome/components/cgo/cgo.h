#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>

#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "cgo_protocol.h"
#include "ride_log.h"

namespace esphome::cgo {

namespace espbt = esphome::esp32_ble_tracker;

// BLE client for Tenways bikes. Unlike the stock ble_client it needs no MAC
// address: it connects to the first advertiser whose name contains one of
// device_names or that advertises a known bike service. The CGO600 speaks its
// protocol over the Nordic UART service; newer displays such as the CGO800's
// use the King Meter service instead.
class CGOComponent : public esp32_ble_client::BLEClientBase {
 public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    bool parse_device(const espbt::ESPBTDevice &device) override;
    bool gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                             esp_ble_gattc_cb_param_t *param) override;
    void set_state(espbt::ClientState state) override;
    void connect() override;
    void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

    void set_device_names(const std::vector<std::string> &names);
    void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
    void set_max_rides(uint16_t max_rides) { this->max_rides_ = max_rides; }
    void add_on_ride_callback(std::function<void(float, uint32_t, int)> &&callback) {
        this->ride_callback_.add(std::move(callback));
    }

    void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }
    void set_speed_sensor(sensor::Sensor *s) { this->speed_sensor_ = s; }
    void set_max_speed_sensor(sensor::Sensor *s) { this->max_speed_sensor_ = s; }
    void set_avg_speed_sensor(sensor::Sensor *s) { this->avg_speed_sensor_ = s; }
    void set_trip_distance_sensor(sensor::Sensor *s) { this->trip_distance_sensor_ = s; }
    void set_trip_duration_sensor(sensor::Sensor *s) { this->trip_duration_sensor_ = s; }
    void set_remaining_range_sensor(sensor::Sensor *s) { this->remaining_range_sensor_ = s; }
    void set_range_sensor(sensor::Sensor *s) { this->range_sensor_ = s; }
    void set_serial_number_text_sensor(text_sensor::TextSensor *s) { this->serial_number_text_sensor_ = s; }
    void set_ble_status_text_sensor(text_sensor::TextSensor *s) { this->ble_status_text_sensor_ = s; }
    void set_light_switch(switch_::Switch *s) { this->light_switch_ = s; }

    // Called by the light switch and the web UI. Returns false when the bike
    // is not connected.
    bool set_light(bool on);

    // Accessors for the web UI.
    // Copy of the telemetry; safe to call from the HTTP server task.
    protocol::BikeState bike_state() const {
        LockGuard guard(this->state_mutex_);
        return this->state_;
    }
    const char *ble_status() const { return this->last_ble_status_ != nullptr ? this->last_ble_status_ : "searching"; }
    // The device names joined with ", ", for display.
    const std::string &device_name() const { return this->device_name_; }
    uint32_t poll_interval_ms() const { return this->poll_interval_ms_; }
    uint32_t last_connected_at() const { return this->last_connected_at_; }
    RideLog &rides() { return this->ride_log_; }

 protected:
    enum class Phase : uint8_t { IDLE, HANDSHAKE, POLLING };
    enum class Transport : uint8_t { NONE, NUS, KING_METER };

    bool matches_(const espbt::ESPBTDevice &device) const;
    void on_services_ready_();
    void subscribe_extra_km_notify_();
    void log_services_();
    void log_bytes_(const char *what, uint16_t handle, const uint8_t *data, size_t len, bool debug);
    void log_km_summary_();
    void run_handshake_step_(uint32_t now);
    bool write_(const protocol::Command &cmd);
    bool write_(const uint8_t *data, size_t len);
    void request_static_fields_();
    void poll_();
    void reset_session_();
    void restore_last_state_();
    void save_last_state_();
    void pause_scanning_();
    void resume_scanning_();
    void publish_state_();
    void update_ble_status_();

    std::vector<std::string> device_names_;
    std::string device_name_;
    uint32_t poll_interval_ms_{1000};

    Phase phase_{Phase::IDLE};
    Transport transport_{Transport::NONE};
    uint8_t handshake_step_{0};
    uint32_t next_step_at_{0};
    uint32_t connect_started_ms_{0};
    uint32_t last_poll_{0};
    uint32_t last_static_request_{0};
    uint32_t bad_crc_count_{0};
    // Handshake waits until the bike confirms the notification subscription.
    uint16_t cccd_handle_{0};
    bool cccd_confirmed_{false};
    uint32_t cccd_deadline_{0};
    // Per-connection King Meter traffic, logged as a periodic summary.
    struct KmStats {
        uint32_t requests{0};
        uint32_t notifications{0};
        uint32_t notification_bytes{0};
        uint32_t extra_notifications{0};
        uint32_t bad_crc{0};
        uint32_t unhandled{0};
    };
    KmStats km_stats_;
    uint32_t last_km_summary_{0};
    // Other notify characteristics in the King Meter service (0xFF03, 0xFF04
    // on the CGO800).
    uint16_t extra_notify_handles_[2]{};
    // After a light command, light reports that still show the old state are
    // ignored until this deadline; the bike applies the change with a delay.
    uint32_t light_pending_until_{0};
    bool light_pending_on_{false};
    bool scan_paused_{false};
    uint16_t tx_handle_{0};
    uint16_t rx_handle_{0};

    protocol::BikeState state_;
    mutable Mutex state_mutex_;
    RideLog ride_log_;
    uint16_t max_rides_{20};
    uint32_t last_connected_at_{0};
    CallbackManager<void(float, uint32_t, int)> ride_callback_;
    // Last known telemetry, kept in flash so the dashboard and Home Assistant
    // show it after a disconnect or reboot instead of nothing.
    struct LastState {
        uint32_t magic;
        uint32_t connected_at;
        protocol::BikeState state;
    };
    ESPPreferenceObject last_state_pref_;
    bool last_state_dirty_{false};
    uint32_t last_state_saved_ms_{0};
    protocol::FrameParser parser_;
    protocol::km::FrameParser km_parser_;
    const char *last_ble_status_{nullptr};

    sensor::Sensor *battery_sensor_{nullptr};
    sensor::Sensor *speed_sensor_{nullptr};
    sensor::Sensor *max_speed_sensor_{nullptr};
    sensor::Sensor *avg_speed_sensor_{nullptr};
    sensor::Sensor *trip_distance_sensor_{nullptr};
    sensor::Sensor *trip_duration_sensor_{nullptr};
    sensor::Sensor *remaining_range_sensor_{nullptr};
    sensor::Sensor *range_sensor_{nullptr};
    text_sensor::TextSensor *serial_number_text_sensor_{nullptr};
    text_sensor::TextSensor *ble_status_text_sensor_{nullptr};
    switch_::Switch *light_switch_{nullptr};
};

// Fires for every inferred ride with distance_km, duration_s and battery_used
// (percentage points, -1 if unknown).
class RideTrigger : public Trigger<float, uint32_t, int> {
 public:
    explicit RideTrigger(CGOComponent *parent) {
        parent->add_on_ride_callback(
            [this](float distance_km, uint32_t duration_s, int battery_used) {
                this->trigger(distance_km, duration_s, battery_used);
            });
    }
};

class CGOLightSwitch : public switch_::Switch, public Parented<CGOComponent> {
 protected:
    void write_state(bool state) override;
};

}  // namespace esphome::cgo

#endif  // USE_ESP32
