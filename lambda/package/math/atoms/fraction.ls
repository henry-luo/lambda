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

    // MathLive geometry style (display/text/script/scriptscript) for this
    // fraction. A directly-nested fraction inherits it via frac_gstyle; a
    // top-level / subscript-reached fraction derives it from the cmd + style
    // (the corpus renders inline at DISPLAY geometry, so text -> display).
    let is_cfrac = cmd == "\\cfrac"
    let is_fraction_child = frac_ctx.frac_gstyle != null
    let gstyle = if (is_cfrac) "display"
        else if (frac_ctx.frac_gstyle != null) frac_ctx.frac_gstyle
        else if (cmd == "\\tfrac") "text"
        // A fraction reached through a sub/superscript (script_container) keeps
        // its own geometry style (the display->script cascade already happened
        // at the script boundary). A fraction in a script-style CELL
        // (cases/smallmatrix, script_container false) renders one geometry step
        // "up" — display-rooted corpus, so a script-cell fraction uses text
        // geometry (children at script, font-size 70%), matching MathLive.
        else if (frac_ctx.style == "script")
            (if (frac_ctx.script_container == true) "script" else "text")
        else if (frac_ctx.style == "scriptscript")
            (if (frac_ctx.script_container == true) "scriptscript" else "script")
        // Matrix cells are textstyle roots, not inline roots; promoting their
        // ordinary \frac geometry back to display makes rows and delimiters too tall.
        else if (frac_ctx.matrix_cell == true) "text"
        else "display"
    // numerator/denominator geometry style (one step smaller), threaded so a
    // fraction nested directly in the numer/denom renders correctly.
    let child_gstyle = if (is_cfrac) "display"
        // Continued fractions keep nested fraction geometry in display style;
        // inheriting the ordinary fraction-child step wrongly scripts them.
        else match gstyle {
        case "display": "text"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }

    // create numerator and denominator contexts (carry the child geometry)
    // Continued-fraction children are display-rooted; otherwise the third
    // denominator reaches scriptscript style and loses binary-operator spaces.
    let child_frac_ctx = if (is_cfrac) ctx.derive(frac_ctx, {style: "display"}) else frac_ctx
    let num_ctx = ctx.derive(ctx.numer_context(child_frac_ctx), {frac_gstyle: child_gstyle, compact_prime: true})
    let den_ctx = ctx.derive(ctx.denom_context(child_frac_ctx), {frac_gstyle: child_gstyle, compact_prime: true})

    // render numerator and denominator
    let numer_box = if (node.numer != null) render_fn(node.numer, num_ctx)
        else box.text_box("", null, "ord")
    let denom_box = if (node.denom != null) render_fn(node.denom, den_ctx)
        else box.text_box("", null, "ord")

    // get metrics for current context
    let si = met.style_index(frac_ctx.style)
    let rule_thickness = if (has_bar) met.at(met.defaultRuleThickness, si) else 0.0

    // build the vbox (bar fractions are fully metric-driven via frac_bar_geom;
    // the TeXBook Rule 15b shift constants now live inside frac_bar_geom).
    let frac_box = if (rule_thickness <= 0.0)
        build_frac_nobar(numer_box, denom_box, frac_ctx, cmd,
            is_unbraced_numeric_nobar(node))
    else
        build_frac_bar(numer_box, denom_box, frac_ctx, gstyle, is_fraction_child)

    // wrap with delimiters if present
    if (left_delim != null or right_delim != null)
        wrap_delimited_fraction(frac_box, left_delim, right_delim)
    else if (cmd == "\\cfrac")
        // \cfrac (continued fraction) emits only the opening nulldelimiter —
        // matches MathLive's left-aligned continued-fraction layout.
        wrap_cfrac_fraction(frac_box)
    else
        wrap_default_fraction(frac_box)
}

fn wrap_cfrac_fraction(frac_box) {
    let el = <span class: css.MFRAC;
            null_delim_el(css.OPEN)
            frac_box.element
        >
    ml_fraction_wrapper(el, frac_box, frac_box.width + 0.12)
}

fn ml_fraction_wrapper(el, frac_box, width) => {
        element: el,
        height: frac_box.height,
        depth: frac_box.depth,
        width: width,
        type: "minner",
        italic: 0.0,
        skew: 0.0,
        max_font_size: frac_box.max_font_size,
        is_fraction: true
}

pub fn render_boxes(numer_box, denom_box, context) {
    let frac_ctx = context
    let gstyle = if (frac_ctx.frac_gstyle != null) frac_ctx.frac_gstyle
        else if (frac_ctx.style == "script")
            (if (frac_ctx.script_container == true) "script" else "text")
        else if (frac_ctx.style == "scriptscript")
            (if (frac_ctx.script_container == true) "scriptscript" else "script")
        else if (frac_ctx.matrix_cell == true) "text"
        else "display"
    let frac_box = build_frac_bar(numer_box, denom_box, frac_ctx,
        gstyle, frac_ctx.frac_gstyle != null)
    wrap_default_fraction(frac_box)
}

