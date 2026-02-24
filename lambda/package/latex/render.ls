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
    // filter out whitespace-only strings (newlines from LaTeX parsing)
    if (len(trim(text)) == 0) { {result: null, ctx: ctx} }
    else { {result: text, ctx: ctx} }
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
        // ---- document structure (rendered inline) ----
        case 'latex_document': render_inline_document(el, ctx)
        case 'document': render_inline_body(el, ctx)

        // ---- preamble commands (already extracted, skip) ----
        case 'documentclass': {result: null, ctx: ctx}
        case 'usepackage': {result: null, ctx: ctx}
        case 'title': {result: null, ctx: ctx}
        case 'author': {result: null, ctx: ctx}
        case 'date': {result: null, ctx: ctx}

        // ---- title ----
        case 'maketitle': structure.render_maketitle(ctx)
        case 'tableofcontents' {
            let toc = structure.render_toc(ctx.headings)
            {result: toc, ctx: ctx}
        }

        // ---- sections (rendered inline) ----
        case 'chapter': render_inline_heading(el, ctx, "chapter", 1)
        case 'section': render_inline_heading(el, ctx, "section",
            if (ctx.docclass == "article") 2 else 2)
        case 'subsection': render_inline_heading(el, ctx, "subsection", 3)
        case 'subsubsection': render_inline_heading(el, ctx, "subsubsection", 4)
        case 'paragraph_command': render_inline_heading(el, ctx, "paragraph", 5)
        case 'subparagraph': render_inline_heading(el, ctx, "paragraph", 6)

        // ---- content paragraphs ----
        case 'paragraph': render_paragraph(el, ctx)

        // ---- text styling (rendered inline) ----
        case 'textbf': render_inline_styled(el, ctx, "b")
        case 'textit': render_inline_styled(el, ctx, "i")
        case 'emph': render_inline_styled(el, ctx, "em")
        case 'underline': render_inline_styled(el, ctx, "u")
        case 'texttt': render_inline_code(el, ctx)
        case 'verb': text.render_verbatim_inline(el, ctx)

        // ---- font families (rendered inline) ----
        case 'textsf': render_inline_font(el, ctx, "font-sans")
        case 'textsc': render_inline_font(el, ctx, "font-smallcaps")
        case 'textsl': render_inline_font(el, ctx, "font-oblique")
        case 'textrm': render_inline_font(el, ctx, "font-serif")

        // ---- font sizes (rendered inline) ----
        case 'tiny': render_inline_size(el, ctx, "tiny")
        case 'scriptsize': render_inline_size(el, ctx, "scriptsize")
        case 'footnotesize': render_inline_size(el, ctx, "footnotesize")
        case 'small': render_inline_size(el, ctx, "small")
        case 'normalsize': render_inline_size(el, ctx, "normalsize")
        case 'large': render_inline_size(el, ctx, "large")
        case 'Large': render_inline_size(el, ctx, "Large")
        case 'LARGE': render_inline_size(el, ctx, "LARGE")
        case 'huge': render_inline_size(el, ctx, "huge")
        case 'Huge': render_inline_size(el, ctx, "Huge")

        // ---- diacritics ----
        case 'accent': render_accent(el, ctx)

        // ---- math ----
        case 'inline_math': math_bridge.render_inline(el, ctx)
        case 'display_math': math_bridge.render_display(el, ctx)
        case 'math_environment': render_math_env(el, ctx)
        case 'equation': math_bridge.render_equation(el, ctx)

        // ---- lists (rendered inline) ----
        case 'itemize': render_inline_itemize(el, ctx)
        case 'enumerate': render_inline_enumerate(el, ctx)
        case 'description': render_inline_description(el, ctx)
        case 'item': {result: null, ctx: ctx}  // handled by list splitters

        // ---- environments (rendered inline to avoid callback JIT corruption) ----
        case 'quote': render_env_quote(el, ctx)
        case 'quotation': render_env_quote(el, ctx)
        case 'verse': render_env_quote(el, ctx)
        case 'center': render_env_center(el, ctx)
        case 'flushleft': render_env_flushleft(el, ctx)
        case 'flushright': render_env_flushright(el, ctx)
        case 'verbatim': envs.render_verbatim(el, ctx)
        case 'lstlisting': envs.render_verbatim(el, ctx)
        case 'abstract': render_env_abstract(el, ctx)
        case 'figure': render_env_figure(el, ctx)
        case 'minipage': render_env_minipage(el, ctx)
        case 'multicols': render_env_multicols(el, ctx)

        // ---- tables (keep callback for now — complex cell rendering) ----
        case 'table': tables.render_table_env(el, ctx, render_node)
        case 'tabular': tables.render_tabular(el, ctx, render_node)

        // ---- spacing and breaks ----
        case 'linebreak': spacing.render_linebreak(ctx)
        case 'newline': spacing.render_newline(ctx)
        case 'hspace': spacing.render_hspace(el, ctx)
        case 'vspace': spacing.render_vspace(el, ctx)
        case 'hrule': spacing.render_hrule(ctx)
        case 'newpage': spacing.render_pagebreak(ctx)
        case 'clearpage': spacing.render_pagebreak(ctx)
        case 'hfill': spacing.render_hfill(ctx)

        // ---- cross references ----
        case 'label': render_label(el, ctx)
        case 'ref': render_ref(el, ctx)
        case 'href': render_href(el, ctx)
        case 'url': render_url(el, ctx)

        // ---- footnotes ----
        case 'footnote': render_footnote(el, ctx)

        // ---- special characters ----
        case 'control_symbol': render_control_symbol(el, ctx)
        case 'controlspace_command': {result: " ", ctx: ctx}

        // ---- groups (transparent wrappers) ----
        case 'curly_group': render_group(el, ctx)
        case 'brack_group': render_group(el, ctx)
        case 'group': render_group(el, ctx)
        case 'text_group': render_group(el, ctx)

        // ---- symbol commands (no children) ----
        case 'LaTeX': {result: "LaTeX", ctx: ctx}
        case 'TeX': {result: "TeX", ctx: ctx}
        case 'LaTeXe': {result: "LaTeX2e", ctx: ctx}
        case 'today': render_today(ctx)
        case 'ldots': {result: "\u2026", ctx: ctx}
        case 'dots': {result: "\u2026", ctx: ctx}
        case 'dag': {result: "\u2020", ctx: ctx}
        case 'ddag': {result: "\u2021", ctx: ctx}
        case 'copyright': {result: "\u00A9", ctx: ctx}
        case 'pounds': {result: "\u00A3", ctx: ctx}
        case 'textbackslash': {result: "\\", ctx: ctx}
        case 'textasciitilde': {result: "~", ctx: ctx}
        case 'textasciicircum': {result: "^", ctx: ctx}
        case 'textbar': {result: "|", ctx: ctx}
        case 'textless': {result: "<", ctx: ctx}
        case 'textgreater': {result: ">", ctx: ctx}
        case 'SS': {result: "\u00A7", ctx: ctx}
        case 'P': {result: "\u00B6", ctx: ctx}
        case 'ss': {result: "\u00DF", ctx: ctx}
        case 'aa': {result: "\u00E5", ctx: ctx}
        case 'AA': {result: "\u00C5", ctx: ctx}
        case 'ae': {result: "\u00E6", ctx: ctx}
        case 'AE': {result: "\u00C6", ctx: ctx}
        case 'oe': {result: "\u0153", ctx: ctx}
        case 'OE': {result: "\u0152", ctx: ctx}
        case 'o': {result: "\u00F8", ctx: ctx}
        case 'O': {result: "\u00D8", ctx: ctx}
        case 'l': {result: "\u0142", ctx: ctx}
        case 'L': {result: "\u0141", ctx: ctx}
        case 'i': {result: "\u0131", ctx: ctx}
        case 'j': {result: "\u0237", ctx: ctx}
        case 'setcounter': {result: null, ctx: ctx}  // skip counter commands
        case 'newcounter': {result: null, ctx: ctx}
        case 'addtocounter': {result: null, ctx: ctx}
        case 'stepcounter': {result: null, ctx: ctx}
        case 'comment': {result: null, ctx: ctx}  // skip comment environments
        case 'includegraphics': render_includegraphics(el, ctx)
        case 'picture': {result: null, ctx: ctx}  // skip picture environments for now
        case 'put': {result: null, ctx: ctx}
        case 'line': {result: null, ctx: ctx}
        case 'vector': {result: null, ctx: ctx}
        case 'circle': {result: null, ctx: ctx}
        case 'oval': {result: null, ctx: ctx}
        case 'bezier': {result: null, ctx: ctx}
        case 'qbezier': {result: null, ctx: ctx}
        case 'unitlength': {result: null, ctx: ctx}
        case 'linethickness': {result: null, ctx: ctx}
        case 'thinlines': {result: null, ctx: ctx}
        case 'thicklines': {result: null, ctx: ctx}
        case 'marginpar': render_marginpar(el, ctx)

        // ---- generic/unknown ----
        default: render_generic(el, ctx)
    }
}

