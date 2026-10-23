# Tune33 — Closing the LambdaJS advantages in Result53

**Status:** scoped compiler mechanisms for phases 1–6 implemented and full-population
release evidence recorded in §8. Two numerical phase targets remain unmet,
long-peer timing confidence is limited, and the aggregate Lambda baseline retains
two control-reproduced failures. The earlier predicate-only evidence is in §7.

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
At the proposal checkpoint, their individual CPU shares, the full
pi-sum/native-loop explanation, and port equivalence for the remaining smaller
wins were still open. Section 8 records the subsequent assembly, diagnostic
and source audit; executed counts alone still do not establish CPU shares.

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

At that checkpoint, Phase 1 and Phases 3–6 remained open. The predicate implementation does not
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

At that checkpoint, the next compiler work was Phase 1's complete string/array admission proof
through Levenshtein's untyped calls. Reusing existing element hints alone did
not establish the receiver's non-error contract (§7.2). The remaining port
equivalence audit and Phase 7 qualification also remained open at that point.

## 8. Coordinated implementation of the remaining compiler phases

This section supersedes §7's remaining-work statement. The source algorithms
and Result53 cells are unchanged by this implementation. Phase 2 was already
present in the new control, so the new paired ratios measure the incremental
effect of phases 1 and 3–6. Historical Result53 comparisons retain their own
source and executable provenance.

### 8.1 Shared mechanisms

**Numeric producers and call facts (phase 1).** An admitted String or ArrayNum
receiver now refines the compiler's conservative `len` defect classification.
The whole-source defect table remains conservative; only the currently
admitted carrier can exclude a deferred host read (D6.1.3, D8.3.3). Open Array
carriers retain virtual-child-array handling. The system operand error walk
also accepts an already established scalar carrier witness. No array element
candidate is treated as an admission certificate (D3.3.3v3).

This connects the existing fill, swap, guarded index and scalar arithmetic
paths in untyped Levenshtein and text search. It does not remove a nullable
array read's fallback. In particular, Levenshtein's `min3` still receives
possibly absent lanes through its boxed entry; fully inlining that call would
need a complete additional admission proof. The native array reads and
arithmetic before that boundary are retained independently.

A true ordering predicate also proves its stable operands present (S6.1.2).
The existing region-fact owner now carries that proof into both conditional
arm queries and emission. Thus `if (step > 1) step else 1` has a non-null
integer result even when `step` came from a nullable read. This preserves the
raw carrier through the following loop update without declaring an absent
value present on an unguarded edge. Error-capable predicates and unrelated
nullable selections remain boxed.

**Forwarded strings and fusion (phase 3).** Closed string call edges now cover
forwarding and joining bodies as well as indexing bodies. The extension
requires a string-valued result or a native string length/index consumer;
plain forwarding into boxed record/array storage keeps Item arguments. This
avoids adding native-entry guards and unbox/rebox work without a consumer
benefit (D8.3.3). The existing
call-site result cache carries scalar/string results through acyclic helper
chains. Each helper has its existing expression budget; an epoch sentinel
breaks cycles and a 32-helper depth cap bounds compiler recursion. String join
candidates and finalized native result metadata feed the same argument fact
resolver. Exact call admission and boxed misses remain responsible for unseen
values (D2.4.1–D2.4.3, D8.3.3). Boxed-entry reachability propagates through
the existing direct-call graph separately from the fast-shape candidates. A
public function's fallback can forward a symbol to a helper whose internal
calls established a string candidate; that helper must retain its own boxed
body. The new `forward_key` fixture pins this path through a dynamic function
value. This fixes the five LaTeX regressions exposed by the initial prototype.
Existing concatenation and owned-buffer helpers perform the work; no second
string builder was introduced.

**Scalar calls and recursion (phase 4).** The existing 48-node inline planner
admits bounded nested scalar calls and one recursive expansion level. It
allows at most two nested inline frames and 16 expansions per emitted MIR
function. A deeper recursive edge remains a real guarded call. The predictive
array-write scan and actual lowering share the bounded inline path and
expansion accounting (D8.2.5v3, D8.3.3). The scan restores the emitter's budget
after analysis and visits scalar argument effects in the caller's scope.
This prevents an inlineable recursive helper from overflowing the compiler
before lowering. Parameter binding locators are restored after expansion. An inline body cannot
use the caller's tail-call state, and calls within the same tail-recursive
activation retain the existing raw backedge.

Pure bitwise and numeric-cast leaves can use the Item result join. Checked
sized local declarations and negative-shift errors leave that join with their
original error, including when an inferred native success result also has a
defect arm. Explicit return contracts and raising signatures retain their
existing exclusions. Saturation and poison checks remain unless an existing
interval proves them unnecessary (S4.1.2, S4.1.5, S4.4.4). `rol` benefits;
`sha1_ft` and calls with incomplete scalar admission retain ordinary calls.
`tak` retains its existing tail-call path.

