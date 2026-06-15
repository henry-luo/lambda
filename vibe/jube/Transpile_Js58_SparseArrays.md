# Transpile_Js58_SparseArrays — Robust Sparse Array Support, Perf-Preserving

Date: 2026-06-16

Status: proposal. Js57 closed the ES2024 module-evaluation work and the sparse-array `SPARSE_GAP_MAX = 10000` perf workaround in P8. The P8 workaround moved 9 partially-broken array methods from "subtly wrong but invisible" to "subtly wrong and more reachable" — any JS that does `arr[20000] = …` followed by `reduce` / `findLast` / `fill` / `sort` / `arr.length = N` now hits a half-finished sparse path. Js58 finishes the integration.

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

### Phase 4 (deferred unless profiling justifies) — sub-linear sparse-key lookup

After Phases 1-3, `js_array_find_next_own_element` still walks the companion Map's full ShapeEntry list on every call (O(s) per call, O(s²) per array iteration). If sparse arrays become a hot path in real workloads, augment the companion Map with:

- A **sorted `sparse_keys[]` array** maintained on insertion: `find_next` does `std::lower_bound` for O(log s).
- Cached `(min_sparse_idx, max_sparse_idx)` so `start > max_sparse_idx` short-circuits the Map walk entirely.

Both changes are sparse-side only — dense fast path stays byte-identical.

**Decision criterion**: land Phase 4 only if, after Phases 1-3, the `test_js_transpile_timing` corpora show >2 % regression on a sparse-heavy corpus (none of the existing corpora are sparse-heavy; this would require a new bench).

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

15 test files; each `.js` ≤ 80 lines, `.txt` ≤ 30 lines. The whole suite runs in under 1 s.

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
| Test authoring | 15 (.js+.txt pairs) | ~700 LOC total | — | 1 day |

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

## 10. Final Js58 Numbers (target)

| Metric | Js57 final | Js58 target |
|---|---:|---:|
| Baseline fully passing | 40261 | ≥ 40261 |
| Regressions | 0 | 0 |
| Failures in baseline | 0 | 0 |
| Sparse-path correctness | partial (9 broken methods + length leak) | full |
| Dense-path wall-clock | n | ±2 % of n |
| `test/js/sparse_*` test files | 0 | ≥ 15 |
| Scope line | ES2024 | ES2024 (Js58 is last ES2024 proposal) |

Js58 is the closer for ES2024 sparse semantics. After Js58 lands, Lambda's array implementation matches V8/SpiderMonkey behaviour on sparse arrays for every test262-relevant method, with no perf regression on the 99 %-dense workloads that dominate real JS.

## 11. Followups After Js58

- **`gc_data_zone` block-overlap fix** (Js58.1 or its own micro-proposal) — root-cause the corruption that made P8 lower `SPARSE_GAP_MAX` to 10K. Once fixed, the threshold can rise back to 1M (or be removed entirely) without losing the sparse-path correctness Js58 added — those are independent fixes.
- **Sparse Proxy interaction** (Js59 candidate) — `new Proxy(sparseArr, traps)` invocations.
- **Performance benches for sparse paths** — currently no `test_js_transpile_timing` corpus is sparse-heavy. Add one based on real-world JSON-tree-walk workloads where `arr[id] = node` patterns create accidental sparse arrays.
