# Transpile_Js58_SparseArrays — Robust Sparse Array Support, Perf-Preserving

Date: 2026-06-16

Status: sparse correctness Phases 1-3 implemented and admitted; Phase 4 sparse-key cursor implemented as the next optimization tranche. Js57 closed the ES2024 module-evaluation work and the sparse-array `SPARSE_GAP_MAX = 10000` perf workaround in P8. The P8 workaround moved 9 partially-broken array methods from "subtly wrong but invisible" to "subtly wrong and more reachable" — any JS that does `arr[20000] = …` followed by `reduce` / `findLast` / `fill` / `sort` / `arr.length = N` now hits the real sparse path. Js58 finishes that integration with grouped `test/js/sparse_*.{js,txt}` fixtures and a clean test262 admission gate.

The audit in Js57 §13 P8 documented the gaps (`reduce` / `reduceRight` / `findLast` / `findLastIndex` / `fill` / `copyWithin` / `reverse` / `sort` ignore sparse entries; `includes` / `flat` / `flatMap` / `concat` / `slice` / `splice` partially miss them; `arr.length` truncation leaks orphaned sparse entries). Test262 doesn't exercise these gaps for arrays with gap > 10K because its sparse fixtures use smaller gaps that still ride the dense path. Js58 fixes the gaps **and** authors a focused `test/js/sparse_*.{js,txt}` suite to lock the behaviour in.

## 1. Starting Baseline

Current checked-in release baseline at Js58 start (Js57 final):

```text
# Scope: ES2024 (skip ES2025+ features)
# Total passing: 40261
# Total tests: 42889  Skipped: 2628  Batched: 40261  Passed: 40261  Failed: 0
# Runtime: ~131 s clean (within Js57 +5 % ceiling)
# t262_slow.txt: 2 entries (decodeURI / decodeURIComponent — CPU-bound, not corruption)
# skip_list.txt: 5 entries (Math.random + 2 RegExp + rgi-emoji-17 + Gate J)
```

The Js58 acceptance bar:

- Passing count stays `>= 40261` after every phase.
- Regressions count is `0` at every phase boundary.
- 0 batch-lost, 0 crash-exits at the gate of every admission run.
- `skip_list.txt` retains the Js57 entries.
- Runtime stays within `+3 %` of the Js57 baseline (~131 s) — 135 s ceiling per phase. Sparse work is hotter on a few `Array.prototype.*` benchmarks than the TLA work was; tightening the budget from `+5 %` to `+3 %` flags any regression earlier.
- Final `# Scope:` line stays at `ES2024 (skip ES2025+ features)` — Js58 is the last ES2024 proposal. ES2025 features open in Js59.
- New regression test files `test/js/sparse_*.{js,txt}` discovered by `test_js_gtest` (≥ 8 new test files).

### 1.1 What "no perf regression" means in concrete numbers

The load-bearing invariant for Js58: **dense-only arrays (`arr->extra == 0`) must hit byte-identical instructions to today's hot path**. Concretely:

- `Array.prototype.{forEach,map,filter,every,some,reduce}` over a 1000-element dense array: within `±2 %` of pre-Js58 wall-clock.
- `Array.prototype.{indexOf,lastIndexOf,find,findIndex}` over a 10K-element dense array: within `±2 %`.
- The headline `test/test_js_transpile_timing_gtest` corpora (`lib_acorn`, `dom_jquery`, `underscore`, `ramda`, `lodash`, `yup`, `ajv`) all within `±2 %` of Js57.

Per-phase pre/post timing snapshots saved to `temp/js58_perf/`.

## 2. Diagnosis (from Js57 P8 audit)

Js57 P8 lowered `SPARSE_GAP_MAX` from 1,000,000 to 10,000 to dodge a `gc_data_zone` block-overlap corruption in multi-MB dense fills. Side effect: any `arr[N] = …` with `N - arr.length > 10000` now routes through `js_array_store_sparse_property` (`js_runtime.cpp:4833`), which writes to the companion `Map*` stored at `arr->extra` and bumps `arr->length` but doesn't touch the dense buffer.

The audit's findings (verified by direct read of the implementations, not grep):

### 2.1 Severe gaps — sparse entries silently invisible

| Method | Line | Failure mode |
|---|---:|---|
| `reduce` | `js_runtime.cpp:23143` | Initial-accumulator hunt and main loop both gate on `js_array_method_has_property`, which checks dense + prototype but not companion-Map data entries. Sparse values skipped entirely. |
| `reduceRight` | same | Same mechanism, backward direction. |
| `findLast` | `js_runtime.cpp:24015` | Loops `for (i = src->length - 1; i >= 0; i--)` and reads `src->items[i]` directly, bypassing companion-Map. Sparse entries below length are invisible. |
| `findLastIndex` | `js_runtime.cpp:24034` | Same shape. |
| `fill` | `js_runtime.cpp:24162` | Direct `a->items[i]` writes; sparse-Map entries in the fill range get neither overwritten nor deleted. |
| `copyWithin` | (delegates to generic) | Same gap as `fill`. |
| `reverse` | dense in-place swap | Touches only dense items; sparse entries stay at their original indices, no swap to mirror position. |
| `sort` | generic compare loop | Reads dense only; sparse entries excluded from sort but left at original positions — output is corrupt mix of sorted dense + unmoved sparse. |
| `arr.length = N` (`js_array_set_length_throw`) | `js_runtime.cpp:22140` | Writes the new length field but doesn't iterate the companion Map to delete entries at indices ≥ N. Result: orphaned entries remain in the Map; `Object.keys(arr)` exposes them; `arr[k]` returns the orphan even though `k >= arr.length` — observable spec violation. |

