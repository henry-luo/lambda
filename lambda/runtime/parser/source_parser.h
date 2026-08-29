#pragma once

// Language-neutral source cursor mechanics shared by first-party parsers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ParserSource {
    const char* source;
    size_t length;
    size_t offset;
    uint32_t line;
    uint32_t column;
} ParserSource;

void parser_source_init(ParserSource* source, const char* text, size_t length,
        uint32_t first_line);
bool parser_source_has(const ParserSource* source, size_t count);
unsigned char parser_source_peek(const ParserSource* source, size_t ahead);
void parser_source_advance_byte(ParserSource* source);
bool parser_source_is_newline(const ParserSource* source);
void parser_source_advance_newline(ParserSource* source);

#ifdef __cplusplus
}
#endif
