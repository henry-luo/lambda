// math/atoms/fraction.ls — Fraction rendering (\frac, \dfrac, \tfrac, \binom)
// Implements TeXBook Rules 15b-15e from Appendix G

import box: lambda.package.math.box
import ctx: lambda.package.math.context
import css: lambda.package.math.css
import met: lambda.package.math.metrics
import util: lambda.package.math.util

// ============================================================
// Fraction rendering
// ============================================================

// render a fraction AST node
// node has: cmd, numer, denom attributes
// render_fn: the top-level render function for recursive calls
pub fn render(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else "\\frac"

    // determine if this has a bar line
    let has_bar = not (cmd == "\\binom" or cmd == "\\choose"
                       or cmd == "\\brack" or cmd == "\\brace")

    // determine style override
    let frac_style = match cmd {
        case "\\dfrac": "display"
        case "\\tfrac": "text"
        default: null
    }

    // determine delimiters for binomial-like
    let left_delim = match cmd {
        case "\\binom": "("
        case "\\choose": "("
        case "\\brack": "["
        case "\\brace": "{"
        default: null
    }
    let right_delim = match cmd {
        case "\\binom": ")"
        case "\\choose": ")"
        case "\\brack": "]"
        case "\\brace": "}"
        default: null
    }

    // create fraction context (may have style override)
    let frac_ctx = if (frac_style != null)
        ctx.derive(context, {style: frac_style})
    else context

    // create numerator and denominator contexts
    let num_ctx = ctx.numer_context(frac_ctx)
    let den_ctx = ctx.denom_context(frac_ctx)

    // render numerator and denominator
    let numer_box = if (node.numer != null) render_fn(node.numer, num_ctx)
        else box.text_box("", null, "ord")
    let denom_box = if (node.denom != null) render_fn(node.denom, den_ctx)
        else box.text_box("", null, "ord")

    // get metrics for current context
    let si = met.style_index(frac_ctx.style)
    let rule_thickness = if (has_bar) met.at(met.defaultRuleThickness, si) else 0.0

    // Rule 15b: compute shifts using expression-context patterns
    let result_shifts = if (ctx.is_display(frac_ctx))
        (let ns = met.at(met.num1, si),
         let cl = if (rule_thickness > 0.0) 3.0 * rule_thickness
                  else 7.0 * rule_thickness,
         let ds = met.at(met.denom1, si),
         {numer_shift: ns, clearance: cl, denom_shift: ds})
    else if (rule_thickness > 0.0)
        {numer_shift: met.at(met.num2, si), clearance: rule_thickness, denom_shift: met.at(met.denom2, si)}
    else
        {numer_shift: met.at(met.num3, si), clearance: 3.0 * met.at(met.defaultRuleThickness, si), denom_shift: met.at(met.denom2, si)}

    let ns = result_shifts.numer_shift
    let cl = result_shifts.clearance
    let ds = result_shifts.denom_shift

    // build the vbox
    let frac_box = if (rule_thickness <= 0.0)
        build_frac_nobar(numer_box, denom_box, ns, cl, ds)
    else
        build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx)

    // wrap with delimiters if present
    if (left_delim != null or right_delim != null)
        wrap_delimited_fraction(frac_box, left_delim, right_delim)
    else
        wrap_default_fraction(frac_box)
}

// Rule 15c: no bar line (binomial)
fn build_frac_nobar(numer_box, denom_box, ns, cl, ds) {
    let candidate_cl = ns - numer_box.depth - (denom_box.height - ds)
    let adj = if (candidate_cl < cl) (cl - candidate_cl) / 2.0 else 0.0
    let final_ns = ns + adj
    let final_ds = ds + adj

    box.vbox([
        {box: numer_box, shift: 0.0 - final_ns},
        {box: denom_box, shift: final_ds}
    ])
}

// Rule 15d: with bar line
fn build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx) {
    let spec = frac_bar_spec(frac_ctx)
    let numer_elements = box.elements_of(numer_box)
    let denom_elements = box.elements_of(denom_box)
    let numer_style = frac_child_style(spec.child_height, spec.child_scale)
    let denom_style = frac_child_style(spec.child_height, spec.child_scale)
    let line_style = "height:" ++ util.fmt_em(spec.rule_height) ++ ";display:inline-block"
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(spec.height);
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.denom_top);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: denom_style;
                        for (el in denom_elements) el
                    >
                >
                <span style: "top:" ++ util.fmt_em(spec.line_top);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span class: css.FRAC_LINE, style: line_style>
                >
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.numer_top);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: numer_style;
                        for (el in numer_elements) el
                    >
                >
            >
            <span class: css.VLIST_S; "\u200B">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(spec.depth_holder)>
        >
    >
    {
        element: el,
        height: spec.height,
        depth: spec.depth,
        render_height: spec.render_height,
        render_depth: spec.render_depth,
        render_total: spec.render_total,
        width: max(numer_box.width, denom_box.width),
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}

fn frac_bar_spec(frac_ctx) {
    if (frac_ctx.style == "script" or frac_ctx.style == "scriptscript") {
        {
            height: 0.84,
            depth: 0.35,
            render_height: 0.84,
            render_depth: 0.35,
            render_total: 1.19,
            depth_holder: 0.35,
            denom_top: -2.65,
            line_top: -3.23,
            numer_top: -3.38,
            child_height: 0.46,
            child_scale: true,
            rule_height: 0.04
        }
    } else {
        {
            height: 1.15,
            depth: 0.685,
            render_height: 1.15,
            render_depth: 0.68,
            render_total: 1.84,
            depth_holder: 0.69,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            child_height: 0.65,
            child_scale: false,
            rule_height: 0.04
        }
    }
}

fn frac_child_style(child_height, scaled) {
    let suffix = if (scaled) ";font-size: 70%" else ""
    "height:" ++ util.fmt_em(child_height) ++ ";display:inline-block" ++ suffix
}

fn null_delim_el(cls) {
    <span class: css.classes([css.NULLDELIMITER, cls]), style: "width:0.12em">
}

fn wrap_default_fraction(frac_box) {
    {
        element: <span class: css.MFRAC;
            null_delim_el(css.OPEN)
            frac_box.element
            null_delim_el(css.CLOSE)
        >,
        height: frac_box.height,
        depth: frac_box.depth,
        render_height: frac_box.render_height,
        render_depth: frac_box.render_depth,
        render_total: frac_box.render_total,
        width: frac_box.width + 0.24,
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}

fn wrap_delimited_fraction(frac_box, left_delim, right_delim) {
    let left_box = if (left_delim != null)
        box.text_box(left_delim, css.SMALL_DELIM, "mopen") else null
    let right_box = if (right_delim != null)
        box.text_box(right_delim, css.SMALL_DELIM, "mclose") else null
    let content_boxes = (for (p in [left_box, frac_box, right_box] where p != null) p)
    let elements = box.child_elements(content_boxes)
    let combined = box.hbox(content_boxes)
    {
        element: <span class: css.MFRAC;
            for (el in elements) el
        >,
        height: combined.height,
        depth: combined.depth,
        render_height: combined.render_height,
        render_depth: combined.render_depth,
        render_total: combined.render_total,
        width: combined.width,
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}
