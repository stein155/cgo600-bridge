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
    // logln() adds the line ending; drop trailing newlines from the format.
    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[--len] = '\0';
    }
    logln(buffer);
}
