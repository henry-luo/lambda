// Fish: a smooth lofted body on a five-bone spine with a blunt head, darker mouth and gill cover, a
// lateral line, scale shimmer, eyes with iris, pupil and highlight, and fins with rays, painted by
// species (koi, trout, perch, bluegill, carp, minnows, blue tang). A looping clip of travelling body
// waves swims it (amplitude grows toward the tail, as in carangiform swimmers). The spotted ray is
// a flat disc with a whip tail whose wings flap on their own bones. A fish faces +Z with its nose at
// +L/2; its kinematic body carries the skeleton, so the host moves it with one pose target.
import s: lambda.scene3d
import physics: lambda.physics
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig

// beat: tail-beat (or wing-beat) period in seconds; depth: the band below the surface it cruises;
// speed: cruise m/s; school: swims in a school; bites: takes the bait; bottom: keeps to the floor
pub let SPECIES = {
    koi: {name: "Koi", length: [0.58, 0.85], width: 0.115, height: 0.14, tail: 0.3, beat: 1.0, depth: [0.2, 0.6], speed: 0.45, bites: true},
    trout: {name: "Rainbow trout", length: [0.5, 0.7], width: 0.08, height: 0.12, tail: 0.2, beat: 0.75, depth: [0.3, 0.8], speed: 0.7, bites: true},
    perch: {name: "Perch", length: [0.32, 0.45], width: 0.085, height: 0.18, tail: 0.2, beat: 0.6, depth: [0.3, 0.75], speed: 0.55, bites: true},
    bluegill: {name: "Bluegill", length: [0.23, 0.32], width: 0.075, height: 0.24, tail: 0.18, beat: 0.5, depth: [0.25, 0.6], speed: 0.5, bites: true},
    carp: {name: "Common carp", length: [0.65, 0.9], width: 0.125, height: 0.17, tail: 0.24, beat: 1.15, depth: [0.35, 0.9], speed: 0.38, bites: true},
    minnow: {name: "Minnow", length: [0.14, 0.19], width: 0.075, height: 0.1, tail: 0.2, beat: 0.32, depth: [0.15, 0.5], speed: 0.85, school: true},
    tang: {name: "Blue tang", length: [0.18, 0.25], width: 0.055, height: 0.3, tail: 0.22, beat: 0.42, depth: [0.2, 0.6], speed: 0.7, school: true},
    ray: {name: "Spotted ray", length: [0.85, 1.15], width: 0.5, height: 0.06, tail: 0.0, beat: 2.2, depth: [0.6, 2.0], speed: 0.3, bottom: true}}
let COUNTS = [["koi", 8], ["trout", 4], ["perch", 4], ["bluegill", 4], ["carp", 3], ["minnow", 10], ["tang", 8], ["ray", 3]]
// one record per fish: identity, species, size, colour variant and swim phase (positions live in the sim)
pub let ROSTER = [for (k, entry in COUNTS, n in 0 to entry[1] - 1, let i = k * 10 + n, let sp = SPECIES[entry[0]])
    {id: "fish-" ++ string(i), kind: entry[0], n: i, look: n % 4, phase: (w.roll(2000 + i, 0) ^ { 0.0 }) * u.TAU,
     length: sp.length[0] + (sp.length[1] - sp.length[0]) * (w.roll(2000 + i, 1) ^ { 0.5 })}]
pub fn schools(kind) => SPECIES[kind].school == true
pub fn bites(kind) => SPECIES[kind].bites == true
pub fn bottom(kind) => SPECIES[kind].bottom == true

