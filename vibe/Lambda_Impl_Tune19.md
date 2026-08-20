# Tune 19: v32 Lane-Parity Audit — the Inference Lane, the Annotation Tax, and the Store/Compare Edges

- **Date:** 2026-08-18 (§1–§6 v32 analysis); 2026-08-19 (§7 Result33 re-assessment)
- **Status:** IN PROGRESS — see **§12 for the current standing summary**.
  **T19-3, T19-4 DONE** (commit `8705d85c5a`, measured in Result33);
  **T19-2 items 1/3/4 DONE** (2026-08-19); **T19-5 REFUTED** by profiling (§7.9);
  **T19-6 partially done** — identifier compare landed, O(1) shape index tried and
  reverted; **recursive record contracts now work at native speed** (§11.5,
  2026-08-20). T19-1, T19-8, T19-2 item 2 open; T19-7 promoted to design (§7.5).
  §7 re-ranks tracks against Result33; **§8 is the negative-results ledger — read
  it before re-attempting anything, it is a list of decisions, not of to-dos**;
  §9–§11 are the type-boundary investigation, whose rulings now live in
  `vibe/Lambda_Design_Compiling_Lane.md` §10 and
  `vibe/Lambda_Design_Type_Boundary.md`.
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

### T19-2 — Kill the annotation tax — **PARTIALLY LANDED (2026-08-19): items 1, 3, 4 done; item 2 open**
1. Declared-binding boundary consumes a lane-native initializer's proof — the `lambda_type_check` survives only where the initializer's rep cannot satisfy the declaration. **DONE**
2. Record/`map?` field contracts: a store whose source shape is the declared shape needs no runtime admission (splay's 20→55) [D3.2.1]. **OPEN — owns the residual tax on splay (1.83x) and is shared with T19-6.**
3. Comparison lowering reads the carried int lane instead of falling to the numeric tower (T19-C). **DONE**
4. A declared `T[]` must never emit a *worse* read than the inferred one — when the witness cannot be proven, fall back to the inferred lowering, not to `item_at` (T19-B). **DONE**
**Acceptance:** all 9 rows in §2 within 5% of their untyped twin (categorical bar restored); bounce ≤0.30, fannkuch ≤0.36, splay ≤145, brainfuck ≤320 ms; `mir-check` fixtures assert both elision edges with the unproven twins still checking. **Fixtures met; the bar is NOT yet restored — see the ledger below.**

#### What actually blocked it — three defects, none of them at the site §2.1 named

The mechanism names (T19-A/B/C) survive. Every *site* was one level away.

**(a) T19-A is the ASSIGNMENT boundary, not the declaration.** The declaration
site already had `emit_native_scalar_declaration_boundary` — an inline
`INT_LANE_NULL` test whose *cold arm alone* calls `lambda_type_check`. Rebinding
had no equivalent, so `k = perm[0]` on a declared `int` BOXED the read's lane and
called `lambda_type_check` on the straight-line path, once per iteration, purely
to reject a null arm the declaration site rejects in two instructions. Same fact,
two lowerings: `var k: int = perm[0]` was cheap and `k = perm[0]` was not. The
boundary decision is now `assignment_boundary_applies`, and the proven scalar
case routes to the same fast boundary.

⚠ The added `val_tid == contract->type_id` guard is load-bearing. The fast
boundary compares the REGISTER against the lane sentinel, so it must only ever
see a register that genuinely holds that lane. The boxed path's
`val_tid = LMD_TYPE_ANY` is what tells the widening below that the value became
an Item; the fast path must NOT set it, because the register is still the raw
lane.

**(b) T19-B is `int()`'s registry row, not the array read.** A declared `int[]`
read emitted a three-branch element-header probe plus an `item_at` fallback on
every access, where the inferred read emitted a bare indexed load. The witness
died in `has_elem_type_invalidation` → `mir_store_may_change_elem_type`, which
reads the STORED VALUE's declared type — and `int()`'s `success_type` in
`sys_func_registry.c` is the looser `&TYPE_NUMBER` (`fn_int` is generic over its
input), not `&TYPE_INT`. So `bx[i] = int(random_next(seed) % 500)` looked
representation-changing and guarded every later read of `bx`. It is not: the
success arm is always the int lane, and the error arm cannot retag either,
because a typed store REJECTS an `ItemError` rather than writing it. Verified
directly, and the contrast is the proof —
`var a: int[] = fill(4,0); a[1] = int("abc")` fails the script, while the same
store into an UNANNOTATED array writes `error` into the array and retags it.
bounce2: header probes 22 → 0, `item_at` 12 → 0.

**(c) T19-C was a missing block, and the literal was paying too.** An ordered
comparison whose operands are both int lanes fell to the generic float tower
whenever either side admitted a sentinel — which a declared `int[]` read always
does (`int?`, D2.5.3). That tower widens through `emit_int_lane_to_double`: a
band test, an `i2d` and a cold call PER OPERAND, *including for the literal `0`*
whose value the compiler already knows. For two in-band lanes the widening is
exact, so the integer compare is the identical predicate; only the sentinel arm
needs the float lowering, and it is kept verbatim — that is what makes a null
compare false (S7.10.3) and a saturated bound order as ±inf (S4.1.2).
`mir_emit_int_lane_pair_in_band` emits the band test only for an operand with no
proven static interval, so a literal bound now costs nothing.

⚠ EQ/NE deliberately stay on the float lowering. `null == null` and nan-vs-nan
disagree, so equality is NOT the same predicate under the widening the way the
ordered relations are. Restricting the new block to LT/LE/GT/GE is what makes it
provably behaviour-preserving rather than a semantics change.

#### Result (release, paired A/B vs the pre-change control, 5 alternating pairs, 112 rows)

Control: `test/benchmark/exe/lambda-t19-control` (HEAD `f46aae989`); candidate:
`test/benchmark/exe/lambda-t19-2`. **All 112 rows produced byte-identical stdout.**

| | geomean (candidate/control) |
|---|---:|
| all 112 rows | **0.9879** |
| typed (56) | **0.9811** |
| untyped (56) | 0.9948 |

| row | ratio | pairs won |
|---|---:|---|
| navier_stokes typed | **0.662** | 5/5 |
| fannkuch typed | **0.716** | 5/5 |
| bounce typed | 0.897 | 4/5 |
| base64 untyped | 0.905 | 4/5 |
| gcbench typed | 0.919 | 2/5 |

⚠ Every apparent regression at 5 pairs dissolved at 15 pairs: diviter untyped
1.108 → **0.960**, cpstak typed 1.068 → 1.004, revcomp untyped 1.072 → 1.013,
list typed 1.080 → 1.028. `sumfp untyped` swung 0.896 → 1.132 across the two
runs — a float-only row these changes cannot touch. Only `triangl untyped`
(1.035 at 15 pairs) stays mildly slow. This reproduces the ⚠ note under T19-4:
short rows on a loaded host produce ±10% noise in both directions, and no
single-run number should be believed.

#### The ledger is NOT closed — the categorical bar still fails on 11 of 13 rows

typed/untyped on the same binary, before → after:

| row | before | after | | row | before | after |
|---|---:|---:|---|---|---:|---:|
| navier_stokes | 1.21 | **0.82** ✓ | | tak | 1.30 | 1.28 |
| fasta | 1.05 | **1.03** ✓ | | cpstak | 1.27 | 1.35 |
| fannkuch | 1.69 | **1.17** | | ack | 1.12 | 1.13 |
| bounce | 1.57 | 1.45 | | fft | 2.06 | 2.02 |
| splay | 1.98 | 1.83 | | pnpoly | 1.11 | 1.18 |
| brainfuck | 1.09 | 1.07 | | knucleotide | 1.19 | 1.13 |

Two rows cross the bar; fannkuch moves most of the way. What remains splits into
two mechanisms that this slice never claimed:

- **splay (1.83)** is item 2 — record/`map?` field contracts. Untouched here, and
  shared with T19-6.
- **fft 2.02, cpstak 1.35, tak 1.28, ack 1.13** are scalar-recursion rows with no
  array read and no int-lane compare in their hot bodies. Their tax is the call
  boundary, i.e. the E5/E6 root-frame cost §1 already charged to those rows —
  not a boundary or compare defect. **Diagnosing that is new work, and it should
  get its own track rather than being folded into T19-2.**

#### Fixtures

`test/mir/lambda/tune19_int_lane_compare.{ls,mir-check,txt}` — the compare body
must contain BOTH a native integer compare and exactly one double compare (the
sentinel arm), with `item_at` at zero.

`test/mir/lambda/tune19_declared_scalar_rebind.{ls,mir-check,txt}` — the rebind's
sentinel test must be immediately followed by a branch, with the unproven twin
still crossing the boxed boundary. ⚠ The assertion has to be ADJACENCY: a call
count cannot discriminate, because both lowerings call `lambda_type_check`
exactly once — the whole difference is whether that call sits on the
straight-line path or on the null arm.

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

### T19-5 — The store edge — **REFUTED by profiling 2026-08-19; do not implement (§7.9)**
⚠ Ranked from static `fn_array_set` counts in the dumps. Sampled, its three nominated rows spend 0% (fannkuch2), 3.2% (navier_stokes2) and 0% (hashmap2) in that helper; hashmap2's real cost is the generic READ chain and navier_stokes2's hot loop already emits a bare inline `dmov` store. The acceptance below is unreachable through the store edge.
Rebuild the indexed-store lowering on the T19-1 rep: inline lane store whenever the value's carried rep matches the array's element lane; `fn_array_set` only for genuinely generic values or demotion-capable carriers [D2.2.2, D2.6.2–D2.6.3].
**Acceptance:** `fn_array_set` gone from the navier_stokes2/hashmap2/fannkuch2 hot bodies; hashmap typed ≤25 ms, navier_stokes typed ≤80 ms.

### T19-6 — Record access residue (owns richards, deltablue, cd, splay) — **PARTIALLY LANDED 2026-08-19 (§7.9); the shape walk is still open**
Confirmed by profiling and promoted to the round's top lever: richards2 was 96% runtime helpers with 40% in `memcmp` from map field lookup. The identifier-compare half landed (json untyped 0.648, richards 0.823, deltablue 0.863). `fn_map_set` is now 46.3% of richards2 on its own — the largest single cost in the corpus.
⚠ The obvious follow-up, an O(1) probe of `TypeMap`'s existing property table, was built and **reverted**: it lost 6% at 0/11 wins because half the corpus has no named type to certify and, at 5–8 fields, hashing the key bytes costs what walking them costs. It only becomes viable with a precomputed site hash or a NameId-keyed index (§7.9).
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
2. **T19-2** — items 1/3/4 landed 2026-08-19; the bar is NOT restored (2 of 13 rows cross it).
   What remains splits in two: item 2 (record/`map?` contracts) folds into T19-6, and the
   scalar-recursion rows (fft 2.02x, cpstak 1.35x, tak 1.28x, ack 1.13x) are a call-boundary
   cost with no boundary/compare defect behind it — they need their own track, see §7.8.
3. ~~**T19-5**~~ — **refuted by profiling, §7.9.** Its three rows spend 0–3.2% in
   `fn_array_set`. Do not implement.
4. **T19-6** — now the top lever, not the fourth: richards2 is 96% runtime
   helpers with 40% in `memcmp` from map field lookup (§7.9). The identifier
   compare landed; the O(fields) shape walk and NameId-keyed `fn_map_set` remain.
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

~~Caveat recorded in `C2MIR_COVERAGE.md`: `crypto_sha1.ls` currently computes a WRONG digest
on the Lambda engine (all-ones saturation...)~~ — **CLEARED 2026-08-19.** Both `crypto_sha1`
and `crypto_sha12` now assert PASS, and the digest `2524d264…b796d` was re-verified against
Python's `hashlib` on the exact doubled-plaintext input. It also passes on the archived
`f46aae989` control, i.e. the fix predates the T19-2 slice and was not bought by it; where in
the window it landed was not bisected (each step needs a full release build).

⚠ The cells, however, are NOT retroactively trustworthy: the crypto_sha1 **MIR** cells in
`benchmark_results_v33.json` and earlier were measured while the engine produced all-ones,
which is not the same work as the correct digest. Re-measure that row before comparing it
across sessions; its C2MIR cell was always correct.

### 7.7 Revised round targets

Unchanged from §6 in kind, restated against v33 on the §7.6 full-coverage basis:
typed/C2MIR **5.34x → ≤3.6x** (the CF4 ladder point; ≤2.9x if T19-6 reaches 3x-of-C on the
macro rows), typed/Node **0.83x → ≤0.55x**, **zero rows where an annotation costs >5%**, no
row >10% above its v33 value at round close. Non-goals and gates of §6 stand.

### 7.8 What the T19-2 slice closed, and what it did not (2026-08-19)

Closed: the three *emission* defects behind the annotation tax — the assignment
boundary (T19-A), the declared-`int[]` read guard (T19-B), and the int-lane
ordered compare (T19-C). Full detail under T19-2 in §5. Paired against the
pre-change control: geomean **0.9879** over 112 rows, **0.9811** on the typed
half, every row byte-identical, biggest movers navier_stokes typed 0.662 and
fannkuch typed 0.716.

Not closed: the categorical bar. Only 2 of the 13 §7.2 rows now sit within 5% of
their untyped twin. The residue is **not** more of the same mechanism, which is
the useful finding — after removing the boundary, guard and compare costs, what
is left divides cleanly:

| residue | rows | owner |
|---|---|---|
| record/`map?` field admission | splay 1.83x | T19-2 item 2, folds into **T19-6** |
| call-boundary cost on scalar recursion | fft 2.02x, cpstak 1.35x, tak 1.28x, ack 1.13x | **new track needed** |
| still-mixed (array + call) | bounce 1.45x, knucleotide 1.13x, pnpoly 1.18x, brainfuck 1.07x | T19-5 store edge |

The scalar-recursion group is the one the round has never had a track for. Those
bodies contain no array read and no int-lane compare; §1 already charged their
regression to Tune18's E5/E6 range-fact and root-frame work, and this slice
confirms it by elimination — every other mechanism has now been removed from
those rows and the gap did not move. **A T19-9 (annotated call boundary on
self-recursive scalar functions) should be opened rather than continuing to
count these rows against T19-2.**

⚠ Measurement discipline, restated because it bit again: at 5 pairs this run
showed six rows "regressing" 5–11%; at 15 pairs every one of them dissolved, and
one (`diviter untyped`) inverted from 1.108 to 0.960. `sumfp untyped` swung
0.896 → 1.132 between runs on a float-only body that these changes cannot reach.
Do not accept a single-run regression on a short row without re-running it at
higher pair counts against the archived control.

### 7.9 Profiles overturn T19-5 and localize T19-6 (2026-08-19)

Every track in §5 was ranked from **static helper counts in the MIR dump**. Those
counts are not time. Sampling the release binary (macOS `sample`, iteration
counts raised so each row runs 3–15 s) says the round was about to spend a whole
track on a 3% lever.

| row | in JIT code | in named helpers | top named helper |
|---|---:|---:|---|
| fannkuch2 (N=11) | **100%** | 0% | — |
| navier_stokes2 | 93% | 6% | `fn_array_set` 3.2% |
| hashmap2 | 35% | 64% | `fn_index` 14.2% |
| richards2 | 3% | **96%** | `_platform_memcmp` **40%** |

**T19-5's premise does not survive.** Its three headline rows were chosen because
`fn_array_set` survives in their dumps — navier_stokes2 ×33, hashmap2 ×11,
fannkuch2 ×8. In execution:

- **fannkuch2 spends 0% in any runtime helper.** All eight `fn_array_set` sites
  are on guarded fallback arms that never run. The store edge costs it nothing.
- **navier_stokes2's hot function already has the inline store.** `_lin_solve`'s
  inner loop emits a bare `dmov d:(base+idx*8), value` — no call. Its surviving
  `fn_array_set` are in `_add_points`/`_vel_step`/`_set_bnd`/`_project`, and the
  whole helper adds to **3.2%**. The acceptance target (typed 111 → ≤80 ms, i.e.
  −28%) is not reachable through the store edge at all.
- **hashmap2 never calls `fn_array_set`.** Its cost is the generic *read* chain:
  `fn_index` 14.2% → `item_at` 10.3% → `array_num_read_item` 11.3% →
  `array_num_get` 11.3% → `array_num_resolve_data` 6.3% — **53% in five nested
  calls per element read**, plus `lambda_numeric_boundary_admit` 6.8%.

hashmap2's reads are generic for a reason the compiler cannot currently fix:
`type HashMap = {keys: array, values: array, …}` declares its fields as bare
`array`, so `var hm_keys = hm.keys` has no element witness to consume. Recovering
it means proving, closed-world over the named type, that every construction of a
`HashMap` gives `keys` an int array — the record-field analogue of T19-4(a).
That is real work and it belongs with **T19-6**, not with the store edge.

**Disposition: T19-5 as written is refuted. Do not implement it.** If the store
edge is revisited it needs a row where stores are measurably hot; none of its
three nominated rows is.

**T19-6 is confirmed and sharply localized.** richards2 — the largest absolute
typed time in the corpus — spends 96% in runtime helpers, and the single biggest
entry is `_platform_memcmp` at 40% (plus `DYLD-STUB$$memcmp` 3.1%), reached from
BOTH `fn_member_by_id` (field read) and `fn_map_set` (field write). Behind those:
`fn_map_set` 23.7% self, `map_field_to_item` 7.8%, `fn_member_by_id` 6.4%,
`map_shape_field_to_item` 5.4%. Record access is not slow because of helper-call
overhead in general; it is slow because **every field access does a linear shape
walk comparing name BYTES**.

#### T19-6 slice landed: stop calling memcmp to compare identifiers

`map_get_by_name_id_keyed` and `fn_map_set` both confirm a field by its name
bytes. That byte confirmation is **correct and stays**: `ShapeEntry` carries a
`name_id`, but element/input shape transitions can preserve an old identity while
replacing the spelling, so neither direction of an id comparison is a sound
filter (ids equal + bytes differ must NOT match; ids differ + bytes equal MUST
match). What was wrong is how the comparison was spelled — field names are short
identifiers, and at that length the libc `memcmp` CALL costs more than the
comparison it performs.

One shared `shape_field_name_equals()` next to `ShapeEntry` (promoted to the
header rather than copied into both walks) compares inline up to 32 bytes and
keeps `memcmp` only above it. It reads the same authoritative bytes, so every
shape resolves exactly as before [D4.6.1v2–D4.6.2v2].

⚠ A first-byte-reject variant that still called `memcmp` for the tail was built
and measured, and it is **half the win**: richards 0.905 vs 0.823, json untyped
0.824 vs 0.648, and it turned splay into a regression (1.055). Avoiding the call
*entirely* is the effect; short-circuiting mismatches is not.

Paired A/B vs the T19-2 binary, 112 rows, all byte-identical stdout:

| | geomean |
|---|---:|
| all 112 rows | **0.9894** |
| untyped | 0.9871 |
| typed | 0.9916 |

| row | ratio | pairs won |
|---|---:|---|
| json untyped | **0.648** | 5/5 |
| richards typed / untyped | **0.823 / 0.833** | 5/5 |
| deriv untyped | 0.836 | 5/5 |
| deltablue untyped / typed | 0.863 / 0.880 | 5/5 |
| raytrace3d untyped | 0.869 | 5/5 |
| hashmap untyped | 0.926 | 5/5 |

⚠ The 5-pair run again showed a tail of 5–24% "regressions" on rows this change
cannot touch (`list` 1.239, `fib` 1.187, `collatz` 1.110, `sumfp` 1.115). At 15
pairs every one dissolved — list 0.995, fib 1.003, collatz 0.982, sumfp 1.000 —
and two inverted into wins (quicksort untyped 1.068 → 0.926, towers typed 1.079 →
0.923). Third time this round. **Treat any 5-pair regression on a short row as
unmeasured until it is re-run.**

Gates: baseline 3827/3827, test262 40261/40261 with zero regressions.

#### What is still open in T19-6

The memcmp call was the cheap half. The linear shape walk itself remains: field
access is still O(fields) per hop, and `fn_map_set` 23.7% is still a
string-keyed lookup plus generic COW-per-write. The infrastructure for the fix
already exists but is not used on this path — `TypeMap` has an inline
open-addressing property hash table (`TYPEMAP_HASH_CAPACITY`, built for JS
objects). Routing Lambda map field access through it, and giving `fn_map_set` a
NameId-keyed entry point, is the remaining T19-6 work and is worth more than what
just landed.

#### The O(1) shape index was built, measured, and REVERTED

The obvious next step after the identifier compare was to stop walking: `TypeMap`
already carries an open-addressing property table (`field_index`,
`typemap_hash_lookup_by_name_id`), built for JS objects and for Input and never
for compiler-built shapes — which is exactly what a `type Foo = {...}` record is.
It was implemented behind two certificates and it does not pay.

The soundness design was fine and is worth keeping on record:

- `is_trusted_contract` is set ONLY by the compiler for named contracts and
  explicitly cleared for Input/JS clones, so it is precisely the set of shapes
  whose generated NameIds cannot drift from their spelling — the drift that
  makes the byte confirmation load-bearing everywhere else.
- `field_count == length` certifies that every field reached the table: no
  unnamed nested-spread entry, no duplicate name, no saturation. That is what
  lets one probe stand in for a walk under last-writer-wins.
- Any miss or failed confirmation returned NULL and ran the original walk, so a
  stale table could cost time and never correctness.

**Measured against the identifier-compare binary, 11 alternating pairs:
richards untyped 1.0605 at 0/11 wins, splay untyped 1.0651 at 0/11, deltablue
1.063/1.065 at 1–2/11.** A consistent loss, not noise — the 0/11 win counts are
the signature the noisy rows never produce.

Two reasons, and the second is the general one:

1. **Half the corpus has no named types to certify.** `richards.ls` declares
   zero types; only `richards2.ls` does. The untyped twin could never take the
   probe, so it paid the added checks for nothing — and it regressed most.
2. **At record sizes the probe is not cheaper than the walk.** These records
   hold 5–8 fields, and after the identifier compare landed each rejected field
   costs about two byte comparisons. The probe must first hash the key bytes —
   an FNV loop over *the same bytes* — before it can index. O(1) only beats
   O(n) once n pays for the hash, and n≈4 does not.

**Precondition for revisiting:** the hash must not be computed at lookup time.
Either the call site carries a precomputed hash beside the NameId (the E3
NameId-hoist pattern, extended to publish `typemap_name_hash` as a site
constant), or trusted shapes get an index keyed on the NameId itself so the
probe needs no bytes at all. Without one of those, do not re-attempt this.

### 7.10 Cumulative position after the 2026-08-19 session

Paired A/B, `lambda-t19-control` (session start, `f46aae989`) → `lambda-t19-6`
(T19-2 items 1/3/4 + T19-6 identifier compare), 112 rows, 5 alternating pairs,
**all byte-identical stdout**:

| | geomean |
|---|---:|
| all 112 rows | **0.9762** |
| typed (56) | **0.9798** |
| untyped (56) | **0.9726** |

| row | ratio | | row | ratio |
|---|---:|---|---|---:|
| navier_stokes typed | **0.643** | | raytrace3d untyped | 0.847 |
| json untyped | **0.664** | | queens untyped | 0.863 |
| fannkuch typed | **0.697** | | quicksort untyped | 0.894 |
| richards untyped / typed | 0.823 / 0.835 | | deltablue untyped | 0.897 |
| deriv untyped | 0.833 | | bounce typed | 0.909 |

**Zero confirmed regressions.** The 5-pair run again flagged 14 rows above 1.05,
two of them at 0/5 wins. Re-run at 15 pairs, thirteen dissolved — fannkuch
untyped 1.191 → 1.020, primes typed 1.099 → 1.017, fft typed 1.133 → 1.019,
knucleotide typed 1.113 → 1.020 — and regexredux typed inverted from 1.111 to
**0.909**. The last holdout, `tak untyped` at 1.137, was ambiguous at 15 pairs
(6/15 wins, i.e. a coin flip with a skewed median) and settled at **0.993** over
31 pairs. Fourth confirmation this round that a 5-pair number on a short row is
not a measurement.

Against the §7.7 targets (composing the measured typed/untyped geomeans onto the
published v33 levels): typed/Node 0.83 → **~0.81** (goal ≤0.55), typed/C2MIR
5.34 → **~5.23** (goal ≤3.6), untyped/Node 1.41 → **~1.37** (goal ≤1.0), rows
where an annotation costs >5% 13 → **11** (goal 0). The round is roughly 3% of
the way to its geomean targets; every large lever (strings, records, float
lanes) is still ahead, and two of them now have a named prerequisite in front.

### 7.11 The record rows are two different problems, and richards2 is mislabelled

Profiling the two headline T19-6 rows separately (release binary, lengthened
runs) shows they do not share a bottleneck — and that the round has been reading
one of them wrong.

**richards2 declares seven record types and uses none of them.** `type Packet`,
`type TCB`, `type Scheduler` … are all present at the top of the file, and every
one of its seven record locals is an unannotated map literal
(`var pkt = { link: null, identity: 0, … }`), with every function taking untyped
parameters and returning `any`. Zero declarations name a record type. So the
corpus's largest absolute "typed" record row is, for record purposes, an
**untyped** workload — which is why its typed/untyped ratio sits near 1, and why
both the `is_trusted_contract` certificate and the declared-shape direct write
miss it entirely. Its literal field types (`null`, `0`, `false`) also disagree
with what is later stored, so each first write is a NULL→MAP retag.

| | richards2 (unannotated records) | deltablue2 (57 typed decls) |
|---|---:|---:|
| in runtime helpers | 93% | 87% |
| `fn_map_set` | **46.3%** | 14.5% |
| `fn_member_by_id` | 10.2% | **12.7%** |
| `map_field_to_item` | 12.3% | 11.9% |
| `map_shape_field_to_item` | 6.6% | 6.4% |
| `lambda_type_matches` | 5.9% | 7.1% |
| `lambda_type_check` | 3.5% | 4.0% |
| `lambda_module_name_id_at` | 2.8% | 5.5% |

Two distinct levers fall out:

- **Annotated records (deltablue2, splay2, cd2)** — field *reads* dominate at
  ~31% (`fn_member_by_id` + `map_field_to_item` + `map_shape_field_to_item`),
  writes are only 14.5%, and a further **11% is `lambda_type_matches` +
  `lambda_type_check`** re-proving at runtime what the annotation already fixed,
  plus 5.5% re-resolving NameIds that E3's hoist was supposed to make O(1) per
  function. `emit_mir_direct_field_write` already exists for the write side; the
  read analogue and the check elision are the missing pieces, and together they
  address ~47% of an annotated record row.
- **Unannotated record literals (richards2)** — needs the map literal's own
  shape to become the binding's contract, and the literal's field types widened
  from the assignments that follow in the same function. That is local
  inference, not the closed-world analysis hashmap2 would need.

⚠ Do not rank record work off richards2's absolute time again without checking
which of these two it exercises. The 46% `fn_map_set` figure is the untyped path.

### 7.12 Type-check elision and the direct-read gate (2026-08-19)

Two changes, one measured win and one measured-flat generalization. Both keep the
corpus byte-identical and the baseline at 3827/3827.

#### Landed: explicit-`any` boundaries emit no check

A boundary against explicit `any` can only ever return its input — the box, the
`lambda_type_check` call, the error test and the unbox compute a known answer.
`return_contract_needs_checked_boundary` already encoded exactly this rule at the
return firewall, and deliberately as **pointer identity against `&TYPE_ANY`**,
because the internal any-without-error top shares the same compact TypeId while
remaining a real firewall.

That rule was never applied at the **declaration** or **parameter** boundary. So
`type Variable = any` — which is how deltablue2 spells 57 of its declarations —
paid a boxed `lambda_type_check` *per execution* to confirm that a value is
`any`. Verified in the dump: the call sits on the straight-line path inside the
loop body. The predicate was generalized to `mir_contract_needs_checked_boundary`
(one predicate, not a copy) and applied at both sites; the declaration site keeps
the boxing the boundary would have done and drops only the call, because the
binding still publishes an Item.

Isolated A/B, 9 pairs: **deltablue typed 0.9532 at 9/9 wins**, splay typed 0.9840
at 7/9, everything else flat. A real-contract twin still emits its check.

#### Landed but measured flat: nullable named-map objects in the direct-read gate

`emit_mir_direct_field_read` already existed and already fired for declared
locals and parameters. It did **not** fire when the object was a `T?` occurrence,
so every hop through a nullable named field fell back to `fn_member_by_id`.
`mir_nonnull_contract_base` already unwraps that; the gate now uses it.

⚠ Restricted to **container fields**, and the reason is a semantic seam worth
keeping: a null object and a null field must give the answer the generic path
gives, and `null.k` yields **null**. The container arm reconstructs both as
`ItemNull` and matches; the scalar arms return a zero of the field's lane (0.0
for a float), which does not. So an object that may be null is admitted only for
container fields.

