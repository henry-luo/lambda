# Tune 19: v32 Lane-Parity Audit — the Inference Lane, the Annotation Tax, and the Store/Compare Edges

- **Date:** 2026-08-18
- **Status:** PROPOSAL (analysis complete, no slice started)
- **Input:** `test/benchmark/Overall_Result32.md` / `benchmark_results_v32.json` (commit `a6192c1086`, archived binary `test/benchmark/exe/lambda-v32-a6192c1086`), paired against `benchmark_results_v31.json`; fresh finalized-MIR dumps of 13 untyped/typed pairs under `temp/r32/` taken with `LAMBDA_MIR_DUMP_PATH=… ./lambda.exe run <bench>[2].ls` on the same binary
- **Related:** `vibe/Lambda_Impl_Tune18.md` (E1–E6, landed), `vibe/Lambda_Impl_Tune17 (done).md` (T1–T5, lane unification), `vibe/Lambda_Impl_Tune16 (done).md` (categorical bar), `vibe/Lambda_Design_Compiling_Lane.md` (ValueRep), `vibe/Lambda_Design_Compiling.md` (LC1), `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (M1–M8, T-A/T-B)
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1.1–S4.1.2, S7.1, S7.2.1; `doc/Lambda_Formal_Design.md` D2.2.2–D2.2.3, D2.5.1, D2.5.3, D2.6.2–D2.6.3, D3.2.1, D3.3.1, D3.3.3, D4.6.1v2–D4.6.2v2, D4.7.1, D5.3.1, D5.3.4, D8.1.1v2, D8.3.3, D8.6.1–D8.6.3
- **ID series:** `T19-#` (round-prefixed; extends the tune-round convention without colliding with Tune14 `F#`, Tune15 `B#`, Tune16 `C#`, Tune17 `T#`, Tune18 `E#`)

## 1. Where v32 stands

| Metric | Result26 | Result30 | Result31 | **Result32** |
|---|---:|---:|---:|---:|
| MIR (untyped)/Node geo | 2.25x | 1.96x | — | **1.81x** |
| MIR (typed)/Node geo | 1.26x | 1.06x | — | **0.85x** |
| MIR (typed)/C2MIR geo (47 rows) | 6.70x | 5.70x | — | **4.39x** |

Paired v31→v32 on the same 59 rows: untyped geomean **0.951**, typed geomean **0.839**. Tune18's E-slices are real and they landed where the doc said they landed (base64 typed 46.0→17.9, fast_diff typed 412→160, quicksort typed 4.78→1.10, ray typed 0.83→0.33, sumfp 0.318→0.071 on both lanes). Nine typed rows regressed ≥10% in the same window (ack +21%, brainfuck +15%, tak +15%, triangl +14%, cpstak +13%, fib +12%, fft +11%, bounce +10%, crypto_sha1 +10%, hashmap +10%) — the E5/E6 range-fact and root-frame work bought the array/string rows and charged the scalar-recursion rows.

The typed/C2MIR distribution is now bimodal, not a smear:

| Tier (typed/C2MIR) | Rows | Reading |
|---|---:|---|
| ≤2x | 16 | the lane machinery works |
| 2–5x | 10 | residual boundary/const density |
| 5–10x | 8 | store edge + call edge |
| 10–20x | 8 | unshaped/record access, strings, generic stores |
| >20x | 5 | bounce 32.3, base64 31.7, list 28.1, hyphen 24.0, microdiff 36.6 |

**26 of 47 rows are within 5x of a native C port through the same backend.** The remaining work is 21 rows concentrated in four mechanisms, all of which are front-end representation decisions.

## 2. Direct answer: is typed still ever slower than untyped?

Yes — **9 rows**, reproducible on the archived v32 binary. This is a live violation of the Tune16/Tune17 categorical bar (*an annotation may never make a row >5% slower than the unannotated emission of the same code*).

| Row | untyped (ms) | typed (ms) | typed/untyped | typed/C2MIR |
|---|---:|---:|---:|---:|
| awfy/bounce | 0.276 | 0.805 | **2.92x** | 32.3x |
| beng/fannkuch | 0.339 | 0.774 | **2.28x** | 5.1x |
| jetstream/splay | 140.2 | 264.8 | **1.89x** | — |
| kostya/brainfuck | 315.1 | 412.2 | **1.31x** | 14.3x |
| r7rs/cpstak | 0.264 | 0.341 | 1.29x | 1.5x |
| r7rs/tak | 0.132 | 0.170 | 1.29x | 1.3x |
| r7rs/ack | 11.13 | 13.56 | 1.22x | 1.1x |
| beng/fasta | 0.794 | 0.889 | 1.12x | 3.6x |
| beng/knucleotide | 4.32 | 4.81 | 1.11x | 16.8x |

