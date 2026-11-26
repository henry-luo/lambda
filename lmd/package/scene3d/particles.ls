// Particle emitters for the scene3d package: the builder and validation slice of M13 over the native M6
// emitter (Radiant_Design_Scene3D §8; S12.1.1v3, D9.5.6). Builders make the emitter and its controls; the
// checks mirror what the native emitter admits, so a bad description fails validation with a message
// instead of blanking the scene. Presets, effect sequences and sub-emitter rules remain M13.
import t: .transform

fn fail(message) error => error("scene3d: " ++ message)
let LIMIT = 1000000.0
// per curve kind: components per key, and the range every value must stay in
let CURVES = {'particle-size-curve': [1, 0.0, 1000000.0], 'particle-rotation-curve': [3, -1000000.0, 1000000.0],
    'particle-color-curve': [3, 0.0, 1.0], 'particle-opacity-curve': [1, 0.0, 1.0], 'particle-velocity-curve': [3, -1000000.0, 1000000.0]}
// the controls an emitter may hold beside its geometry and material, each at most once
pub let CONTROLS = ['particle-size-curve', 'particle-rotation-curve', 'particle-color-curve', 'particle-opacity-curve',
    'particle-velocity-curve', 'particle-noise', 'particle-sheet', 'particle-trail', 'particle-collision']
pub let KINDS = ['particle-emitter', *CONTROLS, 'particle-plane', 'particle-sphere']

// ---- builders
// a curve over each particle's life: kind 'size', 'rotation', 'color', 'opacity' or 'velocity'; times rise
// from 0 to 1 and values hold one (size, opacity) or three numbers per time
pub fn curve(kind, times, values) element^ =>
    if (kind == 'size') <'particle-size-curve' times: times, values: values>
    else if (kind == 'rotation') <'particle-rotation-curve' times: times, values: values>
    else if (kind == 'color') <'particle-color-curve' times: times, values: values>
    else if (kind == 'opacity') <'particle-opacity-curve' times: times, values: values>
    else if (kind == 'velocity') <'particle-velocity-curve' times: times, values: values>
    else raise fail("unknown particle curve kind")
pub fn noise(options = {}) element => <'particle-noise' *: options>
pub fn sheet(options = {}) element => <'particle-sheet' *: options>
pub fn trail(options = {}) element => <'particle-trail' *: options>
// CPU emitters collide with the scene (no shapes); GPU emitters with up to eight analytic planes and spheres
pub fn collision(options = {}, shapes = []) element => <'particle-collision' *: options, *shapes>
pub fn plane(normal = [0.0, 1.0, 0.0], offset = 0.0) element => <'particle-plane' normal: normal, offset: offset>
pub fn sphere(center = [0.0, 0.0, 0.0], radius = 1.0) element => <'particle-sphere' center: center, radius: radius>

// ---- checks (optional attributes: a choice, a flag, a number or whole number in range, a 3-vector in range)
fn chosen(n, key, options) bool^ => n[key] == null or contains(options, string(n[key]))
fn flag(n, key) bool^ => n[key] == null or n[key] is bool
fn ranged(n, key, lo, hi) bool^ => n[key] == null or (t.numeric(n[key]) and n[key] >= lo and n[key] <= hi)
fn whole(n, key, lo, hi) bool^ => ranged(n, key, lo, hi) and (n[key] == null or floor(n[key]) == n[key])
fn spread(n, key, lo, hi) bool^ => n[key] == null or (t.vector(n[key], 3) and all([for (x in n[key]) x >= lo and x <= hi]))
fn digits(v) bool^ => v is string and len(v) > 0 and all([for (i in 0 to (len(v) - 1)) contains("0123456789", slice(v, i, i + 1))])
fn leaf(n) bool^ => (len(content(n)) ^ { 1 }) == 0