**Measured flat** (11 pairs, win counts all near half). splay2's
`fn_member_by_id` sites fall 41 → 36 and none of the five are hot. The reason is
in the source, not the compiler: splay2 declares its links as
`left: map?, right: map?` — generic maps with no shape — so the chain the hot
loop actually walks has nothing to be direct about. Only `SplayTree.root` is a
named `SplayNode?`, and it is read once per operation.

Kept because it is a correct generalization that costs nothing and removes a real
restriction; it simply has almost nothing to bite on in this corpus. ⚠ It could
not be validated against a deeply-annotated variant either — see below.

#### ⚠ Pre-existing: a self-referential nullable record field hangs

Rewriting splay2's links as `left: SplayNode?, right: SplayNode?` (a diagnostic
copy under `temp/`, not a benchmark edit) **hangs** — over 2 minutes with no
output. It hangs identically on `lambda-t19-6`, which predates both changes
above, so this is **pre-existing and unrelated**. A minimal
`type N = {k: int, nxt: N?}` runs fine, so the trigger is more specific than the
self-reference alone and was not minimized further.

This matters beyond the hang: it is a plausible reason the benchmark declares its
links as `map?` in the first place, and it caps what any direct-read work can
demonstrate on this corpus. Worth its own investigation before more record
tuning is ranked.