Re-timed three runs each on `lambda-v32-a6192c1086`: bounce 0.281/0.270/0.263 vs 0.805/0.727/0.767; fannkuch 0.342/0.348/0.341 vs 0.654/0.794/0.780. Not noise.

A second, quieter cohort: **14 rows where a typed source exists and buys nothing** (within ±5% of untyped) — fib, sum, sumfp, mandelbrot, deltablue, havlak, pidigits, regexredux, json_gen, collatz, array1, diviter, pnpoly, microdiff. Every one of the 59 rows has a real `<bench>2.ls` (zero `untyped_fallback` statuses in the JSON), so these are annotations the emitter is not consuming, not missing sources.

### 2.1 The three mechanisms behind the annotation tax (fresh MIR)

**T19-A — declared-type bindings re-check what the lane already proved.** `lambda_type_check` call counts, untyped → typed: fannkuch 0→7, splay 20→**55**, brainfuck 0→10, mbrot 5→12, spectralnorm 0→13. `ensure_typed_array` likewise 0→3 (fannkuch), 0→16 (spectralnorm), 0→6 (bounce). Tune18 E1 taught `emit_checked_boundary` to consume the *array-read* proof; it did not teach it to consume the *declared-binding* proof when the initializer is already lane-native, and it does not reach record/`map?` field contracts at all — which is the whole of splay's 20→55 (`type SplayNode = {key: float, left: map?, …}`: every node handoff is a runtime admission).

**T19-B — an `int[]` annotation can *demote* a read that inference kept inline.** In `bounce2.ls` the `int[]` binding emits a witness-guarded read whose fallback edge calls `item_at`; `bounce.ls` (no annotation) emits a bare `mul/add/mov i64:(…)`. Typed bounce shows `item_at_p` ×12 and `lambda_item_to_int_lane_c_p` ×42 (untyped: 0 and 16). The declared witness is not satisfied by the `fill(n, 0)` producer, so the annotated program pays a guard *and* a generic call where the inferred program paid neither [D2.6.2, D3.2.1].

**T19-C — int-lane comparison lowered through `double`.** `bounce2.mir` L210–L213 lowers `bxv[j] < 0` as: range-check operand, `i2d` (slow edge `lambda_int_lane_to_double_c`), range-check the *literal 0*, `i2d` it too, then `dlt`. Fourteen instructions and two potential calls for one `blts`. `lambda_int_lane_to_double_c` count: bounce untyped **0**, bounce typed **10**. The comparison sees the subscript's public type `int?` (D2.5.3) and falls to the numeric tower; the inferred lane sees a plain in-band int and emits `lts`. This is the same fact reaching two different lowerings — exactly what Tune17's same-facts-same-code invariant forbids.

## 3. The untyped lane is the round's biggest single lever

29 rows gain ≥1.5x from annotation alone; 8 gain ≥10x:

| Row | untyped | typed | annotation gain | mechanism |
|---|---:|---:|---:|---|
| larceny/ray | 10.25 | 0.33 | **31.0x** | float param lane |
| beng/spectralnorm | 45.89 | 1.61 | **28.4x** | float param + return lane |
| r7rs/mbrot | 11.80 | 0.57 | **20.7x** | float lane |
| kostya/primes | 65.88 | 3.44 | **19.2x** | counted loop + int lane |
| awfy/sieve | 0.547 | 0.032 | **17.1x** | counted loop |
| r7rs/fft | 2.60 | 0.26 | **10.1x** | float lane |
| awfy/richards | 2673.9 | 265.3 | **10.1x** | record shape |
| larceny/quicksort | 10.32 | 1.10 | 9.4x | int lane + array witness |

**Counterfactual: if every untyped row merely reached its own typed row, MIR (untyped)/Node geo goes 1.81x → 0.795x** — untyped Lambda becomes faster than Node overall, with **no new optimization mechanism**, only fact propagation. That is a larger win than anything else on the table and it needs no new semantics.

### 3.1 T19-D — the dynamic-range `for` loop (owns sieve 17x, primes 19x)

