# Tune 19: v32 Lane-Parity Audit — the Inference Lane, the Annotation Tax, and the Store/Compare Edges

- **Date:** 2026-08-18 (§1–§6 v32 analysis); 2026-08-19 (§7 Result33 re-assessment)
- **Status:** IN PROGRESS — **T19-3 DONE, T19-4 DONE** (landed in commit `8705d85c5a`, measured in Result33); T19-1, T19-2, T19-5, T19-6, T19-8 open; T19-7 promoted to design (§7.5). §7 re-ranks the remaining tracks against Result33.
- **Input:** `test/benchmark/Overall_Result32.md` / `benchmark_results_v32.json` (commit `a6192c1086`, archived binary `test/benchmark/exe/lambda-v32-a6192c1086`), paired against `benchmark_results_v31.json`; fresh finalized-MIR dumps of 13 untyped/typed pairs under `temp/r32/` taken with `LAMBDA_MIR_DUMP_PATH=… ./lambda.exe run <bench>[2].ls` on the same binary; §7 uses `test/benchmark/Overall_Result33.md` / `benchmark_results_v33.json` (commit `8705d85c5a`, archived binary `test/benchmark/exe/lambda-v33-8705d85c5a`)
- **Related:** `vibe/Lambda_Impl_Tune18.md` (E1–E6, landed), `vibe/Lambda_Impl_Tune17 (done).md` (T1–T5, lane unification), `vibe/Lambda_Impl_Tune16 (done).md` (categorical bar), `vibe/Lambda_Design_Compiling_Lane.md` (ValueRep), `vibe/Lambda_Design_Compiling.md` (LC1), `vibe/Lambda_Tune_Typed_Vs_C2MIR.md` (M1–M8, T-A/T-B)
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S4.1.1–S4.1.2, S7.1, S7.2.1; `doc/Lambda_Formal_Design.md` D2.2.2–D2.2.3, D2.5.1, D2.5.3, D2.6.2–D2.6.3, D3.2.1, D3.3.1, D3.3.3, D4.6.1v2–D4.6.2v2, D4.7.1, D5.3.1, D5.3.4, D8.1.1v2, D8.3.3, D8.6.1–D8.6.3
- **ID series:** `T19-#` (round-prefixed; extends the tune-round convention without colliding with Tune14 `F#`, Tune15 `B#`, Tune16 `C#`, Tune17 `T#`, Tune18 `E#`)

## 1. Where v32 stands

| Metric | Result26 | Result30 | Result31 | **Result32** |
|---|---:|---:|---:|---:|
| MIR (untyped)/Node geo | 2.25x | 1.96x | — | **1.81x** |
| MIR (typed)/Node geo | 1.26x | 1.06x | — | **0.85x** |
| MIR (typed)/C2MIR geo (47 rows) | 6.70x | 5.70x | — | **4.39x** |

⚠ Those C2MIR figures are on the **47-row** basis that was all that existed at the time. After
the §7.6 back-patch every session is measurable over all 59 rows, and the level is ~25% worse
throughout (Result26 8.29x, Result30 6.79x, Result32 **5.46x**). Use the §7.6 table for any
cross-round comparison; the 47-row numbers here are kept only as the record of what was quoted
at v32.

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

### T19-1 — ValueRep on expression results (structural; unblocks 2/3/4) — **OPEN, not started**
Carry `(static type, lane, nullability, in-band proof)` on every expression result through the emitter; every consumer (boundary, compare, store, call arg, array element) reads the carried rep instead of re-proving it; delete the shape whitelists as each consumer converts. No semantic change [D2.5.1, D2.5.3, S4.1.1–S4.1.2 all unchanged — only the emission consumes the proof].
**Acceptance:** the four whitelist helpers named in §4 have zero callers; emission ratchet shrinks [D8.6.1]; no row regresses >5%.

