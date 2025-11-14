// TOML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>

// forward declarations
static void format_item_reader(StringBuf* sb, ItemReader item, const char* parent_context, int depth);
static bool should_format_as_table_section_reader(MapReader map);
static void format_table_section_reader(StringBuf* sb, MapReader map, const char* section_name, const char* parent_context, int depth);
static void format_inline_table_reader(StringBuf* sb, MapReader map, int depth);
static void format_array_items_reader(StringBuf* sb, ArrayReader arr, int depth);

// determine if a map should be formatted as a table section [name] vs inline table {key=val}
static bool should_format_as_table_section_reader(MapReader map) {
    if (!map.isValid()) {
        return false;
    }
    
    int64_t length = map.size();
    if (length >= 3) {
        return true; // many fields
    }
    
    // check for complex content (arrays or nested maps) even with fewer fields
    auto values = map.values();
    ItemReader value;
    while (values.next(&value)) {
        if (value.isArray() || value.isList() || value.isMap()) {
            return true;
        }
    }
    
    return false;
}

// central function to format any Lambda Item with proper type handling
static void format_item_reader(StringBuf* sb, ItemReader item, const char* parent_context, int depth) {
    // prevent infinite recursion
    if (depth > 10) {
        stringbuf_append_str(sb, "\"[max_depth]\"");
        return;
    }
    
    if (item.isNull()) {
        stringbuf_append_str(sb, "\"\"");
    } else if (item.isBool()) {
        stringbuf_append_str(sb, item.asBool() ? "true" : "false");
    } else if (item.isInt() || item.isFloat()) {
        format_number(sb, item.item());
    } else if (item.isString()) {
        String* str = item.asString();
        stringbuf_append_str(sb, "\"");
        if (str && str->len > 0) {
            stringbuf_append_str(sb, str->chars);
        }
        stringbuf_append_str(sb, "\"");
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        if (arr.length() > 0) {
            stringbuf_append_str(sb, "[");
            format_array_items_reader(sb, arr, depth + 1);
            stringbuf_append_str(sb, "]");
        } else {
            stringbuf_append_str(sb, "[]");
        }
    } else if (item.isMap()) {
        MapReader map = item.asMap();
        if (map.isValid()) {
            format_inline_table_reader(sb, map, depth + 1);
        } else {
            stringbuf_append_str(sb, "{}");
        }
    } else {
        // fallback to type placeholder
        stringbuf_append_format(sb, "\"[type_%d]\"", (int)item.getType());
    }
}

// format array items with proper item processing
static void format_array_items_reader(StringBuf* sb, ArrayReader arr, int depth) {
    if (!arr.isValid() || arr.length() == 0) {
        return;
    }
    
    auto items = arr.items();
    ItemReader item;
    bool first = true;
    
    while (items.next(&item)) {
        if (!first) {
            stringbuf_append_str(sb, ", ");
        }
        first = false;
        format_item_reader(sb, item, NULL, depth);
    }
}

// format a map as inline table {key=val, ...}
static void format_inline_table_reader(StringBuf* sb, MapReader map, int depth) {
    if (!map.isValid()) {
        stringbuf_append_str(sb, "{}");
        return;
    }
    
    int64_t length = map.size();
    if (length == 0) {
        stringbuf_append_str(sb, "{}");
        return;
    }
    
    stringbuf_append_str(sb, "{ ");
    
    auto entries = map.entries();
    const char* key;
    ItemReader value;
    bool first_field = true;
    
    while (entries.next(&key, &value)) {
        if (!first_field) {
            stringbuf_append_str(sb, ", ");
        }
        first_field = false;
        
        // field name
        stringbuf_append_str(sb, key);
        stringbuf_append_str(sb, " = ");
        
        // field value using centralized formatting
        format_item_reader(sb, value, NULL, depth);
    }
    
    stringbuf_append_str(sb, " }");
}

// format a map as a table section [section_name]
static void format_table_section_reader(StringBuf* sb, MapReader map, const char* section_name, const char* parent_context, int depth) {
    if (!map.isValid() || !section_name) {
        return;
    }
    
    // prevent infinite recursion
    if (depth > 10) {
        stringbuf_append_str(sb, "# [max_depth_section]\n");
        return;
    }
    
    int64_t length = map.size();
    if (length == 0) {
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
    auto entries = map.entries();
    const char* key;
    ItemReader value;
    
    while (entries.next(&key, &value)) {
        // check if this field should be its own table section
        if (value.isMap()) {
            MapReader nested_map = value.asMap();
            if (should_format_as_table_section_reader(nested_map)) {
                // format as nested section
                format_table_section_reader(sb, nested_map, key, full_section_name, depth + 1);
                continue;
            }
        }
        
        // regular field assignment
        stringbuf_append_str(sb, key);
        stringbuf_append_str(sb, " = ");
        
        format_item_reader(sb, value, full_section_name, depth);
        
        stringbuf_append_str(sb, "\n");
    }
}

// main function to format map attributes - now simplified with delegation
static void format_toml_attrs_from_map_reader(StringBuf* sb, MapReader map, const char* parent_name) {
    if (!map.isValid()) {
        return;
    }
    
    auto entries = map.entries();
    const char* key;
    ItemReader value;
    
    while (entries.next(&key, &value)) {
        // check if this field should be formatted as a table section
        if (value.isMap()) {
            MapReader nested_map = value.asMap();
            if (should_format_as_table_section_reader(nested_map)) {
                format_table_section_reader(sb, nested_map, key, parent_name, 1);
                continue;
            }
        }
        
        // regular field processing
        stringbuf_append_str(sb, key);
        stringbuf_append_str(sb, " = ");
        
        format_item_reader(sb, value, parent_name, 1);
        
        stringbuf_append_str(sb, "\n");
    }
}

// main formatter entry point
String* format_toml(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        return NULL;
    }
    
    // add comment header
    stringbuf_append_str(sb, "# TOML formatted output\n");
    stringbuf_append_str(sb, "# Generated by Lambda TOML formatter\n");
    stringbuf_append_str(sb, "\n");
    
    // wrap root item in ItemReader
    ItemReader reader(root_item.to_const());
    
    if (reader.isMap()) {
        MapReader map = reader.asMap();
        int64_t length = map.size();
        
        if (length > 0) {
            stringbuf_append_str(sb, "# Map with ");
            stringbuf_append_format(sb, "%ld", (int64_t)length);
            stringbuf_append_str(sb, " fields\n\n");
            
            // format using centralized approach
            format_toml_attrs_from_map_reader(sb, map, NULL);
        } else {
            stringbuf_append_str(sb, "# Empty map\n");
        }
    } else {
        // try using central format_item_reader function for non-map roots
        if (!reader.isNull()) {
            stringbuf_append_format(sb, "# Root type: %d\n", (int)reader.getType());
            stringbuf_append_str(sb, "root_value = ");
            format_item_reader(sb, reader, NULL, 0);
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
