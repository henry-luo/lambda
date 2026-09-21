# JS Tune14 — Shared native regions and cheaper ordinary operations

**Version:** 1.4.28

**Date:** 2026-09-21

**Status:** IN PROGRESS — a T14-1 Reference-capture correctness repair, the first
T14-2 numeric slice including both typed-view FFT operand orders, a T14-3
shared physical-address migration, guarded Int32Array parameter admission and
three isolated quicksort changes, plus T14-6 cache/prebuild lifetime and
direct-scope scaling/stability repairs are implemented. T14-0 now has a frozen
current 63-row contract, executable Navier semantic oracle and a dynamic generic
helper census. T14-2 also fuses the redundant pre-update `ToNumeric` call when
an update result is provably discarded, takes shared Number-only bitwise
operations before `ToNumeric`, and shares a primitive-Number relational head
between the boxed and raw comparison facades. T14-3 now admits a present ordinary
dense element through a named-property companion without bypassing holes,
numeric descriptors or scalar-home ownership. Its Navier candidate has a
bounded, oracle-backed paired-release gain. The quicksort and FFT slices have
positive paired-release evidence; the T14-2 Number-update leaf, T14-3
copy-store, companion raw-receiver leaf and over-broad FFT experiments were
measured and rejected. T14-4's shared-hoister audit established that mutable
array storage needs a region proof rather than scalar-call hoisting. T14-5 now
removes impossible receiver-specific setup from ordinary Map named reads and
accepts a guarded recursive literal-return recipe, with bounded Richards and
binarytrees paired-release gains. Its base-class constructor recipe now admits
effect-bounded dynamic RHS values through the shared `TypeMap` transition path,
while refusing receiver-observing RHS values and direct `eval`. C14 now has a frozen release binary,
same-session 63-row control and three-engine Navier oracle; the C14 LambdaJS /
QuickJS geomean is 1.120086, while MVP is intentionally unavailable to release
measurement. T14-7 now reuses immutable definition source across MIR closures
with an accepted Havlak replay. The remaining T14-0 through T14-8 outcomes
remain open; this is not a performance acceptance record.

**Source audit:** `c5052085ce91026edd94c50cf89c4fd0e539c2ec`.

**Scope:** full LambdaJS MIR Direct compilation, its runtime operations, and
shared compiler/runtime primitives where a measured cause requires changes.
Untyped Lambda is the first reuse target and a required co-consumer of new
common physical machinery; §3.2–3.5 specifies the source audit and migration.
This implementation plan lives at the owner's requested `vibe/jube/` path.
`T14-*` identifiers identify work packages, not new normative rulings.

**Predecessors:** [JS Tune13](JS_Tune13.md),
[Tune13 implementation record](../impl/JS_Tune13_Impl.md),
[Result46](../../test/benchmark/Overall_Result46.md), and
[Result47](../../test/benchmark/Overall_Result47.md).

## 1. Objective and implementation decision

Make common full-JavaScript operations small enough that native execution
consistently beats QuickJS across the canonical benchmark population and
approaches the synchronous MVP's performance, while preserving full JS semantics.

The next unit of optimization is a **complete native region**: entry facts,
local values, indices, loads, arithmetic, comparisons, updates, stores and exits
retain their useful representations together. Merely emitting a native function
variant, adding a fast helper, or removing one runtime query does not meet this
objective when surrounding operations still box, resolve names, publish roots
and call generic helpers on every iteration.

The implementation order is:

1. Establish a fresh fixed control and repair the confirmed Navier regression.
2. Complete Number inference and native local updates.
3. Complete ordinary/typed array regions and reuse proofs within valid effects.
4. Broaden ordinary fields, NameId continuity and cheap property inspection.
5. Finish measured compiler-memory/startup work inherited from Tune13.
6. Address remaining built-in, call and allocation costs when profiles support
   them, then close the full correctness and performance gates.

This plan changes no semantic, execution-policy or ownership ruling. It uses
the existing MIR backend, `Item`, `MirValue`, `FnAnalysis`/`FnVariantAnalysis`,
indexed binding facts, helper catalog, property kernels and precise-root model.

### 1.1 Authority

| Formal authority | Binding constraint |
|---|---|
| **S1.11**, **D1.3v3** | LambdaJS retains ECMAScript coercion, identity, property, evaluation-order and exception behavior. Shared physical machinery does not import Lambda semantics. |
| **D2.4.1–D2.4.3**, **D3.3.2v2** | Native representation requires sound facts or a noncoercing guard. Entry specialization is separate from source and result contracts. |
| **D3.3.3v3** | Binding-local element facts do not become an unproved global promise about an aliased container. |
| **D3.4.4v2**, **D4.6.1v3–D4.6.2v2** | Preserve context-owned NameId identity. Do not use String pointer identity or persist arbitrary dynamic IDs in generated code. |
| **D5.3.1–D5.3.5**, **D5.4.1** | Precise roots, scalar owners, safepoints and activation/argument lifetimes remain authoritative. |
| **D6.2.2v2**, **D8.4.3v2** | Keep the callable-entry contract and explicit completion/error propagation. |
| **D8.1.3v11** | Selected JS MIR executes native code. AST is an explicit backend; no hidden MIR interpretation or selected-MIR replay through AST. |
| **D8.2.3–D8.2.6** | Extend existing indexed analysis and common physical lowering. Keep profile semantics distinct and avoid duplicate fact owners. |
| **D8.4.1v2** | Immutable predicted specialization, inline guards and a shared semantic miss; no mutable property/call-site caches or feedback vectors. |
| **D8.6.1–D8.6.3** | Zero-slack MIR ratchets, finalized artifacts and dynamic forced-GC rooting oracles. |

Sources: [formal design](../../doc/Lambda_Formal_Design.md),
[formal semantics](../../doc/Lambda_Formal_Semantics.md), and
[documentation convention](../../doc/Doc_Convention.md).

## 2. What Results 46/47 establish

All workload figures below are medians in milliseconds. Aggregate execution
ratios use equal logarithmic weight for the same 63 canonical rows.

| Observation | Result |
|---|---:|
| Result46 full JS / QuickJS geometric mean | 1.464372x |
| Result47 / Result46 full-JS geometric mean | 0.786940x |
| Result47 / Result46 Node geometric mean | 0.808804x |
| Full-JS change normalized through Node | 0.972968x |
| Full-JS median sum, Result46 → Result47 | 163,162.383 → 135,314.986 ms |
| Result47 JS / Result46 QuickJS, different sessions | 1.152373x |
| Same comparison normalized through Node, proxy only | 1.424787x |
| Result47 JS / historical MVP acceptance session 2 | 1.683102x |

The 21.3% raw geometric improvement is not an attributed compiler speedup:
Node improved 19.1% in the same comparison. Node normalization is a sensitivity
check, not an exact correction. Result47 did not rerun QuickJS. Its `pidigits`
and `crypto_sha1` Lambda cells were separately refreshed with a repaired binary.
Do not publish a current QuickJS gap by relabeling Result46's cells as Result47.

The historical MVP used a narrower semantic surface. Its frozen manifest now
rejects 57 source hashes. The audited acceptance-commit-to-Result46 changes in
the selected JS sources replace `process.hrtime.bigint()` timing with
`performance.now()`; those diffs do not change workload algorithms. This still
requires an explicit new manifest and fresh comparison, not an overwritten v1
acceptance record. Selected JS sources did not change between Results46 and 47.

### 2.1 Priority workloads

QuickJS and MVP columns in this table are historical comparators, not a
same-session Result47 comparison.

| Workload | JS46 | JS47 | QuickJS46 | MVP session 2 | Why it matters |
|---|---:|---:|---:|---:|---|
| `jetstream/navier_stokes` | 213.933 | 565.384 | 99.084 | 57.513 | Confirmed 2.646x archived-binary regression; numeric/indexed code expansion. |
| `larceny/diviter` | 16,262.958 | 12,688.846 | 33,091.976 | 688.598 | 9.38% of JS47 median sum; incomplete Number inference and updates. |
| `text/text_search` | 57,560.615 | 53,625.015 | 38,949.416 | 22,254.900 | 39.63% of median sum; character-code array loops remain partly generic. |
| `awfy/havlak` | 22,277.617 | 14,336.525 | 4,446.140 | 7,719.698 | 10.59% of median sum; object/name/field coverage. |
| `text/microdiff` | 1,346.326 | 825.140 | 163.411 | 173.476 | Property/enumeration/name work across many small operations. |
| `larceny/quicksort` | 179.679 | 133.237 | 23.017 | 32.227 | Int32Array, comparisons, swaps and native loop updates. |
| `r7rs/fft` | 6.205 | 3.805 | 3.071 | 1.163 | Native function still contains boxed stores and arithmetic. |
| `beng/regexredux` | 10.567 | 8.826 | 6.794 | 0.688 | Built-in/protocol residual; profile after core improvements. |
| `text/hyphen` | 843.169 | 100.593 | 101.624 | 87.595 | Preserve the large win; separately measure compiler and allocation residuals. |

`text/log_pipeline` is another 10.56% of the JS47 sum, and
`text/three_way_merge` is 7.32%. The largest five rows account for 77.49%.
Absolute time and broad geometric improvement therefore require different
views of the same population. A 2x win on one row improves the 63-row geomean
only about 1.1%; a 2x win on 30 rows improves it about 28.1%.

### 2.2 Verified structural gaps

Fresh MIR diagnostics used the archived release binaries, not a debug timing
build. Dump-enabled diagnostic elapsed times are not performance evidence.

| Diagnostic | Observation | Interpretation limit |
|---|---|---|
| `diviterDiv` | Native signature has boxed `x`, F64 `y`; loop calls `js_cmp_raw`/`js_subtract`; native `q++` calls `js_to_numeric`, `js_increment`, `js_to_number`. | Establishes executed-loop helper structure, not a measured speedup from a proposed fix. |
| `diviterMod` | Only a boxed body is emitted. | Specialization/refusal must be explained with binding facts. |
| Search functions | `naiveSearch`/`boyerMooreSearch` remain boxed; `kmpSearch` has a native variant with generic length/index/arithmetic work. | Character-code arrays are the hot input; do not attribute this to Unicode string decoding. |
| FFT `four1_n` | Eight `js_typed_array_set_numeric_key` sites; generic arithmetic remains despite available Number-store machinery. | Static sites include misses; obtain dynamic reachability before attributing time. |
| Navier `lin_solve2_body` | 2,407 → 6,293 finalized instructions; 115 → 189 static call sites. | Expansion and conversions are suspects, not an isolated root cause. |
| Navier `project_body` | 3,077 → 6,884 finalized instructions. | Measure native code size/spills and runtime helper work separately. |

The archived 31-pair Navier replay has a 2.646183x candidate/control median
ratio, 31 valid timing pairs and matching binary/source hashes. Its normalized
stdout is empty: the canonical one-frame wrapper does not reach the source's
frame-15 checksum. This confirms repeatable elapsed-time regression but is weak
semantic-equivalence evidence. T14-0/1 must strengthen the result oracle before
accepting a fix without changing the canonical timed workload.

### 2.3 Navier frame-15 Reference repair

The canonical JetStream source checks its result only after frame 15. A wrapper
that runs those 15 frames exposed a MIR checksum of 29, while Node and the AST
backend produce the required 77. The first `project()` pressure solve diverged
in its initial `lin_solve()` iteration even though the pre-solve aggregate state
matched. The one-frame timed workload remains unchanged; this is a semantic
oracle and no new timing claim.

The cause was a live native MIR register retained as a computed member-write
key. In `lastX = x[currentRow] = ... x[++currentRow] ...`, RHS evaluation
updated the local backing `currentRow` register before the deferred store used
the Reference. Capturing the key manually in source restored the result, which
identified the missing compiler ownership boundary. The fix makes every member
update retain its evaluated receiver/key and makes plain `=` retain each direct
local source only when the RHS can rebind it. The existing indexed assignment
census proves direct writes; a small RHS walk retains the snapshot for calls
through a shared closure environment or direct `eval`. Unknown, dynamic and
`with`-scoped sources retain the conservative path. This preserves the
evaluated Reference through later coercion, calls or local writes as required
by **S1.11** and the shared physical-lowering boundary in **D8.2.3–D8.2.6**,
without adding moves to ordinary literal/arithmetic stores whose source
compiler homes cannot change.

`JsOpt.NavierStokesWriteReferenceSurvivesRhsIndexUpdates` reads the canonical
benchmark, appends only the 15-frame driver, and retains the benchmark's own
checksum. `JsOpt.WriteReferenceRetainsClosureMutation` separately covers a
pure native-key increment and a call that mutates a captured key before the
store. The canonical test has a 120-second local child limit because this
complete debug-host workload takes tens of seconds; recent focused and full
contract runs took 28.1 and 26.2 seconds. All other fixture limits remain 30
seconds. The full 55-test JS optimization-contract suite
passed after the narrowing. Its finalized-MIR ratchet returns
`js_corpus_array_methods` from the provisional 3,253 to its prior 3,237
instructions: ordinary literal/arithmetic RHS stores no longer pay Reference
copies. The update-heavy Tune6 fixture retains its nine receiver snapshots,
which are required before its observable get/coercion/write steps.
Final source validation passed `make test-lambda-baseline` at 5,687/5,687 and
`make test262-baseline` at 40,261/40,261, with zero Test262 batch-unstable,
slow, failed or baseline-regressed entries. The deterministic finalized-MIR
ratchet therefore records only the nine required receiver snapshots in the
affected profile; this is a correctness-repair cost, not a timing acceptance
claim.

### 2.4 Tune14 current-corpus and Navier semantic control — 2026-09-20

`test/benchmark/js_tune14_manifest_v1.json` freezes the live 63-row JS
population without modifying the historical MVP acceptance manifest. It records
each selected source and input hash plus its direct or JetStream wrapper
descriptor. `verify_js_mvp_manifest.py --manifest
test/benchmark/js_tune14_manifest_v1.json` regenerates the descriptors from the
standard runner and rejects any source, input, loop-bound, timing-boundary or
oracle drift. The default verifier still audits MVP v1; its current-source hash
mismatch is intentional historical evidence, not a reason to overwrite v1.

The Navier descriptor retains exactly one `runNavierStokes()` call inside the
canonical timer. After the `__TIMING__` marker it executes the remaining 14
frames, so the source's existing frame-15 checksum must succeed, then hashes
every exposed density entry after `floor(value * 1000)`. Node establishes the
expected marker `__NAVIER_DENSITY_DIGEST__:-257786486`; LambdaJS must emit the
same marker. This observes substantially more of the fluid result than the old
empty one-frame stdout without moving source work into the timed interval.
The shared trailer is available to the Node, LambdaJS and QuickJS JetStream
wrappers, and the paired runner can require it on every Navier process with
`--jetstream-post-timing-oracle navier_stokes_density_v1`. This preserves the
source evaluation and result obligations in **S1.11**, while retaining the
selected native backend requirement in **D8.1.3v11**.

`verify_js_tune14_navier_oracle.py --lambda ./lambda.exe` passed against Node
and the current release. It verifies one timing marker and the density marker
from both engines; a frame-15 checksum failure exits before the digest. A
one-pair same-binary paired-runner smoke also recorded matching normalized
stdout and required markers in
`temp/tune14/paired_navier_oracle_smoke.json`. This validates the control path,
not a performance result. C14, the same-session full engine matrix, original
R46/R47 replay under this oracle and the operation census remain open.

#### Archived-control replay — 2026-09-21

The new oracle now writes a compact JSON artifact containing the manifest and
source hashes, executed binary hash, return code, timing-marker count,
density-marker status and normalized output hashes. Its replay changes the
meaning of the historical Navier timing result: Result46
(`0ab480b8…33a9c80`) exits at frame 15 and lacks the density marker, whereas
the original Result47 (`80b287f3…08fc509`) passes Node and LambdaJS with one
timing marker and the expected digest. The separately archived
`lambda-v47-fc0755a79d-repair` (`b3a562b6…5bf687fb`) also exits at frame 15.
The retained current left-typed release (`2bd599a5…e2c7a22`) passes.

