# Lambda Sized Integers — Implementation Plan (open items)

- **Status:** PLAN — **D1 DECIDED (option B), user-confirmed 2026-07-20**;
  implementation not started
- **Date:** 2026-07-16; failure surface re-verified + crash row added
  2026-07-20
- **Design:** `vibe/Lambda_Type_Int_Sized.md` (§7 open items 1–2; this plan
  implements them). Promotion table: `vibe/Lambda_Type_Int.md`. Number model:
  `vibe/Lambda_Semantics_Number_Model.md`.
- **Convention:** `file:line` references drift; confirm against symbol names.

Two work items, independent, one PR each. Item 1 is a correctness fix and
goes first; item 2 is optional ergonomics.

---

## Item 1 — value-preserving `u64` in ordinary mixed arithmetic

### 1.1 Verified failure surface (probed 2026-07-16, re-verified 2026-07-20, `m = 18446744073709551615u64`)

| Expression | Today | Mathematically |
|---|---|---|
| `m * 1` | `-1` | 18446744073709551615 |
| `m + 0.5` | `-0.5` | ≈1.8446744e19 |
| `m div 2` | `0` | 9223372036854775807 |
| `m % 7` | `-1` | `1` |

Already correct (must not regress): same-width u64 arithmetic
(`m + 1u64` → `0u64` wrap), unary negation (`-m` → `1u64`), comparisons
(`m > 1` → true, `m < 0` → false, `-1 == m` → false), printing, `shr`/bitwise,
and the decimal conversion path.

**Crash row (found 2026-07-20; raises Item 1 from correctness fix to crash
fix):** a `u64` valued *exactly* `INT64_MAX` (`9223372036854775807u64`)
**segfaults** in any untyped-mixed arithmetic — `k + 0`, `k * 1`, `k + 0.5`
all die with exit 139. One below the boundary folds fine; one above hits the
wrong-value rows in the table. Cause: `INT64_ERROR` is `#define`d as
`INT64_MAX` (`lambda/lambda.h`), so `push_l(INT64_MAX)` returns `ItemError`
(`lambda/lambda-mem.cpp`), and `normalize_sized()` never checks — the ladder
consumes `ItemError` tagged as `INT64` and dereferences garbage. (Plain
`i64` arithmetic reaching `INT64_MAX` errors cleanly, exit 1 — same
sentinel, but there the error is a propagated *result*, not a consumed
*operand*.) Same-width `u64` ops, comparisons, and printing at this value
are unaffected.

**Root cause:** `normalize_sized()` (`lambda/lambda-eval-num.cpp`) folds
`UINT64` into the ordinary arithmetic ladders by signed reinterpretation —
`push_l((int64_t)item.get_uint64())` — before any `INT64`/`FLOAT`/`DECIMAL`
ladder runs. For values above `INT64_MAX` this changes the mathematical
value. Only untyped-mixed expressions reach this path; the sized ladders
(`SizedIntegerValue.is_unsigned`) and the comparison paths handle `UINT64`
directly and are value-correct.

### 1.2 Decision D1 — semantic model for the fold

**DECIDED 2026-07-20: option B**, user-confirmed. Recorded in
`Lambda_Type_Int_Sized.md` (§1 decision 8; §7 item 1 flipped to
decided/implementation-pending).

Options considered:

- **(A) Reject:** error when a `u64 > INT64_MAX` meets an untyped operand.
  Safe but value-dependent erroring (same expression works for small u64) —
  poor ergonomics, and inconsistent with comparisons, which already succeed
  value-correctly.
- **(B) Value-preserving promotion (RECOMMENDED):** `u64 ≤ INT64_MAX`
  normalizes to `int64` exactly as today (mathematically exact); above that,
  normalize to **decimal** (unlimited/BigInt-family context — u64 is
  integral). Rationale: ordinary Lambda arithmetic operates on mathematical
  values with rank promotion (`int → int64 → decimal`, per the number
  model); the comparison paths already uphold value semantics for `u64`, so
  arithmetic folding to a *different value* is incoherent. The machinery
  exists: `decimal_item_to_mpd()` (`lambda/lambda-decimal.cpp`) already
  converts `UINT64` via `mpd_set_u64`, and every ordinary ladder already has
  a `DECIMAL` arm.
