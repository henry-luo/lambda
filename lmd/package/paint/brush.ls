// Brushes (Lambda_Pkg_Paint.md PT5, PT6). A brush is plain data: the stroke module turns it, a
// colour and pen samples into an engine dab table. Kind 'round' is a simple dab brush; kind
// 'mypaint' follows libmypaint's settings-and-inputs model (dynamics.ls) and its brush files
// (myb.ls); kind 'bristle' draws a trail per hair (PE10). Every kind paints rgba and pigment layers.
import layer: lambda.paint.layer
import dynamics: lambda.paint.dynamics
import table: lambda.paint.table
import surface: lambda.paint.surface

pub let PROFILES = ['mypaint', 'linear']
pub let MODES = ['buildup', 'wash']
pub let KINDS = ['round', 'mypaint', 'bristle']

fn numeric(v) bool => v is int or v is float
fn number_in(value, low: float, high: float) bool => (value is int or value is float) and value >= low and value <= high
fn text_in(value, choices) bool => (value is symbol or value is string) and string(value) in [for (c in choices) string(c)]
fn pick(value, fallback) => if (value == null) fallback else value

// brush(options): kind 'round' (the default) or 'mypaint'.
// round options:
//   size: diameter in pixels; hardness: falloff core in [0, 1]; spacing: dab step as a fraction
//   of the diameter (at least a quarter pixel); flow: per-dab gain scaled by pen pressure;
//   opacity: per-dab alpha in build-up, stroke alpha in wash; aspect >= 1 and angle (degrees)
//   shape the ellipse; aa: anti-aliased rim width; profile: 'mypaint' or 'linear' falloff;
//   mode: 'buildup' composites every dab, 'wash' accumulates coverage and composites once;
//   erase: remove paint instead of adding it; lock_alpha: recolour without adding coverage;
//   blend: one of table.BLENDS; angle_jitter: up to this many degrees of per-dab rotation, so a
//   bitmap tip does not repeat visibly (keyed by dab position, so split strokes still match).
// mypaint options: settings, a map of libmypaint setting names to a number or to
//   {base, inputs: {input: [[x, y], ...]}}; blend; seed for the keyed randomness.
// bristle options: size, spacing (path step as a fraction of the diameter), opacity, flow, aa,
//   erase and grain as for round; count, the number of bristles (1 to 4096); spread, how far
//   across the brush they reach in radii; width, each trail's width in pixels; stiffness in
//   [0, 0.99], how far a bristle lags behind the brush, with stiffness_jitter; load, the paint
//   each bristle starts with (a trail's strength is min(1, load)), with load_jitter; depletion,
//   the load a trail spends per pixel at full strength; pickup in [0, 1], how much of the canvas
//   under a tip each pixel of trail mixes into its ink; pressure_spread in [0, 1], how much pen
//   pressure widens the brush; seed for the keyed bristle placement (PT8).
// round and mypaint: tip, an (h, w) coverage plane (surface.tip) that replaces the procedural
//   dab shape; every kind: grain in [0, 1], how much the paper texture of a stroke modulates
//   coverage; on a pigment layer (PE8), amount, the pigment load per unit coverage (1), and
//   thickness, the paint body that becomes relief (amount).
pub fn brush(options = {}) map^ {
    if (not (options is map)) raise error("paint: brush options must be a map")
    else if (options.kind != null and not text_in(options.kind, KINDS)) raise error("paint: brush kind must be 'round', 'mypaint' or 'bristle'")
    else if (options.kind != null and string(options.kind) == "mypaint") mypaint(options)^
    else if (options.kind != null and string(options.kind) == "bristle") bristle(options)^
    else round(options)^
}

