# Tune26: recover Result37 performance and make typed arrays pay

- **Date:** 2026-09-11.
- **Status:** IMPLEMENTED WITH OPEN PERFORMANCE GATES. T26-0 through T26-4
  have working-tree implementation slices and focused evidence; T26-5 and all
  proposal completion gates remain open. Section 9 records both the measured
  improvement and the failures that prevent closure.
- **User requirement:** typed arrays should speed up code. Adding correct
  homogeneous-array annotations to an equivalent workload must preserve the
  inferred implementation's speed and enable faster native access where the
  inferred path remains boxed. An already optimal inferred lane may tie.
- **Primary reference:** [Result42 versus Result37 analysis](Lambda_Benchmark_Result42_Analysis.md).
- **Source audit:** working tree based on `e86d7ff8b602`. The relevant
  admission, array-access and integer-comparison mechanisms have been
  extended; this is an uncommitted implementation, not a release artifact.
- **Predecessors:** [Tune22](Lambda_Impl_Tune22.md),
  [typed-array implementation, §§13–14](Lambda_Impl_Typed_Array.md),
  [Tune24](Lambda_Impl_Tune24.md), [Tune25](Lambda_Impl_Tune25.md).
- **Authority:** [formal design](../../doc/Lambda_Formal_Design.md)
  **D2.2.2**, **D2.4.1–D2.4.3**, **D3.3.2v2–D3.3.4**, **D5.3.4**,
  **D8.2.6**, **D8.3.1–D8.3.3**, **D8.4.3v2**;
  [formal semantics](../../doc/Lambda_Formal_Semantics.md)
  **S4.1.2**, **S7.1.1v3**, **S7.1.3v2**, **S9.1.2**, **S9.2.2**,
  **S11.4.1v3**. Tune26 changes implementation and performance acceptance;
  it proposes no change to these rulings.

## 1. Objective and completion contract

Recover the lost Result37 performance on unchanged workloads, retain the
subsequent graph/string improvements, and make explicit array contracts
provide reusable optimization evidence. A typed declaration should establish
its proof at the boundary; successful hot loops should consume that proof
without repeatedly reconstructing it.

The round has three independent obligations:

1. **Recovery:** compare the candidate with the exact Result37 release on
   fixed source and input. Faster than Result42 alone is insufficient.
2. **Annotation parity:** compare source pairs differing only in supported
   type annotations on the same candidate. Diagnose every repeatable penalty.
3. **Preservation:** compare the candidate with a frozen pre-Tune26 release
   on the complete current corpus, including untyped, graph, text and auto-tier
   workloads. Keep the wins introduced after Result37.

These are acceptance targets, not forecast measurements. A track may be
implemented while the round remains **IMPLEMENTED WITH OPEN PERFORMANCE
GATES**. Correctness checks alone cannot close Tune26.

## 2. What must be recovered

The Result42 analysis separates benchmark drift from engine regressions:

| Population | Rows | Typed R42/R37 execution geomean | Interpretation |
|---|---:|---:|---|
| Common benchmark names | 59 | 1.427x | Includes two changed text workloads |
| Excluding microdiff/hyphen rewrites | 57 | 1.222x | Still includes other typed-source rewrites |
| Identical selected typed entry-source bytes | 43 | **1.341x** | Primary historical recovery population |
| Complete Result42 | 63 | No direct R37 ratio | Four additional text workloads |

On the 43-row group, Node and QuickJS move by less than 1% and C2MIR by about
1%. The headline typed/Node change, 0.78x to 1.27x, compares different
populations; on the 59 common names it is 0.781x to 1.119x.

The earlier analysis replayed exact release binaries in seven alternating
pairs per row, with equal normalized outputs for all 84 pairs:

| Typed row | R37 replay ms | R42 replay ms | R42/R37 | First investigation track |
|---|---:|---:|---:|---|
| kostya/primes | 3.362 | 23.160 | 6.889x | T26-1, then T26-2/3 |
| awfy/sieve | 0.035 | 0.130 | 3.714x | T26-1/2 |
| larceny/array1 | 0.759 | 1.654 | 2.179x | T26-1/2 |
| jetstream/cube3d | 14.186 | 29.140 | 2.054x | T26-2/4 |
| larceny/diviter | 248.934 | 497.624 | 1.999x | T26-3 |
| text/fast_diff | 142.050 | 274.300 | 1.931x | T26-5; attribution still open |
| kostya/matmul | 22.879 | 42.885 | 1.874x | T26-2/3 |
| r7rs/nqueens | 1.350 | 2.491 | 1.845x | T26-2/5 |
| r7rs/sum | 0.768 | 1.137 | 1.480x | T26-3 |
| r7rs/fib | 1.203 | 1.450 | 1.205x | T26-5 |
| r7rs/fibfp | 1.111 | 1.110 | 0.999x | Float control |
| r7rs/sumfp | 0.064 | 0.064 | 1.000x | Float control |

These are historical replay values retained in the checked-in analysis, not
fresh timings from this proposal. T26-0 must regenerate the measurements and
retain durable raw evidence before using them to close a performance gate.
The original Result37/42 JSON and source commits remain available.

Do not restore the old microdiff scoring shortcut or vowel-based hyphenation.
Commit `a4c2f78c8` replaced them with fuller workloads; their apparent 405x
and 35x slowdowns are not valid engine-recovery targets. Measure their current
implementations against the frozen current release and current native ports.

## 3. Current mechanisms and missing work

| Mechanism already implemented | Remaining cost | Owner to extend |
|---|---|---|
| Packed bool `fill` uses `memset` | Its uncertified result triggers element-by-element typed admission | `fn_fill`, `runtime_type_admit_array`, shared array-contract queries |
| Heap-local certificate interning and live-carrier checks | Fresh exact primitive carriers cannot establish their first proof cheaply | `lambda_array_rep_proves[_cert]`, `runtime_array_rep_cert_intern` |
| Typed native loads/stores and compact-width stores | Per-access certificate, bounds and ownership checks | `emit_array_rep_cert_guard`, `emit_checked_index_load`, native store emitters |
| Dense-loop and compact-integer specialization | Explicit contracts disable dense reads; BOOL is excluded from `mir_dense_array_elem_supported` | `MirDenseScan`, `MirDenseRootFact`, compact-loop analysis |
| Finite integer arithmetic within guarded loops | Comparisons do not consume the same range evidence | `mir_int_lane_interval`, `mir_emit_int_lane_pair_in_band` |
| Same-contract forwarding and typed `var` array write-back | Proof lifetime across calls, sharing and replacement remains narrow | `MirVarEntry`, `MirValue`, existing boundary/publication helpers |
| Tune25 scalar-call hoisting | It deliberately does not establish memory/alias invariants | `em_hoist_loop_scalar_calls` and its effect metadata |

The implementation must extend these mechanisms. In particular, removing
`if (policy.array_contract) dense_inbounds = false` alone is unsafe, and it
would still leave BOOL outside the existing dense-loop admission.

## 4. Implementation tracks

### T26-0 — Freeze evidence and add annotation-parity accounting

**Deliverable:** a checked-in benchmark manifest plus generated comparison
JSON/Markdown for each completed slice.

- Freeze the 43 rows in §7, the selected script variant and source hash,
  inputs and transitive imports, expected output, and source-change status.
  Hash equality of an entry script is not a complete dependency proof.
- Preserve exact R37 and original R42 archive paths/hashes. Build and archive
  a pre-Tune26 release with commit, dirty patch hash, build configuration,
  host/power state, relevant environment, and dependency hashes. A commit
  label by itself does not identify an uncommitted build.
- Extend [the paired runner](../../test/benchmark/run_paired_benchmarks.py)
  with exact manifest selection and a pair-of-source-files mode. Reuse its
  process execution, timing parsing, output checks and sample serialization.
  Its current `--variants both` compares each variant across binaries; it
  does not itself produce an annotation-only comparison within one binary.
