# Lambda Tune32 — Recover Typed Optimizations in Untyped Programs

- **Version:** 1.3.0, 2026-10-02.
- **Status:** Phase I COMPLETE; Phase II PLANNED, not implemented.
  T32-1 through T32-4 are implemented and validated;
  T32-5 is closed as an investigated deferral under its conditional gate.
  All four Phase I pilot targets and its round geomean objective are met.
- **Source inspected:** Phase I control
  `84748fd62ccaba5c82410fa23b3124c48856e52f`; Phase II planning anchor
  `261633ea2`, with the source experiments recorded in §14.
- **Scope:** improve the existing untyped Lambda benchmark sources by
  supplying sound inferred or guarded facts to the existing MIR Direct
  lowering. Preserve the typed path and boxed fallback.
- **Predecessors:** [Tune31](Lambda_Impl_Tune31.md),
  [Tune30](Lambda_Impl_Tune30.md), and the
  [Result49 typed analysis and implementation record](Lambda_Benchmark_Result49_Typed_Analysis.md).
- **Authority:** [formal semantics](../../doc/Lambda_Formal_Semantics.md),
  [formal design](../../doc/Lambda_Formal_Design.md), and
  [documentation convention](../../doc/Doc_Convention.md). This plan changes
  no ruling. T32 numbers identify implementation work, not new semantic ADRs.

Sections 2 and 5–10 preserve the control analysis and implementation plan.
Section 13 records the final changes, measurements, gates and dispositions.
Section 14 records the follow-up review and benchmark-source experiments.
Section 15 specifies the Phase II implementation plan; its tasks and performance
targets are prospective, not completed work or measured improvements.

## 1. Objective and evidence boundary

The immediate opportunity is to make more untyped code reach optimizations
that already work for typed code. Native parameter inference, array lanes,
guarded map reads, leaf inlining and loop facts already exist. Tune32 extends
their coverage and fixes lost facts; it does not introduce a second optimizer.

Success means that an unchanged untyped source executes faster on a candidate
release than on a fixed control release, with matching results and preserved
language behavior. Adding annotations to a benchmark is a diagnostic tool,
not an engine improvement. Rewriting a benchmark algorithm or moving work
outside its timer cannot satisfy this objective.

There are three distinct comparisons throughout the work:

1. **Engine A/B:** identical source, different exact release binaries. This
   is the performance acceptance evidence.
2. **Annotation isolation:** original and minimally annotated sources on one
   binary. This identifies which missing facts could matter.
3. **Typed reference:** the checked-in typed benchmark on the same binary.
   This shows the remaining gap, subject to a source/workload audit.

Every pilot report must include all three when applicable. A typed reference
is not automatically an annotation-only version or a semantic lower bound.

## 2. Measurements that motivate the work

### 2.1 Recent history

The following values are recomputed over the same 63 benchmark names from
[Result48](../../test/benchmark/benchmark_results_v48.json),
[Result49](../../test/benchmark/benchmark_results_v49.json), and
[Result50](../../test/benchmark/benchmark_results_v50.json). They use the
forced-JIT execution measurements, excluding startup and compilation.

| Result | Untyped / C2MIR | Typed / C2MIR | Untyped / typed |
|---|---:|---:|---:|
| 48 | 5.459x | 3.228x | 1.691x |
| 49 | 5.689x | 3.162x | 1.800x |
| 50 | 5.307x | 2.744x | 1.934x |

Across R48 to R50, the geometric mean of per-row execution-time ratios is
0.9732 untyped and 0.8510 typed: approximately 2.7% and 14.9% lower time.
This supports the observation of slower recent untyped progress, but is not
causal engine evidence. Sources, typed ports and measurement conditions vary.
C2MIR here measures independent C ports; the removed Lambda C-text backend
stays removed (**D1.6**).

Derived values and all R50 row timings are in
[metrics.json](../../temp/untyped_analysis_20261002/metrics.json).

### 2.2 Exact release and diagnostic provenance

All fresh diagnostics in this plan used:

- Binary: `test/benchmark/exe/lambda-v50-a9489bc329`.
- SHA-256: `10f5c44a312ef039867fac1fb17b0bd10b1f044dd0ba48b066885adde9a19490`.
- Platform: macOS arm64; `LAMBDA_TIER=jit`.
- Timing: the benchmark's in-program execution marker, not process wall time.
- Date: 2026-10-02, Asia/Singapore. JSON timestamps may be UTC.

The archived R50 release is an evidence anchor. It is not a substitute for a
new implementation-start control built from the checkout on which Tune32 is
implemented. The six checked-in sources in the three-way comparison had no
diff against the R50 revision when inspected. Source and binary hashes are
recorded in the artifacts.

### 2.3 Minimal annotations and fully typed controls

The minimal variants make exactly these changes to copies of the originals:

| Benchmark | Diagnostic change | All other source behavior |
|---|---|---|
| `kostya/primes` | `var flags` becomes `var flags: bool[]` | Algorithm, input, loops and timer unchanged |
| `text/fast_diff` | `score_diff(left, right)` parameters become `string` | String-based LCS and existing typed table/counters unchanged |
| `kostya/levenshtein` | `levenshtein(s1, s2)` parameters become `string` | Two-row algorithm, helpers, array construction and timer unchanged |

The harness label “untyped” means the checked-in unsuffixed source; it does
not mean that every annotation has been erased. In particular, the original
`fast_diff.ls` already annotates its numeric table and several scalar locals.

The initial two-way runs used nine alternating pairs. Every pair completed,
the diagnostic won 9/9 pairs in each row, and normalized outputs matched:

| Benchmark | Original ms | Minimal annotations ms | Speedup |
|---|---:|---:|---:|
| primes | 17.316 | 2.044 | 8.47x |
| fast_diff | 246.844 | 52.199 | 4.73x |
| levenshtein | 11.286 | 7.664 | 1.47x |

The subsequent three-way run included the checked-in typed versions. It
excluded one warmup per form, then ran all six order permutations twice:
12 measured runs per form. All outputs matched, and source/binary hashes
remained stable. Medians below are rounded from the raw JSON:

| Benchmark | Original untyped ms | Minimal annotations ms | Fully typed ms | Interpretation |
|---|---:|---:|---:|---|
| primes | 16.876 | 2.066 | 2.065 | One array declaration captures essentially the whole typed advantage |
| fast_diff | 242.183 | 51.768 | 46.363 | Two string declarations capture most of the gap; the typed reference also changes representation before timing |
| levenshtein | 11.262 | 7.609 | 2.802 | String declarations explain only part of the gap; typed remains 2.72x faster than the partial variant |

The two tables are separate runs; do not combine their samples. Primes had
one 44.519 ms original-source sample in the three-way run, so retain the raw
distribution rather than treating the rounded median as an exact constant.

**Source audit matters:** typed `fast_diff2.ls` converts the static strings to
`int[]` code arrays before its timer and passes those arrays into the LCS
kernel. Its remaining 10.4% advantage over the string-only variant is not
an isolated annotation effect. Typed Primes and Levenshtein retain their
respective algorithms, with additional declarations on parameters, locals,
arrays and results.

The minimal variants narrow the source contracts. They demonstrate what
information helps for these accepted benchmark inputs, not permission to
impose those contracts on arbitrary untyped calls (**D3.3.1v2–D3.3.3v3**).

### 2.4 Additional evidence and limits

- **Tail recursion:** nine source pairs of `divrec.ls` and `divrec2.ls`
  measured 5.524 versus 1.122 ms, all output-equal and 9/9 typed wins.
  Untyped `divrec_div_helper` retains a recursive MIR call; typed emits a
  loop. Other annotations also differ, so 4.92x is not a TCO-only attribution.
- **Records:** R50 Raytrace3d is 85.906 ms untyped and 12.862 ms typed,
  a 6.68x gap. In fresh finalized MIR, untyped `triangle_intersect` has
  15 `fn_member_by_id` sites and generic arithmetic; the typed body removes
  those sites. These are static sites, including possible cold paths, not
  executed call counts or a fresh CPU profile.
- **FFT:** untyped `four1` still contains generic comparison, arithmetic and
  index helpers. It is a secondary proof-propagation pilot, not a reason to
  assume every native-loop issue from Tune30 is still present.
- **Large typed gaps:** Knucleotide replaces general k-mer maps with compact
  counters; Log Pipeline scans directly instead of building split fields and
  records. Their full ratios cannot be credited to inferred types alone.

Initial evidence was collected under `temp/untyped_analysis_20261002/`;
the preserved copies are now under `temp/tune32/`:

| Artifact | Purpose |
|---|---|
| [annotation_diagnostics.json](../../temp/tune32/annotation_diagnostics.json) | Nine-pair minimal-annotation experiments, hashes and uncertainty |
| [three_way_typed_comparison.json](../../temp/tune32/three_way_typed_comparison.json) | Original / partial / fully typed balanced measurements |
| [divrec_source_pair.json](../../temp/tune32/divrec_source_pair.json) | Typed/untyped recursion comparison |
| [provenance.json](../../temp/tune32/provenance.json) | Release identity and MIR-dump source hashes |
| [mir_census.json](../../temp/tune32/mir_census.json) | Static finalized-MIR function/call census |

These are historical diagnostics; the current-control acceptance evidence is
in section 13. Instrumented MIR-dump runs are not timing samples.

## 3. Semantic and implementation constraints

| Formal authority | Requirement for Tune32 |
|---|---|
| **D3.3.1v2, D3.3.2v2** | Accepted programs evaluate identically with weaker or stronger inference. Keep entry-shape specialization separate from body/result inference; infer an implementation, not a new runtime contract. |
| **D3.3.3v3, D3.3.4** | Binding-scoped narrowing is not an array certificate. Prove the actual carrier, preserve legal widening, and retain nullable indexed results until a valid proof removes absence. |
| **D3.3.5, D8.2.5v3–D8.2.6** | Extend existing declarative result relations, the shared analysis schedule and `MirValue` lowering. Do not add a parallel type oracle or node-fact database. |
| **D8.3.1v2–D8.3.4v3** | Raw entries require complete exact shape keys and static proof or boxed-entry guards. An inferred mismatch reaches boxed source semantics, not a declared-type admission error. Preserve `var` homes and boxed fallback. |
| **D8.4.1v2** | No mutable inline caches, feedback vectors or runtime-patched specialization. Use compile-predicted shapes and immutable guards. |
| **S4.1.1–S4.1.5, D2.5.2v3** | Preserve int poison/saturation and sized-int behavior; use each carrier's actual null sentinel. Native bool absence is not interchangeable with boxed null or false. |
| **S7.1.1v3, S7.1.3v2** | Invalid reads yield null; invalid writes raise through the hard error channel. Preserve operand evaluation order and error propagation. |
| **S9.1.2–S9.1.3, S9.2.2** | Value snapshots, plain-parameter isolation, exclusive borrows and caller write-back remain observable obligations. |
| **D4.3.1, D4.4.4v4–D4.4.6** | Reload movable data-zone pointers after relevant allocation points; invalidate path/ownership facts after conflicting writes, sharing or replacement. |
| **D5.3.1–D5.3.4** | Keep canonical roots current at collecting calls. Native pointer proofs do not replace ownership. Reuse emitter root handling and precise `RootFrame` / `Rooted` helpers. |
| **D8.6.1–D8.6.3** | Review emission growth explicitly, inspect finalized release MIR, and run forced-GC plus poison oracles. Never weaken a test to conceal incorrect behavior. |

Use the existing helpers before adding another variant; extract shared code
before a third equivalent lowering appears. Production changes follow the
repository's C++17 C+ conventions and custom `lib` containers. No vendor
patch, conservative-stack GC, log configuration edit, or backend replacement
is part of this plan.

## 4. Tracks and execution order

