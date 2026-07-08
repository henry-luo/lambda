// math/atoms/enclose.ls — Enclosure rendering (\boxed, \fbox, \phantom, \rule)
// Handles box commands, phantom commands, and rule commands.

import box: lambda.package.math.box
import ctx: lambda.package.math.context
import css: lambda.package.math.css
import met: lambda.package.math.metrics
import util: lambda.package.math.util

// ============================================================
// Box commands (\boxed, \fbox, \bbox)
// ============================================================

pub fn render_box(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else "\\boxed"
    let content_box = if (node.content != null) render_fn(node.content, context)
        else box.text_box("", null, "ord")

    if (cmd == "\\llap") render_lap(content_box, "right")
    else if (cmd == "\\rlap") render_lap(content_box, "left")
    else if (cmd == "\\clap") render_lap(content_box, "center")
    else if (cmd == "\\bbox") render_bbox(content_box, node)
    else render_bordered(content_box)
}

// bordered box (\boxed, \fbox, \bbox)
fn render_bordered(content_box) {
    box.ml_box_full(
        <span style: "border:1px solid;padding:0.1em"; content_box.element>,
        content_box.height + 0.15,
        content_box.depth + 0.15,
        content_box.width + 0.3,
        "ord",
        0.0,
        0.0,
        content_box.height + 0.15
    )
}

fn render_bbox(content_box, node) {
    let opts = if (node.options != null) string(node.options) else ""
    let spec = bbox_spec(opts)
    let children = box.elements_of(content_box)
    let outer_style = "display:inline-block;position:relative;line-height:0;padding-left:" ++
        util.fmt_em(spec.padding) ++ ";padding-right:" ++ util.fmt_em(spec.padding) ++
        ";height:" ++ util.fmt_em(spec.outer_height) ++ ";margin-top:" ++
        util.fmt_em(0.0 - spec.padding) ++ ";top:" ++ util.fmt_em(spec.top) ++
        ";vertical-align:" ++ util.fmt_em(spec.vertical_align)
    let overlay_style = "box-sizing:border-box;position:absolute;top:" ++ fmt_bbox_dim(spec.overlay_top) ++
        ";left:0;height:" ++ util.fmt_em(spec.overlay_height) ++ ";width:100%" ++ spec.box_style
    box.ml_box_full(
        <span style: outer_style;
            <span class: "lm_box", style: overlay_style>
            <span style: "display:inline-block;position:relative;height:" ++
                util.fmt_em(content_box.height) ++ ";vertical-align:" ++
                util.fmt_em(0.0 - content_box.height);
                for (child in children) child
            >
        >,
        spec.height,
        spec.depth,
        content_box.width + spec.padding * 2.0,
        "minner",
        0.0,
        0.0,
        spec.height
    )
}

fn fmt_bbox_dim(v) {
    if (v == 0.0) "0" else util.fmt_em(v)
}

fn bbox_spec(opts) {
    let padding = if (contains(opts, "4em")) 4.0 else 0.3
    let box_style = bbox_style(opts)
    if (padding >= 4.0) bbox_large_spec(padding, box_style)
    else bbox_normal_spec(padding, box_style)
}

fn bbox_large_spec(padding, box_style) => {
        padding: padding,
        box_style: box_style,
        height: 5.45,
        depth: 4.95,
        outer_height: 10.4,
        overlay_height: 10.41,
        overlay_top: -3.7,
        top: 7.5,
        vertical_align: 8.95
}

fn bbox_normal_spec(padding, box_style) => {
        padding: padding,
        box_style: box_style,
        height: 1.75,
        depth: 1.25,
        outer_height: 3.0,
        overlay_height: 3.01,
        overlay_top: 0.0,
        top: 0.1,
        vertical_align: 1.55
}

fn bbox_style(opts) {
    bbox_background_style(opts) ++ bbox_border_style(opts)
}

fn bbox_background_style(opts) {
    if (contains(opts, "yellow")) ";background-color:#fff1c2" else ""
}

fn bbox_border_style(opts) {
    if (contains(opts, "border:solid1pxred")) ";border:solid 1px red"
    else if (contains(opts, "border:1pxsolidred")) ";border:1px solid red"
    else ""
}