`sieve.ls`'s `for i in 2 to sz` with untyped `sz` emits, per the dump: `fn_to` (materializes a range object), `item_keys`, `iter_len`, then **`iter_val_at` per iteration**, with `i` a boxed Item feeding `fn_sub`, `fn_index`, `is_truthy`, `fn_le`, `fn_add`, `fn_index_assign`. `sieve2.ls` with `sz: int` emits a native counted loop (`sub/add/ge/bt`), an inline `u8:(…)` bool-array load and int-lane arithmetic — 163 vs 233 MIR lines but 17x faster. This is Result18's **T-B** ("native counted for-loop = biggest lever"), still open on the inference lane only. `sieve(flags, 5000)` has a literal at its single call site: a monomorphic-callsite fact would prove `sz: int` outright.

### 3.2 T19-E — scalar parameter lanes stop at the first unproven argument

`ray.ls` untyped raw entry: `func i64, i64, p:runtime, d:%p1, d:%p2, d:%p3, i64:%p4 … i64:%pa`. The inference lane **already specializes scalar params** — it got the three that receive float literals and gave up on the seven fed by `sx[s]` array reads, which infer `float?` under D2.5.3 and get boxed through `push_d`/`lambda_float_null_lane_c` at the call edge. `ray2.ls` annotates all ten and gets `d:` for all ten: 31x. Same story on `_eval_A` (spectralnorm): untyped returns `i64`, typed returns `d`.

The machinery to fix this exists and shipped last round: Tune18's follow-on *closed inferred ArrayNum entry* proves a raw entry's array carrier when **every non-escaped direct caller** supplies the exact witness [D3.3.1, D3.3.3, D8.3.3]. T19-E is the same analysis applied to scalar param/return lanes.

### 3.3 T19-F — the store edge needs a value-side native proof, not a static type