fn round(options) map^ {
    let o = layer.checked(options, ["kind", "size", "hardness", "spacing", "flow", "opacity", "aspect", "angle",
        "aa", "profile", "mode", "erase", "lock_alpha", "blend", "tip", "grain", "angle_jitter", "amount", "thickness"])^
    let b = {kind: 'round', size: float(pick(o.size, 20.0)), hardness: float(pick(o.hardness, 0.8)),
        spacing: float(pick(o.spacing, 0.2)), flow: float(pick(o.flow, 1.0)), opacity: float(pick(o.opacity, 1.0)),
        aspect: float(pick(o.aspect, 1.0)), angle: float(pick(o.angle, 0.0)), aa: float(pick(o.aa, 1.0)),
        profile: symbol(string(pick(o.profile, 'mypaint'))), mode: symbol(string(pick(o.mode, 'buildup'))),
        erase: pick(o.erase, false), lock_alpha: float(pick(o.lock_alpha, 0.0)), blend: symbol(string(pick(o.blend, 'normal'))),
        tip: o.tip, grain: float(pick(o.grain, 0.0)), angle_jitter: float(pick(o.angle_jitter, 0.0)),
        amount: float(pick(o.amount, 1.0)), thickness: float(pick(o.thickness, pick(o.amount, 1.0)))}
    if (not number_in(b.size, 0.5, 16384.0)) raise error("paint: brush size must be in [0.5, 16384]")
    else if (not number_in(b.hardness, 0.0, 1.0) or not number_in(b.flow, 0.0, 1.0) or not number_in(b.opacity, 0.0, 1.0)
        or not number_in(b.lock_alpha, 0.0, 1.0)) raise error("paint: brush hardness, flow, opacity and lock_alpha must be in [0, 1]")
    else if (not number_in(b.spacing, 0.01, 2.0)) raise error("paint: brush spacing must be in [0.01, 2]")
    else if (not number_in(b.aspect, 1.0, 100.0)) raise error("paint: brush aspect must be in [1, 100]")
    else if (not number_in(b.angle, -360.0, 360.0)) raise error("paint: brush angle must be in [-360, 360] degrees")
    else if (not number_in(b.angle_jitter, 0.0, 180.0)) raise error("paint: brush angle_jitter must be in [0, 180] degrees")
    else if (not number_in(b.aa, 0.0, 8.0)) raise error("paint: brush aa must be in [0, 8]")
    else if (not text_in(b.profile, PROFILES)) raise error("paint: brush profile must be 'mypaint' or 'linear'")
    else if (not text_in(b.mode, MODES)) raise error("paint: brush mode must be 'buildup' or 'wash'")
    else if (not text_in(b.blend, table.BLENDS)) raise error("paint: unknown brush blend mode")
    else if (not (b.erase is bool)) raise error("paint: brush erase must be a bool")
    else surfaced(b)^
}

// tip, grain, amount and thickness are shared by every kind.
fn surfaced(b) map^ {
    if (b.tip != null and not surface.plane_ok(b.tip)) raise error("paint: brush tip must be an (h, w) coverage array of at most 4096 x 4096")
    else if (not number_in(b.grain, 0.0, 1.0)) raise error("paint: brush grain must be in [0, 1]")
    else if (not number_in(b.amount, 0.0, 64.0) or not number_in(b.thickness, 0.0, 64.0))
        raise error("paint: brush amount and thickness must be in [0, 64]")
    else b
}

fn bristle(options) map^ {
    let o = layer.checked(options, ["kind", "size", "spacing", "opacity", "flow", "aa", "erase", "grain", "count", "spread",
        "width", "stiffness", "stiffness_jitter", "load", "load_jitter", "depletion", "pickup", "pressure_spread", "seed",
        "amount", "thickness", "mode", "tip"])^
    let size = float(pick(o.size, 24.0))
    let count = pick(o.count, 24)
    let b = {kind: 'bristle', mode: 'buildup', size: size, spacing: float(pick(o.spacing, 0.1)), opacity: float(pick(o.opacity, 1.0)),
        flow: float(pick(o.flow, 1.0)), aa: float(pick(o.aa, 1.0)), erase: pick(o.erase, false), grain: float(pick(o.grain, 0.0)),
        count: count, spread: float(pick(o.spread, 1.0)), width: float(pick(o.width, max(1.0, 1.6 * size / float(max(1, count))))),
        stiffness: float(pick(o.stiffness, 0.35)), stiffness_jitter: float(pick(o.stiffness_jitter, 0.2)),
        load: float(pick(o.load, 1.0)), load_jitter: float(pick(o.load_jitter, 0.3)), depletion: float(pick(o.depletion, 0.004)),
        pickup: float(pick(o.pickup, 0.0)), pressure_spread: float(pick(o.pressure_spread, 0.5)), seed: pick(o.seed, 1),
        tip: null, amount: float(pick(o.amount, 1.0)), thickness: float(pick(o.thickness, pick(o.amount, 1.0)))}
    // a bristle brush always builds up and has no dab tip; adjust passes its own record back
    if (o.tip != null or (o.mode != null and string(o.mode) != "buildup")) raise error("paint: a bristle brush has no tip and no wash mode")
    else if (not number_in(b.size, 0.5, 16384.0)) raise error("paint: brush size must be in [0.5, 16384]")
    else if (not (b.count is int) or b.count < 1 or b.count > 4096) raise error("paint: bristle count must be an int in [1, 4096]")
    else if (not number_in(b.spacing, 0.01, 2.0)) raise error("paint: brush spacing must be in [0.01, 2]")
    else if (not number_in(b.opacity, 0.0, 1.0) or not number_in(b.flow, 0.0, 1.0) or not number_in(b.pickup, 0.0, 1.0) or
        not number_in(b.pressure_spread, 0.0, 1.0) or not number_in(b.stiffness_jitter, 0.0, 1.0) or not number_in(b.load_jitter, 0.0, 1.0))
        raise error("paint: bristle opacity, flow, pickup, pressure_spread and the jitters must be in [0, 1]")
    else if (not number_in(b.spread, 0.0, 4.0)) raise error("paint: bristle spread must be in [0, 4] radii")
    else if (not number_in(b.width, 0.25, 64.0)) raise error("paint: bristle width must be in [0.25, 64] pixels")
    else if (not number_in(b.stiffness, 0.0, 0.99)) raise error("paint: bristle stiffness must be in [0, 0.99]")
    else if (not number_in(b.load, 0.0, 1000.0) or not number_in(b.depletion, 0.0, 1000.0)) raise error("paint: bristle load and depletion must be in [0, 1000]")
    else if (not number_in(b.aa, 0.0, 8.0)) raise error("paint: brush aa must be in [0, 8]")
    else if (not (b.erase is bool)) raise error("paint: brush erase must be a bool")
    else if (not (b.seed is int)) raise error("paint: brush seed must be an int")
    else surfaced(b)^
}

