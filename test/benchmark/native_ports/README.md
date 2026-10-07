# Native Java and Erlang benchmarks

Handwritten Java and Erlang ports cover the registry's **77 entries / 71 canonical
workloads** across R7RS, AWFY, BENG, Kostya, Larceny, Julia microbenchmarks,
JetStream, and Text. The six duplicate entries remain available with `--all`.

The ports use native functions, local variables, primitive arrays or tuples,
records, maps, and host garbage collection. Java compiles the checked-in upstream
AWFY Java sources unchanged. Erlang implements the algorithms with native BEAM
code; private ETS tables are used where graphs require mutable object identity.
The previous Julia-to-Java/Erlang generators and dynamic language adapters have
been removed.

## Run

From the repository root:

```sh
python3 test/benchmark/run_java_benchmarks.py --all --warmup 1 --verify-node --timeout 180 --output temp/java-validation.json
python3 test/benchmark/run_erlang_benchmarks.py --all --warmup 1 --verify-node --timeout 180 --output temp/erlang-validation.json
python3 test/benchmark/run_java_benchmarks.py --suite awfy --bench cd
python3 test/benchmark/run_erlang_benchmarks.py --list
python3 test/benchmark/verify_julia_suite.py --engines java,erlang --timeout 180 --output temp/native-micro-oracles.json
python3 -m unittest discover -s test/benchmark -p test_native_runner.py
```

`--timeout` is a hard limit for each process. JDK 17+ and Erlang/OTP 27+ are
required. The runners find Homebrew toolchains on macOS, honor `JAVA_HOME`, and
accept `JAVA_EXE`, `JAVAC_EXE`, `ERL_EXE`, and `ERLC_EXE` overrides. Sources compile
before execution into a source/toolchain-hashed cache under `temp/`. Each
standalone run pins one compiled build for all entries. All temporary files and
Erlang crash dumps stay under `temp/`.

The unified runner supports `-e java,erlang` in time and memory modes:

```sh
python3 test/benchmark/run_benchmarks.py --typed -e java,erlang,nodejs -s r7rs -n 3 --fresh --results-output temp/native-r7rs.json
```

## Workload and timing

Workload sizes and iteration counts follow `run_benchmarks.py` and its registered
Node sources. AWFY inner/outer counts and JetStream repeats are passed directly
by the runner. `manifest.json` maps every registered entry to its native source,
Node reference, and relevant counts. Runtime metadata hashes native sources,
compiled artifacts, shared inputs, workload references, and the runner.

`__TIMING__` measures workload execution. It excludes source compilation,
process startup, preparation of external fixtures, optional warmup, and final
verification/reporting. Checks that are part of the original algorithm's inner
loop remain inside. Java may compile methods during the timed workload.
Ordinary result reporting is outside these native timers; Node retains each
checked-in script's own output/timing policy. Process time additionally includes VM startup, module loading, warmup,
preparation, and verification. Java uses `-Xss16m -Xmx2g`; Erlang uses one scheduler
and one async worker.

Normally each process warms once using freshly prepared state and then prepares
separate measured state. `--warmup 0` disables this optional warmup. The four
Julia microbenchmarks **always warm once**, as required by their shared
[workload contract](../julia/SUITE.md). They implement the scalar algorithms
natively; no Julia data model, dispatch, indexing, or formatting machinery is
emulated. Matrix multiplication remains scalar without BLAS or symmetry
shortcuts. Pi summation recomputes all 500 independent sums. Formatted output
performs all 391 synchronous open/write/close batches inside the timer.

Navier–Stokes times one frame and verifies the next 14 frames and the density
digest afterward. Splay setup remains outside timing, with full payloads,
8,000 initial nodes, and 50 × 80 modifications. Its deterministic PRNG follows
the existing cross-language port contract. Text workloads preserve all registered
rounds, inputs, algorithms, and checksum checks.

The four `jq_*` workloads execute the complete shared `.jq` filters. Their native
recursive-descent parser and lexical compiler run before timing; the timed
workload includes bytecode execution, forkable stacks, closures, path updates,
and result collection. A jq interpreter is part of these particular workloads.
It does not provide a language adapter for the other benchmarks. Arrays and
objects copy along updated paths, preserving observable snapshots (formal
semantics **S9.1.2**, `doc/Lambda_Formal_Semantics.md`). The implementation supports
the benchmark subset; these checks do not establish full jq conformance.

`--verify-node` compares complete output, excluding timing markers, and requires
the pinned Node version. The separate microbenchmark verifier checks independent
integer, summation, byte-digest, and matrix identities. Failed checks, missing
ports/toolchains, compilation errors, invalid timers, and timeouts stay explicit.

The original Java/Erlang measurements imported into Result51 used generated
adapters. They are historical measurements of that implementation and must not
be presented as measurements of these rewritten ports. Source notices are in
[LICENSE.md](LICENSE.md).

## Validated native results

Both languages passed all **77/77 registered entries** against pinned Node.
The independent microbenchmark verifier passed **8/8 ports**, and the native
runner checks passed **11/11 tests**.

[Result51](../Overall_Result51.md) now uses three sequential fresh-process samples
per canonical workload, with one full warmup and separate workload/process times.
The [native result JSON](../benchmark_results_v51_java_erlang_native.json) retains
the ordered samples and exact source, input, runtime, and compiled-artifact hashes.
Its evidence archive contains the measured sources and binaries, all six raw run
files, the Node references, and the independent verification output. The earlier
adapter result JSON and baseline archive remain available as historical evidence.
