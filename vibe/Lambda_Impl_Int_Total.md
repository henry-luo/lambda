# Lambda — Total `int` Implementation Plan (C16)

**Status:** IN PROGRESS — landed work is green and committed-quality; the critical path is
**BLOCKED on a design decision** (§0.3). Last worked 2026-08-02.

**Gate state:** `test_lambda_gtest` **651/651** · `test_lambda_std_gtest` **104/104** ·
`test_mir_ratchet_gtest` 9/15 — **all 6 ratchet failures are pre-existing on master**
(verified by stashing all changes and re-running). Note the companion tuning doc records only
*one* pre-existing ratchet failure, so five went red in that work uncaught. **Triage them
before B1**, whose entire purpose is shrinking emission — a shrink cannot be verified against
a red ratchet.

**Supersedes:** `Lambda_Impl_Int_C16.md` (earlier, narrower draft).

**Design authority:** `vibe/Lambda_Semantics_Formal2.md` **C16** (rulings 1–14 + the
poison-symmetry summary) and **C17** (converters keep returning `error()`; nan stays truthy),
normative in `doc/Lambda_Formal_Semantics.md` §3, §4 (head), §4.1, §4.2, §4.6, §4.7, §5.1,
§11.4. Failure containment is `vibe/Lambda_Design_Type_Enforcement.md` TE-15/TE-16 —
implemented separately in `Lambda_Impl_Error_Handling.md`.

**Performance authority / companion:** `vibe/Lambda_Tune_Typed_Vs_C2MIR.md`. That analysis
and this plan are the same work seen from two sides: it measured a **9.48x** gap to C2MIR and
named M1–M8; C16 deletes one whole mechanism (M2), makes another sound (B1), and root-causes
the C14c script fallout (§12.1). **Land this plan before re-baselining the Result18 typed
columns** — several numbers there measure machinery this change removes.

**The change in one line.** Flex `int` becomes the float64-representable integers, so *int
arithmetic is total in `double`*. The 53-bit band survives only as the **compact-Item carrier
capacity**, never again as a semantic boundary.

---

## 0. Current state

### 0.1 Done and green

| Item | What landed |
|---|---|
| **B4 — `div`/`%` stay `int`** | `lambda_numeric_classify` no longer routes int `div`/`%` to float (nor `integer` to decimal); `int_integral_division` in `lambda-eval-num.cpp` serves both classifier arms. Truncation-toward-zero and dividend-signed remainder are C14c's, unchanged. Verified: `7 div 2 → 3 (int)`, `-7 div 2 → -3`, `-7 % 2 → -1`. |
| **A3 — int poison** | `int.inf` / `-int.inf` / `int.nan` as reserved compact payloads above `INT53_MAX`, discriminated by a sign-extended read (`LAMBDA_INT_VALUE_IS_POISON` and friends). No new tag, no cell. `i2it` admits them via `LAMBDA_INT_IS_ENCODABLE` so poison survives a packed-lane round-trip. Zero divisor: `7 div 0 → int.inf`, `-7 div 0 → -int.inf`, `0 div 0 → int.nan`, `x % 0 → int.nan` (undefined, not unbounded). |
| **B5 — vector lanes** | Vectorized `div`/`%` now match the scalar rule per lane (`[6,0,8] div 0` → `[int.inf, int.nan, int.inf]`). The spec required this; it had never actually held. |
| **Poison printing** | Was a three-site duplication (`print_array_num_elem` + two flat-array loops); extracted to `print_packed_int_elem` (`core/print.cpp`). Any future poison-representation change goes through that one helper. |
| **D2 — C14c cleanup** | The `int(…)` workarounds removed from `brainfuck`, `brainfuck2`, `json2`, `json_gen2` — dead code now that `%` returns int. The documented >300 s hang is root-caused away (brainfuck: correct output, 728 ms debug). The recommended "sweep for `div`/`%` into packed arrays" is **cancelled**, not deferred. |
| **A6** | The `INT53_MAX` comment rewritten to say *carrier capacity*, not domain bound — it was the clearest surviving statement of the retired semantics. |
| **A2 — carrier (INERT)** | Built and building, but **nothing produces it**: `LMD_TYPE_INT_BIG` (slot 28, non-double asserted), `type_id()`/`get_type_name` normalization to `int`, `box_int_number_stack` + `int2it` encoder, `lambda_int_item_value` accessor, `lambda_item_uses_scalar_home`/`lambda_item_adopt_scalar_home` rehoming, and `int2it` registered in the JIT symbol table. See §0.3 — the cell's *home* is wrong. |
| **A4 (partial)** | The print visitor `ItemOf<LMD_TYPE_INT>` reads via `lambda_int_item_value` and renders out-of-band ints with `%.0f` (exact: every representable value above 2⁵³ is integral). |

