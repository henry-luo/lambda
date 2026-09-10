# Tune 23: Result41 typed execution overhead

- Date: 2026-09-10
- Status: targeted changes implemented and validated; broader tuning remains open.
- Input: patched `test/benchmark/Overall_Result41.md` and
  `benchmark_results_v41.json`; cached release `lambda-v41-57addc5cf4`, SHA-256
  `d2c8df4bd4fb1fe8c983ccdef752f2943c6970128e0ce1e33bf04434789c4cf8`.
- Authority: D3.3.3v3 (array representation certificates), D3.2.4v3 (record
  reification), D8.3.2–D8.3.3 (boundary proofs), D8.2.6 (producer-owned
  representations), D5.3.4 (precise roots), S7.1.3v2 (checked writes),
  S9.1.2/S9.2 (snapshots and exclusive mutable access).

## Implementation

1. **Immutable array producer admission.** A fresh text split whose local
   binding is used only by length and pure functions with the same explicit
   plain `string[]` contract can materialize that consumer representation once.
   Both split inputs must be proven non-null, error-free strings; refined or
   literal element contracts retain their original admission point.
   Mutable bindings, aliases, captures, open calls and other uses retain the
   original carrier and admission points. No inferred contract is imposed on
   an observable open array, and no mutable dispatch cache is introduced
   (D8.4.1v2).
2. **Open array descendants in typed records.** A declared `array` field
   imposes no element contract. Writes below an integer index in that field
   preserve the enclosing record contract without a staged copy of the entire
   root. The existing COW path walker owns path validation, snapshot isolation
   and the terminal store. Declared `T[]` descendants retain leaf admission.
3. **ArrayNum metadata after COW.** `cow_prepare_write` preserves the numeric
   container representation. MIR keeps that fact, checks allocation errors,
   and reloads a replaced payload descriptor instead of degrading the binding
   to `any`. This restores native numeric loads/stores after `var` borrows.
4. **Record construction at boundaries.** A scoped, node-specific map hint
   extends the existing constructor lowering to direct-call arguments and
   expression-bodied returns when every field is proven to need no semantic
   conversion. Known-layout records are allocated in their final representation; unproven fields keep
   the original whole-map boundary and evaluation order.

5. **Exact union proofs.** Identifiers with an enforced union contract reuse
   it at identical boundaries. A field read from an admitted union can reuse
   that same union contract when every map arm declares that field with the
   same non-null contract.
   Optional nulls retain the slow path; arms omitting the field keep full
   validation because open records may carry an unconstrained extra field.
   This does not assume that union members share a packed layout or that union-contained arrays are reified.
   Forward-call member facts are recovered only for stable values, preserving
   any explicit destination layout.

6. **Recursive union alias closure.** The direct parser published recursive
   map aliases through their pre-bound placeholder, but left recursive union
   references pointing to the empty placeholder map. The cached Result41
   binary accepts a branch with an invalid child. Closing the retained type
   wrapper over the actual union fixes membership and enables the field proof
   above. A visited type walk refreshes cached field storage and recomputes
   packed offsets only where a field width changes (D3.4.6). Regression coverage
   includes recursive arrays and a boolean field after the recursive member.

7. **Representation-preserving boxed array admission.** Reuse the existing
   `any[]` admission path for all boxed element contracts. Only publish a
   certificate after every element admission returns the identical Item, the
   full occurrence/rank contract matches, and neither a pointer lane nor a
   compact ArrayNum rebuild is required. Empty numeric arrays still rebuild.
   Conversions retain the transactional copy path. This preserves the original carrier across repeated `Doc[]`
   reads; open writes still invalidate its certificate (D3.3.3v3).
   Typed appends pass the source binding contract explicitly. Open boxed-array
   appends detach before clearing a certificate left by another alias; a past
   read admission does not constrain the open binding.

8. **Full compound call admission.** A direct call with an already boxed
   Array or union-valued argument still needs the parameter's full contract
   unless proved redundant.
   Previously the carrier alone could skip a refined string-array or union
   check; the JIT now rejects invalid elements at the original call boundary, matching
   the interpreter (D8.3.2–D8.3.3). Numeric-array boxed bodies already own
   their full entry admission, so their direct callers do not add another
   check. A seven-sample three-binary comparison isolated and removed a
   roughly 4% DeltaBlue regression caused by that duplicate boundary.

## Baseline recovery

The current tree also exposed a JS dynamic-Function early-error crash. Analysis
rejected invalid strict parameters after opening a MIR module, then cleanup
finished its context while that module remained open. Failed analysis now
closes the module before teardown; the Function constructor preserves the
catchable SyntaxError. This is a Lambda-side fix; vendor MIR is unchanged.
The existing `JsInterpreter.ThrowsCatchableSyntaxErrorsForInvalidDynamicFunctionBodies`
test covers it. External Node modules were rebuilt after the concurrent
JS runtime layout changes.

## Validation gates

- JIT/interpreter output parity for array producer admission and an escaping
  open-array control, typed nested writes with snapshots/rejected updates,
  numeric borrows, record constructors, and argument evaluation order.
- Optimization counters: repeated immutable reads must not allocate one
  admitted array per call; record constructors must avoid reification; the
  focused numeric-borrow loop must avoid checked stores.