- Record the actual JIT/auto selector, cache/log settings and sample order.
  Validate goldens independently: equal output between two broken engines
  is not a correctness oracle. Preserve failures, timeouts and missing ports.
- Add focused diagnostics for cold array admission, already-admitted calls,
  reads, writes, and combined construction/use. Use sizes 0, 1, 16, 1K and
  1M to separate fixed boundary cost from a length-dependent scan.

Annotation pairs cover bool, int, float and representative sized lanes;
local, parameter, return, stable member and module boundaries; readonly and
exclusive mutable use. Within each pair, algorithm, data, iteration count,
allocation placement and timer boundaries are identical. The declared variant
must accept the chosen inputs under S11.4.1v3. Existing `*2.ls` ports that
also change algorithms or representations are reported separately.

**Exit evidence:** manifest audit, a fresh reproduction of the recovery rows,
and baseline annotation-pair measurements. No canonical benchmark source is
rewritten to make the compiler look faster.

### T26-1 — Establish primitive-array contracts without redundant scans

**Problem:** the proper-array change `794847ec83` routes all explicit array
contracts through admission. A fresh matching ArrayNum has no certificate;
the compact path reads and admits every element before certifying it. The
earlier primes phase probe measured 0.023 -> 15.971 ms for fill plus admission,
against 3.590 -> 6.143 ms for the remaining loops. Most added cost was at the
declaration, while the COW profile reported zero checked-store fallbacks.

**Implementation:**

1. Extend the shared contract/representation relation to answer whether the
   full destination contract follows from a verified live carrier and its
   producer guarantees. Reuse `LambdaArrayContractInfo`, exact element-kind
   resolution, lane descriptors and certificate interning. If a useful
   existing static helper must be shared, promote it through the module header.
2. Start with owned, contiguous rank-one primitive storage. A proof must cover
   the outer array contract, actual rank, leaf contract, physical width,
   null/error constraints and any value restrictions. A matching TypeId or
   element byte is insufficient. Explicitly audit which producer guarantees
   follow from all writers of that carrier.
3. Admit a repeated fill value once when its full contract requires validation,
   then use that result's proof for all initialized elements. Reuse the
   existing fill allocation/kernel and failure ordering. Materialize the
   destination certificate only at a successfully enforced explicit boundary;
   inferred element narrowing remains binding-local under D3.3.3v3.
4. For an exact carrier whose invariants already imply every element's base
   contract, install the interned certificate without boxing/visiting each
   element. Typed empty arrays receive the correct empty carrier and proof.
5. Retain value-dependent admission for refinements, representation-changing
   conversions, unknown boxed elements, unions and insufficient evidence.
   Nested, strided and external views use the existing checked path until
   separately proved. Never flatten an N-D value into a rank-one contract.

The guarantee is **O(1) additional admission work** for the eligible case;
allocating and filling n values still costs O(n). Plain `int` includes its
private sentinel representation (D2.2.2); sized integers, floating NaNs,
signed zero and nullable carriers require their own exact rules. Do not
generalize the bool result into an unchecked numeric cast.

**Evidence:** shared opt-in counters for admission attempts, proof hits,
elements visited, reifications and bytes copied; zero visited elements and
zero admission copies on eligible cold/warm cases. Existing COW counters do
not measure admission scans and must not be used as a substitute. Profiles
run separately from timings. T0 and JIT must use the same admission relation.

### T26-2 — Carry complete array proofs into native loops

**Implementation:**

- Extend the existing dense-loop root facts with the full contract/storage
  proof and the scope in which it is valid. Keep semantic admission, carrier,
  extent, and exclusive-write facts separate. Reuse Tune24's scoped proof
  ownership and existing variable invalidation; do not add unrelated global
  caches or another AST walker for each array kind.
- At a safe loop entry, prove the live owner, complete certificate/storage
  match, index interval and stable extent. In the successful region emit
  direct native loads; remove redundant certificate and bounds checks there.
  Feed the extent proof to nullable-read lowering so a proven in-bounds read
  remains native under D3.3.4. Preserve the public `T?` result elsewhere.
