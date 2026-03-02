// latex/elements/spacing.ls — Spacing, line breaks, page breaks, rules
// Handles \hspace, \vspace, \hskip, \vskip, \\, \newline, \hrule, \hfill, etc.

import util: lambda.package.latex.util

// ============================================================
// Line breaks
// ============================================================

pub fn render_linebreak(ctx) {
    {result: <br>, ctx: ctx}
}

pub fn render_newline(ctx) {
    {result: <br>, ctx: ctx}
}

// ============================================================
// Horizontal space
// ============================================================

pub fn render_hspace(node, ctx) {
    let amount = get_length_arg(node)
    if (amount != null) {
        let result = <span class: "latex-hspace", style: "margin-left:" ++ amount>
        {result: result, ctx: ctx}
    } else {{result: null, ctx: ctx}}
}

// ============================================================
// Vertical space
// ============================================================

pub fn render_vspace(node, ctx) {
    let amount = get_length_arg(node)
    if (amount != null) {
        let result = <div class: "latex-vspace", style: "margin-top:" ++ amount>
        {result: result, ctx: ctx}
    } else {{result: null, ctx: ctx}}
}

// ============================================================
// Horizontal rule
// ============================================================

pub fn render_hrule(ctx) {
    {result: <hr>, ctx: ctx}
}

// ============================================================
// Page break (no-op in HTML, just a visual separator)
// ============================================================

pub fn render_pagebreak(ctx) {
    {result: <div class: "latex-pagebreak">, ctx: ctx}
}

// ============================================================
// Horizontal fill (flexible space)
// ============================================================

pub fn render_hfill(ctx) {
    let result = <span class: "latex-hfill", style: "flex:1">
    {result: result, ctx: ctx}
}

// ============================================================
// Non-breaking space: ~
// ============================================================

pub fn render_nbsp(ctx) {
    {result: "\u00A0", ctx: ctx}
}

// ============================================================
// Vertical skip commands (\bigskip, \medskip, \smallskip)
// ============================================================

pub fn render_bigskip_el() {
    <div class: "latex-bigskip", style: "margin-top:12pt">
}

pub fn render_medskip_el() {
    <div class: "latex-medskip", style: "margin-top:6pt">
}

pub fn render_smallskip_el() {
    <div class: "latex-smallskip", style: "margin-top:3pt">
}

// \bigbreak, \medbreak, \smallbreak — same as skip variants in HTML
pub fn render_bigbreak_el() {
    <div class: "latex-bigskip", style: "margin-top:12pt">
}

pub fn render_medbreak_el() {
    <div class: "latex-medskip", style: "margin-top:6pt">
}

pub fn render_smallbreak_el() {
    <div class: "latex-smallskip", style: "margin-top:3pt">
}

// ============================================================
// Horizontal spacing commands (\quad, \qquad, \enspace, etc.)
// ============================================================

pub fn render_quad_el() {
    <span class: "latex-quad", style: "margin-left:1em">
}

pub fn render_qquad_el() {
    <span class: "latex-qquad", style: "margin-left:2em">
}

pub fn render_enspace_el() {
    "\u2002"
}

pub fn render_thinspace_el() {
    "\u2009"
}

pub fn render_negthinspace_el() {
    <span class: "latex-negthinspace", style: "margin-left:-0.16667em">
}

// ============================================================
// \noindent — suppresses paragraph indentation
// ============================================================

pub fn render_noindent_el() {
    <span class: "latex-noindent">
}

// ============================================================
// \/ — italic correction (zero-width in HTML)
// ============================================================

pub fn render_italcorr_el() {
    null
}

// element-returning versions for two-pass rendering
pub fn render_hspace_el(node) {
    let amount = get_length_arg(node)
    if amount != null {
        <span class: "latex-hspace", style: "margin-left:" ++ amount>
    } else { null }
}

pub fn render_vspace_el(node) {
    let amount = get_length_arg(node)
    if amount != null {
        <div class: "latex-vspace", style: "margin-top:" ++ amount>
    } else { null }
}

// ============================================================
// Helpers
// ============================================================

// extract the length argument like {1cm}, {2em}, {0.5in} from a node
fn get_length_arg(node) {
    let cg = util.find_child(node, 'curly_group')
    if (cg != null) {
        trim(util.text_of(cg))
    } else {
        // try direct text
        let txt = util.text_of(node)
        let trimmed = trim(txt)
        if (trimmed != "") { trimmed } else { null }
    }
}
