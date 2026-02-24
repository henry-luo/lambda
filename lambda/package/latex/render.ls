// latex/render.ls — Core AST dispatcher
// Routes each AST node to the appropriate rendering module by tag name.
// Threads immutable context through every call.
// All render functions return {result: element|string|null, ctx: context}

import util: .lambda.package.latex.util
import sym: .lambda.package.latex.symbols
import text: .lambda.package.latex.text
import structure: .lambda.package.latex.structure
import math_bridge: .lambda.package.latex.math_bridge
import lists: .lambda.package.latex.elements.lists
import envs: .lambda.package.latex.elements.environments
import tables: .lambda.package.latex.elements.tables
import spacing: .lambda.package.latex.elements.spacing
import ctx_mod: .lambda.package.latex.context

// ============================================================
// Main dispatcher — called recursively on every AST node
// ============================================================

pub fn render_node(node, ctx) {
    if (node == null) { {result: null, ctx: ctx} }
    else if (node is string) { render_text_node(node, ctx) }
    else if (node is int or node is float) { {result: string(node), ctx: ctx} }
    else if (node is symbol) { render_symbol(node, ctx) }
    else if (node is element) { render_element(node, ctx) }
    else { {result: string(node), ctx: ctx} }
}

// ============================================================
// Text (string) nodes — pass through
// ============================================================

fn render_text_node(text, ctx) {
    {result: text, ctx: ctx}
}

// ============================================================
// Symbol nodes — parbreak, nbsp, etc.
// ============================================================

fn render_symbol(sym_node, ctx) {
    let s = string(sym_node)
    match s {
        case "parbreak": {result: null, ctx: ctx}   // handled at paragraph level
        case "nbsp": spacing.render_nbsp(ctx)
        case "row_sep": {result: null, ctx: ctx}     // handled at table level
        case "alignment_tab": {result: null, ctx: ctx}
        default: {result: s, ctx: ctx}
    }
}

// ============================================================
// Element nodes — dispatch on tag name
// ============================================================

