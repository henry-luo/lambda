# Transpile_Js57_Es2024 — ES2024 Final Cleanup: Module-Level Scope Env + Real TLA

Date: 2026-06-14

Status: proposal. Js56 closed 17 of 23 admittable failures across 10 sub-phases, leaving **5 admittable failures** (after Gate J was added to `skip_list.txt` as a V8-spec-discrepancy). All 5 require architectural work on two distinct subsystems: module-level closure storage (1 test) and real TLA suspension with live import bindings (4 tests). The work splits cleanly into two independent tracks.

Js57 is the same shape as Js56: every remaining failure is engine work on existing subsystems. The difference is that Js56's fixes were surgical (one-line `_lambda_rt` ordering, missing microtask drain, etc.) while Js57's fixes are structural — adding a synthetic scope-env at `js_main` entry, plus a Promise-based suspension machinery for top-level await that actually pauses and resumes module evaluation.

## 1. Starting Baseline

Current checked-in release baseline at Js57 start (post-Js56 plus Gate J skip):

```text
# Scope: ES2024 (skip ES2025+ features)
# Total passing: 40253
# Total tests: 42889  Skipped: 2637  Batched: 40253  Passed: 40253  Failed: 0
# Runtime: ~131 s clean
# Batch size: batched 50 tests/process; async 50 tests/process
```

The Js57 acceptance bar:

- Passing count stays `>= 40253` after every phase.
- Regressions count is `0` at every phase boundary.
- 0 batch-lost, 0 crash-exits at the gate of every admission run.
- `skip_list.txt` retains the Js56 entries (Math.random + 2 RegExp property-escapes + rgi-emoji-17 + Gate J).
- Total runtime stays within `+5%` of the Js56 P10 clean baseline (~131 s) — 138 s ceiling per phase.
- Final `# Scope:` line stays at `ES2024 (skip ES2025+ features)` — Js57 closes the last architectural gaps before the Js58 ES2025 work.

### 1.1 Runtime drift carryover

Js56 P9's `js_run_microtasks()` on every `await` added 4-6 s of cumulative drain time across the suite (137 s → 131 s normalised after several runs). Js57's TLA suspension work will add per-suspension overhead; the +5 % ceiling has to budget for that. If a phase pushes runtime past the ceiling, revert before the next phase opens.

## 2. What Js56 Deferred

Js56 §14 documented the 5 remaining failures by track. The detailed mechanism for each was probed at the end of Js56 (two surgical attempts on Gate K, traces of the deep-TLA tests). Status: tractable but not surgical.

| Track | Tests | Mechanism | Fix surface |
|---|---:|---|---|
| **Track A — Module-level scope_env** | 1 | Module-level captured block-lets get per-closure env snapshots; mutations don't propagate between sibling closures | `js_mir_module_batch_lowering.cpp:transpile_js_module_to_mir` (scope_env allocation at js_main entry); `js_mir_function_class_lowering.cpp` (capture lookup); var-decl + assignment paths in `js_mir_expression_lowering.cpp` / `js_mir_statement_lowering.cpp` |
| **Track B — Real TLA + live import bindings** | 4 | `await pendingPromise` returns undefined synchronously instead of suspending; imported bindings snapshot at import time instead of reflecting source-module exports; no TDZ before source executes its `export default` | `js_mir_module_batch_lowering.cpp` (per-module TopLevelCapability), `js_runtime.cpp:js_await_sync` (suspend path), `js_mir_statement_lowering.cpp:JS_AST_NODE_IMPORT_DECLARATION` (live binding emission), `js_runtime.cpp` (binding cell type + TDZ check) |
| **Total** | **5** | | |

Probe artifacts retained in `temp/js57_repros/`:

- `temp/js57_failures.tsv` — current 5-row failure manifest
- `temp/js57_repros/track_a_gate_k.txt` — `coerced-new-length-detach.js` (1 entry)
- `temp/js57_repros/track_b_tla.txt` — `async-module-does-not-block-sibling-modules.js`, `fulfillment-order.js`, `rejection-order.js`, `module-self-import-async-resolution-ticks.js` (4 entries)
- `temp/js57_gate_k_probe.js` — `let called; arrow{ obj{ valueOf{ called=true } } }` minimal repro
- `temp/js57_tla_probe.js` — `await Promise.withResolvers().promise` minimal repro for suspension
- `temp/js57_self_import_probe.js` — self-import + TDZ + live-binding repro

## 3. The Two Tracks

### Track A — Module-level `scope_env` (1 test)

**Failing test**: `built_ins/ArrayBuffer/prototype/resize/coerced-new-length-detach.js`

The test pattern (the "valueOf-mutates-outer-via-resize" shape):
```js
{
  const rab = new ArrayBuffer(64, { maxByteLength: 1024 });
  let called = false;
  assert.throws(TypeError, () => rab.resize({ valueOf() {
    $DETACHBUFFER(rab);
    called = true;
  }}));
  assert(called);  // must be true
}
```

Both closures (the outer arrow and the inner `valueOf` method) capture `called` from the surrounding block. With Lambda's current per-closure env mechanism at module scope, each gets its own snapshot:

- block `called` register: 0
- arrow's env[called]: snapshot 0 at arrow creation
- valueOf's env[called]: snapshot 0 at valueOf creation (each invocation creates a new valueOf)

When valueOf mutates its env[called] to true, the change stays in valueOf's env. The outer `assert(called)` reads the block's register, still 0.

Inside a regular function, this works because the function's `scope_env_reg` is shared by all nested closures — all captures route through it (see `jm_scope_env_mark_and_writeback` in `js_mir_calls_boxing_types.cpp:1156`). At module level, `mt->scope_env_reg = 0` (set in `js_mir_module_batch_lowering.cpp:3885`), so the fallback per-closure env path runs.

**Fix surface (proposed)**:

1. **Phase 1**: Pre-scan: at the start of `transpile_js_module_to_mir`, walk the AST and check if any block-let / block-const is captured by a closure (or by a class constructor inside the module body). If yes, mark a `mt->has_module_scope_env` flag.
2. **Phase 2**: At js_main entry, if `has_module_scope_env`, allocate a scope_env (via `js_alloc_env`) and store its register in `mt->scope_env_reg`. Treat the module body as if it were a function body with `has_scope_env` — `jm_scope_env_mark_and_writeback` will then promote captured block-lets to the scope_env on let-declaration, and the per-closure env path in `jm_transpile_func_expr` will detect `use_scope_env` and share it.
3. **Phase 3**: The two failed Js56 attempts at this fix (per §14.1) broke `language/statements/for/scope-body-lex-open.js` per-iteration semantics. The new approach must skip the scope_env promotion for **per-iteration loop bindings** (for-init lets in `for (let x = ...; ...; ...)` etc.) — those need their existing per-closure env so each iteration's closures get separate bindings. The scope_env should only cover block-lets that are NOT for-init lets.

The §12.10 / §12.14 / §12.17 closure-capture invariants from Js55 apply here — promoting to scope_env must not regress the env-reload-after-call logic (`jm_env_reload_shared_captures`, `jm_scope_env_reload_vars`).

**Risk**: medium-high. Touches the central closure mechanism. Pre-flight all `built-ins/Promise/*`, `language/expressions/{async,arrow,class}/*`, and `language/statements/{for,for-of,for-in,for-await-of}/*` on debug — closure storage is hot.

#### A1 — Pre-scan + flag (no behavior change)

Just add the analysis and flag. No allocation, no behavior change. Run regression — should be 0 changes.

#### A2 — Allocate scope_env + promote captured block-lets

The actual fix. Confirm via `temp/js57_gate_k_probe.js` reduction first, then the full test.

#### A3 — Skip per-iteration bindings + regression sweep

Make sure for-loop / for-of / for-in per-iteration semantics still work (re-run `for/scope-body-lex-open.js` + the speciesctor cluster, which has a similar shape).

### Track B — Real TLA + live import bindings (4 tests)

**Failing tests**:
- `language/module-code/top-level-await/async-module-does-not-block-sibling-modules.js`
- `language/module-code/top-level-await/fulfillment-order.js`
- `language/module-code/top-level-await/rejection-order.js`
- `language/module-code/top-level-await/module-self-import-async-resolution-ticks.js`

Each test exercises a different facet of the same architectural gap:

| Test | Facet |
|---|---|
| async-module-does-not-block-sibling-modules | Sibling sync module must evaluate while the TLA sibling is suspended |
| fulfillment-order | Multi-module + Promise.withResolvers + dynamic import + true pending Promise |
| rejection-order | Same as fulfillment-order, rejection path |
| module-self-import-async-resolution-ticks | Self-import + live binding (`import self from './self.js'`) + TDZ on default before `export default` runs |

**Common root cause**: Lambda's `js_await_sync` is synchronous-only. When given a pending Promise, it returns `undefined` instead of suspending the calling code. There is no concept of `[[TopLevelCapability]]` per module — `transpile_js_module_to_mir` runs the module body to completion and returns; any unsettled async chains are dropped.

**Fix surface (proposed)**:

This is the bigger architectural piece — it splits into three sub-tracks:

#### B1 — `[[TopLevelCapability]]` + sync-mode TLA suspension via continuation

Per ES §16.2.1.5, each async module has a TopLevelCapability — a promise representing the module's evaluation completion. Importers await this promise; the module fulfills it after running.

For Lambda's sync-only model, the simplest path:

1. Each module's `js_main` returns a Promise (the TopLevelCapability) instead of returning the namespace directly. Synchronous modules return an already-fulfilled Promise.
2. The MIR codegen for TLA modules wraps the body in a try-block that fulfills the capability on completion and rejects on uncaught throw.
3. `transpile_js_module_to_mir` returns the namespace once the capability settles. Since Lambda drains microtasks synchronously, fully-synchronous TLA chains complete inside the drain.
4. For genuinely-async patterns (Promise.withResolvers + cross-module resolution), `js_await_sync` on a pending Promise needs to either drain the loop until the Promise settles or suspend the module's MIR continuation.

