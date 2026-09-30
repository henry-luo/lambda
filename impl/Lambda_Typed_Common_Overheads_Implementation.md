# Typed Lambda common-overhead implementation record

Status: in progress (2026-09-30). This records measured slices against the
[proposal](Lambda_Typed_Common_Overheads_Proposal.md); its §3 acceptance targets
are not met. No language ruling has changed. The implementation must preserve
**S4.1.1–S4.1.2** (total numeric operations), **S7.1.1v3** (nullable reads),
**S7.1.3v2** (checked writes), **S9.1.2–S9.1.3** (snapshots),
**D3.2.4v4** (trusted record representation), **D3.3.3v3** (array certificates),
and **D5.3.1–D5.3.4** (precise GC roots).

## P0: frozen comparison and executed evidence

The paired runner now records the complete production/bundled Lambda source
corpus, transitive benchmark sources, C sources, build configuration, MIR
revision, compiler/OS/environment flags, binary hashes, and input-stability
checks. The C2MIR runner retains every repeated raw sample with source/binary
provenance. The family report freezes six disjoint sets, retains all 63 typed
rows, computes paired bootstrap intervals, and joins the contemporaneous C2MIR
reference by source row. See `test/benchmark/{benchmark_provenance.py,
run_paired_benchmarks.py,run_c2mir_benchmarks.py,
report_typed_common_overhead.py,typed_common_overhead_families.json}`.

The A/A check over 63 rows and three pairs gave a 0.99715 overall ratio. The
current 63-row/three-pair release screen at
`temp/result49_common_overhead/p4_complete_typed63_screen.json` has equal
outputs and a 0.99586 overall ratio to the archived original release. Its
record-graph family is 0.9420, paired-bootstrap 95% interval 0.8308–0.9556;
the other four non-control families remain close to parity. Three pairs are a
screen, not an independent confirmation. The report with all row ratios is
`temp/result49_common_overhead/p4_complete_family_c2mir_report.json`.

The fresh pinned C2MIR reference ran 65/65 ports five times with valid output;
63 have matching typed Lambda rows. Their source-relative median ratios give a
2.8389× typed/C2MIR geometric mean. This is not a paired cross-language
experiment. Large residual ratios include `awfy/deltablue` 20.58×,
`text/microdiff` 19.58×, `kostya/base64` 18.65×, `jetstream/cube3d` 13.97×,
`jetstream/splay` 10.40×, and `text/prettier_ast` 10.21×. The C ports use
their own data representations and contracts, so these ratios identify
investigation priorities rather than removable Lambda overhead by themselves.

| Executed diagnostic | Finding | Next proof needed |
|---|---|---|
| Prettier AST, 16× diagnostic repetition | Sampling placed 604/3,844 samples at `lambda_type_check_env` and 442 at `fn_member_by_id`; COW counters recorded 735,232 exact trusted-map admissions, with no field reification. | Remove redundant boundary/access work while retaining **D3.2.4v4**. |
| Richards | Sampling found `fn_map_set` and `fn_index` among the largest helpers; prior counters recorded 710,700 unique map mutations but only 1,000 map copies. | Reuse place/shape facts across safe calls under **D4.4.4v4**. |
| Splay | The site census records 951,168 map copies / 62.8 MB; `splay_node` and the two rotations account for 94.5%. | Prove which dead snapshots can move under **S9.1.3**; nearby marks do not establish causality. |
| Towers / NBody | Towers had 8,191 unique ArrayNum mutations and no recorded shared copies; NBody had no recorded COW table activity. | Measure bounds, numeric, and call/root protocol before changing COW. |
| Three-way merge | Native sampling was led by memory copying, GC, indexed reads, string join, and map lookup. | Separate required text bytes from avoidable representation and ownership work. |

Sampling and `COW_EXEC_PROFILE` runs are diagnostics, not timing runs. The
current counters do not cover every guard, root publication, or helper edge.
An opt-in `LAMBDA_EXEC_PROFILE=1` JIT census now counts executed calls by
helper, raw, boxed, indirect, and unresolved entry name. The shared emitter
places the counter before each emitted call only when profiling is selected at
compilation; prologue stack probes and async frame entry are included. Ordinary
MIR has no profile import (**D5.4.4**). A GTest pins opt-in emission, zero
overflow, a known raw and helper count, identical JIT output, and absence of
the profile file when disabled. The reporter is
`test/benchmark/report_lambda_exec_calls.py`; 12 diagnostic TSVs and the
ranked JSON are under `temp/result49_common_overhead/p0_exec_profiles/`.
All 12 profiled outputs matched the same-source unprofiled release outputs
after removing benchmark timer lines, and the profiler overflow count was zero.

| Representative workload | Executed calls exposing remaining protocol |
|---|---|
| Prettier AST | 55.30M classified calls: 13.12M stack probes, 7.00M module-state lookups, 5.62M `fn_member_by_id`, 4.64M `it2s` |
| Three-way merge | 280.58M: 52.64M `item_at`, 41.38M `fn_len_l`, 31.38M each module-state lookup and stack probe, 25.35M `cow_bind_var` |
| Richards | 15.06M: 2.40M `fn_eq`, 2.23M stack probes, 2.19M `fn_index`, 1.51M `fn_member_by_id`, 1.19M checked map stores |
| SHA1 | 6.17M: 2.17M stack probes, 1.28M raw `safe_add`, 884,800 boxed `rol`, 632,000 each `base_type` and `fn_is` |
| Base64 | 1.00M: 666,600 `fn_index`, 333,300 `fn_strcat5`; only 202 stack probes |
| Fib control | 635,621 raw `fib` calls but only two stack-probe calls, because its zero-root entry uses MIR BSTART |

This is a call census, not a CPU-time attribution. It counts JIT-emitted
boundaries, including cold paths only when taken; it does not count C-to-C
calls inside runtime helpers, inlined arithmetic/access, guard outcomes,
or allocations. Those groups remain open P0 coverage.
The three-pair, all-63-row *unprofiled* release screen against the previous
build was 0.9959× overall with matching outputs; the call profiler added no
established off-path regression. Raw timing and family report:
`temp/result49_common_overhead/{p0_exec_profile_off_typed63_screen.json,
p0_exec_profile_off_family_report.json}`. Diagnostic counts were gathered in
separate runs and must not be used as timing samples.

The same opt-in JIT census now marks executed frame entries, exact root-slot
stores, and post-safepoint reloads. The disabled MIR has no counter import;
the optimization GTest pins both modes and a nonzero root event. On the
release diagnostic build, three-way merge executed **272.31M stores** and
**1.972B reloads**, concentrated in `merge_words` (951.89M reloads) and
`merge_lines` (749.13M). Prettier executed 73.17M stores / 124.10M reloads,
SHA1 10.62M / 101.91M, Richards 31.14M / 85.67M, and Splay 23.30M /
52.21M. All ten profiled workloads matched their unprofiled output and had
zero counter overflow. Artifacts: `temp/result49_common_overhead/
p0_root_exec_profiles/`. These are source-level executed protocol counts, not
machine instructions or CPU time; the release timing tests below show why
that distinction matters (**D5.3.1**, **D5.4.4**).

An opt-in `COW_EXEC_PROFILE_SITES=1` diagnostic records the caller PC of
actual share marks, unique writes, and shared copies, with the existing
aggregate counters retained as a cross-check. At compilation, MIR selects
profiling-only COW entries when this flag and `COW_EXEC_PROFILE` are both
enabled. Those entries capture their generated call PC and attribute the
ordinary helper's counter delta; the helpers themselves contain no site-only
branches. Runtime-only events appear in an explicit zero-PC fallback row.
`report_cow_sites.py` joins JIT PCs to `LAMBDA_MIR_LOG_CODE_ADDR` notices.
Ordinary MIR emits no site updates or per-operation site-flag tests
(**D5.4.4**).

The final diagnostic Splay run at
`temp/result49_common_overhead/p5_sites/splay_delta_report.json` counted
951,168 map copies / 62,777,088 bytes, exactly matching the aggregate totals.
Of these, 511,476 originated in `splay_node`, 204,491 in `rotate_left`, and
183,209 in `rotate_right`; together they account for 94.5% of copies. The
two rotation copies each follow a generated `map_set_cow` site. Their 206,071
and 184,752 share-mark counts are nearby, but temporal proximity does not
prove those marks caused the later copies: prior ownership and the runtime
spine test also matter. Exact source statements and alias conditions must be
proved before removing a mark under **S9.1.2–S9.1.3** and
**D4.4.4v4–D4.4.6**. This is diagnostic attribution, not a timing result.
The site-profile optimization GTest checks that per-site mark/copy/write totals
equal the aggregate counters, that a generated copy site is visible, and that
the separate sites file is absent when either profile switch is off. The
final delta-wrapper implementation passed the full **6,071/6,071**
Lambda/Input baseline, including 253 forced-GC and 202 MIR emission checks.

Capturing return addresses inside ordinary COW helpers left a 1.0314× CD
regression over nine paired release runs even with the site flag off. Moving
all site work into the opt-in wrappers restored CD to 0.9961× against the
pre-census release over nine new pairs, with equal output; the 63-row/three-pair
screen was 0.9942× overall. This is a profiler-overhead correction, not an
ownership speedup. Raw runs are
`temp/result49_common_overhead/{p5_wrappers_vs_p4_record9.json,
p5_site_delta_vs_p4_record9.json,p5_site_delta_typed63_screen.json}`.

The compiler report now measures fresh-process transpile/JIT time and peak
process RSS at compile completion, alongside MIR instructions/functions. Both
engine revisions were rebuilt with identical additive instrumentation in
isolated source trees. Over 63 rows and three interleaved fresh-process
repetitions, candidate/control geometric means were 1.0067× compile time,
0.9956× peak RSS, and 1.0005× MIR instructions. The 1.529× three-way-merge
compile-time median has two candidate outliers and needs a longer confirmation
before being treated as a code regression. Raw samples and hashes are in
`temp/result49_common_overhead/p4_compiler_metrics_63x3.json`.

The initial address-span proxy for generated code size was withdrawn: MIR's
executable allocations can be separated by 1 MB or 85 MB in otherwise
identical DeltaBlue runs. The final separate diagnostic pass sums MIR's actual
per-function `len=` values from its level-0 generator log and appends across
transitive module contexts. It does not contaminate compile-time samples. In
the fresh 63-row/three-repeat campaign, final-candidate/original geometric
means were 0.9941× entry-module compile time, 0.9928× process peak RSS,
1.0005× entry-module MIR instructions, and 1.0002× exact generated bytes
across all MIR contexts. No row exceeded 1.25× compile time or 1.10× code
bytes. Raw samples, function counts, and binary hashes are in
`temp/result49_common_overhead/p4_final_compiler_metrics63_exact.json`
(**D8.2.5v3**).

## Retained engine changes and checks

P1 stores proven non-collecting bodies in a compilation-owned, uncapped map,
narrows a cloned variant after emitted-MIR verification, and emits acyclic
sibling callees first. A new compile-owned direct-call fixed point proves a
closed scalar recursive component before lowering, keeps unknown/imported
edges conservative, and clones its raw-entry summary rather than publishing
the solution into a shared AST. Before linking, it scans every claimed emitted
body and rejects any collecting call; an optimization GTest injects a false
claim and verifies rejection. Declaration order, a mutual cycle, more than 64
bodies, and a forward allocating edge have MIR fixtures (**D5.3.2–D5.3.4**,
**D8.2.5v3**). The pre-lowering classifier is bounded to boolean control flow
and direct calls: seemingly scalar integer and float syntax can still emit
box_int64_value, push_d, or a checked return boundary. The emitted-graph
verifier caught those false claims during the full baseline, and the early
proof was narrowed before publication. Boxed wrappers, binder raw variants,
and non-boolean bodies still use conservative effects.
The full per-entry/variant solver remains open.

