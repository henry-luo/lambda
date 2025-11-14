#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

// MarkReader-based forward declarations
static void format_item_reader(StringBuf* sb, const ItemReader& item, int depth, bool raw_text_mode);
static void format_element_reader(StringBuf* sb, const ElementReader& elem, int depth, bool raw_text_mode);

// HTML5 void elements (self-closing tags that should not have closing tags)
static const char* void_elements[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "command",
    "keygen", "menuitem", NULL
};

// HTML5 elements that contain raw text (like script, style)
static const char* raw_text_elements[] = {
    "script", "style", "textarea", "title", "xmp", "iframe", "noembed",
    "noframes", "noscript", "plaintext", NULL
};

// Helper function to check if an element is a void element
static bool is_void_element(const char* tag_name, size_t tag_len) {
    for (int i = 0; void_elements[i]; i++) {
        size_t void_len = strlen(void_elements[i]);
        if (tag_len == void_len && strncasecmp(tag_name, void_elements[i], tag_len) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function to check if an element is a raw text element
static bool is_raw_text_element(const char* tag_name, size_t tag_len) {
    for (int i = 0; raw_text_elements[i]; i++) {
        size_t raw_len = strlen(raw_text_elements[i]);
        if (tag_len == raw_len && strncasecmp(tag_name, raw_text_elements[i], tag_len) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function to check if a type is simple (can be output as HTML attribute)
static bool is_simple_type(TypeId type) {
    return type == LMD_TYPE_STRING || type == LMD_TYPE_INT ||
           type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT ||
           type == LMD_TYPE_BOOL;
}

static void format_html_string(StringBuf* sb, String* str, bool is_attribute) {
    if (!str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];

        // Check if this is an already-encoded entity (starts with & and ends with ;)
        // This prevents double-encoding of entities like &lt; -> &amp;lt;
        if (c == '&') {
            // Look ahead to see if this is an entity reference
            size_t j = i + 1;
            bool is_entity = false;

            // Check for numeric entity: &#123; or &#xAB;
            if (j < len && s[j] == '#') {
                j++;
                if (j < len && (s[j] == 'x' || s[j] == 'X')) {
                    j++; // hex entity
                    while (j < len && ((s[j] >= '0' && s[j] <= '9') ||
                                       (s[j] >= 'a' && s[j] <= 'f') ||
                                       (s[j] >= 'A' && s[j] <= 'F'))) {
                        j++;
                    }
                } else {
                    // decimal entity
                    while (j < len && s[j] >= '0' && s[j] <= '9') {
                        j++;
                    }
                }
                if (j < len && s[j] == ';') {
                    is_entity = true;
                }
            } else {
                // Check for named entity: &nbsp; &lt; &gt; &frac12; etc.
                // Entity names can contain letters and digits
                while (j < len && ((s[j] >= 'a' && s[j] <= 'z') ||
                                   (s[j] >= 'A' && s[j] <= 'Z') ||
                                   (s[j] >= '0' && s[j] <= '9'))) {
                    j++;
                }
                if (j < len && s[j] == ';' && j > i + 1) {
                    is_entity = true;
                }
            }

            if (is_entity) {
                // Copy the entire entity as-is (already encoded)
                while (i <= j && i < len) {
                    stringbuf_append_char(sb, s[i]);
                    i++;
                }
                i--; // Adjust because loop will increment
                continue;
            } else {
                // Not an entity, encode the ampersand
                stringbuf_append_str(sb, "&amp;");
            }
        } else {
            switch (c) {
            case '<':
                stringbuf_append_str(sb, "&lt;");
                break;
            case '>':
                stringbuf_append_str(sb, "&gt;");
                break;
            case '"':
                // Only encode quotes when inside attribute values
                if (is_attribute) {
                    stringbuf_append_str(sb, "&quot;");
                } else {
                    stringbuf_append_char(sb, '"');
                }
                break;
            case '\'':
                // Apostrophes don't need to be encoded in text content (only in attributes)
                // For HTML5, apostrophes in text are safe and don't need encoding
                stringbuf_append_char(sb, '\'');
                break;
            default:
                // Use unsigned char for comparison to handle UTF-8 multibyte sequences correctly
                // UTF-8 continuation bytes (0x80-0xBF) and start bytes (0xC0-0xF7) should pass through
                if ((unsigned char)c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                    // Control characters - encode as numeric character reference
                    char hex_buf[10];
                    snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                    stringbuf_append_str(sb, hex_buf);
                } else {
                    // Pass through as-is (including UTF-8 multibyte sequences)
                    stringbuf_append_char(sb, c);
                }
                break;
            }
        }
    }
}

static void format_indent(StringBuf* sb, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

String* format_html(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // check if root is already an HTML element, if so, format as-is
    if (root_item.item) {
        TypeId type = get_type_id(root_item);

        // handle root-level List (Phase 3: may contain DOCTYPE, comments, and main element)
        if (type == LMD_TYPE_LIST) {
            List* list = root_item.list;
            if (list && list->length > 0) {
                // format each item in the root list using reader API
                for (long i = 0; i < list->length; i++) {
                    Item list_item = list->items[i];
                    ItemReader item_reader(list_item.to_const());
                    format_item_reader(sb, item_reader, 0, false);
                    // add newline after DOCTYPE or comments if not the last item
                    if (i < list->length - 1) {
                        stringbuf_append_char(sb, '\n');
                    }
                }
                return stringbuf_to_string(sb);
            }
        }

        // check if it's an array (most likely case for parsed HTML)
        if (type == LMD_TYPE_ARRAY) {
            Array* arr = root_item.array;
            if (arr && arr->length > 0) {
                // check if the first element is an HTML element
                Item first_item = arr->items[0];
                TypeId first_type = get_type_id(first_item);

                if (first_type == LMD_TYPE_ELEMENT) {
                    Element* element = first_item.element;
                    if (element && element->type) {
                        TypeElmt* elmt_type = (TypeElmt*)element->type;
                        // check if this is an HTML element
                        if (elmt_type->name.length == 4 &&
                            strncmp(elmt_type->name.str, "html", 4) == 0) {
                            // format the HTML element directly without wrapping
                            ItemReader first_reader(first_item.to_const());
                            format_item_reader(sb, first_reader, 0, false);
                            return stringbuf_to_string(sb);
                        }
                    }
                }
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* element = root_item.element;
            if (element && element->type) {
                TypeElmt* elmt_type = (TypeElmt*)element->type;
                // check if this is an HTML element
                if (elmt_type->name.length == 4 &&
                    strncmp(elmt_type->name.str, "html", 4) == 0) {
                    // format the HTML element directly without wrapping
                    ItemReader root_reader(root_item.to_const());
                    format_item_reader(sb, root_reader, 0, false);
                    return stringbuf_to_string(sb);
                }
            }
        }
    }

    // add minimal HTML document structure for non-HTML root elements
    stringbuf_append_str(sb, "<!DOCTYPE html>\n<html>\n<head>");
    stringbuf_append_str(sb, "<meta charset=\"UTF-8\">");
    stringbuf_append_str(sb, "<title>Data</title>");
    stringbuf_append_str(sb, "</head>\n<body>\n");

    ItemReader root_reader(root_item.to_const());
    format_item_reader(sb, root_reader, 0, false);

    stringbuf_append_str(sb, "\n</body>\n</html>");

    return stringbuf_to_string(sb);
}

// Convenience function that formats HTML to a provided StringBuf
void format_html_to_strbuf(StringBuf* sb, Item root_item) {
    ItemReader reader(root_item.to_const());
    format_item_reader(sb, reader, 0, false);
}

// ===== MarkReader-based implementations =====

// format element using reader API
static void format_element_reader(StringBuf* sb, const ElementReader& elem, int depth, bool raw_text_mode) {
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        stringbuf_append_str(sb, "<element/>");
        return;
    }
    
    size_t tag_len = strlen(tag_name);
    
    // special handling for HTML comments (tag name "!--")
    if (tag_len == 3 && memcmp(tag_name, "!--", 3) == 0) {
        // this is a comment element - format as <!--content-->
        stringbuf_append_str(sb, "<!--");
        
        // output comment content (first child text node)
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->chars) {
                // output comment content as-is (no escaping)
                stringbuf_append_format(sb, "%.*s", (int)str->len, str->chars);
            }
        }
        
        stringbuf_append_str(sb, "-->");
        return;
    }
    
    // special handling for DOCTYPE (tag name "!DOCTYPE" or "!doctype")
    if (tag_len >= 8 &&
        (memcmp(tag_name, "!DOCTYPE", 8) == 0 ||
         memcmp(tag_name, "!doctype", 8) == 0)) {
        // this is a DOCTYPE element - format as <!DOCTYPE content>
        stringbuf_append_str(sb, "<!");
        // preserve the case of "DOCTYPE" or "doctype"
        stringbuf_append_format(sb, "%.*s", (int)(tag_len - 1), tag_name + 1);
        
        // output DOCTYPE content (first child text node)
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->chars) {
                // output DOCTYPE content as-is (no escaping)
                stringbuf_append_char(sb, ' ');
                stringbuf_append_format(sb, "%.*s", (int)str->len, str->chars);
            }
        }
        
        stringbuf_append_char(sb, '>');
        return;
    }
    
    // special handling for XML declaration (tag name "?xml")
    if (tag_len == 4 && memcmp(tag_name, "?xml", 4) == 0) {
        // this is an XML declaration - output the stored text directly
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->chars) {
                // output XML declaration as-is
                stringbuf_append_format(sb, "%.*s", (int)str->len, str->chars);
            }
        }
        return;
    }
    
    // format as proper HTML element
    stringbuf_append_char(sb, '<');
    stringbuf_append_str(sb, tag_name);
    
    // add attributes - iterate through element's type shape to get all attributes
    if (elem.element() && elem.element()->type && elem.element()->data) {
        const TypeElmt* elmt_type = (const TypeElmt*)elem.element()->type;
        const TypeMap* map_type = (const TypeMap*)elmt_type;
        const ShapeEntry* field = map_type->shape;
        const void* attr_data = elem.element()->data;
        
        while (field) {
            if (field->name && field->type) {
                const char* field_name = field->name->str;
                int field_name_len = field->name->length;
                
                // skip the "_" field (children)
                if (field_name_len == 1 && field_name[0] == '_') {
                    field = field->next;
                    continue;
                }
                
                const void* data = ((const char*)attr_data) + field->byte_offset;
                TypeId field_type = field->type->type_id;
                
                // add attribute based on type
                if (field_type == LMD_TYPE_BOOL) {
                    // boolean attribute - output name only if true
                    bool bool_val = *(bool*)data;
                    if (bool_val) {
                        stringbuf_append_char(sb, ' ');
                        stringbuf_append_format(sb, "%.*s", field_name_len, field_name);
                    }
                } else if (field_type == LMD_TYPE_STRING || field_type == LMD_TYPE_NULL) {
                    // string attribute or NULL (empty string)
                    String* str = *(String**)data;
                    stringbuf_append_char(sb, ' ');
                    stringbuf_append_format(sb, "%.*s=\"", field_name_len, field_name);
                    if (str && str->chars) {
                        format_html_string(sb, str, true);  // true = is_attribute
                    }
                    stringbuf_append_char(sb, '"');
                }
            }
            field = field->next;
        }
    }
    
    // check if this is a void element (self-closing)
    bool is_void = is_void_element(tag_name, tag_len);
    
    if (is_void) {
        // void elements don't have closing tags in HTML5
        stringbuf_append_char(sb, '>');
    } else {
        stringbuf_append_char(sb, '>');
        
        // check if this is a raw text element (script, style, etc.)
        bool is_raw = is_raw_text_element(tag_name, tag_len);
        
        // add children if available
        auto it = elem.children();
        ItemReader child_item;
        while (it.next(&child_item)) {
            format_item_reader(sb, child_item, depth + 1, is_raw);
        }
        
        // close tag (only for non-void elements)
        stringbuf_append_str(sb, "</");
        stringbuf_append_str(sb, tag_name);
        stringbuf_append_char(sb, '>');
    }
}