The exact records are `temp/tune14/navier_oracle_result46.json`,
`navier_oracle_result47.json`, `navier_oracle_result47_repair.json` and
`navier_oracle_current_left_typed.json`. Therefore the historical one-frame
R47/R46 ratio remains a repeatable elapsed-time observation, but it is not a
semantic-performance comparison: its R46 control fails the now non-vacuous
oracle. Do not use it to accept a recovery or attribute a regression. R47 and
the current release form valid semantic controls; a future historical recovery
reference must also pass the same frame-15/density check (**S1.11**,
**D8.1.3v11**).

### 2.5 C14 release control — 2026-09-21

[`C14_Manifest_20260921.json`](../../test/benchmark/js_mvp/tune14/C14_Manifest_20260921.json)
freezes the current release executable at
`4b140f0c150d631033f896290c051e3536dbfa429d6341b26ed6f941def5d127`,
derived from commit `5ce9894d4728d6d8df728c7c59ca4472fadb9d45` plus the
recorded dirty source patch. Its three independent copies — live executable,
C14 archive and standard-runner cache — matched before and after collection.
The host was on AC power. The current 63-workload manifest verified before the
run and the same-session Test262 gate passed 40,261 / 40,261 with no unstable,
slow or regressed entry.

The generated [C14 matrix](../../test/benchmark/js_mvp/tune14/c14_session1.json)
and [report](../../test/benchmark/js_mvp/tune14/Overall_C14_Session1.md) contain
three samples per row. MIR, typed MIR, C2MIR, LambdaJS, QuickJS and Node each
have 63 `ok` rows. LambdaJS is 8.09x Node and QuickJS is 7.23x Node by their
63-row matched geomeans; the direct LambdaJS / QuickJS geometric ratio is
1.120086. This is a control observation, not a cross-session Result46/47
comparison or a Tune14 acceptance claim.

All 63 MVP cells are explicitly `exit_9`: production builds compile the
experimental MVP selector out and report “MVP runtime is available only in
debug builds.” AGENTS.md prohibits debug builds for performance measurement, so
no artificial debug-MVP timing is substituted. The current `G(full JS / MVP)`
milestone therefore remains unverified, while the full JS and QuickJS values
remain valid controls. This preserves the full-JS semantics and selected-native
backend requirements in **S1.11** and **D8.1.3v11**, rather than narrowing the
release engine to match a debug-only comparator.

The original collection ran every workload successfully but then failed before
serialization because `run_benchmarks.py` assumed its requested output parent
existed. The generic runner now creates that parent before time or memory work
begins; syntax and nested-output checks passed before the retained rerun. The
first attempt has no result JSON and is invalid evidence. The retained
[C14 Navier oracle](../../test/benchmark/js_mvp/tune14/C14_Navier_Oracle_20260921.json)
passes Node, LambdaJS and QuickJS with one timing marker and the required
post-timer density digest. It extends the verifier by reusing the standard
QuickJS wrapper, so all three engines execute the same manifest descriptor.

## 3. Tune13 carryover: retain the unfinished outcomes

**Yes: substantial Tune13 work is worth carrying forward.** Carry the remaining
outcomes and validation debt, not a fresh implementation of mechanisms already
present. Tune13 remains a partial historical implementation record; creating
this plan does not mark it complete.

| Tune13 package | Already present / retain | Outstanding work worth carrying | Tune14 owner and priority |
|---|---|---|---|
| T13-0: controls and census | Historical matrices, opt-event infrastructure, focused profiles. | Actual starting-tree control; fresh full-JS/MVP/QuickJS population; correct source/binary provenance; dynamic refusal/miss and phase/owner census. | T14-0, required first. |
| T13-1: native-only MIR | Main native compilation paths and JS `--mir-interp` rejection. | Full entry/lifetime matrix and actual-backend evidence. Audit the remaining document-module size branch described below. | T14-0 inventory and T14-8 conformance gate. |
| T13-2: compiler scaling | Call-only root liveness storage; compact admitted numeric-array literal construction. | Quantify remaining interference/IR/native-generation peaks; scaling evidence, retained-context lifetimes and measured initializer expansion. | T14-6, required census and disposition; implement diagnosed costs. |
| T13-3: native arrays | Parameter/local dense candidates; typed read snapshot; guarded Number-store leaf; native variants. | End-to-end producer/consumer coverage, native updates, mixed parameter facts, missing typed kinds, real search/FFT hits. | T14-2/3, highest tuning priority. |
| T13-4: reuse metadata | Dense `reduce` read and pre-rooted callback span with revalidation. | General effect-bounded length/data/receiver witnesses; correct alias kills and reuse across several operations. | T14-4, high priority. |
| T13-5: names/descriptors | One-pass classifier, fewer catalog probes, one outer dynamic-key canonicalization, ordinary enumerability inspection, lazy pending-function metadata. | End-to-end NameId continuity, remaining conversion/miss census, enumeration coverage and broad workload evidence. | T14-5, high priority. |
| T13-6: fields/calls | Predicted field infrastructure, light MIR calls, receiver-only bound-argument forwarding. | Constructor/returned-record field coverage and native field consumers. Further call changes need separately attributed cost. | Fields in T14-5 required; further call tuning in T14-7 conditional. |
| T13-7: strings/RegExp | Existing bulk paths and URI-cache admission narrowing. | Reprofile regexredux/revcomp/Hyphen; immutable classifier ownership and UTF-16 indexing only if measured. | T14-7, conditional implementation with required disposition. |
| T13-8: ownership/closeout | Intrusive external-array ownership and a narrow GC-data allocation path. | Allocation/GC attribution, regression resolution, full baselines, paired/full matrix, cold/RSS evidence and cleanup. | T14-6/7/8; final gates mandatory. |

The current `transpile_js_module_to_mir` still computes
`document_ast_too_large` from document context and `mir_large_interp_enabled()`
and joins it with explicit AST selection before invoking the AST module
executor. The source branch is at
`lambda/js/js_mir_module_batch_lowering.cpp:4194` in the audited tree.
Trace effective selection and exercise this path. If it redirects a selected
MIR unit, resolve that implementation conformance gap under **D8.1.3v11**;
do not silently weaken the formal rule or claim T13-1 complete from the normal
CLI path alone. No backend-policy change is proposed here.

### 3.1 Work that should not restart

- The native Map/Set insertion-order second search is already removed.
  `JsCollectionEntry` indexes the stable order node. Keep its semantic/GC tests.
- Numeric `sum`/`fib` specialization and ordinary Number helper heads already
  exist. Preserve these controls while extending coverage elsewhere.
- Dense parameter/local admission already exists despite historical helper
  names containing `array_literal`; inspect behavior rather than naming.
- Do not repeat unpaired sub-percent adapter experiments as accepted gains.
- Do not adopt MVP's coercing numeric admission or restricted typed-array,
  property or RegExp semantics (**S1.11**, **D2.4.3**).
- An ABI/collector replacement, mutable ICs, interpreter-policy changes,
  speculative OSR/deoptimization, vendor patches and benchmark rewrites are
  outside this plan. An observed residual needs a cause before a new mechanism.

### 3.2 Alignment with untyped Lambda: reuse the optimization, retain the semantics

**Further alignment is a required part of Tune14.** Full LJS and untyped Lambda
already share enough infrastructure that a second JS-specific implementation
of each physical optimization is unnecessary. The useful boundary is:

```text
Lambda admission and semantic rules       ECMAScript admission and semantic rules
                 \                         /
          existing indexed facts and function/variant records
                              |
            immutable physical operation plan + MirValue demand
                              |
        common guards, representation, addressing, calls and root ownership
                              |
                 direct operation on the guarded hit
                              |
          profile-owned continuation for a semantic miss
```

An untyped source parameter may acquire a guarded native representation; it
does not need a user annotation. The reuse target is precisely this transition
from dynamic source values to cheap admitted operations, including safe misses.
Untyped Lambda's benchmark ports are useful controls and sources of compiler
techniques, but different ports/semantics do not establish an achievable JS
speedup or permit transplanting their semantic helpers.

**Already shared, verified in current source:** `AstIndex`, `NameEntry`,
`FnAnalysis`/`FnVariantAnalysis` in `ast-core.hpp`, `CompilerPassManager`,
`MirValue`, `em_require_rep`, `em_call_direct`, the emitter's frame/root/scalar
ownership machinery, `MirNumericOpPlan`, shape candidate/access planning, and
scalar loop-invariant hoisting. Extend these owners; do not introduce another
JS optimization framework, parallel fact database or native-value ABI
(**D1.3v3**, **D8.2.3–D8.2.6**, **D5.3.4**).

The outstanding work is incomplete use of these shared mechanisms plus some
physical emission still embedded in profile code. Sharing a header alone is
insufficient: the useful native producer and consumer must both reach it.

| Area and current source evidence | Reuse decision | Tune14 owner |
|---|---|---|
| Untyped parameter inference: core `infer_param_types_batched`, `prepass_collect_call_sites`, `FnParamEvidence`; JS `jm_infer_param_types` and indexed inference. Common function/variant records already exist. | Reuse indexed identities and records immediately. Converge binding dependency/alias work before extracting common traversal/worklist mechanics; keep evidence interpretation and admission profile-owned. Do not copy core `INFER_*` rules or its recursive walkers into JS. | T14-2; compiler cost in T14-6 |
| Numeric operation selection: `em_numeric_op_plan` in `mir_emitter_shared.hpp` is called by both frontends. | Complete native producer/consumer coverage around this existing plan. Do not create a parallel JS opcode table, arithmetic-tree emitter or special per-benchmark path. | T14-1/2/3 |
| Scalar representation: core `emit_box_float` and JS `jm_box_float` each emit in-band bits, signed-zero encoding and a cold `push_d` call. `em_require_rep` delegates to each profile's converter. | Extract the equivalent nonnullable F64-to-Item physical sequence, migrate both callers, and preserve the common call/result-home protocol. Keep Lambda's nullable lane encoding outside the shared leaf. Audit unboxing separately before merging it. | T14-1/2; size/compile evidence in T14-6 |
| Indexed storage: core `MirIndexLoadPolicy` / `emit_checked_index_load`; JS `jm_emit_packed_array_read_impl` and fixed typed-view loads/stores. Both already use common load/address primitives in some paths. | Split physical storage access from semantic access policy. Share admitted base/length/index/width/load/store emission; keep Lambda contract/null/COW and JS property/view/conversion admission outside it. Migrate a live core caller and a JS caller together. | T14-3 |
| Scalar loop invariants: `em_hoist_loop_scalar_calls` in `mir_loop_invariants.hpp` is already used by Lambda and JS loop lowering. | Extend the existing implementation only for proven additional scalar cases. It deliberately excludes mutable memory witnesses; do not use it as proof that array metadata can survive writes/calls. | T14-4 |
| Dense-loop and local arithmetic-tree versioning: core `mir_prepare_dense_for_guard`, `mir_dense_*`, `mir_local_tree_collect` and checked-index paths. | Reuse the strategy and extract physical extent/guard/emission pieces after two valid clients exist. Current core typed-array/nullability proofs are not JS proofs; unannotated Lambda fixtures must also exercise the shared piece. | T14-3/4 |
| Record candidates: both core `mir_expr_candidate_shape` and JS `jm_plan_predicted_literal_field` use `mir_shape_candidate` / `mir_plan_field_access` from `mir_shape_candidates.hpp`. | Extend the current candidate propagation and access plan. Core already follows parameter/return shapes and considers a unique module shape for a field; reuse that mechanism where profitable with JS-specific validity checks, never a second shape planner. | T14-5 |
| Names and field storage: `NameId`, module property-key images, `em_guard_map_shape`, `em_load_at`, and scalar-home/store ownership. | Keep canonical keys end-to-end and share physical loads/stores after profile admission. A matching TypeMap is insufficient proof of ordinary JS property behavior. | T14-5 |
| Calls, roots and compilation: `em_call_direct`, `em_finish_direct_call_normal`, common root liveness/finalization and pass scheduling. | Diagnose and fix the common owner once. Reduce boxing before changing root handling; preserve emitter-owned lifetimes and truthful helper effects. | T14-2/6/7 |
| Strings, allocation and runtime builtins: shared storage/allocator/name primitives coexist with language-specific operators and protocols. | Reuse a concrete leaf only after matching its input, output, effects and lifetime contracts. Do not forward JS arithmetic, truthiness, equality, property access or regex to Lambda's semantic helpers. | T14-7, profile-gated |

Source owners: [core MIR lowering](../../lambda/runtime/transpile-mir.cpp),
[common emitter](../../lambda/runtime/mir_emitter_shared.hpp),
[common function records](../../lambda/runtime/ast-core.hpp),
[shape planning](../../lambda/runtime/mir_shape_candidates.hpp),
[scalar loop invariants](../../lambda/runtime/mir_loop_invariants.hpp),
[JS analysis](../../lambda/js/js_mir_analysis.cpp),
[JS parameter/variant inference](../../lambda/js/js_mir_function_collection_class_inference.cpp),
[JS expressions](../../lambda/js/js_mir_expression_lowering.cpp), and
[JS boxing/calls](../../lambda/js/js_mir_calls_boxing_types.cpp).

### 3.3 Concrete shared implementation slices

These slices belong to the existing T14 packages, not a separate prerequisite
rewrite. Land each at the point its two clients and measured need exist.

**A. Numeric facts and physical representations — T14-2.**

1. Trace an unannotated Lambda parameter-to-alias-to-update chain alongside
   the `diviter` JS chain. Record current candidate discovery, variant admission,
   actual representation and the first consumer that materializes an Item.
2. Use the indexed binding/use/def relationships for the new dependency
   propagation. If both profiles need the same propagation mechanics, extract
   those mechanics and replace the corresponding existing core path in the
   same slice. Do not require wholesale conversion of core inference before
   fixing the JS coverage gap, or add a speculative framework with one client.
3. Keep joins/admission distinct: Lambda's numeric-use-to-integer default,
   optional/null contracts and exact call-edge policy cannot become JS rules.
   JS starts with a Number guard and F64 semantics; native integer counters
   need their own range proof. Capture/eval/with/missing-argument exclusions
   remain explicit. Candidate evidence never becomes an unconditional fact.
4. Retain effective runtime types on their established AST owners and
   variant-specific facts on `FnVariantAnalysis`; do not publish speculation
   into a generic body or a shared `TYPE_ANY` singleton (**D8.2.5v2**).
5. For boxing, make the extracted physical leaf consume an explicitly F64
   `MirValue`/register. Leave profile normalization and Lambda nullable-sentinel
   handling at the caller. Preserve result publication, scalar provenance and
   exception effects of the cold call. This consolidation prevents divergent
   fixes; its performance value must come from measured emission/coverage
   improvements, not the move to a shared file itself.

**B. Guarded storage access — T14-3.**

The existing core `MirIndexLoadPolicy` mixes physical layout with
`array_contract`, `nonnull_boundary`, null-result selection and `item_at`
fallback. Do not expose that entire Lambda policy as the JS API. Extract only
the common admitted access below it, using existing `em_element_address`,
`em_numeric_storage_type`, load/store and `MirValue` primitives first.

The minimal immutable description should identify:

- precisely rooted receiver/owner and the admitted storage base;
- tagged-slot or numeric storage kind, element width and signedness;
- the already-evaluated index and valid extent, with proof provenance;
- the requested physical result representation and destination;
- an operation-level miss label/continuation which preserves evaluated values.

