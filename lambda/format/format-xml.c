#include "../transpiler.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// Extract pointer from an Item
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))

// Extract boolean value from an Item
#define get_bool_value(item) ((bool)((item) & 0xFF))

// Extract integer value from an Item (for 56-bit signed integers)
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)

static void format_item(StrBuf* sb, Item item, const char* tag_name);

static void format_xml_string(StrBuf* sb, String* str) {
    if (!str || !str->chars) return;
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '<':
            strbuf_append_str(sb, "&lt;");
            break;
        case '>':
            strbuf_append_str(sb, "&gt;");
            break;
        case '&':
            strbuf_append_str(sb, "&amp;");
            break;
        case '"':
            strbuf_append_str(sb, "&quot;");
            break;
        case '\'':
            strbuf_append_str(sb, "&apos;");
            break;
        default:
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                // Control characters - encode as numeric character reference
                char hex_buf[10];
                snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                strbuf_append_str(sb, hex_buf);
            } else {
                strbuf_append_char(sb, c);
            }
            break;
        }
    }
}

static void format_number(StrBuf* sb, Item item) {
    if (item == 0) return;
    
    LambdaItem lambda_item = (LambdaItem)item;
    if (lambda_item.type_id == 0 && lambda_item.raw_pointer == NULL) return;
    
    TypeId type = get_type_id(lambda_item);
    
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
                strbuf_append_str(sb, "NaN");
            } else if (isinf(*dptr)) {
                if (*dptr > 0) {
                    strbuf_append_str(sb, "INF");
                } else {
                    strbuf_append_str(sb, "-INF");
                }
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.15g", *dptr);
                strbuf_append_str(sb, num_buf);
            }
        }
    } else if (type == LMD_TYPE_INT64) {
        // 64-bit integer stored as pointer
        long* lptr = (long*)get_pointer(item);
        if (lptr) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%ld", *lptr);
            strbuf_append_str(sb, num_buf);
        }
    }
}

static void format_array(StrBuf* sb, Array* arr, const char* tag_name) {
    printf("format_array: arr %p, length %ld\n", (void*)arr, arr ? arr->length : 0);
    if (arr && arr->length > 0) {
        for (long i = 0; i < arr->length; i++) {
            Item item = arr->items[i];
            format_item(sb, item, tag_name ? tag_name : "item");
        }
    }
}

static void format_map_attributes(StrBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            
            // Only output primitive types as attributes
            TypeId field_type = field->type->type_id;
            if (field_type == LMD_TYPE_STRING || field_type == LMD_TYPE_INT || 
                field_type == LMD_TYPE_FLOAT || field_type == LMD_TYPE_BOOL) {
                
                strbuf_append_char(sb, ' ');
                strbuf_append_format(sb, "%.*s=\"", (int)field->name->length, field->name->str);
                
                if (field_type == LMD_TYPE_STRING) {
                    String* str = *(String**)data;
                    if (str) {
                        format_xml_string(sb, str);
                    }
                } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                    long int_val = *(long*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%ld", int_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_FLOAT) {
                    double float_val = *(double*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_BOOL) {
                    bool bool_val = *(bool*)data;
                    strbuf_append_str(sb, bool_val ? "true" : "false");
                }
                
                strbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_map_elements(StrBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    printf("format_map_elements: map_type %p, map_data %p\n", (void*)map_type, (void*)map_data);
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            
            TypeId field_type = field->type->type_id;
            
            // Debug: print field information
            printf("Field %d: name length=%ld, name str='%.*s'\n", 
                   i, field->name->length, (int)field->name->length, field->name->str);
            
            if (field_type == LMD_TYPE_STRING) {
                printf("format string field");
                String* str = *(String**)data;
                
                // Use field->name->length to get proper field name length
                strbuf_append_char(sb, '<');
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
                if (str) {
                    format_xml_string(sb, str);
                }
                strbuf_append_str(sb, "</");
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
            } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                long int_val = *(long*)data;
                strbuf_append_char(sb, '<');
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
                strbuf_append_format(sb, "%ld", int_val);
                strbuf_append_str(sb, "</");
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
            } else if (field_type == LMD_TYPE_FLOAT) {
                double float_val = *(double*)data;
                strbuf_append_char(sb, '<');
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
                strbuf_append_format(sb, "%.15g", float_val);
                strbuf_append_str(sb, "</");
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
            } else if (field_type == LMD_TYPE_BOOL) {
                bool bool_val = *(bool*)data;
                strbuf_append_char(sb, '<');
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
                strbuf_append_str(sb, bool_val ? "true" : "false");
                strbuf_append_str(sb, "</");
                strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                strbuf_append_char(sb, '>');
            }
            else if (field_type == LMD_TYPE_NULL) {
                strbuf_append_str(sb, "\"\"");
            } else {
                // For complex types, create a proper null-terminated tag name
                printf("format complex field: %d\n", field_type);
                StrBuf* tag_buf = strbuf_new();
                strbuf_append_format(tag_buf, "%.*s", (int)field->name->length, field->name->str);
                char* tag_name = tag_buf->str;
                
                Item item_data = *(Item*)data;
                format_item(sb, item_data, tag_name);
                
                strbuf_free(tag_buf);
            }
        }
        field = field->next;
    }
}