| Track | Deliverable | Priority / dependency | Current disposition |
|---|---|---|---|
| T32-0 | Fixed release controls, source audit, diagnostic reproduction | First | Complete |
| T32-1 | Correct inferred packed-bool producer and store facts | First engine change | Implemented; paired target met |
| T32-2 | Inferred-string character fusion and source-fact propagation | After T32-0; measure separately from T32-1 | Implemented; paired targets met |
| T32-3 | Isolate and repair remaining Levenshtein array/scalar/call facts | After string work | Scalar forwarding/inlining retained; array swap not justified; target met |
| T32-4 | Tail-loop eligibility for proven inferred specializations | After T32-0; separate patch | Implemented; paired target met |
| T32-5 | Guarded record facts through indexed reads and arithmetic | After core tracks; evidence-gated expansion | Investigated and deferred; see 13.3 |
| T32-6 | Combined release validation and closeout | After retained changes | Complete; all required final gates passed |

Do not merge all mechanisms into one candidate. Keep a fixed overall control
and a preceding accepted release for each isolated track, so both incremental
and cumulative effects can be measured. A rejected experiment is recorded
with its evidence; it is not reported as an implemented optimization.

## 5. T32-0 — Establish controls and diagnostics

1. Inspect the worktree and archive a clean implementation-start release
   built with `make release`. Record commit, relevant dirty diff if any,
   build/profile configuration, binary hash and source manifests. Keep this
   immutable binary separate from the R50 archive and mutable `lambda.exe`.
2. Reproduce the original / minimal / typed comparisons on that control.
   Use the same input files, timer placement and output checks. Include
   Primes, Fast Diff, Levenshtein and Divrec; audit typed Raytrace3d before
   interpreting its gap. Preserve the original untyped sources byte-for-byte.
3. Obtain finalized MIR with `LAMBDA_MIR_DUMP_PATH` and `LAMBDA_TIER=jit`.
   Use `utils/analyze_mir_gap.py`; do not count imports as executed helpers.
   Optional specialization diagnostics use the existing
   `LAMBDA_MIR_SPECIALIZATION_PROFILE` gate. Do not modify `log.conf` to
   force a diagnostic channel on.
4. Record where a fact is lost: producer, local binding, container element,
   call argument, native entry, indexed result, reassignment, loop join or
   return. Separate semantic type, physical carrier, nullability and lifetime.
5. Preserve initial diagnostics and later acceptance JSONs under a dedicated
   local `temp/tune32/` evidence directory when implementation
   begins. Keep builds, logs and transient variants in that directory.
   Include commands, source hashes, binary hashes, expected output and status.

**Exit:** all pilots have a reproducible control result and source audit.
If a prior gap is no longer reproduced, close that pilot with current
evidence instead of implementing against the historical timing.

## 6. T32-1 — Preserve packed-boolean producer facts

### 6.1 Confirmed mechanism

`fn_fill` in [lambda-vector.cpp](../../lambda/runtime/lambda-vector.cpp)
creates `ArrayNum(ELEM_BOOL)` for a nonempty boolean fill. In
[transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp), the local
initializer handling around line 22641 still describes `fill(n, bool)` as
generic `Array` and selects `LMD_TYPE_ARRAY`. Native boolean stores already
exist in `emit_array_num_direct_store`, selected by the ArrayNum/BOOL case.

The Primes dump contains five static `fn_array_set` sites without the
annotation. The single array annotation selects native stores with cold
fallbacks; it also changes read proof handling. The measured whole-workload
gain cannot be attributed exclusively to the store calls without a separate
executed-work census.

### 6.2 Implementation

1. Trace `mir_fill_initializer_element_type`, initializer carrier selection,
   `MirVarEntry` element facts and `has_elem_type_invalidation` as one chain.
   Correct the stale boolean carrier classification using the same producer
   fact path already used for integer and floating fill.
2. Account for `fill(0, true)`: the runtime currently returns an empty generic
   Array before its element-type dispatch. A boolean element fact alone is
   not proof that every result has an ArrayNum header. Retain a receiver guard
   or prove the nonempty producer; do not change runtime semantics to simplify
   this optimization.
3. Reuse the existing boolean load/store policies and
   `emit_array_num_direct_store(MIR_ARRAY_NUM_STORE_BOOL, ...)`. Carry actual
   carrier, value-lane, extent and ownership facts independently. Permit a
   raw store only on the successfully proved path.
4. Keep widening and COW replacement on the existing fallback, including
   publication of a replacement owner. Do not attach a declared `bool[]`
   certificate to an open array just because its initial values are boolean
   (**D3.3.3v3**).
5. Audit bool facts through aliases, rebindings, loops and `var` calls. Share
   transfer rules with the existing integer/float lane analysis; avoid a
   benchmark-specific boolean branch that bypasses invalidation.

### 6.3 Tests and mechanism gate

Add or extend focused fixtures for nonempty and zero-length fill, negative
count failure, finite loops, unknown indices, out-of-range reads/writes,
mixed writes, replacement by another carrier, nullable boolean results,
captured snapshots, alias rebindings and `var` write-back. Test plain
parameters separately from borrowed parameters.

Reuse the coverage around `tune15_fill_witness`,
`tune26_dense_declared_bool`, `tune26_plain_bool_param_lazy_snapshot` and
`tune31_bool_null_equality`; add a `tune32_inferred_bool_fill` fixture with
`.ls`, `.txt` and `.mir-check` sidecars where existing coverage is insufficient.

The untyped successful Primes store path must use native boolean stores.
Cold checked fallbacks may remain. Pin the intended function/branch rather
than demanding a module-wide absence of every array helper. Both original
and typed Primes remain correctness and performance controls.

**Initial performance objective:** unchanged untyped Primes at no more than
0.25x control time, and close to its contemporaneous typed reference. This
is a proposed acceptance target informed by the annotation experiment, not
a guaranteed 8.47x engine gain.

## 7. T32-2 — Reuse character fusion for inferred strings

### 7.1 Two distinct missing facts

`mir_declared_ascii_char_index_expr` currently requires
`root->binding->declared_type` before two indexed characters can use
`emit_string_char_pair_compare`. `mir_proven_nonnull_string_root` and the
whole-loop `mir_ascii_scan_plan` likewise require declared string evidence.

Levenshtein already reaches inferred string indexing, but its unannotated
form emits `fn_string_ascii_at` plus generic equality. The annotation-only
variant reaches the paired-character path. Fast Diff also loses string
information through its nested `pairs[index][position]` inputs, so removing
the declared-only fusion gate alone may not close its gap.

### 7.2 Implementation sequence

1. Separate **fusion eligibility** from **permission to omit guards**.
   An inferred string may use the shared fused operation while retaining tag,
   null, ASCII and index guards. Absence of a source annotation is not itself
   a reason to materialize two characters; presence of an inferred TypeId is
   not itself a reason to delete checks.
2. Admit existing native string-entry witnesses and dominating flow proofs
   through the common carrier/type machinery. Verify that a proof describes
   the current binding and survives intervening calls or reassignment.
   Do not set `declared_type` or manufacture a checked source contract.
3. Trace Fast Diff's literal pair collection through each index, argument and
   callee parameter. Recover a justified string fact or a complete predicted
   function-entry shape. If the call edge is open, retain `_b` and its exact
   guard. Do not coerce a mixed array argument into string to reach the fast
   body (**D8.3.1v2–D8.3.3**).
4. Reuse `emit_string_char_pair_compare` and its existing byte/Unicode
   fallback. Evaluate every operand once, preserving the existing lowering
   order (**S7.7.1**); root the left
   source while evaluating the right. Preserve index-value evaluation even
   when a receiver later fails the fast guard.
5. Only after paired-character coverage passes, allow the existing ASCII
   scan planner to consume equivalent inferred non-null and stability
   proofs. Keep its entry ASCII/bounds checks and generic loop. Do not add
   per-site runtime caches or silently broaden the admitted loop shape.

### 7.3 Existing test contract to revisit explicitly

At implementation start, `test/mir/lambda/tune31_inferred_string_char_pair.mir-check`
required `fn_string_ascii_at` and forbade `fn_string_char_eq`. Its description
stated that the inferred path was deferred until a separate regression gate
certifies it. This is an implementation boundary, not a formal ban on fusion.

First preserve and run that test to document current behavior. When the new
inferred path is implemented, update its emission expectation together with
the code and expanded positive/negative coverage. Keep its semantic output.
Do not merely reverse the expectation before proving the new admission path.

### 7.4 Correctness and acceptance

Cover inferred parameters, direct and escaped calls, mixed caller sets,
computed sources, nullable sources, reassignment and captured bindings.
Exercise equality and inequality; ASCII, accented UTF-8 and supplementary
characters; empty strings; one or both indices absent, negative or past-end;
unsupported key types; and errors/effects in argument evaluation. Check
formal invalid-read behavior under **S7.1.1v3**, not an outdated code comment.

Extend `result49_char_pair_compare` / `tune31_inferred_string_char_pair` and
`result49_ascii_scan` where appropriate. A semantic differential comparison
against ordinary indexed equality must accompany MIR pins and forced GC.

**Mechanism gate:** eligible untyped character comparisons use the existing
fused path, while non-string/open cases retain correct generic behavior.
**Initial performance objectives:** unchanged Fast Diff <=0.30x control time;
unchanged Levenshtein <=0.75x control time for the string-only engine step.
Report the minimal-annotation and fully typed references in the same run.
Do not implement the typed Fast Diff preconversion in its untyped source.

## 8. T32-3 — Resolve the rest of the Levenshtein gap

### 8.1 Required diagnosis

The measured 7.609 versus 2.802 ms gap after string annotations is too large
to call string fusion the complete solution. The fully typed source also
annotates the two working arrays, lengths, counters, costs, `min2`/`min3`,
string-builder parameters and return values. Their individual contributions
have not been measured.

Create temporary source variants from one fixed original and isolate:

| Variant family | Change | Question |
|---|---|---|
| A | Only `prev` and `curr` become `int[]` | Are array store/read or ownership facts lost? |
| B | Only lengths, counters and `cost` become `int` | Are loop joins and arithmetic results boxed? |
| C | Only `min2` / `min3` parameters and returns become `int` | Are call-result facts or leaf-inlining eligibility limiting the loop? |
| D | Only remaining producer/return declarations | Does string construction or result transport materially contribute? |

Measure these on top of the string-only variant and, where needed, directly
against the original to expose interactions. Combine only families that
show a signal, and always retain the fully typed control. Preserve `.txt`
expected outputs for any new Lambda fixture; keep diagnostic variants out of
the published benchmark population.

### 8.2 Conditional implementation

Follow the measured loss through `mir_expr_carrier_type`, indexed-result
proofs, `infer_param_types_batched`, return inference, `mir_inline_callee_ok`
and the loop/COW join machinery. Particular candidates are:

- **Array swapping:** `tmp = prev; prev = curr; curr = tmp` should preserve
  compatible element/carrier facts where proved. Ownership and representation
  are separate; sharing cannot be discarded just because both arrays contain
  integers. The existing `tune26_inferred_swap_lane` covers scalar element
  swaps into a declared array, not this complete array-binding permutation.
- **Nullable indexed values:** `prev[j] + 1` remains nullable without a valid
  extent proof. Carry loop relations into existing index and arithmetic
  lowering, or guard the region; do not turn every inferred `int[]` read into
  a non-null machine integer.
- **Scalar helper calls:** reuse existing native argument/result plans and
  leaf inlining when `min2`/`min3` are proved eligible. Preserve unknown or
  mixed caller behavior and int poison/absence rules.
- **Fixed-point precision:** if a call/return chain is unresolved, distinguish
  unknown-so-far from actual dynamic/conflicting evidence. The warning in
  `mir_callsite_join_specialization_type` explicitly forbids relaxing its
  consumer guards while that distinction is missing. Do not just raise the
  six-round cap or globally force native lanes.

