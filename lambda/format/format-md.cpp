#include "format.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <ctype.h>

// Forward declarations for math formatting support
String* format_math_latex(Pool* pool, Item root_item);
static void format_math_inline(StringBuf* sb, Element* elem);
static void format_math_display(StringBuf* sb, Element* elem);
static void format_math_code_block(StringBuf* sb, Element* elem);

static void format_item(StringBuf* sb, Item item);
static void format_element(StringBuf* sb, Element* elem);
static void format_element_children(StringBuf* sb, Element* elem);
static void format_element_children_raw(StringBuf* sb, Element* elem);
static void format_table_row(StringBuf* sb, Element* row, bool is_header);
static void format_table_separator(StringBuf* sb, Element* header_row);

// MarkReader-based forward declarations (using MarkdownContext)
static void format_item_reader(MarkdownContext& ctx, const ItemReader& item);
static void format_element_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_element_children_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_element_children_raw_reader(MarkdownContext& ctx, const ElementReader& elem);

// MarkReader-based helper function forward declarations
static void format_heading_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_emphasis_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_code_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_link_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_list_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_table_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_paragraph_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_blockquote_reader(MarkdownContext& ctx, const ElementReader& elem);
static void format_thematic_break(MarkdownContext& ctx);

// Utility function to get attribute value from element
static String* get_attribute(Element* elem, const char* attr_name) {
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

// MarkReader-based version: get attribute value from element using ElementReader
// Format raw text without escaping (use shared utility)
static void format_raw_text(MarkdownContext& ctx, String* str) {
    format_raw_text_common(ctx.output(), str);
}

// Format plain text (escape markdown special characters using shared utility)
static void format_text(MarkdownContext& ctx, String* str) {
    if (!str || str->len == 0) return;

    const char* s = str->chars;

    // only debug when processing LaTeX-like content
    if (strstr(s, "frac") || strstr(s, "[x") || strchr(s, '$')) {
        printf("DEBUG format_text: Processing text='%s' (len=%zu)\n", s, str->len);
    }

    format_text_with_escape(ctx.output(), str, &MARKDOWN_ESCAPE_CONFIG);
}

// Format heading elements (h1-h6)
// MarkReader version: Format heading elements (h1-h6)
static void format_heading_reader(MarkdownContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    if (!tag_name) return;

    int level = 1;

    // First try to get level from attribute (Pandoc schema)
    String* level_attr = elem.get_string_attr("level");
    if (level_attr && level_attr->len > 0) {
        level = atoi(level_attr->chars);
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    } else if (strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // Fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }

    // Add the appropriate number of # characters
    for (int i = 0; i < level; i++) {
        ctx.write_char( '#');
    }
    ctx.write_char( ' ');

    format_element_children_reader(ctx, elem);
    ctx.write_char( '\n');
}

// Format emphasis elements (em, strong)
// MarkReader version: Format emphasis elements (em, strong)
static void format_emphasis_reader(MarkdownContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    if (!tag_name) return;

    if (strcmp(tag_name, "strong") == 0) {
        ctx.write_text( "**");
        format_element_children_reader(ctx, elem);
        ctx.write_text( "**");
    } else if (strcmp(tag_name, "em") == 0) {
        ctx.write_char( '*');
        format_element_children_reader(ctx, elem);
        ctx.write_char( '*');
    }
}

// Format code elements
// MarkReader version: Format code elements
static void format_code_reader(MarkdownContext& ctx, const ElementReader& elem) {
    String* lang_attr = elem.get_string_attr("language");
    if (lang_attr && lang_attr->len > 0) {
        // Check if this is a math code block
        if (strcmp(lang_attr->chars, "math") == 0) {
            // Use display math formatter instead (still uses Element* temporarily)
            Element* raw_elem = const_cast<Element*>(elem.element());
            format_math_display(ctx.output(), raw_elem);
            return;
        }

        // Regular code block
        ctx.write_text( "```");
        ctx.write_text( lang_attr->chars);
        ctx.write_char( '\n');
        format_element_children_raw_reader(ctx, elem); // Use raw formatter for code content
        ctx.write_text( "\n```\n");
    } else {
        // Inline code
        ctx.write_char( '`');
        format_element_children_raw_reader(ctx, elem); // Use raw formatter for code content
        ctx.write_char( '`');
    }
}

// Format link elements
// MarkReader version: Format link elements
static void format_link_reader(MarkdownContext& ctx, const ElementReader& elem) {
    String* href = elem.get_string_attr("href");
    String* title = elem.get_string_attr("title");

    ctx.write_char( '[');
    format_element_children_reader(ctx, elem);
    ctx.write_char( ']');
    ctx.write_char( '(');

    if (href) {
        ctx.write_text( href->chars);
    }

    if (title && title->len > 0) {
        ctx.write_text( " \"");
        ctx.write_text( title->chars);
        ctx.write_char( '"');
    }

    ctx.write_char( ')');
}

// Format list elements (ul, ol)
// MarkReader version: Format list elements (ul, ol)
static void format_list_reader(MarkdownContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    if (!tag_name) return;

    bool is_ordered = (strcmp(tag_name, "ol") == 0);

    // Get list attributes from Pandoc schema
    String* start_attr = elem.get_string_attr("start");
    String* style_attr = elem.get_string_attr("style");
    String* type_attr = elem.get_string_attr("type");

    int start_num = 1;
    if (start_attr && start_attr->len > 0) {
        start_num = atoi(start_attr->chars);
    }

    // Determine bullet style for unordered lists
    const char* bullet_char = "-";
    if (!is_ordered && style_attr && style_attr->len > 0) {
        if (strcmp(style_attr->chars, "asterisk") == 0) {
            bullet_char = "*";
        } else if (strcmp(style_attr->chars, "plus") == 0) {
            bullet_char = "+";
        } else if (strcmp(style_attr->chars, "dash") == 0) {
            bullet_char = "-";
        }
    }

    // Format list items using MarkReader API
    auto children_iter = elem.children();
    ItemReader child;
    long i = 0;

    while (children_iter.next(&child)) {
        if (child.isElement()) {
            ElementReader li_elem = child.asElement();
            const char* li_tag = li_elem.tagName();

            if (li_tag && strcmp(li_tag, "li") == 0) {
                if (is_ordered) {
                    char num_buf[32];
                    // Use appropriate numbering style based on type attribute
                    if (type_attr && type_attr->len > 0) {
                        if (strcmp(type_attr->chars, "a") == 0) {
                            // Lower alpha
                            char alpha = 'a' + (start_num + i - 1) % 26;
                            snprintf(num_buf, sizeof(num_buf), "%c. ", alpha);
                        } else if (strcmp(type_attr->chars, "A") == 0) {
                            // Upper alpha
                            char alpha = 'A' + (start_num + i - 1) % 26;
                            snprintf(num_buf, sizeof(num_buf), "%c. ", alpha);
                        } else if (strcmp(type_attr->chars, "i") == 0) {
                            // Lower roman - simplified, just use numbers for now
                            snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                        } else {
                            // Default decimal
                            snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                        }
                    } else {
                        snprintf(num_buf, sizeof(num_buf), "%ld. ", start_num + i);
                    }
                    ctx.write_text( num_buf);
                } else {
                    ctx.write_text( bullet_char);
                    ctx.write_char( ' ');
                }

                format_element_children_reader(ctx, li_elem);
                ctx.write_char( '\n');
                i++;
            }
        }
    }
}

// Context for table row formatting
typedef struct {
    MarkdownContext* ctx;
    int first_header_row;
} MarkdownTableContext;

// Table row handler for markdown formatting
static void format_markdown_table_row(
    StringBuf* sb,
    const ElementReader& row,
    int row_idx,
    bool is_header,
    void* ctx_ptr
) {
    MarkdownTableContext* context = (MarkdownTableContext*)ctx_ptr;
    MarkdownContext& ctx = *context->ctx;

    // Format table row
    ctx.write_char('|');
    auto row_children = row.children();
    ItemReader cell_item;

    while (row_children.next(&cell_item)) {
        ctx.write_char(' ');
        if (cell_item.isElement()) {
            ElementReader cell = cell_item.asElement();
            format_element_children_reader(ctx, cell);
        }
        ctx.write_text(" |");
    }
    ctx.write_char('\n');

    // Add separator row after first header row
    if (is_header && row_idx == 0) {
        ctx.write_char('|');
        // Count cells for separator
        auto row_children2 = row.children();
        ItemReader cell_count_item;
        while (row_children2.next(&cell_count_item)) {
            ctx.write_text("---|");
        }
        ctx.write_char('\n');
    }
}

// MarkReader version: Format table elements
static void format_table_reader(MarkdownContext& ctx, const ElementReader& elem) {
    MarkdownTableContext context = {&ctx, 0};
    iterate_table_rows(elem, ctx.output(), format_markdown_table_row, &context);
}

// Format table row
static void format_table_row(StringBuf* sb, Element* row, bool is_header) {
    if (!row) return;

    stringbuf_append_char(sb, '|');
    List* row_list = (List*)row;
    if (row_list && row_list->length > 0) {
        for (long i = 0; i < row_list->length; i++) {
            stringbuf_append_char(sb, ' ');
            Item cell_item = row_list->items[i];
            if (get_type_id(cell_item) == LMD_TYPE_ELEMENT) {
                Element* cell = cell_item.element;
                format_element_children(sb, cell);
            }
            stringbuf_append_str(sb, " |");
        }
    }
    stringbuf_append_char(sb, '\n');
}

// Format table separator row
static void format_table_separator(StringBuf* sb, Element* header_row) {
    if (!header_row) return;

    stringbuf_append_char(sb, '|');

    List* row_list = (List*)header_row;
    if (row_list) {
        for (long i = 0; i < row_list->length; i++) {
            stringbuf_append_str(sb, "---|");
        }
    }

    stringbuf_append_char(sb, '\n');
}

// Format blockquote elements
// MarkReader version: Format blockquote elements
static void format_blockquote_reader(MarkdownContext& ctx, const ElementReader& elem) {
    // Format as blockquote with > prefix
    ctx.write_text( "> ");
    format_element_children_reader(ctx, elem);
    ctx.write_char( '\n');
}

// Helper function to check if an element contains only math (recursively)
static bool element_contains_only_math(Element* elem, bool* only_display_math) {
    if (!elem) return false;

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return false;

    const char* elem_name = elem_type->name.str;

    // If this is a math element, check its type
    if (strcmp(elem_name, "math") == 0) {
        String* type_attr = get_attribute(elem, "type");
        if (!type_attr || (strcmp(type_attr->chars, "block") != 0 && strcmp(type_attr->chars, "code") != 0)) {
            *only_display_math = false;
        }
        return true;
    }

    // If this is a span or similar container, check its children
    if (strcmp(elem_name, "span") == 0) {
        List* list = (List*)elem;
        if (list->length == 0) return true; // Empty span is okay

        for (long i = 0; i < list->length; i++) {
            Item child_item = list->items[i];
            TypeId type = get_type_id(child_item);

            if (type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child_item.element;
                if (!element_contains_only_math(child_elem, only_display_math)) {
                    return false;
                }
            } else if (type == LMD_TYPE_STRING) {
                // Check if it's just whitespace
                String* str = child_item.get_string();
                if (str && str->chars) {
                    for (int j = 0; j < str->len; j++) {
                        if (!isspace(str->chars[j])) {
                            return false;
                        }
                    }
                }
            } else {
                return false;
            }
        }
        return true;
    }

    // Other elements are not math-only
    return false;
}

// Check if an element (via MarkReader) contains only math
static bool element_reader_contains_only_math(const ElementReader& elem, bool* only_display_math) {
    const char* elem_name = elem.tagName();
    if (!elem_name) return false;

    // If this is a math element, check its type
    if (strcmp(elem_name, "math") == 0) {
        ItemReader type_attr = elem.get_attr("type");
        if (!type_attr.isString()) {
            *only_display_math = false;
        } else {
            String* type_str = type_attr.asString();
            if (strcmp(type_str->chars, "block") != 0 && strcmp(type_str->chars, "code") != 0) {
                *only_display_math = false;
            }
        }
        return true;
    }

    // If this is a span or similar container, check its children
    if (strcmp(elem_name, "span") == 0) {
        auto it = elem.children();
        ItemReader child;

        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                if (!element_reader_contains_only_math(child_elem, only_display_math)) {
                    return false;
                }
            } else if (child.isString()) {
                // Check if it's just whitespace
                String* str = child.asString();
                if (str && str->chars) {
                    for (int j = 0; j < str->len; j++) {
                        if (!isspace(str->chars[j])) {
                            return false;
                        }
                    }
                }
            } else {
                return false;
            }
        }
        return true;
    }

    // Other elements are not math-only
    return false;
}

