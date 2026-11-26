// The sea otter: one smooth sculpted skin (mod_sculpt) with a round, broad head, puffy whisker pads, a
// blunt dark nose and small low ears, a thick neck flowing into a long chubby body, short forelegs with
// mitten paws, big webbed hind flippers and a flat tapering tail. Warm brown fur with a pale cream face,
// throat and belly. The bind pose faces +Z with the back up. The sim turns the whole body (it floats on
// its back, rolls over to dive and back again); the clips bend the spine, tail and limbs: floating with
// a clam on its chest, paddling on its back, the dive, swimming under water and surfacing.
import s: lambda.scene3d
import physics: lambda.physics
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig
import sc: .mod_sculpt

// ---- skeleton: name, joint, bone end, parent. Skin indices count bones in document order (depth
// first, children in list order), so the list runs in that order too.
fn side(p, x) => [p[0] * x, p[1], p[2]]
let SHOULDER = [0.072, -0.06, 0.17]
let ELBOW = [0.095, -0.135, 0.19]
let WRIST = [0.09, -0.19, 0.225]
let PAW_TIP = [0.088, -0.212, 0.29]
let HIP_JOINT = [0.07, -0.05, -0.2]
let KNEE = [0.1, -0.088, -0.245]
let ANKLE = [0.12, -0.082, -0.3]
let TOES = [0.148, -0.082, -0.42]
let CHEST_JOINT = [0.0, 0.0, 0.04]
pub let BONES = [["ot-root", [0.0, 0.0, -0.04], [0.0, 0.0, 0.04], -1], ["ot-chest", CHEST_JOINT, [0.0, 0.0, 0.16], 0],
    ["ot-neck", [0.0, 0.0, 0.16], [0.0, 0.006, 0.28], 1], ["ot-head", [0.0, 0.006, 0.28], [0.0, 0.016, 0.46], 2],
    ["ot-arm-l", SHOULDER, ELBOW, 1], ["ot-fore-l", ELBOW, WRIST, 4], ["ot-paw-l", WRIST, PAW_TIP, 5],
    ["ot-arm-r", side(SHOULDER, -1.0), side(ELBOW, -1.0), 1], ["ot-fore-r", side(ELBOW, -1.0), side(WRIST, -1.0), 7],
    ["ot-paw-r", side(WRIST, -1.0), side(PAW_TIP, -1.0), 8],
    ["ot-hips", [0.0, 0.0, -0.12], [0.0, 0.0, -0.27], 0], ["ot-tail1", [0.0, 0.0, -0.27], [0.0, 0.0, -0.39], 10],
    ["ot-tail2", [0.0, 0.0, -0.39], [0.0, 0.0, -0.51], 11], ["ot-tail3", [0.0, 0.0, -0.51], [0.0, 0.0, -0.63], 12],
    ["ot-thigh-l", HIP_JOINT, KNEE, 10], ["ot-shin-l", KNEE, ANKLE, 14], ["ot-foot-l", ANKLE, TOES, 15],
    ["ot-thigh-r", side(HIP_JOINT, -1.0), side(KNEE, -1.0), 10], ["ot-shin-r", side(KNEE, -1.0), side(ANKLE, -1.0), 17],
    ["ot-foot-r", side(ANKLE, -1.0), side(TOES, -1.0), 18]]
let ROOT = 0
let CHEST = 1
let NECK = 2
let HEAD = 3
let ARM = {l: 4, r: 7}
let HIPS = 10
let TAIL = [11, 12, 13]
let LEG = {l: 14, r: 17}
fn joint(i) => BONES[i][1]
fn rel(i) any^ => (let p = BONES[i][3], if (p < 0) joint(i) else rig.sub(joint(i), joint(p))^)
fn bone(i) any^ => <bone id: BONES[i][0], position: rel(i)^, *[for (k, b in BONES where b[3] == i) bone(k)^]>
pub fn skeleton() any^ => <skeleton id: "otter-rig", bone(0)^>
fn rest_dir(i) any^ => rig.unit(rig.sub(BONES[i][2], BONES[i][1])^)^

