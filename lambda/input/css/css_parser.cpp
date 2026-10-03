/**
 * CSS Parser Implementation
 *
 * This file contains the core CSS parsing logic that was restored from
 * git history (commit 1f849233~1) and adapted to work with the new
 * modular CSS architecture.
 *
 * Functions:
 * - Token navigation and whitespace skipping
 * - Selector parsing (element, class, ID, universal)
 * - Declaration parsing (property: value with !important support)
 * - Rule parsing with proper token consumption tracking
 */

#include "css_parser.hpp"
#include "css_style.hpp"
#include "../../../lib/log.h"
#include "../../../lib/mem_grow.hpp"
#include "../../../lib/str.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/escape.h"
#include "../../../lib/recursion_guard.hpp"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Selector and declaration traces are per-token diagnostics, not routine debug output.
#define log_debug(...) log_trace(__VA_ARGS__)

// Caps nested CSS function parsing (e.g. calc(calc(calc(...)))) so pathological input
// reports a parse failure instead of recursing until the stack overflows.
#define MAX_CSS_FUNC_DEPTH 256
#define MAX_CSS_RULE_DEPTH 128

static char* css_parser_unescape_url_component(const char* str, size_t len, Pool* pool) {
    return escape_css_unescape_pool(pool, str, len, true,
        ESCAPE_CSS_EOF_REPLACEMENT, true, true);
}

static const char* css_unicode_skip_ignorable(const char* p, const char* end,
                                              bool allow_whitespace) {
    if (!p || !end) return p;
    for (;;) {
        if (allow_whitespace) {
            while (p < end && (p[0] == ' ' || p[0] == '\t' || p[0] == '\n' ||
                               p[0] == '\r' || p[0] == '\f')) p++;
        }
        if (p + 1 >= end || p[0] != '/' || p[1] != '*') break;
        p += 2;
        while (p < end && p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
        if (p + 1 >= end) return end;
        p += 2;
    }
    return p;
}

static uint32_t css_unicode_parse_hex(const char* chars, int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        value = (value << 4) | (uint32_t)str_hex_val(chars[i]);
    }
    return value;
}

static int css_unicode_format_codepoint(uint32_t codepoint, char* output, size_t output_size) {
    if (!output || output_size < 2) return 0;
    if (codepoint == 0) {
        output[0] = '0';
        output[1] = '\0';
        return 1;
    }

    char reversed[8];
    int length = 0;
    while (codepoint > 0 && length < 6) {
        int digit = (int)(codepoint & 0xF);
        reversed[length++] = digit < 10 ? (char)('0' + digit) : (char)('A' + digit - 10);
        codepoint >>= 4;
    }
    int output_length = length < (int)output_size - 1 ? length : (int)output_size - 1;
    for (int i = 0; i < output_length; i++) output[i] = reversed[length - i - 1];
    output[output_length] = '\0';
    return output_length;
}

static const char* css_unicode_make_range(uint32_t start, uint32_t end, bool has_range, Pool* pool) {
    if (!pool || start > 0x10FFFF || end > 0x10FFFF) return NULL;

    char start_text[8];
    char end_text[8];
    char result[32];
    css_unicode_format_codepoint(start, start_text, sizeof(start_text));
    if (!has_range) {
        snprintf(result, sizeof(result), "U+%s", start_text);
    } else {
        css_unicode_format_codepoint(end, end_text, sizeof(end_text));
        snprintf(result, sizeof(result), "U+%s-%s", start_text, end_text);
    }
    return pool_strdup(pool, result);
}

static bool css_parse_unicode_range_parts(const char* input, size_t length,
                                          uint32_t* out_start, uint32_t* out_end,
                                          bool* out_has_range) {
    if (!input || length == 0 || !out_start || !out_end || !out_has_range) return false;

    const char* p = input;
    const char* end = input + length;
    while (p < end && (p[0] == ' ' || p[0] == '\t')) p++;
    if (p >= end || (p[0] != 'u' && p[0] != 'U')) return false;
    p++;

    // Comments disappear during tokenization; whitespace separates these tokens.
    p = css_unicode_skip_ignorable(p, end, false);
    if (p >= end || p[0] != '+') return false;
    p++;
    p = css_unicode_skip_ignorable(p, end, false);

    char start_chars[7];
    int start_count = 0;
    while (p < end && str_is_hex(p[0]) && start_count < 6) {
        start_chars[start_count++] = p[0];
        p++;
    }

    p = css_unicode_skip_ignorable(p, end, false);
    int wildcard_count = 0;
    while (p < end && p[0] == '?') {
        wildcard_count++;
        p++;
    }
    if (start_count + wildcard_count == 0 || start_count + wildcard_count > 6) return false;

    p = css_unicode_skip_ignorable(p, end, false);
    uint32_t start = css_unicode_parse_hex(start_chars, start_count);
    if (wildcard_count > 0) {
        // a wildcard range must end at the declaration/rule boundary.
        const char* rest = p;
        rest = css_unicode_skip_ignorable(rest, end, true);
        if (rest < end && rest[0] != ';' && rest[0] != '}') return false;

        for (int i = 0; i < wildcard_count; i++) start <<= 4;
        uint32_t finish = css_unicode_parse_hex(start_chars, start_count);
        for (int i = 0; i < wildcard_count; i++) finish = (finish << 4) | 0xF;
        *out_start = start;
        *out_end = finish;
        *out_has_range = true;
        return true;
    }

    // more than six leading hex digits are not a valid unicode-range.
    if (p < end && str_is_hex(p[0])) return false;

    p = css_unicode_skip_ignorable(p, end, false);
    bool has_range = p < end && p[0] == '-';
    uint32_t finish = start;
    if (has_range) {
        p++;
        p = css_unicode_skip_ignorable(p, end, false);
        char end_chars[7];
        int end_count = 0;
        while (p < end && str_is_hex(p[0]) && end_count < 6) {
            end_chars[end_count++] = p[0];
            p++;
        }
        if (end_count == 0 || (p < end && str_is_hex(p[0]))) return false;
        if (p < end && p[0] == '?') return false;
        finish = css_unicode_parse_hex(end_chars, end_count);
    }

    p = css_unicode_skip_ignorable(p, end, true);
    if (p < end && p[0] != ';' && p[0] != '}') return false;
    *out_start = start;
    *out_end = finish;
    *out_has_range = has_range;
    return true;
}

bool css_parse_unicode_range_bounds(const char* input, size_t length,
                                    uint32_t* out_start, uint32_t* out_end) {
    bool has_range = false;
    return css_parse_unicode_range_parts(input, length, out_start, out_end, &has_range);
}

/** parse one CSS <unicode-range> and return its CSSOM canonical spelling. */
const char* css_parse_unicode_range_canonical(const char* input, size_t length, Pool* pool) {
    if (!pool) return NULL;

    uint32_t start = 0;
    uint32_t end = 0;
    bool has_range = false;
    if (!css_parse_unicode_range_parts(input, length, &start, &end, &has_range)) return NULL;
    return css_unicode_make_range(start, end, has_range, pool);
}

// helper: map a functional pseudo-class name to its selector type
static CssSelectorType css_functional_pseudo_type(const char* func_name) {
    if (strcmp(func_name, "nth-child") == 0)         return CSS_SELECTOR_PSEUDO_NTH_CHILD;
    if (strcmp(func_name, "nth-of-type") == 0)       return CSS_SELECTOR_PSEUDO_NTH_OF_TYPE;
    if (strcmp(func_name, "nth-last-child") == 0)    return CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD;
    if (strcmp(func_name, "nth-last-of-type") == 0)  return CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
    if (strcmp(func_name, "not") == 0)               return CSS_SELECTOR_PSEUDO_NOT;
    if (strcmp(func_name, "is") == 0)                return CSS_SELECTOR_PSEUDO_IS;
    if (strcmp(func_name, "where") == 0)             return CSS_SELECTOR_PSEUDO_WHERE;
    if (strcmp(func_name, "has") == 0)               return CSS_SELECTOR_PSEUDO_HAS;
    if (strcmp(func_name, "lang") == 0)              return CSS_SELECTOR_PSEUDO_LANG;
    if (strcmp(func_name, "dir") == 0)               return CSS_SELECTOR_PSEUDO_DIR;
    if (strcmp(func_name, "host") == 0 || strcmp(func_name, "host-context") == 0)
        return CSS_SELECTOR_PSEUDO_IS;               // Shadow DOM — treat like :is()
    if (strcmp(func_name, "slotted") == 0)           return CSS_SELECTOR_PSEUDO_SLOTTED;
    return CSS_SELECTOR_PSEUDO_GENERIC;               // unknown to CSS selectors
}

static bool css_parse_anb_suffix(const CssToken* tokens, int end, int* pos, int* b) {
    while (*pos < end && tokens[*pos].type == CSS_TOKEN_WHITESPACE) (*pos)++;
    if (*pos >= end) return true;

    if (tokens[*pos].type == CSS_TOKEN_NUMBER) {
        double value = tokens[*pos].data.number_value;
        if (value != (int)value) return false;
        const char* number_start = tokens[*pos].start;
        if (number_start && *number_start != '+' && *number_start != '-') return false;
        *b = (int)value;
        (*pos)++;
    } else if (tokens[*pos].type == CSS_TOKEN_DELIM &&
               (tokens[*pos].data.delimiter == '+' || tokens[*pos].data.delimiter == '-')) {
        int sign = tokens[*pos].data.delimiter == '-' ? -1 : 1;
        (*pos)++;
        while (*pos < end && tokens[*pos].type == CSS_TOKEN_WHITESPACE) (*pos)++;
        if (*pos >= end || tokens[*pos].type != CSS_TOKEN_NUMBER) return false;
        double value = tokens[*pos].data.number_value;
        if (value != (int)value) return false;
        const char* number_start = tokens[*pos].start;
        if (number_start && (*number_start == '+' || *number_start == '-')) return false;
        *b = sign * (int)value;
        (*pos)++;
    } else {
        return false;
    }

    while (*pos < end && tokens[*pos].type == CSS_TOKEN_WHITESPACE) (*pos)++;
    return *pos == end;
}

static bool css_anb_expect_end(const CssToken* tokens, int end, int pos) {
    while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
    return pos == end;
}

static bool css_parse_anb_ident_tail(const CssToken* tokens, int end, int* pos,
        const char* rest, size_t rest_len, int a, CssNthFormula* formula) {
    if (rest_len == 0) {
        formula->a = a;
        (*pos)++;
        return css_parse_anb_suffix(tokens, end, pos, &formula->b);
    }
    if (*rest != '-') return false;
    rest++;
    rest_len--;
    if (rest_len == 0) {
        formula->a = a;
        (*pos)++;
        while (*pos < end && tokens[*pos].type == CSS_TOKEN_WHITESPACE) (*pos)++;
        if (*pos >= end || tokens[*pos].type != CSS_TOKEN_NUMBER) return false;
        double value = tokens[*pos].data.number_value;
        if (value != (int)value) return false;
        const char* number_start = tokens[*pos].start;
        if (number_start && (*number_start == '+' || *number_start == '-')) return false;
        formula->b = -(int)value;
        return css_anb_expect_end(tokens, end, *pos + 1);
    }
    int b_value = 0;
    for (size_t i = 0; i < rest_len; i++) {
        if (rest[i] < '0' || rest[i] > '9') return false;
        b_value = b_value * 10 + (rest[i] - '0');
    }
    formula->a = a;
    formula->b = -b_value;
    return css_anb_expect_end(tokens, end, *pos + 1);
}

// ============================================================================
// An+B Token-Level Parser (CSS Syntax Level 3 §6.1)
// ============================================================================
// Strict token-level parsing of the An+B microsyntax.
// Returns true if the token sequence [start, end) is a valid An+B value
// and fills *formula accordingly. Returns false for parse errors.
//
// Grammar (token-level, simplified):
//   <an+b> =
//     odd | even
//   | <integer>                              -- e.g., "5"
//   | <n-dimension>                          -- e.g., "2n"
//   | <n-dimension> <signed-integer>         -- e.g., "2n" "+3" (signed number token)
//   | <n-dimension> ['+' | '-'] <signless-integer>  -- e.g., "2n" "+" "3"
//   | <ndashdigit-dimension>                 -- e.g., "2n-3" (single token)
//   | '+'?'n'                                -- bare "n" or "+n" (sign + ident, NO space)
//   | '-n'                                   -- ident "-n"
//   | '+'?'n' <signed-integer>
//   | '-n' <signed-integer>
//   | '+'?'n' ['+' | '-'] <signless-integer>
//   | '-n' ['+' | '-'] <signless-integer>
//   | '+'? <ndashdigit-ident>                -- e.g., "+n-3" or "n-3" (single ident)
//   | <dashndashdigit-ident>                 -- e.g., "-n-3" (single ident)
//
// Key constraints enforced:
//   - No whitespace between '+'/'-' sign and 'n' when they are separate tokens
//   - After 'n' or dimension, only one sign token is allowed before the integer
//   - <signless-integer> must not have a sign; <signed-integer> must have one
//
static bool css_parse_anb_from_tokens(const CssToken* tokens, int start, int end,
                                       CssNthFormula* formula) {
    formula->a = 0;
    formula->b = 0;
    formula->odd = false;
    formula->even = false;

    // skip leading whitespace
    int pos = start;
    while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
    if (pos >= end) return false;

    // helper: check remaining tokens are just whitespace
    #define EXPECT_END(p) do { \
        int _p = (p); \
        while (_p < end && tokens[_p].type == CSS_TOKEN_WHITESPACE) _p++; \
        if (_p != end) return false; \
    } while(0)

    // helper: get lowercase ident value
    const CssToken* t = &tokens[pos];

    // ---- odd / even ----
    if (t->type == CSS_TOKEN_IDENT) {
        const char* v = t->value ? t->value : "";
        size_t vlen = strlen(v);
        if (vlen == 3 && (v[0] == 'o' || v[0] == 'O') && (v[1] == 'd' || v[1] == 'D') && (v[2] == 'd' || v[2] == 'D')) {
            formula->odd = true;
            EXPECT_END(pos + 1);
            return true;
        }
        if (vlen == 4 && (v[0] == 'e' || v[0] == 'E') && (v[1] == 'v' || v[1] == 'V')
            && (v[2] == 'e' || v[2] == 'E') && (v[3] == 'n' || v[3] == 'N')) {
            formula->even = true;
            EXPECT_END(pos + 1);
            return true;
        }
    }

    // ---- <integer> (bare number, no n) ----
    if (t->type == CSS_TOKEN_NUMBER) {
        double nv = t->data.number_value;
        if (nv != (int)nv) return false; // must be integer
        formula->a = 0;
        formula->b = (int)nv;
        EXPECT_END(pos + 1);
        return true;
    }

    // ---- Patterns starting with DIMENSION (e.g., "2n", "2n-3") ----
    if (t->type == CSS_TOKEN_DIMENSION) {
        // the dimension must have unit "n" (case-insensitive)
        const char* raw = t->start;
        size_t raw_len = t->length;
        // find 'n' or 'N' in the token — the unit part
        // numeric value is at the start, 'n' follows
        const char* n_pos_ptr = NULL;
        for (size_t i = 0; i < raw_len; i++) {
            if (raw[i] == 'n' || raw[i] == 'N') {
                n_pos_ptr = raw + i;
                break;
            }
        }
        if (!n_pos_ptr) return false;

        // check if digits follow 'n' in same token (ndashdigit-dimension: "2n-3")
        size_t after_n = (n_pos_ptr - raw) + 1;
        if (after_n < raw_len) {
            // there's stuff after 'n', should be "-" followed by digits (dash-then-digits)
            if (raw[after_n] == '-') {
                // parse the integer after the dash
                int b_val = 0;
                size_t di = after_n + 1;
                if (di >= raw_len) {
                    // "5n-" with no digit in this token: treat as <n-dimension> '-' <signless-integer>
                    // The '-' is baked into the dimension token; the integer follows as next token
                    formula->a = (int)t->data.dimension.value;
                    pos++;
                    // skip whitespace between the trailing dash and the integer
                    while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
                    if (pos >= end || tokens[pos].type != CSS_TOKEN_NUMBER) return false;
                    double bv = tokens[pos].data.number_value;
                    if (bv != (int)bv) return false;
                    const char* ns = tokens[pos].start;
                    if (ns && (*ns == '+' || *ns == '-')) return false; // must be signless
                    formula->b = -(int)bv;
                    EXPECT_END(pos + 1);
                    return true;
                }
                while (di < raw_len) {
                    if (raw[di] < '0' || raw[di] > '9') return false;
                    b_val = b_val * 10 + (raw[di] - '0');
                    di++;
                }
                formula->a = (int)t->data.dimension.value;
                formula->b = -b_val;
                EXPECT_END(pos + 1);
                return true;
            }
            return false; // unexpected chars after 'n'
        }

        // plain n-dimension like "2n"
        formula->a = (int)t->data.dimension.value;
        pos++;
        return css_parse_anb_suffix(tokens, end, &pos, &formula->b);
    }

    // ---- Patterns starting with IDENT: "n", "-n", "n-3", "-n-3" ----
    if (t->type == CSS_TOKEN_IDENT) {
        const char* v = t->value ? t->value : "";
        size_t vlen = strlen(v);

        // check for "n" or "-n" (possibly with dash-digits: "n-3", "-n-3")
        bool negated = false;
        const char* after_sign = v;
        if (vlen > 0 && v[0] == '-') {
            negated = true;
            after_sign = v + 1;
        }

        // must start with 'n' or 'N' after optional leading '-'
        if (*after_sign != 'n' && *after_sign != 'N') return false;
        const char* rest = after_sign + 1;
        size_t rest_len = vlen - (rest - v);

        return css_parse_anb_ident_tail(tokens, end, &pos, rest, rest_len,
            negated ? -1 : 1, formula);
    }

    // ---- Patterns starting with '+' DELIM: "+n", "+n-3", "+n" <signed-integer> etc. ----
    if (t->type == CSS_TOKEN_DELIM && t->data.delimiter == '+') {
        pos++;
        // NO whitespace allowed between '+' and 'n'/ident
        if (pos >= end) return false;
        if (tokens[pos].type == CSS_TOKEN_WHITESPACE) return false; // "+ n" is parse error

        if (tokens[pos].type == CSS_TOKEN_IDENT) {
            const char* v = tokens[pos].value ? tokens[pos].value : "";
            size_t vlen = strlen(v);
            if (vlen == 0 || (*v != 'n' && *v != 'N')) return false;

            // check for ndashdigit-ident: "+n-3"
            const char* rest = v + 1;
            size_t rest_len = vlen - 1;
            return css_parse_anb_ident_tail(tokens, end, &pos, rest, rest_len, 1, formula);
        }

        // + followed by a number: "+1" → just an integer with leading +
        // But the tokenizer would have returned this as a signed number token, not '+' '1'
        return false;
    }

    #undef EXPECT_END
    return false;
}

// Helper: check if a selector type is an nth-* pseudo-class
static bool css_is_nth_pseudo(CssSelectorType type) {
    return type == CSS_SELECTOR_PSEUDO_NTH_CHILD ||
           type == CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD ||
           type == CSS_SELECTOR_PSEUDO_NTH_OF_TYPE ||
           type == CSS_SELECTOR_PSEUDO_NTH_LAST_OF_TYPE;
}

typedef struct CssSelectorFunction {
    const char* name;
    const char* argument;
    int argument_start;
    int argument_end;
} CssSelectorFunction;

// Functional pseudo-classes and pseudo-elements share the same token envelope.
static bool css_parse_selector_function(const CssToken* tokens, int* pos,
        int token_count, Pool* pool, CssSelectorFunction* function) {
    if (!tokens || !pos || !pool || !function || *pos >= token_count ||
        tokens[*pos].type != CSS_TOKEN_FUNCTION) {
        return false;
    }

    const CssToken* token = &tokens[*pos];
    char* name = css_token_value_dup(token, pool);
    if (!name) return false;
    size_t name_len = name ? strlen(name) : 0;
    if (name_len > 0 && name[name_len - 1] == '(') {
        name[name_len - 1] = '\0';
    }
    str_lower_inplace(name, strlen(name));

    (*pos)++;
    int argument_start = *pos;
    int paren_depth = 1;
    while (*pos < token_count && paren_depth > 0) {
        CssTokenType type = tokens[*pos].type;
        if (type == CSS_TOKEN_EOF) break;
        if (type == CSS_TOKEN_LEFT_PAREN || type == CSS_TOKEN_FUNCTION) {
            paren_depth++;
        } else if (type == CSS_TOKEN_RIGHT_PAREN) {
            paren_depth--;
            if (paren_depth == 0) break;
        }
        (*pos)++;
    }

    int argument_end = *pos;
    size_t argument_len = 0;
    for (int i = argument_start; i < argument_end; i++) {
        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
        argument_len += tokens[i].value ? strlen(tokens[i].value) : tokens[i].length;
    }
    char* argument = NULL;
    if (argument_len > 0) {
        argument = (char*)pool_calloc(pool, argument_len + 1);
        if (!argument) return false;
        char* output = argument;
        for (int i = argument_start; i < argument_end; i++) {
            if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
            const char* source = tokens[i].value ? tokens[i].value : tokens[i].start;
            size_t length = tokens[i].value ? strlen(tokens[i].value) : tokens[i].length;
            if (source && length > 0) {
                memcpy(output, source, length);
                output += length;
            }
        }
        *output = '\0';
    }
    if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) (*pos)++;

    function->name = name;
    function->argument = argument;
    function->argument_start = argument_start;
    function->argument_end = argument_end;
    return name != NULL;
}

