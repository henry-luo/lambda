# Transpile JS Tune6 — AST Build and Lazy MIR Tuning Proposal

Date: 2026-06-06
Status: proposal (rev 3 — benchmark landed, priorities corrected by data)
Scope: JavaScript transpile latency, generated MIR volume, and cold third-party library startup

## Revision history

- rev 1: initial proposal.
- rev 3: the Phase A benchmark (§1) and scope counters were implemented and run.
  The measured data **contradicts the AST-first premise** for every in-tree
  fixture: AST build is <2% of compile time; MIR lowering + link dominate
  (~85–95%). Priorities reordered accordingly — see **§0 Benchmark Findings**.
  Area One (AST, incl. the rev-2 scope fix) is demoted to a minor cleanup; Area
  Two (reduce MIR) and Area Three (lazy MIR) become the primary levers.
- rev 2: revised after review of the actual engine code. Key changes:
  - Added a mandatory **release-mode baseline** as the first measurement task; the
    debug numbers below are kept only as the symptom that started this.
  - Promoted **scope-lookup cost** to the primary AST root cause (linear-scan
    `js_scope_lookup` called per identifier → O(n²) on minified libraries), ahead
    of the symbol/field-id mechanical wins.
  - Corrected §2.4: the identifier no-escape fast path **already exists**; the real
    remaining copy cost is in string-literal decoding.
  - Corrected §2.2: the `sym_*` defines in `build_js_ast.cpp` are **hand-written
    stubs**, not generated values; the switch migration needs the real generated
    symbol table plus a correctness gate.
  - **Removed native vendor installers** (old §3.4). Replaced the Radiant
    fast-path strategy with **compiled-artifact caching across the JS test batch
    and the whole layout/test run**.
  - Replaced source-pattern recognition (old §3.3) with generic always-emitted
    runtime helpers only.
  - Rewrote Lazy MIR around the **verified call ABI**: inter-function calls are a
    hybrid of direct MIR-symbol calls (statically-known functions) and indirect
    `js_call_function` dispatch. Lazy per-function MIR is therefore not feasible
    without a stub/trampoline or an indirection-in-lazy-mode policy.

## Goal

Recent Radiant web-template testing exposed a different performance profile from
the normal LambdaJS execution benchmarks. For large browser libraries, the slow
part is not reading the source file or running layout. It is compiling a large
amount of JavaScript that the page often never calls.

One representative **debug-mode** measurement for local `jquery-1.8.2.min.js`
showed:

| Phase | Time |
| --- | ---: |
| source load | 0.5 ms |
| Tree-sitter parse | 26.7 ms |
| AST build | 1,759 ms |
| MIR lowering | 1,109 ms |
| MIR link/JIT | 549 ms |
| JS execute | 222 ms |

> ⚠️ These are **debug-build** numbers and are used only as the symptom that
> motivated Tune6. Per project rule, performance must not be judged from debug
> builds. The first task in Phase A is to reproduce this in **release** mode.
> Debug build inflates per-node C++ overhead non-uniformly, so the AST-vs-MIR
> ratio above may not hold in release, and prioritization must follow the release
> profile, not this table. The fact that AST build *exceeds* MIR lowering in debug
> is itself a signal of a superlinear algorithm in the AST path (see §2.2).

This proposal targets three areas:

1. Reduce AST building time.
2. Reduce generated MIR code, which should reduce MIR JIT time and speed up
   execution by keeping cold library internals out of the hot startup path.
3. Implement lazy function MIR so function bodies are lowered and linked only
   when they are actually called.

Before tuning any of those areas, add a focused JS transpiling GTest benchmark so
changes can be measured on repeatable workloads without running full layout
suites, and capture a release-mode baseline.

---

## 0. Benchmark Findings (rev 3)

The Phase A benchmark and scope counters were implemented and run. They already
change the plan, which is exactly why "measure first" was a hard prerequisite.

What landed:

- `JsScopeCounters` diagnostics API (`lambda/js/js_runtime.h`,
  `lambda/js/js_scope.cpp`) — counts scope-lookup calls, parent scopes walked,
  and `NameEntry` comparisons; enable-gated for zero cost in normal runs.
- `JS_TRANSPILE_TIMING=1` on the `js` CLI (`lambda/main.cpp`) prints all nine
  `JsMirPhaseTiming` phases plus the scope counters.
- `test/test_js_transpile_timing_gtest.cpp` — a subprocess benchmark over a mixed
  in-tree corpus, registered in `build_lambda_config.json` as `extended`. Pass/fail
  is correctness-only (`mir_ms > 0`); it never asserts timing thresholds.

### 0.1 Release-build results (the baseline that matters)

Captured with `make release` (`lambda.exe` 14 MB, `NDEBUG`), compile+execute path:

| Fixture | bytes | total_ms | ast_ms | **ast %** | mir_ms | link_ms | **link %** | avg/lookup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `underscore_lib.js` | 32,332 | 1,204 | 6.0 | **0.5%** | 120 | 985 | **82%** | 22.5 |
| `ramda_src_min.js` | 53,206 | 2,451 | 15.9 | **0.7%** | 542 | 1,781 | **73%** | 48.5 |
| `lib_lodash.js` | 78,449 | 6,731 | 25.9 | **0.4%** | 1,618 | 4,809 | **71%** | 155.2 |
| `lib_ajv.js` | 124,734 | 1,038 | 17.0 | **1.6%** | 103 | 520 | **50%** | 21.2 |
| `lib_yup.js` | 158,695 | 850 | 19.8 | **2.3%** | 152 | 478 | **56%** | 72.9 |
| `lib_acorn.js` | 234,977 | 1,444 | 14.1 | **1.0%** | 215 | 867 | **60%** | 77.4 |
| `dom_jquery_lib.js` | 290,802 | 1,449 | 20.2 | **1.4%** | 351 | 949 | **65%** | 62.5 |

(Subprocess timings; per-phase numbers come from `js_mir_get_last_phase_timing`
and are unaffected by process startup. Debug numbers gave the same shape — AST
0.8–1.9% — and are omitted now that release is available.)

### 0.2 What the data says

1. **AST build is not the bottleneck — even less so in release: 0.4%–2.3%.** The
   rev-1/rev-2 headline (`jquery-1.8.2.min.js`: AST = 1,759 ms ≈ 45%) is **not
   reproduced** by any file in `test/js`. Either that specific minified file has an
   unusual shape or the number predated a code change; it must be re-measured
   directly (§0.4) before any AST work is justified.

2. **MIR link is the single dominant phase (50–82% of total); MIR lowering is
   second.** Together they are 70–95% of compile time. `link_us` — the MIR
   JIT link/finalize step — is the biggest lever, not AST and not even lowering.

3. **Cost tracks function/module structure, not source bytes.** `lib_ajv.js`
   (125 KB) compiles in ~1.0 s while `lib_lodash.js` (78 KB) takes **6.7 s**,
   almost all of it link (4.8 s). lodash is the worst case and the most
   scope-pathological (155 entries/lookup) — yet its AST is still only 26 ms. This
   superlinear link blow-up likely interacts with the adaptive opt thresholds
   (`JM_LARGE_*_INSN_THRESHOLD`, §3.1): a large single module pushes link cost up
   fast. This is exactly the cost lazy MIR removes — most lodash/underscore
   functions are never called at module load.

4. **The O(n²) scope-lookup hypothesis is confirmed but immaterial.** Average
   `NameEntry` comparisons per lookup grow with file size (22 → 49 → 155), so the
   linear-scan scope is genuinely superlinear — but it lives in the <2% AST phase,
   so fixing it (rev-2 §2.1) would not move total time. Total lookups are modest
   (the builder is not doing a lookup per identifier as feared). Demoted.

### 0.2a Link phase dissected: it is eager codegen, not symbol resolution (rev 3)

The `link_us` phase (the dominant cost, §0.2) was traced into MIR
(`ref/mir/mir.c`, `ref/mir/mir-gen.c`). Findings:

- **Symbol resolution is O(n), not O(n²).** `MIR_link` resolves
  imports/exports/forwards through a hash table (`module_item_tab`,
  `mir.c:691,784`); the Lambda import resolver is two O(1) hashmap lookups
  (`lambda/mir.c:106`). JS functions are module-local forwards, not global
  exports, so the global symbol table stays small. **The number of registered
  functions does not make resolution superlinear.** (Full derivation: Appendix A.)
- **The real driver is eager per-function codegen.** `MIR_link` (`mir.c:1969`)
  makes three passes over *every* function: Pass 1 `simplify_func` (`:1987`),
  Pass 2 `process_inlines` (`:2020`), Pass 3 `set_interface` (`:2061`). The engine
  passes `MIR_set_gen_interface` (`js_mir_entrypoints_require.cpp:664`), which
  calls `MIR_gen` — full codegen + regalloc (+ SSA/GVN/LICM at opt≥2) — for **each
  function**, including ones never called. So `link_us ≈ Σ MIR_gen(function)` over
  all functions. This is why link tracks function count/size, why lodash (~thousands
  of functions) costs 4.8 s, and why the existing opt=0 downgrade for >100k-insn
  modules (`js_mir_entrypoints_require.cpp:642`) exists. **Confirmed by experiment
  (§0.2b): turning codegen lazy collapses `link_us` to near-zero.**

### 0.2b Tested: MIR's native lazy gen interface — collapses link, but NOT viable

MIR ships `MIR_set_lazy_gen_interface` (`mir-gen.c:9779`, public `mir-gen.h:25`): a
drop-in for `MIR_set_gen_interface` that installs a wrapper thunk on
`func_item->addr` and runs `MIR_gen` on first call. It was wired behind
`JS_LAZY_MIR=1` (`js_mir_entrypoints_require.cpp:664`) and A/B-benchmarked in
release. **Result: it craters link as predicted, but execution regresses
catastrophically — the change is net-negative and was not adopted.**

All values are milliseconds. Each column is `{phase} {mode}`, where **eager** =
`JS_LAZY_MIR=0` (`MIR_set_gen_interface`, current default) and **lazy** =
`JS_LAZY_MIR=1` (`MIR_set_lazy_gen_interface`). Phases are from `JsMirPhaseTiming`:

| Header | Meaning |
| --- | --- |
| link eager | `MIR_link` time with eager codegen (ms) |
| link lazy | `MIR_link` time with lazy codegen (ms) |
| exec eager | top-level execution time, eager (ms) |
| exec lazy | top-level execution time, lazy (ms) |
| total eager | full compile+run, eager (ms) |
| total lazy | full compile+run, lazy (ms) |

Lazy is meant to *move* codegen cost from `link` (it drops to ~0) into `exec`
(functions generate on first call). It is net-negative because the cost reappearing
in `exec` far exceeds what leaves `link` — on-demand generation is ~80× costlier
per function than batch (§0.2b synthetic test below).

| Fixture | link eager | link lazy | exec eager | exec lazy | total eager | total lazy |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `underscore_lib.js` | 912 | **12** | 583 | 935 | 1,653 | 1,104 |
| `ramda_src_min.js` | 1,569 | **82** | 11 | **96,676** | 2,204 | **97,380** |
| `lib_lodash.js` | 4,804 | — | 77 | — | 6,746 | **timeout >180,000** |
| `lib_ajv.js` | 529 | **20** | 333 | 1,089 | 1,050 | 1,298 |
| `lib_yup.js` | 481 | **21** | 142 | 1,008 | 853 | 1,254 |
| `lib_acorn.js` | 904 | **42** | 222 | **13,319** | 1,486 | **13,723** |
| `dom_jquery_lib.js` | 948 | **48** | 25 | **8,408** | 1,449 | **8,931** |

Only `underscore` improved; everything else regressed 1.2×–44× and `lodash` timed
out. Diagnosis with a synthetic file — **2000 trivial functions, each called
exactly once** (the *ideal* lazy case: no wasted gen, no regeneration):

| Mode | link_ms | exec_ms | total_ms | per-function gen |
| --- | ---: | ---: | ---: | ---: |
| eager (batch) | 2,237 | 293 | 3,853 | **~1.1 ms** |
| lazy (on-demand) | 35 | **176,626** | 177,972 | **~88 ms** |

**Per-function lazy generation costs ~80× more than batch generation, and scales
superlinearly (≈O(n²)) with function count.** Micro-benchmarks that call one
function in a hot loop (1e6 calls) show *no* regression, so it is not
regeneration-per-call and the redirect does work. The root cause is isolated in
§0.2c: it is the opt≥2 optimizer passes under lazy interleaving, not the wrapper or
MIR codegen itself.

**Conclusions:**

1. `link_us` is definitively eager `MIR_gen`, not resolution (link dropped
   480–4,800 ms → 12–82 ms across the corpus). The §0.2a analysis is validated.
2. **MIR's native lazy interface is not a usable drop-in *at the default opt=2*** —
   see §0.2c for why. The rev-2 caution about the hybrid ABI was the wrong worry;
   the real blocker is the optimizer's behavior under interleaved generation.
3. The flag stays in (off by default) as an experiment harness.

### 0.2c Root cause of the ~80×: opt≥2 optimizer passes under lazy interleaving

The ~80× was traced into the MIR generator (`ref/mir/mir-gen.c`). Key facts:

- Eager (`MIR_set_gen_interface`→`MIR_gen`, `mir-gen.c:9506`) and lazy first-call
  (`generate_func_and_redirect`→`generate_func_code`) call the **identical**
  `generate_func_code(ctx, func, TRUE)` on the **same persistent `gen_ctx`**. So
  per-function codegen of a given function is the same code in both modes.
- `generate_func_code` runs the optimization pipeline (build_ssa, GVN, copy-prop,
  DSE, LICM, coalesce, …) only at `optimize_level >= 2` (`mir-gen.c:9311–9442`).
- The engine's default opt level is **2** (`transpile_js_mir.cpp:35`), and it is
  **restored after `MIR_link`** (`js_mir_entrypoints_require.cpp:668`). So in lazy
  mode the deferred generation runs at the heaviest opt level.

Controlled measurements (synthetic *n* trivial functions, each called once):

| Test | result |
| --- | --- |
| lazy, opt=0, n=2000 | exec **371 ms** (~0.18 ms/func) — linear, fine |
| lazy, opt=1, n=2000 | exec 2,836 ms (~1.4 ms/func) |
| lazy, opt=2, n=2000 | **timeout** |
| lazy, opt=2, n=100/200/400/800 | exec 113 / 427 / 1,778 / 10,741 ms |
| eager, opt=2, n=2000 | link **1,317 ms** (~0.66 ms/func) — linear |

Two diagnostics pin the cause:

1. **It is opt-level, not the wrapper/thunk/code-publish.** At opt=0 lazy is linear
   and even cheaper than eager-opt=2. The code-publish and thunk-redirect path runs
   at every opt level, so it cannot be the quadratic term.
2. **At opt=2, lazy per-function cost grows with *n* (≈O(n²)):** 1.1 → 2.1 → 4.4 →
   13.4 ms/func for n = 100 → 800. Eager opt=2 is flat (~0.66 ms/func). Same passes,
   same `gen_ctx`, same per-function input — so the quadratic comes from gen-context
   state that accumulates across the *interleaved* lazy generations but stays small
   in the tight eager batch. **It is not GC:** adding a 2 M-object live JS heap did
   not inflate per-function gen cost (heap-size independent).

Exact MIR structure that accumulates is not yet pinned to a line (candidates: SSA/
GVN/coalesce data built per-function whose cost depends on accumulated module
state at opt≥2). But the *actionable* conclusion does not need it:

**Deferred/cold functions must not be generated at opt≥2.** Generating at opt=0 is
linear and fast.

### 0.2d The real fast path is the MIR interpreter — and the >100k downgrade is a near-no-op

**Correction to an earlier conflation.** The big "opt=0" speedup is *not* opt=0
JIT — it is the **MIR interpreter**. The engine switches large sources to the
interpreter when `g_js_mir_optimize_level == 0` **and** source ≥ 15 KB
(`js_mir_entrypoints_require.cpp:529`); the interpreter skips JIT codegen entirely.
Since every corpus fixture is >15 KB, the `--opt-level=0` numbers measured the
interpreter, not opt=0 codegen.

Disentangled on lodash (680 k MIR insns; debug build; `LAMBDA_JS_LARGE_INTERP`
toggles the interpreter path):

| Mode | link_ms |
| --- | ---: |
| default — auto-downgrade fires (opt2→opt0 **JIT**) | 3,736 |
| `--opt-level=0`, interpreter **allowed** (→ interpreter) | **180** |
| `--opt-level=0`, interpreter **disabled** (opt0 **JIT**) | 3,691 |
| `--opt-level=2`, interpreter disabled (opt2 JIT) | 3,703 |

Findings:

- **opt0 JIT ≈ opt2 JIT (3,691 vs 3,703 ms).** For a large-insn module the JIT
  optimization level barely matters: link is dominated by *base machine-code
  emission* (`target_translate` + reg-alloc + `_MIR_publish_code`) of 680 k insns,
  not by the optional SSA/GVN/LICM passes. So lowering the opt level does **not**
  speed up JIT link here.
- **The interpreter (180 ms) is the only fast path** for large cold scripts — it
  does no codegen.
- Therefore the **>100k-insn auto-downgrade (`:642`) is essentially a no-op:** it
  *does* fire (log: `large module (680490 insns) → opt=0`), but (1) opt0-JIT costs
  the same as opt2-JIT for large modules, and (2) it only calls
  `MIR_gen_set_optimize_level(ctx, 0)` on the MIR context — it does **not** set the
  global `g_js_mir_optimize_level`, so it never engages the interpreter path that
  would actually help. Net effect on lodash link: ~0.

**Revised remedy.** The lever for cold vendor/layout JS is **interpreter mode**
(skip JIT codegen), not the JIT opt level:

1. **Fix the large-module downgrade** to engage the interpreter (set the global, or
   pass `MIR_set_interp_interface` to `MIR_link`) instead of the no-op opt twiddle.
2. **Context-aware policy:** run Radiant/layout vendor scripts in interpreter mode
   (large + cold → tiny exec, so interpreted execution is fine), while keeping the
   JIT for compute-heavy JS (`lambda.exe js`, benchmarks) where exec dominates.
3. Caveat: interpreted execution is slower per instruction — correct for cold libs,
   wrong for hot loops, hence the context split.

This also revises §0.2c's framing: the per-function opt≥2 O(n²) is real for lazy
*JIT*, but moot here because the recommended path is the interpreter, which does no
per-function codegen at all.

**Where this leaves lazy MIR:** unnecessary for the Radiant cold-start problem. The
cheaper, safer levers are (1) **fix the downgrade + interpreter policy** (above),
and (2) §3.4 caching (elides codegen on a hit). Lazy JIT is only worth revisiting
for compute-heavy apps that need optimized code but call only part of it — and then
as coarse batched deferral (Option C, §4.0.1), never per-function opt≥2.

### 0.2e IMPLEMENTED & VALIDATED: interpreter policy for large/cold modules

The §0.2d remedy is implemented (`js_mir_entrypoints_require.cpp`, link site ~679;
threshold `JM_RADIANT_INTERP_INSN_THRESHOLD` in `js_mir_internal.hpp`):

- Count total MIR insns (post-lowering), then select `MIR_set_interp_interface`
  when `total_insns > JM_LARGE_MODULE_INSN_THRESHOLD` (100k, any context) **or**
  `document_context && total_insns > JM_RADIANT_INTERP_INSN_THRESHOLD` (20k).
  `document_context = runtime->dom_doc != NULL` (Radiant/`--document`).
- The generator stays initialized (`g_mir_interp_mode` left 0), so
  `jit_init`/`jit_cleanup` remain paired; only the `MIR_link` interface differs —
  this lets the decision use the *actual* insn count, not a source-size proxy.
- The old ">100k → opt=0" downgrade is kept only as a JIT fallback for when the
  interpreter is disabled (`LAMBDA_JS_LARGE_INTERP=0`).

Benchmark (release, `lambda.exe js`, non-document → only the 100k arm fires):

