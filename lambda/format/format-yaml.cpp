// YAML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
// Updated to use MarkReader API and YamlContext (Nov 2025)
#include "format.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/datetime.h"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"
#include <string.h>

// forward declarations
static void format_item_reader(YamlContext& ctx, const ItemReader& item, int indent_level);
static void format_array_reader(YamlContext& ctx, const ArrayReader& arr, int indent_level);
static void format_map_reader(YamlContext& ctx, const MapReader& map_reader, int indent_level);
static void format_element_reader(YamlContext& ctx, const ElementReader& elem, int indent_level);
static void format_yaml_string(YamlContext& ctx, String* str);
static void add_yaml_indent(YamlContext& ctx, int indent_level);

// add indentation for nested structures
static void add_yaml_indent(YamlContext& ctx, int indent_level) {
    for (int i = 0; i < indent_level * 2; i++) {
        stringbuf_append_char(ctx.output(), ' ');
    }
}

// format a string value for YAML - handle quoting and escaping
static void format_yaml_string(YamlContext& ctx, String* str) {
    if (!str) {
        stringbuf_append_str(ctx.output(), "null");
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
        stringbuf_append_char(ctx.output(), '"');
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (needs_quotes) {
            switch (c) {
            case '"':
                stringbuf_append_str(ctx.output(), "\\\"");
                break;
            case '\\':
                stringbuf_append_str(ctx.output(), "\\\\");
                break;
            case '\n':
                stringbuf_append_str(ctx.output(), "\\n");
                break;
            case '\r':
                stringbuf_append_str(ctx.output(), "\\r");
                break;
            case '\t':
                stringbuf_append_str(ctx.output(), "\\t");
                break;
            default:
                stringbuf_append_char(ctx.output(), c);
                break;
            }
        } else {
            stringbuf_append_char(ctx.output(), c);
        }
    }
    
    if (needs_quotes) {
        stringbuf_append_char(ctx.output(), '"');
    }
}

// format array items for YAML using ArrayReader
static void format_array_reader(YamlContext& ctx, const ArrayReader& arr, int indent_level) {
    if (!arr.isValid() || arr.isEmpty()) {
        stringbuf_append_str(ctx.output(), "[]");
        return;
    }
    
    auto iter = arr.items();
    ItemReader item;
    int index = 0;
    
    while (iter.next(&item)) {
        if (index > 0 || indent_level > 0) {
            stringbuf_append_char(ctx.output(), '\n');
            add_yaml_indent(ctx, indent_level);
        }
        stringbuf_append_str(ctx.output(), "- ");
        
        // for complex types, add proper indentation
        if (item.isMap() || item.isElement() || item.isArray() || item.isList()) {
            format_item_reader(ctx, item, indent_level + 1);
        } else {
            format_item_reader(ctx, item, 0);
        }
        index++;
    }
}

// format map items using MapReader
static void format_map_reader(YamlContext& ctx, const MapReader& map_reader, int indent_level) {
    if (!map_reader.isValid()) {
        stringbuf_append_str(ctx.output(), "{}");
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
                stringbuf_append_char(ctx.output(), '\n');
            }
            first_item = false;
            
            if (indent_level > 0) {
                add_yaml_indent(ctx, indent_level);
            }
            
            stringbuf_append_format(ctx.output(), "%s: null", key);
            continue;
        }
        
        if (!first_item) {
            stringbuf_append_char(ctx.output(), '\n');
        }
        first_item = false;
        
        if (indent_level > 0) {
            add_yaml_indent(ctx, indent_level);
        }
        
        // add field name
        stringbuf_append_format(ctx.output(), "%s: ", key);
        
        // format field value
        if (value.isMap() || value.isElement() || value.isArray() || value.isList()) {
            // for complex types, add newline and proper indentation
            if (value.isMap() || value.isElement()) {
                stringbuf_append_char(ctx.output(), '\n');
            }
            format_item_reader(ctx, value, indent_level + 1);
        } else {
            format_item_reader(ctx, value, 0);
        }
    }
}

