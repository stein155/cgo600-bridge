#include "protocol.h"

#include <cstdio>
#include <cstring>

namespace esphome::ebike::protocol {

// Pre-built commands, captured from the Tenways app
static const uint8_t READ_INIT_BYTES[] = {0x52, 0x02, 0x07, 0x03, 0xF3, 0x6D, 0x45};
static const uint8_t READ_DATA_BYTES[] = {0x57, 0x0D, 0xFE, 0x0B, 0x07, 0x12, 0x13, 0x14, 0x11,
                                          0x16, 0x17, 0x1A, 0x28, 0x21, 0x15, 0xA2, 0x77, 0x45};
static const uint8_t BOOTSTRAP_2_BYTES[] = {0x57, 0x04, 0xFD, 0x02, 0xF4, 0x01, 0xEB, 0x50, 0x45};
static const uint8_t BOOTSTRAP_3_BYTES[] = {0x57, 0x03, 0xFC, 0x01, 0x01, 0x51, 0xE8, 0x45};
static const uint8_t LIGHT_ON_BYTES[] = {0x57, 0x03, 0x21, 0x01, 0x01, 0xC1, 0xD2, 0x45};
static const uint8_t LIGHT_OFF_BYTES[] = {0x57, 0x03, 0x21, 0x01, 0x00, 0x00, 0x12, 0x45};

const Command READ_INIT_CMD{READ_INIT_BYTES, sizeof(READ_INIT_BYTES)};
const Command READ_DATA_CMD{READ_DATA_BYTES, sizeof(READ_DATA_BYTES)};
const Command BOOTSTRAP_CMD_2{BOOTSTRAP_2_BYTES, sizeof(BOOTSTRAP_2_BYTES)};
const Command BOOTSTRAP_CMD_3{BOOTSTRAP_3_BYTES, sizeof(BOOTSTRAP_3_BYTES)};
const Command LIGHT_ON_CMD{LIGHT_ON_BYTES, sizeof(LIGHT_ON_BYTES)};
const Command LIGHT_OFF_CMD{LIGHT_OFF_BYTES, sizeof(LIGHT_OFF_BYTES)};

// Upper sanity bound for a decoded remaining-range value.
static constexpr float MAX_PLAUSIBLE_RANGE_KM = 2000.0f;

// CRC-16/MODBUS (reflected polynomial 0xA001, init 0xFFFF).
uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

size_t build_request(uint8_t *out, const uint8_t *field_ids, size_t count) {
    out[0] = FRAME_START;
    out[1] = (uint8_t) count;
    memcpy(out + 2, field_ids, count);
    const uint16_t crc = crc16(out, 2 + count);
    out[2 + count] = crc & 0xFF;
    out[3 + count] = (crc >> 8) & 0xFF;
    out[4 + count] = FRAME_END;
    return 5 + count;
}

size_t build_write(uint8_t *out, uint8_t field_id, const uint8_t *value, uint8_t size) {
    out[0] = FRAME_WRITE;
    out[1] = size + 2;
    out[2] = field_id;
    out[3] = size;
    memcpy(out + 4, value, size);
    const uint16_t crc = crc16(out, 4 + size);
    out[4 + size] = crc & 0xFF;
    out[5 + size] = (crc >> 8) & 0xFF;
    out[6 + size] = FRAME_END;
    return 7 + size;
}

static void copy_ascii(char *out, size_t out_size, const uint8_t *data, uint8_t len) {
    size_t j = 0;
    for (uint8_t i = 0; i < len && j < out_size - 1; i++) {
        if (data[i] >= 32 && data[i] <= 126)
            out[j++] = (char) data[i];
    }
    out[j] = '\0';
}

// Upper bound the Tenways app accepts for the motor power.
static constexpr uint32_t MAX_PLAUSIBLE_POWER_W = 3000;

static uint32_t int_be(const uint8_t *data, uint8_t len) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < len; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

bool decode_field(BikeState &state, uint8_t field_id, const uint8_t *data, uint8_t len) {
    switch (field_id) {
        case FIELD_SN:
            copy_ascii(state.serial_number, sizeof(state.serial_number), data, len);
            return true;
        case FIELD_FIRMWARE:
            copy_ascii(state.firmware, sizeof(state.firmware), data, len);
            return true;
        case FIELD_POWER: {
            const uint32_t watts = int_be(data, len);
            if (len == 0 || watts > MAX_PLAUSIBLE_POWER_W)
                return false;
            state.power_w = (int) watts;
            return true;
        }
        case FIELD_BATTERY:
            if (len == 0)
                return false;
            state.battery = data[0];
            return true;
        case FIELD_SPEED:
            state.speed_kmh = int_be(data, len) / 10.0f;
            return true;
        case FIELD_MAX_SPEED:
            state.max_speed_kmh = int_be(data, len) / 10.0f;
            return true;
        case FIELD_AVG_SPEED:
            state.avg_speed_kmh = int_be(data, len) / 10.0f;
            return true;
        case FIELD_TRIP_DISTANCE:
            state.trip_distance_km = int_be(data, len) / 10.0f;
            return true;
        case FIELD_TRIP_DURATION:
            state.trip_duration = (int) int_be(data, len);
            return true;
        case FIELD_REMAINING_RANGE: {
            if (len < 2)
                return false;
            // 0xFFFF (optionally padded with 0x0000) means "unknown"
            if (data[0] == 0xFF && data[1] == 0xFF && (len == 2 || (len == 4 && data[2] == 0x00 && data[3] == 0x00)))
                return false;
            const uint8_t decode_len = (len >= 4 && data[2] == 0x00 && data[3] == 0x00) ? 2 : len;
            const float km = int_be(data, decode_len) / 10.0f;
            if (km > MAX_PLAUSIBLE_RANGE_KM)
                return false;
            state.remaining_range_km = km;
            return true;
        }
        case FIELD_ODOMETER:
            state.odometer_km = int_be(data, len) / 10.0f;
            return true;
        case FIELD_LIGHT:
            if (len == 0)
                return false;
            state.light_on = data[0] != 0;
            state.light_valid = true;
            return true;
        default:
            return false;
    }
}

void FrameParser::append_(const uint8_t *data, size_t len) {
    if (len >= BUFFER_SIZE) {
        memcpy(this->buffer_, data + (len - BUFFER_SIZE), BUFFER_SIZE);
        this->len_ = BUFFER_SIZE;
        return;
    }
    const size_t free_space = BUFFER_SIZE - this->len_;
    if (len > free_space) {
        const size_t drop = len - free_space;
        if (drop >= this->len_) {
            this->len_ = 0;
        } else {
            memmove(this->buffer_, this->buffer_ + drop, this->len_ - drop);
            this->len_ -= drop;
        }
    }
    memcpy(this->buffer_ + this->len_, data, len);
    this->len_ += len;
}

bool FrameParser::feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc) {
    bool changed = false;
    if (data != nullptr && len > 0) {
        this->append_(data, len);
    }

    while (this->len_ >= 6) {
        size_t start = 0;
        while (start < this->len_ && this->buffer_[start] != FRAME_START) {
            ++start;
        }
        if (start >= this->len_) {
            this->len_ = 0;
            break;
        }
        if (start > 0) {
            memmove(this->buffer_, this->buffer_ + start, this->len_ - start);
            this->len_ -= start;
        }

        const uint8_t payload_len = this->buffer_[1];
        const size_t frame_len = 2 + payload_len + 2 + 1;
        if (this->len_ < frame_len) {
            break;
        }

        if (this->buffer_[frame_len - 1] == FRAME_END) {
            // Inbound frames carry the CRC high byte first, whereas requests
            // (build_request) send it low byte first. That asymmetry is how the
            // bike's protocol behaves.
            const uint16_t expected = ((uint16_t) this->buffer_[2 + payload_len] << 8) | this->buffer_[3 + payload_len];
            if (crc16(this->buffer_, 2 + payload_len) != expected) {
                ++bad_crc;
            } else {
                size_t pos = 2;
                const size_t payload_end = 2 + payload_len;
                while (pos + 2 <= payload_end) {
                    const uint8_t field_id = this->buffer_[pos++];
                    const uint8_t field_len = this->buffer_[pos++];
                    if (pos + field_len > payload_end) {
                        break;
                    }
                    if (decode_field(state, field_id, this->buffer_ + pos, field_len)) {
                        changed = true;
                    }
                    pos += field_len;
                }
            }
        }

        if (frame_len < this->len_) {
            memmove(this->buffer_, this->buffer_ + frame_len, this->len_ - frame_len);
        }
        this->len_ -= frame_len;
    }
    return changed;
}

