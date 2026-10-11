#include "cgo600_protocol.h"

#include <cstring>

namespace esphome::cgo600::protocol {

// Pre-built commands, byte-identical to firmware/src/protocol.cpp
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

}  // namespace esphome::cgo600::protocol
