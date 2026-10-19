# Untyped Lambda tuning informed by JS MVP

**Date:** 2026-10-20
**Status:** P1-P3 implemented and qualified; three target execution gains
accepted, with a small confirmed `microdiff` regression recorded below.
**Scope:** `julia/parse_integers`, `beng/fasta`, and `julia/formatted_output`.

**Authority:** **S1.11** preserves each language's semantics; **S4.4.1** and
**S4.5.3** govern integer arithmetic; **S7.8.1**, **D2.8.1–D2.8.3** preserve
errors outside native lanes. Representation and ownership follow
**D2.2.2**, **D2.4.1–D2.4.3**, **D3.3.3v3**, **D5.2–D5.3**.
Use the existing indexed compiler and exact specialization boundaries
(**D8.2.4v2–D8.2.6v2**, **D8.3.1v2–D8.3.4v4**).
This plan changes no ruling. Sections 1-5 record the pre-implementation control
and plan; sections 6-7 record the implementation and final measurements.

## 1. Findings

The main opportunity is preserving facts through conditional initializers,
string producers, and successful numeric reads. Lambda already has parameter
inference, owned string buffers, fused ordinal reads, and guarded array paths.
Those mechanisms lose opportunities when the upstream carrier remains `any`
or an error-capable join. A new string builder or wholesale replacement of the
untyped compiler is unnecessary for this milestone.

A fresh screen with the qualified release reproduces the earlier gaps:

| Workload | Untyped Lambda ms | JS MVP ms | Lambda / MVP |
|---|---:|---:|---:|
| `julia/parse_integers` | 140.989 | 46.057 | 3.06× |
| `beng/fasta` | 1.266 | 0.835 | 1.52× |
| `julia/formatted_output` | 163.523 | 100.511 | 1.63× |

These are medians from three measured rounds per lane, with an untimed
preflight and three identical Lambda lanes. Startup and initial compilation are
excluded from execution time; process time is retained separately. All **51
process outputs** pass their full oracles. These are ordinary-host screening
measurements, not quiet-host performance acceptance.

The two Julia ports preserve their scalar decimal loops and complete warmup.
Formatted output retains **100,000 lines, 1,177,795 bytes, and 391 synchronous
open/write/close batches**. FASTA retains `N=1000`, 10,000 output bases, all
headers and line breaks. Its ports have different implementations: Lambda
copies repeated ALU chunks and uses parallel probability arrays plus a borrowed
seed array; JS appends repeated ALU characters, uses pair tables and a global
seed. Cross-language timings therefore include port/representation differences.

## 2. Measured headroom

Temporary source annotations expose facts already true for these inputs. They
retain the algorithms, operation order, iteration counts and output. Seven
rotating measured rounds plus one preflight per variant produce **120/120
successful output checks** across 15 workload/variant combinations.

**These are source-only diagnostics, not implemented untyped-engine gains.**
Declared contracts can reject values that untyped code accepts. An engine
implementation must prove the facts or retain a guarded boxed fallback.

| Workload | Unchanged source ms | Combined probe ms | Probe speedup | 95% paired ratio interval |
|---|---:|---:|---:|---:|
| `julia/parse_integers` | 141.409 | 48.096 | 2.94× | 0.338–0.346 |
| `beng/fasta` | 1.269 | 0.391 | 3.25× | 0.301–0.321 |
| `julia/formatted_output` | 162.913 | 89.536 | 1.82× | 0.543–0.559 |

The MVP column in §1 comes from a separate screen; it is not part of these
paired intervals. FASTA is particularly short, so fresh longer confirmation is
required before making a production performance claim.

### 2.1 Julia probe breakdown

| Probe | Facts supplied by annotations | Parsing ms | Formatted output ms |
|---|---|---:|---:|
| Control | Original untyped source | 141.409 | 162.913 |
| Numeric joins | `remaining`, parsing `index`, and generated `value` are `int` | 95.151 | 123.950 |
| String local | Decimal formatter's `text` is `string` | 115.508 | 131.307 |
| String boundary | Formatter returns `string`; parser receives `string` | 138.965 | 156.463 |
| Combined | All the above | 48.096 | 89.536 |

The parsing `index` annotation is in `decimal_value`; it is not the already
native outer loop counter. Numeric joins alone remove much of the generic
digit arithmetic. String-local and boundary facts work together: the producer
can use owned append, and the consumer can use the existing string indexing
and ordinal path. Boundary annotations alone recover little of the gap.

