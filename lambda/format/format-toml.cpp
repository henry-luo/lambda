// TOML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
#include "format.h"
#include "../../lib/stringbuf.h"
#include <string.h>

// forward declarations
static void format_item(StringBuf* sb, Item item, const char* parent_context, int depth);
static bool should_format_as_table_section(Map* map);
static void format_table_section(StringBuf* sb, Map* map, const char* section_name, const char* parent_context, int depth);
static void format_inline_table(StringBuf* sb, Map* map, int depth);
static void format_array_items(StringBuf* sb, Array* arr, int depth);

// determine if a map should be formatted as a table section [name] vs inline table {key=val}
static bool should_format_as_table_section(Map* map) {
    if (!map || !map->type || !map->data) {
        return false;
    }
    
    TypeMap* type_map = (TypeMap*)map->type;
    if (type_map->length >= 3) {
        return true; // many fields
    }
    
    // check for complex content (arrays or nested maps) even with fewer fields
    ShapeEntry* shape = type_map->shape;
    int count = 0;
    while (shape && count < type_map->length) {
        if (shape->type && 
            (shape->type->type_id == LMD_TYPE_ARRAY || 
             shape->type->type_id == LMD_TYPE_MAP)) {
            return true;
        }
        shape = shape->next;
        count++;
    }
    
    return false;
}

// central function to format any Lambda Item with proper type handling
static void format_item(StringBuf* sb, Item item, const char* parent_context, int depth) {
    // prevent infinite recursion
    if (depth > 10) {
        stringbuf_append_str(sb, "\"[max_depth]\"");
        return;
    }
    
    TypeId type_id = (TypeId)(get_type_id(item));
    
    switch (type_id) {
        case LMD_TYPE_BOOL: {
            bool value = item.bool_val;
            stringbuf_append_str(sb, value ? "true" : "false");
            break;
        }
        case LMD_TYPE_INT:  case LMD_TYPE_INT64:  case LMD_TYPE_FLOAT: {
            format_number(sb, item);
            break;
        }
        case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
            String* str = (String*)item.pointer;
            stringbuf_append_str(sb, "\"");
            if (str && str->len > 0) {
                stringbuf_append_str(sb, str->chars);
            }
            stringbuf_append_str(sb, "\"");
            break;
        }
        case LMD_TYPE_ARRAY:  case LMD_TYPE_LIST:{
            Array* arr = (Array*)item.pointer;
            if (arr && arr->length > 0) {
                stringbuf_append_str(sb, "[");
                format_array_items(sb, arr, depth + 1);
                stringbuf_append_str(sb, "]");
            } else {
                stringbuf_append_str(sb, "[]");
            }
            break;
        }
        case LMD_TYPE_MAP: {
            Map* map = (Map*)item.pointer;
            if (map && map->type && map->data) {
                format_inline_table(sb, map, depth + 1);
            } else {
                stringbuf_append_str(sb, "{}");
            }
            break;
        }
        case LMD_TYPE_NULL: {
            stringbuf_append_str(sb, "\"\"");
            break;
        }
        default: {
            // fallback to type placeholder
            stringbuf_append_format(sb, "\"[type_%d]\"", (int)type_id);
            break;
        }
    }
}

// format array items with proper item processing
static void format_array_items(StringBuf* sb, Array* arr, int depth) {
    if (!arr || arr->length == 0) {
        return;
    }
    
    for (int i = 0; i < (int)arr->length; i++) {
        if (i > 0) {
            stringbuf_append_str(sb, ", ");
        }
        format_item(sb, arr->items[i], NULL, depth);
    }
}

// format a map as inline table {key=val, ...}
static void format_inline_table(StringBuf* sb, Map* map, int depth) {
    if (!map || !map->type || !map->data) {
        stringbuf_append_str(sb, "{}");
        return;
    }
    
    TypeMap* type_map = (TypeMap*)map->type;
    if (type_map->length == 0 || !type_map->shape) {
        stringbuf_append_str(sb, "{}");
        return;
    }
    
    stringbuf_append_str(sb, "{ ");
    
    ShapeEntry* shape = type_map->shape;
    int field_count = 0;
    bool first_field = true;
    
    while (shape && field_count < type_map->length) {
        if (shape->name && shape->name->str && shape->name->length > 0) {
            if (!first_field) {
                stringbuf_append_str(sb, ", ");
            }
            first_field = false;
            
            // field name
            stringbuf_append_format(sb, "%.*s = ", (int)shape->name->length, shape->name->str);
            
            // field value using centralized formatting
            void* field_data = ((char*)map->data) + shape->byte_offset;
            Item field_item = create_item_from_field_data(field_data, shape->type->type_id);
            format_item(sb, field_item, NULL, depth);
        }
        
        shape = shape->next;
        field_count++;
    }
    
    stringbuf_append_str(sb, " }");
}

