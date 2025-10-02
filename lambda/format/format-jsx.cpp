#include "format.h"
#include "../../lib/stringbuf.h"
#include <ctype.h>

// Forward declarations
static void format_jsx_element(StringBuf* sb, Element* elem);
static void format_jsx_fragment(StringBuf* sb, Element* elem);
static void format_jsx_attributes(StringBuf* sb, Element* elem);
static void format_jsx_attribute_value(StringBuf* sb, String* value);
static void format_jsx_text_content(StringBuf* sb, String* text);
static void format_jsx_children(StringBuf* sb, Element* elem);
static void format_js_expression_element(StringBuf* sb, Element* js_elem);
static bool is_js_expression_element(Element* elem);

// Utility function to get attribute value from element
static String* get_jsx_attribute(Element* elem, const char* attr_name) {
    if (!elem || !elem->data) return NULL;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return NULL;
    
    // Cast the element type to TypeMap to access attributes
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return NULL;
    
    // Iterate through shape entries to find the attribute
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->name->length == strlen(attr_name) &&
            strncmp(field->name->str, attr_name, field->name->length) == 0) {
            void* data = ((char*)elem->data) + field->byte_offset;
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                return *(String**)data;
            }
        }
        field = field->next;
    }
    return NULL;
}

// Check if element is a JS expression element
static bool is_js_expression_element(Element* elem) {
    if (!elem) return false;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return false;
    
    return elem_type->name.length == 2 && strncmp(elem_type->name.str, "js", 2) == 0;
}

// Format JS expression element: <js "expression"> -> {expression}
static void format_js_expression_element(StringBuf* sb, Element* js_elem) {
    if (!js_elem || !is_js_expression_element(js_elem)) return;
    
    stringbuf_append_char(sb, '{');
    
    // Get the JS expression content from the element's children
    List* children = (List*)js_elem;
    if (children && children->items && children->length > 0) {
        Item first_child = children->items[0];
        if (first_child.item != ITEM_NULL && get_type_id(first_child) == LMD_TYPE_STRING) {
            String* js_content = (String*)first_child.pointer;
            // Add extra validation for String pointer
            if (js_content && js_content->len > 0 && js_content->len < 10000) {
                stringbuf_append_str(sb, js_content->chars);
            }
        }
    }
    
    stringbuf_append_char(sb, '}');
}

// Format JSX text content with proper escaping
static void format_jsx_text_content(StringBuf* sb, String* text) {
    if (!text || text->len == 0 || text->len > 10000) return;
    
    for (int i = 0; i < text->len; i++) {
        char c = text->chars[i];
        switch (c) {
            case '<':
                stringbuf_append_str(sb, "&lt;");
                break;
            case '>':
                stringbuf_append_str(sb, "&gt;");
                break;
            case '&':
                stringbuf_append_str(sb, "&amp;");
                break;
            case '{':
                stringbuf_append_str(sb, "&#123;");
                break;
            case '}':
                stringbuf_append_str(sb, "&#125;");
                break;
            default:
                stringbuf_append_char(sb, c);
                break;
        }
    }
}

// Format JSX attribute value
static void format_jsx_attribute_value(StringBuf* sb, String* value) {
    if (!value || value->len == 0) return;
    
    stringbuf_append_char(sb, '"');
    
    for (int i = 0; i < value->len; i++) {
        char c = value->chars[i];
        switch (c) {
            case '"':
                stringbuf_append_str(sb, "&quot;");
                break;
            case '&':
                stringbuf_append_str(sb, "&amp;");
                break;
            case '<':
                stringbuf_append_str(sb, "&lt;");
                break;
            case '>':
                stringbuf_append_str(sb, "&gt;");
                break;
            default:
                stringbuf_append_char(sb, c);
                break;
        }
    }
    
    stringbuf_append_char(sb, '"');
}