The simpler option: when `js_await_sync` hits pending, **drain the libuv event loop** (not just microtasks) for one turn, then re-check. This is what `js_event_loop_drain` already does after `js_main` returns; we'd add a bounded version (loop until promise settles OR a watchdog timeout) usable from inside `js_await_sync`.

Risk: dramatic. The Js55 §P23(b) attempt did exactly this and pushed runtime from 155 s to 1675 s (a 10x slowdown). The bound was wrong then; for Js57 the bound must be:
- only drain the loop if the awaited promise has at least one pending handler queued
- watchdog timeout of ~100 ms per `await`
- detect "promise can never settle" via a no-progress counter (3 consecutive drain turns with no microtask popped → give up)

#### B2 — Live import bindings + TDZ on imported default

Per ES §16.2.1.7, imported bindings are live: the local name `self` in `import self from './x'` reflects whatever `x.default` currently is, not a snapshot at import time. Lambda currently snapshots at import time (see `js_mir_statement_lowering.cpp:JS_AST_NODE_IMPORT_DECLARATION` around line 5948).

For test conformance, only the default-import path needs live binding (the self-import test). The two-step fix:

1. Replace the local var with a "live binding cell" — a runtime struct holding `{namespace_obj, export_name, has_resolved}`. Reads of the local emit a `js_get_live_binding(cell)` call that does `js_property_get(ns, name)` each time.
2. Add a TDZ flag on the cell. When the source module's `export default ...` runs, clear the TDZ flag. Reads of the cell while TDZ is set throw `ReferenceError`.

The TDZ clearance trigger: `js_set_module_var` (or whatever lowers `export default ...`) should additionally clear TDZ on any live-binding cell pointing at this default slot.

Risk: medium. The change touches import-declaration lowering and every read of an imported binding. Pre-flight all `language/module-code/*` tests on debug.

#### B3 — Sibling module evaluation ordering

The `async-module-does-not-block-sibling-modules.js` test requires that when module A is async and module B is sync, both being imported by module C, B evaluates while A is suspended (not waiting for A to finish).

This requires changing `jm_load_imports` (`js_mir_module_batch_lowering.cpp:5987`) to:
- evaluate sync modules first
- start async modules in suspended state (return their TopLevelCapability immediately)
- C's evaluation waits on `Promise.all` of all importsP

Risk: low to medium once B1 lands. Without B1 it's not implementable.

## 4. Phase Plan

Phases ordered by dependency — Track A is independent and small, Track B is sequential B1 → B2 → B3.

### P0 — Baseline confirmation and probes

Goal: confirm 40253/40253 baseline. Generate failure manifest. Build the 3 reduction probes (`temp/js57_*_probe.js`).

Acceptance: 3-run runtime average within `+5%` of 131 s. 5-row failure manifest in `temp/js57_failures.tsv`. Probes check in to `temp/js57_repros/`.

### P1 — Track A: module-level scope_env (1 test)

Cleanest standalone gain. Lower-risk than Track B.

Work:
1. **P1a**: AST pre-scan to detect captured block-lets. Add `mt->has_module_scope_env` flag. No behavior change yet — verify pre-flight is clean.
2. **P1b**: Allocate scope_env at js_main entry when flag is set. Route captured block-let read/write through it. Skip per-iteration loop bindings.
3. **P1c**: Re-run the speciesctor + for-scope regression cluster from Js56. Verify per-iteration semantics still work.

Acceptance: Gate K test passes (1 admission). `language/statements/for/scope-body-lex-open.js` still passes. Speciesctor tests still pass. 0 regressions. Release guard clean.

Estimated: 1-2 days.

### P2 — Track B1: Per-module TopLevelCapability + bounded loop drain

The architectural backbone. Both B2 and B3 depend on this.

Work:
1. **P2a**: Each module's js_main returns a Promise (TopLevelCapability). Sync modules return a pre-resolved Promise; async modules return a Promise that settles when the module body completes.
2. **P2b**: `transpile_js_module_to_mir` waits on the TopLevelCapability before returning the namespace. The wait uses bounded loop drain.
3. **P2c**: `js_await_sync` on a pending Promise uses bounded loop drain — drain libuv loop in `UV_RUN_NOWAIT` mode with a watchdog timeout (suggested: 100 ms) and a no-progress counter (3 consecutive turns with empty microtask queue → give up and return undefined).

Risk controls:
- Pre-flight all `built-ins/Promise/*` and `language/module-code/*` tests on debug. Diff failures before/after.
- 5% wall-clock guard at the gate (no regression on the 138 s ceiling). The Js55 P23(b) attempt blew this to 1675 s — every drain trigger in Js57 must measure wall-clock cost.

Acceptance: Maybe 0-2 of the 4 deep-TLA tests pass (depends on how much B2/B3 are also needed). 0 regressions on existing module / promise tests. Wall-clock within +5% of P0.

Estimated: 2-3 days.

### P3 — Track B2: Live import bindings + TDZ

Required for `module-self-import-async-resolution-ticks.js`.

