# JS MVP slow-kernel tuning

**Date:** 2026-10-08  
**Status:** implemented; targeted semantic/GC, release pairing, Lambda/input
and Test262 gates passed.

Scope: [MVP §17](../jube/JS_MVP_Lmd.md#17-slow-kernel-tuning).
Authorities: **S1.11**, **D2.2.5**, **D2.4.3**, **D3.4.3**, **D5.3.4**,
**D8.2.6**. No formal ruling changes.

## Changes and reuse

- Integer literal and typed-array element reads retain an i64 value plus a
  presence flag. Missing integer payloads are zero; numeric coercion produces
  NaN, boxing produces undefined, and truth/equality retain JS semantics.
  Existing `em_unbox_finite_int_item`, numeric storage/address emitters, and
  scalar homes provide the physical operations. Immediate numeric consumers
  merge absence into NaN at the load, avoiding a second conversion branch.
- Nested indexing uses the range of a present integer element. Missing keys
  still reach the capability error in the same evaluation order. Unproved
  bounds retain their checks, and no data pointer crosses a call/GC boundary.
- Boolean conditions lower `&&`, `||`, and `!` directly to branches. Value
  contexts retain their original operand results and short-circuit behavior.
- A closed unit without property mutation or computed/non-identifier object
  keys can propagate numeric literal-array facts through aliases and calls.
  The same conservative admission summarizes literal object field kinds.
  Missing fields contribute undefined; duplicate fields use the last value.
  These facts do not remove the existing runtime shape guards.
- Nullable object fields can use predicted Map storage with a value-kind
  guard and the existing Lambda shape-transition fallback. Strict equality
  against null or undefined compares their singleton Item encodings.
- Dominated, unchanged local function initializers reuse direct calls and
  bounded inlining. Unobserved function identities need no allocation. Named
  expressions keep the boxed entry that supplies their self binding. The
  second facts walk no longer mistakes revisiting an initializer for a write.

## Disclosed helpers

Both helpers are in `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` and were disclosed
before implementation. No runtime helper, import, allocator, GC hook, or
full-LambdaJS dependency was added.

| Helper | Purpose and reuse boundary | Ownership |
|---|---|---|
| `branch_condition` | Traverse JS logical conditions using existing truth and MIR branch emitters; Lambda AST traversal cannot consume JS AST nodes | Compiler only; original operand order and rooting |
| `immutable_member_kind` | Summarize successful literal property reads in an admitted immutable unit using the existing AST index and kind fixed point | Pool-owned compiler metadata; no runtime allocation |

## Correctness boundaries

The existing snapshot tests caught a lost unknown-range fact after array
parameter propagation: absence of an integer-content proof must be unknown,
not fixed-point bottom. The fix preserves negative zero and tiny fractions
when later assignments have integer values. The existing named recursive
function-expression test also caught a missing self argument; such entries
remain on their existing boxed ABI.

New regression cases cover optional integer elements, numeric/string/bitwise
coercion, missing nested keys, short-circuit order, read-only arrays across
calls, property mutation/deletion/computed-key invalidation, recursive object
fields, and local factory identity/reassignment/TDZ. Global literal facts are
fixed before lowering; guarded inlining does not rewrite that summary.

## Evidence

- Release build: `make build-release-compile`, log
  `temp/mvp_lmd_slow_tuning_20261008/build9.log`; zero errors, 28 existing
  warnings. Both frozen executable files are 19,549,432 bytes.
- Rebuilt focused executable: `make -C build/premake config=release_native
  test_js_mvp_lmd_gtest -j8`. All **37/37** tests pass normally and again with
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Logs are
  `tests-final.log` and `tests-gc-final.log` under the same temporary directory;
  the tested binary is frozen as `mvp-tests-release.exe` (SHA-256
  `60df763e5456b1ebcc7c00dc1f40232df5eadcbe14a2aa1677b8ce033321f59a`).
- [Final paired comparison](../../test/benchmark/js_mvp_lmd/MVP_Result5.md):
  42 workloads, 15 alternating release pairs, pinned native MIR, self-reported
  times, an identical-control peer, Node, and 27 untyped Lambda
  control/candidate comparisons. All **3,330 measured and 222 discarded
  outputs match**. Compiler, binary, benchmark, runner and Node identities
  are checked and recorded in the linked JSON. Frozen sources/binaries, raw
  outputs and MIR are under `temp/mvp_lmd_slow_tuning_20261008/final/`.
- Target paired gains: `triangl` **3.897×**, `deriv` **2.199×**, `pnpoly`
  **1.829×**, `gcbench` **1.054×**, and `binarytrees` **1.055×**. The five
  improve **1.771×** geometrically; all 42 improve **1.093×**. Each target's
  95% interval excludes no change, with its identical-control peer within
  0.4% of parity. Static MIR instruction counts and total process time also
  decrease for every target; parsing/JIT latency was not isolated.
- `quicksort` retains a **3.3%** slowdown (95% ratio interval: **2.3–4.0%**).
  Numeric-demand loads avoid one unnecessary conversion, but optional local
  snapshots still convert integer carriers to doubles. No workload's paired
  median slowdown exceeds 5%. Other small changes remain noise-sensitive.
- `make test-lambda-baseline`: **6,362/6,362** on the first run (2,104 input
  and 4,258 runtime cases), including **20/20** MIR-size checks and **37/37**
  MVP cases. Log: `temp/mvp_lmd_slow_tuning_20261008/lambda-baseline.log`.
- `make test262-baseline`: **40,261/40,261** fully pass, with **zero retries,
  unstable cases, batch failures or regressions**; all 169 batches exit zero.
  The baseline excludes 2,652 discovered tests. Log:
  `temp/mvp_lmd_slow_tuning_20261008/test262-baseline.log`. This is a shared
  full-JS regression gate, not a claim that MVP supports all Test262 features.
  The resulting installed release matches the measured candidate SHA-256.
- `git diff --check` passes. No shared runtime, benchmark source or vendor
  source changed in this phase.

## Remaining scope

Node remains faster on these five kernels: **2.33×** for `triangl`, **2.25×**
for `deriv`, **4.31×** for `gcbench`, **1.28×** for `pnpoly`, and **1.87×** for
`binarytrees`. All five now beat their untyped Lambda ports. These comparisons
are fresh-process workload times, not fully warmed Node throughput.

Recursive allocation, more complete control-flow bounds, and preserving
optional integer locals across snapshots remain follow-up work. This phase
does not introduce region allocation, change array storage, or add runtime
shape feedback. §15's complete constructor/Math edge matrix remains separate
from this phase's targeted semantic/GC checks.