// ---- shape
let BODY = 0.78                        // body share of the length; the tail fin makes up the rest
pub let BONE_U = [0.12, 0.32, 0.52, 0.72, 0.92]
fn profile(t) any^ => if (t >= 0.995) 0.14 else max(0.04, max(if (t > 0.85) 0.14 else 0.0, math.pow(max(0.0, sin(math.pi * math.pow(max(t, 0.002), 0.6))), 0.78)))
fn body_z(f, t) any^ => f.length / 2.0 - t * BODY * f.length
// spine blend: vertex at z takes the two nearest bones
fn spine(f, z) any^ => (let first = body_z(f, BONE_U[0])^, let gap = 0.2 * BODY * f.length, let q = u.clamp((first - z) / gap, 0.0, 4.0),
    let k = min(3, int(floor(q))^), [k, k + 1, 1.0 - (q - k), q - k])

// ---- paint
let C = {white: w.col("#f3efe6"), red: w.col("#e2552f"), gold: w.col("#f0b23e"), black: w.col("#2a2a2a"), orange: w.col("#f08a3a"),
    silver: w.col("#c9cdc6"), pink: w.col("#e48a8a"), belly: w.col("#f1ece0"), perch: w.col("#b9b24e"),
    perch_back: w.col("#8a9440"), gill_back: w.col("#5d8a86"), gill_side: w.col("#6f8e7c"), gill_belly: w.col("#e8a63c"),
    minnow_back: w.col("#7fb0bd"), minnow_side: w.col("#d6dddc"), dark: w.col("#1e2224"), fin_orange: w.col("#e2723c"),
    trout_back: w.col("#8a9a62"), carp_back: w.col("#6e5a2c"), carp_side: w.col("#c79f4a"), carp_belly: w.col("#eadcb0"),
    tang: w.col("#2f5bd0"), tang_dark: w.col("#162a66"), tang_tail: w.col("#f2c532"), iris: w.col("#e8d27a"), shine: w.col("#ffffff")}
fn mix(a, b, k) any^ => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * max(0.0, min(1.0, k))]
fn shade(c, k) any^ => [for (x in c) min(1.0, x * k)]
fn hash(a, b) any^ => (let v = sin(a * 127.1 + b * 311.7) * 43758.5453, v - floor(v))
// base colour at body position t (0 nose .. 1 tail) and around-angle sine (1 = back, -1 = belly)
fn base_skin(f, t, up) any^ {
    let back = u.clamp(up * 0.5 + 0.5, 0.0, 1.0)
    let blot = hash(floor(t * 7.0) + f.n * 3.0, floor(up * 2.5 + 2.0))^
    if (f.kind == "koi") { return (
        if (f.look == 1) mix(C.gold, C.orange, back * 0.5)^
        else if (f.look == 3) mix(C.orange, C.gold, 1.0 - back)^
        else if (up > -0.35 and blot > 0.45) (if (f.look == 2 and blot > 0.85) C.black else C.red)
        else C.white) }
    if (f.kind == "trout") { return (let band = 1.0 - u.clamp(abs(up + 0.05) / 0.22, 0.0, 1.0),
        let speck = if (up > 0.0 and hash(t * 40.0 + f.n, up * 13.0)^ > 0.86) 1.0 else 0.0,
        mix(mix(mix(mix(C.belly, C.silver, up + 0.6)^, C.trout_back, up * 1.6 - 0.2)^, C.pink, band * 0.75)^, C.dark, speck)^) }
    if (f.kind == "perch") { return (let bar = if (up > -0.4 and sin(t * math.pi * 11.0) > 0.45 and t > 0.15 and t < 0.85) 0.6 else 0.0,
        mix(mix(mix(C.belly, C.perch, up + 0.8)^, C.perch_back, up)^, C.dark, bar)^) }
    if (f.kind == "bluegill") { return (let ear = if (t > 0.17 and t < 0.27 and abs(up) < 0.25) 1.0 else 0.0,
        mix(mix(mix(C.gill_belly, C.gill_side, up + 0.5)^, C.gill_back, up)^, C.dark, ear * 0.85)^) }
    if (f.kind == "carp") { return (let net = if (sin(t * 70.0 + up * 9.0) > 0.82 or sin(t * 70.0 - up * 9.0) > 0.82) 0.82 else 1.0,
        shade(mix(mix(C.carp_belly, C.carp_side, up + 0.7)^, C.carp_back, up * 1.4 - 0.2)^, net)^) }
    if (f.kind == "tang") { return (let mark = if (up > 0.15 and t > 0.25 and t < 0.75 and abs(sin(t * 8.0 - 1.0)) > 0.35) 1.0 else 0.0,
        if (t > 0.88) C.tang_tail else mix(C.tang, C.tang_dark, mark * 0.9)^) }
    return (let line = 1.0 - u.clamp(abs(up - 0.05) / 0.08, 0.0, 1.0), mix(mix(C.minnow_side, C.minnow_back, up * 1.5)^, C.dark, line * 0.55)^)
}
// details over the base: a darker mouth, the gill cover's edge, the lateral line, and scales that catch the light
fn skin(f, t, up, around) any^ {
    let c = base_skin(f, t, up)^
    let mouth = if (t < 0.035 and up < 0.15) 0.45 else 1.0
    let gill = if (t > 0.19 and t < 0.215 and abs(up) < 0.7) 0.72 else 1.0
    let lateral = if (f.kind != "koi" and f.kind != "tang" and t > 0.24 and t < 0.92 and abs(up - 0.05) < 0.035) 0.8 else 1.0
    let sheen = if (up > 0.25 and up < 0.6) 1.08 else 1.0
    return shade(c, mouth * gill * lateral * sheen * (0.93 + 0.12 * hash(floor(t * 40.0) + f.n, floor(around * 9.0))^))^
}
fn fin_color(f) any^ => if (f.kind == "koi") (if (f.look == 1 or f.look == 3) C.gold else C.white)
    else if (f.kind == "perch") C.fin_orange else if (f.kind == "bluegill") C.gill_side else if (f.kind == "carp") C.carp_side
    else if (f.kind == "tang") C.tang_tail else if (f.kind == "trout") C.trout_back else C.gold

