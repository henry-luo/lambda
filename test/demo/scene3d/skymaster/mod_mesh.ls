// Procedural mesh buffers for the gear: {p: points, n: normals, i: triangle indices}, merged per
// material and flattened into scene3d buffer geometry. Every builder winds triangles counter-clockwise
// seen from outside, so single-sided materials and shadow maps agree.
import s: lambda.scene3d
import rig: .mod_rig

let TAU = 2.0 * rig.PI
let X = [1.0, 0.0, 0.0]
let Y = [0.0, 1.0, 0.0]
let Z = [0.0, 0.0, 1.0]
pub let WORLD = {u: X, v: Y, w: Z}

fn mix3(a: any, b: any, ka: any, kb: any) => [a[0] * ka + b[0] * kb, a[1] * ka + b[1] * kb, a[2] * ka + b[2] * kb]
fn at(f, o, a, b, w) => rig.add(o, rig.add(mix3(f.u, f.v, a, b), rig.scale(f.w, w)))
// a right-handed frame whose w follows the given direction
pub fn frame(direction) {
    let w = rig.unit(direction)
    let helper = if (abs(w[1]) < 0.9) Y else X
    let u = rig.unit(rig.cross(helper, w));
    {u: u, v: rig.cross(w, u), w: w}
}
fn around(f, angle) => mix3(f.u, f.v, cos(angle), sin(angle))

pub fn merge(parts) {
    let counts = [for (m in parts) len(m.p)]
    let offsets = [for (k in 0 to (len(parts) - 1)) sum(slice(counts, 0, k))];
    {p: [for (m in parts, v in m.p) v], n: [for (m in parts, v in m.n) v],
     i: [for (k, m in parts, index in m.i) index + offsets[k]]}
}
// the port side is the starboard side reflected in z, with its triangles rewound
pub fn mirrored(m) => {p: [for (v in m.p) rig.mirror_point(v)], n: [for (v in m.n) rig.mirror_point(v)],
    i: [for (k in 0 to (len(m.i) div 3 - 1)) *[m.i[3 * k], m.i[3 * k + 2], m.i[3 * k + 1]]]}
pub fn geometry(id, m) element => s.geometry([for (v in m.p, x in v) x],
    {id: id, normals: [for (v in m.n, x in v) x], indices: m.i})

// a quad strip between two rings of k + 1 points, ring a at the start
fn strip(base, k, next) => [for (j in 0 to (k - 1), let a = base + j, let b = base + next + j)
    *[a, a + 1, b, b, a + 1, b + 1]]
fn fan(center, first, k, reverse) => [for (j in 0 to (k - 1))
    *(if (reverse) [center, first + j + 1, first + j] else [center, first + j, first + j + 1])]

