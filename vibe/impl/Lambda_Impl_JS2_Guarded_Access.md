# LambdaJS: shared candidates and guarded access

> **Implemented — 2026-09-10; validation results below.** Tasks 1 and 2 from
> the JS tuning follow-up, implementing the bounded field/indexing steps of
> [JSCU38–JSCU39](../Lambda_Design_Structs_JS2.md#6-jscu38--shared-guarded-field-reads-and-writes).
> Authority: **D3.4.5–D3.4.6**, **D5.1–D5.4**, **D8.4.1v2**, **D8.4.3v2**
> and **D8.6.1–D8.6.3**. No language ruling changes.
> Source base: `a436d168f`, including the separate callable/realm consolidation
> and the earlier [null-slot repair](Lambda_Impl_JS2_Performance.md).

## Task 1: source candidates and field guards

`lambda/runtime/mir_shape_candidates.hpp` now owns the resolved-binding walk
used by both Lambda and JS. The frontend supplies literal, parameter and
callee-result facts. Lambda retains its existing fixpoint. JS propagates the
first usable literal candidate through direct-call parameters and returns,
including expression-body arrows, in four bounded rounds. An unresolved field
can use a module-wide candidate when exactly one eligible literal defines it.
Literal recipes and the unique-field lookup are indexed once with the existing
`lib` hashmap helpers; property accesses do not repeatedly scan the AST.

The candidate is only a prediction (**D8.4.1v2**). Generated code resolves the
existing module recipe lazily once per function activation, then checks the
actual receiver kind, exact shape, plain-map policy, reservation state, data
bounds, slot flags, byte offset and current `LaneStorageDesc`. The shape handle
is pool-owned metadata and contains no receiver feedback or patched code. The
resolver's NO_GC contract is registered in both the import catalog and its
audited allowlist; the debug startup validator checks their agreement.

Admitted float, null and container fields use native loads/stores. The storage
domain is read at the access because the existing construction blueprint can
learn a lane before it is sealed. Incompatible values, descriptor changes,
accessors, deleted fields and other receivers retain `js_get_name_id` or
`js_set_name_id`. Profiling builds emit the existing named probe/hit/miss
counter calls; release builds omit them, and toggling `JS_OPT_TRACE` within a
profiling build leaves its MIR unchanged. Stores recheck after the RHS and
preserve the event-handler observer for `on...` names. The generated hot hit no longer calls
`js_shaped_slot_get`; that runtime helper remains available.

Both frontends now call `em_guard_container` for the common pointer/kind guard.
The JS-specific shape/storage admission remains local. This is an extraction
of shared mechanisms, not completion of the proposed universal operation-plan
interface or every field representation.

## Task 2: native indices and numeric elements

The key producer preserves its actual I64 or F64 carrier. It captures the
value before evaluating an assignment RHS, so changing the index variable
cannot retarget the store. An integer reaches addressing without converting
to double. A double must pass finite, nonnegative, integral array-index bounds
before machine conversion. Negative zero addresses zero; fractional, negative,
NaN, infinite and oversized keys take the semantic kernel.

`em_element_address` and `em_array_element_address` now serve Lambda and JS.
The direct JS paths admit:

- Present, in-bounds tagged array elements without descriptors or sparse/host
  attribute storage. Stores require a tagged elements state; unmanaged or
  numeric states retain the kernel's state transition. Copies of borrowed
  scalar homes stay in the owned writer.
- Packed Float64 `ArrayNum` storage, with the shared layout and capacity checks.
- Fixed Float64Array reads/writes and Int32Array reads with attached,
  non-resizable, non-shared buffers. Both use the same view/buffer checks and
  number publication. Int32 reads sign-extend and convert to JS Number; stores
  retain the existing ToInt32 kernel. A native store also requires writable,
  uniquely owned byte storage. Bounds and payload pointers are freshly loaded
  at each access.

Other element formats, holes, growth, accessors, detached or resizable views,
copy-on-write buffers and coercing stores retain the existing index kernels.
Native number consumers can retain an F64 element load where lowering already
requests a number; this does not infer that an arbitrary boxed JS value is a
number. Float boxing still handles signed zero, NaN and out-of-band values.

## Completion, suspension and ownership

The first large-library run exposed an error in the new emitted guard joins:
a successful hit reached a check of the fallback's uninitialized call register.
The fix publishes the joined Item through the existing error-result mechanism
(**D8.4.3v2**). Native results check the fallback before conversion and join with
a clean completion. CodeMirror's editor-construction regression exercises this.

Compound assignments also revealed an existing suspension bug: the old value
was held in a native activation across `yield`. The existing spill/root helpers
now preserve that value, and resumed references reconstruct their numeric key
from the spilled Item. A single helper is shared with binary-expression
preservation. No raw element or field address survives re-entry or suspension
(**D5.1–D5.4**).

## Validation

The required `make test-lambda-baseline` run reports **5,233/5,247 passing**
(`temp/js2-access/lambda-baseline-int32.log`). Its 14 failures are the seven
JSCU31 network fixtures, each present in the named regression and file suites.
All seven reproduce identical exit status, stdout and stderr on the matched
control and candidate releases (`network-phase3-comparison.json`). This includes
`bind EPERM`, missing network callbacks, and the existing DNS abort; the DNS
failure is not being relabeled as a compiler regression or a passing test.

Within that run:

| Gate | Result |
|---|---:|
| Lambda scripts | 851/851 |
| GC stress, including the new MIR fixture | 119/119 |
| Lambda / JS emission fixtures | 88/88; 22/22 |
| Optimization / coercion contracts | 19/19; 15/15 |
| Debug emission ratchet | 16/16 |
| Input baseline | 2,104/2,104 |

`test/js/js_shared_access_guards.js` covers candidate propagation, aliases,
rebindings, null and numeric lane changes, accessors and strict read-only writes,
holes, borrowed scalar copies, non-index keys, Float64Array subviews, detachment,
resizing, coercion, suspended compound assignment, index-variable mutation,
Int32 signed boundaries and detached Int32 subviews.
It matches Node's output. The focused new fixtures also match their golden
files under forced GC with poison at intervals 1, 7, 31 and 100. The new
`test/mir/js/shared_access_guards.mir-check` checks emitted guards, native element
addressing and retained semantic fallback calls (**D8.6.2–D8.6.3**).

The reviewed **D8.6.1** budget changes are local to the new guard paths:
`js_numeric_inference_call` moves from 229 to 295 release instructions, plus four
profiling instructions in debug; `js_main` moves from 168 to 234/238, with two
additional root stores and one cold float-boxing safepoint. The side-stack
sentinel moves from its 7,653-instruction budget to 7,960 release / 7,984 debug.
The latter includes six sets of profiling counters. Debug counters reuse
`js_opt_trace_record`; no counter is injected into release code. The pre-existing
release-only `lambda_scalar_home_tail_forward` ratchet failure (128 instructions
against 115) also occurs on the control; its budget is unchanged.

The final release passes **40,261/40,261 Test262 baseline cases**, including
async and modules, with zero regressions (`test262-completed.log`). Command:

```sh
./test/test_js_test262_gtest.exe --batch-only --baseline-only --run-async \
  --async-list=test/js262/test262_baseline.txt --jobs=4 \
  --write-failures=temp/js2-access/test262-completed-failures.txt
```

The full JS MIR sweep before the diagnostic/allowlist and Int32 additions passed
482/496; its only failures were the same 14 network cases. The final baseline
and Test262 runs above cover the completed source. A final release smoke run
also checks CodeMirror and both new regression fixtures, including all four
forced-GC intervals (`focused-completed.json`, 11/11).
All temporary artifacts are under `temp/js2-access/`.

## Matched release performance

The control and candidate both include `a436d168f`'s phase-3 callable/realm work
and the earlier null-slot repair. The control overlay restores the field/index
emitter changes to that commit and removes the new shape resolver; its source
recipe is `temp/js2-access/control-phase3-build.json`. Consequently, the large
construction gains in the earlier report are not attributed to these two tasks.

The completed binaries are:

| Binary | SHA-256 |
|---|---|
| `temp/js2-access/lambda-control-phase3` | `e1379a85dc15fec9f9ef95b679448b5e720f1a0cd20dfe76ff1cebe10013799a` |
| `temp/js2-access/lambda-candidate-final` | `3b05032a3467e39e5ea8e07f98c40643adc0353444e1883efc6645968e518f3f` |

`paired-completed.json` records five alternating control/candidate pairs per
unchanged JS program on macOS arm64, using release binaries on AC power.
No tests or builds were launched alongside this measurement. Every pair has matching
normalized stdout, with the timing marker removed. The artifact retains binary
and source hashes, individual execution/wall samples and exit status. These are
execution measurements; **D8.6.4v2**'s compiler-only consolidation gates require
separate captures. Times below are median execution milliseconds; speedup is
control divided by candidate, so values below 1 mean slower execution.

| Benchmark | Control (ms) | Candidate (ms) | Speedup | Candidate wins |
|---|---:|---:|---:|---:|
| r7rs/fft | 25.084 | 24.330 | 1.031× | 4/5 |
| awfy/nbody | 516.629 | 554.891 | 0.931× | 0/5 |
| awfy/richards | 963.861 | 1026.686 | 0.939× | 2/5 |
| awfy/deltablue | 469.731 | 490.188 | 0.958× | 1/5 |
| beng/binarytrees | 36.180 | 35.501 | 1.019× | 5/5 |
| beng/fannkuch | 26.983 | 23.385 | 1.154× | 5/5 |
| kostya/matmul | 658.288 | 326.796 | 2.014× | 5/5 |
| larceny/array1 | 33.769 | 16.998 | 1.987× | 5/5 |
| larceny/gcbench | 931.236 | 889.028 | 1.047× | 3/5 |
| larceny/quicksort | 136.887 | 152.317 | 0.899× | 3/5 |
| jetstream/splay | 392.189 | 397.367 | 0.987× | 2/5 |

The clear wins are `matmul` and `array1`, each faster in all five pairs. An
independent five-pair check of the same binary (`paired-int32-check.json`) gave
1.96× and 1.98×, respectively. Earlier Float64-only code made `array1` slower:
that benchmark uses Int32Array, so every read paid the new guards and then
fell back. Admitting signed Int32 reads through the shared buffer checks and
number-result join resolves that coverage gap; Int32 stores still take the
ToInt32 kernel.

The other rows do not support a universal speedup claim. An isolated 11-pair
repeat of the four slower rows (`paired-completed-followup.json`, same binaries,
all outputs matching) gave:

| Benchmark | Control (ms) | Candidate (ms) | Speedup | Candidate wins |
|---|---:|---:|---:|---:|
| nbody | 557.577 | 534.723 | 1.043× | 7/11 |
| richards | 1014.726 | 1003.034 | 1.012× | 6/11 |
| deltablue | 494.043 | 495.590 | 0.997× | 6/11 |
| quicksort | 146.221 | 154.326 | 0.947× | 4/11 |

The first three slowdowns did not persist. `quicksort` still takes 5.5% longer
by the follow-up medians; this is an unresolved performance limitation. Its
earlier Float64-only 21-pair follow-up was neutral, so guard/code-volume costs
after extending the numeric path merit targeted profiling; those costs have
not yet been causally isolated. No benchmark source or workload was changed.

Extra emitted guards also have a startup cost: the median wall-minus-execution remainder rises from
28.61 to 35.35 ms for `matmul` and 632.36 to 667.66 ms for `splay`. That remainder
includes startup, parsing, compilation and teardown; it is not an isolated
compiler measurement. The MIR budgets above capture the emitted-code increase.

A full-size `text_search` control/candidate checksum comparison also matched
(`paired-text-diagnostic.json`, Float64-only predecessor). It was a single
diagnostic pair with initial overlap from another benchmark, so its times are
not performance evidence. Earlier exploratory artifacts likewise do not
replace the completed-binary table above.

Reproduce the completed comparison with:

```sh
python3 test/benchmark/run_paired_benchmarks.py --language js \
  --control temp/js2-access/lambda-control-phase3 \
  --candidate temp/js2-access/lambda-candidate-final \
  --bench matmul,fft,quicksort,fannkuch,array1,richards,deltablue,nbody,binarytrees,splay,gcbench \
  --pairs 5 --output temp/js2-access/paired-completed.json
```

## Remaining coverage

The full JS2 program remains larger than these two tasks. Follow-on work is:

- Profile and reduce the remaining `quicksort` execution regression and the
  emitted guard cost, retaining the existing correctness checks and MIR gates.
- Additional direct field lanes (boolean, integer and string), Int32 stores,
  and additional ArrayNum / typed-array formats, using shared lane decoding
  and owned writes.
- Constructor-prefix and shared-transition candidates, plus a common operation
  plan that can reduce the remaining frontend-specific admission code.
- Guard and bounds hoisting only where shared effect facts prove that the owner,
  payload and storage witness survive the region (**D5.1–D5.4**, **D8.4.1v2**).
- The broader construction, native-call and completion consolidation described
  in the proposal. The separate phase-3 callable/realm work is already included
  in the source base and in both performance binaries.

These are coverage/extensions, not permission to weaken descriptor, coercion,
COW, precise-rooting or error-completion rules. Existing conformance failures
remain visible in their original tests.
