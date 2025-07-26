#include "input.h"

static Item parse_value(Input *input, const char **json);

static void skip_whitespace(const char **json) {
    while (**json && (**json == ' ' || **json == '\n' || **json == '\r' || **json == '\t')) {
        (*json)++;
    }
}

static String* parse_string(Input *input, const char **json) {
    if (**json != '"') return NULL;
    StrBuf* sb = input->sb;
    
    (*json)++; // Skip opening quote
    while (**json && **json != '"') {
        if (**json == '\\') {
            (*json)++;
            switch (**json) {
                case '"': strbuf_append_char(sb, '"'); break;
                case '\\': strbuf_append_char(sb, '\\'); break;
                case '/': strbuf_append_char(sb, '/'); break;
                case 'b': strbuf_append_char(sb, '\b'); break;
                case 'f': strbuf_append_char(sb, '\f'); break;
                case 'n': strbuf_append_char(sb, '\n'); break;
                case 'r': strbuf_append_char(sb, '\r'); break;
                case 't': strbuf_append_char(sb, '\t'); break;
                // handle \uXXXX escapes
                case 'u': {
                    (*json)++; // skip 'u'
                    char hex[5] = {0};
                    strncpy(hex, *json, 4);
                    (*json) += 4; // skip 4 hex digits
                    int codepoint = (int)strtol(hex, NULL, 16);
                    if (codepoint < 0x80) {
                        strbuf_append_char(sb, (char)codepoint);
                    } else if (codepoint < 0x800) {
                        strbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                        strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    } else {
                        strbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                        strbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                        strbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                    }
                } break;
                default: break; // invalid escape
            }
        } else {
            strbuf_append_char(sb, **json);
        }
        (*json)++;
    }

    if (**json == '"') {
        (*json)++; // skip closing quote
    }
    return strbuf_to_string(sb);
}

static Item parse_number(Input *input, const char **json) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
    char* end;
    *dval = strtod(*json, &end);
    *json = end;
    return d2it(dval);
}

static Array* parse_array(Input *input, const char **json) {
    if (**json != '[') return NULL;
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;

    (*json)++; // skip [ 
    skip_whitespace(json);
    if (**json == ']') { (*json)++;  return arr; }

    while (**json) {
        LambdaItem item = {.item = parse_value(input, json)};
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

        LambdaItem value = (LambdaItem)parse_value(input, json);
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
            return (Item)parse_object(input, json);
        case '[':
            return (Item)parse_array(input, json);
        case '"':
            String* str = parse_string(input, json);
            return str ? (str == &EMPTY_STRING ? ITEM_NULL : s2it(str)): (Item)NULL;
        case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                return b2it(true);
            }
            return ITEM_ERROR;
        case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                return b2it(false);
            }
            return ITEM_ERROR;
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                return ITEM_NULL;
            }
            return ITEM_ERROR;
        default:
            if ((**json >= '0' && **json <= '9') || **json == '-') {
                return parse_number(input, json);
            }
            return ITEM_ERROR;
    }
}

void parse_json(Input* input, const char* json_string) {
    printf("json_parse\n");
    input->sb = strbuf_new_pooled(input->pool);
    input->root = parse_value(input, &json_string);
}

