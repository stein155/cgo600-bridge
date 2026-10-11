#include "cgo600.h"

#ifdef USE_ESP32

#include <cctype>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cgo600 {

static const char *const TAG = "cgo600";

static constexpr uint32_t SUBSCRIBE_SETTLE_MS = 50;
static constexpr uint32_t HANDSHAKE_STEP_MS = 120;
static constexpr uint32_t STATIC_REQUEST_INTERVAL_MS = 15000;
static constexpr size_t MAX_WRITE_LEN = 32;
static constexpr uint32_t LIGHT_CONFIRM_WINDOW_MS = 2000;
// Connection parameters requested from the bike (units: 1.25 ms, 10 ms). A
// relaxed interval leaves airtime for WiFi; the 10 s supervision timeout rides
// out short radio hiccups that otherwise drop the link with reason 0x08.
static constexpr uint16_t CONN_MIN_INTERVAL = 0x18;  // 30 ms
static constexpr uint16_t CONN_MAX_INTERVAL = 0x28;  // 50 ms
static constexpr uint16_t CONN_LATENCY = 0;
static constexpr uint16_t CONN_SUPERVISION_TIMEOUT = 1000;  // 10 s

static const espbt::ESPBTUUID NUS_SERVICE_UUID = espbt::ESPBTUUID::from_raw("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static const espbt::ESPBTUUID NUS_RX_UUID = espbt::ESPBTUUID::from_raw("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static const espbt::ESPBTUUID NUS_TX_UUID = espbt::ESPBTUUID::from_raw("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static bool contains_ignore_case(const std::string &haystack, const std::string &needle) {
    if (needle.empty() || needle.size() > haystack.size())
        return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() &&
               std::tolower((unsigned char) haystack[i + j]) == std::tolower((unsigned char) needle[j])) {
            ++j;
        }
        if (j == needle.size())
            return true;
    }
    return false;
}

void CGO600Component::setup() {
    BLEClientBase::setup();
    this->update_ble_status_();
}

void CGO600Component::dump_config() {
    ESP_LOGCONFIG(TAG, "CGO600:");
    ESP_LOGCONFIG(TAG, "  Device name: %s", this->device_name_.c_str());
    ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", (unsigned) this->poll_interval_ms_);
    LOG_SENSOR("  ", "Battery", this->battery_sensor_);
    LOG_SENSOR("  ", "Speed", this->speed_sensor_);
    LOG_SENSOR("  ", "Max speed", this->max_speed_sensor_);
    LOG_SENSOR("  ", "Avg speed", this->avg_speed_sensor_);
    LOG_SENSOR("  ", "Trip distance", this->trip_distance_sensor_);
    LOG_SENSOR("  ", "Trip duration", this->trip_duration_sensor_);
    LOG_SENSOR("  ", "Remaining range", this->remaining_range_sensor_);
    LOG_SENSOR("  ", "Range", this->range_sensor_);
    LOG_TEXT_SENSOR("  ", "Serial number", this->serial_number_text_sensor_);
    LOG_TEXT_SENSOR("  ", "BLE status", this->ble_status_text_sensor_);
    LOG_SWITCH("  ", "Light", this->light_switch_);
}

bool CGO600Component::matches_(const espbt::ESPBTDevice &device) const {
    if (contains_ignore_case(device.get_name(), this->device_name_))
        return true;
    // The bike sometimes omits its name; the Nordic UART service still
    // identifies the controller's BLE bridge.
    for (const auto &uuid : device.get_service_uuids()) {
        if (uuid == NUS_SERVICE_UUID)
            return true;
    }
    return false;
}

bool CGO600Component::parse_device(const espbt::ESPBTDevice &device) {
    if (this->state() != espbt::ClientState::IDLE || !this->matches_(device))
        return false;

    ESP_LOGI(TAG, "Found bike '%s' [%s], connecting", device.get_name().c_str(), device.address_str().c_str());
    this->set_address(device.address_uint64());
    this->set_remote_addr_type(device.get_address_type());
    this->set_state(espbt::ClientState::DISCOVERED);
    return true;
}

void CGO600Component::set_state(espbt::ClientState state) {
    BLEClientBase::set_state(state);
    if (state == espbt::ClientState::IDLE) {
        this->reset_session_();
    }
    this->update_ble_status_();
}

void CGO600Component::connect() {
    this->set_conn_params_(CONN_MIN_INTERVAL, CONN_MAX_INTERVAL, CONN_LATENCY, CONN_SUPERVISION_TIMEOUT, "cgo600");
    BLEClientBase::connect();
}

void CGO600Component::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    BLEClientBase::gap_event_handler(event, param);
    if (event == ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT && this->check_addr(param->update_conn_params.bda)) {
        ESP_LOGD(TAG, "Connection params: interval=%.2f ms latency=%u timeout=%u ms (status=%d)",
                 param->update_conn_params.conn_int * 1.25f, param->update_conn_params.latency,
                 param->update_conn_params.timeout * 10, param->update_conn_params.status);
    }
}

