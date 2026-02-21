#include "format.h"
#include "format-markup.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"

// Forward declarations
static void format_mdx_item_reader(StringBuf* sb, const ItemReader& item);
static void format_mdx_element_reader(StringBuf* sb, const ElementReader& elem);

// Format MDX element using MarkReader API
static void format_mdx_element_reader(StringBuf* sb, const ElementReader& elem) {
    const char* tag = elem.tagName();

    if (tag && strcmp(tag, "jsx_element") == 0) {
        // JSX element: extract "content" attribute and output directly
        ItemReader content = elem.get_attr("content");
        if (content.isString()) {
            String* jsx_content = content.asString();
            if (jsx_content && jsx_content->chars) {
                stringbuf_append_str(sb, jsx_content->chars);
            }
        }
    } else if (tag && strcmp(tag, "mdx_document") == 0) {
        // Document root: format all children
        for (int64_t i = 0; i < elem.childCount(); i++) {
            format_mdx_item_reader(sb, elem.childAt(i));
        }
    } else {
        // Non-JSX, non-document element: delegate to markdown formatter
        format_markup(sb, (Item){.element = (Element*)elem.element()}, &MARKDOWN_RULES);
    }
}

// Format MDX item (string or element)
static void format_mdx_item_reader(StringBuf* sb, const ItemReader& item) {
    if (item.isNull()) return;

    if (item.isString()) {
        String* text = item.asString();
        if (text && text->len > 0) {
            stringbuf_append_str(sb, text->chars);
        }
    } else if (item.isElement()) {
        format_mdx_element_reader(sb, item.asElement());
    }
}

// Main MDX formatting function
String* format_mdx(Pool* pool, Item root_item) {
    if (root_item.item == ITEM_NULL) return nullptr;

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return nullptr;

    ItemReader root(root_item.to_const());

    if (root.isElement()) {
        ElementReader elem = root.asElement();
        const char* tag = elem.tagName();

        if (tag && strcmp(tag, "mdx_document") == 0) {
            format_mdx_element_reader(sb, elem);
        } else {
            format_markup(sb, root_item, &MARKDOWN_RULES);
        }
    } else {
        format_mdx_item_reader(sb, root);
    }

    return stringbuf_to_string(sb);
}
