// math/optimize.ls — Post-processing optimizations

fn can_merge(a, b) {
    if (not (a is element) or not (b is element)) false
    else if (len(a) != 1 or len(b) != 1) false
    else if (not (a[0] is string) or not (b[0] is string)) false
    else a.class == b.class and a.style == null and b.style == null
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
    if (c is element and len(c) > 1) build_merged(c)
    else c
}

fn build_merged(el) {
    let n = len(el)
    if (n == 0) el
    else
        (let kids = (for (i in 0 to (n - 1)) walk(el[i])),
         let merged = merge_list(kids),
         let items = (for (j in 0 to (len(merged) - 1)) merged[j]),
         <span class: el.class, style: el.style;
             for (c in items) c
         >)
}

fn merge_children(el) {
    if (len(el) > 1) build_merged(el)
    else el
}

pub fn coalesce(bx) {
    if (bx == null) null
    else {
        element: merge_children(bx.element),
        height: bx.height,
        depth: bx.depth,
        width: bx.width,
        type: bx.type,
        italic: bx.italic,
        skew: bx.skew
    }
}
