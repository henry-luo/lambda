#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../../lib/datetime.h"

using namespace lambda;

static const int MARK_MAX_DEPTH = 512;

static Element* parse_element(InputContext& ctx, const char **mark, int depth = 0);
static Item parse_value(InputContext& ctx, const char **mark, int depth = 0);
static Item parse_content(InputContext& ctx, const char **mark, int depth = 0);

static void skip_comments(const char **mark) {
    skip_whitespace(mark);
    while (**mark == '/' && *(*mark + 1) == '/') {
        // Skip single-line comment
        while (**mark && **mark != '\n' && **mark != '\r') {
            (*mark)++;
        }
        skip_whitespace(mark);
    }

    // Handle multi-line comments /* ... */
    while (**mark == '/' && *(*mark + 1) == '*') {
        *mark += 2; // Skip /*
        while (**mark && !(**mark == '*' && *(*mark + 1) == '/')) {
            (*mark)++;
        }
        if (**mark == '*' && *(*mark + 1) == '/') {
            *mark += 2; // Skip */
        }
        skip_whitespace(mark);
    }
}

static String* parse_string(InputContext& ctx, const char **mark) {
    if (**mark != '"') return NULL;
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    (*mark)++; // Skip opening quote
    while (**mark && **mark != '"') {
        if (**mark == '\\') {
            (*mark)++;
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
                    (*mark)++; // skip 'u'
                    char hex[5] = {0};
                    strncpy(hex, *mark, 4);
                    (*mark) += 4; // skip 4 hex digits
                    int codepoint = (int)strtol(hex, NULL, 16);
                    if (codepoint < 0x80) {
                        stringbuf_append_char(sb, (char)codepoint);
                    } else if (codepoint < 0x800) {
                        stringbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                        stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    } else {
                        stringbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                        stringbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    }
                } break;
                default: break; // invalid escape
            }
        } else {
            stringbuf_append_char(sb, **mark);
        }
        (*mark)++;
    }

    if (**mark == '"') {
        (*mark)++; // skip closing quote
    }
    return builder.createString(sb->str->chars, sb->length);
}

static String* parse_symbol(InputContext& ctx, const char **mark) {
    if (**mark != '\'') return NULL;
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    (*mark)++; // Skip opening quote
    while (**mark && **mark != '\'' && **mark != '\n') {
        if (**mark == '\\') {
            (*mark)++;
            switch (**mark) {
                case '\'': stringbuf_append_char(sb, '\''); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                default: stringbuf_append_char(sb, **mark); break;
            }
        } else {
            stringbuf_append_char(sb, **mark);
        }
        (*mark)++;
    }

    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    }

    return builder.createString(sb->str->chars, sb->length);
}

static String* parse_unquoted_identifier(InputContext& ctx, const char **mark) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    // First character must be alpha or underscore
    if (!(**mark >= 'a' && **mark <= 'z') &&
        !(**mark >= 'A' && **mark <= 'Z') &&
        **mark != '_') {
        return NULL;
    }

    while (**mark && ((**mark >= 'a' && **mark <= 'z') ||
                      (**mark >= 'A' && **mark <= 'Z') ||
                      (**mark >= '0' && **mark <= '9') ||
                      **mark == '_' || **mark == '-')) {
        stringbuf_append_char(sb, **mark);
        (*mark)++;
    }

    return builder.createString(sb->str->chars, sb->length);
}

static Item parse_binary(InputContext& ctx, const char **mark) {
    if (**mark != 'b' || *(*mark + 1) != '\'') return {.item = ITEM_ERROR};

    *mark += 2; // Skip b'
    skip_whitespace(mark);

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer before use

    // Check for hex format
    if (**mark == '\\' && *(*mark + 1) == 'x') {
        *mark += 2; // Skip \x
        while (**mark && **mark != '\'') {
            if ((**mark >= '0' && **mark <= '9') ||
                (**mark >= 'a' && **mark <= 'f') ||
                (**mark >= 'A' && **mark <= 'F')) {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid hex character
            }
            (*mark)++;
        }
    }
    // Check for base64 format
    else if (**mark == '\\' && (*(*mark + 1) == '6' && *(*mark + 2) == '4')) {
        *mark += 3; // Skip \64
        while (**mark && **mark != '\'') {
            if ((**mark >= 'A' && **mark <= 'Z') ||
                (**mark >= 'a' && **mark <= 'z') ||
                (**mark >= '0' && **mark <= '9') ||
                **mark == '+' || **mark == '/' || **mark == '=') {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid base64 character
            }
            (*mark)++;
        }
    }
    // Default hex format without \x prefix
    else {
        while (**mark && **mark != '\'') {
            if ((**mark >= '0' && **mark <= '9') ||
                (**mark >= 'a' && **mark <= 'f') ||
                (**mark >= 'A' && **mark <= 'F')) {
                stringbuf_append_char(sb, **mark);
            } else if (**mark != ' ' && **mark != '\t' && **mark != '\n') {
                break; // Invalid hex character
            }
            (*mark)++;
        }
    }

    if (**mark == '\'') {
        (*mark)++; // skip closing quote
    }

    String* binary_str = stringbuf_to_string(sb);
    return binary_str ? (Item){.item = s2it(binary_str)} : (Item){.item = ITEM_ERROR};
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
    }

    // parse the content as a datetime value
    String* content_str = stringbuf_to_string(sb);
    if (!content_str) return {.item = ITEM_ERROR};

    DateTime* dt = datetime_parse_lambda(ctx.input()->pool, content_str->chars);
    if (dt) {
        return {.item = k2it(dt)};
    }

    // fallback: return as string if datetime parsing fails
    return {.item = s2it(content_str)};
}