- Support BOOL explicitly through its actual packed carrier. Parameterize
  width/lane handling for the existing integer/float/sized families instead
  of adding a copied bool-only loop implementation.
- Cover counted loops, increasing/decreasing induction, `i-1`, and bounded
  affine matrix indices through one checked interval transfer. Prove index
  multiplication and address arithmetic cannot overflow; use full-width
  comparisons and retain Tune25's `2^32` regression cases.
- Keep one native function body under D8.3.1. Guarded fast/checked regions
  may share the existing structured loop lowering. A guard miss before entry
  runs the original checked path; no fallback may replay completed effects.
- Unknown/reentrant calls, owner replacement, growth, representation changes
  and escaping mutable aliases end the corresponding fact. Initially keep
  such loops on existing checked access rather than guessing invariance.

**Evidence:** hot-loop MIR assertions for no repeated certificate loads,
nullable boxing or bounds checks on proved accesses; a checked cold path is
still required. Count instructions in the hot region separately from cold
helper sites. Prior cube3d MIR's 181 added `item_at` sites do not establish
181 dynamically executed calls; profiles must distinguish the two.

### T26-3 — Reuse finite integer ranges in comparisons

**Problem:** Tune22 (`58bbb6eeee`) correctly excludes unproved sentinel lanes
from direct comparisons, but mutable variables lose range evidence. Even the
finite version of diviter repeats band tests inside its hot condition.

**Implementation:**

1. Connect `mir_int_lane_interval` to the existing compact-loop facts rather
   than limiting it to literals, `len` and arithmetic trees over those leaves.
   Publish scoped facts for the versioned finite loop's parameters/counters.
2. Use the same fact for arithmetic, comparisons and array indices. For
   `x >= y; x -= y` prove the admitted initial ranges, positive divisor,
   monotonic decrease, counter bound and loop termination conditions. For
   increasing/decreasing induction, include the final executed update and
   the exit test in the range proof, not just values inside the body.
3. `mir_emit_int_lane_pair_in_band` omits checks only when both producer
   descriptors and the dominating region establish finite values. Joining
   branches takes the conservative union; assignment and opaque effects
   invalidate facts. A failed analysis leaves the existing sentinel path.
4. Prove nullable absence separately from finite numeric range. Preserve
   NaN/infinity/equality behavior and overflow degradation under D2.2.2 and
   S4.1.2; never substitute a machine wrap or raw sentinel equality.

**Evidence:** diviter and sum hot-loop conditions become native predicates
without repeated sentinel tests; cold sentinel cases retain the correct
outputs. Cross-check boundary values, null, NaN, both infinities, zero/negative
step, mutated bounds, nested branches, and final-update overflow on T0/JIT.

### T26-4 — Retain write ownership and call-boundary proofs

Read-proof reuse alone does not license an unchecked write. Extend existing
native store and owner-publication paths after T26-2 establishes proof scope.

| Event within a proved region | Required proof action |
|---|---|
| Same-owner, in-bounds, same-contract scalar store | Retain semantic/storage/extent facts; require exclusive ownership |
| Capture or new snapshot alias | Invalidate exclusivity even if storage/certificate is unchanged |
| COW detachment or root rebinding | Publish the new owner through its existing home, discard old address facts, prove the replacement |
| Append/splice/growth | Invalidate length/data facts; retain contract only if the actual new carrier still proves it |
| Unknown, reentrant or mutating call | Kill facts it may affect; no assumed purity |
| Safepoint with stable semantic ownership | Keep only independently valid semantic facts; root the owner and reacquire interior pointers |

Prove exclusivity once per write region where no event can change it. COW
preparation must remain at a semantically valid first write: no allocation,
detachment or validation is moved before a zero-trip loop or an earlier
observable effect. If later invalid writes are possible, preserve the original
ordering and partial effects; do not prevalidate the whole loop transactionally.

