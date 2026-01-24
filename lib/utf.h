#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int utf8_to_codepoint(const unsigned char* utf8, uint32_t* codepoint);
int utf8_char_len(unsigned char first_byte);  // get char length from first byte
int utf8_char_count(const char* utf8_string);
int utf8_char_count_n(const char* utf8_string, size_t byte_len);  // count chars in first n bytes
int utf8_char_to_byte_offset(const char* utf8_string, int char_index);

#ifdef __cplusplus
}
#endif