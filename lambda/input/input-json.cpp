#include "input.h"

static Item parse_value(Input *input, const char **json);

static void skip_whitespace(const char **json) {
    while (**json && (**json == ' ' || **json == '\n' || **json == '\r' || **json == '\t')) {
        (*json)++;
    }
}

static String* parse_string(Input *input, const char **json) {
    if (**json != '"') return NULL;
    StringBuf* sb = input->sb;
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
    return stringbuf_to_string(sb);
}

static Item parse_number(Input *input, const char **json) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return {.item = ITEM_ERROR};
    char* end;
    *dval = strtod(*json, &end);
    *json = end;
    return {.item = d2it(dval)};
}

static Array* parse_array(Input *input, const char **json) {
    if (**json != '[') return NULL;
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*json)++; // skip [ 
    skip_whitespace(json);
    if (**json == ']') { (*json)++;  return arr; }

    while (**json) {
        Item item = parse_value(input, json);
        array_append(arr, item, input->pool);

        skip_whitespace(json);
        if (**json == ']') { (*json)++;  break; }
        if (**json != ',') {
            printf("Expected ',' or ']', got '%c'\n", **json);
            return NULL; // invalid format
        }
        (*json)++;
        skip_whitespace(json);
    }
    return arr;
}

static Map* parse_object(Input *input, const char **json) {
    if (**json != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    (*json)++; // skip '{'
    skip_whitespace(json);
    if (**json == '}') { // empty map
        (*json)++;  return mp;
    }

    while (**json) {
        String* key = parse_string(input, json);
        if (!key) return mp;

        skip_whitespace(json);
        if (**json != ':') return mp;
        (*json)++;
        skip_whitespace(json);

        Item value = parse_value(input, json);
        map_put(mp, key, value, input);

        skip_whitespace(json);
        if (**json == '}') { (*json)++;  break; }
        if (**json != ',') return mp;
        (*json)++;
        skip_whitespace(json);
    }
    return mp;
}

static Item parse_value(Input *input, const char **json) {
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return {.raw_pointer = parse_object(input, json)};
        case '[':
            return {.raw_pointer = parse_array(input, json)};
        case '"': {
            String* str = parse_string(input, json);
            return str ? (str == &EMPTY_STRING ? (Item){.item = ITEM_NULL} : (Item){.item = s2it(str)}) : (Item){.item = 0};
        }
        case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                return {.item = b2it(true)};
            }
            return {.item = ITEM_ERROR};
        case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                return {.item = b2it(false)};
            }
            return {.item = ITEM_ERROR};
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                return {.item = ITEM_NULL};
            }
            return {.item = ITEM_ERROR};
        default:
            if ((**json >= '0' && **json <= '9') || **json == '-') {
                return parse_number(input, json);
            }
            return {.item = ITEM_ERROR};
    }
}

void parse_json(Input* input, const char* json_string) {
    printf("json_parse\n");
    input->sb = stringbuf_new(input->pool);
    input->root = parse_value(input, &json_string);
}