// ---- palette: warm brown fur, a pale cream face, throat and belly, darker paws and flippers
let BACK = w.col("#8a5534")
let FLANK = w.col("#a36c43")
let LIMB = w.col("#95603b")
let CREAM = w.col("#dcbf92")
let FACE = w.col("#e3cba3")
let MUZZLE = w.col("#eedbb6")
let CROWN = w.col("#a87650")
let PAW = w.col("#66402a")
let FOOT = w.col("#633d27")
let NOSE = w.col("#24170f")
let EAR = w.col("#8a5a38")
let EYE = w.col("#0c0806")
let SHINE = w.col("#ffffff")
let WHISKER = w.col("#f3e8d6")
let CLAM = w.col("#e0958f")
let CLAM_RIB = w.col("#f3c6bd")
// colour tags: fur (shaded from the body's own layout), limb fur, paw, flipper, nose, ear
let TAG_FUR = 0
let TAG_LIMB = 1
let TAG_PAW = 2
let TAG_FOOT = 3
let TAG_NOSE = 4
let TAG_EAR = 5

// ---- the sculpt: every part names its bone, colour tag and how softly it hands over to neighbours
fn part(q, b, tag, soft) => {*: q, bone: b, tag: tag, soft: soft}
let BODY = [1.0, 0.9, 1.0]
let TAIL_FLAT = [1.15, 0.52, 1.0]
fn torso() => [
    part(sc.cone([0.0, 0.008, -0.31], [0.0, 0.0, -0.17], 0.07, 0.112, 0.015, BODY), HIPS, TAG_FUR, 0.04),
    part(sc.cone([0.0, 0.0, -0.17], [0.0, -0.006, 0.0], 0.114, 0.128, 0.015, BODY), ROOT, TAG_FUR, 0.04),
    part(sc.cone([0.0, -0.006, 0.0], [0.0, 0.0, 0.15], 0.128, 0.112, 0.015, BODY), CHEST, TAG_FUR, 0.04),
    part(sc.cone([0.0, 0.0, 0.15], [0.0, 0.008, 0.27], 0.108, 0.095, 0.02, [1.05, 0.95, 1.0]), NECK, TAG_FUR, 0.035),
    part(sc.cone([0.0, 0.004, -0.27], [0.0, 0.0, -0.39], 0.058, 0.052, 0.04, TAIL_FLAT), TAIL[0], TAG_FUR, 0.03),
    part(sc.cone([0.0, 0.0, -0.39], [0.0, -0.004, -0.51], 0.052, 0.045, 0.01, TAIL_FLAT), TAIL[1], TAG_FUR, 0.03),
    part(sc.cone([0.0, -0.004, -0.51], [0.0, -0.008, -0.6], 0.045, 0.03, 0.01, TAIL_FLAT), TAIL[2], TAG_FUR, 0.03)]
// the head: a broad round skull, puffy cheeks, whisker pads either side of a short blunt muzzle, a
// small chin, the nose pad and low round ears
let HEAD_CENTRE = [0.0, 0.016, 0.338]
fn head() => [
    part(sc.ellipsoid(HEAD_CENTRE, [0.104, 0.092, 0.095], 0.05), HEAD, TAG_FUR, 0.03),
    *[for (x in [-1.0, 1.0]) part(sc.ellipsoid([x * 0.048, -0.022, 0.365], [0.056, 0.048, 0.056], 0.035), HEAD, TAG_FUR, 0.03)],
    part(sc.cone([0.0, 0.036, 0.372], [0.0, 0.02, 0.428], 0.044, 0.03, 0.03), HEAD, TAG_FUR, 0.03),
    *[for (x in [-1.0, 1.0]) part(sc.ellipsoid([x * 0.026, -0.01, 0.425], [0.033, 0.029, 0.03], 0.02), HEAD, TAG_FUR, 0.03)],
    part(sc.ellipsoid([0.0, -0.042, 0.405], [0.028, 0.02, 0.026], 0.02), HEAD, TAG_FUR, 0.03),
    part(sc.ellipsoid([0.0, 0.016, 0.447], [0.02, 0.013, 0.012], 0.008), HEAD, TAG_NOSE, 0.03),
    *[for (x in [-1.0, 1.0]) part(sc.ellipsoid([x * 0.09, 0.07, 0.3], [0.027, 0.035, 0.02], 0.01), HEAD, TAG_EAR, 0.03)]]
