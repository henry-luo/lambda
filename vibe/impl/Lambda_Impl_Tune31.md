# Lambda Implementation: Tune31 — Specialization and Typed Runtime Boundaries

- **Status:** Phase I IMPLEMENTED 2026-09-19; Phase II typed tuning added
  2026-09-20 (§6). Phase II's typed Hyphen source rewrite and inferred-store
  engine track are implemented, as is the ownership/path A track; B's
  and E's source experiments are rejected by their paired results, while C
  and D close without speculative engine changes.
  Phase I's final code, focused semantic gates and paired release evidence
  are recorded in §3.7 and §4.4. The full Phase II baseline passes, as
  recorded in §6.5. Three follow-on typed source ports are recorded in §7;
  they are intentionally separate from the Tune31 engine results.
  The post-Phase-II typed engine review is in §8. **Phase III is COMPLETE**
  in §9: B1 fixed-arity owned string append, B2 typed character-pair equality
  and A1 typed field borrows are implemented and release-confirmed; A2, C and
  D1 are rejected, and D2/E are deferred with evidence. The full-suite typed
  objective was missed and the row-level regression guard is inconclusive.
- **Scope:** recover native execution in untyped Lambda, close measured
  regressions, and remove repeated runtime work in both Lambda variants.
- **Primary evidence:** [Result42–47 analysis](Lambda_Benchmark_Result47_Analysis.md),
  the checked-in Result42–47 JSONs, the clean `6e1774947` release control
  archived as `temp/tune31/lambda_tune31_control_6e1774947_release`
  (`3c75180f…56abd9`), and the final candidate archived as
  `temp/tune31/lambda_tune31_final_release` (`4e4107d9…c2148`).
- **Predecessors:** [Tune29](Lambda_Impl_Tune29.md),
  [Tune30](Lambda_Impl_Tune30.md), and
  [Tune27](<Lambda_Impl_Tune27 (done).md>).
- **Authority:** [formal semantics](../../doc/Lambda_Formal_Semantics.md)
  and [formal design](../../doc/Lambda_Formal_Design.md). This implementation
  proposal changes no ruling. Track numbers below replace the earlier
  Tune31 ordering.

## 1. Evidence and comparison contract

The round must improve the untyped programs themselves. Annotation-only
experiments identify information the compiler loses; changing the benchmark
ports is not an engine optimization.

C2MIR measures independent native C ports through the MIR C frontend, not
the removed Lambda C-text backend. Both paths use MIR, but the C programs
often do less ownership, numeric, and boundary work. Their times guide
investigation; they are not a promise of parity under identical semantics.

### 1.1 Progress after accounting for drift and changed ports

Execution-time geometric means over the same 63 benchmark names:

| Result | Untyped / C2MIR | Typed / C2MIR | Untyped / Node | Typed / Node |
|---|---:|---:|---:|---:|
| 42 | 8.06x | 6.53x | 1.56x | 1.27x |
| 44 | 6.81x | 4.41x | 1.32x | 0.86x |
| 46 | 6.83x | 4.26x | 1.20x | 0.75x |
| 47 | 6.35x | 3.59x | 1.26x | 0.71x |

Result46 is load-inflated. Its untyped time is 12.6% above Result44, while
C2MIR is 12.2% higher. From Result46 to Result47, untyped time falls 15.0%
and Node falls 19.1%, so worsening untyped/Node is not itself a regression.

Per-row C2MIR normalization is a drift screen, not causal attribution.
Exclude changed sources before interpreting it: the R46→47 C nqueens port
became 2.63x slower, typed cd changed, and typed three_way_merge changed
between R44 and R46. Result45 has no C2MIR measurements.

| Comparison | Excluded rows | Remaining rows | Normalized untyped time | Normalized typed time |
|---|---|---:|---:|---:|
| R42 → R47 | nqueens, cd, three_way_merge | 60 | 0.800 | 0.570 |
| R44 → R47 | nqueens, cd, three_way_merge | 60 | 0.949 | 0.845 |
| R46 → R47 | nqueens, cd | 61 | 0.946 | 0.875 |

Thus the last round gives about **5.4% lower untyped time and 12.5% lower
typed time** on the stricter population, not an across-the-board 16% engine
gain. These filters check entry sources/reference ports, not every
transitive dependency. R47's crypto_sha1 and pidigits Lambda cells use a
separately repaired release; preserve that provenance in any replay.

### 1.2 Regression triage

| Workload | Evidence | Treatment |
|---|---|---|
| splay, both variants | R46→47: untyped 317.863→341.701 ms, typed 305.914→325.548 ms; all Lambda sample ranges separate, C stays near 19 ms | Highest-priority unresolved regression; responsible change not established. |
| fannkuch, typed | Earlier intermediate-binary bisection identifies extra loop ownership checks (§1.5) | Reproduce and repair fact precision while retaining the correctness of LR12-17. |
| towers, untyped | R44→47: 1.177→1.327 ms; typed 0.326→0.322 ms | Replay and inspect the generic wrapper/call path. |
| permute / pidigits, untyped | R44→47: +8.6% / +12.5%; pidigits delta is only 0.037 ms and uses a repaired cell | Secondary candidates; require sustained or paired confirmation. |
| puzzle, typed | R42 2.833→R43 16.140→R47 13.898 ms | Correctness repair, not a target to restore by deleting snapshots. |
| havlak / fib | R46 controls are noisy; R44→47 havlak improves in both variants and typed fib changes only 1% | Expensive workload / weak signal, respectively; neither is an established new regression. |

**S9.1.3** requires a mutated plain parameter to remain a snapshot; only
`var` parameters borrow the caller's place. Tune27 §2.1 reproduced v42's
missing snapshot on the typed native-witness edge. Puzzle uses `bool[]`
plain parameters, not an unexplained `int[]` place-copy pattern.

The current puzzle census records 174,909 ArrayNum copies. A diagnostic
`var`-parameter port runs 11.839→2.149 ms with the same answer, but changes
the ownership contract. Keep the original as a correctness guard; do not
count that port rewrite as an engine speedup.

### 1.3 Fresh evidence for untyped priorities

Same release binary, alternating source pairs, successful matching output
for every valid pair. These are diagnostic variants, not shipped gains.

| Isolated change | Control → variant median | Pairs | What it identifies |
|---|---:|---:|---|
| nbody: annotate only the two `j = i + 1` locals as `int` | 45.012→27.288 ms | 7 | Nested counter facts are lost despite native float reads/arithmetic. |
| quicksort: `int[]` on the producer binding and three array parameter positions | 10.387→0.953 ms | 7 | The array witness fails to reach the partition body. |
| text_search: type only search parameters | 13.186→32.675 s | 3 | Repeated consumer admission can outweigh faster loop code. |
| text_search: those parameters plus `to_codes(...) int[]` return | 13.004→1.424 s | 3 | Establishing the representation at production removes repeated conversion. |

Quicksort's parameter-only attempt failed with E207 because an open
`array` cannot be borrowed as declared `var int[]`; it is excluded from
timing claims. The producer binding is part of the valid experiment.

Executed counters explain two of these gaps:

- Untyped nbody performs **2,700,000 helper-mediated unique ArrayNum
  mutations with zero copies**. Typed nbody avoids those mutation helpers.
- Parameter-only text_search makes **73,728 `fn_mutable_value` calls**:
  1,536 rounds × eight patterns × three algorithms × two array arguments.
  Adding the producer return contract reduces that to **nine**, one corpus
  and eight patterns. Both variants produce checksum 91395120.

In R47, text_search accounts for 55.9% of summed untyped execution time;
all seven text rows account for 88.3%. This supports prioritizing it, while
the geomean must still give every row equal weight.

### 1.4 MIR evidence and remaining typed overhead

Current dumps and the earlier v47 review agree on the mechanisms below.
Static call counts include cold branches; do not equate them with executed
calls or allocation counts.

| Workload | Evidence | Implication |
|---|---|---|
| nbody, untyped | Native array witness and `sqrt` exist, but `j` uses `fn_lt`/`is_truthy`/`fn_add`; stores use `index_assign_cow` and mutation helpers | Fix scalar/body and store-proof propagation together. |
| quicksort, untyped | `partition` lacks an array witness; nine static `fn_index` and six `fn_array_set` sites; generic comparisons in loops | Recover the producer/recursive-call/element chain. |
| pnpoly, both | Native comparisons feed boxed `fn_ne` | Native boolean equality removes the surrounding call boundary too. |
| crypto_sha1, earlier v47 review | Boxed shift/bitwise results feed generic arithmetic; `safe_add`/`rol` fail leaf-inliner admission | Preserve native results first, then inline eligible leaves. |
| bounce / levenshtein, earlier v47 review | `abs`, nullable conditions and character operations cross helpers with root traffic | Lower proven primitive operations and retain correct nullable/UTF-8 fallbacks. |
| towers, earlier v47 review | Recursive typed witness checks are cold; live-root traffic, wrappers and `var` homes remain | The old “33% type_check” profile no longer sizes this work. |

Typed R47 has 21 rows within 2x C, 17 at 2–5x, 21 at 5–20x, and four
above 20x. The remaining expensive families include ownership/path work
(havlak, deltablue, splay, hashmap), construction (cube3d, brainfuck),
and strings/collections (hyphen, microdiff, knucleotide, base64).

R46-era helper percentages are hypotheses for re-profiling, not current
attribution. Typed nbody emits more static MIR than untyped but executes
far fewer hot helper calls. Tune30's register-pressure findings likewise
require native spill and executed-path measurements, not a raw MIR LOC goal.

### 1.5 Historical fannkuch bisection retained

The original Tune31 investigation recorded 5–7 interleaved runs:
v46 0.305/0.311 ms min/median; Tune29 through `lambda-bsa` 0.304–0.312;
`lambda-items` 0.340/0.367; later Tune30 binaries 0.336–0.355; v47
0.336/0.341. The step falls in Tune29 §20, later committed in `f029ed1a0`,
covering `MirCowJoin`, loop premarking, `MirCowLoopJoin` and handle work.

The recorded MIR delta is eight extra shared-bit checks and 22 branches
around stores to `perm`, `perm1`, and `count`, without a changed call
census. The current typed dump has nine static shared-bit loads, while its
executed COW census reports no share marks or copies. This corroborates
a precision problem, but does not reproduce the historical delta.

The old v46/v47 and `temp/t29`/`temp/t30` archives are absent from the
reviewed workspace. Recover them or rebuild isolated revisions and label
them as reconstructions. Do not claim an exact historical replay from a
different binary, or generalize this defect to every typed-array loop.

## 2. Constraints on the implementation

| Ruling | Consequence for Tune31 |
|---|---|
| **D3.3.2v2–D3.3.4** | Entry specialization is separate from body/result inference. Inferred narrowing stays scoped to its binding; it cannot create a source contract or an explicit array certificate. Preserve legal widening of open arrays. |
| **D3.3.5, D8.2.5v2–D8.2.6** | Reuse declarative builtin result relations, the shared analysis pipeline, and representation-aware lowering; do not add another ad hoc type oracle. |
| **D8.3.1v2–D8.3.4v3** | Raw entries need a complete exact key and either static proof or the boxed wrapper's exact guard. Preserve fallback and `var`-home transport. No per-parameter partial dispatch scheme. |
| **D8.4.1v2** | No mutable inline caches or feedback-driven specialization. |
| **D2.5.1–D2.5.3, D2.8.1–D2.8.3** | Test the actual carrier's null sentinel; keep null/error values out of native arithmetic. Unproven indexed reads stay nullable. |
| **S4.1.1–S4.1.5, S4.2.3** | Preserve int saturation, poison behavior, sized-integer wrapping and NaN equality; a native opcode alone is not a semantic proof. |
| **S9.1.2–S9.1.3, D4.4.4v4–D4.4.6** | Preserve snapshots, borrow publication and handle invalidation. Remove unnecessary checks/copies only with an ownership proof. |
| **D5.3.1–D5.3.4** | Root dirty live values at collecting calls; `NO_GC` needs transitive mechanical verification. Root policy remains in `MirEmitter`; never restore conservative native-stack scanning. |