// Helper: Skip whitespace and comment tokens
int css_skip_whitespace_tokens(const CssToken* tokens, int start, int token_count) {
    int pos = start;
    while (pos < token_count &&
           (tokens[pos].type == CSS_TOKEN_WHITESPACE ||
            tokens[pos].type == CSS_TOKEN_COMMENT)) {
        pos++;
    }
    return pos;
}

// skip an at-rule and its optional block while preserving the enclosing boundary
static int css_skip_at_rule_tokens(const CssToken* tokens, int pos, int token_count) {
    if (!tokens || pos < 0 || pos >= token_count ||
        tokens[pos].type != CSS_TOKEN_AT_KEYWORD) {
        return pos;
    }

    pos++;
    while (pos < token_count && tokens[pos].type != CSS_TOKEN_EOF &&
           tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
        if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
            int brace_depth = 1;
            pos++;
            while (pos < token_count && brace_depth > 0) {
                if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) brace_depth++;
                else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) brace_depth--;
                pos++;
            }
            break;
        }
        if (tokens[pos].type == CSS_TOKEN_SEMICOLON) {
            pos++;
            break;
        }
        pos++;
    }
    return pos;
}

bool css_selector_group_parse_consumed_all(const CssToken* tokens, int pos,
                                           int token_count) {
    if (!tokens || pos < 0 || token_count <= 0) return false;
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    // Selector API inputs are complete token streams, not nested-rule
    // preludes: accepting trailing tokens turns an invalid selector into a
    // silent partial match instead of the required SyntaxError.
    return pos < token_count && tokens[pos].type == CSS_TOKEN_EOF;
}

// Forward declaration
static CssValue* css_parse_token_to_value(const CssToken* token, Pool* pool);
static CssValue* css_parse_value_at(const CssToken* tokens, int* pos, int end, Pool* pool);

/**
 * Parse font-family value list with special handling for unquoted multi-word font names.
 * CSS font-family names like "Times New Roman" can be unquoted, so consecutive IDENT tokens
 * between commas should be combined into a single font family name.
 *
 * Example: font-family: Charter, Linux Libertine, Times New Roman, serif;
 * Should parse as: ["Charter", "Linux Libertine", "Times New Roman", "serif"]
 *
 * @param tokens Array of CSS tokens starting at the value
 * @param value_start Start index of values
 * @param value_end End index (exclusive)
 * @param pool Memory pool for allocation
 * @return CssValue of type LIST containing the font family names
 */
static CssValue* css_parse_font_family_values(const CssToken* tokens, int value_start, int value_end, Pool* pool) {
    if (!css_validate_font_family_tokens(tokens + value_start, value_end - value_start)) return NULL;

    // First pass: count actual font families (comma-separated groups)
    int family_count = 0;
    int i = value_start;
    bool has_value = false;

    while (i < value_end) {
        if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
            i++;
            continue;
        }
        if (tokens[i].type == CSS_TOKEN_COMMA) {
            if (has_value) {
                family_count++;
                has_value = false;
            }
            i++;
            continue;
        }
        // Any non-whitespace, non-comma token starts/continues a value
        has_value = true;
        i++;
    }
    if (has_value) family_count++;

    if (family_count == 0) return NULL;

    // Create list value
    CssValue* list_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!list_value) return NULL;

    list_value->type = CSS_VALUE_TYPE_LIST;
    list_value->data.list.count = family_count;
    list_value->data.list.comma_separated = true;
    list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * family_count);
    if (!list_value->data.list.values) return NULL;

    // Second pass: parse each font family
    int list_idx = 0;
    i = value_start;

    while (i < value_end && list_idx < family_count) {
        // Skip leading whitespace
        while (i < value_end && tokens[i].type == CSS_TOKEN_WHITESPACE) {
            i++;
        }
        if (i >= value_end) break;

        // Skip commas
        if (tokens[i].type == CSS_TOKEN_COMMA) {
            i++;
            continue;
        }

        // Handle quoted strings directly
        if (tokens[i].type == CSS_TOKEN_STRING) {
            list_value->data.list.values[list_idx++] = css_parse_token_to_value(&tokens[i], pool);
            i++;
            continue;
        }

        // For IDENT tokens, collect consecutive identifiers until comma or end
        if (tokens[i].type == CSS_TOKEN_IDENT) {
            int start_idx = i;
            int word_count = 0;
            size_t total_len = 0;

            // Collect all IDENT tokens until comma or end
            while (i < value_end) {
                if (tokens[i].type == CSS_TOKEN_IDENT) {
                    total_len += strlen(tokens[i].value);
                    word_count++;
                    i++;
                } else if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
                    // Look ahead to see if there's another IDENT after whitespace
                    int next = i + 1;
                    while (next < value_end && tokens[next].type == CSS_TOKEN_WHITESPACE) {
                        next++;
                    }
                    if (next < value_end && tokens[next].type == CSS_TOKEN_IDENT) {
                        // There's another word, continue collecting
                        total_len++; // for the space
                        i = next;
                    } else {
                        // No more IDENT after whitespace, stop
                        break;
                    }
                } else {
                    // Comma or other token, stop
                    break;
                }
            }

            // Create value for this font family
            if (word_count == 1) {
                // Single word - use standard parsing (handles keywords like serif)
                list_value->data.list.values[list_idx++] = css_parse_token_to_value(&tokens[start_idx], pool);
            } else {
                // Multiple words - combine them
                char* combined = (char*)pool_alloc(pool, total_len + word_count);
                if (combined) {
                    combined[0] = '\0';
                    size_t combined_len = 0;
                    int j = start_idx;
                    bool first = true;
                    while (j < i) {
                        if (tokens[j].type == CSS_TOKEN_IDENT) {
                            if (!first) {
                                combined_len = str_cat(combined, combined_len, total_len + word_count, " ", 1);
                            }
                            combined_len = str_cat(combined, combined_len, total_len + word_count, tokens[j].value, strlen(tokens[j].value));
                            first = false;
                        }
                        j++;
                    }

                    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
                    if (value) {
                        value->type = CSS_VALUE_TYPE_CUSTOM;
                        value->data.custom_property.name = combined;
                        value->data.custom_property.fallback = NULL;
                        list_value->data.list.values[list_idx++] = value;
                    }
                }
            }
            continue;
        }

        // Other token types - parse normally
        list_value->data.list.values[list_idx++] = css_parse_token_to_value(&tokens[i], pool);
        i++;
    }

    // Update actual count in case it differs
    list_value->data.list.count = list_idx;

    return list_value;
}

static bool css_font_shorthand_is_slash(const CssValue* value) {
    return value && value->type == CSS_VALUE_TYPE_CUSTOM &&
        value->data.custom_property.name &&
        strcmp(value->data.custom_property.name, "/") == 0;
}

static bool css_font_shorthand_is_valid_line_height(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH ||
        value->type == CSS_VALUE_TYPE_PERCENTAGE ||
        value->type == CSS_VALUE_TYPE_NUMBER ||
        value->type == CSS_VALUE_TYPE_FUNCTION ||
        value->type == CSS_VALUE_TYPE_VAR ||
        value->type == CSS_VALUE_TYPE_ENV ||
        value->type == CSS_VALUE_TYPE_CALC) {
        return true;
    }
    return value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_NORMAL ||
         value->data.keyword == CSS_VALUE_INHERIT);
}

static bool css_font_shorthand_is_family_value(const CssValue* value) {
    if (!value || css_font_shorthand_is_slash(value)) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return !info || info->group != CSS_VALUE_GROUP_GLOBAL;
    }
    return value->type == CSS_VALUE_TYPE_CUSTOM ||
        value->type == CSS_VALUE_TYPE_STRING ||
        value->type == CSS_VALUE_TYPE_FUNCTION ||
        value->type == CSS_VALUE_TYPE_VAR ||
        value->type == CSS_VALUE_TYPE_ENV;
}

static bool css_font_shorthand_has_family(const CssValue* group, size_t start) {
    if (!group || group->type != CSS_VALUE_TYPE_LIST ||
        start >= (size_t)group->data.list.count) {
        return false;
    }
    for (size_t i = start; i < (size_t)group->data.list.count; i++) {
        if (!css_font_shorthand_is_family_value(group->data.list.values[i])) return false;
    }
    return true;
}

bool css_parse_font_shorthand(const CssValue* value, CssFontShorthandParts* parts) {
    if (!parts) return false;
    *parts = {nullptr, nullptr, nullptr, nullptr, nullptr, 0, false};
    if (!value || value->type != CSS_VALUE_TYPE_LIST || value->data.list.count < 2) {
        return false;
    }
    const CssValue* group = value;
    if (value->data.list.values[0] &&
        value->data.list.values[0]->type == CSS_VALUE_TYPE_LIST) {
        group = value->data.list.values[0];
    }
    size_t count = (size_t)group->data.list.count;
    if (count < 2) return false;
    parts->group = group;
    parts->family_start = count;

    for (size_t i = 0; i < count; i++) {
        const CssValue* item = group->data.list.values[i];
        if (item && item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(item->data.keyword);
            if (info && info->group == CSS_VALUE_GROUP_GLOBAL) return false;
        }
    }

    for (size_t i = 0; i < count; i++) {
        const CssValue* item = group->data.list.values[i];
        if (!item) continue;
        if ((item->type == CSS_VALUE_TYPE_LENGTH ||
             item->type == CSS_VALUE_TYPE_PERCENTAGE) && !parts->size) {
            parts->size = item;
            size_t next = i + 1;
            if (next < count && css_font_shorthand_is_slash(group->data.list.values[next])) {
                if (next + 1 >= count ||
                    !css_font_shorthand_is_valid_line_height(group->data.list.values[next + 1])) {
                    return false;
                }
                parts->line_height = group->data.list.values[next + 1];
                next += 2;
            }
            parts->family_start = next;
            break;
        }
        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(item->data.keyword);
            if (!info) continue;
            if (info->group == CSS_VALUE_GROUP_FONT_WEIGHT) {
                parts->weight = item;
            } else if (info->group == CSS_VALUE_GROUP_FONT_STYLE) {
                parts->style = item;
            } else if (item->data.keyword == CSS_VALUE_SMALL_CAPS) {
                parts->small_caps = true;
            } else if (info->group == CSS_VALUE_GROUP_FONT_SIZE && !parts->size) {
                parts->size = item;
                size_t next = i + 1;
                if (next < count && css_font_shorthand_is_slash(group->data.list.values[next])) {
                    if (next + 1 >= count ||
                        !css_font_shorthand_is_valid_line_height(group->data.list.values[next + 1])) {
                        return false;
                    }
                    parts->line_height = group->data.list.values[next + 1];
                    next += 2;
                }
                parts->family_start = next;
                break;
            }
        } else if (item->type == CSS_VALUE_TYPE_NUMBER && !parts->weight) {
            int weight = (int)item->data.number.value; // INT_CAST_OK: CSS numeric weight.
            if (weight >= 1 && weight <= 1000) parts->weight = item;
        }
    }

    // `font` needs a family after its size; sharing this check keeps invalid
    // declarations out of both the cascade and intrinsic font measurement.
    if (!parts->size || !css_font_shorthand_has_family(group, parts->family_start)) {
        return false;
    }
    if (value != group) {
        for (size_t i = 1; i < (size_t)value->data.list.count; i++) {
            const CssValue* family = value->data.list.values[i];
            if (family && family->type == CSS_VALUE_TYPE_LIST) {
                if (!css_font_shorthand_has_family(family, 0)) return false;
            } else if (!css_font_shorthand_is_family_value(family)) {
                return false;
            }
        }
    }
    return true;
}

// Helper: Parse a CSS function with its arguments from tokens
// Returns a CssValue of type CSS_VALUE_TYPE_FUNCTION
// *pos should point to the CSS_TOKEN_FUNCTION token; on return, *pos points past the closing paren
// CSS function arguments are comma-separated; each argument may contain multiple space-separated tokens
static CssValue* css_parse_function_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    // depth guard for nested functions (calc(calc(...))); thread_local keeps it
    // reentrant-safe without threading a counter through the signature.
    static thread_local int css_func_depth = 0;
    lam::RecursionGuard depth_guard(&css_func_depth, MAX_CSS_FUNC_DEPTH);
    if (!depth_guard) {
        log_error("css: function nesting too deep (>%d) — aborting parse", MAX_CSS_FUNC_DEPTH);
        return NULL;
    }
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;
    if (tokens[*pos].type != CSS_TOKEN_FUNCTION) return NULL;

    // Get function name (strip trailing '(' if present)
    const char* func_name = tokens[*pos].value;
    if (!func_name && tokens[*pos].start && tokens[*pos].length > 0) {
        char* name_buf = pool_dup_n(pool, tokens[*pos].start, tokens[*pos].length);
        if (name_buf) {
            func_name = name_buf;
        }
    }

    // Strip trailing '(' from function name if present
    if (func_name) {
        size_t func_len = strlen(func_name);
        if (func_len > 0 && func_name[func_len - 1] == '(') {
            char* clean_name = pool_dup_n(pool, func_name, func_len - 1);
            if (clean_name) {
                func_name = clean_name;
            }
        }
    }
    // CSS function names are ASCII-insensitive; normalize before dispatch and validation.
    char* canonical_name = pool_strdup(pool, func_name ? func_name : "");
    if (!canonical_name) return NULL;
    str_lower_inplace(canonical_name, strlen(canonical_name));
    func_name = canonical_name;

    (*pos)++;  // Skip FUNCTION token

    CssValue* func_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!func_value) return NULL;

    func_value->type = CSS_VALUE_TYPE_FUNCTION;
    func_value->data.function = (CssFunction*)pool_calloc(pool, sizeof(CssFunction));
    if (!func_value->data.function) return NULL;

    if (func_name && strcmp(func_name, "url") == 0) {
        int url_start_pos = *pos;
        int url_end_pos = *pos;
        int url_paren_depth = 1;
        while (url_end_pos < token_count && url_paren_depth > 0) {
            CssTokenType t = tokens[url_end_pos].type;
            if (t == CSS_TOKEN_EOF) break;
            if (t == CSS_TOKEN_LEFT_PAREN || t == CSS_TOKEN_FUNCTION) {
                url_paren_depth++;
            } else if (t == CSS_TOKEN_RIGHT_PAREN) {
                url_paren_depth--;
                if (url_paren_depth == 0) break;
            }
            url_end_pos++;
        }

        while (url_start_pos < url_end_pos && tokens[url_start_pos].type == CSS_TOKEN_WHITESPACE) {
            url_start_pos++;
        }
        int last_token_pos = url_end_pos - 1;
        while (last_token_pos >= url_start_pos && tokens[last_token_pos].type == CSS_TOKEN_WHITESPACE) {
            last_token_pos--;
        }

        CssValue* url_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
        if (!url_value) return NULL;
        url_value->type = CSS_VALUE_TYPE_URL;

        if (last_token_pos >= url_start_pos && tokens[url_start_pos].start && tokens[last_token_pos].start) {
            const char* raw_start = tokens[url_start_pos].start;
            const char* raw_end = tokens[last_token_pos].start + tokens[last_token_pos].length;
            while (raw_start < raw_end && (*raw_start == ' ' || *raw_start == '\t' || *raw_start == '\n' || *raw_start == '\r' || *raw_start == '\f')) raw_start++;
            while (raw_end > raw_start && (raw_end[-1] == ' ' || raw_end[-1] == '\t' || raw_end[-1] == '\n' || raw_end[-1] == '\r' || raw_end[-1] == '\f')) raw_end--;
            if (raw_end - raw_start >= 2 && ((*raw_start == '"' && raw_end[-1] == '"') || (*raw_start == '\'' && raw_end[-1] == '\''))) {
                raw_start++;
                raw_end--;
            }
            size_t url_len = raw_end > raw_start ? (size_t)(raw_end - raw_start) : 0;
            url_value->data.url = css_parser_unescape_url_component(raw_start, url_len, pool);
            if (!url_value->data.url) url_value->data.url = "";
        } else {
            url_value->data.url = "";
        }
        *pos = url_end_pos;
        if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
            (*pos)++;
        }
        return url_value;
    }

    // Count arguments by counting top-level commas + 1 (or 0 if empty)
    int arg_count = 0;
    int paren_depth = 1;
    int temp_pos = *pos;
    bool has_content = false;

    while (temp_pos < token_count && paren_depth > 0) {
        CssTokenType t = tokens[temp_pos].type;
        if (t == CSS_TOKEN_EOF) break;
        if (t == CSS_TOKEN_LEFT_PAREN || t == CSS_TOKEN_FUNCTION) {
            paren_depth++;
            has_content = true;
        } else if (t == CSS_TOKEN_RIGHT_PAREN) {
            paren_depth--;
            if (paren_depth == 0) break;
        } else if (paren_depth == 1 && t == CSS_TOKEN_COMMA) {
            arg_count++;
        } else if (t != CSS_TOKEN_WHITESPACE) {
            has_content = true;
        }
        temp_pos++;
    }

    // If we have content, we have at least one argument (arg_count is the number of commas)
    if (has_content) {
        arg_count++;  // commas + 1 = number of arguments
    }

    func_value->data.function->name = func_name;
    func_value->data.function->arg_count = arg_count;

    if (arg_count > 0) {
        func_value->data.function->args = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * arg_count);
        if (!func_value->data.function->args) return NULL;
    }

    // Now parse the actual arguments - each argument spans until comma or closing paren
    paren_depth = 1;
    int arg_idx = 0;

    while (*pos < token_count && paren_depth > 0 && arg_idx < arg_count) {
        // Skip leading whitespace
        while (*pos < token_count && tokens[*pos].type == CSS_TOKEN_WHITESPACE) {
            (*pos)++;
        }

        if (*pos >= token_count) break;

        CssTokenType t = tokens[*pos].type;

        if (t == CSS_TOKEN_RIGHT_PAREN) {
            paren_depth--;
            if (paren_depth == 0) break;
            (*pos)++;
            continue;
        }

        // Collect tokens for this argument until we hit a top-level comma or closing paren
        int arg_token_start = *pos;
        int arg_token_count = 0;
        int inner_paren = 0;

        while (*pos < token_count) {
            CssTokenType ct = tokens[*pos].type;

            if (ct == CSS_TOKEN_EOF) break;
            if (ct == CSS_TOKEN_LEFT_PAREN || ct == CSS_TOKEN_FUNCTION) {
                inner_paren++;
                arg_token_count++;
                (*pos)++;
            } else if (ct == CSS_TOKEN_RIGHT_PAREN) {
                if (inner_paren > 0) {
                    inner_paren--;
                    arg_token_count++;
                    (*pos)++;
                } else {
                    // End of function - don't consume this token
                    break;
                }
            } else if (ct == CSS_TOKEN_COMMA && inner_paren == 0) {
                // End of this argument
                (*pos)++;  // consume the comma
                break;
            } else {
                arg_token_count++;
                (*pos)++;
            }
        }

        // Now create a value for this argument
        // If single token, create simple value; if multiple tokens, create a list value
        if (arg_token_count == 0) {
            // Empty argument - skip
            continue;
        }

        // Count non-whitespace tokens in this argument
        int value_count = 0;
        for (int i = arg_token_start; i < arg_token_start + arg_token_count; i++) {
            CssTokenType vt = tokens[i].type;
            if (vt != CSS_TOKEN_WHITESPACE) {
                // Functions count as one value but span multiple tokens
                if (vt == CSS_TOKEN_FUNCTION) {
                    value_count++;
                    int nest = 1;
                    i++;
                    while (i < arg_token_start + arg_token_count && nest > 0) {
                        if (tokens[i].type == CSS_TOKEN_LEFT_PAREN || tokens[i].type == CSS_TOKEN_FUNCTION) nest++;
                        else if (tokens[i].type == CSS_TOKEN_RIGHT_PAREN) nest--;
                        i++;
                    }
                    i--;  // will be incremented by for loop
                } else {
                    value_count++;
                }
            }
        }

        if (value_count == 1) {
            // Single value - find and parse it
            for (int i = arg_token_start; i < arg_token_start + arg_token_count; i++) {
                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;

                if (tokens[i].type == CSS_TOKEN_FUNCTION) {
                    int func_pos = i;
                    func_value->data.function->args[arg_idx++] = css_parse_function_from_tokens(tokens, &func_pos, token_count, pool);
                } else {
                    func_value->data.function->args[arg_idx++] = css_parse_token_to_value(&tokens[i], pool);
                }
                break;
            }
        } else if (value_count > 1) {
            // Multiple values - create a list
            CssValue* list_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!list_value) continue;

            list_value->type = CSS_VALUE_TYPE_LIST;
            list_value->data.list.count = value_count;
            list_value->data.list.comma_separated = false;
            list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * value_count);
            if (!list_value->data.list.values) continue;

            int list_idx = 0;
            for (int i = arg_token_start; i < arg_token_start + arg_token_count && list_idx < value_count; i++) {
                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;

                if (tokens[i].type == CSS_TOKEN_FUNCTION) {
                    int func_pos = i;
                    CssValue* nested = css_parse_function_from_tokens(tokens, &func_pos, token_count, pool);
                    if (nested) {
                        list_value->data.list.values[list_idx++] = nested;
                    }
                    i = func_pos - 1;  // will be incremented by for loop
                } else {
                    CssValue* val = css_parse_token_to_value(&tokens[i], pool);
                    if (val) {
                        list_value->data.list.values[list_idx++] = val;
                    }
                }
            }

            func_value->data.function->args[arg_idx++] = list_value;
        }
    }

    // Skip closing paren if present
    if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
        (*pos)++;
    }

    return func_value;
}

