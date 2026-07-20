# Lambda Number Model v2

**Status:** adopted design (2026-07-08; revised 2026-07-20) — Part 1 is the normative model; Part 2 is an informative snapshot of implementation gaps
**Changelog:** v2 born from the JS-interop question "any issue mapping JS number to `TYPE_FLOAT`?"; N1–N9 confirmed 2026-07-08; sized-scalar arithmetic (Go-style) added same day; division rule and literal-typing rule resolved same day; **`is`/`type()` semantics finalized same day after three iterations** (flat → hybrid → final): **uniform exact-embedding subtyping over all scalar numeric types; containers covariant where values copy, invariant at `var`-param borrows** (a brief blanket-invariance detour was reverted to the pre-existing normative ruling, ADR §9.2/C12) — supersedes both the draft lattice paragraph and the interim flat rule (history preserved in §6); doc restructured into spec form same day.
**2026-07-20 arithmetic decision:** operand promotion is type-directed, never magnitude-directed. Sized-integer × sized-integer arithmetic remains in a Lambda-selected sized lane and then follows Go overflow behavior. A sized integer mixed with a non-sized number first enters its smallest complete non-sized domain (`i8`…`u32 → int`, `i64/u64 → integer`) and then follows the value tower. Flex-`int` result overflow deliberately remains `int → float` for practical performance, accepting correctly rounded precision loss at boundary cases.
**Normative home:** this doc designs in the area owned by `../doc/Lambda_Formal_Semantics.md` **§4 Numerics** (§4.1 two-tier integers [C3] · §4.3 promotion lattice [C3] · §4.4 decimal tiers [C13] · §4.5 float↔decimal [C8.5a] · §4.7 division) and touches **§5 Equality** [C8], **§6 Ordering** [C11], **§9.2 Covariance** [C12]. Where v2 amends a section, the amendment flows back into the ADR when implemented; until then the ADR governs the language and this doc records the agreed direction.
**Grounding:** `js_make_number` (`lambda/js/js_runtime_value.cpp`), the compact-int band (`INT56_MAX` in `lambda/lambda.h` = ±(2⁵³−1), deliberately the JS safe-integer band), the two libmpdec contexts (`g_fixed_ctx` decimal128 / `g_unlimited_ctx` arbitrary in `lambda/lambda-decimal.cpp`), the `NUM_SIZED` carriers and `EnumArrayNumElemType` (`lambda/lambda.h`), the scalar-home contract (`Lambda_Design_Stack_API.md`), and the Go spec (`go.dev/ref/spec#Numeric_types`, `#Integer_overflow`, `#Constants`, `#Conversions`).

---

# Part 1 — The Model

## 1. Principles

