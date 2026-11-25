// The otter: one smooth skinned body (a lofted head, body and tapering tail) with a cute face (cream
// mask, whisker pads, glossy nose and eyes, round ears, whiskers), two-segment forelegs ending in
// mitten paws with pale claws, and thighs with webbed hind feet. It faces +Z from the chest and swims
// with an up-and-down body wave and alternating hind strokes, or floats on its back with its paws
// on its chest; the host rolls the body over to float, so the clips only bend joints.
import s: lambda.scene3d
import physics: lambda.physics
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig

// ---- skeleton: bones in skin-index (document) order with their joint positions in skeleton space
let J = {root: [0.0, 0.0, 0.0], neck: [0.0, 0.01, 0.1], head: [0.0, 0.02, 0.2], hip: [0.0, 0.0, -0.14],
    t1: [0.0, 0.0, -0.32], t2: [0.0, 0.0, -0.5], t3: [0.0, 0.0, -0.7]}
fn side(p, x) any^ => [p[0] * x, p[1], p[2]]
let SHOULDER = [0.08, -0.05, 0.08]
let ELBOW = [0.088, -0.12, 0.072]
let WRIST = [0.09, -0.152, 0.1]
let HIP_JOINT = [0.1, -0.055, -0.24]
let ANKLE = [0.118, -0.112, -0.28]
let TOES = [0.124, -0.124, -0.4]
// name, joint, bone end (for automatic weights), parent index; listed in document order
let BONES = [["ot-root", J.root, [0.0, 0.0, -0.14], -1], ["ot-neck", J.neck, J.head, 0], ["ot-head", J.head, [0.0, 0.0, 0.45], 1],
    ["ot-hip", J.hip, J.t1, 0], ["ot-tail1", J.t1, J.t2, 3], ["ot-tail2", J.t2, J.t3, 4], ["ot-tail3", J.t3, [0.0, 0.0, -0.95], 5],
    ["ot-hl-l", HIP_JOINT, ANKLE, 3], ["ot-hf-l", ANKLE, TOES, 7], ["ot-hl-r", side(HIP_JOINT, -1.0)^, side(ANKLE, -1.0)^, 3],
    ["ot-hf-r", side(ANKLE, -1.0)^, side(TOES, -1.0)^, 9],
    ["ot-fl-l", SHOULDER, ELBOW, 0], ["ot-fw-l", ELBOW, WRIST, 11], ["ot-fl-r", side(SHOULDER, -1.0)^, side(ELBOW, -1.0)^, 0],
    ["ot-fw-r", side(ELBOW, -1.0)^, side(WRIST, -1.0)^, 13]]
fn joint(i) any^ => BONES[i][1]
fn rel(i) any^ => (let p = BONES[i][3], if (p < 0) joint(i)^ else rig.sub(joint(i)^, joint(p)^)^)
fn bone(i) any^ => <bone id: BONES[i][0], position: rel(i)^, *[for (k, b in BONES where b[3] == i) bone(k)^]>
pub fn skeleton() any^ => <skeleton id: "otter-rig", bone(0)^>
fn segments(indices) any^ => [for (i in indices) [i, BONES[i][1], BONES[i][2]]]
let SPINE = segments([0, 1, 2, 3, 4, 5, 6])^

// ---- palette
let BACK = w.col("#5c3b27")
let SIDE = w.col("#80583b")
let CROWN = w.col("#8e6849")
let CREAM = w.col("#e8d6b9")
let TAN = w.col("#b08a62")
let LIMB = w.col("#4b3122")
let PAW = w.col("#3b271b")
let NOSE = w.col("#1d1511")
let EYE = w.col("#120d0b")
let SHINE = w.col("#ffffff")
let CLAW = w.col("#d9c8a6")
let EAR_IN = w.col("#3a2519")

// ---- the body: a loft whose half-width, half-height and centre height follow these keys
let KEYS = [[0.47, 0.004, 0.004, -0.005], [0.455, 0.028, 0.022, -0.006], [0.43, 0.048, 0.04, -0.002], [0.39, 0.068, 0.06, 0.006],
    [0.34, 0.088, 0.078, 0.014], [0.29, 0.1, 0.09, 0.02], [0.24, 0.098, 0.088, 0.018], [0.19, 0.09, 0.082, 0.01],
    [0.12, 0.108, 0.098, 0.004], [0.04, 0.128, 0.112, 0.0], [-0.06, 0.138, 0.12, 0.0], [-0.16, 0.136, 0.116, 0.0],
    [-0.25, 0.118, 0.1, 0.0], [-0.33, 0.088, 0.072, 0.0], [-0.42, 0.066, 0.05, 0.0], [-0.55, 0.05, 0.036, 0.0],
    [-0.7, 0.034, 0.024, 0.0], [-0.84, 0.018, 0.013, 0.0], [-0.95, 0.004, 0.004, 0.0]]
