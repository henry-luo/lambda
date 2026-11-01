# Lambda MIR MVP: scalar implementation and native execution tuning

2026-11-01. Implements and tunes the first scalar slice of
[the MIR MVP proposal](../Lambda_Design_MIR_MVP.md), following the
[JS MVP-Lmd approach](../jube/JS_MVP_Lmd.md). **Experimental; performance
qualification remains open.** M0/M1 are partial and M2 arrays have not started.

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
| `mvp_plan.cpp` | Admission over the shared indexed AST, scalar contracts, recursive effect convergence, native return ABI and module lifecycle. |
| `mvp_mir.cpp` | Fresh scalar, control-flow and direct-call lowering through `MirEmitter`. |

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
  The current no-capture/no-borrow admission is part of these proofs; extending
  that surface requires accounting for new ways to mutate bindings.
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
  `int`, `float`, `bool`, `string`, `null` and boxed `any` contracts.
- Local `let`/`var`, assignment, scalar literals, arithmetic/comparison,
  short-circuit logic, string joining, `if`, `while`, return, break and continue.
- The selected builtin surface: single-argument `print`, `clock`, `abs`, `floor`,
  `int`, `float`, `string`, `len`, `bxor` and `shl`. Builtin combinations can still
  reject when their required representation or control-flow contract is outside
  the admitted subset; the names alone are not a general compatibility promise.

Native-return procedures currently require a final return statement. Fallible
while conditions, multi-value functional content, explicit error propagation,
arrays/indexing, records, imports, closures, indirect calls, optional/named/spread
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

## Release measurements

[Current raw samples and provenance](../../test/benchmark/lambda_mvp/native_2026-11-01.json)
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
milestone remains **unqualified** because the array rows are unsupported.
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
python3 test/benchmark/lambda_mir_mvp.py --release \
  --exe temp/lambda_mir_mvp/native-final-release.exe \
  --previous-mvp temp/lambda_mir_mvp/integer-final-release.exe \
  --runs 60 --warmups 2 --output temp/lambda_mir_mvp/native-final-results.json
```

The omitted `--control` compares both compilers in the same executable. These
paths are locally archived build products. In a new checkout, build with
`make release` and pass that executable; omit `--previous-mvp` when no prior
binary is available. `--release` attests build mode rather than inspecting it.
`--legacy-timer` reproduces the historical C timing scope.

## Next bounded work

Preserve the four scalar gains while adding M2's typed array operations and its
four benchmarks, including ownership, aliasing, missing reads and failed stores
(**D4.4**, **D5.3**, **S7.1**, **S9.1**). The remaining scalar optimization
opportunity is Mandelbrot's boxed success joins and guard/control-flow overhead;
its runtime error and saturation paths remain required (**S4.1**, **S7.7**,
**D8.4.1v2**).

General language coverage, AUTO/embedding integration and production replacement
remain later milestones. The selected scalar performance target is met in this
campaign; the full compiler performance goal is still open.
