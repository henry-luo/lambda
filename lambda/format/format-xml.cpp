#include "format.h"
#include "../windows_compat.h"  // For Windows compatibility functions like strndup
#include "../../lib/stringbuf.h"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

static void format_item(StringBuf* sb, Item item, const char* tag_name);

// Helper function to check if a type is simple (can be output as XML attribute)
static bool is_simple_type(TypeId type) {
    return type == LMD_TYPE_STRING || type == LMD_TYPE_INT || 
           type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT || 
           type == LMD_TYPE_BOOL;
}

static void format_xml_string(StringBuf* sb, String* str) {
    if (!str || !str->chars) return;
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '<':
            stringbuf_append_str(sb, "&lt;");
            break;
        case '>':
            stringbuf_append_str(sb, "&gt;");
            break;
        case '&':
            // Check if this is already an entity reference
            if (i + 1 < len && (s[i + 1] == '#' || isalpha(s[i + 1]))) {
                // Look for closing semicolon
                size_t j = i + 1;
                while (j < len && s[j] != ';' && s[j] != ' ' && s[j] != '<' && s[j] != '&') {
                    j++;
                }
                if (j < len && s[j] == ';') {
                    // This looks like an entity, preserve it as-is
                    for (size_t k = i; k <= j; k++) {
                        stringbuf_append_char(sb, s[k]);
                    }
                    i = j; // Skip past the entity
                    break;
                }
            }
            stringbuf_append_str(sb, "&amp;");
            break;
        case '"':
            stringbuf_append_str(sb, "&quot;");
            break;
        case '\'':
            stringbuf_append_str(sb, "&apos;");
            break;
        default:
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                // Control characters - encode as numeric character reference
                char hex_buf[10];
                snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                stringbuf_append_str(sb, hex_buf);
            } else {
                stringbuf_append_char(sb, c);
            }
            break;
        }
    }
}

static void format_array(StringBuf* sb, Array* arr, const char* tag_name) {
    printf("format_array: arr %p, length %ld\n", (void*)arr, arr ? arr->length : 0);
    if (arr && arr->length > 0) {
        for (long i = 0; i < arr->length; i++) {
            Item item = arr->items[i];
            format_item(sb, item, tag_name ? tag_name : "item");
        }
    }
}