let NOSE_Z = 0.47
let TAIL_Z = -0.95
fn cr(a, b, c, d, k) => 0.5 * (2.0 * b + (c - a) * k + (2.0 * a - 5.0 * b + 4.0 * c - d) * k * k + (3.0 * b - a - 3.0 * c + d) * k * k * k)
// a Catmull-Rom sample of the keys at z: [half width, half height, centre y]
pub fn profile(z) any^ {
    let n = len(KEYS)
    let i = min(n - 2, max(0, len([for (k in KEYS where k[0] > z) k]) - 1))
    let a = KEYS[max(0, i - 1)], b = KEYS[i], c = KEYS[i + 1], d = KEYS[min(n - 1, i + 2)]
    let k = (b[0] - z) / (b[0] - c[0]);
    [max(0.003, cr(a[1], b[1], c[1], d[1], k)), max(0.003, cr(a[2], b[2], c[2], d[2], k)), cr(a[3], b[3], c[3], d[3], k)]
}
// fur by position: a cream face mask, throat and belly; a lighter crown; dark back and tail
fn fur(z, up, grain) any^ => (let belly = 1.0 - u.clamp((up + 0.5) / 0.35, 0.0, 1.0),
    let face = if (z > 0.3) u.clamp((0.25 - up) / 0.4, 0.0, 1.0) + u.clamp((z - 0.4) / 0.04, 0.0, 1.0) else 0.0,
    let throat = if (z > 0.08 and z < 0.3) u.clamp((0.0 - up) / 0.3, 0.0, 1.0) else 0.0,
    let cream = min(1.0, max(belly, max(face, throat))),
    // the underside pales from a tan belly to a cream chest, throat and face
    let under = rig.mix(TAN, CREAM, u.clamp((z + 0.2) / 0.3, 0.0, 1.0))^,
    let base = if (z > 0.2) rig.mix(SIDE, CROWN, u.clamp(up, 0.0, 1.0))^ else rig.mix(SIDE, BACK, u.clamp(up * 1.3, 0.0, 1.0))^,
    let tail = if (z < -0.4) u.clamp((-0.4 - z) / 0.5, 0.0, 1.0) * 0.35 else 0.0,
    [for (c in rig.mix(rig.mix(base, under, cream)^, BACK, tail)^) c * (0.94 + 0.12 * grain)])
fn body_point(z, a) any^ => (let pr = profile(z)^, let sn = sin(a), [cos(a) * pr[0], pr[2] + sn * pr[1] * (if (sn < 0.0) 0.92 else 1.0), z])
fn body_sample(t, around) any^ => (let z = NOSE_Z + (TAIL_Z - NOSE_Z) * t, let a = around * u.TAU,
    {p: body_point(z, a)^, c: fur(z, sin(a), rig.hash(floor(t * 60.0), floor(around * 24.0))^)^})
// a point on the head surface at angle a, pulled in a little, for seating the face parts
fn on_head(z, a, inset) any^ => (let pr = profile(z)^, [cos(a) * pr[0] * inset, pr[2] + sin(a) * pr[1] * inset, z])

