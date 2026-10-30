#include "cgo.h"

#ifdef USE_ESP32

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

#include <esp_random.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cgo {

static const char *const TAG = "cgo";

static constexpr uint32_t SUBSCRIBE_SETTLE_MS = 50;
static constexpr uint32_t HANDSHAKE_STEP_MS = 120;
static constexpr uint32_t STATIC_REQUEST_INTERVAL_MS = 15000;
static constexpr size_t MAX_WRITE_LEN = 64;
static constexpr uint32_t LIGHT_CONFIRM_WINDOW_MS = 2000;
static constexpr uint32_t CCCD_CONFIRM_TIMEOUT_MS = 1000;
// King Meter support is unconfirmed on most bikes: log the first traffic of
// every connection at DEBUG so a default log shows it, the rest at VERBOSE.
static constexpr uint32_t DEBUG_LOGGED = 20;
static constexpr uint32_t KM_SUMMARY_INTERVAL_MS = 30000;
static constexpr uint8_t KM_SWITCH_AFTER_POLLS = 5;
static constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 2000;
// Like the Tenways app, reconnect once the display answers with a second error.
static constexpr uint32_t KM_AT_REPLIES_BEFORE_RECONNECT = 2;
static constexpr size_t MAX_FRAME_LEN = 256;
// A CGO600 variant that sends no data this long after polling started gets the
// encryption handshake.
static constexpr uint32_t NUS_ENCRYPTION_AFTER_MS = 5000;
static constexpr size_t MAX_LOGGED_BYTES = 64;
// The BIKEGO plus app gives the display 10 s to answer the authentication.
static constexpr uint32_t B06_AUTH_TIMEOUT_MS = 10000;
// Connection parameters (units: 1.25 ms, 10 ms). Connect with a short interval
// so service discovery is quick: the bike is often switched off seconds after
// arriving, and a 50 ms interval stretched discovery to over 10 s. Once the
// services are known, switch to a relaxed interval that leaves airtime for
// WiFi. The 10 s supervision timeout rides out short radio hiccups that
// otherwise drop the link with reason 0x08.
static constexpr uint16_t FAST_MIN_INTERVAL = 0x06;  // 7.5 ms
static constexpr uint16_t FAST_MAX_INTERVAL = 0x10;  // 20 ms
static constexpr uint16_t RELAXED_MIN_INTERVAL = 0x18;  // 30 ms
static constexpr uint16_t RELAXED_MAX_INTERVAL = 0x28;  // 50 ms
static constexpr uint16_t CONN_LATENCY = 0;
static constexpr uint16_t CONN_SUPERVISION_TIMEOUT = 1000;  // 10 s
// Anything before 2020 means the clock has not been set by a time source yet.
static constexpr time_t MIN_VALID_EPOCH = 1577836800;
// Bump when protocol::BikeState changes layout; older data is then ignored.
static constexpr uint32_t LAST_STATE_MAGIC = 0x43475302;
static constexpr uint32_t LAST_STATE_SAVE_INTERVAL_MS = 5 * 60 * 1000;

static uint32_t epoch_now() {
    const time_t now = ::time(nullptr);
    return now >= MIN_VALID_EPOCH ? (uint32_t) now : 0;
}

static const espbt::ESPBTUUID NUS_SERVICE_UUID = espbt::ESPBTUUID::from_raw("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static const espbt::ESPBTUUID NUS_RX_UUID = espbt::ESPBTUUID::from_raw("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static const espbt::ESPBTUUID NUS_TX_UUID = espbt::ESPBTUUID::from_raw("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
// "KING METER" in ASCII.
static const espbt::ESPBTUUID KM_SERVICE_UUID = espbt::ESPBTUUID::from_raw("43480001-f001-4b49-4e47-204d45544552");
static const espbt::ESPBTUUID KM_WRITE_UUID = espbt::ESPBTUUID::from_raw("43480002-f001-4b49-4e47-204d45544552");
static const espbt::ESPBTUUID KM_NOTIFY_UUID = espbt::ESPBTUUID::from_raw("43480003-f001-4b49-4e47-204d45544552");
// Microchip transparent UART, used by B06 displays.
static const espbt::ESPBTUUID B06_SERVICE_UUID = espbt::ESPBTUUID::from_raw("49535343-fe7d-4ae5-8fa9-9fafd205e455");
static const espbt::ESPBTUUID B06_NOTIFY_UUID = espbt::ESPBTUUID::from_raw("49535343-1e4d-4bd9-ba61-23c647249616");
static const espbt::ESPBTUUID B06_WRITE_UUID = espbt::ESPBTUUID::from_raw("49535343-8841-43f4-a8d4-ecbe34729bb3");

// Displays report errors as AT commands to their Bluetooth module, which
// forwards them as notifications.
static bool is_at_reply(const uint8_t *data, size_t len) {
    return len >= 3 && data[0] == 'A' && data[1] == 'T' && data[2] == '+';
}

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

void CGOComponent::setup() {
    BLEClientBase::setup();
    this->ride_log_.setup(this->max_rides_);
    this->restore_last_state_();
    this->update_ble_status_();
}

void CGOComponent::set_device_names(const std::vector<std::string> &names) {
    this->device_names_ = names;
    this->device_name_.clear();
    for (const auto &name : names) {
        if (!this->device_name_.empty())
            this->device_name_ += ", ";
        this->device_name_ += name;
    }
}

void CGOComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "CGO:");
    ESP_LOGCONFIG(TAG, "  Device names: %s", this->device_name_.c_str());
    ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", (unsigned) this->poll_interval_ms_);
    ESP_LOGCONFIG(TAG, "  Rides stored: %u of %u", (unsigned) this->ride_log_.count(),
                  (unsigned) this->ride_log_.max_rides());
    LOG_SENSOR("  ", "Battery", this->battery_sensor_);
    LOG_SENSOR("  ", "Speed", this->speed_sensor_);
    LOG_SENSOR("  ", "Max speed", this->max_speed_sensor_);
    LOG_SENSOR("  ", "Avg speed", this->avg_speed_sensor_);
    LOG_SENSOR("  ", "Trip distance", this->trip_distance_sensor_);
    LOG_SENSOR("  ", "Trip duration", this->trip_duration_sensor_);
    LOG_SENSOR("  ", "Remaining range", this->remaining_range_sensor_);
    LOG_SENSOR("  ", "Range", this->range_sensor_);
    LOG_SENSOR("  ", "Power", this->power_sensor_);
    LOG_TEXT_SENSOR("  ", "Serial number", this->serial_number_text_sensor_);
    LOG_TEXT_SENSOR("  ", "Firmware version", this->firmware_version_text_sensor_);
    LOG_TEXT_SENSOR("  ", "BLE status", this->ble_status_text_sensor_);
    LOG_SWITCH("  ", "Light", this->light_switch_);
}