// Helper: Parse a single token into a CssValue
static CssValue* css_parse_token_to_value(const CssToken* token, Pool* pool) {
    if (!token || !pool) return NULL;

    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!value) return NULL;

    switch (token->type) {
        case CSS_TOKEN_IDENT: {
            const char* token_val = token->value;
            if (!token_val && token->start && token->length > 0) {
                char* buf = pool_dup_n(pool, token->start, token->length);
                if (buf) {
                    token_val = buf;
                }
            }

            if (token_val) {
                CssEnum enum_id = css_enum_by_name(token_val);
                if (enum_id != CSS_VALUE__UNDEF) {
                    value->type = CSS_VALUE_TYPE_KEYWORD;
                    value->data.keyword = enum_id;
                } else {
                    value->type = CSS_VALUE_TYPE_CUSTOM;
                    value->data.custom_property.name = pool_strdup(pool, token_val);
                    value->data.custom_property.fallback = NULL;
                }
            }
            break;
        }

        case CSS_TOKEN_STRING: {
            value->type = CSS_VALUE_TYPE_STRING;
            const char* str_val = token->value;
            if (!str_val && token->start && token->length > 0) {
                char* buf = pool_dup_n(pool, token->start, token->length);
                if (buf) {
                    str_val = buf;
                }
            }
            value->data.string = str_val ? pool_strdup(pool, str_val) : "";
            break;
        }

        case CSS_TOKEN_URL: {
            value->type = CSS_VALUE_TYPE_URL;
            const char* url_val = token->value;
            if (!url_val && token->start && token->length > 0) {
                char* buf = pool_dup_n(pool, token->start, token->length);
                if (buf) {
                    url_val = buf;
                }
            }
            value->data.url = url_val ? pool_strdup(pool, url_val) : "";
            break;
        }

        case CSS_TOKEN_NUMBER:
            value->type = CSS_VALUE_TYPE_NUMBER;
            value->data.number.value = token->data.number_value;
            break;

        case CSS_TOKEN_DIMENSION:
            if (token->data.dimension.unit == CSS_UNIT_NONE) {
                // Unknown unit — preserve as CUSTOM text (used for custom properties like --foo: 1foo)
                const char* tok_val = token->value;
                if (!tok_val && token->start && token->length > 0) {
                    char* buf = (char*)pool_calloc(pool, token->length + 1);
                    if (buf) { memcpy(buf, token->start, token->length); buf[token->length] = '\0'; tok_val = buf; }
                }
                if (tok_val) {
                    value->type = CSS_VALUE_TYPE_CUSTOM;
                    value->data.custom_property.name = pool_strdup(pool, tok_val);
                    value->data.custom_property.fallback = NULL;
                } else {
                    return NULL;
                }
                break;
            }
            value->type = CSS_VALUE_TYPE_LENGTH;
            value->data.length.value = token->data.dimension.value;
            value->data.length.unit = token->data.dimension.unit;
            break;

        case CSS_TOKEN_PERCENTAGE:
            value->type = CSS_VALUE_TYPE_PERCENTAGE;
            value->data.percentage.value = token->data.number_value;
            break;

        case CSS_TOKEN_HASH: {
            value->type = CSS_VALUE_TYPE_COLOR;
            value->data.color.type = CSS_COLOR_RGB;

            const char* hex_str = token->value;
            if (!hex_str || !css_parse_hex_to_rgba(hex_str,
                    &value->data.color.data.rgba.r,
                    &value->data.color.data.rgba.g,
                    &value->data.color.data.rgba.b,
                    &value->data.color.data.rgba.a)) {
                value->data.color.data.rgba.r = 0;
                value->data.color.data.rgba.g = 0;
                value->data.color.data.rgba.b = 0;
                value->data.color.data.rgba.a = 255;
            }
            break;
        }

        case CSS_TOKEN_CUSTOM_PROPERTY: {
            // Custom property token (e.g., --my-var)
            value->type = CSS_VALUE_TYPE_CUSTOM;
            const char* token_val = token->value;
            if (!token_val && token->start && token->length > 0) {
                char* buf = pool_dup_n(pool, token->start, token->length);
                if (buf) {
                    token_val = buf;
                }
            }
            value->data.custom_property.name = token_val ? pool_strdup(pool, token_val) : "";
            value->data.custom_property.fallback = NULL;
            break;
        }

        default:
            // Unknown token type - treat as custom value
            value->type = CSS_VALUE_TYPE_CUSTOM;
            if (token->value) {
                value->data.custom_property.name = pool_strdup(pool, token->value);
            } else if (token->start && token->length > 0) {
                char* buf = pool_dup_n(pool, token->start, token->length);
                if (buf) {
                    value->data.custom_property.name = buf;
                }
            }
            value->data.custom_property.fallback = NULL;
            break;
    }

    return value;
}

static CssValue* css_parse_value_at(const CssToken* tokens, int* pos, int end, Pool* pool) {
    while (*pos < end && tokens[*pos].type == CSS_TOKEN_WHITESPACE) (*pos)++;
    if (*pos >= end) return NULL;
    if (tokens[*pos].type == CSS_TOKEN_FUNCTION) {
        return css_parse_function_from_tokens(tokens, pos, end, pool);
    }
    CssValue* value = css_parse_token_to_value(&tokens[*pos], pool);
    (*pos)++;
    return value;
}

// Helper: Parse a compound selector (e.g., "p.intro" or "div#main.content")
// A compound selector is a sequence of simple selectors with no whitespace
CssCompoundSelector* css_parse_compound_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    if (!compound) return NULL;

    // Allocate initial array for simple selectors
    size_t capacity = 4;
    compound->simple_selectors = (CssSimpleSelector**)pool_calloc(pool, capacity * sizeof(CssSimpleSelector*));
    if (!compound->simple_selectors) return NULL;
    compound->simple_selector_count = 0;

    // Parse simple selectors until we hit whitespace, combinator, comma, or brace
    bool has_pseudo_element = false;
    while (*pos < token_count) {
        const CssToken* token = &tokens[*pos];

        // EOF must terminate a pseudo-element too; the guard below rejects followers.
        if (token->type == CSS_TOKEN_EOF ||
            token->type == CSS_TOKEN_WHITESPACE ||
            token->type == CSS_TOKEN_COLUMN ||
            token->type == CSS_TOKEN_COMMA ||
            token->type == CSS_TOKEN_LEFT_BRACE ||
            token->type == CSS_TOKEN_RIGHT_BRACE) {
            break;
        }

        // Stop at combinator delimiters (>, +, ~)
        if (token->type == CSS_TOKEN_DELIM) {
            char delim = token->data.delimiter;
            if (delim == '>' || delim == '+' || delim == '~') {
                break;
            }
        }

        // Per CSS spec, a pseudo-element must be the last simple selector in a compound selector.
        // If we already saw one, reject any further simple selectors (invalidates the selector).
        if (has_pseudo_element) {
            log_debug("[CSS Parser] Rejecting selector: simple selector after pseudo-element");
            return NULL;
        }

        // Try to parse a simple selector (element, class, id, etc.)
        int start_pos = *pos;
        CssSimpleSelector* simple = css_parse_simple_selector_from_tokens(tokens, pos, token_count, pool);

        if (!simple) {
            // If we haven't parsed any selectors yet, this is an error
            if (compound->simple_selector_count == 0) {
                return NULL;
            }
            // Otherwise, we've finished the compound selector
            break;
        }

        // Track pseudo-element presence
        if (simple->type >= CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE &&
            simple->type <= CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC) {
            has_pseudo_element = true;
        }

        // Add the simple selector to the compound
        if (compound->simple_selector_count >= capacity) {
            if (!lam::pool_copy_grow_array(pool, &compound->simple_selectors,
                                            &capacity, compound->simple_selector_count,
                                            compound->simple_selector_count + 1, 4, true)) return NULL;
        }

        compound->simple_selectors[compound->simple_selector_count++] = simple;

        log_debug("[CSS Parser] Added simple selector to compound (count=%zu)", compound->simple_selector_count);

        // Check if position actually advanced
        if (*pos == start_pos) {
            // Position didn't advance, prevent infinite loop
            break;
        }
    }

    if (compound->simple_selector_count == 0) {
        return NULL;
    }

    log_debug("[CSS Parser] Parsed compound selector with %zu simple selectors", compound->simple_selector_count);
    return compound;
}

// Helper: Parse a full selector with combinators (e.g., "div p.intro" or "nav > ul li")
static const char* css_parse_attribute_value(const CssToken* tokens, int* pos,
                                             int token_count, Pool* pool) {
    if (*pos >= token_count || (tokens[*pos].type != CSS_TOKEN_STRING &&
                                tokens[*pos].type != CSS_TOKEN_IDENT)) return NULL;
    char* value = css_token_value_dup(&tokens[*pos], pool);
    if (!value) return NULL;
    if (tokens[*pos].type == CSS_TOKEN_STRING) {
        if (value && (value[0] == '"' || value[0] == '\'')) {
            size_t len = strlen(value);
            if (len >= 2) {
                char* value_buf = pool_dup_n(pool, value + 1, len - 2);
                if (value_buf) {
                    pool_free(pool, value);
                    value = value_buf;
                }
            }
        }
    }
    (*pos)++;
    return value;
}

static void css_parse_attribute_case_flag(const CssToken* tokens, int* pos,
                                          int token_count, bool* case_insensitive,
                                          bool* case_sensitive) {
    if (*pos >= token_count || tokens[*pos].type != CSS_TOKEN_IDENT) return;
    const char* flag = tokens[*pos].value;
    if (flag && (strcmp(flag, "i") == 0 || strcmp(flag, "I") == 0)) {
        *case_insensitive = true;
        (*pos)++;
    } else if (flag && (strcmp(flag, "s") == 0 || strcmp(flag, "S") == 0)) {
        *case_sensitive = true;
        (*pos)++;
    }
}

static const char* css_parse_attribute_tail(const CssToken* tokens, int* pos,
        int token_count, Pool* pool, bool* case_insensitive,
        bool* case_sensitive) {
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    const char* value = css_parse_attribute_value(tokens, pos, token_count, pool);
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    css_parse_attribute_case_flag(tokens, pos, token_count,
        case_insensitive, case_sensitive);
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_BRACKET) (*pos)++;
    return value;
}

CssSelector* css_parse_selector_with_combinators(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    CssSelector* selector = (CssSelector*)pool_calloc(pool, sizeof(CssSelector));
    if (!selector) return NULL;

    // Allocate arrays
    size_t capacity = 4;
    selector->compound_selectors = (CssCompoundSelector**)pool_calloc(pool, capacity * sizeof(CssCompoundSelector*));
    selector->combinators = (CssCombinator*)pool_calloc(pool, capacity * sizeof(CssCombinator));
    if (!selector->compound_selectors || !selector->combinators) return NULL;
    selector->compound_selector_count = 0;

    // Parse first compound selector
    CssCompoundSelector* compound = css_parse_compound_selector_from_tokens(tokens, pos, token_count, pool);
    if (!compound) return NULL;

    selector->compound_selectors[0] = compound;
    selector->compound_selector_count = 1;

    // Parse combinators and subsequent compound selectors
    while (*pos < token_count) {
        int saved_pos = *pos;

        // Check for combinators
        CssCombinator combinator = CSS_COMBINATOR_NONE;
        bool has_whitespace = false;

        // Skip whitespace and detect descendant combinator
        while (*pos < token_count && tokens[*pos].type == CSS_TOKEN_WHITESPACE) {
            has_whitespace = true;
            (*pos)++;
        }

        if (*pos >= token_count) break;

        // Check for explicit combinators (>, +, ~, ||)
        const CssToken* token = &tokens[*pos];
        if (token->type == CSS_TOKEN_COLUMN) {
            combinator = CSS_COMBINATOR_COLUMN;
            (*pos)++;
        } else if (token->type == CSS_TOKEN_DELIM) {
            char delim = token->data.delimiter;
            if (delim == '>') {
                combinator = CSS_COMBINATOR_CHILD;
                (*pos)++;
                log_debug("[CSS Parser] Found child combinator '>'");
            } else if (delim == '+') {
                combinator = CSS_COMBINATOR_NEXT_SIBLING;
                (*pos)++;
                log_debug("[CSS Parser] Found next-sibling combinator '+'");
            } else if (delim == '~') {
                combinator = CSS_COMBINATOR_SUBSEQUENT_SIBLING;
                (*pos)++;
                log_debug("[CSS Parser] Found subsequent-sibling combinator '~'");
            }
        }

        // If no explicit combinator but we had whitespace, it's a descendant combinator
        if (combinator == CSS_COMBINATOR_NONE && has_whitespace) {
            // Check if the next token could start a selector
            if (*pos < token_count) {
                const CssToken* next = &tokens[*pos];
                if (next->type == CSS_TOKEN_IDENT ||
                    (next->type == CSS_TOKEN_DELIM && (next->data.delimiter == '.' || next->data.delimiter == '*')) ||
                    next->type == CSS_TOKEN_HASH ||
                    next->type == CSS_TOKEN_LEFT_BRACKET ||
                    next->type == CSS_TOKEN_COLON) {  // pseudo-classes like :where(), :not(), :is(), :has()
                    combinator = CSS_COMBINATOR_DESCENDANT;
                    log_debug("[CSS Parser] Detected descendant combinator (whitespace)");
                }
            }
        }

        // If we found a combinator, parse the next compound selector
        if (combinator != CSS_COMBINATOR_NONE) {
            // Skip whitespace after combinator
            *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

            // Parse next compound selector
            CssCompoundSelector* next_compound = css_parse_compound_selector_from_tokens(tokens, pos, token_count, pool);
            if (!next_compound) {
                // Restore position if parsing failed
                *pos = saved_pos;
                break;
            }

            // Expand arrays if needed
            if (selector->compound_selector_count >= capacity) {
                size_t next_capacity = 0;
                if (!lam::grow_capacity(capacity, selector->compound_selector_count + 1,
                                        4, &next_capacity)) break;
                CssCompoundSelector** new_compounds = (CssCompoundSelector**)pool_calloc(pool, next_capacity * sizeof(CssCompoundSelector*));
                CssCombinator* new_combinators = (CssCombinator*)pool_calloc(pool, next_capacity * sizeof(CssCombinator));
                if (!new_compounds || !new_combinators) break;

                memcpy(new_compounds, selector->compound_selectors, selector->compound_selector_count * sizeof(CssCompoundSelector*));
                memcpy(new_combinators, selector->combinators, selector->compound_selector_count * sizeof(CssCombinator));

                selector->compound_selectors = new_compounds;
                selector->combinators = new_combinators;
                capacity = next_capacity;
            }

            // Add combinator and compound selector
            selector->combinators[selector->compound_selector_count - 1] = combinator;
            selector->compound_selectors[selector->compound_selector_count] = next_compound;
            selector->compound_selector_count++;

            log_debug("[CSS Parser] Added compound selector with combinator (total count=%zu)",
                    selector->compound_selector_count);
        } else {
            // No combinator found, restore position and stop
            *pos = saved_pos;
            break;
        }
    }

    log_debug("[CSS Parser] Completed selector with %zu compound parts", selector->compound_selector_count);
    return selector;
}

// Parse comma-separated selector group (e.g., "h1, h2, h3" or "p.intro, div.outro")
CssSelectorGroup* css_parse_selector_group_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    log_debug("[CSS Parser] Parsing selector group at position %d", *pos);

    // Initial capacity for selector array
    size_t capacity = 4;
    CssSelector** selectors = (CssSelector**)pool_calloc(pool, capacity * sizeof(CssSelector*));
    if (!selectors) return NULL;

    size_t count = 0;

    // Parse first selector
    CssSelector* first = css_parse_selector_with_combinators(tokens, pos, token_count, pool);
    if (!first) {
        log_debug("[CSS Parser] ERROR: Failed to parse first selector in group");
        return NULL;
    }
    selectors[count++] = first;
    log_debug("[CSS Parser] Parsed selector %zu in group", count);

    // Skip whitespace after selector
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Parse additional selectors separated by commas
    while (*pos < token_count && tokens[*pos].type == CSS_TOKEN_COMMA) {
        log_debug("[CSS Parser] Found comma, parsing next selector in group");
        (*pos)++; // consume comma

        // Skip whitespace after comma
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

        // Parse next selector
        CssSelector* next = css_parse_selector_with_combinators(tokens, pos, token_count, pool);
        if (!next) return NULL;

        // Expand array if needed
        if (count >= capacity) {
            if (!lam::pool_copy_grow_array(pool, &selectors, &capacity, count,
                                            count + 1, 4, true)) {
                log_debug("[CSS Parser] ERROR: Failed to expand selector array");
                return NULL;
            }
        }

        selectors[count++] = next;
        log_debug("[CSS Parser] Parsed selector %zu in group", count);

        // Skip whitespace after selector
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    }

    // Create selector group
    CssSelectorGroup* group = (CssSelectorGroup*)pool_calloc(pool, sizeof(CssSelectorGroup));
    if (!group) return NULL;

    group->selectors = selectors;
    group->selector_count = count;

    log_debug("[CSS Parser] Completed selector group with %zu selectors", count);
    return group;
}

static bool css_selector_contains_generic_pseudo(const CssSelector* selector);

static CssSelectorGroup* css_parse_segmented_selector_group(
        const CssToken* tokens, int start, int end, Pool* pool,
        bool forgiving, bool relative) {
    size_t capacity = 4;
    CssSelector** selectors = (CssSelector**)pool_calloc(pool, capacity * sizeof(CssSelector*));
    if (!selectors) return NULL;
    size_t count = 0;
    int segment_start = start;
    int depth = 0;
    for (int i = start; i <= end; i++) {
        if (i < end) {
            if (tokens[i].type == CSS_TOKEN_FUNCTION ||
                tokens[i].type == CSS_TOKEN_LEFT_PAREN ||
                tokens[i].type == CSS_TOKEN_LEFT_BRACKET) depth++;
            else if (tokens[i].type == CSS_TOKEN_RIGHT_PAREN ||
                     tokens[i].type == CSS_TOKEN_RIGHT_BRACKET) depth--;
        }
        if (i < end && !(tokens[i].type == CSS_TOKEN_COMMA && depth == 0)) continue;
        int pos = css_skip_whitespace_tokens(tokens, segment_start, i);
        CssCombinator leading = CSS_COMBINATOR_DESCENDANT;
        if (relative && pos < i && tokens[pos].type == CSS_TOKEN_DELIM) {
            char delim = tokens[pos].data.delimiter;
            if (delim == '>') leading = CSS_COMBINATOR_CHILD;
            else if (delim == '+') leading = CSS_COMBINATOR_NEXT_SIBLING;
            else if (delim == '~') leading = CSS_COMBINATOR_SUBSEQUENT_SIBLING;
            if (leading != CSS_COMBINATOR_DESCENDANT) {
                pos = css_skip_whitespace_tokens(tokens, pos + 1, i);
            }
        }
        CssSelector* candidate = pos < i
            ? css_parse_selector_with_combinators(tokens, &pos, i, pool) : NULL;
        pos = css_skip_whitespace_tokens(tokens, pos, i);
        if (candidate && pos == i && !css_selector_contains_generic_pseudo(candidate)) {
            if (count >= capacity &&
                !lam::pool_copy_grow_array(pool, &selectors, &capacity, count,
                                           count + 1, 4, true)) return NULL;
            if (relative) candidate->leading_combinator = leading;
            selectors[count++] = candidate;
        } else if (!forgiving) {
            return NULL;
        }
        segment_start = i + 1;
    }
    CssSelectorGroup* group = (CssSelectorGroup*)pool_calloc(pool, sizeof(CssSelectorGroup));
    if (!group) return NULL;
    group->selectors = selectors;
    group->selector_count = count;
    return group;
}

