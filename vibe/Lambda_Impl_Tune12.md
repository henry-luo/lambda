# Lambda MIR Direct Tune12 — Closing the Result21 Front-End Gap

- **Status:** PROPOSED — evidence and implementation order defined; no Tune12 code landed
- **Date:** 2026-08-06
- **Scope:** Lambda MIR Direct performance after Tune11, using Result21, archived Result18,
  release profiles, and side-by-side Lambda/C2MIR MIR as evidence
- **Primary implementation area:** `lambda/runtime/transpile-mir.cpp` and Lambda-owned runtime
  helpers; the vendored MIR tree and the frozen legacy `--c2mir` path are out of scope
- **Baseline:** [`test/benchmark/Overall_Result21.md`](../test/benchmark/Overall_Result21.md)
- **Prior plan:** [`Lambda_Impl_Tune11 (done).md`](Lambda_Impl_Tune11%20%28done%29.md)
- **Numeric authority:** [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md)
  §4.7 and [`Lambda_Semantics_Int_Type.md`](Lambda_Semantics_Int_Type.md)

---

## 1. Executive decision

Tune11 recovered most of the severe Result20 regression, but Result21 still leaves a large,
measurable front-end cost:

- MIR untyped is **29.6% faster than Result20**, but **13.9% slower than Result18** on matched
  snapshot rows;
- MIR typed is **26.3% faster than Result20**, but **53.2% slower than Result18** on matched
  snapshot rows;
- LambdaJS is **39.6% faster than Result20**, but **9.0% slower than Result18**;
- MIR typed is **15.6x slower than C2MIR** over the fixed 43-row population where both have a
  timing in Result21.

The remaining bottleneck is not primarily MIR's backend. C2MIR uses the same MIR optimizer and
code generator, yet its hot functions are short native-lane loops while Lambda MIR repeatedly
boxes values, checks boundaries, dispatches generic indexing and mutation helpers, and maintains
root/scalar-home state around those helper calls.

The highest-confidence Tune12 order is:

| Track | Work | Primary evidence | Initial targets |
|---|---|---|---|
| T12-P0 | Freeze evidence, add MIR/profile counters, resolve missing typed rows | Result21 has 3 missing MIR-typed cells | `cd`, `hashmap`, `raytrace3d` |
| T12-A | Native `int div int` and `int % int` lanes with poison semantics | Collatz sample spends 1,203/3,100 samples in `fmod` | `collatz`, `paraffins`, `primes` |
| T12-B | General dense typed-array range proof, including integer lanes | `array1` keeps a nullable read/type check inside the guarded loop | `array1`, `fft`, `spectralnorm` |
| T12-C | COW-safe admitted typed-array borrow/raw-view capability | Quicksort profiles generic reads, writes, and validation after entry admission | `quicksort`, `fft`, `matmul`, `pnpoly` |
| T12-D | Propagate range/non-null/type facts and remove redundant boundaries | Matmul profile is dominated by `lambda_type_check` and numeric admission | `matmul`, `quicksort`, array kernels |
| T12-E | Evidence-gated native leaf/call-frame reduction | Recursive Result21 rows regressed and Lambda MIR has many more calls | `fib`, `tak`, `cpstak`, `ack`, `towers` |
| T12-F | Evidence-gated loop string builder and indexed text access | C2MIR's C ports use writable byte buffers, unlike Lambda strings | `base64`, `levenshtein` |
| T12-G | Separate evidence-gated LambdaJS residual tuning | LambdaJS remains 9.0% behind Result18 | `nbody`, `towers`, `splay` |

T12-A through T12-D are the core plan. T12-E starts only after their helper calls have been
removed, because static MIR call counts alone do not prove frame cost. T12-F and T12-G require
their own fresh profiles and must not be justified from the Lambda/C2MIR ratio alone.

---

## 2. Result21 checkpoint

### 2.1 Overall movement

The snapshot comparisons below match rows with a timing in both files. They are directional
evidence; retained patches still require archived-binary A/B runs over the same current fixture.

| Engine | Result21 / Result20 | Change vs Result20 | Result21 / Result18 | Remaining gap |
|---|---:|---:|---:|---:|
| MIR untyped | 0.704x | 29.6% faster | 1.139x | 13.9% slower |
| MIR typed | 0.737x | 26.3% faster | 1.532x | 53.2% slower |
| LambdaJS | 0.604x | 39.6% faster | 1.090x | 9.0% slower |

Result21's published engine/Node ratios are:

| Engine | Result21 / Node | Timed rows |
|---|---:|---:|
| MIR untyped | 3.17x | 56 |
| MIR typed | 2.85x | 53 |
| C2MIR | 0.17x | 44 |
| LambdaJS | 15.5x | 56 |

The MIR-typed headline excludes `awfy/cd`, `jetstream/hashmap`, and
`jetstream/raytrace3d`. Tune12 must report a fixed-population comparison as well as the headline;
fixing a missing slow row is a correctness improvement even if it makes a naive geomean worse.

### 2.2 Remaining Result18 regressions

The largest typed regressions are concentrated in recursive calls, integer loops, and typed
array mutation/access:

| Benchmark | Result21 MIR typed | Result21 / Result18 | Main Tune12 signal |
|---|---:|---:|---|
| `larceny/quicksort` | 41.5 ms | 19.27x | borrowed typed array still uses generic read/write/admission machinery |
| `awfy/towers` | 5.39 ms | 7.06x | recursive/native-call and container access overhead |
| `awfy/permute` | 3.12 ms | 6.34x | mutable array access plus call/frame overhead |
| `r7rs/cpstak` | 6.17 ms | 4.97x | recursive call/frame overhead |
| `r7rs/fft` | 5.00 ms | 4.90x | typed float-array access, checked writes, and integer loop arithmetic |
| `r7rs/tak` | 3.05 ms | 4.79x | recursive native-call overhead |
| `r7rs/nqueens` | 7.03 ms | 4.41x | backtracking array access and calls |
| `kostya/collatz` | 5.06 s | 3.74x | generic modulo reaches `fmod` |
| `awfy/storage` | 2.10 ms | 3.56x | allocation/container access and call overhead |
| `awfy/bounce` | 2.73 ms | 3.33x | mutable object/array loop overhead |
| `larceny/array1` | 12.7 ms | 2.93x | nullable typed read is re-admitted on every inner iteration |
| `r7rs/ack` | 66.3 ms | 2.71x | recursive call/frame overhead |

