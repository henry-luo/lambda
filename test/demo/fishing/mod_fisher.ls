// The fisher: a seated adult as one skinned mesh in boat space (+Z the bow). A wide woven straw hat
// with a red band, a bearded face with nose, ears, eyes and brows, a cream striped shirt under blue
// denim bib overalls with brass buckles, and brown boots; hands closed in a grip. The skeleton is pelvis, spine, chest, neck and head,
// with shoulder, elbow and wrist bones per arm; the legs ride the pelvis. Poses name where the
// hands are; a two-bone IK solve turns that into shoulder and elbow turns, so the hands really hold
// the oar handles and the rod.
import s: lambda.scene3d
import shape: lambda.scene3d.shape
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig

// ---- skeleton (boat space joints; the right arm is on -X since the fisher faces the bow)
fn mirror(p) => [0.0 - p[0], p[1], p[2]]
let PELVIS = [0.0, 0.24, -0.2]
let SPINE = [0.0, 0.34, -0.2]
let CHEST = [0.0, 0.52, -0.2]
let NECK = [0.0, 0.7, -0.19]
let HEAD = [0.0, 0.78, -0.18]
pub let SHOULDER_R = [-0.2, 0.665, -0.2]
let ELBOW_R = [-0.225, 0.4, -0.2]
let WRIST_R = [-0.225, 0.158, -0.2]
let KNUCKLES_R = [-0.225, 0.07, -0.2]
pub let UPPER = 0.266
pub let FORE = 0.242
// name, joint, bone end (for weights), parent
let BONES = [["f-pelvis", PELVIS, SPINE, -1], ["f-spine", SPINE, CHEST, 0], ["f-chest", CHEST, NECK, 1], ["f-neck", NECK, HEAD, 2],
    ["f-head", HEAD, [0.0, 1.0, -0.17], 3],
    ["f-arm-r", SHOULDER_R, ELBOW_R, 2], ["f-fore-r", ELBOW_R, WRIST_R, 5], ["f-hand-r", WRIST_R, KNUCKLES_R, 6],
    ["f-arm-l", mirror(SHOULDER_R), mirror(ELBOW_R), 2], ["f-fore-l", mirror(ELBOW_R), mirror(WRIST_R), 8], ["f-hand-l", mirror(WRIST_R), mirror(KNUCKLES_R), 9]]
fn joint(i) => BONES[i][1]
fn rel(i) any^ => (let p = BONES[i][3], if (p < 0) joint(i) else rig.sub(joint(i), joint(p))^)
fn bone(i) any^ => <bone id: BONES[i][0], position: rel(i)^, *[for (k, b in BONES where b[3] == i) bone(k)^]>
pub fn skeleton() any^ => <skeleton id: "fisher-rig", bone(0)^>
fn segments(indices) => [for (i in indices) [i, BONES[i][1], BONES[i][2]]]
fn rest_dir(i) any^ => rig.unit(rig.sub(BONES[i][2], BONES[i][1])^)^

// ---- palette
let SKIN = w.col("#e2b08c")
let SKIN_SHADE = w.col("#c98f6c")
let LIP = w.col("#b9705f")
let BEARD = w.col("#5d4636")
let HAIR = w.col("#6b5747")
let BROW = w.col("#4a3628")
let EYE_WHITE = w.col("#efe9e0")
let IRIS = w.col("#3b4a52")
let SHIRT = w.col("#efe6d2")
let SHIRT_STRIPE = w.col("#c9d2da")
let DENIM = w.col("#4f73a8")
let DENIM_SHADE = w.col("#3e5e8e")
let BUCKLE = w.col("#d9b45a")
let BOOT = w.col("#6e4327")
let SOLE = w.col("#2e231c")
let STRAW = w.col("#e3c27e")
let STRAW_DARK = w.col("#c7a15c")
let HAT_BAND = w.col("#c8583a")

fn bind(indices, soft) any^ => (p) any^ => rig.weights(p, segments(indices), soft)^
fn ball(c, r, rgb, b) any^ => rig.ellipsoid(c, r, rig.IDENTITY, rig.flat(rgb)^, b)^
fn on(i) any^ => rig.rigid(i)^

