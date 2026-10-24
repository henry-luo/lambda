# Tune35 — Typed Lambda proof preservation after Result54

**Status:** initial tranche and three-pilot continuation implemented.
Aggregate Lambda and Test262 gates pass; section 9 records the repaired
baseline, paired results, remaining typed/untyped gaps and size-ratchet limit.
**Date:** 2026-10-24. **Inspected HEAD:** `6d3e8e161c8bd70f995d5e4843fd13ecf5e96ec9`.

**Scope:** general improvements to typed Lambda, starting with the five
reproducible typed/untyped execution-time inversions in Result54. Preserve the
canonical benchmark sources and both execution tiers.

**Authority:** S11.4.1v3 (annotations are contracts), S11.4.10 (verification
remains valid while unchanged), D2.4 (producer-owned representations),
D3.3.1v2–D3.3.4 (inference and full contracts), D3.4.5–D3.4.6 (physical
layout), D8.3.1v2–D8.3.4v4 (complete specialization proofs and callee admission),
S9.1.2–S9.1.3 (snapshots and `var` sharing), D5.3.1–D5.3.4 (precise roots).
No semantic ruling change is proposed.

## 1. Evidence and priority

[Result54](../../test/benchmark/Overall_Result54.md) reports 13 of 71 rows with
a lower untyped median in Part 1. Prioritize these five; the other eight
(`fib`, `tak`, `cpstak`, `mandelbrot`, `pidigits`, `array1`, `divrec`, `ray`)
have small gaps and overlapping or touching reported ranges.

All figures below are workload milliseconds with `LAMBDA_EXEC_BACKEND=jit`.
The last column is a fresh seven-pair alternating screen of the original
sources on the archived Result54 release binary, not candidate qualification.

| Benchmark | Result54 untyped | Result54 typed | Typed/untyped | Fresh screen U / T |
|---|---:|---:|---:|---:|
| larceny/pnpoly | 2.29 | 3.68 | 1.607 | 2.175 / 3.405 |
| jetstream/splay | 191.6 | 223.1 | 1.164 | 190.327 / 222.782 |
| text/microdiff | 32.4 | 36.7 | 1.133 | 32.318 / 36.526 |
| text/jq_records | 333.1 | 390.4 | 1.172 | 337.380 / 391.480 |
| beng/fannkuch | 0.276 | 0.291 | 1.054 | 0.279 / 0.289 |

The archived executable is
`test/benchmark/exe/lambda-v54-aea2a23f46`, SHA-256
`b5fc11a5e9a523278861c043500badd2c35e7e8a66da2f9d3f0ff247e1dff4cc`.
The five benchmark pairs and both jq helper modules are unchanged from its
recorded source commit `aea2a23f46ca134cc86fb5f21b7c7d7586d13fc7`.
The inspected compiler's subsequent change concerns view-state initialization.

Evidence retained under [`temp/typed-result54-audit/`](../../temp/typed-result54-audit/):

- `manifest.json`: executable and benchmark source hashes, diagnostic exits and output comparisons.
- `paired-screen.json`: all 70 uninstrumented samples, alternating order, output equality and host load.
- `counter-summary.json`: helper and category counts for both variants.
- `<name>.<variant>.calls.tsv`, `.cow.tsv`: executed helper/ownership counters.
- `<name>.<variant>.mir.mir`: finalized MIR without execution instrumentation.
- `<name>.<variant>.profile.mir`, `.profile.out`, `.profile.err`: separate instrumented captures.

All 20 diagnostic processes and 70 timing processes exited successfully.
The four pairs with `.txt` files matched their goldens; Splay printed
`PASS (nodes=8000)` and matched across variants. Existing oracles are not
exhaustive semantic proof. The screen ran serially under `caffeinate -is`,
with instrumentation removed and `--no-log`. Host load was recorded; no
claim of an otherwise idle machine or statistical acceptance is made.
Profile counts include setup and verification, not only the timed region,
and are not CPU-time percentages.

## 2. Pnpoly: preserve native boolean consumers and successful return lanes

