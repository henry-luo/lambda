// latex/render.ls — Pass 2: Render AST to HTML elements
// Uses pre-computed DocInfo from analyze.ls (read-only, no state threading).
// render_node(node, info) returns element|string|null directly.

import util: .lambda.package.latex.util
import sym: .lambda.package.latex.symbols
import math_bridge: .lambda.package.latex.math_bridge
import spacing: .lambda.package.latex.elements.spacing

// ============================================================
// Main dispatcher — called recursively on every AST node
// ============================================================

pub fn render_node(node, info) {
    if (node == null) null
    else if (node is string) render_text(node)
    else if (node is int or node is float) string(node)
    else if (node is symbol) render_symbol(node, info)
    else if (node is element) render_element(node, info)
    else string(node)
}

// render children of a node — used by postprocess for footnote bodies
pub fn render_children_of(node, info) {
    render_children(node, 0, info)
}

fn render_text(t) {
    if (len(trim(t)) == 0) null
    else t
}

fn render_symbol(s, info) {
    let v = string(s)
    match v {
        case "parbreak": null
        case "nbsp": "\u00A0"
        case "row_sep": null
        case "alignment_tab": null
        default: v
    }
}

// ============================================================
// Element dispatcher
// ============================================================

fn render_element(el, info) {
    let tag = name(el)
    match tag {
        // ---- document structure ----
        case 'latex_document': render_document(el, info)
        case 'document': render_body(el, info)

        // ---- preamble (skip — already extracted in pass 1) ----
        case 'documentclass': null
        case 'usepackage': null
        case 'title': null
        case 'author': null
        case 'date': null

        // ---- title page ----
        case 'maketitle': render_maketitle(info)
        case 'tableofcontents': render_toc(info)

        // ---- sections ----
        case 'chapter': render_heading(el, info, 1)
        case 'section': render_heading(el, info, 2)
        case 'subsection': render_heading(el, info, 3)
        case 'subsubsection': render_heading(el, info, 4)
        case 'paragraph_command': render_heading(el, info, 5)
        case 'subparagraph': render_heading(el, info, 6)

        // ---- paragraphs ----
        case 'paragraph': render_paragraph(el, info)

        // ---- text styling ----
        case 'textbf': render_styled(el, info, "b")
        case 'textit': render_styled(el, info, "i")
        case 'emph': render_styled(el, info, "em")
        case 'underline': render_styled(el, info, "u")
        case 'texttt': render_code(el, info)
        case 'verb': render_verb(el)

        // ---- font families ----
        case 'textsf': render_font(el, info, "font-sans")
        case 'textsc': render_font(el, info, "font-smallcaps")
        case 'textsl': render_font(el, info, "font-oblique")
        case 'textrm': render_font(el, info, "font-serif")

        // ---- font sizes ----
        case 'tiny': render_size(el, info, "tiny")
        case 'scriptsize': render_size(el, info, "scriptsize")
        case 'footnotesize': render_size(el, info, "footnotesize")
        case 'small': render_size(el, info, "small")
        case 'normalsize': render_size(el, info, "normalsize")
        case 'large': render_size(el, info, "large")
        case 'Large': render_size(el, info, "Large")
        case 'LARGE': render_size(el, info, "LARGE")
        case 'huge': render_size(el, info, "huge")
        case 'Huge': render_size(el, info, "Huge")

        // ---- diacritics ----
        case 'accent': render_accent(el, info)

        // ---- math ----
        case 'inline_math': math_bridge.render_inline_el(el)
        case 'display_math': math_bridge.render_display_el(el)
        case 'math_environment': render_math_env(el, info)
        case 'equation': math_bridge.render_equation_el(el)

        // ---- lists ----
        case 'itemize': render_itemize(el, info)
        case 'enumerate': render_enumerate(el, info)
        case 'description': render_description(el, info)
        case 'item': null

        // ---- environments ----
        case 'quote': render_env_block(el, info, "blockquote", "latex-quote")
        case 'quotation': render_env_block(el, info, "blockquote", "latex-quote")
        case 'verse': render_env_block(el, info, "blockquote", "latex-quote")
        case 'center': render_env_div(el, info, "latex-center", "text-align:center")
        case 'flushleft': render_env_div(el, info, "latex-flushleft", "text-align:left")
        case 'flushright': render_env_div(el, info, "latex-flushright", "text-align:right")
        case 'verbatim': render_verbatim_env(el)
        case 'lstlisting': render_verbatim_env(el)
        case 'abstract': render_abstract(el, info)
        case 'figure': render_figure(el, info)
        case 'minipage': render_env_div(el, info, "latex-minipage", null)
        case 'multicols': render_multicols(el, info)

        // ---- theorem-like environments ----
        case 'theorem': render_theorem_env(el, info, "Theorem", "theorem")
        case 'lemma': render_theorem_env(el, info, "Lemma", "lemma")
        case 'corollary': render_theorem_env(el, info, "Corollary", "corollary")
        case 'proposition': render_theorem_env(el, info, "Proposition", "proposition")
        case 'definition': render_theorem_env(el, info, "Definition", "definition")
        case 'example': render_theorem_env(el, info, "Example", "example")
        case 'remark': render_theorem_env(el, info, "Remark", "remark")
        case 'proof': render_proof_env(el, info)

        // ---- tables ----
        case 'table': render_table_env(el, info)
        case 'tabular': render_tabular(el, info)

        // ---- spacing ----
        case 'linebreak': <br>
        case 'newline': <br>
        case 'hspace': spacing.render_hspace_el(el)
        case 'vspace': spacing.render_vspace_el(el)
        case 'hrule': <hr>
        case 'newpage': <hr class: "latex-pagebreak">
        case 'clearpage': <hr class: "latex-pagebreak">
        case 'hfill': <span class: "latex-hfill">

        // ---- cross references ----
        case 'label': render_label(el, info)
        case 'ref': render_ref(el, info)
        case 'href': render_href(el, info)
        case 'url': render_url(el)
        case 'cite': render_cite(el, info)

        // ---- bibliography ----
        case 'thebibliography': render_bibliography(el, info)
        case 'bibitem': null

        // ---- footnotes ----
        case 'footnote': render_footnote(el, info)

        // ---- special characters ----
        case 'control_symbol': render_control_symbol(el)
        case 'controlspace_command': " "

        // ---- groups ----
        case 'curly_group': render_group(el, info)
        case 'brack_group': render_group(el, info)
        case 'group': render_group(el, info)
        case 'text_group': render_group(el, info)

        // ---- symbol commands ----
        case 'LaTeX': "LaTeX"
        case 'TeX': "TeX"
        case 'LaTeXe': "LaTeX2e"
        case 'today': if (info.date != null) info.date else "today"
        case 'ldots': "\u2026"
        case 'dots': "\u2026"
        case 'dag': "\u2020"
        case 'ddag': "\u2021"
        case 'copyright': "\u00A9"
        case 'pounds': "\u00A3"
        case 'textbackslash': "\\"
        case 'textasciitilde': "~"
        case 'textasciicircum': "^"
        case 'textbar': "|"
        case 'textless': "<"
        case 'textgreater': ">"
        case 'SS': "\u00A7"
        case 'P': "\u00B6"
        case 'ss': "\u00DF"
        case 'aa': "\u00E5"
        case 'AA': "\u00C5"
        case 'ae': "\u00E6"
        case 'AE': "\u00C6"
        case 'oe': "\u0153"
        case 'OE': "\u0152"
        case 'o': "\u00F8"
        case 'O': "\u00D8"
        case 'l': "\u0142"
        case 'L': "\u0141"
        case 'i': "\u0131"
        case 'j': "\u0237"

        // ---- skip commands ----
        case 'setcounter': null
        case 'newcounter': null
        case 'addtocounter': null
        case 'stepcounter': null
        case 'comment': null
        case 'picture': null
        case 'put': null
        case 'line': null
        case 'vector': null
        case 'circle': null
        case 'oval': null
        case 'bezier': null
        case 'qbezier': null
        case 'unitlength': null
        case 'linethickness': null
        case 'thinlines': null
        case 'thicklines': null

        // ---- other ----
        case 'includegraphics': render_includegraphics(el)
        case 'marginpar': render_marginpar(el, info)

        default: render_generic(el, info)
    }
}

