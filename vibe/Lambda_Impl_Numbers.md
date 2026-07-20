# Lambda Number Model Realignment — Implementation Plan

**Status:** IMPLEMENTED — completed 2026-07-20; this document retains the task
inventory and final verification record for the numeric arithmetic realignment
**Design authorities:** `Lambda_Semantics_Number_Model.md` Part 1,
`../doc/Lambda_Formal_Semantics.md` §4, and
`Lambda_Design_Stack_API.md` Phase 7
**Supersedes for arithmetic:** the magnitude-threshold promotion work retained
as history in `Lambda_Impl_Sized_Int.md`

---

## 0. Fixed decisions

These are inputs to the implementation, not questions for an implementation
phase to reopen.

1. Promotion is selected from operand **types**, never runtime magnitudes.
   There is no `u64 <= INT64_MAX -> int64` rule.
2. Sized-integer × sized-integer `+`, `-`, `*`, integral `div`, `%`, bitwise,
   and shift operations select a sized result lane and stay there. Runtime
   overflow follows Go: unsigned modulo `2^n`, signed deterministic
   two's-complement, with `MinInt div -1 == MinInt` in that lane.
3. Different sized integer types do not enter the semantic number tower merely
   because their widths or signedness differ. Lane selection follows the
   complete operand domains: for example `i8 + u8 -> i16`,
   `i32 + u32 -> i64`, and `i64 + u64 -> u64`.
4. A sized integer mixed with a non-sized number first enters the smallest
   complete semantic domain: `i8` through `u32 -> int`; `i64/u64 -> integer`.
   The other operand then meets it through the ordinary number tower.
5. True `/` is not a sized-lane operation. Its result is domain-selected:
   `int / int -> float`, `integer / integer -> decimal`, and decimal
   participation produces decimal. Therefore `i8 / u8 -> float`, while
   `i64 / u64 -> decimal`.
6. Plain `int` `+`, `-`, and `*` remain `int` inside the exact safe band and
   overflow to correctly rounded `float`. The possible loss of precision in
   the overflowing mathematical result is an accepted performance tradeoff;
   it does not trigger automatic `integer` promotion.
7. `integer` is the arbitrary-precision `DECIMAL_BIGINT` carrier. It is not an
   ordinary decimal chosen because a `u64` happened to exceed a threshold.
8. `DOUBLE`, `INT64`, and `UINT64` retain the Stack API Phase 7 ownership model.
   This work must not reintroduce inline full-width integers or use GC ownership
   as an arithmetic promotion mechanism. `DTIME` remains always GC-owned.
9. LambdaJS keeps JavaScript numeric semantics: JS `/` returns a JS Number.
   Python guest-language statements that true division returns float are also
   guest-specific and are not Lambda number-model conflicts.

### 0.1 Result-domain matrix

| Operands | `+ - *` | `/` | integral `div` / `%` |
|---|---|---|---|
| `int`, `int` | `int`, overflow to float | float | `int` / `int` |
| `integer`, `integer` | integer | decimal | integer / integer |
| `float` with `int` or `float` | float | float | invalid unless explicitly converted |
| `decimal` with any semantic number | decimal | decimal | decimal behavior only where the operator is defined |
| `integer` with `int` | integer | decimal | integer / integer |
| `integer` with `float` | decimal | decimal | invalid unless explicitly converted |
| sized integer, sized integer | selected sized lane | map each operand to `int` or `integer`, then use this table | selected sized lane |
| compact sized integer, non-sized | enter as `int`, then meet | enter as `int`, then meet | selected `int`/`integer` domain |
| `i64/u64`, non-sized | enter as `integer`, then meet | enter as `integer`, then meet | selected `integer`/decimal domain where defined |

All rows are symmetric in operand order. Converting an operand into the selected
domain must be exact. Only the documented operation itself may round or wrap.

---

## 1. Pre-implementation gap inventory

The following inventory was checked against the starting tree on 2026-07-20
and is retained to explain the work packages. These are historical starting
conditions, not gaps in the completed tree.