**Finite loops and conversions (phase 5).** The shared finite loop planner
accepts stable affine bounds with a proved finite interval. It retains source
evaluation order and rejects bound writes. Literal/affine scalar loops can
earn the same guarded finite sibling without needing an array scan. Bounds
use their proved maximum rather than the generic INT32 ceiling. Dominated
finite facts reach integer-to-double consumers, so MIR can merge duplicate
conversions and move the invariant bound calculation. Floating-point addition,
multiplication and division keep their original order and precision (S4.1.2).
Scalar copies require a numeric conversion on a variable/expression and a
128-node body budget. Collection construction, joins and general calls are
excluded. Stores around a nested arithmetic loop remain eligible: the outer
counter's finite bound is needed to prove the inner affine bound. Excluding
all array stores also excluded Pi's successful proof and was rejected during
assembly qualification. This profitability rule avoids duplicating allocating
string/collection builders merely to remove a counter check.

**Record consumers and ownership (phase 6).** The existing bounded shape
candidate fixpoint also follows values assigned to members. These remain
compile-predicted candidates, each checked against the live descriptor; they
are not runtime inline caches (D8.4.1v2). Null and boolean slots can retag in
place. Their guarded read therefore uses the existing live
`map_shape_field_to_item` descriptor reader, preserving the new tag and width,
rather than interpreting a slot using its original tag (D2.6.4v3).
The shared reader gives non-nullable container-pointer and null/error Item
slots a direct descriptor dispatch. Both descriptor and general field reads
reuse the same unaligned-safe slot readers, retaining virtual-carrier tags,
foreign raw-container normalization, empty slots and nullable native lanes.

The COW census still records Splay's value-tree copies; its source rebuilds
rotations through local owners and retained aliases. Puzzle's plain parameters
retain caller snapshots. Neither supplies a proof for removing those copies, and no
copy elision or root-reload suppression was added (S9.1.2–S9.1.3, D5.3.3).

### 8.2 Regression coverage and qualification method

Four new golden and MIR-emission fixtures cover string forwarding and aliases,
native-length/fill/swap paths, widening and out-of-bounds reads, numeric
recursion and int53 overflow, checked u32 wrapping and invalid shifts, finite
and changing loop bounds, record retags, descriptor misses and retained map
snapshots. All four are registered for interpreter/JIT/auto parity and are
discovered by the forced moving-GC suite.

The existing optional-int boundary fixture now includes a non-string argument
and an error argument. Its original generic `ord` error-join assertion remains
observable beside the newly eligible closed-string optimization. The scalar
home fixture checks the inlined float leaf's `push_d`; the integer-index
product fixture checks the generic sibling's surviving compact-window guard.
The original boundary checks remain, with two additional golden results for
the new rejected operands.

The first coordinated control and candidate share HEAD
`c86e403b0f2421e43949abfdda0343552374f77b`; their production source manifests
differ only in `ast.hpp` and `transpile-mir.cpp`. Control SHA-256:
`9653f082532ab6db75d3dc37893273bb40e3bb75e2172adbce22843cad5623be`.
Candidate SHA-256:
`70117dbd8798e1495d906301c3ebb2bb3ec29bfefbf015bc953d264aa34686f0`.
These hashes identify the first pilot candidate, subsequently rejected for a
confirmed typed-text-search regression. Pinned executables and full
diagnostic/native-assembly captures are under
`temp/tune33_all/`. Instrumented times do not qualify execution improvements.

Release validation uses serial alternating pairs under `caffeinate -is`.
The final three-pair control/control check over Levenshtein, JSON generation and
Splay retained matching output, with median ratios 0.9939, 1.0052 and 1.0011.
The final executable passes all 256 MIR-emission checks. The broader candidate
also passes all 327 forced-GC checks; all five Tune33 fixtures match their
goldens in interpreter, JIT, auto and forced-GC modes. Final aggregate and full-population results are recorded in §§8.5–8.7.

The added `[int(inf)]` and `[int(nan)]` typed-call cases exposed a pre-existing
compiler crash on the frozen control too. The array-element predictor cast a
compact global `integer` meta-type to the larger `TypeType` layout. It now
uses the existing checked declaration unwrap helper; it grants no new array
certificate or payload kind. Both cases remain in the fixture.

### 8.3 Final candidate provenance and rejected attempts