bool CGOComponent::matches_(const espbt::ESPBTDevice &device) const {
    for (const auto &name : this->device_names_) {
        if (contains_ignore_case(device.get_name(), name))
            return true;
    }
    // The bike sometimes omits its name; its service still identifies it.
    for (const auto &uuid : device.get_service_uuids()) {
        if (uuid == NUS_SERVICE_UUID || uuid == KM_SERVICE_UUID || uuid == B06_SERVICE_UUID)
            return true;
    }
    return false;
}

bool CGOComponent::parse_device(const espbt::ESPBTDevice &device) {
    if (this->state() != espbt::ClientState::IDLE || !this->matches_(device))
        return false;

    ESP_LOGI(TAG, "Found bike '%s' [%s], connecting", device.get_name().c_str(), device.address_str().c_str());
    this->bike_name_ = device.get_name();
    this->set_address(device.address_uint64());
    this->set_remote_addr_type(device.get_address_type());
    this->set_state(espbt::ClientState::DISCOVERED);
    return true;
}

void CGOComponent::set_state(espbt::ClientState state) {
    BLEClientBase::set_state(state);
    if (state == espbt::ClientState::IDLE) {
        this->reset_session_();
    }
    this->update_ble_status_();
}

void CGOComponent::connect() {
    this->connect_started_ms_ = millis();
    this->set_conn_params_(FAST_MIN_INTERVAL, FAST_MAX_INTERVAL, CONN_LATENCY, CONN_SUPERVISION_TIMEOUT, "fast");
    BLEClientBase::connect();
}

void CGOComponent::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    BLEClientBase::gap_event_handler(event, param);
    if (event == ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT && this->check_addr(param->update_conn_params.bda)) {
        ESP_LOGD(TAG, "Connection params: interval=%.2f ms latency=%u timeout=%u ms (status=%d)",
                 param->update_conn_params.conn_int * 1.25f, param->update_conn_params.latency,
                 param->update_conn_params.timeout * 10, param->update_conn_params.status);
    }
}