Continue the existing evaluation order: evaluate/root RHS, evaluate key,
then load the current owner. A key call may allocate, capture or replace it.
Same-contract direct calls reuse admitted descriptors; uncertain public calls
enter `_b` and check once in the callee (D8.3.2–D8.3.3). Keep array `var`
write-back through the existing caller home. No extra specialized ABI,
conservative GC scanning, or unrooted cached buffer pointer is introduced.

**Evidence:** readonly, exclusive-write, snapshot-detach, interprocedural
capture, growth and owner-replacement tests. Unique eligible loops have zero
checked-store calls and zero copies; shared cases perform the required detach
and preserve aliases. Extend `typed_array_reuse` and Tune25's store fixtures.

### T26-5 — Close residual rows and publish the full comparison

After each mechanism lands, recompute the per-row contribution ledger. Trace
every remaining >10% R37 loss to emitted code and profiles. Fast_diff, fib,
nqueens and any remaining cube3d/call costs have explicit ownership here;
they are not assumed solved by the array tracks.

For each residual retain: fixed source/input hashes, old/new timings, output
checks, hot helpers/allocations, MIR difference, responsible commit or
unresolved attribution, and the proposed smallest shared correction. Use
isolated release comparisons where attribution needs confirmation. Fix only
an established cause; do not remove annotations, hand-hoist source operations,
change iteration counts or broaden vendor/backend scope to clear a target.

Generate `test/benchmark/benchmark_tune26.json` and a matching Tune26 report
through the existing runner/reporting code. Include all 63 current rows and
the exact recovery/annotation subsets. Do not overwrite Result37 or Result42,
or assign a new ResultN to a partial run. Unknown/failed rows remain visible
and cannot silently shrink a gate's denominator.

## 5. Performance gates

All ratios below are candidate time / control time; smaller is better.
Absolute times in §2 are orientation only. Thresholds use new same-host
interleaved measurements, with correctness and fixed-source checks first.

| Gate | Required result |
|---|---|
| G1: historical recovery | Typed execution geomean across all 43 frozen R37 rows <= **1.00x**; stretch <= **0.95x** |
| G2: tail recovery | Each of the 12 replay rows <= **1.10x** R37; record and resolve every other 43-row loss >10% |
| G3: annotation parity | Every eligible annotation-only pair has typed/untyped median <= **1.05x**, with the 95% paired upper bound <= **1.10x** |
| G4: positive array gain | At least 20% faster than frozen pre-Tune26 on each of cold typed admission, sustained bool access, and sustained int/float access that had identified avoidable work; parity alone does not close these targeted regressions |
| G5: current-suite preservation | Typed and untyped execution geomeans <= **1.00x** pre-Tune26 across all 63 rows; investigate and resolve repeatable per-row losses >5% |
| G6: end-to-end preservation | Separate same-source typed and untyped auto wall-time geomeans <= **1.05x** pre-Tune26; startup/compile and long-row losses reported separately |
| G7: mechanism | Eligible primitive admission visits zero elements; proved hot accesses omit redundant checks; required fallback/ownership behavior still passes |

G1–G7 are conjunctive. A gain in one family does not excuse a failed gate in
another. A statistically uncertain row stays unresolved; collect more samples
instead of relabeling it as a pass. A value below the useful clock resolution
uses a separately registered sustained diagnostic with both initialization and
steady-state results; retain the original corpus timing too.

G3 compares valid equivalent workloads. Some declared sources already infer
the optimal lane without annotations, so a tie is success there. For dynamic
sources whose annotations establish a new usable lane, the report must show
which operations disappear and measure the resulting gain. Full-contract
validation of genuinely unknown input remains required by S11.4.1v3.

Protect post-R37 wins explicitly: DeltaBlue, binarytrees, gcbench, json_gen,
typed Richards and the current text ports are in G5 even when their source
changed after R37. Track Tune25's documented ~2% Navier-Stokes regression
separately; its R37 recovery target remains visible in the 43-row ledger.

The 59-row typed/Node and typed/C2MIR ratios remain contextual reports,
with source changes labeled. The native C2MIR ports are a static reference
using MIR's C frontend; the retired Lambda C-text backend remains absent.