// format item using reader API
static void format_item_reader(StringBuf* sb, const ItemReader& item, int depth, bool raw_text_mode) {
    // safety check for null
    if (!sb) return;
    if (item.isNull()) {
        stringbuf_append_str(sb, "null");
        return;
    }
    
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
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%.15g", val);
        stringbuf_append_str(sb, num_buf);
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            if (raw_text_mode) {
                // in raw text mode (script, style, etc.), output string as-is without escaping
                stringbuf_append_format(sb, "%.*s", (int)str->len, str->chars);
            } else {
                // in normal mode, escape HTML entities
                format_html_string(sb, str, false);  // false = text content, not attribute
            }
        }
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        if (!arr.isEmpty()) {
            stringbuf_append_str(sb, "<ul>");
            auto it = arr.items();
            ItemReader array_item;
            while (it.next(&array_item)) {
                stringbuf_append_str(sb, "<li>");
                format_item_reader(sb, array_item, depth + 1, raw_text_mode);
                stringbuf_append_str(sb, "</li>");
            }
            stringbuf_append_str(sb, "</ul>");
        } else {
            stringbuf_append_str(sb, "[]");
        }
    }
    else if (item.isMap()) {
        // simple map representation
        stringbuf_append_str(sb, "<div>{object}</div>");
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(sb, elem, depth, raw_text_mode);
    }
    else {
        stringbuf_append_str(sb, "unknown");
    }
}
