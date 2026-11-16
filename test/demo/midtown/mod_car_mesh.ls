// Car bodies lofted through cross-sections: a body skin whose profile carries the hood, the deck and
// the wheel arches, a glasshouse leaning in toward the roof, a roof panel and pillars in paint, and lamp
// strips that follow the body's ends. Each car is the buffers {paint, glass, trim, head, tail}, modelled
// facing +Z with the ground at y = 0, sized to mod_traffic.KINDS (and the player to mod_vehicle.SPEC).
import u: .mod_util
import m: .mod_mesh
import prim: lambda.procart.primitive

// ---- the loft: sections are {z, pts} with pts the right half of a ring, bottom centre to top centre
// ([x, y] pairs); each ring is closed by mirroring. Normals come from the neighbouring points.
fn ring_of(pts) any^ => [*pts, *[for (k in 1 to (len(pts) - 2), let p = pts[len(pts) - 1 - k]) [0.0 - p[0], p[1]]]]
pub fn loft(sections, caps = true) any^ {
    let rings = [for (sc in sections) ring_of(sc.pts)]
    let n = len(rings[0])^, m_ = len(sections)
    let at = (s, i) any^ => (let r = rings[s], let p = r[(i + n) % n], [p[0], p[1], sections[s].z])
    let normal = (s, i) any^ => (let a = at(s, i - 1), let b = at(s, i + 1), let c = at(max(0, s - 1), i), let d = at(min(m_ - 1, s + 1), i),
        let tu = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], let tv = [d[0] - c[0], d[1] - c[1], d[2] - c[2]],
        let nx = tu[1] * tv[2] - tu[2] * tv[1], let ny = tu[2] * tv[0] - tu[0] * tv[2], let nz = tu[0] * tv[1] - tu[1] * tv[0],
        let l = max(sqrt(nx * nx + ny * ny + nz * nz), 0.000001), [nx / l, ny / l, nz / l])
    let skin = {positions: [for (s in 0 to (m_ - 1), i in 0 to (n - 1)) *at(s, i)^], normals: [for (s in 0 to (m_ - 1), i in 0 to (n - 1)) *normal(s, i)^],
        uvs: [for (s in 0 to (m_ - 1), i in 0 to (n - 1)) *[i / float(n), s / float(m_)]],
        indices: [for (s in 0 to (m_ - 2), i in 0 to (n - 1), let a = s * n + i, let b = s * n + (i + 1) % n, let c = b + n, let d = a + n) *[a, b, c, a, c, d]]}
    // flat end caps: a fan from each end ring's centre, facing out along -Z and +Z
    let cap = (s, dir) any^ => (let r = rings[s], let cy = sum([for (p in r) p[1]]) / n,
        {positions: [0.0, cy, sections[s].z, *[for (p in r) *[p[0], p[1], sections[s].z]]], normals: [for (k in 0 to n) *[0.0, 0.0, dir]],
         uvs: [for (k in 0 to n) *[0.5, 0.5]], indices: [for (k in 0 to (n - 1)) *(if (dir > 0.0) [0, 1 + k, 1 + (k + 1) % n] else [0, 1 + (k + 1) % n, 1 + k])]});
    if (caps) m.merge([skin, cap(0, -1.0), cap(m_ - 1, 1.0)]) else skin
}

// ---- a body section at station z: half width w, sill y0, beltline yb and top yt; the sides tuck under
// to the sill and roll over to the top
fn section(z, w, y0, yb, yt) => {z: z, pts: [[0.0, y0], [w - 0.12, y0], [w - 0.02, y0 + 0.13], [w, y0 + 0.5 * (yb - y0) + 0.05],
    [w - 0.015, yb - 0.04], [w - 0.07, yb + 0.02], [w * 0.62, yt], [0.0, yt + 0.008]]}
// piecewise-linear lookup in a table of [z, value] rows ordered by z
fn table(rows, z) any^ {
    let k = max(0, min(len(rows) - 2, len([for (r in rows where r[0] <= z) r]) - 1))
    let a = rows[k], b = rows[k + 1]
    let t = u.clamp((z - a[0]) / max(b[0] - a[0], 0.0001), 0.0, 1.0);
    a[1] + (b[1] - a[1]) * t
}
// the sill height at z: low between the wheels, lifting into an arch over each one
fn sill(z, wz, r, low) any^ => (let d = min(abs(z - wz), abs(z + wz)), let arch = r + 0.11,
    if (d < arch) max(low, r * 0.95 + sqrt(arch * arch - d * d) * 0.82) else low)