The untyped regressions are smaller in aggregate but expose two of the same mechanisms:
`collatz` is 3.28x slower than Result18, `ack` is 2.13x slower, and several JetStream rows retain
generic object/call overhead. Tune12 must not be a typed-only patch set.

### 2.3 C2MIR static ceiling

Result21 now records a reproducible C2MIR result:

- 46/46 C benchmark ports pass their expected-output check;
- 44/56 canonical Result21 rows have a C2MIR timing;
- 43 rows have both MIR-typed and C2MIR timings;
- MIR typed / C2MIR is **15.6x** over that fixed 43-row population;
- the C frontend is `lambda/mir/c2m` at pinned MIR commit
  `99c65079038f3ba9242ef646f308c266cfd7a8e5`;
- the measured `c2m` SHA-256 is
  `25bad0d7eeeae440559d1fab44ac55a642a6919cdb8c1fb36fbf6eb74b71a4fb`.

The widest Result21 gaps are:

| Benchmark | MIR typed | C2MIR | MIR typed / C2MIR |
|---|---:|---:|---:|
| `larceny/quicksort` | 41.5 ms | 0.202 ms | 205x |
| `r7rs/fft` | 5.00 ms | 0.025 ms | 199x |
| `awfy/towers` | 5.39 ms | 0.028 ms | 192x |
| `kostya/base64` | 82.1 ms | 0.564 ms | 146x |
| `awfy/permute` | 3.12 ms | 0.026 ms | 119x |
| `awfy/bounce` | 2.73 ms | 0.025 ms | 110x |
| `kostya/levenshtein` | 80.4 ms | 0.914 ms | 87.9x |
| `beng/spectralnorm` | 25.2 ms | 0.356 ms | 70.7x |
| `awfy/list` | 1.21 ms | 0.022 ms | 55.2x |
| `r7rs/nqueens` | 7.03 ms | 0.129 ms | 54.5x |
| `larceny/array1` | 12.7 ms | 0.322 ms | 39.5x |
| `larceny/paraffins` | 1.93 ms | 0.050 ms | 38.8x |

Ratios based on 0.02–0.05 ms native timings are sensitive to timer resolution and fixed runtime
overheads. They rank investigation targets; they are not literal per-row acceptance targets.

### 2.4 Native integer versus double evidence

[`test/benchmark/Result_Double_vs_Int.md`](../test/benchmark/Result_Double_vs_Int.md) compares
matched C2MIR integer and double ports:

- double/int geomean is only **1.33x** over 24 pairs;
- `collatz` is the exception at **6.99x**, because parity/modulo becomes floating-point work;
- `sum` is 2.68x, `diviter` 2.43x, `array1` 1.89x, and `primes` 1.56x;
- `quicksort` is only 1.19x and `nqueens` 1.35x.

Therefore the 15.6x Lambda/C2MIR gap is not explained by choosing the wrong numeric register
class alone. Integer modulo is a high-value isolated fix, while most of the remaining gap is
helper dispatch, repeated proof, boxing, boundary admission, and frame/root maintenance.

---

## 3. What the emitted MIR shows

### 3.1 Static size and call surface

The finalized frontend MIR was captured from the archived Result21 and Result18 binaries and
compared with C2MIR. These counts are audit signals, not dynamic cost by themselves.

| Benchmark | Result21 MIR lines / calls | Result18 MIR lines / calls | C2MIR lines / calls |
|---|---:|---:|---:|
| `array1` | 722 / 84 | 704 / 71 | 59 / 1 |
| `quicksort` | 2,179 / 238 | 2,003 / 154 | 138 / 6 |
| `fft` | 2,311 / 285 | 2,470 / 220 | 194 / 5 |
| `matmul` | 2,025 / 210 | 1,472 / 154 | 127 / 3 |
| `collatz` | 931 / 115 | 758 / 81 | 71 / 2 |

Specific Result21 imports/calls reinforce the profile evidence:

- `quicksort` references `fn_index`, `lambda_array_set_checked_inplace`, and
  `lambda_type_check`; C2MIR uses direct `i32` loads/stores and six calls in the whole module;
- `fft` has 14 `fn_index` references, 10 checked-array-set references, and seven
  `lambda_type_check` references; C2MIR has five calls;
- `array1` has a runtime `lambda_type_check` in the inner accumulation path; C2MIR's loop is one
  direct integer load, add, and store sequence;
- `collatz` falls through the generic `fn_mod` path even though both operands are `int`.

Result18 often has similar broad control flow but materially fewer calls and no corresponding
per-iteration typed boundary checks in these hot paths. Tune12 should reduce the dynamic helper
surface, not merely shrink textual MIR.

### 3.2 `array1`: a proven loop still carries nullable work

Lambda correctly gives a total `int[]` read type `int?`: an out-of-bounds access must return
`null`. The current compiler has `mir_expr_proven_nonnull_under_dense_guard()` and a dense
in-bounds branch in `emit_checked_index_load()`, but the implementation is incomplete:

1. the integer `ArrayNum` caller does not pass the `dense_inbounds` proof;
2. the dense branch itself hard-codes `MIR_T_D`, `MIR_DMOV`, and a double load;
3. the loaded integer therefore stays nullable through the inner expression;
4. assigning the sum back to `int` calls `lambda_type_check` and numeric admission each
   iteration.

