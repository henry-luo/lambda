#ifndef LAMBDA_INPUT_JSX_COMMON_HPP
#define LAMBDA_INPUT_JSX_COMMON_HPP

#include "input-utils.h"
#include "../../lib/str.h"
#include <stddef.h>

static inline bool jsx_is_identifier_start(char c) {
    return str_char_is_alpha(c) || c == '_';
}

static inline bool jsx_is_identifier_char(char c) {
    return str_char_is_alnum(c) || c == '_' || c == '$';
}

static inline bool jsx_is_whitespace(char c) {
    return input_is_whitespace_char(c);
}

static inline void jsx_skip_whitespace(const char** p, const char* end) {
    while (*p < end && jsx_is_whitespace(**p)) {
        (*p)++;
    }
}

static inline bool jsx_is_component_tag(const char* tag_name) {
    return tag_name && tag_name[0] >= 'A' && tag_name[0] <= 'Z';
}

static inline bool jsx_is_html_tag(const char* tag_name) {
    return tag_name && tag_name[0] >= 'a' && tag_name[0] <= 'z';
}

static inline const char* jsx_scan_tag_name_after_lt(const char* pos, const char* end,
                                                     const char** name_start,
                                                     size_t* name_len) {
    if (name_start) *name_start = NULL;
    if (name_len) *name_len = 0;
    if (!pos || pos >= end || *pos != '<') return NULL;

    const char* p = pos + 1;
    const char* start = p;
    while (p < end && *p != '>' && *p != '/' && !jsx_is_whitespace(*p)) {
        p++;
    }
    if (p == start) return NULL;

    if (name_start) *name_start = start;
    if (name_len) *name_len = (size_t)(p - start);
    return p;
}

#endif
