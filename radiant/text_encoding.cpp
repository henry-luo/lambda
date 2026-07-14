#include "event.hpp"

uint32_t tc_utf8_to_utf16_length(const char* s, uint32_t byte_len) {
    if (!s) return 0;
    uint32_t length = 0;
    const unsigned char* bytes = (const unsigned char*)s;
    for (uint32_t i = 0; i < byte_len; i++) {
        unsigned char byte = bytes[i];
        if ((byte & 0xC0) != 0x80) {
            length += byte < 0xF0 ? 1 : 2;
        }
    }
    return length;
}

uint32_t tc_utf16_to_utf8_offset(const char* s, uint32_t byte_len, uint32_t u16) {
    if (!s || u16 == 0) return 0;
    uint32_t seen = 0;
    const unsigned char* bytes = (const unsigned char*)s;
    for (uint32_t i = 0; i < byte_len; i++) {
        unsigned char byte = bytes[i];
        if ((byte & 0xC0) != 0x80) {
            if (seen >= u16) return i;
            seen += byte < 0xF0 ? 1 : 2;
        }
    }
    return byte_len;
}

uint32_t tc_utf8_to_utf16_offset(const char* s, uint32_t byte_len, uint32_t u8) {
    if (!s) return 0;
    if (u8 > byte_len) u8 = byte_len;
    return tc_utf8_to_utf16_length(s, u8);
}
