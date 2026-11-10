// Host tests of the framework-neutral protocol code. Expected bytes come from
// fixed frames in the Tenways app, the tenways-ble capture of the Longtail Duo
// and the bridge's own CGO600 captures, unless noted otherwise.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "crypto.h"
#include "protocol.h"

using namespace esphome::tenways::protocol;
using Bytes = std::vector<uint8_t>;

static int failures = 0;

static std::string hex(const uint8_t *data, size_t len) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", data[i]);
        out += buf;
    }
    return out;
}

static void check(bool ok, const char *what) {
    if (!ok) {
        ++failures;
        printf("FAIL %s\n", what);
    }
}

static void check_bytes(const uint8_t *data, size_t len, const Bytes &expected, const char *what) {
    const bool ok = len == expected.size() && memcmp(data, expected.data(), len) == 0;
    if (!ok)
        printf("     got      %s\n     expected %s\n", hex(data, len).c_str(),
               hex(expected.data(), expected.size()).c_str());
    check(ok, what);
}

// Appends the CRC (high byte first, as replies carry it) and the end byte.
static Bytes reply(Bytes body, bool crc_low_first = false) {
    const uint16_t crc = crc16(body.data(), body.size());
    body.push_back(crc_low_first ? crc & 0xFF : crc >> 8);
    body.push_back(crc_low_first ? crc >> 8 : crc & 0xFF);
    body.push_back(0x45);
    return body;
}

static void test_king_meter_requests() {
    uint8_t out[64];
    const auto &cgo800 = km::CGO800_PROFILE;
    size_t len = km::build_read(out, cgo800.status_keys, cgo800.status_count, cgo800.crc_low_first);
    check_bytes(
        out, len,
        {0x62, 0x00, 0x0C, 0x0B, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x11, 0x23, 0xC2, 0x75, 0x45},
        "CGO800 status request");
    len = km::build_read(out, cgo800.info_keys, cgo800.info_count, cgo800.crc_low_first);
    check_bytes(out, len, {0x62, 0x00, 0x03, 0x02, 0x04, 0x05, 0x6B, 0x1E, 0x45}, "CGO800 info request");

    const auto &longtail = km::LONGTAIL_PROFILE;
    len = km::build_read(out, longtail.status_keys, longtail.status_count, longtail.crc_low_first);
    check_bytes(
        out, len,
        {0x62, 0x00, 0x0C, 0x0B, 0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F, 0x84, 0x91, 0x45},
        "Longtail Duo status request");
    len = km::build_read(out, longtail.info_keys, longtail.info_count, longtail.crc_low_first);
    check_bytes(out, len, {0x62, 0x00, 0x05, 0x04, 0x04, 0x05, 0x4F, 0x70, 0x8A, 0x12, 0x45},
                "Longtail Duo info request");

    // The app's fixed IV request for the CGO800 pins the low-byte-first CRC.
    static const uint8_t IV[] = {KEY_HANDSHAKE_IV};
    len = km::build_read(out, IV, sizeof(IV), true);
    check_bytes(out, len, {0x62, 0x00, 0x02, 0x01, 0xD2, 0xFD, 0xC5, 0x45}, "CGO800 IV request");

    len = km::build_write(out, km::KEY_LIGHT, 1, true);
    check_bytes(out, len, {0x63, 0x00, 0x04, 0x01, 0x11, 0x01, 0x01, 0x28, 0x6F, 0x45}, "CGO800 light on");

    uint8_t random[16];
    for (int i = 0; i < 16; i++)
        random[i] = i;
    len = km::build_write(out, KEY_HANDSHAKE_RANDOM, random, sizeof(random), true);
    check(len == 25 && out[0] == 0x63 && out[2] == 0x13 && out[3] == 0x01 && out[4] == 0xD8 && out[5] == 0x10 &&
              out[24] == 0x45,
          "CGO800 handshake random layout");
    check(km_handshake_value(out, len, km::TYPE_WRITE, KEY_HANDSHAKE_RANDOM) == out + 6, "King Meter handshake value");
}