// ============================================================
// Children rendering — no state threading
// ============================================================

fn render_children(el, from_idx, info) {
    let n = len(el)
    if from_idx >= n { [] }
    else { collect_children(el, from_idx, n, [], info) }
}

fn collect_children(el, i, n, acc, info) {
    if i >= n { acc }
    else {
        let child = el[i]
        let rendered = render_node(child, info)
        let new_acc = if (rendered != null) acc ++ [rendered] else acc
        collect_children(el, i + 1, n, new_acc, info)
    }
}

// ============================================================
// Document structure
// ============================================================

fn render_document(el, info) {
    let doc_body = util.find_child(el, 'document')
    if doc_body != null { render_body(doc_body, info) }
    else {
        let items = render_children(el, 0, info)
        <article class: "latex-document latex-" ++ info.docclass;
            for c in items { c }
        >
    }
}

fn render_body(el, info) {
    let items = render_children(el, 0, info)
    <article class: "latex-document latex-" ++ info.docclass;
        for c in items { c }
    >
}

fn render_maketitle(info) {
    let title_el = if (info.title != null) <div class: "title"; info.title> else null
    let author_el = if (info.author != null) <div class: "author"; info.author> else null
    let date_el = if (info.date != null) <div class: "date"; info.date> else null
    <header class: "latex-title";
        if title_el != null { title_el }
        if author_el != null { author_el }
        if date_el != null { date_el }
    >
}