Require a concrete lost-fact trace and a reproducer before each code edit.
If none of the isolated annotations explains a suspected mechanism, do not
retain that hypothesis as an implementation requirement.

### 8.3 Tests and exit

Test zero/one/multiple loop iterations, swapped arrays of different lengths,
one widened source, incompatible element lanes, a captured pre-swap snapshot,
branch-dependent assignments, nested loops, recursive scalar forwarding,
nullable/out-of-range reads and mixed numeric inputs. Check the current MIR
value representation at each call and join, not only the inferred AST type.

**Exit:** the residual gap has measured attribution and every retained fix
removes the corresponding hot work on the original source. The cumulative
Levenshtein stretch objective is <=0.40x the implementation-start control.
If it is missed, report the remaining gap and mechanism explicitly; do not
declare the typed/untyped problem solved from the string-only improvement.

## 9. T32-4 — Tail loops for stable inferred specializations

### 9.1 Current exclusion

Function lowering computes `use_tco` with an exclusion for
`tco_nfi->has_inferred_specialization`. The comment correctly protects an
open boxed slow body from jumping into a raw loop with coercing assignments.
Divrec demonstrates that this blanket exclusion also blocks an otherwise
native recursive body. Preserve the reason for the guard while refining its
scope.

### 9.2 Implementation

1. Inspect existing `should_use_tco`, tail-argument lowering, result transport
   and stack/iteration guards. Classify raw and boxed bodies separately.
2. Start with fixed-arity scalar self recursion whose recursive arguments are
   statically proved to preserve the complete current entry key. Reuse the
   existing TCO loop, parallel argument assignment and result/error epilogue.
   Keep pointer, `var`, closure, async and mixed-shape cases on their existing
   path unless independently justified.
3. Evaluate all next-call arguments before overwriting current parameter
   locations. Preserve their evaluation order, error exits, scalar-home
   ownership and any roots live across evaluation.
4. A tail edge that cannot prove the raw key continues to call the correct
   boxed entry through existing machinery. If its values can later be
   proved, specialize that edge separately; do not use coercing assignments
   to make an invalid back edge appear native.
5. Keep the complete boxed slow body. An open call entering it must execute
   source semantics, even if other direct callers always use integers.
   Preserve the existing exhaustion/iteration policy; removing its checks
   is not part of this optimization.

### 9.3 Tests and acceptance

Add `tune32_inferred_tco` coverage beside `tail_call_tco` and
`tail_call_tco_proc`: inferred integer and float recursion, argument
permutation, multiple tail branches, mixed external callers, recursive shape
changes, null/error cases, poison arithmetic, non-tail recursion and
allocation during argument evaluation. Use bounded fixtures for fallback
recursion and a deeper proved case to establish loop execution.

**Mechanism gate:** the eligible Divrec raw body has a loop back edge and no
self-call on its proved tail path; unproved edges still dispatch correctly.
**Initial performance objective:** unchanged Divrec <=0.40x control time,
with its fully typed source as a companion control. Measure this patch
separately so the historical full typed ratio is not mislabelled a TCO gain.

## 10. T32-5 — Guarded record facts through consumers

### 10.1 Scope and admission gate

This is a bounded second stage after the scalar/array/string tracks. Start
with Raytrace3d's `triangle_intersect`; use Towers, Richards, Deltablue,
Havlak and Hashmap as coverage/regression cases, not simultaneous rewrite
targets. Splay's real snapshot copying requires a separate ownership proof.

The inferred field-read path near `transpile-mir.cpp:27090` already compares
a predicted shape against the runtime header and falls back on a miss. Its
comment explicitly preserves boxed `Item` results on both branches.
`mir_expr_candidate_shape`, `mir_resolve_shape_hints`, the shared
`mir_shape_candidate` analysis and module-unique shape lookup already exist.
The missing work is coverage and usable facts through consumers, not adding
shape lookup from scratch.

Before expanding code generation, obtain an executed-work profile or census
on the current release: predicted-shape coverage/hits, generic field calls,
arithmetic helpers, boxing, allocations and copies. Static call counts alone
do not justify a multi-version region. Keep instrumentation separate from
the timed release.

### 10.2 Bounded implementation

1. Trace a record from constructor/return through array storage and index to
   the field read. Extend existing candidate propagation where it loses the
   shape; a candidate remains a guess until its exact runtime guard succeeds.
2. On a guarded hit, retain the field's actual storage lane through a small
   expression or basic block and reuse the typed arithmetic/branch emitter.
   The generic arm executes the original semantics. Join the two through
   `MirValue` and the established representation conversion boundary.
3. Establish separate proofs for receiver shape, receiver non-nullness,
   field presence, field lane, index extent and numeric domain. A field's
   name or a raw MIR register type proves none of the others.
4. Reuse a guard only while the receiver/parent path is unchanged. Unknown
   calls, conflicting mutations, COW replacement, escaped aliases and
   collecting calls invalidate the relevant facts or require pointer reload.
   Start with read-only regions; do not combine this step with new write or
   aggregate-lifetime conventions.
5. Bound code growth. Prefer improving an existing plan and small guarded
   consumers over cloning entire functions. Preserve **D8.4.1v2**: no mutable
   per-site cache or profiling-driven recompilation.

### 10.3 Tests and disposition

Exercise two shapes sharing a field name with different offsets, missing
fields, null receivers, nullable fields, mixed arrays, shape transitions,
nested indices, constructor returns, collection during RHS evaluation and
alias/COW mutation between reads. The generic miss must remain output-equal
to ordinary access under **S7.1.1v3, D3.4.5, D4.4.4v4**.

Proceed to implementation only when current profile/census evidence identifies
a hot eligible region. Retain a pilot only with a measurable paired release
win, unchanged semantics and reviewed emission cost. If guard coverage is
poor or boxing is not a material cost, close or defer this track with the
profile and the narrower next question; do not expand speculative machinery.

## 11. Verification and performance acceptance

### 11.1 Focused semantic and MIR checks

Each implementation patch needs a reproducer of the lost optimization, a
positive emission assertion and negative/fallback semantic cases. Add the
expected `.txt` beside every new `.ls`; use `.mir-check` for finalized
instruction shapes. Assertions should name the intended function and avoid
unstable raw addresses or blanket bans on legitimate cold helpers.

Build test binaries before using them. Representative commands are:

```sh
make build-test
./test/test_mir_emission_gtest.exe --gtest_filter='*tune32*'
./test/test_mir_gc_stress_gtest.exe --gtest_filter='*tune32*'
./test/test_mir_ratchet_gtest.exe
python3 utils/check_gc_effects.py
python3 utils/check_gc_root_hazards.py
```

Include changed existing fixtures explicitly in focused filters; naming only
`tune32` does not run `tune31_inferred_string_char_pair` or other reused cases.
Run relevant Lambda interpreter/JIT/AUTO semantic comparisons separately.
The MIR stress runner's `--mir-interp` mode executes emitted MIR and is not
a substitute for testing the Lambda interpreter tier.

Use the existing GC stress modes: allocation-every-time plus poisoned freed
memory, deterministic randomized collection plus poison, and MIR-interpreter
stress. A wider `NO_GC` claim requires the transitive effects audit; never
classify a helper as non-collecting merely because the common case allocates
nothing. A sanitizer run is required for new raw-memory access or ownership
logic; keep sanitizer results separate from release timings.

### 11.2 Release A/B protocol

1. Build new candidates with `make release` and archive them. After test
   builds, `make build-release-compile` may restore an unchanged release;
   require the same exact binary hash before reusing its measurements.
   Verify the release/profile gate and exact hash for both binaries.
2. Use `test/benchmark/run_paired_benchmarks.py` with unchanged source and
   `--tier jit`. Reuse the existing runner; do not write a timing framework.
   Run timing without MIR dumps, forced GC, execution counters or sanitizers.
3. Use at least nine alternating pairs per primary pilot for iteration;
   confirm retained pilot wins with 21 pairs. Preserve raw samples, output
   hashes and the runner's paired bootstrap uncertainty. A win must survive
   the uncertainty check and have a corresponding emitted/executed mechanism.
4. Run a five-pair screen across all 63 canonical rows, both untyped and
   typed, against the fixed control. Confirm every >5% slowdown with at
   least 31 pairs and an A/A timing control when noise is plausible.
   Sub-millisecond rows are not automatically waived: retain absolute
   deltas, uncertainty and any separate amplified diagnostic evidence.
5. Reject a confirmed regression attributable to the patch unless explicitly
   accepted as a documented tradeoff. Do not average a bad row away. Failures,
   timeouts, output mismatches and missing rows remain visible, not silently
   removed from the geometric mean.
6. Re-run original / partial / typed sources together on the candidate to
   show which gaps closed. These are diagnostic companions, not replacements
   for same-source engine A/B.

Example after the named control/candidate archives have been created:

```sh
python3 test/benchmark/run_paired_benchmarks.py \
  --control temp/tune32/control-release \
  --candidate temp/tune32/tco-release \
  --suite kostya,text,larceny --bench primes,levenshtein,fast_diff,divrec \
  --variants both --tier jit --pairs 21 \
  --output temp/tune32/core-pilots.json
```

Report geometric mean of per-row candidate/control times and sum of row
medians separately. They answer different questions; the sum depends heavily
on the text workload mix. A **5% reduction in the 63-row untyped geomean** is
the proposed round objective, not a forecast. Record a miss as a miss even
when individual useful patches are retained. Do not publish a new named
ResultN or backpatch prior results as part of Tune32 without a separate
snapshot request.

### 11.3 Full closeout gates

After the final retained changes, run:

```sh
make test-lambda-baseline
make test262-baseline
git diff --check
```

These are required in addition to focused tests and release performance
evidence. The Lambda baseline includes the broad emission/GC coverage; do
not substitute selected fixtures for it. Shared compiler changes can affect
LambdaJS, so retain the Test262 gate and investigate runtime failures rather
than modifying the harness to mask them. Run additional affected subsystem
checks when the actual patch crosses those boundaries.

Any failure must have an exact test name, current log, reproduction and
disposition. Pre-existing failures remain separately identified and do not
become passes. Final timing must identify the exact candidate source and
binary tested; a later unmeasured edit invalidates that closeout claim.

## 12. Completion record and deferred work

Use this checklist as work progresses; the completion evidence follows below:

- [x] T32-0: implementation-start release, source manifests and pilot replay.
- [x] T32-1: bool producer/store fix, semantic and GC coverage, paired win.
- [x] T32-2: inferred-string fusion, updated existing emission contract,
  generic/Unicode/absence cases, paired wins and typed controls.
- [x] T32-3: Levenshtein residual decomposition, retained root-cause fixes
  or explicit measured deferrals, cumulative comparison with fully typed.
- [x] T32-4: proved inferred tail loop, boxed fallback and recursion tests,
  isolated paired release evidence.
- [x] T32-5: current record profile; bounded implementation with a win or
  an explicit evidence-based rejection/defer decision.
- [x] T32-6: final fixed-control 63-row typed/untyped screen, outlier closure,
  Lambda and Test262 baselines, MIR ratchet, GC/sanitizer checks as applicable.
- [x] Durable artifacts, commands, hashes, source-difference notes and final
  status for each performance objective.

Completion requires explicit disposition of every track and no unresolved
correctness regression. “Completed implementation” and “met round performance
objective” are separate claims. Report the measured result for both; do not
silently lower a missed target or relabel a source rewrite an engine gain.

Deferred unless new measurements justify them: general aggregate destination
analysis, arbitrary interprocedural mutation summaries, broad loop-relation
solving, changes to the allocator/collector, and benchmark algorithm rewrites.
Existing typed implementations in these areas are useful references, but
their existence alone is not evidence that they are the next untyped bottleneck.

## 13. Implementation evidence — 2026-10-02

