/**
 * @file input-utils.hpp
 * @brief Shared C++ utility functions for Lambda input parsers
 *
 * Provides helpers that depend on InputContext (pool allocation, etc.)
 */

#pragma once
#include "input-utils.h"
#include "input-context.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/escape.h"

namespace lambda {

/**
 * Auto-type a raw string value into a typed Lambda Item.
 *
 * Attempts, in order: bool → null → integer → float → string.
 * Boolean keywords recognised (case-insensitive):
 *   true, yes, on, 1  →  bool true
 *   false, no, off, 0 →  bool false
 * Null keywords: null, nil, empty
 *
 * @param ctx   InputContext for pool allocation
 * @param str   Raw string bytes (not necessarily null-terminated)
 * @param len   Length of @p str
 * @return Typed Item (bool, null, int64, double, or String)
 */
Item parse_typed_value(InputContext& ctx, const char* str, size_t len);

/**
 * Parse an integer token into the smallest exact numeric home: int → int64 → decimal.
 */
Item parse_integer_token_exact(InputContext& ctx, const char* str, size_t len);

/**
 * Parse a scanner-delimited decimal number token.  Optional underscores are
 * removed before conversion; integer tokens still use exact integer promotion.
 */
Item parse_scanned_decimal_number(InputContext& ctx, const char* str, size_t len,
                                  bool allow_underscores, bool negative_zero_is_float);

bool scanned_number_has_float_marker(const char* str, size_t len);

Item parse_prefixed_integer_value(InputContext& ctx, const char* str, int base,
                                  const char* kind, SourceLocation loc,
                                  const char** end_out, bool report_errors,
                                  bool force_long);

static inline void input_skip_to_eol(SourceTracker& tracker) {
    while (!tracker.atEnd() && tracker.current() != '\n') {
        tracker.advance();
    }
}

static inline void input_skip_whitespace_and_comment_markers(SourceTracker& tracker,
                                                            const char* line_comment1,
                                                            const char* line_comment2,
                                                            bool block_comments) {
    while (!tracker.atEnd()) {
        char c = tracker.current();
        if (input_is_whitespace_char(c)) {
            tracker.advance();
            continue;
        }
        if (block_comments && c == '/' && tracker.peek(1) == '*') {
            tracker.advance();
            tracker.advance();
            while (!tracker.atEnd() &&
                   !(tracker.current() == '*' && tracker.peek(1) == '/')) {
                tracker.advance();
            }
            if (!tracker.atEnd()) {
                tracker.advance();
                tracker.advance();
            }
            continue;
        }
        if (line_comment1 && tracker.match(line_comment1)) {
            input_skip_to_eol(tracker);
            continue;
        }
        if (line_comment2 && tracker.match(line_comment2)) {
            input_skip_to_eol(tracker);
            continue;
        }
        break;
    }
}

/**
 * Handle one JSON/properties escape sequence starting at @p *pos (which must
 * point at the char AFTER the leading backslash).
 *
 * Recognised escapes: \" \\ \/ \b \f \n \r \t \uXXXX (UTF-16 surrogate pairs
 * are combined automatically).  Unknown escapes append the literal char.
 *
 * @param pos  In/out: advanced past the escape sequence on return
 * @param sb   Output buffer — decoded bytes are appended here
 * @return     Number of source chars consumed (i.e. how much *pos advanced)
 */
static inline int parse_escape_char(const char** pos, StringBuf* sb) {
    const char* start = *pos;
    char c = **pos;
    // nothing to escape: a trailing backslash at end-of-input lands the caller on the
    // NUL terminator. Return 0 (consume nothing) so the default branch below cannot
    // append the NUL and advance past the buffer end.
    if (!c) return 0;

    switch (c) {
        case '"':  stringbuf_append_char(sb, '"');  (*pos)++; break;
        case '\\': stringbuf_append_char(sb, '\\'); (*pos)++; break;
        case '/':  stringbuf_append_char(sb, '/');  (*pos)++; break;
        case 'b':  stringbuf_append_char(sb, '\b'); (*pos)++; break;
        case 'f':  stringbuf_append_char(sb, '\f'); (*pos)++; break;
        case 'n':  stringbuf_append_char(sb, '\n'); (*pos)++; break;
        case 'r':  stringbuf_append_char(sb, '\r'); (*pos)++; break;
        case 't':  stringbuf_append_char(sb, '\t'); (*pos)++; break;
        case 'u': {
            (*pos)++;  // skip 'u'
            uint32_t cp = 0;
            size_t consumed = 0;
            if (!escape_decode_utf16_escape(*pos, strlen(*pos), true, &cp, &consumed)) {
                // not enough digits — output replacement char
                stringbuf_append_utf8(sb, 0xFFFD);
                break;
            }
            *pos += consumed;
            stringbuf_append_utf8(sb, cp);
            break;
        }
        default:
            stringbuf_append_char(sb, c);
            (*pos)++;
            break;
    }
    return (int)(*pos - start);
}

/**
 * Match and consume a literal keyword in a pointer-style parser.
 *
 * Checks that *src starts with @p literal, then advances both *src and
 * ctx.tracker by len(literal).  On mismatch, adds an error and returns false
 * (leaving *src and the tracker unchanged).
 *
 * @param ctx     InputContext (for tracker sync + error reporting)
 * @param src     In/out: pointer advanced on success
 * @param literal NUL-terminated string to match (e.g. "true", "null")
 * @return        true on success, false on mismatch
 */
static inline bool input_expect_literal(InputContext& ctx, const char** src,
                                        const char* literal) {
    size_t len = strlen(literal);
    if (strncmp(*src, literal, len) != 0) {
        ctx.addError(ctx.tracker.location(), "Expected '%s'", literal);
        return false;
    }
    *src += len;
    ctx.tracker.advance(len);
    return true;
}

/**
 * Parse a double-quoted string from ctx.tracker.
 *
 * On entry tracker.current() must be '"'.  Handles standard escape sequences
 * via parse_escape_char() (superset of JSON/DOT/D2 escapes).
 *
 * @param ctx  InputContext for tracker, string-buffer, and builder
 * @return     New String* on success, nullptr on error
 */
static inline String* parse_shared_quoted_string(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    if (tracker.atEnd() || tracker.current() != '"') return nullptr;

    SourceLocation start_loc = tracker.location();
    tracker.advance(); // skip opening '"'

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (!tracker.atEnd() && tracker.current() != '"') {
        char c = tracker.current();
        if (c == '\\') {
            tracker.advance(); // skip '\'
            if (tracker.atEnd()) {
                ctx.addError(tracker.location(), "Unterminated string escape");
                return nullptr;
            }
            const char* pos = tracker.rest();
            int consumed = parse_escape_char(&pos, sb);
            tracker.advance(consumed);
        } else {
            stringbuf_append_char(sb, c);
            tracker.advance();
        }
    }

    if (tracker.atEnd()) {
        ctx.addError(start_loc, "Unterminated quoted string");
        return nullptr;
    }

    tracker.advance(); // skip closing '"'
    return ctx.builder.createString(sb->str->chars, sb->length);
}

} // namespace lambda
