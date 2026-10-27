// Benchmark (Lambda_Pkg_Proc_Gen.md §7): a 512x512 five-octave simplex field.
// Not a discovered test; run it through bench.py on a release build (AGENTS rule 10).
import rng: lambda.procgen.rng
import noise: lambda.procgen.noise

let gen = noise.make(rng.key(2026), {kind: 'simplex', octaves: 5, frequency: 1.0 / 160.0})^
let f = noise.field(gen, 512, 512)
sum([for (row in f) sum(row)])