## 6. Validation and measurement procedure

1. Implement in ordered slices: T26-0 -> T26-1 -> T26-2 -> T26-3 -> T26-4 ->
   T26-5. At each slice retain the release hash, focused correctness/MIR
   evidence and paired deltas. T26-3 may be developed independently after
   the shared loop-fact design is fixed; do not duplicate that design.
2. Extend existing contract/admission and MIR emission fixtures. Each new
   Lambda `*.ls` has a matching `*.txt`; code-shape tests also have
   `*.mir-check`. Use the shared fixture discovery and ratchet machinery.
3. Exercise bool/int/float plus every affected sized width, empty/singleton
   arrays, nullable/refined/nested contracts, stale certificates, invalid
   ranks, OOB/negative/2^32 indices, COW aliases, typed `var` replacement,
   error propagation, and effects in key/RHS evaluation. Full-width scalar
   ownership and float payloads must survive forced collection.
4. Run the affected emission/ratchet and `make test-mir-gc-stress` gates,
   T0/JIT expected-output comparisons, and appropriate sanitizer coverage
   for memory/address changes. Then run **`make test-lambda-baseline`** and
   **`make test262-baseline`**, with zero regressions. Preserve every failure
   as a failure; do not modify the Test262 harness to mask it.
5. Rebuild with **`make release` after correctness builds**. Verify release
   and instrumentation state, archive the exact binary extensionless under
   `test/benchmark/exe/`, and hash it before timing. Never benchmark a debug
   executable left by a test target. No build/profile job runs concurrently
   with timing; inspect AC power/low-power mode and record the host.
6. For execution comparisons set `LAMBDA_TIER=jit` explicitly. Use identical
   log/cache settings on both sides. For current auto measurements select
   auto explicitly. MIR dumps and admission/COW profiles are separate runs;
   disabling logging suppresses optional MIR dumps.
7. Use at least 15 alternating pairs for short rows and seven for long rows,
   increasing counts for uncertain decisions. Record every sample and order.
   Across the R37/pre-Tune26/candidate comparisons, rotate pair-block order
   to avoid always measuring one comparison later. Compute paired-ratio
   uncertainty with a documented resampling procedure and fixed seed.
8. Run the final complete matrix through
   [run_standard_benchmarks.py](../../test/benchmark/run_standard_benchmarks.py)
   with suites `r7rs,awfy,beng,kostya,larceny,jetstream,text`, engines
   `mir,c2mir,lambdajs,quickjs,nodejs`, and `--typed`. Keep Node **v22.13.0**
   for historical Node comparisons. The guarded wrapper owns its release,
   profiling and Test262 pre-gates. A sequential matrix is the publication
   snapshot; interleaved evidence decides causal/performance gates.
9. Generate reports from JSON, audit source populations and statuses, and
   run `git diff --check`. Keep temporary artifacts under `./temp/` and
   promote gate evidence into the versioned benchmark artifact. No temporary
   filename alone is the durable evidence for completion.

## 7. Frozen historical row set

These 43 selected typed entry sources are byte-identical at the recorded
Result37 and Result42 commits. T26-0 must additionally verify input/import
identity and retain exact source versions if the live checkout later changes.
An old-engine parse/semantic failure is a visible unresolved historical row,
not permission to remove it from G1.

| Suite | Benchmark names |
|---|---|
| r7rs | fib, fibfp, tak, cpstak, sum, sumfp, nqueens, fft, ack |
| awfy | sieve, permute, towers, bounce, list, storage, mandelbrot, nbody |
| beng | binarytrees, fannkuch, pidigits, regexredux, revcomp, spectralnorm |
| kostya | brainfuck, matmul, primes, base64, levenshtein, json_gen, collatz |
| larceny | triangl, array1, diviter, divrec, paraffins, pnpoly, puzzle, quicksort, ray |
| jetstream | cube3d, navier_stokes, crypto_sha1 |
| text | fast_diff |

