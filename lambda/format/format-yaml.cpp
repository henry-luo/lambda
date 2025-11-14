// YAML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
// Updated to use MarkReader API (Nov 2025)
#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>

// forward declarations
static void format_item_reader(StringBuf* sb, const ItemReader& item, int indent_level);
static void format_array_reader(StringBuf* sb, const ArrayReader& arr, int indent_level);
static void format_map_reader(StringBuf* sb, const MapReader& map_reader, int indent_level);
static void format_element_reader(StringBuf* sb, const ElementReader& elem, int indent_level);
static void format_yaml_string(StringBuf* sb, String* str);
static void add_yaml_indent(StringBuf* sb, int indent_level);

// add indentation for nested structures
static void add_yaml_indent(StringBuf* sb, int indent_level) {
    for (int i = 0; i < indent_level * 2; i++) {
        stringbuf_append_char(sb, ' ');
    }
}

// format a string value for YAML - handle quoting and escaping
static void format_yaml_string(StringBuf* sb, String* str) {
    if (!str) {
        stringbuf_append_str(sb, "null");
        return;
    }
    
    const char* s = str->chars;
    size_t len = str->len;
    bool needs_quotes = false;
    
    // check if string needs quotes (contains special chars, starts with special chars, etc.)
    if (len == 0 || strchr(s, ':') || strchr(s, '\n') || strchr(s, '"') || 
        strchr(s, '\'') || strchr(s, '#') || strchr(s, '-') || strchr(s, '[') || 
        strchr(s, ']') || strchr(s, '{') || strchr(s, '}') || strchr(s, '|') ||
        strchr(s, '>') || strchr(s, '&') || strchr(s, '*') || strchr(s, '!') ||
        (len > 0 && (isspace(s[0]) || isspace(s[len-1])))) {
        needs_quotes = true;
    }
    
    // check for yaml reserved words and special values
    if (!needs_quotes && (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || 
                         strcmp(s, "null") == 0 || strcmp(s, "yes") == 0 || 
                         strcmp(s, "no") == 0 || strcmp(s, "on") == 0 || 
                         strcmp(s, "off") == 0 || strcmp(s, "~") == 0 ||
                         strcmp(s, ".inf") == 0 || strcmp(s, "-.inf") == 0 ||
                         strcmp(s, ".nan") == 0 || strcmp(s, ".Inf") == 0 ||
                         strcmp(s, "-.Inf") == 0 || strcmp(s, ".NaN") == 0)) {
        needs_quotes = true;
    }
    
    // check if it looks like a number
    if (!needs_quotes) {
        char* end;
        strtol(s, &end, 10);
        if (*end == '\0' && len > 0) needs_quotes = true; // integer
        strtod(s, &end);
        if (*end == '\0' && len > 0) needs_quotes = true; // float
    }
    
    if (needs_quotes) {
        stringbuf_append_char(sb, '"');
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (needs_quotes) {
            switch (c) {
            case '"':
                stringbuf_append_str(sb, "\\\"");
                break;
            case '\\':
                stringbuf_append_str(sb, "\\\\");
                break;
            case '\n':
                stringbuf_append_str(sb, "\\n");
                break;
            case '\r':
                stringbuf_append_str(sb, "\\r");
                break;
            case '\t':
                stringbuf_append_str(sb, "\\t");
                break;
            default:
                stringbuf_append_char(sb, c);
                break;
            }
        } else {
            stringbuf_append_char(sb, c);
        }
    }
    
    if (needs_quotes) {
        stringbuf_append_char(sb, '"');
    }
}

// format array items for YAML using ArrayReader
static void format_array_reader(StringBuf* sb, const ArrayReader& arr, int indent_level) {
    if (!arr.isValid() || arr.isEmpty()) {
        stringbuf_append_str(sb, "[]");
        return;
    }
    
    auto iter = arr.items();
    ItemReader item;
    int index = 0;
    
    while (iter.next(&item)) {
        if (index > 0 || indent_level > 0) {
            stringbuf_append_char(sb, '\n');
            add_yaml_indent(sb, indent_level);
        }
        stringbuf_append_str(sb, "- ");
        
        // for complex types, add proper indentation
        if (item.isMap() || item.isElement() || item.isArray() || item.isList()) {
            format_item_reader(sb, item, indent_level + 1);
        } else {
            format_item_reader(sb, item, 0);
        }
        index++;
    }
}

// format map items using MapReader
static void format_map_reader(StringBuf* sb, const MapReader& map_reader, int indent_level) {
    if (!map_reader.isValid()) {
        stringbuf_append_str(sb, "{}");
        return;
    }
    
    bool first_item = true;
    auto iter = map_reader.entries();
    const char* key;
    ItemReader value;
    
    while (iter.next(&key, &value)) {
        // skip null/unset fields appropriately
        if (value.isNull()) {
            if (!first_item) {
                stringbuf_append_char(sb, '\n');
            }
            first_item = false;
            
            if (indent_level > 0) {
                add_yaml_indent(sb, indent_level);
            }
            
            stringbuf_append_format(sb, "%s: null", key);
            continue;
        }
        
        if (!first_item) {
            stringbuf_append_char(sb, '\n');
        }
        first_item = false;
        
        if (indent_level > 0) {
            add_yaml_indent(sb, indent_level);
        }
        
        // add field name
        stringbuf_append_format(sb, "%s: ", key);
        
        // format field value
        if (value.isMap() || value.isElement() || value.isArray() || value.isList()) {
            // for complex types, add newline and proper indentation
            if (value.isMap() || value.isElement()) {
                stringbuf_append_char(sb, '\n');
            }
            format_item_reader(sb, value, indent_level + 1);
        } else {
            format_item_reader(sb, value, 0);
        }
    }
}