**Landed alongside — a pre-existing miscompile, separate concern, own commit.**
`fn f(x) float { let p: float = x; p }` failed MIR validation: an interior checked boundary
emits an error return that a native lane cannot carry (MIR rejects an Item into a `double`;
worse, it *silently accepts* it into an `int`, handing the caller error bits as a number).
Fixed by extending `function_return_may_defer` with `function_body_may_check_boundary`, which
denies the native return so the error rides the boxed Item. **Granting the callee an error
lane was tried and rejected**: the caller only reads `Context.mir_return_lane` for `can_raise`
callees, so the error silently became `0` — trading a loud crash for the exact substitution
§13-invariant-7 forbids.

### 0.2 Outstanding

| Phase | Outstanding |
|---|---|
| A | **A1** (flip `i2it`'s overflow arm) and **A2 completion** — both blocked on §0.3. **A4/A5** read-site audit: one known reader remains; classify each site *numeric* (→ `lambda_int_item_value`) vs *index/count* (→ `get_int56()`, in-band by construction). |
| B | **B1 flexint deletion** — the main perf event, and the largest single deletion. **B2** (`pack_compact_int` computing in double), **B3** (retire `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT` for the flex tier). All depend on A. |
| C | Not started, and **fully independent of A/B** — the lexer split (`grammar.js:27`, why `10e1` is float today) plus the literal band diagnostic. Can be done in parallel by anyone. ⚠ Also needs the *int literal parser* to accept exponent form, or `10e1` will tokenize as int and then fail to parse its value. |
| D | **D1** `ELEM_INT` lane decision (recommendation on record: keep the i64 lane, widen the array on an out-of-band store). |
| E | Not started — integrality admission, poison admission, the `is`-lattice change (`int ⊑ int64` dies; visible flip: `5 is int64` → false). |
| F | Not started — range-proven i64 lanes, re-measure. |
| ruling 13 | `integer.inf`/`integer.nan` and `integer div integer → integer` still unimplemented; shares the mpdec specials unblock with `decimal.inf`/`decimal.nan`. |

### 0.3 ⚠ BLOCKING DESIGN ISSUE — the carrier has no legal home

**Symptom.** With either producer flipped on, an out-of-band int stored into a container
prints as a raw pointer whose value *changes between runs* (11130012512 → 11277501280 →
9068249952). That instability is the tell: a stale read-site would print a *stable* wrong
number.

**Root cause.** Not a read-site bug. `box_int_number_stack` allocates on the **side number
stack**, which is frame-scoped. Repro: `show(2147483647 * 2147483647)` where
`fn show(v) => [type(v), v]` (`numeric_fastpath_edges:39`) — the product is celled, stored
into the array, and then `show` returns, restoring the number-frame watermark and reclaiming
the cell. The array holds a dangling pointer. No read-site fix can help; the value is gone.

**Why floats never exposed this.** `flt2it` only takes a cell for tiny/subnormal doubles —
every normal-magnitude double is inline self-tagged. Out-of-band *ints* are the exact inverse:
all large, so they would take a cell on the **common** path. The existing scalar machinery was
tuned for a case that almost never happens; C16 makes it the norm.

**GC-allocating the cell is NOT available.** `heap_alloc`/`heap_calloc` *abort* on scalar type
ids ("heap-scalar-invariant", `lambda-mem.cpp:596`, `:617`), enforcing the **No-Scalar-Cell
Invariant** — `vibe/Lambda_Design_Scalar_GC_Invariant.md`, status IMPLEMENTED, user-confirmed
2026-07-22: *no wide-scalar payload is ever allocated in the GC object zone.*

**The three options.** Everything else in C16 — `div`/`%` staying int, poison, totality
*within* the band, the honest static type — is unaffected by this choice and already landed.

- **(i) Follow the `int64` pattern — destination-owned storage.** Wide scalars escape their
  frame today via caller-donated homes and destination-owned storage: when an `int64` enters a
  container, the *container* owns the payload. Correct and invariant-preserving, but it
  touches every container-insertion site, and it means `2147483647 * 2147483647` — a common
  32-bit overflow pattern that used to become a free inline float — now needs destination
  storage.
- **(ii) Cap flex `int` at the compact band.** Arithmetic stays total *within* ±(2⁵³−1) and
  saturates to poison beyond, carrying no sparse values. Needs no carrier at all, so A1/A2
  collapse to nothing and B1 unblocks immediately. **But it amends C16 ruling 1.** Assistant's
  read: more attractive than it looks — the sparse band's practical value is thin, since
  values above 2⁵³ that must stay exact belong in `i64`/`integer`/`decimal`, which is already
  C16's own documented guidance — while its cost has turned out to be structural.
- **(iii) Keep promotion above 2⁵³** for the sparse band only. Cheapest, but re-opens O1 and
  re-introduces the "static type says int, value is float" hole that motivated C16.

**Until this is decided**, both producers stay reverted and carry `TODO` comments naming the
exact edit: `pack_compact_int` (`lambda-eval-num.cpp`) and the two JIT flex-int promote lanes
(`transpile-mir.cpp`). The tree is green with the carrier inert.

### 0.4 Method note — audit empirically, and sequence audit-before-flip

The plan originally ordered A2 (carrier) → A4/A5 (read audit). **That is backwards.** With
producers live and readers unaudited, every misreader surfaces the cell pointer as the value.
Blast radius measured at 2–4 tests, so it fails loudly rather than silently. The audit is also
far cheaper done empirically than by cold-reading ~90 `get_int56` sites: flip a producer, run
`test_lambda_gtest`, and the misreaders announce themselves.

---

## 1. The constraint that shapes everything: tag space

Audited 2026-08-01 against `lambda/lambda.h` (this closes C16's open "boxed encoding /
tag-space audit" item):

- An inline double **is its own Item, using all 64 bits** — `lambda_float_ptr_to_item`
  ([lambda.h:1293-1301](../lambda/lambda.h:1293)) returns the raw bits whenever
  `bits & ITEM_DBL_MASK` (`0x6000000000000000`, [lambda.h:140](../lambda/lambda.h:140)) is
  non-zero.
- Every *tagged* value spends its high byte on the tag and keeps a **56-bit payload**
  (`ITEM_HIGH_BYTE_MASK`, asserted at [lambda.h:1246](../lambda/lambda.h:1246)).

**Consequence: an int-tagged Item can never carry an inline double.** "Represent `int` as
float64 internally" is available in *native lanes only*; the boxed representation must stay
either a compact integer payload or a **tagged pointer to a double cell** — exactly what
`d2it` ([lambda.h:1286](../lambda/lambda.h:1286)) already does for out-of-band floats. The
design below follows that precedent rather than inventing a carrier.

## 2. Three carrier layers

| Layer | Carrier | Notes |
|---|---|---|
| Boxed `Item` | compact 56-bit payload for `\|v\| ≤ 2⁵³`; double-cell pointer above | two int tags. ⚠ **The cell's home is unresolved — see §0.3.** Under option (ii) this row collapses to the compact payload alone. |
| JIT native lane (default) | `double` (`MIR_T_D`) | no tag, no checks — the semantics *is* binary64 |
| JIT native lane (optimized) | `i64`, **only when range-proven ≤ 2⁵³** | invisible optimization; unproven ⇒ stays double and is still correct |

**Keep the compact threshold at 2⁵³; do not widen to the payload's 2⁵⁵.** Above 2⁵³ not every
integer is float64-representable (evens, then multiples of 4), so a wider compact payload
could encode a value that is *not a member of the int domain*. Capping at 2⁵³ keeps "a compact
payload is always a valid `int`" true by construction and both conversion directions exact.

**Why the `i64` lane must stay range-proven.** Below 2⁵³, `i64` and `double` arithmetic agree
exactly. Above it they diverge: `i64` produces exact values such as `2⁵³ + 1` which are **not
in the int domain**. The proof obligation is soundness-critical — but because the *default* is
`double`, a missing proof costs speed, never correctness. That is the property which makes the
O1-class divergence unrepresentable rather than merely fixed.

---

## 3. Convergence with the tuning work

`Lambda_Tune_Typed_Vs_C2MIR.md` landed A1, A2 (partial), A3 (partial), B1, C2, D1a, D1b
(partial), D3 on 2026-08-01. This plan interacts with nearly all of it. Read this table
before touching either document's items.

| Tuning item | State there | What C16 does to it |
|---|---|---|
| **M2** — flexint lane boxes declared-int arithmetic | open cost, ~9–20 instrs/op | **Deleted.** No promote lane exists; `int ⊕ int` is one `DADD`/`ADD`. The entire box-or-promote decision disappears. |
| **A2** — checked-unboxed int arithmetic (partial) | landed for one consumer, +2–3% | The plumbing (`native_int_out` on `transpile_binary_out`, [transpile-mir.cpp:4669](../lambda/runtime/transpile-mir.cpp:4669)) **survives and generalizes**: it stops being "box or not" and becomes "which carrier". Its cold lane changes *meaning* — today box-float-then-`it2i` (a **type** change, deliberately preserving O1); under C16 it widens the carrier to double at the **same** type. |
| **A2** — unwired consumers (decl/assign initializers, for-range bounds) | listed as remaining | **Subsumed.** With `double` as the default native carrier there is no consumer list to wire: unboxed *is* the lane. |
| **M1 / A1** — statically-true boundary elision | landed, the session's big win | **Amplified twice.** (i) `int ⊕ int : int` is now honest, so more boundaries classify PROVEN instead of ANY-downgraded. (ii) int→float admission becomes a **pure retag** (both are doubles), so `lambda_boundary_is_redundant` ([type_contract.hpp:31](../lambda/runtime/type_contract.hpp:31)) can gain an int→float arm — today that widening is the exact reason elision had to be conservative ("Proven ≠ redundant", §3 T-A1). |
| **M3 / A3** — native returns, load-bearing half not landed | blocked on "what does every `return` produce" analysis | Analysis unchanged, **payoff grows**: pure-int bodies become provably non-raising (int arithmetic cannot fail), so more functions qualify for a native return *with no error lane at all*. |
| **D2** — scalar return mode | blocked on the same analysis | Same. Implement D2 + A3 together, after this plan. |
| **B1** — native counted `for` | landed, sieve −37% | **Made sound.** The i64 induction variable is exactly a range-proven lane (§2); before C16 it silently depended on INT53 semantics. Extend the same proof to array indices (§4 Phase F). |
| **M7 / C1 / C2** — typed arrays | landed | **New work.** `ELEM_INT` reads box through `i2it` ([lambda-data-runtime.cpp:488](../lambda/runtime/lambda-data-runtime.cpp:488)), which today returns `ITEM_ERROR` outside INT53 — the O1 mechanism *inside arrays*. Needs the lane decision in Phase D. |
| **§12.1** — brainfuck hang; the C14c `int % int → float` fallout | fixed per-script with `int(...)` wrappers; a sweep was recommended | **Root-caused away.** C16 ruling 4 returns `div`/`%` to `int`. The wrappers in `brainfuck2.ls`, `fft2`, `cd2`, `json2` become dead code and should be removed; **the recommended sweep is cancelled**, not deferred. |
| **O1** — INT53 divergence | "stays open; A2 doesn't need it resolved" | **Closed.** |
| "Don't touch INT53 overflow semantics to buy speed" | stated guidance | Superseded in *motive only*: C16 is a correctness decision (TE-15 made every int-annotated binding a potential defect site). The speed is a consequence. |
| "Don't add declared types to benchmark sources" | stated guidance | **Keep** until this plan lands; C16 removes the last int-specific reason annotations pessimize, but T-A/T-C rows must be re-measured first. |

**Sequencing note.** Phase B below deletes code that A2 just parameterized. Do not treat that
as reverting A2 — the parameter stays, the branch it selects between changes. Land Phase B on
top of A2 rather than around it.

---

## 4. Phase order

```text
Phase A (representation: carriers, converters, poison)
    └── Phase B (arithmetic: delete flexint lowering, div/% domains)
            ├── Phase C (frontend: lexer, literals, inference, printing)
            ├── Phase D (packed arrays + C14c workaround removal)
            └── Phase E (boundaries: admission, lattice, elision arms)
                    └── Phase F (perf: range-proven i64 lanes, re-measure)
```

⚠ **Two corrections to this order, both learned in implementation (§0.4):**
1. **Within A, audit readers BEFORE flipping producers.** A2-then-A4 is backwards.
2. **Phase C does not depend on A or B** and can proceed in parallel at any time. In practice
   D2 was also completed early, since B4 alone unblocked it.

### Phase A — representation

- **A1. `i2it`'s overflow arm is the O1 fix** ([lambda.h:1279-1282](../lambda/lambda.h:1279)).
  Today `… : ITEM_ERROR` — the exact mechanism behind the measured divergence (a raw
  out-of-band `i64` re-boxed through the 53-bit guard yields the bare `ITEM_ERROR` singleton;
  repro `temp/overflow_fn_test3.ls`). It becomes "box into a double cell".
  **BLOCKED on §0.3**; moot under option (ii).
- **A2. Second int tag** for the sparse band — **built, inert, and its cell home is wrong.**
  The tag, normalization, encoder, accessor and rehoming all exist (§0.1); what does not work
  is the *storage*: the number-stack cell cannot escape into a container (§0.3). ⚠ The
  original wording below ("on the existing scalar-home / number-stack machinery") is exactly
  the mistake — `lambda_item_adopt_scalar_home` covers the **return** path only, not container
  insertion. **BLOCKED on §0.3**; deleted entirely under option (ii).
- **A3. Poison values** `int.inf` / `-int.inf` / `int.nan`, and (C16 ruling 13)
  `integer.inf` / `-integer.inf` / `integer.nan`. The int trio can plausibly live as three
  reserved compact payloads (zero-alloc) — evaluate against A2's cell carrier. `integer` is
  mpdec-backed, so its poison rides the **same unblock as `decimal.inf`/`decimal.nan`**
  (mpdecimal supports the specials; the Lambda wrapper filters them —
  `lambda-decimal.cpp` ~:306/:374/:420).
- **A4. Unbox path.** The natural target for an int Item is now a `double`: compact ⇒ `i2d`,
  sparse ⇒ load from cell. One branch, at dynamic boundaries only. Audit `it2i` consumers —
  14 in `transpile-mir.cpp`, 23 in `lambda-eval-num.cpp`, 13 in `lambda-vector.cpp`.
  **`transpile.cpp` (39 uses) is the FROZEN C2MIR path — do not touch it** (CLAUDE rule 14).
- **A5. Audit `i2it`'s `ITEM_ERROR` consumers.** Any site reading that return as a meaningful
  error signal changes behavior silently once A1 lands. This includes `lambda-data-runtime.cpp`
  (Phase D) and the emitter's inline re-box sequences.
- **A6. Rewrite the comment at [lambda.h:1255-1256](../lambda/lambda.h:1255)** ("compact int is
  capped to the IEEE float64 safe-integer band; every packing/overflow check must enforce this
  bound"). It is the clearest statement of the *retired* semantics and will mislead every
  future reader; it must say **carrier capacity**, not domain bound.

### Phase B — arithmetic

- **B1. Delete the flexint dual-lane emission.** `mir_is_flexint_int_arith`
  ([:4645](../lambda/runtime/transpile-mir.cpp:4645)), `mir_emit_flexint_native_int`
  ([:4672](../lambda/runtime/transpile-mir.cpp:4672)), and the boxing arms in
  `transpile_binary_out` ([:4680–4840](../lambda/runtime/transpile-mir.cpp:4680)) collapse:
  `int ⊕ int` is a single double (or range-proven i64) operation. Keep `native_int_out` as the
  carrier selector. Also retire the duplicated inline INT53 test at
  [:10510-10522](../lambda/runtime/transpile-mir.cpp:10510) and the arg-site special case at
  [:11382](../lambda/runtime/transpile-mir.cpp:11382). **This is the largest single deletion
  and the main perf event**; expect `test/mir/mir_budgets.json` (MT7, 0% slack) to move — an
  *unexplained* jump means a gate is missing.
- **B2. `pack_compact_int_or_float`** ([lambda-eval-num.cpp:234](../lambda/runtime/lambda-eval-num.cpp:234),
  called from the `+`/`-`/`*` arms at :535-539, :581) becomes "pack int, choosing carrier by
  magnitude" — it no longer changes *type*. Switch its `__int128` computation to `double` so
  the interpreter and JIT cannot disagree about rounding.
- **B3. Overflow classification.** `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT`
  ([lambda-number.hpp:40](../lambda/runtime/lambda-number.hpp:40), set at :183, :205, :281) is
  retired for the flex tier; the int arm saturates to poison at the float-range extremes.
  Consumers at `transpile-mir.cpp` :4085 and :10454 follow.
- **B4. `div`/`%` domain change** (C16 rulings 4 + 13). `lambda_numeric_classify`
  ([lambda-number.hpp:179-183](../lambda/runtime/lambda-number.hpp:179) for the common
  int/float pair, [:265-274](../lambda/runtime/lambda-number.hpp:265) for the general arm) must
  stop mapping `int div int` → float and `integer div integer` → decimal. Zero divisor yields
  the domain's own poison. Sized `i64`/`u64` enter `integer` first, so `i64 div u64 → integer`
  (was decimal). **`/` is untouched everywhere.**
- **B5. Vector lanes.** Integer-array `div` stays an int-family array with per-lane poison
  (spec §4.7), replacing today's float-array result (`lambda-vector.cpp`).

### Phase C — frontend

- **C1. Lexer (C16 ruling 9).** [grammar.js:24-28](../lambda/tree-sitter-lambda/grammar.js:24):
  `float_literal` includes `seq(integer_literal, exponent_part)` (line 27), which is why `10e1`
  is a float today. Split `exponent_part` (line 23) by sign: an integer-spelled mantissa with a
  **non-negative** exponent joins `integer` ([:259](../lambda/tree-sitter-lambda/grammar.js:259)),
  a negative exponent stays `float` ([:261](../lambda/tree-sitter-lambda/grammar.js:261)). Then
  `make generate-grammar` — never hand-edit `parser.c`.
- **C2. Literal range check.** Unsuffixed int-form literals outside ±(2⁵³−1) are compile errors
  *even where sparsely representable* (`1e16` errors; `1.0e16` / `1e16n` are the fixes).
  Frontend diagnostic, no runtime path.
- **C3. Parser data homes unchanged** (C16 ruling 6): `int` iff within ±(2⁵³−1), else `int64`,
  else `decimal`. Do not widen ingestion to sparse representability.
- **C4. Inference.** `build_ast.cpp` keeps calling `lambda_numeric_classify`; the change is
  inside the classifier (B3/B4). **Verify the TS-3 consequence**: `int ⊕ int` must now yield a
  clean `int` static type with no ANY downgrade — that is what feeds A1's elision.
- **C5. Printing** (spec §4.6): finite ints print as integers at every magnitude, no exponent
  form; poison prints `int.inf` / `-int.inf` / `int.nan` and the `integer.*` trio, all
  parseable. **Golden churn expected** wherever today's escapes printed float-formatted
  (`1.80144e+16` → `18014398509481982`).

### Phase D — packed arrays and the C14c cleanup

- **D1. `ELEM_INT` lane decision.** `array_num_get`-side read boxes via `i2it`
  ([lambda-data-runtime.cpp:488](../lambda/runtime/lambda-data-runtime.cpp:488)) — today an
  out-of-band element reads back as `ITEM_ERROR`. Two options:
  - **(a) RECOMMENDED — keep `int[]` as an i64 lane for the compact band; widen the array on
    an out-of-band store.** Rationale: int arrays are overwhelmingly indices and counters, so
    the i64 lane is what makes `a[i]` a single indexed load (avoiding a `cvttsd2si` per
    access); sparse-band elements are vanishingly rare; and per-array widening reuses the
    existing specialize→generic conversion machinery. Cost: one range test on the store path,
    which the store already performs.
  - **(b) Make `int[]` a double lane.** Uniform with the semantic carrier, exact for every int
    by construction, same 8 bytes/element, no store check — but every index use pays a
    `d2i`, and it merges `ELEM_INT` with `ELEM_FLOAT64`, which the number-model record
    explicitly kept distinct as "real semantics".
  Whichever is chosen, the read at :488 must stop being able to produce `ITEM_ERROR`.
- **D2. Remove the C14c workarounds.** `int % int → int` is restored, so the `int(...)`
  wrappers added to `brainfuck2.ls`, `fft2`, `cd2`, and `json2` are dead; remove them and
  re-verify each against its golden. The untyped `brainfuck.ls` fix from §12.1 likewise
  reverts to the natural spelling. **Cancel** the recommended "sweep for `div`/`%` flowing
  into packed arrays" — its root cause is gone.

### Phase E — boundaries

- **E1. Value-aware admission** switches from the band test to an **integrality** test: any
  finite integral double (e.g. `1e300`) passes an `int` boundary. Confirm the validator and
  `lambda_type_check`'s numeric arm use integrality, not `INT53_MAX`.
- **E2. Poison admission** (C16 rulings 12 + 14): narrowing verifies membership — a shared
  `inf` passes and re-tags, a **foreign nan rejects** like `3.5`, sized-int boundaries admit no
  poison. Widening needs **no check** (poison classifies upward), so `int → float` and
  `int → integer` stay statically closed.
- **E3. `is` lattice** (ruling 10): drop `int ⊑ int64`; the chain is `i32 ⊑ int ⊑ integer`
  (onward to `decimal`), `int ⊑ float` definitional, `int ∥ int64`. Enumerate affected goldens
  — this is a visible flip (`5 is int64` → false).
- **E4. Extend A1's elision with an int→float arm.** With both carried as doubles, that
  admission is a retag; `lambda_boundary_is_redundant` can return true for it, which is
  precisely the case §3 T-A1 had to exclude ("it *widens* `int` to `float`"). Guard the
  bidirectional trap: float→int is still a *narrowing* and must keep its check.

### Phase F — performance

- **F1. Range-proven `i64` lanes.** Now the only reason to keep an integer lane: a double-lane
  array index pays `cvttsd2si` per access. B1 (native counted `for`) already establishes the
  proof for loop counters; extend it to indices derived from `len()` and from proven counters.
- **F2. Re-measure and re-baseline.** Re-run `run_benchmarks.py -e mir,c2mir,go --typed`;
  update the Result18 typed columns and the `Lambda_Tune_Typed_Vs_C2MIR.md` per-class
  scorecard. Expected movement concentrated in the **sieve-class** (M2 gone from the body) and
  the **fib-class** (M2 gone from `n-1`; A3 still the binding constraint). Release build only.

---

## 5. Gates

- `make test-lambda-baseline` 100% per phase; `make build-test` green.
- MIR budgets re-baselined **with per-gate justification** — Phase B should shrink them
  materially, and a shrink is as suspicious as a growth if it is not explained.
- New `*.ls` tests with `*.txt` goldens for: totality (`(2⁵³−1) + (2⁵³−1)` stays int),
  `div`/`%` poison in both int and integer domains, poison printing/round-trip, `10e1` vs
  `10.e1` vs `1e16`, lattice flips (`5 is int64` → false), narrowing nan rejection, widening
  poison flow-through, and an `int[]` element in the sparse band (Phase D).
- **The C16 equivalence property, asserted directly: erasing the `i64` lane must not change
  any result.** F1's optimization is unobservable — spec §13 invariants 1 and 3 applied here.
- The `temp/overflow_fn_test3.ls` repro must show one consistent answer in both boxed and
  native-arithmetic consumption (the O1 close-out).

## 6. Open before coding

- **§0.3 — the carrier's home. This is the blocker; everything else in A/B waits on it.**
- ~~A2's tag-slot choice~~ — RESOLVED: slot 28, four were free (`LMD_TYPE_COUNT` 28 ≤ 0x20).
- ~~Whether int poison uses reserved compact payloads or cells~~ — RESOLVED: reserved compact
  payloads above `INT53_MAX`, zero-alloc, no second tag needed.
- D1's `int[]` lane decision (recommendation: (a) keep i64, widen on out-of-band store).
  Note this partly depends on §0.3: under option (ii) there is no sparse element to store.
- Bitwise/shift domain over the sparse band (C16 open item; recommendation on record: define
  for finite values within ±(2⁵³−1), `error()` outside — bit reinterpretation is not number
  math).
- Whether `Lambda_Type_Numbers.md` and `Lambda_Semantics_Number_Model.md` need the same
  amendment pass as the spec (both predate C16; the latter's `is`-lattice section is known
  stale per ruling 10).
