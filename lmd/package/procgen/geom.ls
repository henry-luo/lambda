// Planar geometry (Lambda_Pkg_Proc_Gen.md §4 procgen.geom): Delaunay
// triangulation, convex hull, Voronoi cells and Lloyd relaxation. Points are
// [x, y]. Combinatorial decisions use the exact predicates math.orient2d and
// math.incircle, so degenerate input (collinear, cocircular, duplicate points)
// never breaks the triangulation; constructions use basic arithmetic only.

fn finite_point(p) bool => p is array and len(p) == 2 and p[0] is number and p[1] is number and
    abs(float(p[0])) < 1e300 and abs(float(p[1])) < 1e300

// the first index of each distinct point, in input order.
fn distinct(points) array {
    let order = sort([for (i, p in points) [float(p[0]), float(p[1]), i]], (e) => e);
    sort([for (j, e in order where j == 0 or e[0] != order[j - 1][0] or e[1] != order[j - 1][1]) e[2]])
}

// --- Bowyer-Watson insertion over ghost triangles ---
// A triangle is [a, b, c], counterclockwise. A ghost [u, v, -1] stands for the
// region outside hull edge v->u: it holds the open half-plane left of u->v
// plus the open segment uv, so the hull needs no super-triangle (Shewchuk).
fn between(a, b, p) bool =>
    min(a[0], b[0]) <= p[0] and p[0] <= max(a[0], b[0]) and min(a[1], b[1]) <= p[1] and p[1] <= max(a[1], b[1])

fn conflicts(points, t, p) bool {
    if (t[2] == -1) (let o = math.orient2d(points[t[0]], points[t[1]], p),
        o > 0 or (o == 0 and between(points[t[0]], points[t[1]], p)))
    else math.incircle(points[t[0]], points[t[1]], points[t[2]], p) > 0
}

// a cavity edge i->j joined to the new point; a ghost keeps -1 last.
fn joined(i: int, j: int, p: int) array => if (i == -1) [j, p, -1] else if (j == -1) [p, i, -1] else [i, j, p]

fn insert(points, tris, pi: int) array {
    let p = points[pi]
    let flags = [for (t in tris) conflicts(points, t, p)]
    let bad = [for (i, t in tris where flags[i]) t]
    let edges = [for (t in bad) for (s in 0 to 2) [t[s], t[(s + 1) % 3]]]
    let keys = [for (e in edges) string(e[0]) ++ "," ++ string(e[1])];
    [*[for (i, t in tris where not flags[i]) t],
     *[for (e in edges where not ((string(e[1]) ++ "," ++ string(e[0])) in keys)) joined(e[0], e[1], pi)]]
}

fn edge_key(a: int, b: int) string => string(a) ++ "," ++ string(b)

fn hull_from(ghosts) array {
    // ghost [u, v, -1] covers outside edge v->u, so the counterclockwise hull steps v -> u
    let next = map([for (g in ghosts) for (x in [string(g[1]), g[0]]) x]);
    if (len(ghosts) == 0) []
    else reduce([[ghosts[0][1]], *[for (i in 2 to len(ghosts)) i]], (hull, i) => [*hull, next[string(hull[len(hull) - 1])]])
}

fn mesh_of(points, triangles, hull) map {
    let index = map([for (t, tri in triangles) for (s in 0 to 2) for (x in [edge_key(tri[s], tri[(s + 1) % 3]), 3 * t + s]) x]);
    {points: points, triangles: triangles, hull: hull,
     halfedges: [for (tri in triangles) for (s in 0 to 2) (let h = index[edge_key(tri[(s + 1) % 3], tri[s])], if (h == null) -1 else h)]}
}

// the extreme points of a collinear (or tiny) set, in input-order-independent form.
fn line_hull(points, ids) array {
    let order = sort([for (i in ids) [float(points[i][0]), float(points[i][1]), i]], (e) => e);
    if (len(order) == 0) [] else if (len(order) == 1) [order[0][2]] else [order[0][2], order[len(order) - 1][2]]
}