The nine-pair call-heavy release pilot against the preceding candidate was
near parity on Ack, Towers, NBody, Richards, DeltaBlue, and Spectralnorm;
Fib was 1.0371× (two candidate wins). Its finalized Fib MIR differed only in
address immediates, so that isolated timing change is not evidence of a
protocol regression. The 63-row three-repeat compiler campaign gave 0.9987×
compile time, 1.0045× peak RSS, and exactly 1.0000× MIR instruction and
generated-code size geomeans. No 63-row benchmark body gained code from this
conservative SCC slice, so its cross-workload performance value remains
unproved. Raw samples: `temp/result49_common_overhead/{p1_gc_plan_pilot9.json,
p1_gc_plan_compiler_metrics63_exact.json}`.

The scalar-boxing import audit corrected `push_d`, `box_int64_value`,
`box_uint64_value`, `int2it`, and `int2it_i64` from collecting to non-collecting
effects. The first three may write a reserved number-stack home but cannot
collect Lambda GC objects; the latter two encode an inline Item. Their number
stack effects remain distinct. Debug `AutoAssertNoGC` scopes and the
fail-closed import audit protect the classification (**D5.2.2v3**,
**D5.3.2**). A MIR fixture pins rootless raw producers and one-slot
live-string callers for i64, u64, and a tiny float; forced-GC output matches.
The 63-row/three-pair release screen against the rootless-probe predecessor
was 1.0066× overall with equal outputs, so this is a proven root-protocol
reduction without a demonstrated broad timing win. Artifacts:
`temp/result49_common_overhead/{p1_pushd_nogc_pilot9.json,
p1_scalar_home_nogc_typed63_screen.json,p1_scalar_home_nogc_family_report.json}`.

An audited P1 follow-up classifies `fn_len_l`, `fn_len_a`, and `fn_len_s` as
non-collecting, non-reentrant observations of live metadata. They are not
marked pure scalar calls because their receiver can mutate; `fn_len_e` remains
MAY_GC because spread-key flattening allocates. The transitive source audit
verified 89 NO_GC imports and 285 call-graph nodes. The focused profiler
GTest saw its raw length function's root stores fall **6→2** and reloads
**11→3**; the other operations remain unchanged. In the full merge diagnostic,
stores fell **272.31M→229.37M** and reloads **1.918B→1.643B** against the
preceding string-lane build, with exactly 41.382M `fn_len_l` calls in both.
Prettier dropped 2.64M reloads. Yet nine paired unprofiled release runs were
near parity: merge **0.9994×** (5/9 wins) and Prettier **1.0042×** (4/9); the
63-row three-pair screen was **0.9961×** overall with equal output. The
unremoved helper calls, backend optimization, or other work can dominate
those removed MIR protocol events; counts alone do not assign CPU time.
Artifacts: `temp/result49_common_overhead/{p1_readonly_length_text9.json,
p1_readonly_length_typed63_screen.json,p1_readonly_length_family_report.json,
p1_readonly_text_three_way_merge.tsv}`. The complete Lambda/Input baseline
passed **6,085/6,085**, including 258 forced-GC and 207 MIR emission checks
after the stronger rootless/one-root expectations were pinned (**D5.3.1–D5.3.2**).

P2 uses exact int53 interval endpoints for products, avoiding host floating
rounding at the finite/poison boundary (**S4.1.2**). An attempted additional
immutable-local interval rule produced identical MIR in its representative
pilot and was removed. A new dominated-flow interval intersects literal
comparison bounds only within the true arm of ordered tests or the appropriate
equality arm. Immutable bindings and plain `pn` parameters with no whole-body
rebind/`var` borrow are eligible; uncertain nested closures decline. Complete
finite bounds remove the hot `int53` slow-add arm. A one-sided bound, branch
join, or rebound retains it. The MIR fixture and JIT/interpreter/forced-GC
outputs pin both sides (**D2.2.2**, **S4.1.2**, **S9.1.3**). Its isolated
63-row/three-pair release screen was 0.9964× with equal outputs; the 31-pair
Fib rerun was 0.9891× and showed that the three-pair Fib outlier was noise.
Compilation was 1.0029×, peak RSS 0.9996×, and finalized MIR instructions
and generated bytes were exactly unchanged across the 63 benchmark rows.
This proof presently fires in its structural fixture, not the frozen corpus;
no executed-work or broad timing win is claimed. Raw artifacts:
`temp/result49_common_overhead/{p2_branch_interval_typed63_screen.json,
p2_branch_interval_family_report.json,p2_branch_interval_fib31.json,
p2_branch_interval_compiler_metrics63_exact.json}`.

The same dominated scope now records a stable binding as non-null after the
true arm of `x != null` or false arm of `x == null`. On `float?` arithmetic it
removes operand and result null propagation while the unguarded and rebound
forms retain it. The MIR fixture and forced-GC/tier-parity output pass
(**S7.1.1v3**, **D2.2.2**). The full Lambda/Input baseline passed
6,066/6,066 at that point. Its isolated 63-row/three-pair release screen
was 1.0050× overall against the interval predecessor, with equal outputs;
no broad runtime benefit is established. Raw artifacts:
`temp/result49_common_overhead/{p2_nonnull_flow_typed63_screen.json,
p2_nonnull_flow_family_report.json}`.

A dominated `0 <= i < len(a)` fact now proves a rank-one typed-array read
in-bounds when the index and array homes are stable for the complete function.
The reversed `<` comparison is recognized; one-sided, different-array,
branch-join, and rebound cases keep the nullable fallback (**S7.1.1v3**,
**D2.5.3**). This extension exposed a pre-existing direct-call carrier bug:
a raw `float?` result's null sentinel was boxed as `nan` at an Item consumer.
The call nullability oracle and native return boxing now consult the declared
return contract only when it names the raw lane. An initial change boxed the
normal arm of a parameter-error join using the callee's raw lane instead of
that join's logical carrier; Knucleotide and two adapter/parity tests caught
the resulting null/`inf` corruption. Keeping the join's existing carrier
conversion and matching the physical nullable lane fixed those failures.
The MIR/forced-GC fixture verifies the corrected null result.
The full Lambda/Input baseline passed 6,068/6,068 after that correction.
The branch-only 63-row/three-pair release screen was 1.0063× overall with
equal output and no established family gain. Raw artifacts:
`temp/result49_common_overhead/{p2_branch_index_typed63_screen.json,
p2_branch_index_family_report.json}`.

The same array-length relation now applies to an ordinary counted while-loop
when the counter starts at zero, the array length is stable, and exactly one
tail `i = i + 1` writes the counter. Earlier writes and nested captures
decline. The nullability consumer shares this proof, so a positive loop body
emits one raw load and arithmetic operation without a second bounds or null
branch; the mismatched-array and early-write bodies retain their fallback
(**S7.1.1v3**, **D2.5.3**). MIR and forced-GC tests pass.

The completed 6,070/6,070 Lambda/Input baseline covered this loop form.
Its isolated 63-row/three-pair release screen against the branch-only build
was 0.9987× overall with equal outputs. Fasta was 0.9827× in that screen but
1.0012× over a separate 31-pair run (12 candidate wins), so the apparent
Fasta gain was noise. No broad runtime benefit is established. Artifacts:
`temp/result49_common_overhead/{p2_loop_index_typed63_screen.json,
p2_loop_index_family_report.json,p2_loop_index_fasta31.json}`. Compiler metrics
over all 63 rows were 0.9943× compile time, 1.0015× peak RSS, 0.9997× MIR
instructions, and 0.9998× generated bytes. Fasta itself lost 45 finalized
MIR instructions and 224 generated bytes but gained no measured execution
time, consistent with a cold or backend-redundant path. Exact raw metrics:
`temp/result49_common_overhead/p2_loop_index_compiler_metrics63_exact.json`.

An exact packed-parameter admission now tests the complete tag, integer
subtype, and reserved payload bits for i8/i16/i32/u8/u16/u32 before skipping
`coerce_num_sized`; other inputs keep the original checked/coercing path.
`int()` opens a proven-total compact integer payload directly as a native
IntLane. This removes one helper call at each admitted parameter crossing and
another for `int(x)` in the representative fixture (**S4.4.1**,
**D2.4.1–D2.4.3**). A separately labeled one-million-call u32 microbenchmark
measured 0.4862× time over nine alternating release pairs, with equal sum;
it is not a frozen-corpus result. No frozen row appears to use the exact u32
parameter shape, so this mechanism does not establish broad improvement.

Parity probes exposed two previously unguarded u32 errors: the MIR shift's
width-zero arm fell through into a host-width shift, and nested bitwise or
u32 arithmetic masked `ItemError` into a numeric payload. The zero arm now
bypasses computation; fallible compact operands retain a canonical-Item
guard and boxed operator fallback. `int()` returns a boxed success/error join
for such producers, using its direct u32 decode only on the successful tag.
Nullable native-int shift operands stay on the boxed helper. The MIR and
JIT/interpreter/forced-GC fixture pins wide counts, negative counts, nested
errors, and nullable operands (**S4.4.4**, **S11.4.1v3**,
**D2.4.1–D2.4.3**). The retained error path adds a cold return-boundary check
in SHA1's `rol`; the existing MIR sidecar now records that diagnostic arm.

The corrected release's nine-pair SHA1 pilot was 1.0307×, with one candidate
win, against the pre-slice release. The complete 63-row/three-pair screen had
equal output and a 1.0016× overall ratio; the bitwise/bytes family was
1.0076× (paired-bootstrap interval 0.9823–1.0272). This is a correctness and
microbenchmark improvement, not a cross-family speedup. Raw artifacts:
`temp/result49_common_overhead/{p2_u32_param_micro9.json,
p2_compact_error_safe_pilot9.json,p2_compact_error_safe_typed63_screen.json,
p2_compact_error_safe_family_report.json}`.

P2 now carries a proven total int-valued call through a compound machine
index. The index planner consults the existing physical-carrier, null, and
error oracles; a nullable or fallible call keeps the boxed path. The existing
certified pointer-array reader also handles declared `string[]` alongside
reified map arrays, preserving its `item_at` fallback and out-of-range null
under **D2.4.1–D2.4.3**, **D3.3.3v3**, **S7.1.1v3**, and
**S11.4.1v3**. The MIR/forced-GC fixture exercises a direct read, a compound
index with `shr`, a negative-shift fallback, and an unproved array. The
pointer-array extension alone did not remove Base64's generic calls: its
compound indices were still boxed. The shared call-leaf proof completed that
chain; Base64's executed `fn_index` count fell from **666,600 to zero**, and
Hyphen's fell from **280,288 to 138** in separate diagnostic runs.

