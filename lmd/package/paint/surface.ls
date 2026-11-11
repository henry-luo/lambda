// Paper and tips (Lambda_Pkg_Paint.md PT5, PE4): procedural surfaces for the engine's paper grain
// and bitmap dab tips. A paper is a tileable (h, w) height field in [0, 1]; a dab with grain > 0
// reaches its peaks fully and its valleys by (1 - grain). A tip is an (h, w) coverage plane in
// [0, 1] that fills the dab's ellipse box. Both are plain typed arrays, keyed by a seed, so equal
// specs give equal planes. Noise and randomness reuse procart's keyed generators.
import noise: lambda.procart.noise
import rng: lambda.procart.rng
import layers: lambda.paint.layer

pub let PAPERS = ['smooth', 'cold_press', 'rough', 'canvas']
pub let TIPS = ['chalk', 'flat', 'speckle']
let PI = 3.141592653589793

fn text_in(value, choices) bool => (value is symbol or value is string) and string(value) in [for (c in choices) string(c)]
fn unit(v) => if (v < 0.0) 0.0 else if (v > 1.0) 1.0 else v

// a field rescaled so its lowest sample is 0 and its highest is 1.
fn normalized(rows) array {
    let flat = flatten(rows)
    let lo = min(flat)
    let hi = max(flat)
    let span = if (hi - lo > 0.000001) hi - lo else 1.0;
    as_float([for (row in rows) [for (v in row) (v - lo) / span]])
}

// tileable fractal noise of the given size: the period equals the size, so the paper repeats
// across the canvas without seams.
fn tiled_noise(seed, size: int, spec) array^ {
    let gen = noise.make(rng.key(seed)^, {*: spec, period: [float(size), float(size)]})^
    noise.field(gen, size, size)
}

// a woven canvas: alternate warp and weft threads, each a raised half-sine, plus a little tooth.
fn canvas_weave(seed, size: int) array^ {
    let thread = 8
    let tooth = tiled_noise(seed, size, {kind: 'value', octaves: 2, frequency: 0.125})^;
    [for (y in 0 to (size - 1)) [for (x in 0 to (size - 1))
        (let over = ((x div thread) + (y div thread)) % 2 == 0,
         let u = (float(x % thread) + 0.5) / float(thread),
         let v = (float(y % thread) + 0.5) / float(thread),
         (if (over) math.sin(PI * u) else math.sin(PI * v)) * 0.85 + tooth[y][x] * 0.15)]]
}

// paper(kind, options) -> an (h, w) height plane in [0, 1]. Kinds: 'smooth' (flat, so grain has
// no effect), 'cold_press' (fine fractal tooth), 'rough' (coarse ridged tooth), 'canvas' (woven
// threads). Options: size, the tile's side in pixels (16 to 1024, default 256); seed (1).
pub fn paper(kind, options = {}) array^ {
    let o = layers.checked(options, ["size", "seed"])^
    let size = if (o.size == null) 256 else o.size
    let seed = if (o.seed == null) 1 else o.seed
    if (not text_in(kind, PAPERS)) raise error("paint: paper kind must be 'smooth', 'cold_press', 'rough' or 'canvas'")
    else if (not (size is int) or size < 16 or size > 1024) raise error("paint: paper size must be an int in 16..1024")
    else if (not (seed is int)) raise error("paint: paper seed must be an int")
    else if (string(kind) == "smooth") as_float([[1.0]])
    else if (string(kind) == "cold_press") normalized(tiled_noise(seed, size, {kind: 'simplex', octaves: 4, frequency: 0.09})^)
    else if (string(kind) == "rough") normalized(tiled_noise(seed, size, {kind: 'simplex', fractal: 'ridged', octaves: 4, frequency: 0.035})^)
    else normalized(canvas_weave(seed, size)^)
}

// coverage of a soft-edged disk at normalized radius d (0 at the centre, 1 at the rim).
fn disk(d: float) float => unit((1.0 - d) / 0.12)

fn chalk_tip(seed, size: int) array^ {
    let grit = normalized(tiled_noise(seed, size, {kind: 'simplex', octaves: 3, frequency: 0.22})^)
    let half = float(size) / 2.0;
    as_float([for (y in 0 to (size - 1)) [for (x in 0 to (size - 1))
        (let d = math.sqrt((float(x) + 0.5 - half) ** 2 + (float(y) + 0.5 - half) ** 2) / half,
         disk(d) * unit((grit[y][x] - 0.3) / 0.45))]])
}

// a flat brush: a wide, short bar with soft ends; dab angle and aspect turn it into a nib.
fn flat_tip(size: int) array {
    let half = float(size) / 2.0;
    as_float([for (y in 0 to (size - 1)) [for (x in 0 to (size - 1))
        (let u = abs(float(x) + 0.5 - half) / half, let v = abs(float(y) + 0.5 - half) / half,
         unit((0.96 - u) / 0.08) * unit((0.32 - v) / 0.08))]])
}

// scattered round grains inside the disk, as a stippling or splatter brush leaves.
fn speckle_tip(seed, size: int) array^ {
    let k = rng.key(seed)^
    let count = 36
    let half = float(size) / 2.0
    let dots = [for (i in 0 to (count - 1))
        (let a = rng.unit(rng.derive(k, ["angle", i])^) * 2.0 * PI,
         let r = math.sqrt(rng.unit(rng.derive(k, ["radius", i])^)) * 0.82,
         let s = 0.05 + 0.07 * rng.unit(rng.derive(k, ["size", i])^),
         [half + math.cos(a) * r * half, half + math.sin(a) * r * half, s * half])];
    as_float([for (y in 0 to (size - 1)) [for (x in 0 to (size - 1))
        max([0.0, *[for (d in dots) unit(1.0 - math.sqrt((float(x) + 0.5 - d[0]) ** 2 + (float(y) + 0.5 - d[1]) ** 2) / d[2])]])]])
}

// tip(kind, options) -> an (h, w) coverage plane in [0, 1] for a brush's `tip`. Kinds: 'chalk'
// (a grainy disk), 'flat' (a wide bar for a flat or calligraphic brush), 'speckle' (scattered
// grains). Options: size, the plane's side in pixels (8 to 256, default 64); seed (1).
pub fn tip(kind, options = {}) array^ {
    let o = layers.checked(options, ["size", "seed"])^
    let size = if (o.size == null) 64 else o.size
    let seed = if (o.seed == null) 1 else o.seed
    if (not text_in(kind, TIPS)) raise error("paint: tip kind must be 'chalk', 'flat' or 'speckle'")
    else if (not (size is int) or size < 8 or size > 256) raise error("paint: tip size must be an int in 8..256")
    else if (not (seed is int)) raise error("paint: tip seed must be an int")
    else if (string(kind) == "chalk") chalk_tip(seed, size)^
    else if (string(kind) == "flat") flat_tip(size)
    else speckle_tip(seed, size)^
}

// a plane the engine accepts as a tip or paper: a numeric (h, w) array of at most 4096 x 4096.
pub fn plane_ok(p) bool => (p is array and len(shape(p)) == 2 and shape(p)[0] >= 1 and shape(p)[1] >= 1 and
    shape(p)[0] <= 4096 and shape(p)[1] <= 4096) ^ { false }