// ---- the body: a loft from the nose to the tail root
fn body_sample(f, sp) any^ => (tt, around) any^ => (let z = body_z(f, tt)^, let pr = profile(tt)^, let a = around * u.TAU,
    let half_w = sp.width * f.length * pr, let half_h = sp.height * f.length * pr, let sn = sin(a),
    {p: [cos(a) * half_w, sn * half_h * (if (sn < 0.0) 0.9 else 1.0), z], c: skin(f, tt, sn, around)^})
// ---- fins: fans and strips whose rays alternate with the webbing between them
fn fin_vertex(f, p, rgb) any^ => {p: p, n: [1.0, 0.0, 0.0], c: rgb, b: spine(f, p[2])^}
fn fan(f, root, edge, rgb) any^ => rig.part([fin_vertex(f, root, rgb)^, *[for (i, p in edge) fin_vertex(f, p, if (i % 2 == 0) shade(rgb, 0.78)^ else rgb)^]],
    [for (i in 1 to len(edge) - 1, q in [0, i, i + 1]) q])^
fn strip(f, bottoms, tops, rgb) any^ => (let n = len(bottoms), rig.part([*[for (p in bottoms) fin_vertex(f, p, shade(rgb, 0.9)^)^],
        *[for (i, p in tops) fin_vertex(f, p, if (i % 2 == 0) shade(rgb, 0.75)^ else rgb)^]],
    [for (i in 0 to n - 2, q in [i, i + 1, n + i, i + 1, n + i + 1, n + i]) q])^)