static Item parse_number(Input *input, const char **mark) {
    double *dval;
    dval = (double*)pool_calloc(input->pool, sizeof(double));
    if (dval == NULL) return {.item = ITEM_ERROR};

    char* end;
    *dval = strtod(*mark, &end);
    *mark = end;

    // Check for decimal suffix (n or N)
    if (**mark == 'n' || **mark == 'N') {
        (*mark)++;
        // For now, treat as regular double - could enhance for true decimal support
    }

    return {.item = d2it(dval)};
}

static Array* parse_array(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '[') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
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

    while (**mark) {
        Item item = parse_value(ctx, mark, depth + 1);
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ']') {
            (*mark)++;
            break;
        }
        if (**mark != ',') {
            return NULL; // invalid format
        }
        (*mark)++;
        skip_comments(mark);
    }
    return arr;
}

static Array* parse_list(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '(') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
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

    while (**mark) {
        Item item = parse_value(ctx, mark, depth + 1);
        array_append(arr, item, input->pool);

        skip_comments(mark);
        if (**mark == ')') {
            (*mark)++;
            break;
        }
        if (**mark != ',') {
            return NULL; // invalid format
        }
        (*mark)++;
        skip_comments(mark);
    }
    return arr;
}

static Map* parse_map(InputContext& ctx, const char **mark, int depth = 0) {
    Input* input = ctx.input();
    if (**mark != '{') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
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

        if (!key) return mp;

        skip_comments(mark);
        if (**mark != ':') return mp;
        (*mark)++;
        skip_comments(mark);

        Item value = parse_value(ctx, mark, depth + 1);
        ctx.builder.putToMap(mp, key, value);

        skip_comments(mark);
        if (**mark == '}') {
            (*mark)++;
            break;
        }
        if (**mark != ',') return mp;
        (*mark)++;
        skip_comments(mark);
    }
    return mp;
}

static Element* parse_element(InputContext& ctx, const char **mark, int depth) {
    Input* input = ctx.input();
    if (**mark != '<') return NULL;
    if (depth >= MARK_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
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
            builder.putToElement(element, key, attr_value);
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
        Item content_item = parse_content(ctx, mark, depth + 1);
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
    Input* input = ctx.input();
    skip_comments(mark);

    if (depth >= MARK_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum nesting depth (%d) exceeded", MARK_MAX_DEPTH);
        return {.item = ITEM_ERROR};
    }

    switch (**mark) {
        case '{':
            return {.item = (uint64_t)parse_map(ctx, mark, depth + 1)};
        case '[':
            return {.item = (uint64_t)parse_array(ctx, mark, depth + 1)};
        case '(':
            return {.item = (uint64_t)parse_list(ctx, mark, depth + 1)};
        case '<':
            return {.item = (uint64_t)parse_element(ctx, mark, depth + 1)};
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
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = NAN;
                return {.item = d2it(dval)};
            }
            goto UNQUOTED_IDENTIFIER;
        case 'i':
            if (strncmp(*mark, "inf", 3) == 0) {
                *mark += 3;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = INFINITY;
                return {.item = d2it(dval)};
            }
            goto UNQUOTED_IDENTIFIER;
        case '-':
            if (strncmp(*mark, "-inf", 4) == 0) {
                *mark += 4;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = -INFINITY;
                return {.item = d2it(dval)};
            } else if (strncmp(*mark, "-nan", 4) == 0) {
                *mark += 4;
                double *dval;
                dval = (double*)pool_calloc(input->pool, sizeof(double));
                if (dval == NULL) return {.item = ITEM_ERROR};
                *dval = -NAN;
                return {.item = d2it(dval)};
            }
            // Fall through to number parsing
        default:
            if ((**mark >= '0' && **mark <= '9') || **mark == '-' || **mark == '+') {
                return parse_number(input, mark);
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

    // Parse the root content - could be a single value or element
    input->root = parse_content(ctx, &mark, 0);

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }
}
