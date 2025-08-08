#include "format.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

static void format_item(StrBuf* sb, Item item, const char* tag_name);

// Helper function to check if a type is simple (can be output as XML attribute)
static bool is_simple_type(TypeId type) {
    return type == LMD_TYPE_STRING || type == LMD_TYPE_INT || 
           type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT || 
           type == LMD_TYPE_BOOL;
}

static void format_xml_string(StrBuf* sb, String* str) {
    if (!str || !str->chars) return;
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '<':
            strbuf_append_str(sb, "&lt;");
            break;
        case '>':
            strbuf_append_str(sb, "&gt;");
            break;
        case '&':
            strbuf_append_str(sb, "&amp;");
            break;
        case '"':
            strbuf_append_str(sb, "&quot;");
            break;
        case '\'':
            strbuf_append_str(sb, "&apos;");
            break;
        default:
            if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                // Control characters - encode as numeric character reference
                char hex_buf[10];
                snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                strbuf_append_str(sb, hex_buf);
            } else {
                strbuf_append_char(sb, c);
            }
            break;
        }
    }
}

static void format_array(StrBuf* sb, Array* arr, const char* tag_name) {
    printf("format_array: arr %p, length %ld\n", (void*)arr, arr ? arr->length : 0);
    if (arr && arr->length > 0) {
        for (long i = 0; i < arr->length; i++) {
            Item item = arr->items[i];
            format_item(sb, item, tag_name ? tag_name : "item");
        }
    }
}

static void format_map_attributes(StrBuf* sb, TypeMap* map_type, void* map_data) {
    if (!map_type || !map_data || !map_type->shape) return;
    
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;
            
            // Only output simple types as attributes
            TypeId field_type = field->type->type_id;
            if (is_simple_type(field_type)) {
                strbuf_append_char(sb, ' ');
                strbuf_append_format(sb, "%.*s=\"", (int)field->name->length, field->name->str);
                
                if (field_type == LMD_TYPE_STRING) {
                    String* str = *(String**)data;
                    if (str) {
                        format_xml_string(sb, str);
                    }
                } else if (field_type == LMD_TYPE_INT || field_type == LMD_TYPE_INT64) {
                    long int_val = *(long*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%ld", int_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_FLOAT) {
                    double float_val = *(double*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_BOOL) {
                    bool bool_val = *(bool*)data;
                    strbuf_append_str(sb, bool_val ? "true" : "false");
                }
                // else // leave out null or unsupported types
                
                strbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_attributes(StrBuf* sb, TypeMap* map_type, void* map_data) {
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
                strbuf_append_char(sb, ' ');
                strbuf_append_format(sb, "%.*s=\"", (int)field->name->length, field->name->str);
                
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
                    long int_val = *(long*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%ld", int_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_FLOAT) {
                    double float_val = *(double*)data;
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%.15g", float_val);
                    strbuf_append_str(sb, num_buf);
                } else if (field_type == LMD_TYPE_BOOL) {
                    bool bool_val = *(bool*)data;
                    strbuf_append_str(sb, bool_val ? "true" : "false");
                }
                // else // leave out null or unsupported types
                
                strbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_map_elements(StrBuf* sb, TypeMap* map_type, void* map_data) {
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
                    strbuf_append_char(sb, '<');
                    strbuf_append_format(sb, "%.*s", (int)field->name->length, field->name->str);
                    strbuf_append_str(sb, "/>");
                } else {
                    // Create a proper null-terminated tag name
                    printf("format field: %d\n", field_type);
                    StrBuf* tag_buf = strbuf_new();
                    strbuf_append_format(tag_buf, "%.*s", (int)field->name->length, field->name->str);
                    char* tag_name = tag_buf->str;
                    
                    Item item_data = *(Item*)data;
                    format_item(sb, item_data, tag_name);
                    
                    strbuf_free(tag_buf);
                }
            }
        }
        field = field->next;
    }
}

static void format_map(StrBuf* sb, Map* mp, const char* tag_name) {
    printf("format_map: mp %p, type %p, data %p\n", (void*)mp, (void*)mp->type, (void*)mp->data);
    
    if (!tag_name) tag_name = "object";
    
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, tag_name);
    
    // For XML roundtrip, don't use attributes - put everything as child elements
    // if (mp && mp->type) {
    //     TypeMap* map_type = (TypeMap*)mp->type;
    //     // Add simple types as attributes
    //     format_map_attributes(sb, map_type, mp->data);
    // }
    
    strbuf_append_char(sb, '>');
    
    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        // Add all types as child elements for proper XML roundtrip
        format_map_elements(sb, map_type, mp->data);
    }
    
    strbuf_append_str(sb, "</");
    strbuf_append_str(sb, tag_name);
    strbuf_append_char(sb, '>');
}

