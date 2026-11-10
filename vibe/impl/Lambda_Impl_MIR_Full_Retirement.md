# Interpreter alignment with MVP and Full MIR retirement

**Date:** 2026-11-10
**Starting tree:** `aee8f3242`, main checkout, `master`.

## Current checkpoint

- **Helper retirement complete, 2026-11-10.** Starting clean `fd07a893f`,
  main checkout, `master`; this follow-up is uncommitted.
- Removed **54 functions**: 47 runtime helper entries, two compiler utilities,
  two private array kernels and three unused TCO-safety analysis functions.
  Matching JIT rows/externs/NO_GC audit entries are removed. About 800 runtime
  source/header lines disappear; this is function removal, not formatting.
- Audited 267 imports referenced by the retired emitter against MVP, T0,
  LambdaJS, generated-name macros, tests, assembly aliases, both sibling source
  trees and host exports. Symbol inspection confirms removed entries absent.
- Retained the unary imported-predicate trampoline and `fn_call3_into` (Radiant
  assembly alias). T0's live recursive identity/tail eligibility analysis stays.
  Runtime data layouts, call behavior and precise roots are preserved
  (D2.1.1, D6.3, D7.4.2).
- Follow-up [region/profiling assessment](Lambda_Impl_MIR_MVP_Regions_and_Profiling.md)
  connects MVP to the COW site wrappers. The user subsequently requested region
  retirement: allocator, caches, wrappers/imports and shared-header declarations
  are removed; the approved ABI change resolves LI-006. Candidate identities and
  limits are recorded separately; this checkpoint's executable remains unchanged.
- Input owns 30 stale declarations in `lambda/lambda.h`; nonblocking **LI-004**
  records that cleanup. No sibling source/header/build files were edited
  (D9.1.1v4, D9.1.3v2, D9.4.6); ordinary sibling build artifacts were refreshed.
- Radiant **RI-004 is resolved** at `7106a48`. Full release host build succeeds.
  Root `lambda.exe` is the final candidate, SHA256
  `0e17c8c569764b0619088296bdb93fc5bfadc2f6cfde2f433b4fa55ea536ee75`.
  Private copy: `temp/mvp_helper_retirement/lambda-final.exe`.
- Initial focused Python campaign passes all **22 methods**. Additional tail
  and affected-path checks leave **26/29 unique methods passing** overall.
  Selected native/tier GTests pass **24/25**, including the new import-effect
  audit and all nine tier/GC/publication checks. Six interpreter-only syntax
  checks and the recovery source gate pass.
- **All four failing methods predate this cleanup:** cache image/hit assertions
  (**LR01-23**), two MIR-shape assertions (**LR07-57**), and the u32 fixture's
  E501 representation conversion (**LR07-58**, eight subcase failures).
  The cache control restores all six changed runtime implementation objects
  from `fd07a893f` under identical current headers/archives/test code. Other
  failures reproduce on the saved pre-cleanup release SHA256
  `21884b40fcb5392a1747838d91e27628064afb1599a9c1a998f3d95308ff903b`.
  No failing oracle was weakened or filtered out of the aggregate receipt.
- Detailed source/caller/symbol/build/test identity: `temp/mvp_helper_retirement/`;
  `final-summary.json` links the exact receipts. The source audit table below
  groups the removals and documents retained candidates.
- No Lambda/Radiant baseline, full benchmark campaign or performance claim.
  No jobs remain running. Follow-up: owner-header cleanup (LI-004), separate
  investigation of LR01-23/LR07-57/LR07-58, and region/profiling API decisions.

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

## Initial source retirement checkpoint (2026-11-10)

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

## Runtime helper audit (2026-11-10)

The follow-up starts at `fd07a893f`. Registry entries keep a helper linked even
when no compiler emits it, so a registry hit alone is not a caller. The audit
compares the retired emitter against current source references, generated import
name families and macros, C/C++ calls, assembly names with and without a leading
underscore, tests and both sibling source trees. None of the removed APIs is a
language system-function entry or an approved Jube host export (D9.4.6).

| Removed group | Functions | Live replacement/path |
|---|---:|---|
| Boxed-call trampolines, arities 0 and 2–16 | 16 | MVP emits direct ABI calls; retain arity 1 for imported `that` predicates (D7.4.2). |
| COW path adapters | 4 | MVP/T0 use shared path/key and place kernels (S9.1.2–S9.1.3). |
| Array literal, constructor, join and checked-store adapters | 13 | MVP's retained plans select canonical preplanned stores, resolved fill and ordinary constructors. Includes two private dependencies. |
| Scalar/string and virtual-content wrappers | 7 | Current lowering uses shared scalar/string readers and normal virtual-content handling. |
| Varargs list/object fills | 3 | T0/MVP use rooted Item spans and field admission (D6.3). |
| Other call/type/module and field-image adapters | 6 | Canonical dynamic calls, type/predicate loads, module-state constants and slot images. |
| Compiler utilities | 2 | MVP owns its entry/wrapper plans. |
| Unused TCO safety proof and private walkers | 3 | Retain T0 recursive identity, tail-position and eligibility analysis (D8.3.2). |

`temp/mvp_helper_retirement/removed.json` records the exact 54-name inventory.

Two important exclusions were confirmed: `fn_call3_into` is reached through
Radiant's `radiant_lambda_fn_call3_into` assembly alias, and
`fn_call_boxed_1_into` is used by the imported constrained-predicate bridge.
Both definitions and registry entries remain. Public runtime data layouts and
precise-root ownership are unchanged (D2.1.1, D6.3, D7.4.2).

Region retirement subsequently removed the allocator, caches and the region
parameter from `map_alloc_for_type`; the user explicitly approved the scoped
input-owned header edit recorded in LI-006. All native/JS/MVP callers use the
GC-only allocator. See the follow-up assessment for verification identities.
The profiled COW wrappers/site census have an active diagnostic regression test
(`LambdaOptAdmission.CowSiteCensusMatchesAggregateAndIsOptIn`), so this pass did
not delete that interface. The follow-up
[assessment](Lambda_Impl_MIR_MVP_Regions_and_Profiling.md) connects MVP to those
wrappers. The later user-directed region retirement is complete with LI-006
resolved; no region integration milestone remains planned.

Thirty declarations for removed helpers remain in the input-owned
`lambda/lambda.h`. They are recorded as nonblocking
[LI-004](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-004); no header
or implementation in a sibling project was edited.
