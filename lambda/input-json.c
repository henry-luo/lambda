#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>

static Item parse_value(Input *input, const char **json);

static void skip_whitespace(const char **json) {
    while (**json && (**json == ' ' || **json == '\n' || **json == '\r' || **json == '\t')) {
        (*json)++;
    }
}

static String* parse_string(Input *input, const char **json) {
    if (**json != '"') return NULL;
    StrBuf* sb;
    if (!input->sb) {
        input->sb = strbuf_new_pooled(input->pool);
        if (!input->sb) return NULL;
    }
    sb = input->sb;

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
                // TODO: Handle \uXXXX escapes
                default: break; // Invalid escape
            }
        } else {
            strbuf_append_char(sb, **json);
        }
        (*json)++;
    }

    if (**json == '"') {
        (*json)++; // Skip closing quote
    }

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_reset(sb);
    return s2it(string);
}

static Item parse_number(Input *input, const char **json) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
    char* end;
    *dval = strtod(*json, &end);
    *json = end;
    return d2item(dval);
}

static Item parse_array(Input *input, const char **json) {
    if (**json != '[') return ITEM_ERROR;
    (*json)++; // Skip [ 

    Array* arr = array_pooled(input->pool);
    if (!arr) return ITEM_ERROR;

    skip_whitespace(json);
    if (**json == ']') {
        (*json)++;
        return a2it(arr);
    }

    while (**json) {
        Item value = parse_value(input, json);
        array_push(arr, value);

        skip_whitespace(json);
        if (**json == ']') {
            (*json)++;
            break;
        }

        if (**json != ',') return ITEM_ERROR; // Invalid format
        (*json)++;
    }
    return a2it(arr);
}

static Item parse_object(Input *input, const char **json) {
    if (**json != '{') return ITEM_ERROR;
    (*json)++; // Skip {

    Map* map_obj = map(0); // Assuming 0 is a generic map type

    skip_whitespace(json);
    if (**json == '}') {
        (*json)++;
        return m2it(map_obj);
    }

    while (**json) {
        String* key = parse_string(input, json);
        if (!key) return ITEM_ERROR;

        skip_whitespace(json);
        if (**json != ':') return ITEM_ERROR;
        (*json)++;

        Item value = parse_value(input, json);
        map_set(map_obj, key->chars, value);

        skip_whitespace(json);
        if (**json == '}') {
            (*json)++;
            break;
        }

        if (**json != ',') return ITEM_ERROR;
        (*json)++;
    }

    return m2it(map_obj);
}

static Item parse_value(Input *input, const char **json) {
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return parse_object(input, json);
        case '[':
            return parse_array(input, json);
        case '"':
            return s2it(parse_string(input, json));
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

Item json_parse(const char* json_string) {
    return parse_value(&json_string);
}
