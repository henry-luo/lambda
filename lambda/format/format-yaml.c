#include "../transpiler.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// extract pointer from an Item
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))

// extract boolean value from an Item
#define get_bool_value(item) ((bool)((item) & 0xFF))

// extract integer value from an Item (for 56-bit signed integers)
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)

static void format_yaml_item(StrBuf* sb, Item item, int indent_level);

// format a string value for YAML - handle quoting and escaping
static void format_yaml_string(StrBuf* sb, String* str) {
    printf("format_yaml_string: formatting string len=%u\n", str ? str->len : 0);
    fflush(stdout);
    
    if (!str) {
        strbuf_append_str(sb, "null");
        return;
    }
    
    const char* s = str->chars;
    size_t len = str->len;
    bool needs_quotes = false;
    
    // check if string needs quotes (contains special chars, starts with special chars, etc.)
    if (len == 0 || strchr(s, ':') || strchr(s, '\n') || strchr(s, '"') || 
        strchr(s, '\'') || strchr(s, '#') || strchr(s, '-') || strchr(s, '[') || 
        strchr(s, ']') || strchr(s, '{') || strchr(s, '}') || isspace(s[0]) || 
        isspace(s[len-1])) {
        needs_quotes = true;
    }
    
    // also check for yaml reserved words
    if (!needs_quotes && (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || 
                         strcmp(s, "null") == 0 || strcmp(s, "yes") == 0 || 
                         strcmp(s, "no") == 0)) {
        needs_quotes = true;
    }
    
    if (needs_quotes) {
        strbuf_append_char(sb, '"');
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (needs_quotes) {
            switch (c) {
            case '"':
                strbuf_append_str(sb, "\\\"");
                break;
            case '\\':
                strbuf_append_str(sb, "\\\\");
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
                strbuf_append_char(sb, c);
                break;
            }
        } else {
            strbuf_append_char(sb, c);
        }
    }
    
    if (needs_quotes) {
        strbuf_append_char(sb, '"');
    }
}

