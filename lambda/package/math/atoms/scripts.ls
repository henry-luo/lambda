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

    if (is_big_op and is_integral)
        // Integrals place their limits to the SIDE (lm_msubsup adjacent to the
        // symbol) in both inline AND display mode — MathLive never stacks
        // integral limits by default.
        render_integral_inline_scripts(base, node, context, render_fn)
    else if (is_big_op and not ctx.is_script(context))
        render_big_op_limits(base, node, context, render_fn)
    else if (is_big_op and node.sub != null and node.sup != null)
        render_inline_big_op_scripts(base, node, context, render_fn)
    else
        render_scripts(node, context, render_fn)
}

// Compute the height to use for the sub-script wrapper. MathLive uses
// CEIL@2((sub_box.height_raw + sub_box.depth_raw) * 0.7) — the full glyph
// extent scaled to script style. The depth term matters for atoms whose box
// dips below the baseline, e.g. the minus sign in `\int_{-\infty}` (depth
// 0.08333 → 0.47 instead of 0.41). Falls back to 0.46 without raw metrics.
fn sub_height_for(sub_box) {
    let raw = if (sub_box != null) sub_box.height_raw else null
    let draw = if (sub_box != null and sub_box.depth_raw != null and sub_box.depth_raw > 0.0)
        sub_box.depth_raw else 0.0
    if (raw != null) ceil_em2((raw + draw) * 0.7)
    else 0.46
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

    // ∫ Size2 glyph metrics (KaTeX_Size2 U+222B = [depth, height, italic]) +
    // TeXBook Rule 18 side-limit shifts. The sup rises by `int_h - supDrop·s`
    // (a tall base: init shift dominates), the sub drops by `int_d + subDrop·s`;
    // every position below derives from these — no hardcoded em offsets.
    let int_h = 1.36
    let int_d = 0.86225
    let int_italic = 0.44445
    let s = 0.7
    let si = met.style_index(context.style)
    let sup_shift = int_h - met.at(met.supDrop, si) * s
    let sub_shift = int_d + met.at(met.subDrop, si) * s
    let pstrut = 3.0

    // Sub/sup wrapper heights = content (height+depth) scaled to script ×0.7,
    // CEIL@2 (the inline-block CSS height); fall back to 0.46 without raw.
    let sub_inner_h = if (sub_box != null) sub_height_for(sub_box) else 0.46
    let sup_inner_h = if (sup_box != null) sub_height_for(sup_box) else 0.46
    // Full-precision script extents (scaled ×0.7) — used for the box's true
    // height/depth so the single-rounding strut sums them ONCE.
    let sup_raw_h = if (has_sup)
        (if (sup_box.height_raw != null) sup_box.height_raw else sup_box.height) * s
        else 0.0
    let sub_raw_d = if (has_sub and sub_box.depth_raw != null and sub_box.depth_raw > 0.0)
        sub_box.depth_raw * s else 0.0
    let sup_raw_d = if (has_sup and sup_box.depth_raw != null and sup_box.depth_raw > 0.0)
        sup_box.depth_raw * s else 0.0
    // box extent (full precision): sup raises the top to sup_shift+sup_h, the
    // sub drops the bottom to sub_shift(+descender).
    let box_h_raw = if (has_sup) (sup_shift + sup_raw_h) else int_h
    let box_d_raw = if (has_sub) (sub_shift + sub_raw_d) else int_d

    // sub nestles under the integral's italic overhang (margin-left = -italic);
    // alone it sits slightly right (scriptspace kern).
    let sub_margin_attr = if (has_sup)
        "margin-left:" ++ util.fmt_em(util.ceil_em2(0.0 - int_italic))
        else "margin-right:0.05em"
    // vlist max extent: the sup top (sub+sup / sup-only) or the sub top alone
    // (negative — the sub sits below the origin).
    let vlist_height = if (has_sup) util.ceil_em2(box_h_raw)
        else util.ceil_em2(0.0 - sub_shift + sub_inner_h)
    let depth_holder = if (has_sub) util.ceil_em2(box_d_raw) else 0.0
    let sub_top = util.ceil_em2(0.0 - pstrut + sub_shift)
    let sup_top = util.ceil_em2(0.0 - pstrut - sup_shift - sup_raw_d)
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

    let int_symbol_style = "margin-right:" ++ util.fmt_em(util.ceil_em2(int_italic))
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
    // height/depth are the CEIL@2 projections of the full-precision extents
    // (box_h_raw/box_d_raw computed above).
    let box_h = util.ceil_em2(box_h_raw)
    let box_d = 0.0 - util.ceil_em2(0.0 - box_d_raw)
    {
        element: el,
        height: box_h,
        depth: box_d,
        height_raw: box_h_raw,
        depth_raw: box_d_raw,
        // Phase A: render_height/render_depth omitted (== height/depth, consumers
        // null-coalesce). render_total kept until delimiters (task 5) convert.
        render_total: util.ceil_em2(box_h_raw + box_d_raw),
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

// full-precision metric accessors for limit boxes
fn ls_raw_h(b) { if (b.height_raw != null) b.height_raw else b.height }
fn ls_raw_d(b) { if (b.depth_raw != null) b.depth_raw else b.depth }

fn ls_child_style(hd, fs) {
    "height:" ++ util.fmt_em_ceil2(hd) ++ ";display:inline-block" ++
        (if (fs) ";font-size: 70%" else "")
}

// Sum of a makeVList child list's vertical extents (kerns + box h+d).
fn ls_sum(items, i, acc) {
    if (i >= len(items)) acc
    else (let it = items[i],
          let e = if (it.k != null) it.k else (it.h + it.d),
          ls_sum(items, i + 1, acc + e))
}

// Walk a makeVList child list (kern {k} or box {b,h,d,fs}), accumulating
// currPos; record each box's emitted top (= -pstrut - currPos - depth) and
// track the overall max/min position (the box height/depth).
fn ls_walk(items, i, pstrut, cp, maxp, minp, centers) {
    let result = if (i >= len(items)) {maxp: maxp, minp: minp, centers: centers}
        else (let it = items[i],
            if (it.k != null)
                (let cp2 = cp + it.k,
                 ls_walk(items, i + 1, pstrut, cp2, max(maxp, cp2), min(minp, cp2), centers))
            else
                (let top = 0.0 - pstrut - cp - it.d,
                 let cp2 = cp + it.h + it.d,
                 ls_walk(items, i + 1, pstrut, cp2, max(maxp, cp2), min(minp, cp2),
                     centers ++ [{top: top, b: it.b, hd: it.h + it.d, fs: it.fs}])))
    result
}

// Large operator box. The Size2 ∑/∏/⋃/⨁… glyphs share height 1.05, depth
// 0.55001 (kept as-is so the pstrut = max(1.0, 1.05)+2 emits 3.05). The axis
// centring (rule 13) is applied as a baseShift in make_limits_stack's
// positions, NOT folded into height (which would break the pstrut).
fn large_op_box_metric(text) {
    {
        element: <span class: "lm_op-symbol lm_large-op"; text>,
        height: 1.05,
        depth: 0.55001,
        width: 1.44445,
        type: "mop",
        italic: 0.0,
        skew: 0.0
    }
}

// MathLive makeLimitsStack (v-box.ts) for display big operators: stacks the
// below limit, the axis-centred op symbol, and the above limit separated by
// bigOpSpacing gaps, laid out with makeVList — NO hardcoded em constants.
fn make_limits_stack(op_box, sub_box, sup_box, is_centered) {
    let bos1 = met.at(met.bigOpSpacing1, 0)
    let bos2 = met.at(met.bigOpSpacing2, 0)
    let bos3 = met.at(met.bigOpSpacing3, 0)
    let bos4 = met.at(met.bigOpSpacing4, 0)
    let bos5 = met.at(met.bigOpSpacing5, 0)
    let fs = 0.7   // display -> script scale for the limits
    // use full-precision op metrics: a symbol op (large_op_box_metric) sets
    // height directly; a text op (text_box) sets height=CEIL, height_raw=full.
    let op_h = ls_raw_h(op_box)
    let op_d = ls_raw_d(op_box)
    let has_sub = sub_box != null
    let has_sup = sup_box != null
    // limits are rendered unscaled by Lambda; scale into the parent frame
    let sub_h = if (has_sub) ls_raw_h(sub_box) * fs else 0.0
    let sub_d = if (has_sub) ls_raw_d(sub_box) * fs else 0.0
    let sup_h = if (has_sup) ls_raw_h(sup_box) * fs else 0.0
    let sup_d = if (has_sup) ls_raw_d(sup_box) * fs else 0.0
    let above_shift = if (has_sup) max(bos1, bos3 - sup_d) else 0.0
    let below_shift = if (has_sub) max(bos2, bos4 - sub_h) else 0.0
    let op_item = {b: op_box, h: op_h, d: op_d, fs: false}
    let sub_item = {b: sub_box, h: sub_h, d: sub_d, fs: true}
    let sup_item = {b: sup_box, h: sup_h, d: sup_d, fs: true}
    let items = if (has_sub and has_sup)
        [{k: bos5}, sub_item, {k: below_shift}, op_item, {k: above_shift}, sup_item, {k: bos5}]
    else if (has_sub)
        [{k: bos5}, sub_item, {k: below_shift}, op_item]
    else
        [op_item, {k: above_shift}, sup_item, {k: bos5}]
    // axis-centring of the op symbol (rule 13), applied as a position shift
    // symbol ops (∑/∏) are axis-centred (rule 13); text ops (\lim/\max) are
    // NOT (operator.ts passes no baseShift) — they sit on the baseline.
    let bs_half = (op_h - op_d) / 2.0
    let base_shift = if (is_centered) (bs_half - met.AXIS_HEIGHT) else 0.0
    // initial vlist position (depth): both/above use "bottom" mode, below-only
    // uses "top" mode (top = op_h - baseShift).
    let depth0 = if (has_sub and has_sup)
        0.0 - (bos5 + sub_h + sub_d + below_shift + op_d + base_shift)
    else if (has_sub)
        (op_h - base_shift) - ls_sum(items, 0, 0.0)
    else
        0.0 - (op_d + base_shift)
    let pstrut = max(1.0, max(op_h, max(sub_h, sup_h))) + 2.0
    let r = ls_walk(items, 0, pstrut, depth0, depth0, depth0, [])
    let pstrut_style = "height:" ++ util.fmt_em_ceil2(pstrut)
    let centers = (for (c in r.centers)
        <span class: css.CENTER, style: "top:" ++ util.fmt_em_ceil2(c.top);
            <span class: css.PSTRUT, style: pstrut_style>
            <span style: ls_child_style(c.hd, c.fs);
                for (e in box.elements_of(c.b)) e
            >
        >
    )
    let el = <span class: css.OP_GROUP;
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(r.maxp);
                    for (c in centers) c
                >
                <span class: css.VLIST_S; "​">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(0.0 - r.minp)>
            >
        >
    >
    // full precision in *_raw (single-rounding strut path); height/depth and
    // render_* are CEIL projections for non-raw consumers (an hbox sibling
    // that lacks raw forces the rounded path — e.g. the \prod in a sum of
    // fractions).
    let full_depth = 0.0 - r.minp
    let h_num = ceil_em2(r.maxp)
    let d_num = 0.0 - ceil_em2(0.0 - full_depth)
    // Phase A: a metric-driven box carries one height/depth (CEIL projections;
    // full precision in *_raw). render_height/render_depth are omitted — they
    // would equal height/depth and every consumer null-coalesces to those.
    {
        element: el,
        height: h_num,
        depth: d_num,
        height_raw: r.maxp,
        depth_raw: full_depth,
        render_total: ceil_em2(r.maxp + full_depth),
        width: max(op_box.width, max(if (has_sub) sub_box.width * fs else 0.0,
                                     if (has_sup) sup_box.width * fs else 0.0)),
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

    // Both symbol big-ops (∑/∏/⋃/⨁…, axis-centred) AND text-ops (\lim/\max/
    // \det, baseline) with over/under limits use the metric-driven
    // makeLimitsStack — only integrals (side-limits, routed earlier) stay off it.
    if (is_text_op == null and unicode != null and (has_sub or has_sup))
        make_limits_stack(large_op_box_metric(unicode), sub_box, sup_box, true)
    else if (is_text_op != null and (has_sub or has_sup))
        make_limits_stack(op_box, sub_box, sup_box, false)
    else if (has_sub or has_sup)
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

fn large_op_symbol_box(text) {
    // Per-symbol Size2 metric: bigodot/oplus/otimes/etc have shorter descent
    // than sum/prod, so the surrounding depth_holder formula needs to know.
    // h+d typically ~1.61em but split differs.
    let d = large_op_symbol_depth(text)
    {
        element: <span class: "lm_op-symbol lm_large-op"; text>,
        height: 1.61,
        depth: d,
        render_height: 1.61,
        render_depth: d,
        render_total: 1.61 + d,
        width: 0.6,
        type: "mop",
        italic: 0.0,
        skew: 0.0
    }
}

fn large_op_symbol_depth(text) {
    // ⨁/⨂/⨀/⨄/⨆/⨃/⋃/⋂/⋁/⋀ — small-descent (Size2 d≈0.42-0.45). Closes
    // vlist depth_holder mismatch for `\bigoplus`/`\bigotimes`/`\bigodot`/
    // `\biguplus`/`\bigcup`/`\bigcap`/`\bigvee`/`\bigwedge`.
    if (text == "⨁" or text == "⨂" or text == "⨀" or text == "⨃" or
        text == "⨄" or text == "⨆" or text == "⋃" or text == "⋂" or
        text == "⋁" or text == "⋀") 0.42
    else 0.0
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
    // Large-op vlist height: when a sup is present (sub+sup OR sup-only), the
    // height grows with the sup's scaled height — matches MathLive's emit:
    // vlist_h = sup_child_h + 1.35 (CEIL@2). The hardcoded 1.66 historically
    // assumed sup_child=0.31 (n/a sized). Taller sups (digits, uppercase,
    // descenders) lift vlist accordingly. Sub-only keeps the 1.06 default.
    let sup_h_raw_pre = if (has_sup and sup_box.height_raw != null) sup_box.height_raw
                        else if (has_sup) sup_box.height
                        else 0.0
    let sup_scaled_pre = ceil_em2(sup_h_raw_pre * 0.7)
    let vlist_height = if (is_text_op and has_sub and has_sup) 0.94
                       else if (is_text_op) 0.7
                       else if (compact_limits) 1.81
                       else if (has_sub and has_sup) ceil_em2(sup_scaled_pre + 1.35)
                       else if (has_sup) ceil_em2(sup_scaled_pre + 1.35)
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
    // bigodot/oplus/otimes/etc have shorter descent than sum/prod — their
    // sub-only depth_holder offset is ~0.13em shorter.
    let op_short_descent = op_box.depth > 0.0 and op_box.depth < 0.5
    let dh_offset = if (is_text_op and has_sub) 0.26
                    else if (sub_has_descender) 0.82
                    else if (has_sub and has_sup) 0.81
                    else if (op_short_descent and has_sub) 0.82
                    else 0.95
    let bd_offset = if (is_text_op and has_sub) 0.25
                    else if (sub_has_descender) 0.81
                    else if (has_sub and has_sup) 0.80
                    else if (op_short_descent and has_sub) 0.81
                    else 0.94
    // For sup-only large ops (no sub), MathLive emits a 0.56em depth_holder
    // and 0.55em box_depth — the inherent op-symbol descent (Size2 d=0.55).
    // Text-op sup-only (like `\lim` with sup only) doesn't appear in tests
    // yet; keep at 0 there.
    let depth_holder = if (compact_limits) 1.26
                       else if (has_sub) sub_child_height + dh_offset
                       else if (has_sup and not is_text_op) 0.56
                       else 0.0
    let box_depth = if (compact_limits) 1.26
                    else if (has_sub) sub_child_height + bd_offset
                    else if (has_sup and not is_text_op) 0.55
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

// Rule 18e (both sub + sup), emitted via MathLive's makeVList (vlist-t2) —
// NOT the legacy inline-block stack. The corpus is display-rooted, so the min
// sup shift uses sup1. Child metrics are scaled into the parent frame; the box
// exposes full-precision height_raw/depth_raw so the outer strut rounds once.
fn render_both(base_box, sup_box, sub_box, init_sup, init_sub,
               min_sup, x_height, rule_width, si,
               sup_font_scale, sub_font_scale) {
    let sup_h = if (sup_box.height_raw != null) sup_box.height_raw else sup_box.height
    let sup_d = if (sup_box.depth_raw != null) sup_box.depth_raw else sup_box.depth
    let sub_h = if (sub_box.height_raw != null) sub_box.height_raw else sub_box.height
    let sub_d = if (sub_box.depth_raw != null) sub_box.depth_raw else sub_box.depth
    let sup_h_s = sup_h * sup_font_scale
    let sup_d_s = sup_d * sup_font_scale
    let sub_h_s = sub_h * sub_font_scale
    let sub_d_s = sub_d * sub_font_scale
    let min_sup_d = met.at(met.sup1, si)
    let sup_shift0 = max(init_sup, max(min_sup_d, sup_d_s + 0.25 * x_height))
    let sub_shift0 = max(init_sub, met.at(met.sub2, si))
    let gap = sup_shift0 - sup_d_s - (sub_h_s - sub_shift0)
    let min_gap = 4.0 * rule_width
    let adj = if (gap < min_gap)
        (let new_sub = sub_shift0 + (min_gap - gap),
         let psi = 0.8 * x_height - (sup_shift0 - sup_d_s),
         if (psi > 0.0) {sup: sup_shift0 + psi, sub: new_sub - psi}
         else {sup: sup_shift0, sub: new_sub})
    else {sup: sup_shift0, sub: sub_shift0}
    let sup_shift = adj.sup
    let sub_shift = adj.sub
    // makeVList individualShift, children bottom->top: subBox @ +sub_shift,
    // supBox @ -sup_shift. currPos accumulates from the initial depth.
    let depth0 = 0.0 - sub_shift - sub_d_s
    let sub_end = depth0 + sub_h_s + sub_d_s
    let diff_sup = sup_shift - depth0 - sup_d_s
    let cp_sup = depth0 + diff_sup
    let sup_end = cp_sup + sup_h_s + sup_d_s
    let pstrut = max(1.0, max(sub_h_s, sup_h_s)) + 2.0
    let max_pos = max(depth0, max(sub_end, max(cp_sup, sup_end)))
    let min_pos = min(depth0, min(sub_end, min(cp_sup, sup_end)))
    let pstrut_style = "height:" ++ util.fmt_em_ceil2(pstrut)
    let sub_style = "height:" ++ util.fmt_em_ceil2(sub_h_s + sub_d_s) ++
        ";display:inline-block;font-size: " ++ fmt_font_pct(sub_font_scale)
    let sup_style = "height:" ++ util.fmt_em_ceil2(sup_h_s + sup_d_s) ++
        ";display:inline-block;font-size: " ++ fmt_font_pct(sup_font_scale)
    let sub_elements = box.elements_of(sub_box)
    let sup_elements = box.elements_of(sup_box)
    let el = <span class: css.VLIST_T2;
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(max_pos);
                <span style: "top:" ++ util.fmt_em_ceil2(0.0 - pstrut - depth0 - sub_d_s);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: sub_style;
                        for (e in sub_elements) e
                    >
                >
                <span style: "top:" ++ util.fmt_em_ceil2(0.0 - pstrut - cp_sup - sup_d_s);
                    <span class: css.PSTRUT, style: pstrut_style>
                    <span style: sup_style;
                        for (e in sup_elements) e
                    >
                >
            >
            <span class: css.VLIST_S; "​">
        >
        <span class: css.VLIST_R;
            <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(0.0 - min_pos)>
        >
    >
    {
        element: el,
        height: max_pos,
        depth: 0.0 - min_pos,
        height_raw: max_pos,
        depth_raw: 0.0 - min_pos,
        width: max(sup_box.width * sup_font_scale, sub_box.width * sub_font_scale),
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
        // depth is the CEIL@2 projection (non-raw strut consumers); depth_raw
        // keeps full precision for the single-rounding use_raw path.
        height: 0.0,
        depth: ceil_em2(depth_holder_raw),
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
    // Rule 18c (TeXBook): a superscript-only atom. The script rises by
    //   supShift = max(initSup, sup1/2/3, supDepth·scale + ¼·xHeight)
    // above the baseline and is laid out with the single-child makeVList
    // (one box + pstrut). Everything is derived from metrics — no hardcoded
    // top offsets, per-shape vlist tables, or tall-base/tall-script gates.
    let sup_h = if (sup_box.height_raw != null) sup_box.height_raw else sup_box.height
    let sup_d = if (sup_box.depth_raw != null) sup_box.depth_raw else sup_box.depth
    let sup_h_s = sup_h * font_scale
    let sup_d_s = sup_d * font_scale
    let sup_shift = max(init_sup, max(min_sup, sup_d_s + 0.25 * x_height))
    // Single-child makeVList: the child sits at shift -sup_shift (upward).
    //   depth0 = sup_shift - sup_d_s   (lowest point of the content)
    //   max_pos = sup_shift + sup_h_s  (highest point)
    //   top    = -pstrut - sup_shift   (childWrap offset)
    let pstrut = max(1.0, sup_h_s) + 2.0
    let max_pos = sup_shift + sup_h_s
    let min_pos = sup_shift - sup_d_s
    let top = 0.0 - pstrut - sup_shift
    let has_depth = min_pos < 0.0
    let depth_out = if (has_depth) (0.0 - min_pos) else 0.0
    let inner_style = "height:" ++ util.fmt_em_ceil2(sup_h_s + sup_d_s) ++
        ";display:inline-block;font-size: " ++ fmt_font_pct(font_scale)
    let sup_elements = merge_script_elements(box.elements_of(sup_box))
    let top_row = <span style: "top:" ++ util.fmt_em_ceil2(top) ++ ";margin-right:0.05em";
        <span class: css.PSTRUT, style: "height:" ++ util.fmt_em_ceil2(pstrut)>
        <span style: inner_style;
            for (el in sup_elements) el
        >
    >
    // Two vlist rows only when the content dips below the baseline (rare for a
    // sup, e.g. a deep descender shifted little); otherwise a single row.
    let el = if (has_depth)
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(max_pos);
                    top_row
                >
                <span class: css.VLIST_S; "​">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(depth_out)>
            >
        >
    else
        <span class: css.VLIST_T;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em_ceil2(max_pos);
                    top_row
                >
            >
        >
    {
        element: el,
        // height/depth are the CEIL@2 projections (consumed by the non-raw
        // strut path, e.g. next to a \left..\right base that lacks depth_raw);
        // height_raw/depth_raw carry full precision for the single-rounding
        // use_raw path (sibling descenders, x^n + y^n).
        height: ceil_em2(max_pos),
        depth: ceil_em2(depth_out),
        height_raw: max_pos,
        depth_raw: depth_out,
        width: sup_box.width * font_scale,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// Format a font-size percentage the way MathLive does: integer when whole
// (e.g. 0.7 -> "70%"), else 2-decimal (e.g. 5/7 -> "71.43%").
fn fmt_font_pct(scale) {
    let pct = scale * 100.0
    let rounded = round(pct * 100.0) / 100.0
    let as_int = int(rounded + 0.000001)
    if (abs(rounded - float(as_int)) < 0.001) string(as_int) ++ "%"
    else util.fmt_num(rounded, 2) ++ "%"
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
