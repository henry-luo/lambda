// The cove's plants and stones, as plain static buffers or as one clump to instance: smooth
// noise-sculpted boulders with crevice shading, lichen and algae; notched, veined water-lily pads
// and star lilies; lotus stands with round wavy leaves (some held above the water on stems), tall
// layered flowers and buds; and underwater lettuce rosettes, kelp, sea fans and moss balls.
// Colours are linear light in the vertices; every buffer is {positions, normals, colors, indices}.
import shape: lambda.scene3d.shape
import t: lambda.scene3d.transform
import noise: lambda.procart.noise
import prim: lambda.procart.primitive
import rng: lambda.procart.rng
import m: ~~.midtown.mod_mesh
import u: ~~.midtown.mod_util
import w: .mod_world
import rig: .mod_rig

let P = noise.table(rng.key(w.SEED + 3) ^ { u64(7) })
fn n3(x, y, z) any^ => noise.perlin3(P, x, y, z)^
fn unbound(p) => [0, 0, 1.0, 0.0]
// a rig part as a plain buffer (its skin channels are dropped by mod_mesh's geometry)
fn plain(parts) any^ => rig.merge(parts)^
fn placed(b, x, y, z, yaw, sx, sy, sz) any^ => m.placed(b, t.compose([x, y, z], u.quat(yaw), [sx, sy, sz]))^
fn tinted(b, rgb) any^ => m.tinted(b, rgb)^

// ---- boulders: a sphere pushed out by two octaves of noise, its base flattened into the floor
let STONE = w.col("#858b80")
let STONE_LIGHT = w.col("#a3a598")
let STONE_WARM = w.col("#958a76")
let CREVICE = w.col("#4a5048")
let LICHEN = w.col("#97a15c")
let MOSS = w.col("#5d7a3c")
let ALGAE = w.col("#3e5232")
fn rock_sample(k) => (tt, around) any^ => (let th = tt * math.pi, let ph = around * u.TAU,
    let d = [sin(th) * cos(ph), cos(th), sin(th) * sin(ph)], let sd = k.i * 3.17,
    let lump = 0.2 * n3(d[0] * 1.5 + sd, d[1] * 1.5, d[2] * 1.5 - sd)^ + 0.07 * n3(d[0] * 4.2 - sd, d[1] * 4.2 + sd, d[2] * 4.2)^,
    let r = 1.0 + lump, let y = d[1] * r, let flat = if (y < -0.3) -0.3 + (y + 0.3) * 0.25 else y,
    let local = [d[0] * r * k.r, flat * k.r * k.h, d[2] * r * k.r * 0.86],
    let world = rig.add([k.x, k.y + k.r * k.h * 0.3, k.z], rig.qrot(u.quat(k.yaw), local)^),
    let patch = 0.5 + 0.5 * n3(d[0] * 3.0 + sd, d[1] * 3.0, d[2] * 3.0)^, let grain = 0.5 + 0.5 * n3(d[0] * 9.0, d[1] * 9.0 + sd, d[2] * 9.0)^,
    let base = rig.mix(rig.mix(STONE, STONE_LIGHT, grain), STONE_WARM, patch * 0.5)^,
    let dark = rig.mix(base, CREVICE, u.clamp(0.0 - lump * 4.0, 0.0, 0.7))^,
    let top = u.clamp((d[1] - 0.15) * 2.5, 0.0, 1.0) * u.clamp((patch - 0.45) * 3.0, 0.0, 1.0),
    let low = u.clamp((-0.05 - d[1]) * 3.0, 0.0, 1.0),
    {p: world, c: rig.mix(rig.mix(dark, rig.mix(LICHEN, MOSS, grain)^, top * 0.75)^, ALGAE, low * 0.6)^})
pub fn rock_buffer(k) any^ => plain([rig.surface(14, 22, rock_sample(k), unbound)^])^

