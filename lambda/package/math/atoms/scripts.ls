// math/atoms/scripts.ls — Superscript/subscript rendering
// Implements TeXBook Rules 18a-f from Appendix G

import box: lambda.package.math.box
import ctx: lambda.package.math.context
import css: lambda.package.math.css
import met: lambda.package.math.metrics
import util: lambda.package.math.util
import sym: lambda.package.math.symbols

// ============================================================
// Script rendering (superscript, subscript, or both)
// ============================================================

// render a subsup AST node
// node has: base, sub, sup, modifier attributes
// render_fn: top-level render function for recursive calls
pub fn render(node, context, render_fn) {
    // check if base is a big operator (e.g. \sum, \prod, \int, \lim)
    let base = node.base
    let is_big_op = base != null and base is element and name(base) == 'command' and
        base.name != null and sym.is_limit_op(string(base.name))
    // Integral-family operators (\int, \oint, \iint, etc.) use SIDE limits
    // (lm_msubsup as a sibling of the symbol) in inline mode, matching
    // MathLive's subsupPlacement='adjacent' rule. Display mode would stack
    // them (TODO).
    let cmd_name = if (is_big_op) string(base.name) else ""
    let is_integral = is_integral_op(cmd_name)

    if (is_big_op and is_integral and not ctx.is_display(context))
        render_integral_inline_scripts(base, node, context, render_fn)
    else if (is_big_op and not ctx.is_script(context))
        render_big_op_limits(base, node, context, render_fn)
    else if (is_big_op and node.sub != null and node.sup != null)
        render_inline_big_op_scripts(base, node, context, render_fn)
    else
        render_scripts(node, context, render_fn)
}

// Compute the height to use for the sub-script wrapper. MathLive uses
// CEIL@2(sub_box.height_raw * 0.7) when raw is available, else falls back
// to a content-based approximation (0.46 default for short / digit content,
// 0.48 for uppercase-letter content).
fn sub_height_for(sub_box) {
    let raw = if (sub_box != null) sub_box.height_raw else null
    if (raw != null) {
        let scaled = raw * 0.7
        let scaled_100 = scaled * 100.0
        let i = int(scaled_100)
        let f = float(i)
        let ceil_int = if (f >= scaled_100) i else i + 1
        float(ceil_int) / 100.0
    } else 0.46
}

fn is_integral_op(cmd_name) {
    cmd_name == "int" or cmd_name == "oint" or cmd_name == "iint" or
    cmd_name == "iiint" or cmd_name == "iiiint" or cmd_name == "oiint" or
    cmd_name == "oiiint" or cmd_name == "idotsint" or cmd_name == "ointclockwise" or
    cmd_name == "ointctrclockwise"
}

