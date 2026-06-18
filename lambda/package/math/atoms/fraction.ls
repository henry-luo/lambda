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
    let is_fraction_child = frac_ctx.frac_gstyle != null
    let gstyle = if (frac_ctx.frac_gstyle != null) frac_ctx.frac_gstyle
        else if (cmd == "\\tfrac") "text"
        else if (frac_ctx.style == "script") "script"
        else if (frac_ctx.style == "scriptscript") "scriptscript"
        else "display"
    // numerator/denominator geometry style (one step smaller), threaded so a
    // fraction nested directly in the numer/denom renders correctly.
    let child_gstyle = match gstyle {
        case "display": "text"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }

    // create numerator and denominator contexts (carry the child geometry)
    let num_ctx = ctx.derive(ctx.numer_context(frac_ctx), {frac_gstyle: child_gstyle})
    let den_ctx = ctx.derive(ctx.denom_context(frac_ctx), {frac_gstyle: child_gstyle})

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
        build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx,
            gstyle, is_fraction_child)

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
    {
        element: <span class: css.MFRAC;
            null_delim_el(css.OPEN)
            frac_box.element
        >,
        height: frac_box.height,
        depth: frac_box.depth,
        height_raw: frac_box.height_raw,
        depth_raw: frac_box.depth_raw,
        render_height: frac_box.render_height,
        render_depth: frac_box.render_depth,
        render_total: frac_box.render_total,
        left_right_render_depth: frac_box.left_right_render_depth,
        left_right_render_total: frac_box.left_right_render_total,
        width: frac_box.width + 0.12,
        type: "minner",
        italic: 0.0,
        skew: 0.0,
        is_fraction: true
    }
}

pub fn render_boxes(numer_box, denom_box, context) {
    let frac_ctx = context
    let gstyle = if (frac_ctx.frac_gstyle != null) frac_ctx.frac_gstyle
        else if (frac_ctx.style == "script") "script"
        else if (frac_ctx.style == "scriptscript") "scriptscript"
        else "display"
    let frac_box = build_frac_bar(numer_box, denom_box, 0.0, 0.0, 0.0,
        met.at(met.defaultRuleThickness, met.style_index(frac_ctx.style)), frac_ctx,
        gstyle, frac_ctx.frac_gstyle != null)
    wrap_default_fraction(frac_box)
}

// Rule 15c: no bar line (binomial)
// CEIL@2 helper for spec computations that need MathLive-compatible rounding
fn ceil2(x) {
    let scaled = x * 100.0
    let i = int(scaled)
    let f = float(i)
    let ceil_int = if (f >= scaled) i else i + 1
    float(ceil_int) / 100.0
}

fn build_frac_nobar(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric) {
    build_frac_nobar_vlist(numer_box, denom_box, frac_ctx, cmd, unbraced_numeric)
}

// Rule 15d: with bar line — dispatcher.
// Bar fractions are computed directly from TeXBook Rule 15d + MathLive's
// makeVList (see frac_bar_geom / build_frac_bar_rule15) with NO hardcoded em
// constants for: top-level display/text (\frac/\dfrac/\tfrac) AND directly
// fraction-nested fractions (gstyle threaded via frac_gstyle). Subscript-reached
// script/scriptscript fractions, colorbox, and composite-child fractions still
// fall through to the legacy constant table until their parents are ported.
fn build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx, gstyle, is_fraction_child) {
    let geom = frac_bar_geom(frac_ctx, numer_box, denom_box, gstyle, is_fraction_child)
    if (geom != null) build_frac_bar_rule15(numer_box, denom_box, geom)
    else build_frac_bar_legacy(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx)
}

// full-precision metric accessors (fall back to rounded when raw absent)
fn frac_raw_h(bx) { if (bx.height_raw != null) bx.height_raw else bx.height }
fn frac_raw_d(bx) { if (bx.depth_raw != null) bx.depth_raw else bx.depth }

