#include "input.hpp"
#include "../../lib/str.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
extern "C" {
#include "../../lib/strview.h"
}

using namespace lambda;

static const int TOML_MAX_DEPTH = 512;

// Forward declarations
static Item parse_value(InputContext& ctx, const char **toml, int *line_num, int depth = 0);
static Array* parse_array(InputContext& ctx, const char **toml, int *line_num, int depth = 0);
static Map* parse_inline_table(InputContext& ctx, const char **toml, int *line_num, int depth = 0);
static String* parse_bare_key(InputContext& ctx, const char **toml);
static String* parse_quoted_key(InputContext& ctx, const char **toml);
static String* parse_literal_key(InputContext& ctx, const char **toml);
static String* parse_key(InputContext& ctx, const char **toml);
static String* parse_basic_string(InputContext& ctx, const char **toml);
static String* parse_literal_string(InputContext& ctx, const char **toml);
static String* parse_multiline_basic_string(InputContext& ctx, const char **toml, int *line_num);
static String* parse_multiline_literal_string(InputContext& ctx, const char **toml, int *line_num);
static Item parse_number(InputContext& ctx, const char **toml);
static bool handle_escape_sequence(InputContext& ctx, StringBuf* sb, const char **toml, bool is_multiline, int *line_num);

// Common function to handle escape sequences in strings
// is_multiline: true for multiline basic strings, false for regular strings
static bool handle_escape_sequence(InputContext& ctx, StringBuf* sb, const char **toml, bool is_multiline, int *line_num) {
    SourceTracker& tracker = ctx.tracker;

    if (**toml != '\\') return false;

    SourceLocation esc_loc = tracker.location();
    (*toml)++; // skip backslash
    tracker.advance(1);

    switch (**toml) {
        case '"': stringbuf_append_char(sb, '"'); break;
        case '\\': stringbuf_append_char(sb, '\\'); break;
        case 'b': stringbuf_append_char(sb, '\b'); break;
        case 'f': stringbuf_append_char(sb, '\f'); break;
        case 'n': stringbuf_append_char(sb, '\n'); break;
        case 'r': stringbuf_append_char(sb, '\r'); break;
        case 't': stringbuf_append_char(sb, '\t'); break;
        case 'u': {
            (*toml)++; // skip 'u'
            tracker.advance(1);

            // Validate we have 4 hex digits
            for (int i = 0; i < 4; i++) {
                if (!isxdigit((unsigned char)*(*toml + i))) {
                    ctx.addError(esc_loc, "Invalid \\u escape sequence: expected 4 hex digits");
                    return false;
                }
            }

            char hex[5] = {0};
            strncpy(hex, *toml, 4);
            (*toml) += 4; // skip 4 hex digits
            tracker.advance(4);

            int codepoint = (int)strtol(hex, NULL, 16);

            // check for surrogate pairs (used for characters > U+FFFF like emojis)
            // high surrogate: 0xD800-0xDBFF, low surrogate: 0xDC00-0xDFFF
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                // this is a high surrogate, look for low surrogate
                if (**toml == '\\' && *(*toml + 1) == 'u') {
                    // validate next 4 hex digits
                    bool valid_low = true;
                    for (int i = 0; i < 4; i++) {
                        if (!isxdigit((unsigned char)*(*toml + 2 + i))) {
                            valid_low = false;
                            break;
                        }
                    }
                    if (valid_low) {
                        char hex_low[5] = {0};
                        strncpy(hex_low, *toml + 2, 4);
                        int low_surrogate = (int)strtol(hex_low, NULL, 16);
                        uint32_t combined = decode_surrogate_pair((uint16_t)codepoint, (uint16_t)low_surrogate);
                        if (combined != 0) {
                            // valid surrogate pair - combine into full codepoint
                            codepoint = (int)combined;
                            (*toml) += 6; // skip \uXXXX for low surrogate
                            tracker.advance(6);
                        } else {
                            // not a valid low surrogate, output replacement char
                            codepoint = 0xFFFD;
                        }
                    } else {
                        // lone high surrogate - output replacement character
                        codepoint = 0xFFFD;
                    }
                } else {
                    // lone high surrogate - output replacement character
                    codepoint = 0xFFFD;
                }
            }

            // convert codepoint to UTF-8
            append_codepoint_utf8(sb, (uint32_t)codepoint);
            (*toml)--; // Back up one since we'll increment at end
            tracker.advance(-1);
        } break;
        case 'U': {
            (*toml)++; // skip 'U'
            tracker.advance(1);

            // Validate we have 8 hex digits
            for (int i = 0; i < 8; i++) {
                if (!isxdigit((unsigned char)*(*toml + i))) {
                    ctx.addError(esc_loc, "Invalid \\U escape sequence: expected 8 hex digits");
                    return false;
                }
            }

            char hex[9] = {0};
            strncpy(hex, *toml, 8);
            (*toml) += 8; // skip 8 hex digits
            tracker.advance(8);

            long codepoint = strtol(hex, NULL, 16);
            if (codepoint > 0x10FFFF) {
                ctx.addError(esc_loc, "Invalid Unicode codepoint: U+%08lX exceeds maximum U+10FFFF", codepoint);
                return false;
            }

            append_codepoint_utf8(sb, (uint32_t)codepoint);
            (*toml)--; // Back up one since we'll increment at end
            tracker.advance(-1);
        } break;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            if (is_multiline) {
                // Line ending backslash - trim whitespace (only in multiline strings)
                while (**toml && (**toml == ' ' || **toml == '\t' || **toml == '\n' || **toml == '\r')) {
                    if (**toml == '\n' && line_num) {
                        (*line_num)++;
                    }
                    (*toml)++;
                    tracker.advance(1);
                }
                (*toml)--; // Back up one since we'll increment at end
                tracker.advance(-1);
            } else {
                ctx.addWarning(esc_loc, "Invalid escape sequence '\\%c' in string", **toml);
                // Treat as literal backslash + character
                stringbuf_append_char(sb, '\\');
                stringbuf_append_char(sb, **toml);
            }
            break;
        default:
            ctx.addWarning(esc_loc, "Unknown escape sequence '\\%c' in string", **toml);
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, **toml);
            break;
    }
    (*toml)++; // move to next character
    tracker.advance(1);
    return true;
}