fn fins(f, sp) any^ {
    let L = f.length
    let root = body_z(f, 1.0)^, span = sp.tail * L
    let lobe = if (f.kind == "koi") 0.72 else if (f.kind == "trout" or f.kind == "carp") 0.55 else 0.62
    let tip_h = sp.height * L * (if (f.kind == "koi") 1.2 else 0.95)
    let fc = fin_color(f)^
    // the tail: a forked trailing edge from the upper tip through the notch to the lower tip
    let edge = [for (j in 0 to 8, let k = j / 8.0, let y = tip_h * (1.0 - 2.0 * k), let back = span * (lobe + (1.0 - lobe) * abs(1.0 - 2.0 * k)))
        [0.0, y, root - back]]
    let top_at = (t) any^ => sp.height * L * profile(t)^
    let dorsal_t = [0.3, 0.38, 0.46, 0.54, 0.62]
    let dorsal_rise = [0.7, 0.62, 0.48, 0.36, 0.22]
    let pz = body_z(f, 0.24)^, pw = sp.width * L * profile(0.24)^, ph = sp.height * L * profile(0.24)^
    let anal_t = [0.66, 0.71, 0.76]
    return [fan(f, [0.0, 0.0, root + 0.02 * L], edge, fc)^,
        strip(f, [for (t in dorsal_t) [0.0, top_at(t)^ * 0.95, body_z(f, t)^]], [for (i, t in dorsal_t) [0.0, top_at(t)^ + sp.height * L * dorsal_rise[i], body_z(f, t)^ - 0.04 * L]], fc)^,
        strip(f, [for (t in anal_t) [0.0, 0.0 - top_at(t)^ * 0.85, body_z(f, t)^]], [for (i, t in anal_t) [0.0, 0.0 - top_at(t)^ - sp.height * L * (0.4 - 0.12 * i), body_z(f, t)^ - 0.04 * L]], fc)^,
        *[for (side in [-1.0, 1.0]) fan(f, [side * pw * 0.9, 0.0 - ph * 0.35, pz], [for (j in 0 to 4, let k = j / 4.0)
            [side * (pw + 0.13 * L * sin(0.4 + 1.2 * k)), 0.0 - ph * (0.4 + 0.5 * k), pz - 0.05 * L - 0.12 * L * k]], fc)^]]
}
// an eye: a pale iris, a dark pupil looking out to the side, and a highlight
fn eye(f, sp, side) any^ => (let t = 0.1, let z = body_z(f, t)^ - 0.004, let r = 0.034 * f.length + 0.004, let on0 = rig.rigid(0)^,
    let x = side * sp.width * f.length * profile(t)^ * 0.86, let y = sp.height * f.length * profile(t)^ * 0.28,
    [rig.ellipsoid([x, y, z], [r * 0.5, r, r], rig.IDENTITY, rig.flat(C.iris)^, on0)^,
     rig.ellipsoid([x + side * r * 0.3, y, z + r * 0.05], [r * 0.32, r * 0.58, r * 0.58], rig.IDENTITY, rig.flat(C.dark)^, on0)^,
     rig.ellipsoid([x + side * r * 0.42, y + r * 0.3, z + r * 0.25], [r * 0.12, r * 0.16, r * 0.16], rig.IDENTITY, rig.flat(C.shine)^, on0)^])
fn eyes(f, sp) any^ => [*(eye(f, sp, -1.0)^), *(eye(f, sp, 1.0)^)]
// a carp's two barbels at the corners of its mouth
fn barbels(f, sp) any^ => if (f.kind != "carp") [] else (let z = body_z(f, 0.02)^, let r = sp.width * f.length * profile(0.03)^,
    [for (side in [-1.0, 1.0]) rig.tube([[side * r * 0.7, -0.01 * f.length, z], [side * r * 1.4, -0.045 * f.length, z + 0.03 * f.length]],
        [0.004, 0.0015], 4, (k, a) => C.carp_side, rig.rigid(0)^)^])
pub fn buffer(f) any^ {
    if (f.kind == "ray") { return ray_buffer(f)^ }
    let sp = SPECIES[f.kind]
    return rig.merge([rig.surface(22, 14, body_sample(f, sp)^, (p) any^ => spine(f, p[2])^, true)^, *(fins(f, sp)^), *(eyes(f, sp)^), *(barbels(f, sp)^)])^
}

