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
import util: lambda.package.math.util

// ============================================================
// Public entry points
// ============================================================

pub fn render_env(node, context, render_fn) {
    let env_name = if (node.name != null) string(node.name) else "matrix"
    let columns = if (node.columns != null) string(node.columns) else null
    let body = node.body
    if (body == null) box.text_box("", null, "mord")
    else render_body(body, context, render_fn, env_name, columns)
}

pub fn render_matrix(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else "\\matrix"
    let body = node.body
    let env_name = if (cmd == "\\pmatrix") "pmatrix"
        else if (cmd == "\\bordermatrix") "bordermatrix"
        else "matrix"
    if (body == null) box.text_box("", null, "mord")
    else render_body(body, context, render_fn, env_name, null)
}

// ============================================================
// Core rendering
// ============================================================

fn render_body(body, context, render_fn, env_name, columns) {
    let cell_ctx = if (env_name == "smallmatrix")
        ctx.derive(context, {style: "script"})
    else context
    let n = len(body)
    let declared_cols = declared_column_count(columns)
    let source_rows = parse_rows(body, 0, n, [], [], [])
    let row_groups = if (declared_cols > 0)
        expand_declared_rows(source_rows, declared_cols, 0, [])
    else source_rows
    let nrows = max(len(row_groups), 1)
    let ncols = if (declared_cols > 0) declared_cols else max_cols(row_groups, 0, 0)
    let aligns = if (declared_cols > 0) declared_alignment(columns)
        else get_alignment(env_name, ncols)
    let row_boxes = render_row_groups(row_groups, cell_ctx, render_fn)
    let table = build_table(row_boxes, ncols, nrows, aligns, env_name)
    wrap_delimiters(table, env_name)
}

// Parse source rows before rendering so ragged matrices do not shift later
// cells into the wrong row.
fn parse_rows(body, i, n, rows, current_row, current_cell) {
    if (i >= n) {
        rows ++ [make_row(current_row ++ [make_cell(current_cell)])]
    } else if (is_sep(body[i])) {
        if (is_row_sep(body[i]))
            parse_rows(body, i + 1, n, rows ++ [make_row(current_row ++ [make_cell(current_cell)])], [], [])
        else
            parse_rows(body, i + 1, n, rows, current_row ++ [make_cell(current_cell)], [])
    } else
        parse_rows(body, i + 1, n, rows, current_row, current_cell ++ [body[i]])
}

fn make_cell(items) => {items: items}

fn make_row(cells) => {cells: cells}

fn expand_declared_rows(rows, ncols, i, acc) {
    if (i >= len(rows)) acc
    else expand_declared_rows(rows, ncols, i + 1, acc ++ chunk_row(rows[i].cells, ncols, 0, []))
}

fn chunk_row(row, ncols, i, acc) {
    if (i >= len(row)) acc
    else chunk_row(row, ncols, i + ncols, acc ++ [make_row(pad_row(slice(row, i, min(len(row), i + ncols)), ncols))])
}

fn pad_row(row, ncols) {
    if (len(row) >= ncols) row
    else pad_row(row ++ [make_cell([])], ncols)
}

fn max_cols(rows, i, acc) {
    if (i >= len(rows)) max(acc, 1)
    else max_cols(rows, i + 1, max(acc, len(rows[i].cells)))
}

fn render_row_groups(rows, cell_ctx, render_fn) {
    [for (row in rows) [for (g in row.cells) render_cell_group(g.items, cell_ctx, render_fn)]]
}

fn render_cell_group(children, cell_ctx, render_fn) {
    if (len(children) == 0) blank_cell_box()
    else if (len(children) == 1) render_fn(children[0], cell_ctx)
    else {
        let parts = (for (c in children) render_fn(c, cell_ctx))
        box.hbox(parts)
    }
}

fn blank_cell_box() => {
    element: "\u00A0",
    height: 0.0,
    depth: 0.0,
    width: 0.5,
    type: "mord",
    italic: 0.0,
    skew: 0.0
}

// ============================================================
// Separator detection
// ============================================================

