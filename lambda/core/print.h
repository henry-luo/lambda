#pragma once

#include "../lambda-data.hpp"

typedef struct StrBuf StrBuf;

// Core value rendering is independent of AST inspection and is shared by
// formatters and active runtime diagnostics.
void print_item(StrBuf* strbuf, Item item, int depth = 0, const char* indent = "  ");

// Render an `int` from its native double. THE renderer for the int lane: it
// covers the full C16 domain (exact below 2^53, %.0f above it, where an i64
// conversion would clamp) and spells the poison. Any other int-to-text path
// must call this rather than reimplement it.
void print_int_value(StrBuf* strbuf, double value);

// THE int renderer's core: writes an int-lane value's spelling into buf (at
// least PRINT_INT_VALUE_CHARS_CAP bytes, NUL-terminated) and returns its
// length -- the poison spellings and the int53 band. Returns 0 for a finite
// value outside the band, which print_int_value renders with "%.0f".
#define PRINT_INT_VALUE_CHARS_CAP 24
size_t print_int_value_chars(char* buf, double value);
