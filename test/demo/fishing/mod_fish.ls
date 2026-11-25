// Fish: a lofted body on a five-bone spine, with tail, dorsal, pectoral and belly fins, painted by
// species, and a looping swim clip of travelling body waves (amplitude grows toward the tail, as in
// carangiform swimmers). A fish faces +Z with its nose at +L/2; its kinematic body carries the
// skeleton, so the host moves the whole fish with one pose target per frame.
import s: lambda.scene3d
import physics: lambda.physics
import u: ~~.midtown.mod_util
import w: .mod_world

// beat: tail-beat period in seconds; depth: the band below the surface it cruises; speed: cruise m/s
pub let SPECIES = {
    koi: {name: "Koi", length: [0.5, 0.78], width: 0.115, height: 0.14, tail: 0.3, beat: 1.0, depth: [0.35, 1.3], speed: 0.45},
    trout: {name: "Rainbow trout", length: [0.42, 0.62], width: 0.08, height: 0.12, tail: 0.2, beat: 0.75, depth: [0.5, 1.6], speed: 0.7},
    perch: {name: "Perch", length: [0.26, 0.38], width: 0.085, height: 0.18, tail: 0.2, beat: 0.6, depth: [0.4, 1.5], speed: 0.55},
    bluegill: {name: "Bluegill", length: [0.18, 0.27], width: 0.075, height: 0.24, tail: 0.18, beat: 0.5, depth: [0.3, 1.1], speed: 0.5},
    minnow: {name: "Minnow", length: [0.11, 0.15], width: 0.075, height: 0.1, tail: 0.2, beat: 0.32, depth: [0.25, 0.9], speed: 0.85}}
let COUNTS = [["koi", 5], ["trout", 3], ["perch", 4], ["bluegill", 4], ["minnow", 8]]
// one record per fish: identity, species, size, colour variant and swim phase (positions live in the sim)
pub let ROSTER = [for (k, entry in COUNTS, n in 0 to entry[1] - 1, let i = k * 10 + n, let sp = SPECIES[entry[0]])
    {id: "fish-" ++ string(i), kind: entry[0], n: i, look: n % 4, phase: (w.roll(2000 + i, 0) ^ { 0.0 }) * u.TAU,
     length: sp.length[0] + (sp.length[1] - sp.length[0]) * (w.roll(2000 + i, 1) ^ { 0.5 })}]

// ---- shape
let RINGS = 16
let AROUND = 12
let BODY = 0.78                        // body share of the length; the tail fin makes up the rest
pub let BONE_U = [0.12, 0.32, 0.52, 0.72, 0.92]
fn profile(t) => if (t >= 0.995) 0.14 else max(0.14, math.pow(max(0.0, sin(math.pi * math.pow(t, 0.6))), 0.78))
fn body_z(f, t) => f.length / 2.0 - t * BODY * f.length
// spine blend: vertex at z takes the two nearest bones
fn spine(f, z) any^ => (let first = body_z(f, BONE_U[0]), let gap = 0.2 * BODY * f.length, let q = u.clamp((first - z) / gap, 0.0, 4.0),
    let k = min(3, int(floor(q))), [k, k + 1, 1.0 - (q - k), q - k])

// ---- paint
let C = {white: w.col("#f3efe6"), red: w.col("#e2552f"), gold: w.col("#f0b23e"), black: w.col("#2a2a2a"), orange: w.col("#f08a3a"),
    olive: w.col("#55663a"), silver: w.col("#c9cdc6"), pink: w.col("#e48a8a"), belly: w.col("#f1ece0"), perch: w.col("#b9b24e"),
    perch_back: w.col("#8a9440"), gill_back: w.col("#5d8a86"), gill_side: w.col("#6f8e7c"), gill_belly: w.col("#e8a63c"),
    minnow_back: w.col("#7fb0bd"), minnow_side: w.col("#d6dddc"), dark: w.col("#1e2224"), fin_orange: w.col("#e2723c"),
    fin_clear: w.col("#d9d2bf"), trout_back: w.col("#8a9a62")}
