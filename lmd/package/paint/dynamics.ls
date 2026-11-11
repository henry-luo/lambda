// libmypaint-model brush dynamics (Lambda_Pkg_Paint.md PT6). A brush is a set of settings, each a
// base value plus piecewise-linear responses to inputs (pressure, speed, tilt, ...). Pen samples
// drive a small state machine that emits a dab whenever enough distance or time accumulates.
// Written from libmypaint's documented model and its brushsettings.json table (ISC, read
// 2026-11-09); no libmypaint code is used. Names, defaults and input ranges follow that table.
import rng: lambda.procart.rng
import table: lambda.paint.table

pub let INPUTS = ['pressure', 'speed1', 'speed2', 'random', 'stroke', 'direction', 'tilt_declination',
    'tilt_ascension', 'custom', 'direction_angle', 'attack_angle', 'tilt_declinationx', 'tilt_declinationy',
    'gridmap_x', 'gridmap_y', 'viewzoom', 'brush_radius', 'barrel_rotation']

// the settings this engine renders, with libmypaint's defaults.
pub let DEFAULTS = {opaque: 1.0, opaque_multiply: 0.0, opaque_linearize: 0.9, radius_logarithmic: 2.0,
    hardness: 0.8, anti_aliasing: 1.0, dabs_per_basic_radius: 0.0, dabs_per_actual_radius: 2.0,
    dabs_per_second: 0.0, radius_by_random: 0.0, speed1_slowness: 0.04, speed2_slowness: 0.8,
    speed1_gamma: 4.0, speed2_gamma: 4.0, offset_by_random: 0.0, offset_by_speed: 0.0,
    offset_by_speed_slowness: 1.0, slow_tracking: 0.0, slow_tracking_per_dab: 0.0, tracking_noise: 0.0,
    change_color_h: 0.0, change_color_l: 0.0, change_color_hsl_s: 0.0, change_color_v: 0.0,
    change_color_hsv_s: 0.0, smudge: 0.0, smudge_length: 0.5, smudge_radius_log: 0.0, smudge_bucket: 0.0,
    eraser: 0.0, stroke_threshold: 0.0, stroke_duration_logarithmic: 4.0, stroke_holdtime: 0.0,
    custom_input: 0.0, custom_input_slowness: 0.0, elliptical_dab_ratio: 1.0, elliptical_dab_angle: 90.0,
    direction_filter: 2.0, lock_alpha: 0.0, colorize: 0.0, posterize: 0.0, posterize_num: 0.05,
    pressure_gain_log: 0.0}

pub let NAMES = [for (name, v in DEFAULTS) string(name)]

// libmypaint settings this engine does not render yet, with their defaults; brush import
// reports any that a brush changes. paint_mode is spectral pigment mixing (M2).
pub let UNRENDERED = {softness: 0.0, gridmap_scale: 0.0, gridmap_scale_x: 1.0, gridmap_scale_y: 1.0,
    offset_y: 0.0, offset_x: 0.0, offset_angle: 0.0, offset_angle_asc: 0.0, offset_angle_view: 0.0,
    offset_angle_2: 0.0, offset_angle_2_asc: 0.0, offset_angle_2_view: 0.0, offset_angle_adj: 0.0,
    offset_multiplier: 0.0, restore_color: 0.0, paint_mode: 0.0, smudge_transparency: 0.0,
    smudge_length_log: 0.0, snap_to_pixel: 0.0}

// the stroke colour replaces these libmypaint colour settings.
pub let COLOR_SETTINGS = ["color_h", "color_s", "color_v"]

let MAX_DABS_PER_SAMPLE = 100000
let SAMPLE_INTERVAL = 0.01   // seconds between samples that carry no time stamp

fn clamp(v, lo, hi) => if (v < lo) lo else if (v > hi) hi else v
fn unit(v) => clamp(v, 0.0, 1.0)
fn wrap(v, period) => v - floor(v / period) * period
// libmypaint exp_decay: the share of an old value that survives t under time constant tc.
fn decay(tc, t) => if (tc > 0.001) math.exp(0.0 - t / tc) else 0.0

