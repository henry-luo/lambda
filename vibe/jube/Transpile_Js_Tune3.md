# JS Runtime Performance Tuning Proposal — Round 3

Source data: clean release `lambda.exe` at current `master` (well past the
`release_run_003` commit `b406da25`), two baseline runs of
`./test/test_js_test262_gtest.exe --baseline-only --batch-only`
(34 240 timed tests each). Per-test figures below are the **mean of the two
runs** to damp the ~15 % run-to-run noise observed during the Round-2 work.
Analysis date: 2026-05-28.

This follows `Transpile_Js_Tune.md` (landed §3.1/§3.3/§6.x/§7.2.B) and
`Transpile_Js_Tune2.md` (all three proposed items — name-pool length
bypass §2.4.A/B, eval compile cache §3.2, and the external-scanner ID trie
§2.4.E — were implemented, measured, and **reverted** after none cleared the
"no correctness/perf regression" bar; see that doc's retraction notes). The
lesson carried into this round: **every item here is a hypothesis to be
measured against the real binary before keeping**, and the predicted wins are
stated as such.

---

## 1. Slow-cluster inventory (current `master`)

Top-200 slowest tests sum to **74.1 s** of the **626 s** full-suite total
(~12 %). They decompose cleanly:

| Cluster | n (top-200) | Sum | Avg | Shape |
| --- | ---: | ---: | ---: | --- |
| **A. Unicode-identifier parse** (`identifiers_start_unicode_*`) | 19 | 22.4 s | 1179 ms | compile-bound (Tune2 §2) |
| **B. RegExp `\p{…}` property escapes** (`property_escapes_generated_*`) | 110 | 28.5 s | 259 ms | runtime: per-codepoint property walk |
| **C. RegExp-literal eval-loop** (`literals_regexp_S7_8_5_*`) | 4 | 2.8 s | 694 ms | eval-bound (Tune2 §3) |
| **D. RegExp misc** (annexB escape-BMP, char-class escape) | 3 | 1.8 s | 599 ms | regex compile/scan |
| **E. Shift-operator A4 loops** (`S11_7_{1,2,3}_A4_*`) | 12 | 4.5 s | 379 ms | compile-bound straight-line |
| **F. URI encode/decode sweep** (`(en/de)codeURI*`) | 22 | 6.1 s | 277 ms | C-runtime sweep (Tune1 §7) |
| **G. TypedArray ctor cross-product** | 11 | 3.3 s | 297 ms | ctor × ctor nested loops |
| **J. Comment / misc eval-loop** | 2 | 0.7 s | 323 ms | eval-bound |
| Z. other | ~17 | 4.0 s | — | mixed |

Clusters **A, C, F** are already characterised (and, for A, found
hard to move — Tune2 §2.4 reverted). This round targets the two largest
*fresh* opportunities — **B** (by far the biggest, 28.5 s) and **E** — plus a
note on the eval-loop tail.

---

## 2. Cluster B — RegExp `\p{…}` property escapes (~28.5 s, biggest)

### 2.1 What the tests do

Each generated test (`built-ins/RegExp/property-escapes/generated/*.js`, from
the `regExpUtils.js` harness) does:

```js
const matchSymbols    = buildString({ loneCodePoints: [...], ranges: [...] });
const nonMatchSymbols = buildString({ ... });   // the complement
testPropertyEscapes(/^\p{General_Category=Letter}+$/u, matchSymbols,    "…");
testPropertyEscapes(/^\P{General_Category=Letter}+$/u, nonMatchSymbols, "…");
// …repeated for the gc=, short-name, and loose-name spellings (4 each)
```

`buildString` materialises a flat string of **every code point in the
property** (and, separately, its complement) — for broad properties like
`General_Category=Letter` that is **~130 k code points** on the match side and
the rest of the tested range on the non-match side. `testPropertyEscapes`
then runs `regExp.test(wholeString)`.

Breakdown of the 110 top-200 property tests: General_Category 32 (10.4 s),
Script_Extensions 32 (6.8 s), Script 26 (5.6 s), binary/other 20 (5.7 s).

### 2.2 Where the time actually goes (measured by code reading + standalone timing)

A standalone `General_Category=Letter` run is ~0.9 s. The work splits into:

1. **`buildString`** — already fused into the C builtin
   `js_test262_build_string` (`lambda/js/js_globals.cpp`); not the bottleneck.
2. **`regExp.test(wholeString)`** — and here the engine *already* has a good
   fast path: `js_regex_test` (`js_runtime.cpp:15203`) detects the anchored
   single-property form `^\p{X}+$` via `js_regexp_property_all_mode`
   (:15138) and routes to `js_regexp_test_property_all` (:15151), which walks
   the UTF-8 input code point by code point — **bypassing RE2 entirely** — and
   for each code point calls `js_regex_special_property_contains` →
   `js_regex_generated_property_contains` →
   **`js_regex_sorted_range_contains`** (:a binary search over the property's
   sorted range table). There is also a one-entry result cache keyed on
   `(chars ptr, len, mode)` that already collapses the 4 spelling-variants of
   each regex into one walk.

So the residual cost is **`O(input_codepoints) × O(log property_ranges)`** —
a fresh binary search per code point. For `Letter` (~700 ranges → ~10
compares/cp) over hundreds of thousands of code points, twice per test
(match + non-match side), across 110 tests, that is the 28.5 s.

### 2.3 The opportunity: a resumable cursor (exploit code-point locality)

`js_regex_sorted_range_contains` restarts its binary search from scratch on
every call, treating each code point as independent. But two facts make
consecutive lookups highly correlated:

- **The test inputs are monotonic.** `buildString` emits `loneCodePoints`
  then `ranges` in ascending order, so the walked string is (very nearly) a
  monotonically increasing code-point sequence.
- **Real text is locally clustered.** Even outside these tests, identifiers,
  words, and scripts produce runs of nearby code points.

#### 2.3.A — Resumable cursor / last-range hint in the property walk

**Change.** Give `js_regexp_test_property_all` a small mutable cursor — the
index of the range that satisfied (or bracketed) the previous code point —
and pass it into the range lookup. The lookup first checks the cursor range
and its immediate neighbours (O(1)); only on a miss does it fall back to the
full binary search (and update the cursor). For monotonically increasing
input the cursor advances linearly and the whole walk becomes
**O(input + ranges)** instead of **O(input × log ranges)**.

- **Implementation surface.** A cursor-aware variant of
  `js_regex_sorted_range_contains` (~15 lines) plus threading an `int* cursor`
  through `js_regexp_test_property_all` and
  `js_regex_(special|generated)_property_contains` (~20 lines). The existing
  flat `js_regex_sorted_range_contains` stays for non-walk callers.
- **Predicted win (hypothesis).** For the property cluster, collapses the
  per-cp `log(ranges)` factor to ~O(1); plausibly **−10…−18 s** off the
  28.5 s aggregate if the locality assumption holds. *To be measured.*
- **General benefit.** Any `/\p{X}/u` (or `/\p{X}/u` inside a larger pattern
  reduced to the property-all path) over real text — internationalised input
  validation, tokenizers, scanners — pays less per character.
- **Risk.** Low. Pure lookup-acceleration; the result is identical to the
  binary search (verify with a fuzz/equivalence check over random code points
  AND a monotonic sweep). No behaviour change.

#### 2.3.B — *(stretch, only if 2.3.A under-delivers)* range-set containment

If profiling shows the per-cp decode+compare still dominates after 2.3.A,
note that the **anchored** forms `^\p{X}+$` / `^\P{X}+$` are asking a
set-algebra question: "is every code point of the input in property P?" When
the input is itself known to be a sorted run of code points (the fused
`js_test262_build_string` could optionally hand the property walk the *range
list* it just built instead of re-deriving it from UTF-8), the answer is an
**O(input_ranges + property_ranges) merge** with no per-code-point work at
all. This is more invasive (couples the builder to the matcher) and only
helps the anchored whole-string form, so it is a fallback, not the lead.

### 2.4 What I'd land first

`2.3.A` alone — ~35 lines, near-zero risk, targets the single biggest cluster
and every real `\p{…}` match. Measure: re-run the baseline twice; the 110
property tests should drop materially and aggregate `sum` should fall. Keep
only if correctness is unchanged (the equivalence check is mechanical) and the
cluster improves beyond noise.

### 2.5 Measured result (implemented 2026-05-28, vs `release_run_004`) — **KEPT**

Implemented exactly as §2.3.A: a resumable cursor
`js_regex_sorted_range_contains_cursor` (`js_runtime.cpp`) plus a cursor-aware
`js_regex_generated_property_contains_cursor`
(`js_regex_generated_property_tables.inc`), threaded into
`js_regexp_test_property_all` for the generated-property kinds (the gc/script/
scx/binary bulk). Non-generated kinds keep the flat path. ~70 lines total.

- **Correctness.** `--baseline-only --batch-only` exited clean; **zero flipped
  exit codes** across all 583 `property_escapes` tests (joined by name vs the
  `release_run_004` timing TSV). The cursor is a pure lookup hint — every code
  path reduces to the same binary-search membership result.
- **Performance.** Measured twice. The first run was contaminated by two
  concurrent release builds — its control group (33 660 non-property common
  tests) ran +32.6 % *slower* than baseline, yet the property cluster still
  fell −13.8 %. A clean re-run on a quiet machine confirmed it: the control
  group returned to **−10.3 %** (within the ~15 % run-to-run noise band), and
  the generated-property cluster (439 tests) fell **61.84 s → 37.67 s
  (−24.17 s, −39.1 %)**. The control moving back to baseline when the load was
  removed proves the earlier +32.6 % was machine load, not a code regression —
  the change touches only the generated-property walk path and cannot reach the
  non-property tests.
- **Run-time note.** Actual test execution (Phase 2) is ~134 s; the long total
  wall-time is the harness's Phase 4 "retry regressions individually" step
  (~676 s re-running ~4 700 tests in isolation), unrelated to this change.

Verdict: clears the "no correctness regression AND beyond-noise perf
improvement" bar that retired all of Tune2. Change kept.

---

## 3. Cluster E — shift-operator A4 loops (~4.5 s) + general constant folding

### 3.1 What the tests do

`S11.7.{1,2,3}_A4_T*` are **2569-line straight-line files** of:

```js
if (-1 << 16 !== -65536) { throw new Test262Error('#513: -1 << 16 === -65536. Actual: ' + (-1 << 16)); }
if (-2 << 16 !== -131072) { throw new Test262Error('#514: …' + (-2 << 16)); }
…  // ~640 such statements per file
```

No loop. The tests pass, so the `throw` bodies never execute. The cost is
**compiling** ~640 constant `if`-statements — each with a constant shift, a
constant `!==` comparison, and a dead string-concat in the throw — into MIR
and JIT-compiling it.

### 3.2 The opportunity: AST-level constant folding

There is **no general constant folding** in the JS transpiler today (a grep
for `fold`/`const_fold` across `lambda/js/*.cpp` finds only an unrelated
export-unwrap comment). So `-1 << 16` is emitted as a runtime shift, `… !==
-65536` as a runtime compare, and the throw branch as live MIR.

#### 3.2.A — Fold constant arithmetic / bitwise / comparison subtrees

**Change.** In the AST→MIR lowering (`js_mir_expression_lowering.cpp`), add a
recursive constant-folder for nodes whose operands are numeric literals:
unary `-`, binary `+ - * / %`, the bitwise/shift ops `<< >> >>> & | ^ ~`, and
the comparisons `=== !== < > <= >=` when both sides fold to constants. A
folded `if (false) { … }` then lets the existing dead-branch path drop the
throw body entirely.

- **Implementation surface.** ~80–120 lines: a `js_ast_fold_constant(node)`
  returning an optional literal, invoked at binary/unary lowering and at
  `if`-condition lowering. Must replicate JS numeric semantics exactly
  (ToInt32/ToUint32 for the bitwise/shift ops, −0, NaN, double rounding) —
  this is the only real care point.
- **Predicted win (hypothesis).** Cuts MIR instruction count and JIT work for
  these files; plausibly **−2…−3 s** on cluster E. *To be measured* — the
  files are compile-bound, and folding reduces exactly the per-statement MIR
  that dominates.
- **General benefit.** Every program with literal arithmetic — config
  constants, bit-flag math, `1 << N` masks, computed table sizes — emits
  tighter MIR and runs faster. This is a foundational optimisation most
  engines have; its absence likely taxes far more than cluster E.
- **Risk.** Medium-low, concentrated entirely in **semantic fidelity**. A
  mismatch (e.g. folding `0.1 + 0.2` to the wrong double, or mishandling
  `>>>` sign) would be a silent correctness bug. Mitigate with a differential
  test: fold-vs-runtime over a large random operand corpus, asserting
  bit-identical results before enabling. Gate behind a flag for the first
  landing.

### 3.3 Measured result (implemented 2026-05-28, vs `release_run_004`) — **KEPT (scoped)**

Implemented the recursive folder `jm_try_fold_const`
(`js_mir_expression_lowering.cpp`) covering literals, unary `- + ~ !`, binary
`+ - * / %`, bitwise/shift `& | ^ << >> >>>`, and comparisons
`=== !== == != < <= > >=`, with exact runtime semantics (ToInt32/ToUint32 via
the same algorithm as `js_to_int32`; int results required to round-trip through
double within ±2⁵³ else bail; bails on non-finite results, `-0` from unary
minus, bigint, and `** && || ??`). Behind env gate `LAMBDA_JS_CONST_FOLD`
(default on; `=0` disables for A/B).

**Scope decision.** Wired the folder into **`jm_transpile_if` only**, as
dead-branch elimination: a constant test drops the dead branch entirely
(guarded by `jm_branch_dead_safe`, a whitelist that admits only
throw/expression/return/break/continue/block so Annex-B function-declaration
hoisting can never be lost; `var` hoisting is unaffected since it is a separate
`jm_collect_body_locals` pre-pass). I **did not** wire folding into
`jm_transpile_binary`/`jm_transpile_unary` value emission: those functions
return *raw native* registers (the caller `jm_transpile_box_item` boxes based on
`jm_get_effective_type`), and matching that native/boxed convention per-op
proved fragile — an initial attempt produced a `call`-operand type error
("expected double, got int"). The cluster-E win is entirely in the if-path, so
general value-folding was dropped for safety and left as a future step.

- **Correctness.** `--baseline-only --batch-only` exited clean; **zero flipped
  exit codes** across all 34 241 common tests. A differential test (3 780
  constant `if`-conditions across all ops over a boundary-value operand corpus)
  reported folded branch-selection == runtime for **all** cases (FAILS=0). Of
  note: with folding *off* the test surfaced a pre-existing literal-native
  comparison quirk for values in (2³¹, 2³²) (e.g. `0 < 4294967295` returning
  false); the folder computes these spec-correctly, and no test flipped.
- **Performance.** Cluster E (`S11.7.{1,2,3}_A4`, 12 tests) fell
  **6.14 s → 4.16 s (−1.98 s, −32.2 %)**. The control group (everything else)
  moved −12.8 % this run (quieter machine + likely some broad dead-branch wins);
  difference-in-differences (ratio **0.78**) attributes ~**−22 %** to the change
  on cluster E — beyond the ~15 % noise band. Predicted −2…−3 s; measured
  ≈ −1.4…−2 s real. The broad-average "general benefit" the proposal hoped for
  is *not* realised here, since value-folding was scoped out.

Verdict: clears the bar (zero correctness regression, beyond-noise cluster-E
win). Kept. Follow-up: revisit general binary/unary value-folding by emitting
results in the native/boxed convention `jm_transpile_box_item` expects.

---

## 4. The eval-loop tail (clusters C, J) — deferred to Tune2 §3 follow-up

`S7.8.5_*` (regexp literal in `eval`) and `S7.4_A5` (comment in `eval`) are
~65 k `eval()` calls in a tight loop. Tune2 §3.2 added an eval compile cache
but it could not fire: the regexp-literal evals hit the pre-existing
`js_create_regexp_from_source` fast path *before* the cache, and the
`var`-declaration evals route to Phase C (uncached). Two concrete, bounded
follow-ups remain if this tail is ever worth ~3 s:

- **C1.** Extend the eval cache to **Phase C** (top-level-script compiles),
  keyed on `(source, is_eval_direct, strict)`. This is the path the `var _x`
  evals actually take. Correctness care: direct-eval var hoisting into the
  caller scope.
- **C2.** Memoise `js_create_regexp_from_source` results for identical source
  strings. Care: RegExp objects are mutable and have observable identity, so
  the cache must return a *fresh wrapper* over a shared compiled program, not
  a shared object.

Neither is recommended ahead of §2 and §3; listed for completeness so the
tail is not re-discovered from scratch.

---

## 5. Implementation plan

In suggested order; each step independently revertible, each **kept only if it
clears "no correctness regression AND no performance regression vs the HEAD
baseline (within noise)"** — the same bar that retired all of Tune2.

1. **§2.3.A** (resumable property-walk cursor). ~35 lines, near-zero risk,
   biggest target. Add the cursor-equivalence test first (random + monotonic
   code-point sweeps vs the flat binary search), then measure the property
   cluster across two baseline runs.
2. **§3.2.A** (constant folding). ~100 lines; land behind a flag with the
   differential fold-vs-runtime test green, then measure cluster E and the
   whole suite (this one should move the broad average, not just E).
3. Re-measure: capture a `release_run_004` snapshot (timing TSV + per-test
   exit-code diff vs baseline) so the next round has a clean chain.
4. Only if §2.3.A under-delivers → evaluate §2.3.B (range-set merge).
5. The eval tail (§4) and the compile-bound A-cluster (Tune2 §2.3 spike)
   remain open; pursue only if §2/§3 leave the slow tail materially above 0.3 s.

## 6. Verification plan (per landed change)

1. **Correctness gate.** Re-run `--baseline-only --batch-only`; join per-test
   exit codes against the baseline TSV and require **zero flipped tests**
   (the check that retired Tune2 §2.4.E's both-paths form cleanly, and caught
   nothing — exactly the discipline we want).
2. **Targeted micro-equivalence.** §2.3.A: cursor lookup ≡ binary search over
   a random + monotonic code-point corpus. §3.2.A: folded value ≡ runtime
   value, bit-identical, over a random operand corpus.
3. **Performance.** Two baseline runs each for before/after; compare the
   *targeted cluster* (property tests for §2; shift tests + whole-suite
   average for §3) rather than only the noisy aggregate. Keep only on a
   beyond-noise improvement with no regression elsewhere.
4. **Memory.** Peak RSS and largest single-test growth must not increase
   materially.

## 7. Summary

The current slow tail is dominated by one fresh, clean target: **110 RegExp
`\p{…}` property-escape tests (~28.5 s)** whose already-fast code-point walk
restarts a binary search per character — a **resumable cursor** (§2.3.A)
should collapse that to near-linear and speed up every real Unicode-property
match. Second, the engine has **no constant folding** (§3.2.A); adding it
shrinks the compile-bound shift-operator files and, more importantly, tightens
every program that does literal arithmetic. Both are general engine
improvements, not test-shaped hacks. Given Round 2's 0-for-3 record against
the real binary, each is framed as a hypothesis and gated behind the same
measure-then-keep discipline; predicted wins (§2: −10…−18 s on the cluster;
§3: −2…−3 s on cluster E plus a broad average nudge) are explicitly
to-be-measured, not assumed.