- **(C) Documented reinterpretation:** cheapest; keeps today's behavior as
  spec. Rejected in recommendation: it enshrines `m * 1 ≠ m` while
  `m == m` and `m > 1` hold — an equality/arithmetic split no user can
  predict.

Consequences of B to document with the change: `m * 1` yields a *decimal*
(BigInt-family), not a u64 — consistent with "untyped arithmetic exits the
fixed-width domain" (same as `i8 + 1` behavior through promotion). Mixed
float (`m + 0.5`) follows the ladder's decimal/float rule (float embeds into
decimal per number model v2).

### 1.3 Stages

**P1.0 — Confirm D1** — **DONE 2026-07-20.** B confirmed by the user;
recorded in `Lambda_Type_Int_Sized.md` (§1 decision 8, §7 item 1 flipped to
decided/implementation-pending).

**P1.1 — Audit all `LMD_TYPE_UINT64` consumption sites.** Grep scope: all
of `lambda/` **including `lambda/js/`** (verified 2026-07-20:
`js_globals.cpp`, `js_runtime_value.cpp`, `js_runtime_internal.hpp` touch
UINT64 on the BigUint64Array egress path — classify rather than exclude;
expected *value-preserving*). Known core files: `lambda-eval-num.cpp`
(~19 sites: normalize, sized ladders, comparisons, unary, builtins switch),
`lambda-eval.cpp`, `lambda-data.cpp`, `lambda-data-runtime.cpp`,
`lambda-decimal.cpp`, `print.cpp`, `emit_sexpr.cpp`, `build_ast.cpp`,
`transpile-mir.cpp`, `transpile-call.cpp`, `validator/`,
`module/radiant/radiant_module.cpp`. Classify each site:
*value-preserving* (ok) / *width-domain op* (sized ladder — ok) /
*signed-reinterpreting* (fix alongside `normalize_sized`). Deliverable: a
short classification table appended to this doc. **Dedup flag (CLAUDE rule
13):** three identical `heap_calloc(sizeof(uint64_t), LMD_TYPE_UINT64)`
blocks exist — `push_u64` (`lambda-eval-num.cpp`), the `ELEM_UINT64`
typed-array read and `coerce_uint64` (both `lambda-data-runtime.cpp`);
P1.2 promotes `push_u64` to a shared header and routes all three through it.

**P1.2 — Implement.** In `normalize_sized()`'s `UINT64` arm:

```text
v = item.get_uint64()
if v <= INT64_MAX:  item = box_int64_value((int64_t)v); type = INT64
else:               item = <decimal Item from u64, unlimited ctx>; type = DECIMAL
```

**Amended 2026-07-20: `box_int64_value`, NOT `push_l`.** `push_l` returns
`ItemError` for `INT64_MAX` (the §1.1 crash row); `box_int64_value`
(`lambda/lambda-mem.cpp` — "new code should use" per its own comment)
represents the full int64 domain. Below the sentinel the two are
bit-identical, so the `≤ INT64_MAX` arm changes behavior only at exactly
`INT64_MAX` (crash → correct value). Reuse the existing u64→mpd conversion
(`decimal_item_to_mpd` / `mpd_set_u64` + `decimal_push_result`); do not add
a second u64→decimal encoder. Apply the same guard to any other
reinterpreting sites found in P1.1, checking each for the same `push_l`
sentinel hazard. While here: promote `push_u64` to the module header and
replace its two duplicates (P1.1 dedup flag). Add the root-cause comment at
the fix point (CLAUDE rule 12).

**P1.3 — Tests.** New `test/lambda/sized_numeric_u64_mixed.ls` + `.txt`
golden (CLAUDE rule 8): the four failure rows above, boundary values
(`9223372036854775807u64` and `9223372036854775808u64` mixed with int/float —
the former rows are **crash-regression pins**: today they segfault),
`≤ INT64_MAX` cases proving the int64 fold is unchanged, and re-assertions of
the already-correct surface (same-width wrap, `-m`, comparisons) to pin
against regression.