fn foreleg(x, arm) => [
    part(sc.cone(side(SHOULDER, x), side(ELBOW, x), 0.046, 0.036, 0.045), arm, TAG_LIMB, 0.02),
    part(sc.cone(side(ELBOW, x), side(WRIST, x), 0.035, 0.03, 0.015), arm + 1, TAG_LIMB, 0.02),
    part(sc.ellipsoid(side([0.088, -0.205, 0.255], x), [0.032, 0.02, 0.034], 0.02), arm + 2, TAG_PAW, 0.015)]
// a hind leg: a short haunch and shin, and a small flat flipper of five webbed toes, the outer one longest
fn hindleg(x, leg) => [
    part(sc.cone(side(HIP_JOINT, x), side(KNEE, x), 0.066, 0.048, 0.05), leg, TAG_LIMB, 0.025),
    part(sc.cone(side(KNEE, x), side(ANKLE, x), 0.046, 0.034, 0.02), leg + 1, TAG_LIMB, 0.02),
    part(sc.cone(side([0.12, -0.082, -0.312], x), side([0.14, -0.082, -0.385], x), 0.026, 0.04, 0.016, [1.0, 0.34, 1.0]), leg + 2, TAG_FOOT, 0.02),
    *[for (j in 0 to 4) part(sc.cone(side([0.122 + (j - 2) * 0.01, -0.082, -0.37], x),
        side([0.142 + (j - 2) * 0.017, -0.082, -0.422 + 0.005 * abs(j - 2) - (if (j == 4) 0.006 else 0.0)], x), 0.0095, 0.009, 0.012, [1.0, 0.7, 1.0]),
        leg + 2, TAG_FOOT, 0.012)]]
pub let PARTS = [*torso(), *head(), *foreleg(1.0, ARM.l), *foreleg(-1.0, ARM.r), *hindleg(1.0, LEG.l), *hindleg(-1.0, LEG.r)]

// ---- painting
fn smooth(x) => (let k = max(0.0, min(1.0, x)), k * k * (3.0 - 2.0 * k))
// the body's own fur: darker along the back, a cream belly and throat that fade toward the tail, a pale
// face with a light-brown crown, the palest muzzle, and a tail that darkens toward its tip
fn fur(p) any^ {
    let x = p[0], y = p[1], z = p[2]
    let base = rig.mix(FLANK, BACK, smooth(y / 0.1))^
    let belly = smooth((0.01 - y) / 0.07) * smooth((z + 0.3) / 0.12)
    let pale = smooth((z - 0.17) / 0.14)
    // a light-brown cap over the crown and the back of the head, leaving the face pale
    let crown = smooth(y / 0.06) * smooth((0.38 - z) / 0.07)
    let c1 = rig.mix(rig.mix(base, CREAM, belly)^, FACE, pale)^
    let c2 = rig.mix(rig.mix(c1, CROWN, crown * pale * 0.85)^, MUZZLE, smooth((z - 0.395) / 0.03))^
    let c3 = rig.mix(c2, BACK, smooth((-0.45 - z) / 0.15) * 0.6)^
    let grain = 0.94 + 0.1 * rig.hash(floor(x * 90.0) + floor(y * 90.0) * 7.0, floor(z * 90.0))^;
    [for (c in c3) min(1.0, c * grain)]
}
fn tag_colour(tag, p) any^ => if (tag == TAG_FUR) fur(p)^ else if (tag == TAG_LIMB) rig.mix(LIMB, fur(p)^, 0.25)^
    else if (tag == TAG_PAW) PAW else if (tag == TAG_FOOT) FOOT else if (tag == TAG_NOSE) NOSE else EAR
