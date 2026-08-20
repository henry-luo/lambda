# Native C2MIR benchmark coverage

`mac-deps/mir/c2m` compiles and JIT-runs the native C ports. Run all supported
benchmarks with:

```sh
python3 test/benchmark/run_c2mir_benchmarks.py
```

Each port is compiled together with `test/benchmark/c2mir/bench_timer_main.c`
under `-Dmain=c2mir_bench_body`: that renames the port's own entry point so the
timer file can supply `main` and bracket the workload with a wall-clock
measurement, reported as `__TIMING__` milliseconds like every other engine.
Whole-process wall time is not usable for these ports because it includes c2m
parsing and JIT-generating the source, which dominates every sub-100ms
benchmark. The same invocation is reused by the `c2mir` engine of
`run_benchmarks.py`, so the correctness runner and the timing runner cannot
drift apart.

`Overall_Result18.md` carries the resulting column. Note that it is **not** the
retired `lambda --c2mir` transpiler path (CLAUDE rule 14) — that CLI option no
longer exists; the column measures these native C ports.

## Covered workloads

| Suite | Coverage |
| --- | --- |
| R7RS | 10/10 canonical workloads |
| AWFY | 14/14 workloads (SOM-style `cd`, `deltablue`, `havlak`, `json`, `richards` added 2026-08-19) |
| BENG | 9/9 workloads (`pidigits` added 2026-08-19 with a sign-magnitude base-2^32 mini-bignum) |
| Kostya | 7/7 canonical workloads |
| Larceny | 12/12 canonical workloads (`deriv` added 2026-08-19 with a tagged-struct expression tree) |
| JetStream | 6/6 canonical rows (`crypto_sha1`, `cube3d`, `navier_stokes`, `raytrace3d`, `splay` added 2026-08-19) |

The two Lambda source variants (`name.ls` and `name2.ls`) represent the same
benchmark with different Lambda typing; one native C port covers that workload.

The 2026-08-19 additions port the previously excluded rows faithfully rather
than simplifying them: the SOM-style benchmarks keep their object graphs as C
structs with function-pointer or switch dispatch where the polymorphism is part
of the measured work (richards' TCB dispatch, deltablue's constraint kinds),
`cd` ports the `.ls`'s own red-black tree rather than substituting a hash
table, and every port asserts the same final check values as the `.ls` it
mirrors, with identical workload sizes and PRNG sequences.

## Deliberate exclusions

- `cow_document_edit`: exercises Lambda's copy-on-write document runtime, not a
  portable C workload.

## Resolved caveat — `crypto_sha1` (cleared 2026-08-19)

- `jetstream/crypto_sha1.ls` was recorded here as computing a WRONG digest on
  the Lambda engine (`FAIL got=ffffffffffffffffffffffffffffffffffffffff`, an
  all-ones saturation) on both the archived v33 binary and the then-current
  build. **That is no longer true.** Both variants now assert PASS, and the
  asserted digest `2524d264def74cce2498bf112bedf00e6c0b796d` was re-verified
  against Python's `hashlib` on the exact doubled-plaintext input the script
  hashes. The row measures the correct computation on both engines.

- ⚠ Where it was fixed is NOT established. It passes on `f46aae989` (the tree
  the caveat's own commit predates) as well as on current builds, so the fix
  landed somewhere in that window; no bisect was run, because each step needs a
  full release build.

- ⚠ Consequence for the published cells: the crypto_sha1 **MIR** cells in
  `benchmark_results_v33.json` and earlier were taken while the engine was
  producing all-ones, which is not the same amount of work as the correct
  digest. Those cells should be re-measured before this row is compared across
  sessions. The C2MIR cell was always correct and is unaffected.