namespace km {

// Requests of the Tenways app 2.18.0 per driver.
static const uint8_t CGO800_STATUS[] = {0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x11, 0x23};
static const uint8_t CGO800_INFO[] = {0x04, 0x05};
static const uint8_t CGO009_STATUS[] = {0x06, 0x07, 0x08, 0x09, 0x11, 0x12, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F, 0x50};
static const uint8_t CGO009_INFO[] = {0x04, 0x05, 0x70, 0x4F, 0x50};
static const uint8_t LONGTAIL_STATUS[] = {0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F};
static const uint8_t LONGTAIL_INFO[] = {0x04, 0x05, 0x4F, 0x70};
static const uint8_t AGO_AIR_INFO[] = {0x04, 0x05, 0x4F, 0x76};
static const uint8_t AGO_STATUS[] = {0x05, 0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23};
static const uint8_t AGO_INFO[] = {0x04, 0x05};

#define KEYS(name) name, sizeof(name)
const Profile CGO800_PROFILE{"CGO800", KEYS(CGO800_STATUS), KEYS(CGO800_INFO), true, true, false, true};
const Profile CGO009_PROFILE{"CGO009", KEYS(CGO009_STATUS), KEYS(CGO009_INFO), false, false, false, false};
const Profile LONGTAIL_PROFILE{"Longtail Duo", KEYS(LONGTAIL_STATUS), KEYS(LONGTAIL_INFO), false, false, false, false};
const Profile AGO_AIR_PROFILE{"AGO AIR", KEYS(LONGTAIL_STATUS), KEYS(AGO_AIR_INFO), false, false, true, false};
const Profile AGO_PROFILE{"AGO", KEYS(AGO_STATUS), KEYS(AGO_INFO), false, false, false, false};
#undef KEYS

static bool starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }

const Profile &profile_for(const char *device_name) {
    // Same order as the app's driver selection.
    if (starts_with(device_name, "CGO800"))
        return CGO800_PROFILE;
    if (starts_with(device_name, "CGO009"))
        return CGO009_PROFILE;
    if (starts_with(device_name, "Longtail Duo"))
        return LONGTAIL_PROFILE;
    if (starts_with(device_name, "AGO AIR") || starts_with(device_name, "Wayfarer_B") ||
        starts_with(device_name, "CGO600_D"))
        return AGO_AIR_PROFILE;
    return AGO_PROFILE;
}

const Profile &alternative_to(const Profile &profile) {
    return profile.crc_low_first ? AGO_PROFILE : CGO800_PROFILE;
}

static size_t finish_frame(uint8_t *out, size_t body_len, bool crc_low_first) {
    const uint16_t crc = crc16(out, body_len);
    out[body_len] = crc_low_first ? crc & 0xFF : crc >> 8;
    out[body_len + 1] = crc_low_first ? crc >> 8 : crc & 0xFF;
    out[body_len + 2] = END_BYTE;
    return body_len + 3;
}

size_t build_read(uint8_t *out, const uint8_t *keys, uint8_t count, bool crc_low_first) {
    out[0] = TYPE_READ;
    out[1] = HEADER_BYTE;
    out[2] = count + 1;
    out[3] = count;
    memcpy(out + 4, keys, count);
    return finish_frame(out, 4 + count, crc_low_first);
}

size_t build_write(uint8_t *out, uint8_t key, const uint8_t *value, uint8_t size, bool crc_low_first) {
    out[0] = TYPE_WRITE;
    out[1] = HEADER_BYTE;
    out[2] = size + 3;
    out[3] = 1;
    out[4] = key;
    out[5] = size;
    memcpy(out + 6, value, size);
    return finish_frame(out, 6 + size, crc_low_first);
}

