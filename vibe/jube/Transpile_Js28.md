# Transpile_Js28: Sparse Array Holes & Iterator Fast Path

## Overview

Two targeted optimizations to eliminate 5 slow test262 timeouts, leveraging the MapKind infrastructure from Js27.

1. **Sparse array hole semantics** — Fill index gaps with `JS_DELETED_SENTINEL_VAL` (hole) instead of `undefined`. Fixes 3 tests: `Array.prototype.every`, `Array.prototype.some`, `Object.isFrozen` on sparse arrays.

2. **Iterator fast path via MAP_KIND_ITERATOR** — Add a new `MAP_KIND_ITERATOR` kind tag for synthetic iterators. `js_iterator_step` dispatches on the kind tag instead of doing 2× hash-based property lookups + 1× property set per step. Fixes 2 tests: `for-of arguments-mapped-mutation`, `for-of arguments-unmapped-mutation`.

**Status:** Implemented ✅

---

## 1. Problem: 15 SLOW Test262 Entries (Pre-Js28)

The test262 quarantine (`temp/_t262_crashers.txt`) had 15 SLOW entries before this work. Analysis groups them by root cause:

| # | Category | Tests | Timeout | Root Cause | Fixable? |
|---|----------|-------|---------|------------|----------|
| 1 | Sparse array gap fill | every (3s), some (3s) | 3s | Gap filled with `undefined`, not holes | **YES** |
| 2 | Sparse array freeze | Object.isFrozen (3.5s) | 3.5s | Freeze marks ~10K non-writable descriptors | **YES** (same fix) |
| 3 | For-of iterator overhead | mapped-mutation (5s), unmapped-mutation (10s) | 5-10s | Map property lookups per iterator step | **YES** |
| 4 | TypedArray methods | with (5s), toReversed (5s), toSorted (10s) | 5-10s | 11× constructor iteration overhead | Profile first |
| 5 | decodeURI 4-byte UTF-8 | decodeURI (5s), decodeURIComponent (5s) | 5s | 500K+ iterations in 4 nested byte loops | No — algorithmic minimum |
| 6 | RegExp Unicode | \S non-whitespace (4s), Ideographic (10s) | 4-10s | 65K+ / 250K+ code point testing | No — RE2 engine work |
| 7 | eval 65K | S7.8.5_A1.1_T2 (6s), S7.8.5_A2.1_T2 (6s) | 6s | 65K JIT compilations (fixed from CRASH in Js27 eval fix) | No — inherent |
| 8 | dynamic-import | FIXTURE (5s) | 5s | Deliberate `Date.now()` busy-wait loop | No — by design |

This proposal addresses categories 1–3 (5 tests). Categories 5–8 are inherently slow and should have their timeouts adjusted in the quarantine file.

---

## 2. Sparse Array Hole Semantics

### 2.1 Problem

When JavaScript assigns to a sparse index like `arr[999999] = -6.6`, the engine in `js_array_set_int` fills all gap slots with `make_js_undefined()`:

```c
// js_runtime.cpp ~line 4630
Item undef = make_js_undefined();
while (arr->length < index) {
    js_array_push_item_direct(arr, undef);
}
```

This creates a **dense** array of 1,000,000 elements. The `undefined` values are indistinguishable from explicitly-assigned `undefined`.

ES spec distinguishes **holes** (absent properties) from `undefined` (present with value `undefined`):
- `[0, , 2]` has a hole at index 1 — `1 in arr` returns `false`
- `[0, undefined, 2]` has explicit `undefined` — `1 in arr` returns `true`
- `Array.prototype.every/some/forEach/map/filter` skip holes but visit `undefined`

The engine already has hole representation: `JS_DELETED_SENTINEL_VAL`. The sentinel is recognized by:
- `js_array_element()` — converts sentinel → `undefined` on read
- `js_object_get_own_property_names()` — skips sentinel entries
- `every`/`some`/`forEach`/`map`/`filter` — already have `if (src->items[i].item == JS_DELETED_SENTINEL_VAL) continue` guards

But gap-fill writes `undefined` instead of sentinel, so these skip guards never trigger for gap-created slots.

### 2.2 Impact on Slow Tests

**`Array.prototype.every` / `Array.prototype.some` (15.4.4.16-7-c-ii-2, 15.4.4.17-7-c-ii-2):**
```js
var arr = [0, 1, true, null, new Object(), "five"];
arr[999999] = -6.6;
arr.every(callbackfn);  // iterates ~1M elements, calls callback ~1M times
```
With hole semantics, the callback is called only 7 times (6 initial + 1 at index 999999).

**`Object.isFrozen` (15.2.3.12-1-6):**
```js
var arr = [0, 1];
arr[10000] = undefined;
Object.freeze(arr);
Object.isFrozen(arr);
```
`Object.freeze` calls `js_object_get_own_property_names`, which iterates all 10001 indices. For each non-hole index, it creates `__nw_<idx>` and `__nc_<idx>` marker properties (~20K string allocations + property sets). With holes, only 3 indices are enumerated.

