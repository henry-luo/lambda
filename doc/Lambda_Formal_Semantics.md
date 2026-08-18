# Lambda Formal Semantics — Specification

**Spec version:** 3.1.0 (2026-08-18)

**Status:** normative — the single source of truth for Lambda language semantics.
This document records what Lambda's semantics **is by decision**, not what any
build implements. Where any other document — including the `vibe/` design
records — or the implementation disagrees, this specification wins; the design
records govern the history and preserve the full deliberations.

**Ruling IDs.** Every ruling carries a section-path ID: `S4.6.2` is the second
ruling of §4.6. A revised ruling keeps its ID with a version suffix
(`S4.6.2v2`), replacing its predecessor in place; superseded text is not
carried. The spec itself uses semantic versioning: MAJOR — an existing ruling
changed meaning; MINOR — rulings added; PATCH — editorial.

**Implementation marks.** A ruling marked `*` is not, or only partially,
implemented; Appendix A carries the footnote. Unmarked rulings are believed
implemented; any conformance gap is a bug, never a semantics change.
Appendix B lists open design issues.

**Basis:** decision records C1–C17
([`Lambda_Semantics_Formal.md`](../vibe/Lambda_Semantics_Formal.md),
[`Lambda_Semantics_Formal2.md`](../vibe/Lambda_Semantics_Formal2.md));
int v5 ([`Lambda_Semantics_Int_Type.md`](../vibe/Lambda_Semantics_Int_Type.md));
the number model
([`Lambda_Semantics_Number_Model.md`](../vibe/Lambda_Semantics_Number_Model.md));
TE-1–TE-18
([`Lambda_Design_Type_Enforcement.md`](../vibe/Lambda_Design_Type_Enforcement.md));
ER-D1–D13 ([`Lambda_Design_Exec_Recovery.md`](../vibe/Lambda_Design_Exec_Recovery.md));
K11–K32 ([`Lambda_Design_Concurrency.md`](../vibe/Lambda_Design_Concurrency.md));
PD9–PD16 / FC1–FC11
([`Lambda_Design_Data_Processing.md`](../vibe/Lambda_Design_Data_Processing.md),
[`Lambda_Expr_For_Clauses2.md`](../vibe/Lambda_Expr_For_Clauses2.md));
RF1–RF6 ([`Lambda_Design_Sys_Func.md`](../vibe/Lambda_Design_Sys_Func.md));
R1–R5 and the effect doctrine
([`Lambda_Semantics_Features.md`](../vibe/Lambda_Semantics_Features.md));
C4/CW ([`Lambda_Design_Runtime_COW.md`](../vibe/Lambda_Design_Runtime_COW.md)).
Appendix C maps sections to records.

---

## S1 Core Principles

Every rule below is an instance of these axioms; new design questions are
answered from them first.

- **S1.1 — Emptiness is not nothingness.** An empty value is a value (`""` is a
  string; an empty box is a box). General absence is `null`. [C1, C2]
- **S1.2 — Values versus containers.** Scalars are identified by content alone;
  containers are *things* that hold content. [C2]
- **S1.3 — Exactness lives in the exact tier.** `integer` and `decimal`
  arithmetic is exact; parsed data always lands in an exact home (§S4.3).
  `int` is exact over its whole domain (§S4.1); `float` is the explicitly
  inexact type. [C3, C13, C16, v5]
- **S1.4 — Mutation is visible or it does not exist.** Values never alias;
  mutability is a property of bindings, marked by `var`. [C4]
- **S1.5 — Set-oriented: absence flows, failure is deliberate.** *Total reads,
  checked writes, deliberate failures.* Absence lets set processing continue;
  every error value is deliberate. [C5, C14]
- **S1.6 — Representation is invisible.** Boxing, COW sharing, unboxed arrays,
  JIT inference, decimal width, thread count: implementation strategies, never
  observable in results. Every violation is a bug by definition. [B7, C4, C8, K13]
- **S1.7 — One symbol, one concept.** Each operator spelling has one meaning
  everywhere; container operators mean one thing across the map/list duality,
  because an element is both. [C6, C5.3a]
- **S1.8 — Strings are never code.** Programs enter as source files or
  constructed AST values; no API accepts a runtime string for execution. [C9]
- **S1.9 — Equality is the root relation.** `==` is total (two designed poison
  carve-outs); the sort order totally refines it; hashing respects it; two
  numbers are equal iff they print the same. [C8, C11]
- **S1.10 — The effect doctrine.** *Color what changes the caller's contract;
  infer what doesn't; put must-respond channels in types.* Purity is the
  declared `fn`/`pn` bit; errors and resource ownership live in return types;
  may-suspend is inferred and invisible. [Features §3.6]

### The invariant ledger (SI)

Standing invariants distilled from the rulings — the checkable face of the
principles. Any observed violation is a bug, never a semantics change; the
representation-facing subset is verified by the differential and forced-GC
harnesses.

- **SI1 — Boxing invisibility.** Representation choices — tagging, unboxed
  arrays, decimal width, lane selection — never affect results. [S1.6]
- **SI2 — COW unobservability.** Sharing until first mutation is
  undetectable; `let`-finality holds absolutely; reference identity is not
  observable (no `===`, ever). [S9.1, S5.1.4]
- **SI3v2 — Inference evaluation-invariance** (the gradual guarantee,
  revised 2026-08-18). **A script with no type error evaluates identically
  regardless of inference**: erasing all inferred types and running boxed
  produces the same results. Inference IS statically observable — improved
  precision may turn a previously-compiling script into a static compile
  error, and Lambda rejects **straightaway** (no warn-first transition).
  The sanctioned observable change is exactly this: a script that carries
  a type error moves from "runtime soft error, possibly partial results"
  to "static error, no result" — a rejected script has no evaluation to
  preserve. Type-error strictness is a per-surface policy: **Lambda is
  strict by default** (static type errors reject) with an explicit
  per-invocation opt-out (`lambda --static-warning`) that reports the same
  findings as warnings and still runs the script; **LambdaJS is lenient**
  (static type findings are warnings only and the script still runs — a
  dynamic result containing errors is preferred over no result in
  browser-style use). In relaxed mode a diagnosed contract never drives
  representation: an unresolved or rejected annotation falls back to the
  inferred value type, so the binding never lies about its bits (SI14).
  [S11.4]
- **SI4 — Equality laws.** `==` is total (cross-family is `false`, never an
  error) and an equivalence modulo exactly two poison carve-outs (`nan`,
  `error`); numbers tie across all representations;
  `a == b ⟹ hash(a) == hash(b)`. [S5]
- **SI5 — Order refinement.** The total order totally refines `==` — equal
  values always tie; sort is stable; `desc` is exact reversal. [S6.2]
- **SI6 — Printer injectivity.** Distinct doubles print distinctly;
  print→parse round-trips exactly; two numbers are equal iff they print the
  same (modulo the nan carve-out). [S4.7, S4.8]
- **SI7 — Int totality.** `int` is closed and total under `+ - * div % neg
  abs`, bitwise, shifts, and `**`: the result is always an `int` — finite,
  saturated `±inf`, or `nan` — and integer math never returns `error()`.
  [S4.1, S4.5]
- **SI8 — Poison symmetry.** Every unbounded numeric domain is closed with
  its own poison; nans never equal anything, themselves included;
  same-signed infinities are one value across domains; poison is unequal,
  not untypeable — it classifies normally. [S4.2]
- **SI9 — Truthiness tag-decidability.** Truthiness is decidable from the
  type tag alone; the falsy set is exactly `{null, false, error, ""}`. [S3]
- **SI10 — Literal strictness, data totality.** An unsuffixed int-form
  literal outside the band is a compile error; parsed data always lands in
  an exact home — parsers never reject numeric data and never silently
  place it in float. [S4.3]
- **SI11 — Total reads, checked writes.** Reads never raise — absence is
  `null` (or `""` for string results), slices clamp; an out-of-bounds
  write always raises. [S7.1]
- **SI12 — The length law.** `len(x)` is exactly the number of iterations
  `for (i in x)` performs; `len` is shallow;
  `len(container) = Σ count(item)`. [S8.3]
- **SI13 — No aliasing.** Values never alias; construction captures by
  value; cycles are unconstructible natively, so deep `==` terminates
  (depth-limited only against interop imports). [S9.1, S9.3, S5.1.3]
- **SI14 — A binding's static type is never a lie.** If `x : T` is
  readable, it holds a real `T`: failure either never reaches the binding
  (establishment skips before it exists) or leaves it unchanged
  (reassignment keeps the previous value) — never a null-for-failure
  placeholder. Every error value is deliberate; discharge strips error
  constituents from its success type; no boundary silently substitutes
  `0`, `null`, or reinterpreted bits. (Fault *timing* is exempt from SI3v2.)
  [S7.4, S7.7, S11.4]
- **SI15 — Schedule invisibility.** `fn` results are identical under any
  schedule, thread count, or backend; builtin reductions are bit-identical
  by the pairwise spec; thread count is semantically unobservable.
  [S12.1, S13.4]

---

## S2 The Value Domain

### S2.1 Types

- **S2.1.1** Scalars: `null`, `bool`, `int`, `integer`, `int64`/`i64`,
  `uint64`/`u64`, sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, `float`/`f64`,
  `decimal`, `string`, `symbol` (with `path` as a special symbol), `binary`,
  `datetime` (with `date`/`time` sub-kinds). Containers: `range`, `list`,
  `array` (transparently unboxed numeric variants), `map`, `element` (a list of
  children *and* a map of attributes), `object` (nominally-typed map).
  First-class: `function`, `type`, `error`.
- **S2.1.2** `number` is a declared union only; `type()` never returns it, an
  alias, or a storage-tier name. Aliases in, canon out. [NM §2.6]

### S2.2 Empty and solid values

- **S2.2.1** `""` is a genuine `string` with `len 0`. `"" == null` is false;
  `"" is string` is true. [C1]
- **S2.2.2** `symbol` and `binary` are **solid types**: every value has
  `len ≥ 1`. The literals `''` and `b''` do not exist — writing them is a
  compile error; runtime operations that would produce a zero-length symbol or
  binary produce `null`. [C1, C1.6a]