The paired release confirmation over 31 alternating pairs measured Base64 at
**0.5064×** (31/31 candidate wins) and Hyphen at **0.8356×** (30/31 wins),
with equal output. The apparent three-pair Knucleotide slowdown reversed to
0.9768× over 31 pairs (17 candidate wins), so it is not an established
regression. The complete 63-row/three-pair screen was **0.9865×** overall;
the frozen bitwise/bytes family was 0.8032×, interval 0.7811–0.8530, while
other families remained near parity. These are two strong workload gains, not
the proposal's three-family breadth target. Raw artifacts:
`temp/result49_common_overhead/{p2_native_call_index_base64_pilot9.json,
p2_native_call_index_confirm31.json,p2_native_call_index_typed63_screen.json,
p2_native_call_index_family_report.json,p2_native_call_index_base64.tsv,
p2_native_call_index_hyphen_before.tsv,p2_native_call_index_hyphen_after.tsv}`.
The 63-row/three-repeat compiler comparison was 1.0066× transpile/JIT time,
1.0012× peak RSS, 1.0023× finalized MIR instructions, and 1.0017× exact
generated bytes. The largest code increases were 4.4% for three-way merge
and 4.0% for Base64, below the proposal's review gates; raw artifact:
`temp/result49_common_overhead/p2_native_call_index_compiler_metrics63_exact.json`.
The complete Lambda/Input baseline then passed **6,080/6,080**, including
256 forced-GC and 205 MIR emission cases. The GC effect audit verified 86
NO_GC imports; the native-root audit checked 17,272 functions without a
hazard. Auto-tier release output also matched the previous build on the
structural fixture and Base64.

An additional P2 string-array arm guards the physical native pointer-lane
descriptor (array tag, lane flag/kind, and String pointer type) before loading
one checked slot. This lets `split()` results use a direct read even without
an explicit full-array certificate; certified arrays retain the existing
path, and all other carriers retain `item_at` (**D3.3.3v3**, **S7.1.1v3**).
The first guard accidentally read the map-shape pointer byte rather than
`Container.type_id` and regressed merge to 1.0468× over nine pairs; profiling
showed `item_at` increase from 27.29M to 52.64M. Correcting that root cause
reduced `item_at` to **zero** and reloads by 54.58M. The corrected nine-pair
merge run was **0.9256×** (9/9 wins, one-sided bootstrap upper bound 0.9617).
The 63-row/three-pair screen was **1.0140×** overall, with large noise in
sub-millisecond controls, so this is a confirmed workload gain, not a broad
family claim. MIR, interpreter/JIT, and forced-GC fixtures pin the direct
guard and cold fallback. Artifacts: `temp/result49_common_overhead/
{p2_native_string_lane_typeid_merge9.json,
p2_native_string_lane_typeid_typed63_screen.json,
p2_native_string_lane_typeid_merge.tsv}`.

The next P2 slice inlines `len` for flat arrays from the common `List.length`
header and retains the audited helper on the N-D shape path (**S8.3.1v2**,
**D5.3.1**). The MIR fixture checks the header read and the cold helper, and
an executed-call GTest requires the two N-D calls while flat-array calls
disappear. In three-way merge, `fn_len_l` fell **41.382M→0** and root reloads
**1.643B→1.533B**. Nine paired release runs were nevertheless **0.9940×**
(5/9 wins); the 63-row three-pair screen was **1.0057×**, with all outputs
equal. The removed helper calls have no demonstrated CPU-time benefit in this
workload, so this slice is retained for the direct length representation, not
claimed as a broad speedup. Artifacts:
`temp/result49_common_overhead/{p2_inline_array_length_merge9.json,
p2_inline_array_length_typed63_screen.json,p2_inline_array_length_merge.tsv}`.

The dense numeric-array root guard had a separate representation error: it
loaded `Map.type` at byte 8 instead of `Container.type_id` at byte 0. That
made a valid `ArrayNum` fail its one-time loop guard and run the generic loop
copy. The corrected guard keeps the rank/layout, element-lane, and full
certificate tests before any direct access (**D3.3.3v3**, **S7.1.1v3**).
The 63-row three-pair release screen was **0.9909×** overall; the numeric-array
family was **0.9813×** with a confidence interval crossing 1. Longer paired
checks found quicksort **0.7701×** (9/9 wins), nbody **0.8878×** (8/9), sieve
**0.9062×** (6/9), and Navier–Stokes **0.9848×** (9/9), while Cube3D was
**1.0040×** (5/9). MIR coverage pins the header byte and adjacent flags
check. The opt-in dense-loop profiler found 4,936 `partition` guard attempts
and 4,936 fast-arm entries in quicksort. The call/root counts were identical
before and after the header correction, so the measured difference comes from
the loop arm selected, not eliminated helper calls. Its focused GTest checks
two attempts and one fast entry for a valid and short carrier, preserving the
checked error on the latter. The final full Lambda/Input baseline passed
**6,086/6,086**, including 258 forced-GC and 207 MIR emission cases. The GC
effect audit verified 89 NO_GC imports; the native-root audit checked 17,275
functions without a hazard. Artifacts: `temp/result49_common_overhead/
{p2_dense_typeid_typed63_screen.json,p2_dense_typeid_family_report.json,
p2_dense_typeid_numeric9.json,p2_dense_quicksort_branch_debug.tsv}`.

P3 replaces the native stack-probe helper with MIR's supported BSTART
stack-pointer primitive only in entries with zero GC root slots. The generated
guard still compares the current stack position with the context limit and
branches to the existing recovery path (**D5.1.2**, **S7.11.1v2**,
**S7.11.4**). A two-second native Fib sample attributed 178 of 1,543 samples
(11.5%) directly to the previous probe leaf. Using BSTART in every entry
improved the 63-row screen to 0.9765× against the preceding candidate, but
Bounce regressed to 1.0543× in a 31-pair confirmation. Restricting the new
probe to zero-root entries retains the scalar-call gains while leaving the
root-bearing raw Bounce functions on the audited helper. The restricted
63-row/three-pair screen is 0.9781× overall; the frozen scalar-call family is
0.9071× (paired-bootstrap 95% interval 0.8957–0.9488). Bounce is still
1.0326× in a separate 31-pair run, so its residual microsecond-scale
regression remains to investigate. The 63-row compiler comparison gives
0.9945× compilation time, 1.0007× peak RSS, unchanged finalized MIR
instruction count, and 0.9942× exact generated bytes. MIR checks pin both
the rootless BSTART and root-bearing helper path; an optimization GTest
pins JIT stack-overflow recovery. Artifacts:
temp/result49_common_overhead/{fib_stack_sample.txt,p3_bstart_pilot9.json,
p3_bstart_typed63_screen.json,p3_bstart_regression31.json,
p3_rootless_pilot9.json,p3_rootless_regression31.json,
p3_rootless_typed63_screen.json,p3_rootless_family_report.json,
p3_rootless_compiler_metrics63_exact.json}.

P4 reads aligned named fields directly even when another field is byte-packed;
nullable receiver bool/float reads preserve the null Item. Selected aligned
packed path stores take the direct lane, while misaligned and dynamic stores
keep the checked fallback (**D2.6.1**, **D3.2.4v4**, **S7.1.1v3**). A data-zone
pointer is reacquired after an allocating right-hand side before direct field
writes (**D4.4.4v4**). Exact trusted-map admissions and exact array certificate
crossings now return before the general runtime admission ladder. Their
structural and forced-GC fixtures live under `test/mir/lambda/`.

The P4 field-read pilot reduced Richards time to 0.7769× over nine pairs but
left Splay at 0.9757× and three-way merge at 1.0011×. The packed-path-store
pilot reduced Richards to 0.8661× versus the preceding field build and
DeltaBlue to 0.8869× in its separate nine-pair run. These are mechanism pilots,
not additive portfolio estimates. An exact-map admission pilot improved
Prettier AST to 0.9815× over nine pairs; the 63-row/three-pair screen was
0.9881× but its sum of row medians was slightly worse, so breadth is not
confirmed. An exact-array-certificate pilot changed Prettier to 0.9896× and
Cube3D to 0.9803×, with sampled numeric rows near parity. A broader outer
`lambda_type_check_env` fast path regressed Prettier and Cube3D by about 7–8%
over nine pairs and was removed. This failed experiment remains distinct from
the retained inner-admission rule.

For dynamic reads of a closed declared record, the builder now prepares its
existing `TypeMap` name index once when it has at least four unique named
string fields. A runtime `NameId` read uses that index only when there is no
spread and the table is complete; other shapes retain source-order traversal.
This keeps byte-key and last-writer semantics under **D3.4.3v3** and
**D4.6.1v2–D4.6.2v2**. The nine-pair Richards pilot was 0.9396× (8/9 wins),
while DeltaBlue, Prettier AST, Splay, and three-way merge were near parity.
The immediate-predecessor 63-row/three-pair screen was 0.9958× overall and
0.9962× for record graphs; the gain remains narrow rather than a P4-wide
effect. A dynamic-read/spread fixture under `test/lambda/` pins the semantic
fallback.

A trusted-record hash lookup in `fn_member_by_id` was also rejected: although
outputs matched, nine paired release runs regressed Richards to 1.0297×,
DeltaBlue to 1.0959×, and Prettier AST to 1.0272×. These declared shapes
usually lack a populated hash table, so computing a hash before the chain
fallback adds work to the very access path it was intended to shorten. The
semantic fixture remains useful for future access changes (**D4.6.1v2**).

An ordinary-map hash hint in `map_get_for_owner_keyed` was also removed. Its
nine-pair pilot against the preceding release was 0.9987× on Richards, 1.0131×
on DeltaBlue, 1.0007× on Prettier AST, 0.9951× on three-way merge, and 0.9971×
on Splay, with equal outputs. A transition-tree table can also contain a
same-byte descendant with a different NameId; such a miss still requires the
ordered chain under **D3.4.3v3**. The attempted fixture used an index assignment
that triggered an interpreter fallback, so it did not establish JIT coverage
and was removed with the unproductive fast path.

A bounded `len(T[])` leaf-inline extension removed three DeltaBlue `vec_size`
MIR call sites but changed its 31-pair median to 1.0028× (15/31 candidate
wins). It was removed because the executed workload showed no measurable
benefit. The existing array-length body and caller already use short native
paths; shrinking static call counts alone did not meet P3's performance gate.

A P4 length-stability experiment reused the existing whole-body resize/alias
proof to keep an ArrayNum length register across collecting calls while still
reloading its relocatable data pointer (**D4.4.4v4**). A MIR/forced-GC fixture
confirmed the intended one-load shape and unchanged results. On release,
however, Navier–Stokes slowed to 1.0310× in nine pairs and 1.0297× in a
separate 31-pair confirmation (one candidate win). The large function lost 35
MIR lines but kept its length register live across many calls; generated code
grew 0.05%, consistent with backend register-pressure costs. This source-level
rule and its fixture were removed. A shorter-lifetime placement rule needs
executed and backend evidence before revisiting it.

The latest Lambda/Input baseline passed **6,086/6,086**, including 258
forced-GC cases and 207 MIR emission checks. The GC effect audit verified 89
NO_GC imports and 285 call-graph nodes; the native-root audit checked 17,275
functions without a hazard. Earlier per-slice release numbers retain their
archived binaries and source provenance.

## Current cumulative release checkpoint

