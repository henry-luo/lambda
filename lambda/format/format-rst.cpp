#include "format.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include <string.h>
#include <ctype.h>

// MarkReader-based forward declarations with context
static void format_item_reader(RstContext& ctx, const ItemReader& item);
static void format_element_reader(RstContext& ctx, const ElementReader& elem);
static void format_element_children_reader(RstContext& ctx, const ElementReader& elem);

// Format plain text (escape RST special characters)
static void format_text(RstContext& ctx, String* str) {
    if (!str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        ctx.write_escaped_rst_char(s[i]);
    }
}

// formats RST to a provided StrBuf
void format_rst(StringBuf* sb, Item root_item) {
    if (!sb) return;

    // handle null/empty root item
    if (root_item.item == ITEM_NULL || (root_item.item == ITEM_NULL)) return;

    // create context
    Pool* pool = pool_create();
    RstContext ctx(pool, sb);

    // use MarkReader API
    ItemReader root(root_item.to_const());
    format_item_reader(ctx, root);

    pool_destroy(pool);
}

// Main entry point that creates a String* return value
String* format_rst_string(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    format_rst(sb, root_item);

    return stringbuf_to_string(sb);
}

// ===== MarkReader-based implementations =====

// format element children using reader API
static void format_element_children_reader(RstContext& ctx, const ElementReader& elem) {
    // RAII recursion guard
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        printf("RST formatter: Maximum recursion depth reached, stopping element_children_reader\n");
        return;
    }

    // note: can't use shared utility here because format_item_reader signature changed
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        format_item_reader(ctx, child);
    }
}

// format heading using reader API
static void format_heading_reader(RstContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    int level = 1;

    // try to get level from attribute (Pandoc schema)
    ItemReader level_attr = elem.get_attr("level");
    if (level_attr.isString()) {
        String* level_str = level_attr.asString();
        if (level_str && level_str->len > 0) {
            level = (int)str_to_int64_default(level_str->chars, strlen(level_str->chars), 0);
            if (level < 1) level = 1;
            if (level > 6) level = 6;
        }
    } else if (tag_name && strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }

    // format the heading text
    StringBuf* sb = ctx.output();
    size_t start_length = sb->length;
    format_element_children_reader(ctx, elem);
    size_t end_length = sb->length;

    // calculate text length (excluding newlines)
    int title_length = 0;
    for (size_t i = start_length; i < end_length; i++) {
        if (sb->str && sb->str->chars[i] != '\n' && sb->str->chars[i] != '\r') {
            title_length++;
        }
    }

    // add the underline using context utility
    ctx.write_heading_underline(level, title_length);
}

// format emphasis using reader API
static void format_emphasis_reader(RstContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();

    if (strcmp(tag_name, "strong") == 0) {
        ctx.write_text("**");
        format_element_children_reader(ctx, elem);
        ctx.write_text("**");
    } else if (strcmp(tag_name, "em") == 0) {
        ctx.write_char('*');
        format_element_children_reader(ctx, elem);
        ctx.write_char('*');
    }
}

// format code using reader API
static void format_code_reader(RstContext& ctx, const ElementReader& elem) {
    ItemReader lang_attr = elem.get_attr("language");

    if (lang_attr.isString()) {
        String* lang_str = lang_attr.asString();
        if (lang_str && lang_str->len > 0) {
            // code block using RST code-block directive
            ctx.write_text(".. code-block:: ");
            ctx.write_text(lang_str->chars);
            ctx.write_text("\n\n   ");

            // format children with proper indentation
            format_element_children_reader(ctx, elem);

            ctx.write_text("\n\n");
            return;
        }
    }

    // inline code
    ctx.write_text("``");
    format_element_children_reader(ctx, elem);
    ctx.write_text("``");
}

// format link using reader API
static void format_link_reader(RstContext& ctx, const ElementReader& elem) {
    ItemReader href = elem.get_attr("href");

    // RST external link format: `link text <URL>`_
    ctx.write_char('`');
    format_element_children_reader(ctx, elem);

    if (href.isString()) {
        String* href_str = href.asString();
        if (href_str && href_str->len > 0) {
            ctx.write_text(" <");
            ctx.write_text(href_str->chars);
            ctx.write_text(">");
        }
    }

    ctx.write_text("`_");
}