// format element using ElementReader
static void format_element_reader(YamlContext& ctx, const ElementReader& elem, int indent_level) {
    // for yaml, represent element as an object with special "$" key for tag name
    if (indent_level > 0) {
        stringbuf_append_char(ctx.output(), '\n');
        add_yaml_indent(ctx, indent_level);
    }
    stringbuf_append_format(ctx.output(), "$: \"%s\"", elem.tagName());
    
    // add attributes if any
    if (elem.attrCount() > 0) {
        stringbuf_append_char(ctx.output(), '\n');
        // create MapReader from element attributes
        // note: for now we'll iterate manually, but ElementReader could provide attribute iteration
        const TypeMap* map_type = (const TypeMap*)elem.element()->type;
        const ShapeEntry* field = map_type->shape;
        bool first_attr = true;
        
        while (field) {
            const char* key = field->name->str;
            ItemReader attr_value = elem.get_attr(key);
            
            if (!first_attr) {
                stringbuf_append_char(ctx.output(), '\n');
            }
            first_attr = false;
            
            if (indent_level > 0) {
                add_yaml_indent(ctx, indent_level);
            }
            
            stringbuf_append_format(ctx.output(), "%s: ", key);
            format_item_reader(ctx, attr_value, 0);
            
            field = field->next;
        }
    }
    
    // add children if any
    if (elem.childCount() > 0) {
        if (indent_level > 0) {
            stringbuf_append_char(ctx.output(), '\n');
            add_yaml_indent(ctx, indent_level);
        } else {
            stringbuf_append_char(ctx.output(), '\n');
        }
        stringbuf_append_str(ctx.output(), "_:");
        
        auto child_iter = elem.children();
        ItemReader child;
        int child_index = 0;
        
        while (child_iter.next(&child)) {
            if (child_index > 0 || indent_level >= 0) {
                stringbuf_append_char(ctx.output(), '\n');
                add_yaml_indent(ctx, indent_level + 1);
            }
            stringbuf_append_str(ctx.output(), "- ");
            
            if (child.isMap() || child.isElement() || child.isArray() || child.isList()) {
                format_item_reader(ctx, child, indent_level + 2);
            } else {
                format_item_reader(ctx, child, 0);
            }
            child_index++;
        }
    }
}

// centralized function to format any Lambda Item with proper type handling using MarkReader
static void format_item_reader(YamlContext& ctx, const ItemReader& item, int indent_level) {
    // prevent infinite recursion
    if (indent_level > 10) {
        stringbuf_append_str(ctx.output(), "\"[max_depth]\"");
        return;
    }
    
    if (item.isNull()) {
        stringbuf_append_str(ctx.output(), "null");
    } else if (item.isBool()) {
        stringbuf_append_str(ctx.output(), item.asBool() ? "true" : "false");
    } else if (item.isInt() || item.isFloat()) {
        // use centralized number formatting
        format_number(ctx.output(), item.item());
    } else if (item.getType() == LMD_TYPE_DTIME) {
        // format datetime as ISO 8601 string for YAML output
        DateTime* dt = (DateTime*)item.item().datetime_ptr;
        if (dt) {
            stringbuf_append_char(ctx.output(), '"');
            StrBuf* temp = strbuf_new();
            datetime_format_iso8601(temp, dt);
            stringbuf_append_str(ctx.output(), temp->str);
            strbuf_free(temp);
            stringbuf_append_char(ctx.output(), '"');
        } else {
            stringbuf_append_str(ctx.output(), "null");
        }
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_yaml_string(ctx, str);
        } else {
            stringbuf_append_str(ctx.output(), "null");
        }
    } else if (item.isArray() || item.isList()) {
        ArrayReader arr = item.asArray();
        format_array_reader(ctx, arr, indent_level);
    } else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_reader(ctx, mp, indent_level);
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(ctx, elem, indent_level);
    } else {
        // fallback for unknown types
        stringbuf_append_format(ctx.output(), "\"[type_%d]\"", (int)item.getType());
    }
}

// yaml formatter that produces proper YAML output
String* format_yaml(Pool* pool, Item root_item) {
    log_debug("format_yaml: ENTRY - MarkReader version");
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) {
        log_error("format_yaml: failed to create string buffer");
        return NULL;
    }
    
    // Create YAML context
    Pool* ctx_pool = pool_create();
    YamlContext ctx(ctx_pool, sb);
    
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
                    stringbuf_append_str(ctx.output(), "\n---\n");
                } else {
                    stringbuf_append_str(ctx.output(), "---\n");
                }
                
                // add lowercase comment as requested
                stringbuf_append_str(ctx.output(), "# yaml formatted output\n");
                
                // format each document
                format_item_reader(ctx, doc_item, 0);
                stringbuf_append_char(ctx.output(), '\n');
                doc_index++;
            }
        } else {
            // Single document array
            stringbuf_append_str(ctx.output(), "---\n");
            stringbuf_append_str(ctx.output(), "# yaml formatted output\n");
            format_item_reader(ctx, reader, 0);
            stringbuf_append_char(ctx.output(), '\n');
        }
    } else {
        // Single document
        stringbuf_append_str(ctx.output(), "---\n");
        stringbuf_append_str(ctx.output(), "# yaml formatted output\n");
        format_item_reader(ctx, reader, 0);
        stringbuf_append_char(ctx.output(), '\n');
    }
    
    pool_destroy(ctx_pool);
    return stringbuf_to_string(sb);
}