Against the frozen original release, five paired executions of all 63 typed
rows gave **0.9411×** geometric-mean time, with equal output on every row.
The predefined scalar-call, numeric-array, and bitwise/byte families measured
**0.9099×**, **0.9424×**, and **0.8160×** respectively, with their paired
bootstrap intervals below parity. Record/graph and text/collection families
did not establish a family-wide gain. A fresh five-run C2MIR campaign passed
all 65 native ports; the 63 matched source-relative rows put typed Lambda at
**2.7732×** the C2MIR median. This cross-language ratio is not an engine-only
speedup measure because the source representations and semantics differ.
The cumulative run remains above the proposal's **0.90×** overall target and
is a checkpoint, not final confirmation. Raw artifacts:
`temp/result49_common_overhead/{p2_dense_profile_vs_original_typed63x5.json,
p2_dense_profile_vs_original_fresh_c2mir_report.json,
c2mir_p2_dense_profile_fresh5.json}`.

The SHA1 regression trace found a redundant return-boundary check after
int(u32-bitwise-result). The outer int() call is non-defecting in the
solver, but a negative shift in its operand still produces an error Item.
The U32 lowering already guards its successful Item and returns a plain int;
the declared int return can therefore forward the error and skip
lambda_type_check on the success path (**S4.4.4**, **S7.9.3**,
**S11.4.1v3**, **D2.4.1**). The proof is restricted to this exact U32
lowering. The MIR fixture checks that the cold conversion and error branch
remain while the second type check disappears; forced-GC and tier parity
pass. SHA1 improved **0.9887×** against the preceding candidate over 31
paired release runs, with 21 candidate wins and equal output. Across all
63 typed rows, the three-pair screen was **0.9963×** versus that candidate;
its six family intervals include parity. This recovers only part of SHA1's
earlier regression and is not a breadth result. Raw artifacts:
temp/result49_common_overhead/{p2_compact_cast_sha1_stage31.json,
p2_compact_cast_typed63_screen.json,p2_compact_cast_typed63_report.json}.
The complete Lambda/Input baseline passed **6,088/6,088**, including 259
forced-GC cases and 208 MIR emission checks. The GC effect audit verified
89 NO_GC imports and 285 graph nodes; the native-root audit checked 17,276
functions without a hazard.

The next equality slice targets source-level boolean literals compared with a
dynamic Item. The compiler now masks the dynamic tag and bool payload and
compares them with the literal directly. The comparison ignores unused bool
payload bits as `fn_eq` does and leaves nonliteral/deep equality on the
runtime path (**S5.1.1–S5.1.2**). The MIR and forced-GC fixture
`bool_literal_dynamic_equality` pins both sides of that boundary. On Richards,
the opt-in executed-call census fell from 2,400,300 to 410,300 `fn_eq` calls;
this is a call-count result, not a CPU attribution. Its 31-pair release pilot
measured **0.9781×** against the preceding candidate, with 27/31 candidate
wins and identical output. The initial all-63 three-pair screen measured
**0.9963×** overall; every family interval includes parity, so the gain is
still narrow. The final immediate-literal refinement was not in that all-row
screen and showed no separate gain over the first boolean candidate in its
31-pair Richards comparison. Artifacts:
`temp/result49_common_overhead/{p2_bool_literal_richards_before.tsv,
p2_bool_literal_richards_after.tsv,p2_bool_literal_imm_vs_prior_richards31.json,
p2_bool_literal_typed63_screen.json,p2_bool_literal_typed63_report.json}`.

The remaining integer-equality sites in Richards are being tested with a
guarded packed-Item comparison. The direct word comparison runs only when each
dynamic operand has the `ITEM_INT` tag; mixed numeric representations, null,
and error retain `fn_eq`/`fn_ne` (**S5.1.1–S5.1.2**, **S4.1.2**). The
`packed_int_equality_guard` MIR fixture includes `float`, `int64`, `u32`, null,
and error cases. The complete Lambda/Input baseline passed **6,092/6,092**,
including 261 forced-GC cases and 210 MIR emission checks. The 31-pair
Richards release comparison measured **0.9752×** with 31/31 candidate wins and
identical output. The opt-in JIT call profile fell from 410,300 `fn_eq` and
11,700 `fn_ne` calls to zero for both helpers; the cold fallback remains in
the MIR. The all-63 three-pair screen measured **1.0019×** overall, while the
predefined record/graph family measured **0.9725×** with its interval below
parity. The large apparent three-pair changes in `sumfp`, `sieve`, and
`json_gen` disappeared over 17 pairs. `hashmap` retained a **0.8480×** ratio
with 17/17 candidate wins, and `three_way_merge` was **1.0085×**. These are
mechanism-specific gains, not evidence for the proposal's broad 0.90 target.
Artifacts: `temp/result49_common_overhead/{p2_packed_int_eq_richards31.json,
p2_packed_int_eq_profile_comparison.json,p2_packed_int_eq_typed63_screen.json,
p2_packed_int_eq_typed63_report.json,p2_packed_int_eq_focus17.json}`.

The packed-int arm now also emits an in-range decimal literal as its canonical
Item immediate, instead of emitting a redundant int53 boxing guard. The
`packed_int_equality_guard` fixture adds NaN and infinity to its mixed-type
fallback checks (**S4.1.2**, **S5.1.1–S5.1.2**).

P5 now extends the existing scalar-record call binding to a second producer:
an exact trusted record literal bound locally and observed only through fixed
fields. The producer must have the destination's fields in order, each field
must have a proven redundant boundary, and the binding may neither mutate nor
escape. It reuses the record-result field homes; a literal whose value escapes
or whose field requires a runtime check still builds the ordinary map
(**S9.1.2**, **D2.4.1**, **D8.2.6**). A bool literal's narrow static Type is
recognized as an exact value for a bool record slot. The
`scalar_record_literal_binding` MIR fixture pins all three cases, and tier
parity and forced-GC pass. A same-source release probe executed 200,000
`heap_calloc_class` calls before the change and zero after; 31 alternating
timing pairs of a longer two-million-call probe measured **0.2525×**, with
31/31 candidate wins and equal output. This is a narrow admitted-shape result.
The all-63 typed three-pair screen versus the preceding candidate was
**1.0028×** overall and showed no established family gain. The complete
Lambda/Input baseline passed **6,094/6,094**, including 262 forced-GC cases
and 211 MIR checks. Artifacts: `temp/result49_common_overhead/
{p5_record_literal_probe.ls,p5_record_probe_calls.json,
p5_record_literal_probe31.json,p5_record_literal_typed63_screen.json,
p5_record_literal_typed63_report.json}`.

Longer controls distinguish the short-screen noise from an effect of that
record change: in 17 alternating release pairs, `sumfp` was 1.0000×,
`three_way_merge` 0.9970×, and `splay` 0.9993×. Their emitted Splay MIR has
the same 8,595 lines and 503 call sites before and after the change; only
address/source-site immediates differ. Cube3D was 0.9678× in this control,
but the record admission does not apply to it, so this is not attributed to
P5. Artifact: `temp/result49_common_overhead/
p5_record_literal_controls17.json`.

P3's next measured call reduction is the empty string literal. `word_at` in
three-way merge returns `""` on an out-of-range word; the ordinary literal
loaded that immutable value through the module slab on every call. A literal
whose `TypeString` has zero length now uses the existing process-lifetime
`ItemEmptyString` object. Nonempty literals retain their module constants
(**S5.1.4v2**, **D2.4.1**). The `empty_string_literal_direct` MIR fixture pins
both paths, and interpreter/JIT/forced-GC agree. An executed JIT profile of
the same three-way source changed `lambda_module_state_for_unit` from
31,383,005 to 528,005 calls; the three `word_at` raw variants each went from
20,570,000 root stores and 20,570,000 root reloads to zero. Seventeen
alternating release pairs measured three-way merge at **0.9562×**, 17/17
candidate wins. The 63-row three-pair screen was **0.9916×** overall with
unchanged output; no unrelated family gain is established. The complete
Lambda/Input baseline passed **6,096/6,096**, including 263 forced-GC and
212 MIR emission checks. Artifacts: `temp/result49_common_overhead/
{p5_three_way_jit.tsv,p3_empty_literal_three_way.tsv,
p3_empty_literal_merge17.json,p3_empty_literal_typed63_screen.json,
p3_empty_literal_typed63_report.json}`.

P2 now compares boxed string-success/error joins through a guarded pointer
path. The admission uses an explicit string return contract and a direct call
or immutable local; both operands are evaluated once. A string-tag pair uses
pointer identity and `fn_str_eq_ptr`, while null and error values retain the
original `fn_eq`/`fn_ne` path (**S5.1.1–S5.1.2**, **S7.1.1v3**,
**S11.4.1v3**, **D2.4.1**). `string_join_equality_guard` pins the successful
and error paths. The full Lambda/Input baseline passed 6,098/6,098. In the
three-way merge diagnostic, `fn_eq` fell from 21.747M to 10.428M calls, but
the new string-pointer helper was still classified as collecting and added
root traffic. An audit of its immutable String reads and `memcmp` call now
classifies `fn_str_eq_ptr` as non-collecting and non-reentrant. Its rootless
raw-call fixture is `string_pointer_no_gc`; the GC-effect and native-root
audits passed. A 17-pair release comparison of the combined guard and effect
change against the empty-literal release was 0.9805× on three-way merge, with
13/17 candidate wins and equal output. The separate effect correction removed
25.74M root stores and 120.428M reloads from `_merge_words` in the executed
diagnostic, but changed time by only 0.9937× over 17 paired runs. The
63-row/three-pair screen of the combined change was 1.0070× overall; it
established no broad gain. Artifacts: `temp/result49_common_overhead/
{p2_string_join_nogc_merge17.json,p2_string_join_nogc_typed63_screen.json,
p2_string_join_nogc_typed63_report.json,
p1_str_eq_nogc_merge17.json,p1_str_eq_nogc_three_way.tsv}`.

The same P2 guard now accepts a declared `string[]` element read, directly
or through one immutable local. That declaration describes the successful
element, not the read's Item carrier: the string tag is checked at runtime,
and an out-of-range null uses the generic equality path (**S7.1.1v3**,
**D2.4.1–D2.4.3**). `string_array_join_equality` pins equal, unequal, and
out-of-range operands; forced-GC and tier parity agree. The complete
Lambda/Input baseline passed 6,102/6,102. Its executed three-way profile
reduced `fn_eq` from 10.428M to zero calls, with `fn_str_eq_ptr` rising from
12.870M to 15.367M. Total profiled helper calls fell by 7.93M; `_merge_lines`
root reloads fell from 656.084M to 509.212M. Seventeen alternating release
pairs measured three-way merge at 0.9930× (12/17 wins) with equal output.
The 63-row three-pair screen was 1.0022× overall with no established family
gain. This is a substantial protocol reduction with a small confirmed timing
effect in one workload, not a broad-performance result. Artifacts:
`temp/result49_common_overhead/{p2_string_index_three_way.tsv,
p2_string_index_eq_merge17.json,p2_string_index_eq_typed63_screen.json,
p2_string_index_eq_typed63_report.json}`.

P5 now keeps the original COW boundary for any non-string Item but skips its
call when a boxed string success reaches a local binding or insertion. The
same guarded-success oracle covers a direct string, explicit string-return
join, or declared `string[]` read; the physical string tag is tested after
evaluating the value once. The cold arm still marks a malformed or absent
read through the existing helper (**S9.1.2**, **S9.3.1**, **D2.4.1**).
`string_cow_capture_guard` pins both guarded calls and the dynamic fallback;
JIT, interpreter, and forced-GC output agree. In the executed three-way
diagnostic, `cow_bind_var` fell from 25.346M calls to zero and
`cow_capture_value` from 18.735M to 2,304. Root reloads fell by 442.401M.
Seventeen unprofiled release pairs measured three-way merge at 0.9479×,
16/17 candidate wins, with equal output. The 63-row three-pair screen was
1.0034× overall, and every family interval included parity, so this remains
a text-workload gain rather than the proposal's broad result. Artifacts:
`temp/result49_common_overhead/{p5_string_cow_guard_three_way.tsv,
p5_string_cow_guard_merge17.json,p5_string_cow_guard_typed63_screen.json,
p5_string_cow_guard_typed63_report.json}`.

