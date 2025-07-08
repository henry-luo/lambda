#include "utf.h"

// decode UTF8 to UTF32 codepoint, returns number of bytes consumed, or -1 on error.
int utf8_to_codepoint(const unsigned char* utf8, uint32_t* codepoint) {
    unsigned char c = utf8[0];
    
    if (c <= 0x7F) {  // 1-byte sequence (ASCII)
        *codepoint = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {  // 2-byte sequence
        if ((utf8[1] & 0xC0) != 0x80) return -1;
        *codepoint = ((c & 0x1F) << 6) | (utf8[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {  // 3-byte sequence
        if ((utf8[1] & 0xC0) != 0x80 || (utf8[2] & 0xC0) != 0x80) return -1;
        *codepoint = ((c & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {  // 4-byte sequence
        if ((utf8[1] & 0xC0) != 0x80 || (utf8[2] & 0xC0) != 0x80 || 
            (utf8[3] & 0xC0) != 0x80) return -1;
        *codepoint = ((c & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | 
                     ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
        return 4;
    }
    return -1;  // Invalid UTF-8 sequence
}

// Count the number of UTF-8 characters in a string
int utf8_char_count(const char* utf8_string) {
    if (!utf8_string) return 0;
    
    int char_count = 0;
    const unsigned char* ptr = (const unsigned char*)utf8_string;
    uint32_t codepoint;
    
    while (*ptr) {
        int bytes_consumed = utf8_to_codepoint(ptr, &codepoint);
        if (bytes_consumed <= 0) {
            // Invalid UTF-8 sequence, skip this byte
            ptr++;
        } else {
            ptr += bytes_consumed;
            char_count++;
        }
    }
    
    return char_count;
}

