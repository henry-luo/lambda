# Transpile_Js55_Es2024 — ES2024 Long-Tail Cleanup: RAB Iteration, TLA Diversity, BigInt-TA, Final Polish

Date: 2026-06-13

Status: P0 + P1 + P10 + P11 + P12 + P13 + P14 + P16 + P20 (all partial) done (43 admittable tests cleared total: 12 P1 + 1 P10b fix + 1 P10a skip + 4 P11 + 2 P12 + 2 P13 + 2 P14 + 1 P16 + 18 P20); ~30 admittable failures remain (was 73). Latest baseline guard: 40187/40187, 0 regressions, 164.4 s. Latest full run: 40230 passing, 43 improvements, 0 regressions. See §12.15 for P20 closure-capture cluster fix.

Js55 closes the long tail of ES2024 failures left after [Transpile_Js54_Es2024.md](Transpile_Js54_Es2024.md) finished the three Gates (resizable-arraybuffer, arraybuffer-transfer, regexp-v-flag) and the post-Js54 push that picked up another 68 tests (RegExp.prototype.unicodeSets getter, ArrayBuffer.prototype.detached, compound `\p{}`/`\q{}` set ops, UTF-8 emit fix, string set-arithmetic). Js54 admitted the bulk of ES2024 — Js55's job is the 73 individually-failing tests still in the failure list. They split cleanly into three clusters with very different fix surfaces.

Unlike Js54 (net-new feature implementation across three subsystems), Js55 is **engine bug-fix work** on existing subsystems: TypedArray iteration paths need to handle mid-iteration resize callbacks; the top-level-await codegen has several independent issues around strict-mode and async resolution; a handful of long-tail patterns finish off the v-flag and Object semantics. The work is narrower per phase than Js54, but the resizable-arraybuffer cluster is structurally the hardest.

## 1. Starting Baseline

Current checked-in release baseline at Js55 start (post-Js54 + post-push):

```text
# Scope: ES2024 (skip ES2025+ features)
# Total passing: 40187
# Total tests: 42889  Skipped: 2627  Batched: 40262  Passed: 40187  Failed: 73
# Runtime: 150–195s total wall (varies by run; see §1.1)
# Batch size: batched 50 tests/process; async 50 tests/process
```

The Js55 acceptance bar:

- Passing count stays `>= 40187` after every phase.
- Regressions count is `0` at every phase boundary.
- 0 batch-lost, 0 crash-exits at the gate of every admission run.
- `t262_partial.txt` retains the existing flaky-TLA entries (`top-level-ticks{,2}.js`) — those are tracked separately in Phase E1 below, not on the partial-list ceiling.
- Total runtime stays within `+5%` of the last clean Js54 release-run-007 baseline (157.8s). The `\p{RGI_Emoji}` rewriter expanded patterns considerably; that ceiling is already negotiated, but Js55 phases should not widen it further.
- Final `# Scope:` line stays at `ES2024 (skip ES2025+ features)` unless Js55 finishes a meaningful ES2025+ feature cluster — out of scope here; see Js56 candidate features in §8.

### 1.1 Runtime drift note

Js54 release runs vary between 150 s and 195 s depending on memory pressure and the 45 KB `\p{RGI_Emoji}` pattern's RE2 compile cost. Js55 phases should benchmark against the average of 3 runs, not a single number. Phase boundary may accept up to `+5%` of that 3-run average, not the single P0 number from §1.

## 2. What Js54 Deferred

Js54's §11 / §12 documented the post-Js54 expectation as "ES2024 conformant for test262 baseline scope." It did not enumerate the 73 remaining failures because the failure-capture run happened after Js54 P13. This proposal pins those 73 tests.

| Cluster | Tests | Already pass | To admit | Fix surface |
|---|---:|---:|---:|---|
| **Gate D — resizable-arraybuffer (iteration)** | ~120 | ~69 | 51 | TA index access + length re-derivation across `valueOf`/`toString` callbacks during iteration; BigInt-TA coercion path |
| **Gate E — top-level-await diverse** | ~50 | ~30 | 20 | Strict-mode `name`-property assignment, $DONE plumbing, Promise resolver wiring |
| **Gate F — v-flag / property tail** | ~3 | 0 | 3 | Last `\p{StringProperty}` negative tests + Unicode 17 emoji + v-flag String.replace coercion |
| **Gate G — TypedArray makePassthrough + misc** | ~15 | 6 | 9 + 6 | TA iteration with passthrough-constructor + ArrayBuffer options checks + misc Object/Reflect tests |
| **Total** | **~190** | **~105** | **~85 admissible** + flaky | of which ~73 are clean failures, ~10–12 will likely stay deferred to Js56 |

The probe artifacts to retain in `temp/js55_repros/`:

- `temp/js55_failures.tsv` — current 73-row failure manifest (generated at `--write-failures=temp/js55_failures.tsv`)
- `temp/js55_repros/gate_d_rab_iter.txt` — Gate D test paths (~51 entries)
- `temp/js55_repros/gate_e_tla.txt` — Gate E test paths (~20 entries)
- `temp/js55_repros/gate_g_misc.txt` — Gate G test paths (~15 entries)

## 3. The Four Gates

### Gate D — resizable-arraybuffer iteration semantics (51 tests)

This is the deferred §11 cluster from Js53/Js54: Array.prototype methods that iterate over a TypedArray view of a resizable ArrayBuffer must re-derive bounds **after every callback** that could trigger a resize. Today Lambda caches the length at entry; the tests prove this by passing a `valueOf` callback that shrinks the buffer mid-iteration.

**Failure-shape breakdown** (from `temp/js55_failures.tsv`):

| Failure cluster | Count | Root cause |
|---|---:|---|
| `Uncaught TypeError: Cannot convert a BigInt value to a number` | 11 | `Array.prototype.fill.call(bigIntTA, BigInt(n))` — Array fill path coerces value to Number before storing; spec says use the TypedArray's element-type semantics. |
| `Actual [0,0,...] expected [0, undefined, 0, ...]` | ~12 | Array methods like `findIndex`/`find` over an out-of-bounds TA index should return `undefined`, not the stale buffer byte. |
| `Cannot access 'taFull' before initialization` | 4 | TDZ false-positive from a secondary test-fixture error — the `let taFull = ...` line throws, then the catch handler reads `taFull` and trips TDZ. Root cause is one of the earlier issues. |
| `Invalid array buffer length` (RangeError) | 4 | TA constructor with non-fixed length re-validates against the wrong upper bound (uses `byte_length` instead of `max_byte_length`). |
| `Expected a TypeError to be thrown but no exception` | 3 | Method completes silently when it should detect OOB. |
| `Expected [6,4,undefined,undefined]` vs actual `[6,4]` | 2 | Array iteration treats shortened buffer as truncated; should iterate over the original length and yield `undefined` for OOB indices. |
| `Cannot perform %TypedArray%.prototype.set on OOB ArrayBuffer` | 1 | Over-eager OOB throw — `.set` should accept the call and silently no-op when target is OOB. |
| `Cannot perform %TypedArray%.prototype.join on OOB ArrayBuffer` | 1 | Same — `.join` should treat OOB length as 0. |
| Other shrink/grow content mismatches | ~13 | Various mid-iteration resize handling gaps. |

**Highest-leverage fix** is the BigInt-TA Array-method coercion: 11 tests hit one site. Fix-first.

Fix surface:

1. **`Array.prototype.fill`** ([lambda/js/js_runtime.cpp:22401](../../lambda/js/js_runtime.cpp)) needs a BigInt-TA-aware value path: if `this` is `BigInt64Array`/`BigUint64Array` and `value` is a BigInt, pass through as BigInt to `js_property_set`. Today the generic fill calls `js_property_set(object, key, value)` which goes through TA's number-coercion path. Fix at the TA-prototype set boundary (in [lambda/js/js_typed_array.cpp](../../lambda/js/js_typed_array.cpp)) so BigInt typed-array `Set` accepts BigInt without coercing.
2. **Same boundary for `Array.prototype.{copyWithin,reverse,slice,splice,toSorted,toReversed,toSpliced}`** — the generic Array-method shim at [lambda/js/js_runtime.cpp:8610+](../../lambda/js/js_runtime.cpp) needs to know when `this` is a TypedArray and route accordingly.
3. **TypedArray iteration re-derivation** — every `for (i = 0; i < cached_length; i++)` loop in TA prototype methods needs to re-read the current OOB-aware length **after** any `js_call_function` to a callback (`callbackfn`, `comparefn`, `valueOf`, `toString`). Affected methods: `every`, `filter`, `find`, `findIndex`, `findLast`, `findLastIndex`, `forEach`, `map`, `reduce`, `reduceRight`, `some`, `sort`. ~50–80 LOC of `int current_length = js_typed_array_current_length(ta); if (i >= current_length) break;` insertions per method.
4. **Array methods that iterate TA inputs** — `Array.prototype.{at,copyWithin,fill,slice,splice,findIndex,find,findLast,findLastIndex,reverse,toSorted,toReversed,toSpliced,with}` need the same discipline when `this` is a TA. Reuses the same helper as (3).
5. **TA construction range-check** at the constructor site: when constructing from a buffer with length argument, validate against `min(buffer->byte_length, buffer->max_byte_length)` not just `buffer->byte_length`.

Risk: medium-to-high. Per-method audit is mechanical but easy to miss a site. **Phase split** below isolates the highest-leverage fixes from the long tail.

#### D1 — BigInt-TA fill (11 tests, ~30 LOC)

Single-site fix at the TA `Set` boundary. Most leverage per LOC of any Js55 phase.

#### D2 — TA prototype method length re-derivation (~25 tests)

Walk every TA prototype method ([lambda/js/js_typed_array.cpp](../../lambda/js/js_typed_array.cpp) `js_typed_array_*` and [lambda/js/js_runtime.cpp:11500–11800](../../lambda/js/js_runtime.cpp)) and insert post-callback OOB checks. Add a `js_typed_array_validate_or_truncate(ta, current_i, &out_length)` helper that returns the current valid length or signals OOB.

#### D3 — Array.prototype methods on TA inputs (~10 tests)

Same discipline as D2 but at the Array-method boundary in [js_runtime.cpp:22401–22550](../../lambda/js/js_runtime.cpp). The TA-aware shim at [js_runtime.cpp:8610](../../lambda/js/js_runtime.cpp) already routes some methods through a TA-specific path; D3 ensures all the affected methods are wired.

#### D4 — TA constructor max-byte-length check (~4 tests)

One-line fix at the TA constructor's length-argument validation in [lambda/js/js_typed_array.cpp:js_typed_array_construct](../../lambda/js/js_typed_array.cpp).

### Gate E — top-level-await diverse (20 tests)

This is a heterogeneous cluster — five independent issues, each requiring its own fix.

| Failure cluster | Count | Root cause |
|---|---:|---|
| `Cannot assign to read only property 'name' of object` | 7 | Module strict-mode triggers NamedEvaluation for `export let/const/var x = await new Foo(...)` and tries to set `.name` on the result of `new Foo(...)`. Result is an instance; instances have a (configurable, non-writable) `.name` inherited from `Foo.prototype` — assignment silently fails in sloppy mode, throws in strict (module) mode. NamedEvaluation should only apply when the rhs is an anonymous function expression, class expression, or arrow function — not when it's `await NewExpression`. |
| `async test did not call $DONE` | 4 | TLA module's top-level await chain doesn't propagate fulfillment back to the host's `$DONE` callback. The harness sets `$DONE` as `then(undefined, reportError)`; Lambda fulfills the module but never resolves the top-level capability. Fix in [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp) — after `js_main` returns, await the module's `[[TopLevelCapability]]` and call $DONE. |
| `Promise resolver is not a function` | 3 | These tests use `Promise.withResolvers()` in an imported `setup_FIXTURE.js`. The export from the fixture returns `{resolve, reject, promise}`; Lambda's module loader is dropping the resolve/reject function refs across the import boundary. Probably the function items become `null` after the export-binding handoff. |
| `crashed with signal 11` (top-level-ticks*) | 2 | Pre-existing flaky SIGSEGV in TLA tests with Promise.then chains. Not new — already in `t262_partial.txt`. Js55 should either fix root cause or formally accept as flaky-partial (per Js52 §10 kill-switch precedent). |
| `Expected SameValue(??[object Object]??, ??42??)` etc. | 4 | `await thenable` returns the thenable object itself instead of resolving it. `js_async_must_suspend` ([js_runtime.cpp:28678](../../lambda/js/js_runtime.cpp)) calls `js_promise_resolve` which queues a thenable job, but the async state machine's resume isn't waiting for the queued job to settle. Suspension returns 1, but the state machine's resume re-reads `js_async_resolved_value` which is the wrapped pending promise, not its eventual fulfillment value. |

#### E1 — TLA-NamedEvaluation skip (7 tests, ~10 LOC)

In [build_js_ast.cpp](../../lambda/js/build_js_ast.cpp) AssignmentExpression/VariableDeclaration handling, the NamedEvaluation rule must check: rhs is `await` expression → do NOT emit `js_function_set_name` call. The MDN spec list of NamedEvaluation triggers excludes `AwaitExpression`. Single rule, ~3–5 sites.

#### E2 — Top-level await capability chaining (4 tests)

After Js54's `_lambda_rt` save/restore around `js_main` (which fixed the TLA + template-literal SIGSEGV in Js54), the next step is plumbing the TLA top-level capability. Spec §16.2.1.5.3 "ExecuteAsyncModule" creates a promise capability; current Lambda module execution never resolves it. After `js_main` returns, the module is "evaluated" but the capability is still pending — host calls `then` on it but the resolve never fires.

Fix in [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp): after `js_main` returns, check for a top-level capability; if present, fulfill it with `undefined`. Drain microtasks before exit.

#### E3 — Promise.withResolvers across module import (3 tests)

Probe with a focused test: `export const { resolve, reject, promise } = Promise.withResolvers();` then `import { resolve, ... } from "./fixture.js"; resolve(42);`. Compare the imported `resolve` ref's identity against the module's local `resolve`. If they differ, the import binding rewriter is creating a wrapper. Fix in the module export/import handoff path.

