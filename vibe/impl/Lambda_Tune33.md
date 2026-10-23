# Tune33 — Closing the LambdaJS advantages in Result53

**Status:** implementation in progress; boxed predicate lowering implemented
and release pilots qualified. Required aggregate gates finished with one
pre-existing PDF golden failure; the remaining compiler phases and full-plan
performance gates remain open.

**Date:** 2026-10-23.

**Scope:** improve unchanged untyped Lambda programs, retain typed performance, and explain the remaining language/port costs.

**Authority:** S3.4, S4.1.1–S4.1.5, S7.1.1v3, S7.5.3v2, S9.1.2–S9.1.3; D2.4.1–D2.4.3, D3.3.1v2–D3.3.4, D5.3.1–D5.3.4, D8.2.5v3, D8.3.1v2–D8.3.4v4, D8.4.1v2. No semantic change is proposed.

## 1. What the comparison establishes

[Result53](../../test/benchmark/Overall_Result53.md) records LambdaJS ahead of
untyped Lambda on **33/71** workloads. Untyped Lambda wins the other 38 and
has an overall execution-time geometric ratio of **0.6237 relative to
LambdaJS**, approximately 1.60× faster. LambdaJS also beats the typed column
on 21 rows; on 12 of its 33 untyped wins, the typed column reverses the result.
Some typed cells reuse an untyped source, and some typed ports change more
than annotations. These are diagnostic comparisons, not isolated experiments.
The output audit in §7.3 subsequently found two invalid untyped ports in this
historical population; these ratios do not establish output-qualified coverage.

A simpler language creates optimization opportunities, but does not guarantee
cheaper code for every program. Both frontends ultimately use MIR and share
runtime infrastructure; they select different representations, helper calls,
ownership operations and inlining plans. MIR cannot generally recover a raw
array load from an opaque call to a semantic helper. Lambda also has value
semantics, integer poison and error-valued expressions that remain observable
even in small programs. JavaScript's complexity outside a particular hot loop
need not add work to that loop.

The evidence points to three separate causes:

1. **Recoverable compiler costs:** lost array/scalar facts, boxed predicate
   consumers, repeated generic helpers and consequent root traffic; less
   aggressive bounded inlining than LambdaJS.
2. **Costs required by the source contract:** snapshots of mutated plain
   parameters, value-preserving tree updates, and integer overflow/poison
   handling without a sufficient range proof.
3. **Port differences:** typed arrays, character reads, algorithm choice,
   preprocessing, copying versus reference swaps, and timed output boundaries.

Tune33 first targets the first category. The other two require explicit
accounting before a JS ratio can become a compiler acceptance target.

## 2. Evidence and reproducibility

Timings below come from the checked-in
[`benchmark_results_v53.json`](../../test/benchmark/benchmark_results_v53.json):
median of three workload measurements, `LAMBDA_EXEC_BACKEND=jit` versus
`JS_EXEC_BACKEND=mir`, excluding process startup. They are historical
observations, not paired causal measurements. Result53 retains separate
provenance for its three repaired cells and its corrected typed `fast_diff`
timing. The latter was recaptured after moving the timer before preprocessing;
its samples and source hash are in
[`benchmark_results_v53_fast_diff_timing.json`](../../test/benchmark/benchmark_results_v53_fast_diff_timing.json).

Fresh diagnostic runs used the repaired archived release:

- Executable: `test/benchmark/exe/lambda-v53-f3515ebcf3`.
- SHA-256: `67c80a82cad137524222a5fd192b8c0d78404328e73394107bd27d489e9f0290`.
- Source inspected at `cb49b37900dddd572675485c10a4ad15204d9ae3`.
- The inspected runtime/benchmark differences from the repair base are the
  previously recorded Result53 repair. The inspected JS MIR compiler and
  `lambda-eval.cpp` are unchanged from that base.
- Local captures: `temp/tune33_analysis/`, including `manifest.json`, stdout,
  executed-call TSVs, COW TSVs and MIR dumps. The manifest records source hashes.
- Executed counters use `LAMBDA_EXEC_PROFILE=1` and `COW_EXEC_PROFILE=1`.
  **Instrumented elapsed times are not performance measurements.** Counts
  cover the complete process workload, including warmup where present.
- MIR dumps are finalized frontend MIR **before** MIR optimization/code
  generation. They establish emitted mechanisms, not final assembly costs.

Example reproduction from the repository root, using existing diagnostics:

```sh
mkdir -p temp/tune33_analysis
LAMBDA_EXEC_BACKEND=jit LAMBDA_EXEC_PROFILE=1 \
  LAMBDA_EXEC_PROFILE_OUT=temp/tune33_analysis/levenshtein.calls.tsv \
  COW_EXEC_PROFILE=1 \
  COW_EXEC_PROFILE_OUT=temp/tune33_analysis/levenshtein.cow.tsv \
  test/benchmark/exe/lambda-v53-f3515ebcf3 run test/benchmark/kostya/levenshtein.ls

LAMBDA_EXEC_BACKEND=jit \
  LAMBDA_MIR_DUMP_PATH=temp/tune33_analysis/levenshtein.clean.mir \
  test/benchmark/exe/lambda-v53-f3515ebcf3 run test/benchmark/kostya/levenshtein.ls

JS_EXEC_BACKEND=mir \
  LAMBDA_MIR_DUMP_PATH=temp/tune33_analysis/levenshtein.js.mir \
  test/benchmark/exe/lambda-v53-f3515ebcf3 js test/benchmark/kostya/levenshtein.js
```

Run diagnostics serially; `--no-log` suppresses MIR dumps. Timing captures
must unset profiling/dump switches and use a pinned release executable.
The Ruby/legacy C-text backends and retired JS inline-cache paths are irrelevant
to this plan. D8.4.1v2 prohibits adding inline caches to either language.

### 2.1 Representative gaps

All times are milliseconds. U/JS > 1 means LambdaJS is faster.

| Workload | Untyped | Typed | LambdaJS | U/JS | What must be distinguished |
|---|---:|---:|---:|---:|---|
| kostya/levenshtein | 22.498 | 2.822 | 1.532 | 14.68× | Generic array arithmetic; JS typed arrays, hoisted read and inline minimum |
| kostya/brainfuck | 249.711 | 105.682 | 42.738 | 5.84× | Boxed predicates; byte tape, dispatch and remainder differences |
| larceny/puzzle | 13.871 | 14.593 | 2.417 | 5.74× | Plain-parameter snapshot copies in both Lambda variants |
| kostya/json_gen | 24.249 | 6.999 | 4.436 | 5.47× | String result facts and join fusion; different source assembly strategy |
| jetstream/splay | 283.213 | 274.911 | 52.982 | 5.35× | Value-tree updates versus JS mutable links; generic fields and copies |
| jetstream/crypto_sha1 | 48.242 | 28.142 | 10.821 | 4.46× | Bitwise helper/call chains; int53 versus explicit machine-word arithmetic |
| text/fast_diff | 716.586 | 42.611 | 173.386 | 4.13× | Different algorithms; corrected typed timing includes code-array preprocessing |
| jetstream/navier_stokes | 76.534 | 68.482 | 28.935 | 2.65× | Array loop costs plus different swap/solver implementations |
| julia/iteration_pi_sum | 6.808 | 6.808* | 3.210 | 2.12× | Native-loop candidate; no measured hot helper traffic |
| r7rs/fib | 1.549 | 1.581 | 0.935 | 1.66× | Bounded recursive expansion and integer range checks |
| text/text_search | 7732.180 | 1372.130 | 4610.330 | 1.68× | Largest absolute gap; producer/call/index fact propagation |
| awfy/cd | 739.098 | 178.274 | 407.593 | 1.81× | Generic field/index/condition chains |

`*` No separate typed pi-sum implementation. The equal cell is not evidence
that adding annotations has been tested and failed.

Rank by both ratio and saved milliseconds. For example, `text_search`'s
recorded gap is 3121.850 ms, versus Levenshtein's 20.966 ms. These figures are
opportunity rankings, not predictions of attainable speedup.

### 2.2 Executed costs on the archived release

| Workload | Observed executed work | Interpretation |
|---|---|---|
| Levenshtein U | 1,230,274 `fn_index`; 1,230,270 `fn_add`; 410,090 boxed `min3` entries; 22,560,880 root reloads process-wide | Nullable/generic indexed values reach arithmetic and a boxed call boundary |
| Levenshtein T | Zero `fn_index`/`fn_add`; 410,090 raw `min3` calls; 4,102 root reloads | Existing native mechanisms eliminate most of this overhead |
| Brainfuck U | 63,420,212 `is_truthy` calls | A hot predicate-consumption cost remains despite largely native tape operations |
| Brainfuck T | Zero `is_truthy`; both variants still perform 130,000 `fn_chr` and 130,000 `fn_strcat` calls | Condition precision explains an actionable U/T difference; output building is shared residual work |
| JSON U | 360,004 `fn_join`; 360,124 growth/copy events; 7,559,070 copied string bytes | Small string helper results do not consistently reach join fusion |
| JSON T | 4 `fn_join`; 80,000 `fn_strcat_many`; 80,124 growth/copy events; 2,354,068 copied bytes | Existing fusion is a reusable target |
| Puzzle U and T | Each: 174,909 ArrayNum copies, 15,508,598 copied bytes | This is an ownership cost, not merely generic element access |
| Splay U | 951,168 map copies, 62,777,088 copied bytes; 2,622,536 `fn_member_by_id` calls | Both ownership and field/call lowering need separate attribution |
| SHA-1 U | 1,328,200 `fn_index`; 758,400 `fn_bxor_item`; 884,800 `rol` and 316,000 `sha1_ft` boxed calls | Scalar/native entry and bitwise paths deserve direct investigation |
| SHA-1 T | Same `rol`/`sha1_ft` call counts; 632,000 each of `base_type` and `fn_is`; 46,890,125 root reloads | Annotations do not remove the whole boundary/check chain |
| Text search U | 845,994,562 `is_truthy`; 162,653,725 `fn_index`; 109,868,763 `fn_add`; 1,675,060,412 root reloads | Primary absolute-time pilot; Boyer–Moore accounts for 1,664,514,581 reloads |
| Text search T | 3,283,291 `fn_index`; 30,205,818 `fn_add`; 101,118,770 root reloads | Strong improvement, with remaining boxed consumers to investigate |
| CD U | 16,713,168 `fn_index`; 15,345,405 `fn_len_checked`; 15,087,941 `fn_eq_value`; 183,805,477 root reloads | Extend successful array/scalar work into record consumers after profiling the specific paths |
| Fib U and T | Each: 635,621 raw recursive `fib` calls; zero root reloads | Do not prescribe boxing or GC-root removal for this workload |
| Pi sum | Only 81 counted helper calls over the whole process, including warmup | Generic helpers cannot explain millions of inner-loop iterations here; assembly/range analysis remains open |

