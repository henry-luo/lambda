// latex/render.ls — Pass 2: Render AST to HTML elements
// Uses pre-computed DocInfo from analyze.ls (read-only, no state threading).
// render_node(node, info) returns element|string|null directly.

import util: .util
import sym: .symbols
import math_bridge: .math_bridge
import spacing: .elements.spacing
import macros: .macros
import boxes: .elements.boxes
import font_decl: .elements.font_decl
import color: .elements.color
import picture: .elements.picture

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
        case 'part': render_heading(el, info, 1)
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
        case 'textsf': render_font(el, info, "latex-sf")
        case 'textsc': render_font(el, info, "latex-sc")
        case 'textsl': render_font(el, info, "latex-sl")
        case 'textrm': render_font(el, info, "latex-rm")

        // ---- font declarations (handled in render_group, standalone = no-op) ----
        case 'itshape': null
        case 'bfseries': null
        case 'ttfamily': null
        case 'rmfamily': null
        case 'sffamily': null
        case 'scshape': null
        case 'slshape': null
        case 'upshape': null
        case 'mdseries': null

        // ---- alignment declarations (handled in render_group, standalone = no-op) ----
        case 'centering': null
        case 'raggedright': null
        case 'raggedleft': null

        // ---- appendix marker (handled in analyze pass) ----
        case 'appendix': null

        // ---- color commands ----
        case 'textcolor': color.render_textcolor(el, render_children(el, 1, info), info.custom_colors)
        case 'colorbox': color.render_colorbox(el, render_children(el, 1, info), info.custom_colors)
        case 'fcolorbox': color.render_fcolorbox(el, render_children(el, 2, info), info.custom_colors)
        case 'definecolor': null
        case 'pagecolor': null
        case 'color': null

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
        case 'picture': picture.render_picture(el)
        case 'minipage': render_env_div(el, info, "latex-minipage", null)
        case 'multicols': render_multicols(el, info)

        // ---- theorem-like environments ----
        // check custom \newtheorem defs first (for shared counters, starred variants)
        case 'theorem': render_theorem_like(el, info, "Theorem", "theorem")
        case 'lemma': render_theorem_like(el, info, "Lemma", "lemma")
        case 'corollary': render_theorem_like(el, info, "Corollary", "corollary")
        case 'proposition': render_theorem_like(el, info, "Proposition", "proposition")
        case 'definition': render_theorem_like(el, info, "Definition", "definition")
        case 'example': render_theorem_like(el, info, "Example", "example")
        case 'remark': render_theorem_like(el, info, "Remark", "remark")
        case 'proof': render_proof_env(el, info)

        // ---- boxes ----
        case 'fbox': boxes.render_fbox(render_children(el, 0, info))
        case 'mbox': boxes.render_mbox(render_children(el, 0, info))
        case 'frame': boxes.render_frame(render_children(el, 0, info))
        case 'makebox': boxes.render_makebox(el, render_children_skip_brack(el, info))
        case 'framebox': boxes.render_framebox(el, render_children_skip_brack(el, info))
        case 'parbox': boxes.render_parbox(el, render_children_skip_brack(el, info))
        case 'raisebox': boxes.render_raisebox(el, render_children_skip_brack(el, info))
        case 'llap': boxes.render_llap(render_children(el, 0, info))
        case 'rlap': boxes.render_rlap(render_children(el, 0, info))
        case 'smash': boxes.render_smash(render_children(el, 0, info))
        case 'phantom': boxes.render_phantom(render_children(el, 0, info))
        case 'hphantom': boxes.render_hphantom(render_children(el, 0, info))
        case 'vphantom': boxes.render_vphantom(render_children(el, 0, info))

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
        case 'bigskip': spacing.render_bigskip_el()
        case 'medskip': spacing.render_medskip_el()
        case 'smallskip': spacing.render_smallskip_el()
        case 'bigbreak': spacing.render_bigbreak_el()
        case 'medbreak': spacing.render_medbreak_el()
        case 'smallbreak': spacing.render_smallbreak_el()
        case 'quad': spacing.render_quad_el()
        case 'qquad': spacing.render_qquad_el()
        case 'enspace': spacing.render_enspace_el()
        case 'thinspace': spacing.render_thinspace_el()
        case 'negthinspace': spacing.render_negthinspace_el()
        case 'noindent': spacing.render_noindent_el()

        // ---- cross references ----
        case 'label': render_label(el, info)
        case 'ref': render_ref(el, info)
        case 'autoref': render_autoref(el, info)
        case 'nameref': render_nameref(el, info)
        case 'href': render_href(el, info)
        case 'url': render_url(el)
        case 'cite': render_cite(el, info)

        // ---- bibliography ----
        case 'thebibliography': render_bibliography(el, info)
        case 'bibitem': null

        // ---- footnotes ----
        case 'footnote': render_footnote(el, info)

        // ---- captions (rendered by parent figure/table, suppress here) ----
        case 'caption': null

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

        // ---- extended symbols (textgreek, textcomp, gensymb) ----
        default: render_extended_or_generic(el, info)
    }
}