bool CGOComponent::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
    if (!BLEClientBase::gattc_event_handler(event, gattc_if, param))
        return false;

    switch (event) {
        case ESP_GATTC_SEARCH_CMPL_EVT:
            this->on_services_ready_();
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
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
            // The base class enables notifications by writing this descriptor.
            auto *cccd = this->get_config_descriptor(this->tx_handle_);
            this->cccd_handle_ = cccd != nullptr ? cccd->handle : 0;
            this->cccd_confirmed_ = this->cccd_handle_ == 0;
            this->cccd_deadline_ = millis() + CCCD_CONFIRM_TIMEOUT_MS;
            this->update_ble_status_();
            break;
        }

        case ESP_GATTC_WRITE_DESCR_EVT:
            if (this->cccd_handle_ == 0 || param->write.handle != this->cccd_handle_) {
                if (param->write.status != ESP_GATT_OK) {
                    ESP_LOGW(TAG, "Descriptor write 0x%04X failed (status=0x%02X)", param->write.handle,
                             param->write.status);
                }
                break;
            }
            if (param->write.status == ESP_GATT_OK) {
                ESP_LOGD(TAG, "Notifications enabled");
            } else {
                ESP_LOGW(TAG, "Enabling notifications failed (status=0x%02X); the bike will not send data",
                         param->write.status);
            }
            this->cccd_confirmed_ = true;
            break;

        case ESP_GATTC_WRITE_CHAR_EVT:
            if (param->write.handle == this->rx_handle_ && param->write.status != ESP_GATT_OK)
                ESP_LOGW(TAG, "Bike rejected a write (status=0x%02X)", param->write.status);
            break;

        case ESP_GATTC_NOTIFY_EVT: {
            if (param->notify.handle != this->tx_handle_)
                break;
            const bool km = this->transport_ == Transport::KING_METER;
            const bool b06 = this->transport_ == Transport::B06;
            auto &stats = this->link_stats_;
            const uint8_t *data = param->notify.value;
            size_t len = param->notify.value_len;
            if (km || b06) {
                ++stats.notifications;
                stats.notification_bytes += len;
            }
            this->log_bytes_("RX", param->notify.handle, data, len,
                             (km || b06) && stats.notifications <= DEBUG_LOGGED);
            uint8_t plain[MAX_FRAME_LEN];
            if (is_at_reply(data, len)) {
                this->handle_at_reply_(data, len);
                break;
            }
            if (this->handle_enc_handshake_reply_(data, len))
                break;
            if (this->enc_session_.active()) {
                len = this->enc_session_.unwrap(data, len, plain, sizeof(plain));
                if (len == 0) {
                    ++stats.undecryptable;
                    ESP_LOGW(TAG, "Could not decrypt a notification (%u this connection)",
                             (unsigned) stats.undecryptable);
                    break;
                }
                data = plain;
                this->log_bytes_("RX decrypted", param->notify.handle, data, len,
                                 stats.notifications <= DEBUG_LOGGED);
            }
            bool saw_trip = false;
            bool changed;
            const uint32_t bad_crc_before = stats.bad_crc;
            const uint32_t unhandled_before = stats.unhandled;
            {
                LockGuard guard(this->state_mutex_);
                if (km) {
                    changed = this->km_parser_.feed(this->state_, data, len, saw_trip, stats.bad_crc, stats.unhandled);
                } else if (b06) {
                    changed = this->b06_parser_.feed(this->state_, data, len, saw_trip, stats.bad_crc, stats.unhandled);
                } else {
                    changed = this->parser_.feed(this->state_, data, len, saw_trip, this->bad_crc_count_);
                    if (this->state_.power_w >= 0 && !this->power_seen_) {
                        this->power_seen_ = true;
                        ESP_LOGD(TAG, "Bike reports its power, polling it");
                    }
                }
            }
            if (b06)
                this->handle_b06_auth_();
            if (stats.bad_crc != bad_crc_before)
                ESP_LOGD(TAG, "Frame failed the CRC check (%u this connection)", (unsigned) stats.bad_crc);
            if (stats.unhandled != unhandled_before)
                ESP_LOGD(TAG, "Received a frame without known fields (%u this connection)",
                         (unsigned) stats.unhandled);
            if (changed) {
                if (this->connect_started_ms_ != 0) {
                    ESP_LOGI(TAG, "First data %u ms after connecting", (unsigned) (millis() - this->connect_started_ms_));
                    this->connect_started_ms_ = 0;
                }
                this->last_state_dirty_ = true;
                this->publish_state_();
            }
            // A frame carrying the trip counters is a confirmed current reading:
            // its receive time anchors the ride window.
            RideRecord ride;
            if (saw_trip && this->ride_log_.observe(this->state_, epoch_now(), ride)) {
                const int battery_used = (ride.battery_start >= 0 && ride.battery_end >= 0)
                                             ? ride.battery_start - ride.battery_end
                                             : -1;
                this->ride_callback_.call(ride.distance_km, (uint32_t) ride.duration_s, battery_used);
            }
            break;
        }

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

void CGOComponent::on_services_ready_() {
    esp32_ble_client::BLECharacteristic *tx = nullptr;
    esp32_ble_client::BLECharacteristic *rx = nullptr;
    if (this->get_service(NUS_SERVICE_UUID) != nullptr) {
        this->transport_ = Transport::NUS;
        tx = this->get_characteristic(NUS_SERVICE_UUID, NUS_TX_UUID);
        rx = this->get_characteristic(NUS_SERVICE_UUID, NUS_RX_UUID);
    } else if (this->get_service(KM_SERVICE_UUID) != nullptr) {
        this->transport_ = Transport::KING_METER;
        this->km_profile_ = &protocol::km::profile_for(this->bike_name_.c_str());
        this->km_parser_.set_profile(this->km_profile_);
        const size_t name_len = this->bike_name_.size();
        this->km_write_with_response_ = this->km_profile_->write_with_response ||
                                        (name_len >= 2 && this->bike_name_.compare(name_len - 2, 2, "_D") == 0);
        tx = this->get_characteristic(KM_SERVICE_UUID, KM_NOTIFY_UUID);
        rx = this->get_characteristic(KM_SERVICE_UUID, KM_WRITE_UUID);
    } else if (this->get_service(B06_SERVICE_UUID) != nullptr) {
        this->transport_ = Transport::B06;
        // The B06 reports none of these; drop values restored from another bike.
        LockGuard guard(this->state_mutex_);
        this->state_.remaining_range_km = -1;
        this->state_.power_w = -1;
        this->state_.serial_number[0] = '\0';
        tx = this->get_characteristic(B06_SERVICE_UUID, B06_NOTIFY_UUID);
        rx = this->get_characteristic(B06_SERVICE_UUID, B06_WRITE_UUID);
    }
    if (tx == nullptr || rx == nullptr) {
        ESP_LOGE(TAG, "No supported bike service found (tx=%s rx=%s)", YESNO(tx != nullptr), YESNO(rx != nullptr));
        this->log_services_();
        this->disconnect();
        return;
    }
    if (this->transport_ == Transport::KING_METER) {
        ESP_LOGI(TAG, "Using the King Meter protocol (%s layout)", this->km_profile_->name);
    } else if (this->transport_ == Transport::B06) {
        ESP_LOGI(TAG, "Using the B06 protocol");
    } else {
        ESP_LOGI(TAG, "Using the Nordic UART protocol (CGO600)");
    }
    this->tx_handle_ = tx->handle;
    this->rx_handle_ = rx->handle;
    this->update_conn_params_(RELAXED_MIN_INTERVAL, RELAXED_MAX_INTERVAL, CONN_LATENCY, CONN_SUPERVISION_TIMEOUT,
                              "relaxed");

    const esp_err_t err = esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, this->tx_handle_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "register_for_notify failed: %d", err);
        this->disconnect();
    }
}

