// Benchmark (Lambda_Pkg_Proc_Art.md §7): a 512x512 five-octave simplex field.
// Not a discovered test; run it through bench.py on a release build (AGENTS rule 10).
import rng: lambda.procart.rng
import noise: lambda.procart.noise

let gen = noise.make(rng.key(2026), {kind: 'simplex', octaves: 5, frequency: 1.0 / 160.0})^
let f = noise.field(gen, 512, 512)
sum([for (row in f) sum(row)])
