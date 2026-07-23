# Lambda Impl Record: Numeric Hot-Path Tuning (M1/M2 — LANDED)

> **STATUS 2026-07-23 (rev 2): this is now a LANDED-WORK RECORD.** Phase 0 +
> M1 + M2 + the NaN comparison fix (§6.5) shipped 2026-07-22. Verification:
> `test/lambda/numeric_fastpath_edges.ls` (130 cases) **bit-identical
> before/after on both debug and release**; `make test-lambda-baseline`
> **3552/3552**; benchmark correctness sweep **58 rows, 0 suspect**; MT7
> ratchet lifted manually for 2 probes (§6.2). Measured results in §6.
>
> **Removed from this doc (rev 2, per designer):** the unimplemented plans.
> **M3 (COW anchor)** is SUPERSEDED — the designer went straight to refcount
> COW instead of patching the eager clone: design
> `vibe/Lambda_Design_COW.md` (CW10 retires the anchor), implementation plan
> `vibe/Lambda_Impl_Tune_COW.md` (which also absorbs M3's remedy (a) as a
> stopgap and remedy (c) into its JIT phase). **M4 (boxed element loads /
> `any` arithmetic)** is diagnosis only (§6.4), tracked in
> `vibe/Lambda_Tuning_Proposal.md` under R6. The full M3 analysis text
> (anchor sites, guards, exemptions, three sins, remedies) is preserved in
> git history and its load-bearing findings are restated in
> `Lambda_Design_COW.md` §2/§7.

**Record scope.** Fixes for the three Result9→Result10 MIR regressions
root-caused on 2026-07-22 (see `test/benchmark/Overall_Result10.md`). M2's
semantics were the designer's decision (2026-07-22).

**Governing invariant (all phases): observable behavior is bit-identical.**
Every mechanism here is a *representation/lowering* change under P6
("representation is invisible", `doc/Lambda_Formal_Semantics.md`). Any output
difference in `make test-lambda-baseline`, the benchmark goldens, or the new
fixtures is a bug in the phase, not an acceptable cost. No phase may change
*which* values are int vs float, *which* comparisons are true, or *which*
bindings alias.

Diagnosis quick-reference (measured 2026-07-22, release build, commit `ecd19f5a5`):

| Mechanism | Introduced by | Per-op cost today | Benchmarks hit |
|---|---|---|---|
| M1 ANY-float compare via decimal-string | `dd4fbc159` 2026-07-20 "number model realigned" | ~700 ns (micro: 1M array-elem compares = 736 ms) | pnpoly 90x, fasta 80x, ray, raytrace3d, navier_stokes, mbrot, triangl |
| M2 typed int ADD/SUB/MUL → boxed `fn_add` call | `5e4d75be6` 2026-07-07 "lambda semantics fix" | ~8.5 ns vs ~0.3 native (micro: 2M adds = 17 ms) | sum 69x, matmul 18x (loop indices), primes, array1, every loop counter |
| M3 COW anchor: hashmap alloc/free per anchored assignment | same 2026-07-07 commits | ~100–200 ns + malloc traffic | collatz 21x, splay 2.3x, graph-y scripts |

Micros kept at `temp/micro_fcmp.ls` (typed → native), `temp/micro_fcmp_any.ls`
(ANY → decimal path), `temp/micro_iadd.ls` (boxed add tax). Profiles:
`temp/pnpoly_sample.txt`, `temp/collatz_sample.txt`.

---

## 1. Phase 0 — freeze current behavior in fixtures (before any code change)

The point of Phase 0 is that Phases 1–3 are *pure* representation changes; we
prove that by capturing today's observable results first and diffing after.

**0.1 New fixture `test/lambda/numeric_fastpath_edges.ls` (+ `.txt` golden,
generated from CURRENT master).** Contents:

- Int overflow boundary: `9007199254740991 + 1`, `-9007199254740991 - 1`,
  `9007199254740990 + 1` (stays int), `(2^27) * (2^27)` (crosses into float),
  `3037000499 * 3037000499` (fits — 9.22e18? no: must stay ≤ 2^53−1 — pick
  `94906265 * 94906265` just under vs `94906266 * 94906266` just over the
  2^53 line), `type()` of each result (int vs float observability).
- `int / int` results incl. exactness: `1/3`, `7/2`, `-7/2`, `0/5`, division
  by zero behavior as it exists today.
- Mixed compares across representations, each both directions and all six
  operators where meaningful: `2^53 float vs 2^53+1 i64`; `0.5 vs 0`;
  `-0.0 vs 0`; `0.3 vs 0`; `1e308 vs i64 max`; `-1e308 vs i64 min`;
  u64 `18446744073709551615u64` vs `1.8446744073709552e19` float; NaN vs int
  (all four ordered compares false + `==`/`!=`); ±inf vs int extremes;
  decimal vs float; decimal vs int (decimal paths must be untouched).
- ANY-typed variants of all of the above (through array elements and untyped
  `pn` params) so both the static and dynamic paths are pinned.

**0.2 Capture benchmark goldens' current state** — already done via
`temp/mir_correctness_sweep.py` (59/59 audited rows). Re-run this sweep as the
exit gate of every phase.

**0.3 Record micro numbers** for the three micros above plus `sum2.ls`,
`pnpoly.ls`, `collatz.ls`, `matmul.ls` — the before column of the final report.

Gate: fixture passes on unmodified master; golden checked in.

---

## 2. Phase 1 — M1: exact numeric compare without decimal-via-string

### 2.1 Problem (recap)

`lambda_numeric_compare` (`lambda/runtime/lambda-number-runtime.hpp:140`)
sends *any* comparison where either operand part is FLOAT — including
float-vs-float — to `decimal_cmp_items` → `decimal_item_to_mpd` →
`lambda_finite_double_to_shortest` (dtoa) → `mpd_qset_string` (parse), per
operand, per comparison, plus two mpd allocations and frees. The intent
(comment: "without using binary64 as a common integer carrier") is exactness
for mixed int/float — but float×float needs no carrier at all, and
float×int64 has a standard exact algorithm. Statically-typed float compares
never reach this (native `MIR_DGE` at `transpile-mir.cpp:3106-3109`); the
victims are ANY-typed operands: array elements and untyped `pn` params.

### 2.2 Change

All in `lambda_numeric_compare`, inserted AFTER the existing NaN checks
(`:152-161`, unchanged — they already force `unordered`) and BEFORE the
decimal fallback branch (`:163`). The decimal branch remains for any case the
fast paths don't claim (decimal operands, non-simple parts).

**(a) FLOAT × FLOAT** — direct hardware compare. For two finite-or-infinite
doubles (NaN excluded above), IEEE `<`/`>` *is* the exact mathematical order;
`-0.0 == 0.0` matches the decimal relation (`mpd` compares −0 = 0).

```c
if (left_part.kind == LAMBDA_NUM_PART_FLOAT &&
    right_part.kind == LAMBDA_NUM_PART_FLOAT) {
    double a = left_part.float_value, b = right_part.float_value;
    result.order = a < b ? -1 : a > b ? 1 : 0;
    return result;
}
```

**(b) FLOAT × signed int64** (both directions) — exact floor-based compare;
never converts the int64 to double (that rounds above 2^53):

```c
// exact order of int64 i vs finite double d (NaN pre-excluded)
static inline int lambda_cmp_i64_double(int64_t i, double d) {
    if (d >= 9223372036854775808.0) return -1;         // d >= 2^63 > any i64
    if (d < -9223372036854775808.0) return 1;          // d < -2^63 (== -2^63 is representable, falls through)
    double fd = floor(d);                              // exact: |d| < 2^63
    int64_t t = (int64_t)fd;                           // in range by the guards
    if (i != t) return i < t ? -1 : 1;
    return d > fd ? -1 : 0;                            // fractional part → i < d
}
```

