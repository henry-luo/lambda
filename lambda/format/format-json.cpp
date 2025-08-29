#include "format.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// Forward declarations
static void format_item_with_indent(StrBuf* sb, Item item, int indent);
static void format_string(StrBuf* sb, String* str);
static void format_array_with_indent(StrBuf* sb, Array* arr, int indent);
static void format_map_with_indent(StrBuf* sb, Map* mp, int indent);

// Helper function to add indentation
static void add_indent(StrBuf* sb, int indent) {
    for (int i = 0; i < indent; i++) {
        strbuf_append_str(sb, "  ");
    }
}

// JSON-specific function to format map/object contents properly
static void format_json_map_contents(StrBuf* sb, TypeMap* map_type, void* map_data, int indent) {
    if (!map_type || !map_data) return;
    
    // Prevent infinite recursion
    if (indent > 10) {
        strbuf_append_str(sb, "\"[MAX_DEPTH]\":null");
        return;
    }
    
    // Safety check for map_type length
    if (map_type->length < 0 || map_type->length > 1000) {
        strbuf_append_str(sb, "\"error\":\"invalid map length\"");
        return;
    }

    ShapeEntry *field = map_type->shape;
    bool first = true;
    
    for (int i = 0; i < map_type->length; i++) {
        // safety check for valid field pointer
        if (!field || (uintptr_t)field < 0x1000) {
            strbuf_append_str(sb, "\"error\":\"invalid field pointer\"");
            break;
        }
        
        if (!first) {
            strbuf_append_str(sb, ",\n");
        } else {
            strbuf_append_char(sb, '\n');
            first = false;
        }
        
        add_indent(sb, indent + 1);
        void* data = ((char*)map_data) + field->byte_offset;
        
        if (!field->name) { // nested map
            Map *nest_map = *(Map**)data;
            if (nest_map && nest_map->type) {
                TypeMap *nest_map_type = (TypeMap*)nest_map->type;
                format_json_map_contents(sb, nest_map_type, nest_map->data, indent + 1);
            }
        } else {
            // Safety checks
            if (!field->name || (uintptr_t)field->name < 0x1000 || 
                !field->type || (uintptr_t)field->type < 0x1000) {
                goto advance_field;
            }
            
            // Format the key (always quoted in JSON)
            strbuf_append_format(sb, "\"%.*s\":", (int)field->name->length, field->name->str);
            
            // Format the value based on type
            switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                strbuf_append_str(sb, "null");
                break;
            case LMD_TYPE_BOOL:
                strbuf_append_str(sb, *(bool*)data ? "true" : "false");
                break;                    
            case LMD_TYPE_INT:
            case LMD_TYPE_INT64:
                strbuf_append_format(sb, "%ld", *(long*)data);
                break;
            case LMD_TYPE_FLOAT:
                strbuf_append_format(sb, "%g", *(double*)data);
                break;
            case LMD_TYPE_STRING: {
                String *string = *(String**)data;
                if (string) {
                    // Check if this is EMPTY_STRING and handle specially
                    if (string == &EMPTY_STRING) {
                        strbuf_append_str(sb, "\"\"");
                    } else if (string->len == 10 && strncmp(string->chars, "lambda.nil", 10) == 0) {
                        // Handle literal "lambda.nil" content as empty string
                        strbuf_append_str(sb, "\"\"");
                    } else {
                        format_string(sb, string);
                    }
                } else {
                    strbuf_append_str(sb, "\"\"");
                }
                break;
            }
            case LMD_TYPE_ARRAY: {
                Array *arr = *(Array**)data;
                format_array_with_indent(sb, arr, indent + 1);
                break;
            }
            case LMD_TYPE_MAP: {
                Map *mp = *(Map**)data;
                if (mp && mp->type) {
                    strbuf_append_char(sb, '{');
                    format_json_map_contents(sb, (TypeMap*)mp->type, mp->data, indent + 1);
                    strbuf_append_char(sb, '\n');
                    add_indent(sb, indent + 1);
                    strbuf_append_char(sb, '}');
                } else {
                    strbuf_append_str(sb, "{}");
                }
                break;
            }
            default:
                strbuf_append_str(sb, "null");
                break;
            }
        }
        
        advance_field:
        ShapeEntry *next_field = field->next;
        field = next_field;
    }
    
    if (!first) {
        strbuf_append_char(sb, '\n');
        add_indent(sb, indent);
    }
}