static void test_king_meter_profiles() {
    check(&km::profile_for("CGO800 Plus") == &km::CGO800_PROFILE, "CGO800 Plus gets the CGO800 profile");
    check(&km::profile_for("CGO800S") == &km::CGO800_PROFILE, "CGO800S gets the CGO800 profile");
    check(&km::profile_for("CGO009L") == &km::CGO009_PROFILE, "CGO009L gets the CGO009 profile");
    check(&km::profile_for("CGO009_D") == &km::CGO009_PROFILE, "CGO009_D gets the CGO009 profile");
    check(&km::profile_for("Longtail Duo_D") == &km::LONGTAIL_PROFILE, "Longtail Duo_D gets its profile");
    check(&km::profile_for("AGO AIR-BELT_D") == &km::AGO_AIR_PROFILE, "AGO AIR-BELT_D gets the AGO AIR profile");
    check(&km::profile_for("Wayfarer_B") == &km::AGO_AIR_PROFILE, "Wayfarer_B gets the AGO AIR profile");
    check(&km::profile_for("CGO600_D") == &km::AGO_AIR_PROFILE, "CGO600_D gets the AGO AIR profile");
    check(&km::profile_for("AGOX") == &km::AGO_PROFILE, "AGOX gets the AGO profile");
    check(&km::profile_for("CARGO") == &km::AGO_PROFILE, "CARGO gets the AGO profile");
    check(&km::profile_for("") == &km::AGO_PROFILE, "an unnamed bike gets the AGO profile");
    check(km::CGO800_PROFILE.try_encryption && !km::AGO_PROFILE.try_encryption, "only the CGO800 starts encrypted");
    check(km::AGO_AIR_PROFILE.write_with_response && !km::AGO_PROFILE.write_with_response, "write types");
    check(&km::alternative_to(km::CGO800_PROFILE) == &km::AGO_PROFILE &&
              &km::alternative_to(km::LONGTAIL_PROFILE) == &km::CGO800_PROFILE,
          "the alternative layout has the other CRC order");
}

// Request keys of each driver, without their CRC, as the app's frames list them.
static void test_king_meter_profile_requests() {
    struct Case {
        const km::Profile &profile;
        bool status;
        Bytes expected;
    };
    const Case cases[] = {
        {km::CGO009_PROFILE, false, {0x62, 0x00, 0x06, 0x05, 0x04, 0x05, 0x70, 0x4F, 0x50}},
        {km::CGO009_PROFILE,
         true,
         {0x62, 0x00, 0x0E, 0x0D, 0x06, 0x07, 0x08, 0x09, 0x11, 0x12, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F, 0x50}},
        {km::AGO_AIR_PROFILE, false, {0x62, 0x00, 0x05, 0x04, 0x04, 0x05, 0x4F, 0x76}},
        {km::AGO_AIR_PROFILE,
         true,
         {0x62, 0x00, 0x0C, 0x0B, 0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F}},
        {km::AGO_PROFILE, false, {0x62, 0x00, 0x03, 0x02, 0x04, 0x05}},
        {km::AGO_PROFILE,
         true,
         {0x62, 0x00, 0x0C, 0x0B, 0x05, 0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23}},
    };
    for (const auto &c : cases) {
        uint8_t out[64];
        const size_t len = c.status ? km::build_read(out, c.profile.status_keys, c.profile.status_count, false)
                                    : km::build_read(out, c.profile.info_keys, c.profile.info_count, false);
        const std::string what = std::string(c.profile.name) + (c.status ? " status request" : " info request");
        check_bytes(out, len - 3, c.expected, what.c_str());
    }
}

