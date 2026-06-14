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
    let has_bar = not is_nobar_cmd(cmd)

    // determine style override
    let frac_style = match cmd {
        case "\\dfrac": "display"
        case "\\tfrac": "text"
        case "\\dbinom": "display"
        case "\\tbinom": "text"
        default: null
    }

    // determine delimiters for binomial-like
    let left_delim = match cmd {
        case "\\binom": "("
        case "\\dbinom": "("
        case "\\tbinom": "("
        case "\\choose": "("
        case "\\brack": "["
        case "\\brace": "{"
        default: null
    }
    let right_delim = match cmd {
        case "\\binom": ")"
        case "\\dbinom": ")"
        case "\\tbinom": ")"
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
        build_frac_nobar(numer_box, denom_box, frac_ctx, cmd,
            is_unbraced_numeric_nobar(node))
    else
        build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx)

    // wrap with delimiters if present
    if (left_delim != null or right_delim != null)
        wrap_delimited_fraction(frac_box, left_delim, right_delim)
    else
        wrap_default_fraction(frac_box)
}

pub fn render_boxes(numer_box, denom_box, context) {
    let frac_ctx = context
    let frac_box = build_frac_bar(numer_box, denom_box, 0.0, 0.0, 0.0,
        met.at(met.defaultRuleThickness, met.style_index(frac_ctx.style)), frac_ctx)
    wrap_default_fraction(frac_box)
}

// Rule 15c: no bar line (binomial)
fn build_frac_nobar(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric) {
    build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric)
}

// Rule 15d: with bar line
fn build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx) {
    let spec = frac_bar_spec(frac_ctx, numer_box, denom_box)
    let numer_elements = box.elements_of(numer_box)
    let denom_elements = box.elements_of(denom_box)
    let numer_style = frac_child_style(spec.numer_child_height, spec.child_font_pct)
    let denom_style = frac_child_style(spec.denom_child_height, spec.child_font_pct)
    let line_style = "height:" ++ util.fmt_em(spec.rule_height) ++ ";display:inline-block"
    let pstrut_style = "height:" ++ util.fmt_em(if (spec.pstrut != null) spec.pstrut else 3.0)
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(spec.height);
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.denom_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: denom_style;
                        for (el in denom_elements) el
                    >
                >
                <span style: "top:" ++ util.fmt_em(spec.line_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span class: css.FRAC_LINE, style: line_style>
                >
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.numer_top);
                    <span class: css.PSTRUT, style: pstrut_style>
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
        left_right_render_depth: spec.left_right_render_depth,
        left_right_render_total: spec.left_right_render_total,
        width: max(numer_box.width, denom_box.width),
        type: "mord",
        italic: 0.0,
        skew: 0.0,
        is_fraction: true
    }
}

fn build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric) {
    let spec = frac_nobar_spec(frac_ctx, cmd, unbraced_numeric)
    let numer_elements = box.elements_of(numer_box)
    let denom_elements = box.elements_of(denom_box)
    let numer_style = frac_child_style(spec.numer_child_height, spec.child_font_pct)
    let denom_style = frac_child_style(spec.denom_child_height, spec.child_font_pct)
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(spec.height);
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.numer_top);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: numer_style;
                        for (el in numer_elements) el
                    >
                >
                <span class: css.CENTER, style: "top:" ++ util.fmt_em(spec.denom_top);
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: denom_style;
                        for (el in denom_elements) el
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
        skew: 0.0,
        no_bar: true,
        is_fraction: true,
        delim_level: spec.delim_level,
        delim_height: spec.delim_height,
        delim_depth: spec.delim_depth
    }
}

