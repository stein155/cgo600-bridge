#include "bike_state.h"
#include "logger.h"
#include <time.h>

// Upper sanity bound for a decoded remaining-range value; anything above this
// is treated as a corrupt/implausible reading and ignored.
static constexpr float MAX_PLAUSIBLE_RANGE_KM = 2000.0f;

static uint32_t intBE(const uint8_t* data, uint8_t len) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < len; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

static void safeAscii(const uint8_t* data, uint8_t len, char* out, size_t outSize) {
    size_t j = 0;
    for (uint8_t i = 0; i < len && j < outSize - 1; i++) {
        // Printable ASCII only; quote and backslash are dropped so the value
        // can be embedded in hand-built JSON without escaping.
        if (data[i] >= 32 && data[i] <= 126 && data[i] != '"' && data[i] != '\\') {
            out[j++] = data[i];
        }
    }
    out[j] = '\0';
}

static void safeHex(const uint8_t* data, uint8_t len, char* out, size_t outSize) {
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    size_t j = 0;
    for (uint8_t i = 0; i < len && j + 2 < outSize; i++) {
        out[j++] = HEX_DIGITS[(data[i] >> 4) & 0x0F];
        out[j++] = HEX_DIGITS[data[i] & 0x0F];
    }
    out[j] = '\0';
}

static bool getUtcIso8601(char* out, size_t outSize) {
    const time_t now = time(nullptr);
    if (now < 946684800) {
        return false;
    }

    struct tm utcTime;
    if (gmtime_r(&now, &utcTime) == nullptr) {
        return false;
    }

    return strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &utcTime) > 0;
}

bool BikeState::updateField(uint8_t fieldId, const uint8_t* data, uint8_t len) {
    switch (fieldId) {
        case FIELD_SN:
            safeAscii(data, len, serial_number, sizeof(serial_number));
            return true;
        case FIELD_BATTERY:
            if (len > 0) { battery = data[0]; return true; }
            break;
        case FIELD_SPEED:
            speed_kmh = intBE(data, len) / 10.0f;
            return true;
        case FIELD_MAX_SPEED:
            max_speed_kmh = intBE(data, len) / 10.0f;
            return true;
        case FIELD_AVG_SPEED:
            avg_speed_kmh = intBE(data, len) / 10.0f;
            return true;
        case FIELD_TRIP_DISTANCE:
            trip_distance_km = intBE(data, len) / 10.0f;
            return true;
        case FIELD_TRIP_DURATION:
            trip_duration = (int)intBE(data, len);
            return true;
        case FIELD_REMAINING_RANGE:
            if (len < 2) {
                char raw[16];
                safeHex(data, len, raw, sizeof(raw));
                LOGF("[BLE] Ignoring remaining range with unexpected len=%u raw=%s\n", len, raw);
                return false;
            }
            if ((len == 2 && data[0] == 0xFF && data[1] == 0xFF) ||
                (len == 4 && data[0] == 0xFF && data[1] == 0xFF && data[2] == 0x00 && data[3] == 0x00)) {
                return false;
            } else {
                const uint8_t decodeLen = (len >= 4 && data[2] == 0x00 && data[3] == 0x00) ? 2 : len;
                const float remainingRangeKm = intBE(data, decodeLen) / 10.0f;
                if (total_range_km > 0.0f && remainingRangeKm > total_range_km) {
                    LOGF("[BLE] Ignoring remaining range %.1f km > total range %.1f km\n",
                        remainingRangeKm, total_range_km);
                    return false;
                }
                if (remainingRangeKm > MAX_PLAUSIBLE_RANGE_KM) {
                    LOGF("[BLE] Ignoring implausible remaining range %.1f km\n", remainingRangeKm);
                    return false;
                }
                remaining_range_km = remainingRangeKm;
            }
            return true;
        case FIELD_TOTAL_RANGE:
            total_range_km = intBE(data, len) / 10.0f;
            return true;
        case FIELD_LIGHT:
            if (len > 0) {
                light_on = data[0] != 0;
                light_valid = true;
                return true;
            }
            break;
    }
    return false;
}

const char* BikeState::getDisplaySerial() const {
    if (serial_number[0]) return serial_number;
    return nullptr;
}

bool BikeState::setLastConnectedNow() {
    char timestamp[sizeof(last_connected_at)] = {0};
    if (!getUtcIso8601(timestamp, sizeof(timestamp))) {
        return false;
    }

    if (strncmp(last_connected_at, timestamp, sizeof(last_connected_at)) == 0) {
        return false;
    }

    strncpy(last_connected_at, timestamp, sizeof(last_connected_at) - 1);
    last_connected_at[sizeof(last_connected_at) - 1] = '\0';
    return true;
}

bool BikeState::setLastDataNow() {
    char timestamp[sizeof(last_data_at)] = {0};
    if (!getUtcIso8601(timestamp, sizeof(timestamp))) {
        return false;
    }

    strncpy(last_data_at, timestamp, sizeof(last_data_at) - 1);
    last_data_at[sizeof(last_data_at) - 1] = '\0';
    return true;
}