Language wrappers establish receiver validity and semantic key conversion
before that description can be consumed. The shared leaf emits the remaining
physical bounds/address/load/store sequence and returns a `MirValue`. It does
not inspect a JS prototype or decide that a Lambda null is a JS `undefined`.
Keep operation-local proof lifetime separate from the longer-lived region
witness proposed in T14-4. These are proposed additions to existing helpers,
not claims that a common checked-index API already exists.

First pair an admitted tagged/ArrayNum access from unannotated Lambda with a
guarded ordinary JS-array access. Then extend the same physical leaf to JS
typed views after the JS wrapper supplies its validated base/length and element
conversion. Typed views must not be reinterpreted as Lambda array headers.
Stores retain destination ownership and profile-specific write permission,
widening/COW or view-conversion rules. Use the same machinery for both operand
orders and compound updates instead of adding a new lowering family per shape.

**C. Region proofs and hoisting — T14-4.**

Keep the already-shared scalar hoister for immutable scalar observations.
Its current admission requires a registered pure/loop-stable scalar call,
`NO_GC`, no reentry, preserved exception state, a non-GC scalar result and
preserved number-stack state. Removing one requirement to hoist a JS query is
not an acceptable shortcut (**D5.3.1–D5.3.5**).

For mutable storage, converge a small region witness over owner identity,
storage generation/stability proof, extent and relevant effects. A generation
is useful only if the runtime already maintains it truthfully; do not invent
a new global epoch or require all mutations to gain one for this tuning slice.
Begin with a straight-line or loop region with no unknown mutation/reentry.
Keep Lambda alias/COW kills and JS descriptor/prototype/detach/resize kills
profile-owned. Share the physical dominated-guard and witness-consumption
machinery only after both profiles can supply sound inputs. Unknown effects
terminate the witness conservatively.

Core Tune30's loop/tree versioning is an implementation reference, especially
its guard-before-effects and code-size tradeoffs. It is not a blanket proof
for dynamic JS arrays or a requirement to duplicate whole JS loops. Compare
bounded whole-region versioning with smaller shared access regions using
finalized instruction count, dynamic misses, compile time and paired execution.
Never resume a miss by replaying already-observable effects.

**D. Dynamic records and property names — T14-5.**

Reuse the common shape candidate walker and `MirConstructionPlan` /
`MirFieldAccessPlan`. Extend profile hooks for constructor/return/parameter
facts where the source census shows missed coverage. Candidate selection is
compile-time immutable, and every predicted shape still has an appropriate
runtime guard (**D8.4.1v2**).

The JS wrapper additionally establishes own ordinary data-property admission,
descriptor/writability requirements for a store, and the absence of relevant
proxy/host/accessor behavior. A shape match alone cannot establish those facts.
Share slot addressing, native load/store and scalar ownership; keep ECMAScript
lookup and enumeration ordering in the existing JS property kernel. Carry
context-owned canonical NameIds through that kernel instead of repeatedly
round-tripping through spelling strings (**D4.6.1v3–D4.6.2v2**).

**E. Calls, roots, compilation and runtime leaves — T14-6/7.**

Optimize common call/frame/root finalization in `MirEmitter` and consume it from
both profiles. Native scalar continuity should remove unnecessary Item values
and hence their root work naturally; do not weaken ownership to reproduce a
smaller MVP frame. Attribute root publication, result-home transfer, call
marshalling and compiler interference storage separately.

For an additional runtime leaf, document its full contract and two callers
before promotion. Raw byte operations may be shareable; JS UTF-16 indexing,
String coercion, RegExp protocols and observable hooks stay in JS. Allocator
reuse does not imply interchangeable object layout, finalization or mutability.
Do not introduce a JS-specific copy of a common helper merely to change its
effect annotation; audit the shared effect or split out a truly narrower leaf.

### 3.4 Prevent duplicate tuning work

Each implementation slice must record:

1. **Existing owner:** the core and JS functions searched/read, and whether the
   solution is direct reuse, extension, extraction or justified profile code.
2. **Two-client migration:** for a new common primitive, the working Lambda
   and JS callers, the old physical sequences retired in the same change, and
   the precise profile obligations left at each boundary (**D8.2.3**).
3. **Evidence:** finalized hot-region MIR, semantic misses, lifetime tests,
   code/compile cost and paired release results. A moved block or reduced file
   size is not a performance result.
4. **No duplicate ownership:** facts stay with their existing AST/function/type
   owners; shared plans are immutable. No copied third variant, parallel site
   cache or language-name branch inside the common physical operation.

The prior [P7 proposal](../Lambda_Proposal_JS_Unify_P7.md) explicitly withdrew
U-A's blanket structural lowering driver after examining mismatched layouts
and semantic control flow. Do not resurrect it to claim alignment: matching
AST tags do not prove matching child layouts or evaluation behavior. Its landed
emitter callbacks and the [unified compiler boundary](../Lambda_Design_JS_Unified.md)
are the foundation to extend. This plan shares specific physical operations,
not the full semantic walkers or their completion frames.

[Lambda Tune30](../impl/Lambda_Impl_Tune30.md) supplies live implementation
references; [Lambda Tune31](../impl/Lambda_Impl_Tune31.md) is still marked
PROPOSAL / not started in this audit. Coordinate future common work with that
plan, but do not count proposed Tune31 machinery as available code or as
measured improvement. Prefer the earliest useful shared slice over waiting for
an unrelated full Lambda tuning round.

### 3.5 Acceptance must prove reuse in untyped Lambda

- Add small unannotated `.ls` and `.js` fixtures that reach each newly shared
  physical path: alias/update arithmetic, read-compute-store, mixed/changed
  arrays, parameter/returned-record access and invalidation across a call.
  The `.ls` fixture must not depend on added type annotations to obtain coverage.
- Use a separate semantic oracle for each language. Shared hit paths can have
  comparable MIR, while JS coercion/holes/BigInt/negative zero and Lambda
  null/contracts/COW require deliberately different fallback tests.
- Verify both frontends call the common implementation and that the replaced
  copies are gone. Check the hot region, result representation, owner/root
  lifetime and reachable miss; total MIR similarity alone is insufficient.
- Run affected untyped Lambda release controls against the same predecessor
  used for JS; retain typed Lambda controls because they also consume common
  helpers. Include numeric, array, object and compile-heavy cases selected from
  the existing corpus. Investigate repeatable regressions under §5's policy.
- A useful extraction can be performance-neutral by itself. Accept it for
  demonstrated code reuse and parity, then attribute subsequent speedups to
  the measured extension. Do not assign the Lambda port's full performance
  advantage to the shared component.

These gates supplement the complete Lambda/Test262 closeout in §5. They do not
replace it, and they do not require a semantic or formal-design rule change.

## 4. Work packages and dependencies

| Package | Outcome | Dependencies |
|---|---|---|
| T14-0 | Fixed controls, population/oracle repair, census and backend inventory | None |
| T14-1 | Diagnose the Navier observation and resolve a valid regression | T14-0 |
| T14-2 | Complete Number facts, native local updates and profitable admission | T14-0; preserve T14-1 evidence |
| T14-3 | End-to-end ordinary/typed array native regions | T14-2 |
| T14-4 | Effect-bounded proof and metadata reuse | T14-3 |
| T14-5 | NameId continuity and broader guarded field/enumeration coverage | T14-0; reuse T14-2/3 carriers |
| T14-6 | Measured compiler/startup/memory scaling improvements | T14-0; remeasure after MIR-changing packages |
| T14-7 | Profile-gated built-in, allocation and call residuals | Relevant T14-2–6 packages |
| T14-8 | Native contract audit, consolidation and full acceptance | All required packages and conditional dispositions |

T14-1 investigation comes first, but its fix may be a narrowly isolated portion
of T14-2/3. Do not force a temporary rollback merely to satisfy package order.
Keep each causal experiment against its immediate predecessor and the fixed
starting control. Source, semantic tests and measurements travel together.
Apply §3.2–3.5 within each package: reuse/extraction is part of its delivery,
with explicit untyped Lambda consumers and controls where common code changes.

### T14-0 — Establish controls, meaningful outputs and an operation census

**Primary files:** `test/benchmark/run_standard_benchmarks.py`,
`run_paired_benchmarks.py`, `verify_js_mvp_manifest.py`, existing optimization
events in `lambda/js/js_exec_profile.{h,cpp}`, and helper-effect metadata.

- [~] Build and archive **C14**, the actual current release starting tree. Its
  commit, dirty source patch, binary SHA-256 and AC state are durable in the
  C14 manifest; live, archive and cached executable hashes agree around the
  retained matrix. Record the remaining toolchain/native-module identities and
  backend/cache settings with the phase/owner census. Result47 remains a
  historical reference, not a substitute for this newer-tree control.
- [ ] Preserve Result46, original Result47 and repaired Result47 as distinct
  immutable artifacts. Use original R47 for reproducing its Navier observation;
  use C14 as the primary optimization control. Do not attribute later changes
  to the old commit merely because an archive filename includes that commit.
- [~] Freeze 63 JS sources, inputs, wrappers, loop counts, timing boundaries and
  expected results in a versioned Tune14 manifest. `js_tune14_manifest_v1.json`
  and the parameterized existing verifier now cover the live population while
  preserving MVP v1. Archive its identity with C14 and any published matrix.
- [~] Audit result oracles before trusting equal stdout. The Navier control now
  retains one canonical timed frame, then requires its source frame-15 checksum
  and a Node-calibrated full-density digest after the timer. The C14 archive,
  Node, LambdaJS and QuickJS pass it. Run every later archive through the same
  descriptor before acceptance. Any wrapper change remains versioned and
  applies to every relevant engine.
- [~] Run fresh full-JS/MVP/QuickJS/Node controls in the same session. C14 has
  63 `ok` full-JS, QuickJS and Node cells and records each MVP `exit_9` instead
  of dropping it; release MVP is debug-only and cannot supply valid performance
  timings. Keep native Lambda/C ports as secondary physical-backend references
  and collect a second independent full session for any cross-engine claim.
- [ ] Produce separate execution, parse/analysis/lowering, native generation,
  teardown and peak-memory censuses. Sample the executing worker after startup;
  a compiler/teardown sample is not evidence for guest-loop cost.
- [ ] Extend existing opt diagnostics only as needed: refusal reasons, native
  producer/consumer continuity, guard hits/misses, NameId reconstruction,
  descriptor materialization, repeated metadata queries, helper calls,
  allocations, root stores and safepoints. Distinguish static sites, dynamic
  counts, self samples and inclusive samples. Disable counters for timings.
- [ ] Inventory script, eval, dynamic function, CJS/ESM, document, batch/cache,
  lazy-native and nested-language entries for the T14-8 backend/lifetime gate.

#### Implementation update — 2026-09-21: generic helper census and update target

The profile-only helper events retain the generic semantic entry boundaries for
`ToNumeric`, increment/decrement, boxed relational comparison and a Number-key
element read. They compile to no-ops outside `LAMBDA_JS_EXEC_PROFILE`; their
counts include recursive coercion and therefore are helper invocations, not
source-operator counts. `JsOpt.RuntimeHelperCensusKeepsGenericCoercionAndIndexSemantics`
checks trace-on and trace-off behavior for coercing objects, `BigInt`-capable
updates, relational order, integral Number elements and fractional Number
property keys (**S1.11**, **D8.4.1v2**).

The pre-companion-read source's full 15-frame Navier semantic run recorded
`runtime_to_numeric_call=52,247,639`, `runtime_increment_call=49,129,501`,
`runtime_decrement_call=12`, `runtime_boxed_compare_call=6`, and
`runtime_number_index_get_call=36,190,970` in
`temp/tune14/navier_current_runtime_census.{trace,json}`. This rules out another
generic-comparison experiment for Navier and identifies the remaining ordinary
numeric read as the next T14-3 investigation. The count alone does not prove a
legal array fast path: holes, prototype lookup, accessors and aliases remain
profile-owned admission conditions (**D1.3v3**, **D8.2.3–D8.2.6**).

An isolated runtime experiment that reduced the root frame for canonical
integral Number keys was rejected. It preserved all 31 Navier oracle pairs but
measured 521.941 ms versus 520.181 ms against the discarded-update candidate
(ratio 1.003383; one-sided paired-bootstrap upper 1.005174; 9/31 wins). The
source change was removed; retain only
`temp/tune14/paired_navier_integral_number_root_trim.json` as negative evidence.
This directs T14-3 toward guarded physical read admission, rather than reducing
generic helper bookkeeping without a demonstrated benefit.

**Exit:** C14 and the comparison manifest are reproducible; each priority row
has a non-vacuous correctness oracle and an evidence category: confirmed cause,
structural gap, sampled hypothesis, or not yet measured.

### T14-1 — Resolve Navier's regression without discarding semantic fixes

**Primary files:** `js_mir_expression_lowering.cpp`,
`js_mir_function_collection_class_inference.cpp`,
`js_mir_module_batch_lowering.cpp`, `js_mir_calls_boxing_types.cpp`, and
`lambda/runtime/mir_emitter_shared.hpp`.

#### Structural trace — 2026-09-19

The current release MIR shows that `lin_solve`, `lin_solve2`, `advect`,
`project`, `set_bnd` and `addFields` have only boxed `_body` functions, with
no `_n` variant. Each captures mutable solver state (`width`, `height`,
`rowSize`, `iterations` or related fields), while current native-entry
admission deliberately requires zero captures. Their hot loops consequently
retain `js_to_numeric`, `js_increment`, generic arithmetic and generic ordinary
array operations. This is a structural admission gap, not evidence that the
landed direct-local update regressed Navier. A closure-aware ABI and an
effect-valid ordinary-array region belong to T14-2/T14-3; they must preserve
live captured bindings and ordinary-array semantics (**D2.4.1–D2.4.3**,
**D3.3.3v3**, **D8.2.4–D8.2.6**).

- [~] Reproduce archived R46/R47 with the stronger oracle from T14-0. R46 and
  the archived R47 repair fail frame 15; original R47 and C14 pass. Retain the
  JSON records and do not treat R46 as a semantic recovery control. C14 remains
  a separate starting-tree artifact because later native-fact work can change
  generated code without explaining the historical observation.
- [ ] Diff finalized MIR by function and semantic operation. Count instructions,
  static calls, guard branches, box/unbox sequences, scalar homes, roots and
  safepoints; measure native bytes/spills where available. Focus first on
  `lin_solve`, `lin_solve2`, `advect`, `project` and `addFields`.
- [ ] Profile actual hot paths and determine whether time is spent in redundant
  guards/conversions, fallback frequency, generic operations, root publication,
  native spills/code size, or a runtime helper regression.
- [ ] Bisect or ablate independently while retaining the repaired generic
  BigInt path. Capture a small semantic reproducer for the responsible lowering
  shape. A larger MIR dump alone is not sufficient causal attribution.
- [ ] Fix the admitted operation/region. Keep unknown sites compact; evaluate
  operands once and preserve error/ownership joins. A size/profitability rule
  must use general code/effect facts, never a workload/function-name whitelist.
- [ ] Pair a fix with R47 and C14, plus only a recovery reference that passes
  the same oracle. Run search, FFT, numeric controls and mixed/exotic misses to
  detect displaced cost.

**Exit:** the causal change is identified, the reproducer remains correct, the
2.646x loss is removed or any remaining delta has a measured explanation, and
the full-JS semantic repair remains intact. A remaining unexplained confirmed
regression prevents closing this package.

### T14-2 — Complete Number facts and native local updates

**Primary symbols:** `jm_infer_indexed_node`, `jm_infer_param_types`,
`jm_populate_numeric_binding_facts`, `jm_numeric_binding_type`,
`jm_transpile_update_unary`, `jm_emit_assignment_value`, and existing
`MirNumericOpPlan`/`FnVariantAnalysis` consumers.