The implementation-start control is `temp/tune32/control-release`, SHA-256
`dd3d0319e59cfd9b8c7515ab7ada1048028f8eea9468c98c137933c915580a1b`.
It was built from the source commit above before compiler edits. Its Lambda
baseline passed **6,149/6,149**. The binary remains separate from the R50
diagnostic anchor. Durable evidence is in
[`temp/tune32/`](../../temp/tune32/).

### 13.1 Retained mechanisms

| Track | Actual change | Mechanism evidence |
|---|---|---|
| T32-1 | Classify boolean fill as packed ArrayNum; install a guarded element witness without an array certificate | Native `u8` stores, with carrier/bounds/COW fallbacks; empty and mixed arrays keep source semantics (**D3.3.3v3**) |
| T32-2 | Admit inferred string carriers to shared character fusion; nullable nested string elements predict a guarded entry while the honest caller join stays ANY | Fused byte/Unicode comparison in raw bodies; complete boxed body handles absent rows and other keys (**D8.3.1v2–D8.3.3**, **S8.2.4v3**) |
| T32-3 | Treat a closed matching scalar edge to an already native consumer as useful forwarding; permit existing leaf inlining only after complete actual argument admission | Levenshtein `min3` stops forwarding to `_min2_b`; inferred leaf parameters use resolved native facts, with no invented source annotation (**D8.2.5v3**, **D8.3.2**) |
| T32-4 | Enable fixed scalar inferred raw tail loops, proving every argument again; keep boxed slow bodies recursive | Divrec raw backedges have no self-call; argument permutation, float lanes and a 20,000-step countdown pass (**D8.3.2**) |

The residual Levenshtein experiment identifies helper declarations, rather
than row or counter declarations, as the limiting factor. On the control,
the string-only variant measured 8.747 ms; arrays-only 8.574 ms; scalars-only
8.735 ms; helper parameters/results 3.051 ms; other declarations 8.571 ms;
fully typed 3.161 ms. See
[`control-diagnostics.json`](../../temp/tune32/control-diagnostics.json).
No separate array-swap change was justified or retained.

The recursion regression also found and fixed a pre-existing return-proof
defect: a shape-changing self call reached the boxed body, but native return
analysis decoded its string result as integer zero. A fixed scalar recursive
return now needs matching argument-lane proof; otherwise its result stays
Item-valued. The new fixture returns `"done"` identically on JIT, interpreter
and AUTO, with all three GC stress modes (**D3.3.1v2**, **D8.3.2**).

**Evaluation-order clarification:** **S7.7.1** does not prescribe strict
left-to-right expression evaluation. Native indexing already evaluates keys
before receivers; the interpreter evaluates receivers first. Fusion preserves
the native control behavior and evaluates each producer once. The portable
fixture asserts four effects and the comparison result, without imposing a
new cross-tier ordering rule.

### 13.2 Fixed-control pilot confirmation

The 21-pair same-source release confirmation in
[`final-pilots.json`](../../temp/tune32/final-pilots.json) gives these
candidate/control time ratios. Every untyped pilot wins **21/21** pairs and
all normalized outputs match. Exact medians and bootstrap uncertainty are in
the artifact; no annotation or timer placement changed.

| Untyped pilot | Candidate / control | Speedup | Initial objective |
|---|---:|---:|---|
| Primes | 0.1629 | 6.14x | <=0.25: met |
| Fast Diff | 0.2149 | 4.65x | <=0.30: met |
| Levenshtein | 0.2507 | 3.99x | Cumulative <=0.40: met |
| Divrec | 0.2044 | 4.89x | <=0.40: met |

Incremental nine-pair artifacts preserve separate attribution: boolean
Primes 0.1609; string Fast Diff 0.2152 and Levenshtein 0.6856; scalar
Levenshtein 0.3697; TCO Divrec 0.2040. Typed companions are in every artifact.
The full population confirmation follows in 13.4; correctness gates are
recorded in 13.6.

### 13.3 T32-5 disposition

The current control's executed census records 1,524,368 generic member reads,
1,130,616 generic index calls, 890,304 multiplies, 765,728 adds and 513,808
subtracts. It also records 61,989,144 root reloads in `triangle_intersect`.
These are instrumented execution counts, not timing samples. See
[`control-raytrace3d-profile.tsv`](../../temp/tune32/control-raytrace3d-profile.tsv)
and the separate COW census.

All six observed guarded field sites hit, but **none is in
`triangle_intersect`**. Its constructor-derived field candidates still have
boxed scalar storage, and the hot expression chain also has dynamic axis
indices. Exact receiver shape alone establishes neither scalar tags nor
index extent. A guard for one boxed read would leave the downstream helpers
and root traffic intact; cloning the whole function would exceed this track's
bounded-consumer scope. **T32-5 is deferred after investigation; no record
optimization is claimed.** The next bounded question is propagation of the
constructor's complete field facts through scene-array reads, followed by a
measured scalar consumer region (**D3.3.3v3**, **D8.4.1v2**, **D5.3.4**).

### 13.4 Full population and outlier closure

The fixed-control five-pair screen completed **126/126** rows: all 63
canonical untyped benchmarks and all 63 typed companions. Every pair is
valid and every normalized output matches. The runner verifies stable
binaries, source corpus and build manifest. See
[`final-screen.json`](../../temp/tune32/final-screen.json) and its
[`summary`](../../temp/tune32/final-screen-summary.json).

| Population | Geomean candidate/control | Sum of control medians | Sum of candidate medians | Sum ratio |
|---|---:|---:|---:|---:|
| Untyped, 63 rows | 0.89138 | 13,313.730 ms | 13,046.614 ms | 0.97994 |
| Typed, 63 rows | 1.00000 | 6,376.088 ms | 6,392.526 ms | 1.00258 |

The proposed 5% untyped geomean reduction is **met: 10.86%**. Summed medians
fall 2.01%; they weight the longer workloads more heavily. There are **no
rows above the 5% slowdown threshold**, so no 31-pair slowdown confirmation
or A/A noise adjudication is triggered. The slowest screen ratios are
1.02324 untyped (`deriv`) and 1.04013 typed (`quicksort`); they remain valid
screen observations, not confirmed regressions or speedup claims.

### 13.5 Candidate original / partial / typed comparison

The candidate replays the same diagnostic sources in 12 balanced rounds per
group, using the existing runner's timed execution parser. See
[`candidate-diagnostics.json`](../../temp/tune32/candidate-diagnostics.json).
Within each group, every result has the same normalized output hash.

| Benchmark | Original untyped | Minimal annotation | Fully typed | Original / typed |
|---|---:|---:|---:|---:|
| Primes | 2.840 ms | 2.191 ms, `flags: bool[]` | 2.193 ms | 1.30 |
| Fast Diff | 52.771 ms | 52.668 ms, two string parameters | 47.073 ms | 1.12 |
| Levenshtein | 2.829 ms | 2.831 ms, two string parameters | 2.864 ms | 0.99 |
| Divrec | 1.251 ms | — | 1.239 ms | 1.01 |

The string declaration advantage has closed for Levenshtein and Fast Diff.
Primes retains a guarded inferred-array path, while a declared `bool[]`
can reuse its checked contract; this remaining gap is intentional proof
cost (**D3.3.3v3**), not a reason to fabricate a certificate. Divrec's
scalar inferred loop now reaches its typed companion's performance.
The diagnostic families also show that additional annotations are not
universally beneficial: the Levenshtein array-only family takes 5.272 ms
on the candidate, while the unchanged canonical source takes 2.829 ms.
That family is an isolation experiment, not a retained benchmark rewrite.

### 13.6 Correctness and memory gates

The final `make test-lambda-baseline` passed **6,159/6,159**: 2,104 input
tests and 4,055 Lambda/runtime tests. It includes **231/231** Lambda MIR
emission fixtures, **282/282** MIR GC stress fixtures, **32/32** LambdaJS
emission fixtures and **20/20** ratchet probes. The focused seven-fixture
emission and GC gates each passed 7/7 before the full baseline.
See [`final-lambda-baseline.log`](../../temp/tune32/final-lambda-baseline.log)
and the individual `*-final.json` reports in the evidence directory.

The committed goldens also pass **42/42** explicit executions: seven
fixtures in JIT, Lambda interpreter, AUTO, allocation-every-time with poison,
seeded randomized GC with poison, and MIR interpreter with GC/poison.
The rebuilt `lambda-debug-asan.exe` passes the same **42/42**, with no
sanitizer failure. ASan instruments the native host; JIT-generated raw
instructions additionally rely on the forced-GC and poison oracles.
See [`final-focused.json`](../../temp/tune32/final-focused.json)
and [`final-asan.json`](../../temp/tune32/final-asan.json).

`check_gc_effects.py` verifies 93 NO_GC imports and 288 call-graph nodes.
`check_gc_root_hazards.py` checks 16,649 migrated native functions and finds
no automatic-local roots or transient registration guards. No helper effects
classification or rooting convention was weakened (**D5.3.1–D5.3.4**).

An extra ratchet run on the **release/default budget profile** has three
pre-existing failures: `js_hoisted_modvar_write_through` (`js_main` 87 vs
budget 76), `lambda_cow_nested_store` (module 173 vs 166), and
`js_tune6_exact_collection` (`js_main` 12,627 vs 12,573). The exact control
and candidate releases emit the same counts for each; reviewed MIR diffs
contain only relocated pointer constants. The required baseline's
`darwin-debug-v3` profiles pass 20/20. Budgets were not raised or masked.
See [`release-ratchet-diagnosis.json`](../../temp/tune32/release-ratchet-diagnosis.json)
and its paired MIR diffs (**D8.6.1**).

The final standard `make test262-baseline` passed **40,261/40,261 fully**,
with zero failures, regressions, retries or slow classifications. The first
attempt had 40,259 fully passing and two retry-only cases:
`language_identifiers_start_unicode_10_0_0_js` and
`language_identifiers_start_unicode_10_0_0_escaped_js`. Both originally
returned successful results but exceeded the three-second timing classifier;
there was no assertion failure, crash or timeout. Both releases pass the
captured 600-source AST batch in isolation, with these cases taking about
1.7–1.8 seconds. The unchanged standard full rerun clears the timing gate.
The first result remains visible; no runtime, harness, threshold, baseline
or job-count change was made between attempts. See
[`test262-timing-diagnosis.json`](../../temp/tune32/test262-timing-diagnosis.json),
the first full log, and
[`final-test262-baseline.log`](../../temp/tune32/final-test262-baseline.log).

The 20,000-step tail oracle additionally passes JIT, forced-GC/poison and
MIR-interpreter GC/poison execution. Its exact source, expected output and
observed results are preserved in
[`deep-tail.json`](../../temp/tune32/deep-tail.json).

### 13.7 Final provenance and review boundary

The measured candidate is `temp/tune32/tco-release`, SHA-256
`74fec4ed49809bc27aa32a608182ffdd46cf4bd33030a13f6e10fd6ceaa51ec2`.
Its compiler-source SHA-256 is
`140729727b2c516d68728b290a553193bc0ba725cb3c06fee80fb60bb639c555`.
No production code changed after this archive or its paired measurements.
The final host is restored with `make build-release-compile`; byte identity
with the measured archive is required and recorded in the closeout report.

Production changes are confined to `lambda/runtime/transpile-mir.cpp`.
Five new MIR fixtures each have a `.txt` golden and `.mir-check` sidecar;
the existing Tune31 inferred-string emission boundary is updated explicitly.
Final character coverage includes captured snapshots and failure before
argument admission, as well as nullable, borrowed and rebound receivers.
The evidence directory preserves commands, raw samples, source hashes,
stage patches, executed censuses and gate logs. See its
[`README`](../../temp/tune32/README.md) and
[`closeout.json`](../../temp/tune32/closeout.json).