### 2.2 Partial gaps — accessor-aware but data-blind

| Method | Line | Failure mode |
|---|---:|---|
| `includes` | `js_runtime.cpp:23561` | Fast path gated on `a->extra == 0`; slow path calls `js_array_element` which only resolves accessors, not data entries. |
| `flat` / `flatMap` | `js_runtime.cpp:24127` | Iteration uses `js_array_method_has_property` (data-blind for sparse). |
| `concat` | `js_runtime.cpp:23720` | Spread loop uses same `has_property` gate. |
| `slice` | `js_runtime.cpp:23646` | Fast path memcpy of dense slice; `js_array_has_element(check_proto=true)` resolves prototype but not own sparse data. |
| `splice` | `js_runtime.cpp:24173` | Deletes only dense entries from the splice range; sparse entries in the range stay in the Map. |

### 2.3 Robust paths (no work needed)

- `js_array_find_next_own_element` (`js_runtime.cpp:21877`) — fully sparse-aware; the central iter helper.
- Methods routed through it: `indexOf`, `lastIndexOf`, `forEach`, `map`, `filter`, `every`, `some`, `find`, `findIndex`.
- `Object.keys`, `Object.values`, `Object.entries`, `Object.getOwnPropertyNames` — explicitly iterate companion shape entries.
- `in` operator (`js_has_property`) — consults companion-Map.
- `delete arr[N]` — clears both dense slot and companion-Map entry.
- GC tracing — `gc_trace_object` marks `arr->extra` unconditionally when non-zero.

## 3. The Phases

Js58 is four phases. P1 fixes the severe gaps, P2 fixes the partials with a one-line helper change, P3 fixes the length-truncation orphan, P4 is an optional perf optimisation for sparse-heavy paths that we only land if profiling justifies it.

### Phase 1 — Centralise iteration on the sparse-aware helper

**Touch**: `lambda/js/js_runtime.cpp` — `js_array_generic_reduce_with_object` (line 23143), `js_array_generic_find_last` (line 23968), `js_array_generic_fill` (line 24158), `js_array_generic_copy_within`, `js_array_generic_reverse`, `js_array_generic_sort`.

**Approach**: each of the 9 broken methods is converted from a hand-rolled `for (i = 0; i < length; i++)` loop to a `while (js_array_find_next_own_element(...))` loop. The helper already has the dense-fast-path / sparse-merge logic; the conversion is mechanical.

For `findLast` / `findLastIndex` / `reduceRight` (right-to-left), add a sibling helper `js_array_find_prev_own_element` that walks dense items from `start` backward and the companion-Map shape entries to find the *largest* index `< start`. Same dense-fast-path structure: returns immediately when `a->extra == 0`.

For `fill` / `copyWithin`, the spec requires writing to all indices in `[k, final)` regardless of present-ness. Two sub-phases:

1. **Dense pass**: existing `a->items[i] = v` loop for indices in `[k, capacity)`.
2. **Sparse-cleanup pass**: when `a->extra != 0`, walk the companion-Map shape entries and (a) delete any sparse entry whose index is in the fill range covered by the dense pass (now overwritten), (b) write `v` to any sparse entry whose index is in `[capacity, final)` (still beyond dense). After Phase 1, `js_array_set_sparse_for_fill(arr, idx, v)` is the helper that does (b) without touching `arr->length`.

For `reverse` / `sort`, the dense + sparse merge is more invasive. Approach: collect all (index, value) pairs via `js_array_find_next_own_element` into a temp vector, sort/reverse, then rewrite — clearing the companion Map and re-emitting any sparse entries that fall in the new dense range, sparse-emitting any that don't. This is O(n + s) where n = dense count, s = sparse count; the dense-only case still completes in n iterations with `a->extra == 0` so no companion-Map work happens.

**Risk**: medium. `sort` is the highest-touch — it's also the one with the most existing test262 coverage, so any regression flags loud and early.

**Acceptance**: after Phase 1, every regression test in `test/js/sparse_reduce_*.{js,txt}`, `test/js/sparse_find_last_*.{js,txt}`, `test/js/sparse_fill_*.{js,txt}`, `test/js/sparse_sort_*.{js,txt}` passes. Full test262 baseline at 40261+ with 0 regressions. Dense-array `js_transpile_timing` corpora within `±2 %`.

### Phase 2 — `js_array_method_has_property` consults companion-Map data

**Touch**: one function, `js_array_method_has_property` in `js_runtime.cpp` (around line 4953).

**Change**: add 4 lines that read the companion Map when `a->extra != 0`:

```c
static bool js_array_method_has_property(Item arr, Item key) {
    Array* a = arr.array;
    // … existing dense + accessor + prototype checks …
    if (a->extra != 0 && get_type_id(key) == LMD_TYPE_STRING) {
        Map* pm = (Map*)(uintptr_t)a->extra;
        bool found = false;
        Item v = js_map_get_fast_ext(pm, it2s(key)->chars, it2s(key)->len, &found);
        if (found && v.item != JS_DELETED_SENTINEL_VAL) return true;
    }
    return false;
}
```

