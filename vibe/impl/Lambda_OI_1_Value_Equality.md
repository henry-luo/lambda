# OI-1 — Value Equality and VMap Key Canonicalization

**Status:** IMPLEMENTED and VERIFIED 2026-09-08.

**Authority:** **S5.2.1v2**, **S5.6.2**, **S7.1.1v3**,
**S7.1.3v2**, and **S8.2.1v4**; design record **C8.6a** in
`vibe/Lambda_Semantics_Formal2.md`.

## Scope

OI-1 closed three coupled residue points without changing user-visible `==`:

1. `item_deep_equal` now delegates to a strict structural equality walker next
   to `fn_eq`. Strict equality keeps representation ranks distinct, disables
   cross-sequence equality, and treats its recursion cap as unequal without
   publishing a runtime error. Radiant uses it only for conservative no-op
   elision.
2. Lambda VMaps admit only canonical `NameKey` and `IntKey` inputs. String and
   symbol spellings share a name key; finite exact integral numerics share an
   integer key across ranks and decimal scales.
3. Fractional and poison numeric keys read as `null` and fail construction or
   writes through the checked error path. Host raw backing stores retain their
   unconstrained interop key relation.

## Implementation record

- `lambda/runtime/lambda-eval.cpp` carries `EqualityMode`; `fn_eq` remains the
  value relation while `fn_eq_strict` backs `item_deep_equal`.
- `lambda/runtime/vmap.cpp` validates public source VMap keys before insert,
  makes name-key comparison canonical, and keeps a separate raw-backing
  comparator mode for DOM/Jube callers.
- `vmap_set` now returns `ItemError` on a checked failure. MIR Direct and the
  AST interpreter propagate that result, including COW replacement writes.
- `test/lambda/eq_phase1_remaining.ls`, `vmap.ls`,
  `proc/vmap.ls`, `decimal_poison_c14c.ls`, and
  `test_item_repr_gtest.cpp` cover numeric ranks, decimal scales, name-key
  normalization, invalid-key handling, and strict no-promotion equality.

## Verification

- `make build` — PASS.
- Focused direct/JIT VMap fixtures — PASS:
  `eq_phase1_remaining`, `vmap`, and `decimal_poison_c14c`.
- Focused procedural VMap fixture — PASS, including an invalid-key write caught
  by its local error handler without publishing an error value to the COW
  binding.
- `ItemRepresentation.StrictDeepEqualityDoesNotPromoteNumericRanks` and
  `RuntimeShapeTransition.VMapMutationStabilizesWideValues` — PASS (2/2).
- `make test-lambda-baseline` — PASS: 5,083/5,083 combined tests; 2,979/2,979
  Lambda runtime tests.
- `make test262-baseline` — PASS: 40,261/40,261 baseline tests; zero
  regressions.