Counts establish executed work, not percentages of CPU time. Root traffic is
often a consequence of helper calls and erased scalar facts. Remove that cause
before modifying the shared rooting algorithm.

## 3. Source audit: where the explanation changes

### 3.1 Levenshtein and text search: preserve facts through complete paths

Levenshtein's row arrays originate at `fill(m + 1, 0)`, then exchange roles
through `tmp`, `prev` and `curr`. Indexed values feed arithmetic and `min3`.
The generic counts above show a gap at that producer → loop join → read →
consumer chain. The present compiler already has native array loads, bounded
loop proofs, character comparison fusion and small leaf inlining. The work is
to establish the missing proofs and connect these paths.

Text search passes code arrays returned from `to_codes` through the search
functions, with indexed results influencing subsequent loop positions. Its
profile makes this a second pilot for the same analysis, including signed
index/null paths. Both versions record zero `fn_mutable_value` calls in this
capture: the historical repeated-admission diagnosis is not the current
measured bottleneck.

An inferred physical element lane is not a permanent declared `int[]`
certificate (D3.3.3v3). The plan must preserve binding-local facts, exact
call-shape admission and guarded fallback, rather than fabricate annotations.

### 3.2 Brainfuck: preserve predicate results without losing errors

The untyped source repeatedly compares the ordinal opcode with integer
constants. Both typed and untyped lowerings already optimize substantial
array and string work; claiming that every opcode allocates a character is
unsupported. The executed difference is tens of millions of truthiness calls.

Trace the producer's full `MirValue`, native success lane and error edge
through comparison and condition lowering. The recent Queens repair is a
regression guard: a boxed error-capable comparison cannot simply be declared
native boolean (S3.4, D2.4.1–D2.4.3). A guarded successful comparison can branch
directly while its error/null fallback retains the specified semantics.

The JS port also uses `Uint8Array`, `& 0xFF`, an `else if` dispatch chain and
an output array joined once. Lambda uses an integer tape, `% 256`, independent
`if` statements and string appends. These explain why removing truthiness
calls alone cannot be promised to close the entire 5.84× gap.

### 3.3 Strings: extend existing fusion, not a new builder architecture

`json_gen.ls` uses `json_escape` → `json_str` → `json_obj_*` forwarding chains.
The typed profile demonstrates that their joins can use `fn_strcat_many`.
The untyped result facts are insufficient at enough of these edges to leave
360,004 generic joins.

The outer accumulator is **already** using the owned string builder: both
variants record 19,880 in-place appends. It is incorrect to label the measured
Lambda implementation simply quadratic because the source spells `json ++ obj`.
Reuse `mir_string_concat_leaf`, `mir_emit_string_concat_item`,
`fn_strcat_many` and `string_buffer_join`; improve result/argument proofs.
Freeze/alias behavior, left-to-right evaluation and error propagation remain
part of the proof.

The JS port uses fragments plus `join`, and uses `Math.trunc` where Lambda
uses `floor` for negative coordinates. The current outputs both report length
61626, but equality of lengths does not prove equality of generated content.
A future equivalent-work comparison needs a full-content hash and matched
numeric rounding, recorded as a separate benchmark revision.

### 3.4 Recursive/native arithmetic: a real frontend difference

The clean Lambda fib MIR contains two ordinary recursive calls and int53
overflow/poison branches. The JS fib MIR expands one recursive level and
retains deeper recursive calls. This matches the bounded recursion handling
in `mvp_lmd_mir.cpp`'s inline planner. Lambda's existing 48-node leaf plan does
not provide this same expansion on fib.

Use that mechanism as a design reference, with Lambda's own semantic admission
and return rules. A proof that a value is an `int` is not proof that arithmetic
cannot saturate (S4.1.1–S4.1.2). Range analysis can eliminate checks only where
the complete operand/result interval is known. SHA-1 should carry its explicit
machine-word lanes through eligible leaves, preserving wrapping where the
source requests it (S4.1.5), without converting arbitrary `int` arithmetic to
wrapping operations.