// ---- the spotted ray: a flat loft whose sections are wing-wide and body-thick in the middle
let RAY_TOP = w.col("#5a6e82")
let RAY_SPOT = w.col("#e9eef0")
let RAY_BELLY = w.col("#e6e3d8")
fn ray_span(t) any^ => 0.5 * math.pow(max(0.0, sin(math.pi * math.pow(max(t, 0.002), 0.75))), 0.85) + 0.02
fn ray_spot(f, x, z) any^ => (let cell = 0.075 * f.length, let cx = floor(x / cell), let cz = floor(z / cell),
    let ox = (cx + 0.25 + 0.5 * hash(cx + f.n, cz)^) * cell, let oz = (cz + 0.25 + 0.5 * hash(cz, cx - f.n)^) * cell,
    sqrt((x - ox) * (x - ox) + (z - oz) * (z - oz)) < cell * 0.27 and hash(cx * 1.7, cz * 2.3 + f.n)^ > 0.35)
fn ray_sample(f) any^ => (tt, around) any^ => (let L = f.length, let a = around * u.TAU, let sn = sin(a), let cs = cos(a),
    let half_w = ray_span(tt)^ * L, let core = 1.0 + 1.4 * (1.0 - math.pow(abs(cs), 0.6)),
    let half_h = 0.03 * L * math.pow(max(0.0, sin(math.pi * tt)), 0.5) * core + 0.002,
    let x = cs * half_w, let z = 0.42 * L - tt * 0.74 * L,
    {p: [x, sn * half_h * (if (sn < 0.0) 0.55 else 1.0), z], c: if (sn < -0.05) RAY_BELLY else if (ray_spot(f, x, z)^) RAY_SPOT else RAY_TOP})
// wings bend on their own bones: root, inner and outer wing per side, then two tail bones
fn ray_bind(f) any^ => (p) any^ => (let L = f.length, let ax = abs(p[0]), let left = p[0] > 0.0, let inner = if (left) 1 else 3,
    if (p[2] < -0.32 * L) (let k = u.clamp((-0.32 * L - p[2]) / (0.4 * L), 0.0, 1.0), [5, 6, 1.0 - k, k])
    else if (ax < 0.1 * L) [0, 0, 1.0, 0.0]
    else if (ax < 0.28 * L) (let k = (ax - 0.1 * L) / (0.18 * L), [0, inner, 1.0 - k, k])
    else (let k = u.clamp((ax - 0.28 * L) / (0.2 * L), 0.0, 1.0), [inner, inner + 1, 1.0 - k, k]))
fn ray_buffer(f) any^ {
    let L = f.length
    let tail = rig.tube([for (k in 0 to 6) [0.0, 0.0, -0.3 * L - k * 0.14 * L]], [for (k in 0 to 6) 0.022 * L * (1.0 - k / 7.0) + 0.002], 6,
        (k, a) => RAY_TOP, ray_bind(f)^)^
    let on0 = rig.rigid(0)^
    let eye_parts = [for (side in [-1.0, 1.0]) rig.ellipsoid([side * 0.07 * L, 0.04 * L, 0.24 * L], [0.016 * L, 0.012 * L, 0.02 * L], rig.IDENTITY, rig.flat(C.dark)^, on0)^]
    return rig.merge([rig.surface(28, 40, ray_sample(f)^, ray_bind(f)^, true)^, tail, *eye_parts])^
}

