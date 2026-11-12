// Strokes (Lambda_Pkg_Paint.md PT2, PT6, PT7, PT16). Pen samples become an engine dab table.
// Round brushes place dabs every `step` pixels along the stroke's arc length, measured from its
// first point; a continuation carries that arc length and later batches extend the same prefix
// sums, so a stroke split into batches places exactly the dabs of the whole stroke (S9.2.2v2).
// mypaint brushes run libmypaint's state machine (dynamics.ls) and carry its state instead.
// Bristle brushes place their path like round dabs and carry each bristle's state (PE10). On a
// pigment layer (PE8) the stroke's colour becomes its pigment, the one row of its pigment table.
import layer: lambda.paint.layer
import media: lambda.paint.media
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

// the engine row for a round-brush dab at (x, y) with pen pressure p; m holds the pigment fields.
fn row(b, c, m, x: float, y: float, p: float) array^ => table.row({x: x, y: y, radius: b.size / 2.0, aspect: b.aspect,
    angle: (b.angle + jitter(b, x, y)) * math.pi / 180.0, hardness: b.hardness, aa: b.aa, opacity: b.opacity * c[3], flow: b.flow * p,
    r: c[0], g: c[1], b: c[2], eraser: if (b.erase) 0.0 else 1.0, lock_alpha: b.lock_alpha, colorize: 0.0,
    posterize: 0.0, levels: 0.0, smudge: 0.0, smudge_length: 0.0, smudge_radius: 0.0, bucket: 0.0, dh: 0.0,
    dl: 0.0, dhsl_s: 0.0, dv: 0.0, dhsv_s: 0.0, tip: if (b.tip == null) -1.0 else 0.0, grain: b.grain,
    profile: if (string(b.profile) == "linear") 1.0 else 0.0, blend: table.blend_index(b.blend)^, *: m})

// a bristle path row: the brush at (x, y) facing the stroke direction, widened by pressure; the
// bristles carry the ink, so the row's colour is not read (PE10).
fn bristle_row(b, c, x: float, y: float, p: float, angle: float) array^ => table.row({x: x, y: y,
    radius: b.size / 2.0 * (1.0 - b.pressure_spread + b.pressure_spread * p), aspect: 1.0, angle: angle, hardness: 1.0, aa: b.aa,
    opacity: b.opacity * c[3], flow: b.flow * p, r: c[0], g: c[1], b: c[2], eraser: if (b.erase) 0.0 else 1.0, lock_alpha: 0.0,
    colorize: 0.0, posterize: 0.0, levels: 0.0, smudge: 0.0, smudge_length: 0.0, smudge_radius: 0.0, bucket: 0.0, dh: 0.0,
    dl: 0.0, dhsl_s: 0.0, dv: 0.0, dhsv_s: 0.0, tip: -1.0, grain: b.grain, profile: 0.0, blend: 0.0})

// the direction of travel along path segment i, in radians.
fn heading(path, i: int) float => math.atan2(path[i + 1][1] - path[i][1], path[i + 1][0] - path[i][0])

// the dab at arc length s, interpolated within the path segment that contains it: [x, y, pressure, heading].
fn dab_at(path, ends, lengths, s: float, n: int) array^ {
    let i = first_at_least(ends, s, 1, n)^ - 1
    let t = min(1.0, (s - ends[i]) / lengths[i]);
    [path[i][0] + (path[i + 1][0] - path[i][0]) * t, path[i][1] + (path[i + 1][1] - path[i][1]) * t,
     path[i][2] + (path[i + 1][2] - path[i][2]) * t, heading(path, i)]
}

fn place(b, color, points, carry, space, m) map^ {
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
    let head = if (carry == null) ([[path[0][0], path[0][1], path[0][2], if (n > 0) heading(path, 0) else 0.0]]) else ([])
    let placed = [*head, *[for (k in ks) dab_at(path, ends, lengths, k * step, n)^]]
    let next = if (len(ks) == 0) first else ks[len(ks) - 1] + 1
    let bristly = string(b.kind) == "bristle"
    if (len(placed) > MAX_DABS) raise error("paint: a stroke batch places more than 4000000 dabs")
    else ({table: [for (d in placed, v in (if (bristly) bristle_row(b, c, d[0], d[1], d[2], d[3])^ else row(b, c, m, d[0], d[1], d[2])^)) v],
           count: len(placed), carry: {point: path[n], arc: total, next: next}})
}