### T19-2 — Kill the annotation tax (closes the §2 ledger; highest confidence) — **OPEN** (13 rows at v33, see §7.2)
1. Declared-binding boundary consumes a lane-native initializer's proof — the `lambda_type_check` survives only where the initializer's rep cannot satisfy the declaration.
2. Record/`map?` field contracts: a store whose source shape is the declared shape needs no runtime admission (splay's 20→55) [D3.2.1].
3. Comparison lowering reads the carried int lane instead of falling to the numeric tower (T19-C).
4. A declared `T[]` must never emit a *worse* read than the inferred one — when the witness cannot be proven, fall back to the inferred lowering, not to `item_at` (T19-B).
**Acceptance:** all 9 rows in §2 within 5% of their untyped twin (categorical bar restored); bounce ≤0.30, fannkuch ≤0.36, splay ≤145, brainfuck ≤320 ms; `mir-check` fixtures assert zero `lambda_int_lane_to_double_c` in an int-compare body and zero `item_at` in a declared-`int[]` read body, with the unproven twins still checking.

### T19-3 — Native counted loops on the inference lane (owns sieve/primes) — **DONE (landed 2026-08-18)**
`for x in a to b` lowers to a counted loop whenever both bounds carry the int lane, regardless of whether the fact came from an annotation or from inference; `to` only materializes a range object when the result escapes [S7.x range semantics unchanged].
**Acceptance:** untyped sieve ≤0.06 ms, primes ≤5 ms, mbrot ≤1.0 ms; untyped `sieve` dump contains no `fn_to`/`iter_val_at`.

#### What actually blocked it — three independent defects, none of them "the loop"

The counted-loop lowering already existed. Three separate facts kept it from firing.

**(a) The gate asked the wrong oracle.** It required `get_effective_type(bound) == LMD_TYPE_INT`
— the *boxed carrier*. But `int ± int` and `int * int` compute entirely in the i64 lane and
only box at ordinary expression boundaries (`emit_int_lane_arith` returns a `LaneReg`;
`transpile_binary_out` boxes on the way out unless the consumer asked for `native_int_out`).
So the carrier oracle answers ANY for them — correctly — and every `0 to n - 1` bound was
rejected. The gate now asks `mir_native_arithmetic_operand_type` / `mir_is_native_int_tree`,
the same *lane witness* the int-arithmetic emitter already uses for its own operands, and
produces the bound with the identical `emit_int_native_lane_typed(transpile_native_int_expr(…))`
idiom. Note the carrier oracle's stale comment at the `int ± int` case ("too narrow after
53-bit overflow promotion") describes **v4**: under v5 `int` is int53-total and saturates,
it does not promote — but the ANY answer is still right, because the *default* lowering boxes.

**(b) Parameter inference never walked `for` loops.** `gather_evidence_multi` and
`find_aliases_multi` had cases for `if`/`while`/`match`/calls/indexes — and none for
`AST_NODE_FOR_EXPR`/`AST_NODE_FOR_STAM`. Every param used only inside a loop body looked
*unused*: untyped `sieve(flags, sz)` gathered evidence `0` for `flags` and only the
call-site bit for `sz`, so both stayed `any`. Added the missing walk, plus the rule that a
`to` operand is a numeric use (`to` accepts only exact integers or single codepoints, S4.8) —
which is what lets a closed `INFER_CALLSITE_INT` edge settle the param on the int lane.
`sz` now resolves to `int` and untyped sieve is fully counted.

**(c) A raw-lane consumer ignored the boxed-entry protocol.** With (b) landed, `count_in(0, null)`
started routing to the callee's *boxed* entry while the carrier oracle still reported the
callee's *native* return lane — so the array-literal builder stored the returned Item word as
if it were a lane, and `int 0` read back as `inf`. The emitter already records this
(`last_call_returned_boxed_item` + `last_call_record_node`), and two consumers already
honoured it; the boxing helper `mir_box_evaluated_node` and the int/float array-literal
element paths did not. Extracted `mir_last_call_returned_boxed()` and applied it at all of
them. **This bug pre-dates T19-3** — T19-3 only made it reachable from untyped code.

#### Semantics preserved

The int lane is TOTAL, so a saturated or null bound arrives as a sentinel parked at an i64
extreme (`INT_LANE_NULL`/`NAN`/`±INF`, all outside ±(2^53−1)); `end - start + 1` would wrap
one into a plausible trip count and iterate silently where `fn_to` raises. The counted path
now re-checks the same int53 band `fn_to` checks — **once per loop, not per iteration** — and
its cold arm reports through `fn_range_bound_error()`, extracted out of `fn_to` so the message
keeps exactly one owner, then iterates zero times (which is what the generic path already does,
since `iter_len(ItemError)` is 0). The check is skipped entirely when both bounds have a proven
static interval, so a literal `0 to 49` emits no check at all. Char ranges (`"a" to "e"`) can
never enter the counted path — their bounds are not statically `int` — and keep the generic
Range path. This band check also closes a **pre-existing hole**: the old gate admitted an
`int`-carrier bound with no band test, so `for i in 0 to saturated_int` looped instead of raising.

Regression fixture: `test/lambda/proc/type_infer_counted_range.ls`.

#### Result

Every benchmark in `awfy`/`kostya`/`larceny` that uses a range is now on the counted path
except `havlak`/`havlak2`, whose bound is `v.first` — a member read of an untyped param, i.e.
member-shape inference, not range lowering. Before: `sieve` 1 `fn_to`, `bounce2` 2 (of 3 loops,
both `0 to ball_count - 1`), and the same shape across the corpus.

Release build, median of 5, 64 range-using rows vs the archived `lambda-v32-a6192c1086` control:

| row | v32 | T19-3 | |
|---|---|---|---|
| bounce2 (typed) | 0.803 | **0.184** | **4.4x** |
| bounce (untyped) | 0.274 | **0.120** | **2.3x** |
| sieve (untyped) | 0.547 | 0.499 | 1.10x |
| fannkuch2, deriv2, gcbench2 | | | 1.06–1.07x |
| queens, base642, brainfuck2, knucleotide2, cpstak2, pnpoly, pnpoly2 | | | 1.03–1.04x |
| binarytrees2, paraffins2 | | | 1.02x |
| 46 rows | | | flat (±2%) |
| nqueens | 1.833 | 1.893 | 0.97x |
| sieve2 (typed) | 0.033 | 0.035 | 0.94x |

The two slower rows are **not attributable to this change**: `nqueens` contains no `for` and no
`to` at all, and `sieve2`'s +2 µs sits on a 33 µs total where the added work is one band check
per *loop entry* (~8 ALU ops, executed once). Both read as build-to-build code-layout variance.

#### Acceptance — corrected

The dump half is **met**: the untyped `sieve` MIR contains no `fn_to` and no `iter_val_at`.
The timing half was **mis-specified**. `untyped sieve ≤0.06 ms` assumed the range protocol was
what separated `sieve` (0.547) from `sieve2` (0.033). It is not: with the loop fully counted,
untyped sieve is 0.499 ms, still 14x its typed twin, because `flags` remains `any` — every
`flags[i - 1]` read and `flags[k - 1] = false` store stays dynamic. `fill(5000, true)` does not
publish an `ARRAY_NUM` witness on the call edge, so `infer_param_types_batched`'s container
branch cannot fire. That is **T19-4** (closed-caller container/scalar witness), not T19-3.
Restated acceptance for this track: *no `fn_to`/`iter_val_at` in any corpus range loop whose
bounds are int-producible* — met, with `havlak` the one member-shape exception. The sieve and
primes timing goals move to T19-4.

### Post-T19-3 re-measurement (2026-08-18, release, median-of-5, 64 range rows)

Ranking the remaining work against fresh numbers rather than the Result32 snapshot.

**Annotation still buys ≥1.5x on 15 rows** — this is the T19-4 surface, and the mechanism is
*uniform*. Per-helper diffs of untyped vs typed (`temp/t19/mech/*.mir`):

| row | untyped | typed | gain | abs gap (ms) |
|---|---|---|---|---|
| spectralnorm | 44.849 | 1.608 | 27.9x | 43.2 |
| primes | 66.902 | 3.449 | 19.4x | 63.5 |
| sieve | 0.499 | 0.035 | 14.3x | 0.5 |
| richards | 2689.05 | 266.76 | 10.1x | **2422.3** |
| paraffins | 1.933 | 0.288 | 6.7x | 1.6 |
| cd | 852.3 | 243.0 | 3.5x | **609.3** |
| json 3.5x, base64 2.7x, triangl 2.3x (288 ms), binarytrees 2.0x, nqueens/queens 1.7x, nbody/storage/gcbench 1.5x (68 ms) |

In every one, the untyped body falls to the **generic numeric tower** — `fn_add`, `fn_sub`,
`fn_mul`, `fn_div`, `fn_lt`, `fn_gt`, `fn_le`, `fn_ge`, `fn_index`, `fn_index_assign`,
`item_at`, `is_truthy` — and the typed body has **zero** of them, carrying only a handful of
one-time `ensure_typed_array` admissions. One missing container carrier costs the whole body.

**Where the container witness actually dies — verified, not inferred.** Annotating only the
*caller's local* (`var flags: bool[] = fill(5000, true)`, params left untyped) does **not**
recover untyped sieve — 1.01 ms vs 1.01 ms unannotated, against sieve2's 0.046 (same debug
build, relative A/B). Probing `infer_param_types_batched`'s container branch for `flags`:

    used_as_container=1  conflict=0  container_store_type=BOOL      <- consumer READY
    specialization_types[0]=LMD_TYPE_ERROR (never recorded)         <- producer MISSING
    specialization_elem_types[0]=ANY