// ---- rig, clip and body
fn bone_id(f, k) any^ => f.id ++ "-b" ++ string(k)
fn fish_skeleton(f) any^ {
    let gap = 0.2 * BODY * f.length
    let first = body_z(f, BONE_U[0])^;
    <skeleton id: f.id ++ "-rig", <bone id: bone_id(f, 0)^, position: [0.0, 0.0, first],
        <bone id: bone_id(f, 1)^, position: [0.0, 0.0, 0.0 - gap], <bone id: bone_id(f, 2)^, position: [0.0, 0.0, 0.0 - gap],
            <bone id: bone_id(f, 3)^, position: [0.0, 0.0, 0.0 - gap], <bone id: bone_id(f, 4)^, position: [0.0, 0.0, 0.0 - gap]>>>>>>
}
// ray bones: 0 centre, 1-2 left wing, 3-4 right wing, 5-6 tail
fn ray_skeleton(f) any^ {
    let L = f.length;
    <skeleton id: f.id ++ "-rig", <bone id: bone_id(f, 0)^, position: [0.0, 0.0, 0.0],
        <bone id: bone_id(f, 1)^, position: [0.1 * L, 0.0, 0.0], <bone id: bone_id(f, 2)^, position: [0.18 * L, 0.0, 0.0]>>
        <bone id: bone_id(f, 3)^, position: [-0.1 * L, 0.0, 0.0], <bone id: bone_id(f, 4)^, position: [-0.18 * L, 0.0, 0.0]>>
        <bone id: bone_id(f, 5)^, position: [0.0, 0.0, -0.32 * L], <bone id: bone_id(f, 6)^, position: [0.0, 0.0, -0.4 * L]>>>>
}
pub fn skeleton(f) any^ => if (f.kind == "ray") ray_skeleton(f)^ else fish_skeleton(f)^
let AMPLITUDE = [0.05, 0.07, 0.12, 0.19, 0.27]
let KEYS = [for (i in 0 to 12) i / 12.0]
fn yaw_q(a: float) => [0.0, sin(a / 2.0), 0.0, cos(a / 2.0)]
fn roll_q(a: float) => [0.0, 0.0, sin(a / 2.0), cos(a / 2.0)]
fn track(f, k, q) any^ => s.track(bone_id(f, k)^ ++ ".quaternion", [for (t in KEYS) t * SPECIES[f.kind].beat], [for (t in KEYS, c in q(t * u.TAU)^) c], 'quaternion')^
// one loop of the body wave (or the ray's wing-beat); the host scales its rate and weight with speed
pub fn swim_clip(f) any^ {
    let sp = SPECIES[f.kind]
    let tracks = if (f.kind == "ray") [
            track(f, 1, (a) any^ => roll_q(0.32 * sin(a + f.phase)))^, track(f, 2, (a) any^ => roll_q(0.42 * sin(a + f.phase - 0.8)))^,
            track(f, 3, (a) any^ => roll_q(-0.32 * sin(a + f.phase)))^, track(f, 4, (a) any^ => roll_q(-0.42 * sin(a + f.phase - 0.8)))^,
            track(f, 5, (a) any^ => yaw_q(0.12 * sin(a * 0.5 + f.phase)))^, track(f, 6, (a) any^ => yaw_q(0.22 * sin(a * 0.5 + f.phase - 1.0)))^]
        else [for (k in 0 to 4) track(f, k, (a) any^ => yaw_q((if (k == 0) -1.0 else 1.0) * AMPLITUDE[k] * sin(a - k * 0.95 + f.phase)))^];
    s.clip("swim-" ++ f.id, tracks, {duration: sp.beat, autoplay: true})
}
pub fn geometry(f) any^ => rig.geometry("g-" ++ f.id, buffer(f)^)^
// the whole fish inside its kinematic body; skinned bounds are authored so frustum rejection stays sound
pub fn body(f, x, y, z, heading) any^ => physics.body(f.id, [
    physics.collider(f.id ++ "-shape", 'sphere', {radius: 0.05, groups: {membership: 0, filter: 0}}),
    skeleton(f)^,
    s.mesh(geometry(f)^, "m-fish", {id: f.id ++ "-mesh", skeleton: f.id ++ "-rig", 'cast-shadow': true, 'receive-shadow': false,
        bounds: [-0.7 * f.length, -0.4 * f.length, -1.2 * f.length, 0.7 * f.length, 0.4 * f.length, 0.7 * f.length]})],
    {type: 'kinematic-position', position: [x, y, z], quaternion: u.quat(heading)})
