# Lambda MIR MVP: scalar and array implementation

2026-11-01. Implements and tunes the first scalar slice of
[the MIR MVP proposal](../Lambda_Design_MIR_MVP.md), following the
[JS MVP-Lmd approach](../jube/JS_MVP_Lmd.md). **Experimental; performance
qualification remains open.** Updated 2026-11-02: **28 benchmark rows** are
covered, including the latest 15-row expansion. Coverage does not imply a
performance pass. The preceding 13-row tuning campaign beat full MIR on all
13 rows and qualified nine against C2MIR; the expansion and subsequent tuning
measurements appear below. The latest follow-up qualifies 21/28 rows against
Full MIR and 13/28 against both targets; the 28/28 milestone remains open.

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
  floating arithmetic retains its original order and native representation (**D2.4.3**).

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
  (**D2.5**, **S7.1.1v3**, **S7.1.3v2**, **S11.4.1v3**). The null-test emitter
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
  (**S4.5.2**, **D8.2.5v3**).

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
remain shared (**D2.4**, **D5.3**, **D8.4.2v2**).

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
model, helpers, collector and MIR vendor code (**D1.2v2**, **D1.4v4**, **D1.6**).

### Native regions and boundaries

- **Discard demand and native destinations:** unused branch/loop results no
  longer materialize numeric null joins. A discarded direct-call result does
  not box its success lane; argument evaluation, borrow write-back and effects
  still happen. A compatible declared scalar destination consumes a native
  return directly, routing the error companion to that destination's ordinary
  failure boundary. Other fallible call consumers retain their boxed error
  join and success contract (**D8.2.6v2**, **D8.4.2v2**, **S7.7.2–S7.7.4**).
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
  invalidating operations keep the checked path (**D4.4.4v4**, **S9.1.2–S9.1.3**).
  Quicksort's partition now uses native loads/stores in this guarded body.
- **Entry guards and borrows:** dense entries branch directly on each array's
  witness. Only the checked sibling builds persistent layout/COW metadata and
  retains borrow write-back addresses. Both siblings consume the caller's
  transient borrow cells before nested calls. Forward-only array bindings keep
  their precise root/home without rebuilding unused indexing metadata
  (**D5.3**, **S9.1.3**). This removes register spills observed in nbody's entry.
- **Finite ranges and construction:** a finite procedural `for` omits its
  unreachable range-error path. A single nonnegative conditional increment
  may use the range's maximum trip count; an enclosing loop that retains the
  accumulator rejects that proof. Typed primitive fill calls reuse an immutable
  certificate owned with the compiled contract. The existing constructor still
  checks counts/values and owns allocation/rooting (**D3.3.3v3**, **D5.3**,
  **S11.4.1v3**).
- **Exact numeric conversions:** bounded integer expressions divided by positive
  powers of two can remain a scaled integer while every intermediate is exactly
  representable. Proven low zero bits can eliminate the scale; conversion to
  double happens at the consumer. Rounded expressions fall back to the original
  operations; no floating reassociation or fast-math mode is used
  (**S4.4.1–S4.4.3**, **S4.5.2**). Spectralnorm's denominator needs one int-to-double
  conversion. Read-only private parameters whose incoming values all agree can
  use their proven constant; floating constants compare by bits, including the
  sign of zero (**D8.3.1v2**).

Boundary tests also exposed an unused-iterator crash: the shared AST index did
not publish `for` clause bindings, and the binding rewrite occurred after node
publication. The builder now rewrites that edge before indexing and the index
publishes the iterator even without an identifier use (**D8.2.4v2**). This fixes
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
breakdowns. Keep the shared ownership and failure behavior (**D4.4**, **D5.3**,
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
  float (**S7.1.1v3**, **D2.4.1**, **D5.2.3**). Declared nullable signatures remain
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
  **D4.4**, **D5.3**). Brainfuck exercises inferred tape/stack arrays and a typed
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
model and container layouts are unchanged (**D2.4.1–D2.4.3**). C2MIR remains an
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
  shared AST, not another node-fact graph (**D8.2.5v3**).
