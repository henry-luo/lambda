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
import listings: .packages.listings
import floats: .floats
import algorithmic: .packages.algorithmic
import algorithm2e: .packages.algorithm2e
import decorations: .packages.decorations
import paragraph: .packages.paragraph
import frames: .packages.frames
import footmisc: .packages.footmisc
import appendix: .packages.appendix
import colspec: .packages.array
import titles: .packages.titles
import contents: .packages.contents
import frontmatter: .packages.frontmatter

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

pub fn analyze_with_language(ast, packages, language, options = {}) {
    let st = {
        packages: packages,
        citeproc: options.citeproc == true,
        citeproc_note: options.note_style == true,
        language: language,
        bibliography_count: 0,
        listing_count: 0,
        listing_nums: [],
        subfloat_nums: [], subfigure_count: 0, subtable_count: 0,
        captionof_nums: [],
        docclass: "article",
        class_options: [],
        line_spread: null,
        title: null,
        title_el: null,
        author: null,
        author_el: null,
        date: null,
        date_el: null,
        // authblk's \author and \affil commands, in source order
        authblk: [],
        // every counter, built-in or declared, lives in one map keyed by name
        counters: {
            part: 0, chapter: 0, section: 0, subsection: 0, subsubsection: 0,
            paragraph: 0, figure: 0, table: 0, equation: 0, footnote: 0,
            theorem: 0, lemma: 0, corollary: 0, proposition: 0,
            definition: 0, example: 0, remark: 0
        },
        // \newcounter{child}[parent] reset relations, as {key: parent, val: child}
        counter_resets: [],
        headings: [],
        heading_nums: [],
        heading_titles: [],
        // appendix's title and titletoc names by heading slug
        heading_names: [],
        // the slugs of headings in an appendix (titlesec's \chaptertitlename)
        appendix_headings: [],
        labels: [],
        footnote_marks: [],
        footnotes: [],
        floats: [],
        float_kinds: floats.kinds_for(packages),
        float_style: "plain",
        alg_labels: [],
        alg2e_settings: [],
        theorems: [],
        bibitems: [],
        equation_nums: [],
        amsmath_numbers: [],
        // mathtools tag forms by name, and the one \usetagform made current
        tag_forms: {},
        tag_form: null,
        // located diagnostics found while walking declarations
        diagnostics: [],
        // package settings in source order ({key, value, offset}; util.setting_at)
        settings: [],
        enumerate_starts: [],
        last_enumerate_by_depth: [],
        list_depth: 0,
        slug_counts: [],
        contents_anchors: [],
        in_appendix: false,
        // inside appendix's subappendices; the names its title options give headings;
        // the letters an earlier appendices environment reached (\restoreapp)
        subappendix: false,
        appendix_names: null,
        appendix_count: 0,
        secnumdepth: 3,
        custom_colors: [],
        analysis_colors: [],
        page_color: null,
        theorem_defs: [],
        theorem_style: "plain",
        // a package float numbered within a heading (algorithm's section option)
        counter_within: [for (k, v at floats.kinds_for(packages) where v.within != null)
            {key: string(k), val: v.within}],
        counter_positions: [],
        in_body: false,
        env_context: "section",
        env_context_num: ""
    }
    let result = walk_node(ast, st)
    // return clean DocInfo (drop internal counters)
    // keep entry arrays — render pass will use lookup functions
    {
        docclass: result.docclass,
        class_options: result.class_options,
        // a preamble declaration or \linespread wins over the setspace option
        line_spread: result.line_spread or paragraph.option_stretch(
            registry.options_for(packages, "setspace"), result.class_options),
        title: result.title,
        title_el: result.title_el,
        author: result.author,
        author_el: result.author_el,
        date: result.date,
        date_el: result.date_el,
        authblk: result.authblk,
        headings: result.headings,
        heading_nums: result.heading_nums,
        heading_titles: result.heading_titles,
        heading_names: result.heading_names,
        appendix_headings: result.appendix_headings,
        labels: result.labels,
        footnotes: result.footnotes,
        footnote_marks: result.footnote_marks,
        floats: result.floats,
        float_kinds: result.float_kinds,
        alg_labels: result.alg_labels,
        alg2e_settings: result.alg2e_settings,
        theorems: result.theorems,
        bibitems: result.bibitems,
        listing_nums: result.listing_nums,
        subfloat_nums: result.subfloat_nums,
        captionof_nums: result.captionof_nums,
        equation_nums: result.equation_nums,
        amsmath_numbers: result.amsmath_numbers,
        enumerate_starts: result.enumerate_starts,
        secnumdepth: result.secnumdepth,
        custom_colors: result.custom_colors,
        page_color: result.page_color,
        theorem_defs: result.theorem_defs,
        contents_anchors: result.contents_anchors,
        diagnostics: result.diagnostics,
        settings: result.settings
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

// Analysis handlers for commands a package owns (registry CATALOG `commands`),
// consulted only while that package is active, before the core arms.
pub let PACKAGE_WALK = {
    subcaption: {subfigure: (el, st) => walk_subfloat(el, st, "figure"),
        subtable: (el, st) => walk_subfloat(el, st, "table")},
    caption: {captionof: walk_captionof},
    listings: {lstlisting: walk_listing},
    float: {newfloat: walk_newfloat, floatname: walk_floatname, floatstyle: walk_floatstyle,
        restylefloat: walk_restylefloat},
    algpseudocode: map(["algorithmic", walk_algorithmic] ++ alg_label_walkers()),
    algorithmic: {algorithmic: walk_algorithmic},
    subfig: {subfloat: walk_subfig},
    subfigure: {subfigure: walk_subfig},
    wrapfig: {wrapfigure: (el, st) => walk_float(el, st, "figure"),
        wraptable: (el, st) => walk_float(el, st, "table")},
    rotating: {sidewaysfigure: (el, st) => walk_float(el, st, "figure"),
        sidewaystable: (el, st) => walk_float(el, st, "table")},
    // settings commands shape every algorithm (render alg2e_settings)
    algorithm2e: map([for (setting in algorithm2e.SETTINGS,
        x in [setting, (el, st) => ({*:st, alg2e_settings: st.alg2e_settings ++ [string(name(el))]})]) x]),
    mathtools: {newtagform: walk_newtagform, renewtagform: walk_newtagform, usetagform: walk_usetagform},
    // soul's \set…color{color} and ulem's \normalem/\ULforem hold until changed
    soul: map([for (key in decorations.SOUL_SETTINGS, x in [key, (el, st) => record_setting(st, el, key,
        trim(util.text_of(el)))]) x]),
    setspace: map([for (tag in paragraph.SETSPACE_DECLARATIONS, x in [tag, walk_setspace]) x]),
    // \newmdenv and \mdfdefinestyle hold until redefined; keys outside the profile are reported
    mdframed: {newmdenv: (el, st) => mdframed_definition(el, st, "mdf-env:", util.optional_raw(el), 0),
        mdfdefinestyle: (el, st) => mdframed_definition(el, st, "mdf-style:",
            util.raw_argument(el, "required", 1), 0),
        mdframed: (el, st) => walk_default(el, "mdframed",
            mdframed_key_issues(el, st, util.leading_option(el).option))},
    ulem: {normalem: (el, st) => record_setting(st, el, "ulem-emph", "normalem"),
        ULforem: (el, st) => record_setting(st, el, "ulem-emph", "ULforem")},
    // colortbl's rule colors and array's column types hold from where they are given
    colortbl: {arrayrulecolor: (el, st) => walk_rule_color(el, st, "arrayrulecolor"),
        doublerulesepcolor: (el, st) => walk_rule_color(el, st, "doublerulesepcolor")},
    array: {newcolumntype: walk_newcolumntype},
    // titlesec's formats and spacing and sectsty's fonts hold from where they are given
    titlesec: {titleformat: walk_titleformat, titlespacing: (el, st) => record_pair(st, el, titles.titlespacing(el)),
        titlelabel: (el, st) => record_setting(st, el, "titlelabel", util.raw_argument(el, "required", 0) or "")},
    sectsty: map([for (command in titles.SECTSTY_COMMANDS, x in [command, walk_sectsty]) x]),
    tocloft: {cftsetindents: (el, st) => record_pair(st, el, contents.indents(el)), newlistof: walk_newlistof},
    minitoc: {mtcsettitle: walk_mtcsettitle},
    // titling's \pre… and \post… code; authblk's authors and affiliations
    titling: map([for (part in frontmatter.TITLING_PARTS, side in ["pre", "post"],
        x in [side ++ part, walk_titling]) x]),
    authblk: {author: (el, st) => {*:walk_author(el, st), authblk: st.authblk ++ [{kind: "author", el: el}]},
        affil: (el, st) => {*:st, authblk: st.authblk ++ [{kind: "affil", el: el}]}},
    pdfpages: {includepdfset: (el, st) => record_setting(st, el, "includepdfset", util.raw_argument(el, "required", 0) or "")},
    appendix: {appendices: (el, st) => walk_appendices(el, st, false),
        subappendices: (el, st) => walk_appendices(el, st, true), addappheadtotoc: add_appendix_head}
}

// a {key, value} setting, or nothing when the command named no heading
fn record_pair(st, el, pair) => if (pair == null) st else record_setting(st, el, pair.key, pair.value)

// \titleformat: what the format cannot express is reported where it is given
fn walk_titleformat(el, st) {
    let level = titles.level_of(util.raw_argument(el, "required", 0))
    let pair = titles.titleformat(el, util.setting_at(st.settings, "titleformat:" ++ level, el.source_offset))
    if (pair == null) st
    else {*:record_pair(st, el, pair), diagnostics: st.diagnostics ++
        [for (issue in titles.format_issues(pair.value, st.custom_colors))
            util.diagnostic(issue.code, "titlesec", issue.item,
                if (issue.code == "title-shape-approximation") "The " ++ issue.item ++ " title shape renders as hang"
                else "Unsupported command in a title format: \\" ++ issue.item, el.source_offset)]}
}

// a sectsty font command sets the font of the levels it names
fn walk_sectsty(el, st) {
    let raw = util.raw_argument(el, "required", 0) or ""
    let unknown = font_decl_unknown(raw, st.custom_colors)
    let levels = titles.SECTSTY_FONTS[string(name(el))]
    let recorded = {*:st, settings: st.settings ++ [for (level in levels)
        {key: "sectsty:" ++ level, value: raw, offset: el.source_offset}]};
    {*:recorded, diagnostics: st.diagnostics ++ [for (command in unknown)
        util.diagnostic("unsupported-title-format", "sectsty", command,
            "Unsupported command in a heading font: \\" ++ command, el.source_offset)]}
}

fn font_decl_unknown(raw, colors) => [for (issue in titles.format_issues({format: raw}, colors)) issue.item]

fn walk_rule_color(el, st, key) {
    let css = color.command_color(el, 0, st.custom_colors)
    if (css != null) record_setting(st, el, key, css)
    else {*:st, diagnostics: st.diagnostics ++ [util.diagnostic("invalid-color", "colortbl",
        string(name(el)), "Invalid color in \\" ++ string(name(el)), el.source_offset)]}
}

// \newcolumntype{d}[n]{body}: a column letter the preambles after it expand
fn walk_newcolumntype(el, st) {
    let defined = colspec.column_type(el)
    if (defined != null) record_setting(st, el, "column-type:" ++ defined.name, defined.value)
    else {*:st, diagnostics: st.diagnostics ++ [util.diagnostic("invalid-column-type", "array",
        null, "\\newcolumntype needs one letter and a definition", el.source_offset)]}
}

fn mdframed_definition(el, st, prefix, keys, name_index) {
    let defined = util.raw_argument(el, "required", name_index)
    if (defined == null) st
    else record_setting(mdframed_key_issues(el, st, keys), el, prefix ++ trim(defined), keys or "")
}

fn mdframed_key_issues(el, st, keys) {
    let unknown = frames.unknown_keys(util.parse_kv_options(keys));
    {*:st, diagnostics: st.diagnostics ++ [for (key in unknown)
        util.diagnostic("unsupported-mdframed-key", "mdframed", key,
            "Unsupported mdframed key " ++ key, el.source_offset)]}
}

fn record_setting(st, el, key, value) =>
    {*:st, settings: st.settings ++ [{key: key, value: value, offset: el.source_offset}]}

// \newtagform{name}[inner]{left}{right}: how \usetagform{name} frames later tags; a
// number format that is not a mapped text command prints the number plain
fn walk_newtagform(el, st) {
    // the brackets are raw text: "[" and "]" are not argument content
    let args = [for (i in 0 to 2, let raw = util.raw_argument(el, "required", i) where raw != null) trim(raw)]
    let inner = util.optional_raw(el)
    let issues = if (inner == null or amsmath.tag_format(inner) != null) [] else
        [util.diagnostic("unsupported-tag-format", "mathtools", trim(inner),
            "Tag number format " ++ trim(inner) ++ " is not supported", el.source_offset)]
    if (len(args) < 3) st
    else {*:st, diagnostics: st.diagnostics ++ issues,
        tag_forms: {*:st.tag_forms, [args[0]]: {left: args[1], right: args[2], inner: inner}}}
}

fn walk_usetagform(el, st) {
    let form = trim(util.text_of(el))
    {*:st, tag_form: if (form == "default") null else st.tag_forms[form]}
}

// subfig's \subfloat and subfigure's \subfigure: a lettered subfloat of the enclosing float
fn walk_subfig(el, st) => walk_subfloat(el, st, if (st.env_context == "table") "table" else "figure")

// ============================================================
// Pseudocode: numbered lines carry the labels placed on them
// ============================================================

fn walk_algorithmic(el, st) {
    let lines = algorithmic.lines(el)
    {*:st, labels: st.labels ++ [for (line in lines where line.label != null)
        {key: line.label, val: {type: "line", number: string(line.number), id: util.slugify(line.label),
            title: null}}]}
}

// \algrenewcommand\algorithmicrequire{…} leaves the new label as the macro's
// argument: name/handler pairs for the label macros
fn alg_label_walkers() => [for (macro in algorithmic.LABEL_MACROS,
    x in [macro, (el, st) => if (len(content(el)) == 0) st
        else {*:st, alg_labels: add_entry(st.alg_labels, slice(macro, 11, len(macro)), el)}]) x]

// ============================================================
// The float package: \newfloat, \floatname, \floatstyle, \restylefloat
// ============================================================

fn arg_texts(el) => [for (arg in util.command_args(el)) trim(util.text_of(arg))]

// \newfloat{type}{placement}{ext}[within]: a float kind in the current float style
fn walk_newfloat(el, st) {
    let args = [for (text in arg_texts(el) where text != "") text]
    let within = util.optional_raw(el)
    if (len(args) == 0) st
    else {*:st,
        float_kinds: {*:st.float_kinds, [args[0]]: floats.new_kind(args[0], st.float_style,
            if (len(args) > 2) args[2] else null)},
        counters: {*:st.counters, [args[0]]: 0},
        counter_within: if (within == null) st.counter_within
            else add_entry(st.counter_within, args[0], trim(within))}
}

fn walk_floatname(el, st) {
    let args = arg_texts(el)
    if (len(args) < 2 or st.float_kinds[args[0]] == null) st
    else {*:st, float_kinds: {*:st.float_kinds, [args[0]]: {*:st.float_kinds[args[0]], name: args[1]}}}
}

fn walk_floatstyle(el, st) {
    let style = trim(util.text_of(el))
    if (contains(["plain", "plaintop", "ruled", "boxed"], style) or false) {*:st, float_style: style} else st
}

// \restylefloat{type} gives an existing kind the current float style
fn walk_restylefloat(el, st) {
    let kind = trim(util.text_of(el))
    if (st.float_kinds[kind] == null) st
    else {*:st, float_kinds: {*:st.float_kinds, [kind]: {*:st.float_kinds[kind], style: st.float_style}}}
}

fn walk_element(el, st) {
    let tag = name(el)
    let owned = registry.handler(PACKAGE_WALK, st.packages, string(tag))
    if (owned != null) owned(el, st)
    else match tag {
        // ---- preamble ----
        case 'documentclass': walk_documentclass(el, st)
        case 'linespread': walk_linespread(el, st)
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

        // ---- floats ----
        case 'figure': walk_float(el, st, "figure")
        case 'figure*': walk_float(el, st, "figure")
        case 'table': walk_float(el, st, "table")
        case 'table*': walk_float(el, st, "table")
        case 'longtable': walk_float(el, st, "table")
        case 'xltabular': walk_float(el, st, "table")
        // without caption, \captionof is unsupported and numbers nothing
        case 'captionof': st

        // ---- labels (inside anything) ----
        case 'label': walk_label(el, st)

        // ---- footnotes ----
        case 'footnote': walk_footnote(el, st)
        case 'footnotemark': walk_footnotemark(el, st)
        case 'footnotetext': walk_footnotetext(el, st)
        case 'footcite': if (st.citeproc or registry.active(st.packages, "biblatex"))
            walk_footnote(el, st) else walk_children(el, 0, len(el), st)

        case 'autocite': if (st.citeproc_note) walk_footnote(el, st)
            else walk_children(el, 0, len(el), st)
        case 'autocites': if (st.citeproc_note) walk_footnote(el, st)
            else walk_children(el, 0, len(el), st)

        case 'printbibliography': if (st.citeproc or registry.active(st.packages, "biblatex"))
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
        case 'addcontentsline': walk_addcontentsline(el, st)
        case 'setcounter': walk_setcounter(el, st)
        case 'setlength': walk_setlength(el, st)
        case 'addtocounter': walk_addtocounter(el, st)
        case 'stepcounter': walk_stepcounter(el, st, false)
        case 'refstepcounter': walk_stepcounter(el, st, true)
        case 'newcounter': walk_newcounter(el, st)
        case 'numberwithin': walk_numberwithin(el, st)

        // ---- color definitions ----
        case 'definecolor': walk_definecolor(el, st)
        case 'pagecolor': walk_pagecolor(el, st)

        // ---- \newtheorem definitions ----
        case 'newtheorem': walk_newtheorem(el, st)
        case 'newtheorem*': walk_newtheorem_star(el, st)
        case 'theoremstyle': walk_theoremstyle(el, st)

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
    let raw_options = util.optional_raw(el)
    let options = if (raw_options == null) []
        else [for (option in util.split_top_level(raw_options, ",") where trim(option) != "") trim(option)]
    if cls != "" { {*:st, docclass: cls, class_options: options} }
    else { st }
}

// heading levels as the ToC renders them (walk_heading's html levels)
let CONTENTS_LEVELS = {part: 1, chapter: 1, section: 2, subsection: 3, subsubsection: 4,
    paragraph: 5, subparagraph: 6}

// \addcontentsline{file}{level}{text}: an extra ToC heading or list entry,
// anchored where the command stands. A leading \numberline{n} is its number.
fn walk_addcontentsline(el, st) {
    let args = util.command_args(el)
    let file = if (len(args) > 0) trim(util.text_of(args[0])) else ""
    let level = if (len(args) > 1) trim(util.text_of(args[1])) else ""
    // \protect only guards the entry while TeX writes it to the file
    let entry_args = [for (arg in (if (len(args) > 2) slice(args, 2, len(args)) else [])
        where not (arg is element and string(name(arg)) == "protect")) arg]
    let numbered = len(entry_args) > 0 and entry_args[0] is element and
        string(name(entry_args[0])) == "numberline"
    let entry_number = if (numbered) trim(util.text_of(entry_args[0])) else null
    let text = trim(util.text_of(if (numbered) slice(entry_args, 1, len(entry_args)) else entry_args))
    let slug_info = make_unique_slug(util.slugify(text), st.slug_counts)
    let anchored = {*:st, slug_counts: slug_info.counts,
        contents_anchors: add_entry(st.contents_anchors, string(el.source_offset), slug_info.slug)}
    let list_kind = floats.kind_of_file(st.float_kinds, file)
    if (file == "toc" and CONTENTS_LEVELS[level] != null)
        {*:anchored, headings: st.headings ++
            [{level: CONTENTS_LEVELS[level], number: entry_number, text: text, id: slug_info.slug}]}
    else if (list_kind != null)
        {*:anchored, floats: st.floats ++ [{kind: list_kind, number: entry_number, content: text,
            offset: null, caption: text, id: slug_info.slug}]}
    else anchored
}

// a preamble setspace declaration sets the document's stretch, as \linespread does
fn walk_setspace(el, st) {
    let stretch = paragraph.declared_stretch(el, st.class_options)
    if (st.in_body or stretch == null) st else {*:st, line_spread: stretch}
}

// a preamble \linespread scales the document's line height
fn walk_linespread(el, st) {
    let factor = float(trim(util.text_of(el))) ^ { null }
    if (st.in_body or factor == null or factor <= 0.0) st else {*:st, line_spread: factor}
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
    let new_counters = if (starred) st.counters else step_counter(st, counter_name)
    // determine numbering depth for this counter
    let counter_depth = counter_name_to_depth(counter_name)
    let sec_num = if (not starred and counter_depth <= st.secnumdepth)
        compute_section_num(new_counters, counter_name, st)
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
    let entry = {level: html_level, number: sec_num, text: title_text, id: slug, offset: el.source_offset}
    let new_heading_nums = add_entry(st.heading_nums, slug, sec_num)
    let new_headings = if (starred) st.headings else st.headings ++ [entry]
    let sec_num_str = sec_num_to_str(sec_num)
    // appendices with title or titletoc name their top-level headings
    let named = st.appendix_names != null and not starred and
        counter_name == appendix.lettered_counter(appendix.chaptered(st.docclass), false)

    let new_state = {
        *:st,
        counters: new_counters,
        headings: new_headings,
        heading_nums: new_heading_nums,
        heading_titles: add_entry(st.heading_titles, counter_name ++ ":" ++ sec_num_str, title_text),
        heading_names: if (named) add_entry(st.heading_names, slug, st.appendix_names) else st.heading_names,
        appendix_headings: if (st.in_appendix) st.appendix_headings ++ [slug] else st.appendix_headings,
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

fn compute_section_num(raw_counters, counter_name, st) {
    let counters = subappendix_counters(raw_counters, st)
    if (st.docclass == "book") dc_book.format_section_number(counters, counter_name, st.in_appendix)
    else if (st.docclass == "report") dc_report.format_section_number(counters, counter_name, st.in_appendix)
    else dc_article.format_section_number(counters, counter_name, st.in_appendix)
}

// subappendices letter the level below the class's top one, which the class's
// format then joins like a number: 1.A, 1.A.1
fn subappendix_counters(counters, st) {
    let counter = appendix.lettered_counter(appendix.chaptered(st.docclass), true)
    if (not st.subappendix or counters[counter] <= 0) counters
    else {*:counters, [counter]: util.upper_alph(counters[counter])}
}

// ============================================================
// Figures, tables, equations
// ============================================================

fn walk_listing(el, st) {
    let ordinal = st.listing_count + 1
    let selected = listings.options(el)
    let label = selected.label
    let entry = if (label == null) null
        else {type: "listing", number: string(ordinal), id: util.slugify(label),
              title: selected.caption}
    // the listing's label comes from its options; a later \label belongs to the enclosing context
    {*:st, listing_count: ordinal,
     listing_nums: add_entry(st.listing_nums, string(el.source_offset), ordinal),
     labels: if (entry == null) st.labels else add_entry(st.labels, label, entry)}
}

// A float environment numbers its kind's counter. Its caption feeds the
// \listof… list, and its number scopes only its own labels.
fn walk_float(el, st, kind) {
    let desc = st.float_kinds[kind]
    let counters = step_counter(st, desc.counter)
    let scoped = scoped_counter({*:st, counters: counters}, desc.counter, counters[desc.counter])
    let label = floats.own_child(el, "label")
    let entry = {kind: kind, number: scoped.number, content: trim(util.text_of(el)),
        offset: el.source_offset, caption: floats.list_caption(floats.own_child(el, "caption")),
        id: if (label == null) null else util.slugify(trim(util.text_of(label)))}
    let walked = walk_children(el, 0, len(el), {*:st, counters: counters,
        floats: st.floats ++ [entry], counter_positions: scoped.positions,
        subfigure_count: if (kind == "figure") 0 else st.subfigure_count,
        subtable_count: if (kind == "table") 0 else st.subtable_count,
        env_context: kind, env_context_num: string(scoped.number)})
    restore_context(walked, st)
}

// An environment's \refstepcounter is local to it, so a \label after the
// environment refers to the enclosing context again.
fn restore_context(walked, outer) {
    {*:walked, env_context: outer.env_context, env_context_num: outer.env_context_num}
}

fn walk_subfloat(el, st, kind) {
    let ordinal = (if (kind == "figure") st.subfigure_count else st.subtable_count) + 1
    let letter = if (ordinal <= 26) chr(96 + ordinal) else string(ordinal)
    let ordinal_text = st.env_context_num ++ letter
    let next = {*:st,
        subfigure_count: if (kind == "figure") ordinal else st.subfigure_count,
        subtable_count: if (kind == "table") ordinal else st.subtable_count,
        subfloat_nums: add_entry(st.subfloat_nums, string(el.source_offset), letter),
        env_context: "sub" ++ kind, env_context_num: ordinal_text}
    let walked = walk_children(el, 0, len(el), next)
    // Subfloat labels use the combined number; following captions return to the parent.
    restore_context(walked, st)
}

fn walk_captionof(el, st) {
    let kind = trim(util.raw_argument(el, "required", 0))
    if (kind != "figure" and kind != "table") st
    else {
        let counters = step_counter(st, kind)
        let scoped = scoped_counter({*:st, counters: counters}, kind, counters[kind])
        {*:st, counters: counters, counter_positions: scoped.positions,
            captionof_nums: add_entry(st.captionof_nums, string(el.source_offset),
                string(scoped.number)), env_context: kind,
            env_context_num: string(scoped.number)}
    }
}

fn walk_equation(el, st) {
    if (registry.active(st.packages, "amsmath")) walk_amsmath(el, st)
    else walk_legacy_equation(el, st)
}

fn walk_legacy_equation(el, st) {
    let new_counters = step_counter(st, "equation")
    let scoped = scoped_counter({*:st, counters: new_counters},
        "equation", new_counters.equation)
    let eq_num = scoped.number
    let numbers = if (el.source_offset != null)
        add_entry(st.equation_nums, string(el.source_offset), eq_num) else st.equation_nums
    let new_state = {*:st, counters: new_counters,
        counter_positions: scoped.positions,
        equation_nums: numbers,
        env_context: "equation", env_context_num: string(eq_num)}
    restore_context(walk_children(el, 0, len(el), new_state), st)
}

fn walk_amsmath_rows(rows, i, st, numbered, entries) {
    if (i >= len(rows)) {state: st, entries: entries}
    else {
        let row = rows[i]
        let automatic = numbered and not row.suppress
        let counters = if (automatic) step_counter(st, "equation") else st.counters
        let scoped = if (automatic) scoped_counter({*:st, counters: counters},
            "equation", counters.equation)
            else {number: null, positions: st.counter_positions}
        let display = if (row.tag != null) row.tag
            else if (automatic) string(scoped.number) else null
        let labels = if (row.label != null)
            add_entry(st.labels, row.label,
                {type: "equation", number: if (display != null) display else "",
                 id: util.slugify(row.label), title: null, tag_form: st.tag_form})
            else st.labels
        let next = {*:st, counters: counters, labels: labels,
            counter_positions: scoped.positions}
        walk_amsmath_rows(rows, i + 1, next, numbered,
            entries ++ [{display: display, bare_tag: row.bare_tag, form: st.tag_form}])
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
    let new_counters = step_counter(st, env_type)
    let scoped = scoped_counter({*:st, counters: new_counters}, env_type, new_counters[env_type])
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(env_type, scoped.number, env_text)
    let new_theorems = st.theorems ++ [entry]
    let new_state = {*:st, counters: new_counters, theorems: new_theorems,
        counter_positions: scoped.positions,
        env_context: env_type, env_context_num: string(scoped.number)}
    restore_context(walk_children(el, 0, len(el), new_state), st)
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
    // \appendix restarts the top numbered level, lettered from here on: the
    // chapter in book and report, the section otherwise
    let chaptered = appendix.chaptered(st.docclass)
    let new_counters = {*:st.counters, chapter: if (chaptered) 0 else st.counters.chapter,
        section: 0, subsection: 0, subsubsection: 0}
    {*:st, in_appendix: true, counters: new_counters}
}

// appendix's environments (\@resets@pp): the lettered level restarts, appendices
// continuing an earlier environment's letters, and gets its value back at the end
// (\@pprestoresec). The toc and page options add the head and the page title.
fn walk_appendices(el, st, sub) {
    let options = registry.options_for(st.packages, "appendix")
    let chaptered = appendix.chaptered(st.docclass)
    let counter = appendix.lettered_counter(chaptered, sub)
    let start = if (sub) 0 else st.appendix_count
    let headed = if (not sub and options != null and util.option_enabled(options.toc))
        add_appendix_head(el, st) else st
    let reset = if (sub) {*:st.counters, [counter]: 0}
        else if (chaptered) {*:st.counters, chapter: start, section: 0}
        else {*:st.counters, section: start, subsection: 0}
    let walked = walk_children(el, 0, len(el), {*:headed, counters: reset,
        in_appendix: st.in_appendix or not sub, subappendix: sub,
        appendix_names: if (sub) null else appendix.names(options)})
    {*:walked, counters: {*:walked.counters, [counter]: st.counters[counter]},
        in_appendix: st.in_appendix, subappendix: st.subappendix, appendix_names: st.appendix_names,
        appendix_count: if (sub) st.appendix_count else walked.counters[counter]}
}

// \addappheadtotoc: an unnumbered "Appendices" contents entry at the class's top
// level, anchored where it is given
fn add_appendix_head(el, st) {
    let slug_info = make_unique_slug(util.slugify(appendix.HEAD), st.slug_counts)
    {*:st, slug_counts: slug_info.counts,
        contents_anchors: add_entry(st.contents_anchors, string(el.source_offset), slug_info.slug),
        headings: st.headings ++ [{level: if (appendix.chaptered(st.docclass)) 1 else 2,
            number: null, text: appendix.HEAD, id: slug_info.slug}]}
}

// the counter name and integer value of \setcounter or \addtocounter; null otherwise
fn counter_assignment(el) {
    let args = util.command_args(el)
    if (len(args) < 2) null
    else {
        let value = int(trim(util.text_of(args[1]))) ^ { null }
        if (value == null) null else {name: trim(util.text_of(args[0])), value: value}
    }
}

// a \setcounter is also a source-order setting, for packages that read a depth
// where they print (minitoc's minitocdepth)
fn walk_setcounter(el, st) {
    let assignment = counter_assignment(el)
    if (assignment == null) st
    else if (assignment.name == "secnumdepth") {*:st, secnumdepth: assignment.value}
    else {*:record_setting(st, el, "counter:" ++ assignment.name, assignment.value),
        counters: {*:st.counters, [assignment.name]: assignment.value}}
}

// \setlength as a source-order setting, for package lengths (tocloft's skips)
fn walk_setlength(el, st) {
    let css = if (el.length_name == null or el.length_value == null) null else util.css_dimension(el.length_value)
    if (css == null) st else record_setting(st, el, "length:" ++ el.length_name, css)
}

// tocloft's \newlistof[within]{entry}{ext}{title}: a list of entries that
// \addcontentsline{ext}{entry}{…} adds to and \listof<entry> prints
fn walk_newlistof(el, st) {
    let entry = trim(util.raw_argument(el, "required", 0) or "")
    let ext = trim(util.raw_argument(el, "required", 1) or "")
    let within = trim(util.optional_raw(el) or "")
    if (entry == "" or ext == "") st
    else {*:st, float_kinds: {*:st.float_kinds, [entry]: floats.listed_kind(entry, ext,
            util.raw_argument(el, "required", 2) or entry)},
        counters: {*:st.counters, [entry]: 0},
        counter_within: if (within == "") st.counter_within else add_entry(st.counter_within, entry, within)}
}

// titling's \pretitle{code} and the like: what the code cannot express is reported
fn walk_titling(el, st) {
    let raw = util.raw_argument(el, "required", 0) or ""
    let recorded = record_setting(st, el, "titling:" ++ string(name(el)), raw);
    {*:recorded, diagnostics: st.diagnostics ++ [for (command in font_decl_unknown(raw, st.custom_colors))
        util.diagnostic("unsupported-title-format", "titling", command,
            "Unsupported command in title code: \\" ++ command, el.source_offset)]}
}

// \mtcsettitle{minitoc}{title}
fn walk_mtcsettitle(el, st) {
    let mini = trim(util.raw_argument(el, "required", 0) or "")
    if (mini == "") st else record_setting(st, el, "mtctitle:" ++ mini, util.raw_argument(el, "required", 1) or "")
}

fn walk_addtocounter(el, st) {
    let assignment = counter_assignment(el)
    if (assignment == null) st
    else {*:st, counters: {*:st.counters,
        [assignment.name]: counter_value(st.counters, assignment.name) + assignment.value}}
}

// \refstepcounter also makes the counter the target of a following \label
fn walk_stepcounter(el, st, referenced) {
    let counter_name = trim(util.text_of(el))
    let counters = step_counter(st, counter_name)
    let stepped = {*:st, counters: counters}
    if (not referenced) stepped
    else {*:stepped, env_context: counter_name,
        env_context_num: string(counters[counter_name])}
}

// \newcounter{name}[parent]: a zeroed counter, reset whenever its parent steps
fn walk_newcounter(el, st) {
    let args = util.command_args(el)
    let counter_name = if (len(args) > 0) trim(util.text_of(args[0])) else ""
    let parent = util.optional_raw(el)
    if (counter_name == "") st
    else {*:st, counters: {*:st.counters, [counter_name]: 0},
        counter_resets: if (parent == null) st.counter_resets
            else add_entry(st.counter_resets, trim(parent), counter_name)}
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

fn walk_theoremstyle(el, st) {
    let style = trim(util.text_of(el))
    if (style == "plain" or style == "definition" or style == "remark")
        {*:st, theorem_style: style}
    else st
}

fn walk_numberwithin(el, st) {
    let args = util.command_args(el)
    if (not registry.active(st.packages, "amsmath") or len(args) < 2) st
    else {*:st, counter_within: add_entry(st.counter_within,
        trim(util.text_of(args[0])), trim(util.text_of(args[1])))}
}

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
    let within_idx = label_idx + 1
    let within = if (within_idx < n and el[within_idx] is element and
        string(name(el[within_idx])) == "brack_group")
        trim(util.text_of(el[within_idx])) else null

    let def = {
        env_name: clean_name,
        label: label_text,
        shared_counter: shared_counter,
        style: st.theorem_style,
        numbered: is_numbered
    }
    let new_defs = add_entry(st.theorem_defs, clean_name, def)
    // add counter for this theorem type if not shared and not unnumbered
    let is_unnumbered = is_numbered == false
    let new_counters = if (is_unnumbered or shared_counter != null) st.counters
                      else {*:st.counters, [clean_name]: 0}
    let within_counters = if (within == null) st.counter_within
        else add_entry(st.counter_within, clean_name, within)
    {*:st, theorem_defs: new_defs, counters: new_counters,
        counter_within: within_counters}
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
    let float_kind = floats.kind_of(st.float_kinds, tag_str)
    if (amsmath.handles(tag_str, st.packages)) walk_amsmath(el, st)
    // a package's or \newfloat's float environment
    else if (float_kind != null) walk_float(el, st, float_kind)
    else if (thm_def != null) { walk_custom_theorem(el, st, thm_def) }
    else { walk_children(el, 0, len(el), st) }
}

fn walk_custom_theorem(el, st, thm_def) {
    if (thm_def.numbered == false) {
        // unnumbered: just walk children
        let new_state = {*:st, env_context: thm_def.env_name, env_context_num: ""}
        restore_context(walk_children(el, 0, len(el), new_state), st)
    } else {
        walk_custom_theorem_numbered(el, st, thm_def)
    }
}

fn walk_custom_theorem_numbered(el, st, thm_def) {
    // a shared counter ([counter] in \newtheorem) numbers both environments
    let counter_name = if (thm_def.shared_counter != null) thm_def.shared_counter
                       else thm_def.env_name
    let new_counters = step_counter(st, counter_name)
    let scoped = scoped_counter({*:st, counters: new_counters}, counter_name,
        new_counters[counter_name])
    let env_text = trim(util.text_of(el))
    let entry = make_thm_entry(thm_def.env_name, scoped.number, env_text)
    let new_theorems = st.theorems ++ [entry]
    let new_state = {*:st, counters: new_counters,
        counter_positions: scoped.positions,
        theorems: new_theorems,
        env_context: thm_def.env_name, env_context_num: string(scoped.number)}
    restore_context(walk_children(el, 0, len(el), new_state), st)
}

// Scope counters lazily against the current heading identity. This keeps
// shared theorem counters and amsmath equation rows on the same reset path.
fn scoped_counter(st, counter_name, raw_number) {
    let parent = util.lookup(st.counter_within, counter_name)
    if (parent == null) {number: raw_number, positions: st.counter_positions}
    else {
        let identity = heading_identity(st.counters, parent)
        let prior = util.lookup(st.counter_positions, counter_name)
        let next = if (prior != null and prior.identity == identity)
            prior.sequence + 1 else 1
        let prefix = compute_section_num(st.counters, parent, st)
        {number: string(prefix) ++ "." ++ string(next),
         positions: add_entry(st.counter_positions, counter_name,
            {identity: identity, sequence: next})}
    }
}

fn heading_identity(counters, parent) {
    let chapter = string(counters.chapter)
    if (parent == "chapter") chapter
    else {
        let section = chapter ++ ":" ++ string(counters.section)
        if (parent == "section") section
        else {
            let subsection = section ++ ":" ++ string(counters.subsection)
            if (parent == "subsection") subsection
            else subsection ++ ":" ++ string(counters.subsubsection)
        }
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

    let subordinal = if (label_type == "subfigure") st.subfigure_count
        else if (label_type == "subtable") st.subtable_count else null
    let entry = {type: label_type, number: label_number, id: label_id, title: label_title,
        subnumber: if (subordinal == null) null
            else if (subordinal <= 26) chr(96 + subordinal) else string(subordinal)}
    let new_labels = add_entry(st.labels, label_name, entry)
    {*:st, labels: new_labels}
}

// ============================================================
// Footnotes
// ============================================================

// Footnotes are keyed by source offset, so notes with identical text keep their
// own numbers. \footnote[n] and \footnotemark[n] use n and leave the counter alone.
fn walk_footnote(el, st) {
    let explicit = explicit_footnote_number(el)
    let new_counters = if (explicit != null) st.counters else step_counter(st, "footnote")
    let fn_num = if (explicit != null) explicit else new_counters.footnote
    let entry = {number: fn_num, node: el, offset: el.source_offset}
    walk_note_body(el, {*:st, counters: new_counters, footnotes: st.footnotes ++ [entry]}, fn_num)
}

// A note's text sets \@currentlabel to its mark, so a \label inside refers to the
// note. Citation footnotes hold keys, not text, and are not walked.
fn walk_note_body(el, st, fn_num) {
    let tag = string(name(el))
    if (tag != "footnote" and tag != "footnotetext") st
    else restore_context(walk_children(el, 0, len(el), {*:st, env_context: "footnote",
        env_context_num: footmisc.mark(registry.options_for(st.packages, "footmisc"), fn_num)}), st)
}

// \footnotemark places a mark whose text a later \footnotetext supplies
fn walk_footnotemark(el, st) {
    let explicit = explicit_footnote_number(el)
    let new_counters = if (explicit != null) st.counters else step_counter(st, "footnote")
    let fn_num = if (explicit != null) explicit else new_counters.footnote
    {*:st, counters: new_counters,
        footnote_marks: add_entry(st.footnote_marks, string(el.source_offset), fn_num)}
}

// \footnotetext[n]{text} supplies the text for mark n, by default the current counter
fn walk_footnotetext(el, st) {
    let explicit = explicit_footnote_number(el)
    let fn_num = if (explicit != null) explicit else counter_value(st.counters, "footnote")
    walk_note_body(el, {*:st, footnotes: st.footnotes ++ [{number: fn_num, node: el,
        offset: el.source_offset}]}, fn_num)
}

// the [n] of \footnote, \footnotemark or \footnotetext; citation footnotes read
// their optional arguments as notes instead
fn explicit_footnote_number(el) {
    let tag = string(name(el))
    let raw = util.optional_raw(el)
    if (raw == null or (tag != "footnote" and tag != "footnotemark" and tag != "footnotetext")) null
    else int(trim(raw)) ^ { null }
}

// ============================================================
// Counter operations
// ============================================================

// LaTeX's built-in reset chain (\@addtoreset): stepping a counter zeroes these,
// and each of them zeroes its own in turn
let RESETS = {chapter: ["section"], section: ["subsection"], subsection: ["subsubsection"],
    subsubsection: ["paragraph"]}

fn step_counter(st, counter_name) {
    let stepped = {*:st.counters, [counter_name]: counter_value(st.counters, counter_name) + 1}
    reset_children(stepped, counter_name, st.counter_resets)
}

fn counter_value(counters, counter_name) {
    let value = counters[counter_name]
    if (value == null) 0 else value
}

fn reset_children(counters, parent, resets) {
    let builtin = RESETS[parent]
    let children = (if (builtin == null) [] else builtin) ++
        [for (entry in resets where entry.key == parent) entry.val]
    reset_list(counters, children, 0, resets)
}

fn reset_list(counters, children, i, resets) {
    if (i >= len(children)) counters
    else reset_list(reset_children({*:counters, [children[i]]: 0}, children[i], resets),
        children, i + 1, resets)
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
