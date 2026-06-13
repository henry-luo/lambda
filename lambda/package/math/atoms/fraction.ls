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
        build_frac_nobar(numer_box, denom_box, frac_ctx, cmd)
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
fn build_frac_nobar(numer_box, denom_box, frac_ctx, cmd) {
    build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd)
}

// Rule 15d: with bar line
fn build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx) {
    let spec = frac_bar_spec(frac_ctx, numer_box, denom_box)
    let numer_elements = box.elements_of(numer_box)
    let denom_elements = box.elements_of(denom_box)
    let numer_style = frac_child_style(spec.numer_child_height, spec.child_font_pct)
    let denom_style = frac_child_style(spec.denom_child_height, spec.child_font_pct)
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

fn build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd) {
    let spec = frac_nobar_spec(frac_ctx, cmd)
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
    } else if (child_total >= 1.15) {
        {
            height: 1.57,
            depth: 1.06,
            render_height: 1.57,
            render_depth: 1.06,
            render_total: 2.64,
            depth_holder: 1.07,
            denom_top: -2.27,
            line_top: -3.23,
            numer_top: -3.73,
            numer_child_height: 1.18,
            denom_child_height: 1.18,
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

fn frac_nobar_spec(frac_ctx, cmd) {
    if (cmd == "\\tbinom" or frac_ctx.style == "script" or frac_ctx.style == "scriptscript") {
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
        width: frac_box.width + 0.24,
        type: "minner",
        italic: 0.0,
        skew: 0.0
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
        skew: 0.0
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