- **S2.2.3** Element construction normalizes empty text: `<e "">` ≡ `<e>` — a
  tree-construction rule (the XDM position), independent of string equality. [C1]
- **S2.2.4** An empty file is a childless `<file>` element, not an empty
  binary: existence lives in the container, content in the value. [C2]

### S2.3 Ordered storage, unordered equality

- **S2.3.1** Maps store keys in source/insertion order — round-trip fidelity,
  deterministic iteration, order-significant formats — while map **equality
  compares keys unordered** (§S5.4). Representation order is data; identity is
  content. [C8.6-R]

---

## S3 Truthiness

- **S3.1** The falsy set is exactly **`null` · `false` · error values · `""`**.
  Everything else is truthy — all numbers (`0`, `-0.0`, `nan`), all containers
  (empty or not), datetimes, symbols, binaries, functions, types. [C2]
- **S3.2** **Truthiness is decidable from the type tag alone** — a design
  invariant, not an accident. Any proposal to make a *value* of a truthy type
  falsy must clear this bar; `nan` stays truthy because poison carries no
  falsy tag. [C17]
- **S3.3** Consequences to teach: truthy-0 keeps `or` a safe coalescing
  operator (no `??` needed); `if (results)` does not mean "any results" —
  write `len(results) > 0`; `a div b or 0` does not rescue a zero divisor
  (poison is truthy) — guard the divisor or test `is nan`. [C2, C17]

---

## S4 Numerics

**The poison symmetry.** *Every unbounded numeric domain is closed and total
with its own poison; classification flows up, checks only guard the way down.*
Only the sized machine ints stand outside it: bounded, wrapping, poison-free.
[C16, Int_Type §2.1]

**Interior versus ingress.** Poison arises only *inside* number math (computed
zero divisors, saturation); failure at the numeric world's boundaries —
parsing, casting, narrowing admission, validation — reports `error()`. So
`1 div 0` is `inf` while `int("abc")` is an `error`; `int(s) or 0` is the
working default idiom and `a div b or 0` is not. [C14c, C17]

### S4.1 The `int` type (v5: int53, total)

*"int is an i64 that borrows IEEE's edge semantics."* Authority:
[`Lambda_Semantics_Int_Type.md`](../vibe/Lambda_Semantics_Int_Type.md) §3/§5.

- **S4.1.1** The `int` domain is **{n ∈ ℤ : |n| ≤ 2⁵³−1} plus the three
  closure points `inf`/`-inf`/`nan`**, carried natively in i64. The int53 band
  is simultaneously the domain, the carrier capacity, the saturation point,
  and the `int ⊑ float` subtyping edge — *the band is the subtyping edge*.
  (Supersedes the v4/C16 domain of all float64-representable integers: beyond
  2⁵³ the integers inside a double are not contiguous — `big + 1 == big` —
  against all understanding of what an integer is.)
- **S4.1.2** `int` arithmetic is **closed and total**: `+ - * div % neg abs`,
  bitwise `& | ^`, shifts, and `**` never change type. An out-of-band finite
  result **saturates to sign-preserving `±inf`**; `nan` is reserved for
  indeterminate forms (`0 * inf`, `inf - inf`). Overflow has a definite sign
  and direction; wrapping bit-mixing belongs in `i32`/`i64`. [C16, v5]
- **S4.1.3** Aggregates use **mathematical-value semantics**: the result is the
  true sum if in band, `±inf` by the true sum's sign otherwise, `nan` if any
  element is nan. Intermediate excursions do not saturate (reassociation must
  not change the answer). Arithmetic sys funcs returning int stay total
  (`sum` overflow → `inf`, never an error). [v5 §5.2]
- **S4.1.4** `int ∥ int64`: poison has no `int64` home; every finite int is an
  int64 value. Narrowing into `int` is band-membership — former sparse
  representables (`2⁵⁴`) are admission errors, *loud where v4 was quietly
  non-contiguous*. [v5 §5.5]
- **S4.1.5** Machine ints are Go-aligned: runtime overflow wraps
  two's-complement; literal/constant overflow is a compile error;
  poison-free. [C3]

### S4.2 Poison

- **S4.2.1** `int` and `float` share **one poison identity**, spelled bare
  `inf` / `-inf` / `nan` — ordinary IEEE values. `integer` and `decimal` keep
  their prefixed spellings (`integer.inf`, `decimal.nan`, …). There is no
  `int.`-prefixed spelling. [v5 §5.1, C16]
- **S4.2.2** A value shared across domains **types as its narrowest**:
  `type(nan)` → `int`; `nan is int` and `nan is float` are both true. The
  classification seam lives at `type()`/`is` only. [Int_Type §2.1]
- **S4.2.3** Same-signed infinities are **one value** across all
  poison-bearing domains (`inf == decimal.inf`); **nans are never shared** and
  never equal anything, themselves included. *Poison is unequal, not
  untypeable* — `is`, `match`, and the total order classify it normally. [C16]
- **S4.2.4** Classification flows up: widening admits poison with no check
  (`nan` flows into a `float` or `integer` position and stays `nan`); at
  *narrowing* boundaries a foreign nan is rejected exactly like `3.5`;
  sized-int boundaries admit no poison at all. [C16 f14]

### S4.3 Literals and ingestion

- **S4.3.1** Literal type is **lexical, never value-selected**: a numeric
  literal is `int` iff it has no decimal point and no negative exponent
  (`10e1` is int 100; `10.0`, `10e-1` are float). [C16 f9]
- **S4.3.2** An unsuffixed int-form literal outside ±(2⁵³−1) is a **compile
  error** — under v5 the literal band *is* the domain band. [C16, v5]
- **S4.3.3** The suffix names the type: `n` = `integer` always; `m` = `decimal`
  always (including integer-valued `100m`); fractional `n` is a compile error
  pointing at `m`. The retired uppercase `N` suffix is not in the grammar. [C13]
- **S4.3.4** **Data cannot be rejected**: input parsers place integer tokens in
  the smallest exact home — `int` if in band, else `int64`, else `decimal` —
  never silently in float. *Literals are strict; data always fits.* [C3, C16]

### S4.4 Promotion lattice

- **S4.4.1** One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds
  **exactly** into T2. Chains: `i8 ⊑ i16 ⊑ i32 ⊑ int ⊑ int64 ⊑ integer ⊑
  decimal` (with `int ∥ int64` per S4.1.4 — finite ints embed, poison does
  not); `u8 ⊑ u16 ⊑ u32 ⊑ int`; `f16 ⊑ f32 ⊑ float ⊑ decimal`;
  `int ⊑ float`. Never `i* ⊑ u*`; same width never fits its float. [NM §3.4]
- **S4.4.2** Meets are **type-directed, never magnitude-directed**:
  int×int→int; int×integer→integer; int×float→float; ×decimal→decimal;
  integer×float→**decimal**.
- **S4.4.3** Every **bounded** integer type (`i8`…`u32`, `int`, `i64`, `u64`)
  meets `float` in **`float`**; only the unbounded `integer` meets `float` in
  `decimal`. *Exactness is already gone* once a binary float is an operand.
  [NM 2026-07-29]
- **S4.4.4** Sized×sized selects the smallest containing machine lane and
  stays there for `+ - *`, bitwise, shifts (`i8 + u8 → i16`,
  `i64 + u64 → u64`); sized×non-sized leaves the machine domain first
  (`i8`…`u32` → `int`; `i64`/`u64` → `integer`). [NM §3.3]

### S4.5 Division and modulo

- **S4.5.1** A **literal** zero divisor (`x div 0`, `x % 0`) is a compile
  error; the rules below govern computed zeros. Division by zero never
  raises, at any width or tier. [C14b]
- **S4.5.2** `/` is true division, domain-selected: `int / int → float`;
  float-involved → `float` (IEEE: `1/0 → inf`, `0.0/0.0 → nan`);
  `integer / integer → decimal`; any decimal participation → decimal.
- **S4.5.3** `div` and `%` **stay in their operand domain**: `int div int →
  int`; a computed zero divisor yields the domain's poison (`7 div 0 → inf`,
  `0 div 0 → nan`, `x % 0 → nan`; `integer div 0 → integer.inf`). Integer
  math stays in number and never returns `error()`. Sized operands enter the
  non-sized domains first (`i8 div 0 → inf`; `i64 div u64 → integer`). [C16, C14c]
- **S4.5.4** `div` truncates toward zero; `%` takes the dividend's sign (C
  convention, not Python flooring). IEEE closure rows apply: `inf - inf =
  nan`, `inf * 0 = nan`, `x % ±inf = x`, `0 ** 0 = 1`. [A2, v5 §5.2]
- **S4.5.5** Vectorized integer division is per-lane: the result stays an
  int-family array with `inf`/`nan` at offending lanes — representable and
  type-stable. [C16]

### S4.6 Decimal

- **S4.6.1** `decimal` is one source-level type with invisible storage tiers
  (decimal128 when it fits, unbounded above); `type()` reports `decimal` at
  every tier; literal digits are preserved exactly. [C13]
- **S4.6.2** `+ - *` are exact and may grow storage; division and other
  inexact operations round at the documented decimal context — per-operation
  or per-value, **never a mutable global**. A rational (`p/q`) type is
  rejected: no data format round-trips rationals. [C13]

### S4.7 Float ↔ decimal

- **S4.7.1** **A float denotes its shortest round-trip decimal** — the fewest
  digits that parse back to the same double. This one injective conversion
  governs every mixed float↔decimal operation: `==`, ordering, arithmetic,
  hashing. `0.1m == 0.1` → true; `0.1 + 0.2 == 0.3m` → false (the float sum
  *is* a different number); **decimal contagion is the escape**:
  `0.1m + 0.2 == 0.3m` → true. [C8.5a]

### S4.8 Printing

- **S4.8.1** Floats print as their shortest round-trip decimal; the printer is
  **injective** — distinct doubles print distinctly, print→parse is exact.
  With S4.7.1, WYSIWYG equality holds: two numbers are equal iff they print
  the same (modulo the nan carve-out).* [C8.5a]