### 7.13 Recursive record types: why `left: SplayNode?` hangs (2026-08-19)

splay2 declares its tree links as `left: map?, right: map?` rather than
`left: SplayNode?`. That is not a style choice — the honest declaration is
unusable, and §7.12's direct-read work could not be validated because of it.

**It is not a compile hang.** The module transpiles and reaches
"Executing JIT compiled code"; the cost is entirely at runtime.

**Root cause: a declared boundary against a recursive record structurally walks
the whole reachable structure.** `lambda_type_matches` → the fast-mode schema
validator → `validate_against_map_type` → per field
`validate_against_type` → for `left: SplayNode?` →
`validate_occurrence_type` → `validate_against_map_type` on the child … and so
on to the leaves. Admitting ONE node validates its entire subtree, so an O(1)
admission becomes O(n) and the workload becomes O(n²):

| TREE_SIZE (2 iterations) | 800 | 1600 | 3200 | 8000 |
|---|---:|---:|---:|---:|
| `left: SplayNode?` | 0.63 s | 1.94 s | 6.44 s | **38.0 s** |

against ~0.25 s for the *entire* 50-iteration benchmark when the same field is
spelled `map?`, which the validator settles with a single "is it a map".

Two compounding costs, from a sample of the hung process: ~46% in the validator
recursion itself (`validate_against_map_type` / `validate_against_type` /
`validate_occurrence_type` / `validate_against_base_type`), and ~28% in
`_platform_strcmp` + `_platform_strncmp` — because each field the walk visits is
fetched by **string lookup** (`_map_get_const`), even in fast mode.

