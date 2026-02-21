// LaTeX Formatter - Pure MarkReader (Gen3) implementation
#include "format.h"
#include "format-utils.hpp"
#include "../../lib/stringbuf.h"
#include "../mark_reader.hpp"
#include <string.h>

// Forward declarations
static void format_latex_value(LaTeXContext& ctx, const ItemReader& value);
static void format_latex_element(LaTeXContext& ctx, const ElementReader& elem, int depth);

// ---------------------------------------------------------------------------
// Value formatting (string, int, float, array, element, etc.)
// ---------------------------------------------------------------------------

static void format_latex_value(LaTeXContext& ctx, const ItemReader& value) {
    if (value.isNull()) return;

    if (value.isElement()) {
        format_latex_element(ctx, value.asElement(), 0);
    } else if (value.isString()) {
        String* str = value.asString();
        if (str && str->chars && str->len > 0 && str->len < 65536) {
            stringbuf_append_str_n(ctx.output(), str->chars, str->len);
        }
    } else if (value.isArray()) {
        ArrayReader arr = value.asArray();
        auto items = arr.items();
        ItemReader item;
        bool first = true;
        while (items.next(&item)) {
            if (!first) ctx.write_char(' ');
            first = false;
            format_latex_value(ctx, item);
        }
    } else if (value.isInt() || value.isFloat()) {
        format_number(ctx.output(), value.item());
    } else if (value.isSymbol()) {
        String* str = value.asString();
        if (str && str->chars && str->len > 0 && str->len < 65536) {
            stringbuf_append_str_n(ctx.output(), str->chars, str->len);
        }
    } else {
        ctx.write_text("[unknown]");
    }
}

// ---------------------------------------------------------------------------
// Element content helpers
// ---------------------------------------------------------------------------

// Format element children as LaTeX arguments/content
static void format_element_content(LaTeXContext& ctx, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            // Text argument: wrap in braces
            ctx.write_char('{');
            format_latex_value(ctx, child);
            ctx.write_char('}');
        } else {
            format_latex_value(ctx, child);
        }
    }
}

// Format a \command with its arguments
static void format_command_with_args(LaTeXContext& ctx, const ElementReader& elem, const char* cmd) {
    ctx.write_command(cmd);
    format_element_content(ctx, elem);
}

// Format \begin{env}...\end{env}
static void format_environment(LaTeXContext& ctx, const ElementReader& elem, const char* env, int depth) {
    ctx.write_latex_indent(depth);
    ctx.write_begin_environment(env);
    format_element_content(ctx, elem);
    ctx.write_char('\n');

    ctx.write_latex_indent(depth);
    ctx.write_end_environment(env);
}

// ---------------------------------------------------------------------------
// Element dispatch
// ---------------------------------------------------------------------------

// Known command names (simple \cmd{args})
static bool is_command(const char* tag) {
    return strcmp(tag, "documentclass") == 0 || strcmp(tag, "usepackage") == 0 ||
           strcmp(tag, "title") == 0 || strcmp(tag, "author") == 0 || strcmp(tag, "date") == 0 ||
           strcmp(tag, "section") == 0 || strcmp(tag, "subsection") == 0 || strcmp(tag, "subsubsection") == 0 ||
           strcmp(tag, "textbf") == 0 || strcmp(tag, "textit") == 0 || strcmp(tag, "texttt") == 0 ||
           strcmp(tag, "emph") == 0 || strcmp(tag, "underline") == 0;
}

// Known environment names
static bool is_environment(const char* tag) {
    return strcmp(tag, "document") == 0 || strcmp(tag, "abstract") == 0 ||
           strcmp(tag, "itemize") == 0 || strcmp(tag, "enumerate") == 0 ||
           strcmp(tag, "description") == 0 || strcmp(tag, "quote") == 0 ||
           strcmp(tag, "center") == 0 || strcmp(tag, "verbatim") == 0;
}

static void format_latex_element(LaTeXContext& ctx, const ElementReader& elem, int depth) {
    const char* tag = elem.tagName();
    if (!tag) return;

    if (is_command(tag)) {
        format_command_with_args(ctx, elem, tag);
    } else if (is_environment(tag)) {
        format_environment(ctx, elem, tag, depth);
    } else if (strcmp(tag, "maketitle") == 0) {
        ctx.write_text("\\maketitle");
    } else if (strcmp(tag, "tableofcontents") == 0) {
        ctx.write_text("\\tableofcontents");
    } else if (strcmp(tag, "item") == 0) {
        ctx.write_latex_indent(depth + 1);
        ctx.write_text("\\item ");
        format_element_content(ctx, elem);
    } else if (strncmp(tag, "math", 4) == 0) {
        ctx.write_char('$');
        format_element_content(ctx, elem);
        ctx.write_char('$');
    } else if (strncmp(tag, "comment", 7) == 0) {
        ctx.write_text("% ");
        format_element_content(ctx, elem);
    } else {
        // Generic: treat as \command{...}
        ctx.write_command(tag);
        format_element_content(ctx, elem);
    }
}

// ---------------------------------------------------------------------------
// Document-level formatting
// ---------------------------------------------------------------------------

static void format_latex_document(LaTeXContext& ctx, const ElementReader& doc) {
    bool first = true;
    for (int64_t i = 0; i < doc.childCount(); i++) {
        ItemReader child = doc.childAt(i);

        if (!first && child.isElement()) {
            ElementReader ce = child.asElement();
            const char* tag = ce.tagName();
            if (tag && (strncmp(tag, "section", 7) == 0 || strncmp(tag, "document", 8) == 0)) {
                ctx.write_text("\n\n");
            } else {
                ctx.write_char('\n');
            }
        }
        first = false;

        format_latex_value(ctx, child);
    }

    if (doc.childCount() > 0) {
        ctx.write_char('\n');
    } else {
        // Fallback: minimal LaTeX structure
        ctx.write_text("\\documentclass{article}\n");
        ctx.write_text("\\begin{document}\n");
        ctx.write_text("\\end{document}\n");
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

String* format_latex(Pool* pool, Item item) {
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;

    Pool* ctx_pool = pool_create();
    LaTeXContext ctx(ctx_pool, sb);
    ItemReader root(item.to_const());

    if (root.isArray()) {
        ArrayReader arr = root.asArray();
        auto items = arr.items();
        ItemReader elem;
        bool first = true;
        while (items.next(&elem)) {
            if (!first) ctx.write_char('\n');
            first = false;
            format_latex_value(ctx, elem);
        }
    } else if (root.isElement()) {
        ElementReader element = root.asElement();
        const char* tag = element.tagName();

        if (tag && (strcmp(tag, "document") == 0 || strcmp(tag, "article") == 0 ||
                    strcmp(tag, "book") == 0 || strcmp(tag, "latex_document") == 0)) {
            format_latex_document(ctx, element);
        } else {
            format_latex_element(ctx, element, 0);
        }
    } else {
        format_latex_value(ctx, root);
    }

    pool_destroy(ctx_pool);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}
