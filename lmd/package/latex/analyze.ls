// latex/analyze.ls — Pass 1: Walk AST to collect document info
// Extracts preamble, computes section/figure/footnote numbers,
// collects headings (TOC), labels, footnotes.
// Returns immutable DocInfo used by render pass.

import util: .util
import dc_article: .docclass.article
import dc_book: .docclass.book
import dc_report: .docclass.report
import color: .elements.color
import enumitem: .packages.enumitem
import amsmath: .packages.amsmath
import registry: .packages.registry
import bib_style: .packages.bib_style

// helper: append a {key, val} entry to an entry list
fn add_entry(entries, k, v) {
    entries ++ [{key: k, val: v}]
}

// ============================================================
// Public API
// ============================================================

pub fn analyze(ast) => analyze_with_packages(ast, [])

pub fn analyze_with_packages(ast, packages) =>
    analyze_with_language(ast, packages, "english")

pub fn analyze_with_language(ast, packages, language) {
    let st = {
        packages: packages,
        language: language,
        bibliography_count: 0,
        docclass: "article",
        title: null,
        title_el: null,
        author: null,
        author_el: null,
        date: null,
        date_el: null,
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
        equation_nums: [],
        amsmath_numbers: [],
        enumerate_starts: [],
        last_enumerate_by_depth: [],
        list_depth: 0,
        slug_counts: [],
        in_appendix: false,
        secnumdepth: 3,
        custom_colors: [],
        analysis_colors: [],
        page_color: null,
        theorem_defs: [],
        in_body: false,
        env_context: "section",
        env_context_num: ""
    }
    let result = walk_node(ast, st)
    // return clean DocInfo (drop internal counters)
    // keep entry arrays — render pass will use lookup functions
    {
        docclass: result.docclass,
        title: result.title,
        title_el: result.title_el,
        author: result.author,
        author_el: result.author_el,
        date: result.date,
        date_el: result.date_el,
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
        equation_nums: result.equation_nums,
        amsmath_numbers: result.amsmath_numbers,
        enumerate_starts: result.enumerate_starts,
        secnumdepth: result.secnumdepth,
        custom_colors: result.custom_colors,
        page_color: result.page_color,
        theorem_defs: result.theorem_defs
    }
}

// ============================================================
// Tree walk
// ============================================================

fn walk_node(node, st) {
    if (node == null) st
    else if (node is element) walk_element(node, st)
    else st
}

fn walk_element(el, st) {
    let tag = name(el)
    match tag {
        // ---- preamble ----
        case 'documentclass': walk_documentclass(el, st)
        case 'title': walk_title(el, st)
        case 'author': walk_author(el, st)
        case 'date': walk_date(el, st)
        case 'document': walk_children(el, 0, len(el), {*:st, in_body: true})

        // ---- sections ----
        case 'part': walk_heading(el, st, "part", 0)
        case 'chapter': walk_heading(el, st, "chapter", 1)
        case 'section': walk_heading(el, st, "section", 2)
        case 'subsection': walk_heading(el, st, "subsection", 3)
        case 'subsubsection': walk_heading(el, st, "subsubsection", 4)
        case 'paragraph_command': walk_heading(el, st, "paragraph", 5)
        case 'subparagraph': walk_heading(el, st, "paragraph", 6)

        // ---- figures ----
        case 'figure': walk_figure(el, st)

        // ---- tables ----
        case 'table': walk_table(el, st)

        // ---- labels (inside anything) ----
        case 'label': walk_label(el, st)

        // ---- footnotes ----
        case 'footnote': walk_footnote(el, st)
        case 'footcite': if (registry.active(st.packages, "biblatex"))
            walk_footnote(el, st) else walk_children(el, 0, len(el), st)

        case 'printbibliography': if (registry.active(st.packages, "biblatex"))
            walk_printbibliography(el, st) else walk_children(el, 0, len(el), st)

        // ---- equations ----
        case 'equation': walk_equation(el, st)

        // ---- list counters ----
        case 'enumerate': walk_enumerate(el, st)
        case 'itemize': walk_nonnumbered_list(el, st)
        case 'description': walk_nonnumbered_list(el, st)

        // ---- theorem-like environments ----
        // check custom \newtheorem defs first (for shared counters, starred variants)
        case 'theorem': walk_theorem_like(el, st, "theorem")
        case 'lemma': walk_theorem_like(el, st, "lemma")
        case 'corollary': walk_theorem_like(el, st, "corollary")
        case 'proposition': walk_theorem_like(el, st, "proposition")
        case 'definition': walk_theorem_like(el, st, "definition")
        case 'example': walk_theorem_like(el, st, "example")
        case 'remark': walk_theorem_like(el, st, "remark")

        // ---- bibliography ----
        case 'thebibliography': walk_bibliography(el, st)
        case 'bibitem': walk_bibitem(el, st)

        // ---- appendix & counters ----
        case 'appendix': walk_appendix(el, st)
        case 'setcounter': walk_setcounter(el, st)

        // ---- color definitions ----
        case 'definecolor': walk_definecolor(el, st)
        case 'pagecolor': walk_pagecolor(el, st)

        // ---- \newtheorem definitions ----
        case 'newtheorem': walk_newtheorem(el, st)
        case 'newtheorem*': walk_newtheorem_star(el, st)

        // ---- everything else: check custom theorem defs, then walk children ----
        default: walk_default(el, tag, st)
    }
}

