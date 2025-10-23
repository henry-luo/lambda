#include "format.h"
#include "../../lib/stringbuf.h"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

static void format_item(StringBuf* sb, Item item, int depth);

// HTML5 void elements (self-closing tags that should not have closing tags)
static const char* void_elements[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "command",
    "keygen", "menuitem", NULL
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

// Helper function to check if a type is simple (can be output as HTML attribute)
static bool is_simple_type(TypeId type) {
    return type == LMD_TYPE_STRING || type == LMD_TYPE_INT ||
           type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT ||
           type == LMD_TYPE_BOOL;
}

static void format_html_string(StringBuf* sb, String* str) {
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
            stringbuf_append_str(sb, "&amp;");
            break;
        case '"':
            stringbuf_append_str(sb, "&quot;");
            break;
        case '\'':
            stringbuf_append_str(sb, "&#39;");
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

static void format_indent(StringBuf* sb, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(sb, "  ");
    }
}

static void format_array(StringBuf* sb, Array* arr, int depth) {
    if (arr && arr->length > 0) {
        stringbuf_append_str(sb, "\n");
        format_indent(sb, depth + 1);
        stringbuf_append_str(sb, "<ul>\n");

        for (long i = 0; i < arr->length; i++) {
            format_indent(sb, depth + 2);
            stringbuf_append_str(sb, "<li>");
            Item item = arr->items[i];
            format_item(sb, item, depth + 2);
            stringbuf_append_str(sb, "</li>\n");
        }

        format_indent(sb, depth + 1);
        stringbuf_append_str(sb, "</ul>\n");
        format_indent(sb, depth);
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
                stringbuf_append_format(sb, "data-%.*s=\"", (int)field->name->length, field->name->str);

                if (field_type == LMD_TYPE_STRING) {
                    String* str = *(String**)data;
                    if (str) {
                        format_html_string(sb, str);
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

                stringbuf_append_char(sb, '"');
            }
        }
        field = field->next;
    }
}

static void format_map_elements(StringBuf* sb, TypeMap* map_type, void* map_data, int depth) {
    if (!map_type || !map_data || !map_type->shape) return;

    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name && field->type) {
            void* data = ((char*)map_data) + field->byte_offset;

            TypeId field_type = field->type->type_id;

            // Only handle complex types as child elements (simple types are attributes)
            if (!is_simple_type(field_type)) {
                stringbuf_append_str(sb, "\n");
                format_indent(sb, depth + 1);

                if (field_type == LMD_TYPE_NULL) {
                    // Create a div for null
                    stringbuf_append_format(sb, "<div class=\"field-%.*s\">null</div>",
                                        (int)field->name->length, field->name->str);
                } else {
                    // For complex types, create a div with class name
                    stringbuf_append_format(sb, "<div class=\"field-%.*s\">",
                                        (int)field->name->length, field->name->str);

                    Item item_data = *(Item*)data;
                    format_item(sb, item_data, depth + 1);

                    stringbuf_append_str(sb, "</div>");
                }
            }
        }
        field = field->next;
    }
}

static void format_map(StringBuf* sb, Map* mp, int depth) {
    stringbuf_append_str(sb, "<div class=\"object\"");

    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        // Add simple types as data attributes
        format_map_attributes(sb, map_type, mp->data);
    }

    stringbuf_append_char(sb, '>');

    if (mp && mp->type) {
        TypeMap* map_type = (TypeMap*)mp->type;
        // Add complex types as child elements
        format_map_elements(sb, map_type, mp->data, depth);
    }

    stringbuf_append_str(sb, "\n");
    format_indent(sb, depth);
    stringbuf_append_str(sb, "</div>");
}