This is now qualifying evidence for the Tune11-E direction that was previously deferred: the
compiler has a real in-bounds proof, but it does not discharge nullability for integer lanes.
The fix is to complete and generalize the proof path, not to change total-read semantics.

### 3.3 `quicksort`: entry admission is not a usable hot-loop capability

The typed `partition(var arr: int[], lo: int, hi: int)` body receives native scalar arguments
and calls `ensure_typed_array` at the array boundary. However, its hot loop still uses generic
container and validation machinery.

An enlarged archived-Result21 release sample reported these top-of-stack counts:

| Runtime work | Samples |
|---|---:|
| `validate_occurrence_type` | 321 |
| `lambda_type_matches` | 201 |
| `pool_calloc` | 139 |
| `lambda_numeric_boundary_admit` | 134 |
| `validate_against_type` | 112 |
| `fn_index` | 99 |
| `fn_array_set` | 87 |
| `array_num_read_item` | 82 |
| `lambda_type_lane_storage_desc` | 70 |
| `lambda_side_root_alloc_n_for` | 61 |
| `lambda_type_check` | 61 |
| `item_at` | 53 |

The missing abstraction is an admitted borrow that the emitter can consume repeatedly. The
function boundary proves the element contract once, but a `var` parameter cannot use the current
non-borrowed typed-array layout cache because mutation may detach, demote, reallocate, or replace
the caller-visible owner. Tune12 needs a COW-aware capability, not an unsafe permanent raw pointer.

### 3.4 `fft`: direct doubles are obscured by generic array and integer operations

C2MIR's FFT uses direct double pointer loads/stores. Lambda's typed body enters with `float[]`
contracts but still emits generic index fallback, checked writes, boxing, type checks, and
integer `div`/conversion helpers around loop setup and bit reversal.

The dense float proof already demonstrates the right shape, but it is limited to recognized
matrix-style expressions and is not shared by all scalar loops or integer index lanes. T12-B and
T12-C should make the same proof/capability usable by ordinary one-dimensional and nested affine
loops.

### 3.5 `collatz`: integer modulo is routed through `fmod`

The archived Result21 release sample contains 3,100 samples. Its hot chain includes:

- 1,564 samples under `fn_mod`;
- 1,203 samples directly in `fmod`.

`get_effective_type()` currently forces `div` and `%` to `LMD_TYPE_ANY`, and
`transpile_binary()` falls through to boxed `fn_idiv`/`fn_mod`. Comments still refer to a
`fn_idiv_i`/`fn_mod_i` fast path, but no emitter call site uses those helpers. The old helpers also
return `INT64_ERROR` for zero, which does not implement current int-v5 poison semantics and must
not simply be wired back in.

This is the clearest first Tune12 implementation target.

### 3.6 `matmul`: repeated boundary admission remains visible

Tune11 removed catastrophic whole-array cloning and improved typed `matmul` from 1,324 ms to
76.0 ms. A larger archived-Result21 profile still sampled 1,515 calls in
`lambda_type_check`, including 1,028 samples in `lambda_numeric_boundary_admit`.

That evidence says repeated numeric admission remains expensive. It does not yet prove which
source boundary is responsible for every sample. T12-P0 must attribute checks to parameter,
assignment, return, and indexed-write sites before T12-D removes any of them.

---

## 4. What to learn from C2MIR

### 4.1 Adopt these structural properties

1. **Admit once, operate many times.** C parameters already have a fixed representation. Lambda
   should turn an exact typed boundary into an explicit capability and reuse it until an
   invalidating operation.
2. **Keep hot values in native lanes.** A loop counter, finite int, or loaded float should not be
   boxed only to be immediately type-checked and unboxed.
3. **Use direct memory after a dominating proof.** Type, layout, extent, uniqueness, and view
   state can be checked at a loop/function boundary; the guarded body can use raw loads/stores.
4. **Keep native direct calls native.** A specialized caller and callee should not rebuild an Item
   adapter, root frame, or scalar home when no managed value crosses that edge.
5. **Make fallback explicit and cold.** Dynamic values, out-of-bounds reads, shared COW owners,
   views, poison, and representation changes still need the general runtime path, but they do not
   belong on the proved hot arm.

### 4.2 Do not copy these C assumptions

C2MIR is a static ceiling, not a language specification:

- C array reads are not total; Lambda reads are nullable unless a dominating proof discharges
  the out-of-bounds case;
- C mutates aliases directly; Lambda must preserve value semantics, COW isolation, and `var`
  caller-visible replacement;
- C integer division by zero is undefined or trapped; Lambda produces domain poison for computed
  zero divisors;
- C strings are writable byte buffers; Lambda strings are immutable Unicode values;
- C ports may use a different storage strategy or equivalent algorithm, especially in `base64`
  and `levenshtein`;
- C has no precise Lambda GC root protocol.

No Tune12 phase may edit `lambda/mir/`, extend the frozen legacy C2MIR transpiler, or weaken Lambda
semantics to make its MIR resemble C.

---

## 5. Non-negotiable invariants

1. **Total reads remain total.** Without a dominating proof, `T[]` indexing returns `T?` and OOB
   returns the correct null lane/Item.
2. **Integer division/modulo remain closed and total.** `div` truncates toward zero; `%` takes the
   dividend's sign; computed zero produces `inf`, `-inf`, or `nan`; literal zero remains a compile
   error.
3. **Int-v5 sentinels are values in the native lane.** Native arithmetic must propagate
   `INT_LANE_NAN`, `INT_LANE_NEG_INF`, and `INT_LANE_INF` correctly and must never expose them as
   Items or array payloads accidentally.
4. **Type boundaries still enforce contracts.** A proof may remove a repeated check only when it
   proves the same admission relation, including nullability, numeric range, occurrence, and
   structural type.
5. **COW value semantics remain observable.** A direct store is legal only for an exact,
   writable, unique, flat owner. Shared owners, borrowed children, views, and representation
   changes take the existing detach/demotion path.
