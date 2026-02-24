// latex/structure.ls — Document structure: sections, headings, title, preamble
// Handles \documentclass, \title, \author, \maketitle, \section, etc.

import util: .lambda.package.latex.util
import ctx_mod: .lambda.package.latex.context

// ============================================================
// Document-level rendering
// ============================================================

// render the top-level <latex_document> node
// extracts preamble info, then renders the <document> body
pub fn render_document(node, ctx, render_fn) {
    let preamble_ctx = extract_preamble(node, ctx)
    let doc_body = util.find_child(node, "document")
    if (doc_body != null) {
        let body_result = render_body(doc_body, preamble_ctx, render_fn)
        {result: body_result.result, ctx: body_result.ctx}
    } else {
        let children_result = render_children_sequential(node, ctx, render_fn)
        let article = <article class: "latex-document latex-" ++ ctx.docclass;
            for c in children_result.items { c }
        >
        {result: article, ctx: children_result.ctx}
    }
}

// render the <document> element body
pub fn render_body(node, ctx, render_fn) {
    let children_result = render_children_sequential(node, ctx, render_fn)
    let article = <article class: "latex-document latex-" ++ ctx.docclass;
        for c in children_result.items { c }
    >
    {result: article, ctx: children_result.ctx}
}

// ============================================================
// Preamble extraction
// ============================================================

// walk preamble children to extract \title, \author, \date, \documentclass
fn extract_preamble(node, ctx) {
    let n = len(node)
    extract_preamble_rec(node, 0, n, ctx)
}

fn extract_preamble_rec(node, i, n, ctx) {
    if (i >= n) { ctx }
    else {
        let child = node[i]
        let new_ctx = if (child is element) match name(child) {
                case "documentclass": {
                    let cls = util.text_of(child)
                    let trimmed = trim(cls)
                    if (trimmed != "") {ctx, docclass: trimmed} else ctx
                }
                case "title": ctx_mod.set_title(ctx, util.text_of(child))
                case "author": ctx_mod.set_author(ctx, util.text_of(child))
                case "date": ctx_mod.set_date(ctx, util.text_of(child))
                default: ctx
            }
            else ctx
        extract_preamble_rec(node, i + 1, n, new_ctx)
    }
}

// ============================================================
// Section headings
// ============================================================

// render a section heading
// level: 0 = chapter, 1 = section (article), 2 = subsection, etc.
pub fn render_heading(node, ctx, counter_name, html_level, render_fn) {
    // step the counter
    let new_ctx = ctx_mod.step_counter(ctx, counter_name)

    // compute section number
    let sec_level = match counter_name {
        case "chapter": 0
        case "section": if (ctx.docclass == "article") 0 else 1
        case "subsection": if (ctx.docclass == "article") 1 else 2
        case "subsubsection": if (ctx.docclass == "article") 2 else 3
        default: 4
    }
    let number = ctx_mod.format_section_number(new_ctx, sec_level)

    // extract title text
    let title_el = node.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let title_id = util.slugify(title_text)

    // record heading for TOC
    let ctx2 = ctx_mod.add_heading(new_ctx, html_level, number, title_text, title_id)

    // render title children
    let title_children = if (title_el != null and title_el is element) {
        render_children_sequential(title_el, ctx2, render_fn).items
    } else if (title_text != "") { [title_text] }
    else { [] }

    // build the heading element
    let heading = build_heading_element(html_level, title_id, number, title_children)

    {result: heading, ctx: ctx2}
}

fn build_heading_element(level, id, number, title_children) {
    let num_span = if (number != null) <span class: "sec-num"; number> else null
    match level {
        case 1: <h1 id: id; if num_span != null { num_span } for c in title_children { c }>
        case 2: <h2 id: id; if num_span != null { num_span } for c in title_children { c }>
        case 3: <h3 id: id; if num_span != null { num_span } for c in title_children { c }>
        case 4: <h4 id: id; if num_span != null { num_span } for c in title_children { c }>
        case 5: <h5 id: id; if num_span != null { num_span } for c in title_children { c }>
        default: <h6 id: id; if num_span != null { num_span } for c in title_children { c }>
    }
}

// ============================================================
// \maketitle
// ============================================================

pub fn render_maketitle(ctx) {
    let title_el = if (ctx.title != null) <div class: "title"; ctx.title> else null
    let author_el = if (ctx.author != null) <div class: "author"; ctx.author> else null
    let date_el = if (ctx.date != null) <div class: "date"; ctx.date> else null

    let header = <header class: "latex-title";
        if title_el != null { title_el }
        if author_el != null { author_el }
        if date_el != null { date_el }
    >
    {result: header, ctx: ctx}
}

// ============================================================
// Table of contents
// ============================================================

pub fn render_toc(headings) {
    if (len(headings) == 0) { null }
    else {
        let items = (for (h in headings)
            let cls = "toc-l" ++ string(h.level),
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
// Helper: render children sequentially, threading context
// ============================================================

fn render_children_sequential(node, ctx, render_fn) {
    let n = len(node)
    if (n == 0) { {items: [], ctx: ctx} }
    else { render_seq_rec(node, 0, n, [], ctx, render_fn) }
}

fn render_seq_rec(node, i, n, acc, ctx, render_fn) {
    if (i >= n) { {items: acc, ctx: ctx} }
    else {
        let child = node[i]
        let rendered = render_fn(child, ctx)
        let new_acc = if (rendered.result != null) {
            if (rendered.result is array) acc ++ (for (r in rendered.result where r != null) r)
            else acc ++ [rendered.result]
        } else {
            acc
        }
        render_seq_rec(node, i + 1, n, new_acc, rendered.ctx, render_fn)
    }
}