static void format_string(StrBuf* sb, String* str) {
    // Handle EMPTY_STRING specially
    if (str == &EMPTY_STRING) {
        strbuf_append_str(sb, "\"\"");
        return;
    }
    
    // Handle literal "lambda.nil" content as empty string
    if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
        strbuf_append_str(sb, "\"\"");
        return;
    }
    
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

static void format_array_with_indent(StrBuf* sb, Array* arr, int indent) {
    printf("format_array_with_indent: arr %p, length %ld\n", (void*)arr, arr ? arr->length : 0);
    strbuf_append_char(sb, '[');
    if (arr && arr->length > 0) {
        strbuf_append_char(sb, '\n');
        for (long i = 0; i < arr->length; i++) {
            if (i > 0) { 
                strbuf_append_str(sb, ",\n"); 
            }
            add_indent(sb, indent + 1);
            Item item = arr->items[i];
            format_item_with_indent(sb, item, indent + 1);
        }
        strbuf_append_char(sb, '\n');
        add_indent(sb, indent);
    }
    strbuf_append_char(sb, ']');
}

static void format_map_with_indent(StrBuf* sb, Map* mp, int indent) {
    printf("format_map_with_indent: mp %p, type %p, data %p\n", (void*)mp, (void*)mp->type, (void*)mp->data);
    if (mp->type_id != LMD_TYPE_ELEMENT) strbuf_append_char(sb, '{');
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        format_json_map_contents(sb, map_type, mp->data, indent);
    }
    if (mp->type_id != LMD_TYPE_ELEMENT) strbuf_append_char(sb, '}');
}

static void format_item_with_indent(StrBuf* sb, Item item, int indent) {
    TypeId type = get_type_id(item);
    switch (type) {
    case LMD_TYPE_NULL:
        strbuf_append_str(sb, "null");
        break;
    case LMD_TYPE_BOOL: {
        bool val = item.bool_val;
        strbuf_append_str(sb, val ? "true" : "false");
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        format_number(sb, item);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        if (str) {
            format_string(sb, str);
        } else {
            strbuf_append_str(sb, "null");
        }
        break;
    }
    case LMD_TYPE_SYMBOL: {
        String* str = (String*)item.pointer;
        if (str) {
            format_string(sb, str);
        } else {
            strbuf_append_str(sb, "null");
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item.pointer;
        format_array_with_indent(sb, arr, indent);
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item.pointer;
        format_map_with_indent(sb, mp, indent);
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* element = (Element*)item.pointer;
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        
        strbuf_append_format(sb, "\n{\"$\":\"%.*s\"", (int)elmt_type->name.length, elmt_type->name.str);
        
        // Add attributes as direct properties
        if (elmt_type && elmt_type->length > 0 && element->data) {
            // Format attributes directly as properties, not wrapped in "attr"
            Map temp_map;  memset(&temp_map, 0, sizeof(Map));
            temp_map.type_id = LMD_TYPE_MAP;
            temp_map.type = (Type*)elmt_type;
            temp_map.data = element->data;
            temp_map.data_cap = element->data_cap;
            
            // Add comma and format the map contents directly (without braces)
            strbuf_append_str(sb, ",");
            format_json_map_contents(sb, (TypeMap*)elmt_type, element->data, indent);
        }
        
        // Add children if any
        if (elmt_type && elmt_type->content_length > 0) {
            strbuf_append_str(sb, ",\"_\":");
            List* list = (List*)element;
            format_array_with_indent(sb, (Array*)list, indent);
        }
        
        strbuf_append_char(sb, '}');
        break;
    }
    default:
        // unknown types
        printf("format_item_with_indent: unknown type %d\n", type);
        strbuf_append_str(sb, "null");
        break;
    }
}

// Legacy format_item function for backward compatibility
static void format_item(StrBuf* sb, Item item) {
    format_item_with_indent(sb, item, 0);
}

// Legacy functions for backward compatibility
static void format_array(StrBuf* sb, Array* arr) {
    format_array_with_indent(sb, arr, 0);
}

static void format_map(StrBuf* sb, Map* mp) {
    format_map_with_indent(sb, mp, 0);
}

String* format_json(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    printf("format_json: root_item %p\n", (void*)root_item.pointer);
    format_item(sb, root_item);
    
    return strbuf_to_string(sb);
}

// Convenience function that formats JSON to a provided StrBuf
void format_json_to_strbuf(StrBuf* sb, Item root_item) {
    format_item(sb, root_item);
}