// ---- torso: a loft from the hips to the base of the neck; the overalls' seat and bib, and their straps
// over the shoulders, on a striped shirt
let TORSO_KEYS = [[0.17, 0.155, 0.118], [0.26, 0.166, 0.122], [0.36, 0.152, 0.112], [0.48, 0.172, 0.12], [0.58, 0.192, 0.124],
    [0.65, 0.2, 0.116], [0.7, 0.17, 0.1], [0.735, 0.08, 0.07]]
fn torso_at(y) any^ => (let n = len(TORSO_KEYS), let i = min(n - 2, max(0, len([for (k in TORSO_KEYS where k[0] <= y) k]) - 1)),
    let a = TORSO_KEYS[i], let b = TORSO_KEYS[i + 1], let k = max(0.0, min(1.0, (y - a[0]) / (b[0] - a[0]))), let e = k * k * (3.0 - 2.0 * k),
    [a[1] + (b[1] - a[1]) * e, a[2] + (b[2] - a[2]) * e])
fn shirt(a) => if (sin(a * 26.0) > 0.82) SHIRT_STRIPE else SHIRT
fn cloth(y, a) any^ => (let x = cos(a), let z = sin(a),
    if (y < 0.42) (if (y > 0.4 and z < 0.0) DENIM_SHADE else DENIM)
    // the bib: a hemmed top edge and a chest pocket
    else if (z > 0.15 and abs(x) < 0.55 and y < 0.6) (if (y > 0.585 or (abs(x) < 0.2 and y > 0.48 and y < 0.55)) DENIM_SHADE else DENIM)
    else if (abs(abs(x) - 0.42) < 0.12) DENIM else shirt(a))
fn torso_sample(tt, around) any^ => (let y = 0.17 + 0.565 * tt, let a = around * u.TAU, let r = torso_at(y)^,
    // a little belly and chest forward, the back flatter
    let z = sin(a) * r[1] * (if (sin(a) > 0.0) 1.0 + 0.08 * sin(math.pi * tt) else 0.9),
    {p: [cos(a) * r[0], y, -0.2 + z], c: cloth(y, a)^})
fn torso() any^ => rig.surface(26, 40, torso_sample, bind([0, 1, 2], 1.5)^, true)^
// brass buckles where the straps meet the bib
fn buckles() any^ => [for (x in [-1.0, 1.0], let a = math.atan2(sqrt(1.0 - 0.42 * 0.42), x * 0.42), let at = torso_sample((0.59 - 0.17) / 0.565, a / u.TAU)^.p)
    ball(rig.add(at, [0.0, 0.0, 0.006])^, [0.017, 0.013, 0.007], BUCKLE, on(2)^)^]