The typed/untyped sources use the same polygon and loops. Both execute the
dense-array fast path 100,000 times; neither executes hot indexing helpers.
The large gap is therefore not explained by typed code failing to use native
array loads.

| Executed event | Untyped | Typed |
|---|---:|---:|
| `is_truthy` | 0 | 100,000 |
| `lambda_stack_is_exhausted` helper | 3 | 100,003 |
| Root stores | 19 | 300,023 |
| Root reloads | 2 | 200,002 |
| `lambda_type_check` | 0 | 2 |

Two generated-code differences are actionable:

1. In the typed dense loop, `yi_gt != yj_gt` boxes each boolean through
   nullable-lane handling before comparing. The untyped loop compares the
   native values directly. `mir_emit_bool_equality` computes both
   `native_lanes` and `boxed_bool_or_null`, but selects boxed operands whenever
   the latter is true, even if the native proof is also available. Prefer the
   proven native representation when its null/error semantics are exact.
   Preserve the boxed fallback for values that need it.
2. Typed `pnpoly` has a value/error pair return. Its caller boxes the boolean,
   merges the error arm into an Item, calls `is_truthy`, and then tests for an
   error again. Consume the success lane directly after checking the error
   lane. Preserve the cold error path: typed local float admissions can fail
   for short arrays, so globally declaring the function non-failing is wrong.

The additional root traffic is associated with the fallible/possibly
allocating call path. A later step can expose a guarded no-allocation kernel
when caller bounds and admission proofs justify it; first remove the local
representation round trips. Root slots may only disappear when effect and
liveness analysis prove that they are unnecessary (D5.3).

