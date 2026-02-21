// math/atoms/fraction.ls — Fraction rendering (\frac, \dfrac, \tfrac, \binom)
// Implements TeXBook Rules 15b-15e from Appendix G

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css
import met: .lambda.package.math.metrics

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
        build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness)

    // wrap with delimiters if present
    let parts = [
        (if (left_delim != null)
            box.text_box(left_delim, css.SMALL_DELIM, "mopen")
        else null),
        frac_box,
        (if (right_delim != null)
            box.text_box(right_delim, css.SMALL_DELIM, "mclose")
        else null)
    ]

    // filter nulls and build the mfrac wrapper
    let content_boxes = (for (p in parts where p != null) p)
    let combined = box.hbox(content_boxes)
    box.with_class(combined, css.MFRAC)
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
fn build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness) {
    let frac_line = box.box_cls(css.FRAC_LINE,
        rule_thickness / 2.0, rule_thickness / 2.0,
        max(numer_box.width, denom_box.width), "ord")

    let numer_line = met.AXIS_HEIGHT + rule_thickness / 2.0
    let denom_line = met.AXIS_HEIGHT - rule_thickness / 2.0

    let final_ns = max(ns, cl + numer_box.depth + numer_line)
    let final_ds = max(ds, cl + denom_box.height - denom_line)

    box.vbox([
        {box: denom_box, shift: final_ds},
        {box: frac_line, shift: 0.0 - denom_line},
        {box: numer_box, shift: 0.0 - final_ns}
    ])
}