// format a map as a table section [section_name]
static void format_table_section(StringBuf* sb, Map* map, const char* section_name, const char* parent_context, int depth) {
    if (!map || !map->type || !map->data || !section_name) {
        return;
    }
    
    // prevent infinite recursion
    if (depth > 10) {
        stringbuf_append_str(sb, "# [max_depth_section]\n");
        return;
    }
    
    TypeMap* type_map = (TypeMap*)map->type;
    if (type_map->length == 0 || !type_map->shape) {
        return;
    }
    
    // section header
    stringbuf_append_str(sb, "\n[");
    if (parent_context && strlen(parent_context) > 0) {
        stringbuf_append_str(sb, parent_context);
        stringbuf_append_str(sb, ".");
    }
    stringbuf_append_str(sb, section_name);
    stringbuf_append_str(sb, "]\n");
    
    // build full section context for nested sections
    char full_section_name[256];
    if (parent_context && strlen(parent_context) > 0) {
        snprintf(full_section_name, sizeof(full_section_name), "%s.%s", parent_context, section_name);
    } else {
        snprintf(full_section_name, sizeof(full_section_name), "%s", section_name);
    }
    
    // process fields
    ShapeEntry* shape = type_map->shape;
    int field_count = 0;
    
    while (shape && field_count < type_map->length) {
        if (shape->name && shape->name->str && shape->name->length > 0) {
            void* field_data = ((char*)map->data) + shape->byte_offset;
            TypeId field_type = shape->type->type_id;
            
            // check if this field should be its own table section
            if (field_type == LMD_TYPE_MAP) {
                Map* nested_map = *(Map**)field_data;
                if (nested_map && should_format_as_table_section(nested_map)) {
                    // format as nested section
                    char nested_section_name[128];
                    snprintf(nested_section_name, sizeof(nested_section_name), "%.*s", 
                             (int)shape->name->length, shape->name->str);
                    format_table_section(sb, nested_map, nested_section_name, full_section_name, depth + 1);
                    
                    shape = shape->next;
                    field_count++;
                    continue;
                }
            }
            
            // regular field assignment
            stringbuf_append_format(sb, "%.*s = ", (int)shape->name->length, shape->name->str);
            
            Item field_item = create_item_from_field_data(field_data, field_type);
            format_item(sb, field_item, full_section_name, depth);
            
            stringbuf_append_str(sb, "\n");
        }
        
        shape = shape->next;
        field_count++;
    }
}

// main function to format map attributes - now simplified with delegation
static void format_toml_attrs_from_shape(StringBuf* sb, TypeMap* type_map, void* data, const char* parent_name) {
    if (!type_map || !type_map->shape || !data) {
        return;
    }
    
    ShapeEntry* shape = type_map->shape;
    int field_count = 0;
    
    while (shape && field_count < type_map->length) {
        if (shape->name && shape->name->str && shape->name->length > 0) {
            void* field_data = ((char*)data) + shape->byte_offset;
            TypeId field_type = shape->type->type_id;
            
            // check if this field should be formatted as a table section
            if (field_type == LMD_TYPE_MAP) {
                Map* map = *(Map**)field_data;
                if (map && should_format_as_table_section(map)) {
                    char section_name[128];
                    snprintf(section_name, sizeof(section_name), "%.*s", 
                             (int)shape->name->length, shape->name->str);
                    format_table_section(sb, map, section_name, parent_name, 1);
                    
                    shape = shape->next;
                    field_count++;
                    continue;
                }
            }
            
            // regular field processing
            stringbuf_append_format(sb, "%.*s = ", (int)shape->name->length, shape->name->str);
            
            Item field_item = create_item_from_field_data(field_data, field_type);
            format_item(sb, field_item, parent_name, 1);
            
            stringbuf_append_str(sb, "\n");
        }
        
        shape = shape->next;
        field_count++;
    }
}

// main formatter entry point
String* format_toml(VariableMemPool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        return NULL;
    }
    
    // add comment header
    stringbuf_append_str(sb, "# TOML formatted output\n");
    stringbuf_append_str(sb, "# Generated by Lambda TOML formatter\n");
    stringbuf_append_str(sb, "\n");
    
    // handle root item - try as map first
    Map* map = (Map*)root_item.pointer;
    
    if (map && map->data && map->type) {
        TypeMap* type_map = (TypeMap*)map->type;
        if (type_map->length > 0) {
            stringbuf_append_str(sb, "# Map with ");
            stringbuf_append_format(sb, "%ld", (int64_t)type_map->length);
            stringbuf_append_str(sb, " fields\n\n");
            
            // format using centralized approach
            format_toml_attrs_from_shape(sb, type_map, map->data, NULL);
        } else {
            stringbuf_append_str(sb, "# Empty map\n");
        }
    } else {
        // try using central format_item function for non-map roots
        TypeId type = get_type_id(root_item);
        if (type != 0) {
            stringbuf_append_format(sb, "# Root type: %d\n", (int)type);
            stringbuf_append_str(sb, "root_value = ");
            format_item(sb, root_item, NULL, 0);
            stringbuf_append_str(sb, "\n");
        } else {
            stringbuf_append_str(sb, "# Unable to determine root type\n");
            stringbuf_append_format(sb, "# Raw value: 0x%llx\n", (unsigned long long)root_item.item);
            stringbuf_append_str(sb, "status = \"unable_to_format\"\n");
        }
    }
    
    String* result = stringbuf_to_string(sb);
    return result;
}
