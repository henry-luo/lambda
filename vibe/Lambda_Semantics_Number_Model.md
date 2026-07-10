# Lambda Number Model v2

**Status:** adopted design (2026-07-08) — Part 1 is the normative model; Part 2 is an informative snapshot of implementation gaps
**Changelog:** v2 born from the JS-interop question "any issue mapping JS number to `TYPE_FLOAT`?"; N1–N9 confirmed 2026-07-08; sized-scalar arithmetic (Go-style) added same day; division rule and literal-typing rule resolved same day; **`is`/`type()` semantics finalized same day after three iterations** (flat → hybrid → final): **uniform exact-embedding subtyping over all scalar numeric types; containers covariant where values copy, invariant at `var`-param borrows** (a brief blanket-invariance detour was reverted to the pre-existing normative ruling, ADR §9.2/C12) — supersedes both the draft lattice paragraph and the interim flat rule (history preserved in §6); doc restructured into spec form same day.
**Normative home:** this doc designs in the area owned by `../doc/Lambda_Formal_Semantics.md` **§4 Numerics** (§4.1 two-tier integers [C3] · §4.3 promotion lattice [C3] · §4.4 decimal tiers [C13] · §4.5 float↔decimal [C8.5a] · §4.7 division) and touches **§5 Equality** [C8], **§6 Ordering** [C11], **§9.2 Covariance** [C12]. Where v2 amends a section, the amendment flows back into the ADR when implemented; until then the ADR governs the language and this doc records the agreed direction.
**Grounding:** `js_make_number` (`lambda/js/js_runtime_value.cpp:1071`), the compact-int band (`lambda.h:889`, `INT56_MAX` = ±(2⁵³−1), deliberately the JS safe-integer band), the two libmpdec contexts (`lambda-decimal.cpp:21` — `g_fixed_ctx` decimal128 / `g_unlimited_ctx` arbitrary), the sized-scalar carriers (`lambda.h:126` NUM_SIZED, 32-bit payload), the ArrayNum lane enum (`lambda.h:149`), the Go spec (`go.dev/ref/spec#Numeric_types`, `#Integer_overflow`, `#Constants`, `#Conversions`).

---

# Part 1 — The Model

## 1. Principles

