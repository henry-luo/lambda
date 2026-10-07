# Java and Erlang benchmark ports

Both languages provide ports for the authoritative registry's **77 entries**, or **71
canonical workloads**, across R7RS, AWFY, BENG, Kostya, Larceny, Text, JetStream,
and the Julia microbenchmarks. The six duplicate entries remain available with
`--all`. Workload sizes, AWFY inner/outer counts, and JetStream repeats come from
`run_benchmarks.py` and its checked-in Node sources.

## Run

From the repository root:

```sh
python3 test/benchmark/run_java_benchmarks.py --all --warmup 0 --verify-node --timeout 7200 --output temp/java-validation.json
python3 test/benchmark/run_erlang_benchmarks.py --all --warmup 0 --verify-node --timeout 7200 --output temp/erlang-validation.json
python3 test/benchmark/run_java_benchmarks.py --suite awfy --bench cd
python3 test/benchmark/run_erlang_benchmarks.py --list
python3 test/benchmark/verify_julia_suite.py --engines java,erlang --timeout 600 --output temp/native-micro-oracles.json
```

The full correctness commands disable the optional extra warmup and allow long
runs for the boxed adapters. Use `--warmup 1` for the normal extra warmup.
The four jq workloads can take tens of minutes to over an hour, particularly
on Erlang. The normal 600-second timeout can be too short for them. Extra warmup
roughly doubles the workload, so increase `--timeout` when enabling it.

Initial Node-verified coverage is 77/77 entries for Java and 76/77 for Erlang.
Erlang's full `jq_tree` run was cancelled at the user's request after about
112 minutes; it has no passing result. Subsequent adapter fixes were checked
with focused tests and bounded, reduced jq fixtures. Those diagnostic results
do not establish a full-size `jq_tree` pass or replace its registered workload.

Use JDK 17 or newer and Erlang/OTP with `erl` and `erlc`. The runners find
Homebrew's installed JDK/Erlang on macOS, honor `JAVA_HOME`, and accept
`JAVA_EXE`, `JAVAC_EXE`, `ERL_EXE`, and `ERLC_EXE` overrides. Compilation is cached
under `temp/`, keyed by sources and toolchain version. Compilation completes
before a benchmark process is timed. Erlang crash dumps and temporary files also
stay under `temp/`.

The unified runner supports `-e java,erlang` in time and memory modes:

```sh
python3 test/benchmark/run_benchmarks.py --typed -e java,erlang,nodejs -s r7rs -n 3 --fresh --results-output temp/native-r7rs.json
python3 test/benchmark/gen_overall_result.py --input temp/native-r7rs.json --engines java,erlang,nodejs --output temp/native-r7rs.md
```

Execution times use `java` / `erlang`; process times use `java_e2e` /
`erlang_e2e`. Missing toolchains, compilation failures, invalid timers, failed
checks, and timeouts remain explicit failures. `--verify-node` compares full
result output after removing timing markers and requires the runner's pinned
Node version.
The microbenchmark verifier also checks both languages against independent
integer, summation, output-digest, and matrix-trace identities.

## Implementation and timing

`java/Ports.java` and `erlang/ports.erl` contain compiled native functions generated
from the checked-in Julia algorithm ports. They execute in the JVM and BEAM
respectively. `TextSearch.java` and `text_search.erl` use direct native indexed
loops for the same Naive, KMP, and Boyer–Moore algorithms, avoiding boxed access
in the billion character comparisons. The benchmark processes require their native toolchain and shared
input fixtures; Julia is used only when regenerating source files. Python starts
processes and validates outputs.

The shared generation tool retains each algorithm's loops, recursion, data
structures, and checks. Java uses boxed values, maps for lexical environments,
and dynamic method dispatch. Erlang uses process-local mutable object handles,
native arrays/maps, rooted call frames, and collection at loop boundaries and function returns.
These choices preserve imperative benchmark object identity and cycles. Timings
include these adapters and describe this generated implementation. Further
optimization should retain the same workloads and output checks.

The tree investigation found substantial lexical-environment and call-frame
overhead. The generators now resolve proven immutable scalar bindings once per
function call, avoiding repeated constant lookup during bytecode dispatch.
Erlang also avoids redundant argument normalization and unused lexical frames
for expression blocks. Three interleaved pairs on each of two reduced tree
cases showed about 17% lower execution time, with Node-verified checksums.
The generated adapters still have substantial costs; full-size tree timing
after these changes remains unmeasured.

Each process normally performs one full warmup with freshly prepared state,
then prepares separate measured state. `__TIMING__` covers the workload; it
excludes native source compilation, startup, warmup, and subsequent verification.
Java may continue JIT compilation during measurement. Process time includes VM
startup, module/JIT loading, warmup, workload, and verification. `--warmup 0` or
`NATIVE_BENCH_WARMUP=0` disables the extra warmup; the four Julia microbenchmarks
always perform one warmup as required by their shared contract. Java uses
`-Xss16m -Xmx2g`; Erlang uses one scheduler and one async worker.

Text fixtures and jq filters compile/load before timing. The four `jq_*` ports
execute the shared full-size `.jq` programs in a native port of Lambda's jq VM:
recursive-descent parser, lexical bytecode compiler, forkable stacks, paths,
closures, and compacted frame storage. Container updates copy along their paths,
following the observable snapshot principle in **S9.1.2** of
`doc/Lambda_Formal_Semantics.md`. This interpreter implements the benchmark
subset. Its checks do not establish full jq conformance.

Navier–Stokes times one frame and verifies the remaining frames and the shared
density digest afterward. Matrix workloads retain scalar loops without BLAS.
Formatted output opens, writes, and closes the OS null sink for every batch
inside the timer. Other benchmark output follows the Julia ports' buffering
policy. The algorithm ports retain the Julia reference's documented Splay PRNG
and DeltaBlue planner choices; see `../julia/README.md`.

## Regenerate and review

```sh
python3 test/benchmark/native_ports/generate.py
python3 -m unittest discover -s test/benchmark -p test_native_runner.py
```

Regeneration needs Julia on PATH to parse syntax; the Python backends emit native
functions. They support the syntax used by these benchmark sources and fail on
unsupported syntax. They are a benchmark maintenance tool. Edit the source
algorithm or generator, then regenerate; retain the authoritative workload
contract. `manifest.json` records entry mappings and exact algorithm-source
hashes. Runner metadata also records native source, compiled artifact, generator, runtime,
and fixture hashes. A standalone run pins one compiled build for all entries. Source notices are in [LICENSE.md](LICENSE.md).
