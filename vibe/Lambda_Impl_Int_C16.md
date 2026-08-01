# Lambda — C16 Flex-`int` Implementation Plan

**Status:** NOT STARTED — 2026-08-01. Design is decision-complete; no code has moved.

**Design authority:** `vibe/Lambda_Semantics_Formal2.md` **C16** (rulings 1–14 and the
poison-symmetry summary) and **C17** (converters keep returning `error()`; nan stays
truthy), with the normative text in `doc/Lambda_Formal_Semantics.md` §3, §4 (head), §4.1,
§4.2, §4.6, §4.7, §5.1, §11.4. Containment of the failures this plan removes is
`vibe/Lambda_Design_Type_Enforcement.md` TE-15. Where documents disagree, the ledger
governs.

**The one-line summary of the change:** flex `int` becomes the float64-representable
integers, so *int arithmetic is total in `double`*. The 53-bit band survives only as the
**compact-Item carrier capacity**, never again as a semantic boundary. Most of the work is
deletion.

---

## 1. The constraint that shapes everything: tag space

Verified 2026-08-01 against `lambda/lambda.h` (this closes C16's open "boxed encoding /
tag-space audit" item):

- An inline double **is its own Item, using all 64 bits** — `lambda_float_ptr_to_item`
  (`lambda.h:1293-1301`) returns the raw bits whenever `bits & ITEM_DBL_MASK`
  (`0x6000000000000000`, `lambda.h:140`) is non-zero.
- Every *tagged* value spends its high byte on the tag and keeps a **56-bit payload**
  (`ITEM_HIGH_BYTE_MASK`, asserted at `lambda.h:1246`).

**Consequence: an int-tagged Item can never carry an inline double.** The int tag byte and
the double's own bits occupy the same space. So "represent `int` as float64 internally" is
available in *native lanes only*; the boxed representation must stay either a compact
integer payload or a **tagged pointer to a double cell** — which is exactly what `d2it`
(`lambda.h:1286`) already does for out-of-band floats. The design below follows that
precedent rather than inventing a carrier.

---

## 2. Three layers, three answers

| Layer | Carrier | Notes |
|---|---|---|
| Boxed `Item` | compact 56-bit payload for `|v| ≤ 2^53`; double-cell pointer above | two int tags (see §3) |
| JIT native lane (default) | `double` (`MIR_T_D`) | no tag, no checks — semantics *is* binary64 |
| JIT native lane (optimized) | `i64`, **only when range-proven `≤ 2^53`** | invisible optimization; unproven ⇒ stays double and is still correct |

**Keep the compact threshold at 2^53, do not widen it to the payload's 2^55.** Above 2^53
not every integer is float64-representable (only evens, then multiples of 4), so a wider
compact payload could encode a value that is *not a member of the int domain*. Capping at
2^53 keeps the invariant "a compact payload is always a valid `int`" true by construction,
and keeps the compact⇄double conversion exact in both directions.

**Why the `i64` lane must stay range-proven.** Below 2^53, `i64` and `double` arithmetic
agree exactly, so a proven counter is semantically indistinguishable. Above it they
diverge: `i64` would produce exact values such as `2^53 + 1`, which are **not in the int
domain** at all. The proof obligation is therefore soundness-critical — but the *default*
being `double` means a missing proof costs speed, never correctness. This is the property
that makes the O1-class divergence unrepresentable rather than merely fixed.

---

## 3. Phase order

```text
Phase A (representation: carriers, i2it, poison values)
    └── Phase B (arithmetic: delete flexint lowering, div/% domains)
            ├── Phase C (frontend: lexer, literals, inference, printing)
            └── Phase D (boundaries: admission, lattice, TE-15 site removal)
                    └── Phase E (perf: range-proven i64 lanes — the T-B lever)
```

### Phase A — representation

- **A1. `i2it`'s overflow arm is the O1 fix** (`lambda.h:1279-1282`). Today
  `... : ITEM_ERROR` — this is the exact mechanism behind the measured divergence (a raw
  out-of-band `i64` re-boxed through the 53-bit guard yields the bare `ITEM_ERROR`
  singleton; repro `temp/overflow_fn_test3.ls`). It becomes "box into a double cell". The
  companion comment at `lambda.h:1255-1256` ("compact int is capped to the IEEE float64
  safe-integer band; every packing/overflow check must enforce this bound") must be
  rewritten to say *carrier capacity*, not domain bound — the comment is currently the
  clearest statement of the retired semantics and will mislead every future reader.
- **A2. Second int tag** for the sparse band (`INT_BIG` or equivalent), payload = pointer
  to a double cell, allocated on the number-stack / scalar-home machinery already used by
  out-of-band floats (`lambda_item_adopt_scalar_home`). `type()`, `is`, printing, and
  equality map both tags to `int`. Requires a free tag slot in `0x00–0x1F` — audit against
  the existing `LMD_TYPE_*` assignments before choosing.
- **A3. Poison values** `int.inf` / `-int.inf` / `int.nan`, and (C16 ruling 13)
  `integer.inf` / `-integer.inf` / `integer.nan`. Encoding decision: the int poison trio
  can live as three reserved compact payloads (cheap, no cell) — evaluate against A2's
  cell carrier. `integer` is mpdec-backed, so its poison rides the **same unblock as
  `decimal.inf`/`decimal.nan`**: mpdecimal supports the specials natively and the Lambda
  wrapper currently filters them (`lambda-decimal.cpp` ~:306/:374/:420).
- **A4. Unbox path.** The natural target for an int Item is now a `double`: compact ⇒
  `i2d`, sparse ⇒ load from cell. One branch, at dynamic boundaries only. Audit `it2i`
  consumers — under C16 the declared-return flattening that `transpile-mir.cpp:12034`
  feeds needs rework, not patching (it currently truncates a boxed int-or-float through
  `it2i` to satisfy a native int return ABI).
- **A5. Audit `i2it`'s `ITEM_ERROR` consumers.** Any site reading that return as a
  meaningful error signal changes behavior silently once A1 lands.

### Phase B — arithmetic

- **B1. Delete the flexint dual-lane emission** (`transpile-mir.cpp:4554-4628` — the
  compact-loop add/sub fast paths, the `MUL` double-with-range-test path, and the
  ADD/SUB branch-and-box). `int ⊕ int` becomes a single `DADD`/`DSUB`/`DMUL`. **This is
  where the win is**: the same emission the navier-stokes analysis blamed for boxing all
  index arithmetic. Expect the MIR budgets (`test/mir/mir_budgets.json`, MT7 0%-slack) to
  move — an *unexplained* jump means a gate is missing.
- **B2. `pack_compact_int_or_float`** (`lambda-eval-num.cpp:234`, called from the `+`/`-`/
  `*` arms at :535-539, :581) becomes "pack int, choosing carrier by magnitude" — it no
  longer changes *type*, only carrier. Its `__int128` computation should become a `double`
  computation to match B1's emitted semantics exactly (correctly-rounded binary64), so the
  interpreter and JIT cannot disagree.
- **B3. Overflow classification.** `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT`
  (`lambda-number.hpp:40`, set at :183, :205, :281) is retired for the flex tier; the int
  arm's overflow policy becomes saturate-to-poison at the float-range extremes. Consumers
  at `transpile-mir.cpp:4085` and `:10454` follow.
- **B4. `div`/`%` domain change** (C16 ruling 4 + 13): `lambda_numeric_classify`
  (`lambda-number.hpp:179-183` for the common int/float pair, :265-274 for the general
  arm) must stop mapping `int div int` and `integer div integer` to float/decimal. Zero
  divisor yields the domain's own poison. Sized `i64`/`u64` operands enter `integer`
  first, so `i64 div u64 → integer` (was decimal). **`/` is untouched everywhere.**
- **B5. Vector lanes.** Integer-array `div` stays an int-family array with per-lane
  poison (spec §4.7), replacing today's float-array result.

### Phase C — frontend

- **C1. Lexer (C16 ruling 9).** `grammar.js:24-28`: `float_literal` currently includes
  `seq(integer_literal, exponent_part)` (line 27), which is why `10e1` is a float today.
  Split `exponent_part` (line 23) into negative and non-negative forms: an
  integer-spelled mantissa with a **non-negative** exponent joins `integer`
  (`grammar.js:259`), while a negative exponent stays `float` (`:261`). Then
  `make generate-grammar` — never edit `parser.c`.
- **C2. Literal range check.** Unsuffixed int-form literals outside ±(2^53−1) are compile
  errors *even where sparsely representable* (`1e16` errors; `1.0e16` or `1e16n` are the
  fixes). This is a frontend diagnostic, not a runtime path.
- **C3. Parser data homes are unchanged** (C16 ruling 6) — input parsers keep the
  contiguous-band test for `int`, else `int64`, else `decimal`. Do not "helpfully" widen
  ingestion to sparse representability.
- **C4. Inference.** `build_ast.cpp:5174-5201` keeps calling `lambda_numeric_classify`;
  the change is entirely inside the classifier (B3/B4). Verify `int ⊕ int` now yields a
  clean `int` static type with no ANY downgrade — that is the TS-3-adjacent win.
- **C5. Printing** (spec §4.6): finite ints print as integers at every magnitude, no
  exponent form; poison prints `int.inf` / `-int.inf` / `int.nan` and the `integer.*`
  trio, all parseable. **Golden churn is expected** wherever today's escapes printed
  float-formatted (`1.80144e+16` → `18014398509481982`).

### Phase D — boundaries

- **D1. Value-aware admission** switches from the band test to an **integrality** test:
  any finite integral double (e.g. `1e300`) now passes an `int` boundary. Confirm the
  validator and `lambda_type_check` numeric arms use integrality, not `INT53_MAX`.
- **D2. Poison admission** (C16 rulings 12 + 14): narrowing verifies domain membership —
  a shared `inf` passes and re-tags, a **foreign nan rejects** like `3.5`, sized-int
  boundaries admit no poison. Widening needs **no check at all** (poison classifies
  upward), so `int → float` and `int → integer` edges stay statically closed.
- **D3. `is` lattice** (ruling 10): drop `int ⊑ int64`; the chain is
  `i32 ⊑ int ⊑ integer` (onward to `decimal`), `int ⊑ float` definitional, `int ∥ int64`.
  Enumerate affected goldens and W-series items — this is a visible behavior change
  (`5 is int64` flips).
- **D4. TE-15 origination sites.** The flex-int promote edge is deleted from the
  origination set; converter/cast/validation sites remain (C17). Independently, the
  `closed_item_result` polarity bug (`transpile-mir.cpp:11355`) still needs fixing:
  a *missing* variant analysis must mean "defect-capable", not "trusted clean".

### Phase E — performance

- **E1. Range-proven `i64` lanes.** Now the *only* reason to keep an integer lane: a
  double-lane array index pays `cvttsd2si` per access. Prove ranges from loop bounds and
  `len()` and keep counters/indices in `i64`. This is the **native counted for-loop
  (T-B)** item already ranked the biggest lever in the Result18 analysis — C16 is what
  makes it sound.
- **E2. Re-measure** after B1: the typed-vs-C2MIR gap and the navier-stokes row are the
  two headline numbers this work is aimed at. Release build only.

---

## 4. Gates

- `make test-lambda-baseline` at 100%, `make build-test` green, MIR budgets explained.
- New `*.ls` tests with `*.txt` goldens for: totality (`(2^53−1) + (2^53−1)` stays int),
  `div`/`%` poison in both int and integer domains, poison printing/round-trip, `10e1`
  vs `10.e1` vs `1e16`, lattice flips (`5 is int64` → false), narrowing nan rejection,
  and widening poison flow-through.
- The C16 equivalence property worth asserting directly: **erasing the `i64` lane must
  not change any result** — E1's optimization is unobservable, which is spec §13's
  invariant 1/3 applied to this work.

## 5. Open before coding

- A2's tag-slot choice and whether int poison uses reserved compact payloads or cells.
- Bitwise/shift domain over the sparse band (C16 open item; recommendation on record:
  define for finite values within ±(2^53−1), `error()` outside — bit reinterpretation is
  not number math).
- Whether `Lambda_Type_Numbers.md` and `Lambda_Semantics_Number_Model.md` need the same
  amendment pass as the spec (both predate C16; the latter's `is`-lattice section is
  known stale per ruling 10).
