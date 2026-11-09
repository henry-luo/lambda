# Interpreter alignment with MVP and Full MIR retirement

**Date:** 2026-11-10
**Starting tree:** `aee8f3242`, main checkout, `master`.

## Current checkpoint

- **Source retirement implemented, 2026-11-10; full-host validation blocked.**
  Starting HEAD `aee8f3242`, main checkout, `master`; changes are uncommitted.
- Interpreter directly calls MVP eager/snapshot APIs and publishes its boxed
  Item/scalar-companion ABI (D6.2.1, D7.4.2). Shared runtime helpers remain
  shared; T0/AUTO/JIT and publication policy are preserved (D8.3.1–D8.3.5).
- Deleted the 49,836-line Full emitter and its unused loop-hoisting header.
  Shared linker, view registration and satellite lifetime now live in
  `lambda/runtime/mvp/mvp_driver.cpp`; compiler-neutral constants live in
  `const_materialize.cpp`. Unused Full variant/cache fields are removed.
- MVP worker plans borrow read-only frontend analysis and retain private
  compiler pools/constants; old Full variant clones are gone (D8.3.4).
- T0 import admission/dispatch and MVP linking use one JS-import classifier.
- Unset/empty and explicit `mvp` have one compiler/cache identity (D8.5.1).
  `production` is rejected before cache/tier selection; historical Full
  comparisons require an explicit archived `--control` executable.
- Build configuration/generated source lists are updated. Old objects and
  archives containing `transpile-mir.o` were purged (Developer Guide §7.3).
- **Release runtime archive builds successfully** (`runtime-qualified.log`),
  SHA256 `ed23aa723699f57ac515b7d56a890de5729576c1c9dc659572c409236dca6446`.
  Archive/symbol inspection confirms MVP entry APIs and no retired emitter.
- **Independent checks pass:** 8 benchmark-harness unit methods (2.682 s),
  Premake generator self-tests, recovery-boundary source gate, four no-MIR
  source syntax checks and three affected GTest-source syntax checks.
- **RI-004 blocks adapter/full-host rebuilding:** Lambda's existing pointer
  event consumer expects fields/types absent from Radiant `558eb3e`'s
  `event.hpp`. See [owner ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#ri-004)
  and `temp/mvp_full_mir_retirement/adapters.log`. Which checkout needs its
  protocol update is unconfirmed. No owner source/build edits or fallback used.
- Root `lambda.exe` is unchanged (prior default-switch release), SHA256
  `21884b40fcb5392a1747838d91e27628064afb1599a9c1a998f3d95308ff903b`.
  Pre-change copy: `temp/mvp_full_mir_retirement/before.exe`.
- Execution GTests, new compiler/GC/tier runtime checks, performance comparisons
  and Lambda/Radiant baselines were **not run** on this retirement source.
  Syntax/build checks do not qualify runtime correctness or performance.
- Evidence and source/build identities: `temp/mvp_full_mir_retirement/final-summary.json`;
  detailed logs and `syntax-summary.json` are in the same directory.
- No jobs remain running. Next, after owner protocol alignment: rebuild the
  adapter and host from current sources, then run the selected compiler/tier,
  GC, cache and resident-JS-export tests. Do not relink the old adapter to
  bypass RI-004. No commit was requested.

## Audit and implementation

T0 already uses the same C-ABI semantic helpers as MVP. Its direct dependencies
on Full were compiler entry points, compiler-owned public-return variants and
comments naming old emitter details. Replacing general runtime helpers would
add another implementation of the value model; the shared helpers stay shared
(D2.1, D7.4.1). The import classifier is the one duplicated policy extracted for
both tiers in this change.

The old source also owned services required by any MIR image: compiler-pass
orchestration, import resolution, property-key relocation, linking policy,
view registration and satellite lifetime. These move to MVP without copying
the old lowering or routing unsupported nodes through it. Existing MVP islands
continue to use T0 under the established admission rules (D8.3.1).

Full compiler selection is no longer available. The compatibility spelling
`mvp` supports existing test tooling, and a retired `production` request fails
explicitly to keep benchmark provenance honest. Historical Full binaries and
performance receipts remain usable as separately identified controls.
