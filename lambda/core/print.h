#pragma once

#include "../lambda-data.hpp"

typedef struct StrBuf StrBuf;

// Core value rendering is independent of AST inspection and is shared by
// formatters and active runtime diagnostics.
void print_item(StrBuf* strbuf, Item item, int depth = 0, const char* indent = "  ");