// a bone and the bones it joins: its parent and children
let JOINED = [for (i, b in BONES) [i, *(if (b[3] >= 0) [b[3]] else []), *[for (k, c in BONES where c[3] == i) k]]]
// colour: the parts at the vertex, each weighed by how close it is to the nearest (crisp but smooth
// edges). Bones: the nearest part's bone, blended with the bones it joins by each part's own softness
// (strongest part per bone, so a five-toed flipper pulls no harder than one cone), four strongest kept.
// Only joined bones blend, so a flipper never drags the tail beside it, nor a paw the cheek.
fn paint(p, n, near) any^ {
    let dmin = min([for (q in near) q[1]])
    let shades = [for (q in near, let wgt = exp(0.0 - (q[1] - dmin) / 0.012) where wgt > 0.01) [PARTS[q[0]].tag, wgt]]
    let tags = unique([for (t in shades) t[0]])
    let tag_weights = [for (t in tags) max([for (x in shades where x[0] == t) x[1]])]
    let total = sum(tag_weights)
    let colours = [for (i, t in tags) [for (c in tag_colour(t, p)^) c * tag_weights[i] / total]]
    let c = [for (k in 0 to 2) sum([for (cc in colours) cc[k]])]
    let lead = PARTS[[for (q in near where q[1] == dmin) q[0]][0]].bone
    let pulls = [for (q in near, let b = PARTS[q[0]].bone, let wgt = exp(0.0 - (q[1] - dmin) / PARTS[q[0]].soft) where wgt > 0.002 and (contains(JOINED[lead], b) ^ { false })) [b, wgt]]
    let bones = unique([for (b in pulls) b[0]])
    let ranked = take(sort([for (b in bones) [b, max([for (x in pulls where x[0] == b) x[1]])]], (x) => 0.0 - x[1]), 4)
    let sum4 = sum([for (r in ranked) r[1]])
    let ids = [for (k in 0 to 3) if (k < len(ranked)) ranked[k][0] else ranked[0][0]]
    let ws = [for (k in 0 to 3) if (k < len(ranked)) ranked[k][1] / sum4 else 0.0];
    {p: p, n: n, c: c, b: [*ids, *ws]}
}

// ---- face details on top of the sculpt: glossy eyes with a catch-light, and fine pale whiskers
fn on(i) any^ => rig.rigid(i)^
fn ball(c, r, rgb, i) any^ => rig.ellipsoid(c, r, rig.IDENTITY, rig.flat(rgb)^, on(i)^)^
fn eye(x) any^ {
    let dir = rig.unit([x * 0.47, 0.36, 0.8])^
    let c = rig.sub(sc.surface_point(PARTS, HEAD_CENTRE, dir, 0.2)^, rig.scale(dir, 0.006)^)^
    return [ball(c, [0.0145, 0.0145, 0.013], EYE, HEAD)^, ball(rig.add(c, [x * 0.002, 0.0085, 0.0095])^, [0.0038, 0.0038, 0.003], SHINE, HEAD)^]
}
// the nose: a glossy dark pad, wider than tall, with a soft highlight
fn nose() any^ => (let at = sc.surface_point(PARTS, [0.0, 0.01, 0.4], rig.unit([0.0, 0.16, 1.0])^, 0.1)^,
    [rig.ellipsoid(rig.add(at, [0.0, 0.0, -0.006])^, [0.027, 0.0165, 0.015], [0.0, 0.0, 0.0, 1.0], (d) any^ => rig.mix(NOSE, w.col("#4a352a"), d[1] * 0.6)^, on(HEAD)^)^,
     ball(rig.add(at, [0.008, 0.009, 0.004])^, [0.006, 0.003, 0.003], w.col("#b9a89c"), HEAD)^])