// Helper: Parse a simple CSS selector from tokens (simplified for now)
static bool css_apply_functional_pseudo(CssSimpleSelector* selector,
        const CssSelectorFunction* function, const CssToken* tokens,
        int token_count, Pool* pool, bool after_colon) {
    selector->type = css_functional_pseudo_type(function->name);
    if (selector->type == CSS_SELECTOR_PSEUDO_IS &&
        (strcmp(function->name, "host") == 0 ||
         strcmp(function->name, "host-context") == 0)) {
        log_debug(after_colon ? " Shadow DOM pseudo-class: ':%s()'"
                              : " Shadow DOM function: '%s()'",
            function->name);
    }
    selector->value = function->name;
    selector->argument = function->argument;

    if ((selector->type == CSS_SELECTOR_PSEUDO_LANG ||
         selector->type == CSS_SELECTOR_PSEUDO_DIR) &&
        (!selector->argument || !selector->argument[0])) return false;
    if (selector->type == CSS_SELECTOR_PSEUDO_DIR &&
        str_icmp_cstr(selector->argument, "ltr") != 0 &&
        str_icmp_cstr(selector->argument, "rtl") != 0) return false;

    if (css_is_nth_pseudo(selector->type)) {
        int formula_end = function->argument_end;
        int of_pos = -1;
        if (selector->type == CSS_SELECTOR_PSEUDO_NTH_CHILD ||
            selector->type == CSS_SELECTOR_PSEUDO_NTH_LAST_CHILD) {
            for (int i = function->argument_start + 1; i < function->argument_end; i++) {
                if (tokens[i].type == CSS_TOKEN_IDENT && tokens[i].value &&
                    str_icmp_cstr(tokens[i].value, "of") == 0 &&
                    tokens[i - 1].type == CSS_TOKEN_WHITESPACE) {
                    formula_end = i;
                    of_pos = i;
                    break;
                }
            }
        }
        if (!css_parse_anb_from_tokens(tokens, function->argument_start,
                formula_end, &selector->nth_formula)) {
            log_debug(after_colon
                ? "[CSS Parser] An+B parse error for ':%s(%s)'"
                : "[CSS Parser] An+B parse error for '%s(%s)'",
                function->name, function->argument ? function->argument : "");
            return false;
        }
        if (of_pos >= 0) {
            int sub_pos = of_pos + 1;
            CssSelectorGroup* sub_group = css_parse_selector_group_from_tokens(
                tokens, &sub_pos, function->argument_end, pool);
            if (!sub_group || sub_group->selector_count == 0 ||
                css_selector_group_contains_generic_pseudo(sub_group) ||
                css_skip_whitespace_tokens(tokens, sub_pos, function->argument_end) !=
                    function->argument_end) return false;
            selector->function_selectors = sub_group->selectors;
            selector->function_selector_count = sub_group->selector_count;
            // keep the separation around `of` for selectorText serialization.
            const char* raw_start = tokens[function->argument_start].start;
            const CssToken* last = &tokens[function->argument_end - 1];
            const char* raw_end = last->start + last->length;
            selector->argument = pool_dup_n(pool, raw_start, (size_t)(raw_end - raw_start));
        }
    } else if (selector->type == CSS_SELECTOR_PSEUDO_NOT ||
               selector->type == CSS_SELECTOR_PSEUDO_IS ||
               selector->type == CSS_SELECTOR_PSEUDO_WHERE ||
               selector->type == CSS_SELECTOR_PSEUDO_HAS ||
               selector->type == CSS_SELECTOR_PSEUDO_SLOTTED) {
        int sub_pos = function->argument_start;
        bool forgiving = selector->type == CSS_SELECTOR_PSEUDO_IS ||
                         selector->type == CSS_SELECTOR_PSEUDO_WHERE;
        CssSelectorGroup* sub_group = (forgiving || selector->type == CSS_SELECTOR_PSEUDO_HAS)
            ? css_parse_segmented_selector_group(tokens, sub_pos,
                function->argument_end, pool, forgiving,
                selector->type == CSS_SELECTOR_PSEUDO_HAS)
            : css_parse_selector_group_from_tokens(tokens, &sub_pos,
                function->argument_end, pool);
        if (!sub_group || (!forgiving &&
            (sub_group->selector_count == 0 ||
             css_selector_group_contains_generic_pseudo(sub_group) ||
             (selector->type != CSS_SELECTOR_PSEUDO_HAS &&
              css_skip_whitespace_tokens(tokens, sub_pos,
                function->argument_end) != function->argument_end)))) return false;
        if (sub_group->selector_count > 0) {
            selector->function_selectors = sub_group->selectors;
            selector->function_selector_count = sub_group->selector_count;
        }
    }

    if (after_colon) {
        log_debug(" Functional pseudo-class after colon: ':%s(%s)'",
            function->name, function->argument ? function->argument : "");
    } else {
        log_debug(" Functional pseudo-class: '%s(%s)'",
            function->name, function->argument ? function->argument : "");
    }
    return true;
}

static bool css_parse_qualified_selector_name(const CssToken* tokens, int* pos,
                                              int token_count, Pool* pool,
                                              bool allow_universal_local,
                                              const char** prefix,
                                              const char** local) {
    int start = *pos;
    bool empty_prefix = tokens[start].type == CSS_TOKEN_DELIM &&
        tokens[start].data.delimiter == '|';
    bool named_or_wildcard = tokens[start].type == CSS_TOKEN_IDENT ||
        (tokens[start].type == CSS_TOKEN_DELIM &&
         tokens[start].data.delimiter == '*');
    int local_index = start + (empty_prefix ? 1 : 2);
    if (!empty_prefix && (!named_or_wildcard || start + 1 >= token_count ||
        tokens[start + 1].type != CSS_TOKEN_DELIM ||
        tokens[start + 1].data.delimiter != '|')) return false;
    if (local_index >= token_count ||
        (tokens[local_index].type != CSS_TOKEN_IDENT &&
         !(allow_universal_local && tokens[local_index].type == CSS_TOKEN_DELIM &&
           tokens[local_index].data.delimiter == '*'))) return false;
    *prefix = empty_prefix ? "" : tokens[start].type == CSS_TOKEN_IDENT
        ? css_token_value_dup(&tokens[start], pool) : "*";
    *local = tokens[local_index].type == CSS_TOKEN_IDENT
        ? css_token_value_dup(&tokens[local_index], pool) : "*";
    *pos = local_index + 1;
    return *prefix && *local;
}