**(c) FLOAT × unsigned u64** — same shape with bounds `0` / `2^64`:
`d < 0 → u greater` (note `-0.0 < 0` is false, so −0.0 correctly reaches the
floor path and compares equal to `u == 0`); `d >= 18446744073709551616.0 → u
less`; else floor/cast to `uint64_t`/compare/fraction.

Signed×unsigned int pairs already have exact branches below (`:171-187`) —
untouched.

**(d) Decimal operands keep the current route** (`decimal_cmp_items`). The
double→mpd conversion inside `decimal_item_to_mpd` still uses the string path;
replacing it with an exact binary import (mantissa × 2^exp via mpd integer ops)
is a **deferred follow-up** — float×decimal compares are rare and not on any
benchmark's hot path. Record as an open item, do not block the phase.

### 2.3 Blast radius

Single choke point: every ordered compare (`fn_lt/gt/le/ge` in
`lambda-eval.cpp`), equality's numeric arm (`lambda-eval.cpp:1198`, `:1755`),
`min`/`max` (`lambda-vector.cpp:1443/1475`), and sort keys all funnel through
`lambda_numeric_compare`. One edit fixes all of them; nothing else changes.
`fn_gt` remains a C call from JIT'd code (~10–20 ns total) — inlining a
compare-IC into MIR emission is explicitly out of scope (future work).

### 2.4 Tests & gates

- Phase-0 fixture must pass unchanged (it pins exactly the adversarial cases:
  2^53+1, ±0, fractions, ±inf, NaN, u64 high range, decimal mixes).
- `make test-lambda-baseline` 100%; `test/lambda/number_model_realign.ls` green.
- Micro target: `micro_fcmp_any.ls` 736 ms → **< 30 ms** (C-call bound).
- Benchmark target: `pnpoly` ≈ R9 parity (~120–300 ms, vs 11 s today);
  `ray`, `raytrace3d`, `navier_stokes`, `fasta`, `mbrot`, `triangl` recover
  most of their regression (fasta/collatz keep an M2/M3 residue until those
  phases land).
- Re-run `temp/mir_correctness_sweep.py` — all rows still output-correct.

---

## 3. Phase 2 — M2: inline flex-int arithmetic (designer-decided semantics)

### 3.1 Decided semantics (2026-07-22)

- `int + int`, `int - int`: compute in **int64**; if the result leaves the
  compact-int range, **promote to float**.
- `int * int`: compute in **float** (double); if the product is within the
  compact-int range it is an int result, else it stays float.
- `int / int`: **float** (true division) — this is already the language rule
  and already lowers natively (`transpile-mir.cpp:3423-3435`, I2D+I2D+DDIV).
  No change needed; covered by fixtures.

**Equivalence proof against current runtime** (the reason this is a pure
lowering change): today `apply_classified_numeric`
(`lambda-eval-num.cpp:354-375`) computes ADD/SUB/MUL in `__int128` and calls
`pack_compact_int_or_float` (`:78-85`): `|v| ≤ INT56_MAX (2^53−1)` → compact
int, else `(double)v` (exact value, correctly rounded once).

- ADD/SUB: two compact ints are ≤ 2^53−1 in magnitude → int64 sum is exact
  (≤ 2^54, no int64 overflow possible). In-range → same int. Out-of-range →
  `I2D(exact int64)` = round(exact value) = identical to `(double)__int128`.
- MUL: both operands exact as doubles (≤ 2^53). If the true product ≤ 2^53−1
  it is representable → double multiply is exact → identical int after D2I.
  If the true product ≥ 2^53, IEEE gives round(true product) — identical to
  `(double)__int128`. Rounding cannot fall back inside the compact range
  (values ≥ 2^53 round to ≥ 2^53), so the int/float decision is also
  identical. The in-range check on the double (`|d| ≤ 2^53−1`) therefore
  agrees with the runtime's check on the exact product.
