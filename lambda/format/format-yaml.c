// YAML Formatter - Refactored for clarity and maintainability
// Safe implementation with centralized type handling
#include "format.h"
#include <string.h>

// forward declarations
static void format_item(StrBuf* sb, Item item, int indent_level);
static void format_array_items(StrBuf* sb, Array* arr, int indent_level);
static void format_map_items(StrBuf* sb, TypeMap* map_type, void* map_data, int indent_level);
static void format_yaml_string(StrBuf* sb, String* str);
static void add_yaml_indent(StrBuf* sb, int indent_level);

// add indentation for nested structures
static void add_yaml_indent(StrBuf* sb, int indent_level) {
    for (int i = 0; i < indent_level * 2; i++) {
        strbuf_append_char(sb, ' ');
    }
}

// format a string value for YAML - handle quoting and escaping
static void format_yaml_string(StrBuf* sb, String* str) {
    if (!str) {
        strbuf_append_str(sb, "null");
        return;
    }
    
    const char* s = str->chars;
    size_t len = str->len;
    bool needs_quotes = false;
    
    // check if string needs quotes (contains special chars, starts with special chars, etc.)
    if (len == 0 || strchr(s, ':') || strchr(s, '\n') || strchr(s, '"') || 
        strchr(s, '\'') || strchr(s, '#') || strchr(s, '-') || strchr(s, '[') || 
        strchr(s, ']') || strchr(s, '{') || strchr(s, '}') || 
        (len > 0 && (isspace(s[0]) || isspace(s[len-1])))) {
        needs_quotes = true;
    }
    
    // also check for yaml reserved words
    if (!needs_quotes && (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || 
                         strcmp(s, "null") == 0 || strcmp(s, "yes") == 0 || 
                         strcmp(s, "no") == 0)) {
        needs_quotes = true;
    }
    
    if (needs_quotes) {
        strbuf_append_char(sb, '"');
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (needs_quotes) {
            switch (c) {
            case '"':
                strbuf_append_str(sb, "\\\"");
                break;
            case '\\':
                strbuf_append_str(sb, "\\\\");
                break;
            case '\n':
                strbuf_append_str(sb, "\\n");
                break;
            case '\r':
                strbuf_append_str(sb, "\\r");
                break;
            case '\t':
                strbuf_append_str(sb, "\\t");
                break;
            default:
                strbuf_append_char(sb, c);
                break;
            }
        } else {
            strbuf_append_char(sb, c);
        }
    }
    
    if (needs_quotes) {
        strbuf_append_char(sb, '"');
    }
}

// format array items for YAML
static void format_array_items(StrBuf* sb, Array* arr, int indent_level) {
    if (!arr || arr->length == 0) {
        strbuf_append_str(sb, "[]");
        return;
    }
    
    for (long i = 0; i < arr->length; i++) {
        if (i > 0 || indent_level > 0) {
            strbuf_append_char(sb, '\n');
            add_yaml_indent(sb, indent_level);
        }
        strbuf_append_str(sb, "- ");
        
        // use the direct item access like TOML formatter
        Item item = arr->items[i];
        TypeId item_type = (TypeId)(item >> 56);
        
        // for complex types, add proper indentation
        if (item_type == LMD_TYPE_MAP || item_type == LMD_TYPE_ELEMENT || 
            item_type == LMD_TYPE_ARRAY || item_type == LMD_TYPE_LIST) {
            format_item(sb, item, indent_level + 1);
        } else {
            format_item(sb, item, 0);
        }
    }
}