- `lambda/lambda-eval-num.cpp::normalize_sized` collapses every compact sized
  integer to `INT64`, converts a small `UINT64` to `INT64`, and converts only a
  large `UINT64` to ordinary decimal. This is the value-dependent policy being
  retired.
- `sized_integer_arithmetic` already implements the sized × sized lane and
  Go-style wrap for `+ - * div %`. It should become a consumer of the shared
  lane classifier rather than be replaced with semantic-tower arithmetic.
- `fn_add`, `fn_sub`, `fn_mul`, `fn_div`, `fn_idiv`, and `fn_mod` normalize
  before entering separate hand-written ladders. The ladders currently keep
  `INT64 + INT` in `INT64`, convert `INT64 + FLOAT` to float, and make non-decimal
  `INT64 / INT64` return float.
- `decimal_from_uint64` constructs an ordinary decimal. The runtime has
  `bigint_from_int64` but no symmetric exact `bigint_from_uint64`, so the
  required `u64 -> integer` entry edge is missing.
- `integer` and `decimal` share `LMD_TYPE_DECIMAL`; the runtime distinction is
  the `DECIMAL_BIGINT`/`unlimited` carrier. A bare `TypeId` is therefore
  insufficient for either runtime promotion or static result inference.
- `build_binary_expr` uses enum ordering and `std::max` in numeric result
  inference, and treats `OPERATOR_DIV` as float too broadly. Enum order is a
  representation detail, not the semantic lattice.
- MIR-Direct repeats numeric decisions across effective-type discovery,
  binary lowering, boxing, and inference evidence. `OPERATOR_DIV` still adds
  universal float evidence in paths where an integer/decimal result is
  required.
- The legacy C2MIR transpiler is excluded from supported builds. Its archived
  source is not a second numeric implementation or an acceptance target.
- Vector/reduction paths specialize `ArrayLong`/`ArrayInt64`/`ArrayFloat` and
  can silently select float for division. Full-width integer true division
  needs a decimal-capable result container, while integral `div` and `%` stay
  in their selected lane.
- Phase 7 made datetime always GC-owned, but direct typed-field, formatter,
  parser and embedding paths still need an implementation audit for
  retired one-word embedded-`DateTime` assumptions.

---

## 2. Work packages

Each package lands with focused tests. Do not postpone correctness tests until
the final baseline run.

### N0 — Baseline and discriminating probes

- [x] Record the current branch, dirty-worktree inventory, and baseline status
  without modifying unrelated user files.
- [x] Add focused scalar probes that print both `type(value)` and value for the
  matrix in §0.1. Include both operand orders.
- [x] Pin magnitude independence with `u64` values on both sides of
  `INT64_MAX`, including the same expression shape with only the literal value
  changed.
- [x] Capture before-change MIR instruction/local counts for representative
  Lambda and LambdaJS numeric frames. Use the same implementation bodies for
  the final comparison.

### N1 — One shared numeric classifier

- [x] Introduce a C-compatible numeric-kind descriptor that distinguishes
  `int`, `integer`, `float`, `decimal`, each compact sized subtype, `int64`,
  and `uint64`. Do not encode semantic joins as `TypeId` ordering.
- [x] Provide one pure classifier for operation family, operand kinds, selected
  sized lane or semantic domain, result kind, and overflow policy.
- [x] Give the evaluator and active MIR-Direct transpiler thin adapters into the same
  classifier. At the third near-identical decision path, extract the shared
  table/helper rather than copying another switch.
- [x] Make runtime classification inspect the complete `Item`, including the
  bigint-vs-decimal carrier. Make static classification inspect the complete
  `Type*`; a bare `TypeId` must not erase `integer`.
- [x] Add table-driven unit tests for every pair, both operand orders, and each
  arithmetic family before changing execution code.

### N2 — Exact semantic-domain entry conversions

- [x] Replace `normalize_sized` with explicit, destination-named conversions:
  compact sized integer to `int`, `i64` to `integer`, `u64` to `integer`, and
  sized float to its required float lane/domain.
