// The beaver: one chubby skinned body (a round head with puffy cheeks, a pale muzzle, a black nose and
// orange buck teeth, beady eyes and round ears), a flat scaly paddle of a tail, little forepaws with
// claws holding a twig, and big webbed hind feet. It faces +Z from the chest. It swims head-up with
// alternating hind strokes and the twig under its chin, gnaws the twig while the boat rests, and
// slaps its tail on the water for joy at a catch.
import s: lambda.scene3d
import physics: lambda.physics
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig

// ---- skeleton: bones in skin-index (document) order with their joint positions in skeleton space
fn side(p, x) => [p[0] * x, p[1], p[2]]
// long enough forelegs to bring the twig up past the round chest to the mouth
let SHOULDER = [0.1, -0.06, 0.12]
let ELBOW = [0.115, -0.15, 0.15]
let WRIST = [0.105, -0.215, 0.18]
let HIP_JOINT = [0.11, -0.07, -0.21]
let ANKLE = [0.13, -0.15, -0.24]
let TOES = [0.14, -0.162, -0.38]
// name, joint, bone end (for automatic weights), parent index; listed in document order
let BONES = [["bv-root", [0.0, 0.0, 0.0], [0.0, 0.0, -0.2], -1], ["bv-neck", [0.0, 0.02, 0.1], [0.0, 0.03, 0.17], 0],
    ["bv-head", [0.0, 0.03, 0.17], [0.0, 0.0, 0.37], 1], ["bv-hip", [0.0, 0.0, -0.2], [0.0, 0.0, -0.33], 0],
    ["bv-tail1", [0.0, 0.0, -0.33], [0.0, 0.0, -0.5], 3], ["bv-tail2", [0.0, 0.0, -0.5], [0.0, 0.0, -0.68], 4],
    ["bv-hl-l", HIP_JOINT, ANKLE, 3], ["bv-hf-l", ANKLE, TOES, 6], ["bv-hl-r", side(HIP_JOINT, -1.0), side(ANKLE, -1.0), 3],
    ["bv-hf-r", side(ANKLE, -1.0), side(TOES, -1.0), 8],
    ["bv-fl-l", SHOULDER, ELBOW, 0], ["bv-fw-l", ELBOW, WRIST, 10], ["bv-fl-r", side(SHOULDER, -1.0), side(ELBOW, -1.0), 0],
    ["bv-fw-r", side(ELBOW, -1.0), side(WRIST, -1.0), 12]]
fn joint(i) => BONES[i][1]
fn rel(i) any^ => (let p = BONES[i][3], if (p < 0) joint(i) else rig.sub(joint(i), joint(p))^)
fn bone(i) any^ => <bone id: BONES[i][0], position: rel(i)^, *[for (k, b in BONES where b[3] == i) bone(k)^]>
pub fn skeleton() any^ => <skeleton id: "beaver-rig", bone(0)^>
fn segments(indices) => [for (i in indices) [i, BONES[i][1], BONES[i][2]]]
let SPINE = segments([0, 1, 2, 3])
fn rest_dir(i) any^ => rig.unit(rig.sub(BONES[i][2], BONES[i][1])^)^

// ---- palette
let BACK = w.col("#8c5c37")
let SIDE = w.col("#a8743f")
let BELLY = w.col("#bf9163")
let MUZZLE = w.col("#d4ae84")
let CHEEK = w.col("#b07d4b")
let NOSE = w.col("#24180f")
let EYE = w.col("#110b08")
let SHINE = w.col("#ffffff")
let TOOTH = w.col("#efae46")
let EAR = w.col("#5e3b22")
let EAR_IN = w.col("#3d2617")
let PAW = w.col("#3d2a1e")
let CLAW = w.col("#d8c6a2")
let TAIL = w.col("#4b3d35")
let TAIL_SCALE = w.col("#352a24")
let TWIG = w.col("#7b6a3c")
let LEAF = w.col("#6f9d45")

// ---- the body: a chubby loft whose half-width, half-height and centre height follow these keys
let KEYS = [[0.375, 0.004, 0.004, -0.01], [0.36, 0.035, 0.03, -0.012], [0.335, 0.06, 0.05, -0.008], [0.3, 0.085, 0.072, 0.0],
    [0.25, 0.12, 0.104, 0.014], [0.2, 0.132, 0.116, 0.02], [0.15, 0.13, 0.116, 0.014], [0.09, 0.15, 0.13, 0.006],
    [0.0, 0.18, 0.15, 0.0], [-0.09, 0.195, 0.16, -0.005], [-0.18, 0.185, 0.152, 0.0], [-0.26, 0.15, 0.125, 0.005],
    [-0.32, 0.095, 0.075, 0.01], [-0.355, 0.045, 0.03, 0.01], [-0.37, 0.004, 0.004, 0.01]]
