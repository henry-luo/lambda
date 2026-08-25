# Lambda Script Issues — JS Typed Array / ArrayNum Unification (2026-07-10)

Findings from a review of the "unify JS typed array with lambda" migration
(commit series `c37dde6d8`…`dbcd9416c`, landed 2026-06-30). Continues the
numbering from `Lambda_Issues6.md` (last issue #37).

**Review verdict first:** the migration itself is sound. `JsTypedArray`
(`lambda/js/js_typed_array.h:55`) holds an `ArrayNum* view` created by
`array_num_new_external_view` (`lambda/lambda-data-runtime.cpp:184`) over the
`JsArrayBuffer` bytes; length/byteOffset are *derived* from the view + its
`ArrayNumShape` (no duplicated state to drift); there are exactly two
`JsTypedArray` allocation sites and both wire the view; every entry point calls
`js_typed_array_refresh_arraynum_view` before access; element and bulk ops
route through `array_num_get_number_value` / `array_num_set_int64_value` /
`array_num_set_double_value` / `array_num_copy_*_bytes`; JS semantics live in
the shared core (e.g. `ELEM_UINT8_CLAMPED` round-half-even in
`lambda-data-runtime.cpp:851`); GC traces both `buffer_item` and `view` in
`js_native_map_gc_trace` (`lambda/lambda-mem.cpp:76`). Verified empirically:
a smoke script covering clamped rounding, Int32 wrapping, BigInt64 lanes,
shared subarrays, resizable buffers, resize-in-loop, Float16 overflow, and Node
Buffer produced spec-correct output, and all typed-array goldens under
`test/js/` pass.

Deliberate carve-outs that are **not** issues (raw access over the *same*
storage, each with a rationale): Float16 stores raw binary16 bits, BigInt lanes
egress to BigInt objects, DataView does endian-aware byte access, Node `Buffer`
(`is_buffer`) takes the raw byte path, and the MIR JIT inline fast path emits
direct loads/stores using the same element-size table.

The three gaps below are what remains.

---

## 38. P4h loop hoisting: typed-array data pointer goes stale after in-loop `resize()`

**Severity: MEDIUM (latent use-after-free; narrow trigger)** — status: FIXED
(2026-07-14).

**Implemented fix:** `jm_scan_subscript_arrays` now treats every call/new
expression in a loop as an invalidation point for all arrays considered by the
P4h metadata hoist. Arrays found before the call are marked unsafe immediately;
arrays found later inherit the scan-wide call flag. This is deliberately
conservative because an arbitrary call can resize, transfer, or detach a
backing buffer, including through an alias or callback. Such loops use the
existing non-hoisted access path, which reloads the live pointer and length.

Regression coverage:
- `test/js/regression_js_arraynum_loop_resize_hoist.js` covers both `for` and
  `while`, growing a two-byte length-tracking `Uint8Array` backing buffer to
  1 MiB inside the loop before accessing newly valid indices.
- `JavaScriptRegression.ArrayNumLoopResizeInvalidatesHoist` passes in
  `test/test_js_gtest.exe`; `make build` and `make build-test` also pass.

The P4h optimization snapshots a typed array's data pointer and length into
registers *before* `while`/`for` loops
(`lambda/js/js_mir_statement_lowering.cpp:1645`, same pattern ~`:1928`):

```c
// Note: this snapshots the data ptr + length once before the loop, so a
// resize that happens INSIDE the loop is not reflected here; a future
// phase can introduce per-iteration reload when needed.
MIR_reg_t h_len  = jm_call_1(mt, "js_typed_array_length", ...);
MIR_reg_t h_data = jm_call_1(mt, "js_typed_array_current_data_ptr", ...);
```

The safety gate is `jm_scan_subscript_arrays`
(`lambda/js/js_mir_function_collection_class_inference.cpp:1307`), which marks
an array "unsafe" (not hoisted) **only when the array variable itself is
reassigned** in the loop. It does *not* invalidate on calls that can
reallocate the backing store, so this pattern hoists:

```js
let rab = new ArrayBuffer(4, { maxByteLength: 4096 });
let ta = new Uint8Array(rab);          // length-tracking view
for (let i = 0; i < 4; i++) {
    ta[i] = i + 1;
    if (i === 1) rab.resize(2048);     // realloc may move ab->data
}
```

`js_arraybuffer_resize` allocates a new block and copies
(`lambda/js/js_typed_array.cpp:1661` area), so after the resize the hoisted
`h_data` can point at freed memory → subsequent `ta[i]` in the loop is a
use-after-free read/write in JIT'd code. Two consequences of the same
snapshot: the **stale length** is a semantic bug (length-tracking view doesn't
see growth mid-loop); the **stale pointer** is the memory-safety bug.

A smoke run of exactly this shape produced correct results, but that only
means the realloc didn't move (small block) — it is not evidence of safety.

**Fix directions** (pick one):
- Extend the unsafe-scan: any call expression in the loop body (or at minimum
  any `.resize(`/`.transfer(` member call, or any call at all when the hoisted
  array's buffer is resizable) marks all hoisted arrays unsafe. Cheap,
  conservative, matches the existing scan's architecture.
- Or per-iteration reload of `h_data`/`h_len` at the loop head when the view is
  over a resizable buffer (the "future phase" the comment anticipates).

Note the non-hoisted fallback path is already correct — it reloads via
`js_typed_array_current_data_ptr` per access (Js54 P3), which handles resize,
realloc, detach, and OOB. Only the hoisted path can go stale.

## 39. Raw fallback duplicates the entire element-access switch; mismatch fails silently to `0.0`

**Severity: LOW (maintenance hazard, no known miscompute today)** — status: OPEN.

The ArrayNum accessor path is guarded by
`js_typed_array_arraynum_view_matches` (`lambda/js/js_typed_array.cpp:477`),
and when the guard fails, helpers fall back to a **full parallel raw
implementation**: `js_typed_array_raw_get_item`
(`lambda/js/js_typed_array.cpp:2346`) re-implements the whole 11-lane element
switch that `array_num_get_number_value` already implements. Two problems:

1. **Shadow implementation.** The raw switch is load-bearing for the deliberate
   carve-outs (Buffer/`is_buffer`, BigInt lanes, Float16), but for the numeric
   lanes it is a byte-for-byte duplicate of the core. Any future change to a
   lane's semantics (e.g. a new elem type, a conversion tweak) must now be made
   in two places, and nothing enforces they stay in sync.
2. **Silent-zero fallback.** `js_typed_array_raw_load_number`
   (`lambda/js/js_typed_array.cpp:518`) returns `0.0` when the view doesn't
   match — i.e. if a future entry point forgets to call
   `js_typed_array_refresh_arraynum_view` first, reads don't crash or log,
   they quietly produce zeros. Correct today only because every current entry
   point maintains the refresh discipline.

**Fix directions:**
- Shrink the raw switch to exactly the carve-out lanes (is_buffer bytes,
  BigInt, Float16) and route everything else through the view unconditionally
  (refresh inside the helper instead of relying on callers).
- Replace the silent `return 0.0` with `log_error` + refresh-and-retry, so a
  missed refresh is loud in `log.txt` instead of a wrong-answer bug.

## 40. Unification is one-way: Lambda-native `ArrayNum` items are opaque to JS

**Severity: LOW today (no current flow); becomes MEDIUM with jube native modules** —
status: OPEN, design gap rather than bug.

JS typed arrays are backed by ArrayNum, but the reverse direction doesn't
exist: an `LMD_TYPE_ARRAY_NUM` item flowing *into* JS is handled only as an
opaque container pointer in shaped map slots (`lambda/js/js_props.cpp:621`,
`lambda/js/js_runtime.cpp:2959`, `:5659`, `:7353` — all just
`*(Container**)field_ptr = value.container`). There is no
`LMD_TYPE_ARRAY_NUM` arm in the property-get/subscript dispatch, so JS code
receiving a Lambda numeric array cannot do `arr[0]`, `arr.length`, or iterate
it.

No current test exercises this (JS arrays are `LMD_TYPE_ARRAY` of boxed
Items; ArrayNum stays internal to typed arrays), which is why it's latent.
It becomes real the moment jube native modules (see
`vibe/Lambda_Native_Module_Design.md`) or radiant-dom start passing numeric
arrays across the Lambda↔JS boundary — the natural expectation is that a
Lambda `[1.0, 2.0, 3.0]` ArrayNum surfaces in JS as (or like) a Float64Array.

**Fix directions:**
- Cheapest: at the boundary, wrap incoming ArrayNum in a proper JS typed array
  whose `view` *is* the ArrayNum (zero-copy — `array_num_new_external_view`
  already supports a `base` container for lifetime); elem-type mapping is the
  inverse of `js_typed_array_elem_type` (`lambda/js/js_typed_array.cpp:354`).
- Alternatively: add `LMD_TYPE_ARRAY_NUM` arms to the JS subscript/property
  dispatch (length + numeric index via `array_num_get_number_value`). More
  invasive across js_runtime/js_props, and duplicates what the wrapper already
  provides.

---

## Summary table

| # | Issue | Severity | Kind |
|---|-------|----------|------|
| 38 | P4h hoisted data ptr/len stale after in-loop buffer resize | MEDIUM — **FIXED 2026-07-14** | latent UAF in JIT fast path |
| 39 | Raw fallback duplicates element switch; view mismatch → silent `0.0` | LOW | maintenance / silent-failure hazard |
| 40 | Lambda ArrayNum items not indexable from JS (one-way unification) | LOW→MEDIUM | interop design gap |

Related: `doc/dev/js/JS_12_TypedArrays.md` still documents the pre-migration
raw-storage design (zero ArrayNum mentions) — being fixed in background task
`task_a17cd7b2`.