// ---- water lilies
let PAD_FRESH = w.col("#5a9b3d")
let PAD_OLD = w.col("#a1a742")
let PAD_RIM = w.col("#3f7a31")
let PAD_RED = w.col("#7e4a35")
let PAD_VEIN = w.col("#8fc160")
// a pad: a cupped disc with a V notch, a wavy rim and radial veins
pub fn pad_buffer(p) any^ {
    let seg = 26
    let base = rig.mix(PAD_FRESH, PAD_OLD, u.clamp((p.age - 0.7) * 3.0, 0.0, 1.0))^
    let rim_rgb = if (p.age < 0.15) PAD_RED else PAD_RIM
    let angles = [for (i in 0 to seg) 0.22 + (u.TAU - 0.3) * i / float(seg)]
    let ring = (f, lift) => [for (a in angles) [cos(a) * f * (1.0 + 0.025 * sin(9.0 * a + p.yaw)), lift, sin(a) * f * (1.0 + 0.025 * sin(9.0 * a + p.yaw))]]
    let mid = ring(0.6, 0.004), rim = ring(1.0, 0.012)
    let verts = [[0.0, 0.0, 0.0], *mid, *rim]
    let n = seg + 1
    let colors = [PAD_VEIN, *[for (i, a in angles) if (i % 3 == 0) PAD_VEIN else base], *[for (i, a in angles) if (i % 3 == 0) rig.mix(PAD_VEIN, rim_rgb, 0.5)^ else rim_rgb]]
    let b = {positions: [for (q in verts, c in q) c], normals: [for (q in verts, c in [0.0, 1.0, 0.0]) c], colors: [for (q in colors, c in q) c],
        indices: [*[for (i in 0 to seg - 1, q in [0, i + 2, i + 1]) q], *[for (i in 0 to seg - 1, q in [1 + i, 2 + i, 1 + n + i, 2 + i, 2 + n + i, 1 + n + i]) q]]};
    return placed(b, p.x, 0.016, p.z, p.yaw, p.r, 1.0, p.r)^
}
let LILY = {white: [w.col("#f6f2e6"), w.col("#fffdf7"), w.col("#e3dfb6")], pink: [w.col("#f0a3bd"), w.col("#fde3ec"), w.col("#f6cfd8")],
    yellow: [w.col("#f4d257"), w.col("#fbeea8"), w.col("#efd88a")]}
let STAMEN = w.col("#f1bf34")
// one pointed petal along +Z from the centre, rising by `tilt` radians
fn petal(yaw, tilt, size, width, colours) any^ {
    let shape = [[0.0, 0.0, 0.0], [width, 0.0, size * 0.4], [0.0, 0.0, size], [0.0 - width, 0.0, size * 0.4], [0.0, 0.012, size * 0.45]]
    let q = rig.qmul(u.quat(yaw)^, [sin(-tilt / 2.0), 0.0, 0.0, cos(-tilt / 2.0)])^
    let pts = [for (p in shape) rig.qrot(q, p)^]
    let up = rig.qrot(q, [0.0, 1.0, 0.0])^;
    {positions: [for (p in pts, c in p) c], normals: [for (p in pts, c in up) c], colors: [*colours[2], *colours[0], *colours[1], *colours[0], *colours[0]],
     indices: [0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0]}
}
pub fn lily_buffer(p) any^ {
    let colours = LILY[p.flower]
    let layers = [[10, 0.42, 0.11, 0.032], [8, 0.8, 0.085, 0.028], [6, 1.15, 0.06, 0.022]]
    let petals = [for (l, layer in layers, i in 0 to layer[0] - 1) petal(i * u.TAU / layer[0] + l * 0.31, layer[1], layer[2], layer[3], colours)^]
    let heart = tinted(m.post(0.0, 0.0, 0.0, 0.035, 0.022, 0.016, 8), STAMEN)^
    return placed(m.merge([*petals, heart]), p.x + 0.04, 0.03, p.z + 0.03, p.yaw, 1.0, 1.0, 1.0)^
}

// ---- lotus: round wavy leaves cupped like shallow bowls, some lifted on stems, and tall flowers
let LEAF = w.col("#5f9a58")
let LEAF_PALE = w.col("#a9cc7c")
let LEAF_VEIN = w.col("#7db068")
let STEM = w.col("#6f9a4a")
let LOTUS_BASE = w.col("#fcf1f2")
let LOTUS_TIP = w.col("#e46f98")
let POD = w.col("#d9d26a")
let POD_DOT = w.col("#8f8b3a")
fn leaf_sample(r, seed) => (tt, around) any^ => (let a = around * u.TAU, let rr = r * tt,
    let y = 0.09 * r * tt * tt + 0.03 * r * sin(7.0 * a + seed) * tt * tt,
    {p: [cos(a) * rr, y, sin(a) * rr], c: rig.mix(rig.mix(LEAF_PALE, LEAF, u.clamp(tt * 2.0, 0.0, 1.0))^, LEAF_VEIN, if (abs(sin(6.0 * a)) > 0.94) 0.6 else 0.0)^})
