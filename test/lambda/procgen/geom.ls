// procgen.geom: Delaunay, hull, Voronoi, Lloyd (Lambda_Pkg_Proc_Gen.md §4, P1 acceptance).
// Section 1 is hand-checkable; the rest checks the Delaunay invariants with the exact
// predicates on degenerate input: cocircular grids and rings, collinear runs, duplicates.
import rng: lambda.procgen.rng
import sample: lambda.procgen.sample
import geom: lambda.procgen.geom

// ccw triangles, empty circumcircles, symmetric half-edges and t = 2n - 2 - h over the used points
fn check(points) {
    let m = geom.delaunay(points)^
    let t = m.triangles
    let used = len(unique([for (tri in t) for (v in tri) v]))
    let boundary = len([for (h in m.halfedges where h == -1) h]);
    [len(points), used, len(t), len(m.hull),
     all([for (tri in t) math.orient2d(points[tri[0]], points[tri[1]], points[tri[2]]) > 0]),
     all([for (tri in t) all([for (p in points) math.incircle(points[tri[0]], points[tri[1]], points[tri[2]], p) <= 0])]),
     all([for (e, h in m.halfedges) h == -1 or m.halfedges[h] == e]),
     len(t) == 2 * used - 2 - boundary]
}

fn area(poly) => (let n = len(poly), sum([for (i, a in poly) (let b = poly[(i + 1) % n], a[0] * b[1] - b[0] * a[1])]) / 2.0)
fn holds(poly, p) => all([for (i, a in poly) (let b = poly[(i + 1) % len(poly)], (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) >= 0.0)])

"1 a square with its centre";
let sq = geom.delaunay([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0], [0.5, 0.5]])^;
[sq.triangles, sq.halfedges, sq.hull];
geom.neighbors(sq);
geom.voronoi(sq)^;
[geom.next_edge(4), geom.next_edge(5), geom.prev_edge(3), geom.prev_edge(5)];

"2 degenerate inputs: n used triangles hull ccw empty symmetric euler";
let grid = [for (y in 0 to 6) for (x in 0 to 6) [float(x), float(y)]];
check(grid);
check([*grid, [3.0, 3.0], [0.0, 0.0], [6.0, 6.0]]);
check([[5.0, 0.0], [3.0, 4.0], [0.0, 5.0], [-3.0, 4.0], [-5.0, 0.0], [-4.0, -3.0], [0.0, -5.0], [4.0, -3.0], [0.0, 0.0]]);
check([[0.0, 0.0], [1.0, 0.0], [2.0, 0.0], [3.0, 0.0], [1.5, 1.0], [1.5, -1.0]]);
check(sample.poisson(rng.key(3), {width: 60.0, height: 40.0, radius: 4.0})^);

"3 meshes without triangles";
[for (pts in [[], [[1.0, 1.0]], [[1.0, 1.0], [1.0, 1.0]], [[0.0, 0.0], [2.0, 2.0], [1.0, 1.0]]]) (let m = geom.delaunay(pts)^, [m.triangles, m.hull])];
geom.neighbors(geom.delaunay([[0.0, 0.0], [2.0, 2.0], [1.0, 1.0]])^);
geom.voronoi(geom.delaunay([[0.0, 0.0], [2.0, 0.0], [1.0, 0.0]])^, {bounds: [0.0, -1.0, 2.0, 1.0]})^;

"4 convex hull";
geom.hull([[0.0, 0.0], [2.0, 0.0], [1.0, 1.0], [2.0, 2.0], [0.0, 2.0], [1.0, 0.0], [0.0, 2.0]])^;
[geom.hull([[1.0, 1.0], [0.0, 0.0], [2.0, 2.0]])^, geom.hull([[3.0, 3.0]])^];

"5 Voronoi cells tile the bounds";
let pts = sample.poisson(rng.key(4), {width: 50.0, height: 30.0, radius: 5.0})^
let cells = geom.voronoi(geom.delaunay(pts)^, {bounds: [0.0, 0.0, 50.0, 30.0]})^;
[len(cells) == len(pts), all([for (i, c in cells) len(c) >= 3 and holds(c, pts[i])]), abs(sum([for (c in cells) area(c)]) - 1500.0) < 1e-6];
geom.voronoi(geom.delaunay([[0.0, 0.0], [0.0, 0.0], [1.0, 0.0], [0.0, 1.0]])^, {bounds: [-1.0, -1.0, 2.0, 2.0]})^;

"6 centroid and Lloyd";
[geom.centroid([[0.0, 0.0], [2.0, 0.0], [2.0, 2.0], [0.0, 2.0]]), geom.centroid([[0.0, 0.0], [3.0, 0.0], [0.0, 3.0]])];
let relaxed = geom.lloyd(pts, {iterations: 3, bounds: [0.0, 0.0, 50.0, 30.0]})^
// relaxation evens out the cells: their area variance falls
fn area_variance(ps) => math.variance([for (c in geom.voronoi(geom.delaunay(ps)^, {bounds: [0.0, 0.0, 50.0, 30.0]})^) area(c)]);
[len(relaxed) == len(pts), all([for (p in relaxed) p[0] >= 0.0 and p[0] <= 50.0 and p[1] >= 0.0 and p[1] <= 30.0]),
 area_variance(relaxed) < area_variance(pts) * 0.5];
geom.lloyd(pts, {iterations: 0})^ == pts;

"7 rejected input";
[geom.delaunay([[0.0, 0.0], [1.0]]) ^ { "shape" }, geom.delaunay("x") ^ { "array" }, geom.hull([[0.0, 1e309]]) ^ { "finite" },
 geom.voronoi(geom.delaunay([[0.0, 0.0], [1.0, 1.0], [0.0, 1.0]])^, {bounds: [1.0, 0.0, 0.0, 1.0]}) ^ { "bounds" },
 geom.lloyd(pts, {iterations: -1}) ^ { "iterations" }]