// a body: spec {hl, hw, low, wz, r, belt: [[z, y]], top: [[z, y]], width: [[z, w]]}
fn body(sp) any^ => loft([for (k in 0 to 28, let z = 0.0 - sp.hl + k * 2.0 * sp.hl / 28.0)
    section(z, table(sp.width, z)^, sill(z, sp.wz, sp.r, table(sp.lows, z)^)^, table(sp.belt, z)^, table(sp.top, z)^)])
// the glasshouse: trapezoid sections leaning in toward the roof, from the rear window's foot to the
// windshield's (rows of [z, base y, base half width, roof y, roof half width])
fn cabin(rows) any^ => loft([for (r in rows) {z: r[0], pts: [[0.0, r[1]], [r[2], r[1]], [r[4], r[3]], [0.0, r[3]]]}])
// a panel of thickness d lying on the cabin's roof between z0 and z1 (rows as in cabin)
fn roof_panel(rows, z0, z1) any^ => loft([for (z in [z0, (z0 + z1) / 2.0, z1], let y = table([for (r in rows) [r[0], r[3]]], z)^,
        let w = table([for (r in rows) [r[0], r[4]]], z)^)
    {z: z, pts: [[0.0, y - 0.02], [w - 0.03, y - 0.02], [w + 0.004, y + 0.006], [w * 0.7, y + 0.02], [0.0, y + 0.024]]}])
// a pillar strip of paint up the glass from a = [z, base y, base w] to b = [z, roof y, roof w]; each
// side's corners run so its face points outward
fn pillar(a, b, width) any^ => m.merge([for (xs in [-1.0, 1.0], let c = [[xs * (a[2] + 0.012), a[1], a[0] - width / 2.0],
        [xs * (a[2] + 0.012), a[1], a[0] + width / 2.0], [xs * (b[2] + 0.012), b[1], b[0] + width / 2.0], [xs * (b[2] + 0.012), b[1], b[0] - width / 2.0]])
    if (xs > 0.0) m.quad(c[0], c[3], c[2], c[1]) else m.quad(c[0], c[1], c[2], c[3])])
// tyres with dark rims, for cars whose wheels do not turn separately
fn wheels(hl, hw, wz, r) any^ => m.merge([for (zs in [1.0, -1.0], xs in [1.0, -1.0]) *[
    prim.sweep([[xs * (hw - 0.2), r, zs * wz], [xs * (hw - 0.01), r, zs * wz]], r, 14, true),
    prim.sweep([[xs * (hw - 0.012), r, zs * wz], [xs * (hw + 0.004), r, zs * wz]], r * 0.62, 10, true)]])

// lamps: a thin slab following the end of the body at height y, x from x0 to x1 (both sides mirrored)
fn lamp_pair(z, dz, y, h, x0, x1) any^ => m.merge([for (xs in [-1.0, 1.0]) m.cuboid(min(xs * x0, xs * x1), y, min(z, z + dz), max(xs * x0, xs * x1), y + h, max(z, z + dz))])

// ---- the cars
let SEDAN = {hl: 2.25, hw: 0.93, wz: 1.4, r: 0.34,
    lows: [[-2.25, 0.42], [-1.95, 0.3], [1.95, 0.3], [2.25, 0.4]],
    belt: [[-2.25, 0.86], [-2.0, 0.94], [-1.45, 0.97], [0.95, 0.96], [1.7, 0.88], [2.25, 0.7]],
    top: [[-2.25, 0.88], [-2.12, 0.98], [-1.5, 1.0], [0.95, 0.98], [1.6, 0.9], [2.1, 0.8], [2.25, 0.64]],
    width: [[-2.25, 0.84], [-2.0, 0.91], [-1.0, 0.93], [1.2, 0.93], [2.0, 0.89], [2.25, 0.8]],
    cabin: [[-1.48, 0.98, 0.85, 0.99, 0.84], [-1.0, 0.98, 0.86, 1.42, 0.67], [0.2, 0.98, 0.87, 1.44, 0.68], [0.98, 0.97, 0.85, 0.98, 0.84]],
    roof: [-0.98, 0.18], head: [2.12, 0.66, 0.11], tail: [-2.27, 0.74, 0.1]}
let COUPE = {*: SEDAN, wz: 1.35, r: 0.36,
    lows: [[-2.25, 0.4], [-1.95, 0.27], [1.95, 0.27], [2.25, 0.36]],
    belt: [[-2.25, 0.8], [-2.0, 0.88], [-1.35, 0.91], [0.85, 0.88], [1.7, 0.78], [2.25, 0.6]],
    top: [[-2.25, 0.82], [-2.12, 0.92], [-1.4, 0.94], [0.85, 0.9], [1.6, 0.8], [2.1, 0.68], [2.25, 0.54]],
    width: [[-2.25, 0.86], [-1.9, 0.94], [-1.2, 0.95], [1.1, 0.94], [2.0, 0.9], [2.25, 0.78]],
    cabin: [[-1.55, 0.92, 0.84, 0.93, 0.83], [-0.85, 0.91, 0.86, 1.3, 0.64], [0.05, 0.91, 0.87, 1.31, 0.65], [0.88, 0.89, 0.85, 0.9, 0.84]],
    roof: [-0.85, 0.05], head: [2.1, 0.57, 0.1], tail: [-2.27, 0.68, 0.09]}
