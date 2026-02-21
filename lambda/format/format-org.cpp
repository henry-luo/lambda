#include "format.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include <string.h>

// Forward declarations
static void format_inline_element(OrgContext& ctx, const ElementReader& elem);
static void format_org_element(OrgContext& ctx, const ElementReader& elem);
static void format_org_item(OrgContext& ctx, const ItemReader& item);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Get text content of a named child element as cstring (returns nullptr if not found)
static const char* get_child_text(const ElementReader& elem, const char* tag) {
    ElementReader child = elem.findChildElement(tag);
    if (!child.isValid()) return nullptr;

    // Extract text from the child element's first string child
    for (int64_t i = 0; i < child.childCount(); i++) {
        ItemReader item = child.childAt(i);
        if (item.isString()) {
            return item.cstring();
        }
        // If child is an element, recurse one level
        if (item.isElement()) {
            ElementReader sub = item.asElement();
            for (int64_t j = 0; j < sub.childCount(); j++) {
                ItemReader sub_item = sub.childAt(j);
                if (sub_item.isString()) return sub_item.cstring();
            }
        }
    }
    return nullptr;
}

// Write all string children, recursing into child elements for inline formatting
static void format_children_inline(OrgContext& ctx, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            ctx.write_text(child.cstring());
        } else if (child.isElement()) {
            format_inline_element(ctx, child.asElement());
        }
    }
}

// Write only string children (no recursion into child elements)
static void format_string_children(OrgContext& ctx, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            ctx.write_text(child.cstring());
        }
    }
}

// Format children wrapped with symmetric delimiters: *bold*, /italic/, etc.
static void format_delimited_inline(OrgContext& ctx, const ElementReader& elem, const char* delim) {
    ctx.write_text(delim);
    format_string_children(ctx, elem);
    ctx.write_text(delim);
}

// ---------------------------------------------------------------------------
// Heading
// ---------------------------------------------------------------------------

static void format_heading(OrgContext& ctx, const ElementReader& elem) {
    int level = 1;
    const char* title = nullptr;
    const char* todo  = nullptr;
    const char* tags  = nullptr;

    // Extract level / TODO / title / tags from child elements
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        const char* tag = ce.tagName();
        if (!tag) continue;

        if (strcmp(tag, "level") == 0) {
            const char* lstr = get_child_text(elem, "level");
            if (lstr) level = (int)str_to_int64_default(lstr, strlen(lstr), 0);
        } else if (strcmp(tag, "todo") == 0) {
            todo = get_child_text(elem, "todo");
        } else if (strcmp(tag, "title") == 0) {
            title = get_child_text(elem, "title");
        } else if (strcmp(tag, "tags") == 0) {
            tags = get_child_text(elem, "tags");
        }
    }

    // Stars
    for (int i = 0; i < level; i++) ctx.write_char('*');
    ctx.write_char(' ');

    if (todo)  { ctx.write_text(todo); ctx.write_char(' '); }
    if (title) ctx.write_text(title);
    if (tags)  { ctx.write_char(' '); ctx.write_text(tags); }

    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Paragraph (block-level)
// ---------------------------------------------------------------------------