**Gates:** focused gtest for the new script + existing sized-numeric tests +
`make test-lambda-baseline` 100%.

### 1.4 Risks

- **Behavior change is confined to the currently-garbage path**: only
  `u64 > INT64_MAX` mixed with untyped operands changes, and today those
  results are wrong values, not depended-upon semantics. `≤ INT64_MAX` is
  bit-identical except exactly `INT64_MAX`, which today segfaults — any
  change there is an improvement.
- **Result-type surprise**: expressions gain a decimal result where they had
  a (wrong) int64. Documented in D1; goldens make it explicit.
- **Perf**: one unsigned compare on the `UINT64` normalize arm — cold path
  (u64 is a carrier type), negligible.

---

## Item 2 — `ushr` (JS `>>>` behavior) as a builtin

### 2.1 Decision D2 — semantics (to confirm)

Builtin only — **no `>>>` grammar token** (avoids parser churn; Lambda
bitwise is already builtin-based). Name: `ushr(a, n)`.

| Operand | Behavior | Result type |
|---|---|---|
| `u8/u16/u32/u64` | identical to `shr` (already logical) | operand type |
| `i8/i16/i32/i64` | reinterpret bits in the unsigned counterpart width, logical shift | unsigned counterpart (`i32` → `u32`, …) |
| plain `int` | JS alignment: ToUint32 (reinterpret low 32 bits), logical shift | `u32` |

Reference behavior: `ushr(-1i32, 1)` → `2147483647u32` (JS `-1 >>> 1`);
`ushr(8, 1)` → `4u32`. Shift counts follow the existing `shr` edge rules
(count masking/width edges stay consistent with
`test/lambda/sized_numeric_bitwise_go.ls`).

### 2.2 Stages

**P2.0 — Confirm D2**, especially the plain-`int` → `u32` row (the
alternative — operating in u64 — diverges from JS `>>>` and is rejected in
the recommendation since JS porting is the entire motivation).

**P2.1 — Implement.** Register `ushr` wherever `shr` lives — verified
2026-07-20: a sibling registry row beside `SYSFUNC_SHR` in
`lambda/sys_func_registry.c`, a runtime helper beside `fn_shr`/`fn_shr_item`
(`lambda-eval-num.cpp`), and the typed MIR Direct lowering. Implementation
is a thin composition: unsigned-reinterpret the left operand's width, then
delegate to the existing typed logical-shift path — no new shift engine.
Keep the MIR lowering symmetric with `shr`'s compact-operand handling so
`NUM_SIZED` operands don't collapse (the historical `emit_unbox(..., INT)`
bug class).

**P2.2 — Tests.** `test/lambda/sized_numeric_ushr.ls` + golden: the table
rows above, `u64` max value, shift-by-0 and width-edge counts, mixed
compact/plain operands, and a JS cross-check comment row
(`node -e "console.log(-1 >>> 1)"`).

**P2.3 — Docs.** Add to `doc/Lambda_Sys_Func.md`; update
`Lambda_Type_Int_Sized.md` §3 (bitwise table + drop §7 item 2).

**Gates:** focused gtest + `make test-lambda-baseline` 100%.

### 2.3 Priority

Optional — per design §7 the trigger is JS-porting demand. Item 1 does not
depend on it. If bundled, land as a separate commit after Item 1.

---

## Sequencing summary

1. ~~P1.0 decision~~ (DONE 2026-07-20: D1-B) → P1.1 audit → P1.2 fix →
   P1.3 tests → gates.
2. (optional) P2.0 decision → P2.1 impl → P2.2 tests → P2.3 docs → gates.
3. After both: update `Lambda_Type_Int_Sized.md` §7 (items resolved) and §8
   (test list); check `doc/Lambda_Data.md` needs no wording change (u64
   mixed-arithmetic result type).
