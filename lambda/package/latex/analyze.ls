// latex/analyze.ls — Pass 1: Walk AST to collect document info
// Extracts preamble, computes section/figure/footnote numbers,
// collects headings (TOC), labels, footnotes.
// Returns immutable DocInfo used by render pass.

import util: .lambda.package.latex.util

// ============================================================
// Public API
// ============================================================

pub fn analyze(ast) {
    let state = {
        docclass: "article",
        title: null,
        author: null,
        date: null,
        counters: {
            chapter: 0, section: 0, subsection: 0, subsubsection: 0,
            paragraph: 0, figure: 0, table: 0, equation: 0, footnote: 0
        },
        headings: [],
        heading_nums: {},
        labels: {},
        footnote_map: {},
        slug_counts: {}
    }
    let result = walk_node(ast, state)
    // return clean DocInfo (drop internal counters)
    {
        docclass: result.docclass,
        title: result.title,
        author: result.author,
        date: result.date,
        headings: result.headings,
        heading_nums: result.heading_nums,
        labels: result.labels,
        footnote_map: result.footnote_map
    }
}

// ============================================================
// Tree walk
// ============================================================

fn walk_node(node, state) {
    if (node == null) state
    else if (node is element) walk_element(node, state)
    else state
}

fn walk_element(el, state) {
    let tag = name(el)
    match tag {
        // ---- preamble ----
        case 'documentclass': walk_documentclass(el, state)
        case 'title': walk_title(el, state)
        case 'author': walk_author(el, state)
        case 'date': walk_date(el, state)

        // ---- sections ----
        case 'chapter': walk_heading(el, state, "chapter", 1)
        case 'section': walk_heading(el, state, "section", 2)
        case 'subsection': walk_heading(el, state, "subsection", 3)
        case 'subsubsection': walk_heading(el, state, "subsubsection", 4)
        case 'paragraph_command': walk_heading(el, state, "paragraph", 5)
        case 'subparagraph': walk_heading(el, state, "paragraph", 6)

        // ---- figures ----
        case 'figure': walk_figure(el, state)

        // ---- tables ----
        case 'table': walk_table(el, state)

        // ---- labels (inside anything) ----
        case 'label': walk_label(el, state)

        // ---- footnotes ----
        case 'footnote': walk_footnote(el, state)

        // ---- equations ----
        case 'equation': walk_equation(el, state)

        // ---- everything else: walk children ----
        default: walk_children(el, 0, len(el), state)
    }
}

fn walk_children(el, i, n, state) {
    if i >= n { state }
    else {
        let child = el[i]
        let new_state = walk_node(child, state)
        walk_children(el, i + 1, n, new_state)
    }
}

// ============================================================
// Preamble extraction
// ============================================================

fn walk_documentclass(el, state) {
    let cls = trim(util.text_of(el))
    if cls != "" { {docclass: cls, state} }
    else { state }
}

fn walk_title(el, state) {
    let t = util.text_of(el)
    {title: t, state}
}

fn walk_author(el, state) {
    let a = util.text_of(el)
    {author: a, state}
}

fn walk_date(el, state) {
    let d = util.text_of(el)
    {date: d, state}
}

// ============================================================
// Section headings
// ============================================================

fn walk_heading(el, state, counter_name, html_level) {
    let new_counters = step_counter(state.counters, counter_name)
    let sec_num = compute_section_num(new_counters, counter_name)

    // extract title text
    let title_el = el.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let base_slug = util.slugify(title_text)

    // deduplicate slug
    let slug_info = make_unique_slug(base_slug, state.slug_counts)
    let slug = slug_info.slug
    let new_slug_counts = slug_info.counts

    // record heading
    let entry = {level: html_level, number: sec_num, text: title_text, id: slug}
    let new_heading_nums = {heading_nums: {slug: sec_num, state.heading_nums}}

    let new_state = {
        counters: new_counters,
        headings: state.headings ++ [entry],
        heading_nums: new_heading_nums.heading_nums,
        slug_counts: new_slug_counts,
        state
    }
    // continue walking children (might contain labels)
    walk_children(el, 0, len(el), new_state)
}

fn compute_section_num(counters, counter_name) {
    match counter_name {
        case "chapter": string(counters.chapter)
        case "section": string(counters.section)
        case "subsection": string(counters.section) ++ "." ++ string(counters.subsection)
        case "subsubsection": string(counters.section) ++ "." ++ string(counters.subsection) ++ "." ++ string(counters.subsubsection)
        case "paragraph": null
        default: null
    }
}

// ============================================================
// Figures, tables, equations
// ============================================================

fn walk_figure(el, state) {
    let new_counters = step_counter(state.counters, "figure")
    let fig_num = new_counters.figure
    // look for label inside figure to record it
    let new_state = {counters: new_counters, state}
    walk_children(el, 0, len(el), new_state)
}

fn walk_table(el, state) {
    let new_counters = step_counter(state.counters, "table")
    let new_state = {counters: new_counters, state}
    walk_children(el, 0, len(el), new_state)
}

fn walk_equation(el, state) {
    let new_counters = step_counter(state.counters, "equation")
    let new_state = {counters: new_counters, state}
    walk_children(el, 0, len(el), new_state)
}

// ============================================================
// Labels
// ============================================================

fn walk_label(el, state) {
    let label_name = trim(util.text_of(el))
    // determine what we're labeling from current counters
    let c = state.counters
    // heuristic: use the most recently stepped counter
    let label_type = "section"
    let label_number = string(c.section)
    let label_id = util.slugify(label_name)

    let entry = {type: label_type, number: label_number, id: label_id}
    let new_labels = {labels: {label_name: entry, state.labels}}
    {labels: new_labels.labels, state}
}

// ============================================================
// Footnotes
// ============================================================

fn walk_footnote(el, state) {
    let new_counters = step_counter(state.counters, "footnote")
    let fn_num = new_counters.footnote
    // key by content text for lookup in pass 2
    let content_text = trim(util.text_of(el))
    let fn_key = util.slugify(content_text)
    let entry = {number: fn_num}
    let new_fn_map = {footnote_map: {fn_key: entry, state.footnote_map}}
    {counters: new_counters, footnote_map: new_fn_map.footnote_map, state}
}

// ============================================================
// Counter operations
// ============================================================

fn step_counter(counters, counter_name) {
    match counter_name {
        case "chapter":
            {chapter: counters.chapter + 1, section: 0, subsection: 0, subsubsection: 0, counters}
        case "section":
            {section: counters.section + 1, subsection: 0, subsubsection: 0, counters}
        case "subsection":
            {subsection: counters.subsection + 1, subsubsection: 0, counters}
        case "subsubsection":
            {subsubsection: counters.subsubsection + 1, counters}
        case "figure":
            {figure: counters.figure + 1, counters}
        case "table":
            {table: counters.table + 1, counters}
        case "equation":
            {equation: counters.equation + 1, counters}
        case "footnote":
            {footnote: counters.footnote + 1, counters}
        default: counters
    }
}

// ============================================================
// Slug deduplication
// ============================================================

fn make_unique_slug(base, counts) {
    let current = counts[base]
    if (current == null) {
        {slug: base, counts: {base: 1, counts}}
    } else {
        let next = current + 1
        {slug: base ++ "-" ++ string(next), counts: {base: next, counts}}
    }
}
