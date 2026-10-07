# Julia microbenchmark suite

Four cross-language workloads inspired by
[Julia Microbenchmarks](https://github.com/JuliaLang/Microbenchmarks/blob/master/julia/perf.jl).
These are matched scalar adaptations, so their times cannot be substituted for
the published Julia results. The suite is named `julia`; the Julia **language**
implementations live in `julia/julia/` because `julia/` already holds that
language's ports of the other suites.

| Row | Measured workload, identical in every language |
|---|---|
| `parse_integers` | 100,000 signed decimal round trips, including zero; manual digit formatting and left-to-right parsing |
| `matrix_statistics` | 1,000 sets of four 5×5 matrices; horizontal/block assembly, Gram matrices, fourth-power traces, sample standard deviation divided by mean |
| `iteration_pi_sum` | 500 ascending reciprocal-square sums, with lengths 10,000 through 10,499; 5,124,750 terms total |
| `formatted_output` | 100,000 ASCII lines `i i+1\n` for `i=1..100000`; synchronous null-sink writes in batches of 256 lines |

Every port performs one complete untimed warmup, checks it, then measures one
fresh workload and checks its result outside the timer. This suite fixes that
policy even when `JULIA_BENCH_WARMUP=0` is set for other suites. `__TIMING__` is
workload time; process time includes startup, compilation, warmup and checks.
Input generation, array allocation, result accumulation and output writes are
inside the workload. No library-specific benchmark harness, best-of selection,
BLAS, NumPy, SIMD intrinsics or parallel execution is used. Native compilers may
still optimize scalar source; allocation strategies and runtime implementation
costs naturally differ between languages.

## Algorithm contract

The shared generator is Park–Miller: seed starts at 42, then
`seed = seed * 16807 % 2147483647`. Its intermediates fit the exact integer
range of JavaScript and Lambda (formal semantics **S4.1.1**, `int` bound
2^53−1). Digests use modulus 1,000,000,007 and multiplier 31.

Integer conversion uses zero for indices congruent to 0 modulo 8, negative
seed for indices congruent to 1, and positive seed otherwise. Formatting finds
the highest decimal divisor, then appends digits from most significant to
least. Parsing starts after an optional minus sign and accumulates
`value * 10 + digit`. The checksum incorporates every parsed value plus
2,147,483,647, keeping modular arithmetic nonnegative. Every round trip is
checked; total character count and final seed are also verified. This measures
the same explicit conversion algorithm rather than each language's native
integer parser. Result integer rendering follows **S4.8.2**.

Matrix inputs are uniform `seed / 2147483647.0 * 2.0 - 1.0`, generated in
block/row/column order. This replaces the upstream host-specific Gaussian RNG
so every language receives the same values. Four row-major 5×5 blocks A–D
form `P = [A B C D]` (5×20) and `Q = [A B; C D]` (10×10). Gram matrices use
ordered `i,j,k` scalar loops, then two full matrix squarings and an ascending
diagonal trace. Both sets of 1,000 traces use an ascending two-pass mean and
sample variance (denominator 999). Checks include `floor(CV * 1e9)`, the final
seed, and a rolling digest of **every** `floor(trace * 1000)`. No symmetric-matrix
shortcut is used in the ports.

Pi-series summation evaluates `sum(1.0 / (float(k) * float(k)))` in ascending
`k` order. Each round starts at zero; round `r=0..499` ends at `10000+r`.
Different prefix lengths keep the repeated computations observable rather than
allowing one identical sum to replace all 500 rounds. All 500 sums are stored
and contribute, in order, to `sum(values[r] * (r+1))`. The first/last sums scaled
by 1e12 and the weighted sum scaled by 1e6 are floored and checked. The first
sum is the usual truncated π²/6 series.

Formatted output reuses the same decimal formatter. Every line contributes to
the byte count and ASCII digest, then is appended to the current byte or string buffer.
At 256 lines, or the final line, each port opens the OS null device, writes the
entire buffer synchronously, closes it, and resets the buffer. There are 391
open/write/close batches, 1,177,795 bytes and 100,000 lines per workload. Opening,
formatting, hashing, writing and closing are timed. I/O failures fail validation;
Node uses `fs.writeFileSync`, and the QuickJS adapter implements its synchronous
open/write/close behavior. The null device avoids persistent output fixtures or
asynchronous Node writes. Lambda runs the effects and mutable loops in `pn`
procedures under **S12.1.1v2** and **S12.1.2**; inputs to matrix helpers are read
only, while each matrix product writes into a separate result array. Go and C
reuse storage allocated inside each workload: decimal and batch buffers, and
two disjoint matrix scratch arrays. Every scalar product is still computed in
full and in the prescribed order; storage reuse never skips an iteration.

## Implementations and verification

Lambda has untyped `.ls` and annotated `2.ls` entries and shared modules, with
companion `.txt` expected output. JavaScript has four direct Node entries and
one shared module; the runner expands that module for LambdaJS and QuickJS.
Python and Julia use shared local modules; C uses `c2mir/micro_common.h`; Go has
four isolated commands and `internal/bench/julia_micro.go`. C and Go time their
measured call directly so their common outer timers do not include warmup or
emit a second timing marker. JS MVP requires a build that enables that runtime;
its frozen filesystem surface currently lacks `fs.writeFileSync`.

`expected.json` contains the common result vectors. The independent verifier
uses Python's built-in decimal conversions, `math.fsum`, and the symmetric
identity `tr(G^4) = sum((G^2)[i,j]^2)` to check those oracles. It then requires
exact result-vector output, one finite positive timing marker and exit code 0
from every selected port. It records source hashes and per-port outcomes.

Run from the repository root:

```sh
python3 test/benchmark/run_benchmarks.py --typed -s julia -e mir,c2mir,go,lambdajs,quickjs,nodejs,python,julia -n 3 --fresh --results-output temp/julia_micro_times.json
python3 test/benchmark/verify_julia_suite.py --output temp/julia_micro_validation.json
python3 test/benchmark/verify_julia_suite.py --engines interp,interp_typed --output temp/julia_micro_interp.json
python3 -m unittest discover -s test/benchmark -p 'test_julia*.py'
```

Use a release Lambda build (`make release`), set `JULIA_EXE` if Julia is not on
PATH, and set `GOCACHE="$PWD/temp/go-cache"` when the default Go cache is not
writable. A correctness pass verifies workload results; repeated isolated
measurements are needed for performance conclusions.