fn leaf(x, y, z, r, seed) any^ => placed(plain([rig.surface(6, 26, leaf_sample(r, seed), unbound)^])^, x, y, z, seed, 1.0, 1.0, 1.0)^
fn stem(x, z, top, lean) any^ => tinted(m.merge([m.post(x, z, -0.6, top, 0.012, 0.01, 6)]), STEM)^
fn flower(x, y, z, size, seed) any^ {
    let layers = [[8, 1.05, 1.0], [8, 0.7, 0.85], [6, 0.35, 0.7]]
    let petals = [for (l, layer in layers, i in 0 to layer[0] - 1)
        lotus_petal(i * u.TAU / layer[0] + l * 0.4 + seed, layer[1], size * layer[2])^]
    let pod = m.merge([tinted(m.post(0.0, 0.0, 0.0, size * 0.32, size * 0.16, size * 0.2, 10), POD)^,
        *[for (j in 0 to 6, let a = j * 0.9) tinted(m.post(cos(a) * size * 0.1, sin(a) * size * 0.1, size * 0.32, size * 0.335, size * 0.025, size * 0.025, 5), POD_DOT)^]])
    return placed(m.merge([*petals, pod]), x, y, z, seed, 1.0, 1.0, 1.0)^
}
// a broad spoon-shaped petal, concave toward the flower's middle
fn lotus_petal(yaw, tilt, size) any^ {
    let rows = 4, cols = 4
    let grid = [for (j in 0 to rows, i in 0 to cols, let v = j / float(rows), let x = (i / float(cols) - 0.5) * size * 0.55 * sin(math.pi * (0.25 + 0.65 * v)))
        {p: [x, size * 0.12 * abs(x) / (size * 0.3) * (1.0 - v * 0.3), v * size], c: rig.mix(LOTUS_BASE, LOTUS_TIP, math.pow(v, 1.6))^}]
    let q = rig.qmul(u.quat(yaw)^, [sin(-tilt / 2.0), 0.0, 0.0, cos(-tilt / 2.0)])^
    let pts = [for (g in grid) rig.qrot(q, g.p)^]
    let up = rig.qrot(q, [0.0, 1.0, 0.0])^;
    {positions: [for (p in pts, c in p) c], normals: [for (p in pts, c in up) c], colors: [for (g in grid, c in g.c) c],
     indices: [for (j in 0 to rows - 1, i in 0 to cols - 1, q2 in cell(j * (cols + 1) + i, cols + 1)) q2]}
}
// grid triangles facing +Y for rows running +Z and columns +X
fn cell(a, row) => [a, a + row, a + 1, a + 1, a + row, a + row + 1]
fn bud(x, y, z, size) any^ => m.merge([tinted(m.placed(shape.sphere(6, 8), t.compose([x, y + size, z], [0.0, 0.0, 0.0, 1.0], [size * 0.9, size * 2.2, size * 0.9]))^,
    LOTUS_TIP)^])
pub fn lotus_buffer(st) any^ {
    let i = st.i
    let leaves = [for (j in 0 to 6, let a = (w.roll(900 + i * 20 + j, 0) ^ { 0.5 }) * u.TAU, let d = 0.2 + 0.85 * (w.roll(900 + i * 20 + j, 1) ^ { 0.5 }),
        let r = 0.17 + 0.15 * (w.roll(900 + i * 20 + j, 2) ^ { 0.5 }), let lift = if (j % 3 == 0) 0.25 + 0.35 * (w.roll(900 + i * 20 + j, 3) ^ { 0.5 }) else 0.02)
        {x: st.x + cos(a) * d, z: st.z + sin(a) * d, r: r, lift: lift, seed: a}]
    let blooms = [for (j in 0 to 2, let a = j * 2.3 + i, let d = 0.35 + 0.25 * j) {x: st.x + cos(a) * d, z: st.z + sin(a) * d, h: 0.55 + 0.18 * j, seed: a}]
    return m.merge([*[for (l in leaves) leaf(l.x, l.lift, l.z, l.r, l.seed)^], *[for (l in leaves where l.lift > 0.1) stem(l.x, l.z, l.lift, 0.0)^],
        *[for (b in slice(blooms, 0, 2)) flower(b.x, b.h, b.z, 0.17, b.seed)^], *[for (b in slice(blooms, 0, 2)) stem(b.x, b.z, b.h, 0.0)^],
        bud(blooms[2].x, blooms[2].h * 0.8, blooms[2].z, 0.05)^, stem(blooms[2].x, blooms[2].z, blooms[2].h * 0.8, 0.0)^])
}

