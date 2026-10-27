#pragma once

// Framework-neutral CGO600 protocol helpers: frame building, CRC and field
// decoding.

#include <cstddef>
#include <cstdint>

namespace esphome::cgo::protocol {

static constexpr uint8_t FRAME_START = 0x52;
static constexpr uint8_t FRAME_END = 0x45;

// Field IDs inferred from the Tenways app protocol
static constexpr uint8_t FIELD_FIRMWARE = 0x03;
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
static constexpr uint8_t FIELD_POWER = 0x23;
static constexpr uint8_t FRAME_WRITE = 0x57;

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
    int power_w = -1;
    char firmware[16] = {0};
};

uint16_t crc16(const uint8_t *data, size_t len);

// Builds a read request for the given field IDs; out must hold count + 5 bytes.
size_t build_request(uint8_t *out, const uint8_t *field_ids, size_t count);
// Builds a write that sets one field; out must hold size + 6 bytes.
size_t build_write(uint8_t *out, uint8_t field_id, const uint8_t *value, uint8_t size);

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

// King Meter transport used by newer Tenways displays such as the CGO800.
// Frames in both directions:
//   [type][0x00][len][count][payload...][crc][crc][0x45]
// type is 0x62 (read) or 0x63 (write), len counts the count byte + payload,
// and count is the number of keys. A read request lists keys; the reply
// carries [key][size][value] entries with big-endian values. The checksum is
// CRC-16/MODBUS over type..payload. The Longtail Duo sends it high byte first
// (tenways-ble, MIT, https://github.com/Styx85/tenways-ble), the CGO800 low
// byte first.
namespace km {

static constexpr uint8_t TYPE_READ = 0x62;
static constexpr uint8_t TYPE_WRITE = 0x63;
static constexpr uint8_t HEADER_BYTE = 0x00;
static constexpr uint8_t END_BYTE = 0x45;
static constexpr size_t OVERHEAD = 6;  // type, header, len, crc, crc, end
static constexpr uint8_t KEY_LIGHT = 0x11;

// What a display is asked for and how its requests are checksummed.
struct Profile {
    const char *name;
    const uint8_t *status_keys;
    uint8_t status_count;
    const uint8_t *info_keys;
    uint8_t info_count;
    bool crc_low_first;
    // Try the encryption handshake first (see cgo_crypto.h).
    bool try_encryption;
};

extern const Profile LONGTAIL_PROFILE;
extern const Profile CGO800_PROFILE;

// Picks the profile from the advertised name; other names get the Longtail Duo's.
const Profile &profile_for(const char *device_name);

// Builds a read request for keys; out must hold count + OVERHEAD + 1 bytes.
size_t build_read(uint8_t *out, const uint8_t *keys, uint8_t count, bool crc_low_first);
// Builds a write that sets one key; out must hold size + 9 bytes.
size_t build_write(uint8_t *out, uint8_t key, const uint8_t *value, uint8_t size, bool crc_low_first);
inline size_t build_write(uint8_t *out, uint8_t key, uint8_t value, bool crc_low_first) {
    return build_write(out, key, &value, 1, crc_low_first);
}

// Reassembles notification chunks into King Meter frames and decodes their
// keys into the shared BikeState. Frames pass with either CRC byte order.
class FrameParser {
 public:
    static constexpr size_t BUFFER_SIZE = 4 * (0xFF + OVERHEAD);

    void reset() {
        this->len_ = 0;
        this->frames_ = 0;
        this->skipped_bytes_ = 0;
    }
    // Same contract as protocol::FrameParser::feed. Valid frames that carry
    // nothing we decode (writes, unknown commands) are counted in unhandled.
    bool feed(BikeState &state, const uint8_t *data, size_t len, bool &saw_trip, uint32_t &bad_crc,
              uint32_t &unhandled);

    // Diagnostics since the last reset: frames that passed the CRC check, and
    // bytes discarded because they did not form a frame.
    uint32_t frames() const { return this->frames_; }
    uint32_t skipped_bytes() const { return this->skipped_bytes_; }

 private:
    void append_(const uint8_t *data, size_t len);
    void resync_();

    uint8_t buffer_[BUFFER_SIZE]{};
    size_t len_{0};
    uint32_t frames_{0};
    uint32_t skipped_bytes_{0};
};

}  // namespace km

}  // namespace esphome::cgo::protocol
