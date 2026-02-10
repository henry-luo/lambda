#include "format.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"

void print_named_items(StringBuf *strbuf, TypeMap *map_type, void* map_data);

// MarkReader-based forward declarations using HtmlContext
static void format_item_reader(HtmlContext& ctx, const ItemReader& item, int depth, bool raw_text_mode);
static void format_element_reader(HtmlContext& ctx, const ElementReader& elem, int depth, bool raw_text_mode);

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

// HTML5 boolean attributes (attributes that can appear without a value)
// See: https://html.spec.whatwg.org/multipage/indices.html#attributes-3
static const char* boolean_attributes[] = {
    "async", "autofocus", "autoplay", "checked", "controls", "default",
    "defer", "disabled", "formnovalidate", "hidden", "ismap", "loop",
    "multiple", "muted", "nomodule", "novalidate", "open", "playsinline",
    "readonly", "required", "reversed", "selected", NULL
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

// Helper function to check if an attribute is a boolean attribute
static bool is_boolean_attribute(const char* attr_name, size_t attr_len) {
    for (int i = 0; boolean_attributes[i]; i++) {
        size_t bool_len = strlen(boolean_attributes[i]);
        if (attr_len == bool_len && strncasecmp(attr_name, boolean_attributes[i], attr_len) == 0) {
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

static void format_indent(HtmlContext& ctx, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(ctx.output(), "  ");
    }
}

String* format_html(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    // Create HTML context
    Pool* ctx_pool = pool_create();
    HtmlContext ctx(ctx_pool, sb);

    // check if root is already an HTML element or #document, if so, format as-is
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
                    format_item_reader(ctx, item_reader, 0, false);
                    // add newline after DOCTYPE or comments if not the last item
                    if (i < list->length - 1) {
                        stringbuf_append_char(ctx.output(), '\n');
                    }
                }
                pool_destroy(ctx_pool);
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
                        // check if this is an HTML or #document element
                        if ((elmt_type->name.length == 4 &&
                             strncmp(elmt_type->name.str, "html", 4) == 0) ||
                            (elmt_type->name.length == 9 &&
                             strncmp(elmt_type->name.str, "#document", 9) == 0)) {
                            // format the element directly without wrapping
                            ItemReader first_reader(first_item.to_const());
                            format_item_reader(ctx, first_reader, 0, false);
                            pool_destroy(ctx_pool);
                            return stringbuf_to_string(sb);
                        }
                    }
                }
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* element = root_item.element;
            if (element && element->type) {
                TypeElmt* elmt_type = (TypeElmt*)element->type;
                // check if this is an HTML or #document element
                if ((elmt_type->name.length == 4 &&
                     strncmp(elmt_type->name.str, "html", 4) == 0) ||
                    (elmt_type->name.length == 9 &&
                     strncmp(elmt_type->name.str, "#document", 9) == 0)) {
                    // format the element directly without wrapping
                    ItemReader root_reader(root_item.to_const());
                    format_item_reader(ctx, root_reader, 0, false);
                    pool_destroy(ctx_pool);
                    return stringbuf_to_string(sb);
                }
            }
        }
    }

    // add minimal HTML document structure for non-HTML root elements
    stringbuf_append_str(ctx.output(), "<!DOCTYPE html>\n<html>\n<head>");
    stringbuf_append_str(ctx.output(), "<meta charset=\"UTF-8\">");
    stringbuf_append_str(ctx.output(), "<title>Data</title>");
    stringbuf_append_str(ctx.output(), "</head>\n<body>\n");

    ItemReader root_reader(root_item.to_const());
    format_item_reader(ctx, root_reader, 0, false);

    stringbuf_append_str(ctx.output(), "\n</body>\n</html>");

    pool_destroy(ctx_pool);
    return stringbuf_to_string(sb);
}

// Convenience function that formats HTML to a provided StringBuf
void format_html_to_strbuf(StringBuf* sb, Item root_item) {
    Pool* pool = pool_create();
    HtmlContext ctx(pool, sb);
    ItemReader reader(root_item.to_const());
    format_item_reader(ctx, reader, 0, false);
    pool_destroy(pool);
}

// ===== MarkReader-based implementations =====

