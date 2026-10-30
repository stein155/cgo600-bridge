#include "cgo_crypto.h"

#include <cstring>

#include "cgo_protocol.h"
#include "mbedtls/cipher.h"
#include "mbedtls/md.h"

namespace esphome::cgo::protocol {

static constexpr uint8_t PACKET_START = 0xA5;
static constexpr uint8_t PACKET_END = 0x5A;
static constexpr uint8_t PACKET_ENCRYPTED = 0x80;
static constexpr size_t BLOCK = 16;
// Largest padded frame wrap() encrypts; requests are far smaller.
static constexpr size_t KM_MAX_PLAIN = 256;

// The input key of the session key derivation, fixed in the Tenways app.
static const uint8_t SESSION_SECRET[32] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x11, 0x22, 0x33,
                                           0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE,
                                           0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

// AES-256-CBC without padding; the callers pad. The generic cipher API also
// works where ESP-IDF swaps in its hardware AES driver.
static bool aes_cbc(const uint8_t key[32], const uint8_t iv[16], bool encrypt, const uint8_t *in, size_t len,
                    uint8_t *out) {
    const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if (info == nullptr)
        return false;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    size_t out_len = 0;
    const bool ok = mbedtls_cipher_setup(&ctx, info) == 0 &&
                    mbedtls_cipher_setkey(&ctx, key, 256, encrypt ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT) == 0 &&
                    mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_NONE) == 0 &&
                    mbedtls_cipher_crypt(&ctx, iv, BLOCK, in, len, out, &out_len) == 0 && out_len == len;
    mbedtls_cipher_free(&ctx);
    return ok;
}

static bool hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *a, size_t a_len, const uint8_t *b,
                        size_t b_len, uint8_t out[32]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr)
        return false;
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const bool ok = mbedtls_md_setup(&ctx, info, 1) == 0 && mbedtls_md_hmac_starts(&ctx, key, key_len) == 0 &&
                    mbedtls_md_hmac_update(&ctx, a, a_len) == 0 &&
                    (b_len == 0 || mbedtls_md_hmac_update(&ctx, b, b_len) == 0) &&
                    mbedtls_md_hmac_finish(&ctx, out) == 0;
    mbedtls_md_free(&ctx);
    return ok;
}

bool derive_session_key(const uint8_t app_random[16], const uint8_t device_random[16], uint8_t key[32]) {
    uint8_t prk[32];
    if (!hmac_sha256(app_random, HANDSHAKE_VALUE_LEN, SESSION_SECRET, sizeof(SESSION_SECRET), nullptr, 0, prk))
        return false;
    // A 32-byte key is a single expand block: T(1) = HMAC(PRK, info | 0x01).
    static const uint8_t COUNTER = 0x01;
    return hmac_sha256(prk, sizeof(prk), device_random, HANDSHAKE_VALUE_LEN, &COUNTER, 1, key);
}

// KEY_AUTH_SECRET of the BIKEGO plus app 1.6.1.
static const uint8_t B06_AUTH_KEY[16] = {0x94, 0x93, 0x4B, 0xB3, 0x01, 0xB0, 0xE4, 0x0F,
                                         0x2E, 0xC0, 0x5B, 0x04, 0x58, 0x8B, 0xAD, 0xD4};

bool b06_auth_answer(const uint8_t challenge[4], uint8_t answer[16]) {
    const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (info == nullptr)
        return false;
    uint8_t block[BLOCK] = {0};
    memcpy(block, challenge, 4);
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    size_t out_len = 0;
    const bool ok = mbedtls_cipher_setup(&ctx, info) == 0 &&
                    mbedtls_cipher_setkey(&ctx, B06_AUTH_KEY, 128, MBEDTLS_ENCRYPT) == 0 &&
                    mbedtls_cipher_update(&ctx, block, BLOCK, answer, &out_len) == 0 && out_len == BLOCK;
    mbedtls_cipher_free(&ctx);
    return ok;
}

