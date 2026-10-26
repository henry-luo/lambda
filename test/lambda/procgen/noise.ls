// procgen.noise: exact-tier kernels (Lambda_Pkg_Proc_Gen.md PG2, PG10). The section 1
// values were checked bit for bit against an independent Python port of each kernel.
import rng: lambda.procgen.rng
import noise: lambda.procgen.noise

let p = noise.table(rng.key(1))
let pts = [[0.3, 0.7, 1.1], [-5.25, 3.5, 0.125], [100.9, -42.1, 7.7], [255.5, 256.5, -0.5]]

"1 kernels: value2 perlin2 simplex2 value3 perlin3 simplex3 cellular2 cellular3";
[for (q in pts) [noise.value2(p, q[0], q[1]), noise.perlin2(p, q[0], q[1]), noise.simplex2(p, q[0], q[1]),
    noise.value3(p, q[0], q[1], q[2]), noise.perlin3(p, q[0], q[1], q[2]), noise.simplex3(p, q[0], q[1], q[2]),
    *noise.cellular2(p, q[0], q[1]), *noise.cellular3(p, q[0], q[1], q[2])]];

"2 table is a doubled permutation";
[len(p), sort(slice(p, 0, 256)) == [for (i in 0 to 255) i], slice(p, 0, 256) == slice(p, 256, 512)];

"3 ranges and means over 2000 points";
let xs = [for (i in 0 to 1999) [rng.between(rng.at(rng.key(5), 3 * i), -50.0, 50.0),
    rng.between(rng.at(rng.key(5), 3 * i + 1), -50.0, 50.0), rng.between(rng.at(rng.key(5), 3 * i + 2), -50.0, 50.0)]]
fn bounded(vs) => all([for (v in vs) v >= -1.0 and v <= 1.0]) and abs(math.mean(vs)) < 0.06
let kernels = [(x: float, y: float, z: float) => noise.value2(p, x, y), (x: float, y: float, z: float) => noise.perlin2(p, x, y), (x: float, y: float, z: float) => noise.simplex2(p, x, y),
    (x: float, y: float, z: float) => noise.value3(p, x, y, z), (x: float, y: float, z: float) => noise.perlin3(p, x, y, z), (x: float, y: float, z: float) => noise.simplex3(p, x, y, z),
    (x: float, y: float, z: float) => noise.white2(p, x, y), (x: float, y: float, z: float) => noise.white3(p, x, y, z)];
[for (f in kernels) bounded([for (q in xs) f(q[0], q[1], q[2])])];
[all([for (q in xs) (let c = noise.cellular2(p, q[0], q[1]), c[0] >= 0.0 and c[0] <= c[1])]),
 all([for (q in xs) (let c = noise.cellular3(p, q[0], q[1], q[2]), c[0] >= 0.0 and c[0] <= c[1])])];
[for (m in ['manhattan', 'chebyshev']) noise.cellular2(p, 0.3, 0.7, 0.5, m)];

"4 generators";
let spec = {kind: 'simplex', octaves: 4, frequency: 0.05}
let g = noise.make(rng.key(2), spec)^
let f = noise.field(g, 24, 16);
[len(f), len(f[0]), sum([for (row in f) sum(row)]), noise.field(noise.make(rng.key(2), spec)^, 24, 16) == f,
 noise.field(noise.make(rng.key(3), spec)^, 24, 16) != f];
[for (fr in ['none', 'fbm', 'ridged', 'billow']) (let gen = noise.make(rng.key(2), {*:spec, fractal: fr})^,
    let vs = [for (row in noise.field(gen, 24, 16)) for (v in row) v], [fr, gen.octaves, all([for (v in vs) v >= -1.0 and v <= 1.0])])];
[for (kind in ['simplex', 'perlin', 'value', 'white', 'cellular']) (let gen = noise.make(rng.key(2), {kind: kind, frequency: 0.1})^,
    [kind, noise.sample(gen, 3.0, 4.0), noise.sample3(gen, 3.0, 4.0, 5.0)])];
[for (cell in ['f1', 'f2', 'f2-f1']) noise.sample(noise.make(rng.key(2), {kind: 'cellular', cell: cell, frequency: 0.1})^, 3.0, 4.0)];
let warped = noise.make(rng.key(2), {*:spec, warp: {amplitude: 6.0, frequency: 0.03}})^;
[noise.field(warped, 24, 16) != f, noise.sample3(warped, 3.0, 4.0, 5.0)];
let plain = noise.make(rng.key(2))^;
[plain.kind, plain.fractal, plain.octaves, plain.frequencies];

"5 rejected specs";
[noise.make(rng.key(2), {kind: 'bogus'}) ^ { "kind" }, noise.make(rng.key(2), {fractal: 'x'}) ^ { "fractal" },
 noise.make(rng.key(2), {octaves: 0}) ^ { "octaves" }, noise.make(rng.key(2), {octaves: 2.5}) ^ { "octaves" }]