// skip commands that produce null output
let SKIP_COMMANDS = {
    "newcommand": true, "renewcommand": true, "providecommand": true,
    "setcounter": true, "newcounter": true, "addtocounter": true,
    "stepcounter": true, "comment": true,
    // picture sub-commands handled inside picture.render_picture
    "put": true, "line": true, "vector": true,
    "circle": true, "oval": true, "bezier": true, "qbezier": true,
    "unitlength": true, "linethickness": true, "thinlines": true,
    "thicklines": true, "multiput": true, "circle*": true
}

// Fallback: check skip commands, extended symbol map, then macro, then generic
fn render_extended_or_generic(el, info) {
    let tag_str = string(name(el))
    // skip commands that are no-ops
    if (SKIP_COMMANDS[tag_str] == true) { null }
    // other specific commands
    else if (tag_str == "includegraphics") { render_includegraphics(el) }
    else if (tag_str == "marginpar") { render_marginpar(el, info) }
    else { resolve_extended_or_generic(tag_str, el, info) }
}

fn resolve_extended_or_generic(tag_str, el, info) {
    let ext = sym.resolve_extended(tag_str)
    if (ext != null) { ext }
    else { render_generic(el, info) }
}

// ============================================================
// Children rendering — no state threading
// ============================================================

fn render_children(el, from_idx, info) {
    let n = len(el)
    if (from_idx >= n) { [] }
    else { collect_children(el, from_idx, n, [], info) }
}

// render children but skip brack_group elements (used by box commands)
fn render_children_skip_brack(el, info) {
    let n = len(el)
    collect_children_skip_brack(el, 0, n, [], info)
}

fn collect_children_skip_brack(el, i, n, acc, info) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == "brack_group") {
            collect_children_skip_brack(el, i + 1, n, acc, info)
        } else {
            let rendered = render_node(child, info)
            let new_acc = if (rendered != null) acc ++ [rendered] else acc
            collect_children_skip_brack(el, i + 1, n, new_acc, info)
        }
    }
}

fn collect_children(el, i, n, acc, info) {
    if (i >= n) { acc }
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
    if (doc_body != null) { render_body(doc_body, info) }
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
        if (title_el != null) { title_el }
        if (author_el != null) { author_el }
        if (date_el != null) { date_el }
    >
}

