// Hand-worked API cases plus geometric invariants for the remaining PG4/PG5 families.
import rng: lambda.procgen.rng
import noise: lambda.procgen.noise
import field: lambda.procgen.field
import sample: lambda.procgen.sample
import adapt: lambda.procgen.adapt
import lsys: lambda.procgen.lsystem
import terrain: lambda.procgen.terrain

"field shaping and derivatives";
field.curve([[-1.0, 0.0, 0.25, 0.5, 1.0, 2.0]], [[0.0, 0.0], [0.5, 1.0], [1.0, 0.0]])^;
let ramp = field.make(4, 3, (x, y) => 2.0 * float(x) + 3.0 * float(y))
let gradient = field.gradient(ramp)^;
[all([for (row in gradient.dx) all([for (v in row) v == 2.0])]),
 all([for (row in gradient.dy) all([for (v in row) v == 3.0])]),
 all([for (row in field.slope(ramp)^) all([for (v in row) v == math.sqrt(13.0)])]),
 field.gradient([[7.0]])^ == {dx: [[0.0]], dy: [[0.0]]}];
"Euclidean distance transform";
field.distance_transform([[false, true, false], [false, false, false], [false, false, true]])^;
[field.distance_transform([[0, 0]])^ == [[inf, inf]], field.distance_transform([[1, 1]])^ == [[0.0, 0.0]]];
let mask = field.make(7, 5, (x, y) => (x == 1 and y == 1) or (x == 5 and y == 3))
let distances = field.distance_transform(mask)^;
all([for (y, row in distances) for (x, v in row)
    v == math.sqrt(float(min((x - 1) * (x - 1) + (y - 1) * (y - 1), (x - 5) * (x - 5) + (y - 3) * (y - 3))))]);
"contours reuse chart topology";
let paths = field.contours([[0.0, 1.0, 2.0], [0.0, 1.0, 2.0], [0.0, 1.0, 2.0]], [0.5])^;
[len(paths), paths[0].closed, paths[0].level,
 all([for (p in paths[0].points) p[0] == 0.5]), min(paths[0].points |> ~[1]), max(paths[0].points |> ~[1])];
"mesh and SVG adapters";
let mesh = adapt.heightfield([[0.0, 1.0], [2.0, 3.0]])^;
[mesh.positions, mesh.indices, mesh.uvs];
[adapt.svg([[[0.0, 0.0], [1.0, 1.0]]])^[0].points];
"periodic noise in two and three dimensions";
all([for (kind in ['simplex', 'perlin', 'value', 'white', 'cellular'])
    (let gen = noise.make(rng.key(4), {kind: kind, octaves: 3, period: [8.0, 6.0, 4.0], warp: {amplitude: 2.0, frequency: 0.03}})^,
     all([for (x in [-3.25, 0.0, 1.5]) noise.sample(gen, x, 2.25) == noise.sample(gen, x + 8.0, 8.25) and
        noise.sample3(gen, x, 2.25, 1.25) == noise.sample3(gen, x + 8.0, 8.25, 5.25)]))]);
"variable-radius Poisson";
let density = [[1.0, 1.0, 0.25, 0.25], [1.0, 1.0, 0.25, 0.25]]
let options = {width: 24.0, height: 12.0, radius: 2.0, max_radius: 4.0, density: density, attempts: 8}
let points = sample.poisson(rng.key(7), options)^
fn radius(p) => min(4.0, 2.0 / math.sqrt(field.sample(density, p[0] / 24.0 * 3.0, p[1] / 12.0)))
fn squared(a, b) => (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]);
[len(points) > 10, all([for (i, p in points) for (j, q in points where j > i)
    squared(p, q) >= max(radius(p), radius(q)) * max(radius(p), radius(q))]),
 points == sample.poisson(rng.key(7), options)^,
 sample.poisson(rng.key(7), {*: options, density: [[0.0]]})^ == []];
"segment widths";
lsys.turtle("F[FF]F", {width: 4.0, width_decay: 0.5})^.segments |> ~[5];
"climate and joined rivers";
terrain.temperature([[0.4, 0.4], [0.4, 0.4], [0.4, 0.4]], {lapse_rate: 0.0})^;
terrain.biomes([[0.5, 0.5]], [[0.7, 0.7]], {temperature: [[0.0, 1.0]]})^;
terrain.rivers([[3.0, 2.0, 1.0, 0.0]], {threshold: 1}).polylines;
"validation";
[field.curve(ramp, [[1.0, 0.0], [0.0, 1.0]]) ^ { "curve" },
 field.gradient(ramp, [0.0, 1.0]) ^ { "spacing" },
 adapt.heightfield([[1.0]]) ^ { "mesh" }, noise.make(rng.key(1), {period: [0.0, 1.0]}) ^ { "period" }]
