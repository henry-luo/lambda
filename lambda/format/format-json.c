#include "../transpiler.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// Extract pointer from an Item
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))

// Extract boolean value from an Item
#define get_bool_value(item) ((bool)((item) & 0xFF))

// Extract integer value from an Item (for 56-bit signed integers)
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)

static void format_item(StrBuf* sb, Item item);

static void format_string(StrBuf* sb, String* str) {
    strbuf_append_char(sb, '"');
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '"':
            strbuf_append_str(sb, "\\\"");
            break;
        case '\\':
            strbuf_append_str(sb, "\\\\");
            break;
        case '/':
            strbuf_append_str(sb, "\\/");
            break;
        case '\b':
            strbuf_append_str(sb, "\\b");
            break;
        case '\f':
            strbuf_append_str(sb, "\\f");
            break;
        case '\n':
            strbuf_append_str(sb, "\\n");
            break;
        case '\r':
            strbuf_append_str(sb, "\\r");
            break;
        case '\t':
            strbuf_append_str(sb, "\\t");
            break;
        default:
            if (c < 0x20) {
                // Control characters - encode as \uXXXX
                char hex_buf[7];
                snprintf(hex_buf, sizeof(hex_buf), "\\u%04x", (unsigned char)c);
                strbuf_append_str(sb, hex_buf);
            } else {
                strbuf_append_char(sb, c);
            }
            break;
        }
    }
    strbuf_append_char(sb, '"');
}

static void format_number(StrBuf* sb, Item item) {
    TypeId type = get_type_id((LambdaItem)item);
    
    if (type == LMD_TYPE_INT) {
        // 56-bit signed integer stored directly in the item
        int64_t val = get_int_value(item);
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, val);
        strbuf_append_str(sb, num_buf);
    } else if (type == LMD_TYPE_FLOAT) {
        // Double stored as pointer
        double* dptr = (double*)get_pointer(item);
        if (dptr) {
            char num_buf[32];
            // Check for special values
            if (isnan(*dptr)) {
                strbuf_append_str(sb, "null");
            } else if (isinf(*dptr)) {
                strbuf_append_str(sb, "null");
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.15g", *dptr);
                strbuf_append_str(sb, num_buf);
            }
        } else {
            strbuf_append_str(sb, "null");
        }
    } else if (type == LMD_TYPE_INT64) {
        // 64-bit integer stored as pointer
        long* lptr = (long*)get_pointer(item);
        if (lptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%ld", *lptr);
            strbuf_append_str(sb, num_buf);
        } else {
            strbuf_append_str(sb, "null");
        }
    }
}

static void format_array(StrBuf* sb, Array* arr) {
    printf("format_array: arr %p, length %ld\n", (void*)arr, arr ? arr->length : 0);
    strbuf_append_char(sb, '[');
    if (arr && arr->length > 0) {
        for (long i = 0; i < arr->length; i++) {
            if (i > 0) { strbuf_append_char(sb, ','); }
            Item item = arr->items[i];
            format_item(sb, item);
        }
    }
    strbuf_append_char(sb, ']');
}

static void format_map(StrBuf* sb, Map* mp) {
    printf("format_map: mp %p, type %p, data %p\n", (void*)mp, (void*)mp->type, (void*)mp->data);
    strbuf_append_char(sb, '{');
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        print_named_items(sb, map_type, mp->data);
    }
    strbuf_append_char(sb, '}');
}

static void format_item(StrBuf* sb, Item item) {
    TypeId type = get_type_id((LambdaItem)item);
    switch (type) {
    case LMD_TYPE_NULL:
        strbuf_append_str(sb, "null");
        break;
    case LMD_TYPE_BOOL: {
        bool val = get_bool_value(item);
        strbuf_append_str(sb, val ? "true" : "false");
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        format_number(sb, item);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)get_pointer(item);
        if (str) {
            format_string(sb, str);
        } else {
            strbuf_append_str(sb, "null");
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item;
        format_array(sb, arr);
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item;
        format_map(sb, mp);
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* element = (Element*)item;
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        
        strbuf_append_char(sb, '{');
        strbuf_append_str(sb, "\"tag\":\"");
        if (elmt_type && elmt_type->name.str) {
            strbuf_append_str_n(sb, elmt_type->name.str, elmt_type->name.length);
        }
        strbuf_append_str(sb, "\"");
        
        // Add attributes if any
        if (elmt_type && elmt_type->length > 0 && element->data) {
            strbuf_append_str(sb, ",\"attributes\":");
            Map temp_map = {0};
            temp_map.type = (Type*)elmt_type;
            temp_map.data = element->data;
            temp_map.data_cap = element->data_cap;
            format_map(sb, &temp_map);
        }
        
        // Add children if any
        if (elmt_type && elmt_type->content_length > 0) {
            strbuf_append_str(sb, ",\"children\":");
            List* list = (List*)element;
            format_array(sb, (Array*)list);
        }
        
        strbuf_append_char(sb, '}');
        break;
    }
    default:
        // unknown types
        printf("format_item: unknown type %d\n", type);
        strbuf_append_str(sb, "null");
        break;
    }
}

String* format_json(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    printf("format_json: root_item %p\n", (void*)root_item);
    format_item(sb, root_item);
    
    return strbuf_to_string(sb);
}

// Convenience function that formats JSON to a provided StrBuf
void format_json_to_strbuf(StrBuf* sb, Item root_item) {
    format_item(sb, root_item);
}
