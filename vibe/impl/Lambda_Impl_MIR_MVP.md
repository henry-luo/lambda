# Lambda MIR MVP: typed execution implementation

The next milestone is planned in [language parity and untyped execution](Lambda_Impl_MIR_MVP_Language_Parity_and_Untyped.md).
This record retains the tuning wrap-up captured before commit `bdded83e2`.

2026-11-05. Implements and tunes
[the MIR MVP proposal](../Lambda_Design_MIR_MVP.md), following the
[JS MVP-Lmd approach](../jube/JS_MVP_Lmd.md). **Experimental; the performance
milestone remains open.** The current release has faster medians than corrected
Full MIR on all 77 registered benchmarks. The complete 30-pair primary run,
with the matched Sumfp supplement, clears all 77 confidence gates. Five completed
50-pair confirmations leave **76 clear wins and CD inconclusive**. CD's median
is still faster: 167.164 versus 173.874 ms, but its ratio upper bound is 1.011953.
No benchmark has a slower Full-comparison median.

Both compilers pass all 77 benchmark goldens. The Lambda baseline passes
**8,770 checks**: 6,658 fresh runtime checks and 2,112 unchanged parser results
with recorded provenance. Targeted poisoned-GC checks pass. The broader gate
retains 58 legacy failures plus four pre-existing Full-only negative-probe
failures, with no new normalized failure headers against the fresh control.

Base64 (+2.6%) and AWFY DeltaBlue (+2.7%) flag regressions against the starting
MVP in the primary campaign. Their independent follow-up was stopped before
sampling at the user's wrap-up request. Four of nine planned confirmations
remain unfinished. **Do not claim the all-77 performance milestone or zero
regression.** C2MIR is unmeasured. Changes are uncommitted.

Published [analysis](../../test/benchmark/lambda_mvp/tune77_phase3_analysis_2026-11-05.json),
[validation](../../test/benchmark/lambda_mvp/tune77_phase3_validation_2026-11-05.json),
[primary samples](../../test/benchmark/lambda_mvp/tune77_phase3_full_2026-11-05.json),
[Sumfp supplement](../../test/benchmark/lambda_mvp/tune77_phase3_sumfp_2026-11-05.json),
[completed confirmations](../../test/benchmark/lambda_mvp/tune77_phase3_confirmations_2026-11-05.json)
and [original-48 selection](../../test/benchmark/lambda_mvp/tune77_phase3_regression48_2026-11-05.json)
identify the same frozen release and source hashes. Historical phase2 results
remain separate, including their invalid Full controls and incomplete regression
qualification.

Authority: **D1.4, D1.6, D2.3.4–D2.3.5, D6.2–D6.3,
D8.2.4–D8.2.6, D7.4.1–D7.4.2**; numeric semantics
**S4.1, S4.4–S4.5**, declared contracts **S11.4.1v5**, and error
containment **S3.5, S7.7.2–S7.7.3**. No formal ruling changes.


## Current checkpoint

- **Wrapped up:** 2026-11-05 at the user's request. Resumed at `775c00d61` for all 77 faster than Full, preserving covered performance. The milestone remains open. No commit requested for this batch.
- Frozen **`guarded_admission`** release SHA256 `7d48648ec773ff4d1347b94a75c836daa22803f6fdb27de284112aac9c935948`; debug host `8a7b1428d04d27157c818ef4caa8bed837bb0c973e1c0313635802eb5cf7ee61`. All44 release/debug/live compiler source hashes match. Private release O3/ThinLTO/march=native; debug/assertions/profiling host is correctness only. Root executable, runtime layouts, vendor code and benchmark workloads/goldens are untouched.
- Artifacts: `temp/lambda_mir_mvp/tune77_phase3/`; frozen912-file corpus/four live inputs and private baseline workspace under `tune77_full/`. The fresh starting control rebuilt all766 translation units, SHA256 `601f852480d402053a243581609f75020ac0689074d5e46f9a33cff2f7de708b`.
- Changes: runtime-spine RMW binding/release, shared guarded string append, checked-length success facts, nonretaining pure calls, discarded query storage, direct unprofiled COW marking, Boolean negation/logical success facts, bounded recursive array expansion and fused `len(find(...))` using shared search/window semantics (**D5.4.1–D5.4.4, S3.4, S7.9.3, S9.1.2–S9.1.3, S9.3.1, S17.7.1**).
- Direct-parent RMW selects an audited noncollecting wrapper over the existing policy. Deeper paths retain the collecting helper because indexed/virtual reads can materialize ancestors (**D5.4.4, D6.3.2**). Full retains its original helper selection. Full controls fix proof-query recursion and typed-array COW publication (**D8.2.5, D4.3.3, D5.4.6**).
- Dynamic native float admission reuses the proven int-to-float relation. Exact float-to-int admission checks finite range and round-trip equality before selecting the int lane. Primitive-array admission requires the physical lane and exact uncounted error-free primitive certificate; other shapes retain runtime validation (**S4.1, S4.4–S4.5, S11.4.1v5, D4.3.3**).
- Profiles: CD removes39,799 excess plain-array copies/26,745,128 copied bytes and uses440,622 noncollecting root RMW calls. Its runtime type checks drop100,100→20,100. Microdiff removes106,600 spread pushes/41,000 discarded arrays and all61,501 NOT/63,554 truth helper calls. RegexRedux replaces nine find-array constructions with count-only calls. Permute still records5,924 stack checks; no call-reduction claim applies to the bounded recursive change.
- Final release passes all77 MVP/Full benchmark goldens,18 forced-GC benchmark cases and expanded/u32/recurrence fixtures in all MVP normal/forced/seeded/profiled poisoned-GC modes. The broader65-group gate has62 failures:58 legacy broader-gate failures plus four pre-existing Full-only negative-probe failures. The same harness on the fresh starting control has69 failures; no new normalized headers.
- Full's negative probe loses the Error payload of `not (error(...) in ...)`. Both starting/current Full print false; both MVPs print the payload. The byte membership ABI loses Error identity before NOT. Golden and failures remain visible (**S7.9.3**). Actual benchmark controls pass.
- **Final baseline:8,770/8,770**, comprising6,658 fresh runtime checks and2,112 reused unchanged parser results with provenance hashes. No reruns for this final host. Earlier hosts retained ENOSPC/link-start races and an omitted production NO_GC audit entry; the latter was corrected only after proving the zero-link helper cannot allocate/index/reenter. Private object resolution now follows reuse-manifest chains. Losslessly archived older logs/binaries have inventories; do not archive active hosts.
- User authorized stopping the competing dbench baseline; exact four-process inventory `user-authorized-baseline-stop.json`. Never replace shared executables or interrupt unrelated work. Lambda baseline only.
- **Final prespecified protocol:** all77×30 against corrected same-binary Full and the fresh starting MVP; matched Sumfp10k×30; original48 selected from the same30-pair campaign, not another run. Nine fixed50-pair confirmations: CD, Microdiff, RegexRedux, NavierStokes, HashMap, JQRecords, Pidigits, Permute, Diviter. Paired release execution windows exclude startup/JIT; whole pairs disturbed by builds/tests are discarded. Clear Full win requires the one-sided95% ratio upper bound below1; starting-MVP regression requires the two-sided95% lower bound above1. Historical stage24 partial comparisons remain separate.
- Final primary: **77/77 clear Full wins** using 30 paired observations and the matched Sumfp supplement. Original 48 and added 29 both pass their primary gates. No engine/output failures. The raw zero-duration primary Sumfp observation remains retained and unqualified separately.
- Five completed 50-pair confirmations: CD remains faster but inconclusive; Microdiff, RegexRedux, NavierStokes and HashMap pass. Replacing these preselected rows with their confirmation observations yields **76/77 clear wins**, including 48/48 original entries and 28/29 additions. No slower Full medians. Do not discard CD's repeat because its primary passed.
- Primary starting-MVP regression flags: Base64 +2.6% and AWFY DeltaBlue +2.7%. The completed confirmations add no regression flags. No zero-regression claim. The original-48 report is a selection of the same 30-pair primary campaign; its timer-limited aggregate stays unresolved, with matched Sumfp retained separately.
- At user wrap-up, stopped the unfinished JQRecords confirmation and remaining Pidigits/Permute/Diviter queue. Stopped the queued 100-pair regression follow-up before it sampled. Exact own-process inventories are `wrapup-stopped-confirmations.json` and `wrapup-stopped-regression-followup.json`; no workers remain. All completed raw reports are preserved; no new compiler edits followed the frozen qualification.
- Next resume: resolve the two starting-MVP flags and CD's narrow Full margin, then finish source-matched qualification. CD's profile still records 435,072 pair-array constructions, 870,144 literal append calls and 20,100 runtime type checks. These are measured costs, not a validated new optimization. No literal-allocation change was implemented during wrap-up. C2MIR remains unmeasured.

## Implementation

The new compiler lives in [`lambda/runtime/mvp/`](../../lambda/runtime/mvp/):

| File | Responsibility |
|---|---|
| `mvp.hpp` | Development selector, compiler lifecycle interface and emission counts. |
| `mvp_internal.hpp` | Function, binding and lexical error-target plans using shared runtime/compiler types. |
| `mvp_plan.cpp` | Admission over the shared indexed AST, contracts, range queries, recursive effect convergence, native return ABI and module lifecycle. |
| `mvp_mir.cpp` | Fresh scalar, array, control-flow and direct-call lowering through `MirEmitter`. |

The shared [`mir_loop_analysis.hpp`](../../lambda/runtime/mir_loop_analysis.hpp)
now owns the decimal-literal query and descending-sum pattern previously local
to production. [`mir_bitwise.hpp`](../../lambda/runtime/mir_bitwise.hpp) owns the
existing guarded compact-int bitwise emitter, extended to consume native operands.
Both compilers use these helpers; MVP still makes its own lowering decisions.

`transpile-mir.cpp` selects these planning/lowering/finalization callbacks in the
existing pass manager. Its driver still owns MIR initialization, synchronization,
linking, module layout, constant/type binding, and adoption by `Script`. The MVP
does not call production AST lowering. `runner.cpp` validates the selector before
tier planning and cache lookup and disables shared compiled-artifact caching for
MVP. The ordinary per-runtime loaded-script registry remains in use.

Values, types, objects, `Context`, GC and helper implementations are shared.
There is no private heap or value ABI. `MirValue`, `FnVariantAnalysis`, return
companions, scalar homes, root liveness and stack-overflow recovery come from the
shared emitter (**D2.3**, **D6.2–D6.3**). Boxed bindings and reads adopt cold scalar
bytes into owned homes; native scalar recursion avoids collecting-call ceremony
when its effect plan proves that safe. Lowering rejects an emitted collecting
call that violates a planned noncollecting function.

Integer arithmetic checks the int53 domain unless an interval proof establishes
a finite result, and uses existing slow helpers for saturation, nonfinite operands
and division edge cases (**S4.1**, **S4.4–S4.5**).
Floating arithmetic stays in native double registers. Proven direct calls retain
native arguments/results; fallible calls preserve the error lane. Declared
bindings and call arguments check admission independently from representation
conversion (**S11.4.1v3**). Local declaration failure skips the declaring block;
argument rejection becomes the call's error value (**S7.7.2–S7.7.3**).

The initial slice modified no runtime helpers. The follow-up tuning below reuses
the integer renderer and bulk zero fill in shared helpers; vendored dependencies
remain unchanged. Build inputs are registered in `build_lambda_config.json`;
no-MIR builds exclude both MVP implementation files.

### Integer tuning

- **Bounded countdown:** the shared recognizer admits precisely two ordered
  assignments, `s = s + n; n = n - 1`, under `n >= 0`. MVP guards a zero
  accumulator and `0 <= n <= 100000000` at entry. The maximum triangular sum is
  computed from that bound and statically checked against `INT53_MAX`. The fast
  loop uses four independent partial sums and a scalar remainder. Every term,
  partial and merge stays finite, so reassociation is exact; the counter still
  finishes at `-1`. Other entries execute the original checked loop. Reordered,
  additional or nested writes do not match. This is a general
  source-shape proof, independent of file names, function names or benchmark inputs.
- **Binding intervals:** the existing indexed binding-use lists establish finite
  initializers and every assignment. A local counter with exactly one `+1` write
  can acquire a bound from its nearest enclosing `while` comparison, including an
  `and` condition. A different enclosing loop, extra writes or an unknown bound
  withhold that proof. A fixed point handles dependencies between bindings;
  expression ranges are queried transiently, with wide intermediate arithmetic
  preventing overflow in the proof itself (**D8.2.5**).
- **Native consumers:** proven finite arithmetic, comparisons and int-to-double
  conversion omit poison/overflow checks. Finite Item packing/unpacking uses the
  shared representation primitives, retaining helper paths for nonfinite lanes.
  Scalar `var` parameters and captures remain excluded from these proofs.
  The array extension admits only array borrows and invalidates array facts at
  their write-back boundaries.
- **Bitwise operations:** inline finite operands/results, preserve the original
  operands on a guard miss, and use the shared runtime for negative/exhausted
  shift counts and out-of-band words. A machine result equal to a lane sentinel
  must reach the helper as a computed word, not be mistaken for poison. The
  shared success-contract field avoids a repeated type check while retaining the
  error arm at declared boundaries (**S7.7.2–S7.7.4**, **S11.4.1v3**).

The range work also exposed a dangling compiler provenance pointer from unary
minus's temporary subtraction descriptor. Results now retain the original unary
AST node, so later range/representation queries never read expired stack storage.

### Native calls and consumers

- **Bounded inlining:** native scalar functions with no collecting, reentering
  or error effects can expand at a direct admitted call. Admission is capped at
  96 indexed nodes per callee and 256 expanded nodes per generated function. A
  target expands only once along an inline path; recursive edges beyond that
  retain real calls and stack guards. Arguments evaluate once before fresh
  parameter/local registers are installed, and each inline return has its own
  join. The same lowering handles `fn`, `pn`, early returns and nested scalar
  calls (**D7.3.3**, **D6.3.6**).
- **Integer guards:** add/subtract need a finite result and one finite operand
  to exclude all lane sentinels. When neither operand is statically finite,
  checking one operand after the result is sufficient; the old slow helpers
  retain saturation and indeterminate cases. NaN comparisons use predicate
  masks instead of control-flow joins (**S4.1.1–S4.1.2**, **S4.2.3**).
- **Numeric success contracts:** arithmetic and comparisons can consume a
  boxed `int-or-error` bitwise result directly. Both operands evaluate in source
  order, error arms retain their Items, and only the successful arm unboxes for
  native arithmetic. A declared destination still checks the error arm; it can
  omit the redundant type helper. Mandelbrot's shift-plus-add therefore avoids
  `fn_add` and `lambda_type_check` in the hot path (**S7.9.2v3–S7.9.3**,
  **S7.7.2–S7.7.4**, **S11.4.1v3**).
- **Branch demand:** proven Boolean `and`/`or` conditions branch directly with
  ordinary short-circuit order. `while` tests before entry, then branches back
  conditionally; `continue` still reaches that test. This avoids temporary
  Boolean joins and an unconditional backedge (**S3.4**, **D8.2.6**).
- **Floating constants:** each distinct literal bit pattern gets one register
  definition at function entry. MIR otherwise materializes these literals as
  loads inside a loop, including Mandelbrot's `2.0` and `4.0` each iteration.
  The compile-time pool distinguishes signed zero and NaN payloads, and all
  floating arithmetic retains its original order and native representation (**D2.3.5**).

## Use and admitted surface

Set the compiler once before starting the process:

```sh
LAMBDA_EXEC_BACKEND=jit LAMBDA_MIR_COMPILER=mvp \
  ./lambda.exe run test/benchmark/r7rs/fib2.ls
```

Unset `LAMBDA_MIR_COMPILER`, or select `production`, for ordinary compilation.
MVP requires explicit `jit`; `auto`, `interp`, unknown selector values and no-MIR
builds reject the request. Changing the selector within an existing `Runtime`
is unsupported. Unsupported programs receive a diagnostic before execution;
there is no fallback to production compilation.

The initial implementation surface was the following; later additions below
supersede its limits and boundary behavior:

- Top-level, fixed-arity `fn`/`pn` definitions and direct recursion, with scalar
  `int`, `float`, `bool`, `string`, `null`, boxed `any`, and rank-one
  `int[]`, `bool[]`, `float[]` contracts.
- Local `let`/`var`, assignment, scalar literals, arithmetic/comparison,
  short-circuit logic, string joining, `if`, `while`, return, break and continue.
- The selected builtin surface: single-argument `print`, `clock`, `abs`, `floor`, `math.sqrt`, `math.sin`,
  `int`, `float`, `string`, `len`, `bor`, `bxor`, `shl`, `shr`, `fill`, `ord` and `chr`. Builtin combinations can still
  reject when their required representation or control-flow contract is outside
  the admitted subset; the names alone are not a general compatibility promise.
- Primitive array reads and writes through local bindings, dynamic local sequence writes; direct invariant
  `var` array arguments; homogeneous primitive array literals; single-source
  procedural integer `for` ranges.
- Immutable numeric/bool module bindings with call-free scalar initialization
  and earlier immutable scalar dependencies. Native module slots contain no
  GC pointers, and main runs only after successful initialization (**S7.7.6**).

Native-return procedures currently require a final return statement. Fallible
while conditions, multi-value functional content, explicit error propagation,
mixed/nested array literals, path borrows, records, imports, closures, indirect calls, optional/named/spread
arguments, exports and suspension are outside this slice. All indexed bodies,
including uncalled definitions, undergo admission. General REPL, embedding and
AUTO-tier integration remain later work.

The current outcome addition admits deferred local validation, explicit `^`,
native/error return pairs, base-type `is`, and ordinary unfiltered `^ { … }` /
`or^ { … }` recovery (**S7.6.1v9, S7.6.3v3, S7.6.8, S7.7.2v2/S7.7.4v2**).
Filtered handlers and use of the caught-error pseudo-variable remain outside
this addition; unsupported forms still reject before execution.

## Verification

The focused gate runs with an explicit MVP selector:

```sh
python3 test/benchmark/test_lambda_mir_mvp.py
# To use a pinned binary after baseline tests replace lambda.exe:
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/native-final-release.exe \
  python3 test/benchmark/test_lambda_mir_mvp.py
```

Eleven test groups passed on the release executable: all four benchmark goldens;
numeric boundaries, live roots, cold scalar ownership and contained errors under
ordinary, forced/poisoned and seeded/poisoned GC; rejection before side effects;
selector validation; native recursive MIR inspection; stack-overflow recovery;
failed assignment diagnostics naming the binding (**S7.7.4**); and native MIR
checks for the guarded countdown and bounded float-accumulation loop, direct
numeric success consumers, and floating literal definitions preceding the loop.
The emission check requires native Fibonacci arithmetic/direct calls and forbids
generic add/subtract, boxing, admission checks and side-stack growth in that body.
The integer fixture additionally covers nonzero/saturating seeds, negative and
poison countdown inputs, reordered/additional writes, nested loops, native/boxed
shift agreement and failed bitwise operands. The native fixture covers recursive
parameter/local isolation, nested early returns, expression-bodied functions,
signed-zero constants, chained success/error consumers, argument side effects,
short-circuiting, break/continue and every remainder length of the unrolled sum.
All semantic fixtures run under the three GC schedules.

### Array extension

The additional rows are `larceny/array12.ls` and `awfy/sieve2.ls`. Runtime
types, array layouts, certificates and GC are unchanged. A `fill` at a typed
binding uses the shared destination-aware constructor, retaining its error arm
while avoiding a second admission pass. Checked stores also use the existing
helper library. Physical layout/bounds guards and the
`Context::mir_var_homes` transport primitives were promoted from production
into `mir_container_access.hpp`; both emitters now call them.

- **Reads and writes:** int/bool indexing uses existing nullable scalar lanes;
  invalid reads retain null, including negative and 64-bit out-of-bounds keys
  (**S7.1.1v3**, **D2.3**). The float extension below adds native float reads. Native stores
  require a matching primitive layout, writable owner and valid index. The
  existing checked setter owns detachment, dynamic admission and failed stores
  (**S7.1.3v2**, **S11.4.1v3**).
- **Snapshots and borrows:** ordinary array captures share-mark their owner.
  Direct `var` array calls evaluate all operands before publishing addresses
  of fixed suffix slots in the precise root frame. Callees consume those
  transport cells before calling out and publish each replacement; callers
  reload after normal and error returns. This handles nested forwarding,
  reassignment and mixed plain/`var` arguments (**S9.1.2–S9.1.3**, **D6.3.1**).
- **Array descriptors:** layout, length and mutability guards are refreshed
  after binding replacement and borrowed calls; captures revoke the writable
  fact. A guarded extent covers indices with a proven nonnegative interval.
  Buffer pointers are normally reloaded at access. A two-statement scalar
  store loop may retain its buffer because only its checked-store fallback
  can collect; that fallback reloads the buffer before the backedge
  (**D5.4.4**, **D5.4.5**).
- **Integer proofs:** a private scalar parameter gets an interval only when
  every direct call supplies one and the parameter is never assigned. Range
  induction and nonnegative guarded steps extend local counter proofs. A loop
  condition can narrow a read only before the first body write of that binding;
  the proof passes through a nested loop only when that entire loop never
  writes the binding (**D8.2.5**, **S4.1**).
- **Array reduction:** the exact body `sum = sum + a[i]; i = i + 1` under
  `i < limit` gets a four-lane reduction only when `fill` and every write prove
  the complete element interval. Whole-array reassignment or a mutable borrow
  withholds that proof. Entry guards check a zero accumulator, nonnegative
  start, physical layout, array length and a trip bound derived from the
  element magnitude. Every partial sum and merge must fit int53. Other inputs
  execute the original checked loop; no result or iteration is omitted
  (**S4.1.2**, **D5.4.4**).
  The 2026-11-09 regression fix also admits a local boxed `int | error`
  accumulator with an `int` receiving contract. Deferred admission had changed
  these bindings to Item storage, so the native-only eligibility check disabled
  the reduction. An exact boxed-zero guard now selects native temporary sums
  and boxes the finite result on success; nonzero seeds, retained errors and
  shared homes keep ordinary assignment behavior (**S7.7.2v2**, **S7.7.4v2**, **S4.1.2**).
  The existing array fixture covers successful and failed accumulator admission,
  including zero trips; the MIR gate retains the four-step assertion.
  The split debug host passes the reduction and adjacent MIR checks and the
  ownership fixtures in normal, forced and seeded poisoned-GC modes. The
  154 MVP/production benchmark golden checks retain one MVP quicksort rejection
  (`lambda_type_check_lazy` in a closed numeric region); a current-source control
  with only this fix reverted reproduces it. The aggregate Lambda gate stops
  before execution because `test_wpt_html_parser_gtest` has no build target.
  Evidence: `temp/mvp-array-reduction/`.

The focused gate now has **13 groups**, covering all six benchmark goldens,
forced-GC benchmark execution, reduction MIR, and the new
[`mir_mvp_arrays`](../../test/lambda/mvp/mir_mvp_arrays.ls) fixture. It checks
snapshot preservation, both plain/borrowed argument orders, nested argument
evaluation, borrowed replacement/forwarding, a mutating index expression,
GC between accesses, empty arrays, failed stores, range break/continue,
the int53 endpoint, reduction remainders, negative elements, nonzero seeds,
short arrays and a counter changed before its indexed store.
The mutating-index case checks the MVP's current object-before-key evaluation
and resulting temporary ownership; it does not impose that order on production
(**S7.7.1** explicitly leaves strict left-to-right order non-normative).

`mir_mvp_scalar` and `mir_mvp_roots` also pass through the original production
JIT executable and live in the ordinary procedural golden directory.
[`mir_mvp_errors`](../../test/lambda/mvp/mir_mvp_errors.ls) is an explicit MVP-only
regression: the original production compiler loses the error from `shl(1, -1)`
at a declared `int` binding and executes code that **S7.7.2** requires it to skip.
The new compiler retains it. That production discrepancy remains open; the
fixture is not presented as a passing production-baseline test.

The release build, Python syntax checks and no-MIR header syntax check passed.
The initial implementation's `make test-lambda-baseline` completed with **8,490/8,495** passing: four local
HTTP tests could not bind loopback sockets in the sandbox; all four passed when
rerun with socket access. The remaining failure is `proc_mir_mvp_roots` on the
default AUTO tier: after `let b: any = a; a = boxed(2e-310)`, `b == 1e-310` is
false. This reproduces on the original, pre-change production binary with both
`interp` and `auto`; production `jit` and MVP pass. Its correct golden remains
unchanged and this interpreter ownership discrepancy is open. The full baseline
is therefore **not green**. Logs are under `temp/lambda_mir_mvp/` and
`test_output/`. A complete no-MIR build, other architectures and performance
confidence intervals were not part of that initial screen.

The integer-tuning baseline completed with **8,493/8,495** passing, including all
350 forced-GC stress cases and all 152 web-server cases. Besides the same AUTO
ownership failure, it caught a changed bitwise-helper prototype name introduced
by the shared-emitter extraction. Restoring production's original naming fixed
that regression without changing the test. After rebuilding, the complete MIR
emission suite passes **279/279**, and all **9** focused MVP groups pass. The
baseline was not repeated after this prototype-name-only fix; the known AUTO
failure remains open. All four C ports also pass their goldens with the original
timer adapter, producing exactly one timing marker apiece.

The native-tuning baseline completed with **8,494/8,495** passing: all **279**
MIR emission tests, **350** forced-GC stress tests and **152** web-server tests
pass. Its sole failure is the same pre-existing `proc_mir_mvp_roots` AUTO-tier
ownership issue. The MVP's **11** focused groups pass on the pinned release
executable. Logs for this qualification are under `temp/lambda_mir_mvp/tune2/`.
Finalized MVP, production and C2MIR MIR for all four kernels, with hashes, are
retained in that directory's `mir/`; generated ARM64 code was also inspected
to verify the inner-loop constant loads and their removal.

The array-extension baseline completed with **8,501/8,502** passing, including
**279/279** MIR emission, **350/350** forced-GC, **55/55** optimizer and
**152/152** HTTP tests. Its sole failure is the same `proc_mir_mvp_roots`
AUTO-tier issue, reproduced again with the archived pre-change binary at
`270c95e01bbe5e44186ec1ce12f261860ef05345`. The extraction initially used
frame state too early when production consumed incoming borrow homes; the
helper now takes the incoming runtime register explicitly, and the complete
rerun above verifies that correction. The subsequent MVP-only typed-`fill`
fusion passed all **13** focused groups on the final release build, including
negative/wrong-typed construction, empty fill and forced GC. The aggregate gate
is still **not green** because of the pre-existing AUTO failure. Logs and
pinned control/candidate binaries are under `temp/lambda_mir_mvp/arrays/`.

## Release measurements

[Scalar milestone raw samples and provenance](../../test/benchmark/lambda_mvp/native_2026-11-01.json)
retain **60 measured runs and two warmups per engine**, seeded interleaving,
complete non-timing golden checks, round order, source/timer/runner hashes and
executable hashes. The larger campaign was selected before qualification to
reduce uncertainty on the short rows. Builds, regression suites and diagnostic
code generation had finished before timing.

MVP and full production MIR use **the same pinned release executable**, differing
only in compiler selection. The previous MVP is the immediately preceding
integer-tuning release, measured again in this campaign. The
[integer-tuning results](../../test/benchmark/lambda_mvp/integer_2026-11-01.json)
and [initial scalar screen](../../test/benchmark/lambda_mvp/scalar_2026-11-01.json)
remain historical evidence. The base revision is
`d8c604ec5611ee7cac4a4bc6dd4b5e15228149a8`; the candidate is the recorded dirty
working tree on macOS arm64. The sandbox blocked the CPU model query, recorded
in the report.

The C ports' optional kernel timer excludes result validation and printing,
matching Lambda's boundaries. The old C runner retains its existing main timer
when that option is absent. The shared C clock remains `gettimeofday`; Lambda
uses its monotonic runtime clock. Compilation/startup are outside these timings.
C2MIR remains the independent C reference (**D1.6**).

| Benchmark | Previous MVP, ms | Current MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/fib | 1.5120 | 0.9540 | 1.2000 | 1.0435 | 0.769–0.814× | 0.885–0.940× |
| r7rs/sum | 0.2715 | 0.0995 | 0.2705 | 0.2705 | 0.357–0.381× | 0.354–0.381× |
| r7rs/sumfp | 0.0700 | 0.0660 | 0.0720 | 0.0754 | 0.890–0.944× | 0.845–0.919× |
| awfy/mandelbrot | 35.6695 | 24.2870 | 28.3575 | 22.5865 | 0.848–0.864× | 1.064–1.087× |

Intervals use the existing paired-bootstrap ratio-of-medians helper, 10,000
resamples, retaining round pairing. Relative to full MIR, MVP takes **20.5%,
63.2%, 8.3% and 14.4% less time**, respectively. All four ratio intervals are
below 1.00. Relative to the previous MVP, the reductions are **36.9%, 63.4%,
5.7% and 31.9%**.

All four selected rows satisfy the per-row C2MIR criterion: their one-sided
95% upper ratio bounds are **0.935, 0.379, 0.907 and 1.085**, below 1.10.
Mandelbrot's median remains **7.5% slower than C2MIR**; the other three are
faster. The four-row median-ratio geometric means are **0.692× full MIR** and
**0.750× C2MIR**. These are point estimates, not aggregate confidence bounds.
The preservation gate passes for these four rows, but the full eight-row
milestone remained **unqualified** in that campaign because the array rows were unsupported.
Cross-platform results, compile/startup cost and general language replacement
are outside this campaign.

The workload audit is unchanged: fib computes `fib(27)`; sum executes 100
sums descending from 10,000; sumfp accumulates 100,000 through zero; Mandelbrot
uses a 500×500 grid and at most 50 iterations per pixel, checking 191. The C
ports' narrower machine ints do not overflow on these inputs. Lambda retains
int53 semantics on other inputs. The integer reduction is unrolled after the
source-shape and runtime proof, with every term retained; no benchmark result
or function body is replaced. Floating arithmetic order is unchanged.
Mandelbrot's authored C early break and Lambda `not_done` control flow remain.

Reproduce after a release build, without concurrent builds/tests:

```sh
python3 test/benchmark/lambda_mir_mvp.py --release --subset scalar \
  --exe temp/lambda_mir_mvp/native-final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/integer-final-release.exe \
  --runs 60 --warmups 2 --output temp/lambda_mir_mvp/native-final-results.json
```

The omitted `--control` compares both compilers in the same executable. These
paths are locally archived build products. In a new checkout, build with
`make release` and pass that executable; omit `--previous-mvp` when no prior
binary is available. `--release` attests build mode rather than inspecting it.
`--legacy-timer` reproduces the historical C timing scope.

## Six-row array-extension measurements

[Raw samples and provenance](../../test/benchmark/lambda_mvp/arrays_2026-11-01.json)
retain 60 measured runs plus two warmups for **all six rows**, with the same
release binary for MVP and production and the same paired-bootstrap method.
The base revision is `270c95e01bbe5e44186ec1ce12f261860ef05345`; all builds,
regression suites and diagnostics had finished before timing. The new C ports
use the optional kernel timer around their original computation, excluding
validation/output just as the Lambda clock pairs do. The original Lambda benchmark
sources and goldens are unchanged.

| Benchmark | MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 0.9085 | 1.1730 | 1.0350 | 0.757–0.805× | 0.849–0.906× |
| r7rs/sum | 0.0990 | 0.2680 | 0.2665 | 0.356–0.380× | 0.357–0.386× |
| r7rs/sumfp | 0.0675 | 0.0735 | 0.0725 | 0.882–0.935× | 0.903–0.957× |
| awfy/mandelbrot | 23.7465 | 28.0605 | 22.2408 | 0.838–0.854× | 1.057–1.077× |
| larceny/array1 | 0.1160 | 1.3745 | 0.2841 | 0.081–0.089× | 0.387–0.424× |
| awfy/sieve | 0.0280 | 0.0250 | 0.0120 | 1.038–1.160× | 2.212–2.424× |

`array1` now takes **91.6% less time than full MIR** (11.8× faster) and
**59.2% less than C2MIR** (2.45× faster). Its native unrolled reduction executes
every element. The C port uses 32-bit machine ints; its fixture stays in range,
while Lambda retains int53 checks/fallbacks for other inputs (**S4.1.2**).
The four scalar rows still beat full MIR and retain one-sided 95% C2MIR ratio
bounds below 1.10.

**Sieve remains unqualified:** its median is **12% slower than full MIR** and
**2.34× C2MIR**. The native byte-array body, borrow transport and result oracle
are supported, but coverage is not a performance pass. This 12–28 µs row is
also sensitive to timer resolution; a separately recorded repeated-call harness
remains part of its qualification work. The six-row geometric mean must not
hide this failure. At that checkpoint `spectralnorm` and `nbody` were unadmitted.

```sh
python3 test/benchmark/lambda_mir_mvp.py --release --subset all \
  --exe temp/lambda_mir_mvp/arrays/final-release.exe \
  --runs 60 --warmups 2 --output temp/lambda_mir_mvp/arrays/results.json
```

`--subset scalar`, `--subset array` or `--subset float-array` selects a stage. Use an archived
previous MVP only with a subset that it supports. The final release binary's
SHA-256 and all compiler/benchmark/timer hashes are retained in the raw report.

## Float-array extension (2026-11-02)

`beng/spectralnorm2.ls` and `awfy/nbody2.ls` run unchanged, completing coverage
of the eight selected workloads. The runtime value model, array certificates,
borrow transport and precise collector are unchanged (**D2.3**, **D5.4**, **D6.3**).

- Native float reads use the existing nullable float lane. Its reserved NaN
  payload denotes absence; ordinary NaN, signed zero and subnormals remain
  numbers. Arithmetic propagates absence, boxing maps it to null, declared
  float boundaries reject it, and stores take the checked setter on absence
  (**D2.4**, **S7.1.1v3**, **S7.1.3v2**, **S11.4.1v3**). The null-test emitter
  is shared with production; comparison semantics retain the checked path.
- Homogeneous primitive literals allocate through the existing initialized
  `array_num_new` constructor, now registered as a JIT import. The array is
  precisely rooted while members evaluate, and each store reloads its buffer.
  Destination admission installs the ordinary array certificate (**D6.3.1**).