// Rule 15c: no bar line (binomial)

fn build_frac_nobar(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric) {
    build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric)
}

// Rule 15d: with bar line. Every bar fraction — display/text/script/
// scriptscript geometry, colorbox, composite children — is computed directly
// from TeXBook Rule 15d + MathLive's makeVList (frac_bar_geom /
// build_frac_bar_rule15) with NO hardcoded em constant table.
fn build_frac_bar(numer_box, denom_box, frac_ctx, gstyle, is_fraction_child) {
    build_frac_bar_rule15(numer_box, denom_box,
        frac_bar_geom(frac_ctx, numer_box, denom_box, gstyle, is_fraction_child))
}

// full-precision metric accessors
fn frac_metric_h(bx) { bx.height }
fn frac_metric_d(bx) { bx.depth }

// Rule 15d geometry, computed from font metrics + sigma constants — covers
// the display/text/script/scriptscript geometry styles (the children are
// rendered unscaled by Lambda, so we scale their metrics here by s_child and
// emit the matching font-size on the wrapper). Returns the geom record for any
// bar fraction — display/text/script/scriptscript geometry, colorbox, and
// composite children alike. Children carry their full extent in the primary
// height/depth fields; the old frac_bar_spec constant table is gone.
fn frac_bar_geom(frac_ctx, numer_box, denom_box, gstyle, is_fraction_child) {
        // geom_style is the MathLive geometry style threaded/derived in render().
        let geom_style = gstyle
        let is_display = geom_style == "display"
        // numerator/denominator style for this geometry (one step smaller)
        let child_style = match geom_style {
            case "display": "text"
            case "text": "script"
            case "script": "scriptscript"
            default: "scriptscript"
        }
        let csi = met.style_index(child_style)   // shift sigma index (child)
        let fsi = met.style_index(geom_style)    // rule thickness index (frac)
        // child scale relative to the fraction's own frame
        let s_child = met.style_scale(child_style) / met.style_scale(geom_style)
        let theta = met.at(met.defaultRuleThickness, fsi)
        let axis = met.AXIS_HEIGHT
        let clearance = if (is_display) 3.0 * theta else theta
        // scaled child metrics (in the fraction's frame)
        let n_h = frac_metric_h(numer_box) * s_child
        let n_d = frac_metric_d(numer_box) * s_child
        let d_h = frac_metric_h(denom_box) * s_child
        let d_d = frac_metric_d(denom_box) * s_child
        // Rule 15d: shift numerator up by u, denominator down by v, keeping
        // each clear of the bar (centred on the axis) by `clearance`.
        let numer_line = axis + theta / 2.0
        let denom_line = axis - theta / 2.0
        let num_sigma = if (is_display) met.num1 else met.num2
        let denom_sigma = if (is_display) met.denom1 else met.denom2
        let numer_shift = max(met.at(num_sigma, csi), clearance + n_d + numer_line)
        let denom_shift = max(met.at(denom_sigma, csi), clearance + d_h - denom_line)
        let fl_h = theta / 2.0
        let fl_d = theta / 2.0
        // MathLive makeVList (individualShift, children bottom->top:
        // denom @ +denom_shift, fracLine @ -denom_line, numer @ -numer_shift).
        // currPos accumulates from the initial depth; tops emit as
        // -pstrut - currPos - box.depth (pstrut added at emission).
        let depth0 = 0.0 - denom_shift - d_d
        let denom_end = depth0 + d_h + d_d
        let diff_fl = denom_line - depth0 - fl_d
        let cp_fl = depth0 + diff_fl
        let fl_end = cp_fl + fl_h + fl_d
        let diff_n = numer_shift - cp_fl - n_d
        let cp_n = cp_fl + diff_n
        let numer_end = cp_n + n_h + n_d
        let pstrut = max(1.0, max(n_h, d_h)) + 2.0
        let max_pos = max(depth0, max(denom_end, max(cp_fl, max(fl_end, max(cp_n, numer_end)))))
        let min_pos = min(depth0, min(denom_end, min(cp_fl, min(fl_end, min(cp_n, numer_end)))))
        {
            frac_height: max_pos,
            frac_depth: 0.0 - min_pos,
            vlist_h: max_pos,
            depth_holder: 0.0 - min_pos,
            denom_top: 0.0 - pstrut - depth0 - d_d,
            line_top: 0.0 - pstrut - cp_fl - fl_d,
            numer_top: 0.0 - pstrut - cp_n - n_d,
            numer_ch: n_h + n_d,
            denom_ch: d_h + d_d,
            rule_height: theta,
            pstrut: pstrut,
            font_pct: if (s_child == 1.0) null else font_pct_str(s_child),
            // Keep this marker for geometry callers that need to know whether
            // the fraction is a top-level or nested display-style extent.
            expose_raw: is_display or is_fraction_child or frac_ctx.script_container == true
        }
}

