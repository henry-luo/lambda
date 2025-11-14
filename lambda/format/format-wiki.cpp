#include "format.h"
#include "format-utils.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include <string.h>
#include <ctype.h>

// Global recursion depth counter to prevent infinite recursion
static thread_local int recursion_depth = 0;
#define MAX_RECURSION_DEPTH 50

// MarkReader-based forward declarations
static void format_item_reader(StringBuf* sb, const ItemReader& item);
static void format_element_reader(StringBuf* sb, const ElementReader& elem);
static void format_element_children_reader(StringBuf* sb, const ElementReader& elem);

// Format raw text (no escaping - for code blocks, etc.)
// Format raw text without escaping (use shared utility)
static void format_raw_text(StringBuf* sb, String* str) {
    format_raw_text_common(sb, str);
}

// Format plain text (escape wiki markup using shared utility)
static void format_text(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;
    format_text_with_escape(sb, str, &WIKI_ESCAPE_CONFIG);
}

// Main Wiki formatting function (StrBuf version)
void format_wiki(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // handle null/empty root item
    if (root_item.item == ITEM_NULL) return;
    
    recursion_depth = 0;
    
    // use MarkReader API
    ItemReader root(root_item.to_const());
    format_item_reader(sb, root);
}

// Main Wiki formatting function (String version)
String* format_wiki_string(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    format_wiki(sb, root_item);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}

// ===== MarkReader-based implementations =====

// format element children using MarkReader API
static void format_element_children_reader(StringBuf* sb, const ElementReader& elem) {
    // use shared utility for simple child iteration
    format_element_children_with_processors(sb, elem, nullptr, format_item_reader);
}

// format element children raw (no escaping)
static void format_element_children_raw_reader(StringBuf* sb, const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            format_raw_text(sb, str);
        } else {
            format_item_reader(sb, child);
        }
    }
}

// format heading element using reader API
static void format_heading_reader(StringBuf* sb, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    int level = 1;
    
    // try to get level from attribute (Pandoc schema)
    ItemReader level_attr = elem.get_attr("level");
    if (level_attr.isString()) {
        String* level_str = level_attr.asString();
        if (level_str && level_str->len > 0) {
            level = atoi(level_str->chars);
            if (level < 1) level = 1;
            if (level > 6) level = 6;
        }
    } else if (strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }
    
    // Wiki heading format: = Level 1 =, == Level 2 ==, etc.
    for (int i = 0; i < level; i++) {
        stringbuf_append_char(sb, '=');
    }
    stringbuf_append_char(sb, ' ');
    
    format_element_children_reader(sb, elem);
    
    stringbuf_append_char(sb, ' ');
    for (int i = 0; i < level; i++) {
        stringbuf_append_char(sb, '=');
    }
    stringbuf_append_str(sb, "\n\n");
}

// format link element using reader API
static void format_link_reader(StringBuf* sb, const ElementReader& elem) {
    ItemReader href = elem.get_attr("href");
    ItemReader title = elem.get_attr("title");
    
    if (href.isString()) {
        String* href_str = href.asString();
        if (href_str && href_str->len > 0) {
            // external link format: [URL Display Text]
            stringbuf_append_char(sb, '[');
            stringbuf_append_str_n(sb, href_str->chars, href_str->len);
            stringbuf_append_char(sb, ' ');
            
            // use title if available, otherwise use link content
            if (title.isString()) {
                String* title_str = title.asString();
                if (title_str && title_str->len > 0) {
                    format_text(sb, title_str);
                } else {
                    format_element_children_reader(sb, elem);
                }
            } else {
                format_element_children_reader(sb, elem);
            }
            stringbuf_append_char(sb, ']');
            return;
        }
    }
    
    // internal wiki link format: [[Page Name]]
    stringbuf_append_str(sb, "[[");
    format_element_children_reader(sb, elem);
    stringbuf_append_str(sb, "]]");
}

// format list item using reader API
static void format_list_item_reader(StringBuf* sb, const ElementReader& elem, int depth, bool is_ordered) {
    // add proper indentation
    for (int i = 0; i < depth; i++) {
        if (is_ordered) {
            stringbuf_append_char(sb, '#');
        } else {
            stringbuf_append_char(sb, '*');
        }
    }
    stringbuf_append_char(sb, ' ');
    
    format_element_children_reader(sb, elem);
    stringbuf_append_char(sb, '\n');
}

// format unordered list using reader API
static void format_unordered_list_reader(StringBuf* sb, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            format_list_item_reader(sb, child_elem, depth + 1, false);
        }
    }
    
    if (depth == 0) stringbuf_append_char(sb, '\n');
}

