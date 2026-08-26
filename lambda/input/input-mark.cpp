#include "input.hpp"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include "../../lib/datetime.h"
#include "../../lib/mem.h"
#include "../../lib/str.h"
#include "../core/lambda-decimal.hpp"

#include <string.h>

using namespace lambda;

static const int MARK_MAX_DEPTH = 512;

static Element* parse_element(InputContext& ctx, const char **mark, int depth = 0);
static Item parse_value(InputContext& ctx, const char **mark, int depth = 0);
static Item parse_content(InputContext& ctx, const char **mark, int depth = 0);

// keep legacy pointer cursors on the shared source/diagnostic coordinate system
static void mark_sync(InputContext& ctx, const char** mark) {
    if (mark) ctx.syncTo(*mark);
}

static void mark_error(InputContext& ctx, const char** mark, const char* message) {
    mark_sync(ctx, mark);
    ctx.addError(message);
}

static void mark_require_closed(InputContext& ctx, const char** mark,
                                bool closed, const char* message) {
    if (!closed) mark_error(ctx, mark, message);
}

static Item mark_item_from_pointer(const void* pointer) {
    return pointer ? (Item){.item = (uint64_t)pointer} : ItemError;
}

static void skip_comments(const char **mark) {
    skip_whitespace_and_comment_markers(mark, "//", nullptr, true);
}

static bool mark_n_literal_is_integer(const char* str, size_t len) {
    bool has_dot = false;
    bool has_negative_exponent = false;
    for (size_t i = 0; i < len; i++) {
        char ch = str[i];
        if (ch == '.') {
            has_dot = true;
        } else if (ch == 'e' || ch == 'E') {
            size_t exp = i + 1;
            if (exp < len && (str[exp] == '+' || str[exp] == '-')) {
                has_negative_exponent = (str[exp] == '-');
            }
            break;
        }
    }
    return !has_dot && !has_negative_exponent;
}

static Item parse_mark_suffixed_number(InputContext& ctx, const char* start, size_t len,
                                       char suffix) {
    if (suffix == 'N') {
        ctx.syncTo(start);
        ctx.addError(
            "decimal literal suffix 'N' has been retired; use 'm' for decimal or 'n' for integer");
        return {.item = ITEM_ERROR};
    }
    if (suffix == 'n' && !mark_n_literal_is_integer(start, len)) {
        ctx.syncTo(start);
        ctx.addError(
            "'n' literal must be integer-valued; use the 'm' suffix for decimal");
        return {.item = ITEM_ERROR};
    }

    char stack_buf[128];
    char* number = stack_buf;
    bool heap_buf = false;
    if (len >= sizeof(stack_buf)) {
        number = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
        if (!number) return {.item = ITEM_ERROR};
        heap_buf = true;
    }
    memcpy(number, start, len);
    number[len] = '\0';

    // Mark suffixes share Lambda literal tier semantics; the old strtod path
    // rounded exact integer/decimal intent before the formatter could see it.
    Item result = decimal_from_literal_string_arena(number, ctx.builder.arena(), suffix == 'n');
    if (heap_buf) mem_free(number);
    return result.item != ITEM_NULL ? result : (Item){.item = ITEM_ERROR};
}

enum MarkQuotedEscapePolicy {
    MARK_QUOTED_ESCAPE_STRING,
    MARK_QUOTED_ESCAPE_SYMBOL
};

static void append_mark_string_escape(StringBuf* sb, const char** mark) {
    switch (**mark) {
        case '"': stringbuf_append_char(sb, '"'); break;
        case '\\': stringbuf_append_char(sb, '\\'); break;
        case '/': stringbuf_append_char(sb, '/'); break;
        case 'b': stringbuf_append_char(sb, '\b'); break;
        case 'f': stringbuf_append_char(sb, '\f'); break;
        case 'n': stringbuf_append_char(sb, '\n'); break;
        case 'r': stringbuf_append_char(sb, '\r'); break;
        case 't': stringbuf_append_char(sb, '\t'); break;
        case 'u': {
            const char* h = *mark + 1;
            uint32_t codepoint = parse_hex_codepoint(&h, 4);
            if (codepoint != 0xFFFFFFFF) {
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && h[0] == '\\' && h[1] == 'u') {
                    const char* low_pos = h + 2;
                    uint32_t low = parse_hex_codepoint(&low_pos, 4);
                    uint32_t combined = decode_surrogate_pair((uint16_t)codepoint, (uint16_t)low);
                    if (low != 0xFFFFFFFF && combined != 0) {
                        codepoint = combined;
                        h = low_pos;
                    } else {
                        codepoint = 0xFFFD;
                    }
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    codepoint = 0xFFFD;
                }
                append_codepoint_utf8(sb, codepoint);
                *mark = h - 1; // trailing ++ moves past the escape.
            }
        } break;
        default: break; // invalid escape
    }
}