void CGOComponent::log_bytes_(const char *what, uint16_t handle, const uint8_t *data, size_t len, bool debug) {
#if ESPHOME_LOG_LEVEL < ESPHOME_LOG_LEVEL_VERBOSE
    if (!debug)
        return;
#endif
    char hex[format_hex_pretty_size(MAX_LOGGED_BYTES)];
    format_hex_pretty_to(hex, data, std::min(len, MAX_LOGGED_BYTES));
    if (debug) {
        ESP_LOGD(TAG, "%s 0x%04X, %u bytes: %s", what, handle, (unsigned) len, hex);
    } else {
        ESP_LOGV(TAG, "%s 0x%04X, %u bytes: %s", what, handle, (unsigned) len, hex);
    }
}

void CGOComponent::log_km_summary_() {
    this->last_km_summary_ = millis();
    const auto &s = this->link_stats_;
    if (this->transport_ == Transport::B06) {
        ESP_LOGI(TAG, "B06: %u requests, %u notifications (%u bytes), %u frames, %u bad CRC, %u unhandled",
                 (unsigned) s.requests, (unsigned) s.notifications, (unsigned) s.notification_bytes,
                 (unsigned) this->b06_parser_.frames(), (unsigned) s.bad_crc, (unsigned) s.unhandled);
        return;
    }
    ESP_LOGI(TAG,
             "King Meter (%s): %u requests, %u notifications (%u bytes), %u frames, %u bad CRC, %u unhandled, "
             "%u bytes skipped, %u errors, %u undecryptable",
             this->enc_session_.active() ? "encrypted" : "unencrypted", (unsigned) s.requests,
             (unsigned) s.notifications, (unsigned) s.notification_bytes, (unsigned) this->km_parser_.frames(),
             (unsigned) s.bad_crc, (unsigned) s.unhandled, (unsigned) this->km_parser_.skipped_bytes(),
             (unsigned) s.at_replies, (unsigned) s.undecryptable);
    if (s.requests > 0 && s.notifications == 0)
        ESP_LOGW(TAG, "The bike has not sent any notification yet");
}

// Lists what the bike offers, so a log from an unsupported model shows which
// service it uses.
void CGOComponent::log_services_() {
    char uuid[esp32_ble::UUID_STR_LEN];
    for (auto *service : this->services_) {
        ESP_LOGW(TAG, "  Service %s", service->uuid.to_str(uuid));
        service->parse_characteristics();
        for (auto *chr : service->characteristics) {
            ESP_LOGW(TAG, "    Characteristic %s (properties 0x%02X)", chr->uuid.to_str(uuid), chr->properties);
        }
    }
}

void CGOComponent::loop() {
    BLEClientBase::loop();

    const uint32_t now = millis();
    if (this->phase_ == Phase::HANDSHAKE) {
        this->run_handshake_step_(now);
    } else if (this->phase_ == Phase::POLLING) {
        if (this->transport_ != Transport::NUS && now - this->last_km_summary_ >= KM_SUMMARY_INTERVAL_MS)
            this->log_km_summary_();
        if (this->is_nus_variant_() && this->enc_handshake_ == EncHandshake::NONE &&
            this->connect_started_ms_ != 0 && now - this->polling_started_ms_ >= NUS_ENCRYPTION_AFTER_MS) {
            ESP_LOGI(TAG, "No data from the bike yet, trying the encryption handshake");
            this->start_encryption_(now);
        }
        if (!this->encryption_pending_(now) && now - this->last_poll_ >= this->poll_interval_ms_) {
            this->last_poll_ = now;
            this->poll_();
        }
        if (this->last_state_dirty_ && now - this->last_state_saved_ms_ >= LAST_STATE_SAVE_INTERVAL_MS)
            this->save_last_state_();
        // Serial number sometimes arrives later than the bootstrap; keep asking.
        if (now - this->last_static_request_ >= STATIC_REQUEST_INTERVAL_MS)
            this->request_static_fields_();
    }
}

