// math/atoms/scripts.ls — Superscript/subscript rendering
// Implements TeXBook Rules 18a-f from Appendix G

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css
import met: .lambda.package.math.metrics
import util: .lambda.package.math.util

// ============================================================
// Script rendering (superscript, subscript, or both)
// ============================================================

// render a subsup AST node
// node has: base, sub, sup, modifier attributes
// render_fn: top-level render function for recursive calls
pub fn render(node, context, render_fn) {
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
         let supsub_box = if (has_sup and has_sub)
             render_both(base_box, sup_box, sub_box, init_sup_shift, init_sub_shift,
                         min_sup_shift, x_height, rule_width, si, scriptspace)
         else if (has_sub)
             render_sub_only(base_box, sub_box, init_sub_shift, x_height, si,
                             scriptspace, is_char)
         else
             render_sup_only(sup_box, init_sup_shift, min_sup_shift, x_height,
                             scriptspace),
         box.hbox([base_box, box.with_class(supsub_box, css.MSUBSUP)]))
}

// ============================================================
// Case A: Both superscript and subscript
// ============================================================

fn render_both(base_box, sup_box, sub_box, init_sup, init_sub,
               min_sup, x_height, rule_width, si, scriptspace) {
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

    box.vbox([
        {box: sub_box, shift: adjusted.sub_shift},
        {box: sup_box, shift: 0.0 - adjusted.sup_shift}
    ])
}

// ============================================================
// Case B: Subscript only
// ============================================================

fn render_sub_only(base_box, sub_box, init_sub, x_height, si, scriptspace, is_char) {
    let sub_shift = max(init_sub, max(met.at(met.sub1, si), sub_box.height - 0.8 * x_height))
    box.vbox([
        {box: sub_box, shift: sub_shift}
    ])
}

// ============================================================
// Case C: Superscript only
// ============================================================

fn render_sup_only(sup_box, init_sup, min_sup, x_height, scriptspace) {
    let sup_shift = max(init_sup, max(min_sup, sup_box.depth + 0.25 * x_height))
    box.vbox([
        {box: sup_box, shift: 0.0 - sup_shift}
    ])
}