Missing effect/defect analysis is not permission to assume a clean lane.
Retain the existing error channel and the
[LR12-24](../Lambda_Issue_Ledger.md#lr12-24) containment obligations.

## 3. Implementation tracks

T31-0 through T31-5 are complete. T31-6 remains the explicitly deferred
design work described below; it was not required to ship the established
specialization and boundary fixes.

### T31-0 — Establish controls and repair confirmed regressions

1. Archive the implementation-start release, commit, source manifest and
   binary hash. Keep performance artifacts separate from profiling runs.
2. Replay splay in both variants, then untyped towers/permute and pidigits.
   For splay, compare allocation/COW counters, mandatory helper paths,
   frames and native spill behavior; bisect only a reproduced difference.
3. Reproduce fannkuch on a minimal local-array loop and audit
   `mir_premark_loop_cow_bindings`, `MirCowJoin` and `MirCowLoopJoin`.
   Distinguish scalar element reads from escaping container aliases.
   Refine the transfer/join rules; do not revert the
   [LR12-17 correctness fix](../Lambda_Issue_Ledger.md#lr12-17).
4. Pair that fixture with capture-after-store, conditional detach,
   zero-iteration, nested-loop and real-alias cases. Pin absence of repeated
   ownership checks in the proven unique body, and their retention where
   sharing is real. A module-wide “zero COW checks” assertion is not sound.

**Exit evidence:** matched-source regression results, a root-cause record
for each confirmed fix, mechanism pins and unchanged aliasing outputs.
Unreproduced candidates remain labelled as such. Replace the old absolute
0.315 ms and noisy v46-normalized gates with same-session release ratios.

### T31-1 — Preserve untyped counter and array facts

**Pilots:** unchanged untyped nbody and quicksort; guard rows include
permute, queens, towers, primes and typed twins.

Start at `infer_param_types_batched`, `resolve_inferred_type`,
`mir_callsite_join_elem`, `mir_callsite_join_specialization_type`, local
initializer/result inference, and the finite/dense-loop proof consumers in
[transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp).

1. Extend the existing specialization diagnostic to explain refusal:
   unresolved producer/return, conflicting caller, dynamic key, incompatible
   store, lost witness or unproven ownership. Report the binding/call edge.
2. Trace nbody's `i + 1 → j → comparison/index/update` facts. Preserve the
   native integer carrier where the accepted program proves it; make the
   existing array-store proof consume that information.
3. Trace quicksort's `fill → binding → quicksort → partition → recursive
   call` facts, including values written back into the array. Separate
   “not yet resolved” from a genuinely conflicting/dynamic shape in the
   existing fixed-point analysis; convergence must remain conservative.
4. Feed proven inferred carriers into shared typed/native lowerings.
   Reuse exact entry guards and full boxed fallback; do not just relax
   the honest mixed-array join or enable speculative-lift flags globally.

**Mechanism gates:** no generic counter comparison/addition in nbody's
proven integer loop; no per-element mutation helper on its admitted unique
native store path. Quicksort's admitted partition uses element lanes and
native comparisons instead of generic index/compare/set work.

**Correctness cases:** recursive forwarding, mixed Array/ArrayNum callers,
string indices, return-type conflicts, nullable/out-of-range reads,
representation-changing writes, aliases and `var` write-back. An inferred
lane must never silently become an enforced `int[]` contract.

### T31-2 — Preserve producer/return representation and avoid repeated admission

**Pilot:** original untyped text_search plus the existing parameter-only
microbenchmark, the diagnostic producer-return variant and the fully typed
port. Measure all four separately; source edits are controls only.

The confirmed conversion bottleneck is in the **partially typed** variant.
The original untyped row primarily exposes missing native element facts.
The annotation experiment does not prove that an untyped compiler can
simply attach the typed return's certificate.

1. Trace `to_codes → return → corpus_codes / pattern_codes[index] →
   search parameters → indexed reads`. Reuse existing producer and
   result-relation machinery before adding any analysis.
2. Preserve justified element/carrier facts in the caller's scope and in
   complete exact entry plans. An open array may use a compact physical
   representation while retaining legal widening; inferred narrowing must
   not be installed as an explicit contract certificate (**D3.3.3v3**).
3. For **explicit** typed boundaries, establish and reuse the full
   rank/leaf/carrier certificate at an eligible producer, or reuse an
   admitted replacement within a proven unchanged-value region. Audit
   `runtime_array_admit_primitive_contract` and
   `runtime_type_admit_array_env` in
   [lambda-eval.cpp](../../lambda/runtime/lambda-eval.cpp).
4. Preserve admission/error timing and evaluation order. Do not move a
   failing check across effects, replace a caller's observable open array,
   hoist across a possible write, or add a conversion cache as a substitute
   for proving lifetime and identity.

**Mechanism gates:** the admitted hot search loop loses generic
length/index/equality work; conversion in the eligible partial-typing
fixture scales with produced arrays, not repeated search calls. The
observed nine conversions are a workload reference, not a hard-coded
count: vary corpus/pattern counts and repeat count to verify the scaling.

**Correctness cases:** heterogeneous elements, array widening, caller-side
mutation between searches, aliasing, views/rank, nullable/refined contracts,
conversion failures and GC during construction. Where the available proof
is insufficient, retain the existing boundary path and record the gap.

### T31-3 — Remove scalar helper boundaries, then inline eligible leaves

**Pilots:** pnpoly, bounce, crypto_sha1 and levenshtein, both variants.
First reconfirm the hot sites in the candidate's MIR/profile.

1. Lower `bool == bool` / `bool != bool` directly when both operands
   have valid native carriers. Nullable comparisons/conditions must follow
   their semantics and representation: **D2.5.2v3** gives native `bool?`
   the sentinel **2**, not the boxed `ItemNull` word.
2. Lower proven scalar `abs`, min/max and numeric conversions using the
   existing semantic helpers/proofs. Float-to-int is not generally a bare
   `d2i`; handle rounding, bounds, null and poison correctly.
3. Lower bitwise operations/shifts only for the proven domain, with correct
   signedness, shift-count and saturation/wrapping behavior. Neither
   `shr` nor an `int` annotation by itself justifies unsigned shift or
   removal of band checks; distinguish `int` from sized integer lanes.
4. Inline character loads only with a valid ASCII and bounds proof;
   preserve UTF-8 and out-of-range behavior on the fallback. Keep native
   results through arithmetic and comparisons to prevent immediate
   re-boxing; reuse an existing boxed string carrier where appropriate.
5. Once these bodies are native, extend the existing leaf inliner to
   eligible sized-integer locals, bitwise operations and small conditional
   bodies. Preserve error/effect/ownership paths and enforce code budgets.

**Mechanism gates:** eliminate the identified hot helper boundary, not
merely rename its callee. Pin pnpoly's native boolean comparison and the
eligible crypto leaf calls. Cover null, poison, signed/unsigned edges,
shift counts and non-ASCII input in semantic differential tests.

Pure syntax does not establish `NO_GC`. Any remaining helper classified
`NO_GC` must pass the transitive checker (**D5.3.2**); mixed-type
comparison and conversion helpers cannot be blanket-allowlisted.

### T31-4 — Profile and reduce ownership, construction and string work

**Priorities:** splay/deltablue/hashmap/richards; cube3d/brainfuck/storage;
hyphen/microdiff/knucleotide/base64 and the expensive text rows.

Obtain a current profile plus a hot-function MIR listing and executed
counters before selecting each subtask. Havlak remains eligible for
profiling; its real snapshots do not imply every surrounding cost is
unavoidable. Puzzle's required plain-parameter copies stay out of the
“delete excess COW” target.

- Reuse handle-relative identity/layout/ownership proofs across valid
  regions; distinguish repeated checking from real sharing and preserve
  the existing invalidation rules (**D4.4.4v4**).
- Build the representation required by an explicit array contract using
  existing constructors/admission helpers. Specializing `fill` must keep
  count/value/error semantics and GC ownership; zero fill must preserve
  the actual payload, including floating signed zero.
- Attribute string cost to traversal, comparison, copying, allocation or
  dynamic collection dispatch before changing it. Measure copied bytes
  as well as time; do not assume concatenation always copies after the
  earlier in-place append work.

**Exit evidence:** a reproducible profile-backed cost removed on unchanged
sources, with valid certificate/ownership lifetimes and matched outputs.
Do not require `fn_fill` to disappear merely to satisfy a call-count gate.

Region allocation, stack containers and caller-provided result buffers
remain deferred if they require new lifetime/ABI rules. Non-escape alone
does not prove a buffer dead at the next iteration (**D4.1.4v4, D5.2**).

### T31-5 — Reduce proven call-boundary costs

After T31-1/3, re-profile towers, queens, quicksort and permute. Inference
or native lowering may already have removed the costly calls.

Use the current `MirEmitter` machinery to measure and reduce redundant
root publication, unnecessary reloads or remaining wrapper transitions.
The model is dirty **live** roots at `MAY_GC` boundaries, not an assumption
that every Item is spilled at every call (**D5.3.1–D5.3.4**).

Keep CW33 home transport and replacement publication for `var` arguments.
Do not remove them because the input happened to be unique in one run.
Stack maps or a different borrow/call ABI require a separate design
proposal and are not prerequisites for the primary Tune31 tracks.

### T31-6 — Structural codegen work, deferred

Revisit fft/quicksort/nbody only after the preceding improvements and
fresh native-code profiles. Tune30 found register pressure and bounded the
benefit of more index-check removal in its probes; moving a cold block
does not automatically shorten live ranges across it.

Possible follow-ups are a whole-loop proof with a correct generic exit,
or tighter loop-local representation/liveness. Any mid-loop transfer must
preserve completed writes, roots and error timing without replaying
effects. Document unresolved design obligations before implementation;
do not promise a large gain from static MIR shrinkage alone.

### 3.7 Implementation closeout

#### T31-0 — control and regression attribution

The clean source control is 6e1774947, built with make release before any
debug/test build and archived with its SHA-256 above. The candidate was
rebuilt from the final source with make release and archived separately.
Every paired artifact names both hashes, the same script hash on each side,
the execution tier, raw samples, normalized stdout digests and paired
bootstrap configuration.

The implementation-start/final comparison shows no material new splay
regression; it does not bisect the earlier R44→R47 change. The 41-pair JIT
result is 296.380→294.127 ms (untyped ratio 0.9924, one-sided 95% upper bound
1.0049) and 280.980→282.922 ms (typed 1.0069, upper 1.0184), with identical
output. Fannkuch's earlier ownership suspicion likewise has no sustained
regression: untyped is 0.264→0.267 ms (1.0114, upper 1.0228) and typed is
0.322→0.318 ms (0.9876, upper 1.0063). No COW rule was weakened or reverted.

#### T31-1 — untyped counter and recursive-array facts

mir_matches_compact_loop_add now verifies that the matched i + 1 expression
is the selected counter assignment, rather than accepting every syntactically
similar expression inside the loop. This keeps a nested counter initializer
on its own native lane and preserves the source update semantics required by
**S4.1.1–S4.1.5**.

The call-site fixed point now ignores only an unknown self-forward of the
callee's own formal. Such an edge contributes no competing representation
fact before its first iteration; concrete external evidence and all boxed
fallbacks remain in the join. This allows fill → recursive var parameter →
partition/store to retain an inferred ArrayNum witness under **D3.2.1** and
**D3.3.1**, without turning it into a declared array contract. The existing
T21 forwarding fixture now pins the resulting representation-agnostic COW
setter and its widening fallback.

The tune31_nested_counter and tune31_recursive_array_witness sidecars pin
the corresponding MIR paths. On the final structural census, nbody has no
generic comparison/addition/truthiness call in its admitted loop, and
quicksort's recursive partition has no generic index or array-set call on
its admitted raw path.

#### T31-2 — append-only producer and return facts

An unannotated empty array can now become ArrayNum only when a conservative
function-local scan proves one owner, homogeneous direct push writes, no
assignment/index write/alias/escape/nested closure, and a return of the same
binding. The scan also recognizes a nested array-return builder only at the
consumer boundary; the outer open-array index remains checked. This retains
legal widening and the boundary discipline in **D3.2.1**, **D3.3.1** and
**D3.3.3v3**.

lambda_array_int_push_inferred_cow is the shared COW-preserving runtime
entry for the proved integer builder. It roots owner and value, prepares a
write, checks the actual ArrayNum lane, and appends through the ordinary
integer lane setter. It does not use a conversion cache or change
admission/error timing. tune31_append_builder_return and
tune31_nested_builder_return pin the direct producer and the checked nested
consumer boundary. The original text-search source now produces its code
arrays once through this path instead of reopening generic collection work on
every consumer call.

#### T31-3 — nullable Bool equality boundary

Ordered comparisons can return Bool or Null under **S6.1.2**, while equality
is total under **S5.1.1**. Stable local bindings initialized by a native
ordered comparison now compare their canonical Bool/Null Items directly.
The proof rejects ordinary writes, mutable-call transport, nullable/error
operands and any unproven numeric carrier; native Bool lanes still use their
separate direct path. Thus the optimization cannot replace null != null or
null != bool with plain C boolean semantics.

tune31_bool_null_equality covers Bool/Bool, Bool/Null and Null/Null on
interp, JIT and auto tiers. tune31_bool_null_equality_reassign proves that a
later write retains the generic fn_ne path. The pnpoly hot raw loop has
direct canonical Item equality and no mandatory helper call.

#### T31-4 and T31-5 — measured construction and call-boundary work

Fresh paired results attribute the removed work to the T31-1/T31-2
representation chain rather than to a broad ownership relaxation. The append
builder removes recurring construction/admission work in text search; the
counter, recursive witness and Bool equality changes remove the corresponding
generic call boundaries. Splay, deltablue, hashmap and the other
ownership-heavy guards were screened on unchanged sources; no new ownership
rule, snapshot change, root policy or var home transport was introduced.
This preserves **S9.1.2–S9.1.3**, **D4.4.4v4** and **D5.3.1–D5.3.4**.

### 3.8 Regression-pin audit (2026-09-19)

Every effective T31 mechanism now has a structural pin and a semantic
fixture. T31-1's `tune31_nested_counter` and
`tune31_recursive_array_witness` sidecars pin the native counter and
self-forwarded ArrayNum paths, with the T21 forwarding fixture retaining the
representation-agnostic COW fallback; the corresponding parity tests run on
interp, JIT and auto. This holds the S4.1.1-S4.1.5 counter rules and the
D3.2.1/D3.3.1 inference boundary together. T31-2's direct and nested builder
fixtures pin the inferred append helper, raw direct consumer, and checked
outer-boundary fallback under D3.2.1/D3.3.3v3 and S9.2.2. T31-3 has both the
native Bool-or-null equality positive case and the reassignment negative case,
which retain S5.1.1/S6.1.2 total equality. T31-0 made no engine change, T31-4
and T31-5 found no additional implementation target, and T31-6 remains
deferred.

The immediately preceding effective rounds are also pinned by executed test
surfaces: Tune27 combines MIR sidecars, three-tier parity fixtures and COW
profile assertions; Tune28 has module-constant/fixed-index sidecars, its
place/COW parity cases, and the permanent split-kernel optimization fixture noted
above; Tune29's required-field, handle, store and flow-join cases have
sidecars or Lambda optimization counters; Tune30's inlining/store/interval
sidecars and semantic fixtures are supplemented by the
`lambda_tune30_libm_no_gc` MIR budget ratchet. Measured-but-reverted or
timing-neutral proposals are deliberately excluded because no shipped fast
path needs a performance pin.

## 4. Measurement and acceptance

### 4.1 Controls and timing

Use the same original sources for control and candidate, both built with
`make release`. Archive the control before tests that may replace
`lambda.exe` with a debug binary; rebuild release before measuring again.
Run serially on AC power without concurrent builds. Record commit, binary
hash, source/dependency identity, environment, raw samples and outputs.

Use [run_paired_benchmarks.py](../../test/benchmark/run_paired_benchmarks.py):
seven or nine pairs for screening, then 41 pairs for claimed pilot gains
and suspected regressions. Compare medians and paired uncertainty, not
min-of-N. A timing failure or output mismatch invalidates the comparison.
Short rows need an equivalent sustained fixture if timer noise prevents
a useful conclusion; keep the canonical workload unchanged.

Collect profiles/COW counters separately from timing. Track JIT execution,
auto end-to-end wall time, and compile/code-size effects separately.
C2MIR uses the same pinned reference driver and unchanged source on both
sides; an edited port gets a new comparison boundary, not a drift factor.

### 4.2 Proposed performance objectives

These are initial goals against the implementation-start matched release,
not extrapolated speedups from annotations or host-specific millisecond
limits. Any missed goal remains visible in the completion record.

| Scope | Candidate/control time objective | Required evidence |
|---|---:|---|
| Untyped nbody | ≤0.65 | Native counter/store mechanism, not annotation edits |
| Untyped quicksort | ≤0.30 | Array witness reaches partition with correct fallback |
| Untyped text_search | ≤0.35 | Producer/return/element facts reach search loops |
| Parameter-only text_search | ≤0.25 | Repeated conversion eliminated in the eligible region |
| pnpoly, both variants | ≤0.75 | Hot native boolean comparison |
| Complete 63-row untyped geomean | ≤0.85 | Same sources and all successful output checks |
| Complete 63-row typed geomean | ≤0.90 | Gains in both affected and guard workloads |

The previous typed/C2MIR ≤2.8x remains a **stretch reference**, reported
only with the matching C ports. It is not a replacement for same-source
engine A/B. Report typed/untyped inversions (bounce, fannkuch, fasta,
microdiff); do not enforce “typed ≤ untyped on every row” as if different
contracts always did identical work.

No reproducible execution or auto end-to-end regression above 3% is
accepted on an unchanged row. Use paired confirmation; for claimed
non-regression on pilots/flagged rows, require the one-sided 95% upper
ratio bound ≤1.03. An inconclusive bound needs better measurement, not a
pass declaration. Small rows may require sustained companion fixtures.

### 4.3 Correctness and completion gates

- `make test-lambda-baseline` must pass. Establish the actual current
  baseline; do not inherit the old document's “two pre-existing failures”
  exemption or mask failures in a harness.
- Run affected corpus/fixture comparisons in interp, jit and auto against
  expected output, including the negative cases for the changed mechanism.
  A matching old bug is not a correctness oracle.
- Exercise affected ownership/native-boundary cases with
  `LAMBDA_GC_FORCE_EVERY=1`, `LAMBDA_GC_POISON_FREED=1` and
  `LAMBDA_ROOT_WITNESS=1`; preserve precise rooting and containment.
- Pass affected MIR emission sidecars and the shared MIR ratchet.
  Each performance mechanism needs a focused pin whose failure can be
  demonstrated when that mechanism is disabled; distinguish cold arms
  from the admitted hot path.
- Add a corresponding expected `.txt` for every new Lambda `.ls`
  fixture. Keep benchmark-shaped special cases out of implementation.
- Run the guarded standard snapshot workflow for publication, including
  its release/instrumentation/Test262 gates, both Lambda variants, and
  separate end-to-end reporting. Preserve the cached binary and manifest.

A track's closeout records its root cause, shared implementation, semantic
tests, executed-path evidence, paired results in both variants, unresolved
gaps and whether its performance objective was met. New defects belong in
the existing central issue ledger; this document is not another ledger.

### 4.4 Completed evidence

The final JIT confirmation used 41 alternating pairs and one-sided paired
bootstrap bounds:

| Row | Control → candidate | Ratio, upper 95% | Objective |
|---|---:|---:|---|
| untyped AWFY nbody | 46.146→27.291 ms | 0.5914, 0.5949 | met |
| untyped Larceny quicksort | 10.457→0.588 ms | 0.0562, 0.0566 | met |
| untyped text_search | 13049.600→1257.970 ms | 0.0964, 0.0969 | met |
| typed Larceny pnpoly | 10.023→3.086 ms | 0.3079, 0.3130 | met |
| untyped JetStream splay guard | 296.380→294.127 ms | 0.9924, 1.0049 | no regression |
| typed JetStream splay guard | 280.980→282.922 ms | 1.0069, 1.0184 | no regression |

The full JIT screen covered 63 workloads × two variants × seven pairs:
126/126 rows completed with identical normalized output. Its six
screen-only ratios above 1.03 received 41-pair follow-up. Their medians all
fell below 1.02; the remaining wide bounds belong to 0.02–1.2 ms rows and
are timer-resolution limited, not reproduced execution regressions.

Auto-tier confirmation also used 41 pairs for the primary rows. Its
execution and process-wall median ratios were respectively: untyped nbody
0.6945 / 0.7415, untyped quicksort 0.3635 / 0.6202, untyped text_search
0.0956 / 0.1077, typed pnpoly 0.5141 / 0.7277, untyped splay 0.9947 /
0.9960, and typed splay 1.0006 / 1.0024. Every sample had matching
normalized output.

Focused acceptance passed after the final test build: all 163 MIR-emission
checks; six Tune31 interp/JIT/auto parity tests; and the same six with
LAMBDA_GC_FORCE_EVERY=1, LAMBDA_GC_POISON_FREED=1 and
LAMBDA_ROOT_WITNESS=1. The new Lambda fixtures all include expected text
files and MIR sidecars.

At the time this Phase I record was made, make test-lambda-baseline could
not complete: its parallel batch workers stalled after entering the large
Lambda corpus. A serial reproduction isolated graph_transform_html at the
test-batch 60-second limit, and its timeout cleanup stalled; a direct
execution also exceeded two minutes. The archived clean 6e1774947 control
independently exceeded that same boundary. This was a host/batch-lifecycle
observation, not a standing exemption: the Phase II final validation in
§6.5 subsequently completes the full baseline without a harness change.

The recorded artifacts are:

- temp/tune31/paired_confirm_affected_jit_41.json
- temp/tune31/paired_screen_full_jit_7.json
- temp/tune31/paired_confirm_screen_outliers_jit_41.json
- temp/tune31/paired_confirm_primary_auto_untyped_41.json
- temp/tune31/paired_confirm_primary_auto_typed_41.json
- temp/tune31/mir_emission_final.log
- temp/tune31/tune31_final_tiers.log
- temp/tune31/tune31_final_forced_gc.log

## 5. Evidence and corrections to the earlier proposal

The [Result47 analysis](Lambda_Benchmark_Result47_Analysis.md) retains the
full calculations and limitations. Supporting artifacts:

- `temp/result47_analysis/history.py` / `history.json`: historical
  population, drift and source-change accounting.
- `probe.json`, `mir_census.json`, named `.mir` and `*_cow.tsv`:
  current release identity, outputs, structural census and executed counts.
- `source_pairs.json`, `quicksort_chain_pairs.json`,
  `text_params_pairs.json`, `text_return_pairs.json`: isolated source
  experiments; the invalid parameter-only quicksort row stays excluded.
- `text_params_cow.tsv` / `text_params_return_cow.tsv`: 73,728 versus
  nine conversions, with corresponding output and MIR evidence.
- Tune27 §2.1: the archived puzzle correctness reproduction.
  Tune29/Tune30 and §1.5 above retain the earlier implementation evidence.

The rewrite replaces five earlier assumptions: all C ports were unchanged;
C-normalized ratios established causality; all high-ratio rows were
runtime-call bound; R46 helper percentages still described R47; and
annotation-only gains could be obtained by applying more contracts at
consumers. It also removes the incorrect nullable-bool sentinel recipe,
unqualified numeric opcode substitutions, and the blanket pure-helper
`NO_GC` proposal.

Historical `temp/r47`, `temp/t29` and `temp/t30` paths are provenance,
not guaranteed available tooling. Use the checked-in paired runner and
`mir_mandatory_census.py`; retain any new probes under `./temp/`.

## 6. Phase II — Typed Lambda ownership, construction and strings

**Added:** 2026-09-20. **Status:** typed Hyphen source port, the
inferred-store engine track and the ownership/path A track are implemented.
B and E's source experiments are rejected; C and D were re-profiled and
closed without speculative engine changes:
Phase II does not claim an ownership, construction, string or code-structure
change unless it satisfies its own executed-cost and paired-timing gate.
Phase I's completion record above does not claim completion of this new
phase. No formal semantic or design ruling changes.

### 6.1 Motivation and measured starting point

Recomputation of Phase I's full 63-row, seven-pair JIT screen gives an
untyped execution geomean ratio of **0.8675** and a typed ratio of
**0.9778**. Both miss §4.2's original suite objectives, respectively 0.85
and 0.90. Excluding typed pnpoly leaves **0.9962** over 62 typed rows;
52/63 typed rows remain within ±3%. The large quicksort/text-search gains
mostly recover paths their typed twins already had. T31-4/5 shipped no
additional ownership or construction implementation, and T31-6 was deferred.

The [post-Tune31 analysis](../../temp/tune31_analysis/report.md) uses the
archived final release `4e4107d9…c2148`. Canonical executed counters and
separate sustained samples identify the following costs:

| Typed pilot | Canonical executed evidence | Sustained sample evidence | Phase II target |
|---|---|---|---|
| splay | 951,168 map copies; 61,825,920 copied bytes | 37.5% write preparation; 29.9% GC; 5.7% JIT self | Avoid provably unnecessary share/detach operations and repeated path work |
| havlak | 44,999 array + 65,876 map copies; 7,502,280 bytes | 28.7% write preparation; 28.3% GC; 15.1% type checking | Nested ownership facts and admission reuse |
| deltablue | 38,980 ArrayNum copies; 3,409,760 bytes | Fresh timing attribution still needed | Shared-child borrows and repeated checked boundaries |
| hashmap | Zero copies, but 450,000 array and 450,004 map unique-mutation helper events | Fresh timing attribution still needed | Remove repeated prepare/borrow/publish work on proven unique paths |
| cube3d | Repeated returned 3/4/16-element arrays | 31.0% fill; 15.1% admission; 13.9% GC | Construct admitted storage directly and prove temporary lifetimes |
| base64 | 333,406 appends; 332,400 in-place; 2,966,183 copied bytes | 48.6% under `fn_strcat_many`; <1% GC | Reduce per-piece and per-call work in existing string append paths |
| hyphen, old typed entry | Dynamic JSON trie/core shared with untyped entry | Whole-entry: 30.9% member lookup, ~16% map-set paths, ~8% `fn_lt` | A real typed port, then residual string/array boundary work |

The sample percentages are inclusive, can overlap, and are not additive.
Repeated-entry probes change heap history; Hyphen includes parsing and
verification outside its benchmark timer. Use these results to select a
mechanism, then re-profile on the actual candidate before assigning exact
timed-kernel shares. Raw data, binary identity and scripts are retained in
`temp/tune31_analysis/`.

Text rows account for 79.3% of summed typed execution medians after Phase I.
This is a workload-priority view, not an application mix or a suite geomean.
Profile typed log_pipeline, three_way_merge, text_search and prettier_ast
individually; do not transfer Hyphen/base64's percentages to those kernels.

C2MIR remains an independent native C port. Splay uses pointer mutation,
cube3d uses caller-owned buffers, and microdiff uses fixed-capacity result
storage. These differences matter under **S9.1.2–S9.1.3** and **D4.1.4v4**.
Report the remaining gap as semantic/representation work plus removable
compiler/runtime work; a common checksum does not establish identical
memory operations. The R47 3.59x typed/C geomean is historical context,
not a measured Phase II ratio.

### 6.2 Typed Hyphen rewrite — implemented source track

The old [hyphen2.ls](../../test/benchmark/text/hyphen2.ls) only called the
dynamic core. The typed entry now calls
[hyphen_typed.ls](../../lambda/benchmark/hyphen_typed.ls), with these choices:

1. **Flat typed trie tables.** Node first/count/level, edge code/child,
   level offset/count/value and exception marker arrays are `int[]`, held
   in a declared `HyphenTables` record. The existing
   [fixture generator](../../test/benchmark/text/generate_hyphen_fixture.js)
   derives them from the same library data as the JSON and C header.
   No patterns, cases or expected results are substituted for computation.
2. **Admission once at setup.** Generated `hyphen_tables.json` is loaded
   into the declared record before verification/timing. Its generated
   [table module](../../lambda/benchmark/hyphen_tables.ls) defines the
   explicit contract and loader. This uses **D3.3.3v3**'s declared
   representation boundary; it does not attach a certificate to an inferred
   open array. An initial static-Lambda-literal prototype added about two
   seconds of JIT startup and was replaced by this setup-time load. The hot
   traversal still uses typed arrays, while data size stays out of emitted
   initialization code.
3. **Typed caches.** String keys and string results use `string[]`;
   marker results use `int[][]`. A shared typed linear key lookup replaces
   dynamic map access, following the C reference's lookup strategy. Cache
   storage grows normally, without fixture-specific capacity assumptions.
   Writers take `var` cache parameters under **S9.1.3**; stored marker
   arrays retain ordinary snapshot/capture behavior.
4. **Strings throughout.** Input, words, cache values and output remain
   `string`. Use indexed characters, `slice`, and the existing owned-string
   append lowering. Append whole spans between hyphen positions and copy
   unchanged markup/punctuation as spans. There is **no integer/byte output
   buffer and no final decoding step**. Virtual leading/trailing dots avoid
   allocating padded word strings. Preserve **S7.1.1v3–S7.1.2** indexing
   and slice semantics and all ordinary alias/snapshot behavior.
5. **Shared oracle and lexical helpers.**
   [hyphen_common.ls](../../lambda/benchmark/hyphen_common.ls) owns the
   unchanged 13 input/expected pairs and shared character/tag predicates.
   Both cores import it; the untyped core retains dynamic trie traversal.
   Both retain 32 rounds, fresh caches each round, exact output verification,
   and checksum **1183296**. Fixture loading and admission precede the timer;
   all scanning, cache population and result-string construction remain timed.

This is a **benchmark source/representation improvement**, not an engine
gain. Measure it on one immutable release against an archived copy of the
original core. For later engine A/B, run the rewritten source on both
binaries and also retain the old dynamic port as a guard. Re-measure C2MIR
with its pinned driver before publishing a new Lambda/C ratio.

The generator's pre-existing source marker was stale (`hyphen_texts = [`
became `hyphen_cases = [`); extraction now stops at the current case table.
Regeneration leaves the existing JSON and C header unchanged.

Correctness is pinned by
[`tune31_hyphen_typed.ls`](../../test/lambda/proc/tune31_hyphen_typed.ls)
and its expected `.txt`: all 13 exact outputs, empty text, Unicode
preservation, markup attributes, an unterminated tag and existing hyphens.
The canonical benchmark separately exercises cache reuse and reset.

### 6.3 Engine tracks — evidence-gated

#### A. Carry ownership and path facts through valid regions — implemented for typed var field borrows

Start with typed splay/havlak/deltablue, then hashmap. Attribute executed
copies and helper events to the actual source operations; separate real
two-observer snapshots from facts lost at joins, nested handles and calls.
Extend the existing shared analysis for eligible nested/recursive
store-backs and reuse identity/layout/uniqueness until an invalidating
write, escape or replacement. **D4.4.4v4** supplies the optimization rule;
**D4.4.6** and **S9.1.2–S9.1.3** preserve observable snapshots.

For a unique owner, remove redundant preparation/path navigation only when
the same proof also preserves replacement publication and `var` home
transport. Reload movable data buffers after allocation as required by
**D4.3.1**. Do not infer that zero copies means zero ownership cost.

**Gate:** a paired gain plus fewer executed copies/bytes or unique-owner
helper events on the identified path. Pin positive cases and invalidation
cases: live aliases, shared children, recursive replacement, branch joins,
zero iterations and error exits. A shorter static MIR listing alone fails
this gate. Historical splay regression attribution requires a separate
matched-revision replay; the Phase I control/final comparison did not bisect it.

The first safe instance is the typed var record path used by hashmap:
the direct caller has already detached an unmarked root before the callee
starts, so a later var borrow of record.field need not call
cow_prepare_write on that same root again. The child path walker remains
unchanged and still prepares the selected field. A capture, alias, retained
argument or other body-side share sets cow_marked in emission order, so the
next path borrow restores root preparation and write-back. This is the
existing ownership fact, narrowed to **S9.1.2**, **S9.2.2** and
**D4.4.4v4**; it introduces no new uniqueness inference.

#### B. Construct typed arrays in their final representation

Use cube3d's matrix/vector producers, with storage/brainfuck as guards.
Avoid an intermediate open allocation followed by conversion/admission
where the declared destination contract proves the final representation.
Reuse existing fill, constructor and certificate machinery, retaining
count/value/error behavior, null/poison and floating signed zero.

Then assess scalar replacement or caller-provided result storage where
lifetimes prove it safe. A non-escaping allocation can still be live across
the next iteration; **D4.1.4v4, D5.2** require an actual lifetime/rooting
argument. Any new lifetime/return ABI needs its own design before code.

**Gate:** reduced executed allocation/admission work and paired typed gains;
cover returned values kept across iterations, aliases, exceptions and GC.
Do not require `fn_fill` to disappear when an efficient direct typed fill
is the correct remaining operation.

#### C. Reduce string and collection helper boundaries

Re-profile base64 and rewritten Hyphen first. Base64 already reuses its
string storage, so target append argument preparation, repeated validation,
small-piece handling and avoidable calls through the existing shared helper.
For proven string/index cases, preserve native character/comparison facts
through consumers, retaining UTF-8 and out-of-range behavior. Hyphen's
span-based string construction is the source-level model; do not introduce
an integer output buffer or another parallel builder subsystem.

For the high-total-time text rows, select changes from fresh profiles of
traversal, equality, collection construction, copied bytes and runtime
calls. Static shape and representation proofs must preserve dynamic misses;
**D8.4.1v2** excludes feedback-driven mutable inline caches.

**Gate:** helper/copy/allocation reductions on executed paths and matching
outputs. Cover ASCII and multibyte strings, empty slices, string aliases,
builder escape/freeze, cache misses/hits and repeated cache resets. Preserve
the existing string-append semantics under **S9.1.2**.

#### D. Remove remaining scalar and call boundaries

Revisit typed bounce, crypto_sha1, levenshtein and towers after A–C. Select
actual hot abs/bitwise/character/conversion or wrapper operations. Extend
the existing representation-aware lowering and leaf inliner only when the
numeric, nullable and error domains are proven (**S4.1.1–S4.1.5,
D2.5.1–D2.5.3, D2.8.1–D2.8.3**).

Root traffic is a multiplier on those calls: eliminating a boundary can
remove boxing, dirty-live-root publication and layout reloads together.
Use the shared emitter under **D5.3.1–D5.3.4**; a `NO_GC` classification
requires transitive mechanical verification. Keep collecting setter and
admission paths rooted, and preserve the existing defect/error channel.

**Gate:** the hot helper/wrapper boundary disappears, with positive/negative
MIR pins, semantic parity and forced-GC coverage; no blanket NO_GC allowlist.

#### E. Native code structure after helper costs fall — re-profiled, no retained change

Re-profile typed fft/quicksort/nbody with machine-code samples and spill
counts. Improve loop-local liveness, redundant carrier temporaries and
fallback placement only when the executed code demonstrates the cost.
Use the existing MIR Direct backend; no vendor edits or C-text backend.
T30's old spill percentages are hypotheses, not new evidence.

**Gate:** fewer executed spills/reloads or a shorter measured hot path,
with paired gains and stable compilation/end-to-end costs. Do not trade
unbounded loop/version duplication for a static instruction-count claim.

#### F. Carry forward Phase I's inferred-store omission — implemented

Untyped nbody still performs 2.7M unique mutation helpers with zero copies,
and its final time is 8.7x its typed twin. Its counter fix did not satisfy
T31-1's native-store objective. When improving shared store lowering, carry
the inferred float lane into a guarded direct write with the existing
widening/error/COW fallback (**D3.3.3v3, S7.1.3v2**). Keep this as an
explicit secondary deliverable; its improvement cannot substitute for
Phase II's typed goals.

**Gate:** remove the repeated mutation helper on the admitted path, retain
mixed/widened/null/out-of-bounds/shared fallbacks, and pin stores as well as
the previously pinned counter arithmetic.

### 6.4 Measurement and acceptance for Phase II

1. Archive an implementation-start release and manifest before engine work.
   Record source/dependency and data hashes, binary SHA, tier, outputs and
   raw pairs. Preserve source-only Hyphen evidence separately from engine
   A/B; freeze both old and rewritten ports in the engine comparison.
2. Use seven/nine pairs for discovery and 41 for claimed gains or flagged
   regressions, following §4.1. Measure execution and auto process-wall time
   separately. Collect profiles/counters outside timing runs. Compare the
   unchanged 63-row population on both sides; expose the Hyphen source
   change as its own column/experiment rather than folding it into an
   engine geomean.
3. Retain the typed-suite **≤0.90 candidate/control geomean as a planning
   objective**, with no reproducible >3% regression under §4.2's confidence
   gate. This is a target, not a predicted result. Require confirmed gains
   in ownership, construction or text families beyond pnpoly; report every
   missed objective explicitly. Treat untyped as a full-suite guard.
4. Every shipped engine mechanism gets an executed optimization assertion
   or a focused MIR sidecar, including fallback/invalidation negatives.
   Pair those with expected-output fixtures and interp/JIT/auto parity.
   Exercise ownership/representation changes with forced GC, poisoning and
   the root witness. Keep `.txt` goldens for every new test script.
5. Run the applicable baseline/MIR suites for engine changes and the normal
   release publication gates for a new benchmark snapshot. Re-establish
   any baseline blocker on the actual control; Phase I's old blocker is
   not a standing exemption. Source-only Hyphen validation is recorded
   independently and does not claim a new engine baseline run.

### 6.5 Phase II implementation evidence

The typed Hyphen port has passed its benchmark golden and the new fixture
on interp, JIT and auto; the untyped benchmark also passes all three tiers.
The fixture also passes all three tiers with forced GC, freed-memory
poisoning and the root witness. The targeted LambdaOptStrings suite passes
4/4, including the new
`TypedHyphenUsesStringSpansAndOneTableAdmission` optimization test. That
test bounds appends below 15,000 and table-record admissions at two or
fewer; the observed candidate is **11,092 appends and one admission**.
The original core's **71,086 appends** exceeds the budget, so reverting to
character-by-character output is observable without timing assertions.
Generator replays are byte-identical, and every generated node, edge,
level and exception agrees with the existing fixture.

The archived-source comparison uses one immutable release and preserves the
original core as its control. Across 41 alternating pairs, typed Hyphen moves
from **57.908 to 11.376 ms** on JIT (**0.1964×**, upper bound **0.2029×**;
**41/41** wins) and from **59.550 to 12.727 ms** on auto (**0.2137×**,
upper bound **0.2256×**; **41/41** wins), with the checksum equal on every
pair. The matching untyped entry is **0.9809×** JIT and **0.9905×** auto,
within its paired uncertainty. The gain is consequently attributable to the
typed table/cache/span representation rather than a changed oracle or shared
benchmark harness.

**F — inferred native float stores.** The generic indexed-store lowering had
two independent losses on AWFY nbody. First, an inferred parameter's AST
carrier remained 'any' even when its live MirVarEntry held an admitted
ArrayNum; it therefore could not enter the existing guarded direct-store
emitter. Second, nbody's unannotated delta, distance and mag temporaries
broke the nullable-F64 producer proof, even though their unmodified
initializers and native integer loop indices preserved that lane.

The store now uses a direct identifier's live admitted ArrayNum witness only
to select the successful representation arm. It does not fabricate a source
T[] contract: COW, view, element-kind, null, bounds, widening and error
misses still reach the existing checked setters. The nullable-F64 proof
follows an unmodified local initializer and recognizes an integer carrier for
an inferred index. This is the representation-scoped rule in **D3.3.3v3**,
with the hard-write boundary in **S7.1.3v2** and var write-back in
**S9.1.3**.

The release COW_EXEC_PROFILE census on the unchanged AWFY source records
**2,700,000** array[num] unique mutations in the archived
lambda_tune31_final_release control and no array[num] mutation row in the
Phase II candidate. The checksums/output match. The 41-pair, alternating JIT
run has control/candidate medians **27.292 / 5.475 ms** (**0.2006×**;
one-sided paired-bootstrap upper bound **0.2025×**; **41/41** candidate
wins). The nine-pair auto run is **45.682 / 32.378 ms** (**0.7088×**, upper
bound **0.7254×**; **9/9** wins). These are the secondary untyped-F results
required by §6.3 F; they are intentionally not counted as a typed-suite
result.

Three fixtures make the optimization durable:

1. tune31_inferred_float_store pins a repeatedly written inferred ArrayNum
   with a live snapshot.
2. tune31_inferred_var_float_store models the nbody shape: a caller-visible
   inferred var array, dynamic indices and nullable floating intermediates.
   Its COW assertion observes one shared detach and zero unique-mutation
   helpers.
3. tune31_inferred_float_store_fallback proves that a null assignment stays
   on fn_array_set and an out-of-range native float assignment reaches
   array_num_set_cow_idx, with matching interpreter/JIT/auto results.

Each fixture has a .txt golden and a focused MIR sidecar. The Tier parity,
MIR emission, COW-counter and forced-GC suites pass for all three. This pins
the native success arm and the null/out-of-bounds/shared fallback arms
without a timing assertion.

**A — exclusive typed var field borrows.** mir_emit_cow_path_borrow
previously called cow_prepare_write for every nested var argument,
including an unmarked record parameter which had already been detached by
its caller. Hashmap makes five such field borrows for every insertion. The
emitter now skips only that repeated root preparation for an unmarked
is_var_param; cow_path_borrow_fixed still prepares and relinks the child
array. A body-side capture retains cow_marked, therefore preserves the
root preparation before the next borrow. This retains caller-home
publication, snapshot isolation and all shared-child handling under
**S9.1.2**, **S9.2.2** and **D4.4.4v4**.

The release COW census on unchanged typed hashmap2 source records the
pre-A Phase II binary's **450,004** map unique-mutation preparations with
zero map copies. The A candidate records **4** map preparations, also with
zero map copies; its 450,000 ArrayNum child preparations remain because
each selected array must still test its own ownership state. The four map
events are the direct HashMap field assignments, not the removed nested
borrow path.

The isolated release A/B uses lambda-phase2-final as control and
lambda-phase2-var-path-release as candidate, with the same hashmap2
source. Across 121 alternating JIT pairs it moves from **38.950 to
37.653 ms** (**0.9667×**, one-sided paired-bootstrap upper bound
**0.9878×**; **95/121** wins), with equal output in every pair. The
nine-pair auto discovery replay is 0.9630× but its 1.0303 upper bound is not
claimed as an auto result. A separate comparison to the older Phase I
release is directionally similar but has a 1.0163 upper bound, so it is not
used to attribute this A-only change.

Four focused tests make the ownership claim durable:

1. tune31_var_path_borrow verifies interp/JIT/auto snapshot isolation
   through repeated typed record-field borrows.
2. Its MIR sidecar requires cow_path_borrow_fixed and forbids a
   root cow_prepare_write inside the unmarked callee loop.
3. tune31_var_path_reborrow_shared takes a body-side snapshot, verifies
   the caller sees the detached update, and requires root preparation before
   the field borrow in its MIR sidecar.
4. LambdaOptCow.VarPathBorrowAvoidsRepeatedUniqueRootPreparation pins
   one root copy, zero unique map preparations and one child ArrayNum copy
   on the positive case.

The targeted Lambda tier-parity, MIR-emission and COW-profile tests pass.
Final post-implementation validation with make test-lambda-baseline passes
**5,707/5,707** tests: **2,104/2,104** input-parser and **3,603/3,603**
Lambda-runtime tests. This clears the historical Phase I batch observation
without a test-harness exemption.

**B experiment rejected.** A source-only cube3d experiment replaced
zero-filled four-element vectors that were immediately overwritten with
typed literals, including its line-drawn flag array. It preserved output but
regressed: the 41-pair JIT source comparison was **1.0336×** (five candidate
wins) and the nine-pair auto comparison was **1.0634×** (zero wins). The
candidate source was removed. This demonstrates that its existing fill path
is cheaper than per-member literal admission for this workload, so Phase II
does not retain an aesthetic construction rewrite in place of a paired gain.
The control/candidate scripts and raw pairs remain under
temp/tune31_phase2/cube3d_source/ and
temp/tune31_phase2/paired_cube3d_source_*.json.

**C closure — base64 already reaches the shared-builder shape.** The forced-JIT
candidate profile has 333,406 string append calls carrying 1,333,506 pieces;
332,400 append in place, only 1,006 grow, and six generic joins remain. The
finalized MIR has four static fn_strcat_many call sites: the hot encode site
passes the owned accumulator plus all four Base64 characters in one call.
Thus the source's four-character quantum is already one append boundary, not
four nested concatenations. No C change is retained. The later review in §8
identifies a generic remaining opportunity: specialize the existing builder's
known-arity, capacity-fit path. This does not require an encoding-specific
emitter or a second string builder; flattening alone did not establish that
the surviving append boundary was cheap.

**D closure — scalar boundaries require a domain proof.** Forced-JIT bounce
has no array COW events and completes its timed body in 0.149 ms, although
its four static abs sites still call the boxed helper. A typed indexed read
can yield the int-lane null sentinel, so replacing that call needs a
null/error-preserving integer abs lowering rather than a raw machine abs
instruction. Forced-JIT crypto_sha1 completes in 24.420 ms and retains
representative boxed shift and bitwise calls in its string/word conversion
paths. Their values cross the int, u32 and error domains; static call removal
does not prove the needed result carrier. The next candidate is a shared
module-constant/native-lane proof followed by executed helper attribution,
under **S4.1.1–S4.1.5** and **D2.4.1–D2.4.3**. Tune31 adds no D shortcut
without that proof and a paired result.

**E experiment rejected.** Typed fft initially snapshots its plain
four1 float-array parameter once: the forced-JIT profile records one
32,840-byte ArrayNum copy. Making the work-buffer parameter var removes the
copy and preserves output, but its caller-home transport costs more. On the
same immutable release, 121 alternating JIT source pairs move from 0.087 to
0.109 ms (1.2529x; one-sided paired-bootstrap upper bound 1.3448x; five
candidate wins), with equal output on every pair. The var source was removed.
The same forced-JIT census records no COW copies for typed quicksort2 or
awfy nbody2; their existing representation paths do not identify an E
candidate. This is the semantic/ABI distinction from C pointer mutation:
**S9.1.2–S9.1.3** requires an inout transport, and a removed copy alone is
not a performance win.

The Phase II artifacts are:

- temp/tune31_phase2/{candidate,final}.sha256 and
  lambda-phase2-{candidate,final}: candidate binary identities.
- paired_awfy_nbody_{jit_41,auto_9}.json: interleaved same-source engine
  comparison and paired uncertainty.
- awfy_nbody_{control,candidate}_profile.tsv: exact executed COW census.
- paired_hashmap2_var_path_isolated_jit_121.json: isolated A-only release
  comparison; paired_hashmap2_var_path_{jit_41,jit_121,auto_9}.json retain
  the discovery and historical-control replays.
- hashmap2_{pre_var_path_release,var_path_release}_cow.tsv: pre/post-A
  executed COW census; var_path_release.sha256 identifies the candidate.
- hyphen_typed_release.out: release checksum/timer confirmation.
- hyphen_source_{jit,auto}_41.json: archived-source typed and untyped Hyphen
  comparison, including the source/dependency hashes and output digests.
- paired_cube3d_source_{jit_41,auto_9}.json: retained negative source
  experiment, excluded from any gain.
- base642_jit.tsv and base642_jit.mir: C's forced-JIT shared-builder census
  and flattened concat evidence; bounce2_jit.tsv, crypto_sha12_jit.tsv and
  their MIR dumps retain D's executed probes.
- fft2_jit.tsv, quicksort2_jit.tsv and awfy_nbody2_jit.tsv: E's
  forced-JIT ownership census; paired_fft_var_buffer_jit_121.json retains
  its rejected inout source experiment.

Both sides of these **source-only** comparisons use the immutable Phase I
release `4e4107d9…c2148`; the control keeps the original core and inline
oracle/helpers. Every sample matches the canonical checksum:

| Variant / mode | Pairs | Execution median, before → after | Ratio / upper 95% | Process-wall median, before → after |
|---|---:|---:|---:|---:|
| Typed JIT | 41 | 57.908 → 11.376 ms | **0.1964 / 0.2029** | 102.163 → 65.774 ms |
| Typed auto | 41 | 59.550 → 12.727 ms | **0.2137 / 0.2256** | 107.918 → 70.548 ms |
| Untyped JIT shared-helper guard | 81 | 60.362 → 59.665 ms | 0.9885 / 1.0104 | recorded in raw pairs |
| Untyped auto shared-helper guard | 41 | 60.907 → 60.327 ms | 0.9905 / 1.0222 | 104.375 → 106.080 ms |

Typed wins all 41 pairs in both modes: about **5.1x faster JIT execution**
and 4.7x faster auto execution. Median process-wall ratios are 0.6438 and
0.6537 respectively; these are separate from the execution confidence
bounds in the table. The initial 41-pair untyped JIT upper bound was 1.0344,
so the longer 81-pair replay establishes the execution non-regression gate.

Artifacts under `temp/tune31_phase2/`:

- `source_manifest.json`, `control/`, `data_hashes.json`;
- `hyphen_source_jit_41.json`, `hyphen_source_auto_41.json`,
  `hyphen_untyped_jit_81.json`;
- `checks.json`, `gc_checks.json`, `opt_strings.log`;
- `hyphen_typed_cow.tsv`, `hyphen_original_cow.tsv` and corresponding output.

The Hyphen source comparison does not claim an engine result. The separate
inferred-store and exclusive-field-borrow implementations are the Phase II F
and A engine results; B and E are rejected by paired results, while C and D
close without a safe, measured generic change.

## 7. Follow-on typed source ports (2026-09-21)

These benchmark-source rewrites apply the same representation discipline as
the typed Hyphen port. They do not change the runtime and must not be folded
into a Tune31 engine geomean. **D3.3.3v3** permits an explicit source
representation to retain its established carrier; **S7.1.1v3–S7.1.2** keeps
string indexing and slicing semantic rather than requiring a byte-output buffer.
Each port keeps its external text output and canonical oracle.

| Typed source | Rewritten hot representation | JIT control → candidate | Ratio, upper 95% | Pairs |
|---|---|---:|---:|---:|
| `log_pipeline2.ls` | direct string spans and scalar aggregate state instead of a `LogRecord` map per row | 3410.830 → 1778.870 ms | 0.5215, 0.5259 | 41/41 wins |
| `knucleotide2.ls` | fixed 20-lane A/C/G/T one- and two-mer table; longer requested literals remain string scans | 4.258 → 0.647 ms | 0.1519, 0.1557 | 81/81 wins |
| `fast_diff2.ls` | fixed source strings admitted once as `int[]` code tables before the repeated LCS loop | 124.564 → 48.754 ms | 0.3914, 0.4050 | 41/41 wins |

The changes imitate the useful part of the C2MIR ports: the repeated work
uses its closed, native-friendly representation. They do not claim an
identical general-purpose parser or container API. Log Pipeline still scans
all generated fields and checks its scalar schema; Knucleotide emits the same
formatted strings and performs its longer queries as strings; Fast Diff
converts the static fixture before timing and retains string output. None uses
an integer output buffer.

The source rewrite pins are deliberately mechanism-specific:

- `LambdaOptStrings.TypedLogPipelineStreamsWithoutRecordMapTraffic` checks
  the checksum and zero map admissions/mutations.
- `LambdaOptStrings.TypedKnucleotideUsesClosedAlphabetTables` compares the
  normalized full golden output and requires the closed 20-slot table.
- `LambdaOptStrings.TypedFastDiffPrecodesStaticTextBeforeLcsLoop` checks the
  checksum and the static `string → int[]` admission boundary.
- `tune31_typed_text_code_loop` has its `.txt` golden and MIR sidecar. The
  sidecar requires raw integer equality in the repeated loop and forbids both
  `fn_string_ascii_at` and `fn_eq` there.

The three Lambda optimization tests and the new MIR fixture pass after
`make build-test`. Knucleotide and Fast Diff also pass JIT forced collection,
freed-memory poisoning and the root witness; Fast Diff passes interp and auto
with the canonical checksum. The archived release evidence is under
`temp/tune31_phase3/`: `paired_log_pipeline_typed_stream_jit_41.json`,
`paired_knucleotide_typed_table_jit_81.json`,
`paired_fast_diff_code_tables_jit_41.json`, the matching `profiles/` TSVs,
and the Fast Diff `mir/` dumps.

## 8. Typed engine review after Phase II (2026-09-21)

**Status:** diagnostic review informing the Phase III proposal in §9; no new
engine implementation or paired speedup is claimed here. Sources are the tree at
`a6f14956e`; new probes use the immutable final Phase II release
`temp/tune31_phase2/lambda-phase2-var-path-release`, SHA-256
`2b875a393b51be8ef93ec073ac6f4851615250c3db9ced4632af729d57e260ec`.
No formal ruling changes.

### 8.1 Why typed execution has plateaued

Phase I's typed same-source suite ratio is **0.9778**, or 2.22% less execution
time. Excluding pnpoly it is **0.9962**, or only 0.38% less. Its largest
untyped changes recover native counters, array witnesses and producer/return
facts that the typed entries already possessed. The typed 41-pair ratios for
quicksort and text_search are **0.9967** and **1.0005**, respectively; the
large untyped improvements do not describe these typed kernels.

Phase II's confirmed typed engine improvement is the isolated hashmap
root-preparation change: **38.950 → 37.653 ms**, ratio **0.9667**, upper 95%
**0.9878**. The roughly fivefold nbody inferred-store gain is untyped. Hyphen
and §7's three source ports change the workload's internal representation and
are reported separately. B and E retained no engine implementation, and C/D
closed without one. Phase II therefore did not yet deliver the broad typed
ownership, construction and helper removal originally envisaged.

There is no matched full-suite Phase II engine replay from which to calculate
a measured combined Phase I + II geomean. Such a replay is possible by keeping
sources fixed across binaries; the source rewrites do not make it impossible.
For scale only, a 3.33% gain in one of 63 equally weighted rows contributes
about **0.054%** to the suite geomean if all other rows are unchanged. Ten
independent twofold row gains would lower it by about 10.4%. Broadly reused
mechanisms are needed to meet the original typed-suite objective.

An annotation establishes a type/carrier contract, not automatically a proof
of unique ownership, stable field location, valid index range, finite numeric
values or temporary lifetime. Existing typed arithmetic and packed arrays
already remove much dynamic dispatch. The remaining cost lies at the places
where those additional facts are unavailable or lost across a call, borrow,
store or loop (**D3.3.3v3–D3.3.4**, **D4.4.4v4**, **D5.3.1–D5.3.4**).

### 8.2 Fresh executed evidence

Eleven canonical forced-JIT runs completed with matching output/oracles.
Eight separate repeated-entry CPU probes sampled the final Phase II release
at 1 ms intervals for three seconds each. All sampled source bodies are
unchanged from Phase I. The two rewritten sources included in the canonical
census, log_pipeline2 and fast_diff2, are not used to attribute an engine gain.

The counts below cover the complete canonical script, including setup. CPU
shares are approximate diagnostic samples, not isolated benchmark-timer
shares or paired timing results. Repeated entry changes heap history, some
internal symbols are unresolved, and inclusive shares overlap; do not add
them or turn them directly into promised speedups.

| Typed row | Final Phase II evidence | Remaining target |
|---|---|---|
| hashmap | Zero copies; 4 root-map and 450,000 child-array preparations. `cow_prepare_write` ~1.3%, `fn_index` ~26.2%, type checking ~16.5% | Typed field-borrow traversal and repeated array admission, rather than more root share-bit tuning |
| prettier_ast | 6,615,040 union admissions, with 6,615,034 union-map representation cache hits. Member access ~32.4%, type checking ~19.8% | Eliminate proven redundant boundary calls and specialize known field layouts |
| base64 | 333,406 append calls; 332,400 reuse capacity. `fn_strcat_many` ~49.3%, including ~45.8% self; GC <1% | Existing append helper's per-call/per-piece cost |
| cube3d | `fn_fill` ~30.7%, type checking ~15.2%, GC ~13.7%; no COW copies | Small-array construction, admission and result lifetime |
| splay | 951,168 map copies, 61,825,920 copied bytes; write preparation ~35.6%, GC ~29.7% | Avoidable capture/detach and repeated typed path operations |
| havlak | 65,876 map + 44,999 array copies, 7,502,280 bytes; write preparation ~23.8%, GC ~21.6%, type checking ~15.5% | Ownership/path facts and contract propagation |
| text_search | ~82.8% JIT self, ~0.1% type checking; raw lane reads still have repeated bounds/sentinel checks in MIR | Native loop range and value-domain proofs |
| three_way_merge | 20,803,304 unique array preparations but zero copies; preparation ~1.7%, split ~39.4%, array_push ~19.5% | Split/result construction, rather than COW based on call count alone |

Splay and havlak's copy counts are unchanged from the Phase I census.
Deltablue also retains **38,980 ArrayNum copies / 3,409,760 bytes**. These
are actual copies, unlike hashmap's cheap uniqueness tests. Not every copy
is removable: the compiler must establish that the old value has no observer
before suppressing a snapshot (**S9.1.2–S9.1.3**, **D4.4.6**).

The independent C2MIR ports commonly have fixed field offsets, direct array
loads/stores and caller-owned buffers. Lambda still traverses generic paths,
re-establishes contracts and constructs GC-managed results in these cases.
Splay additionally uses a different rotation/mutation organization. Historical
Result47 typed/C ratios identify the scale, not a newly measured Phase II gap:
hashmap and cube3d about 13x, base64 15x, splay 17x, havlak 33x, and text_search
3.2x. Three-way merge was already about 1.06x. Equal results do not imply equal
memory workloads, and these ratios are not all removable compiler overhead.

### 8.3 Proposed next engine work, in priority order

**1. Carry typed field and admission facts through a borrow/call.** Start
with hashmap, then the union-record consumers in prettier/havlak. The current
`mir_emit_cow_path_borrow` avoids the redundant root preparation, but
`cow_path_borrow_impl` still calls `fn_index` for each path link and prepares
the child. Extend the existing typed field plan to load a proven field lane
directly, test the child's current carrier/ownership and retain the shared
detach/relink fallback. Preserve the admitted array contract into the callee
when the proof survives the call boundary. This targets the measured lookup
and admission costs that Phase II A left intact.

Prettier's almost perfect cache-hit rate demonstrates why adding another
cache is not the solution: millions of successful runtime transitions still
cost time. Extend existing `mir_member_read_proves_contract` /
`mir_call_result_proves_contract` facts to justified union edges. For field
access use a proved common layout or a static shape guard with fallback.
Its current `kind: string` fields are not literal discriminants, so a
`kind == "text"` test alone must not be treated as proof of a record shape.
Respect current-carrier certificates, invalidation by mutation/resharing,
caller-home writeback, and pointer reloads after allocation
(**D3.3.3v3**, **D4.4.4v4**, **D5.3.1–D5.3.4**). Use no call-site inline cache
(**D8.4.1v2**).

**2. Remove generic work from the existing string fast paths.** Base64's
concat tree is already flattened and its accumulator normally has capacity;
there is no evidence for a new builder or integer output buffer. The current
`fn_strcat_many` still materializes varargs and uses the variable-count
`string_buffer_join<0>` path. Specialize known arity through the shared helper
template, then evaluate a compiler-emitted capacity-fit path when lengths,
ownership and overlap are proved. Keep allocation/growth/freezing on the
existing slow path. A successful nonallocating branch can avoid the helper
transition and its safepoint traffic; the allocating branch retains precise
roots (**D5.3.1–D5.3.4**).

Also fuse scalar string consumers such as `ord(s[i])` and `s[i] == t[j]`.
Literal-character equality already has an allocation-free helper, but two
indexed characters can still cross two string helpers and generic equality.
Use guarded ASCII byte loads where valid and retain Unicode, null and bounds
semantics (**S7.1.1v3**, **D3.3.4**). This lets ordinary string source recover
part of the benefit obtained manually by Fast Diff's code-table rewrite.
Measure on the archived original source as well as source-port guards. Module
string-table lanes must be validated in auto/T0 as well as forced JIT: the
current module-slot carrier is not interchangeable with a packed pointer lane
(**D3.3.3v3**).

**3. Add relational range proofs to already-native loops.** Text search is
the strongest freshly sampled candidate. Its `naive_search` MIR has raw
integer loads yet repeats index guards, int53 arithmetic checks and nullable
element handling in the inner loop. Prove relationships such as
`0 <= position <= n - m`, `0 <= offset < m`, and hence
`position + offset < n`; carry invariant lengths only while the arrays are
stable. Eliminate redundant read guards and checked counter arithmetic where
these proofs suffice. Range safety and element-value safety are separate:
`int[]` does not establish that every element is finite or in 0..255. Removing
element sentinel handling needs a producer/domain proof or a guarded fallback
(**S4.1.1–S4.1.5**, **S4.2.3**, **S7.1.1v3**, **D3.3.4**).

Reuse the analysis for quicksort and the rewritten Fast Diff numeric loop,
then measure each unchanged source. Inspect native instructions before
attributing any residual cost to register spills or MIR register allocation;
a large MIR dump alone is not evidence for either.

**4. Fuse small-array production with its established contract.** Cube3d
already receives packed numeric storage from fill and has
`mir_fill_proves_nonempty_numeric_array_contract` plus a narrow numeric
admission helper. This is not an unimplemented packed-array path. Remove
remaining redundant producer/consumer checks by constructing storage with
its valid contract and propagating that fact through returns. Next consider
destination construction or scalar replacement for the returned 3/4/16-lane
temporaries, only with an escape/lifetime and full-overwrite proof. C's reused
buffers explain the opportunity, but neither deleting initialization nor
turning every result into an inout parameter is justified
(**D3.3.3v3**, **D4.1.4v4**, **D5.2**, **S9.1.2–S9.1.3**). The rejected
literal and FFT-var experiments already show that fewer copies or more
explicit source can still cost more.

**5. Extend ownership/liveness analysis where actual copies dominate.**
Splay, havlak and deltablue offer a larger structural opportunity but require
more analysis than skipping an extra unique-root check. Attribute remaining
share marks to their binding/call sites and establish which old values are
dead before a rotation or nested update. Extend the existing synthesized-place
and move/store-back analysis for those proved cases, sharing the static
decision between tiers. Retain fallback copies for observed snapshots and
invalidate borrowed facts at conflicting writes, escaping captures and
resharing (**S9.1.2–S9.1.3**, **D4.4.4v4–D4.4.6**). Reduce allocation demand
before proposing GC tuning; a GC percentage alone does not identify a collector
defect.

### 8.4 Evidence gates for the follow-up

Each candidate needs an unchanged-source release A/B, an executed-cost change
and a semantic/optimization pin. Useful pins are mandatory loop-helper
absence on a proved path, bounded admission counts, reduced attributable
copy/allocation counts, and preservation of the slow path. Cover shared-child
detachment and reborrowing, contract invalidation, null/out-of-range reads,
Unicode strings, growth/aliasing and forced precise GC as applicable. Do not
substitute static instruction reduction or cache hits for a measured gain.

Use short alternating pairs for discovery and longer confirmation when the
observed variance requires it; 41 is not a semantic requirement. After the
accepted candidates, replay all 63 typed rows with fixed sources against both
the Phase I control and final release, retaining untyped regression guards.
Report the measured engine geomeans, source-port gains, JIT execution and
auto/process-wall results separately. No combined phase result is inferred
from the isolated hashmap experiment.

Reproducibility artifacts are under `temp/tune31_typed_review/`:
`canonical_profiles.json` retains all eleven canonical runs and counters;
`profiles.json` and `profiles_text.json` retain the release/source/wrapper
hashes for eight successful samples; `*.sample.txt`, `*.tsv` and `*.mir`
retain the raw evidence. `probe.py`, `sample_current.py` and `summary.json`
retain the diagnostic procedure and extracted attribution.

## 9. Phase III — Typed engine optimization proposal

**Added:** 2026-09-21. **Status:** COMPLETE; B1, B2 and A1 are implemented
and independently confirmed below; A2, C and D1 were rejected by measurement,
and D2 and E are deferred with their proof reasons recorded in §9.10. The
full-suite typed objective was not met, as recorded in §9.11.
The diagnostic baseline is §8, and the following tracks elaborate §8.3.
This phase targets engine gains on unchanged typed sources. §7's completed
source ports remain separate results, even though their existing artifact
directory is named `temp/tune31_phase3/`. New Phase III engine artifacts go
under `temp/tune31_phase3_engine/` to keep those experiments distinguishable.

**Objective:** remove repeated generic operations where an existing typed
contract plus a valid ownership, layout, range or lifetime proof can establish
the result. Target a **≤0.90 typed execution geomean against the Phase III
start release**, with confirmed improvements in multiple workload families
and no confirmed >3% unchanged-row regression. This is a planning objective,
not a prediction from inclusive CPU percentages. Report a missed objective
explicitly; individual pilot wins do not satisfy the suite objective.

### 9.1 Scope, order and common implementation rules

| Track | First implementation slice | Primary unchanged pilots | Required mechanism evidence |
|---|---|---|---|
| A1 | Direct typed field-borrow path | hashmap | Generic lookup absent on the proved path; shared-child detach still correct |
| A2 | Contract propagation through calls and union fields | hashmap, prettier_ast, havlak | Repeated admission calls decline, not merely cache misses |
| B1 | Known-arity and capacity-fit string append | base64 | Lower append-path work; copied bytes and growth remain bounded |
| B2 | Fused scalar string consumers | archived pre-rewrite fast_diff, current string workloads | Eligible character reads avoid temporary string/helper chains |
| C | Relational loop bounds and separate numeric-domain proofs | text_search, quicksort, current fast_diff | Redundant checks disappear inside the proved loop region |
| D1 | Typed numeric producer/admission fusion | cube3d | Fewer admission boundaries without extra construction/copies |
| D2 | Destination construction or scalar replacement | cube3d | Fewer temporary allocations, with escape and initialization proofs |
| E | Broader place/liveness analysis | splay, havlak, deltablue | Attributable share marks, actual copies and copied bytes decline |

Implement A1, B1 and C's bounds slice first: they address directly observed
costs with relatively bounded proof requirements. Follow with A2, B2 and D1.
Begin E's attribution early, but implement its ownership changes only after
identifying a specific removable capture pattern. D2 depends on D1's result
and an explicit lifetime proof. Each slice has its own release comparison;
do not combine unmeasured changes and then infer which one helped.

Extend the existing semantic and MIR analysis facilities. In particular:

- Use current typed field/path plans, contract proof helpers, interval facts
  and `MirEmitter` events. Do not create a parallel type authority or another
  builder/ownership subsystem. Extract shared logic before adding another
  near-identical lane or arity case.
- Keep semantic contract, physical carrier, nullability, index validity,
  element domain and uniqueness as distinct facts. **D3.3.3v3** requires the
  certificate to match the current carrier; **D3.3.4** keeps an unproved read
  nullable. An annotation or a previous cache hit cannot substitute for a
  missing proof.
- Record a fact's origin, scope and invalidation conditions. Merge only facts
  valid on every incoming path. Rebinding, prefix mutation, representation
  change, escaping aliases and writer calls invalidate affected facts under
  **D4.4.4v4**. Reload data pointers after possible allocation.
- Preserve evaluation order, observable snapshots, inout publication and
  error behavior. **S9.1.2–S9.1.3**, **S7.1.1v3** and **S7.1.3v2** continue
  to govern optimized and fallback paths.
- Rooting remains owned by `MirEmitter` and precise runtime frames. Remove
  unnecessary allocating boundaries rather than deleting required root
  publication. A `NO_GC` helper requires the transitive verification specified
  by **D5.3.2**; all allocating fallbacks retain **D5.3.1–D5.3.4**.
- Use compile-time facts and static guards, not runtime call-site feedback or
  inline caches (**D8.4.1v2**). Work stays in Lambda's MIR Direct/runtime code;
  this proposal requires no MIR vendor changes or C-text backend.

### 9.2 A — Typed field borrows and admission propagation

**Starting points:** `mir_emit_cow_path_borrow`, the existing direct field
load/store plans and `mir_member_read_proves_contract` /
`mir_call_result_proves_contract` in `lambda/runtime/transpile-mir.cpp`;
`cow_path_borrow_impl` and contract admission in
`lambda/runtime/lambda-eval.cpp`. Existing reuse and union tests in
`test/test_lambda_opt_gtest.cpp` establish the starting behavior.

**A1 implementation sequence.**

1. Trace the typed `int_slot_set(var slots: int[], ...)` call from each
   HashMap field. Record where the field's layout and admitted array carrier
   cease to be available. Keep the Phase II root-preparation elimination.
2. Extend the existing field plan for an admitted record and a statically
   named container field. Load the child from its proved storage lane, check
   the child's ownership/current carrier, and borrow directly on the valid
   unique path. Begin with one field; compose longer paths only through the
   same shared machinery.
3. Reuse the canonical path helper for shared/static storage, incompatible
   carriers or unresolved paths. Detachment must relink the replacement into
   the owner before the mutation, preserve caller-home publication and retain
   error-path visibility. A parent being unique never proves its child unique.
4. Carry stable field/contract facts into the local callee only where the
   existing entry mechanism can honor the proof. An unknown/public entry
   still checks its arguments. Re-establish facts after a body-side capture,
   replacement or writer call; do not cache a raw data pointer across GC.

**A2 implementation sequence.**

1. Classify the surviving array/union admissions by producer, field read,
   parameter and result boundary. Separate a necessary first admission from
   repeated validation of an unchanged admitted value.
2. Extend existing contract propagation for those specific edges. For a
   union field, every possible surviving arm must establish the required
   field contract, or a static shape guard must select a proved arm and leave
   a checked fallback. Nullable receivers need their existing null treatment.
3. Reuse a common field offset only when the physical layout agrees across
   the proved arms. Prettier's `kind: string` is not a literal discriminator;
   equality with `"text"` alone cannot authorize a record-specific offset.
4. Carry a proved return contract to its consumer without another admission,
   preserving raised-error and null branches. Do not globally mark a semantic
   union or boxed array as having a native physical representation.

**Optimization and semantic pins.** Extend the existing
`VarPathBorrowAvoidsRepeatedUniqueRootPreparation`,
`BorrowedNumericArrayRetainsStoreLane` and union-field tests with focused
fixtures. A unique typed field loop should have no mandatory generic field
lookup and no admission count proportional to iteration count when its
contract remains valid. A shared-child fixture must still detach exactly at
the first necessary write; a later capture must re-enable preparation.
Include prefix replacement, callee rebind, nullable/missing fields, malformed
open-union children and a semantically compatible but physically boxed array.
Check caller and retained snapshot outputs in interp/JIT/auto and forced GC.

**Acceptance.** Confirm hashmap improvement beyond the Phase II A release.
For A2, demonstrate reduced executed admissions and gains on at least one
additional applicable pilot. Hashmap's 450,000 unique child events need not
all disappear: success is cheaper proved traversal/transport, not a counter
target that suppresses necessary ownership checks. A cache-hit improvement
without fewer boundary calls or a timing gain does not meet this track.

### 9.3 B — Existing string append and scalar consumers

**Starting points:** `mir_emit_string_concat_tree`,
`mir_ascii_char_index_expr` and `emit_ascii_char_literal_compare` in
`transpile-mir.cpp`; `string_buffer_join<fixed_count>`, `fn_strcat_many` and
the shared copy/reserve functions in `lambda-eval.cpp`; character access
helpers in `lambda-data-runtime.cpp`. Strings remain the public and output
representation under **S7.1.1v3–S7.1.2**.

**B1 implementation sequence.**

1. Add a bounded known-arity entry strategy using the current join template.
   Select it from the concat tree's actual operand count. Avoid hand-copied
   per-arity reserve/copy implementations; use the generic path outside the
   supported static cases. This first slice can reduce varargs and loop work
   without changing allocation semantics.
2. Measure that slice before attempting an inline fast path. If the remaining
   helper cost warrants it, emit the existing capacity-fit operation for an
   owned accumulator when operand types, lengths and overlap checks permit
   it. Evaluate operands once, in order, before updating the accumulator.
3. Preserve required-length overflow checks, terminator, byte length, ASCII
   flag, freeze/share behavior and alias handling. A repeated accumulator
   operand must retain the old contents. Growth and unsupported cases use the
   same shared runtime implementation with normal precise roots.
4. Keep profiling meaningful when the fast path moves: distinguish append
   operations from runtime helper invocations. Reuse the disabled-by-default
   profiling infrastructure so fewer counted helper calls cannot hide extra
   copying, freezing or allocation.

**B2 implementation sequence.**

1. Recognize scalar consumers of indexed strings: start with `ord(s[i])`,
   then two indexed-character equality. Reuse literal-character comparison
   policy instead of adding an unrelated ASCII subsystem.
2. For proved strings with valid finite indices, emit the appropriate bounds
   and ASCII guards, then consume character bytes directly. Keep the existing
   general semantics for non-ASCII input, invalid indices and other values;
   invalid reads must retain their `null` behavior and subsequent consumer
   semantics. `ord` yields the character value, not an arbitrary UTF-8 byte.
3. Hoist stable ASCII/length facts only when the receiver cannot change.
   Exclude effectful operands from rewrites that would duplicate, reorder or
   suppress their evaluation. Do not predecode fixture text before its timer
   as part of an engine comparison.
4. Audit module string-array constants in forced JIT and auto/T0. If a direct
   lane is unavailable because the current module carrier differs, either
   prove that carrier or retain its generic access. A declaration alone does
   not justify interpreting boxed Item slots as string pointers.

**Optimization and semantic pins.** Extend `LambdaOptStrings` growth and
copy-volume tests. Cover all selected arities, empty pieces, capacity fit,
growth, frozen accumulators, self-concat, repeated operands, aliased snapshots,
embedded NUL and non-ASCII characters. Pin helper absence only inside the
eligible fast region; exercise growth/Unicode fallbacks independently.
Scalar-consumer fixtures cover negative/out-of-range/null/poison indices and
side-effect order in addition to ASCII output. Every fixture has a `.txt`
golden; representation and allocation changes run with precise GC stress.

**Acceptance.** B1 must improve unchanged base64 without increasing copy
complexity or regressing existing string-builder guards. B2 uses the archived
pre-rewrite Fast Diff source on both binaries to measure the engine's recovery
of ordinary-string performance; current Fast Diff and Hyphen are regression
guards. Archive dependencies as well as the entry script. Report B1 and B2
separately. No encoding-specific emitter or integer output buffer is needed.

### 9.4 C — Relational bounds for native loops

**Starting points:** `mir_int_lane_interval`,
`mir_index_statically_in_bounds`, existing finite-int facts and dense-loop
index proofs in `transpile-mir.cpp`. Extend their common proof consumers;
do not special-case a benchmark function name or copy the loop emitter.

**Implementation sequence.**

1. Extend the bounded-loop analysis to retain simple affine relationships
   between induction variables, stable lengths and offsets. Start with
   nested counted `while` loops whose updates and exits are understood.
   Intersect facts at joins and restore the prior environment on loop exit.
2. In the text-search pattern, establish `0 <= m <= n`, the valid outer
   position range and `0 <= offset < m`. Prove `position + offset < n` and
   `offset < m` without evaluating potentially overflowing arithmetic in the
   proof guard itself. Empty or longer patterns must retain their original
   behavior. If guards are needed, evaluate them once before a fast loop and
   retain the canonical checked loop on a miss.
3. Feed those facts into the existing indexed read and integer arithmetic
   lowering. Eliminate only the proven redundant sign, upper-bound and
   counter-overflow checks. Account for the update at the final iteration;
   an in-range body does not by itself prove the terminating increment safe.
4. Treat finite element values as a separate slice. A producer's closed
   domain may justify native equality/arithmetic after its stores are proved,
   but `int[]` contains more than ordinary finite machine integers. Retain
   poison/null handling unless an independent scoped proof removes it under
   **S4.1.1–S4.1.5**, **S4.2.3** and **D3.3.4**.
5. Invalidate affected length, carrier or element facts on rebind, resizing,
   uncertain stores and writer calls. Reuse the same analysis on quicksort
   and current Fast Diff where their loop shapes qualify; declining an
   unproved case is preferable to adding a workload-specific exception.

**Optimization and semantic pins.** Add positive nested-loop fixtures with
variable lengths and offsets, plus negatives for non-unit/unknown updates,
early breaks, receiver mutation, aliasing, zero lengths, longer patterns,
negative indices and finite-limit/poison arithmetic. Pin bounds/overflow
instruction removal in a scoped MIR region, and require the checked sibling
when runtime guards remain. A generic helper census alone is insufficient:
these loops were already mostly native. Verify interpreter/JIT/auto outputs
and retain the existing nullable-bool and int-lane regression fixtures.

**Acceptance.** Require an unchanged text_search paired gain, not just fewer
MIR instructions. Record native instruction/code-size evidence for the hot
loop and check compile time when cloning a loop. A result attributed to fewer
spills must include native evidence; do not infer register-allocation costs
from MIR size. Quicksort/Fast Diff provide both applicability and regression
checks, not a requirement that every loop qualify.

### 9.5 D — Typed construction and temporary lifetime

**Starting points:** `mir_fill_proves_nonempty_numeric_array_contract`,
`emit_fill_numeric_array_declaration_boundary`,
`lambda_array_admit_numeric_contract` and the existing packed constructors.
Cube3d already has packed fill; preserve that baseline rather than replacing
it with the rejected source-literal experiment.

**D1 implementation sequence.**

1. Attribute repeated checks to fill production, declaration, parameter and
   return boundaries. Use existing counters where available; add a narrow
   profiling counter only when it identifies a missing cost category.
2. Fuse the producer and its explicit numeric contract through a shared
   constructor/admission facility. The returned value must actually have
   the certified rank, leaf contract and physical carrier. Preserve zero
   length, invalid length, error propagation and nullable-element cases.
3. Carry that established fact through known results and consumers using
   track A2's proof machinery. Avoid replacing a cheap admission with another
   generic call or a second allocation. Generalize lane handling through
   shared helpers, not copied int/float/bool branches.

**D2 is a separately measured lifetime slice.** Examine the 3/4/16-element
results only after D1. Scalar replacement is eligible where the allocation
does not escape and every observed lane can retain its proper value. Direct
destination construction additionally needs compatible ownership, complete
initialization and safe input/output aliasing. A value retained across calls
cannot share a reused result buffer. Exceptions, partial writes and early
returns must preserve the values visible before an exit. Keep ordinary
allocation for unproved cases; use no unsafe stack allocation of escaping
containers or conservative stack GC. Authority is **D3.3.3v3**, **D4.1.4v4**,
**D5.2**, **D5.3** and **S9.1.2–S9.1.3**.

**Optimization and semantic pins.** Test producer-contract reuse across a
local return, zero-size arrays, mismatched/nullable fill values, boxed carrier
fallbacks and a caller that keeps multiple returned arrays alive. For D2,
include partial initialization, input/destination overlap, nested calls and
forced allocation during result construction. Require fewer admission calls
for D1 and fewer attributable allocations for D2; zero COW copies alone is
not an allocation measurement. Extend existing admission/constructor tests.

**Acceptance.** Confirm unchanged cube3d improvement with separate D1/D2
results and allocation evidence. Retain FFT, nbody and ordinary numeric-array
tests as guards. An inout rewrite or a fill-to-literal source change cannot
be credited to this engine track. Close an unsuccessful experiment with its
negative evidence, rather than assuming that buffer reuse must help.

### 9.6 E — Ownership and liveness for copy-heavy structures

**Starting points:** `lambda_ast_plan_place_handles`, `rmw_branch_local` and
the place-copy lifetime analysis in `lambda/runtime/build_ast.cpp`, plus
their interpreter/MIR consumers. The relevant authority is
**D4.4.4v4–D4.4.6** and **S9.1.2–S9.1.3**, not a new aliasing convention.

**Implementation sequence.**

1. Attribute Splay/Havlak/Deltablue's surviving share marks and copies to
   specific bindings, rotations and nested calls. Reuse existing COW
   profiling; if aggregate counters cannot identify a cause, add an opt-in
   site/reason diagnostic. Keep attribution runs outside timing measurements.
2. Select a concrete pattern where the previous value has no observer by the
   time a place is updated. Extend the existing last-use/store-back analysis
   for that shape. A last syntactic reference is insufficient if the value
   escaped, survives in a closure or was stored in another container.
3. Decide eligibility once in shared AST analysis for both tiers. Carry the
   existing runtime spine test and store-back-or-mark obligations onto every
   writing path, including branches, nested calls and error exits. Loop-carried
   values need facts valid across the back edge; an unknown writer invalidates
   the affected borrow.
4. Reuse track A's stable typed path information within the proved region.
   Preserve child-sharing checks and reload moved data after allocation.
   Reduce unnecessary capture/detachment before considering collector tuning.

**Optimization and semantic pins.** Extend the existing sibling-handle,
move-out and place-mutator tests with branch-local rotations, recursive calls,
loop-carried handles and caller-root rebinds. Positive cases pin the specific
copy reduction. Paired negative cases retain an old node, store it elsewhere,
capture it or observe it after a writer call, and must still see the snapshot.
Include early return/error publication and forced GC/poisoning/root-witness
runs on both ownership implementations. Do not replace a copy assertion with
a looser threshold merely to accommodate a regression.

**Acceptance.** At least one unchanged copy-heavy pilot must show both an
attributed copy/byte reduction and a confirmed timing gain. Expand the pattern
to another eligible workload through the same analysis. A lower GC percentage
without allocation/copy evidence does not establish why performance changed,
and C's pointer-rotation algorithm is not a license to change Lambda's source
or snapshot semantics.

### 9.7 Optimization regression tests

Use the existing `test/test_lambda_opt_gtest.cpp`,
`test/test_mir_emission_gtest.cpp` and `test/mir/lambda/` infrastructure.
Candidate fixture names may use `tune31_phase3_*`; all such fixtures in this
proposal are planned, not already present. Every new `.ls` gets a matching
`.txt`; emitted-code checks use `.mir-check` sidecars where appropriate.

Each retained optimization needs a positive mechanism assertion and a
fallback/invalidation negative, in addition to output correctness. Prefer
iteration-independent admission counts, attributable copy/allocation bounds
and scoped instruction assertions over exact full-function MIR snapshots.
The existing sidecar `in_range` support can distinguish a fast arm from its
checked sibling. Use `mir_mandatory_census.py` for mandatory loop calls;
neither its absence result nor a whole-function static count proves that a
helper is never executed. Pair it with the canonical executed census.

Audit earlier assertions when a stronger implementation supersedes them.
For example, `tune31_var_path_borrow.mir-check` currently *requires*
`cow_path_borrow_fixed`; A1 may legitimately replace its eligible path.
Update that assertion to pin direct access, necessary fallback behavior and
root-preparation elimination. Similarly, an admission test that currently
requires cache hits may need a new positive assertion for eliminated calls,
while retaining its malformed-input fallback. Replace the old mechanism with
a stronger measured invariant, not simply removal of the failing assertion.

For retained engine changes, run focused tests, relevant MIR/GC tests and
`make test-lambda-baseline`. Re-establish any failure on the actual control;
do not inherit historical exemptions or mask failures in a harness. Timing
thresholds belong in paired release reports, not unit-test wall clocks.

### 9.8 Release measurement and phase attribution

**Freeze the comparison before implementation.** Archive a `make release`
binary and manifest for the Phase III start, identifying whether its engine
matches the final Phase II binary or includes later engine commits. Record
binary/engine commit, source and transitive dependency hashes, fixture data,
tier, environment, output digests and raw samples. Preserve original and
rewritten source populations explicitly; a matching entry-file hash alone
does not establish workload identity.

| Comparison | Fixed source population | Meaning |
|---|---|---|
| Phase III start → each slice/final | Frozen current 63-row suite | Incremental Phase III engine effect |
| Phase I final → Phase II final → Phase III final | One identical compatible frozen suite on every binary | Measured later-phase effects, including the missing Phase II suite result |
| Pre-Tune31 release → Phase III final | Preserved original Tune31 sources/dependencies on both | Combined Phase I–III engine effect |
| Old source → rewritten source on one binary | Separate source-pair manifests | Source rewrite effect only |
| Typed Lambda → C2MIR | Pinned independent ports, data and reference driver | Remaining cross-language gap, with workload differences identified |

Do not multiply geomeans from different populations to obtain a combined
result. If an older binary cannot run a current source, use a compatible
archived population for the entire historical comparison. If the complete
source/dependency closure is unavailable, report that comparison unavailable;
do not silently omit a row or substitute a new workload. The primary Phase
III start/final comparison must still include all 63 typed rows, with the full
untyped suite as the regression guard.

**Discovery and confirmation.** Use the checked-in
`test/benchmark/run_paired_benchmarks.py` with alternating control/candidate
order. Seven or nine pairs are suitable for discovery. Choose the confirmation
count from pilot variance before starting that run; 41 is a default, not a
requirement or guarantee. Keep discovery separate from confirmation, and do
not repeatedly extend a run until a confidence bound passes. If uncertainty
remains, report it or plan a new independent confirmation. Preserve every
valid sample and disclose invalid/output-failing runs.

For a claimed pilot gain require a candidate/control median ratio below one
and a one-sided paired-bootstrap 95% upper bound below one, plus its mechanism
evidence. For pilots and flagged regressions, the non-regression gate remains
an upper bound ≤1.03. Confirm suspected >3% regressions; an inconclusive bound
is not a pass. Short kernels may use a separately reported sustained companion
fixture for attribution, while the canonical source remains the suite result.

Time uninstrumented release processes. Collect COW/admission/string/allocation
counters and CPU samples in separate runs. Report JIT execution, compilation,
auto execution and process-wall effects separately; record code size for loop
cloning or expanded inline paths. Test output and GC behavior in all relevant
tiers, even when the proposed gain is forced-JIT only. Publish raw per-row
ratios, uncertainty, improved/regressed/unchanged rows and the full geomean.

The typed geomean target is **≤0.90 versus Phase III start**. Claim a broad
typed improvement only when the full-suite data supports it and gains extend
beyond a single row. Historical C2MIR ratios in §8 are context until a matched
reference rerun; they are not the denominator of an engine A/B claim.

### 9.9 Deliverables and completion record

Maintain a per-slice record with status **PROPOSED**, **IMPLEMENTED**,
**REJECTED BY MEASUREMENT**, or **DEFERRED WITH REASON**. Initially all A1/A2,
B1/B2, C, D1/D2 and E are PROPOSED. An investigation without retained code is
not an implemented optimization. A missed target or deferred dependency
remains visible rather than being relabeled complete.

The final record must contain:

1. Each retained change's proof, implementation locations and positive plus
   negative optimization tests; shared analysis/helpers used by related cases.
2. Before/after executed costs and fixed-source release confirmation for each
   claimed pilot, including unsuccessful construction/ownership experiments.
3. The 63-row typed and untyped comparison, auto/process-wall guards, and
   separate historical engine and source-rewrite attribution where available.
4. Actual baseline/MIR/GC validation results, binary/source manifests and
   reproducible commands under `temp/tune31_phase3_engine/`.
5. Remaining bottlenecks from final profiles, the suite objective's measured
   outcome, and an explicit explanation for every rejected or deferred slice.

Phase III is not complete merely because a smaller MIR dump was emitted, a
helper was cached, a copy counter fell, or one benchmark improved. Completion
requires the disposition of every proposed slice and validation of every
retained change; achieving the performance objective is reported separately.

### 9.10 Live implementation record

| Track | Status | Result and next gate |
|---|---|---|
| A1 | IMPLEMENTED | Shape-checked one-field `var` borrow; confirmed on unchanged typed hashmap. Expand only through the same descriptor/fallback mechanism after an independent path shape qualifies. |
| A2 | REJECTED BY MEASUREMENT | Proven `Doc[]` literals reduced union admissions, but the certificate publication made the unchanged prettier_ast pilot 1.0198× slower. The implementation was removed. |
| B1 | IMPLEMENTED | Fixed owned string append arities 2–6; confirmed on unchanged typed base64. Capacity-fit inlining remains a separate measured follow-up. |
| B2 | IMPLEMENTED | An explicit-string pair comparator preserves UTF-8 and absent-read equality without materializing character strings; confirmed on the archived typed-string Fast Diff control. |
| C | REJECTED BY MEASUREMENT | The safe typed `i < len(values)` proof erased its matched bounds branches, but the unchanged workload pilots were 0.9897× on text_search and 1.0303× on Fast Diff. The implementation was removed. |
| D1 | REJECTED BY MEASUREMENT | The exact typed `fill` fusion removed the two emitted JIT calls, but its rooted combined helper regressed unchanged cube3d to 1.0145×. The implementation was removed. |
| D2 | DEFERRED WITH REASON | Cube3d's 3/4/16-element results cross user-call/return boundaries and remain observable; after D1’s rejected fusion no measured construction path justifies a new escape-analysis/stack-representation slice. |
| E | DEFERRED WITH REASON | Fresh forced-JIT attribution reproduced the copy-heavy pilots, but every hot shape retains an observer through a caller, child slot or work list. No sound shared capture rule is identified. |

**B1 — implemented and confirmed.** The concat-tree owner arm now selects
`fn_strcat`, `fn_strcat3`, `fn_strcat4`, `fn_strcat5` or `fn_strcat6` for two
through six known pieces. Those entries share `string_buffer_join`, preserving
capacity, growth, aliases and precise roots; non-owner and larger expressions
retain `fn_strcat_many`. The positive MIR fixtures are
`tune31_phase3_string_arity`, `tune22_concat_chain` and
`tune16_nested_string_builder`; the focused MIR set (16 cases) and focused
string/admission/COW set (9 cases) passed before the release comparison.

The archived Phase III start binary is
`temp/tune31_phase3_engine/lambda_phase3_start_release`
(`755951f3fe0e9a940a1907b94e5107a0f0f7528bc93e1e3fb3fc0ffac4d772e2`), and
the B1 control/candidate archive is recorded in
`paired_b1_base64_jit_41.json`. On the unchanged typed base64 source, the
41-pair JIT candidate/control median ratio was **0.69325**, with identical
stdout in all pairs. A separate COW profile has identical append, growth,
copy-byte and freeze rows before and after, establishing that the improvement
is ABI/join-loop work rather than changed string allocation semantics.

**B2 — implemented and confirmed.** `fn_string_char_eq` compares two
indexed, explicitly typed strings without materializing either character. For
ASCII strings it compares the selected bytes; otherwise it compares the exact
UTF-8 character spans. Two absent indexed reads compare equal, preserving the
ordinary `null == null` result. The helper is a no-GC leaf only after the
MIR-side explicit `string` certificate; inferred string lanes retain their
existing `fn_string_ascii_at` lowering. This is required by **D3.3.3v3** and
preserves **S7.1.1v3** indexing and equality behavior without a source
representation rewrite.

`tune31_string_char_pair` covers ASCII, UTF-8, unequal and both-absent reads,
and requires `fn_string_char_eq` while forbidding the two index helpers and
generic equality. `tune31_inferred_string_char_pair` is the complementary
negative case: it forbids the new helper and requires the established inferred
string index helper. The focused emission suite (including the prior literal
and inferred-string fixtures) and the focused string/COW optimization suite
passed. The final archive
`lambda_phase3_b2_declared_release`
(`f6dbe625e22aa46ec69b089c1ed266e2fcc372f2beca209114d8dfa91916c8d9`)
also preserves the checksum under JIT forced collection and freed-memory
poisoning, JIT root-witness level 2, and auto execution.

Against `lambda_phase3_a1_release`, the source-identical archived
`fast_diff2_before_code_tables.ls` control (entry SHA-256
`dd5691dce71f39323338585ce624fa93773b90676cbd74bf9d3ef884bf5db234`, source
tree `d07772ec4795a77d0031ff9f56d7cdadc7a8348cf16c2defe6473f1d86b013f1`)
was **89.002 ms / 128.656 ms = 0.69178×** in
`paired_b2_declared_typed_string_fast_diff_jit_41.json`; the one-sided
paired-bootstrap 95% upper bound was **0.73082**, with 41/41 candidate wins
and identical normalized stdout. The earlier unrestricted experiment is kept
separately because inferred-string matching gave an inconclusive untyped guard;
the retained implementation is the explicitly certified form. The final
untyped Fast Diff guard centers at 0.99630× but has a 1.06683 upper bound;
the A1-vs-A1 control has a similarly broad 1.03734 upper bound. Since the new
helper is structurally absent from that source, these high-variance guard runs
are not attributed to B2; the full untyped suite remains the final regression
gate.

**A1 — implemented and confirmed.**
`mir_emit_cow_typed_map_field_borrow` recognizes a one-segment member place
only when the root has a trusted fixed record contract and the selected slot
has a container storage lane. It calls
`cow_path_borrow_typed_map_field`, which verifies the current Map/TypeMap
identity and slot capacity, reads the slot through the canonical shaped-field
reader, and otherwise calls `cow_path_borrow_fixed`. The helper roots owner,
key and child; if detaching the child can collect, it reloads the rooted map
and packed data before the canonical lane writer reinstalls the child. This
retains **D3.3.3v3** current-carrier certification, **D4.4.4v4** invalidation
and reload requirements, and **D5.3.1–D5.3.4** precise-root obligations. The
generic helper remains the carrier-mismatch and malformed-child route, so
**S9.1.2–S9.1.3** ownership publication and **S7.1.3v2** error behavior do
not depend on the fast arm.

`tune31_var_path_borrow.mir-check` now requires the direct helper and forbids
the generic fixed helper in its eligible loop; its output and COW assertions
retain the shared-root/child-detach observation. Focused MIR checks (4 cases)
and the focused Lambda optimization suite (9 cases) passed. Forced collection
with `LAMBDA_GC_FORCE_EVERY=1`, `LAMBDA_GC_POISON_FREED=1` passed in JIT and
interpreter tiers; the JIT root-witness level-2 sweep passed as well.

Against the archived B1 release
`lambda_phase3_b1_release`
(`a2b1947ab09a2573504f62297f0580039d0d4c9ccbb4ecf161e2c651cdffc4e7`), the
A1 release
`lambda_phase3_a1_release`
(`408af01f2c982c070d2547345c2659436f85cc371ddb735c8337990f5a13a38b`) used
the identical `hashmap2.ls` source/tree hash recorded in the paired artifacts.
The 41-pair JIT result in `paired_a1_hashmap_jit_41.json` was **25.814 ms /
35.822 ms = 0.72062**, with a one-sided paired-bootstrap 95% upper bound of
**0.76449**, 41/41 candidate wins and identical stdout. The 41-pair auto
result in `paired_a1_hashmap_auto_41.json` was **46.582 ms / 56.253 ms =
0.82808**, upper bound **0.88904**, 39/41 wins and identical stdout.
`hashmap2_a1_{control,candidate}_profile.tsv` are byte-identical: 450,000
ArrayNum unique mutations, four Map unique mutations and zero copies. The
gain is therefore the removed generic field resolution, while all necessary
ownership events remain present.

**A2 — rejected by measurement.** The canonical `prettier_ast2` profile
located the residual union work at direct `Doc[]` literal arguments to
`concat_docs`, rather than at the five `_b` transition entries. A bounded
trial recognized a nonempty, non-spreading literal only when every element had
an existing `Doc` boundary proof, then installed an exact ordinary-Array
certificate once after construction. Its positive and open-element-negative
MIR fixtures proved the compiler selection and fallback; JIT/interpreter,
forced-GC/freed-memory poisoning and JIT root-witness checks all preserved the
checksum. The construction rule followed **D3.3.3v3** and the allocation-safe
certificate publication/rooting rule in **D3.3.4**.

The mechanism did reduce executed union admissions from **6,615,040** to
**6,182,144** per canonical run (6.5%; cache misses remained six), but it
also added a certificate-cache call for each eligible literal. The archived
trial release `lambda_phase3_a2_release`
(`b47d97b482eca006c49e52332c34b7da6c65678e82b6e7c162d2d1ca64ebbe87`) was
compared with the B2 archive on the identical source entry/tree recorded in
`paired_a2_prettier_ast_jit_9.json`. Its 9-pair JIT median was **561.781 ms /
550.897 ms = 1.01976×**, with a one-sided paired-bootstrap 95% upper bound
of **1.08148**, only 4/9 candidate wins, and identical normalized stdout.
The warm union-map proof is cheaper than the added helper call on this path,
so the runtime helper, compiler selection and trial fixtures were removed;
the profile and paired artifact remain under `temp/tune31_phase3_engine/`.

**D1 — rejected by measurement.** Cube3d's remaining typed construction
sites already use packed `ArrayNum` results, but each eligible `fill(n,
scalar)` emitted the public `fn_fill` call followed by
`lambda_array_admit_numeric_contract` to publish its rank-one certificate.
A shared fused helper reused those two existing implementations, rooted the
two source items and fresh result across allocations, and was selected only
when the existing primitive, non-null `T[]` proof qualified. Positive and
untyped-negative MIR fixtures pinned selection/fallback, and JIT/interpreter,
forced-GC/freed-memory poisoning and JIT root-witness runs passed. This kept
the certificate, allocation and error rules required by **D3.3.3v3**,
**D3.3.4** and **S9.2.2**.

The MIR dump reduced all 15 cube3d typed-fill sites to one fused call and
removed the two public calls from those sites, but the required root frame and
combined entry were slower than two specialized JIT crossings. The archived
trial `lambda_phase3_d1_release`
(`81c5cc4bef14414abf4dcf100ded5bb817c2c2e474867f9998bacddabd00b332`) was
compared with the B2 archive on the unchanged cube3d entry SHA-256
`8e070abd68f312196ba6967ff2ed44ec6dbc6b007d815eb5e1dc1d5613538707` and tree
`2b7219ffa483db023f421f1441b19a5fe4abf3e1d6674dc1a47a3a43afef73b7`. The
9-pair JIT artifact `paired_d1_cube3d_jit_9.json` measured **6.856 ms /
6.758 ms = 1.01450×**, one-sided 95% upper bound **1.04611**, one candidate
win out of nine, and identical normalized stdout. The helper, compiler hint
and trial fixtures were removed.

**D2 — deferred with reason.** The current cube3d profile has no COW copies,
and its repeated 3/4/16-element values are not a nonescaping temporary class:
`mat4_mul`, `vmulti`, `vmulti2`, `calc_cross` and `calc_normal` publish
returned arrays; callers retain them, forward them through additional calls,
or observe their elements after later allocation. Reusing a stack buffer or a
single destination would violate the value/snapshot lifetime and precise-root
requirements of **D4.1.4v4**, **D5.2**, **D5.3** and **S9.1.2–S9.1.3**.
The safe candidate first needed D1’s construction gain, which was rejected.
No separate D2 implementation is retained until a future profile identifies
an actually nonescaping result class with its full lifetime proof.

**C — rejected by measurement.** A temporary compiler slice recognized an
admitted typed array in a zero-origin `index < len(values)` loop, required the
counter's sole `+ 1` assignment to be the final body statement, and scanned
the body for resizing calls, rebinding, mutable lending and capture. It then
removed the matching direct read's negative and upper-bound branches. The
temporary `tune31_dynamic_array_bound` MIR fixture proved that the direct
load remained after the loop-head test, and its JIT semantic fixture preserved
the result. This was a valid application of **D3.3.3v3**'s certificate carrier
and **S7.1.1v3**'s in-bounds read condition, but it did not recover enough
work to satisfy §9.8's measured pilot gate.

The archived trial binary
`lambda_phase3_c_release`
(`2c9bc77bddf9f99eb65d8b6e1ee84dbafc57aaafdbabdddc6ed4edd848dbceae`) was
compared against the A1 archive on identical source trees. The 9-pair JIT
artifact `paired_c_text_search_jit_9.json` recorded **0.9897×** with 9/9
candidate wins and identical normalized stdout; its independently selected
Fast Diff cross-check in `paired_c_fast_diff_jit_9.json` recorded **1.0303×**
with only 5/9 candidate wins and identical normalized stdout. The slice was
therefore removed rather than retaining extra loop-analysis complexity for an
inconsequential, non-general gain. The raw artifacts remain under
`temp/tune31_phase3_engine/` for reproducibility.

**E — deferred with reason.** The fresh forced-JIT diagnostic runs in
`e_{splay,havlak,deltablue}_debug_jit.tsv` reproduce the canonical release
copy census byte-for-byte: Splay has **951,168** Map copies
(**61,825,920** bytes), Havlak has **65,876** Map and **44,999** Array copies
(**7,502,280** bytes combined), and Deltablue has **38,980** ArrayNum copies
(**3,409,760** bytes). These are diagnostic runs only, not timing evidence.
Their companion MIR dumps identify the high-density ownership regions:
`splay_node` has 20 capture/prepare sites for recursive branches and
rotations; Havlak concentrates them in `hlf_find_loops`,
`hlf_process_edges`, `hlf_step_e`, `cfg_add_edge` and
`lsg_calc_nesting`; Deltablue concentrates them in plan propagation/removal
and constraint satisfaction.

Inspection of those source regions identifies no eligible last-use pattern.
Splay's `left`/`right`/`branch` values are reattached to the rotated tree or
returned through a `var` caller root; skipping their capture would expose a
later child/root update to an earlier observer. Havlak reads nested `l0`/
`c1`/`c2` arrays and graph nodes, then stores them back into their owning
indexed structure while other graph paths remain live. Deltablue deliberately
uses the handle-store `World`; its mutable `todo`, `sources`, plan and
constraint vectors preserve inputs or work-list observers across a removal or
propagation call. These are precisely the binding/construction copies and
sole-`var` sharing rule of **S9.1.2–S9.1.3**, and the residual Splay rotation
case recorded by **D4.4.4v4**. A syntactic final reference cannot overrule
those reachable observers.

An E implementation would therefore need a new call-site move or general
escape/liveness convention, rather than an extension of the existing
store-back proof. That would require a separate formal design and semantic
proposal; it cannot be admitted as a Tune31 optimization under
**D4.4.4v4–D4.4.6**. No ownership code is retained, and the existing
snapshot, branch-local and COW-counter tests remain the regression guards.

### 9.11 Completion record

**Retained implementation.** B1 adds fixed owned string joins for two through
six pieces; B2 adds the explicitly certified, UTF-8-correct indexed-string
pair comparison; and A1 adds the rechecked one-field typed-map borrow. The
positive and fallback pins are the new `tune31_phase3_string_arity`,
`tune31_string_char_pair`, `tune31_inferred_string_char_pair`,
`tune31_var_path_borrow` and `tune31_var_path_reborrow_shared` MIR fixtures.
The latter now requires root preparation followed by the direct
typed-field helper, so the shared-root case cannot silently regress to the
retired generic path.

**Validation.** `make build-test` completed; 17 focused MIR-emission cases,
29 string/COW/admission optimization tests and seven selected forced-GC corpus
cases passed. `make test-lambda-baseline` passed **5,718 / 5,718** tests,
including 172 MIR-emission and 220 forced-GC cases. The final `make release`
binary is `lambda_phase3_final_release` with SHA-256
`8438e17efe4de93814df5fb3a62afaa051ef36822ce273b6d205b777ee2eca19`;
its commit, source diff and hash manifest are retained under
`temp/tune31_phase3_engine/`. No release timing used a debug binary.

**Frozen full-suite result.**
`paired_phase3_final_full_jit_9.json` compares the archived Phase III start
with this final release over all 63 untyped and 63 typed rows, nine alternating
pairs per row. Every normalized stdout hash matches. The execution-time
geomean is **0.97827× typed** and **1.00443× untyped**; the process-wall
geomean is **0.99323× typed** and **1.00164× untyped**. The typed execution
objective of **≤0.90×** is therefore **missed**. The accepted targeted wins
remain separately established by their 41-pair fixed-source artifacts: B1
base64 **0.69325×**, A1 hashmap **0.72062×**, and B2's archived explicit-string
Fast Diff **0.69178×**. They do not justify claiming a broad 63-row engine
gain.

`paired_phase3_final_full_auto_3.json` is the auto/compilation and
process-wall guard across the same 126 rows. It also has matching stdout in
every process. Its execution geomeans are **0.98949× typed** and **1.00470×
untyped**; process-wall geomeans are **0.99709× typed** and **1.00118×
untyped**. Three pairs are a guard, not a row-level confirmation.

**Regression interpretation.** The JIT discovery pass had several apparent
greater-than-3% rows, so the preselected 41-pair confirmation artifact
`paired_phase3_final_regression_confirm_jit_41.json` was run. It retained
apparent typed Bounce (**1.0460×**) and Fasta (**1.0322×**) changes and
untyped Bounce (**1.0339×**), Havlak (**1.0346×**) and Array1 (**1.1150×**)
changes. However, the independent stability control
`paired_phase3_start_self_jit_41.json` compares the Phase III start binary to
itself and still yields typed Fasta **1.0413×** with upper bound **1.0913**,
and multiple upper bounds above 1.03. The archive bisection likewise reverses
the Array1 direction between valid 9-pair batches. These sources do not call
the newly retained hot helpers in their timed regions, and the same-scale
movement occurs without an engine change. Per §9.8 this is an **inconclusive
non-regression guard, not a pass**; no source or engine regression is
attributed to B1, B2 or A1. A future broad-tuning cycle needs a quieter host
or longer fixed-source sustained forms before it can apply a ≤1.03
row-specific gate reliably.

**Result.** Every Phase III track has a recorded implementation, rejection or
deferral; rejected trials have been removed from the source tree. The remaining
bottleneck is not a missing type annotation or generic field lookup: copy-heavy
graphs still require the observable snapshots in **S9.1.2–S9.1.3**, while the
unresolved broad cost is the aggregate of typed representation, construction,
admission and ownership work described in §8. A future move/escape convention
must be proposed against **D4.4.4v4–D4.4.6** before revisiting E.