static void format_paragraph(OrgContext& ctx, const ElementReader& elem) {
    format_children_inline(ctx, elem);
    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Inline element dispatch (bold, italic, link, math, etc.)
// ---------------------------------------------------------------------------

static void format_inline_element(OrgContext& ctx, const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return;

    // --- simple delimited styles ---
    if (strcmp(tag, "bold") == 0)          { format_delimited_inline(ctx, elem, "*");  return; }
    if (strcmp(tag, "italic") == 0)        { format_delimited_inline(ctx, elem, "/");  return; }
    if (strcmp(tag, "verbatim") == 0)      { format_delimited_inline(ctx, elem, "=");  return; }
    if (strcmp(tag, "code") == 0)          { format_delimited_inline(ctx, elem, "~");  return; }
    if (strcmp(tag, "strikethrough") == 0) { format_delimited_inline(ctx, elem, "+");  return; }
    if (strcmp(tag, "underline") == 0)     { format_delimited_inline(ctx, elem, "_");  return; }

    // --- plain text ---
    if (strcmp(tag, "plain_text") == 0) {
        format_string_children(ctx, elem);
        return;
    }

    // --- link ---
    if (strcmp(tag, "link") == 0) {
        ctx.write_text("[[");
        const char* url  = get_child_text(elem, "url");
        const char* desc = get_child_text(elem, "description");
        if (url) ctx.write_text(url);
        if (desc) { ctx.write_text("]["); ctx.write_text(desc); }
        ctx.write_text("]]");
        return;
    }

    // --- footnote reference: [fn:name] ---
    if (strcmp(tag, "footnote_reference") == 0) {
        ctx.write_text("[fn:");
        const char* name = get_child_text(elem, "name");
        if (name) ctx.write_text(name);
        ctx.write_text("]");
        return;
    }

    // --- inline footnote: [fn:name:definition] ---
    if (strcmp(tag, "inline_footnote") == 0) {
        ctx.write_text("[fn:");
        const char* name = get_child_text(elem, "name");
        if (name && strlen(name) > 0) ctx.write_text(name);
        ctx.write_text(":");

        ElementReader def_elem = elem.findChildElement("definition");
        if (def_elem.isValid()) format_children_inline(ctx, def_elem);

        ctx.write_text("]");
        return;
    }

    // --- inline math ---
    if (strcmp(tag, "inline_math") == 0) {
        const char* raw = get_child_text(elem, "raw_content");
        bool latex_style = raw && strchr(raw, '\\');
        if (latex_style) {
            ctx.write_text("\\(");
            if (raw) ctx.write_text(raw);
            ctx.write_text("\\)");
        } else {
            ctx.write_text("$");
            if (raw) ctx.write_text(raw);
            ctx.write_text("$");
        }
        return;
    }

    // --- display math ---
    if (strcmp(tag, "display_math") == 0) {
        const char* raw = get_child_text(elem, "raw_content");
        bool latex_style = raw && (strchr(raw, '\\') || strlen(raw) > 20);
        if (latex_style) {
            ctx.write_text("\\[");
            if (raw) ctx.write_text(raw);
            ctx.write_text("\\]");
        } else {
            ctx.write_text("$$");
            if (raw) ctx.write_text(raw);
            ctx.write_text("$$");
        }
        return;
    }

    // --- timestamp ---
    if (strcmp(tag, "timestamp") == 0) {
        // Try extracting from first string child
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader c = elem.childAt(i);
            if (c.isString()) { ctx.write_text(c.cstring()); return; }
        }
        // Recurse into child elements
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader c = elem.childAt(i);
            if (c.isElement()) {
                ElementReader sub = c.asElement();
                for (int64_t j = 0; j < sub.childCount(); j++) {
                    ItemReader sj = sub.childAt(j);
                    if (sj.isString()) { ctx.write_text(sj.cstring()); return; }
                }
            }
        }
        return;
    }

    // --- text_content (container of inline elements) ---
    if (strcmp(tag, "text_content") == 0) {
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader c = elem.childAt(i);
            if (c.isElement()) format_inline_element(ctx, c.asElement());
        }
        return;
    }

    // --- fallback: treat as mixed inline content ---
    format_children_inline(ctx, elem);
}

// ---------------------------------------------------------------------------
// List item
// ---------------------------------------------------------------------------

static void format_list_item(OrgContext& ctx, const ElementReader& elem) {
    format_string_children(ctx, elem);
    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Block elements (code, quote, example, verse, center)
// ---------------------------------------------------------------------------

// Helper: emit all "content" child elements as lines
static void format_all_content_lines(OrgContext& ctx, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        if (ce.tagName() && strcmp(ce.tagName(), "content") == 0) {
            for (int64_t j = 0; j < ce.childCount(); j++) {
                ItemReader cj = ce.childAt(j);
                if (cj.isString()) {
                    ctx.write_text(cj.cstring());
                    ctx.write_char('\n');
                }
            }
        }
    }
}

