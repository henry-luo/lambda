# MVP MIR optimization test migration

**Date:** 2026-11-10. **Starting HEAD:** `cf0caf5c7`, main checkout, `master`.

## Current checkpoint

- Follow-up from `dced21367`: all 49 retained emission failures are resolved;
  all 197 failures from the preceding migration are now covered. The complete
  controller passes 582/582, including 285/285 emission contracts and 285/285
  semantic goldens. Both poison-GC modes pass all 285 goldens.
- Lambda size/frame ratchets pass 18/18 and shared JS capture checks pass 8/8.
  Optimization contracts improve from 46/56 to 51/56, with no new failures.
  The five retained failures include LR07-62, hyphen append work from LR07-64
  and three borrowed-array/union admission cost contracts. See the current
  qualification below; the entire optimization gate is not green.
- Native/checked witnesses, semantic controls, precise ownership and reviewed
  cost budgets remain explicit (D8.6.1, D6.3, S9.1.2–S9.1.3). No retired Full
  helper is restored. LR07-61 is qualified on release only; LR07-65 remains a
  separate T0 argument-snapshot issue.
- Current receipts and source/controller/binary identities are under
  `temp/mvp_remaining49/`. Earlier sections record preceding source versions;
  the original audit is `temp/mvp_optimization_coverage/coverage-report.md`.
- Only unit gates ran in this follow-up. The shared executable and
  sibling/vendor sources remain unchanged. No Lambda/Radiant baseline,
  performance campaign or commit was performed.

## Acceptance and verification budget

Every migrated contract must select an actual MVP body, retain a positive
witness of the optimization and preserve an appropriate rejected/checked
control. Missing instrumentation must fail rather than count as zero. The
normal Lambda gate must run the focused MVP unit selection. New `.ls` files
carry semantic goldens and MIR sidecars. Record all remaining implementation
failures and tested source/binary identities; do not call a partial gate green.

The emission ratchet retains reviewed, zero-slack budgets (D8.6.1). Current
MVP emission may differ structurally from Full; budget changes must describe
the new ownership/guard/frame shape rather than mechanically raising limits.

Focused validation covers matcher contracts, Lambda emission and optimization
units, the new gate/report adapter, Lambda frame budgets and selected JS matcher
regressions. Broader suites run only if an implementation change requires them.

## Initial migration contracts and qualification

The shared matcher keeps explicit MVP function namespaces and exact helper
callees; ABI prototype suffixes are not optimization facts. It now has three
positive/negative matcher units. All 32 formerly required retired-helper
mentions use current kernels or explicit native-path requirements. The latter
remain red where MVP lacks the tuning, including nullable packed construction
and inferred append builders. No retired helper is restored to satisfy a test.

Execution-profile checks use emitted MVP bodies and real executed calls in
place of Full-only field/dense/layout events. The multi-source record test
requires the two bodies and their generic member reads before excluding shape
speculation. A missing function counter returns a failure sentinel, never zero;
its negative unit pins that behavior. Contextual record construction tests now
require destination-shaped allocation with dynamic field validation, and accept
zero graph reifications. Copy ceilings allow an avoided copy while preserving
semantic goldens. The string/TCO/admission-work limits are not inflated.

The five new fixtures cover typed/erased numeric loops and divmod, guarded array
loads/stores and misses, COW snapshots, direct/boxed/closure calls, wide outcome
transport, frozen string observations, Unicode/absence, packed record fields,
and invalid dynamic union fields (D4.3.3, D6.2.1, D7.4.2, S9.1.2–S9.1.3v3).
The POSIX companion-result ABI is exercised on this host; Windows has a separate
assertion branch but was not executed here.

Private release host SHA256:
`15b99367361d4a2f7482c7ded6f2966cf4958721348afc4a5f70c5f55efd1ac1`.
Shared `lambda.exe` remains unchanged at
`c6f6766f9e28aacec482d5762b90a4b22be885c21f2ee1eddf37b053a385a3aa`.
Receipts, raw reports, budget observations and controller/source identities:
`temp/mvp_emission_test_update/`.

| Focused gate | Result | Receipt |
|---|---:|---|
| New fixtures + matcher + outcome ABI + profiling root parity | 10/10 | `qualified-new-emission.json` |
| Lambda emission/frame ratchets + config | 18/18 (17 probes) | `final-ratchet.json` |
| Optimization contracts (mixed JIT/T0/JS) | 46/56 | `final-qualified-opt.json` |
| Registered Python MVP emission/ownership gate | 54/57 | `registered-gate/test_summary.json` |
| Gate adapter reporting/selection units | 5/5 | `gate-adapter-final.log` |
| Shared-matcher JS regression selection | 7/7 | `js-matcher-regressions.json` |
| Node aggregate-runner contract file | pass | `runner-contracts.log` |
| Complete Lambda emission controller | 97/294 | `final-emission.json` |

