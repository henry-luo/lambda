// latex/context.ls — Rendering context for LaTeX-to-HTML conversion
// Immutable context threaded through the transformation.
// Child contexts are new maps with overridden fields.

// ============================================================
// Context creation
// ============================================================

// create the root rendering context from user options
pub fn make_context(options) => {
    // document class
    docclass: if (options != null and options.docclass != null) options.docclass else "article",

    // output options
    standalone: if (options != null and options.standalone != null) options.standalone else false,
    numbering: if (options != null and options.numbering != null) options.numbering else true,
    toc: if (options != null and options.toc != null) options.toc else false,

    // counters (section numbering)
    counters: {
        chapter: 0,
        section: 0,
        subsection: 0,
        subsubsection: 0,
        paragraph: 0,
        figure: 0,
        table: 0,
        equation: 0,
        footnote: 0,
        enumi: 0,
        enumii: 0,
        enumiii: 0
    },

    // accumulated state (collected during rendering, used in post-processing)
    labels: {},         // label_name → {type: string, number: string, id: string}
    headings: [],       // [{level: int, number: string, text: string, id: string}]
    footnotes: [],      // [{number: int, content: element}]

    // title metadata (extracted from preamble)
    title: null,
    author: null,
    date: null,

    // current list nesting depth
    list_depth: 0
}

// ============================================================
// Counter operations
// ============================================================

// increment a counter and reset subordinate counters
pub fn step_counter(ctx, counter_name) {
    let c = ctx.counters
    match counter_name {
        case "chapter":
            {ctx, counters: {c, chapter: c.chapter + 1, section: 0, subsection: 0, subsubsection: 0}}
        case "section":
            {ctx, counters: {c, section: c.section + 1, subsection: 0, subsubsection: 0}}
        case "subsection":
            {ctx, counters: {c, subsection: c.subsection + 1, subsubsection: 0}}
        case "subsubsection":
            {ctx, counters: {c, subsubsection: c.subsubsection + 1}}
        case "figure":
            {ctx, counters: {c, figure: c.figure + 1}}
        case "table":
            {ctx, counters: {c, table: c.table + 1}}
        case "equation":
            {ctx, counters: {c, equation: c.equation + 1}}
        case "footnote":
            {ctx, counters: {c, footnote: c.footnote + 1}}
        case "enumi":
            {ctx, counters: {c, enumi: c.enumi + 1, enumii: 0, enumiii: 0}}
        case "enumii":
            {ctx, counters: {c, enumii: c.enumii + 1, enumiii: 0}}
        case "enumiii":
            {ctx, counters: {c, enumiii: c.enumiii + 1}}
        default: ctx
    }
}

// format section number string based on docclass and level
pub fn format_section_number(ctx, level) {
    let c = ctx.counters
    if (not ctx.numbering) {
        null
    } else if (ctx.docclass == "book" or ctx.docclass == "report") {
        match level {
            case 0: string(c.chapter)
            case 1: string(c.chapter) ++ "." ++ string(c.section)
            case 2: string(c.chapter) ++ "." ++ string(c.section) ++ "." ++ string(c.subsection)
            case 3: string(c.chapter) ++ "." ++ string(c.section) ++ "." ++ string(c.subsection) ++ "." ++ string(c.subsubsection)
            default: null
        }
    } else {
        // article class
        match level {
            case 0: string(c.section)
            case 1: string(c.section) ++ "." ++ string(c.subsection)
            case 2: string(c.section) ++ "." ++ string(c.subsection) ++ "." ++ string(c.subsubsection)
            default: null
        }
    }
}

// ============================================================
// Label operations
// ============================================================

// record a label with its associated number and id
pub fn add_label(ctx, label_name, label_type, number, id) {
    let label_entry = {type: label_type, number: number, id: id}
    let label_map = map([label_name, label_entry])
    let new_labels = {ctx.labels, label_map}
    {ctx, labels: new_labels}
}

// record a heading for table of contents
pub fn add_heading(ctx, level, number, text, id) {
    let entry = {level: level, number: number, text: text, id: id}
    {ctx, headings: ctx.headings ++ [entry]}
}

// record a footnote
pub fn add_footnote(ctx, content) {
    let num = ctx.counters.footnote + 1
    let new_ctx = step_counter(ctx, "footnote")
    let entry = {number: num, content: content}
    {new_ctx, footnotes: new_ctx.footnotes ++ [entry]}
}

// ============================================================
// Title metadata
// ============================================================

pub fn set_title(ctx, title) => {ctx, title: title}
pub fn set_author(ctx, author) => {ctx, author: author}
pub fn set_date(ctx, date) => {ctx, date: date}

// derive a context for deeper list nesting
pub fn enter_list(ctx) => {ctx, list_depth: ctx.list_depth + 1}