The consumer side is already satisfied — `used_as_container` now works because T19-3 taught
`gather_evidence_multi` to walk `for` bodies. The producer is the gap:
`mir_callsite_arg_elem_type` handles a literal array, a static `T[]` type, a direct
`fill(n, v)` call, and an identifier that names **a parameter of the enclosing function** —
but has no case for an ordinary **local binding**, which is what `var flags = fill(…)` then
`sieve(flags, …)` is. So no witness is published and the callee's param stays boxed.
**T19-4's container half may therefore be much smaller than a general closed-caller analysis:
teach the recorder to resolve a local binding's element type.** (`sz` in the same call records
fine as INT, so the machinery around it works.)

**The annotation tax (T19-2) is still live on 5 rows** — typed SLOWER than untyped:

| row | untyped | typed | |
|---|---|---|---|
| fannkuch | 0.339 | 0.728 | typed 2.15x slower |
| bounce | 0.120 | 0.184 | typed 1.53x slower |
| brainfuck | 318.2 | 405.0 | typed 1.27x slower |
| cpstak | 0.253 | 0.320 | typed 1.26x slower |
| knucleotide | 4.428 | 4.866 | typed 1.10x slower |

`bounce2` is fully diagnosed by its helper diff: the declared version has `item_at` **×12
where untyped has 0** — the annotation *demotes* a read that inference kept inline — plus
`lambda_int_lane_to_double_c` ×8 (int compares still lowering through `double`),
`lambda_item_to_int_lane_c` 4→16, `lambda_type_check` ×6 and `lambda_array_set_checked_lane`
×14. `fannkuch2` is cleaner and more damning: its ONLY differences from untyped are
`lambda_type_check` ×7, `lambda_int_lane_to_double_c` ×2, `ensure_typed_array` ×3 — and that
alone costs 2.15x.

