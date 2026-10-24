# Tune34 — Remaining large LambdaJS gaps after Tune33

**Status:** shared compiler/runtime batch implemented; paired release performance
qualification complete. Correctness retains inherited baseline and aggregate
concurrency failures (§5.4). Remaining proposal mechanisms are listed in §5.7.

**Date:** 2026-10-23. **Inspected HEAD:**
`46bdd322c03f85f306c912e3237cc0a0ae914927`.

**Scope:** general compiler/runtime improvements for unchanged untyped Lambda,
with typed Lambda and LambdaJS regression controls. Eight workloads enter this
plan because their recorded U/JS ratio is at least 2.0. Two ratios have material
timed-work mismatches and must be recaptured before claiming a JS advantage.

**Authority:** S4.1.1–S4.1.5, S4.2, S9.1.2–S9.1.3;
D2.4.1–D2.4.3, D3.3.1v2–D3.3.4, D3.4.5–D3.4.6,
D4.3.1–D4.3.3, D4.4.1–D4.4.6, D5.3.1–D5.3.4, D8.3.1v2–D8.3.4v4,
D8.4.1v2. This plan proposes no change to language semantics.

## 1. Evidence and corrections to the comparison

The baseline is the final candidate in
[Tune33's evidence](../../test/benchmark/benchmark_results_tune33_all.json),
generated `2026-10-23T13:09:37.032445`, executable SHA-256
`85686ea034e13da9c18312cb3d0f7a0625da030987669401759f21824683ef7a`.
The replay executable is
`test/benchmark/exe/lambda-tune33-all-c86e403b0f`.
At audit time, the three production source hashes, all eight untyped source
hashes, all eight JS source hashes, and the two benchmark-driver hashes matched
the inspected tree. The later Navier workload migration is recorded in §1.3.
This is an audit of existing release evidence, not a fresh timing experiment.

All times below are workload medians in milliseconds. Counts in later sections
are whole-process executed counts from `diagnostics[].candidate`, including
setup; they do not measure CPU percentages or exclusively the timed region.

| Workload | Untyped | Typed | LambdaJS | Recorded U/JS | Interpretation |
|---|---:|---:|---:|---:|---|
| larceny/puzzle | 13.806 | 14.498 | 2.369 | 5.828 | Same search, different mutation contracts |
| jetstream/splay | 261.382 | 276.653 | 54.798 | 4.770 | **Different timed work**, tree algorithm and payload |
| jetstream/crypto_sha1 | 46.754 | 26.269 | 10.865 | 4.303 | Same expected digest; substantial compiler residue |
| kostya/brainfuck | 157.232 | 106.949 | 43.085 | 3.649 | Same interpreter task; tape, dispatch and output differ |
| kostya/json_gen | 14.177 | 6.886 | 4.362 | 3.250 | Assembly differs; floor/truncation can change content |
| kostya/levenshtein | 5.079 | 2.886 | 1.569 | 3.237 | Same distances; inner-loop spelling and timing differ |
| jetstream/navier_stokes | 76.396 | 68.298 | 28.984 | 2.636 | **15 timed Lambda frames versus one JS frame** |
| r7rs/fft | 0.308 | 0.076 | 0.154 | 2.003 | Different index arithmetic; inadequate zero-input oracle |

### 1.1 Splay's 4.770 ratio is not an equal-work speedup

[Lambda's `main` and `run_splay`](../../test/benchmark/jetstream/splay.ls)
time setup of 8,000 nodes, 50 × 80 modifications, and final node counting.
[The JS source](../../test/benchmark/jetstream/splay.js) calls `SplaySetup()`
at top level. `make_jetstream_ljs_wrapper` in
[run_benchmarks.py](../../test/benchmark/run_benchmarks.py) writes that source
before appending the timer around 50 `SplayRun()` calls. Setup is outside the
JS timer; `SplayTearDown()` verification is not called by that timing trailer.
The retained wrapper exactly matches the recorded SHA-256
`97e6d95220aa8c7822480e81bcdf8dbfa5ee7329dd8d723e94afadb150f5d1e0`.

There are further differences: Lambda rebuilds a recursive value tree; JS
rewires a top-down reference tree. Lambda payload leaves contain the numeric
tag, while JS constructs descriptive strings. The random-key generators also
differ. Counts and allocation costs therefore need phase attribution and a
matched input stream before estimating an attainable compiler speedup.

### 1.2 Navier–Stokes' 2.636 ratio does not establish a JS win

[Lambda](../../test/benchmark/jetstream/navier_stokes.ls) allocates six arrays,
executes 15 frames, and computes the checksum within its timer. The
[JS source](../../test/benchmark/jetstream/navier-stokes.js) initializes its
solver before the appended timer, whose one `runNavierStokes()` call performs
one `solver.update()`. The frame-15 checksum is not reached by that call.
The retained wrapper matches the recorded SHA-256
`c4a7f1a40211632a1f446ad7ee471a9bb85e5f2f91d529eb39d9cd6aa878e159`.

The optional oracle in
[js_benchmark_manifest.py](../../test/benchmark/js_benchmark_manifest.py)
adds 14 **post-timing** frames. It does not align timed work, and it was not
present in this recorded wrapper. Do not multiply the one-frame time by 15:
later frames change state and work. Recapture actual matching workloads.
The previous statement that all eight rows demonstrate JS running at least
twice as fast needs this correction. The recorded medians themselves remain
historical observations.

### 1.3 Navier workload migration — implemented 2026-10-23

At the user's request, all current Navier ports now time **15 simulation
frames** and require **checksum 77**. The JS source's `runIteration()` contains
15 updates, so Node, LambdaJS, QuickJS and the standalone Node runner inherit
the same count. Go and Python end their timers after all 15 frames; Julia,
Java and Erlang run all updates in their measured callable and verify the
completed state without advancing it. Lambda's two ports and standalone C/MIR
already used 15 timed frames and retain their checksum checks.

The optional JS density oracle now inspects frame 15 directly, rejects a
different frame count, and runs no additional simulation frames. Current
wrapper descriptors and native-port count/hash metadata are updated. The
historical JS MVP manifest receives only its Navier entry update, preserving
unrelated frozen source identities. Current 71-row MVP and 63-row Tune14
contracts pass their verifiers.

Correctness checks pass for all 11 registered engine/variant lanes: untyped
and typed Lambda, Node, LambdaJS, QuickJS, Go, Python, Julia, Java, Erlang and
standalone C/MIR. JS and the prepared native ports agree on density digest
`-257786486`. Three focused workload checks cover real timer frame boundaries,
checksum-failure rejection and non-advancing validation; all 11 existing
native-runner checks pass. Captures are under `temp/navier15/`.
These are correctness runs, not replacement benchmark results. Existing
snapshots retain their original timings. Setup, warmup and solver-structure
differences still need accounting in a new performance comparison.

## 2. Why the other gaps and compiler costs remain

### 2.1 Puzzle: snapshot allocation and recursive boundaries

[The Lambda solver](../../test/benchmark/larceny/puzzle.ls) takes three plain
array parameters, writes them, recurses, and clears the placements. The
[JS solver](../../test/benchmark/larceny/puzzle.js) shares the same arrays and
undoes mutations. S9.1.2 requires Lambda's caller snapshots to remain
unobservable to callee writes; S9.1.3 reserves sharing for explicit `var`
parameters. Annotations do not remove this contract, explaining why the typed
port is also slow.

The final profile retains **174,909 numeric-array copies / 15,508,598 bytes**,
106,617 share-mark helper calls, 35,539 boxed `solve` entries, and 1,475,926
root reloads. Copy sites are attributed through the setter, not all to exact
generated instructions. These counts establish allocation/call work, not that
all copies are necessary or that copying accounts for 5.83×.

There are two legitimate compiler opportunities: remove marks/copies where
dead-owner and escape proofs establish that nobody can observe sharing; and
represent small nonescaping scalar snapshots more cheaply while still copying
their values. A whole recursive restore-on-return transformation is **not**
assumed safe: errors, early exits, escaping aliases and recursive liveness all
matter. Changing these benchmark parameters to `var` would measure another
source contract, not a compiler improvement.

### 2.2 Splay: guarded reads still box; mutation and snapshots remain

Tune33 removed 2,622,536 generic member-name helper calls. That did not produce
native scalar/tree lanes throughout the program: the final profile still has
**3,079,202 `map_shape_field_to_item` calls**, 1,945,980 boxed entries,
11,041,986 root reloads, and **951,168 map copies / 62,777,088 bytes**.
`map_shape_field_to_item` remains a descriptor-to-Item projection. Generic
comparisons, field setters and recursive calls still consume those Items.

The ownership-site breakdown attributes 511,476 copies to `splay_node`,
183,209 to `rotate_right`, and 204,491 to `rotate_left`. Local owners and
store-back ordering explain why simply eliminating name lookup did not remove
the expensive update path. The next mechanisms are carried place/layout facts,
native field consumers, descriptor-based writes, and proven owner transfers
under D4.4.4v4–D4.4.6. The amount removable remains to be demonstrated on
aligned timing. Splay's 4.77× ratio is not a valid target for this work.

### 2.3 SHA-1: array absence and machine-word result facts break the chain

[The Lambda port](../../test/benchmark/jetstream/crypto_sha1.ls) already spells
wrapping arithmetic with `u32`. Generic `int` must retain saturation/poison
(S4.1.1–S4.1.2), while machine integers wrap (S4.1.5). JS bitwise operations
in [its port](../../test/benchmark/jetstream/crypto-sha1.js) operate on machine
words. Matching the final digest does not license changing Lambda's numeric
semantics on other inputs.

The candidate retains **1,328,200 `fn_index`**, 758,400 `fn_bxor_item`,
252,600 `fn_band_item`, 251,675 `fn_shl_item`, 251,600 `fn_bor_item`,
316,000 boxed `sha1_ft` and 252,800 boxed `rol` entries, plus 21,705,450
root reloads. There are no COW copies in this capture.

The compiler already has native/u32 bitwise emitters. In
`mir_bitwise_kind_for_node`, fallible shifts intentionally do not advertise a
raw-int carrier to an enclosing bitwise call; `mir_bitwise_u32_path` rejects
possibly-null/error operands. This is a correctness boundary, not a missing
XOR instruction. Prove indexed reads present, carry exact numeric domains and
shift ranges, and retain error exits through nested operations and calls.

### 2.4 Brainfuck: mostly native execution, with representation differences

[Lambda](../../test/benchmark/kostya/brainfuck.ls) uses `fill(30000, 0)`, `% 256`,
seven independent opcode conditions, and `output ++ chr(...)`.
[JS](../../test/benchmark/kostya/brainfuck.js) uses `Uint8Array`, a mask,
`else if` dispatch, and output fragments joined at the end. Lambda allocates
the tape 10,000 times. Its physical tape width and zero-initialization cost
need explicit allocation-byte attribution before promising a storage change.

The old 63-million-truthiness-call diagnosis is already addressed: the final
profile has no `is_truthy` traffic. It does retain 130,000 `fn_chr`, 130,000
`fn_strcat`, 140,000 `it2s`, and 1,480,006 root reloads. The output builder
already appends in place 110,000 times; claiming quadratic concatenation is
incorrect. Most opcode execution has no generic-helper count, so final native
assembly and CPU samples must determine the remaining dispatch, remainder,
bounds and tape-initialization shares.

General opportunities are finite-range propagation through numeric stores,
safe power-of-two remainder lowering, mutually exclusive predicate lowering,
and appending a validated character directly to an exclusive builder. A byte
tape source rewrite is a port experiment, not the primary optimization.

### 2.5 JSON: result/consumer facts stop before the complete concat tree

[Lambda's](../../test/benchmark/kostya/json_gen.ls) `json_str` forwards through
`json_escape`, and object helpers combine multiple call results. The current
string inference deliberately requires a useful native consumer, avoiding the
Prettier regression documented in Tune33. Some boundaries therefore remain
boxed; the existing concat collector cannot flatten the entire path.

The candidate has **140,004 `fn_join`**, 230,000 `it2s`, 80,000
`fn_strcat_many`, 60,012 boxed entries, 220,124 growth/copy events and
6,615,320 copied bytes. Both control and candidate already have 19,880
in-place outer-builder appends. Tune33's generic-join reduction was 61.1%,
below its proposed 90% target; increasing an inline budget alone is not a
complete remedy.

[JS](../../test/benchmark/kostya/json_gen.js) exposes primitive concat chains
and uses fragments/join for the outer result. Its `Math.trunc` also differs
from Lambda's `floor` for negative coordinates. A length-only oracle cannot
prove content equivalence. Preserve each source's current output during engine
A/B; use matching rounding and complete JSON content for a cross-language
comparison. Carry string success facts through bounded call summaries, and
compose nonescaping helper results into the existing builder infrastructure.

### 2.6 Levenshtein: nullable reads still force a boxed selection function

Tune33 removed almost all generic indexing and arithmetic. The remaining
hot boundary is **410,090 boxed `min3` calls**, with 2,054,608 root reloads.
There are also **910 array copies / 3,346,216 bytes**, associated with the
row-swapping and mutation pattern. Four generic index calls remain; removing
those four is not the objective.

In [Lambda](../../test/benchmark/kostya/levenshtein.ls), `prev` and `curr`
originate from `fill(m + 1, 0)` and exchange roles through `tmp`. Inside
`1 <= j <= m`, reads `prev[j]`, `curr[j-1]`, and `prev[j-1]` should have
provable presence if equal lengths and owner identity survive the loop join.
The current direct counter/length and fixed-array proofs do not establish
that complete relation for native `min3` admission. All arguments must be
admitted together (D8.3.3).

[JS](../../test/benchmark/kostya/levenshtein.js) additionally uses Int32Array,
an inline minimum expression, and hoists `s1.charCodeAt(i-1)`. Lambda prints
the result lines inside its timer; JS prints afterwards. The plan targets
relational index facts, safe owner permutation, and invariant string reads.
It must not replace the Lambda minimum function by a benchmark-specific rule.

### 2.7 Navier–Stokes: conversion helpers remain in otherwise native loops

Regardless of the invalid cross-language timing ratio, the Lambda profile
establishes **1,474,560 `floor` calls and 1,474,660
`lambda_double_to_int_lane_c` calls**. There are no COW copies, only 75 unique
mutation preparations, and 3,613 root reloads. Another generic-array or root
elimination pass is unlikely to explain much here.

`advect` clamps coordinates, floors them, and converts to integer indices.
`emit_double_to_int_lane` currently calls a helper unconditionally. A
finite-range proof can use native conversion and, for nonnegative finite
values, combine floor and truncating conversion. **Clamp comparisons alone
do not prove finiteness:** NaN can pass both comparisons. Derive finite inputs
or retain a guard and the existing poison/error path (S4.2, D2.4).

The JS port also uses reference swaps and `lin_solve2`; Lambda copies array
contents and calls separate solvers. Removing these passes requires whole-region
alias, exit and floating evaluation-order proofs. Treat that as a later loop
optimization, only after aligned measurements establish its value.

### 2.8 FFT: the expensive residue is concentrated in bit reversal

[Untyped Lambda](../../test/benchmark/r7rs/fft.ls) uses `n / 2` and `m / 2`,
which introduce floating values into the `j` index recurrence.
[JS](../../test/benchmark/r7rs/fft2.js) uses shifts, while
[typed Lambda](../../test/benchmark/r7rs/fft2.ls) explicitly uses
`int(... div 2)`. Thus typed Lambda's 0.076 ms is evidence that a fast
implementation exists, not an isolated annotation experiment.

The profile retains 4,094 generic `ge`, 4,032 `add`, 2,048 `lt`, 2,047 `sub`,
1,984 `fn_index`, 1,984 `index_assign_cow`, and 48,443 root reloads. The
helper pattern matches the bit-reversal stage's mixed numeric index chain;
it does not establish a slow butterfly arithmetic kernel. There is one
32,840-byte array copy, not a repeated-copy bottleneck.

For the actual call, halving positive power-of-two lengths produces exact
integral values. A bounded congruence/range analysis can retain an efficient
integer carrier without changing the semantic float type or reflection.
General odd lengths must preserve `/` semantics. `four1` takes a plain
parameter and returns no transformed array: the caller's zero-input check
cannot establish caller write-back or a correct transform. Add nonzero
transform checks with an explicit result for qualification; do not silently
change the historical source to an inout call.

## 3. Shared implementation work

### Phase 0 — Establish equal work and meaningful outputs

Use the existing runners and provenance helpers, with explicit workload
metadata: setup, iteration/frame count, timed validation, input seed, and
output oracle. Preserve the old rows as historical evidence; give aligned
workloads distinct identities. Correcting a timer earns no compiler speedup.

- Splay: capture setup, modifications and validation separately; report an
  end-to-end sum as well as modification-only time. Use the same key stream,
  payload and operation count for any paired language diagnostic. Verify
  sorted unique keys and payloads, not only node count.
- Navier: the 15-frame/checksum migration is complete (§1.3). For performance
  qualification, align setup boundaries and check full density output plus
  velocity state where needed. Historical one-frame results remain historical.
- JSON: check complete content with the same rounding rule for the aligned
  comparison. Keep the unchanged-source engine A/B independently qualified.
- FFT: add nonzero input and a returned-transform oracle; verify the plain
  parameter leaves its caller unchanged. Repeat the same workload enough
  times to resolve sub-millisecond differences, recording the repeat count.
- Levenshtein: place output outside both aligned timers and check all four
  distances, including the large inputs.

**Gate:** eight audit records with source/wrapper hashes, iterations and
oracles; no unqualified Splay/Navier JS-speedup claim. Preserve separate
source-equivalence and engine-A/B results. Existing per-engine stdout equality
is not cross-language semantic validation.

### Phase 1 — Shared relational facts and complete call admission

Extend the existing `MirFlowScope`, `MirFlowIndexBound`, `MirFiniteIntFact`,
`mir_index_proof`, `mir_index_length_relation_in_bounds`, call/result summaries,
and native-entry selection in
[transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp).
Keep one fact producer shared by representation probing and lowering; avoid
another family of independent AST heuristics.

1. Track bounded affine index/length relations: `length = m + 1`, `1 <= j <= m`,
   offsets such as `j-1`, and equal-length owner permutations at loop joins.
   Retain interval/congruence facts for halving and bitwise index construction.
2. Record semantic domain, physical carrier, null/error possibilities and
   proof dependencies separately. An integer carrier for an integral float
   must still reconstruct a float at observable boundaries (D2.4).
3. Propagate finite scalar and array-result facts through bounded call-graph
   summaries; use a terminating fixed point with conservative widening for
   recursion. Carry only facts justified for the exact call shape.
4. Invalidate on rebinding, relevant writes, widening, escape or unknown calls;
   intersect facts at joins. An element lane remains binding-local and does
   not become a permanent declared certificate (D3.3.3v3).
5. Feed the complete admitted tuple to existing raw entries or bounded
   inlining. Retain boxed fallback and D8.3.3's two proof-producing paths;
   do not introduce an unguarded third entry or speculative public contract.

**Pilots:** Levenshtein, SHA-1, FFT; secondary coverage from text search and
numeric arrays. **Mechanism gate:** eliminate hot boxed `min3` entries;
reduce SHA-1 generic indexed reads by at least 90% where bounds are proved;
eliminate FFT's generic bit-reversal arithmetic on the admitted power-of-two
case. These are proposed targets, not measured predictions.

### Phase 2 — Use proven numeric facts in native operations and control flow

Build on Phase 1 and the existing `mir_bitwise_binary_decision`,
`mir_emit_u32_bitwise`, `emit_int_lane_divmod`, `emit_double_to_int_lane`,
`mir_inline_callee_ok`, and `mir_inline_expr_ok`. Consult shared physical
lowering in [mir_emitter_shared.hpp](../../lambda/runtime/mir_emitter_shared.hpp).
The JS examples are `int32`, `typed_array_lanes`, and `string_concat` in
[mvp_lmd_mir.cpp](../../lambda/js/mvp-lmd/mvp_lmd_mir.cpp); reuse physical
operations, while retaining Lambda's own admission and numeric semantics.

- Carry raw machine-word success values through nested bitwise operations,
  with existing error lanes at unsupported counts or operand shapes. Consume
  proven shift ranges and extend bounded helper admission using those facts.
- Lower `% 2^k` to a mask only for proven finite nonnegative integers. Keep
  signed, poison and zero-divisor behavior on the normal path. Inspect final
  native code first: MIR may already strength-reduce a particular operation.
- Emit guarded native float-to-int conversion; combine `int(floor(x))` only
  when finite nonnegative range and conversion semantics are established.
- Combine independent equality branches into mutually exclusive dispatch
  only if the tested value is stable, comparisons are total and no intervening
  effect changes their meaning. Benchmark names and opcode constants must
  not appear in the optimizer.
- Hoist invariant immutable string reads where bounds, indexing semantics
  and evaluation effects permit it. Preserve Unicode and absence behavior.

**Pilots:** SHA-1, Brainfuck, Navier, FFT. **Mechanism gate:** at least 90%
fewer hot boxed SHA bitwise helpers; native admitted Navier conversion paths;
assembly evidence of reduced Brainfuck dispatch/division cost. Do not claim
success from another reduction in already-zero truthiness calls.

### Phase 3 — Compose string producers into existing builders

Extend `mir_callsite_result_scalar_type`, `mir_string_concat_leaf`,
`mir_collect_string_concat_leaves`, and `mir_emit_string_concat_tree`; reuse
`string_buffer_join`, `fn_strcat_many` and fixed-arity entries in
[lambda-eval.cpp](../../lambda/runtime/lambda-eval.cpp).

Use bounded effect/result summaries for forwarding string helpers. Preserve
the Tune33 consumer-profitability rule: a boxed record constructor should not
gain unbox/rebox work just because its input happens to be a string. Where a
helper's only result is a nonescaping concat tree, compose its pieces into the
caller builder under a bounded expansion budget. Preserve leaf evaluation
order, conversion order, errors and exactly-once evaluation on fallback.

Add a shared validated-codepoint append path if CPU/allocations attribute
material work to `chr` temporaries. It must share the normal Unicode encoder
and builder-growth/rooting code, including invalid-input behavior; a byte-only
benchmark shortcut is insufficient.

**Pilots:** JSON and Brainfuck, with Base64 and Prettier controls.
**Mechanism gate:** JSON generic joins fall at least 90% from Tune34's
140,004 baseline and copied bytes fall materially; no new hot `it2s` traffic
in Prettier. For Brainfuck, valid character appends avoid intermediate String
allocation while producing identical output. A global inlining-limit increase
is not an implementation of this phase.

### Phase 4 — Ownership lifetime, place facts and recursive record operations

Extend the existing place-handle and share-range analysis in
[build_ast.cpp](../../lambda/runtime/build_ast.cpp), its metadata, MIR COW
joins, `mir_emit_cow_path_set`, guarded field access and descriptor projection.
Reuse `cow_prepare_write`, `array_num_set_cow_idx`, `map_set_cow`,
`map_shape_field_to_item` and `LaneStorageDesc` in the runtime/core.

1. Detect ownership permutations of local arrays, where every temporary's
   last use is accounted for and no snapshot escapes. Levenshtein's
   `tmp/prev/curr` cycle is a pilot. Omit unnecessary share marks at their
   origin; do not clear the monotonic shared bit opportunistically (D4.4.1).
2. Carry place identity, layout and uniqueness through valid handle regions,
   invalidating them at D4.4.4v4 events. Use the existing runtime spine test
   and complete COW fallback; reload data-zone pointers after allocating calls.
3. Let exact descriptor proofs reach native field consumers and stores,
   avoiding Item projection/re-admission where the full contract allows it.
   Recursive optional children still need presence/layout checks. Use bounded
   immutable variants, not inline caches (D8.4.1v2).
4. Extend dead-owner/escape analysis to plain parameters and small scalar
   aggregates. For nonescaping snapshots with bounded size, investigate
   activation-local scalar storage, retaining logical copies and the ordinary
   materialization path on escape. Never use native-stack conservative GC
   scanning; rootable payloads require precise homes (D5.3).

**Pilots:** Levenshtein, Puzzle and aligned Splay; controls include retained
snapshots, nested handles and recursive trees. **Mechanism gate:** remove
Levenshtein's row-swap copies when proven redundant; demonstrate reduced
Splay projections/setter costs and separately count safe copy eliminations.
For Puzzle, attribute required versus avoidable snapshots and validate a
general storage/ownership mechanism before setting a copy-reduction target.
The 174,909 copies must not become an instruction to delete all copying.

**Stop condition:** if a copy remains observable or a representation cannot
preserve the source contract, retain it and record the residual cost. The plan
does not depend on a language-semantics change or a source rewrite to claim
completion. High-risk restore-on-return and whole-solver buffer-swap rewrites
require separate proof and are outside the initial implementation tranche.

## 4. Sequence, qualification and completion

| Order | Deliverable | Main workloads | Completion evidence |
|---|---|---|---|
| 0 | Matched work, output audit and CPU attribution | All eight | Hashes, phase counts, outputs, CPU/native-code attribution |
| 1 | Relational facts and complete call admission | Levenshtein, SHA, FFT | Hot-call/load reductions and negative-path correctness |
| 2 | Native numeric/control-flow consumers | SHA, Brainfuck, Navier, FFT | Final assembly, preserved numeric edges, paired timing |
| 3 | String producer composition | JSON, Brainfuck | Join/copy/allocation counters and paired timing |
| 4 | Ownership and record propagation | Levenshtein, Puzzle, Splay | Snapshot correctness, copy attribution, aligned phase timing |
| 5 | Full-suite qualification and residual audit | 71 untyped + 71 typed + JS peers | Per-row results, uncertainty and remaining causes |

Implement and qualify mechanisms in these batches. Do not repeatedly tune a
single benchmark expression or broaden inlining globally until a score moves.
A phase is complete only when its stated mechanism and correctness gates are
met; a green unrelated suite or overall geometric mean is insufficient.

### Correctness and semantic gates

Use existing MIR emission/ratchet, tier-parity and forced-GC harnesses. Add
behavior-focused cases at the new proof boundaries, with `.txt` goldens for
each new `.ls`: alias/rebinding at joins, odd FFT lengths, nonzero transforms,
negative/out-of-range indices, null/error elements, integer poison/overflow,
sized wrap and invalid shifts, NaN/infinite conversions, Unicode/invalid
codepoints, retained snapshots, escaping temporaries and recursive errors.
Check both admitted and fallback paths; probe/lowering must agree.

At implementation milestones run `make test-lambda-baseline` and
`make test262-baseline`, plus the relevant MIR and forced-GC gates. Tune33
recorded 8,095/8,097 Lambda cases with two control-reproduced failures and
40,261/40,261 Test262 cases. Re-establish their status on the new control;
do not silently treat inherited failures as a green aggregate gate.
Run the 142 Lambda benchmark output checks before accepting performance.

### Performance gates

- Read Developer_Guide §7 before building. Use release builds and archived
  private control/candidate executables; `make release` only in the main
  checkout. Record binary, source, wrapper, build and native-module hashes.
- First use the **unchanged** eight sources for causal engine A/B. Report
  aligned cross-language variants separately. Start with CPU samples and
  phase-specific counters; instrumented timings are never acceptance samples.
- Use serialized alternating release pairs, a control/control check and host
  observations. Confirm pilot improvements with at least 21 pairs and
  one-sided 95% bounds. Do not retry failures into acceptance.
- Proposed execution goal: at least **20% geometric-mean reduction over the
  eight unchanged untyped rows**, with a supported gain in each mechanism's
  intended pilots. This is a target, not a prediction. Report every row and
  absolute milliseconds; a large single-row win cannot hide unchanged phases.
- Typed and JS aggregate candidate/control ratios should be at most 1.01;
  investigate every screened slowdown and accept no confirmed regression
  above 3%. Limited samples are inconclusive, not evidence of no regression.
- Record compilation time, generated native bytes and process wall time.
  Review increases above 10% per pilot and enforce bounded variant/inlining
  growth. Workload speedup alone does not establish better startup or tiering.
- Run the full 71-workload untyped/typed/JS screen at closeout. Use existing
  long-peer census limits explicitly; do not imply uniform confidence. Node
  can remain an optional external reference, not a default runtime gate.

Write new evidence to `test/benchmark/benchmark_results_tune34_all.json` and
append implementation progress to this document. Preserve Result53 and Tune33
as historical snapshots. The final report must identify remaining costs as
measured compiler residue, necessary source semantics, port differences or
unresolved native-code costs, and must replace the unsupported Splay/Navier
speedup claims with actual matched-work measurements.

## 5. Implementation record — 2026-10-23

### 5.1 Shared compiler mechanisms

Compiler changes are in `lambda/runtime/transpile-mir.cpp`; the shared
container allocator change is in `lambda/runtime/lambda-mem.cpp`, with precise
slot recycling in `lambda/runtime/gc/gc_heap.{c,h}` and `gc_object_zone.{c,h}`.
Canonical benchmark sources are unchanged by Tune34. The earlier 15-frame
Navier migration remains a shared input to both binaries (§1.3).

| Mechanism | Proof and fallback | Intended pilots |
|---|---|---|
| Native float-to-int conversion and `int(floor(x))` composition | Ordered finite int53 bounds precede D2I; nonnegative truncation implements floor. Negative, nonfinite and out-of-band values retain floor and the canonical converter (S4.2, D2.4.1–D2.4.3). | Navier, numeric conversion consumers |
| Complete local owner permutations | A bounded cycle of up to eight local ArrayNum owners accounts for every scratch use. Owner metadata and precise roots move with the values; observed/captured scratch values retain ordinary binding/COW behavior. No shared bit is cleared (S9.1.2, D4.4.1, D5.3.1). | Levenshtein |
| Dense inferred-array read/write tuples | Complete index and producer proofs establish lane-preserving writes under live length/layout/uniqueness guards. A new write-loop guard must admit a previously boxed scalar call; raw swap loops retain their existing lowering. Guard misses retain the boxed loop. Read-only loops reuse their existing proof; inferred lanes acquire no permanent T[] certificate (D3.3.3v3, D8.3.3). | Levenshtein |
| Bounded concat producer facts | Flattening a concat's bounded leaf set does not consume the alias/call recursion budget once per left-associated join. Existing consumer profitability and existing concat emitters remain authoritative (D8.3.3, D8.4.1v2). | JSON, Prettier control |
| Complete dynamic compact-int bitwise admission | Both original Items are admitted before decoding. Only finite int53 results reach canonical lane boxing; wider raw words retain runtime saturation, avoiding collisions with lane sentinels. Sized domains, poison, unsupported counts, null and errors retain the original helpers (S4.1, D2.4). | SHA-1 |
| Erased numeric-array read admission | Existing physical element/layout/bounds guards can return a boxed int Item from an inferred ArrayNum whose element summary was erased. The runtime miss retains `fn_index`; no public array contract is asserted (D3.3.3v3). | SHA-1 |
| Mixed numeric local recurrences | A terminating proof with eight active bindings and a 96-node budget checks initializers and all writes, rejecting borrow/capture/unknown-domain dependencies. Int/float member tags remain observable; proven float-domain arithmetic and numeric ordering decode their operands directly (D2.4.1–D2.4.3). | FFT |
| Exact numeric-position reads | Stable local numeric-array roots and pure numeric keys retain their semantic types. A finite conversion plus exact round trip admits a machine position; fractions, NaN, infinities and invalid positions read absent. Effectful receivers/keys and foreign carriers keep the original ordered read (S8.2.4v3, S7.1.1v3, D8.3.3). | FFT |
| Exclusive equality dispatch | Bounded runs of distinct finite integer equalities use an unchanged local. Rebinding, capture, borrow and unsupported producer shapes retain the independent branches. Existing optional-int producer facts admit boxed ordinal keys without changing their null/error behavior (D2.2.2, S5.1, S9.1.3). | Brainfuck |
| Shared small-container allocation | Runtime `heap_calloc` routes small containers through the existing zeroed `heap_calloc_class` path used by MIR maps and LambdaJS. Large objects and non-container layouts retain their ordinary allocator. Precise roots, stable object addresses, COW state and payload copying remain authoritative (D4.1.1v2, D5.3, S9.1.2). | Puzzle, Splay, Havlak, merge and constructor-heavy peers |
| Precise bump-slot recycling | Swept bump slots return to shared size-class lists. Every allocation path preserves the storage-owner flag and zeroes reused payloads; requested-byte accounting matches sweep. Exact slot recognition and root lifetimes are unchanged (D4.1.1v2, D5.3). Repeated collection bursts retain the initial block/cursor. | All constructor-heavy workloads |
| Live-only data compaction after sweep | DFS reachability and ephemeron/weak processing finish first. Allocation-free finalizers run while source zones are intact; dead headers leave the list before survivor buffer fixups, and marks clear afterward. Failed mark-stack growth abandons collection before heap mutation (D4.3.1, D4.3.3, D5.3). | Constructor-heavy peers |
| Inlined shared slot construction | Generic and known-class allocation entries use one source helper with inline construction. Zeroing, header ownership and large-object fallbacks remain; generic allocations avoid a delegating class-allocator call (D4.3.2v2). | Small-string and constructor-heavy workloads |

The MIR fixtures `tune34_*` cover numeric conversion edges, bitwise domains,
mixed numeric reflection and fractional halving, exact/fractional indices,
null widening, receiver/key evaluation order, owner cycles and retained snapshots, observed temporaries,
mixed concat inputs, live record slots, and dispatch rebinding/duplicates.
Every fixture has its behavioral golden and emission sidecar. The earlier
`tune21_var_widen_at_declaration` fixture still requires the original member
tags and skipped-assignment behavior; its emission expectation now permits
decoding the Item for a proven float consumer.

Fallback review found two correctness boundaries before acceptance. Numeric
position admission initially reordered an effectful receiver and key; it now
requires a stable local receiver and a pure numeric key. A raw `1 << 63`
initially collided with `INT_LANE_NAN`; the result-range guard now sends every
out-of-band machine word through the original finite-word saturation path.
Both cases have behavioral regression coverage. Captures from before these
fixes are rejected intermediate evidence, not the final measurements.

### 5.2 Rejected record-projection experiment

A live-descriptor MIR decoder reduced Splay's whole-process
`map_shape_field_to_item` count from **3,079,202 to 798,298**, while retaining
the shared decoder for misses. The five-pair release pilot showed no supported
runtime gain (untyped ratio 1.0056; typed 1.0067). Generated native code grew
**54,736 → 81,584 bytes (+49.1%)**, with the same 39 generated functions;
generator MIR volume grew 11,424 → 16,866 instructions. This experiment is
**removed from the accepted candidate**.

The shared decoder is already registered NO_GC/NO_REENTRY. Eliminating its
call does not eliminate the root traffic attributable to recursive mutation
and COW boundaries. A smaller descriptor-call count alone did not satisfy
the runtime/code-growth review. Future native field consumers need complete
layout/domain admission and useful consumers; this attempt establishes no
safe deletion of Splay's snapshot copies (D3.4.6, D4.4.4v4, D8.4.1v2).

### 5.3 Workload and output audit

The reproducible audit is
[`audit_tune34_workloads.py`](../../test/benchmark/audit_tune34_workloads.py):

```sh
python3 test/benchmark/audit_tune34_workloads.py \
  --control temp/tune34/control --candidate temp/tune34/candidate
```

It creates separately identified diagnostics under `temp/tune34/workload_audit/`:

- Full 1,000-object JSON content, on unchanged Lambda sources for engine A/B.
  The original Node truncation differs in **750 objects**. A separately named
  floor-aligned JS diagnostic matches the complete Lambda content. A length
  match is not used as that oracle (S4.2).
- Returned nonzero FFTs with 4, 8 and 16 complex samples, on both Lambda
  variants and both binaries. An independent DFT supplies the expected values
  at absolute tolerance 1e-9. Each check also requires the plain-parameter
  caller's complete original array to remain unchanged (S9.1.2).
- Splay with 8,000 nodes, 4,000 modifications, seed 49,734,321, identical LCG
  keys and numeric depth-five payloads. It compares all 8,000 sorted keys and
  every payload leaf, and records setup/modification/validation phases. JS
  definitions are replaced in the generated diagnostic without reassigning
  function bindings. The recursive value-tree and top-down reference-tree
  algorithms remain different. Single diagnostic timings are not acceptance
  samples or replacement canonical speedups.
- Levenshtein with printing after both aligned timers and all four distances
  required to equal **3, 3, 500, 2**. Its generated identity remains separate
  from the unchanged-source causal measurements.

The audit records all eight canonical source triples and their hashes. JS
diagnostics explicitly select the MIR backend. Historical Result53 and Tune33
timings retain their original workload identities.

### 5.4 Frozen candidate and correctness

The accepted batch changes `lambda/runtime/transpile-mir.cpp` and
`lambda/runtime/lambda-mem.cpp`, plus the shared GC heap/object-zone modules.
The archived release is
`test/benchmark/exe/lambda-tune34-all-46bdd322c0`, SHA-256
`c8a49e249aeb1c7972fb72eb67063a6359dec5a152de0a5862e7edcf92e09c01`.
Its source patch and build manifest accompany it. The control is the frozen
Tune33 release, SHA-256 `85686ea034e13da9c18312cb3d0f7a0625da030987669401759f21824683ef7a`.

Final-source checks:

| Gate | Result |
|---|---|
| Combined Lambda/input baseline | 8,119/8,121 |
| GC unit suite | 90/90, including recycling, mark-failure and finalizer cases |
| MIR emission | 265/265 |
| Size ratchet | 20/20 |
| Full forced-GC sweep | 336/336 |
| Test262 baseline | 40,261/40,261; zero retries |
| New MIR fixtures across interpreter/auto/JIT | 27/27 |
| Focused allocation/concurrency/activation cases | 30/30, including two new allocator cases |

The baseline retains the control-reproduced `crypto_key` and phase-six PDF
failures (object 9, byte 760; both interpreter and JIT). The two new C++
allocator tests are outside the default baseline. They verify exact bump homes,
live precise roots and zeroed large-object fallback (D4.1.1v2, D5.3).

The additional aggregate concurrency executable is **red**: the existing
Jube ordinal-parity case fails, followed by a context/cache crash. An isolated
unit control compiled from the verified frozen allocator source reproduces
both failures. Its old allocation route is independently verified by the two
bump-home assertions failing as expected. Production files/archives were not
changed for that control build. The aggregate failure is retained separately
from the 30 passing focused cases; no aggregate concurrency pass is claimed.

The size review removed redundant read-only-loop guards and unprofitable
guards on raw swap loops. Original typed-array budgets remain. Cube3D adds
three reviewed module instructions, **15,096 → 15,099**. The release-only
`cow_nested_store` 166→169 profile drift also appears on the control; its
existing debug-profile budget already covers 169. No unrelated budget is raised.

### 5.5 Paired release results

Results are recorded in
[`benchmark_results_tune34_all.json`](../../test/benchmark/benchmark_results_tune34_all.json).
The eight targets and selected controls use **21 alternating pairs plus one
warmup per side**; the full Lambda screen uses three pairs per row.

| Target | Untyped control ms | Untyped candidate ms | U ratio | Typed ratio |
|---|---:|---:|---:|---:|
| r7rs/fft | 0.304 | 0.200 | 0.6579 | 0.9868 |
| kostya/brainfuck | 160.791 | 93.238 | 0.5799 | 0.8781 |
| kostya/levenshtein | 5.027 | 2.404 | 0.4782 | 0.8484 |
| kostya/json_gen | 14.299 | 7.264 | 0.5080 | 0.9964 |
| larceny/puzzle | 13.933 | 12.648 | 0.9078 | 0.9095 |
| jetstream/navier_stokes | 76.007 | 74.955 | 0.9862 | 0.9874 |
| jetstream/splay | 261.991 | 221.083 | 0.8439 | 0.8851 |
| jetstream/crypto_sha1 | 46.717 | 27.114 | 0.5804 | 0.9923 |

The eight untyped ratios have geometric mean **0.6701**, or
**33.0% less execution time**. This exceeds the proposed 20% target.

| Population | Rows | Geometric candidate/control ratio |
|---|---:|---:|
| untyped | 71 | 0.9329 |
| typed | 71 | 0.9674 |
| js | 71 | 0.9786 |

These population means combine different sample counts and are descriptive.
Screened Lambda and short-JS slowdowns receive separate 21-pair confirmation.
The four long JQ JS peers have a **one-pair, 600-second-per-process census**;
they do not exclude a small regression or support uniform confidence.
Every accepted sample requires successful exit, equal normalized output and
the available output oracle; inputs and binaries are frozen. Process wall
ratios and per-row uncertainty are retained alongside workload timings.


No row establishes a regression above 3% using the two-sided 95%
lower confidence bound. Three 21-pair results remain inconclusive against
that threshold; this does not establish that their slowdown is below 3%.

| Workload | Variant | Candidate/control | Two-sided 95% interval |
|---|---|---:|---:|
| awfy/havlak | typed | 1.0317 | 1.0042–1.0466 |
| awfy/sieve | typed | 1.0400 | 1.0000–1.0400 |
| r7rs/ack | js | 1.0554 | 0.9976–1.1127 |

Executed profiles are separate from accepted timing. They cover the whole
process, including setup. Helper/boxed-entry and COW instrumentation does
not form an exhaustive C++ call graph or assign source-expression CPU shares.
The eight untyped targets also have five alternating compiler-pipeline
captures and native-code snapshots:

| Target | Native bytes control → candidate | Native ratio | Compiler pipeline ms control → candidate | Compile ratio |
|---|---:|---:|---:|---:|
| brainfuck | 8,608 → 7,600 | 0.8829 | 8.653 → 7.314 | 0.8453 |
| crypto_sha1 | 25,984 → 29,712 | 1.1435 | 21.303 → 24.995 | 1.1733 |
| fft | 17,456 → 18,416 | 1.0550 | 18.253 → 19.557 | 1.0714 |
| json_gen | 12,624 → 12,912 | 1.0228 | 10.518 → 10.705 | 1.0178 |
| levenshtein | 12,976 → 12,768 | 0.9840 | 10.829 → 10.933 | 1.0096 |
| navier_stokes | 73,120 → 73,376 | 1.0035 | 85.774 → 86.853 | 1.0126 |
| puzzle | 8,464 → 8,464 | 1.0000 | 8.623 → 8.647 | 1.0028 |
| splay | 54,736 → 54,736 | 1.0000 | 36.323 → 36.272 | 0.9986 |

The compiler total covers parse/AST/analysis and the MIR pass pipeline,
including finalize/link/native publication. MIR Direct charges the pass
manager under `transpile`; its standalone `mir_gen` column being zero does
not mean native generation is missing. Native generation is not separately
isolated here. Process wall samples include the combined cold startup,
generation and execution cost.

**crypto_sha1 cost review:** native ratio 1.1435, compiler
pipeline ratio 1.1733, execution ratio 0.5804.
The bounded proof/guard and existing fallback paths are retained for this
measured gain. Function/variant counts and all native bytes remain in the
artifact. No broader startup improvement follows from the workload result.

### 5.5.1 Memory review

The rejected bump-slot leak is excluded from acceptance. Two new GC tests
exercise shared-list reuse across generic, known-class and bump allocations,
and fixed storage across repeated collections. Separate repeated-main
resource captures and canonical three-pair RSS captures remain diagnostic:

| Repeated workload | Repeats | Control peak footprint MB | Candidate peak footprint MB |
|---|---:|---:|---:|
| puzzle | 300 | 37.6 | 47.6 |
| splay | 20 | 2034.9 | 1808.2 |
| brainfuck | 40 | 41.2 | 40.4 |

| Canonical workload | Variant | Median RSS candidate/control |
|---|---|---:|
| r7rs/fft | untyped | 1.0257 |
| kostya/brainfuck | untyped | 0.9852 |
| kostya/levenshtein | untyped | 0.9943 |
| kostya/json_gen | untyped | 0.7795 |
| larceny/puzzle | untyped | 1.2029 |
| jetstream/navier_stokes | untyped | 0.9982 |
| jetstream/splay | untyped | 0.9386 |
| jetstream/crypto_sha1 | untyped | 1.0442 |
| awfy/havlak | typed | 1.2134 |
| text/prettier_ast | untyped | 1.0784 |

The CPU sampler has its own raw instantaneous and peak footprints.
Sampling perturbs the process: Brainfuck can show a higher sampled peak than
its separate resource capture. No claim of uniform memory reduction follows.
Repeated Splay remains memory-heavy in both binaries. Puzzle keeps a modest
higher footprint while avoiding the rejected multi-gigabyte growth; dead-slot
storage is independently bounded by the same-run collection-burst invariant.

### 5.5.2 Mechanism census

Whole-process executed counts below are separate from accepted timings.
They identify both removed work and remaining costs; they do not assign CPU
percentages to expressions.

| Pilot | Counter | Control | Candidate |
|---|---|---:|---:|
| levenshtein | boxed_min3 | 410,090 | 0 |
| levenshtein | numeric_array_copies | 910 | 0 |
| crypto_sha1 | fn_index | 1,328,200 | 1,000 |
| crypto_sha1 | bitwise_helpers | 1,514,275 | 0 |
| json_gen | fn_join | 140,004 | 4 |
| json_gen | it2s | 230,000 | 320,000 |
| json_gen | string_copied_bytes | 6,615,320 | 2,354,068 |
| fft | fn_index | 1,984 | 0 |
| fft | fn_add | 4,032 | 1,984 |
| fft | index_assign_cow | 1,984 | 1,984 |
| splay | map_shape_field_to_item | 3,079,202 | 3,079,202 |
| splay | map_copies | 951,168 | 951,168 |
| puzzle | numeric_array_copies | 174,909 | 174,909 |
| prettier_ast | it2s | 257 | 257 |

Levenshtein admission, SHA indexed/bitwise helpers and JSON generic joins
meet their intended call-count targets. FFT still retains generic mixed
updates, and Splay projection/copy elimination is unfulfilled. JSON scalar
string conversions can increase as the flattened concat consumes pieces;
Prettier conversion counts are checked independently. The phase completion
conditions therefore remain partial despite the aggregate execution gain.

### 5.6 Rejected candidates and shared allocator admission

The compiler-only candidate `9a0424b1…b66463` was rejected after typed merge
showed a 21-pair ratio **1.0493**, with two-sided 95% lower bound **1.0389**.
Fannkuch exposed a separate profitability issue: new guards increased dense
entries **5,040 → 23,549** on raw swap loops without removing runtime helpers.
The final complete-producer proof requires a newly admitted scalar call.
Raw swaps keep their previous lowering; Levenshtein retains complete native
call admission. The intermediate proof-only candidate still left typed merge
slower, so it was not accepted.

Havlak and typed merge have equal helper/COW counts and identical
relocation-normalized production MIR in that comparison. A 21-pair
control/control check is neutral. CPU sampling identifies substantial
allocation/GC/copy work. It does not isolate the old merge delta to one
microarchitectural event.

Ordinary small runtime container construction now reuses the existing
`heap_calloc_class` path already used by MIR maps and LambdaJS. The same
zeroed size-class allocator, exact slot bitmap, precise roots and ordinary
large-object fallback apply. The implementation preserves required snapshots
and payload copies (S9.1.2, D4.1.1v2, D5.3). This is a shared runtime change;
it does not scalarize arrays or change their public element domain.

The first combined allocator candidate `f42e92a1…40675` was also rejected:
its CPU capture exposed about 3.3 GB of Puzzle storage over 300 repeats.
Sweep unlinked dead bump objects but never recycled their slots. The final
collector reuses them through the existing shared size-class lists, retaining
their bump ownership and exact root recognition. Live-byte accounting now
matches sweep. Repeated-burst tests keep the initial block and cursor fixed.
Fresh repeated-workload memory captures and the canonical three-pair memory
census are retained in the artifact; rejected timings do not qualify this build.

The first memory-correct candidate `03092711…63607a` was rejected after
21-pair Havlak ratios of 1.0720 untyped and 1.1282 typed. Symbolicated samples
and matching native sections identify dead-header pointer chasing in
compaction. A retained breadth-first worklist fixed Havlak but was rejected
after seven-pair Splay ratios 1.0524/1.0641. The final collector retains DFS
marking, completes ephemeron and weak processing, then finalizes/sweeps dead
headers while both source zones are intact (D4.3.3). Compaction visits only
the survivor list and clears marks afterward (D4.3.1). Failed stack growth
abandons collection before weak clearing, buffer movement or sweep. The
finalizer-order and failure paths have explicit regression coverage. This
is a shared collector mechanism, not a row-specific bypass.

The first sweep-before-compaction candidate `1d1a8fa6…22991f` passed the
full Lambda screen but failed JS confirmation: Fasta1.1210 and JSON1.0446.
Both canonical rows performed zero collections. Symbolicated diagnostics
and native code exposed a generic-allocation delegation into the class
allocator. Both entries now use one shared inline slot-construction helper,
retaining the same zeroing and ownership rules without a per-allocation
delegating call. MIR inline budgets and benchmark sources are unchanged.

The rejected decoder experiment remains documented in §5.2. Intermediate
timings and failed candidates are retained in the evidence history and are
not selected as final acceptance samples.

### 5.7 Remaining proposal scope

This shared batch is implemented and performance-qualified. The proposal remains partial:

- General finite array-content ranges and power-of-two remainder masking
  are not implemented; Brainfuck retains two signed divides and an int tape.
- Invariant immutable string-read hoisting is not implemented.
- FFT retains 1,984 generic mixed-domain additions and COW setters; the
  plain parameter keeps its snapshot and observable int/float tags.
- Native record field-consumer/store fusion and activation-local scalarized
  snapshot storage remain open. Puzzle/Splay copies are not proved redundant.
- ASCII `chr` already uses shared immutable storage. No new character append
  API is justified by an intermediate ASCII allocation in this workload.
- Whole-solver reference swaps and recursive restore-on-return remain outside
  the initial tranche; their alias/exit/floating-order proofs are still needed.

Canonical Splay still has different timed setup and a different tree
algorithm from JS. Its matched-key/payload phase diagnostic remains a single
sample. Current Navier ports run 15 timed frames/check77, while setup and
solver structure still differ. JSON floor/truncation and the historical FFT
zero-input oracle are covered by the separate audits (§5.3). The old Splay
4.77× and one-frame Navier claims are not used as equal-work speedups.
