// math/atoms/array.ls -- Environment / matrix rendering
// Handles: matrix, pmatrix, bmatrix, vmatrix, Vmatrix, Bmatrix,
//          cases, aligned, array, smallmatrix
// Also handles plain TeX \matrix, \pmatrix
//
// Strategy: render non-separator children as flat list of cell boxes,
// use CSS grid with grid-template-columns to achieve proper layout.

import box: lambda.package.math.box
import ctx: lambda.package.math.context
import css: lambda.package.math.css
import delims: lambda.package.math.atoms.delimiters

// ============================================================
// Public entry points
// ============================================================

pub fn render_env(node, context, render_fn) {
    let env_name = if (node.name != null) string(node.name) else "matrix"
    let body = node.body
    if (body == null) box.text_box("", null, "mord")
    else render_body(body, context, render_fn, env_name)
}

pub fn render_matrix(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else "\\matrix"
    let body = node.body
    let env_name = if (cmd == "\\pmatrix") "pmatrix"
        else if (cmd == "\\bordermatrix") "bordermatrix"
        else "matrix"
    if (body == null) box.text_box("", null, "mord")
    else render_body(body, context, render_fn, env_name)
}

// ============================================================
// Core rendering
// ============================================================

fn render_body(body, context, render_fn, env_name) {
    let cell_ctx = if (env_name == "smallmatrix")
        ctx.derive(context, {style: "script"})
    else context
    let n = len(body)
    let ncols = count_first_row_cols(body, n)
    let nrows = count_row_seps(body, n) + 1
    let cell_groups = group_cells(body, 0, n, [], [])
    let cell_boxes = (for (g in cell_groups) render_cell_group(g, cell_ctx, render_fn))
    let table = build_table(cell_boxes, ncols, nrows, env_name)
    wrap_delimiters(table, env_name)
}

// Group consecutive non-separator children into cells.
// Each separator (col_sep or row_sep) marks a cell boundary.
fn group_cells(body, i, n, cells, current) {
    if (i >= n) {
        if (len(current) > 0) cells ++ [current]
        else cells
    } else if (is_sep(body[i])) {
        if (len(current) > 0)
            group_cells(body, i + 1, n, cells ++ [current], [])
        else
            group_cells(body, i + 1, n, cells, [])
    } else
        group_cells(body, i + 1, n, cells, current ++ [body[i]])
}

fn render_cell_group(children, cell_ctx, render_fn) {
    if (len(children) == 1) render_fn(children[0], cell_ctx)
    else {
        let parts = (for (c in children) render_fn(c, cell_ctx))
        box.hbox(parts)
    }
}

// ============================================================
// Separator detection
// ============================================================

fn is_sep(child) {
    if (child is symbol) true
    else false
}

fn is_row_sep(child) {
    if (child is symbol) string(child) == "row_sep"
    else false
}

fn is_col_sep(child) {
    if (child is symbol) string(child) == "col_sep"
    else false
}

// ============================================================
// Counting
// ============================================================

fn count_first_row_cols(body, n) {
    count_cols_until_row_sep(body, 0, n, 0) + 1
}

fn count_cols_until_row_sep(body, i, n, acc) {
    if (i >= n) acc
    else if (is_row_sep(body[i])) acc
    else if (is_col_sep(body[i])) count_cols_until_row_sep(body, i + 1, n, acc + 1)
    else count_cols_until_row_sep(body, i + 1, n, acc)
}

fn count_row_seps(body, n) {
    len((for (i in 0 to (n - 1) where is_row_sep(body[i])) i))
}

// ============================================================
// Table building (nested flexbox: column of row flexes)
// ============================================================