fn frac_bar_spec(frac_ctx, numer_box, denom_box) {
    let numer_total = render_total_of(numer_box)
    let denom_total = render_total_of(denom_box)
    let child_total = max(numer_total, denom_total)
    if (frac_ctx.style == "script" and frac_ctx.script_container) {
        {
            height: 0.97,
            depth: 0.54,
            render_height: 0.97,
            render_depth: 0.54,
            render_total: 1.51,
            depth_holder: 0.54,
            denom_top: -2.46,
            line_top: -3.22,
            numer_top: -3.5,
            numer_child_height: 0.47,
            denom_child_height: 0.47,
            child_font_pct: "71.43%",
            rule_height: 0.05
        }
    } else if ((frac_ctx.style == "script" or frac_ctx.style == "scriptscript") and
               frac_ctx.colorbox_content == true and numer_box.is_fraction == true and
               denom_total < 0.8) {
        {
            height: 1.36,
            depth: 0.35,
            render_height: 1.36,
            render_depth: 0.35,
            render_total: 1.71,
            depth_holder: 0.35,
            denom_top: -2.65,
            line_top: -3.23,
            numer_top: -3.68,
            numer_child_height: 1.05,
            denom_child_height: 0.31,
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if (frac_ctx.style == "scriptscript") {
        {
            height: 0.97,
            depth: 0.54,
            render_height: 0.97,
            render_depth: 0.54,
            render_total: 1.51,
            depth_holder: 0.54,
            denom_top: -2.46,
            line_top: -3.22,
            numer_top: -3.5,
            numer_child_height: 0.47,
            denom_child_height: 0.47,
            child_font_pct: "71.43%",
            rule_height: 0.05
        }
    } else if (frac_ctx.style == "script" and (numer_total >= 0.95 or numer_box.height >= 0.9) and denom_total < 0.75) {
        {
            height: 1.36,
            depth: 0.35,
            render_height: 1.36,
            render_depth: 0.35,
            render_total: 1.71,
            depth_holder: 0.35,
            denom_top: -2.65,
            line_top: -3.23,
            numer_top: -3.68,
            numer_child_height: 1.05,
            denom_child_height: 0.31,
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if ((frac_ctx.style == "script" or frac_ctx.style == "scriptscript") and
               script_fraction_has_descender(numer_box) and denom_total < 0.75) {
        let numer_child_height = script_fraction_descender_child_height(numer_box)
        {
            height: numer_child_height + 0.31,
            depth: 0.35,
            render_height: numer_child_height + 0.31,
            render_depth: 0.35,
            render_total: numer_child_height + 0.66,
            depth_holder: 0.35,
            denom_top: -2.65,
            line_top: -3.23,
            numer_top: -3.44,
            numer_child_height: numer_child_height,
            denom_child_height: 0.46,
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if ((frac_ctx.style == "script" or frac_ctx.style == "scriptscript") and denom_total < 0.7) {
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
            numer_child_height: script_frac_child_height(numer_box, 0.51),
            denom_child_height: 0.46,
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if ((frac_ctx.style == "script" or frac_ctx.style == "scriptscript") and child_total >= 0.7) {
        {
            height: 0.84,
            depth: 0.41,
            render_height: 0.84,
            render_depth: 0.41,
            render_total: 1.24,
            depth_holder: 0.41,
            denom_top: -2.65,
            line_top: -3.23,
            numer_top: -3.38,
            numer_child_height: script_frac_child_height(numer_box, 0.51),
            denom_child_height: script_frac_child_height(denom_box, 0.55),
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if (frac_ctx.style == "script" or frac_ctx.style == "scriptscript") {
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
            numer_child_height: 0.46,
            denom_child_height: 0.46,
            child_font_pct: "70%",
            rule_height: 0.04
        }
    } else if (frac_ctx.colorbox_content == true and numer_total >= 1.15 and denom_total >= 1.2) {
        {
            height: 2.09,
            depth: 1.12,
            render_height: 2.09,
            render_depth: 1.12,
            render_total: 3.22,
            depth_holder: 1.13,
            denom_top: -2.62,
            line_top: -3.58,
            numer_top: -4.08,
            numer_child_height: colorbox_fraction_child_height(numer_box, numer_total),
            denom_child_height: round(denom_total * 100.0) / 100.0,
            child_font_pct: null,
            rule_height: 0.04,
            pstrut: 3.36
        }
    } else if (frac_ctx.colorbox_content == true and numer_total >= 1.15 and denom_total < 0.95) {
        {
            height: 2.09,
            depth: 0.685,
            render_height: 2.09,
            render_depth: 0.68,
            render_total: 2.78,
            depth_holder: 0.69,
            denom_top: -2.66,
            line_top: -3.58,
            numer_top: -4.08,
            numer_child_height: colorbox_fraction_child_height(numer_box, numer_total),
            denom_child_height: colorbox_simple_child_height(denom_box),
            child_font_pct: null,
            rule_height: 0.04,
            pstrut: 3.36
        }
    } else if (frac_ctx.colorbox_content == true and numer_total < 0.95 and
               denom_box.is_fraction == true and denom_total >= 1.15 and denom_total < 1.2) {
        {
            height: 1.15,
            depth: 1.06,
            render_height: 1.15,
            render_depth: 1.06,
            render_total: 2.22,
            depth_holder: 1.07,
            denom_top: -2.27,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: 0.65,
            denom_child_height: 1.18,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (numer_total >= 1.5 and denom_total >= 1.2) {
        {
            height: 2.09,
            depth: 1.12,
            render_height: 2.09,
            render_depth: 1.12,
            render_total: 3.22,
            depth_holder: 1.13,
            denom_top: -2.62,
            line_top: -3.58,
            numer_top: -4.08,
            numer_child_height: 1.7,
            denom_child_height: round(denom_total * 100.0) / 100.0,
            child_font_pct: null,
            rule_height: 0.04,
            pstrut: 3.36
        }
    } else if (numer_total >= 1.5 and denom_total < 0.95) {
        {
            height: 2.09,
            depth: 0.685,
            render_height: 2.09,
            render_depth: 0.68,
            render_total: 2.78,
            depth_holder: 0.69,
            denom_top: -2.66,
            line_top: -3.58,
            numer_top: -4.08,
            numer_child_height: 1.7,
            denom_child_height: if (denom_box.height < 0.65) denom_box.height else 0.65,
            child_font_pct: null,
            rule_height: 0.04,
            pstrut: 3.36
        }
    } else if (numer_box.is_script_radical == true and numer_total >= 0.95 and denom_total < 0.95) {
        {
            height: 1.39,
            depth: 0.685,
            render_height: 1.39,
            render_depth: 0.68,
            render_total: 2.08,
            left_right_render_depth: 0.686,
            left_right_render_total: 2.076,
            depth_holder: 0.69,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.55,
            numer_child_height: 1.0,
            denom_child_height: 0.65,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (numer_total >= 0.95 and denom_total < 0.95) {
        {
            height: 1.39,
            depth: 0.685,
            render_height: 1.39,
            render_depth: 0.68,
            render_total: 2.08,
            depth_holder: 0.69,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.64,
            numer_child_height: 1.0,
            denom_child_height: 0.7,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (denom_total >= 1.2 and numer_total < 0.95) {
        {
            height: 1.15,
            depth: 1.12,
            render_height: 1.15,
            render_depth: 1.12,
            render_total: 2.28,
            depth_holder: 1.13,
            denom_top: -2.27,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: 0.65,
            denom_child_height: round(denom_total * 100.0) / 100.0,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (denom_total >= 0.95 and numer_total < 0.95) {
        {
            height: 1.15,
            depth: 0.94,
            render_height: 1.15,
            render_depth: 0.94,
            render_total: 2.09,
            depth_holder: 0.94,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: 0.65,
            denom_child_height: round(denom_total * 100.0) / 100.0,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (child_total >= 1.15) {
        {
            height: 1.57,
            depth: 1.06,
            render_height: 1.57,
            render_depth: 1.06,
            render_total: 2.64,
            left_right_render_depth: 1.069108,
            left_right_render_total: 2.638216,
            depth_holder: 1.07,
            denom_top: -2.27,
            line_top: -3.23,
            numer_top: -3.73,
            numer_child_height: 1.18,
            denom_child_height: 1.18,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (denom_total >= 0.75 and numer_total < 0.75) {
        {
            height: 1.15,
            depth: 0.77,
            render_height: 1.15,
            render_depth: 0.77,
            render_total: 1.92,
            depth_holder: 0.77,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: 0.65,
            denom_child_height: 0.73,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (child_total >= 0.95) {
        {
            height: 1.4,
            depth: 0.94,
            render_height: 1.4,
            render_depth: 0.94,
            render_total: 2.34,
            depth_holder: 0.94,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.58,
            numer_child_height: max(1.0, round(render_total_of(numer_box) * 100.0) / 100.0),
            denom_child_height: max(1.0, round(render_total_of(denom_box) * 100.0) / 100.0),
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (denom_total < 0.75 and numer_total >= 0.7) {
        let numer_child_height = text_fraction_numer_child_height(numer_box)
        let has_descender = script_fraction_has_descender(numer_box)
        {
            height: if (has_descender) numer_child_height + 0.39 else 1.15,
            depth: 0.685,
            render_height: if (has_descender) numer_child_height + 0.39 else 1.15,
            render_depth: 0.68,
            render_total: if (has_descender) numer_child_height + 1.08 else 1.84,
            depth_holder: 0.69,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: if (has_descender) 0.0 - 3.58 else 0.0 - 3.5,
            numer_child_height: numer_child_height,
            denom_child_height: 0.65,
            child_font_pct: null,
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
            numer_child_height: 0.65,
            denom_child_height: if (denom_box.height < 0.65) denom_box.height else 0.65,
            child_font_pct: null,
            rule_height: 0.04
        }
    }
}

fn script_frac_child_height(child_box, fallback) {
    let total = render_total_of(child_box)
    if (total >= 0.7) fallback else 0.46
}

fn script_fraction_descender_child_height(child_box) {
    if (script_fraction_has_digit(child_box)) 0.59 else 0.55
}

fn text_fraction_numer_child_height(child_box) {
    if (script_fraction_has_descender(child_box)) {
        if (script_fraction_has_digit(child_box)) 0.84 else 0.78
    } else 0.73
}

fn script_fraction_has_descender(child_box) {
    items_have_descender(box.elements_of(child_box), 0)
}

fn script_fraction_has_digit(child_box) {
    items_have_digit(box.elements_of(child_box), 0)
}

fn items_have_descender(items, i) {
    if (i >= len(items)) false
    else item_has_descender(items[i]) or items_have_descender(items, i + 1)
}

fn item_has_descender(item) {
    if (item is string) text_has_descender(string(item), 0)
    else if (item is element) element_has_descender(item, 0)
    else false
}

fn element_has_descender(el, i) {
    if (i >= len(el)) false
    else item_has_descender(el[i]) or element_has_descender(el, i + 1)
}

fn text_has_descender(text, i) {
    if (i >= len(text)) false
    else {
        let ch = slice(text, i, i + 1)
        ch == "g" or ch == "j" or ch == "p" or ch == "q" or ch == "y" or
            text_has_descender(text, i + 1)
    }
}

fn items_have_digit(items, i) {
    if (i >= len(items)) false
    else item_has_digit(items[i]) or items_have_digit(items, i + 1)
}

fn item_has_digit(item) {
    if (item is string) text_has_digit(string(item), 0)
    else if (item is element) element_has_digit(item, 0)
    else false
}

fn element_has_digit(el, i) {
    if (i >= len(el)) false
    else item_has_digit(el[i]) or element_has_digit(el, i + 1)
}

fn text_has_digit(text, i) {
    if (i >= len(text)) false
    else {
        let ch = slice(text, i, i + 1)
        is_digit_char(ch) or text_has_digit(text, i + 1)
    }
}

fn is_digit_char(ch) {
    ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
    ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9"
}

fn colorbox_fraction_child_height(child_box, total) {
    if (child_box.is_fraction == true and total >= 1.7 and total < 1.72)
        1.7
    else
        round(total * 100.0) / 100.0
}

fn colorbox_simple_child_height(child_box) {
    if (is_single_mathit_letter_box(child_box)) 0.44
    else if (child_box.height < 0.65) child_box.height
    else 0.65
}

fn is_single_mathit_letter_box(child_box) {
    let els = box.elements_of(child_box)
    len(els) == 1 and els[0] is element and els[0].class == css.MATHIT and
    len(els[0]) == 1 and els[0][0] is string and len(string(els[0][0])) == 1
}

fn frac_nobar_spec(frac_ctx, cmd, unbraced_numeric) {
    if (unbraced_numeric) {
        {
            height: 1.15,
            depth: 0.69,
            render_height: 1.45,
            render_depth: 0.95,
            render_total: 2.41,
            depth_holder: 0.69,
            numer_top: -3.5,
            denom_top: -2.31,
            numer_child_height: 0.65,
            denom_child_height: 0.65,
            child_font_pct: null,
            delim_level: 3,
            delim_height: 1.45,
            delim_depth: 0.95
        }
    } else if (cmd == "\\tbinom" or frac_ctx.style == "script" or frac_ctx.style == "scriptscript") {
        {
            height: 0.78,
            depth: 0.35,
            render_height: 0.85,
            render_depth: 0.35,
            render_total: 1.21,
            depth_holder: 0.35,
            numer_top: -3.47,
            denom_top: -2.65,
            numer_child_height: 0.31,
            denom_child_height: 0.49,
            child_font_pct: "70%",
            delim_level: 1,
            delim_height: 0.85,
            delim_depth: 0.35
        }
    } else {
        {
            height: 0.94,
            depth: 0.69,
            render_height: 1.45,
            render_depth: 0.95,
            render_total: 2.41,
            depth_holder: 0.69,
            numer_top: -3.5,
            denom_top: -2.31,
            numer_child_height: 0.44,
            denom_child_height: 0.7,
            child_font_pct: null,
            delim_level: 3,
            delim_height: 1.45,
            delim_depth: 0.95
        }
    }
}

fn is_unbraced_numeric_nobar(node) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""
    is_nobar_cmd(cmd) and is_plain_number_node(node.numer) and
    is_plain_number_node(node.denom)
}

fn is_plain_number_node(node) {
    if (not (node is string)) false
    else is_digit_string(string(node), 0)
}

fn is_digit_string(text, i) {
    if (len(text) == 0) false
    else if (i >= len(text)) true
    else
        (let ch = slice(text, i, i + 1),
         if (ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
             ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9")
            is_digit_string(text, i + 1)
         else false)
}

fn render_total_of(bx) {
    if (bx.render_total != null) bx.render_total
    else (if (bx.render_height != null) bx.render_height else bx.height) +
         (if (bx.render_depth != null) bx.render_depth else bx.depth)
}

fn frac_child_style(child_height, font_pct) {
    let suffix = if (font_pct != null) ";font-size: " ++ font_pct else ""
    "height:" ++ util.fmt_em(child_height) ++ ";display:inline-block" ++ suffix
}

fn is_nobar_cmd(cmd) {
    cmd == "\\binom" or cmd == "\\dbinom" or cmd == "\\tbinom" or
    cmd == "\\choose" or cmd == "\\brack" or cmd == "\\brace"
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
        left_right_render_depth: frac_box.left_right_render_depth,
        left_right_render_total: frac_box.left_right_render_total,
        width: frac_box.width + 0.24,
        type: "minner",
        italic: 0.0,
        skew: 0.0,
        is_fraction: true
    }
}

fn wrap_delimited_fraction(frac_box, left_delim, right_delim) {
    let left_box = if (left_delim != null)
        delimiter_box(left_delim, frac_box, "mopen") else null
    let right_box = if (right_delim != null)
        delimiter_box(right_delim, frac_box, "mclose") else null
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
        type: "minner",
        italic: 0.0,
        skew: 0.0,
        is_fraction: true
    }
}

fn delimiter_box(delim, frac_box, atom_type) {
    let cls = if (frac_box.delim_level == 1) css.DELIM_SIZE1
        else if (frac_box.delim_level == 2) css.DELIM_SIZE2
        else if (frac_box.delim_level == 3) css.DELIM_SIZE3
        else if (frac_box.delim_level == 4) css.DELIM_SIZE4
        else css.SMALL_DELIM
    let h = if (frac_box.delim_height != null) frac_box.delim_height else 0.75
    let d = if (frac_box.delim_depth != null) frac_box.delim_depth else 0.25
    {
        element: <span class: cls; delim>,
        height: h,
        depth: d,
        render_height: h,
        render_depth: d,
        render_total: h + d,
        width: 0.4,
        type: atom_type,
        italic: 0.0,
        skew: 0.0
    }
}