- Boundary constant: **`INT56_MAX/INT56_MIN` (`lambda/lambda.h:1178`,
  ±(2^53−1))** — the emitted constants must reference the same values; add a
  static_assert-style comment tying them.
- Corner pins (all in the Phase-0 fixture): `0 * -5 = 0` (int, not −0.0);
  `94906266² > 2^53` → float; `(2^53−1) + 1` → float `9007199254740992.0`;
  `type()` observability of each.

### 3.2 Lowering change

Replace the both-INT ADD/SUB/MUL guard at `transpile-mir.cpp:3367-3377`
(currently: box both operands + `fn_add`/`fn_sub`/`fn_mul` call) with inline
emission. Result contract is **unchanged**: a boxed Item register with
effective type ANY (the type table at `:3148-3150` stays as is), so no
downstream consumer changes. Boxing is pure ALU on both paths — compact ints
self-tag, and doubles ≥ 2^53 are never subnormal so the inline self-tagged
double box always applies (double-boxing v3).

ADD/SUB sketch (MUL analogous with DMUL-first):

```
  ; operands: raw int64 regs a, b (transpile_expr both-INT invariants)
  res  = ADD a, b                      ; exact in int64
  ; range check |res| ≤ INT56_MAX  (two compares or the shift trick)
  t    = ADD res, INT56_MAX            ; unsigned trick: t = res + (2^53−1)
  cmp  = UGT t, 2*INT56_MAX            ; out-of-range iff t > 2^54−2
  BT   cmp, slow
fast:
  item = box_int(res)                  ; tag-only: existing emit_box(…, LMD_TYPE_INT)
  JMP  done
slow:                                  ; rare: promote to float, still inline
  d    = I2D res
  item = box_double(d)                 ; inline self-tagged double
done:
```

MUL:

```
  da = I2D a ; db = I2D b ; d = DMUL da, db
  ad = DABS-equivalent (AND mask or DBGE compares) ; in-range test |d| ≤ INT56_MAX
  in-range:  i = D2I d ; item = box_int(i)
  else:      item = box_double(d)
```

Notes:
- `OPERATOR_DIV` both-int: verified already native — leave alone.
- `fn_idiv_i`/`fn_mod_i` (`:3325-3335`) remain C calls — out of scope (small
  helpers, not the regression).
- The ANY/mixed fallback (`transpile_binary_fallback` → `fn_add` boxed) is
  untouched; it is the correct path for ANY×ANY.
- fn_add/fn_sub/fn_mul runtime helpers are NOT modified — they remain the
  semantic reference and the ANY-path implementation.

### 3.3 MIR emission budgets

Inline sequences add instructions per script, so the MT7 0%-slack ratchet
(`test/mir/mir_budgets.json`, enforced in `test-lambda-baseline`) WILL fire.
Per the ratchet's own rules this is a **manual, reviewed lift**: regenerate the
budgets for affected probes in the same commit as the lowering change, with the
lift called out in the commit message. Do not widen slack globally.

### 3.4 Tests & gates

- Phase-0 fixture unchanged (this is the overflow/`type()` pin).
- `make test-lambda-baseline` 100% including mir-emission suites
  (`test_mir_emission_gtest`, ratchet lifted deliberately).
- Micro target: `micro_iadd.ls` 17 ms → **≤ 2 ms**; typed `micro_fcmp.ls`
  18.5 ms → **≤ 2 ms** (its cost was the adds).
- Benchmark targets: `sum2` 19.6 ms → low single-digit ms (R9: 0.28 ms — the
  remaining gap is tag/untag per iteration; acceptable, record it);
  `matmul`, `primes`, `array1`, `collatz` (partial), `brainfuck` recover their
  index-arithmetic tax.