// ============================================================
// Content paragraphs — rendered as <p>
// ============================================================

fn render_paragraph(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let items = children.items
    if (len(items) == 0) { {result: null, ctx: children.ctx} }
    else if (has_block_child(items)) {
        // split around block elements: inline→<p>, block→pass-through
        let parts = split_around_blocks(items)
        if (len(parts) == 1) { {result: parts[0], ctx: children.ctx} }
        else { {result: parts, ctx: children.ctx} }
    }
    else {
        let result = <p;
            for c in items { c }
        >
        {result: result, ctx: children.ctx}
    }
}

// split items into segments: inline items get wrapped in <p>, block items pass through
fn split_around_blocks(items) {
    let n = len(items)
    split_blocks_rec(items, 0, n, [], [])
}

fn split_blocks_rec(items, i, n, current_inline, acc) {
    if (i >= n) {
        flush_inline(current_inline, acc)
    } else {
        let item = items[i]
        if (is_block_element(item)) {
            let flushed = flush_inline(current_inline, acc)
            split_blocks_rec(items, i + 1, n, [], flushed ++ [item])
        } else {
            split_blocks_rec(items, i + 1, n, current_inline ++ [item], acc)
        }
    }
}

fn flush_inline(inline_items, acc) {
    if (len(inline_items) == 0) { acc }
    else {
        let p = <p;
            for c in inline_items { c }
        >
        acc ++ [p]
    }
}