fn render_toc(info) {
    if len(info.headings) == 0 { null }
    else {
        let items = (for (h in info.headings, let cls = "toc-l" ++ string(h.level))
            <li class: cls;
                <a href: "#" ++ h.id;
                    if h.number != null { <span class: "sec-num"; h.number> }
                    h.text
                >
            >)
        <nav class: "latex-toc";
            <div class: "toc-title"; "Contents">
            <ul; for item in items { item }>
        >
    }
}

// ============================================================
// Headings
// ============================================================

fn render_heading(el, info, html_level) {
    let title_el = el.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let title_id = util.slugify(title_text)

    // look up pre-computed number
    let sec_num = info.heading_nums[title_id]

    // render title children
    let title_children =
        if (title_el != null and title_el is element) render_children(title_el, 0, info)
        else if (title_text != "") [title_text]
        else []

    let num_span = if (sec_num != null) <span class: "sec-num"; sec_num ++ " "> else null
    build_heading_el(html_level, title_id, num_span, title_children)
}

fn build_heading_el(level, id, num_span, children) {
    match level {
        case 1: <h1 id: id; if num_span != null { num_span } for c in children { c }>
        case 2: <h2 id: id; if num_span != null { num_span } for c in children { c }>
        case 3: <h3 id: id; if num_span != null { num_span } for c in children { c }>
        case 4: <h4 id: id; if num_span != null { num_span } for c in children { c }>
        case 5: <h5 id: id; if num_span != null { num_span } for c in children { c }>
        default: <h6 id: id; if num_span != null { num_span } for c in children { c }>
    }
}

// ============================================================
// Paragraphs
// ============================================================

fn render_paragraph(el, info) {
    let items = render_children(el, 0, info)
    if len(items) == 0 { null }
    else if has_block_child(items) {
        let parts = split_around_blocks(items)
        if (len(parts) == 1) parts[0]
        else parts
    }
    else { <p; for c in items { c }> }
}

fn split_around_blocks(items) {
    split_blocks_rec(items, 0, len(items), [], [])
}

fn split_blocks_rec(items, i, n, current, acc) {
    if i >= n { flush_inline(current, acc) }
    else {
        let item = items[i]
        if is_block_element(item) {
            let flushed = flush_inline(current, acc)
            split_blocks_rec(items, i + 1, n, [], flushed ++ [item])
        } else {
            split_blocks_rec(items, i + 1, n, current ++ [item], acc)
        }
    }
}

fn flush_inline(items, acc) {
    if (len(items) == 0) acc
    else acc ++ [<p; for c in items { c }>]
}

fn has_block_child(items) {
    check_block_rec(items, 0, len(items))
}

fn check_block_rec(items, i, n) {
    if (i >= n) false
    else if (is_block_element(items[i])) true
    else check_block_rec(items, i + 1, n)
}

