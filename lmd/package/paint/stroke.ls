// Strokes (Lambda_Pkg_Paint.md PT2, PT6, PT7, PT16). Pen samples become an engine dab table.
// Round brushes place dabs every `step` pixels along the stroke's arc length, measured from its
// first point; a continuation carries that arc length and later batches extend the same prefix
// sums, so a stroke split into batches places exactly the dabs of the whole stroke (S9.2.2v2).
// mypaint brushes run libmypaint's state machine (dynamics.ls) and carry its state instead.
import layer: lambda.paint.layer
import brush: lambda.paint.brush
import dynamics: lambda.paint.dynamics
import table: lambda.paint.table
import surface: lambda.paint.surface
import rng: lambda.procart.rng
import radiant

pub let COLUMNS = table.COLUMNS
pub let BUDGET = 128000000
let MAX_POINTS = 65536
let MAX_DABS = 4000000
let SMUDGE_BUCKETS = 256

// validation predicates take arbitrary input: a failing read or comparison means "not valid".
fn numeric(v) bool => v is int or v is float
fn within(v, lo: float, hi: float) bool => (numeric(v) and v >= lo and v <= hi) ^ { false }
fn coordinate(v) bool => within(v, -1000000000.0, 1000000000.0) ^ { false }
fn unit(v) bool => within(v, 0.0, 1.0) ^ { false }
fn counter(v) bool => ((v is int) and v >= 1) ^ { false }

// a pen sample: [x, y] or [x, y, pressure, tilt_x, tilt_y, twist, time] (W3C degrees, seconds).
fn valid_point(p) bool => (p is array and len(p) >= 2 and len(p) <= 7 and coordinate(p[0]) and coordinate(p[1]) and
    (len(p) < 3 or unit(p[2])) and (len(p) < 4 or within(p[3], -90.0, 90.0)) and (len(p) < 5 or within(p[4], -90.0, 90.0)) and
    (len(p) < 6 or within(p[5], -360.0, 360.0)) and (len(p) < 7 or within(p[6], -1000000000.0, 1000000000.0))) ^ { false }
fn valid_batch(points) bool => (points is array and len(points) >= 1 and len(points) <= MAX_POINTS and
    all([for (p in points) valid_point(p)])) ^ { false }
fn valid_carry(carry) bool => (carry == null or (carry is map and valid_point(carry.point) and numeric(carry.arc) and
    carry.arc >= 0.0 and counter(carry.next))) ^ { false }
fn valid_color(color) bool => (color is array and (len(color) == 3 or len(color) == 4) and
    all([for (c in color) unit(c)])) ^ { false }
fn sample(p) array => [float(p[0]), float(p[1]), if (len(p) >= 3) float(p[2]) else 1.0]

// colours are sRGB-encoded [r, g, b] or [r, g, b, a]; a linear layer receives linear light.
fn decode(c: float) float^ => if (c <= 0.04045) c / 12.92 else ((c + 0.055) / 1.055) ** 2.4
fn channel(color, i: int, linear: bool) float^ => if (linear) decode(float(color[i]))^ else float(color[i])
fn convert(color, linear: bool) array^ =>
    [channel(color, 0, linear)^, channel(color, 1, linear)^, channel(color, 2, linear)^,
     if (len(color) == 4) float(color[3]) else 1.0]
pub fn in_space(color, space) array^ =>
    if (not valid_color(color)) raise error("paint: colour must be [r, g, b] or [r, g, b, a] in [0, 1]")
    else convert(color, string(space) == "linear")^

fn distance(a, b) float => math.sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]))

// the first index j in [lo, hi) with ends[j] >= s, or hi; ends is sorted.
fn first_at_least(ends, s: float, lo: int, hi: int) int^ =>
    if (lo >= hi) lo
    else (let mid = (lo + hi) div 2, if (ends[mid] >= s) first_at_least(ends, s, lo, mid)^ else first_at_least(ends, s, mid + 1, hi)^)

let JITTER_KEY = rng.key(91)^

// a dab's rotation offset in degrees: keyed by its position, so equal strokes and split strokes
// turn each dab the same way.
fn jitter(b, x: float, y: float) float => if (b.angle_jitter == null or b.angle_jitter <= 0.0) 0.0
    else b.angle_jitter * (2.0 * rng.unit(rng.at(JITTER_KEY, int(floor(x * 16.0)) * 65599 + int(floor(y * 16.0)))) - 1.0)

// the engine row for a round-brush dab at (x, y) with pen pressure p.
fn row(b, c, x: float, y: float, p: float) array^ => table.row({x: x, y: y, radius: b.size / 2.0, aspect: b.aspect,
    angle: (b.angle + jitter(b, x, y)) * math.pi / 180.0, hardness: b.hardness, aa: b.aa, opacity: b.opacity * c[3], flow: b.flow * p,
    r: c[0], g: c[1], b: c[2], eraser: if (b.erase) 0.0 else 1.0, lock_alpha: b.lock_alpha, colorize: 0.0,
    posterize: 0.0, levels: 0.0, smudge: 0.0, smudge_length: 0.0, smudge_radius: 0.0, bucket: 0.0, dh: 0.0,
    dl: 0.0, dhsl_s: 0.0, dv: 0.0, dhsv_s: 0.0, tip: if (b.tip == null) -1.0 else 0.0, grain: b.grain,
    profile: if (string(b.profile) == "linear") 1.0 else 0.0, blend: table.blend_index(b.blend)^})