let HATCH = {*: SEDAN, hl: 2.0, hw: 0.9, wz: 1.22, r: 0.33,
    lows: [[-2.0, 0.44], [-1.75, 0.3], [1.75, 0.3], [2.0, 0.42]],
    belt: [[-2.0, 0.96], [-1.6, 0.98], [0.7, 0.97], [1.5, 0.88], [2.0, 0.72]],
    top: [[-2.0, 1.0], [-1.6, 1.01], [0.7, 0.99], [1.4, 0.92], [1.9, 0.82], [2.0, 0.66]],
    width: [[-2.0, 0.84], [-1.6, 0.89], [1.2, 0.9], [1.85, 0.86], [2.0, 0.78]],
    cabin: [[-1.92, 1.0, 0.84, 1.42, 0.68], [-1.55, 0.99, 0.86, 1.5, 0.68], [0.05, 0.99, 0.87, 1.5, 0.69], [0.78, 0.98, 0.86, 0.99, 0.85]],
    roof: [-1.75, -0.02], head: [1.88, 0.68, 0.11], tail: [-2.02, 0.84, 0.14]}
let SUV = {*: SEDAN, hl: 2.4, hw: 1.0, wz: 1.45, r: 0.4,
    lows: [[-2.4, 0.56], [-2.1, 0.42], [2.1, 0.42], [2.4, 0.54]],
    belt: [[-2.4, 1.1], [-2.2, 1.14], [0.95, 1.12], [1.8, 1.06], [2.4, 0.9]],
    top: [[-2.4, 1.16], [-2.22, 1.18], [0.95, 1.15], [1.7, 1.08], [2.3, 0.98], [2.4, 0.82]],
    width: [[-2.4, 0.94], [-2.1, 0.99], [1.5, 1.0], [2.25, 0.96], [2.4, 0.88]],
    cabin: [[-2.32, 1.16, 0.92, 1.66, 0.8], [-2.1, 1.15, 0.94, 1.8, 0.8], [0.25, 1.15, 0.95, 1.8, 0.81], [1.0, 1.14, 0.94, 1.15, 0.93]],
    roof: [-2.18, 0.2], head: [2.28, 0.86, 0.12], tail: [-2.42, 0.95, 0.18]}
let VAN = {*: SEDAN, hl: 2.45, hw: 1.0, wz: 1.55, r: 0.38,
    lows: [[-2.45, 0.5], [-2.2, 0.4], [2.2, 0.4], [2.45, 0.48]],
    belt: [[-2.45, 1.15], [1.15, 1.16], [1.9, 1.1], [2.45, 0.92]],
    top: [[-2.45, 1.95], [-2.4, 2.02], [0.7, 2.02], [1.0, 1.9], [1.5, 1.2], [2.3, 1.06], [2.45, 0.86]],
    width: [[-2.45, 0.97], [-2.3, 1.0], [2.2, 1.0], [2.45, 0.9]],
    cabin: [[0.65, 1.2, 0.96, 1.92, 0.88], [1.0, 1.2, 0.97, 1.86, 0.9], [1.5, 1.16, 0.97, 1.18, 0.96]],
    roof: [0.62, 0.95], head: [2.33, 0.86, 0.13], tail: [-2.47, 0.95, 0.3]}
let PICKUP = {*: SUV, hl: 2.55, hw: 1.0, wz: 1.6, r: 0.4,
    belt: [[-2.55, 1.0], [-0.4, 1.02], [1.0, 1.08], [1.8, 1.02], [2.55, 0.88]],
    top: [[-2.55, 1.02], [-0.5, 1.03], [-0.35, 1.12], [1.0, 1.12], [1.8, 1.04], [2.4, 0.95], [2.55, 0.8]],
    cabin: [[-0.42, 1.12, 0.93, 1.72, 0.8], [-0.2, 1.12, 0.94, 1.78, 0.8], [0.3, 1.12, 0.95, 1.78, 0.81], [1.02, 1.1, 0.94, 1.12, 0.93]],
    roof: [-0.3, 0.3], head: [2.43, 0.84, 0.12], tail: [-2.57, 0.8, 0.2]}
pub let SPECS = {sedan: SEDAN, coupe: COUPE, hatch: HATCH, suv: SUV, van: VAN, pickup: PICKUP, taxi: SEDAN}

