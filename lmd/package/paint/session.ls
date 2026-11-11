// Interactive painting sessions (Lambda_Pkg_Paint.md PT10). A session is a value: one paint layer
// over a paper colour, the stroke in progress, the rectangle that still has to be presented, and
// stroke-level undo and redo. A history entry is the previous layer value, which shares every tile
// the stroke did not touch (S9.1.2), never a replay of strokes.
import layers: lambda.paint.layer
import strokes: lambda.paint.stroke
import surface: lambda.paint.surface
import radiant

// bytes of forked tiles the undo history may keep; the oldest entries are dropped first.
pub let HISTORY_BUDGET = 268435456

fn union(a, b) => if (a == null) b else if (b == null) a
    else [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]

// a rectangle clipped to the layer, or null when nothing of it is inside.
fn clipped(l, r) {
    if (r == null) null
    else {
        let x0 = max(0, int(floor(r[0])))
        let y0 = max(0, int(floor(r[1])))
        let x1 = min(l.width, int(ceil(r[2])))
        let y1 = min(l.height, int(ceil(r[3])))
        if (x1 <= x0 or y1 <= y0) null else [x0, y0, x1, y1]
    }
}

fn whole(l) array => [0, 0, l.width, l.height]

// bytes an undo entry keeps alive: the previous versions of the tiles its stroke forked.
fn cost(l, r) int {
    let c = clipped(l, r)
    if (c == null) 0
    else {
        let tiles = ((c[2] - 1) div l.tile - c[0] div l.tile + 1) * ((c[3] - 1) div l.tile - c[1] div l.tile + 1)
        tiles * l.tile * l.tile * 16
    }
}

// the newest entries whose cost fits the budget; the newest entry always stays.
fn trimmed(entries, budget) {
    let n = len(entries)
    let kept = [for (i in 0 to (n - 1) where n > 0 and (i == n - 1 or
        sum([for (j in i to (n - 1)) entries[j].cost]) <= budget)) entries[i]]
    kept
}

fn checked_color(color) array^ => strokes.in_space(color, 'srgb')^

// a paper texture option: null, an (h, w) height plane, or a spec {kind, seed, size} that
// surface.paper builds. A spec is kept so a painting file can rebuild the texture.
fn texture_of(t) map^ {
    if (t == null) ({texture: null, spec: null})
    else if (t is map) {
        // the kind is kept as a symbol, so a spec read back from a painting file compares equal
        let o = {kind: symbol(string(t.kind)), seed: if (t.seed == null) 1 else t.seed, size: if (t.size == null) 256 else t.size}
        let plane = surface.paper(o.kind, {seed: o.seed, size: o.size})^;
        {texture: plane, spec: o}
    }
    else if (surface.plane_ok(t)) ({texture: t, spec: null})
    else raise error("paint: session texture must be null, an (h, w) height array or {kind, seed, size}")
}

// session(width, height, options) -> a blank session. Options: paper, the sRGB [r, g, b] colour
// under the paint (white); texture, the paper's height field for brush grain (see texture_of);
// blend_space ('linear'); history_budget in bytes (HISTORY_BUDGET).
pub fn session(width: int, height: int, options = {}) map^ {
    let o = layers.checked(options, ["paper", "texture", "blend_space", "history_budget"])^
    let surfaced = texture_of(o.texture)^
    let paper = if (o.paper == null) ([1.0, 1.0, 1.0]) else o.paper
    let budget = if (o.history_budget == null) HISTORY_BUDGET else o.history_budget
    let checked = checked_color(paper)^
    if (len(paper) != 3) raise error("paint: session paper must be an sRGB [r, g, b] colour")
    else if (not (budget is int) or budget < 0) raise error("paint: history_budget must be a non-negative int")
    else {
        let l = layers.layer(width, height, {blend_space: if (o.blend_space == null) 'linear' else o.blend_space})^;
        {layer: l, paper: [checked[0], checked[1], checked[2]], texture: surfaced.texture, texture_spec: surfaced.spec,
         budget: budget, undo: [], redo: [], active: null, dirty: whole(l)}
    }
}

// end(session, pointer) -> the session with that pointer's stroke committed as one undo entry.
// Ending a pointer without a stroke changes nothing.
pub fn end(s, pointer) map^ {
    let a = s.active
    if (a == null or a.pointer != pointer) s
    else if (a.cont == null or a.cont.bounds == null) ({*: s, active: null})
    else {
        let entry = {layer: a.source, rect: a.cont.bounds, cost: cost(a.source, a.cont.bounds)};
        {*: s, active: null, undo: trimmed([*s.undo, entry], s.budget), redo: []}
    }
}

// begin(session, pointer, brush, colour) -> a stroke for that pointer over the current layer.
// One stroke paints at a time: a press from another pointer first ends the stroke in progress.
pub fn begin(s, pointer, b, color) map^ {
    let free = if (s.active == null) s else end(s, s.active.pointer)^
    let checked = strokes.in_space(color, 'srgb')^;
    {*: free, active: {pointer: pointer, brush: b, color: color, source: free.layer, cont: null}}
}

