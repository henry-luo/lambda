# LambdaJS: shared native completion and scalar ownership

> **Implemented — 2026-09-10.** Tasks 3 and 4 of the JS tuning
> follow-up, continuing [JSCU40–JSCU41](../Lambda_Design_Structs_JS.md#13-jscu40--one-function-plan-return-contract-and-entry-obligation).
> Authority: **D1.4v3**, **D5.2.1v3–D5.2.3**, **D5.3.1–D5.3.4**,
> **D5.4.2**, **D6.2.2v2**, **D8.3.1–D8.3.4**, **D8.4.3v2**, **D8.6.1–D8.6.3**.
> Source base: `fa254c56b`, including the earlier tasks 1 and 2 committed in
> `8d4e08671`. No semantics ruling changes.

## Task 3: native completion and direct recursion

JS's native variant previously omitted its return-shape descriptor. Generated
native functions returned one scalar and placed any thrown Error carrier in
`JsAsyncAwaitState::native_throw_lane`. Each caller then imported a separate
take/clear helper. This duplicated the normal/error transport already owned by
`FnReturnAnalysis` and `MirCallResult` (**D8.4.3v2**).

The native variant now publishes `RETURN_SHAPE_NATIVE_ERROR` and the companion
transport chosen by `em_companion_transport`. Native definitions, forward-call
prototypes and function-frame plans consume that descriptor. On register-pair
platforms the function returns `[native value, error Item]`; the existing
context-companion transport remains available where multiple MIR results are
unsupported. This uses the existing ABI shapes and platform policy. Validation
in this record was on macOS arm64; the context-slot platform was not run.

`jm_call_direct_native` retains the complete `MirCallResult`. Its consumer
handles failure before boxing, further helpers or use of the normal register.
The native body's error exits and normal returns stage their result through
`em_stage_function_return`, which is now used by Lambda's return lowering too.
Both lanes reach the existing ownership epilogue. Boxed wrappers retain their
ordinary Item/pending-scalar return protocol; async rejection conversion remains
in the existing JS completion paths.

The runtime throw field, publish/take functions, registry imports and polls are
removed. The async handoff's precise root span now covers its one remaining
Item. Validation caught the stale two-Item span during this migration: reset
wrote into the following runtime IDs, breaking thousands of async Test262
cases. Correcting the span fixes the ownership/layout error; the Test262 runner
is unchanged (**D5.3.1**, **D5.4.2**).

### Invocation depth

Stable, noncapturing direct calls now account for one source invocation using
the same `JsExecutionState::call_depth` and configured limit as dynamic dispatch.
The JS adapter obtains the state through the current EvalContext's capsule
directory. Shared `MirInvocationDepthPlan` emission compares before incrementing
and decrements immediately after either call result. The overflow edge constructs
the existing RangeError and follows ordinary JS error routing.

Public-wrapper-to-body and boxed-body-to-native calls explicitly select an
already-counted internal transition. A source call counts once whether it uses
a native body, an internal boxed body or dynamic dispatch. The counter update
after a generated call is an inline memory operation: no helper runs while a
boxed pending companion is awaiting materialization (**D5.2.1v3**).

This permits ordinary non-tail self-recursion through the existing direct-call
paths. Native entry eligibility and exact argument guards remain unchanged;
functions whose return analysis remains `any`, including addition-based recursive
functions, use the internal boxed ABI. Captures, argument objects, spread,
reassigned bindings, dynamic scope and suspension retain their existing call
admission rules (**D6.2.2v2**, **D8.3.1–D8.3.4**).

## Task 4: consume shared ownership facts

`em_materialize_pending_value` resolves every transported wide payload into the
calling activation's number extent. JS then unconditionally classified and
copied that already-owned Item into another scalar home. The shared `MirValue`
now records `SCALAR_PROVENANCE_ACTIVATION_EXTENT` after resolution, and JS uses
that fact to skip the redundant adoption. Other direct results retain the
existing conservative adoption path. Both Lambda and JS consume the same
materializer and ownership descriptor (**D5.2.1v3**).

The helper catalog also records audited stable results using one initializer:

- Equality, comparisons and logical-not return booleans or Error carriers.
- `typeof` returns a string.
- Bitwise and shift helpers return inline 32-bit Number results, GC-owned
  BigInts, or Error carriers. The BigInt kernels return through
  `bigint_push_result`, which allocates a Decimal container.
- Throw constructors return Error carriers, including when the original thrown
  value is a scalar. Existing `SETS` effects remain intact; the audited
  `js_throw_range_error` row now also declares `SETS`, including its allocation
  failure result. That fact removes the redundant Error-tag check on the new
  depth-overflow edge; the existing direct-call MIR assertion passes unchanged.

These return values need no number-home adoption. Their collection, reentry,
coercion and fallibility remain conservative independently of that ownership
fact. The change does not globally disable JS helper adoption or classify module,
environment or property loads as owned (**D5.2.3**). No loop watermark reclamation
or mutation-based guard hoisting is added.

## Validation and measurements

The new JS regression has a Node-generated expected file. It covers native and
boxed recursion; mixed direct/dynamic calls; catchable depth limits and recovery;
normal/error returns and `finally`; module-derived subnormals; result retention,
mutation and suspension; coercion order; BigInt bitwise results; and helper errors.
Depth checks include successful 2,500-level calls to detect double counting.

The MIR fixture checks native two-result returns, recursive direct calls with a
depth guard, absence of native throw imports, and shared pending resolution
without a second scalar-adoption call (**D8.6.2–D8.6.3**).

Two existing MIR budgets record the reviewed cost of source-invocation guards
(**D8.6.1**). `js_numeric_inference_call` grows from 299 to 312 module
instructions, with `js_main` growing from 238 to 251 and its safepoints from
22 to 23. `js_hoisted_modvar_write_through` on `darwin-debug-v3` grows from
360 to 373 instructions after scalar-adoption savings, with its anonymous
body's safepoints growing from 12 to 13. Neither needs more root stores. Other
budgets are unchanged; the cold overflow path must remain catchable.

The final `make test-lambda-baseline` run reports **5,252/5,266 passing**
(`lambda-baseline-completed.log`). Relevant gates:

| Gate | Result |
|---|---:|
| Lambda scripts | 862/862 |
| Input baseline | 2,104/2,104 |
| Precise GC stress | 120/120 |
| Lambda MIR emission | 88/88 |
| JS MIR emission | 23/23 |
| MIR budgets | 16/16 |
| JS optimization / coercion | 19/19 and 15/15 |
| JS interpreter | 110/110 |
| JS named and file regressions | 480/494 |

The 14 failures are seven existing JSCU31 network fixtures, each present in the
named and file suites: DNS resource table, TLS write callback roots, TLS server
value slots, HTTP client write callback roots, socket callback slots, HTTP
response write tail, and bound socket owner. These were also recorded by
[tasks 1/2](Lambda_Impl_JS2_Guarded_Access.md). All seven reproduce identical
exit status, stdout and stderr on this task's matched control and final
candidate (`network-completed.json`), including the DNS abort and `bind EPERM`.
They remain failures.

The final release also passes the new fixture with forced-GC intervals
0, 1, 7, 31 and 100, with freed-memory poisoning enabled. Its golden output
matches Node. The shared-access regression, side-stack GC regression and
CodeMirror document test pass too: **8/8 focused runs**
(`focused-completed.json`). This includes native failure crossing an allocating
`finally` and subsequent successful calls.

Validation artifacts and the matched release control are under `temp/js2-calls/`.
The control was built with `make release` before these edits, from the source
base above. Its SHA-256 is
`cac23c85753c536b05126104a167fc73f71c752e48afdef8fd41e527d2c92ead`.
The release passes **40,261/40,261 Test262 baseline cases**, including async and
modules, with zero regressions (`test262-completed.log`). The initial root-span
failure remains recorded in `test262.log`; it is superseded by the corrected
runtime run, without runner changes.

### Release execution comparison

`paired-completed.json` records five alternating control/candidate pairs on
macOS arm64 and AC power. No tests or builds were launched alongside the
measurement. Every pair has matching normalized stdout; only timing markers
are excluded. Binary hashes, script hashes, exit status, individual execution
samples and wall time are retained. Candidate SHA-256:
`2dfd5719f8245ac31f300d87ecc7eab475f5f76e596558cc805c9a63d8906a47`.

Times are median execution milliseconds. Speedup is control divided by
candidate. These are measurements against the completed tasks 1/2 control,
using the current benchmark ports, rather than new Result41 comparisons.

| Benchmark | Control (ms) | Candidate (ms) | Speedup | Candidate wins |
|---|---:|---:|---:|---:|
| r7rs/fib | 40.856 | 13.208 | 3.093× | 5/5 |
| r7rs/fibfp | 41.078 | 13.229 | 3.105× | 5/5 |
| r7rs/tak | 3.408 | 0.402 | 8.487× | 5/5 |
| r7rs/cpstak | 6.169 | 0.819 | 7.536× | 5/5 |
| r7rs/ack | 259.365 | 75.554 | 3.433× | 5/5 |
| awfy/nbody | 554.704 | 545.653 | 1.017× | 4/5 |
| beng/binarytrees | 41.557 | 26.777 | 1.552× | 5/5 |
| kostya/matmul | 348.460 | 342.268 | 1.018× | 3/5 |
| larceny/array1 | 20.674 | 19.861 | 1.041× | 2/5 |
| larceny/gcbench | 856.650 | 590.481 | 1.451× | 5/5 |
| larceny/quicksort | 137.851 | 148.406 | 0.929× | 2/5 |

Recursive execution and the two allocation benchmarks improve consistently.
The small `nbody`, `matmul` and `array1` differences warrant less confidence
than the recursive gains. `quicksort` is 7.7% slower in this five-pair run,
with two candidate wins; the earlier candidate run recorded a 1.088× speedup
(`paired-v1.json`). A final 11-pair follow-up records 140.593 ms control versus
146.536 ms candidate: a 4.2% slower median, despite 6/11 candidate wins
(`quicksort-confirmation.json`). All outputs match. Quicksort remains a
performance concern; these samples do not support claiming an improvement.
`tak` is sub-millisecond on the candidate, so its absolute timing is small.
The current `cpstak2.js` performs two ordinary `tak` calls; its result does not
establish a CPS-closure optimization. No benchmark programs were edited.

The guard and completion changes also affect generated code and startup.
Execution timing is not a compiler-time measurement, and these runs do not
claim **D8.6.4v2**'s separate compiler-only consolidation exit ratchets.

```sh
python3 test/benchmark/run_paired_benchmarks.py --language js \
  --control temp/js2-calls/lambda-control \
  --candidate temp/js2-calls/lambda-candidate \
  --bench fib,ack,tak,cpstak,quicksort,matmul,array1,gcbench,nbody,binarytrees \
  --pairs 5 --output temp/js2-calls/paired-completed.json
```

## Remaining scope

The wider JS2 proposal still includes constructor/transition candidates,
additional field and element lanes, effect-based guard hoisting, broader native
return inference, shared construction/operation plans, and profiling the remaining
quicksort cost. This change implements
the native completion/depth and proven scalar-ownership steps; it does not claim
to finish every JSCU36–43 consolidation interface.