// format map items using centralized field data creation
static void format_map_items(StrBuf* sb, TypeMap* map_type, void* map_data, int indent_level) {
    if (!map_type || !map_data) {
        return;
    }
    
    bool first_item = true;
    ShapeEntry *field = map_type->shape;
    int field_count = 0;
    
    // safely iterate through the map fields using linked list with bounds checking
    while (field && field_count < map_type->length) {
        if (!field->name) {
            // nested map - handle specially
            void* data = ((char*)map_data) + field->byte_offset;
            Map *nest_map = *(Map**)data;
            if (nest_map && nest_map->type) {
                TypeMap *nest_map_type = (TypeMap*)nest_map->type;
                format_map_items(sb, nest_map_type, nest_map->data, indent_level);
            }
        } else {
            // named field - use centralized function to create proper Lambda Item
            void* field_data = ((char*)map_data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            // create proper Lambda Item using centralized function
            Item field_item = create_item_from_field_data(field_data, field_type);
            
            // skip null/unset fields appropriately
            if (field_type == LMD_TYPE_NULL) {
                if (!first_item) {
                    strbuf_append_char(sb, '\n');
                }
                first_item = false;
                
                if (indent_level > 0) {
                    add_yaml_indent(sb, indent_level);
                }
                
                strbuf_append_format(sb, "%.*s: null", (int)field->name->length, field->name->str);
                field = field->next;
                field_count++;
                continue;
            }
            
            if (!first_item) {
                strbuf_append_char(sb, '\n');
            }
            first_item = false;
            
            if (indent_level > 0) {
                add_yaml_indent(sb, indent_level);
            }
            
            // add field name
            strbuf_append_format(sb, "%.*s: ", (int)field->name->length, field->name->str);
            
            // format field value using centralized format_item function
            if (field_type == LMD_TYPE_MAP || field_type == LMD_TYPE_ELEMENT || 
                field_type == LMD_TYPE_ARRAY || field_type == LMD_TYPE_LIST) {
                // for complex types, add newline and proper indentation
                if (field_type == LMD_TYPE_MAP || field_type == LMD_TYPE_ELEMENT) {
                    strbuf_append_char(sb, '\n');
                }
                format_item(sb, field_item, indent_level + 1);
            } else {
                format_item(sb, field_item, 0);
            }
        }
        
        field = field->next;
        field_count++;
    }
}

// centralized function to format any Lambda Item with proper type handling
static void format_item(StrBuf* sb, Item item, int indent_level) {
    // prevent infinite recursion
    if (indent_level > 10) {
        strbuf_append_str(sb, "\"[max_depth]\"");
        return;
    }
    
    TypeId type_id = get_type_id((LambdaItem)item);
    
    switch (type_id) {
        case LMD_TYPE_NULL:
            strbuf_append_str(sb, "null");
            break;
        case LMD_TYPE_BOOL: {
            bool val = get_bool_value(item);
            strbuf_append_str(sb, val ? "true" : "false");
            break;
        }
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT:
            // use centralized number formatting
            format_number(sb, item);
            break;
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL: {
            String* str = (String*)get_pointer(item);
            if (str) {
                format_yaml_string(sb, str);
            } else {
                strbuf_append_str(sb, "null");
            }
            break;
        }
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST: {
            Array* arr = (Array*)get_pointer(item);
            format_array_items(sb, arr, indent_level);
            break;
        }
        case LMD_TYPE_MAP: {
            Map* mp = (Map*)get_pointer(item);
            if (mp && mp->type) {
                TypeMap* map_type = (TypeMap*)mp->type;
                format_map_items(sb, map_type, mp->data, indent_level);
            } else {
                strbuf_append_str(sb, "{}");
            }
            break;
        }
        case LMD_TYPE_ELEMENT: {
            Element* element = (Element*)get_pointer(item);
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            
            // for yaml, represent element as an object with special "$" key for tag name
            if (indent_level > 0) {
                strbuf_append_char(sb, '\n');
                add_yaml_indent(sb, indent_level);
            }
            strbuf_append_format(sb, "$: \"%.*s\"", (int)elmt_type->name.length, elmt_type->name.str);
            
            // add attributes if any
            if (elmt_type && elmt_type->length > 0 && element->data) {
                strbuf_append_char(sb, '\n');
                format_map_items(sb, (TypeMap*)elmt_type, element->data, indent_level);
            }
            
            // add children if any
            if (elmt_type && elmt_type->content_length > 0) {
                if (indent_level > 0) {
                    strbuf_append_char(sb, '\n');
                    add_yaml_indent(sb, indent_level);
                } else {
                    strbuf_append_char(sb, '\n');
                }
                strbuf_append_str(sb, "_:");
                
                List* list = (List*)element;
                format_array_items(sb, (Array*)list, indent_level + 1);
            }
            break;
        }
        default:
            // fallback for unknown types
            strbuf_append_format(sb, "\"[type_%d]\"", (int)type_id);
            break;
    }
}

// yaml formatter that produces proper YAML output
String* format_yaml(VariableMemPool* pool, Item root_item) {
    printf("format_yaml: ENTRY - direct traversal version\n");
    fflush(stdout);
    
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) {
        printf("format_yaml: failed to create string buffer\n");
        fflush(stdout);
        return NULL;
    }
    
    // start with YAML document marker
    strbuf_append_str(sb, "---\n");
    
    // add lowercase comment as requested
    strbuf_append_str(sb, "# yaml formatted output\n");
    
    // format the root item directly using centralized function
    format_item(sb, root_item, 0);
    
    // add final newline for proper YAML format
    strbuf_append_char(sb, '\n');
    
    return strbuf_to_string(sb);
}