let NOSE_Z = 0.375
let RUMP_Z = -0.37
fn cr(a, b, c, d, k) => 0.5 * (2.0 * b + (c - a) * k + (2.0 * a - 5.0 * b + 4.0 * c - d) * k * k + (3.0 * b - a - 3.0 * c + d) * k * k * k)
// a Catmull-Rom sample of the keys at z: [half width, half height, centre y]
fn profile(z) any^ {
    let n = len(KEYS)
    let i = min(n - 2, max(0, len([for (k in KEYS where k[0] > z) k]) - 1))
    let a = KEYS[max(0, i - 1)], b = KEYS[i], c = KEYS[i + 1], d = KEYS[min(n - 1, i + 2)]
    let k = (b[0] - z) / (b[0] - c[0]);
    [max(0.003, cr(a[1], b[1], c[1], d[1], k)), max(0.003, cr(a[2], b[2], c[2], d[2], k)), cr(a[3], b[3], c[3], d[3], k)]
}
// warm chestnut fur: darker along the back, a little paler underneath, a pale muzzle
fn fur(z, up, grain) any^ => (let under = u.clamp((-0.2 - up) / 0.5, 0.0, 1.0),
    let base = rig.mix(SIDE, BACK, u.clamp(up * 1.2, 0.0, 1.0))^, let snout = if (z > 0.31) u.clamp((z - 0.31) / 0.03, 0.0, 1.0) else 0.0,
    [for (c in rig.mix(rig.mix(base, BELLY, under)^, MUZZLE, snout * 0.8)^) min(1.0, c * (0.93 + 0.12 * grain))])
fn body_sample(t, around) any^ => (let z = NOSE_Z + (RUMP_Z - NOSE_Z) * t, let a = around * u.TAU, let pr = profile(z)^, let sn = sin(a),
    {p: [cos(a) * pr[0], pr[2] + sn * pr[1] * (if (sn < 0.0) 0.94 else 1.0), z], c: fur(z, sn, rig.hash(floor(t * 50.0), floor(around * 24.0))^)^})
// a point on the head at angle a around it, pulled in a little, for seating the face parts
fn on_head(z, a, inset) any^ => (let pr = profile(z)^, [cos(a) * pr[0] * inset, pr[2] + sin(a) * pr[1] * inset, z])
fn on(i) any^ => rig.rigid(i)^
fn ball(c, r, rgb, i) any^ => rig.ellipsoid(c, r, rig.IDENTITY, rig.flat(rgb)^, on(i)^)^

// ---- the face: puffy cheeks, a pale muzzle, nose, buck teeth, beady eyes, round ears and whiskers
fn face() any^ {
    let eye_l = on_head(0.27, 0.72, 0.93)^
    let ear_l = on_head(0.195, 1.2, 0.95)^
    return [*[for (x in [-1.0, 1.0]) ball([x * 0.062, -0.014, 0.285], [0.062, 0.054, 0.055], CHEEK, 2)^],
        ball([0.0, -0.014, 0.333], [0.052, 0.041, 0.04], MUZZLE, 2)^,
        ball([0.0, 0.014, 0.37], [0.026, 0.017, 0.014], NOSE, 2)^, ball([0.007, 0.023, 0.379], [0.006, 0.003, 0.003], SHINE, 2)^,
        *[for (x in [-1.0, 1.0]) ball([x * 0.0085, -0.047, 0.358], [0.0085, 0.017, 0.006], TOOTH, 2)^],
        *[for (x in [-1.0, 1.0]) ball(side(eye_l, x), [0.016, 0.016, 0.014], EYE, 2)^],
        *[for (x in [-1.0, 1.0]) ball(rig.add(side(eye_l, x), [x * -0.003, 0.007, 0.009])^, [0.0045, 0.0045, 0.0035], SHINE, 2)^],
        *[for (x in [-1.0, 1.0]) ball(side(ear_l, x), [0.026, 0.024, 0.013], EAR, 2)^],
        *[for (x in [-1.0, 1.0]) ball(rig.add(side(ear_l, x), [x * -0.002, -0.002, 0.008])^, [0.015, 0.013, 0.006], EAR_IN, 2)^],
        *[for (x in [-1.0, 1.0], j in 0 to 2) rig.tube([[x * 0.04, -0.01, 0.345], [x * (0.12 + 0.008 * j), 0.004 - 0.016 * j, 0.33 - 0.012 * j]],
            [0.0016, 0.0006], 4, (k, a) => SHINE, on(2)^)^]]
}
// ---- the tail: a flat oval paddle crossed with scales
fn tail_sample(t, around) any^ => (let a = around * u.TAU, let z = -0.31 - 0.36 * t,
    let half_w = 0.02 + 0.07 * math.pow(max(0.0, sin(math.pi * (0.12 + 0.86 * t))), 0.55), let x = cos(a) * half_w,
    {p: [x, 0.012 + sin(a) * 0.02 * (1.0 - 0.5 * t), z], c: if (sin(x * 95.0) > 0.8 or sin(z * 95.0) > 0.8) TAIL_SCALE else TAIL})