Archive identities from the benchmark JSON:

- R37: `test/benchmark/exe/lambda-v37-4758dc7165`, SHA-256
  `07fc09a4d4cd6c832dcc24e7916a8aa0b7f663b23be94666e20ec3187a20eb4e`.
- Original R42: `test/benchmark/exe/lambda-v42-584748bc54`, SHA-256
  `6d34ff2ccd008e0d4b70692101e5799ae1848cfcaead2066a7ca606c9373eef5`.

## 8. Implementation map and status ledger

| Track | Main existing source owners | Status | Required closeout evidence |
|---|---|---|---|
| T26-0 | `test/benchmark/run_paired_benchmarks.py`, `run_benchmarks.py`, report generator | Partially implemented | The frozen 43-row manifest and paired source/binary provenance exist; a durable generated Tune26 comparison report is still absent |
| T26-1 | `lambda/runtime/type_contract.{hpp,cpp}`, `lambda-eval.cpp`, `lambda-vector.cpp`, `collection_runtime.cpp` | Partially implemented | Exact declared-local contract proof and typed-array-only native formal selection landed; no size/counter evidence closes admission cost |
| T26-2 | `lambda/runtime/transpile-mir.cpp`, scoped facts and `mir_emitter_shared.hpp` | Partially implemented | Dense typed reads, BOOL coverage, and an independently proved readonly peer beside a `var` destination; full loop family and measured recovery remain open |
| T26-3 | Compact-loop analysis, `mir_int_lane_interval`, comparison lowering | Partially implemented | Narrow descending-sum induction and finite parity lowering, with sentinel fallback; diviter and broad range reuse remain open |
| T26-4 | Existing checked/native stores, scoped ownership, caller-home publication | Partially implemented | Same-contract `var` call proof and checked COW write-back remain correct; dynamic in-body snapshot capture still requires the checked store |
| T26-5 | Residual source owners established by profiling; benchmark/report tools | Open | All performance gates, residual attribution, generated report, and complete Lambda/Test262 baselines |

Use existing `lib` containers and shared helpers. No benchmark-name dispatch,
hardcoded workload constants, public type weakening, source workaround,
conservative rooting, vendor patch or C2MIR backend restoration belongs to
this round. Larger SIMD, arbitrary interprocedural specialization, general
OSR and broad AST/compiler rewrites remain separate work unless a measured
residual first establishes their necessity and scope.

Update this ledger with measured evidence as work lands. Tune26 is complete
only when typed arrays deliver the required gains, Result37 recovery and
current-suite preservation gates pass, and no correctness obligation remains
unreported.

## 9. Implementation evidence and remaining closeout

### Implemented slices

The current working tree makes the following scoped changes without changing a
language ruling.

- T26-0 freezes the Result37 43-row source population in
  `test/benchmark/tune26_manifest.json`, and the paired runner records source
  and binary provenance for exact manifest selections and source-pair runs.
- T26-1 records a full canonical `T[]` declaration proof in `MirVarEntry`,
  separate from its physical layout cache.  A matching declared local can
  reuse that proof after successful boundary admission.  An all-value function
  with typed-array formals now selects the existing raw-array witness body even
  when it has no scalar lanes or scalar return.  `var` formals deliberately
  retain the adapter because they may publish a replacement owner through the
  caller home (D3.3.3, D8.3.2–D8.3.3).
- T26-2 extends dense-loop analysis to BOOL, same-owner writes whose separate
  write proof is valid, and independently proved roots.  In particular, a
  `var` destination stays on the checked COW path while a distinct readonly
  source can use dense raw loads.  One uncertain root therefore no longer
  discards an unrelated read proof.
- T26-3 adds a deliberately narrow finite descending-sum induction and turns
  finite `n % 2 == 0` or `!= 0` predicates into a native low-bit test.  The
  sentinel/out-of-band arm retains existing remainder and numeric-comparison
  lowering, as required by D2.2.2 and S4.1.2.
