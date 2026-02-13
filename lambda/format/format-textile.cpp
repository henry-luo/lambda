/**
 * Textile formatter - converts Lambda document tree to Textile markup
 *
 * Textile syntax reference:
 * - Headings: h1. h2. h3. etc.
 * - Bold: *text*
 * - Italic: _text_
 * - Underline: +text+
 * - Strikethrough: -text-
 * - Code: @text@
 * - Superscript: ^text^
 * - Subscript: ~text~
 * - Links: "text":url
 * - Images: !url!
 * - Lists: * unordered, # ordered
 * - Code blocks: bc. or pre.
 * - Blockquotes: bq.
 * - Tables: |cell|cell|
 */

#include "format.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include <string.h>
#include <ctype.h>

// Textile formatter context
class TextileContext : public FormatterContextCpp {
public:
    TextileContext(Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , list_depth_(0)
        , in_table_(false)
        , in_code_block_(false)
    {}

    // Textile heading: h1. Heading
    inline void write_heading_prefix(int level) {
        write_char('h');
        char level_char = '0' + (level < 6 ? level : 6);
        write_char(level_char);
        write_text(". ");
    }

    // Textile unordered list marker: * or ** or *** etc.
    inline void write_list_marker(bool ordered, int depth) {
        for (int i = 0; i <= depth; i++) {
            write_char(ordered ? '#' : '*');
        }
        write_char(' ');
    }

    // Textile code block: bc. or bc.. for extended
    inline void write_code_block_start(const char* lang = nullptr) {
        write_text("bc.");
        if (lang && lang[0] != '\0') {
            write_char('(');
            write_text(lang);
            write_char(')');
        }
        write_char(' ');
    }

    // Textile preformatted block: pre.
    inline void write_pre_block_start() {
        write_text("pre. ");
    }

    // Textile blockquote: bq.
    inline void write_blockquote_start() {
        write_text("bq. ");
    }

    // Textile link: "text":url or "text(title)":url
    inline void write_link_start() {
        write_char('"');
    }

    inline void write_link_middle(const char* title = nullptr) {
        if (title && title[0] != '\0') {
            write_char('(');
            write_text(title);
            write_char(')');
        }
        write_text("\":");
    }

    // Textile image: !url! or !url(alt)!
    inline void write_image(const char* url, const char* alt = nullptr) {
        write_char('!');
        write_text(url);
        if (alt && alt[0] != '\0') {
            write_char('(');
            write_text(alt);
            write_char(')');
        }
        write_char('!');
    }

    // Table support
    inline void write_table_cell_start(bool is_header) {
        if (is_header) {
            write_text("|_. ");
        } else {
            write_char('|');
        }
    }

    // State tracking
    int list_depth() const { return list_depth_; }
    void enter_list() { list_depth_++; }
    void exit_list() { if (list_depth_ > 0) list_depth_--; }

    bool in_table() const { return in_table_; }
    void set_in_table(bool in_table) { in_table_ = in_table; }

    bool in_code_block() const { return in_code_block_; }
    void set_in_code_block(bool in_code) { in_code_block_ = in_code; }

private:
    int list_depth_;
    bool in_table_;
    bool in_code_block_;
};

// Forward declarations
static void format_item_reader(TextileContext& ctx, const ItemReader& item);
static void format_element_reader(TextileContext& ctx, const ElementReader& elem);
static void format_element_children_reader(TextileContext& ctx, const ElementReader& elem);

// Format raw text (no escaping - for code blocks, etc.)
static void format_raw_text(TextileContext& ctx, String* str) {
    if (!str || str->len == 0) return;
    ctx.write_text(str);
}

// Escape config for Textile special characters
static const TextEscapeConfig TEXTILE_ESCAPE_CONFIG = {
    "*_+-@^~\"!|[]{}()#<>=",  // special chars in Textile
    false,                      // use_backslash_escape - use == for literal
    nullptr                     // no custom escape function
};

// Format plain text (escape textile markup)
static void format_text(TextileContext& ctx, String* str) {
    if (!str || str->len == 0) return;
    // For now, output directly - Textile escaping is complex
    // In production, would need proper escaping with ==text== for literal output
    ctx.write_text(str);
}

// Format element children
static void format_element_children_reader(TextileContext& ctx, const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        format_item_reader(ctx, child);
    }
}