// --- settings

// the piecewise-linear response of one input, extrapolated past its end points (libmypaint mapping).
fn segment(xs, x, k, top) => if (k >= top or x <= xs[k + 1]) k else segment(xs, x, k + 1, top)
fn respond(c, x) {
    let k = segment(c.xs, x, 0, len(c.xs) - 2)
    let x0 = c.xs[k], x1 = c.xs[k + 1], y0 = c.ys[k], y1 = c.ys[k + 1]
    if (x0 == x1 or y0 == y1) y0 else (y1 * (x - x0) + y0 * (x1 - x)) / (x1 - x0)
}

fn spec_base(spec, name) => if (spec == null) DEFAULTS[name] else spec.base
// libmypaint's default brush scales opacity by pressure; a brush that names opaque_multiply decides itself.
fn spec_inputs(spec, name) => if (spec != null) spec.inputs
    else if (name == "opaque_multiply") ({pressure: [[0.0, 0.0], [1.0, 1.0]]}) else ({})
fn curves_of(inputs) => [for (key, points in inputs)
    {input: string(key), xs: [for (p in points) float(p[0])], ys: [for (p in points) float(p[1])]}]

// libmypaint maps speed to its inputs through log(gamma + speed), fixed so that 45 px/s gives 0.5.
fn speed_map(gamma_log) {
    let gamma = math.exp(gamma_log)
    let m = 0.015 * (45.0 + gamma);
    {gamma: gamma, m: m, q: 0.5 - m * math.log(45.0 + gamma)}
}

// compile(settings) -> the brush's constant values and its input-driven settings.
pub fn compile(settings) {
    let entries = [for (name in NAMES) {name: name, base: spec_base(settings[name], name),
        curves: curves_of(spec_inputs(settings[name], name))}]
    // curves_of always yields an array, so its length cannot fail.
    let fixed = [for (e in entries where (len(e.curves) ^ { 0 }) == 0) e]
    let constants = map([for (e in fixed, v in [e.name, e.base]) v])
    let radius_log = spec_base(settings.radius_logarithmic, "radius_logarithmic");
    {constants: constants, dynamic: [for (e in entries where (len(e.curves) ^ { 0 }) > 0) e],
     radius_log: radius_log, base_radius: math.exp(radius_log),
     speed1: speed_map(spec_base(settings.speed1_gamma, "speed1_gamma")),
     speed2: speed_map(spec_base(settings.speed2_gamma, "speed2_gamma")),
     smudge_active: spec_base(settings.smudge, "smudge") > 0.0 or (len(curves_of(spec_inputs(settings.smudge, "smudge"))) ^ { 0 }) > 0}
}

fn evaluate(cb, inp) => {*: cb.constants, *: map([for (d in cb.dynamic,
    v in [d.name, d.base + sum([for (c in d.curves) respond(c, float(inp[c.input]))])]) v])}

// --- pen samples: [x, y, pressure, tilt_x, tilt_y, twist, time]; tilt and twist in degrees
// (W3C Pointer Events), time in seconds. Missing values default to full pressure, an upright
// pen and one sample every 10 ms.

fn field(p, i, fallback) => if (len(p) > i) p[i] * 1.0 else fallback
fn sample(p, t) => [p[0] * 1.0, p[1] * 1.0, field(p, 2, 1.0), field(p, 3, 0.0), field(p, 4, 0.0),
    field(p, 5, 0.0), field(p, 6, t)]

// declination is 90 for an upright pen; ascension follows libmypaint's atan2(-x, y) convention.
fn declination(tx, ty) => 90.0 - min(90.0, math.sqrt(tx * tx + ty * ty))
fn ascension(tx, ty) => math.atan2(0.0 - tx, ty) * 180.0 / math.pi

// keyed draws in [0, 1); keys are always valid u64 values here, so a draw cannot fail.
fn draw(key, i) => rng.unit(rng.at(key, i)) ^ { 0.5 }
fn gauss(key, i) =>
    math.sqrt(-2.0 * math.log(1.0 - draw(key, 2 * i))) * math.cos(2.0 * math.pi * draw(key, 2 * i + 1))