**Reuse requirement:** §3.3A. Reuse common numeric operation selection and
variant carriers; converge dependency mechanics with core untyped inference.
Extract equivalent nonnullable boxing only with both live callers migrated.

#### Implementation update — 2026-09-19: shared F64 boxing, native updates and direct assignment-alias evidence

The first numeric slice extracts `em_box_f64_to_item` into the common emitter
and migrates both untyped Lambda's `emit_box_float` and LambdaJS's
`jm_box_float`. The shared leaf owns only the physical non-null F64-to-`Item`
encoding, including the in-band representation and signed-zero case. Its
profile callback performs the cold allocation/publication call, so JS retains
its error/completion tracking and Lambda retains its existing call protocol.
This is a two-live-caller reuse, not a JS wrapper around Lambda semantics
(**D1.3v3**, **D5.3.4**).

`jm_transpile_update_unary` now emits F64 `dmov` plus `dadd`/`dsub` when a
native variant has a direct, non-captured mutable F64 local. It explicitly
refuses environment, module, state, const, TDZ, `with` and non-identifier
cases. Postfix updates copy the old F64 value before writeback. Generic updates
remain the semantic path for coercion, BigInt, member/reference targets and
unproved representations (**S1.11**, **D2.4.1–D2.4.3**).

The indexed collector records the candidate edge from a resolved local LHS to
an RHS resolved binding in compound arithmetic. Its direct-body worklist now
also accepts a simple immediate-body assignment such as `cursor = first` after
`first = x`. It advances in source order, removes that relation after a later
non-alias assignment, and excludes parameters, nested bodies and control-flow
joins. Thus `let first = x; let cursor = 0; cursor = first; cursor -= y` reaches
the native candidate without treating a mutable alias as a permanent fact. This
is still a guarded Number entry: the native wrapper guards both F64 inputs, and
strings, BigInts and every other miss continue through the generic body
(**D2.4.1–D2.4.3**, **D8.2.5v2**).

The worklist remains JavaScript-owned because immediate statement order and
assignment effects are profile semantics. It uses the existing shared
`FnParamEvidence` and `FnVariantAnalysis` carriers, while untyped Lambda keeps
its batched recursive walker. The shared F64 boxing leaf remains the completed
two-live-caller migration; extracting this one-client traversal before a second
matching consumer would duplicate rather than reuse compiler policy
(**D1.3v3**, **D8.2.5v2**).

Focused `JsOpt.NativeNumberUpdatesKeepPostfixAndGenericSemantics` verifies
postfix ordering, `-0`, string coercion, BigInt fallback, trace-off parity and
the finalized native/generic MIR split. `JsOpt.NativeAliasCompoundAssignmentKeepsGenericSemantics`
now covers declarator aliases, direct assignment aliases, string relational
behavior, BigInt fallback and an overwritten-alias refusal. Its finalized MIR
contains `dge` and `dsub` in `assignedSubtract`, while `overwrittenAlias`
retains `js_compare`; the focused suite passed 55/55. The direct-local update slice
alone measured `larceny/diviter` tuned/control ratios 0.744359, 0.742719 and
0.749489 (median 0.744359; 9,625.095 versus 12,959.271 ms) in an alternating
release comparison against an isolated `HEAD` control.

After adding direct alias compound evidence, a separate three-pair alternating
release comparison measured cumulative Tune14 ratios 0.049652, 0.048964 and
0.048967 (median 0.048967; 611.610 versus 12,489.937 ms). Finalized
`diviterDiv` MIR has F64 arguments with `dge`, `dsub` and `dadd`; its generic
body still retains coercive comparison and `js_subtract`. This establishes only
the cumulative `diviter` effect of the current working tree, not an isolated
alias-only delta. Navier remains a separate problem: six one-frame pairs after
the complete slice ranged from about 0.9845 to 1.0019 tuned/control, with a
median near 0.9927. No full fixed-population matrix or QuickJS comparison has
been run.

Comparison and dependency propagation beyond immediate-body aliases, control-flow
joins, native integer range proof, array regions and the Navier root cause remain
open. No benchmark measurement is claimed for the direct-assignment expansion.
After the expansion, `make test-lambda-baseline` passed 5,683/5,683 and
`make test262-baseline` passed 40,261/40,261 with zero non-fully-passing,
failed or regressed tests.

#### Implementation update — 2026-09-21: fuse discarded generic updates

The generic update lowering formerly emitted `js_to_numeric(value)` before
calling `js_increment` or `js_decrement` even when an expression statement or a
`for` update discarded the result. Only an observed postfix completion needs the
old completed numeric value. The lowering now marks `for` updates with the
existing `discarded_expression` demand and calls the complete update entry in
that case. The complete entry performs `ToNumeric`; the new numeric entry is
used only after the observed path has explicitly completed `ToNumeric`. Reference
evaluation, `GetValue`, `PutValue`, ordering, errors and `BigInt` behavior stay
on the existing generic path (**S1.11**, **D2.4.1–D2.4.3**, **D8.2.4–D8.2.6**).

The shared `js_numeric_update` kernel makes the interpreter and MIR paths reuse
the same post-`ToNumeric` operation without changing the interpreter's observed
postfix value handling. The change reuses the pre-existing common `MirValue`
discard demand and `em_apply_value_demand` rather than copying an untyped Lambda
update policy. Untyped Lambda cannot consume the semantic wrapper because it has
no ECMAScript `ToNumeric`, `BigInt`, Reference or postfix-completion contract;
the shared boundary is therefore the existing physical demand mechanism
(**D1.3v3**, **D8.2.3**).

`JsOpt.DiscardedGenericUpdateFusesToNumericAndUpdate` covers string and
object coercion, `for` updates, exactly-once coercion and finalized generic MIR:
the discarded loop has the full `js_increment` call and no separate
`js_to_numeric` or `js_increment_numeric`. The semantic Navier oracle passed in
debug and release. The release candidate
`temp/tune14/lambda_t14_discarded_generic_update_release.exe`
(`eee834af42066e74266ea7687d4524d82d1fa19c4bd1250885ac588ac657a8b0`) was
measured against exact predecessor
`lambda_t14_left_typed_candidate_release.exe`
(`2bd599a5a2e177183e241c6adb29c6be761ba24773d69c61dabbbc446e2c7a22`) in 31
alternating, oracle-checked Navier pairs. Candidate median was 520.684 ms versus
524.845 ms (ratio 0.992072; one-sided paired-bootstrap 95% upper 0.994343;
26/31 wins), with `ok` status, equal stdout and both post-timer density checks
for every sample. The archive is
`temp/tune14/paired_navier_discarded_generic_update.json`. This is a retained
single-row result, not a 63-row, QuickJS or JS-MVP claim.

The full optimizer suite passed 61/61, coercion 15/15, MIR ratchet 20/20,
release Lambda baseline 5,690/5,690 and Test262 40,261/40,261 fully passing
with zero unstable, slow, failed or regressed entries. The only ratchet notices
were independent reductions in Lambda corpus probes; no budget was changed.

#### A. Infer a guarded entry candidate through binding relationships

The landed immediate-body chain covers `r = x; r -= y`,
`first = x; r = first; r -= y` and `let r = 0; r = first; r -= y` when resolved
bindings lead back to formals. A later non-alias direct write clears the last
relation. It does not propagate through control-flow joins, nested scopes or a
general alias graph. The solution remains a candidate native entry guarded for
Number inputs, not treating these operators as proof that every source call
receives Numbers.

- [~] Express numeric-use dependencies using resolved binding identity and the
  existing function-owned index. Immediate-body source-ordered declaration and
  assignment aliases, including direct-write invalidation, are implemented;
  propagate comparisons, joins and other dependencies with a bounded fixed
  point and explicit refusal on unsupported/ambiguous joins.
- [~] Establish all required Number guards before specialized effects. The
  current edge relies on the existing native-entry F64 guards; preserve
  the exact original values on the generic entry, including missing arguments,
  strings, BigInt, Symbol and objects with coercion hooks.
- [~] Track initialization and every reaching write. Immediate-body direct
  writes now invalidate the local relation; control-flow, capture, `eval`,
  `with` and unknown effects still prevent or require a richer proof. A mutable
  alias is not a permanent type certificate.
- [ ] Keep variant-local facts with the guarded variant. Do not restore the
  pre-repair publication of speculative F64 facts into the generic boxed body.
  Separately derived unconditional facts may apply there only with their own
  sound proof (**D3.3.2v2**, **D2.4.1–D2.4.3**).
- [ ] Broaden admission beyond numeric return types where local regions justify
  it. Explain zero-numeric-parameter and capture-bearing refusals; admit only
  effect/entry shapes supported by the existing ownership contract, rather than
  removing eligibility restrictions globally.

#### B. Emit native updates and consume native facts consistently

- [x] Add a proved-Number local `++`/`--` arm before the generic reference path.
  Use F64 add/subtract with JS Number rounding; preserve the old numeric value
  for postfix and the new value for prefix. Respect const assignment and TDZ.
- [ ] Reuse common native assignment/writeback machinery. Keep member updates,
  accessors, unresolved/with bindings and unproved values on their full semantic
  path; do not duplicate reference evaluation or coercion.
- [ ] Preserve negative zero in postfix results, NaN/infinities and behavior
  around 2^53. A native integer induction lane requires a separate range proof;
  C integer overflow is not a JS Number implementation.
- [~] Ensure arithmetic/comparison/compound-assignment consumers use the same
  variant facts. Direct alias compound assignment now does so; do not box a
  native local solely because one consumer reads a less precise AST type. Keep
  result/error representations explicit.
- [ ] Use the existing definite-initialization proof to remove only redundant
  TDZ checks. Keep declaration identity, shadowing and loop joins correct.

**Fixtures:** prefix/postfix values, discarded updates, shadowed aliases,
Number/non-Number calls to the same function, changing types, const/TDZ,
captured updates, `eval`/`with`, coercion throwing once, BigInt, signed zero,
NaN/infinities and large Number rounding. Add a compact `diviter`-shaped fixture
whose hot loop's MIR excludes generic arithmetic/update helpers.

**Targets/controls:** `diviter`, `mbrot`, search induction and quicksort;
preserve `sum`, `sumfp`, `fib`, `fibfp`, `mandelbrot`, `collatz` and `pidigits`.

**Exit:** both diviter kernels have appropriate guarded native coverage, the
proved loop updates/arithmetic stay native, generic calls preserve full JS
behavior, and paired release evidence supports the improvement.

### T14-3 — Complete ordinary and typed array regions

**Primary symbols/files:** `jm_is_array_literal_candidate`,
`jm_emit_packed_array_read_impl`, `jm_emit_fixed_typed_array_load_number`,
`jm_emit_fixed_typed_array_number_store`,
`jm_try_emit_right_typed_array_number_binary`, `js_typed_array.cpp`, and existing
dense/typed store helpers in `js_runtime.cpp`.

**Reuse requirement:** §3.3B. Split the physical portion of the core checked
index path and pair its Lambda consumer with the JS access consumer. Keep
semantic access policies outside the shared leaf; extend existing address and
storage primitives before adding another helper.

#### Implementation update — 2026-09-19: core physical addresses use the existing shared leaf

`em_element_address` was already the live common physical leaf for LambdaJS's
fixed typed-view and packed-array reads. Its callers establish JS receiver,
key, prototype, typed-view and conversion semantics before the leaf receives a
base pointer, machine index and width. Rather than add a second access-plan API,
the untyped core now routes its already-admitted checked dynamic load, both
dense `emit_checked_index_load` arms and certified pointer-array load through
the same leaf. The caller still owns every array contract, null/OOB result,
`item_at` fallback, COW and effect decision; this change shares only the
post-admission address calculation required by **D1.3v3** and **D8.2.3**.

The common width-eight spelling is `lsh index, 3` followed by `add base,
offset`; non-eight widths retain the physical multiply. Exact MIR checks now
pin that spelling across typed, dense and pointer storage. In particular,
`tune21_index_mul_hoist` still requires its independent matrix `mul i, n`, then
requires `lsh ..., 3` only for the final element address. The full MIR-emission
suite passed 163/163, the MIR ratchet passed 20/20 (with only its unrelated
existing corpus-shrink notices), and the 55/55 LambdaJS optimization suite
passed. This is a two-client code-reuse migration, not an end-to-end JS array
region or a performance claim; no benchmark result is attributed to it.
Post-migration `make test-lambda-baseline` passed 5,683/5,683 and
`make test262-baseline` passed 40,261/40,261, with zero non-fully-passing,
failed or baseline-regressed Test262 entries.

#### Implementation update — 2026-09-20: guarded Int32Array flow reaches quicksort `partition`

`Int32Array` now joins the existing Uint8/Float64 typed-view lowering through
one element-layout table: four-byte storage uses `MIR_T_I32`, a signed physical
load, then `i2d` for JavaScript Number consumers. The existing runtime
`js_typed_array_set_number_if_kind` remains the sole ToInt32 implementation, so
truncation and modulo-2^32 wrapping were not copied into MIR. This keeps the
shared address leaf physical while the JS runtime retains view-conversion policy
(**D1.3v3**, **D8.2.3**, **D8.4.1v2**).

The receiver-kind evidence now follows only bounded, stable direct-call
parameter forwarding. Every non-cyclic direct caller must nominate the same
kind; a self-recursive forwarding call contributes no independent evidence and
an unknown or conflicting caller refuses admission. The generated leaf still
checks the live receiver kind, index and data pointer, so an indirect call,
rebinding or wrong brand reaches the existing generic path (**D3.3.3v3**,
**D8.2.4–D8.2.6**). This is a JS-specific source/effect proof that reuses the
existing `FnParamEvidence`/`FnVariantAnalysis` collection rather than copying
untyped Lambda's traversal.

`JsOpt.TypedArrayStoresUseGuardedNumericKeyLeaf` now covers `-1`,
2^31, 2^32+1.75 and -1.75, proving exact signed readback and runtime ToInt32
behavior. New forwarding and conflicting-caller fixtures prove both the
recursive `main → quicksort → partition` shape and the refusal case. The final
debug quicksort probe prints `quicksort: PASS`; its native `partition` MIR has
the expected-kind-4 guard, width-four address multiply and signed `i32` loads.
The current optimization suite passed 57/57, exact MIR emission 163/163 and
the MIR ratchet 20/20 (only its pre-existing unrelated corpus-shrink notices).

The paired release control kept the same Int32 support but restored the former
one-hop parameter rule, leaving `partition` on generic element access. Across
31 alternating Larceny quicksort pairs, its median workload time was 128.759 ms
versus 23.623 ms for the transitive-evidence candidate: candidate/control
0.183468 (one-sided 95% paired-bootstrap upper bound 0.184183), 31/31 candidate
wins and matching stdout. The exact binaries, source hash and every pair are in
`temp/tune14/paired_quicksort_transitive_int32.json`. This establishes the
quicksort admission's causal benefit; it does not compare against QuickJS or
establish a ResultN-wide geometric mean.

A temporary copy-store leaf was also tested because `partition` swaps values.
It preserved generic RHS behavior, but it added two typed snapshots before the
already-guarded setter. The 31-pair release comparison in
`temp/tune14/paired_quicksort_copy.json` gave candidate/control 1.0933x, zero
candidate wins and matching stdout. The experiment was removed; it is evidence
against this guard shape, not a performance claim or a QuickJS comparison.

#### Implementation update — 2026-09-20: primitive typed writes retain their live view witness

`js_typed_array_set_numeric_impl` refreshed the live `ArrayNum` view before
checking the canonical index, then unconditionally refreshed it again through
`js_typed_array_prepare_write`. The second refresh is required after a coercive
`ToNumber`: user code can resize or detach a resizable backing buffer between
the first witness and the store. It is redundant for `undefined`, compact
integers and floating Numbers, which cannot run user code. The runtime now
shares `js_typed_array_prepare_write_current` between the ordinary setter and
the raw-Number MIR leaf. It uses that current witness only for those primitive
values; every coercive value keeps the post-`ToNumber` refresh. This preserves
the live buffer-view contract of **D2.4.1–D2.4.3** while improving a common
runtime primitive that compiled and interpreted LambdaJS operations both reuse.
It remains profile-owned: untyped Lambda has no ECMAScript `ToNumber` or
resizable typed-view policy to share at this boundary.