static void skip_line(const char **toml, int *line_num) {
    while (**toml && **toml != '\n') {
        (*toml)++;
    }
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    }
}

static void skip_tab_pace_and_comments(const char **toml, int *line_num) {
    while (**toml) {
        if (**toml == ' ' || **toml == '\t') {
            (*toml)++;
        } else if (**toml == '#') {
            skip_line(toml, line_num);
        } else if (**toml == '\n' || **toml == '\r') {
            if (**toml == '\r' && *(*toml + 1) == '\n') {
                (*toml)++;
            }
            (*toml)++;
            (*line_num)++;
        } else {
            break;
        }
    }
}

static String* parse_bare_key(InputContext& ctx, const char **toml) {
    SourceTracker& tracker = ctx.tracker;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    const char *start = *toml;
    SourceLocation key_loc = tracker.location();

    // Bare keys can contain A-Z, a-z, 0-9, -, _ (including pure numeric keys)
    while (**toml && (isalnum((unsigned char)**toml) || **toml == '-' || **toml == '_')) {
        (*toml)++;
        tracker.advance(1);
    }
    if (*toml == start) {
        ctx.addError(key_loc, "Empty bare key");
        return NULL; // no valid characters
    }

    int len = *toml - start;
    for (int i = 0; i < len; i++) {
        stringbuf_append_char(sb, start[i]);
    }
    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_quoted_key(InputContext& ctx, const char **toml) {
    SourceTracker& tracker = ctx.tracker;

    if (**toml != '"') return NULL;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation key_loc = tracker.location();

    (*toml)++; // Skip opening quote
    tracker.advance(1);

    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            if (!handle_escape_sequence(ctx, sb, toml, false, NULL)) {
                return NULL;
            }
        } else {
            if (**toml == '\n') {
                ctx.addError(key_loc, "Unterminated quoted key: newline in key");
                return NULL;
            }
            stringbuf_append_char(sb, **toml);
            (*toml)++;
            tracker.advance(1);
        }
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
        tracker.advance(1);
    } else {
        ctx.addError(key_loc, "Unterminated quoted key: missing closing quote");
        return NULL;
    }
    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_literal_key(InputContext& ctx, const char **toml) {
    if (**toml != '\'') return NULL;
    SourceTracker& tracker = ctx.tracker;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation key_loc = tracker.location();

    (*toml)++; // Skip opening quote
    tracker.advance(1);

    while (**toml && **toml != '\'') {
        if (**toml == '\n') {
            ctx.addError(key_loc, "Unterminated literal key: newline in key");
            return NULL;
        }
        stringbuf_append_char(sb, **toml);
        (*toml)++;
        tracker.advance(1);
    }

    if (**toml == '\'') {
        (*toml)++; // skip closing quote
        tracker.advance(1);
    } else {
        ctx.addError(key_loc, "Unterminated literal key: missing closing quote");
        return NULL;
    }
    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_basic_string(InputContext& ctx, const char **toml) {
    SourceTracker& tracker = ctx.tracker;

    if (**toml != '"') return NULL;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation str_loc = tracker.location();

    (*toml)++; // Skip opening quote
    tracker.advance(1);

    while (**toml && **toml != '"') {
        if (**toml == '\\') {
            if (!handle_escape_sequence(ctx, sb, toml, false, NULL)) {
                return NULL;
            }
        } else {
            if (**toml == '\n') {
                ctx.addError(str_loc, "Unterminated basic string: newline in string");
                return NULL;
            }
            stringbuf_append_char(sb, **toml);
            (*toml)++;
            tracker.advance(1);
        }
    }

    if (**toml == '"') {
        (*toml)++; // skip closing quote
        tracker.advance(1);
    } else {
        ctx.addError(str_loc, "Unterminated basic string: missing closing quote");
        return NULL;
    }
    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_literal_string(InputContext& ctx, const char **toml) {
    SourceTracker& tracker = ctx.tracker;

    if (**toml != '\'') return NULL;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation str_loc = tracker.location();

    (*toml)++; // Skip opening quote
    tracker.advance(1);

    while (**toml && **toml != '\'') {
        if (**toml == '\n') {
            ctx.addError(str_loc, "Unterminated literal string: newline in string");
            return NULL;
        }
        stringbuf_append_char(sb, **toml);
        (*toml)++;
        tracker.advance(1);
    }

    if (**toml == '\'') {
        (*toml)++; // skip closing quote
        tracker.advance(1);
    } else {
        ctx.addError(str_loc, "Unterminated literal string: missing closing quote");
        return NULL;
    }
    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_multiline_basic_string(InputContext& ctx, const char **toml, int *line_num) {
    SourceTracker& tracker = ctx.tracker;

    if (strncmp(*toml, "\"\"\"", 3) != 0) return NULL;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation str_loc = tracker.location();

    *toml += 3; // Skip opening triple quotes
    tracker.advance(3);

    // Skip optional newline right after opening quotes
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    } else if (**toml == '\r' && *(*toml + 1) == '\n') {
        *toml += 2;
        tracker.advance(1);
        (*line_num)++;
    }

    bool found_closing = false;
    while (**toml) {
        if (strncmp(*toml, "\"\"\"", 3) == 0) {
            *toml += 3; // Skip closing triple quotes
            tracker.advance(3);
            found_closing = true;
            break;
        }

        if (**toml == '\\') {
            if (!handle_escape_sequence(ctx, sb, toml, true, line_num)) {
                return NULL;
            }
        } else {
            if (**toml == '\n') {
                (*line_num)++;
            }
            stringbuf_append_char(sb, **toml);
            (*toml)++;
            tracker.advance(1);
        }
    }

    if (!found_closing) {
        ctx.addError(str_loc, "Unterminated multiline basic string: missing closing \"\"\"");
        return NULL;
    }

    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_multiline_literal_string(InputContext& ctx, const char **toml, int *line_num) {
    SourceTracker& tracker = ctx.tracker;

    if (strncmp(*toml, "'''", 3) != 0) return NULL;
    MarkBuilder* builder = &ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    SourceLocation str_loc = tracker.location();

    *toml += 3; // Skip opening triple quotes
    tracker.advance(3);

    // Skip optional newline right after opening quotes
    if (**toml == '\n') {
        (*toml)++;
        (*line_num)++;
    } else if (**toml == '\r' && *(*toml + 1) == '\n') {
        *toml += 2;
        tracker.advance(1);
        (*line_num)++;
    }

    bool found_closing = false;
    while (**toml) {
        if (strncmp(*toml, "'''", 3) == 0) {
            *toml += 3; // Skip closing triple quotes
            tracker.advance(3);
            found_closing = true;
            break;
        }

        if (**toml == '\n') {
            (*line_num)++;
        }
        stringbuf_append_char(sb, **toml);
        (*toml)++;
        tracker.advance(1);
    }

    if (!found_closing) {
        ctx.addError(str_loc, "Unterminated multiline literal string: missing closing '''");
        return NULL;
    }

    return builder->createString(sb->str->chars, sb->length);
}

static String* parse_key(InputContext& ctx, const char **toml) {
    if (**toml == '"') {
        return parse_quoted_key(ctx, toml);
    } else if (**toml == '\'') {
        return parse_literal_key(ctx, toml);
    } else {
        return parse_bare_key(ctx, toml);
    }
}

static Item parse_number(InputContext& ctx, const char **toml) {
    SourceTracker& tracker = ctx.tracker;

    Input* input = ctx.input();
    char* end;
    const char *start = *toml;
    SourceLocation num_loc = tracker.location();

    // Handle special float values
    if (strncmp(*toml, "inf", 3) == 0) {
        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for infinity value");
            return {.item = ITEM_ERROR};
        }
        *dval = INFINITY;
        *toml += 3;
        tracker.advance(3);
        return {.item = d2it(dval)};
    }
    if (strncmp(*toml, "-inf", 4) == 0) {
        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for negative infinity value");
            return {.item = ITEM_ERROR};
        }
        *dval = -INFINITY;
        *toml += 4;
        tracker.advance(4);
        return {.item = d2it(dval)};
    }
    if (strncmp(*toml, "nan", 3) == 0) {
        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for NaN value");
            return {.item = ITEM_ERROR};
        }
        *dval = NAN;
        *toml += 3;
        tracker.advance(3);
        return {.item = d2it(dval)};
    }
    if (strncmp(*toml, "-nan", 4) == 0) {
        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for negative NaN value");
            return {.item = ITEM_ERROR};
        }
        *dval = NAN;
        *toml += 4;
        tracker.advance(4);
        return {.item = d2it(dval)};
    }

    // Handle hex, octal, binary integers
    if (**toml == '0' && (*(*toml + 1) == 'x' || *(*toml + 1) == 'X')) {
        int64_t val = strtol(start, &end, 16);
        if (end == start + 2) {
            ctx.addError(num_loc, "Invalid hexadecimal number: no digits after 0x");
            return {.item = ITEM_ERROR};
        }
        int64_t *lval;
        lval = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
        if (lval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for hexadecimal integer");
            return {.item = ITEM_ERROR};
        }
        *lval = val;
        size_t consumed = end - *toml;
        *toml = end;
        tracker.advance(consumed);
        return {.item = l2it(lval)};
    }
    if (**toml == '0' && (*(*toml + 1) == 'o' || *(*toml + 1) == 'O')) {
        int64_t val = strtol(start + 2, &end, 8);
        if (end == start + 2) {
            ctx.addError(num_loc, "Invalid octal number: no digits after 0o");
            return {.item = ITEM_ERROR};
        }
        int64_t *lval;
        lval = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
        if (lval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for octal integer");
            return {.item = ITEM_ERROR};
        }
        *lval = val;
        size_t consumed = end - *toml;
        *toml = end;
        tracker.advance(consumed);
        return {.item = l2it(lval)};
    }
    if (**toml == '0' && (*(*toml + 1) == 'b' || *(*toml + 1) == 'B')) {
        int64_t val = strtol(start + 2, &end, 2);
        if (end == start + 2) {
            ctx.addError(num_loc, "Invalid binary number: no digits after 0b");
            return {.item = ITEM_ERROR};
        }
        int64_t *lval;
        lval = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
        if (lval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for binary integer");
            return {.item = ITEM_ERROR};
        }
        *lval = val;
        size_t consumed = end - *toml;
        *toml = end;
        tracker.advance(consumed);
        return {.item = l2it(lval)};
    }

    // Check if it's a float (contains . or e/E)
    bool is_float = false;
    const char *temp = *toml;

    // Skip sign
    if (*temp == '+' || *temp == '-') {
        temp++;
    }

    // Remove underscores and check for float indicators
    while (*temp && (str_char_is_digit(*temp) || *temp == '.' || *temp == 'e' || *temp == 'E' || *temp == '+' || *temp == '-' || *temp == '_')) {
        if (*temp == '.' || *temp == 'e' || *temp == 'E') {
            is_float = true;
        }
        temp++;
    }

    if (is_float) {
        double *dval;
        dval = (double*)pool_calloc(input->pool, sizeof(double));
        if (dval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for float");
            return {.item = ITEM_ERROR};
        }
        *dval = strtod(start, &end);
        if (end == start) {
            ctx.addError(num_loc, "Invalid float number format");
            return {.item = ITEM_ERROR};
        }
        size_t consumed = end - *toml;
        *toml = end;
        tracker.advance(consumed);
        return {.item = d2it(dval)};
    } else {
        int64_t val = strtol(start, &end, 10);
        if (end == start) {
            ctx.addError(num_loc, "Invalid integer number format");
            return {.item = ITEM_ERROR};
        }
        int64_t *lval;
        lval = (int64_t*)pool_calloc(input->pool, sizeof(int64_t));
        if (lval == NULL) {
            ctx.addError(num_loc, "Memory allocation failed for integer");
            return {.item = ITEM_ERROR};
        }
        *lval = val;
        size_t consumed = end - *toml;
        *toml = end;
        tracker.advance(consumed);
        return {.item = l2it(lval)};
    }
}

