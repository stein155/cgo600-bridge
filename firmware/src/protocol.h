#pragma once
#include <Arduino.h>

#define FRAME_START 0x52
#define FRAME_END   0x45

struct ParsedField {
    uint8_t id;
    const uint8_t* data;
    uint8_t len;
};

uint16_t crc16(const uint8_t* data, size_t len);
size_t buildRequest(uint8_t* out, const uint8_t* fieldIds, size_t count);

extern const uint8_t READ_INIT_CMD[];
extern const size_t  READ_INIT_CMD_LEN;
extern const uint8_t BOOTSTRAP_CMD_1[];
extern const size_t  BOOTSTRAP_CMD_1_LEN;
extern const uint8_t BOOTSTRAP_CMD_2[];
extern const size_t  BOOTSTRAP_CMD_2_LEN;
extern const uint8_t BOOTSTRAP_CMD_3[];
extern const size_t  BOOTSTRAP_CMD_3_LEN;
extern const uint8_t* READ_DATA_CMD;
extern const size_t   READ_DATA_CMD_LEN;
extern const uint8_t LIGHT_ON_CMD[];
extern const size_t  LIGHT_ON_CMD_LEN;
extern const uint8_t LIGHT_OFF_CMD[];
extern const size_t  LIGHT_OFF_CMD_LEN;