fn is_block_element(item) {
    if (item is element) is_block_tag(string(name(item)))
    else false
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
// Text styling
// ============================================================

fn render_styled(el, info, html_tag) {
    let items = render_children(el, 0, info)
    match html_tag {
        case "b": <strong; for c in items { c }>
        case "i": <em; for c in items { c }>
        case "em": <em; for c in items { c }>
        case "u": <u; for c in items { c }>
        default: <span; for c in items { c }>
    }
}

fn render_code(el, info) {
    let items = render_children(el, 0, info)
    <code class: "latex-code"; for c in items { c }>
}

fn render_verb(el) {
    let t = util.text_of(el)
    <code class: "latex-code"; t>
}

fn render_font(el, info, css_class) {
    let items = render_children(el, 0, info)
    <span class: css_class; for c in items { c }>
}

fn render_size(el, info, cmd_name) {
    let size = sym.get_font_size(cmd_name)
    let items = render_children(el, 0, info)
    if size != null { <span style: "font-size:" ++ size; for c in items { c }> }
    else { <span; for c in items { c }> }
}

fn render_accent(el, info) {
    let cmd = el.command
    let cmd_name = if (cmd != null) cmd else ""
    let base = if (len(el) > 0) util.text_of(el[0]) else ""
    sym.resolve_diacritic(cmd_name, base)
}

fn render_math_env(el, info) {
    let env_name = util.attr_or(el, "name", "equation")
    math_bridge.render_math_env_el(el, env_name)
}

// ============================================================
// Lists
// ============================================================

fn render_itemize(el, info) {
    let flat = flatten_paragraphs(el)
    let items = split_list_items(flat, info)
    <ul class: "latex-itemize"; for li in items { li }>
}

fn render_enumerate(el, info) {
    let flat = flatten_paragraphs(el)
    let items = split_list_items(flat, info)
    <ol class: "latex-enumerate"; for li in items { li }>
}

fn render_description(el, info) {
    let flat = flatten_paragraphs(el)
    let items = split_desc_items(flat, info)
    <dl class: "latex-description"; for item in items { item }>
}

fn flatten_paragraphs(node) {
    let n = len(node)
    flatten_para_rec(node, 0, n, [])
}

fn flatten_para_rec(node, i, n, acc) {
    if i >= n { acc }
    else {
        let child = node[i]
        if (child is element) {
            if (string(name(child)) == "paragraph") {
                let inner = flatten_inner(child, 0, len(child), [])
                flatten_para_rec(node, i + 1, n, acc ++ inner)
            } else { flatten_para_rec(node, i + 1, n, acc ++ [child]) }
        } else { flatten_para_rec(node, i + 1, n, acc ++ [child]) }
    }
}

fn flatten_inner(el, i, n, acc) {
    if (i >= n) acc
    else flatten_inner(el, i + 1, n, acc ++ [el[i]])
}

fn is_item_node(child) {
    if child is element {
        if (string(name(child)) == "item") true else false
    } else { false }
}

fn split_list_items(flat, info) {
    split_items_rec(flat, 0, len(flat), [], [], info)
}

fn split_items_rec(flat, i, n, current, acc, info) {
    if (i >= n) {
        if (len(current) > 0) acc ++ [<li; for c in current { c }>]
        else acc
    } else if (is_item_node(flat[i])) {
        let flushed = if (len(current) > 0) acc ++ [<li; for c in current { c }>]
            else acc
        split_items_rec(flat, i + 1, n, [], flushed, info)
    } else {
        let rendered = render_node(flat[i], info)
        let new_current = if (rendered != null) current ++ [rendered] else current
        split_items_rec(flat, i + 1, n, new_current, acc, info)
    }
}

fn split_desc_items(flat, info) {
    split_desc_rec(flat, 0, len(flat), [], null, [], info)
}

fn split_desc_rec(flat, i, n, acc, term, content, info) {
    if i >= n { flush_desc(acc, term, content) }
    else if is_item_node(flat[i]) {
        let flushed = flush_desc(acc, term, content)
        let label_text = get_item_label(flat[i])
        let new_term = if (label_text != null) <dt; label_text> else <dt>
        split_desc_rec(flat, i + 1, n, flushed, new_term, [], info)
    } else {
        let rendered = render_node(flat[i], info)
        let new_content = if (rendered != null) content ++ [rendered] else content
        split_desc_rec(flat, i + 1, n, acc, term, new_content, info)
    }
}

fn flush_desc(acc, term, content) {
    if term != null {
        let dd = if (len(content) > 0) <dd; for c in content { c }> else <dd>
        acc ++ [term, dd]
    } else if len(content) > 0 {
        acc ++ [<dd; for c in content { c }>]
    } else { acc }
}

fn get_item_label(item_node) {
    let opt = util.find_child(item_node, 'brack_group')
    if opt != null { util.text_of(opt) }
    else {
        if (len(item_node) > 0) util.text_of(item_node) else null
    }
}

// ============================================================
// Environments
// ============================================================

fn render_env_block(el, info, html_tag, css_class) {
    let items = render_children(el, 0, info)
    match html_tag {
        case "blockquote": <blockquote class: css_class; for c in items { c }>
        default: <div class: css_class; for c in items { c }>
    }
}

fn render_env_div(el, info, css_class, style) {
    let items = render_children(el, 0, info)
    if (style != null) {
        <div class: css_class, style: style; for c in items { c }>
    } else {
        <div class: css_class; for c in items { c }>
    }
}

fn render_verbatim_env(el) {
    let t = util.text_of(el)
    <pre class: "latex-verbatim"; <code; t>>
}

fn render_abstract(el, info) {
    let items = render_children(el, 0, info)
    <div class: "latex-abstract";
        <div class: "abstract-title"; "Abstract">
        for c in items { c }
    >
}

fn render_figure(el, info) {
    let items = render_children(el, 0, info)
    // extract caption
    let caption = util.find_child(el, 'caption')
    let cap_text = if (caption != null) util.text_of(caption) else null
    // look up figure number from info
    let fig_num = get_figure_num(el, info)
    let caption_el = if (cap_text != null) {
        <figcaption;
            <strong; "Figure " ++ string(fig_num) ++ ": ">
            cap_text
        >
    } else null

    <figure class: "latex-figure";
        for c in items { c }
        if caption_el != null { caption_el }
    >
}

fn get_figure_num(el, info) {
    let fig_text = trim(util.text_of(el))
    find_figure_num(info.figures, fig_text, 0)
}

fn find_figure_num(figures, text, i) {
    if (i >= len(figures)) 0
    else check_figure_match(figures, text, i)
}

fn check_figure_match(figures, text, i) {
    let entry = figures[i]
    if (entry.content == text) entry.number
    else find_figure_num(figures, text, i + 1)
}

fn render_multicols(el, info) {
    let col_group = util.find_child(el, 'curly_group')
    let cols = if (col_group != null) util.text_of(col_group) else "2"
    let items = render_children(el, 0, info)
    <div class: "latex-multicols", style: "column-count:" ++ trim(cols);
        for c in items { c }
    >
}

// ============================================================
// Theorem-like environments
// ============================================================

fn render_theorem_env(el, info, display_name, env_type) {
    let items = render_children(el, 0, info)
    let env_num = get_theorem_num(el, info, env_type)
    let heading = display_name ++ " " ++ string(env_num) ++ "."
    <div class: "latex-theorem latex-" ++ env_type;
        <strong class: "latex-theorem-head"; heading>
        " "
        for c in items { c }
    >
}

fn render_proof_env(el, info) {
    let items = render_children(el, 0, info)
    <div class: "latex-proof";
        <em class: "latex-proof-head"; "Proof.">
        " "
        for c in items { c }
        <span class: "latex-qed"; "\u25A1">
    >
}

fn get_theorem_num(el, info, env_type) {
    let thm_text = trim(util.text_of(el))
    find_theorem_num(info.theorems, thm_text, env_type, 0)
}

fn find_theorem_num(theorems, text, env_type, i) {
    if (i >= len(theorems)) 0
    else check_theorem_match(theorems, text, env_type, i)
}

fn check_theorem_match(theorems, text, env_type, i) {
    let entry = theorems[i]
    if (entry.kind == env_type and entry.content == text) entry.number
    else find_theorem_num(theorems, text, env_type, i + 1)
}

// ============================================================
// Tables
// ============================================================

fn render_table_env(el, info) {
    let items = render_children(el, 0, info)
    let caption = util.find_child(el, 'caption')
    let cap_text = if (caption != null) util.text_of(caption) else null
    let tab_num = get_table_num(el, info)
    let caption_el = if (cap_text != null) {
        <div class: "latex-table-caption";
            <strong; "Table " ++ string(tab_num) ++ ": ">
            cap_text
        >
    } else null

    <div class: "latex-table-wrapper";
        if caption_el != null { caption_el }
        for c in items { c }
    >
}

fn get_table_num(el, info) {
    let tab_text = trim(util.text_of(el))
    find_table_num(info.tables, tab_text, 0)
}

fn find_table_num(tables, text, i) {
    if (i >= len(tables)) 0
    else check_table_match(tables, text, i)
}

fn check_table_match(tables, text, i) {
    let entry = tables[i]
    if (entry.content == text) entry.number
    else find_table_num(tables, text, i + 1)
}

fn render_tabular(el, info) {
    let col_spec = parse_col_spec(el)
    let content = find_tabular_content(el)
    let rows = split_rows(content, col_spec, info)
    <table class: "latex-tabular";
        <tbody;
            for row in rows { row }
        >
    >
}

// parse column spec from first curly_group child: "|l|c|r|" → ["left","center","right"]
fn parse_col_spec(el) {
    let cg = find_curly_group(el, 0)
    if (cg != null) extract_alignments(trim(util.text_of(cg)), 0, [])
    else []
}

fn find_curly_group(el, i) {
    if (i >= len(el)) null
    else check_curly(el, i)
}

fn check_curly(el, i) {
    let child = el[i]
    if ((child is element) and name(child) == 'curly_group') child
    else find_curly_group(el, i + 1)
}

fn extract_alignments(spec, i, acc) {
    if (i >= len(spec)) acc
    else extract_one_align(spec, i, acc)
}

fn extract_one_align(spec, i, acc) {
    let ch = slice(spec, i, i + 1)
    if (ch == "l") extract_alignments(spec, i + 1, acc ++ ["left"])
    else if (ch == "c") extract_alignments(spec, i + 1, acc ++ ["center"])
    else if (ch == "r") extract_alignments(spec, i + 1, acc ++ ["right"])
    else extract_alignments(spec, i + 1, acc)
}

// find the paragraph child of tabular that contains the actual content
fn find_tabular_content(el) {
    find_para_child(el, 0)
}

fn find_para_child(el, i) {
    if (i >= len(el)) el
    else check_para(el, i)
}

fn check_para(el, i) {
    let child = el[i]
    if ((child is element) and name(child) == 'paragraph') child
    else find_para_child(el, i + 1)
}

fn split_rows(content, col_spec, info) {
    collect_rows(content, 0, len(content), [], [], col_spec, info)
}

fn collect_rows(el, i, n, current_cells, acc_rows, col_spec, info) {
    if (i >= n) finalize_rows(current_cells, acc_rows, col_spec)
    else route_child(el, i, n, current_cells, acc_rows, col_spec, info)
}

fn finalize_rows(current_cells, acc_rows, col_spec) {
    let final_row = make_row(current_cells, col_spec)
    if (final_row != null) acc_rows ++ [final_row] else acc_rows
}

fn route_child(el, i, n, current_cells, acc_rows, col_spec, info) {
    let child = el[i]
    if (child is string) handle_string_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child is symbol) handle_symbol_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child is element) handle_element_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
}