// format list using reader API
static void format_list_reader(RstContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    bool is_ordered = (strcmp(tag_name, "ol") == 0);

    // get list attributes from Pandoc schema
    ItemReader start_attr = elem.get_attr("start");
    int start_num = 1;
    if (start_attr.isString()) {
        String* start_str = start_attr.asString();
        if (start_str && start_str->len > 0) {
            start_num = (int)str_to_int64_default(start_str->chars, strlen(start_str->chars), 0);
        }
    }

    // format list items
    auto it = elem.children();
    ItemReader child;
    long i = 0;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader li_elem = child.asElement();
            const char* li_tag = li_elem.tagName();

            if (li_tag && strcmp(li_tag, "li") == 0) {
                if (is_ordered) {
                    char num_buf[32];
                    snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                    ctx.write_text(num_buf);
                } else {
                    // RST uses - for bullet points
                    ctx.write_text("- ");
                }

                format_element_children_reader(ctx, li_elem);
                ctx.write_char('\n');
                i++;
            }
        }
    }
}

// context for RST table formatting
typedef struct {
    RstContext* ctx;
    int first_header_row;
} RSTTableContext;

// callback for RST table row formatting
static void format_rst_table_row(
    StringBuf* sb,
    const ElementReader& row,
    int row_idx,
    bool is_header,
    void* ctx_ptr
) {
    RSTTableContext* context = (RSTTableContext*)ctx_ptr;
    RstContext& ctx = *context->ctx;

    // format table row with RST syntax
    ctx.write_text("   ");  // indent for table directive

    auto it = row.children();
    ItemReader cell_item;
    bool first = true;
    while (it.next(&cell_item)) {
        if (!first) ctx.write_text(" | ");
        first = false;

        if (cell_item.isElement()) {
            ElementReader cell = cell_item.asElement();
            format_element_children_reader(ctx, cell);
        }
    }
    ctx.write_char('\n');

    // add separator row after first header row
    if (is_header && row_idx == 0 && context->first_header_row == 0) {
        context->first_header_row = 1;

        ctx.write_text("   ");  // indent for table directive
        auto sep_it = row.children();
        ItemReader sep_cell;
        bool sep_first = true;
        while (sep_it.next(&sep_cell)) {
            if (!sep_first) ctx.write_text(" + ");
            sep_first = false;
            ctx.write_text("===");
        }
        ctx.write_char('\n');
    }
}

// format table using reader API
static void format_table_reader(RstContext& ctx, const ElementReader& elem) {
    ctx.write_text(".. table::\n\n");

    RSTTableContext context = {&ctx, 0};
    iterate_table_rows(elem, ctx.output(), format_rst_table_row, &context);

    ctx.write_char('\n');
}

// format element using reader API
static void format_element_reader(RstContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        return;
    }

    // handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        format_heading_reader(ctx, elem);
    } else if (strcmp(tag_name, "p") == 0) {
        format_element_children_reader(ctx, elem);
        ctx.write_text("\n\n");
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "em") == 0) {
        format_emphasis_reader(ctx, elem);
    } else if (strcmp(tag_name, "code") == 0) {
        format_code_reader(ctx, elem);
    } else if (strcmp(tag_name, "a") == 0) {
        format_link_reader(ctx, elem);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        format_list_reader(ctx, elem);
        ctx.write_char('\n');
    } else if (strcmp(tag_name, "hr") == 0) {
        ctx.write_text("----\n\n");
    } else if (strcmp(tag_name, "table") == 0) {
        format_table_reader(ctx, elem);
        ctx.write_char('\n');
    } else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 ||
               strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0) {
        // just format children for document root, body, and span containers
        format_element_children_reader(ctx, elem);
    } else if (strcmp(tag_name, "meta") == 0) {
        // skip meta elements in RST output
        return;
    } else {
        // for unknown elements, just format children
        format_element_children_reader(ctx, elem);
    }
}

// format item using reader API
static void format_item_reader(RstContext& ctx, const ItemReader& item) {
    // prevent infinite recursion
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        printf("RST formatter: Maximum recursion depth reached, stopping format_item_reader\n");
        return;
    }

    if (item.isNull()) {
        // skip null items
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) { format_text(ctx, str); }
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(ctx, elem);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        auto it = arr.items();
        ItemReader child;
        while (it.next(&child)) {
            format_item_reader(ctx, child);
        }
    }
}
