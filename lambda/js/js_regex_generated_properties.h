#pragma once

#include <stdint.h>
#include "../core/codepoint_interval.h"

enum {
    JS_REGEX_PROP_GENERATED_BASE = 1000
};

bool js_regex_sorted_range_contains(const CodePointInterval* ranges, int count, int cp);
// Kept only for the legacy hand-written property tables; it is the shared type.
using JsRegexRange = CodePointInterval;
int js_regex_generated_property_lookup_kind(const char* name, int name_len);
int js_regex_generated_property_canonicalize_kind(int kind);
bool js_regex_generated_property_kind_contains(int kind, int cp);
bool js_regex_generated_property_kind_contains_cursor(int kind, int cp, int* cursor);

extern "C" int js_regex_wrapper_lookup_property_ranges(const char* name, int name_len,
                                                       int* out_pairs, int max_pairs);
extern "C" bool js_unicode_id_is_start(uint32_t cp);
extern "C" bool js_unicode_id_is_continue(uint32_t cp);