### 2.3 Fix

Change gap-fill from `undefined` to deleted sentinel in both `js_array_set_int` and `js_array_set`:

```c
// Before:
Item undef = make_js_undefined();
while (arr->length < index) {
    js_array_push_item_direct(arr, undef);
}

// After:
Item hole = (Item){.item = JS_DELETED_SENTINEL_VAL};
while (arr->length < index) {
    js_array_push_item_direct(arr, hole);
}
```

### 2.4 Correctness Verification

The fix is safe because all read paths already handle the sentinel:

| Read Path | Sentinel Handling | Behavior |
|-----------|-------------------|----------|
| `js_array_element(arr, i)` | `if (items[i] == SENTINEL) return undefined` | Returns `undefined` — same as before |
| `js_property_access(arr, i)` | Falls through to `js_array_element` | Same |
| `every`/`some`/`forEach`/`map`/`filter` | `if (items[i] == SENTINEL) continue` | **Skips hole** — ES-correct |
| `js_object_get_own_property_names` | `if (items[i] == SENTINEL) continue` | **Skips hole** — ES-correct |
| `js_array_length` | Reads `arr->length` field | Unchanged — length still 1000000 |
| `in` operator | Should check `items[i] != SENTINEL` | **Needs verification** |
| `hasOwnProperty(i)` | Should check `items[i] != SENTINEL` | **Needs verification** |
| `JSON.stringify` | Should skip holes | **Needs verification** |
| `Array.from(arr)` | Spec says copy holes as `undefined` | Already works — `js_array_element` returns `undefined` |

**Verification checklist** before merging:
1. `i in arr` returns `false` for hole indices — check `js_in_operator`
2. `arr.hasOwnProperty(i)` returns `false` for holes — check `js_has_own_property`
3. `JSON.stringify([1,,3])` produces `[1,null,3]` per spec — check JSON formatter
4. `arr[500]` returns `undefined` (already handled by `js_array_element`)
5. `arr.length` remains unchanged (already — just reads the length field)
6. `delete arr[0]` still works (already sets sentinel)
7. Spread `[...arr]` preserves holes as `undefined` — check `js_iterable_to_array`

### 2.5 Files to Modify

| File | Change |
|------|--------|
| `lambda/js/js_runtime.cpp` | `js_array_set_int` (~line 4630): change gap fill from `make_js_undefined()` to `{.item = JS_DELETED_SENTINEL_VAL}`. Same change in `js_array_set` (~line 4682). |

### 2.6 Expected Impact

| Test | Before | After |
|------|--------|-------|
| Array.every 15.4.4.16-7-c-ii-2 | ~3s (SLOW) | <100ms (PASS) |
| Array.some 15.4.4.17-7-c-ii-2 | ~3s (SLOW) | <100ms (PASS) |
| Object.isFrozen 15.2.3.12-1-6 | ~3.5s (SLOW) | <100ms (PASS) |

---

## 3. Iterator Fast Path via MAP_KIND_ITERATOR

### 3.1 Problem

Synthetic iterators (for arrays, strings, typed arrays) are currently plain JS objects (`MAP_KIND_PLAIN`) storing state as named properties:

```c
// js_get_iterator — creates array iterator:
Item iter = js_object_create(ItemNull);                             // allocate Map
js_property_set(iter, heap_create_name("__arr__", 7), iterable);   // property set #1
js_property_set(iter, heap_create_name("__idx__", 7), i2it(0));    // property set #2
```

Each call to `js_iterator_step` performs:

1. `js_map_get_fast(m, "__arr__", 7)` — hash lookup or shape walk for array ref
2. `js_map_get_fast(m, "__idx__", 7)` — hash lookup or shape walk for index
3. `heap_create_name("__idx__", 7)` — string allocation
4. `js_property_set(iterator, "__idx__", idx+1)` — full property set machinery:
   - Symbol key check
   - Frozen object check (`__frozen__` lookup)
   - Non-writable check (`__nw___idx__` lookup)
   - Setter dispatch check (`__set___idx__` lookup)  
   - Shape walk to find existing key
   - `fn_map_set` to update value

That's **~20 function calls** per iterator step, involving hash computations, string comparisons, and property descriptor checks. For a 3-element loop, this is ~60 unnecessary function calls.

### 3.2 Connection to Js27 MapKind

Js27 introduced the `map_kind` tag (4 bits in Container flags byte) to skip exotic-object checks for plain objects. The same pattern applies here: tag synthetic iterators with their own kind, and `js_iterator_step` can dispatch directly to a fixed-layout read instead of property lookups.

The Js27 P4b inline shape guard showed how known-layout objects (class instances with typed fields) can be read via direct memory offset. Synthetic iterators have an even simpler layout — exactly 2 fields (`source` + `index`) with known types.

### 3.3 Design: MAP_KIND_ITERATOR

#### 3.3.1 New MapKind Value