static void format_map(StrBuf* sb, Map* mp, const char* tag_name) {
    printf("format_map: mp %p, type %p, data %p\n", (void*)mp, (void*)mp->type, (void*)mp->data);
    
    if (!tag_name) tag_name = "object";
    
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, tag_name);
    strbuf_append_char(sb, '>');
    
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        // Add child elements
        format_map_elements(sb, map_type, mp->data);
    }
    
    strbuf_append_str(sb, "</");
    strbuf_append_str(sb, tag_name);
    strbuf_append_char(sb, '>');
}

static void format_item(StrBuf* sb, Item item, const char* tag_name) {
    // Safety check for null pointer
    if (!sb) return;
    
    // Check if item is null (0)
    if (item == 0) {
        if (!tag_name) tag_name = "value";
        strbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    // Additional safety check for LambdaItem structure
    LambdaItem lambda_item = (LambdaItem)item;
    if (lambda_item.type_id == 0 && lambda_item.raw_pointer == NULL) {
        if (!tag_name) tag_name = "value";
        strbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    TypeId type = get_type_id(lambda_item);
    
    if (!tag_name) tag_name = "value";
    
    switch (type) {
    case LMD_TYPE_NULL:
        strbuf_append_format(sb, "<%s/>", tag_name);
        break;
    case LMD_TYPE_BOOL: {
        bool val = get_bool_value(item);
        strbuf_append_format(sb, "<%s>%s</%s>", tag_name, val ? "true" : "false", tag_name);
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        strbuf_append_format(sb, "<%s>", tag_name);
        format_number(sb, item);
        strbuf_append_format(sb, "</%s>", tag_name);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)get_pointer(item);
        strbuf_append_format(sb, "<%s>", tag_name);
        if (str) {
            format_xml_string(sb, str);
        }
        strbuf_append_format(sb, "</%s>", tag_name);
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item;
        if (arr) {
            strbuf_append_format(sb, "<%s>", tag_name);
            format_array(sb, arr, "item");
            strbuf_append_format(sb, "</%s>", tag_name);
        } else {
            strbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item;
        if (mp) {
            format_map(sb, mp, tag_name);
        } else {
            strbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* element = (Element*)item;
        if (!element || !element->type) {
            strbuf_append_format(sb, "<%s/>", tag_name);
            break;
        }
        
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        
        char element_name[256];
        snprintf(element_name, sizeof(element_name), "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
        
        strbuf_append_char(sb, '<');
        strbuf_append_str(sb, element_name);
        strbuf_append_char(sb, '>');
        
        // For now, just add simple content without trying to parse attributes/children
        if (elmt_type && elmt_type->length > 0 && element->data) {
            TypeMap* map_type = (TypeMap*)elmt_type;
            print_named_items(sb, map_type, element->data);
        }
        
        strbuf_append_str(sb, "</");
        strbuf_append_str(sb, element_name);
        strbuf_append_char(sb, '>');
        break;
    }
    default:
        // unknown types
        printf("format_item: unknown type %d\n", type);
        strbuf_append_format(sb, "<%s/>", tag_name);
        break;
    }
}

String* format_xml(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    printf("format_xml: root_item %p\n", (void*)root_item);
    
    // Add XML declaration
    strbuf_append_str(sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    
    format_item(sb, root_item, "root");
    
    return strbuf_to_string(sb);
}

// Convenience function that formats XML to a provided StrBuf
void format_xml_to_strbuf(StrBuf* sb, Item root_item) {
    format_item(sb, root_item, "root");
}
