# Lambda MIR MVP: scalar and array implementation

2026-11-01. Implements and tunes the first scalar slice of
[the MIR MVP proposal](../Lambda_Design_MIR_MVP.md), following the
[JS MVP-Lmd approach](../jube/JS_MVP_Lmd.md). **Experimental; performance
qualification remains open.** Updated 2026-11-02: M2 covers all eight selected
rows, including spectralnorm and nbody; coverage does not imply a performance pass.

Authority: **D1.4v4, D1.6, D2.4.1–D2.4.3, D5.2–D5.3,
D8.2.4v2–D8.2.6v2, D8.4.1v2–D8.4.2v2**; numeric semantics
**S4.1, S4.4–S4.5**, declared contracts **S11.4.1v3**, and error
containment **S3.5, S7.7.2–S7.7.3**. No formal ruling changes.

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
shared emitter (**D2.4**, **D5.2–D5.3**). Boxed bindings and reads adopt cold scalar
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

No runtime helper or vendored dependency was modified. Build inputs are registered
in `build_lambda_config.json`; no-MIR builds exclude both MVP implementation files.

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
  preventing overflow in the proof itself (**D8.2.5v3**).
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
  calls (**D8.3.3**, **D5.3.6v2**).
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
  Boolean joins and an unconditional backedge (**S3.4**, **D8.2.6v2**).
- **Floating constants:** each distinct literal bit pattern gets one register
  definition at function entry. MIR otherwise materializes these literals as
  loads inside a loop, including Mandelbrot's `2.0` and `4.0` each iteration.
  The compile-time pool distinguishes signed zero and NaN payloads, and all
  floating arithmetic retains its original order (**S4.3**).

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

The initial surface admits:

- Top-level, fixed-arity `fn`/`pn` definitions and direct recursion, with scalar
  `int`, `float`, `bool`, `string`, `null`, boxed `any`, and rank-one
  `int[]`, `bool[]`, `float[]` contracts.
- Local `let`/`var`, assignment, scalar literals, arithmetic/comparison,
  short-circuit logic, string joining, `if`, `while`, return, break and continue.
- The selected builtin surface: single-argument `print`, `clock`, `abs`, `floor`, `math.sqrt`,
  `int`, `float`, `string`, `len`, `bxor`, `shl` and `fill`. Builtin combinations can still
  reject when their required representation or control-flow contract is outside
  the admitted subset; the names alone are not a general compatibility promise.
- Primitive array reads and writes through local bindings; direct invariant
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
  (**S7.1.1v3**, **D2.4**). The float extension below adds native float reads. Native stores
  require a matching primitive layout, writable owner and valid index. The
  existing checked setter owns detachment, dynamic admission and failed stores
  (**S7.1.3v2**, **S11.4.1v3**).
- **Snapshots and borrows:** ordinary array captures share-mark their owner.
  Direct `var` array calls evaluate all operands before publishing addresses
  of fixed suffix slots in the precise root frame. Callees consume those
  transport cells before calling out and publish each replacement; callers
  reload after normal and error returns. This handles nested forwarding,
  reassignment and mixed plain/`var` arguments (**S9.1.2–S9.1.3**, **D5.3.1**).
- **Array descriptors:** layout, length and mutability guards are refreshed
  after binding replacement and borrowed calls; captures revoke the writable
  fact. A guarded extent covers indices with a proven nonnegative interval.
  Buffer pointers are normally reloaded at access. A two-statement scalar
  store loop may retain its buffer because only its checked-store fallback
  can collect; that fallback reloads the buffer before the backedge
  (**D4.4.4v4**, **D4.4.6**).
- **Integer proofs:** a private scalar parameter gets an interval only when
  every direct call supplies one and the parameter is never assigned. Range
  induction and nonnegative guarded steps extend local counter proofs. A loop
  condition can narrow a read only before the first body write of that binding;
  the proof passes through a nested loop only when that entire loop never
  writes the binding (**D8.2.5v3**, **S4.1**).
- **Array reduction:** the exact body `sum = sum + a[i]; i = i + 1` under
  `i < limit` gets a four-lane reduction only when `fill` and every write prove
  the complete element interval. Whole-array reassignment or a mutable borrow
  withholds that proof. Entry guards check a zero accumulator, nonnegative
  start, physical layout, array length and a trip bound derived from the
  element magnitude. Every partial sum and merge must fit int53. Other inputs
  execute the original checked loop; no result or iteration is omitted
  (**S4.1.2**, **D4.4.4v4**).

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
borrow transport and precise collector are unchanged (**D2.4**, **D4.4**, **D5.3**).

- Native float reads use the existing nullable float lane. Its reserved NaN
  payload denotes absence; ordinary NaN, signed zero and subnormals remain
  numbers. Arithmetic propagates absence, boxing maps it to null, declared
  float boundaries reject it, and stores take the checked setter on absence
  (**S4.3**, **S7.1.1v3**, **S7.1.3v2**, **S11.4.1v3**). The null-test emitter
  is shared with production; comparison semantics retain the checked path.
- Homogeneous primitive literals allocate through the existing initialized
  `array_num_new` constructor, now registered as a JIT import. The array is
  precisely rooted while members evaluate, and each store reloads its buffer.
  Destination admission installs the ordinary array certificate (**D5.3.1**).
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
  (**D4.4.4v4**, **D8.4.1v2**, **S9.1.2–S9.1.3**).
- Outer counter bounds now survive a nested loop that never writes that
  counter, including writes after the current use. Previously an extent of six
  for nbody's five-element arrays forced every access through the helper.
  Scalar numeric conversions and float `sqrt` use native lanes. Division by
  a proven positive integer power of two uses its exactly representable
  reciprocal, preserving IEEE rounding without reassociating expressions
  (**S4.3**, **D8.2.5v3**).

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

## Next bounded work

First close Sieve's per-row performance gap, with separate repeated-call timing
and constructor/loop cost measurements. Preserve the scalar gains and the two
admitted array rows while adding array
literals, native float reads, module constants and math support needed by
`spectralnorm` and `nbody`, with the same ownership and failure gates
(**D4.4**, **D5.3**, **S7.1**, **S9.1**). The remaining scalar optimization
opportunity is Mandelbrot's boxed success joins and guard/control-flow overhead;
its runtime error and saturation paths remain required (**S4.1**, **S7.7**,
**D8.4.1v2**).

General language coverage, AUTO/embedding integration and production replacement
remain later milestones. The selected scalar performance target is met in this
campaign; the full compiler performance goal is still open.

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