// Format paragraph elements
// MarkReader version: Format paragraph elements
static void format_paragraph_reader(MarkdownContext& ctx, const ElementReader& elem) {
    // Check if paragraph contains only math (to avoid adding extra newline)
    bool only_display_math = true;

    // Check each child of the paragraph
    bool contains_only_math = true;
    auto it = elem.children();
    ItemReader child;

    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            if (!element_reader_contains_only_math(child_elem, &only_display_math)) {
                contains_only_math = false;
                break;
            }
        } else if (child.isString()) {
            // Check if it's just whitespace
            String* str = child.asString();
            if (str && str->chars) {
                for (int j = 0; j < str->len; j++) {
                    if (!isspace(str->chars[j])) {
                        contains_only_math = false;
                        break;
                    }
                }
            }
            if (!contains_only_math) break;
        } else {
            contains_only_math = false;
            break;
        }
    }

    format_element_children_reader(ctx, elem);

    // Only add newline if paragraph doesn't contain just inline math
    if (!contains_only_math || only_display_math) {
        ctx.write_char('\n');
    }
}

// Format thematic break (hr)
static void format_thematic_break(MarkdownContext& ctx) {
    ctx.write_text("---\n\n");
}

// Format inline math elements ($math$ or asciimath::...)
// Format inline math
static void format_math_inline(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* element_list = (List*)elem;

    // Check if this is ASCII math by looking at the type attribute
    String* type_attr = get_attribute(elem, "type");
    bool is_ascii_math = (type_attr && strcmp(type_attr->chars, "ascii") == 0);

    // Get the math content from the first child
    // The parsed math AST should be the first child of the math element
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];

        // Use heap-allocated memory pool for formatting
        Pool* pool = pool_create();
        if (pool != NULL) {
            if (is_ascii_math) {
                // Format as ASCII math with original prefix
                String* ascii_output = format_math_ascii_standalone(pool, math_item);

                if (ascii_output && ascii_output->len > 0) {
                    // Check if we have a flavor attribute to determine the original prefix
                    String* flavor_attr = get_attribute(elem, "flavor");
                    printf("DEBUG: flavor_attr = %p\n", flavor_attr);
                    if (flavor_attr) {
                        printf("DEBUG: flavor_attr->chars = '%s'\n", flavor_attr->chars);
                    }
                    if (flavor_attr && strcmp(flavor_attr->chars, "AM") == 0) {
                        printf("DEBUG: Using AM:: prefix\n");
                        stringbuf_append_str(sb, "AM::");
                    } else {
                        printf("DEBUG: Using asciimath:: prefix\n");
                        stringbuf_append_str(sb, "asciimath::");
                    }
                    stringbuf_append_str(sb, ascii_output->chars);
                } else {
                    // Fallback if ASCII math formatting fails
                    stringbuf_append_str(sb, "asciimath::math");
                }
            } else {
                // Format as LaTeX math with $ delimiters
                String* latex_output = format_math_latex(pool, math_item);

                if (latex_output && latex_output->len > 0) {
                    stringbuf_append_str(sb, "$");
                    stringbuf_append_str(sb, latex_output->chars);
                    stringbuf_append_str(sb, "$");
                } else {
                    // Fallback if math formatting fails
                    stringbuf_append_str(sb, "$");
                    stringbuf_append_str(sb, "math");
                    stringbuf_append_str(sb, "$");
                }
            }

            pool_destroy(pool);
        }
    }
}