static constexpr uint8_t KEY_FIRMWARE = 0x04;
static constexpr uint8_t KEY_SERIAL = 0x05;
static constexpr uint8_t KEY_SPEED = 0x06;
static constexpr uint8_t KEY_MAX_SPEED = 0x07;
static constexpr uint8_t KEY_AVG_SPEED = 0x08;
static constexpr uint8_t KEY_TRIP_DISTANCE = 0x09;
static constexpr uint8_t KEY_RIDE_TIME = 0x0A;
static constexpr uint8_t KEY_REMAINING_RANGE = 0x0B;
static constexpr uint8_t KEY_ODOMETER = 0x0C;
static constexpr uint8_t KEY_BATTERY = 0x0F;
static constexpr uint8_t KEY_POWER = 0x23;

// Several keys collide with CGO600 field IDs (0x11 is the light here but the
// speed on the CGO600), so translate them to the CGO600 IDs before decoding.
// Returns 0 for keys we do not decode.
static uint8_t to_field_id(uint8_t key) {
    switch (key) {
        case KEY_FIRMWARE:
            return FIELD_FIRMWARE;
        case KEY_SERIAL:
            return FIELD_SN;
        case KEY_SPEED:
            return FIELD_SPEED;
        case KEY_MAX_SPEED:
            return FIELD_MAX_SPEED;
        case KEY_AVG_SPEED:
            return FIELD_AVG_SPEED;
        case KEY_RIDE_TIME:
            return FIELD_TRIP_DURATION;
        case KEY_TRIP_DISTANCE:
            return FIELD_TRIP_DISTANCE;
        case KEY_REMAINING_RANGE:
            return FIELD_REMAINING_RANGE;
        case KEY_ODOMETER:
            return FIELD_ODOMETER;
        case KEY_BATTERY:
            return FIELD_BATTERY;
        case KEY_POWER:
            return FIELD_POWER;
        case KEY_LIGHT:
            return FIELD_LIGHT;
        default:
            return 0;
    }
}

// Decodes the values whose King Meter encoding differs from the CGO600 field
// they map to; the rest decode like CGO600 fields.
static bool decode_km_field(BikeState &state, const Profile &profile, uint8_t field_id, const uint8_t *data,
                            uint8_t len) {
    switch (field_id) {
        case FIELD_REMAINING_RANGE: {
            // Whole kilometres; the no-range values are described at Profile.
            if (len == 0)
                return false;
            const uint32_t km = int_be(data, len);
            if (profile.cgo800_values) {
                for (uint8_t i = 0; i < len; i++) {
                    if (data[i] >= 0x80)
                        return false;
                }
            } else if (km == 655 || km == 32767) {
                return false;
            }
            state.remaining_range_km = (float) km;
            return true;
        }
        case FIELD_ODOMETER:
            if (!profile.cgo800_values)
                return decode_field(state, field_id, data, len);
            state.odometer_km = (float) int_be(data, len);
            return true;
        case FIELD_FIRMWARE: {
            if (profile.cgo800_values)
                return decode_field(state, field_id, data, len);
            // Dotted bytes, such as 01 02 03 for "1.2.3".
            size_t pos = 0;
            for (uint8_t i = 0; i < len && pos + 4 < sizeof(state.firmware); i++)
                pos += snprintf(state.firmware + pos, sizeof(state.firmware) - pos, i ? ".%u" : "%u", data[i]);
            return len > 0;
        }
        default:
            return decode_field(state, field_id, data, len);
    }
}

static bool is_frame_type(uint8_t b) { return b == TYPE_READ || b == TYPE_WRITE; }

void FrameParser::append_(const uint8_t *data, size_t len) {
    // Unlike the CGO600 parser, keep the start of the buffer: a frame is only
    // found from its first byte. Overflow means garbage, so start over.
    if (len > BUFFER_SIZE - this->len_) {
        this->skipped_bytes_ += this->len_;
        this->len_ = 0;
        if (len > BUFFER_SIZE) {
            this->skipped_bytes_ += len;
            return;
        }
    }
    memcpy(this->buffer_ + this->len_, data, len);
    this->len_ += len;
}

