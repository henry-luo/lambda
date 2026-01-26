/**
 * CommonMark HTML Formatter
 *
 * This formatter converts parsed Mark AST (from markdown input) to CommonMark-style
 * HTML fragments for comparison against the official CommonMark spec test suite.
 *
 * Unlike the standard HTML formatter which outputs complete documents, this formatter
 * produces bare HTML fragments matching CommonMark's expected output format.
 */

#ifndef COMMONMARK_HTML_FORMATTER_HPP
#define COMMONMARK_HTML_FORMATTER_HPP

#include <string>
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"
#include "../../lib/stringbuf.h"

// CommonMark HTML formatting context
class CommonMarkHtmlContext {
public:
    CommonMarkHtmlContext(Pool* pool) : pool_(pool) {
        sb_ = stringbuf_new(pool);
    }

    ~CommonMarkHtmlContext() {
        // Don't free sb_ - it's managed by pool
    }

    StringBuf* output() { return sb_; }
    Pool* pool() { return pool_; }

    // Get result as std::string
    std::string result() {
        if (!sb_) return "";
        String* str = stringbuf_to_string(sb_);
        if (!str) return "";
        return std::string(str->chars, str->len);
    }

private:
    Pool* pool_;
    StringBuf* sb_;
};

// Forward declarations
static void format_cm_item(CommonMarkHtmlContext& ctx, const ItemReader& item);
static void format_cm_element(CommonMarkHtmlContext& ctx, const ElementReader& elem);
static void format_cm_children(CommonMarkHtmlContext& ctx, const ElementReader& elem);
static void format_cm_text(CommonMarkHtmlContext& ctx, const char* text, size_t len);

// HTML entity encoding for text content
static void format_cm_text(CommonMarkHtmlContext& ctx, const char* text, size_t len) {
    StringBuf* sb = ctx.output();
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
            case '<': stringbuf_append_str(sb, "&lt;"); break;
            case '>': stringbuf_append_str(sb, "&gt;"); break;
            case '&': stringbuf_append_str(sb, "&amp;"); break;
            case '"': stringbuf_append_str(sb, "&quot;"); break;
            default: stringbuf_append_char(sb, c); break;
        }
    }
}

// Format raw text (no entity encoding)
static void format_cm_raw_text(CommonMarkHtmlContext& ctx, const char* text, size_t len) {
    StringBuf* sb = ctx.output();
    stringbuf_append_str_n(sb, text, len);
}

// Format element children
static void format_cm_children(CommonMarkHtmlContext& ctx, const ElementReader& elem) {
    for (int i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        format_cm_item(ctx, child);
    }
}

// Format text children, joining them without HTML structure
static void format_cm_text_children(CommonMarkHtmlContext& ctx, const ElementReader& elem) {
    for (int i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            String* str = child.asString();
            if (str && str->chars) {
                format_cm_text(ctx, str->chars, str->len);
            }
        } else if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            format_cm_element(ctx, child_elem);
        }
    }
}