// The CGO800 driver decodes the odometer, the range and the firmware
// differently from the other King Meter drivers.
static void test_king_meter_profile_values() {
    // Odometer 1201, firmware 01 02 03 or "V12", range 0x0290 (656 km).
    const Bytes frame = reply({0x62, 0x00, 0x10, 0x03, 0x0C, 0x04, 0x00, 0x00, 0x04, 0xB1, 0x04, 0x03, 0x01, 0x02, 0x03,
                               0x0B, 0x02, 0x02, 0x90});
    const Bytes ascii = reply({0x62, 0x00, 0x06, 0x01, 0x04, 0x03, 'V', '1', '2'});
    uint32_t bad = 0, unhandled = 0;

    km::FrameParser ago;
    BikeState s1;
    ago.feed(s1, frame.data(), frame.size(), bad, unhandled);
    check(s1.odometer_km > 120.09f && s1.odometer_km < 120.11f, "AGO odometer / 10");
    check(strcmp(s1.firmware, "1.2.3") == 0, "AGO firmware as dotted bytes");
    check(s1.remaining_range_km == 656.0f, "AGO range 656 km");

    km::FrameParser cgo800;
    cgo800.set_profile(&km::CGO800_PROFILE);
    BikeState s2;
    cgo800.feed(s2, frame.data(), frame.size(), bad, unhandled);
    check(s2.odometer_km == 1201.0f, "CGO800 odometer in whole km");
    check(s2.remaining_range_km == -1, "CGO800 range with a byte >= 0x80 means none");
    cgo800.feed(s2, ascii.data(), ascii.size(), bad, unhandled);
    check(strcmp(s2.firmware, "V12") == 0, "CGO800 firmware as ASCII");
}

static void test_king_meter_parser() {
    for (bool low_first : {false, true}) {
        // Battery 57 %, light on, power 300 W, remaining range 42 km, serial.
        const Bytes frame = reply({0x62, 0x00, 0x14, 0x05, 0x0F, 0x01, 57,   0x11, 0x01, 0x01, 0x23, 0x02,
                                   0x01, 0x2C, 0x0B, 0x02, 0x00, 42,   0x05, 0x03, 'S',  'N',  '1'},
                                  low_first);
        km::FrameParser parser;
        BikeState state;
        uint32_t bad = 0, unhandled = 0;
        const bool changed = parser.feed(state, frame.data(), frame.size(), bad, unhandled);
        check(changed && bad == 0 && parser.frames() == 1,
              low_first ? "KM reply, CRC low first" : "KM reply, CRC high first");
        check(state.battery == 57 && state.light_valid && state.light_on, "KM battery and light");
        check(state.power_w == 300, "KM power");
        check(state.remaining_range_km == 42.0f, "KM remaining range in whole km");
        check(strcmp(state.serial_number, "SN1") == 0, "KM serial number from key 0x05");
    }

    // 0x028F (655) is reported with assist level 0 and means no range.
    BikeState state;
    state.remaining_range_km = 10;
    const Bytes none = reply({0x62, 0x00, 0x05, 0x01, 0x0B, 0x02, 0x02, 0x8F});
    km::FrameParser parser;
    uint32_t bad = 0, unhandled = 0;
    parser.feed(state, none.data(), none.size(), bad, unhandled);
    check(state.remaining_range_km == 10, "KM remaining range 0x028F keeps the last value");

    // A CGO800 can send AT+NAME after a frame; the frame still counts.
    Bytes with_error = reply({0x62, 0x00, 0x04, 0x01, 0x0F, 0x01, 80});
    const char *at = "AT+NAME=CGO800S\r\n";
    with_error.insert(with_error.end(), at, at + strlen(at));
    BikeState state2;
    km::FrameParser parser2;
    parser2.feed(state2, with_error.data(), with_error.size(), bad, unhandled);
    check(state2.battery == 80 && parser2.frames() == 1, "KM frame followed by AT+NAME");

    const Bytes wrong = {0x62, 0x00, 0x04, 0x01, 0x0F, 0x01, 80, 0x00, 0x00, 0x45};
    BikeState state3;
    km::FrameParser parser3;
    bad = 0;
    parser3.feed(state3, wrong.data(), wrong.size(), bad, unhandled);
    check(bad == 1 && state3.battery == -1, "KM frame with a wrong CRC is rejected");
}

