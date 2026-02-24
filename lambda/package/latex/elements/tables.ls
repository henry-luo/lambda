// latex/elements/tables.ls — Table rendering
// Handles \begin{tabular}, \begin{table} environments
// Rows split at row_sep symbols, cells split at alignment_tab (&)

import util: .lambda.package.latex.util
import ctx_mod: .lambda.package.latex.context

// helper: check if child should be skipped in table rendering (caption/label)
fn is_skip_in_table(child) {
    if (child is element) {
        let t = string(name(child))
        if (t == "caption") true
        else if (t == "label") true
        else false
    } else { false }
}

// ============================================================
// Table environment (float wrapper)
// ============================================================

pub fn render_table_env(node, ctx, render_fn) {
    let new_ctx = ctx_mod.step_counter(ctx, "table")
    let tbl_num = new_ctx.counters.table
    let children = render_table_children(node, new_ctx, render_fn)

    // extract caption
    let caption_node = util.find_child(node, 'caption')
    let cap_text = if (caption_node != null) util.text_of(caption_node) else null
    let caption_el = if (cap_text != null) make_table_caption(tbl_num, cap_text) else null

    // extract label
    let label_node = util.find_child(node, 'label')
    let label_name = if (label_node != null) util.text_of(label_node) else null
    let ctx2 = if (label_name != null)
        ctx_mod.add_label(children.ctx, label_name, "table", string(tbl_num), util.slugify("tbl-" ++ label_name))
        else children.ctx

    let result = <div class: "latex-table-float";
        for c in children.items { c }
    >
    {result: result, ctx: ctx2}
}

// ============================================================
// Tabular environment
// ============================================================

pub fn render_tabular(node, ctx, render_fn) {
    // extract column spec from brack_group or curly_group
    let col_spec = extract_col_spec(node)

    // split rows at row_sep, then cells at alignment_tab
    let rows = split_rows(node, ctx, render_fn)

    let result = <table class: "latex-tabular";
        if rows.caption != null { rows.caption }
        <tbody; for row in rows.elements { row }>
    >
    {result: result, ctx: rows.ctx}
}

// ============================================================
// Row & cell splitting
// ============================================================

fn split_rows(node, ctx, render_fn) {
    let n = len(node)
    collect_rows(node, 0, n, [], [], ctx, render_fn)
}

// walk children: accumulate cells until row_sep, then emit <tr>
fn collect_rows(node, i, n, current_cells, acc_rows, ctx, render_fn) {
    if (i >= n) {
        let final_rows = flush_row(current_cells, acc_rows)
        {elements: final_rows, ctx: ctx, caption: null}
    } else {
        let child = node[i]
        if (is_row_sep(child)) {
            let tr = make_row(current_cells)
            let new_rows = acc_rows ++ [tr]
            collect_rows(node, i + 1, n, [], new_rows, ctx, render_fn)
        } else if (is_alignment_tab(child)) {
            collect_rows(node, i + 1, n, current_cells ++ [child], acc_rows, ctx, render_fn)
        } else {
            let rendered = render_fn(child, ctx)
            let cell_item = if (rendered.result != null) rendered.result else null
            let new_cells = if (cell_item != null) current_cells ++ [cell_item] else current_cells
            collect_rows(node, i + 1, n, new_cells, acc_rows, rendered.ctx, render_fn)
        }
    }
}

// given a flat list of content mixed with alignment_tab markers, produce <tr>
fn make_row(items) {
    let cells = split_at_tabs(items, 0, len(items), [], [])
    <tr; for td in cells {
        <td; for c in td { c }>
    }>
}

// split an array at alignment_tab elements
fn split_at_tabs(items, i, n, current_cell, acc_cells) {
    if (i >= n) {
        if (len(current_cell) > 0) acc_cells ++ [current_cell]
        else acc_cells
    } else {
        let item = items[i]
        if (is_alignment_tab_rendered(item)) {
            let flushed = acc_cells ++ [current_cell]
            split_at_tabs(items, i + 1, n, [], flushed)
        } else {
            split_at_tabs(items, i + 1, n, current_cell ++ [item], acc_cells)
        }
    }
}

// check if child is a row separator (\\)
fn is_row_sep(child) {
    if (child is symbol) { string(child) == "row_sep" }
    else if (child is element) { string(name(child)) == "row_sep" }
    else { false }
}

// check if child is alignment tab (&) in AST
fn is_alignment_tab(child) {
    if (child is symbol) { string(child) == "alignment_tab" }
    else if (child is element) { string(name(child)) == "alignment_tab" }
    else if (child is string) { trim(child) == "&" }
    else { false }
}

// check rendered output for tab marker
fn is_alignment_tab_rendered(item) {
    if (item is string) { trim(item) == "&" }
    else if (item is element) { string(name(item)) == "alignment_tab" }
    else { false }
}

// extract column specification string
fn extract_col_spec(node) {
    let cg = util.find_child(node, 'curly_group')
    if (cg != null) util.text_of(cg)
    else null
}

// ============================================================
// Helper: render table children (skip caption/label in child rendering)
// ============================================================

fn render_table_children(node, ctx, render_fn) {
    let n = len(node)
    if (n == 0) { {items: [], ctx: ctx} }
    else { render_tbl_rec(node, 0, n, [], ctx, render_fn) }
}

fn render_tbl_rec(node, i, n, acc, ctx, render_fn) {
    if (i >= n) { {items: acc, ctx: ctx} }
    else {
        let child = node[i]
        // skip caption and label — handled separately
        let skip = is_skip_in_table(child)
        if (skip) { render_tbl_rec(node, i + 1, n, acc, ctx, render_fn) }
        else {
            let rendered = render_fn(child, ctx)
            let new_acc = if (rendered.result != null) acc ++ [rendered.result] else acc
            render_tbl_rec(node, i + 1, n, new_acc, rendered.ctx, render_fn)
        }
    }
}

fn make_table_caption(tbl_num, cap_text) {
    let label = <strong; "Table " ++ string(tbl_num) ++ ": ">
    <caption;
        label
        cap_text
    >
}

fn flush_row(current_cells, acc_rows) {
    if (len(current_cells) > 0) {
        let tr = make_row(current_cells)
        acc_rows ++ [tr]
    } else {
        acc_rows
    }
}