- Immutable numeric module bindings use native scalar slots and call-free
  initialization. Dependencies must be earlier immutable scalar expressions.
  Unsupported module calls reject during planning, before MIR construction
  or execution; failed module initialization skips main (**S7.7.6**).
- Closed numeric functions receive a guarded native body and their original
  checked body. Entry guards establish every array's layout and required
  extent, plus exclusive writability for mutated parameters. The native body
  admits no array captures/replacements, conditional expressions, or collecting/
  reentering calls; its buffer pointers remain valid for that body. Emission
  verifies that it introduced no collecting call. Short/shared inputs execute
  the original checked operations, retaining partial-update/error behavior
  (**D5.4.4**, **D7.4.1**, **S9.1.2–S9.1.3**).
- Outer counter bounds now survive a nested loop that never writes that
  counter, including writes after the current use. Previously an extent of six
  for nbody's five-element arrays forced every access through the helper.
  Scalar numeric conversions and float `sqrt` use native lanes. Division by
  a proven positive integer power of two uses its exactly representable
  reciprocal, preserving IEEE rounding without reassociating expressions
  (**S4.5.2**, **D8.2.5**).

All **15 focused groups** pass on the archived release, including all eight
benchmark goldens and forced/poisoned GC on the four array benchmarks.
[`mir_mvp_float_arrays`](../../test/lambda/mvp/mir_mvp_float_arrays.ls) adds
null/NaN distinction, signed zero, subnormals, invalid/dynamic indices,
rejected stores, primitive literals, module constants, snapshot/borrow
detachment, short-input fallback and a nested loop that writes an outer counter
after its indexed use. The semantic fixtures run under ordinary,
forced/poisoned and seeded/poisoned GC. MIR inspection requires both nbody
bodies and no checked-array/admission calls before the first native `sqrt`.
Additional tests cover module initialization failure and admission before
execution.

The required baseline completed at **8,501/8,502**, including all **279** MIR
emission checks, **350** forced-GC cases and **152** HTTP tests. The only failure
is the existing `proc_mir_mvp_roots` AUTO-tier cold-float snapshot issue. It was
reproduced with the archived control at
`f313b325adba945c5227d47d8f58d0786fffc612`; the correct golden is unchanged.
The aggregate gate remains **not green**. Logs and pinned binaries are under
`temp/lambda_mir_mvp/float_arrays/`. The release executable was restored after
the baseline completed; Python syntax checks and `git diff --check` also pass.

The C reference sources retain their original algorithms and gain optional
timer boundaries. Nbody times initialization, all 36,000 steps and energy,
excluding validation/output. Spectralnorm's authored Lambda clock includes
formatting/output; its C timer does too. The C formatter uses `printf` while
Lambda uses its authored `format9` routine, an explicit remaining workload
difference. Compilation/startup remain excluded, and each row records its scope.

### Eight-row release comparison

[Raw samples and provenance](../../test/benchmark/lambda_mvp/float_arrays_2026-11-02.json) retain **60 measured runs and two warmups per engine**, with seeded interleaving, exact non-timing golden checks and paired bootstrap intervals. MVP and production use the same pinned release binary; the base revision is `f313b325adba945c5227d47d8f58d0786fffc612`. Builds and regression suites completed before measurement.

| Benchmark | MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 0.9470 | 1.2140 | 1.0645 | 0.764–0.817× | 0.859–0.916× |
| r7rs/sum | 0.1005 | 0.2735 | 0.2778 | 0.353–0.384× | 0.355–0.383× |
| r7rs/sumfp | 0.0690 | 0.0735 | 0.0740 | 0.895–0.973× | 0.896–0.960× |
| awfy/mandelbrot | 23.4750 | 28.0860 | 22.2190 | 0.827–0.848× | 1.045–1.074× |
| larceny/array1 | 0.1175 | 1.3865 | 0.2954 | 0.081–0.090× | 0.381–0.409× |
| awfy/sieve | 0.0280 | 0.0260 | 0.0129 | 1.037–1.120× | 2.012–2.341× |
| beng/spectralnorm | 0.5020 | 1.1415 | 0.3650 | 0.417–0.457× | 1.283–1.394× |
| awfy/nbody | 1.9450 | 2.7325 | 1.4971 | 0.677–0.722× | 1.250–1.335× |

The two added rows take **56.0% and 28.8% less time than full MIR**, respectively. Their remaining C2MIR gaps are **37.5% for spectralnorm** and **29.9% for nbody**. Their one-sided 95% upper ratios, **1.389×** and **1.324×**, exceed the 1.10× target, so neither is performance-qualified yet. Sieve remains unqualified at **1.077× full MIR** and **2.164× C2MIR**; its very short timer interval still needs care. The other five rows beat full MIR and retain their per-row C2MIR qualification. All eight rows are supported; the eight-row performance milestone remains open.

```sh
python3 test/benchmark/lambda_mir_mvp.py --release --subset all \
  --exe temp/lambda_mir_mvp/float_arrays/final-release.exe \
  --runs 60 --warmups 2 --output temp/lambda_mir_mvp/float_arrays/results.json
```

In a fresh checkout, build with `make release` and use `--exe ./lambda.exe`. The original Lambda benchmark sources and goldens remain unchanged. The raw report records source, runner, timer and executable hashes; timing does not establish cross-platform or whole-language performance.

## Recursion, sorting, matrix and shift expansion (2026-11-02)

Coverage grows from eight to **13 unchanged typed benchmarks**: `r7rs/tak`,
`r7rs/ack`, `larceny/quicksort`, `kostya/matmul`, and `kostya/collatz` join the
original population. Runtime types, data models, allocation, rooting and ABI
remain shared (**D2.3**, **D6.3**, **D7.4.2**).

- `shr` uses the existing shared guarded bitwise emitter. Finite int53 operands
  and a count below 64 take native arithmetic right shift; negative/large
  counts, poison and dynamic operands retain the checked Item helper. The
  result keeps its error arm through typed admission. The `bxor` registry row
  describes a raw integer ABI, so its fallback explicitly keeps `fn_bxor_item`.
  No runtime numeric policy changes (**S4.1.2**, **S7.7.2–S7.7.4**).
- Nullable int/float comparisons now stay in registers. Both operand expressions
  evaluate before absence tests. Non-null operands reuse the existing numeric
  comparison, including NaN masks; equality compares absence, while ordered
  relations produce the nullable Boolean sentinel. Boxing and branch consumers
  retain null, false and true distinctly (**S5.1**, **S6.1.2v2**). This removes
  `fn_le` and `is_truthy` from quicksort's partition loop without assuming that
  every index exists or removing bounds/COW checks.

### Reference audit

The C quicksort and matmul generators previously overflowed a 32-bit multiply
before taking `% 1000000`, so they used different inputs. Both now share
`bench_lcg_next`, with a `long long` intermediate; the largest intermediate
for their generator domain is below 2^41. The focused gate compares every one
of the 80,000 generated matrix inputs against exact integer arithmetic;
quicksort uses the first 5,000. C matmul also uses `floor(total)` before its
integer conversion, matching Lambda's unchanged `sum=-29562` golden. The
standard C2MIR runner's current oracle is corrected accordingly. Historical
reports retain the old values and must not be used as equivalent controls.
Other C ports sharing the old generator spelling were not qualified here.

Tak executes `(18, 12, 6)`, Ackermann `(3, 8)`, quicksort sorts 5,000 entries,
and matmul multiplies two 200×200 matrices in the original i/j/k order, then
sums in index order. Collatz tests all starts from 1 through 999,999. An exact
integer trajectory audit found maximum intermediate **56,991,483,520**, safely
inside int53, and winner **837799**, length **525**. Its C operand is explicitly
`unsigned long long`, avoiding a platform-dependent 32-bit `long`.

Optional C timers match the Lambda clock pairs. Quicksort includes setup,
sorting and the sortedness scan; matmul includes setup, multiplication and
reduction. Final success/formatting output is excluded for all five new rows.
Lambda heap allocation/COW and C stack/static storage retain their own costs;
this is a workload comparison, not an allocation-normalized kernel claim.
All five C ports pass Clang's undefined-behavior sanitizer and the historical
C2MIR timer adapter. No original Lambda benchmark source or golden was edited.

### Verification

All **18 focused groups** pass on release, covering 13 benchmark goldens and
forced/poisoned GC for all six array rows. New shift and comparison fixtures
exercise signed shifts, count boundaries, checked errors, shared-helper poison
behavior, nullable integer/float equality and order, NaN, infinity and absent
branches. All semantic fixtures run under normal, forced/poisoned and seeded/
poisoned GC. MIR assertions require native shift plus its checked sibling, and
native quicksort comparison without `fn_le` or `is_truthy`. C generator audit,
Python syntax checks and whitespace checks pass.

The required aggregate baseline completed at **8,501/8,502**, with all **279**
MIR emission checks, **350** forced-GC cases and **152** HTTP tests passing.
Its sole failure is the existing AUTO-tier `proc_mir_mvp_roots` cold-float
snapshot issue, reproduced with the pre-change release at
`80635b1fdef1613946d2c5bf44bc9eefa4d3d292`. The golden remains unchanged;
the aggregate gate is **not green**. Logs and pinned releases are under
`temp/lambda_mir_mvp/expansion/`. The final release was restored after the gate.

### Thirteen-row release comparison

[Raw samples and provenance](../../test/benchmark/lambda_mvp/expansion_2026-11-02.json)
record **60 measured runs and two warmups per engine**, with seeded interleaving,
exact non-timing output checks on every run and 10,000 paired bootstrap resamples
per ratio. The table reports medians and two-sided 95% intervals. All builds and
regression suites completed before measurement on macOS arm64. MVP and full MIR
use the same pinned release, based on revision
`80635b1fdef1613946d2c5bf44bc9eefa4d3d292`, executable SHA-256
`2a1a8cd784e5d14c95a25c1121104322b27f33ab65470738696669c5df4b97b1`.

| Benchmark | MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 0.9060 | 1.1565 | 1.0179 | 0.772–0.799× | 0.870–0.905× |
| r7rs/sum | 0.0950 | 0.2570 | 0.2585 | 0.364–0.381× | 0.360–0.375× |
| r7rs/sumfp | 0.0660 | 0.0710 | 0.0709 | 0.903–0.944× | 0.915–0.946× |
| awfy/mandelbrot | 22.5665 | 26.8680 | 21.1965 | 0.828–0.847× | 1.052–1.074× |
| larceny/array1 | 0.1150 | 1.3425 | 0.2800 | 0.085–0.087× | 0.403–0.417× |
| awfy/sieve | 0.0270 | 0.0250 | 0.0120 | 1.000–1.120× | 2.173–2.341× |
| beng/spectralnorm | 0.4530 | 1.0725 | 0.3420 | 0.417–0.437× | 1.309–1.371× |
| awfy/nbody | 1.8405 | 2.5640 | 1.4060 | 0.709–0.728× | 1.293–1.343× |
| r7rs/tak | 0.0950 | 0.1270 | 0.1150 | 0.734–0.770× | 0.811–0.852× |
| r7rs/ack | 7.5660 | 10.0375 | 11.6625 | 0.744–0.767× | 0.639–0.656× |
| larceny/quicksort | 1.0015 | 0.5665 | 0.2159 | 1.736–1.796× | 4.563–4.701× |
| kostya/matmul | 4.5305 | 18.8120 | 5.8264 | 0.233–0.244× | 0.753–0.798× |
| kostya/collatz | 499.1805 | 285.1040 | 211.6980 | 1.732–1.778× | 2.320–2.442× |

Tak, Ackermann and matmul take **25.2%, 24.6% and 75.9% less time than full MIR**,
respectively, and **17.4%, 35.1% and 22.2% less time than C2MIR**. They join the
five previously qualified rows: their one-sided 95% upper ratios are below
1.0× full MIR and 1.10× C2MIR. This brings performance qualification to **8/13**.
Quicksort and Collatz remain **1.77× and 1.75× full MIR**, and **4.64× and 2.36×
C2MIR**. Sieve, spectralnorm and nbody also remain unqualified. All 13 rows pass
coverage, but the per-row performance milestone remains open.

```sh
python3 test/benchmark/lambda_mir_mvp.py --release --subset all \
  --exe temp/lambda_mir_mvp/expansion/final-release.exe \
  --runs 60 --warmups 2 --output temp/lambda_mir_mvp/expansion/results.json
```

Use `--subset expansion` for only the five new rows. In a fresh checkout, build
with `make release` and use `--exe ./lambda.exe`. The report includes executable,
compiler source, runner, reference helper, timer and workload hashes. Compilation
and startup are excluded; these results do not establish cross-platform or
whole-language performance.

## Covered-row tuning — 2026-11-02

This follow-up keeps the same 13 Lambda workloads, goldens, C ports and timing
boundaries. It changes compiler decisions, preserving the existing runtime data
model, helpers, collector and MIR vendor code (**D1.2**, **D1.4**, **D1.6**).

### Native regions and boundaries

- **Discard demand and native destinations:** unused branch/loop results no
  longer materialize numeric null joins. A discarded direct-call result does
  not box its success lane; argument evaluation, borrow write-back and effects
  still happen. A compatible declared scalar destination consumes a native
  return directly, routing the error companion to that destination's ordinary
  failure boundary. Other fallible call consumers retain their boxed error
  join and success contract (**D8.2.6**, **D7.4.2**, **S7.7.2–S7.7.4**).
- **Finite integer regions:** closed scalar loops may guard an inferred input
  interval, then execute four ordinary iterations. Interval transfer proves
  every arithmetic operation and shift finite throughout those iterations.
  The source condition is tested on every iteration. A guard miss resumes the
  checked loop at its current state; it never repeats completed work. Unknown
  calls, nested loops and read-after-write dependencies reject the plan;
  mutually exclusive `if` arms are analyzed separately. Saturation and poison
  retain their original helper path (**S4.1.1–S4.1.2**, **S4.2**).
- **Finite predicates:** a zero comparison against remainder by a positive
  power of two uses a bit mask when the dividend is proven finite. The proof
  covers both signs and does not replace an escaping signed remainder.
  Proven finite right shifts stay in their native lane (**S4.5.4**).
- **Indexed regions:** a guarded forward traversal can establish
  `0 <= follower <= counter <= limit <= length`, with one unconditional counter
  increment and at most one follower increment after its last use. Array
  layout, writable state and bounds are checked before entering. A closed
  function with matching parameter initializers can reuse the proof through
  its final indexed operations. Calls, captures, owner replacement and other
  invalidating operations keep the checked path (**D5.4.4**, **S9.1.2–S9.1.3**).
  Quicksort's partition now uses native loads/stores in this guarded body.
- **Entry guards and borrows:** dense entries branch directly on each array's
  witness. Only the checked sibling builds persistent layout/COW metadata and
  retains borrow write-back addresses. Both siblings consume the caller's
  transient borrow cells before nested calls. Forward-only array bindings keep
  their precise root/home without rebuilding unused indexing metadata
  (**D6.3**, **S9.1.3**). This removes register spills observed in nbody's entry.
- **Finite ranges and construction:** a finite procedural `for` omits its
  unreachable range-error path. A single nonnegative conditional increment
  may use the range's maximum trip count; an enclosing loop that retains the
  accumulator rejects that proof. Typed primitive fill calls reuse an immutable
  certificate owned with the compiled contract. The existing constructor still
  checks counts/values and owns allocation/rooting (**D4.3.3**, **D6.3**,
  **S11.4.1v3**).
- **Exact numeric conversions:** bounded integer expressions divided by positive
  powers of two can remain a scaled integer while every intermediate is exactly
  representable. Proven low zero bits can eliminate the scale; conversion to
  double happens at the consumer. Rounded expressions fall back to the original
  operations; no floating reassociation or fast-math mode is used
  (**S4.4.1–S4.4.3**, **S4.5.2**). Spectralnorm's denominator needs one int-to-double
  conversion. Read-only private parameters whose incoming values all agree can
  use their proven constant; floating constants compare by bits, including the
  sign of zero (**D7.3.1**).

Boundary tests also exposed an unused-iterator crash: the shared AST index did
not publish `for` clause bindings, and the binding rewrite occurred after node
publication. The builder now rewrites that edge before indexing and the index
publishes the iterator even without an identifier use (**D8.2.4**). This fixes
the underlying binding identity rather than adding an MVP-only synthetic local.

### Qualification

The focused release gate passes **21 groups**. The new tuning fixture covers
guard misses, negative and poisoned entries, saturation after several fast
iterations, sequential writes, retained accumulators in nested loops, follower
bounds, COW snapshots, exact versus rounded fractions, signed zero, constant
parameter invalidation, floating accumulators beside integer array traversal,
and fallible native-return admission. It runs in normal,
forced/poisoned and seeded/poisoned GC modes. Structural MIR checks cover the
four native shift iterations, guarded indexed stores, native call destinations
and the exact denominator conversion. All 13 benchmark outputs pass, including
forced GC on every array row.

The required aggregate baseline again reports **8,501/8,502**, including all
**279** MIR emission cases, **350** forced-GC cases and **152** HTTP cases passing.
The sole failure remains AUTO-tier `proc_mir_mvp_roots`; current and archived
previous releases reproduce it with an unchanged golden. The gate is **not
green**. This run includes the shared AST binding fix; the final MVP-only
integer-induction admission restriction is covered by the focused release gate.

The tuning fixture also passes full MIR. A separate interpreter/AUTO comparison
exposes an existing repeated-assignment discrepancy: `sequential(2, 3)` prints
`5` instead of `67` on both current and archived releases. That finding remains
outside the explicit-JIT MVP selector; the fixture's correct golden is retained.
Controls are recorded in `temp/lambda_mir_mvp/tuning/interpreter-control.txt` and
`roots-control.txt`.

Small dense-loop unrolling regressed nbody and was removed. MIR optimization
level 3 showed no consistent benefit over level 2 and remains unchanged. A
separate diagnostic split sieve construction from its kernel (about 5 µs and
9 µs in that screen); qualification still includes both, using the unchanged
benchmark clock pair. These screens are not substitutes for paired final data.

### Paired release results

[Raw samples and provenance](../../test/benchmark/lambda_mvp/tuning_2026-11-02.json)
contain **60 measured runs and two warmups per engine**, with seeded interleaving
of current MVP, current full MIR, C2MIR and the archived previous MVP. Every
sample checks its non-timing output. Ratios use 10,000 paired bootstrap resamples;
the table shows two-sided 95% intervals. The final campaign ran after the baseline, final release rebuild and focused
checks. No builds or regression suites overlapped timing.
Current MVP and full MIR share executable SHA-256
`3861c81f513b37be75669a3c1477cd1b3b710d2e2be84c8338da9e2fdb28e46f`;
the previous MVP is the expansion release recorded above. Host: macOS arm64.

| Benchmark | MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fib | 0.9525 | 1.2115 | 1.0520 | 0.752–0.828× | 0.859–0.944× |
| r7rs/sum | 0.0975 | 0.2600 | 0.2665 | 0.346–0.386× | 0.352–0.384× |
| r7rs/sumfp | 0.0670 | 0.0740 | 0.0754 | 0.868–0.947× | 0.865–0.957× |
| awfy/mandelbrot | 23.4465 | 27.9400 | 22.3419 | 0.822–0.891× | 1.033–1.088× |
| larceny/array1 | 0.1155 | 1.3905 | 0.2999 | 0.077–0.086× | 0.360–0.395× |
| awfy/sieve | 0.0190 | 0.0250 | 0.0120 | 0.720–0.800× | 1.468–1.672× |
| beng/spectralnorm | 0.4265 | 1.1545 | 0.3558 | 0.349–0.388× | 1.118–1.268× |
| awfy/nbody | 1.7900 | 2.6855 | 1.4370 | 0.622–0.697× | 1.142–1.280× |
| r7rs/tak | 0.1020 | 0.1375 | 0.1240 | 0.708–0.800× | 0.765–0.880× |
| r7rs/ack | 7.9535 | 10.4790 | 12.0215 | 0.727–0.779× | 0.632–0.693× |
| larceny/quicksort | 0.3225 | 0.5545 | 0.2085 | 0.565–0.608× | 1.470–1.586× |
| kostya/matmul | 4.8510 | 19.0710 | 6.2360 | 0.238–0.263× | 0.748–0.804× |
| kostya/collatz | 187.6325 | 283.3355 | 214.4456 | 0.604–0.696× | 0.802–0.924× |

All **13/13** one-sided 95% upper ratios are below 1.0× full MIR. **9/13** also
meet the 1.10× C2MIR gate, retaining the previous eight and adding Collatz.
The geometric mean is **0.518× full MIR**, **0.869× C2MIR** and **0.807× previous
MVP**; these aggregates do not qualify the four remaining rows. Their median
C2MIR ratios are sieve **1.59×**, spectralnorm **1.20×**, nbody **1.25×** and
quicksort **1.55×**.

The interleaved previous-MVP control establishes these improvements without
comparing timings from different campaigns:

| Benchmark | Previous MVP, ms | Current MVP, ms | Reduction |
|---|---:|---:|---:|
| sieve | 0.0270 | 0.0190 | 29.6% |
| spectralnorm | 0.5000 | 0.4265 | 14.7% |
| nbody | 1.9230 | 1.7900 | 6.9% |
| quicksort | 1.0195 | 0.3225 | 68.4% |
| Collatz | 519.5670 | 187.6325 | 63.9% |

All five improvements have a one-sided 95% upper before/after ratio below 1.0.
The other eight before/after intervals include 1.0; no speedup is claimed for
those rows. Collatz takes **33.8% less time than full MIR** and **12.5% less than
C2MIR**. Quicksort takes **41.8% less than full MIR**, but remains unqualified
against C2MIR.

```sh
python3 test/benchmark/lambda_mir_mvp.py --release --subset all \
  --exe temp/lambda_mir_mvp/tuning/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/expansion/final-release.exe \
  --runs 60 --warmups 2 --output test/benchmark/lambda_mvp/tuning_2026-11-02.json
```

For a fresh checkout, build release and use `--exe ./lambda.exe`; omit
`--previous-mvp` when the archived release is unavailable. The measured
executable, intermediate MIR, screening data and verification logs are retained
under `temp/lambda_mir_mvp/tuning/`.

## Next bounded work

Close the remaining C2MIR gaps in sieve, spectralnorm, nbody and quicksort while
retaining all covered-row correctness and the existing performance passes.
Quicksort still has recursive call/borrow costs and poison-aware comparisons;
nbody still pays function-entry and borrow costs; spectralnorm includes heap
temporaries and Lambda formatting; sieve's short measurement includes runtime
construction. These identify further measurements, not fully attributed cost
breakdowns. Keep the shared ownership and failure behavior (**D5.4**, **D6.3**,
**S7.7**, **S9.1**) and the original workload boundaries. General language
coverage, AUTO/embedding integration and production replacement remain later
milestones.

## Fifteen more benchmarks — 2026-11-02

The `extended` subset adds **15 unchanged typed benchmark scripts**, bringing
coverage to **28**: `r7rs/{fibfp,fft,nqueens}`, `awfy/{bounce,queens}`,
`beng/{fannkuch,mandelbrot}`, `kostya/{primes,levenshtein,json_gen,brainfuck}`,
and `larceny/{divrec,paraffins,pnpoly,ray}`. Existing benchmark inputs, iteration
counts, Lambda sources and goldens are unchanged.

### Lowering and contract fixes

- **Inferred locals:** an unannotated mutable scalar initialized with a literal
  now uses the canonical scalar kind, without imposing the literal as an
  assignment restriction (**S12.2.1**). FFT exposed the defect through `wi = 0.0`:
  subsequent nonzero assignments failed and skipped the loop's progress step.
  The existing canonical-type resolver is exported from `type_contract.hpp`
  instead of duplicating its dispatch. Explicit literal contracts are retained.
- **Nullable inferred locals:** admission recognizes inferred optional primitive
  lanes as well as binary unions. Producer/effect analysis still widens an
  unproven indexed result to an owned Item; no absent read is treated as a proven
  float (**S7.1.1v3**, **D2.3.4**, **D6.2.3**). Declared nullable signatures remain
  outside the current admission boundary.
- **Sine and bitwise OR:** `math.sin` shares one native-math predicate across
  producer proofs, effects, calls and dense-body admission. Only proven float
  operands use libm; dynamic, absent and error operands use existing runtime
  classification. `bor` uses the shared guarded bitwise emitter and its boxed
  fallback, preserving finite-word saturation and errors (**S4.1.2**, **S7.9.3**).
- **Character conversion:** `ord` and `chr` use their existing boxed C ABI,
  including absence, Unicode and error behavior. No ASCII-only semantic shortcut
  or alternate string representation was introduced.
- **Dynamic local sequence writes:** boxed owners use the shared `array_set_cow`
  helper. It roots operands, detaches snapshots, permits element-kind changes,
  and returns the replacement owner. Typed primitive arrays retain checked
  contract enforcement and native guards (**S12.2.1**, **S9.1.2**, **S11.4.1v3**,
  **D5.4**, **D6.3**). Brainfuck exercises inferred tape/stack arrays and a typed
  returned jump table across repeated allocations.

No runtime data layout, GC algorithm, runtime helper implementation, vendor
source or formal ruling changed. The shared type-resolver change only exposes
an existing helper; the new decisions reside in the MVP planner/emitter.

### Reference audit and verification

All 28 C references match the original Lambda goldens. Each of the 15 added
references now uses the existing optional kernel timer with the Lambda clock
boundaries. Fannkuch, BENG Mandelbrot and Levenshtein include reporting; pnpoly
includes its result line and excludes its final `DONE` line. The remaining new
rows exclude final reporting.

Three reference corrections were necessary: JSON generation's 32-bit generator
overflow and truncating negative-coordinate conversion produced different
inputs/output; it now reuses `bench_lcg_next` and `floor`, yielding the original
Lambda length **61,626**. Queens now validates every solved board. Levenshtein
swaps its two row pointers, matching Lambda's algorithm, and reports all four
computed distances rather than a PASS marker alone. Brainfuck reports its actual
computed string and checks the whole output.

All 15 C references also pass a Clang undefined-behavior sanitizer audit. Pnpoly
requires `-ffp-contract=off` for this cross-check: native Clang contraction moves
polygon-boundary points (29,408 versus the existing MIR/Lambda golden 29,415).
The C2MIR reference already matches the golden; its evaluation order was not
changed to accommodate Clang. This is an empirical reference check, not a new
floating-point semantics ruling.

The focused release gate passes **23 groups**: all 28 Lambda and C goldens,
forced collection with poisoned freed memory for all 18 array/string-allocation
rows, existing MIR checks, and fixtures under ordinary, forced/poisoned and
seeded/poisoned GC. `mir_mvp_extended.ls` adds inferred scalar updates, nullable
float reads, sine exceptional inputs, bitwise fallback, Unicode conversion,
dynamic array kind changes, snapshot preservation and returned-array lifetime.
The FFT check reuses the original transform body with a borrowed destination,
checks the nonzero transform `[1,2,3,4] → [10,-2-2i,-2,-2+2i]`, and retains the
input snapshot. Both full MIR and MVP pass that check under forced/poisoned GC;
the timed benchmark itself retains its original zero input and plain parameter.

These are whole-workload comparisons with compilation excluded. They retain
real runtime costs: C commonly uses stack/static storage, narrower integer
arrays and mutable character buffers; Lambda uses its existing GC containers,
Unicode indexing, immutable strings and snapshot/borrow rules. In particular,
JSON generation's C buffer avoids Lambda's repeated immutable concatenation;
Brainfuck's C tape is bytes versus Lambda integer elements; Levenshtein's C
inputs are character buffers. N-Queens uses the same allocating candidate/rest
search, but C uses explicit allocation/free and shares its placed prefix.
These differences are not evidence of a code-generation-only slowdown, and
none of the reference fixes removes Lambda work from the measured score.

The required `make test-lambda-baseline` again reports **8,501/8,502**. Its sole
failure is the previously recorded AUTO-tier `proc_mir_mvp_roots`; the unchanged
MVP fixture passes under the explicit JIT selector. All **279** emission and
**350** forced-GC cases pass. The aggregate baseline remains **not green**.
The final focused release check passes **23/23** after that baseline, including
the nonzero FFT check that rejects NaN as well as numerical mismatches.

The final release was restored to `lambda.exe` after the baseline's debug
build. Its archived copy is
`temp/lambda_mir_mvp/expansion28/final-release.exe`, SHA-256
`4f93e05e4bdae20fec77fbc396b60aea4bfc6a209fd972b481566dfc21d9a844`.
Build, audit, focused-check and baseline logs are retained alongside it.

### Expanded release measurements

[All raw samples, source hashes and provenance](../../test/benchmark/lambda_mvp/extended_2026-11-02.json)
record **30 measured runs and two warmups per engine**, interleaved with seed
1101. MVP and full MIR use the same final release above; C2MIR uses the pinned
independent C driver. Every sample matches the unchanged Lambda golden. No
build, regression suite or diagnostic run overlapped this campaign.

The table shows the **15 additions**. Ratio intervals are two-sided 95% paired
bootstrap intervals from 10,000 resamples; a ratio below 1 is faster.

| Benchmark | MVP, ms | Full MIR, ms | C2MIR, ms | MVP/full MIR 95% interval | MVP/C2MIR 95% interval |
|---|---:|---:|---:|---:|---:|
| r7rs/fibfp | 0.7600 | 0.6875 | 1.1528 | 1.037–1.177× | 0.622–0.734× |
| r7rs/fft | 3.6460 | 0.0715 | 0.0190 | 47.403–52.899× | 173.843–192.249× |
| r7rs/nqueens | 1.2300 | 0.6730 | 0.3386 | 1.726–1.876× | 3.380–3.736× |
| awfy/bounce | 0.1010 | 0.0540 | 0.0215 | 1.737–2.000× | 4.401–5.144× |
| awfy/queens | 0.7935 | 0.0605 | 0.0165 | 12.150–13.895× | 44.608–50.350× |
| beng/fannkuch | 0.3760 | 0.2650 | 0.1479 | 1.357–1.509× | 2.348–2.719× |
| kostya/primes | 3.3285 | 2.2450 | 1.6329 | 1.404–1.566× | 1.940–2.183× |
| kostya/levenshtein | 27.4870 | 1.8970 | 0.7980 | 14.169–15.174× | 33.168–34.937× |
| kostya/brainfuck | 549.9640 | 85.1360 | 25.7905 | 6.353–6.608× | 20.121–22.544× |
| larceny/paraffins | 0.3100 | 0.1395 | 0.0420 | 2.179–2.422× | 7.263–8.139× |
| larceny/pnpoly | 2.6430 | 2.3825 | 1.8845 | 1.053–1.191× | 1.342–1.533× |
| larceny/ray | 0.2825 | 0.2105 | 0.1499 | 1.253–1.435× | 1.803–2.037× |
| beng/mandelbrot | 13.6210 | 13.5160 | 11.7301 | 0.989–1.070× | 1.122–1.235× |
| kostya/json_gen | 49.1805 | 6.6375 | 1.4420 | 7.160–7.682× | 32.637–35.893× |
| larceny/divrec | 3.5535 | 1.2385 | 4.9330 | 2.732–3.026× | 0.689–0.745× |

**Coverage is 28/28; performance qualification is still 9/28.** Using the existing
one-sided 95% gates, the original **13/13** rows remain faster than full MIR,
and the original nine still meet the 1.10× C2MIR target. None of the 15 additions
yet qualifies against full MIR. Fibfp and divrec beat C2MIR but are slower than
full MIR, so they do not pass the combined gate. Across all 28 rows, **13** meet
the full-MIR gate and **11** meet the C2MIR gate. The overall geometric mean is
**1.373× full MIR** and **2.385× C2MIR**; this expansion does not satisfy the
performance objective for the new population.

The largest full-MIR gaps are FFT (**51.0×**), Levenshtein (**14.5×**), Queens
(**13.1×**), JSON generation (**7.4×**) and Brainfuck (**6.5×**). Post-campaign
MIR inspection confirms boxed arithmetic/checked array-store paths in FFT,
generic indexing and admission in Levenshtein, borrowed call boundaries in
Queens, repeated string joins in JSON generation, and generic string/tape
indexing plus COW stores in Brainfuck. These identify the next optimization
sites; static call counts do not establish their share of runtime cost.

Next tuning should retain nullable native values through FFT locals, reduce
proven array and borrow checks in loops, and specialize safe string/dynamic-array
access while keeping the generic fallbacks. The new nonzero FFT, Unicode,
array-kind-change and GC fixtures must remain acceptance cases. Existing
sieve, spectralnorm, nbody and quicksort C2MIR gaps also remain open. No benchmark
source or expected output should be weakened to close these gaps.

```sh
# Reproduce coverage after a release build.
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py

# Measure just the 15 additions, or use --subset all for all 28.
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --subset extended --exe ./lambda.exe --runs 30 --warmups 2 \
  --output temp/lambda_mir_mvp/extended-results.json
```

## Twenty-eight-row tuning — 2026-11-02

This batch preserves the 28 workloads from the preceding expansion. The Lambda
sources, C references and golden-output hashes are unchanged. The runtime value
model and container layouts are unchanged (**D2.3.4–D2.3.5**). C2MIR remains an
external benchmark reference, not an execution back end.

### Lowering changes

- Nullable int/float producers retain native lanes through arithmetic and
  inferred locals. Optional-to-optional boundary containment uses both arms of
  the shared contract (**S10.1.1v4**); missing indices retain absence
  (**S7.1.1v3**). Storage eligibility is now independent of conservative index
  bounds: an overestimated extent no longer disables every native access.
- Native integer speculation guards the actual Item tag and finite domain.
  Dynamic arrays similarly guard their current storage before loading/storing;
  kind changes, snapshots and errors keep the runtime fallback. Integer `abs`
  reuses the shared sentinel-preserving emitter (**S4.1**, **S9.1.2**).
- Source range queries recognize squared and positive-factor loop bounds.
  The existing four-lane array reduction also counts packed booleans and handles
  inclusive limits. Guards retain short arrays, negative starts, nonzero initial
  accumulators and unsafe trip counts. These are transient queries over the
  shared AST, not another node-fact graph (**D8.2.5**).