Pi sum already has negligible executed helper traffic. Its remaining 2.12×
gap requires final instruction/assembly and dependency-chain inspection;
duplicate conversions and residual range/sentinel checks are hypotheses.
Do not claim a diagnosed cause from the timing alone.

### 3.5 Ownership and algorithm mismatches

Puzzle's `solve(row, cols, diag1, diag2, n)` mutates three **plain** parameters.
Each invocation must preserve the caller's values (S9.1.2–S9.1.3). JS mutates
shared arrays and undoes its writes. A `var`-parameter Lambda version is a
useful separate comparison, but changes the source contract. It cannot count
as a speedup of the unchanged benchmark or authorize skipping snapshots.

Splay's Lambda source explicitly rebuilds rotated subtrees through local
owners; the JS port uses mutable pointer links. The measured copies are real,
but are not all proven redundant. Investigate copies on demonstrably dead or
exclusive owners and preserve snapshots that remain observable.

`fast_diff.ls` fills an LCS table. The JS port uses prefix/suffix stripping,
half-match and Myers-style bisection. The original Result53 version of
`fast_diff2.ls` converted the fixtures to code arrays **before timing**. The
timer now precedes conversion, and Result53's typed cell has been refreshed
from 42.722 ms to 42.611 ms using three release JIT runs. The small timing
difference is not an optimization claim; the timed scope now includes
preprocessing. The algorithm difference still prevents the JS/U and T/U ratios
from isolating compiler performance. Keep the current workload as an engine
regression test, and use an additional matched algorithm to qualify claims.

Navier–Stokes also needs normalization before parity claims: Lambda's
`vel_step` swaps contents in loops while JS swaps references, and JS includes
a combined `lin_solve2`. Source changes belong in a separately reported port
track. They do not replace unchanged-source engine acceptance.

## 4. Proposed implementation sequence