This single change corrects `includes`, `flat`, `flatMap`, `concat`, `slice`, `splice` simultaneously — they all gate on `has_property` to decide whether to include an index. The added work executes only when `a->extra != 0`; dense-only arrays pay nothing.

**Acceptance**: `test/js/sparse_includes_*.{js,txt}`, `test/js/sparse_flat_*.{js,txt}`, `test/js/sparse_concat_*.{js,txt}`, `test/js/sparse_slice_splice_*.{js,txt}` pass. Full test262 baseline at 40261+ with 0 regressions.

### Phase 3 — `arr.length = N` deletes orphaned sparse entries

**Touch**: `js_array_set_length_throw` in `js_runtime.cpp` (around line 22140).

**Change**: a helper `js_array_delete_sparse_indices_from(arr, new_length)` already exists at `js_runtime.cpp:4904` — wire it into the truncation branch:

```c
if (a->extra != 0 && new_length < old_length) {
    js_array_delete_sparse_indices_from(lam::gc_borrow(a), new_length);
}
```

Cost: O(sparse_count) one-time scan of the companion-Map shape entries on truncation. Dense-only arrays skip the branch entirely.

**Acceptance**: `test/js/sparse_length_truncate.{js,txt}` passes (asserts: after `arr.length = N`, `Object.keys(arr)` contains no indices ≥ N, `arr[k]` returns undefined for k ≥ N). Full test262 baseline at 40261+ with 0 regressions.

### Phase 4 — sub-linear sparse-key lookup

After Phases 1-3, `js_array_find_next_own_element` still walks the companion Map's full ShapeEntry list on every call (O(s) per call, O(s²) per array iteration). If sparse arrays become a hot path in real workloads, augment the companion Map with:

- A **sorted `sparse_keys[]` array** maintained on insertion: `find_next` does `std::lower_bound` for O(log s).
- Cached `(min_sparse_idx, max_sparse_idx)` so `start > max_sparse_idx` short-circuits the Map walk entirely.

Both changes are sparse-side only — dense fast path stays byte-identical.

**Decision criterion**: land Phase 4 only if, after Phases 1-3, the `test_js_transpile_timing` corpora show >2 % regression on a sparse-heavy corpus (none of the existing corpora are sparse-heavy; this would require a new bench).

**Landed variant:** the first Phase 4 tranche avoids `Map` / `TypeMap` layout changes. Instead, each sparse-aware array iteration creates a local sorted sparse-key cursor from the companion Map's numeric shape entries, uses binary search for next/previous sparse candidates, and refreshes the cursor when callback code changes the companion Map shape. Candidate values are still read from the current companion Map slot, so deletes and re-adds remain visible during iteration. Dense arrays do not allocate a cursor buffer.

## 4. Acceptance Gates

Each phase has the same gate:

```text
make release && make build-test
test/test_js_test262_gtest.exe --baseline-only --run-async \
    --async-list=test/js262/test262_baseline.txt \
    --js-timeout=10 --jobs=12 \
    --write-failures=temp/js58_p{N}_guard.tsv
test/test_js_gtest.exe --gtest_filter='*sparse_*'
test/test_js_transpile_timing_gtest.exe
```

Pass conditions:
- baseline: `Fully passing: 40261 / 40261` (or higher if Phase admitted new tests), `Regressions: 0`, `Failed: 0`
- sparse tests: all pass (added incrementally per phase)
- timing: all corpora within ±2 % of saved Js57 baseline in `temp/js58_perf/js57_baseline.tsv`

## 5. Test Plan (`test/js/sparse_*`)

These tests target the gaps test262 doesn't reach with `SPARSE_GAP_MAX = 10000`. Each is a `.js` script whose stdout is matched against a paired `.txt`.

| Test file | What it covers |
|---|---|
| `sparse_basic.{js,txt}` | `arr[20000] = X; arr[k]` for k in/out of sparse range; `'k' in arr`; `Object.keys(arr)`; baseline sanity. |
| `sparse_iter_reduce.{js,txt}` | `reduce` over sparse — assert callback fires exactly N times for N present entries, accumulator carries sparse values. |
| `sparse_iter_reduce_right.{js,txt}` | `reduceRight` mirror. |
| `sparse_iter_find_last.{js,txt}` | `findLast` / `findLastIndex` return sparse entry past the dense range. |
| `sparse_iter_includes.{js,txt}` | `includes(v)` finds `v` stored only sparsely. |
| `sparse_iter_flat.{js,txt}` | `[[arr], …].flat()` preserves sparse entries; `flatMap` callback fires for sparse entries. |
| `sparse_iter_concat.{js,txt}` | `[1,2].concat(sparseArr)` carries sparse entries to result. |
| `sparse_iter_slice_splice.{js,txt}` | `slice(0, len)` includes sparse entries below `len`; `splice(start, count)` removes sparse entries in range and returns them. |
| `sparse_mutate_fill.{js,txt}` | `arr.fill(v, k, end)` writes `v` at sparse indices in range; entries beyond range untouched. |
| `sparse_mutate_copy_within.{js,txt}` | `copyWithin` moves sparse entries; the source range stays as-was. |
| `sparse_mutate_reverse.{js,txt}` | `reverse` of sparse array swaps sparse entries to mirrored positions. |
| `sparse_mutate_sort.{js,txt}` | `sort` on sparse — present entries collapse to the front in sorted order; holes go to the end (per ES spec). |
| `sparse_length_truncate.{js,txt}` | `arr.length = N` deletes sparse entries at indices ≥ N; `Object.keys`, direct read, `in` all confirm. |
| `sparse_length_extend.{js,txt}` | `arr.length = N` where N > current length adds holes (not sparse entries); existing sparse entries preserved. |
| `sparse_already_robust.{js,txt}` | Regression-lock for the helper-routed methods: `every`, `some`, `indexOf`, `lastIndexOf`, `forEach`, `map`, `filter`, `find`, `findIndex`. Asserts callback counts and return values on the same sparse fixtures used by the broken-method tests, so any future regression in the helper itself is caught. |
| `sparse_gc_survival.{js,txt}` | Allocate a sparse array, trigger several GC cycles via large string concatenation, then verify sparse entries survive intact. Pins the GC tracing of `arr->extra`. |