// Same sequence and spacing as the standalone firmware's runHandshake(), but
// non-blocking: one step per HANDSHAKE_STEP_MS from loop().
void CGOComponent::run_handshake_step_(uint32_t now) {
    if (!this->cccd_confirmed_) {
        if ((int32_t) (now - this->cccd_deadline_) < 0)
            return;
        ESP_LOGW(TAG, "Bike did not confirm the notification subscription, continuing");
        this->cccd_confirmed_ = true;
    }
    if (this->transport_ == Transport::B06) {
        this->run_b06_handshake_(now);
        return;
    }
    if ((int32_t) (now - this->next_step_at_) < 0)
        return;

    static const protocol::Command *const STEPS[] = {
        &protocol::READ_INIT_CMD,
        &protocol::READ_DATA_CMD,
        &protocol::BOOTSTRAP_CMD_2,
        &protocol::BOOTSTRAP_CMD_3,
    };
    static constexpr uint8_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

    if (this->transport_ == Transport::KING_METER && this->km_profile_->try_encryption &&
        this->enc_handshake_ == EncHandshake::NONE) {
        this->start_encryption_(now);
        return;
    }
    if (this->encryption_pending_(now))
        return;
    if (this->transport_ == Transport::NUS && this->handshake_step_ < STEP_COUNT) {
        if (!this->write_(*STEPS[this->handshake_step_])) {
            ESP_LOGW(TAG, "Handshake step %u failed, disconnecting", this->handshake_step_);
            this->disconnect();
            return;
        }
        ++this->handshake_step_;
        this->next_step_at_ = now + HANDSHAKE_STEP_MS;
        return;
    }
    this->finish_handshake_(now);
}

// Starts the authentication and finishes the handshake once the display has
// answered it; the challenge itself is answered from handle_b06_auth_().
void CGOComponent::run_b06_handshake_(uint32_t now) {
    namespace b06 = protocol::b06;
    if (this->handshake_step_ == 0) {
        if ((int32_t) (now - this->next_step_at_) < 0)
            return;
        static const uint8_t START[] = {0x01};
        uint8_t frame[sizeof(START) + b06::OVERHEAD];
        if (!this->send_frame_(frame, b06::build_frame(frame, b06::CMD_AUTH_START, START, sizeof(START)))) {
            ESP_LOGW(TAG, "Authentication request failed, disconnecting");
            this->disconnect();
            return;
        }
        this->handshake_step_ = 1;
        this->next_step_at_ = now + B06_AUTH_TIMEOUT_MS;
        return;
    }
    const auto result = this->b06_parser_.auth_result();
    if (result == b06::FrameParser::AuthResult::NONE && (int32_t) (now - this->next_step_at_) < 0)
        return;
    if (result == b06::FrameParser::AuthResult::OK) {
        ESP_LOGI(TAG, "Display accepted the authentication");
    } else if (result == b06::FrameParser::AuthResult::FAILED) {
        ESP_LOGW(TAG, "Display rejected the authentication, listening anyway");
    } else {
        ESP_LOGW(TAG, "No answer to the authentication, listening anyway");
    }
    this->finish_handshake_(now);
}

void CGOComponent::handle_b06_auth_() {
    uint8_t challenge[protocol::b06::AUTH_CHALLENGE_LEN];
    if (!this->b06_parser_.take_challenge(challenge))
        return;
    uint8_t answer[16];
    uint8_t frame[sizeof(answer) + protocol::b06::OVERHEAD];
    if (!protocol::b06_auth_answer(challenge, answer)) {
        ESP_LOGW(TAG, "Computing the authentication answer failed");
        return;
    }
    ESP_LOGD(TAG, "Answering the authentication challenge");
    if (!this->send_frame_(frame, protocol::b06::build_frame(frame, protocol::b06::CMD_AUTH_ANSWER, answer,
                                                             sizeof(answer))))
        ESP_LOGW(TAG, "Authentication answer failed");
}

// The CGO600 family members other than the CGO600 itself, which the Tenways
// app can also run encrypted.
bool CGOComponent::is_nus_variant_() const {
    return this->transport_ == Transport::NUS && this->bike_name_.rfind("CGO600", 0) == 0 &&
           this->bike_name_ != "CGO600";
}

void CGOComponent::start_encryption_(uint32_t now) {
    namespace km = protocol::km;
    esp_fill_random(this->enc_app_random_, sizeof(this->enc_app_random_));
    uint8_t frame[protocol::HANDSHAKE_VALUE_LEN + 9];
    size_t len;
    if (this->transport_ == Transport::KING_METER) {
        len = km::build_write(frame, protocol::KEY_HANDSHAKE_RANDOM, this->enc_app_random_,
                              sizeof(this->enc_app_random_), this->km_profile_->crc_low_first);
        this->enc_write_with_response_ = true;
    } else {
        len = protocol::build_write(frame, protocol::KEY_HANDSHAKE_RANDOM, this->enc_app_random_,
                                    sizeof(this->enc_app_random_));
    }
    ESP_LOGD(TAG, "Starting the encryption handshake");
    this->enc_handshake_ = EncHandshake::WAIT_RANDOM;
    this->enc_deadline_ = now + HANDSHAKE_TIMEOUT_MS;
    if (!this->send_frame_(frame, len))
        ESP_LOGW(TAG, "Handshake write failed");
}

// True while the handshake waits for the display; gives up at the deadline.
bool CGOComponent::encryption_pending_(uint32_t now) {
    if (this->enc_handshake_ != EncHandshake::WAIT_RANDOM && this->enc_handshake_ != EncHandshake::WAIT_IV)
        return false;
    if ((int32_t) (now - this->enc_deadline_) < 0)
        return true;
    ESP_LOGI(TAG, "No reply to the encryption handshake, continuing unencrypted");
    this->enc_write_with_response_ = false;
    this->enc_handshake_ = EncHandshake::DONE;
    return false;
}