// Format display math
static void format_math_display(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* element_list = (List*)elem;

    // Get the math content from the first child
    // The parsed math AST should be the first child of the math element
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];
        TypeId math_type = get_type_id(math_item);

        if (math_type == LMD_TYPE_STRING) {
            // Raw string content - output as-is
            String* math_string = math_item.get_string();
            if (math_string && math_string->len > 0) {
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str_n(sb, math_string->chars, math_string->len);
                stringbuf_append_str(sb, "$$");
                return;
            }
        }

        // Fallback: Use heap-allocated memory pool for formatting parsed math AST
        Pool* pool = pool_create();
        if (pool != NULL) {
            String* latex_output = format_math_latex(pool, math_item);

            if (latex_output && latex_output->len > 0) {
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str(sb, latex_output->chars);
                stringbuf_append_str(sb, "$$");
            } else {
                // Fallback if math formatting fails
                stringbuf_append_str(sb, "$$");
                stringbuf_append_str(sb, "math");
                stringbuf_append_str(sb, "$$");
            }

            pool_destroy(pool);
        }
    }
}

// Format math code block (```math)
static void format_math_code_block(StringBuf* sb, Element* elem) {
    if (!elem) return;

    List* element_list = (List*)elem;
    String* lang_attr = get_attribute(elem, "language");

    // Use the language attribute, defaulting to "math"
    const char* language = (lang_attr && lang_attr->len > 0) ? lang_attr->chars : "math";

    // Get the math content from the first child (should be raw string)
    if (element_list->length > 0) {
        Item math_item = element_list->items[0];
        TypeId math_type = get_type_id(math_item);

        if (math_type == LMD_TYPE_STRING) {
            // Raw string content - output as code block
            String* math_string = math_item.get_string();
            if (math_string && math_string->len > 0) {
                stringbuf_append_str(sb, "```");
                stringbuf_append_str(sb, language);
                stringbuf_append_char(sb, '\n');
                stringbuf_append_str_n(sb, math_string->chars, math_string->len);
                stringbuf_append_str(sb, "\n```");
                return;
            }
        }
    }

    // Fallback if no content found
    stringbuf_append_str(sb, "```");
    stringbuf_append_str(sb, language);
    stringbuf_append_str(sb, "\n");
    stringbuf_append_str(sb, "math");
    stringbuf_append_str(sb, "\n```");
}