// a mypaint stroke's keyed randomness depends on the brush seed and where the stroke starts.
fn stroke_key(b, p) u64^ => rng.derive(rng.key(b.seed)^, [int(floor(float(p[0]) * 16.0)), int(floor(float(p[1]) * 16.0))])^

fn dynamic(b, color, points, carry, space, m) map^ {
    let c = in_space(color, space)^
    let key = if (carry == null) stroke_key(b, points[0])^ else carry.key
    let ink = {color: c, grain: b.grain, tip: if (b.tip == null) -1.0 else 0.0, blend: table.blend_index(b.blend)^, *: m}
    let d = dynamics.dabs(b.compiled, ink, points, carry, key)^
    if (d.count > MAX_DABS) raise error("paint: a stroke batch places more than 4000000 dabs") else d
}

// dabs(brush, colour, points, carry, space, m) -> {table, count, carry}. `points` are pen samples;
// `carry` is the previous batch's carry or null for a new stroke; m holds a pigment layer's dab
// fields (pigment, amount, thickness) and is empty on an rgba layer. A bristle brush's table is
// its path (PE10).
pub fn dabs(b, color, points, carry = null, space = 'linear', m = {}) map^ =>
    if (not valid_batch(points))
        raise error("paint: a stroke batch needs 1 to 65536 pen samples [x, y, pressure, tilt_x, tilt_y, twist, time] (at least x and y)")
    else if (string(b.kind) == "mypaint") (
        if (carry != null and not (carry is map and carry.key != null)) raise error("paint: invalid stroke continuation carry")
        else dynamic(b, color, points, carry, space, m)^)
    else if (not valid_carry(carry)) raise error("paint: invalid stroke continuation carry")
    else place(b, color, points, carry, space, m)^

// a bristle brush's starting state (bristle layout v1, PE10): count bristles spread across the
// brush, placed and varied by keyed randomness from the brush seed and the stroke's first point
// (PT8), each carrying `ink` (a linear colour on rgba, paint per unit coverage on pigment).
pub fn bristle_state(b, start, ink) array^ {
    let key = stroke_key(b, start)^
    let n = b.count
    let u = (j: int, k: int) => rng.unit(rng.at(key, j * 4 + k))
    let rows = [for (j in 0 to (n - 1)) [b.spread * 0.15 * (2.0 * u(j, 0) - 1.0),
        b.spread * (if (n == 1) 0.0 else -1.0 + 2.0 * (float(j) + u(j, 1)) / float(n)),
        min(0.99, max(0.0, b.stiffness * (1.0 + b.stiffness_jitter * (2.0 * u(j, 2) - 1.0)))), b.width,
        max(0.0, b.load * (1.0 + b.load_jitter * (2.0 * u(j, 3) - 1.0))), 0.0, 0.0, 0.0, *ink]];
    reshape([for (r in rows, v in r) float(v)], [n, 8 + len(ink)])
}

// renders a dab table into a layer: {layer, rect, work, smudge?, resolved?}. Options pass to
// radiant.paint_dabs: budget, tips, paper, smudge or smudge_buckets, resolved.
pub fn render(l, rows_table, rows: int, options = {}) map^ =>
    if (rows == 0) ({layer: l, rect: null, work: 0, smudge: options.smudge})
    else radiant.paint_dabs(l, rows_table, {budget: if (options.budget == null) BUDGET else options.budget, *: options})^

pub fn union(a, b) => if (a == null) b else if (b == null) a
    else [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]

fn same_stroke(previous, source, b, color) bool =>
    previous == null or (previous.source == source and previous.brush == b and previous.color == color)

fn smudges(b) bool => (string(b.kind) == "mypaint" and b.compiled.smudge_active) ^ { false }

// the engine options for a brush's tip and the stroke's paper; without paper, grain is off.
fn surfaces(b, paper) map => {*: if (b.tip == null) ({}) else ({tips: [b.tip]}),
    *: if (paper == null or b.grain <= 0.0) ({}) else ({paper: paper})}

