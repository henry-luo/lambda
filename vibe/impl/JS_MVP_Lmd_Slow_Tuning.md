# JS MVP slow-kernel tuning

**Date:** 2026-10-08  
**Status:** implemented; latest release build and benchmark output checks pass.
Unit, forced-GC and baseline suites were not rerun for the latest follow-up.

Scope: [MVP §17](../jube/JS_MVP_Lmd.md#17-slow-kernel-tuning).
Authorities: **S1.11**, **D2.2.5**, **D2.4.3**, **D3.4.3v5**,
**D5.2.1v3**, **D5.3.4**, **D8.2.6**. No formal ruling changes.

## Current changes and reuse

- Optional integer locals retain an i64 payload plus a presence flag. The
  existing range fixed point tracks present-value ranges separately: ordinary
  arithmetic still treats absence as unknown/NaN. Undefined contributes no
  present payload; fractional values, negative zero and unresolved cycles
  prevent integer admission. Dynamic string keys retain the generic range
  because a successful read may be `length`.
- Existing scalar conversion, snapshot, equality, relational and typed-array
  store emitters consume that carrier. Missing payloads stay zero; boxing
  produces undefined and numeric conversion produces NaN. Integer comparisons
  also inspect presence; missing values remain unordered. ToInt32(undefined)
  is zero, while Float64 stores preserve NaN.
- Closed factories whose result domain excludes Number use Lambda's existing
  plain-Item return ABI. Producer and consumer read the same return descriptor;
  pending scalar resolution and caller number homes are omitted only for this
  domain. Dynamic calls retain their existing companion convention.
- Nonnumeric local values copy their rooted Items without scalar adoption.
  Number-capable values retain existing destination-owned scalar homes.
- Fixed-key planned object literals evaluate their values once, in source
  order, before allocating the unobservable parent. Child snapshots remain
  precise roots across later initializers and the existing `map_alloc_for_type`
  call. Computed/duplicate-key literals retain the incremental path. Existing
  shape guards and transition fallback still handle field type mismatches.

All changes are in `lambda/js/mvp-lmd/mvp_lmd_mir.cpp`. Shared runtime,
allocator, benchmark and vendor sources are unchanged. No new compiler/runtime
helper or import was added; existing functions were extended.

The initial round also introduced direct condition branches, integer element
carriers, immutable container facts across closed calls, nullable object shape
stores, and local factory inlining. Its two disclosed compiler helpers were
`branch_condition` and `immutable_member_kind`; both remain compiler-only.

## Latest release evidence

[Result5](../../test/benchmark/js_mvp_lmd/MVP_Result5.md) contains the complete
42-workload comparison, fresh Node/untyped Lambda references, raw samples,
bootstrap intervals and the initial round's history.

- Build: `make build-release-compile`; **zero errors, 28 existing warnings**.
  Logs: `temp/mvp_lmd_recursive_20261008/build4.log` and `build4-full.log`.
- Control is the exact initial Result5 candidate:
  `172cf89c2a781b8dc15b291c335cfb8518fda54a8110c2e857a1e10ee409f1c5`.
- New candidate:
  `190d5f602876b1023ba83e9d25eee603af5fd938ebf64c3aaaefe1ef8ff38cf7`.
- Compiler source:
  `88c5dfdc937f124f7555fababe55ae5df4bafe675854df9b733ceedd8319e832`.
- **15 alternating release pairs**, pinned native MIR, self-reported execution
  times, identical-control peer, Node v22.13.0 and 27 untyped Lambda
  control/candidate comparisons. All **3,330 measured and 222 discarded
  outputs match** unchanged oracles. Sources, binaries, runner and Node hashes
  were checked. Frozen artifacts are under
  `temp/mvp_lmd_recursive_20261008/final/`.

| Workload | Control → candidate ms | Paired gain | Candidate/control 95% interval |
|---|---:|---:|---:|
| quicksort | 1.707 → 1.298 | 1.316× | 0.760–0.772 |
| gcbench | 95.970 → 91.792 | 1.046× | 0.951–0.961 |
| binarytrees | 3.981 → 3.753 | 1.061× | 0.935–0.962 |
| triangl | 157.987 → 155.499 | 1.016× | 0.982–0.987 |
| deriv | 8.789 → 8.465 | 1.040× | 0.943–0.983 |
| pnpoly | 7.425 → 7.332 | 1.017× | 0.975–0.995 |

The six improve **1.078×** geometrically; all 42 improve **1.025×**.
Quicksort's MIR instruction count falls **841 → 605**, and static I2D/D2I
counts fall **18/19 → 12/11**. These are compiler-output counts, not native
instruction or hardware-counter measurements. Paired evidence measures the
whole tuning bundle, not individual changes.

The retained tradeoff is escaped object retyping: **2.2% slower**, confirmed
with 30 additional pairs (90 measured and three discarded matching outputs)
under `temp/mvp_lmd_recursive_20261008/retype_followup/`. The follow-up remains
separate in the JSON; it does not overwrite the full-run table. No full-run
workload has a paired median slowdown above 5%.

Unit, forced-GC and baseline suites were **not rerun for this follow-up**.
The initial round passed 37/37 MVP tests normally and with forced GC/poisoning,
6,362/6,362 Lambda/input cases, and 40,261/40,261 Test262 cases with zero
retries or instability. Those gates apply to the control binary only; their
logs/hashes are retained in Result5 JSON's `previous_round.validation` and
`temp/mvp_lmd_slow_tuning_20261008/validation.json`.

## Remaining scope

Object allocation and collection still dominate part of the recursive tree
cost. This change reuses the existing allocator and precise roots; it adds no
region allocation or runtime shape feedback. Stronger control-flow bounds,
optional values through more generic/inlined paths, and the small escaped
retyping regression remain tuning opportunities. The complete constructor/Math
edge matrix from §15 is separate from this phase's benchmark evidence.