6. **`var` write-back remains caller-visible.** A callee may not cache a raw owner that becomes
   stale after COW replacement.
7. **Views and sized lanes stay distinct.** Element width, signedness, stride, offset, and
   nullable storage come from the shared lane/layout descriptors, never from copied per-type
   assumptions.
8. **GC remains precise.** Managed owners are held by `RootFrame`/`Rooted`-equivalent ownership;
   native payload pointers are derived capabilities, not roots. Conservative native-stack
   scanning must not return.
9. **Fallback behavior stays available.** Any failed proof or invalidated capability reaches the
   ordinary checked runtime implementation.
10. **Representation is unobservable.** Fast and slow arms must have identical results, errors,
    mutations, and printed output.

---

## 6. T12-P0 — Evidence, attribution, and correctness blockers

### 6.1 Freeze the baseline population

Retain these immutable references before code changes:

- Result21 binary: `test/benchmark/exe/lambda-v21-6fcf2283fa`;
- Result21 archive SHA-256:
  `45284f9c107ccf73feec210983ba32df3e4ab0db8d25ed49b2a6804d428fcc63`;
- Result18 binary: `test/benchmark/exe/lambda-v18-e406aa9b87`;
- Result21 JSON and C2MIR sidecar;
- the fixed 43-row MIR-typed/C2MIR population;
- a fixed Result18/Result21 matched population for MIR untyped, MIR typed, and LambdaJS.

Temporary dumps, profiles, and generated probes belong under `./temp/tune12/`. The durable facts
needed to interpret a retained patch must be copied into this ledger or the next benchmark report;
completion must not depend on an untracked temporary artifact.

### 6.2 Add a reproducible MIR-gap report

Add a Lambda-owned benchmark analysis script that reads finalized MIR and reports, per function:

- line and instruction count;
- call/import count and call targets;
- `fn_index`, checked-set, `lambda_type_check`, numeric-admission, boxing/unboxing, side-root, and
  scalar-home calls;
- direct memory loads/stores by lane;
- loop-local versus entry/cold-fallback call placement where detectable.

The report must compare the same source under Result18, Result21, the candidate, and C2MIR. It is
an audit tool, not an acceptance metric: fewer lines are not a win if runtime or code size
regresses.

### 6.3 Attribute dynamic checks to source boundaries

Extend existing disabled-by-default diagnostics rather than adding unconditional release work.
For a profiling build, count:

- parameter, return, local assignment, and indexed-write admission separately;
- dense-proof attempts, successes, and miss reasons;
- typed-array capability creation, direct reads/writes, invalidations, and fallbacks;
- int `div`/`%` finite fast paths, poison paths, zero-divisor paths, and generic fallbacks;
- direct native calls versus boxed adapters;
- root/scalar-home setup skipped or retained.

Each line starts with a distinct `mir-t12-*` prefix. The canonical release benchmark binary must
pass the existing instrumentation check and contain none of these profile paths when the profile
feature is disabled.

### 6.4 Resolve missing typed rows before headline comparison

`awfy/cd` times out, while `jetstream/hashmap` and `jetstream/raytrace3d` exit with `-11` in the
typed column. Establish the narrowest current repro and classify each as:

- stale fixture/annotation issue;
- compiler representation or rooting defect;
- runtime semantic defect;
- true performance timeout.

Correctness fixes land before tuning those rows. Add a root-cause comment at each fix point and a
focused regression. Do not mark Tune12 complete with a typed crash hidden as a missing timing.

### 6.5 P0 exit criteria

- [ ] MIR-gap reporting reproduces the static counts in §3.1.
- [ ] Profile counters reproduce the `collatz`, `quicksort`, and `matmul` attribution in §3.
- [ ] Every missing MIR-typed row has a focused issue classification and regression test.
- [ ] Fixed benchmark populations and archived hashes are recorded.
- [ ] No performance implementation starts from MIR line count alone.

---

## 7. T12-A — Native int `div` and `%` with int-v5 semantics

### 7.1 Root cause

`get_effective_type()` reports all `div`/`%` expressions as `LMD_TYPE_ANY`, after which
`transpile_binary()` boxes both operands and calls `fn_idiv` or `fn_mod`. For finite `int`
operands, this discards the i64 lane and eventually performs double/floating-point work.

The existing `fn_mod_i`/`fn_idiv_i` helpers are not a valid shortcut: they predate int-v5's
poison contract and return `INT64_ERROR` for zero. Tune12 must implement the current semantics in
the shared int-lane layer.

### 7.2 Shared lane operation

Add one shared int-lane binary-operation shape for `div` and `%`, following the existing
add/sub/mul fast/slow organization:

1. if both operands are statically proven finite and the divisor is proven nonzero, emit native
   signed `DIV`/`MOD` directly;
2. otherwise classify the two lane values once;
3. finite, nonzero inputs use native signed division/modulo;
4. a computed zero divisor returns the correct lane poison:
   - nonzero dividend `div 0` produces signed infinity;
   - `0 div 0` and `0 % 0` produce nan;
   - modulo by zero follows the normative int-domain poison rule;
5. poison inputs propagate according to the numeric semantics;
6. nullable inputs propagate absence only when the expression type is nullable;
7. dynamic or non-int operands remain on `fn_idiv`/`fn_mod`.

Use a shared helper/table for the two operators. Do not create a third copy of int sentinel
classification. Guard native division against the C/MIR signed-overflow corner even though the
finite int53 band cannot contain `INT64_MIN`.

### 7.3 Type and proof propagation

For exact `int div int` and `int % int`:

- report the result as an int lane, not `ANY`;
- preserve nullable occurrence if either input can be null;
- let a checked `int` destination consume the proven lane without box/check/unbox;
- recognize `int(a div b)` as redundant only when the inner operation already has exact int-v5
  semantics;
- carry a nonzero divisor fact from literal/range/control-flow guards such as `b != 0`.