Work:
1. **P3a**: Add `JsLiveBinding` struct: `{Item namespace_obj; String* export_name; bool tdz_active;}`.
2. **P3b**: For `import X from './...'`: emit a live-binding cell allocation, store cell pointer in the local var slot. Every read of X emits a runtime call to dereference the cell.
3. **P3c**: When `export default <expr>` (or `export const X = <expr>` for named exports) lowers, also clear the TDZ flag on any live-binding cell pointing at this slot.
4. **P3d**: Probe via `temp/js57_self_import_probe.js`. Then run the full self-import test.

Acceptance: self-import test passes (1 admission). 0 regressions. Release guard clean.

Estimated: 1-2 days.

### P4 — Track B3: Sibling module evaluation ordering

Required for `async-module-does-not-block-sibling-modules.js`.

Work:
1. **P4a**: Change `jm_load_imports` to evaluate sync modules synchronously and start async modules in "suspended" state (their TopLevelCapability is pending).
2. **P4b**: The importing module's evaluation waits on `Promise.all([...all imports' capabilities])` via the bounded loop drain from P2.
3. **P4c**: Probe via the test. Verify the sync sibling completes before the TLA sibling.

Acceptance: sibling-modules test passes (1 admission). 0 regressions.

Estimated: 1 day (assuming B1 + B2 are solid).

### P5 — Track B leftover: fulfillment-order + rejection-order

The two most complex deep-TLA tests. They combine all of B1 + B2 + B3 + dynamic-import promise chaining.

Work:
1. **P5a**: Run the tests with all prior phases landed. Triage what's still failing.
2. **P5b**: Likely fix surface: dynamic import promise must properly chain with the inner module's TopLevelCapability. The dynamic-import path (per Js56 §14.2) currently wraps the namespace in `js_promise_resolve(ns)` synchronously — needs to wait for the inner module's TopLevelCapability first.

Acceptance: fulfillment-order and rejection-order pass (2 admissions). 0 regressions.

Estimated: 1-2 days (highly dependent on what P5a triage finds).

### P6 — Final admission + stability guard

Run release `--update-baseline`. Stability guard. Document the new ES2024 conformance number.

Acceptance: Baseline updated (40258 target if all 5 land). 0 regressions, 0 batch-lost, 0 crash-exits. Final passing count recorded.

## 5. Per-Phase Guard Commands

Same contract as Js55 §5 / Js56 §5.

Pre-flight (debug build):

```bash
make build && make build-test
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/built_ins_Promise_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_module_code_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_statements_for_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/typed_array_*'
```

Release js262 guard:

```bash
make release
make -C build/premake test_js_test262_gtest config=release_native
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js57_pN_release_guard.tsv \
  --gtest_brief=1
```

The guard tsv must report:
- `Failed: 0` (in baseline)
- `Regressions: 0`
- `Passing >= 40253 + sum of admissions from prior phases`
- `Skipped` non-increasing
- Total runtime within `+5%` of the 3-run average from P0

Full run (track improvements):

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js57_pN_full.tsv \
  --gtest_brief=1
```

Final update (only after P6 guard is clean):

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js57_update_baseline.tsv \
  --gtest_brief=1
