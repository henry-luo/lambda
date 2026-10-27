// Benchmark (Lambda_Pkg_Proc_Gen.md §7): about 20k Poisson points, their Delaunay
// triangulation and Voronoi cells. Not a discovered test; run it through bench.py
// on a release build (AGENTS rule 10).
import rng: lambda.procgen.rng
import sample: lambda.procgen.sample
import geom: lambda.procgen.geom

let pts = sample.poisson(rng.key(2026), {width: 340.0, height: 340.0, radius: 2.0})^
let cells = geom.voronoi(geom.delaunay(pts)^, {bounds: [0.0, 0.0, 340.0, 340.0]})^;
[len(pts), len(cells)]
