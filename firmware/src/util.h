#pragma once
#include <Arduino.h>
#include <cstring>

// Bounded copy that always NUL-terminates; a null src yields an empty string.
static inline void copyString(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}
