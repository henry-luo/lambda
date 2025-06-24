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
    printf("parse_string: %s\n", *json);
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
    sb->str = NULL;  sb->length = 0;  sb->capacity = 0;
    return string;
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
    printf("parse_array: %s\n", *json);
    if (**json != '[') return NULL;
    Array* arr = array_pooled(input->pool);
    printf("array_pooled: arr %p\n", arr);
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
    }
    printf("parsed array length: %ld\n", arr->length);
    return arr;
}

static Item parse_object(Input *input, const char **json) {
    if (**json != '{') return ITEM_ERROR;
    (*json)++; // Skip {
    return ITEM_ERROR;

    // Map* map_obj = map(0); // Assuming 0 is a generic map type

    // skip_whitespace(json);
    // if (**json == '}') {
    //     (*json)++;
    //     return m2it(map_obj);
    // }

    // while (**json) {
    //     String* key = parse_string(input, json);
    //     if (!key) return ITEM_ERROR;

    //     skip_whitespace(json);
    //     if (**json != ':') return ITEM_ERROR;
    //     (*json)++;

    //     Item value = parse_value(input, json);
    //     map_set(map_obj, key->chars, value);

    //     skip_whitespace(json);
    //     if (**json == '}') {
    //         (*json)++;
    //         break;
    //     }

    //     if (**json != ',') return ITEM_ERROR;
    //     (*json)++;
    // }
    // return m2it(map_obj);
}

static Item parse_value(Input *input, const char **json) {
    printf("parse_value: %s\n", *json);
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return parse_object(input, json);
        case '[':
            return (Item)parse_array(input, json);
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

Input* json_parse(const char* json_string) {
    printf("json_parse: %s\n", json_string);
    Input* input = malloc(sizeof(Input));
    input->path = NULL; // path for JSON input
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { free(input);  return NULL; }
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);
    input->root = parse_value(input, &json_string);
    return input;
}
