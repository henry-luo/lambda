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
import sp_table: lambda.package.math.spacing_table
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
    let cell_ctx = if (env_name == "smallmatrix" or env_name == "cases" or env_name == "rcases")
        ctx.derive(context, {style: "script"})
    else if (env_name == "dcases")
        ctx.derive(context, {style: "display"})
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
    let spacing_ctx = cell_spacing_context(env_name, context, cell_ctx)
    let row_boxes = render_row_groups(row_groups, cell_ctx, spacing_ctx, render_fn)
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

fn cell_spacing_context(env_name, context, cell_ctx) {
    if (env_name == "cases" or env_name == "rcases")
        ctx.derive(context, {style: "text"})
    else cell_ctx
}

fn render_row_groups(rows, cell_ctx, spacing_ctx, render_fn) {
    [for (row in rows) [for (g in row.cells) render_cell_group(g.items, cell_ctx, spacing_ctx, render_fn)]]
}

fn render_cell_group(children, cell_ctx, spacing_ctx, render_fn) {
    if (len(children) == 0) blank_cell_box()
    else if (len(children) == 1) render_fn(children[0], cell_ctx)
    else {
        let parts = (for (c in children) render_fn(c, cell_ctx))
        box.hbox(apply_spacing(parts, spacing_ctx))
    }
}

fn bin_as_ord_after(prev_type) {
    prev_type == null or prev_type == "mbin" or prev_type == "mopen" or
    prev_type == "mrel" or prev_type == "mpunct" or prev_type == "mop"
}

fn box_with_type(bx, atom_type) {
    ml_box_with_type(bx, atom_type)
}

fn ml_box_with_type(bx, atom_type) => {
    element: bx.element,
    height: bx.height,
    depth: bx.depth,
    width: bx.width,
    type: atom_type,
    italic: bx.italic,
    skew: bx.skew,
    max_font_size: bx.max_font_size,
    model: "ml",
    is_fraction: bx.is_fraction
}

fn normalize_bin_atom(bx, prev_type) {
    if (bx.type == "mbin" and bin_as_ord_after(prev_type)) box_with_type(bx, "mord")
    else bx
}

fn build_normalized(filtered, i, prev_type, acc) {
    if (i >= len(filtered)) acc
    else
        (let current = normalize_bin_atom(filtered[i], prev_type),
         build_normalized(filtered, i + 1, current.type, acc ++ [current]))
}

fn normalize_atom_types(filtered) {
    build_normalized(filtered, 0, null, [])
}

fn build_spaced(normalized, i, prev_type, acc, context) {
    if (i >= len(normalized)) acc
    else
        (let current = normalized[i],
         let space = if (current.no_left_bin_space == true and prev_type == "mbin")
             0.0 else if (prev_type == "mpunct" and current.type == "skip")
             0.0 else sp_table.get_spacing(prev_type, current.type, context.style),
         let with_space = if (space != 0.0)
             acc ++ [box.skip_box(space), current]
         else
             acc ++ [current],
         build_spaced(normalized, i + 1, current.type, with_space, context))
}

fn apply_spacing(boxes, context) {
    let filtered = (for (b in boxes where b != null) b)
    if (len(filtered) <= 1) filtered
    else
        (let normalized = normalize_atom_types(filtered),
         build_spaced(normalized, 1, normalized[0].type, [normalized[0]], context))
}

fn blank_cell_box() =>
    box.ml_box("\u00A0", 0.0, 0.0, 0.5, "mord")

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
    let metrics = if (env_name == "equation" and nrows == 1 and ncols == 1)
        equation_table_metrics(row_boxes[0][0])
        else if (uses_dyn_metrics(env_name))
        compute_dyn_metrics(row_boxes, ncols, nrows, env_name)
        else table_metrics(env_name, nrows)
    let total_w = compute_table_width(row_boxes, ncols, env_name)
    let children = build_table_children(row_boxes, ncols, aligns, metrics, env_name, 0, [])
    let el = <span class: css.MTABLE;
        for el in children { el }
    >
    ml_table_box(el, metrics, total_w)
}

fn array_has_delimiters(env_name) {
    get_left_delim(env_name) != null or get_right_delim(env_name) != null
}

