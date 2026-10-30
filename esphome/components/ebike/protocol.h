#pragma once

// Framework-neutral protocol code of every supported display: frames, CRCs and
// field decoding.

#include <cstddef>
#include <cstdint>

namespace esphome::ebike::protocol {

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
static constexpr uint8_t FIELD_ODOMETER = 0x17;
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
    float odometer_km = -1;
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
    // was decoded; frames failing the CRC check are counted in bad_crc.
    bool feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc);

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

// One driver of the Tenways app: what a display is asked for, how its
// requests are checksummed and how its values decode.
struct Profile {
    const char *name;
    const uint8_t *status_keys;
    uint8_t status_count;
    const uint8_t *info_keys;
    uint8_t info_count;
    bool crc_low_first;
    // Try the encryption handshake first (see crypto.h); other bikes try
    // it only after getting no answer.
    bool try_encryption;
    // Written with response, like the app does for these models.
    bool write_with_response;
    // CGO800: the odometer in whole km, firmware as ASCII and any range byte
    // >= 0x80 meaning no range. Others: odometer / 10, firmware as dotted
    // bytes and 655 or 32767 meaning no range.
    bool cgo800_values;
};

extern const Profile CGO800_PROFILE;
extern const Profile CGO009_PROFILE;
extern const Profile LONGTAIL_PROFILE;
extern const Profile AGO_AIR_PROFILE;
extern const Profile AGO_PROFILE;

// Picks the profile from the advertised name like the app does; other and
// missing names get the AGO profile, the app's default.
const Profile &profile_for(const char *device_name);
// The profile to try when a display answers nothing: the other CRC order.
const Profile &alternative_to(const Profile &profile);

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
    bool feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc,
              uint32_t &unhandled);
    void set_profile(const Profile *profile) { this->profile_ = profile; }

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
    const Profile *profile_{&AGO_PROFILE};
};

}  // namespace km

// B06 displays (BIKEGO plus app) over the Microchip transparent UART service.
// Frames in both directions:
//   0xA5 [len] [cmd] [payload...] [crc_lo] [crc_hi] 0x0D
// len counts the payload and the checksum is CRC-16/XMODEM over len..payload.
// The display needs an AES challenge-response (see crypto.h) and then
// pushes its ride and battery frames on its own; it is never polled.
namespace b06 {

static constexpr uint8_t FRAME_START = 0xA5;
static constexpr uint8_t FRAME_END = 0x0D;
static constexpr size_t OVERHEAD = 6;  // start, len, cmd, crc, crc, end

static constexpr uint8_t CMD_AUTH_START = 0x20;
static constexpr uint8_t CMD_AUTH_ANSWER = 0x21;
static constexpr uint8_t CMD_VERSION = 0x40;
static constexpr uint8_t CMD_RIDE = 0x41;
static constexpr uint8_t CMD_BATTERY = 0x42;
static constexpr uint8_t CMD_CONFIG = 0x43;
static constexpr size_t AUTH_CHALLENGE_LEN = 4;

uint16_t crc16(const uint8_t *data, size_t len);

// Builds a frame; out must hold len + OVERHEAD bytes.
size_t build_frame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len);
// Builds the configuration write that switches the light; out must hold
// 4 + OVERHEAD bytes.
size_t build_light(uint8_t *out, bool on);

// Reassembles notification chunks into frames, decodes the ride, battery and
// version frames into the shared BikeState and keeps the authentication
// replies for the component.
class FrameParser {
 public:
    static constexpr size_t BUFFER_SIZE = 2 * (0xFF + OVERHEAD);

    void reset() {
        this->len_ = 0;
        this->frames_ = 0;
        this->challenge_ready_ = false;
        this->auth_result_ = AuthResult::NONE;
    }
    // Same contract as km::FrameParser::feed.
    bool feed(BikeState &state, const uint8_t *data, size_t len, uint32_t &bad_crc,
              uint32_t &unhandled);

    // The display's challenge, once per received challenge frame.
    bool take_challenge(uint8_t out[AUTH_CHALLENGE_LEN]);
    enum class AuthResult : uint8_t { NONE, OK, FAILED };
    AuthResult auth_result() const { return this->auth_result_; }
    uint32_t frames() const { return this->frames_; }

 private:
    // Returns false for frames we do not know; sets changed when state was updated.
    bool handle_frame_(BikeState &state, uint8_t cmd, const uint8_t *payload, uint8_t len, bool &changed);

    uint8_t buffer_[BUFFER_SIZE]{};
    size_t len_{0};
    uint32_t frames_{0};
    uint8_t challenge_[AUTH_CHALLENGE_LEN]{};
    bool challenge_ready_{false};
    AuthResult auth_result_{AuthResult::NONE};
};

}  // namespace b06

}  // namespace esphome::ebike::protocol
