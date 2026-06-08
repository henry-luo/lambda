# Transpile JS Tune7 — MIR Interpreter vs JIT Verification

Date: 2026-06-08
Status: benchmark landed (verification report, not a proposal)
Scope: correctness and run-time of LambdaJS under the MIR **interpreter** engine
vs the default MIR **JIT** generator, across two suites — the test262 baseline
and the `test_js_gtest` fixture set.

## Goal

LambdaJS lowers JavaScript to MIR and, by default, JIT-compiles that MIR to
native code. MIR also ships an interpreter that executes the MIR instruction
stream directly. This report measures, on real workloads:

1. **Correctness** — does interpreter mode produce the same results as JIT?
2. **Run time** — interpreter vs JIT, by wall clock and by summed per-test time.

## How interpreter mode is selected

A single global, `g_mir_interp_mode`, chooses the engine at MIR link time. It is
set either by the CLI flag `--mir-interp` or by the environment variable
`JS_MIR_INTERP=1` (fallback checked once per process in
`lambda/js/js_mir_entrypoints_require.cpp:421`). Every MIR entrypoint links with:

```c
MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);
typedef Item (*js_main_func_t)(Context*);
js_main_func_t js_main = (js_main_func_t)find_func(ctx, "js_main");
Item result = js_main((Context*)context);   // identical call site in BOTH modes
```

Link sites: `js_mir_entrypoints_require.cpp:170` (main script/batch path),
`js_mir_module_batch_lowering.cpp:5597` (ES-module / parallel import),
`js_mir_eval_lowering.cpp:190` and `:1113` (dynamic `Function()`/eval).

- `MIR_set_gen_interface` (`ref/mir/mir-gen.c:9755`) installs a generator thunk;
  on first call it emits native machine code and redirects to it.
- `MIR_set_interp_interface` (`ref/mir/mir-interp.c:2050`) builds a compact
  `icode` array and runs MIR's direct-threaded dispatch loop — **no native code
  is ever produced.**

The JS front-end (parse → AST → MIR lowering) and the resolved `js_main` symbol
are identical between modes; only the interface argument to `MIR_link` differs.

## Test environment

- Host: Darwin 24.6.0, arm64, 8 CPU cores.
- Runtime: **release** `lambda.exe` (14 MB, stripped). Release is mandatory —
  a debug build inflates per-test timing and fabricates slow-test noise.
- Optimization level: `--opt-level=0` (the test262 default; short-lived scripts).
- Commits: lambda `e831c5125`, lambda-test `44191a554`.
- Both engines run the **same** workloads; the only difference is the interpreter
  flag/env var.

---

## §1 Verification A — test262 baseline (39,258 tests)

Command (interpreter run adds `--mir-interp`):

```bash
ASAN_OPTIONS=detect_container_overflow=0 ./test/test_js_test262_gtest.exe \
  --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --js-timeout=60 [--mir-interp]
```

`--js-timeout=60` was raised from the default so the interpreter's slower tests
do not spuriously time out. The compiled-in 3 s "fully-passing" slow-gate cannot
be raised without a rebuild; a baseline test that crosses it is reclassified
`partial` and counted as a "regression" by the runner **even though it computed
the correct result** (`test/test_js_test262_gtest.cpp:3615-3622`). The
authoritative correctness signal is therefore the `--write-failures` manifest
(genuine Test262Error / crash / wrong-result), which was **empty in both modes.**

### Correctness — 100% in both modes

| Metric | JIT | Interpreter |
| --- | ---: | ---: |
| Genuine failures (manifest rows) | **0** | **0** |
| Regressions (pass → fail) | **0** | **0** |
| Fully passed | 39256 / 39258 | 39255 / 39258 |
| Non-fully-passing (correct but > 3 s slow-gate) | 2 | 3 |

Every baseline test computed the correct result under the interpreter. The
interpreter trips the slow-gate on one extra exhaustive test (3 vs 2) — a timing
reclassification, not a correctness failure.

### Performance — interpreter slightly faster in aggregate

| Metric | JIT | Interpreter | Δ |
| --- | ---: | ---: | ---: |
| Wall time | 134 s | 131 s | −2% |
| Per-test sum (Σ `elapsed_us`) | 452.9 s | 373.9 s | **−17.4%** (ratio 0.826) |

Per-test distribution over all 39,258 tests:

- Interpreter **faster on 26,879 tests (68.5%)**, **slower on 12,353 (31.5%)**,
  equal on 26.

### Why the interpreter wins here, and where JIT still wins

test262 scripts are tiny and short-lived, so JIT's up-front native-codegen cost
(at MIR link / first call) is not amortized — the interpreter skips codegen and
wins on the majority of short tests. On genuinely hot code the JIT wins
decisively. A 50M-iteration hot-loop microbenchmark:

| Workload | JIT | Interpreter |
| --- | ---: | ---: |
| `for (i<50M) s=(s+i*3)%1000000007` | **26.3 s** | 43.5 s (**1.65× slower**) |

This both confirms the interpreter interface is genuinely engaged and shows the
crossover: native code pays off once codegen is amortized over many executed
instructions.

---

## §2 Verification B — `test_js_gtest` (169 tests)

`test_js_gtest.exe` discovers `.js` + `.txt` fixtures under `test/js`,
`test/node`, `test/js/props` and asserts **exact stdout match** (a stronger check
than test262's "no error"). It shells out to `lambda.exe js` / `js-test-batch`
via `system()`, so interpreter mode was enabled with `JS_MIR_INTERP=1` in the
environment (inherited by the spawned children). Verified equivalent to
`--mir-interp` by reproducing the one failure both ways.

### Correctness — interpreter is NOT 100%: 1 failure

| Mode | Result |
| --- | --- |
| JIT | **169 / 169 PASSED** |
| Interpreter | **168 / 169** — `JsFileTest.Run/tco` **FAILED** |

The single failure is `test/js/tco.js` (tail-call optimization). Run directly:

```
JIT     → all 12 lines correct, exit 0
INTERP  → "Uncaught RangeError: Maximum call stack size exceeded", exit 1
          (dies at line 7, sum_rec(100000,0); first 6 lines printed)
```

**Root cause — the two MIR engines differ in tail-call support.** MIR's JIT
generator performs proper tail-call optimization: tail calls reuse the stack
frame, so `sum_rec(100000)` / `countdown(500000)` run in O(1) stack. MIR's
interpreter does **not** eliminate tail calls — each call consumes a frame, so the
deep tail recursion overflows. The other 168 fixtures produce byte-identical
output in both modes. This is a real, reproducible semantic divergence, not a
flake.

### Performance — interpreter ~28% lower summed run time

Summed per-test `elapsed_us` from `js-test-batch` `BATCH_END` lines over the same
158-test non-DOM set, with the phase breakdown:

| Phase | JIT | Interpreter | Note |
| --- | ---: | ---: | --- |
| MIR build (AST → MIR) | 2.92 s | 2.94 s | same front-end |
| **Link** | **4.29 s** | **0.45 s** | JIT emits native code; interp builds icode — **9.5×** |
| Execute | 2.62 s | 2.81 s | interp ~7% slower (interpret vs native) |
| **Σ elapsed (total)** | **10.94 s** | **7.85 s** | **interp −28%** |
| *full-gtest wall* | *12.24 s* | *8.04 s* | — |

The phase split is the clearest evidence of the mechanism: JIT's cost lives in
**link/codegen** (4.29 s), which the interpreter almost entirely avoids (0.45 s);
the interpreter repays only +0.19 s in execution because these scripts have no hot
loops. Net, the interpreter is faster on both wall and summed run time — the same
direction as test262, now with the link-vs-execute breakdown showing *why*.

---

## §3 Conclusions

1. **Performance is consistent across both suites.** On short-lived scripts the
   interpreter is *faster* in aggregate (test262 per-test sum −17%, `test_js_gtest`
   −28%) because there is no native-codegen cost to amortize. JIT only wins on
   genuinely hot code (hot loop: JIT 1.65× faster). The crossover is entirely in
   the **link** phase.

2. **Correctness is engine-dependent in one known way.** test262 baseline was 100%
   in both modes. `test_js_gtest` exposes a single divergence: the **MIR
   interpreter lacks tail-call optimization**, so TCO-dependent deep recursion
   (`test/js/tco.js`) stack-overflows under the interpreter while passing under
   JIT.

### Recommendations

- If interpreter mode is ever promoted to a primary or fallback execution path,
  the TCO gap must be closed — either by implementing tail-call elimination in the
  MIR interpreter, or by gating TCO-dependent tests the way test262 already gates
  `tail-call-optimization` (the "intentional PTC exception").
- For batch workloads of many short scripts (the test262 and `test_js_gtest`
  profile), the interpreter is a legitimately faster option *if* the TCO caveat is
  acceptable for that workload.

## Reproduction

```bash
make release                       # release lambda.exe is mandatory for timing

# Verification A — test262 baseline, both modes
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt --js-timeout=60 \
  --write-failures=temp/js262_jit_failures.tsv
#   ...repeat with --mir-interp and --write-failures=temp/js262_interp_failures.tsv

# Verification B — test_js_gtest, both modes
./test/test_js_gtest.exe                       # JIT
JS_MIR_INTERP=1 ./test/test_js_gtest.exe       # interpreter

# tail-call divergence, isolated
./lambda.exe js test/js/tco.js --no-log                 # exit 0, correct
./lambda.exe js test/js/tco.js --mir-interp --no-log    # RangeError, exit 1
```
