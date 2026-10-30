#pragma once

// Session encryption used by bikes the Tenways server marks as encrypted, on
// both the King Meter and the Nordic UART transport. Such a display (seen on
// the CGO800 Plus) answers nothing but an AT+NAME error until the app has run
// a key exchange; after it every frame travels inside an AES-256-CBC packet:
//   0xA5 [0x80 | len_hi] [len_lo] [seq_hi] [seq_lo] [ciphertext] [crc_hi] [crc_lo] 0x5A
// len is the ciphertext length, seq counts packets from 1 and the CRC is
// CRC-16/MODBUS over 0xA5..ciphertext. All packets use the session IV.

#include <cstddef>
#include <cstdint>

namespace esphome::ebike::protocol {

static constexpr uint8_t KEY_HANDSHAKE_RANDOM = 0xD8;
static constexpr uint8_t KEY_HANDSHAKE_IV = 0xD2;
static constexpr size_t HANDSHAKE_VALUE_LEN = 16;
static constexpr size_t PACKET_OVERHEAD = 8;  // 0xA5, len, seq, crc, 0x5A

// HKDF-SHA256 with the app's random as salt, the app's fixed secret as input
// key and the display's random as info.
bool derive_session_key(const uint8_t app_random[16], const uint8_t device_random[16], uint8_t key[32]);

// The B06 display's challenge-response: AES-128-ECB with a key fixed in the
// BIKEGO plus app over the 4-byte challenge padded with zeros to one block.
bool b06_auth_answer(const uint8_t challenge[4], uint8_t answer[16]);

// Returns the 16-byte value of a handshake reply
// ([type] 0x00 [len] 0x01 [key] 0x10 [value...]), or nullptr for other frames.
const uint8_t *km_handshake_value(const uint8_t *data, size_t len, uint8_t type, uint8_t key);
// The same for the Nordic UART framing of the CGO600 family
// ([type] [len] [key] 0x10 [value...]).
const uint8_t *nus_handshake_value(const uint8_t *data, size_t len, uint8_t type, uint8_t key);

class Session {
 public:
    void start(const uint8_t key[32], const uint8_t iv[16]);
    void reset();
    bool active() const { return this->active_; }
    // Encrypts one King Meter frame into a packet. Returns the packet length,
    // or 0 when out is too small or no session is active.
    size_t wrap(const uint8_t *frame, size_t len, uint8_t *out, size_t out_size);
    // Checks and decrypts one packet. Returns the frame length, or 0 when the
    // packet is malformed, fails its CRC or does not decrypt.
    size_t unwrap(const uint8_t *packet, size_t len, uint8_t *out, size_t out_size);

 private:
    uint8_t key_[32]{};
    uint8_t iv_[16]{};
    uint16_t sequence_{0};
    bool active_{false};
};

}  // namespace esphome::ebike::protocol