// the mouth: a short line down from the nose that parts under the whisker pads
fn mouth(x) any^ => (let from = [0.0, -0.004, 0.405],
    let points = [for (q in [[0.0, 0.0, 0.452], [0.0, -0.014, 0.449], [x * 0.012, -0.023, 0.443], [x * 0.025, -0.023, 0.432], [x * 0.034, -0.017, 0.42]])
        sc.surface_point(PARTS, from, rig.unit(rig.sub(q, from)^)^, 0.1)^],
    rig.tube(points, [0.0018, 0.0018, 0.0017, 0.0014, 0.001], 4, (k, a) => NOSE, on(HEAD)^)^)
fn eyes() any^ => [for (x in [-1.0, 1.0], q in eye(x)^) q]
fn whiskers() any^ => [for (x in [-1.0, 1.0], j in 0 to 3, let dir = rig.unit([x * 0.8, -0.05 - 0.07 * j, 0.6])^,
    let at = sc.surface_point(PARTS, [x * 0.02, -0.01, 0.41], dir, 0.1)^)
    rig.tube([at, rig.add(at, [x * 0.04, -0.005 - 0.007 * j, 0.0])^, rig.add(at, [x * (0.075 + 0.007 * j), -0.016 - 0.012 * j, -0.03])^],
        [0.0012, 0.0008, 0.0004], 4, (k, a) => WHISKER, on(HEAD)^)^]
// a ribbed clam held on the chest between the paws (riding the chest, so it stays put as the paws work it)
let CLAM_AT = [0.0, -0.122, 0.11]
fn clam_sample(t, around) any^ => (let a = around * u.TAU, let fan = 1.0 + 0.08 * cos(a * 7.0) * t,
    {p: [CLAM_AT[0] + cos(a) * 0.032 * t * fan, CLAM_AT[1] - 0.013 * sqrt(max(0.0, 1.0 - t * t)) * (if (sin(a) > 0.0) 1.0 else 0.85),
         CLAM_AT[2] + sin(a) * 0.026 * t * fan], c: if (cos(a * 7.0) > 0.55) CLAM_RIB else CLAM})
fn clam() any^ => rig.surface(8, 28, clam_sample, on(CHEST)^, true)^

pub let CELL = 0.011
pub fn buffer() any^ => rig.merge([sc.sculpt(PARTS, CELL, paint)^, *(eyes()^), *(nose()^), mouth(1.0)^, mouth(-1.0)^, *(whiskers()^), clam()^])^
pub fn geometry() any^ => rig.geometry("g-otter", buffer()^)^