// a response curve: 2 to 64 [x, y] points with x never decreasing (libmypaint mapping).
fn valid_curve(points) bool => (points is array and len(points) >= 2 and len(points) <= 64 and
    all([for (p in points) p is array and len(p) == 2 and numeric(p[0]) and numeric(p[1])]) and
    all([for (i in 1 to (len(points) - 1)) points[i][0] >= points[i - 1][0]])) ^ { false }
fn valid_inputs(inputs) bool => (inputs is map and
    all([for (name, points in inputs) text_in(name, dynamics.INPUTS) and valid_curve(points)])) ^ { false }
fn valid_spec(spec) bool => (numeric(spec) or (spec is map and numeric(spec.base) and
    (spec.inputs == null or valid_inputs(spec.inputs)))) ^ { false }
pub fn supported(name) bool => ((string(name) ^ { "" }) in dynamics.NAMES) ^ { false }

fn normalize(spec) map => if (numeric(spec)) ({base: float(spec), inputs: {}})
    else ({base: float(spec.base), inputs: if (spec.inputs == null) ({}) else spec.inputs})

fn mypaint(options) map^ {
    let o = layer.checked(options, ["kind", "settings", "blend", "seed", "tip", "grain", "amount", "thickness"])^
    let settings = if (o.settings == null) ({}) else o.settings
    if (not (settings is map)) raise error("paint: mypaint brush settings must be a map")
    else if (not all([for (name, spec in settings) supported(name)]))
        raise error("paint: unknown or unrendered mypaint setting among " ++ string([for (name, spec in settings) string(name)]))
    else if (not all([for (name, spec in settings) valid_spec(spec)]))
        raise error("paint: a mypaint setting must be a number or {base, inputs} with known inputs and 2 to 64 ascending [x, y] points")
    else if (o.blend != null and not text_in(o.blend, table.BLENDS)) raise error("paint: unknown brush blend mode")
    else if (o.seed != null and not (o.seed is int)) raise error("paint: brush seed must be an int")
    else {
        let normalized = map([for (name, spec in settings, v in [string(name), normalize(spec)]) v]);
        surfaced({kind: 'mypaint', mode: 'buildup', settings: normalized, blend: symbol(string(pick(o.blend, 'normal'))),
         seed: pick(o.seed, 1), compiled: dynamics.compile(normalized), tip: o.tip, grain: float(pick(o.grain, 0.0)),
         amount: float(pick(o.amount, 1.0)), thickness: float(pick(o.thickness, pick(o.amount, 1.0)))})^
    }
}

// adjust(brush, {size, opacity}) -> the same brush at another diameter or opacity (PT10's
// palette controls). A mypaint brush keeps its input curves: size sets radius_logarithmic's base
// to ln(size / 2) and opacity sets opaque's base.
pub fn adjust(b, changes) map^ {
    let c = layer.checked(changes, ["size", "opacity"])^
    if (c.size != null and not number_in(c.size, 0.5, 16384.0)) raise error("paint: brush size must be in [0.5, 16384]")
    else if (c.opacity != null and not number_in(c.opacity, 0.0, 1.0)) raise error("paint: brush opacity must be in [0, 1]")
    else if (string(b.kind) == "mypaint") {
        let s = b.settings
        let radius = if (c.size == null) ({}) else ({radius_logarithmic: {base: math.log(float(c.size) / 2.0),
            inputs: if (s.radius_logarithmic == null) ({}) else s.radius_logarithmic.inputs}})
        let opaque = if (c.opacity == null) ({}) else ({opaque: {base: float(c.opacity),
            inputs: if (s.opaque == null) ({}) else s.opaque.inputs}})
        brush({kind: 'mypaint', settings: {*: s, *: radius, *: opaque}, blend: b.blend, seed: b.seed, tip: b.tip, grain: b.grain,
            amount: b.amount, thickness: b.thickness})^
    }
    else if (string(b.kind) == "bristle") brush({*: b, size: pick(c.size, b.size), opacity: pick(c.opacity, b.opacity),
        width: if (c.size == null) b.width else max(1.0, 1.6 * float(c.size) / float(b.count))})^
    else brush({*: b, size: pick(c.size, b.size), opacity: pick(c.opacity, b.opacity)})^
}

// the distance between round-brush dab centres (or bristle path positions) along a stroke.
pub fn step(b) float^ => if (b.size * b.spacing < 0.25) 0.25 else b.size * b.spacing