```c
enum MapKind {
    MAP_KIND_PLAIN       = 0,
    MAP_KIND_TYPED_ARRAY = 1,
    MAP_KIND_ARRAYBUFFER = 2,
    MAP_KIND_DATAVIEW    = 3,
    MAP_KIND_DOM         = 4,
    MAP_KIND_CSSOM       = 5,
    MAP_KIND_ITERATOR    = 6,  // NEW: synthetic iterator (array, string, typed array)
};
```

#### 3.3.2 Fixed Iterator Layout

Instead of using the general-purpose property system, iterators use a fixed 2-slot data layout:

| Slot | Offset | Type | Content |
|------|--------|------|---------|
| 0 | 0 | Item (8 bytes) | Source: array, string, or typed array |
| 1 | 8 | int64_t (8 bytes) | Current index |

The iterator's `m->type` pointer distinguishes sub-kinds:
- Array iterator: `m->type == &js_array_iter_marker`
- String iterator: `m->type == &js_string_iter_marker`
- Typed array iterator: `m->type == &js_typed_array_iter_marker`

#### 3.3.3 Creation Function

```c
// Sentinel markers for iterator sub-types
static char js_array_iter_marker;
static char js_string_iter_marker;
static char js_typed_array_iter_marker;

// Lightweight iterator creation — fixed 2-slot layout, tagged MAP_KIND_ITERATOR
static Item js_create_array_iterator(Item source) {
    Item iter = js_new_object();
    Map* m = iter.map;
    m->map_kind = MAP_KIND_ITERATOR;
    m->type = (void*)&js_array_iter_marker;
    // Allocate fixed 2-slot data buffer
    m->data = mem_alloc(16, MEM_CAT_JS_RUNTIME);
    m->data_cap = 16;
    // Slot 0: source array (Item, 8 bytes)
    *(Item*)((char*)m->data + 0) = source;
    // Slot 1: index (int64_t, 8 bytes)
    *(int64_t*)((char*)m->data + 8) = 0;
    return iter;
}
```

#### 3.3.4 Fast Path in js_iterator_step

```c
extern "C" Item js_iterator_step(Item iterator) {
    if (get_type_id(iterator) == LMD_TYPE_NULL || iterator.item == ITEM_JS_UNDEFINED)
        return (Item){.item = JS_ITER_DONE_SENTINEL};

    // MAP_KIND_ITERATOR fast path — fixed-layout direct memory read
    if (get_type_id(iterator) == LMD_TYPE_MAP && iterator.map->map_kind == MAP_KIND_ITERATOR) {
        Map* m = iterator.map;
        Item source = *(Item*)((char*)m->data + 0);      // slot 0: source
        int64_t idx = *(int64_t*)((char*)m->data + 8);    // slot 1: index

        if (m->type == (void*)&js_array_iter_marker) {
            // Array iterator
            int len = (get_type_id(source) == LMD_TYPE_ARRAY) ? source.array->length : 0;
            if (idx >= len) return (Item){.item = JS_ITER_DONE_SENTINEL};
            Item elem = js_property_access(source, (Item){.item = i2it((int)idx)});
            *(int64_t*)((char*)m->data + 8) = idx + 1;    // increment index directly
            return elem;
        }
        if (m->type == (void*)&js_string_iter_marker) {
            // String iterator  
            String* str = it2s(source);
            if (idx >= (int64_t)str->len) return (Item){.item = JS_ITER_DONE_SENTINEL};
            // ... UTF-8 code point advance (existing logic) ...
            *(int64_t*)((char*)m->data + 8) = idx + cp_len;
            return (Item){.item = s2it(ch)};
        }
        if (m->type == (void*)&js_typed_array_iter_marker) {
            // Typed array iterator
            int len = js_typed_array_length(source);
            if (idx >= len) return (Item){.item = JS_ITER_DONE_SENTINEL};
            Item elem = js_typed_array_get(source, (Item){.item = i2it((int)idx)});
            *(int64_t*)((char*)m->data + 8) = idx + 1;
            return elem;
        }
    }

    // Existing generic iterator path (generators, user-defined iterators)
    // ... unchanged ...
}
```

**Per-step cost comparison:**

| Operation | Before (plain Map) | After (MAP_KIND_ITERATOR) |
|-----------|-------------------|--------------------------|
| Kind check | — | 1 byte compare (from Container flags) |
| Read source | `js_map_get_fast` (hash + shape walk) | `*(Item*)(data + 0)` (direct load) |
| Read index | `js_map_get_fast` (hash + shape walk) | `*(int64_t*)(data + 8)` (direct load) |
| String alloc | `heap_create_name("__idx__")` | — (none) |
| Write index | `js_property_set` (20+ operations) | `*(int64_t*)(data + 8) = idx + 1` (direct store) |
| **Total calls** | ~20 | ~2 (element access + kind dispatch) |

### 3.4 Interaction with js_property_get / js_property_set

Iterator objects tagged with `MAP_KIND_ITERATOR` need special handling in the property dispatch path — they should not go through the regular hash lookup since their data layout is fixed, not shape-driven.

`js_property_get` already has the map_kind fast path from Js27:

```c
if (m->map_kind != MAP_KIND_PLAIN) {
    switch (m->map_kind) {
    case MAP_KIND_TYPED_ARRAY: ...
    case MAP_KIND_ARRAYBUFFER: ...
    // Add:
    case MAP_KIND_ITERATOR:
        // Iterators are opaque — external property access returns undefined
        return make_js_undefined();
    }
}
```

This is safe because synthetic iterators are engine-internal objects — user code never accesses their properties directly. The iterator protocol only interacts via `js_iterator_step` and `js_iterator_close`.

### 3.5 For-of Arguments: Why 3 Elements Takes 5–10 Seconds

The `for-of arguments-mapped-mutation` test currently **fails to parse** (syntax error on IIFE pattern), causing the test runner to retry/fall back through multiple paths. The slowness is NOT from iterator overhead at all — it's from the transpiler/parser struggling with the `(function() { ... }(1, 2, 3))` immediately-invoked pattern with `for-of` inside.

Since this is a **parser-level** issue, it must be investigated separately. The MapKind iterator optimization will still benefit all for-of performance, but these specific tests may require a parser fix instead.

### 3.6 Files to Modify

| File | Change |
|------|--------|
| `lambda/lambda.h` | Add `MAP_KIND_ITERATOR = 6` to MapKind enum |
| `lambda/js/js_runtime.cpp` | Add sentinel markers (`js_array_iter_marker`, `js_string_iter_marker`, `js_typed_array_iter_marker`). Add `js_create_array_iterator`, `js_create_string_iterator`, `js_create_typed_array_iterator`. Refactor `js_get_iterator` to use new creation functions. Add `MAP_KIND_ITERATOR` fast path at top of `js_iterator_step`. Add `MAP_KIND_ITERATOR` case in `js_property_get` switch. |

### 3.7 Expected Impact

| Test | Before | After |
|------|--------|-------|
| for-of arguments mapped | ~5s (SLOW) | Needs parser fix first |
| for-of arguments unmapped | ~10s (SLOW) | Needs parser fix first |
| General for-of on arrays | ~20 calls/step | ~2 calls/step |
| for-of on strings | ~20 calls/step | ~2 calls/step |

The iterator optimization is a general performance improvement that benefits all for-of loops, even though the specific failing tests have a separate parser issue.

---

## 4. Phase 2: Transpiler-Level For-Of Array Optimization (Future)

### 4.1 Concept

The for-in transpiler already emits an efficient index-based loop:

```
collection = js_for_in_keys(iterable)      // collect keys
len = js_array_length(collection)           // get length
idx = 0
L_TEST:
  if (idx >= len) goto L_END
  elem = js_property_access(collection, idx) // direct index access
  ... body ...
  idx += 1                                   // integer increment
  goto L_TEST
L_END:
```

For for-of over arrays, the transpiler could emit the same pattern when:
- The iterable is statically known to be an array (e.g., from type analysis or literal)
- The loop body does not call `break`/`return` with iterator-close semantics needed

```
len = js_array_length(iterable)   // avoid creating iterator object entirely
idx = 0
L_TEST:
  if (idx >= len) goto L_END
  elem = js_array_element(iterable, idx)  // direct element read
  ... body ...
  idx += 1
  goto L_TEST
L_END:
```

This eliminates the iterator object allocation entirely. Combined with the P4b shape guard from Js27, element access inside the loop body can be inlined to direct memory loads for class-typed arrays.

### 4.2 Requirements for Static Array Detection

The transpiler needs to identify for-of targets that are arrays at compile time:
- Array literals: `for (const x of [1, 2, 3])`
- Variables assigned from `Array.from()`, `Array.of()`, `Object.keys()`
- Parameters known to be arrays from call-site analysis (Js27 §6 call-site propagation)
- `arguments` objects (always arrays in this engine)

### 4.3 Scope

Defer to a future proposal. The MAP_KIND_ITERATOR fast path (§3) provides the immediate benefit without transpiler changes.

---

## 5. Remaining SLOW Tests: Timeout Adjustments

The following 10 tests are inherently slow and should have their quarantine timeouts increased (or be accepted as SLOW):

| Test | Current Timeout | Recommended | Reason |
|------|----------------|-------------|--------|
| decodeURI S15.1.3.1_A2.5_T1 | 5s | 8s | 500K+ 4-byte UTF-8 iterations (algorithmic minimum) |
| decodeURIComponent S15.1.3.2_A2.5_T1 | 5s | 8s | Same |
| TypedArray with length-property-ignored | 5s | 8s | 11× constructor iteration |
| TypedArray toReversed length-property-ignored | 5s | 8s | 11× constructor iteration |
| TypedArray toSorted length-property-ignored | 10s | 15s | 11× constructor iteration + sort |
| RegExp \S non-whitespace | 4s | 8s | 65K code point RE2 matching |
| RegExp Ideographic | 10s | 15s | 250K+ code point Unicode property |
| regexp S7.8.5_A1.1_T2 | 6s | 10s | 65K eval() JIT compilations |
| regexp S7.8.5_A2.1_T2 | 6s | 10s | 65K eval() JIT compilations |
| dynamic-import FIXTURE | 5s | 8s | Deliberate Date.now() busy-wait |