static Array* parse_array(InputContext& ctx, const char **toml, int *line_num, int depth) {
    Input* input = ctx.input();

    if (**toml != '[') return NULL;
    if (depth >= TOML_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum TOML nesting depth (%d) exceeded", TOML_MAX_DEPTH);
        return NULL;
    }
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*toml)++; // skip [
    skip_tab_pace_and_comments(toml, line_num);

    if (**toml == ']') {
        (*toml)++;
        return arr;
    }

    while (**toml) {
        Item value = parse_value(ctx, toml, line_num, depth + 1);
        if (value.item == ITEM_ERROR) {
            return NULL;
        }
        array_append(arr, value, input->pool);

        skip_tab_pace_and_comments(toml, line_num);

        if (**toml == ']') {
            (*toml)++;
            break;
        }
        if (**toml != ',') {
            return NULL;
        }
        (*toml)++; // skip comma
        skip_tab_pace_and_comments(toml, line_num);

        // Handle trailing comma
        if (**toml == ']') {
            (*toml)++;
            break;
        }
    }

    return arr;
}

static Map* parse_inline_table(InputContext& ctx, const char **toml, int *line_num, int depth) {
    SourceTracker& tracker = ctx.tracker;

    Input* input = ctx.input();

    if (**toml != '{') return NULL;
    if (depth >= TOML_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum TOML nesting depth (%d) exceeded", TOML_MAX_DEPTH);
        return NULL;
    }
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;

    (*toml)++; // skip '{'
    tracker.advance(1);
    skip_tab_pace(toml);

    if (**toml == '}') { // empty table
        (*toml)++;
        tracker.advance(1);
        return mp;
    }

    while (**toml) {
        String* key = parse_key(ctx, toml);
        if (!key) {
            return NULL;
        }

        skip_tab_pace(toml);
        if (**toml != '=') {
            return NULL;
        }
        (*toml)++;
        tracker.advance(1);
        skip_tab_pace(toml);

        Item value = parse_value(ctx, toml, line_num, depth + 1);
        if (value.item == ITEM_ERROR) {
            return NULL;
        }

        ctx.builder.putToMap(lam::gc_borrow(mp), key, value);

        skip_tab_pace(toml);
        if (**toml == '}') {
            (*toml)++;
            tracker.advance(1);
            break;
        }
        if (**toml != ',') {
            return NULL;
        }
        (*toml)++;
        tracker.advance(1);
        skip_tab_pace(toml);
    }
    return mp;
}