// Render integral with sub/sup limits to the SIDE (msubsup), matching
// MathLive's textstyle integral layout. Wraps in lm_op-group with
// large-op size3 symbol + adjacent lm_msubsup vlist.
fn render_integral_inline_scripts(base, node, context, render_fn) {
    let cmd = string(base.name)
    let unicode = sym.lookup_symbol("\\" ++ cmd)
    let display_text = if (unicode != null) unicode else cmd
    let sub_box = if (node.sub != null)
        render_fn(node.sub, ctx.sub_context(context)) else null
    let sup_box = if (node.sup != null)
        render_fn(node.sup, ctx.sup_context(context)) else null
    let sub_elements = if (sub_box != null) box.elements_of(sub_box) else []
    let sup_elements = if (sup_box != null) box.elements_of(sup_box) else []
    let has_sub = sub_box != null
    let has_sup = sup_box != null

    // MathLive integral metrics (Size2): h=1.36, d=0.86, italic=0.45
    // The msubsup vlist height/depth-holder differ by limit configuration:
    //   sub+sup:    vlist=1.55, depth_holder=0.9, sub uses margin-left:-italic
    //   sub-only:   vlist=-0.41 (negative — content only above its origin),
    //               depth_holder=0.9, sub uses margin-right:+0.05em
    //   sup-only:   vlist=1.55, depth_holder=0 (sup-only TODO)
    // Sub margin behavior: when paired with sup, nestle into the integral's
    // italic correction (negative left margin). When sub alone, MathLive
    // offsets slightly to the right.
    let sub_margin_attr = if (has_sup)
        "margin-left:-0.44em"
        else "margin-right:0.05em"

    // Sub/sup heights derive from content's actual height scaled to script
    // style (×0.7) then CEIL@2 to match MathLive's emission.
    let sub_inner_h = if (sub_box != null) sub_height_for(sub_box) else 0.46
    let sup_inner_h = if (sup_box != null) sub_height_for(sup_box) else 0.46
    // Vlist height tracks the sup's extent + a fixed offset of 1.09em (the
    // gap between baseline and sup top). With only sub, vlist is negative
    // (-0.41) since content is below origin.
    let vlist_height = if (has_sup) (1.09 + sup_inner_h) else (0.0 - 0.41)
    let depth_holder = 0.9
    let sub_top = -2.1
    let sup_top = -4.08
    let sub_span = if (has_sub) [
        <span style: "top:" ++ util.fmt_em(sub_top) ++ ";" ++ sub_margin_attr;
            <span class: css.PSTRUT, style: "height:3em">
            <span style: "height:" ++ util.fmt_em(sub_inner_h) ++ ";display:inline-block;font-size: 70%";
                for (el in sub_elements) el
            >
        >
    ] else []
    let sup_span = if (has_sup) [
        <span style: "top:" ++ util.fmt_em(sup_top);
            <span class: css.PSTRUT, style: "height:3em">
            <span style: "height:" ++ util.fmt_em(sup_inner_h) ++ ";display:inline-block;font-size: 70%";
                for (el in sup_elements) el
            >
        >
    ] else []

    let int_symbol_style = "margin-right:0.45em"
    let el = <span class: css.OP_GROUP;
        <span class: "lm_op-symbol lm_large-op", style: int_symbol_style; display_text>
        <span class: css.MSUBSUP;
            <span class: css.VLIST_T2;
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:" ++ util.fmt_em(vlist_height);
                        for (el in sub_span) el
                        for (el in sup_span) el
                    >
                    <span class: css.VLIST_S; "​">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:" ++ util.fmt_em(depth_holder)>
                >
            >
        >
    >
    // Box height/depth follow MathLive's effective extents. With both
    // limits the box reaches up to the sup and the strut-bottom drops to
    // accommodate the sub. The raw fields use slightly-higher precision so
    // CEIL@2 of (h_raw + d_raw) matches MathLive's emitted strut-bottom.
    let box_h = if (has_sup) vlist_height else 1.36
    let box_d = if (has_sub) 0.89 else 0.86
    let box_h_raw = box_h
    let box_d_raw = if (has_sub and has_sup) 0.89
        else if (has_sub) 0.89222
        else 0.86225
    {
        element: el,
        height: box_h,
        depth: box_d,
        height_raw: box_h_raw,
        depth_raw: box_d_raw,
        render_height: box_h,
        render_depth: box_d,
        render_total: box_h + box_d,
        width: 0.55556,
        type: "mop",
        italic: 0.0,
        skew: 0.0
    }
}

fn render_inline_big_op_scripts(base, node, context, render_fn) {
    let cmd = string(base.name)
    let unicode = sym.lookup_symbol(cmd)
    let display_text = if (unicode != null) unicode else cmd
    let sub_box = render_fn(node.sub, ctx.sub_context(context))
    let sup_box = render_fn(node.sup, ctx.sup_context(context))
    let sub_elements = box.elements_of(sub_box)
    let sup_elements = box.elements_of(sup_box)
    let el = <span class: css.OP_GROUP;
        <span class: "lm_op-symbol lm_small-op"; display_text>
        <span class: css.MSUBSUP;
            <span class: css.VLIST_T2;
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:0.94em";
                        <span style: "top:-2.71em";
                            <span class: css.PSTRUT, style: "height:3em">
                            <span style: "height:0.31em;display:inline-block;font-size: 70%";
                                for (el in sub_elements) el
                            >
                        >
                        <span style: "top:-3.47em";
                            <span class: css.PSTRUT, style: "height:3em">
                            <span style: "height:0.46em;display:inline-block;font-size: 70%";
                                for (el in sup_elements) el
                            >
                        >
                    >
                    <span class: css.VLIST_S; "\u200B">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:0.29em">
                >
            >
        >
    >
    {
        element: el,
        height: 0.94,
        depth: 0.29,
        render_height: 0.94,
        render_depth: 0.29,
        render_total: 1.23,
        width: max(0.6, max(sub_box.width, sup_box.width)),
        type: "mop",
        italic: 0.0,
        skew: 0.0
    }
}