static void append_mark_symbol_escape(StringBuf* sb, const char** mark) {
    switch (**mark) {
        case '\'': stringbuf_append_char(sb, '\''); break;
        case '\\': stringbuf_append_char(sb, '\\'); break;
        case 'n': stringbuf_append_char(sb, '\n'); break;
        case 'r': stringbuf_append_char(sb, '\r'); break;
        case 't': stringbuf_append_char(sb, '\t'); break;
        default: stringbuf_append_char(sb, **mark); break;
    }
}

static String* parse_mark_quoted_string(InputContext& ctx, const char **mark, char quote,
                                        bool stop_at_newline,
                                        MarkQuotedEscapePolicy escape_policy) {
    if (**mark != quote) return NULL;
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    (*mark)++; // skip opening quote
    while (**mark && **mark != quote && (!stop_at_newline || **mark != '\n')) {
        if (**mark == '\\') {
            (*mark)++;
            if (!**mark) {
                mark_error(ctx, mark, "unterminated escape in quoted literal");
                return NULL;
            }
            if (escape_policy == MARK_QUOTED_ESCAPE_STRING) {
                append_mark_string_escape(sb, mark);
            } else {
                append_mark_symbol_escape(sb, mark);
            }
        } else {
            stringbuf_append_char(sb, **mark);
        }
        (*mark)++;
    }

    if (**mark == quote) {
        (*mark)++; // skip closing quote
    } else {
        mark_error(ctx, mark, quote == '\''
            ? "unterminated symbol literal"
            : "unterminated string literal");
        return NULL;
    }
    return builder.createString(sb->str->chars, sb->length);
}

static String* parse_string(InputContext& ctx, const char **mark) {
    return parse_mark_quoted_string(ctx, mark, '"', false, MARK_QUOTED_ESCAPE_STRING);
}

static String* parse_symbol(InputContext& ctx, const char **mark) {
    return parse_mark_quoted_string(ctx, mark, '\'', true, MARK_QUOTED_ESCAPE_SYMBOL);
}

static String* parse_unquoted_identifier(InputContext& ctx, const char **mark) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    // First character must be ASCII alpha, underscore, or a UTF-8 byte.
    if (!(**mark >= 'a' && **mark <= 'z') &&
        !(**mark >= 'A' && **mark <= 'Z') &&
        **mark != '_' && (unsigned char)**mark < 0x80) {
        return NULL;
    }

    while (**mark && ((**mark >= 'a' && **mark <= 'z') ||
                      (**mark >= 'A' && **mark <= 'Z') ||
                      (**mark >= '0' && **mark <= '9') ||
                      **mark == '_' || **mark == '-' ||
                      (unsigned char)**mark >= 0x80)) {
        stringbuf_append_char(sb, **mark);
        (*mark)++;
    }

    return builder.createString(sb->str->chars, sb->length);
}

static Item parse_binary(InputContext& ctx, const char **mark) {
    if (**mark != 'b' || *(*mark + 1) != '\'') return {.item = ITEM_ERROR};

    const char* content = *mark + 2;
    const char* close = content;
    while (*close && *close != '\'') close++;
    if (*close != '\'') {
        mark_error(ctx, mark, "unterminated binary literal");
        *mark = close;
        return {.item = ITEM_ERROR};
    }

    StrBuf* decoded = strbuf_new_cap((size_t)(close - content) + 1);
    int err_off = 0;
    int decoded_len = decoded ? str_binary_payload_decode(
        content, (int)(close - content), decoded, &err_off) : -1;
    *mark = close + 1;
    if (decoded_len < 0) {
        // Mark input must share the compiler's byte invariant; accepting encoded
        // text here previously produced a string-tagged lookalike value.
        mark_sync(ctx, mark);
        ctx.addError("invalid binary literal payload at byte %d", err_off);
        if (decoded) strbuf_free(decoded);
        return {.item = ITEM_ERROR};
    }
    if (decoded_len == 0) {
        strbuf_free(decoded);
        return ItemNull;
    }

    Binary* binary = ctx.builder.createBinary(decoded->str, (size_t)decoded_len);
    strbuf_free(decoded);
    return binary ? (Item){.item = x2it(binary)} : (Item){.item = ITEM_ERROR};
}