CssSimpleSelector* css_parse_simple_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    CssSimpleSelector* selector = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
    if (!selector) return NULL;

    const CssToken* token = &tokens[*pos];

    bool matched = false;  // Track if we found a valid selector
    const char* namespace_prefix = NULL;
    const char* qualified_local = NULL;
    bool has_namespace_local = css_parse_qualified_selector_name(
        tokens, pos, token_count, pool, true,
        &namespace_prefix, &qualified_local);

    // Parse based on token type
    if (has_namespace_local) {
        selector->type = strcmp(qualified_local, "*") != 0
            ? CSS_SELECTOR_TYPE_ELEMENT : CSS_SELECTOR_TYPE_UNIVERSAL;
        selector->value = qualified_local;
        selector->namespace_prefix = namespace_prefix;
        selector->namespace_url = namespace_prefix[0] == '\0' ? "" : NULL;
        matched = true;
    } else if (token->type == CSS_TOKEN_IDENT || token->type == CSS_TOKEN_CUSTOM_PROPERTY) {
        // Element/type selector: div, span, --foo, etc.
        selector->type = CSS_SELECTOR_TYPE_ELEMENT;
        // Extract selector value from token
        if (token->value) {
            selector->value = pool_strdup(pool, token->value);
        } else if (token->start && token->length > 0) {
            char* value_buf = pool_dup_n(pool, token->start, token->length);
            if (value_buf) {
                selector->value = value_buf;
            }
        }
        log_debug("[CSS Parser] Element selector: '%s'", selector->value ? selector->value : "(null)");
        (*pos)++;
        matched = true;
    } else if (token->type == CSS_TOKEN_DELIM && token->data.delimiter == '.') {
        // Class selector: .classname
        (*pos)++;
        if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_IDENT) {
            selector->type = CSS_SELECTOR_TYPE_CLASS;
            const CssToken* name_token = &tokens[*pos];
            if (name_token->value) {
                selector->value = pool_strdup(pool, name_token->value);
            } else if (name_token->start && name_token->length > 0) {
                char* value_buf = pool_dup_n(pool, name_token->start, name_token->length);
                if (value_buf) {
                    selector->value = value_buf;
                }
            }
            log_debug("[CSS Parser] Class selector: '.%s'", selector->value ? selector->value : "(null)");
            (*pos)++;
            matched = true;
        } else {
            // No identifier after '.', invalid class selector
            log_debug("[CSS Parser] ERROR: Expected identifier after '.'");
            (*pos)--;  // Back up to the '.' token
        }
    } else if (token->type == CSS_TOKEN_HASH && token->data.hash_type == CSS_HASH_ID) {
        // ID selector: #identifier (only for id-type hashes, not unrestricted like #1)
        selector->type = CSS_SELECTOR_TYPE_ID;
        // Extract ID value from token (skip the # character)
        if (token->value && token->value[0] == '#') {
            // Hash token value includes the #, skip it
            selector->value = pool_strdup(pool, token->value + 1);
        } else if (token->value) {
            selector->value = pool_strdup(pool, token->value);
        } else if (token->start && token->length > 0) {
            // Hash token includes the #, skip it
            const char* start = token->start + 1;  // Skip '#'
            size_t length = token->length - 1;
            char* value_buf = pool_dup_n(pool, start, length);
            if (value_buf) {
                selector->value = value_buf;
            }
        }
        log_debug("[CSS Parser] ID selector: '#%s'", selector->value ? selector->value : "(null)");
        (*pos)++;
        matched = true;
    } else if (token->type == CSS_TOKEN_DELIM && token->data.delimiter == '*') {
        // Universal selector: *
        selector->type = CSS_SELECTOR_TYPE_UNIVERSAL;
        selector->value = "*";
        log_debug("[CSS Parser] Universal selector: '*'");
        (*pos)++;
        matched = true;
    } else if (token->type == CSS_TOKEN_FUNCTION) {
        CssSelectorFunction function = {};
        if (!css_parse_selector_function(tokens, pos, token_count, pool, &function)) return NULL;

        matched = css_apply_functional_pseudo(selector, &function, tokens,
            token_count, pool, false);
    } else if (token->type == CSS_TOKEN_COLON) {
        // Pseudo-class selector: :hover, :nth-child(), etc.
        (*pos)++;
        if (*pos < token_count) {
            const CssToken* pseudo_token = &tokens[*pos];

            // Check for pseudo-element (double colon ::before, ::after)
            if (pseudo_token->type == CSS_TOKEN_COLON) {
                // This is a pseudo-element
                (*pos)++;
                if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_IDENT) {
                    char* elem_name = css_token_value_dup(&tokens[*pos], pool);
                    if (!elem_name) return NULL;
                    str_lower_inplace(elem_name, strlen(elem_name));

                    (*pos)++;

                    // Map pseudo-element name to type
                    if (strcmp(elem_name, "before") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE;
                    } else if (strcmp(elem_name, "after") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_AFTER;
                    } else if (strcmp(elem_name, "first-line") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE;
                    } else if (strcmp(elem_name, "first-letter") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER;
                    } else if (strcmp(elem_name, "selection") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_SELECTION;
                    } else if (strcmp(elem_name, "backdrop") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BACKDROP;
                    } else if (strcmp(elem_name, "placeholder") == 0 ||
                               strcmp(elem_name, "-webkit-input-placeholder") == 0 ||
                               strcmp(elem_name, "-moz-placeholder") == 0 ||
                               strcmp(elem_name, "-ms-input-placeholder") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER;
                    } else if (strcmp(elem_name, "marker") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_MARKER;
                    } else if (strcmp(elem_name, "file-selector-button") == 0) {
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON;
                    } else {
                        // Use generic pseudo-element type to preserve vendor-specific elements
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC;
                        log_debug(" Generic pseudo-element: '::%s'", elem_name);
                    }

                    selector->value = elem_name;
                    selector->argument = NULL;
                    log_debug(" Pseudo-element: '::%s'", elem_name);
                    matched = true;
                } else if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_FUNCTION) {
                    CssSelectorFunction function = {};
                    if (!css_parse_selector_function(tokens, pos, token_count, pool, &function)) {
                        return NULL;
                    }
                    if (strcmp(function.name, "slotted") == 0) {
                        matched = css_apply_functional_pseudo(selector, &function, tokens,
                            token_count, pool, false);
                    } else {
                        // Functional pseudo-elements are valid selector-list members even when not rendered.
                        selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC;
                        selector->value = function.name;
                        selector->argument = function.argument;
                        log_debug(" Functional pseudo-element: '::%s(%s)'", function.name,
                            function.argument ? function.argument : "");
                        matched = true;
                    }
                }
                if (!matched) {
                    return NULL;
                }
            }

            // Single colon - pseudo-class or legacy pseudo-element
            else if (pseudo_token->type == CSS_TOKEN_IDENT) {
                char* pseudo_name = css_token_value_dup(pseudo_token, pool);
                if (!pseudo_name) return NULL;
                str_lower_inplace(pseudo_name, strlen(pseudo_name));

                (*pos)++;

                // Handle legacy pseudo-elements with single colon (CSS2.1 backward compatibility)
                // :before, :after, :first-line, :first-letter should be treated as pseudo-elements
                if (strcmp(pseudo_name, "before") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE;
                    selector->value = pseudo_name;
                    selector->argument = NULL;
                    log_debug(" Legacy pseudo-element: ':%s' (treated as ::%s)", pseudo_name, pseudo_name);
                    matched = true;
                } else if (strcmp(pseudo_name, "after") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_AFTER;
                    selector->value = pseudo_name;
                    selector->argument = NULL;
                    log_debug(" Legacy pseudo-element: ':%s' (treated as ::%s)", pseudo_name, pseudo_name);
                    matched = true;
                } else if (strcmp(pseudo_name, "first-line") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LINE;
                    selector->value = pseudo_name;
                    selector->argument = NULL;
                    log_debug(" Legacy pseudo-element: ':%s' (treated as ::%s)", pseudo_name, pseudo_name);
                    matched = true;
                } else if (strcmp(pseudo_name, "first-letter") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_FIRST_LETTER;
                    selector->value = pseudo_name;
                    selector->argument = NULL;
                    log_debug(" Legacy pseudo-element: ':%s' (treated as ::%s)", pseudo_name, pseudo_name);
                    matched = true;
                } else if (strcmp(pseudo_name, "-ms-input-placeholder") == 0 ||
                           strcmp(pseudo_name, "-moz-placeholder") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ELEMENT_PLACEHOLDER;
                    selector->value = pseudo_name;
                    selector->argument = NULL;
                    log_debug(" Legacy placeholder pseudo-element: ':%s'", pseudo_name);
                    matched = true;
                }

                // Only process as pseudo-class if not already matched as legacy pseudo-element
                if (!matched) {
                    // Map pseudo-class name to type (comprehensive list)
                    if (strcmp(pseudo_name, "first-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FIRST_CHILD;
                } else if (strcmp(pseudo_name, "last-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LAST_CHILD;
                } else if (strcmp(pseudo_name, "only-child") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ONLY_CHILD;
                } else if (strcmp(pseudo_name, "first-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FIRST_OF_TYPE;
                } else if (strcmp(pseudo_name, "last-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LAST_OF_TYPE;
                } else if (strcmp(pseudo_name, "only-of-type") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ONLY_OF_TYPE;
                } else if (strcmp(pseudo_name, "root") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ROOT;
                } else if (strcmp(pseudo_name, "empty") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_EMPTY;
                } else if (strcmp(pseudo_name, "hover") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_HOVER;
                } else if (strcmp(pseudo_name, "active") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ACTIVE;
                } else if (strcmp(pseudo_name, "focus") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS;
                } else if (strcmp(pseudo_name, "focus-visible") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS_VISIBLE;
                } else if (strcmp(pseudo_name, "focus-within") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FOCUS_WITHIN;
                } else if (strcmp(pseudo_name, "visited") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_VISITED;
                } else if (strcmp(pseudo_name, "link") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LINK;
                } else if (strcmp(pseudo_name, "any-link") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ANY_LINK;
                } else if (strcmp(pseudo_name, "local-link") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_LOCAL_LINK;
                } else if (strcmp(pseudo_name, "enabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ENABLED;
                } else if (strcmp(pseudo_name, "disabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DISABLED;
                } else if (strcmp(pseudo_name, "checked") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_CHECKED;
                } else if (strcmp(pseudo_name, "selected") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_SELECTED;
                } else if (strcmp(pseudo_name, "indeterminate") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INDETERMINATE;
                } else if (strcmp(pseudo_name, "valid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_VALID;
                } else if (strcmp(pseudo_name, "invalid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INVALID;
                } else if (strcmp(pseudo_name, "user-invalid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_USER_INVALID;
                } else if (strcmp(pseudo_name, "user-valid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_USER_VALID;
                } else if (strcmp(pseudo_name, "open") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OPEN;
                } else if (strcmp(pseudo_name, "required") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_REQUIRED;
                } else if (strcmp(pseudo_name, "optional") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OPTIONAL;
                } else if (strcmp(pseudo_name, "read-only") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_READ_ONLY;
                } else if (strcmp(pseudo_name, "read-write") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_READ_WRITE;
                } else if (strcmp(pseudo_name, "placeholder-shown") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_PLACEHOLDER_SHOWN;
                } else if (strcmp(pseudo_name, "default") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DEFAULT;
                } else if (strcmp(pseudo_name, "defined") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DEFINED;
                } else if (strcmp(pseudo_name, "in-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_IN_RANGE;
                } else if (strcmp(pseudo_name, "out-of-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OUT_OF_RANGE;
                } else if (strcmp(pseudo_name, "modal") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_MODAL;
                } else if (strcmp(pseudo_name, "popover-open") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_POPOVER_OPEN;
                } else if (strcmp(pseudo_name, "target") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_TARGET;
                } else if (strcmp(pseudo_name, "scope") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_SCOPE;
                } else if (strcmp(pseudo_name, "fullscreen") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FULLSCREEN;
                } else {
                    // preserve unknown names so DOM selector APIs can reject them
                    // and jQuery can fall back to its Sizzle extension handling.
                    selector->type = CSS_SELECTOR_PSEUDO_GENERIC;
                    log_debug(" Generic pseudo-class: ':%s'", pseudo_name);
                }

                    selector->value = pseudo_name;
                    selector->argument = NULL;

                    log_debug(" Simple pseudo-class: ':%s'", pseudo_name);
                    matched = true;
                } // end if (!matched)

            } else if (pseudo_token->type == CSS_TOKEN_FUNCTION) {
                // Functional pseudo-class after colon: :nth-child(...)
                CssSelectorFunction function = {};
                if (!css_parse_selector_function(tokens, pos, token_count, pool, &function)) return NULL;

                matched = css_apply_functional_pseudo(selector, &function, tokens,
                    token_count, pool, true);
            }
        }
    } else if (token->type == CSS_TOKEN_LEFT_BRACKET) {
        // Attribute selector: [attr], [attr=value], [attr~=value], etc.
        (*pos)++; // skip '['

        // Skip whitespace
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
        if (*pos >= token_count) return NULL;

        // Attribute names do not use the default namespace.
        const char* attr_prefix = NULL;
        const char* attr_name = NULL;
        bool qualified = css_parse_qualified_selector_name(
            tokens, pos, token_count, pool, false, &attr_prefix, &attr_name);
        if (!qualified && tokens[*pos].type != CSS_TOKEN_IDENT) {
            log_debug("[CSS Parser] Expected attribute name, got token type %d", tokens[*pos].type);
            return NULL;
        }
        if (!qualified) attr_name = css_token_value_dup(&tokens[(*pos)++], pool);
        if (!attr_name) return NULL;
        selector->namespace_prefix = qualified ? attr_prefix : "";
        selector->namespace_url = selector->namespace_prefix[0] == '\0' ? "" : NULL;

        // Skip whitespace
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
        if (*pos >= token_count) return NULL;

        // Check for operator or closing bracket
        CssSelectorType attr_type = CSS_SELECTOR_ATTR_EXISTS;
        const char* attr_value = NULL;
        bool case_insensitive = false;
        bool case_sensitive = false;

        if (tokens[*pos].type == CSS_TOKEN_RIGHT_BRACKET) {
            // Simple attribute exists selector: [attr]
            (*pos)++;
        } else if (tokens[*pos].type == CSS_TOKEN_INCLUDE_MATCH ||
                   tokens[*pos].type == CSS_TOKEN_DASH_MATCH ||
                   tokens[*pos].type == CSS_TOKEN_PREFIX_MATCH ||
                   tokens[*pos].type == CSS_TOKEN_SUFFIX_MATCH ||
                   tokens[*pos].type == CSS_TOKEN_SUBSTRING_MATCH) {
            switch (tokens[*pos].type) {
                case CSS_TOKEN_INCLUDE_MATCH: attr_type = CSS_SELECTOR_ATTR_CONTAINS; break;
                case CSS_TOKEN_DASH_MATCH: attr_type = CSS_SELECTOR_ATTR_LANG; break;
                case CSS_TOKEN_PREFIX_MATCH: attr_type = CSS_SELECTOR_ATTR_BEGINS; break;
                case CSS_TOKEN_SUFFIX_MATCH: attr_type = CSS_SELECTOR_ATTR_ENDS; break;
                case CSS_TOKEN_SUBSTRING_MATCH: attr_type = CSS_SELECTOR_ATTR_SUBSTRING; break;
                default: break;
            }
            (*pos)++;

            attr_value = css_parse_attribute_tail(tokens, pos, token_count, pool,
                &case_insensitive, &case_sensitive);
        } else if (tokens[*pos].type == CSS_TOKEN_DELIM) {
            char delim = tokens[*pos].data.delimiter;
            (*pos)++;

            // Check for operator pattern
            if (delim == '=') {
                attr_type = CSS_SELECTOR_ATTR_EXACT;
            } else if (delim == '~' || delim == '^' || delim == '$' || delim == '*' || delim == '|') {
                // These require '=' after
                if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_DELIM && tokens[*pos].data.delimiter == '=') {
                    (*pos)++;
                    switch (delim) {
                        case '~': attr_type = CSS_SELECTOR_ATTR_CONTAINS; break;
                        case '^': attr_type = CSS_SELECTOR_ATTR_BEGINS; break;
                        case '$': attr_type = CSS_SELECTOR_ATTR_ENDS; break;
                        case '*': attr_type = CSS_SELECTOR_ATTR_SUBSTRING; break;
                        case '|': attr_type = CSS_SELECTOR_ATTR_LANG; break;
                    }
                }
            }

            attr_value = css_parse_attribute_tail(tokens, pos, token_count, pool,
                &case_insensitive, &case_sensitive);
        }

        selector->type = attr_type;
        selector->attribute.name = attr_name;
        selector->attribute.value = attr_value;
        selector->attribute.case_insensitive = case_insensitive;
        selector->attribute.case_sensitive = case_sensitive;

        log_debug("[CSS Parser] Attribute selector: [%s%s%s]%s",
               attr_name,
               attr_value ? "=" : "",
               attr_value ? attr_value : "",
               case_insensitive ? " (case-insensitive)" : "");
        matched = true;
    }

    // If no valid selector was matched, return NULL
    if (!matched) {
        log_debug(" WARNING: No valid selector found at position %d (token type %d)",
                *pos, token->type);
        return NULL;
    }

    return selector;
}

// A standard property can defer validation when its entire value is one
// {} block containing var(); adjacent tokens make the declaration invalid.
static bool css_value_is_single_var_block(const CssToken* tokens, int start, int end) {
    start = css_skip_whitespace_tokens(tokens, start, end);
    while (end > start && (tokens[end - 1].type == CSS_TOKEN_WHITESPACE ||
                           tokens[end - 1].type == CSS_TOKEN_COMMENT)) end--;
    if (end - start < 3 || tokens[start].type != CSS_TOKEN_LEFT_BRACE ||
        tokens[end - 1].type != CSS_TOKEN_RIGHT_BRACE) return false;

    int depth = 0;
    bool has_var = false;
    for (int i = start; i < end; i++) {
        if (tokens[i].type == CSS_TOKEN_LEFT_BRACE) depth++;
        else if (tokens[i].type == CSS_TOKEN_RIGHT_BRACE) {
            depth--;
            if (depth < 0 || (depth == 0 && i != end - 1)) return false;
        } else if (depth > 0 && tokens[i].type == CSS_TOKEN_FUNCTION &&
                   tokens[i].length == 4 &&
                   strncmp(tokens[i].start, "var(", 4) == 0) {
            has_var = true;
        }
    }
    return depth == 0 && has_var;
}

// Helper: Parse CSS declaration from tokens
CssDeclaration* css_parse_declaration_from_tokens_mode(const CssToken* tokens,
    int* pos, int token_count, Pool* pool, bool quirks_mode) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    // Expect property name (identifier or custom property)
    if (tokens[*pos].type != CSS_TOKEN_IDENT && tokens[*pos].type != CSS_TOKEN_CUSTOM_PROPERTY) {
        log_debug("[CSS Parser] Expected IDENT or CUSTOM_PROPERTY for property, got token type %d", tokens[*pos].type);
        while (*pos < token_count &&
               tokens[*pos].type != CSS_TOKEN_SEMICOLON &&
               tokens[*pos].type != CSS_TOKEN_RIGHT_BRACE) {
            (*pos)++;
        }
        return NULL;
    }

    // Extract property name from token (use start/length since value may be NULL)
    char* property_name = css_token_value_dup(&tokens[*pos], pool);
    if (!property_name) {
        log_debug("[CSS Parser] No property name in token");
        return NULL;
    }
    // Standard property names are ASCII-insensitive; custom names retain case.
    if (!(property_name[0] == '-' && property_name[1] == '-')) {
        str_lower_inplace(property_name, strlen(property_name));
    }

    (*pos)++;

    // Skip whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Expect colon
    if (*pos >= token_count || tokens[*pos].type != CSS_TOKEN_COLON) return NULL;
    (*pos)++;

    // Skip whitespace after colon
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

    // Parse value tokens until semicolon, right brace, or end
    int value_start = *pos;
    int value_count = 0;
    bool is_important = false;
    int brace_depth = 0;  // track {}-blocks inside values
    int bracket_depth = 0;  // track []-brackets inside values
    int paren_depth_val = 0;  // track ()-parens inside values
    bool bracket_mismatch = false;  // unmatched ] or ) detected
    bool has_bad_token = false;  // CSS §5.4.5: bad-string/bad-url in value → drop declaration
    bool has_top_level_colon = false;
    bool has_brace_block = false;
    int value_end_before_important = -1;  // track end of value before !important

    while (*pos < token_count) {
        CssTokenType t = tokens[*pos].type;

        // CSS §5.4.5: bad-string or bad-url tokens make the declaration invalid
        if (t == CSS_TOKEN_BAD_STRING || t == CSS_TOKEN_BAD_URL) {
            has_bad_token = true;
        }

        // Check for !important (whitespace allowed between ! and important per CSS spec)
        if (brace_depth == 0 && t == CSS_TOKEN_DELIM && tokens[*pos].data.delimiter == '!') {
            int next = css_skip_whitespace_tokens(tokens, *pos + 1, token_count);
            if (next < token_count && tokens[next].type == CSS_TOKEN_IDENT &&
                strcmp(tokens[next].value, "important") == 0) {
                is_important = true;
                value_end_before_important = *pos;  // position of '!' — value ends before this
                *pos = next + 1;
                break;
            }
        }

        // Opening brace inside a value — track nested {}-blocks
        if (t == CSS_TOKEN_LEFT_BRACE) {
            has_brace_block = true;
            brace_depth++;
            value_count++;
            (*pos)++;
            continue;
        }

        // Closing brace: if inside a {}-block, close it; otherwise stop
        if (t == CSS_TOKEN_RIGHT_BRACE) {
            if (brace_depth > 0) {
                brace_depth--;
                value_count++;
                (*pos)++;
                continue;
            }
            break;  // end of declaration block
        }

        // Stop at semicolon or EOF (only at top level, not inside {}-blocks)
        if (brace_depth == 0 && (t == CSS_TOKEN_SEMICOLON || t == CSS_TOKEN_EOF)) {
            break;
        }

        // A colon in a nested simple block belongs to that block. Keep
        // consuming the declaration so its closing brace cannot escape into
        // the qualified-rule parser during malformed-CSS recovery.
        if (t == CSS_TOKEN_COLON) {
            if (brace_depth == 0) has_top_level_colon = true;
        }

        // Handle function tokens - skip entire function as one value
        if (t == CSS_TOKEN_FUNCTION) {
            value_count++;
            (*pos)++;  // skip function token
            // Skip function arguments until closing paren
            int paren_depth = 1;
            while (*pos < token_count && paren_depth > 0) {
                if (tokens[*pos].type == CSS_TOKEN_LEFT_PAREN || tokens[*pos].type == CSS_TOKEN_FUNCTION) {
                    paren_depth++;
                } else if (tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                    paren_depth--;
                }
                (*pos)++;
            }
            continue;
        }

        // Track bracket and paren matching for custom property validation
        if (t == CSS_TOKEN_LEFT_BRACKET) bracket_depth++;
        else if (t == CSS_TOKEN_RIGHT_BRACKET) {
            bracket_depth--;
            if (bracket_depth < 0) bracket_mismatch = true;
        }
        else if (t == CSS_TOKEN_LEFT_PAREN) paren_depth_val++;
        else if (t == CSS_TOKEN_RIGHT_PAREN) {
            paren_depth_val--;
            if (paren_depth_val < 0) bracket_mismatch = true;
        }

        // Count non-whitespace, non-comma tokens as values
        if (t != CSS_TOKEN_WHITESPACE && t != CSS_TOKEN_COMMA) {
            value_count++;
        }
        (*pos)++;
    }

    if (value_count == 0) {
        log_debug("[CSS Parser] No value tokens found");
        return NULL;
    }

    bool is_custom_prop = (property_name[0] == '-' && property_name[1] == '-');

    // CSS Syntax permits a single var-bearing block as a deferred value.
    // Other blocks remain invalid for standard properties after consumption.
    int value_end = value_end_before_important >= 0 ? value_end_before_important : *pos;
    if (!is_custom_prop && (has_top_level_colon ||
        (has_brace_block && !css_value_is_single_var_block(tokens, value_start, value_end)))) {
        log_debug("[CSS Parser] Dropping malformed declaration '%s' after consuming its value", property_name);
        return NULL;
    }

    // CSS §5.4.5: declarations containing bad-string or bad-url tokens must be dropped
    if (has_bad_token) {
        log_debug("[CSS Parser] Dropping declaration '%s': contains bad-string or bad-url token", property_name);
        return NULL;
    }

    // For custom properties, validate bracket matching
    // Per CSS spec, <declaration-value> requires balanced (), [], {} pairs
    if (is_custom_prop && bracket_mismatch) {
        log_debug("[CSS Parser] Custom property '%s' has mismatched brackets — invalid", property_name);
        return NULL;
    }

    // Create declaration
    CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
    if (!decl) return NULL;

    // Get property ID from name
    decl->property_code = css_property_code_from_name(property_name);
    decl->name_id = well_known_name_id({property_name, strlen(property_name)});

    // Store original property name (important for vendor-prefixed properties where property_code = -1)
    decl->property_name = property_name;

    // Capture raw value text from source tokens (for faithful CSSOM serialization)
    // Find last non-whitespace token in value range (excluding !important)
    {
        int val_end = (value_end_before_important >= 0) ? value_end_before_important - 1 : *pos - 1;
        while (val_end >= value_start && tokens[val_end].type == CSS_TOKEN_WHITESPACE) val_end--;
        if (val_end >= value_start && tokens[value_start].start && tokens[val_end].start) {
            const char* raw_start = tokens[value_start].start;
            const char* raw_end = tokens[val_end].start + tokens[val_end].length;
            size_t raw_len = raw_end - raw_start;
            char* raw_buf = pool_dup_n(pool, raw_start, raw_len);
            if (raw_buf) {
                decl->value_text = raw_buf;
                decl->value_text_len = raw_len;
            }
        }
    }

    // Debug: Print property name and ID for troubleshooting
    log_debug("[CSS Parser] Property: '%s' -> ID: %d, important=%d, value_count=%d",
            property_name, decl->property_code, is_important, value_count);

    decl->important = is_important;
    decl->valid = true;
    decl->ref_count = 1;

    // Special handling for font-family: combine multi-word font names
    if (decl->property_code == CSS_PROPERTY_FONT_FAMILY && value_count > 1) {
        decl->value = css_parse_font_family_values(tokens, value_start, *pos, pool);
    }
    // Create value(s) from tokens
    else if (value_count == 1) {
        // Single value - create directly
        int i = value_start;
        while (i < *pos) {
            CssValue* value = css_parse_value_at(tokens, &i, *pos, pool);
            if (value) {
                decl->value = value;
            }
            break;
        }
    } else {
        // Multiple values - create list
        // Check for top-level commas (outside functions/parentheses) indicating
        // comma-separated value groups (e.g. box-shadow, background, transition).
        // Top-level commas split values into sub-lists; commas inside functions
        // like rgba() are NOT group separators.
        bool has_top_level_comma = false;
        {
            int pd = 0;
            for (int i = value_start; i < *pos; i++) {
                CssTokenType t = tokens[i].type;
                if (t == CSS_TOKEN_FUNCTION || t == CSS_TOKEN_LEFT_PAREN) pd++;
                else if (t == CSS_TOKEN_RIGHT_PAREN) { if (pd > 0) pd--; }
                else if (t == CSS_TOKEN_COMMA && pd == 0) { has_top_level_comma = true; break; }
            }
        }

        if (has_top_level_comma) {
            // Count comma-separated groups (tracking paren depth to skip function args)
            int group_count = 1;
            {
                int pd = 0;
                for (int i = value_start; i < *pos; i++) {
                    CssTokenType t = tokens[i].type;
                    if (t == CSS_TOKEN_FUNCTION || t == CSS_TOKEN_LEFT_PAREN) pd++;
                    else if (t == CSS_TOKEN_RIGHT_PAREN) { if (pd > 0) pd--; }
                    else if (t == CSS_TOKEN_COMMA && pd == 0) group_count++;
                }
            }

            CssValue* list_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!list_value) return NULL;
            list_value->type = CSS_VALUE_TYPE_LIST;
            list_value->data.list.comma_separated = true;
            list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * group_count);
            if (!list_value->data.list.values) return NULL;

            // Parse each comma-separated group into a sub-list (or single value)
            int group_idx = 0;
            int i = value_start;
            while (i < *pos && group_idx < group_count) {
                // Find group boundaries (up to next top-level comma or end)
                int group_start = i;
                int pd = 0;
                while (i < *pos) {
                    CssTokenType t = tokens[i].type;
                    if (t == CSS_TOKEN_FUNCTION || t == CSS_TOKEN_LEFT_PAREN) pd++;
                    else if (t == CSS_TOKEN_RIGHT_PAREN) { if (pd > 0) pd--; }
                    else if (t == CSS_TOKEN_COMMA && pd == 0) break;
                    i++;
                }
                int group_end = i;
                if (i < *pos && tokens[i].type == CSS_TOKEN_COMMA) i++; // skip comma

                // Parse values within this group
                // First count values (functions count as 1)
                int gval_count = 0;
                {
                    int j = group_start, gpd = 0;
                    while (j < group_end) {
                        CssTokenType t = tokens[j].type;
                        if (t == CSS_TOKEN_WHITESPACE) { j++; continue; }
                        if (t == CSS_TOKEN_FUNCTION) {
                            gval_count++;
                            gpd = 1;
                            j++;
                            while (j < group_end && gpd > 0) {
                                if (tokens[j].type == CSS_TOKEN_FUNCTION || tokens[j].type == CSS_TOKEN_LEFT_PAREN) gpd++;
                                else if (tokens[j].type == CSS_TOKEN_RIGHT_PAREN) gpd--;
                                j++;
                            }
                        } else {
                            gval_count++;
                            j++;
                        }
                    }
                }

                if (gval_count == 0) {
                    group_idx++;
                    continue;
                } else if (gval_count == 1) {
                    // Single value in group - store directly
                    int j = group_start;
                    while (j < group_end && tokens[j].type == CSS_TOKEN_WHITESPACE) j++;
                    CssValue* value = NULL;
                    if (tokens[j].type == CSS_TOKEN_FUNCTION) {
                        int func_pos = j;
                        value = css_parse_function_from_tokens(tokens, &func_pos, group_end, pool);
                    } else {
                        value = css_parse_token_to_value(&tokens[j], pool);
                    }
                    list_value->data.list.values[group_idx] = value;
                } else {
                    // Multiple values in group - create sub-list
                    CssValue* sub_list = (CssValue*)pool_calloc(pool, sizeof(CssValue));
                    if (!sub_list) { group_idx++; continue; }
                    sub_list->type = CSS_VALUE_TYPE_LIST;
                    sub_list->data.list.comma_separated = false;
                    sub_list->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * gval_count);
                    if (!sub_list->data.list.values) { group_idx++; continue; }

                    int sub_idx = 0;
                    int j = group_start;
                    while (j < group_end && sub_idx < gval_count) {
                        if (tokens[j].type == CSS_TOKEN_WHITESPACE) { j++; continue; }
                        CssValue* value = NULL;
                        if (tokens[j].type == CSS_TOKEN_FUNCTION) {
                            int func_pos = j;
                            value = css_parse_function_from_tokens(tokens, &func_pos, group_end, pool);
                            j = func_pos;
                        } else {
                            value = css_parse_token_to_value(&tokens[j], pool);
                            j++;
                        }
                        if (value) sub_list->data.list.values[sub_idx++] = value;
                    }
                    sub_list->data.list.count = sub_idx;
                    list_value->data.list.values[group_idx] = sub_list;
                }
                group_idx++;
            }
            list_value->data.list.count = group_idx;

            // If only one group, unwrap the outer list
            if (group_idx == 1 && list_value->data.list.values[0]) {
                decl->value = list_value->data.list.values[0];
            } else {
                decl->value = list_value;
            }
        } else {
            // No top-level commas — space-separated flat list (existing behavior)
            CssValue* list_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
            if (!list_value) return NULL;

            list_value->type = CSS_VALUE_TYPE_LIST;
            list_value->data.list.count = value_count;
            list_value->data.list.comma_separated = false;

            // Allocate array of pointers to CssValue
            list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * value_count);
            if (!list_value->data.list.values) return NULL;

            int list_idx = 0;
            int i = value_start;
            while (i < *pos && list_idx < value_count) {
                CssValue* value = css_parse_value_at(tokens, &i, *pos, pool);
                if (value) {
                    list_value->data.list.values[list_idx++] = value;
                }
            }

            decl->value = list_value;
        }
    }

    // reject declaration if no valid value was parsed (e.g. unknown unit like "300x")
    // BUT: custom properties accept any token sequence, so skip this check for custom props
    if (!decl->value && !(decl->property_name && decl->property_name[0] == '-' && decl->property_name[1] == '-')) {
        log_debug("[CSS Parse] Rejecting declaration for property '%s': no valid value parsed",
                  property_name);
        return NULL;
    }

    // Debug: print the value type
    if (decl->value) {
        log_debug("[CSS Parse] Declaration for property ID %d: value type = %d",
               decl->property_code, decl->value->type);
        if (decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            log_debug("[CSS Parse]   Length value = %.2f", decl->value->data.length.value);
        }
    }

    // Validate the parsed value before returning
    if (decl->value) {
        // Check if this property disallows negative values
        bool disallow_negative = false;
        switch (decl->property_code) {
            // width, height, and their min/max variants cannot be negative
            case CSS_PROPERTY_WIDTH:
            case CSS_PROPERTY_HEIGHT:
            case CSS_PROPERTY_MIN_WIDTH:
            case CSS_PROPERTY_MIN_HEIGHT:
            case CSS_PROPERTY_MAX_WIDTH:
            case CSS_PROPERTY_MAX_HEIGHT:
            case CSS_PROPERTY_ZOOM:
            // padding properties (including shorthand) cannot be negative
            case CSS_PROPERTY_PADDING:
            case CSS_PROPERTY_PADDING_TOP:
            case CSS_PROPERTY_PADDING_RIGHT:
            case CSS_PROPERTY_PADDING_BOTTOM:
            case CSS_PROPERTY_PADDING_LEFT:
            case CSS_PROPERTY_PADDING_BLOCK:
            case CSS_PROPERTY_PADDING_BLOCK_START:
            case CSS_PROPERTY_PADDING_BLOCK_END:
            case CSS_PROPERTY_PADDING_INLINE:
            case CSS_PROPERTY_PADDING_INLINE_START:
            case CSS_PROPERTY_PADDING_INLINE_END:
            // border widths cannot be negative
            case CSS_PROPERTY_BORDER_TOP_WIDTH:
            case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
            case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
            case CSS_PROPERTY_BORDER_LEFT_WIDTH:
            case CSS_PROPERTY_BORDER_WIDTH:
            // tab-size cannot be negative (CSS Text 3 §4.2)
            case CSS_PROPERTY_TAB_SIZE:
                disallow_negative = true;
                break;

            // margins, positioning (top/right/bottom/left) CAN be negative
            default:
                disallow_negative = false;
                break;
        }

        if (disallow_negative) {
            // Helper lambda to check a single value for negative
            auto check_negative = [](const CssValue* v) -> bool {
                if (!v) return false;
                if (v->type == CSS_VALUE_TYPE_LENGTH) {
                    return v->data.length.value < 0;
                } else if (v->type == CSS_VALUE_TYPE_NUMBER) {
                    return v->data.number.value < 0;
                }
                return false;
            };

            bool has_negative = false;
            if (decl->value->type == CSS_VALUE_TYPE_LIST) {
                // Check each value in the list (for shorthand properties like padding)
                for (int i = 0; i < decl->value->data.list.count; i++) {
                    if (check_negative(decl->value->data.list.values[i])) {
                        has_negative = true;
                        break;
                    }
                }
            } else {
                has_negative = check_negative(decl->value);
            }

            if (has_negative) {
                // reject negative value for properties that don't allow it
                // per CSS spec, return NULL to prevent invalid declaration from entering cascade
                log_debug("[CSS Parse] Rejecting negative value for property ID %d",
                       decl->property_code);
                return NULL;
            }
        }

        if (decl->property_code > 0 && decl->property_code < CSS_PROPERTY_CUSTOM &&
            !css_property_validate_value_mode(decl->property_code, decl->value,
                                              quirks_mode)) {
            // A parse-time invalid value must never displace an earlier valid
            // declaration; all standard properties use the same validator.
            log_debug("[CSS Parse] Rejecting invalid value for property %d",
                decl->property_code);
            return NULL;
        }
    }

    // Validate: for standard properties, {}-blocks are only valid if the entire
    // value is wrapped in a single {} block (per CSS syntax spec / csswg-drafts#9317).
    // Custom properties (--*) allow arbitrary {}-blocks.
    if (decl->value && decl->property_code > 0) {
        // check if value contains any brace tokens
        bool has_braces = false;
        if (decl->value->type == CSS_VALUE_TYPE_LIST) {
            for (int i = 0; i < decl->value->data.list.count; i++) {
                CssValue* v = decl->value->data.list.values[i];
                if (v && v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name) {
                    if (strcmp(v->data.custom_property.name, "{") == 0 || strcmp(v->data.custom_property.name, "}") == 0) {
                        has_braces = true;
                        break;
                    }
                }
            }
        }
        if (has_braces) {
            // braces are only valid if: first item is "{" and last item is "}" (whole-value block)
            int cnt = decl->value->data.list.count;
            CssValue* first = (cnt > 0) ? decl->value->data.list.values[0] : nullptr;
            CssValue* last = (cnt > 0) ? decl->value->data.list.values[cnt - 1] : nullptr;
            bool first_is_open = first && first->type == CSS_VALUE_TYPE_CUSTOM &&
                first->data.custom_property.name && strcmp(first->data.custom_property.name, "{") == 0;
            bool last_is_close = last && last->type == CSS_VALUE_TYPE_CUSTOM &&
                last->data.custom_property.name && strcmp(last->data.custom_property.name, "}") == 0;
            if (!first_is_open || !last_is_close) {
                log_debug("[CSS Parse] Rejecting standard property '%s': {}-block not wrapping entire value",
                          property_name);
                return NULL;
            }
        }
    }

    if (decl->value && decl->property_code == CSS_PROPERTY_FONT) {
        bool allow_special_value = decl->value->type == CSS_VALUE_TYPE_VAR ||
            decl->value->type == CSS_VALUE_TYPE_ENV;
        if (decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* info = css_enum_info(decl->value->data.keyword);
            allow_special_value = info &&
                (info->group == CSS_VALUE_GROUP_GLOBAL ||
                 info->group == CSS_VALUE_GROUP_SYSTEM_FONT);
        }
        CssFontShorthandParts parts;
        if (!allow_special_value && !css_parse_font_shorthand(decl->value, &parts)) {
            log_debug("[CSS Parse] Rejecting invalid font shorthand");
            return NULL;
        }
    }

    return decl;
}

CssDeclaration* css_parse_declaration_from_tokens(const CssToken* tokens,
    int* pos, int token_count, Pool* pool) {
    return css_parse_declaration_from_tokens_mode(tokens, pos, token_count,
                                                  pool, false);
}

static CssRule* css_new_author_rule(Pool* pool) {
    CssRule* rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (rule) {
        rule->pool = pool;
        // zero-initialization otherwise mislabels author rules as UA origin.
        rule->origin = CSS_ORIGIN_AUTHOR;
    }
    return rule;
}

static bool css_parse_layer_prelude(const CssToken* tokens, int start, int end,
                                    bool statement, CssRule* rule, Pool* pool) {
    if (!rule || !pool) return false;
    int capacity = end - start + 1;
    CssLayerName* names = (CssLayerName*)pool_calloc(
        pool, (size_t)capacity * sizeof(CssLayerName));
    const char** parts = (const char**)pool_calloc(
        pool, (size_t)capacity * sizeof(const char*));
    if (!names || !parts) return false;
    int part_count = 0;
    int name_count = 0;
    int name_start = 0;
    bool expect_ident = true;
    for (int i = start; i < end; i++) {
        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
        if (expect_ident) {
            if (tokens[i].type != CSS_TOKEN_IDENT &&
                tokens[i].type != CSS_TOKEN_CUSTOM_PROPERTY) return false;
            parts[part_count++] = css_token_value_dup(&tokens[i], pool);
            if (!parts[part_count - 1]) return false;
            expect_ident = false;
        } else if (tokens[i].type == CSS_TOKEN_DELIM &&
                   tokens[i].data.delimiter == '.') {
            expect_ident = true;
        } else if (statement && tokens[i].type == CSS_TOKEN_COMMA) {
            names[name_count].parts = parts + name_start;
            names[name_count++].part_count = (size_t)(part_count - name_start);
            name_start = part_count;
            expect_ident = true;
        } else {
            return false;
        }
    }
    if (part_count == 0) return !statement;
    if (expect_ident || (!statement && name_count > 0)) return false;
    names[name_count].parts = parts + name_start;
    names[name_count++].part_count = (size_t)(part_count - name_start);
    rule->data.conditional_rule.layer_names = names;
    rule->data.conditional_rule.layer_name_count = (size_t)name_count;
    return true;
}

static int css_import_function_end(const CssToken* tokens, int start, int end) {
    int depth = 1;
    for (int i = start + 1; i < end; i++) {
        if (tokens[i].type == CSS_TOKEN_FUNCTION ||
            tokens[i].type == CSS_TOKEN_LEFT_PAREN) depth++;
        else if (tokens[i].type == CSS_TOKEN_RIGHT_PAREN && --depth == 0)
            return i;
    }
    return -1;
}

static bool css_parse_import_modifiers(const CssToken* tokens, int start,
                                        int end, CssRule* rule, Pool* pool) {
    int current = css_skip_whitespace_tokens(tokens, start, end);
    if (current < end && tokens[current].type == CSS_TOKEN_IDENT &&
        tokens[current].value &&
        str_ieq_cstr(tokens[current].value, "layer")) {
        rule->data.import_rule.has_layer = true;
        rule->data.import_rule.anonymous_layer = true;
        current = css_skip_whitespace_tokens(tokens, current + 1, end);
    } else if (current < end && tokens[current].type == CSS_TOKEN_FUNCTION &&
               tokens[current].value &&
               str_ieq_cstr(tokens[current].value, "layer(")) {
        int close = css_import_function_end(tokens, current, end);
        if (close < 0) return false;
        CssRule parsed = {};
        if (!css_parse_layer_prelude(tokens, current + 1, close,
                false, &parsed, pool) ||
            parsed.data.conditional_rule.layer_name_count != 1) return false;
        rule->data.import_rule.has_layer = true;
        rule->data.import_rule.layer_name =
            parsed.data.conditional_rule.layer_names[0];
        current = css_skip_whitespace_tokens(tokens, close + 1, end);
    }
    if (current < end && tokens[current].type == CSS_TOKEN_FUNCTION &&
        tokens[current].value &&
        str_ieq_cstr(tokens[current].value, "supports(")) {
        int close = css_import_function_end(tokens, current, end);
        if (close <= current + 1) return false;
        const char* from = tokens[current].start +
            tokens[current].length - 1;
        const char* to = tokens[close].start + tokens[close].length;
        rule->data.import_rule.supports = pool_dup_n(
            pool, from, (size_t)(to - from));
        if (!rule->data.import_rule.supports) return false;
        current = css_skip_whitespace_tokens(tokens, close + 1, end);
    }
    if (current < end) {
        const char* from = tokens[current].start;
        const char* to = tokens[end - 1].start + tokens[end - 1].length;
        rule->data.import_rule.media = pool_dup_n(
            pool, from, (size_t)(to - from));
        if (!rule->data.import_rule.media) return false;
    }
    return true;
}

// Expand each relative-selector branch against the complete parent list, so
// specificity follows :is(parent-list) without a selector cross product.
static char* css_expand_nested_selector(const CssToken* tokens, int start, int end,
                                         const char* parent_text, Pool* pool,
                                         bool implicit_parent,
                                         char** out_authored) {
    if (!tokens || start >= end || !parent_text || !pool) return NULL;
    StrBuf* expanded = strbuf_new();
    StrBuf* authored = out_authored ? strbuf_new() : NULL;
    if (!expanded || (out_authored && !authored)) {
        if (expanded) strbuf_free(expanded);
        if (authored) strbuf_free(authored);
        return NULL;
    }
    int branch_start = start;
    int paren_depth = 0;
    int bracket_depth = 0;
    bool valid = true;
    for (int i = start; i <= end; i++) {
        CssTokenType type = i < end ? tokens[i].type : CSS_TOKEN_COMMA;
        if (type == CSS_TOKEN_LEFT_PAREN || type == CSS_TOKEN_FUNCTION) paren_depth++;
        else if (type == CSS_TOKEN_RIGHT_PAREN && paren_depth > 0) paren_depth--;
        else if (type == CSS_TOKEN_LEFT_BRACKET) bracket_depth++;
        else if (type == CSS_TOKEN_RIGHT_BRACKET && bracket_depth > 0) bracket_depth--;
        if (type != CSS_TOKEN_COMMA || paren_depth || bracket_depth) continue;

        int first = css_skip_whitespace_tokens(tokens, branch_start, i);
        int last = i;
        while (last > first && tokens[last - 1].type == CSS_TOKEN_WHITESPACE) last--;
        if (first >= last) { valid = false; break; }
        if (branch_start != start) {
            strbuf_append_char(expanded, ',');
            if (authored) strbuf_append_str(authored, ", ");
        }
        bool has_ampersand = false;
        for (int j = first; j < last; j++) {
            if (tokens[j].type == CSS_TOKEN_DELIM &&
                tokens[j].data.delimiter == '&') has_ampersand = true;
        }
        if (!has_ampersand && implicit_parent) {
            strbuf_append_str(expanded, ":is(");
            strbuf_append_str(expanded, parent_text);
            strbuf_append_str(expanded, ") ");
        }
        const char* cursor = tokens[first].start;
        const char* finish = tokens[last - 1].start + tokens[last - 1].length;
        if (authored) {
            if (!has_ampersand && implicit_parent)
                strbuf_append_str(authored, "& ");
            strbuf_append_str_n(authored, cursor, (size_t)(finish - cursor));
        }
        for (int j = first; j < last; j++) {
            if (tokens[j].type != CSS_TOKEN_DELIM ||
                tokens[j].data.delimiter != '&') continue;
            strbuf_append_str_n(expanded, cursor, (size_t)(tokens[j].start - cursor));
            strbuf_append_str(expanded, ":is(");
            strbuf_append_str(expanded, parent_text);
            strbuf_append_char(expanded, ')');
            cursor = tokens[j].start + tokens[j].length;
        }
        strbuf_append_str_n(expanded, cursor, (size_t)(finish - cursor));
        branch_start = i + 1;
    }
    char* result = valid && expanded->str
        ? pool_dup_n(pool, expanded->str, expanded->length) : NULL;
    if (out_authored) {
        *out_authored = valid && authored && authored->str
            ? pool_dup_n(pool, authored->str, authored->length) : NULL;
    }
    strbuf_free(expanded);
    // DOM query expansion has no authored output buffer.
    if (authored) strbuf_free(authored);
    return result;
}

static bool css_nesting_flush_declarations(Pool* pool, CssRule*** nested_rules,
                                            int* nested_count, int* nested_capacity,
                                            CssDeclaration*** pending,
                                            int* pending_count, int* pending_capacity,
                                            CssSelectorGroup* parent_group) {
    if (!pending_count || *pending_count == 0) return true;
    CssRule* rule = css_new_author_rule(pool);
    if (!rule) return false;
    rule->type = CSS_RULE_NESTED_DECLARATIONS;
    rule->data.style_rule.declarations = *pending;
    rule->data.style_rule.declaration_count = *pending_count;
    rule->data.style_rule.selector_group = parent_group;
    rule->data.style_rule.selector = parent_group && parent_group->selector_count
        ? parent_group->selectors[0] : NULL;
    if (*nested_count >= *nested_capacity &&
        !lam::pool_copy_grow_array(pool, nested_rules, nested_capacity,
                                   *nested_count, *nested_count + 1, 4, true)) return false;
    (*nested_rules)[(*nested_count)++] = rule;
    *pending = NULL;
    *pending_count = 0;
    *pending_capacity = 0;
    return true;
}

static int css_parse_nested_style_rule(const CssToken* tokens, int token_count,
                                       Pool* pool, const char* parent_text,
                                       CssRule** out_rule, bool implicit_parent,
                                       bool quirks_mode);

// Enhanced rule parsing from tokens (returns number of tokens consumed, or 0 on error)
static int css_parse_rule_from_tokens_with_context(const CssToken* tokens,
    int token_count, Pool* pool, CssRule** out_rule,
    const char* nesting_parent_text, CssSelectorGroup* nesting_parent_group,
    bool quirks_mode) {
    if (!tokens || token_count <= 0 || !pool || !out_rule) return 0;
    static thread_local int css_rule_depth = 0;
    lam::RecursionGuard depth_guard(&css_rule_depth, MAX_CSS_RULE_DEPTH);
    if (!depth_guard) return 0;

    log_debug(" Parsing rule from %d tokens", token_count);

    int pos = 0;
    int start_pos = 0;

    // Skip leading whitespace and comments
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    if (pos >= token_count) {
        log_debug(" No tokens after whitespace skip");
        return 0;
    }

    start_pos = pos;

    // Check for @-rules
    if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
        const char* at_keyword = tokens[pos].value;
        log_debug(" Parsing @-rule: %s", at_keyword ? at_keyword : "(null)");
        pos++; // consume @keyword token

        // Skip leading '@' in keyword name if present
        const char* keyword_start = at_keyword && at_keyword[0] == '@'
            ? at_keyword + 1 : at_keyword;
        char* keyword_name = keyword_start ? pool_strdup(pool, keyword_start) : NULL;
        if (keyword_name) str_lower_inplace(keyword_name, strlen(keyword_name));

        // Create rule structure
        CssRule* rule = css_new_author_rule(pool);
        if (!rule) {
            log_debug(" ERROR: Failed to allocate rule");
            return 0;
        }

        // Determine rule type and parse accordingly
        if (keyword_name && (strcmp(keyword_name, "media") == 0 ||
                          strcmp(keyword_name, "supports") == 0 ||
                          strcmp(keyword_name, "container") == 0 ||
                          strcmp(keyword_name, "layer") == 0)) {
            // Nested at-rules retain their rules for the cascade. @layer's
            // prelude is its layer name rather than a boolean condition.
            rule->type = strcmp(keyword_name, "media") == 0 ? CSS_RULE_MEDIA :
                        strcmp(keyword_name, "supports") == 0 ? CSS_RULE_SUPPORTS :
                        strcmp(keyword_name, "container") == 0 ? CSS_RULE_CONTAINER :
                        CSS_RULE_LAYER;
            int cond_start = pos;
            while (pos < token_count &&
                   tokens[pos].type != CSS_TOKEN_LEFT_BRACE &&
                   tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }

            // Preserve source spacing: range operators such as >= are two tokens,
            // and inserting spaces between them changes the media query grammar.
            if (pos > cond_start) {
                const char* begin = tokens[cond_start].start;
                const char* last = tokens[pos - 1].start;
                if (begin && last && last >= begin) {
                    char* condition = pool_dup_n(pool, begin,
                        (size_t)(last + tokens[pos - 1].length - begin));
                    if (condition) {
                        // CSS comments are whitespace, including between range-operator tokens.
                        for (char* cursor = condition; cursor[0] && cursor[1]; cursor++) {
                            if (cursor[0] != '/' || cursor[1] != '*') continue;
                            cursor[0] = cursor[1] = ' ';
                            cursor += 2;
                            while (cursor[0] && !(cursor[0] == '*' && cursor[1] == '/'))
                                *cursor++ = ' ';
                            if (!cursor[0]) break;
                            cursor[0] = cursor[1] = ' ';
                        }
                        rule->data.conditional_rule.condition = condition;
                    }
                }
            }

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                // A layer order statement ends here; scanning for the next
                // brace would swallow the following style rule.
                rule->data.conditional_rule.layer_statement =
                    rule->type == CSS_RULE_LAYER;
                if (rule->type == CSS_RULE_LAYER) {
                    rule->data.conditional_rule.invalid_layer =
                        !css_parse_layer_prelude(tokens, cond_start, pos,
                            true, rule, pool);
                }
                *out_rule = rule->type == CSS_RULE_LAYER ? rule : NULL;
                return pos + 1 - start_pos;
            }

            // Parse block with nested rules
            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                if (rule->type == CSS_RULE_LAYER) {
                    rule->data.conditional_rule.invalid_layer =
                        !css_parse_layer_prelude(tokens, cond_start, pos,
                            false, rule, pool) ||
                        rule->data.conditional_rule.layer_name_count > 1;
                }
                pos++; // consume '{'

                // Parse nested rules
                int nested_capacity = 4;
                CssRule** nested_rules = (CssRule**)pool_calloc(pool,
                    nested_capacity * sizeof(CssRule*));
                int nested_count = 0;
                CssDeclaration** pending_decls = NULL;
                int pending_count = 0, pending_capacity = 0;

                while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                    // Skip whitespace
                    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
                    if (pos >= token_count || tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

                    if (nesting_parent_text && tokens[pos].type != CSS_TOKEN_AT_KEYWORD) {
                        int next = css_skip_whitespace_tokens(tokens, pos + 1, token_count);
                        bool declaration_start =
                            (tokens[pos].type == CSS_TOKEN_IDENT ||
                             tokens[pos].type == CSS_TOKEN_CUSTOM_PROPERTY) &&
                            next < token_count && tokens[next].type == CSS_TOKEN_COLON;
                        if (declaration_start) {
                            int before = pos;
                            CssDeclaration* decl = css_parse_declaration_from_tokens_mode(
                                tokens, &pos, token_count, pool, quirks_mode);
                            if (decl) {
                                if (pending_count >= pending_capacity &&
                                    !lam::pool_copy_grow_array(pool, &pending_decls,
                                        &pending_capacity, pending_count,
                                        pending_count + 1, 4, true)) return 0;
                                pending_decls[pending_count++] = decl;
                            }
                            if (pos == before) pos++;
                            if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON)
                                pos++;
                            continue;
                        }
                    }
                    if (nesting_parent_text && !css_nesting_flush_declarations(pool,
                            &nested_rules, &nested_count, &nested_capacity,
                            &pending_decls, &pending_count, &pending_capacity,
                            nesting_parent_group)) return 0;

                    // Recursively parse nested rule
                    CssRule* nested_rule = NULL;
                    int nested_consumed = nesting_parent_text &&
                        tokens[pos].type != CSS_TOKEN_AT_KEYWORD
                        ? css_parse_nested_style_rule(tokens + pos,
                            token_count - pos, pool, nesting_parent_text,
                            &nested_rule, true, quirks_mode)
                        : css_parse_rule_from_tokens_with_context(tokens + pos,
                            token_count - pos, pool, &nested_rule,
                            nesting_parent_text, nesting_parent_group,
                            quirks_mode);

                    if (nested_consumed > 0) {
                        pos += nested_consumed;
                        if (nested_rule) {
                            if (nested_count >= nested_capacity) {
                                if (!lam::pool_copy_grow_array(pool,
                                        &nested_rules, &nested_capacity,
                                        nested_count, nested_count + 1, 4, false)) break;
                            }
                            nested_rule->parent = rule;
                            nested_rules[nested_count++] = nested_rule;
                        }
                    } else {
                        // CSS error recovery: skip the unparseable rule and continue
                        // parsing remaining nested rules (e.g., @page inside @media).
                        // Look for the next rule boundary (closing brace or semicolon).
                        int brace_depth = 0;
                        while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                            if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                                brace_depth = 1;
                                pos++;
                                while (pos < token_count && brace_depth > 0) {
                                    if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) brace_depth++;
                                    else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) brace_depth--;
                                    pos++;
                                }
                                break;
                            } else if (tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                                pos++;
                                break;
                            }
                            pos++;
                        }
                        // if we stopped at a RIGHT_BRACE, that's the parent @media's
                        // closing brace — don't consume it
                    }
                }

                if (!css_nesting_flush_declarations(pool, &nested_rules,
                        &nested_count, &nested_capacity, &pending_decls,
                        &pending_count, &pending_capacity,
                        nesting_parent_group)) return 0;
                rule->data.conditional_rule.rules = nested_rules;
                rule->data.conditional_rule.rule_count = (size_t)nested_count;
                for (int i = 0; i < nested_count; i++) {
                    if (nested_rules[i]) nested_rules[i]->parent = rule;
                }

                if (pos < token_count && tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
                    pos++; // consume '}'
                }
            }

            *out_rule = rule;
            log_debug(" Parsed conditional @-rule with %zu nested rules",
                rule->data.conditional_rule.rule_count);
            return pos - start_pos;

        } else if (keyword_name && strcmp(keyword_name, "namespace") == 0) {
            // The stylesheet owns prefix scope; the rule parser only retains a
            // syntactically complete declaration for that scope to consume.
            int current = css_skip_whitespace_tokens(tokens, pos, token_count);
            const char* prefix = NULL;
            if (current < token_count && tokens[current].type == CSS_TOKEN_IDENT) {
                prefix = css_token_value_dup(&tokens[current], pool);
                current = css_skip_whitespace_tokens(tokens, current + 1, token_count);
            }
            const char* namespace_url = NULL;
            if (current < token_count &&
                (tokens[current].type == CSS_TOKEN_STRING ||
                 tokens[current].type == CSS_TOKEN_URL)) {
                namespace_url = css_token_value_dup(&tokens[current], pool);
                current++;
            } else if (current < token_count &&
                tokens[current].type == CSS_TOKEN_FUNCTION &&
                tokens[current].value &&
                str_icmp_cstr(tokens[current].value, "url(") == 0) {
                int argument = css_skip_whitespace_tokens(tokens, current + 1,
                    token_count);
                if (argument < token_count &&
                    tokens[argument].type == CSS_TOKEN_STRING) {
                    namespace_url = css_token_value_dup(&tokens[argument], pool);
                    current = css_skip_whitespace_tokens(tokens, argument + 1,
                        token_count);
                } else {
                    const char* start = tokens[current].start + tokens[current].length;
                    current = argument;
                    while (current < token_count &&
                           tokens[current].type != CSS_TOKEN_RIGHT_PAREN &&
                           tokens[current].type != CSS_TOKEN_SEMICOLON) current++;
                    if (current < token_count &&
                        tokens[current].type == CSS_TOKEN_RIGHT_PAREN) {
                        const char* end = tokens[current].start;
                        while (start < end && css_is_whitespace(*start)) start++;
                        while (end > start && css_is_whitespace(end[-1])) end--;
                        namespace_url = pool_dup_n(pool, start, (size_t)(end - start));
                    }
                }
                if (current < token_count &&
                    tokens[current].type == CSS_TOKEN_RIGHT_PAREN) current++;
                else namespace_url = NULL;
            }
            current = css_skip_whitespace_tokens(tokens, current, token_count);
            if (!namespace_url || current >= token_count ||
                tokens[current].type != CSS_TOKEN_SEMICOLON) {
                while (current < token_count &&
                       tokens[current].type != CSS_TOKEN_SEMICOLON &&
                       tokens[current].type != CSS_TOKEN_LEFT_BRACE) current++;
                if (current < token_count &&
                    tokens[current].type == CSS_TOKEN_SEMICOLON) current++;
                *out_rule = NULL;
                return current - start_pos;
            }
            rule->type = CSS_RULE_NAMESPACE;
            rule->data.namespace_rule.prefix = prefix;
            rule->data.namespace_rule.namespace_url = namespace_url;
            *out_rule = rule;
            return current + 1 - start_pos;

        } else if (keyword_name && (strcmp(keyword_name, "import") == 0 ||
                                 strcmp(keyword_name, "charset") == 0)) {
            // Simple at-rules with no block
            rule->type = strcmp(keyword_name, "import") == 0 ? CSS_RULE_IMPORT : CSS_RULE_CHARSET;            // Parse until semicolon
            int value_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }

            // Extract value
            if (pos > value_start) {
                if (rule->type == CSS_RULE_IMPORT) {
                    // @import URL extraction: handle url('...'), url(...), '...', "..."
                    const char* import_url = nullptr;
                    int after_url = value_start;
                    for (int ti = value_start; ti < pos; ti++) {
                        if (tokens[ti].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[ti].type == CSS_TOKEN_STRING) {
                            // @import 'file.css' or @import "file.css"
                            import_url = css_token_value_dup(&tokens[ti], pool);
                            after_url = ti + 1;
                            break;
                        }
                        if (tokens[ti].type == CSS_TOKEN_URL) {
                            // @import url(file.css) — unquoted URL token
                            import_url = css_token_value_dup(&tokens[ti], pool);
                            after_url = ti + 1;
                            break;
                        }
                        if (tokens[ti].type == CSS_TOKEN_FUNCTION && tokens[ti].value &&
                            str_ieq_cstr(tokens[ti].value, "url(")) {
                            // @import url(...) may contain punctuation that is tokenized
                            // as delimiters, e.g. http://host/css?family=Foo+Bar.
                            int arg_start = ti + 1;
                            while (arg_start < pos && tokens[arg_start].type == CSS_TOKEN_WHITESPACE) {
                                arg_start++;
                            }
                            if (arg_start < pos && tokens[arg_start].type == CSS_TOKEN_STRING) {
                                import_url = css_token_value_dup(&tokens[arg_start], pool);
                                int close = css_import_function_end(tokens, ti, pos);
                                after_url = close >= 0 ? close + 1 : pos;
                                break;
                            }

                            const char* raw_start = tokens[ti].start + tokens[ti].length;
                            const char* raw_end = nullptr;
                            for (int tj = arg_start; tj < pos; tj++) {
                                if (tokens[tj].type == CSS_TOKEN_RIGHT_PAREN) {
                                    raw_end = tokens[tj].start;
                                    break;
                                }
                            }
                            if (!raw_end && pos > arg_start) {
                                raw_end = tokens[pos - 1].start + tokens[pos - 1].length;
                            }
                            while (raw_start && raw_end && raw_start < raw_end &&
                                   (*raw_start == ' ' || *raw_start == '\t' || *raw_start == '\n' ||
                                    *raw_start == '\r' || *raw_start == '\f')) {
                                raw_start++;
                            }
                            while (raw_start && raw_end && raw_end > raw_start &&
                                   (raw_end[-1] == ' ' || raw_end[-1] == '\t' || raw_end[-1] == '\n' ||
                                    raw_end[-1] == '\r' || raw_end[-1] == '\f')) {
                                raw_end--;
                            }
                            if (raw_start && raw_end && raw_end > raw_start) {
                                size_t raw_len = (size_t)(raw_end - raw_start);
                                char* url_buf = pool_dup_n(pool, raw_start, raw_len);
                                if (url_buf) {
                                    import_url = url_buf;
                                }
                            }
                            int close = css_import_function_end(tokens, ti, pos);
                            after_url = close >= 0 ? close + 1 : pos;
                            break;
                        }
                        break; // unknown token pattern
                    }
                    rule->data.import_rule.url = import_url;
                    rule->data.import_rule.invalid = !import_url ||
                        !css_parse_import_modifiers(tokens, after_url, pos,
                            rule, pool);
                    if (import_url) {
                        log_debug(" @import URL extracted: '%s'", import_url);
                    }
                } else if (tokens[value_start].value) {
                    rule->data.charset_rule.charset = css_token_value_dup(
                        &tokens[value_start], pool);
                }
            }

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                pos++; // consume ';'
            }

            *out_rule = rule;
            log_debug(" Parsed simple @-rule: %s", keyword_name);
            return pos - start_pos;

        } else {
            // Other at-rules (like @font-face, @keyframes) - store raw content
            // Determine specific type
            if (keyword_name && strcmp(keyword_name, "font-face") == 0) {
                rule->type = CSS_RULE_FONT_FACE;
            } else if (keyword_name && strcmp(keyword_name, "keyframes") == 0) {
                rule->type = CSS_RULE_KEYFRAMES;
            } else if (keyword_name && strcmp(keyword_name, "page") == 0) {
                rule->type = CSS_RULE_PAGE;
            } else {
                // Unknown at-rule (e.g., @page, @layer, @property) - skip it
                // Per CSS spec, skip the at-rule's block or up to semicolon
                while (pos < token_count &&
                       tokens[pos].type != CSS_TOKEN_LEFT_BRACE &&
                       tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                    pos++;
                }
                if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                    pos++; // consume ';'
                } else if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                    // skip the block by matching braces
                    int brace_depth = 1;
                    pos++; // consume '{'
                    while (pos < token_count && brace_depth > 0) {
                        if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) brace_depth++;
                        else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) brace_depth--;
                        pos++;
                    }
                }
                *out_rule = NULL;
                return pos - start_pos;
            }

            // Store the name
            rule->data.generic_rule.name = pool_strdup(pool, keyword_name);

            // Build prefix content (e.g., animation name for @keyframes)
            // This is everything between the at-keyword and the opening brace
            int prefix_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_LEFT_BRACE &&
                   tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }
            int prefix_end = pos;

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                int opening_brace = pos;
                pos++; // consume '{'
                int content_start = pos; // Content starts after '{'

                // Skip contents until closing brace
                int brace_depth = 1;
                while (pos < token_count && brace_depth > 0) {
                    if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                        brace_depth++;
                    } else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
                        brace_depth--;
                    }
                    pos++;
                }
                int content_end = pos - 1; // Content ends before '}'

                if (rule->type == CSS_RULE_FONT_FACE && brace_depth == 0 &&
                    tokens[opening_brace].start && tokens[content_end].start) {
                    // Descriptor values such as U+3000-30FF require the original
                    // token spacing; the generic formatter inserts invalid spaces.
                    const char* raw_start = tokens[opening_brace].start;
                    const char* raw_end = tokens[content_end].start + tokens[content_end].length;
                    rule->data.generic_rule.content = pool_dup_n(
                        pool, raw_start, (size_t)(raw_end - raw_start));
                    *out_rule = rule;
                    return pos - start_pos;
                }

                // Build content string: prefix + { + content + }
                size_t content_length = 0;
                // Calculate prefix length
                for (int i = prefix_start; i < prefix_end; i++) {
                    if (tokens[i].value && tokens[i].type != CSS_TOKEN_WHITESPACE) {
                        content_length += strlen(tokens[i].value) + 1;
                    }
                }
                // Calculate body content length
                for (int i = content_start; i < content_end; i++) {
                    if (tokens[i].value) {
                        content_length += strlen(tokens[i].value) + 1;
                    }
                }
                content_length += 10; // for { }, spaces, etc.

                char* content = (char*)pool_alloc(pool, content_length + 1);
                content[0] = '\0';
                size_t content_len = 0;

                // Build prefix (e.g., "fadeIn")
                for (int i = prefix_start; i < prefix_end; i++) {
                    if (tokens[i].value && tokens[i].type != CSS_TOKEN_WHITESPACE) {
                        if (content_len > 0) {
                            content_len = str_cat(content, content_len, content_length + 1, " ", 1);
                        }
                        content_len = str_cat(content, content_len, content_length + 1, tokens[i].value, strlen(tokens[i].value));
                    }
                }

                // Add opening brace
                if (content_len > 0) {
                    content_len = str_cat(content, content_len, content_length + 1, " ", 1);
                }
                content_len = str_cat(content, content_len, content_length + 1, "{", 1);

                // Build body content
                // Track if we're inside a function like url() to avoid adding spaces
                int paren_depth = 0;
                for (int i = content_start; i < content_end; i++) {
                    if (tokens[i].value) {
                        // Track parenthesis depth for URL functions
                        if (tokens[i].type == CSS_TOKEN_FUNCTION ||
                            (tokens[i].type == CSS_TOKEN_DELIM && tokens[i].value[0] == '(')) {
                            paren_depth++;
                        }
                        if (tokens[i].type == CSS_TOKEN_RIGHT_PAREN) {
                            paren_depth--;
                            if (paren_depth < 0) paren_depth = 0;
                        }

                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
                            // Only preserve whitespace outside of function calls
                            if (paren_depth == 0) {
                                content_len = str_cat(content, content_len, content_length + 1, " ", 1);
                            }
                        } else {
                            // Add space before tokens that need it - but not inside function calls
                            if (paren_depth == 0 &&
                                content_len > 0 && content[content_len-1] != '{' &&
                                content[content_len-1] != ' ' &&
                                content[content_len-1] != '(' &&
                                tokens[i].type != CSS_TOKEN_SEMICOLON &&
                                tokens[i].type != CSS_TOKEN_COLON &&
                                tokens[i].type != CSS_TOKEN_COMMA &&
                                tokens[i].type != CSS_TOKEN_RIGHT_BRACE &&
                                tokens[i].type != CSS_TOKEN_RIGHT_PAREN) {
                                content_len = str_cat(content, content_len, content_length + 1, " ", 1);
                            }
                            content_len = str_cat(content, content_len, content_length + 1, tokens[i].value, strlen(tokens[i].value));
                        }
                    }
                }

                // Add closing brace
                content_len = str_cat(content, content_len, content_length + 1, " }", 2);

                rule->data.generic_rule.content = content;
                log_debug(" Stored content for %s: '%s'", keyword_name, content);

            } else if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
                pos++; // consume ';'
            }

            *out_rule = rule;
            log_debug(" Parsed generic @-rule: %s", keyword_name);
            return pos - start_pos;
        }
    }
    // Outside nesting, & has the stylesheet :scope context.
    if (!nesting_parent_text) {
        for (int i = pos; i < token_count &&
             tokens[i].type != CSS_TOKEN_LEFT_BRACE; i++) {
            if (tokens[i].type == CSS_TOKEN_DELIM &&
                tokens[i].data.delimiter == '&') {
                return css_parse_nested_style_rule(tokens + pos,
                    token_count - pos, pool, ":scope", out_rule, false,
                    quirks_mode);
            }
        }
    }
    // Parse selector(s) using enhanced parser (supports compound, descendant, and comma-separated selectors)
    log_debug(" Parsing selectors at position %d", pos);

    // Parse selector group (handles single selectors and comma-separated groups)
    CssSelectorGroup* selector_group = css_parse_selector_group_from_tokens(tokens, &pos, token_count, pool);
    if (!selector_group) {
        log_debug(" ERROR: Failed to parse selector group");
        return 0;
    }
    if (css_selector_group_contains_generic_pseudo(selector_group)) return 0;

    log_debug(" Parsed selector group with %zu selector(s)", selector_group->selector_count);

    // Skip whitespace
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);

    // Expect opening brace
    if (pos >= token_count || tokens[pos].type != CSS_TOKEN_LEFT_BRACE) {
        log_debug(" ERROR: Expected '{' but got token type %d at position %d",
                pos < token_count ? tokens[pos].type : -1, pos);
        return 0;
    }
    const char* selector_source = pool_dup_n(pool, tokens[start_pos].start,
        (size_t)(tokens[pos].start - tokens[start_pos].start));
    if (!selector_source) return 0;
    log_debug(" Found '{', parsing declarations");
    pos++;

    // Parse declarations and nested rules (CSS Nesting support)
    CssDeclaration** declarations = NULL;
    int decl_count = 0;
    int decl_capacity = 4;
    declarations = (CssDeclaration**)pool_calloc(pool, decl_capacity * sizeof(CssDeclaration*));

    CssRule** nested_rules = NULL;
    int nested_rule_count = 0;
    int nested_rule_capacity = 0;
    bool seen_nested_rule = false;

    // Post-nested declarations (declarations appearing after a nested rule → CSSNestedDeclarations)
    CssDeclaration** post_nested_decls = NULL;
    int post_nested_count = 0;
    int post_nested_capacity = 0;

    while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
        // Skip whitespace
        pos = css_skip_whitespace_tokens(tokens, pos, token_count);
        if (pos >= token_count || tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

        // A group rule inside a style rule keeps the style selector as its
        // context for declarations and relative child selectors.
        if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
            if (seen_nested_rule && !css_nesting_flush_declarations(pool,
                    &nested_rules, &nested_rule_count, &nested_rule_capacity,
                    &post_nested_decls, &post_nested_count,
                    &post_nested_capacity, selector_group)) return 0;
            CssRule* nested = NULL;
            int consumed = css_parse_rule_from_tokens_with_context(tokens + pos,
                token_count - pos, pool, &nested, selector_source,
                selector_group, quirks_mode);
            if (consumed > 0 && nested &&
                (nested->type == CSS_RULE_MEDIA ||
                 nested->type == CSS_RULE_SUPPORTS ||
                 nested->type == CSS_RULE_LAYER ||
                 nested->type == CSS_RULE_CONTAINER)) {
                if (nested_rule_count >= nested_rule_capacity &&
                    !lam::pool_copy_grow_array(pool, &nested_rules,
                        &nested_rule_capacity, nested_rule_count,
                        nested_rule_count + 1, 4, true)) return 0;
                nested_rules[nested_rule_count++] = nested;
                seen_nested_rule = true;
            }
            pos = consumed > 0 ? pos + consumed
                               : css_skip_at_rule_tokens(tokens, pos, token_count);
            continue;
        }

        // Check if this looks like a nested qualified rule (CSS Nesting)
        // A nested rule starts with tokens that can't begin a declaration:
        //   DELIM (., #, >, ~, +, *, &), HASH, LEFT_BRACKET, COLON
        // Also: IDENT not followed by COLON (e.g., nested "div { }")
        bool looks_like_nested_rule = false;
        CssTokenType cur_type = tokens[pos].type;
        if (cur_type == CSS_TOKEN_DELIM || cur_type == CSS_TOKEN_HASH ||
            cur_type == CSS_TOKEN_LEFT_BRACKET || cur_type == CSS_TOKEN_COLON) {
            looks_like_nested_rule = true;
        } else if (cur_type == CSS_TOKEN_IDENT) {
            // IDENT followed by COLON = declaration; otherwise = nested rule
            int peek = css_skip_whitespace_tokens(tokens, pos + 1, token_count);
            if (peek >= token_count || tokens[peek].type != CSS_TOKEN_COLON) {
                looks_like_nested_rule = true;
            }
        }

        if (looks_like_nested_rule) {
            bool has_nested_block = false;
            int probe = pos;
            int probe_paren = 0;
            int probe_bracket = 0;
            while (probe < token_count) {
                CssTokenType probe_type = tokens[probe].type;
                if (probe_type == CSS_TOKEN_LEFT_PAREN) probe_paren++;
                else if (probe_type == CSS_TOKEN_RIGHT_PAREN && probe_paren > 0) probe_paren--;
                else if (probe_type == CSS_TOKEN_LEFT_BRACKET) probe_bracket++;
                else if (probe_type == CSS_TOKEN_RIGHT_BRACKET && probe_bracket > 0) probe_bracket--;
                else if (probe_type == CSS_TOKEN_LEFT_BRACE &&
                         probe_paren == 0 && probe_bracket == 0) {
                    has_nested_block = true;
                    break;
                }
                if ((probe_type == CSS_TOKEN_SEMICOLON ||
                     probe_type == CSS_TOKEN_RIGHT_BRACE) &&
                    probe_paren == 0 && probe_bracket == 0) {
                    break;
                }
                probe++;
            }
            // css syntax §5.4.5 drops an invalid declaration through its semicolon.
            // Do not mistake legacy `*property` syntax for a nested rule and lose
            // the valid declarations which follow it in the same declaration list.
            if (!has_nested_block) looks_like_nested_rule = false;
        }

        if (looks_like_nested_rule) {
            if (seen_nested_rule && !css_nesting_flush_declarations(pool,
                    &nested_rules, &nested_rule_count, &nested_rule_capacity,
                    &post_nested_decls, &post_nested_count,
                    &post_nested_capacity, selector_group)) return 0;
            CssRule* nested = NULL;
            int consumed = css_parse_nested_style_rule(tokens + pos,
                token_count - pos, pool, selector_source, &nested, true,
                quirks_mode);
            if (consumed > 0) {
                pos += consumed;
                if (nested) {
                    if (nested_rule_count >= nested_rule_capacity &&
                        !lam::pool_copy_grow_array(pool, &nested_rules,
                            &nested_rule_capacity, nested_rule_count,
                            nested_rule_count + 1, 4, true)) return 0;
                    nested_rules[nested_rule_count++] = nested;
                    seen_nested_rule = true;
                }
            } else {
                pos++;
            }
            continue;
        }

        // Parse declaration
        CssDeclaration* decl = css_parse_declaration_from_tokens_mode(
            tokens, &pos, token_count, pool, quirks_mode);
        log_debug(" After parsing: decl=%p", (void*)decl);
        if (decl) {
            log_debug(" Parsed declaration: property_code=%d for position %d",
                    decl->property_code, decl_count);

            if (seen_nested_rule) {
                // Declaration after nested rule → collect for CSSNestedDeclarations
                if (post_nested_count >= post_nested_capacity) {
                    if (!lam::pool_copy_grow_array(pool, &post_nested_decls, &post_nested_capacity,
                                                    post_nested_count, post_nested_count + 1, 4, true)) continue;
                }
                post_nested_decls[post_nested_count++] = decl;
            } else {
                // Expand array if needed
                if (decl_count >= decl_capacity) {
                    if (!lam::pool_copy_grow_array(pool, &declarations, &decl_capacity,
                                                    decl_count, decl_count + 1, 4, true)) return 0;
                }
                declarations[decl_count++] = decl;
                log_debug(" Stored declaration at index %d, now have %d declarations",
                        decl_count - 1, decl_count);
            }
        }

        // Skip optional semicolon
        if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
            pos++;
        }
    }

    // If there are post-nested declarations, create a CSSNestedDeclarations rule
    if (!css_nesting_flush_declarations(pool, &nested_rules, &nested_rule_count,
            &nested_rule_capacity, &post_nested_decls, &post_nested_count,
            &post_nested_capacity, selector_group)) return 0;

    // Expect closing brace (EOF implicitly closes the rule per CSS spec)
    if (pos < token_count && tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
        pos++; // consume the closing brace
    } else if (pos < token_count && tokens[pos].type == CSS_TOKEN_EOF) {
        // EOF implicitly closes the block
    } else if (pos >= token_count) {
        // ran out of tokens — implicitly closed
    } else {
        return 0; // unexpected token
    }

    // Create the CSS rule
    CssRule* rule = css_new_author_rule(pool);
    if (!rule) return 0;

    rule->type = CSS_RULE_STYLE;
    rule->data.style_rule.selector_group = selector_group;
    // For backward compatibility, store the first selector in the single selector field
    rule->data.style_rule.selector = (selector_group->selector_count > 0) ? selector_group->selectors[0] : NULL;
    rule->data.style_rule.declarations = declarations;
    rule->data.style_rule.declaration_count = decl_count;
    rule->data.style_rule.nested_rules = nested_rules;
    rule->data.style_rule.nested_rule_count = nested_rule_count;

    // Set parent pointer on nested rules
    for (int i = 0; i < nested_rule_count; i++) {
        if (!nested_rules[i]) continue;
        nested_rules[i]->parent = rule;
        if (nested_rules[i]->type == CSS_RULE_NESTED_DECLARATIONS) {
            // these declarations retain each matching branch of the parent.
            nested_rules[i]->data.style_rule.selector_group = selector_group;
            nested_rules[i]->data.style_rule.selector = rule->data.style_rule.selector;
        }
    }

    log_debug(" Created rule with %d declarations:", decl_count);
    for (int i = 0; i < decl_count && i < 5; i++) {
        if (declarations[i]) {
            log_debug("   Declaration[%d]: property_code=%d", i, declarations[i]->property_code);
        }
    }

    *out_rule = rule;
    return pos - start_pos; // Return number of tokens consumed
}