`transpile-mir.cpp:19744, 19891, 20054`: the indexed-store lowering picks the inline lane only when `value_native_proven` — established by a *whitelist of expression shapes* (native call, bitwise tree, proven ArrayNum producer, …). Anything else boxes and calls `fn_array_set`, which clones and revalidates. This is why `fn_array_set_p` survives in typed dumps: navier_stokes2 ×33, hashmap2 ×11, brainfuck2 ×5, fannkuch2 ×8 (fannkuch's ×8 is identical in both lanes — the annotation buys nothing on the store edge). The read edge got E1; the store edge did not.

## 4. Root cause: expression results still have no representation

T19-B, T19-C, T19-F and the E1 residue are one defect wearing four hats. `ValueRep` exists for bindings, but an *expression result* carries only a static type, and the emitter re-derives its representation per consumer via ad-hoc shape whitelists (`mir_expr_proves_native_return_lane`, `mir_direct_native_scalar_lane_proven`, `mir_native_int_bitwise_tree`, …). Each consumer that lacks a whitelist entry falls back to boxed/generic, and the *declared* path and the *inferred* path have different whitelists — which is precisely how an annotation can be slower than no annotation.

`vibe/Lambda_Design_Compiling_Lane.md` already rules the shape of the fix: the transpiler must never read back `MIR_reg_type`; extend `ValueRep` to expression results so representation is *carried*, not re-proved. That doc is the structural prerequisite for T19-1 and T19-2 below, and it retires the whitelist family rather than adding a fifth entry to it.

## 5. Tracks (ranked; each separately land-able and gate-able)

### T19-1 — ValueRep on expression results (structural; unblocks 2/3/4)
Carry `(static type, lane, nullability, in-band proof)` on every expression result through the emitter; every consumer (boundary, compare, store, call arg, array element) reads the carried rep instead of re-proving it; delete the shape whitelists as each consumer converts. No semantic change [D2.5.1, D2.5.3, S4.1.1–S4.1.2 all unchanged — only the emission consumes the proof].
**Acceptance:** the four whitelist helpers named in §4 have zero callers; emission ratchet shrinks [D8.6.1]; no row regresses >5%.

### T19-2 — Kill the annotation tax (closes the §2 ledger; highest confidence)
1. Declared-binding boundary consumes a lane-native initializer's proof — the `lambda_type_check` survives only where the initializer's rep cannot satisfy the declaration.
2. Record/`map?` field contracts: a store whose source shape is the declared shape needs no runtime admission (splay's 20→55) [D3.2.1].
3. Comparison lowering reads the carried int lane instead of falling to the numeric tower (T19-C).
4. A declared `T[]` must never emit a *worse* read than the inferred one — when the witness cannot be proven, fall back to the inferred lowering, not to `item_at` (T19-B).
**Acceptance:** all 9 rows in §2 within 5% of their untyped twin (categorical bar restored); bounce ≤0.30, fannkuch ≤0.36, splay ≤145, brainfuck ≤320 ms; `mir-check` fixtures assert zero `lambda_int_lane_to_double_c` in an int-compare body and zero `item_at` in a declared-`int[]` read body, with the unproven twins still checking.

### T19-3 — Native counted loops on the inference lane (owns sieve/primes)
`for x in a to b` lowers to a counted loop whenever both bounds carry the int lane, regardless of whether the fact came from an annotation or from inference; `to` only materializes a range object when the result escapes [S7.x range semantics unchanged].
**Acceptance:** untyped sieve ≤0.06 ms, primes ≤5 ms, mbrot ≤1.0 ms; untyped `sieve` dump contains no `fn_to`/`iter_val_at`.

### T19-4 — Closed-caller scalar lane specialization (owns ray/spectralnorm/fft)
Extend Tune18's closed-caller witness analysis from array carriers to scalar param **and return** lanes: when every non-escaped direct caller supplies a proven `int`/`float` lane, the raw entry takes `i64`/`d` natively; open/escaped callees keep the boxed entry unchanged [D3.3.1, D3.3.3, D8.3.3]. Combine with T19-1 so an `sx[s]` float-array read counts as a proven producer on the call edge.
**Acceptance:** untyped ray ≤0.6, spectralnorm ≤2.5, fft ≤0.4, navier_stokes ≤250 ms; untyped `_sphere_intersect` entry shows `d:` for all ten params.

### T19-5 — The store edge (owns navier_stokes, hashmap, fannkuch's shared floor)
Rebuild the indexed-store lowering on the T19-1 rep: inline lane store whenever the value's carried rep matches the array's element lane; `fn_array_set` only for genuinely generic values or demotion-capable carriers [D2.2.2, D2.6.2–D2.6.3].
**Acceptance:** `fn_array_set` gone from the navier_stokes2/hashmap2/fannkuch2 hot bodies; hashmap typed ≤25 ms, navier_stokes typed ≤80 ms.

### T19-6 — Record access residue (owns richards, deltablue, cd, splay)
E3 landed `fn_member_by_id` but the dumps still show `lambda_module_name_id_at` ×30 (richards2) and ×72 (deltablue2) — the per-site NameId hoist did not reach these shapes — plus `fn_map_set` ×60/×71 (generic COW-per-write) and `fn_eq` ×30/×89 on `map?` operands that D2.5.1 makes a tag test. Finish E3's hoist for these shapes; give a declared-shape map store a direct slot write [D4.6.1v2–D4.6.2v2; LC1 stands — no inline caches].
**Acceptance:** richards typed ≤120, deltablue typed ≤45, cd typed ≤120 ms; `lambda_module_name_id_at` count in the hot bodies is O(1) per function, not per site.

### T19-7 — Strings (the >20x tail: base64 31.7x, hyphen 24.0x, microdiff 36.6x)
Unchanged from Tune18 E4's disposition: the two cheap slices landed, the residue is representation. The byte/binary lane ships only behind its own design doc (Tune17 T5, S1.4–S1.6 scope). Do not spend this round here.

## 6. Round targets and non-goals

Targets: MIR (untyped)/Node **1.81x → ≤1.0x** (T19-3 + T19-4 alone are worth 1.81→~0.9 by the §3 counterfactual); MIR (typed)/Node **0.85x → ≤0.70x**; MIR (typed)/C2MIR **4.39x → ≤3.0x**; **zero rows where typed is >5% slower than untyped**; no row >10% above its v32 value at round close.

Non-goals: no inline caches [LC1, D8.4.1]; no change to int53 saturation semantics [S4.1.2]; no change to `a[i] : T?` inference [D2.5.3] — only its emission; no benchmark-source annotation edits to dodge a cost the compiler should elide (the §2 rows must be fixed in the compiler, not by deleting annotations); no vendored-dependency edits.

Gates (unchanged house rules): `make test-lambda-baseline` 100% and `make test262-baseline` fully green after each retained slice; a `mir-check` fixture for every elision edge asserting both the fast path and the surviving check on the unproven twin [D8.6.2]; forced-GC + poison sweeps for representation changes [D8.6.3]; release-build paired A/B against the archived `lambda-v32-a6192c1086` control, three runs, medians [`run_paired_benchmarks.py`].

Interaction: T19-1's rep carrying is a prerequisite the D8.1.1v2 tier-up plan will want anyway — a per-function promotion needs a representation it can hand across the tier boundary without re-proving it.
