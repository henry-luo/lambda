// latex/elements/lists.ls — List environments: itemize, enumerate, description
// Handles \begin{itemize}, \begin{enumerate}, \begin{description}

import util: .lambda.package.latex.util
import ctx_mod: .lambda.package.latex.context

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
    let n = len(node)
    collect_items(node, 0, n, [], [], ctx, render_fn)
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
        if (child is element and name(child) == "item") {
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
    let n = len(node)
    collect_items_enum(node, 0, n, [], [], ctx, render_fn, counter)
}

fn collect_items_enum(node, i, n, current_content, acc_items, ctx, render_fn, counter) {
    if (i >= n) {
        let final_items = if (len(current_content) > 0)
            acc_items ++ [<li; for c in current_content { c }>]
            else acc_items
        {elements: final_items, ctx: ctx}
    } else {
        let child = node[i]
        if (child is element and name(child) == "item") {
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
    let n = len(node)
    collect_desc_items(node, 0, n, [], null, [], ctx, render_fn)
}

// acc_term: current <dt> element or null
// acc_content: content for current <dd>
fn collect_desc_items(node, i, n, acc_items, acc_term, acc_content, ctx, render_fn) {
    if (i >= n) {
        let final_items = if (acc_term != null) {
            let dd = if (len(acc_content) > 0) <dd; for c in acc_content { c }> else <dd>
            acc_items ++ [acc_term, dd]
        } else if (len(acc_content) > 0) {
            acc_items ++ [<dd; for c in acc_content { c }>]
        } else {
            acc_items
        }
        {elements: final_items, ctx: ctx}
    } else {
        let child = node[i]
        if (child is element and name(child) == "item") {
            let flushed = if (acc_term != null) {
                let dd = if (len(acc_content) > 0) <dd; for c in acc_content { c }> else <dd>
                acc_items ++ [acc_term, dd]
            } else if (len(acc_content) > 0) {
                acc_items ++ [<dd; for c in acc_content { c }>]
            } else {
                acc_items
            }

            let label_text = get_item_label(child)
            let new_term = if (label_text != null) <dt; label_text> else <dt>
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

fn get_item_label(item_node) {
    // look for optional argument or direct text child
    let opt = util.find_child(item_node, "brack_group")
    if (opt != null) { util.text_of(opt) }
    else {
        let n = len(item_node)
        if (n > 0) util.text_of(item_node)
        else null
    }
}