- T26-4 carries the same declared contract through direct one-hop `var T[]`
  calls, reloads a possibly replaced owner, and retains checked COW stores.
  New fixture families cover typed-array-only formals, descending sum, parity
  including an out-of-band integer, `var` call boundaries, `var` bool-array
  stores, and the readonly-peer/destination combination.

Focused evidence at the last fresh correctness build was:

| Check | Result | Scope |
|---|---:|---|
| `test_mir_emission_gtest --gtest_filter='*tune26*:*typed_array_reuse*'` | 12/12 passed | MIR shape and ratchet fixtures |
| `test_lambda_gtest --gtest_filter='LambdaTypedPathTests.*'` | 10/10 passed | expected-output typed-path fixtures |
| `git diff --check` | passed | current working-tree patch |

A later experimental relaxation of `var ArrayNum` COW was rejected and fully
reverted.  In the reproducer `let snapshot = values; values[0] = 2.0`, a raw
store caused the snapshot to observe `2.0` rather than its old value.  The
expected result was `12 2`; the trial produced `22 2`.  This establishes that
call-boundary preparation alone does not prove exclusive ownership after an
in-body snapshot capture.  The current implementation keeps the dynamic COW
guard at that store, preserving S9.2.2 and D3.3.3.  The binary produced during
that rejected trial is stale, so the focused checks must be rebuilt and rerun
before any final correctness claim.

### Measured performance evidence

All timings below are release, same-host alternating paired measurements with
equal normalized output.  They are implementation evidence, not a completed
gate report; their raw JSON is presently under `temp/` and must be promoted
into a versioned report for closure.

| Candidate archive | Measurement | Result | Gate consequence |
|---|---|---:|---|
| `lambda-tune26-e86d7ff8b-t15` SHA-256 `9e5920187bc13584056a1b64434e42796d99c2aa5e083969c11fe1ef26ab4edc` | 43 frozen Result37 rows, 31 pairs | 0.998773x geomean | G1 passes for this earlier candidate only |
| same t15 archive | 43-row tail | 17 rows still exceed 1.10x | G2 fails |
| `lambda-tune26-e86d7ff8b-t16` SHA-256 `74adba47a17ab5af11bbceb024f935f3871d6f31042204b1964a0fdf0c8a4487` | Result37 Navier-Stokes, 31 pairs | 1.4119x, 0/31 wins | Better than t15's 1.4442x, but still fails G2 |

The t15 tail failures were `navier_stokes`, `fft`, `nqueens`, `paraffins`,
`divrec`, `permute`, `cpstak`, `tak`, `levenshtein`, `fib`, `storage`,
`nbody`, `crypto_sha1`, `brainfuck`, `fannkuch`, `cube3d`, and `mandelbrot`.
Their t15 ratios range from 1.1002x to 1.4442x.  The detailed samples are
`temp/tune26_r37_t15_full.json`; this temporary path is not durable gate
evidence.  The t16 readonly-peer change has only the Navier-Stokes probe
(`temp/tune26_r37_t16_navier_probe.json`), so it cannot inherit t15's full G1
result despite restoring the source state after the rejected COW experiment.

### Work still outstanding

1. Rebuild the reverted current source and rerun all focused tests, then run
   the required Lambda, Test262, MIR GC-stress, T0/JIT, and sanitizer checks.
2. Archive a new release binary and repeat the complete 43-row interleaved
   Result37 comparison.  G1 must be established on that exact binary, while
   every listed G2 tail row needs emitted-code/profile attribution and a
   root-cause fix.
3. Complete annotation-pair measurements and uncertainty reporting for G3;
   add the cold admission and sustained bool/int/float diagnostics with the
   required counters and size sweeps for G4 and G7.
4. Compare typed and untyped current 63-row execution plus auto wall time to
   the frozen pre-Tune26 control for G5 and G6.  Generate and check in
   `benchmark_tune26.json` and its Markdown report from those data.
5. Keep `var` writes COW-safe while expanding ownership proof only where a
   capture/alias analysis proves it.  The rejected snapshot result rules out
   treating a `var` parameter as permanently exclusive.
