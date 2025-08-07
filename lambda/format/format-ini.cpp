// INI Formatter - Direct traversal implementation
// Safe implementation with centralized type handling
#include "format.h"
#include <string.h>

// forward declarations
static void format_item(StrBuf* sb, Item item, const char* key_name);
static void format_map_as_section(StrBuf* sb, Map* map, const char* section_name);
static void format_ini_string(StrBuf* sb, String* str);
static bool is_simple_value(TypeId type_id);

// format a string value for INI - handle escaping for INI format
static void format_ini_string(StrBuf* sb, String* str) {
    if (!str || !str->chars) {
        // empty string in INI
        return;
    }
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '\n':
            strbuf_append_str(sb, "\\n");
            break;
        case '\r':
            strbuf_append_str(sb, "\\r");
            break;
        case '\t':
            strbuf_append_str(sb, "\\t");
            break;
        case '\\':
            strbuf_append_str(sb, "\\\\");
            break;
        case '"':
            strbuf_append_str(sb, "\\\"");
            break;
        case ';':
            strbuf_append_str(sb, "\\;");
            break;
        case '#':
            strbuf_append_str(sb, "\\#");
            break;
        default:
            strbuf_append_char(sb, c);
            break;
        }
    }
}

// check if a type can be represented as a simple INI value
static bool is_simple_value(TypeId type_id) {
    return (type_id == LMD_TYPE_BOOL || 
            type_id == LMD_TYPE_INT || 
            type_id == LMD_TYPE_INT64 || 
            type_id == LMD_TYPE_FLOAT || 
            type_id == LMD_TYPE_STRING || 
            type_id == LMD_TYPE_SYMBOL ||
            type_id == LMD_TYPE_NULL);
}

// format map as INI section
static void format_map_as_section(StrBuf* sb, Map* map, const char* section_name) {
    if (!map || !map->type || !map->data) {
        return;
    }
    
    TypeMap* map_type = (TypeMap*)map->type;
    
    // add section header if we have a name
    if (section_name && strlen(section_name) > 0) {
        strbuf_append_format(sb, "[%s]\n", section_name);
    }
    
    // iterate through map fields using safe linked list traversal
    ShapeEntry* field = map_type->shape;
    int field_count = 0;
    
    while (field && field_count < map_type->length) {
        if (!field->name) {
            // nested map - handle as subsection
            void* data = ((char*)map->data) + field->byte_offset;
            Map* nest_map = *(Map**)data;
            if (nest_map && nest_map->type) {
                // add blank line before subsection
                strbuf_append_char(sb, '\n');
                format_map_as_section(sb, nest_map, "nested");
            }
        } else {
            // named field - create proper Lambda Item
            void* field_data = ((char*)map->data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            Item field_item = create_item_from_field_data(field_data, field_type);
            
            // format key=value pair
            strbuf_append_format(sb, "%.*s=", (int)field->name->length, field->name->str);
            format_item(sb, field_item, field->name->str);
            strbuf_append_char(sb, '\n');
        }
        
        field = field->next;
        field_count++;
    }
}

// centralized function to format any Lambda Item for INI format
static void format_item(StrBuf* sb, Item item, const char* key_name) {
    TypeId type_id = get_type_id(item);
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            // empty value in INI
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
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL: {
            String* str = (String*)item.pointer;
            if (str) {
                format_ini_string(sb, str);
            }
            break;
        }
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST: {
            // arrays in INI are typically comma-separated values
            Array* arr = (Array*)item.pointer;
            if (arr && arr->length > 0) {
                for (long i = 0; i < arr->length; i++) {
                    if (i > 0) {
                        strbuf_append_str(sb, ",");
                    }
                    
                    Item arr_item = arr->items[i];
                    TypeId arr_type = get_type_id(arr_item);
                    
                    // only format simple values in arrays for INI
                    if (is_simple_value(arr_type)) {
                        format_item(sb, arr_item, NULL);
                    } else {
                        strbuf_append_str(sb, "[complex]");
                    }
                }
            }
            break;
        }
        case LMD_TYPE_MAP: {
            // nested maps cannot be represented as simple values in INI
            strbuf_append_str(sb, "[map]");
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* element = (Element*)item.pointer;
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            
            // represent element as its tag name
            if (elmt_type) {
                strbuf_append_format(sb, "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
            } else {
                strbuf_append_str(sb, "[element]");
            }
            break;
        }
        default:
            // fallback for unknown types
            strbuf_append_format(sb, "[type_%d]", (int)type_id);
            break;
    }
}

// main INI formatter function
String* format_ini(VariableMemPool* pool, Item root_item) {
    printf("format_ini: ENTRY - direct traversal version\n");
    fflush(stdout);
    
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) {
        printf("format_ini: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    // add lowercase comment as requested
    strbuf_append_str(sb, "; ini formatted output\n");
    
    TypeId root_type = get_type_id(root_item);
    
    if (root_type == LMD_TYPE_MAP) {
        // root is a map - format as sections
        Map* root_map = (Map*)root_item.pointer;
        if (root_map && root_map->type && root_map->data) {
            TypeMap* map_type = (TypeMap*)root_map->type;
            
            // check if we should treat this as a global section or named sections
            ShapeEntry* field = map_type->shape;
            int field_count = 0;
            bool has_nested_maps = false;
            
            // analyze structure to determine formatting approach
            while (field && field_count < map_type->length) {
                if (field->name && field->type && field->type->type_id == LMD_TYPE_MAP) {
                    has_nested_maps = true;
                    break;
                }
                field = field->next;
                field_count++;
            }
            
            if (has_nested_maps) {
                // format each top-level map field as a section
                field = map_type->shape;
                field_count = 0;
                
                while (field && field_count < map_type->length) {
                    if (field->name) {
                        void* field_data = ((char*)root_map->data) + field->byte_offset;
                        TypeId field_type = field->type->type_id;
                        
                        if (field_type == LMD_TYPE_MAP) {
                            Map* section_map = *(Map**)field_data;
                            if (section_map) {
                                if (field_count > 0) {
                                    strbuf_append_char(sb, '\n');
                                }
                                format_map_as_section(sb, section_map, field->name->str);
                            }
                        } else {
                            // simple field at root level - add to global section
                            if (field_count == 0) {
                                strbuf_append_str(sb, "[global]\n");
                            }
                            Item field_item = create_item_from_field_data(field_data, field_type);
                            strbuf_append_format(sb, "%.*s=", (int)field->name->length, field->name->str);
                            format_item(sb, field_item, field->name->str);
                            strbuf_append_char(sb, '\n');
                        }
                    }
                    field = field->next;
                    field_count++;
                }
            } else {
                // no nested maps - format as single section
                format_map_as_section(sb, root_map, NULL);
            }
        } else {
            strbuf_append_str(sb, "; empty configuration\n");
        }
    } else {
        // root is not a map - treat as single value
        strbuf_append_str(sb, "value=");
        format_item(sb, root_item, "value");
        strbuf_append_char(sb, '\n');
    }
    
    printf("format_ini: completed successfully\n");
    fflush(stdout);
    
    return strbuf_to_string(sb);
}