bool CGO600Component::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
    if (!BLEClientBase::gattc_event_handler(event, gattc_if, param))
        return false;

    switch (event) {
        case ESP_GATTC_SEARCH_CMPL_EVT:
            this->on_services_ready_();
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            if (param->reg_for_notify.handle != this->tx_handle_)
                break;
            if (param->reg_for_notify.status != ESP_GATT_OK) {
                ESP_LOGW(TAG, "Subscribing to notifications failed (status=%d)", param->reg_for_notify.status);
                this->disconnect();
                break;
            }
            ESP_LOGD(TAG, "Subscribed, starting handshake");
            this->pause_scanning_();
            this->phase_ = Phase::HANDSHAKE;
            this->handshake_step_ = 0;
            this->next_step_at_ = millis() + SUBSCRIBE_SETTLE_MS;
            this->update_ble_status_();
            break;

        case ESP_GATTC_NOTIFY_EVT:
            if (param->notify.handle != this->tx_handle_)
                break;
            if (this->parser_.feed(this->state_, param->notify.value, param->notify.value_len, this->bad_crc_count_))
                this->publish_state_();
            break;

        case ESP_GATTC_DISCONNECT_EVT:
        case ESP_GATTC_CLOSE_EVT:
            this->reset_session_();
            this->update_ble_status_();
            break;

        default:
            break;
    }
    return true;
}

void CGO600Component::on_services_ready_() {
    auto *tx = this->get_characteristic(NUS_SERVICE_UUID, NUS_TX_UUID);
    auto *rx = this->get_characteristic(NUS_SERVICE_UUID, NUS_RX_UUID);
    if (tx == nullptr || rx == nullptr) {
        ESP_LOGE(TAG, "Nordic UART service not found (tx=%s rx=%s)", YESNO(tx != nullptr), YESNO(rx != nullptr));
        this->disconnect();
        return;
    }
    this->tx_handle_ = tx->handle;
    this->rx_handle_ = rx->handle;

    const esp_err_t err = esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, this->tx_handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "register_for_notify failed: %d", err);
        this->disconnect();
    }
}

void CGO600Component::loop() {
    BLEClientBase::loop();

    const uint32_t now = millis();
    if (this->phase_ == Phase::HANDSHAKE) {
        this->run_handshake_step_(now);
    } else if (this->phase_ == Phase::POLLING) {
        if (now - this->last_poll_ >= this->poll_interval_ms_) {
            this->last_poll_ = now;
            if (!this->write_(protocol::READ_DATA_CMD))
                ESP_LOGW(TAG, "Poll write failed");
        }
        // Serial number sometimes arrives later than the bootstrap; keep asking.
        if (now - this->last_static_request_ >= STATIC_REQUEST_INTERVAL_MS)
            this->request_static_fields_();
    }
}

// Same sequence and spacing as the standalone firmware's runHandshake(), but
// non-blocking: one step per HANDSHAKE_STEP_MS from loop().
void CGO600Component::run_handshake_step_(uint32_t now) {
    if ((int32_t) (now - this->next_step_at_) < 0)
        return;

    static const protocol::Command *const STEPS[] = {
        &protocol::READ_INIT_CMD,
        &protocol::READ_DATA_CMD,
        &protocol::BOOTSTRAP_CMD_2,
        &protocol::BOOTSTRAP_CMD_3,
    };
    static constexpr uint8_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

    if (this->handshake_step_ < STEP_COUNT) {
        if (!this->write_(*STEPS[this->handshake_step_])) {
            ESP_LOGW(TAG, "Handshake step %u failed, disconnecting", this->handshake_step_);
            this->disconnect();
            return;
        }
        ++this->handshake_step_;
        this->next_step_at_ = now + HANDSHAKE_STEP_MS;
        return;
    }

    this->request_static_fields_();
    this->phase_ = Phase::POLLING;
    this->last_poll_ = now;
    ESP_LOGI(TAG, "Handshake complete, polling every %u ms", (unsigned) this->poll_interval_ms_);
    this->update_ble_status_();
}

void CGO600Component::request_static_fields_() {
    static const uint8_t FIELDS[] = {protocol::FIELD_SN};
    uint8_t cmd[sizeof(FIELDS) + 5];
    const size_t len = protocol::build_request(cmd, FIELDS, sizeof(FIELDS));
    this->last_static_request_ = millis();
    if (!this->write_(cmd, len))
        ESP_LOGW(TAG, "Static field request failed");
}

bool CGO600Component::write_(const protocol::Command &cmd) { return this->write_(cmd.data, cmd.len); }

