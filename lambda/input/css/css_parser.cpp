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
#include "../../../lib/str.h"
#include <stdlib.h>
#include <string.h>

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
    return CSS_SELECTOR_PSEUDO_NOT;                   // unknown — default fallback
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

        // skip whitespace
        while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
        if (pos >= end) {
            // just "2n" with no b
            return true;
        }

        // check for <signed-integer> (e.g., "+3" or "-3" as a single number token)
        if (tokens[pos].type == CSS_TOKEN_NUMBER) {
            double bv = tokens[pos].data.number_value;
            if (bv != (int)bv) return false;
            // signed-integer: the token must include the sign
            const char* ns = tokens[pos].start;
            if (ns && *ns != '+' && *ns != '-') return false; // signless not allowed here
            formula->b = (int)bv;
            EXPECT_END(pos + 1);
            return true;
        }

        // check for ['+' | '-'] <signless-integer>
        if (tokens[pos].type == CSS_TOKEN_DELIM &&
            (tokens[pos].data.delimiter == '+' || tokens[pos].data.delimiter == '-')) {
            int sign = (tokens[pos].data.delimiter == '-') ? -1 : 1;
            pos++;
            // skip whitespace between sign and integer
            while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
            if (pos >= end || tokens[pos].type != CSS_TOKEN_NUMBER) return false;
            double bv = tokens[pos].data.number_value;
            if (bv != (int)bv) return false;
            // signless-integer: must NOT have a sign
            const char* ns = tokens[pos].start;
            if (ns && (*ns == '+' || *ns == '-')) return false;
            formula->b = sign * (int)bv;
            EXPECT_END(pos + 1);
            return true;
        }

        return false; // unexpected token after dimension
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

        if (rest_len > 0) {
            // ndashdigit-ident: "n-3" or "-n-3", or "n-"/ "-n-" (dash with no inline digit)
            if (*rest != '-') return false;
            rest++;
            rest_len--;
            if (rest_len == 0) {
                // "n-" or "-n-" with no digits in the ident token
                // Treat as bare n/−n with '-' baked in; next token should be signless integer
                formula->a = negated ? -1 : 1;
                pos++;
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
            int b_val = 0;
            for (size_t i = 0; i < rest_len; i++) {
                if (rest[i] < '0' || rest[i] > '9') return false;
                b_val = b_val * 10 + (rest[i] - '0');
            }
            formula->a = negated ? -1 : 1;
            formula->b = -b_val;
            EXPECT_END(pos + 1);
            return true;
        }

        // bare "n" or "-n"
        formula->a = negated ? -1 : 1;
        pos++;

        // skip whitespace
        while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
        if (pos >= end) return true; // just "n" or "-n"

        // check for <signed-integer>
        if (tokens[pos].type == CSS_TOKEN_NUMBER) {
            double bv = tokens[pos].data.number_value;
            if (bv != (int)bv) return false;
            const char* ns = tokens[pos].start;
            if (ns && *ns != '+' && *ns != '-') return false; // must be signed
            formula->b = (int)bv;
            EXPECT_END(pos + 1);
            return true;
        }

        // check for ['+' | '-'] <signless-integer>
        if (tokens[pos].type == CSS_TOKEN_DELIM &&
            (tokens[pos].data.delimiter == '+' || tokens[pos].data.delimiter == '-')) {
            int sign = (tokens[pos].data.delimiter == '-') ? -1 : 1;
            pos++;
            while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
            if (pos >= end || tokens[pos].type != CSS_TOKEN_NUMBER) return false;
            double bv = tokens[pos].data.number_value;
            if (bv != (int)bv) return false;
            const char* ns = tokens[pos].start;
            if (ns && (*ns == '+' || *ns == '-')) return false; // must be signless
            formula->b = sign * (int)bv;
            EXPECT_END(pos + 1);
            return true;
        }

        return false;
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
            if (rest_len > 0) {
                if (*rest != '-') return false;
                rest++;
                rest_len--;
                if (rest_len == 0) {
                    // "+n-" with no inline digits: e.g. "+n- 5"
                    formula->a = 1;
                    pos++;
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
                int b_val = 0;
                for (size_t i = 0; i < rest_len; i++) {
                    if (rest[i] < '0' || rest[i] > '9') return false;
                    b_val = b_val * 10 + (rest[i] - '0');
                }
                formula->a = 1;
                formula->b = -b_val;
                EXPECT_END(pos + 1);
                return true;
            }

            // bare "+n"
            formula->a = 1;
            pos++;
            while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
            if (pos >= end) return true;

            // <signed-integer>
            if (tokens[pos].type == CSS_TOKEN_NUMBER) {
                double bv = tokens[pos].data.number_value;
                if (bv != (int)bv) return false;
                const char* ns = tokens[pos].start;
                if (ns && *ns != '+' && *ns != '-') return false;
                formula->b = (int)bv;
                EXPECT_END(pos + 1);
                return true;
            }

            // ['+' | '-'] <signless-integer>
            if (tokens[pos].type == CSS_TOKEN_DELIM &&
                (tokens[pos].data.delimiter == '+' || tokens[pos].data.delimiter == '-')) {
                int sign = (tokens[pos].data.delimiter == '-') ? -1 : 1;
                pos++;
                while (pos < end && tokens[pos].type == CSS_TOKEN_WHITESPACE) pos++;
                if (pos >= end || tokens[pos].type != CSS_TOKEN_NUMBER) return false;
                double bv = tokens[pos].data.number_value;
                if (bv != (int)bv) return false;
                const char* ns = tokens[pos].start;
                if (ns && (*ns == '+' || *ns == '-')) return false;
                formula->b = sign * (int)bv;
                EXPECT_END(pos + 1);
                return true;
            }
            return false;
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

// Forward declaration
static CssValue* css_parse_token_to_value(const CssToken* token, Pool* pool);

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

// Helper: Parse a CSS function with its arguments from tokens
// Returns a CssValue of type CSS_VALUE_TYPE_FUNCTION
// *pos should point to the CSS_TOKEN_FUNCTION token; on return, *pos points past the closing paren
// CSS function arguments are comma-separated; each argument may contain multiple space-separated tokens
static CssValue* css_parse_function_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;
    if (tokens[*pos].type != CSS_TOKEN_FUNCTION) return NULL;

    // Get function name (strip trailing '(' if present)
    const char* func_name = tokens[*pos].value;
    if (!func_name && tokens[*pos].start && tokens[*pos].length > 0) {
        char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
        if (name_buf) {
            memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
            name_buf[tokens[*pos].length] = '\0';
            func_name = name_buf;
        }
    }

    // Strip trailing '(' from function name if present
    if (func_name) {
        size_t func_len = strlen(func_name);
        if (func_len > 0 && func_name[func_len - 1] == '(') {
            char* clean_name = (char*)pool_calloc(pool, func_len);
            if (clean_name) {
                memcpy(clean_name, func_name, func_len - 1);
                clean_name[func_len - 1] = '\0';
                func_name = clean_name;
            }
        }
    }

    (*pos)++;  // Skip FUNCTION token

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

    // Create the function value
    CssValue* func_value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!func_value) return NULL;

    func_value->type = CSS_VALUE_TYPE_FUNCTION;
    func_value->data.function = (CssFunction*)pool_calloc(pool, sizeof(CssFunction));
    if (!func_value->data.function) return NULL;

    func_value->data.function->name = func_name ? pool_strdup(pool, func_name) : "";
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
                char* buf = (char*)pool_calloc(pool, token->length + 1);
                if (buf) {
                    memcpy(buf, token->start, token->length);
                    buf[token->length] = '\0';
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
                char* buf = (char*)pool_calloc(pool, token->length + 1);
                if (buf) {
                    memcpy(buf, token->start, token->length);
                    buf[token->length] = '\0';
                    str_val = buf;
                }
            }
            value->data.string = str_val ? pool_strdup(pool, str_val) : "";
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
                char* buf = (char*)pool_calloc(pool, token->length + 1);
                if (buf) {
                    memcpy(buf, token->start, token->length);
                    buf[token->length] = '\0';
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
                char* buf = (char*)pool_calloc(pool, token->length + 1);
                if (buf) {
                    memcpy(buf, token->start, token->length);
                    buf[token->length] = '\0';
                    value->data.custom_property.name = buf;
                }
            }
            value->data.custom_property.fallback = NULL;
            break;
    }

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

        // Stop at structural tokens (whitespace, combinators, commas, braces)
        if (token->type == CSS_TOKEN_WHITESPACE ||
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
            // Expand array
            capacity *= 2;
            CssSimpleSelector** new_array = (CssSimpleSelector**)pool_calloc(pool, capacity * sizeof(CssSimpleSelector*));
            if (!new_array) return NULL;
            memcpy(new_array, compound->simple_selectors, compound->simple_selector_count * sizeof(CssSimpleSelector*));
            compound->simple_selectors = new_array;
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

        // Check for explicit combinators (>, +, ~)
        const CssToken* token = &tokens[*pos];
        if (token->type == CSS_TOKEN_DELIM) {
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
                capacity *= 2;
                CssCompoundSelector** new_compounds = (CssCompoundSelector**)pool_calloc(pool, capacity * sizeof(CssCompoundSelector*));
                CssCombinator* new_combinators = (CssCombinator*)pool_calloc(pool, capacity * sizeof(CssCombinator));
                if (!new_compounds || !new_combinators) break;

                memcpy(new_compounds, selector->compound_selectors, selector->compound_selector_count * sizeof(CssCompoundSelector*));
                memcpy(new_combinators, selector->combinators, selector->compound_selector_count * sizeof(CssCombinator));

                selector->compound_selectors = new_compounds;
                selector->combinators = new_combinators;
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
        if (!next) {
            log_debug("[CSS Parser] WARNING: Failed to parse selector after comma, stopping group");
            break;
        }

        // Expand array if needed
        if (count >= capacity) {
            capacity *= 2;
            CssSelector** new_selectors = (CssSelector**)pool_calloc(pool, capacity * sizeof(CssSelector*));
            if (!new_selectors) {
                log_debug("[CSS Parser] ERROR: Failed to expand selector array");
                break;
            }
            memcpy(new_selectors, selectors, count * sizeof(CssSelector*));
            selectors = new_selectors;
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

// Helper: Parse a simple CSS selector from tokens (simplified for now)
CssSimpleSelector* css_parse_simple_selector_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
    if (!tokens || !pos || *pos >= token_count || !pool) return NULL;

    // Skip leading whitespace
    *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
    if (*pos >= token_count) return NULL;

    CssSimpleSelector* selector = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
    if (!selector) return NULL;

    const CssToken* token = &tokens[*pos];

    bool matched = false;  // Track if we found a valid selector

    // Parse based on token type
    if (token->type == CSS_TOKEN_IDENT || token->type == CSS_TOKEN_CUSTOM_PROPERTY) {
        // Element/type selector: div, span, --foo, etc.
        selector->type = CSS_SELECTOR_TYPE_ELEMENT;
        // Extract selector value from token
        if (token->value) {
            selector->value = pool_strdup(pool, token->value);
        } else if (token->start && token->length > 0) {
            char* value_buf = (char*)pool_calloc(pool, token->length + 1);
            if (value_buf) {
                memcpy(value_buf, token->start, token->length);
                value_buf[token->length] = '\0';
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
                char* value_buf = (char*)pool_calloc(pool, name_token->length + 1);
                if (value_buf) {
                    memcpy(value_buf, name_token->start, name_token->length);
                    value_buf[name_token->length] = '\0';
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
            char* value_buf = (char*)pool_calloc(pool, length + 1);
            if (value_buf) {
                memcpy(value_buf, start, length);
                value_buf[length] = '\0';
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
        // Functional pseudo-class like nth-child(1)
        // Token value contains the function name without the opening parenthesis
        log_debug("[CSS Parser] Detected CSS_TOKEN_FUNCTION: '%s'", token->value ? token->value : "(null)");
        const char* func_name = token->value;
        if (!func_name && token->start && token->length > 0) {
            char* name_buf = (char*)pool_calloc(pool, token->length + 1);
            if (name_buf) {
                memcpy(name_buf, token->start, token->length);
                name_buf[token->length] = '\0';
                func_name = name_buf;
            }
        }

        // Function tokens include the opening '(', strip it
        size_t func_len = func_name ? strlen(func_name) : 0;
        if (func_len > 0 && func_name[func_len - 1] == '(') {
            char* clean_name = (char*)pool_calloc(pool, func_len);
            if (clean_name) {
                memcpy(clean_name, func_name, func_len - 1);
                clean_name[func_len - 1] = '\0';
                func_name = clean_name;
            }
        }

        (*pos)++; // skip FUNCTION token

        // Collect tokens until matching ')' or EOF (unclosed parens implicitly close at EOF)
        int arg_start = *pos;
        int paren_depth = 1; // Already inside the function
        while (*pos < token_count && paren_depth > 0) {
            if (tokens[*pos].type == CSS_TOKEN_EOF) break;
            if (tokens[*pos].type == CSS_TOKEN_LEFT_PAREN) {
                paren_depth++;
            } else if (tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                paren_depth--;
                if (paren_depth == 0) break;
            }
            (*pos)++;
        }

        // Extract argument as string
        char* pseudo_arg = NULL;
        int arg_end = *pos; // position of ')' or end
        if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
            arg_end = *pos;
            // Build argument string from tokens
            size_t arg_len = 0;
            for (int i = arg_start; i < arg_end; i++) {
                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                if (tokens[i].value) {
                    arg_len += strlen(tokens[i].value);
                } else if (tokens[i].length > 0) {
                    arg_len += tokens[i].length;
                }
            }

            if (arg_len > 0) {
                pseudo_arg = (char*)pool_calloc(pool, arg_len + 1);
                if (pseudo_arg) {
                    char* p = pseudo_arg;
                    for (int i = arg_start; i < arg_end; i++) {
                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[i].value) {
                            size_t len = strlen(tokens[i].value);
                            memcpy(p, tokens[i].value, len);
                            p += len;
                        } else if (tokens[i].start && tokens[i].length > 0) {
                            memcpy(p, tokens[i].start, tokens[i].length);
                            p += tokens[i].length;
                        }
                    }
                    *p = '\0';
                }
            }

            (*pos)++; // skip ')'
        }

        // Map function name to pseudo-class type
        selector->type = css_functional_pseudo_type(func_name);
        if (selector->type == CSS_SELECTOR_PSEUDO_IS &&
            (strcmp(func_name, "host") == 0 || strcmp(func_name, "host-context") == 0)) {
            log_debug(" Shadow DOM function: '%s()'", func_name);
        }

        selector->value = func_name;
        selector->argument = pseudo_arg;

        // For nth-* pseudo-classes: validate An+B and populate nth_formula
        if (css_is_nth_pseudo(selector->type)) {
            if (!css_parse_anb_from_tokens(tokens, arg_start, arg_end, &selector->nth_formula)) {
                // invalid An+B → parse error: reject this selector
                log_debug("[CSS Parser] An+B parse error for '%s(%s)'",
                         func_name, pseudo_arg ? pseudo_arg : "");
                // matched stays false → caller treats as parse error
            } else {
                log_debug(" Functional pseudo-class: '%s(%s)'",
                       func_name, pseudo_arg ? pseudo_arg : "");
                matched = true;
            }
        } else {
            // For :not(), :is(), :where(), :has(): parse argument tokens as selector list
            if (selector->type == CSS_SELECTOR_PSEUDO_NOT ||
                selector->type == CSS_SELECTOR_PSEUDO_IS ||
                selector->type == CSS_SELECTOR_PSEUDO_WHERE ||
                selector->type == CSS_SELECTOR_PSEUDO_HAS) {
                int sub_pos = arg_start;
                CssSelectorGroup* sub_group = css_parse_selector_group_from_tokens(tokens, &sub_pos, arg_end, pool);
                if (sub_group && sub_group->selector_count > 0) {
                    selector->function_selectors = sub_group->selectors;
                    selector->function_selector_count = sub_group->selector_count;
                }
            }

            log_debug(" Functional pseudo-class: '%s(%s)'",
                   func_name, pseudo_arg ? pseudo_arg : "");
            matched = true;
        }
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
                    const char* elem_name = tokens[*pos].value;
                    if (!elem_name && tokens[*pos].start && tokens[*pos].length > 0) {
                        char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
                        if (name_buf) {
                            memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
                            name_buf[tokens[*pos].length] = '\0';
                            elem_name = name_buf;
                        }
                    }

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
                    } else if (strcmp(elem_name, "placeholder") == 0) {
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
                }
                if (!matched) {
                    return NULL;
                }
            }

            // Single colon - pseudo-class or legacy pseudo-element
            else if (pseudo_token->type == CSS_TOKEN_IDENT) {
                const char* pseudo_name = pseudo_token->value;
                if (!pseudo_name && pseudo_token->start && pseudo_token->length > 0) {
                    char* name_buf = (char*)pool_calloc(pool, pseudo_token->length + 1);
                    if (name_buf) {
                        memcpy(name_buf, pseudo_token->start, pseudo_token->length);
                        name_buf[pseudo_token->length] = '\0';
                        pseudo_name = name_buf;
                    }
                }

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
                } else if (strcmp(pseudo_name, "enabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_ENABLED;
                } else if (strcmp(pseudo_name, "disabled") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_DISABLED;
                } else if (strcmp(pseudo_name, "checked") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_CHECKED;
                } else if (strcmp(pseudo_name, "indeterminate") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INDETERMINATE;
                } else if (strcmp(pseudo_name, "valid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_VALID;
                } else if (strcmp(pseudo_name, "invalid") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_INVALID;
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
                } else if (strcmp(pseudo_name, "in-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_IN_RANGE;
                } else if (strcmp(pseudo_name, "out-of-range") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_OUT_OF_RANGE;
                } else if (strcmp(pseudo_name, "target") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_TARGET;
                } else if (strcmp(pseudo_name, "scope") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_SCOPE;
                } else if (strcmp(pseudo_name, "fullscreen") == 0) {
                    selector->type = CSS_SELECTOR_PSEUDO_FULLSCREEN;
                } else {
                    // Accept unknown pseudo-classes but use a generic type
                    selector->type = CSS_SELECTOR_PSEUDO_HOVER; // default fallback
                    log_debug(" Generic pseudo-class: ':%s'", pseudo_name);
                }

                    selector->value = pseudo_name;
                    selector->argument = NULL;

                    log_debug(" Simple pseudo-class: ':%s'", pseudo_name);
                    matched = true;
                } // end if (!matched)

            } else if (pseudo_token->type == CSS_TOKEN_FUNCTION) {
                // Functional pseudo-class after colon: :nth-child(...)
                const char* func_name = pseudo_token->value;
                if (!func_name && pseudo_token->start && pseudo_token->length > 0) {
                    char* name_buf = (char*)pool_calloc(pool, pseudo_token->length + 1);
                    if (name_buf) {
                        memcpy(name_buf, pseudo_token->start, pseudo_token->length);
                        name_buf[pseudo_token->length] = '\0';
                        func_name = name_buf;
                    }
                }

                // Function tokens include the opening '(', strip it
                size_t func_len = func_name ? strlen(func_name) : 0;
                if (func_len > 0 && func_name[func_len - 1] == '(') {
                    char* clean_name = (char*)pool_calloc(pool, func_len);
                    if (clean_name) {
                        memcpy(clean_name, func_name, func_len - 1);
                        clean_name[func_len - 1] = '\0';
                        func_name = clean_name;
                    }
                }

                (*pos)++; // skip FUNCTION token

                // Collect tokens until matching ')' or EOF (unclosed parens implicitly close at EOF)
                int arg_start = *pos;
                int paren_depth = 1; // Already inside the function
                while (*pos < token_count && paren_depth > 0) {
                    if (tokens[*pos].type == CSS_TOKEN_EOF) break;
                    if (tokens[*pos].type == CSS_TOKEN_LEFT_PAREN) {
                        paren_depth++;
                    } else if (tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                        paren_depth--;
                        if (paren_depth == 0) break;
                    }
                    (*pos)++;
                }

                // Extract argument as string
                char* pseudo_arg = NULL;
                int arg_end = *pos; // position of ')' or end
                if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_PAREN) {
                    arg_end = *pos;
                    // Build argument string from tokens
                    size_t arg_len = 0;
                    for (int i = arg_start; i < arg_end; i++) {
                        if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[i].value) {
                            arg_len += strlen(tokens[i].value);
                        } else if (tokens[i].length > 0) {
                            arg_len += tokens[i].length;
                        }
                    }

                    if (arg_len > 0) {
                        pseudo_arg = (char*)pool_calloc(pool, arg_len + 1);
                        if (pseudo_arg) {
                            char* p = pseudo_arg;
                            for (int i = arg_start; i < arg_end; i++) {
                                if (tokens[i].type == CSS_TOKEN_WHITESPACE) continue;
                                if (tokens[i].value) {
                                    size_t len = strlen(tokens[i].value);
                                    memcpy(p, tokens[i].value, len);
                                    p += len;
                                } else if (tokens[i].start && tokens[i].length > 0) {
                                    memcpy(p, tokens[i].start, tokens[i].length);
                                    p += tokens[i].length;
                                }
                            }
                            *p = '\0';
                        }
                    }

                    (*pos)++; // skip ')'
                }

                // Map function name to pseudo-class type
                selector->type = css_functional_pseudo_type(func_name);
                if (selector->type == CSS_SELECTOR_PSEUDO_IS &&
                    (strcmp(func_name, "host") == 0 || strcmp(func_name, "host-context") == 0)) {
                    log_debug(" Shadow DOM pseudo-class: ':%s()'", func_name);
                }

                selector->value = func_name;
                selector->argument = pseudo_arg;

                // For nth-* pseudo-classes: validate An+B and populate nth_formula
                if (css_is_nth_pseudo(selector->type)) {
                    if (!css_parse_anb_from_tokens(tokens, arg_start, arg_end, &selector->nth_formula)) {
                        log_debug("[CSS Parser] An+B parse error for ':%s(%s)'",
                                 func_name, pseudo_arg ? pseudo_arg : "");
                        // matched stays false → caller treats as parse error
                    } else {
                        log_debug(" Functional pseudo-class after colon: ':%s(%s)'",
                               func_name, pseudo_arg ? pseudo_arg : "");
                        matched = true;
                    }
                } else {
                    // For :not(), :is(), :where(), :has(): parse argument tokens as selector list
                    if (selector->type == CSS_SELECTOR_PSEUDO_NOT ||
                        selector->type == CSS_SELECTOR_PSEUDO_IS ||
                        selector->type == CSS_SELECTOR_PSEUDO_WHERE ||
                        selector->type == CSS_SELECTOR_PSEUDO_HAS) {
                        int sub_pos = arg_start;
                        CssSelectorGroup* sub_group = css_parse_selector_group_from_tokens(tokens, &sub_pos, arg_end, pool);
                        if (sub_group && sub_group->selector_count > 0) {
                            selector->function_selectors = sub_group->selectors;
                            selector->function_selector_count = sub_group->selector_count;
                        }
                    }

                    log_debug(" Functional pseudo-class after colon: ':%s(%s)'",
                           func_name, pseudo_arg ? pseudo_arg : "");
                    matched = true;
                }
            }
        }
    } else if (token->type == CSS_TOKEN_LEFT_BRACKET) {
        // Attribute selector: [attr], [attr=value], [attr~=value], etc.
        (*pos)++; // skip '['

        // Skip whitespace
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
        if (*pos >= token_count) return NULL;

        // Expect attribute name (IDENT)
        if (tokens[*pos].type != CSS_TOKEN_IDENT) {
            log_debug("[CSS Parser] Expected attribute name, got token type %d", tokens[*pos].type);
            return NULL;
        }

        const char* attr_name = tokens[*pos].value;
        if (!attr_name && tokens[*pos].start && tokens[*pos].length > 0) {
            char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
            if (name_buf) {
                memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
                name_buf[tokens[*pos].length] = '\0';
                attr_name = name_buf;
            }
        }
        (*pos)++;

        // Skip whitespace
        *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);
        if (*pos >= token_count) return NULL;

        // Check for operator or closing bracket
        CssSelectorType attr_type = CSS_SELECTOR_ATTR_EXISTS;
        const char* attr_value = NULL;
        bool case_insensitive = false;

        if (tokens[*pos].type == CSS_TOKEN_RIGHT_BRACKET) {
            // Simple attribute exists selector: [attr]
            (*pos)++;
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

            // Skip whitespace
            *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

            // Get attribute value if present
            if (*pos < token_count && (tokens[*pos].type == CSS_TOKEN_STRING || tokens[*pos].type == CSS_TOKEN_IDENT)) {
                if (tokens[*pos].type == CSS_TOKEN_STRING) {
                    // String token - strip quotes
                    const char* str_val = tokens[*pos].value;
                    if (str_val && (str_val[0] == '"' || str_val[0] == '\'')) {
                        size_t len = strlen(str_val);
                        if (len >= 2) {
                            char* val_buf = (char*)pool_calloc(pool, len - 1);
                            if (val_buf) {
                                memcpy(val_buf, str_val + 1, len - 2);
                                val_buf[len - 2] = '\0';
                                attr_value = val_buf;
                            }
                        }
                    } else {
                        attr_value = pool_strdup(pool, str_val);
                    }
                } else {
                    // IDENT token
                    attr_value = tokens[*pos].value;
                    if (!attr_value && tokens[*pos].start && tokens[*pos].length > 0) {
                        char* val_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
                        if (val_buf) {
                            memcpy(val_buf, tokens[*pos].start, tokens[*pos].length);
                            val_buf[tokens[*pos].length] = '\0';
                            attr_value = val_buf;
                        }
                    }
                }
                (*pos)++;
            }

            // Skip whitespace
            *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

            // Check for case insensitivity flag 'i' or 's'
            if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_IDENT) {
                const char* flag = tokens[*pos].value;
                if (flag && (strcmp(flag, "i") == 0 || strcmp(flag, "I") == 0)) {
                    case_insensitive = true;
                    (*pos)++;
                } else if (flag && (strcmp(flag, "s") == 0 || strcmp(flag, "S") == 0)) {
                    // Case sensitive (default)
                    (*pos)++;
                }
            }

            // Skip whitespace
            *pos = css_skip_whitespace_tokens(tokens, *pos, token_count);

            // Expect closing bracket
            if (*pos < token_count && tokens[*pos].type == CSS_TOKEN_RIGHT_BRACKET) {
                (*pos)++;
            }
        }

        selector->type = attr_type;
        selector->attribute.name = attr_name;
        selector->attribute.value = attr_value;
        selector->attribute.case_insensitive = case_insensitive;

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

