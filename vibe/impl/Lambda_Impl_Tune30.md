# Lambda Impl Proposal: Tune30 — The Codegen-Bound Class

- **Date:** 2026-09-17 (rev 2: pivoted to the codegen-bound class; rev 1
  recorded the middle-band profile, now §6)
- **Status:** IN PROGRESS (round 1 landed, §9; round 2 landed, §10 — the
  index-sentinel fix LR12-21 and T30-5 leaf inlining; queens 5.5x → 3.5x,
  spectralnorm 4.5x → 3.5x, T30-4 loop-counter intervals: fft 4.6x → 3.9x,
  paraffins 4.1x → 3.4x, T30-2 store facts: permute 3.8x → 3.3x, argument
  intervals into inlined leaves: spectralnorm 4.5x → 2.25x; no row at the 2x
  gate yet, nbody 2.4x and spectralnorm 2.25x are closest). Issue audit in
  §11: LR12-19/20/21/23 fixed, LR12-15/16/18/22 closed as symptoms of the new
  consolidated **LR12-24**, whose declaration and reassignment classes were
  then implemented in §13 — a defect now crosses a native return on the error
  lane instead of arriving as `inf`/`nan`, at no measurable runtime cost; the
  other origination classes stay open, and §11.3 carries two corrections §13
  made to it. Follows
  `impl/Lambda_Impl_Tune29.md`, which closed with its three §19.1 items
  implemented (§20).
- **Formal authority:** **S4.1.2 / S4.2.3** (int saturation to the shared
  inf; nan is unequal), **S7.1.3v2** (an out-of-range read yields null),
  **D2.4.1–D2.4.3** (native lanes and their null sentinels), **D2.5.3**
  (flow-sensitive use of an index read's payload), **D3.3.3v3** (array
  certificates), **D4.3.1** (headers never move; the data zone compacts, so
  a data pointer is reloaded after any allocation point), **D4.4.4v4** (a
  handle carries facts valid while the place is unchanged), **D3.2.6** /
  **S11.4.10** (verified once, valid while unchanged), S9.1.3 / S9.2.2
  (plain params snapshot; a `var` borrow un-shares first), D5 (precise
  rooting through the side stack).
- **Vibe authority:** `impl/Lambda_Impl_Tune29.md` §17–§20;
  `impl/Lambda_Impl_Tune28.md` §9.7 (null-sentinel receiver arm), §9.17
  (emission diet); `impl/Lambda_Impl_Tune27.md` §10.10, §10.15 (float null
  lane spellings measured), T27-9 (literal-bounded dense loops);
  `Lambda_Design_Runtime_COW.md` §11.14 and Appendix D.
- **ID series:** `T30-#` for this round's tracks; `T30-C#` for the
  runtime-call clusters carried in §6.

## 1. Objective

Seven typed rows spend ≥94% of their time in emitted MIR code, call almost
nothing in the runtime, and are still 3.5–8x slower than the same program
through MIR's C frontend (§3). No runtime elision can reach them; the gap is
the instructions the transpiler emits per array read, per float operation,
per store and per call. This round is about those instructions.

**Gate for the round: every one of the seven ≤ 2x C2MIR**, same-run,
release, `LAMBDA_TIER=jit`:

| row | typed (ms) | C2MIR (ms) | now | target |
|---|---:|---:|---:|---:|
| nbody | 13.03 | 1.70 | 7.7x | ≤ 3.4 ms |
| fft | 0.188 | 0.028 | 6.7x | ≤ 0.056 ms |
| queens | 0.132 | 0.022 | 6.0x | ≤ 0.044 ms |
| spectralnorm | 2.01 | 0.381 | 5.3x | ≤ 0.76 ms |
| paraffins | 0.251 | 0.055 | 4.6x | ≤ 0.11 ms |
| quicksort | 0.861 | 0.220 | 3.9x | ≤ 0.44 ms |
| permute | 0.105 | 0.029 | 3.6x | ≤ 0.058 ms |

Secondary: no row of the suite slower than before; typed ≤ untyped on every
row; tier parity; forced GC clean.

## 2. Result46: the distribution, not the geomean

Result46 (63 rows, every row has a C2MIR port): typed/C2MIR geomean 4.26x,
median 4.6x, bimodal by workload kind:

| typed / C2MIR | rows | what is there |
|---|---:|---|
| ≤ 2x | 19 | scalar arithmetic and recursion: fib, tak, ack, sum, sumfp, matmul, collatz, mandelbrot, diviter, divrec, binarytrees, gcbench, regexredux, primes, navier_stokes, ray |
| 2–5x | 14 | mixed scalar/array: fannkuch, sieve, mbrot, deriv, triangl, quicksort, permute, storage, bounce, fasta, paraffins, revcomp, text_search |
| **5–20x** | **25** | record- and array-heavy: nbody, raytrace3d, crypto_sha1, brainfuck, levenshtein, json, towers, hashmap, richards, nqueens, splay, prettier, base64, cube3d, knucleotide, … |
| > 20x | 5 | microdiff 24x, havlak 27x, deltablue 32x, cd 37x, hyphen 55x (spec-invalid) |

Result46 was ~13% load-inflated on every engine (Tune29 §2.1); same-run
ratios are valid, absolute numbers are not comparable across runs.

## 3. The codegen-bound class

### 3.1 How it was found

Every typed row was wrapped in a repeat loop to ~2 s (`temp/t30/prof/`,
generic wrapper: the timed body runs N−1 times in a `while` and once more
unwrapped, so later declarations keep their scope), run under the symbolized
release with `LAMBDA_MIR_LOG_CODE_ADDR=1`, sampled 4 s at 1 ms, and
attributed (`temp/t29/prof/attribute.py`; full table
`temp/t30/prof/jit_self_table.txt`). Sorting by JIT-self share: 25 of 60
rows are ≥94% emitted code. Eighteen of those are the ≤2x band, which is
what a fast row looks like. Seven are ≥94% emitted code and still 3.5–8x:

| row | typed/C2MIR | JIT self | the hot loop |
|---|---:|---:|---|
| nbody | 7.7x | 99% | seven `var float[]` parameters indexed by `i`, `j`; `math.sqrt` |
| fft | 6.7x | 98% | one `float[]` indexed by `ii`, `jj`, `jj + 1` with data-dependent bounds |
| queens | 6.0x | 97% | three `bool[]` indexed by `r`, `c + r`, `c − r + 7` through two tiny `pn`s, in recursion |
| spectralnorm | 5.3x | 100% | `float[]` read per element, `eval_A(i, j)` per element |
| paraffins | 4.6x | 94% | `int[]` reads, three nested counters, `ms2/ms3/ms4` per term |
| quicksort | 3.9x | 100% | `int[]` compare-and-swap through a `var` parameter |
| permute | 3.6x | 100% | `int[]` swap through a `var` parameter, in recursion |

Nothing else in the suite has this shape: every other row over 3x has a
runtime helper in its top three (§6).

### 3.2 Static accounting: emitted hot path against the C port

The C ports were compiled with the same frontend (`lambda/mir/c2m -S`,
dumps in `temp/t30/cmir/`); the typed dumps are in `temp/t30/mir/`.
`hotcensus.py` splits each function into basic blocks and excludes the
cold ones (blocks that call a type check, a boxing helper, a checked store,
an overflow slow path) and the nan arms (blocks that materialize the float
null sentinel); what remains is what runs when every guard passes.

| function | Lambda hot | of which cold | nan arms | C2MIR | hot / C | time |
|---|---:|---:|---:|---:|---:|---:|
| nbody `advance` | 1784 | 1102 | 731 | 103 | 17x | 7.7x |
| fft `four1` | 1059 | 276 | 236 | 124 | 8.5x | 6.7x |
| queens `place_queen` + `get_row_column` + `set_row_column` | 439 | 339 | 0 | 53 (both inlined) | 8.3x | 6.0x |
| spectralnorm `mul_Av` + `eval_A` | 363 | 127 | 43 | 29 + 12 | 8.9x | 5.3x |
| paraffins `count_ccp` | 601 | 127 | 0 | 112 | 5.4x | 4.6x |
| quicksort `partition` | 437 | 183 | 0 | 32 | 13.7x | 3.9x |
| permute `swap` + `permute` | 224 | 106 | 0 | 9 + 21 | 7.5x | 3.6x |