#### E4 — Thenable await resume (4 tests)

The state machine in [js_runtime.cpp:28658+](../../lambda/js/js_runtime.cpp) needs to be aware that a queued thenable job's result fulfills the wrapped promise, not the wrapped's then() return. Trace: `js_promise_enqueue_thenable_job` → microtask runs → calls `thenable.then(resolve, reject)` → user code calls `resolve(42)` → wrapped promise settles to `42`. The async state machine's resume needs to read `wrapped->result` after resumption.

#### E5 — top-level-ticks SIGSEGV decision

Either fix the underlying issue (likely a use-after-free in the TLA + promise-then trampoline) or accept as flaky-partial in `t262_partial.txt`. Recommend: fix it; the crash is deterministic per-batch but flaky across batches due to memory layout, which suggests a real use-after-free.

### Gate F — v-flag / property-escape tail (3 tests)

Three tests after the Js54-push left:

1. `built-ins/RegExp/unicodeSets/generated/rgi-emoji-17.0.js` — uses Unicode 17 emoji. Js54's tables are Unicode 16.0. Out of scope unless the test-262 sources upgrade. **Recommend defer to Js56.**
2. `built-ins/String/prototype/replace/regexp-prototype-replace-v-u-flag.js` — String.replace edge case where `/v` flag interacts with the replacement function's argument coercion. See `temp/js55_failures.tsv` row for exact assertion.
3. `built-ins/RegExp/unicodeSets/generated/character-class-escape-difference-string-literal.js` (1 leftover) — a `[\d--\q{ab}]` edge case where my string-set-arithmetic doesn't handle the case where a multi-byte string in `\q{}` happens to be subtracted from a single-codepoint shorthand.

Each test is its own work item; F should be ~50 LOC across the three.

### Gate G — TypedArray makePassthrough + misc (15 tests)

The "makePassthrough" test helper constructs a TA via a custom Symbol.species constructor that proxies through to the test's real construction. Lambda's Symbol.species path was partially fixed in Js54 P6 but several TA prototype methods still bypass it.

| Test path | Issue |
|---|---|
| `built-ins/TypedArray/prototype/filter/*` | filter() doesn't use Symbol.species |
| `built-ins/TypedArray/prototype/map/*` | map() result construction misses species |
| `built-ins/TypedArray/prototype/subarray/*` | subarray() misses species |
| `built-ins/Object/getOwnPropertyDescriptors/*` | one remaining order-of-keys edge case (post-Js54 partial fix) |
| `built-ins/Reflect/construct/*` | one Reflect.construct case |
| `built-ins/Array/prototype/includes/*` | resizable-buffer includes() edge case |
| `built-ins/Symbol/replace/*` | Symbol.replace dispatch on `/v` regex |
| `built-ins/ArrayBuffer/options-*` | ArrayBuffer constructor options-bag validation order |
| Other | small individual fixes |

Each is ~10–30 LOC. The cluster is "long tail one-off bugs" rather than a coherent feature.

## 4. Phase Plan

Phases ordered to minimize blast radius. Gate D dominates the failure count and is the highest leverage; do D first. Gate E is independent and can branch. Gate F is small. Gate G is the long tail — schedule as cleanup after the others land.

### P0 — Baseline confirmation and probe

Goal: confirm the Js55 starting baseline and generate per-gate probe artifacts.

Work:

1. Re-run the release js262 guard. Confirm `40187 / 40187`, 0 regressions, 0 batch-lost, 0 crash-exits. Capture runtime as the 3-run average baseline.
2. Generate `temp/js55_failures.tsv` via `--write-failures`.
3. Split into `temp/js55_repros/gate_d_rab_iter.txt`, `temp/js55_repros/gate_e_tla.txt`, `temp/js55_repros/gate_f_v_tail.txt`, `temp/js55_repros/gate_g_misc.txt`.
4. Snapshot the current `t262_partial.txt` to confirm only the 2 SLOW entries + TLA crashes are tracked.

Acceptance:

- 3 runs averaged, runtime within 5% of each other.
- Probe artifacts checked in to `temp/js55_repros/`.

### P1 — Gate D1: BigInt-TA fill path (11 tests)

Single-site fix. Highest leverage per LOC of any Js55 phase.

Work:

1. Trace `Array.prototype.fill.call(bigInt64Ta, BigInt(n), 1, 2)`. Confirm it routes through `js_array_generic_fill` → `js_property_set` → TA Set → `js_to_number(value)` → BigInt throws.
2. In the TA `Set` boundary at [lambda/js/js_typed_array.cpp:js_typed_array_set](../../lambda/js/js_typed_array.cpp): when `ta->element_type` is `JS_TYPED_BIGINT64` or `JS_TYPED_BIGUINT64`, call `js_to_bigint(value)` instead of `js_to_number(value)`.
3. Verify the existing `built-ins/BigInt64Array/*` tests still pass byte-for-byte.

Acceptance:

- 11 BigInt-TA fill tests pass.
- `built-ins/BigInt64Array/prototype/fill/*`, `built-ins/BigUint64Array/prototype/fill/*` plus the `Array.prototype.fill` calls on bigint TAs.
- Release guard clean.
- Passing count rises by ~11.

### P2 — Gate D2: TA prototype method length re-derivation (~25 tests)

Risk class: medium. Touches ~12 TA prototype methods.

Work:

1. Introduce a `js_typed_array_current_valid_length(ta_item)` helper in [js_typed_array.cpp](../../lambda/js/js_typed_array.cpp) returning `min(ta->length, max(0, ab->byte_length / element_size - ta->byte_offset / element_size))`.
2. For each method `every`, `filter`, `find`, `findIndex`, `findLast`, `findLastIndex`, `forEach`, `map`, `reduce`, `reduceRight`, `some`, `sort` in `js_runtime.cpp`'s TA-method dispatch, insert post-callback OOB check: `current_len = js_typed_array_current_valid_length(ta); if (i >= current_len) break;`.
3. For OOB indices encountered during iteration (when `current_len < cached_len`), yield `undefined` to the callback per spec semantics.
4. `join` / `set` / `slice` need OOB-aware truncation, not throw, when the buffer shrinks.

Risk controls:

- Pre-flight all `built-ins/TypedArray/prototype/*` tests on debug. Diff failures before/after; only `resizable-buffer` tests should change state.
- Diff any `Array.prototype` test outputs — they should be byte-identical.

Acceptance:

- ~25 mid-iteration-resize tests pass.
- 0 regressions on existing TA tests.
- Release guard clean.
- Passing count rises by ~25.

### P3 — Gate D3: Array.prototype methods on TA inputs (~10 tests)

Risk class: small. Same discipline as P2, applied at the Array-method shim.

Work:

1. In `js_runtime.cpp:8610+` (TA-aware Array-method shim), the methods that already route through generic handlers (`fill`, `copyWithin`, `reverse`, `sort`) need the same post-callback OOB check.
2. `find`, `findIndex`, `findLast`, `findLastIndex` need to yield `undefined` for OOB indices via `js_array_method_has_property` returning false for index >= current_length.

Acceptance:

- ~10 Array-method-on-TA tests pass.
- 0 regressions.
- Release guard clean.

### P4 — Gate D4: TA constructor max-byte-length check (~4 tests)

One-line fix per relevant constructor case.

Work:

1. In [js_typed_array.cpp:js_typed_array_construct](../../lambda/js/js_typed_array.cpp), the variant that takes `(buffer, byteOffset, length)`: when `length` is given, validate against `buffer->max_byte_length / element_size - byteOffset / element_size`, not `buffer->byte_length / element_size - byteOffset / element_size`.

Acceptance:

- 4 TA-construction range-error tests pass.
- Release guard clean.

### P5 — Gate E1: TLA NamedEvaluation skip for `await NewExpression` (7 tests)

Single-rule fix in the AST→MIR lowering.

Work:

1. In [build_js_ast.cpp](../../lambda/js/build_js_ast.cpp) where AssignmentExpression / VariableDeclaration RHS triggers `js_function_set_name`, add a check: if the RHS is an `AwaitExpression`, skip NamedEvaluation entirely. The spec list excludes await; we mistakenly include it.
2. Same for `YieldExpression` while we're there (in case any future generator test hits it).

Acceptance:

- 7 TLA `export let/const/var x = await new X(...)` tests pass.
- Existing tests where `const fn = function() {}` still has its name set to "fn" continue to pass.
- Release guard clean.

### P6 — Gate E2: Top-level await capability chaining (4 tests)

Work:

1. In [js_mir_module_batch_lowering.cpp](../../lambda/js/js_mir_module_batch_lowering.cpp:transpile_js_module_to_mir), after `js_main` returns: if the module had top-level await, the module-level capability should fulfill with `undefined`. Currently we return the module's namespace object but never settle the host's outer promise.
2. After `js_main` returns, drain microtasks. The `$DONE` callback is attached via `.then()`; without a microtask drain, it never fires.

Acceptance:

- 4 TLA `$DONE` tests pass.
- Existing module tests pass byte-for-byte.
- Release guard clean.

### P7 — Gate E3: Promise.withResolvers across module import (3 tests)

Work:

1. Reproduce the issue with a focused test: `setup.js` exports `{resolve, reject, promise}` from `Promise.withResolvers()`. `main.js` imports them. The import binding for `resolve` must point to the same function object that the closure in `setup.js`'s promise captures.
2. Trace the import-binding rewriter — if it wraps the import in a getter or copies the value, the closure identity breaks.

Acceptance:

- 3 TLA tests using `Promise.withResolvers` cross-module pass.
- Existing `Promise.withResolvers` tests (Js52 admitted set) continue to pass.
- Release guard clean.

### P8 — Gate E4: Thenable await resume (4 tests)

Work:

1. In [js_runtime.cpp:28678](../../lambda/js/js_runtime.cpp) `js_async_must_suspend`, after `js_promise_resolve(value)` is called and returns a pending promise, ensure the async state machine's resume reads from the **resolved** wrapped promise, not the still-pending wrapped item.
2. Probe: `await {then: function(res) { res(42); }}` should return 42, not the thenable.

Acceptance:

- 4 TLA thenable tests pass.
- Existing async/await tests pass byte-for-byte.

### P9 — Gate E5: top-level-ticks SIGSEGV decision

Investigate the crash via ASAN reduction. Options:

1. **Fix root cause.** Recommended if reduce-to-repro shows a real use-after-free.
2. **Accept as flaky-partial.** Add to skip list explicitly with a comment pointing to a tracking issue. The 2 tests already crash flakily in `t262_partial.txt`.

Acceptance:

- Either the 2 SIGSEGV tests pass, OR they have an explicit skip-list entry with rationale.

### P10 — Gate F: v-flag / property tail

Work:

1. **`rgi-emoji-17.0.js`** — out of scope (Unicode 17 not pinned). Add to skip list with comment "requires Unicode 17 property tables; tracked for Js56."
2. **`regexp-prototype-replace-v-u-flag.js`** — investigate the replace edge case.
3. **`character-class-escape-difference-string-literal.js` leftover** — investigate the multi-byte difference edge case in the string-set-arithmetic.

Acceptance:

- 2 tests pass (replace + character-class-escape-difference).
- 1 skip-list entry added with Js56 rationale.

### P11 — Gate G: TA Symbol.species + Object descriptors + misc (~15 tests)

Risk class: low individually, but high admission gate due to count.

Work:

1. Audit TA `filter`, `map`, `subarray` for Symbol.species respect.
2. Fix the `Object.getOwnPropertyDescriptors` order-after-define-property remaining edge case.
3. Fix `Reflect.construct` edge case (1 test).
4. Fix `Array.prototype.includes` resizable-buffer edge case (1 test).
5. Fix `Symbol.replace` dispatch on `/v` regex (1 test).
6. Fix `ArrayBuffer.options-maxbytelength-compared-before-object-creation` (1 test).
7. Other misc (~8 tests).

Acceptance:

- ~13 tests pass (probably 2 stay deferred).
- 0 regressions.
- Release guard clean.

### P12 — Final admission + stability guard

Run release `--update-baseline` to admit everything. Run a stability guard to confirm 0 regressions.

Acceptance:

- Baseline updated with new commit hash.
- 0 regressions, 0 batch-lost, 0 crash-exits.
- Final passing count recorded.

## 5. Per-Phase Guard Commands

Same as Js54 §5. The contract is identical.

Pre-flight (debug build):

```bash
make build && make build-test
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/typed_array_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/buffer_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/regex_*'
```

Release js262 guard:

```bash
make release
make -C build/premake test_js_test262_gtest config=release_native
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js55_pN_release_guard.tsv \
  --gtest_brief=1
```

(Replace `pN` with the phase number.)

The guard tsv must report:

- `Failed: 0` (in baseline)
- `Regressions: 0`
- `Passing >= 40187 + sum of admissions from prior phases`
- `Skipped` non-increasing
- Total runtime within `+5%` of the 3-run average from P0

Final update (only after P12 guard is clean):

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --update-baseline \
  --write-failures=temp/js55_update_baseline.tsv \
  --gtest_brief=1
