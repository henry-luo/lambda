#pragma once

#include "../io/mark_builder.hpp"
#include "../../lib/strbuf.h"

// shared PDF/PostScript byte syntax; callers choose their output representation.
struct PsStringResult {
    const char* next;
    bool terminated;
    bool limited;
};

bool ps_is_space(unsigned char ch);
const char* ps_skip_comment(const char* p, const char* end);
PsStringResult ps_read_literal(const char* p, const char* end, StrBuf* out,
                              size_t max_bytes, bool normalize_lines);

// scans one source object, never executing names or reading beyond that object.
Item postscript_scan_io(Input* owner, Item source, size_t offset,
                        size_t max_bytes, size_t max_depth, size_t source_end = SIZE_MAX,
                        size_t max_objects = 65536);
bool eps_source_expected(const char* path, const char* bytes, size_t length);