// Format a single element
static void format_cm_element(CommonMarkHtmlContext& ctx, const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return;

    StringBuf* sb = ctx.output();

    // Handle different element types based on tag name
    // Block elements
    if (strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
        strcmp(tag, "h4") == 0 || strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {
        // Heading - check for level attribute
        String* level_attr = elem.get_string_attr("level");
        int level = 1;
        if (level_attr && level_attr->len > 0) {
            level = atoi(level_attr->chars);
            if (level < 1) level = 1;
            if (level > 6) level = 6;
        } else if (strlen(tag) == 2 && tag[0] == 'h') {
            level = tag[1] - '0';
        }

        char open_tag[8], close_tag[8];
        snprintf(open_tag, sizeof(open_tag), "<h%d>", level);
        snprintf(close_tag, sizeof(close_tag), "</h%d>", level);

        stringbuf_append_str(sb, open_tag);
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, close_tag);
        stringbuf_append_char(sb, '\n');
    }
    else if (strcmp(tag, "p") == 0) {
        stringbuf_append_str(sb, "<p>");
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, "</p>\n");
    }
    else if (strcmp(tag, "pre") == 0 || strcmp(tag, "code_block") == 0 ||
             strcmp(tag, "fenced_code") == 0 || strcmp(tag, "indented_code") == 0) {
        stringbuf_append_str(sb, "<pre><code>");
        // Code blocks preserve exact content
        for (int i = 0; i < elem.childCount(); i++) {
            ItemReader child = elem.childAt(i);
            if (child.isString()) {
                String* str = child.asString();
                if (str && str->chars) {
                    format_cm_text(ctx, str->chars, str->len);
                }
            }
        }
        stringbuf_append_str(sb, "</code></pre>\n");
    }
    else if (strcmp(tag, "code") == 0) {
        // Check if this is a block-level code (has code attribute/property)
        bool is_block = elem.get_string_attr("info") != nullptr ||
                        elem.get_string_attr("language") != nullptr;

        if (is_block) {
            String* info = elem.get_string_attr("info");
            if (info && info->len > 0) {
                stringbuf_append_str(sb, "<pre><code class=\"language-");
                format_cm_text(ctx, info->chars, info->len);
                stringbuf_append_str(sb, "\">");
            } else {
                stringbuf_append_str(sb, "<pre><code>");
            }
            for (int i = 0; i < elem.childCount(); i++) {
                ItemReader child = elem.childAt(i);
                if (child.isString()) {
                    String* str = child.asString();
                    if (str && str->chars) {
                        format_cm_text(ctx, str->chars, str->len);
                    }
                }
            }
            stringbuf_append_str(sb, "</code></pre>\n");
        } else {
            // Inline code
            stringbuf_append_str(sb, "<code>");
            for (int i = 0; i < elem.childCount(); i++) {
                ItemReader child = elem.childAt(i);
                if (child.isString()) {
                    String* str = child.asString();
                    if (str && str->chars) {
                        format_cm_text(ctx, str->chars, str->len);
                    }
                }
            }
            stringbuf_append_str(sb, "</code>");
        }
    }
    else if (strcmp(tag, "blockquote") == 0) {
        stringbuf_append_str(sb, "<blockquote>\n");
        format_cm_children(ctx, elem);
        stringbuf_append_str(sb, "</blockquote>\n");
    }
    else if (strcmp(tag, "ul") == 0) {
        stringbuf_append_str(sb, "<ul>\n");
        format_cm_children(ctx, elem);
        stringbuf_append_str(sb, "</ul>\n");
    }
    else if (strcmp(tag, "ol") == 0) {
        String* start = elem.get_string_attr("start");
        if (start && start->len > 0 && strcmp(start->chars, "1") != 0) {
            stringbuf_append_str(sb, "<ol start=\"");
            format_cm_raw_text(ctx, start->chars, start->len);
            stringbuf_append_str(sb, "\">\n");
        } else {
            stringbuf_append_str(sb, "<ol>\n");
        }
        format_cm_children(ctx, elem);
        stringbuf_append_str(sb, "</ol>\n");
    }
    else if (strcmp(tag, "li") == 0) {
        stringbuf_append_str(sb, "<li>");

        // Check if li contains only text/inline or block elements
        bool has_block = false;
        for (int i = 0; i < elem.childCount(); i++) {
            ItemReader child = elem.childAt(i);
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && (strcmp(child_tag, "p") == 0 ||
                                  strcmp(child_tag, "ul") == 0 ||
                                  strcmp(child_tag, "ol") == 0 ||
                                  strcmp(child_tag, "blockquote") == 0)) {
                    has_block = true;
                    break;
                }
            }
        }

        if (has_block) {
            stringbuf_append_char(sb, '\n');
            format_cm_children(ctx, elem);
        } else {
            format_cm_text_children(ctx, elem);
        }

        stringbuf_append_str(sb, "</li>\n");
    }
    else if (strcmp(tag, "hr") == 0 || strcmp(tag, "thematic_break") == 0) {
        stringbuf_append_str(sb, "<hr />\n");
    }
    // Inline elements
    else if (strcmp(tag, "em") == 0 || strcmp(tag, "i") == 0) {
        stringbuf_append_str(sb, "<em>");
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, "</em>");
    }
    else if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
        stringbuf_append_str(sb, "<strong>");
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, "</strong>");
    }
    else if (strcmp(tag, "a") == 0 || strcmp(tag, "link") == 0) {
        String* href = elem.get_string_attr("href");
        String* title = elem.get_string_attr("title");

        stringbuf_append_str(sb, "<a href=\"");
        if (href && href->chars) {
            format_cm_text(ctx, href->chars, href->len);
        }
        stringbuf_append_str(sb, "\"");

        if (title && title->chars && title->len > 0) {
            stringbuf_append_str(sb, " title=\"");
            format_cm_text(ctx, title->chars, title->len);
            stringbuf_append_str(sb, "\"");
        }
        stringbuf_append_str(sb, ">");
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, "</a>");
    }
    else if (strcmp(tag, "img") == 0 || strcmp(tag, "image") == 0) {
        String* src = elem.get_string_attr("src");
        String* alt = elem.get_string_attr("alt");
        String* title = elem.get_string_attr("title");

        stringbuf_append_str(sb, "<img src=\"");
        if (src && src->chars) {
            format_cm_text(ctx, src->chars, src->len);
        }
        stringbuf_append_str(sb, "\" alt=\"");
        if (alt && alt->chars) {
            format_cm_text(ctx, alt->chars, alt->len);
        }
        stringbuf_append_str(sb, "\"");

        if (title && title->chars && title->len > 0) {
            stringbuf_append_str(sb, " title=\"");
            format_cm_text(ctx, title->chars, title->len);
            stringbuf_append_str(sb, "\"");
        }
        stringbuf_append_str(sb, " />");
    }
    else if (strcmp(tag, "br") == 0 || strcmp(tag, "hard_break") == 0) {
        stringbuf_append_str(sb, "<br />\n");
    }
    else if (strcmp(tag, "softbreak") == 0 || strcmp(tag, "soft_break") == 0) {
        stringbuf_append_char(sb, '\n');
    }
    // Document structure elements - just process children
    else if (strcmp(tag, "doc") == 0 || strcmp(tag, "document") == 0 ||
             strcmp(tag, "body") == 0 || strcmp(tag, "span") == 0) {
        format_cm_children(ctx, elem);
    }
    // Unknown elements - try to format as generic
    else {
        // Check if it looks like an HTML element
        stringbuf_append_str(sb, "<");
        stringbuf_append_str(sb, tag);
        stringbuf_append_str(sb, ">");
        format_cm_text_children(ctx, elem);
        stringbuf_append_str(sb, "</");
        stringbuf_append_str(sb, tag);
        stringbuf_append_str(sb, ">");
    }
}