fn render_element(el, ctx) {
    let tag = name(el)
    match tag {
        // ---- document structure ----
        case "latex_document": structure.render_document(el, ctx, render_node)
        case "document": structure.render_body(el, ctx, render_node)

        // ---- preamble commands (already extracted, skip) ----
        case "documentclass": {result: null, ctx: ctx}
        case "usepackage": {result: null, ctx: ctx}
        case "title": {result: null, ctx: ctx}
        case "author": {result: null, ctx: ctx}
        case "date": {result: null, ctx: ctx}

        // ---- title ----
        case "maketitle": structure.render_maketitle(ctx)
        case "tableofcontents" {
            let toc = structure.render_toc(ctx.headings)
            {result: toc, ctx: ctx}
        }

        // ---- sections ----
        case "chapter": structure.render_heading(el, ctx, "chapter", 1, render_node)
        case "section": structure.render_heading(el, ctx, "section",
            if (ctx.docclass == "article") 2 else 2, render_node)
        case "subsection": structure.render_heading(el, ctx, "subsection", 3, render_node)
        case "subsubsection": structure.render_heading(el, ctx, "subsubsection", 4, render_node)
        case "paragraph": structure.render_heading(el, ctx, "paragraph", 5, render_node)
        case "subparagraph": structure.render_heading(el, ctx, "paragraph", 6, render_node)

        // ---- text styling ----
        case "textbf": text.render_styled(el, ctx, "b", render_node)
        case "textit": text.render_styled(el, ctx, "i", render_node)
        case "emph": text.render_styled(el, ctx, "em", render_node)
        case "underline": text.render_styled(el, ctx, "u", render_node)
        case "texttt": text.render_code_inline(el, ctx, render_node)
        case "verb": text.render_verbatim_inline(el, ctx)

        // ---- font families ----
        case "textsf": text.render_font_family(el, ctx, "font-sans", render_node)
        case "textsc": text.render_font_family(el, ctx, "font-smallcaps", render_node)
        case "textsl": text.render_font_family(el, ctx, "font-oblique", render_node)
        case "textrm": text.render_font_family(el, ctx, "font-serif", render_node)

        // ---- font sizes ----
        case "tiny": text.render_font_size(el, ctx, "tiny", render_node)
        case "scriptsize": text.render_font_size(el, ctx, "scriptsize", render_node)
        case "footnotesize": text.render_font_size(el, ctx, "footnotesize", render_node)
        case "small": text.render_font_size(el, ctx, "small", render_node)
        case "normalsize": text.render_font_size(el, ctx, "normalsize", render_node)
        case "large": text.render_font_size(el, ctx, "large", render_node)
        case "Large": text.render_font_size(el, ctx, "Large", render_node)
        case "LARGE": text.render_font_size(el, ctx, "LARGE", render_node)
        case "huge": text.render_font_size(el, ctx, "huge", render_node)
        case "Huge": text.render_font_size(el, ctx, "Huge", render_node)

        // ---- diacritics ----
        case "accent": render_accent(el, ctx)

        // ---- math ----
        case "inline_math": math_bridge.render_inline(el, ctx)
        case "display_math": math_bridge.render_display(el, ctx)
        case "math_environment": render_math_env(el, ctx)
        case "equation": math_bridge.render_equation(el, ctx)

        // ---- lists ----
        case "itemize": lists.render_itemize(el, ctx, render_node)
        case "enumerate": lists.render_enumerate(el, ctx, render_node)
        case "description": lists.render_description(el, ctx, render_node)
        case "item": {result: null, ctx: ctx}  // handled by list splitters

        // ---- environments ----
        case "quote": envs.render_quote(el, ctx, render_node)
        case "quotation": envs.render_quote(el, ctx, render_node)
        case "verse": envs.render_quote(el, ctx, render_node)
        case "center": envs.render_center(el, ctx, render_node)
        case "flushleft": envs.render_flushleft(el, ctx, render_node)
        case "flushright": envs.render_flushright(el, ctx, render_node)
        case "verbatim": envs.render_verbatim(el, ctx)
        case "lstlisting": envs.render_verbatim(el, ctx)
        case "abstract": envs.render_abstract(el, ctx, render_node)
        case "figure": envs.render_figure(el, ctx, render_node)
        case "minipage": envs.render_minipage(el, ctx, render_node)
        case "multicols": envs.render_multicols(el, ctx, render_node)

        // ---- tables ----
        case "table": tables.render_table_env(el, ctx, render_node)
        case "tabular": tables.render_tabular(el, ctx, render_node)

        // ---- spacing and breaks ----
        case "linebreak": spacing.render_linebreak(ctx)
        case "newline": spacing.render_newline(ctx)
        case "hspace": spacing.render_hspace(el, ctx)
        case "vspace": spacing.render_vspace(el, ctx)
        case "hrule": spacing.render_hrule(ctx)
        case "newpage": spacing.render_pagebreak(ctx)
        case "clearpage": spacing.render_pagebreak(ctx)
        case "hfill": spacing.render_hfill(ctx)

        // ---- cross references ----
        case "label": render_label(el, ctx)
        case "ref": render_ref(el, ctx)
        case "href": render_href(el, ctx)
        case "url": render_url(el, ctx)

        // ---- footnotes ----
        case "footnote": render_footnote(el, ctx)

        // ---- special characters ----
        case "control_symbol": render_control_symbol(el, ctx)
        case "controlspace_command": {result: " ", ctx: ctx}

        // ---- groups (transparent wrappers) ----
        case "curly_group": render_group(el, ctx)
        case "brack_group": render_group(el, ctx)
        case "group": render_group(el, ctx)
        case "text_group": render_group(el, ctx)

        // ---- generic/unknown ----
        default: render_generic(el, ctx)
    }
}