// ---- underwater clumps, rooted at their origin so the wind clip bends them from the base
let LETTUCE = w.col("#5f9f5c")
let LETTUCE_EDGE = w.col("#9fce78")
let LETTUCE_VEIN = w.col("#b9dc94")
// a rosette of broad ruffled leaves curling outward
fn lettuce_leaf(yaw, size) any^ {
    let rows = 5, cols = 4
    let grid = [for (j in 0 to rows, i in 0 to cols, let v = j / float(rows), let x = (i / float(cols) - 0.5) * size * 0.8 * sin(math.pi * (0.15 + 0.8 * v)))
        {p: [x, size * (0.5 * v - 0.25 * v * v) + 0.03 * size * sin(i * 2.6 + j * 1.7) * v, size * (0.15 + 0.7 * v)],
         c: if (i == 2) LETTUCE_VEIN else rig.mix(LETTUCE, LETTUCE_EDGE, v)^}]
    let q = u.quat(yaw)^
    let pts = [for (g in grid) rig.qrot(q, g.p)^];
    {positions: [for (p in pts, c in p) c], normals: [for (p in pts, c in [0.0, 1.0, 0.0]) c], colors: [for (g in grid, c in g.c) c],
     indices: [for (j in 0 to rows - 1, i in 0 to cols - 1, q2 in cell(j * (cols + 1) + i, cols + 1)) q2]}
}
pub fn lettuce_buffer() any^ => m.merge([for (k in 0 to 8) lettuce_leaf(k * 2.4, 0.22 + 0.04 * (k % 3))^])
let KELP = w.col("#6e7f37")
let KELP_TIP = w.col("#a8aa55")
// tall straps with wavy edges and a slow twist
fn strap(yaw, height, width) any^ {
    let rows = 12
    let pts = [for (j in 0 to rows, side in [-1.0, 1.0], let v = j / float(rows), let tw = yaw + v * 1.2,
        let wv = width * (0.6 + 0.4 * sin(v * math.pi)) * (1.0 + 0.25 * sin(v * 18.0 + side)))
        [cos(tw) * wv * side * 0.5 + 0.08 * sin(v * 3.0 + yaw) * v, v * height, sin(tw) * wv * side * 0.5]];
    {positions: [for (p in pts, c in p) c], normals: [for (j in 0 to rows, side in [-1.0, 1.0], c in [0.0, 0.3, 1.0]) c],
     colors: [for (j in 0 to rows, side in [-1.0, 1.0], c in rig.mix(KELP, KELP_TIP, j / float(rows))^) c],
     indices: [for (j in 0 to rows - 1, q in [j * 2, j * 2 + 1, j * 2 + 2, j * 2 + 1, j * 2 + 3, j * 2 + 2]) q]}
}
pub fn kelp_buffer() any^ => m.merge([for (k in 0 to 3) strap(k * 1.6, 1.0 + 0.2 * k, 0.15)^])
let FAN = w.col("#a8519a")
let FAN_TIP = w.col("#dc8fc6")
// a sea fan: a flat tree of thin branches in the x-y plane
fn branches(x, y, a, length, depth) => if (depth == 0) [] else (let ex = x + sin(a) * length, let ey = y + cos(a) * length,
    [{a: [x, y], b: [ex, ey], d: depth}, *branches(ex, ey, a - 0.45, length * 0.7, depth - 1), *branches(ex, ey, a + 0.4, length * 0.72, depth - 1)])
pub fn fan_buffer() any^ => m.merge([for (b in branches(0.0, 0.0, 0.0, 0.12, 5))
    tinted(prim.sweep([[b.a[0], b.a[1], 0.0], [b.b[0], b.b[1], 0.0]], [0.003 + 0.0022 * b.d, 0.002 + 0.0022 * b.d], 4, false), if (b.d > 2) FAN else FAN_TIP)^])
pub fn moss_buffer() any^ => m.merge([for (k in 0 to 3, let a = k * 1.7, let r = 0.06 + 0.02 * (k % 2))
    tinted(m.placed(shape.sphere(6, 8), t.compose([cos(a) * 0.07 * k, r * 0.8, sin(a) * 0.07 * k], [0.0, 0.0, 0.0, 1.0], [r * 2.0, r * 1.6, r * 2.0]))^,
        rig.mix(w.col("#3b6a34"), w.col("#5f8c45"), k / 3.0)^)^])
