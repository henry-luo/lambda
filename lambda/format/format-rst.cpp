#include "format.h"
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

// Format plain text (escape RST special characters)
static void format_text(StringBuf* sb, String* str) {
    if (!sb || !str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '*':
            // Escape these characters in RST
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        case '_':
        case '|':
        case '\\':
        case ':':
            // Escape these characters in RST
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// formats RST to a provided StrBuf
// formats RST to a provided StrBuf
void format_rst(StringBuf* sb, Item root_item) {
    if (!sb) return;
    
    // handle null/empty root item
    if (root_item.item == ITEM_NULL || (root_item.item == ITEM_NULL)) return;
    
    // reset recursion depth for each top-level call
    recursion_depth = 0;
    
    // use MarkReader API
    ItemReader root(root_item.to_const());
    format_item_reader(sb, root);
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
static void format_element_children_reader(StringBuf* sb, const ElementReader& elem) {
    // prevent infinite recursion
    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        printf("RST formatter: Maximum recursion depth reached, stopping element_children_reader\n");
        return;
    }
    
    recursion_depth++;
    
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        format_item_reader(sb, child);
    }
    
    recursion_depth--;
}

// format heading using reader API
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
    } else if (tag_name && strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }
    
    // RST heading characters in order of preference
    char underline_chars[] = {'=', '-', '~', '^', '"', '\''};
    char underline_char = underline_chars[(level - 1) % 6];
    
    // format the heading text
    size_t start_length = sb->length;
    format_element_children_reader(sb, elem);
    size_t end_length = sb->length;
    
    // calculate text length (excluding newlines)
    int title_length = 0;
    for (size_t i = start_length; i < end_length; i++) {
        if (sb->str && sb->str->chars[i] != '\n' && sb->str->chars[i] != '\r') {
            title_length++;
        }
    }
    
    stringbuf_append_char(sb, '\n');
    
    // add the underline with the same length as the title
    for (int i = 0; i < title_length; i++) {
        stringbuf_append_char(sb, underline_char);
    }
    stringbuf_append_str(sb, "\n\n");
}

// format emphasis using reader API
static void format_emphasis_reader(StringBuf* sb, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    
    if (strcmp(tag_name, "strong") == 0) {
        stringbuf_append_str(sb, "**");
        format_element_children_reader(sb, elem);
        stringbuf_append_str(sb, "**");
    } else if (strcmp(tag_name, "em") == 0) {
        stringbuf_append_char(sb, '*');
        format_element_children_reader(sb, elem);
        stringbuf_append_char(sb, '*');
    }
}

// format code using reader API
static void format_code_reader(StringBuf* sb, const ElementReader& elem) {
    ItemReader lang_attr = elem.get_attr("language");
    
    if (lang_attr.isString()) {
        String* lang_str = lang_attr.asString();
        if (lang_str && lang_str->len > 0) {
            // code block using RST code-block directive
            stringbuf_append_str(sb, ".. code-block:: ");
            stringbuf_append_str(sb, lang_str->chars);
            stringbuf_append_str(sb, "\n\n   ");
            
            // format children with proper indentation
            format_element_children_reader(sb, elem);
            
            stringbuf_append_str(sb, "\n\n");
            return;
        }
    }
    
    // inline code
    stringbuf_append_str(sb, "``");
    format_element_children_reader(sb, elem);
    stringbuf_append_str(sb, "``");
}

// format link using reader API
static void format_link_reader(StringBuf* sb, const ElementReader& elem) {
    ItemReader href = elem.get_attr("href");
    
    // RST external link format: `link text <URL>`_
    stringbuf_append_char(sb, '`');
    format_element_children_reader(sb, elem);
    
    if (href.isString()) {
        String* href_str = href.asString();
        if (href_str && href_str->len > 0) {
            stringbuf_append_str(sb, " <");
            stringbuf_append_str(sb, href_str->chars);
            stringbuf_append_str(sb, ">");
        }
    }
    
    stringbuf_append_str(sb, "`_");
}