// Helper: Parse CSS declaration from tokens
CssDeclaration* css_parse_declaration_from_tokens(const CssToken* tokens, int* pos, int token_count, Pool* pool) {
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
    const char* property_name;
    if (tokens[*pos].value) {
        property_name = tokens[*pos].value;
    } else if (tokens[*pos].start && tokens[*pos].length > 0) {
        // Create null-terminated string from start/length
        char* name_buf = (char*)pool_calloc(pool, tokens[*pos].length + 1);
        if (!name_buf) return NULL;
        memcpy(name_buf, tokens[*pos].start, tokens[*pos].length);
        name_buf[tokens[*pos].length] = '\0';
        property_name = name_buf;
    } else {
        log_debug("[CSS Parser] No property name in token");
        return NULL;
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

        // Stop at unexpected colon (indicates malformed CSS like "width: 600px: height: 100px;")
        // Colons should only appear after property names, not in values
        if (t == CSS_TOKEN_COLON) {
            log_debug("[CSS Parser] Unexpected colon in value, stopping parse (malformed CSS)");
            return NULL;
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

    // CSS §5.4.5: declarations containing bad-string or bad-url tokens must be dropped
    if (has_bad_token) {
        log_debug("[CSS Parser] Dropping declaration '%s': contains bad-string or bad-url token", property_name);
        return NULL;
    }

    // For custom properties, validate bracket matching
    // Per CSS spec, <declaration-value> requires balanced (), [], {} pairs
    bool is_custom_prop = (property_name[0] == '-' && property_name[1] == '-');
    if (is_custom_prop && bracket_mismatch) {
        log_debug("[CSS Parser] Custom property '%s' has mismatched brackets — invalid", property_name);
        return NULL;
    }

    // Create declaration
    CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
    if (!decl) return NULL;

    // Get property ID from name
    decl->property_id = css_property_id_from_name(property_name);

    // Store original property name (important for vendor-prefixed properties where property_id = -1)
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
            char* raw_buf = (char*)pool_calloc(pool, raw_len + 1);
            if (raw_buf) {
                memcpy(raw_buf, raw_start, raw_len);
                raw_buf[raw_len] = '\0';
                decl->value_text = raw_buf;
                decl->value_text_len = raw_len;
            }
        }
    }

    // Debug: Print property name and ID for troubleshooting
    log_debug("[CSS Parser] Property: '%s' -> ID: %d, important=%d, value_count=%d",
            property_name, decl->property_id, is_important, value_count);

    decl->important = is_important;
    decl->valid = true;
    decl->ref_count = 1;

    // Special handling for font-family: combine multi-word font names
    if (decl->property_id == CSS_PROPERTY_FONT_FAMILY && value_count > 1) {
        decl->value = css_parse_font_family_values(tokens, value_start, *pos, pool);
    }
    // Create value(s) from tokens
    else if (value_count == 1) {
        // Single value - create directly
        int i = value_start;
        while (i < *pos) {
            if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
                i++;
                continue;
            }

            CssValue* value = NULL;

            // Handle function tokens specially
            if (tokens[i].type == CSS_TOKEN_FUNCTION) {
                int func_pos = i;
                value = css_parse_function_from_tokens(tokens, &func_pos, *pos, pool);
                i = func_pos;  // advance past the function
            } else {
                value = css_parse_token_to_value(&tokens[i], pool);
                i++;
            }

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

            // Allocate array of pointers to CssValue
            list_value->data.list.values = (CssValue**)pool_calloc(pool, sizeof(CssValue*) * value_count);
            if (!list_value->data.list.values) return NULL;

            int list_idx = 0;
            int i = value_start;
            while (i < *pos && list_idx < value_count) {
                if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
                    i++;
                    continue;
                }

                CssValue* value = NULL;

                // Handle function tokens specially
                if (tokens[i].type == CSS_TOKEN_FUNCTION) {
                    int func_pos = i;
                    value = css_parse_function_from_tokens(tokens, &func_pos, *pos, pool);
                    i = func_pos;  // advance past the function
                } else {
                    value = css_parse_token_to_value(&tokens[i], pool);
                    i++;
                }

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
               decl->property_id, decl->value->type);
        if (decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            log_debug("[CSS Parse]   Length value = %.2f", decl->value->data.length.value);
        }
    }

    // Validate the parsed value before returning
    if (decl->value) {
        // Check if this property disallows negative values
        bool disallow_negative = false;
        switch (decl->property_id) {
            // width, height, and their min/max variants cannot be negative
            case CSS_PROPERTY_WIDTH:
            case CSS_PROPERTY_HEIGHT:
            case CSS_PROPERTY_MIN_WIDTH:
            case CSS_PROPERTY_MIN_HEIGHT:
            case CSS_PROPERTY_MAX_WIDTH:
            case CSS_PROPERTY_MAX_HEIGHT:
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
                       decl->property_id);
                return NULL;
            }
        }
    }

    // Validate: for standard properties, {}-blocks are only valid if the entire
    // value is wrapped in a single {} block (per CSS syntax spec / csswg-drafts#9317).
    // Custom properties (--*) allow arbitrary {}-blocks.
    if (decl->value && decl->property_id > 0) {
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

    return decl;
}