- Correctness sweep re-run.

---

## 4. Phase 3 — M3: the COW anchor — SUPERSEDED (2026-07-23)

M3 never landed here. The eager-clone anchor (`fn_mutable_value` +
`MutableCloneContext` wrapping container-possible RHS at
`transpile-mir.cpp:5506-5520` / `:9786-9794`, with the
`mir_var_rhs_keeps_mutable_alias` exemptions) is retired wholesale by the
refcount-COW design: `vibe/Lambda_Design_COW.md` — its §2 restates this
section's diagnosis as Context B, CW10 replaces the anchor with O(1)
share-and-mark, and its §7 retires the clone context from the mutation path.
Implementation: `vibe/Lambda_Impl_Tune_COW.md`. Of the old remedies, (a)
runtime scalar early-return survives as that plan's stopgap Phase A0, and
(c) trust-definite-scalar-types folds into its JIT phase; (b) lazy
visited-map and (d) demotion repair die with the anchor itself. The full
analysis text is preserved in git history (this file, pre-rev-2).

---

## 5. Rollout & verification (as executed)

Executed order: **Phase 0 → Phase 1 (M1) → Phase 2 (M2)**, each one commit
with its budgets/goldens moves included, under the identical per-phase gate:
`make build-test` clean → `make test-lambda-baseline` 100% → Phase-0 fixture
byte-identical → correctness sweep on all timed benchmark rows → numbers
recorded in §6.

**Final verification (Result11) did NOT run here** — it is deliberately
deferred to the exit of `vibe/Lambda_Impl_Tune_COW.md` (COW Stage 1), so the
fresh benchmark round measures M1+M2 *and* the anchor retirement together:
full protocol re-run (3-run median, release, 180 s), output-correctness sweep
in the protocol (`WRONG_OUTPUT_ROWS` + sweep — a wrong-but-fast row must
never enter the mean again), then re-rank the tuning proposal's R-queue on
the new floor.

Open items deliberately NOT covered by this record (tracked elsewhere):

- Exact binary double→mpd import for float×decimal compares (rare path).
- Compare-IC / inline type-dispatch for ANY operands in MIR emission
  (overlaps M4 → `vibe/Lambda_Tuning_Proposal.md` R6).
- Range analysis to keep provably-in-range int arithmetic fully native
  (no tag/untag) in typed loops.
- `fn_idiv_i`/`fn_mod_i` inlining.
- LambdaJS (R0b) — plausibly shares the M1 shape through the JS runtime;
  diagnose separately before touching.

---

## 6. Landed results (2026-07-22)

### 6.1 What shipped

**Phase 0** — `test/lambda/numeric_fastpath_edges.ls` + golden (130 cases:
compact-int boundary, mul/div/idiv/mod, float-vs-float, exactness above 2^53,
fractional parts, u64 range, infinities, NaN, decimal relation, signed/unsigned
pairs, array-element (`any`) loads, min/max/sort, accumulation loops).

**M1** — `lambda/runtime/lambda-number-runtime.hpp`: added
`lambda_cmp_i64_double_exact` / `lambda_cmp_u64_double_exact` (floor-based, no
rounding of either operand) and a fast dispatch for simple float-involving
pairs ahead of the decimal fallback. Decimal operands and non-simple parts keep
the old route unchanged.

**M2** — `lambda/transpile-mir.cpp` `transpile_binary`: both-INT ADD/SUB/MUL now
emit inline instead of calling `fn_add`/`fn_sub`/`fn_mul`. ADD/SUB compute in
int64 (exact for compact operands) with a single biased-unsigned range test,
promoting to float out of range; MUL computes in double with a range test, then
`D2I` back to int when it fits. Equivalence with
`pack_compact_int_or_float` argued in-comment at the site.

### 6.2 MT7 ratchet lift (manual, this commit)

Two probes grew; both reviewed and traced to the inline sequences:

| Probe | metric | before → after |
|---|---|---|
| `lambda_scalar_home_donation` | module_insns / `_twice_#`.insns | 136→148 / 47→59 |
| `lambda_scalar_home_tail_forward` | module_insns / `_accumulate_#`.insns | 157→174 / 77→94 |
| both | `scalar_homes` on the same frames | 0→1 |

Instruction growth is the inline sequence replacing a 3-instruction call
(`twice(a){a*2}` = one MUL → ~15 emitted, net +12 — matches exactly). The
`scalar_homes` +1 is the float-promote branch boxing a double, which reserves a
number-stack slot for the subnormal residue of double-boxing v3. Accepted:
1 slot per frame that does int arithmetic, in exchange for removing a C call
per operation. Ratchet green 11/11 after the lift.

### 6.3 Measured (release build, single runs, ms)

| Benchmark | R9 | R10 (pre-fix) | **Now** | vs R10 |
|---|---|---|---|---|
| larceny/pnpoly | 122 | 11 000 | **110** | 100x faster — at R9 parity |
| jetstream/raytrace3d | 471 | 5 530 | **370** | 15x — **better than R9** |
| larceny/ray | 18.1 | 210 | **12.8** | 16x — **better than R9** |
| jetstream/navier_stokes | 1 290 | 14 900 | **1 913** | 7.8x |
| r7rs/sum | 0.282 | 19.6 | **3.98** | 4.9x |
| beng/fasta | 1.73 | 138 | **21.2** | 6.5x |
| jetstream/cube3d | 46.7 | 257 | **120** | 2.1x |
| kostya/matmul | 34.6 | 611 | **442** | 1.4x |
| larceny/array1 | 1.99 | 25.8 | **18.8** | 1.4x |
| kostya/primes | 11.9 | 78.1 | **69.9** | 1.1x |
| kostya/collatz | 355 | 7 470 | **6 358** | 1.2x |
| larceny/triangl | 398 | 2 310 | **2 251** | ~1.0x |

Micros: ANY-float compare 736 → **31 ms** (24x); typed float compare 18.5 →
**4.5 ms**; 2M int adds 17 → **4.4 ms** (3.9x).

### 6.4 What did NOT move, and why (honest residue)

M2 as scoped fires only when **both operands are statically `int`**. The rows
that barely moved are dominated by costs outside M1/M2:

- **collatz, and part of primes/array1** — the COW anchor (M3): collatz's
  profile was malloc/`hashmap_new`, untouched by M1/M2. Now owned by
  `vibe/Lambda_Design_COW.md` / `vibe/Lambda_Impl_Tune_COW.md` (§4).
- **matmul, triangl, array1** — `any`-typed values flowing through *array
  element loads*. Index arithmetic is now inline, but each element load still
  returns a boxed Item and the arithmetic on loaded values takes the boxed
  `fn_add` fallback. This is a fourth mechanism (**M4: boxed element loads /
  `any` arithmetic**), not previously separated out — likely needs an inline
  type test + native fast path on the ANY arm, or unboxed typed-array element
  access. **Tracked in `vibe/Lambda_Tuning_Proposal.md` under R6**; not
  planned in this record.
- **fib/tak** keep their R10 gains (frame/rooting work, unrelated).

### 6.5 NaN comparison bug — surfaced by Phase 0, FIXED 2026-07-22

Phase 0 exposed a genuine spec violation, fixed as a follow-up on the
designer's instruction (it is a *semantic* change, so it was kept out of the
M1/M2 commits precisely so those could be proven behavior-preserving).