// render a big operator with limits above/below (display mode)
fn render_big_op_limits(base, node, context, render_fn) {
    let cmd = string(base.name)
    let unicode = sym.lookup_symbol(cmd)
    let is_text_op = sym.get_operator_name(
        if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\") slice(cmd, 1, len(cmd)) else cmd
    )
    let display_text = if (unicode != null) unicode
        else if (is_text_op != null) is_text_op
        else cmd
    let op_box = if (is_text_op != null)
        box.text_box(display_text, css.CMR, "mop")
    else large_op_symbol_box(display_text)

    let has_sub = node.sub != null
    let has_sup = node.sup != null

    let sub_box = if (has_sub) render_fn(node.sub, ctx.sub_context(context)) else null
    let sup_box = if (has_sup) render_fn(node.sup, ctx.sup_context(context)) else null

    if (has_sub or has_sup)
        render_large_op_limits_vlist(op_box, sub_box, sup_box, is_text_op != null)
    else
        (let scaled_op = if (is_text_op != null) op_box else box.with_scale(op_box, 1.5),
         box.vbox([{box: scaled_op, shift: 0.0}]))
}

// CEIL@2 helper — returns a 2-decimal CEIL of the input em value. Mirrors
// MathLive's toString rounding rule for emit-side dimensions.
fn ceil_em2(x) {
    let scaled = x * 100.0
    let i = int(scaled)
    let f = float(i)
    let ceil_int = if (f >= scaled) i else i + 1
    float(ceil_int) / 100.0
}

fn large_op_symbol_box(text) => {
    element: <span class: "lm_op-symbol lm_large-op"; text>,
    height: 1.61,
    depth: 0.0,
    render_height: 1.61,
    render_depth: 0.0,
    render_total: 1.61,
    width: 0.6,
    type: "mop",
    italic: 0.0,
    skew: 0.0
}

fn sub_box_has_descender(sub_box) {
    // Detect if a subscript box contains a descender letter (g/j/p/q/y/f).
    // Used to pick proper box depth in limit-style big-ops.
    if (sub_box.depth >= 0.18) true
    else false
}