Implementation note: the landed suite groups related rows into 10 fixtures rather than one file per method family. The grouped files are `sparse_basic`, `sparse_reduce`, `sparse_find`, `sparse_includes`, `sparse_concat_flat`, `sparse_slice_splice`, `sparse_mutate`, `sparse_length`, `sparse_already_robust`, and `sparse_gc_survival`.

15 test files were the original split-plan target; each `.js` ≤ 80 lines, `.txt` ≤ 30 lines. The grouped suite keeps the same coverage with less fixture duplication.

The robustness-lock file (`sparse_already_robust`) is the safety net: if Phase 1's helper refactor breaks something in the already-correct iteration methods, this catches it without requiring a full test262 run.

## 6. Out Of Scope

- **Raising `SPARSE_GAP_MAX` back up.** The latent `gc_data_zone` block-overlap that Js57 P8 dodged is a separate proposal. Js58 fixes correctness of the sparse path; the allocator-layer fix is its own ~3-day investigation (track per-allocation high-water marks across `data_zone_reset`, or have `allocate_block` reject ranges that overlap existing live allocations).
- **`Proxy` arrays with non-default `has` / `get` / `set` traps.** Sparse + Proxy is a known complex interaction; covered separately in the Js59 ES2025 proposal if needed.
- **TypedArrays** — these have their own fixed-buffer model, not affected by sparse-Map.
- **`Array.from` / `Array.of` / `new Array(N)`** — already sparse-aware (use `js_array_new_sparse_length`).
- **`Object.defineProperty(arr, "N", { …non-trivial descriptor… })`** with N as a sparse index — descriptor kernel handles this via accessor pairs, not data entries. Existing behaviour preserved.

## 7. Phase Effort Estimates

| Phase | Files touched | LOC | Risk | Estimate |
|---|---:|---:|---|---|
| P1 — centralise iteration on helper | 1 (.cpp) | ~250 | medium (sort/reverse refactor) | 1.5 days |
| P2 — `has_property` companion-Map check | 1 (.cpp) | ~12 | low | 30 min |
| P3 — length-truncation cleanup | 1 (.cpp) | ~8 | low | 30 min |
| P4 — sub-linear sparse-key lookup | 1 (.cpp) + struct change | ~100 | low (sparse-side only) | 1 day (only if needed) |
| Test authoring | 10 grouped (.js+.txt pairs) | ~700 LOC total | — | 1 day |

**Total**: 3-4 days for full correctness; Phase 4 only if profiling justifies.

## 8. Why This Doesn't Regress Test262

Phases 1-3 only change behaviour when `arr->extra != 0`. Across the 40261 test262 baseline:

- ~0 tests reach the sparse path under `SPARSE_GAP_MAX = 10000` (any test with gap > 10K). Verified by tracing: no baseline test calls `js_array_store_sparse_property` more than once per test scope.
- The handful that do hit sparse storage already pass via the robust paths (`indexOf`, `every`, etc.).
- The broken-path tests are excluded *because* the broken paths return wrong results that happen to match the slot-being-undefined-coincidence — these tests don't assert sparse behaviour explicitly. Phase 1's fix changes them from "accidentally pass" to "correctly pass"; no observable diff at the test262 level.

## 9. Why This Doesn't Regress Perf

The instruction-level invariant: every method's dense path is gated on `if (arr->extra == 0)`. For dense arrays, the new sparse-aware branches never execute. The `if` itself is a single int compare-and-branch that the predictor wins on every iteration of a dense-only loop.

Empirical check at each phase: re-run `test_js_transpile_timing_gtest` on the headline 7 corpora (lib_acorn, lib_lodash, lib_ajv, lib_yup, ramda, underscore, dom_jquery). Saved baselines in `temp/js58_perf/js57_baseline.tsv` from before Phase 1.

The only sparse-path perf hit comes in Phase 1's `sort`/`reverse` refactor for arrays with both dense and sparse content — those are O(n + s) instead of O(n). Acceptable, since sparse `sort` was previously *wrong*, not fast.

## 10. Final Js58 Numbers (verified)

| Metric | Js57 final | Js58 verified |
|---|---:|---:|
| Baseline fully passing | 40261 | 40261 |
| Non-fully-passing | 0 | 0 |
| Regressions | 0 | 0 |
| Failures in baseline | 0 | 0 |
| Sparse-path correctness | partial (9 broken methods + length leak) | full |
| Dense-path wall-clock | n | timing smoke passed; formal `±2 %` comparison pending baseline file |
| Sparse-key lookup microbench | n/a | `forEach` 59 ms → 51 ms; `indexOf` 44 ms → 35 ms on 2,500 sparse keys |
| `test/js/sparse_*` test files | 0 | 10 grouped fixtures covering the 15 split-plan method families |
| Scope line | ES2024 | ES2024 (Js58 is last ES2024 proposal) |