// format list using reader API
static void format_list_reader(StringBuf* sb, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    bool is_ordered = (strcmp(tag_name, "ol") == 0);
    
    // get list attributes from Pandoc schema
    ItemReader start_attr = elem.get_attr("start");
    int start_num = 1;
    if (start_attr.isString()) {
        String* start_str = start_attr.asString();
        if (start_str && start_str->len > 0) {
            start_num = atoi(start_str->chars);
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
                    stringbuf_append_str(sb, num_buf);
                } else {
                    // RST uses - for bullet points
                    stringbuf_append_str(sb, "- ");
                }
                
                format_element_children_reader(sb, li_elem);
                stringbuf_append_char(sb, '\n');
                i++;
            }
        }
    }
}

// format table row using reader API
static void format_table_row_reader(StringBuf* sb, const ElementReader& row, bool is_header) {
    stringbuf_append_str(sb, "   ");  // indent for table directive
    
    auto it = row.children();
    ItemReader cell_item;
    bool first = true;
    while (it.next(&cell_item)) {
        if (!first) stringbuf_append_str(sb, " | ");
        first = false;
        
        if (cell_item.isElement()) {
            ElementReader cell = cell_item.asElement();
            format_element_children_reader(sb, cell);
        }
    }
    stringbuf_append_char(sb, '\n');
}

// format table separator using reader API
static void format_table_separator_reader(StringBuf* sb, const ElementReader& header_row) {
    stringbuf_append_str(sb, "   ");  // indent for table directive
    
    auto it = header_row.children();
    ItemReader cell;
    bool first = true;
    while (it.next(&cell)) {
        if (!first) stringbuf_append_str(sb, " + ");
        first = false;
        stringbuf_append_str(sb, "===");
    }
    
    stringbuf_append_char(sb, '\n');
}

// format table using reader API
static void format_table_reader(StringBuf* sb, const ElementReader& elem) {
    stringbuf_append_str(sb, ".. table::\n\n");
    
    // process table sections (thead, tbody)
    auto it = elem.children();
    ItemReader section_item;
    while (it.next(&section_item)) {
        if (section_item.isElement()) {
            ElementReader section = section_item.asElement();
            const char* section_type = section.tagName();
            
            bool is_header = (section_type && strcmp(section_type, "thead") == 0);
            
            auto row_it = section.children();
            ItemReader row_item;
            bool first_row = true;
            while (row_it.next(&row_item)) {
                if (row_item.isElement()) {
                    ElementReader row = row_item.asElement();
                    format_table_row_reader(sb, row, is_header);
                    
                    // add separator row after header
                    if (is_header && first_row) {
                        format_table_separator_reader(sb, row);
                    }
                    first_row = false;
                }
            }
        }
    }
    stringbuf_append_char(sb, '\n');
}

// format element using reader API
static void format_element_reader(StringBuf* sb, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    if (!tag_name) {
        return;
    }
    
    // handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        format_heading_reader(sb, elem);
    } else if (strcmp(tag_name, "p") == 0) {
        format_element_children_reader(sb, elem);
        stringbuf_append_str(sb, "\n\n");
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "em") == 0) {
        format_emphasis_reader(sb, elem);
    } else if (strcmp(tag_name, "code") == 0) {
        format_code_reader(sb, elem);
    } else if (strcmp(tag_name, "a") == 0) {
        format_link_reader(sb, elem);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        format_list_reader(sb, elem);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "hr") == 0) {
        stringbuf_append_str(sb, "----\n\n");
    } else if (strcmp(tag_name, "table") == 0) {
        format_table_reader(sb, elem);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 || 
               strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0) {
        // just format children for document root, body, and span containers
        format_element_children_reader(sb, elem);
    } else if (strcmp(tag_name, "meta") == 0) {
        // skip meta elements in RST output
        return;
    } else {
        // for unknown elements, just format children
        format_element_children_reader(sb, elem);
    }
}

// format item using reader API
static void format_item_reader(StringBuf* sb, const ItemReader& item) {
    // prevent infinite recursion
    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        printf("RST formatter: Maximum recursion depth reached, stopping format_item_reader\n");
        return;
    }
    
    recursion_depth++;
    
    if (item.isNull()) {
        // skip null items
    }
    else if (item.isString()) {
        String* str = item.asString();
        if (str) { format_text(sb, str); }
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
    
    recursion_depth--;
}
