#pragma once
#include <stddef.h>

// Mark names share one token rule between the reader and the value printer.
static inline bool mark_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
        (unsigned char)c >= 0x80;
}
static inline bool mark_is_name_char(char c) {
    return mark_is_name_start(c) || (c >= '0' && c <= '9') || c == '-';
}
static inline bool mark_is_unquoted_name(const char* chars, size_t length) {
    if (!length || !mark_is_name_start(chars[0])) return false;
    for (size_t i = 1; i < length; i++) if (!mark_is_name_char(chars[i])) return false;
    return true;
}