- Read-only array calls use the shared mutation/retention analysis. A dead
  scratch in a complete local owner permutation permits root moves without
  creating snapshots. Full MIR and MVP share the AST recognizer. The MVP uses
  the same invariant-array admission proof as ordinary assignment; an observed
  scratch still captures a snapshot (**S9.1.2**, **D6.3.1**).
- ASCII `ord(s[i])` and indexed character equality avoid temporary strings;
  Unicode, embedded NUL and missing characters retain their semantics. String
  concatenation flattens source-order leaves and simple string-returning
  helpers. Every actual argument, including unused arguments, is evaluated
  once in order. Ordinary string reads freeze append buffers before aliases
  escape; only assignment consumes its existing prefix (**S9.1.2**).
- Bounded inlining now carries collecting calls, primitive array parameters
  and native error results. Fresh locals, storage witnesses and precise roots
  belong to each expansion; an inlined failure joins at its call boundary.
  Array contracts carried as Item remain eligible when their container domain
  is proven (**D6.3**, **D7.4.2**). Successful native call results can flow into
  array stores while failed calls retain the checked error path.
- Scalar self-tail calls evaluate arguments before simultaneous parameter
  replacement. Other recursion retains stack checks. Positive integral float
  countdowns with zero accumulators use an exact integer triangular sum only
  inside the proved exact domain; fractional inputs, signed zero, nonzero
  accumulators and nonfinite values retain the original loop (**S4.3**).

### Release measurements

[Raw paired report](../../test/benchmark/lambda_mvp/tune28_2026-11-02.json):
30 samples after two warmups per engine and workload, randomized interleaving
with seed 1101. Every sample is checked against its original golden. Full MIR
and MVP use the same release executable. The prior MVP binary is also measured
in the same campaign; the previous column below is this interleaved control,
not a historical median copied from another run. Compilation is excluded.
The report retains source, runner, timer and executable hashes.

Release SHA-256: `551ed8e84cb10f8f13ef4d077df3a6a458d411f0cf54772f46c0064c3191b18a`.

Times are median milliseconds. U95 is the one-sided 95% paired-bootstrap upper
ratio bound (10,000 resamples). Qualification requires **U95 MVP/Full < 1.00**
and **U95 MVP/C2MIR <= 1.10** for each row.

| Workload | Previous MVP | MVP | Full MIR | C2MIR | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.9635 | 0.9450 | 1.1735 | 1.0533 | 0.824× | 0.940× | yes |
| r7rs/sum | 0.0975 | 0.0970 | 0.2720 | 0.2640 | 0.373× | 0.384× | yes |
| r7rs/sumfp | 0.0710 | below resolution | 0.0760 | 0.0740 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 23.5440 | 23.7735 | 28.1965 | 22.5906 | 0.859× | 1.073× | yes |
| larceny/array1 | 0.1115 | 0.1125 | 1.3205 | 0.2899 | 0.089× | 0.412× | yes |
| awfy/sieve | 0.0190 | 0.0215 | 0.0275 | 0.0122 | 0.815× | 1.839× | no |
| beng/spectralnorm | 0.4365 | 0.4260 | 1.2210 | 0.3705 | 0.360× | 1.188× | no |
| awfy/nbody | 1.7515 | 1.7545 | 2.6900 | 1.4515 | 0.681× | 1.242× | no |
| r7rs/tak | 0.1000 | 0.0865 | 0.1320 | 0.1165 | 0.680× | 0.776× | yes |
| r7rs/ack | 8.1085 | 6.6100 | 10.4920 | 12.4026 | 0.648× | 0.552× | yes |
| larceny/quicksort | 0.3455 | 0.3535 | 0.5910 | 0.2310 | 0.622× | 1.590× | no |
| kostya/matmul | 4.5935 | 4.4530 | 19.6035 | 5.8911 | 0.248× | 0.809× | yes |
| kostya/collatz | 192.9270 | 190.3915 | 300.4945 | 226.2104 | 0.681× | 0.908× | yes |
| r7rs/fibfp | 0.7485 | 0.7645 | 0.6910 | 1.1106 | 1.171× | 0.729× | no |
| r7rs/fft | 3.6115 | 0.1630 | 0.0770 | 0.0200 | 2.213× | 8.967× | no |
| r7rs/nqueens | 1.3030 | 1.2650 | 0.7185 | 0.3595 | 1.892× | 3.760× | no |
| awfy/bounce | 0.0980 | 0.0640 | 0.0540 | 0.0205 | 1.241× | 3.247× | no |
| awfy/queens | 0.7740 | 0.1030 | 0.0600 | 0.0161 | 1.767× | 6.805× | no |
| beng/fannkuch | 0.3830 | 0.3565 | 0.2600 | 0.1450 | 1.455× | 2.620× | no |
| kostya/primes | 3.4115 | 1.4030 | 2.3210 | 1.6740 | 0.622× | 0.874× | yes |
| kostya/levenshtein | 27.2675 | 2.3090 | 1.7995 | 0.8009 | 1.320× | 2.936× | no |
| kostya/brainfuck | 546.1845 | 92.8070 | 86.6030 | 27.4375 | 1.085× | 3.653× | no |
| larceny/paraffins | 0.3105 | 0.2840 | 0.1360 | 0.0404 | 2.159× | 7.431× | no |
| larceny/pnpoly | 2.5565 | 2.3340 | 2.3530 | 1.7969 | 1.052× | 1.339× | no |
| larceny/ray | 0.3020 | 0.2365 | 0.2175 | 0.1479 | 1.117× | 1.661× | no |
| beng/mandelbrot | 13.7775 | 13.5280 | 12.9810 | 11.7760 | 1.064× | 1.211× | no |
| kostya/json_gen | 49.8420 | 4.5375 | 6.4470 | 1.3630 | 0.740× | 3.529× | no |
| larceny/divrec | 3.7840 | 0.9425 | 1.3065 | 5.0220 | 0.752× | 0.193× | yes |

**Coverage remains 28/28. Performance qualification is 15/28 against Full MIR,
11/28 against C2MIR, and 10/28 against both. The requested 28/28 milestone is
not achieved.** Primes now passes both gates; JSON generation and divrec now
beat Full MIR. Several earlier bottlenecks improved substantially: FFT about
22×, Levenshtein 12×, JSON generation 11×, Queens 7.5× and Brainfuck 5.9× versus
the paired prior MVP. Improvement alone does not qualify these rows.

Sumfp's exact countdown reduction makes many original clock samples zero.
These are retained as resolution-limited observations, not interpreted as zero
runtime or floored to an invented duration. The runner withholds its confidence
bounds, qualification and aggregate geometric means. A longer matched timing
experiment is needed to qualify this row. This is why the earlier sumfp pass is
not counted in the current totals.

The remaining large C2MIR gaps include FFT, Queens, paraffins, Brainfuck,
Levenshtein and JSON generation. Sieve, spectralnorm, nbody and quicksort still
miss the C2MIR allowance despite beating Full MIR. Fibfp beats C2MIR but remains
slower than Full MIR; pnpoly's lower median does not establish a confidence-bound
win. These remain optimization work, not exceptions to the performance gates.

### Verification

The final release passes **24/24 focused test groups**, including all 28 original
benchmark goldens, all 18 allocation-bearing workloads under forced GC with
freed-memory poisoning, and the normal/forced/seeded ownership fixtures.
`mir_mvp_consumers.ls` checks nullable arithmetic, character semantics, string
aliasing and argument order, dynamic array kind changes, scalar tail-call
argument permutation, inlined failures, owner swaps and boolean reductions.
MIR assertions verify that array accessors inline, reductions unroll, and dead
owner cycles avoid extra captures. No benchmark output or source was weakened.

The final `make test-lambda-baseline` rerun reports **8,501/8,502**, including
**279/279** MIR emission tests and **350/350** MIR GC stress tests. Its only
failure remains the previously recorded AUTO-tier `proc_mir_mvp_roots`
cold-float snapshot issue. It is not an MVP regression, and the gate is not
reported as fully passing. Logs are retained under
`temp/lambda_mir_mvp/tune28/` (`focused23.log`, `paired23.log`,
`baseline-final.log`). The measured release executable was restored after
the baseline replaced `lambda.exe` with a debug build.

```sh
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --subset all --exe ./lambda.exe --runs 30 --warmups 2 \
  --output temp/lambda_mir_mvp/tune28-results.json
```


## Follow-up tuning of the same 28 rows — 2026-11-02

This pass keeps the benchmark sources, C ports, expected outputs, runtime types
and storage layouts unchanged (**D2.3.4–D2.3.5**). It continues to use the shared
AST and physical emitter, with no production lowering or benchmark-specific
replacement kernels (**D8.2.5**).

### Changes and proof boundaries

- Borrowed array accessors can inline, including discarded Item returns.
  Nested expansions reserve disjoint slices of the caller's precise argument
  root span; replacement owners propagate through the same write-back cells.
  Actual arguments are evaluated before publication (**D6.3.1**, **D6.3.5**,
  **D7.4.2**). Existing mutation/retention analysis also recognizes pure builtin
  actuals that cannot mutate an earlier argument, while retaining their GC roots.
- Array iterations guard storage, writability and every distinct index before
  effects. Existing single-store loops and lone accesses whose presence is
  already proven keep their own checks; duplicating these checks regressed
  Primes in the first campaign. Uncertain reads can still benefit by discharging
  nullable consumers, as in spectralnorm. Pure unconditional index initializers
  may move before the guards;
  a guard miss executes the checked iteration exactly once. Shared eligibility
  queries reject captures, owner replacement and collecting/reentrant calls.
  Character comparison retains its noncollecting Unicode/absence fallback
  (**S7.1.1v3**, **S9.1.2**, **D5.4.4**, **D6.3.2**).
- A loop repeatedly calling one closed numeric array procedure can guard its
  owners and complete extents once, then inline the native kernel. The checked
  call remains for short, shared or incompatible storage. No allocation,
  capture or owner assignment is allowed to invalidate the guarded expansion.
  The emitter verifies that no collecting call escaped the proof. This removes
  repeated entry checks and borrow-cell traffic in NBody and pnpoly.
- Guarded array-copy loops check their complete extents once and preserve
  forward load/store order, including overlapping ranges of the same owner.
  Shared owners, absence and failed stores retain their checked path and
  partial state (**S9.1.2**, **S7.1.1v3**, **S7.7.2–S7.7.3**).
- Finite integer queries cover positive geometric counters, contracting integer
  division and additive index chains. Ordered finite guards permit native
  double-to-int conversion through the helper now shared with Full MIR;
  nonfinite/out-of-band values retain the canonical conversion. Existing
  finite-Item guards also discharge redundant poison tests (**S4.1**, **S4.2**).
- Very small noncollecting scalar recursion permits two bounded inline
  expansions; real recursive activations retain stack checks. Larger
  noncollecting scalar helpers receive a 192-node per-callee limit within the
  existing total budget. Inlined array accessors reuse caller storage witnesses
  only when capture-free numeric bodies preserve them (**D5.4.4**).
- Consecutive discarded equality branches on one unchanged native integer local
  dispatch to at most one arm when their finite literal keys are distinct.
  Duplicate keys, selector mutation and consumed branch values keep their
  original evaluation. This reduces repeated opcode tests in Brainfuck.
- Shared primitive fill uses bulk zeroing for zero numeric lanes, preserving
  negative-zero bits through the ordinary fill path. Integer-to-string conversion
  reuses the existing fixed-buffer renderer before allocating a temporary
  `StrBuf` for its uncommon fallback (**S4.3**). Full MIR receives these helper
  improvements too; its control executable is the same final release as MVP.


### Final release comparison

[Raw paired report](../../test/benchmark/lambda_mvp/tune28_followup_2026-11-02.json):
30 measured samples after two warmups per engine and workload, randomized
interleaving with seed 1101. Every output matches its original golden. The
previous MVP column is commit `ca3cadb47`'s pinned release measured in this same
campaign. Full MIR and current MVP use the same final executable, including the
shared helper improvements.

These are **self-reported internal execution times, not end-to-end times**.
Parsing, JIT compilation, process startup and teardown are excluded. The same
per-row clock boundaries remain in place; five rows also include formatting and
output. The original Lambda/C/golden hashes are unchanged for all 28 rows.

Release SHA-256: `6b35837935c8d66e676c227c49d5dfc3e874547ac42b6df4b079ca4d059c0a98`.

Times below are median milliseconds. U95 is the one-sided 95% paired-bootstrap
upper ratio bound from 10,000 resamples. Qualification still requires
**U95 MVP/Full < 1.00** and **U95 MVP/C2MIR <= 1.10**.

| Workload | Previous MVP | MVP | Full MIR | C2MIR | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.9395 | 0.8165 | 1.2065 | 1.0594 | 0.697× | 0.790× | yes |
| r7rs/sum | 0.1010 | 0.0990 | 0.2745 | 0.2820 | 0.379× | 0.374× | yes |
| r7rs/sumfp | below resolution | below resolution | 0.0725 | 0.0719 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 23.8390 | 24.1670 | 28.6220 | 22.6444 | 0.853× | 1.124× | no |
| larceny/array1 | 0.1085 | 0.1100 | 1.3385 | 0.2841 | 0.086× | 0.398× | yes |
| awfy/sieve | 0.0220 | 0.0220 | 0.0260 | 0.0122 | 0.880× | 1.839× | no |
| beng/spectralnorm | 0.4290 | 0.3315 | 1.2580 | 0.3639 | 0.275× | 0.960× | yes |
| awfy/nbody | 1.7195 | 1.4315 | 2.7145 | 1.5134 | 0.553× | 0.981× | yes |
| r7rs/tak | 0.0890 | 0.0860 | 0.1310 | 0.1166 | 0.695× | 0.794× | yes |
| r7rs/ack | 6.5380 | 6.4220 | 10.6455 | 12.3351 | 0.636× | 0.543× | yes |
| larceny/quicksort | 0.3555 | 0.3160 | 0.5905 | 0.2310 | 0.556× | 1.438× | no |
| kostya/matmul | 4.6545 | 4.6570 | 19.3805 | 6.1075 | 0.252× | 0.807× | yes |
| kostya/collatz | 183.4200 | 196.9875 | 301.1715 | 221.4934 | 0.672× | 0.936× | yes |
| r7rs/fibfp | 0.7215 | 0.5625 | 0.6405 | 1.0980 | 0.916× | 0.547× | yes |
| r7rs/fft | 0.1585 | 0.0490 | 0.0760 | 0.0200 | 0.662× | 2.573× | no |
| r7rs/nqueens | 1.2995 | 1.2730 | 0.7490 | 0.3660 | 1.768× | 3.622× | no |
| awfy/bounce | 0.0635 | 0.0570 | 0.0565 | 0.0205 | 1.077× | 2.897× | no |
| awfy/queens | 0.0965 | 0.0760 | 0.0615 | 0.0151 | 1.350× | 5.351× | no |
| beng/fannkuch | 0.3845 | 0.3080 | 0.2785 | 0.1531 | 1.150× | 2.078× | no |
| kostya/primes | 1.3540 | 1.3855 | 2.3150 | 1.5865 | 0.637× | 0.891× | yes |
| kostya/levenshtein | 2.3515 | 1.4505 | 1.8860 | 0.8025 | 0.790× | 1.851× | no |
| kostya/brainfuck | 93.5315 | 65.6700 | 77.6410 | 27.2086 | 0.883× | 2.547× | no |
| larceny/paraffins | 0.2775 | 0.2690 | 0.1310 | 0.0383 | 2.115× | 7.249× | no |
| larceny/pnpoly | 2.3825 | 1.9050 | 2.4370 | 1.8895 | 0.807× | 1.030× | yes |
| larceny/ray | 0.2200 | 0.1795 | 0.2045 | 0.1425 | 0.923× | 1.335× | no |
| beng/mandelbrot | 13.5745 | 13.5850 | 13.2065 | 12.1274 | 1.047× | 1.193× | no |
| kostya/json_gen | 4.8195 | 3.0870 | 4.8940 | 1.4126 | 0.654× | 2.225× | no |
| larceny/divrec | 0.9375 | 0.9310 | 1.2380 | 5.1215 | 0.781× | 0.187× | yes |

**Coverage remains 28/28. Qualification is 21/28 against Full MIR, 13/28 against
C2MIR and 13/28 against both. The requested all-28 performance target is not
achieved.** Compared with the previous report, six additional rows beat Full
MIR: fibfp, FFT, Levenshtein, Brainfuck, pnpoly and ray. Spectralnorm, NBody,
fibfp and pnpoly now pass both gates. AWFY mandelbrot's median is within the
C2MIR allowance, but its upper confidence bound misses it in this campaign;
its earlier pass is not carried forward.

Against the interleaved prior MVP, FFT is about 69% faster, Levenshtein 38%,
JSON generation 36%, Brainfuck 30%, spectralnorm 23%, fibfp 22%, Queens 21%,
pnpoly 20% and NBody 17%. These improvements do not qualify the remaining
C2MIR gaps. NQueens, Bounce, Queens, Fannkuch, paraffins and BENG mandelbrot
still fail the Full MIR gate. Sumfp remains below clock resolution, with its
samples retained and qualification withheld; it needs longer matched timing.

The largest remaining C2MIR gaps are paraffins (7.0× median), Queens (5.0×),
NQueens (3.5×), Bounce (2.8×), FFT (2.4×), Brainfuck (2.4×), JSON generation
(2.2×), Fannkuch (2.0×) and Levenshtein (1.8×). Array allocation/copying,
recursive array-call transport, checked integer consumers and string-building
allocation remain candidates for further emitted-code and runtime investigation.

The first follow-up campaign exposed redundant guards in Primes. Restricting
iteration versioning to accesses that can discharge checks restores its previous
kernel MIR, apart from runtime addresses. An intermediate restriction on every
single read also removed spectralnorm's useful nullable-consumer specialization;
the final restriction applies only when presence is already proven. Intermediate
reports/binaries are retained under `temp/lambda_mir_mvp/tune28b/`; only the final
source-matched campaign above supplies the qualification totals.

### Follow-up verification

The final release passes **26/26 focused test groups**, including all 28 benchmark
goldens and the existing forced/poisoned-GC workloads. Four new fixtures, each
with a golden, run normally and under forced and seeded collection:

- `mir_mvp_borrow_inline`: nested borrowed replacements, sibling owner cells,
  snapshots, allocations and failure after mutation.
- `mir_mvp_iterations`: array-iteration misses, changing indices, geometric and
  contracting counters, signed zero, integer rendering/conversion boundaries,
  duplicate dispatch keys and selector mutation.
- `mir_mvp_kernel_loops`: repeated closed kernels, short/shared owners,
  zero-trip loops, scalar results and owner replacement.
- `mir_mvp_copy_loops`: distinct and overlapping buffers, boolean storage,
  negative/short extents, partial state on failure and final counter values.

MIR assertions retain checked siblings, verify native guarded bodies, inspect
borrowed expansions and confirm bounded scalar recursive expansion. Build and
focused logs are `build23.log` and `focused23.log`; the final timing log is
`paired23.log`, all under `temp/lambda_mir_mvp/tune28b/`.


The required `make test-lambda-baseline` gate reports **8,501/8,502**, including
**279/279** MIR emission and **350/350** MIR GC-stress tests. Its sole failure is
the same previously recorded AUTO-tier `proc_mir_mvp_roots` cold-float snapshot
mismatch (last line `false`, expected `true`). No golden or assertion was weakened;
the aggregate gate is not reported as fully passing. Logs are
`temp/lambda_mir_mvp/tune28b/baseline-final.log` and
`baseline-script-details.log`. The pinned measured release was restored after
the baseline's debug build. The final report's compiler, runner, timer and binary
hashes were rechecked, and `git diff --check` passes.

Reproduce the focused gate and paired experiment from a release build:

```sh
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28b/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28/final-release.exe \
  --runs 30 --warmups 2 --output temp/lambda_mir_mvp/repeated-followup.json
```


## Region and predicate tuning of the same 28 rows — 2026-11-02

This pass extends the existing MVP planner and emitter. Runtime types, layouts
and runtime helpers are unchanged (**D2.3.4–D2.3.5**). Proofs use the shared AST
and transient binding intervals, with no additional node-type graph
(**D8.2.5**).

### Implementation

- Whole numeric loops can guard array storage, writability and complete index
  extents once. The shared closed-region query excludes capture, replacement,
  reentry and collecting calls; emission checks the collecting-call count.
  Checked siblings preserve shared owners and failed accesses (**D5.4.4**,
  **S9.1.2**, **S7.1.1v3**). This removes repeated guards from Primes and Bounce.
- Counter bounds include positive scaled counters with nonnegative finite
  offsets. Branch predicates refine unchanged, already-finite integer locals
  inside their selected arms. Writes invalidate the refinement, and ambiguous
  or contradictory intervals remain conservative (**S4.1.1–S4.1.2**).
- Small pure arithmetic expansions can guard their actual arguments once and
  prove every intermediate fits the native integer band. The radius search is
  shared with guarded numeric loops. Out-of-band operands retain the original
  checked expression. Alternative guarded bodies receive independent inline
  budgets so emitting a hot version does not starve its checked sibling.
- Reversal loops guard both endpoints and writable storage before entering a
  native inward swap loop. The original loop handles misses; final indices,
  forward statement order, snapshots, poison and floating-point bit patterns
  are preserved (**S9.1.2**, **S4.2.1**, **S4.3**).
- Boolean demand emits direct comparison branches when native contracts justify
  them. Ordered floating predicates keep their NaN behavior; only equality and
  inequality can be complemented directly. Power-of-two remainder tests reuse
  the existing bit-test lowering (**S4.2.1**, **S6.1.2v2**).
- A finite equality operand eliminates redundant integer NaN masks. A nonzero
  finite multiplier plus checked machine overflow and a finite result also
  excludes sentinel inputs; zero multiplication keeps its poison check
  (**S4.1.2**, **S4.2.1**).
- Consecutive discarded conjunctions of equality/inequality predicates can
  share an exit when opposite literals prove them pairwise exclusive and no
  arm writes their operands. This avoids evaluating the remaining pure
  predicates after a match in paraffins.

### Measured qualification

[Raw paired report](../../test/benchmark/lambda_mvp/tune28_regions_2026-11-02.json):
30 measured samples after two warmups, with all four engines interleaved using
seed 1101. Previous MVP is commit `1e3ca619d`'s pinned release from the preceding
campaign, measured again alongside the candidate. Full MIR uses the same
executable as the candidate. All 28 original Lambda sources, C ports and goldens
have unchanged hashes, and every measured output matches its golden.

Times are **self-reported internal execution milliseconds, not end-to-end
latency**. The existing per-row boundaries are retained, including formatting
and output in five rows; compilation and process startup/teardown are excluded.
U95 is the one-sided 95% paired-bootstrap upper bound from 10,000 resamples.
The gates remain **U95 MVP/Full < 1.00** and **U95 MVP/C2MIR <= 1.10**.

Release SHA-256: `71fdcfa5d81f8c42e27cea686800980903a5aec5e556ec7b56678284b935d0ca`.

| Workload | Previous MVP | MVP | Full MIR | C2MIR | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.8260 | 0.8245 | 1.2350 | 1.0339 | 0.684× | 0.807× | yes |
| r7rs/sum | 0.0950 | 0.0950 | 0.2570 | 0.2551 | 0.373× | 0.376× | yes |
| r7rs/sumfp | below resolution | below resolution | 0.0730 | 0.0754 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 23.2980 | 21.9645 | 28.0320 | 22.2456 | 0.791× | 1.004× | yes |
| larceny/array1 | 0.1145 | 0.1175 | 1.4410 | 0.3044 | 0.086× | 0.397× | yes |
| awfy/sieve | 0.0215 | 0.0210 | 0.0250 | 0.0129 | 0.880× | 1.755× | no |
| beng/spectralnorm | 0.3320 | 0.3290 | 1.2140 | 0.3695 | 0.281× | 0.916× | yes |
| awfy/nbody | 1.4975 | 1.5000 | 2.7360 | 1.5085 | 0.558× | 1.010× | yes |
| r7rs/tak | 0.0860 | 0.0895 | 0.1400 | 0.1210 | 0.655× | 0.760× | yes |
| r7rs/ack | 6.4460 | 6.1100 | 10.4455 | 12.1119 | 0.594× | 0.516× | yes |
| larceny/quicksort | 0.3210 | 0.3000 | 0.6000 | 0.2225 | 0.525× | 1.393× | no |
| kostya/matmul | 4.8625 | 4.7110 | 20.0480 | 6.2789 | 0.237× | 0.759× | yes |
| kostya/collatz | 197.0760 | 179.1890 | 304.6575 | 226.9156 | 0.609× | 0.797× | yes |
| r7rs/fibfp | 0.5755 | 0.5545 | 0.6785 | 1.1250 | 0.885× | 0.529× | yes |
| r7rs/fft | 0.0470 | 0.0480 | 0.0735 | 0.0190 | 0.676× | 2.573× | no |
| r7rs/nqueens | 1.3020 | 1.2945 | 0.7570 | 0.3580 | 1.757× | 3.845× | no |
| awfy/bounce | 0.0605 | 0.0520 | 0.0580 | 0.0225 | 0.946× | 2.458× | no |
| awfy/queens | 0.0780 | 0.0750 | 0.0590 | 0.0156 | 1.321× | 5.036× | no |
| beng/fannkuch | 0.3135 | 0.2600 | 0.2820 | 0.1555 | 0.935× | 1.686× | no |
| kostya/primes | 1.3890 | 0.9975 | 2.3475 | 1.7015 | 0.441× | 0.603× | yes |
| kostya/levenshtein | 1.4890 | 1.4855 | 1.8835 | 0.8096 | 0.809× | 1.889× | no |
| kostya/brainfuck | 65.8975 | 65.4790 | 80.1915 | 27.2415 | 0.824× | 2.462× | no |
| larceny/paraffins | 0.2850 | 0.2070 | 0.1365 | 0.0405 | 1.622× | 5.409× | no |
| larceny/pnpoly | 1.8955 | 1.9000 | 2.4125 | 1.8765 | 0.809× | 1.028× | yes |
| larceny/ray | 0.1935 | 0.1225 | 0.2090 | 0.1505 | 0.620× | 0.845× | yes |
| beng/mandelbrot | 13.6200 | 12.9935 | 13.3945 | 11.9885 | 0.980× | 1.097× | yes |
| kostya/json_gen | 2.9465 | 3.0715 | 4.5780 | 1.3409 | 0.697× | 2.376× | no |
| larceny/divrec | 0.9940 | 0.9615 | 1.2910 | 5.0585 | 0.777× | 0.197× | yes |

**Coverage remains 28/28. Qualification is 24/28 against Full MIR and 16/28
against C2MIR and both gates. The requested all-28 performance target remains
unmet.** Bounce, Fannkuch and BENG Mandelbrot newly pass the Full MIR gate.
AWFY Mandelbrot, Ray and BENG Mandelbrot newly pass both gates relative to the
preceding report. NQueens, Queens and paraffins still miss Full MIR; sumfp is
below clock resolution and remains unqualified. Its zero observations are
retained, with no invented timing floor or all-row geometric mean.

Against the interleaved previous MVP, median execution improves by about 37%
for Ray, 28% for Primes, 27% for paraffins, 17% for Fannkuch, 14% for Bounce and
9% for Collatz. Not every row improves: JSON generation and Tak have medians
about 4% higher in this campaign; their before/after confidence intervals include
parity. Both still beat Full MIR, while JSON remains outside the C2MIR allowance.
BENG Mandelbrot passes the C2MIR gate narrowly (U95 1.097×), so the margin should
not be overstated. The largest remaining C2MIR gaps are paraffins (5.1× median),
Queens (4.8×), NQueens (3.6×), FFT (2.5×), Brainfuck (2.4×), Bounce (2.3×) and
JSON generation (2.3×).

### Verification

The release passes **26/26 focused test groups**, including all benchmark goldens
and forced/poisoned GC. The new `mir_mvp_region_loops.ls` fixture and its golden
cover whole-loop guards, shared/short owners, zero-trip loops, replacement,
affine bounds, conditional bounds invalidated by writes, arithmetic guard
misses, reversal ranges and final counters, NaN/signed-zero preservation,
exclusive predicates with mutated operands, and direct floating branches.
It runs normally and under forced and seeded collection.

Direct-branch validation caught and corrected an unsupported floating branch
inversion and preservation of the existing modulo bit-test path. A newly written
negative-index fixture initially used a failed typed declaration inside a loop,
which skipped its counter updates and repeated. Its inferred temporary now
exercises the intended checked store failure, with the expected output verified
against Full MIR. No production golden or existing assertion was weakened.

The required `make test-lambda-baseline` gate reports **8,501/8,502**, including
**279/279** MIR emission and **350/350** MIR GC-stress tests. Its sole failure is
the previously recorded AUTO-tier `proc_mir_mvp_roots` cold-float snapshot
mismatch (last line `false`, expected `true`). No golden or assertion was
weakened; the aggregate gate is not fully passing. Logs are
`temp/lambda_mir_mvp/tune28c/baseline-final.log` and
`baseline-script-details.log`. The final region fixture also matches its golden
under Full MIR. The pinned measured release was restored after the baseline's
debug build, and its SHA-256 matches the benchmark report.

Build, focused and timing logs are `build9.log`, `focused9.log` and `paired9.log`
under `temp/lambda_mir_mvp/tune28c/`. The measured release is pinned there as
`final-release.exe`; `start-release.exe` is the previous MVP. Benchmark-source,
compiler-source, timer and runner hashes match the report. Reproduce with:

```sh
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28c/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28c/start-release.exe \
  --runs 30 --warmups 2 --output temp/lambda_mir_mvp/repeated-regions.json
```


## Recursive ownership and string construction tuning — 2026-11-02

This pass keeps the same 28 workloads, runtime types, container layouts and
runtime helpers (**D2.3.4–D2.3.5**). It extends the MVP's transient analyses of
the shared AST and existing direct-call ABI (**D8.2.5**, **D7.4.2**).

### Implementation

- Planner and emitter share the existing static boundary proof, including
  invariant primitive-array contracts. Integer-key primitive reads are
  classified as member-or-absence; consuming typed boundaries still retain
  their error paths (**S7.1.1v3**, **S9.1.3**).
- A bounded traversal of indexed binding uses and function write references
  closes recursive read-only forwarding without treating every call edge as
  an escape. Writes, captures, returned arrays and simultaneous writer aliases
  retain snapshot handling (**S9.1.2**, **S9.1.3**). This removes repeated COW
  detachment from paraffins' read-only helpers.
- Inlined `var` calls carry replacement owners in precise rooted registers and
  publish them at the common exit, including partial-error exits. Real calls
  keep their rooted borrow cells. A separate closed-use proof preserves
  length, lane and conservative writability witnesses across indexed writes;
  replacement, snapshot capture and escaping uses invalidate it
  (**D6.3.1**, **D5.4.4**).
- Integer parameter ranges start at private external callers and must reach
  a fixed point over every self edge. A preceding sole-return guard refines
  an immutable parameter on the continuing path. Overflowing or unresolved
  recurrences keep checked arithmetic (**S4.1.1–S4.1.2**).
- Closed array bodies guard physical storage, complete access extents and
  writable owners before using native accesses. Forwarded accessor requirements
  contribute to the entry guards. Recursive calls must forward the same
  owners; only fixed access extents are inherited. Leaf accessors are proved
  before their error effects are narrowed within the guarded body. One bounded
  recursive expansion amortizes entry checks, while real self calls retain
  stack recovery (**D5.4.4**, **S7.11.1v2**). Buffer pointers are carried only
  through noncollecting expansions; emitted collecting calls reject the proof.
- An adjacent, single-use string constructor can contribute its pieces directly
  to an append. The AST index's declaration identifier is excluded from read
  counting. Argument order, aliases and multiple-use temporaries remain
  observable as before (**S9.1.2**). JSON's 25-part object now joins its owner
  in one 26-part append.

A separate private native entry was tested and removed because it did not
improve the paired release screen. A broader tail-call experiment was also
removed after slowing NQueens. Neither experiment is in the final compiler.

### Measured qualification

[Raw paired report](../../test/benchmark/lambda_mvp/tune28_recursive_2026-11-02.json):
30 measured samples after two warmups, with candidate MVP, previous MVP,
Full MIR and the independent C2MIR C ports interleaved using seed 1101.
Previous MVP is commit `0865507b1`'s pinned release, measured again in this
campaign. Full MIR uses the candidate executable. Every measured output matches
its golden; all original Lambda sources, C ports and goldens retain their hashes.

Times are **self-reported internal execution milliseconds, not end-to-end
latency**. Timer boundaries are unchanged, including the five rows that time
formatting/output. Startup and compilation are excluded. U95 is the one-sided
95% paired-bootstrap upper bound from 10,000 resamples. The gates remain
**U95 MVP/Full < 1.00** and **U95 MVP/C2MIR <= 1.10**.

Release SHA-256: `009b97150f050aaf439fec067d0aa155539897d60e699f6cfabf1bc57523e65e`.