static Item parse_value(InputContext& ctx, const char **toml, int *line_num, int depth) {
    SourceTracker& tracker = ctx.tracker;

    if (depth >= TOML_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum TOML nesting depth (%d) exceeded", TOML_MAX_DEPTH);
        return {.item = ITEM_ERROR};
    }

    skip_tab_pace_and_comments(toml, line_num);

    SourceLocation value_loc = tracker.location();
    switch (**toml) {
        case '{': {
            Map* table = parse_inline_table(ctx, toml, line_num, depth + 1);
            if (!table) {
                ctx.addError(value_loc, "Invalid inline table");
            }
            return table ? (Item){.item = (uint64_t)table} : (Item){.item = ITEM_ERROR};
        }
        case '[': {
            Array* array = parse_array(ctx, toml, line_num, depth + 1);
            if (!array) {
                ctx.addError(value_loc, "Invalid array");
            }
            return array ? (Item){.item = (uint64_t)array} : (Item){.item = ITEM_ERROR};
        }
        case '"': {
            String* str = NULL;
            if (strncmp(*toml, "\"\"\"", 3) == 0) {
                str = parse_multiline_basic_string(ctx, toml, line_num);
            } else {
                str = parse_basic_string(ctx, toml);
            }
            if (!str) {
                ctx.addError(value_loc, "Invalid string value");
                return (Item){.item = ITEM_ERROR};
            }
            return (Item){.item = s2it(str)};
        }
        case '\'': {
            String* str = NULL;
            if (strncmp(*toml, "'''", 3) == 0) {
                str = parse_multiline_literal_string(ctx, toml, line_num);
            } else {
                str = parse_literal_string(ctx, toml);
            }
            if (!str) {
                ctx.addError(value_loc, "Invalid literal string");
                return (Item){.item = ITEM_ERROR};
            }
            return (Item){.item = s2it(str)};
        }
        case 't':
            if (strncmp(*toml, "true", 4) == 0 && !isalnum((unsigned char)*(*toml + 4))) {
                *toml += 4;
                tracker.advance(4);
                return {.item = b2it(true)};
            }
            ctx.addError(value_loc, "Invalid boolean: expected 'true'");
            return {.item = ITEM_ERROR};
        case 'f':
            if (strncmp(*toml, "false", 5) == 0 && !isalnum((unsigned char)*(*toml + 5))) {
                *toml += 5;
                tracker.advance(5);
                return {.item = b2it(false)};
            }
            ctx.addError(value_loc, "Invalid boolean: expected 'false'");
            return {.item = ITEM_ERROR};
        case 'i':
            if (strncmp(*toml, "inf", 3) == 0) {
                return parse_number(ctx, toml);
            }
            ctx.addError(value_loc, "Invalid value starting with 'i'");
            return {.item = ITEM_ERROR};
        case 'n':
            if (strncmp(*toml, "nan", 3) == 0) {
                return parse_number(ctx, toml);
            }
            ctx.addError(value_loc, "Invalid value starting with 'n'");
            return {.item = ITEM_ERROR};
        case '-':
            if (*((*toml) + 1) == 'i' || *((*toml) + 1) == 'n' || isdigit((unsigned char)*((*toml) + 1))) {
                return parse_number(ctx, toml);
            }
            ctx.addError(value_loc, "Invalid negative number");
            return {.item = ITEM_ERROR};
        case '+':
            if (isdigit((unsigned char)*((*toml) + 1))) {
                return parse_number(ctx, toml);
            }
            ctx.addError(value_loc, "Invalid positive number");
            return {.item = ITEM_ERROR};
        default:
            if ((**toml >= '0' && **toml <= '9')) {
                return parse_number(ctx, toml);
            }
            ctx.addError(value_loc, "Unexpected character '%c' (0x%02X)", **toml, (unsigned char)**toml);
            return {.item = ITEM_ERROR};
    }
}// Helper function to create string key from C string
static String* create_string_key(InputContext& ctx, const char* key_str) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    int len = strlen(key_str);
    for (int i = 0; i < len; i++) {
        stringbuf_append_char(sb, key_str[i]);
    }

    String* key = builder.createName(sb->str->chars, sb->length);
    return key;
}

