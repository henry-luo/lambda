// latex/elements/boxes.ls — Box commands: \fbox, \mbox, \makebox, \framebox, \parbox, \raisebox, etc.
// Also low-level boxes: \llap, \rlap, \smash, \phantom, \hphantom, \vphantom

import util: lambda.doc.latex.util

// ============================================================
// Simple boxes (take pre-rendered items)
// ============================================================

// TeX boxes are horizontal lists: text-indent is inherited in CSS, so a box
// inside an indented paragraph would otherwise indent its own first line
let BOX_BASE = "display:inline-block;text-indent:0;white-space:nowrap"

// The frame-box model shared by tcolorbox, framed, mdframed and fancybox: a
// background, a line (width, style, color) on all or some sides, padding, a corner
// radius and a shadow, each omitted when the spec leaves it null. A width of "0"
// draws no line.
pub fn frame_css(spec) {
    let line = (spec.width or "1px") ++ " " ++ (spec.style or "solid") ++
        (if (spec.line == null) "" else " " ++ spec.line)
    let border = if (spec.width == "0") ""
        else if (spec.sides == null) "border:" ++ line ++ ";"
        else util.str_join([for (side in spec.sides) "border-" ++ side ++ ":" ++ line ++ ";"], "");
    (if (spec.background == null) "" else "background-color:" ++ spec.background ++ ";") ++ border ++
        (if (spec.padding == null) "" else "padding:" ++ spec.padding ++ ";") ++
        (if (spec.radius == null) "" else "border-radius:" ++ spec.radius ++ ";") ++
        (if (spec.shadow == null) "" else "box-shadow:" ++ spec.shadow ++ ";")
}

// fancybox's boxes, framed by \fboxrule (.4pt) at \fboxsep (3pt): \shadowbox casts
// a \shadowsize (4pt) shadow to the lower right, \doublebox draws two lines, \ovalbox
// rounds the corners and \Ovalbox does so with a thicker line
let FANCYBOX = {shadowbox: {width: "0.4pt", padding: "3pt", shadow: "4pt 4pt 0 currentColor"},
    doublebox: {width: "3px", style: "double", padding: "3pt"},
    ovalbox: {width: "0.4pt", padding: "3pt", radius: "0.6em"},
    Ovalbox: {width: "0.8pt", padding: "3pt", radius: "0.6em"}}
pub let FANCYBOX_COMMANDS = [for (k, v at FANCYBOX) string(k)]

// a fancybox command's CSS: an inline TeX box on the frame-box model
pub fn fancybox_css(tag) => BOX_BASE ++ ";" ++ frame_css(FANCYBOX[tag])

// boxedminipage: a minipage of the given width framed like \fbox
pub fn boxed_minipage_css(width) => "display:inline-block;vertical-align:middle;" ++
    (if (width == null) "" else "width:" ++ width ++ ";") ++ frame_css({width: "0.4pt", padding: "3pt"})

// \mbox{text} — prevent line breaks within text
pub fn render_mbox(items) {
    <span class: "latex-mbox", style: "white-space:nowrap", for c in items { c }>
}

// \fbox{text} — framed box; a TeX box, so the border encloses tall content
// such as a picture instead of only the text line
pub fn render_fbox(items) {
    <span class: "latex-fbox", style: BOX_BASE ++ ";border:1px solid;padding:3pt", for c in items { c }>
}

// \frame{text} — frame with no padding (TeX primitive)
pub fn render_frame(items) {
    <span class: "latex-frame", style: BOX_BASE ++ ";border:1px solid", for c in items { c }>
}

// ============================================================
// Sized boxes (take the raw element for argument parsing)
// ============================================================

// \makebox[width][pos]{text}
// el children: [brack_group(width)?, brack_group(pos)?, ...content]
pub fn render_makebox(el, content_items) {
    let parsed = parse_brack_args(el)
    sized_box("latex-makebox", parsed, "", content_items)
}

// \framebox[width][pos]{text} — like makebox with a border
pub fn render_framebox(el, content_items) {
    let parsed = parse_brack_args(el)
    sized_box("latex-framebox", parsed, ";border:1px solid;padding:3pt", content_items)
}

// A sized TeX box is exactly its width and never wraps: content wider than the
// box sticks out on the left for [r], both sides for [c] and the right for [l].
// The inner box is offset relative to the box and then by its own width, which
// anchors it the way TeX does whether it is narrower or wider than the box.
fn sized_box(class_name, parsed, frame, content_items) {
    if (parsed.width == null)
        <span class: class_name, style: BOX_BASE ++ frame, for c in content_items { c }>
    else
        <span class: class_name, style: build_makebox_style(parsed.width) ++ frame,
            <span class: "latex-box-content", style: box_content_anchor(parsed.pos),
                for c in content_items { c }>>
}

