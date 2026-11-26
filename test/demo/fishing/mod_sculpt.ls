// Implicit-surface sculpting for organic characters: round cones and ellipsoids blended with a smooth
// minimum (or carved with a smooth maximum) into one signed distance field, then polygonised with
// surface nets. Every part flows into its neighbours with no seams, and the caller paints each vertex
// from the distances to the parts nearby (skin weights and colour).
//
// The grid is split into 4 × 4 × 4-point bricks. Each brick keeps only the parts that can reach it,
// and a brick whose centre is far from the surface is filled from that one sample, so the field is
// evaluated in full only in a thin shell around the surface.
import rig: .mod_rig

// a part: a round cone from a to b with radii ra to rb, squashed by s along the axes (an ellipsoid is
// a cone of radius 1 squashed by its radii), blended in over k; op 1 adds, -1 carves
pub fn cone(a, b, ra, rb, k, s = [1.0, 1.0, 1.0], op = 1.0) => {a: a, b: b, ra: ra, rb: rb, k: k, s: s, op: op}
pub fn ellipsoid(c, radii, k, op = 1.0) => cone(c, c, 1.0, 1.0, k, radii, op)

// ---- the field over packed parts: 15 floats each
// [ax, ay, az, dx, dy, dz, d·d, ra, rb, sx, sy, sz, min s, k, op] with d = (b - a) / s
let STRIDE = 15
fn pack_one(q) any^ => (let d = [for (i in 0 to 2) (q.b[i] - q.a[i]) / q.s[i]],
    [q.a[0] * 1.0, q.a[1] * 1.0, q.a[2] * 1.0, d[0] * 1.0, d[1] * 1.0, d[2] * 1.0, (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 1.0,
     q.ra * 1.0, q.rb * 1.0, q.s[0] * 1.0, q.s[1] * 1.0, q.s[2] * 1.0, min(q.s[0], min(q.s[1], q.s[2])) * 1.0, q.k * 1.0, q.op * 1.0])
fn pack(parts, subset) any^ => [for (i in subset, c in pack_one(parts[i])^) c]
// distance to one part (an approximation for squashed parts, close enough to blend)
fn part_distance(pr: float[], i: int, x: float, y: float, z: float) float^ {
    let o = i * STRIDE
    let qx = (x - pr[o]) / pr[o + 9], qy = (y - pr[o + 1]) / pr[o + 10], qz = (z - pr[o + 2]) / pr[o + 11]
    let dx = pr[o + 3], dy = pr[o + 4], dz = pr[o + 5], dd = pr[o + 6]
    let t = if (dd > 0.0000000001) max(0.0, min(1.0, (qx * dx + qy * dy + qz * dz) / dd)) else 0.0
    let ex = qx - dx * t, ey = qy - dy * t, ez = qz - dz * t
    return (sqrt(ex * ex + ey * ey + ez * ez) - (pr[o + 7] + (pr[o + 8] - pr[o + 7]) * t)) * pr[o + 12]
}
fn smin(a: float, b: float, k: float) float^ => (let h = max(k - abs(a - b), 0.0) / k, min(a, b) - h * h * k * 0.25)
fn blend(pr: float[], i: int, acc: float, d: float) float^ =>
    if (pr[i * STRIDE + 14] > 0.0) smin(acc, d, pr[i * STRIDE + 13])^ else 0.0 - smin(0.0 - acc, d, pr[i * STRIDE + 13])^
fn fold(pr: float[], j: int, n: int, acc: float, x: float, y: float, z: float) float^ =>
    if (j >= n) acc else fold(pr, j + 1, n, blend(pr, j, acc, part_distance(pr, j, x, y, z)^)^, x, y, z)^
fn field(pr: float[], x: float, y: float, z: float) float^ => fold(pr, 0, len(pr) div STRIDE, 1.0, x, y, z)^
fn field_at(pr, p) any^ => field(pr, p[0], p[1], p[2])^

// ---- bounds: each part's box, grown by its blend radius
fn reach(q) any^ => max(q.ra, q.rb) * max(q.s[0], max(q.s[1], q.s[2]))
fn part_box(q, grow) any^ => (let r = reach(q)^ + q.k + grow,
    [[for (i in 0 to 2) min(q.a[i], q.b[i]) - r], [for (i in 0 to 2) max(q.a[i], q.b[i]) + r]])
fn overlaps(a, b) any^ => a[0][0] <= b[1][0] and a[1][0] >= b[0][0] and a[0][1] <= b[1][1] and a[1][1] >= b[0][1] and a[0][2] <= b[1][2] and a[1][2] >= b[0][2]

// ---- surface nets
// cell corners numbered dx * 4 + dy * 2 + dz, and the twelve cell edges as corner pairs
let CORNERS = [for (dx in 0 to 1, dy in 0 to 1, dz in 0 to 1) [dx, dy, dz]]
let EDGES = [[0, 4], [1, 5], [2, 6], [3, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 1], [2, 3], [4, 5], [6, 7]]
// the four cells around a grid edge leaving point (i, j, k) along each axis, counter-clockwise seen
// from the edge's far end
let AROUND = [[[0, 0, 0], [0, -1, 0], [0, -1, -1], [0, 0, -1]], [[0, 0, 0], [0, 0, -1], [-1, 0, -1], [-1, 0, 0]],
    [[0, 0, 0], [-1, 0, 0], [-1, -1, 0], [0, -1, 0]]]
let AXES = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
fn local(a, b, c) => (a * 4 + b) * 4 + c
fn padded(a, b, c) => (a * 5 + b) * 5 + c

// the shape's field sampled brick by brick; `paint(p, n, near)` gives each vertex its colour and bone
// blend from [[part index, distance], ...] for the parts around it, and returns a rig part
pub fn sculpt(parts, cell, paint) any^ {
    let boxes = [for (q in parts) part_box(q, 0.0)^]
    let lo = [for (i in 0 to 2) min([for (b in boxes) b[0][i]]) - 2.0 * cell]
    let hi = [for (i in 0 to 2) max([for (b in boxes) b[1][i]]) + 2.0 * cell]
    let nb = [for (i in 0 to 2) int(ceil((hi[i] - lo[i]) / (4.0 * cell)))^]
    let grid = {lo: lo, cell: cell, nb: nb, n: [nb[0] * 4, nb[1] * 4, nb[2] * 4]}
    let reaches = [for (q in parts) part_box(q, 2.0 * cell)^]
    let bricks = [for (bx in 0 to nb[0] - 1, by in 0 to nb[1] - 1, bz in 0 to nb[2] - 1) brick(grid, parts, reaches, bx, by, bz)^]
    let cells = [for (br in bricks) if (br.near) brick_cells(grid, bricks, br)^ else null]
    let offsets = math.cumsum([0, *[for (c in cells) if (c == null) 0 else len(c.verts)]])
    let verts = [for (bi, c in cells, v in (if (c == null) [] else c.verts)) finish(bricks[bi], v, cell, paint)^]
    let tris = [for (br in bricks, t in (if (br.near) brick_faces(grid, bricks, cells, offsets, br)^ else [])) t]
    return rig.part(verts, tris)^
}
fn brick_index(grid, bx, by, bz) => (bx * grid.nb[1] + by) * grid.nb[2] + bz
fn point(grid, i, j, k) => [grid.lo[0] + i * grid.cell, grid.lo[1] + j * grid.cell, grid.lo[2] + k * grid.cell]
// a brick keeps the parts that can reach it; it is near the surface when its centre is closer than
// its half-diagonal plus two cells, and only then are its 64 points evaluated
fn brick(grid, parts, reaches, bx, by, bz) any^ {
    let b0 = point(grid, bx * 4, by * 4, bz * 4), b1 = point(grid, bx * 4 + 3, by * 4 + 3, bz * 4 + 3)
    let subset = [for (i, r in reaches where overlaps(r, [b0, b1])^) i]
    if (len(subset) == 0) { return {b: [bx, by, bz], near: false, d: 1.0} }
    let pr = pack(parts, subset)^
    let d = field_at(pr, rig.lerp3(b0, b1, 0.5)^)^
    if (abs(d) > grid.cell * (1.5 * 1.7320508 + 2.0)) { return {b: [bx, by, bz], near: false, d: d} }
    return {b: [bx, by, bz], near: true, d: d, subset: subset, pr: pr,
        vals: [for (a in 0 to 3, b in 0 to 3, c in 0 to 3) field_at(pr, point(grid, bx * 4 + a, by * 4 + b, bz * 4 + c))^]}
}
// the field at any grid point: a near brick's sample, else its brick's centre value
fn value(grid, bricks, i, j, k) any^ {
    if (i >= grid.n[0] or j >= grid.n[1] or k >= grid.n[2] or i < 0 or j < 0 or k < 0) { return 1.0 }
    let br = bricks[brick_index(grid, i div 4, j div 4, k div 4)]
    return if (br.near) br.vals[local(i % 4, j % 4, k % 4)] else br.d
}
// the brick's 5 × 5 × 5 points: its own 64 and the next bricks' first layer
fn block(grid, bricks, br) any^ => [for (a in 0 to 4, b in 0 to 4, c in 0 to 4)
    if (a < 4 and b < 4 and c < 4) br.vals[local(a, b, c)] else value(grid, bricks, br.b[0] * 4 + a, br.b[1] * 4 + b, br.b[2] * 4 + c)^]
// one vertex per cell the surface crosses, at the mean of its edge crossings
fn brick_cells(grid, bricks, br) any^ {
    let vals = block(grid, bricks, br)^
    let found = [for (a in 0 to 3, b in 0 to 3, c in 0 to 3) cell_vertex(grid, vals, br, a, b, c)^]
    let flags = [for (v in found) if (v == null) 0 else 1]
    let running = math.cumsum(flags);
    {verts: [for (v in found where v != null) v], loc: [for (i, f in flags) if (f == 0) -1 else running[i] - 1], vals: vals}
}
fn cell_vertex(grid, vals, br, a, b, c) any^ {
    let v = [for (q in CORNERS) vals[padded(a + q[0], b + q[1], c + q[2])]]
    let inside = len([for (x in v where x < 0.0) x])
    if (inside == 0 or inside == 8) { return null }
    let base = point(grid, br.b[0] * 4 + a, br.b[1] * 4 + b, br.b[2] * 4 + c)
    let hits = [for (e in EDGES where (v[e[0]] < 0.0) != (v[e[1]] < 0.0), let t = v[e[0]] / (v[e[0]] - v[e[1]]),
        let p = CORNERS[e[0]], let q = CORNERS[e[1]]) [for (i in 0 to 2) base[i] + (p[i] + (q[i] - p[i]) * t) * grid.cell]]
    return [for (i in 0 to 2) sum([for (h in hits) h[i]]) / len(hits)]
}
// the global vertex of cell (i, j, k), or -1
fn vertex_id(grid, bricks, cells, offsets, i, j, k) any^ {
    if (i < 0 or j < 0 or k < 0) { return -1 }
    let bi = brick_index(grid, i div 4, j div 4, k div 4)
    let c = cells[bi]
    if (c == null) { return -1 }
    let at = c.loc[local(i % 4, j % 4, k % 4)]
    return if (at < 0) -1 else offsets[bi] + at
}
// a quad (two triangles) around every grid edge the surface crosses, wound to face outward
fn brick_faces(grid, bricks, cells, offsets, br) any^ {
    let vals = cells[brick_index(grid, br.b[0], br.b[1], br.b[2])].vals
    let crossings = [for (a in 0 to 3, b in 0 to 3, c in 0 to 3, axis in 0 to 2, let d = AXES[axis],
        let v0 = vals[padded(a, b, c)], let v1 = vals[padded(a + d[0], b + d[1], c + d[2])] where (v0 < 0.0) != (v1 < 0.0))
        [br.b[0] * 4 + a, br.b[1] * 4 + b, br.b[2] * 4 + c, axis, v0 < 0.0]];
    [for (e in crossings, t in edge_quad(grid, bricks, cells, offsets, e[0], e[1], e[2], e[3], e[4])^) t]
}
fn edge_quad(grid, bricks, cells, offsets, i, j, k, axis, outward) any^ {
    let ids = [for (o in AROUND[axis]) vertex_id(grid, bricks, cells, offsets, i + o[0], j + o[1], k + o[2])^]
    if (len([for (x in ids where x < 0) x]) > 0) { return [] }
    return if (outward) [ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]] else [ids[0], ids[2], ids[1], ids[0], ids[3], ids[2]]
}
// pull a vertex onto the surface along the field's gradient, which is also its normal, and paint it
fn finish(br, p, cell, paint) any^ {
    let e = cell * 0.25
    let g = [for (i in 0 to 2) field_at(br.pr, [for (j in 0 to 2) p[j] + (if (i == j) e else 0.0)])^ - field_at(br.pr, [for (j in 0 to 2) p[j] - (if (i == j) e else 0.0)])^]
    let n = rig.unit(g)^
    let d = max(0.0 - cell, min(cell, field_at(br.pr, p)^))
    let q = [for (i in 0 to 2) p[i] - n[i] * d]
    let near = [for (j, i in br.subset) [i, part_distance(br.pr, j, q[0], q[1], q[2])^]]
    return paint(q, n, near)^
}
// where a ray from inside the shape leaves it: the first sign change along it, refined by bisection
// (for seating eyes and whiskers on the sculpted surface)
pub fn surface_point(parts, from, dir, far) any^ {
    let pr = pack(parts, [for (i in 0 to len(parts) - 1) i])^
    let steps = [for (i in 0 to 40) far * i / 40.0]
    let out = [for (t in steps where field_at(pr, [for (j in 0 to 2) from[j] + dir[j] * t])^ >= 0.0) t]
    let t1 = if (len(out) > 0) out[0] else far
    return rig.add(from, rig.scale(dir, bisect(pr, from, dir, max(0.0, t1 - far / 40.0), t1, 12)^)^)^
}
fn bisect(pr, from, dir, a, b, n) any^ {
    if (n == 0) { return (a + b) / 2.0 }
    let m = (a + b) / 2.0
    let inside = field_at(pr, [for (j in 0 to 2) from[j] + dir[j] * m])^ < 0.0
    return if (inside) bisect(pr, from, dir, m, b, n - 1)^ else bisect(pr, from, dir, a, m, n - 1)^
}