| Fixture | insns | total before | total after | link before | link after |
| --- | ---: | ---: | ---: | ---: | ---: |
| `underscore_lib.js` | 62k | 1,204 | 1,436 | 985 | 537 (stays JIT, <100k) |
| `ramda_src_min.js` | 304k | 2,451 | **408** | 1,781 | **48** |
| `lib_lodash.js` | 680k | 6,731 | **1,291** | 4,809 | **128** |
| `lib_ajv.js` | >100k | 1,038 | **321** | 520 | **11** |
| `lib_yup.js` | >100k | 850 | **240** | 478 | **14** |
| `lib_acorn.js` | 234k | 1,444 | **374** | 867 | **27** |
| `dom_jquery_lib.js` | 253k | 1,449 | **332** | 949 | **32** |

**4–6× faster total** on large libraries (in a Radiant document context the 20k arm
also catches medium vendor code like underscore).

**End-to-end on the real Radiant suite** (`make layout suite=web-tmpl`, 170
templates each loading vendor JS; wall = parallel wall-clock, CPU = user+sys across
all `lambda.exe` subprocesses = total compile work):

| Build | Interp | Wall | CPU (user+sys) |
| --- | --- | ---: | ---: |
| release | ON (default) | **48.3 s** | **113.5 s** |
| release | OFF (`LAMBDA_JS_LARGE_INTERP=0`) | 84.4 s | 296.4 s |
| debug | ON (default) | **113.9 s** | **398.3 s** |
| debug | OFF | 254.7 s | 920.0 s |

→ release **1.75× wall / 2.6× CPU**; debug **2.24× wall / 2.3× CPU**. The CPU figure
isolates the compile-work reduction from parallel scheduling. (Above measured with
the insn-threshold policy alone.) With the render-command interp default also
enabled — §0.2e — forcing interp for *all* document JS, a re-confirmation run gave
release **35.9 s wall / 120.7 s CPU (interp) vs 109.4 s / 390.6 s (JIT) = 3.05× wall
/ 3.24× CPU**.

Validation:

- **test262: 0 regressions, 0 failures** (39,254 / 39,258 fully passing; the 4
  non-fully-passing are pre-existing batch-flaky tests that pass on retry).
- **Radiant baseline: 5,715 / 5,715, 0 failed** — exercises the document-context
  interpreter path directly; no layout regressions.

Caveat retained: interpreted execution is slower per instruction, correct for cold
vendor/layout JS (tiny exec) but not compute-heavy hot loops — hence the
context+threshold gating and the `LAMBDA_JS_LARGE_INTERP=0` escape hatch.

**Render commands default to the interpreter (link-interface path).** Beyond the
insn-count policy, the `layout`, `render`, and `view` CLI commands set
`g_js_force_document_interp = 1` (`lambda/main.cpp` `default_render_cmd_to_interp()`),
which makes the core_len policy use the interpreter for *all* document JS regardless
of size. `lambda js` (raw JS / benchmarks) and Lambda scripts keep the JIT;
`LAMBDA_JS_LARGE_INTERP=0` overrides back to the JIT.

**Important mechanism detail (found via regression):** this must use the
**link-interface interp** path — `MIR_set_interp_interface` at link with the JIT
generator **still initialized** (`g_mir_interp_mode` left 0) — *not* the pure-interp
path (`g_mir_interp_mode = 1`, which skips `MIR_gen_init`). A first attempt set
`g_mir_interp_mode = 1` and **regressed 49 interactive UI-automation tests**
(TodoMVC-style apps run via `lambda view`); `LAMBDA_JS_LARGE_INTERP=0` (JIT) passed
all 234. Switching to the link-interface path (generator initialized) restored
**234/234** and full Radiant baseline **5712/5712**. Root cause: pure-interp leaves
the generator uninitialized, so paths that still need it (e.g. eval/batch lowering,
which key off `g_mir_interp_mode`, and any on-demand generation) diverge for
interactive JS. Keeping the generator initialized and only swapping the link
interface is both correct and gives the same compile savings. Interpreter *semantics*
are not at fault — the link-interface interp passes interactive tests.