// Helper function to check if an element is a block-level element
static bool is_block_element(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) return false;

    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return false;

    const char* tag_name = elem_type->name.str;

    // Check for heading elements
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) return true;

    // Check for other block elements
    if (strcmp(tag_name, "p") == 0 ||
        strcmp(tag_name, "ul") == 0 ||
        strcmp(tag_name, "ol") == 0 ||
        strcmp(tag_name, "blockquote") == 0 ||
        strcmp(tag_name, "table") == 0 ||
        strcmp(tag_name, "hr") == 0) {
        return true;
    }

    // Check for math elements with display types
    if (strcmp(tag_name, "math") == 0) {
        String* type_attr = get_attribute(elem, "type");
        return (type_attr && (strcmp(type_attr->chars, "block") == 0 || strcmp(type_attr->chars, "code") == 0));
    }

    return false;
}

// MarkReader-based version: check if an element is a block-level element
static bool is_block_element_reader(const ItemReader& item) {
    if (!item.isElement()) return false;

    ElementReader elem = item.asElement();
    const char* tag_name = elem.tagName();
    if (!tag_name) return false;

    // check for heading elements
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) return true;

    // check for other block elements
    if (strcmp(tag_name, "p") == 0 ||
        strcmp(tag_name, "ul") == 0 ||
        strcmp(tag_name, "ol") == 0 ||
        strcmp(tag_name, "blockquote") == 0 ||
        strcmp(tag_name, "table") == 0 ||
        strcmp(tag_name, "hr") == 0) {
        return true;
    }

    // check for math elements with display types
    if (strcmp(tag_name, "math") == 0) {
        String* type_attr = elem.get_string_attr("type");
        return (type_attr && (strcmp(type_attr->chars, "block") == 0 || strcmp(type_attr->chars, "code") == 0));
    }

    return false;
}

