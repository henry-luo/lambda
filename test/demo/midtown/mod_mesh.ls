// Mesh buffers for Midtown: {positions, normals, uvs, indices[, colors]}, merged per material and
// turned into scene3d buffer geometry. Faces wind counter-clockwise seen from outside. Vehicles and
// people are modelled facing +Z with the ground at y = 0.
import s: lambda.scene3d
import prim: lambda.procart.primitive
import v: lambda.scene3d.camera
import t: lambda.scene3d.transform
import shapes: lambda.scene3d.shape
import u: .mod_util

let UNIT_UV = [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0]
pub let EMPTY = {positions: [], normals: [], uvs: [], indices: []}
pub fn merge(parts) => prim.merge(parts)
pub fn geometry(id, b) element => s.geometry([for (x in b.positions) x], {id: id, normals: b.normals, indices: b.indices,
    *: (if (b.uvs != null) {uvs: b.uvs} else {}), *: (if (b.colors != null) {colors: b.colors} else {})})

// a planar quad a b c d, counter-clockwise from its front
pub fn quad(a, b, c, d, uv = UNIT_UV) {
    let n = v.unit(v.cross(v.subtract(b, a), v.subtract(c, a)));
    {positions: [*a, *b, *c, *d], normals: [*n, *n, *n, *n], uvs: uv, indices: [0, 1, 2, 0, 2, 3]}
}
pub fn tri(a, b, c) {
    let n = v.unit(v.cross(v.subtract(b, a), v.subtract(c, a)));
    {positions: [*a, *b, *c], normals: [*n, *n, *n], uvs: [0.0, 0.0, 1.0, 0.0, 0.0, 1.0], indices: [0, 1, 2]}
}
// a hexahedron from four bottom and four top corners, each ring ordered SW, SE, NE, NW (x east, z south)
pub fn hexa(lo, hi, bottom = true) => merge([
    *[for (k in 0 to 3, let m = (k + 1) % 4) quad(lo[k], lo[m], hi[m], hi[k])],
    quad(hi[0], hi[1], hi[2], hi[3]), *(if (bottom) [quad(lo[0], lo[3], lo[2], lo[1])] else [])])
pub fn ring(x0, z0, x1, z1, y) => [[x0, y, z1], [x1, y, z1], [x1, y, z0], [x0, y, z0]]
pub fn cuboid(x0, y0, z0, x1, y1, z1, bottom = true) => hexa(ring(x0, z0, x1, z1, y0), ring(x0, z0, x1, z1, y1), bottom)
// a box centred on (x, z) with its base at y0
pub fn block(x, y0, z, w, h, d) => cuboid(x - w / 2.0, y0, z - d / 2.0, x + w / 2.0, y0 + h, z + d / 2.0)

// apply a column-major affine matrix to a buffer; normals turn with it and are renormalised
pub fn placed(b, m) {
    let turned = t.points([m[0], m[1], m[2], 0.0, m[4], m[5], m[6], 0.0, m[8], m[9], m[10], 0.0, 0.0, 0.0, 0.0, 1.0], b.normals) ^ { [] }
    let n = [for (i in 0 to (len(turned) div 3 - 1), let x = turned[3 * i], let y = turned[3 * i + 1], let z = turned[3 * i + 2],
        let l = max(sqrt(x * x + y * y + z * z), 0.000001)) *[x / l, y / l, z / l]];
    {*: b, positions: t.points(m, b.positions) ^ { [] }, normals: n}
}
pub fn yawed(b, x, y, z, yaw, size = 1.0) => placed(b, t.compose([x, y, z], u.quat(yaw), [size, size, size]))
pub fn tinted(b, rgb) => {*: b, colors: [for (i in 1 to (len(b.positions) div 3)) *rgb]}
// a vertical cylinder or cone from y0 to y1
pub fn post(x, z, y0, y1, r0, r1, segments = 10) => prim.sweep([[x, y0, z], [x, y1, z]], [r0, r1], segments, true)