static String* create_string_key_view(InputContext& ctx, const StrView* key_view) {
    if (!key_view) return NULL;
    MarkBuilder& builder = ctx.builder;
    return builder.createName(key_view->str, key_view->length);
}

// Helper function to find or create a section in the root map
static Map* find_or_create_section(InputContext& ctx, Map* root_map, const char* section_name) {
    Input* input = ctx.input();
    String* key = create_string_key(ctx, section_name);
    if (!key) return NULL;

    // Look for existing section in root map
    ShapeEntry* entry = ((TypeMap*)root_map->type)->shape;
    while (entry) {
        if (entry->name->length == key->len &&
            strncmp(entry->name->str, key->chars, key->len) == 0) {
            // Found existing section
            void* field_ptr = (char*)root_map->data + entry->byte_offset;
            return *(Map**)field_ptr;
        }
        entry = entry->next;
    }

    // Create new section
    Map* section_map = map_pooled(input->pool);
    if (!section_map) return NULL;

    // Add section to root map
    ctx.builder.putToMap(lam::gc_borrow(root_map), key, {.item = (uint64_t)section_map});

    return section_map;
}

// Helper function to handle nested sections (like "database.credentials")
static Map* handle_nested_section(InputContext& ctx, Map* root_map, const char* section_path) {
    Input* input = ctx.input();
    if (!section_path) return NULL;

    StrView section_view = strview_from_cstr(section_path);
    StrViewSplitIter iter;
    StrView part;
    strview_split_init(&iter, section_view, '.');
    if (!strview_split_next(&iter, &part) || part.length == 0) return NULL;

    // Get or create the first level section
    char* first_part = strview_dup_with_pool(&part, input->pool);
    if (!first_part) return NULL;
    Map* current_map = find_or_create_section(ctx, root_map, first_part);
    if (!current_map) return NULL;

    // If there's no remaining path, return the current section
    if (iter.finished) return current_map;

    // Handle nested parts
    TypeMap* current_map_type = (TypeMap*)current_map->type;
    ShapeEntry* current_shape_entry = current_map_type->shape;
    if (current_shape_entry) {
        while (current_shape_entry->next) {
            current_shape_entry = current_shape_entry->next;
        }
    }

    while (strview_split_next(&iter, &part)) {
        if (part.length == 0) return NULL;
        String* key = create_string_key_view(ctx, &part);
        if (!key) return NULL;

        // Look for existing nested table in current map
        Map* nested_map = NULL;
        ShapeEntry* entry = current_map_type->shape;
        while (entry) {
            if (entry->name->length == key->len &&
                strncmp(entry->name->str, key->chars, key->len) == 0) {
                // Found existing entry
                void* field_ptr = (char*)current_map->data + entry->byte_offset;
                nested_map = *(Map**)field_ptr;
                break;
            }
            entry = entry->next;
        }

        if (!nested_map) {
            // Create new nested table
            nested_map = map_pooled(input->pool);
            if (!nested_map) return NULL;

            ctx.builder.putToMap(lam::gc_borrow(current_map), key, {.item = (uint64_t)nested_map});
        }

        current_map = nested_map;
        current_map_type = (TypeMap*)nested_map->type;
        // Find the last shape entry in the current table
        current_shape_entry = current_map_type->shape;
        if (current_shape_entry) {
            while (current_shape_entry->next) {
                current_shape_entry = current_shape_entry->next;
            }
        }
    }

    return current_map;
}