Concurrent Julia benchmark infrastructure changes are outside this work and
were preserved. No named ResultN was published or backpatched. Deferred
record propagation and the remaining declared-array proof advantage remain
explicit future work, not claimed Tune32 optimizations.

## 14. Follow-up analysis and source experiments — 2026-10-02

This review uses committed Tune32 source `261633ea2` and verifies that both the
current compiler and release binary have the exact hashes recorded in §13.7.
The engine is unchanged. New evidence, executable source experiments and replay
commands are in [`review/`](../../temp/tune32/review/README.md).

### 14.1 What improved, and what still dominates

Tune32 breaks the plateau in its selected kernels: the four confirmed wins are
3.99–6.14x, and Levenshtein/Divrec now approximately match their typed companions.
The 63-row time geomean falls 10.86%, but the sum of medians falls only 2.01%.
The four pilot savings account for 220.602 ms of the 267.116 ms net reduction.
Further shaving these already-small kernels will have little effect on the
longest workloads. These weights describe this benchmark population, not a
production application's workload distribution.

The original Tune32 candidate screen gives:

| Benchmark | Untyped | Typed | Untyped / typed | Share of untyped summed medians |
|---|---:|---:|---:|---:|
| log_pipeline | 4673.370 ms | 1187.100 ms | 3.94 | 35.82% |
| three_way_merge | 2789.170 ms | 1568.610 ms | 1.78 | 21.38% |
| text_search | 1489.300 ms | 1432.940 ms | 1.04 | 11.42% |
| prettier_ast | 965.687 ms | 330.731 ms | 2.92 | 7.40% |
| cd | 589.765 ms | 152.884 ms | 3.86 | 4.52% |
| richards | 329.879 ms | 76.508 ms | 4.31 | 2.53% |
| gcbench | 190.680 ms | 83.886 ms | 2.27 | 1.46% |
| raytrace3d | 87.997 ms | 13.024 ms | 6.76 | 0.67% |

The first four consume 76.02% of the untyped sum. `three_way_merge` is the best
large, algorithm-matched type-gap pilot. `text_search` is already close to typed;
its further gains require improvements common to both lanes. Typed/untyped
gaps alone cannot rank compiler changes where the source representation differs.

### 14.2 Annotation-free ports of the typed algorithms

At the user's request, the review removes **all** parameter, result and local
annotations and type declarations from the alternate representations. Canonical
sources are replaced only after a repeatable win with matching output. This is
source tuning; it does not revise Tune32's fixed-source engine measurements.

| Source experiment | Original untyped | New untyped | New / original | Decision |
|---|---:|---:|---:|---|
| Log span scanner and scalar aggregates | 4582.720 ms | 6648.840 ms | 1.4509 | Keep original; 0/9 wins |
| Formatter integer tags, uniform records and capped traversal | 971.303 ms | 985.943 ms | 1.0151 | Keep original; 2/9 wins |
| Formatter capped traversal alone | 953.962 ms | 966.091 ms | 1.0127 | Keep original; 1/9 wins |
| Fast Diff character-code arrays | 52.588 ms | 382.030 ms | 7.2646 | Keep original; 0/9 wins |
| k-nucleotide compact counters and literal scans | 4.867 ms | 1.706 ms | 0.3505 | Confirm, then adopt |
| Collision detection flattened motion records | 587.918 ms | 589.819 ms | 1.0032 | Keep original; 2/9 wins |

The k-nucleotide 21-pair confirmation measures **4.846 → 1.648 ms (2.94x)**,
wins **21/21**, and has a one-sided 95% paired ratio upper bound of 0.3432.
`test/benchmark/beng/knucleotide.ls` now contains that exact measured source,
without annotations. Its input, frequency/count outputs and existing golden
are unchanged. The timed region still includes reading the input; the new source
uses the typed companion's `io.read` path. One/two-mer maps become compact
counters and the five longer requested patterns use direct overlapping scans.
This is the supplied A/C/G/T workload, not a claim about arbitrary alphabets.
Full output matches in JIT, Lambda interpreter, AUTO and three GC/poison modes.

The rejected Fast Diff experiment converts strings to integer arrays before the
timer, matching the typed companion's preprocessing boundary. Its slowdown
persists despite excluding that conversion cost. Existing canonical "untyped"
benchmarks can contain selected declarations; these new candidates contain none.
The small formatter/CD differences are not confirmed regressions, but they offer
no repeatable gain supporting a replacement. Every pair matches the complete
normalized output, including formatted text and frequency tables.

Separate nine-pair new-untyped/typed comparisons give log **6645.610 / 1169.770
ms**, formatter **984.192 / 328.862 ms**, Fast Diff **383.102 / 46.987 ms**,
k-nucleotide **1.714 / 0.427 ms**, and CD **588.836 / 154.925 ms**. Thus adopting
the representation alone closes part of the k-nucleotide gap; the other tested
representations depend heavily on the typed execution paths.

### 14.3 Recommended next compiler work

**Priority 1 — preserve string collection facts through complete producer and
consumer chains.** Use `three_way_merge` as the unchanged canonical pilot and
the annotation-free log scanner as a diagnostic. The current append-builder
analysis (`mir_append_builder_value_type`) accepts int/float producers; the
call-site element helper (`mir_array_type_element_type`) recognizes numeric/bool
arrays. String handling cannot be added by routing it through ArrayNum: strings
have distinct boxed-slot and pointer-slot carriers. The current
`emit_generic_pointer_array_load` also falls back to `fn_index` without a full
array contract. This leaves opportunities at `builder -> return -> parameter ->
index -> string consumer`, even though the typed emitter already has suitable
checked loads, string comparisons, ASCII scans and unique open-array push paths.

Extend the shared producer/result analysis to preserve string-element facts and
consume them through guarded boxed-slot reads where no certificate exists.
Reuse `SysFuncInfo`'s existing `SYS_RESULT_TEXT_SPLIT` relation; it is already
implemented and needs to participate in the resolved argument/result chain,
not be duplicated in another special-case table. Reuse the existing string
push and comparison lowerings once actual carrier/bounds/tag proof is available.
Inference remains binding-local, indexed reads retain null, and no string-array
certificate may be invented (**D3.3.3v3–D3.3.5**, **D8.2.5v3**, **D8.3.2**).

The executed three-way-merge census drops from 334.6 million helper calls and
2.918 billion root reloads in untyped to 55.8 million and 606.2 million in typed.
`word_at` executes 30.855 million times. Its existing leaf-inlining predicate
accepts int/float/bool, not string results; a subsequent bounded extension for
read-only string selection is worth testing after element proofs are available.
Do not increase the generic inlining budget to compensate for missing proofs.

The new untyped log scanner executes **491,515,200 `fn_index` calls in
`process_logs`**, plus 23,074,380 in `decimal_span`; the typed companion executes
none. This is a concrete failure to reach the typed string-access paths. Retest
the scanner for canonical adoption after fixing that chain.

Seven rotating/reversing diagnostic rounds further localize the merge gap:
original 2774.80 ms; `word_at` declarations only 2561.91; string consumer only
2403.61; merge-array parameters only 3160.04; builder returns only 2784.58;
combined consumer declarations 2258.63; full typed 1542.24. Annotation fragments
interact, so merely adding an array type is not an optimization strategy. These
are diagnostic source changes, not compiler gains or proposed benchmark edits.

**Priority 2 — extend constructor-derived record facts through reads and small
scalar consumers.** Start with read-only `gcbench` and `raytrace3d` before mutable
Richards/CD paths. The constructor fact must include field storage and
nullability, not just a layout candidate. Carry it across return/parameter and
array-read edges; use exact immutable guards and preserve the full generic miss
path (**D8.3.1v2–D8.3.3**, **D8.4.1v2**). A map shape alone does not authorize an
unchecked scalar decode after an in-place retag.

There are two reusable typed mechanisms: direct constructor allocation/stores
and field-to-native-arithmetic lowering. `gcbench` executes 3,222,190 calls each
to `map_with_region_tl` and `map_fill`, plus 6,422,538 generic member reads;
typed construction uses `heap_calloc_class` and typed reads avoid those member
helpers. Extend the common constructor/store plan only for storage classes it
can actually write; nullable/ANY `TypedItem` slots need their real layout, not
the declared-record raw-word store. No allocator or GC policy bypass is proposed.

Raytrace's `triangle_intersect` still performs 1,394,080 generic field reads,
986,432 generic index calls and 61,989,144 root reloads after Tune32. Specializing
a proven arithmetic region has more potential than speeding up one field lookup.
The annotation-free formatter similarly leaves millions of member accesses and
type checks in `flat_length`; its uniform field names alone do not establish the
typed record's representations. The existing small-record return plan requires
an explicit contract, so inferred `render_result`-style records are a later
bounded candidate for the same mechanism after complete result proof.

**Priority 3 — remove repeated array checks only when loop proofs justify it.**
Primes retains about 0.65 ms between inferred and declared arrays. This is a real
proof obligation, but not necessarily a requirement to repeat every check per
iteration. Investigate loop-entry carrier/element/bounds guards and stable owner
facts using existing loop and COW plans, preserving out-of-bounds reads, failing
writes and snapshot/borrow behavior (**S7.1.1v3**, **S7.1.3v2**, **S9.1.2–S9.1.3**,
**D3.3.3v3**). Keep its priority below the text and record pipelines.

Root-traffic reductions should follow from eliminating generic MAY_GC calls,
shortening actual live ranges and auditing exact effects. These counters are
executed events, not CPU samples or collection counts; they do not justify
removing required reloads or weakening precise ownership (**D5.3.4**).

### 14.4 Acceptance for the next tuning round

Keep source-rewrite evidence separate from engine A/B. For compiler changes,
use exact release binaries and unchanged source hashes, 21 alternating pilot
pairs and typed companions, then the 63+63 population screen. Require emitted
mechanism evidence as well as elapsed-time gains. Include mixed/empty/nullable
arrays, widening and rebinding, Unicode, record retags, COW snapshots/borrow
writeback, escaping calls, boxed misses and data-zone compaction/poison cases
(objects themselves do not move: **D4.3.1**). Run the
Lambda and Test262 baselines for engine changes. This review itself changes no
engine code; its adopted benchmark receives full-output tier/GC validation.

## 15. Phase II — String collections, record pipelines and loop proofs

### 15.1 Objective, starting point and scope

**Status: planned.** Phase II targets the remaining costs in unchanged untyped
programs, starting with the largest text workloads. It extends the existing
analysis, MIR value and guarded-lowering machinery. It does not add a new
execution tier, adaptive optimizer, container representation or GC policy.

Phase I improved four pilots substantially, but reduced the sum of the 63
untyped medians by only 2.01%. `log_pipeline`, `three_way_merge`, `text_search`
and `prettier_ast` account for 76.02% of that sum. These are benchmark weights,
not a claim about production workloads. `text_search` is already close to its
typed companion; the first investment goes into the larger remaining gaps in
string collections and records. Further tuning of the four Phase I pilots is
secondary unless a shared fix reaches them naturally.

The review's engine anchor is release SHA-256
`74fec4ed49809bc27aa32a608182ffdd46cf4bd33030a13f6e10fd6ceaa51ec2`, at source
anchor `261633ea2`. The canonical untyped `knucleotide` has already adopted the
annotation-free compact-counter/direct-scan source: 4.846 → 1.648 ms in its
21-pair source comparison. This adoption belongs in the Phase II baseline;
it must not be counted again as a Phase II engine improvement.

The other six-experiment outcomes remain as recorded in §14: the log scanner,
both formatter alternatives, character-code Fast Diff and flattened CD were
not adopted. Keep them as diagnostic inputs. Improving a rejected candidate
relative to its own baseline does not establish that it beats the canonical
source, nor that the engine improved the canonical benchmark.