- **P1 — Type-directed FFI, never value-directed.** A Lambda value's foreign type (JS `number` vs `BigInt`, …) is a function of its Lambda *type*, never of its magnitude. (The rejected alternative is the snowflake-ID bug — §6.)
- **P2 — Tier-hiding.** Hide representation tiers when the external contract is tier-independent (decimal's tiers hide; `integer` stays a distinct type *because its FFI face differs*).
- **P3 — Two orthogonal axes, each uniform.** `==` is **equality**: value-precise across representations for values (ADR §5.2 Numbers [C8]: `1 == 1.0`, `2.5f32 == 2.5`), exact equality for types. `is` is **subsumption**: `x is T` ⟺ `type(x) ⊑ T`, and with a type on the left, `T1 is T2` ⟺ `T1 ⊑ T2` — where ⊑ is the exact-embedding lattice over scalar numeric types, **covariance over containers where values copy / invariance at `var`-param borrows** (§3.4, per the normative §9.2/C12), and membership for unions. No operator mixes the axes.
- **P4 — Exact embeddings up the tower; division is domain-selected.** `int ⊂ integer ⊂ decimal`; `float ⊂ decimal` (every binary64 has an exact finite decimal expansion); `float ∥ integer`. Mixed-type promotion never loses information while converting an operand. True `/` then uses the selected arithmetic domain: `int / int → float`, with binary64 rounding where necessary, while `integer / integer → decimal`, with the §2.4 decimal rounding rule. Two explicit arithmetic-domain rules sit beside that exact lattice: sized integer lanes wrap per §3.3, and flex-`int` result overflow enters `float` and may round per §2.1. *(Extends the ADR's §4.3 promotion lattice [C3] and §4.7 division rules with the `integer` column.)*

## 2. Value types

User-visible numeric value types: **`int` · `integer` · `float` · `decimal`**, grouped by the union **`number`**.

### 2.1 `int`

The everyday integer is the ADR's **flex `int`** (`Lambda_Formal_Semantics.md`
§4.1–4.2 [C3]). Its domain is **±(2⁵³−1) by design — exactly the JS
safe-integer band**, which makes every `int → float` or JS-`number` conversion
exact (§5.4). `int64`/`i64` is a distinct sized machine type, not a hidden
storage tier of `int`.

For `+`, `-`, and `×`, a result inside the compact band remains `int`. A result
outside the band promotes to `float`, and subsequent arithmetic stays float.
The conversion into float is exact for each source `int`, but the overflowing
mathematical result is only correctly rounded to binary64: odd or otherwise
unrepresentable integers above 2⁵³ can lose precision. Retaining this
`int → float` overflow rule is a deliberate 2026-07-20 practical/performance
decision; automatic `int → integer` overflow is not adopted (W5).

### 2.2 `integer` — arbitrary precision, the BigInt counterpart

- **Backing: libmpdec's unlimited-precision context** (`g_unlimited_ctx`) — *not* decimal128: BigInt is unbounded, and 2¹²⁸ is 39 digits, already past 34. With unlimited backing, `integer ⇔ BigInt` is faithful in both directions.
- **Literal:** the `n` suffix — **`integer` always** (A.5 decision, 2026-07-11, supersedes the ON2 lexical rule): `1n`, `1e3n` → integer; fractional or negative-exponent spellings (`1.0n`, `1.5e-2n`) are a **compile error** pointing at the `m` suffix. The suffix alone names the type; `n` matches JS BigInt exactly in both directions (every valid Lambda `n` literal is a valid JS BigInt literal and vice versa).
- **Arithmetic:** `+`, `-`, `×` stay in `integer`; **`integer ÷ integer → decimal`** (P4 — no JS-BigInt-style floor). Mixing: `integer op float → decimal`, `integer op decimal → decimal` (§2.4 tower table). JS's throw-on-mix is rejected — JS throws because it lacks an exact meeting type; Lambda has one.
- **Why a distinct type rather than an `int` tier:** P2 — its FFI face (`BigInt`) differs from `int`'s (`number`); folding it into `int` would make egress value-dependent, violating P1.

### 2.3 `float` — binary64, the JS-number twin

- IEEE-754 binary64; **`f64` is a spelling alias for the same type** (§3.2), not a second type.
- **Considered-and-kept** (the "is float redundant given decimal?" question): (a) the JS bijection requires a native binary64 type — it is the identity mapping in both directions; (b) it is the hardware type — SIMD lanes, ArrayNum, K19 pairwise reductions, Stage-A parallelism, and all of Radiant (layout is float by rule); (c) the world's data is binary64 (JSON-in-practice, Float64Array, Arrow, C ABI). `float` and `decimal` serve disjoint domains: measurements vs exact human quantities.
- **Specials:** −0, NaN, ±Infinity flow untouched; the canonical float encoding preserves −0; NaN payloads pass through unmodified (observable only via typed-array bit views; spec latitude).

### 2.4 `decimal` — one type, invisible tiers, BigDecimal-family arithmetic

*(Amends the ADR's §4.4 decimal tiers [C13] — the tiering survives, the source-level `n`/`N` split does not; and §4.5 float↔decimal [C8.5a] governs the embedding edge this section builds on. The §4.7 division rules gain the ON1 precision formula.)*

- **One type, one literal suffix: `m`** (A.5 decision, 2026-07-11 — `100m`, `1.5m`, `1.5e-2m`; integer-valued spellings included, giving money its natural literal). Storage tiers invisibly: decimal128 layout for values that fit, unbounded above. The source-level `n`/`N` split is removed (`N` retires from the grammar; fractional `n` spellings retire under A.5). **Literal digits are always preserved exactly regardless of tier** — the `N` suffix only ever controlled arithmetic-context participation, and context control belongs on operations: `decimal(x, prec:, rounding:)` for the advanced case, **`quantize` / `round(x, places)` as the everyday money tool**. `type()` reports `decimal` at every tier; `==` holds across tiers (P3).
- **Arithmetic is BigDecimal-family, not IEEE-context-family.** `+`, `-`, `×`: **exact, auto-growing** (digit growth bounded per op; promotion is mere storage tiering). `÷` and other inexact ops (roots): **round at `max(34, digits(a), digits(b)) + 4` guard digits, round-half-even.** Lineage of the division rule:
  - the floor of 34 = the **IEEE/TC39 parent** (IEEE 754-2008 decimal128; TC39 JS Decimal; Python `decimal` at 28) — everyday money division behaves exactly like decimal128 and like JS's future Decimal;
  - the proportional term + 4 guard digits = the **PostgreSQL parent** (PG `numeric`: derived scale + 4 guard; SQL Server/DB2 guard-6 formulas; Oracle full 38+) — the battle-tested database family, fitting Lambda's relational identity;
  - **half-even** = the IEEE/Python/Java consensus (PG's half-up is the SQL-compat outlier); chosen for zero accumulation bias.
  - Pleasant property: chained divisions grow digits **linearly** (+4 per step), unlike exact `×` which doubles — no cap needed; the unbounded tier absorbs it.
- **Costs accepted consciously:** results diverge from strict IEEE decimal128 (one-time golden migration — W1; `quantize` at SQL/decimal128 interop boundaries), and exact-`×` chains are a performance cliff (documented; `decimal(..., prec:)` is the escape valve — the Python-bigint trade).
- Decimal has its own NaN/±Inf (IEEE 754-2008 / libmpdec) for the `float → decimal` embedding edge.

### 2.5 The tower where types meet

| | `int` | `integer` | `float` | `decimal` |
|---|---|---|---|---|
| `int` | C3 (unchanged) | `integer` | C3 (unchanged) | `decimal` |
| `integer` | | `+−×` → `integer`; `÷` → `decimal` | `decimal` | `decimal` |
| `float` | | | `float` | `decimal` |
| `decimal` | | | | `decimal` (§2.4 rules) |

All mixed-type meets are **exact embeddings** (P4); nothing silently loses
precision while an operand enters the selected common type. The `int`/`int`
cell retains C3's separate result-overflow rule: crossing the compact band
selects float arithmetic and can round the result as described in §2.1.
Decimal division and explicit `quantize`/conversion carry their own documented
precision decisions. *(This table is the v2 extension of ADR §4.3 [C3]; the
`int`/`float` cells defer to it unchanged.)*

### 2.6 `number`

The declared union of the numeric types. Grouping in `is` happens **only** through unions (§3.4); `type()` never returns `number`.

## 3. Sized-ness (storage types)

### 3.1 The family, and what "sized" means

`i8 i16 i32 i64 · u8 u16 u32 u64 · f16 f32 f64` — **fixed-width storage and interop types**: ArrayNum lanes, native-module struct fields, wire formats, typed-array interop. They are *not* hidden tiers of `int`/`float` (N10) — size is semantic at the storage level (`[i8]` ≠ `[i16]`: different layout, range, FFI shape).

Sized *scalar* values exist for widths below the native word: `i8/i16/i32/u8/u16/u32/f16/f32` carry a runtime `NUM_SIZED` subtype (32-bit payload). The three names that **coincide with native representations are aliases, not new types**: **`f64` ≡ `float`, `i64` ≡ `int64`, `u64` ≡ `uint64`** — a sized name denoting an existing representation is a spelling, by necessity (64-bit values cannot pack a sized carrier, and boxing one would create a distinction with no content).

### 3.2 The alias clause and canonical naming

Aliases are **accepted everywhere on input** (annotations, `is` targets, signatures, lane declarations) and **never emitted**: `type()` returns the canonical name (`float`, never `f64`); a double-lane array is `[float]`, whether declared `[float]` or `[f64]`. Aliases in, canon out — otherwise the alias re-enters as a second `type()` string for one type. Precedent: C# `double` ≡ `System.Double`; SQL `DOUBLE PRECISION` ≡ `FLOAT(53)`.

### 3.3 Arithmetic: Go-style, precisely

Sized scalar arithmetic follows the Go spec — no UB, no overflow panic, no optimizer assumption that overflow cannot happen. *(Kin to the ADR's C3 "exact-until-float, Go-aligned machine tier" doctrine — the same Go alignment, applied to the sized family; division/remainder-by-zero returning `error()` matches ADR §4.7.)*

- **Unsigned:** `+ - * <<` are modulo 2ⁿ: `255u8 + 1u8 → 0u8`; `0u8 - 1u8 → 255u8`.
- **Signed:** overflow produces the deterministic two's-complement result: `127i8 + 1i8 → -128i8`; `-128i8 div -1i8 → -128i8`.
- **Division/remainder by zero:** `error()` — never wraps, never panics, never yields a lane value.
- **Shifts:** negative counts → `error()`; non-negative counts compute mathematically, then truncate to the lane width: `1u8 << 8 → 0u8`.
- **Constants:** exact constant evaluation does not wrap; a suffixed literal/constant expression must be representable in its declared type — `128i8`, `256u8` are compile-time errors.
- **Conversions:** constant vs non-constant deliberately split (Go's rule): `u8(-1)` on a constant is a compile-time error; on a non-constant, sign/zero-extend then truncate with no overflow signal (`let x = -1; u8(x) → 255u8`). Float-sized conversions round to the destination IEEE format.
- **Sized floats:** `f32` follows Go `float32` (working precision allowed; conversion/assignment rounds to binary32); `f16` same principle with binary16 (primarily storage/interop).

#### 3.3.1 Sized integer × sized integer

Integer operands remain in the sized domain. Go defines arithmetic and
overflow once a machine lane has been selected; Go itself normally requires an
explicit conversion between different named integer types, so Lambda supplies
this mixed-width lane-selection extension:

| Sized integer operands | Lambda result lane |
|---|---|
| same type | that type |
| same signedness, different widths | wider participating width |
| signed width represents the complete unsigned domain | that signed width |
| next wider signed width represents both domains | that next signed width |
| no signed lane represents both, including `i64` with `u64` | wider unsigned lane (`u64` at 64 bits) |

Examples: `i8 + u8 → i16`, `i32 + u32 → i64`, and `i64 + u64 → u64`.
After selection, the operation—including overflow—uses the chosen fixed width;
it does not promote to `int`, `integer`, `float`, or `decimal`. This rule covers
`+`, `-`, `*`, integral `div`/`%`, bitwise operations, and shifts. Constants
must still fit exactly; runtime results wrap as specified above. `/` is not an
integer-lane operator in Lambda: it is true division and exits through the
non-sized mapping in §3.3.2, consistent with ADR §4.7.

#### 3.3.2 Sized integer × non-sized number

A mixed sized/non-sized expression leaves the machine domain before the
operation. First map the sized integer by its **complete type domain**, never by
the current value:

| Sized integer source | Non-sized entry type |
|---|---|
| `i8/i16/i32/u8/u16/u32` | `int` |
| `i64/u64` (`int64/uint64`) | `integer` |

Then meet that entry type with the other non-sized operand through §2.5:

| Sized integer | with `int` | with `float` | with `integer` | with `decimal` |
|---|---|---|---|---|
| `i8/i16/i32/u8/u16/u32` | `int` | `float` | `integer` | `decimal` |
| `i64/u64` | `integer` | `decimal` | `integer` | `decimal` |

Consequences:

- `255u8 + 1u8 → 0u8`, but `255u8 + 1 → 256` as `int`;
- `9223372036854775807i64 + 1 → 9223372036854775808` as `integer`;
- `18446744073709551615u64 + 1 → 18446744073709551616` as `integer`;
- `1i64 + 0.5` and `1u64 + 0.5` both produce `decimal`;
- small and large `u64` values take the same route. There is no
  `u64 ≤ INT64_MAX → int64` rule.

For `/`, map every sized integer operand through this section even when both
operands are sized; the selected non-sized domain controls the result:
`i8 / u8` enters `int / int → float`, while `i64 / u64` enters
`integer / integer → decimal`. Integral `div` and `%` stay in the selected
sized lane when both operands are sized, or in the selected `int`/`integer`
domain after a sized/non-sized mix. A sized integer mixed with `f16`/`f32` is
category mixing rather than sized-integer arithmetic: map the integer as above,
map the sized float to `float`, and use §2.5. Mixed sized floats where one
exactly contains the other retain the wider sized-float lane
(`f16 + f32 → f32`).

This operational promotion is conversion, not `is`-subtyping (§3.4). Operand
types alone select the route. Result overflow may still trigger flex-`int`'s
documented `int → float` behavior after both operands have entered `int`; that
is an arithmetic result rule, not value-directed operand promotion.

### 3.4 `is` and `type()`: subsumption by exact embedding; containers invariant

**The scalar rule — one principle, every edge derived:**

> **T1 ⊑ T2 ⟺ every T1 value embeds exactly (value-preserving) into T2's value domain.**
> `x is T` ⟺ `type(x) ⊑ T` · with a type on the left, **`T1 is T2` ⟺ T1 ⊑ T2** (`int is integer` → true) · aliases resolve first (§3.2) · unions check membership · `type(x) == T` remains the exact check.

The derived lattice over all scalar numeric types — sized and semantic follow
the *same* subsumption rule. This lattice governs `is`, assignment, and exact
embedding. It does not replace §3.3.1's explicit machine-lane selection when
both arithmetic operands are sized; when an expression leaves the sized domain,
§3.3.2 uses this lattice to select its exact non-sized entry type.

- **Widening within a kind:** `i8 ⊑ i16 ⊑ i32 ⊑ int ⊑ int64 ⊑ integer ⊑ decimal` · `u8 ⊑ u16 ⊑ u32 ⊑ int` and `u32 ⊑ u64 ⊑ integer` · `f16 ⊑ f32 ⊑ float ⊑ decimal` · `int ⊑ float`.
- **Cross signed/unsigned:** `u8 ⊑ i16`, `u16 ⊑ i32`, `u32 ⊑ int64` — **never the reverse; `i* ⋢ u*`, ever** (negatives). *(user-confirmed)*
- **Integers into floats, by mantissa band:** `i8/u8 ⊑ f16` (±2¹¹), `i16/u16 ⊑ f32` (±2²⁴), `i32/u32/int ⊑ float` (±2⁵³ — the compact band *is* float's exact-integer band by design). **Same-width never fits: `i16 ⋢ f16`, `i32 ⋢ f32`, `i64 ⋢ f64`** — a float's mantissa is narrower than its width. `int64/u64 ⋢ float`. *(user-confirmed)*
- **Incomparables:** `float ∥ integer/int64/u64`; `int64 ∥ u64`.

**No comparison-operator overload for subtyping** (`type(x) <= T` considered and rejected): ⊑ is a *partial* order (`float` vs `integer` are incomparable) while the ADR's **§6 total order [C11]** gives all values — including type values — a *total* order that `sort`/`order by` run on (§6.1's "two relations, by design" doctrine is exactly why a third relation can't hijack `<=`). Overloading `<=` would make one operator mean two orders on the same operands. Julia met the same problem with a dedicated `<:`; Lambda's spelling for subtype is `is` itself, which costs no grammar and reads naturally in both positions.

**Containers: covariant where values copy, invariant where they're borrowed** — this is the pre-existing normative ruling (`doc/Lambda_Formal_Semantics.md` §9.2, C12), to which this doc aligns after a brief blanket-invariance detour (§6 history):

- **Covariant for `is`, reads, value parameters, and assignment**: `T1[] ⊑ T2[]` ⟺ `T1 ⊑ T2`. So `i8[] is int[]` → true, `[f32] is [float]` → true, `int[] is any[]` → true (§9.2's own example). Sound because these channels **copy** (C4): the classic covariant-array hole cannot arise without aliasing. Representation widening (e.g. i8 lanes → int lanes, unboxed → boxed) happens **lazily at COW-copy time** (§9.2).
- **Invariant at `var` parameters** — the *only* aliasing channel: passing `var xs: i8[]` to `pn f(var a: int[])` is a **compile error**; a `var` parameter's argument type must match exactly (after alias resolution). This is where the Java `ArrayStoreException` argument genuinely applies — `pn f(var a: int[]) { a[0] = 300 }` with an aliased `[i8]` — and §9.2 scopes the restriction to exactly that channel: *"C4 removed aliasing everywhere but the borrow channel, so the borrow channel carries the restriction (Rust's `&mut` shape)."*

The division of labor: users get useful covariance everywhere it is sound (the common cases — passing, checking, assigning); the type system pays invariance only at the one construct that borrows. Element reads compose with the scalar lattice as expected (an element of an `[i8]` array is an `i8` scalar; `elem is int` → true).

The case table (rows marked ✗ supersede earlier rulings/goldens):

| Check | Result | Why |
|---|---|---|
| `1.0 is float` / `is f64` / `is decimal` | true | alias; `float ⊑ decimal` |
| `1.0 is f32` / `is f16` / `is integer` | false | narrowing never subsumes; `float ⋢ integer` |
| `2.5f32 is f32` / `is float` / `is f64` / `is decimal` | true ✗ | `f32 ⊑ float(≡f64) ⊑ decimal` |
| `2.5f32 is f16` | false | narrowing |
| `42i8 is i8` / `is i16` / `is int` / `is f16` | true ✗ (`is i16` flips a golden) | widening; mantissa band |
| `42i8 is u8` / `is u16` | false | `i* ⋢ u*` ever (golden survives) |
| `1u8 is u16` / `is i16` / `is int` | true ✗ (supersedes the early "precise" ruling) | widening; cross-sign |
| `5 is int` / `is i64` / `is integer` / `is float` / `is decimal` / `is number` | true | lattice; union |
| `5 is u8` | false | `int ⋢ u*` (int has negatives; `is` operates on type, not value) |
| `100u64 is i32` / `1i64 is float` | false | range; mantissa (goldens survive) |
| `int is integer` / `float is decimal` | true | type-on-left `is` |
| `[1i8, 2i8] is [i8]` / `is [i16]` / `is [int]` / `is array` | true | container covariance (elementwise ⊑); kind membership |
| `[1.5, 2.5] is [i8]` | false | narrowing — covariance only widens |
| `var xs: i8[]` passed to `pn f(var a: int[])` | **compile error** | `var`-borrow invariance (§9.2/C12) |
| `type(1.0f64)` | `float` | canonical output (§3.2) |

**Tests to pin:** trues — `1u8 is u16`, `42i8 is i16` (golden migration, W1), `2.5f32 is float`, `42i8 is int`, `5 is i64`, `1.0 is decimal`, `int is integer`, `[1i8] is [int]`; falses — `1.0 is f32`, `1i16 is f16`, `5 is u8`, `1i64 is float`, `[1.5] is [i8]`; compile-error negative — `var i8[]` argument to a `var int[]` parameter (`test/lambda/negative/semantic/`). Arithmetic pins — same-lane wrap (`255u8 + 1u8 → 0u8`), mixed-sized lane selection (`i8 + u8 → i16`, `i64 + u64 → u64`), sized/non-sized exit (`255u8 + 1 → int`, `i64 + int → integer`, `u64 + int → integer`), magnitude independence at both sides of `INT64_MAX`, symmetric `i64/u64 + float → decimal`, sized `div` remaining in its machine lane, and true division exiting (`i8 / u8 → float`, `i64 / u64 → decimal`).

### 3.5 Lane tags: one tag per representation

- The ArrayNum lane enum keeps **exactly one tag per distinct representation**. The current `ELEM_FLOAT` / `ELEM_FLOAT64` pair (identical 8-byte double storage, by its own comment) **merges into a single tag** — C spelling `ELEM_FLOAT64` (readability: lane tags are about size) — produced uniformly by inference, `[float]`/`[f64]` annotations, and JS `Float64Array`. `[f64]` and `[float]` are one array type.
- **Explicit non-case:** `ELEM_INT` vs `ELEM_INT64` is *not* this merge — they differ semantically (compact-band domain + packed read-out vs full-range + boxed read-out). Any change there is a separate analysis.

## 4. `type()` formal vocabulary (numeric)

The set of names `type()` can return **is** the set of runtime numeric types — normative, one canonical spelling each:

| Canonical `type()` output | Accepted input aliases | Kind |
|---|---|---|
| `int` | — | value type; compact safe-integer band |
| `int64` | `i64` | native 64-bit signed sized type |
| `uint64` | `u64` | native 64-bit unsigned (FFI/lane boundary) |
| `integer` | — | value type, unbounded |
| `float` | `f64` | value type, binary64 |
| `decimal` | — | value type (tiered internally) |
| `i8` `i16` `i32` `u8` `u16` `u32` | — | sized scalar carriers |
| `f16` `f32` | — | sized scalar carriers |

Never returned: `number` (union), `f64`/`i64`/`u64` (aliases), tier names. Array types print canonical lane names: `[float]`, `[f32]`, `[i8]`, … The full type vocabulary is owned by the ADR (`Lambda_Formal_Semantics.md` §2.1 Types [C1]); this table is its numeric row, and float printing at the value level follows ADR §4.6.

## 5. JS interop & type mapping

**The governing rule of this section: one JS rule per type, per direction** — every Lambda numeric type has exactly one JS face, every JS numeric type has exactly one Lambda face, and no rule consults a value's magnitude (P1).

### 5.1 Inside LambdaJS: number ≡ `float`, uniformly

JS number *is* binary64; the mapping is exact by construction (NaN, ±Infinity, −0 included). **Compact-int packing is removed from `js_make_number`** — with it go the integer fast path, the `-0` special case, and the `JS_SYMBOL_BASE` guard (symbols keep the packed encoding; numbers never use it). The obligation that "every JS-visible surface keys on value, never representation" — the discipline that produced the `v18p` −0 bug and the Stage-4C int-vs-float inference family — is dissolved rather than policed.

**Perf plan, eyes open:** JS Number uses Lambda's canonical float encoding.
Most doubles are self-tagged in the `Item`; out-of-band transient values use an
activation/caller number home, and retained values normally copy into
destination-owned scalar storage. Only a bare-Item persistent boundary without
a natural owner uses the explicit interim GC fallback. MIR inference keeps hot
numbers as raw doubles in registers, and number-dense JS arrays use float64
lanes. The resulting allocation/rooting behavior is governed by
`Lambda_Design_Stack_API.md`, not by a JS-specific heap-boxing rule. Doubles do
exact integer arithmetic through 2⁵³, so integer-heavy JS follows the JS
specification's own precision envelope.

### 5.2 Ingress (JS → Lambda)

| JS | Lambda |
|---|---|
| `number` | `float` — always, exactly |
| `BigInt` | `integer` — always, losslessly |

Round trips normalize to `float` (deterministic; documented membrane normalization — `int 5` → JS → back is `float 5.0`).

### 5.3 Egress (Lambda → JS)

| Lambda | JS | Fidelity |
|---|---|---|
| `int` | `number` | **exact always** — compact band = JS safe band by design |
| `i8` `i16` `i32` `u8` `u16` `u32` | `number` | exact always (inside the safe band) |
| `f16` `f32` | `number` | exact (every binary16/32 value is a binary64 value) |
| `float` | `number` | identity |
| `integer` | `BigInt` | lossless, both directions |
| `int64` / `uint64` | **`BigInt`, always** | lossless; predictable `typeof` |
| `decimal` | `number` + **runtime warning, once per call site** | lossy by declaration; warn-throttled (W6) |

An API wanting JS-`number` ergonomics returns `int`; exactness is then guaranteed by the band alignment.

### 5.4 What v2 deletes

`js_make_number`'s three special cases; the JS-MIR int/float inference machinery *as a correctness burden*; the value-vs-representation audit on every JS ToString/format path; the membrane's representation-dependent `type()` divergence; the `N` suffix.

## 6. Rejected alternatives & prior art

**Rejected, with reasons on record:**
- **Value-directed egress** (`number` when small, `BigInt` when large) — the Twitter-snowflake-ID bug: passes tests with small values, breaks in production past 2⁵³; `number`/`BigInt` don't mix in JS arithmetic.
- **A distinct `TYPE_FLOAT64`** — a nominal split tracking no observable property (same size, range, rounding, layout as `float`); would force a result-tag decision on every arithmetic op and builtin, split the N1 bijection, and replicate the dual-lane-tag bug pattern language-wide.
- **The `is`-semantics design history** (three iterations in one day; the losing positions preserved):
  1. *Two-regime draft* (exact for sized targets, value-domain lattice for `is int`/`is float`, `type(1.0f64)` → `"f64"`) — rejected: two stipulated regimes, and the `"f64"` output requires exactly the runtime carrier the no-`TYPE_FLOAT64` decision forbids.
  2. *Flat rule* (`x is T` ⟺ `type(x) = T`, siblings disjoint, `2.5f32 is float → false`) — rejected in turn: it discarded value-domain truths (`int is integer`, `float is decimal`) that the tower (P4) already guarantees; its "transitivity trap" argument against embedding dissolved once sibling disjointness was deliberately abandoned.
  3. **Final (§3.4): uniform exact-embedding subsumption for all scalars + invariant containers** — one derived principle, no regimes, no stipulated table.
- **Blanket container invariance** — briefly adopted (simplicity + the `var`-param aliasing argument), then **reverted upon recalling the pre-existing normative ruling** (`Lambda_Formal_Semantics.md` §9.2, C12): covariance where values copy is sound *because* C4 removed aliasing from those channels; the Java array-store argument governs exactly one construct — the `var`-param borrow — and §9.2 places the invariance restriction exactly there (Rust's `&mut` shape). Precise scoping beats the blanket rule: users keep useful covariance in the common cases; the type system pays at the one aliasing channel. Lesson recorded: check the ADR before re-deriving — the ADR had both the question and the sharper answer.
- **Overloading `<`/`<=` for subtyping** — rejected: ⊑ is partial, C11's value order (which type values participate in, for `sort`/`order by`) must stay total; one operator cannot carry both. Julia's dedicated `<:` is the precedent for a separate spelling; Lambda's spelling is `is` with a type on the left.
- **Python-`decimal`-style all-rounded arithmetic** (loses exactness of ±×) and **exact rationals** (Raku/Racket — a different type philosophy where `1/3` is a rational).
- **JS's BigInt throw-on-mix** — JS throws for lack of an exact meeting type; Lambda's `decimal` is that type (§2.5).
- **Value-directed `u64` normalization** (`u64 ≤ INT64_MAX → int64`, otherwise
  decimal) — rejected: two values of one type must not take different promotion
  paths. `u64` enters `integer` whenever it mixes with a non-sized number
  (§3.3.2), irrespective of magnitude.
- **Promoting every mixed pair of sized integers out of the machine domain** —
  rejected by the 2026-07-20 decision. Lambda selects a sized result lane, then
  applies Go arithmetic and overflow semantics (§3.3.1).
- **The `n`/`N` dual suffixes** — precision-context control on literals; moved to operations.

**Precedent index:** Go spec (sized arithmetic, constants, conversions — §3.3's source) · V8's Smi (what §5.1 deliberately walks away from, trading representation cleverness for one clean rule) · C#/SQL name-aliasing (§3.2) · Java BigDecimal vs Python decimal vs rationals (§2.4's family choice) · IEEE 754-2008 / TC39 Decimal + PostgreSQL/SQL Server/Oracle (the division rule's two parents) · Python's unbounded ints (the accepted exact-arithmetic performance trade) · NumPy pairwise summation (the K19 sibling policy for reductions).

## 7. Cross-references

**`../doc/Lambda_Formal_Semantics.md` — the ADR this doc amends**: §4 Numerics [C3 integers/lattice, C13 decimal tiers, C8.5a float↔decimal, §4.6 float printing, §4.7 division] · §5 Equality [C8] · §6 Ordering [C11] · §9.2 Covariance [C12] — per its own section↔ledger mapping table · `Lambda_Semantics_Formal.md`/`Formal2.md` (the decision records behind those entries; A8 = the variance implementation gap, Part 2 item 11) · `Lambda_Semantics_Features.md` §1.8 **G7** (this is the numeric row of the per-guest projection table; G3 error-mapping is the sibling) · `Lambda_Design_Concurrency.md` K16/§4.6 (the membrane §5 simplifies) · K19 (deterministic reductions; the same order-as-spec philosophy) · K28 (the best-effort banner W2 rides) · `Lambda_Type_Numbers.md`, `Lambda_Type_Int.md`, and `Lambda_Type_Int_Sized.md` (implementation-facing numeric records; the latter still requires W10 synchronization) · `test/lambda/int53_boundary.ls`, `decimal_tiers.ls`, `sized_numeric_type_annot.ls` (the tests that gain siblings).

---

# Part 2 — Implementation status (informative, non-exhaustive)

## 8. Known non-compliance with Part 1

Deliberately informal — a snapshot of divergences found while designing v2, each with the rule it violates. Not a complete audit.

| # | Item | Status | Violates |
|---|---|---|---|
| 1 | `js_make_number` compact-int packing + `-0` special case + `JS_SYMBOL_BASE` guard (`js_runtime_value.cpp:1071`) | **Fixed in current tree:** `js_make_number` always emits the canonical `LMD_TYPE_FLOAT` representation. | §5.1 |
| 2 | Dual double lane tags `ELEM_FLOAT`/`ELEM_FLOAT64`: the dead-but-armed **truncation landmine** in `print.cpp:23` (`read_compact_elem` FLOAT64 → `i2it`), the JS seam (`Float64Array` → `ELEM_FLOAT64` vs inference → `ELEM_FLOAT`), reductions routing FLOAT64 via the generic slow path (`lambda-eval-num.cpp:1385`), `validate_pattern.cpp:111` collapsing both to one leaf | **Fixed:** `ELEM_FLOAT64` is the single canonical double lane at `0x10`; `ELEM_FLOAT` is only a source-compatibility alias, the retired `0xC0` slot has size `0`, JS `Float64Array` and `[float]` inference produce the same lane, and the duplicate slow/dead branches are removed. | §3.5 |
| 3 | `LIT_TYPE_F64` as a distinct static type object (`build_ast.cpp:3181`) | **Fixed:** `f64` input canonicalizes to `float` literals/type refs; `TYPE_F64`/`LIT_TYPE_F64` now point at `TYPE_FLOAT`. | §3.2 |
| 4 | Any `type()` path reporting `"f64"` (the superseded draft direction) | **Fixed for new values:** `type(1.0f64)` reports `float`; the legacy scalar `LMD_TYPE_FLOAT64` compatibility tag remains distinct from the ArrayNum lane merge. | §4 |
| 5 | Grammar still carries both `n` and `N` suffixes; integer-valued `n` literals still type as `decimal` | **Fixed:** grammar accepts only lowercase `n`; integer-like `n` spellings (`1n`, `1e3n`) report `integer`, fractional/negative-exponent spellings report `decimal`, and exact integer `+`/`-`/`*`/`%` preserve `integer` while `/` exits to `decimal`. | §2.2/§2.4 (W1) |
| 6 | `int64 → JS` silent `(double)` casts and raw host integer egress (e.g. `js_fs.cpp`, `js_child_process.cpp`) | **Fixed for the v2 scope:** raw Lambda `int64`/`uint64` values crossing the JS primitive boundary now report `typeof === "bigint"`, box with `BigInt.prototype`, reject `Number.*`/`JSON.stringify` Number paths, and canonicalize to mpdec-backed BigInt for arithmetic/equality/comparison. `fs.Stats`/`fs.StatFs` numeric fields use the explicit Node API split: JS `number` by default, JS `bigint` when `{ bigint: true }` is requested. Node host surfaces whose contract is `number` are now explicit too: `os.totalmem()`/`freemem()`, `os.cpus()[].speed/times.*`, fs read/write byte counts, WriteStream `bytesWritten`, and net/http socket counters/high-water marks use JS Number construction rather than raw packed host integers. Internal handles, fds, errno/status constants, ports, and small counts may remain packed because their JS contract is number-like and safe-band; lossless data fields use BigInt where the API demands it. | §5.3 |
| 7 | LambdaJS BigInt scaffolding is int64-leaning (`js_bigint_to_index`) | **Fixed for the v2 scope:** BigInt string parsing no longer has a 4 KB stack-buffer ceiling for valid large inputs, parse contexts are sized for decimal and radix-prefixed sources, decimal/scientific and non-decimal `toString(radix)` stay on arbitrary-precision mpdec digit extraction instead of truncating through `bigint_to_int64`, and wide shifts/`asIntN`/`asUintN` size their contexts past the default 2000-digit BigInt context. Regression coverage pins `(2^64-1n).toString(16)`, `BigInt.asUintN(64, -1n)`, `BigInt.asUintN(8000, -1n)`, 4097-digit decimal parsing, 4096-hex-digit parsing, and `1n << 8000n`. Buffer/DataView/BigInt typed-array 64-bit reads/writes preserve the JS BigInt face and use spec modulo/signed narrowing; BigInt typed-array `fill()` uses the same path; fs BigInt positions range-check before host narrowing; BigInt exponent sign checks use BigInt negativity instead of host-width extraction. The remaining `js_bigint_to_index` behavior is intentionally spec-facing: BigInt where JS ToIndex requires Number still throws. | §5.2/§2.2 (W2) |
| 8 | ArrayNum `==` is representation-sensitive (task_38782787) | **Fixed:** N-D shape remains structural, but mixed-lane ArrayNum equality now compares flat elements through exact numeric equality instead of lossy `double` promotion; scalar mixed numeric `==` uses the same comparator for high `i64`/`u64` boundaries. | P3 (`==` must be value-precise) |
| 9 | `is` test coverage vs the final §3.4 semantics: missing pins (trues: `1u8 is u16`, `2.5f32 is float`, `42i8 is int`, `5 is i64`, `int is integer`; falses: `1.0 is f32`, `1i16 is f16`, `5 is u8`, `[1i8] is [int]`) **and one existing golden to flip** (`42i8 is i16`: false → true, `sized_numeric_type_annot.txt`) | **Fixed:** scalar/type-left pins and array covariance pins are covered in `sized_numeric_semantics`; `sized_numeric_type_annot.txt` golden flipped. | §3.4 |
| 10 | `Lambda_Type_Numbers.md` documents the superseded lattice `is` semantics | **Fixed:** doc now says `f64` aliases to canonical `float` and `is` uses exact-embedding widening. | §3.4 |
| 11 | **A8** (`Lambda_Semantics_Formal.md`): `is`-subtyping and assignment-subtyping disagree for arrays (`var ys: any[] = xs` fails at `ensure_typed_array`), and the failure is a **log-only error with silent continuation** — `ys` left invalid, program keeps running | **Fixed:** `is`/validator checks support covariant numeric arrays, including `ArrayNum` and range occurrence checks. Typed-array assignment coercion widens by value: `ArrayNum → any[]` boxes each element, `ArrayNum → int[]/float[]/int64[]` allocates a widened typed copy, and boxed sized-scalar payloads are decoded before widening. MIR failed-coercion sites halt before storing a null typed-array pointer, and the legacy generated-C assignment expression emits the same null guard when that source path is built. The intentional remaining restriction is `var`-parameter invariance, per §3.4/ADR §9.2, not an implementation gap. | §3.4 covariant-assignment + error-enforcement discipline |
| 12 | Ordinary arithmetic normalizes every compact sized integer to `int64`, keeps `int64 + int` in `int64`, and selects `uint64 → int64/decimal` by comparing the runtime value with `INT64_MAX` (`normalize_sized`, `lambda-eval-num.cpp`) | **Open — W10:** replace the normalization shortcut with the type-directed §3.3.2 entry map. Both small and large `u64` must enter `integer`; compact sized integers must enter `int`; `i64/u64 + float` must enter `decimal`. | §3.3.2, P1 |
| 13 | The implementation-facing number/type records, historical sized-int plan, Stack API summary, public numeric guide, and LR detailed-design set contained pre-realignment promotion or scalar-storage wording | **Fixed in the 2026-07-20 documentation synchronization.** Historical threshold work remains visible but is explicitly superseded; the new implementation work is centralized in `Lambda_Impl_Numbers.md`. | §3.1, §3.3 |
| 14 | `doc/Lambda_Formal_Semantics.md` §4.3 said `integer / integer → decimal`, while §4.7 over-generalized `/` as unconditionally float-producing; §4.1 used `/` instead of `div` in the `MinInt / -1` machine-overflow example | **Fixed:** true-division results are domain-selected (`int/int → float`, `integer/integer → decimal`), and the machine-overflow example now uses `MinInt div -1`. | §2.2, §2.5, §3.3.2 |

## 9. Scheduled work & migrations (W)

- **W1** — Done for literal suffix semantics and affected goldens: integer-valued `n` literals retype to `integer`; decimal goldens shifted under §2.4 exactness; `N`-suffix scripts migrated or now fail at parse time; the `42i8 is i16` golden has flipped under the final §3.4 lattice.
- **W2** — Done for the v2 BigInt helper/conversion scope: radix string conversion, large string parsing, wide shifts, wide `asIntN`/`asUintN`, Buffer/DataView/BigInt typed-array 64-bit paths, BigInt typed-array `fill()`, fs BigInt positions, BigInt bitwise/shift MIR typing, and huge-exponent sign/identity cases are all implemented and pinned. The remaining caps (`100000` precision digits / shift bits) are explicit implementation guardrails, not host-width narrowing.
- **W3** — Done for the v2 host-egress scope: lossless raw `int64`/`uint64` egress uses BigInt, while public Node APIs whose contract is JS `number` now construct Numbers explicitly for fs stats/statfs defaults, os memory/cpu counters, fs read/write byte counts, WriteStream `bytesWritten`, and net/http socket counters/high-water marks. `{ bigint: true }` stats/statfs paths remain BigInt. Internal ids/status constants/ports stay safe-band numeric implementation details, and host option readers that consume JS numeric literals now accept integral float-backed Numbers before narrowing to native ports/fds/high-water marks.
- **W4** — Implemented and accepted by regression gate rather than a separate perf budget document: `js_make_number` now always emits canonical `LMD_TYPE_FLOAT`; self-tagging, activation number homes, and typed lanes avoid a mandatory GC allocation per Number. The current closeout gate is semantic regression safety (`make build`, focused JS/fs/os/BigInt fixtures, and `make test-lambda-baseline`/JS harness checks), with future perf tuning tracked outside this semantics proposal if benchmark deltas become material.
- **W5** — **Decided 2026-07-20:** flex-`int` result overflow continues to promote to float, not `integer`. The conversion of each in-range operand is exact, but the overflowing result is correctly rounded and may lose integer precision above the safe band. This is the accepted practical/performance tradeoff recorded in §2.1; no C3 amendment is planned.
- **W6** — Done per §5.3: `js_to_number` warns once per native caller when a regular Lambda `decimal` crosses into JS `Number` through the lossy `decimal_to_double` path, while JS BigInt (`DECIMAL_BIGINT`) still throws before any warning. The warning site table is mutex-protected, resets with the JS batch lifecycle, and the LambdaJS audit found no direct decimal-egress bypass outside `js_to_number`.
- **W7** — Done for the original v2 scope: parser literals, AST range checks, conversions, `is`/`type()`, public docs, and runtime number docs were synchronized with the earlier §3.3–§3.4 and BigInt/egress decisions. The 2026-07-20 arithmetic revision intentionally reopens only the promotion subset tracked by W10.
- **W8** — Done: the §3.5 ArrayNum lane merge now uses `ELEM_FLOAT64` as the single double tag at `0x10`, keeps `ELEM_FLOAT` only as a compatibility alias, retires the duplicate `0xC0` slot, closes the JS `Float64Array` seam, and removes doubled branches.
- **W9** — **Done:** the suffix split migration (A.5) added `m` for all decimal spellings, restricted `n` to integer-valued spellings, made the suffix determine the type unconditionally, migrated fractional-`n` scripts/goldens, and synchronized the public docs.
- **W10** — **Open implementation; documentation synchronized 2026-07-20:** remove all magnitude-directed `u64` promotion; keep sized-integer × sized-integer `+ - * div %`, bitwise operations, and shifts in the Lambda-selected machine lane with Go overflow behavior; map compact sized integers to `int` and `i64/u64` to `integer` before any sized/non-sized operation or true `/`; route 64-bit integer + float through decimal; update evaluator, AST, MIR, and legacy C static/result inference; add magnitude-independence, boundary, overflow, division, and result-type tests. The executable work packages and gates are in `Lambda_Impl_Numbers.md`.

### 9.1 Verification snapshot (2026-07-10)

- `make build` — pass.
- Focused fixtures — pass: `test/js/v9_number_json.js`, `test/js/fs_basic.js`, `proc_sized_array_widening.ls`, `type_occurrence.ls`.
- Focused gtests — pass: `JavaScriptTests/JsFileTest.Run/v9_number_json`, `JavaScriptTests/JsFileTest.Run/fs_basic`.
- Focused Node official fs/http set — pass: `test-file-write-stream.js`, `test-http-max-sockets.js`, `test-http-client-race.js`, `test-http-default-encoding.js`, `test-http-write-callbacks.js`.
- `make test-lambda-baseline` — pass: 3282/3282.
- Full `test_node_gtest --baseline-only` — number-model/fs/http regressions from this slice are cleared; remaining failures are the pre-existing non-number pockets observed before closeout (`test-async-hooks-recursive-stack-runInAsyncScope.js`, `test-async-wrap-pop-id-during-load.js`, `test-global-console-exists.js`, `test-util-getcallsites-preparestacktrace.js`).

---

# Appendix A — Lambda `decimal` × TC39 Decimal (comparison, informative)

*Snapshot of the proposal as of 2026-07-11 ([tc39/proposal-decimal](https://github.com/tc39/proposal-decimal)); revisit if it advances past Stage 1.*

## A.1 The proposal in brief

- **Stage 1**, with a draft spec; Stage 2 advancement was attempted (April/June 2024) and has not landed.
- **Representation: strict IEEE 754-2019 Decimal128** — 34 significant digits, exponent ±6143, every operation rounds into that format. The champions **explicitly rejected unlimited-precision BigDecimal** designs.
- **API: a `Decimal` class with method arithmetic** (`add`/`subtract`/`multiply`/`divide`/`remainder`, comparison methods, `toString`/`toFixed`/`toPrecision`/`toExponential`/`toLocaleString`, mantissa/exponent accessors). The arithmetic **operators deliberately throw** — no operator overloading, no implicit mixing with Number or BigInt.
- **No literal syntax in v1.** The suffix contemplated for a future version is **`m`** (`123.456m` — the C# `decimal` suffix), never `n`: in JS, `n` is BigInt, which maps to Lambda's `integer` (§2.2), not to `decimal`.

## A.2 Where the designs agree — by construction

The overlap is not coincidental; §2.4 names IEEE/TC39 as the "parent" of the division rule.

- **The everyday-money envelope is identical**: Lambda's division precision floor of 34 digits was chosen *so that* money-scale division behaves exactly like decimal128 — and therefore exactly like JS's future Decimal.
- **Same special values**: decimal NaN/±Inf per IEEE 754-2008 (both sides; Lambda needs them for the `float → decimal` embedding edge).
- **Same arithmetic engine family**: libmpdec implements the IEEE decimal arithmetic the proposal specifies; Lambda's fixed context (`g_fixed_ctx`, `lambda-decimal.cpp:21`) *is* a decimal128 context.
- **Same value-type stance**: immutable values, equality by numeric value, no exact-rational detour (both reject Raku/Racket-style rationals).

## A.3 Where they differ — deliberately

| Dimension | Lambda `decimal` (§2.4) | TC39 Decimal | Consequence |
|---|---|---|---|
| Precision model | Tiered: decimal128 layout when it fits, **unbounded above**; tiers invisible to `type()`/`==` (P2/P3) | Strict decimal128, always | Lambda's value set is a strict superset |
| `+` `−` `×` | **Exact** (BigDecimal-family) | Rounded to 34 digits | Exact chains diverge from decimal128 — the accepted cost on record in §2.4; `quantize`/`round(x, places)` at interop boundaries |
| `÷` | Rounds by the §2.4 precision rule (floor 34) | Rounds to 34 | Identical in the everyday envelope |
| Operators | Native `+ − × ÷`, total order, `sort`/`order by` participation | Method calls only; operators **throw** | Ergonomics vs. host-language constraint (JS can't overload) |
| Mixing | `int ⊂ integer ⊂ decimal`, `float ⊂ decimal` — the tower (§2.5) is the exact meeting type | Throws on mixing with Number/BigInt | Same reasoning as §6's BigInt note: JS throws *for lack of* a meeting type; Lambda has one |
| Literal | `m` suffix, all spellings (`100m`, `1.5m` — A.5) | None in v1; future candidate is `m`, not `n` | Aligned by A.5: Lambda's `m` = TC39's future `m` |
| Precision control | On operations: `decimal(x, prec:, rounding:)`, `quantize` | Implicit in the format | Same capability, different home |

## A.4 Interop when JS Decimal ships (forward note, not scheduled work)

Today `decimal` egresses to JS `Number` through the lossy warn-once `decimal_to_double` path (W6). If Decimal128 lands in engines, the type-directed egress table (§5.3) gains a natural row: `decimal` values within the decimal128 envelope map losslessly to `Decimal`; above-tier values need `quantize` or a throw — the same shape as the BigInt egress policy. Ingress `Decimal → decimal` is always lossless (decimal128 ⊂ Lambda's tier 1). Nothing to build until the proposal reaches engines.

## A.5 Decimal literal suffix: `n`, `m`, or both — **DECIDED: split (`n` = `integer`, `m` = `decimal`)**

*Status: **decided 2026-07-11** (user-confirmed, option B). Amends §2.2/§2.4; supersedes the ON2 lexical rule; migration = W9. Recorded here because A.1 changed the landscape the lexical rule was designed in.*

The prior rule (§2.2 as of W1) made `n` serve two types by spelling: `1n`/`1e3n` → `integer`, `1.5n`/`1.5e-2n` → `decimal`. That was the right repair of the old `n`/`N` precision split — at that time `n` had to carry decimal, and the lexical rule was the least-surprising way to carve `integer` out of it. But with `integer` established as a first-class type, three options were weighed:

- **A — keep `n` double-duty (the then-status quo).** No migration. Rejected: the suffix alone doesn't name the type — `1n` and `1.0n` differ in type *by their digits*, a genuine reviewer trap (the type of a literal should not require inspecting its mantissa); and JS alignment is only half-true (`1n` matches BigInt, but `1.5n` is a JS SyntaxError, so "Lambda `n` = JS `n`" cannot be stated cleanly).
- **B — split: `n` = `integer` always, `m` = `decimal` always.** ✅ **Adopted**, on four arguments:
  1. **The suffix names the type.** One-suffix-one-type is the simpler contract than any lexical rule — no digit inspection, no reviewer trap, `grep`-able. This is the same P1 instinct (predictable, never value/spelling-dependent) applied to literals.
  2. **Full JS parity in both directions.** `n` ⇔ BigInt becomes exact: every valid Lambda `n` literal is a valid JS BigInt literal and vice versa. And `m` is TC39's contemplated future decimal literal (A.1), so if/when JS grows decimal literals, Lambda source is already spelling-compatible with *both* JS numeric literal families.
  3. **`100m` — integer-valued decimal literals.** The money spelling the lexical rule structurally could not express (`100.0n` was the workaround, and it reads as a float-ism). With `m`, the everyday-money type gets the everyday-money literal.
  4. **Precedent.** `m` is C#'s `decimal` suffix — the one mainstream language with a first-class decimal literal — and TC39's candidate; there is no competing convention for `m`, and no language uses `n` for decimal.
  - Cost accepted: a small W-style migration (grammar + fractional-`n` goldens); the `N` retirement (W1) already exercised exactly this path.
- **C — both `n` and `m` accepted for decimal.** Rejected on sight: two spellings for one type is review noise and settles nothing — it keeps A's trap and adds a synonym.

Consequences: fractional/negative-exponent `n` spellings (`1.5n`, `1e-3n`) are **compile errors** whose message points at `m`; `n` requires an integer-valued spelling (no `.`, no negative exponent — the old lexical test survives as the *validity* rule for `n` rather than a *typing* rule); `m` accepts every numeric spelling. The lexical rule is thereby superseded, not deleted — it demoted from "picks the type" to "guards the suffix."