// Drops bytes until the buffer starts with a plausible frame header.
void FrameParser::resync_() {
    size_t start = 0;
    while (start < this->len_) {
        if (is_frame_type(this->buffer_[start]) &&
            (start + 1 >= this->len_ || this->buffer_[start + 1] == HEADER_BYTE))
            break;
        ++start;
    }
    if (start > 0) {
        memmove(this->buffer_, this->buffer_ + start, this->len_ - start);
        this->len_ -= start;
        this->skipped_bytes_ += start;
    }
}

bool FrameParser::feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc,
                       uint32_t &unhandled) {
    bool changed = false;
    if (data != nullptr && len > 0) {
        this->append_(data, len);
    }

    while (true) {
        this->resync_();
        if (this->len_ < 3)
            break;
        const size_t frame_len = this->buffer_[2] + OVERHEAD;
        if (this->len_ < frame_len)
            break;
        const uint8_t *frame = this->buffer_;
        if (frame[2] == 0 || frame[frame_len - 1] != END_BYTE) {
            // Wrong length byte: this was not a frame start, search again.
            memmove(this->buffer_, this->buffer_ + 1, this->len_ - 1);
            --this->len_;
            ++this->skipped_bytes_;
            continue;
        }

        const uint16_t crc = crc16(frame, frame_len - 3);
        const uint16_t high_first = ((uint16_t) frame[frame_len - 3] << 8) | frame[frame_len - 2];
        const uint16_t low_first = ((uint16_t) frame[frame_len - 2] << 8) | frame[frame_len - 3];
        if (crc != high_first && crc != low_first) {
            ++bad_crc;
        } else if (frame[0] != TYPE_READ) {
            ++this->frames_;
            ++unhandled;
        } else {
            ++this->frames_;
            const size_t payload_end = frame_len - 3;
            size_t pos = 4;
            bool decoded = false;
            while (pos + 2 <= payload_end) {
                const uint8_t key = frame[pos++];
                const uint8_t size = frame[pos++];
                if (pos + size > payload_end)
                    break;
                const uint8_t field_id = to_field_id(key);
                if (field_id != 0 && decode_km_field(state, *this->profile_, field_id, frame + pos, size)) {
                    changed = true;
                    decoded = true;
                }
                pos += size;
            }
            if (!decoded)
                ++unhandled;
        }

        memmove(this->buffer_, this->buffer_ + frame_len, this->len_ - frame_len);
        this->len_ -= frame_len;
    }
    return changed;
}

}  // namespace km

namespace b06 {

// Payload sizes the BIKEGO plus app 1.6.1 decodes.
static constexpr uint8_t RIDE_LEN = 29;
static constexpr uint8_t BATTERY_LEN = 6;

// CRC-16/XMODEM (polynomial 0x1021, init 0).
uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t) data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

size_t build_frame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len) {
    out[0] = FRAME_START;
    out[1] = len;
    out[2] = cmd;
    if (len > 0)
        memcpy(out + 3, payload, len);
    const uint16_t crc = crc16(out + 1, 2 + len);
    out[3 + len] = crc & 0xFF;
    out[4 + len] = crc >> 8;
    out[5 + len] = FRAME_END;
    return OVERHEAD + len;
}

size_t build_light(uint8_t *out, bool on) {
    // [write] [CONTENT_CONFIG_LIGHT] [value], as the app's MeterConfig.lightOn/lightOff.
    const uint8_t payload[] = {0x02, 0x0E, 0x01, (uint8_t) (on ? 1 : 0)};
    return build_frame(out, CMD_CONFIG, payload, sizeof(payload));
}

static uint16_t u16_le(const uint8_t *data) { return data[0] | (data[1] << 8); }
static uint32_t u32_le(const uint8_t *data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t) data[3] << 24);
}