// a cylinder (or a cone, with r2) from a to b, optionally capped
pub fn cylinder(a, b, r, k = 24, r2 = null, caps = true) {
    let f = frame(rig.sub(b, a))
    let top = if (r2 == null) r else r2
    let dirs = [for (j in 0 to k) around(f, TAU * j / k)]
    let side = {p: [*[for (d in dirs) rig.add(a, rig.scale(d, r))], *[for (d in dirs) rig.add(b, rig.scale(d, top))]],
        n: [*dirs, *dirs], i: strip(0, k, k + 1)}
    let ends = if (not caps) [] else [
        {p: [a, *[for (d in dirs) rig.add(a, rig.scale(d, r))]], n: [for (j in 0 to (k + 1)) rig.scale(f.w, -1.0)], i: fan(0, 1, k, true)},
        {p: [b, *[for (d in dirs) rig.add(b, rig.scale(d, top))]], n: [for (j in 0 to (k + 1)) f.w], i: fan(0, 1, k, false)}];
    merge([side, *ends])
}
// a tyre: a torus around the axle, its tube flattened to an oval section
pub fn torus(center, axis, big, radial, axial, k = 56, m = 18) {
    let f = frame(axis)
    let rings = [for (i in 0 to k) around(f, TAU * i / k)];
    {p: [for (d in rings, j in 0 to m, let phi = TAU * j / m)
            rig.add(center, rig.add(rig.scale(d, big + radial * cos(phi)), rig.scale(f.w, axial * sin(phi))))],
     n: [for (d in rings, j in 0 to m, let phi = TAU * j / m)
            rig.unit(rig.add(rig.scale(d, cos(phi) / radial), rig.scale(f.w, sin(phi) / axial)))],
     i: [for (i in 0 to (k - 1), j in 0 to (m - 1), let a = i * (m + 1) + j, let b = a + m + 1) *[a, b, a + 1, a + 1, b, b + 1]]}
}
// an oriented box: half extents along the frame's u, v and w
pub fn cuboid(center, half, f = WORLD) {
    let axes = [rig.scale(f.u, half[0]), rig.scale(f.v, half[1]), rig.scale(f.w, half[2])]
    // each face: its normal axis, sign and two tangents whose cross product is that normal
    let faces = [[0, 1, 2, 1.0], [0, 2, 1, -1.0], [1, 2, 0, 1.0], [1, 0, 2, -1.0], [2, 0, 1, 1.0], [2, 1, 0, -1.0]]
    let corners = [[-1.0, -1.0], [1.0, -1.0], [1.0, 1.0], [-1.0, 1.0]]
    let unit_axes = [f.u, f.v, f.w];
    {p: [for (face in faces, k in corners) rig.add(center, rig.add(rig.scale(axes[face[0]], face[3]),
            rig.add(rig.scale(axes[face[1]], k[0]), rig.scale(axes[face[2]], k[1]))))],
     n: [for (face in faces, k in corners) rig.scale(unit_axes[face[0]], face[3])],
     i: [for (q in 0 to 5, let a = q * 4) *[a, a + 1, a + 2, a, a + 2, a + 3]]}
}
fn shoelace(poly) => sum([for (k, pt in poly, let nx = poly[(k + 1) % len(poly)]) pt[0] * nx[1] - nx[0] * pt[1]])
// a convex polygon in the frame's (u, v) plane, extruded along w from w0 to w1
pub fn extrude(poly, f, w0, w1) {
    let ring = if (shoelace(poly) >= 0) poly else reverse(poly)
    let k = len(ring)
    let near = [for (pt in ring) at(f, [0.0, 0.0, 0.0], pt[0], pt[1], w0)]
    let far = [for (pt in ring) at(f, [0.0, 0.0, 0.0], pt[0], pt[1], w1)]
    let sides = [for (j in 0 to (k - 1), let nj = (j + 1) % k, let e = rig.sub(far[nj], far[j]))
        {p: [near[j], near[nj], far[nj], far[j]], n: (let o = rig.unit(rig.cross(e, f.w)), [o, o, o, o]), i: [0, 1, 2, 0, 2, 3]}];
    merge([{p: far, n: [for (pt in far) f.w], i: [for (j in 1 to (k - 2)) *[0, j, j + 1]]},
        {p: near, n: [for (pt in near) rig.scale(f.w, -1.0)], i: [for (j in 1 to (k - 2)) *[0, j + 1, j]]}, *sides])
}
// a spur gear around the frame's w axis: `teeth` trapezoid teeth between the root and tip circles
pub fn gear(center, f, teeth, root, tip, half_width, phase = 0.0) {
    let pitch = TAU / teeth
    let profile = [for (k in 0 to (teeth - 1), step in [[-0.40, root], [-0.18, tip], [0.18, tip], [0.40, root]], let a = phase + pitch * k)
        mix3(f.u, f.v, cos(a + step[0] * pitch) * step[1], sin(a + step[0] * pitch) * step[1])]
    let n = len(profile)
    let front = rig.add(center, rig.scale(f.w, half_width))
    let back = rig.add(center, rig.scale(f.w, 0.0 - half_width))
    let sides = [for (j in 0 to (n - 1), let nj = (j + 1) % n, let e = rig.sub(profile[nj], profile[j]))
        (let o = rig.unit(rig.cross(e, f.w)), {p: [rig.add(back, profile[j]), rig.add(back, profile[nj]), rig.add(front, profile[nj]), rig.add(front, profile[j])],
            n: [o, o, o, o], i: [0, 1, 2, 0, 2, 3]})];
    merge([{p: [front, *[for (q in profile) rig.add(front, q)]], n: [for (j in 0 to n) f.w],
            i: [for (j in 0 to (n - 1)) *[0, j + 1, (j + 1) % n + 1]]},
        {p: [back, *[for (q in profile) rig.add(back, q)]], n: [for (j in 0 to n) rig.scale(f.w, -1.0)],
            i: [for (j in 0 to (n - 1)) *[0, (j + 1) % n + 1, j + 1]]}, *sides])
}
fn bezier(c0, c1, c2, c3, u) => (let v = 1.0 - u,
    rig.add(rig.add(rig.scale(c0, v * v * v), rig.scale(c1, 3.0 * v * v * u)), rig.add(rig.scale(c2, 3.0 * v * u * u), rig.scale(c3, u * u * u))))
fn bezier_tangent(c0, c1, c2, c3, u) => (let v = 1.0 - u,
    rig.unit(rig.add(rig.add(rig.scale(rig.sub(c1, c0), 3.0 * v * v), rig.scale(rig.sub(c2, c1), 6.0 * v * u)), rig.scale(rig.sub(c3, c2), 3.0 * u * u))))
fn transport(normals, tangents, k) => if (k >= len(tangents)) normals else
    transport([*normals, rig.unit(rig.sub(normals[k - 1], rig.scale(tangents[k], rig.dot(normals[k - 1], tangents[k]))))], tangents, k + 1)