static void test_nus_requests() {
    uint8_t out[64];
    static const uint8_t SERIAL[] = {FIELD_SN};
    size_t len = build_request(out, SERIAL, sizeof(SERIAL));
    check(len == 6 && out[0] == FRAME_START && out[1] == 1 && out[2] == FIELD_SN && out[5] == FRAME_END,
          "CGO600 serial request");

    static const uint8_t IV[] = {KEY_HANDSHAKE_IV};
    len = build_request(out, IV, sizeof(IV));
    check_bytes(out, len, {0x52, 0x01, 0xD2, 0x51, 0xDC, 0x45}, "CGO600 IV request");

    static const uint8_t STATUS[] = {0x07, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x21, 0x1A, 0x2D};
    len = build_request(out, STATUS, sizeof(STATUS));
    check(len == 16 && out[1] == 11 && out[15] == FRAME_END, "CGO600 variant status request");

    static const uint8_t ON[] = {1};
    len = build_write(out, FIELD_LIGHT, ON, sizeof(ON));
    check_bytes(out, len, Bytes(LIGHT_ON_CMD.data, LIGHT_ON_CMD.data + LIGHT_ON_CMD.len),
                "CGO600 light write matches the captured command");

    uint8_t random[16] = {};
    len = build_write(out, KEY_HANDSHAKE_RANDOM, random, sizeof(random));
    check(len == 23 && out[0] == FRAME_WRITE && out[1] == 0x12 && out[2] == 0xD8 && out[3] == 0x10,
          "CGO600 handshake random layout");
    check(nus_handshake_value(out, len, FRAME_WRITE, KEY_HANDSHAKE_RANDOM) == out + 4, "CGO600 handshake value");
}

static void test_nus_parser() {
    // Firmware "V1.23", power 250 W, battery 57 %.
    const Bytes frame =
        reply({0x52, 14, FIELD_FIRMWARE, 5, 'V', '1', '.', '2', '3', FIELD_POWER, 2, 0x00, 0xFA, FIELD_BATTERY, 1, 57});
    FrameParser parser;
    BikeState state;
    uint32_t bad = 0;
    const bool changed = parser.feed(state, frame.data(), frame.size(), bad);
    check(changed && bad == 0, "CGO600 reply parsed");
    check(strcmp(state.firmware, "V1.23") == 0, "CGO600 firmware");
    check(state.power_w == 250 && state.battery == 57, "CGO600 power and battery");

    const Bytes implausible = reply({0x52, 4, FIELD_POWER, 2, 0x13, 0x88});  // 5000 W
    BikeState state2;
    FrameParser parser2;
    parser2.feed(state2, implausible.data(), implausible.size(), bad);
    check(state2.power_w == -1, "CGO600 power above 3000 W is ignored");
}