fn mix(a, b, k) => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * k]
fn hash(a, b) => (let v = sin(a * 127.1 + b * 311.7) * 43758.5453, v - floor(v))
// colour at body position t (0 nose .. 1 tail) and around-angle sine (1 = back, -1 = belly)
pub fn skin(f, t, up) any^ {
    let back = u.clamp(up * 0.5 + 0.5, 0.0, 1.0)
    let blot = hash(floor(t * 7.0) + f.n * 3.0, floor(up * 2.5 + 2.0))
    if (f.kind == "koi") (
        if (f.look == 1) mix(C.gold, C.orange, back * 0.5)
        else if (f.look == 3) mix(C.orange, C.gold, 1.0 - back)
        else if (up > -0.35 and blot > 0.45) (if (f.look == 2 and blot > 0.85) C.black else C.red)
        else C.white)
    else if (f.kind == "trout") (let band = 1.0 - u.clamp(abs(up + 0.05) / 0.22, 0.0, 1.0),
        let speck = if (up > 0.0 and hash(t * 40.0 + f.n, up * 13.0) > 0.86) 1.0 else 0.0,
        mix(mix(mix(mix(C.belly, C.silver, u.clamp(up + 0.6, 0.0, 1.0)), C.trout_back, u.clamp(up * 1.6 - 0.2, 0.0, 1.0)), C.pink, band * 0.75), C.dark, speck))
    else if (f.kind == "perch") (let bar = if (up > -0.4 and sin(t * math.pi * 11.0) > 0.45 and t > 0.15 and t < 0.85) 0.6 else 0.0,
        mix(mix(mix(C.belly, C.perch, u.clamp(up + 0.8, 0.0, 1.0)), C.perch_back, u.clamp(up, 0.0, 1.0)), C.dark, bar))
    else if (f.kind == "bluegill") (let ear = if (t > 0.17 and t < 0.27 and abs(up) < 0.25) 1.0 else 0.0,
        mix(mix(mix(C.gill_belly, C.gill_side, u.clamp(up + 0.5, 0.0, 1.0)), C.gill_back, u.clamp(up, 0.0, 1.0)), C.dark, ear * 0.85))
    else (let line = 1.0 - u.clamp(abs(up - 0.05) / 0.08, 0.0, 1.0),
        mix(mix(C.minnow_side, C.minnow_back, u.clamp(up * 1.5, 0.0, 1.0)), C.dark, line * 0.55))
}
fn fin_color(f) any^ => if (f.kind == "koi") (if (f.look == 1 or f.look == 3) C.gold else C.white)
    else if (f.kind == "perch") C.fin_orange else if (f.kind == "bluegill") C.gill_side
    else if (f.kind == "trout") C.trout_back else C.gold

// ---- buffers: body rings, nose tip, fins; every vertex records its spine blend
fn ring_vertex(f, sp, r, a) any^ => (let t = r / float(RINGS), let z = body_z(f, t), let pr = profile(t),
    let half_w = sp.width * f.length * pr, let half_h = sp.height * f.length * pr, let sn = sin(a), let cs = cos(a),
    let y = sn * half_h * (if (sn < 0.0) 0.9 else 1.0),
    {p: [cs * half_w, y, z], n: [cs / max(half_w, 0.001), sn / max(half_h, 0.001), 0.0], c: skin(f, t, sn)^, b: spine(f, z)^})
fn fin_vertex(f, p, rgb) any^ => {p: p, n: [1.0, 0.0, 0.0], c: rgb, b: spine(f, p[2])^}
fn fins(f, sp) any^ {
    let L = f.length
    let root = body_z(f, 1.0), hp = sp.height * L * 0.14, span = sp.tail * L
    let lobe = if (f.kind == "koi") 0.75 else if (f.kind == "trout") 0.5 else 0.6
    let tip_h = sp.height * L * (if (f.kind == "koi") 1.2 else 0.95)
    let fc = fin_color(f)^
    let caudal = [[0.0, hp, root], [0.0, -hp, root], [0.0, tip_h, root - span], [0.0, 0.0, root - span * lobe], [0.0, 0.0 - tip_h, root - span]]
    let d0 = body_z(f, 0.3), d1 = body_z(f, 0.62), top0 = sp.height * L * profile(0.3), top1 = sp.height * L * profile(0.62)
    let dorsal = [[0.0, top0 * 0.95, d0], [0.0, top1 * 0.95, d1], [0.0, top0 + sp.height * L * 0.7, d0 - 0.06 * L], [0.0, top1 + sp.height * L * 0.25, d1]]
    let pz = body_z(f, 0.24), pw = sp.width * L * profile(0.24), ph = sp.height * L * profile(0.24)
    let pecs = [for (side in [-1.0, 1.0]) [[side * pw * 0.9, 0.0 - ph * 0.35, pz], [side * pw * 0.9, 0.0 - ph * 0.5, pz - 0.08 * L],
        [side * (pw + 0.12 * L), 0.0 - ph * 0.9, pz - 0.14 * L]]]
    let az = body_z(f, 0.7), ah = sp.height * L * profile(0.7)
    let anal = [[0.0, 0.0 - ah * 0.85, az + 0.05 * L], [0.0, 0.0 - ah * 0.85, az - 0.07 * L], [0.0, 0.0 - ah - 0.08 * L, az - 0.06 * L]];
    {verts: [*[for (p in caudal) fin_vertex(f, p, fc)^], *[for (p in dorsal) fin_vertex(f, p, fc)^],
        *[for (tri in pecs, p in tri) fin_vertex(f, p, fc)^], *[for (p in anal) fin_vertex(f, p, fc)^]],
     tris: [0, 2, 3, 0, 3, 1, 1, 3, 4, 5, 7, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17]}
}
fn eye(f, sp, side) any^ => (let t = 0.1, let z = body_z(f, t), let x = side * sp.width * f.length * profile(t) * 0.92,
    let y = sp.height * f.length * profile(t) * 0.3, let r = 0.03 * f.length + 0.004,
    [for (d in [[r, 0.0, 0.0], [0.0 - r, 0.0, 0.0], [0.0, r, 0.0], [0.0, 0.0 - r, 0.0], [0.0, 0.0, r], [0.0, 0.0, 0.0 - r]])
        {p: [x + d[0] * 0.5 + side * r * 0.4, y + d[1], z + d[2]], n: [side, 0.0, 0.0], c: C.dark, b: [0, 1, 1.0, 0.0]}])