fn render_toc(info) {
    if (len(info.headings) == 0) { null }
    else {
        let items = (for (h in info.headings, let cls = "toc-l" ++ string(h.level))
            <li class: cls;
                <a href: "#" ++ h.id;
                    if (h.number != null) { <span class: "sec-num"; h.number> }
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
    let sec_num = util.lookup(info.heading_nums, title_id)

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
        case 1: <h1 id: id; if (num_span != null) { num_span } for c in children { c }>
        case 2: <h2 id: id; if (num_span != null) { num_span } for c in children { c }>
        case 3: <h3 id: id; if (num_span != null) { num_span } for c in children { c }>
        case 4: <h4 id: id; if (num_span != null) { num_span } for c in children { c }>
        case 5: <h5 id: id; if (num_span != null) { num_span } for c in children { c }>
        default: <h6 id: id; if (num_span != null) { num_span } for c in children { c }>
    }
}

// ============================================================
// Paragraphs
// ============================================================

fn render_paragraph(el, info) {
    // check for leading font/alignment declaration
    let decl_tag = find_leading_decl(el, 0, len(el))
    if (decl_tag != null) { render_paragraph_with_decl(el, info, decl_tag) }
    else {
        let items = render_children(el, 0, info)
        if (len(items) == 0) { null }
        else if (has_block_child(items)) {
            let parts = split_around_blocks(items)
            if (len(parts) == 1) parts[0]
            else parts
        }
        else { <p; for c in items { c }> }
    }
}

fn render_paragraph_with_decl(el, info, decl_tag) {
    let items = render_children(el, 0, info)
    if (len(items) == 0) { null }
    else if (font_decl.is_font_decl(decl_tag)) {
        let style = font_decl.font_decl_style(decl_tag)
        <p style: style; for c in items { c }>
    }
    else if (color.is_color_decl(decl_tag)) {
        let style = color.color_decl_style(find_decl_el(el, decl_tag), info.custom_colors)
        <p style: style; for c in items { c }>
    }
    else {
        let style = font_decl.align_decl_style(decl_tag)
        <p style: style; for c in items { c }>
    }
}

fn split_around_blocks(items) {
    split_blocks_rec(items, 0, len(items), [], [])
}

fn split_blocks_rec(items, i, n, current, acc) {
    if (i >= n) { flush_inline(current, acc) }
    else {
        let item = items[i]
        if (is_block_element(item)) {
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
    if (size != null) { <span style: "font-size:" ++ size; for c in items { c }> }
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
    if (i >= n) { acc }
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
    if (child is element) {
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
        // check for custom label on \item[label]
        let custom = get_item_custom_label(flat[i])
        if (custom != null)
            split_items_custom_rec(flat, i + 1, n, [custom], flushed, info)
        else
            split_items_rec(flat, i + 1, n, [], flushed, info)
    } else {
        let rendered = render_node(flat[i], info)
        let new_current = if (rendered != null) current ++ [rendered] else current
        split_items_rec(flat, i + 1, n, new_current, acc, info)
    }
}

// handle items with custom labels — suppress default marker
fn split_items_custom_rec(flat, i, n, current, acc, info) {
    if (i >= n) {
        if (len(current) > 0) acc ++ [<li style: "list-style-type:none"; for c in current { c }>]
        else acc
    } else if (is_item_node(flat[i])) {
        let flushed = if (len(current) > 0) acc ++ [<li style: "list-style-type:none"; for c in current { c }>]
            else acc
        let custom = get_item_custom_label(flat[i])
        if (custom != null)
            split_items_custom_rec(flat, i + 1, n, [custom], flushed, info)
        else
            split_items_rec(flat, i + 1, n, [], flushed, info)
    } else {
        let rendered = render_node(flat[i], info)
        let new_current = if (rendered != null) current ++ [rendered] else current
        split_items_custom_rec(flat, i + 1, n, new_current, acc, info)
    }
}

// get custom label from \item[label] as a rendered span, or null
fn get_item_custom_label(item_node) {
    let opt = util.find_child(item_node, 'brack_group')
    if (opt != null) { <span class: "latex-item-label"; util.text_of(opt) ++ " "> }
    else { null }
}

fn split_desc_items(flat, info) {
    split_desc_rec(flat, 0, len(flat), [], null, [], info)
}

fn split_desc_rec(flat, i, n, acc, term, content, info) {
    if (i >= n) { flush_desc(acc, term, content) }
    else if (is_item_node(flat[i])) {
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
    if (term != null) {
        let dd = if (len(content) > 0) <dd; for c in content { c }> else <dd>
        acc ++ [term, dd]
    } else if (len(content) > 0) {
        acc ++ [<dd; for c in content { c }>]
    } else { acc }
}

fn get_item_label(item_node) {
    let opt = util.find_child(item_node, 'brack_group')
    if (opt != null) { util.text_of(opt) }
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
    // extract caption (may be nested inside paragraph)
    let caption = util.find_descendant(el, 'caption')
    let cap_text = if (caption != null) util.text_of(caption) else null
    // look up figure number from info
    let fig_num = get_figure_num(el, info)
    let caption_el = if (cap_text != null) (
        <figcaption;
            <strong; "Figure " ++ string(fig_num) ++ ": ">
            cap_text
        >
    ) else null

    <figure class: "latex-figure";
        for c in items { c }
        if (caption_el != null) { caption_el }
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

// route theorem-like env through custom defs if available, else default numbered
fn render_theorem_like(el, info, display_name, env_type) {
    let thm_def = util.lookup(info.theorem_defs, env_type)
    if (thm_def != null) render_custom_theorem(el, info, thm_def)
    else render_theorem_env(el, info, display_name, env_type)
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
    let caption = util.find_descendant(el, 'caption')
    let cap_text = if (caption != null) util.text_of(caption) else null
    let tab_num = get_table_num(el, info)
    let caption_el = if (cap_text != null) (
        <div class: "latex-table-caption";
            <strong; "Table " ++ string(tab_num) ++ ": ">
            cap_text
        >
    ) else null

    <div class: "latex-table-wrapper";
        if (caption_el != null) { caption_el }
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
    if (final_row != null) {
        append_row(acc_rows, final_row)
    } else {
        acc_rows
    }
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
    if (trimmed == null or len(trimmed) == 0) collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
    else add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed)
}

fn add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed) {
    let new_cells = append_to_last_cell(current_cells, trimmed)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn handle_element_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let child_name = name(child)
    if (child_name == 'linebreak_command') handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info)
    else if (child_name == 'hline') handle_hline_rule(el, i, n, current_cells, acc_rows, col_spec, info, "latex-hline")
    else if (child_name == 'toprule') handle_hline_rule(el, i, n, current_cells, acc_rows, col_spec, info, "latex-toprule")
    else if (child_name == 'midrule') handle_hline_rule(el, i, n, current_cells, acc_rows, col_spec, info, "latex-midrule")
    else if (child_name == 'bottomrule') handle_hline_rule(el, i, n, current_cells, acc_rows, col_spec, info, "latex-bottomrule")
    else if (child_name == 'multicolumn') handle_multicol_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child_name == 'multirow') handle_multirow_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
}

fn handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info) {
    let row = make_row(current_cells, col_spec)
    if (row != null) {
        let new_rows = append_row(acc_rows, row)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    } else {
        collect_rows(el, i + 1, n, [], acc_rows, col_spec, info)
    }
}

