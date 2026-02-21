// math/atoms/color.ls — Color command rendering (\textcolor, \color, \colorbox)
// Handles foreground and background color wrapping.

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css

// ============================================================
// Color command rendering
// ============================================================

// render a color_command AST node
// node has: cmd, color, content attributes
// render_fn: top-level render function for recursive calls
pub fn render(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    let color = resolve_color(node)
    let new_ctx = ctx.derive(context, {color: color})

    if (cmd == "\\colorbox") render_colorbox(node, new_ctx, color, render_fn)
    else render_foreground(node, new_ctx, color, render_fn)
}

// ============================================================
// Foreground color (\textcolor, \color)
// ============================================================

fn render_foreground(node, context, color, render_fn) {
    if (node.content != null) {
        let content_box = render_fn(node.content, context)
        box.with_color(content_box, color)
    } else {
        box.text_box("", null, "ord")
    }
}

// ============================================================
// Background color (\colorbox)
// ============================================================

fn render_colorbox(node, context, bg_color, render_fn) {
    let content_box = if (node.content != null) render_fn(node.content, context)
        else box.text_box("", null, "ord")
    let style = "background-color:" ++ bg_color ++ ";padding:0.1em 0.2em"
    {
        element: <span style: style; content_box.element>,
        height: content_box.height + 0.1,
        depth: content_box.depth + 0.1,
        width: content_box.width + 0.4,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Color resolution
// ============================================================

// extract and resolve color from node
fn resolve_color(node) {
    let raw = if (node.color != null) get_color_text(node.color) else "black"
    resolve_named_color(raw)
}

// get text from color node (may be element or string)
fn get_color_text(color_node) {
    if (color_node is string) string(color_node)
    else if (color_node is symbol) string(color_node)
    else if (color_node is element) get_element_text(color_node)
    else string(color_node)
}

fn get_element_text(el) {
    let n = len(el)
    if (n == 0) ""
    else concat_children(el, 0, n, "")
}

fn concat_children(el, i, n, acc) {
    if (i >= n) acc
    else
        (let child = el[i],
         let txt = if (child is string) string(child)
            else if (child is symbol) string(child)
            else if (child is element) string(child)
            else "",
         concat_children(el, i + 1, n, acc ++ txt))
}

// map named LaTeX colors to CSS colors
fn resolve_named_color(raw) {
    if (len(raw) > 0 and slice(raw, 0, 1) == "#") raw
    else resolve_by_name(raw)
}

fn resolve_by_name(name) {
    if (name == "red") "#d32f2f"
    else if (name == "blue") "#1976d2"
    else if (name == "green") "#388e3c"
    else if (name == "black") "#000000"
    else if (name == "white") "#ffffff"
    else if (name == "gray" or name == "grey") "#9e9e9e"
    else if (name == "yellow") "#fbc02d"
    else if (name == "orange") "#f57c00"
    else if (name == "purple") "#7b1fa2"
    else if (name == "cyan") "#00bcd4"
    else if (name == "magenta") "#e91e63"
    else if (name == "brown") "#795548"
    else if (name == "lime") "#cddc39"
    else if (name == "olive") "#827717"
    else if (name == "pink") "#e91e63"
    else if (name == "teal") "#009688"
    else if (name == "violet") "#9c27b0"
    else if (name == "darkgray") "#616161"
    else if (name == "lightgray") "#bdbdbd"
    else name
}
