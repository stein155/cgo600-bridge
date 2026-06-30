#pragma once

#include <Arduino.h>

class Logger {
public:
    void begin();
    void logln(const char* message);
    void logf(const char* fmt, ...);
};

extern Logger logger;

#define LOGLN(msg) logger.logln(msg)
#define LOGF(...) logger.logf(__VA_ARGS__)
