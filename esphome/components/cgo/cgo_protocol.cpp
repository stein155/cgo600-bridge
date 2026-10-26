#include "cgo_protocol.h"

#include <cstring>

namespace esphome::cgo::protocol {

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

static uint32_t int_be(const uint8_t *data, uint8_t len) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < len; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

bool decode_field(BikeState &state, uint8_t field_id, const uint8_t *data, uint8_t len) {
    switch (field_id) {
        case FIELD_SN: {
            size_t j = 0;
            for (uint8_t i = 0; i < len && j < sizeof(state.serial_number) - 1; i++) {
                if (data[i] >= 32 && data[i] <= 126) {
                    state.serial_number[j++] = (char) data[i];
                }
            }
            state.serial_number[j] = '\0';
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
            if (state.total_range_km > 0.0f && km > state.total_range_km)
                return false;
            if (km > MAX_PLAUSIBLE_RANGE_KM)
                return false;
            state.remaining_range_km = km;
            return true;
        }
        case FIELD_TOTAL_RANGE:
            state.total_range_km = int_be(data, len) / 10.0f;
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

bool FrameParser::feed(BikeState &state, const uint8_t *data, size_t len, bool &saw_trip, uint32_t &bad_crc) {
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
                    if (field_id == FIELD_TRIP_DISTANCE || field_id == FIELD_TRIP_DURATION) {
                        saw_trip = true;
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

// Pre-built read requests, copied from the tenways-ble capture.
static const uint8_t STATUS_REQUEST_BYTES[] = {0x62, 0x00, 0x0C, 0x0B, 0x06, 0x07, 0x08, 0x09, 0x11,
                                               0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F, 0x84, 0x91, 0x45};
static const uint8_t INFO_REQUEST_BYTES[] = {0x62, 0x00, 0x05, 0x04, 0x04, 0x05, 0x4F, 0x70, 0x8A, 0x12, 0x45};

const Command STATUS_REQUEST{STATUS_REQUEST_BYTES, sizeof(STATUS_REQUEST_BYTES)};
const Command INFO_REQUEST{INFO_REQUEST_BYTES, sizeof(INFO_REQUEST_BYTES)};

static constexpr uint8_t KEY_SPEED = 0x06;
static constexpr uint8_t KEY_MAX_SPEED = 0x07;
static constexpr uint8_t KEY_AVG_SPEED = 0x08;
static constexpr uint8_t KEY_TRIP_DISTANCE = 0x09;
static constexpr uint8_t KEY_RIDE_TIME = 0x0A;
static constexpr uint8_t KEY_REMAINING_RANGE = 0x0B;
static constexpr uint8_t KEY_ODOMETER = 0x0C;
static constexpr uint8_t KEY_BATTERY = 0x0F;
// Follows the light on the Longtail Duo (0 = off); "on" is not yet confirmed.
static constexpr uint8_t KEY_LIGHT = 0x11;
static constexpr uint8_t KEY_SERIAL = 0x70;

// Keys are scoped to their command, and several collide with CGO600 field IDs
// (0x11 is the light here but the speed on the CGO600), so translate them to
// the CGO600 IDs before decoding. Returns 0 for keys we do not decode.
static uint8_t to_field_id(uint8_t cmd, uint8_t key) {
    if (cmd == CMD_INFO)
        return key == KEY_SERIAL ? FIELD_SN : 0;
    if (cmd != CMD_STATUS)
        return 0;
    switch (key) {
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
            return FIELD_TOTAL_RANGE;
        case KEY_BATTERY:
            return FIELD_BATTERY;
        case KEY_LIGHT:
            return FIELD_LIGHT;
        default:
            return 0;
    }
}

// The remaining range is the only King Meter value encoded differently from
// the CGO600 field it maps to.
static bool decode_km_field(BikeState &state, uint8_t field_id, const uint8_t *data, uint8_t len) {
    if (field_id != FIELD_REMAINING_RANGE)
        return decode_field(state, field_id, data, len);
    if (len == 0)
        return false;
    // Whole kilometres. The app treats any byte >= 0x80 as no range, which the
    // display reports with assist level 0 (0x028F).
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] >= 0x80)
            return false;
    }
    state.remaining_range_km = (float) int_be(data, len);
    return true;
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

bool FrameParser::feed(BikeState &state, const uint8_t *data, size_t len, bool &saw_trip, uint32_t &bad_crc,
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

        const uint16_t expected = ((uint16_t) frame[frame_len - 3] << 8) | frame[frame_len - 2];
        if (crc16(frame, frame_len - 3) != expected) {
            ++bad_crc;
        } else if (frame[0] != TYPE_READ) {
            ++this->frames_;
            ++unhandled;
        } else {
            ++this->frames_;
            const uint8_t cmd = frame[3];
            const size_t payload_end = frame_len - 3;
            size_t pos = 4;
            bool decoded = false;
            while (pos + 2 <= payload_end) {
                const uint8_t key = frame[pos++];
                const uint8_t size = frame[pos++];
                if (pos + size > payload_end)
                    break;
                const uint8_t field_id = to_field_id(cmd, key);
                if (field_id == FIELD_TRIP_DISTANCE || field_id == FIELD_TRIP_DURATION)
                    saw_trip = true;
                if (field_id != 0 && decode_km_field(state, field_id, frame + pos, size)) {
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

}  // namespace esphome::cgo::protocol