static int css_parse_nested_style_rule(const CssToken* tokens, int token_count,
                                       Pool* pool, const char* parent_text,
                                       CssRule** out_rule, bool implicit_parent,
                                       bool quirks_mode) {
    if (!tokens || token_count <= 0 || !pool || !parent_text || !out_rule) return 0;
    *out_rule = NULL;
    int block_start = 0;
    int paren_depth = 0, bracket_depth = 0;
    while (block_start < token_count) {
        CssTokenType type = tokens[block_start].type;
        if (type == CSS_TOKEN_LEFT_BRACE && !paren_depth && !bracket_depth) break;
        if (type == CSS_TOKEN_LEFT_PAREN || type == CSS_TOKEN_FUNCTION) paren_depth++;
        else if (type == CSS_TOKEN_RIGHT_PAREN && paren_depth > 0) paren_depth--;
        else if (type == CSS_TOKEN_LEFT_BRACKET) bracket_depth++;
        else if (type == CSS_TOKEN_RIGHT_BRACKET && bracket_depth > 0) bracket_depth--;
        if ((type == CSS_TOKEN_SEMICOLON || type == CSS_TOKEN_RIGHT_BRACE) &&
            !paren_depth && !bracket_depth) return 0;
        block_start++;
    }
    if (block_start >= token_count) return 0;
    int end = block_start + 1;
    int block_depth = 1;
    while (end < token_count && block_depth > 0) {
        if (tokens[end].type == CSS_TOKEN_LEFT_BRACE) block_depth++;
        else if (tokens[end].type == CSS_TOKEN_RIGHT_BRACE) block_depth--;
        end++;
    }

    char* authored = NULL;
    char* expanded = css_expand_nested_selector(tokens, 0, block_start,
                                                 parent_text, pool,
                                                 implicit_parent, &authored);
    if (!expanded) return end;
    StrBuf* source = strbuf_new();
    if (!source) return end;
    const char* block_end = tokens[end - 1].start + tokens[end - 1].length;
    strbuf_append_str(source, expanded);
    strbuf_append_str_n(source, tokens[block_start].start,
                        (size_t)(block_end - tokens[block_start].start));
    // selector and value spans can be retained by the parsed rule.
    char* retained_source = source->str
        ? pool_dup_n(pool, source->str, source->length) : NULL;
    if (retained_source) {
        size_t nested_token_count = 0;
        CssToken* nested_tokens = css_tokenize(retained_source, source->length,
                                                pool, &nested_token_count);
        CssRule* nested = NULL;
        if (nested_tokens && nested_token_count > 0 &&
            css_parse_rule_from_tokens_with_context(nested_tokens,
                (int)nested_token_count, pool, &nested, NULL, NULL,
                quirks_mode) > 0 &&
            nested && nested->type == CSS_RULE_STYLE) {
            nested->data.style_rule.authored_selector_text = authored;
            *out_rule = nested;
        }
    }
    strbuf_free(source);
    return end;
}