Do not globally treat a runtime int parameter as finite: int-v5 poison is a member of the type.
Finite facts come from literals, bounded loop/range facts, successful finite checks, or operations
whose result proof establishes the band.

### 7.4 Focused tests

Add scalar and MIR-emission coverage for:

- positive and negative operands, pinning truncation toward zero and dividend-sign remainder;
- all four sign combinations;
- computed `+0` and `-0` divisors;
- `0 div 0`, nonzero `div 0`, and `% 0` poison;
- nan and both infinities as either operand;
- `INT53_MIN`, `INT53_MAX`, and values adjacent to the saturation boundary;
- nullable `int?` operands;
- dynamic mixed numeric fallback;
- sized-int operands leaving their machine lane under §4.7;
- literal-zero compile rejection remaining unchanged.

Add a `test/mir/lambda/int_div_mod_lane.mir-check` fixture that proves the finite hot path has no
`fn_mod`, `fn_idiv`, `fmod`, boxing, or numeric boundary call, while its cold semantic arms remain.

### 7.5 Performance acceptance

Retain T12-A only if an interleaved release comparison shows:

- typed and untyped `kostya/collatz` each improve by at least **2.0x**;
- `fmod` is no longer a top worker leaf for the int benchmark;
- `paraffins` or another division-heavy control improves by at least **15%** if its static proof
  enters the new path;
- float/double modulo controls do not regress by more than 3%;
- no correctness or poison test changes output.

The 6.99x C2MIR double/int result is a ceiling for the modulo portion, not a promise that Lambda's
whole `collatz` benchmark will improve by 6.99x.

---

## 8. T12-B — General dense typed-array proof

### 8.1 Complete the existing dense path

Refactor `emit_checked_index_load()` so its dense arm is lane-generic:

- select memory type, move opcode, width, signedness, result kind, and null representation from
  `MirIndexLoadPolicy`/the shared lane descriptor;
- remove the hard-coded `MIR_T_D`/`MIR_DMOV` load;
- pass `dense_inbounds` from integer, float, bool, sized-int, int64, and uint64 callers where the
  same proof is valid;
- use one parameterized implementation rather than per-lane copies;
- preserve the ordinary checked arm as the false branch.

### 8.2 Expand loop/range recognition

The proof should recognize common canonical loops without becoming a general optimizer:

- `0 <= i && i < len(a)` and `i < n` where a dominating guard proves `n <= len(a)`;
- loops with nonzero lower bounds such as `lo <= i && i < hi`;
- affine indexes `i`, `i + c`, `i - c`, and `base + i` with proved range;
- nested row-major indexes `i * n + j` with proved nonnegative dimensions and total extent;
- fixed-size arrays created by `fill(size, value)` when no invalidating write changes length or
  representation;
- paired arrays whose independent lengths are each guarded.

Keep the proof conservative. Unknown calls, mutation of the bound/length, negative strides,
views, aliasing length changes, or arithmetic overflow invalidate it.

### 8.3 Propagate non-null on the true arm

On the dense true branch:

- a loaded `T?` becomes exact `T` for that expression;
- arithmetic and comparison consume the native lane directly;
- a destination with the same exact contract does not call `lambda_type_check` merely to reject
  the OOB null that the guard already excluded;
- joining with the false branch restores the declared nullable result.

The proof is path-local. It must not rewrite the AST's source-level total-read type globally.

### 8.4 Dense checked writes

After T12-C establishes a writable capability, use the same range proof to remove per-iteration
bounds checks from stores. A dense proof alone does not prove uniqueness, flat layout, or COW
ownership; those remain separate conditions.

### 8.5 Focused tests

Extend `test/mir/lambda/typed_array_guard.mir-check` and add Lambda fixtures for:

- dense `int[]`, `float[]`, bool, sized-int, int64, and uint64 reads;
- exact-end and one-past-end bounds;
- negative start/index;
- nested affine matrix access;
- overflow in `i * n + j`;
- array length or representation mutation inside the loop;
- generic arrays and shaped/N-D views taking the checked fallback;
- OOB null being rejected by a plain `int` destination;
- nullable lane reads remaining null outside the proved arm.

Reuse and extend:

- `test/lambda/proc/proc_typed_array_param.ls`;
- `test/lambda/proc/proc_typed_array_guard.ls`;
- `test/lambda/proc/proc_nullable_int_lane_read.ls`;
- `test/lambda/negative/runtime/nullable_index_read_reject_plain_int.ls`;
- `test/lambda/typed_array_ndim_index.ls`.

### 8.6 Performance acceptance

- typed `array1` improves by at least **2.0x** from 12.7 ms;
- its hot inner loop contains no `fn_index`, `lambda_type_check`, or numeric-admission call;
- typed `fft` improves by at least **20%** if the new proof covers its loops;
- no typed-array, nullable-read, view, or OOB control regresses by more than 3%.

---

## 9. T12-C — Admitted typed-array borrow/raw-view capability

### 9.1 Required capability

After an exact typed-array boundary succeeds, represent the reusable proof explicitly. The
capability needs at least:

- rooted owner Item or caller-visible owner slot;
- current `ArrayNum*`/container pointer;
- current payload pointer and logical length;
- element kind, width, signedness, and nullable storage descriptor;
- flat/view/stride state;
- value versus `var` borrow mode;
- uniqueness/COW state for writes;
- an invalidation/version state understood by the emitter.

Use or extend existing `MirVarEntry`, typed-array cache, lane descriptor, and root ownership
structures. Do not create a parallel type/layout system.

### 9.2 Value parameters

For a non-`var` exact `T[]` parameter:

- admit/convert once at the function boundary;
- root the owner for the full capability lifetime;
- cache pointer, payload, length, and exact element layout;
- emit direct reads under the bounds proof;
- invalidate on an operation that can change the local representation or payload;
- never publish a detached replacement to the caller.

The current non-borrowed cache is the starting point; Tune12 generalizes its validity proof and
connects it to dense loops.

