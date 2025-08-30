#include "format.h"
#include <string.h>
#include <ctype.h>
#include "../../lib/stringbuf.h"

// Global recursion depth counter to prevent infinite recursion
static thread_local int recursion_depth = 0;
#define MAX_RECURSION_DEPTH 50

static void format_item_text(StringBuf* sb, Item item);
static void format_element_text(StringBuf* sb, Element* elem);
static void format_array_text(StringBuf* sb, Array* arr);
static void format_map_text(StringBuf* sb, Map* mp);

// Helper function to format scalar values as raw text (no quotes)
static void format_scalar_value(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);
    
    switch (type) {
        case LMD_TYPE_BOOL: {
            bool val = item.bool_val;
            stringbuf_append_str(sb, val ? "true" : "false");
            break;
        }
        case LMD_TYPE_INT: {
            int val = item.int_val;
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), "%d", val);
            stringbuf_append_str(sb, num_buf);
            break;
        }
        case LMD_TYPE_INT64: {
            // 64-bit integer stored as pointer
            long* lptr = (long*)item.pointer;
            if (lptr) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%ld", *lptr);
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_FLOAT: {
            // Double stored as pointer
            double* dptr = (double*)item.pointer;
            if (dptr && !isnan(*dptr) && !isinf(*dptr)) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%.15g", *dptr);
                stringbuf_append_str(sb, num_buf);
            }
            break;
        }
        case LMD_TYPE_STRING: {
            String* str = (String*)item.pointer;
            if (str && str->chars && str->len > 0) {
                // Output string content without quotes
                stringbuf_append_str_n(sb, str->chars, str->len);
            }
            break;
        }
        case LMD_TYPE_SYMBOL: {
            String* symbol = (String*)item.pointer;
            if (symbol && symbol->chars && symbol->len > 0) {
                // Output symbol content without quotes
                stringbuf_append_str_n(sb, symbol->chars, symbol->len);
            }
            break;
        }
        case LMD_TYPE_DTIME: {
            DateTime* dt_ptr = (DateTime*)item.pointer;
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
            break;
        }
        default:
            // For non-scalar types, recursively process them
            format_item_text(sb, item);
            break;
    }
}

// Format an array by extracting all scalar values from its elements
static void format_array_text(StringBuf* sb, Array* arr) {
    if (!arr || recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    // Iterate through array elements using length field and direct item access
    for (long i = 0; i < arr->length; i++) {
        Item item = arr->items[i];
        format_item_text(sb, item);
        
        // Add space between array elements for readability
        if (i < arr->length - 1) {
            stringbuf_append_char(sb, ' ');
        }
    }
    
    recursion_depth--;
}

// Format a map by extracting all scalar values from its fields
static void format_map_text(StringBuf* sb, Map* mp) {
    if (!mp || !mp->type || !mp->data || recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    TypeMap* map_type = (TypeMap*)mp->type;
    if (!map_type->shape || map_type->length <= 0) {
        recursion_depth--;
        return;
    }
    
    ShapeEntry* field = map_type->shape;
    bool first = true;
    
    for (int i = 0; i < map_type->length && field; i++) {
        if (!field || field->byte_offset < 0) break;
        
        void* field_data = ((char*)mp->data) + field->byte_offset;
        
        // Create item from field data
        Item field_item = create_item_from_field_data(field_data, field->type->type_id);
        
        // Add space separator between fields (except for first)
        if (!first) {
            stringbuf_append_char(sb, ' ');
        }
        first = false;
        
        // Extract scalar values from this field
        format_item_text(sb, field_item);
        
        field = field->next;
    }
    
    recursion_depth--;
}

// Format an element by extracting scalar values from its attributes and content
static void format_element_text(StringBuf* sb, Element* elem) {
    if (!elem || recursion_depth >= MAX_RECURSION_DEPTH) return;
    
    recursion_depth++;
    
    // Process element attributes (stored as a map)
    if (elem->data && elem->type) {
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        TypeMap* attr_map = (TypeMap*)elem_type;
        
        if (attr_map && attr_map->shape && attr_map->length > 0) {
            ShapeEntry* field = attr_map->shape;
            bool first = true;
            
            for (int i = 0; i < attr_map->length && field; i++) {
                if (!field || field->byte_offset < 0) break;
                
                void* field_data = ((char*)elem->data) + field->byte_offset;
                Item field_item = create_item_from_field_data(field_data, field->type->type_id);
                
                // Add space separator between attributes (except for first)
                if (!first) {
                    stringbuf_append_char(sb, ' ');
                }
                first = false;
                
                // Extract scalar values from attribute
                format_item_text(sb, field_item);
                
                field = field->next;
            }
        }
    }
    
    // Process element content (List part of Element)
    List* content_list = (List*)elem;
    if (content_list && content_list->length > 0) {
        // Add space before content if we had attributes
        if (elem->data && elem->type) {
            stringbuf_append_char(sb, ' ');
        }
        
        for (long i = 0; i < content_list->length; i++) {
            Item content_item = content_list->items[i];
            format_item_text(sb, content_item);
            
            // Add space between content items
            if (i < content_list->length - 1) {
                stringbuf_append_char(sb, ' ');
            }
        }
    }
    
    recursion_depth--;
}

// Main recursive function to extract scalar values from any Lambda Item
static void format_item_text(StringBuf* sb, Item item) {
    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        return; // Prevent stack overflow
    }
    
    TypeId type = get_type_id(item);
    
    switch (type) {
        case LMD_TYPE_BOOL:
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_DTIME:
            // Handle scalar types directly
            format_scalar_value(sb, item);
            break;
            
        case LMD_TYPE_ARRAY: {
            Array* arr = (Array*)item.pointer;
            if (arr) {
                format_array_text(sb, arr);
            }
            break;
        }
        
        case LMD_TYPE_LIST: {
            List* list = (List*)item.pointer;
            if (list) {
                format_array_text(sb, (Array*)list);
            }
            break;
        }
        
        case LMD_TYPE_MAP: {
            Map* mp = (Map*)item.pointer;
            if (mp) {
                format_map_text(sb, mp);
            }
            break;
        }
        
        case LMD_TYPE_ELEMENT: {
            Element* elem = (Element*)item.pointer;
            if (elem) {
                format_element_text(sb, elem);
            }
            break;
        }
        
        default:
            // For unknown types, try to treat as pointer and skip
            break;
    }
}

// Public interface function that formats a Lambda Item as plain text
void format_text(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // Reset recursion depth
    recursion_depth = 0;
    
    // Extract all scalar values from the item tree
    format_item_text(sb, root_item);
}

// String variant that returns a String* allocated from the pool
String* format_text_string(VariableMemPool* pool, Item root_item) {
    if (!pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    format_text(sb, root_item);
    
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    
    return result;
}
