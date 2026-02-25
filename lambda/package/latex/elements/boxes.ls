// latex/elements/boxes.ls — Box commands: \fbox, \mbox, \makebox, \framebox, \parbox, \raisebox, etc.
// Also low-level boxes: \llap, \rlap, \smash, \phantom, \hphantom, \vphantom

import util: .lambda.package.latex.util

// ============================================================
// Simple boxes (take pre-rendered items)
// ============================================================

// \mbox{text} — prevent line breaks within text
pub fn render_mbox(items) {
    <span class: "latex-mbox", style: "white-space:nowrap"; for c in items { c }>
}

// \fbox{text} — framed box
pub fn render_fbox(items) {
    <span class: "latex-fbox", style: "border:1px solid;padding:3pt"; for c in items { c }>
}

// \frame{text} — frame with no padding (TeX primitive)
pub fn render_frame(items) {
    <span class: "latex-frame", style: "border:1px solid"; for c in items { c }>
}

// ============================================================
// Sized boxes (take the raw element for argument parsing)
// ============================================================

// \makebox[width][pos]{text}
// el children: [brack_group(width)?, brack_group(pos)?, ...content]
pub fn render_makebox(el, content_items) {
    let parsed = parse_brack_args(el)
    let style = build_makebox_style(parsed.width, parsed.pos)
    <span class: "latex-makebox", style: style; for c in content_items { c }>
}

// \framebox[width][pos]{text} — like makebox with a border
pub fn render_framebox(el, content_items) {
    let parsed = parse_brack_args(el)
    let style = build_makebox_style(parsed.width, parsed.pos) ++ ";border:1px solid;padding:3pt"
    <span class: "latex-framebox", style: style; for c in content_items { c }>
}

// \parbox[pos]{width}{text}
// el children: [brack_group(pos)?, width_text, ...content_text]
// content_items: already rendered non-brack children
pub fn render_parbox(el, content_items) {
    let parsed = parse_parbox_args(el)
    let style = build_parbox_style(parsed.width, parsed.pos)
    // content_items[0] is the width text (skip it), rest is actual content
    let body = if (len(content_items) > 1) slice(content_items, 1, len(content_items))
               else content_items
    <div class: "latex-parbox", style: style; for c in body { c }>
}

// \raisebox{lift}[height][depth]{text}
// el children: [lift_text, brack_group?, brack_group?, ...content]
// content_items: already rendered non-brack children: [lift, ...content]
pub fn render_raisebox(el, content_items) {
    let lift = if (len(content_items) > 0) trim(string(content_items[0])) else "0pt"
    let body = if (len(content_items) > 1) slice(content_items, 1, len(content_items))
               else []
    let style = "display:inline-block;position:relative;bottom:" ++ lift
    <span class: "latex-raisebox", style: style; for c in body { c }>
}

// ============================================================
// Low-level boxes (take pre-rendered items)
// ============================================================

// \llap{text} — left overlap (zero width, content extends left)
pub fn render_llap(items) {
    <span class: "latex-llap", style: "display:inline-block;width:0;text-align:right;overflow:visible"; for c in items { c }>
}

// \rlap{text} — right overlap (zero width, content extends right)
pub fn render_rlap(items) {
    <span class: "latex-rlap", style: "display:inline-block;width:0;text-align:left;overflow:visible"; for c in items { c }>
}

// \smash{text} — zero height, content overflows
pub fn render_smash(items) {
    <span class: "latex-smash", style: "display:inline-block;height:0;vertical-align:baseline;overflow:visible"; for c in items { c }>
}

// \phantom{text} — invisible, but takes up space
pub fn render_phantom(items) {
    <span class: "latex-phantom", style: "visibility:hidden"; for c in items { c }>
}

// \hphantom{text} — phantom with zero height
pub fn render_hphantom(items) {
    <span class: "latex-hphantom", style: "visibility:hidden;display:inline-block;height:0;overflow:hidden"; for c in items { c }>
}

// \vphantom{text} — phantom with zero width
pub fn render_vphantom(items) {
    <span class: "latex-vphantom", style: "visibility:hidden;display:inline-block;width:0;overflow:hidden"; for c in items { c }>
}

// ============================================================
// Argument parsing helpers
// ============================================================

// extract optional [width] and [pos] from leading brack_group children
fn parse_brack_args(el) {
    let n = len(el)
    let brack_groups = collect_brack_groups(el, 0, n, [])
    let brack_count = len(brack_groups)
    let width = if (brack_count > 0) trim(util.text_of(brack_groups[0])) else null
    let pos = if (brack_count > 1) trim(util.text_of(brack_groups[1])) else null
    {width: width, pos: pos}
}

// for \parbox[pos]{width}{content}: extract optional [pos] from brack_groups
fn parse_parbox_args(el) {
    let n = len(el)
    let brack_groups = collect_brack_groups(el, 0, n, [])
    let brack_count = len(brack_groups)
    let pos = if (brack_count > 0) trim(util.text_of(brack_groups[0])) else null
    // width comes from first non-brack child (handled by caller via content_items)
    let width = find_first_non_brack_text(el, 0, n)
    {width: width, pos: pos}
}

fn collect_brack_groups(el, i, n, acc) {
    if i >= n { acc }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == "brack_group")
            collect_brack_groups(el, i + 1, n, acc ++ [child])
        else acc
    }
}

fn find_first_non_brack_text(el, i, n) {
    if i >= n { "auto" }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == "brack_group")
            find_first_non_brack_text(el, i + 1, n)
        else if (child is string) trim(child)
        else if (child is element) trim(util.text_of(child))
        else "auto"
    }
}

// ============================================================
// Style builders
// ============================================================

fn build_makebox_style(width, pos) {
    let base = "display:inline-block"
    let w = if (width != null) base ++ ";width:" ++ width else base
    let align = resolve_pos(pos)
    let result = if (align != null) w ++ ";text-align:" ++ align else w
    result
}

fn build_parbox_style(width, pos) {
    let base = "display:inline-block"
    let w = if (width != null) base ++ ";width:" ++ width else base
    let valign = resolve_vertical_pos(pos)
    let result = if (valign != null) w ++ ";vertical-align:" ++ valign else w
    result
}

fn resolve_pos(pos) {
    if (pos == null) null
    else if (pos == "c") "center"
    else if (pos == "l") "left"
    else if (pos == "r") "right"
    else if (pos == "s") "justify"
    else null
}

fn resolve_vertical_pos(pos) {
    if (pos == null) null
    else if (pos == "t") "top"
    else if (pos == "b") "bottom"
    else if (pos == "c") "middle"
    else if (pos == "m") "middle"
    else null
}