The final control is the fresh HEAD release above, SHA-256
`9653f082532ab6db75d3dc37893273bb40e3bb75e2172adbce22843cad5623be`.
The final candidate is SHA-256
`85686ea034e13da9c18312cb3d0f7a0625da030987669401759f21824683ef7a`.
Its production manifest differs from that control in exactly three files:
`lambda/runtime/transpile-mir.cpp`, `lambda/runtime/ast.hpp`, and
`lambda/core/lambda-data.cpp`. The final Test262 build reproduced the timed
candidate byte for byte. The corpus, build inputs, module binaries, executable
and benchmark source hashes accompany each timing capture.

The earlier candidates are excluded from final performance aggregates:

- The first candidate regressed typed text search by 4.36% in a paired
  confirmation. Copying allocating loop bodies without a numeric consumer
  benefit was removed. Its expanded inference also exposed the missing
  transitive boxed fallback described in §8.1; five LaTeX failures caught it.
- An over-restricted replacement rejected every array store and lost Pi's
  outer induction proof. Imported-function assembly showed that the intended
  finite inner loop was absent. This partial capture was rejected.
- The next candidate passed the pilot set but crashed the compiler on typed
  `divrec`. LLDB identified recursive predictive array-write analysis without
  the emitter's inline bound. The shared path/budget in §8.1 fixes the cause;
  a tail-recursive scalar helper called inside a loop now covers it.
  The same candidate regressed untyped Prettier by 3.34% in 21 pairs. String
  admission into boxed record constructors added over 3.35 million `it2s`
  calls. Consumer-driven admission removes those conversions: the final
  Prettier control and candidate both execute 257 `it2s` calls and 18,809,091
  frame entries. Both cases receive fresh final-candidate confirmation.

### 8.4 Compilation and code-size review

These measurements are separate from uninstrumented workload timing. Each
compile entry is the median of five whole-process phase captures, summing
the **own** compile cost of every imported module exactly once. Native bytes
sum every generated function in those modules. Pi includes `micro_common`;
the final entry-module dump alone would omit its hot function. The existing
`transpile` counter includes MIR scheduling and native generation; its zero
`mir_gen` field does not indicate free native code generation.

| Untyped pilot | Native bytes, control → candidate | Own compile ms, control → candidate | Cost review |
|---|---:|---:|---|
| fib | 2,624 → 3,776 | 2.226 → 2.844 | One recursive level halves actual call boundaries; depth 2 / 16 expansions caps growth |
| fibfp | 2,768 → 3,456 | 2.331 → 2.668 | Same bounded expansion; also halves executed integer-to-double helper conversions |
| tak | 3,632 → 3,632 | 2.926 → 2.944 | Existing tail-call path retained |
| Levenshtein | 10,400 → 12,976 | 7.941 → 10.927 | Guarded array and string lanes retain complete misses; nullable `min3` stays boxed |
| JSON generation | 11,648 → 12,624 | 9.254 → 10.482 | String lanes require a consumer benefit; generic fallback bodies remain reachable |
| Pi sum | 38,160 → 43,888 | 27.595 → 32.103 | Finite siblings require a numeric conversion, bounded body and no general calls or allocating builders |
| CD | 69,408 → 77,152 | 55.127 → 62.153 | Bounded member-source candidates, exact live-shape guard and generic miss; no demonstrated whole-workload gain |
| Splay | 40,320 → 54,736 | 26.748 → 35.984 | More guarded field sites, with required fallbacks; modest execution gain and a substantial compile/code tradeoff |
| SHA-1 | 22,960 → 25,984 | 18.206 → 21.230 | Bounded scalar/bitwise expansion retains checked local declarations and invalid-shift errors |

Every entry exceeding 10% received this review. Scope was narrowed by the
consumer rule, the 128-node scalar-copy rule, and shared inline limits;
unrestricted forwarding, allocating loop duplication and unrestricted
recursive analysis were rejected. Remaining growth is explicit. In particular,
CD and Splay do not justify a claim of uniformly better startup latency.
Removing their safety fallbacks merely to meet a size target would violate
D8.3.3. Process wall times are retained alongside workload timers in the
final evidence, so compile tradeoffs remain visible. These measurements
qualify the forced JIT lane, not automatic-tier promotion economics.

### 8.5 Correctness closeout

The final `make test-lambda-baseline` passes **8,095/8,097**: input
**2,112/2,112**, runtime **5,983/5,985**. The two failures reproduce on the
fresh frozen control: `PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`
(object 9, byte 760) and `JavaScriptTests/JsFileTest.Run/crypto_key`.
They remain unresolved; this is not a fully green aggregate baseline.

The final focused gates pass MIR emission **256/256**, MIR ratchets
**20/20**, forced moving GC **327/327**, Lambda scripts/tier parity
**1,720/1,720**, and optimization contracts **54/54**. All **142/142**
release Lambda benchmark variants pass the output census: 130 use their
existing golden text, and 12 legacy JetStream rows use complete control
stdout equality plus in-script result checks. Census timings are discarded.