bool CGO600Component::write_(const uint8_t *data, size_t len) {
    if (this->rx_handle_ == 0 || !this->connected() || len > MAX_WRITE_LEN)
        return false;

    // The ESP-IDF API takes a mutable buffer.
    uint8_t buf[MAX_WRITE_LEN];
    memcpy(buf, data, len);

    // Write without response like the Tenways app; fall back to with-response.
    esp_err_t err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->rx_handle_, len, buf,
                                             ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
        err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->rx_handle_, len, buf,
                                       ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    }
    return err == ESP_OK;
}

bool CGO600Component::set_light(bool on) {
    if (this->phase_ != Phase::POLLING) {
        ESP_LOGW(TAG, "Light command ignored: bike not connected");
        return false;
    }
    if (!this->write_(on ? protocol::LIGHT_ON_CMD : protocol::LIGHT_OFF_CMD)) {
        ESP_LOGW(TAG, "Light write failed");
        return false;
    }
    ESP_LOGD(TAG, "Light %s", on ? "on" : "off");
    this->light_pending_on_ = on;
    this->light_pending_until_ = millis() + LIGHT_CONFIRM_WINDOW_MS;
    // Poll right away so the bike's confirmation arrives quickly.
    this->last_poll_ = millis() - this->poll_interval_ms_;
    return true;
}

// Scanning keeps running alongside an established connection by default. With
// WiFi sharing the radio that costs connection events and the link drops with
// supervision timeouts, so scan only while the bike is not connected.
void CGO600Component::pause_scanning_() {
    auto *tracker = espbt::global_esp32_ble_tracker;
    if (!this->pause_scan_while_connected_ || tracker == nullptr || this->scan_paused_)
        return;
    this->scan_paused_ = true;
    tracker->stop_scan();
}

void CGO600Component::resume_scanning_() {
    auto *tracker = espbt::global_esp32_ble_tracker;
    if (tracker == nullptr || !this->scan_paused_)
        return;
    this->scan_paused_ = false;
    // The tracker restarts the scan from its loop once the scanner is idle.
    tracker->set_scan_continuous(true);
}

void CGO600Component::reset_session_() {
    this->resume_scanning_();
    this->phase_ = Phase::IDLE;
    this->handshake_step_ = 0;
    this->tx_handle_ = 0;
    this->rx_handle_ = 0;
    this->parser_.reset();
}

static void publish_if_changed(sensor::Sensor *sensor, float value) {
    if (sensor == nullptr || value < 0)
        return;
    if (!sensor->has_state() || sensor->state != value)
        sensor->publish_state(value);
}

void CGO600Component::publish_state_() {
    const auto &s = this->state_;
    publish_if_changed(this->battery_sensor_, s.battery);
    publish_if_changed(this->speed_sensor_, s.speed_kmh);
    publish_if_changed(this->max_speed_sensor_, s.max_speed_kmh);
    publish_if_changed(this->avg_speed_sensor_, s.avg_speed_kmh);
    publish_if_changed(this->trip_distance_sensor_, s.trip_distance_km);
    publish_if_changed(this->trip_duration_sensor_, s.trip_duration);
    publish_if_changed(this->remaining_range_sensor_, s.remaining_range_km);
    publish_if_changed(this->range_sensor_, s.total_range_km);

    if (this->serial_number_text_sensor_ != nullptr && s.serial_number[0] != '\0' &&
        this->serial_number_text_sensor_->state != s.serial_number) {
        this->serial_number_text_sensor_->publish_state(s.serial_number);
    }
    if (this->light_switch_ != nullptr && s.light_valid) {
        const bool pending = (int32_t) (millis() - this->light_pending_until_) < 0;
        // Skip stale reports sent before the bike applied the last command.
        if (!pending || s.light_on == this->light_pending_on_) {
            this->light_pending_until_ = millis();
            if (this->light_switch_->state != s.light_on)
                this->light_switch_->publish_state(s.light_on);
        }
    }
}

void CGO600Component::update_ble_status_() {
    const char *status;
    if (this->phase_ == Phase::POLLING) {
        status = "connected";
    } else if (this->phase_ == Phase::HANDSHAKE) {
        status = "handshake";
    } else {
        switch (this->state()) {
            case espbt::ClientState::DISCOVERED:
            case espbt::ClientState::CONNECTING:
            case espbt::ClientState::CONNECTED:
            case espbt::ClientState::ESTABLISHED:
                status = "connecting";
                break;
            default:
                status = "searching";
                break;
        }
    }
    if (status == this->last_ble_status_)
        return;
    this->last_ble_status_ = status;
    ESP_LOGD(TAG, "BLE status: %s", status);
    if (this->ble_status_text_sensor_ != nullptr)
        this->ble_status_text_sensor_->publish_state(status);
}

void CGO600LightSwitch::write_state(bool state) {
    // Optimistic: the next poll publishes the bike's actual light state.
    if (this->parent_->set_light(state)) {
        this->publish_state(state);
    } else {
        this->publish_state(this->state);
    }
}

}  // namespace esphome::cgo600

#endif  // USE_ESP32