let EYE_TRIS = [0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5]
fn band(i0, i1) => [i0, i0 + AROUND, i1, i1, i0 + AROUND, i1 + AROUND]
pub fn buffer(f) any^ {
    let sp = SPECIES[f.kind]
    let body = [for (r in 0 to RINGS, a in 0 to AROUND - 1) ring_vertex(f, sp, r, a * u.TAU / float(AROUND))^]
    let nose = {p: [0.0, 0.0, f.length / 2.0 + 0.01 * f.length], n: [0.0, 0.0, 1.0], c: skin(f, 0.0, 0.3)^, b: [0, 1, 1.0, 0.0]}
    let tip = len(body)
    let fin = fins(f, sp)^
    let fin_base = tip + 1
    let eyes = [*eye(f, sp, -1.0)^, *eye(f, sp, 1.0)^]
    let eye_base = fin_base + len(fin.verts)
    let verts = [*body, nose, *fin.verts, *eyes]
    let tris = [*[for (r in 0 to RINGS - 1, a in 0 to AROUND - 1, k in band(r * AROUND + a, r * AROUND + (a + 1) % AROUND)) k],
        *[for (a in 0 to AROUND - 1, k in [tip, a, (a + 1) % AROUND]) k], *[for (k in fin.tris) fin_base + k],
        *[for (e in 0 to 1, k in EYE_TRIS) eye_base + e * 6 + k]];
    {positions: [for (q in verts, c in q.p) c], normals: [for (q in verts, c in q.n) c], colors: [for (q in verts, c in q.c) c],
     indices: tris, skin: [for (q in verts, c in [q.b[0], q.b[1], 0, 0]) c], weights: [for (q in verts, c in [q.b[2], q.b[3], 0.0, 0.0]) c]}
}

// ---- rig, clip and body
fn bone_id(f, k) => f.id ++ "-b" ++ string(k)
pub fn skeleton(f) any^ {
    let gap = 0.2 * BODY * f.length
    let first = body_z(f, BONE_U[0]);
    <skeleton id: f.id ++ "-rig", <bone id: bone_id(f, 0), position: [0.0, 0.0, first],
        <bone id: bone_id(f, 1), position: [0.0, 0.0, 0.0 - gap], <bone id: bone_id(f, 2), position: [0.0, 0.0, 0.0 - gap],
            <bone id: bone_id(f, 3), position: [0.0, 0.0, 0.0 - gap], <bone id: bone_id(f, 4), position: [0.0, 0.0, 0.0 - gap]>>>>>>
}
let AMPLITUDE = [0.05, 0.07, 0.12, 0.19, 0.27]
let KEYS = [for (i in 0 to 12) i / 12.0]
fn yaw_q(a) => [0.0, sin(a / 2.0), 0.0, cos(a / 2.0)]
// one loop of the body wave; the host scales its rate and weight with swimming speed
pub fn swim_clip(f) any^ {
    let sp = SPECIES[f.kind];
    s.clip("swim-" ++ f.id, [for (k in 0 to 4) s.track(bone_id(f, k) ++ ".quaternion", [for (t in KEYS) t * sp.beat],
        [for (t in KEYS, q in yaw_q((if (k == 0) -1.0 else 1.0) * AMPLITUDE[k] * sin(t * u.TAU - k * 0.95 + f.phase))^) q], 'quaternion')^],
        {duration: sp.beat, autoplay: true})
}
pub fn geometry(f) any^ {
    let b = buffer(f)^;
    s.geometry(b.positions, {id: "g-" ++ f.id, normals: b.normals, colors: b.colors, indices: b.indices, 'skin-indices': b.skin, 'skin-weights': b.weights})
}
// the whole fish inside its kinematic body; skinned bounds are authored so frustum rejection stays sound
pub fn body(f, x, y, z, heading) any^ => physics.body(f.id, [
    physics.collider(f.id ++ "-shape", 'sphere', {radius: 0.05, groups: {membership: 0, filter: 0}}),
    skeleton(f)^,
    s.mesh(geometry(f)^, "m-fish", {id: f.id ++ "-mesh", skeleton: f.id ++ "-rig", 'cast-shadow': true, 'receive-shadow': false,
        bounds: [-0.4 * f.length, -0.4 * f.length, -0.7 * f.length, 0.4 * f.length, 0.4 * f.length, 0.7 * f.length]})],
    {type: 'kinematic-position', position: [x, y, z], quaternion: u.quat(heading)})
