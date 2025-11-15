#include "format.h"
#include <string.h>
#include <ctype.h>
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include "format-utils.hpp"

// Forward declarations (MarkReader-based with context)
static void format_item_text_reader(TextContext& ctx, const ItemReader& item);
static void format_element_text_reader(TextContext& ctx, const ElementReader& elem);
static void format_array_text_reader(TextContext& ctx, const ArrayReader& arr);
static void format_map_text_reader(TextContext& ctx, const MapReader& mp);
static void format_scalar_value_reader(TextContext& ctx, const ItemReader& item);

// Helper function to format scalar values as raw text (no quotes) - MarkReader version
static void format_scalar_value_reader(TextContext& ctx, const ItemReader& item) {
    if (item.isBool()) {
        bool val = item.asBool();
        ctx.write_text(val ? "true" : "false");
    }
    else if (item.isInt()) {
        int val = item.asInt();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%d", val);
        ctx.write_text(num_buf);
    }
    else if (item.isFloat()) {
        double val = item.asFloat();
        if (!isnan(val) && !isinf(val)) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%.15g", val);
            ctx.write_text(num_buf);
        }
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars && str->len > 0) {
            // Output string content without quotes
            ctx.write_text(str);
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
                ctx.write_text(date_buf);
            }
        } else {
            // For non-scalar types, recursively process them
            format_item_text_reader(ctx, item);
        }
    }
}

// MarkReader-based version: format an array by extracting scalar values
static void format_array_text_reader(TextContext& ctx, const ArrayReader& arr) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;
    
    auto items_iter = arr.items();
    ItemReader item;
    bool first = true;
    
    while (items_iter.next(&item)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;
        
        format_item_text_reader(ctx, item);
    }
}

// MarkReader-based version: format a map by extracting scalar values
static void format_map_text_reader(TextContext& ctx, const MapReader& mp) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;
    
    auto entries = mp.entries();
    const char* key;
    ItemReader value;
    bool first = true;
    
    while (entries.next(&key, &value)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;
        
        format_item_text_reader(ctx, value);
    }
}

// MarkReader-based version: format element by extracting scalar values
static void format_element_text_reader(TextContext& ctx, const ElementReader& elem) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;
    
    // for text extraction, we focus on element children/content
    // attributes are typically metadata, not content text
    
    // process element children
    auto children_iter = elem.children();
    ItemReader child;
    bool first = true;
    
    while (children_iter.next(&child)) {
        if (!first) {
            ctx.write_char(' ');
        }
        first = false;
        
        format_item_text_reader(ctx, child);
    }
}

// MarkReader-based version: main recursive function to extract scalar values
static void format_item_text_reader(TextContext& ctx, const ItemReader& item) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) return;
    
    if (item.isNull()) {
        // skip null values
        return;
    }
    else if (item.isBool() || item.isInt() || item.isFloat() || item.isString()) {
        // handle common scalar types
        format_scalar_value_reader(ctx, item);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        format_array_text_reader(ctx, arr);
    }
    else if (item.isMap()) {
        MapReader mp = item.asMap();
        format_map_text_reader(ctx, mp);
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_text_reader(ctx, elem);
    }
    else {
        // for other types (like DateTime), try format_scalar_value_reader
        format_scalar_value_reader(ctx, item);
    }
}

// Public interface function that formats a Lambda Item as plain text
void format_text(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Create a temporary pool for context operations
    Pool* temp_pool = pool_create();
    TextContext ctx(temp_pool, sb);
    
    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    format_item_text_reader(ctx, root);
    
    pool_destroy(temp_pool);
}

// String variant that returns a String* allocated from the pool
String* format_text_string(Pool* pool, Item root_item) {
    if (!pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    // Create context using the passed pool (not a temporary one)
    TextContext ctx(pool, sb);
    
    // use MarkReader API for type-safe traversal
    ItemReader root(root_item.to_const());
    format_item_text_reader(ctx, root);
    
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    
    return result;
}