// ---- poses: a local rotation for every bone (the root stays put; the sim turns the whole body)
fn xq(a) => [sin(a / 2.0), 0.0, 0.0, cos(a / 2.0)]
fn yq(a) => [0.0, sin(a / 2.0), 0.0, cos(a / 2.0)]
fn zq(a) => [0.0, 0.0, sin(a / 2.0), cos(a / 2.0)]
fn turn(x, y, z) any^ => rig.qmul(rig.qmul(zq(z), yq(y))^, xq(x))^
let UPPER = rig.length(rig.sub(ELBOW, SHOULDER)^)^
let LOWER = rig.length(rig.sub(WRIST, ELBOW)^)^
// a foreleg reaching its wrist to a point in the chest's frame, elbow out and back, the paw pointing
// at a second point
fn arm(x, target) any^ {
    let upper = if (x > 0.0) ARM.l else ARM.r
    let root = side(SHOULDER, x)
    let elbow = rig.knee(root, target.wrist, UPPER, LOWER, [x, -0.1, -0.5])^
    let qs = rig.chain(rig.IDENTITY, root, elbow, target.wrist, rest_dir(upper)^, rest_dir(upper + 1)^)^
    let fore = rig.qmul(qs[0], qs[1])^
    return [qs[0], qs[1], rig.aim(rest_dir(upper + 2)^, rig.qrot(rig.qinv(fore)^, rig.sub(target.tip, target.wrist)^)^)^]
}
// a pose spec: {chest, neck, head, hips, tail: [3], legs: {l: [thigh, shin, foot], r}, wrists: {l, r}}
// resolved into the 20 local rotations in BONES order
fn resolve(p) any^ {
    let al = arm(1.0, p.wrists.l)^, ar = arm(-1.0, p.wrists.r)^;
    [rig.IDENTITY, p.chest, p.neck, p.head, *al, *ar, p.hips, *p.tail, *p.legs.l, *p.legs.r]
}
fn leg(x, splay, flex, knee, ankle) any^ => [turn(flex, 0.0, x * splay)^, xq(knee), xq(ankle)]
// paws holding the clam on the chest from either side, working it a little
fn hold(x, dz) => {wrist: [x * 0.06, -0.142, 0.088 + dz], tip: [x * 0.014, -0.15, 0.114 + dz]}
fn grip(a, work) => {l: hold(1.0, work * sin(a * 4.0)), r: hold(-1.0, work * sin(a * 4.0 + 3.14159))}
// floating on its back: curled a little toward the sky, the head bent right down to its tummy so its
// face looks up (and out at the camera) over the clam with small nibbling nods, hind flippers up out of
// the water, the tail lying flat and sculling slowly
fn float_pose(a) any^ => resolve({chest: xq(0.14), neck: xq(0.85 + 0.03 * sin(a)), head: turn(0.8 + 0.06 * max(0.0, sin(a * 4.0)), 0.1 * sin(a), 0.0)^,
    hips: xq(-0.14), tail: [turn(-0.06, 0.08 * sin(a), 0.0)^, turn(-0.08, 0.1 * sin(a - 0.8), 0.0)^, turn(-0.12, 0.12 * sin(a - 1.6), 0.0)^],
    legs: {l: leg(1.0, 0.1, -0.85, 0.25, -1.1 + 0.12 * sin(a))^, r: leg(-1.0, 0.14, -0.8, 0.2, -1.15 + 0.12 * sin(a + 2.0))^}, wrists: grip(a, 0.008)})^
// paddling on its back, face up: hind flippers kick in turn under the surface and the tail sculls side to side
fn paddle_pose(a) any^ => resolve({chest: xq(0.1), neck: xq(0.72), head: xq(0.58), hips: xq(-0.06 + 0.04 * sin(a)),
    tail: [turn(-0.03, 0.14 * sin(a - 1.0), 0.0)^, turn(-0.03, 0.18 * sin(a - 1.6), 0.0)^, turn(-0.03, 0.22 * sin(a - 2.2), 0.0)^],
    legs: {l: leg(1.0, 0.18, -0.3 + 0.6 * sin(a), 0.4 + 0.35 * sin(a - 0.9), -0.3 + 0.55 * sin(a - 1.6))^,
           r: leg(-1.0, 0.18, -0.3 + 0.6 * sin(a + 3.14159), 0.4 + 0.35 * sin(a + 2.24), -0.3 + 0.55 * sin(a + 1.54))^}, wrists: grip(a, 0.0)})^
// swimming under water belly-down: a wave runs from the chest through the hips to the tail tip, both
// hind flippers stroke together with it, the head stays level and the paws keep the clam tucked
fn swim_pose(a) any^ => resolve({chest: xq(0.06 * sin(a)), neck: xq(-0.05 - 0.05 * sin(a)), head: xq(-0.03 * sin(a + 0.4)), hips: xq(0.1 * sin(a - 1.0)),
    tail: [xq(0.14 * sin(a - 1.7)), xq(0.18 * sin(a - 2.4)), xq(0.2 * sin(a - 3.1))],
    legs: {l: leg(1.0, 0.12, 0.15 + 0.5 * sin(a - 1.3), -0.05 + 0.25 * sin(a - 1.9), 0.3 * sin(a - 2.5))^,
           r: leg(-1.0, 0.12, 0.15 + 0.5 * sin(a - 1.3), -0.05 + 0.25 * sin(a - 1.9), 0.3 * sin(a - 2.5))^}, wrists: grip(a, 0.0)})^