#### Landed: shape identity short-circuits the walk

A map whose runtime `TypeMap` pointer IS the expected contract was built to that
shape; its slots are that shape's slots, so walking its fields cannot discover a
disagreement its construction did not already prevent. Added at the top of
fast-mode `validate_against_map_type`, and deliberately **fast-mode only** —
that is the runtime admission predicate, while the reporting validator owes a
per-field diagnosis rather than a verdict [D3.2.1, D4.6.1v2].

Worth 38.0 s → 15.0 s at TREE_SIZE 8000, and it pays on the existing corpus too:
**splay typed 0.9252 at 9/9 wins**, cd typed 0.955, deltablue typed 0.985.

#### Still open: the asymptotics, because instances do not share shapes

The fast path fires for some nodes and not others, so the walk survives and the
curve is still quadratic (3200 → 8000 goes 3.6 s → 15.5 s). Instrumenting the
misses shows why: **each node ends up with its own distinct `TypeMap`** —
sequential heap addresses, one per instance, all structurally identical to
`SplayNode`. Identity can never hold for them.

The literal `{key: key, left: null, right: null, value: value}` does not adopt
the named contract's TypeMap (`mir_direct_map_contract` →
`mir_map_literal_matches_contract` decides this, and the machinery to build in
the contract's shape already exists — `emit_map_alloc` → `map_with_type_tl`),
and the first `node.left = other` retags a NULL slot to MAP, which clones the
shape per instance.

Two routes, both real design work:

1. **Make instances share shapes.** `TypeMap` already carries
   `TypeMapTransition* transitions` and `is_transition_shared_shape`, built for
   JS objects. Routing Lambda's field retag through that table would give every
   node the same shape, make identity fire universally, and cut 8000 TypeMap
   allocations to a handful. This is the fix that also helps memory.
2. **Make named-record validation compositional.** Validate the immediate shape
   and stop, on the argument that a value stored into a `SplayNode?` field was
   itself admitted at that store's boundary. This is cheaper to implement but it
   is a **semantics ruling about boundary depth** and needs an `S#`/`D#` entry
   before it can land, not just a code change.

Route 1 is preferable: it is a representation fix with no semantic content, and
it subsumes the string-lookup cost as well.

⚠ Verification gap on the landed slice: the validator's own gtest binaries
(`test_validator_gtest` and siblings) **fail to load** —
`symbol not found in flat namespace '_ItemNull'` — a pre-existing link problem
in those targets, unrelated to this change. Coverage therefore rests on
`make test-lambda-baseline` (3827/3827), `test_validator_path_reporting` (18/18)
and test262. Fixing those link failures is worth its own task.

## 8. Negative results ledger — what was tried and did not work

Each item is recorded in full at its cross-reference; this section exists so a
later session can find them in one place instead of re-deriving them. **An entry
here is a decision, not a to-do.** Where an item can be revisited at all, the
precondition is stated; if a precondition is not met, do not re-attempt.

### 8.1 Built, measured, and rejected

| what | measured cost | where |
|---|---|---|
| Transitive scalar param edge (`INFER_FORWARDED_ARG`) | geomean **1.02** over 63 rows (gcbench +31%, nqueens +29%, binarytrees +26%) for spectralnorm 1.25x | T19-4 |
| Honest call-site join (ANY instead of skip) | geomean **1.038** on its own | T19-4 |
| Dropping the `INFER_FLOAT_CONTEXT` veto outright | net geomean **0.5%** — spectralnorm 1.87x paid for by brainfuck2 +13%, gcbench2 +11% | T19-4 |
| O(1) shape index over `TypeMap.field_index` | **1.060 / 1.065 at 0/11 wins** | §7.9 |
| First-byte-reject variant of the identifier compare | **half the win**, and turned splay into a regression (1.055) | §7.9 |

The two shipped-instead alternatives are worth remembering: the FLOAT_CONTEXT
veto was kept but **narrowed** (drop it only when the parameter's own arithmetic
uses are int-flavoured), which bought the same spectralnorm win with the
brainfuck2 regression gone; and the identifier compare shipped as a *full* inline
loop rather than a memcmp-with-early-out, because avoiding the libc CALL is the
entire effect.

⚠ The O(1) index and the transitive scalar edge share a failure shape: **both
were correct and both cost more than they saved.** Neither is a bug to fix. The
index needs a precomputed site hash or a NameId-keyed table before it can pay;
the scalar edge needs "unknown yet" separated from "genuinely dynamic" in the
call-site join. Absent those, re-attempting either reproduces the same loss.

### 8.2 Refuted before implementation

**T19-5, the store edge (§7.9).** Ranked from static `fn_array_set` counts in MIR
dumps. Profiled, its three nominated rows spend **0%** (fannkuch2 — all eight
sites are on guarded arms that never execute), **3.2%** (navier_stokes2, whose
hot `_lin_solve` loop already emits a bare inline `dmov`) and **0%** (hashmap2,
which never calls it at all). The track was closed without writing code.

### 8.3 Diagnosed at the wrong site

Three times the *mechanism* named in the analysis was right and the *site* was
one level away. This is the round's most repeated error and it is cheap to guard
against: reproduce the emission on a two-line probe before editing the emitter.

| named site | actual site |
|---|---|
| T19-A: the declared-binding boundary | the **assignment** boundary — the declaration site already had `emit_native_scalar_declaration_boundary` |
| T19-B: the declared `T[]` read lowering | **`int()`'s registry row** — `success_type` is the looser `&TYPE_NUMBER`, which made every store look representation-changing |
| T19-3(a): "the counted-loop lowering" | the **gate's oracle** — it asked the boxed carrier (`get_effective_type`) instead of the lane witness |

### 8.4 Measurement and method failures

These produced wrong conclusions, not just wasted time.

1. **Static dump counts read as time.** This is what mis-ranked T19-5 and it
   would have cost a whole track. A helper's count in a finalized MIR dump says
   nothing about whether its block executes. *Profile before ranking.*
2. **Believing a 5-pair paired run — four times.** Every session-flagged
   regression above 1.05 on a short row dissolved at 15–31 pairs, several
   inverting into wins (diviter 1.108 → 0.960, regexredux typed 1.111 → 0.909,
   quicksort 1.068 → 0.926, tak untyped 1.137 → **0.993** at 31 pairs). A win
   count near half with a skewed median is the tell. *A 5-pair number on a short
   row is not a measurement.*
3. **Profiling the debug+ASAN binary.** `make test-lambda-baseline` leaves a
   sanitizer-instrumented `lambda.exe` behind, and a profile taken on it shows
   `__asan_memset` in the top ten and distorts every proportion. *Check
   `otool -L lambda.exe | grep asan`, or profile an archived release binary.*
4. **Classifying MIR hot/cold by scanning backwards.** Two attempts were wrong:
   a fixed N-line window misses guards separated by root-slot spills, and
   "nearest enclosing label is a branch target" misses blocks entered by an
   unconditional `jmp`. Static CFG reasoning on this dump format is not worth
   the effort — *the profiler answers the same question directly.*
5. **`mir-check` fixtures scoped to `main`.** The body lives in the mangled
   `_main_#`; `main` is a thin wrapper, so patterns silently fail to match.
6. **`mir-check` call counts that cannot discriminate.** Both the fast and the
   boxed declared-scalar boundary call `lambda_type_check` exactly once — the
   difference is whether the call is on the straight-line path. *Assert
   adjacency (`expect_seq` + `next_line`), and note that the matcher does not
   backtrack, so step 0 must be unique.*

### 8.5 A doc claim that was wrong

§7.6 recorded `crypto_sha1` as computing a wrong digest (all-ones saturation).
It passes on the archived `f46aae989` control and on current builds, and the
digest re-verified against `hashlib`. Corrected in place. ⚠ The consequence
survives the correction: the crypto_sha1 **MIR** cells in
`benchmark_results_v33.json` and earlier were recorded while the defect was live
and time a different computation; re-measure that row before comparing it across
sessions.