int css_parse_rule_from_tokens_internal_mode(const CssToken* tokens,
    int token_count, Pool* pool, CssRule** out_rule, bool quirks_mode) {
    return css_parse_rule_from_tokens_with_context(tokens, token_count, pool,
                                                   out_rule, NULL, NULL,
                                                   quirks_mode);
}

int css_parse_rule_from_tokens_internal(const CssToken* tokens, int token_count,
                                        Pool* pool, CssRule** out_rule) {
    return css_parse_rule_from_tokens_internal_mode(tokens, token_count,
                                                    pool, out_rule, false);
}

// Legacy wrapper that returns CssRule* (for compatibility)
CssRule* css_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool) {
    CssRule* rule = NULL;
    css_parse_rule_from_tokens_internal(tokens, token_count, pool, &rule);
    return rule;
}

static bool css_declaration_parse_consumed_all(const CssToken* tokens, int pos, int token_count) {
    if (!tokens || pos < 0 || token_count <= 0) return false;
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    if (pos < token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) {
        pos++;
        pos = css_skip_whitespace_tokens(tokens, pos, token_count);
    }
    return pos < token_count && tokens[pos].type == CSS_TOKEN_EOF;
}

CssRule* css_parse_rule_text(const char* text, size_t length, Pool* pool) {
    if (!text || length == 0 || !pool) return NULL;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &token_count);
    if (!tokens || token_count == 0) return NULL;

    int start = css_skip_whitespace_tokens(tokens, 0, (int)token_count);
    CssRule* rule = NULL;
    if (start < (int)token_count) {
        int consumed = css_parse_rule_from_tokens_internal(
            tokens + start, (int)token_count - start, pool, &rule);
        if (consumed <= 0 || !rule) {
            rule = NULL;
        } else {
            int end = start + consumed;
            end = css_skip_whitespace_tokens(tokens, end, (int)token_count);
            if (end >= (int)token_count || tokens[end].type != CSS_TOKEN_EOF) {
                rule = NULL;
            }
        }
    }
    css_token_array_release(pool, tokens, token_count);
    return rule;
}

