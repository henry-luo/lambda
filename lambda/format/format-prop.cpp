// Properties Formatter - Java-style properties file format
// Properties files are flat key-value pairs without sections
#include "format.h"
#include "../../lib/stringbuf.h"
#include <string.h>

// forward declarations
static void format_item(StringBuf* sb, Item item, const char* key_name);
static void format_properties_string(StringBuf* sb, String* str);
static bool is_simple_value(TypeId type_id);

// format a string value for Properties - handle escaping for properties format
static void format_properties_string(StringBuf* sb, String* str) {
    if (!str || !str->chars) {
        // empty string in properties
        return;
    }
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '\n':
            stringbuf_append_str(sb, "\\n");
            break;
        case '\r':
            stringbuf_append_str(sb, "\\r");
            break;
        case '\t':
            stringbuf_append_str(sb, "\\t");
            break;
        case '\\':
            stringbuf_append_str(sb, "\\\\");
            break;
        case '=':
            stringbuf_append_str(sb, "\\=");
            break;
        case ':':
            stringbuf_append_str(sb, "\\:");
            break;
        case '#':
            stringbuf_append_str(sb, "\\#");
            break;
        case '!':
            stringbuf_append_str(sb, "\\!");
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// check if a type can be represented as a simple properties value
static bool is_simple_value(TypeId type_id) {
    return (type_id == LMD_TYPE_BOOL || 
            type_id == LMD_TYPE_INT || 
            type_id == LMD_TYPE_INT64 || 
            type_id == LMD_TYPE_FLOAT || 
            type_id == LMD_TYPE_STRING || 
            type_id == LMD_TYPE_SYMBOL ||
            type_id == LMD_TYPE_NULL);
}

// centralized function to format any Lambda Item for Properties format
static void format_item(StringBuf* sb, Item item, const char* key_name) {
    TypeId type_id = get_type_id(item);
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            // empty value in properties
            break;
        case LMD_TYPE_BOOL: {
            bool val = item.bool_val;
            stringbuf_append_str(sb, val ? "true" : "false");
            break;
        }
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT:
            format_number(sb, item);
            break;
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL: {
            String* str = (String*)item.pointer;
            if (str) {
                format_properties_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST: {
            // arrays in properties are typically comma-separated values
            Array* arr = (Array*)item.pointer;
            if (arr && arr->length > 0) {
                for (long i = 0; i < arr->length; i++) {
                    if (i > 0) {
                        stringbuf_append_str(sb, ",");
                    }
                    
                    Item arr_item = arr->items[i];
                    TypeId arr_type = get_type_id(arr_item);
                    
                    // only format simple values in arrays for properties
                    if (is_simple_value(arr_type)) {
                        format_item(sb, arr_item, NULL);
                    } else {
                        stringbuf_append_str(sb, "[complex]");
                    }
                }
            }
            break;
        }
        case LMD_TYPE_MAP: {
            // nested maps cannot be represented as simple values in properties
            stringbuf_append_str(sb, "[map]");
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* element = item.element;
            if (!element || !element->type) {
                stringbuf_append_str(sb, "[element]");
                break;
            }
            
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            
            // represent element as its tag name
            if (elmt_type) {
                stringbuf_append_format(sb, "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
            } else {
                stringbuf_append_str(sb, "[element]");
            }
            break;
        }
        default:
            // fallback for unknown types
            stringbuf_append_format(sb, "[type_%d]", (int)type_id);
            break;
    }
}

// format nested maps by flattening keys with dot notation
static void format_map_flattened(StringBuf* sb, Map* map, const char* prefix) {
    if (!map || !map->type || !map->data) {
        return;
    }
    
    TypeMap* map_type = (TypeMap*)map->type;
    
    // iterate through map fields using safe linked list traversal
    ShapeEntry* field = map_type->shape;
    int field_count = 0;
    
    while (field && field_count < map_type->length) {
        if (field->name && field->name->str) {
            void* field_data = ((char*)map->data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            // build full key name with prefix - use safer string handling
            if (prefix && strlen(prefix) > 0) {
                stringbuf_append_str(sb, prefix);
                stringbuf_append_char(sb, '.');
                stringbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
            } else {
                stringbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
            }
            
            if (field_type == LMD_TYPE_MAP) {
                // for nested maps, we skip them in properties format since it's flat
                // but we could extend this later to support flattening
                stringbuf_append_str(sb, "=[nested_map]\n");
            } else {
                // format as key=value pair
                Item field_item = create_item_from_field_data(field_data, field_type);
                stringbuf_append_char(sb, '=');
                format_item(sb, field_item, NULL);
                stringbuf_append_char(sb, '\n');
            }
        }
        
        field = field->next;
        field_count++;
    }
}

// main Properties formatter function
String* format_properties(VariableMemPool* pool, Item root_item) {
    printf("format_properties: ENTRY - flattened key-value format\n");
    fflush(stdout);
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        printf("format_properties: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    // temporary implementation - just output a note that this is work in progress
    stringbuf_append_str(sb, "# properties formatting is work in progress\n");
    stringbuf_append_str(sb, "# use 'format(data, \"json\")' or other formats for now\n");
    
    printf("format_properties: completed successfully\n");
    fflush(stdout);
    
    return stringbuf_to_string(sb);
}
