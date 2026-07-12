/**
 * input-rfc-text.h — shared RFC-style key:value record parsing
 *
 * Common helpers for formats that use RFC-style line-oriented records
 * with line folding (VCF/RFC 6350, ICS/RFC 5545, EML/RFC 5322).
 *
 * parse_rfc_property_name   — read key before ':' or ';'
 * parse_rfc_property_value  — read value after ':', handling line folding
 * parse_rfc_property_params — read ';'-delimited parameter list into a Map
 */
#ifndef LAMBDA_INPUT_RFC_TEXT_H
#define LAMBDA_INPUT_RFC_TEXT_H

#include "input-utils.h"
#include "input-context.hpp"
#include "../../lib/str.h"
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

static inline size_t parse_rfc_value_after_colon(StringBuf* sb, const char** pos,
                                                  bool skip_initial_ws,
                                                  bool trim_trailing_ws) {
    if (**pos != ':') return 0;
    (*pos)++; // skip ':'
    if (skip_initial_ws) {
        skip_line_whitespace(pos);
    }

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

    if (trim_trailing_ws) {
        while (sb->length > 0 &&
               (sb->str->chars[sb->length - 1] == ' ' || sb->str->chars[sb->length - 1] == '\t')) {
            sb->length--;
            sb->str->len = sb->length;
            sb->str->chars[sb->length] = '\0';
        }
    }
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
    return parse_rfc_value_after_colon(sb, pos, false, false);
}

/**
 * Parse an RFC 5322-style header value: skip optional whitespace after ':',
 * unfold continuation lines, and trim trailing horizontal whitespace.
 */
static inline size_t parse_rfc_header_value(StringBuf* sb, const char** pos) {
    return parse_rfc_value_after_colon(sb, pos, true, true);
}

/**
 * Parse ';'-delimited parameters into @p params_map.
 *
 * On entry @p *pos must point at ';' or ':'.
 * Returns leaving *pos at ':' (ready for parse_rfc_property_value).
 *
 * @param sb             Scratch string buffer (reset internally)
 * @param pos            In/out: current parse position
 * @param ctx            InputContext for pool allocation
 * @param params_map     Map to populate with PARAM_NAME → value pairs
 * @param upper_case_keys  true → normalise param names to UPPER; false → lower
 */
namespace lambda {
static inline char rfc_ascii_case_char(char c, bool uppercase) {
    char out = c;
    if (uppercase) {
        str_to_upper(&out, &c, 1);
    }
    else {
        str_to_lower(&out, &c, 1);
    }
    return out;
}

static inline void parse_rfc_property_params(StringBuf* sb, const char** pos,
                                              InputContext& ctx, Map* params_map,
                                              bool upper_case_keys) {
    MarkBuilder& builder = ctx.builder;
    while (**pos == ';') {
        (*pos)++;   // skip ';'
        stringbuf_reset(sb);
        while (**pos && **pos != '=' && **pos != ':' && **pos != '\n' && **pos != '\r') {
            char c = rfc_ascii_case_char(**pos, upper_case_keys);
            stringbuf_append_char(sb, c);
            (*pos)++;
        }
        if (sb->length == 0) continue;

        String* param_name = builder.createName(sb->str->chars, sb->length);
        if (!param_name) continue;

        String* param_value = NULL;
        if (**pos == '=') {
            (*pos)++;  // skip '='
            stringbuf_reset(sb);
            bool in_quotes = (**pos == '"');
            if (in_quotes) (*pos)++;
            while (**pos &&
                   (in_quotes ? **pos != '"'
                               : (**pos != ';' && **pos != ':')) &&
                   **pos != '\n' && **pos != '\r') {
                stringbuf_append_char(sb, **pos);
                (*pos)++;
            }
            if (in_quotes && **pos == '"') (*pos)++;  // skip closing quote
            if (sb->length > 0)
                param_value = builder.createString(sb->str->chars, sb->length);
        }
        if (param_value) {
            Item value = {.item = s2it(param_value)};
            builder.putToMap(lam::gc_borrow(params_map), param_name, value);
        }
    }
}
} // namespace lambda

#endif // LAMBDA_INPUT_RFC_TEXT_H