The extended typed-store contract creates an `Int32Array` over a resizable
buffer and stores an object whose `valueOf` shrinks that buffer. It observes
`length === 0` and `data[0] === undefined`, proving the coercive path still
refreshes after the callback. The complete optimization suite passed 57/57.

The exact pre-change transitive-Int32 release binary was the control for 31
alternating Larceny quicksort pairs. Its median workload time was 23.988 ms;
the primitive-witness candidate measured 21.639 ms, candidate/control
0.902061 (one-sided 95% paired-bootstrap upper bound 0.905694), with 30/31
candidate wins and matching stdout. The exact binaries, source hashes and all
pairs are in `temp/tune14/paired_quicksort_primitive_store.json`. This is a
causal quicksort result for the shared typed-array setter; it neither compares
LambdaJS with QuickJS nor establishes a ResultN-wide aggregate.

#### Implementation update — 2026-09-20: reuse ArrayNum data only within one handle generation

Every physical typed-array read still validates the receiver kind and live
index. After those checks, the adapter used to resolve the same `ArrayNum`
buffer handle on every read even when its cached pointer had already been
resolved for the current `ByteBufferHandle::generation`. The handle already
advances that generation on resize, detach, transfer and copy-on-write. The JS
adapter now reuses a non-null cached read pointer only when its `ArrayNumShape`
records the same handle and generation. Writes always execute the existing
`array_num_resolve_data(..., true)` path because the write itself may perform
copy-on-write and advance the generation. This consumes the common
`ArrayNum`/`ByteBufferHandle` lifetime contract without creating a parallel
cache; the JS receiver, typed-view and numeric-index policy remains outside it
(**D1.3v3**, **D2.4.1–D2.4.3**, **D8.2.3**).

The focused typed-store, typed-parameter and forwarding contracts passed, as
did `Js54P3TypedArrayLengthTracking` and
`ArrayNumLoopResizeInvalidatesHoist`. Those resize/detach regressions verify
that a generation change still resolves the current backing storage rather than
borrowing a stale pointer.

The primitive-witness release was the exact control for 31 alternating Larceny
quicksort pairs. Its median workload time was 21.618 ms; the
generation-validated read candidate measured 17.963 ms, candidate/control
0.830905 (one-sided 95% paired-bootstrap upper bound 0.833944), with 31/31
candidate wins and matching stdout. The exact binaries, source hashes and all
pairs are in `temp/tune14/paired_quicksort_generation_view_cache.json`. A
separate five-run focused observation measured 17.9 ms for LambdaJS and 19.4
ms for QuickJS. That single-row observation is faster than QuickJS, but it does
not establish the complete 63-row cross-engine milestone.

#### Implementation update — 2026-09-20: retain nested typed-view Number carriers only on an admitted hit

FFT's `four1` already had guarded physical `Float64Array` reads, but
`scale * data[jj] - scale * data[jj + 1]` lost its Number carrier between
the nested binary nodes. The outer subtraction then boxed both operands and
missed the existing guarded Number-store leaf. `jm_get_effective_type` now
retains an F64 carrier for subtraction, multiplication, division, remainder
and exponentiation only when a native Number peer is paired with an already
admitted fixed typed-view member whose key is statically numeric. The existing
guarded typed read supplies the Number hit; its unchanged generic path supplies
the ECMAScript `ToNumeric` miss. Addition remains excluded because one Number
operand cannot rule out string concatenation.

A broader version classified every non-add arithmetic node with one native
Number operand as F64. Although its FFT screen was positive, it increased the
unrelated `js_corpus_array_methods` finalized-MIR ratchet from 3,237 to 3,255.
That version was removed. The retained proof is intentionally limited to the
existing typed-view admission and therefore preserves the profile boundary:
the physical typed access remains the shared `em_element_address` client, while
the ECMAScript Number/coercion decision remains LambdaJS-owned
(**D1.3v3**, **D3.3.3v3**, **D8.2.3–D8.2.6**, **D8.4.1v2**).

`JsOpt.NestedTypedArrayArithmeticRetainsNumberResultAfterGenericMiss` covers
the native `Float64Array` transform, an ordinary-array fallback and objects
whose three `valueOf` calls must still occur. It checks the admitted typed
reads, generic misses, F64 multiply/subtract and guarded Number store. The
focused contracts passed and the exact previously failing ratchet returned to
its 3,237 instruction budget.

The exact generation-validated typed-view release binary was the control for
31 alternating R7RS FFT pairs. The control median was 5.872292 ms and the
retained candidate was 5.726250 ms: candidate/control 0.975130, one-sided
95% paired-bootstrap upper bound 0.985491, 25/31 candidate wins, all 31
samples `ok` and matching stdout. The archive is
`temp/tune14/paired_fft_nested_number.json`; its control SHA-256 is
`c064ec5602f7b57f003526a941f38841c71a4165c71bdbc264ca9e9c1dda8084`
and its candidate SHA-256 is
`897c5ae588cf9731af8532dd4f56165e1b6bbcbfa350e192e28a495b69c35080`.
This is a causal single-row result, not a QuickJS comparison or a 63-row
acceptance result.

Final source validation passed `make test-lambda-baseline` at 5,687/5,687 and
`make test262-baseline` at 40,261/40,261 fully passing, with zero Test262
batch-unstable, slow, failed or baseline-regressed entries. The final release
binary, after the direct-scope repair in T14-6, is SHA-256
`886598b48c60e328866eb202b8ac09dd48bad2b6a38b4c892ac1d9f92d7792c3`.

#### Implementation update — 2026-09-20: share typed-view Number arithmetic across both operand orders

The original guarded binary helper admitted only `nativeNumber OP view[key]`.
That retained the nested `wr * data[jj]` carrier in FFT, but its symmetric
`data[ii] - tempr` and `data[ii] + tempr` expressions still called the boxed
operator. The helper is now one operand-order-aware lowering: it accepts one
already admitted fixed typed-view member with a statically numeric key and one
native Number peer, then emits the same guarded physical read and F64 operation
for either order.

The shared slow helper preserves left-to-right `GetValue` order. With the member
on the left it performs the member Get before evaluating the native peer, roots
and spills the resulting Item when the peer can suspend, then invokes the
existing generic binary kernel. With the member on the right it retains the
already evaluated native peer and resumes the ordinary member Get. Addition,
coercion, string concatenation, object hooks, wrong brands and unsupported keys
therefore remain on the existing JavaScript operation; no ToNumeric policy was
moved into the common physical address leaf (**D1.3v3**, **D2.4.1–D2.4.3**,
**D8.2.3–D8.2.6**, **D8.4.1v2**).

`JsOpt.LeftTypedArrayArithmeticRetainsNumberResultAfterGenericMiss` covers the
reverse multiply/subtract/read-store shape on `Float64Array`, an indirect
ordinary array and three object `valueOf` calls. It asserts both guarded-read
hits and misses plus the resulting `dmul`/`dsub`/Number-store MIR. The full JS
optimization suite passed 59/59, the full MIR ratchet passed 20/20 and the
full Lambda baseline passed 5,688/5,688.

The exact scope-index release was the control for 31 alternating R7RS FFT
pairs. Its median was 4.713458 ms; the symmetric candidate was 3.987958 ms:
candidate/control 0.846079, one-sided 95% paired-bootstrap upper bound
0.871704, 28/31 candidate wins, all samples `ok` and matching stdout. The
archive is `temp/tune14/paired_fft_left_typed_number.json`; control SHA-256 is
`886598b48c60e328866eb202b8ac09dd48bad2b6a38b4c892ac1d9f92d7792c3` and
candidate SHA-256 is
`2bd599a5a2e177183e241c6adb29c6be761ba24773d69c61dabbbc446e2c7a22`.
The candidate source copy is
`temp/tune14/js_mir_expression_lowering.left_typed_candidate.cpp`. This is a
causal single-row result only; it does not establish the 63-row or cross-engine
milestone.

Final source validation passed `make test262-baseline` at 40,261/40,261 fully
passing, with zero batch-unstable, slow, failed or baseline-regressed entries.

A comparison extension for the same typed-view Number witness was implemented
and tested with relational and strict-equality misses, then removed. Its 31-pair
release quicksort replay measured 17.719750 ms versus 17.699792 ms
(candidate/control 0.998874; one-sided upper bound 1.000836; 17/31 wins) with
matching stdout. The archive is
`temp/tune14/paired_quicksort_typed_compare.json`; control SHA-256 is
`2bd599a5a2e177183e241c6adb29c6be761ba24773d69c61dabbbc446e2c7a22` and
the discarded candidate SHA-256 is
`6040243b3a0a34bcce2561627a65d98b3b8e6cb4ede3808b10b1224f351cebd9`.
The semantics and MIR fixture were green, but the gain does not meet the
one-sided paired gate, so neither the code nor its test remains in the working
tree. Revisit comparisons only after a dynamic profile identifies an executed
generic comparison cost (**S1.11**, **D8.4.1v2**).

#### Implementation update — 2026-09-21: admit scalar-safe companion dense reads

The Navier census showed that every `runtime_number_index_get_call` receiver was
a tagged Array with a named-property companion. The previous direct array arm
required no companion, so all 36,190,970 generic Number-index reads missed its
physical load. The new leaf
`js_array_get_existing_own_dense_with_props_or_missing` admits exactly one
present ordinary dense slot through that companion. It rejects content arrays,
numeric companion slots (including indexed descriptors), holes, bounds misses,
non-dense storage, scalar tails and scalar-home values; each rejection resumes
the existing `js_elements_get_number` operation. The leaf is `NO_GC` and does
not re-enter JavaScript, so it cannot create a stale array or scalar borrow.
This keeps ECMAScript indexed-property policy in LambdaJS while using the
existing physical dense-slot primitive, address emission and common scalar-home
contract rather than duplicating untyped Lambda array semantics (**S1.11**,
**D1.3v3**, **D5.3.4**, **D8.2.3–D8.2.6**).

The two physical tagged-array arms copy into one explicit MIR carrier before
the common Number decode. This is necessary because MIR has no implicit phi:
the initial experiment loaded the plain arm into a different register and then
read the companion arm's register at the join. The resulting four optimizer
regressions found the defect before release acceptance. The corrected lowering
is covered by `JsOpt.CompanionDenseReadPreservesHoles`, which executes both a
named companion own-element hit and a named companion hole fallback; the
existing indexed-lane, native-store, closure-write and sparse-literal fixtures
cover the affected join paths.

The 15-frame semantic profile records 51,433,760 admitted own-element reads and
zero generic Number-index helper calls, while retaining the source checksum and
the full density digest. The scalar-safe release candidate
`temp/tune14/lambda_t14_companion_dense_phi_release.exe`
(`f7b5442dfaa9af24b321a8fcac0213565069ad94d494e68732c91f9554fa6f14`) was
measured against the exact discarded-update predecessor
`eee834af42066e74266ea7687d4524d82d1fa19c4bd1250885ac588ac657a8b0` in 31
alternating Navier pairs. Candidate/control medians were 184.561/515.726 ms:
ratio 0.357866, one-sided 95% paired-bootstrap upper bound 0.359286 and 31/31
candidate wins. All samples were `ok`, had equal stdout and passed the
post-timing frame-15 and full-density oracle. The artifact is
`temp/tune14/paired_navier_companion_dense_phi.json`. This is a causal
single-row result; it is neither a full 63-row matrix nor a comparison with
QuickJS or JS-MVP.

Validation passed optimizer 62/62, coercion 15/15, MIR ratchet 20/20,
`make test-lambda-baseline` 5,691/5,691 and `make test262-baseline`
40,261/40,261. Test262 had zero non-fully-passing, slow, failed or regressed
entries. The ratchet reported only unrelated corpus reductions
(`js_corpus_array_methods`, `lambda_corpus_cube3d` and
`lambda_corpus_deltablue`); no budget changed.

A follow-up Number-only leaf for discarded identifier updates reached 13,985,833
Navier hits with zero dynamic misses, reducing both generic `ToNumeric` and
generic increment entries by that count. It remained semantically correct, but
the exact 31-pair release replay measured 184.296 ms versus 184.800 ms for the
companion-read control (ratio 0.997273; one-sided upper 1.002706; 16/31 wins).
The result does not meet the paired gate, so the leaf, its import metadata and
its test were removed. Retain
`temp/tune14/paired_navier_number_update_leaf.json` as negative evidence:
dynamic reach alone does not justify an extra runtime branch when the existing
complete update entry is already cheap on Number inputs.

A second companion-read leaf accepted MIR's already-proved raw `Array*` instead
of its rooted `Item`, while sharing the exact hole, numeric-descriptor and
scalar-home core with the accepted helper. The focused optimizer fixture and
the Node/Lambda frame-15 density oracle passed, but the exact 31-pair release
replay regressed from 186.841 to 189.653 ms (candidate/control 1.015050;
one-sided upper 1.017173; 5/31 wins). All samples were `ok`, had equal stdout
and matched the expected density digest. The source, import metadata and test
expectation were removed because avoiding the boxed type check did not outweigh
the raw-pointer call boundary. Retain
`temp/tune14/lambda_t14_companion_dense_raw_release.exe`
(`d849ba29e89de4d38c08360f3a8c016433b878d4bd8d3098ba6821d873fcc66f`) and
`temp/tune14/paired_navier_companion_dense_raw.json` as negative evidence. The
ordinary `Item` helper remains the selected path, preserving the same rooted
ABI and complete fallback (**S1.11**, **D5.3.4**, **D8.2.3–D8.2.6**).

#### Implementation update — 2026-09-21: admit Number-only bitwise pairs

The `crypto_sha1` profile recorded 9,221,600 Number-head fallbacks and
18,759,825 `js_to_numeric` calls in its canonical wrapper. Its hot SHA-1 rounds
are bitwise Number operations, but `js_numeric_number_pair` only admitted
arithmetic opcodes; every `&`, `|`, `^`, `<<`, `>>` and `>>>` therefore entered
the complete `ToNumeric` kernel twice despite both operands already being JS
Numbers.

`js_numeric_bitwise_number_pair` now shares the existing `js_to_int32`
conversion with the generic Number tail and is called only after both operands
pass the existing Number-like test. Strings, objects, arrays, Functions,
Symbols and BigInts continue through the complete conversion/error path. The
expanded Number-head fixture covers all six operators, signed/unsigned shifts,
string concatenation and `valueOf` coercion fallback (**S1.11**, **D1.3v3**,
**D6.2.2v2**, **D8.4.1v2**, **D8.4.3v2**).

The release candidate
`temp/tune14/lambda_t14_bitwise_number_head_release.exe`
(`4b140f0c150d631033f896290c051e3536dbfa429d6341b26ed6f941def5d127`) was
replayed against the exact accepted Map-preamble predecessor
`9bb0ee2a434b9e2e67466d0c33fd413cc31c36f34f08cb37e35c6eb3f9abc601` on
canonical `jetstream/crypto_sha1`. Across 31 alternating pairs, medians
improved from 321.452000 to 264.704000 ms (candidate/control 0.823464;
one-sided 95% paired-bootstrap upper bound 0.824575; 29/31 candidate wins).
All samples were `ok` with equal stdout. The exact archive is
`temp/tune14/paired_crypto_sha1_bitwise_number_head.json`. This is a shared
runtime leaf with one causal workload result, not a full-matrix claim.