// Helper function to get heading level from an element
static int get_heading_level(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) return 0;

    Element* elem = item.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) return 0;

    const char* tag_name = elem_type->name.str;

    // Check if it's a heading
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        // First try to get level from attribute (Pandoc schema)
        String* level_attr = get_attribute(elem, "level");
        if (level_attr && level_attr->len > 0) {
            int level = atoi(level_attr->chars);
            return (level >= 1 && level <= 6) ? level : 0;
        }
        // Fallback: parse level from tag name
        int level = tag_name[1] - '0';
        return (level >= 1 && level <= 6) ? level : 0;
    }

    return 0;
}

// MarkReader-based version: get heading level from an element
static int get_heading_level_reader(const ItemReader& item) {
    if (!item.isElement()) return 0;

    ElementReader elem = item.asElement();
    const char* tag_name = elem.tagName();
    if (!tag_name) return 0;

    // check if it's a heading
    if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        // first try to get level from attribute (Pandoc schema)
        String* level_attr = elem.get_string_attr("level");
        if (level_attr && level_attr->len > 0) {
            int level = atoi(level_attr->chars);
            return (level >= 1 && level <= 6) ? level : 0;
        }
        // fallback: parse level from tag name
        int level = tag_name[1] - '0';
        return (level >= 1 && level <= 6) ? level : 0;
    }

    return 0;
}

static void format_element_children_raw(StringBuf* sb, Element* elem) {
    // Format children without escaping (for code blocks)
    List* list = (List*)elem;
    if (list->length == 0) return;

    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        TypeId type = get_type_id(child_item);

        if (type == LMD_TYPE_STRING) {
            String* str = child_item.get_string();
            if (str) {
                format_raw_text_common(sb, str);
            }
        } else {
            // For non-strings, use regular formatting
            format_item(sb, child_item);
        }
    }
}

// MarkReader-based version: format children without escaping (for code blocks)
static void format_element_children_raw_reader(MarkdownContext& ctx, const ElementReader& elem) {
    auto children_iter = elem.children();
    ItemReader child;

    while (children_iter.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            if (str) {
                format_raw_text(ctx, str);
            }
        } else {
            // for non-strings, use regular formatting
            format_item_reader(ctx, child);
        }
    }
}

static void format_element_children(StringBuf* sb, Element* elem) {
    // Element extends List, so access content through List interface
    List* list = (List*)elem;
    if (list->length == 0) return;

    for (long i = 0; i < list->length; i++) {
        Item child_item = list->items[i];
        format_item(sb, child_item);

        // Add appropriate spacing after block elements for better markdown formatting
        if (i < list->length - 1) { // Not the last element
            Item next_item = list->items[i + 1];

            bool current_is_block = is_block_element(child_item);
            bool next_is_block = is_block_element(next_item);

            // Add blank line between different heading levels (markdown best practice)
            int current_heading_level = get_heading_level(child_item);
            int next_heading_level = get_heading_level(next_item);

            if (current_heading_level > 0 && next_heading_level > 0 &&
                current_heading_level != next_heading_level) {
                // Different heading levels: add blank line
                stringbuf_append_char(sb, '\n');
            }
            else if (current_heading_level > 0 && next_is_block && next_heading_level == 0) {
                // Heading followed by non-heading block: add blank line
                stringbuf_append_char(sb, '\n');
            }
            else if (current_is_block && next_heading_level > 0) {
                // Block element followed by heading: paragraphs already add \n\n, so no extra needed
                // For other blocks that don't add their own spacing, add blank line
                TypeId current_type = get_type_id(child_item);
                if (current_type == LMD_TYPE_ELEMENT) {
                    Element* current_elem = child_item.element;
                    TypeElmt* current_elem_type = (TypeElmt*)current_elem->type;
                    if (current_elem_type && current_elem_type->name.str) {
                        const char* tag_name = current_elem_type->name.str;
                        // Only add newline for blocks that don't add their own spacing
                        if (strcmp(tag_name, "p") != 0 && strcmp(tag_name, "hr") != 0) {
                            stringbuf_append_char(sb, '\n');
                        }
                    }
                }
            }
        }
    }
}

// MarkReader-based version: format element children with proper spacing
static void format_element_children_reader(MarkdownContext& ctx, const ElementReader& elem) {
    // collect all children into a vector for lookahead logic
    std::vector<ItemReader> children;
    auto children_iter = elem.children();
    ItemReader child;
    while (children_iter.next(&child)) {
        children.push_back(child);
    }

    if (children.empty()) return;

    for (size_t i = 0; i < children.size(); i++) {
        const ItemReader& child_item = children[i];
        format_item_reader(ctx, child_item);

        // add appropriate spacing after block elements for better markdown formatting
        if (i < children.size() - 1) { // not the last element
            const ItemReader& next_item = children[i + 1];

            bool current_is_block = is_block_element_reader(child_item);
            bool next_is_block = is_block_element_reader(next_item);

            // add blank line between different heading levels (markdown best practice)
            int current_heading_level = get_heading_level_reader(child_item);
            int next_heading_level = get_heading_level_reader(next_item);

            if (current_heading_level > 0 && next_heading_level > 0 &&
                current_heading_level != next_heading_level) {
                // different heading levels: add blank line
                ctx.write_char( '\n');
            }
            else if (current_heading_level > 0 && next_is_block && next_heading_level == 0) {
                // heading followed by non-heading block: add blank line
                ctx.write_char( '\n');
            }
            else if (current_is_block && next_heading_level > 0) {
                // block element followed by heading: paragraphs already add \n\n, so no extra needed
                // for other blocks that don't add their own spacing, add blank line
                if (child_item.isElement()) {
                    ElementReader current_elem = child_item.asElement();
                    const char* tag_name = current_elem.tagName();
                    if (tag_name) {
                        // only add newline for blocks that don't add their own spacing
                        if (strcmp(tag_name, "p") != 0 && strcmp(tag_name, "hr") != 0) {
                            ctx.write_char( '\n');
                        }
                    }
                }
            }
        }
    }
}