// the dab at arc length s, interpolated within the path segment that contains it.
fn dab_at(path, ends, lengths, s: float, n: int) array^ {
    let i = first_at_least(ends, s, 1, n)^ - 1
    let t = min(1.0, (s - ends[i]) / lengths[i]);
    [path[i][0] + (path[i + 1][0] - path[i][0]) * t, path[i][1] + (path[i + 1][1] - path[i][1]) * t,
     path[i][2] + (path[i + 1][2] - path[i][2]) * t]
}

fn place(b, color, points, carry, space) map^ {
    let c = in_space(color, space)^
    let step = brush.step(b)^
    let samples = [for (p in points) sample(p)]
    let path = if (carry == null) samples else [carry.point, *samples]
    let n = len(path) - 1
    let lengths = [for (i in 0 to (n - 1) where n > 0) distance(path[i], path[i + 1])]
    // ends[j] is the arc length at path[j]; one cumsum over [arc, *lengths] keeps batches exact.
    let arc = if (carry == null) 0.0 else float(carry.arc)
    let ends = math.cumsum([arc, *lengths])
    let total = float(ends[n])^
    let first = if (carry == null) 1 else int(carry.next)^
    let stop = int(floor(total / step)) + 1
    let ks = [for (k in first to stop where k >= first and k <= stop and k * step <= total) k]
    let head = if (carry == null) ([path[0]]) else ([])
    let placed = [*head, *[for (k in ks) dab_at(path, ends, lengths, k * step, n)^]]
    let next = if (len(ks) == 0) first else ks[len(ks) - 1] + 1
    if (len(placed) > MAX_DABS) raise error("paint: a stroke batch places more than 4000000 dabs")
    else ({table: [for (d in placed, v in row(b, c, d[0], d[1], d[2])^) v], count: len(placed),
           carry: {point: path[n], arc: total, next: next}})
}

// a mypaint stroke's keyed randomness depends on the brush seed and where the stroke starts.
fn stroke_key(b, p) u64^ => rng.derive(rng.key(b.seed)^, [int(floor(float(p[0]) * 16.0)), int(floor(float(p[1]) * 16.0))])^

fn dynamic(b, color, points, carry, space) map^ {
    let c = in_space(color, space)^
    let key = if (carry == null) stroke_key(b, points[0])^ else carry.key
    let ink = {color: c, grain: b.grain, tip: if (b.tip == null) -1.0 else 0.0, blend: table.blend_index(b.blend)^}
    let d = dynamics.dabs(b.compiled, ink, points, carry, key)^
    if (d.count > MAX_DABS) raise error("paint: a stroke batch places more than 4000000 dabs") else d
}

// dabs(brush, colour, points, carry, space) -> {table, count, carry}. `points` are pen samples;
// `carry` is the previous batch's carry or null for a new stroke.
pub fn dabs(b, color, points, carry = null, space = 'linear') map^ =>
    if (not valid_batch(points))
        raise error("paint: a stroke batch needs 1 to 65536 pen samples [x, y, pressure, tilt_x, tilt_y, twist, time] (at least x and y)")
    else if (string(b.kind) == "mypaint") (
        if (carry != null and not (carry is map and carry.key != null)) raise error("paint: invalid stroke continuation carry")
        else dynamic(b, color, points, carry, space)^)
    else if (not valid_carry(carry)) raise error("paint: invalid stroke continuation carry")
    else place(b, color, points, carry, space)^

// renders a dab table into a layer: {layer, rect, work, smudge?, resolved?}. Options pass to
// radiant.paint_dabs: budget, tips, paper, smudge or smudge_buckets, resolved.
pub fn render(l, rows_table, rows: int, options = {}) map^ =>
    if (rows == 0) ({layer: l, rect: null, work: 0, smudge: options.smudge})
    else radiant.paint_dabs(l, rows_table, {budget: if (options.budget == null) BUDGET else options.budget, *: options})^

fn union(a, b) => if (a == null) b else if (b == null) a
    else [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]

fn same_stroke(previous, source, b, color) bool =>
    previous == null or (previous.source == source and previous.brush == b and previous.color == color)

fn smudges(b) bool => (string(b.kind) == "mypaint" and b.compiled.smudge_active) ^ { false }

// the engine options for a brush's tip and the stroke's paper; without paper, grain is off.
fn surfaces(b, paper) map => {*: if (b.tip == null) ({}) else ({tips: [b.tip]}),
    *: if (paper == null or b.grain <= 0.0) ({}) else ({paper: paper})}