fn tail() any^ => rig.surface(16, 24, tail_sample, (p) any^ => rig.weights(p, segments([3, 4, 5]), 1.2)^, true)^
// ---- legs: little forepaws with claws, big webbed hind feet
fn claw(base, dir, i) any^ => rig.tube([base, rig.add(base, dir)^], [0.0038, 0.0007], 5, (k, a) => CLAW, on(i)^)^
fn foreleg(x, upper, lower) any^ {
    let bones = segments([0, upper, lower])
    let paw = rig.add(side(WRIST, x), [0.0, -0.01, 0.018])^
    return [rig.tube([side([0.07, -0.03, 0.12], x), side(SHOULDER, x), side(ELBOW, x), side(WRIST, x)], [0.04, 0.036, 0.028, 0.022], 10,
            (k, a) => SIDE, (p) any^ => rig.weights(p, bones, 1.2)^)^,
        ball(paw, [0.025, 0.016, 0.026], PAW, lower)^,
        *[for (j in 0 to 3, let dx = (j - 1.5) * 0.011) claw(rig.add(paw, [x * dx, -0.004, 0.024])^, [x * dx * 0.2, -0.006, 0.012], lower)^]]
}
fn hindleg(x, upper, foot) any^ {
    let bones = segments([3, upper, foot])
    let centre = rig.add(side(ANKLE, x), [0.004 * x, -0.01, -0.07])^
    return [rig.tube([side([0.07, -0.03, -0.2], x), side(HIP_JOINT, x), side(ANKLE, x)], [0.06, 0.05, 0.03], 10,
            (k, a) => SIDE, (p) any^ => rig.weights(p, bones, 1.2)^)^,
        ball(centre, [0.05, 0.012, 0.08], PAW, foot)^,
        *[for (j in 0 to 4, let dx = (j - 2) * 0.022) ball(rig.add(centre, [x * dx, 0.0, -0.075 + 0.008 * abs(j - 2)])^, [0.011, 0.01, 0.016], PAW, foot)^],
        *[for (j in 0 to 4, let dx = (j - 2) * 0.022) claw(rig.add(centre, [x * dx, -0.002, -0.09 + 0.008 * abs(j - 2)])^, [x * dx * 0.15, -0.003, -0.012], foot)^]]
}
// a leafy twig held across both forepaws (it rides the left paw)
fn twig() any^ => [rig.tube([[-0.11, -0.232, 0.2], [0.0, -0.23, 0.205], [0.12, -0.232, 0.21]], [0.011, 0.012, 0.009], 6, (k, a) => TWIG, on(11)^)^,
    ball([0.145, -0.227, 0.217], [0.03, 0.006, 0.016], LEAF, 11)^, ball([0.13, -0.223, 0.235], [0.024, 0.005, 0.013], LEAF, 11)^]
pub fn buffer() any^ => rig.merge([rig.surface(40, 24, body_sample, (p) any^ => rig.weights(p, SPINE, 1.4)^, true)^, tail()^, *(face()^),
    *(foreleg(1.0, 10, 11)^), *(foreleg(-1.0, 12, 13)^), *(hindleg(1.0, 6, 7)^), *(hindleg(-1.0, 8, 9)^), *(twig()^)])^
pub fn geometry() any^ => rig.geometry("g-beaver", buffer()^)^