fn ml_table_box(el, metrics, total_w) => {
    element: el,
    height: if (metrics.box_height != null) metrics.box_height else metrics.height,
    depth: if (metrics.box_depth != null) metrics.box_depth else metrics.depth,
    width: total_w,
    type: "ord",
    italic: 0.0,
    skew: 0.0,
    max_font_size: if (metrics.box_height != null) metrics.box_height else metrics.height,
    model: "ml",
    delim_total: metrics.box_total
}

fn build_table_children(row_boxes, ncols, aligns, metrics, env_name, col, acc) {
    if (col >= ncols) add_trailing_array_sep(acc, env_name)
    else {
        let with_leading = if (col == 0 and (env_name == "array" or env_name == "equation"))
            acc ++ [array_col_sep(0.5)]
        else if (col == 0 and env_name == "smallmatrix")
            acc ++ [array_col_sep(0.2)]
        else acc
        let col_el = build_column(row_boxes, col, get_align_char(aligns, col), metrics)
        let with_col = with_leading ++ [col_el]
        let between_w = if (env_name == "smallmatrix") 0.39 else 1.0
        let with_sep = if (col < ncols - 1)
            with_col ++ [array_col_sep(between_w)]
        else with_col
        build_table_children(row_boxes, ncols, aligns, metrics, env_name, col + 1, with_sep)
    }
}

