// Shared helpers for the skinned characters (otter, fisher, fish): quaternion aiming and a two-bone
// IK solve for posing limbs at targets, automatic skin weights from bone segments, smooth grid
// surfaces whose normals come from the surface itself, and buffers that carry their bone blend.
// A skinned buffer is {positions, normals, colors, indices, skin, weights}, four influences per
// vertex. A vertex's bone blend `b` is [i0, i1, w0, w1], or [i0, i1, i2, i3, w0, w1, w2, w3] for
// sculpted bodies that blend up to four bones.
import s: lambda.scene3d
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import shape: lambda.scene3d.shape

// ---- vectors and quaternions
pub fn add(a, b) any^ => [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
pub fn sub(a, b) any^ => [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
pub fn scale(a, k) any^ => [a[0] * k, a[1] * k, a[2] * k]
pub fn dot(a, b) any^ => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
pub fn length(a) any^ => sqrt(dot(a, a)^)
pub fn unit(a, magnitude = null) any^ => (let n = max(if (magnitude == null) length(a)^ else magnitude, 0.000001),
    [a[0] / n, a[1] / n, a[2] / n])
pub fn lerp3(a, b, k) any^ => [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k]
pub fn qmul(a, b) any^ => t.quaternion_multiply(a, b)
pub fn qrot(q, p) any^ => t.quaternion_rotate(q, p)^
pub fn qinv(q) any^ => [0.0 - q[0], 0.0 - q[1], 0.0 - q[2], q[3]]
pub let IDENTITY = [0.0, 0.0, 0.0, 1.0]
// the shortest rotation taking direction a onto direction b
pub fn aim(a, b) any^ {
    let x = unit(a)^, y = unit(b)^
    let d = dot(x, y)^
    if (d < -0.9999) {
        let side = if (abs(x[0]) < 0.9) [1.0, 0.0, 0.0] else [0.0, 1.0, 0.0]
        let axis = unit(v.cross(x, side)^)^;
        return [axis[0], axis[1], axis[2], 0.0]
    }
    let c = v.cross(x, y)
    return t.quaternion_normalize([c[0], c[1], c[2], 1.0 + d])^
}
// two-bone IK: where the middle joint goes for a chain of lengths l1, l2 from root reaching for target,
// bending toward pole; an out-of-reach target straightens the chain toward it
pub fn knee(root, target, l1, l2, pole) any^ {
    let d = sub(target, root)^
    let dist = min(max(length(d)^, 0.0001), l1 + l2 - 0.0001)
    let dir = unit(d)^
    let along = (l1 * l1 + dist * dist - l2 * l2) / (2.0 * dist)
    let height = sqrt(max(0.0, l1 * l1 - along * along))
    let bend = unit(sub(pole, scale(dir, dot(pole, dir)^)^)^)^
    return add(add(root, scale(dir, along)^)^, scale(bend, height)^)^
}
// local rotations for a two-bone chain whose bones lie along rest_upper / rest_lower in their own
// frames (bind pose, no rest rotation): the first relative to the chain's parent orientation, the
// second relative to the first
pub fn chain(parent_q, root, middle, tip, rest_upper, rest_lower) any^ {
    let upper_local = aim(rest_upper, qrot(qinv(parent_q)^, sub(middle, root)^)^)^
    let upper_world = qmul(parent_q, upper_local)^
    let lower_local = aim(rest_lower, qrot(qinv(upper_world)^, sub(tip, middle)^)^)^
    return [upper_local, lower_local]
}

// ---- skin weights: the two nearest bone segments, blended by inverse distance
fn segment_distance(p, a, b) any^ => (let ab = sub(b, a)^, let k = max(0.0, min(1.0, dot(sub(p, a)^, ab)^ / max(dot(ab, ab)^, 0.000001))),
    length(sub(p, lerp3(a, b, k)^)^)^)
// bones: [[index, start, end], ...]; `soft` widens the blend between neighbouring bones
pub fn weights(p, bones, soft = 1.0) any^ {
    let ranked = sort([for (b in bones) {i: b[0], d: segment_distance(p, b[1], b[2])^}], (x) => x.d)
    if (len(ranked) == 1) { return [ranked[0].i, ranked[0].i, 1.0, 0.0] }
    let a = ranked[0], b = ranked[1]
    let wa = 1.0 / math.pow(a.d + 0.004, 4.0 / soft), wb = 1.0 / math.pow(b.d + 0.004, 4.0 / soft)
    return [a.i, b.i, wa / (wa + wb), wb / (wa + wb)]
}

// ---- parts: lists of vertices {p, n, c, b} plus triangles, merged at the end
pub fn part(verts, tris) any^ => {verts: verts, tris: tris}
pub fn merge(parts) any^ {
    let offsets = math.cumsum([0, *[for (q in parts) len(q.verts)]])
    let verts = [for (q in parts, x in q.verts) x];
    {positions: [for (x in verts, c in x.p) c], normals: [for (x in verts, c in x.n) c], colors: [for (x in verts, c in x.c) c],
     indices: [for (i, q in parts, k in q.tris) k + offsets[i]],
     skin: [for (x in verts, c in (if ((len(x.b) ^ { 4 }) == 8) slice(x.b, 0, 4) else [x.b[0], x.b[1], 0, 0])) c],
     weights: [for (x in verts, c in (if ((len(x.b) ^ { 4 }) == 8) slice(x.b, 4, 8) else [x.b[2], x.b[3], 0.0, 0.0])) c]}
}
pub fn geometry(id, b) any^ => s.geometry(b.positions, {id: id, normals: b.normals, colors: b.colors, indices: b.indices,
    'skin-indices': b.skin, 'skin-weights': b.weights})
// a parametric surface sampled on a (rows + 1) × columns grid that wraps around its columns; the
// sampler returns {p, c} and normals come from neighbouring samples, so every surface shades smoothly.
// `flip` turns the normals for samplers that run clockwise.
pub fn surface(rows, columns, sampler, bind, flip = false) any^ {
    let grid = [for (r in 0 to rows, k in 0 to columns - 1) sampler(r / float(rows), k / float(columns))^]
    let at = (r, k) => grid[min(rows, max(0, r)) * columns + (k + columns) % columns].p
    let verts = [for (r in 0 to rows, k in 0 to columns - 1, let g = grid[r * columns + k],
        let du = sub(at(r, k + 1), at(r, k - 1))^, let dv = sub(at(r + 1, k), at(r - 1, k))^,
        let n0 = v.cross(du, dv), let norm = length(n0)^,
        let n = if (norm >= 0.0000001) unit(n0, norm)^ else pole_normal(at, r, k, rows, dv)^)
        {p: g.p, n: if (flip) scale(n, -1.0)^ else n, c: g.c, b: bind(g.p)^}]
    let tris = [for (r in 0 to rows - 1, k in 0 to columns - 1, q in quad(r * columns + k, r * columns + (k + 1) % columns, columns, flip)^) q]
    return part(verts, tris)^
}
// tube faces wind outward: around (a to b) crossed with along (a to a + segments) points inward, so run along first
fn band(a, b, segments) any^ => [a, a + segments, b, b, a + segments, b + segments]
// at a pole or a collapsed centre row the around-tangent vanishes: borrow the neighbouring row's
fn pole_normal(at, r, k, rows, dv) any^ {
    let near = if (r == 0) 1 else r - 1
    let n1 = v.cross(sub(at(near, k + 1), at(near, k - 1))^, dv)
    let first_norm = length(n1)^
    if (first_norm >= 0.0000001) { return unit(n1, first_norm)^ }
    let n2 = v.cross(sub(at(near, k + 1), at(near, k - 1))^, sub(at(near, k), at(r, k))^)
    let norm = length(n2)^
    return if (norm >= 0.0000001) unit(n2, norm)^ else [0.0, 1.0, 0.0]
}
// the winding faces the same way as the normal: (a, b, a + columns) runs du then dv
fn quad(a, b, columns, flip) any^ => if (flip) [a, a + columns, b, b, a + columns, b + columns] else [a, b, a + columns, b, b + columns, a + columns]
// a smooth ellipsoid centred at c with radii r, turned by q, coloured by colour(direction)
let BALL = shape.sphere(8, 12)
pub fn ellipsoid(c, r, q, colour, bind, ball = BALL) any^ {
    let count = (len(ball.positions) div 3)^
    let verts = [for (i in 0 to count - 1, let d = [ball.normals[i * 3], ball.normals[i * 3 + 1], ball.normals[i * 3 + 2]],
        let p = add(c, qrot(q, [d[0] * r[0], d[1] * r[1], d[2] * r[2]])^)^,
        let n = unit(qrot(q, [d[0] / r[0], d[1] / r[1], d[2] / r[2]])^)^) {p: p, n: n, c: colour(d)^, b: bind(p)^}]
    return part(verts, ball.indices)^
}
// a tapered tube through points with radii, capped, coloured by colour(k along 0..1, around angle)
pub fn tube(points, radii, segments, colour, bind) any^ {
    let n = len(points)
    let frames = [for (i in 0 to n - 1, let d = unit(sub(points[min(n - 1, i + 1)], points[max(0, i - 1)])^)^,
        let side = unit(v.cross(d, if (abs(d[1]) < 0.9) [0.0, 1.0, 0.0] else [1.0, 0.0, 0.0])^)^, let up = v.cross(side, d)) {d: d, s: side, u: up}]
    let ring = [for (i in 0 to n - 1, j in 0 to segments - 1, let a = j * 6.283185307179586 / segments, let f = frames[i],
        let nrm = add(scale(f.s, cos(a))^, scale(f.u, sin(a))^)^, let p = add(points[i], scale(nrm, radii[i])^)^)
        {p: p, n: nrm, c: colour(i / float(n - 1), a)^, b: bind(p)^}]
    let caps = [for (e in [0, n - 1], let dir = if (e == 0) scale(frames[0].d, -1.0)^ else frames[n - 1].d)
        {p: points[e], n: dir, c: colour(e / float(n - 1), 0.0)^, b: bind(points[e])^}]
    let base = n * segments
    let tris = [*[for (i in 0 to n - 2, j in 0 to segments - 1, q in band(i * segments + j, i * segments + (j + 1) % segments, segments)^) q],
        *[for (j in 0 to segments - 1, q in [base, j, (j + 1) % segments]) q],
        *[for (j in 0 to segments - 1, q in [base + 1, (n - 1) * segments + (j + 1) % segments, (n - 1) * segments + j]) q]]
    return part([*ring, *caps], tris)^
}
// one bone for every vertex
pub fn rigid(index) any^ => (p) => [index, index, 1.0, 0.0]
pub fn flat(rgb) any^ => (d) => rgb
pub fn mix(a, b, k) any^ => [for (i in 0 to 2) a[i] + (b[i] - a[i]) * max(0.0, min(1.0, k))]
// a deterministic hash in [0,1) for colour noise
pub fn hash(a, b) any^ => (let q = sin(a * 127.1 + b * 311.7) * 43758.5453, q - floor(q))
