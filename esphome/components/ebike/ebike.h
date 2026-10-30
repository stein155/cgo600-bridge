#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>

#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "crypto.h"
#include "protocol.h"

namespace esphome::ebike {

namespace espbt = esphome::esp32_ble_tracker;

// BLE client for one e-bike display. Unlike the stock ble_client it needs no
// MAC address: it connects to the first advertiser whose name contains one of
// device_names, or that advertises no name but a known service, and that no
// other block holds. The transport follows from the services: Nordic UART
// (CGO600), King Meter (newer Tenways displays) or Microchip transparent UART
// (B06 displays of the BIKEGO plus app).
class EBikeComponent : public esp32_ble_client::BLEClientBase {
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

    // Key of this bike's state in flash; the block's id.
    void set_storage_key(const std::string &key) { this->storage_key_ = key; }
    void set_device_names(const std::vector<std::string> &names) { this->device_names_ = names; }
    void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
    // 0 keeps the bike connected for as long as it is in range.
    void set_max_connection_time(uint32_t ms) { this->max_connection_ms_ = ms; }

    void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }
    void set_speed_sensor(sensor::Sensor *s) { this->speed_sensor_ = s; }
    void set_max_speed_sensor(sensor::Sensor *s) { this->max_speed_sensor_ = s; }
    void set_avg_speed_sensor(sensor::Sensor *s) { this->avg_speed_sensor_ = s; }
    void set_trip_distance_sensor(sensor::Sensor *s) { this->trip_distance_sensor_ = s; }
    void set_trip_duration_sensor(sensor::Sensor *s) { this->trip_duration_sensor_ = s; }
    void set_remaining_range_sensor(sensor::Sensor *s) { this->remaining_range_sensor_ = s; }
    void set_odometer_sensor(sensor::Sensor *s) { this->odometer_sensor_ = s; }
    void set_power_sensor(sensor::Sensor *s) { this->power_sensor_ = s; }
    void set_serial_number_text_sensor(text_sensor::TextSensor *s) { this->serial_number_text_sensor_ = s; }
    void set_firmware_version_text_sensor(text_sensor::TextSensor *s) { this->firmware_version_text_sensor_ = s; }
    void set_ble_status_text_sensor(text_sensor::TextSensor *s) { this->ble_status_text_sensor_ = s; }
    void set_light_switch(switch_::Switch *s) { this->light_switch_ = s; }

    // Called by the light switch. Returns false when the bike is not connected.
    bool set_light(bool on);

 protected:
    enum class Phase : uint8_t { IDLE, HANDSHAKE, POLLING };
    enum class Transport : uint8_t { NONE, NUS, KING_METER, B06 };

    bool matches_(const espbt::ESPBTDevice &device) const;
    bool claimed_by_other_(uint64_t address) const;
    void on_services_ready_();
    void log_services_();
    void log_bytes_(const char *what, uint16_t handle, const uint8_t *data, size_t len, bool debug);
    void log_km_summary_();
    void run_handshake_step_(uint32_t now);
    void run_b06_handshake_(uint32_t now);
    void handle_b06_auth_();
    bool write_(const protocol::Command &cmd);
    bool write_(const uint8_t *data, size_t len, bool with_response = false);
    void flush_tx_(uint32_t now);
    bool static_fields_missing_() const;
    bool send_frame_(const uint8_t *frame, size_t len);
    bool write_km_read_(const uint8_t *keys, uint8_t count);
    void finish_handshake_(uint32_t now);
    bool is_nus_variant_() const;
    void start_encryption_(uint32_t now);
    bool encryption_pending_(uint32_t now);
    bool handle_enc_handshake_reply_(const uint8_t *data, size_t len);
    void handle_at_reply_(const uint8_t *data, size_t len);
    bool write_nus_read_(const uint8_t *fields, uint8_t count);
    void request_static_fields_();
    void poll_();
    void reset_session_();
    void restore_last_state_();
    void save_last_state_();
    void pause_scanning_();
    void resume_scanning_();
    void publish_state_();
    void update_ble_status_();

    // Every bike block, so blocks never connect to the same bike and scanning
    // only pauses once all of them are connected.
    static std::vector<EBikeComponent *> instances_;
    static bool scan_paused_;