The emission controller includes 284 fixture cases: 87 pass, 197 fail. The
older 279 cases improved from 2 passing in the audit to 82. All five new
fixtures pass. These totals do not establish full corpus migration: remaining
cases include Full-specific result/parameter/register assumptions, changed
inlining and generic-path contracts, and missed MVP tunings. The remaining
inventory is in `remaining-emission.json`; review each contract before changing
it. At that checkpoint LR07-63 still failed compilation, and LR07-62 crashed
the release host. The apparent release success of LR07-61 checked only exit
status; the follow-up golden exposed malformed value output and fixes it below.

The ten optimization-contract failures remain: immutable array-consumer crash;
borrowed numeric store helper; recursive union admission work; union cache-hit
minimum; tail string copies; hyphen append work; checked-tail frame reuse;
read-only length root reloads; nullable packed construction; and redundant
stable-length reads. Python retains three failures: propagation boxing,
conditional modular slow calls, and JSON constructor fusion. The registered
runner reports those failures and exits nonzero. No case is skipped, marked
expected-failure, or removed from its selection.

The new argument-order golden also exposes a T0 snapshot discrepancy, LR07-65.
Keep `[3, 9]`; changing it to the interpreter's `[9, 9]` would hide S9.1.3v3's
plain-argument snapshot rule.

No Lambda/Radiant baseline or benchmark timing campaign ran. The private split
release build needed two Lambda-owned include dependencies exposed by the
concurrent header split. No sibling implementation/header/test or vendor source
was edited.

## Remaining-fixture follow-up (2026-11-10)

Starting HEAD: `c2f1d2426dcd8621db195c3d2293b9eafab90cf7`. Fresh private
release/controller qualification reproduced the 197/284 fixture failures.
Reviewed 156 contracts across result/parameter ABI, scalar homes, COW, numeric
lanes, arrays, record fields, strings, predicates, bounds and effect guards.
Native positive witnesses, checked misses and cost ceilings remain explicit;
retired Full registers, trampoline names and constructor assumptions do not.
The unresolved inventory includes both further contract review and real
missing scalarization, native wide/compact lanes, builders, TCO and guard
caching. No case is skipped or marked expected-failure.

Every Lambda MIR fixture now also runs against a platform-aware `.txt` golden
in the same controller. Twelve missing goldens are supplied. This closes the
hole where a script could exit zero with a wrong value while its MIR patterns
passed. Output corrections follow error propagation (S5.1.1v2–S5.1.2v2,
S7.9.2v3), contract validation (S11.4.1v5) and frozen receiver observations
(S9.1.2–S9.1.3v3). The persistent-record typed-boundary rejection stays intact;
an additional observation requires the preceding unannotated write to succeed
and its old COW snapshot to remain unchanged (D3.4.5, S11.4.10).

Named MIR sequences retry ambiguous prefixes with independent capture sets.
A store prefix cannot bind the registers needed by a later real load. Strict
adjacency still applies; bounded search fails explicitly if exhausted. A
positive/negative matcher unit covers retry, wrong-address/store rejection and
label adjacency. Shared JS named-capture fixtures qualify the same matcher.
The interval fixture also invokes its unbounded controls with `INT53_MAX` so
closed-call constant specialization cannot erase their required slow paths.

Compiler/runtime root fixes:

- Nullable integer-to-float conversion retains absence; already-guarded
  numeric operands use their present contract rather than duplicating guards.
- Runtime result publication resolves a pending wide scalar with its MIR
  companion before retaining the Item (D6.2.1). The earlier release exit-zero
  observation did not validate its malformed result text; the golden now does.
- Recursive inferred-array classification reads the current parameter proof,
  with declared-contract fallback for imported formals. It now selects the
  recursive extent proof without suppressing unplanned checked-store errors;
  effect verification remains enforced (D6.3.2).
- Mutable nullable records lose inferred layout proofs just like non-nullable
  records. Nested writes and projected `var` arguments are audited through the
  existing COW place-root helper. A primitive-array invariant borrow can
  preserve the root domain only when it borrows that root directly. Later
  typed crossings validate mutated records again (D3.4.5, S11.4.10).

Qualification artifacts are under `temp/mvp_fixture_followup/`; source and
controller identities are in `record-identities.json`. Final private release
SHA256: `cb1fac1e77f2b7ac74163f3be0c0b1734446c9289148deca629a738af11ee8ef`.