```

## 6. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| **D2 length re-derivation introduces a perf cliff** in tight TA-method loops | medium — runtime drift | Bench `built_ins/TypedArray/prototype/{forEach,map,reduce}/*` before/after. If 3-run average drifts > +5%, add an opt-out fast-path: `if (!ta->buffer || !ta->buffer->resizable) skip re-derivation`. |
| **D1 BigInt-TA fix breaks existing BigInt64Array semantics** | high — silent wrong values | Pre-flight all `built-ins/BigInt64Array/*` tests; require byte-identical passing. |
| **D3 Array-method-on-TA refactor breaks the Array-on-Array baseline tests** | high — false positive | The TA-aware shim is in a narrow branch; the Array-on-Array path is unaffected. Diff `built_ins/Array/prototype/*` outputs that don't involve TA. |
| **E1 NamedEvaluation skip too broad, breaks `const fn = await async function() {}` (anon)** | medium — silent name absence | Spec: NamedEvaluation skips AwaitExpression entirely. The result of `await Promise.resolve(function() {})` has no name. Verify with focused test. |
| **E2 microtask drain misses a critical resolution** in non-TLA modules | low — passing test goes silent | Only drain when the module had top-level await. Detect via flag set during MIR codegen. |
| **E4 thenable resume fix breaks the existing native-promise await chain** | high — async tests stop resolving | Add a focused test for native-promise (`await Promise.resolve(42)`), thenable (`await {then: ...}`), non-promise (`await 42`). All three must resolve. |
| **E5 SIGSEGV "fix" doesn't actually fix the use-after-free** | medium — flakiness returns | If the reduction doesn't yield a clear repro, formally accept as flaky-partial rather than land a speculative patch. |
| **F2 v-flag String.replace fix breaks the existing String.replace baseline** | medium | Pre-flight all `built-ins/String/prototype/replace/*` tests. Diff outputs. |
| **G2 Symbol.species TA-method refactor cascades to non-TA Array methods** | medium | TA-species lives in a narrow code path; verify no Array.prototype.* test changes. |
| **Cumulative drift exceeds +5% over the Js55 phases** | medium | Per-phase +5% ceiling, multi-run trend. If a single phase pushes over, revert before the next phase opens. |
| **A gate proves larger than this proposal anticipates** | medium | Js52 §10 / Js53 §11 / Js54 §11 kill-switch precedent. Stop at the gate, document deferral. |

## 7. Completion Criteria

| Criterion | Target | Notes |
|---|---|---|
| Skip-list ES2024 entries | unchanged | All ES2024 features stayed unblocked at the end of Js54 P12. Js55 doesn't re-skip anything. |
| Baseline scope header | `ES2024 (skip ES2025+ features)` | unchanged from Js54. |
| Passing count rises by ~73 (best case) or ~60 (realistic) | ≈ 40,247–40,260 | Js54 final 40,187 + Gate D ~50 + Gate E ~15–18 + Gate F ~2 + Gate G ~13 ≈ +80, minus the ~10 that may stay deferred. |
| Failures (in baseline) | 0 | unchanged contract. |
| Regressions | 0 | unchanged contract per phase. |
| `t262_partial.txt` | <= 4 (no growth from current) | the 2 SLOW + 2 flaky-TLA-crash entries either get fixed (E5) or stay flagged. |
| Runtime within +5% of 3-run-average baseline | yes | enforced per phase. |
| New regression tests landed | 4+ | `test/js/regression_js55_bigint_ta_fill.js`, `regression_js55_rab_iter_resize.js`, `regression_js55_tla_named_eval.js`, `regression_js55_thenable_await.js`. |

## 8. Out Of Scope

- **Unicode 17 property tables** (`rgi-emoji-17.0.js`). Test262 sources currently pin Unicode 16; if upstream updates to Unicode 17, regenerate the existing 16.0 tables via the same generator script and re-admit.
- **ES2025+ features.** `Float16Array`, `iterator-helpers` (already partial), `set-methods` (already), `import-attributes`, `Promise.try`, `RegExp.escape`, `regexp-modifiers`, `regexp-duplicate-named-groups`, `json-modules`, `json-parse-with-source`, `Math.sumPrecise`, `Uint8Array.prototype.{toBase64,fromBase64,toHex,fromHex}` — defer to Js56.
- **Stage 3 / Stage 2 proposals.** `Temporal`, `ShadowRealm`, `decorators`, `explicit-resource-management`, `Atomics.pause`, `import-defer`, `Error.isError` — defer.

## 9. Phase Effort Estimates

| Phase | Tests | LOC | Risk | Estimate |
|---|---:|---:|---|---|
| P0 baseline + probe | 0 | — | trivial | 0.5 h |
| P1 Gate D1 BigInt-TA fill | 11 | ~30 | low | 1–2 h |
| P2 Gate D2 TA-method re-derivation | 25 | ~200 | medium | 4–6 h |
| P3 Gate D3 Array-on-TA re-derivation | 10 | ~80 | small | 2–3 h |
| P4 Gate D4 TA constructor max-byte | 4 | ~5 | trivial | 0.5 h |
| P5 Gate E1 TLA NamedEval skip | 7 | ~15 | small | 1 h |
| P6 Gate E2 TLA capability chain | 4 | ~50 | medium | 2–3 h |
| P7 Gate E3 Promise.withResolvers import | 3 | ~30 | medium-high | 2–3 h |
| P8 Gate E4 Thenable await resume | 4 | ~30 | medium | 2 h |
| P9 Gate E5 top-level-ticks SIGSEGV | 2 | ?? | high (unknown) | 2–4 h or kill-switch |
| P10 Gate F v-flag tail | 2 + 1 skip | ~50 | low | 1–2 h |
| P11 Gate G misc cleanup | ~13 | ~150 | low-medium | 3–4 h |
| P12 admission + guard | 0 | — | trivial | 1 h |

**Total estimated effort: ~20–30 hours** of focused engine work, dominated by Gate D2 (TA-method audit) and Gate E2/E4 (TLA + thenable plumbing).

## 10. Why this is "long tail" rather than "next feature"

Js53 admitted easy ES2024 features. Js54 admitted the implementation-heavy ones (RAB OOB + transfer + `/v`). Js55 closes the **integration gaps** that Js53 and Js54 left: RAB iteration semantics (which are technically Gate A but harder than the single-call OOB fixes that Js54 P3–P5 landed), TLA host integration (Js52–Js54 added TLA syntax + runtime; Js55 finishes host callback wiring), and the various one-off bugs that aren't really a "feature" anymore.

When Js55 P12 lands, Lambda's ES2024 baseline conformance is **>= 99.7%** of all batched ES2024 tests. The remaining gap is intentional (Unicode 17, deferred features) or platform-specific (resource limits on the largest patterns).

## 11. Anticipated Final Numbers

| Metric | Js54 final | Js55 P12 target | Js55 best case |
|---|---:|---:|---:|
| Baseline fully passing | 40,187 | ≈ 40,250 | ≈ 40,260 |
| ES2024 admissions from Gate D | 0 | + ~50 | + ~50 |
| ES2024 admissions from Gate E | 0 | + ~15 | + ~18 |
| ES2024 admissions from Gate F | 0 | + ~2 | + ~2 |
| ES2024 admissions from Gate G | 0 | + ~13 | + ~13 |
| Scope line | ES2024 | ES2024 | ES2024 |
| Failures in baseline | 0 | 0 | 0 |
| Regressions | 0 | 0 | 0 |
| Total ES2024 tests admitted across Js53+Js54+Js55 | ≈ 648 | ≈ 728 | ≈ 730 |

Js55's primary deliverable is "ES2024 long-tail closed." After Js55 P12, the remaining failures should be limited to:

- 2 SLOW pre-existing tests (unchanged from Js54)
- 0–2 SIGSEGV TLA tests (depending on E5 outcome)
- ~5–10 explicitly-deferred Unicode 17 / Js56 candidates

No further ES2024 work is planned after Js55. The next proposal is Js56, opening ES2025 features.

## 12. Investigation Notes (added during implementation)

### P0 Result (2026-06-13)

3 baseline runs, all clean:

| Run | Wall (s) | Passing | Failed | Regressions |
|---|---:|---:|---:|---:|
| 1 | 135.5 | 40187 | 0 | 0 |
| 2 | (clean) | 40187 | 0 | 0 |
| 3 | (clean) | 40187 | 0 | 0 |

The 3-run average is ≈135 s — below the proposal's 150–195 s estimate. The §1.1 +5% ceiling is therefore tight (~142 s).

Per-gate failure split written to `temp/js55_repros/`:

- `gate_d_rab_iter.txt` — 51 entries (matches proposal exactly)
- `gate_e_tla.txt` — 20 entries (matches)
- `gate_f_v_tail.txt` — 2 entries (proposal expected 3; the `character-class-escape-difference-string-literal.js` referenced in §3 Gate F is **not** in the failure manifest)
- `gate_g_misc.txt` — 0 entries (proposal expected ~15)

The Gate G/D overlap means the proposal's Gate G cluster (TA `makePassthrough` species tests) is already accounted for inside Gate D's 51 resizable-arraybuffer entries via the `TypedArray;resizable-arraybuffer` joint feature tags. Total still adds to 73 as expected — only the cluster decomposition differs.

### P1 Investigation — root cause does NOT match §3 Gate D1

The proposal §3 Gate D1 claims 11 tests fail because `Array.prototype.fill` coerces values to Number before storing, instead of dispatching to the TypedArray BigInt path. The recommended fix is to update the TA `Set` boundary to accept BigInt without coercing.

Tracing the actual code reveals:

1. `js_typed_array_set` at [js_typed_array.cpp:2045](../../lambda/js/js_typed_array.cpp) **already** has a BigInt branch (lines 2055–2101) that handles `JS_TYPED_BIGINT64` / `JS_TYPED_BIGUINT64` element types via `js_bigint_constructor` and `bigint_to_int64`, **not** `js_to_number`.
2. `js_typed_array_fill` at [js_typed_array.cpp:2194](../../lambda/js/js_typed_array.cpp) **already** dispatches via `is_bigint_array` and calls `js_dataview_to_bigint_value` — the correct ToBigInt path.
3. The `Array.prototype.fill.call(typedArray, ...)` dispatch at [js_runtime.cpp:8597](../../lambda/js/js_runtime.cpp) routes TAs to `js_map_method`, whose fill handler at [js_runtime.cpp:17305](../../lambda/js/js_runtime.cpp) calls `js_typed_array_fill` — the BigInt-correct path.

A series of focused micro-repros confirms every code path described in §3 Gate D1 actually works correctly in isolation:

- `temp/js55_p1_micro.js` — basic `Array.prototype.fill.call(BigInt64Array, BigInt(3), 1, 2)`: PASSES
- `temp/js55_p1_micro2.js` — with `valueOf` resize side-effect: PASSES, fills no elements (correct OOB no-op)
- `temp/js55_p1_micro3.js` — all 11 builtin TA ctors across loop 1: PASSES every ctor
- `temp/js55_p1_micro4.js` — `BigUint64Array` isolated: PASSES
- `temp/js55_p1_micro5.js` — all 3 evil-position loops × 2 BigInt ctors: PASSES all 6
- `temp/js55_p1_micro6.js` — `MyBigInt64Array` subclass (`class extends BigInt64Array`): PASSES
- `temp/js55_p1_microfull.js` — full reproduction of test262 file content using `print` instead of `assert`: PASSES every ctor in loop 1
- `temp/js55_p1_microloops.js` — same for loops 2 and 3: PASSES every ctor
- `temp/js55_p1_concat.js` — `sta.js + assert.js + compareArray.js + resizableArrayBufferUtils.js + typed-array-resize.js` concatenated and run as a single script: PASSES (exit 0)

But the same test under `test_js_test262_gtest.exe --batch-only --batch-file=...` (with or without `--no-hot-reload`) fails with the exact error `Cannot convert a BigInt value to a number`.

The same pattern holds for Gate E1: `temp/js55_e1_micro.js` reproduces the TLA `try { await new Promise(...) }` pattern from `export-lex-decl-await-expr-new-expr.js` and **passes**, but the actual test262 run reports `Cannot assign to read only property 'name' of object`.

### What this means

The failures are **batch-mode-specific**, not engine-level bugs in the call paths §3 identifies. The proposal's root-cause analysis was inferred from the failure error strings without verifying that the code paths described actually fail in isolation. Possible actual root causes:

1. **Harness preamble binding lookup**: in batch mode, `sta.js` / `assert.js` / `resizableArrayBufferUtils.js` are compiled separately as a "preamble" with their module vars persisted. The test then runs as a second module that references preamble vars (`ctors`, `assert`, `BigInt`, etc.) via cross-module binding. A subtle binding-stale or constructor-prototype-shadowing issue at the boundary could mis-coerce values.
2. **Persistent state across runs in the batch process**: `js_batch_reset_to(preamble_var_checkpoint)` at [main.cpp:3469](../../lambda/main.cpp) resets some state but maybe not all — a cached prototype or interned BigInt/Number could leak.
3. **Module-mode flag plumbing**: TLA tests have `flags: [module]` and are dispatched as modules in batch mode. The `is_module` boolean drives strict-mode, NamedEvaluation, and the host capability wiring. A flag mismatch between batch-module and CLI-script execution would explain the divergence.

### Recommended next step

Before further phase work, instrument the batch dispatch path to:

1. Diff the JIT-emitted MIR for a single failing test between (a) batch-mode-as-module and (b) single-script `./lambda.exe js`. The first instruction where the two diverge points at the responsible compile-time codegen condition.
2. Once the divergence is localized, the actual fix may make several phases of §3 unnecessary (a single binding-lookup fix could clear 51+ Gate D entries simultaneously) — or may invalidate the per-phase risk estimates entirely.

Until that diagnostic is run, none of the phase fixes can be validated reliably: a "successful" surgical patch could just be masking the real bug, and a "failing" fix could be working in isolation but blocked by the batch-mode anomaly.

The 13 phases as written remain the appropriate fallback if the diagnostic shows multiple independent batch-mode issues rather than a single underlying one.

### Diagnostic result (2026-06-13)

The divergence localized to `js_new_from_class_object` in [lambda/js/js_runtime.cpp:1630](../../lambda/js/js_runtime.cpp). Specifically, the **subclass-of-typed-array path at line 2042**: `js_typed_array_construct` runs, but the function returns without clearing `js_pending_new_target` / `js_has_pending_new_target`. Every other `return` in the same function clears those flags — this branch was missed.

**Repro**: `new MyBigInt64Array(8)` followed by `new Uint8Array(8)` in the same for-of loop. After iter 1 the pending-new-target globals leak with value `MyBigInt64Array`. Iter 2 enters with `js_has_pending_new_target == true`, so line 1638 picks `effective_new_target = MyBigInt64Array` instead of `Uint8Array`. The Uint8Array dispatch then calls `js_apply_constructed_builtin_prototype(uint8_ta, Uint8Array, MyBigInt64Array)` which sets `uint8_ta.__proto__ = MyBigInt64Array.prototype` — corrupting subsequent `instanceof BigInt64Array` checks for ordinary Uint8Array instances.

This explains:
- **Single-script does not reproduce**: the harness, subClass calls, and test code all run in one compile-and-execute; the `subClass('BigInt64Array')` call returns the class object but the `new MyBigInt64Array(...)` only happens later inside the loop. In the test262 batch driver the `ctors` array is built during harness compile, then the iteration loop runs with the leak surfacing on every `MyBigInt64Array → Uint8Array` transition.
- **Order dependence**: `[MyBigInt64Array, Uint8Array]` triggers; `[Uint8Array, MyBigInt64Array]` does not. The leak from the BigInt subclass corrupts the next call only.
- **Why so many tests fail**: any test that iterates over `ctors` and constructs both subclass and builtin typed arrays trips the bug on the very first `instanceof BigInt64Array` evaluation after the transition. Inside `ArrayFillHelper`, the false `instanceof` routes a Number-value call through the BigInt branch, which then tries `BigInt(...)` on a wrong value — surfacing as the `Cannot convert a BigInt value to a number` error from `js_to_number`.

**Fix**: clear `js_pending_new_target` / `js_has_pending_new_target` before all three return paths in the subclass-TA branch (early-return on exception, return on `ctor_result`, fall-through return on `result`). Single edit in [js_runtime.cpp:2041](../../lambda/js/js_runtime.cpp).

**Result**: 12 of 51 Gate D tests pass with no other code change. The remaining 39 are unrelated to the new-target leak — those are the genuine length-re-derivation / range-check / Symbol.species tests the proposal §3 D2/D3/D4 describes.

The §3 D1 "BigInt-TA fill (~11 tests)" cluster is now closed by this one-site fix at a completely different location than the proposal anticipated. D2/D3/D4 remain as written but now have a credible execution model: the BigInt error was a symptom, not a coercion bug.

### Confirmed baseline contract (P1, 2026-06-13)

| Metric | Pre-P1 | Post-P1 |
|---|---:|---:|
| Fully passed (out of 40262 batched) | 40187 | 40199 |
| Failed (admittable) | 73 | 63 |
| Baseline regressions | 0 | 0 |
| Runtime (release guard wall) | 135.5 s | 144.0 s |

The +5% runtime ceiling (≤142 s) is borderline; 144 s is 2 s over. Js55 phases should hold this line — large added work would need to come with a corresponding optimization or a renegotiated ceiling.

### Phase-by-phase status

| Phase | Tests targeted | Status | Notes |
|---|---:|---|---|
| P0 | — | done | 3 runs averaging 135 s, all 40187/40187 clean. |
| **P1 — D1** | **11** | **done** | One-site fix at js_runtime.cpp:2042-2073 — clears js_pending_new_target across the three return paths of the subclass-of-TA branch. 12 tests fixed (one extra captured by the same fix), 0 regressions. |
| P2 — D2 | ~25 | pending | 39 Gate D failures remaining match proposal §3 D2/D3/D4 description (length re-derivation, OOB checks, RangeError, ReferenceError cascade). |
| P3 — D3 | ~10 | pending | Overlaps with P2 — fix in same site. |
| **P4 — D4** | ~5 | **deferred** | Investigated 2026-06-13. Root cause is NOT a TA constructor max-byte-length check as §3 claims. It's a for-of let-binding closure-capture stale-read in the JIT scope-env mechanism (see §12.6 below). Fix surface is hot, broadly-used JIT path; risky without scope-env protocol audit. |
| P5 — E1 | 7 | pending | Repro confirmed in module mode via batch protocol. Error path: `js_strict_throw_property_error("assign to read only", "name", 4)` fires twice (once per `export let/const = await new Promise(...)`). Investigation traced multiple `js_property_set` of `name=""` before the throw. Root cause requires further tracing (the proposal hypothesis "skip NamedEvaluation for AwaitExpression" needs to be verified — the existing var-decl path at js_mir_statement_lowering.cpp:373/429/504/646 already skips await; the actual emission site is elsewhere). |
| P6 — E2 | 4 | pending | TLA capability chaining. |
| P7 — E3 | 3 | pending | Promise.withResolvers cross-module. |
| P8 — E4 | 4 | pending | Thenable await resume. |
| P9 — E5 | 2 | pending | top-level-ticks SIGSEGV intentionally kept on the failure list (not papered over via t262_partial.txt). Needs a real engine fix. |
| P10(a) — F skip | 1 | done | `rgi-emoji-17.0.js` added to `test/js262/skip_list.txt` with Js56 deferral comment. Per proposal §3 Gate F item 1. |
| **P10(b) — F fix** | **1** | **done** | One-line fix at `js_runtime.cpp:16252` — `utf16_replace` now considers `S->is_ascii`, not just the regex's `needs_utf16_subject`. Root cause was a unit-basis mismatch between `js_regex_exec` (returns `match.index` in UTF-16 code units whenever input has non-ASCII bytes) and `js_regexp_symbol_replace` (was treating `position` as a byte offset). 55/55 String.prototype.replace tests pass; baseline guard clean; 0 regressions. See §12.8 below. |
| **P11(a) — G ArrayBuffer ordering** | 1 | done | Two-prong fix at `js_globals.cpp:js_reflect_construct`: ArrayBuffer pre-check (byteLength + maxByteLength) mirrors the existing SharedArrayBuffer pattern. Fixes `options-maxbytelength-compared-before-object-creation.js`. |
| **P11(c) — G with current-length** | 3 | done | One fix at `js_runtime.cpp:18345` in TypedArray.prototype.with — IsValidIntegerIndex now uses the **current** length (post-coercion), not the cached step-3 length. Spec §22.2.3.34 step 9. Fixes the three `with/*.js` tests. |
| P11 — G remaining | ~10 | pending | Species (TA filter/map/subarray), `set/*`, `freeze/*` all share for-of let-binding closure-capture issue with P4. Defer until that's fixed. |
| **P13 — Array.toLocaleString** | **2** | **done** | One-site mis-registration fix at `js_runtime_builtin_registry.cpp:192` + new enum + dispatch case. `Array.prototype.toLocaleString` was routing to `JS_BUILTIN_OBJ_TO_LOCALE_STRING` (which calls `this.toString()`) — TA receivers then went through TA.join's OOB throw. New `JS_BUILTIN_ARR_TO_LOCALE_STRING` routes correctly through the Array shim. Fixes `Array/prototype/toLocaleString/{resizable-buffer,user-provided-tolocalestring-shrink}.js`. See §12.13. |
| **P14 — TA.set targetLength** | **2** | **done** | One-block fix at `js_typed_array.cpp:js_typed_array_set_from`: remove the spec-incorrect re-OOB check and `target_len` re-capture after the source's length getter ran. Per ES2024 §22.2.3.26.1 step 4 captures targetLength BEFORE the length getter; step 10 uses that captured value; write loop relies on IsValidIntegerIndex to no-op OOB writes. Fixes 2 of 3 set-length-getter tests; third (`this-backed-by-resizable-buffer.js`) is closure-capture cluster. See §12.13. |
| P15 — ArrayBuffer.resize | 1 | deferred | Probe reveals **block-scoped `let` closure-capture bug** — same family as §12.10 for-of let, exposed via `{...}` block. The "one detach check after coercion" spec fix would have no effect. See §12.13. |
| **P16 — for-in/keys on TA** | **1** | **done** | Three edits at `js_globals.cpp`: TA branch added to `js_object_keys` enumerating integer indices then non-internal shape entries; TA index seed added to `js_for_in_keys` at the start of the LMD_TYPE_MAP path with seen-set marking; `__ta__`/`__ab__` added to engine-internal-key suppression. ES2024 §10.4.5: TA integer-indexed properties are enumerable own properties. Fixes `language/statements/for-in/resizable-buffer.js`. See §12.13. |
| P17 (was P12) | admission | pending | Final baseline update. |

### Lessons for future work

1. **Symptom-driven root causes are unreliable for JIT engines.** §3 read the failure error strings and inferred causes per error type. The actual P1 cause was a pending-globals leak surfacing as a coercion error in an entirely different code path. Future ES feature long-tail proposals should require a repro-in-isolation step before committing to phase decomposition.
2. **Batch-mode vs single-script divergence is a load-bearing distinction.** Tests pass in `./lambda.exe js <script>` but fail under `--batch-only` for several Gate D entries. The harness preamble compile boundary and the `assemble_module_test_source` vs script path produce materially different execution. Phase work needs to verify reproduction in the actual test runner — single-script repros mask real bugs and create false reassurance.
3. **One fix can clear a cluster.** D1's 11 tests + 1 extra all came from one missing assignment to `js_pending_new_target = ItemNull`. If E2/E4 share a TLA-state-leak shape (plausible given the symptoms), one fix there could similarly clear most of Gate E rather than the proposed 4 separate fixes.

### P4 (D4) investigation (2026-06-13)

Attempted Gate D4 next per the proposal's "trivial, 0.5h, ~5 LOC" estimate. The diagnosis there was: TA constructor (buffer, byteOffset, length) form validates length against `byte_length` instead of `max_byte_length`.

Probe of `built-ins/Array/prototype/indexOf/coerced-searchelement-fromindex-grow.js`:

```js
for (let ctor of ctors) {                                     // Loop A (14 ctors)
  const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT, 8 * ctor.BYTES_PER_ELEMENT);
  const lengthTracking = new ctor(rab);
  ... fill lengthTracking ...
  let evil = { valueOf: () => { rab.resize(6 * ctor.BYTES_PER_ELEMENT); return 0; } };
  assert.sameValue(Array.prototype.indexOf.call(lengthTracking, n0, evil), -1);
}
for (let ctor of ctors) {                                     // Loop B (14 ctors)
  const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT, 8 * ctor.BYTES_PER_ELEMENT);
  const lengthTracking = new ctor(rab);
  lengthTracking[0] = MayNeedBigInt(lengthTracking, 1);
  let evil = { valueOf: () => { rab.resize(6 * ctor.BYTES_PER_ELEMENT); return -4; } };
  let n1 = MayNeedBigInt(lengthTracking, 1);
  assert.sameValue(Array.prototype.indexOf.call(lengthTracking, n1, -4), 0);
  assert.sameValue(Array.prototype.indexOf.call(lengthTracking, n1, evil), 0);  // ← throws here
}
```

Adding `print("BYTES_PER_ELEMENT:", ctor.BYTES_PER_ELEMENT)` outside the closure showed the correct value (1 for Uint8Array). Adding `print("ctor.BYTES_PER_ELEMENT:", ctor.BYTES_PER_ELEMENT)` *inside* `evil.valueOf` showed `8` (MyBigInt64Array's value, the last ctor of Loop A). evil then computes `6 * 8 = 48` and calls `rab.resize(48)` on a buffer with `maxByteLength = 8` → "Invalid array buffer length" RangeError.

The root cause is NOT the TA constructor at all — it's the same family of JIT specialization bugs as P1's `instanceof`: the closure inside `for (let ctor of ctors)` reads the wrong `ctor`. Both loops happen to bind the iteration variable to the same name `ctor`, and the closure captures a stale scope-env slot from Loop A's last iteration instead of Loop B's current one.

Fix surface lives in the JIT closure-capture / scope-env mechanism for `for-of let` bindings — not in [js_typed_array.cpp:js_typed_array_construct](../../lambda/js/js_typed_array.cpp). The proposal's §3 D4 single-line edit at `js_typed_array_construct` would have no effect on this failure shape.

**Decision**: deferred. The actual fix is in a hot, broadly-used JIT path (closure binding for any `for (let x of …)` body that captures `x`); altering it without a clean understanding of the scope-env protocol risks regressions across hundreds of unrelated tests. P4 is left pending until a focused closure-capture diagnostic runs.

**Pattern observation**: P1 and P4 are both JIT specialization stalenesses surfacing as RangeError/TypeError that the proposal blamed on TA constructors. Likely several of the remaining 39 Gate D / 20 Gate E failures share this shape — the failure error strings should be treated as symptoms, not diagnoses. A targeted audit of "values cached across for-of let-binding iterations" would probably clear another large batch in one site (just like P1's pending-new-target fix).

## 12.7 Recommended next phase (2026-06-13)

Ranked from lowest to highest risk, with two attractive options:

### Option A (recommended) — **P10(a): skip rgi-emoji-17.0 (docs-only phase)**

Single low-risk piece, strictly endorsed by §3 Gate F item 1:

**P10(a) — skip `rgi-emoji-17.0.js`.** §3 Gate F item 1 says verbatim "out of scope unless the test262 sources upgrade. **Recommend defer to Js56**." Added: one entry in `test/js262/skip_list.txt` with the comment "requires Unicode 17 property tables; tracked for Js56." Result: 1 test off the failure list (63 → 62 admittable), 0 lines of C/C++ touched, 0 regression risk. Baseline guard after the change: 40187/40187, 0 regressions, 132.8 s — well inside the +5% ceiling.

P9 (E5 — top-level-ticks SIGSEGV) was considered for the same flaky-partial treatment but is intentionally kept out of `t262_partial.txt`: the proposal's §3.E5 lists the partial-acceptance as a fallback only, and the maintainers want it surfaced as a real failure pressure for a future engine fix rather than papered over. **Keep it on the failure list until a real fix lands.**

### Option B — **P10(b): regexp-prototype-replace-v-u-flag fix**

Single test, targeted to one code path:

- Test asserts `RegExp.prototype[Symbol.replace].call(/𠮷/g, '𠮷a𠮷b𠮷', '-') === '-a-b-'`.
- Lambda currently returns `-a?-??????` — the surrogate-pair character `𠮷` (U+20BB7) is being mis-handled in the match-or-replace path.
- Likely fix surface: `js_runtime.cpp` around the `Symbol.replace` dispatch and/or the UTF-8 ↔ UTF-16 boundary in regex match-iteration.
- Risk: medium — touches a regex code path that the entire String.prototype.replace baseline relies on. Pre-flight required: diff all `built-ins/String/prototype/replace/*` outputs before/after.

Skip if Option A is taken first — Option A gets us into a known-clean state from which Option B can be attempted with a smaller delta.

### Options to AVOID next

- **P2/P3 (D2/D3 length re-derivation)**: requires per-method audit of ~12 TA prototype methods (~50–80 LOC each). High blast radius. Save until a clean baseline is established and the for-of closure-capture pattern from P4 is resolved (some Gate D entries may then disappear without separate fixes).
- **P4 (D4 closure-capture)**: as documented above, fix surface is the JIT scope-env / closure-binding code which underpins every `for (let x of …)` body in the engine. Requires a focused diagnostic separate from the §3 fix plan.
- **P5–P8 (Gate E TLA cluster)**: each has its own batch-mode-vs-script reproduction question still open. Likely all four share an underlying TLA host-binding or pending-state-leak issue (analogous to P1) — a discovery pass should run before per-phase work.

## 12.8 P10(b) walkthrough (2026-06-13)

Failing assertion (first of seven):

```js
const text = '𠮷a𠮷b𠮷';  // 8 UTF-16 code units, 14 UTF-8 bytes
const regex = /𠮷/g;
RegExp.prototype[Symbol.replace].call(regex, text, '-');
// expected: "-a-b-"   (5 code units, 5 UTF-8 bytes)
// actual:   "-a�-𠮷" (6 code units; � = U+FFFD replacement char)
```

The actual output's exact byte sequence is `2D 61 F0 2D F0 A0 AE B7`. Decoding as UTF-8: `-`, `a`, F0 (orphan high byte → replacement char), `-`, then a valid `𠮷`.

### Trace

`js_regex_exec` at [js_runtime.cpp:15837](../../lambda/js/js_runtime.cpp) sets `code_unit_indices = input_s && !input_s->is_ascii` (line 15856) and then at line 15962:

```cpp
int match_index = (int)(matches[0].data() - match_chars);
if (code_unit_indices) match_index = (int)js_utf16_index_from_byte(match_chars, match_len, match_index);
```

So for any non-ASCII input, `result.index` is a **UTF-16 code-unit count**, not a byte offset. RE2 still matches on raw UTF-8 bytes underneath, but the index reported back to JS is converted.

Meanwhile `js_regexp_symbol_replace` at [js_runtime.cpp:16252](../../lambda/js/js_runtime.cpp) was deciding the unit basis purely from the regex compile flags:

```cpp
bool utf16_replace = fast_rd && fast_rd->needs_utf16_subject;
```

`needs_utf16_subject` is only set when the *pattern* contains a surrogate escape or a dot-atom (line 14358). For a literal pattern like `/𠮷/g`, it's false even though the *input* has surrogate code units. The replace loop at line 16462 then walks `position` as a byte offset:

```cpp
strbuf_append_str_n(buf, S->chars + next_source_pos, position - next_source_pos);
```

For our trace: matches reported at code-unit positions 0, 3, 6. Bytes consumed per match: 4 (`matched_len`). The byte loop adds bytes 4 to position-3 then position-6, splitting the second and third 𠮷 mid-UTF-8 sequence — hence the orphan F0.

### Fix

```cpp
bool utf16_replace = (fast_rd && fast_rd->needs_utf16_subject) || (S && !S->is_ascii);
```

When `S` is non-ASCII, force `utf16_replace = true` so the replace loop:

- computes `source_units` via `js_utf16_len(S->chars, S->len, S->is_ascii)`;
- substitutes via `js_str_substring_utf16(str, next_source_pos, position)` for prefixes;
- advances `next_source_pos` by `matched_units` (code units) rather than `matched_len` (bytes).

This puts every count in the same basis as `result.index`. One line edited; the rest of the replace machinery already handles the utf16 branch correctly.

### Why this is a Gate F win and not a v-flag tweak

The proposal §3 Gate F item 2 named this test as "the replace edge case where `/v` flag interacts with the replacement function's argument coercion." That diagnosis was inverted by the failure: the very first assertion (using `/g`, no `/v`) was already failing, so v-flag interaction was never reached. The bug is older than v-flag — it would have surfaced on any non-ASCII replace any time `is_ascii` is false. v-flag tests just happen to exercise the path on supplementary-plane characters.

Acceptance check post-fix:

- Probe (the test itself): PASS
- Pre-flight: 55/55 `built-ins/String/prototype/replace/*` PASS
- Baseline guard: 40187/40187, 0 regressions, 134.8 s (well inside the +5 % ceiling)

### Counter-cluster check

Other call sites that may compare `position`/`index` with byte offsets after a `js_regex_exec`:
- `js_regexp_symbol_match` ([js_runtime.cpp:16157](../../lambda/js/js_runtime.cpp)) — feeds `position` only into building the array result; doesn't mix with byte slicing.
- `js_regexp_symbol_split` — same: uses code-unit indices throughout when constructing splits.
- `js_string_replace_impl` ([js_runtime.cpp:19416](../../lambda/js/js_runtime.cpp)) — non-regex string replace; not affected.

No further unit-basis fixes from this diagnosis. If a similar regression appears, the call-site pattern to look for is "uses `S->chars + position` after a `result.index` from `js_regex_exec`."

## 12.9 P11 walkthrough (2026-06-13)

P11 was scoped as "TA species + misc, ~13 tests" but the actual remaining Gate G surface after P0–P10 is much smaller. Two distinct fixes, four tests, ~50 LOC.

### P11(a) — ArrayBuffer constructor argument ordering

Test: `built-ins/ArrayBuffer/options-maxbytelength-compared-before-object-creation.js`

Failure: "Expected a RangeError but got a Test262Error"

Spec §25.1.5.1 step 4 (AllocateArrayBuffer) says `byteLength > maxByteLength` validation happens *before* OrdinaryCreateFromConstructor (which reads `NewTarget.prototype`). The test exploits this by passing a `newTarget` whose `prototype` getter throws `Test262Error`:

```js
let newTarget = Object.defineProperty(function(){}.bind(null), "prototype", {
  get() { throw new Test262Error(); }
});
assert.throws(RangeError, () => {
  Reflect.construct(ArrayBuffer, [10, {maxByteLength: 0}], newTarget);
});
```

Lambda's `js_reflect_construct` had pre-checks for several builtins (DataView byteOffset, SharedArrayBuffer byteLength/maxByteLength, Promise executor) but not for ArrayBuffer. So control reached the construct path, which read `NewTarget.prototype` first and surfaced the wrong error.

**Fix**: add an `ArrayBuffer` branch to the pre-check at `js_globals.cpp:js_reflect_construct`, mirroring the existing `SharedArrayBuffer` block at line ~6498. Validate `byteLength` and (if options present) `maxByteLength >= byteLength` early, throwing the spec-correct `RangeError` before any prototype access.

### P11(c) — TypedArray.prototype.with: validate against current length

Tests:
- `built-ins/TypedArray/prototype/with/index-validated-against-current-length.js`
- `built-ins/TypedArray/prototype/with/negative-index-resize-to-out-of-bounds.js`
- `built-ins/TypedArray/prototype/with/valid-typedarray-index-checked-after-coercions.js`

All three exercise a single spec step. ES2024 §22.2.3.34:

```
1. Let O be the this value.
2. ValidateTypedArray.
3. Let len be TypedArrayLength(taRecord).           ← cached length
4. Let relativeIndex be ToIntegerOrInfinity(index).
5–6. actualIndex from relativeIndex and len.
7–8. Coerce value (ToNumber or ToBigInt — may resize a backing rab).
9. If IsValidIntegerIndex(O, F(actualIndex)) is false, RangeError.   ← CURRENT length
10. Let A be TypedArrayCreateSameType(O, « F(len) »).                ← cached length
```

Lambda's `with` handler at `js_runtime.cpp:18345` was reusing the cached `len` from step 3 for the step-9 validity check. When `ta.with(4, evilValue)` resizes the buffer from 2 → 5 elements during coercion, the actualIndex 4 *should* now be valid (4 < 5), but Lambda saw `4 >= cached_len (2)` and threw `RangeError`. Conversely, a negative index that came back in-range against the cached length but went out-of-range against a shrunken buffer was silently accepted.

**Fix**: re-fetch `current_len = js_typed_array_length(obj)` after the coercion's side effect, and check `actual_index` against `current_len`. Result array still uses cached `len` (per step 10). 9-line code reordering, no semantic risk to the result-construction path.

### Counter-cluster check

Similar "cached length used after coercion" patterns to audit:
- `at`, `slice`, `subarray`, `set`, `copyWithin`, `fill` — looked at; either re-fetch length at the right place or don't have a coercion step that resizes. None matched the bug pattern.
- The general Gate D length-rederivation cluster (per proposal P2/P3) is the broader form of this bug surface; D-cluster will need an audit of every TA method's spec ordering once the P4 closure-capture issue is unblocked.

### Tests intentionally NOT fixed in P11

Two failing items were probed and deferred:

1. `ArrayBuffer/prototype/resize/coerced-new-length-detach.js` — passes as a single-script `./lambda.exe js` (called=true, TypeError thrown) but fails in batch mode with "Expected true but got false". Same family as the P4 closure-capture batch-mode divergence. Punt to that diagnostic.
2. `TypedArray/prototype/set/this-backed-by-resizable-buffer.js` and friends — heavy `for (let ctor of ctors)` loops with subclass species. Definitely P4-family.

### Acceptance

- 23/23 pre-flight (all TypedArray.prototype.with + the ArrayBuffer test) PASS
- Baseline guard: 40187/40187, 0 regressions, 145.1 s
- Full run: 40204/40261, 17 improvements vs. original baseline (was 13 post-P10), 0 regressions
- Diff vs. pre-P11 failures: exactly 4 newly passing (the 4 above), 0 newly failing
- Net contribution this phase: **+4 tests**, 0 LOC of risky changes

### Cumulative cleared, end of P11

| Phase | Tests cleared | Mechanism |
|---|---:|---|
| P1 (D1) | 12 | Clear `js_pending_new_target` leak in subclass-TA branch |
| P10(a) | 1 | Skip-list rgi-emoji-17.0 (Unicode 17 dependency) |
| P10(b) | 1 | UTF-16 unit basis in regex Symbol.replace |
| P11(a) | 1 | ArrayBuffer constructor arg ordering in Reflect.construct |
| P11(c) | 3 | TypedArray.prototype.with current-length validation |
| **Total** | **18** | (out of original 73 admittable failures) |

### P11(d) — additional pending-new-target leak (preventive, no test surfaced)

While auditing the remaining failures, found a second `js_pending_new_target` leak in `js_new_from_class_object` at [js_runtime.cpp:2162-2175](../../lambda/js/js_runtime.cpp) — the user-class-extending-non-TA-builtin branch. Same pattern as P1's bug (set, call, no clear), one branch over from the P1 fix:

```cpp
js_pending_new_target = effective_new_target;
js_has_pending_new_target = true;
Item result = js_call_function(ctor, obj, args, argc);
// 4 lines later: return result  ← no clear
// or fall through: return obj   ← no clear
```

Doesn't affect Promise/RegExp/Error/etc. (those builtins clear on entry), so the failing E1 NamedEvaluation tests aren't fixed by this. But it closes a latent leak that could surface as a P1-class bug for `class MyPromise extends Promise {}` style code in the wild. Filed under P11(d). Baseline guard clean post-fix.

### Why E1 NamedEvaluation isn't fixed yet

Investigation confirmed the proposal's stated root cause hypothesis ("skip NamedEvaluation when RHS is AwaitExpression") is **not** the actual bug surface. The existing variable-declaration code at `js_mir_statement_lowering.cpp:373/429/504/646` correctly skips NamedEvaluation for AwaitExpression — that path already only fires for FUNCTION_EXPRESSION / ARROW_FUNCTION literals.

The runtime trace of a failing test shows many `js_property_set(fn, "name", "")` calls (setting an empty string) before the strict-mode "Cannot assign to read only property 'name'" throw fires. The source of these empty-string `name` writes hasn't been located — neither the explicit `js_set_function_name` paths nor the obvious initialization paths match. Likely a Map-shape-sharing artifact across function expressions in the second export, but tracing requires further runtime instrumentation that wasn't completed in this session.

## 12.10 P4 closure-capture diagnostic (2026-06-13)

Per user direction, dug into the for-of let-binding closure-capture bug. The bug is real and **reproduces in single-script mode**, not only in batch mode (correcting an earlier note in §12.6). Both single-script `./lambda.exe js` and batch dispatch show the same staleness.

### Trace pattern

```js
const ctors = [Uint8Array, Int8Array, /* … */, MyBigInt64Array];   // ends with the BigInt subclass

