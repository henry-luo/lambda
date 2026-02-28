/**
 * input-rfc-text.h — shared RFC-style key:value record parsing
 *
 * Common helpers for formats that use RFC-style line-oriented records
 * with line folding (VCF/RFC 6350, ICS/RFC 5545, EML/RFC 5322).
 *
 * parse_rfc_property_name  — read key before ':' or ';'
 * parse_rfc_property_value — read value after ':', handling line folding
 */
#ifndef LAMBDA_INPUT_RFC_TEXT_H
#define LAMBDA_INPUT_RFC_TEXT_H

#include "input-utils.h"
#include "../../lib/stringbuf.h"

/**
 * Parse an RFC-style property name: the text before the first ':' or ';'.
 * Stops at ':', ';', '\n', '\r', or NUL.
 * Returns the length written to @p sb (0 = nothing read).
 */
static inline size_t parse_rfc_property_name(StringBuf* sb, const char** pos) {
    stringbuf_reset(sb);
    const char* p = *pos;
    while (*p && *p != ':' && *p != ';' && *p != '\n' && *p != '\r') {
        stringbuf_append_char(sb, *p);
        p++;
    }
    *pos = p;
    return sb->length;
}

/**
 * Parse an RFC-style property value: text after ':', with line folding.
 *
 * Expects **pos to point at ':'.  Consumes the colon, reads value bytes,
 * unfolds continuation lines (lines starting with SP/HT), and advances
 * *pos past the final line terminator.
 *
 * Returns the length written to @p sb (0 = nothing read / no colon found).
 */
static inline size_t parse_rfc_property_value(StringBuf* sb, const char** pos) {
    if (**pos != ':') return 0;
    (*pos)++; // skip ':'

    stringbuf_reset(sb);
    const char* p = *pos;

    while (*p) {
        if (*p == '\r' || *p == '\n') {
            const char* next = p;
            if (*next == '\r' && *(next + 1) == '\n') next += 2;
            else next++;

            if (is_folded_line(next)) {
                // continuation line — replace line break with space
                stringbuf_append_char(sb, ' ');
                p = next;
                skip_line_whitespace(&p);
            } else {
                // end of value
                p = next;
                break;
            }
        } else {
            stringbuf_append_char(sb, *p);
            p++;
        }
    }
    *pos = p;
    return sb->length;
}

#endif // LAMBDA_INPUT_RFC_TEXT_H
