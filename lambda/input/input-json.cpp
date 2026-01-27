#include "input.hpp"
#include "input-context.hpp"
#include "../mark_builder.hpp"
#include <cstring>

#define MAX_PARSING_DEPTH 64                  // Max nesting depth
using namespace lambda;

static Item parse_value(InputContext& ctx, const char **json, int depth = 0);

static String* parse_string(InputContext& ctx, const char **json) {
    SourceTracker& tracker = ctx.tracker;

    if (**json != '"') {
        ctx.addError(tracker.location(), "Expected '\"' to start string");
        return nullptr;
    }

    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    (*json)++; // Skip opening quote
    tracker.advance(1);

    while (**json && **json != '"') {
        if (**json == '\\') {
            (*json)++;
            tracker.advance(1);

            if (!**json) {
                ctx.addError(tracker.location(), "Unexpected end of string after escape");
                return nullptr;
            }

            switch (**json) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case '/': stringbuf_append_char(sb, '/'); break;
                case 'b': stringbuf_append_char(sb, '\b'); break;
                case 'f': stringbuf_append_char(sb, '\f'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                // handle \uXXXX escapes
                case 'u': {
                    (*json)++; // skip 'u'
                    tracker.advance(1);

                    if (strlen(*json) < 4) {
                        ctx.addError(tracker.location(), "Invalid unicode escape: need 4 hex digits");
                        return nullptr;
                    }

                    char hex[5] = {0};
                    strncpy(hex, *json, 4);
                    (*json) += 4; // skip 4 hex digits
                    tracker.advance(4);

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
                default:
                    ctx.addWarning(tracker.location(),
                                   "Invalid escape sequence: \\%c", **json);
                    break;
            }
        } else {
            stringbuf_append_char(sb, **json);
        }
        (*json)++;
        tracker.advance(1);
    }

    if (**json == '"') {
        (*json)++; // skip closing quote
        tracker.advance(1);
    } else {
        ctx.addError(tracker.location(), "Unterminated string");
        return nullptr;
    }

    return builder.createString(sb->str->chars, sb->length);
}

static Item parse_number(InputContext& ctx, const char **json) {
    SourceTracker& tracker = ctx.tracker;

    const char* start = *json;
    char* end;
    double value = strtod(*json, &end);

    if (end == *json) {
        ctx.addError(tracker.location(), "Invalid number format");
        return ctx.builder.createNull();
    }

    size_t len = end - *json;
    *json = end;
    tracker.advance(len);

    // Check if it's an integer
    if (value == (int64_t)value) {
        return ctx.builder.createInt((int64_t)value);
    } else {
        return ctx.builder.createFloat(value);
    }
}

static Item parse_array(InputContext& ctx, const char **json, int depth) {
    SourceTracker& tracker = ctx.tracker;

    if (**json != '[') {
        ctx.addError(tracker.location(), "Expected '[' to start array");
        return ctx.builder.createNull();
    }

    ArrayBuilder arr_builder = ctx.builder.array();

    (*json)++; // skip [
    tracker.advance(1);
    skip_whitespace(json);

    if (**json == ']') {
        (*json)++;
        tracker.advance(1);
        return arr_builder.final();
    }

    while (**json && !ctx.shouldStopParsing()) {
        Item item = parse_value(ctx, json, depth + 1);
        arr_builder.append(item);

        skip_whitespace(json);
        if (**json == ']') {
            (*json)++;
            tracker.advance(1);
            break;
        }

        if (**json != ',') {
            ctx.addError(tracker.location(), "Expected ',' or ']' in array");
            // Error recovery: skip to next comma or closing bracket
            while (**json && **json != ',' && **json != ']') {
                (*json)++;
                tracker.advance(1);
            }
            if (**json == ',') {
                (*json)++;
                tracker.advance(1);
            }
            continue;
        }

        (*json)++;
        tracker.advance(1);
        skip_whitespace(json);
    }

    return arr_builder.final();
}

static Item parse_object(InputContext& ctx, const char **json, int depth) {
    SourceTracker& tracker = ctx.tracker;

    if (**json != '{') {
        ctx.addError(tracker.location(), "Expected '{' to start object");
        return ctx.builder.createNull();
    }

    MapBuilder map_builder = ctx.builder.map();

    (*json)++; // skip '{'
    tracker.advance(1);
    skip_whitespace(json);

    if (**json == '}') { // empty map
        (*json)++;
        tracker.advance(1);
        return map_builder.final();
    }

    while (**json && !ctx.shouldStopParsing()) {
        // Parse key as a raw string to get the name
        if (**json != '"') {
            ctx.addError(tracker.location(), "Expected '\"' for object key");
            break;
        }

        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        (*json)++; // Skip opening quote
        tracker.advance(1);

        // Parse key content (simplified - no escape handling needed for keys typically)
        while (**json && **json != '"') {
            if (**json == '\\') {
                (*json)++;
                tracker.advance(1);
                if (**json) {
                    stringbuf_append_char(sb, **json);
                }
            } else {
                stringbuf_append_char(sb, **json);
            }
            (*json)++;
            tracker.advance(1);
        }

        if (**json == '"') {
            (*json)++; // skip closing quote
            tracker.advance(1);
        }

        // Create key as a name (always pooled)
        // Special case: empty key "" in JSON becomes "''" in Lambda
        String* key;
        if (sb->length == 0) {
            // Empty JSON key maps to the literal string "''" (two single quotes)
            key = ctx.builder.createName("''", 2);
        } else {
            key = ctx.builder.createName(sb->str->chars, sb->length);
        }
        if (!key) {
            // Error recovery: skip to next comma or closing brace
            while (**json && **json != ',' && **json != '}') {
                (*json)++;
                tracker.advance(1);
            }
            if (**json == ',') {
                (*json)++;
                tracker.advance(1);
                skip_whitespace(json);
                continue;
            }
            break;
        }

        skip_whitespace(json);
        if (**json != ':') {
            ctx.addError(tracker.location(), "Expected ':' after object key");
            // Error recovery: skip to next comma or closing brace
            while (**json && **json != ',' && **json != '}') {
                (*json)++;
                tracker.advance(1);
            }
            if (**json == ',') {
                (*json)++;
                tracker.advance(1);
                skip_whitespace(json);
                continue;
            }
            break;
        }

        (*json)++;
        tracker.advance(1);
        skip_whitespace(json);

        Item value = parse_value(ctx, json, depth + 1);
        map_builder.put(key, value);

        skip_whitespace(json);
        if (**json == '}') {
            (*json)++;
            tracker.advance(1);
            break;
        }

        if (**json != ',') {
            ctx.addError(tracker.location(), "Expected ',' or '}' in object");
            // Error recovery: skip to next comma or closing brace
            while (**json && **json != ',' && **json != '}') {
                (*json)++;
                tracker.advance(1);
            }
            if (**json == ',') {
                (*json)++;
                tracker.advance(1);
            }
            continue;
        }

        (*json)++;
        tracker.advance(1);
        skip_whitespace(json);
    }

    return map_builder.final();
}

static Item parse_value(InputContext& ctx, const char **json, int depth) {
    SourceTracker& tracker = ctx.tracker;

    // Security: Prevent stack overflow from deeply nested structures
    if (depth > MAX_PARSING_DEPTH) {
        ctx.addError(tracker.location(), "JSON nesting too deep (max %d levels)", MAX_PARSING_DEPTH);
        return ctx.builder.createNull();
    }

    skip_whitespace(json);

    if (!**json) {
        ctx.addError(tracker.location(), "Unexpected end of JSON");
        return ctx.builder.createNull();
    }

    switch (**json) {
        case '{':
            return parse_object(ctx, json, depth);
        case '[':
            return parse_array(ctx, json, depth);
        case '"': {
            String* str = parse_string(ctx, json);
            if (!str) return ctx.builder.createNull();  // empty string maps to null
            return (Item){.item = s2it(str)};
        }
        case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                tracker.advance(4);
                return ctx.builder.createBool(true);
            }
            ctx.addError(tracker.location(), "Invalid value, expected 'true'");
            return ctx.builder.createNull();
        case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                tracker.advance(5);
                return ctx.builder.createBool(false);
            }
            ctx.addError(tracker.location(), "Invalid value, expected 'false'");
            return ctx.builder.createNull();
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                tracker.advance(4);
                return ctx.builder.createNull();
            }
            ctx.addError(tracker.location(), "Invalid value, expected 'null'");
            return ctx.builder.createNull();
        default:
            if ((**json >= '0' && **json <= '9') || **json == '-') {
                return parse_number(ctx, json);
            }
            ctx.addError(tracker.location(),
                        "Unexpected character: '%c'", **json);
            return ctx.builder.createNull();
    }
}

void parse_json(Input* input, const char* json_string) {
    InputContext ctx(input, json_string, strlen(json_string));

    input->root = parse_value(ctx, &json_string, 0);

    // Log any errors that occurred during parsing
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