The first implementation step freezes a fresh control binary, source manifest,
inputs and output oracles. Use `temp/tune32-phase2/` for working artifacts and
`temp/tune32/phase2/` for retained local evidence. These are planned artifact
locations. Never overwrite the Phase I or review evidence.

### 15.2 Work packages and dependency order

| ID | Work package | Dependencies | Primary evidence / exit artifact |
|---|---|---|---|
| T32-II-0 | Freeze control; inventory lost facts and emitted costs | None | Release/source manifest, A/A noise check, proof-loss map |
| T32-II-1 | Preserve string producer and result facts | II-0 | Closed builder and `split` facts survive resolved producer chains |
| T32-II-2 | Lower guarded string collection reads and consumers | II-1 | Fewer executed generic index/string helpers in unchanged merge/log workloads |
| T32-II-3 | Conditional bounded string-selection leaf inlining | II-2 and a remaining measured call cost | Small inline plan with nullable fallback and measured incremental win |
| T32-II-4 | Reuse direct record construction for inferred literals | II-0; shared proof model below | Descriptor-correct construction, starting with `gcbench` |
| T32-II-5 | Carry record facts into guarded reads and scalar consumers | II-4; existing field plans | Fewer generic field/arithmetic calls in `gcbench` and `raytrace3d` |
| T32-II-6 | Reuse array guards across proven loop regions | II-0; after the two higher-priority tracks | Primes read-loop proof, then separately justified store-loop proof |
| T32-II-7 | Retest annotation-free source alternatives | Relevant engine track accepted | Separate source-pair adoption report; switch only confirmed winners |
| T32-II-8 | Full correctness, population and provenance closeout | All retained changes | Final gates, fixed-population A/B, typed comparison and dispositions |

Implement and measure II-1/II-2 first. II-4/II-5 are the second priority; they
reopen the narrower constructor/read-only opportunities left deferred by
Phase I T32-5. II-6 is third. II-3 and inferred small-record return expansion
are conditional extensions, not prerequisites for the initial string/record
slices. Do not combine all tracks into one unmeasured compiler patch.

Each core track needs a working, semantically validated slice or an explicit
record of the remaining blocker. An investigation-only deferral does not make
the Phase II core implementation complete. Conditional extensions may close
without implementation when their stated profitability gate is not met.

### 15.3 Shared proof model and analysis ownership

Keep these facts distinct throughout analysis and lowering:

| Fact | What it authorizes | What it does not authorize |
|---|---|---|
| Candidate element kind or map shape | Select an immutable guard/plan worth emitting | Unchecked load, scalar decode or raw function entry |
| Complete inferred semantic type | Describe possible values, including null/error branches | Invent a declared contract or attach a heap certificate |
| Actual physical carrier and slot layout | Choose the correct addressing and load/store width | Assume every loaded `Item` has a particular scalar tag |
| Current element tag / field storage proof | Decode that value into its matching `MirValue` representation | Remove independent bounds, presence or lifetime checks |
| Dominating bounds and non-null proof | Narrow a particular read on the proven path | Narrow another binding, a changed array, or the public result type |
| Exclusive owner and mutation stability | Reuse a store path while its ownership remains valid | Ignore COW detachment, borrowed-home publication or intervening aliases |
| Rooted owner / live value | Keep the owner/value alive at a safepoint | Keep a cached data-zone pointer valid across compaction |

Use the existing owners: `AstNode.type` for effective semantic types;
`FnAnalysis`, `FnParamTypeInfo` and the existing variant/binding analysis for
function and binding summaries; `MirFlowFact`, `MirFlowIndexBound` and scoped
lowering plans for dominating execution facts. Type-only erasable properties
follow the lazy `Type` side-record rule. Do not add a competing ID-keyed
per-node type table or mutate the shared `TYPE_ANY` singletons
(**D8.2.5v3**).

An inferred string-element fact is tied to the analyzed binding. Across a
return, assignment or call boundary, establish the receiving binding's fact
from the complete producer/edge proof; do not smuggle it through a container
certificate. Joins retain only facts true on every reachable incoming path.
Use resolved binding identity, not identifier spelling. Rebinding, incompatible
writes, escaping aliases and unknown effects kill the relevant local facts
(**D3.3.1v2–D3.3.4**).

Extend the existing inference worklist/pass schedule and its invalidation epoch.
Compute summaries once per relevant analysis revision, using the indexed
bindings/uses/calls. On recursion, ambiguity or an existing analysis cap, widen
to the honest open result and use generic lowering. Do not repeatedly walk a
function body for every index/member emission or add a private pass schedule.

Function entry specialization remains subject to complete, bounded, immutable
exact keys and the existing `_b` fallback. Operation-local guards do not create
a third path into a raw callee. No partial per-parameter raw specialization,
whole-array re-admission on every read, inline cache or feedback-selected body
is introduced (**D8.3.1v2–D8.3.3**, **D8.4.1v2**). Any call-entry guard hoisting
must separately satisfy **D8.3.4v3**; synchronous `pn` is not eligible for that
hoisting merely because a local loop guard is legal.

Use `MirValue` and `em_require_rep()` for carrier conversion. Root insertion and
final-store ownership stay with the emitter (**D2.4.1–D2.4.3**, **D8.2.6**,
**D5.3.4**). Object-zone addresses do not move, but array items/map data may
move during data-zone compaction. Reload such data pointers from a live owner
after collecting calls or COW replacement (**D4.3.1**, **D4.4.4v4**).

### 15.4 T32-II-0 — Establish the control and proof-loss inventory

1. Record the current commit, dirty patch, build configuration, compiler/host,
   release binary SHA-256 and relevant source hashes. Preserve unrelated work.
   Build with `make release` and archive the exact control before changing
   engine code. The review binary is an evidence anchor; use it as the new
   control only after verifying its build and engine-source provenance.
2. Freeze the authoritative Lambda benchmark manifest, currently 63 untyped
   and 63 typed rows, including the adopted `knucleotide`. Store source/input
   hashes, repetition counts, timed regions and complete normalized-output
   oracles. Include the `.txt` and any post-timing oracle used by the runner.
   Archive the actual source/oracle files and required dependencies, preserving
   import and input-path resolution; a hash alone cannot replay a later-edited
   source. Verify the archived copies against canonical stdout, and point the
   frozen engine manifest at those stable copies.
3. Run an A/A check on the large text pilots and small Primes pilot with the
   same release on both sides. Record variability before interpreting small
   improvements. Run correctness preflight for every selected source pair.
4. For `three_way_merge`, trace `build_base` / `make_variant` / `split` →
   returned string collection → `merge_lines` → `word_at` → string consumer.
   Record where semantic element type, actual carrier, bounds and non-null
   proof are separately lost. Repeat for canonical `log_pipeline` and the
   rejected scanner; the scanner is a useful diagnostic, not the primary row.
5. For `gcbench` and `raytrace3d`, trace literal constructor → returned/array
   value → parameter → field read → arithmetic. Record actual slot descriptors,
   null branches and escaped/mutated bindings. For Primes, distinguish the
   final read-only count loop from mutating sieve loops.
6. Capture finalized MIR and separate executed-helper/root/COW censuses for
   control sources. The review counts identify candidates, but are not CPU
   samples: 334.6 million merge helper calls, 514.6 million scanner index calls
   and millions of record helper calls do not individually prove time share.
   If helper elimination fails to improve time, take a CPU profile before
   widening the optimization.

Deliver `control-manifest.json`, `proof-inventory.md` and the pilot MIR/profile
artifacts. Preserve both the existing source alternatives and typed references
with their hashes. All later engine comparisons use a fixed source on both
sides; annotation probes remain diagnostic only.

### 15.5 T32-II-1 — String producer and result analysis

**Entry points:** `mir_append_builder_value_type`, `MirAppendBuilderScan`,
`mir_append_builder_scan_node`, `mir_append_builder_return_element_type`,
`mir_array_type_element_type`, `mir_callsite_arg_elem_type_at` and existing
function-result inference in `lambda/runtime/transpile-mir.cpp`;
`sys_func_success_result_type` in `lambda/runtime/build_ast.cpp` and the
`SysFuncInfo` result relations in `sys_func_registry.h/.c`.

The current append-builder path recognizes int/float producers. The element
helper also recognizes bool, but many downstream users interpret its result
as permission to choose an `ArrayNum` lane. Adding `STRING` to that whitelist
alone would conflate semantics with storage.

Implement in this order:

1. Audit every caller of the element helper. Separate the semantic element
   query from numeric-lane eligibility, reusing existing type/carrier helpers.
   Keep numeric/bool behavior unchanged. A string collection can be a boxed
   `Array` or an explicitly admitted pointer-slot carrier; inference alone
   must never select the latter.
2. Extend the closed append-builder summary to string producers. Initially
   retain the current strict eligibility: a local empty builder, recognized
   homogeneous pushes, direct returned binding, no reassignment, index store,
   alias escape, capture or unknown use. Join every reachable pushed value
   and every return. Empty, nullable, mixed and early-return cases must keep
   their full types; inability to prove homogeneity yields an open array.
3. Reuse `SYS_RESULT_TEXT_SPLIT` to recover nested string results when the
   complete resolved source argument justifies it. Its registry relation
   already exists. Expose/reuse the same relation instantiation as necessary;
   do not add a duplicate `split` inference switch in MIR lowering or infer
   the result from a printed nominal signature.
4. Feed complete producer results through the existing function-result and
   call-edge analysis. Preserve the distinction between body/result inference
   and entry-shape selection. A variant-local guard fact stays local to that
   variant; it cannot globally retype an AST parameter used by other callers.
5. Exercise forwarding helpers, multiple returns, nested calls and recursive
   cycles. Only publish a string result when all accepted paths support it.
   Avoid promoting a single observed call shape to a universal contract.

The expected deliverable is a correct fact chain, not necessarily a standalone
speedup. Measure II-1 together with its first consumer in II-2. Intermediate
analysis precision must preserve accepted-program results under inference
erasure (**D3.3.1v2**, **D3.3.2v2**, **D3.3.5**).

### 15.6 T32-II-2 — Guarded string collection reads and consumers

**Entry points:** `emit_generic_array_index_value`,
`emit_generic_pointer_array_load`, `MirIndexLoadPolicy`, `MirIndexProof`,
`mir_index_proof`, `mir_flow_note_length_condition`; existing
`mir_open_push_string_source` / `mir_emit_open_string_push`, string comparison
lowering and `MirAsciiScanPlan`.

`emit_generic_pointer_array_load` currently falls back to `fn_index` without
a full array contract. Retain the declared-contract path and add a distinct
guarded boxed-slot path through the shared index planner:

1. Evaluate the receiver and key exactly once in the existing language/lowering
   order. The current index lowering may evaluate the key first; an optimization
   must not silently reorder effectful expressions or replay them on a miss
   (**S7.7.1**).
2. Resolve or guard the supported physical container kind and ordinary boxed
   slot layout. A view, multidimensional or other unsupported carrier uses the
   existing generic helper. The native pointer-slot path still requires its
   actual carrier proof and applicable explicit certificate.
3. Prove or check an integer scalar index and both bounds. Invalid reads retain
   null; other supported key kinds retain their generic indexing behavior
   (**S7.1.1v3**). Load an `Item` from a boxed slot, not a `String*`.
4. A downstream string operation may consume the loaded value natively only
   with a valid local semantic proof or an exact string-tag guard. If a tag
   check is required, check the selected element rather than scanning the
   entire array. A miss enters the existing boxed operation with the already
   evaluated operands; it does not raise a new admission error.
5. Keep the boxed source/owner alive through loads and collecting operations.
   Publish a pointer-represented string with the correct emitter-owned root
   class. The null/miss join remains nullable/boxed unless the use site's flow
   proof justifies a narrower result (**D3.3.4**, **D5.3.4**).