// a flat oval bar swept along a cubic Bézier, tapering from `wide`/`thin` half sizes to `ends`
pub fn sweep(c0, c1, c2, c3, wide, thin, ends, n = 28, m = 20) {
    let points = [for (i in 0 to n) bezier(c0, c1, c2, c3, i * 1.0 / n)]
    let tangents = [for (i in 0 to n) bezier_tangent(c0, c1, c2, c3, i * 1.0 / n)]
    // the thin side stays in the plane the bar bends in, so it flexes like a leaf spring
    let tail = tangents[n]
    let normals = transport([rig.unit(rig.sub(tail, rig.scale(tangents[0], rig.dot(tail, tangents[0]))))], tangents, 1)
    let frames = [for (i in 0 to n) {u: normals[i], v: rig.cross(tangents[i], normals[i]), w: tangents[i]}]
    let sizes = [for (i in 0 to n, let u = i * 1.0 / n) [thin + (ends[1] - thin) * u, wide + (ends[0] - wide) * u]]
    let body = {p: [for (i in 0 to n, j in 0 to m, let a = TAU * j / m) at(frames[i], points[i], cos(a) * sizes[i][0], sin(a) * sizes[i][1], 0.0)],
        n: [for (i in 0 to n, j in 0 to m, let a = TAU * j / m) rig.unit(mix3(frames[i].u, frames[i].v, cos(a) / sizes[i][0], sin(a) / sizes[i][1]))],
        i: [for (i in 0 to (n - 1), j in 0 to (m - 1), let a = i * (m + 1) + j, let b = a + m + 1) *[a, a + 1, b, b, a + 1, b + 1]]}
    merge([body, sweep_cap(points[0], frames[0], sizes[0], m, true), sweep_cap(points[n], frames[n], sizes[n], m, false)])
}
fn sweep_cap(point, f, size, m, reverse) => {p: [point, *[for (j in 0 to m, let a = TAU * j / m) at(f, point, cos(a) * size[0], sin(a) * size[1], 0.0)]],
    n: [for (j in 0 to (m + 1)) rig.scale(f.w, if (reverse) -1.0 else 1.0)], i: fan(0, 1, m, reverse)}
// a flat quad on the ground plane (y up), as two triangles
pub fn quad(x0, x1, z0, z1, y = 0.0) => {p: [[x0, y, z0], [x0, y, z1], [x1, y, z1], [x1, y, z0]],
    n: [Y, Y, Y, Y], i: [0, 1, 2, 0, 2, 3]}
// a cut-away tube wall: the part of a cylinder between angles a0 and a1, with its inner face and edges
fn ring(dirs, o, r) => [for (d in dirs) rig.add(o, rig.scale(d, r))]
fn shell_edge(a, b, f, d, outer, inner, end) {
    let o = rig.unit(rig.cross(d, f.w))
    let n = if (end) rig.scale(o, -1.0) else o;
    {p: [rig.add(a, rig.scale(d, inner)), rig.add(a, rig.scale(d, outer)), rig.add(b, rig.scale(d, outer)), rig.add(b, rig.scale(d, inner))],
     n: [n, n, n, n], i: if (end) [0, 2, 1, 0, 3, 2] else [0, 1, 2, 0, 2, 3]}
}
pub fn shell(a, b, outer, inner, a0, a1, k = 20) {
    let f = frame(rig.sub(b, a))
    let dirs = [for (j in 0 to k) around(f, a0 + (a1 - a0) * j / k)]
    let inward = [for (d in dirs) rig.scale(d, -1.0)];
    merge([{p: [*ring(dirs, a, outer), *ring(dirs, b, outer)], n: [*dirs, *dirs], i: strip(0, k, k + 1)},
        {p: [*ring(dirs, a, inner), *ring(dirs, b, inner)], n: [*inward, *inward],
         i: [for (j in 0 to (k - 1), let p = j, let q = k + 1 + j) *[p, q, p + 1, q, q + 1, p + 1]]},
        {p: [*ring(dirs, a, inner), *ring(dirs, a, outer)], n: [for (j in 0 to (2 * k + 1)) rig.scale(f.w, -1.0)],
         i: [for (j in 0 to (k - 1), let p = j, let q = k + 1 + j) *[p, p + 1, q, q, p + 1, q + 1]]},
        {p: [*ring(dirs, b, inner), *ring(dirs, b, outer)], n: [for (j in 0 to (2 * k + 1)) f.w],
         i: [for (j in 0 to (k - 1), let p = j, let q = k + 1 + j) *[p, q, p + 1, q, q + 1, p + 1]]},
        shell_edge(a, b, f, dirs[0], outer, inner, false), shell_edge(a, b, f, dirs[k], outer, inner, true)])
}