// Helper: format paragraphs inside a block
static void format_contained_paragraphs(OrgContext& ctx, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        if (ce.tagName() && strcmp(ce.tagName(), "paragraph") == 0) {
            format_paragraph(ctx, ce);
        }
    }
}

static void format_code_block(OrgContext& ctx, const ElementReader& elem) {
    const char* lang = get_child_text(elem, "language");
    ctx.write_text("#+BEGIN_SRC");
    if (lang) { ctx.write_char(' '); ctx.write_text(lang); }
    ctx.write_char('\n');
    format_all_content_lines(ctx, elem);
    ctx.write_text("#+END_SRC\n");
}

static void format_quote_block(OrgContext& ctx, const ElementReader& elem) {
    ctx.write_text("#+BEGIN_QUOTE\n");
    format_contained_paragraphs(ctx, elem);
    ctx.write_text("#+END_QUOTE\n");
}

static void format_example_block(OrgContext& ctx, const ElementReader& elem) {
    ctx.write_text("#+BEGIN_EXAMPLE\n");
    format_all_content_lines(ctx, elem);
    ctx.write_text("#+END_EXAMPLE\n");
}

static void format_verse_block(OrgContext& ctx, const ElementReader& elem) {
    ctx.write_text("#+BEGIN_VERSE\n");
    format_all_content_lines(ctx, elem);
    ctx.write_text("#+END_VERSE\n");
}

static void format_center_block(OrgContext& ctx, const ElementReader& elem) {
    ctx.write_text("#+BEGIN_CENTER\n");
    format_contained_paragraphs(ctx, elem);
    ctx.write_text("#+END_CENTER\n");
}

// ---------------------------------------------------------------------------
// Drawer
// ---------------------------------------------------------------------------

static void format_drawer(OrgContext& ctx, const ElementReader& elem) {
    const char* name = get_child_text(elem, "name");
    ctx.write_char(':');
    if (name) ctx.write_text(name);
    ctx.write_text(":\n");
    format_all_content_lines(ctx, elem);
    ctx.write_text(":END:\n");
}

// ---------------------------------------------------------------------------
// Scheduling (SCHEDULED, DEADLINE, CLOSED)
// ---------------------------------------------------------------------------