| Workload | Previous MVP | MVP | Full MIR | C2MIR | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.7780 | 0.5955 | 1.1615 | 1.0259 | 0.521× | 0.597× | yes |
| r7rs/sum | 0.0950 | 0.0950 | 0.2585 | 0.2551 | 0.373× | 0.372× | yes |
| r7rs/sumfp | below resolution | below resolution | 0.0735 | 0.0720 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 21.0905 | 21.2900 | 26.5565 | 21.3954 | 0.817× | 1.019× | yes |
| larceny/array1 | 0.1100 | 0.1105 | 1.3200 | 0.2889 | 0.086× | 0.394× | yes |
| awfy/sieve | 0.0210 | 0.0200 | 0.0260 | 0.0120 | 0.800× | 1.672× | no |
| beng/spectralnorm | 0.3075 | 0.3070 | 1.1280 | 0.3335 | 0.287× | 0.960× | yes |
| awfy/nbody | 1.4620 | 1.4160 | 2.6720 | 1.3781 | 0.568× | 1.081× | yes |
| r7rs/tak | 0.0825 | 0.0850 | 0.1280 | 0.1135 | 0.701× | 0.787× | yes |
| r7rs/ack | 5.7285 | 5.3405 | 9.8480 | 11.5996 | 0.545× | 0.464× | yes |
| larceny/quicksort | 0.2710 | 0.2685 | 0.5650 | 0.2070 | 0.493× | 1.327× | no |
| kostya/matmul | 4.7395 | 4.6925 | 19.9130 | 6.2201 | 0.239× | 0.762× | yes |
| kostya/collatz | 177.3840 | 177.7615 | 302.9570 | 225.6686 | 0.595× | 0.799× | yes |
| r7rs/fibfp | 0.5840 | 0.5935 | 0.6775 | 1.1309 | 0.891× | 0.548× | yes |
| r7rs/fft | 0.0460 | 0.0470 | 0.0730 | 0.0184 | 0.685× | 2.660× | no |
| r7rs/nqueens | 1.2920 | 1.2475 | 0.7410 | 0.3674 | 1.718× | 3.477× | no |
| awfy/bounce | 0.0520 | 0.0570 | 0.0600 | 0.0215 | 0.983× | 2.762× | no |
| awfy/queens | 0.0780 | 0.0295 | 0.0595 | 0.0151 | 0.541× | 2.082× | no |
| beng/fannkuch | 0.2615 | 0.2570 | 0.2780 | 0.1589 | 0.955× | 1.667× | no |
| kostya/primes | 0.9985 | 1.0065 | 2.3565 | 1.6990 | 0.442× | 0.610× | yes |
| kostya/levenshtein | 1.4830 | 1.4640 | 1.8875 | 0.8049 | 0.789× | 1.855× | no |
| kostya/brainfuck | 65.6690 | 65.1425 | 82.9790 | 27.3435 | 0.803× | 2.437× | no |
| larceny/paraffins | 0.2160 | 0.1035 | 0.1390 | 0.0410 | 0.784× | 2.722× | no |
| larceny/pnpoly | 1.8690 | 1.8960 | 2.3425 | 1.9146 | 0.842× | 1.019× | yes |
| larceny/ray | 0.1230 | 0.1230 | 0.2075 | 0.1475 | 0.614× | 0.888× | yes |
| beng/mandelbrot | 12.9715 | 13.0085 | 13.3420 | 12.0845 | 0.988× | 1.086× | yes |
| kostya/json_gen | 3.0390 | 2.5650 | 4.8655 | 1.3920 | 0.539× | 1.899× | no |
| larceny/divrec | 0.9635 | 0.9390 | 1.2355 | 5.1370 | 0.803× | 0.194× | yes |

**Coverage is 28/28. Qualification improves from 24/28 to 26/28 against
Full MIR; 16/28 pass the C2MIR allowance and both gates. The requested all-28
performance target remains unmet.** Queens and paraffins newly beat Full MIR.
NQueens still misses Full MIR; sumfp remains below clock resolution and is
unqualified. Zero observations are retained without an invented timing floor
or an all-row geometric mean.

Against the interleaved previous MVP, median execution improves about 62% for
Queens, 52% for paraffins, 23% for Fib, 16% for JSON generation and 7% for Ack.
Queens and paraffins still take about 1.95× and 2.52× their C2MIR references.
Bounce regresses about 10% against the previous MVP (95% before/after interval
1.036–1.118×), although it still passes the Full MIR gate. That regression is
retained in the report; its root cause has not been established. No other row
has a before/after interval wholly above parity.

The unresolved C2MIR rows are Sieve, Quicksort, FFT, NQueens, Bounce, Queens,
Fannkuch, Levenshtein, Brainfuck, paraffins and JSON generation, plus the
resolution-limited sumfp. These remain tuning work, not qualified coverage.

### Verification

The final release passes **30/30 focused test groups**, including all benchmark
goldens, forced/poisoned GC and seeded collection. The new recursive-array
fixture covers mutual read-only recursion, retained and mutated plain arrays,
simultaneous readers/writers, invalid indices, finite and overflowing recursive
ranges, snapshots through borrowed recursive writes, and short-array errors.
The new string fixture covers single and multiple uses, saved prefixes,
self-append aliases, effectful argument order and Unicode. Both new fixtures
also match their goldens under Full MIR. Emission assertions verify the guarded
recursive path and fused string construction; unbounded guarded recursion
still reaches stack-overflow recovery.

The required `make test-lambda-baseline` reports **8,501/8,502**, including
**279/279** MIR emission and **350/350** GC-stress tests. Its sole failure is
the previously recorded AUTO-tier `proc_mir_mvp_roots` snapshot mismatch:
the last line is `false`, expected `true`. The isolated rerun reproduces it.
No golden or assertion was weakened; the aggregate gate is not fully passing.
Logs are `baseline-final.log` and `baseline-script-details.log` under the pass's
scratch directory. The measured release was restored after the baseline's
debug build. Compiler, runner, timer and executable hashes were rechecked
against the report, and all 28 original source/golden triples match the prior
campaign. `git diff --check` passes.

Build, focused and timing logs are `build22.log`, `focused22.log` and
`paired22.log` under `temp/lambda_mir_mvp/tune28d/`. The measured release is
pinned there as `final-release.exe`; `start-release.exe` is the previous MVP.
Reproduce the focused gate and paired experiment from the release build:

```sh
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28d/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28d/start-release.exe \
  --runs 30 --warmups 2 --output temp/lambda_mir_mvp/repeated-recursive.json
```


## Constructor admission and native update tuning — 2026-11-02

This pass preserves the same 28 workloads, runtime types, container layouts and
shared helpers (**D2.3.4–D2.3.5**). The changes remain in MVP planning/lowering
and reuse the shared AST and direct-call ABI (**D8.2.5**, **D7.4.2**).

### Implementation

- Constructor success now uses the same invariant primitive-array proof as
  ordinary admission. The old generic relation rechecked arrays that a typed
  `fill` had already admitted. Boxed errors still take the ordinary boundary
  before any native array witness becomes available (**S9.1.3**, **D2.5.2**).
- Contextual `fill` lowering is shared by bindings and direct-call arguments.
  It passes the parameter's existing immutable representation certificate to
  the shared constructor, avoiding runtime metadata resolution. Count/value
  evaluation order, allocation, error checking and precise roots stay with
  the existing helper (**D6.3.1**, **S9.1.3**).
- Successfully admitted primitive literals and matching typed fills establish
  fresh packed storage. Their initial native/writable witnesses no longer
  inspect the same physical carrier and COW flags again. Snapshot captures
  still invalidate writability; unknown carriers retain guards (**S9.1.2**).
- Unit-stride copy loops accept constant offsets on either index. Complete
  counter and shifted-access bounds are checked before effects. The native
  loop preserves forward overlap order, counter results and checked fallback
  on short arrays or invalid indices (**S7.1.1v3**, **S9.1.2**).
- Integer array updates of the form `(owner[key] +/- constant) % power_of_two`
  can load, add, mask and store directly after guards establish writable
  integer storage, a valid index and a finite nonnegative intermediate. Guard
  misses retain the original expression/store, including snapshot detachment,
  negative remainder, poison, dynamic element kinds and failed writes
  (**S4.1.2**, **S9.1.2**). No buffer pointer crosses a collecting call
  (**D5.4.4**).
- Finite integer division/remainder by a known positive power of two uses
  shifts/masks. Negative dividends receive the truncation-toward-zero bias;
  non-finite lanes retain the checked helper (**S4.1.2**). Power-of-two tests
  share the existing checked-math helper. The runner now fingerprints that
  dependency alongside the compiler sources.

### Measured qualification

[Raw paired report](../../test/benchmark/lambda_mvp/tune28_constructor_2026-11-02.json):
30 measured samples after two warmups, interleaving candidate MVP, previous
MVP, Full MIR and the independent C2MIR C ports with seed 1101. The previous
MVP is the preceding recursive-tuning release, SHA-256
`009b97150f050aaf439fec067d0aa155539897d60e699f6cfabf1bc57523e65e`;
it includes uncommitted changes beyond the recorded Git HEAD. Full MIR uses
the candidate executable. All original Lambda/C/golden hashes are unchanged,
and every measured result matches its golden. The retired Lambda C-text
backend remains removed (**D1.6**).

Times are **self-reported internal execution milliseconds, not end-to-end
latency**. Timer boundaries are unchanged, including formatting/output in the
same five rows. Startup and compilation are excluded. U95 is the one-sided
95% paired-bootstrap upper bound from 10,000 resamples. Qualification still
requires **U95 MVP/Full < 1.00** and **U95 MVP/C2MIR <= 1.10**.

Candidate release SHA-256:
`be462999194811d7538b0ba07c50f50c9a258c9da61e3a36f8ce3d3b632a8d17`.

| Workload | Previous MVP | MVP | Full MIR | C2MIR | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.6085 | 0.6115 | 1.1975 | 1.0366 | 0.523× | 0.604× | yes |
| r7rs/sum | 0.0990 | 0.1000 | 0.2680 | 0.2681 | 0.379× | 0.379× | yes |
| r7rs/sumfp | below resolution | below resolution | 0.0720 | 0.0761 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 22.4850 | 22.4205 | 28.3345 | 22.4121 | 0.854× | 1.024× | yes |
| larceny/array1 | 0.1155 | 0.1145 | 1.4230 | 0.2974 | 0.083× | 0.394× | yes |
| awfy/sieve | 0.0210 | 0.0190 | 0.0265 | 0.0120 | 0.760× | 1.672× | no |
| beng/spectralnorm | 0.3135 | 0.3270 | 1.1705 | 0.3455 | 0.287× | 0.982× | yes |
| awfy/nbody | 1.4470 | 1.4825 | 2.6505 | 1.5015 | 0.577× | 1.018× | yes |
| r7rs/tak | 0.0795 | 0.0845 | 0.1285 | 0.1196 | 0.696× | 0.745× | yes |
| r7rs/ack | 5.8050 | 5.8275 | 10.8025 | 12.3716 | 0.545× | 0.479× | yes |
| larceny/quicksort | 0.2820 | 0.2900 | 0.5985 | 0.2324 | 0.499× | 1.284× | no |
| kostya/matmul | 4.7305 | 4.6715 | 19.9030 | 6.1990 | 0.239× | 0.769× | yes |
| kostya/collatz | 177.3540 | 175.1035 | 302.5720 | 227.6925 | 0.592× | 0.784× | yes |
| r7rs/fibfp | 0.5970 | 0.5920 | 0.6735 | 1.1315 | 0.911× | 0.538× | yes |
| r7rs/fft | 0.0500 | 0.0480 | 0.0745 | 0.0205 | 0.658× | 2.448× | no |
| r7rs/nqueens | 1.1625 | 0.7775 | 0.7420 | 0.3546 | 1.120× | 2.311× | no |
| awfy/bounce | 0.0535 | 0.0540 | 0.0580 | 0.0215 | 0.982× | 2.697× | no |
| awfy/queens | 0.0320 | 0.0270 | 0.0620 | 0.0159 | 0.459× | 1.764× | no |
| beng/fannkuch | 0.2400 | 0.2185 | 0.2595 | 0.1545 | 0.863× | 1.495× | no |
| kostya/primes | 0.9545 | 0.9955 | 2.3165 | 1.6326 | 0.445× | 0.624× | yes |
| kostya/levenshtein | 1.5175 | 1.4700 | 1.8715 | 0.8070 | 0.796× | 1.917× | no |
| kostya/brainfuck | 65.1460 | 52.4355 | 80.3720 | 27.4396 | 0.664× | 1.936× | no |
| larceny/paraffins | 0.1070 | 0.0965 | 0.1400 | 0.0409 | 0.714× | 2.448× | no |
| larceny/pnpoly | 1.8520 | 1.8960 | 2.3675 | 1.8501 | 0.828× | 1.047× | yes |
| larceny/ray | 0.1270 | 0.1270 | 0.2210 | 0.1536 | 0.597× | 0.842× | yes |
| beng/mandelbrot | 12.9385 | 12.8580 | 13.2685 | 12.0841 | 1.000× | 1.081× | no |
| kostya/json_gen | 2.6410 | 2.5830 | 4.6575 | 1.3868 | 0.569× | 1.933× | no |
| larceny/divrec | 0.9500 | 0.8965 | 1.2400 | 5.0575 | 0.768× | 0.189× | yes |

**Coverage remains 28/28. This campaign qualifies 25/28 against Full MIR,
16/28 against the C2MIR allowance, and 15/28 against both. The requested
all-28 performance target remains unmet.** Beng Mandelbrot's median is
3.1% faster than Full MIR, but its U95 is exactly 1.000, so it loses strict
qualification relative to the preceding campaign. Its before/after interval
includes parity. NQueens still misses Full MIR; sumfp remains below timer
resolution and unqualified. Zero observations are retained without an invented
floor or an all-row geometric mean.

Against the interleaved previous MVP, median execution improves 33.1% for
NQueens, 19.5% for Brainfuck, 15.6% for Queens, 9.8% for paraffins, 9.5% for
Sieve and 9.0% for Fannkuch. No before/after 95% two-sided interval lies wholly
above parity. The isolated arithmetic screen improved Bounce by about 5%,
but the full-pass comparison does not establish an overall Bounce gain.

The C2MIR misses remain Sieve, Quicksort, FFT, NQueens, Bounce, Queens,
Fannkuch, Levenshtein, Brainfuck, paraffins and JSON generation, plus
resolution-limited sumfp. NQueens now takes 0.778 ms versus Full MIR's
0.742 ms and C2MIR's 0.355 ms. The C port also shares its placed board
across recursive frames; Lambda still preserves ordinary parameter snapshots
(**S9.1.2**). This semantic difference is retained in interpreting the gap,
without changing workloads or gates.

### Verification

The release passes **34/34 focused test groups**, including all 28 benchmark
goldens, forced/poisoned GC and seeded collection. New constructor tests cover
admitted fills, snapshots, effectful argument order and rejected counts/values.
Copy-loop cases cover adjacent offsets, overlap order, short arrays, negative
indices and zero trips. Modular-update cases cover shared owners, invalid
indices, floats, wrong element kinds, signed division/remainder, integer
limits and non-finite lanes. All three new/extended fixtures also match their
goldens under Full MIR. Emission checks verify that typed constructor success
avoids readmission, offset copies use a native loop, and modular arithmetic
retains the checked fallback.

The required `make test-lambda-baseline` reports **8,501/8,502**, including
**279/279** MIR emission and **350/350** GC-stress tests. Its sole failure is
the previously recorded AUTO-tier `proc_mir_mvp_roots` cold-float snapshot
mismatch: last line `false`, expected `true`. The isolated rerun reproduces
it. No golden or assertion was weakened; the aggregate gate remains failing.
The pinned measured release was restored after the debug baseline. Compiler,
runner, helper, timer and executable hashes match the report, and all 28
original source/golden triples match the preceding campaign. `git diff --check`
passes. Logs are `build7.log`, `focused7.log`, `paired7.log`,
`baseline-final.log`, `baseline-script-details.log` and `roots-isolated.log`.

Build, focused, timing and baseline logs are under
`temp/lambda_mir_mvp/tune28e/`. The measured release is `final-release.exe`;
`start-release.exe` is the preceding recursive-tuning MVP. Reproduce with:

```sh
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28e/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28e/start-release.exe \
  --runs 30 --warmups 2 --output temp/lambda_mir_mvp/repeated-constructor.json
```


## Native fill and bitwise tree tuning — 2026-11-02

### Why the preceding qualification count decreased

Only Beng Mandelbrot changed qualification between the recursive and constructor
campaigns. Its one-sided 95% upper MVP/Full ratio moved from **0.987782** to
**1.000000**. The strict Full MIR gate requires `< 1`, so both the Full count
and the combined count decreased by one. In the constructor campaign its median
was still 12.858 ms against Full MIR's 13.2685 ms. The interleaved before/after
95% interval was **[0.975724, 1.017497]**, including parity; that result does not
establish a regression. It does show that the previous performance margin was
too small to qualify reliably. The gate and its threshold remain unchanged.

### Implementation

Runtime types, array layouts and ownership semantics remain unchanged
(**D2.3.4–D2.3.5**, **S9.1.2–S9.1.3**).

- Proven singleton integer bindings now materialize their value directly,
  including immutable module constants and locals whose complete write set
  preserves that value. Variable flags and poison inputs retain their ordinary
  behavior (**S4.1.2**).
- Nested integer bitwise operations keep finite intermediate values in native
  lanes and box once at the outer result. Every intermediate and shift count
  is guarded. A miss replays the original checked tree; eligibility requires
  pure leaves, preserving evaluation order, overflow and subsequent errors
  (**S4.1.2**). The opcode selection helper is shared with Full MIR.
- Self-tail calls can reuse their current frame when every array parameter is
  forwarded unchanged and the existing recursive effect analysis proves it
  read-only. Scalar arguments are evaluated before parameter replacement;
  allocations retain the same precise root protocol (**D6.3.1–D6.3.4**).
- Discarded conditional arms can reuse the existing closed-array-region guard.
  This removes repeated checks within FFT's bit-reversal swaps even though its
  surrounding loop also changes indices in a nested loop. Guard misses execute
  the checked arm before any source effect has occurred; failed writes and
  snapshots remain observable (**S7.1.1v3**, **S9.1.2**, **D8.2.6**).
- A native-bit entry reuses the existing primitive fill kernel. MVP proves a
  matching uncounted primitive-array certificate and scalar representation,
  guards the count and integer poison values, and passes scalar bits directly.
  The shared allocator, construction roots and certificate installation remain
  in the runtime. Counted contracts and guard misses keep the existing checked
  constructor (**D4.3.3**, **S9.1.3**, **D6.3.3**). The import remains
  `MAY_GC` with conservative reentry metadata. A repeated NQueens profile
  identified array construction and allocation as the principal helper costs.
- Small constant integer fills passed to proven read-only, nonescaping
  parameters use the existing `MarkBuilder` Input-owned representation. Each
  payload is capped at 64 lanes. Only direct readers qualify, preventing a
  forwarded boundary from replacing an Input-owned certificate with activation
  metadata. Mutable or retained actuals, effectful arguments, counted contracts
  and uncertain values keep runtime construction
  (**D5.1.1–D5.1.3**, **S9.1.2–S9.1.3**).

The first 100-sample report,
[`tune28_native_fill_2026-11-02.json`](../../test/benchmark/lambda_mvp/tune28_native_fill_2026-11-02.json),
was retained before the constant-fill changes. It qualified
26/28 against Full MIR and 14/28 against both gates. NQueens improved over the
previous MVP but its Full MIR upper bound was 1.017; Pnpoly and Beng Mandelbrot
lost C2MIR qualification with upper bounds 1.125 and 1.117. No samples were
dropped or substituted. The following campaign measures the additional code
changes against the same pinned previous release.

### Measured qualification

[Final 100-sample report](../../test/benchmark/lambda_mvp/tune28_constant_fill_2026-11-02.json)
uses two warmups, 100 measured samples per row and randomized interleaving of
MVP, the pinned previous MVP, Full MIR and the independent C2MIR C ports.
Timing is the scripts' self-reported execution window and the matched C kernel
window; compilation is excluded. The original 28 Lambda/C/golden triples are
unchanged. Full MIR uses the candidate executable, including the shared fill
kernel refactoring.

The candidate is SHA-256
`6a1d5dd787c5da62a8c86af5aa2805b6e372373f6d690862f077540e8b35fb2c`,
and the previous release is
`be462999194811d7538b0ba07c50f50c9a258c9da61e3a36f8ce3d3b632a8d17`.
The gates remain one-sided paired-bootstrap upper bounds: Full MIR `< 1`,
C2MIR `<= 1.10`, with 10,000 resamples and seed 1101. Every sample is retained.

| Benchmark | Previous MVP ms | MVP ms | Full MIR ms | C2MIR ms | U95 / Full | U95 / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|---|
| r7rs/fib | 0.5670 | 0.5765 | 1.1360 | 1.0359 | 0.540 | 0.595 | yes |
| r7rs/sum | 0.0950 | 0.0960 | 0.2590 | 0.2561 | 0.429 | 0.434 | yes |
| r7rs/sumfp | 0.0000 | 0.0000 | 0.0690 | 0.0681 | unresolved | unresolved | no |
| awfy/mandelbrot | 22.2975 | 22.3490 | 27.3460 | 21.9336 | 0.836 | 1.035 | yes |
| larceny/array1 | 0.1070 | 0.1095 | 1.3565 | 0.2859 | 0.083 | 0.392 | yes |
| awfy/sieve | 0.0180 | 0.0170 | 0.0260 | 0.0120 | 0.680 | 1.505 | no |
| beng/spectralnorm | 0.3200 | 0.3175 | 1.1620 | 0.3445 | 0.276 | 0.933 | yes |
| awfy/nbody | 1.4215 | 1.3970 | 2.5825 | 1.4314 | 0.551 | 0.994 | yes |
| r7rs/tak | 0.0830 | 0.0835 | 0.1290 | 0.1176 | 0.672 | 0.738 | yes |
| r7rs/ack | 5.5275 | 5.5690 | 10.2040 | 11.9325 | 0.551 | 0.470 | yes |
| larceny/quicksort | 0.2750 | 0.2745 | 0.5755 | 0.2145 | 0.487 | 1.305 | no |
| kostya/matmul | 4.4665 | 4.4645 | 19.2915 | 5.8931 | 0.235 | 0.767 | yes |
| kostya/collatz | 169.8510 | 171.6480 | 293.4985 | 224.9275 | 0.595 | 0.781 | yes |
| r7rs/fibfp | 0.5510 | 0.5460 | 0.6420 | 1.0909 | 0.879 | 0.528 | yes |
| r7rs/fft | 0.0500 | 0.0470 | 0.0740 | 0.0190 | 0.644 | 2.468 | no |
| r7rs/nqueens | 0.7985 | 0.6660 | 0.7250 | 0.3365 | 0.944 | 2.012 | no |
| awfy/bounce | 0.0530 | 0.0540 | 0.0570 | 0.0200 | 0.964 | 2.697 | no |
| awfy/queens | 0.0285 | 0.0270 | 0.0650 | 0.0151 | 0.462 | 1.982 | no |
| beng/fannkuch | 0.3075 | 0.2665 | 0.4000 | 0.1436 | 1.178 | 2.444 | no |
| kostya/primes | 0.9545 | 1.1065 | 2.3600 | 1.7014 | 0.522 | 0.734 | yes |
| kostya/levenshtein | 1.4140 | 1.4420 | 1.8050 | 0.7865 | 0.806 | 1.873 | no |
| kostya/brainfuck | 51.3075 | 52.9835 | 81.2225 | 25.9844 | 0.662 | 2.073 | no |
| larceny/paraffins | 0.0910 | 0.0910 | 0.1310 | 0.0391 | 0.777 | 2.586 | no |
| larceny/pnpoly | 2.1295 | 2.0580 | 2.9550 | 1.9969 | 0.768 | 1.139 | no |
| larceny/ray | 0.1200 | 0.1190 | 0.2060 | 0.1400 | 0.583 | 0.862 | yes |
| beng/mandelbrot | 12.3855 | 11.4395 | 12.6455 | 11.2485 | 0.913 | 1.030 | yes |
| kostya/json_gen | 2.5105 | 2.4940 | 4.7195 | 1.3419 | 0.540 | 1.902 | no |
| larceny/divrec | 0.9085 | 0.9210 | 1.2565 | 5.4070 | 0.842 | 0.201 | yes |

The original single-run windows qualify **26/28 against Full MIR, 15/28
against C2MIR and 15/28 against both**. All 27 rows with resolved clocks have
lower MVP medians than Full MIR, but that does not replace the confidence gate.
NQueens now passes Full MIR: 0.666 ms against 0.725 ms, U95 **0.944**; its median
improves **16.6%** against the paired previous MVP. Beng Mandelbrot also passes
both gates, with upper bounds below parity against Full MIR and 1.030 against
C2MIR.

Two rows illustrate why medians alone are insufficient. Fannkuch's median is
0.2665 ms against Full MIR's 0.4000 ms, but its broad interval gives a Full MIR
upper bound of **1.178**. Pnpoly's median is 1.031 times C2MIR, but its upper
bound is **1.139**. Neither is counted as qualified for that gate. The original
Sumfp window still records zeros and remains unresolved in this table.

The paired comparison with the previous MVP also records a remaining regression:
Brainfuck's median rises **3.3%**, with a two-sided ratio interval of
**[1.0235, 1.0469]**. Its generated `run_bf` remains 1,275 MIR instructions and
5,936 native bytes in both pinned releases; that size check does not establish
the cause or dismiss the measured slowdown. Sieve, FFT, NQueens and Beng
Mandelbrot have before/after intervals wholly below parity. Other faster
medians, including Fannkuch, are not claimed as established improvements.

### Resolving Sumfp's clock limit

The host reports a 1-microsecond `CLOCK_MONOTONIC` resolution. MVP's existing
guarded exact-sum lowering finishes the original Sumfp invocation below that
resolution. The single-run zeros remain in the original report and do not
qualify; neither a denominator floor nor a fabricated duration is used.

The runner now supports `--case r7rs/sumfp --sumfp-batch 10000`. It retains both
original kernel bodies and generates matched mains under `temp/`: each calls
the kernel 10,000 times, accumulates every returned value, and validates the
total. The loop and accumulation are timed; result checking and reporting are
outside the window. The original benchmark files and goldens remain unchanged.
The [batch report](../../test/benchmark/lambda_mvp/tune28_sumfp_batch_2026-11-02.json)
records both generated sources, hashes, every sample and the changed scope.

With the same final executable, two warmups and 100 measured samples, the
whole-batch medians are **0.011 ms MVP, 1,001.3975 ms Full MIR and 951.375 ms
C2MIR**. All samples are positive; MVP's minimum is 0.010 ms. The one-sided
upper ratios are **0.0000211 against Full MIR** and **0.0000221 against C2MIR**.
The previous MVP also measures 0.011 ms: this resolves qualification of an
existing optimization, rather than demonstrating a new speedup for this row.

Combining the 27 clock-resolved original rows with this explicitly separate
Sumfp batch gives **27/28 against Full MIR, 16/28 against C2MIR and 16/28
against both**. The original-window counts remain **26/28, 15/28 and 15/28**.
Fannkuch still fails the Full MIR confidence gate. Twelve rows still miss the
C2MIR allowance: Sieve, Quicksort, FFT, NQueens, Bounce, Queens, Fannkuch,
Levenshtein, Brainfuck, Paraffins, Pnpoly and JSON generation. The requested
all-28 performance milestone remains open.

### Validation and reproduction

All **40 focused test groups** pass, including the 28 Lambda and C goldens,
forced and seeded GC with freed-memory poisoning, and emission checks for the
new lowering. Constructor tests cover counted contracts, poison values,
read-only constant actuals, retained/mutated/forwarded actuals and collection
while an Input-owned constant is live. Loop tests cover short arrays, snapshot
preservation, checked partial writes and collecting tail calls.

The required `make test-lambda-baseline` finishes **8,501/8,502**, including
**279/279 MIR emission** and **350/350 forced-GC** tests. The sole failure is
the previously recorded AUTO-tier
`AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/proc_mir_mvp_roots` cold-float
snapshot: its final line is `false`, expected `true`. It reproduces in isolation
with the same mismatch as the preceding pass. The golden is unchanged. Logs
are in `temp/lambda_mir_mvp/tune28f/baseline-final.log` and
`temp/lambda_mir_mvp/tune28f/roots-isolated.log`. The measured release executable
was restored after the debug baseline, and its SHA-256 rechecked.

Full MIR also passes the constructor, conditional-array, recursive-array and
flag fixtures in normal and forced-GC modes. The shift fixture exposes existing
Full MIR poison/count edge failures; its output is identical with the pinned
previous and final releases. MVP passes its unchanged expected semantics
(**S4.1.2**). Full MIR is a performance comparison, while the golden and formal
semantics remain the correctness authority.

All original Lambda/C/golden hashes match the constructor campaign. The final
compiler, shared helper, runner, timer and executable fingerprints were checked
against the recorded report after measurement.

```sh
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/tune28f/final-constant-release.exe \
  PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28f/final-constant-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28f/start-release.exe \
  --runs 100 --warmups 2 --output temp/lambda_mir_mvp/recheck28.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28f/final-constant-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28f/start-release.exe \
  --case r7rs/sumfp --sumfp-batch 10000 --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/recheck_sumfp.json
```

## Small numeric payloads and fused string formatting — 2026-11-02

### Implementation

Small one-dimensional numeric arrays now keep their payload beside the existing
`ArrayNum` header when both fit an existing GC object size class. This removes
the second allocation and its construction root frame. Objects remain nonmoving
(**D5.3.1–D5.3.1**); the header, lane representations, capacity semantics and
runtime type model are unchanged. Larger and multidimensional arrays retain
data-zone allocation. Growth copies the lanes into the ordinary data zone;
views retain their base owner and COW snapshots still detach (**S9.1.2**).
No collector or vendor code was changed.

String recipes now carry proven integer-formatting inputs to the final join.
Only direct `string(int)` conversions and exact transparent wrappers qualify;
effectful functions retain their calls. The helper renders finite integers into
native temporary bytes using the existing canonical renderer (**S4.1.2**), then
uses the existing owned-string join. Poison values keep the canonical generic
conversion. Generic conversions publish precise input and converted-string roots;
the finite-only path needs no additional frame before the join's own allocating
path (**D6.3.2–D6.3.3**). Argument expressions still evaluate once in source order.

Adjacent literal fragments, including literals forwarded through recipe
parameters, coalesce into one Input-owned constant (**D5.1.1**). The existing
Full MIR literal recognizer was promoted into the shared AST module. JSON's
outer object append now passes eight fragments instead of twenty-six, and its
inner coordinate object passes five. Prefix ownership, alias freezing, Unicode
bytes and snapshot behavior are unchanged (**S9.1.2**).

The preliminary allocation-only 15-sample screen measured NQueens at 0.542 ms
against the preceding MVP's 0.679 ms, with a before/after 95% interval of
[0.773, 0.835]. Queens measured 0.023 ms against 0.026 ms. This screen guided the
work; the full qualification below uses a separate 100-sample campaign.

### Measured qualification

[100-sample report](../../test/benchmark/lambda_mvp/tune28_small_arrays_strings_2026-11-02.json)
uses two warmups and randomized interleaving of the candidate MVP, previous MVP,
Full MIR and the independent C2MIR C ports. Timing remains the scripts' reported
execution windows and matched C kernel windows; compilation is excluded. All 28
Lambda/C/golden hashes match the preceding campaign. Every sample is retained.
The gates remain one-sided 95% paired-bootstrap upper bounds, Full MIR `< 1`
and C2MIR `<= 1.10`, with 10,000 resamples and seed 1101.

Candidate SHA-256: `51457671ba22db43cf304f91ac5aeaa15115feed616fd86a39e71ed4b1042dd9`.

Previous SHA-256: `6a1d5dd787c5da62a8c86af5aa2805b6e372373f6d690862f077540e8b35fb2c`.

The shared checkout also received concurrent chat/WebSocket work after the
previous release. Executables are pinned, and each report records source hashes,
revision and working-tree state. Full MIR uses the same candidate executable.
Before/after comparisons therefore compare these complete executable revisions;
they are not an isolated build differing only in the tuning patch.

| Benchmark | Previous MVP ms | MVP ms | Full MIR ms | C2MIR ms | U95 / C2MIR | C2MIR gate |
|---|---:|---:|---:|---:|---:|---|
| r7rs/fib | 0.5775 | 0.5830 | 1.1910 | 1.0565 | 0.564 | yes |
| r7rs/sum | 0.0970 | 0.0975 | 0.2625 | 0.2700 | 0.369 | yes |
| r7rs/sumfp | 0.0000 | 0.0000 | 0.0710 | 0.0735 | unresolved | no |
| awfy/mandelbrot | 21.1025 | 21.2330 | 26.6990 | 21.3524 | 1.005 | yes |
| larceny/array1 | 0.1080 | 0.1075 | 1.3610 | 0.2950 | 0.374 | yes |
| awfy/sieve | 0.0170 | 0.0160 | 0.0240 | 0.0120 | 1.421 | no |
| beng/spectralnorm | 0.3285 | 0.3235 | 1.2090 | 0.3615 | 0.920 | yes |
| awfy/nbody | 1.5000 | 1.4310 | 2.6750 | 1.5045 | 1.003 | yes |
| r7rs/tak | 0.0860 | 0.0850 | 0.1315 | 0.1195 | 0.728 | yes |
| r7rs/ack | 5.5115 | 5.4995 | 10.2210 | 11.7130 | 0.475 | yes |
| larceny/quicksort | 0.2805 | 0.2860 | 0.5945 | 0.2209 | 1.331 | no |
| kostya/matmul | 4.6810 | 4.5925 | 20.0685 | 6.1906 | 0.761 | yes |
| kostya/collatz | 177.3880 | 177.5860 | 302.2715 | 227.3075 | 0.787 | yes |
| r7rs/fibfp | 0.5940 | 0.5880 | 0.6855 | 1.1356 | 0.537 | yes |
| r7rs/fft | 0.0420 | 0.0425 | 0.0755 | 0.0199 | 2.258 | no |
| r7rs/nqueens | 0.6855 | 0.5515 | 0.6015 | 0.3534 | 1.597 | no |
| awfy/bounce | 0.0540 | 0.0530 | 0.0560 | 0.0210 | 2.647 | no |
| awfy/queens | 0.0250 | 0.0230 | 0.0605 | 0.0151 | 1.586 | no |
| beng/fannkuch | 0.2165 | 0.2200 | 0.2605 | 0.1411 | 1.800 | no |
| kostya/primes | 0.9320 | 0.9260 | 2.1885 | 1.5151 | 0.636 | yes |
| kostya/levenshtein | 1.4805 | 1.4640 | 1.8325 | 0.8065 | 1.861 | no |
| kostya/brainfuck | 53.9940 | 53.8960 | 82.3610 | 26.7323 | 2.060 | no |
| larceny/paraffins | 0.0920 | 0.0895 | 0.1310 | 0.0398 | 2.304 | no |
| larceny/pnpoly | 1.8300 | 1.8215 | 2.3650 | 1.8191 | 1.017 | yes |
| larceny/ray | 0.1230 | 0.1200 | 0.2090 | 0.1455 | 0.833 | yes |
| beng/mandelbrot | 11.3570 | 11.4730 | 12.5825 | 11.1815 | 1.040 | yes |
| kostya/json_gen | 2.5305 | 1.4290 | 4.7680 | 1.3660 | 1.070 | yes |
| larceny/divrec | 0.9030 | 0.9065 | 1.2130 | 5.1165 | 0.182 | yes |