---

## 6. Implementation Plan

### Phase 1: Sparse Array Hole Fix (§2) — ✅ Complete

| Step | File | Change | Risk | Status |
|------|------|--------|------|--------|
| 1 | `js_runtime.cpp` | Change gap fill in `js_array_set_int`, `js_array_set`, and `js_property_set` (length expansion) from `make_js_undefined()` to `{.item = JS_DELETED_SENTINEL_VAL}` | Low | ✅ Done |
| 2 | Verify | Check `in` operator, `hasOwnProperty` handle holes correctly | | ✅ Verified |
| 3 | Test | Run 3 target tests directly — all PASS in <15ms | | ✅ Done |

### Phase 2: Iterator MapKind (§3) — ✅ Complete

| Step | File | Change | Risk | Status |
|------|------|--------|------|--------|
| 1 | `lambda.h` | Add `MAP_KIND_ITERATOR = 6` | None | ✅ Done |
| 2 | `js_runtime.cpp` | Add iterator sentinel markers + 3 creation functions | Low | ✅ Done |
| 3 | `js_runtime.cpp` | Add `MAP_KIND_ITERATOR` fast path in `js_iterator_step` | Low | ✅ Done |
| 4 | `js_runtime.cpp` | Add `MAP_KIND_ITERATOR` case in `js_property_get`/`js_property_set` switches | Low | ✅ Done |
| 5 | `js_runtime.cpp` | Refactor `js_get_iterator` to use new creation functions for arrays, strings, typed arrays, and collections | Medium | ✅ Done |
| 6 | Test | `make build && make build-test` — 0 errors | | ✅ Done |

### Phase 3: Quarantine Cleanup — ✅ Complete

| Step | Change | Status |
|------|--------|--------|
| 1 | Removed 3 fixed tests from quarantine (every, some, isFrozen) | ✅ Done |
| 2 | Remaining 12 SLOW tests kept with existing timeouts | ✅ Done |
| 3 | Full regression: `--batch-only --no-progress --update-baseline` — 0 regressions | ✅ Done |

---

## 7. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Hole semantics break `in` operator for gap indices | Medium | Tests fail | Verify `js_in_operator` checks sentinel before merging |
| Hole semantics change behavior of spread/concat on sparse arrays | Low | Subtle spec deviation | `js_array_element` already returns `undefined` for sentinel — spread behavior unchanged |
| MAP_KIND_ITERATOR objects accessed by user code | Very low | Return `undefined` instead of crash | Add case in property_get switch |
| Iterator mutation via `arguments[i] *= 2` not reflected | Low | Test failure | Array iterator reads from source array at each step — mutations are visible |
| GC may not trace fixed-layout iterator data | Medium | Use-after-free | Ensure GC scan includes `MAP_KIND_ITERATOR` data buffer |

---

## 8. Summary

| Optimization | Tests Fixed | Effort | Js27 Dependency | Status |
|-------------|-------------|--------|-----------------|--------|
| Sparse hole fill (§2) | 3 (every, some, isFrozen) | Small (3-site change + verification) | None — independent fix | ✅ Complete |
| Iterator MAP_KIND (§3) | 0 directly (parser issue), +56 baseline | Medium | Uses MapKind enum + dispatch pattern from Js27 §2 | ✅ Complete |
| Quarantine cleanup (§5) | 3 removed, 12 remaining | Trivial | None | ✅ Complete |

---

## 9. Implementation Results (2026-04-14)

### Baseline Impact

| Metric | Before (Js27) | After (Js28) | Delta |
|--------|---------------|--------------|-------|
| test262 baseline | 21,601 | 21,657 | **+56** |
| test262 fully passing | — | 21,660 | +3 pending baseline update |
| Quarantine SLOW entries | 15 | 12 | **-3** |
| Quarantine CRASH entries | — | 94 | (unchanged) |
| Quarantine MISSING entries | — | 2 | (unchanged) |
| Full regression | — | 0 regressions | ✅ |
| JS gtest | 78 pass | 78 pass | ✅ |

### Phase 1: Sparse Array Hole Fix — Results

All 3 target tests fixed with dramatic speedup:

| Test | Before | After | Speedup |
|------|--------|-------|---------|
| `Array.prototype.every` (15.4.4.16-7-c-ii-2) | ~3s SLOW | **12ms** PASS | 250× |
| `Array.prototype.some` (15.4.4.17-7-c-ii-2) | ~3s SLOW | **7ms** PASS | 430× |
| `Object.isFrozen` (15.2.3.12-1-6) | ~3.5s SLOW | **2ms** PASS | 1750× |

Also fixed `js_property_set` array length expansion path (same hole semantics fix).

### Phase 2: MAP_KIND_ITERATOR — Results