// ---- buildings: walls carry a repeating facade, a storefront band and a flat roof
pub let BAY = 4.0                  // facade texture: eight 4 m bays by eight 3.5 m storeys
pub let STOREY = 3.5
pub let BASE = 4.5                 // ground-floor shop band
fn perimeter(b) => [[b.x0, b.z1], [b.x1, b.z1], [b.x1, b.z0], [b.x0, b.z0]]
// walls between y0 and y1; u runs around the corners, v climbs from `from`
pub fn walls(b, y0, y1, u_span, v_span, u0 = 0.0, v0 = 0.0, from = 0.0) {
    let p = perimeter(b)
    let widths = [for (k in 0 to 3, let q = p[(k + 1) % 4]) abs(q[0] - p[k][0]) + abs(q[1] - p[k][1])]
    let starts = [0.0, *math.cumsum(widths)];
    merge([for (k in 0 to 3, let q = p[(k + 1) % 4], let ua = u0 + starts[k] / u_span, let ub = u0 + starts[k + 1] / u_span,
            let va = v0 + (y0 - from) / v_span, let vb = v0 + (y1 - from) / v_span)
        quad([p[k][0], y0, p[k][1]], [q[0], y0, q[1]], [q[0], y1, q[1]], [p[k][0], y1, p[k][1]], [ua, va, ub, va, ub, vb, ua, vb])])
}
pub fn facade(b, ground) => walls(b, if (b.base) ground + BASE else ground, b.h, 8.0 * BAY, 8.0 * STOREY, b.u0, b.v0, ground + BASE)
pub fn storefront(b, ground) => if (not b.base) EMPTY else walls(b, ground, ground + BASE, 4.0 * BAY, BASE, b.u0, 0.0, ground)
pub fn roof(b) => quad([b.x0, b.h, b.z1], [b.x1, b.h, b.z1], [b.x1, b.h, b.z0], [b.x0, b.h, b.z0],
    [b.x0 / 8.0, b.z1 / 8.0, b.x1 / 8.0, b.z1 / 8.0, b.x1 / 8.0, b.z0 / 8.0, b.x0 / 8.0, b.z0 / 8.0])
// rooftop clutter: plant rooms on offices and flats, a mast on each tower crown
pub fn rooftop(b) {
    let cx = (b.x0 + b.x1) / 2.0, cz = (b.z0 + b.z1) / 2.0
    let w = b.x1 - b.x0, d = b.z1 - b.z0;
    if (b.crown == true) merge([block(cx, b.h, cz, w * 0.3, 2.5, d * 0.3), post(cx, cz, b.h + 2.5, b.h + 14.0, 0.25, 0.08, 6)])
    else merge([block(cx - w * 0.18, b.h, cz + d * 0.12, w * 0.22, 1.8, d * 0.18), block(cx + w * 0.2, b.h, cz - d * 0.2, 2.2, 1.2, 2.2)])
}
// flat ground pieces with world-scaled UVs (one texture repeat per `tile` metres)
pub fn ground_quad(x0, z0, x1, z1, y, tile) => quad([x0, y, z1], [x1, y, z1], [x1, y, z0], [x0, y, z0],
    [x0 / tile, 0.0 - z1 / tile, x1 / tile, 0.0 - z1 / tile, x1 / tile, 0.0 - z0 / tile, x0 / tile, 0.0 - z0 / tile])
// a raised slab: top with world UVs and plain curb faces
pub fn slab(x0, z0, x1, z1, y0, y1, tile) => merge([ground_quad(x0, z0, x1, z1, y1, tile),
    *[for (k in 0 to 3, let lo = ring(x0, z0, x1, z1, y0), let hi = ring(x0, z0, x1, z1, y1), let m = (k + 1) % 4)
        quad(lo[k], lo[m], hi[m], hi[k], [0.0, 0.0, 0.25, 0.0, 0.25, 0.02, 0.0, 0.02])]])
// a ramp wedge rising along its heading; the low lip sits on the lot
pub fn wedge(r, base) {
    let f = u.forward(r.heading), side = u.right(r.heading)
    let hl = r.length / 2.0, hw = r.width / 2.0
    let at = (a, c, y) => [r.x + f[0] * a + side[0] * c, y, r.z + f[1] * a + side[1] * c]
    let top = base + r.height;
    merge([quad(at(0.0 - hl, 0.0 - hw, base), at(0.0 - hl, hw, base), at(hl, hw, top), at(hl, 0.0 - hw, top)),
        quad(at(hl, hw, base), at(hl, 0.0 - hw, base), at(hl, 0.0 - hw, top), at(hl, hw, top)),
        tri(at(hl, 0.0 - hw, base), at(0.0 - hl, 0.0 - hw, base), at(hl, 0.0 - hw, top)),
        tri(at(0.0 - hl, hw, base), at(hl, hw, base), at(hl, hw, top))])
}

// ---- vehicles: one buffer per material; kinds share proportions with mod_traffic.KINDS
fn wheels_at(hl, hw, r) => merge([for (zs in [1.0, -1.0], xs in [1.0, -1.0])
    prim.sweep([[xs * (hw - 0.14), r, zs * (hl - 0.85)], [xs * (hw + 0.02), r, zs * (hl - 0.85)]], r, 12, true)])