The original windows qualify **27/28 against Full MIR and 17/28 against C2MIR
and both gates**. Sumfp's zero-duration observations remain unresolved here.
JSON generation improves **43.5%** against the interleaved previous MVP, with a
before/after 95% interval of **[0.557, 0.577]**, and now passes the C2MIR gate
with an upper ratio of **1.070**. NQueens improves **19.5%**
(**[0.768, 0.834]**) and Queens improves **8.0%** (**[0.885, 0.960]**), but both
remain outside the allowance.

Pnpoly also qualifies in this campaign (**U95 1.017**), but its before/after
interval **[0.975, 1.015]** includes parity. This is restored qualification,
not evidence of a new code-driven speedup. Fannkuch qualifies against Full MIR
with **U95 0.977**; its before/after interval **[0.982, 1.220]** likewise does
not establish improvement. Brainfuck's before/after interval **[0.982, 1.007]**
includes parity; this pass does not establish recovery of its earlier slowdown.

### Sumfp batch and remaining work

[Separate Sumfp batch report](../../test/benchmark/lambda_mvp/tune28_small_arrays_strings_sumfp_2026-11-02.json)
repeats each unchanged kernel 10,000 times inside matched timer boundaries,
consuming every result, with 100 samples and two warmups. The original
zero-duration samples remain recorded and unqualified. The batch confirms the
existing finite-reduction optimization rather than attributing a new gain to
this pass.

| Batch | Previous MVP ms | MVP ms | Full MIR ms | C2MIR ms | U95 / Full | U95 / C2MIR |
|---|---:|---:|---:|---:|---:|---:|
| Sumfp ×10,000 | 0.0110 | 0.0110 | 767.6415 | 795.9965 | 0.00001485 | 0.00001479 |

Combining the 27 resolved original windows with this explicitly separate batch
qualifies **28/28 against Full MIR, 18/28 against C2MIR and 18/28 against both**.
The requested all-28 C2MIR milestone remains open. The ten misses are Sieve,
Quicksort, FFT, NQueens, Bounce, Queens, Fannkuch, Levenshtein, Brainfuck and
Paraffins. Emitted-code inspection still finds poison/overflow arithmetic checks
in Bounce and Levenshtein and per-iteration array/character guards in
Levenshtein. These checks require stronger proofs before elimination; the
measured C2MIR gap does not authorize weakening Lambda semantics (**S4.1.2**,
**S7.1.1v3**, **S9.1.2–S9.1.3**).

### Validation and reproduction

All **40 focused MVP groups** pass, including every original benchmark golden,
forced and seeded GC with freed-memory poisoning, and emission checks. Expanded
string cases exercise integer endpoints, inf/nan, Unicode, empty and adjacent
literals, snapshots, self-appends, effectful wrappers and argument order. Full
MIR matches the expanded string golden in normal and forced-GC modes with both
the previous and candidate releases.

The native representation suite passes **47/47**. New tests cover all fourteen
numeric lanes across collection and growth from inline payloads to data-zone
storage, plus COW and a view that is the only root retaining its base. Both new
cases also pass with collection forced at every allocation and freed-memory
poisoning.

The current shared-tree `make test-lambda-baseline` records **8,514/8,517**,
including **279/279 MIR emission**, **350/350 forced-GC** and **849/849 JS MVP**
tests. Its three failures were:

- `Tiers/HotwireHttpTest.ClientAndServer/1`: 30-second timeout; all three
  isolated reruns pass (13.974, 14.287 and 11.868 seconds).
- `docx_docx_business_plans`: 60-second timeout. Separate-process reruns
  pass at 53.473 and 29.514 seconds, with one intervening 60.007-second timeout.
  A separate `test/interp/tier_sweep.py --jobs 3` was active in the shared
  checkout during these checks. This remains timeout instability under load,
  not a clean aggregate pass or a demonstrated tuning regression.
- `proc_mir_mvp_roots`: the previously recorded AUTO-tier cold-float snapshot
  mismatch, final `false` instead of `true`. Both pinned releases reproduce
  identical output; the golden remains unchanged.

No test limit, expected result or harness behavior was relaxed. Logs are under
`temp/lambda_mir_mvp/tune28g/`: `focused5.log`, `item-repr2.log`,
`item-repr2-gc.log`, `full-string-parity.log`, `baseline-final.log`,
`hotwire-isolated.log`, `docx-isolated.log` and `roots-release-parity.log`.
The measured executable remains pinned as `final-release.exe` there. The root
`lambda.exe` is left at the baseline's debug build while the independent tier
sweep uses it; performance commands below explicitly select the release pin.
Source and executable fingerprints match the retained reports.

```sh
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/tune28g/final-release.exe \
  PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28g/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28g/start-release.exe \
  --runs 100 --warmups 2 --output temp/lambda_mir_mvp/recheck28.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28g/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28g/start-release.exe \
  --case r7rs/sumfp --sumfp-batch 10000 --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/recheck_sumfp.json
```

## Guarded strides, character reads and integer ranges — 2026-11-03

### Implementation

A closed ascending loop can now guard the first and final **visited** index of
an affine access once, including a stride that does not divide the loop limit.
The planner requires a finite counter and update, one unconditional terminal
increment, stable key inputs, and a closed body. The generated entry checks a
positive finite step, native writable/readable owners and both index endpoints.
Guard misses enter the original checked loop before any source effects. This
preserves short arrays, negative indices, snapshots and mutation errors
(**S7.1.1v3**, **S9.1.2**, **D5.4.4**). The existing iteration planner and closed
region emitter provide the shared eligibility and lowering; no workload names
or benchmark constants participate in the optimization.

Fused character comparisons can join this loop proof. ASCII and bounds checks
run at entry, immutable invariant character reads load once, and comparisons of
two proven present characters use their bytes directly. The checked sibling
retains Unicode code-point and absence behavior, including NUL versus absence
(**S7.1.1v3**, **S6.1.2v2**). The ASCII owner guard was extracted from the existing
shared emitter, preserving Full MIR's instruction sequence.

Integer comparisons now use the signed ordering of the existing NaN sentinel
to retain only the necessary unordered check. A finite operand can eliminate
that check for the appropriate comparison direction; equality needs one witness
when both inputs are unknown. Branch demand emits that check and the ordered
branch directly; value demand retains its boolean result. NaN, infinity and
nullable behavior remain under **S4.2.1–S4.2.3**, **S6.1.2v2**.

Range analysis can multiply finite enclosing loop trip bounds for an additive
recurrence. It rejects counter rewrites, skipped increments, mutable borrows and
other noncontracting writes to the accumulator. A subtraction at the start of a
while body can contract a nonnegative interval when the unchanged loop predicate
proves the difference nonnegative. These proofs remove checks only when the
entire interval remains inside int53; saturation and poison retain the original
checked arithmetic (**S4.1.1–S4.1.2**).

Runtime types, container layouts, GC ownership and the default MIR optimization
level remain unchanged. A separate 15-sample O2/O3 screen did not establish a
broad advantage from changing optimizer settings.

### Measurement contract

The user's 2026-11-03 target is a **1.20×** upper ratio bound for **each of the
28 rows**. The runner defaults to `--c2mir-allowance-percent 20`, records the
allowance and its per-row gate, and retains the 10% and Full MIR diagnostics.
Previously committed reports are unchanged. Timers remain self-reported
execution windows with matched C-port boundaries, excluding compilation and
process startup. This uses independent C ports through the standalone C2MIR
reference tool; the removed Lambda C-text backend is not restored or used.

The candidate is `temp/lambda_mir_mvp/tune28h/step10-release.exe`, SHA-256
`c8ac3031b927e0f01973779f89acc1aa3d213549e1c6ef41ccf10300be0ff026`.
The interleaved prior MVP is the previous pass's pinned release, SHA-256
`51457671ba22db43cf304f91ac5aeaa15115feed616fd86a39e71ed4b1042dd9`.
The 25-sample screens are diagnostic; qualification uses the complete campaign
below, with 100 samples, two warmups and the existing one-sided 95% paired
bootstrap (10,000 resamples, seed 1101).

### Complete 28-row campaign

[Retained 100-sample report](../../test/benchmark/lambda_mvp/tune28_guarded_ranges_2026-11-03.json).
Medians are milliseconds; U95 is the one-sided 95% upper ratio bound.
Each row keeps its original workload and timer boundary.

| Benchmark | MVP ms | Full MIR ms | C2MIR ms | U95 / Full | U95 / C2MIR | ≤20% |
|---|---:|---:|---:|---:|---:|:---:|
| r7rs/fib | 0.5850 | 1.1620 | 1.0305 | 0.516 | 0.582 | yes |
| r7rs/sum | 0.0980 | 0.2660 | 0.2659 | 0.375 | 0.378 | yes |
| r7rs/sumfp | 0.0000 | 0.0725 | 0.0710 | unresolved | unresolved | unresolved |
| awfy/mandelbrot | 22.2155 | 27.6485 | 21.9861 | 0.817 | 1.029 | yes |
| larceny/array1 | 0.1050 | 1.3485 | 0.2820 | 0.079 | 0.376 | yes |
| awfy/sieve | 0.0160 | 0.0240 | 0.0120 | 0.708 | 1.421 | no |
| beng/spectralnorm | 0.3155 | 1.1725 | 0.3450 | 0.276 | 0.922 | yes |
| awfy/nbody | 1.4285 | 2.5990 | 1.4525 | 0.572 | 1.026 | yes |
| r7rs/tak | 0.0720 | 0.1300 | 0.1169 | 0.566 | 0.638 | yes |
| r7rs/ack | 5.7015 | 10.6300 | 12.4686 | 0.543 | 0.463 | yes |
| larceny/quicksort | 0.2705 | 0.5900 | 0.2190 | 0.481 | 1.303 | no |
| kostya/matmul | 4.6230 | 19.7910 | 6.0305 | 0.238 | 0.778 | yes |
| kostya/collatz | 173.2065 | 300.7425 | 223.6250 | 0.587 | 0.790 | yes |
| r7rs/fibfp | 0.5515 | 0.6475 | 1.0876 | 0.874 | 0.522 | yes |
| r7rs/fft | 0.0350 | 0.0720 | 0.0190 | 0.500 | 1.915 | no |
| r7rs/nqueens | 0.5260 | 0.5665 | 0.3401 | 0.940 | 1.561 | no |
| awfy/bounce | 0.0470 | 0.0540 | 0.0200 | 0.889 | 2.398 | no |
| awfy/queens | 0.0220 | 0.0590 | 0.0159 | 0.386 | 1.453 | no |
| beng/fannkuch | 0.2135 | 0.2620 | 0.1460 | 0.826 | 1.479 | no |
| kostya/primes | 0.9740 | 2.2455 | 1.5972 | 0.448 | 0.629 | yes |
| kostya/levenshtein | 0.8795 | 1.8650 | 0.8075 | 0.479 | 1.107 | yes |
| kostya/brainfuck | 54.2765 | 78.8005 | 27.1310 | 0.697 | 2.018 | no |
| larceny/paraffins | 0.0880 | 0.1290 | 0.0391 | 0.701 | 2.298 | no |
| larceny/pnpoly | 1.7975 | 2.3865 | 1.8466 | 0.775 | 0.999 | yes |
| larceny/ray | 0.1240 | 0.2080 | 0.1450 | 0.611 | 0.881 | yes |
| beng/mandelbrot | 11.8830 | 13.2180 | 11.8511 | 0.905 | 1.008 | yes |
| kostya/json_gen | 1.4215 | 4.7640 | 1.3816 | 0.304 | 1.051 | yes |
| larceny/divrec | 0.9385 | 1.2235 | 5.1229 | 0.777 | 0.186 | yes |

The original windows qualify **27/28 against Full MIR and 18/28 within 20%
of C2MIR and both gates**. The retained 10% diagnostic qualifies **17/28**.
Sumfp remains unresolved here because the original samples include zero.

The interleaved before/after comparison establishes these improvements:

| Benchmark | Faster than prior MVP | Before/after 95% interval |
|---|---:|---:|
| r7rs/tak | 13.3% | [0.857, 0.890] |
| r7rs/fft | 18.6% | [0.795, 0.837] |
| awfy/bounce | 10.5% | [0.855, 0.923] |
| kostya/levenshtein | 40.9% | [0.583, 0.601] |

Levenshtein newly qualifies at **1.089× C2MIR**, with **U95 1.107×**.
It meets the requested 20% allowance, while narrowly missing the retained 10%
diagnostic. Quicksort's before/after interval **[0.931, 1.036]** includes parity;
its 2.2% lower median is not an established gain. Brainfuck's interval
**[0.989, 1.009]** likewise includes parity. No resolved row's before/after
95% interval lies wholly above 1.0 in this campaign.

### Sumfp batch and remaining work

[Separate matched Sumfp batch](../../test/benchmark/lambda_mvp/tune28_guarded_ranges_sumfp_2026-11-03.json)
repeats each unchanged kernel 10,000 times, consuming every result inside the
matched timer boundaries. It retains 100 samples and two warmups. This resolves
the original row's clock limit and confirms its existing finite-reduction
optimization; both MVP versions have the same median.

| Batch | Previous MVP ms | MVP ms | Full MIR ms | C2MIR ms | U95 / Full | U95 / C2MIR |
|---|---:|---:|---:|---:|---:|---:|
| Sumfp ×10,000 | 0.0110 | 0.0110 | 739.2070 | 739.9825 | 0.00001513 | 0.00001507 |

Combining the 27 resolved original windows with this explicitly separate batch
qualifies **28/28 against Full MIR and 19/28 within 20% of C2MIR and both gates**.
Re-evaluating the prior committed campaign at the same 20% allowance gives
**18/28**. The retained 10% diagnostic remains **18/28** in both campaigns.
The original zero-duration samples remain unqualified.

The all-28 C2MIR target is **not met**. Nine misses remain: Sieve, Quicksort,
FFT, NQueens, Bounce, Queens, Fannkuch, Brainfuck and Paraffins. FFT still has
non-affine bit-reversal accesses outside the strided proof. Unknown array
element ranges still require checked arithmetic in several integer kernels;
scalar loop bounds do not prove those stored values finite (**S4.1.1–S4.1.2**).
Allocation-heavy search also retains the normal runtime ownership costs
(**S9.1.2**, **D5.3.1**, **D5.4.4**). Broader value-range and ownership proofs
remain necessary before removing those checks or allocations.

### Validation

All **40 focused MVP groups** pass on the measured release, including the 28
benchmark goldens, forced and seeded GC with freed-memory poisoning, and
emission checks. Expanded cases cover nondividing strides, empty loops,
changing limits, short arrays, negative indices, snapshots, invariant and
varying character indices, Unicode, NUL versus absence, NaN on either side of
every comparison, nested counts, contracting subtraction and overflow after
counter resets.

The final `make test-lambda-baseline` records **8,518/8,520**, including
**279/279 MIR emission**, **350/350 forced-GC**, **849/849 JS MVP** and
**47/47 item representation** tests. The two failures are:

- `docx_docx_business_plans`: the existing 60-second timeout. All three
  separate-process isolated reruns pass in **24.119, 24.216 and 24.114 seconds**.
- `proc_mir_mvp_roots`: the known AUTO-tier cold-float snapshot mismatch,
  final `false` instead of `true`. The prior and final release both reproduce
  identical output. Its MVP-mode focused test passes.

These reruns do not turn the aggregate result into a clean pass. No expected
output, test selection or timeout was relaxed. Logs are under
`temp/lambda_mir_mvp/tune28h/`: `focused10.log`, `baseline10.log`,
`baseline10-details.log`, `docx-isolated10.log`, `start-release.exe.roots.txt`
and `step10-release.exe.roots.txt`. The root `lambda.exe` remains the baseline's
debug build; all timing explicitly selects the pinned release.

```sh
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/tune28h/step10-release.exe \
  PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28h/step10-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28h/start-release.exe \
  --runs 100 --warmups 2 --c2mir-allowance-percent 20 \
  --output temp/lambda_mir_mvp/recheck28.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune28h/step10-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune28h/start-release.exe \
  --case r7rs/sumfp --sumfp-batch 10000 --runs 100 --warmups 2 \
  --c2mir-allowance-percent 20 --output temp/lambda_mir_mvp/recheck_sumfp.json
```


## 2026-11-03: twenty-row coverage expansion

This expansion retains the original 28 workloads and adds 20 distinct rows:
CPSTak, Diviter, Larceny Primes, Puzzle, Triangl, GCbench, Deriv, BENG NBody,
Binarytrees, Fasta, Pidigits, AWFY List, Permute, Storage, Towers, Base64,
JetStream NBody, Navier–Stokes, Cube3D and Raytrace3D. Admission is implemented
in MVP itself. The Lambda benchmark sources and runtime data layouts are
unchanged. Coverage and performance qualification remain separate gates.

### Implemented capabilities

- Typed record construction, native descriptor reads and scalar stores, nullable
  recursive records, and invariant record `var` arguments. Literal fields are
  evaluated before destination admission. Scalar fields stay in native lanes
  across allocation; existing insertion-ownership rules avoid share-marking
  scalars and freshly produced children. Direct stores require matching trusted
  layout and unique storage. Other cases use the existing checked map runtime
  (**S9.1.2, S9.1.3v2, S9.3.1, S11.4.1v5, D3.2.1, D6.3**).
- General and heterogeneous array literals, ordinary array-key iteration with
  a captured source, base-type `match`, and the existing `cos`, `round` and
  `slice` builtins. Value-producing comprehensions and general match patterns
  remain outside this admitted subset (**S11.2.1, S9.1.2**).
- Nullable native returns retain their semantic contract. Item null and native
  pointer null are converted explicitly; scalar nullable lanes retain their
  own sentinels. Recursive records and mixed arrays exercise these boundaries
  under collection and freed-memory poisoning (**D2.3.5, D6.3**).
- Unannotated mutable container contents remain observations rather than write
  contracts. An inferred array may change element type, and an inferred map may
  accept a different field value. Closed homogeneous literal tables can retain
  a private representation proof after all writes and `var` borrows have been
  excluded; the public AST type remains unchanged (**S12.2.1v3, D4.3.3**).
- Immutable module literal tables use the existing Input arena and ordinary
  COW rules. Constant string-table indices and nullable String[] reads avoid
  repeated generic dispatch. Null text pieces use the join identity only when
  another piece guarantees a string result; owned multi-piece append reuses the
  existing fixed-arity helpers (**D5.1.2, S9.1.2, S10.6.1**).
- Finite table domains feed guarded indexed loops. A typed integer declaration
  may retain the finite element domain after its existing admission check;
  an unguarded absent read never becomes a finite integer proof
  (**S7.1.1v3, S11.4.1v5**).
- A possible sibling `var` alias no longer makes every read-only parameter an
  unconditional storage escape. Direct calls still capture actual aliases;
  guarded kernels require exclusive writable owners before borrowing storage.
  Same-binding aliases, distinct snapshot aliases, short arrays and partial
  writes followed by a rejected read are covered explicitly
  (**S9.1.3v2, D7.4.1**).

The additional native C references use the same kernel-timing macros as the
original rows. BENG and JetStream NBody share the existing AWFY kernel; the
AWFY main retains its original initialization and loop after preprocessing.
Every C reference is checked against the corresponding Lambda golden. C2MIR
is the independent C reference driver, not the removed Lambda C-text backend
(**D1.6**). Timings exclude process startup and compilation; row metadata
records whether formatting or output falls inside the measured window.

### Validation

The aggregate `make test-lambda-baseline` passes **8,646/8,646** tests
(**2,112 input; 6,534 runtime**), including **353/353** forced-GC stress tests.
This final aggregate run includes the completed constructor refinement. All
**41 focused groups** pass, including all 48 Lambda and C reference goldens.
The three new semantic fixtures also match the interpreter, production MIR and
AUTO execution. The new record,
array-domain and alias fixtures run normally, with collection at every
allocation, and with seeded collection plus freed-memory poisoning. The record
and read-only NBody kernel emission assertions also pass. Logs are retained in
`temp/lambda_mir_mvp/expand20/`: `focused-final.log`,
`ownership-delivered.log` and `baseline-delivered.log`.

### Qualification summary

Against the merged starting release (`fc5f257e3`), **no original row has a
two-sided 95% ratio interval wholly above 1.0**. Spectralnorm improves **20.2%**;
the other 27 rows show no statistically established change. This is a measured
no-regression result for these frozen workloads, not a proof of identical
performance on every input or machine. The raw single-run campaign qualifies
27/28 against Full MIR and 18/28 against C2MIR; the supplemental Sumfp batch
brings the combined result to **28/28 and 19/28**, respectively.

The added rows qualify **7/20 against Full MIR and 5/20 against C2MIR and both
gates**. CPSTak, Diviter, Larceny Primes and both new NBody rows meet the complete
target. BENG NBody runs at **0.975× C2MIR**, JetStream NBody at **1.031×**.
Base64 and Navier–Stokes beat Full MIR but still miss the C2MIR allowance.
AWFY List has a faster median than Full MIR, but its confidence bound crosses
1.0 and it does not qualify. The other 15 additions remain performance failures;
none is hidden or routed through production lowering.

Raw samples, randomized run orders, timer scopes, source and binary hashes:
[original 28](../../test/benchmark/lambda_mvp/expand20_regression28_2026-11-03.json),
[additional 20](../../test/benchmark/lambda_mvp/expand20_2026-11-03.json),
[matched Sumfp batch](../../test/benchmark/lambda_mvp/expand20_sumfp_2026-11-03.json).
The old pre-merge campaign does not qualify this implementation.

### Release measurements

All measurements use the isolated final release, execution-only script timers,
two warmups, randomized engine order and 10,000 paired-bootstrap resamples.
The original rows and supplemental Sumfp batch use 100 measured samples;
the 20 additions use 30. Full MIR qualifies only when the one-sided 95%
upper ratio bound is below 1.0; C2MIR qualifies at an upper bound of 1.20.
Raw zero-duration Sumfp samples remain unqualified. The table below uses
the separate matched 10,000-invocation batch for that row.

| Added benchmark | MVP ms | Full MIR ms | C2MIR ms | MVP / Full | MVP / C2MIR | Both gates |
|---|---:|---:|---:|---:|---:|:---:|
| r7rs/cpstak | 0.140 | 0.248 | 0.235 | 0.563× | 0.594× | yes |
| larceny/diviter | 230.095 | 262.858 | 262.818 | 0.875× | 0.875× | yes |
| larceny/primes | 0.904 | 2.281 | 1.707 | 0.396× | 0.530× | yes |
| larceny/puzzle | 20.618 | 4.748 | 1.274 | 4.343× | 16.185× | no |
| larceny/triangl | 672.149 | 143.468 | 60.397 | 4.685× | 11.129× | no |
| larceny/gcbench | 145.661 | 81.386 | 75.206 | 1.790× | 1.937× | no |
| larceny/deriv | 13.659 | 7.317 | 2.623 | 1.867× | 5.208× | no |
| beng/nbody | 1.369 | 9.751 | 1.403 | 0.140× | 0.975× | yes |
| beng/binarytrees | 4.329 | 3.343 | 2.703 | 1.295× | 1.602× | no |
| beng/fasta | 1.333 | 0.371 | 0.229 | 3.588× | 5.833× | no |
| beng/pidigits | 0.361 | 0.336 | 0.045 | 1.076× | 8.014× | no |
| awfy/list | 0.183 | 0.189 | 0.018 | 0.971× | 10.130× | no |
| awfy/permute | 0.072 | 0.068 | 0.021 | 1.059× | 3.429× | no |
| awfy/storage | 0.300 | 0.228 | 0.087 | 1.319× | 3.447× | no |
| awfy/towers | 0.308 | 0.180 | 0.025 | 1.711× | 12.248× | no |
| kostya/base64 | 3.279 | 4.954 | 0.491 | 0.662× | 6.677× | no |
| jetstream/nbody | 1.456 | 10.238 | 1.412 | 0.142× | 1.031× | yes |
| jetstream/navier_stokes | 58.818 | 80.028 | 38.594 | 0.735× | 1.524× | no |
| jetstream/cube3d | 8.309 | 4.889 | 0.399 | 1.699× | 20.827× | no |
| jetstream/raytrace3d | 38.249 | 12.987 | 1.896 | 2.945× | 20.169× | no |

| Original benchmark | MVP / starting MVP | Two-sided 95% interval | Comparison |
|---|---:|---:|---|
| r7rs/fib | 0.997× | 0.982–1.018 | no established change |
| r7rs/sum | 0.986× | 0.953–1.010 | no established change |
| r7rs/sumfp | 1.000× | 1.000–1.000 | no established change |
| awfy/mandelbrot | 1.008× | 0.994–1.022 | no established change |
| larceny/array1 | 0.996× | 0.973–1.009 | no established change |
| awfy/sieve | 0.941× | 0.882–1.000 | no established change |
| beng/spectralnorm | 0.798× | 0.781–0.830 | faster |
| awfy/nbody | 1.002× | 0.956–1.018 | no established change |
| r7rs/tak | 0.980× | 0.947–1.027 | no established change |
| r7rs/ack | 0.985× | 0.946–1.010 | no established change |
| larceny/quicksort | 0.996× | 0.970–1.042 | no established change |
| kostya/matmul | 1.010× | 0.983–1.035 | no established change |
| kostya/collatz | 1.006× | 0.988–1.028 | no established change |
| r7rs/fibfp | 1.000× | 0.977–1.026 | no established change |
| r7rs/fft | 1.000× | 0.974–1.028 | no established change |
| r7rs/nqueens | 0.988× | 0.959–1.010 | no established change |
| awfy/bounce | 0.990× | 0.959–1.021 | no established change |
| awfy/queens | 0.958× | 0.885–1.000 | no established change |
| beng/fannkuch | 0.969× | 0.747–1.149 | no established change |
| kostya/primes | 0.971× | 0.834–1.074 | no established change |
| kostya/levenshtein | 0.993× | 0.858–1.021 | no established change |
| kostya/brainfuck | 0.993× | 0.978–1.005 | no established change |
| larceny/paraffins | 0.989× | 0.957–1.045 | no established change |
| larceny/pnpoly | 1.004× | 0.978–1.030 | no established change |
| larceny/ray | 0.984× | 0.953–1.017 | no established change |
| beng/mandelbrot | 0.993× | 0.985–1.004 | no established change |
| kostya/json_gen | 0.994× | 0.971–1.012 | no established change |
| larceny/divrec | 0.988× | 0.964–1.023 | no established change |

### Remaining performance work

The all-20 target remains open. The source and current lowering expose several
areas for further proofs and measurement:

- Puzzle mutates plain array parameters; Lambda requires snapshots, while its
  C reference shares arrays and restores their contents. Removing snapshots
  requires a restoration/escape proof (**S9.1.3v2**). Triangl still needs stronger
  bounds for its changing search stack and indirect indices.
- GCbench, Deriv, Binarytrees and the AWFY record rows retain allocation and
  typed call/root boundaries. Native field stores are implemented; reducing
  those remaining boundaries must preserve **S9.3.1** and precise **D6.3** roots.
- Fasta and Base64 still construct text through repeated owned append; their C
  ports write contiguous byte buffers. Cube3D and Raytrace3D allocate and return
  small float arrays across calls, beyond the present allocation-free kernel
  proofs. Pidigits uses the existing decimal runtime while the C port uses
  fixed-capacity native big-integer structs. C2MIR is a performance reference,
  with different physical work, rather than a Lambda semantic oracle.

No benchmark algorithm or Lambda workload was rewritten to close these gaps.
The full 48-row harness continues checking the original output goldens.

### Reproduction

The root executable is the baseline's debug build. All measurements explicitly
select the isolated `release_native` binary; do not time the root executable.

```sh
make -C build/premake -f lambda.make config=release_native \
  TARGET=../../temp/lambda_mir_mvp/expand20/tuned-release.exe -j6
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/expand20/tuned-release.exe \
  PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/expand20/tuned-release.exe \
  --previous-mvp temp/lambda_mir_mvp/expand20/start-release.exe \
  --subset original --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/expand20/original-final.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/expand20/tuned-release.exe \
  --subset additional --runs 30 --warmups 2 \
  --output temp/lambda_mir_mvp/expand20/additional-final.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/expand20/tuned-release.exe \
  --previous-mvp temp/lambda_mir_mvp/expand20/start-release.exe \
  --subset original --case r7rs/sumfp --sumfp-batch 10000 --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/expand20/sumfp-final.json
```


## 2026-11-03: tuning the slower expansion rows

This follow-up tunes the existing 48 workloads against the committed expansion
(`63f73cd3a`). No Lambda benchmark algorithm, workload, golden, or native C
reference was changed. The original 28 remain the regression control.

### Lowering changes

- Contextual record constructors retain their enforced shape when all field
  admissions are proven. A local observed only by returns can adopt
  that return layout too. Unknown fields still use checked admission, and an
  ordinary error companion remains whenever a field or call can fail
  (**S11.4.1v5, D7.4.1**). Nullable native pointers need a full boundary check
  on their absence arm; an already admitted present value stays native.
- Read-only record cursors retain the same trusted shape across rebinding.
  Field mutation and `var` borrowing exclude that proof; assignment to another
  shape widens it. Exact shape identity is required: first-class type equality
  is not a physical record-layout certificate (**S12.2.1v3, D4.3.3**).
- Primitive inferred arrays retain a private domain only after their initializer
  and every indexed write pass the effect fixed point. Replacement, borrowing,
  a changing element kind, an unknown producer, or a potentially failed fill
  keeps the general path. No inferred observation becomes a user write contract
  (**S12.2.1v3, S11.4.1v5**).
- A nullable integer key retains the nullable element domain through nested
  indexing. Nullable boolean `not` and logical selection use native lanes;
  selection preserves the selected null, and encountered errors retain the
  general error path. Read-only compound array predicates can guard their read
  set independently of a taken arm that changes search indices
  (**S7.1.1v3, S3.4–S3.5**).
- Guarded allocating vector bodies reuse the existing extent and primitive-array
  admission proofs. Inputs remain precise owner roots and every access reloads
  its buffer after allocation. Native literals construct with the existing
  primitive constructor and destination certificate; short arrays and other
  layouts retain the original checked construction. These bodies never retain
  an interior pointer across GC (**D3.2.1, D6.3, S11.4.1v5**).
- Erased-value stores into plain, uncounted primitive arrays reuse the runtime's
  existing preplanned setter and live primitive-carrier proof. A valid
  compiler-owned certificate need not have the runtime interner's identity;
  repeatedly comparing its semantic type made Fasta regress despite identical
  hot-loop MIR. The shared helper still checks keys, values, COW ownership and
  the actual carrier; masks, invalid values and incompatible layouts retain
  checked admission (**D4.3.3, S9.1.3v2, S11.4.1v5**).
- Forwarded `var` arguments reuse an enclosing precise owner home when the actual
  occurs once. Aliased actuals keep the original call cells; replaced owners are
  reloaded on return. Record insertion avoids a duplicate capture after a named
  read that already captured its snapshot. The required capture remains for
  borrowed member/index results. Fresh map/array locals observed only by returns
  transfer their owner without a redundant snapshot capture; any other use
  preserves the ordinary capture (**S9.1.2–S9.1.3v2, S9.3.1, D6.3**).

Runtime types, layouts, GC and the production MIR lowering are unchanged. No
vendor code or benchmark-specific native replacement was introduced.

### Validation and measurement notes

The new `mir_mvp_proven_domains` fixture covers unrestricted inferred writes,
record-shape changes, invalid record fields, nullable pointer admission, short
vectors, nested boolean indexing, selected absence, erased-value checked writes,
recursive owner replacement, and caller snapshots. The ownership gate runs it
normally, under collection at every allocation, and with seeded collection plus
freed-memory poisoning. An
emission check requires native vector arithmetic/construction and preserves the
checked fallback. The focused gate has 42 groups.

The interpreter and AUTO match the complete new fixture. Full MIR has an existing
failure on `require_flag`: `flags[9] and true` is null and must fail its declared
`bool` return (**S3.4, S11.4.1v5**), but Full MIR returns false. Both the starting
and final release binaries reproduce that discrepancy. The MVP golden retains
the correct error result; no production harness or golden was weakened to hide
it. All selected benchmark goldens remain shared across the measured engines.
Detailed reproduction output is in
`temp/lambda_mir_mvp/tune48/cross-tier-final.log`.

### Release comparison protocol

The final candidate is release SHA-256
`15c90345fa9379b2ba5188d132057a108a9a61fd8bd9c828df6eed0889954d6b`.
The committed-expansion control is release SHA-256
`06422a07e8703838bb9b141ee08a2a5d6c5a0727b0f36cf0ddf58da984562119`.
Reports retain both executable identities, compiler-source hashes, benchmark
hashes, engine order, raw samples and bootstrap intervals. The unrelated
Radiant merge at `da601e608` does not change Lambda runtime sources relative to
the starting expansion.

Each row uses 100 randomized, interleaved samples after two warmups per engine.
The engines are the new MVP, the starting MVP, Full MIR from the same new
executable, and the independent native C2MIR driver. Build and aggregate-test
jobs finish before timing begins. These are self-reported execution-window
timings, excluding parsing, JIT compilation and process startup. All selected
Lambda and native C goldens must pass before measurement.

The per-row gates use 10,000 paired bootstrap resamples: a one-sided 95% upper
ratio below 1.0 against Full MIR and at most 1.20 against C2MIR. A regression
against the starting MVP is established only when the two-sided 95% lower
ratio is above 1.0. Inconclusive comparisons remain unqualified. Single-run
Sumfp remains below timer resolution, so the original-set totals substitute a
separate matched 10,000-invocation, 30-sample batch without changing either
kernel or treating a zero-duration sample as a pass.

### Final qualification

The original set qualifies **28/28 against Full MIR and 20/28 against both
targets**, including the matched Sumfp batch. The additions qualify **10/20
against Full MIR and 6/20 against both**, compared with 7/20 and 5/20 in the
committed expansion. Overall qualification is **38/48 against Full MIR and
26/48 against both targets**. The requested all-row milestone remains open.

No statistically established regression against the starting MVP appears in
either set. Twelve additions improve: Puzzle **11.4%**, Triangl **25.4%**,
GCbench **46.3%**, Deriv **40.8%**, Binarytrees **47.5%**, Fasta **2.9%**, List
**27.1%**, Permute **8.2%**, Storage **2.3%**, Towers **3.5%**, Cube3D **31.0%**
and Raytrace3D **41.6%**. Quicksort, Bounce, Brainfuck and Larceny Ray also
improve in the original set. An interval crossing 1.0 is recorded as no
established change, even when its median moves.