`make test262-baseline` passes **40,261/40,261**, with zero regressions,
non-fully-passing cases or retries; 2,652 outside-baseline cases are skipped.
The root-hazard audit passes for **18,020** native functions. The effect
checker still reports **21** existing diagnostics: replacing the changed
production definitions with HEAD produces the identical set, with zero new
or removed diagnostics. Its nonzero status is preserved, not called a clean
effect audit. No effect catalog or regression budget was relaxed.

<!-- tune33-final-results:start -->
### 8.6 Full-population performance closeout

The durable evidence is [benchmark_results_tune33_all.json](../../test/benchmark/benchmark_results_tune33_all.json). It contains every paired sample/status/output digest, the 71-workload comparison matrix, binary/source/build/module provenance, all 33 historical-gap diagnostics, ownership-site attribution, compile samples, native-code counts, and correctness results. Historical Result53 cells are unchanged. The replay binaries are `test/benchmark/exe/lambda-tune33-control-c86e403b0f` and `test/benchmark/exe/lambda-tune33-all-c86e403b0f`; the latter has its production source patch and manifest alongside it.

All captures use pinned release binaries, serialized alternating pairs under `caffeinate -is`, unchanged benchmark sources, and zero retries. The 32 pilot rows use 21 pairs; the full 142-row Lambda screen uses three pairs. Suspected slowdowns and additional screened gains have their recorded 21-or-more-pair confirmations. Aggregates use each row's most-sampled capture; they describe this mixed-sample population, not a uniform-confidence application speedup.

| Lane | Rows | Candidate/control workload geomean | Ratio of summed medians | Process-wall geomean |
|---|---:|---:|---:|---:|
| untyped Lambda | 71 | 0.8936 | 0.9230 | 0.9431 |
| typed Lambda | 71 | 0.9717 | 0.9948 | 0.9939 |
| LambdaJS | 71 | 1.0029 | — | samples retained |

LambdaJS covers all 71 peer rows: 67 short rows use at least three pairs; screened slowdowns receive 31-pair confirmation. The four registered 600-second JQ rows use a **one-pair output/performance census**, with no useful uncertainty claim. All 71 Node controls use three runs on pinned **Node v22.13.0**. Node and cross-language ratios are peer observations, not the causal A/B estimate of the Lambda compiler change. The JQ Lambda ports are direct queries while JS runs a JQ interpreter, so those large cross-language ratios describe different work.

There are **12 confirmed untyped improvements** and **7 confirmed typed improvements** (two-sided paired-bootstrap upper bound below 1 on at least 21 pairs). Confirmed regressions above 3%: **0**. The artifact retains the actual bounds and sample counts, including unresolved small changes; a three-pair screen alone is not called confirmation. In particular, the 31-pair LambdaJS List estimate is 1.0345, with a 95% two-sided interval of 1.0189–1.0446. This is an unresolved small peer slowdown, not proof that its regression is below 3%.

All peer point estimates above 3% are listed explicitly. A single pair supplies a census observation and cannot establish or exclude a regression.

| LambdaJS row | Pairs | Candidate/control | Two-sided 95% interval |
|---|---:|---:|---|
| awfy/list | 31 | 1.0345 | 1.0189–1.0446 |
| text/jq_mix | 1 | 1.0988 | one-pair census; no useful interval |

The executed profiles identify the removed work independently of timing:

| Untyped workload | Generic index calls, control → candidate | Generic add calls | Root reloads |
|---|---:|---:|---:|
| Levenshtein | 1,230,274 → 4 | 1,230,270 → 0 | 22,560,880 → 2,054,608 |
| Fast diff | 17,171,200 → 3,072 | 34,853,888 → 0 | 529,749,036 → 29,484 |
| Text search | 162,653,725 → 3,283,291 | 109,868,763 → 36,864 | 1,675,060,412 → 10,399,911 |

#### Phase acceptance and limits

- Phase 1: large unchanged-source Levenshtein and text-search improvements, with the specific generic index/arithmetic reductions above. Fast diff benefits from the same shared paths without a benchmark recognizer.
- Phase 2: the earlier Brainfuck predicate improvement is already present in this fresh control (§7). An incremental ratio near 1 is expected.
- Phase 3: existing string fusion improves JSON execution and copying. **The ≥90% generic-join reduction target is not met by the final profitability policy:** `fn_join` falls 360,004 → 140,004 (61.1%); copied bytes fall 7,559,070 → 6,615,320 and growth copies 360,124 → 220,124. The outer builder already performs the same 19,880 in-place appends. Bounded return/admission facts still leave some forwarded calls boxed. Do not substitute earlier rejected-candidate counts for these final counts.
- Phase 4: bounded recursion improves fib/fibfp; deeper calls, tail calls, overflow and invalid-shift behavior remain covered. SHA-1 improves more modestly.
- Phase 5: the imported Pi fast inner loop has 12 static instructions versus the control's 54-instruction enclosing span including cold arms. This is not an executed-instruction count. The retained finite proof removes sentinel/conversion work without floating reassociation.
- Phase 6: record reads improve, but **the ≥15% record-pilot target is not met**. Splay's 951,168 map copies / 62,777,088 bytes and Puzzle's 174,909 numeric-array copies / 15,508,598 bytes remain. Splay's largest sites are `splay_node` (511,476 copies), `rotate_left` (204,491) and `rotate_right` (183,209). Required snapshots/retained owners lack a valid elision proof (S9.1.2–S9.1.3); no required roots or copies were suppressed (D5.3.3).

