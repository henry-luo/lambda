# Tune 21: Result36 — Regression Triage, the Untyped Lane, and the Auto Tier

- **Date:** 2026-09-02
- **Status:** PROPOSAL. Nothing in this round is implemented. §2 is measured
  evidence (release binaries, quiet machine, `LAMBDA_TIER=jit`, workload-only
  `__TIMING__`); §3 ranks the tracks; §4 lists what not to do; §5 sets targets.
- **Input:** `test/benchmark/Overall_Result36.md` / `benchmark_results_v36.json`
  as of the 2026-09-02 re-measure (all 59 rows validate on every engine; the
  hyphen and queens rows were re-measured after their sources were fixed —
  see §2.6). Clean release builds of nine commits between Result35 and HEAD
  under `temp/wt*/` (recipe in §6.3), plus `temp/lambda_head_release.exe`.
- **Related:** `vibe/impl/Lambda_Impl_Tune20 (done).md` (T20-#; §2.5 status
  table and §4 inherited decisions still stand), `vibe/impl/Lambda_Impl_Tune19
  (done).md` (§8 negative-results ledger — every entry there is a decision),
  `vibe/Lambda_Design_Compiling_Lane.md` (ValueRep / `MirValue`),
  `vibe/Lambda_Design_Runtime_COW.md` (CW29–CW33; §11.6 CW30),
  `vibe/impl/Lambda_Impl_Ast_Interp.md` §3.1 (P2 satellite gates).