for (let ctor of ctors) { /* first loop, creates closures over `ctor` */ }
for (let ctor of ctors) {
  const evil = { valueOf: () => { /* …reads ctor… */ } };
  // outer scope: ctor.name === Uint8Array (iter 1) — correct
  // inside evil.valueOf when invoked by ToNumber: ctor.name === MyBigInt64Array — wrong
}
```

Across every iteration of the second loop, the outer scope sees the **current** `ctor`, but a closure captured during that iteration reads the **first loop's last iteration value** (`MyBigInt64Array`).

### What fails

Attempted fix: extend the for-of writeback at `jm_transpile_for_of` (`js_mir_statement_lowering.cpp` lines ~3889 and ~4127) to call `jm_scope_env_mark_and_writeback` for let/const loops too. Was previously gated on `!is_let_const_loop`. The hypothesis was that scope_env wasn't being updated per iteration for let bindings, so closures captured a stale slot.

After rebuilding with the change, instrumented `jm_scope_env_mark_and_writeback`:

```text
[WB] no scope_env_reg for '_js_ctor'
[WB] no scope_env_reg for '_js_ctor'
```

The writeback is **reached but no-ops** because `mt->scope_env_reg == 0` at module top level (the for-of's enclosing function is module main, which doesn't allocate its own scope_env — closures inside use the module's locals directly via per-variable registers, not through scope_env). The fix was therefore ineffective and the test still fails identically.

### What this implies

The bug surface is NOT in the for-of writeback path. The closure body's read of `ctor` resolves through one of:
- the closure's own env (populated at `js_alloc_env` time), or
- a direct register reference into the module's local frame.

Either way, the JIT is somehow resolving `ctor` in the second for-of's closure body to a stale value — possibly because:
1. The two for-of bodies' closures share a `JsFuncCollected` entry (the function literal `() => ctor` is identical AST-wise), and the captured `ctor` slot is computed once at analysis time and points at the first for-of's loop_var register.
2. Or the analysis pass assigns capture indices based on enclosing-function lexical position, and both for-of bodies map `ctor` to the same closure capture index but the runtime env slot points to whichever loop_var register the analysis visited first.

Verifying which requires:
- A focused dump of `fc->captures[]` for both closures and the corresponding `var->reg` registers at analysis time, OR
- A focused dump of the emitted MIR for the closure's body, to see exactly which memory location its `ctor` read targets.

That instrumentation wasn't completed in this session. The fix surface is likely in `lambda/js/js_mir_analysis.cpp` (closure capture analysis) or `lambda/js/js_mir_function_class_lowering.cpp` (closure env layout) — not in `jm_transpile_for_of`.

### Reverted change

The non-working writeback edit at `jm_transpile_for_of` was reverted before rebuilding. Baseline guard re-confirmed clean post-revert: P1, P10, P11 fixes all intact, 0 regressions.

### Recommendation

P4 closure-capture is a real bug affecting ~25 Gate D failures plus likely several Gate G items, but the fix surface is deeper than a single edit. It requires either:
- Per-loop fresh `JsFuncCollected` entries for closures inside `for (let x …)` bodies (so each loop's closure has its own capture mapping), or
- A scope_env allocation at module top level so the writeback path can persist iteration values.

Either approach is a multi-hour JIT refactor with broad blast radius. Not safe to attempt without a focused isolation pass and a test-suite-wide baseline guard at each intermediate step.

## 12.11 P12 isolated wins (2026-06-13)

After P4 deferral, walked the remaining 55-failure list looking for items that don't depend on the for-of closure-capture cluster. Two cleanly-isolated wins identified and fixed.

### P12(a) — `lastIndexOf` raw fast path reverse-iteration clamp (1 test)

Test: `built-ins/TypedArray/prototype/lastIndexOf/negative-index-and-resize-to-smaller.js`

Failing assertion (simplified):

```js
const rab = new ArrayBuffer(0, {maxByteLength: 32});  // BPE=8 (Float64Array)
const ta = new Float64Array(rab);
rab.resize(32);                       // ta.length === 4
ta.fill(123);
const indexValue = {
  valueOf() { rab.resize(24); return -1; }   // shrinks to length 3
};
// Spec: len captured = 4 (pre-coercion), actualLast = 4 + -1 = 3,
// iterate k = 3, 2, 1, 0 using HasProperty (current length = 3):
//   k=3 → HasProperty false (3 ≥ 3) → skip
//   k=2 → HasProperty true → ta[2] === 123 → return 2
ta.lastIndexOf(123, indexValue);      // expected: 2,  Lambda returned: -1
```

The bug was in `js_typed_array_raw_index_of` ([js_typed_array.cpp:488](../../lambda/js/js_typed_array.cpp)):

```cpp
int current_len = js_typed_array_current_length(ta);    // 3 (post-resize)
int len = bound < current_len ? bound : current_len;     // bound=4 → len=3
if (from < 0 || from >= len) return -1;                  // from=3, len=3 → bail
```

The caller in `js_map_method`'s `lastIndexOf` handler ([js_runtime.cpp:17630](../../lambda/js/js_runtime.cpp)) treats this `-1` as "not found, definitive" and skips the spec-correct fallback loop. So the search short-circuits before the in-range `k=2` element gets checked.

**Fix**: split the `from >= len` case by direction.
- Forward iteration (`indexOf`): `from >= len` means no valid indices remain — return `-1` is correct.
- Reverse iteration (`lastIndexOf`): clamp `from = len - 1` and continue. Indices in `[len, original_from]` are skipped per spec HasProperty=false; the search then proceeds over the in-range `[0, len-1]`. This is safe because those skipped indices would have been HasProperty=false in the spec anyway.

```cpp
if (from < 0) return -1;
if (from >= len) {
    if (!reverse) return -1;
    from = len - 1;
}
```

10-line edit. All 395 `indexOf` / `lastIndexOf` / `includes` tests pre-flight cleanly; only failures remaining are the 3 pre-existing P4-family items (`coerced-searchelement-fromindex-{resize,grow,shrink}`).

### P12(b) — `Object.freeze` on resizable-buffer-backed TypedArray throws TypeError (1 test)

Test: `built-ins/Object/freeze/typedarray-backed-by-resizable-buffer.js`

Per ES2024 §10.4.5.16 `IntegerIndexedDefineOwnProperty` and `SetIntegrityLevel("frozen")` at §7.3.16: freezing a TypedArray backed by a resizable ArrayBuffer must throw TypeError, because the integer-indexed properties can't be redefined as `{writable: false, configurable: false}` (the buffer can resize behind them, invalidating the frozen state). Applies regardless of current length (even zero-length TAs throw, since the buffer could grow).

Lambda's `js_object_freeze` had no such check — it called `js_object_prevent_extensions` then walked all keys and tried to define them as non-writable / non-configurable. For empty TAs the walk had nothing to do and silently "succeeded"; for non-empty TAs the per-key define may or may not have failed downstream. The test expected the spec-mandated upfront TypeError.

**Fix**: in `js_object_freeze` ([js_globals.cpp:11045](../../lambda/js/js_globals.cpp)), add an early check before `js_object_prevent_extensions`:

```cpp
if (js_is_typed_array(obj)) {
    JsTypedArray* ta = js_get_typed_array_ptr(obj.map);
    if (ta && ta->buffer && ta->buffer->resizable) {
        js_throw_type_error("Cannot freeze a TypedArray backed by a resizable ArrayBuffer");
        return obj;
    }
}
```

15-line edit (with comment). All 112 `Object/freeze` + `Object/isFrozen` tests pre-flight cleanly.

### P12 acceptance

- Probes: both newly-fixed tests PASS
- Pre-flight: 395 indexOf/includes + 112 freeze/isFrozen = 507 tests, 0 regressions (only the pre-existing 3 P4-family failures remain)
- Baseline guard: 40187/40187 clean, 0 regressions, 137.7 s (well inside the +5% ceiling)
- Net contribution this phase: **+2 tests**

### Cumulative cleared, end of P12

| Phase | Tests cleared | Mechanism |
|---|---:|---|
| P1 (D1) | 12 | `js_pending_new_target` clear in subclass-TA branch |
| P10(a) | 1 | skip-list rgi-emoji-17.0 |
| P10(b) | 1 | UTF-16 unit basis in regex Symbol.replace |
| P11(a) | 1 | ArrayBuffer constructor arg ordering in Reflect.construct |
| P11(c) | 3 | TypedArray.prototype.with current-length |
| P11(d) | 0 (preventive) | second pending-new-target leak (user-class-extends-non-TA) |
| P12(a) | 1 | lastIndexOf raw fast path reverse-iteration clamp |
| P12(b) | 1 | Object.freeze TypedArray-resizable-buffer detection |
| **Total** | **20** | (of original 73 admittable failures) |

### Remaining 53 failures — what's NOT yet tractable

The remaining failure list is now structurally narrow. After harvesting the cleanly-isolated wins (P1 + P10 + P11 + P12), the residual 53 failures concentrate in:

1. **For-of let closure-capture cluster** (~28 tests): all Gate D items with `for (let ctor of ctors)` patterns and inner closures referencing `ctor`. Documented in §12.10. Single-edit fixes proven ineffective.

2. **TLA module-mode plumbing** (Gate E, 20 tests): E1 NamedEvaluation (7) + E2 capability/$DONE (4) + E3 Promise.withResolvers (3) + E4 thenable await (4) + E5 SIGSEGV (2).

3. **Wider engine refactors** (~5 tests): Array.prototype.toLocaleString mis-registered to OBJ_TO_LOCALE_STRING; ArrayBuffer.prototype.resize coerced-new-length-detach batch-mode divergence; a few species-ctor tests.

None of these have a single-edit fix in the proposal's scope. The next iteration would need to either tackle P4 closure-capture (multi-hour JIT refactor) or carve out smaller wins from the TLA cluster (each requires its own batch-mode-vs-script reproduction step).

## 12.12 P13 closure-capture deep dive (2026-06-13)

After §12.11 deferred P4, took another attempt at the for-of let-binding closure-capture cluster. The investigation produced a precise mechanism description but no working fix — reverted clean.

### Smoking gun

A minimal reduction (no harness asserts, no comparisons):

```js
// Loop 1 — body contains a closure capturing `ctor`
for (let ctor of ctors) {
  const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT, 8 * ctor.BYTES_PER_ELEMENT);
  const lengthTracking = new ctor(rab);
  for (let i = 0; i < 4; ++i) lengthTracking[i] = MayNeedBigInt(lengthTracking, 1);
  let evil = { valueOf: () => { rab.resize(6 * ctor.BYTES_PER_ELEMENT); return 0; } };
  Array.prototype.indexOf.call(lengthTracking, MayNeedBigInt(lengthTracking, 0));
  Array.prototype.indexOf.call(lengthTracking, MayNeedBigInt(lengthTracking, 0), evil);
}