Thus all scoped compiler mechanisms have implementations and full-population evidence; some numerical goals and remaining JS advantages are still open. This is not a claim that every proposed speedup target was reached.

#### Current results for all 71 workloads

Times are workload milliseconds. U/C and T/C compare each Lambda lane with its fresh frozen control. Smaller is better. U/JS is observational and retains the port-equivalence limits above. The artifact carries pair counts and confidence bounds for every row.

| Workload | U ms | T ms | LambdaJS ms | Node ms | U/C | T/C | U/JS |
|---|---:|---:|---:|---:|---:|---:|---:|
| r7rs/fib | 1.307 | 1.302 | 0.854 | 1.748 | 0.8427 | 0.8368 | 1.530 |
| r7rs/fibfp | 1.260 | 0.764 | 0.872 | 1.740 | 0.5436 | 0.5941 | 1.444 |
| r7rs/tak | 0.142 | 0.141 | 0.093 | 0.778 | 1.0071 | 0.9930 | 1.534 |
| r7rs/cpstak | 0.282 | 0.284 | 0.174 | 0.965 | 0.9965 | 1.0143 | 1.621 |
| r7rs/sum | 0.266 | 0.266 | 0.545 | 1.163 | 1.0000 | 1.0000 | 0.488 |
| r7rs/sumfp | 0.068 | 0.070 | 0.054 | 1.023 | 1.0000 | 1.0145 | 1.259 |
| r7rs/nqueens | 1.092 | 1.040 | 0.884 | 1.801 | 1.0177 | 0.9952 | 1.235 |
| r7rs/fft | 0.308 | 0.076 | 0.154 | 1.541 | 1.0132 | 1.0000 | 2.003 |
| r7rs/mbrot | 0.713 | 0.568 | 1.425 | 1.871 | 0.9972 | 0.9982 | 0.500 |
| r7rs/ack | 13.478 | 9.877 | 7.534 | 13.272 | 1.0028 | 1.0012 | 1.789 |
| awfy/sieve | 0.027 | 0.025 | 0.084 | 0.363 | 1.0000 | 1.0000 | 0.323 |
| awfy/permute | 0.263 | 0.064 | 1.016 | 0.776 | 0.9813 | 1.0000 | 0.259 |
| awfy/queens | 0.174 | 0.058 | 0.479 | 0.626 | 1.0058 | 1.0000 | 0.363 |
| awfy/towers | 0.466 | 0.161 | 2.097 | 1.102 | 0.9979 | 0.7667 | 0.222 |
| awfy/bounce | 0.064 | 0.054 | 0.671 | 0.533 | 0.9846 | 1.0000 | 0.095 |
| awfy/list | 0.813 | 0.116 | 0.497 | 0.498 | 0.9951 | 0.9831 | 1.636 |
| awfy/storage | 0.573 | 0.371 | 0.684 | 0.636 | 0.9828 | 1.0082 | 0.838 |
| awfy/mandelbrot | 38.646 | 37.619 | 33.664 | 30.574 | 1.0147 | 0.9949 | 1.148 |
| awfy/nbody | 7.069 | 2.628 | 124.880 | 5.410 | 1.0026 | 1.0135 | 0.057 |
| awfy/richards | 306.874 | 77.253 | 222.144 | 46.661 | 1.0183 | 1.0049 | 1.381 |
| awfy/json | 5.592 | 1.988 | 9.613 | 2.599 | 1.0045 | 0.9995 | 0.582 |
| awfy/deltablue | 80.450 | 19.592 | 96.059 | 11.663 | 1.0005 | 0.9900 | 0.838 |
| awfy/havlak | 1712.240 | 597.677 | 2491.445 | 90.403 | 1.0183 | 0.9913 | 0.687 |
| awfy/cd | 713.773 | 176.972 | 402.648 | 35.661 | 1.0095 | 1.0019 | 1.773 |
| beng/binarytrees | 5.077 | 2.576 | 3.879 | 3.662 | 1.0083 | 1.0186 | 1.309 |
| beng/fannkuch | 0.280 | 0.290 | 43.189 | 3.803 | 1.0182 | 0.9966 | 0.006 |
| beng/fasta | 0.470 | 0.415 | 0.901 | 5.885 | 1.0064 | 1.0097 | 0.522 |
| beng/knucleotide | 0.841 | 0.440 | 3.344 | 4.660 | 0.8582 | 0.9910 | 0.251 |
| beng/pidigits | 0.379 | 0.375 | 0.340 | 1.792 | 1.0134 | 1.0274 | 1.115 |
| beng/regexredux | 0.735 | 0.729 | 2.958 | 2.301 | 1.0014 | 0.9733 | 0.248 |
| beng/revcomp | 0.558 | 0.456 | 3.970 | 3.214 | 1.0109 | 1.0000 | 0.141 |
| beng/spectralnorm | 1.660 | 0.787 | 235.652 | 2.518 | 1.0006 | 1.0051 | 0.007 |
| kostya/brainfuck | 157.232 | 106.949 | 43.085 | 32.015 | 0.9972 | 0.9971 | 3.649 |
| kostya/matmul | 4.838 | 4.756 | 7.056 | 14.811 | 1.0202 | 0.9933 | 0.686 |
| kostya/primes | 2.251 | 2.201 | 2.724 | 4.277 | 0.9838 | 0.9968 | 0.826 |
| kostya/base64 | 12.659 | 5.187 | 10.029 | 17.223 | 1.0002 | 1.0031 | 1.262 |
| kostya/levenshtein | 5.079 | 2.886 | 1.569 | 3.938 | 0.2263 | 1.0141 | 3.237 |
| kostya/json_gen | 14.177 | 6.886 | 4.362 | 5.806 | 0.6092 | 1.0051 | 3.250 |
| kostya/collatz | 291.301 | 278.180 | 229.495 | 1388.977 | 0.9955 | 1.0110 | 1.269 |
| larceny/triangl | 169.381 | 150.955 | 140.003 | 68.635 | 0.9980 | 1.0004 | 1.210 |
| larceny/array1 | 0.804 | 0.805 | 0.543 | 1.762 | 1.0000 | 1.0000 | 1.480 |
| larceny/deriv | 20.994 | 5.985 | 14.619 | 3.650 | 0.9640 | 1.0117 | 1.436 |
| larceny/diviter | 266.038 | 265.697 | 539.506 | 441.274 | 1.0003 | 0.9958 | 0.493 |
| larceny/divrec | 1.212 | 1.207 | 4.756 | 7.538 | 1.0050 | 1.0000 | 0.255 |
| larceny/gcbench | 119.712 | 77.321 | 92.712 | 22.785 | 1.0120 | 1.0051 | 1.291 |
| larceny/paraffins | 0.191 | 0.160 | 0.101 | 0.994 | 0.9745 | 1.0191 | 1.893 |
| larceny/pnpoly | 2.308 | 3.651 | 10.641 | 5.773 | 0.9948 | 0.9997 | 0.217 |
| larceny/puzzle | 13.806 | 14.498 | 2.369 | 3.207 | 1.0015 | 1.0019 | 5.828 |
| larceny/quicksort | 1.015 | 0.605 | 1.231 | 1.618 | 1.0120 | 0.9886 | 0.825 |
| larceny/ray | 0.214 | 0.215 | 0.225 | 3.544 | 0.9862 | 0.9773 | 0.951 |
| julia/parse_integers | 37.629 | 34.183 | 45.505 | 12.950 | 0.9769 | 1.0116 | 0.827 |
| julia/matrix_statistics | 16.555 | 15.706 | 66.798 | 20.533 | 1.0214 | 0.9808 | 0.248 |
| julia/iteration_pi_sum | 2.894 | 2.891 | 3.250 | 4.592 | 0.4248 | 0.4240 | 0.890 |
| julia/formatted_output | 76.225 | 61.774 | 99.194 | 35.977 | 0.9938 | 0.9976 | 0.768 |
| text/fast_diff | 48.945 | 43.131 | 174.812 | 38.789 | 0.0672 | 1.0054 | 0.280 |
| text/microdiff | 36.735 | 38.752 | 117.823 | 15.963 | 0.9956 | 0.9925 | 0.312 |
| text/hyphen | 47.987 | 9.438 | 51.522 | 6.508 | 0.9944 | 1.0070 | 0.931 |
| text/prettier_ast | 843.215 | 363.087 | 563.849 | 98.971 | 1.0157 | 1.0008 | 1.495 |
| text/text_search | 1443.980 | 1121.990 | 4592.915 | 771.576 | 0.2182 | 0.8191 | 0.314 |
| text/three_way_merge | 2109.120 | 1489.360 | 1598.470 | 961.788 | 1.0019 | 0.9935 | 1.319 |
| text/log_pipeline | 4297.020 | 1053.430 | 4509.844 | 944.257 | 1.0056 | 1.0010 | 0.953 |
| text/jq_mix | 45496.400 | 44404.400 | 280800.245 | 3929.610 | 1.0150 | 0.9998 | 0.162 |
| text/jq_records | 356.021 | 410.957 | 54353.987 | 2002.355 | 0.9931 | 1.0114 | 0.007 |
| text/jq_bf | 186.517 | 143.948 | 252588.569 | 2959.902 | 0.9935 | 1.0060 | 0.001 |
| text/jq_tree | 1997.720 | 1623.890 | 404427.090 | 5773.777 | 1.0079 | 0.9939 | 0.005 |
| jetstream/cube3d | 9.480 | 7.150 | 69.577 | 17.250 | 0.9952 | 1.0052 | 0.136 |
| jetstream/navier_stokes | 76.396 | 68.298 | 28.984 | 13.805 | 1.0044 | 0.9974 | 2.636 |
| jetstream/splay | 261.382 | 276.653 | 54.798 | 18.382 | 0.9441 | 0.9988 | 4.770 |
| jetstream/hashmap | 73.428 | 27.449 | 355.824 | 15.162 | 1.0262 | 1.0147 | 0.206 |
| jetstream/crypto_sha1 | 46.754 | 26.269 | 10.865 | 8.577 | 0.9662 | 0.9384 | 4.303 |
| jetstream/raytrace3d | 79.141 | 13.651 | 124.526 | 18.130 | 1.0050 | 0.9861 | 0.636 |