**Symptom.** `nan <= x` and `nan >= x` returned `true`, violating
`doc/Lambda_Formal_Semantics.md` 6.1 ("poison stays incomparable — `nan < x`
false both ways, IEEE"). Worse than a spec deviation: the *statically-typed*
path was already correct (native `MIR_DLE`/`MIR_DGE` follow IEEE), so the same
expression answered differently depending on whether inference happened to type
the operands:

```
static_ops(nan, 1.0)  ->  [false, false, false, false, false, true]   // correct
any_ops(nan, 1.0)     ->  [false, TRUE,  false, TRUE,  false, true]   // wrong
```

**Cause.** `fn_le`/`fn_ge` were derived by negating `fn_gt_scalar`/`fn_lt_scalar`
(`lambda-eval.cpp`), and `!(a > b)` is not `a <= b` when the pair is
*unordered* — with a NaN operand neither relation holds, so both negations
report true. The comparison core reported `unordered` correctly; only the
negating wrappers discarded it. The elementwise keyword path
(`cmp_scalar_item` ops 3/5 in `lambda-vector.cpp`) had the identical defect.

**Fix.** Rather than add more negating wrappers, the shared shape was extracted
(CLAUDE rule 13): `fn_lt_scalar`/`fn_gt_scalar` were near-duplicates differing
only in the final direction. Introduced `scalar_order()` returning a 5-state
`ScalarOrder` (LT/EQ/GT/**UNORDERED**/INVALID) plus a tiny
`scalar_order_holds()` selector; all four relations now read the same ordering,
an unordered pair satisfies none of them, and an invalid pair errors. Added
`fn_le_scalar`/`fn_ge_scalar` (exported in `lambda.h`) and pointed both
`fn_le`/`fn_ge` and the elementwise ops at them. Net effect: two
near-duplicate functions collapsed into one core, and the bug class is now
unrepresentable — no caller can derive `<=` by negating `>`.

**Unit tests.** `test/test_scalar_compare_gtest.cpp` (16 cases, registered in
`build_lambda_config.json`, in the lambda baseline suite). Subprocess-driven
like `test_js_coerce_gtest` — deliberately, because the defining property of
this bug is that the two *lowerings* disagreed, which only exists end-to-end:
every NaN case asserts the statically-typed row (native MIR compares), the
`any`-typed row (runtime path), AND that the two are equal. Also covers
ordered pairs, mixed int/float fractional ordering, exactness at 2^53,
u64 beyond int64 range, infinities, the untouched decimal relation, and the
elementwise `lt/le/gt/ge` keyword lanes.

An in-process C++ variant was tried first and abandoned: calling the
`fn_*_scalar` family directly drags `lambda-rt`'s JS-DOM/radiant/CoreText/curl
link graph into a numeric test binary. Needing DOM stubs to test scalar
comparison is a sign the test is at the wrong level, so it was rewritten at the
level the property actually lives.

**Mutation-verified.** The fix was temporarily reverted (both wrappers back to
negating `fn_gt_scalar`/`fn_lt_scalar`) and the suite re-run: exactly the 6
NaN cases failed with a message naming the cause, while the 10
ordered/mixed/elementwise cases still passed — confirming the tests fail for
the right reason and are not merely green-on-green. Fix restored, 16/16.
Baseline with the new suite registered: **3568/3568**.

**Verification.** Exactly 5 lines of the pinned fixture changed (the NaN rows,
`le`/`ge` columns only) — nothing else moved, confirming the fix is surgical.
Native and runtime paths now agree. Baseline **3552/3552** with no other golden
needing an update; correctness sweep **58 rows, 0 suspect**; no measurable perf
change on comparison-heavy rows (pnpoly/ray/raytrace3d flat, ANY-compare micro
31 -> 27.7 ms). Fixture extended with native-path and elementwise NaN coverage
so the two paths are pinned against each other.

### 6.6 Next

The anchor (ex-M3) is retired by COW Stage 1 — `vibe/Lambda_Impl_Tune_COW.md`
— whose exit runs the full benchmark protocol as **Result11** (correctness
sweep in-protocol). M4 is queued in `vibe/Lambda_Tuning_Proposal.md` R6.