P1 now classifies `cow_bind_var` and its profiled entry as non-collecting and
non-reentrant. Their current implementation marks a COW header bit, returns
the original Item, and updates only bounded diagnostic counters; a later
write owns any detach allocation (**D5.3.2**, **S9.1.2**). The import audit
verified 92 NO_GC entries and 287 call-graph nodes; the native-root audit
checked 17,288 functions. The `CowBindingDoesNotReloadLiveOwner` opt GTest
pins a required dynamic mark with one fewer live-owner reload. At equal
executed call counts, DeltaBlue lost 46,200 root stores and 289,400 reloads,
and Richards lost 2.412M reloads. Five nine-pair focus rows were near parity;
the 63-row three-pair screen was 0.9916× overall, but controls moved by a
similar amount and no family interval excluded parity. A separate 17-pair
Splay check was 0.9929× (10/17 wins), resolving its noisy nine-pair
regression. The DeltaBlue size ratchet was re-baselined after reviewing the
emitted MIR: root publication moves from the share mark to later allocating
calls, expanding `constraint_choose_method` from 1,659 to 1,696 instructions
while the whole module grows by only four. These counts do not establish a
CPU gain. Artifacts: `temp/result49_common_overhead/
{p1_cow_bind_before_deltablue.tsv,p1_cow_bind_deltablue.tsv,
p1_cow_bind_before_richards.tsv,p1_cow_bind_richards.tsv,
p1_cow_bind_nogc_focus9.json,p1_cow_bind_nogc_splay17.json,
p1_cow_bind_nogc_typed63_screen.json,p1_cow_bind_nogc_typed63_report.json}`.
The complete Lambda/Input baseline passed **6,105/6,105** after the reviewed
ratchet update, including 216 MIR emission, 267 forced-GC, and 41 Lambda opt
checks.

A broader P5 experiment skipped a COW helper for every tagged non-container
Item at these binding and insertion sites, preserving the helper for raw
container pointers (**S9.1.2**, **S9.3.1**, **D2.4.1**). It removed all
430,200 `cow_bind_var` calls in Richards and 10,080 in DeltaBlue. Yet five
nine-pair release focus rows were near parity, and the 63-row three-pair
screen was 1.0059× overall with no family improving by 5%. DeltaBlue gained
24 additional module MIR instructions over the string-only guard. The
generalized branch and its test expectations were removed; the exact string
success rule above remains. Artifacts: `temp/result49_common_overhead/
{p5_tagged_cow_richards.tsv,p5_tagged_cow_deltablue.tsv,
p5_tagged_cow_guard_focus9.json,p5_tagged_cow_guard_typed63_screen.json,
p5_tagged_cow_guard_typed63_report.json}`.

A P3 probe experiment admitted MIR `BSTART` for root-bearing entries with at
most 64 MIR locals, aiming to remove the native-stack helper from short typed
helpers while excluding large root-bearing bodies (**D5.1.2**). `word_at`'s
three raw variants had 53 locals and gained the inline probe; Bounce's hot
`random_next` raw body had 77 locals and retained the helper. The six-row,
nine-pair release pilot measured three-way merge at 0.9760×, DeltaBlue at
0.9833×, Prettier AST at 0.9898×, Richards at 1.0049×, and Splay at 0.9993×.
Bounce measured 1.0957× in that short pilot and 1.0341× over 31 pairs, though
the latter difference is only 0.003 ms and its 400 hot raw calls retained the
same helper. A local-count threshold is therefore not an established
cross-workload win or a reliable allocator criterion. The change was removed;
artifacts are `temp/result49_common_overhead/
{p3_small_body_probe_focus9.json,p3_small_body_probe_bounce31.json,
p3_small_probe_bounce_control.tsv,p3_small_probe_bounce_candidate.tsv}`.

P0 now has an optional caller breakdown for its existing executed-call census.
`LAMBDA_EXEC_PROFILE_CALLERS=1` records `caller -> target` instead of aggregating
all calls to one helper; it emits no instrumentation when profiling is off
(**D5.4.4**). The caller GTest pins the two distinct `fn_len_l` sites in the
read-only-length fixture. A Prettier diagnostic attributes 4.170M
`lambda_module_state_for_unit` calls to `flat_length` and 0.926M to
`render_doc`; the caller table reported zero overflow. Typed three-way merge
instead takes direct list lengths, so the old `fn_len_l` hypothesis does not
apply to that final source. Artifacts:
`temp/result49_common_overhead/{p0_prettier_caller_sites.tsv,
p0_merge_caller_sites.tsv}`.

P4 now uses a context-local JIT module-state hint. Source-backed MIR names a
process-stable logical unit but previously called
`lambda_module_state_for_unit` once on every entry that read a module constant
or binding. Each `LambdaModuleState` carries the layout key it currently
answers for; the JIT execution boundary selects the current slab in
`EvalContext`, including each imported initializer, and a scope restores the
previous hint after execution or recovery. Generated entries compare that
context-owned key with their module layout and use the slab directly on a hit;
cross-module calls and absent/rebound hints keep the audited lookup. The
semantic `active_module_state` selector is untouched (**D5.4.3**,
**D7.2.1**, **D8.5.1v2**). A profiler GTest pins a zero-call own-module case and
an imported-function fallback; focused LambdaJS module tests pass. The typed
Prettier diagnostic drops 6.699M executed module-state helper calls to zero.

Against the preceding release, 17 paired runs confirm Prettier AST at
**0.9590×** (17/17 wins) and DeltaBlue at **0.9648×** (15/17). Three-way merge
is **1.0056×** over 17 and Raytrace3D **1.0004×** over 17; its apparent 0.654×
three-pair screen was noise. The all-63 three-pair ratio was **0.9950×**,
but excluding that Raytrace3D outlier gives **1.0017×**. No new independent
family gain is established. Thirty-one-pair checks of sumfp, nqueens, sieve,
queens, Bounce, and pidigits resolved the short-row screen swings to near
parity. Artifacts: `temp/result49_common_overhead/
{p4_jit_hint_focus9.json,p4_jit_hint_focus17.json,
p4_jit_hint_typed63_screen.json,p4_jit_hint_typed63_report.json,
p4_jit_hint_short_controls31.json,p4_jit_hint_prettier.tsv,
p4_jit_hint_import.tsv}`.

An additional BSS-load removal baked the logical unit ID into source-backed
MIR (**D5.4.3**), but its six-row nine-pair release pilot was near parity on
Prettier (1.0026×), DeltaBlue (0.9918×), merge (0.9909×), and Raytrace3D
(0.9919×), with SHA1 at 1.0117×. It was removed; the guarded context hint
above remains. Artifact: `temp/result49_common_overhead/p4_jit_imm_focus9.json`.
The retained guard adds seven MIR instructions to most eligible entries and
leaves its checked fallback in cold code; six MIR size budgets were reviewed
and re-baselined. The full-suite rerun passed **6107/6107**.

The next P2 slice reuses the existing stable-binding non-null fact at a trusted
record field read. A `Doc?` receiver in the false arm of `doc == null` is
non-null for the rest of that dominated branch, yet the direct reader had
excluded its required `string` field because the declared receiver type was
optional. That exclusion forced `fn_member_by_id`, an Item-to-string decode,
and generic length work in Prettier's hot `flat_length` routine. The direct
reader now consumes the branch fact when selecting and guarding its native
field lane; an unguarded optional receiver and a rebound procedural parameter
retain the generic null-preserving path (**S7.1.1v3**, **D3.2.6**).
`branch_nonnull_string_field.ls` and its MIR sidecar pin both sides of the
decision; the interpreter and forced JIT produce `hello||null|null`.
Executed `flat_length -> fn_member_by_id` calls fell from **1,744,640 to 0**
in the Prettier diagnostic. Relative to the preceding module-hint release,
Prettier is **0.9095×** over 17 pairs (17/17 wins); DeltaBlue and three-way
merge are at 1.0000× over 17. The seven-row nine-pair pilot matched output
throughout and showed no material regression. Artifacts:
`temp/result49_common_overhead/{p2_nonnull_prettier_calls.tsv,
p2_nonnull_field_focus9.json,p2_nonnull_field_focus17.json}`.

The same diagnostic still counted **1,744,640** `flat_length -> fn_len_s`
calls. An ASCII-header length fast path retained `fn_len_s` for non-ASCII
codepoint length (**S2.5.8**, **S8.3.1v3**), but removing that call did not
produce a robust gain: 17 pairs put Prettier at **0.9970×** and three-way
merge at **0.9953×**, with only 8/17 merge wins; Base64 moved to **1.0066×**.
The extra branch and code were removed, leaving the native helper. Artifacts:
`temp/result49_common_overhead/{p2_ascii_len_focus9.json,
p2_ascii_len_focus17.json}`.

P0's executed profile now counts each speculative record-field guard's entry
and fast arm by caller, source byte, and field name. A branch-proof fixture
records 31 attempts but only one fast hit at its `.value` site, so fallback
frequency is visible rather than inferred from static MIR. The counters emit
no instructions with profiling disabled (**D5.4.4**). The DeltaBlue diagnostic
then showed **zero fast hits** on several hot `w.vars[index].val` guards and
347,440 `c_execute -> fn_member_by_id` calls.

The next P2/P4 fix follows the root cause through the existing declared-path
contract resolver: a typed integer member used as an index carried a
`TYPE`-tagged AST annotation, so `ast_index_keys_select` mistook it for a type
selection and refused to recover the `Variable[]` element contract. It now
uses the declared integer value contract to classify that key as positional
(**S8.2.4v3**), and the MIR field reader consumes the admitted array element's
trusted record layout while retaining the invalid-index null arm
(**D3.3.3v3**, **S7.1.1v3**). The nested typed-index MIR fixture covers direct
int/bool reads, nullable out-of-bounds reads, and the still-generic string
read. In a diagnostic DeltaBlue run, `c_execute -> fn_member_by_id` fell
**347,440 -> 0**, and `c_execute` root reloads fell **5,399,500 -> 1,980**;
`DeltaBlue: PASS` remained. A 17-pair release comparison against the preceding
field-read release confirms **DeltaBlue 0.7022×** (17/17 wins) and
**Richards 0.9548×** (15/17); three-way merge is **0.9978×**. Prettier's
initial 17-pair **1.0111×** result settled to **1.0025×** over 31 pairs, a
0.72 ms median difference. The 31-pair controls likewise put Splay at
**0.9970×**, Raytrace3D at **1.0014×**, and knucleotide at **0.9974×**;
sumfp's 1.0167× ratio is only a 0.001 ms difference on a 0.060 ms region.
The 63-row three-pair screen matched every output and had a **0.9979×**
geometric mean; sub-millisecond outliers, notably knucleotide at 0.37–0.53 ms,
make that breadth estimate provisional. The fresh full baseline passed
**6112/6112**, including 269 forced-GC cases, 218 MIR emission cases, and the
new field-guard profile assertion. The GC effect and root-hazard audits pass.
The seven-row, three-repeat compiler pilot found DeltaBlue at **0.9809×**
compile time, **0.9659×** peak RSS, and **0.9863×** generated code bytes;
Richards was **1.0064×**, **0.9909×**, and **0.9941×** respectively. Prettier,
merge, and Splay code bytes were unchanged. Artifact:
`temp/result49_common_overhead/p4_trusted_nested_index_compiler_metrics_pilot.json`.
Artifacts: `temp/result49_common_overhead/
{p0_deltablue_field_guards.tsv,p4_deltablue_nested_index_calls.tsv,
p4_trusted_nested_index_focus9.json,p4_trusted_nested_index_focus17.json,
p4_trusted_nested_index_controls31.json,
p4_trusted_nested_index_typed63_screen.json,
p4_trusted_nested_index_typed63_report.json}`.

