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
    let latex_el = emit_ml_latex(result_box)

    // optionally wrap with stylesheet for standalone HTML
    if (is_standalone) css.wrap_standalone(latex_el, options)
    else latex_el
}

// MathLive-model root emission: public height/depth are already full precision,
// so this path has exactly one CEIL@2 stringification site for root struts.
fn emit_ml_latex(result_box) {
    let h = result_box.height
    let d = result_box.depth
    let h_em = util.fmt_ml_em(h)
    if (d == 0.0) {
        <span class: css.LATEX;
            <span class: css.STRUT, style: "height:" ++ h_em>
            result_box.element
        >
    } else {
        let total_em = util.fmt_ml_em(h + d)
        let depth_em = util.fmt_ml_em(0.0 - d)
        let strut_bottom_style = "height:" ++ total_em ++ ";vertical-align:" ++ depth_em
        <span class: css.LATEX;
            <span class: css.STRUT, style: "height:" ++ h_em>
            <span class: css.STRUT_BOTTOM, style: strut_bottom_style>
            result_box.element
        >
    }
}

// convenience: render in display mode
pub fn render_display(ast) { render_math(ast, {display: true}) }

// convenience: render in inline (text) mode
pub fn render_inline(ast) { render_math(ast, {display: false}) }

// convenience: render standalone (with embedded CSS)
pub fn render_standalone(ast) { render_math(ast, {display: true, standalone: true}) }

// get the CSS stylesheet string
pub fn stylesheet(options = null) { css.get_stylesheet(options) }
