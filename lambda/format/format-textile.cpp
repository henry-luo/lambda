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

// ==============================================================================
// Map-as-element helpers
// ==============================================================================

static void format_map_children(TextileContext& ctx, const MapReader& mp) {
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

static void format_map_children_raw(TextileContext& ctx, const MapReader& mp) {
    ItemReader children = mp.get("_");
    if (children.isArray()) {
        ArrayReader arr = children.asArray();
        auto it = arr.items();
        ItemReader child;
        while (it.next(&child)) {
            if (child.isString()) {
                format_raw_text(ctx, child.asString());
            }
        }
    }
}

static const char* get_map_attr_cstr(const MapReader& mp, const char* key) {
    ItemReader val = mp.get(key);
    if (val.isString()) {
        String* s = val.asString();
        return (s && s->len > 0) ? s->chars : nullptr;
    }
    return nullptr;
}

// ==============================================================================
// TextileSource traits — unified interface for ElementReader and MapReader
// ==============================================================================

template<typename Source>
struct TextileSource;

template<>
struct TextileSource<ElementReader> {
    static const char* get_attr(const ElementReader& s, const char* k) {
        return s.get_attr_string(k);
    }
    static void children(TextileContext& c, const ElementReader& s) {
        format_element_children_reader(c, s);
    }
    static void children_raw(TextileContext& c, const ElementReader& s) {
        format_element_children_raw_reader(c, s);
    }

    // complex tag handlers that use ElementReader-specific helpers
    static void handle_link(TextileContext& c, const ElementReader& s) {
        format_link_reader(c, s);
    }
    static void handle_image(TextileContext& c, const ElementReader& s) {
        format_image_reader(c, s);
    }
    static void handle_ul(TextileContext& c, const ElementReader& s) {
        format_unordered_list_reader(c, s, 0);
    }
    static void handle_ol(TextileContext& c, const ElementReader& s) {
        format_ordered_list_reader(c, s, 0);
    }
    static void handle_table(TextileContext& c, const ElementReader& s) {
        format_table_reader(c, s);
    }
    static void handle_blockquote(TextileContext& c, const ElementReader& s) {
        format_blockquote_reader(c, s);
    }
    static void handle_pre(TextileContext& c, const ElementReader& s) {
        // check if pre contains a code element
        auto it = s.children();
        ItemReader child;
        bool has_code = false;
        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && strcmp(child_tag, "code") == 0) {
                    format_code_block_reader(c, child_elem);
                    has_code = true;
                    break;
                }
            }
        }
        if (!has_code) {
            c.write_pre_block_start();
            format_element_children_raw_reader(c, s);
            c.write_text("\n\n");
        }
    }
    static void handle_dl(TextileContext& c, const ElementReader& s) {
        auto it = s.children();
        ItemReader child;
        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader ce = child.asElement();
                const char* ct = ce.tagName();
                if (ct && strcmp(ct, "dt") == 0) {
                    c.write_text("- ");
                    format_element_children_reader(c, ce);
                } else if (ct && strcmp(ct, "dd") == 0) {
                    c.write_text(" := ");
                    format_element_children_reader(c, ce);
                    c.write_char('\n');
                }
            }
        }
        c.write_char('\n');
    }
};

template<>
struct TextileSource<MapReader> {
    static const char* get_attr(const MapReader& s, const char* k) {
        return get_map_attr_cstr(s, k);
    }
    static void children(TextileContext& c, const MapReader& s) {
        format_map_children(c, s);
    }
    static void children_raw(TextileContext& c, const MapReader& s) {
        format_map_children_raw(c, s);
    }

    static void handle_link(TextileContext& c, const MapReader& s) {
        c.write_link_start();
        children(c, s);
        c.write_link_middle(nullptr);
        const char* href = get_attr(s, "href");
        if (href) c.write_text(href);
    }
    static void handle_image(TextileContext& c, const MapReader& s) {
        const char* src = get_attr(s, "src");
        const char* alt = get_attr(s, "alt");
        if (src) c.write_image(src, alt);
    }
    static void handle_ul(TextileContext& c, const MapReader& s) {
        ItemReader ch = s.get("_");
        if (ch.isArray()) {
            ArrayReader arr = ch.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                // detect li children (as maps or elements)
                const char* child_tag = nullptr;
                if (child.isMap()) {
                    child_tag = get_map_attr_cstr(child.asMap(), "$");
                } else if (child.isElement()) {
                    child_tag = child.asElement().tagName();
                }
                if (child_tag && strcmp(child_tag, "li") == 0) {
                    c.write_list_marker(false, 0);
                    format_item_reader(c, child);
                    c.write_char('\n');
                }
            }
        }
        c.write_char('\n');
    }
    static void handle_ol(TextileContext& c, const MapReader& s) {
        ItemReader ch = s.get("_");
        if (ch.isArray()) {
            ArrayReader arr = ch.asArray();
            auto it = arr.items();
            ItemReader child;
            while (it.next(&child)) {
                const char* child_tag = nullptr;
                if (child.isMap()) {
                    child_tag = get_map_attr_cstr(child.asMap(), "$");
                } else if (child.isElement()) {
                    child_tag = child.asElement().tagName();
                }
                if (child_tag && strcmp(child_tag, "li") == 0) {
                    c.write_list_marker(true, 0);
                    format_item_reader(c, child);
                    c.write_char('\n');
                }
            }
        }
        c.write_char('\n');
    }
    static void handle_table(TextileContext& c, const MapReader& s) {
        // table support not available for map-as-element; format children
        children(c, s);
    }
    static void handle_blockquote(TextileContext& c, const MapReader& s) {
        c.write_blockquote_start();
        children(c, s);
        c.write_text("\n\n");
    }
    static void handle_pre(TextileContext& c, const MapReader& s) {
        c.write_code_block_start(nullptr);
        children_raw(c, s);
        c.write_text("\n\n");
    }
    static void handle_dl(TextileContext& c, const MapReader& s) {
        // dl support not available for map-as-element; format children
        children(c, s);
    }
};

