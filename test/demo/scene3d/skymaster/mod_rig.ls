// The Skymaster main gear as one degree of freedom: the gear angle g, 0 with the legs down and π
// with them stowed. Every part pose, keyframe track, panel value and label anchor comes from here.
// World frame: +X forward, +Y up, +Z to starboard; metres. Geometry is authored in the gear-down
// pose, so each moving part is a rigid transform {q, p}: x -> rotate(q, x) + p.
import t: lambda.scene3d.transform

pub fn add(a: any, b: any) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
pub fn sub(a: any, b: any) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
pub fn scale(a: any, k: any) => [a[0] * k, a[1] * k, a[2] * k]
pub fn dot(a: any, b: any) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
pub fn cross(a: any, b: any) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
pub fn unit(a: any) => scale(a, 1.0 / sqrt(dot(a, a)))
pub fn mirror_point(a) => [a[0], a[1], 0.0 - a[2]]
pub fn clamp(x, lo, hi) => min(hi, max(lo, x))
// the native runtime only admits seeks at whole ticks of its configured timestep
pub fn seek_time(seconds, timestep, duration) => round(clamp(seconds, 0.0, duration) / timestep) * timestep

pub let PI = 3.141592653589793
pub fn ease(u) => (let v = clamp(u, 0.0, 1.0), 0.5 - 0.5 * cos(PI * v))

// the rotary output shaft runs athwartships under the plate; its pinion meshes a fore-aft rack
pub let SHAFT = [0.0, -0.16, 0.0]
pub let JOINT_Z = 0.24
// 32 teeth on a 4 mm module: a 128 mm pitch circle, so a 180° turn takes 201 mm of rack
pub let MODULE = 0.004
pub let TEETH = 32
pub let PINION = MODULE * TEETH / 2.0
pub let STROKE = PINION * PI
// the starboard side tube: swept 35° aft of the shaft line and drooping 6°, joined by a U-joint
let SWEEP = -35.0 * PI / 180.0
let DROOP = 6.0 * PI / 180.0
pub let AXIS = [sin(SWEEP) * cos(DROOP), 0.0 - sin(DROOP), cos(SWEEP) * cos(DROOP)]
pub let JOINT = [0.0, SHAFT[1], JOINT_Z]
pub let TUBE = 0.24
pub let ROOT = add(JOINT, scale(AXIS, TUBE))
// the spring leg runs from the side tube's end to the axle; the wheel sits outboard of the axle end
pub let AXLE_END = add(ROOT, [0.15, -0.45, 0.20])
pub let WHEEL = add(AXLE_END, [0.0, 0.0, 0.075])
pub let TIRE = 0.21
pub let IDENTITY = {q: [0.0, 0.0, 0.0, 1.0], p: [0.0, 0.0, 0.0]}

// rigid transforms: a rotation about an axis through a pivot, composition, and the port-side mirror
pub fn turn(axis, angle, pivot) {
    let q = t.quaternion_axis_angle(axis, angle);
    {q: q, p: sub(pivot, t.quaternion_rotate(q, pivot))}
}
pub fn shift(offset) => {q: IDENTITY.q, p: offset}
pub fn compose(a, b) => {q: t.quaternion_multiply(a.q, b.q), p: add(t.quaternion_rotate(a.q, b.p), a.p)}
pub fn place(m, x) => add(t.quaternion_rotate(m.q, x), m.p)
// reflecting z flips handedness: the axis keeps x and y but the angle reverses
pub fn mirror(m) => {q: [0.0 - m.q[0], 0.0 - m.q[1], m.q[2], m.q[3]], p: mirror_point(m.p)}

// a U-joint at angle β turns its output unevenly; both ends of the half-turn still line up
pub fn cardan(g) => math.atan2(sin(g), cos(g) * AXIS[2])

// every moving part for gear angle g, leg flex f (radians of splay) and wheel roll w (radians)
pub fn pose(g, f, w) {
    let rod = shift([0.0 - PINION * g, 0.0, 0.0])
    let side = turn(AXIS, cardan(g), JOINT)
    let leg = compose(side, turn([1.0, 0.0, 0.0], 0.0 - f, ROOT))
    let wheel = compose(leg, turn([0.0, 0.0, 1.0], w, WHEEL));
    {rod: rod, rack: rod, pinion: turn([0.0, 0.0, 1.0], g, SHAFT),
     'side-r': side, 'leg-r': leg, 'wheel-r': wheel,
     'side-l': mirror(side), 'leg-l': mirror(leg), 'wheel-l': mirror(wheel)}
}
// the runway sits under the lowest tyre of the extended gear (flexed legs ride a little higher)
pub fn contact(f) => place(turn([1.0, 0.0, 0.0], 0.0 - f, ROOT), WHEEL)[1] - TIRE

// ---- the two operating sequences ----

let FLEX = 0.05          // static splay of the spring legs under the aircraft's weight
let HEIGHT = 1.70        // metres of air under the tyres when the gear starts moving
let APPROACH = 5.0       // studio speeds: slow enough that the rolling wheels still read
let CRUISE = 7.0

