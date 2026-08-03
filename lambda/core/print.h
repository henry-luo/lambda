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