// format element using reader API
static void format_element_reader(HtmlContext& ctx, const ElementReader& elem, int depth, bool raw_text_mode) {
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        stringbuf_append_str(ctx.output(), "<element/>");
        return;
    }

    size_t tag_len = strlen(tag_name);

    // special handling for #document (HTML5 parser document root)
    // output children directly without any wrapper tags
    if (tag_len == 9 && memcmp(tag_name, "#document", 9) == 0) {
        auto it = elem.children();
        ItemReader child_item;
        while (it.next(&child_item)) {
            format_item_reader(ctx, child_item, depth, false);
        }
        return;
    }

    // special handling for #doctype (HTML5 parser DOCTYPE element)
    // format as <!DOCTYPE name>
    if (tag_len == 8 && memcmp(tag_name, "#doctype", 8) == 0) {
        stringbuf_append_str(ctx.output(), "<!DOCTYPE ");
        // get the "name" attribute
        ItemReader name_attr = elem.get_attr("name");
        if (name_attr.isString()) {
            String* str = name_attr.asString();
            if (str && str->chars) {
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            }
        } else {
            // default to "html" if no name specified
            stringbuf_append_str(ctx.output(), "html");
        }
        stringbuf_append_char(ctx.output(), '>');
        return;
    }

    // special handling for #comment (HTML5 parser comment element)
    // format as <!--data-->
    if (tag_len == 8 && memcmp(tag_name, "#comment", 8) == 0) {
        stringbuf_append_str(ctx.output(), "<!--");
        // get the "data" attribute
        ItemReader data_attr = elem.get_attr("data");
        if (data_attr.isString()) {
            String* str = data_attr.asString();
            if (str && str->chars) {
                // output comment content as-is (no escaping)
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            }
        }
        stringbuf_append_str(ctx.output(), "-->");
        return;
    }

    // special handling for HTML comments (tag name "!--") - old parser format
    if (tag_len == 3 && memcmp(tag_name, "!--", 3) == 0) {
        // this is a comment element - format as <!--content-->
        stringbuf_append_str(ctx.output(), "<!--");

        // output comment content (first child text node)
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->chars) {
                // output comment content as-is (no escaping)
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            }
        }

        stringbuf_append_str(ctx.output(), "-->");
        return;
    }

    // special handling for DOCTYPE (tag name "!DOCTYPE" or "!doctype")
    if (tag_len >= 8 &&
        (memcmp(tag_name, "!DOCTYPE", 8) == 0 ||
         memcmp(tag_name, "!doctype", 8) == 0)) {
        // this is a DOCTYPE element - format as <!DOCTYPE content>
        stringbuf_append_str(ctx.output(), "<!");
        // preserve the case of "DOCTYPE" or "doctype"
        stringbuf_append_format(ctx.output(), "%.*s", (int)(tag_len - 1), tag_name + 1);

        // output DOCTYPE content (first child text node)
        ItemReader first_child = elem.childAt(0);
        if (first_child.isString()) {
            String* str = first_child.asString();
            if (str && str->chars) {
                // output DOCTYPE content as-is (no escaping)
                stringbuf_append_char(ctx.output(), ' ');
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            }
        }

        stringbuf_append_char(ctx.output(), '>');
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
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            }
        }
        return;
    }

    // special handling for raw HTML blocks from markdown parser
    // Element name "html" with type="raw" attribute - output content verbatim
    if (tag_len == 4 && memcmp(tag_name, "html", 4) == 0) {
        ItemReader type_attr = elem.get_attr("type");
        if (type_attr.isString()) {
            String* type_str = type_attr.asString();
            if (type_str && type_str->len == 3 && memcmp(type_str->chars, "raw", 3) == 0) {
                // Output raw HTML content directly, no escaping
                ItemReader first_child = elem.childAt(0);
                if (first_child.isString()) {
                    String* str = first_child.asString();
                    if (str && str->chars) {
                        stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
                    }
                }
                stringbuf_append_char(ctx.output(), '\n');
                return;
            }
        }
    }

    // format as proper HTML element
    stringbuf_append_char(ctx.output(), '<');
    stringbuf_append_str(ctx.output(), tag_name);

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
                        stringbuf_append_char(ctx.output(), ' ');
                        stringbuf_append_format(ctx.output(), "%.*s", field_name_len, field_name);
                    }
                } else if (field_type == LMD_TYPE_STRING || field_type == LMD_TYPE_NULL) {
                    // string attribute or NULL (empty string)
                    String* str = *(String**)data;
                    stringbuf_append_char(ctx.output(), ' ');
                    stringbuf_append_format(ctx.output(), "%.*s", field_name_len, field_name);
                    // HTML5 boolean attributes can be output without ="value" if empty
                    // Regular attributes should always have ="value" even if empty
                    bool is_bool_attr = is_boolean_attribute(field_name, field_name_len);
                    if (str && str->len > 0) {
                        stringbuf_append_char(ctx.output(), '=');
                        stringbuf_append_char(ctx.output(), '"');
                        format_html_string_safe(ctx.output(), str, true);  // true = is_attribute
                        stringbuf_append_char(ctx.output(), '"');
                    } else if (!is_bool_attr) {
                        // Non-boolean attribute with empty value: output =""
                        stringbuf_append_str(ctx.output(), "=\"\"");
                    }
                    // Boolean attribute with empty value: output just the name (no ="" part)
                }
            }
            field = field->next;
        }
    }

    // check if this is a void element (self-closing)
    bool is_void = is_void_element(tag_name, tag_len);

    if (is_void) {
        // void elements don't have closing tags in HTML5
        stringbuf_append_char(ctx.output(), '>');
    } else {
        stringbuf_append_char(ctx.output(), '>');

        // check if this is a raw text element (script, style, etc.)
        bool is_raw = is_raw_text_element(tag_name, tag_len);

        // add children if available
        auto it = elem.children();
        ItemReader child_item;
        while (it.next(&child_item)) {
            format_item_reader(ctx, child_item, depth + 1, is_raw);
        }

        // close tag (only for non-void elements)
        stringbuf_append_str(ctx.output(), "</");
        stringbuf_append_str(ctx.output(), tag_name);
        stringbuf_append_char(ctx.output(), '>');
    }
}