GCbench's one-sided upper ratios are **0.991× Full MIR and 1.080× C2MIR**;
Binarytrees' medians are below both references. List now clears the Full MIR
gate, but remains far from C2MIR. BENG NBody's median is **1.076× C2MIR**, while
its upper confidence bound is **1.320×**, so it remains unqualified in this
campaign. Counts use the retained final reports and their confidence gates.

Raw reports:

- [Added 20, 100 samples](../../test/benchmark/lambda_mvp/tune48_additional_2026-11-03.json).
- [Original 28, 100 samples and paired starting-MVP control](../../test/benchmark/lambda_mvp/tune48_regression28_2026-11-03.json).
- [Matched Sumfp batch, 30 samples](../../test/benchmark/lambda_mvp/tune48_sumfp_2026-11-03.json).

The following ratios are medians; **Both** follows the confidence gates above.

| Added benchmark | Previous MVP ms | MVP ms | Full ms | C2MIR ms | MVP / Full | MVP / C2MIR | Both |
|---|---:|---:|---:|---:|---:|---:|:---:|
| r7rs/cpstak | 0.143 | 0.142 | 0.260 | 0.233 | 0.547× | 0.611× | yes |
| larceny/diviter | 237.897 | 235.709 | 278.322 | 276.756 | 0.847× | 0.852× | yes |
| larceny/primes | 0.999 | 0.994 | 2.312 | 1.690 | 0.430× | 0.588× | yes |
| larceny/puzzle | 19.895 | 17.623 | 4.662 | 1.231 | 3.780× | 14.310× | no |
| larceny/triangl | 605.522 | 451.873 | 129.101 | 52.079 | 3.500× | 8.677× | no |
| larceny/gcbench | 130.945 | 70.364 | 72.219 | 66.131 | 0.974× | 1.064× | yes |
| larceny/deriv | 16.950 | 10.038 | 9.780 | 2.752 | 1.026× | 3.647× | no |
| beng/nbody | 1.427 | 1.737 | 14.140 | 1.615 | 0.123× | 1.076× | no |
| beng/binarytrees | 4.418 | 2.320 | 3.362 | 2.735 | 0.690× | 0.848× | yes |
| beng/fasta | 1.318 | 1.280 | 0.379 | 0.243 | 3.382× | 5.267× | no |
| beng/pidigits | 0.366 | 0.365 | 0.343 | 0.045 | 1.064× | 8.081× | no |
| awfy/list | 0.180 | 0.132 | 0.185 | 0.018 | 0.709× | 7.279× | no |
| awfy/permute | 0.073 | 0.067 | 0.067 | 0.021 | 1.000× | 3.191× | no |
| awfy/storage | 0.305 | 0.298 | 0.235 | 0.086 | 1.268× | 3.468× | no |
| awfy/towers | 0.302 | 0.291 | 0.183 | 0.025 | 1.593× | 11.592× | no |
| kostya/base64 | 3.471 | 3.423 | 5.214 | 0.502 | 0.657× | 6.825× | no |
| jetstream/nbody | 1.446 | 1.485 | 10.256 | 1.473 | 0.145× | 1.008× | yes |
| jetstream/navier_stokes | 52.517 | 52.151 | 78.598 | 35.166 | 0.664× | 1.483× | no |
| jetstream/cube3d | 8.232 | 5.684 | 4.643 | 0.434 | 1.224× | 13.084× | no |
| jetstream/raytrace3d | 34.549 | 20.188 | 11.696 | 1.986 | 1.726× | 10.163× | no |

| Original benchmark | MVP / previous MVP | Two-sided 95% interval | Result |
|---|---:|---:|---|
| r7rs/fib | 1.001× | 0.984–1.020 | no established change |
| r7rs/sum | 1.044× | 1.000–1.081 | no established change |
| r7rs/sumfp | 1.000× | 0.917–1.091 | no established change |
| awfy/mandelbrot | 0.998× | 0.988–1.008 | no established change |
| larceny/array1 | 1.000× | 0.964–1.037 | no established change |
| awfy/sieve | 1.062× | 1.000–1.125 | no established change |
| beng/spectralnorm | 0.977× | 0.950–1.028 | no established change |
| awfy/nbody | 1.005× | 0.972–1.040 | no established change |
| r7rs/tak | 1.007× | 0.973–1.041 | no established change |
| r7rs/ack | 1.004× | 0.991–1.015 | no established change |
| larceny/quicksort | 0.837× | 0.811–0.860 | faster |
| kostya/matmul | 1.003× | 0.992–1.015 | no established change |
| kostya/collatz | 1.004× | 0.985–1.017 | no established change |
| r7rs/fibfp | 0.968× | 0.951–1.007 | no established change |
| r7rs/fft | 1.029× | 1.000–1.088 | no established change |
| r7rs/nqueens | 0.993× | 0.965–1.009 | no established change |
| awfy/bounce | 0.837× | 0.816–0.894 | faster |
| awfy/queens | 1.000× | 0.917–1.000 | no established change |
| beng/fannkuch | 0.986× | 0.950–1.023 | no established change |
| kostya/primes | 0.993× | 0.960–1.056 | no established change |
| kostya/levenshtein | 1.007× | 0.980–1.025 | no established change |
| kostya/brainfuck | 0.948× | 0.936–0.957 | faster |
| larceny/paraffins | 0.979× | 0.946–1.011 | no established change |
| larceny/pnpoly | 1.009× | 0.980–1.035 | no established change |
| larceny/ray | 0.968× | 0.930–0.992 | faster |
| beng/mandelbrot | 1.006× | 0.983–1.019 | no established change |
| kostya/json_gen | 1.006× | 0.984–1.034 | no established change |
| larceny/divrec | 0.951× | 0.930–1.019 | no established change |

### Remaining performance work

The ten additions that do not clear Full MIR are Puzzle, Triangl, Deriv, Fasta,
Pidigits, Permute, Storage, Towers, Cube3D and Raytrace3D. The largest gains
reduce those gaps but do not complete their qualification. Puzzle still needs
a proof before removing snapshots across restored recursive mutation
(**S9.1.3v2**); Triangl retains checked indirect search indices. Deriv and the
AWFY record workloads still allocate owners and cross typed call/root
boundaries (**D4.2.5, D6.3**). Fasta retains the dynamic integer conversion
and text construction paths. Cube3D and Raytrace3D now have guarded allocating
vector bodies, while their callers still allocate and return arrays across
calls. Further extent and escape proofs must preserve **D3.2.1, D6.3 and
S9.3.1**. The existing decimal-versus-native-big-integer Pidigits gap remains.

The original eight C2MIR misses are Sieve, FFT, NQueens, Bounce, Queens,
Fannkuch, Brainfuck and Paraffins. Their Full MIR gates pass. Runtime types,
data models and benchmark algorithms remain unchanged.

### Aggregate baseline and focused failure rechecks

The final release passes **42/42 focused MVP groups**, including the new
semantic fixture in normal, collect-every-allocation and seeded/poisoned modes,
all 48 Lambda/C goldens, and constructor/vector/store MIR checks. All measured
executions also check their original golden. The new fixture matches the
interpreter and AUTO; its known Full MIR boolean-return discrepancy is recorded
above rather than hidden by a changed expectation.

The final `make test-lambda-baseline` run was sandboxed and reports
**8,617/8,641 with 24 failures**. This raw count includes a synthetic
binary-storage report failure instead of that executable's six real cases.
Server logs show denied loopback binds. Other failures include missing MIR/JSON
artifacts, REPL checks, a Docx timeout, and chart output checks. The aggregate
run is retained as failed, not described as an all-green baseline.

After timing finished, sequential rechecks with loopback access pass **630/630**:
server **152**, JS optimization **121**, runtime/error **301**, REPL **50**, and
binary storage **6**. The seven selected script rechecks pass **6/7**, including
the four socket scripts, Docx and the previously failing HTML adapter. Only
`chart_test_transform_controls` remains: its final two error-result checks
produce false. The unchanged starting release reproduces the same mismatch in
both AUTO and Full JIT; this is an existing chart/runtime issue, outside this
MVP lowering change. Its golden and harness remain intact. Starting-release
Docx and HTML controls also pass in both modes.

Evidence under `temp/lambda_mir_mvp/tune48/`:
`focused-setter.log`, `baseline-setter.log`, `baseline-test_summary.json`,
`sequential-rechecks.log`, `script-rechecks.json`,
`starting-script-controls.log`, and `cross-tier-final.log`.
Unrelated in-progress HTML adapter edits were preserved.

### Reproduction

Build the isolated release executable, then run correctness checks before the
timing commands. Finish aggregate tests before measuring. The root executable
is the baseline's debug build and is not used for timing.

```sh
make -C build/premake -f lambda.make config=release_native \
  TARGET=../../temp/lambda_mir_mvp/tune48/final-release.exe -j6
LAMBDA_TEST_EXE=temp/lambda_mir_mvp/tune48/final-release.exe \
  PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/test_lambda_mir_mvp.py
make test-lambda-baseline
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune48/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune48/start-release.exe \
  --subset additional --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/tune48/additional-final.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune48/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune48/start-release.exe \
  --subset original --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/tune48/original-final.json
PYTHONPYCACHEPREFIX=./temp/pycache python3 test/benchmark/lambda_mir_mvp.py \
  --release --exe temp/lambda_mir_mvp/tune48/final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune48/start-release.exe \
  --subset original --case r7rs/sumfp --sumfp-batch 10000 --runs 30 --warmups 2 \
  --output temp/lambda_mir_mvp/tune48/sumfp-final.json
```


## 2026-11-03: native array reads and closed scalar records

This pass builds on `26c8b39ee`, keeping the same 48 benchmark workloads and
original goldens. The release candidate is SHA-256
`4eeeca3cce3f3d06974cc60f050e5ca15d2c04ee12ec6284ab7ae1b2dd6514ad`.

### Lowering and runtime changes

- Nullable `int`/`float` conversion keeps the present numeric arm native and
  the absent arm on the original runtime conversion. A boxed join retains its
  admitted success contract so a typed consumer can use the native result
  without erasing the error alternative. Eligibility uses the numeric type ID:
  literal contracts have distinct objects but the same native lanes. Requiring
  canonical pointer identity had incorrectly introduced a collecting helper
  into a planned noncollecting literal conversion; the focused fixture now
  covers all four int/float literal combinations
  (**S4.1.2, S4.2.1, S7.9.3, D2.3.5**).
- The existing modular array update accepts a positive constant multiplier,
  named constant operands, a non-power-of-two positive modulus, and an explicit
  `int()` wrapper. Entry guards prove the multiplication and addition both
  remain in band before writing; a small remainder never hides an earlier
  saturation. Negative, poisoned, out-of-bounds, shared or incompatible cases
  retain the checked expression (**S4.1.2, S4.5.3–S4.5.4, S9.1.2**).
- Erased bindings can cache a physical `ArrayNum` float witness, separately
  from the existing integer witness. Owner replacement and indexed stores
  refresh it. A float comparison can consume a guarded element directly,
  retaining the generic comparison for other carriers and invalid positions.
  `ArrayNum` float slots are non-nullable; nullable native lanes use `Array`.
  Buffer addresses are reloaded from precise owner roots, and a witness is used
  only in its owning activation (**D2.3.5, D3.2.1, D4.3.3, D6.3**).
- Unknown loop predicates now share branch lowering: `and`/`or` preserve their
  evaluation order without constructing the selected Item, and a guarded float
  comparison can branch from its native predicate. A lexical condition target
  keeps an error as the loop outcome; it does not create a hidden return from
  the enclosing callable. Inlining isolates and restores that target
  (**S3.4–S3.5, S12.1.2v3**).
- Enforced `string[]` reads accept both boxed Items and native `String*` words.
  The existing eight-byte addressing and tag mask work for both carriers;
  bounds and alternate shapes retain their fallback (**D3.2.1, S7.1.1v3**).
- Scalar record writers reuse one shape/exclusivity witness when shared AST
  references prove a closed call graph: known scalar fields, no retention or
  owner replacement, and identical shapes at forwarded boundaries. Captures
  invalidate exclusivity, checked writes refresh it, and forwarding-only frames
  reserve just their precise owner home. Unused witnesses in recursive
  forwarding frames were removed before final qualification
  (**D4.2.5, S9.1.3v2, D6.3**).
- The shared `fn_index` preserves an encountered error base/key instead of
  converting it into absence. Error bases use the existing member reader so
  computed diagnostic fields agree with dot access; invalid reads of non-error
  values still return null. `proc/error_index_operands` covers both operand
  positions and computed metadata across interpreter, AUTO and Full JIT
  (**S7.1.1v3, S7.4.4, S7.9.3, S8.2.1v4**). The first baseline exposed the
  old `handler_current_error` golden's null for `^["message"]`; its expectation
  now retains the same message as `^.message` through this shared path.

These changes preserve the runtime types, data layouts and precise-root model.
The production MIR compiler and vendored code are unchanged.

### Correctness coverage

The release passes **47/47 focused groups**. Added fixtures cover optional
numeric conversions, affine overflow before modulo, float carriers and changing
element kinds, large/invalid indices, native string arrays, recursive record
updates, snapshots, owner replacement and short-circuit effects. The ownership
gate runs all of them normally, at every allocation, and with seeded collection
plus freed-memory poisoning. Emission checks require native comparison/branch,
string-lane and record-witness paths while retaining checked fallbacks.

The required `make test-lambda-baseline` also passes **8,647/8,647**: 2,112 input
parser cases and 6,535 runtime cases, including all 2,008 Lambda script cases.
The baseline rerun follows the computed-error-metadata correction described
above; its first run passed 8,646/8,647 and identified that stale golden.
The new index-error and metadata fixture matches interpreter, AUTO and Full
JIT on the archived release. General `error()` construction remains outside
MVP admission; MVP's float-comparison fixture checks its error-base fallback.

The new index-error, float-comparison, string-array and affine fixtures match
all three production execution tiers. Existing discrepancies remain explicit:
Full MIR loses nullable-int conversion failures and loop-guard errors, while
interpreter/AUTO match those goldens; all three production tiers observe a
record mutation through a plain sibling argument where both the starting and
new MVP preserve its required snapshot (**S7.9.3, S3.5, S9.1.3v2**). These
fixtures remain under `test/lambda/mvp/`; their expected results were not
weakened to match a production defect. Reproductions are in
`temp/lambda_mir_mvp/tune48c/cross-tier-final.log`.

### Measurement protocol

Full MIR and previous MVP both use the unchanged archived starting release,
SHA-256 `15c90345fa9379b2ba5188d132057a108a9a61fd8bd9c828df6eed0889954d6b`.
This freezes the reference while a shared runtime helper changes. C2MIR remains
the independent native C driver. Both references must match the original
benchmark golden on every measured execution.

The final campaign schedules each row independently with 100 randomized,
interleaved samples after two warmups. A separate matched 10,000-invocation
Sumfp batch uses 30 samples. Timing covers each workload's existing execution
window, excluding process startup and JIT compilation. The 10,000-resample
paired bootstrap gates remain a one-sided 95% upper ratio below 1.0 against
Full MIR and at most 1.20 against C2MIR; regressions use a two-sided 95% lower
ratio above 1.0 against the starting MVP.

Early probes overlapped an unrelated interpreter sweep and remain diagnostic.
The sweep was stopped with explicit user authorization before rebuilding the
shared debug executable. Aggregate validation finishes before timing begins.
The coordinator waits for other builds/tests before each row and discards an
interrupted row if competing work starts, recording the reason independently
of its timings. Raw reports retain per-row engine order, executable and source
hashes, samples, intervals and scheduling records.

The earlier `tune48b` campaign was stopped when review found the literal-contract
regression. Its completed 20-row expansion and partial original-row report are
retained under `temp/lambda_mir_mvp/tune48b/`; they do not qualify the corrected
binary. The fresh `tune48c` campaign uses experiment-source SHA-256
`d2a693a8c0640b6da0027be2825e8a3a3a55e1dea579cb9d779bf50b90117946`
for every final row.

The final campaign qualifies **40/48 against Full MIR and 27/48 against both
targets**, up from 38/48 and 26/48 in the preceding campaign. The original 28
retain their 28 Full MIR and 20 combined qualifications, with no statistically
established regressions. Six rows improve against the interleaved starting MVP:
Quicksort, Puzzle, Triangl, Deriv, Fasta and Permute.

Fasta improves from **1.2725 to 0.3495 ms** (3.64×), beating Full MIR at
0.3750 ms. Permute improves from **0.0680 to 0.0640 ms**, beating Full MIR at
0.0690 ms. Their one-sided Full MIR upper bounds qualify below 1.00. Neither
row yet meets the C2MIR allowance. BENG NBody also qualifies against C2MIR in
this fresh campaign, but its paired comparison establishes no speedup against
the starting MVP; a changed qualification alone is not evidence of a code gain.

The preliminary candidate's Binarytrees regression flag does not recur on the
final binary. A separate **200-sample confirmation** reports final MVP
2.4365 ms versus starting MVP 2.3955 ms, with a two-sided 95% ratio interval
**0.9875–1.0351**. This establishes neither a regression nor a speedup.
The pointer-normalized starting/final MIR is identical (2,623 lines); it does
not identify a JIT-code change to tune. Both performance gates still pass.
[Confirmation samples](../../test/benchmark/lambda_mvp/tune48_native_reads_binarytrees_confirmation_2026-11-03.json)
are retained separately from the primary campaign.

**The milestone remains open:** 21 rows miss at least one target. Eight added
rows still fail the Full MIR gate: Puzzle, Triangl, Deriv, Pidigits, Storage,
Towers, Cube3D and Raytrace3D. Their measurements remain in the denominator.

### Release results

All values below are median execution-window milliseconds. Qualification uses
the one-sided 95% upper ratio bounds in the two confidence columns; median
ratios alone do not qualify a row. `Full` means the Full MIR gate only,
`both` means Full MIR plus the 20% C2MIR allowance, and `—` meets neither.

**20 additions: 12/20 against Full MIR; 7/20 against both targets.**

| Workload | Starting MVP | MVP | Full MIR | C2MIR | Full upper | C2MIR upper | Gates |
|---|---:|---:|---:|---:|---:|---:|---|
| r7rs/cpstak | 0.1500 | 0.1495 | 0.2695 | 0.2371 | 0.5672 | 0.6438 | both |
| larceny/diviter | 234.3815 | 232.8945 | 267.9605 | 267.0031 | 0.8777 | 0.8831 | both |
| larceny/primes | 0.9615 | 0.9830 | 2.2530 | 1.5851 | 0.4464 | 0.6448 | both |
| larceny/puzzle | 18.4700 | 18.2415 | 4.8925 | 1.2640 | 3.808 | 14.66 | — |
| larceny/triangl | 456.4555 | 446.7925 | 129.5415 | 52.6560 | 3.504 | 8.696 | — |
| larceny/gcbench | 71.4610 | 71.1755 | 73.5155 | 67.9246 | 0.981 | 1.07 | both |
| larceny/deriv | 8.0130 | 7.9220 | 7.3700 | 2.6169 | 1.086 | 3.116 | — |
| beng/nbody | 1.4955 | 1.4760 | 10.3075 | 1.4821 | 0.1452 | 1.021 | both |
| beng/binarytrees | 2.3920 | 2.4135 | 3.5590 | 2.8286 | 0.6959 | 0.8725 | both |
| beng/fasta | 1.2725 | 0.3495 | 0.3750 | 0.2450 | 0.9543 | 1.473 | Full |
| beng/pidigits | 0.3675 | 0.3690 | 0.3595 | 0.0455 | 1.066 | 8.329 | — |
| awfy/list | 0.1305 | 0.1300 | 0.1870 | 0.0181 | 0.7081 | 7.251 | Full |
| awfy/permute | 0.0680 | 0.0640 | 0.0690 | 0.0220 | 0.9552 | 3.048 | Full |
| awfy/storage | 0.3030 | 0.2970 | 0.2390 | 0.0885 | 1.293 | 3.477 | — |
| awfy/towers | 0.3030 | 0.3035 | 0.1905 | 0.0259 | 1.626 | 12.13 | — |
| kostya/base64 | 3.4145 | 3.4765 | 5.1460 | 0.5229 | 0.6858 | 6.905 | Full |
| jetstream/nbody | 1.4875 | 1.4315 | 10.3495 | 1.4890 | 0.1422 | 0.9952 | both |
| jetstream/navier_stokes | 51.9620 | 51.3355 | 78.6375 | 35.1416 | 0.6655 | 1.494 | Full |
| jetstream/cube3d | 5.4825 | 5.3500 | 4.4015 | 0.4050 | 1.256 | 13.6 | — |
| jetstream/raytrace3d | 20.4045 | 20.3290 | 11.7705 | 1.9296 | 1.759 | 10.71 | — |

**Original 28: 28/28 against Full MIR; 20/28 against both targets.**

| Workload | Starting MVP | MVP | Full MIR | C2MIR | Full upper | C2MIR upper | Gates |
|---|---:|---:|---:|---:|---:|---:|---|
| r7rs/fib | 0.5940 | 0.6065 | 1.2050 | 1.0421 | 0.5153 | 0.5891 | both |
| r7rs/sum | 0.0990 | 0.1010 | 0.2685 | 0.2686 | 0.3829 | 0.3847 | both |
| r7rs/sumfp (10,000× batch) | 0.0110 | 0.0110 | 729.6185 | 745.3840 | 1.699e-05 | 1.626e-05 | both |
| awfy/mandelbrot | 21.8975 | 22.1220 | 27.9995 | 21.9661 | 0.8024 | 1.022 | both |
| larceny/array1 | 0.1095 | 0.1085 | 1.4085 | 0.2941 | 0.08023 | 0.3806 | both |
| awfy/sieve | 0.0170 | 0.0150 | 0.0280 | 0.0129 | 0.5714 | 1.311 | Full |
| beng/spectralnorm | 0.2585 | 0.2550 | 1.1890 | 0.3610 | 0.2209 | 0.7201 | both |
| awfy/nbody | 1.4910 | 1.4475 | 2.6990 | 1.4910 | 0.5512 | 1.018 | both |
| r7rs/tak | 0.0725 | 0.0730 | 0.1325 | 0.1196 | 0.5769 | 0.6327 | both |
| r7rs/ack | 5.6835 | 5.6365 | 10.4210 | 12.1294 | 0.5447 | 0.4696 | both |
| larceny/quicksort | 0.2355 | 0.2250 | 0.5860 | 0.2209 | 0.3963 | 1.05 | both |
| kostya/matmul | 4.6525 | 4.6705 | 19.6460 | 6.1465 | 0.24 | 0.7729 | both |
| kostya/collatz | 173.9615 | 175.8940 | 300.4950 | 225.6290 | 0.5936 | 0.793 | both |
| r7rs/fibfp | 0.5695 | 0.5825 | 0.6540 | 1.1055 | 0.9109 | 0.5378 | both |
| r7rs/fft | 0.0350 | 0.0350 | 0.0780 | 0.0190 | 0.4615 | 1.89 | Full |
| r7rs/nqueens | 0.5270 | 0.5290 | 0.6020 | 0.3455 | 0.9117 | 1.585 | Full |
| awfy/bounce | 0.0400 | 0.0395 | 0.0590 | 0.0220 | 0.6949 | 1.948 | Full |
| awfy/queens | 0.0225 | 0.0220 | 0.0610 | 0.0161 | 0.377 | 1.427 | Full |
| beng/fannkuch | 0.2180 | 0.2180 | 0.2730 | 0.1460 | 0.8277 | 1.527 | Full |
| kostya/primes | 0.9860 | 0.9735 | 2.2630 | 1.6704 | 0.4396 | 0.5975 | both |
| kostya/levenshtein | 0.8935 | 0.8905 | 1.8480 | 0.8020 | 0.492 | 1.126 | both |
| kostya/brainfuck | 51.6870 | 51.8415 | 78.8950 | 26.9880 | 0.6675 | 1.941 | Full |
| larceny/paraffins | 0.0900 | 0.0895 | 0.1345 | 0.0400 | 0.6861 | 2.304 | Full |
| larceny/pnpoly | 1.7990 | 1.8150 | 2.4140 | 1.8914 | 0.7681 | 0.9755 | both |
| larceny/ray | 0.1175 | 0.1210 | 0.2165 | 0.1460 | 0.5817 | 0.8516 | both |
| beng/mandelbrot | 11.8095 | 11.8075 | 13.2340 | 11.8945 | 0.9137 | 1.004 | both |
| kostya/json_gen | 1.4330 | 1.3980 | 4.7620 | 1.3984 | 0.3035 | 1.036 | both |
| larceny/divrec | 0.8980 | 0.9000 | 1.2170 | 5.0300 | 0.7775 | 0.1895 | both |

Raw samples and provenance:

- [20 additions](../../test/benchmark/lambda_mvp/tune48_native_reads_2026-11-03.json).
- [Original 28](../../test/benchmark/lambda_mvp/tune48_native_reads_regression28_2026-11-03.json).
- [Matched Sumfp batch](../../test/benchmark/lambda_mvp/tune48_native_reads_sumfp_2026-11-03.json).

The original unbatched Sumfp observations remain in the raw 28-row report.
They do not qualify at the clock resolution; only the separately identified
matched batch supplies its contribution to the counts above.

### Reproduction

The archived final binary is `temp/lambda_mir_mvp/tune48c/final-release.exe`;
`start-release.exe` in the same directory is the frozen Full MIR / previous
MVP reference. `qualify.py` records the per-row process scheduling and discarded
attempts. It waits for two quiet process checks, one second apart, before each
row and monitors competing work during execution. A foreign baseline paused
this campaign; interrupted rows were rerun after that work finished.

```sh
LAMBDA_TEST_EXE=./temp/lambda_mir_mvp/tune48c/final-release.exe \
  python3 test/benchmark/test_lambda_mir_mvp.py
make test-lambda-baseline
python3 test/benchmark/lambda_mir_mvp.py --release \
  --exe temp/lambda_mir_mvp/tune48c/final-release.exe \
  --control temp/lambda_mir_mvp/tune48c/start-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune48c/start-release.exe \
  --subset additional --runs 100 --warmups 2 \
  --output temp/lambda_mir_mvp/recheck_additional.json
```