fn append_row(rows, row) {
    let wrapped = [row]
    rows ++ wrapped
}

// handle \hline, \toprule, \midrule, \bottomrule — apply border to previous/next row
fn handle_hline_rule(el, i, n, current_cells, acc_rows, col_spec, info, css_class) {
    // flush current cells if any, then add rule row
    let row = make_row(current_cells, col_spec)
    let rule_row = <tr class: css_class>
    if (row != null) {
        let flushed = append_row(acc_rows, row)
        let new_rows = append_row(flushed, rule_row)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    } else {
        let new_rows = append_row(acc_rows, rule_row)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    }
}

fn handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let rendered = render_node(child, info)
    let new_cells = append_to_last_cell(current_cells, rendered)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

// --- multicolumn / multirow cell handling ---

fn handle_multicol_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let colspan = int(child[0])
    let align = parse_multicol_align(child[1])
    let content = render_span_content(child, 2, info)
    let cell_desc = {type: "multicol", colspan: colspan, align: align, content: content}
    let new_cells = current_cells ++ [cell_desc]
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn handle_multirow_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let rowspan = int(child[0])
    let content = render_span_content(child, 2, info)
    let cell_desc = {type: "multirow", rowspan: rowspan, content: content}
    let new_cells = current_cells ++ [cell_desc]
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn parse_multicol_align(spec) {
    if (spec == null) "left"
    else find_align_in_spec(spec, 0, len(spec))
}