Current-source validation passed optimizer 63/63, coercion 15/15 and MIR
ratchet 20/20. The ratchet again reported only unrelated corpus reductions
`js_corpus_array_methods` (3237 to 3236), `lambda_corpus_cube3d` (16011 to
15996) and `lambda_corpus_deltablue` (8998 to 8908), so no budget changed.
`make test-lambda-baseline` passed 5,717/5,717. `make test262-baseline` passed
40,261/40,261 fully, with zero batch-unstable, slow, failed or
baseline-regressed entries; its 169 batches completed in 68.5 seconds.

#### Implementation update — 2026-09-21: share primitive-Number relational comparisons

The Havlak runtime census still entered `js_compare_boxed` 3,056,642 times for
dynamic relational expressions. Its operands are function parameters and field
loads, so the compiler correctly keeps their source types open, but the executed
values are primitive Numbers. `js_relational_number_pair` now sits beneath both
`js_compare_boxed` and `js_cmp_raw`: after both values pass the existing
Number-like tag check, it uses the existing `js_get_number` conversion and one
of the four IEEE relational operators. NaN therefore produces false for every
relational spelling without touching `ToPrimitive` or `ToNumeric`. Objects,
strings, Symbols and BigInts still take the complete Abstract Relational
Comparison kernel, including its original operand order, coercion, completion
and error behavior (**S1.11**, **D1.3v3**, **D8.4.3v2**).

This is direct reuse of the existing JavaScript Number conversion and raw/boxed
facade boundary, not a new common physical primitive: untyped Lambda has
different relational semantics, so extracting this ECMAScript admission into a
shared compiler helper would create duplicate semantic ownership. The runtime
profile is correspondingly precise: `runtime_boxed_compare_call` now measures
only the actual generic tail, while `runtime_number_compare_head` measures the
effect-free primitive head (**D8.2.3–D8.2.6**, **D8.4.1v2**).

The release-profile Havlak record
`temp/tune14/havlak_number_compare_head_profile.tsv`
(`cc59cdbe1a08a602d2869c96ffd90f773fa563dfb2f07dcce3507fb05c1253da`)
records 11,221,940 primitive comparison-head hits and zero generic boxed
comparison calls. It includes raw condition-facade traffic that the old boxed
counter did not measure; it is an admission census, not a timing claim.
`JsOpt.RuntimeNumberCompareHeadKeepsRelationalCoercion` covers dynamic Number
arguments, NaN and a `valueOf` object whose four relational operations must
remain on the generic tail. It passes trace-on/off and also under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.

The immutable release candidate
`temp/tune14/lambda_t14_number_compare_head_release.exe`
(`9643f64873962270fd619ddeaee7d9b36859061d32ef27144543da90ebb7ed1a`) was
replayed against the exact recursive-record predecessor
`cb4bf928ffd71156597f19cb44a2d0107e5bd1c73779afe3e31834a398140e1c` on the
same `awfy/havlak` bundle
(`32e7e09c936c2a7d131e24aeb2558ce7b775130f35aa8eededd7fabdc9b35f72`).
Across 31 alternating pairs, all samples were `ok` with equal stdout; the
median fell from 13,245.162333 to 13,211.574667 ms (candidate/control
0.997464; one-sided 95% paired-bootstrap upper bound 0.998807; 23/31 candidate
wins). The exact archive is
`temp/tune14/paired_havlak_number_compare_head.json`
(`43587710df4248e4269a52bedb0f19292c083c4e0adc4b45f0979db4d57aec39`). This
is a small accepted causal result on one workload, not a 63-row or
QuickJS/MVP comparison.

Final source validation passed the focused Number, comparison and recursive
literal contract tests; `make test-lambda-baseline` at 5,719/5,719; and `make
test262-baseline` at 40,261/40,261 fully passing in 169 batches, with zero
unstable, slow, failed or baseline-regressed entries.

#### Rejected experiment — 2026-09-21: public Number update head

The C14 `hashmap` profile recorded 982,149 complete `ToNumeric` and increment
entries. A Number-only public `++` / `--` head reduced `ToNumeric` calls to
180,000 in that profile while preserving the complete conversion path for
strings and other non-Number values. The exact C14 31-pair alternating replay
had equal stdout and all `ok` statuses, but its candidate/control median ratio
was 0.9950999, the one-sided paired-bootstrap upper bound was 1.001581, and it
won only 16/31 pairs. The branch, its test and its candidate binary were
therefore rejected and reverted; retain
`temp/tune14/paired_hashmap_number_update_head.json` as negative evidence. A
lower helper count alone does not establish a faster public semantic operation:
the completed ECMAScript conversion must remain the shared fallback (**S1.11**,
**D1.3v3**).

- [ ] Produce an admission/refusal map for all three search algorithms, FFT
  `four1`, Navier's inner functions and quicksort `partition`. Account separately
  for receiver kind, key, element value, operator, local join and destination.
- [~] Extend producer/consumer continuity to both operand positions and nested
  expressions. The admitted typed-view Number shape now shares one lowering for
  both operand orders and preserves the already-evaluated reference on a miss.
  Extend other producer/consumer shapes only with equally explicit source-order
  and generic-fallback proofs.
- [ ] Keep numeric lengths/indices, loaded Numbers, arithmetic/comparison
  results and writable destinations native. Materialize Items at actual generic
  consumers, representation merges, boxed ABI edges or semantic misses.
- [ ] Wire the existing guarded Number-store leaf into admitted FFT stores.
  Explain each surviving boxed setter; presence of a leaf in the registry is
  not evidence that the workload uses it.
- [~] Reuse one physical typed-access plan for supported element kinds.
  `Int32Array` now covers quicksort's signed loads and exact runtime ToInt32
  stores through guarded direct-call forwarding. Add other kinds only with
  explicit conversion tests and measured use. Keep clamping, Float32 rounding
  and BigInt kinds distinct.
- [~] Preserve ordinary-array holes and indexed descriptors/prototypes. The
  companion leaf now proves one present own tagged-array slot and rejects
  numeric overlays, holes and scalar-home values before the generic Get. Extend
  this only with an equally local proof; it is not a certificate that every
  element of an aliased tagged array is numeric.
- [ ] Separate overwriting an existing writable slot from creating/growing a
  property so the former does not inherit irrelevant growth checks. Retain the
  established scalar-home/store ownership primitive.
- [ ] Keep index semantics exact: numeric `-0`, string `"-0"`, fractions,
  NaN/infinities, bounds, holes, detached/resizable views and coercing stores.
  Receiver/source hints select a guarded arm, never prove a runtime brand.

**Fixtures:** read-compute-store with both operand orders; alias mutation;
ordinary mixed arrays; holes with inherited getters; descriptor changes;
proxy/host/wrong-brand receivers; detached/resized views; coercing RHS; thrown
keys; signed and wrapping Int32 stores; fast/miss ownership joins under GC.

**Exit:** the diagnosed search and FFT regions consume native values through
their hot operations, Int32Array quicksort has guarded coverage, and dynamic
hit/miss evidence plus paired release measurements confirm benefit. List any
remaining boxed operations explicitly. Helpers on cold miss arms are allowed.

### T14-4 — Reuse guards and metadata within valid effect regions

**Primary files:** existing loop/effect analysis,
`lambda/runtime/mir_loop_invariants.hpp`, array lowering and helper catalog.

**Reuse requirement:** §3.3C. Extend the common scalar hoister for scalar
invariants; converge memory-witness handling separately. Do not copy Lambda's
typed dense-loop scanner and treat its conclusions as JS admission facts.

**2026-09-21 audit disposition:** LambdaJS already calls the shared
`em_hoist_loop_scalar_calls` helper. Its registered admission requires a pure,
loop-stable, no-GC, non-reentrant scalar call whose scalar dependencies are also
invariant. That excludes mutable array length, backing/data pointers and dense
element storage: an aliasing write, descriptor/prototype change, detach/resize,
reentry or unknown call can invalidate each. No T14-4 storage hoist was added.
The rejected raw-receiver companion leaf above is a separate immediate no-GC
read, not a reusable region witness. It confirms that changing a leaf ABI alone
does not satisfy this package's measured-benefit requirement (**S1.11**,
**D3.3.3v3**, **D5.3.1–D5.3.5**, **D8.2.3–D8.2.6**).

- [ ] Represent an operation/region-local witness through existing immutable
  plans: receiver identity, storage/element kind, valid index range, length/data
  validity and its invalidating effects. Do not add a mutable site cache or
  duplicate global analysis table.
- [ ] Reuse or hoist only facts whose full dependencies are invariant. A kind
  check does not alone authorize reusing length, data or element-value facts.
- [ ] Kill/reacquire witnesses for relevant alias writes, length changes,
  backing replacement, descriptor/prototype mutation, detach/resize, reentry or
  unknown calls. `NO_GC` is not proof of nonreentry, nonmutation or nonthrowing.
- [ ] Consume raw data borrows immediately unless a region-wide stability proof
  exists. Keep the owner precisely rooted if a retained borrow can cross an
  admitted safepoint; otherwise end the region at the call.
- [ ] Place misses before observable effects, or continue from a valid
  operation-level continuation. Never restart a partially executed loop or
  repeat a getter/coercion to recover from a failed guard.
- [ ] Measure reduction in dynamic metadata queries/guards against T14-3's
  predecessor, not only against C14. Bound guard/code expansion for miss-heavy
  loops and preserve compact generic lowering where specialization is unhelpful.

**Exit:** at least the diagnosed ordinary-array search and typed FFT regions
reuse justified metadata beyond a single leaf call, with invalidation fixtures,
fewer executed queries and no rooting/evaluation-order regression.

### T14-5 — Preserve names and broaden guarded ordinary fields

**Primary files:** `lambda/core/name_pool.cpp`, `js_props.cpp`,
`js_property_attrs.cpp`, `js_runtime.cpp`, `js_globals.cpp`, and
`jm_plan_predicted_literal_field` with the existing `MirFieldAccessPlan` and
shape-candidate planner.

#### Implementation update — 2026-09-21: bypass lazy-function metadata for ordinary names

`js_get_name_id` first obtains the active context's `NameRef`, then performs
the normal host/property policy and ordinary NameId fast lookup. Before this
change it also called `js_function_materialize_lazy_metadata_property` on every
receiver. That helper creates a two-slot `RootFrame` before rejecting any value
whose tag is not `Function`. The Richards profile recorded 10,604,714 named
probes, 10,158,273 direct hits and only 14 function/no-receiver misses, so
ordinary Map reads paid that Function-only setup repeatedly.

The runtime now calls the materializer only after `get_type_id(object)` proves
the receiver is `Function`. It neither changes a `NameId` domain nor caches a
property decision: Function `name`/`length` still materialize before the same
ordinary lookup, while Maps continue directly to their existing host and
descriptor policy. The focused lazy-metadata, strict function-data and
companion-hole fixtures pass (**S1.11**, **D4.6.1v3–D4.6.2v2**, **D5.3.1–D5.3.5**,
**D8.2.4–D8.2.6**).

The release candidate
`temp/tune14/lambda_t14_function_metadata_gate_release.exe`
(`7192240e263bf081d7212a95bfd2fa05980050f30f83be97fc794b10bb26167d`) was
replayed against the accepted companion-read release
`f7b5442dfaa9af24b321a8fcac0213565069ad94d494e68732c91f9554fa6f14` on the
canonical `awfy/richards` source. Across 31 alternating pairs, medians improved
from 1110.781875 to 1073.825417 ms (candidate/control 0.966729; one-sided 95%
paired-bootstrap upper bound 0.969959; 31/31 candidate wins). All samples were
`ok` with equal stdout. The exact archive is
`temp/tune14/paired_richards_function_metadata_gate.json`. This establishes one
ordinary-name entry-cost reduction; it does not prove broader field-shape or
enumeration coverage.

Final source validation passed optimizer 62/62, coercion 15/15 and MIR ratchet
20/20. The ratchet again reported only the unrelated corpus reductions
`js_corpus_array_methods` (3237 to 3236), `lambda_corpus_cube3d` (16011 to
15996) and `lambda_corpus_deltablue` (8998 to 8908), so no budget changed.
`make test-lambda-baseline` passed 5,691/5,691. `make test262-baseline` passed
40,261/40,261 fully, with zero batch-unstable, slow, failed or
baseline-regressed entries; its 169 batches completed in 81.4 seconds.

#### Implementation update — 2026-09-21: restrict live Window reads to the global receiver

`js_get_host_dynamic_property` previously let every Map receiver probe the
Window `event` hook and the Radiant Window bridge before ordinary property
lookup. Both facilities independently require exact identity with the active
realm's global object; no ordinary Map can acquire either hook. The property
kernel therefore repeated two global-state preparations for every ordinary
named read, including the Richards field traffic.

The helper now requires both a Map receiver and
`js_is_global_this_object_value(object)` before asking either host provider.
This is a receiver-identity gate, not a cache or a name-domain shortcut:
`globalThis.event` remains a live host read, while ordinary `event` and
`innerWidth` own properties continue through the same NameId and descriptor
lookup. `JsOpt.HostDynamicReadsRemainGlobalOnly` asserts both outcomes and
observes a host-dynamic miss plus an ordinary named-fast hit (**S1.11**,
**D4.6.1v3–D4.6.2v2**, **D5.3.1–D5.3.5**, **D8.2.4–D8.2.6**).

The release candidate
`temp/tune14/lambda_t14_host_dynamic_global_gate_release.exe`
(`83c680089439deb8e7ae73b44d8ab4506b783100a94a8a2835e4d882efe1028b`) was
replayed against the exact accepted Function-metadata predecessor
`7192240e263bf081d7212a95bfd2fa05980050f30f83be97fc794b10bb26167d` on
canonical `awfy/richards`. Across 31 alternating pairs, medians improved from
1098.790542 to 1063.490792 ms (candidate/control 0.967874; one-sided 95%
paired-bootstrap upper bound 0.974647; 30/31 candidate wins). All samples were
`ok` with equal stdout. The exact archive is
`temp/tune14/paired_richards_host_dynamic_global_gate.json`. This establishes a
second property-entry cost reduction only; it does not establish field-shape,
NameId transport or enumeration coverage.

Current-source validation passed optimizer 63/63, coercion 15/15 and MIR
ratchet 20/20. The ratchet again reported only unrelated corpus reductions
`js_corpus_array_methods` (3237 to 3236), `lambda_corpus_cube3d` (16011 to
15996) and `lambda_corpus_deltablue` (8998 to 8908), so no budget changed.
`make test-lambda-baseline` passed 5,692/5,692. `make test262-baseline` passed
40,261/40,261 fully, with zero batch-unstable, slow, failed or
baseline-regressed entries; its 169 batches completed in 59.7 seconds.

#### Implementation update — 2026-09-21: skip impossible special-name checks for ordinary Maps

After resolving the active-context `NameRef`, `js_get_name_id` still checked
Array `length`, primitive-string `length`, Window hooks and Function metadata
before every named Map lookup. The post-gate `hashmap` profile records 7,759,056
ordinary named-fast hits in one canonical iteration; a non-global Map cannot
observe any of those receiver-specific behaviors. Repeating the tests delayed
the same shared shape/slot lookup without contributing a semantic decision.

The entry now identifies a non-global Map once and sends it directly to the
existing named fast lookup. Global Maps retain the complete preamble so live
Window fields and lazy globals remain observable; Arrays, primitive strings and
Functions retain their existing heads. A rejected direct lookup still resumes
the original complete property kernel. The receiver test now covers a same-name
ordinary `length` field alongside `event` and `innerWidth`, and continues to
assert the live global `event` path (**S1.11**, **D4.6.1v3–D4.6.2v2**,
**D5.3.1–D5.3.5**, **D8.2.4–D8.2.6**).

