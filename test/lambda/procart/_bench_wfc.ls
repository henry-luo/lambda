// Benchmark (Lambda_Pkg_Proc_Art.md §7): a 64x64 overlapping model (n = 3, all 8
// symmetries) trained on a small maze. Not a discovered test; run it through bench.py
// on a release build (AGENTS rule 10).
import rng: lambda.procart.rng
import grid: lambda.procart.grid
import wfc: lambda.procart.wfc

let model = wfc.overlapping(grid.maze(rng.key(1), {width: 6, height: 6})^, {n: 3, symmetry: 8})^
let out = wfc.run(rng.key(2026), model, {width: 64, height: 64, periodic: true})^;
[model.count, len(out), len(out[0])]