fn render_large_op_limits_vlist(op_box, sub_box, sup_box, is_text_op) {
    let op_elements = box.elements_of(op_box)
    let has_sub = sub_box != null
    let has_sup = sup_box != null
    let sub_elements = if (has_sub) box.elements_of(sub_box) else []
    let sup_elements = if (has_sup) box.elements_of(sup_box) else []
    let sub_w = if (has_sub) sub_box.width else 0.0
    let sup_w = if (has_sup) sup_box.width else 0.0
    let compact_limits = has_sub and has_sup and sub_w <= 1.0 and sup_w > 1.0
    let sub_has_descender = has_sub and sub_box_has_descender(sub_box)
    // Op-symbol wrapper height: large ops (sym) are at Size3 scale = 1.61em;
    // text ops (\\lim, \\sup, etc.) emit at the actual letter heights.
    let op_wrap_h = if (is_text_op) 0.7 else 1.61
    // vlist_height varies by which limits are present:
    //   text op + sub+sup:        0.94 (op + sub stacked)
    //   text op + single limit:   0.7  (op + tiny limit)
    //   large op + compact:       1.81
    //   large op + sub+sup:       1.66
    //   large op + single limit:  1.06
    let vlist_height = if (is_text_op and has_sub and has_sup) 0.94
                       else if (is_text_op) 0.7
                       else if (compact_limits) 1.81
                       else if (has_sub and has_sup) 1.66
                       else 1.06
    let sub_h_raw = if (has_sub and sub_box.height_raw != null) sub_box.height_raw
                    else if (has_sub) sub_box.height
                    else 0.0
    let sub_scaled = ceil_em2(sub_h_raw * 0.7)
    let sub_child_height = if (compact_limits) 0.31
                           else if (sub_has_descender) 0.6
                           else if (has_sub) sub_scaled else 0.0
    let sup_h_raw = if (has_sup and sup_box.height_raw != null) sup_box.height_raw
                    else if (has_sup) sup_box.height
                    else 0.0
    let sup_scaled = ceil_em2(sup_h_raw * 0.7)
    let sup_child_height = if (compact_limits) 0.46
                           else if (has_sup) sup_scaled else 0.0
    // pstrut & op_top differ by op kind: large-op uses 3.05em pstrut and
    // top:-3.05em; text-op uses 3em pstrut and top:-3em.
    let pstrut_em = if (is_text_op) "3em" else "3.05em"
    let op_top_em = if (is_text_op) "-3em" else "-3.05em"
    // Offsets for depth_holder and box_depth differ by configuration:
    //   text-op + sub-only:       offsets 0.26 / 0.25 (small op, tight stack)
    //   sub+sup with descender:   0.82 / 0.81
    //   sub+sup (default):        0.81 / 0.80
    //   sub-only (large-op):      0.95 / 0.94
    let dh_offset = if (is_text_op and has_sub) 0.26
                    else if (sub_has_descender) 0.82
                    else if (has_sub and has_sup) 0.81
                    else 0.95
    let bd_offset = if (is_text_op and has_sub) 0.25
                    else if (sub_has_descender) 0.81
                    else if (has_sub and has_sup) 0.80
                    else 0.94
    let depth_holder = if (compact_limits) 1.26
                       else if (has_sub) sub_child_height + dh_offset
                       else 0.0
    let box_depth = if (compact_limits) 1.26
                    else if (has_sub) sub_child_height + bd_offset
                    else 0.0
    // sub_top positioning differs by op kind too. Text-op uses
    // -(2.38) for sub_child=0.46 (formula: -1.92 - sub_child).
    let sub_top = if (is_text_op and has_sub) (0.0 - 1.92 - sub_child_height)
                  else if (compact_limits) 0.0 - 1.89
                  else if (has_sub and has_sup) 0.0 - 1.87
                  else 0.0 - 1.89
    let sub_span = if (has_sub) [
        <span class: css.CENTER, style: "top:" ++ util.fmt_em(sub_top);
            <span class: css.PSTRUT, style: "height:" ++ pstrut_em>
            <span style: "height:" ++ util.fmt_em(sub_child_height) ++ ";display:inline-block;font-size: 70%";
                for (el in sub_elements) el
            >
        >
    ] else []
    let sup_top_em = if (is_text_op) "-3.7em" else "-4.3em"
    let sup_span = if (has_sup) [
        <span class: css.CENTER, style: "top:" ++ sup_top_em;
            <span class: css.PSTRUT, style: "height:" ++ pstrut_em>
            <span style: "height:" ++ util.fmt_em(sup_child_height) ++ ";display:inline-block;font-size: 70%";
                for (el in sup_elements) el
            >
        >
    ] else []
    let el = <span class: css.OP_GROUP;
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em(vlist_height);
                    for (el in sub_span) el
                    <span class: css.CENTER, style: "top:" ++ op_top_em;
                        <span class: css.PSTRUT, style: "height:" ++ pstrut_em>
                        <span style: "height:" ++ util.fmt_em(op_wrap_h) ++ ";display:inline-block";
                            for (el in op_elements) el
                        >
                    >
                    for (el in sup_span) el
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em(depth_holder)>
            >
        >
    >
    // Text-op strut height includes the op's natural ascent (0.75 for "lim")
    // — slightly more than vlist_height (0.7) because the strut covers the
    // unscaled letter heights. depth_raw is set slightly below the rounded
    // depth so CEIL@2(-d_raw) emits "-0.71em" not "-0.72em" (matches MathLive).
    let out_h = if (is_text_op) 0.75 else vlist_height
    let out_d = if (is_text_op and has_sub) 0.72 else box_depth
    let out_d_raw = if (is_text_op and has_sub) 0.715 else box_depth
    {
        element: el,
        height: out_h,
        depth: out_d,
        height_raw: out_h,
        depth_raw: out_d_raw,
        render_height: out_h,
        render_depth: out_d,
        render_total: out_h + out_d,
        width: max(max(op_box.width, sub_w), sup_w),
        type: "mop",
        italic: 0.0,
        skew: 0.0
    }
}