### 2.2 FASTA probe breakdown

| Probe | Facts supplied by annotations | Execution ms |
|---|---|---:|
| Control | Original untyped source | 1.269 |
| Numeric locals | `num_chars: int`, `r: float` | 0.869 |
| Array result | `cum: float[]` | 1.124 |
| Combined | All three local facts | 0.391 |
| Existing typed port | Canonical `fasta2.ls`, a separate comparison | 0.721 |

The existing typed port is not a lower bound: different declared contracts can
introduce different admission and mutation costs. The combined probe supplies
only three local facts; its result motivates recovering successful-read facts
in the untyped compiler, rather than adding annotations to the benchmark.

## 3. Causes and JS MVP references

### 3.1 Conditional and return facts stop too early

In [`micro_common.ls`](../../test/benchmark/julia/micro_common.ls), the integer
parameter of `decimal_text` is already inferred. Nevertheless,
`remaining = if (negative) -value else value` is boxed. The loop then calls
`fn_idiv`, `fn_ge`, `fn_add`, `fn_mod`, `is_truthy` and `fn_join` on ordinary
digit work. The corresponding string conditional also loses its useful carrier.

In [`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp),
`mir_expr_carrier_type`, `transpile_if`, declaration/widening analysis, and
`mir_expr_proves_native_return_lane` must agree about one producer's carrier.
`transpile_if` currently boxes when its expression carrier is `any` even if the
emission-time branch carriers agree. `infer_proc_native_return_lane` tests only
integer, float and boolean candidates; it supplies no inferred string result
lane. String-success information still needs an honest error channel.

JS MVP's [`kind`](../../lambda/js/mvp-lmd/mvp_lmd_mir.cpp) joins both conditional
arms and consults direct callees' result kinds. Reference that proof structure;
Lambda must retain its own numeric/null/error semantics (**S1.11**).

### 3.2 Existing string-buffer reuse is bypassed

The current [`lambda-eval.cpp`](../../lambda/runtime/lambda-eval.cpp) already
implements owned append in `string_buffer_join`/`fn_strcat`. Generic `fn_join`
does not establish the same exclusive binding ownership. Actual release COW
counters show the difference:

| Complete process, including Julia warmup | Control | Combined probe |
|---|---:|---:|
| Parsing: generic joins | 1,683,969 | 11 |
| Parsing: in-place appends | 0 | 1,308,958 |
| Parsing: copied string bytes | 8,987,174 | 1,909,348 |
| Formatted output: generic joins | 2,555,601 | 11 |
| Formatted output: in-place appends | 193,752 | 1,349,376 |
| Formatted output: copied string bytes | 16,946,028 | 10,185,360 |

All **six counter captures** pass their output oracles. Counters are diagnostic
and excluded from timed comparisons. Some generic calls remain in emitted
fallback MIR; low runtime counts do not mean every fallback was removed.

FASTA has identical string counters before/after the three annotations. Its
measured gain is therefore not evidence for a string-buffer optimization.

### 3.3 Character and array facts can use existing kernels

`decimal_value` uses generic `fn_index` followed by `fn_ord` when its string
fact is unavailable. Lambda already has `fn_string_ord_at`, guarded ASCII
byte loads, and `mir_ascii_scan_plan`; recovering the string fact lets those
mechanisms participate. Formatted output's `line` still traverses a generic
iterator/index/ordinal path in the combined diagnostic, leaving further work.

There is also a small shared-runtime opportunity: `fn_chr` currently allocates
even for ASCII. JS MVP's character path already reuses
`get_ascii_char_string`, which Lambda's string indexing also uses. Each measured
parsing workload constructs **841,979 decimal digit characters**, and formatted
output constructs **977,795**. Reusing the existing immutable ASCII strings
could remove those allocations. Its incremental speedup has not been measured.

In [`fasta.ls`](../../test/benchmark/beng/fasta.ls), `len(chars)` retains a
checked Item join, `make_cumulative`'s returned array has no usable local float
element witness, and nullable `float(seed_arr[0])` takes generic conversion and
division paths. Probability search then uses generic comparisons/truth tests.
The combined probe eliminates the `fn_lt`/`is_truthy` calls from
`random_fasta`'s generated body, although generic conversion/division fallbacks
remain. JS MVP separately tracks numeric element kind and presence; Lambda has
related machinery in `mir_guarded_array_num_witness`,
`mir_expr_may_be_null`, and `mir_local_tree_collect` to extend.

## 4. Implementation sequence

### P1 — Preserve homogeneous conditional and binding facts

Extend the existing carrier/proof pipeline to retain equal scalar branch
carriers when both arms are present and the condition cannot deliver an error.
Use the same proof in the initializer, emission and whole-binding widening
decision. Track every assignment, capture and borrowed write; mixed/null/error
joins retain their full descriptors. Reuse the shared binding-use index and
`MirValue` contract (**D8.2.4v2–D8.2.6v2**), rather than introducing another
parallel type oracle.

**Exit:** unchanged decimal loops retain integer locals and valid string-owner
facts. Validate error conditions, absent `else`, mixed arms, overflow/poison
values, captured/reassigned locals and mutable borrows. A valid numeric example
must still have its complete boxed behavior for mismatching inputs.

### P2 — Recover string producer and consumer facts

Extend bounded procedure result summaries with proven string success and its
error possibility. Propagate that information through the existing exact-call
and boxed-call boundaries. Prove that a concrete numeric `chr` operand cannot
be an error; retain all dynamic-operand and Unicode behavior. Connect qualifying
locals to the existing owned-concat/freeze logic, and qualifying ordinal reads
to the existing fused string path. Extend the existing ASCII scan/for-loop
machinery where necessary instead of adding a benchmark-specific loop matcher.

Independently reuse `get_ascii_char_string` for valid ASCII in `fn_chr`; keep
the original invalid/non-ASCII path. Measure this small runtime change separately
from inference improvements so its contribution remains attributable.

**Exit:** no per-digit generic join on the proven formatter path; formatted
output retains every line/digest operation and all 391 synchronous writes.
Validate aliases and snapshots, Unicode and absence, error propagation, and
forced-GC/poison behavior. Root every live string across allocating calls
(**D5.2–D5.3**); ownership cannot be inferred from a type alone.

### P3 — Preserve guarded numeric result and presence facts

Extend the existing homogeneous array producer/result summary for `fill` plus
homogeneous stores. Retain the element witness on `cum` only while its binding
and mutation proof remain valid (**D3.3.3v3**). A successful result can be guarded
without pretending that a possible error result is a native array.

Reuse exact-array length lowering and local guarded numeric reads for the
conversion/comparison chain. Separate the successful float payload from absence
and error. Prove or guard `seed_arr[0]` presence, while retaining its borrowed
write-back and COW semantics (**S9.1.3**). Revalidate/reload array storage after
any slow helper that can detach or replace it.

**Exit:** probability search uses native comparisons on the proved path; full
FASTA output remains byte-identical. Validate empty/short arrays, wrong element
types, aliased arrays, borrowed mutations, nullable reads and invalid indices.

## 5. Qualification and remaining work

Implement one phase at a time with focused behavioral and MIR evidence, then
freeze a release candidate and compare it against the original control and an
identical-control peer. Use at least 15 rotating pairs on the three unchanged
canonical sources. Treat the annotation-probe medians as planning evidence,
not promised implementation targets. A phase needs a gain whose paired upper
bound is below 1 against both control lanes; report process/compilation cost
and any regressions separately.

At the milestone boundary run `make test-lambda-baseline` and
`make test262-baseline`, plus focused JIT/interpreter parity and forced-GC checks
for the changed paths. Preserve the configured Test262 exclusions and report
failures, timeouts and retries explicitly. Run the shared benchmark regression
population appropriate to the final changed paths; a targeted gain does not
establish general no-regression. The runtime implementation requires the engine gates above.

Evidence lives under [`temp/lambda_untyped_tune_20261019/`](../../temp/lambda_untyped_tune_20261019/):
`provenance.json`, `baseline/comparison.json`, `probes/results.json`, the frozen
source variants and outputs, `mir_summary.json`, `fasta_mir_summary.json`, and
`counters/`. `run.py` and `probes.py` reuse the repository's existing benchmark
preparation, oracle and paired-bootstrap helpers. The baseline executable is
SHA-256 `63fe391be4de54417ba6bda1eb162fe0fd22e3283370c1a869035ca6d17ad518`.
The diagnostic variants in sections 1-2 use that same baseline release. Canonical
benchmark sources and historical result tables remain unchanged.


## 6. Implementation record

The implementation changes `lambda/runtime/transpile-mir.cpp`,
`lambda/runtime/lambda-eval.cpp` and `lambda/runtime/lambda-mem.cpp`; it does not
annotate the benchmark sources.

- **P1:** the carrier oracle returns equal scalar conditional-arm carriers after
  checking both arms and the condition error channel. Newly recovered kinds
  require positive presence evidence through local aliases, not just an absent
  optional annotation. Native return proof and call-shape hints follow the same
  branch structure. The existing whole-body write scan also preserves presence
  for conditional-initialized mutable scalars; captures, unknown producers and
  borrowed replacements retain boxed behavior (**D3.3.3v3**, **D8.3.1v2-D8.3.4v4**).
- **P2:** proven integer `chr` operands keep the string lane; string procedure
  results and bounded local-producer hints reach the existing exact-call
  boundary. String-lane joins publish their nullable string contract even when the
  runtime helper physically returns an Item (**D2.4.1-D2.4.3**). Fused ASCII
  literal comparisons share their matcher with the condition/error oracle.
  `fn_chr` reuses the existing immutable ASCII character cache, whose storage now
  aligns both the base and every entry to the `String` ABI. Existing owned
  append and ordinal kernels supply the actual string operations.
- **P3:** wholly unwritten local vars can retain initializer success facts,
  including system-function registry contracts. A float-array producer hint
  selects a guarded indexed comparison: rank/type/storage, scalar float kind,
  bounds and absence must all match. Every miss uses the original boxed
  comparison, including errors, nulls, other element kinds and rebindings
  (**S7.1.1v3**, **D2.8.1-D2.8.3**, **D3.3.3v3**).

Focused regressions are `test/mir/lambda/untyped_scalar_flow.*` and
`untyped_numeric_read_flow.*`. They cover homogeneous/mixed branches, borrowed
replacement, string snapshots, character boundaries, imported recursive string
helpers, missing reads, nullable/retagged arrays, infinities, NaN and error
operands. MIR assertions require the optimized arithmetic/comparison and retain
its boxed fallback where appropriate.

Qualification caught an untyped borrowed var incorrectly certifying its
initializer's return lane, string-lane joins retaining their old `any` descriptor
in imported music/UI expressions, an unaligned ASCII cache stride, and
conditional kind recovery erasing absence through direct or aliased reads.
The assignment regression also reproduced an existing control-JIT defect: a
missing float read written after a homogeneous conditional became NaN. The
declaration-time presence check and producer-owned Item conversion fix this
case too: assignment boxing retains the full nullable `MirValue`, instead of
rebuilding it from the compact TypeId (**D2.4.1-D2.4.3**). The proof, producer and
cache storage now own those fixes. The string-join descriptor retains null, and error-capable joins remain
boxed. Conditional result homogeneity does not prove presence: potentially
absent scalar joins remain Items, including at inferred return boundaries.
This preserves null in integer ordering, float arithmetic and bool logic
(**S7.1.1v3**, **D2.4.1-D2.4.3**). The affected UI, math and music reproductions now match their goldens.
Only the final private release's gates below are acceptance evidence.



## 7. Final release evidence

The private release is `implementation/final.exe`, SHA-256
`08fe7bd34458f2f374a1938c46cd57d50ed90e5c30abcccb914525bd7e95e06b`. `final_provenance.json` records all three runtime source hashes;
matching source snapshots are retained beside it. Benchmark source closures are
frozen and hashed separately by the driver. The canonical sources remain
unchanged, with no numeric annotations added.

### 7.1 Paired performance

Fifteen rotating measured rounds compare the candidate, original control, an
identical-control peer and the control's JS MVP. A Node oracle preflight and
all measured outputs pass: **195/195 checks**. Execution medians exclude startup
and compilation; total process medians retain those costs and Julia warmup.

| Workload | Control ms | Tuned ms | Speedup | JS MVP ms | Tuned/control 95% paired interval |
|---|---:|---:|---:|---:|---:|
| `julia/parse_integers` | 145.303 | 39.184 | 3.71x | 45.067 | 0.268-0.272 |
| `beng/fasta` | 1.240 | 0.408 | 3.04x | 0.884 | 0.323-0.341 |
| `julia/formatted_output` | 165.682 | 77.723 | 2.13x | 101.697 | 0.464-0.473 |

All three execution improvements have paired upper bounds below 1 against both
control lanes. These are fresh A/B measurements; the MVP port differences in
section 1 still apply. The candidate's parsing and formatted-output process
medians improve from **329.34 to 113.88 ms** and **371.55 to 198.03 ms**.
FASTA process latency changes from **19.11 to 19.26 ms** despite its execution
gain: startup/compilation dominates, and the total-process paired ratio is
1.002-1.021 against control. It has an execution gain, not a process-latency gain.
Full samples and both control-lane intervals are in
`implementation/final_pairs15/comparison.json`.

The 28-workload, five-round regression screen passes **700/700 output checks**;
its execution geometric ratio is **0.968**. It is a screening population, not a
proof of general no-regression. Some existing typed benchmark annotations are
retained in this broader population. The short run flags `text/microdiff` at
about 2% slower. A separate 31-round confirmation passes **129/129 output
checks** and confirms a **1.7% execution regression**: control **37.379 ms**,
candidate **38.003 ms**, identical-control peer **37.456 ms**. The 95% paired
ratio intervals are **1.011-1.023** against control and **1.006-1.022** against
peer. Total-process medians are **68.682 to 69.573 ms**. Its normalized finalized
MIR is identical; the cause of this small runtime/binary difference is not
isolated. No workaround is added. Samples are in
`implementation/final_microdiff_pairs31/comparison.json`.
`awfy/queens` crashes in both original control and final candidate; no timing is
accepted for it (`implementation/final_queens_replay.json`).

### 7.2 Correctness qualification

Final focused tier/forced-GC checks pass **12/12** and release MIR emission
checks pass **251/251**. `make test-lambda-baseline JOBS=2 TEST_JOBS=2` passes
**7,893/7,893** (2,112 input and 5,781 Lambda/runtime checks), including
**319/319** forced-GC and **20/20** debug MIR budget checks.
`make test262-baseline JOBS=2 TEST_JOBS=2 VERBOSE=1` passes **40,261/40,261**
with **0 failures, 0 abnormal/incomplete batches, 0 retries**.

The full configured MIR/MVP Test262 gate passes **40,782/40,782** with
**0 failures, 0 timeouts, 0 retries**, **413/413 normal, complete batches**, and
**2,131 existing exclusions**. It uses pinned corpus commit
`673e9bacbe28590f501e2dcd817aadcc31899191`. Configured native assertion helpers
remain enabled: 16,673 native-assert jobs and 24,109 JS-harness jobs, all on MIR
with **0 AST jobs**, including 651 module jobs and two separate slow jobs.
MVP is the default MIR backend (**D8.1.3v26**); `--mir-only` pins every selected
job to it. The frozen private release and runtime sources retain their recorded
hashes; the release rebuilt by the baseline is byte-identical.

The accepted run uses `caffeinate -is`, which holds sleep assertions only for
the validation process. The preceding run is rejected: host idle/maintenance sleep produced a 938.2 s
batch wall interval, triggered the unchanged 120 s no-progress watchdog, and
caused 75 tests to be retried. It eventually passed all tests but fails the
zero-retry gate. Its log and strict summary are retained as
`implementation/test262_full_sleep_interrupted*`; no harness, timeout, test or
exclusion was modified to address this host interruption.

The additional release MIR budget check passes **19/20**. Its sole overage,
`lambda_cow_nested_store` at **169 instructions versus 166**, is already present
in the frozen control and has identical normalized finalized MIR in the final
candidate. The debug profile allows 169. No budget is changed; evidence is
`implementation/final_ratchet_control_comparison.json`.

Reproduce the full configured gate from the repository root with the qualified
release (macOS ARM64 `release_native`):

```bash
LAMBDA_TEST262_EXE="$PWD/temp/lambda_untyped_tune_20261019/implementation/final.exe" \
caffeinate -is ./test/test_js_test262_gtest.exe \
  --mir-only --no-stripped --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt --jobs=2 --verbose \
  --feature-summary --gtest_brief=1
```

`implementation/test262_full_final_summary.json` checks full selection coverage,
normal batch exits and retry counts, rather than accepting the runner's final
pass count after recovery. `qualification_awake_final.log` also records the
post-gate 31-round performance confirmation and source/binary hash verification.
