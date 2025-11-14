#include "format.h"
#include <string.h>
#include <ctype.h>
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"

// Global recursion depth counter to prevent infinite recursion
static thread_local int recursion_depth = 0;
#define MAX_RECURSION_DEPTH 50

// Forward declarations (MarkReader-based only)
static void format_item_text_reader(StringBuf* sb, const ItemReader& item);
static void format_element_text_reader(StringBuf* sb, const ElementReader& elem);
static void format_array_text_reader(StringBuf* sb, const ArrayReader& arr);
static void format_map_text_reader(StringBuf* sb, const MapReader& mp);
static void format_scalar_value_reader(StringBuf* sb, const ItemReader& item);

// Helper function to format scalar values as raw text (no quotes) - MarkReader version
static void format_scalar_value_reader(StringBuf* sb, const ItemReader& item) {
    if (item.isBool()) {
        bool val = item.asBool();
        stringbuf_append_str(sb, val ? "true" : "false");
    }
    else if (item.isInt()) {
        int val = item.asInt();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%d", val);
        stringbuf_append_str(sb, num_buf);
    }
    else if (item.isFloat()) {
        double val = item.asFloat();
        if (!isnan(val) && !isinf(val)) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%.15g", val);
            stringbuf_append_str(sb, num_buf);
        }
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars && str->len > 0) {
            // Output string content without quotes
            stringbuf_append_str_n(sb, str->chars, str->len);
        }
    }
    else {
        // For other types like DateTime, use raw Item access
        Item raw_item = item.item();
        TypeId type = get_type_id(raw_item);
        
        if (type == LMD_TYPE_DTIME) {
            DateTime* dt_ptr = (DateTime*)raw_item.pointer;
            if (dt_ptr) {
                char date_buf[32];
                // Extract date components using DateTime macros
                int year = DATETIME_GET_YEAR(dt_ptr);
                int month = DATETIME_GET_MONTH(dt_ptr);
                int day = dt_ptr->day;
                // Format as ISO 8601 string without quotes
                snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", 
                        year, month, day);
                stringbuf_append_str(sb, date_buf);
            }
        } else {
            // For non-scalar types, recursively process them
            format_item_text_reader(sb, item);
        }
    }
}

// MarkReader-based version: format an array by extracting scalar values
static void format_array_text_reader(StringBuf* sb, const ArrayReader& arr) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    auto items_iter = arr.items();
    ItemReader item;
    bool first = true;
    
    while (items_iter.next(&item)) {
        if (!first) {
            stringbuf_append_char(sb, ' ');
        }
        first = false;
        
        format_item_text_reader(sb, item);
    }
    
    recursion_depth--;
}

// MarkReader-based version: format a map by extracting scalar values
static void format_map_text_reader(StringBuf* sb, const MapReader& mp) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    auto entries = mp.entries();
    const char* key;
    ItemReader value;
    bool first = true;
    
    while (entries.next(&key, &value)) {
        if (!first) {
            stringbuf_append_char(sb, ' ');
        }
        first = false;
        
        format_item_text_reader(sb, value);
    }
    
    recursion_depth--;
}

// MarkReader-based version: format element by extracting scalar values
static void format_element_text_reader(StringBuf* sb, const ElementReader& elem) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    // for text extraction, we focus on element children/content
    // attributes are typically metadata, not content text
    
    // process element children
    auto children_iter = elem.children();
    ItemReader child;
    bool first = true;
    
    while (children_iter.next(&child)) {
        if (!first) {
            stringbuf_append_char(sb, ' ');
        }
        first = false;
        
        format_item_text_reader(sb, child);
    }
    
    recursion_depth--;
}

// MarkReader-based version: main recursive function to extract scalar values
static void format_item_text_reader(StringBuf* sb, const ItemReader& item) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        return; // prevent stack overflow
    }
    
    if (item.isNull()) {
        // skip null values
        return;
    }
    else if (item.isBool() || item.isInt() || item.isFloat() || item.isString()) {
        // handle common scalar types
        format_scalar_value_reader(sb, item);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        format_array_text_reader(sb, arr);
    }
    else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_text_reader(sb, mp);
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_text_reader(sb, elem);
    }
    else {
        // for other types (like DateTime), try format_scalar_value_reader
        format_scalar_value_reader(sb, item);
    }
}

// Public interface function that formats a Lambda Item as plain text
void format_text(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Reset recursion depth
    recursion_depth = 0;
    
    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    format_item_text_reader(sb, root);
}

// String variant that returns a String* allocated from the pool
String* format_text_string(Pool* pool, Item root_item) {
    if (!pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    format_text(sb, root_item);
    
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    
    return result;
}
