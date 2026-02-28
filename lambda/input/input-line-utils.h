#ifndef LAMBDA_INPUT_LINE_UTILS_H
#define LAMBDA_INPUT_LINE_UTILS_H

/**
 * @file input-line-utils.h
 * @brief Shared line-oriented parsing helpers for key-value / RFC-style parsers.
 *
 * Extracted from duplicate implementations in input-eml.cpp, input-vcf.cpp,
 * input-ics.cpp, input-ini.cpp, and input-prop.cpp.
 */

#include <stdbool.h>

// ── Skip helpers ───────────────────────────────────────────────────

/** Skip horizontal whitespace (space and tab) only. */
static inline void skip_line_whitespace(const char** p) {
    while (**p && (**p == ' ' || **p == '\t')) {
        (*p)++;
    }
}

/**
 * Skip to end of current line and past the line terminator.
 * Handles \n, \r, and \r\n.
 */
static inline void skip_to_newline(const char** p) {
    while (**p && **p != '\n' && **p != '\r') {
        (*p)++;
    }
    if (**p == '\r' && *(*p + 1) == '\n') {
        (*p) += 2; // skip \r\n
    } else if (**p == '\n' || **p == '\r') {
        (*p)++; // skip \n or \r
    }
}

// ── Line classification ────────────────────────────────────────────

/**
 * Return true if position starts a folded (continuation) line —
 * i.e. the first character is a space or tab (RFC 5322 / RFC 6350 / RFC 5545).
 */
static inline bool is_folded_line(const char* p) {
    return *p == ' ' || *p == '\t';
}

#endif // LAMBDA_INPUT_LINE_UTILS_H