// Consumes the display's handshake replies: its random, then the IV.
bool CGOComponent::handle_enc_handshake_reply_(const uint8_t *data, size_t len) {
    namespace km = protocol::km;
    const bool nus = this->transport_ == Transport::NUS;
    if (this->enc_handshake_ == EncHandshake::WAIT_RANDOM) {
        const uint8_t *device_random =
            nus ? protocol::nus_handshake_value(data, len, protocol::FRAME_WRITE, protocol::KEY_HANDSHAKE_RANDOM)
                : protocol::km_handshake_value(data, len, km::TYPE_WRITE, protocol::KEY_HANDSHAKE_RANDOM);
        if (device_random == nullptr)
            return false;
        if (!protocol::derive_session_key(this->enc_app_random_, device_random, this->enc_session_key_)) {
            ESP_LOGW(TAG, "Deriving the session key failed, continuing unencrypted");
            this->enc_write_with_response_ = false;
            this->enc_handshake_ = EncHandshake::DONE;
            return true;
        }
        ESP_LOGD(TAG, "Got the display's random, requesting the IV");
        static const uint8_t IV_KEYS[] = {protocol::KEY_HANDSHAKE_IV};
        this->enc_handshake_ = EncHandshake::WAIT_IV;
        this->enc_deadline_ = millis() + HANDSHAKE_TIMEOUT_MS;
        const bool ok = nus ? this->write_nus_read_(IV_KEYS, sizeof(IV_KEYS))
                            : this->write_km_read_(IV_KEYS, sizeof(IV_KEYS));
        if (!ok)
            ESP_LOGW(TAG, "IV request failed");
        return true;
    }
    if (this->enc_handshake_ == EncHandshake::WAIT_IV) {
        const uint8_t *iv =
            nus ? protocol::nus_handshake_value(data, len, protocol::FRAME_START, protocol::KEY_HANDSHAKE_IV)
                : protocol::km_handshake_value(data, len, km::TYPE_READ, protocol::KEY_HANDSHAKE_IV);
        if (iv == nullptr)
            return false;
        this->enc_session_.start(this->enc_session_key_, iv);
        this->enc_handshake_ = EncHandshake::DONE;
        ESP_LOGI(TAG, "Encrypted session established");
        return true;
    }
    return false;
}

void CGOComponent::handle_at_reply_(const uint8_t *data, size_t len) {
    // A display that does not expect the handshake can reject it like this.
    if (this->enc_handshake_ == EncHandshake::WAIT_RANDOM) {
        ESP_LOGI(TAG, "Display rejected the encryption handshake, continuing unencrypted");
        this->enc_write_with_response_ = false;
        this->enc_handshake_ = EncHandshake::DONE;
        return;
    }
    char text[48];
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof(text) - 1 && data[i] >= 0x20 && data[i] < 0x7F; i++)
        text[n++] = (char) data[i];
    text[n] = '\0';
    const uint32_t count = ++this->link_stats_.at_replies;
    ESP_LOGW(TAG, "Display reported an error: %s (%u this connection)", text, (unsigned) count);
    // Encrypted displays report this error until the handshake has run.
    const bool may_need_encryption = this->is_nus_variant_() || this->transport_ == Transport::KING_METER;
    if (may_need_encryption && this->enc_handshake_ == EncHandshake::NONE && this->phase_ == Phase::POLLING) {
        this->start_encryption_(millis());
        return;
    }
    if (this->transport_ == Transport::KING_METER && count >= KM_AT_REPLIES_BEFORE_RECONNECT) {
        ESP_LOGW(TAG, "Reconnecting to clear the display error");
        this->disconnect();
    }
}

void CGOComponent::finish_handshake_(uint32_t now) {
    this->request_static_fields_();
    this->phase_ = Phase::POLLING;
    this->last_poll_ = now;
    this->last_km_summary_ = now;
    this->polling_started_ms_ = now;
    if (this->transport_ == Transport::NUS) {
        // A power value stored from an earlier session must not count as the
        // bike answering this session's power read.
        LockGuard guard(this->state_mutex_);
        this->state_.power_w = -1;
    }
    ESP_LOGI(TAG, "Handshake complete, polling every %u ms", (unsigned) this->poll_interval_ms_);
    const uint32_t connected_at = epoch_now();
    if (connected_at != 0) {
        this->last_connected_at_ = connected_at;
        this->last_state_dirty_ = true;
    }
    this->update_ble_status_();
}

void CGOComponent::request_static_fields_() {
    this->last_static_request_ = millis();
    bool ok;
    if (this->transport_ == Transport::KING_METER) {
        ok = this->write_km_read_(this->km_profile_->info_keys, this->km_profile_->info_count);
    } else if (this->transport_ == Transport::B06) {
        {
            LockGuard guard(this->state_mutex_);
            if (this->state_.firmware[0] != '\0')
                return;
        }
        uint8_t frame[protocol::b06::OVERHEAD];
        ok = this->send_frame_(frame, protocol::b06::build_frame(frame, protocol::b06::CMD_VERSION, nullptr, 0));
    } else {
        // Separate requests, so a bike that does not know one of the fields
        // still answers the others. Not every CGO600 reports its power; ask
        // until it answers once.
        static const uint8_t SERIAL[] = {protocol::FIELD_SN};
        static const uint8_t FIRMWARE[] = {protocol::FIELD_FIRMWARE};
        static const uint8_t POWER[] = {protocol::FIELD_POWER};
        ok = this->write_nus_read_(SERIAL, sizeof(SERIAL)) && this->write_nus_read_(FIRMWARE, sizeof(FIRMWARE));
        if (ok && !this->power_seen_)
            ok = this->write_nus_read_(POWER, sizeof(POWER));
    }
    if (!ok)
        ESP_LOGW(TAG, "Static field request failed");
}