### 9.3 `var` parameters and caller-visible replacement

A `var T[]` capability must carry the owner slot, not only the incoming pointer:

1. load the current owner from the rooted slot;
2. before a write, prove exact element representation, flat layout, bounds, and uniqueness;
3. store directly only on that arm;
4. otherwise call the existing checked COW/demotion path;
5. if the slow path replaces the owner, write the replacement through the caller-visible slot;
6. refresh the cached pointer/payload/length before subsequent direct access.

Recursive quicksort is the primary test: recursive calls mutate the same logical array while each
frame must observe the current owner. A capability may survive a call only when the callee's
effects prove that it cannot replace, resize, demote, or reallocate that owner.

### 9.4 Invalidation rules

Invalidate or reload after:

- any store that can change representation;
- any resize/append/insert/remove operation;
- any call receiving the same array as `var` or dynamic input;
- any operation that can expose a mutable alias;
- view creation or a transition to shaped/N-D layout;
- a checked slow path that may detach or demote;
- any runtime operation whose GC/relocation contract does not guarantee payload stability.

Do not assume that a raw payload pointer is kept alive by being in a native register. Its rooted
owner is the lifetime authority.

### 9.5 Direct store policy

The direct store arm requires all of:

- exact trusted `T[]` contract;
- value admitted to the exact lane, including range/nullability;
- current element representation matches;
- flat, non-view storage;
- index proved in bounds;
- owner unique and writable;
- no borrowed child/path condition requiring detach;
- destination owner slot available if replacement can occur.

If any condition fails, use `lambda_array_set_checked_inplace`/the existing checked setter. Do
not weaken it or duplicate its semantic logic in MIR.

### 9.6 Focused tests

Cover:

- read-only value parameter direct access;
- caller-visible `var` mutation;
- shared owner detaching before write;
- nested/shared child arrays;
- representation-preserving int/float/sized stores;
- nullable demotion and null sentinel storage;
- heterogeneous store rejection at a typed boundary;
- view and N-D fallback;
- recursive quicksort-like calls;
- invalidation after resize, helper call, and slow checked store;
- forced GC between admission, load, detach, store, and reload.

Reuse `proc_typed_array_param.ls`, `proc_typed_array_guard.ls`, `proc_array_set.ls`,
`proc_nullable_native_array.ls`, `proc_nullable_native_float_array.ls`,
`proc_nullable_native_sized_array.ls`, `proc_nullable_native_int64_array.ls`, `cow_alias.ls`,
`cow_ordering.ls`, and `cow_element_snapshot.ls`.

### 9.7 Performance acceptance

Retain T12-C only if:

- typed `quicksort` improves by at least **3.0x** from 41.5 ms;
- `fn_index`, `fn_array_set`, and occurrence validation are no longer dominant worker leaves in
  its admitted hot loop;
- typed `matmul` or `pnpoly` improves by at least **15%** if its capability path is entered;
- no COW, `var`, nullable, view, or forced-GC fixture changes behavior;
- code size does not grow by more than 10% on the representative MIR modules without a measured
  runtime win.

---

## 10. T12-D — Range facts and redundant boundary elimination

### 10.1 Separate facts that are currently conflated

Track these independently through `MirValue`/variable metadata:

- semantic type and occurrence/nullability;
- physical native lane;
- finite/non-poison fact;
- int53 in-band fact;
- nonnegative/nonzero/range fact;
- exact typed-array admission capability;
- COW ownership/write capability.

A native i64 carrier is not evidence that the value satisfies `int`; an exact `int` contract is
not evidence that the value is finite; and an exact array element type is not evidence that the
owner is unique. Boundary elimination is legal only from the fact it actually needs.

### 10.2 Loop induction facts

For canonical loops, establish once:

- initial counter range;
- bound range and stability;
- step direction and nonzero step;
- arithmetic staying inside int53;
- derived index range.

Use the proof to remove repeated int53 checks and slow helpers from induction increments and index
arithmetic. If a loop cannot prove closure, retain the current checked int-lane operation or guard
the optimized loop with a cold fallback.

### 10.3 Boundary proof rules

Extend `mir_boundary_is_redundant()` only for explicit, auditable cases:

- an exact native scalar parameter already admitted at the same or stronger contract;
- a dense in-bounds typed load whose non-null element contract equals the destination;
- a finite/in-band int-lane operation assigned to exact `int`;
- an exact float/int/sized store already admitted by the array capability;
- a return whose body result carries the exact declared contract.

Never elide structural validation from Shape identity unless the Shape is an audited immutable
contract Shape. Dynamic/input/JS Shapes remain on validation paths.

### 10.4 Site attribution and ratchets

Every removed hot boundary must have:

- a source-site category from T12-P0;
- a MIR-check assertion proving the call is absent on the hot arm;
- a negative fixture that reaches the retained slow/error arm;
- a measured profile showing the removed category mattered.

For `matmul`, first identify whether the dominant numeric admissions come from local sum updates,
array initialization, checked stores, parameter entry, or return. Do not add a broad
`lambda_type_check` bypass.

### 10.5 Performance acceptance

- `lambda_type_check` plus `lambda_numeric_boundary_admit` falls below 10% of sampled worker time
  in the declared `matmul`/`quicksort` target;
- the target improves by at least **15%** beyond T12-B/C;
- proven loop increments do not call `lambda_int_lane_*_slow` on the hot arm;
- numeric-boundary negative tests and structural admission tests remain unchanged;
- no untargeted benchmark regresses by more than 3%.

---

## 11. T12-E — Native call and leaf-frame reduction, conditional

### 11.1 Entry condition

Start T12-E only after T12-A through T12-D are integrated and a fresh release profile attributes
at least 10% of worker time in a recursive target to one or more of:

- boxed direct-call adapters;
- root-frame or scalar-home creation/restoration;
- side-root allocation;
- boxing/unboxing native parameters and returns;
- repeated stack-overflow/recovery-frame setup that can be safely shared or hoisted.