// ---- head: skull, jaw, beard, nose, ears, eyes, brows, a roll collar and the hat
fn head() any^ {
    let h = on(4)^
    return [ball([0.0, 0.905, -0.178], [0.09, 0.105, 0.1], SKIN, h)^, ball([0.0, 0.85, -0.148], [0.072, 0.068, 0.082], SKIN, h)^,
        ball([0.0, 0.828, -0.142], [0.079, 0.06, 0.088], BEARD, h)^, ball([0.0, 0.858, -0.072], [0.03, 0.012, 0.012], BEARD, h)^,
        ball([0.0, 0.843, -0.073], [0.018, 0.006, 0.008], LIP, h)^,
        ball([0.0, 0.88, -0.072], [0.015, 0.024, 0.02], SKIN_SHADE, h)^,
        *[for (x in [-1.0, 1.0]) ball([x * 0.09, 0.885, -0.18], [0.013, 0.028, 0.02], SKIN_SHADE, h)^],
        *[for (x in [-1.0, 1.0]) ball([x * 0.033, 0.905, -0.088], [0.013, 0.008, 0.006], EYE_WHITE, h)^],
        *[for (x in [-1.0, 1.0]) ball([x * 0.033, 0.905, -0.082], [0.0055, 0.0055, 0.003], IRIS, h)^],
        *[for (x in [-1.0, 1.0]) ball([x * 0.034, 0.922, -0.088], [0.018, 0.0045, 0.006], BROW, h)^],
        ball([0.0, 0.89, -0.205], [0.092, 0.07, 0.085], HAIR, h)^,
        // the straw hat: a rounded crown with a red band, and a wide woven brim that droops a little
        rig.tube([[0.0, 0.93, -0.178], [0.0, 1.0, -0.18], [0.0, 1.035, -0.18]], [0.099, 0.094, 0.068], 24, (k, a) => straw(k * 3.0, a), h)^,
        rig.tube([[0.0, 0.937, -0.178], [0.0, 0.963, -0.178]], [0.102, 0.1], 24, (k, a) => HAT_BAND, h)^,
        rig.surface(5, 36, (tt, around) any^ => (let a = around * u.TAU, let r = 0.098 + 0.17 * tt,
            {p: [cos(a) * r, 0.94 - 0.03 * tt * tt + 0.008 * sin(a * 2.0) * tt, -0.178 + sin(a) * r], c: straw(r * 40.0, a)}), h)^]
}
// woven straw: darker rings and spokes
fn straw(v, a) => if (sin(v * 3.0) > 0.55 or sin(a * 40.0) > 0.88) STRAW_DARK else STRAW
fn neck() any^ => [rig.tube([[0.0, 0.66, -0.2], [0.0, 0.83, -0.172]], [0.058, 0.052], 14, (k, a) => SKIN, bind([2, 3, 4], 1.2)^)^,
    rig.tube([[0.0, 0.705, -0.195], [0.0, 0.755, -0.19]], [0.078, 0.07], 16, (k, a) => SHIRT, bind([2, 3], 1.0)^)^]

let JOINT_BALL = shape.sphere(12, 18)
// ---- arms: striped shirt sleeves to the elbow, then to a turned-back cuff at the wrist, a gripping hand
fn arm(x, upper, fore, hand) any^ {
    let sh = if (x < 0.0) SHOULDER_R else mirror(SHOULDER_R)
    let el = if (x < 0.0) ELBOW_R else mirror(ELBOW_R)
    let wr = if (x < 0.0) WRIST_R else mirror(WRIST_R)
    let palm = rig.add(wr, [0.0, -0.05, 0.008])^
    // round caps centred on the shoulder and elbow joints turn about their own centres with the arm, so
    // the sleeve stays joined to the body and to the forearm at any angle; the sleeve starts at the
    // shoulder joint and rides the arm alone (a sleeve begun inside the chest twisted into a pinch)
    return [rig.ellipsoid(sh, [0.067, 0.063, 0.069], rig.IDENTITY, rig.flat(SHIRT)^, on(upper)^, JOINT_BALL)^,
        rig.ellipsoid(el, [0.049, 0.049, 0.049], rig.IDENTITY, rig.flat(SHIRT)^, on(fore)^, JOINT_BALL)^,
        rig.tube([sh, rig.lerp3(sh, el, 0.5)^, el], [0.064, 0.057, 0.048], 14, (k, a) => shirt(a * 0.6), bind([upper, fore], 1.3)^)^,
        rig.tube([el, rig.lerp3(el, wr, 0.75)^, rig.add(wr, [0.0, 0.025, 0.0])^], [0.047, 0.04, 0.044], 14,
            (k, a) => if (k > 0.7) SHIRT else shirt(a * 0.6), bind([upper, fore, hand], 1.3)^)^,
        ball(palm, [0.03, 0.048, 0.042], SKIN, on(hand)^)^,
        // curled fingers across the front of the palm, and the thumb over them
        *[for (j in 0 to 3) ball(rig.add(palm, [-0.02 + 0.0135 * j, -0.03, 0.036])^, [0.0085, 0.022, 0.012], SKIN, on(hand)^)^],
        ball(rig.add(palm, [x * -0.022, -0.008, 0.035])^, [0.01, 0.024, 0.011], SKIN_SHADE, on(hand)^)^]
}
// ---- legs: thighs forward along the seat, shins to boots on the floorboards (they ride the pelvis)
fn legs() any^ => [for (x in [-1.0, 1.0]) *[
    rig.tube([[x * 0.085, 0.24, -0.24], [x * 0.1, 0.25, -0.02], [x * 0.11, 0.24, 0.2]], [0.085, 0.078, 0.062], 14, (k, a) => if (k > 0.5) DENIM_SHADE else DENIM, on(0)^)^,
    rig.tube([[x * 0.11, 0.25, 0.2], [x * 0.12, 0.17, 0.235], [x * 0.125, 0.09, 0.26]], [0.06, 0.052, 0.046], 12, (k, a) => DENIM, on(0)^)^,
    ball([x * 0.125, 0.085, 0.3], [0.05, 0.045, 0.1], BOOT, on(0)^)^, ball([x * 0.125, 0.052, 0.305], [0.054, 0.012, 0.108], SOLE, on(0)^)^]]