// Loop 2 — body also contains a closure capturing `ctor`. Just prints.
let i = 0;
for (let ctor of ctors) {
  let evil = { valueOf: () => { return ctor.BYTES_PER_ELEMENT; } };
  print("loop2 iter " + (++i) + ": ctor=" + ctor.name);
  if (i > 3) break;
}
// Expected: Uint8Array, Int8Array, Uint16Array, Int16Array
// Actual:   MyBigInt64Array, MyBigInt64Array, MyBigInt64Array, MyBigInt64Array
```

`ctor` in loop 2's body reads loop 1's LAST iteration value, never updates. The same pattern WITHOUT the closure in loop 1's body works correctly. The same pattern WITHOUT the closure in loop 2's body also works correctly. Both closures must be present.

### Mechanism (precise)

After tracing through the JIT scope-env and capture analysis:

1. **Loop 1's body** contains `let evil = { valueOf: () => …ctor… }`. The arrow `() => …ctor…` is compiled. Its `JsFuncCollected` records a capture of `ctor` with a `scope_env_slot`.

2. **Closure analysis** propagates: the module-main function's `var` entry for `ctor` ends up with `in_scope_env = true` and some `scope_env_reg` set. (Mechanism is in `js_mir_function_class_lowering.cpp` around the closure-capture-marking lines 2785, 2829.)

3. **Loop 2's `let ctor`** binding inherits `in_scope_env = true` from the existing entry (via `jm_set_var` at `js_mir_hashmap_scope_utils.cpp:267`).

4. **Reading `ctor` in loop 2's body** emits the env-load at `js_mir_expression_lowering.cpp:978`:

   ```cpp
   if (var->in_scope_env && var->scope_env_reg != 0 && var->mir_type == MIR_T_I64) {
       jm_emit(... MIR_MOV var->reg, env[var->scope_env_slot * 8 + var->scope_env_reg] ...);
   }
   ```

   This **clobbers** `var->reg` (the loop_var register) with the env's stored value. Since the env was last written in loop 1's body (or its closure setup) with `MyBigInt64Array`, the loop's per-iteration MIR_MOV is silently overwritten on every read.

5. **The for-of's `MIR_MOV`** (line 3878 / 4122) does write the new iteration's value to `loop_var` register. But every subsequent read in the body refreshes the register from env, so the body never sees the iteration update.

### Why neither attempted fix worked

- **Attempt A**: extend the for-of writeback (`jm_scope_env_mark_and_writeback`) to fire for `let`/`const` loops too. At trace time, `mt->scope_env_reg == 0` (module main hasn't allocated its scope_env at the for-of's compile point), so the writeback no-ops at the early-return on line 1157.

- **Attempt B**: gate the env-load in `js_mir_expression_lowering.cpp:978` on `var->scope_env_reg == mt->scope_env_reg` so that a "wrong" env-register doesn't clobber the parent's register. Test still fails — at the read site, the var's recorded scope_env_reg apparently matches `mt->scope_env_reg`, so the guard doesn't shortcut.

### Where the actual fix lives

The structural mismatch:
- Scope-env setup for module-main (at function lowering time, `function_class_lowering.cpp:2792`) marks the var entries with `in_scope_env = true` and `scope_env_reg = mt->scope_env_reg`.
- The closure inside the for-of body sets up its OWN scope_env at expression lowering time, AND that allocation runs DURING module-main's body lowering, mutating `mt->scope_env_reg` mid-body. So the `mt->scope_env_reg` value seen at the for-of's writeback site is NOT the same as the value seen at the var-marking site or the closure-creation site.

Two structural fix shapes (both larger than a single edit):

1. **Save/restore `mt->scope_env_reg` around the closure expression compile** so that the module-main lowering sees a stable scope_env_reg throughout. Requires careful audit of every site that mutates `mt->scope_env_reg`.

2. **Don't mark `in_scope_env` on let-loop variables at all** — closures that capture a let-loop variable should snapshot the variable's value into their OWN env at closure-creation time, with no shared scope_env dependency on the parent. This is closer to the JavaScript spec's "fresh binding per iteration" semantics, but requires reworking the capture analysis to distinguish loop-bound `let` captures from outer-scope captures.

Neither fix is a single-edit landing. Both need:
- A targeted MIR dump of the body's `ctor` read instructions before/after the change
- A test-suite-wide baseline guard (every existing test that uses for-of let with inner closures could be affected)
- A regression-test addition for the bisected reduction in `temp/js55_p4_bisect.js`

### Outcome

P13 attempts reverted. Verified post-P12 fixes still PASS (lastIndexOf, Object.freeze). Baseline guard re-confirmed clean.

The structural fix is feasible but takes its own session. The mechanism description above (along with the bisected reduction in `temp/js55_p4_bisect.js`) gives the next person a precise starting point.

## 12.13 P13 + P14 + P16 isolated wins (2026-06-13, second pass)

After the §12.12 P13 closure-capture attempt was reverted, did another harvest pass over the remaining 53-failure list looking for clean wins that don't touch the closure-capture cluster. Found five new tests across three independent fix surfaces, none of which depend on the for-of let JIT bug.

### P13 — `Array.prototype.toLocaleString` mis-registration (2 tests)

Tests:
- `built-ins/Array/prototype/toLocaleString/resizable-buffer.js`
- `built-ins/Array/prototype/toLocaleString/user-provided-tolocalestring-shrink.js`

Failure: `Array.prototype.toLocaleString.call(typedArray)` throws "Cannot perform %TypedArray%.prototype.join on an out-of-bounds ArrayBuffer".

Root cause: `Array.prototype.toLocaleString` was registered in `JS_ARRAY_PROTOTYPE_METHOD_SPECS` ([js_runtime_builtin_registry.cpp:192](../../lambda/js/js_runtime_builtin_registry.cpp)) with `builtin_id = JS_BUILTIN_OBJ_TO_LOCALE_STRING`. That dispatch handler ([js_runtime.cpp:8363](../../lambda/js/js_runtime.cpp)) is `Object.prototype.toLocaleString`, which just does `this.toString()`. For a TypedArray, `toString()` routes to TA's join, which throws OOB on a shrunken-buffer-backed TA.

The correct routing: `Array.prototype.toLocaleString` should iterate indexed properties and call each element's `toLocaleString`, matching the spec at §23.1.3.31. For a TA receiver, the TA-aware path at [js_runtime.cpp:18021](../../lambda/js/js_runtime.cpp) already implements the right behavior (and respects `js_dispatch_as_array_method` to skip the OOB throw).

**Fix** (4 edits, ~10 LOC total):
1. Added enum value `JS_BUILTIN_ARR_TO_LOCALE_STRING` to [js_runtime_internal.hpp:160](../../lambda/js/js_runtime_internal.hpp) right after `JS_BUILTIN_ARR_WITH`.
2. Updated registry at [js_runtime_builtin_registry.cpp:192](../../lambda/js/js_runtime_builtin_registry.cpp) to use the new builtin id.
3. Added the new id to the case list and `arr_method_names` map at [js_runtime.cpp:8505](../../lambda/js/js_runtime.cpp) so dispatch routes through the existing Array-method shim (which already handles `LMD_TYPE_ARRAY` → `js_array_method` and `LMD_TYPE_MAP` TA → `js_map_method`).

The dispatch flag protocol takes over: when `Array.prototype.toLocaleString.call(taOOB)` runs, `js_dispatch_as_array_method = true` (because the Array.prototype function doesn't have `TYPED_ARRAY_METHOD` flag), the TA path at line 18021 skips the OOB throw, and `js_typed_array_length(obj)` returns the captured length (0 for OOB). Result: empty string, matching spec.

For the user-provided shrink test, `len` is captured at entry (= 4), the loop iterates 4 times, the user's `Number.prototype.toLocaleString` callback fires and shrinks the buffer mid-iteration. For OOB indices `js_typed_array_get` returns undefined, which the loop's `if (elem_type == NULL || elem.item == ITEM_JS_UNDEFINED) continue;` skips. Result: `"0,0,,"` (two non-OOB elements + two skipped OOB elements yielding empty between separators), matching spec.

Pre-flight: all 74 `toLocaleString` tests pass. Baseline guard clean.

### P14 — `TypedArray.prototype.set` targetLength re-capture (2 tests)

Tests:
- `built-ins/TypedArray/prototype/set/target-grow-source-length-getter.js`
- `built-ins/TypedArray/prototype/set/target-shrink-source-length-getter.js`

Failure (grow): no exception thrown when RangeError expected. Failure (shrink): TypeError thrown when no throw expected.

Root cause: `js_typed_array_set_from` at [js_typed_array.cpp:2323](../../lambda/js/js_typed_array.cpp) was re-checking OOB AND re-capturing `target_len` AFTER the source's length getter was invoked. Per ES2024 §22.2.3.26.1 SetTypedArrayFromArrayLike, the spec captures `targetLength` at step 4 (before any source-property access) and uses that captured value for the range check at step 10. The OOB check at step 3 also happens before the length getter. After the getter (step 8) the write loop relies on `IsValidIntegerIndex` to silently no-op OOB writes, never an upfront throw.

Lambda's path was:
```cpp
int target_len = js_typed_array_current_length(dst);        // step 4: correct capture
// ... source.length getter fires (step 7-8), may resize buffer
if (!js_dispatch_as_array_method && js_typed_array_is_out_of_bounds(dst)) {
    return js_throw_type_error("...OOB");                   // BUG: re-checking after getter
}
target_len = js_typed_array_current_length(dst);             // BUG: stomps spec-captured value
if ((int64_t)offset + src_len > (int64_t)target_len) {       // step 10: uses wrong target_len
    return js_throw_range_error("source is too large");
}
```

**Fix**: removed the re-OOB check and the `target_len` re-capture ([js_typed_array.cpp:2402](../../lambda/js/js_typed_array.cpp)). The write loop already uses `js_typed_array_set` which gates per-index writes through `js_typed_array_current_length` ([js_typed_array.cpp:2139-2142](../../lambda/js/js_typed_array.cpp)) — OOB-indexed writes return silently.

Grow case: target_len = 4 captured, getter grows buffer to 6, srcLength = 6, range check 0 + 6 > 4 → RangeError, matches spec. ✓
Shrink case: target_len = 4 captured, getter shrinks to 3 (TA OOB), srcLength = 1, range check 0 + 1 > 4 false → continue to write loop, writes silently no-op on OOB indices, no throw — matches spec. ✓

Pre-flight: 101 of 102 TA.set tests pass (1 failure is `this-backed-by-resizable-buffer.js`, a known closure-capture cluster victim). 2 SAB tests crash on signal 6 — pre-existing unrelated issue.

### P15 — `ArrayBuffer.prototype.resize/coerced-new-length-detach` (deferred)

Test: `built-ins/ArrayBuffer/prototype/resize/coerced-new-length-detach.js`

Looks like a one-site spec ordering bug (one detach check, AFTER coercion), but the test's `let called = false; assert.throws(...); assert(called);` pattern hits a **block-scoped `let` closure-capture bug**: assignments to `let called` from inside an arrow body created within the same block don't propagate to the outer scope.

Reduction (no for-of involved):
```js
{
  let called = false;
  try {
    (function() { called = true; throw new Error("test"); })();
  } catch (e) {}
  print("called=", called);
}
// Actual: "called= false"   (BUG)
// Expected: "called= true"
```

Top-level `let` works correctly. Function-body `let` also works. Only `let` declared **inside a `{...}` block** with a closure that mutates it from another scope fails to propagate the write. This is essentially the same bug-family as §12.10's for-of `let` capture, just exposed via block scoping.

Same structural fix space as P13(closure)/P4 — deferred. The proposal's stated "one-line ArrayBuffer.resize spec fix" would have no effect on this failure shape.

### P16 — for-in / Object.keys on TypedArrays (1 test)

Test: `language/statements/for-in/resizable-buffer.js`

Failure: `for (const key in ta)` over a TypedArray with length 3 yields no keys (expected `"012"`).

Root cause: `js_for_in_keys` ([js_globals.cpp:9117](../../lambda/js/js_globals.cpp)) walks the prototype chain's `ShapeEntry`s but never enumerates the TypedArray's integer-indexed properties — those live in the buffer, not as shape entries. `js_object_keys` ([js_globals.cpp:8797](../../lambda/js/js_globals.cpp)) had the same gap. Per ES2024 §10.4.5, integer-indexed properties on a TypedArray are own enumerable properties.

**Fix** (3 edits, ~50 LOC):
1. Added a TA detection branch at the top of `js_object_keys` (right after the LMD_TYPE_ARRAY branch). Enumerates `[0, length-1]` as string keys, then walks shape entries for any custom non-index enumerable properties.
2. Inside `js_for_in_keys`'s LMD_TYPE_MAP path, seeded the `idx_vals/idx_items` arrays with integer indices before the existing shape-entry-and-prototype walk. Added each integer index to the `seen` set so subsequent shape-walks don't re-emit.
3. Extended `js_is_engine_internal_enumeration_key` to suppress the internal `__ta__` and `__ab__` markers (used to store the upgraded JsTypedArray*/JsArrayBuffer* pointers when the receiver gets non-internal properties). Without this, `for (key in ta)` was leaking `__ta__` after the index keys.

The dynamic alloca sizing in the for-in path handles TAs longer than the default 16-slot capacity (most tests use small TAs; sizing-up is rare but safe).

Pre-flight: 174 / 174 across `for-in`, `Object.keys`, `Object.getOwnPropertyNames`, and TA-related tests. Baseline guard clean.

### P13 + P14 + P16 acceptance

- Probes: 5 / 5 newly fixed tests PASS
- Pre-flight (toLocaleString + set + for-in + keys): 350 / 350, 0 regressions
- Baseline guard: 40187 / 40187 clean, 0 regressions, 140.8 s (well inside the +5 % ceiling vs the §12.11 baseline of 137.7 s)
- Full run: 40211 / 40261, **24 improvements** vs original baseline (was 20 post-P12), 0 regressions
- Net contribution this session: **+5 tests** (2 P13 + 2 P14 + 1 P16), 0 LOC of risky changes

### Cumulative cleared, end of P16

| Phase | Tests cleared | Mechanism |
|---|---:|---|
| P1 (D1) | 12 | `js_pending_new_target` clear in subclass-TA branch |
| P10(a) | 1 | skip-list rgi-emoji-17.0 |
| P10(b) | 1 | UTF-16 unit basis in regex Symbol.replace |
| P11(a) | 1 | ArrayBuffer constructor arg ordering in Reflect.construct |
| P11(c) | 3 | TypedArray.prototype.with current-length |
| P11(d) | 0 (preventive) | second pending-new-target leak (user-class-extends-non-TA) |
| P12(a) | 1 | lastIndexOf raw fast path reverse-iteration clamp |
| P12(b) | 1 | Object.freeze TypedArray-resizable-buffer detection |
| **P13** | **2** | **Array.prototype.toLocaleString builtin-id fix** |
| **P14** | **2** | **TypedArray.set targetLength capture, drop re-OOB check** |
| **P16** | **1** | **TA index enumeration in for-in + Object.keys** |
| **Total** | **25** | (of original 73 admittable failures) |

### Remaining 48 failures — structural buckets

1. **For-of let closure-capture cluster** (~24 tests): all the Array.prototype.{copyWithin, find*, findLast*, includes, indexOf, lastIndexOf, map, reverse, slice, sort, toSorted/toReversed/toSpliced} resizable-buffer entries with `for (let ctor of ctors)` patterns. Also pulls in 2 TypedArray.map speciesctor tests, 1 set this-backed test, 1 slice speciesctor, and 4 sort/reverse tests with `wholeArrayView`/`taFull` TDZ-cascade errors. Mechanism in §12.10/§12.12.

2. **TLA module-mode plumbing** (20 tests): E1 NamedEvaluation (7) + E2 capability/$DONE (4) + E3 Promise.withResolvers (3) + E4 thenable await (4) + E5 SIGSEGV (2). Per-phase fixes documented in §3.

3. **Block-scoped let closure-capture** (1 test): `ArrayBuffer/prototype/resize/coerced-new-length-detach.js`. Same family as the for-of let bug, exposed via `{...}` block instead of for-of (§P15 above).

4. **TypedArrayConstructors src-typedarray-resizable-buffer** (1 test): probably part of the closure-capture cluster; not investigated this pass.

5. ~~**2 already pre-existing SAB crashes**~~ — retracted. The two `set/*-diff-buffer-other-type-sab.js` tests were noted as crashing in §12.13 preflight, but follow-up investigation revealed those were transient worker-crash artifacts from co-located tests, NOT real SAB failures. Stress-testing (3× isolated runs, 3× set/ preflight runs, 1× full release guard) confirms both SAB tests pass reliably. Likely cleared as a side effect of P14's `js_typed_array_set_from` simplification — removing the buggy re-OOB check eliminated whatever edge case was destabilizing the worker. All 97 baseline-admitted SAB tests pass.

## 12.14 P19 closure-capture deep dive (2026-06-13, third pass)

After the §12.13 wins landed, returned to the for-of let closure-capture cluster (~24 tests) with new instrumentation. Investigation produced a precise mechanism description — beyond §12.10 and §12.12 — but no fix landed.

### Reproduction (simplified from `coerced-searchelement-fromindex-grow.js`)

```js
const ctors = [/* 14 TA constructors including MyBigInt64Array as last */];

