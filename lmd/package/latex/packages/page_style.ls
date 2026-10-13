// Fancy page-style declarations are collected in script; page repetition is host work.
import util: ~~.util

let COMMANDS = ["pagestyle", "thispagestyle", "lhead", "chead", "rhead",
    "lfoot", "cfoot", "rfoot"]

fn entries_in(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        if (len([for (candidate in COMMANDS where candidate == tag)
            candidate]) > 0)
            [{tag: tag, raw: util.raw_argument(node, "required", 0),
              offset: node.source_offset,
              alternate: util.raw_argument(node, "optional", 0)}]
        else [for (child in node, entry in entries_in(child)) entry]
    }
}

fn preamble_entries(ast) => [for (child in ast,
    entry in if (child is element and string(name(child)) != "document")
        entries_in(child) else []) entry]

fn last_value(entries, key, fallback = null) {
    let matches = [for (entry in entries where entry.tag == key) entry.raw]
    if (len(matches) == 0) fallback else matches[len(matches) - 1]
}

fn last_style(entries) {
    let styles = [for (entry in entries where entry.tag == "pagestyle")
        trim(entry.raw)]
    if (len(styles) == 0) "plain" else styles[len(styles) - 1]
}

// \pagenumbering styles as CSS counter styles; gobble prints no page numbers
let NUMBER_STYLES = {arabic: "decimal", roman: "lower-roman", Roman: "upper-roman",
    alph: "lower-alpha", Alph: "upper-alpha", gobble: "none"}

fn numbering_entries(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "pagenumbering")
        [{style: trim(util.text_of(node)), offset: node.source_offset}]
    else [for (child in node, entry in numbering_entries(child)) entry]
}

// The page counter as CSS content. Output has one numbering for the whole
// document, so the last \pagenumbering applies and a change is reported.
fn page_numbering(ast) {
    let entries = numbering_entries(ast)
    let invalid = [for (entry in entries where NUMBER_STYLES[entry.style] == null)
        util.diagnostic("unsupported-page-numbering", "latex", entry.style,
            "Unsupported page numbering style " ++ entry.style, entry.offset)]
    let valid = [for (entry in entries where NUMBER_STYLES[entry.style] != null) entry]
    let latest = if (len(valid) == 0) null else valid[len(valid) - 1]
    let changed = [for (entry in valid where entry.style != latest.style) entry]
    let css_style = if (latest == null) "decimal" else NUMBER_STYLES[latest.style]
    {counter: if (css_style == "none") "\"\"" else if (css_style == "decimal") "counter(page)"
            else "counter(page," ++ css_style ++ ")",
     diagnostics: invalid ++ (if (len(changed) == 0) [] else
        [util.diagnostic("page-numbering-approximation", "latex", latest.style,
            "Output has one page numbering; the last \\pagenumbering applies to every page",
            latest.offset)])}
}

pub fn prepare(ast, active) {
    let running = prepare_running(ast, active)
    let numbering = page_numbering(ast)
    {*:running, page_counter: numbering.counter,
        diagnostics: running.diagnostics ++ numbering.diagnostics}
}

fn prepare_running(ast, active) {
    if (not active) {style: "plain", header: null, footer: null,
        diagnostics: [], offset: null}
    else {
        let entries = preamble_entries(ast)
        let style = last_style(entries)
        let slots = ["lhead", "chead", "rhead", "lfoot", "cfoot", "rfoot"]
        let invalid = [for (entry in entries
            where entry.tag == "pagestyle" and
                trim(entry.raw) != "plain" and trim(entry.raw) != "empty" and
                trim(entry.raw) != "fancy" and trim(entry.raw) != "fancyplain")
            util.diagnostic("unsupported-page-style", "fancyhdr", entry.raw,
                "Unsupported page style " ++ entry.raw, entry.offset)]
        let used = [for (entry in entries where entry.tag == "pagestyle")
            entry.offset]
        let active_fancy = style == "fancy" or style == "fancyplain"
        {style: style,
         header: if (active_fancy) {left: last_value(entries, "lhead"),
             center: last_value(entries, "chead"),
             right: last_value(entries, "rhead")} else null,
         footer: if (active_fancy) {left: last_value(entries, "lfoot"),
             center: last_value(entries, "cfoot"),
             right: last_value(entries, "rfoot")} else null,
         diagnostics: invalid,
         offset: if (len(used) == 0) null else used[len(used) - 1]}
    }
}

pub fn quoted_content(text) => "\"" ++ replace(replace(replace(replace(text,
    "\\", "\\\\"), "\"", "\\\""), "\n", "\\A "), "\r", "") ++ "\""

fn content_css(node, page_counter) {
    if (node is string) quoted_content(node)
    else if (node is array or node is list)
        join([for (child in node) content_css(child, page_counter)], " ")
    else if (not (node is element)) ""
    else if (node.class == "latex-page-number") page_counter
    else if (node.class == "latex-page-count") "counter(pages)"
    else if (node.class == "latex-chapter-mark") "string(latex-chapter)"
    else if (node.class == "latex-section-mark") "string(latex-section)"
    else if (node.class == "latex-target-page")
        "target-counter(" ++ quoted_content(node.href) ++ ",page)"
    else if (string(name(node)) == "br") "\"\\A \""
    else content_css(content(node), page_counter)
}

pub fn stylesheet(profile, render_slot) {
    let page_counter = if (profile.page_counter == null) "counter(page)" else profile.page_counter
    let bands = if (profile.header == null and profile.footer == null) ""
        else join([for (band in ["header", "footer"], slot in ["left", "center", "right"])
            "@page{@" ++ (if (band == "header") "top" else "bottom") ++ "-" ++ slot ++
                "{white-space:pre-line;font-size:0.85em;content:" ++
                slot_css(profile[band][slot], render_slot, page_counter) ++ ";}}\n"], "")
    bands ++ ".latex-page-number::before,.latex-page-count::before,.latex-target-page::before{content:'?';}\n" ++
        "@media print{.latex-running-header,.latex-running-footer{display:none}" ++
        ".latex-page-number::before{content:" ++ page_counter ++ "}" ++
        ".latex-page-count::before{content:counter(pages)}" ++
        ".latex-target-page::before{content:target-counter(attr(href),page)}" ++
        ".latex-chapter-mark::before{content:string(latex-chapter)}" ++
        ".latex-section-mark::before{content:string(latex-section)}}\n"
}

fn slot_css(raw, render_slot, page_counter) {
    let generated = content_css(render_slot(raw), page_counter)
    if (trim(generated) == "") "none" else generated
}

fn has_inline_format(node) {
    if (node is array or node is list) any([for (child in node) has_inline_format(child)])
    else if (not (node is element)) false
    else (string(name(node)) != "br" and node.class != "latex-page-number" and
        node.class != "latex-page-count" and node.class != "latex-target-page" and
        node.class != "latex-chapter-mark" and node.class != "latex-section-mark") or
        has_inline_format(content(node))
}

pub fn target_issues(profile, render_slot, target) {
    if (target != "pdf" or (profile.header == null and profile.footer == null)) []
    else if (any([for (band in ["header", "footer"], slot in ["left", "center", "right"])
        has_inline_format(render_slot(profile[band][slot]))]))
        [util.diagnostic("running-format-approximation", "fancyhdr", null,
            "PDF margin content preserves text, marks and counters; mixed inline font formatting is flattened",
            profile.offset)]
    else []
}
