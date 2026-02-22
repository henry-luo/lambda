#include "format.h"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <ctype.h>

// Forward declarations
static void format_jsx_element(StringBuf* sb, const ElementReader& elem);
static void format_jsx_item(StringBuf* sb, const ItemReader& item);

// ---------------------------------------------------------------------------
// Text and attribute value helpers (gen-neutral, operate on String*)
// ---------------------------------------------------------------------------

// Format JSX text content with proper escaping
static void format_jsx_text_content(StringBuf* sb, String* text) {
    if (!text || text->len == 0 || text->len > 10000) return;

    for (int i = 0; i < text->len; i++) {
        char c = text->chars[i];
        switch (c) {
            case '<': stringbuf_append_str(sb, "&lt;");   break;
            case '>': stringbuf_append_str(sb, "&gt;");   break;
            case '&': stringbuf_append_str(sb, "&amp;");  break;
            case '{': stringbuf_append_str(sb, "&#123;"); break;
            case '}': stringbuf_append_str(sb, "&#125;"); break;
            default:  stringbuf_append_char(sb, c);       break;
        }
    }
}

// Format JSX attribute value with escaping
static void format_jsx_attribute_value(StringBuf* sb, String* value) {
    if (!value || value->len == 0) return;

    stringbuf_append_char(sb, '"');
    for (int i = 0; i < value->len; i++) {
        char c = value->chars[i];
        switch (c) {
            case '"': stringbuf_append_str(sb, "&quot;"); break;
            case '&': stringbuf_append_str(sb, "&amp;");  break;
            case '<': stringbuf_append_str(sb, "&lt;");   break;
            case '>': stringbuf_append_str(sb, "&gt;");   break;
            default:  stringbuf_append_char(sb, c);       break;
        }
    }
    stringbuf_append_char(sb, '"');
}

// ---------------------------------------------------------------------------
// Attribute formatting (uses shape-level access via elem.element())
// ---------------------------------------------------------------------------

// Check if an element tagged "js" and format as {expression}
static bool try_format_js_expr(StringBuf* sb, const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag || strcmp(tag, "js") != 0) return false;

    stringbuf_append_char(sb, '{');
    if (elem.childCount() > 0) {
        ItemReader first = elem.childAt(0);
        if (first.isString()) {
            String* js = first.asString();
            if (js && js->len > 0 && js->len < 10000) {
                stringbuf_append_str(sb, js->chars);
            }
        }
    }
    stringbuf_append_char(sb, '}');
    return true;
}

// Format JSX attributes from element shape data
// NOTE: uses raw Element* via elem.element() because ElementReader
// lacks a generic attribute iterator. All other code is Gen3.
static void format_jsx_attributes(StringBuf* sb, const ElementReader& elem) {
    const Element* raw = elem.element();
    if (!raw || !raw->data) return;

    TypeElmt* elem_type = (TypeElmt*)raw->type;
    if (!elem_type) return;

    TypeMap* map_type = (TypeMap*)elem_type;
    if (!map_type->shape) return;

    ShapeEntry* field = map_type->shape;
    for (int i = 0; i < map_type->length && field; i++) {
        if (field->name) {
            const char* attr_name = field->name->str;

            // Skip internal markers
            if (strcmp(attr_name, "is_component") == 0 || strcmp(attr_name, "self_closing") == 0) {
                field = field->next;
                continue;
            }

            // Skip internal JSX type marker
            if (strcmp(attr_name, "type") == 0) {
                void* data = ((char*)raw->data) + field->byte_offset;
                if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                    String* type_value = *(String**)data;
                    if (type_value && strcmp(type_value->chars, "jsx_element") == 0) {
                        field = field->next;
                        continue;
                    }
                }
            }

            stringbuf_append_format(sb, " %s", attr_name);

            void* data = ((char*)raw->data) + field->byte_offset;

            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                String* attr_value = *(String**)data;
                if (attr_value && strcmp(attr_value->chars, "true") != 0) {
                    stringbuf_append_char(sb, '=');
                    format_jsx_attribute_value(sb, attr_value);
                }
            } else if (field->type && field->type->type_id == LMD_TYPE_ELEMENT) {
                Element* expr_elem = *(Element**)data;
                if (expr_elem) {
                    ElementReader expr(expr_elem);
                    if (expr.tagName() && strcmp(expr.tagName(), "js") == 0) {
                        stringbuf_append_char(sb, '=');
                        try_format_js_expr(sb, expr);
                    }
                }
            }
        }
        field = field->next;
    }
}

// ---------------------------------------------------------------------------
// Element dispatch
// ---------------------------------------------------------------------------

static void format_jsx_element(StringBuf* sb, const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return;

    // JSX fragment: <>...</>
    if (strcmp(tag, "jsx_fragment") == 0) {
        stringbuf_append_str(sb, "<>");
        for (int64_t i = 0; i < elem.childCount(); i++)
            format_jsx_item(sb, elem.childAt(i));
        stringbuf_append_str(sb, "</>");
        return;
    }

    // JS expression: {expr}
    if (try_format_js_expr(sb, elem)) return;

    // Regular JSX element
    stringbuf_append_format(sb, "<%s", tag);

    format_jsx_attributes(sb, elem);

    // Self-closing check
    ItemReader sc = elem.get_attr("self_closing");
    if (sc.isString()) {
        String* scv = sc.asString();
        if (scv && strcmp(scv->chars, "true") == 0) {
            stringbuf_append_str(sb, " />");
            return;
        }
    }

    stringbuf_append_char(sb, '>');

    // Children
    for (int64_t i = 0; i < elem.childCount(); i++)
        format_jsx_item(sb, elem.childAt(i));

    // Closing tag
    stringbuf_append_format(sb, "</%s>", tag);
}

// ---------------------------------------------------------------------------
// Item dispatch
// ---------------------------------------------------------------------------

static void format_jsx_item(StringBuf* sb, const ItemReader& item) {
    if (item.isNull()) return;

    if (item.isString()) {
        format_jsx_text_content(sb, item.asString());
    } else if (item.isElement()) {
        format_jsx_element(sb, item.asElement());
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

String* format_jsx(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    ItemReader root(root_item.to_const());
    format_jsx_item(sb, root);
    return stringbuf_to_string(sb);
}