fn handle_symbol_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let sv = string(child)
    if (sv == "alignment_tab") collect_rows(el, i + 1, n, current_cells ++ [null], acc_rows, col_spec, info)
    else if (sv == "row_sep") handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info)
    else collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
}

fn handle_string_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    handle_text_content(el, i, n, current_cells, acc_rows, col_spec, info, child)
}

fn handle_text_content(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let trimmed = trim(child)
    if (len(trimmed) == 0) collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
    else add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed)
}

fn add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed) {
    let new_cells = append_to_last_cell(current_cells, trimmed)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn handle_element_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let child_name = name(child)
    if (child_name == 'linebreak_command') handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info)
    else if (child_name == 'hline') collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
    else handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
}

fn handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info) {
    let row = make_row(current_cells, col_spec)
    let new_rows = if (row != null) acc_rows ++ [row] else acc_rows
    collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
}

fn handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let rendered = render_node(child, info)
    let new_cells = append_to_last_cell(current_cells, rendered)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn get_align(col_spec, idx) {
    if (idx < len(col_spec)) col_spec[idx]
    else "left"
}

fn cell_to_td_aligned(c, align) {
    if (align == "left") cell_to_td_plain(c)
    else cell_to_td_styled(c, align)
}

fn cell_to_td_plain(c) {
    if c is array { <td; for item in c { item }> }
    else if c != null { <td; c> }
    else { <td> }
}