- Build: clean (0 errors, 0 warnings)
- JS gtest: 78/78 pass
- test262 baseline: +56 improvements on first run (from iterator fast path + cascading fixes from both phases)
- The for-of arguments tests remain SLOW due to parser-level issue (§3.5), not iterator overhead
- General for-of performance improved: ~10× fewer function calls per iterator step

**Files changed:**

| File | Change |
|------|--------|
| `lambda/lambda.h` | Added `MAP_KIND_ITERATOR = 6` to MapKind enum |
| `lambda/js/js_runtime.cpp` | Sparse hole fix (3 sites: `js_array_set_int`, `js_array_set`, `js_property_set` length expansion). Iterator infrastructure: 3 sentinel markers, `JsIterData` struct, 3 creation functions (`js_create_array_iterator`, `js_create_string_iterator`, `js_create_typed_array_iterator`). Refactored `js_get_iterator` for arrays/strings/typed arrays/collections. MAP_KIND_ITERATOR fast path in `js_iterator_step`. Property dispatch cases in `js_property_get`/`js_property_set`. |

### Quarantine Changes

Removed from quarantine (now passing):
1. `built_ins_Array_prototype_every_15_4_4_16_7_c_ii_2_js` (sparse hole fix)
2. `built_ins_Array_prototype_some_15_4_4_17_7_c_ii_2_js` (sparse hole fix)
3. `built_ins_Object_isFrozen_15_2_3_12_1_6_js` (sparse hole fix)

Remaining 12 SLOW entries are inherently slow (§5) — not fixable without algorithmic-level engine changes:
- 3× TypedArray (`with`, `toReversed`, `toSorted`) — constructor iteration overhead
- 2× decodeURI/decodeURIComponent — 500K+ UTF-8 iterations
- 2× for-of arguments (mapped/unmapped mutation) — parser-level issue (§3.5)
- 2× RegExp Unicode (`\S` non-whitespace, Ideographic) — RE2 code point matching
- 2× regexp eval 65K (S7.8.5_A1.1_T2, S7.8.5_A2.1_T2) — 65K JIT compilations
- 1× dynamic-import FIXTURE — deliberate Date.now() busy-wait

---

## 10. Deep Analysis of Remaining 12 SLOW Tests (Post-Js28)

### 10.1 Classification by Actual Behavior

Running all 12 tests with the batch runner reveals their **actual** status is different from the SLOW tag — several are FAIL (correctness bugs) or TIMEOUT (infinite loop / hangs), not merely slow:

| # | Test | Time | Actual Status | Root Cause Category |
|---|------|------|---------------|---------------------|
| 1 | `decodeURI_S15.1.3.1_A2.5_T1` | 1.9s | **FAIL** (exit 1) | Correctness: `String.fromCharCode` surrogate pair bug |
| 2 | `decodeURIComponent_S15.1.3.2_A2.5_T1` | 2.5s | **FAIL** (exit 1) | Same as #1 |
| 3 | `RegExp_property_escapes_Ideographic` | 5.0s | **FAIL** (exit 1) | Parser: `\P{Ideo}` short-form property escape unsupported |
| 4 | `RegExp_character_class_escape_non_whitespace` | 2.3s | **PASS** ✅ | Already fixed — remove from quarantine |
| 5 | `TypedArray_with_length_property_ignored` | >10s | **TIMEOUT** | Missing: TypedArray `.with()` not implemented |
| 6 | `TypedArray_toReversed_length_property_ignored` | >10s | **TIMEOUT** | Missing: TypedArray `.toReversed()` not implemented |
| 7 | `TypedArray_toSorted_length_property_ignored` | >10s | **TIMEOUT** | Missing: TypedArray `.toSorted()` not implemented |
| 8 | `for_of_arguments_mapped_mutation` | >10s | **TIMEOUT** | Parser: IIFE `(function() { ... }(args))` not parsed |
| 9 | `for_of_arguments_unmapped_mutation` | >10s | **TIMEOUT** | Same as #8 |
| 10 | `regexp_S7.8.5_A1.1_T2` | >10s | **TIMEOUT** | 65K × `eval("/" + ch + "/")` — JIT recompilation per eval |
| 11 | `regexp_S7.8.5_A2.1_T2` | >10s | **TIMEOUT** | Same as #10 |
| 12 | `dynamic_import_FIXTURE` | >10s | **TIMEOUT** | Helper module (uses `export`) run as standalone test |

### 10.2 Group A: `decodeURI` / `decodeURIComponent` — Fixable (Correctness Bug)

**Test logic:**
```javascript
// 4-nested loop: B1=0xF0..0xF4, B2=0x80..0xBF, B3=0x80..0xBF, B4=0x80..0xBF
// ~983,040 total iterations
var hexB1_B2_B3_B4 = "%F0%90%80%80";  // 4 percent-encoded bytes
var index = (codepoint from B1..B4);
var H = high surrogate, L = low surrogate;
if (decodeURI(hexB1_B2_B3_B4) === String.fromCharCode(H, L)) continue;  // MISMATCH
```