// format Lambda element to markdown
static void format_element(StringBuf* sb, Element* elem) {
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    if (!elem_type || !elem_type->name.str) {
        return;
    }

    const char* tag_name = elem_type->name.str;

    printf("DEBUG format_element: processing element '%s' (len=%zu)\n", tag_name, strlen(tag_name));

    // Create temporary context for calling _reader functions
    Pool* temp_pool = pool_create();
    MarkdownContext temp_ctx(temp_pool, sb);

    // Special debug for math elements - print exact bytes
    if (strcmp(tag_name, "math") == 0) {
        printf("DEBUG: EXACT MATCH for math element!\n");
        for (int i = 0; i < 5; i++) {
            printf("DEBUG: tag_name[%d] = 0x%02x ('%c')\n", i, (unsigned char)tag_name[i], tag_name[i]);
        }
    }
    if (strcmp(tag_name, "math") == 0) {
        printf("DEBUG: math element detected!\n");
    }

    // Handle different element types
    if (strcmp(tag_name, "math") == 0) {
        // Math element - check type attribute to determine formatting
        printf("DEBUG: *** MATH ELEMENT HANDLER TRIGGERED ***\n");
        printf("DEBUG: Found math element, checking type attribute\n");
        String* type_attr = get_attribute(elem, "type");
        printf("DEBUG: type_attr = %p\n", type_attr);
        if (type_attr) {
            printf("DEBUG: type_attr->chars = '%s'\n", type_attr->chars);
        }

        if (type_attr && strcmp(type_attr->chars, "block") == 0) {
            // Display math ($$math$$)
            printf("DEBUG: Calling format_math_display\n");
            format_math_display(sb, elem);
        } else if (type_attr && strcmp(type_attr->chars, "code") == 0) {
            // Math code block (```math)
            printf("DEBUG: Calling format_math_code_block\n");
            format_math_code_block(sb, elem);
        } else {
            // Inline math ($math$) - default when no type or unknown type
            printf("DEBUG: Calling format_math_inline\n");
            format_math_inline(sb, elem);
        }
    } else if (strncmp(tag_name, "h", 1) == 0 && isdigit(tag_name[1])) {
        ElementReader elem_reader(elem);
        format_heading_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "p") == 0) {
        ElementReader elem_reader(elem);
        format_paragraph_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "blockquote") == 0) {
        ElementReader elem_reader(elem);
        format_blockquote_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "em") == 0) {
        ElementReader elem_reader(elem);
        format_emphasis_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "code") == 0) {
        ElementReader elem_reader(elem);
        format_code_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "a") == 0) {
        ElementReader elem_reader(elem);
        format_link_reader(temp_ctx, elem_reader);
    } else if (strcmp(tag_name, "ul") == 0 || strcmp(tag_name, "ol") == 0) {
        ElementReader elem_reader(elem);
        format_list_reader(temp_ctx, elem_reader);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "hr") == 0) {
        format_thematic_break(temp_ctx);
    } else if (strcmp(tag_name, "table") == 0) {
        ElementReader elem_reader(elem);
        format_table_reader(temp_ctx, elem_reader);
        stringbuf_append_char(sb, '\n');
    } else if (strcmp(tag_name, "math") == 0) {
        // Math element - check type attribute to determine formatting
        printf("DEBUG: *** MATH ELEMENT HANDLER TRIGGERED ***\n");
        printf("DEBUG: Found math element, checking type attribute\n");
        String* type_attr = get_attribute(elem, "type");
        printf("DEBUG: type_attr = %p\n", type_attr);
        if (type_attr) {
            printf("DEBUG: type_attr->chars = '%s'\n", type_attr->chars);
        }

        if (type_attr && strcmp(type_attr->chars, "block") == 0) {
            // Display math ($$math$$)
            printf("DEBUG: Calling format_math_display\n");
            format_math_display(sb, elem);
        } else if (type_attr && strcmp(type_attr->chars, "code") == 0) {
            // Math code block (```math)
            printf("DEBUG: Calling format_math_code_block\n");
            format_math_code_block(sb, elem);
        } else {
            // Inline math ($math$) - default when no type or unknown type
            printf("DEBUG: Calling format_math_inline\n");
            format_math_inline(sb, elem);
        }
    } else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 ||
               strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0) {
        // Just format children for document root, body, and span containers
        printf("DEBUG: Container element case triggered for '%s'\n", tag_name);
        format_element_children(sb, elem);
    } else if (strcmp(tag_name, "meta") == 0) {
        // Skip meta elements in markdown output
        pool_destroy(temp_pool);
        return;
    } else {
        // for unknown elements, just format children
        printf("DEBUG format_element: unknown element '%s', formatting children\n", tag_name);

        // Special case for jsx_element: try to output the content directly
        if (strcmp(tag_name, "jsx_element") == 0) {
            // jsx_element uses elmt_put to store Items in data with ShapeEntry offsets
            TypeElmt* elmt_type = (TypeElmt*)elem->type;
            if (elmt_type && elmt_type->shape) {
                ShapeEntry* field = elmt_type->shape;
                while (field) {
                    if (field->name && field->name->length == 7 &&
                        strncmp(field->name->str, "content", 7) == 0) {
                        // Get the Item stored at this field's byte offset
                        void* field_ptr = ((char*)elem->data) + field->byte_offset;
                        String* jsx_content = *(String**)field_ptr;  // String stored directly via elmt_put

                        if (jsx_content && jsx_content->chars) {
                            stringbuf_append_str(sb, jsx_content->chars);
                            // Add a space after JSX element to preserve spacing
                            stringbuf_append_char(sb, ' ');
                        }
                        pool_destroy(temp_pool);
                        return;
                    }
                    field = field->next;
                }
            }
            pool_destroy(temp_pool);
            return;
        }

        format_element_children(sb, elem);
    }

    pool_destroy(temp_pool);
}

