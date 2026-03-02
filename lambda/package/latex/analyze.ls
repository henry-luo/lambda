// latex/analyze.ls — Pass 1: Walk AST to collect document info
// Extracts preamble, computes section/figure/footnote numbers,
// collects headings (TOC), labels, footnotes.
// Returns immutable DocInfo used by render pass.

import util: lambda.package.latex.util
import dc_article: lambda.package.latex.docclass.article
import dc_book: lambda.package.latex.docclass.book
import dc_report: lambda.package.latex.docclass.report

// helper: append a {key, val} entry to an entry list
fn add_entry(entries, k, v) {
    entries ++ [{key: k, val: v}]
}

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
        custom_counters: [],
        headings: [],
        heading_nums: [],
        heading_titles: [],
        labels: [],
        footnote_map: [],
        footnotes: [],
        figures: [],
        tables: [],
        theorems: [],
        bibitems: [],
        slug_counts: [],
        in_appendix: false,
        secnumdepth: 3,
        custom_colors: [],
        theorem_defs: [],
        env_context: "section",
        env_context_num: ""
    }
    let result = walk_node(ast, state)
    // return clean DocInfo (drop internal counters)
    // keep entry arrays — render pass will use lookup functions
    {
        docclass: result.docclass,
        title: result.title,
        author: result.author,
        date: result.date,
        headings: result.headings,
        heading_nums: result.heading_nums,
        heading_titles: result.heading_titles,
        labels: result.labels,
        footnotes: result.footnotes,
        footnote_map: result.footnote_map,
        figures: result.figures,
        tables: result.tables,
        theorems: result.theorems,
        bibitems: result.bibitems,
        secnumdepth: result.secnumdepth,
        custom_colors: result.custom_colors,
        theorem_defs: result.theorem_defs
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
        // check custom \newtheorem defs first (for shared counters, starred variants)
        case 'theorem': walk_theorem_like(el, state, "theorem")
        case 'lemma': walk_theorem_like(el, state, "lemma")
        case 'corollary': walk_theorem_like(el, state, "corollary")
        case 'proposition': walk_theorem_like(el, state, "proposition")
        case 'definition': walk_theorem_like(el, state, "definition")
        case 'example': walk_theorem_like(el, state, "example")
        case 'remark': walk_theorem_like(el, state, "remark")

        // ---- bibliography ----
        case 'thebibliography': walk_bibliography(el, state)
        case 'bibitem': walk_bibitem(el, state)

        // ---- appendix & counters ----
        case 'appendix': walk_appendix(el, state)
        case 'setcounter': walk_setcounter(el, state)

        // ---- color definitions ----
        case 'definecolor': walk_definecolor(el, state)

        // ---- \newtheorem definitions ----
        case 'newtheorem': walk_newtheorem(el, state)
        case 'newtheorem*': walk_newtheorem_star(el, state)

        // ---- everything else: check custom theorem defs, then walk children ----
        default: walk_default(el, tag, state)
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
    if cls != "" { {state, docclass: cls} }
    else { state }
}

fn walk_title(el, state) {
    let t = util.text_of(el)
    {state, title: t}
}

fn walk_author(el, state) {
    let a = util.text_of(el)
    {state, author: a}
}

fn walk_date(el, state) {
    let d = util.text_of(el)
    {state, date: d}
}

// ============================================================
// Section headings
// ============================================================

fn walk_heading(el, state, counter_name, html_level) {
    let new_counters = step_counter(state.counters, counter_name)
    // determine numbering depth for this counter
    let counter_depth = counter_name_to_depth(counter_name)
    let sec_num = if (counter_depth <= state.secnumdepth)
        compute_section_num(new_counters, counter_name, state.docclass, state.in_appendix)
    else null

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
    let new_heading_nums = add_entry(state.heading_nums, slug, sec_num)
    let new_headings = state.headings ++ [entry]
    let sec_num_str = sec_num_to_str(sec_num)

    let new_state = {
        state,
        counters: new_counters,
        headings: new_headings,
        heading_nums: new_heading_nums,
        heading_titles: add_entry(state.heading_titles, counter_name ++ ":" ++ sec_num_str, title_text),
        slug_counts: new_slug_counts,
        env_context: counter_name,
        env_context_num: sec_num_str
    }
    // continue walking children (might contain labels)
    walk_children(el, 0, len(el), new_state)
}