pub fn buffer() any^ => rig.merge([torso()^, *(buckles()^), *(head()^), *(neck()^), *(arm(-1.0, 5, 6, 7)^), *(arm(1.0, 8, 9, 10)^), *(legs()^)])^
pub fn geometry() any^ => rig.geometry("g-fisher", buffer()^)^

// ---- posing: torso and head turns, and hands placed by IK
fn quat3(a) any^ => u.quat(a[0], a[1], a[2])^
// shoulder, the chest's orientation, and arm turns reaching for `target` (boat space)
fn reach(torso_q, side, target) any^ {
    let upper = if (side < 0.0) 5 else 8
    let shoulder0 = joint(upper)
    let shoulder = rig.add(SPINE, rig.qrot(torso_q, rig.sub(shoulder0, SPINE)^)^)^
    // elbows hang out to the side and a little back
    let pole = rig.add(shoulder, rig.qrot(torso_q, [side * 0.55, -0.75, -0.35])^)^
    let elbow = rig.knee(shoulder, target, UPPER, FORE, pole)^
    return rig.chain(torso_q, shoulder, elbow, target, rest_dir(upper)^, rest_dir(upper + 1)^)^
}
// tracks for one action: `poses` carry torso/head angles and scale; `hands` the resolved {r, l} targets
pub fn tracks(poses, hands, times, track) any^ {
    let torso_q = [for (p in poses) quat3(p.torso)^]
    let right = [for (i, p in poses) reach(torso_q[i], -1.0, hands[i].r)^]
    let left = [for (i, p in poses) reach(torso_q[i], 1.0, hands[i].l)^]
    let half = (a) => [a[0] * 0.5, a[1] * 0.5, a[2] * 0.5];
    [track("f-spine.quaternion", times, [for (q in torso_q, c in q) c], 'quaternion')^,
     track("f-spine.scale", times, [for (p in poses, c in [2.0 - p.scale, p.scale, 2.0 - p.scale]) c], 'vector')^,
     track("f-neck.quaternion", times, [for (p in poses, c in quat3(half(p.head))^) c], 'quaternion')^,
     track("f-head.quaternion", times, [for (p in poses, c in quat3(half(p.head))^) c], 'quaternion')^,
     track("f-arm-r.quaternion", times, [for (q in right, c in q[0]) c], 'quaternion')^,
     track("f-fore-r.quaternion", times, [for (q in right, c in q[1]) c], 'quaternion')^,
     track("f-arm-l.quaternion", times, [for (q in left, c in q[0]) c], 'quaternion')^,
     track("f-fore-l.quaternion", times, [for (q in left, c in q[1]) c], 'quaternion')^]
}
pub let KNEE_L = [0.12, 0.33, 0.14]
pub fn mesh() any^ => s.mesh(geometry()^, "m-boat", {id: "fisher", skeleton: "fisher-rig", 'cast-shadow': true, 'receive-shadow': true,
    bounds: [-1.2, -0.2, -1.2, 1.2, 1.6, 1.2]})