### 8.7 Disposition of all 33 historical LambdaJS advantages

Every historical JS-winning untyped row has a fresh executed profile and a source audit. The following notes explain what the retained mechanisms change and what their proofs do not cover. Helper counts are whole-process counts, including warmup where present; they are not CPU percentages. An unresolved native-code gap remains labeled unresolved. Full per-side counters and source identities are in the artifact's `source_audit` and `diagnostics` sections.

| Historical row | Current U/JS | Disposition |
|---|---:|---|
| r7rs/fib | 1.530 | Bounded recursive expansion halves raw calls; remaining int53 poison/overflow and real deeper calls are preserved (S4.1.2). |
| r7rs/fibfp | 1.444 | Bounded expansion halves raw calls and it2d conversions; floating evaluation order is unchanged. |
| r7rs/tak | 1.534 | Already raw scalar recursion with no root reloads; the existing tail-call path is retained. Residual JS gap needs native-code attribution. |
| r7rs/cpstak | 1.621 | Both current ports run direct Tak twice; neither executes CPS closures. Raw recursion and the existing tail-call path remain. The old Lambda comment is not a diagnosis. |
| r7rs/sumfp | 1.259 | Both sources descend with a floating counter. No hot generic helpers or root reloads were measured; an integer-counter explanation would be incorrect. Residual gap is unresolved. |
| r7rs/nqueens | 1.235 | Lambda mutates a plain placed parameter under snapshot semantics; JS uses shared Int32Array storage. The profile retains allocation and mutation work. Numeric-call tuning does not prove those snapshots redundant (S9.1.2). |
| r7rs/fft | 2.003 | Lambda takes a plain data parameter and uses open numeric operations; JS uses Float64Array and integer bit shifts. Generic index/arithmetic helpers remain. The zero-input result is not proof of caller write-back equivalence. |
| r7rs/ack | 1.789 | Already native scalar recursion, with no hot helpers or roots. Real recursive edges and integer semantics remain; scalar expansion is not unrestricted. |
| awfy/list | 1.636 | Linked-list recursion retains 38,216 generic member reads and nullable records. Member-assignment candidates do not prove recursive constructor-result shapes. JS uses reference-linked class instances. |
| awfy/mandelbrot | 1.148 | The Lambda spelling retains 250,000 generic adds in packed-byte construction. The scalar loop policy does not infer a complete range for a repeatedly shifted byte accumulator. JS uses machine-word bitwise arithmetic. |
| awfy/richards | 1.381 | Lambda uses a COW-safe handle store indexed through one var root, whereas the JS scheduler uses reference-linked tasks/packets. 8,656,300 generic index calls remain; source representation is not matched. |
| awfy/cd | 1.773 | Guarded record reads remove the remaining generic member helpers, but 16.7 million index calls and 15.1 million equality calls remain. Their CPU shares are not established by counts; no claim of a complete record-workload speedup. |
| beng/binarytrees | 1.309 | Both ports allocate/check trees, but the boxed recursive tree boundary, allocation and descriptor reads remain. Scalar-only inlining excludes container results. Value semantics alone does not prove this read-only workload needs copies. |
| beng/pidigits | 1.115 | The Lambda port uses arbitrary-precision decimal n values and quotient via remainder/subtraction; JS uses BigInt and division with sign adjustment. Do not narrow this to int53 merely to match JS. |
| kostya/brainfuck | 3.649 | Phase 2 is already in this fresh control. JS also uses a byte tape, mask, else-if dispatch and joined output; Lambda retains integer tape, remainder, independent ifs and string output. No incremental predicate win is expected here. |
| kostya/base64 | 1.262 | Raw string length/entry admission removes one boxed boundary per iteration, while 1,333,500 string concatenations and 666,600 index calls remain. JS uses Uint8Array and fragments/join; ports are not representation matched. |
| kostya/levenshtein | 3.237 | Native producer/length/array/scalar paths eliminate most generic indexing/arithmetic and roots. min3 still takes possibly absent arguments through its boxed entry; no partial tuple is asserted fully admitted (D8.3.3). JS also uses typed arrays and a hoisted character read. |
| kostya/json_gen | 3.250 | Forwarded string facts enable existing join fusion, but bounded admission leaves some boxed calls/joins. Both already use the owned outer builder. Floor versus Math.trunc for negative coordinates also prevents a full-content JS parity claim from the length oracle alone. |
| kostya/collatz | 1.269 | 999,999 generic comparisons remain. Lambda shr(n,1) and JS division by two have different intermediate machine-word semantics; the full dynamic trajectory has no established finite range proof. |
| larceny/triangl | 1.210 | Both run backtracking, with Lambda bool/int arrays versus JS Uint8Array/Int32Array and different break spelling. Already-native loops have no hot generic helper traffic; remaining indexed/induction guards need their own proofs. |
| larceny/array1 | 1.480 | Already-native array loops; JS uses Int32Array. Helper elimination cannot explain this residual gap. No numeric-conversion consumer exists to justify the additional scalar-copy policy. |
| larceny/deriv | 1.436 | Heterogeneous expression-tree tags/fields retain 865,000 generic member reads, allocation and boxed recursion. A single guarded candidate is not a general recursive sum-type specialization. |
| larceny/gcbench | 1.291 | Boxed container-result recursion and allocation remain, including 3,222,190 map allocations. Descriptor reads improve independently; this is not evidence that all copies or allocation work is redundant. |
| larceny/paraffins | 1.893 | Scalar multiset helpers inline, while nullable indexed operands still need checked admission. JS uses Int32Array and else-if partitioning; Lambda uses independent conditions and int53 arithmetic. |
| larceny/puzzle | 5.828 | 174,909 numeric-array copies preserve mutated plain-parameter snapshots. JS uses shared arrays and undo. Removing the copies would change the unchanged Lambda source contract (S9.1.2-S9.1.3). |
| julia/iteration_pi_sum | 0.890 | Imported hot-function assembly proves the finite affine induction/conversion reduction. Floating addition/division and evaluation order are unchanged. Both Lambda columns use the same source. |
| text/fast_diff | 0.280 | Native length/array/scalar flow removes the generic LCS inner-loop operations. The untyped Lambda port still uses LCS and the JS port uses Myers/bisection; the timed typed port includes preprocessing. A remaining cross-language ratio is not an isolated compiler comparison. |
| text/prettier_ast | 1.495 | Record constructors keep boxed string forwarding when it would only add conversions. Recursive mixed AST nodes retain 26.6 million generic member reads and boxed calls. Final diagnostics match control counts; the rejected admission policy is excluded. |
| text/text_search | 0.314 | Producer/nullable-branch/call facts remove most Boyer-Moore generic arithmetic and root reloads. Remaining 3,283,291 generic reads preserve paths without complete bounds/shape proofs. |
| text/three_way_merge | 1.319 | 33 million checked lengths, push/COW and generic index paths remain on collection-producing helpers. String-valued scalar forwarding does not certify these string-array layouts or eliminate observable copies. |
| jetstream/navier_stokes | 2.636 | The hot loops were already native; Lambda content swaps differ from JS reference swaps and the JS port combines a solver pass. The retained 1.47 million floor/conversion calls are not eliminated by an integer-to-double proof. |
| jetstream/splay | 4.770 | All 2,622,536 generic member calls become live-descriptor guarded reads. The 951,168 value-tree map copies remain, especially splay_node and rotations with retained local owners. Source rebuilds differ from JS mutable links (S9.1.2-S9.1.3). |
| jetstream/crypto_sha1 | 4.303 | Bounded rol and scalar expansion reduce calls/roots; nullable array inputs and fallible machine-word operations retain ordinary helpers. Explicit wrap/shift and error semantics are preserved (S4.1.5). |

<!-- tune33-final-results:end -->