**9 rows where annotation buys nothing at all** (0.95–1.05x): revcomp, pidigits, regexredux,
pnpoly, sumfp, sum, havlak, deltablue, json_gen. Worth an audit of what the typed source fails
to express, but lower yield than the two tracks above.

**T19-6 residue confirmed** — untyped `richards` (2.7 s, the largest absolute gap in the whole
corpus) shows `fn_map_set` ×60, `fn_member_by_id` ×49, `lambda_type_check` ×52,
`lambda_module_name_id_at` ×30, `int2it_lane` ×102.

**Suggested order:** T19-4's local-binding witness first (small, unblocks the 15-row surface,
consumer already built) → T19-2's `item_at` demotion + `lambda_type_check` elision (fixes the
categorical-bar violation, and `fannkuch2` isolates it cleanly) → T19-6 records (owns the
biggest absolute number) → T19-5 store edge.

### T19-4 — Closed-caller specialization (owns sieve/primes/spectralnorm) — **DONE (landed 2026-08-18)**

Four changes, two of them fixes to *pre-existing* oracle/emitter disagreements that T19-3
merely made reachable.

**(a) The call-site witness had no case for a local binding.** `mir_callsite_arg_elem_type`
resolved a literal array, a static `T[]`, a direct `fill(n, v)` and an identifier naming a
parameter of the ENCLOSING function — but not `var flags = fill(5000, true)` followed by
`sieve(flags, sz)`, which is how an array normally reaches a call. Verified by probe: the
consumer side was already satisfied (`used_as_container=1`, `container_store_type=BOOL`,
courtesy of T19-3's `for`-walk) while `specialization_types[0]` sat at its never-recorded
sentinel. Added local-binding resolution (declared array type first, else recurse into the
initializer, depth-capped). A mutable binding rebound to a different shape is a *speed*
question, not a soundness one — the witness only picks which native shape to generate, and
both the callee's body evidence and the call site's carrier check must still agree or the
call routes to the `_b` entry.

**(b) Unannotated locals never reached the int lane.** The declaration gate required the
initializer's *boxed carrier* to already read `int`, so `var k = i + i` bound boxed and
dragged its whole loop into the generic tower. A proven native int tree IS the lane. This is
not a new kind of binding — `var b = 3` already binds natively and the assignment cascade
already widens it when a later value does not fit (verified: `var b = 3; b = 1.5` → `1.5`,
identical on a pre-change binary).

⚠ The decision must have **one owner**. Writing it only at the declaration site broke
`(let a = 100, let b = a + 1, b)`: the block published a raw lane while the carrier oracle's
own block-tail resolution still reported the default boxed carrier, and the module read `101`
as an Item tag. Boxing the block instead broke the opposite direction (`(let t = ints[1], t) + 1`
→ `inf`, caught by `type_infer_carrier_lanes`). Both are the same mistake — the answer asked
twice, spelled differently. Extracted `mir_unannotated_native_int_decl()` and call it from
both the declaration lowering and the oracle.

**(c) The carrier oracle claimed `int / int` and `int % int` were raw lanes.** They are not:
`transpile_binary_out` returns the `LaneReg` only when the consumer passes `native_int_out`,
and boxes otherwise — exactly as for ADD/SUB/MUL, which already reported ANY. This stayed
hidden while both operands were rarely proven int at once; once loop induction variables
reached the int lane, chart's `ci = i % n_cols` published a boxed Item through an int-typed
binding and every read saturated to `inf`. div/mod now joins the ADD/SUB/MUL rule. Consumers
that reopen the lane are unaffected: they ask the LANE WITNESS
(`mir_native_arithmetic_operand_type`), which still reports INT for the whole tree.

**(d) `INFER_FLOAT_CONTEXT` vetoed closed INT call edges.** A closed edge is a static fact
about *that parameter*; FLOAT_CONTEXT only says the body mentions a float literal somewhere.
`eval_A(i, j)` stayed boxed because an unrelated `1.0 /` sits in the same body. Dropping the
veto outright was measured and **rejected** — spectralnorm 1.87x but brainfuck2 +13%,
gcbench2 +11%, deriv2 +7%, net geomean only 0.5%. The shipped rule keeps the veto unless the
parameter's OWN arithmetic uses are int-flavoured (`INFER_ARITH_USE` present, `INFER_FLOAT`
absent): same spectralnorm win, brainfuck2 regression gone, geomean 0.9% better on its own.