- **P1 — Type-directed FFI, never value-directed.** A Lambda value's foreign type (JS `number` vs `BigInt`, …) is a function of its Lambda *type*, never of its magnitude. (The rejected alternative is the snowflake-ID bug — §6.)
- **P2 — Tier-hiding.** Hide representation tiers when the external contract is tier-independent (decimal's tiers hide; `integer` stays a distinct type *because its FFI face differs*).
- **P3 — Two orthogonal axes, each uniform.** `==` is **equality**: value-precise across representations for values (ADR §5.2 Numbers [C8]: `1 == 1.0`, `2.5f32 == 2.5`), exact equality for types. `is` is **subsumption**: `x is T` ⟺ `type(x) ⊑ T`, and with a type on the left, `T1 is T2` ⟺ `T1 ⊑ T2` — where ⊑ is the exact-embedding lattice over scalar numeric types, **covariance over containers where values copy / invariance at `var`-param borrows** (§3.4, per the normative §9.2/C12), and membership for unions. No operator mixes the axes.
- **P4 — Exact embeddings up the tower; division exits honestly.** `int ⊂ integer ⊂ decimal`; `float ⊂ decimal` (every binary64 has an exact finite decimal expansion); `float ∥ integer`. Division always produces a type that can hold the answer honestly (`integer ÷ integer → decimal`; decimal `÷` rounds by the §2.4 rule) — no silent truncation, no silent wrap in the value domain. *(Extends the ADR's §4.3 promotion lattice [C3] and §4.7 division rules with the `integer` column.)*

## 2. Value types

User-visible numeric value types: **`int` · `integer` · `float` · `decimal`**, grouped by the union **`number`**.

### 2.1 `int`

The everyday integer, per the ADR's **two-tier model** (`Lambda_Formal_Semantics.md` §4.1–4.2 [C3] — compact band + int64 tier, strict literals). The compact band is **±(2⁵³−1) by design — exactly the JS safe-integer band**, which makes `int → number` egress exact always (§5.4). C3 semantics unchanged by v2. (Whether `int64`-tier overflow should someday promote to `integer`, Python-style, is deferred — W5; it would be a C3 amendment.)

### 2.2 `integer` — arbitrary precision, the BigInt counterpart

- **Backing: libmpdec's unlimited-precision context** (`g_unlimited_ctx`) — *not* decimal128: BigInt is unbounded, and 2¹²⁸ is 39 digits, already past 34. With unlimited backing, `integer ⇔ BigInt` is faithful in both directions.
- **Literal:** the `n` suffix, typed by the **lexical rule**: a `n` literal is `integer` iff it has no `.` and no negative exponent (`1n`, `1e3n` → integer; `1.0n`, `1.5e-2n` → decimal). Predictable from source text alone.
- **Arithmetic:** `+`, `-`, `×` stay in `integer`; **`integer ÷ integer → decimal`** (P4 — no JS-BigInt-style floor). Mixing: `integer op float → decimal`, `integer op decimal → decimal` (§2.4 tower table). JS's throw-on-mix is rejected — JS throws because it lacks an exact meeting type; Lambda has one.
- **Why a distinct type rather than an `int` tier:** P2 — its FFI face (`BigInt`) differs from `int`'s (`number`); folding it into `int` would make egress value-dependent, violating P1.

### 2.3 `float` — binary64, the JS-number twin

- IEEE-754 binary64; **`f64` is a spelling alias for the same type** (§3.2), not a second type.
- **Considered-and-kept** (the "is float redundant given decimal?" question): (a) the JS bijection requires a native binary64 type — it is the identity mapping in both directions; (b) it is the hardware type — SIMD lanes, ArrayNum, K19 pairwise reductions, Stage-A parallelism, and all of Radiant (layout is float by rule); (c) the world's data is binary64 (JSON-in-practice, Float64Array, Arrow, C ABI). `float` and `decimal` serve disjoint domains: measurements vs exact human quantities.
- **Specials:** −0, NaN, ±Infinity flow untouched; uniform boxing preserves −0 naturally; NaN payloads pass through unmodified (observable only via typed-array bit views; spec latitude).

### 2.4 `decimal` — one type, invisible tiers, BigDecimal-family arithmetic

*(Amends the ADR's §4.4 decimal tiers [C13] — the tiering survives, the source-level `n`/`N` split does not; and §4.5 float↔decimal [C8.5a] governs the embedding edge this section builds on. The §4.7 division rules gain the ON1 precision formula.)*

- **One type, one literal suffix.** Storage tiers invisibly: decimal128 layout for values that fit, unbounded above. The source-level `n`/`N` split is removed (`N` retires from the grammar). **Literal digits are always preserved exactly regardless of tier** — the `N` suffix only ever controlled arithmetic-context participation, and context control belongs on operations: `decimal(x, prec:, rounding:)` for the advanced case, **`quantize` / `round(x, places)` as the everyday money tool**. `type()` reports `decimal` at every tier; `==` holds across tiers (P3).
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

All meets are **exact embeddings** (P4); nothing silently loses precision on the way in — precision decisions happen only at `÷` (the §2.4 rule) and at explicit `quantize`/conversion. *(This table is the v2 extension of the ADR §4.3 mixed-operation promotion lattice [C3]; the `int`/`float` cells defer to it unchanged.)*

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
- **Signed:** overflow produces the deterministic two's-complement result: `127i8 + 1i8 → -128i8`; `-128i8 / -1i8 → -128i8`.
- **Division/remainder by zero:** `error()` — never wraps, never panics, never yields a lane value.
- **Shifts:** negative counts → `error()`; non-negative counts compute mathematically, then truncate to the lane width: `1u8 << 8 → 0u8`.
- **Constants:** exact constant evaluation does not wrap; a suffixed literal/constant expression must be representable in its declared type — `128i8`, `256u8` are compile-time errors.
- **Conversions:** constant vs non-constant deliberately split (Go's rule): `u8(-1)` on a constant is a compile-time error; on a non-constant, sign/zero-extend then truncate with no overflow signal (`let x = -1; u8(x) → 255u8`). Float-sized conversions round to the destination IEEE format.
- **Sized floats:** `f32` follows Go `float32` (working precision allowed; conversion/assignment rounds to binary32); `f16` same principle with binary16 (primarily storage/interop).
- **Mixing:** same sized type → sized result (Go overflow rule applies). Mixed sized floats where one exactly contains the other → the wider (`f16 + f32 → f32`). Otherwise operands **promote out of the sized lane** into the value tower (`int`/`float`/`integer`/`decimal`) and §2.5 applies. Promotion is by the type lattice, never by inspecting the value — there is no value-dependent promotion to dodge overflow. *Promotion is conversion (it produces a new value of the wider type); it is not `is`-subtyping (§3.4).*

### 3.4 `is` and `type()`: subsumption by exact embedding; containers invariant

**The scalar rule — one principle, every edge derived:**

> **T1 ⊑ T2 ⟺ every T1 value embeds exactly (value-preserving) into T2's value domain.**
> `x is T` ⟺ `type(x) ⊑ T` · with a type on the left, **`T1 is T2` ⟺ T1 ⊑ T2** (`int is integer` → true) · aliases resolve first (§3.2) · unions check membership · `type(x) == T` remains the exact check.

The derived lattice over all scalar numeric types — sized and semantic follow the *same* rule:

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

**Tests to pin:** trues — `1u8 is u16`, `42i8 is i16` (golden migration, W1), `2.5f32 is float`, `42i8 is int`, `5 is i64`, `1.0 is decimal`, `int is integer`, `[1i8] is [int]`; falses — `1.0 is f32`, `1i16 is f16`, `5 is u8`, `1i64 is float`, `[1.5] is [i8]`; compile-error negative — `var i8[]` argument to a `var int[]` parameter (`test/lambda/negative/semantic/`).

### 3.5 Lane tags: one tag per representation

- The ArrayNum lane enum keeps **exactly one tag per distinct representation**. The current `ELEM_FLOAT` / `ELEM_FLOAT64` pair (identical 8-byte double storage, by its own comment) **merges into a single tag** — C spelling `ELEM_FLOAT64` (readability: lane tags are about size) — produced uniformly by inference, `[float]`/`[f64]` annotations, and JS `Float64Array`. `[f64]` and `[float]` are one array type.
- **Explicit non-case:** `ELEM_INT` vs `ELEM_INT64` is *not* this merge — they differ semantically (compact-band domain + packed read-out vs full-range + boxed read-out). Any change there is a separate analysis.

## 4. `type()` formal vocabulary (numeric)

The set of names `type()` can return **is** the set of runtime numeric types — normative, one canonical spelling each:

| Canonical `type()` output | Accepted input aliases | Kind |
|---|---|---|
| `int` | — | value type (tiered internally) |
| `int64` | `i64` | native 64-bit signed (today's tier/type; W5 may revisit) |
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

**Perf plan, eyes open:** every boxed JS number heap-allocates (Lambda tags pointers; no NaN-boxing). Mitigations: MIR inference keeps hot-path numbers as raw unboxed doubles in registers (and *simplifies* — one JS numeric type to infer); number-dense JS arrays store as float64 typed lanes; nursery bump allocation. **Gate: node-baseline runtime + benchmark suite before/after (W4)** — the regression budget is a conscious decision. Doubles do exact integer arithmetic to 2⁵³, so integer-heavy JS stays correct by construction — the JS spec's own model.

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
- **The `n`/`N` dual suffixes** — precision-context control on literals; moved to operations.

**Precedent index:** Go spec (sized arithmetic, constants, conversions — §3.3's source) · V8's Smi (what §5.1 deliberately walks away from, trading representation cleverness for one clean rule) · C#/SQL name-aliasing (§3.2) · Java BigDecimal vs Python decimal vs rationals (§2.4's family choice) · IEEE 754-2008 / TC39 Decimal + PostgreSQL/SQL Server/Oracle (the division rule's two parents) · Python's unbounded ints (the accepted exact-arithmetic performance trade) · NumPy pairwise summation (the K19 sibling policy for reductions).

## 7. Cross-references

**`../doc/Lambda_Formal_Semantics.md` — the ADR this doc amends**: §4 Numerics [C3 integers/lattice, C13 decimal tiers, C8.5a float↔decimal, §4.6 float printing, §4.7 division] · §5 Equality [C8] · §6 Ordering [C11] · §9.2 Covariance [C12] — per its own section↔ledger mapping table · `Lambda_Semantics_Formal.md`/`Formal2.md` (the decision records behind those entries; A8 = the variance implementation gap, Part 2 item 11) · `Lambda_Semantics_Features.md` §1.8 **G7** (this is the numeric row of the per-guest projection table; G3 error-mapping is the sibling) · `Lambda_Design_Concurrency.md` K16/§4.6 (the membrane §5 simplifies) · K19 (deterministic reductions; the same order-as-spec philosophy) · K28 (the best-effort banner W2 rides) · `Lambda_Type_Numbers.md` (sized-scalar implementation doc — **must align to §3.4**, see Part 2) · `test/lambda/int53_boundary.ls`, `decimal_tiers.ls`, `sized_numeric_type_annot.ls` (the tests that gain siblings).

---

# Part 2 — Implementation status (informative, non-exhaustive)

## 8. Known non-compliance with Part 1

Deliberately informal — a snapshot of divergences found while designing v2, each with the rule it violates. Not a complete audit.

| # | Item | Status | Violates |
|---|---|---|---|
| 1 | `js_make_number` compact-int packing + `-0` special case + `JS_SYMBOL_BASE` guard (`js_runtime_value.cpp:1071`) | **Fixed in current tree:** `js_make_number` always boxes `LMD_TYPE_FLOAT`. | §5.1 |
| 2 | Dual double lane tags `ELEM_FLOAT`/`ELEM_FLOAT64`: the dead-but-armed **truncation landmine** in `print.cpp:23` (`read_compact_elem` FLOAT64 → `i2it`), the JS seam (`Float64Array` → `ELEM_FLOAT64` vs inference → `ELEM_FLOAT`), reductions routing FLOAT64 via the generic slow path (`lambda-eval-num.cpp:1385`), `validate_pattern.cpp:111` collapsing both to one leaf | **Fixed:** `ELEM_FLOAT64` is the single canonical double lane at `0x10`; `ELEM_FLOAT` is only a source-compatibility alias, the retired `0xC0` slot has size `0`, JS `Float64Array` and `[float]` inference produce the same lane, and the duplicate slow/dead branches are removed. | §3.5 |
| 3 | `LIT_TYPE_F64` as a distinct static type object (`build_ast.cpp:3181`) | **Fixed:** `f64` input canonicalizes to `float` literals/type refs; `TYPE_F64`/`LIT_TYPE_F64` now point at `TYPE_FLOAT`. | §3.2 |
| 4 | Any `type()` path reporting `"f64"` (the superseded draft direction) | **Fixed for new values:** `type(1.0f64)` reports `float`; the legacy scalar `LMD_TYPE_FLOAT64` compatibility tag remains distinct from the ArrayNum lane merge. | §4 |
| 5 | Grammar still carries both `n` and `N` suffixes; integer-valued `n` literals still type as `decimal` | **Fixed:** grammar accepts only lowercase `n`; integer-like `n` spellings (`1n`, `1e3n`) report `integer`, fractional/negative-exponent spellings report `decimal`, and exact integer `+`/`-`/`*`/`%` preserve `integer` while `/` exits to `decimal`. | §2.2/§2.4 (W1) |
| 6 | `int64 → JS` silent `(double)` casts and raw host integer egress (e.g. `js_fs.cpp`, `js_child_process.cpp`) | **Partially fixed:** raw Lambda `int64`/`uint64` values crossing the JS primitive boundary now report `typeof === "bigint"`, box with `BigInt.prototype`, reject `Number.*`/`JSON.stringify` Number paths, and canonicalize to mpdec-backed BigInt for arithmetic/equality/comparison. `fs.Stats`/`fs.StatFs` numeric fields now use an explicit Node API face: JS `number` by default, JS `bigint` when `{ bigint: true }` is requested, including sync/callback/promise Stats entry points. Remaining work is the broader API-by-API host audit for legacy call sites that still cast native 64-bit host fields or return raw packed integers instead of using an explicit JS face. | §5.3 (int64 → BigInt always) |
| 7 | LambdaJS BigInt scaffolding is int64-leaning (`js_bigint_to_index`) | **Partially fixed:** BigInt non-decimal `toString(radix)` no longer truncates through `bigint_to_int64`; arbitrary-precision mpdec division now drives radix conversion, covering cases such as `(2^64-1n).toString(16)` and `BigInt.asUintN(64, -1n).toString(16)`. Buffer `readBigInt64*`/`readBigUInt64*` now return JS BigInt instead of packed number values, and the matching writes consume BigInt through 64-bit modulo/signed narrowing; DataView and BigInt typed-array 64-bit edge reads/writes are pinned by the same fixture. MIR type inference now keeps BigInt bitwise/shift results boxed instead of mislabeling them as JS Numbers, preserving cases such as `-(1n << 63n)` and the signed 64-bit minimum boundary. Remaining work is the broader `js_bigint_to_index`/BigInt helper audit under W2. | §5.2/§2.2 (W2) |
| 8 | ArrayNum `==` is representation-sensitive (task_38782787) | **Fixed:** N-D shape remains structural, but mixed-lane ArrayNum equality now compares flat elements through exact numeric equality instead of lossy `double` promotion; scalar mixed numeric `==` uses the same comparator for high `i64`/`u64` boundaries. | P3 (`==` must be value-precise) |
| 9 | `is` test coverage vs the final §3.4 semantics: missing pins (trues: `1u8 is u16`, `2.5f32 is float`, `42i8 is int`, `5 is i64`, `int is integer`; falses: `1.0 is f32`, `1i16 is f16`, `5 is u8`, `[1i8] is [int]`) **and one existing golden to flip** (`42i8 is i16`: false → true, `sized_numeric_type_annot.txt`) | **Fixed:** scalar/type-left pins and array covariance pins are covered in `sized_numeric_semantics`; `sized_numeric_type_annot.txt` golden flipped. | §3.4 |
| 10 | `Lambda_Type_Numbers.md` documents the superseded lattice `is` semantics | **Fixed:** doc now says `f64` aliases to canonical `float` and `is` uses exact-embedding widening. | §3.4 |
| 11 | **A8** (`Lambda_Semantics_Formal.md`): `is`-subtyping and assignment-subtyping disagree for arrays (`var ys: any[] = xs` fails at `ensure_typed_array`), and the failure is a **log-only error with silent continuation** — `ys` left invalid, program keeps running | **Partially fixed:** `is`/validator checks now support covariant numeric arrays, including `ArrayNum` and range occurrence checks. Typed-array assignment coercion now decodes packed sized-scalar payloads when widening boxed `i8[]`-style values through `int[]` and then `any[]`. Broader COW widening and full error-enforcement cleanup remain open. | §3.4 covariant-assignment (per §9.2/C12: assignment copies, must widen at COW time) + the error-enforcement discipline |

## 9. Scheduled work & migrations (W)

- **W1** — Done for literal suffix semantics and affected goldens: integer-valued `n` literals retype to `integer`; decimal goldens shifted under §2.4 exactness; `N`-suffix scripts migrated or now fail at parse time; the `42i8 is i16` golden has flipped under the final §3.4 lattice.
- **W2** — Partially fixed: BigInt radix string conversion now stays on the mpdec-unlimited backing instead of truncating via `int64`, regression coverage pins the 64-bit `asUintN` edge, signed-minimum `asIntN`/bitwise-shift edge, `ToIndex` rejection for BigInt `bits`, mixed Number/BigInt throw paths, and BigInt Atomics lane conversion. Buffer/DataView/BigInt typed-array read/write APIs now preserve the JS BigInt face at the 64-bit boundary. Wider `asIntN`/`asUintN` probes above 64 bits pass in the current tree. Remaining BigInt semantic coverage is the broader helper/conversion audit, riding the node baseline under the K28-style best-effort banner.
- **W3** — Partially fixed: the shared JS egress helper now routes raw `uint64` values to BigInt via decimal-string construction, so values above `INT64_MAX` have a lossless unsigned path. `fs.Stats`/`fs.StatFs` now make their Node API split explicit (`number` by default, `bigint` for `{ bigint: true }`) instead of letting packed host integers choose by magnitude. Remaining work is to route the rest of the FFI/lane boundary producers through explicit number-vs-BigInt helpers instead of ad hoc numeric casts.
- **W4** — The §5.1 performance budget: define the acceptable node-baseline/benchmark regression *before* removing compact-int packing; typed-lane storage for number-dense JS arrays is the committed mitigation if exceeded.
- **W5** — Deferred design question (not v2): `int64`-tier overflow promoting to `integer` (Python-style full tower). Needs its own C3 amendment; §5.3 egress is unaffected either way.
- **W6** — The warn-once-per-call-site mechanism for decimal egress (site keying, reset policy); error-by-default stays on record if warnings prove ignorable.
- **W7** — Partially fixed: parser literals, AST range checks, MIR/runtime arithmetic, conversions, `is`/`type()`, and the number-specific doc now align with §3.3–§3.4; `doc/Lambda_Type.md` has also been migrated from exact-only sized `is` wording to exact-embedding widening. Remaining work is a final cross-document sweep for stale numeric examples outside the number docs.
- **W8** — Done: the §3.5 ArrayNum lane merge now uses `ELEM_FLOAT64` as the single double tag at `0x10`, keeps `ELEM_FLOAT` only as a compatibility alias, retires the duplicate `0xC0` slot, closes the JS `Float64Array` seam, and removes doubled branches.