// Format element children raw (no escaping)
static void format_element_children_raw_reader(TextileContext& ctx, const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            format_raw_text(ctx, str);
        } else {
            format_item_reader(ctx, child);
        }
    }
}

// Format heading element
static void format_heading_reader(TextileContext& ctx, const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    int level = 1;

    // Try to get level from attribute (Pandoc schema)
    ItemReader level_attr = elem.get_attr("level");
    if (level_attr.isString()) {
        String* level_str = level_attr.asString();
        if (level_str && level_str->len > 0) {
            level = (int)str_to_int64_default(level_str->chars, strlen(level_str->chars), 0);
            if (level < 1) level = 1;
            if (level > 6) level = 6;
        }
    } else if (strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        // Fallback: parse level from tag name
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }

    ctx.write_heading_prefix(level);
    format_element_children_reader(ctx, elem);
    ctx.write_text("\n\n");
}

// Format link element
static void format_link_reader(TextileContext& ctx, const ElementReader& elem) {
    ItemReader href = elem.get_attr("href");
    ItemReader title = elem.get_attr("title");

    ctx.write_link_start();
    format_element_children_reader(ctx, elem);

    const char* title_str = nullptr;
    if (title.isString()) {
        String* t = title.asString();
        if (t && t->len > 0) {
            title_str = t->chars;
        }
    }
    ctx.write_link_middle(title_str);

    if (href.isString()) {
        String* href_str = href.asString();
        if (href_str && href_str->len > 0) {
            ctx.write_text(href_str);
        }
    }
}

// Format image element
static void format_image_reader(TextileContext& ctx, const ElementReader& elem) {
    ItemReader src = elem.get_attr("src");
    ItemReader alt = elem.get_attr("alt");

    const char* src_str = nullptr;
    const char* alt_str = nullptr;

    if (src.isString()) {
        String* s = src.asString();
        if (s && s->len > 0) {
            src_str = s->chars;
        }
    }

    if (alt.isString()) {
        String* a = alt.asString();
        if (a && a->len > 0) {
            alt_str = a->chars;
        }
    }

    if (src_str) {
        ctx.write_image(src_str, alt_str);
    }
}

// Format list item
static void format_list_item_reader(TextileContext& ctx, const ElementReader& elem, int depth, bool is_ordered) {
    ctx.write_list_marker(is_ordered, depth);
    format_element_children_reader(ctx, elem);
    ctx.write_char('\n');
}

// Forward declaration for mutual recursion
static void format_ordered_list_reader(TextileContext& ctx, const ElementReader& elem, int depth);

// Format unordered list
static void format_unordered_list_reader(TextileContext& ctx, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "li") == 0) {
                format_list_item_reader(ctx, child_elem, depth, false);
            } else if (tag && strcmp(tag, "ul") == 0) {
                format_unordered_list_reader(ctx, child_elem, depth + 1);
            } else if (tag && strcmp(tag, "ol") == 0) {
                format_ordered_list_reader(ctx, child_elem, depth + 1);
            }
        }
    }

    if (depth == 0) ctx.write_char('\n');
}

// Format ordered list
static void format_ordered_list_reader(TextileContext& ctx, const ElementReader& elem, int depth) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "li") == 0) {
                format_list_item_reader(ctx, child_elem, depth, true);
            } else if (tag && strcmp(tag, "ul") == 0) {
                format_unordered_list_reader(ctx, child_elem, depth + 1);
            } else if (tag && strcmp(tag, "ol") == 0) {
                format_ordered_list_reader(ctx, child_elem, depth + 1);
            }
        }
    }

    if (depth == 0) ctx.write_char('\n');
}