// the dive's key poses after rolling over: head ducked, then the back arched with both flippers up
// in the air, then the kick that drives it under
let TUCK = {chest: xq(0.25), neck: xq(0.25), head: xq(0.3), hips: xq(0.1), tail: [xq(0.1), xq(0.05), xq(0.0)], wrists: {l: hold(1.0, 0.0), r: hold(-1.0, 0.0)}}
fn duck_pose() any^ => resolve({*: TUCK, legs: {l: leg(1.0, 0.2, -0.2, 0.3, -0.2)^, r: leg(-1.0, 0.2, -0.2, 0.3, -0.2)^}})^
fn flippers_up_pose() any^ => resolve({*: TUCK, chest: xq(0.15), neck: xq(0.15), head: xq(0.2), hips: xq(0.55), tail: [xq(0.25), xq(0.2), xq(0.1)],
    legs: {l: leg(1.0, 0.4, 0.7, 0.0, 0.15)^, r: leg(-1.0, 0.4, 0.7, 0.0, 0.15)^}})^
fn kick_pose() any^ => resolve({*: TUCK, chest: xq(0.05), neck: xq(0.0), head: xq(0.05), hips: xq(0.1), tail: [xq(-0.2), xq(-0.25), xq(-0.2)],
    legs: {l: leg(1.0, 0.15, -0.35, 0.3, -0.5)^, r: leg(-1.0, 0.15, -0.35, 0.3, -0.5)^}})^
// surfacing: nose up toward the light with a last kick, before rolling onto its back
fn rise_pose() any^ => resolve({*: TUCK, chest: xq(-0.05), neck: xq(-0.3), head: xq(-0.15), hips: xq(-0.05), tail: [xq(-0.1), xq(-0.1), xq(-0.1)],
    legs: {l: leg(1.0, 0.15, -0.3, 0.3, -0.4)^, r: leg(-1.0, 0.15, -0.3, 0.3, -0.4)^}})^

// ---- blending poses
fn nlerp(a, b, k) any^ => (let hemi = if (rig.dot([a[0], a[1], a[2]], [b[0], b[1], b[2]])^ + a[3] * b[3] < 0.0) -1.0 else 1.0,
    let q = [for (i in 0 to 3) a[i] + (b[i] * hemi - a[i]) * k], let n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]),
    [for (c in q) c / n])
fn mix_pose(a, b, k) any^ => [for (i, q in a) nlerp(q, b[i], k)^]
// a pose along key poses at times [0..1]: eased from one key to the next
fn keyed(keys, times, k) any^ {
    let i = max(0, min(len(times) - 2, len([for (t in times where t <= k) t]) - 1))
    return mix_pose(keys[i], keys[i + 1], smooth((k - times[i]) / (times[i + 1] - times[i])))^
}
pub let FLOAT_PERIOD = 4.0
pub let PADDLE_PERIOD = 0.8
pub let SWIM_PERIOD = 0.9
pub let DIVE_TIME = 1.5
pub let SURFACE_TIME = 1.3
pub let TO_PADDLE_TIME = 0.25
pub let TO_FLOAT_TIME = 0.5
// how the whole body moves through the dive and the surfacing (k over each one's time): its height,
// roll (pi on its back) and pitch (positive tips the nose down); the sim applies these, the clips bend
// the body to match
pub let FLOAT_Y = -0.03
pub let SWIM_Y = -0.42
pub fn dive_root(k) => {roll: 3.14159 + 3.14159 * smooth(k / 0.32), pitch: 1.2 * smooth((k - 0.12) / 0.32) - 0.85 * smooth((k - 0.5) / 0.45),
    y: FLOAT_Y - 0.08 * smooth((k - 0.3) / 0.3) - (0.0 - SWIM_Y - 0.115) * smooth((k - 0.55) / 0.45)}