for (let ctor of ctors) {
  const rab = CreateResizableArrayBuffer(...);
  const lengthTracking = new ctor(rab);
  // ... initialize ...
  let evil = { valueOf: () => { rab.resize(...); return 0; } };
  // calls that invoke evil.valueOf
}

// Second loop — same structure
for (let ctor of ctors) {
  const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT, 8 * ctor.BYTES_PER_ELEMENT);
  // ...
  let evil = { valueOf: () => { rab.resize(6 * ctor.BYTES_PER_ELEMENT); return -4; } };
  // evil.valueOf() reads ctor.BYTES_PER_ELEMENT — gets MyBigInt64Array.BPE (8) for every iter
}
```

The body's `print("ctor=" + ctor.name)` shows correct per-iteration values. But `evil.valueOf()` (when invoked) reads `ctor` as `MyBigInt64Array` (loop 1's last iteration) for every iteration of loop 2.

### Precise mechanism (verified via MIR dump + runtime instrumentation)

1. **Runtime closure env contents are stale.** Instrumented `js_new_closure` (runtime) shows:
   - Loop 1's 14 closures: each env[1] holds the iteration's distinct `ctor` (Uint8Array, Int8Array, ..., MyBigInt64Array).
   - Loop 2's 14 closures: each env[1] holds the SAME value across all iterations (a MAP, not a FUNC — and matching nothing in `ctors`).

2. **Per-closure env is allocated correctly.** `js_alloc_env` returns a fresh address per call. Each closure has its OWN env (different pointer). Not a shared-env issue.

3. **The bug is in what's STORED into env[1]**, not the env allocation. The MIR-emitted store `MOV env_582[1], reg315` runs once per iteration. `reg315` is loop 2's loop_var register. But somehow `reg315` holds the same stale value across all 14 iterations.

4. **MIR dump reveals the actual sequence at closure-creation site in loop 2's body:**
   ```text
   mov  _js_rab_537, i64:(js_alloc_env_444)        ← READ from LOOP 1's env[0]
   mov  _js_ctor_505, i64:8(js_alloc_env_444)      ← READ from LOOP 1's env[1]
   call js_alloc_env, js_alloc_env_582, 2          ← allocate LOOP 2's env
   mov  i64:(js_alloc_env_582), _js_rab_537        ← STORE stale rab to loop 2's env
   mov  i64:8(js_alloc_env_582), _js_ctor_505      ← STORE stale ctor to loop 2's env
   call js_new_closure, ..., js_alloc_env_582, 2
   ```
   `js_alloc_env_444` is **loop 1's** closure env register. After loop 1 ends, that MIR register still holds its last allocated env address (containing MyBigInt64Array et al). Loop 2's body is reading rab/ctor THROUGH this stale pointer.

5. **The writeback into env_444 at `const rab = ...`** is ALSO wrong:
   ```text
   mov  _js_rab_537, dcall_541                     ← rab init result
   mov  i64:(js_alloc_env_444), _js_rab_537        ← writeback to LOOP 1's env[0]
   ```
   So loop 2 is OVERWRITING loop 1's last env's rab slot with loop 2's value. (Doesn't matter for correctness since loop 1's closures already captured their values; but signals broken tracking.)

### Why this happens

Two distinct mechanisms collude:

**A) `jm_write_last_closure_capture_if_matching`** (statement_lowering.cpp:28) fires when a `let`/`const` initializer matches a NAME in the most-recently-created closure's capture list. The function writes the new value to `mt->last_closure_env_reg`. After loop 1's last `let evil = ...` creates its closure, `mt->last_closure_env_reg = js_alloc_env_444`. This state **is not reset between for-of loops**. So when loop 2's `const rab = ...` runs, it matches `_js_rab` against loop 1's evil's captures (also named _js_rab) and writes to env_444.

**B) The READS at lines 1316-1317** are NOT from `jm_scope_env_reload_vars` (would emit `boxed = env[slot]; var->reg = boxed` — TWO mov instructions). They're from a single `mov var->reg, env[slot]` pattern. That pattern only appears at `expression_lowering.cpp:978` (the env-load in identifier resolution). For this to fire, `var->in_scope_env` must be true AND `var->scope_env_reg != 0`. But P18-FE-STORE log shows loop 2's rab/ctor have `from_env=0`, `env_reg=0`, `in_scope_env=0` at MIR emission time.

So either (i) the marking happens AFTER P18-FE-STORE fires (during subsequent statement processing inside the body), or (ii) the reads are emitted by a code path I haven't located.

### Two attempted fixes (BOTH ineffective)

- **§12.10 Attempt A**: extend for-of writeback to fire for let/const loops. No effect because `mt->scope_env_reg == 0` in js_main at for-of compile time. The writeback function early-returns.

- **§12.12 Attempt B**: gate the env-load at expression_lowering.cpp:978 on `var->scope_env_reg == mt->scope_env_reg`. No effect — at the read site, the var's recorded scope_env_reg happens to match mt->scope_env_reg, so the guard doesn't kick in.

### Where the actual fix needs to land

One of:

1. **Reset `mt->last_closure_env_reg = 0` at every for-of/for-in loop boundary** (or at every scope-pop). This would prevent the writeback at line 1283 from going to a stale env. But it might break other cases that rely on cross-block last-closure tracking.

2. **`jm_write_last_closure_capture_if_matching` must verify the captured-name var entry's reg matches** (the val_reg argument). If the val_reg is for a DIFFERENT var than what the last closure captured (i.e. a re-declared let in a sibling scope), skip the writeback.

3. **Properly clear the from_env / in_scope_env / scope_env_reg flags when popping a closure's body scope** — and ensure those flags don't leak via jm_set_var inheritance from outer-scope same-named entries.

4. **Each for-of let iteration should allocate a fresh scope_env** matching ES spec's "fresh binding per iteration" — this is the spec-aligned fix but requires significant rework.

### Outcome

P19 attempts reverted. Mechanism described above is precise but the fix surface requires:
- A targeted MIR dump diff between working (loop 1) and broken (loop 2) closure creations
- Verification of which exact code path emits the `mov var->reg, env[slot]` pattern when var entries appear to have `from_env=0`
- A test-suite-wide guard at each intermediate step (the for-of let pattern is ubiquitous)

The mechanism description is more precise than §12.10/§12.12 but the structural fix still takes its own session. The reproduction lives at `temp/js55_p18_two_loops.js` and the MIR dump at `temp/js_mir_dump.txt`.

None of these have a single-edit fix in this proposal's scope. The next iteration would need to either tackle the closure-capture structural fix (multi-hour JIT refactor in `js_mir_function_class_lowering.cpp` or `js_mir_analysis.cpp`) or carve into the TLA cluster (each needs its own batch-mode-vs-script reproduction step).

## 12.15 P20 closure-capture cluster — actual fix (2026-06-14)

After §12.14 deferred the cluster, returned with the precise mechanism description and **landed a working fix** in one session. Four distinct edits, ~18 tests cleared from the closure-capture cluster (plus spillover).

### P20(a) — Reset `mt->last_closure_env_reg` at for-of/block boundaries (5 tests)

The mechanism (verified via MIR dump): `jm_write_last_closure_capture_if_matching` ([js_mir_statement_lowering.cpp:28](../../lambda/js/js_mir_statement_lowering.cpp)) writes a let/const initializer's value to `mt->last_closure_env_reg[slot]` whenever the variable's name matches a capture name in the most-recently-created closure. The state persists across statement boundaries.

When a SECOND `for (let X of …)` loop appears after a FIRST loop whose body created closures, the second loop's body initializers (`const rab = ...`) match the FIRST loop's last closure's captures by NAME — and write the values into the first loop's stale closure env. Subsequent reads of `_js_rab` / `_js_ctor` in the second loop go through `jm_env_reload_shared_captures` (called after every CALL expression) and pull from that stale env.

**Fix**: at `jm_transpile_for_of` entry ([js_mir_statement_lowering.cpp:3547](../../lambda/js/js_mir_statement_lowering.cpp)), save and zero out `mt->last_closure_has_env`, `last_closure_env_reg`, `last_closure_capture_count` (+ names/slots/is_nfe arrays). Restore on exit. Same pattern in `case JS_AST_NODE_BLOCK_STATEMENT:` ([js_mir_statement_lowering.cpp:5403](../../lambda/js/js_mir_statement_lowering.cpp)) for nested-block patterns.

Tests cleared: indexOf {grow,shrink}, includes resize, lastIndexOf shrink, slice grow.

### P20(b) — find/findIndex/findLast/findLastIndex OOB semantics (8 tests)

ES2024 §23.1.3.10-13: the four `find*` methods do NOT use HasProperty — they call `Get(O, k)` directly, which returns `undefined` for OOB indices. The callback is invoked with `undefined`.

Lambda's TA path at four sites ([js_runtime.cpp:17777, 17807, 17898, 17929](../../lambda/js/js_runtime.cpp)) was doing `if (... continue;` on OOB when dispatched as Array method — skipping the callback entirely. For `forEach`/`every`/`some`/`reduce`/`reduceRight`/`filter`/`map` the `continue` IS spec-correct (those DO use HasProperty); only the four `find*` methods needed the change.

**Fix**: at each site, replace the `continue` with:
```cpp
bool i_oob = js_dispatch_as_array_method && i >= js_typed_array_length(obj);
Item elem = i_oob ? make_js_undefined()
                  : js_typed_array_get(obj, (Item){.item = i2it(i)});
```

Tests cleared: 4 `find{,Index,Last,LastIndex}/callbackfn-resize-arraybuffer.js` + 4 `*/resizable-buffer-shrink-mid-iteration.js` (less the find/values which has a separate TDZ cascade, cleared by P20(c)).

### P20(c) — Block-scoped function-decl: refresh closure at textual position (5 tests)

`jm_init_block_tdz` ([js_mir_analysis.cpp:1416](../../lambda/js/js_mir_analysis.cpp)) hoists function declarations inside a block: creates `binding_reg`, initializes to undefined, then immediately creates the closure and assigns. This happens at block ENTRY — BEFORE any of the block's `const`/`let` initializers run.

If the function body captures one of those let/const bindings, the capture reads TDZ sentinels and the env slot stays TDZ for the life of the closure. When the function is called later (even after the const has been initialized), the captured value still reads TDZ — runtime throws `Cannot access 'X' before initialization`.

**Fix**: at the textual function-declaration statement handler ([js_mir_statement_lowering.cpp:4501](../../lambda/js/js_mir_statement_lowering.cpp)), when `existing` is the from_block_func_decl binding, REMEMBER it via `p19_block_func_existing` before clearing it. After the fresh closure is created at line 4547, also assign that closure to `p19_block_func_existing->reg`. The binding now sees a closure that captures the just-initialized const.

Existing semantics for Annex B (legacy var/global env mirroring) are preserved by keeping `existing = NULL` for the rest of the function.

Tests cleared: find shrink-mid-iteration (TDZ cascade), sort comparefn, reverse (Array + TA), sort default-comparator.

### P20(d) — Array.prototype.slice on TA delegates to Array semantics (1 test)

When `Array.prototype.slice.call(taOOB, evil)` is dispatched, ES spec ArraySpeciesCreate creates a regular Array (not a TA). The resulting Array should have HOLES (sparse slots) for OOB indices, observable as `undefined` per index.

Lambda's `js_map_method` slice handler ([js_runtime.cpp:17456](../../lambda/js/js_runtime.cpp)) called `js_typed_array_slice` which creates a TA copy (zeros for OOB, since TAs can't hold undefined).

**Fix**: when `js_dispatch_as_array_method` is true, short-circuit to `js_array_generic_slice` ([js_runtime.cpp:22263](../../lambda/js/js_runtime.cpp)). The TA-specific OOB throw also reorganized to fire only when NOT dispatching as Array method.

Tests cleared: slice/coerced-start-end-shrink.

### Cumulative cleared, end of P20

| Phase | Tests cleared | Mechanism |
|---|---:|---|
| P1 (D1) | 12 | `js_pending_new_target` clear in subclass-TA branch |
| P10(a) | 1 | skip-list rgi-emoji-17.0 |
| P10(b) | 1 | UTF-16 unit basis in regex Symbol.replace |
| P11(a) | 1 | ArrayBuffer constructor arg ordering in Reflect.construct |
| P11(c) | 3 | TypedArray.prototype.with current-length |
| P12(a) | 1 | lastIndexOf raw fast path reverse-iteration clamp |
| P12(b) | 1 | Object.freeze TypedArray-resizable-buffer detection |
| P13 | 2 | Array.prototype.toLocaleString builtin-id fix |
| P14 | 2 | TypedArray.set targetLength capture, drop re-OOB check |
| P16 | 1 | TA index enumeration in for-in + Object.keys |
| **P20(a)** | **5** | **Reset `mt->last_closure_env_reg` at for-of/block boundaries** |
| **P20(b)** | **8** | **find\* spec: yield undefined for OOB instead of skipping** |
| **P20(c)** | **5** | **Block function-decl refreshes closure at textual position** |
| **P20(d)** | **1** | **Array.slice on TA uses ArraySpeciesCreate path** |
| **Total** | **43** | (of original 73 admittable failures) |

Final full run: **40230 passing, 43 improvements vs baseline, 0 regressions** in 164.4s (within +5% of baseline).

### Remaining ~9 failures after P20

Now structurally narrow. The remaining cluster failures are all spec-correctness issues, NOT closure-capture:

1. **`Array.prototype.copyWithin/resizable-buffer.js`** — uses `(...rest) => Array.prototype.copyWithin.call(ta, ...rest)`. Spread dispatch doesn't properly forward to the generic copyWithin path. Test passes WITHOUT spread (`f.call(ta, 0, 2)` works; `f.call(ta, ...rest)` doesn't). Suggests a spread+TA-dispatch interaction in the method-call routing.

2. **`Array.prototype.map/callbackfn-resize-arraybuffer.js`** — Lambda's map on TA returns a TA which can't hold undefined. Needs the same `js_dispatch_as_array_method` shortcut as P20(d), but at the map site.

3. **`Array.prototype.sort/resizable-buffer-default-comparator.js`** — `[4,6,8,10]` vs `[10,4,6,8]`. Array's default comparator does STRING compare, TA's does NUMERIC. When dispatched as Array method, should use string compare.

4. **`TypedArrayConstructors/.../src-typedarray-resizable-buffer.js`** — TypeError missing for TA constructor edge case.

5-6. **`TypedArray.prototype.map/speciesctor-resizable-buffer-{grow,shrink}.js`** — species ctor wiring with mid-construction resize.

7. **`TypedArray.prototype.set/this-backed-by-resizable-buffer.js`** — TypeError vs Error: the test uses `throwingProxy` whose getter throws; should fail OOB check BEFORE accessing the proxy.

8. **`TypedArray.prototype.slice/speciesctor-resize.js`** — species ctor + resize pattern.

The species-ctor tests are TypedArray spec issues separate from closure-capture. (1) is a spread-dispatch issue. (2) is the same routing pattern as P20(d) — could be fixed similarly. (3) needs a separate sort dispatch.

### Why this works where §12.10/§12.12 attempts didn't

The key insight: the bug isn't in the SHARED scope_env path (which §12.10/§12.12 attacked). It's in the **per-closure env path** (use_scope_env=false), via a parallel state tracking system (`mt->last_closure_env_reg`). That state was being USED by `jm_write_last_closure_capture_if_matching` to forward let-initializer writes, but never RESET between sibling scopes. The fix is structural for the state lifetime, not for the captures protocol — much narrower in blast radius.

§12.14 mechanism (B) — "the READS at lines 1316-1317" — was a red herring. Those reads ARE from `jm_env_reload_shared_captures` after CALL expressions. The var->reg pointing at the stale env_reg came from earlier shared-env marking elsewhere. The actual primary bug was simply the LACK of a reset on `mt->last_closure_env_reg` at for-of/block boundaries.