// Format code block
static void format_code_block_reader(TextileContext& ctx, const ElementReader& elem) {
    // Check for language attribute
    ItemReader lang_attr = elem.get_attr("class");
    const char* lang = nullptr;
    if (lang_attr.isString()) {
        String* lang_str = lang_attr.asString();
        if (lang_str && lang_str->len > 0) {
            // Extract language from class like "language-python"
            if (strncmp(lang_str->chars, "language-", 9) == 0) {
                lang = lang_str->chars + 9;
            } else {
                lang = lang_str->chars;
            }
        }
    }

    ctx.write_code_block_start(lang);
    format_element_children_raw_reader(ctx, elem);
    ctx.write_text("\n\n");
}

// Table formatting context
typedef struct {
    bool table_started;
    bool in_header;
    TextileContext* formatter_ctx;
} TextileTableContext;

// Callback for Textile table row formatting
static void format_textile_table_row(
    StringBuf* sb,
    const ElementReader& row,
    int row_idx,
    bool is_header,
    void* ctx
) {
    TextileTableContext* context = (TextileTableContext*)ctx;
    TextileContext* fmt_ctx = context->formatter_ctx;

    // Format cells
    auto it = row.children();
    ItemReader cell_item;
    while (it.next(&cell_item)) {
        if (cell_item.isElement()) {
            ElementReader cell = cell_item.asElement();
            const char* tag = cell.tagName();
            bool cell_is_header = is_header || (tag && strcmp(tag, "th") == 0);

            if (cell_is_header) {
                stringbuf_append_str(sb, "|_. ");
            } else {
                stringbuf_append_char(sb, '|');
            }

            format_element_children_reader(*fmt_ctx, cell);
        }
    }
    stringbuf_append_str(sb, "|\n");
}

// Format table
static void format_table_reader(TextileContext& ctx, const ElementReader& elem) {
    TextileTableContext context = {false, false, &ctx};
    iterate_table_rows(elem, ctx.output(), format_textile_table_row, &context);
    ctx.write_char('\n');
}

// Format blockquote
static void format_blockquote_reader(TextileContext& ctx, const ElementReader& elem) {
    ctx.write_blockquote_start();
    format_element_children_reader(ctx, elem);
    ctx.write_text("\n\n");
}