// overlap commands (\llap, \rlap, \clap)
fn render_lap(content_box, align) {
    let cls = if (align == "right") "lm_llap"
        else if (align == "left") "lm_rlap"
        else "lm_clap"
    let children = box.elements_of(content_box)
    box.ml_box_full(
        <span class: cls;
            <span class: "lm_inner";
                for (child in children) child
            >
            <span class: "lm_fix">
        >,
        content_box.height,
        content_box.depth,
        0.0,
        "ord",
        0.0,
        0.0,
        if (content_box.max_font_size != null) content_box.max_font_size else content_box.height
    )
}

// ============================================================
// Phantom commands (\phantom, \hphantom, \vphantom, \smash)
// ============================================================

pub fn render_phantom(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else "\\phantom"
    let content_box = if (node.content != null)
        render_fn(node.content, ctx.derive(context, {phantom: true}))
    else box.text_box("", null, "ord")

    if (cmd == "\\hphantom") render_hphantom(content_box)
    else if (cmd == "\\vphantom") render_vphantom(content_box)
    else if (cmd == "\\smash") render_smash(content_box, node)
    else render_full_phantom(content_box)
}

// full phantom — invisible but takes up full space
fn render_full_phantom(content_box) {
    box.ml_box_full(
        <span style: "visibility:hidden;display:inline-block"; content_box.element>,
        content_box.height,
        content_box.depth,
        content_box.width,
        "ord",
        0.0,
        0.0,
        if (content_box.max_font_size != null) content_box.max_font_size else content_box.height
    )
}

// hphantom — invisible, zero height/depth, keeps width
fn render_hphantom(content_box) {
    box.ml_box(
        <span style: "visibility:hidden;display:inline-block;height:0"; content_box.element>,
        0.0,
        0.0,
        content_box.width,
        "ord"
    )
}

// vphantom — invisible, zero width, keeps height/depth
fn render_vphantom(content_box) {
    box.ml_box_full(
        <span style: "visibility:hidden;display:inline-block;width:0"; content_box.element>,
        content_box.height,
        content_box.depth,
        0.0,
        "ord",
        0.0,
        0.0,
        if (content_box.max_font_size != null) content_box.max_font_size else content_box.height
    )
}

// smash — visible, zero height and/or depth
fn render_smash(content_box, node) {
    let opts = if (node.options != null) string(node.options) else ""
    let zero_h = if (opts == "b") false else true
    let zero_d = if (opts == "t") false else true
    box.ml_box_full(
        <span style: "display:inline-block"; content_box.element>,
        if (zero_h) 0.0 else content_box.height,
        if (zero_d) 0.0 else content_box.depth,
        content_box.width,
        "ord",
        0.0,
        0.0,
        if (zero_h) 0.0 else if (content_box.max_font_size != null)
            content_box.max_font_size else content_box.height
    )
}

// ============================================================
// Rule command (\rule)
// ============================================================

pub fn render_rule(node, context, render_fn) {
    let w = parse_dim(node.width, 1.0)
    let h = parse_dim(node.height, w)
    let vert_raise = parse_dim(node.shift, 0.0)
    let rw = round_rule_dim(w)
    let rh = round_rule_dim(h)
    let rs = round_rule_dim(vert_raise)
    let rule_style = "border-right-width:" ++ fmt_rule_dim(w) ++
        ";border-top-width:" ++ fmt_rule_dim(h) ++
        ";vertical-align:" ++ fmt_rule_shift(vert_raise) ++
        ";width:" ++ fmt_rule_width(w)
    box.box_styled(css.RULE, rule_style, rh + rs, 0.0 - rs, rw, "ord")
}

// parse a dimension from a node attribute
fn parse_dim(attr, default_val) {
    if (attr == null) default_val
    else
        (let s = if (attr is string) attr
             else if (attr is element) get_all_text(attr)
             else string(attr),
         let v = parse_dim_string(s),
         if (v != null) v
         else default_val)
}