fn sec_num_to_str(sec_num) {
    if (sec_num != null) { string(sec_num) }
    else { "" }
}

// map counter names to numbering depth levels
// matches LaTeX: part=-1, chapter=0, section=1, subsection=2, subsubsection=3, paragraph=4, subparagraph=5
fn counter_name_to_depth(counter_name) {
    match counter_name {
        case "part": -1
        case "chapter": 0
        case "section": 1
        case "subsection": 2
        case "subsubsection": 3
        case "paragraph": 4
        default: 5
    }
}

fn compute_section_num(counters, counter_name, docclass, in_appendix) {
    if (docclass == "book") dc_book.format_section_number(counters, counter_name, in_appendix)
    else if (docclass == "report") dc_report.format_section_number(counters, counter_name, in_appendix)
    else dc_article.format_section_number(counters, counter_name, in_appendix)
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
    let new_state = {state, counters: new_counters, figures: new_figures,
        env_context: "figure", env_context_num: string(fig_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_table(el, state) {
    let new_counters = step_counter(state.counters, "table")
    let tab_num = new_counters.table
    let tab_text = trim(util.text_of(el))
    let entry = {number: tab_num, content: tab_text}
    let new_tables = state.tables ++ [entry]
    let new_state = {state, counters: new_counters, tables: new_tables,
        env_context: "table", env_context_num: string(tab_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_equation(el, state) {
    let new_counters = step_counter(state.counters, "equation")
    let eq_num = new_counters.equation
    let new_state = {state, counters: new_counters,
        env_context: "equation", env_context_num: string(eq_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_numbered_env(el, state, env_type) {
    let new_counters = step_counter(state.counters, env_type)
    let env_num = get_env_counter(new_counters, env_type)
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(env_type, env_num, env_text)
    let new_theorems = state.theorems ++ [entry]
    let new_state = {state, counters: new_counters, theorems: new_theorems,
        env_context: env_type, env_context_num: string(env_num)}
    walk_children(el, 0, len(el), new_state)
}

// route theorem-like env through custom defs if available, else default numbered
fn walk_theorem_like(el, state, env_type) {
    let thm_def = util.lookup(state.theorem_defs, env_type)
    if (thm_def != null) { walk_custom_theorem(el, state, thm_def) }
    else { walk_numbered_env(el, state, env_type) }
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
    {state, bibitems: new_bibitems}
}

fn make_bib_entry(k, n) {
    {key: k, number: n}
}

// ============================================================
// Appendix & counter commands
// ============================================================

fn walk_appendix(el, state) {
    // reset section counter and switch to appendix mode
    let new_counters = {state.counters, section: 0, subsection: 0, subsubsection: 0}
    {state, in_appendix: true, counters: new_counters}
}

fn walk_setcounter(el, state) {
    // children: [counter_name_text, value_text]
    let n = len(el)
    if (n >= 2) {
        let cname = trim(string(el[0]))
        let cval = trim(string(el[1]))
        let val = int(cval)
        if (cname == "secnumdepth" and val != null) {
            {state, secnumdepth: val}
        }
        else { state }
    }
    else { state }
}

fn walk_definecolor(el, state) {
    // children: [name, model, spec]
    let n = len(el)
    if (n >= 3) {
        let cname = trim(string(el[0]))
        let model = trim(string(el[1]))
        let spec = trim(string(el[2]))
        let css = parse_color_model(model, spec)
        let new_colors = add_entry(state.custom_colors, cname, css)
        {state, custom_colors: new_colors}
    }
    else { state }
}

fn parse_color_model(model, spec) {
    if (model == "HTML" or model == "html") "#" ++ spec
    else if (model == "rgb") parse_rgb_float(spec)
    else if (model == "RGB") parse_rgb_int(spec)
    else if (model == "gray") parse_gray(spec)
    else spec
}

fn parse_rgb_float(spec) {
    let parts = split(spec, ",")
    if (len(parts) >= 3) {
        let r = int(float(trim(parts[0])) * 255.0)
        let g = int(float(trim(parts[1])) * 255.0)
        let b = int(float(trim(parts[2])) * 255.0)
        "rgb(" ++ string(r) ++ "," ++ string(g) ++ "," ++ string(b) ++ ")"
    }
    else { spec }
}

fn parse_rgb_int(spec) {
    let parts = split(spec, ",")
    if (len(parts) >= 3)
        "rgb(" ++ trim(parts[0]) ++ "," ++ trim(parts[1]) ++ "," ++ trim(parts[2]) ++ ")"
    else spec
}

fn parse_gray(spec) {
    let val = int(float(spec) * 255.0)
    "rgb(" ++ string(val) ++ "," ++ string(val) ++ "," ++ string(val) ++ ")"
}

// ============================================================
// \newtheorem definitions
// ============================================================

// \newtheorem{name}{Label}       → independent counter
// \newtheorem{name}[counter]{Label} → shared counter with existing type
// \newtheorem*{name}{Label}      → unnumbered (star variant)
fn walk_newtheorem(el, state) {
    let n = len(el)
    if (n < 2) state
    else build_newtheorem_def(el, state, n, true)
}

fn walk_newtheorem_star(el, state) {
    let n = len(el)
    if (n < 2) state
    else build_newtheorem_def(el, state, n, false)
}

fn build_newtheorem_def(el, state, n, is_numbered) {
    let env_name = get_newthm_child_text(el, 0)
    // clean star suffix if present (some parsers may include it)
    let clean_name = if (ends_with(env_name, "*")) slice(env_name, 0, len(env_name) - 1)
                     else env_name
    // check if second child is brack_group (shared counter) or the label
    let second = el[1]
    let has_shared = second is element and string(name(second)) == "brack_group"
    let shared_counter = if (has_shared) trim(util.text_of(second)) else null
    // the label text is the next curly_group or string child
    let label_idx = if (has_shared) 2 else 1
    let label_text = if (label_idx < n) get_newthm_child_text(el, label_idx) else clean_name

    let def = {
        env_name: clean_name,
        label: label_text,
        shared_counter: shared_counter,
        numbered: is_numbered
    }
    let new_defs = add_entry(state.theorem_defs, clean_name, def)
    // add counter for this theorem type if not shared and not unnumbered
    let is_unnumbered = is_numbered == false
    let new_custom_counters = if (is_unnumbered or shared_counter != null) state.custom_counters
                      else add_entry(state.custom_counters, clean_name, 0)
    {state, theorem_defs: new_defs, custom_counters: new_custom_counters}
}

fn get_newthm_child_text(el, idx) {
    let child = el[idx]
    if (child is string) { trim(child) }
    else if (child is element) { trim(util.text_of(child)) }
    else { string(child) }
}

// ============================================================
// Default handler: check custom theorem defs
// ============================================================

fn walk_default(el, tag, state) {
    let tag_str = string(tag)
    let thm_def = util.lookup(state.theorem_defs, tag_str)
    if (thm_def != null) { walk_custom_theorem(el, state, thm_def) }
    else { walk_children(el, 0, len(el), state) }
}

fn walk_custom_theorem(el, state, thm_def) {
    if (thm_def.numbered == false) {
        // unnumbered: just walk children
        let new_state = {state, env_context: thm_def.env_name, env_context_num: ""}
        walk_children(el, 0, len(el), new_state)
    } else {
        walk_custom_theorem_numbered(el, state, thm_def)
    }
}

fn walk_custom_theorem_numbered(el, state, thm_def) {
    // determine which counter to step
    let counter_name = if (thm_def.shared_counter != null) thm_def.shared_counter
                       else thm_def.env_name
    // step the counter
    let stepped = step_and_get(state, counter_name)
    let env_num = stepped.num
    let new_counters = stepped.counters
    let new_custom_counters = stepped.custom_counters
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(thm_def.env_name, env_num, env_text)
    let new_theorems = state.theorems ++ [entry]
    let new_state = {state, counters: new_counters, custom_counters: new_custom_counters,
        theorems: new_theorems,
        env_context: thm_def.env_name, env_context_num: string(env_num)}
    walk_children(el, 0, len(el), new_state)
}

// step the appropriate counter and return {num, counters, custom_counters}
fn step_and_get(state, counter_name) {
    if (is_fixed_counter(counter_name)) step_and_get_fixed(state, counter_name)
    else step_and_get_custom(state, counter_name)
}

fn step_and_get_fixed(state, counter_name) {
    let new_counters = step_counter(state.counters, counter_name)
    let num = get_fixed_counter(new_counters, counter_name)
    {num: num, counters: new_counters, custom_counters: state.custom_counters}
}

fn step_and_get_custom(state, counter_name) {
    let new_cc = step_custom_counter(state.custom_counters, counter_name)
    let num = get_custom_counter_val(new_cc, counter_name)
    {num: num, counters: state.counters, custom_counters: new_cc}
}

fn is_fixed_counter(counter_name) {
    counter_name == "theorem" or counter_name == "lemma" or counter_name == "corollary" or
    counter_name == "proposition" or counter_name == "definition" or counter_name == "example" or
    counter_name == "remark" or counter_name == "section" or counter_name == "subsection" or
    counter_name == "subsubsection" or counter_name == "figure" or counter_name == "table" or
    counter_name == "equation" or counter_name == "footnote" or counter_name == "chapter" or
    counter_name == "part" or counter_name == "paragraph"
}

fn get_fixed_counter(counters, counter_name) {
    match counter_name {
        case "theorem": counters.theorem
        case "lemma": counters.lemma
        case "corollary": counters.corollary
        case "proposition": counters.proposition
        case "definition": counters.definition
        case "example": counters.example
        case "remark": counters.remark
        case "section": counters.section
        case "figure": counters.figure
        case "table": counters.table
        case "equation": counters.equation
        default: 0
    }
}

// dynamic counter stepping using custom_counters pair array
fn step_custom_counter(custom_counters, counter_name) {
    let current = util.lookup(custom_counters, counter_name)
    let val = if (current != null) { current } else { 0 }
    add_entry(custom_counters, counter_name, val + 1)
}

fn get_custom_counter_val(custom_counters, counter_name) {
    let v = util.lookup(custom_counters, counter_name)
    if (v != null) { v } else { 0 }
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
    let c = state.counters
    // use env_context to determine what we're labeling
    let label_type = state.env_context
    let label_number = state.env_context_num
    let label_id = util.slugify(label_name)
    // also store the heading title for \nameref
    let heading_key = label_type ++ ":" ++ label_number
    let label_title = util.lookup(state.heading_titles, heading_key)

    let entry = {type: label_type, number: label_number, id: label_id, title: label_title}
    let new_labels = add_entry(state.labels, label_name, entry)
    {state, labels: new_labels}
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
    let new_fn_map = add_entry(state.footnote_map, fn_key, entry)
    let new_footnotes = state.footnotes ++ [entry]
    {state, counters: new_counters, footnote_map: new_fn_map, footnotes: new_footnotes}
}

// ============================================================
// Counter operations
// ============================================================

fn step_counter(counters, counter_name) {
    match counter_name {        case "part":
            ({counters, part: counters.part + 1})        case "chapter":
            {counters, chapter: counters.chapter + 1, section: 0, subsection: 0, subsubsection: 0}
        case "section":
            {counters, section: counters.section + 1, subsection: 0, subsubsection: 0}
        case "subsection":
            {counters, subsection: counters.subsection + 1, subsubsection: 0}
        case "subsubsection":
            {counters, subsubsection: counters.subsubsection + 1}
        case "figure":
            {counters, figure: counters.figure + 1}
        case "table":
            {counters, table: counters.table + 1}
        case "equation":
            {counters, equation: counters.equation + 1}
        case "theorem":
            {counters, theorem: counters.theorem + 1}
        case "lemma":
            {counters, lemma: counters.lemma + 1}
        case "corollary":
            {counters, corollary: counters.corollary + 1}
        case "proposition":
            {counters, proposition: counters.proposition + 1}
        case "definition":
            {counters, definition: counters.definition + 1}
        case "example":
            {counters, example: counters.example + 1}
        case "remark":
            {counters, remark: counters.remark + 1}
        case "footnote":
            {counters, footnote: counters.footnote + 1}
        default: counters
    }
}

// ============================================================
// Slug deduplication
// ============================================================

fn make_unique_slug(base, counts) {
    let current = util.lookup(counts, base)
    if (current == null) {
        {slug: base, counts: add_entry(counts, base, 1)}
    } else {
        let next = current + 1
        {slug: base ++ "-" ++ string(next), counts: add_entry(counts, base, next)}
    }
}
