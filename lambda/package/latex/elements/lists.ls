// latex/elements/lists.ls — List environments: itemize, enumerate, description
// Handles \begin{itemize}, \begin{enumerate}, \begin{description}

import util: .lambda.package.latex.util
import ctx_mod: .lambda.package.latex.context

// helper: check if child is an <item> element
fn is_item(child) {
    if (child is element) {
        if (string(name(child)) == "item") true else false
    } else { false }
}

// ============================================================
// Itemize (unordered list)
// ============================================================

pub fn render_itemize(node, ctx, render_fn) {
    let inner_ctx = ctx_mod.enter_list(ctx)
    let items = split_items(node, inner_ctx, render_fn)
    let result = <ul class: "latex-itemize";
        for li in items.elements { li }
    >
    {result: result, ctx: items.ctx}
}

// ============================================================
// Enumerate (ordered list)
// ============================================================

pub fn render_enumerate(node, ctx, render_fn) {
    let inner_ctx = ctx_mod.enter_list(ctx)

    // determine counter name based on nesting depth
    let counter = match inner_ctx.list_depth {
        case 1: "enumi"
        case 2: "enumii"
        default: "enumiii"
    }

    let items = split_items_enum(node, inner_ctx, render_fn, counter)
    let result = <ol class: "latex-enumerate";
        for li in items.elements { li }
    >
    {result: result, ctx: items.ctx}
}

// ============================================================
// Description list
// ============================================================

pub fn render_description(node, ctx, render_fn) {
    let inner_ctx = ctx_mod.enter_list(ctx)
    let items = split_description_items(node, inner_ctx, render_fn)
    let result = <dl class: "latex-description";
        for item in items.elements { item }
    >
    {result: result, ctx: items.ctx}
}

// ============================================================
// Item splitting — split children at <item> separator elements
// ============================================================

// split unordered list items
fn split_items(node, ctx, render_fn) {
    let flat = flatten_paragraphs(node)
    let n = len(flat)
    collect_items(flat, 0, n, [], [], ctx, render_fn)
}

pub fn flatten_paragraphs_pub(node) {
    flatten_paragraphs(node)
}

// flatten paragraph wrappers: if direct children of node are <paragraph> elements,
// inline their children into a flat array for item splitting
fn flatten_paragraphs(node) {
    let n = len(node)
    flatten_para_rec(node, 0, n, [])
}

fn flatten_para_rec(node, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = node[i]
        if (is_paragraph(child)) {
            let inner_n = len(child)
            let inner = flatten_inner(child, 0, inner_n, [])
            flatten_para_rec(node, i + 1, n, acc ++ inner)
        } else {
            flatten_para_rec(node, i + 1, n, acc ++ [child])
        }
    }
}

fn is_paragraph(child) {
    if (child is element) {
        if (string(name(child)) == "paragraph") true else false
    } else { false }
}

fn flatten_inner(el, i, n, acc) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        flatten_inner(el, i + 1, n, acc ++ [child])
    }
}

// recursive helper: walk children, accumulate content between <item> separators
fn collect_items(node, i, n, current_content, acc_items, ctx, render_fn) {
    if (i >= n) {
        let final_items = if (len(current_content) > 0)
            acc_items ++ [<li; for c in current_content { c }>]
            else acc_items
        {elements: final_items, ctx: ctx}
    } else {
        let child = node[i]
        if (is_item(child)) {
            let flushed = if (len(current_content) > 0)
                acc_items ++ [<li; for c in current_content { c }>]
                else acc_items
            collect_items(node, i + 1, n, [], flushed, ctx, render_fn)
        } else {
            let rendered = render_fn(child, ctx)
            let new_content = if (rendered.result != null)
                current_content ++ [rendered.result]
                else current_content
            collect_items(node, i + 1, n, new_content, acc_items, rendered.ctx, render_fn)
        }
    }
}

// split enumerated list items (with counter stepping)
fn split_items_enum(node, ctx, render_fn, counter) {
    let flat = flatten_paragraphs(node)
    let n = len(flat)
    collect_items_enum(flat, 0, n, [], [], ctx, render_fn, counter)
}

fn collect_items_enum(node, i, n, current_content, acc_items, ctx, render_fn, counter) {
    if (i >= n) {
        let final_items = if (len(current_content) > 0)
            acc_items ++ [<li; for c in current_content { c }>]
            else acc_items
        {elements: final_items, ctx: ctx}
    } else {
        let child = node[i]
        if (is_item(child)) {
            let flushed = if (len(current_content) > 0)
                acc_items ++ [<li; for c in current_content { c }>]
                else acc_items
            let new_ctx = ctx_mod.step_counter(ctx, counter)
            collect_items_enum(node, i + 1, n, [], flushed, new_ctx, render_fn, counter)
        } else {
            let rendered = render_fn(child, ctx)
            let new_content = if (rendered.result != null)
                current_content ++ [rendered.result]
                else current_content
            collect_items_enum(node, i + 1, n, new_content, acc_items, rendered.ctx, render_fn, counter)
        }
    }
}

// split description items — <item> children may have a label attribute
fn split_description_items(node, ctx, render_fn) {
    let flat = flatten_paragraphs(node)
    let n = len(flat)
    collect_desc_items(flat, 0, n, [], null, [], ctx, render_fn)
}

// acc_term: current <dt> element or null
// acc_content: content for current <dd>
fn collect_desc_items(node, i, n, acc_items, acc_term, acc_content, ctx, render_fn) {
    if (i >= n) {
        let final_items = flush_desc(acc_items, acc_term, acc_content)
        {elements: final_items, ctx: ctx}
    } else {
        let child = node[i]
        if (is_item(child)) {
            let flushed = flush_desc(acc_items, acc_term, acc_content)
            let label_text = get_item_label(child)
            let new_term = make_dt(label_text)
            collect_desc_items(node, i + 1, n, flushed, new_term, [], ctx, render_fn)
        } else {
            let rendered = render_fn(child, ctx)
            let new_content = if (rendered.result != null)
                acc_content ++ [rendered.result]
                else acc_content
            collect_desc_items(node, i + 1, n, acc_items, acc_term, new_content, rendered.ctx, render_fn)
        }
    }
}

fn flush_desc(acc_items, acc_term, acc_content) {
    if acc_term != null {
        let dd = make_dd(acc_content)
        acc_items ++ [acc_term, dd]
    } else if (len(acc_content) > 0) {
        acc_items ++ [make_dd(acc_content)]
    } else {
        acc_items
    }
}

fn make_dd(content) {
    if (len(content) > 0) { <dd; for c in content { c }> } else { <dd> }
}

fn make_dt(label_text) {
    if (label_text != null) { <dt; label_text> } else { <dt> }
}

pub fn get_item_label_pub(item_node) {
    get_item_label(item_node)
}

fn get_item_label(item_node) {
    // look for optional argument or direct text child
    let opt = util.find_child(item_node, 'brack_group')
    if (opt != null) { util.text_of(opt) }
    else {
        let n = len(item_node)
        if (n > 0) util.text_of(item_node)
        else null
    }
}