Use `--subset original` for the 28-row check and separately add
`--case r7rs/sumfp --sumfp-batch 10000 --runs 30` for its matched batch.
The baseline leaves the shared executable in debug configuration; all reported
performance uses the archived release binaries; see
[Developer Guide §7.3](../../doc/dev/Developer_Guide.md#73-build-gotchas).


## 2026-11-03: immutable indirect keys and decimal consumers

This pass targets the remaining Full MIR misses, especially Triangl and
Pidigits. Authority: **S3.4–S3.5**, checked absence **S7.1.1v3, S7.9.3**,
immutable snapshots and borrows **S9.1.2–S9.1.3v2**, decimal arithmetic
**S4.4.1–S4.4.2, S4.5.1–S4.5.4**, representation and precise ownership
**D2.3.5, D6.3**, and checked compiler facts **D8.2.6**. No formal ruling
changes.

### Root causes and lowering

Triangl reads its mutable board through immutable move tables, for example
`board[mfrom[mi]]`. The ordinary pure-expression query did not admit an index
expression, so each read captured the board before computing the key. That
froze it and forced later stores through copy-on-write. The planner already
has a stronger indexed-key proof that admits readonly literal `int[]` tables,
including nested lookups. Array reads now reuse that proof for receiver
ownership and cached native witnesses. Mutable lookup tables and effectful
keys retain capture and receiver-before-key evaluation. Checked bounds,
nullable results, alternate carriers and rooted owner reloads remain shared
with the existing read lowering.

Predicate lowering guards each selected `and`/`or` operand in evaluation
order. Previously a whole-condition guard prefetched immutable keys in arms
that would be skipped, then the selected body evaluated them again. Native
checked Boolean reads now branch from the loaded byte directly; absence takes
the false arm. Their alternate-carrier path still uses `fn_index`. Unary
`not` reverses branch demand while retaining lexical error propagation. Value
demand continues to materialize ordinary nullable values.

Decimal-heavy Pidigits emitted compact-int speculation even when operand
contracts made the int-tag guards impossible. Such domains now call their
existing operator helper directly; erased or union domains retain guarded
integer speculation. Decimal arithmetic publishes only its successful decimal
domain, including decimal poison values, through the existing admitted-result
fact. Error transport remains independent, and unknown arguments retain
admission checks. The existing bounded, rooted inliner also accepts immutable
decimal results. String recipes with nonnullable decimal fragments use the
shared `fn_strcat_values` formatter, including its existing owned-prefix path.
No runtime representation, runtime helper or vendor source changes.

### Correctness and measurement

The release passes **49/49 focused groups**, including benchmark goldens,
normal collection, collection on every allocation with freed-memory poisoning,
and seeded collection with poisoning. New fixture/golden pairs cover nested
immutable keys, mutable keys, receiver replacement during key evaluation,
negative and full-width out-of-range keys, skipped Boolean arms, decimal
inlining across allocations, failed admission, decimal poison propagation and
string snapshots. MIR assertions check capture boundaries, selected-arm
ordering, direct Boolean branches, retained checked fallbacks, and removal of
impossible int speculation and redundant successful decimal admission.

The control was rebuilt from clean merged revision
`1b0927e30e203096b114e92b94d6ea9b6b401ef7`, rather than reusing the prior
campaign's executable: shared numeric/error code changed in that merge.
The provenance corpus now also covers `lambda-eval-num.cpp` and the decimal
implementation/header, which were split out of previously hashed files.
The expanded 38-file starting corpus is reconstructed from that revision
under `temp/lambda_mir_mvp/tune48d/base_sources/`; the original 35-file capture
is retained separately. Full MIR and the previous MVP use the same frozen
starting release. The C2MIR comparator remains the independent C benchmark
ports, not a restored Lambda C-text backend (**D8.2.4**).

The three changed fixtures were also checked on the frozen starting and final
releases under interpreter, AUTO and Full JIT. Starting and final outputs agree
in every tier. Interpreter/AUTO match all goldens. Existing Full JIT differences
remain: effectful receiver replacement in `mir_mvp_indirect_keys` reads the new
receiver, and predicate errors in `mir_mvp_predicate_branch` do not reach the
expected error outcome. These are recorded MVP-only fixtures, not newly
introduced shared-runtime changes. Raw cross-tier outputs are retained in
`temp/lambda_mir_mvp/tune48d/cross-tier-final.json`.

The required aggregate baseline passes **8,647/8,647 cases**: 2,112 input
parser cases and 6,535 runtime cases, including 2,008 Lambda script cases.
The final archived release also passes all 49 focused groups after timing.

### Qualified results and remaining gaps

The primary 100-sample campaign qualifies **40/48 against Full MIR and 25/48
against both targets**. The original 28 qualify **28/28 against Full MIR and
19/28 against both**, including the separate 30-sample matched Sumfp batch.
The additions qualify **12/20 against Full MIR and 6/20 against both**.
Single-invocation Sumfp is still sub-resolution and unqualified in its raw
report; its matched batch retains all 10,000 invocations per sample.

Triangl improves **433.4470 → 99.6075 ms**, about **4.35×**, and now beats
Full MIR at **124.2505 ms**. Its one-sided Full MIR upper bound is **0.8104**.
C2MIR is **50.6331 ms**, so the C2MIR target remains open. The emitted module
shrinks from 3,823 to 3,435 lines and removes eight hot capture calls; the two
remaining captures belong to other boundaries.

Pidigits improves **0.3735 → 0.3550 ms**, about **5%**, with a statistically
supported paired gain. Full MIR is **0.3580 ms**, but the upper ratio bound
**1.0173** does not qualify a Full MIR win. C2MIR is **0.0457 ms**. Its emitted
module shrinks from 2,385 to 1,203 lines after impossible speculation,
decimal admission and call boundaries are removed.

**Regression checks:** the primary run flags only Levenshtein, at 0.9125 ms
versus starting MVP 0.8755 ms (two-sided ratio interval 1.0056–1.0646). The
prearranged separate **200-sample confirmation** does not reproduce it:
**0.8830 versus 0.8890 ms**, with a two-sided 95% ratio interval
**0.9787–1.0045**. That confirmation passes both targets against Full MIR
1.8700 ms and C2MIR 0.7988 ms. Starting/final pointer-normalized MIR is
identical at 2,514 lines. There is no reproduced regression in the original
28; the primary flag and confirmation remain separate, visible reports.
The primary qualification count above is not replaced with the more favorable
confirmation result.

Binarytrees shows a paired gain in the primary samples, but its normalized
MIR is identical at 2,623 lines. This pass does not attribute that sample
difference to a compiler change. GCbench also has identical MIR (2,741 lines)
and no paired regression: MVP 71.1555 ms versus starting MVP 71.0730 ms.
Its Full MIR comparison is 70.7265 ms and the Full upper bound is 1.0183, so
it misses that gate. It still satisfies the C2MIR allowance.

The preceding campaign's **40 Full / 27 combined** result and this campaign's
**40 / 25** primary result are both retained. Triangl gains a Full win while
GCbench loses its marginal Full qualification; GCbench and Levenshtein account
for the two lost primary combined qualifications. Full MIR was rebuilt from
the newer merged revision for this campaign, and all timings were collected
again. These gate changes do not establish an MVP slowdown: GCbench's
starting/final MIR is identical and its paired comparison is unchanged;
Levenshtein's flag fails independent confirmation. They remain target misses
in the primary denominator.

**The milestone remains open.** Eight rows miss the primary Full MIR gate:
Puzzle, GCbench, Deriv, Pidigits, Storage, Towers, Cube3D and Raytrace3D.
Twenty-three rows miss at least one primary target. Prioritize their measured
hot paths; do not substitute median parity or a faster diagnostic sample for
qualification.

### Complete release tables

Values are median execution-window milliseconds, excluding startup and JIT.
Each primary row has 100 samples per engine and two warmups, with randomized
interleaved order. `Full` requires the one-sided 95% upper ratio bound below
1.00; `both` also requires the C2MIR upper bound at most 1.20. Every row and
sample remains present. The coordinator discarded one interrupted Diviter
attempt when a separate Test262 job began, waited for that job to finish,
and reran the entire row without trimming timings.

**20 additions: 12 Full; 6 both.**

| Workload | Starting MVP | MVP | Full MIR | C2MIR | Full upper | C2MIR upper | Gates |
|---|---:|---:|---:|---:|---:|---:|---|
| r7rs/cpstak | 0.1460 | 0.1475 | 0.2685 | 0.2371 | 0.5627 | 0.6348 | both |
| larceny/diviter | 230.0405 | 230.4145 | 270.9405 | 269.2316 | 0.8550 | 0.8707 | both |
| larceny/primes | 0.9415 | 0.9415 | 2.2240 | 1.5990 | 0.4315 | 0.6007 | both |
| larceny/puzzle | 17.3480 | 17.2260 | 4.6965 | 1.2295 | 3.7179 | 14.2656 | — |
| larceny/triangl | 433.4470 | 99.6075 | 124.2505 | 50.6331 | 0.8104 | 1.9892 | Full |
| larceny/gcbench | 71.0730 | 71.1555 | 70.7265 | 68.5226 | 1.0183 | 1.0448 | — |
| larceny/deriv | 7.7565 | 7.7750 | 7.2130 | 2.5975 | 1.0855 | 3.0343 | — |
| beng/nbody | 1.4555 | 1.4865 | 10.3300 | 1.4895 | 0.1453 | 1.0420 | both |
| beng/binarytrees | 2.5235 | 2.4655 | 3.6000 | 2.8855 | 0.7009 | 0.8687 | both |
| beng/fasta | 0.3470 | 0.3480 | 0.3765 | 0.2451 | 0.9307 | 1.4501 | Full |
| beng/pidigits | 0.3735 | 0.3550 | 0.3580 | 0.0457 | 1.0173 | 7.9706 | — |
| awfy/list | 0.1260 | 0.1300 | 0.1860 | 0.0190 | 0.7228 | 7.1405 | Full |
| awfy/permute | 0.0690 | 0.0660 | 0.0690 | 0.0226 | 0.9714 | 3.0482 | Full |
| awfy/storage | 0.2995 | 0.3010 | 0.2430 | 0.0906 | 1.2678 | 3.4003 | — |
| awfy/towers | 0.3000 | 0.3015 | 0.1880 | 0.0269 | 1.6519 | 11.3945 | — |
| kostya/base64 | 3.4975 | 3.5030 | 5.1770 | 0.5310 | 0.6861 | 6.6628 | Full |
| jetstream/nbody | 1.4875 | 1.4940 | 10.3840 | 1.4845 | 0.1454 | 1.0240 | both |
| jetstream/navier_stokes | 52.2695 | 52.3825 | 78.5265 | 35.2566 | 0.6736 | 1.4942 | Full |
| jetstream/cube3d | 5.7445 | 5.7745 | 4.7910 | 0.4385 | 1.2332 | 13.3477 | — |
| jetstream/raytrace3d | 20.6000 | 20.6685 | 11.9530 | 1.9810 | 1.7456 | 10.6641 | — |

**Original 28: 28 Full; 19 both in the primary comparison.** The Sumfp
row below uses the separate matched batch, not its zero-duration single-run
samples. Levenshtein's independent confirmation follows the table.

| Workload | Starting MVP | MVP | Full MIR | C2MIR | Full upper | C2MIR upper | Gates |
|---|---:|---:|---:|---:|---:|---:|---|
| r7rs/fib | 0.6080 | 0.6045 | 1.2135 | 1.0630 | 0.5079 | 0.5821 | both |
| r7rs/sum | 0.1035 | 0.1025 | 0.2730 | 0.2810 | 0.3820 | 0.3704 | both |
| r7rs/sumfp (10,000×) | 0.0110 | 0.0110 | 732.0485 | 749.8407 | 0.0000 | 0.0000 | both |
| awfy/mandelbrot | 22.3650 | 22.2800 | 28.2995 | 22.2681 | 0.7923 | 1.0080 | both |
| larceny/array1 | 0.1075 | 0.1095 | 1.4145 | 0.2947 | 0.0793 | 0.3807 | both |
| awfy/sieve | 0.0160 | 0.0160 | 0.0270 | 0.0120 | 0.6296 | 1.4210 | Full |
| beng/spectralnorm | 0.2575 | 0.2535 | 1.1815 | 0.3610 | 0.2188 | 0.7158 | both |
| awfy/nbody | 1.4745 | 1.4860 | 2.7075 | 1.4895 | 0.5558 | 1.0137 | both |
| r7rs/tak | 0.0740 | 0.0730 | 0.1330 | 0.1179 | 0.5692 | 0.6360 | both |
| r7rs/ack | 5.6790 | 5.6610 | 10.3215 | 12.1110 | 0.5552 | 0.4731 | both |
| larceny/quicksort | 0.2280 | 0.2305 | 0.5940 | 0.2271 | 0.3997 | 1.0484 | both |
| kostya/matmul | 4.6285 | 4.6510 | 19.7640 | 6.1650 | 0.2368 | 0.7605 | both |
| kostya/collatz | 176.8205 | 177.0515 | 301.7050 | 226.2095 | 0.5916 | 0.7861 | both |
| r7rs/fibfp | 0.5885 | 0.5920 | 0.6815 | 1.1274 | 0.8816 | 0.5333 | both |
| r7rs/fft | 0.0350 | 0.0370 | 0.0780 | 0.0190 | 0.4805 | 1.9430 | Full |
| r7rs/nqueens | 0.5505 | 0.5425 | 0.6085 | 0.3560 | 0.9086 | 1.5509 | Full |
| awfy/bounce | 0.0400 | 0.0390 | 0.0590 | 0.0210 | 0.6897 | 1.9481 | Full |
| awfy/queens | 0.0220 | 0.0230 | 0.0620 | 0.0159 | 0.3770 | 1.5195 | Full |
| beng/fannkuch | 0.2195 | 0.2175 | 0.2735 | 0.1516 | 0.8060 | 1.4711 | Full |
| kostya/primes | 0.9810 | 0.9865 | 2.3010 | 1.6525 | 0.4413 | 0.6156 | both |
| kostya/levenshtein | 0.8755 | 0.9125 | 1.7900 | 0.7485 | 0.5170 | 1.2376 | Full |
| kostya/brainfuck | 52.2985 | 52.2850 | 83.1290 | 27.0754 | 0.6354 | 1.9543 | Full |
| larceny/paraffins | 0.0910 | 0.0920 | 0.1370 | 0.0415 | 0.6957 | 2.3477 | Full |
| larceny/pnpoly | 1.8285 | 1.8075 | 2.3555 | 1.8716 | 0.7877 | 0.9830 | both |
| larceny/ray | 0.1220 | 0.1210 | 0.2160 | 0.1444 | 0.5769 | 0.8516 | both |
| beng/mandelbrot | 11.7570 | 11.8535 | 13.4090 | 11.9661 | 0.8949 | 0.9989 | both |
| kostya/json_gen | 1.4385 | 1.4375 | 4.7490 | 1.4147 | 0.3085 | 1.0351 | both |
| larceny/divrec | 0.9580 | 0.9765 | 1.2700 | 5.1000 | 0.7799 | 0.1941 | both |

**Levenshtein confirmation, 200 fresh samples per engine:**

| Workload | Starting MVP | MVP | Full MIR | C2MIR | Full upper | C2MIR upper | Gates |
|---|---:|---:|---:|---:|---:|---:|---|
| kostya/levenshtein | 0.8890 | 0.8830 | 1.8700 | 0.7988 | 0.4772 | 1.1158 | both |

### Evidence and reproduction

- [20 additions](../../test/benchmark/lambda_mvp/tune48_indirect_keys_2026-11-03.json).
- [Original 28](../../test/benchmark/lambda_mvp/tune48_indirect_keys_regression28_2026-11-03.json).
- [Matched Sumfp batch](../../test/benchmark/lambda_mvp/tune48_indirect_keys_sumfp_2026-11-03.json).
- [Levenshtein confirmation](../../test/benchmark/lambda_mvp/tune48_indirect_keys_levenshtein_confirmation_2026-11-03.json).

Final release SHA-256:
`faf6de30e2d533bbe0a16d61ae460e582e412efb8efb31c4471f8e077e558793`.
Starting release SHA-256:
`9d1a5cdf1e13685707f8d9f26ab7d49d02c2a1cd5ff1d5d27a22e345db3c68de`.
The final 38-file experiment corpus is
`cc7fefad76fd63c80ed4e959dc08d3424374a58f007b698a5c96281ad110b087`;
the expanded starting corpus is
`2fc738d291dab22ba50d82b6c8e5127125d041ae35e36d766fd12f812ba2038a`.

All case Lambda/C/golden hashes, executable hashes, sample counts, execution
orders and per-row report hashes were audited after timing. The engine source
corpus still matches the frozen release. Subsequent unrelated parser/spec
edits in the shared checkout are not part of this binary or tuning pass.
Intermediate binaries, build logs, baseline artifacts, timing/discard logs,
normalized MIR/diffs and cross-tier outputs remain under
`temp/lambda_mir_mvp/tune48d/`. Earlier reports and diagnostic stages are retained.

```bash
LAMBDA_TEST_EXE=./temp/lambda_mir_mvp/tune48d/final-release.exe \
  python3 test/benchmark/test_lambda_mir_mvp.py
python3 test/benchmark/lambda_mir_mvp.py --release --subset additional \
  --exe temp/lambda_mir_mvp/tune48d/final-release.exe \
  --control temp/lambda_mir_mvp/tune48d/start-release.exe \
  --previous-mvp temp/lambda_mir_mvp/tune48d/start-release.exe \
  --runs 100 --warmups 2
```

Use `--subset original` for the original rows; separately add
`--case r7rs/sumfp --sumfp-batch 10000 --runs 30` for the matched batch. Use
`--subset original --case kostya/levenshtein --runs 200` for the independent
confirmation. The baseline leaves the shared `lambda.exe` in debug mode;
all performance results use the archived release binaries.


## 2026-11-03: current-source outcomes and constructor admission

The previous qualification predates shared error-contract and source migrations.
Reusing its counts would compare different programs. This pass first restores
current-source coverage, then measures the resulting compiler against a frozen,
minimally adapted MVP control and Full MIR in the same archived executable.
C2MIR remains the independent C-port reference; the removed Lambda C-text backend
is not involved (**D1.6**).

**Root causes and implementation.** A normalized fallible return has a successful
native contract and an error alternative (**S11.4.2v3, D6.2.1**). MVP now plans
both from `TypeFunc::return_contract`; ordinary error values never enter a native
success slot (**D2.3.5**). Deferred initialization and whole-variable reassignment
store their admitted value or validation error and continue (**S7.7.2v2,
S7.7.4v2**). Explicit propagation selects the return path, after `var` write-back
and with ordinary enclosing-return validation (**S7.6.3v3, S11.4.10**). Parameter
failure still rejects the original argument before callee entry (**S7.7.3**).
Raw-return builtin helpers classify error arguments before entering their C ABI
(**S7.9.3**). Unfiltered handlers evaluate their operand once and preserve a clean
result or the selected recovery result (**S7.6.1v9, S7.6.8**).

Widening a binding to `T | error` had also erased its constructor destination.
Recursive maps then used generic construction followed by repeated deep
validation. Construction now uses the original declared admission target;
storage and consumers retain the outcome type. Typed fills with a caret
specialize only when the element is proved compatible and the destination is
uncounted: a constructor failure propagates, while later destination validation
still becomes a stored outcome (**S7.7.2v2, S11.4.10**).

The compiler retains successful-domain facts independently of the error Item.
Compact scalar outcomes avoid COW and scalar-home copies; unknown and wide
scalar domains keep their existing ownership (**D6.3**). Adjacent single-use
validation/discharge declarations fuse without an intermediate boxed result;
additional consumers or intervening effects prevent that proof. Native nullable
returns admit their success lane directly, keeping full outcome validation on
Item paths. Closed noncollecting bodies may refine int/bool outcome locals only
when every initializer and write passes the guarded proof; allocating variants
restore the boxed domains. Builtin carets retain existing numeric producer facts
so they can participate in that proof (**D8.2.6**).

**Validation and qualification.** See the checkpoint for active artifacts. The
new `mir_mvp_propagation` golden covers deferred outcomes, native/error return
pairs, argument admission, write-back ordering, snapshots, empty arrays, and
constructor-versus-destination failures in three GC modes. Existing assertions
remain intact. The broad MVP gate still exposes legacy fixture migrations,
changed hidden-exit expectations and emission gaps; these are not benchmark
correctness passes. The benchmark runner's optional `--record-failures` retains
failed-engine diagnostics and partial samples, leaves their comparison gates
unqualified, continues unaffected engines, and exits nonzero. A regression probe
verifies that a failed engine cannot count as a performance win.


The scheduling controls address concurrent work without selecting timings by
value. `--pause-for-jobs` waits for active builds/test suites, checks between
engine observations, retains completed rounds, and retries a whole interrupted
round. Each discarded round records its attempted order, observed timings and
competing PIDs. A deterministic probe injects an interrupted 999 ms observation
and verifies that every retained engine still has exactly the requested sample
count and that the interrupted pair is absent. Failed engines remain failed;
scheduling never converts a crash into a win. `--source-manifest` verifies the
candidate executable SHA before using its archived compiler identity, and records
the observed moving checkout separately. This was necessary after concurrent
frontend work changed `build_ast.cpp` and replaced `lambda.exe` during validation.

The required baseline's input stage passes **2,112/2,112**. Its later Lambda stage
was contaminated by that executable replacement. All **494** affected script
cases, **3** standard-library cases and **921** math-corpus cases pass in isolated
reruns against the pinned release; the original failing output is preserved.
The final focused aggregate runs **53 groups**: **36 pass**, with **58 failing
subtests across 17 groups**. These include the previously deferred fixture
migrations, changed hidden-exit/string-error expectations, missing emission
optimizations and Full MIR's nonzero FFT crash. The 48 MVP benchmark goldens,
new outcome tests, recursive-constructor GC probe and scheduler/provenance probes
pass. Neither historical focused counts nor an uncontended all-green aggregate
are claimed for this tree.


**Guarded float outcomes.** Phase-one qualification exposed a large remaining
loss in typed numeric kernels. A deferred `float` binding occupied an Item/I64
register, so the existing guarded-body proof could not retain its successful
native domain. The compiler now hypothesizes the float success domain, requires
the complete noncollecting initializer/write proof, and allocates a separate D
register only after that proof succeeds. Restoring the saved bindings preserves
the checked Item sibling. Allocating variants retain their ordinary domains;
no raw float enters a boxed root slot (**D2.3.5, D6.3, D8.2.6**).

The new fixture exercises a unique writable float array, an aliased snapshot that
selects COW fallback, and an empty-array validation error, in three GC modes.
Spectralnorm's primary release median falls from **7.4905 to 0.2625 ms**, beating
Full MIR's **2.9155 ms** and C2MIR's **0.360107 ms**. AWFY Nbody falls from
**76.5265 to 1.7445 ms**; its primary C2MIR upper bound **1.20831** narrowly misses
the 1.20 gate despite a qualifying median. Jetstream Nbody also improves sharply
but misses that confidence gate. Independent confirmations are reported separately.

The clean required baseline passes **8,714/8,714** (6,602 Lambda + 2,112 input).
The final 53-group MVP gate retains **58 failing subtests in 17 groups**; the
new correctness/GC/provenance/scheduling groups pass. This supersedes the earlier
contaminated baseline run, while preserving its evidence under `tune48e/`.


**Final primary release campaign (30 paired samples per valid engine).** The
candidate and matching Full MIR snapshot use the same archived release; the regression
control is the frozen phase-one MVP. All 48 Lambda/C workloads retain their
source/golden hashes. Matched 10,000-call Sumfp resolves its single-run clock
limit. Source identity is `c301686ddc63e57041e9062163a488f7663aa2a60ff933b645250b6b572bb5de`;
binary SHA is `1749a4a8ed18a59905e960772e2d056fe76098c364991de51523b507a30a8c7d`.
The source snapshot matches HEAD `6b68618a2` plus the MVP changes; the frontend
commit completed after archival without changing that fingerprint.

| Set | Phase-one Full wins | Final Full wins | Phase-one both targets | Final both targets |
|---|---:|---:|---:|---:|
| Original 28 | 25 | 27 | 12 | 14 |
| Additional 20 | 12 | 13 | 5 | 5 |
| All 48 | 37 | 40 | 17 | 19 |

These counts use the same current-workload paired observations and one-sided
95% ratio bounds: Full MIR <1.0, C2MIR ≤1.20. They are not comparisons with the
historical 40/25 campaign. Three Full MIR rows are unqualified: FFT and Fasta
crash; Navier–Stokes returns checksum 12 instead of 77. All MVP and prior-MVP
rows match their goldens. Invalid Full MIR observations never count as wins.

| Benchmark | Previous MVP ms | MVP ms | Full MIR ms | C2MIR ms |
|---|---:|---:|---:|---:|
| Spectralnorm | 7.4905 | 0.2625 | 2.9155 | 0.360107 |
| AWFY Nbody | 76.5265 | 1.7445 | 91.9015 | 1.487549 |
| Jetstream Nbody | 77.386 | 1.6925 | 93.5225 | 1.440430 |
| Storage | 0.3085 | 0.3115 | 0.3580 | 0.086548 |
| Deriv | 8.5600 | 8.7185 | 10.1530 | 2.572388 |
| GCbench | 74.3425 | 74.7345 | 201.2575 | 68.114502 |

The constructor/storage gains precede the last float-only change: phase-one
Storage was 0.308 ms against its adapted control's 0.713 ms; Deriv was 8.4545
against 92.524 ms; GCbench was 73.540 against 1190.020 ms. That campaign contains
17 complete 100-sample rows and remains explicitly partial. The final primary
uses all 48 rows, comparing the float change against that phase-one candidate.

No primary regression flag occurs among the original 28, including batched
Sumfp. The sole flag is additional-row Binarytrees: 2.487 vs 2.371 ms (about
4.9%); it still meets both targets. Independent 200-sample Binarytrees and
100-sample Nbody confirmations preserve the primary observations separately.

Five rows miss the Full MIR confidence gate: Puzzle, Pidigits, Permute, Base64
and Raytrace3D. Permute's median is faster but its upper bound crosses parity.
The C2MIR milestone remains open: 29 rows miss its primary allowance gate.
The remaining work includes these measured gaps and the legacy focused-suite
migration/emission failures; no all-target completion is claimed.


**Independent confirmations.** Binarytrees' 200-sample median is **2.4355 ms**
versus **2.355 ms** for phase one: the approximately **3.4% regression is
confirmed** (two-sided lower ratio bound 1.01941), while Full MIR and C2MIR
gates both pass. Its MIR matches after normalizing native addresses; this does
not establish the cause of the timing difference. AWFY Nbody's 100-sample median
is **1.706 ms**, C2MIR **1.422485 ms**; Jetstream Nbody's is **1.6845 ms**, C2MIR
**1.4069825 ms**. Both remain outside the one-sided confidence allowance despite
medians near 1.20. Primary counts above are unchanged.

Reproduction and evidence: [primary 48-row report](../../test/benchmark/lambda_mvp/tune48_outcomes_2026-11-03.json),
[matched Sumfp](../../test/benchmark/lambda_mvp/tune48_outcomes_sumfp_2026-11-03.json),
[gate analysis](../../test/benchmark/lambda_mvp/tune48_outcomes_analysis_2026-11-03.json),
[validation](../../test/benchmark/lambda_mvp/tune48_outcomes_validation_2026-11-03.json),
[partial phase one](../../test/benchmark/lambda_mvp/tune48_outcomes_phase1_partial_2026-11-03.json),
[Binarytrees confirmation](../../test/benchmark/lambda_mvp/tune48_outcomes_beng-binarytrees_confirmation_2026-11-03.json),
[AWFY Nbody confirmation](../../test/benchmark/lambda_mvp/tune48_outcomes_awfy-nbody_confirmation_2026-11-03.json),
[Jetstream Nbody confirmation](../../test/benchmark/lambda_mvp/tune48_outcomes_jetstream-nbody_confirmation_2026-11-03.json),
[Full MIR failures](../../test/benchmark/lambda_mvp/tune48_outcomes_full_failures_2026-11-03.json),
[normalized MIR comparison](../../test/benchmark/lambda_mvp/tune48_outcomes_binarytrees_mir_2026-11-03.json).

These results qualify the archived release snapshot. Subsequent shared COW/RDB
work in the checkout is outside this measurement identity; its files are retained.


A final copy of the latest shared executable also passes the 48 MVP benchmark
goldens and the outcome/float fixture (`tune48f/latest-shared-correctness.log`).
This is a correctness-only integration check; its timings are excluded.


## 2026-11-03: guarded string appends and loop kernels

A fresh release control includes the intervening shared COW changes; only
`mvp_mir.cpp` differs between that control and this candidate. The earlier
`tune48f/` results remain historical evidence with their own source identity.
Both use release self-reported execution windows, the same unchanged workloads,
and independent C2MIR C ports (**D1.6, D7.4.2**).

**String growth.** Deferred `string | error` storage stopped selecting the owned
append path. Base64's MIR then captured its prefix and invoked generic `fn_join`
every iteration; those generic results are frozen, forcing repeated prefix
copies. Destination selection now uses the original string admission contract,
while the stored outcome remains unchanged (**S7.7.2v2, S7.7.4v2**). Statically
known text/error fragments classify their errors before entering the raw string
ABI. Unknown fragments use a guarded text/null path; other kinds replay the
original join tree over the already evaluated operands. The fallback preserves
sequence shape and earlier join-error precedence (**S7.9.3, S10.6.1**). Ordinary
reads still freeze an escaping string snapshot; precise roots cover fragments
and both result paths (**S9.1.2, D6.3**). Existing shared append helpers are reused.

**Guarded loop kernels.** The loop planner now applies the same successful
int/bool/float hypotheses as the complete function proof. Every initializer and
write must satisfy the closed-body proof under the array entry guards. A
conservative generic-conversion reentry flag no longer vetoes that proof before
it runs; actual unproved/reentrant calls still reject the closed body. Inline
locals choose their native representations before new registers are allocated,
so float success never reuses an Item register (**D2.3.5, D8.2.6**). Short,
shared or alternate-layout arrays retain the original checked loop. No shared
runtime layout, data model, GC scanner or vendored code changes.

**Verification.** `mir_mvp_string_outcomes` covers Unicode appends,
null and numeric fallback, rejected sequence destinations, alias snapshots,
left-to-right effects even after an earlier stored error, and continued loop
outcomes. `mir_mvp_propagation` adds repeated float kernels with unique storage,
COW snapshots and a short-array miss. Both run normally, under every-allocation
poisoned GC, and under seeded poisoned GC. Native MIR assertions require the
append and guarded float expansion while retaining their checked fallbacks.
All 48 benchmark goldens pass. The seven targeted groups pass, while the
55-group aggregate retains the existing 58 failing subtests across 17 groups.
No all-target completion is claimed.


A follow-up Puzzle inspection identifies a lowering difference to investigate:
Full MIR's recursive `solve` emits `lambda_array_scalar_snapshot` at three plain
parameter entries; MVP emits three general `cow_capture_value` calls. Full's
existing helper uses a precisely rooted GC header with a confined fixed scratch
payload for compact int/bool arrays, falling back to ordinary COW for other
shapes (**S9.1.2, D6.3**). Its escape/index/recursive-transfer proof is currently
static in `transpile-mir.cpp`. Reusing this optimization requires extracting a
shared analysis interface; copying its static proof or weakening lifetime
checks would violate the implementation constraints. The MIR difference is a
lead for subsequent tuning, not a measured speedup in this candidate.


**Frozen release qualification (30 paired samples per valid engine).**
The fresh control contains the same shared COW/runtime sources; only
`mvp_mir.cpp` differs in the recorded 38-file compiler corpus. The candidate
source SHA is `48e3920d45c97a27de4cff8b2e7a2bb64e88a8f3aee51edaa2fb44b850890021`;
its executable SHA is `716b8cfe006de13905aa847f6e3440cf6a91a0163512e012adeba85d010442f2`.
The same private C2MIR executable is used for every primary row, following a
restart after a concurrent clean removed the shared driver. Early observations
remain under `before-reference-freeze/`, outside these primary counts. The
optional `--c2mir-exe` argument and its provenance probe cover archived drivers.

| Set | Control Full wins | Candidate Full wins | Control both | Candidate both |
|---|---:|---:|---:|---:|
| Original 28 | 27 | 27 | 14 | 15 |
| Additional 20 | 13 | 14 | 5 | 7 |
| All 48 | 40 | 41 | 19 | 22 |

The one-sided 95% ratio gates remain Full MIR <1.0 and C2MIR ≤1.20.
Matched 10,000-call Sumfp passes both gates at 0.011 ms; its clock-limited
single run remains in the raw report. Three invalid Full MIR rows remain
unqualified: FFT/Fasta crash and Navier–Stokes produces checksum 12 instead of 77.
All MVP and prior-MVP outputs match their goldens.

| Benchmark | Control MVP ms | Candidate MVP ms | Full MIR ms | C2MIR ms |
|---|---:|---:|---:|---:|
| Base64 | 158.659 | 17.992 | 139.3935 | 0.5129395 |
| AWFY Nbody | 1.755 | 1.478 | 90.0500 | 1.4429930 |
| BENG Nbody | 1.7575 | 1.485 | 93.0205 | 1.4559330 |
| Jetstream Nbody | 1.769 | 1.494 | 93.3785 | 1.4509275 |
| Matmul | 5.3645 | 5.236 | 56.0635 | 6.1411130 |

Base64 changes from a Full MIR loss to a qualified win, while its large C2MIR
gap remains. All three Nbody variants now meet both targets. The C2MIR upper
ratio bounds are 1.06846 for AWFY and 1.11187 for Jetstream. Original-28
regression flags are absent, including matched Sumfp.

Additional Triangl retains a primary regression flag: 1873.335 vs1853.760 ms
(about 1.1%); the two-sided lower ratio bound is 1.00034. Its complete MIR matches
after normalizing native addresses. An independent 31-pair audit observes
1915.860 vs1898.920 ms, but concurrent compiler/build jobs disturb that audit,
so it is explicitly unqualified and cannot confirm or clear the primary flag.
The cause is unestablished. No code padding, workload changes or timing-based
sample selection is used to address it.

The required shared integration baseline passes **8,720/8,720** (6,608 Lambda +
2,112 input). Its source fingerprints match before and after the run; its log
and summary artifacts are frozen under `tune48g/`. All MVP sources match the
performance candidate. That integration baseline includes a subsequent shared
Full MIR edit, which is outside the archived performance identity. A reuse-check
escaping error briefly started a duplicate build; only our duplicate process
group was stopped before it reran tests, and the original all-pass evidence was
preserved. The release checks pass all 48 goldens and all 7 targeted groups,
including forced/poisoned GC and three Nbody kernel-emission variants. The
55-group broader gate still records 58 failing subtests across 17 groups; previously
deferred fixture migrations and existing emission failures remain asserted.

Four rows miss the Full MIR confidence gate: Puzzle, Pidigits, Permute and
Raytrace3D. Twenty-six rows miss the C2MIR allowance. These and the additional
Triangl flag remain open work; no all-48 target completion is claimed.

Evidence: [48-row primary](../../test/benchmark/lambda_mvp/tune48_guarded_2026-11-03.json),
[matched Sumfp](../../test/benchmark/lambda_mvp/tune48_guarded_sumfp_2026-11-03.json),
[paired analysis](../../test/benchmark/lambda_mvp/tune48_guarded_analysis_2026-11-03.json),
[validation](../../test/benchmark/lambda_mvp/tune48_guarded_validation_2026-11-03.json),
[unqualified Triangl audit](../../test/benchmark/lambda_mvp/tune48_guarded_triangl_confirmation_2026-11-03.json),
[normalized MIR comparison](../../test/benchmark/lambda_mvp/tune48_guarded_triangl_mir_2026-11-03.json).


## 2026-11-03: confined recursive array snapshots

Authority: **S9.1.2–S9.1.3v2, S7.7.3, D2.3.5, D6.3,
D8.2.6 and D7.4.2**. No formal ruling, runtime data layout or GC ownership
rule changes. Final performance artifacts are under `temp/lambda_mir_mvp/tune48j/`.

Puzzle's emitted MVP recursion captured three Boolean array owners before each
self call. The share marks invalidated the caller's writable witnesses and caused
repeated COW detachments. Production already used a confined scalar snapshot:
its existing `lambda_array_scalar_snapshot` helper allocates a GC header and
copies compact integer/Boolean payloads into fixed activation scratch. MVP now
reuses that helper and the extracted proof in `mir_cow_analysis.hpp`.

The shared proof allows scalar indexing, length observations and forwarding to
the same recursive formal. It rejects whole-value retention, aliases, non-scalar
keys, rebinds, captures and suspending/nested definitions. Each compiler supplies
its own scalar-key query. Production keeps its existing numeric proof; MVP admits
only proven primitive array parameters. The helper retains ordinary COW for
large arrays, views, other physical lanes and enforced runtime layouts.

Every ordinary or inlined MVP entry establishes its own snapshot and preserves
the native error companion. A direct caller may omit its capture only when all
actuals are pure and no simultaneous `var` actual names that owner. Effectful
later actuals retain the original argument-evaluation snapshot. Inlined entries
build witnesses for their selected owner, and callers refresh ownership after
return: the fallback can share the incoming header even though the fast path
leaves it unique. An initial large-array probe exposed stale witness reuse; the
final candidate fixes both entry and caller witnesses.

`mir_mvp_confined_arrays.ls` checks recursive isolation across collecting calls,
a simultaneous plain/var alias, a later effectful argument, retained results,
unknown selection keys, large-array fallback, empty arrays and rejected input.
It passes normal, forced-every-allocation/poison and seeded/poison GC modes.
The MIR checks require the snapshot helper for confined functions and preserve
ordinary capture for retaining functions. Puzzle emits exactly three snapshot
calls and no generic COW capture. The older `plain_write` emission assertion now
requires that replacement; its semantic golden is unchanged. Literal range
selection remains outside MVP admission; this increment does not expand it.

The frozen candidate and fresh control share the recorded runtime sources.
The candidate adds the shared header, MVP planning/emission and the production
proof extraction. Later shared RDB/parser/registry edits are recorded separately
from those performance binaries. Execution windows, validation/output boundaries,
workloads and benchmark goldens are unchanged. C2MIR uses the same private
reference driver as the preceding campaign.

Remaining investigation priorities are the three Full MIR misses: Pidigits,
Permute and Raytrace3D. Pidigits' C reference uses fixed-capacity integer limbs
and a bounded small-quotient algorithm; the Lambda workload uses general decimal
operations. Raytrace3D repeatedly reads mixed record/vector contracts. These are
inspection leads, not established profiles or measured changes in this increment;
no workload-specific integer algorithm or record-layout assumption was added.

The long-loop regression also caught per-expansion `MIR_ALLOCA` executing inside
an enclosing caller loop: the initial candidate returned `error[E308]: Stack
overflow`. The final lowering reserves each expansion's private scratch once at
the containing function anchor, after its stack guard and before any body loop
or tail entry. The confined payload may then reuse that reservation because no
array reference escapes the expansion. `mir_mvp_snapshot_loop.ls` requires 50,000
calls to finish with `[450000, 3]`; GC modes and MIR placement assertions cover
the same lifetime. The old h campaign's 37 completed rows are diagnostic only.
A fresh i campaign includes the final placement and the latest shared runtime
and production representation fixes on both sides.

A second entry audit found that an allocation error could reach the inline
write-back join before later parameters were initialized. Snapshot-bearing
entries now initialize all arguments first; ordinary entries also consume their
borrow homes before allocation. The native sibling MIR checks require the later
arguments and home consumption before the helper. An aliasing wrapper exercises
the inline form while retaining its real recursive edge (**S7.6.3v3, D6.3**).

Final measurements use a SHA-verified frozen benchmark corpus. Concurrent edits
removed propagation from three live benchmark scripts, so the old binaries
rejected their new source rather than executing comparable workloads. Those
failures and both partial campaigns are retained as diagnostics. All 144 frozen
Lambda/C/golden files match the prior g corpus; the three changed Lambda files
were recovered read-only from commit `793d58bd5`. The harness and correctness
runner accept an explicit archived benchmark root, and a provenance test verifies
that its actual commands use that root. No live benchmark file was edited here.

The final j build recompiles the final MVP emitter privately and links copied
release objects from the i runtime snapshot. Object/library hashes and timestamps
are recorded; 202 repository header dependencies were checked. A Clang VFS
overlay supplies the exact frozen `lambda.h` bytes because the shared header
changed afterward. This keeps the runtime and Full MIR side identical to the
fresh i control, without replacing shared sources or objects. Later shared
integration sources and their baseline remain a separate validation scope.

**Frozen release qualification (30 paired samples per valid engine).** The final
candidate source SHA is
`dccfed18823b3c66e33a19a86d1f772a5cab74e3192e8dd0cba6e1a68ad9d703`;
its executable SHA is
`11c3c626a452ebdb515d02f64598c0ba5197958cfd5d9e8fe4d0aea642c0fca3`.
The fresh pre-snapshot control uses the same frozen runtime, Full MIR and inputs.
Timing excludes startup and compilation; correctness and output are checked
separately. Competing jobs cause whole interrupted rounds to be retried.

| Set | Control Full wins | Candidate Full wins | Control both | Candidate both |
|---|---:|---:|---:|---:|
| Original 28 | 27 | 27 | 15 | 15 |
| Additional 20 | 14 | 15 | 7 | 7 |
| All 48 | 41 | 42 | 22 | 22 |

The one-sided 95% ratio gates remain Full MIR <1.0 and C2MIR ≤1.20. Matched
10,000-call Sumfp passes both at 0.011 ms; its single-call timing is clock-limited.
All 48 candidate/control outputs match their goldens. FFT/Fasta crash in the
archived Full executable, and Navier–Stokes returns checksum 12 instead of 77;
these three Full comparisons remain unqualified.

| Benchmark | Control MVP ms | Candidate MVP ms | Full MIR ms | C2MIR ms |
|---|---:|---:|---:|---:|
| Puzzle | 18.759 | 4.1115 | 4.903 | 1.25293 |
| AWFY Nbody | 1.4585 | 1.4685 | 89.7925 | 1.472412 |
| Spectralnorm | 0.267 | 0.267 | 2.940 | 0.361572 |
| GCbench | 74.112 | 73.6865 | 203.0075 | 66.7524415 |
| AWFY Towers | 0.314 | 0.324 | 1.267 | 0.0273435 |

Puzzle improves 4.56× over the control and changes from a Full MIR loss to a
qualified win; its upper ratio bound is 0.85132. Its remaining 3.28× C2MIR ratio
still misses the requested allowance. Three valid rows miss the Full confidence
gate: Pidigits, Permute and Raytrace3D. Twenty-six miss the C2MIR allowance.
The original 28 have no regression flags, including matched Sumfp.

Additional Towers retains a primary regression flag (about 3.2%; two-sided lower
ratio bound 1.00623). Its complete module MIR matches after normalizing prototype
names and native pointer immediates. An independent 61-pair audit, with zero
recorded competing jobs, measures 0.315 vs0.314 ms (0.3%). Its two-sided 95%
ratio interval is 0.92988–1.07256, so it does not confirm the primary slowdown.
Both reports and the primary flag remain; the cause is unestablished. No workload
changes, code padding or timing-based sample selection were used.

Nine targeted release groups pass: all 48 goldens, 18 array/GC cases, the new
confinement and long-loop fixtures in three GC modes, native entry/inline error
ownership, guarded append and propagation, Nbody kernels and frozen-source
provenance. The full 57-group experimental gate retains 58 failing subtests
across 17 groups, exactly the prior failure headers. No failure assertion or
golden was removed to qualify this increment.

Current-runtime integration is also checked independently. Its six new/recent
ownership, GC and emission groups pass. On a frozen copy of current benchmark
fixtures, 47/48 normal goldens and 17/18 array/GC cases pass. The unchanged FFT
fixture is rejected before MVP lowering by subsequent shared frontend contract
changes (E200 at lines 24/28); that failure remains recorded. The first diagnostic
against the older performance corpus additionally rejected Quicksort, Matmul and
JSONgen, whose live fixtures another task had already migrated. These later
frontend inputs and runtime binaries are outside the frozen timing identity.

The required integration baseline passes **8,733/8,733** (6,621 Lambda +2,112
input) with pinned host/native-test executables. The initial shared `make` run
was disrupted when another build removed/relinked `lambda.exe`, producing
ENOENT failures. An isolated run then exposed three missing Makefile fixture
assertions and eight sandbox-denied localhost tests. Copying those unchanged
fixtures and permitting local networking gives the complete passing run.
During focused environmental rechecks, one Hotwire JS MIR execution exited 137;
two other repeats and the complete final baseline pass. That unexplained
intermittency, both earlier failures, pinned binary hashes and final logs remain
in the validation artifact. No test assertion or golden was changed for baseline
qualification. Later shared integration sources differ from the frozen release
runtime; at qualification, the current MVP sources matched the final performance
snapshot. Commit preparation subsequently removes one trailing blank line from
`mir_cow_analysis.hpp`; both hashes are recorded in validation and no code changes.

Evidence: [48-row primary](../../test/benchmark/lambda_mvp/tune48_confined_2026-11-03.json),
[matched Sumfp](../../test/benchmark/lambda_mvp/tune48_confined_sumfp_2026-11-03.json),
[paired analysis](../../test/benchmark/lambda_mvp/tune48_confined_analysis_2026-11-03.json),
[validation](../../test/benchmark/lambda_mvp/tune48_confined_validation_2026-11-03.json),
[independent Towers audit](../../test/benchmark/lambda_mvp/tune48_confined_towers_confirmation_2026-11-03.json),
[normalized MIR comparison](../../test/benchmark/lambda_mvp/tune48_confined_towers_mir_2026-11-03.json).

## 2026-11-04: batch tuning of every Full MIR miss

A fresh current-runtime 48-row screen identified **Fasta, Pidigits, Permute and
Raytrace3D** as the valid comparisons still slower than Full MIR. This increment
tunes them in one compiler candidate. Full MIR now passes Fasta and FFT; its
Navier–Stokes checksum remains **12 instead of 77**, so that comparison stays
unqualified. These current-source results are separate from the November 3
campaign, whose frozen frontend and three invalid Full comparisons differed.

The complete primary's priority rows each have 30 paired observations, two warmups per engine,
matched execution windows and one-sided 95% confidence gates. Numbers below are
release execution medians in milliseconds; startup and compilation are excluded.

| Benchmark | Previous MVP | Batch MVP | Full MIR | Reduction versus Full | Full gate |
|---|---:|---:|---:|---:|---|
| Fasta | 0.7815 | 0.3805 | 0.8120 | 53.1% | Pass |
| Pidigits | 0.3415 | 0.3230 | 0.3580 | 9.8% | Pass |
| Permute | 0.0730 | 0.0690 | 0.0725 | 4.8% | Pass |
| Raytrace3D | 22.3295 | 12.0785 | 12.7000 | 4.9% | Pass |

The 20% C2MIR target remains unmet for these four rows. Their C2MIR medians are
0.2438965, 0.0444335, 0.021973 and 1.925537 ms respectively. No C port, workload
bound, algorithm or golden changed. The FFT fixture only removes two obsolete
binding carets after native integer conversion became infallible
(**S7.7.2v2**). The standalone archived C2MIR executable remains a measurement
reference; no product back end is restored.

The combined implementation changes these shared compiler paths:

- Typed decimal operations dispatch directly to the existing precisely rooted
  decimal arithmetic. Generic operands retain ordinary dispatch, and BigInt and
  decimal results preserve their existing rules (**S4.4, D6.3**).
- Small self-recursive array procedures may expand once even when their bodies
  contain loops. The next recursive edge remains a real guarded call; expansion
  budgets and precise scratch ownership remain enforced (**D6.3, D8.2.6**).
- MVP field reads use storage descriptors and the shared admitted-prefix guard.
  Closed read-only record parameters cache layout validity; writers retain
  their exclusive-storage witness. Container-pointer slots retain null and
  fallback guards (**D2.3.3, D2.3.5, D4.2.5**).
- Successful producer facts survive Item transport, unchanged locals and typed
  pointer-element projections. They preserve error alternatives, declared write
  contracts and call-site admission. A discharged call drops Error from its
  continuing domain, like unary caret (**S7.7.2v2–S7.7.3, D7.4.2**).
- Unchanged scalar outcomes reuse binding-owned number homes. Native match
  joins require an explicit scalar boundary and complete outcome coverage;
  partial matches retain null completion and untyped matches retain selected
  tags. The existing erased-float-array comparison now also guards immutable
  float/error thresholds before its native path (**D6.2.3, S11.2.1, S6.1.2v2**).
- Guarded allocating array kernels can expand native scalar leaves after a
  complete body proof. Their checked paths, rooted owners and ordinary helper
  effects remain available (**D6.3, D8.2.6**).

Ownership tests exposed two defects while extending those paths. A retained
member/index child now captures ownership at binding, return and map-insertion
boundaries; transient reads remain borrows (**S9.1.2, S9.3.1**). Nullable
container fields now store zero for tagged null. Their physical writer preserves
existing virtual-carrier pointers instead of applying a stricter semantic tag
check; renderer and DOM regressions verify this distinction (**D2.3.5,
D9.5.5**). Runtime types, data layouts and precise GC ownership are unchanged.

All **48 benchmark goldens**, **18 forced-GC benchmarks** and **nine targeted
GC/emission/provenance groups** pass. New fixture pairs cover admitted prefixes,
nullable container fields, recursive writes with saved observers, projected-child
snapshots, decimal/BigInt dispatch, error thresholds, exact match ordering,
partial matches and cold scalar-home retention in three collection modes. The
broader **59-group** experimental gate retains **57 failing subtests across
16 groups**, with no new failure headers. No legacy assertion or golden was
weakened.

The private debug integration baseline passes **8,736/8,736**: **6,624 current
Lambda cases** plus **2,112 reused unchanged input-parser cases**. It pins the
host and native test executables, uses private scratch/logs and copies the
repository's logging configuration unchanged. The earlier runs without that
configuration and the caught virtual-pointer regression remain diagnostic logs.
The shared root executable was never replaced.

The complete primary and matched Sumfp qualify **46/48 Full wins and 22/48
combined wins**, versus **44 and 22** for the interleaved fresh control. The
original 28 have **28 Full wins, 15 combined wins and no regression flags**.
List's 0.2065 ms exceeds Full's 0.1745 ms; List and Storage also regress against
the previous MVP. These primary flags remain in the artifact.

The follow-up removes retaining captures from pure null predicates and restores
the compact exact-layout cache guard for record writers. It retains real
binding/return snapshots and checked prefix fallbacks. Its six-case results are:

| Benchmark | Latest MVP ms | Full MIR ms | Strict Full gate |
|---|---:|---:|---|
| Fasta | 0.3865 | 0.8200 | Pass |
| Pidigits | 0.3300 | 0.3470 | Pass |
| Permute | 0.0670 | 0.0695 | Unconfirmed: upper bound 1.00 |
| Raytrace3D | 12.1680 | 12.9320 | Pass |
| List | 0.1505 | 0.1745 | Pass |
| Storage | 0.3030 | 0.3635 | Pass |

Storage's prior-MVP regression flag clears. List remains **18% slower** than the
previous MVP's 0.1275 ms; unconditional projected-cursor captures remain an
optimization opportunity requiring a safe observation/escape proof. The latest
compiler passes all 48 goldens, 18 GC benchmarks and the same nine targeted groups
inside the 59-group gate, still with 57 legacy failures and no new failure
headers. The user requested wrap-up, so **a complete latest-compiler timing
requalification remains pending**. The six-case follow-up does not replace the
older primary's48-row aggregate counts.

The pinned primary is `temp/lambda_mir_mvp/tune48_batch/final-release.exe`,
SHA `a7a20904f412b94d1877f8e08bc545824b13121ffc24f6734a1c544f28e33bb0`.
Its build freezes 777 link inputs and 79 dependency headers, replacing only the
MVP compiler, shared emitter use and three narrow runtime units. Exact overlaid
runtime sources are archived to avoid unrelated concurrent registry/header
changes. The Full lowering object stays frozen; the debug baseline additionally
validates the shared guard extraction in current production lowering.


The latest compiler is `batch-v8-release.exe`, SHA
`94b1f2414865afb68f958f744b356cda6653b90578bf37c90460aaf0ea2e174c`.
Primary and latest source identities are recorded separately. The primary's
replacement-object copies were overwritten by the follow-up; its exact source
snapshot, binary and build commands remain archived. Replacement-object hashes
in the report explicitly describe the follow-up, while all 777 frozen inputs
are verified unchanged.

Evidence: [complete48 primary](../../test/benchmark/lambda_mvp/tune48_batch_2026-11-04.json),
[matched Sumfp](../../test/benchmark/lambda_mvp/tune48_batch_sumfp_2026-11-04.json),
[primary paired analysis](../../test/benchmark/lambda_mvp/tune48_batch_analysis_2026-11-04.json),
[six-case follow-up](../../test/benchmark/lambda_mvp/tune48_batch_focus_2026-11-04.json),
[validation and build identities](../../test/benchmark/lambda_mvp/tune48_batch_validation_2026-11-04.json).


## 2026-11-04: complete registered benchmark coverage

This expansion implements the remaining shared language features exercised by
the benchmark registry, without changing workloads, existing goldens, runtime
representations, or vendor sources. The registry contains 77 typed entries,
including six duplicate suite workloads: 48 previously covered and 29 added.
The `covered` and `completion` subsets keep those populations distinct.

- Public/imported calls use qualified boxed entries and the existing result ABI
  (**D7.4.2**). Module values use context-owned traced slabs; imported binding
  identities cannot alias a local compiler's slots (**D6.3.1–D6.3.4**). Context-slab
  reads enter the collecting/error effect plan before storage descriptors are
  emitted, including a callable that only returns a retained module value.
- Defaults execute in the callee after preceding parameters are bound.
  Noncapturing anonymous functions share physical function-object construction
  with production. Capturing closures remain outside MVP admission. Native
  expansions retain boxed/default parameter receivers at the real callee entry.
- Nested writes, mutating builtins and projected `var` actuals retain captured
  pre-images and publish replacements through the shared checked COW helpers
  (**S9.1.2–S9.2.3, D4.2.5**). Computed map keys use map writers.
- Collection queries retain source snapshots, key/value bindings, empty
  item-position results, null/string rows, grouping and ordering
  (**S2.5.2v2, S9.2.3, S14.1.1v2**). Content-list append rules cannot replace
  query row semantics. Multi-source joins and limit/offset remain unimplemented.
- Builtins consume registry physical ABI descriptors. Byte Boolean returns are
  normalized and boxed, error Items remain outcomes, and native pointer joins
  depend on emitted carriers rather than an inferred success type
  (**D2.3.5, S7.9.3**). Earlier-branch conversions retain their labels at the
  insertion cursor. Inferred record layouts require a field proof before a
  direct native store (**D4.3.3**).
- Typed-record producers with scalar-only parameters may use the existing
  bounded inline budget. Their return paths retain ordinary captures and
  precisely rooted results; the next real recursive edge retains its stack
  guard (**S9.3.1, D6.3.1–D6.3.4**). No caller container needs write-back.

Correctness uses the existing formatted-output oracle for Prettier; its C port
has a separate checksum-only stdout contract. Five missing JetStream summary
goldens were added and checked against production and independent C ports where
available. JetStream Richards and DeltaBlue have no standalone C port; the
harness keeps their Lambda rows and explicitly marks that reference unavailable.

The final release passes all **77/77** output checks. Its original 48 cases have
**30 paired measurements and two warmups**, interleaving the frozen starting MVP
and final MVP. Timing uses scripts' self-reported execution windows, excluding
process startup and JIT compilation. No statistically significant regression is
detected: a regression requires the paired bootstrap two-sided 95% lower ratio
bound to exceed 1.0. This does not assert that every sample median is lower.
Sumfp retains its timer-limited primary samples and uses a matched 10,000-call
supplement for qualification; its final median ratio is 0.9091.

The pre-inline candidate flagged Binary Trees at 1.0308×, independently confirmed
at 1.0418×. Its hot function MIR and machine code matched the control; that
comparison did not establish the slowdown's cause. General bounded record-result
inlining reduces call/frame overhead: the final 48-case campaign measures
Binary Trees at **0.9374×**, with an independent 60-pair check at **0.9333×**.
The final campaign measures MVP regression only. Earlier Full MIR/C2MIR
observations remain archived separately and do not qualify the final compiler.

The shared expansion fixture passes MVP and production under normal,
force-every/poison and seeded/poison GC. Separate MVP probes retain membership
operand error payloads (**S7.9.3**) and returned module-record snapshots
(**S9.1.2, S9.3.1**). Production's payload erasure and snapshot mutation reproduce
with the unmodified control, so those two probes remain MVP-only. The 18 covered
array benchmarks also pass forced GC. The broader experimental gate has 62
groups and retains exactly **59 pre-existing failing subtests**, with no new
failure headers; its assertions are unchanged.

The private production/shared-runtime Lambda baseline passes **8,755/8,755**:
6,643 freshly executed cases plus 2,112 reused unchanged input-parser results.
The baseline host includes the shared/production changes; subsequent changes
are confined to MVP and are covered by the final release and GC checks above.
Earlier sandbox/setup failures and their complete corrected rerun remain in
the raw logs. Per the user's instruction, no Radiant baseline is required.

| Evidence | Scope |
|---|---|
| [Coverage](../../test/benchmark/lambda_mvp/expand_all_coverage_2026-11-04.json) | All 77 goldens, sources and output identities |
| [Regression analysis](../../test/benchmark/lambda_mvp/expand_all_analysis_2026-11-04.json) | Final 48-case ratios, uncertainty and zero regression flags |
| [Paired samples](../../test/benchmark/lambda_mvp/expand_all_regression48_2026-11-04.json) | Final release observations and binary/source provenance |
| [Matched Sumfp](../../test/benchmark/lambda_mvp/expand_all_sumfp_2026-11-04.json) | Timer-resolution supplement on both binaries |
| [Validation](../../test/benchmark/lambda_mvp/expand_all_validation_2026-11-04.json) | Baseline, GC, retained failures, production discrepancies and build identities |
| [Pre-inline campaign](../../test/benchmark/lambda_mvp/expand_all_pre_inline48_2026-11-04.json) | Earlier four-engine observations; superseded for final qualification |
| [Binary Trees before](../../test/benchmark/lambda_mvp/expand_all_binarytrees_before_2026-11-04.json), [after](../../test/benchmark/lambda_mvp/expand_all_binarytrees_after_2026-11-04.json), [MIR comparison](../../test/benchmark/lambda_mvp/expand_all_binarytrees_mir_2026-11-04.json) | Independent regression investigation and confirmation |

Immutable control link inputs, exact source snapshots, private hosts, raw logs
and MIR dumps remain under `temp/lambda_mir_mvp/expand_all/`.

## 2026-11-04: Full MIR tuning across all 77 entries

The performance target remains open. This batch retains all 77 registered typed
benchmark entries, the original 48 regression population, shared runtime models,
and the existing benchmark algorithms, sizes and goldens. Authority:
**D2.3.5, D4.2.5, D4.3.3, D6.2–D6.3, D7.4.2**;
**S4.4.4, S7.9.3, S9.1.2–S9.1.3, S11.4.1v5, S11.4.6v4, S12.2.1v3**.
No formal ruling or execution default changes.

### Lowering changes

Fixed-key places now evaluate/root keys once and reuse checked COW helpers.
Exclusive projected borrows avoid temporary path arrays; typed scalar record
spines and packed/erased N-D stores guard the complete path before writing.
Shared physical emitters retain layout, lane, bounds and ownership checks.
Pointer arrays use their admitted element contract and guarded physical carrier
(**D4.2.5, D5.3, S9.1.2–S9.1.3**).

The native bitwise emitter is shared with production and handles mixed u32/int
promotion and full-width shift counts. Outcome-aware numeric and indexed reads
keep their error and absence alternatives while exposing admitted native lanes.
Text comparison handles pointer identity; indexed ASCII literal comparisons
use an ordinal lane, while Unicode and NUL/absence keep their full semantics
(**D2.3.5, S4.4.4, S7.9.3**).

Immutable null-arm facts, typed call/split outcomes and closed local array
observations remove repeated classification and unnecessary scalar-home work.
Implicit self-tail positions preserve precise owners and checked scalar
arguments. Primitive array refinements survive the observation pass; otherwise
Triangl lost its packed storage proof. Larger collecting var-array mutators
retain their entries, while read-only witnesses and confined snapshots retain
their established expansion paths (**D6.2–D6.3, S11.4.1v5, S12.2.1v3**).

The final record fix resets inferred mutable `Record | Error` observations just
as plain mutable records were reset. A generic field replacement can change the
physical prefix. Treating the earlier producer's semantic record type as the
current layout proof skipped a later boundary and made Raytrace3D take 237,304
extra `fn_member` calls. Re-admission removes them. The retained-input fixture
and MIR check cover this boundary (**D4.2.5, S11.4.6v4, S9.1.2**).

Builtin effects retain collection behavior. Certified `pn_push` remains
callback-conservative because element admission can invoke a nominal constraint.
Open `pn_push_cow` clears the certificate before growth; MVP also proves plain
builtin numeric-array push callback-free at its call site. String conversion,
replacement, sorting and joining retain their conservative generic effects.
Diagnostic call counting reuses the shared emitter and emits no counters in
ordinary MIR (**D6.4.4**).

Shared production changes fix u32 shift-width guards and nullable-int admission
on typed tail backedges. The strengthened MIR sidecars and nullable recursive
probe cover those fixes (**S4.4.4, S11.4.1v5**).

### Qualification protocol

The private `primitive_push_effects` release has SHA256
`f4ccd15e804c0e8a1cd021436c47de32018e2d498d1a6ef3aa41732aa486f418`.
It rebuilds the current compiler/registry translation units against frozen
expansion runtime link inputs. The source manifest captures 41 compiler inputs;
912 frozen benchmark inputs and four live input files are checked by hash.
Root `lambda.exe`, vendor sources and unrelated package/editor changes are
untouched.

All timings are paired release self-reported execution windows, excluding
startup/JIT. Original 48 uses 30 pairs plus 2 warmups against frozen expansion
MVP; Full 77 uses 15 pairs plus 2 warmups on the same candidate binary with the
compiler selector changed. A matched 10,000-call Sumfp supplement retains both
original kernels because the primary MVP window is timer-limited. Full wins
require a one-sided 95% paired-bootstrap upper ratio bound below 1.0; regression
flags require the two-sided 95% lower bound above 1.0. Competing builds/tests
pause timing or discard affected paired rounds. C2MIR is unmeasured.

All 77 MVP output checks pass. The expanded shared fixture and u32 fixture pass
under MVP and production in normal, force-every/poison and seeded/poison GC
modes; 18 array benchmark cases also pass forced GC. The 65-group broader gate
retains 58 legacy failing subtests versus the starting 59, with no new failure
headers; the read-only tail emission check is now satisfied. No legacy failure
was masked.

The final shared-runtime Lambda baseline passes **8,756/8,756**: all 6,644 fresh
runtime cases on the private `audited_layouts_debug` host, plus 2,112 unchanged
parser results. No rerun was needed with local HTTP/WebSocket access. The final
release differs only in MVP planning, as checked by source hashes. No Radiant
baseline was run.

### Results

The primary 77-row campaign has 57 clear Full MIR wins, 17 gate misses and 3
unqualified rows. Replacing only the timer-limited Sumfp qualification with its
matched supplement gives the following complete population; the primary
samples remain published unchanged.

| Population | Entries | Clear Full wins | Gate misses | Unqualified | Slower medians |
|---|---:|---:|---:|---:|---:|
| Original 48 | 48 | 47 | 0 | 1 | 0 |
| Additional 29 | 29 | 11 | 17 | 1 | 16 |
| All 77 | 77 | 58 | 17 | 2 | 16 |

All 48 original benchmarks have a qualified regression comparison with **zero
slowdown flags**. All 47 original entries with valid Full MIR controls beat
Full MIR. The 16 slower medians are confined to the 29 additions. Julia Formatted
Output has a faster median but its upper 95% ratio bound is 1.0472, so its lead is
inconclusive. Full MIR fails NavierStokes' checksum and crashes in TextSearch;
these controls remain unqualified. Both failures also reproduce in the starting
control. All 77 MVP outputs pass. **The all-benchmark Full MIR target remains
open**, and C2MIR has not been measured for this compiler.

Selected final execution windows (milliseconds):

| Benchmark | MVP | Full MIR | Full/MVP |
|---|---:|---:|---:|
| `larceny/triangl` | 719.494 | 2546.670 | 3.54× |
| `awfy/havlak` | 872.508 | 2644.750 | 3.03× |
| `julia/matrix_statistics` | 93.971 | 150.395 | 1.60× |
| `jetstream/hashmap` | 14.953 | 21.430 | 1.43× |
| `jetstream/raytrace3d` | 8.976 | 13.316 | 1.48× |
| `text/fast_diff` | 74.818 | 186.490 | 2.49× |
| `text/log_pipeline` | 1023.230 | 1327.850 | 1.30× |
| `text/jq_bf` | 148.003 | 176.989 | 1.20× |

The next tuning population contains these 16 slower medians.

| Benchmark | MVP ms | Full MIR ms | MVP/Full |
|---|---:|---:|---:|
| `r7rs/mbrot` | 0.846 | 0.523 | 1.618× |
| `awfy/richards` | 260.654 | 157.402 | 1.656× |
| `awfy/json` | 5.214 | 3.782 | 1.379× |
| `awfy/deltablue` | 57.745 | 24.185 | 2.388× |
| `awfy/cd` | 245.072 | 183.744 | 1.334× |
| `beng/regexredux` | 0.729 | 0.702 | 1.038× |
| `julia/parse_integers` | 48.979 | 40.815 | 1.200× |
| `jetstream/richards` | 259.929 | 158.881 | 1.636× |
| `jetstream/splay` | 743.941 | 641.967 | 1.159× |
| `jetstream/crypto_sha1` | 119.581 | 91.849 | 1.302× |
| `text/microdiff` | 45.874 | 30.481 | 1.505× |
| `text/hyphen` | 15.755 | 13.533 | 1.164× |
| `text/prettier_ast` | 2843.630 | 1635.290 | 1.739× |
| `text/three_way_merge` | 3112.520 | 1421.000 | 2.190× |
| `text/jq_mix` | 948.361 | 713.061 | 1.330× |
| `text/jq_records` | 420.286 | 374.054 | 1.124× |

The next investigations should batch nested numeric stores and scalar floating
loop calls, record-spine guards, pointer-array witnesses, u32 array lanes and
remaining generic text/query dispatch. These are investigation directions,
not measured fixes. Preserve the qualified original 48 comparison while working
on them; the faster median in Formatted Output also still needs a clear lead.

### Evidence and reproduction

| Evidence | Scope |
|---|---|
| [Analysis](../../test/benchmark/lambda_mvp/tune77_analysis_2026-11-04.json) | Complete 77-row counts, uncertainty, remaining misses and zero original 48 regression flags |
| [Full 77 paired samples](../../test/benchmark/lambda_mvp/tune77_full_2026-11-04.json) | Final same-binary MVP/Full comparison, 15 pairs plus 2 warmups |
| [Original 48 paired samples](../../test/benchmark/lambda_mvp/tune77_regression48_2026-11-04.json) | Final candidate versus frozen expansion MVP, 30 pairs plus 2 warmups |
| [Matched Sumfp](../../test/benchmark/lambda_mvp/tune77_sumfp_2026-11-04.json) | Matched 10,000-call supplement across candidate, Full and starting MVP |
| [Starting screen](../../test/benchmark/lambda_mvp/tune77_start_2026-11-04.json) | Earlier 5-pair diagnostic; 49 primary Full wins and 23 slower medians, with its own source identity |
| [Validation](../../test/benchmark/lambda_mvp/tune77_validation_2026-11-04.json) | Source/build/input hashes, baseline provenance, GC/fixture coverage and retained failures |

Exact commands are retained in each paired report. Private release binaries,
source snapshots, frozen link provenance, MIR dumps, diagnostic profiles,
60-pair Towers/Raytrace3D confirmations and superseded measurements remain in
`temp/lambda_mir_mvp/tune77_full/`. The earlier two-regression candidate is retained
as diagnostic evidence and never substituted for this qualification.

## 2026-11-05: remaining Full MIR misses, phase 2

This continuation follows **D4.2.5, D4.3.3, D5.6.1–D5.6.2,
D6.2–D6.3, D7.4.1–D7.4.2; S3.4, S5.1.1v2, S7.9.3,
S9.1.2–S9.1.3, S10.6.1, S11.1, S11.4.1v5 and S12.2.1v3**.
No formal ruling or runtime data model changes.

The initial phase-2 profiles found 62.3 million ThreeWayMerge validations and
31.9 million no-op freezes, repeated module resolution in Prettier, nullable
Boolean dispatch in Richards, and checked scalar stores in DeltaBlue. Private
normal/error observations, may-own string-buffer analysis, linked module/key
images, direct constructor destinations and guarded native lanes address those
causes. Full's existing physical operations and ownership/shape queries were
promoted for reuse instead of copied.

Intermediate failures remain recorded. A boxed nullable-float join needed a
separate destination-lane register. Cached predefined NameId registers lacked
dominance when defined in cold branches. Blanket N-D packing made JQ Tree
allocate row views for reads. Cold typed-member key loads added hot prologue
work. The later candidates correct those causes; those failing/superseded
builds do not qualify current code.

The `imported_domains` release completes the current correctness milestone:
all 77 MVP goldens, unchanged broad failure headers, 21 paired diagnostic cases,
and a fresh Lambda baseline of 8,756/8,756. Its 23-case seven-pair screen has 16
clear Full wins, five slower medians and two inconclusive faster medians. Both
Richards entries, Splay, JQ Records and JQ Tree now beat Full in that screen.
The report is `temp/lambda_mir_mvp/tune77_phase2/imported_domains-screen17.json`;
the filename predates the expanded population. Original48 and full77 final
qualification are still required. The phase-1 public reports remain unchanged.

The next batch shares literal UTF-8 comparisons, semantic type/null predicates
and certified primitive-array append operations, and retains normal primitive
array observations across invariant var borrows. These observations do not
weaken the no-borrow storage/escape proofs. New probes cover error/absence joins,
Unicode and embedded NUL, named union-arm identity, generic member misses,
append growth/snapshots, imported wide scalar ownership, typed borrows and
coarse replacement. The null type's singleton AST descriptor is canonicalized
before the MVP's ordinary literal-pattern exclusion.

The completed `primitive_borrows` selected screen has 16 clear Full wins, four
inconclusive faster medians and three slower medians. JSON moves to
3.675/3.691 ms and DeltaBlue to 21.146/21.756 ms (MVP/Full), both inconclusive;
ThreeWayMerge becomes a clear win at 1300.800/1381.670 ms. No selected
previous-MVP regression flags. Its baseline retains the initial three emission
expectation failures and two full-suite reruns; the final emission suite passes
285/285 and the qualified combined baseline is 8,756/8,756.

The next physical-read batch generalizes the existing pointer-array reader into
a shared guarded word reader, rejecting native, shaped, JS and virtual storage
when the consumer needs Items. It preserves generic index fallbacks and
full-width absence checks. Literal insertion captures projected/named observers
once and leaves fresh children owned (**S9.3.1**); scalar predicates borrow only
when sibling evaluation cannot mutate their input (**S7.9.3, S9.1.2**). A new
snapshot probe exposed an existing MVP dispatch bug: a coarse array root entered
the fixed typed setter, whose fallback accepts maps or concrete array contracts.
Its resolved leaf was correctly `any`; the root dispatch was wrong. Open arrays
now use the generic checked COW walk, while concrete element contracts retain
their declared admission (**D4.2.5, S11.4.1v5**). These changes pass all 77
benchmark goldens, expanded GC probes and the existing broad failure-header
gate; baseline and performance qualification remain active.

The next admitted-field batch reuses the native scalar store proof for admitted
GC-pointer fields, including projected-container insertion captures before the
COW/layout walk. Known container captures check the existing share bit before
calling the idempotent runtime marker; null/error arms stay unchanged. Integer
text joins use the existing rooted `fn_strcat_values` formatter rather than
constructing disposable intermediate strings (**D4.2.5, D5.3, S9.1.2–S9.1.3,
S9.3.1, S10.6.1**). Expanded probes cover retained field snapshots and large
integer/error text joins. This source is not yet correctness/performance
qualified. Its first gate stopped at a new invalid fixture (undischarged
constructor errors in both compilers). After correcting the fixture, a recovered
text join exposed missing producer domains: exact Error metadata now reuses
shared AST typing, and recovery joins retain both normal domains using the same
join helper as conditional expressions (**S7.4.4, S7.6.1v9**). No benchmark output
regression was observed. A new build is required.

The field snapshot probe also finds an existing MVP alias defect (199 versus
Full's 179), reproduced in `physical_paths`. Projected RHS values now acquire
their **S9.3.1** snapshot at array, member and nested-place write boundaries;
checked destination setters do not provide that caller-side capture. Typed
container pointer slots use their physical descriptor independently of its
scalar-native flag, matching the existing member reader. The `captured_fields`
build contains these corrections and is pending qualification.

## 2026-11-05 wrap-up

The user requested wrap-up before another tuning batch. Live compiler source now
exactly matches the correctness-checked `captured_fields` release; the unbuilt
RMW follow-up is preserved only as a private patch. All42 source hashes agree
with both release/debug snapshots. No shared executable or unrelated change was
replaced. The in-flight Full 77 and Sumfp campaigns finished; the longer
original 48 regression repeat was stopped before samples. No coordinator remains
active.

The complete current run has **70/77 clear Full MIR wins**, versus 58/77 in the
previous full qualification. It retains **three slower medians**, two faster
inconclusive medians and two invalid Full controls. These are release
self-reported execution windows, with 15 measured pairs plus two warmups and a
matched 10,000-call Sumfp supplement with 30 pairs. The strict Full win criterion
is the one-sided95% upper ratio bound below1.0.

| Case | MVP ms | Full ms | Phase-1 MVP ms |
|---|---:|---:|---:|
| AWFY JSON | 1.569 | 3.824 | 5.208 |
| AWFY DeltaBlue | 19.127 | 24.413 | 62.284 |
| AWFY Richards | 105.221 | 153.657 | 262.200 |
| Splay | 553.056 | 659.842 | 771.294 |
| Prettier AST | 454.351 | 1747.000 | 3043.610 |
| ThreeWayMerge | 1388.180 | 1445.880 | 3199.400 |
| JQ Tree | 902.111 | 1416.880 | 1368.100 |

Remaining slower medians are CD 194.975/180.002 ms (+8.3%),
Microdiff 32.622/29.665 ms (+10.0%) and RegexRedux 0.750/0.748 ms (+0.3%;
statistically inconclusive). Pidigits and Permute have faster inconclusive
medians. NavierStokes and TextSearch retain the previously reproduced invalid
Full controls; all 77 MVP goldens pass.

The fresh Lambda baseline passes **8,756/8,756** without reruns (6,644 fresh
runtime checks plus 2,112 unchanged parser results). Expanded/u32 fixtures pass
both compilers and all three GC modes,18 array benchmarks pass forced GC,
and the broader 65-group gate has the same 58 legacy failures and no new headers.
The performance goal remains open: Diviter retains a phase-1 MVP regression
flag (+1.4% median), and the original 48×30 campaign was not completed for this
source. Earlier zero-regression results remain historical.

Current evidence: [analysis](../../test/benchmark/lambda_mvp/tune77_phase2_analysis_2026-11-05.json),
[Full 77 samples](../../test/benchmark/lambda_mvp/tune77_phase2_full_2026-11-05.json),
[matched Sumfp](../../test/benchmark/lambda_mvp/tune77_phase2_sumfp_2026-11-05.json),
and [validation](../../test/benchmark/lambda_mvp/tune77_phase2_validation_2026-11-05.json).
The checkpoint records the private binaries, failed intermediate probes, source
restoration, stopped own processes and proposed follow-ups. The subsequent user
commit request covers this validated source and evidence.
