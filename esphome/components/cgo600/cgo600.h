#pragma once

#ifdef USE_ESP32

#include <string>

#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "cgo600_protocol.h"

namespace esphome::cgo600 {

namespace espbt = esphome::esp32_ble_tracker;

// BLE client for the Tenways CGO600. Unlike the stock ble_client it needs no
// MAC address: it connects to the first advertiser whose name contains
// device_name or that advertises the Nordic UART service.
class CGO600Component : public esp32_ble_client::BLEClientBase {
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

    void set_device_name(const std::string &name) { this->device_name_ = name; }
    void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
    void set_pause_scan_while_connected(bool pause) { this->pause_scan_while_connected_ = pause; }

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

    // Called by the light switch. Returns false when the bike is not connected.
    bool set_light(bool on);

 protected:
    enum class Phase : uint8_t { IDLE, HANDSHAKE, POLLING };

    bool matches_(const espbt::ESPBTDevice &device) const;
    void on_services_ready_();
    void run_handshake_step_(uint32_t now);
    bool write_(const protocol::Command &cmd);
    bool write_(const uint8_t *data, size_t len);
    void request_static_fields_();
    void reset_session_();
    void pause_scanning_();
    void resume_scanning_();
    void publish_state_();
    void update_ble_status_();

    std::string device_name_{"CGO600"};
    uint32_t poll_interval_ms_{1000};
    bool pause_scan_while_connected_{true};

    Phase phase_{Phase::IDLE};
    uint8_t handshake_step_{0};
    uint32_t next_step_at_{0};
    uint32_t last_poll_{0};
    uint32_t last_static_request_{0};
    uint32_t bad_crc_count_{0};
    // After a light command, light reports that still show the old state are
    // ignored until this deadline; the bike applies the change with a delay.
    uint32_t light_pending_until_{0};
    bool light_pending_on_{false};
    bool scan_paused_{false};
    uint16_t tx_handle_{0};
    uint16_t rx_handle_{0};

    protocol::BikeState state_;
    protocol::FrameParser parser_;
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

class CGO600LightSwitch : public switch_::Switch, public Parented<CGO600Component> {
 protected:
    void write_state(bool state) override;
};

}  // namespace esphome::cgo600

#endif  // USE_ESP32