fn inputs_at(cb, s, pressure, progress) {
    let heading = math.atan2(s.dir_dy, s.dir_dx) * 180.0 / math.pi;
    {pressure: pressure,
     speed1: math.log(cb.speed1.gamma + s.speed1_slow) * cb.speed1.m + cb.speed1.q,
     speed2: math.log(cb.speed2.gamma + s.speed2_slow) * cb.speed2.m + cb.speed2.q,
     random: draw(s.key, 8 * s.dab + 7), stroke: min(progress, 1.0),
     direction: wrap(heading + 180.0, 180.0), direction_angle: wrap(heading + 360.0, 360.0),
     tilt_declination: declination(s.tilt_x, s.tilt_y), tilt_ascension: ascension(s.tilt_x, s.tilt_y),
     custom: s.custom_value, attack_angle: wrap(ascension(s.tilt_x, s.tilt_y) - heading + 180.0, 360.0) - 180.0,
     tilt_declinationx: s.tilt_x, tilt_declinationy: s.tilt_y, gridmap_x: 0.0, gridmap_y: 0.0, viewzoom: 0.0,
     brush_radius: cb.radius_log, barrel_rotation: wrap(s.twist + 180.0, 360.0) - 180.0}
}

// the state at a stroke's first sample; it paints nothing until the pen moves or time passes.
fn begin(cb, first, key) {
    let s = {x: first[0], y: first[1], pressure: first[2], tilt_x: first[3], tilt_y: first[4], twist: first[5],
        time: first[6], actual_x: first[0], actual_y: first[1], actual_radius: cb.base_radius,
        speed1_slow: 0.0, speed2_slow: 0.0, dx_slow: 0.0, dy_slow: 0.0, dir_dx: 0.0, dir_dy: 0.0,
        progress: 0.0, started: false, custom_value: 0.0, partial: 0.0, dab: 0, seen: 1, key: key, vals: cb.constants};
    {*: s, vals: evaluate(cb, inputs_at(cb, s, unit(first[2]), 0.0))}
}

// how many dabs the move to the target needs (libmypaint count_dabs_to): by actual radius,
// base radius and time; elongated dabs space along their ellipse.
fn dabs_to(cb, s, tg, left) {
    let v = s.vals
    let dx = tg[0] - s.x, dy = tg[1] - s.y
    let a = v.elliptical_dab_angle * math.pi / 180.0
    let ratio = max(1.0, v.elliptical_dab_ratio)
    let xr = dx * math.cos(a) + dy * math.sin(a)
    let yr = (dy * math.cos(a) - dx * math.sin(a)) * ratio
    let dist = if (ratio > 1.0) math.sqrt(xr * xr + yr * yr) else math.sqrt(dx * dx + dy * dy);
    dist / s.actual_radius * max(0.0, v.dabs_per_actual_radius) + dist / cb.base_radius * max(0.0, v.dabs_per_basic_radius) +
        left * max(0.0, v.dabs_per_second)
}