static bool parse_table_header(const char **toml, char *table_name, int *line_num) {
    if (**toml != '[') return false;
    (*toml)++; // skip '['

    skip_tab_pace(toml);

    int i = 0;
    while (**toml && **toml != ']' && i < 255) {
        if (**toml == ' ' || **toml == '\t') {
            skip_tab_pace(toml);
            continue;
        }
        table_name[i++] = **toml;
        (*toml)++;
    }
    table_name[i] = '\0';

    if (i == 0 || **toml != ']') {
        return false;
    }
    (*toml)++; // skip ']'

    return true;
}

void parse_toml(Input* input, const char* toml_string) {
    if (!toml_string || !*toml_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    InputContext ctx(input, toml_string, strlen(toml_string));

    Map* root_map = map_pooled(input->pool);
    if (!root_map) { return; }
    input->root = {.item = (uint64_t)root_map};

    const char *toml = toml_string;
    int line_num = 1;

    // Current table context
    Map* current_table = root_map;

    while (*toml) {
        skip_tab_pace_and_comments(&toml, &line_num);
        if (!*toml) break;

        // Check for table header
        if (*toml == '[') {
            // Check for array of tables [[...]] which we don't support yet
            if (*(toml + 1) == '[') {
                ctx.addWarning(ctx.tracker.location(), "Array of tables [[...]] not yet supported");
                skip_line(&toml, &line_num);
                continue;
            }

            char table_name[256];
            if (parse_table_header(&toml, table_name, &line_num)) {
                // Handle sections using the new refactored function
                Map* section_map = handle_nested_section(ctx, root_map, table_name);
                if (section_map) {
                    current_table = section_map;
                }
                skip_line(&toml, &line_num);
                continue;
            } else {
                ctx.addError(ctx.tracker.location(), "Invalid table header");
                skip_line(&toml, &line_num);
                continue;
            }
        }

        // Parse key-value pair
        SourceLocation key_loc = ctx.tracker.location();
        String* key = parse_key(ctx, &toml);
        if (!key) {
            ctx.addError(key_loc, "Invalid or empty key");
            skip_line(&toml, &line_num);
            continue;
        }

        skip_tab_pace(&toml);
        if (*toml != '=') {
            ctx.addError(ctx.tracker.location(), "Expected '=' after key '%.*s'", (int)key->len, key->chars);
            skip_line(&toml, &line_num);
            continue;
        }
        toml++; // skip '='

        Item value = parse_value(ctx, &toml, &line_num);
        if (value.item == ITEM_ERROR) {
            ctx.addError(ctx.tracker.location(), "Failed to parse value for key '%.*s'", (int)key->len, key->chars);
            skip_line(&toml, &line_num);
            continue;
        }

        ctx.builder.putToMap(lam::gc_borrow(current_table), key, value);

        skip_line(&toml, &line_num);
    }

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