Extend the shared length/bounds relation to eligible generic arrays, rather
than duplicating typed-array bounds logic. Tie it to the same unchanged array
and index bindings. `word_at` currently tests only `index < len(words)`;
this does **not** prove `index >= 0`. Negative input still returns null, even
though its other branch returns `""`. Neither the helper's public result nor
an inline copy may become unconditionally non-null string.

Then connect the resulting facts to existing consumers:

- String equality/character-pair comparisons should reach the existing pointer
  or fused comparison lowering, with the current Unicode and mismatch path.
- ASCII scanning may reuse `MirAsciiScanPlan` only for its proven stable string,
  induction and effect constraints. Guard ASCII once at the supported scope;
  retain Unicode semantics and avoid rescanning at every character access.
- Closed string builders should reach the existing guarded unique open-array
  push path when its capacity, ownership and physical-layout tests hold.
  Growth/shared-owner cases keep the existing push fallback and owner update.

Do not initially relax the ASCII planner to arbitrary nested loops or unknown
calls. First show the chain works in a small unannotated reproducer, then in
unchanged `three_way_merge` and canonical `log_pipeline`. Record the remaining
miss reasons in the diagnostic scanner, including whether string facts are lost
after collection indexing or at a helper boundary.

Acceptance requires exact output/tier/GC parity and a measured pilot benefit.
MIR evidence must show the intended boxed-slot load/tag/bounds sequence, while
executed profiles show fewer hot `fn_index` and generic string-consumer calls.
The fallback helper may remain in cold MIR; banning its name from a whole
function is not a valid mechanism assertion.

### 15.7 T32-II-3 — Conditional string-selection leaf inlining

The current scalar leaf predicate accepts int/float/bool, and the body budget
is 48. `word_at` executes 30.855 million times in the review census. After
II-2, measure how much of that cost remains before extending
`mir_inline_scalar_tid`, `mir_inline_callee_ok` and the shared inline plan.

The first eligible extension is a small read-only string-selection leaf with
resolved bindings, supported scalar control and a proven/guarded collection
read. Reuse the existing body budget; do not increase it or introduce general
function cloning. Initially exclude captures, mutation, recursion, allocation
and unsupported calls. Inline only when the existing argument/entry rules are
met; operation-local guards within an inline expression are not permission to
call an unproven raw function entry.

Preserve each argument's single evaluation and each branch's null result.
Merge the full `MirValue`, retaining a root for an escaping string. Test local
name shadowing, negative and large indices, mixed elements, empty arrays and
effectful argument producers. Compare II-2 versus II-2+II-3 using exact release
binaries and the same merge source. Retain the extension only if it improves
the remaining cost without exceeding code-size or typed-regression gates.
Otherwise close this conditional package as deferred with its measurements.

### 15.8 T32-II-4 — Descriptor-correct inferred record construction

**Entry points:** `MirConstructionPlan` / `MirFieldAccessPlan` and their
planners in `lambda/runtime/mir_shape_candidates.hpp`; `transpile_map` and
`mir_map_literal_stores_direct` in `transpile-mir.cpp`; the existing public
descriptor-aware `set_field_value` in `lambda/core/lambda-data.cpp`.

Start with `gcbench`'s immutable tree constructors. The untyped census executes
3,222,190 calls each to `map_with_region_tl` and `map_fill`; the typed constructor
already uses direct allocation/stores. Build on that common construction plan:

1. Identify a fixed-key literal whose actual allocated layout is known. A
   construction descriptor can prove its own field offsets/storage even when
   the source has no nominal record annotation. Initially exclude spread,
   computed keys and unsupported dynamic layouts.
2. Have the shared plan describe each field's actual storage operation. The
   current declared-record raw-word loop rejects `ANY`/`TypedItem` slots;
   those slots cannot be enabled by merely relaxing its predicate. Respect
   packed widths and unaligned storage, including the nine-byte `TypedItem`
   representation.
3. Reuse the existing descriptor-aware store logic and allocation/GC APIs.
   If a helper must be shared, expose/refactor its implementation once. Prove
   scalar-storage ownership for floats and full-width integers before field
   publication; never copy a transient number home into an escaping map.
4. Preserve field-expression order, evaluate each value once and root all live
   values across allocation or collecting field operations. Allocate/publish
   in the existing order where observably relevant. Do not label an allocating
   constructor NO_GC just because its field writes are direct.
5. Handle `gcbench` leaf and internal-node layouts as the actual distinct
   layouts they are. A null child and a map child need not have identical slot
   storage. Do not invent one nominal `Node` contract to force a common layout.
   An unsupported field/layout keeps the existing complete construction path.

Validate small constructor cases before running the allocation-heavy pilot:
null children, nested records, nullable/ANY fields, strings, wide integers,
floats and unsupported values. Verify exact returned values under forced GC
and poison. Inspect MIR for the expected allocator and descriptor-correct
stores; measure generic construction helper reduction separately from read
improvements. This work changes lowering, not allocation policy or source
admission (**D3.3.2v2**, **D4.3.1**, **D5.3.4**).

### 15.9 T32-II-5 — Guarded record reads and scalar consumer regions

**Entry points:** `mir_expr_candidate_shape`, `mir_resolve_shape_hints`,
`mir_shape_candidate`, `mir_guarded_field_storage_admits`,
`emit_mir_direct_field_read` and the shared field plan. Later, conditionally:
`mir_record_result_contract`, `mir_record_result_plan` and `MirRecordValue`.

First carry constructor candidates through complete result/parameter edges and
read-only array selection. The current shape-candidate walk does not cover an
array index. Add only candidates supported by the analyzed producer chain;
an ambiguous or capped result remains generic. Reuse existing deterministic
bounds and shape-fixpoint handling. A candidate is a guard choice, not a proof
that every array element has that layout.

For each eligible field access, discharge these obligations separately:

1. The value is a map with the expected current immutable descriptor, and the
   field is present at the descriptor's offset/width.
2. That field's storage class permits the proposed decode. An `ANY` slot needs
   its actual `Item`/tag handling; a nullable slot retains null. A matching map
   descriptor is insufficient for mutable null/bool lanes that can retag in
   place. Reuse the exclusions in `mir_guarded_field_storage_admits` and check
   the runtime retag/rebuild rules (**D3.4.5**, **D4.3.4**).
3. Any subsequent array indexing has an independently valid carrier/index/
   extent proof. Raytrace's dynamic axis does not become in-bounds because its
   enclosing triangle has a known shape.
4. Native arithmetic preserves the operation's numeric domain, overflow and
   error behavior; a field-name match is not an integer/float contract.
5. The map owner and returned pointer values remain rooted; data pointers are
   refreshed after collecting calls and owner replacement.

Start with read-only `gcbench` traversal, then `raytrace3d`'s
`triangle_intersect`. Its 1,394,080 generic member calls, 986,432 index calls and
61,989,144 root reloads explain why one faster field lookup is insufficient:
boxed arithmetic and MAY_GC consumers remain downstream.

Extend the existing field/native-expression lowering to a bounded straight-line
consumer region. For the first slice, use a conservative plan limit of at most
four record inputs and twelve supported scalar operations, with one guarded
entry and one result join. These are proposed implementation limits, not new
language rules. Exclude loops, stores, escaping temporaries and unknown calls.
Evaluate effectful producers outside the region exactly once. Guard the facts
needed by that region, lower its successful arm using `MirValue`, and let a
miss execute the original generic operations with the saved operands.

At the join, box only where the surrounding demand requires it. Preserve all
generic error/overflow paths; do not replace arithmetic with unchecked machine
operations merely because the fast arm has native registers. If the shared
lowering already supplies a needed guard/region shape, extend it instead of
adding a second speculative emitter. Region plans must earn their MIR-size
cost through measured benefit.

After these read-only cases, use canonical and revised `prettier_ast` as a
secondary consumer. Uniform field names or integer tags do not by themselves
prove field representations. Keep mutable Richards/CD out of the first slice;
their alias, COW and writeback obligations require separate evidence.

**Conditional extension — small inferred record results.** The existing
`mir_record_result_contract` requires an explicit return contract and restricts
captures, effects, parameters and shape. Only reopen it if post-II-5 profiles
still show a material small-result allocation cost. Prove all return paths,
field storage, non-escape/materialization points and caller-home lifetime;
then reuse `MirRecordValue` and its rooted result homes. Keep the boxed entry
and existing eligibility restrictions unless independently proved. No new ABI,
invented nominal result contract or stack-home escape is part of this phase.

### 15.10 T32-II-6 — Reuse guards in inferred array loops

**Entry points:** `MirFiniteLoopPlan`, `mir_length_bound_loop_plan`,
`mir_finite_loop_plan`, `MirDenseRootFact`, `MirDenseScan`,
`mir_dense_typed_array_root`, `mir_dense_loop_scan`,
`mir_prepare_dense_loop_guard`, `MirFlowIndexBound`, and
`emit_array_num_direct_store`.

The remaining Primes gap is approximately 2.840 versus 2.193 ms in the Phase I
confirmation. Investigate repeated checks after the higher-weight text/record
work, with two separately measured slices:

1. **Read-only count loop.** Extend the existing dense-loop plan to recognize
   an inferred bool array with a runtime-checked matching physical lane. Prove
   stable owner/length, induction start/step/termination, nonnegative bounds
   and absence of overflow. Guard at the supported loop entry and reuse the
   existing loop emission with scoped facts. A mismatch runs the generic loop.
2. **Mutating sieve loop.** Add only after the read case is validated. Prove
   stored values remain bool and ownership/carrier/length are stable under the
   body's effects. Reuse the existing exclusive-owner and COW/store machinery.
   Reject growth, alias escape, unsupported calls and stores that can widen or
   replace the owner unless the shared fallback explicitly handles them.

The loop plan must distinguish reusable carrier/bounds facts from a cached data
pointer. A collecting operation requires the latter to be reloaded even if the
former remains true. A possible owner replacement or length-changing mutation
invalidates both as appropriate. Use the existing loop templates and scoped
flow state; do not clone whole functions or make an unbounded loop-version tree.

Zero-trip loops must retain zero writes and the same observable error behavior.
Do not detach COW storage, perform a failing write check or allocate a new owner
before proving that the original loop executes the corresponding operation.
An out-of-bounds read still yields null; an invalid write still fails. Shared
snapshots and `var` caller-home publication must be unchanged
(**S7.1.1v3**, **S7.1.3v2**, **S9.1.2–S9.1.3**, **D4.4.3–D4.4.4v4**).

A whole-array validation on every inner-loop entry may cost more than repeated
element guards. Measure the actual guard scope and trip counts. Retain the
slice only if the common path improves and empty/short-loop cases do not gain
unjustified work. Extend beyond Primes only where the same proof is reusable.

### 15.11 Correctness and emitted-mechanism tests

Add focused fixture families under the existing Lambda/MIR test directories,
using a `tune32_phase2_` prefix. Every new `.ls` gets its expected `.txt`; add
`.mir-check` assertions where they establish an emitted mechanism. Extend
existing fixtures when they already cover the exact boundary rather than
copying whole test families.

| Area | Positive case | Required negative / invalidation cases |
|---|---|---|
| String producers | Empty builder followed by homogeneous pushes; `split` and forwarding returns | Mixed/nullable pushes, early returns, rebind, escaped alias, captures, recursive/open callers, shadowed names |
| String indexing | Guarded boxed-slot load feeding equality and character access | Empty/OOB/negative index, non-integer selection key, mixed element, view/unsupported carrier, lost lower bound |
| Text semantics | ASCII compare/scan and proven non-null branch | Unicode/multibyte input, empty string, null branch, unequal lengths, effectful receiver/key evaluated once |
| Optional leaf inline | Small read-only selector with valid local proofs | Nullable result, effectful arguments, captures, recursion, unsupported calls, body-budget limit |
| Record constructors | Fixed-key literal with supported descriptor stores | Null/ANY and packed slots, wide scalar storage, nested allocation, multiple return layouts, unsupported dynamic keys |
| Record consumers | Exact descriptor plus independently proven field/index/arithmetic inputs | Missing field, alternate shape, null/bool retag, widened field, dynamic axis/OOB, numeric overflow, escaping or mutating call |
| Loop facts | Stable inferred bool carrier and finite induction | Zero/one trip, overflow boundary, changed length/owner, widening, nested loop scope, branch joins, unknown call |
| Ownership | Unique mutation and read-only shared access | Shared snapshot, detach, nested borrowed writeback, caller rebind, multiple live owners, GC during fallback |