bool css_resolve_selector_namespaces(CssSelector* selector,
                                     CssNamespaceLookupFn lookup, void* context) {
    if (!selector) return true;
    for (size_t c = 0; c < selector->compound_selector_count; c++) {
        CssCompoundSelector* compound = selector->compound_selectors[c];
        if (!compound) continue;
        for (size_t s = 0; s < compound->simple_selector_count; s++) {
            CssSimpleSelector* simple = compound->simple_selectors[s];
            if (!simple) continue;
            if (simple->type == CSS_SELECTOR_TYPE_ELEMENT ||
                simple->type == CSS_SELECTOR_TYPE_UNIVERSAL) {
                const char* prefix = simple->namespace_prefix;
                if (!prefix) {
                    simple->namespace_url = lookup ? lookup(context, NULL) : NULL;
                } else if (strcmp(prefix, "*") == 0) {
                    simple->namespace_url = NULL;
                } else if (!*prefix) {
                    simple->namespace_url = "";
                } else {
                    simple->namespace_url = lookup ? lookup(context, prefix) : NULL;
                    if (!simple->namespace_url) return false;
                }
            } else if (simple->type >= CSS_SELECTOR_ATTR_EXACT &&
                       simple->type <= CSS_SELECTOR_ATTR_CASE_SENSITIVE) {
                const char* prefix = simple->namespace_prefix;
                if (!prefix || !*prefix) {
                    simple->namespace_url = "";
                } else if (strcmp(prefix, "*") == 0) {
                    simple->namespace_url = NULL;
                } else {
                    simple->namespace_url = lookup ? lookup(context, prefix) : NULL;
                    if (!simple->namespace_url) return false;
                }
            }
            // :is() and :where() discard invalid arguments; other functional
            // selectors invalidate the enclosing selector.
            bool forgiving = simple->type == CSS_SELECTOR_PSEUDO_IS ||
                             simple->type == CSS_SELECTOR_PSEUDO_WHERE;
            size_t write = 0;
            for (size_t i = 0; i < simple->function_selector_count; i++) {
                CssSelector* argument = simple->function_selectors[i];
                if (css_resolve_selector_namespaces(argument, lookup, context)) {
                    simple->function_selectors[write++] = argument;
                } else if (!forgiving) {
                    return false;
                }
            }
            simple->function_selector_count = write;
        }
    }
    return true;
}

CssSelectorGroup* css_parse_selector_group_text(const char* text, size_t length, Pool* pool) {
    if (!text || length == 0 || !pool) return NULL;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &token_count);
    if (!tokens || token_count == 0) return NULL;

    bool has_nesting_selector = false;
    int selector_end = (int)token_count;
    if (tokens[selector_end - 1].type == CSS_TOKEN_EOF) selector_end--;
    for (int i = 0; i < selector_end; i++) {
        if (tokens[i].type == CSS_TOKEN_DELIM &&
            tokens[i].data.delimiter == '&') {
            has_nesting_selector = true;
            break;
        }
    }
    if (has_nesting_selector) {
        // & outside a nested rule has the caller's :scope anchor.
        char* expanded = css_expand_nested_selector(tokens, 0, selector_end,
                                                     ":scope", pool, false, NULL);
        css_token_array_release(pool, tokens, token_count);
        if (!expanded) return NULL;
        tokens = css_tokenize(expanded, strlen(expanded), pool, &token_count);
        if (!tokens || token_count == 0) return NULL;
    }

    int pos = 0;
    CssSelectorGroup* group = css_parse_selector_group_from_tokens(
        tokens, &pos, (int)token_count, pool);
    bool valid_namespace = true;
    if (group) {
        for (size_t i = 0; i < group->selector_count; i++) {
            if (!css_resolve_selector_namespaces(group->selectors[i], NULL, NULL)) {
                valid_namespace = false;
                break;
            }
        }
    }
    if (!group || group->selector_count == 0 || !valid_namespace ||
        !css_selector_group_parse_consumed_all(tokens, pos, (int)token_count) ||
        css_selector_group_contains_generic_pseudo(group)) {
        group = NULL;
    }
    css_token_array_release(pool, tokens, token_count);
    return group;
}

CssSelectorGroup* css_parse_nested_selector_group_text(const char* text,
    size_t length, const char* parent_selector_text, Pool* pool,
    char** authored_text) {
    if (!text || !length || !parent_selector_text || !pool) return NULL;
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &token_count);
    if (!tokens || !token_count) return NULL;
    int selector_end = (int)token_count;
    if (tokens[selector_end - 1].type == CSS_TOKEN_EOF) selector_end--;
    char* authored = NULL;
    char* expanded = css_expand_nested_selector(tokens, 0, selector_end,
        parent_selector_text, pool, true, &authored);
    css_token_array_release(pool, tokens, token_count);
    if (!expanded) return NULL;
    CssSelectorGroup* group = css_parse_selector_group_text(
        expanded, strlen(expanded), pool);
    if (group && authored_text) *authored_text = authored;
    return group;
}

static bool css_selector_contains_generic_pseudo(const CssSelector* selector) {
    if (!selector) return false;
    for (size_t i = 0; i < selector->compound_selector_count; i++) {
        CssCompoundSelector* compound = selector->compound_selectors[i];
        if (!compound) continue;
        for (size_t j = 0; j < compound->simple_selector_count; j++) {
            CssSimpleSelector* simple = compound->simple_selectors[j];
            if (!simple) continue;
            if ((simple->type == CSS_SELECTOR_PSEUDO_GENERIC ||
                 simple->type == CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC) &&
                !css_selector_generic_pseudo_is_known(simple)) {
                return true;
            }
            for (size_t k = 0; k < simple->function_selector_count; k++) {
                if (css_selector_contains_generic_pseudo(simple->function_selectors[k])) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool css_selector_generic_pseudo_is_known(const CssSimpleSelector* selector) {
    if (!selector || !selector->value) return false;
    if (selector->type == CSS_SELECTOR_PSEUDO_GENERIC) {
        return strcmp(selector->value, "host") == 0;
    }
    if (selector->type != CSS_SELECTOR_PSEUDO_ELEMENT_GENERIC) return false;
    static const char* names[] = {
        "picker", "picker-icon", "checkmark", "details-content",
        "part", "cue", "cue-region"
    };
    for (const char* name : names) {
        if (strcmp(selector->value, name) == 0) return true;
    }
    return false;
}

bool css_selector_group_contains_generic_pseudo(const CssSelectorGroup* group) {
    if (!group) return false;
    for (size_t i = 0; i < group->selector_count; i++) {
        if (css_selector_contains_generic_pseudo(group->selectors[i])) return true;
    }
    return false;
}

CssDeclaration* css_parse_declaration_text(const char* text, size_t length, Pool* pool) {
    if (!text || length == 0 || !pool) return NULL;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &token_count);
    if (!tokens || token_count == 0) return NULL;

    int pos = 0;
    CssDeclaration* declaration = css_parse_declaration_from_tokens(
        tokens, &pos, (int)token_count, pool);
    if (!declaration || !css_declaration_parse_consumed_all(tokens, pos, (int)token_count)) {
        declaration = NULL;
    }
    css_token_array_release(pool, tokens, token_count);
    return declaration;
}

bool css_declaration_is_supported(const CssDeclaration* declaration) {
    if (!declaration) return false;
    if (declaration->property_name &&
        declaration->property_name[0] == '-' && declaration->property_name[1] == '-') {
        return true;
    }
    return declaration->property_code > 0 &&
        css_property_exists(declaration->property_code) &&
        css_property_validate_value(declaration->property_code,
                                    declaration->value);
}

CssDeclaration** css_parse_declaration_list_text(const char* text, size_t length,
                                                 Pool* pool, size_t* declaration_count) {
    if (declaration_count) *declaration_count = 0;
    if (!text || length == 0 || !pool || !declaration_count) return NULL;

    size_t token_count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &token_count);
    if (!tokens || token_count == 0) return NULL;

    size_t capacity = 8;
    CssDeclaration** declarations = (CssDeclaration**)pool_calloc(
        pool, capacity * sizeof(CssDeclaration*));
    if (!declarations) {
        css_token_array_release(pool, tokens, token_count);
        return NULL;
    }

    int pos = 0;
    while (pos < (int)token_count) {
        pos = css_skip_whitespace_tokens(tokens, pos, (int)token_count);
        if (pos >= (int)token_count || tokens[pos].type == CSS_TOKEN_EOF ||
            tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

        // descriptor blocks may contain nested at-rules. They are not
        // declarations and must be skipped while preserving forward progress.
        if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
            pos = css_skip_at_rule_tokens(tokens, pos, (int)token_count);
            continue;
        }

        if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
            pos++;
            continue;
        }

        int before = pos;
        CssDeclaration* declaration = css_parse_declaration_from_tokens(
            tokens, &pos, (int)token_count, pool);
        if (declaration) {
            if (*declaration_count >= capacity) {
                if (!lam::pool_copy_grow_array(pool, &declarations, &capacity,
                                                *declaration_count, *declaration_count + 1, 8, true)) {
                    css_token_array_release(pool, tokens, token_count);
                    return declarations;
                }
            }
            declarations[(*declaration_count)++] = declaration;
        }

        if (pos < (int)token_count && tokens[pos].type == CSS_TOKEN_SEMICOLON) pos++;
        if (pos == before) pos++;
    }

    css_token_array_release(pool, tokens, token_count);
    if (*declaration_count == 0) return NULL;
    return declarations;
}

CssDeclaration* css_parse_property_declaration(const char* property, size_t property_length,
                                               const char* value, size_t value_length,
                                               Pool* pool) {
    if (!property || property_length == 0 || !value || !pool) return NULL;
    const size_t max_size = (size_t)-1;
    if (property_length > max_size - 2 || value_length > max_size - property_length - 2) return NULL;

    size_t length = property_length + value_length + 2;
    char* text = pool_join3(pool, property, property_length, ": ", 2, value, value_length);
    if (!text) return NULL;
    return css_parse_declaration_text(text, length, pool);
}