The release candidate
`temp/tune14/lambda_t14_map_preamble_gate_release.exe`
(`9bb0ee2a434b9e2e67466d0c33fd413cc31c36f34f08cb37e35c6eb3f9abc601`) was
replayed against the exact accepted global-host-gate predecessor
`83c680089439deb8e7ae73b44d8ab4506b783100a94a8a2835e4d882efe1028b` on
canonical `awfy/richards`. Across 31 alternating pairs, medians improved from
1042.163208 to 1014.719500 ms (candidate/control 0.973667; one-sided 95%
paired-bootstrap upper bound 0.976489; 31/31 candidate wins). All samples were
`ok` with equal stdout. The exact archive is
`temp/tune14/paired_richards_map_preamble_gate.json`. It establishes only an
ordinary Map entry reduction, not a generic property-policy shortcut.

Current-source validation again passed optimizer 63/63, coercion 15/15 and MIR
ratchet 20/20. The ratchet again reported only unrelated corpus reductions
`js_corpus_array_methods` (3237 to 3236), `lambda_corpus_cube3d` (16011 to
15996) and `lambda_corpus_deltablue` (8998 to 8908), so no budget changed.
`make test-lambda-baseline` passed 5,692/5,692. `make test262-baseline` passed
40,261/40,261 fully, with zero batch-unstable, slow, failed or
baseline-regressed entries; its 169 batches completed in 77.3 seconds.

#### Implementation update — 2026-09-21: publish recursive literal-return recipes

The existing common `mir_shape_candidate` and `MirFieldAccessPlan` path now
plans a narrow recursive record family before any function lowers. A candidate
has at least two return-object literals with the same ordered ordinary own data
properties; every field is an existing supported scalar or a Map child written
only as `null` or a direct stable self call; and at least two static field
receivers trace from a direct returned call or its directly forwarded parameter.
Each return literal receives one immutable `TypeMap` recipe. A live exact-shape
and payload-capacity guard still precedes every direct map-slot load, and each
miss enters the existing `js_get_name_id` property kernel. This is predicted
specialization with a shared semantic miss, not an IC or feedback cache
(**S1.11**, **D4.6.1v3–D4.6.2v2**, **D8.2.3–D8.2.6**, **D8.4.1v2**).

The plan reuses untyped Lambda's shape-candidate, guarded-map and physical-slot
machinery (`mir_shape_candidate`, `em_guard_map_shape`, `em_load_at` and
`TypeMap`) instead of introducing a parallel JS field planner. LambdaJS retains
the language-specific boundary: JavaScript descriptor and publication rules
remain in `js_create_data_property`. During source-order initialization, a
pre-reserved ordinary literal slot is unobservable, so it can publish through
the same shared `fn_map_set` path without cloning its immutable recipe. A null
child already has the native null Map-pointer representation; preserving that
lane avoids a spurious Map-to-null transition. All observable descriptor,
accessor, special-name and miss behavior remains on the normal JS path
(**S1.11**, **D5.3.1–D5.3.5**, **D8.4.1v2**, **D8.4.3v2**).

`JsOpt.RecursiveLiteralReturnShapeUsesGuardedMapSlots` proves the admission,
shared literal allocator, generic miss presence and trace-on/off parity. Its
binarytrees diagnostic drops named fast probes from 270,351 (270,348 hits) to
5 (2 hits, 3 misses), admits three literal-field plans and removes 68,608
static-object initializers; stdout hashes match. The immutable release candidate
`temp/tune14/lambda_t14_recursive_literal_shape_release.exe`
(`cb4bf928ffd71156597f19cb44a2d0107e5bd1c73779afe3e31834a398140e1c`) has
an accepted exact 31-pair alternating replay on canonical `beng/binarytrees`
against frozen C14 (`4b140f0c150d631033f896290c051e3536dbfa429d6341b26ed6f941def5d127`):
35.608417 to 19.519250 ms, candidate/control 0.548164, one-sided 95%
paired-bootstrap upper bound 0.553640 and 31/31 candidate wins, with all
statuses `ok` and equal stdout. The archive is
`temp/tune14/paired_binarytrees_recursive_literal_shape.json`; its candidate
also passes the three-engine Navier density oracle.

The preceding source validation applies to the recursive literal-return slice.
The constructor extension is separately covered by
`JsOpt.DynamicConstructorFieldsReuseReservedShapeTransitions` and
`JsOpt.ConstructorShapeDeclinesReceiverEscapingRhs`: ordinary dynamic values,
conditional allocation and source-order property publication preserve the
shared transition layout, while an RHS that passes `this` to a function which
defines a later planned property retains generic construction. Both tests pass
with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. This is a correctness
and reuse boundary only; its frozen pre-slice release control is
`temp/tune14/lambda_t14_pre_dynamic_constructor_shape_release.exe`
(`13dfb3597f017691917eda4f647a3c4bec360e8324ad2396009f0ff24724be34`), but
no release-profile or paired performance result has been accepted for this
extension.

Final source validation passed the focused literal tests, including
`JsOpt.RecursiveLiteralReturnShapeUsesGuardedMapSlots` under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, `make
test-lambda-baseline` at 5,691/5,691 and `make test262-baseline` at
40,261/40,261 fully passing in 169 batches, with zero batch-unstable, slow,
failed or baseline-regressed entries. This accepts the recursive-return record
slice only; constructor assignment, aliases, enumeration and general
descriptor coverage remain open.

- [ ] Trace NameIds from parser/static linking and enumeration through reference
  creation, own/prototype lookup and stores. Count each spelling/hash/catalog
  reconstruction and remove only redundant boundaries.
- [ ] Preserve the owning context's ID domain. Materialize observable strings
  at reflection/proxy boundaries; do not make dynamic IDs portable across realms
  or use spelling to identify a JS Symbol (**D4.6.1v3–D4.6.2v2**).
- [~] Extend existing shape candidates through analyzable constructor assignments,
  returned records and local aliases. Base-class, straight-line direct
  `this.ident = rhs` assignments can reserve nonreceiver-observing dynamic RHS
  fields as `LMD_TYPE_NULL` placeholders and reuse the shared `TypeMap` /
  `fn_map_set` transition path; exact supported literals retain their static
  lane (**D8.2.3–D8.2.6**, **D8.4.1v2**). RHS `this` observation and direct
  `eval` retain generic construction. Partial initialization, indirect escape,
  descriptor mutation of a reserved planned field, returned records and local
  aliases, constructor-returned replacement objects, subclasses and prototype
  changes remain open.
- [ ] Guard the live shape, applicable own descriptor and slot representation.
  Use shared direct load/store primitives on a hit; retain the property kernel
  for misses. Keep mutable field values independent of immutable layout facts.
- [ ] Preserve native numeric field values into native consumers/destinations.
  Avoid wrapping a direct field load in the generic numeric protocol immediately.
- [ ] Extend the existing nonallocating internal descriptor inspection where
  profiles find remaining materialization. Preserve for-in liveness, deletion,
  re-addition, shadowing, ordering and array index/length rules. Public reflection
  must still construct its specified result object.
- [ ] Separate property/method resolution from callable invocation in profiles.
  JetStream `hashmap` is a JS implementation with fields and array buckets;
  native `Map.set` changes are not a substitute for its access coverage.

**Fixtures:** shape hit/miss, field deletion/redefinition, accessors/proxies,
constructor escape/return/subclassing, own versus inherited fields, mixed numeric
stores, symbol keys, enumeration mutation, cross-context cache reuse and GC.

**Targets:** Havlak, Richards, DeltaBlue, CD, microdiff, JetStream hashmap,
Prettier AST, log pipeline and merge workloads.

**Exit:** constructor/returned-record fields have demonstrated guarded coverage,
targeted names avoid repeat resolution, and descriptor inspection avoids measured
temporary allocations. Publish admitted/refused cases and per-family A/B results.

### T14-6 — Finish compiler, cold-start and memory scaling work

**Primary files:** `em_finalize_semantic_root_write_back` and scalar-home
planning in `mir_emitter_shared.hpp`, indexed analysis, JS initializer lowering,
MIR artifact/cache ownership and native-code lifecycle.

#### Implementation update — 2026-09-19: safe cache rejection and parallel AST prebuild

A JavaScript module-cache candidate records copied source and declaration
metadata, but initially points at the MIR context whose compiled function bodies
have already been published into the active realm. Cache admission is optional.
When it is disabled or rejects that candidate, cleanup now releases only the
candidate-owned copy; ordinary module completion retains the live context in the
realm's deferred code store. A cross-language exported function therefore cannot
call an address from a context that candidate cleanup has already destroyed
(**D5.4.3**, **D8.5.1v4**).

The Lambda script registry mutex also no longer covers a cache single-flight
claim. A parallel AST-prebuild worker may recursively register an import while
another worker waits on that claim; holding the receiving runtime's registry
mutex across the wait deadlocked the closure. The cold global system-function
maps now publish through `uv_once`, so independent first-import workers cannot
race initialization (**D8.1.1v10**, **D8.5.1v4**). Focused cache tests cover a
two-worker shared-import closure and a one-entry cross-language `test-batch`
manifest with `LAMBDA_SCRIPT_CACHE=off`; the latter previously terminated from
a freed MIR code context.

#### Implementation update — 2026-09-20: make direct scope binding linear in its binding count

The direct JS binder preserved its ordered `NameEntry` list for lexical-slot
planning, but `js_scope_find_entry` linearly scanned that list for every
predeclared and bound `var`. The two Unicode-10 identifier tests each declare
8,327 names, so their global-scope reconstruction was quadratic and crossed
the Test262 three-second timing gate only under the seven-worker batch.

The builder now holds a transient `TypedHashMap` keyed by the existing
`(JsScope*, interned String*)` identity and maps it to the same `NameEntry`.
The retained `NameScope` ABI, list order and slot planner are unchanged; an
allocation failure falls back to the authoritative list. The map is destroyed
with the `JsTranspiler` tail before AST adoption, so it cannot become retained
execution state or a mutable runtime cache. This shares the repository's typed
hash-map substrate but is deliberately JS-builder-local: it does not claim a
second untyped-Lambda scope implementation or relax JavaScript declaration
rules (**D1.3v3**, **D8.2.4**, **D8.2.5v2**).

`JsDirectScope.IndexesLargeUnicodeBindingsWithStableSlots` parses 768 mixed
raw and escaped CJK declarations, verifies all lexical slots retain source
order and resolves representative bindings through the index. The full
Test262 run measured the two former slow entries at 2.274625s and 2.121400s,
then reported 40,261/40,261 fully passing with zero slow or batch-unstable
entries. This is the stability acceptance evidence; it is not a benchmark
speedup claim.

- [ ] Attribute bytes and lifetime overlap to AST/index, MIR, CFG, root
  candidates, collecting-call liveness, interference, scalar homes, native code
  and guest objects. Measure peak RSS and retained bytes, not just allocation
  counts. Distinguish one-shot compilation from repeated contexts/cache reuse.
- [ ] Retain Tune13's collecting-call-only liveness change. Audit remaining
  candidate-by-candidate interference and repeated full-index scans. Use
  sparse/adaptive storage or indexed facts only when the census shows a scaling
  problem; do not create a second permanent planner or liveness oracle.
- [ ] Preserve exceptional edges, aliases, scalar-home constraints, exact slot
  interference and root reload behavior. Temporary fact comparisons may aid
  migration; dynamic GC remains the independent oracle (**D8.6.3**).
- [ ] Measure compact numeric-array initializer coverage before extending it.
  Reuse construction primitives for further admitted forms only when code
  volume is material. Preserve property order, duplicate keys, `__proto__`,
  holes, computed keys, spreads, getters and partially constructed ownership.
- [ ] Account for recent generated-IR release on retained artifacts. Do not
  retain compiler IR merely to make diagnostics easy, or free code/context state
  while closures, callbacks or native lazy entries can still call it.
- [ ] Run a size-scaling family plus Hyphen, Prettier and affected document
  scripts. Record finalized instructions, native bytes, root stores/safepoints,
  lowering/generation time and memory peaks. Keep diagnostic and timing builds
  separate; no vendored MIR modification is assumed.

**Exit:** remaining major costs have owner/phase attribution, diagnosed scaling
defects are reduced with dynamic-root and MIR evidence, and cold/RSS comparisons
are published. A measured low-cost component can receive a no-change disposition;
an unmeasured or unresolved large peak cannot be declared complete.

### T14-7 — Address measured built-in, allocation and call residuals

This package requires profiling and an explicit disposition for each family;
it does not require speculative implementation in every family.

| Family | First measurements | Permitted next step | Required semantic guards |
|---|---|---|---|
| RegExp | Matching vs property/protocol vs result allocation on regexredux, revcomp and Hyphen. | Reuse bulk kernels; retain immutable pattern classification with its compiled owner when valid. | Custom `exec`, symbol protocols, source/flag accessors, subclassing, global/sticky lastIndex, empty/Unicode matches, throws and reentry. |
| Strings | ASCII/non-ASCII paths, repeated UTF-16 index/length scans, substring/concat allocation. | Existing string leaves; immutable index metadata only for a demonstrated scan cost. | UTF-16 code units, lone surrogates, observable coercion/order and correct lifetime. |
| Allocation/GC | Allocations/bytes, collection time, root/safepoint work and live/retained objects on Hyphen, binarytrees, gcbench and Havlak. | Remove duplicate construction, ownership bookkeeping or temporary objects; audited leaf effects where true. | Precise roots, scalar-home transport, weak/strong edges and cleanup on failure. |
| Calls | Dispatcher self time vs argument preparation, property lookup, metadata and callee work. | Reuse established direct/light invoke paths and rooted argument spans where their contract permits. | Get-before-arguments, `this`, `new.target`, bound args, proxies, constructors, exceptions and activation observability. |

#### Implementation update — 2026-09-21: share immutable closure source

The C14 Havlak profile records 2,395,949 deferred function finalizations and
4,791,896 lazy-metadata capability finalizations. Source audit found that each
MIR closure nevertheless called `js_make_string_len` for its definition-site
`Function#toString` source, even after its `JsCallableCode` was interned by
compiled entry, realm and signature. That code record is already traced through
every live function and its source is immutable for the compiled definition.

`js_finalize_function` now creates `source_text` only when the shared code
record has none. Function names remain per value because later anonymous-name
inference is evaluation-specific. The finalizer still roots the fresh function
across the first allocation and reloads it before publishing the source edge;
subsequent closures reuse the traced code-owned source without adding a new
rooting scheme (**S1.11**, **D5.3**, **D6.2.2v2**, **D8.4.3v2**).

The deferred-MIR metadata fixture now creates two closures, verifies their
independent captured values, descriptors and equal `Function#toString` source.
It passes with `LAMBDA_GC_FORCE_EVERY=1` and
`LAMBDA_GC_POISON_FREED=1`. The wider lazy-metadata reflection fixture passes
normally but fails under that forced schedule on the frozen C14 archive as well
as this candidate, so it is pre-existing control evidence rather than a result
of this leaf.

Candidate
`temp/tune14/lambda_t14_function_source_cache_release.exe`
(`138ac17d9919e4e6a5d1d862e62c5cfabe9bd930a58abcc10a07ab9ca9510c82`)
passed the three-engine Navier oracle. Its exact C14 31-pair alternating replay
on `awfy/havlak` against
`4b140f0c150d631033f896290c051e3536dbfa429d6341b26ed6f941def5d127`
has 31 `ok` pairs and equal stdout. Medians improve from 13,811.743 to
13,267.151 ms (candidate/control 0.960570; one-sided 95%
paired-bootstrap upper bound 0.963009; 31/31 wins). The archive is
`temp/tune14/paired_havlak_function_source_cache.json`. This accepts one
allocation/call residual leaf; it does not close the other T14-7 families or
establish a full-matrix result.