// batch(space, brush, colour, points, previous, paper) -> {dabs, spent, options}: a batch's dab
// table, the work the stroke spent before it and its engine options. The pure and in-place
// forms share it; `previous` is the last batch's continuation or null.
pub fn batch(space, b, color, points, previous, paper) map^ {
    let spent = if (previous == null) 0 else previous.work
    let placed = if (paper == null) ({*: b, grain: 0.0}) else b
    let d = dabs(placed, color, points, if (previous == null) null else previous.carry, space)^
    // smudge buckets live as long as the stroke: the engine returns them and the next batch passes them back.
    let carried = if (previous == null) null else previous.smudge
    let smudge = if (not smudges(b)) ({}) else if (carried == null) ({smudge_buckets: SMUDGE_BUCKETS}) else ({smudge: carried});
    {dabs: d, spent: spent, options: {*: smudge, *: surfaces(b, paper), budget: BUDGET - spent}}
}

// the engine result of a batch that places no dabs.
pub fn unpainted(p) map => {rect: null, work: 0, smudge: p.options.smudge}

pub fn washes(b) bool^ => string(b.mode)^ == "wash"

// the continuation after a batch, without the painted layer.
pub fn continued(p, painted, previous, coverage) map^ {
    let bounds = union(if (previous == null) null else previous.bounds, painted.rect);
    {carry: p.dabs.carry, smudge: painted.smudge, coverage: coverage, rect: painted.rect, bounds: bounds,
     work: p.spent + painted.work}
}

fn washed(source, b, color, coverage, bounds) map^ {
    if (bounds == null) source
    else {
        let c = in_space(color, source.blend_space)^
        radiant.paint_wash(source, coverage, {color: [c[0], c[1], c[2]], opacity: b.opacity * c[3],
            eraser: if (b.erase) 0.0 else 1.0, lock_alpha: b.lock_alpha, rect: bounds})^.layer
    }
}

// wash(source, brush, colour, batch, previous) -> {layer, cont}: a wash batch paints the stroke's
// coverage, then composites it over `source`, the layer before the stroke, once (PT6). Both
// forms use it, so a wash always yields a new layer; the coverage rides in the continuation.
pub fn wash(source, b, color, p, previous) map^ {
    let painted = render(if (previous == null) layer.coverage_like(source)^ else previous.coverage,
        p.dabs.table, p.dabs.count, p.options)^
    let c = continued(p, painted, previous, painted.layer)^;
    {layer: washed(source, b, color, painted.layer, c.bounds)^, cont: c}
}

fn advance(source, b, color, points, previous, paper) map^ {
    let p = batch(source.blend_space, b, color, points, previous, paper)^
    if (washes(b)^) {
        let w = wash(source, b, color, p, previous)^;
        {source: source, brush: b, color: color, *: w.cont, layer: w.layer}
    }
    else {
        let painted = render(if (previous == null) source else previous.layer, p.dabs.table, p.dabs.count, p.options)^;
        {source: source, brush: b, color: color, *: continued(p, painted, previous, null)^, layer: painted.layer}
    }
}

// stroke options: paper, an (h, w) height plane (surface.paper) that the brush's grain reads.
pub fn options_checked(options) map^ {
    let o = layer.checked(options, ["paper"])^
    if (o.paper != null and not surface.plane_ok(o.paper))
        raise error("paint: stroke paper must be an (h, w) height array of at most 4096 x 4096")
    else o
}

// stroke_step(source, brush, colour, points, previous, options) -> continuation (PT7 pure form):
//   layer: the preview; coverage: wash coverage so far (null in build-up); rect: pixels this
//   batch could change; bounds: pixels the stroke could change so far; work: candidate pixels.
// Pass the same source, brush and colour with each batch; earlier states stay valid branches.
// Options: paper (options_checked).
pub fn stroke_step(source, b, color, points, previous = null, options = {}) map^ {
    let o = options_checked(options)^
    if (not same_stroke(previous, source, b, color))
        raise error("paint: a stroke continuation needs the same source, brush and colour")
    else advance(source, b, color, points, previous, o.paper)^
}

// stroke(var layer, brush, colour, points, options) -> {rect, work} (PT7 in-place form): paints a
// whole stroke into the layer. A build-up stroke writes through radiant.paint_stroke, which
// copies a tile only when it is shared (D7.6.4); a wash replaces the layer with its composite.
// Options as for stroke_step.
pub pn stroke(var l, b, color, points, options = {}) map^ {
    let o = options_checked(options)^
    let p = batch(l.blend_space, b, color, points, null, o.paper)^
    if (washes(b)^) {
        let w = wash(l, b, color, p, null)^
        l = w.layer
        return {rect: w.cont.bounds, work: w.cont.work}
    }
    let painted = if (p.dabs.count == 0) unpainted(p) else radiant.paint_stroke(l, p.dabs.table, p.options)^
    return {rect: painted.rect, work: painted.work}
}
