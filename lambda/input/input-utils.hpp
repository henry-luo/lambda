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
 * Encode a codepoint as UTF-8 and append to a StringBuf.
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8(StringBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = codepoint_to_utf8(codepoint, buf);
    for (int i = 0; i < n; i++) {
        stringbuf_append_char(sb, buf[i]);
    }
}

/**
 * Encode a codepoint as UTF-8 and append to a StrBuf (lib/strbuf.h).
 * Convenience wrapper around codepoint_to_utf8().
 */
static inline void append_codepoint_utf8_strbuf(StrBuf* sb, uint32_t codepoint) {
    char buf[5];
    int n = codepoint_to_utf8(codepoint, buf);
    for (int i = 0; i < n; i++) {
        strbuf_append_char(sb, buf[i]);
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
            if (!(*pos)[0] || !(*pos)[1] || !(*pos)[2] || !(*pos)[3]) {
                // not enough digits — output replacement char
                stringbuf_append_char(sb, (char)0xEF);
                stringbuf_append_char(sb, (char)0xBF);
                stringbuf_append_char(sb, (char)0xBD);
                break;
            }
            char hex[5] = {(*pos)[0], (*pos)[1], (*pos)[2], (*pos)[3], '\0'};
            char* end;
            unsigned long cp = strtoul(hex, &end, 16);
            if (end != hex + 4) {
                stringbuf_append_char(sb, (char)0xEF);
                stringbuf_append_char(sb, (char)0xBF);
                stringbuf_append_char(sb, (char)0xBD);
                break;
            }
            (*pos) += 4;

            if (cp >= 0xD800 && cp <= 0xDBFF) {
                // high surrogate — look for low surrogate \uXXXX
                const char* la = *pos;
                if (la[0] == '\\' && la[1] == 'u' &&
                    la[2] && la[3] && la[4] && la[5]) {
                    char hex2[5] = {la[2], la[3], la[4], la[5], '\0'};
                    char* end2;
                    unsigned long lo = strtoul(hex2, &end2, 16);
                    uint32_t combined = decode_surrogate_pair((uint16_t)cp, (uint16_t)lo);
                    if (end2 == hex2 + 4 && combined != 0) {
                        cp = combined;
                        (*pos) += 6;  // skip \uXXXX
                    } else {
                        cp = 0xFFFD;
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;  // lone low surrogate
            }
            append_codepoint_utf8(sb, (uint32_t)cp);
            break;
        }
        default:
            stringbuf_append_char(sb, c);
            (*pos)++;
            break;
    }
    return (int)(*pos - start);
}

} // namespace lambda