## 9. Design note: validating recursive and structural contracts (2026-08-19)

D3.2.2* is right on the principle — *"the validator is the runtime enforcer for
user-defined types: deep, on first crossing"* — and the implementation diverges
from it in a way that is worth spelling out, because the divergence is what makes
recursive types unusable. Three measured facts frame the design.

### 9.1 Three findings

**(1) `on first crossing` is not what happens — it is every crossing.**
Re-validating an already-admitted value is stricter and slower than the ruling
asks for. That is the whole of the O(n²) reported in §7.13.

**(2) A field store could skip its check on a FALSE PROOF.** ⚠ This entry
originally read "field stores do not enforce the field contract". That was wrong,
and the truth is narrower and more useful. Stores DO enforce: a value the
compiler cannot type statically goes through the checked setter and raises
correctly, for both a wrong map and a wrong scalar. The hole was in the
*elision* — the direct-write gate accepted a **storage-lane** proof
(`mir_expr_proves_native_return_lane(value, LMD_TYPE_MAP)`, i.e. "it is a map")
as if it were a **contract** proof for a *shaped* field. `nxt: N?` and
`{zzz: "not an N"}` are both `LMD_TYPE_MAP`, so the check was skipped:

```lambda
type N = {k: float, nxt: N?}
var a: N = {k: 1.0, nxt: null}
var bogus = {zzz: "not an N"}
a.nxt = bogus          // was ACCEPTED; now raises
```

**Fixed 2026-08-19**: a nominal (shaped) field contract can only earn the direct
write through `proven_map_value`, which consults the real shape relation. Both
lane predicates — `exact_direct_lane` and `proven_direct_lane` — are gated on it.
⚠ The nominal test must key off the field's OWN contract: `N?` unwraps to a
`TYPE_KIND_UNARY` carrier whose `type_id` is `LMD_TYPE_TYPE`, so testing the
unwrapped simple decl silently never fires (cost me two failed attempts).
Measured free — splay typed **0.897 at 8/9 wins**, nothing else above noise;
baseline 3827/3827.

**(3) Deep validation is WRONG for cyclic data, not merely slow.** A cycle of two
well-formed `N` nodes is a valid `N`. Validating it reports:

```
error[E201]: type check at declaration 'c' failed: expected N, got map;
validator at .nxt.nxt.nxt … (~76 hops)
```

It does not hang — it bottoms out on a depth limit and returns a **false
negative**. Structural deep validation of a cyclic value cannot terminate without
either cycle detection or a depth bound, and a depth bound rejects valid data.
Recursive types describe exactly the shapes that can be cyclic, so this is not an
edge case for them; it is the main case. **Shallow-by-construction is therefore
not an optimization for recursive contracts — it is the only formulation that is
correct.**

### 9.2 The two cases, and why they need different machinery

- **Built under annotation.** Every write went through a declared field contract,
  so conformance is an invariant of construction. The value should already carry
  its declared shape and a shallow match should settle it. Needs finding (2)
  fixed to be true.
- **Generic or ingested (`any`, `map`, `array`, Input).** Nothing enforced the
  shape on the way in, so the first crossing genuinely has to look. That result
  must not be discarded: the same document is matched against the same contract
  repeatedly.

### 9.3 One data : many types — put the memo on the SHAPE, not the value

The stated limitation of "migrate the type onto the data" — *one datum can only
carry one type, but structurally it satisfies many* — dissolves once the thing
being recorded changes. **Do not tag the datum with the contract it matched.
Refine the datum's shape to what the walk actually learned.**

Conformance to a structural contract is a property of the **(shape, contract)
pair**, not of the value: two maps sharing shape `S` conform to `T` identically,
provided `S` is precise enough to decide it. Shapes are interned and few; values
are many. So:

- the memo is keyed `(S, T) → verdict` — bounded by *distinct shapes × contracts*,
  never by data size, and one entry serves every instance;
