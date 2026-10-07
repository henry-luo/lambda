# Julia benchmark ports

Native Julia implementations of all **77 registered entries**, or **71 canonical
rows** after the shared runner removes duplicate workloads. Tested with Julia
1.13.1 on macOS arm64 and the runner's pinned Node v22.13.0. No Julia packages
are required; the ports use Base and the Printf standard library.

| Suite | Entries |
|---|---:|
| R7RS | 10 |
| AWFY | 14 |
| BENG | 10 |
| Kostya | 7 |
| Larceny | 12 |
| Text | 11 |
| JetStream | 9 |
| Julia microbenchmarks | 4 |

The new `julia` workload suite is documented in [SUITE.md](SUITE.md). Its
native Julia scripts live in `julia/julia/`; its other language ports share the
outer `julia/` suite directory.

From the repository root, with `julia` on PATH or `JULIA_EXE` set:

```sh
python3 test/benchmark/run_julia_benchmarks.py --all --verify-node --output temp/julia_validation.json
python3 test/benchmark/run_julia_benchmarks.py --suite r7rs --bench fib
python3 test/benchmark/run_julia_benchmarks.py --list
python3 -m unittest discover -s test/benchmark -p test_julia_runner.py
```

The unified runner supports `-e julia` in time and memory modes, and stores
execution milliseconds as `julia` and process milliseconds as `julia_e2e`:

```sh
python3 test/benchmark/run_benchmarks.py --typed -e julia,nodejs -s r7rs -n 3 --fresh --results-output temp/julia_node.json
python3 test/benchmark/gen_overall_result.py --input temp/julia_node.json --engines julia,nodejs --output temp/julia_node.md
```

`--typed` is the existing unified runner's required Lambda-variant selection;
it does not change Julia's implementation. Whenever a Lambda engine is selected,
the existing release-build gate still applies. The standalone Julia runner does
not need a Lambda executable.

Each entry can also run directly, for example:

```sh
julia --startup-file=no --history-file=no --threads=1 test/benchmark/julia/r7rs/fib.jl
```

Julia is dynamically typed. Function arguments generally have no annotations,
and compilation specializes them for the actual inputs. Numeric arrays and
stable struct fields use concrete types; heterogeneous documents, object
payloads, and polymorphic constraints retain dynamic fields. This is one Julia
implementation per workload, without separate typed/untyped benchmark columns.
Algorithms execute in Julia; the Python runner only starts processes and checks
their output. Matrix multiplication uses the original loops, without BLAS.

## Timing and validation

By default, each fresh process runs one complete warmup with fresh inputs,
verifies it, then prepares a separate measured state. `__TIMING__` covers the
measured workload. Startup, warmup, and verification outside the original timed
body are excluded from that marker and included in process time. Printing inside
a workload formats into an IOBuffer during timing; flushing to stdout occurs
afterward. Node uses each checked-in script's own warmup and output policy, so
these execution figures do not have identical compilation/I/O accounting.
The `julia` microbenchmark suite fixes one warmup in **every** language and
times synchronous formatted output to the OS null device. Its output is not
buffered until after the timer, and its warmup is not disabled by the setting
below.

For process measurements with one workload and no extra warmup:

```sh
python3 test/benchmark/run_julia_benchmarks.py --warmup 0 --suite r7rs
JULIA_BENCH_WARMUP=0 python3 test/benchmark/run_benchmarks.py --typed -e julia,nodejs -s r7rs --results-output temp/julia_cold.json
```

Both policies use one Julia thread. The runner keeps Julia's depot and temporary
files under `temp/`, records runtime version, executable hash, source hashes and
fixture hashes, and fails on crashes, failed result checks, timeouts, or missing
timing markers. `--verify-node` additionally requires matching result output
after timing lines are removed. It checks all 77 entries with `--all`; without
that flag it uses the authoritative manifest's 71 canonical rows.

AWFY inner/outer counts come from `awfy_node_iterations()`. JetStream repeat
counts come from each Node source's `runIteration()` body:

| Entries | Repeats |
|---|---:|
| nbody, cube3d, raytrace3d | 8 |
| richards, splay | 50 |
| crypto_sha1 | 25 |
| deltablue | 20 |
| navier_stokes, hashmap | 1 |

Navier–Stokes times one update; updates 2–15 verify checksum 77 and the shared
density digest -257786486 after timing. Splay builds 8,000 nodes before timing,
then performs 50 × 80 insert/remove pairs; its fixed Park–Miller generator differs
from the current Node wrapper's host random generator. DeltaBlue shares AWFY's
SOM incremental planner, with JetStream's zero-based projection values and no
chain-edit cleanup. Node's Octane planner has different internal collection and
strength-processing operations. Cube3D retains initial drawing, 51 rotations,
and line rasterization; its vertex-sum tolerance is 5e-10 for floating-point
library differences. Raytrace3D retains floor reflection, all 900 pixels per
iteration, and canvas serialization with the original 20,970-character oracle.

Text ports load the existing checked-in fixtures before timing. The shared SOM
JSON parser is used for those bounded fixtures; it is not a general JSON library.
The formatted AST is compared in full, and hyphenation checks every fixture in
addition to its aggregate checksum. Source notices are in [LICENSE.md](LICENSE.md).

The four `jq_*` text ports use a native Julia jq interpreter adapted from
`../jq_vm.ls` and `../text/c2mir/jq_*.h`: recursive-descent parsing, lexical
bytecode compilation, a forkable data stack, path tracking, and a compacted
frame arena. They evaluate the same checked-in `../text/jq/*.jq` filters as
the other reference columns, at their full sizes. No C jq library or external
interpreter runs inside the Julia column. Updates copy containers along their
paths, preserving input snapshots as in Lambda's S9.1.4 value semantics.
The interpreter covers the benchmark subset; it does not claim full jq
conformance. Parsing/compilation and input loading precede the timer; one full
warmup uses separate VM state before measurement. Filter and input hashes are
included in Julia runtime metadata.

```sh
python3 test/benchmark/run_julia_benchmarks.py --suite text --bench jq_mix --bench jq_records --bench jq_bf --bench jq_tree --verify-node --timeout 600 --output temp/julia_jq_validation.json
julia --startup-file=no --history-file=no --threads=1 test/benchmark/julia/test_jq_vm.jl
```

Passing output checks establishes correct benchmark results, rather than
identical instructions, allocations, or a Julia-versus-Node speed claim. Record
the warmup policy when comparing timings; use repeated measurements for any
performance conclusion.
