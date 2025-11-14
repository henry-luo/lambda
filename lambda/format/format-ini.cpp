// INI Formatter - Direct traversal implementation
// Safe implementation with centralized type handling
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <string.h>

// Forward declarations (MarkReader-based only)
static void format_item_reader(StringBuf* sb, const ItemReader& item, const char* key_name);
static void format_map_as_section_reader(StringBuf* sb, const MapReader& map, const char* section_name);
static void format_ini_string(StringBuf* sb, String* str);

// format a string value for INI - handle escaping for INI format
static void format_ini_string(StringBuf* sb, String* str) {
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
        case '"':
            stringbuf_append_str(sb, "\\\"");
            break;
        case ';':
            stringbuf_append_str(sb, "\\;");
            break;
        case '#':
            stringbuf_append_str(sb, "\\#");
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// MarkReader-based version: format map as INI section
static void format_map_as_section_reader(StringBuf* sb, const MapReader& map, const char* section_name) {
    // add section header if we have a name
    if (section_name && strlen(section_name) > 0) {
        stringbuf_append_format(sb, "[%s]\n", section_name);
    }
    
    // iterate through map entries
    auto entries = map.entries();
    const char* key;
    ItemReader value;
    
    while (entries.next(&key, &value)) {
        // format key=value pair
        stringbuf_append_format(sb, "%s=", key);
        format_item_reader(sb, value, key);
        stringbuf_append_char(sb, '\n');
    }
}

// MarkReader-based version: format any Lambda Item for INI format
static void format_item_reader(StringBuf* sb, const ItemReader& item, const char* key_name) {
    if (item.isNull()) {
        // empty value in INI
        return;
    }
    
    if (item.isBool()) {
        bool val = item.asBool();
        stringbuf_append_str(sb, val ? "true" : "false");
    }
    else if (item.isInt() || item.isFloat()) {
        format_number(sb, item.item());
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_ini_string(sb, str);
        }
    }
    else if (item.isArray()) {
        // arrays in INI are typically comma-separated values
        ArrayReader arr = item.asArray();
        auto items_iter = arr.items();
        ItemReader arr_item;
        bool first = true;
        
        while (items_iter.next(&arr_item)) {
            if (!first) {
                stringbuf_append_str(sb, ",");
            }
            first = false;
            
            // only format simple values in arrays for INI
            if (arr_item.isNull() || arr_item.isBool() || arr_item.isInt() || 
                arr_item.isFloat() || arr_item.isString()) {
                format_item_reader(sb, arr_item, NULL);
            } else {
                stringbuf_append_str(sb, "[complex]");
            }
        }
    }
    else if (item.isMap()) {
        // nested maps cannot be represented as simple values in INI
        stringbuf_append_str(sb, "[map]");
    }
    else if (item.isElement()) {
        ElementReader element = item.asElement();
        const char* tag_name = element.tagName();
        
        // represent element as its tag name
        if (tag_name) {
            stringbuf_append_str(sb, tag_name);
        } else {
            stringbuf_append_str(sb, "[element]");
        }
    }
    else {
        // fallback for unknown types
        stringbuf_append_str(sb, "[unknown]");
    }
}

// main INI formatter function
String* format_ini(Pool* pool, Item root_item) {
    printf("format_ini: ENTRY - MarkReader version\n");
    fflush(stdout);
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        printf("format_ini: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    // add lowercase comment as requested
    stringbuf_append_str(sb, "; ini formatted output\n");
    
    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    
    if (root.isMap()) {
        // root is a map - format as sections
        MapReader root_map = root.asMap();
        
        // check if we should treat this as a global section or named sections
        bool has_nested_maps = false;
        
        // analyze structure to determine formatting approach
        auto entries = root_map.entries();
        const char* key;
        ItemReader value;
        while (entries.next(&key, &value)) {
            if (value.isMap()) {
                has_nested_maps = true;
                break;
            }
        }
        
        if (has_nested_maps) {
            // format each top-level map field as a section
            auto entries2 = root_map.entries();
            bool first = true;
            bool has_global = false;
            
            while (entries2.next(&key, &value)) {
                if (value.isMap()) {
                    if (!first) {
                        stringbuf_append_char(sb, '\n');
                    }
                    MapReader section_map = value.asMap();
                    format_map_as_section_reader(sb, section_map, key);
                    first = false;
                } else {
                    // simple field at root level - add to global section
                    if (!has_global) {
                        if (!first) {
                            stringbuf_append_char(sb, '\n');
                        }
                        stringbuf_append_str(sb, "[global]\n");
                        has_global = true;
                        first = false;
                    }
                    stringbuf_append_format(sb, "%s=", key);
                    format_item_reader(sb, value, key);
                    stringbuf_append_char(sb, '\n');
                }
            }
        } else {
            // no nested maps - format as single section
            format_map_as_section_reader(sb, root_map, NULL);
        }
    } else {
        // root is not a map - treat as single value
        stringbuf_append_str(sb, "value=");
        format_item_reader(sb, root, "value");
        stringbuf_append_char(sb, '\n');
    }
    
    printf("format_ini: completed successfully\n");
    fflush(stdout);
    
    return stringbuf_to_string(sb);
}
