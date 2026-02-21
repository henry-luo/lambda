// math/atoms/enclose.ls — Enclosure rendering (\boxed, \fbox, \phantom, \rule)
// Handles box commands, phantom commands, and rule commands.

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css
import met: .lambda.package.math.metrics

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
    else render_bordered(content_box)
}

// bordered box (\boxed, \fbox, \bbox)
fn render_bordered(content_box) {
    {
        element: <span style: "border:1px solid;padding:0.1em"; content_box.element>,
        height: content_box.height + 0.15,
        depth: content_box.depth + 0.15,
        width: content_box.width + 0.3,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// overlap commands (\llap, \rlap, \clap)
fn render_lap(content_box, align) {
    let style = "display:inline-block;width:0;text-align:" ++ align
    {
        element: <span style: style; content_box.element>,
        height: content_box.height,
        depth: content_box.depth,
        width: 0.0,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
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
    {
        element: <span style: "visibility:hidden;display:inline-block"; content_box.element>,
        height: content_box.height,
        depth: content_box.depth,
        width: content_box.width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// hphantom — invisible, zero height/depth, keeps width
fn render_hphantom(content_box) {
    {
        element: <span style: "visibility:hidden;display:inline-block;height:0"; content_box.element>,
        height: 0.0,
        depth: 0.0,
        width: content_box.width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// vphantom — invisible, zero width, keeps height/depth
fn render_vphantom(content_box) {
    {
        element: <span style: "visibility:hidden;display:inline-block;width:0"; content_box.element>,
        height: content_box.height,
        depth: content_box.depth,
        width: 0.0,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// smash — visible, zero height and/or depth
fn render_smash(content_box, node) {
    let opts = if (node.options != null) string(node.options) else ""
    let zero_h = if (opts == "b") false else true
    let zero_d = if (opts == "t") false else true
    {
        element: <span style: "display:inline-block"; content_box.element>,
        height: if (zero_h) 0.0 else content_box.height,
        depth: if (zero_d) 0.0 else content_box.depth,
        width: content_box.width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Rule command (\rule)
// ============================================================

pub fn render_rule(node, context, render_fn) {
    let w = parse_dim(node.width, 1.0)
    let h = parse_dim(node.height, 0.4)
    let vert_raise = parse_dim(node.shift, 0.0)
    let rule_style = "width:" ++ string(w) ++ "em;height:" ++ string(h) ++ "em;background:currentColor"
    let raised_style = if (vert_raise != 0.0)
        rule_style ++ ";vertical-align:" ++ string(vert_raise) ++ "em"
    else rule_style
    box.box_styled(css.RULE, raised_style, h, 0.0, w, "ord")
}

// parse a dimension from a node attribute
fn parse_dim(attr, default_val) {
    if (attr == null) default_val
    else
        (let s = if (attr is string) attr
             else if (attr is element) get_text(attr)
             else string(attr),
         let v = float(s),
         if (v != null) v
         else default_val)
}

fn get_text(el) {
    if (len(el) > 0 and el[0] is string) string(el[0])
    else if (el.value != null) string(el.value)
    else ""
}