#### Result (release, median-of-5, 61 range rows, vs the post-T19-3 build)

| row | T19-3 | T19-4 | |
|---|---|---|---|
| sieve | 0.499 | **0.032** | **15.6x** |
| paraffins | 1.933 | 0.527 | 3.67x |
| primes (kostya / larceny) | 66.9 / 67.1 | 19.0 / 19.0 | 3.5x |
| base64 | 47.76 | 17.07 | 2.80x |
| spectralnorm | 44.85 | 22.09 | 2.03x |
| nqueens 1.18x, pnpoly 1.14x, brainfuck2 1.12x, revcomp 1.10x, gcbench2/json2/cd2/cd2_orig 1.05–1.06x, richards/richards2/json/gcbench/deriv2 1.03–1.04x |
| triangl / pidigits / cpstak2 | | | 1.03–1.04x slower |
| 39 rows flat | | | **geomean 0.859** |

Untyped `sieve` now **beats** its typed twin (0.032 vs 0.037) and `base64` is at parity. The
untyped-vs-typed gap closed from 14.3x → 0.89x (sieve), 19.4x → 5.5x (primes), 2.7x → 0.99x
(base64), 27.9x → 13.7x (spectralnorm). Baseline 3808/3808; mir-emission 59/59, ratchet 16/16,
js-mir-emission 21/21; test262 0 failures / 0 regressions (one test needed the harness's own
Phase-4 batch-kill retry). Fixture: `test/lambda/proc/type_infer_native_locals.ls`, whose
values were checked to be **identical on a binary predating both T19-3 and T19-4** (SI3v2).

#### The companion-lane bug — **FIXED**

`mir: pending companion reached function epilogue in _try_error_prefix_match_…` was a real
latent crash, not a symptom of the inference work. Two independent causes:

**(a) Root cause — a declaration initializer inherited the caller's tail flag.**
`transpile_let_stam` never cleared `mt->in_tail_position`, so `(let text = concat_strings(…),
if …)` appearing in tail position lowered its *initializer* as if it were the function's tail
expression. The direct-call emitter's tail-forward path then handed the callee's shape-2 pair
to the return epilogue for a value that never reaches a return, and `finish_function_epilogue`
aborted on the D5.2.1v3 one-live-pair assertion. A binding is a side effect of the enclosing
expression, not its result — `transpile_binary_out` already clears the same flag for operands,
and `transpile_let_stam` now does too.

**(b) A structural hole in the same invariant.** `transpile_if` materialized a pending pair for
the *condition* but not for the *arms*, and the arms are `MIR_MOV`ed into a shared join
register — which erases the register identity `mir_materialize_pending_reg` matches on
(`pending_live_item == value`). A pair reaching a join therefore survives silently to the same
abort. No corpus script reaches it (the leak above was the initializer), but the epilogue
asserts the invariant, so both arms now resolve at the producer. It is a no-op when no pair is
live.

Verified: baseline 3808/3808, mir-emission 59/59, ratchet 16/16, js-mir 21/21, test262
40261/40261 with **zero** batch-kill retries.

#### The transitive scalar edge — BUILT, MEASURED, **NOT SHIPPED**

It works. It is not worth it. All three pieces were implemented and measured on release builds.

1. **Forwarding as a real use** (`INFER_FORWARDED_ARG`) closes the chain: `mul_AtAv.n`
   resolves int, and the driver's fixpoint carries the witness to `mul_Av`/`mul_Atv` in the
   next round. spectralnorm 22.09 → 17.69 (**1.25x**).
2. **But it costs far more than it gains.** A param merely passed along gains nothing from a
   native lane and pays an unbox at entry plus a re-box at the forward: **geomean 1.02** over
   63 rows, with gcbench +31%, nqueens +29%, binarytrees +26%, cd2 +9%. The existing comment
   ("a param merely passed along stays boxed") was right on performance grounds.
3. **And the soundness prerequisite costs more still.** Relaxing the resolve-time guards to
   let the chain through exposes that `INFER_CALLSITE_INT` is not the proof its name claims:
   `mir_callsite_join_specialization_type` SKIPS an argument it cannot type instead of joining
   it to ANY, so it means "every call site whose argument was a statically known scalar passed
   an int". cd's `rbt_put(tree, key, value)` takes `1`, an array AND a map, and records INT —
   with the guards relaxed, cd2_orig miscompiles (`type check at declaration 'wc2' failed:
   expected int, got null`). Making the join honest fixes cd and is the correct rule, but it
   costs **geomean 1.038** on its own (gcbench +31%, nqueens +30%, binarytrees +28%), because
   `ANY` there conflates "genuinely dynamic argument" with "this round cannot type it yet" and
   poisons the second case too.

