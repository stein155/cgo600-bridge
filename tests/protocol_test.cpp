// Host tests of the framework-neutral protocol code. Expected bytes come from
// fixed frames in the Tenways app, the tenways-ble capture of the Longtail Duo
// and the bridge's own CGO600 captures, unless noted otherwise.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cgo_crypto.h"
#include "cgo_protocol.h"

using namespace esphome::cgo::protocol;
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
    check_bytes(out, len, {0x62, 0x00, 0x0C, 0x0B, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x11, 0x23,
                           0xC2, 0x75, 0x45},
                "CGO800 status request");
    len = km::build_read(out, cgo800.info_keys, cgo800.info_count, cgo800.crc_low_first);
    check_bytes(out, len, {0x62, 0x00, 0x03, 0x02, 0x04, 0x05, 0x6B, 0x1E, 0x45}, "CGO800 info request");

    const auto &longtail = km::LONGTAIL_PROFILE;
    len = km::build_read(out, longtail.status_keys, longtail.status_count, longtail.crc_low_first);
    check_bytes(out, len, {0x62, 0x00, 0x0C, 0x0B, 0x06, 0x07, 0x08, 0x09, 0x11, 0x0A, 0x0B, 0x0C, 0x0F, 0x23, 0x4F,
                           0x84, 0x91, 0x45},
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
    check(km_handshake_value(out, len, km::TYPE_WRITE, KEY_HANDSHAKE_RANDOM) == out + 6,
          "King Meter handshake value");
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
    const Bytes frame = reply({0x62, 0x00, 0x10, 0x03, 0x0C, 0x04, 0x00, 0x00, 0x04, 0xB1, 0x04, 0x03, 0x01, 0x02,
                               0x03, 0x0B, 0x02, 0x02, 0x90});
    const Bytes ascii = reply({0x62, 0x00, 0x06, 0x01, 0x04, 0x03, 'V', '1', '2'});
    bool trip = false;
    uint32_t bad = 0, unhandled = 0;

    km::FrameParser ago;
    BikeState s1;
    ago.feed(s1, frame.data(), frame.size(), trip, bad, unhandled);
    check(s1.total_range_km > 120.09f && s1.total_range_km < 120.11f, "AGO odometer / 10");
    check(strcmp(s1.firmware, "1.2.3") == 0, "AGO firmware as dotted bytes");
    check(s1.remaining_range_km == 656.0f, "AGO range 656 km");

    km::FrameParser cgo800;
    cgo800.set_profile(&km::CGO800_PROFILE);
    BikeState s2;
    cgo800.feed(s2, frame.data(), frame.size(), trip, bad, unhandled);
    check(s2.total_range_km == 1201.0f, "CGO800 odometer in whole km");
    check(s2.remaining_range_km == -1, "CGO800 range with a byte >= 0x80 means none");
    cgo800.feed(s2, ascii.data(), ascii.size(), trip, bad, unhandled);
    check(strcmp(s2.firmware, "V12") == 0, "CGO800 firmware as ASCII");
}