fn find_align_in_spec(spec, i, n) {
    if (i >= n) "left"
    else match_align_char(spec, i, n)
}

fn match_align_char(spec, i, n) {
    let ch = slice(spec, i, i + 1)
    if (ch == "l") "left"
    else if (ch == "c") "center"
    else if (ch == "r") "right"
    else find_align_in_spec(spec, i + 1, n)
}

fn render_span_content(child, start, info) {
    if (start >= len(child)) null
    else if (start == len(child) - 1) render_one_span(child[start], info)
    else render_span_seq(child, start, len(child), info, [])
}

fn render_one_span(c, info) {
    if (c is element) render_node(c, info)
    else c
}

fn render_span_seq(el, i, n, info, acc) {
    if (i >= n) acc
    else render_next_span(el, i, n, info, acc)
}

fn render_next_span(el, i, n, info, acc) {
    let c = el[i]
    let r = render_one_span(c, info)
    render_span_seq(el, i + 1, n, info, acc ++ [r])
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
    if (c is array) { <td; for item in c { item }> }
    else if (c != null) { <td; c> }
    else { <td> }
}

fn cell_to_td_styled(c, align) {
    let style = "text-align: " ++ align
    if (c is array) { <td style: style; for item in c { item }> }
    else if (c != null) { <td style: style; c> }
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
    let tds = build_tds(cells, col_spec, 0, 0, [])
    <tr; for td in tds { td }>
}

fn build_tds(cells, col_spec, ci, col, acc) {
    if (ci >= len(cells)) acc
    else dispatch_td(cells, col_spec, ci, col, acc)
}

fn dispatch_td(cells, col_spec, ci, col, acc) {
    let cell = cells[ci]
    if (is_span_cell(cell, "multicol")) emit_multicol_td(cells, col_spec, ci, col, acc, cell)
    else if (is_span_cell(cell, "multirow")) emit_multirow_td(cells, col_spec, ci, col, acc, cell)
    else emit_regular_td(cells, col_spec, ci, col, acc, cell)
}

fn is_span_cell(cell, t) {
    (cell is map) and (cell.type == t)
}

fn emit_multicol_td(cells, col_spec, ci, col, acc, cell) {
    let td = multicol_td(cell)
    build_tds(cells, col_spec, ci + 1, col + cell.colspan, acc ++ [td])
}

fn multicol_td(cell) {
    let c = cell.content
    <td colspan: string(cell.colspan), style: "text-align: " ++ cell.align; c>
}

fn emit_multirow_td(cells, col_spec, ci, col, acc, cell) {
    let align = get_align(col_spec, col)
    let td = multirow_td(cell, align)
    build_tds(cells, col_spec, ci + 1, col + 1, acc ++ [td])
}

fn multirow_td(cell, align) {
    let c = cell.content
    if (align == "left") { <td rowspan: string(cell.rowspan); c> }
    else { <td rowspan: string(cell.rowspan), style: "text-align: " ++ align; c> }
}

fn emit_regular_td(cells, col_spec, ci, col, acc, cell) {
    let td = cell_to_td_aligned(cell, get_align(col_spec, col))
    build_tds(cells, col_spec, ci + 1, col + 1, acc ++ [td])
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
    let label_info = util.lookup(info.labels, ref_name)
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

fn render_autoref(el, info) {
    let ref_name = trim(util.text_of(el))
    let label_info = util.lookup(info.labels, ref_name)
    if (label_info != null) {
        let prefix = autoref_prefix(label_info.type)
        let display = prefix ++ label_info.number
        <a class: "latex-ref latex-autoref", href: "#" ++ label_info.id;
            display
        >
    } else {
        <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name);
            "??"
        >
    }
}

