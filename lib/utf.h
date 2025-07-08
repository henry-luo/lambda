#pragma once
#include <stdint.h>

int utf8_to_codepoint(const unsigned char* utf8, uint32_t* codepoint);
int utf8_char_count(const char* utf8_string);