static void test_crypto() {
    uint8_t app[16], device[16], iv[16];
    for (int i = 0; i < 16; i++) {
        app[i] = i;
        device[i] = 0xF0 + i;
        iv[i] = 0x10 * i;
    }
    // Same as the Tenways app's own key derivation and a Python HKDF.
    uint8_t key[32];
    check(derive_session_key(app, device, key), "session key derived");
    check_bytes(key, sizeof(key),
                {0xCD, 0xCE, 0x9A, 0x3A, 0xB4, 0x4D, 0xD0, 0xD7, 0xFA, 0x0F, 0xD8, 0xE4, 0x3D, 0xBF, 0xBC, 0xC8,
                 0xA6, 0x1C, 0xA9, 0xAA, 0x0B, 0x77, 0xBA, 0x0D, 0x19, 0x08, 0xD7, 0xDB, 0x6F, 0x73, 0xC1, 0x69},
                "session key");

    const auto &cgo800 = km::CGO800_PROFILE;
    uint8_t frame[32];
    const size_t frame_len = km::build_read(frame, cgo800.status_keys, cgo800.status_count, true);
    Session session;
    session.start(key, iv);
    uint8_t packet[64];
    const size_t packet_len = session.wrap(frame, frame_len, packet, sizeof(packet));
    // Same as openssl enc -aes-256-cbc with this key and IV.
    check_bytes(packet, packet_len, {0xA5, 0x80, 0x20, 0x00, 0x01, 0x6F, 0x9F, 0xD6, 0xAC, 0xFD, 0x1C, 0x3D, 0x6F, 0xC8,
                                     0xE9, 0x17, 0x2E, 0x65, 0xB2, 0xB9, 0xAF, 0xC8, 0xCD, 0x00, 0xD5, 0xF2, 0x78, 0x0B,
                                     0x96, 0x15, 0xDF, 0x0F, 0x51, 0xCB, 0x39, 0xCF, 0xC9, 0x49, 0xEB, 0x5A},
                "encrypted packet");

    uint8_t second[64];
    session.wrap(frame, frame_len, second, sizeof(second));
    check(second[3] == 0x00 && second[4] == 0x02, "packet sequence numbers count up");

    uint8_t plain[64];
    const size_t plain_len = session.unwrap(packet, packet_len, plain, sizeof(plain));
    check_bytes(plain, plain_len, Bytes(frame, frame + frame_len), "packet decrypts to the frame");

    packet[10] ^= 1;
    check(session.unwrap(packet, packet_len, plain, sizeof(plain)) == 0, "tampered packet is rejected");

    Session idle;
    check(idle.wrap(frame, frame_len, packet, sizeof(packet)) == 0, "no packets without a session");
}

// A stray start byte, chunk boundaries and CRC failures must not lose the
// real frames around them.
static void test_nus_parser_resync() {
    FrameParser parser;
    BikeState state;
    uint32_t bad = 0;
    const Bytes battery = reply({0x52, 3, FIELD_BATTERY, 1, 64});
    // A stray 0x52 whose length byte claims 0x30 more bytes, then a real frame.
    Bytes stream = {0x00, 0x52, 0x30, 0x11};
    stream.insert(stream.end(), battery.begin(), battery.end());
    stream.resize(stream.size() + 0x40, 0x00);
    check(parser.feed(state, stream.data(), stream.size(), bad) && state.battery == 64,
          "CGO600 frame behind a stray start byte");

    // One frame split over three notifications.
    const Bytes odometer = reply({0x52, 6, FIELD_ODOMETER, 4, 0x00, 0x00, 0x30, 0x39});
    FrameParser split;
    BikeState split_state;
    check(!split.feed(split_state, odometer.data(), 2, bad), "CGO600 waits for the rest of a frame");
    check(!split.feed(split_state, odometer.data() + 2, 3, bad), "CGO600 still waits");
    check(split.feed(split_state, odometer.data() + 5, odometer.size() - 5, bad) && split_state.odometer_km > 0,
          "CGO600 frame split over notifications");

    // A CRC failure is counted and the next frame still parses.
    Bytes corrupt = reply({0x52, 3, FIELD_BATTERY, 1, 10});
    corrupt[4] ^= 0xFF;
    const uint32_t bad_before = bad;
    Bytes both = corrupt;
    both.insert(both.end(), battery.begin(), battery.end());
    FrameParser crc_parser;
    BikeState crc_state;
    check(crc_parser.feed(crc_state, both.data(), both.size(), bad) && bad == bad_before + 1 && crc_state.battery == 64,
          "CGO600 CRC failure counted, next frame parsed");
}

