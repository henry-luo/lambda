// math/atoms/style.ls — Style command rendering (\mathbf, \mathrm, \displaystyle, etc.)
// Handles font/style overrides that wrap their argument in a new rendering context.

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css

// ============================================================
// Style command rendering
// ============================================================

// render a style_command AST node
// node has: cmd, arg attributes
// render_fn: top-level render function for recursive calls
pub fn render(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""

    // map command to font name
    let font_name = get_font_name(cmd)

    // map command to style override (for \displaystyle, etc.)
    let style_override = get_style_override(cmd)

    // derive new context
    let new_ctx = if (font_name != null) ctx.derive(context, {font: font_name})
        else if (style_override != null) ctx.derive(context, {style: style_override})
        else context

    // render argument or children
    if (node.arg != null) render_fn(node.arg, new_ctx)
    else render_children(node, new_ctx, render_fn)
}

// ============================================================
// Font name mapping
// ============================================================

fn get_font_name(cmd) {
    if (cmd == "\\mathbf") "mathbf"
    else if (cmd == "\\mathrm") "cmr"
    else if (cmd == "\\mathit") "mathit"
    else if (cmd == "\\mathbb") "bb"
    else if (cmd == "\\mathcal") "cal"
    else if (cmd == "\\mathfrak") "frak"
    else if (cmd == "\\mathtt") "tt"
    else if (cmd == "\\mathscr") "script"
    else if (cmd == "\\mathsf") "sans"
    else if (cmd == "\\operatorname") "cmr"
    else null
}

// ============================================================
// Style override mapping
// ============================================================

fn get_style_override(cmd) {
    if (cmd == "\\displaystyle") "display"
    else if (cmd == "\\textstyle") "text"
    else if (cmd == "\\scriptstyle") "script"
    else if (cmd == "\\scriptscriptstyle") "scriptscript"
    else null
}

// ============================================================
// Helpers
// ============================================================

fn render_children(node, context, render_fn) {
    let n = len(node)
    if (n == 0) box.text_box("", null, "ord")
    else
        (let children = (for (i in 0 to (n - 1),
                             let child = node[i]
                             where child != null)
                         render_fn(child, context)),
         box.hbox(children))
}