fn cell_to_td_styled(c, align) {
    let style = "text-align: " ++ align
    if c is array { <td style: style; for item in c { item }> }
    else if c != null { <td style: style; c> }
    else { <td style: style> }
}

fn make_row(cells, col_spec) {
    if (len(cells) == 0) null
    else if (all_cells_empty(cells, 0)) null
    else make_row_inner(cells, col_spec)
}

fn all_cells_empty(cells, i) {
    if (i >= len(cells)) true
    else if (cells[i] != null) false
    else all_cells_empty(cells, i + 1)
}

fn make_row_inner(cells, col_spec) {
    let tds = build_tds(cells, col_spec, 0, len(cells), [])
    <tr; for td in tds { td }>
}

fn build_tds(cells, col_spec, i, n, acc) {
    if (i >= n) acc
    else build_next_td(cells, col_spec, i, n, acc)
}

fn build_next_td(cells, col_spec, i, n, acc) {
    let td = cell_to_td_aligned(cells[i], get_align(col_spec, i))
    let new_acc = acc ++ [td]
    build_tds(cells, col_spec, i + 1, n, new_acc)
}

fn append_to_last_cell(cells, content) {
    if (content == null) cells
    else if (len(cells) == 0) [[content]]
    else set_last_cell(cells, content)
}

