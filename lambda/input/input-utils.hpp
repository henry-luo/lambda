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

static inline void skip_to_newline_raw(const char** p) {
    while (**p && **p != '\n' && **p != '\r') {
        (*p)++;
    }
    if (**p == '\r' && *(*p + 1) == '\n') {
        (*p) += 2;
    } else if (**p == '\n' || **p == '\r') {
        (*p)++;
    }
}

static inline void skip_to_newline_with_tracker(const char** p, SourceTracker* tracker) {
    const char* start = *p;
    skip_to_newline_raw(p);
    if (tracker) tracker->advance((size_t)(*p - start));
}

static inline int encode_codepoint_utf8(uint32_t codepoint, char out[5]) {
    return codepoint_to_utf8(codepoint, out);
}

/**
 * Encode a codepoint as UTF-8 and append to a StringBuf.
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8(StringBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = encode_codepoint_utf8(codepoint, buf);
    if (n > 0) stringbuf_append_str_n(sb, buf, (size_t)n);
}

/**
 * Encode a codepoint as UTF-8 and append to a StrBuf (lib/strbuf.h).
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8_strbuf(StrBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = encode_codepoint_utf8(codepoint, buf);
    if (n > 0) strbuf_append_str_n(sb, buf, (size_t)n);
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
            uint32_t cp = parse_hex_codepoint(pos, 4);
            if (cp == 0xFFFFFFFF) {
                // not enough digits — output replacement char
                stringbuf_append_char(sb, (char)0xEF);
                stringbuf_append_char(sb, (char)0xBF);
                stringbuf_append_char(sb, (char)0xBD);
                break;
            }

            if (cp >= 0xD800 && cp <= 0xDBFF) {
                // high surrogate — look for low surrogate \uXXXX
                const char* la = *pos;
                if (la[0] == '\\' && la[1] == 'u' &&
                    la[2] && la[3] && la[4] && la[5]) {
                    const char* low_pos = la + 2;
                    uint32_t lo = parse_hex_codepoint(&low_pos, 4);
                    uint32_t combined = decode_surrogate_pair((uint16_t)cp, (uint16_t)lo);
                    if (lo != 0xFFFFFFFF && combined != 0) {
                        cp = combined;
                        *pos = low_pos;
                    } else {
                        cp = 0xFFFD;
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;  // lone low surrogate
            }
            append_codepoint_utf8(sb, cp);
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