// Format element
static void format_element_reader(TextileContext& ctx, const ElementReader& elem) {
    // RAII recursion guard
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        printf("WARNING: Maximum recursion depth reached in Textile formatter\n");
        return;
    }

    const char* tag_name = elem.tagName();
    if (!tag_name) {
        format_element_children_reader(ctx, elem);
        return;
    }

    // Handle different element types
    if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && isdigit(tag_name[1])) {
        format_heading_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "p") == 0) {
        format_element_children_reader(ctx, elem);
        ctx.write_text("\n\n");
    }
    else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        ctx.write_char('_');
        format_element_children_reader(ctx, elem);
        ctx.write_char('_');
    }
    else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        ctx.write_char('*');
        format_element_children_reader(ctx, elem);
        ctx.write_char('*');
    }
    else if (strcmp(tag_name, "u") == 0 || strcmp(tag_name, "ins") == 0) {
        ctx.write_char('+');
        format_element_children_reader(ctx, elem);
        ctx.write_char('+');
    }
    else if (strcmp(tag_name, "s") == 0 || strcmp(tag_name, "del") == 0 || strcmp(tag_name, "strike") == 0) {
        ctx.write_char('-');
        format_element_children_reader(ctx, elem);
        ctx.write_char('-');
    }
    else if (strcmp(tag_name, "code") == 0) {
        ctx.write_char('@');
        format_element_children_raw_reader(ctx, elem);
        ctx.write_char('@');
    }
    else if (strcmp(tag_name, "sup") == 0) {
        ctx.write_char('^');
        format_element_children_reader(ctx, elem);
        ctx.write_char('^');
    }
    else if (strcmp(tag_name, "sub") == 0) {
        ctx.write_char('~');
        format_element_children_reader(ctx, elem);
        ctx.write_char('~');
    }
    else if (strcmp(tag_name, "cite") == 0) {
        ctx.write_text("??");
        format_element_children_reader(ctx, elem);
        ctx.write_text("??");
    }
    else if (strcmp(tag_name, "span") == 0) {
        ctx.write_char('%');
        format_element_children_reader(ctx, elem);
        ctx.write_char('%');
    }
    else if (strcmp(tag_name, "pre") == 0) {
        // Check if it contains a code element
        auto it = elem.children();
        ItemReader child;
        bool has_code = false;
        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && strcmp(child_tag, "code") == 0) {
                    format_code_block_reader(ctx, child_elem);
                    has_code = true;
                    break;
                }
            }
        }
        if (!has_code) {
            ctx.write_pre_block_start();
            format_element_children_raw_reader(ctx, elem);
            ctx.write_text("\n\n");
        }
    }
    else if (strcmp(tag_name, "a") == 0) {
        format_link_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "img") == 0) {
        format_image_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "ul") == 0) {
        format_unordered_list_reader(ctx, elem, 0);
    }
    else if (strcmp(tag_name, "ol") == 0) {
        format_ordered_list_reader(ctx, elem, 0);
    }
    else if (strcmp(tag_name, "li") == 0) {
        // List items are handled by their parent list
        format_element_children_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "table") == 0) {
        format_table_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "tr") == 0 || strcmp(tag_name, "td") == 0 ||
             strcmp(tag_name, "th") == 0 || strcmp(tag_name, "thead") == 0 ||
             strcmp(tag_name, "tbody") == 0) {
        // Table elements are handled by their parent table
        format_element_children_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "blockquote") == 0) {
        format_blockquote_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "br") == 0) {
        ctx.write_char('\n');
    }
    else if (strcmp(tag_name, "hr") == 0) {
        ctx.write_text("\n---\n\n");
    }
    else if (strcmp(tag_name, "dl") == 0) {
        // Definition list
        auto it = elem.children();
        ItemReader child;
        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && strcmp(child_tag, "dt") == 0) {
                    ctx.write_text("- ");
                    format_element_children_reader(ctx, child_elem);
                } else if (child_tag && strcmp(child_tag, "dd") == 0) {
                    ctx.write_text(" := ");
                    format_element_children_reader(ctx, child_elem);
                    ctx.write_char('\n');
                }
            }
        }
        ctx.write_char('\n');
    }
    else if (strcmp(tag_name, "dt") == 0 || strcmp(tag_name, "dd") == 0) {
        // Handled by dl parent
        format_element_children_reader(ctx, elem);
    }
    else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "body") == 0 ||
             strcmp(tag_name, "article") == 0 || strcmp(tag_name, "section") == 0 ||
             strcmp(tag_name, "div") == 0 || strcmp(tag_name, "main") == 0) {
        // Container elements - just format children
        format_element_children_reader(ctx, elem);
    }
    else {
        // Unknown element - just format children
        format_element_children_reader(ctx, elem);
    }
}

// Forward declaration for Map handling
static void format_map_as_element_reader(TextileContext& ctx, const MapReader& mp);

