// math/math.ls — Main entry point for the Lambda Math Package
// Provides math.render(ast) → HTML element tree

import render: .lambda.package.math.render
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css
import box: .lambda.package.math.box

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
    let result_box = render.render_node(ast, root_ctx2)

    // wrap with struts and ML__latex class
    let content_with_struts = box.make_struts(result_box)
    let latex_el = <span class: css.LATEX; content_with_struts>

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