static void format_scheduling(OrgContext& ctx, const ElementReader& elem) {
    const char* keyword   = get_child_text(elem, "keyword");
    const char* timestamp = get_child_text(elem, "timestamp");

    ctx.write_text("  "); // indent

    if (keyword) {
        if (strcmp(keyword, "scheduled") == 0)      ctx.write_text("SCHEDULED: ");
        else if (strcmp(keyword, "deadline") == 0)   ctx.write_text("DEADLINE: ");
        else if (strcmp(keyword, "closed") == 0)     ctx.write_text("CLOSED: ");
    }

    if (timestamp) ctx.write_text(timestamp);
    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Footnote definition
// ---------------------------------------------------------------------------

static void format_footnote_definition(OrgContext& ctx, const ElementReader& elem) {
    const char* name = get_child_text(elem, "name");

    ctx.write_text("[fn:");
    if (name) ctx.write_text(name);
    ctx.write_text("] ");

    ElementReader content = elem.findChildElement("content");
    if (content.isValid()) format_children_inline(ctx, content);

    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Directive
// ---------------------------------------------------------------------------

static void format_directive(OrgContext& ctx, const ElementReader& elem) {
    format_string_children(ctx, elem);
    ctx.write_char('\n');
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

static void format_table_cell(OrgContext& ctx, const ElementReader& elem) {
    format_string_children(ctx, elem);
}

static void format_table_row(OrgContext& ctx, const ElementReader& elem, bool is_header) {
    ctx.write_char('|');

    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        if (ce.tagName() && strcmp(ce.tagName(), "table_cell") == 0) {
            ctx.write_char(' ');
            format_table_cell(ctx, ce);
            ctx.write_text(" |");
        }
    }
    ctx.write_char('\n');

    // Separator after header row
    if (is_header) {
        ctx.write_char('|');
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader child = elem.childAt(i);
            if (child.isElement()) {
                ctx.write_text("---------|");
            }
        }
        ctx.write_char('\n');
    }
}

static void format_table(OrgContext& ctx, const ElementReader& elem) {
    bool first_row = true;
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        const char* tag = ce.tagName();
        if (!tag) continue;

        if (strcmp(tag, "table_row") == 0 || strcmp(tag, "table_header_row") == 0) {
            bool is_header = (strcmp(tag, "table_header_row") == 0) || first_row;
            format_table_row(ctx, ce, is_header);
            first_row = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Top-level element dispatch
// ---------------------------------------------------------------------------

static void format_org_element(OrgContext& ctx, const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return;

    if (strcmp(tag, "heading") == 0)              { format_heading(ctx, elem);              return; }
    if (strcmp(tag, "paragraph") == 0)            { format_paragraph(ctx, elem);            return; }
    if (strcmp(tag, "list_item") == 0)            { format_list_item(ctx, elem);            return; }
    if (strcmp(tag, "code_block") == 0)           { format_code_block(ctx, elem);           return; }
    if (strcmp(tag, "quote_block") == 0)          { format_quote_block(ctx, elem);          return; }
    if (strcmp(tag, "example_block") == 0)        { format_example_block(ctx, elem);        return; }
    if (strcmp(tag, "verse_block") == 0)          { format_verse_block(ctx, elem);          return; }
    if (strcmp(tag, "center_block") == 0)         { format_center_block(ctx, elem);         return; }
    if (strcmp(tag, "drawer") == 0)               { format_drawer(ctx, elem);               return; }
    if (strcmp(tag, "scheduling") == 0)           { format_scheduling(ctx, elem);           return; }
    if (strcmp(tag, "footnote_definition") == 0)  { format_footnote_definition(ctx, elem);  return; }
    if (strcmp(tag, "directive") == 0)            { format_directive(ctx, elem);            return; }
    if (strcmp(tag, "table") == 0)                { format_table(ctx, elem);                return; }

    if (strcmp(tag, "timestamp") == 0) {
        format_inline_element(ctx, elem);
        return;
    }
    if (strcmp(tag, "display_math") == 0) {
        format_inline_element(ctx, elem);
        ctx.write_char('\n');
        return;
    }
    if (strcmp(tag, "table_row") == 0 || strcmp(tag, "table_header_row") == 0) {
        bool is_header = (strcmp(tag, "table_header_row") == 0);
        format_table_row(ctx, elem, is_header);
        return;
    }
    if (strcmp(tag, "table_cell") == 0) {
        format_table_cell(ctx, elem);
        return;
    }
    if (strcmp(tag, "text") == 0) {
        format_paragraph(ctx, elem);
        return;
    }

    // Unknown element: recurse into children
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            ctx.write_text(child.cstring());
        } else if (child.isElement()) {
            format_org_element(ctx, child.asElement());
        }
    }
}

// ---------------------------------------------------------------------------
// Item dispatch (string or element)
// ---------------------------------------------------------------------------

static void format_org_item(OrgContext& ctx, const ItemReader& item) {
    if (item.isNull()) return;

    if (item.isString()) {
        ctx.write_text(item.cstring());
    } else if (item.isElement()) {
        format_org_element(ctx, item.asElement());
    }
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void format_org(StringBuf* sb, Item root_item) {
    if (!sb || root_item.item == ITEM_NULL) return;

    Pool* pool = pool_create();
    OrgContext ctx(pool, sb);
    ItemReader root(root_item.to_const());

    if (root.isElement()) {
        ElementReader elem = root.asElement();
        if (elem.tagName() && strcmp(elem.tagName(), "org_document") == 0) {
            // Document root: format all children
            for (int64_t i = 0; i < elem.childCount(); i++) {
                format_org_item(ctx, elem.childAt(i));
            }
        } else {
            format_org_element(ctx, elem);
        }
    } else {
        format_org_item(ctx, root);
    }

    pool_destroy(pool);
}

String* format_org_string(Pool* pool, Item root_item) {
    if (root_item.item == ITEM_NULL) return NULL;

    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    format_org(sb, root_item);

    return stringbuf_to_string(sb);
}