fn autoref_prefix(label_type) {
    if (label_type == "section") { "Section\u00A0" }
    else if (label_type == "subsection") { "Section\u00A0" }
    else if (label_type == "subsubsection") { "Section\u00A0" }
    else if (label_type == "chapter") { "Chapter\u00A0" }
    else if (label_type == "part") { "Part\u00A0" }
    else if (label_type == "figure") { "Figure\u00A0" }
    else if (label_type == "table") { "Table\u00A0" }
    else if (label_type == "equation") { "Equation\u00A0" }
    else if (label_type == "theorem") { "Theorem\u00A0" }
    else if (label_type == "lemma") { "Lemma\u00A0" }
    else if (label_type == "corollary") { "Corollary\u00A0" }
    else if (label_type == "definition") { "Definition\u00A0" }
    else { label_type ++ "\u00A0" }
}

fn render_nameref(el, info) {
    let ref_name = trim(util.text_of(el))
    let label_info = util.lookup(info.labels, ref_name)
    if (label_info != null) {
        let title_val = label_info.title
        let display = get_nameref_display(title_val, label_info.number)
        <a class: "latex-ref latex-nameref", href: "#" ++ label_info.id;
            display
        >
    } else {
        <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name);
            "??"
        >
    }
}

fn get_nameref_display(title_val, number_val) {
    if (title_val != null and title_val is string and len(title_val) > 0) title_val
    else if (number_val != null) string(number_val)
    else "??"
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
    // check for leading font/alignment declaration in this group
    let decl_tag = find_leading_decl(el, 0, len(el))
    if (decl_tag != null) { render_group_with_decl(el, info, decl_tag) }
    else {
        let items = render_children(el, 0, info)
        if (len(items) == 0) null
        else if (len(items) == 1) items[0]
        else <span; for c in items { c }>
    }
}

// find the first font/alignment/color declaration tag among children (skipping whitespace)
fn find_leading_decl(el, i, n) {
    if (i >= n) { null }
    else {
        let child = el[i]
        if (child is string and len(trim(child)) == 0) {
            find_leading_decl(el, i + 1, n)
        } else if (child is element) {
            let tag_str = string(name(child))
            if (font_decl.is_font_decl(tag_str)) tag_str
            else if (font_decl.is_align_decl(tag_str)) tag_str
            else if (color.is_color_decl(tag_str)) tag_str
            else null
        }
        else { null }
    }
}

// render a group whose first significant child is a font/alignment/color declaration
fn render_group_with_decl(el, info, decl_tag) {
    // render children after the declaration (the decl itself returns null)
    let items = render_children(el, 0, info)
    if (font_decl.is_font_decl(decl_tag))
        font_decl.wrap_font_decl(decl_tag, items)
    else if (color.is_color_decl(decl_tag))
        color.wrap_color_decl(find_decl_el(el, decl_tag), items, info.custom_colors)
    else
        font_decl.wrap_align_decl(decl_tag, items)
}

// find the declaration element node by tag name within a parent
fn find_decl_el(el, tag_str) {
    find_decl_el_rec(el, 0, len(el), tag_str)
}

fn find_decl_el_rec(el, i, n, tag_str) {
    if (i >= n) { el }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == tag_str) child
        else find_decl_el_rec(el, i + 1, n, tag_str)
    }
}

fn render_includegraphics(el) {
    // extract optional arguments [key=val,...] from brack_group
    let brack = util.find_child(el, 'brack_group')
    let opts = util.parse_kv_options(if (brack != null) util.text_of(brack) else null)

    // extract src from non-brack children
    let src = trim(util.text_of_skip_brack(el))

    // build style string from options
    let style = build_img_style(opts)

    if (style != "") {
        <img class: "latex-image", src: src, alt: src, style: style>
    } else {
        <img class: "latex-image", src: src, alt: src>
    }
}

