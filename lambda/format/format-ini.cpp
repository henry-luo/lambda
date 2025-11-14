// INI Formatter - Direct traversal implementation
// Safe implementation with centralized type handling
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <string.h>

// forward declarations
static void format_item(StringBuf* sb, Item item, const char* key_name);
static void format_map_as_section(StringBuf* sb, Map* map, const char* section_name);
static void format_ini_string(StringBuf* sb, String* str);
static bool is_simple_value(TypeId type_id);

// MarkReader-based forward declarations
static void format_item_reader(StringBuf* sb, const ItemReader& item, const char* key_name);
static void format_map_as_section_reader(StringBuf* sb, const MapReader& map, const char* section_name);

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
static void format_map_as_section(StringBuf* sb, Map* map, const char* section_name) {
    if (!map || !map->type || !map->data) {
        return;
    }
    
    TypeMap* map_type = (TypeMap*)map->type;
    
    // add section header if we have a name
    if (section_name && strlen(section_name) > 0) {
        stringbuf_append_format(sb, "[%s]\n", section_name);
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
                stringbuf_append_char(sb, '\n');
                format_map_as_section(sb, nest_map, "nested");
            }
        } else {
            // named field - create proper Lambda Item
            void* field_data = ((char*)map->data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            Item field_item = create_item_from_field_data(field_data, field_type);
            
            // format key=value pair
            stringbuf_append_format(sb, "%.*s=", (int)field->name->length, field->name->str);
            format_item(sb, field_item, field->name->str);
            stringbuf_append_char(sb, '\n');
        }
        
        field = field->next;
        field_count++;
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

// centralized function to format any Lambda Item for INI format
static void format_item(StringBuf* sb, Item item, const char* key_name) {
    TypeId type_id = get_type_id(item);
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            // empty value in INI
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
                        stringbuf_append_str(sb, ",");
                    }
                    
                    Item arr_item = arr->items[i];
                    TypeId arr_type = get_type_id(arr_item);
                    
                    // only format simple values in arrays for INI
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
            // nested maps cannot be represented as simple values in INI
            stringbuf_append_str(sb, "[map]");
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* element = item.element;
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