fn bind_body(p) any^ => rig.weights(p, SPINE, 1.4)^
fn on(i) any^ => rig.rigid(i)^
fn ball(c, r, rgb, i) any^ => rig.ellipsoid(c, r, rig.IDENTITY, rig.flat(rgb)^, on(i)^)^
// ---- the face
fn face() any^ {
    let eye_l = on_head(0.345, 0.66, 0.9)^
    let ear_l = on_head(0.255, 1.12, 0.92)^
    let pad = [0.022, -0.012, 0.433]
    return [ball([0.022, -0.012, 0.433], [0.026, 0.02, 0.022], CREAM, 2)^, ball([-0.022, -0.012, 0.433], [0.026, 0.02, 0.022], CREAM, 2)^,
        ball([0.0, -0.032, 0.41], [0.03, 0.016, 0.025], CREAM, 2)^,
        ball([0.0, 0.012, 0.458], [0.022, 0.014, 0.013], NOSE, 2)^, ball([0.006, 0.02, 0.466], [0.005, 0.003, 0.003], SHINE, 2)^,
        *[for (x in [-1.0, 1.0]) ball(side(eye_l, x)^, [0.018, 0.018, 0.016], EYE, 2)^],
        *[for (x in [-1.0, 1.0]) ball(rig.add(side(eye_l, x)^, [x * -0.004, 0.008, 0.011])^, [0.005, 0.005, 0.004], SHINE, 2)^],
        *[for (x in [-1.0, 1.0]) ball(side(ear_l, x)^, [0.022, 0.018, 0.012], CROWN, 2)^],
        *[for (x in [-1.0, 1.0]) ball(rig.add(side(ear_l, x)^, [x * -0.002, -0.001, 0.007])^, [0.013, 0.01, 0.006], EAR_IN, 2)^],
        *[for (x in [-1.0, 1.0], j in 0 to 2) rig.tube([side(pad, x)^, [x * (0.09 + 0.006 * j), -0.006 - 0.014 * j + 0.012, 0.42 - 0.012 * j]],
            [0.0018, 0.0006], 4, (k, a) => SHINE, on(2)^)^]]
}
// ---- legs: a tube from inside the body to the wrist or ankle, then a paw or webbed foot with claws
fn claw(base, dir, i) any^ => rig.tube([base, rig.add(base, dir)^], [0.0042, 0.0008], 5, (k, a) => CLAW, on(i)^)^
fn foreleg(x, upper, lower) any^ {
    let bones = segments([0, upper, lower])^
    let paw = rig.add(side(WRIST, x)^, [0.0, -0.01, 0.022])^
    return [rig.tube([side([0.06, -0.025, 0.08], x)^, side(SHOULDER, x)^, side(ELBOW, x)^, side(WRIST, x)^], [0.036, 0.033, 0.026, 0.021], 10,
            (k, a) => LIMB, (p) any^ => rig.weights(p, bones, 1.2)^)^,
        ball(paw, [0.027, 0.014, 0.03], PAW, lower)^,
        *[for (j in 0 to 4, let dx = (j - 2) * 0.0105) ball(rig.add(paw, [x * dx, -0.002, 0.026 - 0.004 * abs(j - 2)])^, [0.0075, 0.007, 0.008], PAW, lower)^],
        *[for (j in 0 to 4, let dx = (j - 2) * 0.0105) claw(rig.add(paw, [x * dx, -0.004, 0.032 - 0.004 * abs(j - 2)])^, [x * dx * 0.2, -0.006, 0.014], lower)^]]
}
fn hindleg(x, upper, foot) any^ {
    let bones = segments([3, upper, foot])^
    let centre = rig.add(side(ANKLE, x)^, [0.004 * x, -0.01, -0.06])^
    return [rig.tube([side([0.06, -0.02, -0.23], x)^, side(HIP_JOINT, x)^, side(ANKLE, x)^], [0.046, 0.042, 0.026], 10,
            (k, a) => LIMB, (p) any^ => rig.weights(p, bones, 1.2)^)^,
        ball(centre, [0.034, 0.011, 0.066], PAW, foot)^,
        *[for (j in 0 to 4, let dx = (j - 2) * 0.016) ball(rig.add(centre, [x * dx, 0.0, -0.06 + 0.006 * abs(j - 2)])^, [0.009, 0.008, 0.014], PAW, foot)^],
        *[for (j in 0 to 4, let dx = (j - 2) * 0.016) claw(rig.add(centre, [x * dx, -0.002, -0.074 + 0.006 * abs(j - 2)])^, [x * dx * 0.15, -0.004, -0.014], foot)^]]
}
pub fn buffer() any^ => rig.merge([rig.surface(48, 22, body_sample, bind_body, true)^, *(face()^),
    *(foreleg(1.0, 11, 12)^), *(foreleg(-1.0, 13, 14)^), *(hindleg(1.0, 7, 8)^), *(hindleg(-1.0, 9, 10)^)])^
pub fn geometry() any^ => rig.geometry("g-otter", buffer()^)^

