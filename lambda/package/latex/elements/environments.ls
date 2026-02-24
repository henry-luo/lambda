// latex/elements/environments.ls — Environment rendering
// Handles: quote, center, flushleft, flushright, verbatim, abstract,
// minipage, figure, multicols, and generic fallback environments.

import util: .lambda.package.latex.util
import ctx_mod: .lambda.package.latex.context

// helper: check if element is caption or label (to skip in figure rendering)
fn is_caption_or_label(c) {
    if (c is element) {
        let t = string(name(c))
        if (t == "caption") true
        else if (t == "label") true
        else false
    } else { false }
}

// ============================================================
// Quote / quotation / verse
// ============================================================

pub fn render_quote(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <blockquote class: "latex-quote"; for c in children.items { c }>
    {result: result, ctx: children.ctx}
}

// ============================================================
// Center / flushleft / flushright
// ============================================================

pub fn render_center(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-center", style: "text-align:center";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

pub fn render_flushleft(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-flushleft", style: "text-align:left";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

pub fn render_flushright(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-flushright", style: "text-align:right";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Verbatim / lstlisting
// ============================================================

pub fn render_verbatim(node, ctx) {
    let text = util.text_of(node)
    let result = <pre class: "latex-verbatim"; <code; text>>
    {result: result, ctx: ctx}
}

// ============================================================
// Abstract
// ============================================================

pub fn render_abstract(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-abstract";
        <div class: "abstract-title"; "Abstract">
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Figure
// ============================================================

pub fn render_figure(node, ctx, render_fn) {
    let new_ctx = ctx_mod.step_counter(ctx, "figure")
    let fig_num = new_ctx.counters.figure

    let children = render_children(node, new_ctx, render_fn)

    // extract caption if present
    let caption = find_caption(node)
    let cap_text = if (caption != null) util.text_of(caption) else null
    let caption_el = if (cap_text != null) make_figcaption(fig_num, cap_text) else null

    // extract label if present
    let label_node = util.find_child(node, 'label')
    let label_name = if (label_node != null) util.text_of(label_node) else null
    let ctx2 = if (label_name != null)
        ctx_mod.add_label(children.ctx, label_name, "figure", string(fig_num), util.slugify("fig-" ++ label_name))
        else children.ctx

    let result = <figure class: "latex-figure";
        for c in children.items {
            if (not is_caption_or_label(c)) { c }
        }
        if caption_el != null { caption_el }
    >
    {result: result, ctx: ctx2}
}

fn make_figcaption(fig_num, cap_text) {
    let label = <strong; "Figure " ++ string(fig_num) ++ ": ">
    <figcaption;
        label
        cap_text
    >
}

fn find_caption(node) {
    util.find_child(node, 'caption')
}

// ============================================================
// Minipage
// ============================================================

pub fn render_minipage(node, ctx, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-minipage";
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Multicols
// ============================================================

pub fn render_multicols(node, ctx, render_fn) {
    // extract column count from first curly_group
    let col_group = util.find_child(node, 'curly_group')
    let cols = if (col_group != null) util.text_of(col_group) else "2"
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-multicols", style: "column-count:" ++ trim(cols);
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Generic / fallback environment
// ============================================================

pub fn render_generic_env(node, ctx, env_name, render_fn) {
    let children = render_children(node, ctx, render_fn)
    let result = <div class: "latex-env-" ++ env_name;
        for c in children.items { c }
    >
    {result: result, ctx: children.ctx}
}

// ============================================================
// Helper: render all children threading context
// ============================================================

fn render_children(node, ctx, render_fn) {
    let n = len(node)
    if (n == 0) { {items: [], ctx: ctx} }
    else { render_children_rec(node, 0, n, [], ctx, render_fn) }
}

fn render_children_rec(node, i, n, acc, ctx, render_fn) {
    if (i >= n) { {items: acc, ctx: ctx} }
    else {
        let child = node[i]
        let rendered = render_fn(child, ctx)
        let new_acc = if (rendered.result != null) {
            if (rendered.result is array) acc ++ (for (r in rendered.result where r != null) r)
            else acc ++ [rendered.result]
        } else {
            acc
        }
        render_children_rec(node, i + 1, n, new_acc, rendered.ctx, render_fn)
    }
}