**Implementation sites:** `mir_emit_bool_equality`, `em_require_rep`, native
call result/condition lowering, `mir_solve_may_defect`, and the existing dense
array proof machinery in
[`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp).

**Completion gate:** the dense loop has native boolean equality; a successful
boolean call used as a condition does not call `is_truthy`; short-array,
nullable, NaN and error behavior remains source-correct. Measure each step
separately before attempting broader effect specialization.

## 3. Microdiff: retain proofs from container producers and flow tests

Typed Microdiff executes **311,624 `lambda_type_check` calls**, versus zero
untyped. The largest callers are `diff_value` (131,200), `diff_map` (106,600)
and `diff_array` (49,200). Its emitted MIR checks results immediately after
specialized array joins; map-producing constructors also cross generic `map`
return checks. Typed root stores grow from 2,009,263 to 2,786,273.

The typed path already performs useful optimization: generic joins fall from
100,454 to four. It is inappropriate to disable typed lowering wholesale.
Instead, preserve the successful producer's broad `array`/`map` proof through
assignment, return and a subsequent direct call. Reuse successful `is array`
and `is map` facts where they prove a broad contract. Such tests alone do not
prove a particular record layout or a reified `T[]` carrier.

Extend the existing `mir_boundary_success_is_redundant` and producer-result
facts, retaining error checks and physical-representation obligations.
S11.4.10 permits proof reuse while the admitted data is unchanged; it does not
permit ignoring a failed contract or treating all container representations
as interchangeable.

Array copies also rise from **12,300 / 885,600 bytes** to
**16,400 / 1,180,800 bytes**. Attribute the additional 4,100 copies to their
ownership sites before proposing elimination; the present capture does not
prove they are redundant.

**Completion gate:** no repeated broad-container admission immediately after
a producer that already proves that exact successful contract. Confirm the
counter reduction and timing benefit independently. Preserve mixed nested
values, rich leaves, NaNs, errors and snapshot behavior.

## 4. Splay: carry complete recursive contracts across `var` calls

Typed Splay saves 2,328,900 `map_shape_field_to_item` calls, but adds wrapper
and boundary work:

| Executed event | Untyped | Typed |
|---|---:|---:|
| `lambda_type_check` | 12,000 | 274,180 |
| `cow_prepare_write` | 429,942 | 1,081,097 |
| Boxed call events | 1,945,980 | 2,118,311 |
| Map copies | 951,168 | 951,168 |
| Map bytes copied | 62,777,088 | 61,825,920 |

The typed `_splay_node_b` wrapper alone performs 250,180 type checks.
Recursive `splay_node` and rotation edges still enter `_b`; wrappers perform
ownership preparation and then enter the body. This is not evidence of extra
map copying: typed copies are the same count and slightly smaller.

Carry exact `SplayNode?`, float-key and transported `var` home proofs through
recursive field reads, local rebindings and store-back. Use the existing
proved direct edge when the **whole** signature is admitted; preserve the
boxed boundary for unknown callers. Do not introduce partial specialization
or a third unguarded route into a native body (D8.3.1v2–D8.3.4v4).

Then investigate whether the caller's prepared-owner proof can survive the
adapter so a second preparation is avoided. Snapshot creation or aliasing
between preparation and use invalidates that proof. The current 951,168
copies must remain until independent lifetime analysis proves otherwise.

**Implementation sites:** direct call selection and argument-contract facts,
`emit_boxed_abi_wrapper` and `var` home setup in
[`transpile-mir.cpp`](../../lambda/runtime/transpile-mir.cpp); existing
descriptor and COW helpers in
[`lambda-eval.cpp`](../../lambda/runtime/lambda-eval.cpp).

**Completion gate:** fewer proved recursive edges reach `_b`, fewer redundant
admissions/preparations, preserved write-back on every exit, and unchanged
tree results under forced collection and retained snapshot tests.

## 5. Jq_records: apply shared proofs, then isolate source differences

This pair is not an annotation-only experiment. The typed source imports
`jq_query_typed`, uses typed slice helpers and `jq_unique_strings`, and adds
`string(it.sku)`. It performs **120,030 additional `fn_string` calls** in
`transform_order`. Both ports produce the same existing digest, but attributing
the whole gap to the compiler would overstate the evidence.

There is nevertheless substantial shared compiler work:

- `lambda_type_check`: **480,255 → 1,163,581**. Typed `transform_order`
  accounts for 240,045 and `jq_collect_paths` for 94,500.
- Typed open-map stores call `lambda_map_set_checked` **454,185 times**.
  Its runtime implementation already recognizes plain `map` and delegates
  to the ordinary COW setter. Select that existing path during lowering when
  the plain-map contract and receiver are proven; do not add another helper.
- The typed `collect_numbers_b` path prepares its borrowed output **79,515
  times**. Apply the same exact-contract/home work as Splay.
- Both variants make **40,005 map copies / 3,840,480 bytes**. Their difference
  is not explained by additional map-copy volume.

Use the existing annotation-control machinery in
[`run_typed_audit.py`](../../test/benchmark/run_typed_audit.py) as the starting
point for a diagnostic factorial comparison: same typed algorithm with and
without annotations, then helper/conversion changes independently. Include
both imported modules in the controls. Keep all such variants under `temp/`
with expected output files; canonical Result54 sources remain the acceptance
workloads.

**Completion gate:** attribute the helper/conversion portion separately;
demonstrate shared proof and call-path reductions on the unchanged typed row.
Keep generic mixed input and failed string admissions correct.

## 6. Fannkuch and the remaining eight rows

Fannkuch executes essentially no hot generic runtime helpers in either lane.
Typed dense-loop fast entries/attempts are **32,208**, versus **5,040**
untyped; every recorded attempt succeeds. Inspect guard placement and final
native assembly before choosing a change. Reuse dominating bounds, storage
and ownership proofs across nested loops only while they remain valid.
Candidate sites are `mir_emit_dense_loop_guard` and the typed-array write
guard analysis. Native instruction count, register pressure and guard
frequency matter more here than removing a one-time admission helper.

The fresh screen is 0.279 versus 0.289 ms: lower priority than the four larger
rows. Confirm on longer repeated work with unchanged results before accepting
a small code-generation adjustment. Do not expand MIR inlining budgets or
modify vendored MIR based on this screen.

For the remaining eight rows, first run paired confirmation with calibrated
repetition and unchanged work. Treat neutral rows as controls. Their three-run
Result54 medians alone do not establish a tuning defect.

## 7. Delivery and acceptance

Implement in finite batches: (1) Pnpoly boolean consumers; (2) broad-container
producer/admission proofs, with Microdiff first and Jq_records as a companion;
(3) complete recursive call/home proofs with Splay; (4) remaining Jq_records
and Fannkuch work only where attribution warrants it.

For each batch:

1. Keep fixed release control/candidate executables and source/oracle hashes.
   Freeze imported helpers too. Separate instrumented diagnostics from timings.
2. Add focused semantic and MIR checks for the actual proof change: nullable
   and error arms, out-of-bounds reads, shape changes, detached/widened arrays,
   failed typed stores, `var` write-back and GC safepoints as applicable.
3. Screen the unchanged target pairs, then confirm a retained candidate with
   21 alternating pairs. Use 31 only for an unresolved result; use identical
   control/control runs when code/layout or host noise makes attribution unclear.
4. Proposed performance gate: a target's candidate/control confidence interval
   must show an improvement; aim to bring typed within 3% of untyped on the
   matched source rows. This is a goal, not a predicted speedup. Jq_records
   additionally needs the source-difference attribution above.
5. Record compile time, generated MIR/native size, root operations and process
   wall time. A workload gain must not hide an auto-tier startup regression.
6. At a batch boundary, run the Lambda and Test262 baselines plus relevant
   forced-GC/MIR checks. Then screen all 71 typed and untyped rows with
   LambdaJS as a shared-runtime control; follow up material regressions with
   longer pairs. Never accept output mismatches or raise budgets to hide them.

## 8. Implementation and qualification — 2026-10-24

The initial tranche changes only `lambda/runtime/transpile-mir.cpp`:

- `mir_emit_bool_equality` prefers the native bool/null lane when both native
  and boxed comparison proofs apply. Checked plain-bool calls retain a direct
  ItemTrue truth test through `mir_boxed_predicate_result`.
- One broad-container success proof covers array/map literals, joins,
  single-value branches, direct declared returns, stable flow tests and
  admitted declared bindings. It preserves error checks and does not prove a
  refined `T[]` representation, constrained type or record shape.
- A local boxed procedure body may receive a completely proven signature
  directly, including its existing transported `var` home. Unknown arguments,
  unavailable homes, captures, omitted defaults, variadics and binders retain
  their previous routes. No native pointer ABI is enabled for borrowed maps.
- One shared map-store emitter selects the runtime checker's existing plain-map
  COW operation directly; named/refined maps retain checked storage. Both
  computed keys and member notation preserve root replacement and write-back.

Three permanent MIR fixtures include goldens and emission assertions:
`tune35_bool_consumers`, `tune35_container_proofs`, and
`tune35_var_record_edge`. They cover bool/null equality, NaNs, failed scalar
admission, mixed arrays, refined representation admission, open-map snapshots,
recursive nullable-record rebinds and failed dynamic scalar arguments.

The release control is the unmodified inspected HEAD, archived under
`temp/tune35/control`; the pinned candidate is `temp/tune35/candidate`.
`temp/tune35/provenance.json` records binary and emitter source hashes.
Canonical benchmark sources and their goldens are unchanged.

Completed focused gates:

- Release MIR emission: **272/272**.
- Release forced/randomized-GC and MIR-interpreter corpus: **343/343**.
- Release size ratchet: **19/20**. `lambda_cow_nested_store` emits 169
  instructions against budget 166 on both pinned control and candidate.
  The budget is unchanged; logs are `ratchet-final.log` and
  `ratchet-control.log` under `temp/tune35/`.
- The new fixtures pass JIT and auto execution with forced collection and
  poisoning. The two supported fixtures also pass forced T0 execution;
  `tune35_container_proofs` is rejected by both control and candidate under
  forced T0 because computed index assignment is unsupported there.

The intermediate three-pair `second-screen.json` is rejected as acceptance
evidence: a subsequent build overlapped its later samples. Its rejection is
recorded separately. Final performance and aggregate results follow below.

### 8.1 Release performance

`temp/tune35/final-pairs.json` contains 21 alternating pairs plus one warmup
per row on unchanged source, for five typed targets and their untyped controls.
Every measured pair has equal output; all available pilot goldens also match.
All four improvements below have a paired-bootstrap 95% upper ratio below 1.
The unchanged Fannkuch row remains a neutral control.

| Typed workload | Control ms | Candidate ms | Candidate/control | 95% two-sided ratio interval |
|---|---:|---:|---:|---:|
| pnpoly | 3.651 | 2.600 | 0.7121 | 0.7036–0.7194 |
| microdiff | 36.618 | 29.726 | 0.8118 | 0.8090–0.8153 |
| splay | 225.746 | 215.710 | 0.9555 | 0.9506–0.9618 |
| jq_records | 401.191 | 390.927 | 0.9744 | 0.9550–0.9930 |
| fannkuch | 0.291 | 0.291 | 1.0000 | 0.9932–1.0138 |

The corresponding candidate untyped medians are 2.313, 32.371, 192.402,
342.032 and 0.278 ms. Microdiff becomes faster typed than untyped. Pnpoly,
Splay and Jq_records still have typed overhead of approximately 12.4%, 12.1%
and 14.3%; the proposed 3% parity goal is not achieved for those rows.

Separate whole-process diagnostics in `final-counters.json` record:

| Typed workload / event | Control | Candidate |
|---|---:|---:|
| pnpoly / `is_truthy` | 100,000 | 0 |
| microdiff / `lambda_type_check` | 311,624 | 61,500 |
| microdiff / root stores | 2,786,273 | 2,035,901 |
| splay / `lambda_type_check` | 274,180 | 24,000 |
| splay / `cow_prepare_write` | 1,081,097 | 453,942 |
| jq_records / `lambda_type_check` | 1,163,581 | 1,029,031 |

Pnpoly's 300,023 root stores and 200,002 root reloads are unchanged: the cold
fallible path still requires the existing call/root protocol. Native success
and effect separation remains further work. Jq_records retains substantial
dynamic admission and the source-level helper/conversion differences in §5.
Splay still uses boxed record carriers and required value-tree copies.

`compiler-metrics.json` contains three alternating fresh-process compilation
samples and separate exact native-code censuses per target. Compile-time
candidate/control ratios are 0.9964 Pnpoly, 0.9880 Microdiff, 0.9913 Jq_records
and 0.9917 Splay; generated native-byte ratios are respectively 0.9828,
0.9696, 0.9898 and 0.9883. Fannkuch's code size is unchanged. These diagnostic
samples are independent of accepted workload timings.

### 8.2 Full screen, process timing and source controls

The one-pair full screen (`full-screen.json`) covers **142/142** typed/untyped
rows with matching output. Its descriptive candidate/control geomeans are
0.9789 typed and 1.0049 untyped. These single samples are screening evidence.

The typed Gcbench outlier (1.3787 in the screen) is neutral over 21 follow-up
pairs: 0.9993, 95% ratio interval 0.9952–1.0037. Other material single-sample
outliers receive 21-pair confirmation in `untyped-followup.json`; Splay's
original 21 pairs already cover its full-screen outlier. Confirmed untyped
point slowdowns remain: Puzzle **1.0243** (1.0139–1.0362), Cube3D **1.0193**
(1.0148–1.0304), and Jq_bf **1.0072** (1.0058–1.0103). No follow-up has a
95% lower bound above 1.03, but these intervals do not certify every slowdown
below 3%. Do not describe the candidate as having no regressions. Untyped
Prettier improves to 0.9852 (0.9818–0.9906); Fib, Fibfp and Primes are neutral.

Five-pair auto-tier **process-wall** screens in `auto-screen.json` include
startup, input loading and compilation. Read the individual pairs' `wall_ms`,
not the runner's `median_exec_ms` summary, for this measurement:

| Typed workload | Control wall ms | Candidate wall ms |
|---|---:|---:|
| pnpoly | 35.565 | 34.633 |
| microdiff | 68.338 | 61.541 |
| jq_records | 2,619.021 | 2,577.653 |
| splay | 895.582 | 881.549 |

The Jq_records factorial controls live under `temp/tune35/jq-controls/`, with
source hashes and goldens; `jq-factorial.json` records three alternating pairs
per comparison. Erasure uses the existing `run_typed_audit.erase_annotations`
on both the benchmark and its imported typed helper module. At fixed casts,
typed/erased is 1.2023; without the extra SKU cast it is 1.1286. Removing that
cast gives 1.0090 with annotations and 0.9900 without annotations, with equal
digests in every pair. These short diagnostic screens establish no reliable
benefit from deleting the cast and do not quantify its interaction precisely.
They retain the same typed algorithms/helpers and show annotation overhead
remaining; canonical benchmark sources are unchanged.

The production change is confined to Lambda's emitter. The full LambdaJS
performance matrix is not repeated; the standing Test262 behavior gate is
retained as the JS control. Shared runtime, GC and JS lowering are unchanged.

### 8.3 Aggregate gates and remaining limits

`make test262-baseline` passes **40,261/40,261**, with zero regressions and
zero retry, partial, timing, memory or eval rerun time. The complete harness
execution phase takes 60.7s. It ran under `caffeinate -is` with the unchanged
baseline file and harness. `temp/tune35/test262.log` retains the phase report.

`make test-lambda-baseline` reports **8,188/8,195** including input gates;
the Lambda target alone is **6,076/6,083**. All 272 MIR emission checks,
343 GC corpus checks and the debug-profile size ratchet (20/20) pass inside
this aggregate. Seven script cases fail:

- `docx_docx_business_plans`: missing `test/input/docx/biz-plan2.body.txt`.
- Five Word export cases (`tests__columns_export`, `tests__equations_export`,
  `tests__margins_export`, `tests__object_layout_export`,
  `tests__shape_format_export`) define `pn main()` but are discovered as
  functional scripts: the directory is absent from the harness's procedural
  directory list. The harness therefore prints null rather than running main.
  Both pinned binaries reproduce all five failures in the exact harness mode.
  Running the scripts with `run` makes Columns and Margins pass; the other
  three additionally require missing `temp/word/equations.word.json`,
  `temp/word/object-layout.word.json` and `temp/word/shape-format.word.json`.
- `mail_compose`: both binaries emit the same 3,718 bytes, containing 34 CRLF
  sequences, against a 3,684-byte LF-only golden. The difference is exactly
  CRLF versus LF. The initial Python text-mode audit normalized line endings;
  `mail-raw.json` and binary stdout captures retain the decisive byte check.

All seven failures thus predate the emitter change. The focused actual-harness
rerun still fails 7/7; this is not a green aggregate gate. Evidence includes
`baseline-failure-audit.json`, `failure-batch.json`,
`failure-harness-batch.json`, `mail-raw.json`, `lambda-failure-followup.log`
and raw stdout/stderr under `temp/tune35/`. Baseline logs and copied result
JSON are retained there too. No golden, budget, corpus exclusion or test
harness is modified.

Remaining tuning is explicit: Pnpoly's cold effect/root protocol, Splay's
boxed recursive carriers, residual broad admission and array copies in
Microdiff, and Jq_records' dynamic checks. Fannkuch's repeated guards remain
untuned because this tranche's confirmed result is neutral. The initial
tranche achieves four typed speedups, but not the proposed 3% typed/untyped
parity goal for every pilot; small untyped regressions in §8.2 are retained.


## 9. Baseline repair and three-pilot continuation (2026-10-24)

The follow-up fixes the seven failures identified in section 8.3:

- Restore the missing business-plan oracle from the original DOCX main part's
  `w:t` nodes, independently of Lambda's converter. All 26 business-plan
  assertions pass.
- The mail fixture verifies CRLF framing and the absence of bare line breaks
  before rendering LF for its portable golden. MIME wire formatting is unchanged.
- Move all five Word `pn main()` checks to the existing procedural discovery
  convention, `tests/proc`. A shared fixture factory builds fresh equation,
  shape and group models, writes their snapshots/HTML and reads them back.
  The UI runner explicitly selects its own generated documents through
  `LAMBDA_WORD_EXPORT_UI=1`; missing UI files remain errors. The seven cases
  pass through the actual discovered-script harness, with no exclusions.

The continuation control is the prior Tune35 release,
`temp/tune35-next/control` (SHA-256
`dd66c90da4205b73b047f6b7268cd66e636f850b38480e477127db06d69bbad1`).
Canonical benchmark sources, helpers, goldens and inputs are unchanged.

Implementation under qualification:

- An implicit non-error-top admission is an identity function: non-errors
  pass unchanged, and errors are forwarded to the consumer's existing guard
  (S11.4.3). Removing the helper preserves short-circuiting and diagnostics.
- Plain float admission reuses the existing exact float representation test,
  including inline doubles, signed zeros and number homes. Conversion from
  another numeric domain retains the lossless runtime boundary (S11.4.5).
- Borrowed record parameters retain the Item carrier and CW33 home while
  sibling scalars may take native lanes. The complete signature still crosses
  checked admission; snapshot/detach and rebind publication are unchanged
  (S9.1.2-S9.1.3, D8.3.2-D8.3.3).
- Typed-array arguments may reuse an admitted local's precise home when later
  arguments are supplied identifier reads without borrowed positions. Calls,
  defaults and later borrows retain independent snapshot homes. An admitted
  array Item is already its raw header pointer; masking made another alias and
  the call callback homed an already owned register again (D5.3.4, D2.4.1).
- COW child walks inspect the stored physical descriptor and skip scalar lanes.
  Dynamic slots and container pointers retain their full decoder and sharing
  marks, preserving one-level snapshots (D3.4.6, S9.1.2).

Intermediate seven-pair screens are diagnostic, not acceptance evidence.
They show small changes in the three pilots; complete paired confirmation,
focused root/contract regressions and both aggregate gates follow below.


### 9.1 Paired release result

`temp/tune35-next/final-pairs.json` contains 21 alternating pairs plus one
unmeasured warmup per row, for all five original pilots and both language
variants. Every measured run has matching output; `golden-checks.json`
independently checks all ten rows against goldens (Splay checks PASS/8000).
No builds or regression suites overlapped the measurements.

| Typed pilot | Prior Tune35 control (ms) | Candidate (ms) | Candidate/control | Two-sided 95% interval |
|---|---:|---:|---:|---:|
| Pnpoly | 2.585 | 2.584 | 0.9996 | 0.9938–1.0078 |
| Splay | 214.439 | 212.145 | 0.9893 | 0.9876–0.9914 |
| Jq_records | 383.047 | 378.527 | 0.9882 | 0.9830–0.9956 |

The new typed gains are small: 1.1% for Splay and 1.2% for Jq_records.
Pnpoly's 200,000 fewer root stores do not improve its elapsed time; the
callee's guarded numeric loop and fallible return remain the next profiling targets.
This does not achieve parity: candidate typed/untyped is 1.1047 for Pnpoly,
1.0898 for Splay and 1.1436 for Jq_records. The Splay gap also narrows because
its untyped lane slows; that slowdown is not counted as a typed speedup.

Peer lanes remain explicit. Untyped Pnpoly is 1.0174x
(95% interval 1.0056–1.0300), untyped Splay 1.0172x
(1.0144–1.0191), and untyped Jq_records 0.9837x
(0.9747–0.9925). Typed Microdiff is 1.0052x (1.0009–1.0081);
untyped Microdiff is 0.9951x. Typed Fannkuch is 0.9966x and its untyped
lane is 0.9893x. These are per-row results, not a claim that every benchmark
improves. Pnpoly's interval does not establish a strict 3% untyped bound.

`provenance.json` pins HEAD, both emitter/runtime source hashes and binaries.
The candidate SHA-256 is
`793ff4d02595f4a498b5b74ab3d1f3de52dbc3f2cfe36133071b7d0cf6794c9b`.
Release emission passes 275/275 and forced-GC coverage passes 346/346.
The final six Tune35 emission fixtures and all seven repaired baseline cases
also pass through the actual harness after the signed-zero/home assertions.
The standalone release size ratchet remains 19/20: the existing
`lambda_cow_nested_store` emits 169 instructions against 166, as on the
previous control. Its budget is unchanged.


### 9.2 Aggregate baseline repair

`make test-lambda-baseline` now passes **8,201/8,201** including Input,
with no failing suite. The seven previously failing script cases pass, and
three new MIR fixtures add six aggregate checks (emission plus GC).
`temp/tune35-next/lambda-baseline.log`, `lambda-baseline-test_summary.json`
and `lambda-baseline-scripts.json` retain the full result. No baseline
exclusion, budget increase, vendor change or corpus ceiling is used.


The follow-up Test262 gate passes **40,261/40,261**, with no regressions,
retry, partial, timing, memory or eval reruns. The execution phase is 43.4s
under `caffeinate -is` (`temp/tune35-next/test262.log`). The rebuilt release
host is byte-identical to the measured pinned candidate.

Three alternating fresh-process compile samples plus separate code-size
censuses (`compiler-metrics.json`) give candidate/control ratios:

| Typed pilot | Build/transpile time | Generated code bytes |
|---|---:|---:|
| Pnpoly | 1.0208 | 1.0131 |
| Splay | 0.9738 | 1.0130 |
| Jq_records | 0.9778 | 0.9979 |
| Microdiff | 1.0006 | 1.0000 |
| Fannkuch | 1.0268 | 0.9981 |

These three-sample compile ratios are descriptive; Pnpoly's root reduction
comes with modest code growth rather than a measured execution-time win.


### 9.3 Broad screen, diagnostics and tier controls

The one-pair broad screen covers **142/142 rows** (71 typed plus 71 untyped),
all with matching control/candidate stdout and successful status. Its geometric
candidate/control ratios are 0.9952 typed and 0.9975 untyped; these are triage
statistics, not a repeated acceptance claim. All timings were serialized with
builds and correctness suites. Repeating the large one-sample outliers gives:

| Follow-up, 21 pairs | Candidate/control | Two-sided 95% interval |
|---|---:|---:|
| Untyped Richards | 1.0025 | 0.9984–1.0073 |
| Untyped Levenshtein | 0.9991 | 0.9775–1.0361 |
| Typed Quicksort | 1.0103 | 0.9603–1.0655 |
| Typed SHA-1 | 0.9858 | 0.9787–1.0049 |
| Untyped Jq_bf | 0.9517 | 0.9496–0.9571 |

Thus the large single-pair slowdowns do not persist. SHA-1's median improvement
is not established by its interval; Jq_bf's 4.8% improvement is. Small/uncertain
rows do not establish a universal 3% bound. The pilot/control regressions in
section 9.1 remain disclosed.

Final whole-process instrumentation (`final-counters.json`, not CPU fractions):

| Typed pilot | Root stores, control → candidate | Root reloads, control → candidate | Admission calls, control → candidate |
|---|---:|---:|---:|
| Pnpoly | 300,023 → 100,023 | 200,002 → 200,002 | 2 → 2 |
| Splay | 11,194,957 → 10,958,625 | 6,967,641 → 6,967,641 | 24,000 → 24,000 |
| Jq_records | 20,867,285 → 19,354,835 | 54,281,823 → 53,729,703 | 1,029,031 → 524,881 |

All six Tune35 fixtures produce identical control/candidate outputs on forced
T0. Five match their goldens; the existing `tune35_container_proofs` computed
index-assignment fixture returns the same unsupported E501 on both binaries.
`interp-fixtures.json` records that limit. MIR-interpreter GC stress is a
separate gate from T0 and passes all 346 cases.

All implementation and baseline fixes remain uncommitted. The final release
host, pinned candidate and source hashes are checked in `provenance.json`;
`gates.json` records both aggregate results and the separate release ratchet.
Remaining work is typed/untyped parity on the three pilots, especially
Pnpoly's guarded kernel and fallible-call consumer, Splay's remaining boxed
recursive field/home traffic, and Jq_records' remaining dynamic admissions.