- Read-only array calls use the shared mutation/retention analysis. A dead
  scratch in a complete local owner permutation permits root moves without
  creating snapshots. Full MIR and MVP share the AST recognizer. The MVP uses
  the same invariant-array admission proof as ordinary assignment; an observed
  scratch still captures a snapshot (**S9.1.2**, **D5.3.1**).
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
  is proven (**D5.3**, **D8.4.2v2**). Successful native call results can flow into
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
and storage layouts unchanged (**D2.4.1–D2.4.3**). It continues to use the shared
AST and physical emitter, with no production lowering or benchmark-specific
replacement kernels (**D8.2.5v3**).

### Changes and proof boundaries

- Borrowed array accessors can inline, including discarded Item returns.
  Nested expansions reserve disjoint slices of the caller's precise argument
  root span; replacement owners propagate through the same write-back cells.
  Actual arguments are evaluated before publication (**D5.3.1**, **D5.3.5**,
  **D8.4.2v2**). Existing mutation/retention analysis also recognizes pure builtin
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
  (**S7.1.1v3**, **S9.1.2**, **D4.4.4v4**, **D5.3.2**).
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
  only when capture-free numeric bodies preserve them (**D4.4.4v4**).
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
and runtime helpers are unchanged (**D2.4.1–D2.4.3**). Proofs use the shared AST
and transient binding intervals, with no additional node-type graph
(**D8.2.5v3**).

### Implementation

- Whole numeric loops can guard array storage, writability and complete index
  extents once. The shared closed-region query excludes capture, replacement,
  reentry and collecting calls; emission checks the collecting-call count.
  Checked siblings preserve shared owners and failed accesses (**D4.4.4v4**,
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
runtime helpers (**D2.4.1–D2.4.3**). It extends the MVP's transient analyses of
the shared AST and existing direct-call ABI (**D8.2.5v3**, **D8.4.2v2**).

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
  (**D5.3.1**, **D4.4.4v4**).
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
  stack recovery (**D4.4.4v4**, **S7.11.1v2**). Buffer pointers are carried only
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
shared helpers (**D2.4.1–D2.4.3**). The changes remain in MVP planning/lowering
and reuse the shared AST and direct-call ABI (**D8.2.5v3**, **D8.4.2v2**).

### Implementation

- Constructor success now uses the same invariant primitive-array proof as
  ordinary admission. The old generic relation rechecked arrays that a typed
  `fill` had already admitted. Boxed errors still take the ordinary boundary
  before any native array witness becomes available (**S9.1.3**, **D2.8.2**).
- Contextual `fill` lowering is shared by bindings and direct-call arguments.
  It passes the parameter's existing immutable representation certificate to
  the shared constructor, avoiding runtime metadata resolution. Count/value
  evaluation order, allocation, error checking and precise roots stay with
  the existing helper (**D5.3.1**, **S9.1.3**).
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
  (**D4.4.4v4**).
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
(**D2.4.1–D2.4.3**, **S9.1.2–S9.1.3**).

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
  allocations retain the same precise root protocol (**D5.3.1–D5.3.4**).
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
  constructor (**D3.3.3v3**, **S9.1.3**, **D5.3.3**). The import remains
  `MAY_GC` with conservative reentry metadata. A repeated NQueens profile
  identified array construction and allocation as the principal helper costs.
- Small constant integer fills passed to proven read-only, nonescaping
  parameters use the existing `MarkBuilder` Input-owned representation. Each
  payload is capped at 64 lanes. Only direct readers qualify, preventing a
  forwarded boundary from replacing an Input-owned certificate with activation
  metadata. Mutable or retained actuals, effectful arguments, counted contracts
  and uncertain values keep runtime construction
  (**D4.1.1v2–D4.1.3**, **S9.1.2–S9.1.3**).

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
(**D4.3.1–D4.3.2v2**); the header, lane representations, capacity semantics and
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
path (**D5.3.2–D5.3.3**). Argument expressions still evaluate once in source order.

Adjacent literal fragments, including literals forwarded through recipe
parameters, coalesce into one Input-owned constant (**D4.1.1v2**). The existing
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
