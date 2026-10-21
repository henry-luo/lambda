// Collage layouts and edits: every tiled template covers its area without gaps or overlaps for
// 2, 7 and 30 photos; masks, scrapbook prints, swaps, crops, history and credits.
import L: ~~.mod_library
import C: ~~.mod_collage

let manifest = input("test/demo/photo-album/fixture/photos.json", 'json')^
let lib = L.build(manifest, [for (p in manifest.photos) {photo:true, thumb:true}], "file:///corpus/")^
// 30 slots cycle through the 24 fixture photos; layouts only read their aspects
let ids30 = [for (i in 0 to 29) lib.order[i % 24]]
fn model(n, template) => {*:C.create(slice(ids30, 0, n), "Trip"), template:template, gutter:0, credits:false, caption:""}
fn aspects(n) => [for (i in 0 to (n - 1)) L.photo(lib, ids30[i]).aspect]
fn r2(v) => round(v * 100) / 100
fn overlap(a, b) => min(a.x + a.w, b.x + b.w) - max(a.x, b.x) > 0.01 and min(a.y + a.h, b.y + b.h) - max(a.y, b.y) > 0.01
// area covered exactly (sum of cell areas) and no two cells overlap
fn tiles(m, n) {
    let lay = C.layout(m, aspects(n))
    let cells = lay.cells
    let target = if (lay.mask != null) lay.mask.w * lay.mask.h else lay.width * lay.height
    let pairs = [for (i in 0 to (len(cells) - 1), j in 0 to (len(cells) - 1) where i < j and overlap(cells[i], cells[j])) [i, j]];
    {cells:len(cells), covered:r2(sum([for (c in cells) c.w * c.h]) / target), overlaps:len(pairs),
        slots:len([for (c in cells) c.slot]) == n}
}
let base = model(7, "mosaic")
let swapped = C.swap(base, 0, 3)
let panned = C.pan(C.zoom(base, 2, 2.0), 2, 0.3, -0.7)
let h1 = C.record(C.history(), base)
let undone = C.undo(h1, swapped)
let redone = C.redo(undone.history, undone.model)
let book = C.layout({*:model(7, "scrapbook"), gutter:16}, aspects(7))
let moved = C.place(model(7, "scrapbook"), 1, {x:10.0, y:20.0, w:300.0, rot:5.0})
let banded = C.layout({*:base, caption:"Summer", credits:true, gutter:20}, aspects(7));
{
    tiled: [for (t in ["grid", "mosaic", "magazine", "heart", "circle"]) [t, [for (n in [2, 7, 30]) tiles(model(n, t), n)]]],
    grid_cols: [for (c in C.layout(model(7, "grid"), aspects(7)).cells) [r2(c.x), r2(c.y), r2(c.w)]],
    masks: [C.layout(model(7, "circle"), aspects(7)).mask.clip, slice(C.layout(model(7, "heart"), aspects(7)).mask.clip, 0, 24)],
    scrapbook: [len(book.cells), all([for (c in book.cells) c.kind == "print" and c.x >= 0.0 and c.y >= 0.0 and
        c.x + c.w <= book.width and c.y + c.h <= book.height and abs(c.rot) <= 8.0])],
    moved_print: (let c = C.cell_at(C.layout(moved, aspects(7)), 1), [c.x, c.y, c.w, c.rot]),
    shuffled: C.shuffle(moved).slots[1].free == null and C.shuffle(moved).seed == 2,
    bands: [banded.caption.h, banded.credits.h, r2(banded.cells[0].y)],
    gutters: (let g = C.layout({*:model(2, "grid"), aspect:"landscape", gutter:20}, aspects(2)).cells,
        [[r2(g[0].x), r2(g[0].w)], [r2(g[1].x), r2(g[1].w)], r2(g[1].x - g[0].x - g[0].w)]),
    swap: [for (s in swapped.slots) s.id] == [for (i, s in base.slots) if (i == 0) base.slots[3].id else if (i == 3) base.slots[0].id else s.id],
    assign_present: [for (s in C.assign(base, 0, base.slots[5].id).slots) s.id][0] == base.slots[5].id,
    assign_new: [for (s in C.assign(base, 0, 999).slots) s.id][0],
    crop: [panned.slots[2].zoom, panned.slots[2].fx, panned.slots[2].fy, C.zoom(base, 0, 10.0).slots[0].zoom],
    undo: [undone.model == base, len(undone.history.future), redone.model == swapped, C.undo(C.history(), base)],
    settings: [C.set(base, "template", "grid").template, C.set(base, "template", "bogus").template,
        C.set(base, "gutter", 500).gutter, C.set(base, "aspect", "wide").aspect],
    valid: [C.valid(base), C.valid(model(1, "grid")), C.valid({*:base, template:"x"})],
    credits_line: C.credits_line(lib, model(2, "grid")),
    credits_text: C.credits_text(lib, model(2, "grid"))
}