pub fn car(kind, with_wheels = true) any^ {
    let sp = SPECS[kind]
    let rows = sp.cabin
    let front = rows[len(rows) - 2], back = rows[1]
    let pillars = m.merge([pillar([rows[0][0], rows[0][1], rows[0][2]], [back[0] + 0.05, back[3], back[4]], 0.22),
        pillar([(back[0] + front[0]) / 2.0, back[1], back[2]], [(back[0] + front[0]) / 2.0, back[3], back[4]], 0.12),
        pillar([rows[len(rows) - 1][0] - 0.04, rows[len(rows) - 1][1], rows[len(rows) - 1][2]], [front[0] - 0.05, front[3], front[4]], 0.1)])
    let bed = if (kind == "pickup") [m.cuboid(-0.94, 1.0, -2.5, -0.86, 1.22, -0.5), m.cuboid(0.86, 1.0, -2.5, 0.94, 1.22, -0.5), m.cuboid(-0.94, 1.0, -2.53, 0.94, 1.2, -2.45)] else [];
    {paint: m.merge([body(sp)^, roof_panel(rows, sp.roof[0], sp.roof[1])^, pillars, *bed]),
     glass: cabin(rows)^,
     trim: m.merge([
        // a dark sill and lower valance, a grille, side mirrors, and the tyres
        m.cuboid(0.0 - sp.hw + 0.03, sp.r + 0.02, 0.0 - sp.wz + sp.r + 0.15, sp.hw - 0.03, sp.r + 0.1, sp.wz - sp.r - 0.15),
        m.cuboid(-0.5, sp.head[1] - 0.24, sp.hl - 0.06, 0.5, sp.head[1] - 0.04, sp.hl + 0.005),
        *[for (xs in [-1.0, 1.0]) m.block(xs * (front[2] + 0.07), front[1] + 0.05, front[0] - 0.12, 0.16, 0.11, 0.22)],
        *(if (with_wheels) [wheels(sp.hl, sp.hw, sp.wz, sp.r)^] else [])]),
     head: m.merge([lamp_pair(sp.head[0], 0.12, sp.head[1], sp.head[2], 0.42, sp.hw - 0.12),
        *(if (kind == "taxi") [m.cuboid(-0.34, rows[1][3] + 0.06, -0.55, 0.34, rows[1][3] + 0.28, -0.22)] else [])]),
     tail: m.merge([lamp_pair(sp.tail[0], 0.05, sp.tail[1], sp.tail[2], 0.32, sp.hw - 0.1), m.cuboid(-0.32, sp.tail[1] + sp.tail[2] * 0.3, sp.tail[0], 0.32, sp.tail[1] + sp.tail[2] * 0.7, sp.tail[0] + 0.05)])}
}
// every kind built once, for traffic, parked cars and the player (whose wheels turn separately)
pub let CARS = {sedan: car("sedan") ^ { null }, hatch: car("hatch") ^ { null }, van: car("van") ^ { null }, taxi: car("taxi") ^ { null },
    pickup: car("pickup") ^ { null }, suv: car("suv") ^ { null }, player: car("coupe", false) ^ { null }}
// the player coupe's rear indicator: its tail lamp grown just enough to cover the lit side (+1 is the car's left)
pub fn indicator(xs) any^ => (let sp = COUPE, let x0 = xs * 0.6, let x1 = xs * (sp.hw - 0.09),
    m.cuboid(min(x0, x1), sp.tail[1] - 0.006, sp.tail[0] - 0.006, max(x0, x1), sp.tail[1] + sp.tail[2] + 0.006, sp.tail[0] + 0.056))

// ---- one draw per car: the parts merge into one buffer whose UVs pick a cell of a five-cell atlas
// (paint, glass, trim, head lamp, tail lamp), so a single material with atlas maps shades them all
pub let ATLAS_PARTS = ["paint", "glass", "trim", "head", "tail"]
pub fn merged(parts) any^ => m.merge([for (k, name in ATLAS_PARTS, let b = parts[name])
    {*: b, uvs: [for (i in 1 to (len(b.positions)^ div 3)) *[(k + 0.5) / 5.0, 0.5]]}])
// a car to tint: its merged buffer and each vertex's paint share (1 on the paint cell, which merges first)
pub fn tintable(parts) any^ => (let n = len(parts.paint.positions)^ div 3, let b = merged(parts)^,
    {buffer: b, mask: [for (i in 0 to (len(b.positions)^ div 3 - 1)) if (i < n) 1.0 else 0.0]})
// its buffer with the paint cell tinted by a linear colour and the other cells left white
pub fn tinted(car, rgb) any^ => {*: car.buffer, colors: flatten(transpose([for (c in rgb) 1.0 + car.mask * (c - 1.0)]))}