pub fn surface_root(k, from_y = SWIM_Y) => {roll: 3.14159 * smooth((k - 0.42) / 0.4), pitch: -0.55 * (1.0 - smooth(k / 0.45)),
    y: from_y + (FLOAT_Y - from_y) * smooth(k / 0.5)}
// the dive (k over DIVE_TIME): rolled belly-down over the first third, then tipped nose-down
fn dive_pose(k) any^ => keyed([float_pose(0.0)^, duck_pose()^, flippers_up_pose()^, kick_pose()^, swim_pose(0.0)^], [0.0, 0.3, 0.56, 0.78, 1.0], k)^
// surfacing (k over SURFACE_TIME): nose up, then the sim rolls it onto its back as the float pose returns
fn surface_pose(k) any^ => keyed([swim_pose(0.0)^, rise_pose()^, float_pose(0.0)^], [0.0, 0.4, 1.0], k)^

// ---- clips: every bone but the root, sampled at 30 fps; one-shots hold their last pose a second
let FPS = 30.0
let HOLD = 1.0
fn clip(id, f, duration, loop) any^ {
    let n = int(ceil(duration * FPS))^
    let times = [*[for (i in 0 to n) min(duration, i / FPS)], *(if (loop) [] else [duration + HOLD])]
    let poses = [for (t in times) f(min(t, duration) / duration)^];
    s.clip(id, [for (bi in 1 to len(BONES) - 1) s.track(BONES[bi][0] ++ ".quaternion", times, [for (p in poses, c in p[bi]) c], 'quaternion')^],
        {duration: if (loop) duration else duration + HOLD, autoplay: id == "otter-float"})
}
pub fn clips() any^ => [clip("otter-float", (k) any^ => float_pose(k * u.TAU)^, FLOAT_PERIOD, true)^,
    clip("otter-paddle", (k) any^ => paddle_pose(k * u.TAU)^, PADDLE_PERIOD, true)^,
    clip("otter-swim", (k) any^ => swim_pose(k * u.TAU)^, SWIM_PERIOD, true)^,
    clip("otter-dive", dive_pose, DIVE_TIME, false)^, clip("otter-surface", surface_pose, SURFACE_TIME, false)^,
    // short blends between floating still and paddling, so the flippers never jump
    clip("otter-to-paddle", (k) any^ => mix_pose(float_pose(0.0)^, paddle_pose(0.0)^, smooth(k))^, TO_PADDLE_TIME, false)^,
    clip("otter-to-float", (k) any^ => mix_pose(paddle_pose(0.0)^, float_pose(0.0)^, smooth(k))^, TO_FLOAT_TIME, false)^]
pub let CLIP_IDS = ["otter-float", "otter-paddle", "otter-swim", "otter-dive", "otter-surface", "otter-to-paddle", "otter-to-float"]

// a small adult (about 1 m nose to tail), modelled at 1.15 m and scaled down as a whole
pub let SIZE = 0.9
pub fn body(x, y, z, heading) any^ => physics.body("otter", [
    physics.collider("otter-shape", 'sphere', {radius: 0.05, groups: {membership: 0, filter: 0}}),
    s.group([skeleton()^,
        s.mesh(geometry()^, "m-otter", {id: "otter-mesh", skeleton: "otter-rig", 'cast-shadow': true, bounds: [-0.7, -0.7, -1.0, 0.7, 0.7, 0.8]})],
        {id: "otter-size", scale: [SIZE, SIZE, SIZE]})],
    {type: 'kinematic-position', position: [x, y, z], quaternion: u.quat(heading, 0.0, 3.14159)})
pub fn materials() any^ => [s.material('lambert', {id: "m-otter", color: "#ffffff"})]