// format item using reader API
static void format_item_reader(HtmlContext& ctx, const ItemReader& item, int depth, bool raw_text_mode) {
    // safety check for null
    if (!ctx.output()) return;
    if (item.isNull()) {
        stringbuf_append_str(ctx.output(), "null");
        return;
    }

    if (item.isBool()) {
        bool val = item.asBool();
        stringbuf_append_str(ctx.output(), val ? "true" : "false");
    }
    else if (item.isInt()) {
        int val = item.asInt();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%d", val);
        stringbuf_append_str(ctx.output(), num_buf);
    }
    else if (item.isFloat()) {
        double val = item.asFloat();
        char num_buf[32];
        snprintf(num_buf, sizeof(num_buf), "%.15g", val);
        stringbuf_append_str(ctx.output(), num_buf);
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) {
            if (raw_text_mode) {
                // in raw text mode (script, style, etc.), output string as-is without escaping
                stringbuf_append_format(ctx.output(), "%.*s", (int)str->len, str->chars);
            } else {
                // in normal mode, escape HTML entities
                format_html_string_safe(ctx.output(), str, false);  // false = text content, not attribute
            }
        }
    }
    else if (item.isSymbol()) {
        // Symbol items represent HTML entities like &copy;, &mdash;, etc.
        // Format them back as entity references for proper roundtrip
        Symbol* sym = item.asSymbol();
        if (sym && sym->chars) {
            stringbuf_append_char(ctx.output(), '&');
            stringbuf_append_format(ctx.output(), "%.*s", (int)sym->len, sym->chars);
            stringbuf_append_char(ctx.output(), ';');
        }
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        if (!arr.isEmpty()) {
            stringbuf_append_str(ctx.output(), "<ul>");
            auto it = arr.items();
            ItemReader array_item;
            while (it.next(&array_item)) {
                stringbuf_append_str(ctx.output(), "<li>");
                format_item_reader(ctx, array_item, depth + 1, raw_text_mode);
                stringbuf_append_str(ctx.output(), "</li>");
            }
            stringbuf_append_str(ctx.output(), "</ul>");
        } else {
            stringbuf_append_str(ctx.output(), "[]");
        }
    }
    else if (item.isMap()) {
        // simple map representation
        stringbuf_append_str(ctx.output(), "<div>{object}</div>");
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(ctx, elem, depth, raw_text_mode);
    }
    else {
        stringbuf_append_str(ctx.output(), "unknown");
    }
}