fn delaunay_with(points, use_native: bool) map^ {
    if (not (points is array)) raise error("procgen.geom.delaunay: points must be an array of [x, y]")
    else if (len(points) > 20000) raise error("procgen.geom.delaunay: more than 20000 points")
    else if (not all([for (p in points) finite_point(p)])) raise error("procgen.geom.delaunay: every point must be a finite [x, y]")
    else {
        let ids = distinct(points)
        let a0 = if (len(ids) > 0) ids[0] else -1
        let b0 = if (len(ids) > 1) ids[1] else -1
        let third = [for (c in ids where b0 >= 0 and math.orient2d(points[a0], points[b0], points[c]) != 0) c];
        if (len(third) == 0) mesh_of(points, [], line_hull(points, ids))
        else {
            let c = third[0]
            let ccw = math.orient2d(points[a0], points[b0], points[c]) > 0
            let a = if (ccw) a0 else b0
            let b = if (ccw) b0 else a0
            let seed = [[a, b, c], [b, a, -1], [c, b, -1], [a, c, -1]]
            let tris = if (use_native) (let flat = math.delaunay(points, ids, [a, b, c])^,
                [for (i in 0 to (len(flat) div 3 - 1)) slice(flat, i * 3, i * 3 + 3)])
                else reduce([seed, *[for (i in ids where i != a and i != b and i != c) i]],
                    (acc, i: int) => insert(points, acc, i));
            mesh_of(points, [for (t in tris where t[2] != -1) t], hull_from([for (t in tris where t[2] == -1) t]))
        }
    }
}

// the native cavity walker preserves creation order; keep the full-scan oracle (PG7.3).
pub fn delaunay(points) map^ => delaunay_with(points, math.delaunay is fn)^
pub fn delaunay_reference(points) map^ => delaunay_with(points, false)^

// --- navigation (Delaunator half-edge layout: edge 3t + s runs from triangle t's vertex s) ---
pub fn next_edge(e: int) int => if (e % 3 == 2) e - 2 else e + 1
pub fn prev_edge(e: int) int => if (e % 3 == 0) e + 2 else e - 1

// each point's neighbours, ascending; a mesh without triangles links consecutive collinear points.
pub fn neighbors(mesh) array {
    let n = len(mesh.points)
    let pairs = if (len(mesh.triangles) > 0) [for (t in mesh.triangles) for (s in 0 to 2) for (pr in [[t[s], t[(s + 1) % 3]], [t[(s + 1) % 3], t[s]]]) pr]
        else (let line = sort([for (i in distinct(mesh.points)) [float(mesh.points[i][0]), float(mesh.points[i][1]), i]], (e) => e),
            [for (j in 1 to (len(line) - 1)) for (pr in [[line[j - 1][2], line[j][2]], [line[j][2], line[j - 1][2]]]) pr])
    let groups = map([for (pr in pairs group by pr[0] as v into g) for (x in [string(v), sort(unique(g |> ~[1]))]) x]);
    [for (i in 0 to (n - 1)) (let g = groups[string(i)], if (g == null) [] else g)]
}

// --- convex hull: Andrew's monotone chain on exact orientation ---
fn chain(points, ids) array => reduce([[], *ids], (kept, i) => [*pop_right(points, kept, i), i])

fn pop_right(points, kept, i) array {
    let n = len(kept);
    if (n >= 2 and math.orient2d(points[kept[n - 2]], points[kept[n - 1]], points[i]) <= 0)
        pop_right(points, slice(kept, 0, n - 1), i)
    else kept
}

