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
    let doc_body = util.find_child(node, 'document')
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

pub fn extract_preamble_ctx(node, ctx) {
    let n = len(node)
    extract_preamble_rec(node, 0, n, ctx)
}

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
                case 'documentclass': (let cls = util.text_of(child), let trimmed = trim(cls),
                    if (trimmed != "") {docclass: trimmed, ctx} else ctx)
                case 'title': ctx_mod.set_title(ctx, util.text_of(child))
                case 'author': ctx_mod.set_author(ctx, util.text_of(child))
                case 'date': ctx_mod.set_date(ctx, util.text_of(child))
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

    // compute section number directly from counter
    let sec_num = compute_section_num(new_ctx.counters, counter_name, ctx.docclass)

    // extract title
    let title_info = extract_title_info(node)
    let title_text = title_info.text
    let title_id = title_info.id

    // record heading for TOC
    let ctx2 = ctx_mod.add_heading(new_ctx, html_level, sec_num, title_text, title_id)

    // render title children and build element
    let title_children = get_title_children(title_info.el, title_text, ctx2, render_fn)
    let heading = build_heading_element(html_level, title_id, sec_num, title_children)

    {result: heading, ctx: ctx2}
}

pub fn compute_section_num_pub(counters, counter_name, docclass) {
    compute_section_num(counters, counter_name, docclass)
}

fn compute_section_num(counters, counter_name, docclass) {
    match counter_name {
        case "section": string(counters.section)
        case "subsection": string(counters.section) ++ "." ++ string(counters.subsection)
        case "subsubsection": string(counters.section) ++ "." ++ string(counters.subsection) ++ "." ++ string(counters.subsubsection)
        case "chapter": string(counters.chapter)
        default: null
    }
}

fn extract_title_info(node) {
    let title_el = node.title
    let title_text = get_title_text(title_el)
    let title_id = util.slugify(title_text)
    {el: title_el, text: title_text, id: title_id}
}

fn get_title_text(title_el) {
    if (title_el != null) { util.text_of(title_el) }
    else { "" }
}

fn get_title_children(title_el, title_text, ctx, render_fn) {
    if (title_el == null) {
        if (title_text != "") { [title_text] }
        else { [] }
    } else {
        get_title_children_from_el(title_el, title_text, ctx, render_fn)
    }
}

fn get_title_children_from_el(title_el, title_text, ctx, render_fn) {
    if (title_el is element) {
        render_children_sequential(title_el, ctx, render_fn).items
    } else if (title_text != "") { [title_text] }
    else { [] }
}

pub fn build_heading_pub(level, id, sec_num, title_children) {
    build_heading_element(level, id, sec_num, title_children)
}

fn build_heading_element(level, id, sec_num, title_children) {
    let num_span = make_num_span(sec_num)
    match level {
        case 1: build_h1(id, num_span, title_children)
        case 2: build_h2(id, num_span, title_children)
        case 3: build_h3(id, num_span, title_children)
        case 4: build_h4(id, num_span, title_children)
        case 5: build_h5(id, num_span, title_children)
        default: build_h6(id, num_span, title_children)
    }
}

fn make_num_span(sec_num) {
    if (sec_num == null) { null }
    else { <span class: "sec-num"; sec_num ++ " "> }
}

fn build_h1(id, num_span, title_children) {
    <h1 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
}
fn build_h2(id, num_span, title_children) {
    <h2 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
}
fn build_h3(id, num_span, title_children) {
    <h3 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
}
fn build_h4(id, num_span, title_children) {
    <h4 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
}
fn build_h5(id, num_span, title_children) {
    <h5 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
}
fn build_h6(id, num_span, title_children) {
    <h6 id: id;
        if num_span != null { num_span }
        for c in title_children { c }
    >
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
        let items = (for (h in headings, let cls = "toc-l" ++ string(h.level))
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
        let new_acc = accumulate_result(acc, rendered.result)
        render_seq_rec(node, i + 1, n, new_acc, rendered.ctx, render_fn)
    }
}

fn accumulate_result(acc, result) {
    if (result == null) acc
    else if (result is array) acc ++ (for (r in result where r != null) r)
    else acc ++ [result]
}
