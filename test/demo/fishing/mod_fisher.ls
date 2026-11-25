// The fisher: a seated adult as one skinned mesh in boat space (+Z the bow). A canvas bucket hat, a
// bearded face with nose, ears, eyes and brows, a navy knit sweater under a waxed olive vest, khaki
// trousers and boots; hands closed in a grip. The skeleton is pelvis, spine, chest, neck and head,
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
let SWEATER = w.col("#2f3d58")
let SWEATER_RIB = w.col("#3a4a68")
let VEST = w.col("#5c683b")
let VEST_DARK = w.col("#46512c")
let ZIP = w.col("#2b2a24")
let BELT = w.col("#3b2b1f")
let TROUSER = w.col("#8c7d58")
let TROUSER_SHADE = w.col("#76694a")
let BOOT = w.col("#3d2d22")
let SOLE = w.col("#24201c")
let HAT = w.col("#b7a477")
let HAT_BAND = w.col("#5a4932")

fn bind(indices, soft) any^ => (p) any^ => rig.weights(p, segments(indices), soft)^
fn ball(c, r, rgb, b) any^ => rig.ellipsoid(c, r, rig.IDENTITY, rig.flat(rgb)^, b)^
fn on(i) any^ => rig.rigid(i)^

// ---- torso: a loft from the hips to the base of the neck; the vest covers the front and back
let TORSO_KEYS = [[0.17, 0.155, 0.118], [0.26, 0.166, 0.122], [0.36, 0.152, 0.112], [0.48, 0.172, 0.12], [0.58, 0.192, 0.124],
    [0.65, 0.2, 0.116], [0.7, 0.17, 0.1], [0.735, 0.08, 0.07]]
fn torso_at(y) any^ => (let n = len(TORSO_KEYS), let i = min(n - 2, max(0, len([for (k in TORSO_KEYS where k[0] <= y) k]) - 1)),
    let a = TORSO_KEYS[i], let b = TORSO_KEYS[i + 1], let k = max(0.0, min(1.0, (y - a[0]) / (b[0] - a[0]))), let e = k * k * (3.0 - 2.0 * k),
    [a[1] + (b[1] - a[1]) * e, a[2] + (b[2] - a[2]) * e])
fn cloth(y, a) any^ => (let x = cos(a), let z = sin(a),
    if (y < 0.3) (if (y > 0.27) BELT else TROUSER)
    else if (abs(x) > 0.78 and y > 0.42) SWEATER
    else if (z > 0.0 and abs(x) < 0.035) ZIP
    else if (z > 0.3 and y > 0.46 and y < 0.56 and abs(x) > 0.25 and abs(x) < 0.6) VEST_DARK else VEST)
fn torso_sample(tt, around) any^ => (let y = 0.17 + 0.565 * tt, let a = around * u.TAU, let r = torso_at(y)^,
    // a little belly and chest forward, the back flatter
    let z = sin(a) * r[1] * (if (sin(a) > 0.0) 1.0 + 0.08 * sin(math.pi * tt) else 0.9),
    {p: [cos(a) * r[0], y, -0.2 + z], c: cloth(y, a)^})
fn torso() any^ => rig.surface(22, 24, torso_sample, bind([0, 1, 2], 1.5)^, true)^

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
        // the bucket hat: crown, band and a brim that turns down
        rig.tube([[0.0, 0.93, -0.178], [0.0, 1.02, -0.18]], [0.102, 0.092], 22, (k, a) => HAT, h)^,
        rig.tube([[0.0, 0.935, -0.178], [0.0, 0.955, -0.178]], [0.104, 0.104], 22, (k, a) => HAT_BAND, h)^,
        rig.surface(3, 26, (tt, around) any^ => (let a = around * u.TAU, let r = 0.1 + 0.075 * tt,
            {p: [cos(a) * r, 0.94 - 0.035 * tt * tt, -0.178 + sin(a) * r], c: HAT}), h)^]
}
fn neck() any^ => [rig.tube([[0.0, 0.66, -0.2], [0.0, 0.83, -0.172]], [0.058, 0.052], 14, (k, a) => SKIN, bind([2, 3, 4], 1.2)^)^,
    rig.tube([[0.0, 0.705, -0.195], [0.0, 0.755, -0.19]], [0.078, 0.07], 16, (k, a) => SWEATER_RIB, bind([2, 3], 1.0)^)^]

// ---- arms: sleeve to the elbow, sleeve and ribbed cuff to the wrist, a gripping hand
fn arm(x, upper, fore, hand) any^ {
    let sh = if (x < 0.0) SHOULDER_R else mirror(SHOULDER_R)
    let el = if (x < 0.0) ELBOW_R else mirror(ELBOW_R)
    let wr = if (x < 0.0) WRIST_R else mirror(WRIST_R)
    let palm = rig.add(wr, [0.0, -0.05, 0.008])^
    return [rig.tube([[x * 0.13, 0.655, -0.2], sh, rig.lerp3(sh, el, 0.5)^, el], [0.062, 0.06, 0.055, 0.048], 14,
            (k, a) => SWEATER, bind([2, upper, fore], 1.3)^)^,
        rig.tube([el, rig.lerp3(el, wr, 0.75)^, rig.add(wr, [0.0, 0.025, 0.0])^], [0.047, 0.04, 0.042], 14,
            (k, a) => if (k > 0.7) SWEATER_RIB else SWEATER, bind([upper, fore, hand], 1.3)^)^,
        ball(palm, [0.03, 0.048, 0.042], SKIN, on(hand)^)^,
        // curled fingers across the front of the palm, and the thumb over them
        *[for (j in 0 to 3) ball(rig.add(palm, [-0.02 + 0.0135 * j, -0.03, 0.036])^, [0.0085, 0.022, 0.012], SKIN, on(hand)^)^],
        ball(rig.add(palm, [x * -0.022, -0.008, 0.035])^, [0.01, 0.024, 0.011], SKIN_SHADE, on(hand)^)^]
}
// ---- legs: thighs forward along the seat, shins to boots on the floorboards (they ride the pelvis)
fn legs() any^ => [for (x in [-1.0, 1.0]) *[
    rig.tube([[x * 0.085, 0.24, -0.24], [x * 0.1, 0.25, -0.02], [x * 0.11, 0.24, 0.2]], [0.085, 0.078, 0.062], 14, (k, a) => if (k > 0.5) TROUSER_SHADE else TROUSER, on(0)^)^,
    rig.tube([[x * 0.11, 0.25, 0.2], [x * 0.12, 0.17, 0.235], [x * 0.125, 0.09, 0.26]], [0.06, 0.052, 0.046], 12, (k, a) => TROUSER, on(0)^)^,
    ball([x * 0.125, 0.085, 0.3], [0.05, 0.045, 0.1], BOOT, on(0)^)^, ball([x * 0.125, 0.052, 0.305], [0.054, 0.012, 0.108], SOLE, on(0)^)^]]
pub fn buffer() any^ => rig.merge([torso()^, *(head()^), *(neck()^), *(arm(-1.0, 5, 6, 7)^), *(arm(1.0, 8, 9, 10)^), *(legs()^)])^
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