- Forced allocator collection and freed-memory poisoning on the new fixtures.
- Full `make test-lambda-baseline` and `make test262-baseline`.
- Release-only paired measurements against the input cache, using unchanged
  benchmark sources and retaining individual samples and correctness output.

Current correctness results (2026-09-10):

- `make test-lambda-baseline`: **5,259/5,259**, comprising 2,104 input-parser
  cases and 3,155 runtime cases. Local socket access was required for the
  existing TCP/TLS tests; sandbox `bind EPERM` failures were not suppressed.
- Included gates: 88/88 MIR emission, 16/16 MIR size ratchets, 118/118 MIR
  forced-GC stress, 14/14 Lambda optimization contracts, 491/491 JS scripts,
  and 110/110 JS ownership/interpreter tests.
- Two MIR expectations now require explicit checked append. One reviewed
  instruction budget increases by five for COW allocation-error propagation.
  The checks still require native stores before the cold checked fallback.

- `make test262-baseline`: **40,261/40,261**, zero regressions.
- Final release: **22/22** new fixture executions (11 scripts on both JIT and
  interpreter) under `LAMBDA_GC_FORCE_EVERY=1` and
  `LAMBDA_GC_POISON_FREED=1`; **14/14** optimization-counter checks.
- Focused release counters: two mutable conversions for the split fixture
  including its open-alias control; zero constructor reifications; zero
  checked stores in the numeric-borrow loop; zero mutable copies for repeated
  union-array reads; 34 union admissions for the recursive field fixture.

## Measurement discipline

Diagnostics live under `temp/result41_reanalysis/` and `temp/tune23/`.
Final timings and release checks invoke the content-addressed cache directly: a
concurrent workspace build replaced `lambda.exe` with a debug binary, which
the release guard rejected. Compiler source hashes matched the retained cache.
Instrumented measurements establish operation counts separately from ordinary
timing samples.
Result41 is the input snapshot and is not rewritten to contain tuning results.
C2MIR uses MIR's C frontend as an external benchmark reference, not a Lambda
backend. Differences in allocation strategies are part of the measured gap;
only matched-work C2MIR rows should be used for attribution; concurrent
benchmark-port edits are outside this compiler measurement.

## Release measurements

Three interleaved fresh-process samples per binary and case, using in-script
kernel timing with JIT forced, MIR cache disabled, and profiling off.
All 72 runs returned matching correctness output. This is a focused comparison,
not a full Result42 run; Result41 remains the input snapshot.

| Benchmark | Version | Before (ms) | After (ms) | Speedup |
|---|---|---:|---:|---:|
| Three-way merge | typed | 31,850.900 | 6,242.220 | 5.102× |
| Prettier AST | typed | 9,688.120 | 2,677.600 | 3.618× |
| Richards | typed | 1,957.020 | 725.186 | 2.699× |
| DeltaBlue | typed | 45.730 | 45.850 | 0.997× |
| Navier–Stokes | typed | 1,603.940 | 261.174 | 6.141× |
| Primes | typed | 23.548 | 23.481 | 1.003× |
| Cube3d | typed | 29.595 | 29.451 | 1.005× |
| Log pipeline | typed | 6,824.430 | 6,580.580 | 1.037× |
| Three-way merge | untyped | 3,861.870 | 3,833.360 | 1.007× |
| Prettier AST | untyped | 1,092.200 | 1,085.190 | 1.006× |
| Richards | untyped | 422.792 | 427.972 | 0.988× |
| Navier–Stokes | untyped | 1,555.740 | 261.816 | 5.942× |

The largest median slowdown is **1.2% on untyped Richards**; DeltaBlue is
within 0.3% of its baseline after removing the duplicate numeric admission.
These small differences remain visible in the report; the sample size does
not establish whether the untyped Richards difference is repeatable.

The JSON preserves every timing sample, output hash, benchmark source hash,
binary hash, compiler source hash, validation result and focused operation count.
Three samples characterize these runs; they do not establish statistical confidence.
Other workspace activity was not controlled.

Final cached release: `test/benchmark/exe/lambda-tune23-23697ee47d8e`.
SHA-256: `23697ee47d8efc45feea5f264e1e74e2e3dd6c8481b70cd41d215eb398f96672`.
Source base: `e11c74392dff1441633fb1a2eaae8211b8933bcd` plus the recorded implementation patch.

Data: [benchmark_tune23.json](../../test/benchmark/benchmark_tune23.json).

Typed execution still takes more time than untyped execution in these cases:
Three-way merge 1.63×, Prettier AST 2.47×, Richards 1.69×. Those residual costs
remain tuning work.

## Remaining tuning areas

The subsequent [Tune24 implementation](Lambda_Impl_Tune24.md) adds branch-local
proofs, read-only borrowing intervals, and eligible typed record/destination
lowering. Broader scalar replacement and builder ownership outside that
eligibility, induction-variable range proofs, and call-site specialization
remain open (D8.3.1 permits one unboxed version per function).
They require explicit lifetime, effect and range proofs.
Loop-wide bounds/COW proofs and smaller precise-root frames around leaf calls
remain useful follow-ups beyond those delivered scopes. These are
implementation opportunities under D3.3.3v3, D5.3.4 and D8.3.1, not a request
to weaken source contracts or add a C-text backend.

The builder itself already exists from earlier tuning rounds. The live-source
audit, release reproductions, and revised extension proposal are recorded in
[String builder follow-up](Lambda_Impl_String_Builder_Followup.md).