Static call counts and a slow recursive row are not sufficient by themselves.

### 11.2 Leaf/native frame plan

Classify specialized functions by emitted effects:

- managed values live across calls;
- allocating/GC-capable calls;
- raising/recovery edges;
- closure/capture use;
- dynamic calls;
- caller-visible `var` ownership;
- native-only parameters/return.

A function may use a reduced native frame only when the emitted body proves no managed root or
scalar home is needed. Direct native recursive calls may reuse the native ABI, but must preserve
stack-overflow checks, error propagation, and precise rooting whenever a managed value appears.

### 11.3 Tests and acceptance

Add MIR checks for native leaf, recursive native, managed recursive, closure, raising, and `var`
cases. Run forced GC and stack-overflow fixtures.

Retain only if:

- at least one of `fib`, `tak`, `cpstak`, `ack`, `towers`, or `quicksort` improves by 15%;
- the affected recursive Result21/Result18 gap is reduced by at least half;
- no stack-overflow, exception, closure, indirect-call, or GC-root fixture regresses.

---

## 12. T12-F — String/byte loop lowering, conditional

### 12.1 Why the C2MIR ratio is not enough

The `base64` C port writes a mutable byte buffer; Lambda builds immutable strings and indexes
string/table values. `levenshtein` likewise uses C character arrays while Lambda exercises string
indexing and typed row storage. Their 146x and 87.9x C2MIR ratios combine code-generation cost,
storage model, and algorithmic representation differences.

Before tuning, fix or isolate any correctness issue in typed string returns, concatenated integer
lengths, or GC ownership exposed by the current ports. A crashing or miscompiled typed fixture is
not a benchmark target.

### 12.2 Entry condition

Enter T12-F only when a release allocation/profile run attributes at least 15% of a target to:

- repeated loop-carried `s = s ++ piece` allocation/copy;
- string builder growth/copy;
- generic string/byte indexing;
- repeated one-character boxing or UTF-8 rescanning.

### 12.3 Allowed implementation

- recognize an owned loop-carried concatenation accumulator and lower it to the existing mutable
  string/buffer machinery;
- append batches or exact known-width scalar pieces;
- preserve Unicode indexing semantics and use byte access only where the source type proves
  binary/ASCII byte semantics;
- finalize one immutable String at the observable boundary;
- root the builder precisely and detach on alias/escape.

Do not expose string mutability, reinterpret Unicode indexes as bytes, or copy the C port's
storage assumptions into Lambda.

### 12.4 Acceptance

A retained string change must improve its profiled target by at least 20%, keep allocation/bytes
copied lower by the predicted amount, and pass Unicode, empty-string, binary, alias, and forced-GC
tests. If the entry profile does not qualify, defer the track.

---

## 13. T12-G — LambdaJS residual, separate and conditional

Result21 LambdaJS is 39.6% faster than Result20 but remains 9.0% behind Result18. The largest
remaining snapshot regressions include `nbody` (2.12x), `towers` (1.56x), and `splay` (1.54x).

Do not infer a JavaScript bottleneck from Lambda MIR or C2MIR. Collect fresh
`JS_EXEC_PROFILE=2` profiles for those exact rows and rank property access, array access, calls,
boxing, allocation, and GC from the measured counts/time. Reuse the existing JS execution profile
events; add a new event only when no existing category can answer the question.

Retain a T12-G patch only when:

- one measured category accounts for at least 10% of target worker time;
- the patch improves the target by at least 10%;
- it recovers at least half of that target's Result18 gap;
- `make test262-baseline` remains 40,261/40,261 with zero retries/regressions;
- no JS semantic/prototype/descriptor/GC fast-path invariant is weakened.

This track may become its own JS-specific tuning plan if the profile identifies more than one
independent mechanism.

---

## 14. Implementation sequence

### Phase P0 — Evidence and correctness

- [ ] Freeze fixed populations, hashes, and archived binaries.
- [ ] Add the reproducible MIR-gap report and disabled profile counters.
- [ ] Reproduce the three key profiles.
- [ ] Resolve/classify typed `cd`, `hashmap`, and `raytrace3d` failures.

### Phase P1 — T12-A int `div`/`%`

- [ ] Implement shared int-v5 lane semantics.
- [ ] Propagate exact lane/result facts.
- [ ] Add scalar and MIR checks.
- [ ] Run Collatz-focused A/B and retain or roll back.

### Phase P2 — T12-B dense reads

- [ ] Generalize `emit_checked_index_load()` by lane descriptor.
- [ ] Pass dense proofs through int/sized/float callers.
- [ ] Propagate non-null only on the guarded arm.
- [ ] Run `array1`/`fft` A/B and nullable/OOB tests.

### Phase P3 — T12-C admitted array capability

- [ ] Extend non-borrowed cache into an explicit capability.
- [ ] Add owner-slot semantics and invalidation for `var` arrays.
- [ ] Add direct unique flat writes and cold checked fallback.
- [ ] Run quicksort/matmul/pnpoly A/B and COW/forced-GC tests.

### Phase P4 — T12-D proof propagation

- [ ] Attribute remaining boundary checks by source category.
- [ ] Add induction/range facts.
- [ ] Remove only exact redundant boundaries.
- [ ] Run post-C `matmul`/quicksort profiles and A/B.

### Phase P5 — Conditional tracks

- [ ] Re-profile recursive targets; enter T12-E only if its 10% gate passes.
- [ ] Profile string targets; enter T12-F only if its 15% gate passes.
- [ ] Profile LambdaJS targets; enter T12-G only if its 10% gate passes.

### Phase P6 — Integrated release

- [ ] Run all correctness and baseline gates.
- [ ] Run fixed-population interleaved Result21/candidate comparisons.
- [ ] Run the full standard benchmark suite and capture Result22.
- [ ] Record retained/rejected experiments and final hashes in this ledger.

---

## 15. Correctness gates