Side note: building `make release` surfaced two **pre-existing** release-only
`-Werror` breaks unrelated to this work, both now fixed by guarding debug-only code:
`radiant/script_runner.cpp` (debug-only timing helpers moved under `#ifndef NDEBUG`)
and `test_js_test262_gtest.cpp` (`cached_native_harness` is used only in the debug
`#ifndef NDEBUG` native-harness branch, so it's marked `[[maybe_unused]]`).

### 0.3 Corrected priority order (post-experiment)

The native-lazy-interface experiment (§0.2b) failed, so it is removed from the
front of the queue, and the §0.2d interpreter finding adds a new #1.
Updated order:

1. ✅ **DONE — Interpreter-mode policy for cold scripts + fixed the broken
   downgrade (§0.2d/§0.2e):** the cheapest, biggest immediate win. Large cold
   vendor/layout JS now runs in the **MIR interpreter** (skip JIT codegen): **4–6×
   faster total** on big libraries (lodash 6.7 s → 1.3 s), 0 test262 regressions,
   Radiant baseline 5,715/5,715. Context-aware (interpreter for document/Radiant +
   very-large modules; JIT for compute-heavy standalone JS).
2. **Area Two — Reduce MIR volume (§3.2 DONE / §3.3 investigated):** MIR-volume
   counters landed (§3.2). The per-opcode breakdown (§3.3) found volume is **88% MOV
   (data movement)**, not calls — so helper extraction is the wrong lever. The real
   fix is a **destination-passing lowering** (≈40% fewer insns), a deep/high-risk
   codegen refactor. **Not recommended for Tune6** given diminishing returns after
   the interp win; treat as a separate codegen-quality project.
3. **Compiled-artifact caching (§3.4): DEPRIORITIZED** (measured, see §3.4 banner).
   Post-interp the realm-safe slice (AST cache) saves only ~5–15%; the valuable
   MIR-module slice is blocked by ~59 baked realm pointers in the lowering. Revisit
   only alongside a de-pointered/relocatable MIR lowering.

**Net (rev 3):** after the interpreter win (#1), the remaining proposal levers
(caching #3, MIR-volume #2) both turn out to be **deep codegen refactors with
uncertain ROI** — caching needs de-pointered relocatable MIR; volume needs
destination-passing lowering. Tune6's headline goal (slow cold vendor-JS compile in
layout) is **substantially met**: ~3× faster web-tmpl suite, all baselines green.
The instrumentation (timing GTest, scope/volume counters, opcode histogram) remains
for any future codegen-quality project.
4. **Lazy generation — only if needed, redesigned (§4):** §0.2c/§0.2d show it is
   largely unnecessary for Radiant (the interpreter already skips codegen entirely).
   Revisit only for compute-heavy apps that need optimized code but call only part
   of it, and only as **coarse batched deferral** (Option C, §4.0.1) — never the
   per-function opt≥2 path (≈O(n²), §0.2c).
5. **Area One — AST (§2): minor cleanup — DONE (cheap items).** The two cheap,
   low-risk items shipped: string-literal no-escape fast path (§2.4) and object-
   literal shorthand de-duplication (§2.6), both validated js262 0-regression. Effect
   is minor as expected (AST <2% of compile). The scope-lookup refactor (§2.1) and
   the symbol/field-id migrations (§2.2/§2.3) remain **not worth doing** unless §0.4
   surfaces an AST-dominated fixture.

### 0.4 Open items / what we learned

- ✅ **Release baseline captured** (§0.1).
- ✅ **`link_us` root-caused** (§0.2a): eager per-function `MIR_gen`, not symbol
  resolution. Resolution is O(n) (Appendix A).
- ✅ **Native lazy interface tested and rejected** (§0.2b): collapses link but
  per-function on-demand generation is ~80× batch cost and ≈O(n²); net-negative.
- ✅ **~80× root-caused** (§0.2c): opt≥2 optimizer passes under lazy *JIT*
  interleaving go ≈O(n²); not the wrapper, not codegen, not GC.
- ✅ **Real fast path identified + earlier conflation corrected** (§0.2d): the win
  is the **MIR interpreter** (skips codegen), not opt=0 JIT (opt0 JIT ≈ opt2 JIT for
  large modules). The **>100k auto-downgrade fires but is a no-op** — it twiddles the
  JIT opt level (ineffective) and never engages the interpreter.
- **Next:** fix the downgrade to engage the interpreter + add a context-aware policy
  (interpreter for Radiant vendor scripts, JIT for compute-heavy JS), then implement
  §3.4 caching. Add §3.2 MIR-volume counters. Drop active pursuit of lazy MIR.
- **Obtain the exact `jquery-1.8.2.min.js`** from the rev-1 table to reconcile the
  45%-AST claim against the ≤2%-AST seen in-tree.

---

## 1. Benchmark First: Focused JS Transpiling GTest

### 1.1 Why this is needed

Current timing signals are spread across:

- `lambda.exe js <file>` benchmark runs, which mix compile and execution policy.
- Radiant `layout` timing, which includes HTML parse, DOM/CSS work, layout,
  rendering output, script loading policy, and page-level lifecycle behavior.
- Ad-hoc `RADIANT_JS_TASK_TIMING=1` logs (handled in
  `radiant/script_runner.cpp`), which are useful for diagnosis but not a stable
  regression benchmark.

Tune6 should start by adding a focused GTest binary that measures JavaScript
frontend and MIR phases directly:

```text
read source
tree-sitter parse
AST build
early checks
import resolution
MIR lowering
MIR link/JIT
top-level execute   (optional; see compile-only mode below)
cleanup
```

The benchmark should report per-file timing and aggregate totals. It should not
decide pass/fail from fixed timing thresholds, because debug and release timing
vary by machine. The GTest should fail only on compile/runtime correctness
errors. Timing output is diagnostic data.

**Capture a release baseline as task 0.** Build with `make release` and record the
per-phase profile for the large fixtures. All later prioritization decisions
reference the release profile. Keep a debug run too, but only to explain
developer-workflow latency, never to justify a perf claim.

### 1.2 Compile-only vs compile+execute

Provide two timing modes:

- **compile-only**: parse → AST → early → imports → MIR → link. This is the clean
  signal for AST/MIR tuning. Many library fixtures have side effects or expect a
  DOM/window environment; executing them pollutes `ast_us`/`mir_us` and can fail
  for reasons unrelated to compilation.
- **compile+execute**: adds top-level execution, used only for fixtures known to
  run standalone (e.g. the awfy/jetstream benchmarks) to guard against execution
  regressions.

Default the large vendor fixtures to compile-only.

### 1.3 Proposed test binary

Add:

```text
test/test_js_transpile_timing_gtest.cpp
test/js/transpile_timing/
```

Build integration:

- Edit `build_lambda_config.json`.
- Run `make` to regenerate build files.
- Do not edit generated `.lua` files manually.

Suggested executable:

```text
./test/test_js_transpile_timing_gtest.exe
./test/test_js_transpile_timing_gtest.exe --gtest_filter=JSTranspileTiming.jquery_1_8_2_min
```

The test should use existing JS runtime entry points (verified to exist):

- `transpile_js_to_mir_len(...)` / `transpile_js_to_mir_core_len(...)`
  (`lambda/js/js_transpiler.hpp`, defined in
  `lambda/js/js_mir_entrypoints_require.cpp`).
- `js_mir_get_last_phase_timing(...)`
- `js_mir_reset_last_phase_timing(...)`

The timing struct is `JsMirPhaseTiming` in `lambda/js/js_runtime.h`. It currently
exposes **nine** fields:

```c
typedef struct JsMirPhaseTiming {
    long parse_us;
    long ast_us;
    long early_us;
    long imports_us;
    long mir_us;
    long link_us;
    long execute_us;
    long cleanup_us;
    long total_us;
} JsMirPhaseTiming;
```

The benchmark must report all nine, not just the six in rev 1. For minified
libraries `early_us` and `imports_us` are not guaranteed negligible and should be
visible. If AST-specific sub-phase detail is needed, extend this struct rather
than adding one-off log parsing.

### 1.4 Benchmark corpus

Use a mixed corpus so Tune6 does not optimize only jQuery:

| Category | Candidate files | Reason |
| --- | --- | --- |
| browser/vendor | `test/js/dom_jquery_lib.js` | large DOM/jQuery-oriented library input already in `test/js` |
| schema libraries | `test/js/lib_joi.js`, `test/js/lib_validator.js`, `test/js/lib_ajv.js`, `test/js/lib_yup.js` | large object/function-heavy libraries |
| parser/compiler libraries | `test/js/lib_acorn.js`, `test/benchmark/octane/typescript-compiler.js`, `test/benchmark/octane/typescript-input.js` | large source with deep function/class structures |
| utility libraries | `test/js/ramda_src.js`, `test/js/ramda_src_min.js`, `test/js/lib_lodash.js`, `test/js/underscore_lib.js` | many closures and higher-order functions |
| web-template vendor JS | selected jQuery, Bootstrap, Raphael, DataTables, Morris files from `test/layout/data/web-tmpl` | mirrors the Radiant bottleneck |
| execution-heavy small benchmarks | `test/benchmark/awfy/*_bundle.js`, `test/benchmark/jetstream/*.js` | checks that compile tuning does not regress normal JS benchmarks (compile+execute mode) |

For tidiness, copy a small curated set of web-template vendor scripts into:

```text
test/js/transpile_timing/vendor/
```

Suggested initial vendor set:

```text
jquery-1.8.2.min.js
jquery-1.10.2.js
jquery-1.11.0.min.js
bootstrap.min.js
raphael-2.1.0.min.js
morris.js
jquery.datatables.js
modernizr-latest.js
```

The copied files should be exact snapshots from `test/layout/data/web-tmpl`, with
a README noting the source path and byte size. The reason to copy is not to fork
the fixtures semantically; it is to avoid benchmark tests depending on the
web-template directory layout.

Note: `dom_jquery_lib.js` is also the fixture behind a recently-resolved baseline
golden flake. The timing test must depend only on **compile success**, never on
layout output, so it cannot reintroduce that fragility.

### 1.5 Output format

Use a plain tabular diagnostic line per file (all nine phases):

```text
JS_TRANSPILE_TIMING file=jquery-1.8.2.min.js bytes=93435 mode=compile-only build=release parse_ms=2.1 ast_ms=110.4 early_ms=1.2 imports_ms=0.3 mir_ms=140.7 link_ms=70.0 exec_ms=0.0 cleanup_ms=3.0 total_ms=327.7
```

The line must include `build=` (debug/release), `mode=` (compile-only /
compile+execute), and the active feature flags (lazy MIR, cache), so a copied log
line is self-describing.

For machine-readable tracking, optionally support:

```text
JS_TRANSPILE_TIMING_JSON=./temp/js_transpile_timing.jsonl
```

Temporary output must go under `./temp/`, not `/tmp`.

### 1.6 Useful counters to add

Timing alone will not explain why AST build is slow. Add optional counters gated
by an env var such as `JS_AST_TIMING=1`. **Counters perturb timing, so run them as
a separate pass — never inside a measured timing run.**

| Counter | Why |
| --- | --- |
| total AST nodes built | normalizes timing by tree size |
| count by Tree-sitter symbol | shows dominant syntax nodes |
| identifier count | identifies name-pool/scope pressure |
| **scope-lookup calls and total entries scanned** | **directly tests the O(n²) hypothesis in §2.2** |
| string literal decode count and bytes | catches heavy unescape/copy paths |
| `ts_node_type()` calls | measures string-based classification overhead |
| field lookup calls | measures `ts_node_child_by_field_name` pressure |
| AST allocation bytes | detects allocation shape and node bloat |
| functions declared vs functions called | baseline for lazy MIR |
| MIR functions emitted | direct measure of generated-code reduction |

The scope-lookup counter is the most important new one: it confirms or refutes
the primary AST hypothesis before any refactor is attempted.

---

## 2. Area One: Reduce AST Build Time

> **rev 3 status: DEMOTED to minor cleanup.** The §0 benchmark shows AST build is
> <2% of compile time on every in-tree fixture, so none of the work in this
> section is justified on current evidence. Do it only if §0.4 surfaces an
> AST-dominated fixture or a release profile that inverts the ratio. The analysis
> below is retained for that contingency. If any of it is done opportunistically,
> prefer the cheap, low-risk items (§2.4 string-literal fast path, §2.6 duplicate
> shorthand removal); skip the scope refactor (§2.1) and symbol/field-id
> migrations (§2.2/§2.3) until the data calls for them.

The JavaScript AST builder is `lambda/js/build_js_ast.cpp` (~4,500 lines). It
consumes a Tree-sitter CST and creates a LambdaJS-specific AST. The cost centers
below were listed in priority order under the rev-2 assumption that AST build was
hot; rev 3's measurements supersede that ordering (see §0.3).

### 2.1 Scope lookup is O(n²) — confirmed superlinear, but immaterial (do not fix yet)

> **rev 3:** the §1.6 counter (now `JS_AST_COUNTERS`) confirmed the superlinear
> behavior — average `NameEntry` comparisons per lookup rose 22.5 → 48.5 → 62.5
> with fixture size. **But** the AST phase it lives in is <2% of compile time, so
> this refactor would not measurably help. It also turned out the builder does
> far fewer lookups than feared (6k–17k total, not one per identifier). Keep this
> as a documented opportunity; do **not** implement it unless a release profile or
> a new fixture (§0.4) shows AST as a real cost. The analysis below stands as the
> rationale for the eventual fix.

`build_js_identifier()` resolves a binding inline for every identifier
(`build_js_ast.cpp:452`):

```c
identifier->entry = js_scope_lookup(tp, identifier->name);
```

`js_scope_lookup()` (`lambda/js/js_scope.cpp:49`) is a **linear scan over a linked
list of `NameEntry`, walking up each parent scope**:

```c
while (scope) {
    NameEntry* entry = scope->first;
    while (entry) {                       // linear scan
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0)
            return entry;
        entry = entry->next;
    }
    scope = scope->parent;
}
```

For a minified library, a single function/IIFE scope accumulates hundreds to
thousands of `NameEntry`. Each identifier reference scans that list, giving
**O(scope_size) per reference → O(n²) overall** in identifier-dense code — which
the rev-3 counters confirmed. The cost is algorithmic, not constant-factor; the
reason it is nevertheless not worth fixing now is simply that the AST phase is
tiny (§0).

The same `js_scope_lookup()` is also called repeatedly during MIR lowering
(`js_mir_function_collection_class_inference.cpp:112`,
`js_mir_statement_lowering.cpp:3207`, `js_mir_expression_lowering.cpp:9172`, …).
So **one fix helps both `ast_us` and `mir_us`.**

Proposed fix (root-cause, per project rule "fix the root cause, do not work
around"):

- Replace the per-scope linked-list with a hash-backed lookup. The `JsScope`
  struct (`js_transpiler.hpp:30`) keeps `first`/`last` for ordered iteration; add
  a `HashMap` (from `lib/hashmap.h`) keyed by interned name for O(1) lookup, while
  preserving insertion order for declaration semantics. Names are already
  interned via the name pool, so the key can be the interned pointer/length.
- Validate against the `scope-lookup calls / entries scanned` counter from §1.6:
  the "entries scanned" total should collapse after the change.

This is gated by the §1.6 counter: implement the counter, confirm the quadratic
behavior on a large minified fixture, then make the change. If the counter shows
scope scanning is *not* dominant in release, fall back to the mechanical wins
below as the primary lever instead.

An alternative, deferring lookup out of AST build entirely, is described in §2.5;
the hashmap fix is simpler and lower-risk and should be tried first.

### 2.2 Replace node-type strings with numeric symbols — with a real symbol table

Many builders classify nodes with `strcmp(ts_node_type(node), "...")`
(~80 sites). Switching on `ts_node_symbol(node)` is faster and profiles cleaner:

Current pattern:

```c
const char* node_type = ts_node_type(node);
if (strcmp(node_type, "identifier") == 0) { ... }
```

Proposed pattern:

```c
TSSymbol sym = ts_node_symbol(node);
switch (sym) {
    case sym_identifier:
        ...
        break;
}
```

**Correction to rev 1.** The `sym_*` defines currently in `build_js_ast.cpp`
(around lines 64–101, under `#ifndef sym_number`) are **hand-written stubs**, not
values generated from the tree-sitter-javascript grammar, and only two
`ts_node_symbol()` call sites exist today (`build_js_ast.cpp:2134`, `:2696`).
Switching a `switch` onto stub constants that may not match the parser's actual
symbol ids would introduce silent miscompiles.

So this is **not** "mostly mechanical." It requires:

1. Sourcing the authoritative symbol ids from the generated JS parser
   (the tree-sitter-javascript `parser.c` symbol enum), not the local stubs.
2. A guard/check so a future grammar regeneration cannot silently desync the ids
   (e.g. a static assertion or a generation step analogous to `ts-enum.h` on the
   Lambda grammar side).
3. Converting the hot builders first (`build_js_expression`, `build_js_statement`,
   identifier/member/call/binary), measured against the corpus.

Expected effect: less string work, better branch shape, explicit node categories.
Risk is real (id desync), hence the correctness gate above.

### 2.3 Cache field ids instead of field-name lookup

`ts_node_child_by_field_name(..., strlen("..."))` is called 121 times, ~70 of them
recomputing `strlen` each call. A few sites already pass hard-coded lengths
(`build_js_ast.cpp:516-518`).

Proposed approach — resolve field ids once from the JS language and use field-id
child access:

```c
static TSFieldId field_left, field_right, field_function, field_arguments;
...
field_left = ts_language_field_id_for_name(lang, "left", 4);
```

Expected effect: removes repeated string hashing/search inside Tree-sitter for
binary expressions, calls, members, classes, functions, imports, exports,
destructuring, and assignments. Lower risk than §2.2 (field ids are queried from
the live language, not hard-coded).

### 2.4 Fast path for **string-literal** decoding (identifier fast path already exists) — DONE

> **rev 3: implemented.** `build_js_literal` (`build_js_ast.cpp`) now `memchr`s the
> string body for `\`; if none, it interns directly from the source slice and
> returns, skipping the temp-buffer alloc/copy/free. Escaped strings keep the
> existing decode path. Validated: js262 **0 regressions**; targeted test confirms
> `\t \n \x42 \u{1F600}` and unescaped strings decode identically. AST time
> equal-or-slightly-lower (minor, as expected — AST is <2% of compile).


**Correction to rev 1.** The identifier no-escape fast path is already
implemented: `js_decode_identifier_name()` (`build_js_ast.cpp:387`) does
`memchr(source, '\\', len)` and interns directly when no escape is present
(line 389). No work needed there.

The remaining copy cost is in **string-literal** decoding. `build_js_literal()`
(`build_js_ast.cpp:281-363`) always does `mem_alloc` temp buffer → escape-process
→ `name_pool_create_len` → `mem_free`, even for literals with no escapes. Add the
analogous fast path:

1. Scan the literal body (minus quotes) for `'\\'`.
2. If absent, intern the slice directly via `name_pool_create_len`, skipping the
   temp buffer entirely.
3. Only take the allocate-and-decode path when an escape is present.

Correctness requirement: escaped and unescaped spellings that denote the same
string must still canonicalize identically; line-continuation and quote handling
must be preserved.

Expected effect: removes a heap alloc/copy/free per clean string literal — common
in minified libraries.

### 2.5 (Optional) Defer scope lookup out of AST construction

If the §2.1 hashmap fix is insufficient, or to enable function-scoped lazy
compilation later, separate syntax from semantics:

- AST build records identifier name and source range only.
- A later semantic pass resolves bindings and reports undefined/global usage.
- Per-identifier missing-binding logging is gated behind a specific env var.

Risk: some lowerers assume `identifier->entry` is populated immediately
(`build_js_ast.cpp:452`, and the MIR-side lookups). Migration must add lazy
accessors or a sentinel state first. Treat this as a larger follow-up, not a
Phase B item — §2.1 captures most of the win with far less churn.

### 2.6 Remove duplicate shorthand-property nodes — DONE (object literal)

> **rev 3: implemented for object literals.** `{x}` now builds the identifier once
> and shares it for `property->key` and `property->value` (`build_js_ast.cpp:~809`),
> removing a duplicate identifier build + scope lookup per shorthand property.
> Verified safe: lowering consumes the key read-only (`key->name`) and the value as
> an expression, and never mutates AST nodes. The **destructuring** shorthand site
> (`{x}` pattern, `build_js_ast.cpp:~2046`) was intentionally **left as-is** — there
> the value is an lvalue binding target, so node-sharing carries lvalue-semantics
> risk not worth a <1% win. Validated: js262 **0 regressions**; shorthand object +
> both destructuring forms produce correct output.


Object shorthand and shorthand destructuring build the same node twice — once for
key, once for value (`build_js_ast.cpp:809-812` and `:2046-2047`):

```c
property->key   = build_js_expression(tp, property_node);
property->value = build_js_expression(tp, property_node);   // duplicate build
```

Build the identifier once and share it (or set an explicit `shorthand` flag and a
single key node), eliminating one full identifier build — including its scope
lookup — per shorthand property. Low risk, do it in Phase B.

### 2.7 Compact common AST nodes (later, counter-driven)

Reduce memory/init for the hottest nodes (identifiers, members, calls, literals,
binaries): store source offsets/lengths instead of full `TSNode` where location is
enough, avoid storing both raw source and decoded name when one is recoverable.

This is not an early change — AST-shape bugs are subtle. Do it only after the
§1.6 counters show node allocation is a real bottleneck in release.

### 2.8 Combine top-level collection passes where safe

The MIR path performs several whole-AST scans (declarations, hoists, imports,
classes, captures, implicit globals). Simple top-level metadata can be collected
during AST build to avoid rediscovery:

- top-level function/class declarations,
- top-level `var`/`let`/`const`,
- imports/exports,
- static field declarations,
- function body source ranges,
- direct child function declarations.

Do not move capture propagation into AST build — that still needs semantic
context. The short-term win is avoiding repeated rediscovery of simple top-level
metadata. (This also produces exactly the body-range/metadata needed by lazy MIR
in §4.)

---

## 3. Area Two: Reduce MIR Code

### 3.1 Current issue

The current pipeline is **eager**: `jm_collect_functions` gathers every function
(`js_mir_module_batch_lowering.cpp:1912`,
`js_mir_function_collection_class_inference.cpp:395`), then a loop calls
`jm_define_function` on all of them before `js_main` is emitted
(`js_mir_module_batch_lowering.cpp:3862-3865`). For application code this is fine.
For third-party browser libraries it is expensive because:

- libraries declare hundreds or thousands of functions,
- layout tests often need only top-level init plus a tiny fraction of
  DOM-ready/plugin code,
- many functions are feature branches for browsers Radiant does not emulate,
- much generated MIR is linked but never executed.

Reducing MIR code helps three ways: less lowering work, fewer/smaller functions to
link, and less startup initialization.

Note an existing interaction: adaptive optimization thresholds already exist
(`transpile_js_mir.cpp:50`, `JM_LARGE_FUNC_INSN_THRESHOLD` 10k,
`JM_LARGE_MODULE_INSN_THRESHOLD` 100k) and downgrade opt level on large
functions/modules. Anything that changes module size (lazy MIR, helper
extraction) will shift which fixtures cross these thresholds, so re-check them
when measuring.

### 3.2 Track generated MIR volume — DONE (counters landed + measured)

Implemented `JsMirVolumeCounters` (`functions_discovered`, `mir_insns_emitted`) in
`js_runtime.h` / `js_mir_entrypoints_require.cpp`, populated in core_len and printed
as `JS_MIR_VOLUME file=… functions=… mir_insns=…` under `JS_TRANSPILE_TIMING=1`.

Measured (release, interp default):

| Fixture | funcs | MIR insns | mir_ms | insns/fn |
| --- | ---: | ---: | ---: | ---: |
| `underscore_lib.js` | 333 | 62,258 | 70 | 186 |
| `lib_yup.js` | 476 | 119,203 | 91 | 250 |
| `dom_jquery_lib.js` | 616 | 253,474 | 210 | 411 |
| `lib_ajv.js` | 263 | 120,718 | 61 | 459 |
| `ramda_src_min.js` | 581 | 303,890 | 286 | 523 |
| `lib_acorn.js` | 340 | 233,704 | 129 | 687 |
| `lib_lodash.js` | 701 | 680,490 | 959 | 970 |

Findings: `mir_ms` ≈ linear in insn count (~1.4 µs/insn), so cutting emitted MIR
reduces lowering time proportionally. **insns/function spans 186–970** — lodash
emits ~970 MIR insns/func, evidence of verbose inline lowering. The per-opcode
breakdown of *what* that MIR is follows in §3.3 (answer: ~88% data-movement MOVs,
not calls — so the lever is a destination-passing lowering, not helper extraction).

### 3.3 Reduce MIR volume — DEFERRED TO FUTURE (design below)

> **rev 3 per-operation breakdown (measured) — the helper-extraction premise is
> wrong; the volume is data movement.** A per-opcode histogram at the `jm_emit`
> chokepoint (env `JS_MIR_OPCODE_HIST`, `js_mir_hashmap_scope_utils.cpp`) shows:
>
> | Workload | mov | call | branches | other |
> | --- | ---: | ---: | ---: | ---: |
> | `lib_lodash.js` | **87.9%** | 6.9% | 3.3% | 1.9% |
> | 500-elem array literal | 64.8% | 34.4% | 0.6% | — |
> | 500-key object literal | 65.2% | 34.2% | 0.5% | — |
> | `a=a+i` ×50 | 37% | 43% | — | ~20% |
> | tiny `f(a,b){return a+b}` | 30% | 53% | 9% | — |
>
> **MOV (data movement) dominates (66–88%); calls are secondary (7–34%).** So
> extracting verbose idioms into helpers (calls) targets the *wrong* thing. The MOVs
> come from pervasive value materialization — roughly **2 MOV per value op** (one to
> materialize into a temp, one to place into the destination/var/arg slot), plus
> call-arg setup. Small functions are call-heavy; large minified library functions
> are MOV-heavy (lodash 88%), and `mir_ms` is ~linear in insn count, so MOVs are the
> lever for both lowering time and interp execution.
>
> **What would actually reduce it: a destination-passing lowering** (compute each
> expression directly into its target register instead of temp-then-MOV), which
> could roughly halve MOVs → ~40% fewer total insns. That is a **deep, invasive,
> high-risk refactor** of the entire expression lowering (every lower-expression
> path threads a destination), requiring full js262 + Radiant re-validation. A
> `jm_emit`-level peephole can't safely coalesce across the opt=0/SSA-less flow.
>
> **Recommendation: do not pursue MOV-reduction as part of Tune6.** It is a
> codegen-architecture project disproportionate to "tuning," with uncertain ROI now
> that interp (§0.2e) already banked the headline win (3× suite). The MOVs are
> largely structural to the "materialize each value" lowering style, and most pages
> are far smaller than lodash. Treat destination-passing as a separate, scoped
> codegen-quality effort if ever justified by profiles. The original
> helper-extraction idea (below) does not match the data and is retired.

#### 3.3.1 Deferred design: destination-passing lowering

This is the recommended (future) way to cut the ~88% MOV volume. It is recorded
here so a later effort can pick it up; it is **not** scheduled for Tune6.

**Problem.** The current expression lowering is value-returning: each
`jm_lower_expression(mt, node)` allocates a fresh temp register, computes the result
into it, and returns it. The caller then `MOV`s that temp into wherever the value
actually belongs (a variable slot, a call-argument slot, an operand of the parent
op, the return register). That is the structural source of "~2 MOV per value op":
one MOV to materialize, one MOV to place. For leaf nodes (identifier reads,
literals) the materialize MOV is itself often redundant.

**Idea.** Make lowering *destination-passing*: the caller tells the callee where the
result must end up, and the callee emits code that writes the result **directly**
into that destination — eliminating the trailing placement MOV (and, for leaves,
the materialize MOV).

**Sketch.**

```c
// A destination descriptor: where a lowered expression should write its result.
typedef enum { JM_DEST_ANY, JM_DEST_REG, JM_DEST_DISCARD } JmDestKind;
typedef struct { JmDestKind kind; MIR_reg_t reg; } JmDest;

// New core entry; the old value-returning form becomes a thin wrapper that
// passes JM_DEST_ANY and returns the chosen reg (preserving behavior for
// not-yet-converted callers).
void      jm_lower_expr_to(JsMirTranspiler* mt, JsAstNode* n, JmDest dest);
MIR_reg_t jm_lower_expr   (JsMirTranspiler* mt, JsAstNode* n);  // wrapper: dest=ANY
```

- `JM_DEST_ANY`: callee may pick/return a register (today's behavior; fallback for
  unconverted paths and for values needed in a reusable reg, e.g. used twice).
- `JM_DEST_REG`: callee must leave the result in `dest.reg` — no extra MOV.
- `JM_DEST_DISCARD`: result unused (expression statements) — skip the result MOV
  entirely, and let dead-value elimination drop more.

**Where it pays (convert these first, highest MOV density):**

1. **Assignment** `x = expr` → lower `expr` with `dest = slot/reg of x`. Removes the
   assign MOV.
2. **Call arguments** `f(a, b, …)` → lower each arg with `dest =` its arg slot.
   Removes one MOV per argument (call-arg setup is a large MOV source).
3. **`return expr`** → lower with `dest =` the return reg.
4. **Leaves** (identifier read, literal) → write straight into `dest` instead of
   temp-then-return. Removes the materialize MOV.
5. **Binary / unary** → emit the op's output operand as `dest` directly.
6. **Array/object literal elements** → lower each element with `dest =` the
   element's target slot (these are ~2 MOV/element today, §3.3 table).

**Correctness constraints.**

- All control-flow paths must write `dest` (short-circuit `&&`/`||`/`?:`, `try`,
  exceptions/`?` propagation). Where a path can't, fall back to ANY + one MOV.
- A value needed more than once (or needed in a specific reg class) uses ANY and is
  MOV'd as today — destination-passing is an optimization, never a requirement.
- Boxing/tagging stays identical; only the *placement* changes.

**Rollout & validation.**

- Behind a flag (e.g. `JS_DEST_PASSING=1`), convert paths incrementally, leaving the
  ANY wrapper for everything not yet converted (so partial conversion is always
  correct). Measure MOV delta per step with `JS_MIR_OPCODE_HIST` and total insns
  with `JS_MIR_VOLUME`.
- Gate on full `make test262-baseline` + `make test-radiant-baseline` green at each
  step; this changes generated code so re-validation is mandatory.
- Expected: ~40–50% fewer MOVs → ~30–40% fewer total insns → proportional
  `mir_ms` + interp-execution reduction. Unlike caching, this helps **single** page
  loads too, not just the repeated-compile suite.

**Scope estimate:** large — touches every expression-lowering path in
`js_mir_expression_lowering.cpp` (and assignment/return/arg sites in
`js_mir_statement_lowering.cpp`). A multi-step project, not a tuning patch.

### 3.4 Compiled-artifact caching across the test batch and run — DEFERRED TO FUTURE *(replaces native vendor installers)*

> **rev 3 feasibility finding (measured): caching is NOT a tractable near-term win
> after the interpreter change, and is deprioritized.** Three measurements:
>
> 1. **Duplication is real** — across web-tmpl: `bootstrap.min.js` ×116, jquery
>    variants ×58/45/30/27…, modernizr ×44 (byte-identical within a version). So the
>    suite recompiles identical vendor files many times.
> 2. **But the cacheable cost shrank.** The interpreter (§0.2e) already removed JIT
>    codegen — caching's pre-interp premise ("elides the entire generate+link cost")
>    is moot. Post-interp the remaining per-file cost is parse+ast+**mir-lowering**,
>    and mir-lowering dominates (jquery: parse+ast 37 ms vs mir 210 ms; lodash 30 ms
>    vs 957 ms). So the **realm-safe AST-level cache saves only ~5–15%** — low value.
> 3. **The valuable slice (MIR-module reuse) is blocked by realm-specific pointers.**
>    MIR *can* serialize (`MIR_write_module`/`MIR_read`), but the JS→MIR lowering
>    bakes ~**59 raw realm pointers** into modules as integer constants — interned
>    `String->chars` (`js_mir_expression_lowering.cpp:2069`), `ctor_prop_ptrs`,
>    `shape_cache_ptr`, inline caches (`js_mir_calls_boxing_types.cpp:566,1192,…`).
>    These are valid only in the creating realm/process, so a deserialized or
>    cross-realm-reused module would dereference stale pointers. Making it safe needs
>    de-pointering all 59 sites into symbolic/link-resolved references — a large,
>    high-risk codegen refactor — and some (shape/inline caches) are *inherently*
>    per-realm runtime state.
>
> **Recommendation:** do not build caching now. The better next lever is **reducing
> MIR-lowering cost** (§3.2/§3.3) — it cuts the dominant remaining per-file cost and,
> unlike caching, helps *single* page loads (real product use), not just the
> repeated-compile test suite. Revisit caching only if a de-pointered, relocatable
> MIR lowering is pursued for other reasons. Original design retained below.

The native vendor installer idea from rev 1 is **removed**. Hand-written
jQuery/Bootstrap/Modernizr compat layers are a rule-#1 work-around: they fake
semantics, rot under maintenance, and hollow out the layout fixtures (the test
would validate the hand-rolled clone, not the engine). The Radiant startup win is
instead obtained by **not recompiling the same script repeatedly**.

The web-template corpus and the JS test suites repeat the same vendor files many
times. Today only **source** caching exists
(`radiant/script_runner.cpp:561 load_script_content_with_source_cache`); there is
no parse/AST/MIR artifact cache. Add content-hash caching of the compiled result:

```text
key   = sha(source bytes) + runtime/grammar version + feature flags
value = reusable compiled artifact
```

Two concrete scopes, in increasing ambition:

1. **Per JS test batch / whole layout-or-test run cache (primary target).** Within
   a single process or test run, when the same `(hash, version, flags)` script is
   loaded again, reuse the prior compiled module instead of re-running
   parse→AST→MIR→link. For the layout suite, where one jQuery file appears across
   many pages, this removes nearly all redundant compilation. This is the biggest,
   lowest-risk Radiant win and should be implemented before lazy MIR and before
   any AST micro-tuning ships to Radiant.

2. **Cross-run persistent cache (later).** Persist artifacts under `./temp/`
   keyed by hash+version. Useful for repeated CI/dev runs.

Cache levels, safest first:

1. source hash + metadata,
2. Tree-sitter parse tree / compact syntax summary,
3. AST template,
4. MIR module template,
5. linked code — **only** after a lifetime/realm-safety audit.

Critical constraint: any cached artifact must not retain realm-specific pointers
(pools, name pools, runtime globals, document objects). The MIR module and runtime
currently reference such state, so reuse must either rebind those references per
realm or restrict caching to the levels that own no realm-specific heap. Start
with AST/MIR-template caching that is realm-independent; gate linked-code reuse on
the audit. The cache key must include the runtime/grammar version so a rebuild
never serves a stale artifact.

This is opt-in until proven:

```text
JS_COMPILE_CACHE=1
```

---

## 4. Area Three: Lazy Function MIR

> **rev 3:** MIR's native per-function lazy interface was **tested and rejected**
> (§0.2b / §4.0): it craters `link_us` but makes per-function generation ~80×
> more expensive (≈O(n²)), regressing real libraries up to 44× and timing out
> lodash. Lazy generation remains desirable but needs a different design (§4.0.1).
> The custom call-ABI design in §4.1–§4.10 is kept for reference; note the ABI was
> *not* the blocker — generation-path cost was.

### 4.0 Tested and rejected: MIR's native lazy gen interface

The obvious move is to swap the eager interface
(`MIR_set_gen_interface`, `js_mir_entrypoints_require.cpp:664`) for MIR's drop-in
lazy sibling `MIR_set_lazy_gen_interface` (`ref/mir/mir-gen.c:9779`):

```c
// eager (current): MIR_gen() runs for every function inside MIR_link
MIR_link(ctx, MIR_set_gen_interface, import_resolver);
// lazy: wrapper thunk per function; MIR_gen() on first call, then redirect
MIR_link(ctx, MIR_set_lazy_gen_interface, import_resolver);
```

This was implemented behind `JS_LAZY_MIR=1` and benchmarked. It is **net-negative**
— see §0.2b for the full table. Summary: `link_us` collapsed (e.g. lodash
4,804 ms → gone) but `exec_us` exploded (ramda 11 ms → 96,676 ms; lodash timed out
>180 s). A synthetic test of 2000 trivial functions each called once isolates the
cause: **on-demand generation costs ~88 ms/function vs ~1.1 ms/function in batch
(~80×), scaling ≈O(n²).** It is not regeneration (hot-loop micro-tests don't
regress) and not the ABI (direct and higher-order both fine) — it is a large
per-function fixed overhead in MIR's lazy generation path that the batch loop
amortizes.

The flag remains in the tree (default off) as an experiment harness, marked
non-viable.

### 4.0.1 What a viable lazy design would need

Given §4.0, deferring generation requires avoiding per-function on-demand cost:

- **Option C — coarse batched deferral (preferred):** at load, skip codegen
  entirely (link with no gen interface, or interp). Then on a later tick (e.g.
  DOM-ready, or after the first N runtime calls), collect the set of functions
  actually reached and **batch-generate them in one pass** via the cheap
  `MIR_set_gen_interface` batch path — never one-at-a-time. This keeps the per-
  function unit cost at batch rates while still skipping never-reached functions.
- **Option D — fix MIR's lazy unit cost:** profile why `generate_func_and_redirect`
  / `_MIR_get_wrapper` per call is ~80× the batch unit cost (suspected: generator
  context setup or module-proportional work per generation) and reduce it. If the
  per-function overhead becomes ~constant, the native interface becomes viable.

Both are larger than a flag swap and are gated behind the cheaper near-term wins
(caching §3.4, MIR-volume reduction §3). The original §4.1–§4.10 custom machinery
is a third, heaviest option and is not recommended given the ABI turned out to be a
non-issue.

### 4.1 The call ABI is hybrid (verified) — not the blocker

> Retained for context. §0.2b showed the ABI is *not* what makes lazy hard (both
> direct and indirect calls behave correctly under the lazy interface); the
> generation-path cost is. The mechanics below are still accurate.

Before designing lazy MIR, the inter-function call ABI was checked in the code.
**Inter-function calls are a hybrid:**

- **Direct MIR calls** for statically-known callees — the common library case
  (`function foo(){}` called as `foo()`). The transpiler emits
  `MIR_new_ref_op(fc->func_item)` into an `MIR_CALL`
  (`js_mir_expression_lowering.cpp:9454-9530`, plus a native-typed direct path at
  `:9399-9437`). Forward references work via `MIR_new_forward` stubs created for
  all functions up front (`js_mir_module_batch_lowering.cpp:3843-3860`).
- **Indirect calls** via the `js_call_function` runtime helper for first-class,
  dynamic, or reassigned callees (`js_mir_expression_lowering.cpp:9535-9717`).
  Function values are boxed Items created by `js_new_function` / `js_new_closure`
  (`:11440-11620`), which already hold a code pointer.

**Implication:** naive lazy MIR is **not** feasible. A direct caller emits a
reference to the callee's `MIR_item_t`, which must be a *defined* function at link
time. If the body is deferred, the direct call cannot link. The `MIR_new_forward`
mechanism only solves ordering within a module where all bodies are eventually
defined; it does not allow a permanently-undefined body.

Two viable designs follow; pick per the Phase A measurement and call-graph data.

### 4.2 Design option A — lazy stub trampoline (preserves direct ABI)

For each deferrable function, emit at module-build time a **tiny stub** MIR
function under the real symbol name, instead of the full body. The stub calls a
runtime helper:

```text
js_lazy_compile_and_call(lazy_record, this, args_ptr, argc)
```

which on first call compiles the real body (into a separate MIR module), links it,
patches the function object's entry pointer, and forwards the call. Direct callers
still reference the existing symbol (the stub), so the direct-call ABI is
untouched. The body — the expensive part — is lowered only on first call. The
lazily-compiled body module imports peer symbols by name for its own calls (MIR
cross-module linking).

Pros: keeps fast direct calls in steady state; MIR lowering/link cost drops with
body size. Cons: more machinery (per-function stub, separate-module compile +
link + import resolution, entry patching).

### 4.3 Design option B — indirect-in-lazy-mode (simpler v1)

When `JS_LAZY_MIR=1`, route statically-known calls through the existing
`js_call_function` indirect path instead of emitting a direct ref. The function
object starts in an uncompiled state; the first `js_call_function` triggers
compilation and patches the entry pointer. No stub/trampoline or cross-module
import is needed — the indirection already exists.

Pros: reuses the existing indirect dispatch; far less new machinery for a first
prototype. Cons: gives up direct-call speed for *all* functions while lazy mode is
on, even hot ones. Acceptable for the Radiant cold-library goal (those functions
are rarely called), but it would regress call-heavy benchmarks, so it must stay
behind the flag and not become default.

Recommendation: prototype **B** first to measure the lowering/link savings cheaply
and validate semantics, then move to **A** if steady-state call speed matters
once the savings are proven.

### 4.4 Lazy function record

Add a runtime/transpiler-side record (project allocation and project containers,
not STL):

```c
typedef struct JsLazyFunction {
    const char* source;
    size_t source_len;
    uint32_t body_start;
    uint32_t body_end;
    uint32_t params_start;
    uint32_t params_end;
    JsAstNode* body_ast;      // null until materialized
    JsFunctionMeta* meta;
    JsScopeSnapshot* scope;
    void* compiled_entry;
    uint8_t state;
} JsLazyFunction;
```

It must carry enough to compile later without freed parser state: source pointer
and lifetime owner; byte ranges for body and params; function kind (normal,
arrow, method, constructor, generator, async); strict-mode status; lexical outer
scope/capture metadata; realm/runtime pointer; module/global environment
reference; original filename/source-map location. Much of this is exactly the
top-level metadata §2.8 can collect during AST build.

### 4.5 Function object state machine

```text
JS_FUNC_LAZY_UNCOMPILED
JS_FUNC_LAZY_COMPILING
JS_FUNC_LAZY_COMPILED
JS_FUNC_LAZY_FAILED
```

`COMPILING` prevents recursive re-entry compiling the same function twice.
`FAILED` stores the compile exception so repeated calls observe identical failure
behavior.

### 4.6 What remains eager

- top-level statements,
- imports/exports/module linking,
- hoisted declaration registration,
- class declaration shape,
- function names and arity,
- parameter list metadata,
- capture classification sufficient to create closures,
- direct eval hazards,
- syntax errors.

Important: JavaScript reports **syntax errors at parse time**, not first call.
Lazy MIR must not mean lazy parsing for syntax validity — Tree-sitter must already
have parsed the whole source successfully. We defer AST body materialization and
MIR lowering, not parsing.

### 4.7 Capture and scope handling

Lazy MIR is only safe if closure environments are represented independently of
generated code (they already are: `js_new_closure` boxes an environment,
`:11440-11620`). At eager load: build enough metadata to know captured outer
bindings, allocate/reference the correct lexical environment, store the scope
snapshot on the function object. At first compile: rehydrate/finish the body AST,
resolve identifiers against the stored snapshot plus local scope, and generate MIR
using the same capture slots the eager path would have used.

Capture analysis stays eager for now (the current pipeline already does whole-
module capture analysis in Phases 1.5–1.7c). Making capture analysis incremental
is a later, separate step — Tune6 must not change multiple semantic layers at
once.

### 4.8 Direct eval and dynamic hazards

- If a function or any ancestor scope contains direct `eval`, compile it eagerly.
- If the script uses `with`, compile affected scopes eagerly.
- If arguments/parameter aliasing creates special environment behavior, use the
  eager path until explicitly supported.

This still leaves most vendor library functions eligible for lazy MIR.

### 4.9 Top-level call batching

Avoid making startup slower by compiling tiny functions one at a time as top-level
code calls them:

- Compile top-level body eagerly.
- Allow first-call lazy compilation for locally declared functions.
- If link fragmentation proves expensive, batch: collect pending lazy functions
  reached during top-level execution, lower/link them in one MIR module, patch all
  entry points, resume. (Option A naturally supports batching; Option B can batch
  by deferring the first compile of several pending callees.)

Start simple and measured:

```text
lazy compile one function on first call
record timing
record whether link cost is too fragmented
```

### 4.10 Interaction with caching

Lazy MIR (§4) and compiled-artifact caching (§3.4) are complementary and both
generic — neither fakes semantics:

1. If the compile cache has a valid artifact for the script, reuse it.
2. Otherwise compile top-level eagerly and lazily compile function bodies.
3. If lazy MIR is disabled, use the current eager path.

The cache should store whatever lazy MIR produced (top-level module + lazy
records), so a cached script also benefits from cold-body deferral.

Feature flags:

```text
JS_LAZY_MIR=1
JS_COMPILE_CACHE=1
```

Both opt-in until js262, benchmark, and layout coverage are stable.

---

## 5. Implementation Phases

> **rev 3 ordering.** Phase A is **done** (benchmark, counters, release baseline —
> §0). The data (§0.3) reorders what follows: do **MIR-volume counters → lazy MIR
> (option B) → link-cost investigation → caching** next. The "Phase B: AST Builder
> Speedups" below is **deferred** — keep it only as a contingency if §0.4 surfaces
> an AST-dominated fixture. Treat the phase letters below as a menu re-sequenced by
> §0.3, not a literal A→E order.

### Phase A: Measurement Infrastructure (+ release baseline + ABI confirmation) — DONE

Status: the timing GTest, `JsScopeCounters`, the `JS_TRANSPILE_TIMING` CLI surface,
and the release baseline (§0.1) all landed. Remaining sub-items kept for context.

### Phase A (original task list)

Tasks:

1. Add `test/test_js_transpile_timing_gtest.cpp` with compile-only and
   compile+execute modes.
2. Add curated fixtures under `test/js/transpile_timing/`.
3. Report all nine `JsMirPhaseTiming` fields plus build mode / mode / flags.
4. Add optional JSONL output under `./temp/`.
5. Add the `scope-lookup calls / entries scanned` counter and the MIR-volume
   counters (separate counter pass).
6. Add build entry in `build_lambda_config.json`; regenerate with `make`.
7. **Capture and commit a release-mode baseline** for the large fixtures.
8. Confirm the call-graph split (direct vs indirect callees) on the corpus so the
   §4 design choice (A vs B) is data-driven.

Validation:

```text
make build-test
make release
./test/test_js_transpile_timing_gtest.exe
```

Expected: stable correctness pass/fail; release timing for all phases; large
fixtures reproduce the bottleneck outside layout; scope-lookup counter confirms or
refutes the O(n²) hypothesis.

### Phase B: AST Builder Speedups — DEFERRED (see §0.3; AST is <2% of compile time)

Tasks:

1. **If the counter confirms it: replace linear-scan scope lookup with a
   hash-backed scope (§2.1).** Re-measure `ast_us` and `mir_us`.
2. Add the string-literal no-escape fast path (§2.4).
3. Remove duplicate shorthand-property construction (§2.6).
4. Cache Tree-sitter field ids (§2.3).
5. Convert hot `ts_node_type()` comparisons to `ts_node_symbol()` **with a real
   generated symbol table and a desync guard** (§2.2).
6. Gate per-identifier undefined/global logging behind an env var.

Validation:

```text
./test/test_js_transpile_timing_gtest.exe
make test-js-baseline
make layout suite=web-tmpl
```

Expected: AST time drops on large fixtures (most from step 1); no js baseline
regression; no layout behavior change.

### Phase C: Compiled-Artifact Caching

Tasks:

1. Add content-hash keying (source + runtime/grammar version + flags).
2. Implement in-process / per-run reuse of realm-independent artifacts
   (AST/MIR-template level).
3. Wire into the Radiant script loader alongside the existing source cache.
4. Gate with `JS_COMPILE_CACHE=1`.
5. (Later) persistent `./temp/` cache; (later, audited) linked-code reuse.

Validation:

```text
JS_COMPILE_CACHE=1 make layout suite=web-tmpl
JS_COMPILE_CACHE=1 make test-js-baseline
```

Expected: large drop in total layout-suite compile time from de-duplicated vendor
compilation; identical layout output; identical js baseline results.

### Phase D: Generated MIR Counters and Generic Helpers

Tasks:

1. Count functions discovered/lowered/linked and MIR instructions emitted.
2. Identify generic operations that inline repeated MIR (counter-driven).
3. Add generic always-emitted native helpers and lower those operations to calls
   (no source-pattern recognition).

Validation:

```text
./test/test_js_transpile_timing_gtest.exe
make test-js-baseline
make test-radiant-baseline
```

Expected: MIR instruction/function count decreases; lowering/link time decreases;
execution neutral or better.

### Phase E: Lazy Function MIR Prototype

Tasks:

1. Implement option B (indirect-in-lazy-mode) first: lazy function record + state
   machine; create lazy function objects for eligible declarations/expressions;
   eagerly compile top-level only; compile body on first call via the existing
   `js_call_function` path; patch entry point; fall back to eager for
   eval/with/unsupported kinds.
2. Add lazy-compile-count / lazy-compile-time counters.
3. Measure link fragmentation; add batching if needed (§4.9).
4. If steady-state direct-call speed matters, implement option A (stub
   trampoline) and re-measure.

Validation:

```text
JS_LAZY_MIR=1 ./test/test_js_transpile_timing_gtest.exe
JS_LAZY_MIR=1 make test-js-baseline
JS_LAZY_MIR=1 make layout suite=web-tmpl
```

Expected: large vendor files show much lower initial MIR lowering/link time; some
execution time moves to first-call compile; total page load improves when most
library functions are cold; no closure/class/method/strict-mode regressions.

---

## 6. Risks and Guardrails

### 6.1 Correctness risks

| Risk | Guardrail |
| --- | --- |
| hashmap scope changes declaration/redeclaration semantics | preserve ordered iteration; reuse existing redeclaration checks; run js baseline + js262 |
| `ts_node_symbol` ids desync from grammar | source ids from generated parser; static-assert / regen guard |
| lazy MIR changes closure semantics | keep capture metadata eager first |
| syntax errors become lazy | always parse full source eagerly |
| direct eval observes wrong environment | eager compile eval/with scopes |
| direct-call ABI breaks under deferral | option A stub trampoline, or option B indirect-in-lazy-mode; never leave a direct ref to an undefined symbol |
| AST builder refactor changes source locations | keep source-range and error-reporting tests |
| cached artifacts retain realm-specific pointers | cache realm-independent levels first; audit before linked-code reuse; version the key |

### 6.2 Performance measurement risks

Debug builds are for zooming into relative phase costs during development. **All
performance claims must use release builds.** Record both, but label every number
with build mode. The benchmark prints build mode, platform, fixture byte size, and
active flags so a copied line is unambiguous.

### 6.3 Non-goals

Tune6 should not:

- replace the JS frontend with ad-hoc string interpretation,
- skip parsing unknown scripts,
- **hand-write native compatibility layers for specific vendor libraries**
  (removed in rev 2),
- recognize and special-case JS source idioms to redirect them to native helpers,
- hard-code layout test expected output,
- manually edit generated build files,
- judge performance from debug builds.

---

## 7. Success Criteria

Measurement:

- A focused GTest reports stable per-phase **release** timing for large JS
  fixtures, in compile-only and compile+execute modes.
- Counters identify the top AST cost centers and quantify MIR volume; the
  scope-lookup counter confirms whether scope scanning was the AST bottleneck.

AST:

- AST build time improves on large library fixtures (primarily from the scope
  lookup fix); no js baseline / js262 regression.

Caching:

- `make layout suite=web-tmpl` total compile time drops sharply from
  de-duplicated vendor compilation, with identical layout output.

MIR volume:

- Eagerly lowered function count and MIR instructions drop for large libraries;
  lowering/link time drops proportionally.

Lazy MIR:

- Initial compile/link time drops for large vendor libraries because cold bodies
  are not lowered; first-call compilation is visible in counters and breaks no
  closure/class/method/strict-mode behavior.

Radiant:

- `make layout suite=web-tmpl` is materially faster (mostly from caching, with
  lazy MIR for uncached/unique scripts).
- Debug-mode auto-close runs no longer spend seconds compiling unused vendor
  functions per test.
- New behavior stays behind flags and does not change the default JS correctness
  path.

---

## 8. Recommended First Patch

Small and measurement-only:

1. Add `test/test_js_transpile_timing_gtest.cpp` (compile-only + compile+execute).
2. Add 5–10 curated fixtures.
3. Print all nine phases via `JsMirPhaseTiming`, with build/mode/flags labels.
4. Add the scope-lookup counter and MIR/function counters (separate pass) only
   where they can be exposed without changing lowering behavior.
5. Wire into `build_lambda_config.json`.
6. **Record a release baseline** and the direct-vs-indirect call-graph split.

Only after this benchmark and baseline exist should Tune6 change AST or MIR
behavior. **rev 3 update:** the benchmark and release baseline now exist (§0), and
the data redirected the first behavioral change away from AST entirely. The first
change is the **MIR lazy-gen interface swap (§4.0)** — it targets the measured
dominant phase (`link_us`) with a one-line, flag-gated, ABI-compatible change.

---

## Appendix A: Why MIR symbol registration/resolution is O(n), not O(n²)

A common worry (and the one raised during review) is that linking many functions
gets quadratic because each function reference must be matched against every
registered global. That would be O(references × globals) ≈ O(n²). It is **not** the
case in MIR. This appendix records why, from the source in `ref/mir/`.

### A.1 The global symbol table is a hash table

Registered/exported items live in a single hash table, not a list:

- `module_item_tab` is an `HTAB(MIR_item_t)` (`ref/mir/mir.c:42`), created with an
  initial 512 buckets (`mir.c:784`).
- Key = (item name, module); see `item_hash` (`mir.c:682`) and the lookup helper
  `item_tab_find` (`mir.c:691`), which is a single `HTAB_DO(..., HTAB_FIND, ...)`.

So a name → definition lookup is **O(1) average**, not a linear scan.

### A.2 Resolution does one lookup per reference — no nested loop

In `MIR_link` Pass 1 (`mir.c:1987–2019`), for each `MIR_import_item` /
`MIR_export_item` / `MIR_forward_item` the code does exactly one `item_tab_find`
(plus, for an unresolved import, one `import_resolver` call and one
`MIR_load_external` that inserts once). There is **no** "for each import, scan all
exports" loop. The Lambda import resolver itself is also O(1): two hashmap probes
(`lambda/mir.c:106`).

Total resolution work = (number of references) × O(1) = **O(n)**.

### A.3 Hash-table growth is amortized O(n)

Inserting N items triggers doublings at 512 → 1024 → 2048 → … Each rehash
re-inserts the current contents (`ref/mir/mir-htab.h:159–174`), but the sizes form
a geometric series whose sum is ≤ 2N. So N insertions cost **O(N) total**, not
O(N) per insert.

### A.4 JS functions are not global exports anyway

The Lambda transpiler emits one module per JS file, registers each function as a
module-local `forward` item (`jm_register_local_func`, then `MIR_new_forward`),
and exports only `js_main`. Native runtime functions are resolved as imports via
the O(1) resolver. So the *global* environment table (`environment_module`) holds
only a small, roughly constant set (runtime imports + `js_main`) regardless of how
many JS functions the file declares. The thing that scales — module-local func
items — is resolved through the same O(1) hash table.

### A.5 Where the superlinearity actually is

Linking *time* still grows with function count, but the cause is **codegen, not
resolution**. `MIR_link`'s Pass 3 calls `set_interface` on every function
(`mir.c:2061`), and the eager interface `MIR_set_gen_interface` runs `MIR_gen`
(full codegen + regalloc, and SSA/GVN/LICM at opt≥2) per function
(`ref/mir/mir-gen.c:9755`). Per-function `MIR_gen` is itself super-linear in
function size at higher opt levels — which is why the engine downgrades to opt=0
for very large modules (`js_mir_entrypoints_require.cpp:642`). The fix is therefore
to *avoid generating cold functions at all* (lazy gen, §4.0), not to change symbol
resolution.

### A.6 Summary

| Step | Data structure | Per-op | Total for n functions |
| --- | --- | --- | --- |
| name → def resolution | hash table (`module_item_tab`) | O(1) avg | O(n) |
| table growth | doubling HTAB | amortized O(1) | O(n) |
| import resolution (Lambda) | hashmaps (`func_map`, dynamic) | O(1) | O(n) |
| eager codegen (`MIR_gen`) per func | — | super-linear in func size | **the actual cost** |

Resolution is O(n). The link phase is dominated by eager per-function codegen,
addressed by §4.0.