fn has_block_child(items) {
    let n = len(items)
    check_block_rec(items, 0, n)
}

fn check_block_rec(items, i, n) {
    if (i >= n) { false }
    else {
        let item = items[i]
        if (is_block_element(item)) { true }
        else { check_block_rec(items, i + 1, n) }
    }
}

fn is_block_element(item) {
    if (item is element) {
        let tag = string(name(item))
        is_block_tag(tag)
    } else { false }
}

fn is_block_tag(tag) {
    match tag {
        case "header": true
        case "div": true
        case "section": true
        case "article": true
        case "nav": true
        case "aside": true
        case "main": true
        case "ul": true
        case "ol": true
        case "dl": true
        case "table": true
        case "figure": true
        case "blockquote": true
        case "pre": true
        case "h1": true
        case "h2": true
        case "h3": true
        case "h4": true
        case "h5": true
        case "h6": true
        default: false
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

fn render_today(ctx) {
    let d = ctx.date
    if (d != null) { {result: d, ctx: ctx} }
    else { {result: "today", ctx: ctx} }
}

fn render_includegraphics(el, ctx) {
    let src = util.text_of(el)
    let result = <img class: "latex-image", src: trim(src), alt: trim(src)>
    {result: result, ctx: ctx}
}

fn render_marginpar(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <span class: "latex-cmd-marginpar";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Text styling renderers (inline — avoid callback JIT corruption)
// ============================================================

fn render_inline_styled(el, ctx, html_tag) {
    let children = render_children_inline(el, 0, ctx)
    let result = match html_tag {
        case "b":  <strong; for c in children.items { c }>
        case "i":  <em; for c in children.items { c }>
        case "em": <em; for c in children.items { c }>
        case "u":  <u; for c in children.items { c }>
        default:   <span; for c in children.items { c }>
    }
    {result: result, ctx: children.ctx}
}

fn render_inline_code(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <code class: "latex-code";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_inline_font(el, ctx, css_class) {
    let children = render_children_inline(el, 0, ctx)
    let result = <span class: css_class;
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_inline_size(el, ctx, cmd_name) {
    let size = sym.get_font_size(cmd_name)
    let children = render_children_inline(el, 0, ctx)
    if (size != null) {
        let result = <span style: "font-size:" ++ size;
            for c in children.items { c }
        >
        {result: result, ctx: children.ctx}
    } else {
        let result = <span;
            for c in children.items { c }
        >
        {result: result, ctx: children.ctx}
    }
}

// ============================================================
// Document structure renderers (inline)
// ============================================================

fn render_inline_document(el, ctx) {
    let preamble_ctx = structure.extract_preamble_ctx(el, ctx)
    let doc_body = util.find_child(el, 'document')
    if (doc_body != null) {
        render_inline_body(doc_body, preamble_ctx)
    } else {
        let children = render_children_inline(el, 0, preamble_ctx)
        let article = <article class: "latex-document latex-" ++ preamble_ctx.docclass;
            for c in children.items { c }
        >
        {result: article, ctx: children.ctx}
    }
}

fn render_inline_body(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let article = <article class: "latex-document latex-" ++ ctx.docclass;
        for c in children.items { c }
    >
    {result: article, ctx: children.ctx}
}

fn render_inline_heading(el, ctx, counter_name, html_level) {
    // step the counter
    let new_ctx = ctx_mod.step_counter(ctx, counter_name)

    // compute section number
    let sec_num = structure.compute_section_num_pub(new_ctx.counters, counter_name, ctx.docclass)

    // extract title
    let title_el = el.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let title_id = util.slugify(title_text)

    // record heading for TOC
    let ctx2 = ctx_mod.add_heading(new_ctx, html_level, sec_num, title_text, title_id)

    // render title children inline
    let title_children = if (title_el != null) {
        if (title_el is element) {
            render_children_inline(title_el, 0, ctx2).items
        } else if (title_text != "") { [title_text] }
        else { [] }
    } else {
        if (title_text != "") { [title_text] } else { [] }
    }

    let heading = structure.build_heading_pub(html_level, title_id, sec_num, title_children)
    {result: heading, ctx: ctx2}
}

// ============================================================
// List renderers (inline)
// ============================================================

fn render_inline_itemize(el, ctx) {
    let inner_ctx = ctx_mod.enter_list(ctx)
    let flat = lists.flatten_paragraphs_pub(el)
    let items = split_list_items(flat, inner_ctx)
    let result = <ul class: "latex-itemize";
        for li in items.elements { li }
    >
    {result: result, ctx: items.ctx}
}

fn render_inline_enumerate(el, ctx) {
    let inner_ctx = ctx_mod.enter_list(ctx)
    let counter = match inner_ctx.list_depth {
        case 1: "enumi"
        case 2: "enumii"
        default: "enumiii"
    }
    let flat = lists.flatten_paragraphs_pub(el)
    let items = split_list_items_enum(flat, inner_ctx, counter)
    let result = <ol class: "latex-enumerate";
        for li in items.elements { li }
    >
    {result: result, ctx: items.ctx}
}

fn render_inline_description(el, ctx) {
    let inner_ctx = ctx_mod.enter_list(ctx)
    let flat = lists.flatten_paragraphs_pub(el)
    let items = split_desc_items(flat, inner_ctx)
    let result = <dl class: "latex-description";
        for item in items.elements { item }
    >
    {result: result, ctx: items.ctx}
}

// ---- item splitting with inline rendering ----

fn is_item_node(child) {
    if (child is element) {
        if (string(name(child)) == "item") true else false
    } else { false }
}

fn split_list_items(flat, ctx) {
    let n = len(flat)
    split_items_rec(flat, 0, n, [], [], ctx)
}

fn split_items_rec(flat, i, n, current, acc, ctx) {
    if (i >= n) {
        let final_items = if (len(current) > 0) acc ++ [<li; for c in current { c }>]
            else acc
        {elements: final_items, ctx: ctx}
    } else {
        let child = flat[i]
        if (is_item_node(child)) {
            let flushed = if (len(current) > 0) acc ++ [<li; for c in current { c }>]
                else acc
            split_items_rec(flat, i + 1, n, [], flushed, ctx)
        } else {
            let rendered = render_node(child, ctx)
            let new_current = if (rendered.result != null) current ++ [rendered.result]
                else current
            split_items_rec(flat, i + 1, n, new_current, acc, rendered.ctx)
        }
    }
}

fn split_list_items_enum(flat, ctx, counter) {
    let n = len(flat)
    split_items_enum_rec(flat, 0, n, [], [], ctx, counter)
}

fn split_items_enum_rec(flat, i, n, current, acc, ctx, counter) {
    if (i >= n) {
        let final_items = if (len(current) > 0) acc ++ [<li; for c in current { c }>]
            else acc
        {elements: final_items, ctx: ctx}
    } else {
        let child = flat[i]
        if (is_item_node(child)) {
            let flushed = if (len(current) > 0) acc ++ [<li; for c in current { c }>]
                else acc
            let new_ctx = ctx_mod.step_counter(ctx, counter)
            split_items_enum_rec(flat, i + 1, n, [], flushed, new_ctx, counter)
        } else {
            let rendered = render_node(child, ctx)
            let new_current = if (rendered.result != null) current ++ [rendered.result]
                else current
            split_items_enum_rec(flat, i + 1, n, new_current, acc, rendered.ctx, counter)
        }
    }
}

fn split_desc_items(flat, ctx) {
    let n = len(flat)
    split_desc_rec(flat, 0, n, [], null, [], ctx)
}

fn split_desc_rec(flat, i, n, acc, term, content, ctx) {
    if (i >= n) {
        let final_items = flush_desc_inline(acc, term, content)
        {elements: final_items, ctx: ctx}
    } else {
        let child = flat[i]
        if (is_item_node(child)) {
            let flushed = flush_desc_inline(acc, term, content)
            let label_text = lists.get_item_label_pub(child)
            let new_term = if (label_text != null) <dt; label_text> else <dt>
            split_desc_rec(flat, i + 1, n, flushed, new_term, [], ctx)
        } else {
            let rendered = render_node(child, ctx)
            let new_content = if (rendered.result != null) content ++ [rendered.result]
                else content
            split_desc_rec(flat, i + 1, n, acc, term, new_content, rendered.ctx)
        }
    }
}

fn flush_desc_inline(acc, term, content) {
    if (term != null) {
        let dd = if (len(content) > 0) <dd; for c in content { c }> else <dd>
        acc ++ [term, dd]
    } else if (len(content) > 0) {
        let dd = <dd; for c in content { c }>
        acc ++ [dd]
    } else { acc }
}

// ============================================================
// Environment renderers (inline — avoid callback JIT corruption)
// ============================================================

fn render_env_quote(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <blockquote class: "latex-quote";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_center(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-center", style: "text-align:center";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_flushleft(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-flushleft", style: "text-align:left";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_flushright(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-flushright", style: "text-align:right";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_abstract(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-abstract";
        <div class: "abstract-title"; "Abstract">
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_minipage(el, ctx) {
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-minipage";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

fn render_env_figure(el, ctx) {
    let new_ctx = ctx_mod.step_counter(ctx, "figure")
    let fig_num = new_ctx.counters.figure
    let children = render_children_inline(el, 0, new_ctx)

    // extract caption if present
    let caption = util.find_child(el, 'caption')
    let cap_text = if (caption != null) util.text_of(caption) else null
    let caption_el = if (cap_text != null) {
        <figcaption;
            <strong; "Figure " ++ string(fig_num) ++ ": ">
            cap_text
        >
    } else { null }

    // extract label
    let label_node = util.find_child(el, 'label')
    let label_name = if (label_node != null) util.text_of(label_node) else null
    let ctx2 = if (label_name != null) {
        ctx_mod.add_label(children.ctx, label_name, "figure", string(fig_num), util.slugify("fig-" ++ label_name))
    } else { children.ctx }

    let result = <figure class: "latex-figure";
        for c in children.items { c }
        if caption_el != null { caption_el }
    >
    {result: result, ctx: ctx2}
}

fn render_env_multicols(el, ctx) {
    let col_group = util.find_child(el, 'curly_group')
    let cols = if (col_group != null) util.text_of(col_group) else "2"
    let children = render_children_inline(el, 0, ctx)
    let result = <div class: "latex-multicols", style: "column-count:" ++ trim(cols);
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
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