**Disposition:** shipped the companion-lane fix only; the tree is otherwise at landed T19-4
(re-verified against the archived v32 control back-to-back: sieve 16.4x, primes 3.47x,
spectralnorm 2.01x, bounce 2.31x). ⚠ comments now mark both the skip in
`mir_callsite_join_specialization_type` and the forwarding decision in
`resolve_inferred_type`, each carrying its measured cost. **The prerequisite for revisiting
this is separating "unknown yet" from "genuinely dynamic" in the call-site join** — flipping
the branch is not it. Until then the resolve-time guards must not be relaxed: that combination
miscompiles cd.

⚠ Benchmark note: an early sweep of this work showed a uniform 3–18% slowdown across all 63
rows *including* rows the change cannot touch (`sum`, `sumfp`, `pidigits2`, `regexredux`).
That was machine state after hours of continuous builds, not code. Always re-check against an
archived control binary run back-to-back before believing a broad regression.

### T19-8 — Closed-caller scalar *float* lane specialization (owns untyped ray/spectralnorm/fft) — **OPEN**
(Originally drafted as a second T19-4 section; renumbered to keep one ID per track. The landed
T19-4 covers container witnesses, unannotated int locals, and closed INT call edges; this track
is the *float* param **and return** lane extension.)
Extend Tune18's closed-caller witness analysis from array carriers to scalar float param and return lanes: when every non-escaped direct caller supplies a proven `int`/`float` lane, the raw entry takes `i64`/`d` natively; open/escaped callees keep the boxed entry unchanged [D3.3.1, D3.3.3, D8.3.3]. Combine with T19-1 so an `sx[s]` float-array read counts as a proven producer on the call edge. Still live at v33: untyped ray 8.94 vs typed 0.30 (30x), untyped spectralnorm 21.9 vs typed 1.62 (13.6x).
**Acceptance:** untyped ray ≤0.6, spectralnorm ≤2.5, fft ≤0.4, navier_stokes ≤250 ms; untyped `_sphere_intersect` entry shows `d:` for all ten params.

### T19-5 — The store edge (owns navier_stokes, hashmap, fannkuch's shared floor) — **OPEN**
Rebuild the indexed-store lowering on the T19-1 rep: inline lane store whenever the value's carried rep matches the array's element lane; `fn_array_set` only for genuinely generic values or demotion-capable carriers [D2.2.2, D2.6.2–D2.6.3].
**Acceptance:** `fn_array_set` gone from the navier_stokes2/hashmap2/fannkuch2 hot bodies; hashmap typed ≤25 ms, navier_stokes typed ≤80 ms.

### T19-6 — Record access residue (owns richards, deltablue, cd, splay) — **OPEN**
E3 landed `fn_member_by_id` but the dumps still show `lambda_module_name_id_at` ×30 (richards2) and ×72 (deltablue2) — the per-site NameId hoist did not reach these shapes — plus `fn_map_set` ×60/×71 (generic COW-per-write) and `fn_eq` ×30/×89 on `map?` operands that D2.5.1 makes a tag test. Finish E3's hoist for these shapes; give a declared-shape map store a direct slot write [D4.6.1v2–D4.6.2v2; LC1 stands — no inline caches].
**Acceptance:** richards typed ≤120, deltablue typed ≤45, cd typed ≤120 ms; `lambda_module_name_id_at` count in the hot bodies is O(1) per function, not per site.

### T19-7 — Strings (the >20x tail: base64 31.7x, hyphen 24.0x, microdiff 36.6x) — **PROMOTED to design (§7.5)**
Original disposition (v32): unchanged from Tune18 E4 — the two cheap slices landed, the residue is representation. The byte/binary lane ships only behind its own design doc (Tune17 T5, S1.4–S1.6 scope). Do not spend this round here.
§7.5 revises this: at v33 the string rows are the single biggest remaining geomean lever vs C2MIR, so the design doc should be drafted now, in parallel with T19-1/2/5 — implementation still gated on that doc.

## 6. Round targets and non-goals

Targets: MIR (untyped)/Node **1.81x → ≤1.0x** (T19-3 + T19-4 alone are worth 1.81→~0.9 by the §3 counterfactual); MIR (typed)/Node **0.85x → ≤0.70x**; MIR (typed)/C2MIR **4.39x → ≤3.0x**; **zero rows where typed is >5% slower than untyped**; no row >10% above its v32 value at round close.

Non-goals: no inline caches [LC1, D8.4.1]; no change to int53 saturation semantics [S4.1.2]; no change to `a[i] : T?` inference [D2.5.3] — only its emission; no benchmark-source annotation edits to dodge a cost the compiler should elide (the §2 rows must be fixed in the compiler, not by deleting annotations); no vendored-dependency edits.