static void test_nus_light_commands() {
    // The light commands write field 0x21 with 1 or 0.
    check(LIGHT_ON_CMD.data[0] == FRAME_WRITE && LIGHT_OFF_CMD.data[0] == FRAME_WRITE, "light commands are writes");
    check(LIGHT_ON_CMD.len == LIGHT_OFF_CMD.len, "light commands have the same layout");
    uint8_t on[16], off[16];
    static const uint8_t ONE[] = {1}, ZERO[] = {0};
    const size_t on_len = build_write(on, FIELD_LIGHT, ONE, 1);
    const size_t off_len = build_write(off, FIELD_LIGHT, ZERO, 1);
    check_bytes(LIGHT_ON_CMD.data, LIGHT_ON_CMD.len, Bytes(on, on + on_len), "light on matches build_write");
    check_bytes(LIGHT_OFF_CMD.data, LIGHT_OFF_CMD.len, Bytes(off, off + off_len), "light off matches build_write");
}

// unwrap() reads the end byte at the packet's own length, so bytes after the
// packet are ignored and a truncated packet is rejected.
static void test_crypto_packet_bounds() {
    uint8_t key[32] = {0}, iv[16] = {0};
    Session session;
    session.start(key, iv);
    const auto &cgo800 = km::CGO800_PROFILE;
    uint8_t frame[32];
    const size_t frame_len = km::build_read(frame, cgo800.status_keys, cgo800.status_count, true);
    uint8_t packet[80];
    const size_t packet_len = session.wrap(frame, frame_len, packet, sizeof(packet));
    uint8_t plain[64];
    packet[packet_len] = 0xEE;  // trailing byte
    check(session.unwrap(packet, packet_len + 1, plain, sizeof(plain)) == frame_len, "trailing byte ignored");
    check(session.unwrap(packet, packet_len - 1, plain, sizeof(plain)) == 0, "truncated packet rejected");
    uint8_t small[8];
    check(session.wrap(frame, frame_len, small, sizeof(small)) == 0, "wrap refuses a buffer that is too small");
}

// The data watchdog only counts frames that pass the CRC check.
static void test_nus_parser_frame_count() {
    FrameParser parser;
    BikeState state;
    uint32_t bad = 0;
    const Bytes battery = reply({0x52, 3, FIELD_BATTERY, 1, 64});
    static const uint8_t ERROR_REPLY[] = "AT+NAME=ERR";
    FrameParser errors;
    errors.feed(state, ERROR_REPLY, sizeof(ERROR_REPLY) - 1, bad);
    check(errors.frames() == 0, "an AT reply is no CGO600 frame");
    Bytes corrupt = battery;
    corrupt[4] ^= 0xFF;
    parser.feed(state, corrupt.data(), corrupt.size(), bad);
    check(parser.frames() == 0 && bad == 1, "a CGO600 frame with a wrong CRC is not counted");
    parser.feed(state, battery.data(), battery.size(), bad);
    check(parser.frames() == 1, "a valid CGO600 frame is counted");
    // A valid frame counts even when it carries no field we decode.
    const Bytes unknown = reply({0x52, 3, 0xEE, 1, 0x00});
    check(!parser.feed(state, unknown.data(), unknown.size(), bad) && parser.frames() == 2,
          "a valid CGO600 frame without known fields is counted");
    parser.reset();
    check(parser.frames() == 0, "reset clears the CGO600 frame count");
}

// Collects every packet the assembler returns for one notification.
static std::vector<Bytes> assemble(PacketAssembler &assembler, const uint8_t *data, size_t len) {
    std::vector<Bytes> packets;
    while (len > 0) {
        const size_t taken = assembler.feed(data, len);
        data += taken;
        len -= taken;
        size_t packet_len;
        const uint8_t *packet;
        while ((packet = assembler.next(packet_len)) != nullptr)
            packets.emplace_back(packet, packet + packet_len);
        if (taken == 0)
            break;
    }
    return packets;
}