fn parse_dim_string(s) {
    let start = find_number_start(s, 0)
    if (start >= len(s)) null
    else
        (let end = find_number_end(s, start),
         let unit_end = find_unit_end(s, end),
         let num_text = normalize_decimal(slice(s, start, end), 0, ""),
         let unit = if (unit_end > end) slice(s, end, unit_end) else "pt",
         let val = float(num_text),
         if (val == null) null else convert_dim(val, unit))
}

fn convert_dim(val, unit) {
    if (unit == "em") val
    else if (unit == "ex") val * 0.432
    else if (unit == "pt") val / util.PT_PER_EM
    else if (unit == "pc") val * 12.0 / util.PT_PER_EM
    else if (unit == "mu") val / 18.0
    else if (unit == "cm") val * 28.452756 / util.PT_PER_EM
    else if (unit == "mm") val * 0.286
    else if (unit == "in") val * 72.27 / util.PT_PER_EM
    else if (unit == "bp") val * 0.101
    else if (unit == "dd") val * 1238.0 / 1157.0 / util.PT_PER_EM
    else val / util.PT_PER_EM
}

fn normalize_decimal(s, i, acc) {
    if (i >= len(s)) acc
    else
        (let ch = slice(s, i, i + 1),
         normalize_decimal(s, i + 1, acc ++ if (ch == ",") "." else ch))
}

fn find_number_start(s, i) {
    if (i >= len(s)) i
    else if (is_number_start_char(slice(s, i, i + 1))) i
    else find_number_start(s, i + 1)
}

fn find_number_end(s, i) {
    if (i >= len(s)) i
    else if (is_number_char(slice(s, i, i + 1))) find_number_end(s, i + 1)
    else i
}

fn find_unit_end(s, i) {
    if (i >= len(s)) i
    else if (is_unit_char(slice(s, i, i + 1))) find_unit_end(s, i + 1)
    else i
}

fn is_number_start_char(ch) {
    ch == "-" or ch == "+" or ch == "." or ch == "," or is_digit_char(ch)
}

fn is_number_char(ch) {
    ch == "-" or ch == "+" or ch == "." or ch == "," or is_digit_char(ch)
}

fn is_digit_char(ch) {
    ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
    ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9"
}

fn is_unit_char(ch) {
    ch == "a" or ch == "b" or ch == "c" or ch == "d" or ch == "e" or
    ch == "f" or ch == "g" or ch == "h" or ch == "i" or ch == "j" or
    ch == "k" or ch == "l" or ch == "m" or ch == "n" or ch == "o" or
    ch == "p" or ch == "q" or ch == "r" or ch == "s" or ch == "t" or
    ch == "u" or ch == "v" or ch == "w" or ch == "x" or ch == "y" or
    ch == "z"
}

fn fmt_rule_dim(v) {
    if (v == 0.0) "0"
    else if (abs(v) >= 100000.0) fmt_large_rule_dim(v)
    else util.fmt_num(v, 2) ++ "em"
}

fn fmt_rule_width(v) {
    if (abs(v) >= 100000.0) fmt_large_rule_dim(v)
    else util.fmt_num(v, 2) ++ "em"
}

fn fmt_rule_shift(v) {
    if (v == 0.0) "0"
    else if (abs(v) >= 100000.0) fmt_large_rule_dim(v)
    else util.fmt_num(v, 2) ++ "em"
}

fn round_rule_dim(v) => round(v * 100.0) / 100.0

fn fmt_large_rule_dim(v) {
    let scaled = int(round(v * 10.0))
    let s = string(abs(scaled))
    let body = if (len(s) <= 1) "0." ++ s
        else slice(s, 0, len(s) - 1) ++ "." ++ slice(s, len(s) - 1, len(s))
    (if (scaled < 0) "-" else "") ++ body ++ "em"
}

fn get_text(el) {
    if (len(el) > 0 and el[0] is string) string(el[0])
    else if (el.value != null) string(el.value)
    else ""
}

fn get_all_text(el) {
    get_all_text_at(el, 0, "")
}

fn get_all_text_at(el, i, acc) {
    if (i >= len(el)) if (acc != "") acc else get_text(el)
    else
        (let child_text = if (el[i] is string) string(el[i]) else get_all_text(el[i]),
         get_all_text_at(el, i + 1, acc ++ child_text))
}