// Format a single item (element, string, or other)
static void format_cm_item(CommonMarkHtmlContext& ctx, const ItemReader& item) {
    if (item.isNull()) return;

    if (item.isString()) {
        String* str = item.asString();
        if (str && str->chars) {
            format_cm_text(ctx, str->chars, str->len);
        }
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_cm_element(ctx, elem);
    }
    else if (item.isList()) {
        // Access list directly from Item
        Item raw_item = item.item();
        List* list = raw_item.list;
        if (list && list->items) {
            for (int64_t i = 0; i < list->length; i++) {
                ItemReader child(list->items[i].to_const());
                format_cm_item(ctx, child);
            }
        }
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        for (int64_t i = 0; i < arr.length(); i++) {
            ItemReader child = arr.get(i);
            format_cm_item(ctx, child);
        }
    }
}

/**
 * Format a parsed Markdown AST as CommonMark-style HTML fragment.
 *
 * @param root The root item of the parsed AST (typically a doc element or list)
 * @return HTML fragment string
 */
static std::string format_commonmark_html(Item root) {
    if (root.item == ITEM_NULL || root.item == ITEM_ERROR) {
        return "";
    }

    Pool* pool = pool_create();
    CommonMarkHtmlContext ctx(pool);

    // Create reader for the root item
    ItemReader root_reader(root.to_const());
    format_cm_item(ctx, root_reader);

    std::string result = ctx.result();
    pool_destroy(pool);

    return result;
}

#endif // COMMONMARK_HTML_FORMATTER_HPP