- **S4.8.2** Finite `int` values print as integers at every magnitude — no
  decimal point, no exponent — keeping int and float output visibly distinct.
  Merged poison prints bare `inf`/`-inf`/`nan`; `integer`/`decimal` poison
  prints prefixed, and all poison spellings round-trip through the grammar. [C16, v5]

### S4.9 Guest boundary

- **S4.9.1** Numeric FFI is **type-directed, never value-directed** — one rule
  per type, per direction; no rule consults magnitude. Lambda `int` → JS
  `number` (exact always: int53 ⊂ exact doubles); JS number → Lambda `float`,
  always; BigInt ⇄ `integer` losslessly; `int64`/`u64` → BigInt; a guest
  whose numeric type is IEEE double maps to `float`, never `int`. Poison
  crosses as itself. [NM §5, v5 §5.6]

---

## S5 Equality

### S5.1 Total deep value equality

- **S5.1.1** `==` is total over all value pairs: cross-family comparison
  returns `false`, never an error; within-family it is deep, structural, and
  value-based. `!=` is the exact negation. [C8]
- **S5.1.2** **Two designed poison carve-outs**: `nan` (the whole family) and
  `error` values never equal anything, including themselves — the only
  exceptions to reflexivity. An error compared equal could silently enter an
  `if` branch. Detection is `is error` / `is nan`, never `==`. [C8.5]
- **S5.1.3** Structural recursion is depth-limited; exceeding the limit
  **raises** a system fault (a wrong `false` would be silent; a hang worse).
  Cycles are unconstructible natively (§S9) but importable via interop. [C8.5-8]
- **S5.1.4** `==` is the only equality. Values have no identity; no `===`,
  `ref_eq`, or identity operator exists or ever will — reference identity is
  not observable (S1.6). [C4, C8]

### S5.2 Numbers

- **S5.2.1** Numeric equality is by mathematical value across all
  representations: `1 == 1.0 == 1n == 1m`; `-0.0 == 0.0`; storage width is
  representation, not identity; `decimal.inf == inf`. [C8.5, C16]

### S5.3 Sequences

- **S5.3.1** `range`, `list`, and `array` are one sequence family:
  `(1 to 3) == [1,2,3]`, element-wise in order. Unboxed numeric arrays are
  representation only — equality is never layout-based.* [C8.5-2]

### S5.4 Maps, objects, elements

- **S5.4.1** Map equality is key-unordered (JSON/XML, the document standards);
  accepted consequence: `a == b` does not imply `format(a) == format(b)`. [C8.6-R]
- **S5.4.2** Objects are a distinct nominal family; a plain map never equals
  an object.
- **S5.4.3** Element equality = tag + namespace, attributes as an unordered
  map (XML InfoSet), children ordered (document order is meaning).* [C8.6-R]

### S5.5 Functions and types

- **S5.5.1** Function equality is **intensional**: same definition site +
  deep-equal captures. Site = static AST node identity `(module, node)` —
  never a memory address. Dynamically constructed functions compare by the
  content hash of their normalized AST (alpha-normalized), so
  `(x) => x + 1 == (y) => y + 1`. `f == f` is always true.* [C8.7, C9]
- **S5.5.2** Type equality is **representational** — normalized forms compare
  (`int|string == string|int`), not semantic equivalence (undecidable with
  constraints). [C8.5-4]

### S5.6 Dedup, grouping, hashing

- **S5.6.1** `unique`, set semantics, and grouping are defined by `==` with no
  special cases: nulls group together; each `nan`/`error` stands alone. [C8.6]
- **S5.6.2** `a == b ⟹ hash(a) == hash(b)` wherever values are hashed: maps
  hash in canonically sorted key order; numbers hash via canonical value
  across representations. [C8.6-R, C8.5a]

---

## S6 Ordering and Sort

### S6.1 Two relations, by design

*The total order says where things go on a shelf; `<` says which is smaller.*

- **S6.1.1** The comparison operators `< <= > >=` answer magnitude questions
  only. Comparable: numbers, strings (dictionary order), datetimes (same-kind
  by instant; `date` vs `datetime` via day-start coercion; `time` vs the
  others → `error()`). Not comparable — `error()`: symbols, binaries,
  booleans, containers, all cross-family pairs. Statically-visible invalid
  comparisons are compile errors; dynamic ones return `error()`.* [C11.5]
- **S6.1.2** Poison stays incomparable (`nan < x` false both ways; `error`
  operands taint); `null < x` → `null` (absorption, one rule with
  `null + 1 → null`). [C11.5]

### S6.2 The Lambda total order

- **S6.2.1** `sort` and `order by` use the separate total order:*

  > **null < false < true < number (by value) < datetime < symbol (path ⊂
  > symbol) < string < binary < sequence (range = list = array) < map <
  > object < element < type < function < nan < error**

  *Null is less than everything (absence); nan and error are beyond
  everything (broken).* [C11.4]
- **S6.2.2** The total order **totally refines `==`** — equal values always
  tie; ties resolve by stability. Numbers order by mathematical value with no
  representation ranks. Within-band: strings/symbols/binaries bytewise UTF-8
  (no locale collation); sequences lexicographic; maps via canonically sorted
  keys; elements by tag, attributes, children. [C11.4]
- **S6.2.3** Sort is stable; `desc` is **full reversal** — one pure order, no
  pinning exceptions. [C11.4]

---

## S7 Absence and Errors

*Total reads, checked writes, deliberate failures.*

### S7.1 Reads are total; writes are checked

- **S7.1.1** Out-of-bounds and negative indexing, missing map keys, and
  string out-of-range indexing yield **`null`**. Null propagates through
  chained access (`data.users[5].name` → null end-to-end) and scalar
  arithmetic (`null + 1` → null). `arr[i] or default` is the coalescing
  idiom.* [C5]
- **S7.1.2** Slices **clamp** to bounds and return an empty collection (or
  `""` for string results — the result type is preserved). Clamping is
  symmetric: a negative offset clamps to 0, never wraps from the end
  (`slice("hello", -2, 3)` = `"hel"`). An absent source *or absent offset*
  makes the whole selection `null` — a missed search degrades end-to-end
  instead of returning a plausible wrong prefix. A non-null non-integral
  offset is an `error`. [C5, RF3D]
- **S7.1.3** An out-of-bounds **write is a raised error** — not null, not a
  silent no-op. *Reads ask a question; writes issue a command, and a command
  that silently does nothing hides bugs.* Growth is explicit
  (`push`/`splice`). [C5]

### S7.2 Indexing and `last`

- **S7.2.1** Indexing is 0-based; an index is a position, not a direction.
  **Negative indices carry no meaning**: `a[-1]` is out-of-range → `null`,
  exactly like `a[len]` — both failure directions of a computed index are
  symmetric absence. [C15]
- **S7.2.2** Reaching from the end is the reserved word **`last`** =
  `len(container) − 1` of the innermost enclosing subscript's container;
  ordinary arithmetic applies (`a[last - 1]`, `a[5 to last]`). On an empty
  sequence `last = −1`, so `a[last]` → null and `a[last] = v` raises — no
  special case. On N-D arrays `last` resolves against the leading axis.* [C15]
- **S7.2.3** **The two-homes rule**: `last` is legal only inside subscripts
  and as a modifier in `limit` clauses (`limit last 10` — the last 10, in
  original order); everywhere else it is a syntax error. No `last()`
  function, `.last` property, or `first` keyword. [C15a]
- **S7.2.4** Functions never take the `last` keyword and never accept signed
  counts. The blessed API mirror is the named-option pair — the standard for
  every limit-taking function: `{limit: n}` = first n, `{last: n}` = last n;
  both = error; `{limit: 0}` means zero (*a count counts* — no zero
  sentinel); unlimited is absence, never `-1`.* [C15b]

### S7.3 Aggregation