Static hot count tracks time to within a factor of two (the census counts
the whole function, the loop bodies are the part that runs). C2MIR is the
fair bound: same register allocator, same code generator; the difference is
entirely what the Lambda front end emits.

### 3.3 The five mechanisms

Read from the dumps, with the census columns that measure each.

**M1 — Nullable reads poison the whole expression tree.** A typed-array
read whose index is not proven in range yields `T?` (S7.1.3v2: out of range
is null). On the float lane null is a NaN payload (`FLOAT_LANE_NULL_BITS`),
so every arithmetic operation with a nullable operand goes through
`emit_nullable_float_arith`: the operand's bits are stored to the Context
scratch slot, reloaded as an integer, compared with the sentinel, OR-ed into
`has_null`, branched on, and a nan arm materialized — then the result is
nullable too, so the next operation repeats it. In `advance`, one
`bx[i] − bx[j]` is 46 instructions on the hot path where C has three. The
dense-proof machinery (T27-9, `typed_array_inbounds_guard`,
`emit_dense_guarded_float_arith`) exists for exactly this, but its extent
proof covers only the counted loop's own counter: `advance`'s guard proves
`i`, so `bx[i]` is a plain load under it, while every `[j]` read
(`j = i + 1 … 4`) stays bounds-tested and nullable and poisons every
expression it touches. Census: `advance` 731 nan-arm instructions plus
~300 sentinel tests on the hot path; `four1` 236; `mul_Av` 43. Affects
nbody, fft, spectralnorm, and (on the int lane, where the sentinel is
`INT_LANE_NULL`) queens' `bool[]` reads and paraffins' `rcount[…]`.

**M2 — Per-store guard sequences.** A store through a `var T[]` parameter
is emitted inline as: band test on the value (S4.1.2, `add …, 2^53−1;
ubgt …, 2^54−2`), share-bit load and test (CW32v2), length load, two
bounds tests, then the scaled store — twelve instructions plus a cold arm
that boxes the value and calls `lambda_array_set_checked_inplace_lane` /
`array_num_set_cow_idx`. `partition`'s two stores per iteration are 2 × 12
hot plus 2 × 25 cold; `swap`'s two stores are 84 hot instructions against 9
in C. The value being stored was read from the same certified `int[]` one
line earlier, so its band is already proven; the share bit and the length
of a `var` parameter are facts (D4.4.4v4) that cannot change between two
stores with no call between them.

**M3 — Root spills and layout reloads around leaf calls.** `math.sqrt` in
`advance` is emitted as a MAY-GC call: nine root registers are written back
to the side-stack frame before it, nine reloaded after it, and the data
pointers of the seven arrays reloaded after that (D4.3.1) — twenty-five
instructions around a one-instruction call, once per inner iteration.
`sqrt`/`sin`/`cos`/`floor` are libm leaves: they allocate nothing and
re-enter nothing. Census: `advance` 243 spills, 754 reloads, 304 memory
operations on the hot path for a body whose C form has none.

**M4 — S4.1.2 band tests on every int operation.** `eval_A` is `1.0 /
float((i + j) * (i + j + 1) / 2 + i + 1)`: eight band-test pairs, each with
a slow-path call, and `(i + j)` computed twice — 59 hot instructions against
12 in C. `count_ccp` has 67 band tests for three nested counters whose
bounds are `nc1 * 4 <= m` with `m = n − 1 ≤ 20`. Every operand here is
bounded by a loop condition or a small constant; the tests fold under an
interval proof the emitter does not yet do.

**M5 — Small `pn`s are not inlined.** `eval_A` per element, `ms2/ms3/ms4`
per term, `get_row_column`/`set_row_column` per placement, `swap` per
permutation step, `lcg_next` per element. Each call costs the call itself,
the spills and reloads of M3, the callee's entry witness test on every
array parameter, and the return boundary — and blocks every proof in M1/M2
from crossing the call. The C ports inline all of them (`place_queen` is
53 instructions with both helpers inside).

One more, not a mechanism but a consequence: every counted loop is emitted
twice (proven body and fallback body with per-access checks), behind a
25-instruction proof prologue. Once M1's proof is stronger the fallback is
cold code; the prologue is per call and only matters for the recursive rows.

## 4. Tracks

### T30-1 — Index-range proofs make reads total (M1)