fn walk_children(el, i, n, st) {
    if i >= n { st }
    else {
        let child = el[i]
        let new_state = walk_node(child, st)
        walk_children(el, i + 1, n, new_state)
    }
}

// ============================================================
// Preamble extraction
// ============================================================

fn walk_documentclass(el, st) {
    let cls = trim(util.text_of_skip_brack(el))
    if cls != "" { {*:st, docclass: cls} }
    else { st }
}

fn walk_title(el, st) {
    let t = util.rich_text_of(el)
    {*:st, title: t, title_el: el}
}

fn walk_author(el, st) {
    let a = util.rich_text_of(el)
    {*:st, author: a, author_el: el}
}

fn walk_date(el, st) {
    let d = util.rich_text_of(el)
    {*:st, date: d, date_el: el}
}

// ============================================================
// Section headings
// ============================================================

fn walk_heading(el, st, counter_name, html_level) {
    let starred = el.starred == true
    // starred headings do not advance the section counter or enter the TOC.
    let new_counters = if (starred) st.counters else step_counter(st.counters, counter_name)
    // determine numbering depth for this counter
    let counter_depth = counter_name_to_depth(counter_name)
    let sec_num = if (not starred and counter_depth <= st.secnumdepth)
        compute_section_num(new_counters, counter_name, st.docclass, st.in_appendix)
    else null

    // extract title text
    let title_el = el.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let base_slug = util.slugify(title_text)

    // deduplicate slug
    let slug_info = make_unique_slug(base_slug, st.slug_counts)
    let slug = slug_info.slug
    let new_slug_counts = slug_info.counts

    // record heading
    let entry = {level: html_level, number: sec_num, text: title_text, id: slug}
    let new_heading_nums = add_entry(st.heading_nums, slug, sec_num)
    let new_headings = if (starred) st.headings else st.headings ++ [entry]
    let sec_num_str = sec_num_to_str(sec_num)

    let new_state = {
        *:st,
        counters: new_counters,
        headings: new_headings,
        heading_nums: new_heading_nums,
        heading_titles: add_entry(st.heading_titles, counter_name ++ ":" ++ sec_num_str, title_text),
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

fn walk_figure(el, st) {
    let new_counters = step_counter(st.counters, "figure")
    let fig_num = new_counters.figure
    let fig_text = trim(util.text_of(el))
    let entry = {number: fig_num, content: fig_text}
    let new_figures = st.figures ++ [entry]
    let new_state = {*:st, counters: new_counters, figures: new_figures,
        env_context: "figure", env_context_num: string(fig_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_table(el, st) {
    let new_counters = step_counter(st.counters, "table")
    let tab_num = new_counters.table
    let tab_text = trim(util.text_of(el))
    let entry = {number: tab_num, content: tab_text}
    let new_tables = st.tables ++ [entry]
    let new_state = {*:st, counters: new_counters, tables: new_tables,
        env_context: "table", env_context_num: string(tab_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_equation(el, st) {
    if (registry.active(st.packages, "amsmath")) walk_amsmath(el, st)
    else walk_legacy_equation(el, st)
}

fn walk_legacy_equation(el, st) {
    let new_counters = step_counter(st.counters, "equation")
    let eq_num = new_counters.equation
    let numbers = if (el.source_offset != null)
        add_entry(st.equation_nums, string(el.source_offset), eq_num) else st.equation_nums
    let new_state = {*:st, counters: new_counters,
        equation_nums: numbers,
        env_context: "equation", env_context_num: string(eq_num)}
    walk_children(el, 0, len(el), new_state)
}

fn walk_amsmath_rows(rows, i, st, numbered, entries) {
    if (i >= len(rows)) {state: st, entries: entries}
    else {
        let row = rows[i]
        let automatic = numbered and not row.suppress
        let counters = if (automatic) step_counter(st.counters, "equation") else st.counters
        let display = if (row.tag != null) row.tag
            else if (automatic) string(counters.equation) else null
        let labels = if (row.label != null)
            add_entry(st.labels, row.label,
                {type: "equation", number: if (display != null) display else "",
                 id: util.slugify(row.label), title: null})
            else st.labels
        let next = {*:st, counters: counters, labels: labels}
        walk_amsmath_rows(rows, i + 1, next, numbered,
            entries ++ [{display: display, bare_tag: row.bare_tag}])
    }
}

fn walk_amsmath(el, st) {
    let tag = string(name(el))
    let numbered = not ends_with(tag, "*") and tag != "aligned" and tag != "split"
    let rows = amsmath.rows_for(el)
    let folded = walk_amsmath_rows(rows, 0, st, numbered, [])
    let stored = if (el.source_offset != null)
        add_entry(folded.state.amsmath_numbers, string(el.source_offset), folded.entries)
        else folded.state.amsmath_numbers
    {*:folded.state, amsmath_numbers: stored}
}

fn count_list_items(node) {
    if (not (node is element)) 0
    else {
        let tag = string(name(node))
        if (tag == "item") 1
        else if (tag == "enumerate" or tag == "itemize" or tag == "description") 0
        else count_list_items_children(node, 0)
    }
}

fn count_list_items_children(node, i) {
    if (i >= len(node)) 0
    else count_list_items(node[i]) + count_list_items_children(node, i + 1)
}

fn walk_enumerate(el, st) {
    let opts = enumitem.options(el)
    let explicit = enumitem.start(opts)
    let depth = st.list_depth + 1
    let prior = util.lookup(st.last_enumerate_by_depth, string(depth))
    let first = if (explicit != null) explicit
        else if (util.option_enabled(opts.resume) and prior != null) prior + 1 else 1
    let starts = if (el.source_offset != null)
        add_entry(st.enumerate_starts, string(el.source_offset), first)
        else st.enumerate_starts
    let walked = walk_children(el, 0, len(el), {*:st, enumerate_starts: starts,
        list_depth: depth})
    // restore the parent depth after nested lists and record only direct items.
    {*:walked, list_depth: st.list_depth,
        last_enumerate_by_depth: add_entry(walked.last_enumerate_by_depth,
            string(depth), first + count_list_items_children(el, 0) - 1)}
}

fn walk_nonnumbered_list(el, st) {
    let walked = walk_children(el, 0, len(el), {*:st, list_depth: st.list_depth + 1})
    {*:walked, list_depth: st.list_depth}
}

fn walk_numbered_env(el, st, env_type) {
    let new_counters = step_counter(st.counters, env_type)
    let env_num = get_env_counter(new_counters, env_type)
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(env_type, env_num, env_text)
    let new_theorems = st.theorems ++ [entry]
    let new_state = {*:st, counters: new_counters, theorems: new_theorems,
        env_context: env_type, env_context_num: string(env_num)}
    walk_children(el, 0, len(el), new_state)
}

// route theorem-like env through custom defs if available, else default numbered
fn walk_theorem_like(el, st, env_type) {
    let thm_def = util.lookup(st.theorem_defs, env_type)
    if (thm_def != null) { walk_custom_theorem(el, st, thm_def) }
    else { walk_numbered_env(el, st, env_type) }
}

fn make_thm_entry(k, n, c) {
    {kind: k, number: n, content: c}
}

// ============================================================
// Bibliography
// ============================================================

fn walk_printbibliography(el, st) {
    let index = st.bibliography_count + 1
    let opts = util.parse_kv_options(util.optional_raw(el))
    let heading = if (opts.heading == null) "bibliography" else opts.heading
    let title = if (opts.title != null) opts.title else bib_style.locale(st.language).references
    // bibintoc contributes a heading entry using the renderer's print target.
    let headings = if (heading == "bibintoc" and
        len(registry.bib_print_issues(el)) == 0) st.headings ++
        [{level: 2, number: null, text: title, id: "bibliography-" ++ string(index)}]
        else st.headings
    {*:st, headings: headings, bibliography_count: index}
}

fn walk_bibliography(el, st) {
    walk_children(el, 0, len(el), st)
}

fn walk_bibitem(el, st) {
    let bib_key = trim(util.text_of(el))
    let bib_num = len(st.bibitems) + 1
    let entry = make_bib_entry(bib_key, bib_num)
    let new_bibitems = st.bibitems ++ [entry]
    {*:st, bibitems: new_bibitems}
}

fn make_bib_entry(k, n) {
    {key: k, number: n}
}

// ============================================================
// Appendix & counter commands
// ============================================================

fn walk_appendix(el, st) {
    // reset section counter and switch to appendix mode
    let new_counters = {*:st.counters, section: 0, subsection: 0, subsubsection: 0}
    {*:st, in_appendix: true, counters: new_counters}
}

fn walk_setcounter(el, st) {
    // children: [counter_name_text, value_text]
    let args = util.command_args(el)
    let n = len(args)
    if (n >= 2) {
        let cname = trim(util.text_of(args[0]))
        let cval = trim(util.text_of(args[1]))
        let val = int(cval)
        if (cname == "secnumdepth" and val != null) {
            {*:st, secnumdepth: val}
        }
        else { st }
    }
    else { st }
}

fn walk_definecolor(el, st) {
    let definition = color.definition(el)
    if (definition.color_name == "") st
    else {
        let colors = add_entry(st.analysis_colors, definition.color_name,
            color.definition_value(definition))
        {*:st, analysis_colors: colors,
            custom_colors: if (st.in_body) st.custom_colors else colors}
    }
}

fn walk_pagecolor(el, st) {
    let css = color.page_color(el, st.analysis_colors)
    if (css == null) st else {*:st, page_color: css}
}

// ============================================================
// \newtheorem definitions
// ============================================================

// \newtheorem{name}{Label}       → independent counter
// \newtheorem{name}[counter]{Label} → shared counter with existing type
// \newtheorem*{name}{Label}      → unnumbered (star variant)
fn walk_newtheorem(el, st) {
    let args = util.command_args(el)
    let n = len(args)
    if (n < 2) st
    else build_newtheorem_def(args, st, n, true)
}

fn walk_newtheorem_star(el, st) {
    let args = util.command_args(el)
    let n = len(args)
    if (n < 2) st
    else build_newtheorem_def(args, st, n, false)
}

fn build_newtheorem_def(el, st, n, is_numbered) {
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
    let new_defs = add_entry(st.theorem_defs, clean_name, def)
    // add counter for this theorem type if not shared and not unnumbered
    let is_unnumbered = is_numbered == false
    let new_custom_counters = if (is_unnumbered or shared_counter != null) st.custom_counters
                      else add_entry(st.custom_counters, clean_name, 0)
    {*:st, theorem_defs: new_defs, custom_counters: new_custom_counters}
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

fn walk_default(el, tag, st) {
    let tag_str = string(tag)
    let thm_def = util.lookup(st.theorem_defs, tag_str)
    if (registry.active(st.packages, "amsmath") and amsmath.is_environment(tag_str))
        walk_amsmath(el, st)
    else if (thm_def != null) { walk_custom_theorem(el, st, thm_def) }
    else { walk_children(el, 0, len(el), st) }
}

fn walk_custom_theorem(el, st, thm_def) {
    if (thm_def.numbered == false) {
        // unnumbered: just walk children
        let new_state = {*:st, env_context: thm_def.env_name, env_context_num: ""}
        walk_children(el, 0, len(el), new_state)
    } else {
        walk_custom_theorem_numbered(el, st, thm_def)
    }
}

fn walk_custom_theorem_numbered(el, st, thm_def) {
    // determine which counter to step
    let counter_name = if (thm_def.shared_counter != null) thm_def.shared_counter
                       else thm_def.env_name
    // step the counter
    let stepped = step_and_get(st, counter_name)
    let env_num = stepped.num
    let new_counters = stepped.counters
    let new_custom_counters = stepped.custom_counters
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(thm_def.env_name, env_num, env_text)
    let new_theorems = st.theorems ++ [entry]
    let new_state = {*:st, counters: new_counters, custom_counters: new_custom_counters,
        theorems: new_theorems,
        env_context: thm_def.env_name, env_context_num: string(env_num)}
    walk_children(el, 0, len(el), new_state)
}

// step the appropriate counter and return {num, counters, custom_counters}
fn step_and_get(st, counter_name) {
    if (is_fixed_counter(counter_name)) step_and_get_fixed(st, counter_name)
    else step_and_get_custom(st, counter_name)
}

fn step_and_get_fixed(st, counter_name) {
    let new_counters = step_counter(st.counters, counter_name)
    let num = get_fixed_counter(new_counters, counter_name)
    {num: num, counters: new_counters, custom_counters: st.custom_counters}
}

fn step_and_get_custom(st, counter_name) {
    let new_cc = step_custom_counter(st.custom_counters, counter_name)
    let num = get_custom_counter_val(new_cc, counter_name)
    {num: num, counters: st.counters, custom_counters: new_cc}
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

fn walk_label(el, st) {
    let label_name = trim(util.text_of(el))
    let c = st.counters
    // use env_context to determine what we're labeling
    let label_type = st.env_context
    let label_number = st.env_context_num
    let label_id = util.slugify(label_name)
    // also store the heading title for \nameref
    let heading_key = label_type ++ ":" ++ label_number
    let label_title = util.lookup(st.heading_titles, heading_key)

    let entry = {type: label_type, number: label_number, id: label_id, title: label_title}
    let new_labels = add_entry(st.labels, label_name, entry)
    {*:st, labels: new_labels}
}

// ============================================================
// Footnotes
// ============================================================

fn walk_footnote(el, st) {
    let new_counters = step_counter(st.counters, "footnote")
    let fn_num = new_counters.footnote
    // key by content text for lookup in pass 2
    let content_text = trim(util.text_of(el))
    let fn_key = util.slugify(content_text)
    let entry = {number: fn_num, node: el}
    let new_fn_map = add_entry(st.footnote_map, fn_key, entry)
    let new_footnotes = st.footnotes ++ [entry]
    {*:st, counters: new_counters, footnote_map: new_fn_map, footnotes: new_footnotes}
}

// ============================================================
// Counter operations
// ============================================================

fn step_counter(counters, counter_name) {
    match counter_name {        case "part":
            ({*:counters, part: counters.part + 1})        case "chapter":
            {*:counters, chapter: counters.chapter + 1, section: 0, subsection: 0, subsubsection: 0}
        case "section":
            {*:counters, section: counters.section + 1, subsection: 0, subsubsection: 0}
        case "subsection":
            {*:counters, subsection: counters.subsection + 1, subsubsection: 0}
        case "subsubsection":
            {*:counters, subsubsection: counters.subsubsection + 1}
        case "figure":
            {*:counters, figure: counters.figure + 1}
        case "table":
            {*:counters, table: counters.table + 1}
        case "equation":
            {*:counters, equation: counters.equation + 1}
        case "theorem":
            {*:counters, theorem: counters.theorem + 1}
        case "lemma":
            {*:counters, lemma: counters.lemma + 1}
        case "corollary":
            {*:counters, corollary: counters.corollary + 1}
        case "proposition":
            {*:counters, proposition: counters.proposition + 1}
        case "definition":
            {*:counters, definition: counters.definition + 1}
        case "example":
            {*:counters, example: counters.example + 1}
        case "remark":
            {*:counters, remark: counters.remark + 1}
        case "footnote":
            {*:counters, footnote: counters.footnote + 1}
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
        {slug: base ++ "-" ++ (next), counts: add_entry(counts, base, next)}
    }
}