| Final unit gate | Result | Receipt |
|---|---:|---|
| Complete emission controller | 530/579 | `record-final-emission.json` |
| MIR emission contracts | 235/284 | same report |
| Semantic goldens | 284/284 | same report |
| Forced GC + poison, all goldens | 284/284 | `record-forced-goldens.json` |
| Seeded GC + poison, all goldens | 284/284 | `record-seeded-goldens.json` |
| Optimization contracts | 46/56 | `record-final-opt.json` |
| Lambda emission/frame ratchets + config | 18/18 | `record-final-ratchet.json` |
| Shared JS named-capture regressions | 8/8 | `final-js-regressions.json` |
| MIR interpreter root-fix goldens + record T0 parity | 5/5 | `record-backend-parity.json` |

All 49 remaining emission failures were already among the 197: 148 original
failures are resolved and no previously passing whole fixture regressed. The
ten optimization failures are unchanged. Budget objects are unchanged. The
strengthened record MIR/golden controls both reject the preceding engine and
pass the fixed engine (`record-negative-control.json`, `record-contracts.json`).
The four nullable/wide/recursive/record fix goldens match under native JIT and
the MIR interpreter; the record golden also matches T0. Imported scalar flow
passes in the complete golden population. Remaining fixture names are recorded
in `remaining-record-final.json`.

The shared `lambda.exe` remains unchanged. No sibling/vendor source edits,
Lambda/Radiant baseline, Python aggregate, timing campaign or commit occur in
this follow-up. Debug and non-macOS hosts are not requalified by these release
unit results. Full corpus migration remains incomplete until the retained
emission and optimization failures are fixed.

## Remaining 49 contracts (2026-11-10)

Base HEAD: `dced21367` (`mir test fixes`), clean checkout. Fix lowering gaps
by family, then migrate obsolete Full ABI assertions to the resulting MVP
native paths and checked siblings. All 49 original failures pass the complete
gate. The corpus now has 285 fixtures, including a new structural membership
revocation control. Working receipts and source identities are in
`temp/mvp_remaining49/`.

Implemented compiler changes:

- Native non-nullable i64/u64 arithmetic and u64 bit operations preserve full
  width and wrapped results. Exact compact integer admission checks the
  complete tag/subtype encoding before native decode; stores use their native
  width with COW and checked conversion misses. Nullable wide values stay
  boxed; escaping scalar bits retain precise destination ownership
  (D2.3.4–D2.3.5, D6.2.3). Native literal bits use immediates; transient boxed
  literal operands reuse the immutable Input constant owner.
- Predicate success joins retain bool/nullable-bool domains, including Error
  alternatives; array-kind membership includes ranges. Native null tests use
  the lane's absence marker (D2.3.5). Dominating nonnegative and matching
  `len` bounds remove repeated array checks while auditing writes and loop
  induction. No int32 bound is assumed for a 64-bit array length.
- Admitted read-only record prefixes reuse their witnesses. Structural
  membership facts describe field domains separately from physical layout;
  mutation revokes them. Typed path stores retain live carrier/certificate,
  null, COW and checked-miss guards. A mixed prefix containing an `any` slot
  still checks its live shape: a prior write can repack it and move later
  fields (D4.2.5–D4.2.6, D4.3.3, S11.4.10).
- Total invariant native unary math can move outside a loop only with an
  unchanged argument and a noncollecting, nonreentrant helper. Float indices
  require exact integer round trips. Cached array lengths survive collecting
  calls; physical length guards preserve checked virtual-container fallbacks
  (D6.3.2, D8.2.6).
- Self-tail lowering accepts safe boxed scalar versions, replacement records
  and string builders, evaluates all replacement arguments before updating
  bindings, and preserves ownership exclusions. A final string append may
  borrow its owned buffer; ordinary observations still freeze it
  (D6.2.1, D7.3.1, S9.1.2–S9.1.3).
- Primitive nullable literals stage each member once, then choose packed
  present storage or generic null storage; NaN remains distinct from absence.
  Construction hints also reach implicit returns. Homogeneous private append
  builders infer their primitive lane only after auditing every push;
  nullable/mixed builders and later escaped widening keep checked semantics
  (D4.3.1, D4.3.3).
- One shared projection-use audit enables local array/record scalarization
  and short pure array-result recipes. The latter guard actual source extents
  once, with the ordinary checked call for short inputs. Escapes, reassignment,
  duplicate/missing record keys and incompatible uses prevent scalarization
  (D8.2.6). Whole-binding array facts audit every replacement and borrow.

Contract updates retain native positive witnesses and checked negative
controls. MIR label captures (`{{l:name}}`, `{{l!name}}`) bind actual backedges
and cold joins; their matcher tests reject wrong labels and mixed hexadecimal
suffixes. Literal-only scalarization and closed-call inference are tested
with varied inputs, a rejected record field input and an actual allocating
`split` RHS. New controls cover structural membership mutation, nullable typed
path roots, mixed-prefix repacking and escaped builder COW/widening. Revised
goldens accompany intentional fixture changes; the mixed-record regression's
golden remains unchanged (D8.6.1, S9.1.2–S9.1.3).