// ---- clips: joints turn in their parents' frames
fn xq(a: float) => [sin(a / 2.0), 0.0, 0.0, cos(a / 2.0)]
fn turn(pitch: float, yaw: float) any^ => u.quat(yaw, pitch, 0.0)^
let STEPS = [for (i in 0 to 24) i / 24.0]
fn joint_track(name, period, f) any^ => s.track(name ++ ".quaternion", [for (k in STEPS) k * period], [for (k in STEPS, c in f(k * u.TAU)^) c], 'quaternion')^
fn rest_dir(i) any^ => rig.unit(rig.sub(BONES[i][2], BONES[i][1])^)^
// paws on the chest while floating: an IK chain to an elbow beside the ribs and a paw under the chin
fn chest_paws(x, a) any^ {
    let upper = if (x > 0.0) 11 else 13
    let rub = 0.012 * sin(3.0 * a) * x
    return rig.chain(rig.IDENTITY, side(SHOULDER, x)^, side([0.1, -0.11, 0.07], x)^, side([0.016 + rub, -0.112, 0.17], x)^, rest_dir(upper)^, rest_dir(upper + 1)^)^
}
pub let SWIM_PERIOD = 0.85
pub let FLOAT_PERIOD = 3.0
pub fn clips() any^ => [
    s.clip("otter-swim", [
        joint_track("ot-hip", SWIM_PERIOD, (a: float) => xq(0.12 * sin(a)))^,
        joint_track("ot-tail1", SWIM_PERIOD, (a: float) => xq(0.2 * sin(a - 0.8)))^,
        joint_track("ot-tail2", SWIM_PERIOD, (a: float) => xq(0.24 * sin(a - 1.6)))^,
        joint_track("ot-tail3", SWIM_PERIOD, (a: float) => xq(0.3 * sin(a - 2.4)))^,
        joint_track("ot-neck", SWIM_PERIOD, (a: float) => xq(-0.08 * sin(a + 0.3)))^,
        joint_track("ot-head", SWIM_PERIOD, (a: float) => xq(-0.05 * sin(a + 0.6)))^,
        joint_track("ot-hl-l", SWIM_PERIOD, (a: float) => xq(0.15 + 0.55 * sin(a)))^,
        joint_track("ot-hl-r", SWIM_PERIOD, (a: float) => xq(0.15 + 0.55 * sin(a + math.pi)))^,
        joint_track("ot-hf-l", SWIM_PERIOD, (a: float) => xq(0.25 * sin(a - 0.9)))^,
        joint_track("ot-hf-r", SWIM_PERIOD, (a: float) => xq(0.25 * sin(a + math.pi - 0.9)))^,
        joint_track("ot-fl-l", SWIM_PERIOD, (a: float) => xq(1.45 + 0.06 * sin(a)))^,
        joint_track("ot-fl-r", SWIM_PERIOD, (a: float) => xq(1.45 + 0.06 * sin(a + math.pi)))^,
        joint_track("ot-fw-l", SWIM_PERIOD, (a: float) => xq(0.7))^,
        joint_track("ot-fw-r", SWIM_PERIOD, (a: float) => xq(0.7))^], {duration: SWIM_PERIOD, autoplay: true}),
    s.clip("otter-float", [
        joint_track("ot-hip", FLOAT_PERIOD, (a: float) => xq(0.06 * sin(a)))^,
        joint_track("ot-tail1", FLOAT_PERIOD, (a: float) any^ => turn(-0.12, 0.2 * sin(a))^)^,
        joint_track("ot-tail2", FLOAT_PERIOD, (a: float) any^ => turn(-0.08, 0.28 * sin(a - 0.9))^)^,
        joint_track("ot-tail3", FLOAT_PERIOD, (a: float) any^ => turn(0.0, 0.35 * sin(a - 1.8))^)^,
        joint_track("ot-neck", FLOAT_PERIOD, (a: float) => xq(0.8 + 0.05 * sin(a)))^,
        joint_track("ot-head", FLOAT_PERIOD, (a: float) any^ => turn(0.45, 0.22 * sin(a * 0.5))^)^,
        joint_track("ot-hl-l", FLOAT_PERIOD, (a: float) => xq(-0.5 + 0.25 * sin(a)))^,
        joint_track("ot-hl-r", FLOAT_PERIOD, (a: float) => xq(-0.5 + 0.25 * sin(a + 2.0)))^,
        joint_track("ot-hf-l", FLOAT_PERIOD, (a: float) => xq(-0.4 + 0.2 * sin(a + 1.0)))^,
        joint_track("ot-hf-r", FLOAT_PERIOD, (a: float) => xq(-0.4 + 0.2 * sin(a + 3.0)))^,
        joint_track("ot-fl-l", FLOAT_PERIOD, (a: float) any^ => chest_paws(1.0, a)^[0])^,
        joint_track("ot-fw-l", FLOAT_PERIOD, (a: float) any^ => chest_paws(1.0, a)^[1])^,
        joint_track("ot-fl-r", FLOAT_PERIOD, (a: float) any^ => chest_paws(-1.0, a)^[0])^,
        joint_track("ot-fw-r", FLOAT_PERIOD, (a: float) any^ => chest_paws(-1.0, a)^[1])^], {duration: FLOAT_PERIOD, autoplay: false})]
pub fn body(x, y, z, heading) any^ => physics.body("otter", [
    physics.collider("otter-shape", 'sphere', {radius: 0.05, groups: {membership: 0, filter: 0}}),
    skeleton()^,
    s.mesh(geometry()^, "m-otter", {id: "otter-mesh", skeleton: "otter-rig", 'cast-shadow': true, bounds: [-0.5, -0.5, -1.2, 0.5, 0.5, 0.8]})],
    {type: 'kinematic-position', position: [x, y, z], quaternion: u.quat(heading)})
pub fn materials() any^ => [s.material('lambert', {id: "m-otter", color: "#ffffff"})]