A subsequent P4 guard also accepts a declared rank-one array's trusted map
element when the index is an untyped local. The local key's AST annotation
does not identify a positional index, so a static classification would
discard valid type/selection behavior. Instead, the field reader speculates
the declared element layout and checks the live map's exact shape before a
direct load; failed checks still call `fn_member_by_id` (**S8.2.4v3**,
**D3.3.3v3**). `trusted_array_dynamic_index_field` pins the successful
field read, an out-of-bounds null, and a selection-key fallback. DeltaBlue's
executed `c_execute -> fn_member_by_id` calls fell from **80,880 to 12,000**,
with **68,880** field-guard hits; root reloads fell from **11.204M to
10.878M**. The 31-pair DeltaBlue release check measured **0.9426×** against
the preceding candidate (31/31 wins), with equal output. Knucleotide,
pidigits, and the other short-row controls were near parity over 31 pairs.
The full Lambda/Input baseline passed **6,114/6,114**. Artifacts:
`temp/result49_common_overhead/{p4_dynamic_index_deltablue_calls.tsv,
p4_dynamic_index_controls31.json,p4_dynamic_index_baseline.log}`.

Against the original archived release, a new five-pair, all-63-row campaign
of that cumulative P4 build measured **0.9232×** geometric-mean typed time,
with equal output throughout. Four predeclared mechanism families met the
5% point-estimate threshold with confidence intervals below parity:
scalar/calls 0.9105×, bitwise/bytes 0.8000×, record/graphs 0.8579×, and
text/collections 0.9443×. Numeric arrays measured 0.9534×, just short of
the 5% threshold; regression controls measured 0.9719× with an interval
including parity.
The overall **0.90×** acceptance threshold remains unmet. Raw pairs and
the family report are `temp/result49_common_overhead/
{p4_trusted_nested_index_vs_original_63x5.json,
p4_trusted_nested_index_vs_original_report.json}`. This comparison
predates the untyped-index guard above.

P2/P5 now gives a unique open `Array` with a boxed string value and spare
capacity a direct append. It checks the live string tag, array kind,
static/immortal flags, native-lane bit, representation certificate, COW bit,
reserved tail, and remaining capacity; every failed check keeps the ordinary
`pn_push_cow` path (**S2.5.1v2**, **S9.1.2**, **D2.6.5v3**). The source oracle
admits a direct string carrier, literal, declared `string[]` read, or an
immutable binding proved to hold one; the runtime tag guard protects nullable
and malformed values. `open_string_array_push` pins growth, snapshot detach,
a non-string fallback, and a declared `string[]` read that produces null
out of bounds. Forced JIT and interpreter output agree. In the executed merge
profile, `pn_push_cow`
fell from **20.803M to 4.809M** calls and root reloads from **782.119M to
606.185M**. A 17-pair release comparison against the preceding P4 build
measured three-way merge at **0.9477×** (15/17 wins), with equal output;
Richards, DeltaBlue, Prettier, Base64, and five other focus rows were near
parity. The all-63 three-pair screen matched every output and measured
**0.9888×** overall versus that P4 build, with short-row noise still visible.
The full Lambda/Input baseline passed **6,116/6,116** before the last fixture
extension; its updated fixture passed targeted MIR and tier-parity checks.
The fresh-process 63-row/three-repeat compiler check measured 0.9930×
transpile/JIT time, 0.9999× peak RSS, and 1.0006× generated code bytes in
aggregate. Three-way merge, the affected workload, measured 1.0726× compile
time and 1.0405× code bytes, below the proposal's review thresholds.
Artifacts:
`temp/result49_common_overhead/{p2_string_push_merge_before.tsv,
p2_string_extended_merge_calls.tsv,p2_open_string_extended_focus10x17.json,
p2_open_string_extended_typed63_screen.json,
p2_open_string_extended_typed63_report.json,
p2_open_string_extended_baseline.log,
p2_open_string_extended_compiler_metrics63.json}`.

The next five-pair cumulative comparison against the original release matched
all 63 outputs and measured **0.9213×** geometric-mean typed time. Five
predeclared mechanism families now have point estimates at least 5% faster
with paired intervals below parity: scalar/calls 0.9085×, numeric arrays
0.9443×, bitwise/bytes 0.8098×, record/graphs 0.8543×, and
text/collections 0.9394×. The fixed-C2MIR-reference source-relative ratio
is **2.6502×**; it is not a paired cross-language timing result. The
proposal's **0.90×** overall target is still unmet. Artifacts:
`temp/result49_common_overhead/{p2_open_string_extended_vs_original63x5.json,
p2_open_string_extended_vs_original63x5_report.json}`.

P2/P4 now recognizes a declared rank-one nullable-map array whose trusted
element contract failed the former pointer-lane/reified-map conjunction.
Its indexed reads tried an ArrayNum numeric arm and called `fn_index` for the
actual `Array`.
The new arm checks the live Array kind and exact representation certificate,
then reads the Item slot; out-of-bounds, wrong-carrier, and uncertified values
retain the generic path (**S7.1.1v3**, **D3.3.3v3**). An existing pointer-lane
map retains its former certificate-only path. The extended
`trusted_array_dynamic_index_field` fixture covers a map value, a valid null
slot, an out-of-range read, the newly admitted wider record shape, and a
collection-selection key; JIT and
interpreter output agree. In Richards, executed `fn_index` calls fell from
**2.186M to 0.241M** with the same `PASS` result. A 17-pair release run
against the preceding candidate measured Richards at **0.8921×** (16/17
wins), DeltaBlue at **1.0034×**, and Prettier at **0.9962×**, with equal output
on all seven focus rows. The earlier candidate's 63-row three-pair screen
measured **0.9981×** overall with equal outputs; the final Array-kind guard
was added afterward. In the five-row, three-repeat compiler pilot of that
earlier candidate, Richards measured 1.0032× compile time and 0.9943×
generated bytes; DeltaBlue's code bytes were unchanged. The final full
comparison, compiler check, and baseline follow. Artifacts:
`temp/result49_common_overhead/{p4_current_richards_calls.tsv,
p4_nullable_map_final_richards_calls.tsv,p4_nullable_map_final_focus7x17.json,
p4_nullable_map_narrow_typed63_screen.json,
p4_nullable_map_narrow_typed63_report.json,
p4_nullable_map_narrow_compiler_pilot.json}`.

The final full Lambda/Input baseline passed **6,116/6,116**, including
**271** forced-GC and **220** MIR-emission tests. DeltaBlue's ratchet was
reviewed and updated: the module changed **10,119→10,124** finalized MIR
instructions while `constraint_choose_method` shrank **1,696→1,684**.
The 17-pair DeltaBlue control was 1.0034× with equal output, so the extra
five static instructions have no established execution regression. The
GC-effect, root-hazard, and whitespace audits pass. Artifacts:
`temp/result49_common_overhead/p4_nullable_map_final_baseline_green.log`.

The final guarded candidate's independent five-pair comparison against the
original release matched all 63 outputs and measured **0.9244×** overall.
Scalar/calls, bitwise/bytes, record/graphs, and text/collections each met
the 5% point-estimate and below-parity interval criterion; numeric arrays
measured 0.9611× with a wide interval. Against the earlier fixed C2MIR
reference, the source-relative ratio is **2.6893×**. These are new samples,
so the 0.9244× versus the preceding cumulative campaign's 0.9213× should
not be interpreted as a regression from the nullable-map change; its direct
17-pair Richards/control comparison above isolates that change. A fresh
C2MIR reference and final full baseline follow. Artifacts:
`temp/result49_common_overhead/{p4_nullable_map_final_vs_original63x5.json,
p4_nullable_map_final_vs_original63x5_report.json}`.

The refreshed pinned standalone C2MIR driver passed **65/65** ports over five
repeats. Joining its 63 matching typed rows to the final Lambda campaign
gives a **2.6062×** source-relative typed/C2MIR geometric mean. The runs
are independently repeated rather than execution-paired across languages;
they compare each port's own timed region and retain the representation and
semantic differences documented in the proposal. Artifacts:
`temp/result49_common_overhead/{c2mir_final_fresh5.json,
p4_nullable_map_final_vs_original_fresh_c2mir_report.json}`.

The final fresh-process, three-repeat compiler campaign over all 63 rows
used the archived original build with the same additive metrics as the
candidate. Aggregate candidate/original ratios were **1.0116×**
transpile/JIT time, **0.9951×** peak compile RSS, **1.0055×** finalized MIR
instructions, and **1.0015×** exact generated code bytes. Three-way merge
crossed the proposal's per-workload 25% review trigger: **1.3394×** compile
time and **1.2634×** generated bytes (21,136→26,704), while its five-pair
execution time was **0.7623×** original and its executed `pn_push_cow` calls
fell by about 16M. The added guarded string append/equality/COW arms are
therefore an explicit compile-cost tradeoff for a measured runtime gain, not
an aggregate code-size increase. Base64 generated bytes were 1.1117× with
1.1619× compile time, below the per-workload 25% trigger; its execution
time was 0.511–0.52× original across the cumulative campaigns. Raw samples,
exact code lengths, and binary hashes are in
`temp/result49_common_overhead/p4_nullable_map_final_compiler_metrics63.json`
(**D8.2.5v3**).

P4 now guards field reads from rank-one open arrays of admitted records with
the exact runtime record layout. Richards executed 527,000 guards, all fast,
and `fn_member_by_id` calls fell from 855,650 to 328,650. A 31-pair direct
release comparison measured 0.9553× with 27/31 wins and equal output. A
subsequent P4 lane extension reassembles an unaligned packed `int` from byte
loads after the same guard; this handles Richards' `fn_id` at byte offset 35
without asking MIR to select an unaligned word load. Its remaining 328,650
generic field calls fell to zero, and the direct 31-pair comparison measured
0.9634× with 28/31 wins. Inferred layouts, wrong carriers, and out-of-bounds
reads still use the generic arm (**D3.3.3v3**, **S7.1.1v3**). The two MIR
fixtures and opt-profile GTests pin guard attempts, hits, and fallbacks.
Artifacts: `temp/result49_common_overhead/{p4_open_array_field_typed63_screen_report.json,
p4_unaligned_int_baseline_green.log,p4_unaligned_int_vs_original63x5_report.json}`.

