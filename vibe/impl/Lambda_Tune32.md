# Lambda Tune32 — Recover Typed Optimizations in Untyped Programs

- **Version:** 1.2.0, 2026-10-02.
- **Status:** COMPLETE. T32-1 through T32-4 are implemented and validated;
  T32-5 is closed as an investigated deferral under its conditional gate.
  All four pilot targets and the round geomean objective are met.
- **Source inspected:** `84748fd62ccaba5c82410fa23b3124c48856e52f`.
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
the preserved copies are now under `test/benchmark/tune32/`:

| Artifact | Purpose |
|---|---|
| [annotation_diagnostics.json](../../test/benchmark/tune32/annotation_diagnostics.json) | Nine-pair minimal-annotation experiments, hashes and uncertainty |
| [three_way_typed_comparison.json](../../test/benchmark/tune32/three_way_typed_comparison.json) | Original / partial / fully typed balanced measurements |
| [divrec_source_pair.json](../../test/benchmark/tune32/divrec_source_pair.json) | Typed/untyped recursion comparison |
| [provenance.json](../../test/benchmark/tune32/provenance.json) | Release identity and MIR-dump source hashes |
| [mir_census.json](../../test/benchmark/tune32/mir_census.json) | Static finalized-MIR function/call census |

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
   durable `test/benchmark/tune32/` evidence directory when implementation
   begins. Keep scratch builds, logs and transient variants under `temp/tune32/`.
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
[`test/benchmark/tune32/`](../../test/benchmark/tune32/).

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
[`control-diagnostics.json`](../../test/benchmark/tune32/control-diagnostics.json).
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
[`final-pilots.json`](../../test/benchmark/tune32/final-pilots.json) gives these
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
[`control-raytrace3d-profile.tsv`](../../test/benchmark/tune32/control-raytrace3d-profile.tsv)
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
[`final-screen.json`](../../test/benchmark/tune32/final-screen.json) and its
[`summary`](../../test/benchmark/tune32/final-screen-summary.json).

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
[`candidate-diagnostics.json`](../../test/benchmark/tune32/candidate-diagnostics.json).
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
See [`final-lambda-baseline.log`](../../test/benchmark/tune32/final-lambda-baseline.log)
and the individual `*-final.json` reports in the evidence directory.

The committed goldens also pass **42/42** explicit executions: seven
fixtures in JIT, Lambda interpreter, AUTO, allocation-every-time with poison,
seeded randomized GC with poison, and MIR interpreter with GC/poison.
The rebuilt `lambda-debug-asan.exe` passes the same **42/42**, with no
sanitizer failure. ASan instruments the native host; JIT-generated raw
instructions additionally rely on the forced-GC and poison oracles.
See [`final-focused.json`](../../test/benchmark/tune32/final-focused.json)
and [`final-asan.json`](../../test/benchmark/tune32/final-asan.json).

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
See [`release-ratchet-diagnosis.json`](../../test/benchmark/tune32/release-ratchet-diagnosis.json)
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
[`test262-timing-diagnosis.json`](../../test/benchmark/tune32/test262-timing-diagnosis.json),
the first full log, and
[`final-test262-baseline.log`](../../test/benchmark/tune32/final-test262-baseline.log).

The 20,000-step tail oracle additionally passes JIT, forced-GC/poison and
MIR-interpreter GC/poison execution. Its exact source, expected output and
observed results are preserved in
[`deep-tail.json`](../../test/benchmark/tune32/deep-tail.json).

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
[`README`](../../test/benchmark/tune32/README.md) and
[`closeout.json`](../../test/benchmark/tune32/closeout.json).

Concurrent Julia benchmark infrastructure changes are outside this work and
were preserved. No named ResultN was published or backpatched. Deferred
record propagation and the remaining declared-array proof advantage remain
explicit future work, not claimed Tune32 optimizations.