void CGOComponent::poll_() {
    // B06 displays push their data without being asked.
    if (this->transport_ == Transport::B06)
        return;
    bool ok;
    if (this->transport_ == Transport::KING_METER) {
        // A silent display may expect the encryption handshake, or requests in
        // the other CRC order, for example when its name picked the wrong
        // profile or it advertises none. Try the handshake first, then the
        // other layout.
        if (this->km_parser_.frames() == 0 && !this->enc_session_.active() && this->link_stats_.at_replies == 0 &&
            ++this->km_unanswered_polls_ > KM_SWITCH_AFTER_POLLS) {
            this->km_unanswered_polls_ = 0;
            if (this->enc_handshake_ == EncHandshake::NONE) {
                ESP_LOGI(TAG, "No reply from the bike, trying the encryption handshake");
                this->start_encryption_(millis());
                return;
            }
            this->km_profile_ = &protocol::km::alternative_to(*this->km_profile_);
            this->km_parser_.set_profile(this->km_profile_);
            ESP_LOGI(TAG, "No reply from the bike, trying the %s layout", this->km_profile_->name);
            this->request_static_fields_();
        }
        ok = this->write_km_read_(this->km_profile_->status_keys, this->km_profile_->status_count);
    } else {
        bool status_has_power = false;
        if (this->is_nus_variant_() || this->enc_session_.active()) {
            // The status request of the Tenways app; only the CGO600 PLUS gets
            // the trailing power field.
            static const uint8_t STATUS[] = {0x07, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                             0x17, 0x21, 0x1A, 0x2D, protocol::FIELD_POWER};
            status_has_power = this->bike_name_.rfind("CGO600 PLUS", 0) == 0;
            ok = this->write_nus_read_(STATUS, status_has_power ? sizeof(STATUS) : sizeof(STATUS) - 1);
        } else {
            ok = this->send_frame_(protocol::READ_DATA_CMD.data, protocol::READ_DATA_CMD.len);
        }
        static const uint8_t POWER[] = {protocol::FIELD_POWER};
        if (ok && this->power_seen_ && !status_has_power)
            ok = this->write_nus_read_(POWER, sizeof(POWER));
    }
    if (!ok)
        ESP_LOGW(TAG, "Poll write failed");
}

bool CGOComponent::write_nus_read_(const uint8_t *fields, uint8_t count) {
    uint8_t cmd[MAX_WRITE_LEN];
    if (count + 5u > sizeof(cmd))
        return false;
    return this->send_frame_(cmd, protocol::build_request(cmd, fields, count));
}

bool CGOComponent::write_km_read_(const uint8_t *keys, uint8_t count) {
    uint8_t cmd[MAX_WRITE_LEN];
    if (count + protocol::km::OVERHEAD + 1 > sizeof(cmd))
        return false;
    return this->send_frame_(cmd, protocol::km::build_read(cmd, keys, count, this->km_profile_->crc_low_first));
}

bool CGOComponent::send_frame_(const uint8_t *frame, size_t len) {
    if (!this->enc_session_.active())
        return this->write_(frame, len, this->enc_write_with_response_ || this->km_write_with_response_);
    uint8_t packet[MAX_WRITE_LEN];
    const size_t packet_len = this->enc_session_.wrap(frame, len, packet, sizeof(packet));
    if (packet_len == 0)
        return false;
    this->log_bytes_("TX plain", this->rx_handle_, frame, len, this->link_stats_.requests < DEBUG_LOGGED);
    return this->write_(packet, packet_len, this->enc_write_with_response_ || this->km_write_with_response_);
}

bool CGOComponent::write_(const protocol::Command &cmd) { return this->write_(cmd.data, cmd.len); }

bool CGOComponent::write_(const uint8_t *data, size_t len, bool with_response) {
    if (this->rx_handle_ == 0 || !this->connected() || len > MAX_WRITE_LEN)
        return false;

    // The ESP-IDF API takes a mutable buffer.
    uint8_t buf[MAX_WRITE_LEN];
    memcpy(buf, data, len);

    // Write without response like the Tenways app unless asked otherwise; fall
    // back to with-response.
    esp_err_t err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->rx_handle_, len, buf,
                                             with_response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
                                             ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK && !with_response) {
        err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, this->rx_handle_, len, buf,
                                       ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    }
    if (err == ESP_OK && this->transport_ != Transport::NUS) {
        ++this->link_stats_.requests;
        this->log_bytes_("TX", this->rx_handle_, data, len, this->link_stats_.requests <= DEBUG_LOGGED);
    }
    return err == ESP_OK;
}