bool FrameParser::take_challenge(uint8_t out[AUTH_CHALLENGE_LEN]) {
    if (!this->challenge_ready_)
        return false;
    memcpy(out, this->challenge_, AUTH_CHALLENGE_LEN);
    this->challenge_ready_ = false;
    return true;
}

bool FrameParser::handle_frame_(BikeState &state, uint8_t cmd, const uint8_t *payload, uint8_t len,
                                bool &changed) {
    switch (cmd) {
        case CMD_AUTH_START:
            if (len < AUTH_CHALLENGE_LEN)
                return false;
            memcpy(this->challenge_, payload, AUTH_CHALLENGE_LEN);
            this->challenge_ready_ = true;
            return true;
        case CMD_AUTH_ANSWER:
            if (len == 0)
                return false;
            this->auth_result_ = payload[0] == 0 ? AuthResult::OK : AuthResult::FAILED;
            return true;
        case CMD_VERSION: {
            // [protocol type] then the software and hardware version as ASCII,
            // separated by a non-printable byte; keep the software version.
            size_t j = 0;
            for (uint8_t i = 1; i < len && j < sizeof(state.firmware) - 1; i++) {
                if (payload[i] < 32 || payload[i] > 126)
                    break;
                state.firmware[j++] = (char) payload[i];
            }
            state.firmware[j] = '\0';
            changed |= j > 0;
            return true;
        }
        case CMD_RIDE:
            if (len < RIDE_LEN)
                return false;
            state.speed_kmh = u16_le(payload) / 10.0f;
            state.max_speed_kmh = u16_le(payload + 2) / 10.0f;
            state.avg_speed_kmh = u16_le(payload + 4) / 100.0f;
            // Bytes 6-7 are the app's "range", which it never displays; on a
            // real display they read 16665 at 81 % battery, so they are skipped.
            state.trip_duration = (int) u32_le(payload + 8);
            state.trip_distance_km = u32_le(payload + 12) / 100.0f;
            // The odometer, as on the King Meter bikes.
            state.odometer_km = u32_le(payload + 16) / 100.0f;
            state.light_on = payload[21] != 0;
            state.light_valid = true;
            changed = true;
            return true;
        case CMD_BATTERY:
            if (len < BATTERY_LEN)
                return false;
            state.battery = payload[0];
            changed = true;
            return true;
        default:
            return false;
    }
}

bool FrameParser::feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc,
                       uint32_t &unhandled) {
    bool changed = false;
    if (data != nullptr && len > 0) {
        // Overflow means garbage, so start over.
        if (len > BUFFER_SIZE - this->len_)
            this->len_ = 0;
        if (len > BUFFER_SIZE)
            return false;
        memcpy(this->buffer_ + this->len_, data, len);
        this->len_ += len;
    }

    while (true) {
        size_t start = 0;
        while (start < this->len_ && this->buffer_[start] != FRAME_START)
            ++start;
        if (start > 0) {
            memmove(this->buffer_, this->buffer_ + start, this->len_ - start);
            this->len_ -= start;
        }
        if (this->len_ < 2)
            break;
        const size_t frame_len = this->buffer_[1] + OVERHEAD;
        if (this->len_ < frame_len)
            break;
        const uint8_t *frame = this->buffer_;
        const uint8_t payload_len = frame[1];
        if (frame[frame_len - 1] != FRAME_END) {
            // Not a frame start after all, search again.
            memmove(this->buffer_, this->buffer_ + 1, this->len_ - 1);
            --this->len_;
            continue;
        }

        const uint16_t crc = crc16(frame + 1, 2 + payload_len);
        if (frame[3 + payload_len] != (crc & 0xFF) || frame[4 + payload_len] != (crc >> 8)) {
            ++bad_crc;
        } else {
            ++this->frames_;
            if (!this->handle_frame_(state, frame[2], frame + 3, payload_len, changed))
                ++unhandled;
        }

        memmove(this->buffer_, this->buffer_ + frame_len, this->len_ - frame_len);
        this->len_ -= frame_len;
    }
    return changed;
}

}  // namespace b06

}  // namespace esphome::ebike::protocol
