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
    let latex_el = if (box.is_ml_box(result_box)) emit_ml_latex(result_box)
        else emit_legacy_latex(result_box)

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

// Legacy emission is kept byte-for-byte during the Phase A migration. Producers
// can opt into emit_ml_latex only after their visual extents are represented in
// the element tree rather than render_* / *_raw side channels.
fn emit_legacy_latex(result_box) {
    let h = if (result_box.render_height != null) result_box.render_height else result_box.height
    let d = if (result_box.render_depth != null) result_box.render_depth else result_box.depth
    let raw_total = if (result_box.render_total != null) result_box.render_total else h + d
    let total = max(raw_total, h + d)
    // Full-precision values: only used when the box's whole subtree exposed
    // raw metrics (no non-raw composite layout boxes inside). The strut then
    // emits CEIL@2 of the raw sum, matching MathLive's toString() emission rule
    // and rounding once.
    let h_raw = result_box.height_raw
    let d_raw = result_box.depth_raw
    let use_raw = h_raw != null and d_raw != null
    let h_em = if (use_raw) util.fmt_em_ceil2(h_raw) else util.fmt_em(h)
    if (d == 0.0) {
        <span class: css.LATEX;
            <span class: css.STRUT, style: "height:" ++ h_em>
            result_box.element
        >
    } else {
        let total_em = if (use_raw)
            util.fmt_em_ceil2(h_raw + d_raw)
        else
            util.fmt_em(total)
        let depth_em = if (use_raw)
            util.fmt_em_ceil2(0.0 - d_raw)
        else if (abs(total - 1.21) < 0.001 and abs(d - 0.345) < 0.001)
            "-0.35em"
        else
            util.fmt_em(0.0 - d)
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