### 15.1 Focused after every affected phase

- `./test/test_mir_emission_gtest.exe` with the exact new filter;
- `./test/test_mir_gc_stress_gtest.exe` for ownership/root changes;
- `./test/test_item_repr_gtest.exe` for int/array lane changes;
- `./test/test_lambda_errors_gtest.exe` for numeric admission/poison changes;
- exact `.ls`/`.txt` fixtures for typed arrays, COW, nullable lanes, and numeric boundaries;
- exact benchmark output/exit checks for every performance target and control.

Any new Lambda unit script gets its matching expected `.txt` file.

### 15.2 Forced-GC matrix

Run affected array, call-frame, and string fixtures under:

```bash
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./lambda.exe run <fixture>
```

Use `run` only for procedural fixtures. Include JIT and MIR-interpreter coverage where the existing
test harness supports both. No test may rely on conservative stack scanning.

### 15.3 Broad gates

Before retaining an integrated Lambda runtime/compiler phase:

```bash
make release
make test-lambda-baseline
make test262-baseline
```

The Test262 gate is 40,261/40,261 with zero failures and zero retries. Do not update a baseline to
hide a regression. `make node-baseline` is not part of this plan unless separately requested.

---

## 16. Performance protocol

### 16.1 Build and host discipline

- use `make release`; never benchmark a debug build;
- AC power connected, low-power mode off, thermally idle before each group;
- reject binaries containing enabled profile instrumentation;
- preserve exact binary size, commit, and SHA-256;
- use workload-reported `__TIMING__`, exact output, and exact exit status;
- keep fixtures identical between archived Result21 and candidate binaries.

### 16.2 Focused A/B protocol

For each phase:

1. run archived Result21 and candidate in interleaved `A/B/A/B/A/B` order;
2. use at least five measured pairs for sub-10 ms rows and at least three for long rows;
3. compare medians and retain all individual timings;
4. rerun with reversed first-engine order when the result is within 5%;
5. include at least one semantic slow-path control and one unrelated performance control;
6. profile the candidate only after the timing result is reproducible.

Do not compare a profiling build's timing with the canonical release binary.

### 16.3 Fixed populations

Report all of:

- candidate / Result21 MIR untyped on the same 56-row population or explicit fixed subset;
- candidate / Result21 MIR typed on the same 53-row Result21 population;
- a second full typed population including any restored `cd`/`hashmap`/`raytrace3d` rows;
- candidate MIR typed / the same Result21 C2MIR cells over the fixed 43-row population;
- candidate / Result18 on the previous matched population;
- LambdaJS separately if T12-G lands.

Missing cells must never make an aggregate look faster silently.

### 16.4 Integrated Result22 targets

Tune12 is successful when the retained core phases meet all of:

| Metric | Result21 | Tune12 target |
|---|---:|---:|
| MIR typed / Result18, fixed matched rows | 1.532x | <= 1.15x |
| MIR untyped / Result18, fixed matched rows | 1.139x | <= 1.05x |
| MIR typed / C2MIR, fixed 43 rows | 15.6x | <= 9.5x |
| MIR typed / Node, Result21 population | 2.85x | <= 2.25x |
| MIR typed candidate / Result21 | 1.00x | <= 0.80x |

These are integrated goals, not permission to trade one benchmark for another. A candidate also
needs:

- no unexplained target/control regression over 5%;
- no suite geomean regression over 3%;
- exact output and exit status on every row;
- no new timeout/crash/missing timing;
- no >10% representative MIR/code-size growth without a measured corresponding speedup.

If the core phases deliver the per-track acceptance criteria but miss an integrated target, record
the measured result and re-profile. Do not weaken semantics or conceal population changes to hit a
headline.

---

## 17. Stop and rollback rules

Roll back or redesign a performance patch when any of these occurs:

1. nullable/OOB, int poison, numeric admission, COW, `var`, view, sized-lane, GC, or JS semantics
   change;
2. the patch depends on editing vendored MIR or the frozen legacy C2MIR path;
3. a raw payload pointer outlives or detaches from its rooted owner;
4. a typed boundary is skipped from carrier type, dynamic Shape identity, or mutable metadata
   rather than an exact proof;
5. a fast store bypasses uniqueness, view, or caller-visible owner replacement;
6. a target misses its per-track minimum improvement after noise-controlled A/B;
7. an untargeted control regresses by more than 5% and the regression reproduces;
8. MIR/code size grows materially without a measured runtime win;
9. a missing or crashing typed row is excluded to improve the aggregate;
10. a benchmark result requires enabled instrumentation or a debug build.

Rejected experiments stay documented with their measurement and rejection reason. Do not leave
dead branches, disabled alternate implementations, or benchmark-specific special cases in the
runtime.

---

## 18. Completion definition

Tune12 is complete only when:

- T12-P0 and T12-A through T12-D are implemented or rejected with measured evidence;
- every entered conditional track meets its own profile gate and acceptance threshold;
- the three missing Result21 MIR-typed rows are fixed or have explicit root-caused blockers;
- int `div`/`%` hot paths preserve all §4.7 and int-v5 poison behavior;
- dense typed loops use direct lane-correct loads/stores only under dominating proofs;
- admitted array capabilities preserve COW, `var` write-back, views, and precise GC;
- redundant boundaries are removed only from explicit semantic facts;
- focused, forced-GC, Lambda baseline, and Test262 gates pass;
- a clean release Result22 is captured with fixed-population Result21, Result18, and C2MIR
  comparisons;
- this document contains the final implementation ledger, retained measurements, hashes, and any
  deliberately deferred work.

The central Tune12 rule is:

> **Prove semantics once at the boundary, carry the proof explicitly, and make the hot loop look
> native only while that proof remains valid.**

That is the useful lesson from C2MIR. The objective is not C semantics or identical textual MIR;
it is Lambda semantics with C-like native work on the proved arm and a correct checked fallback
everywhere else.