fn is_sep(child) {
    is_row_sep(child) or is_col_sep(child)
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
// Table building (MathLive-style columns of vlists)
// ============================================================

fn build_table(row_boxes, ncols, nrows, aligns, env_name) {
    let metrics = table_metrics(env_name, nrows)
    let total_w = compute_table_width(row_boxes, ncols, env_name)
    let children = build_table_children(row_boxes, ncols, aligns, metrics, env_name, 0, [])
    {
        element: <span class: css.MTABLE;
            for el in children { el }
        >,
        height: metrics.height,
        depth: metrics.depth,
        render_height: metrics.height,
        render_depth: metrics.depth,
        render_total: metrics.render_total,
        width: total_w,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

fn build_table_children(row_boxes, ncols, aligns, metrics, env_name, col, acc) {
    if (col >= ncols) add_trailing_array_sep(acc, env_name)
    else {
        let with_leading = if (col == 0 and env_name == "array")
            acc ++ [array_col_sep(0.5)]
        else acc
        let col_el = build_column(row_boxes, col, get_align_char(aligns, col), metrics)
        let with_col = with_leading ++ [col_el]
        let with_sep = if (col < ncols - 1)
            with_col ++ [array_col_sep(1.0)]
        else with_col
        build_table_children(row_boxes, ncols, aligns, metrics, env_name, col + 1, with_sep)
    }
}

fn add_trailing_array_sep(acc, env_name) {
    if (env_name == "array") acc ++ [array_col_sep(0.5)] else acc
}

fn array_col_sep(width) =>
    <span class: "lm_arraycolsep", style: "width:" ++ util.fmt_em(width)>

fn build_column(row_boxes, col, align, metrics) {
    let cls = "col-align-" ++ align
    let row_spans = build_column_rows(row_boxes, col, metrics, 0, [])
    <span class: cls;
        <span class: css.VLIST_T2;
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em(metrics.vlist_height);
                    for row_span in row_spans { row_span }
                >
                <span class: css.VLIST_S; "\u200B">
            >
            <span class: css.VLIST_R;
                <span class: css.VLIST, style: "height:" ++ util.fmt_em(metrics.depth_holder)>
            >
        >
    >
}

fn build_column_rows(row_boxes, col, metrics, row, acc) {
    if (row >= len(row_boxes)) acc
    else {
        let cell = cell_at(row_boxes, row, col)
        let top = row_top(metrics, row)
        let span = <span style: "top:" ++ util.fmt_em(top);
            <span class: css.PSTRUT, style: "height:" ++ util.fmt_em(metrics.pstrut)>
            <span style: "height:" ++ util.fmt_em(metrics.cell_height) ++ ";display:inline-block";
                for child in box.elements_of(cell) { child }
            >
        >
        build_column_rows(row_boxes, col, metrics, row + 1, acc ++ [span])
    }
}

fn cell_at(row_boxes, row, col) {
    if (row < len(row_boxes) and col < len(row_boxes[row])) row_boxes[row][col]
    else blank_cell_box()
}

fn table_metrics(env_name, nrows) {
    if (env_name == "array") array_table_metrics(nrows)
    else matrix_table_metrics(nrows)
}

fn matrix_table_metrics(nrows) => {
    height: 0.85 + 0.6 * float(nrows - 1),
    depth: 0.35 + 0.6 * float(nrows - 1),
    render_total: 1.21 + 1.2 * float(nrows - 1),
    vlist_height: 0.85 + 0.6 * float(nrows - 1),
    depth_holder: if (nrows == 1) 0.35
        else if (nrows == 2) 0.96
        else 0.35 + 0.6 * float(nrows - 1),
    pstrut: 3.0,
    cell_height: 1.2
}

fn array_table_metrics(nrows) => {
    height: 0.84,
    depth: 0.36 + 1.2 * float(nrows - 1),
    render_total: 1.2 + 1.2 * float(nrows - 1),
    vlist_height: 0.84,
    depth_holder: 0.36 + 1.2 * float(nrows - 1),
    pstrut: 3.0,
    cell_height: 1.2
}

fn row_top(metrics, row) {
    if (metrics.height == 0.84) array_row_top(row)
    else matrix_row_top(metrics, row)
}

fn matrix_row_top(metrics, row) {
    let nrows = int(round((metrics.render_total - 1.21) / 1.2)) + 1
    if (nrows == 1) 0.0 - 3.01
    else if (nrows == 2) {
        if (row == 0) 0.0 - 3.61 else 0.0 - 2.4
    }
    else {
        if (row == 0) 0.0 - 4.21
        else if (row == 1) 0.0 - 3.0
        else 0.0 - 1.81 + 1.2 * float(row - 2)
    }
}

fn array_row_top(row) {
    if (row == 0) 0.0 - 3.0
    else if (row == 1) 0.0 - 1.79
    else 0.0 - 0.6 + 1.2 * float(row - 2)
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

fn declared_column_count(columns) {
    if (columns == null) 0 else count_declared_columns(columns, 0, 0)
}

fn count_declared_columns(columns, i, acc) {
    if (i >= len(columns)) acc
    else {
        let ch = slice(columns, i, i + 1)
        if (ch == "l" or ch == "c" or ch == "r")
            count_declared_columns(columns, i + 1, acc + 1)
        else
            count_declared_columns(columns, i + 1, acc)
    }
}

fn declared_alignment(columns) {
    if (columns == null) "" else declared_alignment_at(columns, 0, "")
}

fn declared_alignment_at(columns, i, acc) {
    if (i >= len(columns)) acc
    else {
        let ch = slice(columns, i, i + 1)
        if (ch == "l" or ch == "c" or ch == "r")
            declared_alignment_at(columns, i + 1, acc ++ ch)
        else
            declared_alignment_at(columns, i + 1, acc)
    }
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
    if (ld == null and rd == null) table_box
    else wrap_with_delimiters(table_box, ld, rd)
}

fn wrap_with_delimiters(table_box, ld, rd) {
    let left_box = if (ld != null) render_matrix_delim(ld, table_box) else null
    let right_box = if (rd != null) render_matrix_delim(rd, table_box) else null
    let parts = (for (p in [left_box, table_box, right_box] where p != null) p)
    let children = (for (p in parts) p.element)
    let total_width = sum((for (p in parts) p.width))
    {
        element: <span style: "display:inline-block";
            for child in children { child }
        >,
        height: table_box.height,
        depth: table_box.depth,
        render_height: table_box.render_height,
        render_depth: table_box.render_depth,
        render_total: table_box.render_total,
        strut_total: table_box.strut_total,
        strut_depth_em: table_box.strut_depth_em,
        width: total_width,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

fn render_matrix_delim(ch, table_box) {
    let nrows = int(round((table_box.render_total - 1.21) / 1.2)) + 1
    if (nrows >= 3 and (ch == "[" or ch == "]")) render_square_mult_delim(ch)
    else render_plain_sized_delim(ch, matrix_delim_level(table_box.render_total))
}

fn matrix_delim_level(render_total) {
    if (render_total <= 1.21) 1
    else if (render_total <= 1.81) 2
    else if (render_total <= 2.41) 3
    else 4
}

fn render_plain_sized_delim(ch, level) {
    let cls = if (level == 1) css.DELIM_SIZE1
        else if (level == 2) css.DELIM_SIZE2
        else if (level == 3) css.DELIM_SIZE3
        else css.DELIM_SIZE4
    {
        element: <span class: cls; ch>,
        height: 0.6 * float(level),
        depth: 0.3 * float(level),
        width: 0.4,
        type: "ord",
        italic: 0.0,
        skew: 0.0
    }
}

fn render_square_mult_delim(ch) {
    let pieces = if (ch == "[") ["⎣", "⎡"] else ["⎦", "⎤"]
    let tops = [0.0 - 2.25, 0.0 - 4.03]
    {
        element: <span class: "lm_delim-mult";
            <span class: "delim-size4 lm_vlist-t lm_vlist-t2";
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:2.04em";
                        for i in 0 to 1 {
                            <span style: "top:" ++ util.fmt_em(tops[i]);
                                <span class: css.PSTRUT, style: "height:3.16em">
                                <span style: "height:1.81em;display:inline-block"; pieces[i]>
                            >
                        }
                    >
                    <span class: css.VLIST_S; "\u200B">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:1.56em">
                >
            >
        >,
        height: 2.05,
        depth: 1.55,
        width: 0.4,
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

fn compute_table_width(row_boxes, ncols, env_name) {
    let cell_w = sum((for (col in 0 to (ncols - 1)) max_col_width(row_boxes, col)))
    let sep_w = if (ncols <= 1) 0.0 else float(ncols - 1)
    let edge_w = if (env_name == "array") 1.0 else 0.0
    cell_w + sep_w + edge_w
}

fn max_col_width(row_boxes, col) {
    if (len(row_boxes) == 0) 0.0
    else max((for (row in row_boxes) if (col < len(row)) row[col].width else 0.5))
}