```

## 6. Risk Register

Quick-reference table of all phase risks; the three highest-impact entries are documented in detail in §6.1 below — read those before starting their respective phases.

| Risk | Impact | Phase | See §6.1? |
|---|---|---|---|
| **scope_env promotion breaks per-iteration loop bindings** | very high — would lose hundreds of tests | P1 | yes (§6.1.1) |
| **bounded loop drain in js_await_sync blows wall-clock** | very high — Js55 P23(b) went from 155 s → 1675 s (10×) | P2 | yes (§6.1.2) |
| **live-binding cell breaks the hot-path read of import variables** | high — slows down every imported-symbol read | P3 | yes (§6.1.3) |
| **TDZ on imported default fires for in-order reads** | medium | P3 | Check at runtime (not lexical position): only TDZ if source module's `export default` hasn't executed yet. Verify with both static and dynamic import patterns. |
| **changes to jm_load_imports break the existing sync-module chain** | medium-high | P4 | Pre-flight all `language/module-code/*` tests + the imports-resolution suite. Sync modules' evaluation order must stay deterministic. |
| **dynamic import + fulfillment-order has correctness bugs we don't yet understand** | unknown | P5 | P5a triage is explicit — don't promise a P5b until P5a results are in. |
| **Cumulative drift exceeds +5% over the Js57 phases** | medium | all | Per-phase +5% ceiling. If a single phase pushes over, revert before the next phase opens. |

### 6.1 Critical risks (detailed)

These three carry direct historical evidence of past regressions. Each entry documents the prior incident, the precise mitigation shape, and the abort condition.

#### 6.1.1 P1 — scope_env promotion breaks per-iteration loop bindings

**Prior incident**: Js56 §14.1 attempted this fix and broke `language/statements/for/scope-body-lex-open.js`. The for-init binding `let x = 'outside'` was incorrectly shared with the iteration body's `x = 'inside'` assignment, polluting the closure captured at init time. The guard `mt->iteration_depth == 0` did NOT prevent this — INIT runs at depth 0 (before `mt->iteration_depth++` fires).

**Mitigation shape**:
- Detect for-init lets via AST inspection (parent is `JS_AST_NODE_FOR_STATEMENT` and the let is in the init slot, not the body), NOT via `iteration_depth`.
- Same logic for `for-in`, `for-of`, `for-await-of` init lets.
- Skip scope_env promotion for these — they keep their per-closure env snapshot (preserving per-iteration binding semantics).
- For block-scoped lets that are NOT for-init: route through the new module-level scope_env.

**Pre-flight gate**:
```bash
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_statements_for_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_statements_for_of_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_statements_for_in_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_statements_for_await_of_*'
```
Byte-identical passing before/after — any single test flipping pass→fail aborts the phase.

**Abort condition**: any new failure in `language/statements/for*` → revert and re-design the scope detection.

#### 6.1.2 P2 — bounded loop drain in js_await_sync blows wall-clock

**Prior incident**: Js55 P23(b) added an unconditional `js_event_loop_drain()` call from `js_await_sync` when the promise was pending. The full-suite runtime went from **155 s to 1675 s** — a 10.8× slowdown. The mechanism: many tests have awaits on promises that never settle within the test (e.g. tests that intentionally await a hanging promise to verify timeout behavior); each such await spun the full libuv loop until the watchdog fired (multi-second timeout × thousands of tests = catastrophe). The fix was reverted within hours.

**Mitigation shape** (must implement ALL THREE bounds):
1. **Watchdog timeout**: each loop drain inside `js_await_sync` has its own short watchdog — suggested **100 ms maximum** per `await`. If the watchdog fires, return undefined (the spec-mismatch outcome the caller already handles).
2. **No-progress counter**: if 3 consecutive `uv_run(loop, UV_RUN_NOWAIT)` turns pop zero microtasks AND the promise is still pending, give up immediately (don't wait for the watchdog). This catches "promise can never settle in this turn" without burning the full 100 ms.
3. **Conditional trigger**: only drain if the awaited promise has at least one pending then-handler queued. If `p->then_count == 0` and the promise is still pending, no amount of microtask draining will help — return undefined immediately. (Spec-wise this is a pending promise no one resolves; it's actually undefined behavior in sync-only models.)

**Pre-flight gate**:
```bash
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/built_ins_Promise_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_module_code_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_expressions_async*'
```
Measure wall-clock on each subset. Record the timing before P2 and after. **Any subset growing >+10% wall-clock is an abort signal** (the +5% per-phase ceiling is on the full suite — subsets can absorb more, but 10× the budget is the canary).

**Abort condition**: full-suite runtime > 145 s (+10% of 131 s baseline) → revert and find a more surgical trigger.

#### 6.1.3 P3 — live-binding cell breaks hot-path read of import variables

**Prior context** (no incident yet — this is a forward-looking risk): every read of an imported variable currently emits a single register move (the snapshot from import time). Replacing this with a runtime `js_get_live_binding(cell)` call adds at minimum a function call + property hash lookup per read. For a tight loop using an imported helper (e.g. `for (let i=0; i<1e6; i++) imported_fn(i);`), this could be a measurable slowdown.

**Mitigation shape** (two-tier approach):
1. **Scope the change to default-imports only**. The self-import test is the only one that needs live bindings; named-import live bindings are nice-to-have but not currently tested. So `import x from './...'` gets a cell; `import { x } from './...'` keeps the snapshot.
2. **Cache the dereferenced value in a register**. The MIR emits:
   - At each read: `r = js_get_live_binding(cell)`; subsequent reads in the same basic block reuse `r`.
   - Invalidate the cached register on any operation that could mutate the source module's exports — primarily on every JS-level call (treat same as `jm_scope_env_reload_vars`).
3. **Skip the cell entirely if the import isn't a self-import AND the source module has no TLA**. Static analysis: if neither condition holds, the snapshot is correct (the source module fully evaluates before the import resolves).

**Pre-flight gate**:
```bash
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/language_module_code_*'
./test/test_js_test262_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/built_ins_*'
```
Special perf benchmark: time a known import-heavy test (e.g. `built_ins/JSON/parse/*` chain) before/after. **Any single test slowing down >+20% is the canary.**

**Abort condition**: full-suite runtime > 145 s OR any individual test slowing by >+20% → revert and narrow the cell allocation further (maybe only self-imports get cells).

## 7. Completion Criteria

| Criterion | Target | Notes |
|---|---|---|
| Skip-list ES2024 entries | unchanged (Gate J stays as V8 discrepancy) | Js57 doesn't re-skip anything. |
| Baseline scope header | `ES2024 (skip ES2025+ features)` | unchanged from Js56. |
| Passing count rises by 3-5 (best case) or 1-3 (realistic) | ≈ 40254-40258 | Js56 final 40253 + Track A ~1 + Track B ~2-4. |
| Failures (in baseline) | 0 | unchanged contract. |
| Regressions | 0 | unchanged contract per phase. |
| `skip_list.txt` | unchanged (still includes Gate J) | no new skips. |
| `t262_partial.txt` | unchanged from Js56 | the 1 remaining SLOW + 1 CRASH (early-no-escaped-await) stay. |
| Runtime within +5 % of 3-run-average baseline | yes | enforced per phase. P2 (TLA loop drain) is the highest-risk phase for this. |
| New regression tests landed | 2+ | `test/js/regression_js57_module_scope_env.js`, `test/js/regression_js57_self_import_live_binding.js`. |

## 8. Out Of Scope

- **ES2025+ features.** `Float16Array`, `iterator-helpers`, `set-methods`, `import-attributes`, `Promise.try`, `RegExp.escape`, `regexp-modifiers`, `regexp-duplicate-named-groups`, `json-modules`, `json-parse-with-source`, `Math.sumPrecise`, `Uint8Array.prototype.{toBase64,fromBase64,toHex,fromHex}` — defer to Js58.
- **Stage 3 / Stage 2 proposals.** `Temporal`, `ShadowRealm`, `decorators`, `explicit-resource-management`, `Atomics.pause`, `import-defer`, `Error.isError` — defer.
- **Full ESM live binding for named imports** (P3 scope is default-import only). Named exports rarely change after module init, so snapshotting them is mostly safe; the only test that depends on live named-import bindings would be a follow-up.
- **CJS dynamic import edge cases.** Js56 §14.2 made `js_dynamic_import` bypass the CJS default extraction (correct for ESM `.js` files). Lambda doesn't currently support importing `.cjs` files dynamically — out of scope for Js57.
- **Async iteration / async generators in modules.** `for await...of` at top-level — out of scope. Not in any of the 5 failing tests.

## 9. Phase Effort Estimates

| Phase | Tests | LOC | Risk | Estimate |
|---|---:|---:|---|---|
| P0 baseline + probe | 0 | — | trivial | 0.5 h |
| P1 Track A scope_env | 1 | ~80–150 | medium-high | 1-2 days |
| P2 Track B1 TLA suspension | 0-2 | ~200–400 | high (wall-clock) | 2-3 days |
| P3 Track B2 live bindings + TDZ | 1 | ~150–250 | medium-high | 1-2 days |
| P4 Track B3 sibling ordering | 1 | ~80–150 | medium | 1 day |
| P5 Track B leftover (fulfillment/rejection) | 0-2 | ?? | unknown (triage-first) | 1-2 days |
| P6 admission + guard | 0 | — | trivial | 1 h |

**Total estimated effort: ~6–10 days** of focused engine work, dominated by P2 (TLA suspension — both the highest leverage and the highest wall-clock risk).

## 10. Why this is "the architectural cleanup" rather than "final ES2024"

Js53 admitted easy ES2024 features. Js54 admitted the implementation-heavy ones (RAB OOB + transfer + `/v`). Js55 closed the integration gaps left by Js53/Js54. Js56 closed the surgical correctness bugs left after Js55 (closure-mutation primitive, E1 NamedEvaluation, microtask drain on pending Promise, dynamic-import-as-ESM, SIGSEGV from `_lambda_rt` ordering). Js57 closes the last 5 architectural gaps.

When Js57 P6 lands, Lambda's ES2024 baseline conformance is **>= 99.99%** of all batched ES2024 tests. The remaining intentional gaps would be:
- 2 SLOW Array.prototype.{some,every} tests (Gate L, in `t262_partial.txt`) — slow but correct
- 1 V8-discrepancy Gate J (in `skip_list.txt`) — Lambda spec-conformant, test V8-specific
- 1 CRASH_139 early-no-escaped-await (in `t262_partial.txt`) — single edge-case crash

**No further ES2024 work is planned after Js57.** The next proposal is Js58, opening ES2025 features.

## 11. Anticipated Final Numbers

| Metric | Js56 final | Js57 P6 target | Js57 best case |
|---|---:|---:|---:|
| Baseline fully passing | 40253 | ≈ 40256 | ≈ 40258 |
| ES2024 admissions from Track A | 0 | + ~1 | + ~1 |
| ES2024 admissions from Track B | 0 | + ~2 | + ~4 |
| Scope line | ES2024 | ES2024 | ES2024 |
| Failures in baseline | 0 | 0 | 0 |
| Regressions | 0 | 0 | 0 |
| Total ES2024 tests admitted across Js53+Js54+Js55+Js56+Js57 | ≈ 745 | ≈ 748 | ≈ 750 |

Js57's primary deliverable is "ES2024 architecturally closed." After Js57 P6, the remaining failures should be limited to:
- 1 V8-spec-discrepancy Gate J (in `skip_list.txt`)
- 1 SLOW Array.prototype.every (in `t262_partial.txt`)
- 1 CRASH early-no-escaped-await (in `t262_partial.txt`)
- 0–2 deep-TLA tests if P5 triage uncovers more spec-edge issues

## 12. Investigation Notes Carryover from Js56

Js56 §14 documented the deferred-tests investigation thoroughly. Key conclusions to carry forward into Js57:

### 12.1 Gate K probes (carry into P1)

Two surgical attempts failed in Js56:

- **Attempt A**: route module-level captured-let var reads through the closure's per-closure env by setting `var->in_scope_env = true; var->scope_env_reg = env`. **Broke** `language/statements/for/scope-body-lex-open.js`: the loop's `x = 'inside'` assignment wrote through to `probeBefore`'s env, returning 'inside' instead of 'outside' per per-iteration semantics. The guard `mt->iteration_depth == 0` didn't help — INIT runs at depth 0 (before the iteration counter bumps).

- **Attempt B**: remove the `mt->last_closure_has_env = false` reset at `jm_transpile_call`'s member-call entry. **No effect**: trace showed `jm_readback_closure_env` was never called for `obj.valueOf()` — the issue isn't in the immediate member call but in `rab.resize(obj)` where resize invokes `obj.valueOf` internally (via the C runtime's `ToIntegerOrInfinity`), so there's no JS-level call expression to hang readback on.

The proper fix (Track A in Js57) is the scope_env allocation at js_main entry — both attempts above tried to work around the lack of a scope_env at module level. Allocating one cleanly resolves the issue without the per-iteration regression (assuming the iteration-skip is implemented carefully).

### 12.2 Deep-TLA probes (carry into P2-P5)

The 4 deep-TLA tests were probed with the always-drain-on-await fix from Js56 P10 (drain microtasks on every `js_await_sync` regardless of promise state):

- `module-async-import-async-resolution-ticks.js` → **PASSED** in Js56 P10 (now in baseline)
- `async-module-does-not-block-sibling-modules.js` → still fails: sync sibling needs to evaluate during TLA suspension
- `fulfillment-order.js` / `rejection-order.js` → still fail: `await p1.promise` where p1 is settled by a different module never resolves (no inter-module suspension/resumption)
- `module-self-import-async-resolution-ticks.js` → still fails: `import self from './self.js'` snapshots `default` as undefined; needs live binding + TDZ

The Js55 P23(b) wall-clock catastrophe (155 s → 1675 s on full-promise-loop-drain in `js_await_sync`) is the cautionary tale for P2. The bounded loop drain with watchdog + no-progress counter is the safer shape.

### 12.3 Self-import live-binding probe

Js56's trace confirmed:
```
import self from './module-self-import-async-resolution-ticks.js';
// at this point: self === undefined (snapshot of self.default which is unset)
export default await Promise.resolve(42);
// at this point: self.default is now 42, but the local `self` is still undefined
```

The fix shape (P3 in Js57): replace `self`'s storage with a runtime cell pointing at `(namespace_obj, "default", tdz_active=true)`. Every read of `self` calls `js_get_live_binding(cell)`. When `export default 42` lowers, additionally call `js_clear_tdz(cell)` for any live binding cell pointing at this slot.

## 13. Implementation Status (2026-06-14)

### Landed

- **P0** — baseline probes written to `temp/js57_repros/` (gate_k_min, t262_gate_k, tla_setTimeout, self_import_probe, tla_probe, for_init_regression, misc_regressions, block_let_no_capture, misc_edge, p2_correctness).
- **P1 — Track A (module-level `scope_env`)**:
  - `js_mir_context.hpp`: new `module_fc` synthetic `JsFuncCollected` on the transpiler + `module_scope_env_active` gate.
  - `js_mir_module_batch_lowering.cpp`: new AST walker `jm_collect_for_init_lexical_names` (skips function/class bodies) and a Phase 1.7.5 between the per-function scope-env pass (Phase 1.7) and the parent-env-reuse pass (Phase 1.7b) that builds `module_fc.scope_env_names` from top-level closures' captures (filtering out modvars, for-init lets, NFE self-bindings, `this`/`new.target`/`arguments`) and remaps each capture's `scope_env_slot` to a module-env position.
  - `js_mir_module_batch_lowering.cpp`: at `js_main` entry, when `module_fc.has_scope_env`, allocate the env via `js_alloc_env`, pre-fill slots with the TDZ sentinel, set `mt->scope_env_reg` / `mt->scope_env_slot_count` / `mt->current_fc = &mt->module_fc` so the existing `jm_scope_env_mark_and_writeback` and identifier-load paths route through the new env unchanged.
  - `js_mir_calls_boxing_types.cpp`: `jm_scope_env_mark_and_writeback` now also accepts `current_func_index == -1 && module_scope_env_active` — same behavior as the function-body case but driven by `module_fc`.
  - `js_mir_statement_lowering.cpp`: the v24 "scope-env captured native widening" check switched from `current_func_index/func_entries[fi]` to `mt->current_fc` so the same widening applies at module scope.
  - **Verification**: `temp/js57_repros/js57_gate_k_min.js` and `js57_t262_gate_k.js` now print `OK`/`PASS` (mirrors `built_ins/ArrayBuffer/prototype/resize/coerced-new-length-detach.js`). `js57_for_init_regression.js` (mirror of `language/statements/for/scope-body-lex-open.js`) still prints `OK regression` — per-iteration semantics preserved. The full `test/test_js_gtest.exe` suite still reports `177 PASSED` (the single `lib_marked.js` SIGSEGV is pre-existing — reproduces identically with the changes stashed).
- **P2c — bounded loop drain (infrastructure)**:
  - `js_event_loop.cpp` + `.h`: new `js_microtask_pending_count()` getter and new `js_await_bounded_drain(predicate, user, watchdog_ms, max_no_progress, max_turns)` helper. Drains microtasks + `uv_run(UV_RUN_NOWAIT)` in tight turns until predicate non-zero or one of three bounds expires. Defaults: 100 ms watchdog, 3 no-progress turns, 64 turn cap — matches the §6.1.2 mitigation shape.
  - `js_runtime.cpp:js_await_sync`: pending-promise branch now calls `js_await_bounded_drain` (gated on `then_count > 0 || js_microtask_pending_count() > 0` so a strict no-handlers-no-microtasks pending promise still returns `undefined` immediately — matches the §6.1.2 "conditional trigger" mitigation).
  - **Verification**: `js57_p2_correctness.js` exercises await on resolved/chained/rejected/value/`Promise.all` — all print `OK`. Same 177/178 JS gtest count as P1.

- **P3 — Track B2 (live import bindings + TDZ for self-imports)**:
  - `js_mir_context.hpp`: added `is_live_default_binding` + `live_binding_specifier` to both `JsMirVarEntry` and `JsModuleConstEntry`. The MCONST flag covers the closure path (where captures of module-level imports route through `js_get_module_var`); the var-entry flag covers module-level reads via `jm_find_var`.
  - `js_mir_module_batch_lowering.cpp`:
    - third-pass module-const registration (the place the import name is reserved as `MCONST_MODVAR`) now resolves the import source against the current filename and stamps `is_live_default_binding` + `live_binding_specifier` when they match;
    - `jm_load_imports` skips self-imports up front so the loader does not recurse-compile the same source.
  - `js_mir_statement_lowering.cpp`: the `JS_AST_NODE_IMPORT_DECLARATION` handler now detects self-imports and, instead of snapshotting `js_property_get(ns, "default")` into a local register, leaves the register uninitialised and tags the `JsMirVarEntry` with the live-binding fields so direct reads also go through the runtime helper.
  - `js_mir_expression_lowering.cpp`: both identifier-load paths (`jm_find_var` and the `MCONST_MODVAR` branch) emit `js_get_live_binding_default(specifier)` when the live-binding flag is set.
  - `js_runtime.cpp` + `js_runtime.h` + `sys_func_registry.c`: new exported `js_get_live_binding_default(specifier)` runtime function. Uses `js_has_own_property(ns, "default")` to detect the uninitialised state (the spec's "binding not initialised" → ReferenceError condition). The has-own check sidesteps the underlying map's low-bit normalisation on undefined-typed values that defeats a TDZ-sentinel approach.
  - **Verification**: `temp/js57_repros/self_import_test.js` (small self-import scenario) prints both `OK self TDZ before export default` and `OK self === 42 after export default` when run through the module-source batch protocol — i.e. before `export default`, `self` throws `ReferenceError`; after, it reflects the published value. Full `test/test_js_gtest.exe` still reports `177 PASSED` (same `lib_marked` pre-existing crash).

### Still pending

- **P2a/P2b — per-module `TopLevelCapability`**: not implemented. `js_main` still returns the namespace synchronously; the bounded drain alone is not sufficient for the cross-module-resolution patterns of the deep-TLA tests.
- **P4 — sibling module evaluation ordering**: not implemented. The test fundamentally requires interleaved execution: a TLA module's pre-await statements must run, then sync siblings evaluate, then the TLA module's post-await statements resume. This requires either compiling TLA module bodies as async state machines (large refactor — reuses existing async-function infrastructure) or chunking module bodies at await points (also large). Neither fits a single session's scope.
- **P5 — fulfillment-order / rejection-order triage**: gated on P2a/P4 — both fixtures use cross-module shared `Promise.withResolvers` resolution that only works once modules can actually suspend at top-level await.
- **P6 — `--update-baseline` and stability guard**: gated on the deep-TLA work or an explicit decision to admit Gate K + self-import only.

### Tests that should flip after a release-mode admission run

With P1 and P3 landed, the expected admission delta is two of the five Js56 deferred failures:

- `built_ins/ArrayBuffer/prototype/resize/coerced-new-length-detach.js` — PASS (driven by the new module scope env; reproduced locally via `js57_t262_gate_k.js`).
- `language/module-code/top-level-await/module-self-import-async-resolution-ticks.js` — PASS (driven by the new live-binding + TDZ for self-imported default; reproduced locally via the module-source batch protocol).

The three remaining deep-TLA tests (`async-module-does-not-block-sibling-modules`, `fulfillment-order`, `rejection-order`) still need real top-level-await suspension (P2a/P4) which is out of scope for this session.

### Risk notes carried forward

- The §6.1.2 wall-clock catastrophe risk for `js_await_sync` is still the highest concern on the P2 side. The bounded drain's three bounds (watchdog/no-progress/turn cap) were chosen to match the explicit mitigation shape, but the `+5 %` wall-clock guard at the P2 gate (release-mode P10 run on the full suite) is what would actually validate this. Debug-build smoke tests in `temp/js57_repros/` cannot substitute for that timing measurement.
- The §6.1.1 for-loop per-iteration regression risk for P1 is mitigated by `jm_collect_for_init_lexical_names` excluding for-init / for-of / for-in lexical bindings from the module env. The `js57_for_init_regression.js` probe (mirror of `scope-body-lex-open.js`) and the full `language/statements/for*` cluster are the gates for the release admission run.