Gates (unchanged house rules): `make test-lambda-baseline` 100% and `make test262-baseline` fully green after each retained slice; a `mir-check` fixture for every elision edge asserting both the fast path and the surviving check on the unproven twin [D8.6.2]; forced-GC + poison sweeps for representation changes [D8.6.3]; release-build paired A/B against the archived `lambda-v32-a6192c1086` control, three runs, medians [`run_paired_benchmarks.py`].

Interaction: T19-1's rep carrying is a prerequisite the D8.1.1v2 tier-up plan will want anyway — a per-function promotion needs a representation it can hand across the tier boundary without re-proving it.

## 7. Result33 re-assessment (2026-08-19) — after T19-3 + T19-4

Input: `Overall_Result33.md` / `benchmark_results_v33.json`, commit `8705d85c5a`, archived
binary `test/benchmark/exe/lambda-v33-8705d85c5a`. The v32→v33 window is exactly the landed
T19-3 + T19-4, and it bought what §3 predicted — mostly the untyped lane:

| Metric | Result32 | **Result33** |
|---|---:|---:|
| MIR (untyped)/Node geo | 1.81x | **1.41x** |
| MIR (typed)/Node geo | 0.85x | **0.83x** |
| MIR (typed)/C2MIR geo (47 rows) | 4.39x | **4.30x** |

### 7.1 The typed/C2MIR tail is four mechanisms, and the good half proves the ceiling

18 rows sit ≤2x of C2MIR; three are at or past it (divrec 0.4x, sumfp 0.9x, ack/fibfp/
regexredux/mbrot ≈1.1x, fib/tak/cpstak/mandelbrot ≈1.3x). Scalar int/float lanes, the call
boundary, and counted loops are effectively done. The remaining 4.3x is concentrated:

| Mechanism | Rows (typed/C2MIR) | Owning track |
|---|---|---|
| Strings/bytes — no byte lane | microdiff 57x, base64 30.5x, hyphen 24.7x, knucleotide 17x, json_gen 14.3x, fast_diff 12x, levenshtein 7.9x | §7.5 (was T19-7) |
| Indexed store edge | hashmap 18.6x, nbody 13.4x, navier_stokes (typed 179 ms, 12.6x Node), fannkuch 5.2x | T19-5 |
| Record/map access | brainfuck 14x; richards 257 ms, splay 257 ms, cd 229 ms, deltablue 88 ms (largest absolute typed times; no C ports at v33) | T19-6 |
| Annotation tax | 13 rows, §7.2 | T19-2 (on T19-1) |

### 7.2 The annotation tax GREW: 13 rows at v33 (was 9 at v32)

Because T19-3/4 sped up the untyped twins, typed is now slower than untyped on 13 rows —
still a live violation of the Tune16/17 categorical bar: splay **1.87x**, fannkuch **2.31x**,
bounce 1.59x, brainfuck 1.28x, tak/cpstak 1.27x, navier_stokes 1.26x, ack 1.23x, fft 1.19x,
fasta 1.16x, pnpoly 1.13x, sieve 1.12x, knucleotide 1.11x. The mechanisms are unchanged from
§2.1 (T19-A declared-binding re-checks, T19-B `item_at` demotion, T19-C int-compare through
`double`); fannkuch remains the clean isolate. The §2 ledger's row set is superseded by this
one; the fix list in T19-2 is unchanged.

### 7.3 What each remaining track is worth (counterfactuals on the v33 data)

| Scenario (cumulative) | typed/C2MIR geo | typed/Node geo |
|---|---:|---:|
| Result33 baseline | 4.30x | 0.83x |
| + T19-2 tax kill (typed := min(typed, untyped)) | 4.04x | 0.78x |
| + T19-5 store edge (navier ≤80, hashmap ≤25, nbody ≈5, fannkuch ≈0.35) | 3.86x | 0.74x |
| + T19-6 records (richards ≤120, deltablue ≤45, cd ≤120, splay ≤140) | 3.86x* | 0.72x |
| + byte/string lane to 3x of C2MIR on the 8 string rows | **2.80x** | **0.55x** |
| + brainfuck to 3x (falls out of tax + records + stores) | 2.72x | 0.54x |

\* Records did not move the C2MIR geo at v33 because none of those rows had a C port — a
measurement gap, closed by §7.6.

### 7.4 Re-ranked plan

1. **T19-1** — structural prerequisite, unchanged. Do first.
2. **T19-2** — highest confidence, fully diagnosed, restores the categorical bar on all 13 rows.
3. **T19-5** — owns the worst typed/Node row (navier_stokes 12.6x) and retires the
   Result22 clone-per-store quadratic as a side effect.