// counterclockwise hull indices starting from the lowest-x point; collinear points are dropped.
pub fn hull(points) array^ {
    if (not all([for (p in points) finite_point(p)])) raise error("procgen.geom.hull: every point must be a finite [x, y]")
    else {
        let ids = [for (e in sort([for (i in distinct(points)) [float(points[i][0]), float(points[i][1]), i]], (e) => e)) e[2]];
        if (len(ids) < 3) ids
        else (let low = chain(points, ids), let high = chain(points, reverse(ids)),
            if (len(low) == 2 and len(high) == 2 and low[0] == high[1]) [low[0], low[1]]
            else [*slice(low, 0, len(low) - 1), *slice(high, 0, len(high) - 1)])
    }
}

// --- Voronoi cells: each cell is the bounds clipped by the bisectors with its Delaunay neighbours ---
fn crossing(a, b, fa: float, fb: float) array => (let t = fa / (fa - fb), [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t])

// Sutherland-Hodgman against the half-plane nearer p than q.
fn clip_half(poly, p, q) array {
    let dx = q[0] - p[0]
    let dy = q[1] - p[1]
    let mx = (p[0] + q[0]) * 0.5
    let my = (p[1] + q[1]) * 0.5
    let f = [for (v in poly) (v[0] - mx) * dx + (v[1] - my) * dy];
    let pieces = [for (i, a in poly) (let j = (i + 1) % len(poly), let fa = f[i], let fb = f[j],
        if (fa <= 0.0 and fb <= 0.0) [a]
        else if (fa <= 0.0) (if (fa == 0.0) [a] else [a, crossing(a, poly[j], fa, fb)])
        else if (fb < 0.0) [crossing(a, poly[j], fa, fb)]
        else [])];
    [for (piece in pieces) for (v in piece) v]
}

fn bounds_of(points, ids) array {
    let xs = [for (i in ids) float(points[i][0])]
    let ys = [for (i in ids) float(points[i][1])];
    [min(xs), min(ys), max(xs), max(ys)]
}

// cells[i] is a counterclockwise polygon, or [] for a repeated point. bounds = [x0, y0, x1, y1].
pub fn voronoi(mesh, options = {}) array^ {
    let ids = distinct(mesh.points)
    let b = if (options.bounds == null) bounds_of(mesh.points, ids) else options.bounds
    let box = [[b[0], b[1]], [b[2], b[1]], [b[2], b[3]], [b[0], b[3]]]
    let near = neighbors(mesh)
    let used = map([for (i in ids) for (x in [string(i), true]) x]);
    if (b[2] <= b[0] or b[3] <= b[1]) raise error("procgen.geom.voronoi: bounds must have positive extent")
    else [for (i, p in mesh.points) if (used[string(i)] == null) []
        else reduce([box, *near[i]], (poly, j) => if (len(poly) == 0) poly else clip_half(poly, p, mesh.points[j]))]
}

// area-weighted centroid by the shoelace sums.
pub fn centroid(poly) array {
    let n = len(poly)
    let cross = [for (i, a in poly) (let b = poly[(i + 1) % n], a[0] * b[1] - b[0] * a[1])]
    let area = sum(cross);
    if (area == 0.0) poly[0]
    else [sum([for (i, a in poly) (a[0] + poly[(i + 1) % n][0]) * cross[i]]) / (3.0 * area),
          sum([for (i, a in poly) (a[1] + poly[(i + 1) % n][1]) * cross[i]]) / (3.0 * area)]
}

// Lloyd relaxation: move each point to its cell's centroid, `iterations` times (PG2 tolerance tier).
pub fn lloyd(points, options = {}) array^ {
    let iterations = if (options.iterations == null) 1 else options.iterations;
    if (not (iterations is int) or iterations < 0 or iterations > 100) raise error("procgen.geom.lloyd: iterations must be an int in 0..100")
    else reduce([points, *[for (i in 1 to iterations) i]], (pts, i) => (let cells = voronoi(delaunay(pts)^, options)^,
        [for (j, c in cells) if (len(c) >= 3) centroid(c) else pts[j]]))
}
