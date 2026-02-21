// math/atoms/array.ls -- Environment / matrix rendering
// Handles: matrix, pmatrix, bmatrix, vmatrix, Vmatrix, Bmatrix,
//          cases, aligned, array, smallmatrix
// Also handles plain TeX \matrix, \pmatrix
//
// Strategy: render non-separator children as flat list of cell boxes,
// use CSS grid with grid-template-columns to achieve proper layout.

import box: .lambda.package.math.box
import ctx: .lambda.package.math.context
import css: .lambda.package.math.css

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
    let cell_boxes = (for (i in 0 to (n - 1),
                           let child = body[i]
                           where not is_sep(child))
        render_fn(child, cell_ctx))
    let nrows = count_row_seps(body, n) + 1
    let table = build_table(cell_boxes, ncols, nrows, env_name)
    wrap_delimiters(table, env_name)
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
// Table building
// ============================================================

fn build_table(cell_boxes, ncols, nrows, env_name) {
    let gap = get_row_gap(env_name)
    let grid_cols = make_grid_cols(ncols, 1, "auto")
    let aligns = get_alignment(env_name, ncols)
    let total_cells = len(cell_boxes)
    let cell_els = (for (idx in 0 to (total_cells - 1),
                         let col = idx % ncols,
                         let align = get_align_char(aligns, col),
                         let justify = if (align == "l") "start"
                             else if (align == "r") "end"
                             else "center")
        <span style: "justify-self:" ++ justify; cell_boxes[idx].element>)
    let total_h = compute_height(cell_boxes, total_cells, nrows)
    let total_d = compute_depth(cell_boxes, total_cells, nrows)
    let total_w = compute_width(cell_boxes, total_cells)
    let table_style = "display:inline-grid;grid-template-columns:" ++
        grid_cols ++ ";row-gap:" ++ gap ++ ";column-gap:0.5em;align-items:baseline;vertical-align:middle"
    {
        element: <span class: css.MTABLE, style: table_style;
            for (el in cell_els) el
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

fn make_grid_cols(n, i, acc) {
    if (i >= n) acc
    else make_grid_cols(n, i + 1, acc ++ " auto")
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
    let left_box = if (ld != null) box.text_box(ld, css.SMALL_DELIM, "mopen") else null
    let right_box = if (rd != null) box.text_box(rd, css.SMALL_DELIM, "mclose") else null
    let parts = (for (p in [left_box, table_box, right_box] where p != null) p)
    box.hbox(parts)
}

// ============================================================
// Height / width estimation
// ============================================================

fn compute_height(cell_boxes, total_cells, nrows) {
    if (total_cells == 0) 0.5
    else max((for (i in 0 to (total_cells - 1)) cell_boxes[i].height)) * float(nrows) * 0.6
}

fn compute_depth(cell_boxes, total_cells, nrows) {
    if (total_cells == 0) 0.2
    else max((for (i in 0 to (total_cells - 1)) cell_boxes[i].depth)) * float(nrows) * 0.4
}

fn compute_width(cell_boxes, total_cells) {
    if (total_cells == 0) 0.0
    else sum((for (i in 0 to (total_cells - 1)) cell_boxes[i].width)) + 1.0
}
