#include "input.h"
#include "../mark_builder.hpp"

static Item parse_value(Input *input, MarkBuilder* builder, const char **json);

static void skip_whitespace(const char **json) {
    while (**json && (**json == ' ' || **json == '\n' || **json == '\r' || **json == '\t')) {
        (*json)++;
    }
}

static String* parse_string(Input *input, MarkBuilder* builder, const char **json) {
    if (**json != '"') return NULL;
    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);

    (*json)++; // Skip opening quote
    while (**json && **json != '"') {
        if (**json == '\\') {
            (*json)++;
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
                    char hex[5] = {0};
                    strncpy(hex, *json, 4);
                    (*json) += 4; // skip 4 hex digits
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
            stringbuf_append_char(sb, **json);
        }
        (*json)++;
    }

    if (**json == '"') {
        (*json)++; // skip closing quote
    }
    return builder->createString(sb->str->chars, sb->length);
}

static Item parse_number(Input *input, MarkBuilder* builder, const char **json) {
    char* end;
    double value = strtod(*json, &end);
    *json = end;

    // Check if it's an integer
    if (value == (int64_t)value) {
        return builder->createInt((int64_t)value);
    } else {
        return builder->createFloat(value);
    }
}

static Item parse_array(Input *input, MarkBuilder* builder, const char **json) {
    if (**json != '[') return builder->createNull();

    ArrayBuilder arr_builder = builder->array();

    (*json)++; // skip [
    skip_whitespace(json);
    if (**json == ']') {
        (*json)++;
        return arr_builder.final();
    }

    while (**json) {
        Item item = parse_value(input, builder, json);
        arr_builder.append(item);

        skip_whitespace(json);
        if (**json == ']') {
            (*json)++;
            break;
        }
        if (**json != ',') {
            printf("Expected ',' or ']', got '%c'\n", **json);
            return builder->createNull();
        }
        (*json)++;
        skip_whitespace(json);
    }
    return arr_builder.final();
}

static Item parse_object(Input *input, MarkBuilder* builder, const char **json) {
    if (**json != '{') return builder->createNull();

    MapBuilder map_builder = builder->map();

    (*json)++; // skip '{'
    skip_whitespace(json);
    if (**json == '}') { // empty map
        (*json)++;
        return map_builder.final();
    }

    while (**json) {
        String* key = parse_string(input, builder, json);
        if (!key) return map_builder.final();

        skip_whitespace(json);
        if (**json != ':') return map_builder.final();
        (*json)++;
        skip_whitespace(json);

        Item value = parse_value(input, builder, json);
        // Use the String* directly
        map_builder.put(key, value);

        skip_whitespace(json);
        if (**json == '}') {
            (*json)++;
            break;
        }
        if (**json != ',') return map_builder.final();
        (*json)++;
        skip_whitespace(json);
    }
    return map_builder.final();
}

static Item parse_value(Input *input, MarkBuilder* builder, const char **json) {
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return parse_object(input, builder, json);
        case '[':
            return parse_array(input, builder, json);
        case '"': {
            String* str = parse_string(input, builder, json);
            if (!str) return builder->createNull();
            if (str == &EMPTY_STRING) return builder->createNull();
            // String is already allocated, just wrap it in an Item
            return (Item){.item = s2it(str)};
        }
        case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                return builder->createBool(true);
            }
            return builder->createNull();
        case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                return builder->createBool(false);
            }
            return builder->createNull();
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                return builder->createNull();
            }
            return builder->createNull();
        default:
            if ((**json >= '0' && **json <= '9') || **json == '-') {
                return parse_number(input, builder, json);
            }
            return builder->createNull();
    }
}

void parse_json(Input* input, const char* json_string) {
    printf("json_parse\n");
    MarkBuilder builder(input);
    input->root = parse_value(input, &builder, &json_string);
}