// Rule 15d geometry, computed from font metrics + sigma constants — covers
// the display/text/script/scriptscript geometry styles (the children are
// rendered unscaled by Lambda, so we scale their metrics here by s_child and
// emit the matching font-size on the wrapper). Returns a geom record (all em
// values full precision) or null to fall back to the legacy constant dispatch
// (colorbox quirks, or composite children that lack raw metrics).
fn frac_bar_geom(frac_ctx, numer_box, denom_box, gstyle, is_fraction_child) {
    let has_raw = numer_box.height_raw != null and numer_box.depth_raw != null and
                  denom_box.height_raw != null and denom_box.depth_raw != null
    // A script/scriptscript fraction reached via a SUBSCRIPT (not a fraction
    // numerator) interlocks with the still-legacy scripts.ls that composes it:
    // changing the inner box regresses the script. Those stay on legacy until
    // Rule 18 is ported. Fraction-nested fractions (is_fraction_child) and
    // top-level display/text fractions have no such legacy-parent dependency.
    let subscript_script = (not is_fraction_child) and
        (frac_ctx.style == "script" or frac_ctx.style == "scriptscript")
    if (frac_ctx.colorbox_content == true or not has_raw or subscript_script)
        null
    else {
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
        let n_h = frac_raw_h(numer_box) * s_child
        let n_d = frac_raw_d(numer_box) * s_child
        let d_h = frac_raw_h(denom_box) * s_child
        let d_d = frac_raw_d(denom_box) * s_child
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
            // Expose height_raw to the single-rounding strut path AND to a
            // parent fraction's Rule 15 (which reads the child's raw metrics)
            // when this fraction is itself a fraction-child or a top-level
            // display fraction. A scaled fraction reached via a subscript does
            // NOT expose raw, so it can't flip the still-legacy script parent.
            expose_raw: is_display or is_fraction_child
        }
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
// box carries a single full-precision height/depth (mirrored into *_raw so the
// outer strut rounds once via math.ls).
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
    // Phase A (box-model collapse), applied where it's safe — a metric-driven
    // box. height/depth ARE the layout values (CEIL projections for a non-raw
    // consumer); full precision lives in *_raw for the single-rounding strut.
    // render_height/render_depth are NOT set: they would just equal height/
    // depth, and every consumer null-coalesces render_height -> height. Only
    // render_total survives because CEIL(h+d) != CEIL(h)+CEIL(d) (the §1.4
    // asymmetry); it goes when the whole tree carries full precision.
    let h_em = util.ceil_em2(geom.frac_height)             // strut height
    let d_em = 0.0 - util.ceil_em2(0.0 - geom.frac_depth)  // va depth (ceil toward 0)
    let total_em = util.ceil_em2(geom.frac_height + geom.frac_depth)
    {
        element: el,
        height: h_em,
        depth: d_em,
        height_raw: if (geom.expose_raw) geom.frac_height else null,
        depth_raw: if (geom.expose_raw) geom.frac_depth else null,
        render_total: total_em,
        // full-precision content extent for \left..\right delimiter sizing
        // (stretchy delimiters size against the unrounded content, not CEIL@2).
        left_right_render_depth: geom.frac_depth,
        left_right_render_total: geom.frac_height + geom.frac_depth,
        width: max(numer_box.width, denom_box.width),
        type: "mord",
        italic: 0.0,
        skew: 0.0,
        is_fraction: true
    }
}