Post-change validation passed `make test-lambda-baseline` 5,717/5,717,
including optimizer 63/63 and MIR forced-GC stress 217/217. `make
test262-baseline` passed 40,261/40,261 in 169 batches in 60.1 seconds, with
zero batch-unstable, slow, failed or baseline-regressed entries. The Test262
run rebuilt and executed the same release candidate hash recorded above.

Do not add overlapping inclusive samples into a promised speedup. Do not label
a generic helper `NO_GC` because its frequent branch is nonallocating. A shared
leaf and full semantic miss must retain truthful effect contracts, completion
handling and precise liveness (**D5.3**, **D6.2.2v2**, **D8.4.3v2**).

**Exit:** each family has either an attributed implementation with paired
evidence, or a measured no-change/deferred disposition that names the remaining
cost. Full JS already beats historical MVP on some allocation-heavy workloads;
the MVP's private heap is not evidence for replacing the full engine's collector.

### T14-8 — Native contract, consolidation and full acceptance

- [ ] Complete the T13-1 entry matrix: default/explicit MIR, explicit AST,
  old size boundaries, document scripts/modules, CJS/ESM, eval/dynamic functions,
  batch/cache, nested languages and inherited interpreter-global state.
- [ ] Verify actual native entry publication/execution and generator cleanup,
  including opt=0, admitted lazy-native mode, timeout/failure, retained callbacks
  and repeated contexts. Preserve explicit AST admission; do not confuse AST
  unsupported cases with MIR correctness or silently replay effects.
- [ ] Resolve the document-module routing residual under **D8.1.3v11** or
  document tested unreachability from selected MIR. A source-string scan alone
  cannot close the entry-matrix requirement.
- [ ] Recheck inherited `list`, `crypto_sha1`, binarytrees/deriv and other
  repeatable losses using genuine controls. More than 3% triggers mandatory
  investigation; smaller systematic regressions still require disposition.
- [ ] Remove abandoned experiments, duplicated primitives, unused tuning flags,
  redundant conversions and temporary fact verifiers. Keep useful diagnostics
  disabled by default. Do not reduce LOC by stripping comments/blank lines.
- [ ] Close §3.4's reuse ledger for each common change: two live consumers,
  retired duplicate sequences, profile-owned semantics and untyped Lambda
  reachability. Explain any newly profile-specific implementation by its
  different contract, not by the location of the existing helper.
- [ ] Run the complete gates below on the final source and identified binaries.
  Preserve all statuses and explain any remaining performance miss.
- [ ] Update this plan's progress and link durable evidence. Add a carryover
  disposition to the historical Tune13 record without claiming that new results
  were measured on its old binaries. Sync implementation-status documentation
  only for behavior actually verified; formal semantics/design are unchanged.

**Exit:** required structural/correctness packages pass, conditional work has
evidence-backed dispositions, final performance/memory results are reproducible,
and confirmed regressions are resolved or remain explicitly open. Do not equate
implementation completion with attainment of the QuickJS/MVP milestones.

## 5. Validation and performance acceptance

### 5.1 Correctness and structural evidence

Use the current test population rather than historical passing counts.

| Gate | Required evidence |
|---|---|
| Focused JS semantics | Boundary/miss fixtures for each package; correct output, evaluation count/order and error identity. Extend existing test owners. |
| Optimization contracts | `make test-js-opt`; trace admissions and misses independently of timing. This target builds debug: rebuild release before any performance run. |
| Finalized MIR | `test_js_mir_emission_gtest`, relevant shared emission tests and `test_mir_ratchet_gtest`; inspect the hot region and complete fallback, not just an `_n` name. |
| Precise GC | `make test-mir-gc-stress` and focused JS runs with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; fast/miss joins, arrays, closures, throws, captured scalars and retained artifacts. |
| Runtime baselines | `make test-lambda-baseline` and `make test262-baseline` after the final changes. Retain Tune13's full-Test262 closeout with `make test262-full`; report baseline regressions separately from broader unsupported cases. |
| Shared consumers | Typed/untyped Lambda controls; affected document/Radiant baseline and module/eval/batch paths when their compiler/runtime behavior changes. |
| Reuse contract | §3.5 unannotated Lambda/JS fixtures reach the shared physical primitive; old copies retired; profile-specific misses and ownership preserved. |
| Ownership/effects | Existing GC effect/hazard checks; truthful call effects, no conservative native-stack scan and no roots removed solely for speed. |

New Lambda `.ls` fixtures require matching `.txt` expected outputs. Test262
failures/crashes/timeouts must be investigated in the runtime; never modify its
harness to mask them. Inspect deterministic finalized MIR before any necessary
profile-specific zero-slack budget edit (**D8.6.1–D8.6.3**).

### 5.2 Measurement protocol

1. Use release builds only. Archive C14 and each candidate under unique exact
   identities; capture external-module/configuration differences. Diagnostic
   builds and dump/profile runs do not supply accepted elapsed-time samples.
2. For each implementation package, run at least 11 alternating
   control/candidate pairs on targets, representative misses and controls.
   Compare each predecessor as well as C14. Publish raw samples, valid-output
   accounting and paired uncertainty. Increase pairs where the estimate is
   uncertain; do not keep only favorable reruns.
3. Before closeout, pair all 63 full-JS rows against C14, and collect a fresh
   same-session complete full-JS/MVP/QuickJS matrix with at least three process
   samples per engine/row. Use a second independent complete session to confirm
   any claimed cross-engine milestone.
4. Preserve the standard runner's build, profile, power, output and Test262
   gates. Explicitly request QuickJS and MVP: the current default engine list
   omits them. Run timed processes without concurrent build/profiling load.
5. Report startup/lowering/native generation and outer process time separately
   from workload time. Include peak RSS, retained bytes, cold/reused contexts
   and native code volume. A warm-loop win cannot erase a cold-start regression.
6. Report every row/status. Failure or timeout prevents claiming complete
   63-row acceptance. An explicit matched subset can be diagnostic but cannot
   replace the acceptance population or silently shrink its denominator.

Define, for valid positive row medians:

```text
G(A/B) = exp(mean_i(log(time_A_i / time_B_i)))
T(A/B) = sum_i(time_A_i) / sum_i(time_B_i)
```

Also report row win counts, suite geomeans, worst losses, and the timing range
or ordered samples. `T` is the ratio of sums, not the arithmetic mean of row
ratios. Node-normalized historical comparisons remain labelled proxies.

### 5.3 Explicit milestones

These are performance acceptance targets, not changes to language semantics or
guaranteed consequences of any individual package.

| Milestone | Target | Required qualification |
|---|---|---|
| Structural completion | All required package exits and current correctness gates pass. | List conditional dispositions and remaining unsupported specializations. |
| Beat QuickJS | Fresh `G(full JS / QuickJS) < 1.00`. | Complete 63 rows in both confirmation sessions; publish `T`, suites and losses. This is an aggregate claim, not faster on every row. |
| Tune13 stretch carried forward | Fresh `G(full JS / QuickJS) <= 0.80`. | Same population/protocol; do not retain an obsolete historical gap as the baseline. |
| Close to MVP, Tune14 planning target | Fresh `G(full JS / MVP) <= 1.20`. | Same audited source/input/work; all 63 supported and valid, with full JS semantics retained. Unsupported MVP rows must be resolved or the milestone remains unverified. |
| Absolute workload cost | Reduce `T(candidate/C14)` and publish `T(full JS/MVP)` and `T(full JS/QuickJS)`. | No unsupported promise that one geomean target implies the same sum ratio. |
| Regression/operational gate | No unresolved confirmed correctness regression; investigate timing, startup and RSS regressions. | More than 3% repeatable elapsed regression requires root-cause analysis; smaller systematic losses remain visible. |

If the mandatory implementation work is finished but a performance target is
missed, record **implementation complete; performance milestone unmet** and
retain the measured residual. If a required structural or correctness exit is
still open, the plan remains partial. Avoid claiming completion from effort,
attempted experiments, a favorable subset or historical baseline passes.

### 5.4 Command templates

The following are future execution templates, not commands run to create this
plan. Replace archive names with the exact C14/candidate identities established
by T14-0; create output directories beneath `./temp/` before exploratory work.

```sh
make test-js-opt
make test-mir-gc-stress
make test-lambda-baseline
make test262-baseline
make test262-full
make release

python3 test/benchmark/run_paired_benchmarks.py \
  --control test/benchmark/exe/lambda-tune14-control \
  --candidate test/benchmark/exe/lambda-tune14-candidate \
  --language js --suite jetstream --bench navier_stokes \
  --pairs 31 --timeout 180 \
  --output temp/tune14/navier_paired.json

python3 test/benchmark/run_paired_benchmarks.py \
  --control test/benchmark/exe/lambda-tune14-control \
  --candidate test/benchmark/exe/lambda-tune14-candidate \
  --language js --pairs 11 --timeout 180 \
  --output temp/tune14/final_paired.json

python3 test/benchmark/run_standard_benchmarks.py \
  --engines mir,c2mir,lambdajs,mvpjs,quickjs,nodejs \
  --suite r7rs,awfy,beng,kostya,larceny,jetstream,text \
  --runs 3 --timeout 180 --cooldown 10 --typed \
  --results-output test/benchmark/js_mvp/tune14/final_session1.json \
  --report-output test/benchmark/js_mvp/tune14/Overall_Final_Session1.md \
  --report-title 'JS Tune14 final session 1' \
  --log-dir temp/tune14/final_session1
```

Archive names above are placeholders and must not be reused for different bytes.
The standard runner may rebuild/archive independently; verify its recorded hash
against the paired candidate, rather than assuming a shared filename or commit
means identical executable content. Confirm current runner/report support for
the complete engine list before the long run, without disabling its guards.

## 6. Evidence storage and progress record

Exploratory dumps, probes and profiles belong under `./temp/tune14/`.
Accepted raw JSON, manifest, hashes, commands, focused reproducer references and
generated reports belong under `test/benchmark/js_mvp/tune14/`. Do not hand-edit
published timing cells or overwrite Results46/47, Tune12, Tune13 or MVP v1 data.

Starting evidence, with its limits:

- [Result46 JSON](../../test/benchmark/benchmark_results_v46.json) and
  [Result47 JSON](../../test/benchmark/benchmark_results_v47.json): durable
  snapshots with metadata and raw samples, subject to §2's mixed/session limits.
- [MVP release acceptance](../../test/benchmark/js_mvp/JS_MVP_Release_Acceptance_20260915.md):
  historical narrow-runtime performance; not a full-JS conformance oracle.
- [Tune13 implementation record](../impl/JS_Tune13_Impl.md): landed mechanisms,
  focused verification and open package exits; unpaired Hyphen observations
  do not become package A/B evidence.
- `test/benchmark/js_tune14_manifest_v1.json` and
  `verify_js_tune14_navier_oracle.py`: the versioned current-source contract
  and Node/LambdaJS semantic control. The one-pair same-binary smoke artifact
  validates its runner wiring only; it supplies no timing claim.
- §3.2's source-owner map: verified common primitives, profile-local copies
  and proposed extraction boundaries. Record two-client migrations and
  untyped Lambda controls with the implementing package's evidence.
- `temp/result47_ljs_navier_v46_vs_v47.json`: the prior 31-pair regression
  diagnostic. Its weak stdout oracle is explicitly addressed in T14-0.
- `temp/result46_47_analysis/{comparison.json,mir_provenance.json,*.mir}`:
  arithmetic and six release diagnostic executions used in this analysis.
  Key findings are reproduced in §2 so the plan survives scratch-file removal.
  Reproduce and archive whatever is used for implementation acceptance.

| Package | Current status | Evidence needed to close |
|---|---|---|
| T14-0 | [~] C14 archive, 63-row same-session control and Node/LambdaJS/QuickJS Navier oracle landed | Record remaining toolchain/native-module and phase/owner census, replay later archives under the three-engine oracle, and collect the required independent confirmation session. MVP is release-unavailable, so its milestone remains unverified. The current Navier helper census directs T14-3 toward guarded ordinary numeric reads, not generic comparisons. |
| T14-1 | [~] Reference repair and archived-oracle audit landed | Frame-15 canonical Navier checksum now exercises the fixed evaluated-Reference boundary. Original R47 and C14 pass the density oracle, while R46 and R47-repair fail, so complete the remaining boxed-kernel/generic-array work with valid-control evidence. |
| T14-2 | [~] Guarded native/local updates, discarded generic-update fusion, Number-pair bitwise head and primitive relational head landed | Extend the landed shared F64 boxer, guarded local `++`/`--`, immediate-body assignment-alias compound admission and both operand orders of the narrow typed-view Number carrier to complete Number/update regions; retain generic parity and add comparison/join/range proofs. The `crypto_sha1` bitwise replay measures 0.823464 (upper 0.824575) against its exact predecessor; the Havlak primitive-comparison replay measures 0.997464 (upper 0.998807; 23/31 wins); and the Navier-only update fusion result is 0.992072 (upper 0.994343). The public Number update head was reverted after `hashmap` reached only 0.995100 (upper 1.001581; 16/31 wins). |
| T14-3 | [~] Address migration, typed-view, FFT carrier and companion dense-read slices landed | `em_element_address` now serves existing LambdaJS paths and admitted untyped checked, dense and pointer loads. Guarded Int32 direct-call forwarding reaches quicksort `partition` with a 0.183468 paired-release ratio over one-hop evidence; the shared primitive typed-array setter then reached 0.902061 and generation-validated ArrayNum reads 0.830905. The nested FFT carrier result is 0.975130 (upper 0.985491), followed by the symmetric operand-order result of 0.846079 (upper 0.871704) against its exact predecessor. A scalar-safe companion own-element leaf changes the Navier profile from 36,190,970 generic Number-index helper calls to zero and measures 0.357866 (upper 0.359286; 31/31 wins) against its exact update-fusion predecessor. The copy-store and over-broad arithmetic variants were rejected. Broader ordinary-array region evidence remains open. |
| T14-4 | [~] Shared scalar-hoister audit complete; mutable storage remains unadmitted | Build an effect-bounded witness with profile-valid invalidation and measured query reduction. The raw companion leaf was rejected as a slower immediate read, not retained as a region mechanism. |
| T14-5 | [~] Ordinary Map named reads skip impossible Function, Window and special-length setup; recursive record-return recipes and effect-bounded dynamic constructor RHS recipes reuse common shape/slot machinery | Three Richards replays and a 31-pair binarytrees recursive-record replay are accepted. Dynamic constructor values now reserve placeholder slots and take shared transitions only when their RHS cannot observe the receiver or invoke direct `eval`; focused normal and forced-GC tests cover admission and refusal. Release profiling and paired measurement, indirect escape/descriptor mutation coverage, constructor/alias propagation, NameId transport, descriptor/enumeration coverage and broad paired results remain. |
| T14-6 | [~] Stability repair landed | Cache-rejection native-code lifetime, parallel prebuild publication and the direct-scope index are covered. The index returns the full Test262 batch matrix to 40,261/40,261 with zero unstable/slow entries; compiler/memory owner census, scaling fixes/dispositions and cold results remain. |
| T14-7 | [~] Havlak closure-source allocation leaf accepted | Profile the RegExp, string and remaining allocation/call families, then record an implementation or no-change/deferred disposition for each. The source-cache replay is 0.960570 (upper 0.963009; 31/31 wins) against C14, but is not a full-matrix claim. |
| T14-8 | [~] Runtime gates revalidated | Lambda baseline (5,717/5,717) and Test262 baseline (40,261/40,261 fully passing; zero unstable, slow or regressed) pass after the current T14-2, T14-3, T14-5, T14-6 and T14-7 work. Native entry/lifetime audit, two-client reuse ledger, durable final matrices and milestone status remain. |

For every landed package record the exact revision/binary, predecessor, changed
proof or primitive, admitted/refused cases, semantic/GC/MIR checks, paired result
and uncertainty, code/compile/RSS impact and remaining work. No implementation
or runtime validation is claimed by this planning document itself.