- the deep walk's product is not a bool but a **more precise interned shape `S'`**
  describing the data as it actually is. Afterwards **any** contract `T` is
  answered by `subtype(S', T)` — the static relation D3.2.1 already names. One
  shape, many contracts. That is the many-types answer.

A side table keyed on the *value* (option b) is then unnecessary at the value
level: it degenerates into the small `(S, T)` table above, without the pointer
lifetime, GC-movement and mutation-invalidation problems that a value-keyed
table carries.

### 9.4 Proposed layering

| layer | question | cost | when |
|---|---|---|---|
| 0 | `S == T`? | pointer compare | landed (§7.13) |
| 1 | `subtype(S, T)`? memoized on the shape pair | O(1) after first | settles case 1 entirely |
| 2 | deep walk, then **refine** the value to interned `S'` | O(n) once per datum | case 2, first crossing only |
| 3 | value-level predicates (`T where …`) | per crossing, uncacheable | D3.2.2 already scopes these out |

Cycles are handled at layer 2 by marking a node in-progress: a back-edge is
`subtype`-checked against the in-progress contract rather than re-descended —
which is also what makes finding (3) go away.

**Prerequisite for all of it: shapes must be interned and shared.** Today they
are not — §7.13 measured one distinct `TypeMap` per instance, which is why even
layer 0 misses. `TypeMap` already carries `TypeMapTransition* transitions` and
`is_transition_shared_shape` for JS objects; routing Lambda's field retag through
that table is the enabling change, and it independently removes the per-instance
`TypeMap` allocation.

### 9.5 What this asks of the spec

D3.2.2's *principle* stands; it needs its details separated, roughly:

- **D3.2.2v2 (a)** A declared field store is a contract boundary and enforces the
  field's declared type. (Today it does not — finding 2.)
- **D3.2.2v2 (b)** Given (a), a value built under a declared contract is valid by
  construction; a crossing checks it **shallowly**.
- **D3.2.2v2 (c)** A value of generic or ingested origin is validated **deeply on
  first crossing**, and the result is retained by refining the value's shape —
  restoring the ruling's own "on first crossing" wording, which the
  implementation does not honour today.
- **D3.2.2v2 (d)** Deep validation is cycle-aware: a back-edge is resolved by
  `subtype` against the in-progress contract, never by re-descent. A depth bound
  must never produce a type verdict (finding 3).

Requires a `v2` revision in `doc/Lambda_Formal_Design.md` with a doc semver bump,
plus this note as the working record [D3.2.1, D3.2.2*, D4.6.1v2].


## 10. Implementing D3.2.2v2 — findings that reorder the plan (2026-08-19)

Target order was: intern/share shapes → enforce stores → validate shallowly.
Investigating the first step showed the three are not independent, and that the
first one cannot be completed before the second.

### 10.1 Why shapes are not shared: contract adoption is gated on STATIC proof

A map literal adopts its declared contract's `TypeMap` — and therefore shares a
shape with every sibling instance — only when `mir_direct_map_contract` →
`mir_map_literal_matches_contract` can prove, **at compile time**, that every
field initializer satisfies the corresponding field contract. Otherwise the
literal builds its own shape, and from there each instance diverges further on
its first field retag.

Two distinct blockers, found by instrumenting the match:

**(a) A `null` initializer was rejected for a nullable named field.** Seeding
with null is how a recursive record is *built* —
`{key: k, left: null, right: null}` against `left: SplayNode?` — and
`mir_map_field_contract_compatible` required the proven type to be a MAP, so
`null` failed. **Fixed**: a proven `null` is admissible for any nullable field.
This unlocked `SplayTree = {root: SplayNode?}`, whose literal `{root: null}` now
adopts the contract.

**(b) An untyped value can never be proven, and that is not a fixable gap.**
splay2's `create_node(key: float, value)` leaves `value` untyped, so the
`value: map?` field has no static proof and the SplayNode literal still fails to
adopt. Scaling is unchanged (0.9 s / 2.7 s / 15.4 s at 800 / 3200 / 8000).

(b) is the load-bearing finding. **Static provability is not a completable
strategy** — any untyped expression anywhere in a literal denies the whole map
its declared shape, and untyped expressions are exactly what a gradually typed
language has everywhere.

### 10.2 Consequence: store enforcement must come FIRST, and subsumes adoption

The way to make adoption unconditional is to stop proving and start *checking*:
construct the literal directly in the contract's shape and **admit each field
value against its field contract as it is written**. Then the map has the
contract's shape by construction, whatever the static knowledge was.

That is D3.2.2v2 (a) — the store check — applied at construction, and it is the
same mechanism the mutation path needs (§9.1 finding 2: `a.nxt = bogus` is
accepted today). One mechanism, two call sites: literal construction and field
store. Once it exists:

- adoption is unconditional → instances of a declared record share one shape,
  which is the interning goal, reached without a transition cache;
- retags stop happening for declared fields, because the slot already has the
  declared contract's type — which removes the per-instance shape divergence at
  its source rather than de-duplicating it afterwards;
- shallow validation (D3.2.2v2 (b)) becomes sound, because construction is now
  the enforcer;
- the shape-identity check already landed (§7.13) then fires universally, and
  the deep walk survives only for genuinely generic/ingested data (case 2).

**Revised order: (1) field-contract admission at store and at literal
construction; (2) unconditional contract adoption for annotated literals;
(3) shallow validation for contract-built values; (4) shape refinement + the
`(S, T)` memo for ingested data (§9.3); (5) a retag transition cache only if
profiling still shows per-instance shapes after (1)–(2).**

A transition cache over `map_rebuild_for_type_change` was scoped and is NOT
implemented: after (1)–(2) most of the retags it would de-duplicate should no
longer occur, and building it first would optimize a path the design intends to
delete. `TypeMap.transitions` + `is_transition_shared_shape` remain the right
mechanism if step (5) proves necessary; `map_transition_target_for_add` in
`input.cpp` is the working pattern to copy.

### 10.3 Where the detailed design lives

Rule 17 wants the normative ruling and the working record updated together. For
this work that is four places, and only one of them is new:

| what | where |
|---|---|
| the ruling D3.2.2v2 (terse, normative, `v2` + doc semver bump) | `doc/Lambda_Formal_Design.md` |
| validation engine: the layering of §9.4, cycle rule, `(S,T)` memo | `doc/dev/lambda/LR_13_Schema_Validator.md` §3 |
| shape/TypeMap interning, adoption, retag transitions | `doc/dev/lambda/LR_03_Value_and_Type_Model.md`, "Map shape: `TypeMap` & `ShapeEntry`" |
| the decision record — options weighed, measurements, rejected routes | **`vibe/Lambda_Design_Type_Boundary.md`** (new) |

So `Lambda_Design_Type_Boundary.md` is the right name, in `vibe/` as the working
record; the *detailed* design belongs in the existing LR_13 and LR_03 rather
than a new file, since both already own their sections.

## 11. The literal-construction half: reverted 2026-08-19, RE-LANDED 2026-08-20 (§11.5)

Step (2) of §10.2 — construct an annotated literal directly in its declared
contract's shape and admit each field value it cannot prove statically. **The
approach is right and the payoff is enormous. It is reverted anyway, on a
segfault whose cause is a real prerequisite nobody had named.**

### 11.1 What was built

Three parts, all small:

1. `mir_map_literal_shape_matches_contract()` — do the literal's keys line up with
   the contract's fields, **in order and by name**, saying nothing about whether
   the values satisfy their contracts. Order is checked because both consumers
   (the direct store loop and `map_fill`) walk values against the shape in
   lockstep.
2. Adoption in `mir_direct_map_contract` and `transpile_map` switched from the
   value proof to the shape match — so an untyped initializer no longer denies
   the whole literal its declared shape (§10.1 finding b).
3. Per-field admission in the generic fill path: for each field the compiler
   cannot prove, `emit_checked_boundary(value, field_contract, "field 'x'")` plus
   the error test, before the value enters `map_fill`. The RAW per-field store
   path keeps the full value proof, since it writes slots with no admission.

### 11.2 It worked, and the numbers are the reason to finish it

Construction-time admission behaves exactly as designed — `mk({zzz: 1})` into a
`nxt: N?` field raises `type check at field 'nxt' failed`, while valid
construction is untouched. And the recursive-type problem simply disappears:

| TREE_SIZE (2 iterations) | 800 | 3200 | 8000 |
|---|---:|---:|---:|
| original (§7.13) | 0.63 s | 6.44 s | **38.0 s** |
| + shape-identity fast path | 0.86 s | 2.62 s | 15.0 s |
| **+ literal adoption** | **0.039 s** | **0.066 s** | **0.139 s** |

O(n²) → **O(n)**, and a 273x improvement at n=8000. The full deep-annotated
benchmark (`left: SplayNode?`, 8000 nodes, 50 iterations) runs in **142 ms** —
indistinguishable from the 145 ms of the `map?` workaround it replaces. The
honest declaration becomes free.

It also pays on the SHIPPED benchmark, because `SplayTree`/`SplayNode` literals
now adopt their contracts and validation goes O(1): **splay typed 0.6059 at 9/9
wins**, everything else flat.

### 11.5 RE-LANDED 2026-08-20 — `left: SplayNode?` at native speed

The three §11.1 pieces are back, behind the gate the §11.3 analysis implied.
§11.4's table is superseded: the O(n) column is now the shipped column.

**The gate is one predicate, and it needs no offset recompute.** The AST
contract shape lays fields on 8-byte strides (they were type-VALUED when built:
`sizeof(Type*)`), and every CONCRETE storage class also fits 8 bytes — only
`ANY`/TypedItem overflows at 9. So refusing ANY-bearing contracts is sufficient
for self-consistency:

```
mir_map_contract_storage_valid(expected):
    every field classifies to a concrete carrier (no ANY)
    and shape_entry_storage_fits_data(field, byte_size) for every field
```

`SplayNode = {key: float, left: SplayNode?, …}` passes — the recursive case
adoption exists to fix. `Person = {…, scores: int[], choice: int | string}`
fails (both composites classify ANY) and keeps its inferred shape, so
`proc_type_numeric_structural_admission` — the test that forced the revert —
passes untouched. ⚠ After the TB1/TB5 classifier slice reclassifies occurrences
and constrained bases to concrete lanes, more contracts pass this gate
automatically and NO code here changes.

| | before | after |
|---|---:|---:|
| `left: SplayNode?` 2 iters, n=8000 | 16.5 s (O(n²)) | **0.139 s** |
| n=800 / 3200 / 8000 | 1.5 / 2.8 / 16.5 s | **0.036 / 0.064 / 0.139 s** (linear) |
| full deep splay, 50 iters | unusable | **142 ms** — the `map?` workaround is 145 ms |

Paired A/B vs the pre-adoption control, 112 rows, all byte-identical: geomean
**1.0001** overall, **0.9868 typed** — carried by **splay typed 0.5943 at 5/5
wins**, since its literals now adopt and validation goes O(1). Twelve rows
flagged "slower" at 5 pairs all dissolved at 15 (fibfp untyped 1.171 → 1.015,
brainfuck typed 1.129 → 1.018, towers typed 1.089 → **0.978**) — the fifth
time this round.

⚠ **Semantic consequence, by design:** admitting an unproven field at
construction makes the CONSTRUCTOR fallible. A `pn make(k: float, payload) Node`
whose literal has an unprovable field now returns `Node^` and its callers must
propagate or contain. This is correct — the check has to live somewhere — but it
is a source-visible change for any function that builds a record from untyped
inputs.

Tests: `test/lambda/proc/recursive_record_contract.{ls,txt}` (untyped field
admitted at construction, contract enforced, fallible constructor contained).
⚠ It is a CORRECTNESS test only — it passes on a pre-adoption build too, since
the rejection merely reports at a different site. The O(n²) guard remains the
splay benchmark plus the scaling numbers above. Baseline 3829/3829.

### 11.4 State after the revert (SUPERSEDED by §11.5) — still O(n²) — recursive types remain unusable

⚠ **This section describes an INTERMEDIATE state that no longer ships.**
Adoption re-landed the next day (§11.5); the "reverted" row below is now the
shipped row. Kept as the record of what the identity fast path bought on its
own, and of why that was not enough.

Measured while adoption was reverted (release build, `left: SplayNode?`,
2 iterations):

| TREE_SIZE | 1600 | 3200 | 8000 | shape |
|---|---:|---:|---:|---|
| before any of this session's work | 1.94 s | 6.44 s | 38.0 s | O(n²) |
| identity fast path only (this state) | 0.79 s | 2.78 s | 16.5 s | O(n²) |
| ~~with literal adoption (reverted)~~ → **shipped, §11.5** | **0.049 s** | **0.065 s** | **0.139 s** | **O(n)** |

The shape-identity fast path and the null-initializer fix bought a **~2.3x
constant**, nothing more — 0.79 → 2.78 → 16.5 is 3.5x then 5.9x for 2x then
2.5x the size. The deep walk still ran on every declared crossing because most
instances did not carry their contract's shape.

That is exactly what adoption fixed, and the linear column was the evidence the
design was right. §11.5 records the re-landing, whose gate turned out NOT to
need the layout-seam audit this section anticipated.

### 11.3 Why it is reverted: a slot-representation seam

`test/lambda/proc/proc_type_numeric_structural_admission.ls` **segfaults**
(`exit=139`), reduced to:

```lambda
type Child = {score: int}
type Person = {age: int, child: Child, scores: int[], choice: int | string}
var updated: Person = {age: 1, child: {score: 2}, scores: [3], choice: "kept"}
let snapshot = updated
updated.child.score = dynamic(9.0)      // nested COW write -> SIGSEGV
```

⚠ The first write-up of this section blamed the **union** field, inferred from
the crash symbol `typeditem_to_item(TypedItem*)`. That was a guess and it was
wrong. Bisected properly, neither the union field nor the nested write triggers
it alone — and on a release build the same program does not segfault at all, it
reports:

```
type check at typed nested map assignment failed: expected Person, got map;
  validator at .scores: Expected type 'int', but got 'string'
```

`scores`, declared `int[]`, holds `"kept"` — the value of `choice`, the field
after it. **The values are landing in the wrong slots.**

⚠ "Different layout" was imprecise; dumping both shapes shows the offsets are
IDENTICAL and the difference is the slot FORMAT:

| field | literal's inferred shape | declared contract |
|---|---|---|
| `age` | `INT`, off 0, 8B | `INT`, off 0, 8B |
| `child` | `MAP`, off 8, 8B | `MAP`, off 8, 8B |
| `scores` | **`ARRAY`** (raw `Array*`), off 16, **8B** | **`ANY`** (`TypedItem`), off 16, **9B** |
| `choice` | **`STRING`** (raw `String*`), off 24, **8B** | **`ANY`** (`TypedItem`), off 24, **9B** |
| total | `byte_size` 32 | `byte_size` 32 |

A composite contract (`int[]`, `int | string`) cannot be reduced to one concrete
carrier, so the contract stores it **self-describing** — `type_info[LMD_TYPE_ANY]`
is `sizeof(TypedItem)` = 9 bytes, a tag plus a payload. The literal's inferred
shape resolved the same fields to concrete carriers and stores a bare 8-byte
pointer. Same offset, same total size, **different meaning for the bytes at that
offset** — and note the contract still spaces those 9-byte slots 8 bytes apart,
which is its own inconsistency.

So adoption does not shift offsets; it changes whether a slot holds a raw pointer
or a tagged `TypedItem`. A value written under one reading and recovered under
the other is garbage.

#### Resolved: the contract shape is malformed for contract use — no two paths "disagree"

The table above contains a contradiction, and chasing it (how can 9-byte slots at
8-byte strides total 32?) resolves the mechanism completely. **They can't.** The
contract TypeMap's offsets and `byte_size` were computed while its fields were
type-VALUED — in `type Person = {age: int, scores: int[], …}` the map's "values"
are type expressions, i.e. `LMD_TYPE_TYPE` payloads, and
`type_info[LMD_TYPE_TYPE].byte_size == sizeof(void*) == 8`. Stride 8 per field →
offsets 0/8/16/24, byte_size 32. When the SAME TypeMap is later consumed as a
value contract, `type_field_storage_type_id` unwraps each field and classifies
the composites (`int[]`, `int | string`) as self-describing `ANY`/`TypedItem`
slots — 9 bytes. One TypeMap, two accounting rules. The runtime's own bounds
predicate agrees it is malformed: `shape_entry_storage_fits_data(choice)` is
`24 <= 32 − 9` = **false** — by its own arithmetic the last field does not fit.

Both observed failures fall out of this one seam, byte for byte:

- `scores`' TypedItem = tag@16 + payload@17..24; `choice`'s = tag@24 +
  payload@25..32. Writing `choice`'s tag — `LMD_TYPE_STRING` = **13** — at byte
  24 overwrites the most-significant byte of `scores`' little-endian payload.
  An Item's tag lives in its high byte, so `scores` reads back as a
  **string-tagged Item**: exactly `validator at .scores: Expected type 'int',
  but got 'string'`.
- `choice`'s payload runs to byte 32 — one past the 32-byte buffer: exactly the
  ASAN heap overflow behind the debug-build SIGSEGV.

Why nothing fails today: ordinary flow never gives a VALUE map the AST contract
TypeMap — admission and `map_rebuild_for_type_change` build runtime shapes with
the consistent 9-stride rule (`byte_offset += type_info[type_field_storage_type_id
(…)].byte_size`). Adoption made real data carry the AST shape for the first
time, which is why the malformation was reachable only through it. (The
`map_get ANY type is UNKNOWN: 0` [ERR!] lines visible even in the PASSING
baseline run of this test are consistent with the same seam and worth a look in
the audit.)

➡ **Superseded by rulings TB1/TB2 in `vibe/Lambda_Design_Type_Boundary.md`
(2026-08-20)**, which resolve the storage question this paragraph left open:
`T[]` becomes a pointer slot, unions become admission-only with actual-member
storage, and adoption gates on a storage-valid contract — re-landing the O(n)
column for exactly the recursive-type shapes without touching unions.

**So the prerequisite is narrower than an audit of every fill/rebuild/COW path:
give a TypeMap used as a value contract offsets computed under the same
`type_field_storage_type_id` rule its consumers use** — either recompute at
definition time (when the type-valued map becomes a named contract) or lazily
before first adoption — and assert `shape_entry_storage_fits_data` over every
entry as the malformation tripwire. ⚠ One hypothesis in this account is inferred,
not traced: that the stride ran while fields were still type-valued. Every
number matches it (8 = sizeof(Type*)), but the exact builder line was not
pinpointed; verify it first when implementing.

(The earlier SIGSEGV was the same misalignment reached through the debug+ASAN
binary the baseline leaves behind; the release build detects it at the validator
instead of dereferencing garbage.)

**So the prerequisite is one level deeper than §10.2 said.** Adoption is not just
"use the contract's TypeMap"; it changes the physical slot layout, and every path
that fills, rebuilds or COW-detaches a map has to be reading the layout from the
shape it actually has. That audit — fill, `map_rebuild_for_type_change`,
`lambda_map_path_set_checked`, and the direct writers, against union/TypedItem
and other non-uniform slots — is the real step (2), and it should be done and
tested on its own before adoption is switched on again.

The three pieces above are small and the measurements above are the argument for
redoing them once the layout seam is closed. ⚠ Do not re-land adoption without
first making `proc_type_numeric_structural_admission` pass — it is the exact
regression test for this seam, and it is already in the baseline.

## 12. Standing summary (2026-08-20)

Where the round actually is, after two sessions. **Cumulative paired A/B**,
`lambda-t19-control` (session start, `f46aae989`) → current tree, 112 rows,
5 alternating pairs, **all byte-identical**:

| | geomean |
|---|---:|
| all 112 rows | **0.9678** |
| typed (56) | **0.9704** |
| untyped (56) | **0.9652** |

| row | ratio | | row | ratio |
|---|---:|---|---|---:|
| splay typed | **0.548** | | raytrace3d typed | 0.816 |
| navier_stokes typed | **0.656** | | deltablue typed | 0.831 |
| json untyped | **0.671** | | richards typed / untyped | 0.834 / 0.838 |
| fannkuch typed | **0.676** | | deriv untyped | 0.810 |

**Zero confirmed regressions.** Eleven rows flagged >1.05 at 5 pairs; all
eleven dissolved at 15, and several INVERTED into wins — storage typed
1.153 → **0.940**, spectralnorm typed 1.142 → **0.996**, fibfp typed
1.137 → **0.944**, binarytrees untyped 1.086 → **0.950**. Sixth confirmation
this round that a 5-pair number on a short row is not a measurement.

### 12.1 What landed

| slice | effect |
|---|---|
| T19-2 items 1/3/4 (§7.8) — assignment boundary, int-lane compare, declared-`int[]` read | fannkuch typed 0.676, navier_stokes typed 0.656 |
| T19-6 identifier compare (§7.9) — stop calling `memcmp` to compare identifiers | json untyped 0.671, richards 0.834 |
| `mir_budgets.json` re-baselined | 32 values, 14 probes |
| Validator shape-identity fast path (§7.13) | splay typed 0.925 on its own |
| Explicit-`any` boundary elision (§7.12) | deltablue typed 0.953 |
| Direct-read gate: nullable named-map objects (§7.12) | measured flat; kept as a correct generalization |
| Store-side soundness fix (§9.1 (2)) | a storage-lane proof no longer stands in for a contract proof on a shaped field — a wrong map could reach a declared field |
| G6: union-local carrier (Lane §10.4) | a float member switch MISCOMPILED (static) or SILENTLY coerced (dynamic); now boxed-from-declaration |
| **Literal contract adoption (§11.5)** | **recursive record contracts at native speed** — `left: SplayNode?` O(n²) → O(n), 38.0 s → 0.139 s at n=8000; splay typed 0.548 |

Two correctness bugs (store-side proof, G6) were found by pulling on
performance threads, and both predate this round.

### 12.2 What the round's targets look like now

Composing the measured typed/untyped geomeans onto the published v33 levels:
typed/Node 0.83 → **~0.81** (goal ≤0.55); typed/C2MIR 5.34 → **~5.18**
(goal ≤3.6); untyped/Node 1.41 → **~1.36** (goal ≤1.0). The round remains
early against its geomean targets — the large levers (strings, records,
float lanes) are still ahead.

### 12.3 Open, in the order I would take them

1. ~~**TB1/TB5 classifier slice**~~ — **G1/G2b LANDED 2026-08-20** (§12.5);
   **G5 split out**, see below.
2. **G5 — `integer` → `Decimal*` lane.** Ruled (TB4) but blocked on conversion
   plumbing: `set_field_value`'s DECIMAL arm assumes the Item already carries a
   `Decimal*`, so reclassifying without a compact-int → heap-Decimal conversion
   at the store (and in `map_field_store`) would silently corrupt
   `n: integer = 3`. Needs the conversion first, then the one classifier line.
3. **G2** — record the actual member at union-field CONSTRUCTION, as the
   mutation path already does.
4. **T19-6 remainder** — `fn_map_set` is still ~46% of richards2 on its own.
   ⚠ The O(1) shape index was tried and reverted (§7.9): it needs a
   precomputed site hash or a NameId-keyed index first.
5. **T19-8** — closed-caller float lanes; untyped ray 30x, spectralnorm 13.6x.
   The largest remaining untyped multiples, no prerequisite.
6. **T19-7** — the strings design doc; still the single biggest C2MIR lever.
7. **T19-9** (unopened) — call boundary on scalar recursion (§7.8).

Not worth doing: **T19-1** (three slices have now landed without it) and
**T19-5** (refuted by profiling, §7.9). Anything in **§8** needs its stated
precondition met first.

### 12.4 Verification standing

Baseline **3829/3829**; test262 **40261/40261**, zero regressions; ratchet
16/16; every A/B byte-identical across 112 rows. New tests this round:
`tune19_int_lane_compare`, `tune19_declared_scalar_rebind` (mir-check),
`union_local_carrier`, `recursive_record_contract`.

⚠ Standing hazard, hit SIX times: `make test-lambda-baseline` and
`make build-test` leave a **debug+ASAN** `lambda.exe` behind. Profiles taken on
it show `__asan_memset` in the top ten; benchmark A/Bs against it show uniform
2–20x "regressions". Always `make release` and check
`otool -L lambda.exe | grep -c asan` before measuring anything.

### 12.5 TB1/TB5 classifier slice — LANDED 2026-08-20 (G1, G2b)

One function, `type_field_storage_type_id`:

- **G1** — an occurrence contract (`T[]`, and `T[]?` via the optional branch)
  classifies to the **pointer lane** (`LMD_TYPE_ARRAY`), not ANY. Array and
  ArrayNum are both `Container*` and the pointee's own `type_id` discriminates,
  exactly as `map?`/named-map fields already work. Verified safe at the store:
  `set_field_value`'s container arm accepts any container in `RANGE..OBJECT`.
- **G2b** — a constrained contract (`T where …`) recurses to its **base type's
  lane**, with a self-reference guard. The predicate is an admission-time check
  on the value, not a property of how it is carried.
- **G5 deliberately NOT included** — see §12.3 item 2. Reclassifying `integer`
  to `Decimal*` without conversion plumbing would silently corrupt a compact-int
  store; that is a separate slice.

**Effect — the gate widened with no adoption-code change**, which was the point:
a `T[]`-bearing contract now passes `mir_map_contract_storage_valid` and adopts,
admitting an unproven field at construction
(`type check at field 'scores' failed: expected int[], got string`).

Gates: baseline **3857/3857**, test262 40261/40261 zero regressions, 112/112
byte-identical. ⚠ The baseline total jumped 3829 → 3857 because
`test_lambda_parser_poc_gtest.exe` had never been BUILT in this tree — the
runner reported it as a failure until `make build-test` produced it, after which
its 28 tests pass. Pre-existing build gap, unrelated to this slice.

⚠ Paired A/B read **1.0063 geomean**, and re-measuring the 16 flagged rows at 15
pairs dissolved all but one — paraffins 1.129 → 0.990, matmul 1.147 → 0.965,
puzzle typed 1.086 → **0.926**, list typed 1.131 → **0.950**. The survivor,
`binarytrees untyped` at 1.054 with **0/15 wins**, is NOT a semantic change:
binarytrees declares no types and uses no occurrence/constrained/integer field,
and diffing its emitted MIR between the two binaries gives 42 differing lines,
**all 42 pure address constants** with an identical instruction stream. It is
C++ binary layout shifted by editing a widely-included inline header — the same
class of effect seen when `shape_field_name_equals` was added. Treat the slice
as neutral-to-enabling, not as a 0.6% regression.

### 12.6 G5 attempt: a regression I caused, and the pre-existing bugs it uncovered

**G5 is NOT landed.** Starting it surfaced two things that had to come first.

#### The regression (mine, now fixed)

`{v: integer}` map fields read back as `error` — introduced by the §11.5
adoption slice, shipped through **3857 passing tests**, and caught only by
probing `integer` fields directly while scoping G5.

Cause: I REPLACED the adoption test rather than extending it. `integer`
classifies ANY, so an `{v: integer}` contract is not storage-valid, so the new
gate refused adoption — and refusing pushes the literal through the declaration
boundary's **re-pack into that same malformed contract shape**, which is worse
than adopting it. Before the slice, that literal adopted via the VALUE-PROOF
path (`0n` proves `integer`).

Fix: adopt when EITHER the storage-valid shape match OR the original
all-values-proven test passes. The disjunction is a strict superset of the
pre-slice behaviour, so it cannot regress what used to adopt, while keeping the
new capability. Guard added: `test/lambda/proc/abstract_numeric_field.{ls,txt}`
— verified to FAIL (empty output) on the broken binary.

⚠ Lesson worth generalising: a gate that redirects work to a DIFFERENT path is
not conservative just because the gate itself is. Ask what the refused case
falls through to.

#### Pre-existing bugs found (all reproduce on `f46aae989`)

Abstract-numeric map fields are substantially broken, independent of this round:

| case | behaviour at HEAD |
|---|---|
| `{n: integer, label: string}` — multi-field | `type(n)` reports **`float`** |
| `{q: number, …}` | read produces nothing |
| `{n: integer}` holding a wide BigInt | `fn_string unhandled type: any` |
| `bg.v = bg.v + 1n` on an `integer` field | silently produces no output |
| `{v: decimal}` field | reads back `null` |

These are the exact failure class TB4/G5 addresses — an abstract contract in a
TypedItem slot, inside a contract shape whose stride does not match that slot.
G5 would likely fix several of them, which strengthens the case for it.

#### What G5 still needs

Beyond the classifier line: `set_field_value`'s DECIMAL arm is
`*(Decimal**)field_ptr = item.get_decimal()` — it assumes the Item already
carries a Decimal. `decimal_from_int64` is public but produces a FIXED-precision
decimal (`unlimited = 0`), and `item_type_is_integer_subtype` only accepts
`DECIMAL_BIGINT`, so converting through it would break `n is integer`. The
BigInt constructors (`bigint_push_result` and the `unlimited = DECIMAL_BIGINT`
site near `lambda-decimal.cpp:728`) are `static`. So G5 needs a public
int → BigInt-decimal conversion exported first, applied at BOTH store sites
(`set_field_value` and `map_field_store`), and it should be landed together with
fixes for the table above rather than on its own.

Gates after the fix: baseline **3858/3858**, test262 40261/40261 zero
regressions.

### 12.7 Abstract-numeric and pointer-lane field bugs — three of five fixed

Three defects, all pre-existing, all of the same family: **an admission that is
broader than the implementation it guards.**

**(a) The map-TYPE parser strode a flat `sizeof(void*)`.**
`parse_type_pattern.cpp` laid every field 8 bytes apart regardless of its
storage class, so a field following an `integer`/`number` slot began ONE BYTE
INSIDE it (`sizeof(TypedItem)` is 9). Dumped for
`type Counts = {n: integer, label: string}`: `n` a 9-byte ANY slot at 0,
`label` at **8**, `byte_size` 16 for 17 bytes of fields — the shape failing its
own `shape_entry_storage_fits_data`. Both stride sites now use
`type_info[type_field_storage_type_id(...)].byte_size`. This is **gap G3**, and
it turned out to live in ONE builder, not to need the fill/rebuild/COW audit
§11.3 anticipated.

**(b) The map-literal direct-store loop admitted more types than it stores.**
`all_direct` asked `mir_is_native_scalar_value_type`, which also admits SYMBOL,
BINARY, DECIMAL, DTIME and COMPLEX — none of which the store if-chain has a
branch for, so those fields **fell off the end and were never written**. An
annotated `{v: decimal}` read back as null. Admission now lists exactly the
classes the loop stores; the rest go through `map_fill`, whose
`set_field_value` covers every class.

**(c) The direct field read/write pair had the same asymmetry.** Its scalar arm
returns the raw 8 bytes — correct for the int/bool lanes and for STRING (whose
consumers re-tag), wrong for DECIMAL/DTIME/SYMBOL/BINARY/COMPLEX, which came
back untagged: `{v: decimal}` reported `raw_pointer`. `is_direct_access_type`
admits all of them, so it is too broad to gate that path; added
`mir_direct_field_access_type` and gated both read and write on it.

| bug | state |
|---|---|
| `{n: integer, label: string}` → `type(n)` was `float` | **FIXED** |
| wide BigInt in an `integer` field → `fn_string unhandled type: any` | **FIXED** |
| `{v: decimal}` → read back `null` / `raw_pointer` | **FIXED** (with symbol/binary/datetime/complex) |
| `{q: number, …}` — fails at CONSTRUCTION | **still broken**, pre-existing; `var m: M = {q: 1.5}` produces no output on the session-start binary too |
| `c.n = c.n + 1n` on an `integer` field | **still broken**, pre-existing |

Guard: `test/lambda/proc/abstract_numeric_field.{ls,txt}` — verified to fail on
the session-start binary, where it prints the overlap garbage directly
(`float -3.38461e+125 counts`, `3.91911e+202 wide`). Gates: baseline
**3858/3858**, test262 40261/40261 zero regressions. No benchmark A/B: the
corpus contains no map type declaring any of the reclassified field types
(grep-verified).

⚠ The recurring shape here is worth stating once: **three separate defects, and
all three were an admission predicate that was broader than the code it
guarded.** When adding a fast path, the gate and the switch must be derived from
one list.

**G5 remains blocked** on the two still-broken rows above plus the missing
public int → BigInt conversion (§12.6). Fixing `number` construction first is
the natural next step, since G5 moves `integer` off this slot entirely and the
`number` path will still be there.

### 12.8 `number` construction fixed; two build/link traps; G5 still open

**`number`-contracted map fields worked at no point before today.**
`var m: M = {q: 1.5}` aborted construction outright. ASAN named it precisely: a
**global-buffer-overflow** next to `TYPE_STRING`, from
`validate_against_base_type`.

Root cause: the validator's `unwrap_type` loops on
`type_id == LMD_TYPE_TYPE && kind == TYPE_KIND_SIMPLE` and dereferences
`((TypeType*)type)->type`. A compact global meta-type (`number`, `integer`,
`type`) matches that condition EXACTLY but carries only the two-byte `Type`
prefix — there is no payload to read. The runtime's own
`runtime_boundary_unwrap_type` already guards this with
`!type_is_global_meta_type`; the validator's copy did not. Guard added there,
plus a defensive arm at the top of `validate_against_base_type` (both its fast
and full arms dereferenced before checking).

`{q: number}` now stores each value in its ACTUAL carrier — `float 1.5`,
`int 7` — which is TB2's storage model, and `m.q = m.q + 1.0` gives 2.5.

**Still open (the 5th bug):** `c.n = c.n + 1n` on an `integer` field. The read
and the arithmetic are both correct (`integer 5` → `integer 6`); the STORE is
rejected: `validator at .v: Expected type 'type', but got 'decimal'`. The
validator's numeric lattice is TypeId-only and BigInt has no tag of its own
(`Decimal*` + `unlimited == DECIMAL_BIGINT`), so it cannot see it. A fix was
attempted in `validator_numeric_item_embeds` and did NOT take — the reported
target displays as `type`, not `integer`, so the failing comparison is not the
one patched. Needs tracing from the map-field admission path, not guessing.

#### ⚠ Two traps that cost more than the fixes

**(a) A recursive `static inline` in a widely-included header.** G2b's
constrained-type case was written as a recursive call to
`type_field_storage_type_id`. Recursion defeats inlining, so the compiler emits
an out-of-line copy in every TU including `lambda-data.hpp`, and that copy
pulled a symbol into two test binaries that do not link it —
`test_binary_storage_gtest` and `test_compiler_pass_gtest` failed to **LOAD**
with `symbol not found in flat namespace '_ItemError'`. Not a compile error, not
a test failure: a dyld failure at startup, which the runner reports only as
"no valid JSON output". Unwound to a bounded loop; both pass.
**Rule: a header-only classifier on the hot path must stay inlinable.**

**(b) The pre-existing `node_trace_events` link gap.** `make test-lambda-baseline`
began failing to LINK — `node_trace_events_init` etc. undefined — from
uncommitted work already in the tree at session start
(`lambda/module/node_core/node_trace_events.cpp` is untracked, and the modified
`jube_registry.cpp` calls it). The file was registered in the main source list
but not in the `node-core` library target, and the two targets that link
`lambda-rt` without `node-core` could not resolve it. Fixed in
`build_lambda_config.json` (rule 7): added the source to `node-core`, and
`node-core` to exactly the two targets whose link failed. ⚠ A blanket edit
adding `node-core` to all six `lambda-rt` dependents BROKE two other binaries
the same dyld way — scope the dependency to the targets that actually need it.

Gates: baseline **3858/3858**, test262 40261/40261 zero regressions,
splay_deep still 135 ms.