// format a number for YAML
static void format_yaml_number(StrBuf* sb, Item item) {
    printf("format_yaml_number: formatting number\n");
    fflush(stdout);
    
    TypeId type = get_type_id((LambdaItem)item);
    
    if (type == LMD_TYPE_INT) {
        // 56-bit signed integer stored directly in the item
        int64_t val = get_int_value(item);
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%" PRId64, val);
        strbuf_append_str(sb, num_buf);
    } else if (type == LMD_TYPE_FLOAT) {
        // double stored as pointer
        double* dptr = (double*)get_pointer(item);
        if (dptr) {
            char num_buf[32];
            // check for special values
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

// add indentation for nested structures
static void add_yaml_indent(StrBuf* sb, int indent_level) {
    for (int i = 0; i < indent_level * 2; i++) {
        strbuf_append_char(sb, ' ');
    }
}

// format an array for YAML
static void format_yaml_array(StrBuf* sb, Array* arr, int indent_level) {
    printf("format_yaml_array: arr %p, length %ld, indent_level %d\n", 
           (void*)arr, arr ? arr->length : 0, indent_level);
    fflush(stdout);
    
    if (!arr || arr->length == 0) {
        strbuf_append_str(sb, "[]");
        return;
    }
    
    for (long i = 0; i < arr->length; i++) {
        if (i > 0 || indent_level > 0) {
            strbuf_append_char(sb, '\n');
            add_yaml_indent(sb, indent_level);
        }
        strbuf_append_str(sb, "- ");
        
        Item item = arr->items[i];
        TypeId item_type = get_type_id((LambdaItem)item);
        
        // for complex types, add proper indentation
        if (item_type == LMD_TYPE_MAP || item_type == LMD_TYPE_ELEMENT || item_type == LMD_TYPE_ARRAY) {
            format_yaml_item(sb, item, indent_level + 1);
        } else {
            format_yaml_item(sb, item, 0);
        }
    }
}

// custom function to format map items for YAML
static void format_yaml_map_items(StrBuf* sb, TypeMap* map_type, void* map_data, int indent_level) {
    printf("format_yaml_map_items: map_type %p, map_data %p, indent_level %d\n", 
           (void*)map_type, map_data, indent_level);
    fflush(stdout);
    
    if (!map_type || !map_data) {
        return;
    }
    
    bool first_item = true;
    ShapeEntry *field = map_type->shape;
    
    // iterate through the map fields using linked list
    for (int i = 0; i < map_type->length && field; i++) {
        printf("format_yaml_map_items: processing field %d\n", i);
        fflush(stdout);
        
        if (!field->name) {
            // nested map - handle specially
            void* data = ((char*)map_data) + field->byte_offset;
            Map *nest_map = *(Map**)data;
            if (nest_map && nest_map->type) {
                TypeMap *nest_map_type = (TypeMap*)nest_map->type;
                format_yaml_map_items(sb, nest_map_type, nest_map->data, indent_level);
            }
        } else {
            // named field
            void* data = ((char*)map_data) + field->byte_offset;
            
            // create a temporary item from the data based on field type
            Item field_value = 0;
            TypeId field_type = field->type->type_id;
            
            printf("format_yaml_map_items: field type = %d\n", field_type);
            fflush(stdout);
            
            switch (field_type) {
            case LMD_TYPE_NULL:
                field_value = 0; // null item
                break;
            case LMD_TYPE_BOOL:
                field_value = *(bool*)data ? 1 : 0;
                field_value |= ((uint64_t)LMD_TYPE_BOOL << 56);
                break;
            case LMD_TYPE_INT:
                field_value = *(int64_t*)data;
                field_value |= ((uint64_t)LMD_TYPE_INT << 56);
                break;
            case LMD_TYPE_INT64:
                field_value = (uint64_t)data;
                field_value |= ((uint64_t)LMD_TYPE_INT64 << 56);
                break;
            case LMD_TYPE_FLOAT:
                field_value = (uint64_t)data;
                field_value |= ((uint64_t)LMD_TYPE_FLOAT << 56);
                break;
            case LMD_TYPE_STRING:
                field_value = (uint64_t)*(void**)data;
                field_value |= ((uint64_t)LMD_TYPE_STRING << 56);
                break;
            case LMD_TYPE_ARRAY:
                field_value = (uint64_t)*(void**)data;
                field_value |= ((uint64_t)LMD_TYPE_ARRAY << 56);
                break;
            case LMD_TYPE_MAP:
                field_value = (uint64_t)*(void**)data;
                field_value |= ((uint64_t)LMD_TYPE_MAP << 56);
                break;
            default:
                // for complex types, just point to the data
                field_value = (uint64_t)data;
                field_value |= ((uint64_t)field_type << 56);
                break;
            }
            
            // skip null/unset fields
            if (field_type == LMD_TYPE_NULL) {
                // don't skip null fields, format them as null
                if (!first_item) {
                    strbuf_append_char(sb, '\n');
                }
                first_item = false;
                
                if (indent_level > 0) {
                    add_yaml_indent(sb, indent_level);
                }
                
                // add field name
                strbuf_append_format(sb, "%.*s: null", (int)field->name->length, field->name->str);
                goto advance_field;
            }
            
            if (!first_item) {
                strbuf_append_char(sb, '\n');
            }
            first_item = false;
            
            if (indent_level > 0) {
                add_yaml_indent(sb, indent_level);
            }
            
            // add field name
            strbuf_append_format(sb, "%.*s: ", (int)field->name->length, field->name->str);
            
            // format field value
            if (field_type == LMD_TYPE_MAP || field_type == LMD_TYPE_ELEMENT || field_type == LMD_TYPE_ARRAY) {
                // for complex types, add newline and proper indentation
                if (field_type == LMD_TYPE_MAP || field_type == LMD_TYPE_ELEMENT) {
                    strbuf_append_char(sb, '\n');
                }
                format_yaml_item(sb, field_value, indent_level + 1);
            } else {
                format_yaml_item(sb, field_value, 0);
            }
        }
        
        advance_field:
        field = field->next;
    }
}

// format a map for YAML
static void format_yaml_map(StrBuf* sb, Map* mp, int indent_level) {
    printf("format_yaml_map: mp %p, type %p, data %p, indent_level %d\n", 
           (void*)mp, (void*)(mp ? mp->type : NULL), (void*)(mp ? mp->data : NULL), indent_level);
    fflush(stdout);
    
    if (!mp || !mp->type) {
        strbuf_append_str(sb, "{}");
        return;
    }
    
    TypeMap* map_type = (TypeMap*)mp->type;
    format_yaml_map_items(sb, map_type, mp->data, indent_level);
}

// main function to format any item as YAML
static void format_yaml_item(StrBuf* sb, Item item, int indent_level) {
    printf("format_yaml_item: item %p, indent_level %d\n", (void*)item, indent_level);
    fflush(stdout);
    
    TypeId type = get_type_id((LambdaItem)item);
    printf("format_yaml_item: type = %d\n", type);
    fflush(stdout);
    
    switch (type) {
    case LMD_TYPE_NULL:
        printf("format_yaml_item: formatting null\n");
        fflush(stdout);
        strbuf_append_str(sb, "null");
        break;
    case LMD_TYPE_BOOL: {
        bool val = get_bool_value(item);
        printf("format_yaml_item: formatting bool = %s\n", val ? "true" : "false");
        fflush(stdout);
        strbuf_append_str(sb, val ? "true" : "false");
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        printf("format_yaml_item: formatting number type %d\n", type);
        fflush(stdout);
        format_yaml_number(sb, item);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)get_pointer(item);
        printf("format_yaml_item: formatting string %p\n", (void*)str);
        fflush(stdout);
        if (str) {
            format_yaml_string(sb, str);
        } else {
            strbuf_append_str(sb, "null");
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item;
        printf("format_yaml_item: formatting array %p\n", (void*)arr);
        fflush(stdout);
        format_yaml_array(sb, arr, indent_level);
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item;
        printf("format_yaml_item: formatting map %p\n", (void*)mp);
        fflush(stdout);
        format_yaml_map(sb, mp, indent_level);
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* element = (Element*)item;
        printf("format_yaml_item: formatting element %p\n", (void*)element);
        fflush(stdout);
        
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        
        // for yaml, represent element as an object with special "$" key for tag name
        if (indent_level > 0) {
            strbuf_append_char(sb, '\n');
            add_yaml_indent(sb, indent_level);
        }
        strbuf_append_format(sb, "$: \"%.*s\"", (int)elmt_type->name.length, elmt_type->name.str);
        
        // add attributes if any
        if (elmt_type && elmt_type->length > 0 && element->data) {
            Map temp_map = {.type_id = LMD_TYPE_ELEMENT};
            temp_map.type = (Type*)elmt_type;
            temp_map.data = element->data;
            temp_map.data_cap = element->data_cap;
            
            strbuf_append_char(sb, '\n');
            format_yaml_map_items(sb, (TypeMap*)elmt_type, element->data, indent_level);
        }
        
        // add children if any
        if (elmt_type && elmt_type->content_length > 0) {
            if (indent_level > 0) {
                strbuf_append_char(sb, '\n');
                add_yaml_indent(sb, indent_level);
            } else {
                strbuf_append_char(sb, '\n');
            }
            strbuf_append_str(sb, "_:");
            
            List* list = (List*)element;
            format_yaml_array(sb, (Array*)list, indent_level + 1);
        }
        break;
    }
    default:
        // unknown types
        printf("format_yaml_item: unknown type %d\n", type);
        fflush(stdout);
        strbuf_append_str(sb, "null");
        break;
    }
}

// yaml formatter that produces proper YAML output
String* format_yaml(VariableMemPool* pool, Item root_item) {
    printf("format_yaml: ENTRY - direct traversal version\n");
    fflush(stdout);
    
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) {
        printf("format_yaml: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    // start with YAML document marker
    strbuf_append_str(sb, "---\n");
    
    // add lowercase comment as requested
    strbuf_append_str(sb, "# yaml formatted output\n");
    
    printf("format_yaml: root_item %p\n", (void*)root_item);
    fflush(stdout);
    
    // format the root item directly
    format_yaml_item(sb, root_item, 0);
    
    // add final newline for proper YAML format
    strbuf_append_char(sb, '\n');
    
    printf("format_yaml: completed direct traversal version\n");
    fflush(stdout);
    
    return strbuf_to_string(sb);
}