// batch(space, format, brush, colour, points, previous, paper) -> {dabs, spent, options, bristles}:
// a batch's dab table (a bristle brush's path), the work the stroke spent before it, its engine
// options and, for a bristle brush, the bristle state it starts from. The pure and in-place forms
// share it; `previous` is the last batch's continuation or null. space and format are the
// target layer's blend_space and format.
pub fn batch(space, format, b, color, points, previous, paper) map^ {
    let spent = if (previous == null) 0 else previous.work
    let placed = if (paper == null) ({*: b, grain: 0.0}) else b
    let pigmented = string(format) == "pigment"
    let pig = if (pigmented) media.pigment(color)^ else null
    let m = if (pigmented) ({pigment: 0.0, amount: b.amount, thickness: b.thickness}) else ({})
    let d = dabs(placed, color, points, if (previous == null) null else previous.carry, space, m)^
    let pigments = if (pigmented) ({pigments: media.table([pig])^}) else ({})
    if (pigmented and washes(b)^) raise error("paint: wash mode paints rgba layers, not pigment layers")
    else if (string(b.kind) == "bristle") {
        let c = in_space(color, space)^
        let ink = if (pigmented) [for (k in pig.K) b.amount * k] ++ [for (v in pig.S) b.amount * v] ++ [b.thickness] else [c[0], c[1], c[2]]
        let start = if (previous == null) bristle_state(b, points[0], ink)^ else previous.bristles
        let surface = if (paper == null or b.grain <= 0.0) ({}) else ({paper: paper});
        {dabs: d, spent: spent, bristles: start,
         options: {*: surface, depletion: b.depletion, pickup: b.pickup, budget: BUDGET - spent}}
    }
    else {
        // smudge buckets live as long as the stroke: the engine returns them and the next batch passes them back.
        let carried = if (previous == null) null else previous.smudge
        let smudge = if (not smudges(b)) ({}) else if (carried == null) ({smudge_buckets: SMUDGE_BUCKETS}) else ({smudge: carried});
        {dabs: d, spent: spent, bristles: null, options: {*: smudge, *: surfaces(b, paper), *: pigments, budget: BUDGET - spent}}
    }
}

// the engine result of a batch that places no dabs.
pub fn unpainted(p) map => {rect: null, work: 0, smudge: p.options.smudge, state: p.bristles}

// renders a batch into a layer (pure form): {layer, rect, work, smudge?, state?}.
fn render_batch(l, p) map^ =>
    if (p.dabs.count == 0) ({layer: l, *: unpainted(p)})
    else if (p.bristles != null) radiant.paint_bristles(l, p.dabs.table, p.bristles, p.options)^
    else radiant.paint_dabs(l, p.dabs.table, p.options)^

pub fn washes(b) bool^ => string(b.mode)^ == "wash"

// the continuation after a batch, without the painted layer.
pub fn continued(p, painted, previous, coverage) map^ {
    let bounds = union(if (previous == null) null else previous.bounds, painted.rect);
    {carry: p.dabs.carry, smudge: painted.smudge, bristles: painted.state, coverage: coverage, rect: painted.rect, bounds: bounds,
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
    let p = batch(source.blend_space, source.format, b, color, points, previous, paper)^
    if (washes(b)^) {
        let w = wash(source, b, color, p, previous)^;
        {source: source, brush: b, color: color, *: w.cont, layer: w.layer}
    }
    else {
        let painted = render_batch(if (previous == null) source else previous.layer, p)^;
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
    let p = batch(l.blend_space, l.format, b, color, points, null, o.paper)^
    if (washes(b)^) {
        let w = wash(l, b, color, p, null)^
        l = w.layer
        return {rect: w.cont.bounds, work: w.cont.work}
    }
    let painted = if (p.dabs.count == 0) unpainted(p)
        else if (p.bristles != null) radiant.paint_bristle_stroke(l, p.dabs.table, p.bristles, p.options)^
        else radiant.paint_stroke(l, p.dabs.table, p.options)^
    return {rect: painted.rect, work: painted.work}
}