    std::string storage_key_;
    std::vector<std::string> device_names_;
    // Advertised name of the bike being connected; picks the King Meter profile.
    std::string bike_name_;
    uint32_t poll_interval_ms_{1000};
    uint32_t max_connection_ms_{0};
    // The bike disconnected after max_connection_time, and when it last advertised.
    uint64_t resting_address_{0};
    uint32_t resting_seen_ms_{0};

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
    // Per-connection traffic; logged as a periodic summary for King Meter and
    // B06 displays.
    struct LinkStats {
        uint32_t requests{0};
        uint32_t notifications{0};
        uint32_t notification_bytes{0};
        uint32_t bad_crc{0};
        uint32_t unhandled{0};
        uint32_t at_replies{0};
        uint32_t undecryptable{0};
    };
    LinkStats link_stats_;
    const protocol::km::Profile *km_profile_{nullptr};
    uint8_t km_unanswered_polls_{0};
    uint32_t last_km_summary_{0};
    enum class EncHandshake : uint8_t { NONE, WAIT_RANDOM, WAIT_IV, DONE };
    EncHandshake enc_handshake_{EncHandshake::NONE};
    uint32_t enc_deadline_{0};
    uint32_t polling_started_ms_{0};
    // Set once the bike answered a power read; it is then polled with the status.
    bool power_seen_{false};
    // A CGO600 that ignored the app's status read gets the older requests.
    bool nus_legacy_{false};
    uint8_t static_requests_{0};
    uint32_t last_rx_ms_{0};
    static constexpr size_t MAX_WRITE_LEN = 64;
    static constexpr size_t TX_QUEUE_SIZE = 8;
    struct TxFrame {
        uint8_t data[MAX_WRITE_LEN];
        uint8_t len;
        bool with_response;
    };
    TxFrame tx_queue_[TX_QUEUE_SIZE]{};
    uint8_t tx_head_{0};
    uint8_t tx_count_{0};
    uint32_t last_tx_ms_{0};
    // Encrypted displays are written with response, as the Tenways app does.
    bool enc_write_with_response_{false};
    // Models the app always writes with response: some by profile, and every
    // model whose name ends in _D.
    bool km_write_with_response_{false};
    uint8_t enc_app_random_[protocol::HANDSHAKE_VALUE_LEN]{};
    uint8_t enc_session_key_[32]{};
    protocol::Session enc_session_;
    // After a light command, light reports that still show the old state are
    // ignored until this deadline; the bike applies the change with a delay.
    uint32_t light_pending_until_{0};
    bool light_pending_on_{false};
    uint16_t tx_handle_{0};
    uint16_t rx_handle_{0};

    protocol::BikeState state_;
    // Last known telemetry, kept in flash so Home Assistant shows it after a
    // reboot instead of nothing.
    struct LastState {
        uint32_t magic;
        protocol::BikeState state;
    };
    ESPPreferenceObject last_state_pref_;
    bool last_state_dirty_{false};
    uint32_t last_state_saved_ms_{0};
    protocol::FrameParser parser_;
    protocol::km::FrameParser km_parser_;
    protocol::b06::FrameParser b06_parser_;
    const char *last_ble_status_{nullptr};

    sensor::Sensor *battery_sensor_{nullptr};
    sensor::Sensor *speed_sensor_{nullptr};
    sensor::Sensor *max_speed_sensor_{nullptr};
    sensor::Sensor *avg_speed_sensor_{nullptr};
    sensor::Sensor *trip_distance_sensor_{nullptr};
    sensor::Sensor *trip_duration_sensor_{nullptr};
    sensor::Sensor *remaining_range_sensor_{nullptr};
    sensor::Sensor *odometer_sensor_{nullptr};
    sensor::Sensor *power_sensor_{nullptr};
    text_sensor::TextSensor *serial_number_text_sensor_{nullptr};
    text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};
    text_sensor::TextSensor *ble_status_text_sensor_{nullptr};
    switch_::Switch *light_switch_{nullptr};
};

class EBikeLightSwitch : public switch_::Switch, public Parented<EBikeComponent> {
 protected:
    void write_state(bool state) override;
};

}  // namespace esphome::ebike

#endif  // USE_ESP32