static Item parse_datetime(InputContext& ctx, const char **mark) {
    if (**mark != 't' || *(*mark + 1) != '\'') return {.item = ITEM_ERROR};

    *mark += 2; // Skip t'
    skip_whitespace(mark);

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    while (**mark && **mark != '\'') {
        stringbuf_append_char(sb, **mark);
        (*mark)++;
    }

    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    } else {
        mark_error(ctx, mark, "unterminated datetime literal");
        return {.item = ITEM_ERROR};
    }

    // parse the content as a datetime value
    String* content_str = stringbuf_to_string(sb);
    if (!content_str) return {.item = ITEM_ERROR};

    DateTime* dt = datetime_parse_lambda(ctx.input()->pool, content_str->chars);
    if (dt) {
        return ctx.builder.createDateTime(*dt);
    }

    // fallback: return as string if datetime parsing fails
    return {.item = s2it(content_str)};
}

static Item parse_number(InputContext& ctx, const char **mark) {
    const char* start = *mark;
    char* end;
    double dval = strtod(start, &end);
    if (end == start) return {.item = ITEM_ERROR};

    // Check for numeric suffix (n = integer, m = decimal; N retired)
    if (*end == 'n' || *end == 'N' || *end == 'm') {
        char suffix = *end;
        *mark = end + 1;
        return parse_mark_suffixed_number(ctx, start, (size_t)(end - start), suffix);
    } else {
        *mark = end;
    }

    // Plain Mark numbers used to round-trip through strtod, losing exact integer spellings.
    Item number = parse_scanned_decimal_number(ctx, start, (size_t)(end - start), false, true);
    return number.item != ITEM_NULL ? number : ctx.builder.createFloat(dval);
}

static Array* parse_array(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '[') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        mark_sync(ctx, mark);
        ctx.addError("Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return NULL;
    }
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*mark)++; // skip [
    skip_comments(mark);

    if (**mark == ']') {
        (*mark)++;
        return arr;
    }

    bool closed = false;
    while (**mark) {
        Item item = parse_value(ctx, mark, depth + 1);
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ']') {
            (*mark)++;
            closed = true;
            break;
        }
        if (**mark != ',') {
            mark_error(ctx, mark, "expected ',' or ']' after array item");
            return NULL;
        }
        (*mark)++;
        skip_comments(mark);
    }
    mark_require_closed(ctx, mark, closed, "expected ']' after array items");
    return arr;
}

static Array* parse_list(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '(') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        mark_sync(ctx, mark);
        ctx.addError("Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return NULL;
    }
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*mark)++; // skip (
    skip_comments(mark);

    if (**mark == ')') {
        (*mark)++;
        return arr;
    }

    bool closed = false;
    while (**mark) {
        const char* item_start = *mark;
        Item item = parse_value(ctx, mark, depth + 1);
        if (item.item == ITEM_ERROR && *mark == item_start) {
            if (!ctx.hasErrors()) mark_error(ctx, mark, "expected a list item");
            return NULL;
        }
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ')') {
            (*mark)++;
            closed = true;
            break;
        }
        if (**mark == ',') {
            (*mark)++;
            skip_comments(mark);
        } else if (**mark == ':') {
            // Mark lists also carry the legacy `:name value` attribute
            // spelling, so whitespace (and this marker) separates items.
            (*mark)++;
            skip_comments(mark);
        }
    }
    mark_require_closed(ctx, mark, closed, "expected ')' after list items");
    return arr;
}

