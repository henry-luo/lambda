// Justified photo grid: rows keep each photo's aspect ratio and fill the width (S12.1.3, pure).
// Layout values are CSS pixels relative to the grid content box.

pub let HEADER = 44          // date section header
pub let SECTION_GAP = 12     // extra space after a section
pub let ZOOMS = [80, 120, 160, 220, 300]

// contiguous runs of equal section keys: [{key, label, start, end}]
pub fn sections(items) {
    let starts = [for (i, it in items where i == 0 or items[i - 1].key != it.key) i];
    [for (j, s in starts) {key:items[s].key, label:items[s].label, start:s,
        end:if (j + 1 < len(starts)) starts[j + 1] else len(items)}]
}

// first index k > s at which a row of aspects s..k-1 fills the width; n when the row stays short
fn row_end(P, s, n, width, target, gap) =>
    [for (k in (s + 1) to n where k == n or (P[k] - P[s]) * target + (k - s - 1) * gap >= width) k][0]

// rows of one section as [{start, end, h}], indexes into the section's own aspect list
fn pack(P, s, n, width, target, gap) {
    if (s >= n) []
    else (
        let k = row_end(P, s, n, width, target, gap),
        let total = P[k] - P[s],
        let full = total * target + (k - s - 1) * gap >= width,
        // a short last row keeps the target height instead of stretching
        let h = if (full) (width - (k - s - 1) * gap) / total else target,
        [{start:s, end:k, h:h}, *pack(P, k, n, width, target, gap)]
    )
}

// items: [{id, aspect, key, label}] in display order
pub fn layout(items, width, target, gap) {
    let secs = sections(items)
    let packed = [for (sec in secs) (
        let aspects = [for (it in slice(items, sec.start, sec.end)) it.aspect],
        let P = [0.0, *math.cumsum(aspects)],
        let rows = pack(P, 0, len(aspects), width, target, gap),
        {sec:sec, P:P, rows:rows, height:HEADER + sum([for (r in rows) r.h + gap]) + SECTION_GAP})]
    let offsets = [0.0, *math.cumsum([for (s in packed) s.height])];
    {width:width, target:target, gap:gap, height:offsets[last],
        headers:[for (i, s in packed) {y:offsets[i], label:s.sec.label, key:s.sec.key, count:s.sec.end - s.sec.start}],
        rows:[for (i, s in packed, j, r in s.rows) (
            let tops = [0.0, *math.cumsum([for (q in s.rows) q.h + gap])],
            {section:i, y:offsets[i] + HEADER + tops[j], h:r.h,
                tiles:[for (k in r.start to (r.end - 1)) (
                    let it = items[s.sec.start + k],
                    {id:it.id, x:(s.P[k] - s.P[r.start]) * r.h + (k - r.start) * gap, w:it.aspect * r.h})]})]}
}

// row indexes overlapping [top - margin, top + height + margin]
pub fn window(lay, top, height, margin) {
    let lo = top - margin
    let hi = top + height + margin;
    [for (i, r in lay.rows where r.y + r.h >= lo and r.y <= hi) i]
}

pub fn headers_in(lay, top, height, margin) =>
    [for (i, h in lay.headers where h.y + HEADER >= top - margin and h.y <= top + height + margin) i]

// the section whose header has scrolled under the top edge
pub fn current_header(lay, top) => [for (h in lay.headers where h.y <= top) h][last] or lay.headers[0]

pub fn locate(lay, id) => [for (r in lay.rows, t in r.tiles where t.id == id) {id:id, x:t.x, y:r.y, w:t.w, h:r.h}][0]

pub fn tile_at(lay, x, y) => [for (r in lay.rows, t in r.tiles where y >= r.y and y < r.y + r.h and x >= t.x and x < t.x + t.w) t.id][0]

// ids whose tiles intersect the rectangle (content coordinates)
pub fn tiles_in(lay, x0, y0, x1, y1) =>
    [for (r in lay.rows, t in r.tiles where r.y + r.h >= y0 and r.y <= y1 and t.x + t.w >= x0 and t.x <= x1) t.id]

// scroll offset that keeps `anchor` (a located tile and the pointer's offset into the viewport) in place
pub fn anchored_top(before, after, pointer_y, scroll_top) {
    let fraction = if (before.h > 0) (pointer_y + scroll_top - before.y) / before.h else 0.0;
    max(0.0, after.y + fraction * after.h - pointer_y)
}

// grid neighbour for arrow keys: previous/next in order, or the nearest tile in the adjacent row
pub fn neighbour(lay, ids, id, key) {
    let at = index_of(ids, id)
    let here = locate(lay, id);
    if (at == null or here == null) ids[0]
    else if (key == "ArrowLeft") ids[max(0, at - 1)]
    else if (key == "ArrowRight") ids[min(len(ids) - 1, at + 1)]
    else (
        let rows = lay.rows,
        let ri = [for (i, r in rows where r.y == here.y) i][0],
        let target = if (key == "ArrowUp") ri - 1 else ri + 1,
        let centre = here.x + here.w / 2,
        if (target < 0 or target >= len(rows)) id
        else [for (t in rows[target].tiles order by abs(t.x + t.w / 2 - centre)) t.id][0]
    )
}

pub fn zoom_step(target, delta) {
    let at = [for (i, z in ZOOMS where z >= target) i][0] or len(ZOOMS) - 1;
    ZOOMS[max(0, min(len(ZOOMS) - 1, at + delta))]
}