fn build_table(cell_boxes, ncols, nrows, env_name) {
    let gap = get_row_gap(env_name)
    let aligns = get_alignment(env_name, ncols)
    let total_cells = len(cell_boxes)
    let total_h = compute_height(cell_boxes, total_cells, nrows)
    let total_d = compute_depth(cell_boxes, total_cells, nrows)
    let total_w = compute_width(cell_boxes, total_cells)
    let row_els = build_rows(cell_boxes, ncols, nrows, aligns, 0, [])
    let est_h = estimate_total_height(cell_boxes, total_cells, nrows)
    let table_style = "display:inline-flex;flex-direction:column;vertical-align:middle;height:" ++ string(est_h) ++ "em"
    {
        element: <span class: css.MTABLE, style: table_style;
            for el in row_els { el }
        >,
        height: total_h,
        depth: total_d,
        width: total_w,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

fn get_row_gap(env_name) {
    if (env_name == "smallmatrix") "0.15em"
    else if (env_name == "cases" or env_name == "rcases" or env_name == "dcases") "0.2em"
    else "0.5em"
}

fn build_rows(cell_boxes, ncols, nrows, aligns, row, acc) {
    if (row >= nrows) acc
    else {
        let start = row * ncols
        let cells_in_row = build_row_cells(cell_boxes, aligns, ncols, start, 0, ncols, [])
        let row_margin = if (row < nrows - 1) "margin-bottom:" ++ get_row_gap_val(nrows) else ""
        let row_el = <span style: row_margin; for c in cells_in_row { c }>
        build_rows(cell_boxes, ncols, nrows, aligns, row + 1, acc ++ [row_el])
    }
}

fn get_row_gap_val(nrows) {
    if (nrows <= 1) "0"
    else "0.3em"
}

fn build_row_cells(cell_boxes, aligns, ncols, start, col, total_cols, acc) {
    if (col >= ncols) acc
    else {
        let idx = start + col
        let total = len(cell_boxes)
        let align = get_align_char(aligns, col)
        let text_align = if (align == "l") "left"
            else if (align == "r") "right"
            else "center"
        let col_margin = if (col < total_cols - 1) ";margin-right:0.5em" else ""
        let cell_style = "text-align:" ++ text_align ++ col_margin
        let cell_el = if (idx < total)
            <span style: cell_style; cell_boxes[idx].element>
        else
            <span style: cell_style>
        build_row_cells(cell_boxes, aligns, ncols, start, col + 1, total_cols, acc ++ [cell_el])
    }
}

// ============================================================
// Alignment
// ============================================================

fn get_alignment(env_name, ncols) {
    if (env_name == "cases" or env_name == "rcases" or env_name == "dcases")
        repeat_char(ncols, "l")
    else if (env_name == "aligned" or env_name == "align")
        make_alternating(ncols, 0, "")
    else
        repeat_char(ncols, "c")
}

fn repeat_char(n, ch) {
    if (n <= 0) ch
    else do_repeat_char(n, 0, "", ch)
}

fn do_repeat_char(n, i, acc, ch) {
    if (i >= n) acc
    else do_repeat_char(n, i + 1, acc ++ ch, ch)
}

fn make_alternating(n, i, acc) {
    if (i >= n) acc
    else if (i % 2 == 0) make_alternating(n, i + 1, acc ++ "r")
    else make_alternating(n, i + 1, acc ++ "l")
}

fn get_align_char(aligns, col) {
    if (col < len(aligns)) slice(aligns, col, col + 1)
    else "c"
}

// ============================================================
// Delimiter wrapping
// ============================================================

fn get_left_delim(env_name) {
    if (env_name == "pmatrix") "("
    else if (env_name == "bmatrix") "["
    else if (env_name == "Bmatrix") "{"
    else if (env_name == "vmatrix") "|"
    else if (env_name == "Vmatrix") "\u2016"
    else if (env_name == "cases") "{"
    else null
}

fn get_right_delim(env_name) {
    if (env_name == "pmatrix") ")"
    else if (env_name == "bmatrix") "]"
    else if (env_name == "Bmatrix") "}"
    else if (env_name == "vmatrix") "|"
    else if (env_name == "Vmatrix") "\u2016"
    else if (env_name == "rcases") "}"
    else null
}

fn wrap_delimiters(table_box, env_name) {
    let ld = get_left_delim(env_name)
    let rd = get_right_delim(env_name)
    let content_height = table_box.height + table_box.depth
    let left_box = if (ld != null) delims.render_stretchy(ld, content_height, "mopen") else null
    let right_box = if (rd != null) delims.render_stretchy(rd, content_height, "mclose") else null
    let parts = (for (p in [left_box, table_box, right_box] where p != null) p)
    let children = (for (p in parts) p.element)
    let total_width = sum((for (p in parts) p.width))
    {
        element: <span style: "display:inline-flex;align-items:center;vertical-align:middle";
            for child in children { child }
        >,
        height: table_box.height,
        depth: table_box.depth,
        width: total_width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

// ============================================================
// Height / width estimation
// ============================================================

// estimate total visual height of the table accounting for line-height and gaps
fn estimate_total_height(cell_boxes, total_cells, nrows) {
    if (total_cells == 0) 1.0
    else {
        let max_h = max((for (i in 0 to (total_cells - 1)) cell_boxes[i].height))
        let max_d = max((for (i in 0 to (total_cells - 1)) cell_boxes[i].depth))
        let row_ht = max(max_h + max_d, 1.2)
        let gap = if (nrows > 1) 0.3 else 0.0
        float(nrows) * row_ht + float(nrows - 1) * gap
    }
}

fn compute_height(cell_boxes, total_cells, nrows) {
    estimate_total_height(cell_boxes, total_cells, nrows) * 0.5
}

fn compute_depth(cell_boxes, total_cells, nrows) {
    estimate_total_height(cell_boxes, total_cells, nrows) * 0.5
}

fn compute_width(cell_boxes, total_cells) {
    if (total_cells == 0) 0.0
    else sum((for (i in 0 to (total_cells - 1)) cell_boxes[i].width)) + 1.0
}