// ---- clips: joints turn in their parents' frames
fn xq(a: float) => [sin(a / 2.0), 0.0, 0.0, cos(a / 2.0)]
let STEPS = [for (i in 0 to 24) i / 24.0]
fn joint_track(name, period, f) any^ => s.track(name ++ ".quaternion", [for (k in STEPS) k * period], [for (k in STEPS, c in f(k * u.TAU)^) c], 'quaternion')^
// both forepaws reach for points (skeleton space) beside each other: under the chin, or at the mouth
fn paws_at(x, paw) any^ {
    let upper = if (x > 0.0) 10 else 12
    let elbow = side([0.13, -0.15, rig.lerp3(SHOULDER, paw, 0.5)^[2]], x)
    return rig.chain(rig.IDENTITY, side(SHOULDER, x), elbow, side(paw, x), rest_dir(upper)^, rest_dir(upper + 1)^)^
}
fn under_chin(x, a) any^ => paws_at(x, [0.05, -0.15, 0.25 + 0.005 * sin(a)])^
fn at_mouth(x, a) any^ => paws_at(x, [0.045, -0.085 + 0.006 * sin(a * 8.0), 0.3])^
pub let SWIM_PERIOD = 0.9
pub let REST_PERIOD = 1.6
pub let SLAP_TIME = 1.0
// the slap: the tail lifts high, cracks down on the water at SLAP_AT, and settles
pub let SLAP_AT = 0.42
let TURN = 6.283185307179586
fn slap(k: float) float^ => if (k < 0.32) 0.95 * (k / 0.32) else if (k < 0.42) 0.95 - 1.2 * (k - 0.32) / 0.1 else -0.25 + 0.25 * min(1.0, (k - 0.42) / 0.3)
pub fn clips() any^ => [
    s.clip("beaver-swim", [
        joint_track("bv-hip", SWIM_PERIOD, (a: float) => xq(0.04 * sin(a)))^,
        joint_track("bv-tail1", SWIM_PERIOD, (a: float) => xq(0.1 * sin(a - 1.0)))^,
        joint_track("bv-tail2", SWIM_PERIOD, (a: float) => xq(0.14 * sin(a - 1.8)))^,
        joint_track("bv-head", SWIM_PERIOD, (a: float) => xq(-0.12 - 0.03 * sin(a + 0.4)))^,
        joint_track("bv-hl-l", SWIM_PERIOD, (a: float) => xq(0.25 + 0.7 * sin(a)))^,
        joint_track("bv-hl-r", SWIM_PERIOD, (a: float) => xq(0.25 + 0.7 * sin(a + math.pi)))^,
        joint_track("bv-hf-l", SWIM_PERIOD, (a: float) => xq(0.3 * sin(a - 0.9)))^,
        joint_track("bv-hf-r", SWIM_PERIOD, (a: float) => xq(0.3 * sin(a + math.pi - 0.9)))^,
        joint_track("bv-fl-l", SWIM_PERIOD, (a: float) any^ => under_chin(1.0, a)^[0])^,
        joint_track("bv-fw-l", SWIM_PERIOD, (a: float) any^ => under_chin(1.0, a)^[1])^,
        joint_track("bv-fl-r", SWIM_PERIOD, (a: float) any^ => under_chin(-1.0, a)^[0])^,
        joint_track("bv-fw-r", SWIM_PERIOD, (a: float) any^ => under_chin(-1.0, a)^[1])^], {duration: SWIM_PERIOD, autoplay: true}),
    // gnawing: the twig at the mouth and quick little nods
    s.clip("beaver-rest", [
        joint_track("bv-neck", REST_PERIOD, (a: float) => xq(-0.12))^,
        joint_track("bv-head", REST_PERIOD, (a: float) => xq(0.12 + 0.06 * sin(a * 8.0)))^,
        joint_track("bv-tail1", REST_PERIOD, (a: float) => xq(-0.05 + 0.04 * sin(a)))^,
        joint_track("bv-hl-l", REST_PERIOD, (a: float) => xq(0.2 + 0.2 * sin(a)))^,
        joint_track("bv-hl-r", REST_PERIOD, (a: float) => xq(0.2 + 0.2 * sin(a + 2.0)))^,
        joint_track("bv-fl-l", REST_PERIOD, (a: float) any^ => at_mouth(1.0, a)^[0])^,
        joint_track("bv-fw-l", REST_PERIOD, (a: float) any^ => at_mouth(1.0, a)^[1])^,
        joint_track("bv-fl-r", REST_PERIOD, (a: float) any^ => at_mouth(-1.0, a)^[0])^,
        joint_track("bv-fw-r", REST_PERIOD, (a: float) any^ => at_mouth(-1.0, a)^[1])^], {duration: REST_PERIOD, autoplay: false}),
    s.clip("beaver-slap", [
        joint_track("bv-tail1", SLAP_TIME, (a: float) any^ => xq(slap(a / TURN)^))^,
        joint_track("bv-tail2", SLAP_TIME, (a: float) any^ => xq(0.5 * slap(max(0.0, a / TURN - 0.05))^))^,
        joint_track("bv-hip", SLAP_TIME, (a: float) any^ => xq(-0.12 * slap(a / TURN)^))^,
        joint_track("bv-head", SLAP_TIME, (a: float) => xq(-0.15))^], {duration: SLAP_TIME, autoplay: false})]
pub fn body(x, y, z, heading) any^ => physics.body("beaver", [
    physics.collider("beaver-shape", 'sphere', {radius: 0.05, groups: {membership: 0, filter: 0}}),
    skeleton()^,
    s.mesh(geometry()^, "m-beaver", {id: "beaver-mesh", skeleton: "beaver-rig", 'cast-shadow': true, bounds: [-0.6, -0.6, -1.2, 0.6, 0.6, 0.8]})],
    {type: 'kinematic-position', position: [x, y, z], quaternion: u.quat(heading)})
pub fn materials() any^ => [s.material('lambert', {id: "m-beaver", color: "#ffffff"})]