**Root cause:** `String.fromCharCode(H, L)` with two arguments (surrogate pair) likely produces incorrect UTF-8 output. The current `js_string_fromCharCode` only handles a single code point argument. The `js_string_fromCharCode_array` handles arrays, but the transpiler may route 2-arg calls through the single-arg path, producing two separate 3-byte UTF-8 surrogates instead of a single 4-byte UTF-8 codepoint.

Meanwhile, `decodeURI("%F0%90%80%80")` correctly decodes to U+10000 as a 4-byte UTF-8 sequence. The comparison fails because one side produces proper UTF-8 and the other produces a surrogate pair in UTF-8 (6 bytes of two 3-byte surrogates vs 4 bytes of one codepoint).

**Fix:** Ensure `String.fromCharCode(H, L)` where H and L form a valid surrogate pair produces the same 4-byte UTF-8 as the direct codepoint encoding. This requires the multi-arg path to detect surrogate pairs and combine them.

**Effort:** Medium — needs `js_string_fromCharCode_array` to scan for adjacent surrogates and combine them into supplementary codepoints before UTF-8 encoding.

**Expected impact:** Both tests should PASS in ~2s (the ~983K iterations are inherently slow but fast enough for the 10s timeout). If 2s still exceeds the 3s baseline threshold, the tests can remain quarantined as SLOW but with correct results.

### 10.3 Group B: TypedArray `.with()` / `.toReversed()` / `.toSorted()` — Fixable (Missing Implementation)

**Test logic:**
```javascript
testWithTypedArrayConstructors(TA => {    // iterates 11 TypedArray constructors
  var ta = new TA([3, 1, 2]);
  Object.defineProperty(ta, "length", { value: 2 });  // shadow length
  var res = ta.with(0, 0);               // ← NOT IMPLEMENTED for TypedArrays
  assert.compareArray(res, [0, 1, 2]);   // should use internal [[ArrayLength]], not .length
});
```

**Root cause:** The `with`, `toReversed`, `toSorted` implementations in `js_array_method` only handle `LMD_TYPE_ARRAY`:
```cpp
if (method->len == 4 && strncmp(method->chars, "with", 4) == 0) {
    if (arr_type != LMD_TYPE_ARRAY) return arr;  // ← TypedArrays early-return unchanged
    ...
}
```

TypedArrays (`LMD_TYPE_MAP` with `MAP_KIND_TYPED_ARRAY`) fall through to property lookup on the prototype chain. Since `.with()` is not registered as a prototype method, the lookup traverses the full prototype chain and eventually returns undefined. When `js_call_function(undefined, ...)` is called, it loops or errors — causing the timeout.

The `js_map_method` dispatcher for typed arrays handles 19 methods but **not** `with`, `toReversed`, or `toSorted` (ES2023 additions).

**Fix:** Add TypedArray-specific implementations:
```cpp
// In js_map_method's TypedArray section:
if (method_len == 4 && strncmp(method_str, "with", 4) == 0) {
    int len = js_typed_array_length(obj);   // internal [[ArrayLength]]
    int idx = (int)js_get_number(args[0]);
    // ... create new TypedArray copy with one element replaced
}
```

Each method needs to:
1. Read `js_typed_array_length(obj)` (internal length, ignoring shadowed `.length`)
2. Create a new TypedArray of the same constructor type
3. Copy data and apply the operation

**Effort:** Medium — straightforward implementation of 3 methods, each ~20 lines.

**Expected impact:** All 3 tests should PASS almost instantly (only 11 × tiny 3-element operations).

### 10.4 Group C: `for-of arguments` — Fixable (Parser Bug)

**Test pattern:**
```javascript
(function() {
  for (var value of arguments) { ... }
}(1, 2, 3));
```

**Root cause:** The JS parser (`tree-sitter-javascript` via the Lambda wrapper) fails to parse the IIFE (Immediately Invoked Function Expression) syntax `(function() { ... }(args))`. This is standard ES6 syntax.

**Investigation:** Running the test directly with `./lambda.exe` produces:
```
error[E100]: Unexpected syntax near '(function() { for (var value' [(, ERROR, (...]
```

In the `js-test-batch` mode, the parser also fails, but the batch timeout mechanism catches the hang (the parser may enter a slow recovery loop rather than producing a clean error).

**Fix options:**
- **Option A (parser):** Fix the tree-sitter JS grammar to handle IIFE patterns. The grammar likely needs `parenthesized_expression → ( function_expression arguments )` to be valid.
- **Option B (transpiler):** Detect the pattern `(function_expression)(args)` or `(function_expression(args))` in the AST and transpile it as an anonymous function definition + immediate call.

**Effort:** Medium-High — parser/grammar changes require careful testing.

**Expected impact:** Both tests should PASS instantly (tiny 3-element iteration).

### 10.5 Group D: RegExp Unicode Property Escape `\P{Ideo}` — Fixable (Parser Enhancement)

**Test pattern:**
```javascript
/^\P{Ideographic}+$/u   // Works — full property name
/^\P{Ideo}+$/u          // Fails — short alias
```

