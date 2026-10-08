# JS MVP Lambda numeric facts and element access

**Date:** 2026-10-08  
**Status:** implemented in source; release benchmark comparison passed;
semantic/GC acceptance pending.

Scope: [MVP §16](../jube/JS_MVP_Lmd.md#16-numeric-facts-and-element-access-tuning).
Authorities: **S1.11**, **D2.2.5**, **D2.4.3**, **D2.6.1v3**, **D5.3.4**,
**D8.2.6**, **D8.4.1v2**. No formal ruling changes.

## Reuse and helper inventory

The implementation extends the existing MVP type/range fixed points, numeric
conversion, binding snapshots, and array-reference emitters. Physical access
continues to use Lambda's `em_numeric_storage_type`,
`em_array_element_address`, `em_load_at`, and `em_store_at`. Scalar boxing uses
the existing shared emitter and destination-owned homes. No new runtime
imports, allocators, GC hooks, or full-JS dependencies are introduced.
Lambda's nullable native lanes represent null (**D2.5**); JS undefined uses a
separate presence flag so a present NaN keeps its identity. Shared physical
emitters explicitly leave nullable/JS admission to the frontend.

Three compiler utilities in `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` were disclosed
before implementation:

| Utility | Purpose and reason | Dependencies / ownership |
|---|---|---|
| `array_facts` | Recognize and propagate typed lanes/extents and unchanged numeric literals through the JS AST; physical Lambda emitters do not supply these JS admission facts | Existing binding/call lookup and range solver; compiler metadata only |
| `numeric_operands` | Share numeric-demand admission between binary and compound assignments while preserving JS addition, relational comparison, and equality | Existing shared numeric-op plan and MVP kind masks; no runtime effect |
| `member_spelling` | Consolidate repeated fixed property-name decoding used by inference, Math, references, and iteration | Existing AST/String metadata only; no runtime effect |

## Representation and proof boundaries

- Typed lane sets propagate through constructors, assignments, aliases,
  `.fill()`, conditionals, and closed calls. Unknown or unresolved alternatives
  prevent specialization. Extents widen conservatively; fixed storage keeps
  lane and length stable under element mutation.
- Element loads feed native numeric consumers. General number-or-undefined
  results use a native double plus a presence register; absence carries NaN
  only in the numeric register. Boxing, equality, strings, `typeof`, nullish
  operations, and keys still distinguish undefined from a present NaN.
  Local bindings snapshot both registers; no borrowed scalar pointer is kept.
- Stores preserve proven integer inputs through JS wrapping. Other values
  still undergo ToNumber before an out-of-bounds store is ignored. Fill
  performs JS coercion and selects its lane outside the loop.
- Counted `for` induction variables can retain finite integer facts when
  nested, provided their initializer resets them and the only update is the
  loop update. Body-only bounds narrow immutable induction values, including
  affine expressions; exit/update uses keep their activation-wide ranges.
  Unproved bounds retain the existing check. Successful immutable module
  constant reads keep range facts while preserving TDZ checks.
- Ordinary literal arrays retain boxed Item storage. Content/extent facts
  require numeric literals and no writes, method calls, or escapes of the
  binding. Solved bounds can narrow element kinds and closed-call parameters.
  Mutable or escaped ordinary arrays retain the existing path.
- Raw data pointers are loaded through the existing owner at each access.
  No data pointer is cached across a call or GC boundary. Shape transitions,
  Map behavior, VMap exclusion, and unsupported JS features are unchanged.

## Evidence

- Release build: `make build-release-compile`; final log
  `temp/mvp_lmd_numeric_tuning_20261008/build-final2.log`. `git diff --check`
  passes. No shared runtime or vendor source changed.
- [Final paired comparison](../../test/benchmark/js_mvp_lmd/MVP_Result4.md):
  42 workloads, 15 alternating pairs, pinned native MIR, self-reported times,
  an identical-control peer, Node, and 27 untyped Lambda control/candidate
  pairs. All **3,330 measured and 222 discarded outputs match**. Binary,
  source, runner and Node hashes are recorded and checked.
- The twelve newly admitted kernels improve **2.396×** geometrically over the
  exact preceding release: `matmul` 45.321 → 5.938 ms (**7.64×** paired),
  `array1` 3.531 → 0.666 ms (**5.31×**), `triangl` 872.795 → 620.267 ms
  (**1.41×**). `pnpoly` improves **1.25×** and `diviter` is unchanged.
  No paired median slowdown exceeds 5%; identical-control speedups span
  **0.985–1.021×**, and Lambda control comparisons **0.982–1.022×**.
- Static finalized MIR counts fall from 1,760 → 862 for `matmul`,
  832 → 228 for `array1`, and 10,670 → 5,006 for `triangl`. These include cold
  paths and are code-size evidence, not execution profiles.
- Frozen sources/binaries, build logs, MIR, raw outputs and the runner are
  under `temp/mvp_lmd_numeric_tuning_20261008/`; `final/` is the final source
  and binary measurement. Earlier `screen1`, `screen5`, and `confirmed`
  directories retain intermediate evidence and are not the final result.

Numeric edge/GC and full acceptance suites from §15.3 remain pending; benchmark
output checks do not replace them. No new tests or baseline suites were added
or run in this tuning turn.
