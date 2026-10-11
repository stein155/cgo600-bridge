#pragma once

// Framework-neutral CGO600 protocol helpers: frame building, CRC and field
// decoding.

#include <cstddef>
#include <cstdint>

namespace esphome::cgo600::protocol {

static constexpr uint8_t FRAME_START = 0x52;
static constexpr uint8_t FRAME_END = 0x45;

// Field IDs inferred from the Tenways app protocol
static constexpr uint8_t FIELD_SN = 0x07;
static constexpr uint8_t FIELD_SPEED = 0x11;
static constexpr uint8_t FIELD_MAX_SPEED = 0x12;
static constexpr uint8_t FIELD_AVG_SPEED = 0x13;
static constexpr uint8_t FIELD_TRIP_DISTANCE = 0x14;
static constexpr uint8_t FIELD_TRIP_DURATION = 0x15;
static constexpr uint8_t FIELD_REMAINING_RANGE = 0x16;
static constexpr uint8_t FIELD_TOTAL_RANGE = 0x17;
static constexpr uint8_t FIELD_BATTERY = 0x1A;
static constexpr uint8_t FIELD_LIGHT = 0x21;

struct Command {
    const uint8_t *data;
    size_t len;
};

extern const Command READ_INIT_CMD;
extern const Command READ_DATA_CMD;
extern const Command BOOTSTRAP_CMD_2;
extern const Command BOOTSTRAP_CMD_3;
extern const Command LIGHT_ON_CMD;
extern const Command LIGHT_OFF_CMD;

struct BikeState {
    int battery = -1;
    int trip_duration = -1;
    float speed_kmh = -1;
    float max_speed_kmh = -1;
    float avg_speed_kmh = -1;
    float trip_distance_km = -1;
    float remaining_range_km = -1;
    float total_range_km = -1;
    bool light_on = false;
    bool light_valid = false;
    char serial_number[32] = {0};
};

uint16_t crc16(const uint8_t *data, size_t len);

// Builds a read request for the given field IDs; out must hold count + 5 bytes.
size_t build_request(uint8_t *out, const uint8_t *field_ids, size_t count);

// Decodes one field into state. Returns false when the field is unknown or
// the value is implausible and was ignored.
bool decode_field(BikeState &state, uint8_t field_id, const uint8_t *data, uint8_t len);

// Reassembles notification chunks into frames and decodes their fields.
class FrameParser {
 public:
    static constexpr size_t BUFFER_SIZE = 512;

    void reset() { this->len_ = 0; }
    // Appends data and decodes every complete frame. Returns true if any field
    // was decoded. saw_trip is set when a frame carried the trip counters;
    // frames failing the CRC check are counted in bad_crc.
    bool feed(BikeState &state, const uint8_t *data, size_t len, bool &saw_trip, uint32_t &bad_crc);

 private:
    void append_(const uint8_t *data, size_t len);

    uint8_t buffer_[BUFFER_SIZE]{};
    size_t len_{0};
};

}  // namespace esphome::cgo600::protocol
