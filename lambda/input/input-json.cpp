#include "input.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "../mark_builder.hpp"
#include <cstring>
#include <cstdlib>

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
        unsigned char c = (unsigned char)**json;
        // JSON does not allow unescaped control characters U+0000-U+001F
        if (c < 0x20) {
            ctx.addError(tracker.location(), "Unexpected control character in string");
            return nullptr;
        }
        if (**json == '\\') {
            (*json)++;
            tracker.advance(1);

            if (!**json) {
                ctx.addError(tracker.location(), "Unexpected end of string after escape");
                return nullptr;
            }

            int consumed = parse_escape_char(json, sb);
            tracker.advance(consumed);
            continue;
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

    char* end;
    const char* start = *json;
    strtod(*json, &end);

    if (end == *json) {
        ctx.addError(tracker.location(), "Invalid number format");
        return ctx.builder.createNull();
    }

    size_t len = end - *json;
    *json = end;
    tracker.advance(len);

    Item number_item = parse_scanned_decimal_number(ctx, start, len, false, true);
    if (number_item.item == ITEM_NULL) {
        const char* msg = scanned_number_has_float_marker(start, len)
            ? "Invalid number format"
            : "Invalid integer decimal value";
        ctx.addError(tracker.location(), msg);
        return ctx.builder.createNull();
    }
    return number_item;
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
        // parse object keys with the same JSON string routine as values so
        // escape and control-character validation cannot drift.
        if (**json != '"') {
            ctx.addError(tracker.location(), "Expected '\"' for object key");
            break;
        }

        // JSON keys are strings; preserve empty/non-identifier keys instead of symbol spelling.
        String* key_str = parse_string(ctx, json);
        String* key = key_str ? ctx.builder.createName(key_str->chars, key_str->len) : nullptr;
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
        if (key->len == 9 && strncmp(key->chars, "__proto__", 9) == 0) {
            map_builder.put("__json_own_proto__", true);
        }

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
            if (!str) {
                return ctx.builder.createNull();  // actual parse error
            }
            return (Item){.item = s2it(str)};
        }
        case 't':
            if (!input_expect_literal(ctx, json, "true")) return ctx.builder.createNull();
            return ctx.builder.createBool(true);
        case 'f':
            if (!input_expect_literal(ctx, json, "false")) return ctx.builder.createNull();
            return ctx.builder.createBool(false);
        case 'n':
            if (!input_expect_literal(ctx, json, "null")) return ctx.builder.createNull();
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
    if (!json_string || !*json_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, json_string, strlen(json_string));

    input->root = parse_value(ctx, &json_string, 0);

    // Log any errors that occurred during parsing
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}

// Parse a JSON string and return the result as an Item
// Reuses the same JSON parser but doesn't set input->root
Item parse_json_to_item(Input* input, const char* json_string) {
    if (!json_string || !*json_string) {
        return {.item = ITEM_NULL};
    }

    InputContext ctx(input, json_string, strlen(json_string));

    Item result = parse_value(ctx, &json_string, 0);

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }

    return result;
}

// Strict variant: parse JSON and verify no trailing non-whitespace content
// Returns ITEM_NULL and sets *ok = false if there is trailing content or parse error
Item parse_json_to_item_strict(Input* input, const char* json_string, bool* ok) {
    *ok = false;
    if (!json_string || !*json_string) {
        return {.item = ITEM_NULL};
    }

    InputContext ctx(input, json_string, strlen(json_string));

    Item result = parse_value(ctx, &json_string, 0);

    if (ctx.hasErrors()) {
        return {.item = ITEM_NULL};
    }

    // check for trailing non-whitespace content
    skip_whitespace(&json_string);
    if (*json_string != '\0') {
        return {.item = ITEM_NULL};
    }

    *ok = true;
    return result;
}