fn build_img_style(opts) {
    let parts0 = []
    // width
    let parts1 = if (opts.width != null) { parts0 ++ ["width:" ++ opts.width] } else parts0
    // height
    let parts2 = if (opts.height != null) { parts1 ++ ["height:" ++ opts.height] } else parts1
    // keepaspectratio (only meaningful with width or height)
    let parts3 = if (opts.keepaspectratio != null) { parts2 ++ ["object-fit:contain"] } else parts2
    // build transform: may combine scale and angle
    let tx0 = []
    let tx1 = if (opts.scale != null) { tx0 ++ ["scale(" ++ opts.scale ++ ")"] } else tx0
    let tx2 = if (opts.angle != null) { tx1 ++ ["rotate(" ++ opts.angle ++ "deg)"] } else tx1
    let parts4 = if (len(tx2) > 0) { parts3 ++ ["transform:" ++ join(tx2, " ")] } else parts3
    // trim + clip → clip-path:inset(top right bottom left)
    // LaTeX trim order: left bottom right top
    let trim_vals = if (opts.trim != null) split(trim(opts.trim), null) else []
    let parts5 = if (opts.trim != null and opts.clip != null and len(trim_vals) == 4) {
        // LaTeX: trim=left bottom right top → CSS: inset(top right bottom left)
        parts4 ++ ["clip-path:inset(" ++ trim_vals[3] ++ " " ++ trim_vals[2] ++ " " ++ trim_vals[1] ++ " " ++ trim_vals[0] ++ ")"]
    } else parts4

    join(parts5, ";")
}

fn render_marginpar(el, info) {
    let items = render_children(el, 0, info)
    <span class: "latex-cmd-marginpar"; for c in items { c }>
}

fn render_generic(el, info) {
    // check if this element is a macro invocation
    let tag_str = string(name(el))
    let macro_def = if (info.macros != null) macros.find_macro(info.macros, tag_str) else null
    if (macro_def != null) { render_macro_invocation(el, info, macro_def) }
    else {
        // check if this is a custom theorem environment
        let thm_def = util.lookup(info.theorem_defs, tag_str)
        if (thm_def != null) { render_custom_theorem(el, info, thm_def) }
        else { render_generic_default(el, info) }
    }
}

fn render_custom_theorem(el, info, thm_def) {
    let items = render_children(el, 0, info)
    let display_name = thm_def.label
    let env_type = thm_def.env_name
    if (thm_def.numbered) {
        // look up counter from theorems list
        let env_num = get_theorem_num(el, info, env_type)
        let heading = display_name ++ " " ++ string(env_num) ++ "."
        <div class: "latex-theorem latex-" ++ env_type;
            <strong class: "latex-theorem-head"; heading>
            " "
            for c in items { c }
        >
    } else {
        // unnumbered variant
        <div class: "latex-theorem latex-" ++ env_type;
            <strong class: "latex-theorem-head"; display_name ++ ".">
            " "
            for c in items { c }
        >
    }
}

fn render_macro_invocation(el, info, macro_def) {
    let args = build_macro_args(el, macro_def)
    let body_items = macros.substitute_body(macro_def.body, args)
    let rendered = render_items(body_items, 0, len(body_items), info, [])
    if (len(rendered) == 0) null
    else if (len(rendered) == 1) rendered[0]
    else <span; for c in rendered { c }>
}

fn build_macro_args(el, macro_def) {
    if (macro_def.default_arg == null) { el }
    else if (has_optional_arg(el)) { el }
    else { prepend_default(el, macro_def.default_arg) }
}

fn has_optional_arg(el) {
    if (len(el) > 0) {
        let first = el[0]
        (first is element and string(name(first)) == "brack_group")
    }
    else { false }
}

fn prepend_default(el, default_val) {
    [default_val] ++ collect_el_children(el, 0, len(el), [])
}

fn collect_el_children(el, i, n, acc) {
    if (i >= n) { acc }
    else { collect_el_children(el, i + 1, n, acc ++ [el[i]]) }
}

fn render_items(items, i, n, info, acc) {
    if (i >= n) { acc }
    else {
        let item = items[i]
        let rendered = render_node(item, info)
        let new_acc = if (rendered != null) acc ++ [rendered] else acc
        render_items(items, i + 1, n, info, new_acc)
    }
}

fn render_generic_default(el, info) {
    let n = len(el)
    if (n == 0) { null }
    else {
        let items = render_children(el, 0, info)
        if (len(items) == 0) null
        else if (len(items) == 1) items[0]
        else <span class: "latex-cmd-" ++ name(el); for c in items { c }>
    }
}