The overflow unit uses an existing non-tail negative fixture; the older
duplicate also has a non-tail recurrence now that MVP optimizes its former
self-tail. Profile units select named MVP bodies and current native length
operations, and account for inlined nullable producers and printing controls.
No unrelated admission/string-copy ceilings are raised.

Emission budget review (D8.6.1), measured on private darwin release:

| Probe | Module instructions | Reviewed cause |
|---|---:|---|
| Call-site inference | 557 → 599 | +42 guarded native scalar entry instructions; checked sibling and frame caps retained |
| Prettier AST | 29,089 → 29,651 | pointer-array carrier/certificate admission guards; `print_node` adds 20 guards (+360), with the same helper-call population |
| Array regions | 2,494 → 2,498 | packed nullable construction alternative; main adds two root stores and one static safepoint, with one allocation on either runtime arm |

Only those reviewed caps change; budgets retain zero slack. Wide-literal
emission passes its existing frame/instruction caps after removing unnecessary
loads and scalar homes. Debug and non-macOS observations
are not qualified by these release results. No Lambda/Radiant baseline,
performance campaign, sibling/vendor source edit or shared executable
replacement occurs in this follow-up.


Current qualification, private release SHA256
`6bd984ed27c89b1971094c0878fabfdf0c0074857c932f8a22d0208ff16e860c`:

| Unit gate | Result | Receipt under `temp/mvp_remaining49/` |
|---|---:|---|
| Complete emission controller | 582/582 | `complete-emission.json` |
| MIR emission contracts | 285/285 | same report |
| Semantic goldens | 285/285 | same report |
| Forced GC + poison, all goldens | 285/285 | `complete-forced-goldens.json` |
| Seeded GC + poison, all goldens | 285/285 | `complete-seeded-goldens.json` |
| Optimization contracts | 51/56 | `complete-opt.json` |
| Lambda emission/frame ratchets + config | 18/18 | `complete-ratchet.json` |
| Shared JS capture regressions | 8/8 | `complete-js-regressions.json` |

Seeded collection uses seed 1847 with one-in-three allocation checks; forced
collection uses `LAMBDA_GC_FORCE_EVERY=1`, both with freed-memory poisoning.
`complete-identities.json` records the qualified source and private binaries.
`complete-comparison.json` records no new failures against the preceding
optimization report. Five previously failing optimization units are now green:
checked scalar tail frames, read-only length roots, nullable literal
construction, stable lengths after GC and linear string-tail copying.

Five pre-existing optimization failures remain, with their assertions intact:

| Contract | Current observation |
|---|---|
| `ImmutableArrayConsumerAdmitsOnce` | JIT child exit 139 (LR07-62) |
| `BorrowedNumericArrayRetainsStoreLane` | one checked store, expected zero |
| `RecursiveUnionFieldsReuseAdmittedContract` | 213 admissions, required below 40 |
| `OpenUnionMemberShapeCachesAdmissionProof` | 198 cache hits, required above 200 |
| `TypedHyphenUsesStringSpansAndOneTableAdmission` | 19,275 appends, required below 15,000 (LR07-64) |

These are separate remaining optimization work, not emission fixture failures.
The string-tail part of LR07-64 now records 125 in-place appends, 370 copied
bytes and four freezes without changing its cost ceilings. No wall-clock
performance claim follows from these unit counters.

## Budget review (D8.6.1)

These are first MVP budgets, rather than a claim that old Full emission costs
were preserved. MVP's guarded native/checked bodies, inlining and precise
result/home ownership differ. The measured default budget is shared by the
identical darwin release/debug emission observations for the original twelve
probes; debug execution of the wide-tail probe still traps. Other platforms
receive no slack and need explicit qualification when their emission differs.
The seven JS probe objects are unchanged. Tests never write the budgets file.

| Existing probe | Previous Full default instructions | MVP instructions |
|---|---:|---:|
| Scalar home | 251 | 317 |
| Wide tail | 266 | 383 |
| Sized boxing | 57 | 114 |
| Closures | 2,350 | 5,073 |
| Float arrays | 1,849 | 3,839 |
| Nested COW store | 166 | 418 |
| Typed array guard | 1,461 | 1,681 |
| Call-site inference | 602 | 557 |
| Noncollecting libm | 541 | 695 |
| Cube3d | 19,072 | 23,903 |
| Deltablue | 14,646 | 22,557 |
| Prettier AST | 21,950 | 29,089 |

The five new region probes additionally pin every emitted function and its
roots, inserted root stores, scalar homes, number scratch and safepoints.
Numeric loops/divmod remain rootless with no scalar homes; array and record
misses retain their checked frames; closure and wide-value crossings retain
owned homes. These ceilings add zero slack to the observed MIR.
