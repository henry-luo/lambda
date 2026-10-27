// PG7.3: topology, triangle order, hull start and halfedges must all match.
import rng: lambda.procgen.rng
import geom: lambda.procgen.geom
fn same(points) bool => geom.delaunay(points)^ == geom.delaunay_reference(points)^

"native Delaunay";
math.delaunay is fn;
all([for (points in [[], [[0.0, 0.0]], [[0.0, 0.0], [0.0, 0.0]],
    [[0.0, 0.0], [2.0, 2.0], [1.0, 1.0]],
    [[0.0, 0.0], [1.0, 0.0], [2.0, 0.0], [3.0, 0.0], [1.5, 1.0], [1.5, -1.0]]]) same(points)]);
let grid = [for (y in 0 to 6) for (x in 0 to 6) [float(x), float(y)]];
all([for (seed in 0 to 7) same(rng.shuffle(rng.key(seed), grid))]);
same([*grid, [3.0, 3.0], [0.0, 0.0], [6.0, 6.0]]);
let ring = [[5.0, 0.0], [3.0, 4.0], [0.0, 5.0], [-3.0, 4.0], [-5.0, 0.0], [-4.0, -3.0], [0.0, -5.0], [4.0, -3.0], [0.0, 0.0]];
all([for (scale in [1e-40, 1.0, 1e80]) for (seed in 0 to 2) same(rng.shuffle(rng.key(seed), ring * scale))]);
all([for (seed in 0 to 15) (let k = rng.key(seed), same([for (i in 0 to 39)
    [rng.unit(rng.at(k, i * 2)), rng.unit(rng.at(k, i * 2 + 1))]]))]);
"boxed boundary";
math.delaunay([], [], [])^;
[math.delaunay([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]], [0, 1, 1], [0, 1, 2]) ^ { "indices" },
 math.delaunay([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]], [0, 1, 2], [0, 2, 1]) ^ { "orientation" },
 math.delaunay([[0.0, 0.0], [1.0, 0.0], [2.0, 0.0]], [0, 1, 2], [0, 1, 2]) ^ { "collinear" }]