// the emitter's own attributes and the make-up of its controls; `material` and `geometry` are what it draws
pub fn check_emitter(n, children, material, geometry) bool^ {
    let checked_choices = if (not (chosen(n, "backend", ["cpu", "gpu"]) and chosen(n, "space", ["local", "world"]) and
        chosen(n, "shape", ["point", "box", "sphere", "cone", "mesh"]) and chosen(n, "overflow", ["drop-new", "recycle-oldest", "error"]) and
        chosen(n, "presentation", ["mesh", "billboard"]))) raise fail("invalid particle backend, space, shape, overflow or presentation")
    let checked_counts = if (not (whole(n, "capacity", 1, 16384) and whole(n, "burst", 0, 4096)))
        raise fail("particle capacity must be a whole number in 1..16384 and burst in 0..4096")
    let checked_motion = if (not (all([for (key in ["rate", "duration", "drag", "radius", "speed", "spread"]) ranged(n, key, 0.0, LIMIT)]) and
        ranged(n, "angle", 0.0, 180.0))) raise fail("particle rate, duration and motion must be in [0,1000000] and angle in [0,180]")
    let checked_life = if (not (ranged(n, "lifetime", 0.0, LIMIT) and ranged(n, "lifetime-min", 0.0, LIMIT) and ranged(n, "size", 0.0, LIMIT)))
        raise fail("particle lifetimes and size must be in [0,1000000]")
    let longest = if (n.lifetime == null) 1.0 else n.lifetime
    let shortest = if (n["lifetime-min"] == null) longest else n["lifetime-min"]
    let checked_order = if (not (shortest > 0) or shortest > longest or n.size == 0)
        raise fail("particle lifetimes and size must be positive, with lifetime-min at most lifetime")
    let checked_vectors = if (not (spread(n, "extent", 0.0, LIMIT) and spread(n, "velocity", 0.0 - LIMIT, LIMIT) and spread(n, "gravity", 0.0 - LIMIT, LIMIT)))
        raise fail("particle extent, velocity and gravity require three numbers")
    let checked_flags = if (not (flag(n, "loop") and flag(n, "emitting"))) raise fail("particle loop and emitting must be booleans")
    let checked_seed = if (n.seed != null and not ((t.numeric(n.seed) and n.seed >= 0 and floor(n.seed) == n.seed) or digits(n.seed)))
        raise fail("particle seed must be a whole number")
    // mesh emission samples a named static geometry's surface (the caller resolves the reference)
    let mesh_shape = n.shape != null and string(n.shape) == "mesh"
    let checked_surface = if (mesh_shape != (n.surface != null) or not ranged(n, "normal-speed", 0.0 - LIMIT, LIMIT) or
        (not mesh_shape and n["normal-speed"] != null and n["normal-speed"] != 0)) raise fail("particle shape mesh requires a surface, and only it takes surface and normal-speed")
    // the native profile draws unlit basic meshes without deformation or authored instances
    let checked_profile = if ((material.type != null and string(material.type) != "basic") or material.depth_only == true or
        geometry["skin-indices"] != null or geometry["morph-positions"] != null or
        any([for (key in ["instances", "instance-count", "instance-data", "skeleton"]) n[key] != null]))
        raise fail("particles draw basic color meshes without deformation or authored instances")
    let controls = [for (c in children where (contains(CONTROLS, name(c)) ^ { false })) string(name(c))]
    let checked_once = if (any([for (i, c in controls) contains(slice(controls, 0, i), c)])) raise fail("each particle control appears at most once")
    // CPU collision queries the scene and holds no shapes; GPU collision needs one to eight analytic shapes
    let gpu = n.backend != null and string(n.backend) == "gpu"
    let collisions = [for (c in children where ((name(c) == 'particle-collision') ^ { false })) len(content(c)) ^ { 0 }]
    let checked_collision = if (any([for (k in collisions) (gpu and (k < 1 or k > 8)) or (not gpu and k > 0)]))
        raise fail("GPU particle collision needs 1..8 planes or spheres and CPU collision none")
    true
}
// a control or collision shape, on its own
pub fn check_control(n) bool^ {
    let kind = name(n)
    let spec = CURVES[kind]
    if (spec != null) {
        let times = if (n.times is array) n.times else [], values = if (n.values is array) n.values else []
        let keys = len(times) ^ { 0 }
        let checked_shape = if (not leaf(n) or keys < 2 or keys > 16 or (len(values) ^ { 0 }) != keys * spec[0])
            raise fail("a particle curve needs 2..16 times and matching values")
        if (not all([for (x in [*times, *values]) t.numeric(x)]) or times[0] != 0 or times[keys - 1] != 1 or
            any([for (i in 1 to (keys - 1)) times[i] <= times[i - 1]]) or any([for (v in values) v < spec[1] or v > spec[2]]))
            raise fail("a particle curve needs strictly increasing times from 0 to 1 and values in its range") else true
    }
    else if (kind == 'particle-noise') {
        if (not leaf(n) or not spread(n, "strength", 0.0, LIMIT) or not ranged(n, "frequency", 0.0, 1024.0))
            raise fail("particle noise needs a strength in [0,1000000] and a frequency in [0,1024]") else true
    }
    else if (kind == 'particle-sheet') {
        if (not leaf(n) or not whole(n, "columns", 1, 256) or not whole(n, "rows", 1, 256) or not chosen(n, "mode", ["lifetime", "fps"]) or
            not ranged(n, "inset", 0.0, 0.4999) or not flag(n, "loop") or not flag(n, "random-start"))
            raise fail("particle sheet needs 1..256 columns and rows, mode lifetime or fps, and an inset below 0.5") else true
    }
    else if (kind == 'particle-trail') {
        if (not leaf(n) or not whole(n, "segments", 1, 63) or not ranged(n, "lifetime", 0.000001, LIMIT) or
            not ranged(n, "width", 0.000001, LIMIT) or not flag(n, "fade"))
            raise fail("particle trail needs 1..63 segments and a positive lifetime and width") else true
    }
    else if (kind == 'particle-plane') {
        if (not leaf(n) or not spread(n, "normal", 0.0 - LIMIT, LIMIT) or not ranged(n, "offset", 0.0 - LIMIT, LIMIT) or
            (n.normal != null and all([for (x in n.normal) x == 0])))
            raise fail("particle plane needs a non-zero normal") else true
    }
    else if (kind == 'particle-sphere') {
        if (not leaf(n) or not spread(n, "center", 0.0 - LIMIT, LIMIT) or not ranged(n, "radius", 0.000001, LIMIT))
            raise fail("particle sphere needs a centre and a positive radius") else true
    }
    else true
}
