# Lambda Implementation: Tune31 — Specialization and Typed Runtime Boundaries

- **Status:** Phase I IMPLEMENTED 2026-09-19; Phase II typed tuning added
  2026-09-20 (§6). Phase II's typed Hyphen source rewrite and inferred-store
  engine track are implemented, as is the ownership/path A track; B's
  and E's source experiments are rejected by their paired results, while C
  and D close without speculative engine changes.
  Phase I's final code, focused semantic gates and paired release evidence
  are recorded in §3.7 and §4.4. The full Phase II baseline passes, as
  recorded in §6.5.
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
four nested concatenations. Removing that boundary would require an
encoding-specific emitter or a second string builder, neither of which is a
generic helper reduction under **S7.1.1v3–S7.1.2**. No C change is retained.

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