Run each new unsafe-load/store/rooting boundary under JIT, the Lambda
interpreter oracle, forced GC with poison, seeded random GC with poison and
the MIR interpreter GC path supported by the existing harness. Include an
ASAN run for new raw-access boundaries. Test both optimized and guard-miss
paths; an all-happy-path benchmark is not a rooting test.

Assert finalized MIR structure without raw addresses (**D8.6.2**): actual load
width/carrier, dominating guard, preserved fallback, root class and conversion
points. Use executed profiles to distinguish a cold fallback's presence from
its hot execution. Never delete roots/reloads merely to lower a counter or
introduce another static liveness authority (**D5.3.4**, **D8.6.3**).

Run the emission-size ratchet with **zero slack**. A justified emission increase
needs an explicit, reviewable budget change with the implementation and measured
benefit; do not auto-pad budgets or mask a failed case (**D8.6.1**). Establish
the current control's gate status first, including the release/default ratchet
differences recorded in §13; distinguish inherited failures from new ones.

### 15.12 Performance targets and retention rules

The following are **initial engineering targets**, not predictions or previously
measured Phase II results. Ratios are candidate/control on identical sources;
the control is the frozen Phase II release, not a historical typed time.

| Track | Primary unchanged source | Initial target | Mechanism evidence |
|---|---|---:|---|
| String chain | `three_way_merge` | ≤0.80 time ratio | Fewer hot generic index/string calls; reduced downstream root traffic |
| String chain | Canonical `log_pipeline` | ≤0.90 | Proven text values reach existing string operations |
| Record construction/reads | `gcbench` | ≤0.80 | Fewer generic construction and member helpers, correct descriptor stores |
| Record/scalar regions | `raytrace3d` | ≤0.70 | Fewer member/index/boxed-arithmetic calls in `triangle_intersect` |
| Shared record consumers | Canonical `prettier_ast` | ≤0.90 stretch target | Field/consumer facts survive joins without broad code duplication |
| Loop guard reuse | `primes` | ≤0.90 | Carrier/bounds checks move to a valid loop scope |

The revised scanner is an additional diagnostic target: aim to halve its
same-source runtime and sharply reduce its 514.6 million generic index calls.
It still needs a separate comparison against canonical `log_pipeline` before
adoption. Revised formatter/Fast Diff/CD outcomes are likewise secondary;
their faster typed companions are references, not proof of an untyped gain.

Phase-wide objectives are at least a 5% reduction in both the fixed untyped
time-ratio geomean and the sum of per-row medians. Report both because the
Phase I pilot geomean hid the small reduction in total benchmark time. Also
report per-row results and typed behavior; neither aggregate can hide a
confirmed material regression.

For every retained independently measurable slice:

1. Screen with nine alternating release A/B pairs after correctness preflight.
   Confirm the intended pilot with at least 21 pairs and the runner's paired
   bootstrap interval. Require a repeatable improvement, a one-sided 95% upper
   time-ratio bound below 1.0, and the expected emitted/executed mechanism.
2. Compare against both the previous retained candidate and the fixed Phase II
   control. This prevents a later patch from silently undoing an earlier gain.
   Keep analysis-only prerequisite patches grouped with their first consumer.
3. Run typed companions on the same binaries and compare their source/workload
   differences explicitly. Include already-optimized Fast Diff, Levenshtein,
   Divrec, `text_search` and the adopted `knucleotide` as protected witnesses.
4. Run the full frozen 63+63 population screen with five pairs. Any apparent
   slowdown over 5%, or unstable/invalid row, gets a focused 31-pair follow-up
   with control noise checks. A confirmed >5% regression blocks retention of
   that change; a noisy screen alone is not a regression verdict.
5. Record compilation time, emitted MIR instruction counts, binary size and
   root-frame changes. Investigate material growth, particularly >10% pilot
   compilation-time growth, before retaining a runtime win. Honor the formal
   emission ratchet independently of this diagnostic threshold.

Run timing campaigns serially without concurrent builds, profiling or baseline
tests. Use MIR Direct JIT explicitly; AUTO and instrumented runs are diagnostic
only. Preserve raw samples, ordering, environment and exact hashes. Targets
missed by an otherwise useful retained slice must be reported as missed; do not
convert a prospective goal into a claimed result. Track implementation status
and performance-objective status separately.

### 15.13 T32-II-7 — Retest and conditionally adopt source alternatives

After the relevant engine change is accepted, rerun the existing annotation-free
experiments using their frozen sources in `temp/tune32/review/sources/`.
Reuse the review manifests and paired runner rather than creating another
benchmark harness.

- After the string track: canonical log pipeline versus scanner; canonical
  Fast Diff versus character-code arrays. Preserve and disclose Fast Diff's
  preprocessing/timing boundary difference from §14.
- After the record track: canonical formatter versus each of its two
  alternatives, and canonical CD versus flattened motion records.
- Keep the adopted `knucleotide` as the Phase II canonical control. Revisit its
  remaining typed gap as a diagnostic only if the shared optimizations reach it.

For adoption, run the same final candidate binary on both source versions,
first nine pairs and then 21 confirmation pairs for any winner. Require no type,
parameter, result or binding annotations; audit the source as well as the
timings. Preserve input size, repetition count, required output and workload
semantics, and disclose any timing-boundary difference. Validate complete
normalized stdout with the existing oracle and full tier/GC checks.

The user's authorization is to replace an untyped source only when the new
annotation-free version is faster. Retain the canonical source when the result
is slower, inconclusive or semantically unmatched. An implementation shortcut
that changes required work is not a benchmark win.

If another source is adopted, preserve the engine comparison on the original
frozen Phase II manifest. Publish a separate source-pair report and a separately
identified post-adoption manifest. Do not combine different source revisions
into one engine geomean or rewrite historical JSON/results.

### 15.14 Implementation touchpoints and verification commands

| File / area | Intended change |
|---|---|
| `lambda/runtime/transpile-mir.cpp` | Extend existing string/result/flow, constructor, field and loop plans; consume facts through shared lowering |
| `lambda/runtime/ast-core.hpp` | Only the minimal existing function/binding-summary extensions actually needed; no duplicate semantic authority |
| `lambda/runtime/build_ast.cpp`, `sys_func_registry.h/.c` | Reuse existing declarative result relations; change registry metadata only if a missing relation is demonstrated |
| `lambda/runtime/mir_shape_candidates.hpp` | Shared constructor/field plan extensions and bounded candidate propagation |
| `lambda/core/lambda-data.cpp` and its existing declarations | Share descriptor-correct field storage only where current helpers cannot already be called |
| `test/mir/lambda/`, existing Lambda test directories | Focused `.ls` / `.txt` and emitted-mechanism fixtures, with GC coverage |
| `temp/tune32/phase2/` | Local manifests, measurements, profiles, gate logs and dispositions |
| Canonical benchmark sources | Only separately validated annotation-free winners from II-7 |

These are candidate touchpoints, not a requirement to edit every listed file.
Search for an existing helper before extending an abstraction. Keep profiling
hooks out of normal benchmark timing and avoid changes to vendored MIR, the
parser, logging configuration or unrelated runtime behavior.

Representative commands from the repository root follow. The control/candidate
archives and frozen manifests are produced by II-0 and each implementation
slice; the paths below do not imply that those artifacts already exist.

```sh
make build-test
./test/test_mir_emission_gtest.exe --gtest_filter='*tune32*'
./test/test_mir_gc_stress_gtest.exe --gtest_filter='*tune32*'
./test/test_mir_ratchet_gtest.exe
python3 utils/check_gc_effects.py
python3 utils/check_gc_root_hazards.py

make release
python3 test/benchmark/run_paired_benchmarks.py \
  --control temp/tune32-phase2/control-release \
  --candidate temp/tune32-phase2/candidate-release \
  --tier jit --variants both --pairs 21 \
  --bench three_way_merge,log_pipeline,prettier_ast,gcbench,raytrace3d,primes,knucleotide \
  --output temp/tune32-phase2/pilot-pairs.json
python3 test/benchmark/run_paired_benchmarks.py \
  --control temp/tune32-phase2/control-release \
  --candidate temp/tune32-phase2/candidate-release \
  --tier jit --pairs 5 \
  --manifest temp/tune32-phase2/frozen-engine-manifest.json \
  --output temp/tune32-phase2/population-pairs.json

make test-lambda-baseline
make test262-baseline
```

Register fixtures so the filtered suites actually execute them, and record
test counts; an empty filter is not a pass. Expand to the existing neighboring
string, array, shape, COW and call-boundary fixtures affected by each patch.
The pilot selector uses current canonical paths, so verify them against the
frozen hashes before running; after any source adoption, use explicit frozen
manifest rows for engine attribution. Run the existing source-pair manifests
with the same candidate binary on both sides for II-7.

Archive each `make release` output under its own immutable step-specific name
before a later test build can replace `lambda.exe`. Record build profile and
SHA-256, and restore/verify the final release at closeout. Never run a performance
campaign against whichever unverified binary a test target happened to leave.
The required engine closeout includes the Lambda and Test262 baselines; no
Node benchmark refresh is required by this plan.

### 15.15 T32-II-8 — Completion checklist and report

All items below are initially unchecked.

- [ ] II-0: freeze the exact control, canonical population, source/input/oracle
  hashes, noise check and per-pilot proof-loss inventory.
- [ ] II-1/II-2: implement and validate a complete string producer → collection
  read → consumer path, with honest null/mixed/unknown fallbacks.
- [ ] II-3: measure residual selector-call cost; implement and retain the bounded
  extension only if justified, otherwise record its conditional deferral.
- [ ] II-4: implement descriptor-correct inferred record construction and prove
  precise lifetime/scalar-storage ownership on positive and fallback paths.
- [ ] II-5: implement a bounded read-only record/consumer slice and measure its
  incremental benefit; separately disposition inferred small-record returns.
- [ ] II-6: implement and measure the eligible loop slice; validate read/store
  ownership, zero-trip and bounds behavior independently.
- [ ] II-7: retest relevant annotation-free alternatives; adopt only confirmed
  winners with separate source-pair provenance.
- [ ] Run focused output/MIR/GC/ASAN checks, root/effect audits and the emission
  ratchet; resolve or explicitly identify inherited gate failures.
- [ ] Pass `make test-lambda-baseline` and `make test262-baseline` for the final
  engine changes; do not mask failed Test262 cases in the test harness.
- [ ] Complete 21-pair pilots, typed companions, the fixed-population screen
  and required regression follow-ups with matching full outputs.
- [ ] Preserve raw samples, exact binaries/build hashes, fixed source manifests,
  emitted/executed mechanism evidence and the final release identity.
- [ ] Update this document with actual per-track results, target met/missed
  status, retained/rejected changes and remaining gaps. Mark Phase II complete
  only when its core implementation and required validation are complete.

The final report must distinguish engine speedup, source-rewrite speedup and
the remaining typed gap. Show the fixed-population geomean **and** sum-of-medians
change, together with the dominant text rows and any regression investigations.
Document a failed optimization as a failed experiment; do not weaken semantics,
certificates, roots or correctness gates to satisfy a timing target.