pub let SEQUENCES = {
    landing: {name: "landing", label: "Landing", duration: 7.0, start: "GEAR UP", finish: "RUNWAY",
        phases: [{until: 0.7, caption: "GEAR DOWN SELECTED"}, {until: 1.5, caption: "ACTUATOR STROKES FORWARD"},
            {until: 2.3, caption: "RACK TURNS THE PINION"}, {until: 3.6, caption: "SIDE TUBES SPIN THE LEGS DOWN"},
            {until: 4.6, caption: "GEAR DOWN & LOCKED"}, {until: 5.0, caption: "TOUCHDOWN"},
            {until: 7.0, caption: "ROLLOUT"}]},
    takeoff: {name: "takeoff", label: "Takeoff", duration: 8.0, start: "RUNWAY", finish: "GEAR UP",
        phases: [{until: 2.6, caption: "TAKEOFF ROLL"}, {until: 3.2, caption: "ROTATE & LIFT OFF"},
            {until: 3.8, caption: "GEAR UP SELECTED"}, {until: 4.6, caption: "ACTUATOR STROKES AFT"},
            {until: 5.4, caption: "RACK TURNS THE PINION"}, {until: 6.7, caption: "SIDE TUBES SPIN THE LEGS UP"},
            {until: 8.0, caption: "GEAR UP & LOCKED"}]}
}
pub fn caption(seq, sec) => (let found = [for (ph in seq.phases where sec < ph.until) ph.caption],
    if (len(found) > 0) found[0] else seq.phases[len(seq.phases) - 1].caption)

// landing: descend on a flared path while the gear comes down, touch down at 4.6 s, roll out
fn landing(sec) {
    let g = PI * (1.0 - ease((sec - 0.7) / 2.9))
    let land = 4.6
    let air = clamp(1.0 - sec / land, 0.0, 1.0)
    let after = max(0.0, sec - land)
    let brake = (APPROACH - 0.8) / 2.4
    let travel = APPROACH * min(sec, land) + APPROACH * after - 0.5 * brake * after * after
    // the legs splay on touchdown and settle with a damped bounce
    let flex = if (sec < land) 0.0 else FLEX * (1.0 - math.exp(-4.0 * after) * cos(9.0 * after))
    let roll = if (sec < land) 0.0 else 0.0 - (travel - APPROACH * land) / TIRE;
    {gear: g, height: HEIGHT * air * air, travel: travel, flex: flex, roll: roll}
}
// takeoff: accelerate, lift off at 2.8 s, unload the legs, then retract while climbing
fn takeoff(sec) {
    let accel = 3.0
    let ramp = CRUISE / accel
    let travel = if (sec < ramp) 0.5 * accel * sec * sec else 0.5 * accel * ramp * ramp + CRUISE * (sec - ramp)
    let lift = 2.8
    let after = max(0.0, sec - lift)
    let ground = if (sec < lift) travel else 0.5 * accel * ramp * ramp + CRUISE * (lift - ramp)
    // once airborne the wheels spin down; the legs spring back past straight and settle
    let spin_down = if (sec < lift) 0.0 else CRUISE / TIRE * 0.5 * (1.0 - math.exp(-2.0 * after))
    let flex = if (sec < lift) FLEX else FLEX * math.exp(-5.0 * after) * cos(7.0 * after);
    {gear: PI * ease((sec - 3.8) / 2.9), height: HEIGHT * clamp((sec - lift) / 5.2, 0.0, 1.0) ** 1.5,
     travel: travel, flex: flex, roll: 0.0 - ground / TIRE - spin_down}
}
pub fn moment(seq, sec) {
    let at = clamp(sec, 0.0, seq.duration)
    let s = if (seq.name == "landing") landing(at) else takeoff(at);
    {*: s, sec: at, stroke: PINION * s.gear, caption: caption(seq, at)}
}

// ---- native tracks: the clip samples the same functions at 30 Hz ----

pub let RATE = 30.0
pub let PARTS = ["rod", "rack", "pinion", "side-r", "leg-r", "wheel-r", "side-l", "leg-l", "wheel-l"]
pub fn sample_times(seq) => [for (i in 0 to int(seq.duration * RATE)) i / RATE]
pub fn tracks(seq) {
    let states = [for (sec in sample_times(seq)) moment(seq, sec)]
    let poses = [for (s in states) pose(s.gear, s.flex, s.roll)];
    [*[for (part in PARTS) {path: part ++ ".quaternion", kind: 'quaternion', values: [for (m in poses) *m[part].q]}],
     *[for (part in PARTS) {path: part ++ ".position", kind: 'vector', values: [for (m in poses) *m[part].p]}],
     {path: "ground.position", kind: 'vector', values: [for (s in states) *[0.0, contact(s.flex) - s.height, 0.0]]},
     {path: "dashes.position", kind: 'vector', values: [for (s in states) *[0.0 - s.travel, 0.0, 0.0]]}]
}