// Rule 15d: with bar line (legacy constant dispatch)
fn build_frac_bar_legacy(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx) {
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
        // Metric-driven: derive child wrappers from numer/denom raw metrics
        // scaled by 5/7 (scriptscript ratio from script parent). vlist_h
        // follows: -numer_top - 3 + numer_child_h. depth_holder accounts
        // for denom descender.
        let numer_ch = script_frac_child_h_metric(numer_box)
        let denom_ch = script_frac_child_h_metric(denom_box)
        let dh = script_frac_depth_holder_metric(denom_box, 0.54)
        let vh = 0.5 + numer_ch
        {
            height: vh,
            depth: dh,
            render_height: vh,
            render_depth: dh,
            render_total: vh + dh,
            depth_holder: dh,
            denom_top: -2.46,
            line_top: -3.22,
            numer_top: -3.5,
            numer_child_height: numer_ch,
            denom_child_height: denom_ch,
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
        // B3: scriptscript-style frac (mirror of B1's metric-driven approach)
        let numer_ch = script_frac_child_h_metric(numer_box)
        let denom_ch = script_frac_child_h_metric(denom_box)
        let dh = script_frac_depth_holder_metric(denom_box, 0.54)
        let vh = 0.5 + numer_ch
        {
            height: vh,
            depth: dh,
            render_height: vh,
            render_depth: dh,
            render_total: vh + dh,
            depth_holder: dh,
            denom_top: -2.46,
            line_top: -3.22,
            numer_top: -3.5,
            numer_child_height: numer_ch,
            denom_child_height: denom_ch,
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
            denom_child_height: denom_child_for_default(denom_box),
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
        // Short-denom: use the actual denom height so digit denoms emit
        // 0.65 (matches MathLive) and letter denoms still emit ~0.7.
        let denom_h = ceil2(max(0.7, denom_box.height))
        let denom_h_final = if (denom_h <= 0.7 and denom_box.depth == 0.0) ceil2(denom_box.height) else denom_h
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
            denom_child_height: denom_h_final,
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
    } else if (denom_total >= 0.75 and numer_total < 0.75 and
               not (numer_box.height < 0.5 and numer_box.depth < 0.01)) {
        // Compound denominator with potential descender (e.g. p+q below 1).
        // Short single-letter numerators (e/x, height < 0.5) are excluded so
        // they fall through to the short-numer branch (frac height 0.94),
        // matching MathLive's `\frac{e}{f}` / `\cfrac{e}{f}` output.
        // When the denom has a substantial descender, MathLive's effective
        // depth reflects the full descent extent, not the hardcoded 0.77.
        // When the numerator has a tall letter (height >= 0.7), MathLive
        // emits a taller fraction (height 1.2 not 1.15).
        let has_descender = denom_box.depth > 0.15
        let numer_is_tall = numer_box.height >= 0.7
        let frac_h = if (numer_is_tall) 1.2 else 1.15
        let frac_d = if (has_descender) 0.68 + denom_box.depth + 0.01 else 0.77
        let frac_total = if (has_descender) frac_h + frac_d
            else if (numer_is_tall) frac_h + frac_d
            else 1.92
        let frac_dh = if (has_descender) frac_d + 0.01 else 0.77
        let denom_ch = if (has_descender and numer_is_tall) ceil2(denom_box.height + denom_box.depth)
            else if (has_descender) 0.78
            else if (numer_is_tall) ceil2(denom_box.height + denom_box.depth)
            else 0.73
        {
            height: frac_h,
            depth: frac_d,
            render_height: frac_h,
            render_depth: frac_d,
            render_total: frac_total,
            depth_holder: frac_dh,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: ceil2(numer_box.height + numer_box.depth),
            denom_child_height: denom_ch,
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
        // Tall numerator with short denom. When numerator has a tall letter
        // (height >= 0.7), MathLive emits a taller fraction (height 1.2 not
        // 1.15) to make room for the ascender.
        let numer_child_height = text_fraction_numer_child_height(numer_box)
        let has_descender = script_fraction_has_descender(numer_box)
        let numer_is_tall = numer_box.height >= 0.7
        let base_h = if (numer_is_tall) 1.2 else 1.15
        let base_d = 0.68
        // MathLive sizes the denom wrapper to the letter's actual height
        // (ceil@2 of h+d) rather than padding to 0.65. For tall numerators
        // this matters most when the denom is single-char or compound with
        // a tall letter.
        let denom_full = ceil2(denom_box.height + denom_box.depth)
        let denom_ch = if (numer_is_tall) denom_full
            else if (denom_box.height < 0.5 and denom_box.depth < 0.01) denom_full
            else 0.65
        // For tall numerators, MathLive emits the numerator wrapper at
        // its actual full extent.
        let numer_ch = if (numer_is_tall and not has_descender) ceil2(numer_box.height + numer_box.depth)
            else numer_child_height
        {
            height: if (has_descender) numer_child_height + 0.39 else base_h,
            depth: if (has_descender) 0.685 else base_d + 0.005,
            render_height: if (has_descender) numer_child_height + 0.39 else base_h,
            render_depth: if (has_descender) 0.68 else base_d,
            render_total: if (has_descender) numer_child_height + 1.08
                else if (numer_is_tall) base_h + base_d + 0.01
                else 1.84,
            depth_holder: 0.69,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: if (has_descender) 0.0 - 3.58 else 0.0 - 3.5,
            numer_child_height: numer_ch,
            denom_child_height: denom_ch,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (numer_box.height < 0.5 and numer_box.depth < 0.01) {
        // Short-body numerator (a single letter like a, m, x with cmmi
        // height 0.44 and no descender). MathLive uses fraction height 0.94
        // instead of the default 1.15 for these.
        // When the denominator has a descender, MathLive's effective depth
        // rounds up by 0.01em (CEIL@2 emission semantics). Lambda emits
        // render_depth/render_total directly, so we bake the +0.01 in. The
        // denom_child_height also accounts for the full descender extent.
        let has_descender = denom_box.depth > 0.0
        let denom_full_h = denom_box.height + denom_box.depth
        let denom_h = if (has_descender) ceil2(denom_full_h)
            else if (denom_box.height < 0.7) denom_box.height else 0.7
        let extra_depth = if (has_descender) denom_box.depth else 0.0
        let frac_depth_base = 0.68 + extra_depth
        let frac_depth = if (has_descender) frac_depth_base + 0.01 else frac_depth_base
        let total = 0.94 + frac_depth
        let dh = if (has_descender) frac_depth + 0.01 else 0.69
        {
            height: 0.94,
            depth: frac_depth + 0.005,
            render_height: 0.94,
            render_depth: frac_depth,
            render_total: total,
            depth_holder: dh,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: numer_box.height,
            denom_child_height: denom_h,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else if (numer_box.height < 0.75 and numer_box.depth < 0.15 and
               not (numer_box.height >= 0.6 and numer_box.height < 0.68) and
               not (numer_box.height >= 0.6 and numer_box.height < 0.66)) {
        // Tall-body numerator OR compound (letters and operators) with no
        // significant descender (>0.15). MathLive uses height 1.19 for clean
        // tall-only fractions, 1.20 when descenders are present. The
        // hardcoded 1.2 was off by 0.01em on pure cmmi tall-body atoms.
        // Excludes the i-like case (height ~0.66) which uses 1.16.
        let has_any_descender = numer_box.depth > 0.05 or denom_box.depth > 0.05
        let denom_full = denom_box.height + denom_box.depth
        let denom_h = if (denom_box.height < 0.7) ceil2(denom_full) else 0.7
        // The fraction descent below the bar follows the denominator's real
        // descent: 0.68 baseline + denom descender (CEIL@2). For `c+d` the
        // `+`/`d` descent (0.08333) lifts it to 0.77, not the bare 0.76.
        let denom_d_for_depth = if (denom_box.depth_raw != null and denom_box.depth_raw > 0.0)
            denom_box.depth_raw else denom_box.depth
        let frac_depth = if (denom_box.depth > 0.05) ceil2(0.68 + denom_d_for_depth) else 0.68
        let frac_h = if (has_any_descender) 1.2 else 1.19
        let total = frac_h + frac_depth
        let dh = if (denom_box.depth > 0.05) frac_depth else 0.69
        // Child wrapper heights span the full glyph extent (h+d). When the
        // numer/denom carries an operator with descent (e.g. `+` in `a+b`,
        // depth 0.08), the wrapper grows to ceil2(h+d) = 0.78 rather than the
        // bare height. Uses raw metrics when the hbox propagated them.
        let numer_h_raw = if (numer_box.height_raw != null) numer_box.height_raw else numer_box.height
        let numer_d_raw = if (numer_box.depth_raw != null) numer_box.depth_raw else numer_box.depth
        let numer_ch = if (numer_box.depth > 0.05) ceil2(numer_h_raw + numer_d_raw) else numer_box.height
        let denom_h_raw = if (denom_box.height_raw != null) denom_box.height_raw else denom_box.height
        let denom_d_raw = if (denom_box.depth_raw != null) denom_box.depth_raw else denom_box.depth
        let denom_ch = if (denom_box.height >= 0.7 and denom_box.depth > 0.05)
            ceil2(denom_h_raw + denom_d_raw) else denom_h
        {
            height: frac_h,
            depth: frac_depth,
            render_height: frac_h,
            render_depth: frac_depth,
            render_total: total,
            depth_holder: dh,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: -3.5,
            numer_child_height: numer_ch,
            denom_child_height: denom_ch,
            child_font_pct: null,
            rule_height: 0.04
        }
    } else {
        // Default fraction layout (digits, mixed content of moderate height).
        // When denominator has a descender, bump depth/depth_holder so the
        // strut-bottom and inner vlist reflect the full descender extent.
        // render_total is bumped by 0.01 to match MathLive's CEIL@2 emission
        // (the hardcoded prior value was 1.84 even though render_depth was 0.68).
        let has_descender = denom_box.depth > 0.005
        let extra_d = if (has_descender) denom_box.depth + 0.01 else 0.0
        let base_depth_render = 0.68 + extra_d
        let base_depth = base_depth_render + 0.005
        // numer wrapper height: when numer is a plain low-letter word (max
        // height ~ 0.43-0.61, no descender) use its actual content height so
        // MathLive's compact 0.62em emit matches. Tall numers without
        // descenders (like `n!` h=0.7) bump frac_height to 1.2. Other
        // cases take the hardcoded 0.65/1.15 defaults.
        // numer wrapper height: when numer has no descender, use its actual
        // content height (CEIL@2) rather than the hardcoded 0.65 default.
        // Operator-name numers (`\sin x` h=0.67) and digit-only numers (`2`
        // h=0.65) both get their true heights; full-letter words still use
        // 0.65 to match MathLive's default fraction sizing.
        let numer_h = if (numer_box.depth < 0.005 and numer_box.height < 0.65 and numer_box.height > 0.0)
            ceil2(numer_box.height)
            else if (numer_box.depth < 0.005 and numer_box.height > 0.65 and numer_box.height <= 0.68)
                ceil2(numer_box.height)
            else 0.65
        let frac_height = if (numer_h < 0.65) 1.12
            else if (numer_h > 0.65 and numer_h <= 0.68) 1.17
            else 1.15
        let frac_top_numer = -3.5
        let base_total = if (has_descender) frac_height + base_depth_render
            else frac_height + base_depth_render + 0.01
        let base_dh = if (has_descender) base_depth_render + 0.01 else 0.69
        {
            height: frac_height,
            depth: base_depth,
            render_height: frac_height,
            render_depth: base_depth_render,
            render_total: base_total,
            depth_holder: base_dh,
            denom_top: -2.31,
            line_top: -3.23,
            numer_top: frac_top_numer,
            numer_child_height: numer_h,
            denom_child_height: denom_child_for_default(denom_box),
            child_font_pct: null,
            rule_height: 0.04
        }
    }
}

fn script_frac_child_height(child_box, fallback) {
    let total = render_total_of(child_box)
    if (total >= 0.7) fallback else 0.46
}

// Compute the wrapper height for a fraction child rendered at scriptscript
// scaling (5/7 of parent script). Uses raw metrics when available; matches
// MathLive's `ceil2((h_raw + d_raw) * 5/7)` for descender atoms, h-only for
// non-descenders. The hbox's raw fields propagate from text_box leaves.
fn script_frac_child_h_metric(child_box) {
    let h_raw = if (child_box.height_raw != null) child_box.height_raw else child_box.height
    let d_raw = if (child_box.depth_raw != null) child_box.depth_raw else child_box.depth
    let has_descender = d_raw > 0.005
    let total_em = if (has_descender) (h_raw + d_raw) * (5.0 / 7.0)
                   else h_raw * (5.0 / 7.0)
    ceil2(total_em)
}

// depth_holder for script-style fraction with descender denominator:
// extends below the denom's baseline by the scaled descender extent.
fn script_frac_depth_holder_metric(denom_box, base_holder) {
    let d_raw = if (denom_box.depth_raw != null) denom_box.depth_raw else denom_box.depth
    if (d_raw > 0.005) ceil2(base_holder + d_raw * (5.0 / 7.0))
    else base_holder
}

// For default fraction spec: denom wrapper height should accommodate
// content with operators (which have descent). Use max(height + depth, 0.65)
// capped at typical TeX baseline alignment.
fn denom_child_for_default(denom_box) {
    let total = denom_box.height + denom_box.depth
    let has_descender = denom_box.depth > 0.005
    // When the denominator has a descender, MathLive uses CEIL@2 of the
    // full h+d extent for the wrapper, even when h+d < 0.65. This makes
    // the wrapper match the visible glyph rather than padding to a default.
    if (denom_box.height < 0.65 and denom_box.depth < 0.01) denom_box.height
    else if (has_descender) ceil2(total)
    else if (total > 0.65) total
    else 0.65
}

fn script_fraction_descender_child_height(child_box) {
    if (script_fraction_has_digit(child_box)) 0.59 else 0.55
}

fn text_fraction_numer_child_height(child_box) {
    if (script_fraction_has_descender(child_box)) {
        // When the numerator has a clearly tall non-operator atom (h >= 0.7
        // from a letter like `\partial` paired with a descender like `f`),
        // prefer the full extent (rounded up). Compound numerators with
        // operator signs use Lambda's 0.69em `+`/`-` heuristic and would
        // otherwise trip this branch — exclude them by requiring h > 0.69.
        let full = ceil2(child_box.height + child_box.depth)
        if (child_box.height > 0.69 and full > 0.85) full
        else if (script_fraction_has_digit(child_box)) 0.84 else 0.78
    } else 0.73
}

fn script_fraction_has_descender(child_box) {
    items_have_descender(box.elements_of(child_box), 0)
}

fn script_fraction_has_digit(child_box) {
    items_have_digit(box.elements_of(child_box), 0)
}

// Returns true when the box contains ONLY digit/numeric content
// (no letters or operators). Used to distinguish digit-only numerators
// from compound expressions in frac_bar_spec.
fn is_numeric_text_box(child_box) {
    let els = box.elements_of(child_box)
    is_all_numeric(els, 0)
}

fn is_all_numeric(items, i) {
    if (i >= len(items)) true
    else if (item_is_numeric(items[i])) is_all_numeric(items, i + 1)
    else false
}

fn item_is_numeric(item) {
    if (item is string) is_digit_only_text(string(item), 0)
    else if (item is element and len(item) > 0 and item[0] is string)
        is_digit_only_text(string(item[0]), 0)
    else false
}

fn is_digit_only_text(s, i) {
    if (i >= len(s)) i > 0
    else
        (let ch = slice(s, i, i + 1),
         if ((ch >= "0" and ch <= "9") or ch == ".")
             is_digit_only_text(s, i + 1)
         else false)
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
        // f in cmmi italic has depth 0.19444 — counted as a descender for
        // fraction sizing so `\frac{\partial f}{...}` emits a taller wrapper.
        ch == "g" or ch == "j" or ch == "p" or ch == "q" or ch == "y" or
            ch == "f" or
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
        height_raw: frac_box.height_raw,
        depth_raw: frac_box.depth_raw,
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