static void test_king_meter_parser() {
    for (bool low_first : {false, true}) {
        // Battery 57 %, light on, power 300 W, remaining range 42 km, serial.
        const Bytes frame = reply({0x62, 0x00, 0x14, 0x05, 0x0F, 0x01, 57, 0x11, 0x01, 0x01, 0x23, 0x02, 0x01, 0x2C,
                                   0x0B, 0x02, 0x00, 42, 0x05, 0x03, 'S', 'N', '1'},
                                  low_first);
        km::FrameParser parser;
        BikeState state;
        bool trip = false;
        uint32_t bad = 0, unhandled = 0;
        const bool changed = parser.feed(state, frame.data(), frame.size(), trip, bad, unhandled);
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
    bool trip = false;
    uint32_t bad = 0, unhandled = 0;
    parser.feed(state, none.data(), none.size(), trip, bad, unhandled);
    check(state.remaining_range_km == 10, "KM remaining range 0x028F keeps the last value");

    // A CGO800 can send AT+NAME after a frame; the frame still counts.
    Bytes with_error = reply({0x62, 0x00, 0x04, 0x01, 0x0F, 0x01, 80});
    const char *at = "AT+NAME=CGO800S\r\n";
    with_error.insert(with_error.end(), at, at + strlen(at));
    BikeState state2;
    km::FrameParser parser2;
    parser2.feed(state2, with_error.data(), with_error.size(), trip, bad, unhandled);
    check(state2.battery == 80 && parser2.frames() == 1, "KM frame followed by AT+NAME");

    const Bytes wrong = {0x62, 0x00, 0x04, 0x01, 0x0F, 0x01, 80, 0x00, 0x00, 0x45};
    BikeState state3;
    km::FrameParser parser3;
    bad = 0;
    parser3.feed(state3, wrong.data(), wrong.size(), trip, bad, unhandled);
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
    const Bytes frame = reply(
        {0x52, 14, FIELD_FIRMWARE, 5, 'V', '1', '.', '2', '3', FIELD_POWER, 2, 0x00, 0xFA, FIELD_BATTERY, 1, 57});
    FrameParser parser;
    BikeState state;
    bool trip = false;
    uint32_t bad = 0;
    const bool changed = parser.feed(state, frame.data(), frame.size(), trip, bad);
    check(changed && bad == 0, "CGO600 reply parsed");
    check(strcmp(state.firmware, "V1.23") == 0, "CGO600 firmware");
    check(state.power_w == 250 && state.battery == 57, "CGO600 power and battery");

    const Bytes implausible = reply({0x52, 4, FIELD_POWER, 2, 0x13, 0x88});  // 5000 W
    BikeState state2;
    FrameParser parser2;
    parser2.feed(state2, implausible.data(), implausible.size(), trip, bad);
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
    check_bytes(packet, packet_len,
                {0xA5, 0x80, 0x20, 0x00, 0x01, 0x6F, 0x9F, 0xD6, 0xAC, 0xFD, 0x1C, 0x3D, 0x6F, 0xC8, 0xE9,
                 0x17, 0x2E, 0x65, 0xB2, 0xB9, 0xAF, 0xC8, 0xCD, 0x00, 0xD5, 0xF2, 0x78, 0x0B, 0x96, 0x15,
                 0xDF, 0x0F, 0x51, 0xCB, 0x39, 0xCF, 0xC9, 0x49, 0xEB, 0x5A},
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

// Expected bytes come from the reference implementation in Python (b06-reader),
// written from the BIKEGO plus app 1.6.1.
static void test_b06() {
    uint8_t frame[64];
    static const uint8_t START[] = {0x01};
    check_bytes(frame, b06::build_frame(frame, b06::CMD_AUTH_START, START, 1),
                {0xA5, 0x01, 0x20, 0x01, 0xF7, 0x21, 0x0D}, "B06 authentication request");
    check_bytes(frame, b06::build_frame(frame, b06::CMD_VERSION, nullptr, 0), {0xA5, 0x00, 0x40, 0xC4, 0x48, 0x0D},
                "B06 version request");

    static const uint8_t CHALLENGE[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t answer[16];
    check(b06_auth_answer(CHALLENGE, answer), "B06 authentication answer computed");
    check_bytes(answer, sizeof(answer),
                {0xDE, 0x5C, 0x8F, 0x89, 0xEC, 0x8D, 0xDE, 0xEC, 0x82, 0x1F, 0xAB, 0xE8, 0xCE, 0xF2, 0xB3, 0x19},
                "B06 authentication answer");

    // Same as the app's MeterConfig.lightOn: 0x43 [write, CONTENT_CONFIG_LIGHT, on].
    const size_t light_len = b06::build_light(frame, true);
    check(light_len == 10 && frame[2] == 0x43 && frame[3] == 0x02 && frame[4] == 0x0E && frame[5] == 0x01 &&
              frame[6] == 0x01,
          "B06 light on");
    const uint16_t light_crc = b06::crc16(frame + 1, 6);
    check(frame[7] == (light_crc & 0xFF) && frame[8] == (light_crc >> 8) && frame[9] == 0x0D, "B06 light frame");

    b06::FrameParser parser;
    BikeState state;
    bool trip = false;
    uint32_t bad = 0, unhandled = 0;

    // Challenge and accepted answer: handled, but no telemetry.
    Bytes auth(frame, frame + b06::build_frame(frame, b06::CMD_AUTH_START, CHALLENGE, 4));
    check(!parser.feed(state, auth.data(), auth.size(), trip, bad, unhandled), "B06 challenge is not telemetry");
    uint8_t challenge[4];
    check(parser.take_challenge(challenge) && memcmp(challenge, CHALLENGE, 4) == 0, "B06 challenge kept");
    check(!parser.take_challenge(challenge), "B06 challenge taken once");
    static const uint8_t ACCEPTED[] = {0x00};
    Bytes ok(frame, frame + b06::build_frame(frame, b06::CMD_AUTH_ANSWER, ACCEPTED, 1));
    parser.feed(state, ok.data(), ok.size(), trip, bad, unhandled);
    check(parser.auth_result() == b06::FrameParser::AuthResult::OK, "B06 authentication accepted");

    // Battery 80 %, split over two notifications.
    const Bytes battery = {0xA5, 0x06, 0x42, 0x50, 0x01, 0x9A, 0x00, 0x10, 0x00, 0x46, 0x4A, 0x0D};
    check(!parser.feed(state, battery.data(), 4, trip, bad, unhandled), "B06 partial frame waits");
    check(parser.feed(state, battery.data() + 4, battery.size() - 4, trip, bad, unhandled) && state.battery == 80,
          "B06 battery");

    // 30.0 km/h, max 45.5, average 18.25, range 50 km, 600 s, trip 12.34 km,
    // odometer 1000.00 km, light on.
    const uint8_t ride[29] = {0x2C, 0x01, 0xC7, 0x01, 0x21, 0x07, 0x00, 0x32, 0x58, 0x02, 0x00, 0x00, 0xD2, 0x04, 0x00,
                              0x00, 0xA0, 0x86, 0x01, 0x00, 3, 1, 0, 0, 0, 0, 0, 0, 0};
    Bytes ride_frame(frame, frame + b06::build_frame(frame, b06::CMD_RIDE, ride, sizeof(ride)));
    trip = false;
    check(parser.feed(state, ride_frame.data(), ride_frame.size(), trip, bad, unhandled) && trip, "B06 ride parsed");
    check(state.speed_kmh == 30.0f && state.max_speed_kmh == 45.5f && state.avg_speed_kmh == 18.25f,
          "B06 speeds");
    check(state.remaining_range_km == -1 && state.trip_duration == 600, "B06 ride time, no range");
    check(state.trip_distance_km == 12.34f && state.total_range_km == 1000.0f, "B06 trip and odometer");
    check(state.light_valid && state.light_on, "B06 light");

    static const uint8_t VERSION[] = {0x01, 'B', '0', '6', '_', 'V', '1', 0x00, 'H', 'W', '1'};
    Bytes version(frame, frame + b06::build_frame(frame, b06::CMD_VERSION, VERSION, sizeof(VERSION)));
    parser.feed(state, version.data(), version.size(), trip, bad, unhandled);
    check(strcmp(state.firmware, "B06_V1") == 0, "B06 software version");

    ride_frame[5] ^= 1;
    parser.feed(state, ride_frame.data(), ride_frame.size(), trip, bad, unhandled);
    check(bad == 1 && unhandled == 0, "B06 bad CRC counted");
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
    test_b06();
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("All protocol tests passed\n");
    return 0;
}