P3 now stores a checked cross-unit module-state resolution in the active
context's module hint. Later calls in that context check the logical unit ID
inline and reuse the context-owned slab (**D5.4.3**). Richards' executed
`lambda_module_state_for_unit` calls fell from 657,350 to one, and Hyphen's
from 47,787 to 8,385. Direct 31-pair release comparisons measured Richards
at 0.9890× and Hyphen at 0.9908×, with equal output. The new cold store
increased five MIR budgets by one to four instructions each; those budgets
were reviewed and updated, and the ratchet passed 20/20. The import fixture
and opt GTest pin repeated-call reuse and the required cross-unit fallback.
Artifacts: `temp/result49_common_overhead/{p3_module_hint_compiler_metrics63.json,
p2_ambiguous_ratchet_green.log}`.

P2 shape selection now avoids a module-wide layout guess for an escaped
parameter or an unresolved parameter with several call sources. Those
conditions give no evidence that the module's one observed literal shape is
the parameter's layout; explicit candidate layouts remain guarded as before
(**D2.4.1–D2.4.3**). In Prettier, executed field-guard attempts fell from
2,365,440 to 17,152 with no lost fast hits. DeltaBlue's 80,880 attempts,
68,880 hits, and 12,000 generic arms were unchanged. A direct 31-pair
Prettier comparison measured 0.9892×, 23/31 wins, equal output. The opt
GTest pins the multi-source case. The full baseline passed **6,122/6,122**,
including 272 forced-GC and 221 MIR-emission tests; GC-effect and root-hazard
audits pass. The cumulative five-pair release campaign versus the original
archive matched all 63 outputs and measured **0.9191×** overall (paired
bootstrap interval [0.9046, 0.9642]). Four families had point estimates at
least 5% faster with below-parity intervals; numeric arrays did not. The
fresh C2MIR-reference source-relative ratio was **2.6412×**. This remains
above the 0.90× engineering target. Artifacts:
`temp/result49_common_overhead/{p2_ambiguous_param_baseline_green.log,
p2_ambiguous_final_vs_original63x5.json,
p2_ambiguous_final_vs_original63x5_report.json}`.

A nullable-record string-field extension was tested and removed. Its
null-preserving direct read passed the 6,122-test baseline and forced-GC
fixture (**S7.1.1v3**, **D3.2.6**), but typed Prettier's executed helper and
root counts were identical on both builds, and the all-63 three-pair screen
was 1.0065× with no established gain. The existing dominated non-null string
read remains. Artifacts: `temp/result49_common_overhead/
{p4_nullable_string_baseline_green.log,
p4_nullable_string_typed63_screen_report.json}`.

P3 now preserves tail position through a functional braced block whose
declared result is plain `int` even when its native return transport is an
Item. A recursive backedge reuses the frame only when every non-integer
argument is the same read-only admitted parameter; changing scalar arguments
cross their declared boundary in source order before rebinding. Procedural,
nullable, error-bearing, and changing-owner forms retain the ordinary call
and return path (**D8.2.6**, **D5.3.1**, **S9.1.2**). The MIR fixture pins a
positive allocating recursive body and two negative shapes; the opt-in
profile GTest pins its frame count. The final source passed the full
**6,125/6,125** Lambda/Input baseline. In typed Prettier, executed
`flat_length_at` frame entries fell **4,664,576→1,171,968**, aggregate
frame entries **13,123,081→9,630,473**, root stores
**59,666,116→49,188,292**, and reloads **105,792,519→84,836,871**.
Output remained equal. A direct 31-pair unprofiled release comparison with
the preceding candidate measured Prettier at **0.9599×** (29/31 wins),
three-way merge at **0.9825×** (22/31), and Splay at **0.9950×** (19/31),
with equal output. The latter two ratios are controls rather than proof of
an affected hot path. The full 63-row three-pair screen was **0.9967×**
against the preceding candidate with equal output; it is a breadth screen,
not a confirmation. Artifacts: `temp/result49_common_overhead/
{p3_checked_int_fn_only_baseline.log,
p3_checked_int_fn_only_prettier.tsv,
p3_checked_int_final_focus3x31.json,
p3_checked_int_final_typed63_screen_report.json}`.

A guarded direct ASCII `len(string)` experiment was removed. It retained the
Unicode code-point helper under **S8.3.1v3** and passed the full
**6,128/6,128** baseline, but a direct 17-pair release comparison found
Prettier at only **0.9936×** while text_search was **1.0374×** with zero
candidate wins and three-way merge was **1.0118×**. All outputs matched.
The extra hot-loop flag and branch work are a plausible cause, but those
timings do not isolate the machine-level cost. The ordinary `fn_len_s`
lowering and original MIR expectations were restored. Artifact: `temp/result49_common_overhead/
p3_ascii_len_focus6x17.json`.

P5 recognizes rank-one `int[]` literals whose every member is a finite
decimal integer. After one allocation, it writes each lane directly without
an intervening safepoint; dynamic, nullable, or out-of-band members keep the
checked setter path (**S4.1.2**, **D3.3.3v3**, **D5.3.1**). The MIR fixture
checks both forms, its profile GTest observes three setters only for the
dynamic form, and forced-GC JIT/interpreter output agrees. The zeroed
direct-store checkpoint passed the full **6,128/6,128** baseline. In Splay,
executed `array_int_set` calls fell **3,840,000→0** while allocations stayed
at **384,000**; root stores fell **22.888M→18.664M** and reloads
**50.981M→39.461M**. A direct 31-pair release comparison measured Splay
at **0.9733×** with 27/31 wins and equal output. Richards and Prettier were
near parity, and three-way merge measured 0.9889×. The 63-row three-pair
screen was **0.9963×** overall with equal output. These results establish
a hot-protocol reduction and a modest Splay runtime gain, not the proposal's
broad target. Artifacts: `temp/result49_common_overhead/
{p5_int_literal_splay_before.tsv,p5_int_literal_splay_after.tsv,
p5_int_literal_focus4x31.json,p5_int_literal_typed63_report.json,
p5_int_literal_baseline.log}`.

For the same proven literal shape, P5 now calls the existing uninitialized
numeric-array allocator, then fills every lane before the next safepoint.
The helper may return a zero-length header on data-allocation failure; the
emitted store path checks the data pointer before writing (**D5.3.1**,
**D3.3.3v3**). The complete **6,128/6,128** baseline and GC audits passed.
Against the zeroed direct-store checkpoint, 41 release pairs measured Splay
at **0.9946×** (30/41 wins), with equal output. The 63-row three-pair screen
was **0.9986×** overall. Its apparent Cube3D slowdown was not supported by
executed work—only 32 allocations changed there—and 31 direct pairs measured
Cube3D at 0.9943×; three-way merge was 0.9997×. This is a small additional
gain, not a broad conclusion. Artifacts: `temp/result49_common_overhead/
{p5_int_literal_uninit_baseline.log,p5_int_literal_uninit_splay41.json,
p5_int_literal_uninit_typed63_report.json,
p5_int_literal_uninit_controls31.json}`.

P5 applies the same uninitialized allocator to float literals whose members
are already in double lanes. Every lane is filled before the nullable
fallback or any other collecting call; a null data pointer skips the stores
and retains the allocator's empty-header failure behavior (**D5.3.1**,
**D3.3.3v3**, **S7.1.3v2**). The existing nullable/guarded MIR fixtures were
updated to pin materialization versus scalarized cases, and a profile GTest
pins eight allocations plus one nullable fallback. Forced-GC JIT and
interpreter outputs agree; the complete baseline passed **6,129/6,129**,
including 274 forced-GC tests, 223 MIR checks, and the reviewed two-branch
ratchet increase in `tune30_libm_no_gc`. Cube3D changed **50,048** executed
`array_float_new` calls to the same number of uninitialized allocations,
with helper and root totals otherwise identical. Its direct 41-pair release
comparison was **0.9637×** (34/41 wins), while Splay was 1.0021×. The
63-row three-pair screen was 1.0024× overall, driven by short-row outliers;
31-pair controls put FFT at 1.0000×, knucleotide at 1.0162× (15/31 wins,
no changed allocator calls), and three-way merge at 0.9867×. All outputs
matched. Artifacts: `temp/result49_common_overhead/
{p5_float_uninit_baseline_green.log,p5_float_uninit_focus2x41.json,
p5_float_uninit_typed63_report.json,p5_float_uninit_controls31.json,
p5_float_uninit_cube_after.tsv}`.

The resulting five-pair, all-63-typed-row release comparison against the
archived original matched every output and measured **0.9141×** geometric
mean execution time (paired-bootstrap 95% interval **0.8899–0.9216**,
conditional on the fixed rows; every row is retained in the artifact). All
five frozen non-control mechanism families had point
estimates at least 5% faster with below-parity paired intervals: scalar/calls
0.8977×, numeric arrays 0.9490×, bitwise/bytes 0.8096×, record/graphs
0.8214×, and text/collections 0.9293×. The independently repeated pinned
C2MIR reference gives a **2.5854×** source-relative typed/C2MIR ratio;
it is not a paired cross-language speedup. The proposal's **≤0.90×**
overall target and its unsolved P0–P5 gates remain open. Artifact:
`temp/result49_common_overhead/
p5_float_uninit_vs_original63x5_report.json`.

The fresh-process, three-repeat compiler comparison against the instrumented
original over the same 63 rows measured **1.0037×** transpile/JIT time,
**0.9945×** peak compile RSS, **1.0019×** finalized MIR instructions, and
**0.9982×** exact generated bytes. No aggregate >10% review threshold was
crossed. Three-way merge still exceeds the per-workload 25% review trigger:
**1.3297×** compile time and **1.2650×** generated bytes, versus **0.7540×**
execution time. Its extra guarded append/string arms and previously measured
~16M fewer executed `pn_push_cow` calls remain the explicit runtime-versus-
compile-cost tradeoff; the new float allocator does not run in that workload.
Base64 measured **1.1778×** compile time and **1.1117×** generated bytes,
below the per-workload trigger, versus **0.5175×** execution time. Raw samples
and binary hashes: `temp/result49_common_overhead/
p5_float_uninit_compiler_metrics63.json` (**D8.2.5v3**).

P4 now separates relocating typed-array data from a proven stable logical
length after a collecting call. A read-only parameter or local with an exact
array certificate keeps its length only when the whole body has no rebind,
resize, `var` handoff, capture, or unknown call; the data pointer still
reloads (**D4.4.4v4**, **S7.1.3v2**, **S9.1.2**). Writable array caches keep
the conservative length reload to avoid extra register pressure. Both
post-call and rebind reloads use the existing liveness pruning and the opt-in
executed profile counts only retained reloads (**D5.4.4**). The positive and
two resizing fallbacks are pinned by `stable_array_length_after_gc` MIR and
profile tests; forced-GC JIT/interpreter outputs agree. The final debug
baseline passed **6,132/6,132**, including 274 forced-GC tests and 224 MIR
checks. Audits found 92 verified `NO_GC` imports and no root hazards in
17,306 migrated functions. The all-63 release screen versus the preceding
P5 checkpoint was **1.0010×** with equal outputs, so this is a retained
proof/invalidation improvement without a measured broad runtime gain.
Artifacts: `temp/result49_common_overhead/
{p4_layout_final_baseline.log,p4_layout_final_typed63_screen_report.json,
p4_layout_final_focus8x17.json}`.

