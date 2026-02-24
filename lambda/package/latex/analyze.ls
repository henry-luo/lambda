// latex/analyze.ls — Pass 1: Walk AST to collect document info
// Extracts preamble, computes section/figure/footnote numbers,
// collects headings (TOC), labels, footnotes.
// Returns immutable DocInfo used by render pass.

import util: .lambda.package.latex.util
import dc_article: .lambda.package.latex.docclass.article
import dc_book: .lambda.package.latex.docclass.book
import dc_report: .lambda.package.latex.docclass.report

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
            part: 0, chapter: 0, section: 0, subsection: 0, subsubsection: 0,
            paragraph: 0, figure: 0, table: 0, equation: 0, footnote: 0,
            theorem: 0, lemma: 0, corollary: 0, proposition: 0,
            definition: 0, example: 0, remark: 0
        },
        headings: [],
        heading_nums: {},
        labels: {},
        footnote_map: {},
        footnotes: [],
        figures: [],
        tables: [],
        theorems: [],
        bibitems: [],
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
        footnotes: result.footnotes,
        figures: result.figures,
        tables: result.tables,
        theorems: result.theorems,
        bibitems: result.bibitems
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
        case 'part': walk_heading(el, state, "part", 0)
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

        // ---- theorem-like environments ----
        case 'theorem': walk_numbered_env(el, state, "theorem")
        case 'lemma': walk_numbered_env(el, state, "lemma")
        case 'corollary': walk_numbered_env(el, state, "corollary")
        case 'proposition': walk_numbered_env(el, state, "proposition")
        case 'definition': walk_numbered_env(el, state, "definition")
        case 'example': walk_numbered_env(el, state, "example")
        case 'remark': walk_numbered_env(el, state, "remark")

        // ---- bibliography ----
        case 'thebibliography': walk_bibliography(el, state)
        case 'bibitem': walk_bibitem(el, state)

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
    let sec_num = compute_section_num(new_counters, counter_name, state.docclass)

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
    let new_headings = state.headings ++ [entry]

    let new_state = {
        counters: new_counters,
        headings: new_headings,
        heading_nums: new_heading_nums.heading_nums,
        slug_counts: new_slug_counts,
        state
    }
    // continue walking children (might contain labels)
    walk_children(el, 0, len(el), new_state)
}

fn compute_section_num(counters, counter_name, docclass) {
    if (docclass == "book") dc_book.format_section_number(counters, counter_name)
    else if (docclass == "report") dc_report.format_section_number(counters, counter_name)
    else dc_article.format_section_number(counters, counter_name)
}

// ============================================================
// Figures, tables, equations
// ============================================================

fn walk_figure(el, state) {
    let new_counters = step_counter(state.counters, "figure")
    let fig_num = new_counters.figure
    let fig_text = trim(util.text_of(el))
    let entry = {number: fig_num, content: fig_text}
    let new_figures = state.figures ++ [entry]
    let new_state = {counters: new_counters, figures: new_figures, state}
    walk_children(el, 0, len(el), new_state)
}

fn walk_table(el, state) {
    let new_counters = step_counter(state.counters, "table")
    let tab_num = new_counters.table
    let tab_text = trim(util.text_of(el))
    let entry = {number: tab_num, content: tab_text}
    let new_tables = state.tables ++ [entry]
    let new_state = {counters: new_counters, tables: new_tables, state}
    walk_children(el, 0, len(el), new_state)
}

fn walk_equation(el, state) {
    let new_counters = step_counter(state.counters, "equation")
    let new_state = {counters: new_counters, state}
    walk_children(el, 0, len(el), new_state)
}

fn walk_numbered_env(el, state, env_type) {
    let new_counters = step_counter(state.counters, env_type)
    let env_num = get_env_counter(new_counters, env_type)
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(env_type, env_num, env_text)
    let new_theorems = state.theorems ++ [entry]
    let new_state = {counters: new_counters, theorems: new_theorems, state}
    walk_children(el, 0, len(el), new_state)
}

fn make_thm_entry(k, n, c) {
    {kind: k, number: n, content: c}
}

// ============================================================
// Bibliography
// ============================================================

fn walk_bibliography(el, state) {
    walk_children(el, 0, len(el), state)
}

fn walk_bibitem(el, state) {
    let bib_key = trim(util.text_of(el))
    let bib_num = len(state.bibitems) + 1
    let entry = make_bib_entry(bib_key, bib_num)
    let new_bibitems = state.bibitems ++ [entry]
    {bibitems: new_bibitems, state}
}

fn make_bib_entry(k, n) {
    {key: k, number: n}
}

fn get_env_counter(counters, env_type) {
    match env_type {
        case "theorem": counters.theorem
        case "lemma": counters.lemma
        case "corollary": counters.corollary
        case "proposition": counters.proposition
        case "definition": counters.definition
        case "example": counters.example
        case "remark": counters.remark
        default: 0
    }
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
    let entry = {number: fn_num, node: el}
    let new_fn_map = {footnote_map: {fn_key: entry, state.footnote_map}}
    let new_footnotes = state.footnotes ++ [entry]
    {counters: new_counters, footnote_map: new_fn_map.footnote_map, footnotes: new_footnotes, state}
}

// ============================================================
// Counter operations
// ============================================================

fn step_counter(counters, counter_name) {
    match counter_name {        case "part":
            ({part: counters.part + 1, counters})        case "chapter":
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
        case "theorem":
            {theorem: counters.theorem + 1, counters}
        case "lemma":
            {lemma: counters.lemma + 1, counters}
        case "corollary":
            {corollary: counters.corollary + 1, counters}
        case "proposition":
            {proposition: counters.proposition + 1, counters}
        case "definition":
            {definition: counters.definition + 1, counters}
        case "example":
            {example: counters.example + 1, counters}
        case "remark":
            {remark: counters.remark + 1, counters}
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