// Format JSX attributes
static void format_jsx_attributes(StringBuf* sb, Element* elem) {
    if (!elem || !elem->data) return;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type) return;
    
    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return;
    
    // Iterate through attributes
    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name) {
            const char* attr_name = field->name->str;
            
            // Skip internal attributes and JSX type markers
            if (strcmp(attr_name, "is_component") == 0 ||
                strcmp(attr_name, "self_closing") == 0) {
                field = field->next;
                continue;
            }
            
            // Skip type attribute if it's an internal JSX type marker
            if (strcmp(attr_name, "type") == 0) {
                void* data = ((char*)elem->data) + field->byte_offset;
                if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                    String* type_value = *(String**)data;
                    if (type_value && strcmp(type_value->chars, "jsx_element") == 0) {
                        field = field->next;
                        continue;
                    }
                }
            }
            
            stringbuf_append_char(sb, ' ');
            stringbuf_append_str(sb, attr_name);
            
            void* data = ((char*)elem->data) + field->byte_offset;
            
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                String* attr_value = *(String**)data;
                if (attr_value && strcmp(attr_value->chars, "true") != 0) {
                    stringbuf_append_char(sb, '=');
                    format_jsx_attribute_value(sb, attr_value);
                }
                // Boolean attributes (value="true") are output without value
            } else if (field->type && field->type->type_id == LMD_TYPE_ELEMENT) {
                // JSX expression attribute
                Element* expr_elem = *(Element**)data;
                if (expr_elem && is_js_expression_element(expr_elem)) {
                    stringbuf_append_char(sb, '=');
                    format_js_expression_element(sb, expr_elem);
                }
            }
        }
        field = field->next;
    }
}

// Format JSX children
static void format_jsx_children(StringBuf* sb, Element* elem) {
    List* children = (List*)elem;
    if (!children || children->length == 0) return;
    
    for (int i = 0; i < children->length; i++) {
        Item child = children->items[i];
        if (child.item == ITEM_NULL) continue;
        
        if (get_type_id(child) == LMD_TYPE_STRING) {
            String* text = (String*)child.pointer;
            if (text) {
                format_jsx_text_content(sb, text);
            }
        } else if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            if (child_elem) {
                if (is_js_expression_element(child_elem)) {
                    format_js_expression_element(sb, child_elem);
                } else {
                    format_jsx_element(sb, child_elem);
                }
            }
        }
    }
}

// Format JSX fragment: <>...</>
static void format_jsx_fragment(StringBuf* sb, Element* elem) {
    stringbuf_append_str(sb, "<>");
    format_jsx_children(sb, elem);
    stringbuf_append_str(sb, "</>");
}

// Format JSX element
static void format_jsx_element(StringBuf* sb, Element* elem) {
    if (!elem) return;
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return;
    
    // Extract tag name from StrView
    char tag_name[256];
    int name_len = elem_type->name.length < 255 ? elem_type->name.length : 255;
    strncpy(tag_name, elem_type->name.str, name_len);
    tag_name[name_len] = '\0';
    
    // Handle JSX fragment
    if (strcmp(tag_name, "jsx_fragment") == 0) {
        format_jsx_fragment(sb, elem);
        return;
    }
    
    // Handle JS expression element
    if (is_js_expression_element(elem)) {
        format_js_expression_element(sb, elem);
        return;
    }
    
    // Regular JSX element
    stringbuf_append_char(sb, '<');
    stringbuf_append_str(sb, tag_name);
    
    // Format attributes
    format_jsx_attributes(sb, elem);
    
    // Check if self-closing
    String* self_closing = get_jsx_attribute(elem, "self_closing");
    if (self_closing && strcmp(self_closing->chars, "true") == 0) {
        stringbuf_append_str(sb, " />");
        return;
    }
    
    stringbuf_append_char(sb, '>');
    
    // Format children
    format_jsx_children(sb, elem);
    
    // Closing tag
    stringbuf_append_str(sb, "</");
    stringbuf_append_str(sb, tag_name);
    stringbuf_append_char(sb, '>');
}

// Format item (handles both elements and strings)
static void format_jsx_item(StringBuf* sb, Item item) {
    if (item.item == ITEM_NULL) return;
    
    TypeId type_id = get_type_id(item);
    
    if (type_id == LMD_TYPE_STRING) {
        String* text = (String*)item.item;
        format_jsx_text_content(sb, text);
    } else if (type_id == LMD_TYPE_ELEMENT) {
        Element* elem = (Element*)item.item;
        format_jsx_element(sb, elem);
    }
}

// Main JSX formatter entry point
String* format_jsx(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    
    format_jsx_item(sb, root_item);
    
    return stringbuf_to_string(sb);
}