// ==============================================================================
// Dispatcher-based Markdown Formatting
// ==============================================================================

// global dispatcher (initialized once)
static FormatterDispatcher* md_dispatcher = NULL;
static Pool* dispatcher_pool = NULL;

// default handler for unknown elements
static void format_element_default_reader(MarkdownContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();

    // container elements: just format children
    if (tag_name && (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "document") == 0 ||
                      strcmp(tag_name, "body") == 0 || strcmp(tag_name, "span") == 0)) {
        format_element_children_reader(ctx, elem);
        return;
    }

    // meta elements: skip
    if (tag_name && strcmp(tag_name, "meta") == 0) {
        return;
    }

    // jsx_element: try to output content directly
    if (tag_name && strcmp(tag_name, "jsx_element") == 0) {
        ItemReader content_attr = elem.get_attr("content");
        if (content_attr.isString()) {
            String* jsx_content = content_attr.asString();
            if (jsx_content && jsx_content->chars) {
                ctx.write_text( jsx_content->chars);
                ctx.write_char( ' ');
            }
            return;
        }
    }

    // unknown elements: format children
    format_element_children_reader(ctx, elem);
}

// special handler for list elements (adds newline after)
static void format_list_with_newline_reader(MarkdownContext& ctx, const ElementReader& elem) {
    format_list_reader(ctx, elem);
    ctx.write_char( '\n');
}

// special handler for table elements (adds newline after)
static void format_table_with_newline_reader(MarkdownContext& ctx, const ElementReader& elem) {
    format_table_reader(ctx, elem);
    ctx.write_char( '\n');
}

// special handler for thematic break (hr element - no elem parameter needed)
static void format_thematic_break_reader(MarkdownContext& ctx, const ElementReader& elem) {
    (void)elem; // unused parameter
    format_thematic_break(ctx);
}

// special handler for math elements (still uses Element* temporarily)
static void format_math_element_reader(MarkdownContext& ctx, const ElementReader& elem_reader) {
    Element* elem = (Element*)elem_reader.element();
    String* type_attr = elem_reader.get_string_attr("type");

    if (type_attr && strcmp(type_attr->chars, "block") == 0) {
        format_math_display(ctx.output(), elem);
    } else if (type_attr && strcmp(type_attr->chars, "code") == 0) {
        format_math_code_block(ctx.output(), elem);
    } else {
        format_math_inline(ctx.output(), elem);
    }
}

// ==============================================================================
// Dispatcher Wrapper Functions
// ==============================================================================
// The dispatcher system uses StringBuf* API, so we need wrappers that create
// temporary MarkdownContext instances to call our MarkdownContext-based functions

#define CREATE_DISPATCHER_WRAPPER(func_name) \
    static void func_name##_wrapper(StringBuf* sb, const ElementReader& elem) { \
        Pool* temp_pool = pool_create(); \
        MarkdownContext temp_ctx(temp_pool, sb); \
        func_name(temp_ctx, elem); \
        pool_destroy(temp_pool); \
    }

CREATE_DISPATCHER_WRAPPER(format_heading_reader)
CREATE_DISPATCHER_WRAPPER(format_paragraph_reader)
CREATE_DISPATCHER_WRAPPER(format_blockquote_reader)
CREATE_DISPATCHER_WRAPPER(format_emphasis_reader)
CREATE_DISPATCHER_WRAPPER(format_code_reader)
CREATE_DISPATCHER_WRAPPER(format_link_reader)
CREATE_DISPATCHER_WRAPPER(format_list_with_newline_reader)
CREATE_DISPATCHER_WRAPPER(format_thematic_break_reader)
CREATE_DISPATCHER_WRAPPER(format_table_with_newline_reader)
CREATE_DISPATCHER_WRAPPER(format_math_element_reader)
CREATE_DISPATCHER_WRAPPER(format_element_default_reader)