static void format_item(StringBuf* sb, Item item, int depth) {
    // Safety check for null pointer
    if (!sb) {
        return;
    }

    // Check if item is null (0)
    if (!item.item) {
        stringbuf_append_str(sb, "null");
        return;
    }

    // Additional safety check for Item structure
    if (item.type_id == 0 && item.raw_pointer == NULL) {
        stringbuf_append_str(sb, "null");
        return;
    }

    TypeId type = get_type_id(item);

    switch (type) {
    case LMD_TYPE_NULL:
        stringbuf_append_str(sb, "null");
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
    case LMD_TYPE_STRING: {
        String* str = (String*)item.pointer;
        if (str) {
            format_html_string(sb, str);
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = item.array;
        if (arr && arr->length > 0) {
            stringbuf_append_str(sb, "<ul>");
            for (long i = 0; i < arr->length; i++) {
                stringbuf_append_str(sb, "<li>");
                Item array_item = arr->items[i];
                format_item(sb, array_item, depth + 1);
                stringbuf_append_str(sb, "</li>");
            }
            stringbuf_append_str(sb, "</ul>");
        } else {
            stringbuf_append_str(sb, "[]");
        }
        break;
    }
    case LMD_TYPE_MAP: {
        Map* mp = item.map;
        if (mp && mp->type) {
            stringbuf_append_str(sb, "<div>");
            // Simple map representation
            stringbuf_append_str(sb, "{object}");
            stringbuf_append_str(sb, "</div>");
        } else {
            stringbuf_append_str(sb, "{}");
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* element = item.element;
        if (element && element->type) {
            TypeElmt* elmt_type = (TypeElmt*)element->type;

            // Format as proper HTML element
            stringbuf_append_char(sb, '<');
            stringbuf_append_format(sb, "%.*s", (int)elmt_type->name.length, elmt_type->name.str);

            // Add attributes if available
            if (elmt_type && elmt_type->length > 0 && element->data) {
                TypeMap* map_type = (TypeMap*)elmt_type;
                ShapeEntry* field = map_type->shape;
                for (int i = 0; i < map_type->length && field; i++) {
                    if (field->name && field->type) {
                        void* data = ((char*)element->data) + field->byte_offset;
                        TypeId field_type = field->type->type_id;

                        // Check if this is a known HTML attribute
                        const char* field_name = field->name->str;
                        int field_name_len = field->name->length;

                        // Skip the "_" field (children)
                        if (field_name_len == 1 && field_name[0] == '_') {
                            field = field->next;
                            continue;
                        }

                        // Add attribute
                        if (field_type == LMD_TYPE_STRING) {
                            String* str = *(String**)data;
                            if (str && str->chars) {
                                stringbuf_append_char(sb, ' ');
                                stringbuf_append_format(sb, "%.*s=\"", field_name_len, field_name);
                                format_html_string(sb, str);
                                stringbuf_append_char(sb, '"');
                            }
                        }
                    }
                    field = field->next;
                }
            }

            // Check if this is a void element (self-closing)
            bool is_void = is_void_element(elmt_type->name.str, elmt_type->name.length);

            if (is_void) {
                // Void elements don't have closing tags in HTML5
                stringbuf_append_char(sb, '>');
            } else {
                stringbuf_append_char(sb, '>');

                // Add children if available (same approach as JSON formatter)
                if (elmt_type && elmt_type->content_length > 0) {
                    List* list = (List*)element;
                    for (long j = 0; j < list->length; j++) {
                        Item child_item = list->items[j];
                        format_item(sb, child_item, depth + 1);
                    }
                }

                // Close tag (only for non-void elements)
                stringbuf_append_str(sb, "</");
                stringbuf_append_format(sb, "%.*s", (int)elmt_type->name.length, elmt_type->name.str);
                stringbuf_append_char(sb, '>');
            }
        } else {
            stringbuf_append_str(sb, "<element/>");
        }
        break;
    }
    default:
        stringbuf_append_str(sb, "unknown");
        break;
    }
}

String* format_html(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Check if root is already an HTML element, if so, format as-is
    if (root_item.item) {
        TypeId type = get_type_id(root_item);

        // Check if it's an array (most likely case for parsed HTML)
        if (type == LMD_TYPE_ARRAY) {
            Array* arr = root_item.array;
            if (arr && arr->length > 0) {
                // Check if the first element is an HTML element
                Item first_item = arr->items[0];
                TypeId first_type = get_type_id(first_item);

                if (first_type == LMD_TYPE_ELEMENT) {
                    Element* element = first_item.element;
                    if (element && element->type) {
                        TypeElmt* elmt_type = (TypeElmt*)element->type;
                        // Check if this is an HTML element
                        if (elmt_type->name.length == 4 &&
                            strncmp(elmt_type->name.str, "html", 4) == 0) {
                            // Format the HTML element directly without wrapping
                            stringbuf_append_str(sb, "<!DOCTYPE html>\n");
                            format_item(sb, first_item, 0);
                            return stringbuf_to_string(sb);
                        }
                    }
                }
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* element = root_item.element;
            if (element && element->type) {
                TypeElmt* elmt_type = (TypeElmt*)element->type;
                // Check if this is an HTML element
                if (elmt_type->name.length == 4 &&
                    strncmp(elmt_type->name.str, "html", 4) == 0) {
                    // Format the HTML element directly without wrapping
                    stringbuf_append_str(sb, "<!DOCTYPE html>\n");
                    format_item(sb, root_item, 0);
                    return stringbuf_to_string(sb);
                }
            }
        }
    }

    // Add minimal HTML document structure for non-HTML root elements
    stringbuf_append_str(sb, "<!DOCTYPE html>\n<html>\n<head>");
    stringbuf_append_str(sb, "<meta charset=\"UTF-8\">");
    stringbuf_append_str(sb, "<title>Data</title>");
    stringbuf_append_str(sb, "</head>\n<body>\n");

    format_item(sb, root_item, 0);

    stringbuf_append_str(sb, "\n</body>\n</html>");

    return stringbuf_to_string(sb);
}

// Convenience function that formats HTML to a provided StringBuf
void format_html_to_strbuf(StringBuf* sb, Item root_item) {
    format_item(sb, root_item, 0);
}