// render normal inline subscript/superscript
fn render_scripts(node, context, render_fn) {
    // render the base
    let base_box = if (node.base != null) render_fn(node.base, context)
        else box.text_box("", null, "ord")

    let has_sup = node.sup != null
    let has_sub = node.sub != null

    if (not has_sup and not has_sub) base_box
    else
        (let is_char = base_box.height <= met.DEFAULT_CHAR_HEIGHT + 0.01,
         let sup_ctx = ctx.sup_context(context),
         let sub_ctx_ = ctx.sub_context(context),
         let sup_box = if (has_sup) render_fn(node.sup, sup_ctx) else null,
         let sub_box = if (has_sub) render_fn(node.sub, sub_ctx_) else null,
         let si = met.style_index(context.style),
         let scriptspace = util.SCRIPT_SPACE,
         let x_height = met.X_HEIGHT,
         let rule_width = met.at(met.defaultRuleThickness, si),
         let init_sup_shift = if (is_char) 0.0
             else base_box.height - met.at(met.supDrop, si) * met.style_scale(sup_ctx.style),
         let init_sub_shift = if (is_char) 0.0
             else base_box.depth + met.at(met.subDrop, si) * met.style_scale(sub_ctx_.style),
         let min_sup_shift = if (ctx.is_display(context)) met.at(met.sup1, si)
             else if (context.cramped) met.at(met.sup3, si)
             else met.at(met.sup2, si),
         let parent_scale = met.style_scale(context.style),
         let sup_font_scale = if (has_sup) met.style_scale(sup_ctx.style) / parent_scale else 1.0,
         let sub_font_scale = if (has_sub) met.style_scale(sub_ctx_.style) / parent_scale else 1.0,
         let supsub_box = if (has_sup and has_sub)
             render_both(base_box, sup_box, sub_box, init_sup_shift, init_sub_shift,
                         min_sup_shift, x_height, rule_width, si,
                         sup_font_scale, sub_font_scale)
         else if (has_sub)
             render_sub_only(base_box, sub_box, init_sub_shift, x_height, si,
                             is_char, sub_font_scale)
         else
             render_sup_only(base_box, sup_box, init_sup_shift, min_sup_shift, x_height,
                             context, sup_font_scale),
         script_pair(base_box, box.with_class(supsub_box, css.MSUBSUP)))
}