#undef CREATE_DISPATCHER_WRAPPER

// initialize markdown dispatcher
static void init_markdown_dispatcher(Pool* pool) {
    if (md_dispatcher) return;
    if (!pool) return;

    dispatcher_pool = pool;
    md_dispatcher = dispatcher_create(pool);
    if (!md_dispatcher) return;

    // register all element type handlers
    dispatcher_register(md_dispatcher, "h1", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "h2", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "h3", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "h4", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "h5", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "h6", format_heading_reader_wrapper);
    dispatcher_register(md_dispatcher, "p", format_paragraph_reader_wrapper);
    dispatcher_register(md_dispatcher, "blockquote", format_blockquote_reader_wrapper);
    dispatcher_register(md_dispatcher, "strong", format_emphasis_reader_wrapper);
    dispatcher_register(md_dispatcher, "em", format_emphasis_reader_wrapper);
    dispatcher_register(md_dispatcher, "code", format_code_reader_wrapper);
    dispatcher_register(md_dispatcher, "a", format_link_reader_wrapper);
    dispatcher_register(md_dispatcher, "ul", format_list_with_newline_reader_wrapper);
    dispatcher_register(md_dispatcher, "ol", format_list_with_newline_reader_wrapper);
    dispatcher_register(md_dispatcher, "hr", format_thematic_break_reader_wrapper);
    dispatcher_register(md_dispatcher, "table", format_table_with_newline_reader_wrapper);
    dispatcher_register(md_dispatcher, "math", format_math_element_reader_wrapper);

    // set default handler for unknown elements
    dispatcher_set_default(md_dispatcher, format_element_default_reader_wrapper);
}

// MarkReader-based version: format Lambda element to markdown
static void format_element_reader(MarkdownContext& ctx, const ElementReader& elem_reader) {
    // use dispatcher for element type routing
    if (md_dispatcher) {
        dispatcher_format(md_dispatcher, ctx.output(), elem_reader);
    } else {
        // fallback to default handler if dispatcher not initialized
        format_element_default_reader(ctx, elem_reader);
    }
}

// Format any item to markdown
static void format_item(StringBuf* sb, Item item) {
    TypeId type = get_type_id(item);

    // Only debug when processing elements or strings that might contain math
    if (type == LMD_TYPE_STRING) {
        String* str = item.get_string();
        if (str && (strstr(str->chars, "frac") || strstr(str->chars, "[x") || strchr(str->chars, '$'))) {
            printf("DEBUG format_item: type=%d (STRING), text='%s'\n", type, str->chars);
        }
    } else if (type == LMD_TYPE_ELEMENT) {
        printf("DEBUG format_item: type=%d (ELEMENT), pointer=%lu\n", type, item.element);
    }

    switch (type) {
    case LMD_TYPE_NULL:
        // Skip null items in markdown formatting
        break;
    case LMD_TYPE_STRING: {
        String* str = item.get_string();
        if (str) {
            format_text_with_escape(sb, str, &MARKDOWN_ESCAPE_CONFIG);
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element* elem = item.element;
        if (elem) {
            format_element(sb, elem);
        }
        break;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = item.array;
        if (arr && arr->length > 0) {
            for (long i = 0; i < arr->length; i++) {
                format_item(sb, arr->items[i]);
            }
        }
        break;
    }
    default:
        // For other types, skip or handle gracefully
        break;
    }
}

// MarkReader-based version: format any item to markdown
static void format_item_reader(MarkdownContext& ctx, const ItemReader& item) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        return;
    }

    if (item.isNull()) {
        // skip null items in markdown formatting
        return;
    }

    if (item.isString()) {
        String* str = item.asString();
        if (str) {
            format_text(ctx, str);
        }
    }
    else if (item.isSymbol()) {
        // Symbol items - these are emoji shortcodes (e.g., "smile" for :smile:)
        String* sym = item.asSymbol();
        if (sym && sym->chars) {
            // Output as :shortcode: format for markdown emoji
            ctx.write_char(':');
            ctx.write_text(sym->chars);
            ctx.write_char(':');
        }
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(ctx, elem);
    }
    else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        auto items_iter = arr.items();
        ItemReader child;
        while (items_iter.next(&child)) {
            format_item_reader(ctx, child);
        }
    }
}

// formats markdown to a provided StrBuf
void format_markdown(StringBuf* sb, Item root_item) {
    if (!sb) return;

    // initialize dispatcher on first use
    if (!md_dispatcher) {
        Pool* pool = pool_create();
        if (pool) {
            init_markdown_dispatcher(pool);
        }
    }

    // handle null/empty root item
    if (root_item.item == ITEM_NULL || (root_item.item == ITEM_NULL)) return;

    // create context and use MarkReader API for type-safe traversal
    Pool* pool = pool_create();
    MarkdownContext ctx(pool, sb);
    ItemReader reader(root_item.to_const());
    format_item_reader(ctx, reader);
    pool_destroy(pool);
}
