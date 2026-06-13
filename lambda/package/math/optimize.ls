// math/optimize.ls — Post-processing optimizations

fn can_merge(a, b) {
    if (not (a is element) or not (b is element)) false
    else if (len(a) != 1 or len(b) != 1) false
    else if (not (a[0] is string) or not (b[0] is string)) false
    else a.math_data_attrs == null and b.math_data_attrs == null and
        a.class == b.class and a.style == null and b.style == null
}

fn merge_two(a, b) {
    let txt = string(a[0]) ++ string(b[0])
    <span class: a.class; txt>
}

fn do_merge(items, i, acc) {
    if (i >= len(items)) acc
    else
        (let curr = items[i],
         let last_idx = len(acc) - 1,
         let prev = acc[last_idx],
         if (can_merge(prev, curr))
             (let combined = merge_two(prev, curr),
              let head = if (last_idx > 0) slice(acc, 0, last_idx) else [],
              do_merge(items, i + 1, head ++ [combined]))
         else
              do_merge(items, i + 1, acc ++ [curr]))
}

fn merge_list(items) {
    if (len(items) <= 1) items
    else do_merge(items, 1, [items[0]])
}

fn walk(c) {
    if (c is element and len(c) > 0) build_merged(c)
    else c
}

fn build_merged(el) {
    let n = len(el)
    if (n == 0) el
    else
        (let kids = (for (i in 0 to (n - 1)) walk(el[i])),
         let merged = if (has_direct_merge_boundary_child(kids, 0)) kids else merge_list(kids),
         let items = (for (j in 0 to (len(merged) - 1)) merged[j]),
         <span class: el.class, style: el.style, id: el.id, math_data_attrs: el.math_data_attrs;
             for (c in items) c
         >)
}

fn has_direct_merge_boundary_child(items, i) {
    if (i >= len(items)) false
    else if (items[i] is element and is_merge_boundary_child(items[i])) true
    else has_direct_merge_boundary_child(items, i + 1)
}

fn is_merge_boundary_child(el) {
    starts_with_color_style(el.style) or el.class == "lm_rlap"
}

fn starts_with_color_style(style) {
    if (style == null) false
    else len(style) >= 6 and slice(style, 0, 6) == "color:"
}

fn merge_children(el) {
    if (len(el) > 0) build_merged(el)
    else el
}

pub fn coalesce(bx) {
    if (bx == null) null
    else {
        element: merge_children(bx.element),
        height: bx.height,
        depth: bx.depth,
        render_height: bx.render_height,
        render_depth: bx.render_depth,
        render_total: bx.render_total,
        left_right_render_depth: bx.left_right_render_depth,
        left_right_render_total: bx.left_right_render_total,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew,
        strut_total: bx.strut_total,
        strut_depth_em: bx.strut_depth_em
    }
}