A follow-up P0 experiment tried deferring a post-safepoint register reload
when its uniquely owned canonical root slot would survive until the next
collecting call within the same basic block (**D5.3.1**). It reduced
microdiff's profiled root reload count from **21.37M to 9.11M**, with
unchanged root stores and forced-GC output. The experimental code passed
**6,135/6,135** baseline tests. But the eight-workload compiler comparison
found **1.0000× generated-code bytes in every workload**, despite **0.9872×**
finalized MIR instructions, and the all-63 uninstrumented release screen was
**1.0000×** with equal output. Seventeen direct pairs put microdiff at
**0.9990×**. This is evidence that the executed profile counter was counting
pre-backend reloads that did not translate into a material machine-code or
timing cost. The deferral and its fixture were removed; the existing eager
reload remains. Diagnostic artifacts are retained under
`temp/result49_common_overhead/p0_deferred_root_*` to prevent repeating a
counter-only optimization.

P4 now consumes a program-point scalar fact for a local unannotated `var`
written into a trusted record's plain `int` field. The whole-body native-lane
proof declined Richards' `ct` after a nullable indexed assignment even while
its current MIR binding still held the raw int lane. The initial direct-store
trial exposed why the whole-body proof was conservative: on an absent link,
the lane can hold `INT_LANE_NULL`, and writing it directly made JIT and
interpreter disagree. The retained lowering compares that sentinel and
routes it to the existing cold E201 admission/error path before the direct
store. Literal/const fields, dynamic or module bindings, errors, and nullable
values retain the checked setter (**D3.2.1–D3.2.2**, **S7.1.3v2**, **D5.3.1**).
The MIR fixture pins the direct store and nullable fallback; forced-GC JIT
and interpreter outputs agree for both the valid and absent-link cases. The
full baseline passed **6,134/6,134**; GC audits and whitespace checks passed.

Richards' `schedule` fell from four to one checked setter in finalized MIR,
and its executed checked-setter calls fell **1,190,850→328,650**. A direct
31-pair release comparison measured Richards at **0.7460×** with 30/31 wins
and equal output. The all-63 three-pair screen measured **0.9933×** overall,
retaining every row and matching every output. Longer 31-pair controls put
Havlak at 1.0016×, Prettier at 0.9999×, Bounce and Paraffins at 1.0000×,
microdiff at 0.9988×, and Cube3D at 0.9816×; thus the short-screen outliers
are not confirmed regressions. The eight-pilot compiler comparison measured
0.9835× transpile time, 1.0028× peak RSS, and 0.9991× generated bytes;
Richards' generated bytes were 0.9926×, while all seven controls were equal.
These figures establish a large Richards-specific gain, not a broad P4
effect-summary implementation. Artifacts: `temp/result49_common_overhead/
{p4_live_int_guarded_full_baseline.log,
p4_live_int_guarded_controls4x31.json,
p4_live_int_guarded_typed63_screen_report.json,
p4_live_int_guarded_noise4x31.json,
p4_live_int_guarded_compiler_focus8.json}`.

The next P4/P5 diagnostic checked Splay's two rotation helpers against the
existing **D4.4.5** move-out planner. Both `var left = node.left` and `var
right = node.right` are already accepted as move-out borrows. Their finalized
MIR calls `cow_bind_rmw_handle`, which skips a mark only when the complete
runtime spine is unique; it retains `cow_bind_var` on a shared spine. The
rotation's later `map_set_cow` is the write through the borrowed child. Thus
the 387,700 rotation copies in the site census are not explained by a missed
straight-line syntax pattern: the runtime uniqueness check can fail because
an observer still owns a parent or child. Clearing that share bit solely from
the later overwrite would violate **S9.1.2–S9.1.3**. No ownership shortcut was
made; Splay needs a proof spanning the caller's parent slot and the rotation
call before any of these copies can be removed safely.

P2 now specializes a syntactic generic map or array type test to one audited
value-kind query and exact kind comparisons. The generic array predicate
includes ranges, packed arrays, Item arrays, and virtual arrays; the map
predicate includes plain and virtual maps. Named/shaped and first-class type
values retain fn_is. A syntactic simple error type instead compares the
self-tagged Error Item high byte, preserving an actual error value as the
subject (**S11.1.2v2**, **S2.5.1v2**, **S11.4.1v3**,
**D2.4.1–D2.4.3**). The MIR fixture pins all three paths, including a true
error result; JIT/interpreter and forced-GC outputs agree.

In Microdiff's opt-in diagnostic, the map/array slice replaced **207,050**
executed fn_is calls and **207,050** base_type calls with **207,050**
non-collecting item_type_id calls. Root stores fell
**2,815,049→2,607,999** and post-call reloads
**25,373,161→23,597,861**, with zero profile overflow and unchanged
checksum. The direct 31-pair release comparison measured Microdiff at
**0.7796×** (30/31 wins); a four-row 17-pair check put Richards, Prettier,
and SHA1 near parity with equal outputs. The all-63 three-pair screen was
**0.9906×**, every output equal; this is a screen, not the final cumulative
confirmation. The separate error predicate had no residual fn_is/base_type
calls to remove in the current SHA1 release, so no SHA1 speedup is claimed.
Artifacts: temp/result49_common_overhead/
{p2_simple_is_microdiff_prior.tsv,p2_simple_is_microdiff_new.tsv,
p2_simple_is_microdiff31.json,p2_simple_is_focus4x17.json,
p2_simple_is_typed63_screen_report.json}.

A follow-up trial inlined the guarded pointer-header decoder at these two
syntactic type tests. It passed all **6,136** Lambda/input baseline cases,
including the MIR fixture and forced-GC parity. Against the retained
`item_type_id` build, Microdiff was **0.9921×** over 31 pairs; a separate
17-pair four-row comparison put Richards, Microdiff, Prettier, and SHA1 near
parity. The added branches and MIR size have no established broad benefit, so
the trial was reverted. Raw artifacts:
`temp/result49_common_overhead/{p2_simple_is_inline_kind_microdiff31.json,
p2_simple_is_inline_kind_focus4x17.json}`.

The retained type-test build's five-pair all-63 comparison to the archived
original has matching outputs and **0.9137×** geometric-mean execution time.
The fresh source-relative typed/C2MIR comparison is **2.6574×**. Compiler
metrics for eight pilots remain under the aggregate review thresholds; the
three-way-merge row grows **32.71%** in compile time and **26.50%** in code
size, the explicit tradeoff from earlier guarded arms. Artifacts:
`temp/result49_common_overhead/{p2_simple_is_error_vs_original63x5_report.json,
p2_simple_is_error_compiler_focus8.json}`. These figures remain above the
proposal's overall target.

P4 now lets a live nullable local int lane use its existing store-site null
admission before writing a trusted scalar record field. Previously
`direct_base` rejected the value for possible null before the admission could
run, routing every valid iteration through the checked map setter. A direct
nullable indexed expression still uses that setter; a named local retains the
physical int lane and rejects the null sentinel before the raw store
(**S7.1.3v2**, **D3.2.2**, **D5.3.1–D5.3.4**). The MIR fixture pins both
paths, the cold type error, and JIT/interpreter/forced-GC parity; the complete
Lambda/Input baseline passed **6,136/6,136**.

In Richards' `schedule`, the executed `lambda_map_set_checked_inplace` count
fell **328,650→0**, root reloads **4,558,500→2,257,950**, and root stores
**3,901,200→2,586,600**, with equal output. The direct 31-pair release test
measured Richards at **0.8038×** (29/31 wins). An all-63 three-pair screen
against the preceding release measured **0.9954×**, with equal output. Its
apparent Base64 slowdown was noise: the 31-pair rerun measured **1.0011×**.
Compile/RSS/code-size ratios across Richards, DeltaBlue, Prettier, and
three-way merge were near parity. Artifacts:
`temp/result49_common_overhead/{p4_schedule_store_current_calls.tsv,
p4_nullable_local_store_richards_calls.tsv,
p4_nullable_local_store_richards31.json,
p4_nullable_local_store_typed63_screen_report.json,
p4_nullable_local_store_base64_noise31.json,
p4_nullable_local_store_compiler_focus4.json}`.

The new five-pair cumulative release comparison against the archived original
has all **63/63** outputs equal and a **0.9071×** overall ratio. Four frozen
mechanism families meet the ≥5% criterion with paired intervals below parity:
scalar/calls **0.9025×**, bitwise/bytes **0.8173×**, record graphs **0.7712×**,
and text/collections **0.9197×**. Numeric arrays have a **0.9474×** point
estimate but an interval crossing parity. The source-relative typed/C2MIR
geometric mean is **2.5897×**; it is not a paired cross-language ratio.
Artifact: `temp/result49_common_overhead/
p4_nullable_local_store_vs_original63x5_report.json`.

A P1/P3 plain-map `at` trial guarded dynamic receivers and called a new
audited `NO_GC` map-name reader, retaining `fn_at` for other kinds
(**S8.1.1**, **S8.2.2v2**, **D5.3.2**). It passed 6,138 Lambda/Input tests and
selected the reader for all 106,600 Microdiff map-name checks, but executed
`diff_map` root stores and reloads were unchanged. The unprofiled 31-pair
release comparison was **0.9989×** (13/31 wins), with equal output. The extra
kind dispatch and helper were reverted. Diagnostic and raw timing artifacts:
`temp/result49_common_overhead/{p4_plain_map_at_microdiff_calls.tsv,
p4_plain_map_at_microdiff31.json}`.

An audited P1 trial marked `iter_key_at` as non-collecting: it reads a
prebuilt key-list root chunk and returns an existing symbol or inline integer
(**S8.1.1**, **D5.3.2**). In Microdiff's `diff_map`, 106,600 calls kept the
same output while executed root reloads fell **16,465,600→15,612,800**.
Seventeen-pair Microdiff and Richards timings were near parity; Prettier was
**1.0056×** over 31 pairs (9/31 wins), and the all-63 three-pair screen was
**1.0026×**, with a text/collection regression. The metadata and its local
audit exception were reverted: fewer source-level reload events alone did not
pay for this change in release code. Artifacts:
`temp/result49_common_overhead/{p1_iter_key_microdiff_control.tsv,
p1_iter_key_microdiff_candidate.tsv,p1_iter_key_nogc_focus3x17.json,
p1_iter_key_nogc_prettier31.json,
p1_iter_key_nogc_typed63_screen_report.json}`.

A P2 multi-shape member-read trial targeted Prettier's `flat_length`:
eight literal document shapes share a `kind` string slot at offset zero, while
the existing unique-shape guard cannot select any of them. A bounded set of
header-shape checks followed by the packed read was emitted, with the generic
accessor retained on a miss (**D2.4.1–D2.4.3**, **D3.2.4v4**). The 17-pair
release comparison found Prettier at **1.0002×**, Richards at **0.9965×**, and
Microdiff at **0.9852×**, all with matching output. This did not establish a
Prettier benefit; the extra eight comparisons were reverted. Raw artifact:
`temp/result49_common_overhead/p2_equiv_shape_focus17.json`.

## Remaining implementation gates

P0 still needs full executed-counter coverage and a larger paired confirmation;
compile memory and exact code-size collection are present. P1 has a measured
executed-root reduction but still needs broader planned-entry/variant effect
coverage. P2 needs shared program-point range/nullness/layout relations; P3
needs their bounded inline and guarded-region consumers; P4 needs
per-parameter/path effects and precise logical versus relocating invalidation;
P5 needs escape/destination lifetime rules and attributed Splay copy
elimination. The latest campaign meets the three-family, correctness, and
compile-cost reporting criteria, but its **0.9071×** overall ratio remains
above the proposal's **0.90×** target.
