// Benchmark (Lambda_Pkg_Proc_Gen.md §7): 50k droplets of hydraulic erosion on a 256x256
// island. Not a discovered test; run it through bench.py on a release build (AGENTS rule 10).
import rng: lambda.procgen.rng
import field: lambda.procgen.field
import terrain: lambda.procgen.terrain

let land = terrain.heightmap(rng.key(2026), {width: 256, height: 256, island: 0.5})^
let worn = terrain.hydraulic(rng.key(7), land, {droplets: 50000})^;
field.bounds(worn)