4. **T19-6** — biggest absolute typed times (richards/splay/cd/deltablue/brainfuck).
5. **T19-8** — closed-caller float lanes, after T19-1 (an `sx[s]` read must count as a proven
   producer on the call edge for it to fire).

### 7.5 Strings: promote T19-7 from "deferred" to "design now"

The byte/string lane is the single biggest remaining geomean lever vs C2MIR (4.0 → 2.8 by
itself; 6 of the 12 widest v33 gaps are string rows). Deferring it was right while cheaper
wins existed; after T19-2/5 it is the frontier. This round's deliverable is the **design
doc** (Tune17 T5 scope, S1.4–S1.6): element-width-aware string/byte storage so `base64`-style
code does indexed byte loads/stores instead of boxed codepoint round-trips. Implementation
stays gated on that doc.

### 7.6 Measurement: close the C2MIR port gap — **DONE (2026-08-19)**

12 of 59 rows had no C2MIR port at v33 — including every macro row that dominates the
absolute gap (richards, deltablue, cd, havlak, json, splay, navier_stokes, raytrace3d,
cube3d, crypto_sha1, pidigits, deriv), so the typed/C2MIR geo could not see the rows that
matter most for T19-6. Native C ports for all 12 were added under
`test/benchmark/<suite>/c2mir/`, registered in `run_c2mir_benchmarks.py` (61/61 pass),
verified against the same expected results as their `.ls` twins, and the 12 cells were
patched into `benchmark_results_v33.json` (median-of-3, 2026-08-19, environment calibrated
within ±5% of the published hashmap/ack cells; provenance recorded in the report header).

**The full-coverage ceiling is worse than the 47-row one: typed/C2MIR geo = 5.34x over all
59 rows** (was 4.30x over 47). The previously invisible macro rows land at the top of the
gap table — deltablue **75.7x** (88.4 vs 1.17 ms), havlak 26.9x, raytrace3d 24.8x, cube3d
20.9x, splay (typed 256.6 vs 20.4 ms), richards 8.2x, cd 14.7x — confirming T19-6's records
mechanism as a first-class gap, not just an absolute-time concern. On this basis the §7.3
ladder becomes: baseline 5.34x/0.83x → tax kill 5.00x/0.78x → +store edge 4.77x/0.74x →
+records(§7.3 targets) 4.61x/0.72x → +strings@3x 3.57x/0.55x; driving every macro row to 3x
of its C cell reaches **2.91x/0.45x**.

**Back-patched across every prior session (2026-08-19).** The same 12 rows were missing in
Result18/21/22/23/25–32. Because C2MIR measures native C ports through `lambda/mir/c2m` and
does not depend on the Lambda binary — and its cells are stable to a few percent across all
13 sessions (hashmap 2.73–3.65, ack 11.68–12.44, gcbench 69.9–75.5 ms) — the one 2026-08-19
median set legitimately fills the gap in each, and every patched file records that provenance
in `_metadata.merged_engines`. The full-coverage trend is therefore now apples-to-apples:

| | R18 | R21 | R22 | R23 | R25 | R26 | R27 | R28 | R29 | R30 | R31 | R32 | **R33** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| typed/C2MIR (all rows) | 10.78x | 16.22x | 13.01x | 11.19x | 8.73x | 8.29x | 8.26x | 6.87x | 7.13x | 6.79x | 6.66x | 5.46x | **5.34x** |
| typed/Node | 2.10x | 2.85x | 2.27x | 1.91x | 1.32x | 1.26x | 1.26x | 1.07x | 0.99x | 1.06x | 1.04x | 0.85x | **0.83x** |

The shape of the trend is unchanged — steady improvement, with the Tune17/18 rounds (R28→R32)
doing the heavy lifting — but every level is ~25% worse than the 47-row basis reported at the
time. Reports whose C2MIR column was never present (Result19/20/24) were left alone: those
sessions recorded no C2MIR at all, and filling them would fabricate a session rather than
complete one. `Overall_Result.md` (2026-02-21) is also out of scope — its "C2MIR" is the
retired `lambda --c2mir` transpiler measured by whole-process wall clock, a different quantity.

Caveat recorded in `C2MIR_COVERAGE.md`: `crypto_sha1.ls` currently computes a WRONG digest
on the Lambda engine (all-ones saturation; the `.ls`-asserted digest was independently
verified against hashlib, and the C port asserts it) — the MIR cells for that row time a
wrong computation until the engine's bitwise defect is fixed.

### 7.7 Revised round targets

Unchanged from §6 in kind, restated against v33 on the §7.6 full-coverage basis:
typed/C2MIR **5.34x → ≤3.6x** (the CF4 ladder point; ≤2.9x if T19-6 reaches 3x-of-C on the
macro rows), typed/Node **0.83x → ≤0.55x**, **zero rows where an annotation costs >5%**, no
row >10% above its v33 value at round close. Non-goals and gates of §6 stand.