// MathLive box.ts emits font-size as `Math.ceil(scale*10000)/100`% (with a
// leading space). 0.7 -> "70%", 5/7 -> "71.43%".
fn font_pct_str(s) {
    let scaled = s * 10000.0
    let i = int(scaled)
    let c = if (float(i) >= scaled) i else i + 1
    let pct = float(c) / 100.0
    "font-size: " ++ util.fmt_num(pct, 2) ++ "%"
}

// Emit the bar-fraction vlist from a computed geom record. Every CSS dimension
// is CEIL@2 of a full-precision value (matching MathLive box.ts toString); the
    // box carries a single full-precision height/depth so the outer strut rounds
    // once via math.ls.
fn build_frac_bar_rule15(numer_box, denom_box, geom) {
    let numer_elements = box.elements_of(numer_box)
    let denom_elements = box.elements_of(denom_box)
    let fs_suffix = if (geom.font_pct != null) ";" ++ geom.font_pct else ""
    let numer_style = "height:" ++ util.fmt_em_ceil2(geom.numer_ch) ++ ";display:inline-block" ++ fs_suffix
    let denom_style = "height:" ++ util.fmt_em_ceil2(geom.denom_ch) ++ ";display:inline-block" ++ fs_suffix
    let line_style = "height:" ++ util.fmt_em_ceil2(geom.rule_height) ++ ";display:inline-block"
    let pstrut_style = "height:" ++ util.fmt_em_ceil2(geom.pstrut)
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(geom.vlist_h);
                <span class: css.CENTER, style: "top:" ++ util.fmt_em_ceil2(geom.denom_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: denom_style;
                        for (e in denom_elements) e
                    >
                >
                <span style: "top:" ++ util.fmt_em_ceil2(geom.line_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span class: css.FRAC_LINE, style: line_style>
                >
                <span class: css.CENTER, style: "top:" ++ util.fmt_em_ceil2(geom.numer_top);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: numer_style;
                        for (e in numer_elements) e
                    >
                >
            >
            <span class: css.VLIST_S; "​">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(geom.depth_holder)>
        >
    >
    // Phase A: this producer is now a MathLive one-box-field box. The visual
    // CSS dimensions above are CEIL@2 strings, while height/depth remain the
    // full-precision makeVList extents for the single root strut emit site.
    {
        element: el,
        height: geom.frac_height,
        depth: geom.frac_depth,
        width: max(numer_box.width, denom_box.width),
        type: "mord",
        italic: 0.0,
        skew: 0.0,
        max_font_size: geom.frac_height,
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
        height: spec.box_height,
        depth: spec.box_depth,
        width: max(numer_box.width, denom_box.width),
        type: "mord",
        italic: 0.0,
        skew: 0.0,
        max_font_size: spec.box_height,
        no_bar: true,
        is_fraction: true,
        delim_level: spec.delim_level,
        delim_height: spec.delim_height,
        delim_depth: spec.delim_depth
    }
}


fn frac_nobar_spec(frac_ctx, cmd, unbraced_numeric) {
    if (unbraced_numeric) {
        {
            height: 1.15,
            depth: 0.69,
            box_height: 1.45,
            box_depth: 0.95,
            box_total: 2.41,
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
            box_height: 0.85,
            box_depth: 0.35,
            box_total: 1.21,
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
            box_height: 1.45,
            box_depth: 0.95,
            box_total: 2.41,
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
    let el = <span class: css.MFRAC;
            null_delim_el(css.OPEN)
            frac_box.element
            null_delim_el(css.CLOSE)
        >
    ml_fraction_wrapper(el, frac_box, frac_box.width + 0.24)
}

fn wrap_delimited_fraction(frac_box, left_delim, right_delim) {
    let left_box = if (left_delim != null)
        delimiter_box(left_delim, frac_box, "mopen") else null
    let right_box = if (right_delim != null)
        delimiter_box(right_delim, frac_box, "mclose") else null
    let content_boxes = (for (p in [left_box, frac_box, right_box] where p != null) p)
    let elements = box.child_elements(content_boxes)
    let combined = box.hbox(content_boxes)
    let el = <span class: css.MFRAC;
            for (el in elements) el
        >
    ml_delimited_fraction_wrapper(el, combined)
}

fn ml_delimited_fraction_wrapper(el, combined) => {
        element: el,
        height: combined.height,
        depth: combined.depth,
        width: combined.width,
        type: "minner",
        italic: 0.0,
        skew: 0.0,
        max_font_size: combined.max_font_size,
        is_fraction: true
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
        width: 0.4,
        type: atom_type,
        italic: 0.0,
        skew: 0.0,
        max_font_size: h
    }
}