- **Formal authority:** `doc/Lambda_Formal_Semantics.md` S3 (truthiness), S9.1.3
  (plain-param snapshot), S11.4.2 (declared returns are enforced);
  `doc/Lambda_Formal_Design.md` D2.4.1–D2.4.3 (value representation
  discipline), D3.3 (inference), D4 (memory), D5.2 (scalar homes), **D8.1.1v5**
  (tiered execution: T0 + P2 satellite promotion; "P2 fails closed for
  aggregate/structured signatures"), D8.4.1v2 (no inline caches), D8.6.1–D8.6.3
  (emission ratchet, mir-check, forced-GC sweeps).
- **ID series:** `T21-#`.

---

## 1. Where v36 stands

| Metric (59-row basis) | Result34 | Result35 | **Result36** |
|---|---:|---:|---:|
| MIR (untyped)/Node geo | 1.35x | 1.26x | **1.37x** |
| MIR (typed)/Node geo | 0.80x | 0.73x | **0.85x** |
| MIR (typed)/C2MIR geo | 5.23x | 4.67x | **5.67x** |
| MIR (untyped)/C2MIR geo | — | — | **9.17x** |
| Rows ≥ 20x C2MIR (typed) | 5 | 4 | **8** |
| Typed rows > 1.05x their untyped row | 4+ | — | **14** |
| Auto tier e2e / Node e2e geo (part 2) | — | — | untyped **4.13x**, typed 2.79x |

Result36 is the first report since Result22 where every headline number moved
the wrong way, and it did so *despite* the Tune20 wins landing in the same
window (microdiff 0.17x of its v34 typed time, binarytrees 0.25x, list 0.37x,
raytrace3d 0.40x, base64 0.67x). The reason is not one regression but four
independent ones that landed in different commit windows between Result35
(`fe6c8e14c1`, Aug 26) and HEAD, each on a different mechanism. §2 attributes
them; T21-1 is their reversal and is the round's first and cheapest track.

The two structural findings underneath are unchanged from Tune19/20 and are
now the larger levers once the regressions are undone:

- **The untyped lane is 9.17x from C2MIR against typed 5.67x, and on the rows
  where the two differ the gap is enormous:** ray 51.7x vs 1.7x, spectralnorm
  67x vs 4.6x, quicksort 53x vs 5.4x, levenshtein 39x vs 7.2x, list 37x vs
  10x, permute 27x vs 4.7x, mbrot 23x vs 1.5x, raytrace3d 33x vs 8.4x, nbody
  29x vs 10x, towers 47x vs 17x. Result32's finding stands: if every untyped
  row merely reached its own typed row, MIR-U/Node would be below 0.8x with
  no new mechanism. This is T21-2.
- **The shipped tier is not the measured tier.** Part 1 pins `LAMBDA_TIER=jit`.
  What `lambda.exe run x.ls` actually does is T0 with P2 promotion, and P2
  refuses every function that has an `any`, array or map parameter — which is
  every untyped `pn f(x)`. Untyped hyphen: 3.3 ms on the JIT, 64 ms end to end
  on the auto tier (T0 interprets 1.9 M nodes); ray typed 0.30 ms vs 157 ms;
  tak 0.13 ms vs 53 ms; sum 0.82 ms vs 155 ms; diviter 263 ms vs 107 s. This
  is T21-3.

## 2. Evidence

### 2.1 Method

- Nine release binaries built from clean worktrees (§6.3): v35 `fe6c8e14c1`,
  `357df0ae0` (Tune20 merge, Aug 26), `742b2bb1f` (Aug 27), `c13515b80`
  (pre-COW, Aug 29), `cfd215819` (post-CW33, Aug 29), `9f3f05e1f` (Aug 30),
  `176bdd934` (Sep 1), v36 `33a178ed0` (Sep 2), HEAD `db2c7d91d`+tree. The v35
  binary is byte-for-byte the size Result35 recorded for its archive
  (19,817,976); the v36 binary matches its metadata (18,615,016).
- Timing: `LAMBDA_TIER=jit`, best of two, quiet machine, one row at a time.
  Emitted code: `LAMBDA_MIR_DUMP_PATH=<file>` (works on release builds) with
  callee histograms diffed across binaries, names normalized. GC:
  `LAMBDA_GC_STATS=1` (`mark_collections` / `mark_ms` on the `gc-tune-stats`
  line). Profiles: `sample <pid> 6 5` on 200-iteration loop copies — with the
  caveat in §6.2.

### 2.2 The bisect

Same-source rows only (the row's `.ls` is identical across all nine binaries).
Milliseconds, `LAMBDA_TIER=jit`, best of two.

| Row | v35 | Aug 26 | Aug 27 | pre-COW | post-CW33 | Aug 30 | Sep 1 | v36 | HEAD | attributed window |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| crypto_sha1 (untyped) | 59 | 91 | 104 | 223 | 221 | 222 | — | 206 | 223 | **W1a** Aug 26 (+55%) then **W1b** Aug 27–29 (2.1x) |
| crypto_sha12 (typed) | 47 | 46 | 52 | 156 | 172 | 179 | fail | fail | 197 | **W1b** Aug 27–29 (3.0x) |
| gcbench2 | 252 | 259 | 537 | 532 | 547 | 592 | — | 543 | 617 | **W1a** `357df0ae0..742b2bb1f` (2.1x) |
| matmul (untyped) | 12.1 | — | — | 12.2 | 12.2 | 12.1 | 23.0 | 23.1 | 23.1 | **W3** `9f3f05e1f..176bdd934` (1.9x) |
| matmul2 (typed) | 11.6 | — | — | 11.7 | 11.6 | 11.6 | 28.9 | 28.7 | 28.5 | **W3** (2.5x) |
| primes2 | 3.38 | — | — | 3.39 | 3.37 | 3.45 | 5.66 | 5.55 | 5.30 | **W3** (1.65x) |
| sieve2 | 0.033 | — | — | 0.033 | 0.038 | 0.039 | 0.051 | 0.051 | 0.053 | **W3** (1.35x) |
| puzzle | 8.86 | 8.84 | 8.9 | 8.84 | 13.0 | 13.3 | 13.1 | 12.8 | 18.9 | **W2** COW window (1.47x) then **W4** v36→HEAD (1.45x) |
| deltablue2 | 146 | 146 | — | 173* | 117 | 131 | 128 | 117 | 135 | improved through W2; **W4** +15% |
| fft | 0.167 | — | — | — | — | — | — | 0.158 | 0.165 | flat — the report's 1.64x is a tiny-row artefact |

\* single run under load. The Aug 26/27/30/Sep 1 columns were timed while a
build was running for some rows; the *steps* are unambiguous (each is ≥1.5x
and consistent in both neighbours), the absolute values on those columns are
not to be trended.

### 2.3 W1a — gcbench2: the object-pressure collector (commit `95b029739`)

`LAMBDA_GC_STATS=1` on gcbench2: v35 and the Aug 26 binary run **0**
collections; every binary from Aug 27 on runs **19**, spending 88–320 ms in
marking on a 139 ms workload. The emitted MIR is *identical* between v35 and
pre-COW (1,151 lines, no callee changes). Commit `95b029739` "Complete JS AST
interpreter JIT integration" (2026-08-26) added `gc_heap_maybe_collect_object_pressure`
to `gc_heap_alloc` / `gc_heap_calloc_class` / `gc_heap_bump_alloc`
([gc_heap.c](../../lambda/runtime/gc/gc_heap.c) ~L645): it collects whenever
`total_allocated ≥ object_threshold`, with `object_threshold` starting at
`GC_OBJECT_HEAP_THRESHOLD = 4 × GC_DATA_ZONE_BLOCK_SIZE` and rebased to
`2 × total_allocated` after each collection. `total_allocated` is decremented
on free, so this is a live-bytes trigger whose floor is four data-zone blocks —
small enough that a tree-building workload doubles past it nineteen times.
The prior policy (data-zone threshold only) never fired on this row.

This is the T20-5 gate in reverse: Tune20 closed the GC track because
collection was under 10% of every profiled row; W1a moved gcbench2 to ~50%.

### 2.4 W3 — matmul, primes2, sieve2: the index-expression lowering (`9f3f05e1f..176bdd934`)

matmul2's `_matmul` kernel, post-CW33 vs v36 (normalized dumps, 334 → 379
lines, 130 → 146 locals). Post-CW33 hoists `i * N` once per outer iteration
with a single `mulo`/`bo`, and the inner loop indexes with `mul %r,%r,%p` /
`add` / `and 72057594037927935` (the int53 mask) and a bounds `ges`. v36's
inner loop instead computes `mulo`/`bo`/`addo` **per element**, followed by the
full int53 range clamp (`le %r, 9007199254740991` … `mov %r,
9223372036854775807`) before the load. The only new helper call is one
`lambda_type_check`; the cost is the in-line ceremony, not a call. The same
window boxes bool-lane element reads (primes2: `is_truthy` 0 → 2 in the sieve
loop; sieve2 the same) — this is the `VALUE_REP_I64` publish that produced the
Result36 wrong-output rows, whose correctness fix in `56ab0a571` chose
`MIR_INDEX_RESULT_BOXED_BOOL` + `is_truthy` rather than a native bool rep.

Both are the same root cause class as Result32's finding: the index and bool
expressions lost their **witness** (in-range by construction; boolean by
contract) when their producers were unified onto the generic ValueRep publish,
so consumers re-prove at runtime what the planner knew. D2.4.2 says a
`MirValue` carries the *contract*; a counted-loop index against a
loop-invariant bound is in range by that contract, and the emitter must not
regenerate the overflow arm for it.

### 2.5 W2 and W4 — puzzle, deltablue2

puzzle pre-COW → post-CW33 (8.84 → 13.0 ms): the dump replaces
`fn_index_assign ×4` / `fn_array_set ×8` with `index_assign_cow ×4`,
`member_set_cow ×3`, `cow_mark_shared ×3` — the CW29/CW32v2 write path on a
container the function created itself (`fill`) and never shares. deltablue2 at
HEAD carries 127 `cow_prepare_write` and 6 `cow_bind_var` calls (v36: 122/1);
havlak untyped carries 60 `cow_capture_value`, 27 `cow_prepare_write`, 24
`cow_bind_var`, 20 `cow_mark_shared`. None of these are wrong; they are the
price of S9 value semantics paid dynamically where CW30's compile-time
exclusivity proof could pay it statically.

puzzle v36 → HEAD (12.8 → 18.9 ms) and deltablue2 (+15%) have **identical
emitted MIR**, so the +45% is on the runtime side of `56ab0a571`/`e2059fdfc`
or the DOM relocation commits. A split build at `56ab0a571` is queued (§2.7).

### 2.6 What Result36 got wrong about itself (fixed before this proposal)

- **Bool-lane read bug** (`176bdd934`): `if (flags[i])` on a bool array boxed
  the 0/1 as int64 and asked `is_truthy`, which is always true for numbers
  (S3.1). 11 untyped / 6 typed rows were excluded as `wrong_output`. Fixed in
  `56ab0a571` (boxed-bool result), verified on a fresh build. The fix costs
  primes2/sieve2 their native branch (§2.4).
- **Nine benchmarks relied on write-through plain params** (mbrot, nbody, json,
  quicksort, havlak, cd, richards, splay, hashmap); migrated to `var` in
  `56ab0a571`. **queens** was missed because its check was too weak: post-flip
  it made 8 probes instead of 876 and printed PASS at 0.032 ms (the report's
  "11x win"). Fixed 2026-09-02 (`var` params + `is_valid`); re-measured 0.393 ms.
- **hyphen / hyphen2 were spec-invalid**: `pn is_letter(ch) int` returned a
  bool. T0 enforces S11.4.2 and raised E201 19,392 times per run, an error is
  falsy, the checksum was 627872 against the C port's 731008, and the JIT only
  agreed after `56ab0a571` made the `and`/`or` result boxed (which engaged the
  return firewall). Fixed 2026-09-02 (`bool` returns, checksum asserted);
  re-measured 3.35 / 1.85 ms, auto e2e 64 ms (the 1776 ms cell was an
  unreproducible run artefact — a clean build of the same commit gives 185 ms).
- **C2MIR column was empty** because `make release` deletes `lambda/mir/c2m`
  in its clean step; rebuild with `make c2mir-driver` before any run.
- The report header still names `33a178ed0` although 13 rows and the C2MIR
  column were merged from later builds (`_metadata.merged_engines`).

### 2.7 Open attribution

- **crypto_sha1 W1b** (Aug 27–29, 2.1x/3.0x): 0 collections on every binary
  and near-identical dumps (+3 `lambda_type_check`, +3 `int2it_lane`), so it is
  a runtime-helper change. Profiles of both sides are dominated by the same
  symbols (`__vfprintf`/`__sfvwrite` — number formatting inside `binb2hex`'s
  `slice`/`++` loop — `unwrap_simple_type_type`, `lambda_side_root_alloc_n_for`,
  malloc/free, memmove), which says the row is a string/formatting workload,
  not SHA arithmetic, on both sides. A split build at `0468de102` (Aug 28,
  after the issue-ledger fixes) is **flat with Aug 27** on both rows, so the
  step is inside `0468de102..c13515b80` (JS unify `e514defe6`/`0a243279b`/
  `4a4bbf2d3`, the DOM edit clean-ups, `ca574c869` ledger clean-up, JS P2c
  `3c01d18e9`/`068301268`, `c7ff68031` linux fix, `114bc449a` print bug fix,
  `fba350120` JS P3f, the js262 runs). A build at `ca574c869` is queued to halve
  it. Candidates by file churn: `type_contract.cpp` (+126),
  `lambda-data-runtime.cpp` (+105), `lambda-stack.cpp` (+75), `lambda-eval.cpp`.
- **W1a's +55% on untyped crypto_sha1** at the Tune20 merge is unexplained
  (typed is flat there); it may be the same runtime mechanism as W1b or the GC
  policy (the row allocates per iteration).
- **W4** (§2.5): split build at `56ab0a571` queued.

## 3. Tracks (ranked; each separately land-able and gate-able)

### T21-1 — Reverse the four regressions (the cheapest 20% of the ceiling)

Undoing W1–W4 alone returns typed/C2MIR from 5.67x to roughly Result35's
4.67x on the same sources, and untyped/Node from 1.37x to ~1.2x. Each item
has a bracketing pair of binaries under `temp/wt*/`, so every fix is an A/B
against a known-good build, not a hypothesis.

**Implementation status (2026-09-02):** T21-1a, 1b, 1c and 1e landed in the
working tree (see the per-item notes below); 1d is deferred (§T21-1d). Three
emission fixtures pin the results: `test/mir/lambda/tune21_index_mul_hoist`,
`tune21_bool_lane_native`, `tune21_u32_decl_lane`. Interleaved release A/B
against the HEAD control binary (3 alternating pairs, a foreign build was
loading the machine so read the ratios, not the absolutes):

| Row | control (ms) | T21-1 final (ms) | ratio | mechanism confirmed |
|---|---:|---:|---:|---|
| crypto_sha12 (typed) | 179 / 182 / 170 | 58.2 / 56.8 / 56.2 | **0.32x** | u32 declaration witness (1e) |
| crypto_sha1 (untyped) | 211 / 199 / 197 | 103 / 106 / 105 | **0.52x** | same; the untyped row's other half (W1a, +55% at the Tune20 merge) is still open |
| gcbench2 | 572 / 559 / 565 | 357 / 389 / 388 | **0.67x** | 19 → 4 collections, mark 280 → 21 ms (1a) |
| havlak2 | 511 / 525 / 584 | 360 / 358 / 415 | **0.70x** | index arithmetic (1b) |
| puzzle | 34.5 / 33.4 / 32.4 | 25.2 / 25.3 / 28.3 | **0.78x** | index arithmetic (1b); W2's COW helpers remain (1d) |
| hashmap2 | 215 / 225 / 239 | 194 / 171 / 175 | 0.80x | index arithmetic (1b) |
| sieve2 / sieve | 0.102 / 0.080 | 0.079 / 0.076 | 0.78x / 0.95x | native bool lane (1c) |
| primes / primes2 | 31.8 / 9.2 | 27.8 / 9.3 | 0.87x / flat | 1c; primes2's typed loop already branched natively |
| quicksort2, queens | 2.25, 0.78 | 1.86, 0.64 | ~0.82x | 1b / 1c |
| matmul (untyped) | 38.8 / 37.8 / 41.0 | 33.4 / 32.2 / 34.7 | 0.85x | 1b |
| matmul2 (typed) | 52.4 / 51.0 / 57.9 | 59.7 / 53.8 / 54.7 | **flat** | 1b only partially recovers W3 — see below |
| bounce2 | 2.67 / 3.36 / 2.52 | 0.169 / 0.188 / 0.181 | **0.07x** | the HEAD control had regressed this row post-Result36 (v36: 0.108); 1b/1c recover it |
| fib, fft2, nbody2, array1, fannkuch2, spectralnorm2 | — | — | flat | control rows, no regression |

**matmul2 is the one row not recovered.** The typed kernel is now 294 MIR
instructions against the control's 420 and does 14 per index against ~25, but
the Result35 kernel did 4 (`mul`/`add`/mask/`bt` — payload arithmetic with no
overflow contract at all). The remaining 10 are the S4-sound parts: two narrow
compares, the int53 `le`, and the branch-free poison fold. Two of those are
loop-invariant for `i * n` and hoistable by MIR's LICM; whether it hoists them
could not be measured on the loaded machine. Closing the rest needs a proof
that the *bound* is narrow once per function (a versioned kernel or an entry
guard), which is T21-2's counted-loop witness work, not a regression reversal.

**What landed, per item (the mechanism each fix actually needed):**

- *1a* — `gc_heap_rebase_object_threshold` paces the object-pressure floor
  by the collector's share of wall time since the previous pressure
  collection (×4 above a 50% share, ×2 above 10%, capped at 1 GB, never below
  the original floor); each pressure collection records its cost and end
  time. gcbench2: 19 → 4 collections, marking 280 → 21 ms.
- *1b* — three changes in `mir_emit_native_index_expr` /
  `mir_sanitize_native_index`: (i) the flag-carrying `mulo`+`bo` pair is
  gone — a product of two loop-proven nonnegative operands is a plain `mul`
  guarded by one unsigned compare per operand against 2^26 (exact: product
  < 2^52), and any other product is checked exactly in the double lane
  (`|i2d(l)·i2d(r)| < 2^53`); (ii) the validity flag is lazy (a leaf carries
  none; only a multiply contributes a predicate), which removed the
  constant-1 `mov`/`and` bookkeeping per leaf; (iii) the poison is selected
  branch-free (proven-nonnegative: `or` with `valid-1`, which the bounds
  check's `lts 0` rejects; otherwise `xor`/`and`/`xor` against INT64_MAX).
  matmul2's kernel: 420 → 294 instructions, 14 per index against ~25. The
  first attempt (narrow check + a `mulo` fallback branch) measured flat, and
  the second (double lane only) measured slower — instruction count was the
  cost, not the branch, and this is now recorded in §6.2.
- *1c* — no new `ValueRep`: the descriptor already distinguishes a native bool
  as `(VALUE_REP_I64, LMD_TYPE_BOOL)`; the two bool-array load policies now
  publish exactly that (via `mir_value_from_reg`'s semantic override) instead
  of the AST's `any`, so `mir_profile_emit_condition` branches on the byte and
  `lambda_convert_rep` boxes it as a bool. The out-of-bounds and slow arms
  produce the lane's null (2, D2.5.1), `mir_expr_may_be_null` admits bool
  element reads (and native `and`/`or` results), the converter boxes a
  possibly-null native bool through `emit_box_bool_lane`, an unannotated
  `let x = flags[9]` binds as an Item, and `and`/`or`/`not` on the lane test
  `== 1` / `!= 1` (null is falsy, `not null` is true) — verified identical to
  T0 on every probe (`temp/oob_bool.ls`, `temp/oob2.ls`).
- *1e* — `emit_checked_boundary`'s u32 fast arm was dead since `ca574c869`
  replaced the banned `MIR_reg_type` probe with a `value_rep` parameter that
  the declaration site never passed; the site now passes
  `VALUE_REP_INT_LANE` when its own `native_u32_initializer` path produced
  the lane. `_rol`/`_safe_add`: `lambda_type_check` 1/2 → 0/0.
- Gates run: every `test/lambda/**/*.ls` with a golden, both tiers, against
  the HEAD control binary — **0 regressions** (82 diffs pre-exist on the
  control); the bool/index/cow fixtures listed above; three new emission
  fixtures. `make test-lambda-baseline`: **4098/4100** on the first run, the
  two failures being over-strict `forbid` lines in the new sidecars (the
  float `dmul` of the kernel itself; an unrelated boxing call in `_rol`);
  after correcting them `test_mir_emission_gtest` is 66/66 and
  `test_mir_ratchet_gtest` 16/16, every other suite in that run passed
  (Lambda runtime 1994/1996 → 1996 with the sidecar fix, input parsers
  2104/2104, gc-stress 96/96).

Two pre-existing tier divergences surfaced while probing and are recorded
here rather than fixed (they predate Result36 and are outside T21-1's rows):
`u32 + int` overflow prints `inf` on the JIT and `4294967297` on T0 (v35 and
HEAD both); untyped `kostya/matmul` prints `sum=0` on T0 at HEAD (v35 T0 was
correct) — a T0 `var`-param write-back regression on the `c` array.

- **T21-1a GC object-pressure policy** (W1a). Ruling needed: the pressure
  collector is a reasonable defence for the JS/DOM heap it was added for, but
  its floor (4 data-zone blocks) and its rebase rule (2× live) are wrong for
  allocation-bound scripts. Proposal: (i) trigger on **bytes allocated since
  the last collection**, not on live bytes, with a floor no lower than the
  data-zone threshold; (ii) rebase to `max(2 × live-after-collect, floor)`;
  (iii) never fire while the data-zone trigger would fire first. Gate: gcbench2
  back to 0–2 collections and ≤ 145 ms; collection share < 10% on every
  profiled row (T20-5's rule); js262 and the DOM suites unchanged.
- **T21-1b Index witness restoration** (W3). Put the "in-range by
  construction" fact back into the one oracle both planner and emitter consult
  (the Tune20 rule confirmed twice: a lane witness derived locally at an
  emission site is how representation bugs happen). A counted-loop induction
  variable times a loop-invariant `int` bound, plus a loop-invariant offset, is
  provably < 2^53 when the array length is; emit the hoisted `mulo` once and the
  masked add inside. Add a `mir-check` fixture on `_matmul` that **forbids**
  `mulo`/`addo` inside the inner loop (D8.6.1 ratchet, adjacency-asserted per
  Tune19 §8.4-6). Gate: matmul/matmul2 back to ≤ 12.5 / ≤ 12 ms; no row worse
  at 3 pairs.
- **T21-1c Native bool rep** (W3 + `56ab0a571`). Add `VALUE_REP_BOOL` (a
  0/1 machine bool distinct from `VALUE_REP_I64`) so a bool-lane element read
  publishes a rep that (a) conditions branch on directly and (b) boxes as a
  *bool* Item when a consumer needs an Item — instead of today's forced
  `MIR_INDEX_RESULT_BOXED_BOOL` + `is_truthy` call. This closes the class of
  bug behind Result36's wrong-output rows structurally (D2.4.3: conversion is
  keyed by (source rep, target rep); I64→ITEM and BOOL→ITEM must be different
  pairs). Gate: primes2 ≤ 3.5 ms, sieve2 ≤ 0.035 ms, `proc_fill_bool_inferred_read`
  and the r1/h1 repros pass on both tiers, forced-GC sweep (D8.6.3).
- **T21-1d COW write elision by exclusivity** (W2). **DEFERRED (2026-09-02):**
  not started in this round; puzzle recovered 0.78x through 1b alone and the
  exclusivity proof is a CW30-family design change with its own soundness
  gate (poison sweep on every raw-store site). Extend CW30's
  compile-gated exclusivity to the write side: a `var` local or `var` param
  whose container was created in this activation (`fill`, literal, `array()`)
  and never escaped (no capture, no store into another container, not
  returned before the write) is exclusive; its element/member stores lower to
  the raw setter. Gate: puzzle ≤ 9 ms, no `index_assign_cow`/`member_set_cow` in
  its dump (mir-check), COW semantic fixtures unchanged, poison sweep.
- **T21-1e crypto_sha1 and W4** — attribute first (§2.7), then fix. Do not
  guess; the site is likely one level away from the obvious file (Tune19 §8.3).
  **Investigation (2026-09-06): the specified mechanism has no remaining
  target; the real copies come from two idioms it does not cover — needs a
  ruling before code.** Evidence on `temp/lambda_t33_final.exe`:
  - puzzle already meets the gate: its dump has no `_cow` call at all (six
    raw `fn_array_set`), 0.89 ms — T21-2d/CW33 got there first.
  - richards (AWFY) and deltablue2, the T21-4 rows that pointed here, spend
    ~4% and <2% of samples in COW helpers (`cow_path_set_raw` 22 +
    `cow_prepare_write` 18 of ~1000); the spine emitter already skips the
    reinstall when a link did not move. Their cost is the untyped map field
    protocol (`fn_map_set` 111, `fn_index` 83, shape walks ~190) — T20-1.
  - `COW_EXEC_PROFILE=1` over the suite (real one-level copies, not call
    sites): cd/cd2 **220k array copies / 88 MB** + 132k map copies; splay
    **607k map copies / 40 MB**; havlak **205k array + 40k map copies /
    38 MB**; crypto_aes **277k `array[num]` copies / 53 MB**; jetstream
    deltablue 16k copies of 3.8 KB arrays / 61 MB; awfy deltablue 49k / 5 MB;
    richards 23k map copies / 1.7 MB (with 1.75 M unique mutations — the
    nested-path port works as designed). Estimated share of the row at
    ~0.3 µs per small copy: havlak ~30%, splay ~25%, crypto_aes ~17%,
    deltablue ~7%, cd ~6%, richards <2%.
  - The two idioms: **(A) read-modify-write through a handle** — `var l =
    a.l0; … l[i] = c; a.l0 = l` (cd/havlak `arr_set`, splay's node
    handles, deltablue's `p_vars`/`constraints`): the bind marks the level
    shared (`cow_bind_var`), the write copies it, the store-back captures the
    copy (marks it again), and the mark is monotonic (D4.4.1), so every later
    call copies every level — `marks ≈ copies, unique = 0` is the signature.
    **(B) functional rebinding through a mutated plain parameter** — `st =
    SubBytes(st)` with `pn SubBytes(s) { s[i] = …; return s }` (crypto_aes):
    CW29's callee-side entry mark plus one copy per call, although the
    caller's `st` dies at the rebind.
  - Neither is "a container created in this activation that never escaped":
    in (A) the level was read out of `a`, in (B) the container is the
    caller's. Both elisions are compile-time exclusivity proofs of a new
    kind: (A) = lower the bind as a CW25-style **path borrow** when the body
    stores `l` back to the same path with no intervening read of `a` and no
    other escape of `l` (the store-back then vanishes — richards' nested
    path port does this by hand); (B) = a **moved argument**: at `x = f(x)`
    with `x` an unmarked local dead after the call, pass ownership so the
    callee skips its CW29 entry mark (an ABI signal the callee must see,
    since CW29 placed the mark callee-side). D4.4.1's sanctioned extensions
    (saturating counts / GC-refresh) do not help (A): at the write the field
    still observes the old level, so the copy is required until the
    store-back is proven. Options and the rows each unlocks are in the
    2026-09-06 session summary; **ruling needed (CW34/CW35 candidates)
    before any of this lands** — S9.1.2/S9.1.3 make both observable
    unless the proof holds, so they are semantics work, not oracle edits.

  **Implementation status (2026-09-06, later): (A) ratified as CW34 and
  landed** (`vibe/Lambda_Design_Runtime_COW.md` §11.11; **D4.4.4**, spec
  1.46.0). The static shape is decided once in `build_ast`
  (`lambda_ast_lower_rmw_borrows` at FUNCTION_END → `NameEntry::
  cow_borrow_lowered` on the handle, `AstAssignNode::cow_borrow_release` on
  every store-back); the runtime spine test `cow_bind_rmw_handle` takes the
  borrow only when root and intermediate links are unshared; MIR models the
  handle as `cow_marked` (bit-consulting stores) and T0 calls the same
  helper. Fixtures: `test/lambda/proc/cow_rmw_borrow.ls` (nine shapes,
  golden byte-identical on three tiers and to the pre-CW34 build) and the
  emission pin `test/mir/lambda/cw34_rmw_borrow` (arr_set: two
  `cow_bind_rmw_handle`, no `cow_bind_var`/`cow_capture_value`).
  `COW_EXEC_PROFILE` copies per run: **havlak arrays 204 687 → 41 168**,
  awfy deltablue arrays 48 960 → 38 880; cd, splay and jetstream deltablue
  unchanged — cd's root arrives through a plain-parameter chain (the copies
  are S9.1.3's), splay's rotations have no store-back, jetstream deltablue
  writes through plain parameters (see §11.11 "Not covered").
  Two **pre-existing JIT defects** surfaced by the probe (present on the
  pre-CW34 binary, T0 correct) — **both fixed 2026-09-07**
  (`test/lambda/proc/jit_tail_if_push_error.ls`, golden tier-agreed):
  (1) a `pn` body whose tail `if` needed boxing (braced arms, a `null` arm
  against an index read) returned `null`: `transpile_if`'s proc-mode
  `proc_discard` treated the tail `if` as a discarded statement; the content
  lowering now sets `preserve_proc_if_result` around a proc block's tail
  `if` (S16.4.1v3, S12.1.2 — the tail is the block's value, braced or not).
  (2) `push(vals, k)` on a numeric-array handle fails softly ("expected a
  growable array"); the COW push/splice arm republished `pn_push_cow`'s
  ERROR Item as the binding, so `t.vals = vals` stored the error and
  `len(vals)`'s argument boundary handed it to the native return lane →
  `inf`. The arm now publishes the helper result only when it is not an
  error and keeps the boxed owner otherwise (S7.4.1/S7.4.5: a sys-func
  failure is a value the statement discards, as T0 already did).
  Measured (release, interleaved ×5, `temp/lambda_t33_final.exe` →
  `temp/lambda_t34_final.exe`): **havlak 110.7 → 70.8 ms (0.64x)**,
  **havlak2 114.0 → 74.6 ms (0.65x)**; deltablue/deltablue2 0.99/0.98; cd,
  cd2, splay, json, json2, richards, hashmap, crypto_aes, nbody, gcbench2,
  binarytrees, matmul, brainfuck, quicksort, permute, towers 0.99–1.02.
  Gates: `auto`/`jit`/`interp` sweeps 0 regressions (755 scripts, the new
  fixture included); `make test-lambda-baseline` green with the emission
  pin; `make interp-sweep` partition regenerated.
  (B) — the moved argument for `x = f(x)` — remains open for its own ruling.

### T21-2 — Untyped lane parity (the biggest lever)

**Implementation status (2026-09-02): T21-2a landed as six call-site
inference edits in `transpile-mir.cpp`; T21-2b was already covered (the
counted-loop lowering fires for int-lane bounds — sieve's `for i in 2 to sz`
emits no `fn_to`/`item_keys`, and only bounce/havlak/sieve use dynamic bounds
at all); T21-2c and T21-2d landed 2026-09-05, T21-2e 2026-09-06 (status below the 2a notes).** The dumps said exactly which witness each
row lost, and every fix is an oracle edit, not a new mechanism:

| Row | before → after (ms, release, quiet, ×3) | witness restored |
|---|---:|---|
| r7rs/mbrot | 10.5 → **0.70** (15x; typed 0.69) | `float(x)` typed by its sys-func success type at the call site; the caller's forwarded `r/i/step` reach `count` through the closed edge kept across rounds (`closed_scalar`) |
| larceny/ray | 8.9 → **1.55** (5.8x; typed 0.30) | an element read `sx[si]` passes its array's element lane at the call site |
| larceny/puzzle | 14.5 → **2.75** (5.3x; typed 3.1 — untyped now faster) | `n - 1` and loop-counter locals typed by recursion over call-site-typed operands |
| r7rs/nqueens | 1.79 → **0.98** (1.8x; typed 1.46) | same |
| awfy/permute | 0.74 → **0.49** (1.5x; typed 0.13) | a subscript key counts as numeric use, so `swap(v, i, j)`'s closed INT edge attaches |
| kostya/levenshtein | 35.1 → **24.5** (1.4x; typed 6.3) | arithmetic/local typing at call sites |
| beng/spectralnorm | 24.1 → **17.0** (1.4x; typed 1.66) | a closed INT edge is vetoed only by direct FLOAT evidence, not by a float literal elsewhere in the body (`n` in `while (i < n)`) |
| awfy/towers | 1.17 → **0.90** (1.3x; typed 0.40) | element witness no longer poisoned by the recursive self-call (an unknown-yet element skips the join like an unknown-yet scalar does) |
| jetstream/crypto_sha1 | 66 → **54** (1.2x; typed 30) | same |
| quicksort, nbody, raytrace3d, list, json, hashmap, sieve, primes, fib, fft, matmul, bounce, deltablue2, gcbench2, spectralnorm2, ray2, matmul2 | flat | no regression |

**A latent misrecording the relaxation exposed.** The prepass recursed into
`start(f, [args])`'s inner call as an ordinary direct call, joining the
argument *array* as `f`'s first parameter. That was inert while the array
witness required an index use in the callee; with the relaxation, `child`
in `test/lambda/conc/scope_loop_exit.ls` inferred `value: int[]` and read the
dispatched scalar as an array (both tiers). The `AST_NODE_START` prepass arm
now visits the callee as an escaping reference and the arguments as
expressions only; the concurrency scripts pass on both tiers.

**Two more guards the JIT-pinned sweep demanded.** The call-site scan
*skips* an argument it cannot type (T19-4's measured trade), so a closed
edge is only "closed among the typable callers". That is safe when the body's
own arithmetic backs the lane and unsafe otherwise, and two of the new rules
had reached the unsafe side: (i) the witness for a parameter that never
indexes its array (`f([1, 2, 3])` next to `f("hello")`,
`transpile_len_typed`), and (ii) a lane selected by index-key or
comparison use alone (`write_array(xs, "name", 99)` next to
`write_array(xs, 5.5, 99)`, `proc_invalid_member_access`). Both now consult
a per-position *contradiction* flag (`CallSiteEntry::concrete_conflict`):
a caller the AST types as a concrete non-scalar refutes the lane; a caller
nobody can type yet (a local call result, `min2(min2(a, b), c)`) is
uncertainty, not refutation, and still gets the lane. The honest join
`arg_types` itself was left as it was — it folds untypable callers to `any`,
and the direct-entry selection depends on that (skipping them there let a
deferred float call enter an int body, `proc_inferred_mixed_direct_entry`).
A third guard withholds the inferred array witness
when any subscript key on the parameter is not call-site-typed int
(`xs["name"]` through an inferred `int[]` witness lowered the string key
through the int-key lane): `FnParamEvidence::container_key_dynamic`, set by
a small walk over the body after the evidence pass. With these, the sweep of
every `test/lambda/**/*.ls` against the T21-1 binary is **0 regressions on
the JIT tier** (45 pre-existing) and the interpreter tier likewise.

**Open inside 2a — the `var` array-parameter witness chain.** Giving a
forwarding-only `var` array parameter the inferred witness made a witnessed
callee's typed store clone under a runtime shared bit while the caller's home
never saw the write (queens' `free_rows`, on both tiers, since the interpreter
falls back to the JIT for `var`-param scripts). Two gates keep it sound: a
forwarding-only `var` parameter keeps the boxed borrow, and the element lane
crosses a parameter hop only when that parameter itself resolved to the
witnessed array shape. This is what still separates quicksort (10.6 vs 1.09),
permute (0.49 vs 0.13) and towers (0.90 vs 0.40) from their typed twins. The
fix is not an oracle edit: either the raw witness path must detach the
caller's root without demoting its binding to `any` (`mir_prepare_cow_root`
does both today), or the CW33 home cell must be published for ArrayNum roots
(excluded today because the caller's register may hold the raw pointer
representation and the post-call reload would reinterpret it). Either is a
CW33/D5.2 design item, recorded here for the next round.

**Implementation status (2026-09-05): T21-2c — boxed arithmetic round trips
— landed.** The dumps showed the round trips were not at call/index boundaries
but at *type-oracle* boundaries: `(i + j) * (i + j + 1) / 2` already lowered
natively (`i2d` + `ddiv`) in eval_A, and the very next `+ i` boxed both sides
for `fn_add`, after which `float()`, `1.0 / …` and the return stayed boxed.
Every fix is again an oracle edit in `transpile-mir.cpp`, cited to the lane
rulings (S4.5.3, D2.4.1–D2.4.3, D3.3.1):

1. *Operand oracle, double lane.* `mir_native_arithmetic_operand_type`
   recovered a closed `int` tree for a native consumer but had no float arm;
   a binary `+ - * /` over int/float operands with a float involved (or `/`)
   now reports `float`, which is the descriptor `emit_binary_value`'s float
   block already publishes. The `float(x)` emitter asks that same oracle (it
   asked the plain carrier oracle, which still read the tree's stale `any`).
2. *Return-lane proof, prepass.* `mir_expr_proves_native_return_lane` admits
   a raw `float(x)` and the always-float native math builtins (`math.sqrt`)
   by proving their arguments through the *proof itself* — during the prepass
   an inferred parameter has no MIR binding yet, so the carrier oracle cannot
   see its lane (the int branch already relied on the same fact).
3. *Reassigned untyped locals.* The proof refused every reassigned `var`
   ("until mutation-aware lane tracking exists"). An unannotated var binds on
   its initializer's descriptor and keeps it unless an assignment widens it,
   so the proof now admits the binding when the initializer proves the lane
   and every write proves a value the assignment cascade stores without
   widening (the lane itself, or int into a float lane); a self-referencing
   write (`t = t + 1.0`) is proved inductively (`lane_proof_bindings`). The
   write scan is the existing `mir_nested_control_writes_binding` with an
   optional keep-lane argument. A live MIR entry must agree at emission (a
   loop-prewidened binding already carries an Item). `tune14_native_return`'s
   second check pinned the old refusal as a limitation and was updated.
4. *`and`/`or` over inferred-lane comparisons.* `t > 0.0 and t < min_t` kept
   an `any` AST type for each comparison and boxed the native 0/1 lane for
   `is_truthy`; the gate now shares the loop-condition emitter's predicate
   (`mir_numeric_comparison_native_lane`: ordered op, both operands native
   numeric and non-null).

| Row | before → after (ms, release, interleaved ×3) | what changed in the kernel |
|---|---:|---|
| beng/spectralnorm | 17.8 → **1.67** (10.6x; typed spectralnorm2 1.67 — parity) | eval_A returns `d`, body has no fn_add/fn_float/fn_div/push_d; mul_Av's inner loop is one native call + native fma |
| larceny/ray | 1.60 → **0.30** (5.3x; typed ray2 0.30 — parity) | sphere_intersect returns `d` (reassigned `var t`), the caller's `t > 0.0 and t < min_t` is native, no is_truthy/it2d/push_d in the loop |
| larceny/puzzle | 2.73 → 2.41 (0.89) | comparison gate |
| 27 other rows (mbrot, nbody, raytrace3d, matmul, nqueens, permute, towers, fft, bounce, json, list, richards, deltablue2, gcbench2, quicksort, fib, sieve, havlak, binarytrees, fannkuch, mandelbrot, primes, splay, cube3d, hashmap, levenshtein, spectralnorm2, ray2) | flat (0.93–1.03) | no emission change |
| jetstream/crypto_sha1 | 47.7 → 53.5 (1.12, quiet re-run ×3; 1.19 under load) | **MIR byte-identical** modulo addresses on both debug and release binaries; see the note below |

Gates: `test/lambda` sweeps against the T21-2 debug binary 0 regressions on
both tiers (7 pre-existing on JIT); `make test-lambda-baseline` all green
after the tune14 sidecar update (emission suite 67/67); every benchmark row
matches its golden on the release except nbody and cd2_orig, which fail
identically on the control (stale goldens). New fixture
`test/mir/lambda/tune21_float_lane_roundtrip.*` pins all four mechanisms.

*What 2c does not cover.* levenshtein (24.6 vs typed 6.3) is not arithmetic:
its `prev`/`curr` rows are swapped through a `var tmp`, which demotes both
bindings to `any` and routes every `prev[j]`/`curr[j] = …` through
`fn_index`/`fn_array_set` (15 `int2it_lane`); that is a binding-lane item
(D3.3.1), not a round trip. crypto_sha1's remaining `fn_add ×4` are the u32
builtin results (`shl(...) + 16`, `input_len + 64`) in core_sha1's prologue,
outside the hot loop; the u32 lane has a pre-existing tier divergence
(`u32 + int`, §T21-1) and was left alone. The 1.12x reading is therefore a
*link-layout* effect: the two release binaries differ only in
`transpile-mir.o` (every other object was reused by the incremental release
build), the JIT'd module is byte-identical, and the row's time is dominated
by calls into runtime helpers (`int2it_lane ×66`, `lambda_int_lane_add_slow`,
`fn_index`) whose placement moved. The same row has read 66, 54, 48, 57 and
53 ms across the last five builds with unchanged emission — treat crypto as
layout-sensitive and never attribute a ≤20% swing on it to an oracle edit
without a dump diff (§6.2).

**Implementation status (2026-09-05): T21-2d — the inferred witness through
`var` array parameters — landed.** This closes the item 2a left open above.
The "lost write-through" diagnosis was wrong, and finding that out was the
whole fix: with the forwarding-only gate removed, queens' `set_row_column`
did receive the witness and its stores did land in the caller's array — they
never executed. A guarded store (the callee cannot prove `v` is a bool) took
the DECLARED-contract checked store, `emit_typed_array_store_fallback`, with
`root->full_type` as the array contract. For an untyped pn parameter that
field is the parameter's implicit `any \ error`, which is not an array
contract, so `lambda_array_set_checked_inplace` rejected every element with
E201 ("expected any \ error, got bool") and the callee left through the error
lane before its first store. The benchmark swallowed the error and reported
a solver that "found" eight queens in row 0. Four edits (D3.2.1, D3.3.1,
CW33):

1. `emit_array_num_store_fallback` reserves the checked typed store for a root
   whose `full_type` is an array occurrence; every other root (an inferred
   witness) takes the representation-agnostic arms, whose `fn_array_set`
   widens the packed lane in place like any untyped array (a string into an
   inferred `int[]` witness prints `[0, "x", 0]` on both tiers).
2. The `borrowed_forward_only` gate in `infer_param_types_batched` is gone:
   a forwarding-only `var` parameter takes the witness like any other.
3. The subscript KEY of an indexed store is numeric use (`rows[r] = v`); 2a's
   edit 4 covered only read subscripts, so `r` stayed `any`, the boxed key
   sent the store through `index_assign_cow`, and the callee never reached
   the typed lane even with the witness.
4. The call-site typer returned the zeroed sentinel (`LMD_TYPE_RAW_POINTER`)
   for an enclosing parameter no round had resolved yet, joining a bogus
   concrete type into the callee's specialization and poisoning the edge for
   the round; it now answers "unknown" (which the specialization join skips).
   With that, `MIR_CALLSITE_MAX_ROUNDS` 3 → 6 (the loop exits when a round
   changes nothing) lets towers' four-hop chain benchmark → move_disks →
   move_top_disk → push_disk converge.

| Row | before → after (ms, release, interleaved ×3) | kernel |
|---|---:|---|
| larceny/quicksort | 10.54 → **0.99** (10.6x; typed quicksort2 1.06 — parity) | partition/quicksort: `fn_index` 6 → 0, `fn_array_set` 5 → 1 |
| awfy/permute | 0.48 → **0.27** (1.8x; typed 0.13) | swap through the witness |
| awfy/towers | 0.88 → **0.69** (1.3x; typed 0.38) | all four pile/top functions carry the witness; `fn_index`/`index_assign_cow` 5 → 0 |
| awfy/queens | 0.41 → 0.40 (typed 0.34) | `fn_index`/`fn_array_set` 6 → 0; the row is dominated by `ensure_typed_array` re-admission at every hop (28 per solve) |
| 33 other rows incl. every typed twin | flat (0.97–1.02) | raytrace3d and levenshtein each showed one slow pair with byte-identical MIR; flat on the quiet re-run |

Gates: `test/lambda` sweeps against the T21-2c binary 0 regressions on both
tiers (7 pre-existing on JIT), `make test-lambda-baseline` green (emission
68/68 with the new fixture `test/mir/lambda/tune21_var_witness_forward.*`),
every benchmark golden matches on the release except nbody/cd2_orig (stale,
identical on the control).

*What 2d leaves.* The per-hop `ensure_typed_array` re-admission on witness
arguments (identity for a matching ArrayNum, but a call per array argument
per call — queens 4 → 28, towers 6 → 16) is what separates queens (0.40 vs
0.34), permute (0.27 vs 0.13) and towers (0.69 vs 0.38) from their typed
twins now; the typed path skips it through `proven_array_witness`, which an
inferred parameter's binding does not satisfy after a MAY_GC call. That is a
cache-validity question (D3.3.1), not an inference one. The typed contract
also rejects an array LITERAL for a `var t: int[]` parameter statically
(E207) while the inferred witness admits it — `[0, 0, 0]` builds an ArrayNum,
so the admission is the identity and write-through holds; worth a ruling
whether E207 should relax to match.

**Correction to the 2d residual.** The typed twins emit MORE
`ensure_typed_array` admissions than the untyped rows (queens2 38 vs 28,
towers2 34 vs 16), so re-admission is not what separates them. permute and
towers are separated by `st.count = st.count + 1` / `st.moves` on an untyped
map `var` parameter: the read is already a guarded packed load (T20-1c), but
the guard publishes a boxed Item on both arms by the D2.4 same-representation
rule, so the add is `fn_add` and the store `fn_map_set`. Lifting that is a
representation decision on the guard (D2.4), not an inference edit, and is
left for T21-4.

**Implementation status (2026-09-06): T21-2e — call-site string lane and
error-free argument boundaries — landed.** levenshtein's remaining 4x was not
arithmetic either: its typed twin indexes `s1[i - 1]` through
`fn_string_ascii_at` (one bounds check, one byte load), the untyped row
through `fn_index`, which allocates a one-character string, and then a string
`fn_eq`. Two edits (S4.1, D2.4, D3.2.1):

1. *String lane.* The inference had no string notion at all. The call-site
   typer now passes a statically string-typed argument (a literal, a typed
   local), the join sets `INFER_CALLSITE_STRING`, and a parameter with a
   closed string edge that the body only subscripts (`used_as_container`,
   never an indexed-store target — `FnParamEvidence::container_stored` — no
   arithmetic or numeric use, no concrete non-string caller, int keys only,
   never reassigned) resolves to `string`, the same String* lane a declared
   `s: string` takes. An untypable caller (levenshtein's `make_string` result)
   still enters the boxed wrapper, whose exact-shape guard already handled
   `string`, and reaches the slow body; an array-literal caller is a concrete
   conflict and withholds the lane (fixture `tune21_string_lane.*`).
2. *Error-free argument boundary.* Every untyped pn parameter carries the
   implicit `any \ error` contract, and a call site admits an `any`-carrier
   argument with `lambda_type_check`. A closed `+ - *` int tree over int-lane
   leaves (element read with an int witness and an int key, counter local,
   literal) cannot be an error Item — overflow widens on the checked slow
   arm, an out-of-range read yields the lane null — so
   `mir_argument_may_return_item_error` answers no for it and
   `mir_boundary_is_redundant` elides the check for exactly that contract
   (`TYPE_ANY_NO_ERROR` only; `any \ error \ null` still rejects the null).
   levenshtein paid three of those per inner iteration for its `min3`
   arguments; `idiv`/`mod` and calls stay excluded (fixture
   `tune21_int_tree_boundary.*`).

| Row | before → after (ms, release, interleaved ×3) | kernel |
|---|---:|---|
| kostya/levenshtein | 24.7 → **10.2** (2.4x; typed levenshtein2 6.2) | `fn_index`/string `fn_eq` → `fn_string_ascii_at`, three `lambda_type_check` per iteration gone; string lane alone was 19.9, the boundary elision took it to 10.2 |
| 32 other rows incl. every typed twin | flat (0.97–1.04) | no emission change |

Gates: `test/lambda` sweeps against the T21-2c binary 0 regressions on both
tiers (7 pre-existing on JIT); `make test-lambda-baseline` green with the two
new fixtures; every benchmark golden matches on the release except
nbody/cd2_orig (stale, identical on the control).

*What 2e leaves.* levenshtein's last 1.6x is T19-4's measured trade:
`min3(a, b, c)` only forwards its parameters to `min2`, so they stay boxed
and each call boxes three int lanes (`int2it_lane ×11`); the typed twin
declares them. The row swap (`var tmp = prev; prev = curr; curr = tmp`) is
NOT the cost — both twins lower it identically.

**Two pre-existing defects surfaced by the timing runs — both fixed
2026-09-06 (not introduced by this track).**

(a) *Widening inside a control region.* `r7rs/fft` stalled intermittently
(1–11 runs in 200 on every release back to the T21-1 binary, 4.7 GB RSS). The
assignment cascade widened an inferred int local to a boxed Item by switching
the binding to a FRESH register at the assignment (`j = j - m` with a float
`m`, inside the inner `while`). That register is undefined on every path that
skips the assignment — fft's first outer iteration has `j >= m` false, so
`j = j + m` read garbage — while the reads emitted before it (`j >= m`,
`i < j`) kept the stale int-lane register. The GC root slot held the garbage
too. The garbage occasionally decoded as an array Item and `fn_add` ran the
vector path over a phantom multi-gigabyte operand; otherwise it logged
`unknown add type: 144, 8`. A four-line probe reproduces it deterministically
(`var j = 0; if (flag) { j = j - 0.5 }; j + 1` printed a type error for
`flag == false`). Fix (D2.2.2, D3.3.1): the binding is widened at its
DECLARATION, the one point dominating every read and write —
`transpile_let_stam` scans the function body with
`mir_nested_control_writes_binding(…, cascade_widen)` for any assignment the
cascade would widen and boxes the initializer. Two supporting edits: the
loop pre-widening predicate `mir_assign_widens_native_binding` now says an
INFERRED int taking a float widens (it said "never", contradicting the
cascade, which is why the existing hoist never fired), and the carrier
oracle predicts a not-yet-bound unannotated local by its initializer (so
`j - m` is seen as float before `var m = n / 2` is emitted). The cascade's
late switch now logs `mir: late widening …`; a corpus scan with stderr
captured found none. Fixture `tune21_var_widen_at_declaration.*`; 0/300
fft stalls on the release afterwards.

(b) *Lane null in arithmetic.* `fill(3, 5)[10] + 1` printed `inf` on the JIT
and `null` on the interpreter: the int-lane null sentinel (ItemNull's bits,
2^56) is out of band and always reaches `lambda_int_lane_{add,sub,mul}_slow`,
which classified NaN/Inf but not null and saturated it. They now propagate
the null, as the div/mod classifier already did (S7). Same fixture, `h()`.

| Row | before → after (ms, release, interleaved ×3) | note |
|---|---:|---|
| r7rs/fft | 0.095 → 0.250 (2.6x slower; typed fft2 0.20) | the old number was a miscompiled bit reversal; `j` is now a boxed Item through that loop (fn_ge/fn_sub/fn_add ×3, is_truthy ×3) because Lambda's `n / 2` is a float and the binding's runtime type genuinely changes from int to float — a boxed carrier is the only one that keeps `j is int` honest across the switch |
| 32 other rows incl. every typed twin | flat (0.95–1.04) | the declaration scan widened nothing else in the suite; the `late widening` marker never fired across test/lambda and test/benchmark |

The fft cost is the correctness price of an untyped int-then-float local; a
native carrier for it would need an int-or-float lane with a runtime tag,
which is a value-model item (S4.5), not a lowering one. fft2 keeps its int
lanes because it declares `j`, `m` as `int` and divides with `idiv`.

Per §1, ten untyped rows are 3–30x their own typed row. The mechanisms are
known and none needs annotations:

- **T21-2a Scalar param lanes that stop at the first unproven argument.**
  untyped `ray` already enters with `d:%p1, d:%p2, d:%p3, i64:%p4`; inference
  specializes then gives up on the `sx[s]` element reads, after which the loop
  runs `push_d ×20`, `fn_mul ×10`, `it2d ×10` (dump census). The typed row
  runs the same loop with zero `push_d`/`fn_mul`. Fix shape: the T19-3 counted
  loop lane witness plus a **per-call-site element witness** for arrays that
  the caller created with `fill(n, <float>)` — the caller knows the lane; pass
  it as the array-witness argument the dual-entry ABI already has
  (`_array_witness` in every `_f_N` satellite). Tune19 §8.1 rejected the
  *transitive scalar* edge at 1.02 geomean; this is the *container-lane* edge,
  one level, from construction sites only (same constraint as T20-1's "no
  transitive closed-caller edges").
- **T21-2b Dynamic-range `for` loops** (Result18 T-B, still open): an untyped
  bound lowers the loop through `fn_to`/`item_keys`/`iter_val_at`. The bound is
  almost always an `int` lane at the site (`len(x)`, a param proven int by
  T19-3); lower `for i in a to b` to the counted form whenever both ends carry
  an int rep at the site, with the boxed form as the guarded fallback.
- **T21-2c Int-lane arithmetic fallbacks.** quicksort untyped: `int2it_lane
  ×30`, `lambda_item_to_int_lane_c ×6`, `fn_index ×6`, `fn_array_set ×5`;
  typed: 14 / 7 / 0 / 0. spectralnorm untyped: 14 / 7 plus `push_d ×9`. These
  are the boxing round trips at call and index boundaries that T20-3's
  companion-lane mechanism (return-convention v3) is designed to remove; the
  untyped rows are its best customers because they have no contract to check.

Gate for the track: no untyped row > 2x its typed row (today: 14 rows above
3x); MIR-U/Node ≤ 1.0x. Measure each sub-track at ≥ 3 alternating pairs against
`temp/lambda_head_release.exe` (Tune19 §8.4-2).

### T21-3 — The auto tier promotes nothing untyped (needs a D8.1.1 revision)

**Implementation status (2026-09-06): landed as D8.1.1v6 (spec 1.45.0;
`vibe/Lambda_Design_Ast_Interpreter.md` §5.2.1).** Two admissions in
`interp_plan.cpp`: `interp_satellite_supported` no longer refuses a plain
`any` parameter (the satellite is entered through its boxed `_b` wrapper, so
there is no raw carrier to mis-decode, and a body-inferred lane is guarded by
the wrapper's exact shape test with the boxed slow body behind it), and
`interp_scan_satellite_node` no longer refuses local `var` declarations and
plain rebinding assignments (a promoted body owns its whole activation; this
second pin was what kept hyphen/sum/tak's bodies in T0 even with the
signature admitted). Aggregate/structured contracts, `var` parameters,
indexed/member stores, nested definitions, indirect Lambda calls,
object-field identifiers and match expressions stay pinned as in v5.

The widening exposed two latent satellite defects, both fixed: (1) a
satellite resolves literals through the T0 module *state*, whose const image
was bound at module init to the const list's buffer of that moment; T0 never
interns literals, the satellite lowering does, and the append reallocated the
list — `compile_ast_function_satellite` now rebinds the state's static image
after every satellite (pdf `path.ls` `apply_op` crashed in `fn_eq` on a
garbage string pointer); (2) the >3-argument dynamic-call builder appended
with `array_push`, which splices a content-list argument (a `split()` result
reached `fn_call_into` as several arguments: `build_kv_pairs expects 4
arguments, got 5`); the new verbatim `array_push_argument` is the appender
(the eager JIT's own dynamic calls had the same latent defect).

| Row (untyped) | JIT e2e | auto e2e before | auto e2e after | after/JIT | note |
|---|---:|---:|---:|---:|---|
| text/hyphen | 22.2 | 63.8 | **19.5** | 0.88x | gate met |
| r7rs/sum | 12.1 | 150.7 | **12.8** | 1.06x | gate met |
| r7rs/tak | 11.9 | 49.8 | **10.6** | 0.89x | gate met |
| r7rs/fib | 12.4 | 213.9 | **11.6** | 0.93x | |
| r7rs/mbrot | 14.6 | 112.8 | **23.3** | 1.60x | |
| beng/binarytrees | 23.2 | 116.9 | **27.5** | 1.19x | |
| kostya/levenshtein | 29.4 | 758.6 | 296.4 | 10.1x | `levenshtein`'s indexed stores keep it pinned; `min2`/`min3`/`make_string` promote |
| larceny/quicksort, awfy/permute | 15.6 / 13.5 | 126 / 27 | 125 / 26.5 | 8.0x / 2.0x | `var` array parameters (the CW33 gap) |
| beng/nbody | 59.3 | 968.7 | 959.3 | 16.2x | indexed stores through untyped arrays |
| primes, sieve, queens, towers | ≈ | ≈ | ≈ | ≈1x | already at parity or pinned identically |

(ms, wall clock from process start to exit, median of 3, release, quiet.)
Gates: auto-tier differential 751/751 exact against the pre-widening
binary; forced `jit`/`interp` sweeps 0 regressions; every benchmark golden
matches on the auto tier except five that fail identically on the
pre-widening binary (cd2_orig, nbody stale goldens; fasta, spectralnorm,
matmul are pre-existing T0 divergences — matmul's `sum=0` is already in the
T21-1 ledger). `make interp-sweep` and `make test-lambda-baseline` under the
unset AUTO default: see the closing summary below the table.

**T21-3b (2026-09-06, landed): satellite write-back for indexed stores and
untyped `var` parameters** (impl `vibe/impl/Lambda_Impl_Ast_Interp (done).md`
§3.0.58; design §5.2.1 "Write-back across the tier boundary"; D8.1.1v6 text
extended in place). Indexed/member stores are simply admitted by the scan (a
promoted activation owns its registers; module stores go to the shared slab;
a store through an untyped `var` parameter is published by the CW33
epilogue). Untyped `var` parameters cross both tier edges through the CW33
home-transport cells: T0 publishes its frame-slot addresses and calls the
promoted callee through the new borrowed dispatch mode
(`fn_call_borrowed_into`), and a satellite's dynamic call publishes its
rooted slots and reloads them, with `interp_call_borrowed` consuming the
cells on the interpreted side. Typed `var` parameters (raw-lane ABI, no
home) stay pinned. A `var` local passed to an untyped `var` parameter is
bound boxed at declaration — which also fixed a pre-existing eager-JIT
divergence (`pn bump(var n) { n = n + 1 }` left an int-lane caller local
unchanged).

| Row (untyped) | JIT e2e | auto before (t27) | auto after (t30) | after/JIT | note |
|---|---:|---:|---:|---:|---|
| larceny/quicksort | 16.7 | 128.4 | **31.5** | 1.88x | `var` array params promote; rest = boxed satellite indexing |
| awfy/permute | 14.3 | 27.9 | **14.4** | 1.00x | parity |
| kostya/levenshtein | 31.2 | 305.7 | **216.8** | 6.9x | `levenshtein` now promotes (was pinned by its indexed stores); the satellite's `any`-param body indexes boxed — satellite call-site specialization is the residual, not write-back |
| beng/nbody | 62.8 | 1017.8 | 1018.7 | 16.2x | **correction:** `advance`/`energy` take typed `float[]` parameters — pinned by the aggregate-contract rule D8.1.1v6 keeps, not by indexed stores |
| hyphen, sum, tak, fib, primes, queens, towers, sieve, mbrot, binarytrees | ≈ | ≈ | ≈ | 0.87–1.6x | unchanged |

(ms, wall clock, median of 3, release `temp/lambda_t30_final.exe`, quiet.)
Gates: `auto`/`jit`/`interp` sweeps over `test/lambda` 0 regressions (753/754
scripts, incl. the two new fixtures); release goldens: jit 111/113 (cd2_orig,
nbody stale), auto 108/113 (the same two plus the pre-existing fasta,
spectralnorm, matmul T0 divergences) — identical sets to before;
`test/lambda/proc/interp_var_writeback.ls` exact on all tiers.

⚠ Found on the way (not a satellite defect): the 15:54 merge of remote master
brought upstream `ecca6370a` ("dynamic attr `[expr]:val`"), whose
`elmt_literal_begin` resolved a computed-key/spread element literal's
`type_index` against the *running script's* type list instead of the defining
module's — 50 `graph/*` scripts segfaulted in `fn_map_set` on every tier
(and `make release` failed on a write-only counter in `lambda-error.cpp`).
Both fixed here (`elmt_with_type`, `depth` reused); fixture
`test/lambda/elmt_literal_module.ls`. Lesson: a block of same-tier-everywhere
regressions after a `git log` merge is the merge, not the branch under test.

*Next for the auto tier*: satellite call-site specialization (levenshtein,
quicksort residual — the satellite lowers an `any` parameter boxed where the
eager module compiler would have witnessed an int/array lane through its
prepass), typed aggregate parameters (nbody, ray2, array1 — the v6
aggregate-contract pin), and the once-called-`main` hot loop (the secondary
item above).

`interp_satellite_supported` ([interp_plan.cpp:1874](../../lambda/runtime/interp_plan.cpp))
refuses promotion when any parameter's contract is `any`, array, map, element,
object or a structured type. D8.1.1v5 records this as "P2 fails closed for
aggregate/structured signatures", justified by the satellite's raw-carrier
specialization mis-decoding aggregates. Two consequences the report makes
visible:

1. Every untyped `pn f(x)` is pinned to T0 forever (`reason=satellite-boundary`
   in the debug log), so the shipped auto tier interprets the hot loops of
   most untyped scripts: hyphen 64 ms e2e vs 3.3 ms JIT, sum 155 vs 0.82,
   tak 53 vs 0.13, diviter 107 s vs 263 ms.
2. Typed rows with array params are pinned too (ray2 157 ms vs 0.30, array1
   210 vs 0.81), which is why part 2's typed/Node is 2.79x while part 1's is
   0.85x.

Proposal (a ruling, then code): **D8.1.1v6** — a definition whose signature
fails the raw-carrier specialization test is still promotable to a satellite
compiled with the **boxed dynamic ABI for every parameter** (the same entry
`lambda_dynamic_call` already uses for multi-argument dynamic calls). The
mis-decode hazard the v5 text describes only exists for the specialized entry;
the boxed entry carries Items and cannot mis-decode. `var` parameters stay
pinned until CW33's home-transport ABI is available to satellites (that is
the one genuine write-back channel gap). Gate: hyphen/sum/tak auto e2e ≤ 2×
their JIT e2e; T0/JIT differential suite unchanged; no new `pinned function`
line for an all-scalar or all-`any` signature.

Secondary: back-edge promotion is deferred to the *next entry* (no OSR), so a
script whose hot loop is in `main` never promotes at all (hyphen2 typed: 27 ms
auto vs 1.85 JIT; 8 E201 errors were raised in T0 before promotion in the
pre-fix source). Loop-entry OSR is explicitly out of scope in D8.1.1v5; the
cheap alternative is the whole-script route (`LAMBDA_AUTO_WHOLE_SCRIPT=1`) —
worth measuring as the default for import-free scripts whose `main` owns the
loop, since it is already implemented and gated.

### T21-4 — The container tail (carry-over from Tune20, restated on v36 numbers)

The rows ≥ 15x C2MIR are the same family Tune20 profiled: deltablue 101x,
havlak 89x, cd 35x, hashmap 31x, cube3d 27.5x, hyphen 22.6x (now 731008-
correct), base64 20.7x, queens 19.4x (now doing work), towers 17.1x,
knucleotide 17.0x, json_gen 15.7x, splay 15.6x, richards 15.5x, json 15.0x.
Tune20's four clusters (map field protocol, boxed arithmetic dispatch,
per-element admission, allocation rate) still describe them; two things are
new since v34:

- **COW marks are now a fifth cluster** (§2.5): capture/prepare/bind calls in
  the object rows' dumps. T21-1d's exclusivity proof is the entry point; the
  full answer is CW30-style compile-time sharing analysis on the graph rows
  (where the sources deliberately alias — see Tune20 §2.3's correction).
- **The graph benchmarks were rewritten for `var` semantics** in `56ab0a571`
  (cd2 973 → 663 lines, richards and splay restructured), so their v34/v35
  cells are not comparable and Tune20's per-row targets need re-baselining
  on the new sources before any claim.

The T20-3 companion-lane mechanism (member-result lanes were **refused** as a
slice on soundness — do not re-attempt as a slice) and T20-1's guarded shape
resolution remain the ranked answers; nothing in this round changes that
order. Re-profile on `temp/lambda_head_release.exe` before picking a row.

**Re-profile (2026-09-06, release after T21-1..3, `sample` 2 s on ×10
repeat variants, leaf symbols):**

| Row | top leaf symbols (samples) | mechanism |
|---|---|---|
| richards | `fn_map_set` 135, `fn_index` 82, shape walk (`shape_entry_uses_native_lane` 51, `type_field_storage_type_id` 40, `map_shape_field_to_item` 37, `map_field_to_item` 33), map-get statics ~200, `fn_member_by_id` 38, `cow_path_set_raw` 34 | every store is `w.field = v` through a **`var` parameter** (`pn tcb_run_task(var w, …)`), i.e. the CW33 borrow path: 104 `cow_path_set_raw` sites, 10 `fn_map_set`. The T20-1d guarded store cannot fire on a `cow_marked` root. **T21-1d / CW30 sharing analysis** is the only lever; not an oracle edit |
| deltablue2 | `fn_index` 38, `item_at` 16, map-get statics ~60, `fn_map_set` 13, `cow_prepare_write` 8 | same family (`type Variable = any` by design, Tune20 §2.3) |
| hashmap | **`fmod` 188**, shape walk 127, `fn_index` 54, `lambda_numeric_boundary_admit` 40, `fn_member_by_id` 36 | `hash % hm.cap`: the divisor is a map field read, so the `%` takes the boxed `fn_mod`, whose in-band int arm computed through libm `fmod` |
| havlak | **`gc_mark_item` 411, `gc_collect_with_root_region` 216**, `array_push` 91, `array_set` 42 | allocation rate: 27 collections, 38 ms of marking in a 150 ms run — T20-5's cluster, not T21-1a's pacing (which only spaces the object-pressure floor) |

**Implementation status (2026-09-06): one slice landed.** The boxed
integral division arm (`int_integral_division`, lambda-eval-num.cpp) now
computes `%`/`div` with the machine remainder when both operands are in band
(|x| ≤ 2^53, which is every compact int Item): exact and identical to the
`fmod` result (truncation toward zero, dividend-signed remainder), one
instruction instead of a libm call; the out-of-band arm keeps `fmod`. Probe
`temp/t4/m1.ls` pins signs, zero divisors, the 2^53 boundary and float
operands identical on both tiers and against the previous build.

Measured (release, interleaved ×3): **hashmap 174 → 160 ms (−8%)**; the other
25 rows flat (0.96–1.04), `fmod` gone from hashmap's top slot. Sweeps 0
regressions on both tiers.

The other three rows are design items this track deliberately does not
slice: richards/deltablue need CW30-style sharing analysis so a `var`-borrowed
root can take the guarded store (T21-1d), havlak needs the allocation-rate
work (T20-5), and the map-get statics are T20-1's remaining shape-walk cost.

**Implementation status (2026-09-06, later): the T20-5 slice for havlak
landed — nursery trigger pacing.** The 27 collections were all *nursery*
(data-zone) threshold collections: the trigger was a fixed 3 MB of
data-buffer churn (`GC_DATA_ZONE_THRESHOLD`), every collection marks the
whole object heap, and havlak's live graph grows through the run (21k → 65k
objects), so it paid a full mark per 3 MB of worklist growth. The existing
productivity rule in `gc_collect` never grew the nursery because each cycle
freed ≥ 75% of it — the rule only spaces *unproductive* collections. T21-1a's
object-pressure pacing did not apply either (that trigger's floor is 16 MB of
object bytes; havlak peaks at ~5 MB).

The fix (`gc_heap.c`): `gc_data_alloc`'s trigger now times the cycle and
paces `gc_threshold` by the same 10% time budget as the object trigger,
through a shared `gc_paced_floor` (×2 while the collector's share of the
mutator time since the previous nursery collection exceeds 10%, ×4 above
50%, cap `GC_DATA_ZONE_THRESHOLD_CAP` = the productivity rule's 256 MB).
**The cost that is paced is the mark phase only** (`gc->tune.mark_nanos`
delta across the callback), not the whole cycle: marking is the fixed
per-collection cost a larger nursery amortizes, whereas compaction and the
reset `memset` scale with the nursery itself, so a total-cost share never
converges for a churn-heavy script with a tiny live set — the first cut
paced on total cost ran brainfuck's nursery to the 256 MB cap (+100 MB RSS,
714 → 17 collections, no speedup) while havlak was already at 5. On the
mark-cost rule brainfuck stays at 3 MB / 714 collections (its marks cost
0.5 µs), cd and deltablue stay put, havlak grows to 48 MB.
`LAMBDA_GC_STATS=1` now also prints `collections`, `data_threshold` and
`object_threshold` at exit.

| Row | collections before → after | JIT ms before → after (interleaved ×5) | peak RSS |
|---|---:|---:|---:|
| awfy/havlak | 27 → 5 (mark 40 → 16 ms) | 606 → **304 (0.50x)** | 65 → 99 MB |
| awfy/havlak2 | 27 → 5 | 811 → **331 (0.41x)** | |
| gcbench2 | 5 → 6 | 432 → 406 (0.94) | |
| brainfuck / brainfuck2 | 714 → 714 | 0.97 / 1.00 | 195 → 196 MB |
| cd / cd2, deltablue / deltablue2, fast_diff, crypto_aes, gcbench, splay, hashmap, raytrace3d, cube3d, binarytrees, json, richards, levenshtein, nbody | unchanged | 0.95–1.03 (five rows first read 1.05–1.09 and re-read 0.98–1.03 at ×7) | |

Gates: `auto`/`jit`/`interp` sweeps 0 regressions; `make test-lambda-baseline`
green (includes the forced-GC stress corpus). Dev doc
`doc/dev/lambda/LR_08_Memory_and_GC.md` §5 re-verified (both triggers, both
rules). Binaries `temp/lambda_t32_final.exe` / `temp/lambda_t32_debug.exe`
(control `temp/lambda_t30_final.exe`).

*Residual on havlak* (now mutator-bound): `array_push`/`expand_list` growth
churn of the worklists and the `fill(16/32, null)` slabs of its 3-level
indexed arrays — allocation *volume*, T20-4/T20-1 material; brainfuck's 714
cheap collections are the nursery reset (`memset` of the churn) and a sweep
per 3 MB — a lazy-zeroing nursery is the lever there, not pacing.

**Implementation status (2026-09-06, later still): lazy-zeroing nursery
landed (brainfuck).** `gc_data_zone_reset` no longer memsets the reused
nursery; `gc_data_alloc` meets the zeroed contract at allocation time
(cache-warm, right before the caller touches the buffer), so every existing
`heap_data_alloc`/`heap_data_calloc` caller is unchanged and no audit was
needed — `heap_data_calloc`'s own memset was a third zeroing pass and is gone.
A caller that writes every byte before any read or GC safepoint takes the new
`gc_data_alloc_uninit` → `heap_data_alloc_uninit` → `array_num_new_uninit`
chain: `fill`'s int/uint64/float/bool lanes and the varargs `array_*_fill`
helpers (the `ArrayNum` constructor is one `array_num_new_with_extra_init`
with a `zeroed` flag). brainfuck's tape (`fill(30000, 0)` per `run_bf`,
10 000 calls = 2.4 GB of nursery churn = its 714 collections) was zeroed at
reset, zeroed by calloc, then written by fill; it is now written once.

| Row | JIT ms t32 → t33 (interleaved ×5) |
|---|---:|
| kostya/brainfuck | 312 → **287 (0.92x)** |
| kostya/brainfuck2 | 462 → **433 (0.94x)** |
| kostya/matmul | 35.8 → **31.0 (0.87x)** (fill-built matrices) |
| beng/binarytrees | 17.2 → 15.1 (0.88x) |
| havlak, cd, fast_diff, gcbench, gcbench2, json, hashmap, splay, nbody, fasta, sieve, richards, cube3d, deltablue | 0.96–1.04 |

Gates: `auto`/`jit`/`interp` sweeps 0 regressions; forced-GC stress corpus
102/102; `fill` probe (int/float/bool/string/empty/2^53−1 lanes) identical to
the previous build on both tiers; `make test-lambda-baseline` green. Binaries
`temp/lambda_t33_final.exe` / `temp/lambda_t33_debug.exe`.

*Residual on brainfuck* (714 collections still, 0.4 ms each): each cycle
compacts the one live 240 KB tape nursery→tenured (`gc_data_zone_copy`) and
sweeps the object heap; **the tenured data zone is never reclaimed** (only
destroyed with the heap), so 714 dead tape copies = the 195 MB peak RSS. A
tenured collection (or not promoting a buffer that survives exactly one
cycle) is the next lever — memory first, time second.

### T21-5 — Benchmark and report hygiene (small, do first)

- Every benchmark prints PASS/FAIL against a golden (fast_diff and microdiff
  still print only a checksum; the runner only looks for `FAIL`). Add the
  expected checksum to each, as hyphen now does.
- The runner should record the Lambda commit **per merged cell**, not only in
  the header; `merged_engines` has the data.
- `make release` must not delete `lambda/mir/c2m`, or the runner must rebuild
  it (`make c2mir-driver`) before a C2MIR run.
- Archive the v36 binary under `test/benchmark/exe/` (the v35 archive named in
  Result35 is gone; this round rebuilt it from source).
- Typed crypto_sha12 fails on the `176bdd934` and `33a178ed0` builds with
  `mir-value: unavailable representation transition 1 -> 2 in _core_sha1_2024`
  and passes on HEAD — a representation-conversion pair that was missing for
  one commit window. Pin it with a fixture so it cannot regress silently
  (D8.6.2).

**Implementation status (2026-09-06): all five items closed.**

1. `fast_diff`/`fast_diff2` and `microdiff`/`microdiff2` now compare their
   checksum against the expected value (748544 / 3278848) and print
   `FAIL checksum=…` otherwise, the pattern hyphen uses; goldens unchanged,
   verified on `jit` and the auto tier.
2. `merge_engine_results.py` records `source_lambda_commit` in each
   `merged_engines` entry (the source run's `_metadata.lambda_commit`), and
   `gen_overall_result.py` prints it in the "Separately measured" line, so a
   merged cell names the Lambda commit it was measured on.
3. `c2m` after `make release`: already handled on the runner side —
   `run_c2mir_benchmarks.ensure_c2m()` rebuilds the driver with
   `make c2mir-driver` when it is missing, and `run_benchmarks.py` calls it
   before any C2MIR run; the Makefile keeps the driver on demand by design, so
   nothing changed there. (The §6.3 recipe note stands for manual A/B.)
4. `test/benchmark/exe/lambda-v36-33a178ed0` archived (18,615,016 bytes,
   size-matched to `benchmark_results_v36.json`, SHA-256 `9c93e328b3cb5238`),
   manifest row added; the v35 archive is in fact present, and three older
   rows were marked as no longer on disk.
5. `test/mir/lambda/tune21_crypto_repr_transition.*`: typed crypto_sha12's
   `core_sha1`/`safe_add`/`rol`/`str2binb`/`binb2hex` lowered under the
   emission gate with SHA-1 digests of three strings as the golden (both
   tiers); the sidecar asserts the function lowers at all, which is exactly
   what the `176bdd934..33a178ed0` window failed.

## 4. What NOT to do (inherited and new)

- Everything in Tune20 §4 and Tune19 §8 stands: no inline caches (D8.4.1v2),
  no dynamic shape index, no transitive closed-caller or scalar-param edges,
  no shape interning for literals, no `is_trusted_contract` on inferred
  shapes, no C2MIR-path or vendored-dep changes, no typed contracts on the
  graph benchmark sources.
- **Do not fix T21-1c by re-publishing the bool read as `VALUE_REP_I64`.**
  That is the Result36 bug. The rep must be a distinct bool.
- **Do not "fix" T21-1a by raising `GC_OBJECT_HEAP_THRESHOLD`.** A larger
  constant moves the cliff; the trigger's *quantity* (live vs allocated-since)
  is what is wrong.
- **Do not edit benchmark annotations to close T21-2.** Parity means the
  untyped source gets faster, not that it gets typed (Result32's rule).
- **Do not measure the auto tier with `LAMBDA_TIER=jit` numbers.** Part 2 of
  the report exists for that; T21-3's gates are e2e.
- **Do not profile with `sample -i 1` on a loop copy and read the timing.**
  See §6.2.

## 5. Round targets

| Metric | v36 | after T21-1 (regressions undone) | round target | stretch |
|---|---:|---:|---:|---:|
| MIR (typed)/C2MIR geo | 5.67x | ~4.7x | **≤ 4.0x** | ≤ 3.5x |
| MIR (untyped)/C2MIR geo | 9.17x | ~8.3x | **≤ 6.0x** (T21-2) | ≤ 5.0x |
| MIR (typed)/Node geo | 0.85x | ~0.73x | **≤ 0.70x** | ≤ 0.60x |
| MIR (untyped)/Node geo | 1.37x | ~1.2x | **≤ 1.0x** | ≤ 0.9x |
| Auto e2e / Node e2e geo (untyped) | 4.13x | — | **≤ 2.5x** (T21-3) | ≤ 2.0x |
| Untyped rows > 2x their typed row | 14 | 14 | **≤ 4** | 0 |
| Rows > 20x C2MIR (typed) | 8 | 5 | **≤ 3** | 0 |

The "after T21-1" column is what the bracketing binaries already measure; it
is a floor, not a forecast.

## 6. Gates, method, and recipes

### 6.1 Acceptance (house rules, unchanged)

`make test-lambda-baseline` 100% and test262 unchanged after each retained
phase; MIR emission ratchet updated in the same commit for justified growth
[D8.6.1]; `mir-check` coverage with adjacency assertions for every new
witness/guard/lowering edge [D8.6.2]; forced-GC + poison sweeps
(`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`) for every representation
or lifetime change [D8.6.3]. Release-build timing only; one row at a time;
≥ 3 alternating pairs before any claim, ≥ 15 for a short row; A/B against the
bracketing binary in `temp/wt*/`, not against memory.

### 6.2 Measurement lessons from this round

1. **A stale object file lies.** The tree's `lambda.exe` reproduced the bool
   bug after the fix had landed because `transpile-mir.o` predated it. Check
   the `.o` timestamp before trusting any repro.
2. **`sample` at 1 ms changes the regime.** Loop copies of crypto_sha12 and
   matmul2 ran 100x slower per iteration under `sample -i 1` than the
   unsampled benchmark, and the "cliffs" that suggested vanished at `-i 5` on
   the benchmark's own size. Use ≥ 5 ms intervals and the benchmark's own
   workload shape; never read a sampled run's `__TIMING__`.
3. **Emitted-MIR histograms attribute compiler regressions; `LAMBDA_GC_STATS`
   attributes GC ones; neither attributes a runtime-helper regression.**
   gcbench2 and puzzle-at-HEAD have identical dumps and moved 1.5–2x. When both
   are flat, the answer is in `lambda/runtime/*.cpp`, and a bisect build is
   cheaper than a profile.
4. **The auto tier masks JIT defects and JIT timings mask the auto tier.**
   Every correctness claim needs both tiers; every "shipped" performance claim
   needs part 2.
5. **In a tight numeric loop the cost is instruction count, not branches.**
   T21-1b's first two attempts reasoned about MIR's LICM and the `mulo`/`bo`
   flag pair; one measured flat, the other slower. Counting the emitted
   instructions per index (25 → 14) is what moved the rows, and the lazy
   validity flag that removed eight constant `mov`/`and` per index was worth
   more than the branch-free select. Count the kernel before theorizing about
   the optimizer.
6. **A stale object file lies, twice.** The tree's `lambda.exe` reproduced an
   already-fixed bug because `transpile-mir.o` predated the fix; keep a copy
   of every binary you measure (`temp/lambda_t21_*.exe`) and check the `.o`
   timestamp before trusting a repro.

### 6.3 Worktree build recipe (used for all nine binaries)

```
git worktree add temp/wtX <commit>
ln -s $PWD/node_modules temp/wtX/node_modules
ln -s $PWD/mac-deps    temp/wtX/mac-deps
mkdir -p temp/wtX/build_temp && for d in build_temp/*; do ln -s $PWD/$d temp/wtX/$d; done
(cd temp/wtX && make release)     # ~7 min; produces temp/wtX/lambda.exe
```

Binaries left in place for this round: `temp/wt35` (v35), `temp/wtC`
(`357df0ae0`), `temp/wtD` (`742b2bb1f`), `temp/wtA` (`c13515b80`), `temp/wtB`
(`cfd215819`), `temp/wtE` (`9f3f05e1f`), `temp/wtF` (`176bdd934`), `temp/wt36`
(v36), `temp/lambda_head_release.exe` (HEAD). Remove with `git worktree remove`
when the round closes.