Js58 is the closer for ES2024 sparse semantics. With the Phase 1-3 sparse fixes admitted, Lambda's array implementation now matches the intended V8/SpiderMonkey sparse-array behaviour for the test262-relevant method families covered here. The dense-workload timing smoke passes all 7 headline transpile corpora; the saved `temp/js58_perf/js57_baseline.tsv` comparison file is not present in this checkout, so the final `±2 %` claim remains a follow-up measurement rather than a hard admission fact.

## 10.1. Js58 P0.1 — Slice/Splice OOB Fix (landed)

ASAN caught a real heap-buffer-overflow in `js_array_method` for slice/splice on sparse arrays:

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60700000e1b8
READ of size 8 at js_runtime.cpp:23738 in js_array_method (slice)
READ of size 159992 at js_runtime.cpp:24324 in js_array_method (splice)
```

For sparse arrays, `length > capacity`. The dense items[] buffer is sized to capacity, but slice/splice were reading `items[start+i]` up to `length` and the `memmove(elements_to_move)` based on `old_len`.

Fix in `js_runtime.cpp` (slice path ~line 23735, splice path ~line 24280):
1. Slice: treat indices `>= capacity` as hole sentinels (no OOB read).
2. Splice deleted-elements: same — bound read by capacity.
3. Splice shift memmove: cap `elements_to_move` by `dense_end = min(old_len, capacity)`. Holes past capacity remain holes after splice.
4. Grow path: also cap the `memcpy(new_items, a->items, ...)` by capacity, and recompute `dense_end` after expansion.

Resulting behaviour: `sparse_slice_splice.js` test now passes deterministically (no ASAN abort). `slice-100[50]` reads as `undefined` (correct ES semantics for hole) instead of the previous `null` (which leaked through from null-initialized dense memory).

## 10.5. Decision: accept the failure, skip only the failing sub-tests (not the whole test)

After exhausting all four no-mir-gen approaches (§10.1–10.4), the decision is to **accept the
failure** rather than continue bending MIR emission around a register-allocator defect, or take on a
deep mir-gen spill-bug fix. But rather than skip the *entire* `lib_marked` gtest, we skip **only the
sub-tests that actually fail**, so the test keeps verifying what works.

**What `lib_marked.js` exercises** (probed empirically): the library-load smoke check
(`typeof marked === 'object'`, `typeof parse === 'function'`) **passes**; all 9 markdown-parse
sub-tests (paragraph, heading, bold/italic, inline-code, fenced, ul, ol, link, parseInline)
**throw** — every one runs `_Lexer.blockTokens`/`inlineTokens`, which hit the mir-gen bug and corrupt
the freshly-built Lexer (the safety nets turn the would-be SIGSEGV into a catchable `TypeError`).

**What was decided (no gtest-level skip):**
- `test/test_js_gtest.cpp` is **unchanged** — `lib_marked` runs as a normal parametrized test and
  reports `[ OK ]` (not `[ SKIPPED ]`).
- `test/js/lib_marked.js` keeps the passing smoke check as real assertions, and wraps the 9 parse
  sub-tests in a `probe(label, fn)` helper: it prints the real result on success, or
  `SKIP <label> (mir-gen blockTokens bug)` when the **known `TypeError`** is caught. A non-`TypeError`
  is re-thrown so unrelated breakage is never masked.
- `test/js/lib_marked.txt` matches the current reality (smoke check + 9 `SKIP` lines + `MARKED_DONE`).
- The **runtime safety nets are kept** (`js_map_get_fast`, `js_invoke_fn`, `js_obj_typemap`,
  `js_class_get`/`js_class_id`): they degrade the SIGSEGV to the catchable `TypeError` the probe relies on.

**Why per-sub-test rather than whole-file skip:** the library *does* load correctly; only the
JIT-miscompiled parsing path is broken. Skipping just the failing sub-tests keeps the smoke test live
(so a regression in library construction would still be caught) while quarantining the known defect.
The suite stays 187/187 green.

**Re-enable criteria / built-in signal:** when the mir-gen spill bug is fixed, the 9 parses will
succeed and emit real HTML — which will **mismatch** the `SKIP` lines in `lib_marked.txt`, failing
the test. That failure is the prompt to restore the real expected HTML (the per-`probe` comments
carry the expected output) and retire the `probe` wrapper. The fix vehicle is a future
`patches/mir-gen-*.patch` via the hardened `MIR_PATCH` pipeline.

**Kept from this effort (independent wins):**
- Makefile MIR-patch application is now idempotent and **fails loudly** if a patch can't apply
  (no more silently building an unpatched `libmir.a`).
- `patches/mir-alloca-branch-fix.patch` applied + `libmir.a` rebuilt — a genuine fix for the
  inlining alloca-after-branch misclassification (regression-clean; not the lib_marked bug, but a
  real correctness fix that was stale in the prebuilt lib).
- Sparse `slice`/`splice` OOB fixes (§10.1) and the runtime corruption safety nets.

## 10.6. lib_marked Follow-up — no-mir-gen workaround found

This supersedes the §10.5 quarantine decision. `lib_marked.js` now runs the real markdown parse
assertions again, with no `lambda/mir.c` / mir-gen patch and no MIR patch pipeline dependency.

Two independent issues were hiding behind the same symptom:

1. **Stale closure-env readback metadata.** The previous Js56 Gate K assumption preserved
   `last_closure_*` across member calls. In `_Lexer.blockTokens`, skipped callback branches left
   stale closure env state behind; later unrelated method calls read it back into the current lexer
   path and corrupted the fresh `_Lexer` object. The durable workaround is to clear closure tracking
   before lowering the current member-call arguments, then let any closure created by those arguments
   repopulate the tracking state for the post-call readback. `jm_readback_closure_env` also now has a
   runtime null-env guard, so a stale zero register cannot become a native null dereference.
2. **No-capture positive lookahead in the regex wrapper.** Marked's heading rule relies on
   `(?=\s|$)` being zero-width. The old `X(?=Y) -> X(Y) + trim` rewrite works for simple trailing
   lookaheads, but it changes the full-match span for middle/leading no-capture assertions. The
   wrapper now rewrites safe no-capture positive lookaheads to a synthetic zero-width marker with a
   post-filter assertion. Lookaheads under an alternation parent keep the older rewrite path so a
   failing assertion does not reject the whole match before JS-style alternation fallback can run.

Validation from the follow-up:

- `test/js/lib_marked.js` emits the real expected HTML for paragraph, heading, bold/italic,
  inline-code, fenced-code, lists, links, and `parseInline`.
- `test/js/regex_lookahead.js` now covers middle/leading zero-width lookahead, the marked heading
  shape, and the marked inline alternation fallback.
- `make build`, `make release`, and `make build-test` pass.
- `test/test_js_gtest.exe --gtest_filter='*lib_marked*:*regex_lookahead*' --gtest_brief=1` passes
  2/2 tests.
- `test/test_js_transpile_timing_gtest.exe --gtest_brief=1` passes all 7 timing corpora. The saved
  `temp/js58_perf/js57_baseline.tsv` comparison file was not present in this checkout, so this pass
  was a timing smoke rerun rather than a formal per-corpus `±2 %` comparison.
- Documented 12-worker test262 gate:
  `test/test_js_test262_gtest.exe --baseline-only --run-async --async-list=test/js262/test262_baseline.txt --js-timeout=10 --jobs=12 --write-failures=temp/js58_lib_marked_guard.tsv`
  finished with `Fully passed: 40261 / 40261`, `Failed: 0`, `Regressions: 0`, and header-only
  failure manifests under `temp/`.

## 10.4. lib_marked — reload-from-MIR_ALLOCA-home attempt (tried, reverted)

Definitive root cause confirmed via disassembly: `_Lexer.blockTokens`'s `scope_env` pointer
(`js_alloc_env(4)` result) is held in callee-saved `x23` and **lost across the loop back-edge**
(`x23` reads back as a small garbage value, e.g. 4). It is a genuine **mir-gen register-allocator
spill bug** at this call-heavy loop's back-edge join — NOT scope_env-specific in nature; scope_env
is simply what landed in the lost register.

Attempted fix (no mir-gen change): per-frame `MIR_ALLOCA` "home" slot — store `scope_env_reg` into
it at function entry, reload `scope_env_reg` from it at each loop header (via the existing
`jm_scope_env_reload_vars` hook). The MIR was emitted exactly as designed (verified in the dump:
`alloca se_home`, top-of-function placement, store after `js_alloc_env`, reloads at every loop
header).

**Why it failed — GVN load-elimination.** mir-gen *deleted* every loop-header reload. The home is a
private alloca slot whose address never escapes, so mir-gen's escape analysis proves `[se_home]` is
unmodified across calls and concludes the reload is redundant (the value is "already" in the
register). Confirmed in the disassembly: `x23` is defined once (the `js_alloc_env` result) and never
redefined across the loop — the reloads are gone.

**The fundamental tension** (verified against `mac-deps/mir/mir.c` + `mir-gen.c`):
- A **non-escaping** alloca → address is a rematerializable FP-relative constant (good: not itself
  carried across the back-edge) **but** loads from it are elided as redundant (bad).
- Making the address **escape** (e.g. pass `&se_home` to a barrier) blocks the elision **but**
  forfeits the constant-displacement rematerialization → the home-address register is now itself
  carried across the back-edge and lost (circular).
- `LICM` (`loop_invariant_p`, mir-gen.c:5347 rejects `MIR_OP_VAR_MEM`) does NOT hoist the reload —
  but GVN/redundant-load-elim is a separate pass and DOES kill it.

Other homes also fail: a **heap** home (`js_alloc_env(1)`) makes the load non-elidable but its
pointer is carried across the loop and lost the same way; a **C-global** home is non-elidable and
constant-addressed but is reentrancy-unsafe (`blockTokens` recurses for nested list items).

**Conclusion.** All four no-mir-gen approaches now exhausted, each foundering on a facet of the same
reality: capture-demotion / per-block (§10.1–10.3) can't move the irreducible function-level
captures (`src`/`tokens`/`token` are mutated-and-shared params/lets); reload-from-home can't survive
mir-gen's optimizer. The only robust fixes are (A) patch mir-gen's spill/back-edge handling (now
shippable via the `MIR_PATCH` pipeline the Makefile applies), or (B) accept lib_marked as a
known-fail behind the existing safety nets (deterministic `TypeError`, no crash, no other test
affected). The reload scaffolding was reverted; the analysis is retained here.

## 10.3. lib_marked Root-Cause Identified (not yet fixed)

Further bisection in a follow-up turn produced a more reduced repro (`/tmp/marked_bt37.js`):
- Real lib_marked.js preamble (lines 1-1290 — helpers, regexes, _Tokenizer, full _Lexer class up to constructor)
- A minimal `blockTokens` body with:
  - `while (src)` loop
  - First `if (token = this.tokenizer.space(src))` with `tokens.push(token)`
  - `if (this.options.extensions && this.options.extensions.startBlock) { ... .forEach(arrow capturing this + 3 block-scoped lets) ... }`
  - Second `if (token = this.tokenizer.text(src))`

Removing the second `if (token = ...)`, the closure, the closure's `this` capture, OR the first `tokens.push` makes the crash disappear.

**MIR dump analysis** (`temp/js_mir_dump.txt`, search `_Lexer_blockTokens_60`):

`blockTokens` allocates a 4-slot scope env at function entry (`js_alloc_env_9257`):
- slot 0 → `startIndex` (block-scoped to the `if extensions` branch)
- slot 1 → `tempStart` (same)
- slot 2 → `tempSrc` (same `const`)
- slot 3 → `this` (lexical binding)

After **every** `js_call_function`, the JIT re-reads `i64:24(js_alloc_env_9257)` (slot 3 = `this`) into a "this" register — this is the standard pattern for keeping captured `this` live across calls that mutate `js_current_this`.

**The crash:** native `ldr x0, [x23, #0x18]` where `x23 == 0`. `x23` is the register MIR's allocator assigned to `js_alloc_env_9257`, and somewhere along the multi-path control flow (the two `if (token = ...)` branches with their early-`continue` back-edges), the value is clobbered without being reloaded.

**Why block-scoped vars matter:** the JIT hoists block-scoped lets into the function-level scope_env because the forEach arrow references them. This bloats the scope env and increases register pressure / spill complexity, making it more likely for the allocator to lose `js_alloc_env_9257` on one path.

**Fix paths (any one is enough):**
1. Fix MIR's register allocator / live-range tracker for back-edges in functions that have a scope env. The lost-value behaviour smells like a liveness bug across `continue` edges in a loop containing `is_truthy` branches.
2. Don't hoist block-scoped lets into function-level scope_env — give each block its own env. Bigger refactor; affects all closures inside blocks.
3. Force `js_alloc_env_9257`-style scope env regs into callee-saved registers explicitly (or spill to a fixed FP-relative slot at function entry, then reload before each use). Bandaid but localized.

### 10.3.1. Option 3 attempt (tried, reverted)

Implementation:
- Compute `parent_block_lets` per parent function (recursive all-let-const minus top-level let-const)
- In `js_mir_module_batch_lowering.cpp` scope_env_names population, skip captures present in `parent_block_lets`
- In the remap loop, assign closure-only slots (beyond `slot_count`) for skipped captures so the closure env still has space for them

This change worked on the reduced repro (`/tmp/marked_bt37.js` no longer SIGSEGVs after the fix) and lib_marked itself stopped crashing with `m->type` corruption — but lib_marked then surfaced a second-order failure ("Cannot read properties of undefined (reading 'async')") inside `#parseMarkdown`'s arrow, and 3 other tests regressed: `hljs_highlight`, `lib_immer`, `lib_zod`.

Why option 3 as implemented broke things: when a capture is "block-scoped" by the parent's body but is also referenced by a closure nested inside the SKIPPED block, the closure-only slot assignment doesn't agree with the closure body's MIR (which was emitted from the analysis pass that ran BEFORE the slot reassignment). The mismatch flips a previously-correct `_js_this` capture into a `js_global_binding_exists` global lookup on the wrong name, or into an out-of-bounds env read.

For option 3 to work correctly, the closure body's MIR emission needs to read its capture slots from the SAME source of truth as the parent's closure env init — currently they diverge when scope_env_slot is reassigned after analysis. That means either:

- Plumb the closure-only slot back into a per-closure slot table that the body emission consults, OR
- Defer slot assignment until both parent emission and child body emission can see the final slot numbers, OR
- Move closure-body emission to run AFTER `closure_only_next` (the simplest, but requires phase reordering).

Until that plumbing exists, option 3 is half-implemented in concept and reverted in code.

**Net of the option 3 attempt**: kept the diagnostic insight (which captures are block-scoped, why scope_env bloats), kept the existing safety nets, reverted the scope_env_names filter so the rest of the test suite stays at 186/187 passing.

For now the existing safety nets in `js_map_get_fast`, `js_invoke_fn`, `js_obj_typemap`, `js_class_get`/`js_class_id` convert the SIGSEGV into a deterministic `TypeError` so lib_marked fails cleanly without breaking other tests.

## 10.2. lib_marked Bisection (not yet fixed)

The `lib_marked` test crashes with `m->type` (offset 0x08 of Map struct) corrupted to `ITEM_JS_UNDEFINED` (0x1a00000000000000). The corrupted Map is the freshly-constructed `_Lexer` instance, and lookups for `'tokenizer'` / `'__instance_proto__'` fail because the corrupted type field fails `js_map_get_fast`'s sanity check.

This turn's bisection narrowed the trigger to a specific combination inside the `blockTokens` method body:
- A `while (src)` loop
- `if (token = this.tokenizer.X(src))` — assignment-in-condition reading a method off `this.tokenizer`
- `tokens.push(...)` on a function-parameter array
- A block-scoped closure: `if (this.options.extensions && ...) { let startIndex; this.options.extensions.startBlock.forEach(arrow => { ... }); }`

Removing any one element makes the bug disappear in isolation; the simplest standalone class+IIFE repro I could build does NOT trigger it, so the actual codegen interaction requires lib_marked's preamble (the helpers, regex constants, and full `_Tokenizer`/`_Lexer` class chain ahead of `blockTokens`).

Tripwires installed on `js_property_set`, `js_set_shaped_slot`, `js_create_data_property`, `js_set_slot_i`, `js_set_slot_f` do NOT fire — meaning the corrupt write happens through neither runtime helper. So the write is either:
1. A direct JIT-emitted memory store (no helper call), OR
2. A GC bug where the lex Map's slab slot was wrongly freed and reused — the conservative stack scanner missed a register-held pointer to it.

Hypothesis (2) is now most likely because all the helper tripwires are clean. Next step would be a tracing GC build (or an LLDB scripted watchpoint on Map+0x08 after lex allocation) to identify exactly when the slab slot is recycled vs the JS-side reference is still considered live.

For now the existing safety nets (`js_map_get_fast`, `js_invoke_fn`, `js_obj_typemap`, `js_class_get`/`js_class_id`) convert the SIGSEGV into a `TypeError`, keeping the test deterministic and preventing crash propagation to other tests.

## 10.7. Js58 Sparse Phases 1-3 — grouped-fixture implementation

The sparse correctness portion now lands as grouped fixtures instead of the original one-method-per-file split.

Runtime changes:
- `js_array_element` reads companion-Map DATA entries when no dense slot is present, while keeping index reads bounded by `capacity`.
- `js_create_data_property_or_throw`, direct `slice`, direct `splice`, `at`, `item`, `findLast`, and `findLastIndex` avoid dense-buffer OOB reads/writes for sparse logical indexes.
- `fill` uses the generic property-setting path; `sort` keeps the dense cleanup path unchanged for ordinary dense arrays and clears sparse/dense tail entries only for sparse-shaped arrays.
- `js_delete_property` no longer writes past `arr->capacity` when deleting sparse numeric indexes.

Fixture coverage:
- Existing grouped files cover reduce/reduceRight, find/findIndex/findLast/findLastIndex, includes, concat/flat/flatMap, slice/splice, fill/copyWithin/reverse/sort, length truncate/extend, already-robust helper-routed methods, delete, and sparse GC survival.
- Current focused gate: `test/test_js_gtest.exe --gtest_filter='*sparse*' --gtest_brief=1` runs 10 sparse JS fixtures and passes.
- Current build gate: `make build-test` passes.
- Current test262 admission gate: `make test262-baseline` passes with `Fully passed: 40261 / 40261`, `Non-fully-passing: 0`, `Failed: 0`, and `Regressions: 0`.
- Current URI guard: `test/test_js_gtest.exe --gtest_filter='*lib_joi*:*lib_ajv*' --gtest_brief=1` passes both fixtures; the URI regex failures no longer block Js58 admission.
- Current timing guard: `test/test_js_transpile_timing_gtest.exe --gtest_brief=1` passes all 7 headline corpora. The saved Js57 timing baseline file is absent, so this is a timing smoke rather than a formal per-corpus delta comparison.

## 10.8. Js58 Phase 4 — sparse-key cursor implementation

The original persistent companion-Map cache idea was not landed because `Map` size is baked into allocation size classes and JIT-visible layout. The admitted Phase 4 tranche keeps the cache local to array iteration:

- `JsArraySparseKeyCursor` builds a sorted list of numeric companion-map shape keys for an array iteration.
- `js_array_find_next_own_element_cached` and `js_array_find_prev_own_element_cached` use binary search over that key list instead of scanning every `ShapeEntry` on every helper call.
- The cursor records the companion `Map*`, `TypeMap*`, shape head, and type length; if callback code adds a new sparse property and changes the shape, the next helper call rebuilds the key list.
- Candidate presence is checked against the current companion-map slot before returning it, so deleted entries are skipped and tombstoned entries that are re-added under an existing shape become visible.
- `sparse_already_robust` now includes dynamic add/delete cases during `forEach` to lock this mutation behaviour.

Follow-up only if needed: a persistent per-companion-map cache can still be considered later, but it should be designed with the `Map` layout/JIT offset constraints in mind.

## 11. Followups After Js58

- **`gc_data_zone` block-overlap fix** (Js58.1 or its own micro-proposal) — root-cause the corruption that made P8 lower `SPARSE_GAP_MAX` to 10K. Once fixed, the threshold can rise back to 1M (or be removed entirely) without losing the sparse-path correctness Js58 added — those are independent fixes.
- **Sparse Proxy interaction** (Js59 candidate) — `new Proxy(sparseArr, traps)` invocations.
- **Performance benches for sparse paths** — currently no `test_js_transpile_timing` corpus is sparse-heavy. Add one based on real-world JSON-tree-walk workloads where `arr[id] = node` patterns create accidental sparse arrays.
