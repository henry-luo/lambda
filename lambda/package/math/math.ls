// math/math.ls — Main entry point for the Lambda Math Package
// Provides math.render(ast) → HTML element tree

import render: .render
import ctx: .context
import css: .css
import box: .box
import opt: .optimize
import util: .util

// ============================================================
// Public API
// ============================================================

// render a math AST (from tree-sitter-latex-math) into an HTML element tree
// options: {display: bool, standalone: bool, color: string}
pub fn render_math(ast, options) {
    let is_display = if (options != null and options.display != null) options.display else false
    let is_standalone = if (options != null and options.standalone != null) options.standalone else false

    // create root rendering context
    let root_ctx = if (is_display) ctx.display_context() else ctx.text_context()
    let root_ctx2 = if (options != null and options.color != null)
        ctx.derive(root_ctx, {color: options.color})
    else root_ctx

    // render the AST into a box tree
    let raw_box = render.render_node(ast, root_ctx2)

    // coalesce adjacent spans with identical classes
    let result_box = opt.coalesce(raw_box)

    // wrap with struts and lm_latex class
    let h = if (result_box.render_height != null) result_box.render_height else result_box.height
    let d = if (result_box.render_depth != null) result_box.render_depth else result_box.depth
    let total = if (result_box.render_total != null) result_box.render_total else h + d
    let strut_bottom_style = "height:" ++ util.fmt_em(total) ++ ";vertical-align:" ++ util.fmt_em(0.0 - d)
    let latex_el = <span class: css.LATEX;
        <span class: css.STRUT, style: "height:" ++ util.fmt_em(h)>
        <span class: css.STRUT_BOTTOM, style: strut_bottom_style>
        result_box.element
    >

    // optionally wrap with stylesheet for standalone HTML
    if (is_standalone) css.wrap_standalone(latex_el)
    else latex_el
}

// convenience: render in display mode
pub fn render_display(ast) { render_math(ast, {display: true}) }

// convenience: render in inline (text) mode
pub fn render_inline(ast) { render_math(ast, {display: false}) }

// convenience: render standalone (with embedded CSS)
pub fn render_standalone(ast) { render_math(ast, {display: true, standalone: true}) }

// get the CSS stylesheet string
pub fn stylesheet() { css.get_stylesheet() }