// ==============================================================================
// Unified tag dispatch
// ==============================================================================

template<typename Source>
static void format_tag_dispatch(TextileContext& ctx, const char* tag_name, const Source& src) {
    using S = TextileSource<Source>;

    // headings h1-h6
    if (is_heading_tag(tag_name)) {
        int level = tag_name[1] - '0';
        const char* la = S::get_attr(src, "level");
        if (la) {
            int l = atoi(la);
            if (l >= 1 && l <= 6) level = l;
        }
        ctx.write_heading_prefix(level);
        S::children(ctx, src);
        ctx.write_text("\n\n");
    }
    else if (strcmp(tag_name, "p") == 0) {
        S::children(ctx, src);
        ctx.write_text("\n\n");
    }
    else if (strcmp(tag_name, "em") == 0 || strcmp(tag_name, "i") == 0) {
        ctx.write_char('_');
        S::children(ctx, src);
        ctx.write_char('_');
    }
    else if (strcmp(tag_name, "strong") == 0 || strcmp(tag_name, "b") == 0) {
        ctx.write_char('*');
        S::children(ctx, src);
        ctx.write_char('*');
    }
    else if (strcmp(tag_name, "u") == 0 || strcmp(tag_name, "ins") == 0) {
        ctx.write_char('+');
        S::children(ctx, src);
        ctx.write_char('+');
    }
    else if (strcmp(tag_name, "s") == 0 || strcmp(tag_name, "del") == 0 || strcmp(tag_name, "strike") == 0) {
        ctx.write_char('-');
        S::children(ctx, src);
        ctx.write_char('-');
    }
    else if (strcmp(tag_name, "code") == 0) {
        ctx.write_char('@');
        S::children_raw(ctx, src);
        ctx.write_char('@');
    }
    else if (strcmp(tag_name, "sup") == 0) {
        ctx.write_char('^');
        S::children(ctx, src);
        ctx.write_char('^');
    }
    else if (strcmp(tag_name, "sub") == 0) {
        ctx.write_char('~');
        S::children(ctx, src);
        ctx.write_char('~');
    }
    else if (strcmp(tag_name, "cite") == 0) {
        ctx.write_text("??");
        S::children(ctx, src);
        ctx.write_text("??");
    }
    else if (strcmp(tag_name, "span") == 0) {
        ctx.write_char('%');
        S::children(ctx, src);
        ctx.write_char('%');
    }
    else if (strcmp(tag_name, "pre") == 0) {
        S::handle_pre(ctx, src);
    }
    else if (strcmp(tag_name, "a") == 0) {
        S::handle_link(ctx, src);
    }
    else if (strcmp(tag_name, "img") == 0) {
        S::handle_image(ctx, src);
    }
    else if (strcmp(tag_name, "ul") == 0) {
        S::handle_ul(ctx, src);
    }
    else if (strcmp(tag_name, "ol") == 0) {
        S::handle_ol(ctx, src);
    }
    else if (strcmp(tag_name, "li") == 0) {
        // list items handled by parent list
        S::children(ctx, src);
    }
    else if (strcmp(tag_name, "table") == 0) {
        S::handle_table(ctx, src);
    }
    else if (strcmp(tag_name, "tr") == 0 || strcmp(tag_name, "td") == 0 ||
             strcmp(tag_name, "th") == 0 || strcmp(tag_name, "thead") == 0 ||
             strcmp(tag_name, "tbody") == 0) {
        S::children(ctx, src);
    }
    else if (strcmp(tag_name, "blockquote") == 0) {
        S::handle_blockquote(ctx, src);
    }
    else if (strcmp(tag_name, "br") == 0) {
        ctx.write_char('\n');
    }
    else if (strcmp(tag_name, "hr") == 0) {
        ctx.write_text("\n---\n\n");
    }
    else if (strcmp(tag_name, "dl") == 0) {
        S::handle_dl(ctx, src);
    }
    else if (strcmp(tag_name, "dt") == 0 || strcmp(tag_name, "dd") == 0) {
        S::children(ctx, src);
    }
    else {
        // container or unknown — just format children
        S::children(ctx, src);
    }
}

// ==============================================================================
// Entry dispatchers
// ==============================================================================

// Format element
static void format_element_reader(TextileContext& ctx, const ElementReader& elem) {
    FormatterContextCpp::RecursionGuard guard(ctx);
    if (guard.exceeded()) {
        log_debug("textile: Maximum recursion depth reached");
        return;
    }

    const char* tag_name = elem.tagName();
    if (!tag_name) {
        format_element_children_reader(ctx, elem);
        return;
    }

    format_tag_dispatch(ctx, tag_name, elem);
}

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
        // map-as-element: extract tag from "$" key and dispatch
        MapReader mp = item.asMap();
        const char* tag_name = get_map_attr_cstr(mp, "$");
        if (tag_name) {
            FormatterContextCpp::RecursionGuard guard(ctx);
            if (!guard.exceeded()) {
                format_tag_dispatch(ctx, tag_name, mp);
            }
        } else {
            format_map_children(ctx, mp);
        }
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