fn set_last_cell(cells, content) {
    let last_idx = len(cells) - 1
    let last = cells[last_idx]
    let new_val = compute_new_cell(last, content)
    slice(cells, 0, last_idx) ++ [new_val]
}

fn compute_new_cell(last, content) {
    if (last == null) wrap_content(content)
    else if (last is array) last ++ [content]
    else [last, content]
}

fn wrap_content(content) {
    [content]
}

fn replace_last(arr, new_val) {
    slice(arr, 0, len(arr) - 1) ++ [new_val]
}

// ============================================================
// Cross references
// ============================================================

fn render_label(el, info) {
    let label_name = trim(util.text_of(el))
    <a id: util.slugify(label_name)>
}

fn render_ref(el, info) {
    let ref_name = trim(util.text_of(el))
    let label_info = info.labels[ref_name]
    if (label_info != null) {
        <a class: "latex-ref", href: "#" ++ label_info.id;
            label_info.number
        >
    } else {
        <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name);
            "??"
        >
    }
}

fn render_href(el, info) {
    let n = len(el)
    let url = if (n > 0) util.text_of(el[0]) else "#"
    let display = if (n > 1) render_children(el, 1, info) else [url]
    <a href: url; for d in display { d }>
}

fn render_url(el) {
    let url = util.text_of(el)
    <a class: "latex-url", href: url; url>
}

fn render_cite(el, info) {
    let cite_key = trim(util.text_of(el))
    let bib_num = find_bib_num(info.bibitems, cite_key, 0)
    let display_num = if (bib_num > 0) string(bib_num) else "?"
    <a class: "latex-cite", href: "#bib-" ++ cite_key; "[" ++ display_num ++ "]">
}

fn find_bib_num(bibitems, key, i) {
    if (i >= len(bibitems)) 0
    else check_bib_match(bibitems, key, i)
}

fn check_bib_match(bibitems, key, i) {
    let entry = bibitems[i]
    if (entry.key == key) entry.number
    else find_bib_num(bibitems, key, i + 1)
}

fn render_bibliography(el, info) {
    let items = render_bib_items(el, info)
    <section class: "latex-bibliography";
        <h2; "References">
        <ol class: "latex-bib-list";
            for item in items { item }
        >
    >
}

fn render_bib_items(el, info) {
    collect_bib_entries(el, 0, len(el), info, [], null)
}

// walk children of thebibliography: each bibitem starts a new entry,
// content between bibitems is the entry text
fn collect_bib_entries(el, i, n, info, acc, current_key) {
    if (i >= n) finalize_bib_entries(acc, current_key, [])
    else collect_bib_entry_at(el, i, n, info, acc, current_key)
}

fn collect_bib_entry_at(el, i, n, info, acc, current_key) {
    let child = el[i]
    if ((child is element) and name(child) == 'bibitem') start_bib_entry(el, i, n, info, acc, child)
    else if ((child is element) and name(child) == 'paragraph') collect_bib_in_paragraph(el, i, n, info, acc, current_key, child)
    else collect_bib_entries(el, i + 1, n, info, acc, current_key)
}

fn start_bib_entry(el, i, n, info, acc, bibitem) {
    let key = trim(util.text_of(bibitem))
    collect_bib_entries(el, i + 1, n, info, acc, key)
}

fn collect_bib_in_paragraph(el, i, n, info, acc, current_key, para) {
    // process paragraph children to split by bibitem
    let result = collect_bib_from_para(para, 0, len(para), info, acc, current_key)
    collect_bib_entries(el, i + 1, n, info, result.acc, result.key)
}

fn collect_bib_from_para(para, i, n, info, acc, current_key) {
    if (i >= n) make_bib_state(acc, current_key)
    else collect_bib_from_para_at(para, i, n, info, acc, current_key)
}