// format element using ElementReader
static void format_element_reader(StringBuf* sb, const ElementReader& elem, int indent_level) {
    // for yaml, represent element as an object with special "$" key for tag name
    if (indent_level > 0) {
        stringbuf_append_char(sb, '\n');
        add_yaml_indent(sb, indent_level);
    }
    stringbuf_append_format(sb, "$: \"%s\"", elem.tagName());
    
    // add attributes if any
    if (elem.attrCount() > 0) {
        stringbuf_append_char(sb, '\n');
        // create MapReader from element attributes
        // note: for now we'll iterate manually, but ElementReader could provide attribute iteration
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;
        bool first_attr = true;
        
        while (field) {
            const char* key = field->name->str;
            ItemReader attr_value = elem.get_attr(key);
            
            if (!first_attr) {
                stringbuf_append_char(sb, '\n');
            }
            first_attr = false;
            
            if (indent_level > 0) {
                add_yaml_indent(sb, indent_level);
            }
            
            stringbuf_append_format(sb, "%s: ", key);
            format_item_reader(sb, attr_value, 0);
            
            field = field->next;
        }
    }
    
    // add children if any
    if (elem.childCount() > 0) {
        if (indent_level > 0) {
            stringbuf_append_char(sb, '\n');
            add_yaml_indent(sb, indent_level);
        } else {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, "_:");
        
        auto child_iter = elem.children();
        ItemReader child;
        int child_index = 0;
        
        while (child_iter.next(&child)) {
            if (child_index > 0 || indent_level >= 0) {
                stringbuf_append_char(sb, '\n');
                add_yaml_indent(sb, indent_level + 1);
            }
            stringbuf_append_str(sb, "- ");
            
            if (child.isMap() || child.isElement() || child.isArray() || child.isList()) {
                format_item_reader(sb, child, indent_level + 2);
            } else {
                format_item_reader(sb, child, 0);
            }
            child_index++;
        }
    }
}

// centralized function to format any Lambda Item with proper type handling using MarkReader
static void format_item_reader(StringBuf* sb, const ItemReader& item, int indent_level) {
    // prevent infinite recursion
    if (indent_level > 10) {
        stringbuf_append_str(sb, "\"[max_depth]\"");
        return;
    }
    
    if (item.isNull()) {
        stringbuf_append_str(sb, "null");
    } else if (item.isBool()) {
        stringbuf_append_str(sb, item.asBool() ? "true" : "false");
    } else if (item.isInt() || item.isFloat()) {
        // use centralized number formatting
        format_number(sb, item.item());
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_yaml_string(sb, str);
        } else {
            stringbuf_append_str(sb, "null");
        }
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        format_array_reader(sb, arr, indent_level);
    } else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader(sb, mp, indent_level);
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(sb, elem, indent_level);
    } else {
        // fallback for unknown types
        stringbuf_append_format(sb, "\"[type_%d]\"", (int)item.getType());
    }
}

// yaml formatter that produces proper YAML output
String* format_yaml(Pool* pool, Item root_item) {
    printf("format_yaml: ENTRY - MarkReader version\n");
    fflush(stdout);
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        printf("format_yaml: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    ItemReader reader(root_item.to_const());
    
    // Check if root is an array that might represent multiple YAML documents
    if (reader.isArray() || reader.isList()) {
        ArrayReader arr = reader.asArray();
        if (arr.length() > 1) {
            // Treat as multi-document YAML
            auto iter = arr.items();
            ItemReader doc_item;
            int doc_index = 0;
            
            while (iter.next(&doc_item)) {
                if (doc_index > 0) {
                    stringbuf_append_str(sb, "\n---\n");
                } else {
                    stringbuf_append_str(sb, "---\n");
                }
                
                // add lowercase comment as requested
                stringbuf_append_str(sb, "# yaml formatted output\n");
                
                // format each document
                format_item_reader(sb, doc_item, 0);
                stringbuf_append_char(sb, '\n');
                doc_index++;
            }
        } else {
            // Single document array
            stringbuf_append_str(sb, "---\n");
            stringbuf_append_str(sb, "# yaml formatted output\n");
            format_item_reader(sb, reader, 0);
            stringbuf_append_char(sb, '\n');
        }
    } else {
        // Single document
        stringbuf_append_str(sb, "---\n");
        stringbuf_append_str(sb, "# yaml formatted output\n");
        format_item_reader(sb, reader, 0);
        stringbuf_append_char(sb, '\n');
    }
    
    return stringbuf_to_string(sb);
}
