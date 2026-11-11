// Brushes (Lambda_Pkg_Paint.md PT6). A brush is plain data: the stroke module turns it, a colour
// and pen samples into an engine dab table. Kind 'round' is a simple dab brush; kind 'mypaint'
// follows libmypaint's settings-and-inputs model (dynamics.ls) and its brush files (myb.ls).
import layer: lambda.paint.layer
import dynamics: lambda.paint.dynamics
import table: lambda.paint.table

pub let PROFILES = ['mypaint', 'linear']
pub let MODES = ['buildup', 'wash']
pub let KINDS = ['round', 'mypaint']

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
//   blend: one of table.BLENDS.
// mypaint options: settings, a map of libmypaint setting names to a number or to
//   {base, inputs: {input: [[x, y], ...]}}; blend; seed for the keyed randomness.
pub fn brush(options = {}) map^ {
    if (not (options is map)) raise error("paint: brush options must be a map")
    else if (options.kind != null and not text_in(options.kind, KINDS)) raise error("paint: brush kind must be 'round' or 'mypaint'")
    else if (options.kind != null and string(options.kind) == "mypaint") mypaint(options)^
    else round(options)^
}

fn round(options) map^ {
    let o = layer.checked(options, ["kind", "size", "hardness", "spacing", "flow", "opacity", "aspect", "angle",
        "aa", "profile", "mode", "erase", "lock_alpha", "blend"])^
    let b = {kind: 'round', size: float(pick(o.size, 20.0)), hardness: float(pick(o.hardness, 0.8)),
        spacing: float(pick(o.spacing, 0.2)), flow: float(pick(o.flow, 1.0)), opacity: float(pick(o.opacity, 1.0)),
        aspect: float(pick(o.aspect, 1.0)), angle: float(pick(o.angle, 0.0)), aa: float(pick(o.aa, 1.0)),
        profile: symbol(string(pick(o.profile, 'mypaint'))), mode: symbol(string(pick(o.mode, 'buildup'))),
        erase: pick(o.erase, false), lock_alpha: float(pick(o.lock_alpha, 0.0)), blend: symbol(string(pick(o.blend, 'normal')))}
    if (not number_in(b.size, 0.5, 16384.0)) raise error("paint: brush size must be in [0.5, 16384]")
    else if (not number_in(b.hardness, 0.0, 1.0) or not number_in(b.flow, 0.0, 1.0) or not number_in(b.opacity, 0.0, 1.0)
        or not number_in(b.lock_alpha, 0.0, 1.0)) raise error("paint: brush hardness, flow, opacity and lock_alpha must be in [0, 1]")
    else if (not number_in(b.spacing, 0.01, 2.0)) raise error("paint: brush spacing must be in [0.01, 2]")
    else if (not number_in(b.aspect, 1.0, 100.0)) raise error("paint: brush aspect must be in [1, 100]")
    else if (not number_in(b.angle, -360.0, 360.0)) raise error("paint: brush angle must be in [-360, 360] degrees")
    else if (not number_in(b.aa, 0.0, 8.0)) raise error("paint: brush aa must be in [0, 8]")
    else if (not text_in(b.profile, PROFILES)) raise error("paint: brush profile must be 'mypaint' or 'linear'")
    else if (not text_in(b.mode, MODES)) raise error("paint: brush mode must be 'buildup' or 'wash'")
    else if (not text_in(b.blend, table.BLENDS)) raise error("paint: unknown brush blend mode")
    else if (not (b.erase is bool)) raise error("paint: brush erase must be a bool")
    else b
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
    let o = layer.checked(options, ["kind", "settings", "blend", "seed"])^
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
        {kind: 'mypaint', mode: 'buildup', settings: normalized, blend: symbol(string(pick(o.blend, 'normal'))),
         seed: pick(o.seed, 1), compiled: dynamics.compile(normalized)}
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
        brush({kind: 'mypaint', settings: {*: s, *: radius, *: opaque}, blend: b.blend, seed: b.seed})^
    }
    else brush({*: b, size: pick(c.size, b.size), opacity: pick(c.opacity, b.opacity)})^
}

// the distance between round-brush dab centres along a stroke.
pub fn step(b) float^ => if (b.size * b.spacing < 0.25) 0.25 else b.size * b.spacing