// \parbox[pos]{width}{text}
// el children: [brack_group(pos)?, width_text, ...content_text]
// content_items: already rendered non-brack children
pub fn render_parbox(el, content_items) {
    let parsed = parse_parbox_args(el)
    let style = build_parbox_style(parsed.width, parsed.pos)
    // content_items[0] is the width text (skip it), rest is actual content
    let body = if (len(content_items) > 1) slice(content_items, 1, len(content_items))
               else content_items;
    <div class: "latex-parbox", style: style, for c in body { c }>
}

// \raisebox{lift}[height][depth]{text}
// el children: [lift_text, brack_group?, brack_group?, ...content]
// content_items: already rendered non-brack children: [lift, ...content]
pub fn render_raisebox(el, content_items) {
    let lift = if (len(content_items) > 0) trim(string(content_items[0])) else "0pt"
    let body = if (len(content_items) > 1) slice(content_items, 1, len(content_items))
               else []
    let style = "display:inline-block;position:relative;bottom:" ++ lift;
    <span class: "latex-raisebox", style: style, for c in body { c }>
}

// ============================================================
// Low-level boxes (take pre-rendered items)
// ============================================================

// \llap{text} — left overlap (zero width, content extends left)
// \rule[raise]{width}{height}: a solid box in the text color, raised from the baseline
pub fn render_rule(el) {
    let width = rule_dimension(util.raw_argument(el, "required", 0))
    let height = rule_dimension(util.raw_argument(el, "required", 1))
    let raw_lift = util.raw_argument(el, "optional", 0)
    let lift = if (raw_lift == null) "0" else rule_dimension(raw_lift)
    if (width == null or height == null or lift == null)
        util.unsupported_element("latex", "Invalid rule dimensions", el.source_offset)
    else <span class: "latex-rule", style: "display:inline-block;width:" ++ width ++
        ";height:" ++ height ++ ";vertical-align:" ++ lift ++ ";background-color:currentColor">
}

fn rule_dimension(raw) => if (raw == null) null else util.css_dimension(raw)

pub fn render_llap(items) {
    <span class: "latex-llap", style: "display:inline-block;width:0;text-align:right;overflow:visible", for c in items { c }>
}

// \rlap{text} — right overlap (zero width, content extends right)
pub fn render_rlap(items) {
    <span class: "latex-rlap", style: "display:inline-block;width:0;text-align:left;overflow:visible", for c in items { c }>
}

// \smash{text} — zero height, content overflows
pub fn render_smash(items) {
    <span class: "latex-smash", style: "display:inline-block;height:0;vertical-align:baseline;overflow:visible", for c in items { c }>
}

// \phantom{text} — invisible, but takes up space
pub fn render_phantom(items) {
    <span class: "latex-phantom", style: "visibility:hidden", for c in items { c }>
}

// \hphantom{text} — phantom with zero height
pub fn render_hphantom(items) {
    <span class: "latex-hphantom", style: "visibility:hidden;display:inline-block;height:0;overflow:hidden", for c in items { c }>
}

// \vphantom{text} — phantom with zero width
pub fn render_vphantom(items) {
    <span class: "latex-vphantom", style: "visibility:hidden;display:inline-block;width:0;overflow:hidden", for c in items { c }>
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
    let args = util.command_args(el)
    let n = len(args)
    let brack_groups = collect_brack_groups(args, 0, n, [])
    let brack_count = len(brack_groups)
    let pos = if (brack_count > 0) trim(util.text_of(brack_groups[0])) else null
    // width comes from first non-brack child (handled by caller via content_items)
    let width = find_first_non_brack_text(args, 0, n)
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

// the width is seen from the outside, frame included (border-box)
fn build_makebox_style(width) {
    BOX_BASE ++ ";box-sizing:border-box;vertical-align:baseline;text-align:left;width:" ++ width
}

// LaTeX centers the content when no position is given; spread is not
// reproducible in HTML and anchors like [l]
fn box_content_anchor(pos) {
    let align = resolve_pos(pos)
    let anchor = if (align == "right") ";left:100%;transform:translateX(-100%)"
        else if (align == "left" or align == "justify") ""
        else ";left:50%;transform:translateX(-50%)"
    "display:inline-block;position:relative" ++ anchor
}

fn build_parbox_style(width, pos) {
    let base = "display:inline-block;text-indent:0"
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