fn collect_bib_from_para_at(para, i, n, info, acc, current_key) {
    let child = para[i]
    if ((child is element) and name(child) == 'bibitem') start_bib_from_para(para, i, n, info, acc, child)
    else accumulate_bib_content(para, i, n, info, acc, current_key, child)
}

fn start_bib_from_para(para, i, n, info, acc, bibitem) {
    let key = trim(util.text_of(bibitem))
    collect_bib_from_para(para, i + 1, n, info, acc, key)
}

fn accumulate_bib_content(para, i, n, info, acc, current_key, child) {
    if (current_key == null) collect_bib_from_para(para, i + 1, n, info, acc, current_key)
    else add_bib_content(para, i, n, info, acc, current_key, child)
}

fn add_bib_content(para, i, n, info, acc, current_key, child) {
    let rendered = render_node(child, info)
    let new_entry = make_bib_item_entry(current_key, rendered)
    let new_acc = acc ++ [new_entry]
    collect_bib_from_para(para, i + 1, n, info, new_acc, current_key)
}

fn make_bib_state(acc, key) {
    {acc: acc, key: key}
}

fn make_bib_item_entry(key, content) {
    {key: key, content: content}
}

fn finalize_bib_entries(entries, last_key, acc) {
    if (len(entries) == 0) acc
    else group_bib_entries(entries, acc)
}

fn group_bib_entries(entries, acc) {
    // group entries by key into <li> elements
    build_bib_list(entries, 0, len(entries), null, [], [])
}

fn build_bib_list(entries, i, n, cur_key, cur_content, acc) {
    if (i >= n) finish_bib_list(cur_key, cur_content, acc)
    else process_bib_entry(entries, i, n, cur_key, cur_content, acc)
}

fn process_bib_entry(entries, i, n, cur_key, cur_content, acc) {
    let e = entries[i]
    if (cur_key == null or cur_key == e.key) build_bib_list(entries, i + 1, n, e.key, cur_content ++ [e.content], acc)
    else flush_and_continue(entries, i, n, cur_key, cur_content, acc)
}

fn flush_and_continue(entries, i, n, cur_key, cur_content, acc) {
    let li = make_bib_li(cur_key, cur_content)
    let e = entries[i]
    build_bib_list(entries, i + 1, n, e.key, [e.content], acc ++ [li])
}

fn finish_bib_list(cur_key, cur_content, acc) {
    if (cur_key == null) acc
    else acc ++ [make_bib_li(cur_key, cur_content)]
}

fn make_bib_li(key, content) {
    <li id: "bib-" ++ key;
        for c in content { c }
    >
}

// ============================================================
// Footnotes
// ============================================================

fn render_footnote(el, info) {
    let content_text = trim(util.text_of(el))
    let fn_key = util.slugify(content_text)
    let fn_num = find_footnote_num(info.footnotes, fn_key, 0)
    <sup class: "latex-footnote-ref";
        <a href: "#fn-" ++ string(fn_num), id: "fnref-" ++ string(fn_num);
            string(fn_num)
        >
    >
}

fn find_footnote_num(footnotes, key, i) {
    if (i >= len(footnotes)) 0
    else find_footnote_by_key(footnotes, key, i)
}

fn find_footnote_by_key(footnotes, key, i) {
    let entry = footnotes[i]
    let entry_text = trim(util.text_of(entry.node))
    let entry_key = util.slugify(entry_text)
    if (entry_key == key) entry.number
    else find_footnote_num(footnotes, key, i + 1)
}

// ============================================================
// Control symbols, groups, misc
// ============================================================

fn render_control_symbol(el) {
    let t = util.text_of(el)
    let resolved = sym.resolve_special(t)
    if (resolved != null) resolved else t
}

fn render_group(el, info) {
    let items = render_children(el, 0, info)
    if (len(items) == 0) null
    else if (len(items) == 1) items[0]
    else <span; for c in items { c }>
}

fn render_includegraphics(el) {
    let src = trim(util.text_of(el))
    <img class: "latex-image", src: src, alt: src>
}

fn render_marginpar(el, info) {
    let items = render_children(el, 0, info)
    <span class: "latex-cmd-marginpar"; for c in items { c }>
}

fn render_generic(el, info) {
    let n = len(el)
    if n == 0 { null }
    else {
        let items = render_children(el, 0, info)
        if (len(items) == 0) null
        else if (len(items) == 1) items[0]
        else <span class: "latex-cmd-" ++ name(el); for c in items { c }>
    }
}