Extend the extent proof from "the counted loop's own counter" to every
index expression that is an affine function of bounded counters: nested
`while` counters with an inductive bound (`j = i + 1; j < 5`), `c + r` and
`c − r + 7` with `r, c ∈ [0, 8)`, `jj + 1` and `ii + mmax` with `mmax < n`,
`nc4 = remain − nc3`. The proof needs one runtime fact per array per loop —
`len ≥ bound` — hoisted to the loop head (or the function entry for a `var`
parameter, D4.4.4v4: the length is a fact until a call that can touch the
array). Under the proof an element read is one load, and it is **not
nullable** (S7.1.3v2's null cannot occur), so the whole expression tree
stays on the plain lane: no sentinel spill/reload/compare per operation, no
nan arm. Rows: nbody, fft, spectralnorm, queens, paraffins, quicksort,
permute. Expected: `advance` hot 1784 → ~600; `four1` 1059 → ~450.

Gate fixture: `tune30_range_proof` — one probe per index shape above, the
`.mir-check` forbidding `FLOAT_LANE_NULL_BITS` compares inside the loop
body, and the golden showing the out-of-range case still yields null when
the proof cannot hold (a parameter shorter than the bound).

### T30-2 — Store facts hoisted per loop (M2)

A store through a certified `T[]` needs: the value in band, the array
unshared, the index in range. Fold each: the value read from a certified
lane of the same element type is in band (D3.3.3v3); the share bit of a
`var` parameter or a local is tested once at loop entry and re-tested only
after a call that can share it (Appendix D's kill rows); the index is
T30-1's proof. The store becomes the scaled `mov`. Rows: quicksort,
permute, fft, nbody (the six stores per inner iteration). Expected:
`partition` hot 437 → ~120; `swap` 84 → ~15.

### T30-3 — Leaf native calls are not allocation points (M3)

Register the libm leaves the sys-func table reaches by native pointer
(`sqrt`, `sin`, `cos`, `floor`, `fabs`, `pow`, …) with NO_GC / REENTRY_NO
metadata and put them on the audited list (Tune29 §20.4's mechanism), so
`emit_call` neither spills roots before them nor reloads layouts after
them. Rows: nbody (25 instructions per inner iteration), fft (`sin`), and
every float row. This is the cheapest track and should go first.

### T30-4 — Band-test folding and CSE (M4)

An interval per int lane value: loop counters from their bounds, sums and
products of bounded intervals, array lengths from certificates. An
operation whose result interval is inside the band (2^53) emits no
`add/ubgt` pair and no slow arm (S4.1.2 is preserved: the proof shows
saturation cannot occur). Repeated checked subexpressions (`i + j` twice in
`eval_A`) share one lane value. Rows: paraffins (67 tests in `count_ccp`),
spectralnorm (`eval_A` 12), fft (19), queens `is_valid` (18). Expected:
`eval_A` 59 → ~14.

### T30-5 — Leaf `pn` inlining (M5)

Inline a `pn` whose body is a single statement list with no loop, no
`raise`, no handler, no nested function, no `var` rebinding of a parameter,
and fewer than N instructions, at a direct call site. Semantics are those
of the call: a plain parameter the callee writes still snapshots (S9.1.3),
a `var` parameter still writes through (S9.2.2); the inlined body's
boundaries become T30-1/T30-4 proofs of the caller. Rows: spectralnorm
(`eval_A`), paraffins (`ms2/ms3/ms4`), queens (`get_row_column`,
`set_row_column`), permute (`swap`), quicksort (`lcg_next`). No ruling
covers inlining; it is an implementation choice that must stay observably
identical, so the gate is tier parity on every fixture that names a
parameter effect.

### T30-6 — Proof prologue in the caller (the consequence)

When the argument is a witnessed parameter of the caller, the callee's
25-instruction witness/layout/certificate prologue repeats a proof the
caller already holds. Pass the proof (the `_array_witness` bits already
exist for this) and skip the prologue. Rows: the recursive ones
(`place_queen`, `permute`, `quicksort`, `count_radicals`).

Order: T30-3 (a registry edit), T30-1, T30-2, T30-4, T30-5, T30-6.
T30-1 and T30-2 are the round; the others are what remains after them.

## 5. Budget per row

Using the §3.2 ratio (static hot ≈ 2× time ratio), the 2x gate needs each
hot function at or under ~4x its C count:

| function | hot now | C | budget (≤4× C) | tracks that pay |
|---|---:|---:|---:|---|
| `advance` | 1784 | 103 | 410 | T30-1 (nan arms, sentinels), T30-3 (sqrt), T30-2 (stores) |
| `four1` | 1059 | 124 | 500 | T30-1, T30-4, T30-2 |
| queens trio | 439 | 53 | 210 | T30-5 (inline), T30-1, T30-2 |
| `mul_Av` + `eval_A` | 363 | 41 | 165 | T30-5, T30-4, T30-1 |
| `count_ccp` | 601 | 112 | 450 | T30-4, T30-1, T30-5 |
| `partition` | 437 | 32 | 130 | T30-2, T30-1 |
| `swap` + `permute` | 224 | 30 | 120 | T30-2, T30-5, T30-6 |

Re-run `hotcensus.py` after each track; a track that does not move its
column is reverted, as Tune29 did with T29-7a.

## 6. The runtime-call clusters (carried, not this round)

Rev 1 of this document profiled thirteen 5–20x rows whose time is in
runtime helpers (`temp/t29/prof/mid/`). Kept here as the next round's
evidence:

| cluster | rows (share of samples) | the mechanism |
|---|---|---|
| **A** checked boundaries on already-proven values | raytrace3d 47%, towers 33%, crypto_sha1 32%, json 31%, list 26%, cube3d 18%, hashmap 14% | `lambda_type_check` on `float[]`/`int[]` arguments and returns that came from certified fields or same-contract bindings; `int` returns of nullable typed reads admitted through the numeric ladder instead of one null test; `match { case error … case float }` as two `fn_is`; member-to-union returns walked |
| **B** COW stores and copies | puzzle 74%, richards 55%, splay 46%, hashmap 39%, json 21% | `int[]` cloned on store while a place copy is alive (D4.4.6); record path store as one `lambda_map_path_set_checked_fixed` per write; `var` path borrow re-navigating the spine with `fn_index` per call |
| **C** typed-array allocation | cube3d 46%, storage 71%, brainfuck 51%, nqueens 49%, raytrace 15%, splay 16% | `fn_fill` temporaries and `float[]` results allocated per call (Tune29 T29-5 item 2, closed there on cube3d's 25%, wider than that) |
| **D** strings | json_gen 86%, levenshtein 54%, base64 46%, revcomp 85% (`fn_replace`), regexredux | `fn_strcat_many` copies, `fn_string`, string `fn_eq_depth`, per-char `fn_string_ascii_at` (Tune22's territory) |
| other | pnpoly `fn_ne` 45%; bounce `is_truthy` 21%; fasta `fn_index`+`fn_lt` 47%; pidigits `fn_numeric_binary` 96%; binarytrees/deriv/gcbench `heap_calloc_class` ~70% (allocation-bound by construction, at parity) | |

Tracks, to be scheduled after §4: **T30-C1** boundary elision from
carried proofs (four mechanisms: certified-field array arguments and
returns incl. raising callees; native null test on scalar returns of
typed reads; inline tag test for two-member `error` unions; member-to-union
returns without admission, D8.3.2–D8.3.3); **T30-C2** handle-relative
direct record stores (D4.4.4v4, Appendix D kill table, richards/splay);
**T30-C3** puzzle's shared `int[]` traced to its bind before any code;
**T30-C4** borrow spine cached per call (hashmap); **T30-C5**
caller-provided storage for `fill`/`float[]` results (cube3d, storage,
nqueens, brainfuck). T30-1's range proofs and T30-2's store facts also
remove Cluster A's typed-read admissions in towers and part of B's checked
stores, so the clusters should be re-profiled after §4 lands before these
are sized.

## 7. Gates

- The seven rows of §1 at ≤ 2x C2MIR, same run, release, `LAMBDA_TIER=jit`,
  N=7 interleaved (`temp/r46/ab.py`); the §5 hot counts re-censused after
  each track.
- No row of the suite slower than its pre-round time; typed ≤ untyped on
  every row.
- `make test-lambda-baseline`; corpus JIT output diff against v46; forced
  GC (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1
  LAMBDA_ROOT_WITNESS=1`) on every new fixture; ratchet re-baselines with a
  stated reason (the range proofs and hoisted store facts should *shrink*
  cube3d, deltablue and nqueens budgets, not grow them).
- Tier parity on every fixture: T0 keeps per-access checks, and every
  proof-elided read or store must print what T0 prints, including the
  out-of-range null and the shared-array copy.
- No ruling changes. S4.1.2, S7.1.3v2, D4.3.1 and D4.4.4v4 are the four
  most likely to be brushed; a track that needs any of them changed stops
  and asks.

## 8. What Tune29 taught, and where it stops

- "Calls and allocations move time; guards do not" (Tune29 §13.2, §16.6,
  §18) held on rows whose time was in runtime helpers. This class has no
  helpers left, so the instructions are the cost; §3.2's static counts track
  the time ratios within 2x, and Tune27 §10.15 already measured one
  spelling of the null test at ±10–24% on nbody, matmul and fft. The lesson
  is bounded, not wrong.
- Measure the seven rows and the whole suite, not three rows. The generic
  repeat wrapper and `hotcensus.py` make both cheap.
- Re-profile on the current binary before starting a track: Tune29 §20.2's
  first claim was already stale when the track began.
- The C2MIR dump is the specification of the target: for every hot
  function there is a C form with the same semantics whose instruction
  count is the floor. Read it before emitting.

## 9. Implementation log — round 1 (2026-09-18)

Same-machine ratios (release, `LAMBDA_TIER=jit`, min of 7, C2MIR measured
the same run with `temp/t30/ratio.py`):

| row | before | after round 1 | gate |
|---|---:|---:|---:|
| nbody | 7.75x | 2.39x | 2x |
| fft | 6.47x | 4.58x | 2x |
| queens | 5.44x | 5.48x | 2x |
| spectralnorm | 5.04x | 4.51x | 2x |
| paraffins | 4.20x | 3.84x | 2x |
| quicksort | 3.94x | 3.68x | 2x |
| permute | 3.61x | 3.65x | 2x |

Suite-wide A/B against the Tune29 binary (`temp/t30/suite_ab_g.txt`): no
output differs; geomean 0.959 over 60 rows. Largest moves: nbody 0.33,
navier_stokes 0.74, fft 0.73, ray 0.77, spectralnorm and matmul 0.88,
primes 0.89, cube3d 0.92. splay (+4%) and brainfuck (+3%) are slower from
the first Tune30 binary on. None of the round's changes is on splay's path,
and that binary also includes the upstream merge of 2026-09-17 23:01, so the
cause is unattributed.

### 9.1 What landed

- **T30-3, libm leaves are NO_GC.** `JIT_LIBM_LEAVES` in
  `sys_func_registry.c` feeds both the import rows (`JIT_IMPORT_PURE_SCALAR`)
  and the NO_GC audit. `sqrt` in `advance` lost its 9 spills, 9 reloads and
  7 data-pointer reloads.
- **T30-3b, GC-free bodies.** `mir_narrow_body_gc_effect` runs after a body
  is emitted. If every call in the body targets a NO_GC/REENTRY_NO import,
  itself, or an earlier GC-free body, the body variants are marked
  `may_gc=false`. Direct calls compiled afterwards emit no spills and no
  reloads. The side-stack ensure and the stack-overflow raise are admitted:
  the reservation never moves, and the raise leaves through the recovery
  frame. Now GC-free: `eval_A`, `ms2/3/4`, `lcg_next`, `fib`, `tak`.
  `swap` and `get_row_column` are not, because their cold arms call the
  checked store and the type check.
- **T30-1, whole-loop versioning.** When a condition loop has a dense guard,
  it branches once at entry on the read guard. The fast copy
  (`mir_emit_while_copy`) runs with `typed_array_inbounds_assumed`: proven
  reads are plain loads and never null (no sentinel test per operation, no
  nan arm). Its dense stores are unconditional when the store guard equals
  the read guard (`typed_array_dense_store_assumed`). The fallback is the
  unversioned loop. Inside a finite plan's fast arm, the fallback is the
  plan's existing generic sibling (`loop_generic_entry`), so no third copy is
  emitted. Loops with an accumulator plan and async procs keep one copy.
  `MirCowJoin` joins the two copies' facts.
- **T30-1, nested counters.** `mir_int_init_known_positive` marks
  `var j: int = i + 1` as positive when `i` is a non-negative counter of an
  enclosing loop, so the inner loop records `j` and proves `bx[j]`.
- **T30-1c, local tree versioning.** A float tree whose only possible nulls
  are reads from proven `T[]` bindings checks each read's index once
  (`ult index, cached length`), then evaluates a copy of the tree with those
  reads as plain loads. The original lowering is the fallback.
  `mir_note_float_null_flag` records the merged result's null flag, so a
  consumer's null test is a register test instead of the Context scratch
  round-trip. This took fft from 6.6x to 4.6x.
- **Defects fixed on the way:** LR12-19 (literal extent `-1`, then the
  folded `mulo`/`bo` flag) and LR12-20 (dense proof leaking into the
  fallback arm). The fixture is `test/lambda/proc/dense_loop_short_array.ls`.
  LR12-18 (native float return drops an error) was ledgered and left open.

### 9.2 Tried and removed

- **Offering direct calls to MIR's inliner as `MIR_INLINE`** (all calls, then
  only calls to GC-free leaves) moved nothing, and the first form slowed nbody
  and spectralnorm. The reason: `process_inlines` skips every caller larger
  than `MIR_MAX_CALLER_SIZE_FOR_ANY_GROWTH_INLINE` (200 instructions), since
  its growth test is already true at the start. Every Lambda function with a
  loop exceeds that. Raising the limit means building the vendored MIR with
  different defines, which also changes the `c2m` reference, so it is left to
  the user (CLAUDE.md rule 16).

### 9.3 Corrections to §3

- **§3.2's "hot" counts are too high.** `hotcensus.py` counted both
  versions of every tree the dense guard versions. The instruction-level
  profiles (`temp/t30/prof2/pchist.py` over PCs, with the machine code taken
  from the live process through lldb, `hotpc.sh`) are the reliable view.
- **M1's cause in nbody was not only the unproven `j`.** The dense guard of
  every literal-bounded loop was false at run time (LR12-19's folded `bo`),
  so `advance` always ran the checked copy. Fixing that alone took nbody from
  8.9 ms to 3.7 ms in the debug build.

### 9.4 What the remaining gap is (profiles in `temp/t30/prof2/`)

- **nbody (2.4x):** the fast copy is close to the C shape. What remains is a
  store-guard branch per store (the store guard includes T26-4's owner
  proof), per-store header loads of the data pointer, and register pressure:
  `advance` is ~4k MIR instructions with its fallback, so hot values live in
  stack slots.
- **spectralnorm (4.5x):** `eval_A` is 77% of samples. Its prologue saves 7
  callee-saved registers, it has 8 band-test pairs each loading two 64-bit
  constants, and there is no inlining (see §9.2). Needs T30-4 (band folding
  from caller ranges) plus an inliner that MIR's size rule does not block.
- **quicksort / permute / queens / paraffins (3.7–5.5x):** int trees. Store
  guards (band test on a value read from the same lane, owner bit, length),
  `arr[i]` with `i` not a counter, and calls to `swap`/`get_row_column` that
  are not GC-free. The int analogue of T30-1c and T30-2's hoisted store facts
  are next. queens' hot loop is a `for` loop, which has no dense proof at
  all.
- **All rows:** function size drives register pressure. Every versioning
  step adds copies, so the next steps should shrink the fallbacks, for
  example by sending a cold arm to a shared checked tail.

## 10. Implementation log — round 2 (2026-09-18)

| row | round 1 | round 2 | gate |
|---|---:|---:|---:|
| nbody | 2.39x | 2.38x | 2x |
| fft | 4.58x | 4.61x | 2x |
| queens | 5.48x | 3.49x | 2x |
| spectralnorm | 4.51x | 3.48x | 2x |
| paraffins | 3.84x | 4.06x | 2x |
| quicksort | 3.68x | 3.69x | 2x |
| permute | 3.65x | 3.80x | 2x |

Suite A/B round 1 → round 2 (`temp/t30/suite_ab.py`, 60 rows): no output
differs, geomean 0.986. The rows that moved are the two the round targeted;
paraffins and permute are within this suite's run-to-run spread (their
callees were not inlined, see below).

### 10.1 The index sanitizer (correctness first)

The instruction profile of `get_row_column` (`temp/t30/prof2/q3_grc.*`,
50% of queens' samples) showed the band test and poison select of
`mir_sanitize_native_index` on every subscript. Reading it turned up a
defect, ledgered as **LR12-21**: a leaf was admitted into the index sum
without any check, so two lane sentinels could wrap into a valid index
(`a[inf + -inf]` read `a[0]`; T0 yields null). It is present in v46.

The fix inverts where the proof sits. Each leaf that
`mir_int_lane_operand_proven_in_band` does not already prove carries the
exact test `(v >> 53) + 1 <=u 1` — three instructions, no 64-bit constant,
and it rejects every sentinel because a legal lane value is in
`[-(2^53-1), 2^53-1]` and every sentinel is far outside it. With every leaf
in band a sum cannot wrap (it would take more than 1024 terms), so the band
test on the *result* is gone, and the poison is a two-instruction
`(value | mask) >>u shift` yielding `INT64_MAX`. The poison must be
`INT64_MAX` rather than `-1`: a consumer holding a nonnegative index proof
skips its `< 0` test, and only `>= len` is left to reject it.

Cost: +267 module instructions on cube3d (the leaf checks), timing neutral.
Regression: `test/lambda/proc/index_sentinel_sum.ls`.

### 10.2 T30-5, leaf inlining on the Lambda side

MIR's inliner is unusable here (§9.2), so a direct call to a small leaf now
emits the callee's body in the caller. Inlining has no ruling of its own, so
the admitted shape is narrow enough to stay observably identical to the call:

- **Callee:** a local `fn`/`pn` with no captures, not variadic, not
  `can_raise`, no binders, no defaults or optional parameters, a native
  scalar or `any` result, and a body of `if`/`return`/store statements over
  expressions that cannot allocate or raise. Element reads are admitted only
  in conditions (S7.1.3v2's null is what the `if` consumes), never as a
  returned value; `/` needs a float divisor or a nonzero int literal.
- **Call site:** every scalar argument already carries the parameter's lane
  and is neither null nor an error, and every array argument is a caller
  binding whose certificate is invariant-equal to the parameter's contract
  (`lambda_array_contract_compatible`).
- **Parameters:** a scalar binds to the argument's lane register. An array
  parameter *borrows the caller's binding* — the caller's `MirVarEntry` is
  re-pointed at the parameter's `NameEntry` for the body — so reads reuse the
  caller's proven layout and a `var` parameter's store lands on the caller's
  own entry. That is exactly the write-through the call performs (S9.2.2),
  including the copy-on-write detach of a shared array.
- **Exits:** `return` moves its value to the join register and jumps to the
  join label (`MirInlineFrame`). A store can fail, and a direct call merges a
  callee's error lane into the call's *value* before the caller sees it, so
  an error exit inside an inlined body becomes that same value at the join.
  A native scalar lane cannot carry an error, so store statements are
  admitted only for an `any` result, and `emit_function_return` aborts the
  build if an error path is ever reached in a scalar-result body.
- **Context:** the caller's dense-loop assumptions are cleared for the body
  (they were proved over the loop's own reads, not the callee's), and
  `func_body` points at the callee body so the binding scans see the writes.

Inlined in the seven rows: `eval_A` (spectralnorm, 77% of its samples),
`get_row_column` and `set_row_column` (queens). Not inlined:
`swap` (permute) and `bump`-shaped bodies, because the stored value is an
element read that may be null and would reach the setter's error arm;
`ms2`/`ms3`/`ms4` and `lcg_next` are not yet covered.

`tune15_native_scalar_call.mir-check` now asserts the stronger property (no
call to the leaf remains at all); the cube3d budget was re-baselined once for
both changes. Regressions: `test/lambda/proc/inline_leaf.ls` (returns,
conditions over out-of-range reads, sentinel arguments, nesting) and
`test/lambda/proc/inline_leaf_store.ls` (write-through, a shared array's
detach, a `var` caller passing its own `var` parameters, an out-of-range
store).

### 10.3 T30-4, intervals for more loop counters

The instruction profiles showed where the band tests still came from: a
counter whose loop shape the T27-9 finite plan did not recognise has no
interval, so every operation on it pays `add 2^53-1; cmp; b.hi` (and, since
§10.1, a three-instruction leaf check per index). The plan only supplies an
entry guard and the facts; the fast arm re-emits the loop's own condition, so
widening the *shape* it recognises is sound as long as the counter still
cannot run past the bound:

- **Affine conditions.** The left side may be any sum of non-negative terms
  containing the counter with a positive coefficient: `nc1 * 4 <= m`,
  `nc1 + nc2 * 3 <= m`, `nc3 * 2 <= remain`. Every other term must already
  carry a non-negative fact and stay unwritten in the body, so the bound still
  bounds the counter. paraffins' `count_ccp` went from 67 band tests to a
  shift and a compare; the row moved 4.06x → 3.38x.
- **Affine updates.** The update may add several bounded terms, not just one
  binding or `+ 1`: fft's `ii = ii + mmax + mmax`. Each added binding is
  guarded to `[0, STEP_MAX]` at entry, and the counter's fact widens by the
  headroom one iteration can add.
- **Scaling updates.** `mmax = mmax * 2` is bounded too — by the bound times
  one more factor — so a doubling counter earns a fact.
- **Derived locals.** A local declared from an expression with a known
  interval, which nothing else writes, keeps that interval (`var jj: int = ii
  + mmax`). The declaration dominates its reads and the loop re-establishes
  it each iteration.

fft 4.61x → 3.89x, paraffins 4.06x → 3.39x. Suite A/B over the whole track:
geomean 0.992, no output differs, no row worse than 1.02 (noise at this
scale). `prettier_ast` grew 15 instructions from one extra entry guard and is
unchanged in time; re-baselined.

| row | round 1 | after §10.2 | after §10.3 | gate |
|---|---:|---:|---:|---:|
| nbody | 2.39x | 2.38x | 2.40x | 2x |
| fft | 4.58x | 4.61x | 3.89x | 2x |
| queens | 5.48x | 3.49x | 3.63x | 2x |
| spectralnorm | 4.51x | 3.48x | 3.51x | 2x |
| paraffins | 3.84x | 4.06x | 3.39x | 2x |
| quicksort | 3.68x | 3.69x | 3.66x | 2x |
| permute | 3.65x | 3.80x | 3.80x | 2x |

(The C2MIR column is re-measured each run; queens' and permute's moves are
that reference drifting, their own times are 0.062 ms and 0.091 ms
throughout.)

Regression: `test/lambda/proc/finite_loop_shapes.ls` covers each new shape
plus the values that must leave the guarded arm (a step and a bound past
`STEP_MAX`, a negative bound, an empty range). Writing it turned up two
pre-existing tier divergences, ledgered as **LR12-22** (a raised E201 inside
a JIT function yields `inf` to the caller instead of propagating) and
**LR12-23** (a doubling accumulator saturates on T0 but not on the JIT);
both reproduce on the Tune29 binary, and the probe is kept at
`temp/t30/h/tier_divergence_probe.ls`.

### 10.4 T30-2, store facts

A store through a certified `T[]` needs three facts. Two were already hoisted
per loop (T26-4's carrier and owner proof); this round supplies the third and
widens where the hoist applies.

- **The value.** A value read from a certified lane of the same element type
  is already an element of that lane — saturation included: `int[]` holds
  `inf` and the checked setter stores it unchanged (verified on both tiers).
  So the band test on such a value only has to reject null, and a read that
  is provably in range cannot be null (S7.1.3v2). `v[i] = v[j]` now stores
  with no validity check and no cold arm of its own.
- **Stores inside an inlined leaf.** A loop whose body calls a leaf used to
  lose its hoisted guard, because the scan invalidated every root passed to a
  call. The scan now asks the same question the call site asks
  (`mir_inline_call_target`) and, for a call that will be inlined, walks the
  callee's body with its array parameters standing for the caller bindings
  passed to them, pruning the call so its arguments do not count as bare
  uses. A loop calling a storing leaf keeps its one entry-time ownership
  proof.
- **The inliner reaches store-shaped leaves.** Three admissions were too
  narrow: a stored value may be a possibly-null element read (the checked
  setter's rejection leaves through the inline join), a callee may declare
  scalar locals of its own, and `a[i] + 1` is typed `any` because overflow
  may promote — its leaves are what restrict it, not its declared type.
  A contract wrapper such as `int?` carries `LMD_TYPE_TYPE`, so the
  admission unwraps it and asks the nullability question separately.
  permute's `swap` now inlines; the row went 3.80x → 3.30x (0.091 ms →
  0.079 ms).

Suite A/B: geomean 0.998, no output differs. Regression:
`test/mir/lambda/tune30_inline_store_guard.{ls,mir-check,txt}` pins both
halves — no call to the leaf survives, and the inlined store reaches the raw
lane store rather than the checked setter.

| row | round 1 | after §10.3 | after §10.4 | gate |
|---|---:|---:|---:|---:|
| nbody | 2.39x | 2.40x | 2.40x | 2x |
| fft | 4.58x | 3.89x | 4.05x | 2x |
| queens | 5.48x | 3.63x | 3.63x | 2x |
| spectralnorm | 4.51x | 3.51x | 3.51x | 2x |
| paraffins | 3.84x | 3.39x | 3.26x | 2x |
| quicksort | 3.68x | 3.66x | 3.56x | 2x |
| permute | 3.65x | 3.80x | 3.30x | 2x |

(Lambda's own times: nbody 3.61 ms, fft 0.093 ms, queens 0.062 ms,
spectralnorm 1.25 ms, paraffins 0.159 ms, quicksort 0.711 ms, permute
0.079 ms. fft's column moved only because the C2MIR reference did.)

### 10.5 T30-4 + T30-5, the argument's interval is the parameter's

Inlining removed spectralnorm's call but not the band tests inside the body,
and the instruction profile said why: `mul_Av`'s counters carry finite facts,
but those facts are keyed by *the caller's* bindings. Inside the inlined
`eval_A` the same expression is spelled in the callee's own parameters, which
had no interval at all, so `(i + j) * (i + j + 1) / 2 + i + 1` paid a
saturation check and a slow-arm call per operation.

The inline site now reads each int argument's interval in the caller's fact
scope and installs it on the parameter binding for the body. spectralnorm
**3.51x → 2.25x** (1.25 ms → 0.79 ms), the largest single move of the round
and the first row inside 2.5x. Suite A/B: geomean 0.995, no output differs,
spectralnorm 0.64x.

The first attempt was wrong in a way worth recording: the fact scope was
captured *after* the parameters were installed, so their intervals outlived
the body. The next inline of the same leaf found a stale fact for the same
parameter binding, and `tune27_add(pinf, ninf)` folded its sentinel check and
returned `0` where both tiers must say `nan`. The existing
`tune27_int_sentinel_arith` parity fixture caught it (S4.1.2: a sentinel
operand must never be laundered back into the band). The scope is now taken
before the binding loop.

### 10.6 A counted `for` gives its variable an interval

The finite plan is a `while` construct, so `for r in 0 to 7` carried no
interval at all. The counted-range path already proves both bounds are in
band, and the loop variable is an immutable binding (assigning to it is
E211), so its interval is exactly `[lower(a), upper(b)]` on every iteration
that runs. It is installed at the binding and dropped at the loop's end
label.

This did **not** move queens, and the profile says why: `get_row_column`'s
`c + r` has `r` from the `for` but `c` from `place_queen`'s parameter, which
no caller-side proof reaches (the call is recursive, so T30-6's caller-held
proof is what that row needs). The suite shows the fact paying elsewhere —
bounce 0.93, sieve 0.94, deriv 0.96, geomean 0.998, no output differs — so it
stays.

### 10.7 Measured and rejected

- **MIR optimization level 3** (register renaming + LICM, over the level 2 we
  ask for today) is a wash on the codegen-bound rows when measured
  interleaved: fft 0.097/0.099 vs 0.100/0.096, nbody 3.56 vs 3.57,
  spectralnorm 0.79-0.88 either way, quicksort 0.70 vs 0.72. Not worth its
  compile time. The spills are not a pass-ordering problem.

### 10.8 A container Item is already its pointer

With the band tests gone, the profiles put the remaining cost somewhere
else: spill traffic is 23% of `four1`'s samples and 29% of `partition`'s
(`ldr`/`str` against `x29`), ahead of any single guard. One emitter habit
fed it directly. `emit_box_container` is the identity — a container Item *is*
the pointer — yet every indexed access boxed the object and then masked it
back with `and reg, 0x00FFFFFFFFFFFFFF`. That is an instruction per access
and, worse, a second live register per array for the whole loop.

`mir_container_item_is_pointer` now skips the mask wherever the carrier type
is statically a container (the store site and the four array read sites that
did not already carry a raw-pointer rep). Suite A/B: geomean 0.993, no output
differs, quicksort 0.93x, sieve/knucleotide/levenshtein 0.96x, nothing worse
than 1.03 (noise at those sizes).

| row | after §10.6 | after §10.8 | gate |
|---|---:|---:|---:|
| spectralnorm | 2.25x | 2.17x | 2x |
| nbody | 2.40x | 2.32x | 2x |
| permute | 3.13x | 2.96x | 2x |
| quicksort | 3.54x | 3.20x | 2x |
| paraffins | 3.28x | 3.34x | 2x |
| queens | 3.15x | 3.43x | 2x |
| fft | 3.85x | 3.73x | 2x |

(paraffins and queens moved only because the C2MIR reference drifted between
runs; their own times are 0.18 ms and 0.072 ms, unchanged.)

### 10.9 T30-5 operator coverage, and T30-6

**T30-5's remaining operators.** The round's inliner admitted `float(...)`
but not `int(...)`, `div` or `%`, which is why paraffins' `ms2`/`ms3`/`ms4`
and quicksort's `lcg_next` still cost a call. Both are now admitted: an
integer division or remainder by a **nonzero literal** divisor has no error
arm (a saturated dividend still yields an int, S4.1.2), and `int`/`float` of
a numeric value is a pure conversion.

One trap cost most of the debugging: `int(x)` is a *type used as a cast*, so
its AST type is the type value itself and reads as `LMD_TYPE_TYPE`, exactly
like an `int?` contract wrapper but without a payload to unwrap.
`mir_inline_ast_tid` now falls back to the carrier for that case, and asks
nullability separately through `lambda_type_accepts_null`.

Every leaf the track named is now inlined: `ms2`/`ms3`/`ms4` (paraffins, 42
sites), `lcg_next` (quicksort), `swap` (permute), `get_row_column` and
`set_row_column` (queens), `eval_A` (spectralnorm). paraffins 0.180 ms →
0.152 ms, quicksort 0.721 ms → 0.660 ms.

**T30-6.** The track reads "the callee's witness/layout/certificate prologue
repeats a proof the caller already holds". Measuring `place_queen` first
showed the prologue is 9% of its samples and spills are 38%, so the ceiling
here is small — but the witness half of it is genuinely dead code:

- a direct native call **always** publishes the witness bit for a witness
  parameter (it either proves the argument or runs `emit_checked_boundary`
  on it first, then passes the raw pointer);
- the boxed `_b` wrapper — the entry every dynamic, T0 and cross-module call
  takes — publishes the *full* mask after admitting each array;
- `escaped` is already set for a public or address-taken function, so a
  closed function has no other caller.

`typed_array_witness_closed` nevertheless bailed out for *declared* array
parameters, to keep their admission for "callers outside the collected
graph". Those callers cannot reach the raw body, and the admission D3.3.1 and
D3.3.3 require still happens on every edge; what the closed case drops is the
callee re-testing a bit no reachable caller leaves clear. With the bail-out
removed, queens' four bodies all close (mask 0x1, 0x7, 0xf).

**It was reverted.** The elision is timing-neutral — the test is a perfectly
predicted branch, and removing it with its cold arms moved no row — while it
contradicted **ten** emission contracts that pin the admission shape
(`tune15_typed_array_witness`, `tune26_var_param_proof`, `typed_array_guard`
and seven more, several citing D3.3.1/D3.3.3). Rewriting ten encoded design
expectations for zero measured gain is not a trade worth making, so the
bail-out stays with the measurement recorded next to it.

What remains per call in `place_queen`'s prologue is the CW33 `var`-home
transport (4 params x ~7 instructions through `Context::mir_var_homes`),
which is ABI, not a redundant proof. Eliding it needs a new fact — "this
callee cannot publish a replacement when entered with a unique array" — which
touches S9.2.2 and must be proposed before it is built. **T30-6 is therefore
closed as "already implemented, remainder not worth it"**, and the row it was
supposed to help (queens) is bounded by spills (38% of samples), not by its
prologue (9%).

### 10.10 Register pressure: measured, and bounded at ~10%

The spill share (23-38% of samples) made register pressure look like the next
big lever. It is not, and the measurements say why.

**Where the spills come from.** `partition`'s loop body contains five calls —
the cold arms of the accesses whose index is not proven (`arr[i]`, with `i` a
*secondary* counter the dense proof does not cover). Every value live across
a call must sit in a callee-saved register or spill; ARM64 offers ten, the
loop needs about twelve, so two stack slots are reloaded three times per
iteration (~32% of the function's samples by attribution).

**What removing them is worth.** Two unsound ceiling experiments, each
reverted:

| experiment | quicksort | nbody |
|---|---|---|
| every index *read* proven (`mir_dense_index_proven` = true) | 0.67 → 0.655 ms (~2-3%) | — |
| every index proven, reads *and* stores, so no cold arm is emitted at all (`mir_index_proof` = IN_BOUNDS) | 0.66-0.78 → 0.60-0.66 ms (~8-10%) | 3.60 → 3.32 ms (~8%) |

So a *perfect* index-range analysis — the secondary-counter proof, T30-1
carried to its limit — is worth under 10% on these rows, not the 30% the
spill attribution suggested. The sampled spill reloads are largely stalls
attributed to the instruction after a dependent load, not independent cost.

**Conclusion for the round.** The codegen-bound rows are no longer dominated
by any single removable mechanism. What remains at 3.2-3.8x is diffuse: the
loop's own compare, two dependent element loads, the store, and a bounds
compare, with no one item above ~13% of samples. Reaching the 2x gate from
here needs a structural change — unchecked access under a whole-loop proof
with a deoptimisation exit, or a narrower value representation — not another
guard elision. That is a design question, not a tuning step, and it should be
proposed before it is built.

### 10.11 Two debts paid: the store-value fixture, and T30-1 for counted `for`

**The store-value fact is now pinned.** §10.4's claim — a value read from a
certified lane of the same element type needs no band test — rests on two
behaviours that were only checked in a scratch probe:
`test/lambda/proc/typed_store_saturated_value.ls` now pins them on both tiers.
`int[]` holds a saturated `inf` and `-inf`, the checked setter stores that
lane value unchanged through a direct store, a `var`-parameter store and a
loop store, and an absent value (an out-of-range read) is still rejected so
the destination keeps its old element. The golden matches the pre-round
binary, so it records existing behaviour rather than this round's.

(One cosmetic divergence surfaced: the same rejection prints "typed array
element assignment" on T0 and "typed array representation fallback" on the
JIT. Same behaviour, different boundary label; not ledgered.)

**T30-1 now covers the counted `for`.** The dense proof was a `while`
construct; `for i in a to b` had no extent proof at all. The scan and the
guard emitter are now shared between the two forms
(`mir_dense_scan_loop_body`, `mir_emit_dense_loop_guard`), and the `for`
entry point conjoins one extra fact its range gives it for free: `a >= 0`.
The guard is therefore `start >= 0 && len >= b + 1` per root, with every
unproven arm keeping the ordinary checked path. queens' `place_queen` now
carries a guarded `queen_rows` root through its `for r in 0 to 7`.

**It is performance-neutral, and the reason is structural.** Without
versioning, the guard replaces a per-access length load plus compare with a
per-access branch on a hoisted register — close to a wash. The win would need
a versioned fast copy of the `for` body, which §10.10's ceiling measurement
does not justify: the entire index-check category is worth under 10%, and a
second body copy feeds the register pressure that is already the binding
constraint. Suite A/B: geomean 1.003, no output differs, all 153 emission
contracts and every Lambda budget unchanged. It is kept because it closes the
gap T30-1 named, at no measured cost, and because the scan and guard it
factors out are what any future versioning of the form would build on.

Regression: `test/lambda/proc/dense_for_range.ls` covers the proven arm, an
array shorter than the range, a negative start, an empty range, a store
through a `var` parameter, and nested `for` loops. It counts matches rather
than summing, because adding an absent element to a declared `int` raises
E201 and would pin [LR12-22](../Lambda_Issue_Ledger.md#lr12-22) instead of the
proof under test.

### 10.12 Pinning the round: which mechanisms had no test

An audit of the round found that only four sidecars mentioned Tune30, and
three of those were pre-existing ones this round *updated*. The mechanisms
that moved the most time had no emission pin at all, so a regression in them
would have been silent — correct output, slower code. Four were added, each
verified to actually fail when its mechanism is disabled:

| mechanism | pin | verified bite |
|---|---|---|
| §10.5 argument intervals into inlined leaves | `tune30_inline_arg_interval.mir-check`: no call to the leaf survives, and a **count ratchet** of 4 `lambda_int_lane_add_slow` call sites | gating the propagation off raises it to 7 — the test fails |
| §10.9 `int()`/`div`/`%` in inlined leaves | `tune30_leaf_div_cast.mir-check`: neither leaf's call survives, the remainder reaches the caller's native `mod` | the fixture is the only coverage of these operators, behaviour included |
| §10.8 container-pointer mask | a count of `72057594037927935` in the same sidecar | a regression re-masks every indexed access |
| T30-3 libm NO_GC | budget probe `lambda_tune30_libm_no_gc` (`_tune30_norms_#` at 213) | removing `sqrt` from `JIT_LIBM_LEAVES` gives 222 — each call regains a root spill, a watermark publish and a layout reload |

T30-3 needed the *budget* ratchet rather than a pattern: the sidecar DSL has
register placeholders but no way to say "no spill between these two
instructions", and the property is exactly an absence. The size probe states
it directly.

Two behavioural fixtures were added alongside (§10.11):
`typed_store_saturated_value.ls` for T30-2's premise and `dense_for_range.ls`
for the counted-`for` proof.

Writing `tune30_leaf_div_cast` surfaced a second instance of
[LR12-23](../Lambda_Issue_Ledger.md#lr12-23): `int(r * (r + 1) div 2)` with a
saturating `r` prints `inf` on the JIT while T0 abandons the statement. The
fixture stays in band so it pins the inlining, not the divergence.

Still unpinned, deliberately: the counted-`for` dense guard (§10.11) and the
index leaf-check *shape* (§10.1). Both are already covered behaviourally, and
both are performance-neutral or correctness-critical rather than silent-perf
risks.

## 11. Issue audit and the consolidated defect item (2026-09-18)

Every issue this round touched was re-read against the specs and the design
ledger, and the open ones were resolved into one item.

### 11.1 Closed by a fix

| issue | resolution |
|---|---|
| **LR12-19** literal-bounded dense loops read past a short array | FIXED round 1 (the literal extent, then the folded `mulo`/`bo` flag) |
| **LR12-20** a dense-guard proof leaked into its fallback arm | FIXED round 1 |
| **LR12-21** index arithmetic over lane sentinels wrapped into a valid index | FIXED §10.1; `index_sentinel_sum.ls` |
| **LR12-23** the interpreter re-ran a partially applied iteration | FIXED §11.2 below; `loop_fast_path_bail.ls` |

### 11.2 LR12-23 was not a saturation bug

Filed as "a doubling accumulator saturates on T0 but not on the JIT". The
cause was worse. `interp_fast_int_exec` commits each assignment as it runs it,
and a value leaving the compact band abandons the fast path *mid-body*; the
ordinary evaluator then re-ran the whole iteration, so every statement that
had already committed ran twice. `while (i < n) { c = c + 1; m = m * K;
i = i + 1 }` returned **4** for `n = 3` — a wrong answer with no saturation in
it. One iteration is now atomic: `interp_fast_int_collect_targets` records the
register slots the body writes and a bail restores them, so the ordinary
evaluator resumes from the state the iteration started with.

### 11.3 LR12-15, LR12-16, LR12-18 and LR12-22 are one missing feature

All four are a failed deferred type check (E201) that the tiers disagree
about. **TE-15** (`Lambda_Design_Type_Enforcement.md`, decided 2026-08-01)
already rules on it: the error skips to the end of the smallest enclosing
block, the block evaluates to the error, and an uncontained defect becomes the
function's result, crossing a plain `T` return on the *unenumerated system
channel* — inference must never widen a signature because a defect is
possible. Neither tier implements this: T0 over-contains (the caller's own
statement is abandoned too) and the JIT republishes the error Item's bits as a
number through a native return lane, `nan` from a float and `inf` from an int.

Two fixes were implemented and reverted here, both rejected by the ruling:

- **a system fault** (`lambda_recovery_frame_raise_fault`) when the lane
  cannot carry an error — removes the `nan`/`inf`, but unwinds to the
  execution boundary and kills the rest of `main` where T0 continues. TE-15
  lists "skip to end of function" among the options it rejected; unwinding
  past the function is further still.
- **an error companion lane** on the callee — contradicts "inference must
  never widen a signature because of defect possibility", and S7.11.1v2 for
  the fault family; it is also an ABI change on every typed-store function.

The four entries are therefore **closed as symptoms** and replaced by
**LR12-24**. The design's tracking pointer
(`vibe/impl/Lambda_Impl_Type_Enforce.md`) is stale — the file on disk is named
`Lambda_Impl_Type_Enforce (done).md`, which is why the remaining work reads as
finished.

> **Two corrections to this subsection, made in §13 (2026-09-18).**
> 1. **T0 does not over-contain.** Measured on both tiers: T0 binds the error
>    to the caller's unannotated `let`, answers `is error` = true, and runs the
>    rest of the block. The "caller's statement is abandoned" reading came from
>    `print` rendering an error as nothing, which both tiers do — now
>    [LR12-25](../Lambda_Issue_Ledger.md#lr12-25), together with the worse
>    finding beside it (`string(<contained error>)` terminates the script).
> 2. **The error companion lane was rejected for the wrong reason.** TE-15
>    *prescribes* a lane for exactly this case. What it forbids is widening the
>    declared TYPE; an out-of-band ABI lane is invisible in types, which the
>    ruling says explicitly. The declaration and reassignment classes were
>    implemented this way in §13 and the fault-channel rejection above stands
>    unchanged.

### 11.4 Still open, unchanged

- **LR12-24** (new, above) — the consolidated defect-containment item.
- The 2x gate: no row reaches it; §10.10 bounds the remaining index-check
  category at under 10% and §10.13 states what a structural attempt needs.
- The §7 validation gates not run this round: the corpus JIT output diff
  against v46, forced GC with `POISON_FREED` + `ROOT_WITNESS`, and
  "typed <= untyped on every row".

## 12. What is next

- fft (3.85x) and quicksort (3.5x) are the furthest rows. Both are now
  register-pressure bound: `four1`'s hot window is `ldr`/`str` of loop
  bookkeeping against `x29`, not checks — the band tests T30-4 removed are
  gone from the profile.
- Function size is the cause: `four1` is ~4,800 instructions. Versioning is
  linear in nesting depth (a generic sibling never re-splits), but four loop
  levels still double a large body. Outlining each cold arm was measured as a
  non-fix (§10.10): MIR computes liveness on the CFG, so relocating a cold
  block does not shorten the live ranges that cross it.
- Reaching the 2x gate needs a structural change, not another guard elision:
  unchecked access under a whole-loop proof with a deoptimisation exit, or a
  narrower loop-local value representation. Both are design proposals.
- **LR12-24** is the round's one open defect item, and it belongs to the
  type-enforcement round-2 work rather than to tuning.

---

## 13. LR12-24: the defect channel for declaration and reassignment boundaries (2026-09-18)

Scope note: this is not tuning. It is recorded here because the round's issue
audit (§11) produced it and because it moves a ratchet this document owns.

### 13.1 What the ruling actually asks for

TE-15's cross-function ABI is explicit, and it is neither of the two things
§11.3 recorded as tried-and-rejected:

> Boxed-returning calls carry the error in the result Item; native-returning
> calls check the context error lane — one load-and-branch after the call, the
> Swift-`throws` shape. […] carry this as the emission-time effect bit
> (`FnEffectSummary.may_return_error`) — transitive in the implementation,
> **invisible in types**.

The rejected fix "give the callee an error companion lane" was rejected on the
grounds that it widens a signature. It does not: an out-of-band ABI lane is
what the ruling prescribes, and the widening TE-15 forbids is of the *declared
type*. That earlier rejection conflated the two, and it was wrong.

### 13.2 The mechanism already existed

`RETURN_SHAPE_NATIVE_ERROR` (v3 shape 4) returns `[native, error]` with
`ItemNull` meaning "no error", and the call site already merges lane 2 into a
boxed value-or-error join — its own comment says "it2d on the ERROR arm turns a
raised error into NaN", which is the reported symptom. The whole thing was
gated on `TypeFunc::can_raise`, so only a source-level `^E` got it.

Three edits, all in `transpile-mir.cpp` unless noted:

| | change |
|---|---|
| producer | `function_body_may_originate_defect` walks the **whole** body for declaration (TE-18 case 1) and reassignment (case 7) boundaries. `function_body_may_check_boundary` scanned only top-level statements, and returns immediately for a body that is a BLOCK rather than a LIST — i.e. it is blind for every `pn`. |
| contract | `lambda_body_return_lane` and `analyze_lambda_mir_variants` both select on one `carries_error_lane = can_raise \|\| may_originate_defect` fact; `em_return_shape`'s parameter (`mir_emitter_shared.hpp`) was renamed off `can_raise` so the next reader does not re-derive the old rule. |
| consumer | the call site reads `call_variant->result.shape == RETURN_SHAPE_NATIVE_ERROR` from the callee descriptor instead of the signature (RV10). |

### 13.3 The predicate has to ask the emitter's own question

Two failed attempts, both instructive:

1. **The MIR-aware gate applied naively.** `mir_assignment_boundary_applies`
   (extracted from `transpile_assign` so there is one spelling, not two) is the
   right question, but `declaration_may_check_boundary` — the AST-level
   approximation the boxed-return deopt uses — is not. It answers "may check"
   for `var total: int = 0`, because a literal carries its own `is_literal`
   Type object. That gave `tune29_finite_lane`'s `residues` an error lane,
   and with it the number-frame scratch slot the shape-4 epilogue spills its
   native first result into — a side-stack frame a counted loop never needed.
2. **Predicting from AST types instead.** This fixed `residues` and broke
   `tune14_native_return`: the AST type of `value + tune14_mutable(n - 1)` is
   `any` (a forward-referenced recursive call), so the accumulator got a lane,
   the call result became a boxed join, and the native consumer aborted with
   `mir-value: unavailable representation transition 1 -> 2`.

The answer is `mir_boundary_is_redundant` in both arms. It is emitter state
dependent, but in the *safe* direction: the pre-pass runs before the body's
bindings exist, so any lane proof it cannot make answers "may originate",
never the reverse. Measured: the pre-pass does have enough state for
`tune14_mutable` (its `mirgate` probe reads 0).

`declaration_may_check_boundary` therefore takes an optional `MirTranspiler*`
that selects the redundancy test, instead of the scan carrying a second copy of
the declarator walk. The two callers genuinely want different answers, and the
reason is worth stating where the code is: the deopt's response to a positive
is to deny the native return, which is always representable, so a false
positive costs it nothing; the defect scan's response is to reserve a lane, so
a false positive costs every counted loop a side-stack frame.

**A third thing was tried and abandoned: aborting on a mis-prediction.**
`emit_function_error_return` was made to `abort()` when a native-return frame
reaches it without a lane — the idea being that the predicate should be exact
and any gap is an emitter bug. It fired on **75 of 156** emission fixtures.
That is not a mis-prediction; it is the rest of LR12-24 (§13.7), so the abort
became a `log_debug("mir-defect-residue: …")` breadcrumb. Running an assertion
you expect to be silent, and then reading what it actually says, was the
cheapest measurement in this whole item.

### 13.4 Attribution of the ratchet movement

Measured by toggling the scan off behind a temporary env switch and re-running
the ratchet, rather than by attributing the difference from HEAD — the tree
already carries this round's uncommitted work.

| probe | LR12-24 | pre-existing (this round) |
|---|---|---|
| `lambda_corpus_deltablue` | **+225 module (+2.6%)**, `_constraint_choose_method_#` +43 | −31 / −23 |
| `lambda_corpus_cube3d` | 0 | +1437 module, `_run_cube_#` +57 |
| `lambda_corpus_prettier_ast` | 0 | +15 |
| `js_corpus_*` (3 probes) | 0 | +55 / +3 / +61 |
| `lambda_tune4_typed_array_guard` | 0 | −9 module, −8 insns, −1 safepoint |
| `lambda_tune4_callsite_inference` | 0 | −6 |
| `lambda_scalar_home_tail_forward` | 0 | −2 / −2 / −1 root / −1 safepoint |

`mir_budgets.json` was re-baselined for all of these, each description carrying
its attribution. **`lambda_tune30_libm_no_gc` had to be re-added**: the probe
was lost when `git checkout` was used on the budgets file to undo a bad edit,
which reverted this round's entries with it. Its `_tune30_norms_#` budget is
165, not the 213 first recorded.

### 13.5 A sidecar was pinning the bug

`tune26_terminal_oob_boundary.mir-check` required `lambda_type_check` to be
followed **on the immediately next line** by `call it2d_p, it2d`. That pair
*is* the NaN republication: the checked boundary returns an error Item and
`it2d` reinterprets it as a double. The sidecar now forbids `it2d` in
`_read_sum_#` outright and says why.

The other 20 `expect_seq` sidecars were checked for the same shape: none pairs
a checked boundary with a lane conversion, so this was the only one. The
general hazard stands though — an `expect_seq` pins *adjacency*, which is a
statement about how the emitter happens to be written rather than about what it
must guarantee, so it can freeze a defect as easily as a property.

### 13.6 Runtime cost is within noise

The +2.6% static growth does not show up in time. A/B measured from **one**
release binary with the predicate behind a temporary `getenv` switch (median of
7, `temp/lr1224/ab.sh`):

| row | OFF (ms) | ON (ms) | delta |
|---|---|---|---|
| deltablue (jetstream) | 157.2 | 157.4 | +0.1% |
| deltablue (awfy) | 274.8 | 275.4 | +0.2% |
| nbody (awfy) | 102.0 | 101.9 | −0.1% |
| quicksort (larceny) | 46.7 | 46.7 | +0.0% |
| richards | 432.4 | 430.2 | −0.5% |
| splay | 430.8 | 430.0 | −0.2% |
| queens | 35.2 | 35.0 | −0.6% |
| towers | 35.1 | 35.2 | +0.3% |
| permute | 31.6 | 31.6 | +0.0% |

The added instructions are a lane store on each return path plus a cold error
arm; the hot path gains one compare-and-branch per native call to a
defect-capable callee, which is what TE-15 budgeted for. Measuring both arms
from the same binary matters here — a per-build A/B on differences this small
would be reporting build noise.

### 13.7 Verification

Baseline **5631/5634** (Lambda runtime 3527/3530), MIR emission 156/156,
ratchet 20/20, forced-GC stress 203/203, Lambda runtime suite 941/941, proc
6/6. The three remaining failures are JS — `tune12_array_access`,
`dynamic_call_invoke_entry`, `regex_bt_legacy_octal_assertion` — and are red at
HEAD: no `lambda/js/` file is modified in this tree, the one shared header edit
is a parameter rename, and the five unpulled upstream commits touch only
Radiant, CSS and the build. They are not attributable to this round.

Fixture `test/lambda/proc/defect_native_return_lane.{ls,txt}` covers both
classes on both native lanes — `int_acc`/`float_acc` (case 7, `inf` and `nan`
respectively before the fix) and `read_sum` (case 1) — together with the happy
path and a clean call issued after the defect, to pin that this is containment
and not an abort. Identical on both tiers.

### 13.8 What is not fixed

`emit_return_if_item_error` has ~40 call sites and this covers two classes. An
assertion placed at the unrepresentable return during this work fired on **75
of 156** emission fixtures; that is the size of the remainder, and it is now
logged as `mir-defect-residue` rather than being silent. The blockers are named
in [LR12-24](../Lambda_Issue_Ledger.md#lr12-24): TE-17's I3 lane eligibility
(a defect-capable call's result is `T | error` and must not be lane-eligible)
and TE-18 S1's required runtime report for element stores. Both are design
items, and TE-15 says the `may_defect` call-graph fixed point "must be built
before, not after, the routing work".

Two symptoms were split out as [LR12-25](../Lambda_Issue_Ledger.md#lr12-25),
both identical on the two tiers: `print` renders an error value as nothing
(which is what made T0 look like it was over-containing — it is not), and
`string(<contained error>)` **re-raises it as a top-level E201 and terminates
the script**. The second is a containment escape and the more serious of the
two: a value the block legitimately holds, and that `is error` answers true
for, becomes an uncatchable process failure the moment it is converted.