// Enhanced rule parsing from tokens (returns number of tokens consumed, or 0 on error)
int css_parse_rule_from_tokens_internal(const CssToken* tokens, int token_count, Pool* pool, CssRule** out_rule) {
    if (!tokens || token_count <= 0 || !pool || !out_rule) return 0;

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
        const char* keyword_name = at_keyword;
        if (keyword_name && keyword_name[0] == '@') {
            keyword_name++;
        }

        // Create rule structure
        CssRule* rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
        if (!rule) {
            log_debug(" ERROR: Failed to allocate rule");
            return 0;
        }
        rule->pool = pool;

        // Determine rule type and parse accordingly
        if (keyword_name && (strcmp(keyword_name, "media") == 0 ||
                          strcmp(keyword_name, "supports") == 0 ||
                          strcmp(keyword_name, "container") == 0)) {
            // Conditional at-rules: @media, @supports, @container
            rule->type = strcmp(keyword_name, "media") == 0 ? CSS_RULE_MEDIA :
                        strcmp(keyword_name, "supports") == 0 ? CSS_RULE_SUPPORTS :
                        CSS_RULE_CONTAINER;            // Parse condition (everything until '{')
            int cond_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_LEFT_BRACE) {
                pos++;
            }

            // Extract condition text
            if (pos > cond_start) {
                size_t cond_length = 0;
                for (int i = cond_start; i < pos; i++) {
                    if (tokens[i].value) {
                        cond_length += strlen(tokens[i].value) + 1; // +1 for space
                    }
                }

                char* condition = (char*)pool_alloc(pool, cond_length + 1);
                if (condition) {
                    condition[0] = '\0';
                    size_t condition_len = 0;
                    for (int i = cond_start; i < pos; i++) {
                        if (tokens[i].value) {
                            if (condition_len > 0) condition_len = str_cat(condition, condition_len, cond_length + 1, " ", 1);
                            condition_len = str_cat(condition, condition_len, cond_length + 1, tokens[i].value, strlen(tokens[i].value));
                        }
                    }
                    rule->data.conditional_rule.condition = condition;
                }
            }

            // Parse block with nested rules
            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                pos++; // consume '{'

                // Parse nested rules
                int nested_capacity = 4;
                rule->data.conditional_rule.rules = (CssRule**)pool_calloc(pool,
                    nested_capacity * sizeof(CssRule*));
                rule->data.conditional_rule.rule_count = 0;

                while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                    // Skip whitespace
                    pos = css_skip_whitespace_tokens(tokens, pos, token_count);
                    if (pos >= token_count || tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) break;

                    // Recursively parse nested rule
                    CssRule* nested_rule = NULL;
                    int nested_consumed = css_parse_rule_from_tokens_internal(
                        tokens + pos, token_count - pos, pool, &nested_rule);

                    if (nested_consumed > 0) {
                        pos += nested_consumed;
                        if (nested_rule) {
                            // Expand array if needed
                            if (rule->data.conditional_rule.rule_count >= nested_capacity) {
                                nested_capacity *= 2;
                                CssRule** new_rules = (CssRule**)pool_alloc(pool,
                                    nested_capacity * sizeof(CssRule*));
                                memcpy(new_rules, rule->data.conditional_rule.rules,
                                    rule->data.conditional_rule.rule_count * sizeof(CssRule*));
                                rule->data.conditional_rule.rules = new_rules;
                            }
                            rule->data.conditional_rule.rules[rule->data.conditional_rule.rule_count++] = nested_rule;
                        }
                    } else {
                        // CSS error recovery: skip the unparseable rule and continue
                        // parsing remaining nested rules (e.g., @page inside @media).
                        // Look for the next rule boundary (closing brace or semicolon).
                        int brace_depth = 0;
                        bool found_block = false;
                        while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                            if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                                found_block = true;
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
                        // if we stopped at a RIGHT_BRACE without finding a block/semicolon,
                        // that's the parent @media's closing brace — don't consume it
                    }
                }

                if (pos < token_count && tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) {
                    pos++; // consume '}'
                }
            }

            *out_rule = rule;
            log_debug(" Parsed conditional @-rule with %zu nested rules",
                rule->data.conditional_rule.rule_count);
            return pos - start_pos;

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
                    for (int ti = value_start; ti < pos; ti++) {
                        if (tokens[ti].type == CSS_TOKEN_WHITESPACE) continue;
                        if (tokens[ti].type == CSS_TOKEN_STRING) {
                            // @import 'file.css' or @import "file.css"
                            import_url = tokens[ti].value;
                            break;
                        }
                        if (tokens[ti].type == CSS_TOKEN_URL) {
                            // @import url(file.css) — unquoted URL token
                            import_url = tokens[ti].value;
                            break;
                        }
                        if (tokens[ti].type == CSS_TOKEN_FUNCTION && tokens[ti].value &&
                            strcmp(tokens[ti].value, "url(") == 0) {
                            // @import url('file.css') — FUNCTION + STRING + RIGHT_PAREN
                            for (int tj = ti + 1; tj < pos; tj++) {
                                if (tokens[tj].type == CSS_TOKEN_WHITESPACE) continue;
                                if (tokens[tj].type == CSS_TOKEN_STRING ||
                                    tokens[tj].type == CSS_TOKEN_IDENT) {
                                    import_url = tokens[tj].value;
                                    break;
                                }
                                break;
                            }
                            break;
                        }
                        break; // unknown token pattern
                    }
                    rule->data.import_rule.url = import_url;
                    if (import_url) {
                        log_debug(" @import URL extracted: '%s'", import_url);
                    }
                } else if (tokens[value_start].value) {
                    rule->data.charset_rule.charset = tokens[value_start].value;
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
            rule->data.generic_rule.name = keyword_name;

            // Build prefix content (e.g., animation name for @keyframes)
            // This is everything between the at-keyword and the opening brace
            int prefix_start = pos;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_LEFT_BRACE &&
                   tokens[pos].type != CSS_TOKEN_SEMICOLON) {
                pos++;
            }
            int prefix_end = pos;

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                int brace_start = pos;
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
    }    // Parse selector(s) using enhanced parser (supports compound, descendant, and comma-separated selectors)
    log_debug(" Parsing selectors at position %d", pos);

    // Parse selector group (handles single selectors and comma-separated groups)
    CssSelectorGroup* selector_group = css_parse_selector_group_from_tokens(tokens, &pos, token_count, pool);
    if (!selector_group) {
        log_debug(" ERROR: Failed to parse selector group");
        return 0;
    }

    log_debug(" Parsed selector group with %zu selector(s)", selector_group->selector_count);

    // Skip whitespace
    pos = css_skip_whitespace_tokens(tokens, pos, token_count);

    // Expect opening brace
    if (pos >= token_count || tokens[pos].type != CSS_TOKEN_LEFT_BRACE) {
        log_debug(" ERROR: Expected '{' but got token type %d at position %d",
                pos < token_count ? tokens[pos].type : -1, pos);
        return 0;
    }
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

        // Skip @-rules inside declaration blocks (e.g., @at {} or @at;)
        if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
            pos++; // skip @keyword
            // skip until block or semicolon
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
            // Parse nested qualified rule: consume prelude until '{', then consume block
            int prelude_start = pos;
            bool has_ampersand = false;

            // Check for leading '&' nesting selector
            if (cur_type == CSS_TOKEN_DELIM && tokens[pos].data.delimiter == '&') {
                has_ampersand = true;
            }

            // Skip prelude tokens until '{' (respecting nested brackets)
            int nest_paren = 0, nest_bracket = 0;
            while (pos < token_count && tokens[pos].type != CSS_TOKEN_RIGHT_BRACE) {
                if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE && nest_paren == 0 && nest_bracket == 0) break;
                if (tokens[pos].type == CSS_TOKEN_LEFT_PAREN) nest_paren++;
                else if (tokens[pos].type == CSS_TOKEN_RIGHT_PAREN && nest_paren > 0) nest_paren--;
                else if (tokens[pos].type == CSS_TOKEN_LEFT_BRACKET) nest_bracket++;
                else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACKET && nest_bracket > 0) nest_bracket--;
                pos++;
            }

            if (pos < token_count && tokens[pos].type == CSS_TOKEN_LEFT_BRACE) {
                // Consume the block (match braces)
                int block_start = pos;
                pos++; // skip '{'
                int block_depth = 1;
                while (pos < token_count && block_depth > 0) {
                    if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) block_depth++;
                    else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) block_depth--;
                    pos++;
                }

                // Try to parse prelude as a selector group
                // If prelude starts with '&', skip it for selector parsing
                int sel_start = prelude_start;
                if (has_ampersand) {
                    sel_start++; // skip '&' token
                    sel_start = css_skip_whitespace_tokens(tokens, sel_start, token_count);
                }

                int sel_pos = sel_start;
                CssSelectorGroup* nested_sg = NULL;
                if (sel_pos < block_start) {
                    nested_sg = css_parse_selector_group_from_tokens(tokens, &sel_pos, block_start, pool);
                }

                // Verify entire prelude was consumed (skip trailing whitespace)
                int after_sel = css_skip_whitespace_tokens(tokens, sel_pos, block_start);
                if (nested_sg && nested_sg->selector_count > 0 && after_sel >= block_start) {
                    // Valid nested rule — create it
                    CssRule* nested = (CssRule*)pool_calloc(pool, sizeof(CssRule));
                    nested->type = CSS_RULE_STYLE;
                    nested->pool = pool;
                    nested->data.style_rule.selector_group = nested_sg;
                    nested->data.style_rule.selector = nested_sg->selectors[0];

                    // Parse declarations inside the nested block
                    int inner_pos = block_start + 1; // skip '{'
                    int inner_end = pos - 1; // before '}'
                    CssDeclaration** inner_decls = NULL;
                    int inner_count = 0;
                    int inner_cap = 4;
                    inner_decls = (CssDeclaration**)pool_calloc(pool, inner_cap * sizeof(CssDeclaration*));
                    while (inner_pos < inner_end) {
                        inner_pos = css_skip_whitespace_tokens(tokens, inner_pos, inner_end);
                        if (inner_pos >= inner_end) break;

                        // skip a nested-of-nested qualified rule's brace block to avoid
                        // the declaration parser stalling on its tokens. proper recursive
                        // nested-rule handling can be added later; for now drop the inner
                        // block so we keep forward progress.
                        if (tokens[inner_pos].type == CSS_TOKEN_LEFT_BRACE) {
                            int depth = 1;
                            inner_pos++;
                            while (inner_pos < inner_end && depth > 0) {
                                if (tokens[inner_pos].type == CSS_TOKEN_LEFT_BRACE) depth++;
                                else if (tokens[inner_pos].type == CSS_TOKEN_RIGHT_BRACE) depth--;
                                inner_pos++;
                            }
                            continue;
                        }

                        int before_pos = inner_pos;
                        CssDeclaration* idecl = css_parse_declaration_from_tokens(tokens, &inner_pos, inner_end, pool);
                        if (idecl) {
                            if (inner_count >= inner_cap) {
                                inner_cap *= 2;
                                CssDeclaration** nd = (CssDeclaration**)pool_calloc(pool, inner_cap * sizeof(CssDeclaration*));
                                memcpy(nd, inner_decls, inner_count * sizeof(CssDeclaration*));
                                inner_decls = nd;
                            }
                            inner_decls[inner_count++] = idecl;
                        }
                        if (inner_pos < inner_end && tokens[inner_pos].type == CSS_TOKEN_SEMICOLON) inner_pos++;
                        // guarantee forward progress: if the declaration parser made no
                        // progress (e.g., stuck on a RIGHT_BRACE inside a nested block),
                        // skip one token to avoid an infinite loop.
                        if (inner_pos == before_pos) inner_pos++;
                    }
                    nested->data.style_rule.declarations = inner_decls;
                    nested->data.style_rule.declaration_count = inner_count;

                    // Add to nested_rules array
                    if (nested_rule_count >= nested_rule_capacity) {
                        nested_rule_capacity = (nested_rule_capacity == 0) ? 4 : nested_rule_capacity * 2;
                        CssRule** nr = (CssRule**)pool_calloc(pool, nested_rule_capacity * sizeof(CssRule*));
                        if (nested_rule_count > 0) memcpy(nr, nested_rules, nested_rule_count * sizeof(CssRule*));
                        nested_rules = nr;
                    }
                    nested_rules[nested_rule_count++] = nested;
                    seen_nested_rule = true;
                    log_debug(" Parsed nested rule with %d declarations", inner_count);
                } else {
                    // Invalid nested rule — discard it (error recovery)
                    log_debug(" Discarded invalid nested rule (bad selector)");
                }
            } else {
                // No '{' found — skip to end of outer block
                // (pos is already at '}' or past token_count)
            }
            continue;
        }

        // Parse declaration
        CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, token_count, pool);
        log_debug(" After parsing: decl=%p", (void*)decl);
        if (decl) {
            log_debug(" Parsed declaration: property_id=%d for position %d",
                    decl->property_id, decl_count);

            if (seen_nested_rule) {
                // Declaration after nested rule → collect for CSSNestedDeclarations
                if (post_nested_count >= post_nested_capacity) {
                    post_nested_capacity = (post_nested_capacity == 0) ? 4 : post_nested_capacity * 2;
                    CssDeclaration** nd = (CssDeclaration**)pool_calloc(pool, post_nested_capacity * sizeof(CssDeclaration*));
                    if (post_nested_count > 0) memcpy(nd, post_nested_decls, post_nested_count * sizeof(CssDeclaration*));
                    post_nested_decls = nd;
                }
                post_nested_decls[post_nested_count++] = decl;
            } else {
                // Expand array if needed
                if (decl_count >= decl_capacity) {
                    decl_capacity *= 2;
                    CssDeclaration** new_decls = (CssDeclaration**)pool_calloc(pool, decl_capacity * sizeof(CssDeclaration*));
                    memcpy(new_decls, declarations, decl_count * sizeof(CssDeclaration*));
                    declarations = new_decls;
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
    if (post_nested_count > 0) {
        CssRule* nd_rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
        nd_rule->type = CSS_RULE_NESTED_DECLARATIONS;
        nd_rule->pool = pool;
        nd_rule->data.style_rule.declarations = post_nested_decls;
        nd_rule->data.style_rule.declaration_count = post_nested_count;
        // Add to nested_rules
        if (nested_rule_count >= nested_rule_capacity) {
            nested_rule_capacity = (nested_rule_capacity == 0) ? 4 : nested_rule_capacity * 2;
            CssRule** nr = (CssRule**)pool_calloc(pool, nested_rule_capacity * sizeof(CssRule*));
            if (nested_rule_count > 0) memcpy(nr, nested_rules, nested_rule_count * sizeof(CssRule*));
            nested_rules = nr;
        }
        nested_rules[nested_rule_count++] = nd_rule;
    }

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
    CssRule* rule = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (!rule) return 0;

    rule->type = CSS_RULE_STYLE;
    rule->pool = pool;
    rule->data.style_rule.selector_group = selector_group;
    // For backward compatibility, store the first selector in the single selector field
    rule->data.style_rule.selector = (selector_group->selector_count > 0) ? selector_group->selectors[0] : NULL;
    rule->data.style_rule.declarations = declarations;
    rule->data.style_rule.declaration_count = decl_count;
    rule->data.style_rule.nested_rules = nested_rules;
    rule->data.style_rule.nested_rule_count = nested_rule_count;

    // Set parent pointer on nested rules
    for (int i = 0; i < nested_rule_count; i++) {
        if (nested_rules[i]) nested_rules[i]->parent = rule;
    }

    log_debug(" Created rule with %d declarations:", decl_count);
    for (int i = 0; i < decl_count && i < 5; i++) {
        if (declarations[i]) {
            log_debug("   Declaration[%d]: property_id=%d", i, declarations[i]->property_id);
        }
    }

    *out_rule = rule;
    return pos - start_pos; // Return number of tokens consumed
}

// Legacy wrapper that returns CssRule* (for compatibility)
CssRule* css_parse_rule_from_tokens(const CssToken* tokens, int token_count, Pool* pool) {
    CssRule* rule = NULL;
    css_parse_rule_from_tokens_internal(tokens, token_count, pool, &rule);
    return rule;
}