fn add_trailing_array_sep(acc, env_name) {
    if (env_name == "array" or env_name == "equation") acc ++ [array_col_sep(0.5)]
    else if (env_name == "cases" or env_name == "dcases") acc ++ [array_col_sep(1.0)]
    else if (env_name == "smallmatrix") acc ++ [array_col_sep(0.2)]
    else acc
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
        let cell_height = row_cell_height(metrics, row)
        let span = <span style: "top:" ++ util.fmt_em(top);
            <span class: css.PSTRUT, style: "height:" ++ util.fmt_em(metrics.pstrut)>
            <span style: "height:" ++ util.fmt_em(cell_height) ++ ";display:inline-block";
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

// only smallmatrix still uses a hardcoded table (scriptstyle-cell model not
// yet ported); everything else is metric-driven via compute_dyn_metrics.
fn table_metrics(env_name, nrows) {
    matrix_table_metrics(nrows)
}

// ============================================================
// Metric-driven table layout (TeX \@array, ref MathLive array.ts render).
// Replaces the per-nrows hardcoded *_table_metrics + *_row_top tables for
// scale-1.0 environments. Each row's height/depth derive from the real cell
// boxes plus the array strut (arstrutHeight 0.84, arstrutDepth 0.36 =
// 0.7/0.3 × arraystretch(1.0) × baselineskip(1.2)); the box height/depth,
// vlist tops, and depth-holder all fall out of the centering offset.
// ============================================================

// scale-1.0 envs (cells render at the ambient/display style): matrix family,
// array, dcases (display cells; adds inter-row `jot`). cases/rcases/smallmatrix
// scale cells to scriptstyle and keep their legacy tables; equation has its own
// centering.
fn uses_dyn_metrics(env_name) {
    not (env_name == "smallmatrix" or env_name == "equation")
}

// TeX \jot (3pt = 0.3em): extra depth added to every non-last row in dcases/
// rcases/aligned-style environments (NOT matrices, cases, or array).
fn env_jot(env_name) {
    if (env_name == "dcases" or env_name == "rcases" or
        env_name == "aligned" or env_name == "align") 0.3
    else 0.0
}

// \arraystretch: cases/rcases reserve taller rows (1.2 → arstrut 1.008/0.432,
// so short script cells get a fixed row height); everything else uses 1.0.
fn env_arraystretch(env_name) {
    if (env_name == "cases" or env_name == "rcases") 1.2
    else 1.0
}

fn dyn_cell_h(b) { if (b != null) b.height else 0.0 }
fn dyn_cell_d(b) { if (b != null) b.depth else 0.0 }

fn dyn_row_max_h(row_boxes, r, ncols, col, acc) {
    if (col >= ncols) acc
    else dyn_row_max_h(row_boxes, r, ncols, col + 1, max(acc, dyn_cell_h(cell_at(row_boxes, r, col))))
}

fn dyn_row_max_d(row_boxes, r, ncols, col, acc) {
    if (col >= ncols) acc
    else dyn_row_max_d(row_boxes, r, ncols, col + 1, max(acc, dyn_cell_d(cell_at(row_boxes, r, col))))
}

// per-row [height, depth, pos]: arstrut maxed with the tallest/deepest cell;
// pos = running total after the row's height (TeX \@array accumulation). The
// running float order MUST match MathLive so the 0.01 CEIL@2 boundary cases
// (e.g. 2-row depth-holder 0.96, not 0.95) reproduce exactly.
fn dyn_rows(row_boxes, ncols, nrows, ah, ad, jot, r, total, acc) {
    if (r >= nrows) acc
    else {
        let mh = max(ah, dyn_row_max_h(row_boxes, r, ncols, 0, 0.0))
        let md0 = max(ad, dyn_row_max_d(row_boxes, r, ncols, 0, 0.0))
        let md = if (r < nrows - 1) md0 + jot else md0
        let t1 = total + mh
        dyn_rows(row_boxes, ncols, nrows, ah, ad, jot, r + 1, t1 + md, acc ++ [[mh, md, t1]])
    }
}

fn dyn_max_h(rows, i, acc) {
    if (i >= len(rows)) acc
    else dyn_max_h(rows, i + 1, max(acc, rows[i][0]))
}

// getVListChildrenAndDepth (individualShift): the kern inserted before each
// row i>=1 so its baseline lands at shift = pos[i]-offset.
fn dyn_sizes(rows, off, i, cp_gv, acc) {
    if (i >= len(rows)) acc
    else {
        let shift = rows[i][2] - off
        let diff = 0.0 - shift - cp_gv - rows[i][1]
        let size = diff - (rows[i - 1][0] + rows[i - 1][1])
        dyn_sizes(rows, off, i + 1, cp_gv + diff, acc ++ [size])
    }
}

// makeRows: place each row, accumulating currPos / minPos / maxPos exactly as
// MathLive v-box.ts. Returns the childWrap tops + extremes (full precision).
fn dyn_walk(rows, sizes, pstrut, i, cur, mn, mx, tops) {
    if (i >= len(rows)) {
        let r = {tops: tops, min_pos: mn, max_pos: mx}
        r
    } else {
        let cp1 = if (i == 0) cur else cur + sizes[i - 1]
        let mn1 = if (i == 0) mn else min(mn, cp1)
        let mx1 = if (i == 0) mx else max(mx, cp1)
        let top = 0.0 - pstrut - cp1 - rows[i][1]
        // match MathLive makeRows float order EXACTLY: `currPos += height + depth`
        // groups (h+d) FIRST, then adds to currPos. Left-assoc (cp1+h)+d gives a
        // different IEEE result that ceil2-tips a row top (dcases row1 -2.1 vs
        // golden -2.09).
        let cp2 = cp1 + (rows[i][0] + rows[i][1])
        dyn_walk(rows, sizes, pstrut, i + 1, cp2, min(mn1, cp2), max(mx1, cp2),
                 tops ++ [util.ceil_em2(top)])
    }
}

fn dyn_cell_heights(rows, r, out) {
    if (r >= len(rows)) out
    else dyn_cell_heights(rows, r + 1, out ++ [util.ceil_em2(rows[r][0] + rows[r][1])])
}

fn compute_dyn_metrics(row_boxes, ncols, nrows, env_name) {
    // arstrut = 0.7/0.3 × arrayskip, arrayskip = arraystretch × baselineskip(1.2).
    // Compute the products (not rounded) so the float matches MathLive.
    let arrayskip = env_arraystretch(env_name) * 1.2
    let ah = 0.7 * arrayskip
    let ad = 0.3 * arrayskip
    let rows = dyn_rows(row_boxes, ncols, nrows, ah, ad, env_jot(env_name), 0, 0.0, [])
    let tail_row = rows[nrows - 1]
    let total = tail_row[2] + tail_row[1]
    // `array` aligns to the first row's baseline; matrices/dcases centre on axis
    let off = if (env_name == "array") rows[0][0] else total / 2.0 + 0.25
    let pstrut = max(1.0, dyn_max_h(rows, 0, 0.0)) + 2.0
    let depth0 = 0.0 - (rows[0][2] - off) - rows[0][1]
    let sizes = dyn_sizes(rows, off, 1, depth0, [])
    let w = dyn_walk(rows, sizes, pstrut, 0, depth0, depth0, depth0, [])
    let box_h = w.max_pos
    let box_d = 0.0 - w.min_pos
    let m = {
        height: util.ceil_em2(box_h),
        depth: util.ceil_em2(box_d),
        box_height: box_h,
        box_depth: box_d,
        box_total: util.ceil_em2(box_h + box_d),
        vlist_height: util.ceil_em2(box_h),
        depth_holder: util.ceil_em2(box_d),
        pstrut: util.ceil_em2(pstrut),
        tops: w.tops,
        cell_heights: dyn_cell_heights(rows, 0, [])
    }
    m
}



// A single-row `equation` environment centers its content on the math axis
// (0.25em). Derive the table metrics from the content box's real height/depth
// rather than the fixed matrix estimates, so `E = mc^2` (content h=0.87) emits
// strut 0.87 / strut-bottom 1.23 instead of the generic 0.85 / 1.21.
fn equation_table_metrics(content_box) {
    let ch = content_box.height
    let cd = content_box.depth
    // Centered on the axis: the symmetric half-extent is the larger of the
    // above-axis and below-axis reach; height/depth straddle the axis.
    let axis = 0.255
    let half = max(ch - axis, cd + axis)
    let h = util.ceil_em2(axis + half)
    let d = util.ceil_em2(half - axis)
    // The depth-holder vlist runs one rounding-notch deeper than the emitted
    // box depth (the vlist-s baseline rule), matching MathLive's 0.37 vs the
    // 0.36 strut-bottom vertical-align.
    let dh = util.ceil_em2(half - axis + 0.01)
    {
        height: h,
        depth: d,
        box_total: util.ceil_em2(h + d),
        vlist_height: h,
        depth_holder: dh,
        pstrut: 3.0,
        cell_height: util.ceil_em2(h + d),
        is_equation: true,
        eq_top: 0.0 - util.ceil_em2(3.0 - (h - 0.86))
    }
}

fn matrix_table_metrics(nrows) => {
    height: 0.85 + 0.6 * float(nrows - 1),
    depth: 0.35 + 0.6 * float(nrows - 1),
    box_total: 1.21 + 1.2 * float(nrows - 1),
    vlist_height: 0.85 + 0.6 * float(nrows - 1),
    depth_holder: if (nrows == 1) 0.35
        else if (nrows == 2) 0.96
        else 0.35 + 0.6 * float(nrows - 1),
    pstrut: 3.0,
    cell_height: 1.2
}

fn row_top(metrics, row) {
    if (metrics.tops != null) metrics.tops[row]
    else if (metrics.is_equation == true) metrics.eq_top
    else matrix_row_top(metrics, row)
}

fn row_cell_height(metrics, row) {
    if (metrics.cell_heights != null) metrics.cell_heights[row]
    else metrics.cell_height
}



fn matrix_row_top(metrics, row) {
    let nrows = int(round((metrics.box_total - 1.21) / 1.2)) + 1
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
    else if (env_name == "cases" or env_name == "dcases") "{"
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
    let right_box = if (rd != null) render_matrix_delim(rd, table_box)
        else if (ld == "{") render_null_right_delim()
        else null
    let parts = (for (p in [left_box, table_box, right_box] where p != null) p)
    let children = (for (p in parts) p.element)
    let total_width = sum((for (p in parts) p.width))
    // The strut spans the taller of the content and the (axis-centred)
    // delimiter glyphs — exactly like \left..\right. The result stores the
    // full extent in the primary fields so the outer strut rounds once.
    let ch = table_box.height
    let cd = table_box.depth
    let box_h = max(ch, max(delim_ext_h(left_box), delim_ext_h(right_box)))
    let box_d = max(cd, max(delim_ext_d(left_box), delim_ext_d(right_box)))
    // A tall `{` brace (visual total > 4.0) is built as a STACKED multi-piece
    // delimiter (render_brace_mult_delim) whose segments are laid out with
    // CEIL@2 CSS tops, so its bounding box spans the SEPARATELY-rounded content
    // extent: the surrounding strut emits ceil2(h)+ceil2(-d), not the once-
    // rounded ceil2(h+d) that a single sized glyph (cases/Bmatrix) gives.
    // Store the pre-rounded extent in the public fields so the root strut does
    // not re-tip dcases from 6.41 to 6.42.
    let table_total = table_delim_total(table_box)
    let stacked_brace = ld == "{" and table_total > 4.0
    let h_raw = if (stacked_brace) util.ceil_em2(box_h) else box_h
    let d_raw = if (stacked_brace) 0.0 - util.ceil_em2(0.0 - box_d) else box_d
    box.ml_box_full(
        <span style: "display:inline-block";
            for child in children { child }
        >,
        h_raw,
        d_raw,
        total_width,
        "ord",
        0.0,
        0.0,
        h_raw
    )
}

fn delim_ext_h(b) { if (b == null) 0.0 else b.height }
fn delim_ext_d(b) { if (b == null) 0.0 else b.depth }

fn table_delim_total(table_box) {
    if (table_box.delim_total != null) table_box.delim_total
    else util.ceil_em2(table_box.height + table_box.depth)
}

fn render_null_right_delim() {
    box.ml_box(<span class: css.NULLDELIMITER, style: "width:0.12em">,
        0.0, 0.0, 0.12, "mclose")
}

fn render_matrix_delim(ch, table_box) {
    let table_total = table_delim_total(table_box)
    let nrows = int(round((table_total - 1.21) / 1.2)) + 1
    if (nrows >= 3 and (ch == "[" or ch == "]")) render_square_mult_delim(ch)
    else if (ch == "{" and table_total > 4.0) render_brace_mult_delim()
    else render_plain_sized_delim(ch, matrix_delim_level(table_total))
}

fn matrix_delim_level(visual_total) {
    if (visual_total <= 1.21) 1
    else if (visual_total <= 1.81) 2
    else if (visual_total <= 2.41) 3
    else 4
}

fn render_plain_sized_delim(ch, level) {
    let cls = if (level == 1) css.DELIM_SIZE1
        else if (level == 2) css.DELIM_SIZE2
        else if (level == 3) css.DELIM_SIZE3
        else css.DELIM_SIZE4
    let raw = matrix_sized_raw(level)
    box.ml_box_full(<span class: cls; ch>, raw.h, raw.d, 0.4, "ord", 0.0, 0.0, raw.h)
}

// full-precision Size1–4 delimiter glyph extent (axis-centred U+0028 metrics,
// shared with delimiters.ls): drives the strut so a Size3 bracket pair rounds
// 1.45+0.95003 → 2.41 once, matching MathLive.
fn matrix_sized_raw(level) {
    let h = if (level == 1) 0.85 else if (level == 2) 1.15 else if (level == 3) 1.45 else 1.75
    let d = if (level == 1) 0.35001 else if (level == 2) 0.65002 else if (level == 3) 0.95003 else 1.25003
    let r = {h: h, d: d}
    r
}

fn render_square_mult_delim(ch) {
    let pieces = if (ch == "[") ["⎣", "⎡"] else ["⎦", "⎤"]
    let tops = [0.0 - 2.25, 0.0 - 4.03]
    box.ml_box_full(
        <span class: "lm_delim-mult";
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
        2.05,
        1.55003,
        0.4,
        "ord",
        0.0,
        0.0,
        2.05
    )
}

fn render_brace_mult_delim() {
    let pieces = ["⎩", "⎪", "⎪", "⎪", "⎪", "⎨", "⎪", "⎪", "⎪", "⎪", "⎧"]
    let tops = [
        0.0 - 1.29, 0.0 - 1.29, 0.0 - 1.59, 0.0 - 1.89, 0.0 - 2.19,
        0.0 - 3.13, 0.0 - 4.27, 0.0 - 4.57, 0.0 - 4.87, 0.0 - 5.17,
        0.0 - 5.46
    ]
    let heights = [0.91, 0.3, 0.3, 0.3, 0.3, 1.81, 0.3, 0.3, 0.3, 0.3, 0.91]
    box.ml_box_full(
        <span class: "lm_delim-mult";
            <span class: "delim-size4 lm_vlist-t lm_vlist-t2";
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:3.22em";
                        for i in 0 to 10 {
                            <span style: "top:" ++ util.fmt_em(tops[i]);
                                <span class: css.PSTRUT, style: "height:3.15em">
                                <span style: "height:" ++ util.fmt_em(heights[i]) ++ ";display:inline-block"; pieces[i]>
                            >
                        }
                    >
                    <span class: css.VLIST_S; "\u200B">
                >
                <span class: css.VLIST_R;
                    <span class: css.VLIST, style: "height:2.76em">
                >
            >
        >,
        3.46,
        2.95,
        0.4,
        "ord",
        0.0,
        0.0,
        3.46
    )
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