// feed(var session, pointer, samples) -> {rect, work} (PT7 in-place form): continues the stroke
// with pen samples in layer pixels ([x, y, pressure, tilt_x, tilt_y, twist, time] rows). rect is
// the layer area this batch changed, which joins the dirty rect (null when nothing changed);
// work is the batch's candidate pixels.
pub pn feed(var s, pointer, samples) map^ {
    if (s.active == null or s.active.pointer != pointer) { raise error("paint: no stroke in progress for this pointer") }
    if (len(samples) == 0) { return {rect: null, work: 0} }
    let previous = s.active.cont
    let p = strokes.batch(s.layer.blend_space, s.active.brush, s.active.color, samples, previous, s.texture)^
    if (strokes.washes(s.active.brush)^) {
        let w = strokes.wash(s.active.source, s.active.brush, s.active.color, p, previous)^
        s.layer = w.layer
        s.active.cont = w.cont
    }
    else {
        // the first batch copies the tiles it touches, which the stroke's undo entry keeps;
        // later batches write those tiles in place (D7.6.4)
        let painted = if (p.dabs.count == 0) strokes.unpainted(p)
            else radiant.paint_stroke(s.layer, p.dabs.table, p.options)^
        s.active.cont = strokes.continued(p, painted, previous, null)^
    }
    let r = clipped(s.layer, s.active.cont.rect)
    s.dirty = union(s.dirty, r)^
    return {rect: r, work: s.active.cont.work - p.spent}
}

// cancel(session, pointer) -> the session as it was before that pointer's stroke.
pub fn cancel(s, pointer) map^ {
    let a = s.active
    if (a == null or a.pointer != pointer) s
    else {
        let painted = if (a.cont == null) null else clipped(a.source, a.cont.bounds);
        {*: s, layer: a.source, active: null, dirty: union(s.dirty, painted)}
    }
}

// with_texture(session, texture) -> the session with another paper texture for later strokes
// (texture as for session()); painted pixels do not change.
pub fn with_texture(s, t) map^ {
    let surfaced = texture_of(t)^;
    {*: s, texture: surfaced.texture, texture_spec: surfaced.spec}
}

pub fn can_undo(s) bool => (len(s.undo) > 0) ^ { false }
pub fn can_redo(s) bool => (len(s.redo) > 0) ^ { false }

// undo(session) -> the layer before the last stroke; a stroke in progress is cancelled instead.
pub fn undo(s) map^ {
    if (s.active != null) cancel(s, s.active.pointer)^
    else if (len(s.undo) == 0) s
    else {
        let n = len(s.undo)^
        let entry = s.undo[n - 1];
        {*: s, layer: entry.layer, undo: [for (i in 0 to (n - 2) where n > 1) s.undo[i]],
         redo: [*s.redo, {layer: s.layer, rect: entry.rect, cost: entry.cost}],
         dirty: union(s.dirty, clipped(entry.layer, entry.rect))}
    }
}

// redo(session) -> the stroke undo last removed, back in place.
pub fn redo(s) map^ {
    if (s.active != null or len(s.redo) == 0) s
    else {
        let n = len(s.redo)^
        let entry = s.redo[n - 1];
        {*: s, layer: entry.layer, redo: [for (i in 0 to (n - 2) where n > 1) s.redo[i]],
         undo: trimmed([*s.undo, {layer: s.layer, rect: entry.rect, cost: entry.cost}], s.budget),
         dirty: union(s.dirty, clipped(entry.layer, entry.rect))}
    }
}

// clear(session) -> a blank layer, undoable as one entry.
pub fn clear(s) map^ {
    let free = if (s.active == null) s else end(s, s.active.pointer)^
    let blank = layers.layer(free.layer.width, free.layer.height, {blend_space: free.layer.blend_space})^
    let rect = whole(free.layer);
    {*: free, layer: blank, undo: trimmed([*free.undo, {layer: free.layer, rect: rect, cost: cost(free.layer, rect)}],
        free.budget), redo: [], dirty: rect}
}

// mark(session, rect) -> the session with `rect` (or the whole layer for null) to present again.
pub fn mark(s, rect = null) map => ({*: s, dirty: union(s.dirty, if (rect == null) whole(s.layer) else clipped(s.layer, rect))})

fn background(s) array^ => strokes.in_space([s.paper[0], s.paper[1], s.paper[2], 1.0], s.layer.blend_space)^

// frame(session) -> {session, rect, pixels}: the dirty rectangle as (h, w, 4) sRGB u8 samples over
// the paper, ready for dom.set_canvas_pixels_rect, and the session with nothing left to present.
// rect and pixels are null when nothing changed.
pub fn frame(s) map^ {
    let r = clipped(s.layer, s.dirty)
    if (r == null) ({session: {*: s, dirty: null}, rect: null, pixels: null})
    else ({session: {*: s, dirty: null}, rect: r,
        pixels: layers.present(s.layer, {rect: r, background: background(s)^})^})
}

// color_at(session, x, y) -> the sRGB [r, g, b] in [0, 1] shown at a layer pixel (paint over paper).
pub fn color_at(s, x: int, y: int) array^ {
    let px = layers.present(s.layer, {rect: [x, y, x + 1, y + 1], background: background(s)^})^[0, 0];
    [px[0] / 255.0, px[1] / 255.0, px[2] / 255.0]
}

// in_layer(samples, box, s, pressure) -> pen samples moved from viewport CSS pixels into layer
// pixels, for a canvas whose bitmap is the layer and whose on-screen box is `box` ({x, y, width,
// height}, as dom.bounding_box gives). A non-null `pressure` replaces every sample's pressure.
pub fn in_layer(samples, box, s, pressure = null) array {
    let sx = float(s.layer.width) / box.width
    let sy = float(s.layer.height) / box.height;
    [for (r in samples) [(r[0] - box.x) * sx, (r[1] - box.y) * sy, if (pressure == null) r[2] else pressure,
        r[3], r[4], r[5], r[6]]]
}