static Map* parse_map(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '{') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        mark_sync(ctx, mark);
        ctx.addError("Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return NULL;
    }
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;

    (*mark)++; // skip '{'
    skip_comments(mark);

    if (**mark == '}') { // empty map
        (*mark)++;
        return mp;
    }

    bool closed = false;
    while (**mark) {
        String* key = NULL;

        // Parse key - can be string, symbol, or identifier
        if (**mark == '"') {
            key = parse_string(ctx, mark);
        } else if (**mark == '\'') {
            key = parse_symbol(ctx, mark);
        } else {
            key = parse_unquoted_identifier(ctx, mark);
        }

        if (!key) {
            mark_error(ctx, mark, "expected map key");
            return NULL;
        }

        skip_comments(mark);
        if (**mark != ':') {
            mark_error(ctx, mark, "expected ':' after map key");
            return NULL;
        }
        (*mark)++;
        skip_comments(mark);

        Item value = parse_value(ctx, mark, depth + 1);
        ctx.builder.putToMap(lam::gc_borrow(mp), key, value);

        skip_comments(mark);
        if (**mark == '}') {
            (*mark)++;
            closed = true;
            break;
        }
        if (**mark != ',') {
            mark_error(ctx, mark, "expected ',' or '}' after map item");
            return NULL;
        }
        (*mark)++;
        skip_comments(mark);
    }
    mark_require_closed(ctx, mark, closed, "expected '}' after map items");
    return mp;
}

static Element* parse_element(InputContext& ctx, const char **mark, int depth) {
    Input* input = ctx.input();
    if (**mark != '<') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        mark_sync(ctx, mark);
        ctx.addError("Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return NULL;
    }

    (*mark)++; // skip '<'
    skip_comments(mark);

    // Parse element name - can be symbol or identifier
    String* element_name = NULL;
    if (**mark == '\'') {
        element_name = parse_symbol(ctx, mark);
    } else {
        element_name = parse_unquoted_identifier(ctx, mark);
    }

    if (!element_name) return NULL;

    MarkBuilder builder(input);
    ElementBuilder element_builder = builder.element(element_name->chars);
    Element* element = element_builder.final().element;
    if (!element) return NULL;

    skip_comments(mark);

    // Parse attributes
    while (**mark && **mark != '>') {
        String* attr_name = NULL;

        // Check if this might be content instead of an attribute
        // If we see a quote, angle bracket, or brace that doesn't look like an attribute
        if (**mark == '"' || **mark == '<' || **mark == '{' || **mark == '[') {
            // Look ahead to see if this looks like an attribute or content
            const char* lookahead = *mark;
            if (**mark == '"') {
                // Skip the string to see what comes after
                lookahead++;
                while (*lookahead && *lookahead != '"') {
                    if (*lookahead == '\\') lookahead++; // skip escaped chars
                    if (*lookahead) lookahead++;
                }
                if (*lookahead == '"') lookahead++;
                skip_whitespace(&lookahead);

                // If we don't see a colon after the string, treat as content
                if (*lookahead != ':') {
                    break; // Start parsing content
                }
            } else {
                // For other content markers, just break to content parsing
                break;
            }
        }

        // Parse attribute name
        if (**mark == '"') {
            attr_name = parse_string(ctx, mark);
        } else if (**mark == '\'') {
            attr_name = parse_symbol(ctx, mark);
        } else {
            attr_name = parse_unquoted_identifier(ctx, mark);
        }

        if (!attr_name) break;

        skip_comments(mark);
        if (**mark != ':') break;
        (*mark)++;
        skip_comments(mark);

        // Parse attribute value
        Item attr_value = parse_value(ctx, mark, depth + 1);
        MarkBuilder builder(input);
        String* key = builder.createString(attr_name->chars);
        if (key) {
            builder.putToElement(lam::gc_borrow(element), key, attr_value);
        }

        skip_comments(mark);
        if (**mark == ',') {
            (*mark)++;
            skip_comments(mark);
        }
    }

    // Check for content separator (semicolon, newline, or just whitespace)
    skip_comments(mark);

    // Parse content - content can be separated by semicolons, newlines, or just whitespace
    while (**mark && **mark != '>') {
        if (**mark == ';') {
            (*mark)++;
            skip_comments(mark);
            continue;
        }
        const char* content_start = *mark;
        Item content_item = parse_content(ctx, mark, depth + 1);
        if (content_item.item == ITEM_ERROR && *mark == content_start) {
            if (!ctx.hasErrors()) mark_error(ctx, mark, "expected element content");
            return NULL;
        }
        if (content_item .item != ITEM_ERROR && content_item .item != ITEM_NULL) {
            // Add content to element
            list_push((List*)element, content_item);
            ((TypeElmt*)element->type)->content_length++;
        }
        skip_comments(mark);

        // Skip optional separators
        if (**mark == ';' || **mark == '\n') {
            (*mark)++;
            skip_comments(mark);
        }
    }

    if (**mark == '>') {
        (*mark)++; // skip closing '>'
    } else {
        mark_error(ctx, mark, "expected '>' after element content");
        return NULL;
    }

    return element;
}