fn lamps(hl, y, w, front) {
    let z = if (front) hl - 0.01 else 0.0 - hl - 0.03;
    merge([for (xs in [-1.0, 1.0]) cuboid(xs * 0.55 - (if (xs > 0.0) 0.0 else w), y, z, xs * 0.55 + (if (xs > 0.0) w else 0.0), y + 0.14, z + 0.04)])
}
pub fn vehicle(kind, wheels = true) {
    if (kind == "van") {
        let hl = 2.45, hw = 1.0;
        {paint: merge([cuboid(0.0 - hw, 0.35, 0.0 - hl, hw, 1.15, hl), cuboid(0.0 - hw + 0.02, 1.15, 0.0 - hl, hw - 0.02, 2.05, 0.95)]),
         glass: hexa(ring(0.0 - hw + 0.04, 0.95, hw - 0.04, 1.75, 1.15), ring(0.0 - hw + 0.06, 0.95, hw - 0.06, 1.1, 1.95)),
         trim: merge([cuboid(0.0 - hw - 0.02, 0.28, hl - 0.05, hw + 0.02, 0.55, hl + 0.08), cuboid(0.0 - hw - 0.02, 0.28, 0.0 - hl - 0.08, hw + 0.02, 0.55, 0.0 - hl + 0.05),
            *(if (wheels) [wheels_at(hl, hw, 0.38)] else [])]),
         head: lamps(hl, 0.78, 0.3, true), tail: lamps(hl, 0.9, 0.22, false)}
    } else if (kind == "pickup") {
        let hl = 2.55, hw = 1.0;
        {paint: merge([cuboid(0.0 - hw, 0.38, 0.0 - hl, hw, 0.98, hl), cuboid(0.0 - hw, 0.98, 0.0 - hl, 0.0 - hw + 0.08, 1.2, -0.25),
            cuboid(hw - 0.08, 0.98, 0.0 - hl, hw, 1.2, -0.25), cuboid(0.0 - hw, 0.98, 0.0 - hl, hw, 1.2, 0.0 - hl + 0.08),
            cuboid(-0.86, 1.58, -0.15, 0.86, 1.64, 0.85)]),
         glass: hexa(ring(-0.92, -0.2, 0.92, 1.35, 0.98), ring(-0.86, -0.15, 0.86, 0.85, 1.58)),
         trim: merge([cuboid(0.0 - hw - 0.02, 0.3, hl - 0.05, hw + 0.02, 0.6, hl + 0.08), cuboid(0.0 - hw - 0.02, 0.3, 0.0 - hl - 0.08, hw + 0.02, 0.6, 0.0 - hl + 0.05),
            *(if (wheels) [wheels_at(hl, hw, 0.4)] else [])]),
         head: lamps(hl, 0.74, 0.3, true), tail: lamps(hl, 0.78, 0.2, false)}
    } else {
        // sedans, taxis and hatchbacks: a low body, a glasshouse and a roof panel
        let hatch = kind == "hatch"
        let hl = if (hatch) 2.0 else 2.25, hw = if (hatch) 0.9 else 0.93
        let back0 = if (hatch) 0.0 - hl + 0.15 else -1.35, back1 = if (hatch) 0.0 - hl + 0.3 else -0.95
        let front0 = if (hatch) 0.75 else 0.95, front1 = if (hatch) 0.05 else 0.25;
        {paint: merge([cuboid(0.0 - hw, 0.32, 0.0 - hl, hw, 0.86, hl), cuboid(-0.71, 1.36, back1 + 0.03, 0.71, 1.42, front1 - 0.03),
            block(hw + 0.07, 0.93, 0.62, 0.14, 0.1, 0.18), block(0.0 - hw - 0.07, 0.93, 0.62, 0.14, 0.1, 0.18)]),
         glass: hexa(ring(-0.86, back0, 0.86, front0, 0.86), ring(-0.72, back1, 0.72, front1, 1.36)),
         trim: merge([cuboid(-0.95, 0.24, hl - 0.06, 0.95, 0.52, hl + 0.07), cuboid(-0.95, 0.24, 0.0 - hl - 0.07, 0.95, 0.52, 0.0 - hl + 0.06),
            cuboid(-0.42, 0.5, hl - 0.02, 0.42, 0.66, hl + 0.02), *(if (wheels) [wheels_at(hl, hw, 0.34)] else [])]),
         head: merge([lamps(hl, 0.62, 0.3, true), *(if (kind == "taxi") [cuboid(-0.32, 1.42, -0.5, 0.32, 1.62, -0.2)] else [])]),
         tail: lamps(hl, 0.66, 0.33, false)}
    }
}
// the player's road wheel: a tyre around a hub with five spokes, axle along X
pub fn tyre() => prim.sweep([[-0.13, 0.0, 0.0], [0.13, 0.0, 0.0]], 0.36, 18, true)
pub fn hub() => merge([for (xs in [-1.0, 1.0]) *[prim.sweep([[xs * 0.125, 0.0, 0.0], [xs * 0.145, 0.0, 0.0]], 0.24, 14, true),
    *[for (k in 0 to 4, let a = k * u.TAU / 5.0) placed(cuboid(xs * 0.148 - 0.012, -0.03, 0.0, xs * 0.148 + 0.012, 0.03, 0.21),
        t.compose([0.0, 0.0, 0.0], [sin(a / 2.0), 0.0, 0.0, cos(a / 2.0)], [1.0, 1.0, 1.0]))]]])