// advances the state a fraction of the way to the target over dt seconds (libmypaint update_states).
fn advance(cb, s, tg, frac, dt) {
    let dtime = max(dt, 0.0001)
    let br = cb.base_radius
    let dx = (tg[0] - s.x) * frac
    let dy = (tg[1] - s.y) * frac
    let pressure = s.pressure + (tg[2] - s.pressure) * frac
    let tilted = {tilt_x: s.tilt_x + (tg[3] - s.tilt_x) * frac, tilt_y: s.tilt_y + (tg[4] - s.tilt_y) * frac,
        twist: s.twist + (tg[5] - s.twist) * frac}
    let gain = unit(pressure * math.exp(s.vals.pressure_gain_log))
    let threshold = s.vals.stroke_threshold
    let starting = not s.started and gain > threshold + 0.0001
    let ending = s.started and gain <= threshold * 0.9 + 0.0001
    let progress0 = if (starting) 0.0 else s.progress
    let ndx = dx / dtime / br
    let ndy = dy / dtime / br
    let v = evaluate(cb, inputs_at(cb, {*: s, *: tilted}, gain, progress0))
    let nspeed = math.sqrt(ndx * ndx + ndy * ndy)
    let lag = decay(math.exp(v.offset_by_speed_slowness * 0.01) - 1.0, dtime)
    // the heading filter ignores the sign of motion: it follows whichever direction is nearer.
    let fdx = dx / br
    let fdy = dy / br
    let flip = (s.dir_dx - fdx) ** 2 + (s.dir_dy - fdy) ** 2 > (s.dir_dx + fdx) ** 2 + (s.dir_dy + fdy) ** 2
    let hx = if (flip) 0.0 - fdx else fdx
    let hy = if (flip) 0.0 - fdy else fdy
    let turn = 1.0 - decay(math.exp(v.direction_filter * 0.5) - 1.0, math.sqrt(fdx * fdx + fdy * fdy))
    let progressed = max(0.0, progress0 + math.sqrt(fdx * fdx + fdy * fdy) * math.exp(0.0 - v.stroke_duration_logarithmic))
    let hold = 1.0 + v.stroke_holdtime
    let progress = if (progressed <= hold) progressed else if (hold > 10.9) 1.0 else wrap(progressed, hold)
    let x = s.x + dx
    let y = s.y + dy
    let follow = 1.0 - decay(v.slow_tracking_per_dab, 1.0);
    {*: s, *: tilted, x: x, y: y, pressure: pressure, time: s.time + dt,
     actual_x: s.actual_x + (x - s.actual_x) * follow, actual_y: s.actual_y + (y - s.actual_y) * follow,
     actual_radius: clamp(math.exp(v.radius_logarithmic), 0.2, 1000.0),
     speed1_slow: s.speed1_slow + (nspeed - s.speed1_slow) * (1.0 - decay(v.speed1_slowness, dtime)),
     speed2_slow: s.speed2_slow + (nspeed - s.speed2_slow) * (1.0 - decay(v.speed2_slowness, dtime)),
     dx_slow: s.dx_slow + (ndx - s.dx_slow) * (1.0 - lag), dy_slow: s.dy_slow + (ndy - s.dy_slow) * (1.0 - lag),
     dir_dx: s.dir_dx + (hx - s.dir_dx) * turn, dir_dy: s.dir_dy + (hy - s.dir_dy) * turn,
     custom_value: s.custom_value + (v.custom_input - s.custom_value) * (1.0 - decay(v.custom_input_slowness, 0.1)),
     progress: progress, started: (s.started or starting) and not ending, vals: v}
}

// one engine row for the dab the state now describes (libmypaint prepare_and_draw_dab).
fn dab_row(cb, c, s) {
    let v = s.vals
    let br = cb.base_radius
    let base_opaque = unit(v.opaque * v.opaque_multiply)
    // linearize: overlapping dabs should add up to the setting's opacity, not exceed it.
    let per_pixel = 1.0 + v.opaque_linearize * (max(1.0, (v.dabs_per_actual_radius + v.dabs_per_basic_radius) * 2.0) - 1.0)
    let opaque = if (v.opaque_linearize > 0.0 and base_opaque < 1.0) 1.0 - (1.0 - base_opaque) ** (1.0 / per_pixel) else base_opaque
    let jitter = v.radius_by_random != 0.0
    let radius = if (jitter) clamp(math.exp(v.radius_logarithmic + gauss(s.key, 4 * s.dab + 2) * v.radius_by_random), 0.2, 1000.0)
        else s.actual_radius
    let shrink = (s.actual_radius / radius) ** 2
    let x = s.actual_x + s.dx_slow * v.offset_by_speed * 0.1 * br + gauss(s.key, 4 * s.dab) * v.offset_by_random * br
    let y = s.actual_y + s.dy_slow * v.offset_by_speed * 0.1 * br + gauss(s.key, 4 * s.dab + 1) * v.offset_by_random * br;
    table.row({x: x, y: y, radius: radius, aspect: clamp(v.elliptical_dab_ratio, 1.0, 10.0),
        angle: v.elliptical_dab_angle * math.pi / 180.0, hardness: unit(v.hardness), aa: clamp(v.anti_aliasing, 0.0, 5.0),
        opacity: unit((if (shrink < 1.0) opaque * shrink else opaque) * c.color[3]), flow: 1.0,
        r: c.color[0], g: c.color[1], b: c.color[2], eraser: 1.0 - unit(v.eraser), lock_alpha: unit(v.lock_alpha),
        colorize: unit(v.colorize), posterize: unit(v.posterize), levels: clamp(1.0 * round(v.posterize_num * 100.0), 1.0, 128.0),
        smudge: unit(v.smudge), smudge_length: unit(v.smudge_length),
        smudge_radius: if (cb.smudge_active) clamp(math.exp(v.smudge_radius_log), 0.0, 64.0) else 0.0,
        bucket: clamp(1.0 * floor(v.smudge_bucket), 0.0, 255.0), dh: clamp(v.change_color_h, -1.0, 1.0),
        dl: clamp(v.change_color_l, -1.0, 1.0), dhsl_s: clamp(v.change_color_hsl_s, -1.0, 1.0),
        dv: clamp(v.change_color_v, -1.0, 1.0), dhsv_s: clamp(v.change_color_hsv_s, -1.0, 1.0),
        tip: c.tip, grain: c.grain, profile: 0.0, blend: c.blend})
}