static Item parse_content(InputContext& ctx, const char **mark, int depth) {
    skip_comments(mark);

    if (**mark == '<') {
        return {.item = (uint64_t)parse_element(ctx, mark, depth)};
    } else {
        return parse_value(ctx, mark, depth);
    }
}

static Item parse_value(InputContext& ctx, const char **mark, int depth) {
    skip_comments(mark);

    if (depth >= MARK_MAX_DEPTH) {
        mark_sync(ctx, mark);
        ctx.addError("Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return {.item = ITEM_ERROR};
    }

    switch (**mark) {
        case '{':
            return mark_item_from_pointer(parse_map(ctx, mark, depth + 1));
        case '[':
            return mark_item_from_pointer(parse_array(ctx, mark, depth + 1));
        case '(':
            return mark_item_from_pointer(parse_list(ctx, mark, depth + 1));
        case '<':
            return mark_item_from_pointer(parse_element(ctx, mark, depth + 1));
        case '"':
            return {.item = s2it(parse_string(ctx, mark))};
        case '\'':
            {
                String* str = parse_symbol(ctx, mark);
                // Create proper Symbol from the parsed string (different memory layout)
                Symbol* sym = str ? ctx.builder.createSymbol(str->chars, str->len) : nullptr;
                return {.item = y2it(sym)};
            }
        case 'b':
            if (*(*mark + 1) == '\'') {
                return parse_binary(ctx, mark);
            }
            goto UNQUOTED_IDENTIFIER;
        case 't':
            if (*(*mark + 1) == '\'') {
                return parse_datetime(ctx, mark);
            } else if (strncmp(*mark, "true", 4) == 0) {
                *mark += 4;
                return {.item = b2it(true)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'f':
            if (strncmp(*mark, "false", 5) == 0) {
                *mark += 5;
                return {.item = b2it(false)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'n':
            if (strncmp(*mark, "null", 4) == 0) {
                *mark += 4;
                return {.item = ITEM_NULL};
            } else if (strncmp(*mark, "nan", 3) == 0) {
                *mark += 3;
                return ctx.builder.createFloat(NAN);
            }
            goto UNQUOTED_IDENTIFIER;
        case 'i':
            if (strncmp(*mark, "inf", 3) == 0) {
                *mark += 3;
                return ctx.builder.createFloat(INFINITY);
            }
            goto UNQUOTED_IDENTIFIER;
        case '-':
            if (strncmp(*mark, "-inf", 4) == 0) {
                *mark += 4;
                return ctx.builder.createFloat(-INFINITY);
            } else if (strncmp(*mark, "-nan", 4) == 0) {
                *mark += 4;
                return ctx.builder.createFloat(-NAN);
            }
            // Fall through to number parsing
        default:
            if ((**mark >= '0' && **mark <= '9') || **mark == '-' || **mark == '+') {
                return parse_number(ctx, mark);
            }
            else if ((**mark >= 'a' && **mark <= 'z') ||
                (**mark >= 'A' && **mark <= 'Z') || **mark == '_') {
                UNQUOTED_IDENTIFIER:
                // Parse as identifier/symbol
                String* id = parse_unquoted_identifier(ctx, mark);
                if (!id) return {.item = ITEM_ERROR};
                Symbol* sym = ctx.builder.createSymbol(id->chars, id->len);
                return sym ? (Item){.item = y2it(sym)} : (Item){.item = ITEM_ERROR};
            }
            else if ((unsigned char)**mark >= 0x80) {
                goto UNQUOTED_IDENTIFIER;
            }
            return {.item = ITEM_ERROR};
    }
}

void parse_mark(Input* input, const char* mark_string) {
    if (!mark_string || !*mark_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    // create error tracking context with integrated source tracking
    InputContext ctx(input, mark_string, strlen(mark_string));

    const char* mark = mark_string;
    skip_comments(&mark);

    if (!*mark) {
        input->root = ItemNull;
        return;
    }

    // Parse the root content - could be a single value or element
    input->root = parse_content(ctx, &mark, 0);

    skip_comments(&mark);
    if (*mark && !ctx.hasErrors()) {
        mark_error(ctx, &mark, "unexpected trailing input after Mark value");
        input->root = ItemError;
    }

    if (ctx.hasErrors()) {
        // Keep malformed Mark values out of the runtime even when a nested
        // helper managed to assemble a partial container before failing.
        input->root = ItemError;
        ctx.logErrors();
    }
}