- **S7.3.1** A `null` input makes the aggregate `null` — uniformly. `sum =
  reduce(+, v, 0)` stays literally true (SQL's skipping aggregates contradict
  SQL's own scalar algebra). Skipping is explicit: `xs[!null]`, or the
  `skip_null` option on denominator-sensitive statistics.* [C5.3]
- **S7.3.2** Monoid identities: `sum([])` = 0, `prod([])` = 1; identity-less
  aggregates (`avg`, `min`, `max`) over empty input yield `null`. [C5.3]

### S7.4 The three failure channels

*Lambda has exactly three failure channels. Two are types — always visible in
the signature, always enforced. The third is never a type — the system owns
it.* [TE-13, C14]

- **S7.4.1** **Value errors `T | error`** — soft; flow as data; no caller
  obligation; detected at type boundaries. This is the only form inference
  produces. *Interiors flow, interfaces enforce.*
- **S7.4.2** **Raised errors `T^` / `T^E`** — enforcing; explicit-only
  (inference never creates `^`); must be engaged at the immediate expression;
  alone license `raise`. `raise` in a `T | error` fn is a compile error —
  construct and return `error(...)` instead. *`^` raises; `| error` returns.*
  In value positions `T^` ≡ `T | error`; `^` is semantically distinctive only
  on function returns. Every `R^E` is operationally `R^(E | error)`: `E`
  constrains user-raised errors, system defects flow implicitly.
- **S7.4.3** **System faults** — stack exhaustion, out-of-memory, the `==`
  depth limit, compiler-inserted boundary defects. Never in types;
  transparent through `fn` frames; caught only at a `pn` `^ { }` boundary or
  the global handler (§S7.11). *We handle it for the user.*
- **S7.4.4** An error value is a first-class value carrying `code`, `message`,
  and source location; constructors `error(msg)`, `error(msg, source)`,
  `error({...})`. **Every error value is deliberate** — constructed by
  `error()`, returned by a `T | error` computation, raised in a `T^E`
  context, or produced by a failed deferred type check. Rich diagnostics are
  mandatory at every checked-channel failure (fault-channel records are
  pre-reserved and lean).* [C14, TE-4, TE-9]
- **S7.4.5** System `fn` failures are values, never `T^E`: *absence in / no
  answer → `null`* (or `""` for string results); present-but-invalid →
  `error()` (§S7.10). `input`/`fetch` are effectful readers — pn-family, they
  **raise** (`T^E`), though permitted in expression position; set-oriented
  input is an explicit wrapper (`fn my_input(f) { input(f) ^ { ^ } }`). The
  handler is the acknowledgment boundary: `^ { ^ }` engages the hard raised
  error and returns it as a soft `error` value (`T | error`).
  [C14, C14a]

### S7.5 Acknowledgment

- **S7.5.1** A call carrying a `^` channel must engage the error at the
  **immediate expression**, through exactly one of: an `error` arm in
  `match`; the `^ { }` handler; postfix `^`; an `or` rescue; or a receiving
  position that textually admits error (`let x: T^`, `let x: T | error`, or
  a declared param/return of that shape). *Must-handle = must-engage-
  explicitly.* [TE-13, TE-16]
- **S7.5.2** `any` never acknowledges (it admits error but engages nothing);
  a bare `let x = a()` never acknowledges. Automatic containment (§S7.7)
  never acknowledges: *skip is containment, not acknowledgement.* [TE-13, TE-16]
- **S7.5.3** `or`-rescue is not a rule-bend: errors are falsy, so `a() or 0`
  consumes the error by the truthiness definitions and counts as engagement.
  The typing rule is normative: `type(a or b) = (type(a) \ {error, null}) |
  type(b)`, so `int(s) or 0 : int` — plain union arithmetic, no flow
  analysis. Errors log at origination, so a consumed diagnostic still leaves
  a breadcrumb. [TE-13]

### S7.6 Discharge: the handler and postfix `^`

- **S7.6.1v3*** **`e ^ { … ^ … }` handles the error locally and is
  channel-agnostic** — it receives soft values, raised errors, and (at `pn`
  boundaries) system faults alike, binding the error to handler-local `^`
  (innermost-wins). In the one-arm form, the handler introduces no `~`
  binding: `~` retains the current-value meaning supplied by an enclosing pipe,
  match, constraint, or view context, and a non-error operand passes through
  unchanged. Typing mirrors `or`:
  `type(e ^ { h }) = (type(e) \ error) | type(h)`.

  The optional two-arm form **`e ^ { h } ~ { v }`** evaluates `e` exactly
  once. An error selects `h`, with `^` bound to that error and any enclosing
  `~` left intact; every non-error value — including `null` and `false` —
  selects `v`, with `~` bound to that value as the innermost current-value
  context. Its type is `type(h) | type(v)`. An error raised while evaluating
  either selected arm is a fresh outcome and is not consumed again by the
  same handler. In statement position, the selected arm executes as a
  statement body and normal completion continues after the handler.

  In either form the selected handler result has the ordinary contextual type
  or the arm does not complete normally (`raise` / `return`); the binding it
  feeds is therefore **statically clean** — *sound by construction*, no flow
  analysis: *a binding's static type is never a lie* (SI14). [TE-16]
- **S7.6.2v3** The handler is the left-associative postfix-primary form
  `primary ^ { error_body }`, optionally followed immediately by the normal
  value arm `~ { value_body }`. It occupies the same precedence tier as member
  (`.`) and query (`?`) access. Consequently `a + b ^ { h } ~ { v }` means
  `a + (b ^ { h } ~ { v })`; handling the complete binary expression requires
  `(a + b) ^ { h } ~ { v }`. The complete handler result is primary-like, so
  postfix operations continue left-to-right: `e ^ { h } ~ { v }.field` means
  `(e ^ { h } ~ { v }).field`, and handler chains associate from the left.
  Both arms are brace-delimited; the optional `~ { … }` belongs to the handler
  only when it immediately follows the error arm. `^` followed by anything
  other than `{` is propagation (`f()^ - 1` propagates then subtracts). There
  is no prefix `^ { h } e` shorthand and no `else`, `default`, `error`, or
  `catch` spelling for the second arm. The caret belongs to the handler or
  propagation construct, never to `call_expr`. [TE-16]
- **S7.6.3v2** **Postfix `e^` propagates** and occupies the same
  left-associative postfix-primary tier as the handler, member access, and
  query access. Its operand is a primary expression; parentheses admit a
  wider expression. The propagation construct owns its caret — a call has no
  optional trailing caret. Propagation yields the error-free success type (a
  type-narrowing operator — `let b = a()^` gives `b : T`,
  lane-eligible) and forwards the combined error set to the enclosing
  function's declared channel. In a declared plain-`T` function it is a
  compile error. `^` on an operand whose *explicit* type excludes error is a
  compile error; where cleanness is merely inferred, a defensive `^` is
  warn-only. [TE-13]
- **S7.6.4** **`?` is not propagation.** Postfix `?` is the query operator;
  `T?` is the nullable type marker. The propagation spelling is `^` — a
  deliberate divergence from Rust/Swift habits. [TE-13]
- **S7.6.5v2** The retired forms `let a^err = e` and prefix `^err` /
  `if (^err)` do not exist: the destructure was Go's `(v, err)` product with
  a typing hole (`a` claimed `T` while holding null), and the test is spelled
  `e is error`. `^` appears in four syntactic contexts — postfix propagation,
  postfix braced-handler delimitation, the type-level channel, and the
  handler-local current-error atom — all meaning "the error channel". There
  is no general prefix error test or prefix braced-handler shorthand.* [TE-16]
- **S7.6.6v2** Division of labor: `or` catches all falsy without access;
  `^ { }` catches errors only, with access and normal-value pass-through;
  `^ { } ~ { }` branches explicitly between error and non-error outcomes;
  `e^` catches errors only, propagating. `or` and `^` are *not* a soft/hard
  split — both work on both channels; the axis is coalescing-without-access
  vs error-specific handling. [TE-16]
- **S7.6.7v2*** Either handler form over a possibly-suspending operand is a
  **compile error** — a recovery frame cannot span a scheduler yield, and
  silently splitting the capability ("same construct, two behaviours by
  invisible context") is rejected. [TE-16, ER-D13]

### S7.7 Containment: the declaration-boundary skip

*Regions are created by declarations, not by control structures. The guard
dominates the scope.* [TE-15, TE-18]

- **S7.7.1*** Skip is a **declaration-boundary mechanism only** — `let`,
  `var`, `for` loop variables, declared parameters, declared returns.
  **Expression interiors never skip**: in `a + e + b` the error flows as a
  value and the expression types `T | error`. (Strict left-to-right
  evaluation order is *not* normative.) [TE-18]
- **S7.7.2*** A failed deferred check at a declaration skips to the end of
  the block that **declares** the binding; that block yields the error; the
  binding is never established, and code between failure and boundary never
  runs. On success, the declaration's guard **dominates every use of the
  binding in its scope** — native-lane, no per-use checks. [TE-18]
- **S7.7.3*** A declared parameter guards at the **call site**: on failure the
  function is not entered and **the call expression evaluates to that
  error** — call-site contagion, never a hidden early return, never a
  widening of the callee's signature. [TE-18, TE-5]
- **S7.7.4*** Reassignment to a declared `var` carries a diagnostic
  obligation, in three tiers: compile error where the RHS is provably
  `T | error`; compile warning where deferred-fallible with a tail to
  abandon; a runtime report naming the binding that kept its previous value.
  `x = e ^ { … }` suppresses all three. **No use of a binding ever observes
  a value left by a failed assignment.** [TE-18]
- **S7.7.5*** In a `for`, the skip target is the **iteration body**, not the
  loop: per-item skip keeps the batch alive; *body native, result boxed* —
  the loop result is `(T | error)[]`. An accumulator declared outside the
  loop dies with the batch, correctly (a stale accumulator would poison every
  later iteration); a per-item temp declared inside it continues. *Batch
  friendliness is a theorem, not a hope.* [TE-15, TE-18]
- **S7.7.6*** Edge sites: element/field stores report via S7.7.4's tiers
  (containers are not scoped away — documented partial state); a failed
  module-level reassignment **aborts module initialization**; a cross-frame
  reassignment from a closure becomes an error return of the inner function,
  re-originating at the call site. [TE-18]
- **S7.7.7** Rescue moves to the initializer: `let a: T = e or 0` and
  `let a = e ^ { … }`; an `a or 0` *after* the binding is unreachable by
  construction. [TE-15]

### S7.8 Containers and errors

- **S7.8.1*** Acceptance is read from the **destination contract**, never from
  syntactic position — uniformly for literal elements, indexed/field stores,
  `push`/`splice`, parameters, and returns: a contract that admits error
  (`any`, `error`, `T | error`, unannotated boxed slot) **accepts**; a native
  lane whose source is provably infallible enters the lane branch-free; a
  native lane whose source is only `T | error` **cannot enter the lane** —
  the value stays boxed until discharged, and the standing acknowledgment
  obligation is what narrows it. *Typed containers are all-or-nothing by
  construction; per-element error retention is a capability of Item-lane
  containers.* [TE-17]
- **S7.8.2** Both spellings are visible in source:
  `[ f(x) ^ { 0 } for x in xs ]` → `int[]`, lane preserved;
  `[ f(x) for x in xs ]` → `(int | error)[]`, boxed, per-element errors
  retained. This is type-level, not representation-level: `int[]` excludes
  error because its element type does; `ArrayNum` may hold `nan` (a float
  value, not an error). [TE-17]

### S7.9 What an error participates in

*An error is a failure that has not yet been discharged.* The split is one
question: **can the result be mistaken for a successful computation?** [§7.6 record]

- **S7.9.1** **Type family participates**: `err is error` → true, `type(err)`
  → `error`, `match` dispatches the error arm — detection must always work.
- **S7.9.2** **Truthy family participates**: `if (err)`, `not err`,
  `err or d`, `err == err`, `err ^ { }` — these are the discharge surfaces;
  absorbing is their job.
- **S7.9.3** **Value family propagates**: `string/symbol/name(err)`,
  `len(err)`, arithmetic, comparisons, conversions, `err in x`, `err at x`,
  and every other value function return the error. `in` searches one level,
  so a `false` would read as "error-free" — the honest answer is that the
  question was not answered. One exemption: **`print(err)` participates** —
  inspection, and it emits rather than returning a flowable value.
- **S7.9.4** **Containment is not participation**: `[err]` is an array,
  `len([1, err, 3])` is 3 — propagation concerns an error *being* the
  operand, not being reachable from it.

### S7.10 The sys-func return contract

*Take input broadly; keep results in domain. Preserve the successful result's
cardinality, and keep failure on a separate channel.* [RF1–RF6, §7.7 record]

- **S7.10.1** Result shape is fixed by the contract, never by cardinality:
  zero-to-many → `[]`; zero-or-one → `null`; string-valued no-content →
  `""`; non-admissive invalid input → `error`. The four are distinct and must
  stay distinguishable.
- **S7.10.2** Admission is an explicit per-function contract: an **admissive**
  case has a meaningful no-answer reading and returns result-domain absence
  (`arr[-1]`, `argmin([])`); a **non-admissive** case returns a detailed
  `error()` where absence would hide a malformed source (`int("abc")`,
  malformed `parse`). Both absences are falsy, so `f(x) or default` rescues
  uniformly. A declared type boundary is always non-admissive on mismatch.
- **S7.10.3** **No in-band sentinels, ever.** `index_of`, `last_index_of`,
  and `ord` return `int | null` — absence is never `-1`, never an unchanged
  input. *A sentinel is only a sentinel at rest; `null` survives
  computation.* Migration: `idx >= 0` remains a valid found test; `idx < 0`
  silently breaks (null comparisons are false) — absence is tested with
  `is null`. Private `-1` adapters must normalize at the Lambda boundary.
- **S7.10.4** Error operands are rejected at the call boundary (parameters
  are `any \ error`), keeping "error operand" distinct from "no match".
- **S7.10.5*** Vectorized sys funcs are array-in/array-out: one lane does not
  collapse to a scalar; zero lanes produce a typed empty; lane exceptions
  produce lane values (`nan`/`inf`), never a shape change.
- **S7.10.6** A mutator family picks one public convention (updated owner, or
  unit) and holds it; `[]` never means "mutation succeeded"; invalid mutation
  is `error`, never the unchanged input. (Which convention — open, App. B.)

### S7.11 System faults and recovery

- **S7.11.1** Fault reasons are a closed, typed set: stack overflow,
  side-stack exhaustion, out-of-memory, equality-depth exhaustion, runtime
  boundary defect. Faults never enter function types and are never a normal
  call-result ABI — recursion never forces `T^stack_overflow`. [ER-D4, ER-D9]
- **S7.11.2** Faults pass transparently through `fn` frames; only `pn`
  boundaries and execution boundaries own them. A caught fault cannot resume
  the abandoned expression. Recovery frames never survive a scheduler yield —
  an async task completes with the fault result. [ER-D9, ER-D11]
- **S7.11.3** Transaction barriers (module init, hosted-guest entry) take
  priority over inner handlers: no handler may resume through a
  half-initialized module or abandoned guest activation. Fault delivery
  cannot allocate; on OOM-during-error the primary error is discarded and
  `OUT_OF_MEMORY` is raised with the prior code attached. [ER-D2, ER-D6]
- **S7.11.4** Production containment is fail-stop: recoverable faults use
  recovery frames; arbitrary memory faults terminate the process and are
  never recast as language errors. Unhandled faults abort with a report.
  Fault *timing* is exempt from S1.6 — when a stack limit fires may differ
  across execution tiers.* [ER-D10]

---

## S8 Membership and Iteration

### S8.1 The `in`/`at` axis

- **S8.1.1** `in` is value membership; `at` is key-name membership — used
  identically in iteration and membership: `for (x in coll)` ↔ `x in coll`;
  `for (k at m)` ↔ `k at m`. Operand order: member left, container right.
  *Whatever `for…in` walks, `in` tests.* [C5.3a]
- **S8.1.2** On elements, `in` ranges over attribute values then children;
  `at` ranges over attribute keys — one meaning across the map/list duality.

### S8.2 The key space

- **S8.2.1** Every item in every container has a key, in one space: maps key
  by symbol; arrays/lists/ranges by integer index; elements by **both**.
  `for (k, v in c)` exposes the key uniformly (`[for (k, v in [10,20]) k]` →
  `[0, 1]`). [§8.0 record]
- **S8.2.2*** A *name* is a symbol key only. `at` ranges over names, so
  `1 at [10, 20, 30]` is **false** — the narrower reading is what lets
  `for (k at e)` give an element's attributes without its children; an index
  bound is written `i < len(arr)`. [§8.0.1 record]

### S8.3 `len`

- **S8.3.1** The law: **`len(x)` is the number of iterations `for (i in x)`
  performs.** Consequences, not separate rules: `len("str")` = 3;
  `len([[1,2],[3]])` = **2** (shallow — the count indexing needs);
  `len({a: null, b: 2})` = 2; `len(null)` = 0 (absence is the empty
  sequence); `len(err)` = **error** (iterating an error yields an error, not
  nothing — collapsing it onto 0 would make a failed computation
  indistinguishable from an empty one); `len(<e a:1, b:2; "t">)` = **3**
  (attributes + children). [§8.1 record]
- **S8.3.2*** Lazy sequences: a forceable stream's `len` forces and returns
  the actual size; a non-forceable/infinite stream's `len` is **`inf`** — the
  honest answer, impossible to mistake for a size. Consequently a `for` over
  a statically-infinite stream is a **compile error**; unbounded streams are
  consumed by recursion with an explicit termination condition. [§8.1 record]
- **S8.3.3** Two lengths: `len` measures content; `count(x)` — defined, not
  implemented — is 1 for every value, making shallowness an identity:
  `len(container) = Σ count(item)`. For-expressions and spreads *splice at
  the construction site* — by the time `count` applies, there is one value.
  [§8.3 record]

### S8.4 Projections

- **S8.4.1** `keys(c)` ≡ `for (k, v in c) k`; `values(c)` ≡
  `for (k, v in c) v`; `names(c)` ≡ `for (k at c) k`. Defined by the
  comprehensions they abbreviate; deliberately not built in until a call
  site pays for one. `len(names(c))` is the `at`-axis length. [§8.2 record]

---

## S9 Mutability: Mutable Value Semantics

### S9.1 The model

*Values never alias; `var` is the only mutability marker and the only sharing
construct; `let` is final.* [C4]

- **S9.1.1** `let` is final: nothing reachable through a `let` binding ever
  changes. `let` is the variable of algebra (referential transparency);
  `var` is the primed variable of program logic — every mutation is a total
  function of the old value (`push(b, 2)` ≡ `b′ = b ++ [2]`), which is what
  makes `pn` *locally imperative but observably functional*.
- **S9.1.2** Binding, assignment, and construction copy, observably, for
  every container kind. Implementation is COW; **sharing must be
  unobservable** — a verifiable property, not a convention.
- **S9.1.3*** `var` parameters (`pn f(var a: T)`) are the sole sharing
  construct — an inout borrow. Compile checks: arguments must be `var`;
  **exclusivity** (writer-vs-writer only: the same `var` or overlapping
  paths cannot feed two `var` params; plain params snapshot before any
  borrow's mutation begins, so readers need no check); `pn` methods require
  a `var` receiver; `var` parameters are **invariant** (S9.2.1).
- **S9.1.4*** Closures are immutable values: captures snapshot at creation;
  assignment to a captured name — including interior mutation through it —
  is a compile error. State lives in module-level `var`s, view state, and
  objects with `pn` methods; never inside a function value. (A non-escaping
  nested `pn` used only in call position may later be allowed direct access
  — designed, deferred.)
- **S9.1.5** No reference cells; structural `==` is the only equality.
  Cycles are unconstructible, so `==` is total and no cycle collector is
  needed.

### S9.2 Covariance, borrows, and views

- **S9.2.1** *Covariance where values copy, invariance where they're
  borrowed*: `int[] <: any[]` holds for `is`, reads, value params, and
  assignment (which copies — the covariant-array hole cannot arise); passing
  `var xs: int[]` to `pn f(var a: any[])` is a compile error. [C12]
- **S9.2.2*** Read views are first-class values with snapshot semantics (a
  zero-copy slice observably behaves as a copy taken at creation).
  Write-through views are **borrows, never values** — legal only in
  `var`-param position, exclusivity-checked, non-escaping. Creating a
  mutable borrow over shared storage un-shares first. [CW16]
- **S9.2.3*** Iterating a `var` container walks the entry-time value: the
  loop share-marks at the head, and the first in-body mutation copies — no
  iterator invalidation. The same rule covers pipes over `var` containers.
  [CW §11.6]
- **S9.2.4*** A module-level or view-state `var` may not be passed as a
  `var` argument (no call-site check can see the callee's independent path
  to the same storage). [CW §11.4]

### S9.3 Construction captures values

- **S9.3.1** Placing a value into a container captures it **by value** at
  every constructor and insertion point — literals, field/index writes,
  `push`/`splice`. After construction, container and source are independent.
  A corollary of "values never alias", and what makes cycles
  unconstructible. Porting hazard (silent): *fill-after-storing* leaves the
  stored copy empty — fill before storing, or read-modify-write. [C4]

---

## S10 Operators

### S10.1 Union and pipe

- **S10.1.1** `|` means union/alternative **everywhere**: type expressions,
  match or-patterns, string patterns, value expressions (types are
  first-class). `|>` is the pipe. [C6]
- **S10.1.2** The pipe is dual-mode on a parse-time syntactic test: a body
  with a **free `~`** is a mapping pipe (binds `~` per item; `~#` is the
  current key/index); with no free `~` it is whole-value application
  (`data |> sum` ≡ `sum(data)`; extra args append: `data |> take(3)` ≡
  `take(data, 3)`). A `~`-free non-callable body is a type error. Scalars
  pipe as a whole value. [C6]
- **S10.1.3** `~` is lexically scoped to the RHS of its pipe; nested
  constructs shadow, **innermost-wins** — uniformly for pipes, `that`
  clauses, match arms, and `last` (S7.2.2); reach an outer item via a `let`.
- **S10.1.4** File write/append syntax is deferred; `output(data, file)` is
  the interim. [C6a]

### S10.2 Vectorization

- **S10.2.1** Arithmetic `+ - * /` is vectorized (element-wise with
  broadcasting) — vector arithmetic is mathematics. [C10]
- **S10.2.2*** Bare comparisons `< <= > >=` are **scalar-only**, never
  element-wise (the killing exhibit: `if ([1,2,3] > 99)` took the
  then-branch — a mask is a container, containers are truthy). Element-wise
  comparison has its own keyword operators **`eq ne lt le gt ge`**, yielding
  bool masks; nan lanes compare false. Bare `==` is untouched. [C10]
- **S10.2.3*** Mask consumption is explicit and non-magical: `sum(mask)`
  counts true lanes; `a[mask]` is boolean indexing. `and`/`or` remain scalar
  short-circuit operators, never mask combinators. Condition-position lints
  protect the truthiness boundary (suggest `any`/`all`/`len`). [C10]

### S10.3 Keyword operators

- **S10.3.1** `and or not is in to div that where at eq ne lt le gt ge` —
  Lambda is a keyword-operator language; new operators prefer words over
  sigils.

---

## S11 Types and Patterns

### S11.1 Types compose like values

- **S11.1.1** A bracket type is a structural pattern whose positions mix
  values and types freely: `[1, int, "str"]`; `[int]` is **exactly one int**
  (TypeScript's reading — forced by compositionality at n = 1) and enforced
  with a teaching message; `int[]` is the homogeneous-array spelling, with a
  lint on bare `[T]` in annotation position only.* [C7]
- **S11.1.2** String structural patterns are delimited islands: `\( ... )`
  denotes a string-domain pattern and `\symbol( ... )` denotes a
  symbol-domain pattern. Inside an island, quoted literals are strings, `d`,
  `w`, `s`, `a`, `.`, and `...` are the reserved pattern atoms, whitespace is
  concatenation, and the existing union, grouping, occurrence, negation, and
  `to` rules apply. A pattern's tag is part of its type value: matching checks
  the value domain before content, so a string never satisfies a symbol
  pattern or vice versa. A literal-only island is representationally identical
  to the corresponding ordinary literal union; named structural patterns may
  be reused as content inside either tagged domain. [S10.1.1, D3.1.1v2, D3.1.2]
- **S11.1.3** A range type `X to Y` denotes inclusive membership in the
  consecutive values between its bounds. Integer ranges admit exact integer
  values; string ranges require single Unicode-codepoint strings and admit
  only single-codepoint strings in the inclusive codepoint interval. Range
  bounds of mixed domains or strings containing more than one codepoint are
  errors, not coercions. The same membership rule applies in annotations,
  match arms, and value expressions; indexing or iteration materializes each
  character-range member as a one-codepoint string. [S7.1.1, S11.2.1]

### S11.2 Match

- **S11.2.1** `match` is a **type match**: arms are type expressions tried in
  order, first match wins, no fall-through; literal arms are singleton types
  dispatched by `==`; type arms dispatch via `is`; constrained arms add a
  predicate (`case int that (~ > 0):`). `~` is the matched value in every
  arm body, narrowed to the matched type — no destructuring sub-language.
  The scrutinee evaluates exactly once; `default` must be last.* [C8.5c, Match]
- **S11.2.2** Poison is unequal, not untypeable: `case float:` catches nan,
  `case error:` catches errors; errors reach `default` only when no `error`
  arm exists. The `type(x)` trap: `match type(err) { case error: }` falls to
  `default` — a type value matches only `case type:`; *`match x` already is
  Lambda's typeof-switch.* [C8.5c]
- **S11.2.3*** Exhaustiveness is compiler-checked: unions need every
  constituent, `bool` both arms, `T?` needs `T` and `null`; open scrutinee
  types require `default`. Statement arms yield `null` in expression
  context. [Match]

### S11.3 Structural `is`, nominal objects

- **S11.3.1** `is` is structural for maps/arrays/elements (extra fields
  permitted; key lookup by name) and nominal for object types. `is` is
  type-directional: `3.0 is int` is false even though a deferred `int`
  boundary admits `3.0` (S11.4.5) — membership asks what a value *is*; the
  boundary asks what it may *satisfy*. [C7, TE-6]

### S11.4 Declared types are contracts

Full record: [`Lambda_Design_Type_Enforcement.md`](../vibe/Lambda_Design_Type_Enforcement.md) (TE-1–TE-18).

- **S11.4.1** An annotation is a contract on the binding, not a hint.
  **Three outcomes, never a fourth**: statically proven, statically
  rejected, or a deferred runtime check whose failure produces a rich error
  (boundary, expected type, actual value, location) — never null, never a
  wrong value, never silence. Failure never establishes the binding;
  reassignment checks before commit and leaves the old value unchanged.
  An annotation may drive representation only if its boundary is enforced.
  [TE-1, TE-2, TE-4, TE-9]
- **S11.4.2** Signatures spell both failure dimensions: plain `T` excludes
  null *and* error; absence is `T?`, failure is `T | error` (soft) or `T^E`
  (enforcing). **Declared returns are effect firewalls**: a plain-`T`
  function with an open body is a compile error — the author must *contain,
  disclose, or impose*. An unannotated fn is implicitly contracted
  `(any \ error, …) -> any \ error` and enforced identically; disclosure is
  soft (`| error`), so a pushed-open function never spills `^` onto callers.
  [TE-5, TE §10.7]
- **S11.4.3** `any` is the top type and includes error; **`any \ error` is
  the non-error top and the unwritten default of the untyped world**
  (untyped params, dynamic reads, unannotated returns). Explicit `any` is
  the opt-in to carry errors. An error argument reaching an `any \ error`
  parameter never enters the function — the call's result is that error.*
  [TE-5]
- **S11.4.4** *When the user is explicit, we check explicitly*: an explicitly
  declared error possibility cannot enter a plain-`T` position without
  visible discharge (`let x: int = a()` where `a : int | error` is a compile
  error; the idioms are `or`, `^ { }`, `^`, or the union binding). Only
  implicitly open values cross as deferred checks. [TE-13]
- **S11.4.5*** Deferred numeric admission is **value-aware**: an
  exactly-embedding value passes and is re-represented (`float 3.0` →
  `int 3`); an inexact one fails with the rich error. Poison follows domain
  membership (S4.2.4). Static positions reject the whole class
  (`let x: int = 3.0` is a compile error). [TE-5, TE-6]
- **S11.4.6*** User-defined types are enforced by the validator: deep, on
  first crossing, or a rich error with a validator path and no binding.
  Named map types are **open** — extra fields pass. Constrained types
  (`T where …`) enforce the base only, for now. [TE-10]
- **S11.4.7** Containment and discharge follow §S7.7–S7.8: skip at
  declaration boundaries, destination-contract container acceptance, and
  `^ { }` as the engagement form that suppresses the skip.

---

## S12 Functions, Effects, Resources

### S12.1 The one-bit effect system

- **S12.1.1** `fn`/`pn` is a declared, compiler-checked, one-bit effect
  system: `fn` is pure and deterministic under any schedule; `pn` may have
  effects; `fn` cannot call `pn`. Accepted price: no reified effects — a
  `pn` call executes; there is no held, unexecuted effect value. [Features §3.6]
- **S12.1.2** `break`, `continue`, `return`, `while`, and `var` declarations
  are `pn`-only; using them in an `fn` is a compile error. `return` inside
  `for` exits the function; a function without an explicit `return` returns
  its last expression. [Procedural]
- **S12.1.3** Reactive templates are the doctrine applied: template body =
  pure `fn` transformation; mutation only in `on` handlers (`pn`) — the Elm
  architecture enforced by the effect bit. [Features §3.7]

### S12.2 Assignment

- **S12.2.1** `let` bindings and parameters are immutable; only `var`
  reassigns; shadowing is allowed. An unannotated `var` may change runtime
  type on reassignment; an annotated `var` constrains every assignment to
  the declared type (S11.4.1). Implicit widening at annotated positions is
  by **exact embedding** only (S4.4.1) — never lossy coercion; assignment
  never silently corrupts data. [Proc_Assignment]
- **S12.2.2** Element mutation is defined on both faces: `elem.attr = v` as
  map-field assignment; `elem[i] = v` as child assignment.

### S12.3 The call contract

- **S12.3.1** A function has at most **16 source-language argument slots**
  (a rest collector consumes one); exceeding it is an ordinary Lambda error,
  statically diagnosed where possible. Hidden ABI operands do not count.
  [Function_Arg]
- **S12.3.2** Two dynamic-call restrictions are deliberate: a dynamic call
  with named arguments is rejected, and a dynamic call to a `var`/inout
  signature is rejected (a value span carries no writable caller location).

### S12.4 Resources

*Auto-close is `with` without the `with`. Close is not just release — it is
the last write.* [Features R1–R5]

- **S12.4.1*** `open()` is resource acquisition and is `pn`-only. The
  source quartet: `input()` eager value; `stream()` lazy plan; `open()`
  scoped resource. `input()` is not a resource — it closes inside the call
  and returns a pure value.
- **S12.4.2*** A resource auto-closes at the end of its enclosing **block**;
  ownership escapes only by `return`, and the escape must be visible in the
  declared return type — any other escape is a compile error.
  Cleanup-on-failure and transfer-on-success fall out (*`errdefer` for
  free*). Cleanup runs innermost-out as an error propagates, before any
  boundary handler observes it; a cancelled await is an error-shaped exit,
  so cancellation safety is inherited.
- **S12.4.3*** No `defer`, `with`, or `finally` keyword: auto-close is the
  only user-facing cleanup mechanism; non-close cases are modeled as scoped
  resources with a close capability. The GC finalizer is a backstop and
  leak detector, never the closing mechanism — *GC runs on memory pressure,
  not resource pressure.*

---

## S13 Concurrency

*One keyword, two tiers. Concurrency enters a program through exactly one
word.* Full record: [`Lambda_Design_Concurrency.md`](../vibe/Lambda_Design_Concurrency.md) (K11–K32).

### S13.1 Tasks and workers

- **S13.1.1** `start` is the only concurrency keyword — contextual, legal
  only where an expression begins inside a `pn`, operand a `pn` call.
  Everything else (`wait`, `send`, `receive`, `select`, `worker`, `cancel`,
  `self`) is a builtin `pn`. **`async` and `await` do not exist.** [K12]
- **S13.1.2** Calls are **colorless**: `f(x)` synchronously yields the value
  and may suspend invisibly (`f(x)` ≡ `wait(start f(x))` minus the handle);
  may-suspend-ness is inferred and never observable. Every Lambda `pn`
  exposed to JS is uniformly Promise-returning; a Lambda resume is a
  macrotask. [K16]
- **S13.1.3*** Two tiers, one handle vocabulary: tasks (`start f(x)`, shared
  context) and workers (`start worker(spec, isolation: 'thread'|'process')`,
  share-nothing isolate; default `'thread'`). Handles are uniform:
  awaitable, sendable-to, selectable, cancellable; they compare by identity
  and only the concurrency builtins operate on them. [K11, K31]
- **S13.1.4** **The capture rule**: a `start` operand must not capture
  `var`s by reference — compile error. Tasks communicate only via messages
  and immutable values; consequence: **thread count is semantically
  unobservable**. [K13]
- **S13.1.5** **Failures are values; faults are not.** `wait(h)` yields
  `T^E` for errors, raised values, and cancellation; that surface is total
  only under process isolation — under thread isolation a hard fault is
  process-fatal. *Share-nothing is the model; read-only sharing is the
  representation.* [K18, K31/K32]

### S13.2 Messaging

- **S13.2.1** Handle = address; there is no channel type. One mailbox per
  task; N:1 by design (a shared work queue is an explicit dispatcher).
  `receive()` yields the oldest message — FIFO-head only, no in-queue
  selective receive (*the BEAM rescan trap is unrepresentable*); dispatch
  with `match`. [K20]
- **S13.2.2** `send(h, msg)` never blocks and returns `ok^E`: a full mailbox
  is the error value `'mailbox_full'` — never blocking, silent drop, or
  unbounded growth. *Backpressure is an error value, visible in the type
  system.* [K20d]
- **S13.2.3** Ordering: per-sender FIFO; a task's termination becomes
  observable only after its previously sent messages are enqueued;
  end-of-stream *is* handle completion carrying the final `T^E` — no
  sentinel or injected system messages. [K20e]

### S13.3 Scope and cancellation

- **S13.3.1** A started handle is a scoped resource owned by the nearest
  lexical block: normal exit **joins**; error exit **cancels then joins**;
  ownership escapes only by `return` (visible in the type). Storing or
  sending a handle grants capability, not ownership. [K30a/b]
- **S13.3.2** Cancellation is an error value at park points (`'cancelled'`),
  unwinding by ordinary `^` propagation with auto-close cleanup on the
  path; cleanup runs cancellation-masked. Any holder may cancel; idempotent.
  `wait(h, timeout:)` times out the **waiter** only — *observing ≠ owning*.
  [K30c–f]

### S13.4 Determinism

- **S13.4.1*** Builtin numeric reductions (`sum`, `avg`, `prod`, `variance`,
  dot, `min`/`max` join) are **pairwise-by-spec**: a fixed tree order
  depending only on n — bit-identical across runs, machines, thread counts,
  and SIMD/scalar/parallel backends. *Same everywhere, always.*
  User-supplied `reduce` stays strictly sequential (purity ≠ associativity).
  [K19]
- **S13.4.2*** In stream pipelines, `fn` stages auto-parallelize and are
  **ordered by default** (a re-sequencing buffer restores index order);
  `pn` stages are sequential anchors. [K22, K23]

---

## S14 Data Processing

### S14.1 For-clause grouping and joins

- **S14.1.1** `group by KEY [as ALIAS], … into g` — and **a group is an
  element**: keys are attributes, members are children, tag `'group'`. *A
  group is a document node* — it formats and queries like any element.
  [FC1, FC10]
- **S14.1.2** Key equality is `==` with numeric-tower coherence (`1` and
  `1.0` group together); null keys form one group. Groups emit in
  first-appearance order; `order by`/`limit`/`offset` after `group by`
  apply to groups; loop variables go out of scope after grouping — only the
  `into` binding survives. An omitted `as` infers a name only from a
  trailing field access; anything else demands `as` (no generated names).
  [FC2–FC4, FC9]
- **S14.1.3** Join `on` is restricted to conjunctions of equality tests
  (non-equi conditions are a compile error pointing at `where` — never a
  silent O(n·m)); `c? in src on …` is the null-padded side; **null join
  keys never match** (deliberate asymmetry with grouping, documented at
  both). Output preserves probe-side order, stable. [FC5–FC7]

### S14.2 Verbs and windows*

- **S14.2.1*** The verb surface is generic over row-oriented data —
  DataFrame, arrays of maps, element children, RDB rows — *one relational
  algebra over both documents and tables*, same names, same semantics,
  Rosetta-tested both ways. [PD15]
- **S14.2.2*** Column references in verb arguments are `~.field` (the pipe
  current-item reference extended into verb scope; the leading-dot form is
  not adopted). `over(part:, order:)` is a postfix construct on
  window-function calls only. [PD13, PD14]

### S14.3 Streams and laziness*

- **S14.3.1*** `input()` is eager, `stream()` lazy — symmetric over the same
  source specifiers. **Laziness is carried by the data, never by the
  operator**: `|>` and `for` are unchanged; stream in → the stage is
  recorded onto a plan; terminals force. Plan construction never performs
  I/O and never errors. [PD9, PD10]
- **S14.3.2*** Two stream kinds: value-backed streams are true values
  (re-forcible, usable in `fn`); live-I/O streams are one-shot resources,
  `pn`-only. `fn` stages are fusible by verified purity; `pn` stages are
  plan barriers. Stream faults surface as `T^E` at the forcing point. [PD10–PD12]
- **S14.3.3*** Handles are stream sources/sinks (`stream(h)`,
  `send_to(h)`); a forced pipeline is an implicit task scope — early
  termination cancels upstream. WHATWG Web Streams are the committed compat
  target; legacy Node streams are best-effort. [K21, K26, K28]

---

## S15 Metaprogramming

- **S15.1** Lambda is homoiconic through elements: the canonical AST is an
  element tree in the ambient **`lm.` namespace** (`<lm.if …>`, `<lm.add …>`)
  — namespaced because AST tags must be distinguishable from the world's
  documents (HTML has a real `<var>`). Quoted-symbol tags (`<'if' …>`) are
  general grammar orthogonality. [C9a]
- **S15.2** Element literals are inverted quasiquotation: expression children
  evaluate (splice); element-literal children are structure (quoted). *The
  angle bracket is the quote mark.* A `quote { … }` authoring form is
  deferred, next in line. [C9, C9a]
- **S15.3*** `input(f, 'lambda')` parses Lambda source into the `lm.` AST;
  `compile(ast, env?) fn^` compiles a **closed** function — stdlib plus
  explicitly passed bindings, never ambient scope; deterministic and pure.
  **Strings are never code**: `compile` accepts AST values only; no
  string-eval API exists in any form. Constructed functions take identity
  from their normalized-AST hash (S5.5.1). [C9]
- **S15.4** `name(item)` is the shadow-proof accessor for intrinsic names
  (element tag, function name, type name); `null` for unnamed values.
  Operators and functions, not properties, are the reliable surface over
  open containers. [C9a]

---

## Appendix A — Implementation Footnotes

Status of `*`-marked rulings as of 2026-08-17. Conformance plans:
[`Lambda_Impl_Error_Handling (done).md`](../vibe/Lambda_Impl_Error_Handling%20(done).md),
[`Lambda_Impl_Int_Total (done).md`](../vibe/Lambda_Impl_Int_Total%20(done).md).

| Ruling | Status |
|---|---|
| S4.8.1 | Float printer is not yet shortest-round-trip (`0.1 + 0.2` prints `0.3`). |
| S5.3.1 | `ArrayNum ==` is representation-sensitive in known cases — ruled a bug; also gates the data-processing engines (P0/FC8). |
| S5.4.3 | Element `==` defect (map-cast layout bug) — priority fix in the C8.5 bug list. |
| S5.5.1 | Function self-equality defect open; normalized-AST hash awaits `compile()` (S15.3). |
| S6.1.1 | `fn_lt` uses `strcmp` (NUL-unsafe) and accepts symbols; two-layer invalid-comparison treatment not landed. |
| S6.2.1 | `sort()` coerces to float (`sort(["b","a","c"])` → `[nan,nan,nan]`); total order not implemented in `sort`/`order by`. |
| S7.1.1 | OOB-read→null / write→raise / clamping partially landed; slice-offset rules (RF3D) landed with regression tests. |
| S7.2.2–S7.2.4 | `last` keyword, `limit last N`, and `{limit:}/{last:}` options not implemented; ArrayNum negative-index audit outstanding. |
| S7.3.1 | Strict null propagation + `skip_null` option pending. |
| S7.4.4 | Skip-edge errors currently surface the bare `ITEM_ERROR` singleton — rich payload pending. |
| S7.6.1 | The one- and two-arm postfix handler grammar and MIR lowering conform to S7.6.1v3/S7.6.2v3/S7.6.6v2, including nested `^`/`~` scope restoration and direct raised-`pn` outcome routing. System-fault capture remains incomplete. |
| S7.6.5 | Retired `^err` destructuring and prefix `^expr` error tests are removed from the grammar, AST/runtime, and active `.ls` corpus. Remaining open work is system-fault capture for braced handlers (S7.6.7). |
| S7.6.7 | May-suspend handler rejection: predicate machinery exists but silently degrades instead of diagnosing. |
| S7.7.1–S7.7.6 | TE-18 declaration-boundary skip pending (routing, case-7 tiers, edge sites). `for x: T in e` does not parse yet — case 1 is `let`/`var`-only until the grammar is extended. |
| S7.8.1 | TE-17 lane gating pending (predicates exist, gate does not). Known violation V1: `fn_array_set` silently despecializes a declared `int[]` — the dominance invariant (S7.7.2) is false today. The `may_defect` effect split must land before routing or every unanalyzed call costs a native lane. |
| S7.10.5 | RF5 audit: several vectorized ops return generic arrays where typed `ArrayNum` is required; a few error-channel violations open (`query`, `url_resolve`, invalid `push`/`splice`). |
| S7.11.4 | Exec recovery implemented on POSIX. **Blocking hazard H1**: batch mode overwrites the stack-overflow handler, so fault capture differs between batch and standalone runs. Windows SEH never exercised. |
| S8.2.2 | `at` membership still tests the whole key space (`1 at [10,20,30]` returns true); iteration conforms. |
| S8.3.2 | Streams (and hence stream `len`) not implemented. |
| S9.1.3, S9.1.4, S9.2.2–S9.2.4 | COW Stage 1 landed (`let`-finality real for Array/Map/Object/Element/VMap). Stage 2 pending: `var`-param grammar + exclusivity checks (all four faces), capture-assignment compile errors, view-borrow confinement, module-`var` rule, snapshot iteration. |
| S10.2.2, S10.2.3 | `eq ne lt le gt ge` operators and the `vec_cmp` revert not landed; mask-consumption functions deferred. |
| S11.1.1 | Array-pattern composition unbuilt; `is [T]` inline parse crash open. |
| S11.2.3 | Match exhaustiveness checking unverified in the implementation. |
| S11.4.3 | `any \ error` has no working surface spelling (the `!` exclusion operator is broken for general types); it exists as the unwritten default only. |
| S11.4.5 | Landed check implements the superseded type-directional reject: an ANY-held `3.0` into an `int` boundary errors instead of admitting as `3`. Round-2 deliverable #1. |
| S11.4.6 | Constrained-type `is`/`fn_is`/validator divergence open; base-only interim is the shipped behavior. |
| S12.4.1–S12.4.3 | Resource model R1–R5 designed, not implemented. |
| S13.1.3 | Tasks fully implemented (2026-07-15); the worker tier (thread/process isolation) is pending — process first, thread gated on the isolate-state audit and open item O-D. |
| S13.4.1, S13.4.2 | Pairwise reductions decided, not implemented (sequenced before concurrency work); stream parallelism pending with streams. |
| S14.2, S14.3 | Group-by and joins (S14.1) are implemented; verbs, `over(...)`, DataFrame, and the whole stream/plan system are pending (phases P3–P8). |
| S15.3 | `compile()`, closed environments, and `quote` unimplemented; C9 grammar worklist open (general expression children). |
| int v5 (S4.1) | Substantially landed (lane, encoding, saturation, printing, goldens). Residue: `INT64_ERROR` collides with `INT_LANE_INF` (pre-cutover gate unsatisfied); ELEM_INT SIMD kernels partly gated; nullable lane (`INT_LANE_NULL`) partial; `IntLane`/ValueRep typing of the four i64 meanings pending (known silent bug class). |

## Appendix B — Open Design Issues

Numbered `SO#` (semantics-open) for stable reference; each links to its
record. (The prefix is the spec's own — distinct from the historic review
findings B1–B13 cited as `[B#]`, and from the `OI-#` ledger in
`vibe/Lambda_Issues_Outstanding.md`.)

**Numerics**
- **SO1** Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` stays in the machine lane; this spec (S4.5.3, per Int_Type §2.2) says it leaves the lane — `3i8 div 0i8` needs an explicit call, and Number_Model needs a supersession note.
- **SO2** Int v5 §5 details: poison-algebra table ratification; finiteness-proof dataflow home; formal ruling on the 56-bit packing (de facto shipped) and the freed encoding octant; migration gates. [Int_Type §5]
- **SO3** The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; the `int | null` ABI for `index_of`/`ord` needs restating under the nullable lane.
- **SO4** Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs a sweep.

**Errors and enforcement**
- **SO5** TE-17 transitivity: does discharging `(int | error)[]` re-narrow in place, or only by copy? Copy is the safe default. [TE-17 §Open]
- **SO6** Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); where containment materializes under deferred evaluation.
- **SO7** TE-5 R5 sticky `any`; validator schema-`any` uniformity.
- **SO8** Should `is` become value-aware? Deliberately undecided (S11.3.1 records the intentional asymmetry).
- **SO9** A surface spelling for `any \ error` (the `!` exclusion operator route is broken); closed named-map opt-in; constrained-type predicate enforcement; checked-cast surface (`as`/`as?`); generics; flow-sensitive narrowing — all out of scope or unowned.
- **SO10** A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) — real question, future design (S7.9.3).
- **SO11** Whether a non-null scalar iterates once (`len(5)`): `for (i in 5)` yields nothing while `5 |> ~` yields one item; the S8.3.1 law requires them to agree before `len(5)` is settled.
- **SO12** `for (k, v at c)` paired form does not parse; whether it should exist is open.

**Values, COW, resources**
- **SO13** COW granularity on large documents: node representation for spine-copying, refcount discipline for unique-path in-place update, and the gating benchmark. [C4.3]
- **SO14** Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or guaranteed get-modify-put — no owner document yet. [C4.4]
- **SO15** Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping); module-`var`-as-borrow final rule (forbid vs dynamic bit).
- **SO16** Close-error routing (double fault): proposed — normal-exit close failure becomes the `pn`'s error; on error exit the original wins, close error attached suppressed. To confirm. [Features §3.5.2]
- **SO17** Resource-carrying-type containment rules (when a wrapping value is itself resource-typed). [R3]
- **SO18** Snapshot iteration to be formally recorded (C4.2d) when implemented.
- **SO19** Upward/lateral document axes must be path-carrying or zippers, never parent pointers — needs a design note before someone hacks pointers in.

**Concurrency**
- **SO20** O-D: cross-isolate lifetime for shared graph Items (promote-on-share recommended) — must precede thread-mode workers.
- **SO21** `select` surface syntax; a first-class static `handle` type.
- **SO22** Deferred opt-ins: blocking send, true selective receive, `unordered` streams, CPU-bound cancellation safepoints, Kahan accuracy modes, `sync` JS export annotation.

**Data processing**
- **SO23** PD4 join column-collision suffixes; PD5 `over(...)` scope on the generic engine; PD6 the exact `~`-binding spec for verbs/`on:`/`over`; PD7 relational `join()` vs string `join()` naming; PD16 window-in-for-clause form (proposed, unconfirmed).
- **SO24** PD12 sub-items: `on error` resume semantics (abort vs skip-record), handler scoping over multiple forced streams, interaction with cleanup.
- **SO25** Deferred group-by vocabulary: `having`-style filter, post-group `let`, extended aggregates, full/right/semi/anti joins, as-of join, pivot/melt.

**Sys funcs and surface**
- **SO26** RF6 mutator convention: updated-owner vs unit; and `splice`'s public result (owner / unit / removed members).
- **SO27** Whether debug logging inside `fn` is a permitted non-observable effect — the purity boundary's one undefined edge; best pre-decided before users ask. [Features §3.6]
- **SO28** Effect polymorphism (pure-iff-argument-pure HOFs) — dodged by convention; Flix-style Boolean effect polymorphism is the recorded minimal fix.
- **SO29** File write/append syntax (C6a: `into`/`onto` candidates); string interpolation syntax (note the `$` collision with quote splices); a set type; `assert`/`expect` unification.
- **SO30** Empty JSON keys (`{"": 1}` currently corrupts on round-trip) — map keys from data need a defined answer given solid symbols.
- **SO31** The `<file>` element shape (name/size/mime, content as child) — pin with file-I/O spec.
- **SO32** Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns.
- **SO33** A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in assignment position — document or delete.
- **SO34** `emit()` vs `send()` — two event vocabularies coexist; state the boundary explicitly.

## Appendix C — Decision-Record Index

| Section | Records | Where argued |
|---|---|---|
| S1 principles | C1–C17 distilled; Features §3.6 | `Lambda_Semantics_Formal.md`, `Lambda_Semantics_Features.md` |
| S2 value domain | C1, C1.6a, C2, C8.6-R | `Lambda_Semantics_Formal.md` |
| S3 truthiness | C2, C17 | ibid.; `Lambda_Semantics_Formal2.md` |
| S4 numerics | C3, C13, C14b/c, C16, C17; int v5 | `Lambda_Semantics_Formal2.md`, `Lambda_Semantics_Int_Type.md`, `Lambda_Semantics_Number_Model.md` |
| S5 equality | C8, C8.5, C8.5a, C8.6, C8.6-R, C8.7, C9-4 | `Lambda_Semantics_Formal2.md`, `Lambda_Expr_Eq.md` (rationale only) |
| S6 ordering | C11, C11.4, C11.5 | `Lambda_Semantics_Formal2.md` |
| S7 absence/errors | C5, C5.3, C14, C14a, C15, C15a/b; TE-4, TE-9, TE-13, TE-15–TE-18; RF1–RF6; ER-D1–PD13 | `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Sys_Func.md`, `Lambda_Design_Exec_Recovery.md` |
| S8 membership | C5.3a; §8.0–8.3 records | `Lambda_Semantics_Formal2.md` |
| S9 mutability | C4, C4.2a/b/c, C4.3, C12; CW16–CW20 | `Lambda_Semantics_Formal.md`, `Lambda_Design_Runtime_COW.md` |
| S10 operators | C6, C6.2–C6.4, C10 | `Lambda_Semantics_Formal2.md` |
| S11 types | C7, C8.5c; TE-1–TE-18 | ibid.; `Lambda_Design_Type_Enforcement.md` |
| S12 effects/resources | Features §3.5–3.7; Procedural; Function_Arg | `Lambda_Semantics_Features.md`, `Lambda_Procedural.md`, `Lambda_Proc_Assignment.md`, `Lambda_Design_Function_Arg.md` |
| S13 concurrency | K11–K32 | `Lambda_Design_Concurrency.md` |
| S14 data processing | PD9–PD16; FC1–FC11 | `Lambda_Design_Data_Processing.md`, `Lambda_Expr_For_Clauses2.md` |
| S15 metaprogramming | C9, C9a | `Lambda_Semantics_Formal2.md` |

The decision records preserve the full deliberations — every alternative that
lost and the arguments that did not persuade. This specification is their
distillation: the record governs the history; this document governs the
language.