static void format_item(StrBuf* sb, Item item, const char* tag_name) {
    // Safety check for null pointer
    if (!sb) return;
    
    // Check if item is null
    if ((item.item == ITEM_NULL)) {
        if (!tag_name) tag_name = "value";
        strbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    // Additional safety check for Item structure
    if (get_type_id(item) == 0 && item.pointer == 0) {
        if (!tag_name) tag_name = "value";
        strbuf_append_format(sb, "<%s/>", tag_name);
        return;
    }
    
    TypeId type = get_type_id(item);
    printf("format_item: item %p, type %d, tag_name '%s'\n", (void*)item.pointer, type, tag_name ? tag_name : "NULL");
    
    if (!tag_name) tag_name = "value";
    
    switch (type) {
    case LMD_TYPE_NULL:
        strbuf_append_format(sb, "<%s/>", tag_name);
        break;
    case LMD_TYPE_BOOL: {
        bool val = item.bool_val;
        strbuf_append_format(sb, "<%s>%s</%s>", tag_name, val ? "true" : "false", tag_name);
        break;
    }
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
        strbuf_append_format(sb, "<%s>", tag_name);
        format_number(sb, item);
        strbuf_append_format(sb, "</%s>", tag_name);
        break;
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        strbuf_append_format(sb, "<%s>", tag_name);
        if (str) {
            format_xml_string(sb, str);
        }
        strbuf_append_format(sb, "</%s>", tag_name);
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = (Array*)item.pointer;
        if (arr) {
            strbuf_append_format(sb, "<%s>", tag_name);
            format_array(sb, arr, "item");
            strbuf_append_format(sb, "</%s>", tag_name);
        } else {
            strbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = (Map*)item.pointer;
        if (mp) {
            format_map(sb, mp, tag_name);
        } else {
            strbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        printf("format_item: handling LMD_TYPE_ELEMENT\n");
        Element* element = (Element*)item.pointer;
        if (!element || !element->type) {
            printf("format_item: element is null or element->type is null\n");
            strbuf_append_format(sb, "<%s/>", tag_name);
            break;
        }
        
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        printf("format_item: element type name='%.*s', length=%ld, content_length=%ld\n", 
               (int)elmt_type->name.length, elmt_type->name.str, elmt_type->length, elmt_type->content_length);
        
        char element_name[256];
        snprintf(element_name, sizeof(element_name), "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
        
        // Special handling for XML declaration
        if (strcmp(element_name, "?xml") == 0) {
            strbuf_append_str(sb, "<?xml");
            
            // Handle XML declaration content as attributes
            if (elmt_type->content_length > 0) {
                List* element_as_list = (List*)element;
                for (long i = 0; i < element_as_list->length; i++) {
                    Item content_item = element_as_list->items[i];
                    TypeId content_type = get_type_id(content_item);
                    
                    if (content_type == LMD_TYPE_STRING) {
                        String* str = (String*)content_item.pointer;
                        if (str && str != &EMPTY_STRING) {
                            strbuf_append_char(sb, ' ');
                            strbuf_append_str(sb, str->chars);
                        }
                    }
                }
            }
            
            strbuf_append_str(sb, "?>");
            return; // Early return for XML declaration
        }
        
        strbuf_append_char(sb, '<');
        strbuf_append_str(sb, element_name);
        
        // Handle attributes (simple types from element fields)
        if (elmt_type->length > 0 && element->data) {
            printf("format_item: element has %ld fields, checking for attributes\n", elmt_type->length);
            TypeMap* map_type = (TypeMap*)elmt_type;
            format_attributes(sb, map_type, element->data);
        }
        
        strbuf_append_char(sb, '>');
        
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
        
        strbuf_append_str(sb, "</");
        strbuf_append_str(sb, element_name);
        strbuf_append_char(sb, '>');
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
            strbuf_append_format(sb, "<%s/>", tag_name);
        }
        break;
    }
}

String* format_xml(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    printf("format_xml: root_item %p\n", (void*)root_item.pointer);
    
    // Check if we have an XML document structure with declaration
    bool has_xml_declaration = false;
    Item actual_root = root_item;
    
    if (get_type_id(root_item) == LMD_TYPE_ELEMENT) {
        Element* root_element = (Element*)root_item.pointer;
        if (root_element && root_element->type) {
            TypeElmt* root_type = (TypeElmt*)root_element->type;
            
            // Check if this element has child elements
            if (root_type->content_length > 0) {
                List* root_as_list = (List*)root_element;
                
                // Check if first child is XML declaration
                if (root_as_list->length > 0) {
                    Item first_child = root_as_list->items[0];
                    if (get_type_id(first_child) == LMD_TYPE_ELEMENT) {
                        Element* first_elem = (Element*)first_child.pointer;
                        if (first_elem && first_elem->type) {
                            TypeElmt* first_type = (TypeElmt*)first_elem->type;
                            if (first_type->name.length == 4 && 
                                strncmp(first_type->name.str, "?xml", 4) == 0) {
                                has_xml_declaration = true;
                                
                                // Format XML declaration first
                                format_item(sb, first_child, NULL);
                                
                                // If there's a second child, use it as the real root
                                if (root_as_list->length > 1) {
                                    actual_root = root_as_list->items[1];
                                    printf("format_xml: found XML declaration, using second child as root\n");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Format the actual root element
    const char* tag_name = NULL;
    if (get_type_id(actual_root) == LMD_TYPE_ELEMENT) {
        Element* element = (Element*)actual_root.pointer;
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
    
    format_item(sb, actual_root, tag_name);
    
    // Free the allocated element name if we created one
    if (tag_name && strcmp(tag_name, "root") != 0) {
        free((char*)tag_name);
    }
    
    return strbuf_to_string(sb);
}

// Convenience function that formats XML to a provided StrBuf
void format_xml_to_strbuf(StrBuf* sb, Item root_item) {
    format_item(sb, root_item, "root");
}