fn script_pair(base_box, script_box) {
    let render_h = if (script_box.render_height != null)
        max(base_box.height, script_box.render_height) else null
    let render_d = if (script_box.render_depth != null)
        max(base_box.depth, script_box.render_depth) else null
    // Propagate raw values when BOTH boxes expose them. Use the max for
    // each (the outer extent visible to the strut wrapper).
    let h_raw_pair = if (base_box.height_raw != null and script_box.height_raw != null)
        max(base_box.height_raw, script_box.height_raw) else null
    let d_raw_pair = if (base_box.depth_raw != null and script_box.depth_raw != null)
        max(base_box.depth_raw, script_box.depth_raw) else null
    {
        element: box.hbox([base_box, script_box]).element,
        elements: [base_box.element, script_box.element],
        height: max(base_box.height, script_box.height),
        depth: max(base_box.depth, script_box.depth),
        height_raw: h_raw_pair,
        depth_raw: d_raw_pair,
        render_height: render_h,
        render_depth: render_d,
        render_total: script_box.render_total,
        width: base_box.width + script_box.width,
        type: "mord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Case A: Both superscript and subscript
// ============================================================

fn render_both(base_box, sup_box, sub_box, init_sup, init_sub,
               min_sup, x_height, rule_width, si,
               sup_font_scale, sub_font_scale) {
    let sup_shift = max(init_sup, max(min_sup, sup_box.depth + 0.25 * x_height))
    let sub_shift = max(init_sub, met.at(met.sub2, si))

    // Rule 18e: check gap between sup bottom and sub top
    let gap = sup_shift - sup_box.depth - (sub_box.height - sub_shift)
    let min_gap = 4.0 * rule_width

    let adjusted = if (gap < min_gap)
        (let sub_adj = min_gap - gap,
         let new_sub = sub_shift + sub_adj,
         let psi = 0.8 * x_height - (sup_shift - sup_box.depth),
         if (psi > 0.0)
             {sup_shift: sup_shift + psi, sub_shift: new_sub - psi}
         else
             {sup_shift: sup_shift, sub_shift: new_sub})
    else
        {sup_shift: sup_shift, sub_shift: sub_shift}

    let sup_scale_str = util.fmt_pct(sup_font_scale)
    let sub_scale_str = util.fmt_pct(sub_font_scale)
    let sup_inner = sup_box.element
    let sub_inner = sub_box.element
    let sup_el = <span style: "display:block;font-size:" ++ sup_scale_str ++ ";line-height:1;text-align:left"; sup_inner>
    let sub_el = <span style: "display:block;font-size:" ++ sub_scale_str ++ ";line-height:1;text-align:left"; sub_inner>
    // inline-block baseline = last child (sub) baseline; shift it down
    let va = util.fmt_em(0.0 - adjusted.sub_shift)
    let container_style = "display:inline-block;vertical-align:" ++ va ++ ";text-align:left"
    let max_w = max(sup_box.width * sup_font_scale, sub_box.width * sub_font_scale)
    let el = <span style: container_style;
        sup_el
        sub_el
    >
    {
        element: el,
        height: adjusted.sup_shift + sup_box.height * sup_font_scale,
        depth: adjusted.sub_shift + sub_box.depth * sub_font_scale,
        width: max_w,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Case B: Subscript only
// ============================================================

fn render_sub_only(base_box, sub_box, init_sub, x_height, si, is_char, font_scale) {
    // MathLive's sub_shift uses the SCALED sub_box height (script-style),
    // not the parent-scale height. Lambda's sub_box reports parent-scale
    // dimensions, so we scale here before applying the formula.
    let sub_h_for_shift = sub_box.height * font_scale
    let sub_shift = max(init_sub, max(met.at(met.sub1, si), sub_h_for_shift - 0.8 * x_height))
    // child_h is the WRAPPER height — includes descent so the wrapper
    // accommodates descender content like y. CEIL@2 at script-style scale.
    // vlist_height uses the H-ONLY portion (no descent) — the descent is
    // accounted for separately by depth_holder.
    let sub_h_src = if (sub_box.height_raw != null) sub_box.height_raw else sub_box.height
    let sub_d_src = if (sub_box.depth_raw != null) sub_box.depth_raw else sub_box.depth
    let has_descender = sub_d_src > 0.0
    let child_h_raw = if (has_descender) (sub_h_src + sub_d_src) * font_scale
                     else sub_h_src * font_scale
    let child_h = ceil_em2(child_h_raw)
    let vlist_h_only = ceil_em2(sub_h_src * font_scale)
    let depth_holder_raw = sub_shift + sub_d_src * font_scale
    let depth_holder = ceil_em2(depth_holder_raw)
    let top = sub_shift - 3.0
    let vlist_height = vlist_h_only - sub_shift
    let inner_style = "height:" ++ util.fmt_em(child_h) ++ ";display:inline-block;font-size: " ++ util.fmt_pct(font_scale)
    // For compound sub_boxes (groups via hbox), strip the lm_base wrapper
    // so the inner span contains the atoms directly — matches MathLive.
    let inner_elements = box.elements_of(sub_box)
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(vlist_height);
                <span style: "top:" ++ util.fmt_em(top) ++ ";margin-right:0.05em";
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: inner_style;
                        for (e in inner_elements) e
                    >
                >
            >
            <span class: css.VLIST_S; "​">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(depth_holder)>
        >
    >
    {
        element: el,
        height: 0.0,
        depth: depth_holder_raw,
        height_raw: 0.0,
        depth_raw: depth_holder_raw,
        width: sub_box.width * font_scale,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Case C: Superscript only
// ============================================================

fn render_sup_only(base_box, sup_box, init_sup, min_sup, x_height, context, font_scale) {
    let tall_script = sup_box.height > 0.72
    let tall_base = base_box.height > 0.72
    let in_fraction_child = context.fraction_child == true
    let compound_fraction_script = in_fraction_child and sup_box.width > 0.8
    let numeric_x_script = is_mathit_x_box(base_box) and is_numeric_script_box(sup_box)
    let script_height = script_inner_height(sup_box, tall_script, in_fraction_child)
    let vlist_height = if (compound_fraction_script and context.cramped == true) 0.76
        else if (in_fraction_child and context.cramped == true) 0.75
        else if (in_fraction_child) 0.82
        else if (tall_script) 1.16 else if (tall_base) 0.94
        else if (numeric_x_script) 0.87
        else 0.72
    let top = if (in_fraction_child and context.cramped == true) -3.28
        else if (in_fraction_child) -3.36
        else 0.0 - (3.0 + if (tall_script) 0.48 else if (tall_base) 0.47 else 0.41)
    let inner_style = "height:" ++ util.fmt_em(script_height) ++ ";display:inline-block;font-size: 70%"
    let sup_elements = merge_script_elements(box.elements_of(sup_box))
    let el = <span class: css.VLIST_T;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em(vlist_height);
                <span style: "top:" ++ util.fmt_em(top) ++ ";margin-right:0.05em";
                    <span class: css.PSTRUT, style: "height:3em">
                    <span style: inner_style;
                        for (el in sup_elements) el
                    >
                >
            >
        >
    >
    {
        element: el,
        height: vlist_height,
        depth: 0.0,
        render_height: if (in_fraction_child) vlist_height else null,
        render_depth: if (in_fraction_child) 0.0 else null,
        render_total: if (in_fraction_child)
            (if (compound_fraction_script) 1.01
             else if (context.cramped == true) 1.0 else 1.01)
            else null,
        width: sup_box.width * font_scale,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

fn is_mathit_x_box(bx) {
    // Any single italic Latin letter base. MathLive's 0.87 vlist_height
    // applies to all letter+digit superscript pairs, not only "x".
    let els = box.elements_of(bx)
    len(els) == 1 and els[0] is element and els[0].class == css.MATHIT and
    len(els[0]) == 1 and els[0][0] is string and
    (let ch = string(els[0][0]),
     len(ch) == 1 and ((ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z")))
}

fn is_numeric_script_box(bx) {
    let els = box.elements_of(bx)
    len(els) == 1 and els[0] is element and els[0].class == css.CMR and
    len(els[0]) == 1 and els[0][0] is string and is_digit_text(string(els[0][0]), 0)
}

fn is_digit_text(text, i) {
    if (len(text) == 0) false
    else if (i >= len(text)) true
    else {
        let ch = slice(text, i, i + 1)
        (ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
         ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9") and
            is_digit_text(text, i + 1)
    }
}

fn script_inner_height(sup_box, tall_script, in_fraction_child) {
    if (in_fraction_child and sup_box.width > 0.8) 0.6
    else if (in_fraction_child) 0.46
    else if (tall_script) 1.05
    else if (sup_box.element is element and sup_box.element.class == css.CMR) 0.46
    else 0.31
}

fn can_merge_script_text(a, b) {
    if (not (a is element) or not (b is element)) false
    else if (len(a) != 1 or len(b) != 1) false
    else if (not (a[0] is string) or not (b[0] is string)) false
    else a.class == b.class and a.style == null and b.style == null
}

fn merge_script_two(a, b) {
    let txt = string(a[0]) ++ string(b[0])
    <span class: a.class; txt>
}

fn merge_script_scan(items, i, acc) {
    if (i >= len(items)) acc
    else
        (let curr = items[i],
         let last_idx = len(acc) - 1,
         let prev = acc[last_idx],
         if (can_merge_script_text(prev, curr))
             (let combined = merge_script_two(prev, curr),
              let head = if (last_idx > 0) slice(acc, 0, last_idx) else [],
              merge_script_scan(items, i + 1, head ++ [combined]))
         else
              merge_script_scan(items, i + 1, acc ++ [curr]))
}

fn merge_script_elements(items) {
    if (len(items) <= 1) items
    else merge_script_scan(items, 1, [items[0]])
}