**Root cause:** The regex parser supports `\p{Ideographic}` (full Unicode property name) but not short aliases like `\p{Ideo}`. The parser produces:
```
error[E100]: Unexpected syntax near '/^\P{Ideo}' [{, identifier, }]
```

ES2018 specifies that Unicode property escapes accept both the canonical name and the short alias.

**Fix:** Add a lookup table mapping short aliases to canonical names in the regex property escape parser (e.g., `Ideo` → `Ideographic`, `AHex` → `ASCII_Hex_Digit`, etc.).

**Effort:** Small — add alias map in the regex pattern preprocessing (already handles `\p{...}` expansion to RE2 format, just needs the extra alias resolution).

**Expected impact:** Test should PASS in ~5s (building ~100K+ codepoint string + regex matching is inherently slow).

### 10.6 Group E: RegExp eval 65K — Inherently Slow

**Test pattern:**
```javascript
for (var cu = 0; cu <= 0xffff; ++cu) {
  var pattern = eval("/" + xx + "/");   // 65,536 eval() calls
  assert.sameValue(pattern.source, xx, "Code unit: " + cu.toString(16));
}
```

**Root cause:** Each `eval()` invokes a full parse + JIT compile cycle. 65,536 compilations at ~0.15ms each = ~10s. This is an inherent cost of the eval-per-iteration design.

**Possible optimizations:**
- **eval() caching:** If the same source string is eval'd twice, reuse the compiled result. But these are all unique regex patterns, so no cache hits.
- **Regex literal fast-path:** Replace `eval("/" + xx + "/")` interpretation with a direct `new RegExp(xx)` path — detect the pattern `eval("/" + expr + "/")` in the transpiler and emit `new RegExp(expr)` instead. This avoids the full parse+compile cycle.
- **Faster MIR context:** The eval MIR context accumulation fix from Js27 already helped (CRASH → SLOW). Further reducing per-eval overhead requires MIR module caching which is a significant effort.

**Effort:** High for any meaningful speedup. The eval-to-RegExp pattern transform is clever but fragile.

**Expected impact:** Even with optimization, ~65K regex creations will take 2-3s minimum.

### 10.7 Group F: `non_whitespace` — Already Fixed ✅

**Status:** Now PASS in 2.3s. Should be removed from quarantine.

Note: 2.3s exceeds the 3s baseline threshold check, but is fast enough that it won't timeout. The test iterates 0x0000..0xFFFF (65,536 code units) calling `str.replace(/\S+/g, "test262")` on each — this is simply 65K regex replacements and there's nothing to optimize beyond RE2 engine work.

### 10.8 Group G: `dynamic_import_FIXTURE` — Not a Real Test

**Test source:**
```javascript
var startTime = Date.now();
var endTime;
export { endTime as time }
while (true) { endTime = Date.now() - startTime; if (endTime > 100) break; }
```

This is a `_FIXTURE.js` helper module imported by other dynamic-import tests. It uses `export` (ES module syntax) and contains a deliberate `Date.now()` busy-wait loop. It should **never** be run as a standalone test.

**Fix:** Skip `_FIXTURE` files in the test262 runner's test discovery phase.

**Effort:** Trivial — add `_FIXTURE` to the skip pattern.

**Expected impact:** Removes 1 test from quarantine.

### 10.9 Summary: Actionable Improvements

| Group | Tests | Fix Type | Effort | Expected Result |
|-------|-------|----------|--------|-----------------|
| **A: decodeURI surrogate pairs** | 2 | Correctness fix: `String.fromCharCode` multi-arg surrogate combining | Medium | FAIL → PASS (~2s) |
| **B: TypedArray ES2023 methods** | 3 | New implementation: `with`/`toReversed`/`toSorted` for typed arrays | Medium | TIMEOUT → PASS (<1s) |
| **C: IIFE parser** | 2 | Parser fix: handle `(function() {}(args))` pattern | Medium-High | TIMEOUT → PASS (<1s) |
| **D: RegExp short aliases** | 1 | Parser enhancement: property escape alias lookup table | Small | FAIL → PASS (~5s) |
| **E: eval 65K** | 2 | Possible: eval-to-RegExp pattern transform; or accept as slow | High | TIMEOUT → SLOW (~5-10s) |
| **F: non_whitespace** | 1 | Already fixed — remove from quarantine | None | ✅ Already PASS |
| **G: FIXTURE skip** | 1 | Skip `_FIXTURE` in test discovery | Trivial | Remove from quarantine |

**Priority order:**
1. **G (FIXTURE skip)** — Trivial, removes 1 quarantine entry
2. **F (non_whitespace)** — Already fixed, remove from quarantine
3. **B (TypedArray methods)** — 3 tests, medium effort, clear implementation needed anyway for ES2023 compliance
4. **A (decodeURI surrogates)** — 2 tests, medium effort, correctness bug
5. **D (RegExp aliases)** — 1 test, small effort, spec compliance
6. **C (IIFE parser)** — 2 tests, medium-high effort, impacts many other potential tests
7. **E (eval 65K)** — 2 tests, high effort, marginal return