- [x] Add a shared exact `bigint_from_uint64` implementation beside
  `bigint_from_int64`; do not route `u64` through ordinary decimal and do not
  signed-cast values above `INT64_MAX`.
- [x] Reuse the canonical float-to-decimal conversion so mixed
  `integer/float` operations follow the shortest-round-trip rule rather than a
  binary approximation or formatter-specific path.
- [x] Audit allocation/rooting at every entry conversion. Creating an integer
  or decimal is a GC allocation; the other operand and intermediate values
  must remain live through it. Numeric number-home pointers are not GC roots.
- [x] Keep the Phase 7 scalar storage ABI intact: exact conversion into
  `integer` is a semantic-domain exit, not a new heap representation for the
  original `i64/u64` value.

### N3 — Runtime scalar arithmetic

- [x] Route `fn_add`, `fn_sub`, `fn_mul`, `fn_div`, `fn_idiv`, and `fn_mod`
  through the shared result decision before converting either operand.
- [x] Preserve `sized_integer_arithmetic` behavior for sized × sized integral
  operations, including signed/unsigned lane selection, wrap, `MinInt div -1`,
  and zero-divisor errors.
- [x] Remove the `UINT64 <= INT64_MAX` branch and every equivalent magnitude
  test from promotion paths.
- [x] Implement compact-sized × non-sized entry through `int`, including the
  existing flex-`int` overflow-to-float result rule.
- [x] Implement `i64/u64` × non-sized entry through `integer`, preserving exact
  results for `+ - * div %` and producing decimal for true `/`.
- [x] Make `i64/u64` mixed with float enter decimal symmetrically, without a
  binary64 round trip of the integer operand.
- [x] Correct true division by domain: `int/int` and float-domain division
  return float; `integer/integer` and integer/float-domain joins return decimal.
  Float-domain zero division follows the normative IEEE result rather than the
  current generic integer-zero error path; integral `div`/`%` retain
  `error()`.
- [x] Audit unary negation, `abs`, power, comparisons, equality, conversions,
  and min/max for accidental use of the retired normalization helper. Change
  only behavior implied by the normative model and add a pin for each change.

### N4 — AST typing and diagnostics

- [x] Replace enum-order numeric promotion in `build_binary_expr` with the
  shared classifier. Preserve the complete result `Type*`, especially the
  distinction between `integer` and ordinary decimal.
- [x] Infer the selected sized lane for sized × sized integral operators and
  the mapped semantic result for sized × non-sized expressions.
- [x] Infer `/` as float only for the int/float domain and as decimal for the
  integer/decimal domain. `i64/u64` true division must infer decimal.
- [x] Apply the same result rules to constant folding and conditional/common
  numeric joins where they currently depend on enum order.
- [x] Route value-parameter and annotated-assignment compatibility through the
  exact-embedding lattice. Preserve exact `int -> float`/`int -> int64`
  widening; reject implicit `int64/u64 -> float` and narrowing conversions.
- [x] Keep literal-zero diagnostics aligned with the formal semantics: static
  zero for integral `div`/`%` is a compile error; runtime computed zero returns
  `error()`.

### N5 — MIR-Direct lowering

- [x] Feed the AST's complete numeric kind into MIR effective-type and boxing
  decisions; do not reconstruct semantics from a physical MIR register type.
- [x] Remove universal `OPERATOR_DIV -> FLOAT` inference evidence. Select
  native float, sized integer, integer carrier, or decimal helper lowering from
  the shared result decision.
- [x] Emit native arithmetic only when it implements the selected semantic
  result exactly. In particular, do not lower full-width integer/float mixes
  through a double register and do not lower `integer/integer /` as native
  float division.
- [x] Preserve caller-donated homes for actual `INT64`/`UINT64` sized results.
  Integer/decimal results are GC objects and follow ordinary root publication.
- [x] Remove the retired C2MIR backend from active build and test
  configuration. Its archived source is outside this implementation gate.
- [x] Consolidate duplicated result/boxing switches after the new path is
  working; do not leave old and new promotion systems selectable in one build.

### N6 — Arrays, vectors, and reductions