// format ordered list using reader API
static void format_ordered_list_reader(StringBuf* sb, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            format_list_item_reader(sb, child_elem, depth + 1, true);
        }
    }
    
    if (depth == 0) stringbuf_append_char(sb, '\n');
}

// context for Wiki table formatting
typedef struct {
    bool table_started;
} WikiTableContext;

// callback for Wiki table row formatting
static void format_wiki_table_row(
    StringBuf* sb,
    const ElementReader& row,
    int row_idx,
    bool is_header,
    void* ctx
) {
    WikiTableContext* context = (WikiTableContext*)ctx;
    
    // start table on first row
    if (!context->table_started) {
        stringbuf_append_str(sb, "{| class=\"wikitable\"");
        if (is_header) {
            stringbuf_append_str(sb, " style=\"font-weight:bold\"");
        }
        stringbuf_append_str(sb, "\n");
        context->table_started = true;
    }
    
    // start row
    stringbuf_append_str(sb, "|-\n");
    
    // format cells
    auto it = row.children();
    ItemReader cell_item;
    while (it.next(&cell_item)) {
        if (cell_item.isElement()) {
            ElementReader cell = cell_item.asElement();
            
            if (is_header) {
                stringbuf_append_str(sb, "! ");
            } else {
                stringbuf_append_str(sb, "| ");
            }
            
            format_element_children_reader(sb, cell);
            stringbuf_append_char(sb, '\n');
        }
    }
}

// format table using reader API
static void format_table_reader(StringBuf* sb, const ElementReader& elem) {
    WikiTableContext context = {false};
    iterate_table_rows(elem, sb, format_wiki_table_row, &context);
    
    // close table if it was started
    if (context.table_started) {
        stringbuf_append_str(sb, "|}\n\n");
    }
}

// format element using reader API
static void format_element_reader(StringBuf* sb, const ElementReader& elem) {
    // recursion depth check
    recursion_depth++;
    if (recursion_depth > MAX_RECURSION_DEPTH) {
        printf("WARNING: Maximum recursion depth reached in Wiki formatter\n");
        recursion_depth--;
        return;
    }
    
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        format_element_children_reader(sb, elem);
        recursion_depth--;
        return;
    }
    
    // handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && isdigit(tag_name[1])) {
        format_heading_reader(sb, elem);
    }
    else if (strcmp(tag_name, "p") == 0) {
        format_element_children_reader(sb, elem);
        stringbuf_append_str(sb, "\n\n");
    }
    else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        stringbuf_append_str(sb, "''");
        format_element_children_reader(sb, elem);
        stringbuf_append_str(sb, "''");
    }
    else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        stringbuf_append_str(sb, "'''");
        format_element_children_reader(sb, elem);
        stringbuf_append_str(sb, "'''");
    }
    else if (strcmp(tag_name, "code") == 0) {
        stringbuf_append_str(sb, "<code>");
        format_element_children_raw_reader(sb, elem);
        stringbuf_append_str(sb, "</code>");
    }
    else if (strcmp(tag_name, "pre") == 0) {
        stringbuf_append_str(sb, "<pre>\n");
        format_element_children_raw_reader(sb, elem);
        stringbuf_append_str(sb, "\n</pre>\n\n");
    }
    else if (strcmp(tag_name, "a") == 0) {
        format_link_reader(sb, elem);
    }
    else if (strcmp(tag_name, "ul") == 0) {
        format_unordered_list_reader(sb, elem, 0);
    }
    else if (strcmp(tag_name, "ol") == 0) {
        format_ordered_list_reader(sb, elem, 0);
    }
    else if (strcmp(tag_name, "li") == 0) {
        // list items are handled by their parent list
        format_element_children_reader(sb, elem);
    }
    else if (strcmp(tag_name, "table") == 0) {
        format_table_reader(sb, elem);
    }
    else if (strcmp(tag_name, "tr") == 0 || strcmp(tag_name, "td") == 0 || strcmp(tag_name, "th") == 0) {
        // table elements are handled by their parent table
        format_element_children_reader(sb, elem);
    }
    else if (strcmp(tag_name, "br") == 0) {
        stringbuf_append_str(sb, "\n");
    }
    else if (strcmp(tag_name, "hr") == 0) {
        stringbuf_append_str(sb, "----\n\n");
    }
    else {
        // unknown element - just format children
        format_element_children_reader(sb, elem);
    }
    
    recursion_depth--;
}

// format item using reader API
static void format_item_reader(StringBuf* sb, const ItemReader& item) {
    if (item.isString()) {
        String* str = item.asString();
        format_text(sb, str);
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(sb, elem);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        auto it = arr.items();
        ItemReader child;
        while (it.next(&child)) {
            format_item_reader(sb, child);
        }
    }
}