// ---- people: legs, torso and arms, head and hair, coloured per vertex
pub fn person(shirt, pants, skin, hair) => merge([
    tinted(merge([block(-0.11, 0.0, 0.0, 0.16, 0.86, 0.2), block(0.11, 0.0, 0.0, 0.16, 0.86, 0.2)]), pants),
    tinted(merge([block(0.0, 0.86, 0.0, 0.44, 0.58, 0.26), block(-0.29, 0.9, 0.0, 0.11, 0.5, 0.13), block(0.29, 0.9, 0.0, 0.11, 0.5, 0.13)]), shirt),
    tinted(block(0.0, 1.44, 0.0, 0.24, 0.27, 0.24), skin),
    tinted(block(0.0, 1.66, -0.01, 0.26, 0.08, 0.26), hair)])

// ---- props
pub fn cone() => merge([cuboid(-0.21, 0.0, -0.21, 0.21, 0.04, 0.21), post(0.0, 0.0, 0.04, 0.74, 0.17, 0.035, 10)])
pub let CONE_HULL = [for (y in [0.0, 0.74], a in 0 to 5, let r = if (y == 0.0) 0.2 else 0.04, let ang = a * u.TAU / 6.0) [r * cos(ang), y, r * sin(ang)]]
pub fn crate(size) => cuboid(0.0 - size / 2.0, 0.0 - size / 2.0, 0.0 - size / 2.0, size / 2.0, size / 2.0, size / 2.0)
pub fn barrel() => post(0.0, 0.0, -0.45, 0.45, 0.3, 0.3, 12)
pub let BARREL_HULL = [for (y in [-0.45, 0.45], a in 0 to 9, let ang = a * u.TAU / 10.0) [0.3 * cos(ang), y, 0.3 * sin(ang)]]
pub fn bin() => merge([block(0.0, -0.45, 0.0, 0.62, 0.84, 0.62), block(0.0, 0.39, 0.0, 0.68, 0.08, 0.68)])

// ---- street furniture, modelled at the origin and instanced
// a lamp post on the curb, its arm reaching out over the road (+Z)
pub fn lamp_post() => merge([post(0.0, 0.0, 0.0, 7.3, 0.11, 0.08, 8), cuboid(-0.05, 7.1, 0.0, 0.05, 7.22, 1.7), block(0.0, 6.98, 1.75, 0.34, 0.16, 0.7)])
pub fn lamp_bulb() => cuboid(-0.13, 6.95, 1.47, 0.13, 6.98, 2.03, false)
// a light pool under the lamp head, laid on the road for night scenes
pub fn light_pool() => quad([-6.0, 0.02, 8.0], [6.0, 0.02, 8.0], [6.0, 0.02, -4.0], [-6.0, 0.02, -4.0])
// signal mast: local +Z faces approaching traffic, the arm runs out along +X over their lane
pub fn signal_mast() => merge([post(0.0, 0.0, 0.0, 5.9, 0.13, 0.1, 8), cuboid(0.0, 5.62, -0.07, 3.75, 5.74, 0.07),
    cuboid(3.62, 4.25, -0.12, 4.0, 5.62, 0.12), cuboid(3.52, 4.15, -0.14, 4.1, 5.72, -0.12)])
pub fn signal_lamp(slot) => (let y = 5.37 - slot * 0.42, cuboid(3.68, y - 0.15, 0.12, 3.94, y + 0.15, 0.16))
pub fn trunk() => post(0.0, 0.0, 0.0, 2.6, 0.2, 0.14, 7)
pub fn crown() => (let ball = shapes.sphere(7, 10), placed({positions: ball.positions, normals: ball.normals, uvs: ball.uvs, indices: ball.indices},
    t.compose([0.0, 3.9, 0.0], [0.0, 0.0, 0.0, 1.0], [3.8, 3.3, 3.8])))
