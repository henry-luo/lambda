# JS MVP Lambda numeric libraries — implementation record

**Date:** 2026-10-07  
**Status:** implemented in source; runtime and benchmark acceptance pending.

**Scope:** [JS_MVP_Lmd §15](../jube/JS_MVP_Lmd.md#15-numeric-libraries-and-wider-benchmark-coverage),
**S1.11**, **D1.3v3**, **D2.2.5**, **D2.4.3**, **D2.6.1v3**, **D5.3**, **D8.2.6**.
No formal ruling changes.

## 1. Reuse decisions

- `array_num_new` owns zero-initialized `ELEM_INT32`, `ELEM_UINT8`, and
  `ELEM_FLOAT64` buffers. Construction uses the existing precise native root
  during buffer allocation; generated code checks both the returned header
  and its realized length. No new carrier, allocator, GC hook, or full-JS
  typed-array API is involved.
- `em_numeric_storage_type`, `em_array_element_address`, `em_load_at`, and
  `em_store_at` provide the physical lane operations. Existing MVP
  `to_number`/`int32` lowering supplies JS conversion before storage.
  `array_num_set_item` uses Lambda coercion; the vector writer rounds/clamps.
  Neither supplies JS typed stores. Reusing the existing MIR operations
  avoids a new native setter or an option on those unrelated conversion paths.
- Math reuses `fn_abs_f`, `fn_min2_u`, `fn_max2_u`, plus the same scalar C
  `sqrt`, `sin`, `cos`, `floor`, `ceil`, and `trunc` entry points selected by
  Lambda's system-function registry. The boxed `fn_math_*`/rounding APIs also
  handle Lambda null/vector/complex semantics, so the native entry points
  follow MVP's JS coercion. No native Math wrapper was added.

## 2. Disclosed helper inventory

All four additions are compiler utilities in
`lambda/js/mvp-lmd/mvp_lmd_mir.cpp`; none is a runtime import.

| Utility | Purpose and dependencies | Ownership effect |
|---|---|---|
| `math_operation` | Share binding-aware fixed-member recognition between kind inference and call lowering; uses existing identifier and spelling lookup | Compiler metadata only |
| `typed_array_lanes` | Share the three lane cases across reads, stores, and fill; uses the shared storage emitters and existing coercion/boxing | Reads copy values into registers or emitter-owned scalar homes; stores retain native bits; successful lane operations do not collect |
| `typed_array_reference` | JS bounds, `.length`, fill-method capture, and numeric-index policy; uses existing key conversion and the lane emitter | Owner remains a precise Item root; error paths use the existing failure helper |
| `typed_array_fill` | Convert value once, clamp start/end, and reuse lane stores in a loop; uses existing numeric conversion and native `trunc` | Snapshotted receiver/arguments remain emitter-rooted; loop holds no borrowed scalar pointers |

The existing native `mvp_lmd_string_key` accepts an optional typed-index mode;
its default preserves ordinary dense-array/string key classification. Typed
mode uses `mvp_lmd_string_to_number` and
`lambda_finite_double_to_shortest` with stack buffers. This dependency closure
does not collect or call full-JS helpers. It distinguishes `.length`, `.fill`,
valid indices, invalid canonical numeric indices, and unsupported named keys.

Newly admitted runtime imports are `array_num_new` and the nine native Math
entry points above. Allocation remains `MAY_GC`; scalar Math and key conversion
are `NO_GC`. Allocation/error dependencies are the existing Lambda heap,
precise-root, and error paths. Owned flat ArrayNum tracing, compaction, and
destruction use shared container/data-zone handling, without full-JS hooks
(**D5.3.2–D5.3.4**).

## 3. Semantic boundaries

Lengths apply JS ToIndex after all constructor arguments have been evaluated.
Indexed stores convert before the bounds check; invalid canonical numeric
indices read as undefined and ignore stores. Numeric `-0` addresses element
zero; the string `"-0"` is an invalid numeric index. Other named properties are
capability failures. Length writes are ignored in sloppy code and produce a
TypeError in strict code. Fill snapshots its receiver before arguments,
converts its value once, clamps negative/infinite/fractional bounds, honors
an undefined end, and returns the receiver.

Math recognizes the unshadowed intrinsic with a named member or a literal
string member. Argument expressions run in order, including unused extras.
Unary calls coerce the first argument; min/max coerce every supplied argument.
Aliases, dynamic Math member names, intrinsic mutation, object-to-primitive
conversion, additional typed-array properties, and typed-array iteration stay
outside this phase. Ordinary mixed arrays keep their representation.

These are JS profile rules (**S1.11**, **D2.4.3**); algorithm references:
[TypedArray fill](https://tc39.es/ecma262/multipage/indexed-collections.html#sec-%typedarray%.prototype.fill),
[canonical numeric indices](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-canonicalnumericindexstring),
and [ToIndex](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-toindex).

## 4. Evidence and remaining gates

- Release build: `make build-release-compile`; build logs under
  `temp/mvp_lmd_numeric_phase/`.
- `git diff --check`: passed.
- No tests were added or run in this implementation round. Numeric edge,
  alias/snapshot, shadowing, forced-GC/poison, existing MVP, Lambda/input, and
  Test262 checks remain pending.
- The nine §15 benchmark kernels remain unverified targets. No new timing or
  speedup claim is made. Next measurements must retain matched kernels,
  inputs, iteration counts, and result oracles; use frozen release binaries,
  pinned native MIR, self-reported times, alternating pairs, and a control peer.
- Native compilation on Linux and Windows remains pending.
