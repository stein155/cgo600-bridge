#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

Logger logger;

void Logger::begin() {
    logln("[LOG] Logger initialized");
}

void Logger::logln(const char* message) {
    char formatted[192];
    snprintf(formatted, sizeof(formatted), "%10lu %s", millis(), message ? message : "");
    Serial.println(formatted);
}

void Logger::logf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    logln(buffer);
}
