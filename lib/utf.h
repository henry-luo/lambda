#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int utf8_to_codepoint(const unsigned char* utf8, uint32_t* codepoint);
int utf8_char_count(const char* utf8_string);

#ifdef __cplusplus
}
#endif