// Format item
static void format_item_reader(TextileContext& ctx, const ItemReader& item) {
    if (item.isString()) {
        String* str = item.asString();
        format_text(ctx, str);
    }
    else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element_reader(ctx, elem);
    }
    else if (item.isMap()) {
        // Handle Maps that represent Element-like structures (from JSON)
        // These have "$" key for tag name and "_" key for children
        MapReader mp = item.asMap();
        format_map_as_element_reader(ctx, mp);
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

// Format a Map that represents an Element (from JSON parsing)
// Maps with "$" key represent elements: {"$":"tagname", "_":[children], ...attrs}
static void format_map_as_element_reader(TextileContext& ctx, const MapReader& mp) {
    // RAII recursion guard
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        log_debug("format_textile: Maximum recursion depth reached in map_as_element");
        return;
    }

    // Get tag name from "$" key
    ItemReader tag_item = mp.get("$");
    const char* tag_name = nullptr;
    if (tag_item.isString()) {
        String* tag_str = tag_item.asString();
        if (tag_str && tag_str->len > 0) {
            tag_name = tag_str->chars;
        }
    }

    if (!tag_name) {
        // No tag name, just format children if any
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        return;
    }

    // Handle different element types by tag name
    // Headings
    if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && isdigit(tag_name[1])) {
        int level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;

        // Check for "level" attribute
        ItemReader level_attr = mp.get("level");
        if (level_attr.isString()) {
            String* level_str = level_attr.asString();
            if (level_str && level_str->len > 0) {
                int attr_level = (int)str_to_int64_default(level_str->chars, strlen(level_str->chars), 0);
                if (attr_level >= 1 && attr_level <= 6) {
                    level = attr_level;
                }
            }
        }

        ctx.write_heading_prefix(level);
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_text("\n\n");
    }
    // Paragraphs
    else if (strcmp(tag_name, "p") == 0) {
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_text("\n\n");
    }
    // Emphasis (italic)
    else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        ctx.write_char('_');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('_');
    }
    // Strong (bold)
    else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        ctx.write_char('*');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('*');
    }
    // Inline code
    else if (strcmp(tag_name, "code") == 0) {
        ctx.write_char('@');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                if (child.isString()) {
                    String* str = child.asString();
                    format_raw_text(ctx, str);
                } else {
                    format_item_reader(ctx, child);
                }
            }
        }
        ctx.write_char('@');
    }
    // Links
    else if (strcmp(tag_name, "a") == 0) {
        ItemReader href = mp.get("href");

        ctx.write_link_start();
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_link_middle(nullptr);

        if (href.isString()) {
            String* href_str = href.asString();
            if (href_str && href_str->len > 0) {
                ctx.write_text(href_str);
            }
        }
    }
    // Images
    else if (strcmp(tag_name, "img") == 0) {
        ItemReader src = mp.get("src");
        ItemReader alt = mp.get("alt");

        const char* src_str = nullptr;
        const char* alt_str = nullptr;

        if (src.isString()) {
            String* s = src.asString();
            if (s && s->len > 0) {
                src_str = s->chars;
            }
        }
        if (alt.isString()) {
            String* a = alt.asString();
            if (a && a->len > 0) {
                alt_str = a->chars;
            }
        }

        if (src_str) {
            ctx.write_image(src_str, alt_str);
        }
    }
    // Unordered list
    else if (strcmp(tag_name, "ul") == 0) {
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                if (child.isMap()) {
                    MapReader child_mp = child.asMap();
                    ItemReader child_tag = child_mp.get("$");
                    if (child_tag.isString()) {
                        String* ct = child_tag.asString();
                        if (ct && strcmp(ct->chars, "li") == 0) {
                            ctx.write_list_marker(false, 0);
                            ItemReader li_children = child_mp.get("_");
                            if (li_children.isArray()) {
                                ArrayReader li_arr = li_children.asArray();
                                auto li_it = li_arr.items();
                                ItemReader li_child;
                                while (li_it.next(&li_child)) {
                                    format_item_reader(ctx, li_child);
                                }
                            }
                            ctx.write_char('\n');
                        }
                    }
                }
            }
        }
        ctx.write_char('\n');
    }
    // Ordered list
    else if (strcmp(tag_name, "ol") == 0) {
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                if (child.isMap()) {
                    MapReader child_mp = child.asMap();
                    ItemReader child_tag = child_mp.get("$");
                    if (child_tag.isString()) {
                        String* ct = child_tag.asString();
                        if (ct && strcmp(ct->chars, "li") == 0) {
                            ctx.write_list_marker(true, 0);
                            ItemReader li_children = child_mp.get("_");
                            if (li_children.isArray()) {
                                ArrayReader li_arr = li_children.asArray();
                                auto li_it = li_arr.items();
                                ItemReader li_child;
                                while (li_it.next(&li_child)) {
                                    format_item_reader(ctx, li_child);
                                }
                            }
                            ctx.write_char('\n');
                        }
                    }
                }
            }
        }
        ctx.write_char('\n');
    }
    // Blockquote
    else if (strcmp(tag_name, "blockquote") == 0) {
        ctx.write_blockquote_start();
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_text("\n\n");
    }
    // Pre/Code blocks
    else if (strcmp(tag_name, "pre") == 0) {
        ctx.write_code_block_start(nullptr);
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                if (child.isString()) {
                    String* str = child.asString();
                    format_raw_text(ctx, str);
                } else if (child.isMap()) {
                    // Handle nested code element
                    MapReader code_mp = child.asMap();
                    ItemReader code_tag = code_mp.get("$");
                    if (code_tag.isString()) {
                        String* ct = code_tag.asString();
                        if (ct && strcmp(ct->chars, "code") == 0) {
                            ItemReader code_children = code_mp.get("_");
                            if (code_children.isArray()) {
                                ArrayReader code_arr = code_children.asArray();
                                auto code_it = code_arr.items();
                                ItemReader code_child;
                                while (code_it.next(&code_child)) {
                                    if (code_child.isString()) {
                                        String* str = code_child.asString();
                                        format_raw_text(ctx, str);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        ctx.write_text("\n\n");
    }
    // Horizontal rule
    else if (strcmp(tag_name, "hr") == 0) {
        ctx.write_text("\n---\n\n");
    }
    // Break
    else if (strcmp(tag_name, "br") == 0) {
        ctx.write_char('\n');
    }
    // Underline/insert
    else if (strcmp(tag_name, "u") == 0 || strcmp(tag_name, "ins") == 0) {
        ctx.write_char('+');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('+');
    }
    // Strikethrough/delete
    else if (strcmp(tag_name, "s") == 0 || strcmp(tag_name, "del") == 0 || strcmp(tag_name, "strike") == 0) {
        ctx.write_char('-');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('-');
    }
    // Superscript
    else if (strcmp(tag_name, "sup") == 0) {
        ctx.write_char('^');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('^');
    }
    // Subscript
    else if (strcmp(tag_name, "sub") == 0) {
        ctx.write_char('~');
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
        ctx.write_char('~');
    }
    // Container elements - just format children
    else if (strcmp(tag_name, "doc") == 0 || strcmp(tag_name, "body") == 0 ||
             strcmp(tag_name, "article") == 0 || strcmp(tag_name, "section") == 0 ||
             strcmp(tag_name, "div") == 0 || strcmp(tag_name, "main") == 0 ||
             strcmp(tag_name, "header") == 0 || strcmp(tag_name, "footer") == 0) {
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
    }
    // Unknown element - just format children
    else {
        ItemReader children = mp.get("_");
        if (children.isArray()) {
            ArrayReader arr = children.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                format_item_reader(ctx, child);
            }
        }
    }
}

// Main Textile formatting function (StrBuf version)
void format_textile(StringBuf* sb, Item root_item) {
    if (!sb) return;

    // Handle null/empty root item
    if (root_item.item == ITEM_NULL) return;

    // Create context
    Pool* pool = pool_create();
    TextileContext ctx(pool, sb);

    // Use MarkReader API
    ItemReader root(root_item.to_const());
    format_item_reader(ctx, root);

    pool_destroy(pool);
}

// Main Textile formatting function (String version)
String* format_textile_string(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    format_textile(sb, root_item);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}