- [x] Route scalar-vector and vector-vector result selection through the scalar
  numeric classifier, including mixed element lanes.
- [x] Keep sized `+ - * div %` in the selected typed lane with Go wrap and
  whole-operation poison on an integral zero divisor.
- [x] Materialize compact-integer true division in a float-capable result lane.
  Materialize any true division whose mapped domain is decimal in a generic or
  decimal-capable result container; never truncate it into `ArrayInt64` or
  silently use `ArrayFloat`.
- [x] Make reductions behave like the corresponding scalar fold: integer-lane
  sums/products stay in the lane, and averages/true divisions use the selected
  float or decimal domain.
- [x] Audit typed-array assignment/conversion separately from arithmetic.
  Explicit destination narrowing may truncate/wrap as documented; it must not
  leak backward into expression result promotion.

### N7 — Phase 7 storage-boundary audit

- [x] Verify that small and wide `INT64`/`UINT64` take identical number-home,
  caller-return, destination-owner, and ownerless-fallback paths.
- [x] Verify typed maps/objects, arrays, closure environments, module/global
  bindings, async/generator state, tasks/promises, and exception/completion
  records own persistent `INT64`/`UINT64` payloads rather than retaining an
  activation pointer.
- [x] Remove any surviving direct-field or legacy-C assumption that a datetime
  is an embedded one-word value. Store the GC-owned datetime pointer and use
  accessors that permit the object layout to grow.
- [x] Confirm GC classification roots `DTIME` but does not treat number-home
  `DOUBLE`/`INT64`/`UINT64` payload pointers as independent GC objects. Retain
  the explicit counted fallback for ownerless persistent numeric slots.

### N8 — Regression matrix and cleanup

- [x] Add Lambda integration cases and expected `.txt` files for every new
  `.ls` script. Cover values, `type()`, both operand orders, and error paths.
- [x] Cover all sized lanes, mixed signedness, same/different widths, safe-band
  edges, `INT64_MIN/MAX`, `UINT64_MAX`, and values immediately around
  `INT64_MAX`.
- [x] Cover locals, arguments, generated returns, closures, arrays/maps,
  persistent ownerless slots, and repeated calls so arithmetic changes also
  exercise the Phase 7 lifetime rules.
- [x] Add direct evaluator/unit tests for the result classifier and exact
  `u64 -> integer` conversion.
- [x] Remove `normalize_sized`, retired threshold helpers, stale comments,
  disabled duplicate decimal branches, and now-unreachable boxing/lowering
  cases. Grep for the old `INT64_MAX` promotion condition after cleanup.
- [x] Update the status/gap tables in the number-model, sized-int, runtime, and
  Stack API documents with the final symbols, tests, and gate evidence.

---

## 3. Required behavior examples

| Expression shape | Required result |
|---|---|
| `255u8 + 1u8` | `0u8` |
| `255u8 + 1` | `256` as `int` |
| `127i8 + 1i8` | `-128i8` |
| `i8 + u8` | `i16` lane |
| `i32 + u32` | `i64` lane |
| `i64 + u64` | `u64` lane, wrapping there if needed |
| `1i64 + 1` | exact `integer` value `2` |
| `18446744073709551615u64 + 1` | exact `integer` value `18446744073709551616` |
| small `u64 + int` | `integer`, identical route to large `u64 + int` |
| `1i64 + 0.5` | decimal `1.5` |
| `1u64 + 0.5` | decimal `1.5` |
| `i8 / u8` | float |
| `i64 / i64` | decimal |
| `i64 / u64` | decimal |
| `integer / integer` | decimal |
| `int / int` | float |
| `1u8 div 0u8`, `1u8 % 0u8` | `error()` |
| `MinInt iN div -1iN` | `MinInt iN` in the same lane |

Every row needs a result-type assertion, not only a printed numeric value.

---

## 4. Acceptance gates

The implementation is not complete until all of these are recorded with the
final change, not inferred from focused tests:

- [x] `make build`
- [x] `make build-test`
- [x] focused numeric unit and Lambda integration tests: zero failures
- [x] JIT/MIR-Direct and MIR-interpreter parity probes: zero mismatches
- [x] `make test-lambda-baseline`: **0 failed tests**
- [x] `make test262-baseline`: **0 failed tests and 0 retries**
- [x] relevant LambdaJS/Node preliminary or baseline gate when shared runtime
  code changes: zero regressions
- [x] forced-GC/lifetime probes for integer/decimal conversions and Phase 7
  storage boundaries: zero stale-home or rooting failures
- [x] release build (`make release`) performance comparison; never use a debug
  build for performance conclusions
- [x] MIR instruction counts per representative Lambda and LambdaJS frame are
  lower than the captured N0 controls, with the exact bodies, instruction
  counts, local counts, and percentage reductions recorded
- [x] final grep proves the magnitude-directed `u64 <= INT64_MAX` promotion and
  retired normalization paths are absent
- [x] documentation status tables and this checklist reflect the landed tree

### 4.1 Completion record — 2026-07-20

- Scope was implemented on `master` while preserving the pre-existing dirty
  worktree. The pre-change MIR controls remain in `temp/stack_api_baseline_lambda.mir`
  and `temp/stack_api_baseline_js.mir` for local review.
- `make build`, `make build-test`, the C2MIR-free `make build-jube`, and the
  final release build completed successfully. The legacy C2MIR sources remain
  archived but are excluded from the main, Jube, and test configurations.
- Focused verification passed: 18/18 number-stack/classifier unit tests, 17/17
  numeric Lambda/procedural integrations, 2/2 new negative semantic tests,
  and the wide-`uint64` MarkReader regression.
- JIT, MIR-interpreter, and procedural forced-GC number-model probes matched
  their goldens. The complete root gate passed with 31 `NO_GC` imports and
  12,215 audited native functions.
- `make test-lambda-baseline` passed 3,498/3,498 tests: 2,105 input-parser
  cases and 1,393 Lambda runtime cases, including 559 Lambda, 385 LambdaJS,
  and 110 Node preliminary tests.
- `make test262-baseline` fully passed 40,261/40,261 runnable tests with zero
  non-fully-passing tests, zero failures, zero regressions, and `retry 0.0s`.
- Release `test/benchmark/kostya/matmul.ls`, measured as eight alternating
  runs against the pre-change release, improved from a 797.009 ms median to
  741.037 ms (7.0% lower). Both binaries produced `matmul: sum=-29562`.
- For the exact representative bodies, Lambda frame `_frame_review_0` fell
  from 101 instructions / 47 locals to 78 / 43 (22.8% fewer instructions),
  and LambdaJS frame `_js_frameReview_149` fell from 136 / 53 to body frame
  `_js_frameReview_149_body` at 104 / 49 (23.5% fewer instructions).
- Final source search finds no live `normalize_sized` symbol or magnitude-based
  `UINT64 <= INT64_MAX` promotion branch. Historical design records retain the
  old spelling only when explicitly labeling the retired policy.

---

## 5. Main implementation risks

1. **`integer` erased to `DECIMAL`.** This causes a result to use the wrong
   precision/context even when its printed value looks correct. Tests must
   inspect both carrier and user-visible type.
2. **GC allocation during exact conversion.** Converting one operand can collect
   while the other exists only in an unrooted register or transient Item.
3. **Static/runtime disagreement.** Native MIR chosen from a stale float result
   while the evaluator returns decimal can corrupt boxing, caller-home choice,
   or GC publication.
4. **Vector result storage.** A typed integer or float buffer cannot hold a
   decimal result; result-container selection must precede lane execution.
5. **Guest-language leakage.** Lambda's domain-selected `/` must not change
   JavaScript or Python guest semantics.
6. **Representation regression.** Arithmetic realignment must not revive inline
   full-width integers, the numeric nursery, or standalone numeric GC objects
   outside the explicit ownerless persistent fallback.

The order above addresses the highest-risk invariant first: define one result
decision, make entry conversions exact, then let runtime and code generators
consume the same decision.