bool CGOComponent::set_light(bool on) {
    if (this->phase_ != Phase::POLLING) {
        ESP_LOGW(TAG, "Light command ignored: bike not connected");
        return false;
    }
    bool ok;
    if (this->transport_ == Transport::B06) {
        uint8_t cmd[4 + protocol::b06::OVERHEAD];
        ok = this->send_frame_(cmd, protocol::b06::build_light(cmd, on));
    } else if (this->transport_ == Transport::KING_METER) {
        uint8_t cmd[10];
        ok = this->send_frame_(cmd, protocol::km::build_write(cmd, protocol::km::KEY_LIGHT, on ? 1 : 0,
                                                              this->km_profile_->crc_low_first));
    } else {
        const auto &cmd = on ? protocol::LIGHT_ON_CMD : protocol::LIGHT_OFF_CMD;
        ok = this->send_frame_(cmd.data, cmd.len);
    }
    if (!ok) {
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
void CGOComponent::pause_scanning_() {
    auto *tracker = espbt::global_esp32_ble_tracker;
    if (tracker == nullptr || this->scan_paused_)
        return;
    this->scan_paused_ = true;
    tracker->stop_scan();
}

void CGOComponent::resume_scanning_() {
    auto *tracker = espbt::global_esp32_ble_tracker;
    if (tracker == nullptr || !this->scan_paused_)
        return;
    this->scan_paused_ = false;
    // The tracker restarts the scan from its loop once the scanner is idle.
    tracker->set_scan_continuous(true);
}

void CGOComponent::reset_session_() {
    this->resume_scanning_();
    if (this->transport_ != Transport::NUS && this->transport_ != Transport::NONE && this->phase_ != Phase::IDLE)
        this->log_km_summary_();
    if (this->phase_ == Phase::POLLING) {
        // Keep the last readings, but a speed from before the bike left is no
        // longer current.
        {
            LockGuard guard(this->state_mutex_);
            if (this->state_.speed_kmh > 0)
                this->state_.speed_kmh = 0;
            if (this->state_.power_w > 0)
                this->state_.power_w = 0;
        }
        this->publish_state_();
        this->last_state_dirty_ = true;
        this->save_last_state_();
        global_preferences->sync();  // disconnects are rare; do not wait for the periodic flush
    }
    this->phase_ = Phase::IDLE;
    this->transport_ = Transport::NONE;
    this->km_profile_ = nullptr;
    this->km_unanswered_polls_ = 0;
    this->power_seen_ = false;
    this->handshake_step_ = 0;
    this->tx_handle_ = 0;
    this->rx_handle_ = 0;
    this->cccd_handle_ = 0;
    this->cccd_confirmed_ = false;
    this->link_stats_ = {};
    this->enc_handshake_ = EncHandshake::NONE;
    this->enc_write_with_response_ = false;
    this->km_write_with_response_ = false;
    this->enc_session_.reset();
    memset(this->enc_app_random_, 0, sizeof(this->enc_app_random_));
    memset(this->enc_session_key_, 0, sizeof(this->enc_session_key_));
    this->parser_.reset();
    this->km_parser_.reset();
    this->b06_parser_.reset();
}

void CGOComponent::restore_last_state_() {
    this->last_state_pref_ = global_preferences->make_preference<LastState>(fnv1_hash("cgo_last_state"));
    LastState saved{};
    if (!this->last_state_pref_.load(&saved) || saved.magic != LAST_STATE_MAGIC)
        return;
    {
        LockGuard guard(this->state_mutex_);
        this->state_ = saved.state;
    }
    this->last_connected_at_ = saved.connected_at;
    ESP_LOGI(TAG, "Restored last known state (battery %d%%)", saved.state.battery);
    this->publish_state_();
}

void CGOComponent::save_last_state_() {
    if (!this->last_state_dirty_)
        return;
    LastState data{};
    data.magic = LAST_STATE_MAGIC;
    data.connected_at = this->last_connected_at_;
    {
        LockGuard guard(this->state_mutex_);
        data.state = this->state_;
    }
    if (this->last_state_pref_.save(&data)) {
        this->last_state_dirty_ = false;
        this->last_state_saved_ms_ = millis();
    } else {
        ESP_LOGW(TAG, "Saving last state failed");
    }
}

static void publish_if_changed(sensor::Sensor *sensor, float value) {
    if (sensor == nullptr || value < 0)
        return;
    if (!sensor->has_state() || sensor->state != value)
        sensor->publish_state(value);
}

void CGOComponent::publish_state_() {
    const auto &s = this->state_;
    publish_if_changed(this->battery_sensor_, s.battery);
    publish_if_changed(this->speed_sensor_, s.speed_kmh);
    publish_if_changed(this->max_speed_sensor_, s.max_speed_kmh);
    publish_if_changed(this->avg_speed_sensor_, s.avg_speed_kmh);
    publish_if_changed(this->trip_distance_sensor_, s.trip_distance_km);
    publish_if_changed(this->trip_duration_sensor_, s.trip_duration);
    publish_if_changed(this->remaining_range_sensor_, s.remaining_range_km);
    publish_if_changed(this->range_sensor_, s.total_range_km);
    publish_if_changed(this->power_sensor_, s.power_w);

    if (this->serial_number_text_sensor_ != nullptr && s.serial_number[0] != '\0' &&
        this->serial_number_text_sensor_->state != s.serial_number) {
        this->serial_number_text_sensor_->publish_state(s.serial_number);
    }
    if (this->firmware_version_text_sensor_ != nullptr && s.firmware[0] != '\0' &&
        this->firmware_version_text_sensor_->state != s.firmware) {
        this->firmware_version_text_sensor_->publish_state(s.firmware);
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

void CGOComponent::update_ble_status_() {
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

void CGOLightSwitch::write_state(bool state) {
    // Optimistic: the next poll publishes the bike's actual light state.
    if (this->parent_->set_light(state)) {
        this->publish_state(state);
    } else {
        this->publish_state(this->state);
    }
}

}  // namespace esphome::cgo

#endif  // USE_ESP32