const uint8_t *km_handshake_value(const uint8_t *data, size_t len, uint8_t type, uint8_t key) {
    if (len < 6 + HANDSHAKE_VALUE_LEN || data[0] != type || data[4] != key || data[5] != HANDSHAKE_VALUE_LEN)
        return nullptr;
    return data + 6;
}

const uint8_t *nus_handshake_value(const uint8_t *data, size_t len, uint8_t type, uint8_t key) {
    if (len < 4 + HANDSHAKE_VALUE_LEN || data[0] != type || data[2] != key || data[3] != HANDSHAKE_VALUE_LEN)
        return nullptr;
    return data + 4;
}

void Session::start(const uint8_t key[32], const uint8_t iv[16]) {
    memcpy(this->key_, key, sizeof(this->key_));
    memcpy(this->iv_, iv, sizeof(this->iv_));
    this->sequence_ = 0;
    this->active_ = true;
}

void Session::reset() {
    memset(this->key_, 0, sizeof(this->key_));
    memset(this->iv_, 0, sizeof(this->iv_));
    this->sequence_ = 0;
    this->active_ = false;
}

size_t Session::wrap(const uint8_t *frame, size_t len, uint8_t *out, size_t out_size) {
    const size_t cipher_len = (len / BLOCK + 1) * BLOCK;  // PKCS#7 always pads
    if (!this->active_ || cipher_len + PACKET_OVERHEAD > out_size || cipher_len > 0x7FFF)
        return 0;
    uint8_t *cipher = out + 5;
    memcpy(cipher, frame, len);
    memset(cipher + len, (int) (cipher_len - len), cipher_len - len);

    uint8_t padded[KM_MAX_PLAIN];
    if (cipher_len > sizeof(padded))
        return 0;
    memcpy(padded, cipher, cipher_len);
    if (!aes_cbc(this->key_, this->iv_, true, padded, cipher_len, cipher))
        return 0;

    this->sequence_ = this->sequence_ == 0xFFFF ? 0 : this->sequence_ + 1;
    out[0] = PACKET_START;
    out[1] = PACKET_ENCRYPTED | ((cipher_len >> 8) & 0x7F);
    out[2] = cipher_len & 0xFF;
    out[3] = this->sequence_ >> 8;
    out[4] = this->sequence_ & 0xFF;
    const uint16_t crc = crc16(out, 5 + cipher_len);
    out[5 + cipher_len] = crc >> 8;
    out[6 + cipher_len] = crc & 0xFF;
    out[7 + cipher_len] = PACKET_END;
    return cipher_len + PACKET_OVERHEAD;
}

size_t Session::unwrap(const uint8_t *packet, size_t len, uint8_t *out, size_t out_size) {
    if (len < PACKET_OVERHEAD || packet[0] != PACKET_START || packet[len - 1] != PACKET_END)
        return 0;
    const bool encrypted = (packet[1] & PACKET_ENCRYPTED) != 0;
    const size_t payload_len = ((size_t) (packet[1] & 0x7F) << 8) | packet[2];
    if (len < payload_len + PACKET_OVERHEAD || payload_len > out_size)
        return 0;
    const uint16_t expected = ((uint16_t) packet[5 + payload_len] << 8) | packet[6 + payload_len];
    if (crc16(packet, 5 + payload_len) != expected)
        return 0;
    if (!encrypted) {
        memcpy(out, packet + 5, payload_len);
        return payload_len;
    }
    if (!this->active_ || payload_len == 0 || payload_len % BLOCK != 0)
        return 0;

    if (!aes_cbc(this->key_, this->iv_, false, packet + 5, payload_len, out))
        return 0;
    const uint8_t pad = out[payload_len - 1];
    if (pad == 0 || pad > BLOCK)
        return 0;
    for (size_t i = payload_len - pad; i < payload_len; i++) {
        if (out[i] != pad)
            return 0;
    }
    return payload_len - pad;
}

}  // namespace esphome::cgo::protocol
