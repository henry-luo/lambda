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

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);  string->ref_cnt = 0;
    strbuf_full_reset(sb);
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

static Map* parse_object(Input *input, const char **json) {
    if (**json != '{') return NULL;
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    (*json)++; // skip '{'
    skip_whitespace(json);
    if (**json == '}') { // empty map
        (*json)++;  return mp;
    }

    LambdaTypeMap *map_type = (LambdaTypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
    if (!map_type) { return mp; }
    mp->type = map_type;
    int byte_offset = 0, byte_cap = 64;  ShapeEntry* prev_entry = NULL;
    mp->data = pool_calloc(input->pool, byte_cap);  mp->data_cap = byte_cap;
    if (!mp->data) return mp;
    while (**json) {
        String* key = parse_string(input, json);
        if (!key) return mp;

        skip_whitespace(json);
        if (**json != ':') return mp;
        (*json)++;

        LambdaItem value = (LambdaItem)parse_value(input, json);

        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(input->pool, 
            sizeof(ShapeEntry) + sizeof(StrView));
        StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
        nv->str = key->chars;  nv->length = key->len;
        shape_entry->name = nv;
        TypeId type_id = value.type_id == LMD_TYPE_RAW_POINTER ? 
            ((Container*)value.raw_pointer)->type_id : value.type_id;
        shape_entry->type = type_info[type_id].type;
        printf("shape_entry: key: %.*s, type: %d\n", 
            (int)nv->length, nv->str, shape_entry->type->type_id);
        shape_entry->byte_offset = byte_offset;
        if (!prev_entry) { map_type->shape = shape_entry; } 
        else { prev_entry->next = shape_entry; }
        prev_entry = shape_entry;
        map_type->length++;

        int bsize = type_info[type_id].byte_size;
        byte_offset += bsize;
        if (byte_offset > byte_cap) {
            byte_cap *= 2;
            void* new_data = pool_calloc(input->pool, byte_cap);
            if (!new_data) return mp;
            memcpy(new_data, mp->data, byte_offset - bsize);
            pool_variable_free(input->pool, mp->data);
            mp->data = new_data;  mp->data_cap = byte_cap;
        }
        void* field_ptr = (char*)mp->data + byte_offset - bsize;
        switch (type_id) {
        case LMD_TYPE_NULL:
            *(void**)field_ptr = NULL;
            break;
        case LMD_TYPE_BOOL:
            *(bool*)field_ptr = value.bool_val;             
            break;
        case LMD_TYPE_INT:
            *(long*)field_ptr = *(long*)value.pointer;
            break;
        case LMD_TYPE_FLOAT:
            *(double*)field_ptr = *(double*)value.pointer;
            break;
        case LMD_TYPE_STRING:
            *(String**)field_ptr = (String*)value.pointer;
            break;
        case LMD_TYPE_ARRAY:  case LMD_TYPE_MAP:
            *(Map**)field_ptr = (Map*)value.raw_pointer;
            break;
        default:
            printf("unknown type %d\n", value.type_id);
        }
        
        skip_whitespace(json);
        if (**json == '}') { (*json)++;  break; }
        if (**json != ',') return mp;
        (*json)++;
    }
    map_type->byte_size = byte_offset;
    arraylist_append(input->type_list, map_type);
    map_type->type_index = input->type_list->length - 1;
    printf("parsed map length: %ld, byte_size: %ld, shape: %p\n", 
        map_type->length, map_type->byte_size, map_type->shape);
    return mp;
}

static Item parse_value(Input *input, const char **json) {
    printf("parse_value: %s\n", *json);
    skip_whitespace(json);
    switch (**json) {
        case '{':
            return (Item)parse_object(input, json);
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
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);
    input->root = parse_value(input, &json_string);
    return input;
}