// ============================================================
// Specific handlers
// ============================================================

fn render_accent(el, ctx) {
    // find the command name attribute
    let cmd = el.command
    let cmd_name = if (cmd != null) cmd else ""
    text.render_diacritic(el, ctx, cmd_name)
}

fn render_math_env(el, ctx) {
    let env_name = util.attr_or(el, "name", "equation")
    math_bridge.render_math_environment(el, ctx, env_name)
}

fn render_label(el, ctx) {
    let label_name = util.text_of(el)
    let trimmed = trim(label_name)
    let result = <a id: util.slugify(trimmed)>
    {result: result, ctx: ctx}
}

fn render_ref(el, ctx) {
    let ref_name = trim(util.text_of(el))
    let label_info = ctx.labels[ref_name]
    if (label_info != null) {
        let result = <a class: "latex-ref", href: "#" ++ label_info.id;
            label_info.number
        >
        {result: result, ctx: ctx}
    } else {
        let result = <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name);
            "??"
        >
        {result: result, ctx: ctx}
    }
}

fn render_href(el, ctx) {
    let n = len(el)
    let url = if (n > 0) { util.text_of(el[0]) } else { "#" }
    let display = if (n > 1) {
        render_children_inline(el, 1, ctx).items
    } else { [url] }
    let result = <a href: url; for d in display { d }>
    {result: result, ctx: ctx}
}

fn render_url(el, ctx) {
    let url = util.text_of(el)
    let result = <a class: "latex-url", href: url; url>
    {result: result, ctx: ctx}
}

fn render_footnote(el, ctx) {
    // render footnote content
    let children = render_children_inline(el, 0, ctx)
    let content = <span; for c in children.items { c }>
    let new_ctx = ctx_mod.add_footnote(children.ctx, content)
    let fn_num = new_ctx.counters.footnote

    let result = <sup class: "latex-footnote-ref";
        <a href: "#fn-" ++ string(fn_num), id: "fnref-" ++ string(fn_num);
            string(fn_num)
        >
    >
    {result: result, ctx: new_ctx}
}

fn render_control_symbol(el, ctx) {
    let text = util.text_of(el)
    let resolved = sym.resolve_special(text)
    if (resolved != null) { {result: resolved, ctx: ctx} }
    else { {result: text, ctx: ctx} }
}

fn render_group(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    if (len(children.items) == 0) { {result: null, ctx: children.ctx} }
    else if (len(children.items) == 1) { {result: children.items[0], ctx: children.ctx} }
    else {
        let result = <span; for c in children.items { c }>
        {result: result, ctx: children.ctx}
    }
}

fn render_generic(el, ctx) {
    // try to render children — skip truly unknown commands
    let n = len(el)
    if (n == 0) { {result: null, ctx: ctx} }
    else {
        let children = render_children_inline(el, 0, ctx)
        if (len(children.items) == 0) { {result: null, ctx: children.ctx} }
        else if (len(children.items) == 1) { {result: children.items[0], ctx: children.ctx} }
        else {
            let result = <span class: "latex-cmd-" ++ name(el); for c in children.items { c }>
            {result: result, ctx: children.ctx}
        }
    }
}

// ============================================================
// Helper: render children inline from start_index
// ============================================================

fn render_children_inline(el, start_idx, ctx) {
    let n = len(el)
    if (start_idx >= n) { {items: [], ctx: ctx} }
    else { render_inline_rec(el, start_idx, n, [], ctx) }
}

fn render_inline_rec(el, i, n, acc, ctx) {
    if (i >= n) { {items: acc, ctx: ctx} }
    else {
        let child = el[i]
        let rendered = render_node(child, ctx)
        let new_acc = if (rendered.result != null) acc ++ [rendered.result] else acc
        render_inline_rec(el, i + 1, n, new_acc, rendered.ctx)
    }
}