Build on the completed mechanisms in [Tune32 §16](Lambda_Tune32.md#16-phase-ii-implementation-record).
Use the existing typed pass manager and fact ownership (D8.2.5v3), shared index
planner, bounded leaf planner, string builder and MIR emitter. Do not introduce
a second ad hoc inference pass or copy JS lowering code into Lambda.

| Phase | Work | Primary pilots | Exit evidence |
|---|---|---|---|
| 0 | Freeze control; audit ports and hot proof losses | All pilots | Source/binary manifest, output oracles, A/A noise check, rejected-path reasons |
| 1 | Carry numeric array/scalar facts through loops and direct calls | Levenshtein, text_search | Fewer executed index/arithmetic helpers on unchanged sources; guarded misses preserved |
| 2 | Direct predicate consumers from proved success lanes | Brainfuck, text_search | Hot truthiness calls eliminated or reduced by ≥95%; Queens/error controls pass |
| 3 | String result propagation and existing join fusion | json_gen, three_way_merge | ≥90% fewer generic joins in json_gen; lower copied bytes and matching output |
| 4 | Bounded scalar/recursive inlining and machine-word paths | fib/fibfp/tak, crypto_sha1 | Fewer call boundaries; correct overflow, error and stack-limit paths |
| 5 | Reuse loop proofs; inspect already-native loops | pi_sum, array1, navier_stokes | Measured assembly/guard reduction before a speedup claim |
| 6 | Record consumer facts and proven ownership transfers | cd, splay, gcbench | Attribution of generic field work versus copies; only proven redundant copies removed |
| 7 | Full correctness and performance qualification | All 71 rows, both Lambda variants and peers | Qualified unchanged-source gains and explicit remaining limits |

### Phase 0 — Establish the implementation baseline

1. Freeze a current release control, its sources, transitive benchmark inputs,
   build configuration, module binaries and hashes. Retain the Result53 archive
   for historical replay. Do not use a rebuilt shared `lambda.exe` as a moving
   control.
2. Reproduce current output and timings before changing code. Capture the
   executed counter profile separately. Classify each pilot's exact fact loss:
   producer, local merge, array read, call admission, return or predicate.
3. Add bounded optional rejection diagnostics to existing specialization
   diagnostics only where necessary. Record why an existing fast path fails;
   count no imports as executed operations.
4. Audit workload equivalence for all 33 wins, starting with the mismatches in
   §3. Preserve original benchmark rows. Any source normalization gets its own
   before/after comparison and a full result oracle.

**Completion:** pilots have a frozen control and a source-backed causal
experiment ready; observational ratios remain labeled as such.

### Phase 1 — Numeric facts across loops and calls

1. Trace `mir_fill_initializer_element_type`, local/loop fact merging,
   `mir_exact_array_contract_for_object`, `MirIndexLoadPolicy`,
   `mir_index_expr_is_native_int` and scalar consumers in
   [`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp).
2. Preserve the physical lane through provably compatible row-array swaps and
   loop joins. Invalidate it on reassignment, widening or an escaping mutation;
   retain the full nullable/error contract in `MirValue`.
3. Extend the existing guarded index plan when layout, integer index and
   live bounds are proved. Carry the successful raw lane through arithmetic
   and a fully admitted native call edge. Out-of-bounds/null/mismatch paths
   continue through their specified fallback.
4. For text search, propagate bounded producer/call facts without re-admitting
   entire arrays on each search. Guard the complete required shape at the
   existing admission point (D8.3.1v2–D8.3.3); do not specialize a partial
   argument tuple as if the full raw entry were proved.

**Proposed performance target:** ≥20% less untyped execution time on each of
Levenshtein and text_search, with ≥80% fewer hot generic index/add calls in
the specific optimized regions. Targets are acceptance goals, not forecasts.
Keep the scalar/array work independently measurable from later inlining.

### Phase 2 — Predicate consumers

1. Inspect `mir_condition_may_error`, `mir_equality_needs_item`, comparison
   emission and condition demand using Brainfuck's actual opcode producer.
2. A proved boolean success value should reach a direct MIR branch. If its
   producer also has an error edge, preserve that edge explicitly rather than
   retagging the entire result. Evaluate each operand once and preserve
   short-circuit evaluation (S3.4, S7.5.3v2).
3. Apply through shared expression/condition handling so text search and CD
   can benefit. Avoid a Brainfuck opcode recognizer.

**Exit:** zero hot `is_truthy` calls where the entire predicate is proved, or
≥95% reduction for the measured opcode region, plus a ≥15% incremental
Brainfuck improvement. Re-run the Queens representation regression and
nullable/error logical consumers in interp, JIT and auto tiers.

### Phase 3 — String forwarding and joins

1. Carry successful string result facts through eligible local forwarding
   functions while keeping actual boxed/raw carriers separate.
2. Reuse current concatenation flattening and owned-buffer helpers. Validate
   aliases, freeze points, self-concatenation, RHS rebinding, Unicode, and errors.
3. Keep runtime builder changes conditional on a measured residual cost;
   the existing builder already supports in-place append and geometric growth.

**Exit:** json_gen generic joins fall by ≥90%, copied bytes fall, and untyped
execution improves by ≥20%. The unchanged `61626` oracle is necessary; a
matched-port full-content hash is required before claiming JS parity.

### Phase 4 — Calls, recursion and machine-word arithmetic

1. First make exact raw admission/result facts reach the existing leaf planner
   for `min3`, `rol` and related scalar leaves. Explain each rejected call
   before increasing the planner's scope.
2. Add a bounded recursive expansion policy through that planner: one level
   initially, an explicit node/code budget, and a real deeper recursive call
   with its stack guard. Restrict initial eligibility to completely admitted
   scalar inputs and understood return/error contracts; exclude suspension,
   captures and mutation until separately justified.
3. Retain int53 saturation/poison checks unless existing interval analysis
   proves them unnecessary. Preserve explicit u32/i32 wrapping and shift rules.
4. Use `mvp_lmd_mir.cpp`'s `inline_budget`/`inline_call_body` as evidence of a
   working mechanism. Extract shared mechanics only after both clients have
   converged; semantic admission stays language-specific (D8.2.3v2).

**Exit:** ≥15% improvement on at least two qualifying recursion/bitwise pilots,
matching outputs on arithmetic boundary cases, and bounded code size. A failed
pilot does not justify unrestricted inlining or removal of stack checks.

### Phase 5 — Loop proofs and native arithmetic

1. Obtain the actual imported pi-sum function's MIR and final machine code;
   a final module dump can be overwritten by another module. Inspect integer
   induction ranges, repeated conversions, arithmetic dependencies and spills.
2. Extend existing `mir_int_lane_interval`/loop region proofs where they can
   discharge repeated checks. Hoist layout/bounds checks only across operations
   that preserve those facts; reload moved owners after `MAY_GC` calls.
3. Do not reassociate floating-point sums or replace division with an
   approximation. Require the same numeric oracle and source evaluation
   semantics. Treat the Navier–Stokes source differences separately.

**Exit:** retained changes have an identified instruction/guard reduction and
a paired improvement. If assembly does not reveal a defensible transformation,
leave this phase open with the diagnosis rather than invent a boxing cause.

### Phase 6 — Records, ownership and residual rooting

1. Profile CD's tree lookup/voxel paths and Splay's rotations separately.
   Extend the existing guarded record-field/constructor plans to preserve
   shape and scalar facts where the binding/call proofs permit it.
2. Use COW site counters to separate required snapshots, unique mutations and
   avoidable copies. Reuse existing last-use/ownership-transfer machinery only
   when no plain parameter, retained alias, result or closure observes the old
   value. Avoid introducing whole-program alias speculation for this phase.
3. Re-measure roots after helper elimination. The shared emitter already
   reloads live root candidates after `MAY_GC` calls. Any residual improvement
   must prove narrower liveness or a correct helper effect contract; never
   suppress required reloads or restore conservative stack scanning.
4. Keep Puzzle as a snapshot correctness/performance control. A separate
   borrowed-array port may quantify the semantic cost but cannot satisfy this
   phase's unchanged-source acceptance gate.

**Exit:** ≥15% improvement on a profiled record pilot with a corresponding
reduction in helper calls or demonstrably redundant copies. No promise to
remove all 951,168 Splay copies or make Puzzle match JS under different
ownership contracts.

## 5. Correctness, performance and stop rules

For each implementation slice, add focused semantic and emitted-mechanism
coverage alongside the existing MIR fixtures. New `.ls` fixtures require
goldens. The essential adverse cases are:

- Empty/zero-trip loops, negative/out-of-range indices, nullable reads,
  widening arrays, swaps with a different lane, effects between guard and use.
- Error-valued comparisons/logical operators, Unicode and empty string reads,
  and RHS expressions that allocate or rebind an owner.
- Aliases and plain-parameter snapshots, borrowed write-back, nested COW,
  forced moving GC across fast/slow paths, scalar-home lifetimes.
- int53 limits, poison, machine-word wrap, recursion depth, and boxed fallback
  calls with inputs outside the inferred exact shape.

At retained phase boundaries run `make test-lambda-baseline`, relevant MIR
emission/ratchet and forced-GC gates, and `make test262-baseline` when runtime
or shared emitter code changes. Use `caffeinate -is` for long macOS gates.
The prior Result53 repair recorded an inherited PDF pixel-golden baseline
failure; re-establish its status on the frozen control and report it separately,
without weakening a gate or declaring a partial baseline fully green.

Performance qualification:

1. Build release only; archive control and candidate. Baseline targets can
   replace the shared executable with debug, so always hash the timed archive.
2. Use serial, interleaved paired runs with unchanged inputs and verified
   outputs. Start with an A/A noise check and a short breadth screen. Confirm
   retained pilots and suspected regressions with at least 21 pairs and paired
   confidence intervals; lengthen tiny workloads in an identical dedicated
   harness for both sides when process/timer noise obscures the result.
3. Run all 71 workloads for untyped and typed Lambda at closeout, with LambdaJS
   and Node controls. Keep peer-source equivalence labels. Report workload
   time, compilation/code generation, end-to-end time, allocation/copy counts
   and MIR/code size separately. Four Result53 LambdaJS end-to-end rows timed
   out; do not treat them as finite control measurements.
4. Proposed release budget: overall untyped execution geomean ≤0.95 of control,
   typed and unaffected LambdaJS geomeans ≤1.01, and no confirmed per-row
   regression >3%. Compile-time and code-size growth >10% on a pilot requires
   narrowing/review before acceptance. Report summed medians separately from
   geometric means; the former reflects this workload mix, not a universal
   application speedup.
5. Reject sleep-interrupted, retried or host-contended acceptance runs. Keep
   failed attempts labeled; do not alter timeouts or output checks to qualify.

Success is a measured reduction in avoidable work on unchanged Lambda source,
with semantics and peer performance preserved. Closing every JS ratio is a
longer-term target contingent on equivalent work; it is not the completion
criterion for this bounded implementation round.

## 6. Initial analysis and remaining uncertainties

This proposal is based on live source inspection, Result53 recomputation,
18 successful Lambda diagnostic executions and successful JS reproductions
for Levenshtein, Brainfuck, JSON generation and Fibonacci. Diagnostic output
includes Levenshtein's complete distance vector, Puzzle's 724-solution check,
text search's 91395120 checksum and CD's 4305-collision result. An initial
attempt to use `r7rs/fib.js` found no file; the runner's actual `fib2.js` path
was then used successfully. No engine implementation or benchmark result
snapshot was changed during this analysis.

The observed helper/copy counts and recursive expansion are established.
Their individual CPU shares, the full pi-sum/native-loop explanation, and
port equivalence for the remaining smaller wins are still open. Phase 0
resolves those uncertainties before accepting changes for those workloads.

### Source touchpoints

| Source | Existing mechanism to inspect or extend |
|---|---|
| [Lambda MIR compiler](../../lambda/runtime/transpile-mir.cpp) | `MirIndexLoadPolicy`, `mir_exact_array_contract_for_object`, `mir_index_expr_is_native_int`, `mir_int_lane_interval`, `mir_condition_may_error`, `mir_equality_needs_item`, `mir_inline_call_target`, string fusion and record plans |
| [Shared MIR emitter](../../lambda/runtime/mir_emitter_shared.hpp) | `em_finalize_recorded_roots`, `em_root_reload_live_values_after_call`; change only after a residual root/effect defect is demonstrated |
| [Runtime helpers](../../lambda/runtime/lambda-eval.cpp) | `string_buffer_join`, existing COW/string profiles and executed-call instrumentation |
| [JS MIR compiler](../../lambda/js/mvp-lmd/mvp_lmd_mir.cpp) | `typed_array_lanes`, native `charCodeAt` lowering, bounded inline planning and recursive expansion as comparative evidence |
| [Benchmark registry](../../test/benchmark/run_benchmarks.py) | Actual source paths, include expansion, workload wrappers and tier selection |
| [Standard benchmark runner](../../test/benchmark/run_standard_benchmarks.py) | Release provenance, typed expansion, statuses and result serialization |

## 7. Implementation record — boxed predicates

### 7.1 Retained mechanism

Phase 2 now recognizes scalar comparison results as the closed
`bool | null | error` Item domain. A branch admits exactly `ITEM_TRUE` using
one MIR word comparison. The existing error-value routing remains responsible
for returning an encountered error and skipping the corresponding branch or
logical RHS (S3.4–S3.5, D2.4.1–D2.4.3).

The bounded structural proof also covers `and`/`or` trees whose operands each
belong to this domain. It does not infer a predicate domain from an arbitrary
binding's stale type. An `and` returning a numeric value and a keyword
elementwise comparison producing an array both retain general truthiness
(S3.1v2). The existing helper is shared by conditional and logical consumers;
the former special case for integer-ordering joins is subsumed by it.

Fresh executed-call profiles record `is_truthy` falling from **63,420,212 to
zero** in untyped Brainfuck and from **845,994,562 to zero** in untyped text
search. These are diagnostic counts, not instrumented timing claims.

The new `tune33_boxed_predicates` fixture covers all six scalar comparisons,
nulls, condition errors, logical error retention, compound predicates, a
non-boolean selected operand, array-valued masks and single evaluation of
side effects. Its golden is also registered in the tier-parity suite. The
existing Queens regression now pins comparison with the canonical true Item
instead of requiring the removed `is_truthy` call.

### 7.2 Investigation disposition and qualification status

The initial Phase 1 probe found that Levenshtein's `fill` bindings already
retain integer element candidates. Their receiver values still carry the
conservative error possibility through untyped length calls; the admitted
string entry needed to discharge it is not established. A prototype that
discharged stale system-operand facts for already admitted scalars did not
change Levenshtein's 1,230,274 generic index calls or 1,230,270 generic adds.
That prototype and its temporary binding diagnostics were removed. No native
array proof was asserted for those error-capable receiver values.

The frozen release control is `temp/tune33_impl/control`, SHA-256
`67c80a82cad137524222a5fd192b8c0d78404328e73394107bd27d489e9f0290`.
Control/source manifests, release build logs, diagnostic profiles and paired
captures are under `temp/tune33_impl/`. An initial three-pair A/A screen over
Brainfuck, Levenshtein and JSON generation passed output and source stability
checks. A seven-pair scalar-predicate screen measured Brainfuck at 0.6335 of
control time; this is preliminary evidence only.

The first longer capture was interrupted after adding the vector-mask fixture
changed its source inventory. Its rejection record is
`predicates_qualification.rejected.json`; no partial samples from it qualify
the change. The replacement `predicates_qualification_frozen.json` completed
with unchanged source inventory and binary hashes, 21/21 valid matching-output
pairs per row, and no timeouts or retries:

| Workload | Control ms | Candidate ms | Candidate/control | One-sided 95% upper |
|---|---:|---:|---:|---:|
| Brainfuck untyped | 249.853 | 157.217 | 0.6292 | 0.6328 |
| Brainfuck typed | 107.708 | 107.460 | 0.9977 | 1.0054 |
| Text search untyped | 7752.930 | 6590.410 | 0.8501 | 0.8520 |
| Text search typed | 1372.950 | 1372.930 | 1.0000 | 1.0051 |

This confirms the Phase 2 Brainfuck target and an additional approximately
15% execution-time reduction in text search. It does not satisfy Phase 1's
separate array/arithmetic targets. The candidate is
`temp/tune33_impl/candidate`, SHA-256
`323df1a2a2064efc079ad03368686a95a269bc626328cecefe598421a0929d3a`.
The permanent local archive is
`test/benchmark/exe/lambda-tune33-predicates-cb49b37900`, with its source patch
and manifest alongside it. Both executable files are 25,625,256 bytes.

Phase 1 and Phases 3–6 remain open. The predicate implementation does not
complete the entire tuning plan or establish its overall population target.

### 7.3 Benchmark destination contracts

The 142-row output-checked screen found two existing untyped port failures on
**both** the frozen control and candidate:

| Port | Observed output | Existing oracle |
|---|---|---|
| `beng/spectralnorm.ls` | `nan.000000nan` | `1.274219991` |
| `kostya/matmul.ls` | `matmul: sum=0` | `matmul: sum=-29562` |

Both wrote an output array through a plain parameter, whose mutations affect a
snapshot under S9.1.3. Their destination parameters now use `var`; matmul's
caller also declares the matching `float[]` place contract required by
D3.3.3v3. No golden was changed. Both corrected scripts match their existing
goldens under JIT and auto on both binaries (eight checks), and their 21-pair
reruns retain matching outputs throughout. These source repairs are not counted
as engine improvements: each retained row compares the same corrected source
on control and candidate. Result53's historical cells remain separate.

### 7.4 Population and peer screening

[`benchmark_results_tune33_predicates.json`](../../test/benchmark/benchmark_results_tune33_predicates.json)
retains every numeric paired sample, status, output hash, entry-source hash,
paired confidence interval and capture metadata used below. Full stdout/stderr
and corpus inventories remain in the raw captures under `temp/tune33_impl/`;
their file hashes are recorded in the compact artifact. It verifies that all
142 selected entry-source hashes match the current files and are identical
between the timed lanes.

The breadth screen ran all 71 canonical workloads in both Lambda variants,
three serial alternating pairs per row, with zero retries. There are 130 rows
with existing expected-output text checks; the 12 legacy JetStream rows without
separate goldens retain complete output equality and in-script checks. The two
failed ports above are retained as failed captures and replaced only by their
explicit corrected-source reruns.

Four screen slowdowns above 3% were followed with 21 pairs: untyped Fibonacci,
typed regexredux, untyped quicksort and typed matrix statistics. The last two
also received 31-pair captures. None has a two-sided 95% lower bound above
1.03. Quicksort remains inconclusive for a 3% noninferiority bound: its latest
ratio is 1.0264, with one-sided 95% upper bound 1.0411. This is a roughly
one-millisecond workload; the screening result does not certify a ≤3% bound.

The population below uses 21-pair pilot/followup rows, the two 31-pair
confirmations and the remaining three-pair breadth rows. It is a descriptive
screen, not an equally sampled full-population qualification:

| Variant | Rows | Geometric candidate/control | Summed-median ratio |
|---|---:|---:|---:|
| Untyped Lambda | 71 | 0.9792 | 0.9779 |
| Typed Lambda | 71 | 0.9865 | 0.9973 |

The approximately 2.1% untyped geometric reduction falls short of the proposed
5% overall target. The separate ≥15% Brainfuck predicate target is met.

Seven LambdaJS peers were screened on the same two releases: Fibonacci,
floating Fibonacci, Brainfuck, Levenshtein, JSON generation, splay and SHA-1.
Their outputs match across all captures. Floating Fibonacci's initial 6.0%
screen slowdown led to a 31-pair confirmation: ratio 1.0217, lower bound
0.9944 and one-sided upper bound 1.0586. It has no confirmed >3% regression,
but its bound remains inconclusive. The other peers' screen ratios range from
0.9686 to 1.0074. This is partial peer coverage; Node and the remaining JS
workloads were not recaptured in this implementation slice.

Compile-phase attribution and final native-code-size qualification remain
open. Process wall times are retained separately from the workload timer;
the unchanged executable file size does not establish unchanged generated
code size. Phase 7's full LambdaJS/Node, compile-time and code-size gates are
not claimed complete.

### 7.5 Correctness qualification

The new predicate fixture matches its golden under interpreter, JIT, auto,
and forced moving-GC with freed-memory poisoning on the archived candidate.
The existing scalar-flow and numeric-read fixtures also match under all
three execution tiers.

`check_gc_root_hazards.py` passes for 17,948 migrated native functions.
`check_gc_effects.py` reports unresolved calls in existing allocator,
certificate and scalar-accessor code. Replacing only the candidate transpiler's
parsed function definitions with the saved HEAD definitions produces exactly
the same diagnostics (SHA-256
`c308cd48a470c82a2e7b3e2cd755835fca4b2360d0b5d1f7db66462f9e6b1f13`).
The checker and effect catalog are unchanged.

`make test-lambda-baseline` finishes at **8075/8076**: input parsers
2112/2112 and runtime 5963/5964. The sole failure is
`PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`.
The saved control reproduces exactly the same phase-6 object-9 image mismatch
at byte 760 under interpreter and JIT; this patch leaves that pre-existing
failure unresolved. In the aggregate run, MIR emission passes 252/252,
MIR ratchets 20/20, forced-GC stress 323/323, Lambda scripts/tier parity
1719/1719, and optimization contracts 54/54. The new fixture participates in
the JIT-forced, randomized-forced and interpreter-forced GC schedules.

`make test262-baseline` passes **40,261/40,261** in 284.6 seconds, with zero
regressions, non-fully-passing cases or retries; 2,652 tests outside the baseline
are skipped. Its rebuilt release host has the same SHA-256 as the measured
candidate. Logs and the control PDF reproduction record are under
`temp/tune33_impl/`; the compact benchmark artifact records gate results and
log hashes. `git diff --check` passes.

The next compiler work is Phase 1's complete string/array admission proof
through Levenshtein's untyped calls. Reusing existing element hints alone did
not establish the receiver's non-error contract (§7.2). The remaining port
equivalence audit and Phase 7 qualification also remain open.