// emits the dabs of one move (libmypaint stroke_to's loop) and returns {state, rows}.
fn emit(cb, c, s, tg, left, rows, budget) map^ {
    let todo = dabs_to(cb, s, tg, left)
    if (s.partial + todo >= 1.0 and budget > 0) {
        let frac = (1.0 - s.partial) / todo
        let moved = advance(cb, s, tg, frac, frac * left)
        let row = dab_row(cb, c, moved);
        emit(cb, c, {*: moved, partial: 0.0, dab: moved.dab + 1}, tg, left - frac * left, [*rows, row], budget - 1)^
    } else {
        let moved = advance(cb, s, tg, 1.0, left);
        {state: {*: moved, partial: s.partial + todo, time: tg[6], seen: s.seen + 1}, rows: rows}
    }
}

// one pen sample: slow tracking eases the target toward the pen, tracking noise jitters it.
fn move(cb, c, s, p, rows) map^ {
    // samples are validated numbers, so the difference cannot fail
    let dtime = max(0.0001, p[6] - s.time) ^ { 0.0001 }
    let ease = 1.0 - decay(s.vals.slow_tracking, 100.0 * dtime)
    let noise = s.vals.tracking_noise * cb.base_radius
    let tx = s.x + (p[0] - s.x) * ease + (if (noise > 0.0) gauss(s.key, 4 * s.dab + 3) * noise else 0.0)
    let ty = s.y + (p[1] - s.y) * ease + (if (noise > 0.0) gauss(s.key, 4 * s.dab + 5) * noise else 0.0)
    emit(cb, c, s, [tx, ty, p[2], p[3], p[4], p[5], s.time + dtime], dtime, rows, MAX_DABS_PER_SAMPLE)^
}

fn walk(cb, c, s, samples, i, rows) map^ =>
    if (i >= len(samples)) ({state: s, rows: rows})
    else (let r = move(cb, c, s, samples[i], rows)^, walk(cb, c, r.state, samples, i + 1, r.rows)^)

// a sample without a stamp is timed by its index in the stroke, so batches split anywhere agree.
fn stamped(points, first) => [for (i, p in points) sample(p, SAMPLE_INTERVAL * (first + i))]

// dabs(compiled, ink, points, carry, key) -> {table, count, carry}. `ink` is {color: [r, g, b, a]
// in the layer's space, grain, blend}; `carry` is the previous batch's carry or null.
pub fn dabs(cb, ink, points, carry, key) map^ {
    let samples = stamped(points, if (carry == null) 0 else carry.seen)
    let s0 = if (carry == null) begin(cb, samples[0], key) else carry
    let rest = if (carry == null) [for (i, p in samples where i > 0) p] else samples
    let r = walk(cb, ink, s0, rest, 0, [])^;
    {table: [for (row in r.rows, v in row) v], count: len(r.rows), carry: r.state}
}