static void format_map_attributes(StringBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            
            // Only output simple types as attributes
            TypeId field_type = field->type->type_id;
            if (is_simple_type(field_type)) {
                stringbuf_append_char(sb, ' ');
                stringbuf_append_format(sb, "%.*s=\"", (int)field->name->length, field->name->str);
                
                if (field_type == LMD_TYPE_STRING) {
                    String* str = *(String**)data;
                    if (str) {
                        format_xml_string(sb, str);
                    }
                } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                    int64_t int_val = *(int64_t*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%" PRId64, int_val);
                    stringbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_FLOAT) {
                    double float_val = *(double*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                    stringbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_BOOL) {
                    bool bool_val = *(bool*)data;
                    stringbuf_append_str(sb, bool_val ? "true" : "false");
                }
                // else // leave out null or unsupported types
                
                stringbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_attributes(StringBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    printf("format_attributes: map_type %p, map_data %p\n", (void*)map_type, (void*)map_data);
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            // Debug: print field information
            printf("Field %d: name length=%lld, name str='%.*s', type=%d\n", 
                   i, (long long)field->name->length, (int)field->name->length, field->name->str, field_type);
            
            // Only output simple types as attributes, skip null values
            if (is_simple_type(field_type) && field_type != LMD_TYPE_NULL) {
                stringbuf_append_char(sb, ' ');
                stringbuf_append_format(sb, "%.*s=\"", (int)field->name->length, field->name->str);
                
                if (field_type == LMD_TYPE_STRING) {
                    String* str = *(String**)data;
                    if (str && str != &EMPTY_STRING) {
                        // Also check for literal "lambda.nil" content
                        if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
                            // Output empty string for "lambda.nil"
                        } else {
                            format_xml_string(sb, str);
                        }
                    }
                } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                    int64_t int_val = *(int64_t*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%ld", int_val);
                    stringbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_FLOAT) {
                    double float_val = *(double*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                    stringbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_BOOL) {
                    bool bool_val = *(bool*)data;
                    stringbuf_append_str(sb, bool_val ? "true" : "false");
                }
                // else // leave out null or unsupported types
                
                stringbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_map_elements(StringBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    printf("format_map_elements: map_type %p, map_data %p\n", (void*)map_type, (void*)map_data);
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            TypeId field_type = field->type->type_id;
            
            // Debug: print field information
            printf("Field %d: name length=%lld, name str='%.*s', type=%d\n", 
                   i, (long long)field->name->length, (int)field->name->length, field->name->str, field_type);
            
            // Only output complex types as child elements (not simple attributes)
            if (!is_simple_type(field_type)) {
                if (field_type == LMD_TYPE_NULL) {
                    // Create empty element for null
                    stringbuf_append_char(sb, '<');
                    stringbuf_append_format(sb, " %.*s=\"%.*s\"", (int)field->name->length, field->name->str, (int)field->name->length, field->name->str);
                    stringbuf_append_str(sb, "/>");
                } else {
                    // Create a proper null-terminated tag name
                    printf("format field: %d\n", field_type);
                    StringBuf* tag_buf = stringbuf_new(NULL);
                    stringbuf_append_format(tag_buf, "%.*s", (int)field->name->length, field->name->str);
                    String* tag_string = stringbuf_to_string(tag_buf);
                    char* tag_name = tag_string->chars;
                    
                    Item item_data = *(Item*)data;
                    format_item(sb, item_data, tag_name);
                    
                    stringbuf_free(tag_buf);
                }
            }
        }
        field = field->next;
    }
}

static void format_map(StringBuf* sb, Map* mp, const char* tag_name) {
    printf("format_map: mp %p, type %p, data %p\n", (void*)mp, (void*)mp->type, (void*)mp->data);
    
    if (!tag_name) tag_name = "object";
    
    stringbuf_append_char(sb, '<');
    stringbuf_append_str(sb, tag_name);
    
    // Add simple types as attributes for better XML structure
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        format_map_attributes(sb, map_type, mp->data);
    }
    
    // Check if there are complex child elements
    bool has_children = false;
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        ShapeEntry* field = map_type->shape;
        for (int i = 0; i < map_type->length && field; i++) {
            if (field->name && field->type && !is_simple_type(field->type->type_id)) {
                has_children = true;
                break;
            }
            field = field->next;
        }
    }
    
    if (has_children) {
        stringbuf_append_char(sb, '>');
        
        // Add complex types as child elements
        if (mp && mp->type) {
            TypeMap* map_type = (TypeMap*)mp->type;
            format_map_elements(sb, map_type, mp->data);
        }
        
        stringbuf_append_str(sb, "</");
        stringbuf_append_str(sb, tag_name);
        stringbuf_append_char(sb, '>');
    } else {
        // Self-closing tag if no children
        stringbuf_append_str(sb, "/>");
    }
}

static void format_item(StringBuf* sb, Item item, const char* tag_name) {
    // Safety check for null pointer
    if (!sb) return;
    
    // Check if item is null
    if ((item.item == ITEM_NULL)) {
        if (!tag_name) tag_name = "value";
        stringbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    // Additional safety check for Item structure
    if (get_type_id(item) == 0 && item.pointer == 0) {
        if (!tag_name) tag_name = "value";
        stringbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    TypeId type = get_type_id(item);
    printf("format_item: item %p, type %d, tag_name '%s'\n", (void*)item.pointer, type, tag_name ? tag_name : "NULL");
    
    if (!tag_name) tag_name = "value";
    
    switch (type) {
    case LMD_TYPE_NULL:
        stringbuf_append_format(sb, "<%s/>", tag_name);
        break;
    case LMD_TYPE_BOOL: {
        bool val = item.bool_val;
        stringbuf_append_format(sb, "<%s>%s</%s>", tag_name, val ? "true" : "false", tag_name);
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        stringbuf_append_format(sb, "<%s>", tag_name);
        format_number(sb, item);
        stringbuf_append_format(sb, "</%s>", tag_name);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        stringbuf_append_format(sb, "<%s>", tag_name);
        if (str) {
            format_xml_string(sb, str);
        }
        stringbuf_append_format(sb, "</%s>", tag_name);
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item.pointer;
        if (arr) {
            stringbuf_append_format(sb, "<%s>", tag_name);
            format_array(sb, arr, "item");
            stringbuf_append_format(sb, "</%s>", tag_name);
        } else {
            stringbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item.pointer;
        if (mp) {
            format_map(sb, mp, tag_name);
        } else {
            stringbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        printf("format_item: handling LMD_TYPE_ELEMENT\n");
        Element* element = item.element;
        if (!element || !element->type) {
            printf("format_item: element is null or element->type is null\n");
            stringbuf_append_format(sb, "<%s/>", tag_name);
            break;
        }
        
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        printf("format_item: element type name='%.*s', length=%ld, content_length=%ld\n", 
               (int)elmt_type->name.length, elmt_type->name.str, elmt_type->length, elmt_type->content_length);
        
        char element_name[256];
        snprintf(element_name, sizeof(element_name), "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
        
        // Special handling for XML declaration
        if (strcmp(element_name, "?xml") == 0) {
            stringbuf_append_str(sb, "<?xml");
            
            // Handle XML declaration content as attributes
            if (elmt_type->content_length > 0) {
                List* element_as_list = (List*)element;
                for (long i = 0; i < element_as_list->length; i++) {
                    Item content_item = element_as_list->items[i];
                    TypeId content_type = get_type_id(content_item);
                    
                    if (content_type == LMD_TYPE_STRING) {
                        String* str = (String*)content_item.pointer;
                        if (str && str != &EMPTY_STRING) {
                            stringbuf_append_char(sb, ' ');
                            stringbuf_append_str(sb, str->chars);
                        }
                    }
                }
            }
            
            stringbuf_append_str(sb, "?>");
            return; // Early return for XML declaration
        }
        
        stringbuf_append_char(sb, '<');
        stringbuf_append_str(sb, element_name);
        
        // Handle attributes (simple types from element fields)
        if (elmt_type->length > 0 && element->data) {
            printf("format_item: element has %ld fields, checking for attributes\n", elmt_type->length);
            TypeMap* map_type = (TypeMap*)elmt_type;
            format_attributes(sb, map_type, element->data);
        }
        
        stringbuf_append_char(sb, '>');
        
        // Handle element content (text/child elements as list)
        if (elmt_type->content_length > 0) {
            printf("format_item: element has %ld content items, formatting as list\n", elmt_type->content_length);
            List* element_as_list = (List*)element;
            printf("format_item: list length=%ld\n", element_as_list->length);
            
            // Format each content item
            for (long i = 0; i < element_as_list->length; i++) {
                Item content_item = element_as_list->items[i];
                TypeId content_type = get_type_id(content_item);
                printf("format_item: content item %ld, type=%d\n", i, content_type);
                
                if (content_type == LMD_TYPE_STRING) {
                    String* str = (String*)content_item.pointer;
                    if (str && str != &EMPTY_STRING) {
                        // Also check for literal "lambda.nil" content
                        if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) {
                            // Skip "lambda.nil" content - don't output anything
                        } else {
                            format_xml_string(sb, str);
                        }
                    }
                    // Skip EMPTY_STRING - don't output "lambda.nil"
                } else {
                    // For child elements, format them recursively
                    format_item(sb, content_item, NULL);
                }
            }
        }
        
        stringbuf_append_str(sb, "</");
        stringbuf_append_str(sb, element_name);
        stringbuf_append_char(sb, '>');
        break;
    }
    default:
        // unknown types
        printf("format_item: unknown type %d, handling as map\n", type);
        // Try to handle unknown types as maps
        Map* mp = (Map*)item.pointer;
        if (mp) {
            format_map(sb, mp, tag_name);
        } else {
            stringbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
}

String* format_xml(VariableMemPool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    printf("format_xml: root_item %p\n", (void*)root_item.pointer);
    
    // Check if we have a document structure with multiple children (XML declaration + root element)
    if (get_type_id(root_item) == LMD_TYPE_ELEMENT) {
        Element* root_element = (Element*)root_item.pointer;
        if (root_element && root_element->type) {
            TypeElmt* root_type = (TypeElmt*)root_element->type;
            
            printf("format_xml: root element name='%.*s', length=%ld, content_length=%ld\n", 
                   (int)root_type->name.length, root_type->name.str, root_type->name.length, root_type->content_length);
            
            // Check if this is a "document" element containing multiple children
            if (root_type->name.length == 8 && strncmp(root_type->name.str, "document", 8) == 0 && 
                root_type->content_length > 0) {
                
                List* root_as_list = (List*)root_element;
                printf("format_xml: document element with %ld children\n", root_as_list->length);
                
                // Format all children in order (XML declaration, then actual elements)
                for (long i = 0; i < root_as_list->length; i++) {
                    Item child = root_as_list->items[i];
                    if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                        Element* child_elem = (Element*)child.pointer;
                        if (child_elem && child_elem->type) {
                            TypeElmt* child_type = (TypeElmt*)child_elem->type;
                            
                            // Check if this is XML declaration
                            if (child_type->name.length == 4 && 
                                strncmp(child_type->name.str, "?xml", 4) == 0) {
                                printf("format_xml: formatting XML declaration\n");
                                format_item(sb, child, NULL);
                                stringbuf_append_char(sb, '\n');
                            } else {
                                // Format actual XML element with its proper name
                                char* element_name = strndup(child_type->name.str, child_type->name.length);
                                printf("format_xml: formatting element '%s'\n", element_name);
                                format_item(sb, child, element_name);
                                free(element_name);
                            }
                        }
                    }
                }
                
                return stringbuf_to_string(sb);
            }
        }
    }
    
    // Fallback: format as single element
    const char* tag_name = NULL;
    if (get_type_id(root_item) == LMD_TYPE_ELEMENT) {
        Element* element = (Element*)root_item.pointer;
        if (element && element->type) {
            TypeElmt* elmt_type = (TypeElmt*)element->type;
            if (elmt_type->name.length > 0 && elmt_type->name.str) {
                char* element_name = strndup(elmt_type->name.str, elmt_type->name.length);
                tag_name = element_name;
                printf("format_xml: using element name '%s'\n", tag_name);
            }
        }
    }
    
    if (!tag_name) {
        tag_name = "root";
        printf("format_xml: using default name 'root'\n");
    }
    
    format_item(sb, root_item, tag_name);
    
    // Free the allocated element name if we created one
    if (tag_name && strcmp(tag_name, "root") != 0) {
        free((char*)tag_name);
    }
    
    return stringbuf_to_string(sb);
}

// Convenience function that formats XML to a provided StringBuf
void format_xml_to_stringbuf(StringBuf* sb, Item root_item) {
    format_item(sb, root_item, "root");
}