// Encrypted packets are cut out of the notification stream by the length in
// their header, whatever the notification boundaries are.
static void test_crypto_packet_assembly() {
    uint8_t key[32] = {0}, iv[16] = {0};
    Session session;
    session.start(key, iv);
    // A reply of 40 bytes makes a 56-byte packet: three notifications at MTU 23.
    Bytes frame = {0x62, 0x00, 0x25, 0x01, 0x05, 0x22};
    frame.resize(40, 0x31);
    uint8_t wrapped[80];
    const Bytes first(wrapped, wrapped + session.wrap(frame.data(), frame.size(), wrapped, sizeof(wrapped)));
    const Bytes second(wrapped, wrapped + session.wrap(frame.data(), 5, wrapped, sizeof(wrapped)));
    check(first.size() == 56 && second.size() == 24, "test packets have the expected sizes");
    uint8_t plain[64];

    PacketAssembler whole;
    auto packets = assemble(whole, first.data(), first.size());
    check(packets.size() == 1 && packets[0] == first, "a whole packet passes the assembler");
    check(session.unwrap(packets[0].data(), packets[0].size(), plain, sizeof(plain)) == frame.size() &&
              memcmp(plain, frame.data(), frame.size()) == 0,
          "an assembled packet decrypts to the frame");

    PacketAssembler split;
    check(assemble(split, first.data(), 20).empty(), "the assembler waits for the rest of a packet");
    check(assemble(split, first.data() + 20, 20).empty(), "the assembler still waits");
    packets = assemble(split, first.data() + 40, first.size() - 40);
    check(packets.size() == 1 && packets[0] == first, "a packet in three chunks is put together");
    check(session.unwrap(packets[0].data(), packets[0].size(), plain, sizeof(plain)) == frame.size(),
          "a packet in three chunks decrypts");

    Bytes both = first;
    both.insert(both.end(), second.begin(), second.end());
    PacketAssembler pair;
    packets = assemble(pair, both.data(), both.size());
    check(packets.size() == 2 && packets[0] == first && packets[1] == second, "two packets in one notification");

    // Garbage with a stray start byte whose length claims more than a packet
    // holds, and one that claims a plausible length.
    Bytes dirty = {0x00, 0xA5, 0xFF, 0xFF, 0x13, 0xA5, 0x80, 0x10, 0x37};
    const size_t garbage = dirty.size();
    dirty.insert(dirty.end(), first.begin(), first.end());
    PacketAssembler resync;
    packets = assemble(resync, dirty.data(), dirty.size());
    check(packets.size() == 1 && packets[0] == first, "a packet behind garbage is found");
    check(resync.skipped_bytes() == garbage, "the garbage is counted as skipped");

    // A packet with a broken CRC is dropped and the next one still arrives.
    Bytes corrupt = first;
    corrupt[10] ^= 1;
    corrupt.insert(corrupt.end(), second.begin(), second.end());
    PacketAssembler crc;
    packets = assemble(crc, corrupt.data(), corrupt.size());
    check(packets.size() == 1 && packets[0] == second, "a packet with a wrong CRC is skipped");

    // More bytes than the buffer holds in one notification.
    Bytes many;
    for (int i = 0; i < 12; i++)
        many.insert(many.end(), first.begin(), first.end());
    PacketAssembler full;
    check(many.size() > PacketAssembler::BUFFER_SIZE && assemble(full, many.data(), many.size()).size() == 12,
          "a notification larger than the buffer loses no packet");

    split.reset();
    check(assemble(split, first.data() + 40, first.size() - 40).empty() && split.skipped_bytes() > 0,
          "reset forgets a partial packet");
}

int main() {
    test_king_meter_requests();
    test_king_meter_profiles();
    test_king_meter_profile_requests();
    test_king_meter_profile_values();
    test_king_meter_parser();
    test_nus_requests();
    test_nus_parser();
    test_crypto();
    test_nus_parser_resync();
    test_nus_light_commands();
    test_crypto_packet_bounds();
    test_nus_parser_frame_count();
    test_crypto_packet_assembly();
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All protocol tests passed\n");
    return 0;
}
