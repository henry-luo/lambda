# Lambda Formal Semantics — Specification

**Spec version:** 22.0.0 (2026-09-05)

**Status:** normative — the single source of truth for Lambda language semantics.
This document records what Lambda's semantics **is by decision**, not what any
build implements. Where any other document — including the `vibe/` design
records — or the implementation disagrees, this specification wins; the design
records govern the history and preserve the full deliberations.

**Ruling IDs.** Every ruling carries a section-path ID: `S4.6.2` is the second
ruling of §4.6. A revised ruling keeps its ID with a version suffix
(`S4.6.2v2`), replacing its predecessor in place; superseded text is not
carried. The spec itself uses semantic versioning: MAJOR — an existing ruling
changed meaning, **or** added rulings break existing programs; MINOR —
rulings added compatibly; PATCH — editorial.

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
REH-D1–D14
([`Lambda_Design_Runtime_Error_Handling.md`](../vibe/Lambda_Design_Runtime_Error_Handling.md));
K11–K32 ([`Lambda_Design_Concurrency.md`](../vibe/Lambda_Design_Concurrency.md));
PD9–PD16 / FC1–FC11
([`Lambda_Design_Data_Processing.md`](../vibe/Lambda_Design_Data_Processing.md),
[`Lambda_Expr_For_Clauses2.md`](../vibe/Lambda_Expr_For_Clauses2.md));
RF1–RF6 ([`Lambda_Design_Sys_Func.md`](../vibe/Lambda_Design_Sys_Func.md));
R1–R5 and the effect doctrine
([`Lambda_Semantics_Features.md`](../vibe/Lambda_Semantics_Features.md));
C4/CW ([`Lambda_Design_Runtime_COW.md`](../vibe/Lambda_Design_Runtime_COW.md));
surface syntax
([`Lambda_Design_Syntax.md`](../vibe/Lambda_Design_Syntax.md));
OB1–OB22 ([`Lambda_Type_Object.md`](../vibe/Lambda_Type_Object.md)).
PTH1v2, PTH2v2, PTH3–PTH15, PTH16v3, PTH17–PTH29
([`Lambda_Type_Path.md`](../vibe/Lambda_Type_Path.md)).
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
  mutability is a property of bindings, marked by `var`. **Lambda script has
  no global mutable state** — `var` is a procedural binding, so every mutable
  root is owned by a `pn` activation, a view/template instance, or an object
  reachable from one (S9.1.7). [C4, RG14]
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

- **S1.11 — References, not authorities.** When a system function's semantics
  are otherwise under-determined, resolve them by consulting **ECMAScript
  first, Python second**. Both are references, not authorities: Lambda departs
  from either whenever a Lambda principle (S1.1–S1.10), an existing Lambda
  ruling, or internal consistency with a sibling operation says otherwise, and
  the departure is then recorded as a ruling rather than left implicit. A
  *hosted* language keeps its own specification — LambdaJS follows ECMAScript
  and the Python guest follows CPython regardless of what Lambda chose for the
  same-named Lambda builtin. Two orderings are settled by this: closing an
  under-determined edge case beats inventing one, and matching a sibling
  Lambda operation beats matching the reference. [S17]

- **SI1 — Boxing invisibility.** Representation choices — tagging, unboxed
  arrays, decimal width, lane selection — never affect results. [S1.6]
- **SI2 — COW unobservability.** Sharing until first mutation is
  undetectable; `let`-finality holds absolutely; reference identity is not
  observable — `===` compares node identity carried as data, never whether
  two bindings share storage (S5.1.4v2). [S9.1, S5.1.4v2]
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
- **SI11 — Total reads, checked writes.** Invalid member reads never raise —
  they yield `null`; slices clamp. The corresponding invalid member write
  always raises a hard language error. [S7.1]
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

- **S2.1.1v3** Scalars: `null`, `bool`, `int`, `integer`, `i64`, `u64`,
  sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, `float`/`f64`,
  `decimal`, `string`, `symbol` (with `path` as a special symbol), `binary`,
  `datetime` (with `date`/`time` sub-kinds). Containers: `range`, `list`,
  `array` (transparently unboxed numeric variants), `map`, `element` (a list of
  children *and* a map of attributes). First-class: `function`, `type`,
  `error`. **`object` is a nominal container, not a container kind**: a map,
  array, or element whose type is nominal (S2.1.4). `A is object` asks whether
  A carries a nominal type and is orthogonal to the structural test, so a
  nominal map is both `map` and `object`. `object` remains a valid type and a
  valid order band (S6.2.1). This is deliberately **not** the OOP sense in
  which everything is an object: a structural value is not one. `entity` is
  not a type — the word is retired from the keyword table. [OB1, OB2, OB13]
- **S2.1.2** `number` is a declared union only; `type()` never returns it, an
  alias, or a storage-tier name. Aliases in, canon out. [NM §2.6]
- **S2.1.3v2*** An object type `type T { … }` declares a structure of exactly
  one structural kind — map, array, or element — plus methods and
  constraints, each optional; a content pattern is legal only when that kind
  admits content. Its literal is the element form `<T a: 1, "child">` under
  S16.9.3's two-regime commas, whatever the kind. A derived type
  (`type U : T`) has T's kind — **inheritance never changes the base kind** —
  merges attributes with its base, and its content pattern **replaces** the
  base's. When formatted to a markup format the type name is the tag. Input
  without a schema yields structural values only; input with a schema —
  `input(doc, schema: …)`, or a document that declares its own schema —
  yields the same maps and elements **stamped** with the declared nominal
  types. Structural elements never carry methods. [OB2, OB3, OB7–OB9, OB14,
  OB17]
- **S2.1.4*** **A nominal instance is sealed to its type and open in its
  fields.** Three parts. (1) The instance's binding to its nominal type is
  sealed: it changes only by instance type alteration (S2.1.5). (2) The
  nominal type itself is sealed during evaluation: it does not change while a
  program runs. (3) The instance's layout follows its type — declared fields
  at their declared positions — but the instance is **open by default** and
  may hold fields the type does not declare. Extending an instance with an
  extra field is an ordinary S9.1.6 member addition and a shape transition,
  and every shape so reached **shares the one nominal record**: the value
  remains an instance of its type, its methods still resolve, and the
  declared prefix keeps its layout. SO9's closed-shape opt-in remains the
  reserved route to a type that forbids extras. [OB15, OB16]
- **S2.1.5*** **Instance type alteration is reconstruction.** A future
  operation may give an existing instance a different nominal type; it builds
  the instance anew under the new type, and is the only way the S2.1.4(1)
  binding changes. It acts on one instance. Altering a **type** itself —
  which would touch every instance — is a separate matter, not ruled, and
  would relax S2.1.4(2). Neither exists today (SO42, SO43). [OB18]

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

### S2.4 Paths

- **S2.4.1v2*** Path values use dotted steps. `/` selects the logical global
  reference root and `.` the active relative reference root: a resolver may
  qualify `/.a.b` as `file.hostname.a.b` (projected as `/a/b`) or as
  `http.hostname.a.b`; `.a.b` remains relative to the active base. `~~` is the
  parent step, never a root: `.~~.a` is parent-relative and `.~~.~~.a` applies
  two parent steps. The retired `/a`, `..a`, and compound `_..` spellings do
  not exist. Quoted, wildcard, and dynamic steps retain their ordinary
  dotted/indexed forms. [PTH1v2, PTH2v2, PTH3–PTH4, PTH11, PTH17]
- **S2.4.2v4*** Every hierarchical reference is a typed root plus ordered
  operations. Ordinary keys are `NameKey` or non-negative `IntKey`; a dynamic
  subscript is evaluated and normalized through S8.2.1v3 before lookup.
  A key-domain mismatch is an invalid member access: its read yields `null`
  under S7.1.1v2 and its write raises through the hard `T^` channel under
  S7.1.3v2/S7.4.2. It never implies container conversion. Root, parent, and
  wildcard navigation are operation kinds, so `a.1.b` and `a.'1'.b` remain
  distinct. Postfix root `./` discards descendant steps back to the logical or
  provider/authority anchor; unresolved relative root selection remains as
  `./`. Parent steps apply left-to-right: they remove a preceding child,
  accumulate at the relative root, and clamp at an anchored root. Equality,
  hashing, printing, target resolution, and
  `base ++ relative_suffix` observe the same normalization. [S1.6, S8.2.1v3,
  PTH7–PTH9, PTH12–PTH14, PTH25, PTH28]
- **S2.4.3v3*** Paths, names, symbols, and member expressions use this one
  reference scheme but retain distinct evaluation contracts. Paths are
  static root-selected plans and produce lazy target handles; names are
  statically namespace-qualified (`a` may become `ns.a`) and read bindings;
  symbols are static `NameKey` reference values and do not implicitly read
  bindings.
  Member/index expressions apply typed keys to a runtime base and are dynamic.
  Name-position parsing is maximal: once an element tag or attribute name has
  the namespace-qualified `ns.name` form, the complete dotted name is consumed
  before element content is considered, and whitespace does not terminate it.
  Thus `<svg.rect>` and `<svg .rect>` name the same qualified tag. A relative
  path child needs no boundary at all: S16.9.4 respells the relative path
  `\.a.b`, so `.rect` can no longer be a path and `<svg \.rect>` is
  unambiguously tag `svg` with a path child. It takes **no comma** — the
  S16.9.3 boundary comma is a biconditional and this element has no
  attributes; `<svg, \.rect>` is an error, and the former `<svg; .rect>`
  spelling is retired with the divider `;`.
  Static specialization and generic dynamic lookup must be semantically
  identical; the scheme introduces no mutable reference identity. [S1.6,
  S5.1.4v2, S8.2.2v2, S9.1.5v2, S16.9.3, S16.9.4, PTH13–PTH15, PTH16v3, PTH20]
- **S2.4.4*** Each evaluation's immutable resolver deterministically maps
  logical `/` prefixes, namespaces, and provider aliases to qualified roots.
  Resolution obeys lexical visibility, exports, sandboxing, and capabilities;
  it never uses mutable process-global bindings or existence/failure-based
  provider fallback. Address resolution performs no I/O; forcing an external
  target is a separate operation with its declared effect/error contract.
  [S1.10, PTH16v3, PTH17–PTH19]
- **S2.4.5v2*** Paths have three root forms: rooted `/.a.b`, relative `.a.b`,
  and absolute `SchemeName...`. Rooted paths qualify logical `/` through the
  active resolver; absolute paths name their provider/authority directly and
  bypass that mount. File absolutes use `file./.a.b` for `/a/b` on the current
  machine and `file.hostname.a.b` for `/a/b` on a named machine; consequently
  `file.a.b` selects host `a` and path key `b`. A registered scheme name
  heading a dotted chain is reserved as an absolute-path root. Rooted and
  absolute path values retain distinct root kinds even when they resolve to
  the same qualified target. All three forms use the same typed operations.
  [S5.1.4v2, PTH21–PTH28]

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
  (Supersedes the v4/C16 float64-representable domain — argument in
  [Int_Type §3](../vibe/Lambda_Semantics_Int_Type.md).)
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
- **S4.1.4** `int ∥ i64`: poison has no `i64` home; every finite int is an
  `i64` value. Narrowing into `int` is band-membership — former sparse
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
  the smallest exact home — `int` if in band, else `i64`, else `decimal` —
  never silently in float. *Literals are strict; data always fits.* [C3, C16]

### S4.4 Promotion lattice

- **S4.4.1** One subsumption principle: `T1 ⊑ T2` iff every T1 value embeds
  **exactly** into T2. Chains: `i8 ⊑ i16 ⊑ i32 ⊑ int ⊑ i64 ⊑ integer ⊑
  decimal` (with `int ∥ i64` per S4.1.4 — finite ints embed, poison does
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
  always; BigInt ⇄ `integer` losslessly; `i64`/`u64` → BigInt; a guest
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
- **S5.1.4v2*** `==` is the only **value** equality and compares content
  alone. **Identity is data, not a reference**: a container loaded from or
  created within an addressable document may carry a universal node
  identity — conceptually the document path plus the node's id within it,
  one scheme across local and online documents. `===` compares that identity
  and never content; an operand without identity compares `===`-false,
  never equal to anything. `==`, hashing, and the total order ignore
  identity. No `ref_eq` or address comparison exists — reference identity
  (sharing versus copying) remains unobservable (S1.6, S9.1.2). Which
  operations preserve an identity and how it is carried are open (SO39).
  [C4, C8, OB10]

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
- **S5.4.2v3*** A nominal value equals only a value of the **same nominal
  type**, whatever its structural kind; a structural value never equals a
  nominal one, even with identical fields. Sameness of type is identity of
  the shared nominal record (S2.1.4), never equality of the type's name, so
  two modules' `Point`s stay distinct and an imported type stays itself.
  Given the same type, equality is structural over the **full** key set —
  declared fields and any extras alike — as an unordered map, then content
  ordered where the kind has content (the S5.4.3 shape). Methods belong to
  the type and identity is not content (S5.1.4v2), so neither takes part.
  [OB4, OB10, OB16, OB19]
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
- **S6.2.2v3** The total order **totally refines `==`** — equal values always
  tie; ties resolve by stability. Numbers order by mathematical value with no
  representation ranks. Within-band: strings/symbols/binaries bytewise UTF-8
  (no locale collation); sequences lexicographic; maps via canonically sorted
  keys; elements by tag, attributes, children; objects — nominal values of
  ANY structural kind, the object band being orthogonal to the structural
  kinds (S2.1.1v3) — by type name, then attributes, then content.* [C11.4,
  OB4, OB13]
- **S6.2.3** Sort is stable; `desc` is **full reversal** — one pure order, no
  pinning exceptions. [C11.4]

---

## S7 Absence and Errors

*Total reads, checked writes, deliberate failures.*

### S7.1 Reads are total; writes are checked

- **S7.1.1v2*** Every invalid member/index **read yields `null`**. This
  includes out-of-bounds or negative sequence/string positions, missing named
  members, a key outside the base's domain (`array["name"]`, `array[5.5]`,
  `map[1]`), and a non-error base with no applicable member face. No invalid
  read raises or returns an error value. Null propagates through chained access
  (`data.users[5].name` → null end-to-end) and scalar arithmetic
  (`null + 1` → null). `arr[i] or default` is the coalescing idiom. [C5,
  C5.3b]
- **S7.1.2** Slices **clamp** to bounds and return an empty collection (or
  `""` for string results — the result type is preserved). Clamping is
  symmetric: a negative offset clamps to 0, never wraps from the end
  (`slice("hello", -2, 3)` = `"hel"`). An absent source *or absent offset*
  makes the whole selection `null` — a missed search degrades end-to-end
  instead of returning a plausible wrong prefix. A non-null non-integral
  offset is an `error`. [C5, RF3D]
- **S7.1.3v2*** Every invalid member/index **write raises a hard language
  error** through the `T^` channel of S7.4.2 — never a soft `T | error` value,
  null, or a silent no-op. This includes the same bounds, key-domain, and base
  failures covered by S7.1.1v2. *Reads ask a question; writes issue a command,
  and a command that silently does nothing hides bugs.* Growth is explicit
  (`push`/`splice`). [C5, C5.3b]

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

- **S7.6.1v4*** **`e ^ { … ^ … }` handles the error locally and is
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
  analysis: *a binding's static type is never a lie* (SI14).

  Ordinary error outcomes always reach this handler by normal return and a
  local generated branch; the handler never uses non-local recovery for a
  returned or raised language error. In statement position,
  `pn_call() ^ { error_body }` may protect a procedure call that suspends: its
  completion and handler destination are durable state, and the body executes
  after resume with the same single-evaluation and fresh-outcome rules.
  [TE-16, REH-D8, REH-D12]
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
  `if (^err)` do not exist (why: the TE-13/TE-16 record); the test is spelled
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
- **S7.6.7v3*** A statement-position procedure handler
  `pn_call() ^ { error_body }` may protect a possibly-suspending call. No
  `LambdaRecoveryFrame`, native frame address, or jump buffer survives a
  scheduler yield. Ordinary errors are stored as the call's durable completion
  and branch to the handler when the caller state machine resumes. If an S7.11
  native system fault occurs after suspension, the task fault boundary may use
  the temporary non-local carve-out, but it must materialize the fault as a
  durable completion and resume the nearest active procedural handler state;
  subsequent propagation is again frame-by-frame. A value-producing postfix
  handler over a possibly-suspending `pn` remains a compile error because `pn`
  handlers are statement-only. [TE-16, ER-D13, REH-D12, REH-D13]

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

- **S7.11.1v2** Fault reasons are a closed, typed native set: stack overflow,
  side-stack exhaustion, out-of-memory, and runtime boundary defect. Faults
  never enter function types and are never a normal call-result ABI —
  recursion never forces `T^stack_overflow`. Structural-equality depth
  exhaustion is instead a language-visible ordinary error and propagates by
  explicit completion through each frame. [ER-D4, ER-D9, D1.4v3]
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
- **S8.1.2v2** On elements and objects, `in` ranges over attribute values
  then children; `at` ranges over attribute keys — one meaning across the
  map/list duality. [OB4]
- **S8.1.3*** **Axis and arity are independent.** The axis (`in`/`at`) selects
  WHICH members are walked; the arity of the binding selects the PROJECTION
  over those same members. One name binds the axis's own element — `for (x in
  c)` a value, `for (k at c)` a name. Two names bind `(key, value)` of each
  walked member, on either axis: `for (k, v in c)` and `for (k, v at c)`.
  The axis still decides membership, so the paired forms are not synonyms —
  on an element `for (k, v at e)` yields attribute pairs only, while
  `for (k, v in e)` also walks children; on an array `for (k, v at a)` is
  empty because an `IntKey` is not a name (S8.2.2v2). S8.1.1's mirror law is
  unaffected: it equates the single-name iteration with membership on the same
  axis, and the paired form walks that identical member set. [C5.3a]

### S8.2 The key space

- **S8.2.1v3*** Every container has a fixed key domain. The sequence face of
  arrays, lists, and ranges uses non-negative `IntKey`s. An `int`, `float`, or
  `decimal` subscript normalizes to an `IntKey` only when its value is finite,
  mathematically integral, exact in the index integer domain, and
  non-negative: `a[5]`, `a[5.0]`, and `a[5.00n]` select the same member.
  Fractional values such as `5.5`, negative integral positions, strings, and
  symbols do not name sequence members. The named face of a map accepts
  string and symbol subscripts as `NameKey`s; the empty string is a valid,
  distinct name rather than absence. An element or an object exposes both
  faces: an `IntKey` selects a content child, while a `NameKey` selects an
  attribute.
  Any subscript not admitted by the selected container face is an invalid
  member access: the read yields `null`, while the write raises a hard error
  under S7.1.3v2/S7.4.2. On an object a `NameKey` subscript is the dynamic
  form of dot — `obj["m"]` resolves exactly as `obj.m` does (S12.3.3v2),
  reaching the type's methods before yielding `null`.
  `for (k, v in c)` exposes the resulting canonical key uniformly
  (`[for (k, v in [10,20]) k]` → `[0, 1]`). [C5.3b]
- **S8.2.2v2*** A *name* is a `NameKey`: string and symbol subscripts with the
  same exact contents normalize to the same name, including the empty name.
  `at` ranges over names, so
  `1 at [10, 20, 30]` is **false** — the narrower reading is what lets
  `for (k at e)` give an element's attributes without its children; an index
  bound is written `i < len(arr)`. [C5.3b; §8.0.1 record]
- **S8.2.3*** **Methods are members of the type, never of the value.** An
  object's key domain is its attributes and content; its methods live on the
  type value `T`. Everything that walks the key domain — `in`, `at`,
  `for … in`/`for … at`, `len`, `keys`/`values`/`names`, `==`, the total
  order, formatting — sees attributes and content only: `"m" at obj` is
  false even when `obj.m` resolves a method, exactly as `"sum" at {a: 1}`
  is false today while `{a: 1}.sum()` calls the builtin. `"m" at T` may be
  true. Member access — `obj.m` and its dynamic form `obj["m"]`, one
  operation — resolves beyond the key domain (S12.3.3v2); membership and
  iteration do not. [OB4, OB5]

### S8.3 `len`

- **S8.3.1v2** The law: **`len(x)` is the number of iterations `for (i in x)`
  performs.** Consequences, not separate rules: `len("str")` = 3;
  `len([[1,2],[3]])` = **2** (shallow — the count indexing needs);
  `len({a: null, b: 2})` = 2; `len(null)` = 0 (absence is the empty
  sequence); `len(err)` = **error** (iterating an error yields an error, not
  nothing — collapsing it onto 0 would make a failed computation
  indistinguishable from an empty one); `len(<e a:1, b:2, "t">)` = **3**
  (attributes + children); `len(<Point x: 1, "t">)` = **2** for an object
  no matter how many methods `Point` declares (S8.2.3). [§8.1 record, OB4]
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
construct; `let` is final; there is no global mutable state.* [C4, RG14]

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
  is a compile error. State lives in a `pn` activation, a view/template
  instance, or an object instance reachable from one of those; never inside a
  function value, and **never at module scope** (S9.1.7). This clause
  previously named "module-level `var`s" first — stale wording inherited from
  the C4 record, corrected 2026-08-28. (A non-escaping
  nested `pn` used only in call position may later be allowed direct access
  — designed, deferred.)
- **S9.1.5v2** No reference cells; structural `==` is the only value
  equality (`===` compares identity data, S5.1.4v2, and creates no
  reference). The value model is a tree: cycles are unconstructible, so
  `==` is total and no cycle collector is needed; a cross-reference is
  data — an identity or a key resolved through its document, the way a
  foreign key resolves through its table. [OB10]
- **S9.1.6*** Member/index assignment may update or add only a member admitted
  by the container's existing key domain; it never changes the container kind.
  Thus `var a = []; a["str"] = 1` raises under S7.1.3v2 and `a` remains an array
  rather than being promoted to a map or object. Changing kinds requires
  whole-binding replacement with a newly constructed value, for example
  `var a = [1]; a = {value: 1}`. Such reassignment remains subject to the
  binding's declared type; an unannotated `var` may change runtime type under
  S12.2.1. [S8.2.1v3, S9.1.2, C5.3b]

- **S9.1.7** **No global mutable state.** A mutable binding cannot be declared
  at module scope: there is no script-level global variable. Every mutable root
  is owned by one `pn` activation, one view/template instance, or one object
  instance reachable from such a root. The transitive path is closed too — a
  module-level `let` holding a container or object cannot be written through
  (S9.1.1). This is not a convention; it follows from the scope model, since
  only `fn`/`pn` bodies, `while` bodies and event handlers open a procedural
  scope. [RG14]

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
- **S9.2.4v2*** A **view-state** `var` may not be passed as a `var` argument
  (no call-site check can see the callee's independent path to the same
  storage). The original ruling also named module-level `var`; that half is
  **vacuous by construction under S9.1.7 / RG14** — Lambda has no global
  mutable state, so no module-level `var` exists to pass. Ruled out by design,
  not left unimplemented. View state remains a real mutable binding outside any
  `pn`, so the rule stands for it alone. [CW §11.4, RG14]

### S9.3 Construction captures values

- **S9.3.1*** Placing a value into a container captures it **by value** at
  every constructor and insertion point — literals, field/index writes,
  `push`/`splice`. After construction, container and source are independent.
  A corollary of "values never alias", and what makes cycles
  unconstructible. Porting hazard (silent): *fill-after-storing* leaves the
  stored copy empty — fill before storing, or read-modify-write. [C4]

*Replacement idiom for reference sharing — the **handle store**: one container
owns each record, every other reference is a key into it, the store travels as
one `var` parameter. Identity that aliasing supplied implicitly becomes data.
Not a ruling; see [C4.2e](../vibe/Lambda_Semantics_Formal.md) and
[`Lambda_Procedural.md`](Lambda_Procedural.md).*

---

## S10 Operators

### S10.1 Union, pipe, and filter

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
- **S10.1.5** `that` is the **filter**: `c that p` keeps the members of `c`
  for which `p` is truthy. It binds `~` per member and `~#` to the key/index
  on the same discipline as the mapping pipe (S10.1.2), scoped by S10.1.3, and
  it sits at the **pipe precedence tier**, left-associative, so filters and
  pipes chain left to right (`c |> f that p`). Its result spreads into an
  enclosing array literal exactly as a pipe or `for` does. A scalar filters as
  a one-member collection; an empty result is absent (`null`) by the ordinary
  collection rule, not an empty container. Filtering a map tests its **values**
  and yields an array — the keys are dropped (SO38). The filter was spelled
  `where` before it was renamed to remove the ambiguity with the `for`-header
  clause; that spelling is retired (S10.3.1v2).
  [Grammar_Reduce2 appendix]

### S10.2 Vectorization

- **S10.2.1** Arithmetic `+ - * /` is vectorized (element-wise with
  broadcasting) — vector arithmetic is mathematics. [C10]
- **S10.2.2*** Bare comparisons `< <= > >=` are **scalar-only**, never
  element-wise (a mask is a container, containers are truthy — exhibit in
  the C10 record). Element-wise
  comparison has its own keyword operators **`eq ne lt le gt ge`**, yielding
  bool masks; nan lanes compare false. Bare `==` is untouched. [C10]
- **S10.2.3*** Mask consumption is explicit and non-magical: `sum(mask)`
  counts true lanes; `a[mask]` is boolean indexing. `and`/`or` remain scalar
  short-circuit operators, never mask combinators. Condition-position lints
  protect the truthiness boundary (suggest `any`/`all`/`len`). [C10]

### S10.3 Keyword operators

- **S10.3.1v2** `and or not is in to div that at eq ne lt le gt ge` — Lambda is
  a keyword-operator language; new operators prefer words over sigils. `where`
  is **not** among them: binary `where` was retired in favour of `that`
  (S10.1.5), and `where` survives only as a `for`-header clause word. A `where`
  in infix position is the retired spelling and is a compile error naming
  `that`, never a silent reinterpretation. [Grammar_Reduce2 appendix]

### S10.4 Parent navigation

- **S10.4.1*** Postfix `.~~` is the parent-navigation step at the ordinary
  member/index precedence tier; it chains left-to-right (`value.~~.~~.name`).
  A value domain or active traversal context supplies the parent relation;
  absence of one yields `null` and chains by S7.1.1v2. Path values use S2.4.2v4.
  A field named `.parent` remains an ordinary member, not a syntactic alias.
  [PTH3, PTH5, PTH9]
- **S10.4.2*** Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is
  bound, selects the innermost current-value context, and counts as a free
  `~` for the S10.1.2 mapping-pipe test. Thus `~~.~~.a` means
  `~.~~.~~.a`; it never denotes a relative path, whose form starts with `.`.
  [S10.1.2,
  S10.1.3, PTH6]
- **S10.4.3v2*** Contextual parent navigation is occurrence-based and
  carries lineage in the evaluation context as a navigation path, cursor, or
  zipper. It never adds parent pointers to Lambda containers or document
  values; node identity (S5.1.4v2) is data a document assigns, not lineage,
  and a document resolves a node's parent from it. [S1.4, S1.6, S9.1, S9.3,
  PTH10, OB10]

### S10.5 Root navigation

- **S10.5.1*** `/` is the one root-selection operation. Initial `/` selects
  the logical root of the implicit resolution universe; postfix `./` selects
  the root of its explicit base. The postfix form has ordinary member/index
  precedence and chains left-to-right (`value./.name`, `value.~~./.name`).
  [S1.7, PTH25–PTH26]
- **S10.5.2*** For a path, `./` selects its logical or provider/authority
  anchor. For a traversal occurrence it selects the outermost occurrence from
  the active navigation path/zipper; a declared root-aware model supplies its
  own root; a standalone hierarchical value is its own root. Absence of a root
  relation yields `null` and chains by S7.1.1v2. [PTH26–PTH28]
- **S10.5.3v2*** Dynamic root navigation is occurrence-based. Its lineage
  lives in the evaluation context as a navigation path, cursor, or zipper; it
  never adds root/parent pointers to Lambda values, and node identity
  (S5.1.4v2) is not lineage. [S1.4, S1.6, S9.1, S9.3, PTH29, OB10]

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
  character-range member as a one-codepoint string. [S7.1.1v2, S11.2.1]

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

- **S11.3.1v2** `is` is structural for maps/arrays/elements (extra fields
  permitted; key lookup by name) and nominal for object types: `A is T` for a
  nominal `T` holds when A's nominal record is T's or derives from it, and
  `A is object` holds when A carries any nominal record. The two axes are
  independent — a nominal map satisfies both `is map` and `is object`
  (S2.1.1v3). `is` is type-directional: `3.0 is int` is false even though a deferred `int`
  boundary admits `3.0` (S11.4.5) — membership asks what a value *is*; the
  boundary asks what it may *satisfy*. [C7, TE-6, OB13]

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
  (`T that …`) enforce the base only, for now. [TE-10]
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
- **S12.1.4*** **Effect polymorphism, admitted narrowly.** A function may
  take its colour from an argument rather than declaring one. `call` (S12.3.4)
  is the first and, for now, only such function: `call(f, args)` is `fn` when
  `f` is `fn` and `pn` when `f` is `pn`. The colour is resolved **statically
  whenever `f`'s is statically known**, and checked at **run time** otherwise
  — a dynamic callee is exactly the case where the bit cannot be read off the
  source. This is the minimal instance of SO28's pure-iff-argument-pure
  polymorphism; it does not generalize to user-declared signatures, which
  still carry one declared bit each (S12.1.1).

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
- **S12.3.3v2*** **Member access is resolution, not membership.** `x.name`
  and its dynamic form `x["name"]` are one operation: resolve the receiver's
  key domain (an attribute by name), then the receiver's type — its own
  methods, then the base chain — and stop at the first hit; nothing found is
  `null` (S7.1.1v2). The **member-call form** `x.name(...)` adds one more
  tier after those: a method-eligible builtin. So a member shadows a system
  function of the same name — a map field or object method called `sum` wins
  over the built-in `sum()` — without this, every builtin name would be a
  silent latent trap in user types. The builtin tier is a call-site rule
  only: bare `x.sum` on a map with no such field is `null`, never a bound
  builtin, so probing a map by key stays safe. S8.2.3 keeps membership and
  iteration on the key domain. A bare `x.m` naming an `fn` method is a
  **bound function value**: a closure
  whose environment is the receiver, captured by value (S9.3.1), equal to
  another bound method iff same definition site and `==` receivers
  (S5.5.1), and callable through `call` (S12.3.4) with the receiver already
  supplied. A `pn` method is **call-only**: taking it as a value is a
  compile error, for S12.3.2's reason — a value span carries no writable
  location, so a detached `pn` could only mutate its own copy. The
  shadow-proof accessor for intrinsic names remains `name(item)` (S15.4).
  [OB5, OB6]

- **S12.3.4*** **`call(f, args)` is the dynamic-application form.** It
  applies `f` to the members of the array `args` as individual arguments,
  and is the sanctioned way to forward a collected argument list — notably
  `fn outer(...) => call(inner, varg())`, which nothing else expresses when
  `inner` is itself variadic. Its colour follows `f` (S12.1.4). Three
  consequences follow from its being dynamic by construction, and are
  accepted rather than worked around: arity is checked at run time, not
  statically (S12.3.1 still bounds the callee's own slots); the result type
  is `any`; and the call takes the dynamic ABI, not a direct-call fast path.
  A `pn` target reached from `fn` context is an error — statically when `f`'s
  colour is known, at run time otherwise. Error convention follows the
  resolved colour: an `fn`-coloured `call` **returns** an error value, a
  `pn`-coloured `call` **raises**. `args` must be an array; any other type is
  an error.
- **S12.3.5** **Spread splices into containers, never into an argument
  list; the spelling follows the container's shape.** Positional containers
  take the bare operator — `[*a, 3]`, `(*a, 3)`. **Keyed** containers take it
  in key position — `{*: m, w: 5}` and `<div *: attrs, id: "x">` — since a key
  slot needs a key, and `*` is S16.8.6's any-key of the unit family. In
  argument position `*x` passes its operand as one value; the expansion was
  considered and **rejected** — argument in
  [LR02-R10](../vibe/Lambda_Issue_Ledger.md), `call` covers forwarding
  generally.

- **S12.3.6** **No arity overloading for user definitions.** Two definitions
  sharing a name in one scope are a duplicate-definition error regardless of
  parameter count; a name binds to exactly one function (ECMAScript per
  S1.11). Optional parameters are the sanctioned alternative: `pn f(a)` and
  `pn f(a, b)` are one `pn f(a, b?)`. The builtin registry's `(name, arity)`
  keying is a dispatch optimization, never a language rule — builtins are not
  source-overloadable either. Full rationale in
  [TS-8](../vibe/Lambda_Issue_Ledger.md). [S1.11, TS-8]

- **S12.3.7*** **User definitions shadow system functions — user-first,
  module-lexical, warned.** A module-level `fn`/`pn` or value binding whose
  name matches a system function shadows it for every call site **in that
  module/script only**, resolved statically; resolution is never global
  (no JS-style prototype or global mutation). Every such shadowing draws a
  **compile warning**. A shadowing definition exports like any other:
  `pub` extends it to an importing script through the explicit import,
  never ambiently. A non-callable shadow (`let sum = 5`) makes `sum(x)` the
  ordinary not-callable error — never a fallback to the builtin; the effect
  bit follows the actual callee (S12.1). The reserved core stays intrinsic
  by S16.10.1: keywords and base-type words cannot be binding names, so
  `int()`, `string()`, `type()` are un-shadowable. *User-first is the
  forward-compatibility rule: a new sys func never changes an existing
  program.* The alternative — collision as a compile error — was rejected:
  maximum silent-capture protection, but it freezes the stdlib namespace;
  the warning recovers the protection (argument in Design_Syntax §7.25).
  Reaching a shadowed builtin from inside the shadowing module is
  deliberately unspecified (SO37). [S1.11, S12.3.3v2, Design_Syntax §7.25]

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

*One procedure, two tiers. Explicit concurrency enters a program through one
ordinary call surface.* Full record: [`Lambda_Design_Concurrency.md`](../vibe/Lambda_Design_Concurrency.md) (K11–K32).

### S13.1 Tasks and workers

- **S13.1.1v2** `start` is a builtin `pn`, not a keyword. It uses ordinary call
  grammar as `start(target, args = [], options = {})`, is legal only inside a
  `pn`, and requires `target` to resolve to a `pn`. `args` is an array; the
  compiler-recognized `options` literal accepts `mode: 'task' | 'thread' |
  'process'` and defaults to `'task'`. Everything else (`wait`, `send`,
  `receive`, `select`, `cancel`, `self`) is also a builtin `pn`.
  **`async` and `await` do not exist.** [K12v2]
- **S13.1.2v2** Calls are **colorless**: `f(x)` synchronously yields the value
  and may suspend invisibly (`f(x)` ≡ `wait(start(f, [x]))` minus the handle);
  may-suspend-ness is inferred and never observable. Every Lambda `pn`
  exposed to JS is uniformly Promise-returning; a Lambda resume is a
  macrotask. [K16]
- **S13.1.3v2*** Two tiers, one handle vocabulary: tasks
  (`start(f, [x])`, shared context) and isolated workers
  (`start(f, [x], {mode: 'thread' | 'process'})`, share-nothing isolate).
  Handles are uniform:
  awaitable, sendable-to, selectable, cancellable; they compare by identity
  and only the concurrency builtins operate on them. [K11, K31v2]
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

## S16 Surface Syntax

Syntax is ruled here because it decides meaning: separation, brace role, and
element scope each select *which program* a text denotes. A dedicated formal
syntax document may follow; until it does, this section is the single source.
Argued in [`Lambda_Design_Syntax.md`](../vibe/Lambda_Design_Syntax.md), cited
below by its section.

### S16.1 Whitespace and separation

- **S16.1.1*** **Line breaks carry no meaning.** Replacing any line break in
  an accepted program with a space yields the same program with the same
  semantics. The converse direction may *reject* — inserting a line break can
  make a valid program a syntax error (S16.2.3) — but never reinterprets one.
  [Design_Syntax §3.1]
- **S16.1.2*** `;` is a **strict separator** between statements — never a
  terminator, and `,` obeys the same discipline in every list. A separator
  sits between two items: trailing separators (`{ a; b; }`, `[1, 2,]`,
  `f(a, b,)`) and empty slots (`{ a; ; b }`, `[1, , 2]`) are syntax errors.
  A block's value remains its last expression; no separator can discard it.
  [Design_Syntax §3.2]
- **S16.1.3v2*** Adjacent statements need **no separator** when the second
  begins with a token that cannot continue the first, **or when the first
  has a closed tail**. A tail is closed after the structural closer of a
  non-postfixable construct — `fn`/`pn`/`type`/`view` bodies, braced
  `if`/`for`/`while`, `match` — or a self-complete keyword statement
  (`import`): *after a block, never `;`*. Open tails, which do require the
  S16.2.3 repair, are `let`/`var`/assignment, type aliases, `=>` bodies,
  `return`, and every bare expression (primaries take postfix). Declarations
  are not expressions, so `(fn () {})[1]` is impossible by construction.
  [Design_Syntax §3.2–3.3, §7.14]

### S16.2 Line-start classification

- **S16.2.1*** An **incomplete** expression continues across a line break
  unconditionally: a trailing operator, an unclosed bracket, or a keyword
  form awaiting its remainder binds the next line. [Design_Syntax §3.2]
- **S16.2.2v2*** After a **complete** expression, a line-start token that can
  only continue an expression does continue it: `|> | & ! ? .? ** ++ % > =`,
  `== != <= >=`, and the word operators `and or to in is at div that eq ne
  lt le ge gt`, plus `else case default`. These are unambiguous in any
  position and need no separator. `!` is here, not below: it is a pure
  infix token (S16.8.1). [Design_Syntax §3.3, §7.1]
- **S16.2.3v2*** After an **open-tail** statement (S16.1.3v2), a line-start
  **dual-role** token — one that could either continue the expression or
  start a new one — is a **syntax error**. The set is final:
  `( [ - + * ^ / < .` — the whole arithmetic family `- + * /` is banned
  uniformly rather than freeing `+` alone (S16.8.5). Neither reading wins by
  default; the repair is explicit (`;` to separate, or move the token to the
  end of the previous line to continue). This is what makes S16.1.1 hold:
  the parser never guesses, so a line break can never silently split or
  merge. [Design_Syntax §3.1, §3.3]
- **S16.2.4v2*** One carve-out: `.` `ident` at line start is **member
  continuation**, sanctioning full leading-dot fluent chains — widened from
  `.ident(` calls once the relative path was respelled `\.a.b` (S16.9.4),
  which vacated the ambiguity. `.` is sub-classified, not retired: `.digit`
  stays dual-role, since `a.5` is member access with an integer field.
  [Design_Syntax §3.4, §7.15]
- **S16.2.5*** `return` followed by a line break and a start token returns
  that value: `return` ⏎ `42` is `return 42`. A bare return is `return`
  followed by a separator or the closing brace. The JS restricted-production
  trap is fixed by inversion, not by a special rule. [Design_Syntax §3.3]

- **S16.2.6*** A handler's brace opens **on the same line as its `^`**
  (`expr ^ { ... }`). A trailing `^` followed by a line-start `{` is the
  propagate-versus-handler ambiguity, and the same-line requirement is what
  resolves it. [Design_Syntax §3.6]
### S16.3 Juxtaposition

- **S16.3.1*** Juxtaposition **sequences, never combines.** Adjacent
  expressions are separate statements or content items; no juxtaposed form
  denotes a value computed from its neighbours. The general `expr expr expr`
  construct (application, unit suffixes) is therefore permanently
  unavailable — S16.1.3 claims that syntax space. Constructs needing operand
  adjacency must use an introducer keyword, a sigil, or explicit delimiters.
  [Design_Syntax §3.8]

### S16.4 Braces

- **S16.4.1v3*** **Interior decides, wherever braces are an expression.** A
  map needs a key and `':'` at the front — a name, or one balanced `[expr]`
  group (S16.8.9) — and neither `ident ':'` nor `[…] ':'` occurs in
  statement space — Lambda has no labels — so the two interiors are
  **disjoint grammars**: `{a: 1}` and `{[k]: 1}` are maps, `{let x = 1 x}`,
  `{f(1)}` and `{[1, 2]}` are blocks. This is a decidable bounded lookahead
  (one balanced group, then a colon), not a guess, and it holds in every
  expression position **and in control-form bodies of both spellings**.
  Grouping parens never flip the reading: `if c {a: 1}` and `if (c) {a: 1}`
  are the same program, as grouping parens must be. Block expressions
  therefore exist — `{statements}` is legal in any expression position, its
  value is its last expression, and its `let`s are block-scoped
  (`let x = { let y = 1 y + 1 }`), which is what gives arrow functions block
  bodies without a JS-style `({...})` quirk. [Design_Syntax §5.9]
- **S16.4.2** **Empty `{}` resolves by context, and only where a tie
  exists.** Value position — initializers, call arguments, operands, in `fn`
  *and* `pn` — is the empty **map**. Content position is an empty map item,
  which is meaningful because it serializes. `if`/`for` bodies take **fn
  context → empty map, pn context → empty block**, aligning with value use:
  fn control bodies produce values, pn control bodies discard them. Arrow
  bodies are fn context by definition, even written inside a `pn`. A bare
  `{}` **statement** in `pn` is a syntax error — dead code under either
  reading — which removes the context rule from statement position entirely.
  [Design_Syntax §5.9]
- **S16.4.3** **Declaration braces are structural and never read as maps.**
  `fn`, `pn`, `view`, and `on` bodies, braced match arms (`case T { ... }`),
  handler arms (`^ { ... }`, `~ { ... }`), and `while` bodies are always
  blocks; `{}` in each is the empty body. `while` belongs here by the same
  value-use principle — it is procedural-only, so its body value is always
  discarded and a map body is dead by construction. Each declaration has an
  expression escape where a value body is wanted (`fn f() => {a: 1}`,
  `case int: {a: 1}`). `match`'s outer braces delimit the arm list
  (S16.6.4); `type` bodies have their own interior; map-*type* patterns
  (`{a: int}`) live in type space and are untouched. The dividing test
  between S16.4.1v3 and this ruling: interior decides exactly where a
  bare-expression second form exists; the brace is structural exactly
  where it is mandatory — handler arms have no unbraced form, since
  `expr ^ expr` is already propagate-then-continue (S16.2.6).
  [Design_Syntax §5.9]

  | Braces | Reading |
  |---|---|
  | value/expression position, call args, initializers (`fn` **and** `pn`) | map or block by interior; `{}` = map |
  | `if`/`for` bodies (both spellings), `else`, colon-form match arms, arrow bodies | map or block by interior; `{}` by fn/pn context (arrows: always fn) |
  | `fn` `pn` `view` `on` bodies, braced match arms, handler arms, `while` bodies | always block; `{}` = empty body |
  | `match` outer braces | arm list (structural) |
  | `type` bodies | fields/constraint/methods interior |
  | content position | map item (`{}` = empty map item, meaningful) |
  | bare statement position in `pn` | **error** (dead either way) |
  | type-annotation `{...}` | type space, unaffected |

### S16.5 Element scope

- **S16.5.1*** Inside an element — attribute values and bare content
  expressions — `< > <= >=` **are not operators.** `>` always terminates the
  element, `<` always opens a child. A comparison there is written as a
  parenthesized island (`attr: (a > b)`), inside which the full expression
  grammar returns, or with the keyword operators, which are element-wise by
  S10.2.2 and agree with the symbol forms on scalars only. Removing the
  reading, rather than ranking two readings, is what keeps S16.1.1 true at
  the markup boundary. [Design_Syntax §5.10]

### S16.6 Control forms

- **S16.6.1*** `if`, `for`, and `while` each have **one node with two
  spellings**: parenthesized head with any-expression body
  (`if (c) e`), or bare head with braced body (`if c { … }`). There is no
  separate statement form; the expression/statement distinction is semantic,
  not syntactic. [Design_Syntax §5.1]
- **S16.6.2*** `(` immediately after `if` or `while` **commits** to the
  parenthesized spelling. Consequently a bare head must not begin with `(`:
  `if (a+b)*2 { … }` is a syntax error, repaired as `if ((a+b)*2) { … }`.
  `for` is unaffected — loop declarations begin with an identifier.
  [Design_Syntax §5.2]
- **S16.6.3*** `else` is **optional** in both spellings. An absent else
  yields `null` in value position and contributes nothing in content
  position. A dangling `else` binds to the nearest `if`. [Design_Syntax §5.4]
- **S16.6.4*** `match` keeps its single braced form: its braces delimit an
  arm list, not a body, so no parenthesized spelling exists.
  [Design_Syntax §5.5]
- **S16.6.5*** The expression/statement distinction is enforced by semantic
  analysis on the S12.1 effect boundary, not by grammar: `break`/`continue`
  and `while` are procedural-only; a `for` is a comprehension in functional
  context and an effect loop in procedural context. [Design_Syntax §5.6,
  S12.1]
- **S16.6.6*** Control statements require braces. `return`, `break`, and
  `continue` are statements and are admitted **only inside a braced body**.
  Every unbraced control body — the paren-form `if`/`for` body, an `else`
  body, a `case T:` arm, and every `=>` arrow body — is an expression
  position and rejects them (`raise` is an expression and remains valid
  there). A braced block in any of those positions is the statement spelling
  and admits them: `if (c) { return x }`, `case T: { return x }`. Each
  rejection names the repair; the two grounds are argued in Design_Syntax
  §6 point 35. [Design_Syntax §6 point 35]
- **S16.6.7*** A procedure has exactly **one body form**: the braced
  statement block `pn name(...) { ... }`. `=>` bodies are fn-only, named or
  anonymous — an expression-bodied procedure is redundant with `fn` plus
  S16.6.6, and the reference grammar never accepted it. [Design_Syntax §6
  point 36]
- **S16.6.8*** A **procedural block is a statement, never an expression**.
  A braced block whose top level contains a pn-only construct (`return`,
  `break`, `continue`, `var`, assignment) is rejected in every expression
  position: after `case T:`, in tuple/argument/operand position, as an `=>`
  arrow body — an expression must produce a value, and a procedural block
  may not (probe and grounds in Design_Syntax §6 point 37).
  Functional blocks (`{ 1; 2 }`, `{ let r = f(x); g(r) }`) and maps remain
  expressions everywhere, so `case T: { … }` stays legal and — by this very
  ruling — can never conceal a statement: after `:`, braces are a map or a
  pure block, nothing else. Classification is by interior, extending
  S16.4.1v3's doctrine from brace-disambiguation to statement-ness.
  [Design_Syntax §6 point 37]
- **S16.6.9*** **Branch homogeneity.** An `if`/`else` chain or `match` is
  either a **value form** — every branch an expression, where functional
  blocks, maps, and diverging `raise` arms all count as expressions — or a
  **control form** — every branch a procedural block, the whole form a
  statement that yields no value and is illegal in value position. Mixtures
  (`if (c) { return 1 } else 0`, a braced-statement `case` arm beside a
  `default: expr` arm) are rejected. Match surface reading: `:` arms are
  value arms, braced arms are control arms. The fn/pn classification rides
  the S12.1 effect boundary and lives in semantic analysis per S16.6.5.
  [Design_Syntax §6 point 38]

### S16.7 Script top level

- **S16.7.1** **A script's top level is element content, not a list.** The
  statement sequence forming a script body is modelled as the content of a
  virtual `<file …>` / `<script …>` element. This is why the two share a
  syntax: top-level juxtaposition, separation, and line-start classification
  (S16.1–S16.3) are the element-content rules applied to the file. The mental
  model is the normative one — a script *is* content, so it normalizes like
  content (S16.7.2, S16.7.3) rather than accumulating like a list.
  [Design_Syntax §7.23]
- **S16.7.2** **Null is stripped from content.** A `null` reaching content
  contributes nothing, however it arose — written literally, read from a
  missing key (S7.1.1v2), or produced by an `else`-less `if` (S16.6.3). If
  stripping leaves the content empty, the script's value is a single `null`;
  that residual is the only null observable at top level. Containers do not
  normalize: `[1, null, 2]` keeps its null, so a null is observed by placing
  it in a value (`let r = [s.b]`), never by writing it as a bare statement.
- **S16.7.3** **Adjacent strings are merged.** Two string items with no
  intervening non-string content collapse into a single text node. Merging is
  applied *after* null stripping, so `"a" ⏎ null ⏎ "b"` yields `"ab"` — the
  stripped null does not keep its neighbours apart.

### S16.8 Lexical forms

- **S16.8.1** **`not` is the one logical negation.** Unary `!` is removed
  from value expressions; `!` keeps its type-level roles (infix exclusion,
  complement) and is therefore a pure infix token (S16.2.2v2).
  [Design_Syntax §7.1]
- **S16.8.2** **`not` binds loose** — below comparisons and `is`/`in`/`at`,
  above `and`/`or`: `not a == b` ≡ `not (a == b)`, the Python placement.
  [Design_Syntax §7.2]
- **S16.8.3** **Numeric spelling.** Sized floats accept integer spellings
  (`1f32`, symmetric with `1i32`); `_` is a digit separator in every numeric
  family, hex included, and is spelling only; **hex is the only radix
  prefix** — `0b`/`0o` are rejected. [Design_Syntax §7.3–§7.5]
- **S16.8.4*** **No implicit adjacent-literal concatenation**, strings or
  symbols: `"a" "b"` never combines into one value (S16.3.1). Distinct and
  kept is content normalization, where adjacent string *items* merge into
  one text node (S16.7.3) — document construction, not expression-level
  concatenation. Explicit concatenation is `++`. [Design_Syntax §7.9]
- **S16.8.5** **Unary `+` is kept** (identity, plus string→number
  coercion), and `+` stays banned at line start: the arithmetic family
  `- + * /` is banned as a class, not per token. [Design_Syntax §7.12]
- **S16.8.6** **`*` is spread; `*` and `...` are two wildcard families,
  not one.** `*` is the unit wildcard (path segment, any-key, `T*`
  repetition, spread); `...` is the elided run (pattern gap, rest
  parameters), with the normative equivalence `...` ≡ `any*`. Paths keep
  `*`/`**` — an ellipsis would collide with path dots. [Design_Syntax §7.10]
- **S16.8.7** **A single-quoted literal is a symbol, not a string**, and
  comma decomposition (`let a, b = expr`) is by design; bracket destructuring
  patterns are rejected. [Design_Syntax §7.8]

- **S16.8.8*** **The backtick syntax space is reserved and must not be spent
  otherwise.** String interpolation is deferred with its direction fixed: if
  built, `` `...` `` is a quoted-DSL mechanism — interpolated text being one
  instance — never plain string interpolation. [Design_Syntax §7.13]
- **S16.8.9*** **Computed keys: `[expr]: val`.** A map literal or element
  attribute list admits an entry whose key is a bracketed expression, chosen
  for symmetry with the dynamic read `m[expr]` (S12.3.3v2): `{[k]: v}`
  defines what `m[k]` reads. `expr` must yield a name (string or symbol,
  S8.2.2v2); any other value is an error, as the literal `{1: v}` is — the
  key domain is unchanged and a computed key never makes a `VMap`. Entries
  evaluate left to right and a later entry wins, as with `*: m` spread; a
  literal holding a computed key builds its shape at run time exactly as a
  spread-holding literal does. `(expr): val` is rejected: grouping parens
  must stay inert (S16.4.1v3). The keyed for-splice `{for … [k]: v}` is not
  ruled. [Design_Syntax §7.26]

### S16.9 Declarations, elements, paths

- **S16.9.1** **`pub` is a uniform prefix modifier** — `pub let` / `pub fn`
  / `pub type`. Bare `pub x = 1` is removed; `pub var` stays illegal by
  non-composition. [Design_Syntax §7.6]
- **S16.9.2*** The **`apply;` fused token is retired**: bare `apply` is the
  keyword statement, disambiguated from `apply(...)` by the S16.2.5 pattern,
  and any neighbouring `;` is ordinary separation. [Design_Syntax §7.7]
- **S16.9.3** **`;` has exactly one role language-wide: statement
  separation.** `,` takes over inside elements and object types, under the
  two-regime doctrine — **pair-lists are strict comma lists** (maps,
  attributes, named arguments, parameters, fields/methods: the comma is
  always required, so `{a: b c: d}` and `<div a:1 b:2>` are both rejected),
  while **content and statements juxtapose**. The element attribute/content
  boundary comma is a **biconditional**: present exactly when the element
  has both attributes and content. `<div "text">` and `<div a:1>` take none;
  `<div a:1, "text">` requires one; `<div a:1 "text">` and `<div, "text">`
  are errors. This retires the language's last optional delimiter, so
  S16.1.2 has no exception. [Design_Syntax §7.11]
- **S16.9.4*** **The relative path is spelled `\.a.b`** (rooted `/.a.b`
  unchanged): `\` already carries path flavour from its import-separator
  role, and unlike `./a.b` it does not collide with S10.5.1's postfix root
  step. Vacating `.name` is what widens S16.2.4v2. `import .mod` is
  unaffected — its keyword introducer leaves no ambiguity to escape.
  [Design_Syntax §7.15]
- **S16.9.5** **`a?: T` marks an optional field** — the whole field may be
  absent — which is distinct from `a: T?`, where the field is present and
  its value nullable. The marker applies in every type-field position:
  object-type fields, pattern position, and map-type items.*
  [Design_Syntax §7.22]

### S16.10 Keywords as names

- **S16.10.1v2*** **Keywords never name bindings — where they could
  capture.** A word is barred as a binding name when it can **begin a
  construct**: declaration and statement keywords (`let` `var` `fn` `pn`
  `type` `view` `edit` `if` `for` `while` `match` `return` `import` `apply`
  `not` `last`, …), base-type words, and the named values. The bar covers
  `let`/`var` names, parameters, `fn`/`pn`/`type`/`view` declaration names,
  and import aliases (`import edit: …` declares a binding and is rejected);
  the rejection is a compile error at the declaration site (E201). Words
  that can never begin a construct stay **legal** as binding names:
  for-header clause words (`order` `by` `group` `into` `limit` `offset`
  `asc` `desc` `where` `as`), infix word operators (`and` `or` `to`
  `is` `in` `at` `div` `that` `eq` `ne` `lt` `le` `ge` `gt`), and the
  continuation-only words `else` `case` `default` (S16.2.2v2) together with
  `on`. **Where both readings fit, the clause wins** — an enclosing `if`,
  `match`, `for`, or view claims its clause word before an expression is
  parsed, so `let default = 4` then `if (false) 1 else default` reads the
  clause `else` and the binding `default`. A word legal as a binding must
  also **read** as one in expression position: accepting a declaration whose
  every use fails is the defect this ruling exists to remove. *Leaving clause
  words bindable is the same forward-compatibility rule S12.3.7 applies to the
  library: a new clause word must not capture a name existing programs already
  bind, just as a new sys func must not.* **There is no
  quoted escape**: `import 'edit': …` is rejected too — at a use site
  `'edit'.x` is a symbol member expression, and symbols never implicitly
  read bindings (S2.4.3). Two words are barred by **reservation rather than
  capture**: `state`, a view-signature clause today but held for a possible
  standalone word (`expr is state`), and the namespace root `lambda`, so the
  `lambda.sys.*` escape of S17.2.2 can never itself be shadowed. The barred
  and allowed words are enumerated in
  [Design_Syntax Appendix K](../vibe/Lambda_Design_Syntax.md).
  [Design_Syntax §7.24]
- **S16.10.2*** **Data names admit keywords.** Container name positions —
  map keys, element tags, and attribute names — accept keywords:
  `{type: 1}`, `<if a:1, "x">`, `<div class:"a">` are legal. Definition and
  use are both sigil-guarded (after `{` `<` `,` before `:`; after `.`), so
  no keyword construct can begin there and no capture arises. Advisory, not
  enforced: prefer the quoted-symbol spelling (`<'if' …>`, `{'type': 1}`)
  where a bare keyword would read as its construct; quoted-symbol tags
  remain general grammar orthogonality (S15.1).

  **A declaration's own name is a binding; the members it introduces are
  not** — and this covers **methods as well as fields**. In
  `type T { a: int, fn f() {} }`, only `T` is a binding and takes
  S16.10.1v2's bar; `a` and `f` are data names, so both may be keywords, and
  both are reached through a receiver (`x.a`, `x.f()`) under S16.10.3 rather
  than spoken bare. A method is therefore never a shadow of anything
  (S12.3.7 governs module bindings only), and renaming one is an API change
  to its call sites. [Design_Syntax §7.24]
- **S16.10.3*** **Member steps admit keywords.** After `.`, a keyword is an
  ordinary member name: `m.type`, `x.if`, `v.int` read data members —
  including in line-start member continuation (S16.2.4v2). Subscripts are
  expression space, not name space: `a["type"]` is a string key and `last`
  keeps its S7.2.2 subscript meaning. [Design_Syntax §7.24]

---

## S17 System Library

Per-builtin semantics that the general rules above do not already fix. S1.11
governs how an under-determined case here is resolved.

### S17.1 String splitting

- **S17.1.1** **`split` follows ECMAScript `String.prototype.split`.** The
  delimiter may be a string or a pattern, and both spellings obey one rule set:
  a match consumes its span and opens a new segment; the segment before the
  first match and after the last are both emitted, so a leading or trailing
  delimiter yields an empty string at that end (`split(",a,b", ",")` is
  `["", "a", "b"]`, `split("a1b1", \(d))` is `["a", "b", ""]`); a subject with
  no match yields a one-element result holding the whole subject. In
  particular Lambda adopts ECMAScript's `e == p` rule: **a match whose end
  lands on the current segment's start contributes no segment and only
  advances the search** — this is what suppresses the leading and trailing
  empties of a zero-width delimiter (`split("ab", \(d*))` is `["a", "b"]`).
  A zero-width advance steps a whole codepoint, never a byte. An **empty
  subject** yields `[]` when the delimiter matches the empty string and
  `[""]` otherwise (`split("", ",")` is `[""]`, `split("", \(d*))` is `[]`).
  The Python-shaped whitespace form `split(str, null)` — runs of whitespace,
  outer whitespace stripped — has no ECMAScript analogue and is retained.
  Deliberation in the
  [C18 record](../vibe/Lambda_Semantics_Formal2.md). [S1.11, LR09-8]

### S17.2 The system-function namespace

- **S17.2.1*** **System functions live at `lambda.sys.*`, and the prelude
  imports them unqualified.** `len`, `sum`, `print` and their siblings are
  members of one built-in module; an implicit prelude import makes them
  available with no qualification, so ordinary code is unchanged. The
  qualified spelling addresses the same registry row — there is one owner of
  the builtin name list, not a parallel table. [D7.2.4]
- **S17.2.2*** **`lambda.sys.f` is the escape from a shadow.** Where a module
  shadows a system function under S12.3.7, the qualified spelling still
  reaches the builtin: `let sum = 5` leaves `lambda.sys.sum(xs)` working.
  This closes SO37 and needs no new syntax — it is an ordinary member access
  on an imported module (S12.3.3v2). The **`lambda` namespace root is
  reserved**: it may not name a binding (S16.10.1v2), so the escape can
  never itself be captured — unlike Python's shadowable `builtins`.
  Qualification is unnecessary for the reserved core (`int`, `string`,
  `type`), which S16.10.1v2 already makes un-shadowable. [S12.3.7, D7.2.4]

---

## Appendix A — Implementation Footnotes

Status of `*`-marked rulings as of 2026-08-24. Conformance plans:
[`Lambda_Impl_Error_Handling (done).md`](../vibe/impl/Lambda_Impl_Error_Handling%20(done).md),
[`Lambda_Impl_Error_Rework.md`](../vibe/impl/Lambda_Impl_Error_Rework.md),
[`Lambda_Impl_Int_Total (done).md`](../vibe/impl/Lambda_Impl_Int_Total%20(done).md).

| Ruling | Status |
|---|---|
| S2.4.1v2, S2.4.2v4, S2.4.3v3–S2.4.4, S2.4.5v2, S10.4.1–S10.4.3, S10.5.1–S10.5.3 | Implemented for the current path/name scope on 2026-08-19, with the S2.4.3v3 spelling re-verified on 2026-08-28: maximal namespace-qualified element/attribute names, the undelimited relative-path element child `<svg \.rect>` (no `;`, no comma), logical `/.a`, relative `\.a`, absolute `file./.a`/`file.host.a`/`http.host.a`, root `./`, parent `.~~`, contextual `~~`, typed key operations, and interpreter/MIR Direct occurrence carriers. The default resolver qualifies logical roots to local `file./`; generalized immutable mount tables, remote transport, network hostname discovery, and complete S8.2.1v3 key normalization remain deferred. |
| S4.8.1 | Float printer is not yet shortest-round-trip (`0.1 + 0.2` prints `0.3`). |
| S5.3.1 | `ArrayNum ==` is representation-sensitive in known cases — ruled a bug; also gates the data-processing engines (P0/FC8). |
| S5.4.3 | Element `==` defect (map-cast layout bug) — priority fix in the C8.5 bug list. |
| S5.5.1 | Function self-equality defect open; normalized-AST hash awaits `compile()` (S15.3). |
| S8.3.1v2 (element arm) | **Conformant as of 2026-09-03 (USER ruling).** `fn_len`'s element arm and the JIT's `fn_len_e` both answer attribute count plus content-item count, so `len(<e a:1, b:2, "t">)` is 3 and equals what `for (x in e)` walks — attribute VALUES first, then content items. Structural and nominal elements no longer disagree, and `len_iter_law.ls` pins the law. The child axis it displaced is now spelled **`content(e)`** (2026-09-04, USER): a read-only ARRAY VIEW that shadow-copies the element's items pointer and length without copying the slots, so `len(content(e))` is the child count and `content(e)[i]` the child walk. See [LR09-9](../vibe/Lambda_Issue_Ledger.md) for the ruling, the rejected `e.content` / `size(e)` spellings, and the migration. Baseline 4090/4090, GC stress 93/93. |
| S8.2.3, S12.3.3v2 | **Conformant as of 2026-09-03**, with one deliberate substitution. `lambda_object_member` is the single resolver for both member lanes — the ANY lane (`fn_member`) and the static lane (`item_attr`), which previously diverged: a bare `obj.m` bound on one and read `null` on the other. It resolves the key domain, then the type's own methods, then the base chain; `lambda_object_find_method` is the one walk. A bare `fn` method now yields a receiver-captured closure on both tiers (T0 binds through the new `interp_bind_object_method` seam, since an un-JITted method has no `compiled_fn`), and `obj["m"]` resolves identically to `obj.m`. `len`, `in`, `at` and the projections were already key-domain-only and are unchanged. **Gap:** S12.3.3v2 rules a bare `pn` method reference a compile error; it is not rejected at all today. The rejection cannot live in the runtime member lane — MIR lowers a `pn` method *call* by lowering its callee through that same lane, so refusing there makes the call a silent no-op on the JIT tier. It needs an AST flag plus a validation point in build_ast, which alone can tell a bare reference from a sanctioned callee. Tracked as [LR02-18](../vibe/Lambda_Issue_Ledger.md). Fixtures: `test/lambda/object_method_value.ls`, `object_method_receiver.ls`, `proc/object_method_write.ls`; baseline 4082/4082. |
| S2.1.1v3, S2.1.3v2, S2.1.4, S2.1.5, S5.4.2v3, S6.2.2v3, S11.3.1v2 | **Conformant as of 2026-09-03.** Nominal-ness is a property of the type descriptor, carried by a `TypeNominal` record allocated once per declaration and cached by an `is_nominal` base flag (D2.6.6v2, D2.6.11). A nominal value wears its DECLARED structural kind, so an attribute-only type yields maps and a type with a content pattern yields elements, and `is object` / `is map` read as the independent axes S2.1.1v3 rules. Nominal sameness is record identity rather than name equality, so two modules' `Point`s stay distinct while every shape grown from one declaration still answers `is T` — which is also what makes S2.1.4 part 3 work: an undeclared field grows the shape and the grown shape points at the same record. The object TypeId is gone from the enum entirely; `object` survives as a TYPE matched by pointer identity. Fixtures `test/lambda/object_nominal.ls` and `proc/object_open_instance.ls`; baseline 4085/4085, exact tier parity, stable under forced GC. Still open: schema-driven input producing objects (S2.1.3v2), and the S12.3.3v2 bare-`pn` rejection tracked as [LR02-18](../vibe/Lambda_Issue_Ledger.md). |
| S2.1.1v2, S5.4.2v2, S6.2.2v2, S8.1.2v2, S8.2.1v3, S8.3.1v2 (object arm) | **Shipped state under the superseded v2 rulings (2026-09-03).** `entity` is retired from all three keyword tables (C lexer `base_types`, `grammar.js` `_base_type_kw`, `is_type_keyword`) and the reference grammar is regenerated; `let entity = 1` is now legal, where it was `error[E201]`. Objects carry content (D2.6.6 — `Object` is an alias of `struct Element`) and conform across the whole surface: `len` is attributes + content, `in`/`at` walk attribute values then children, an IntKey subscript selects a child, equality is nominal type + unordered attributes + ordered content, ordering is type name then attributes then content, both clone paths copy content, and printing emits round-trippable `<T a: 1, "child">`. Three pre-existing defects were fixed on the way: `item_keys` had NO object arm, so `for (v in obj)` yielded nothing while `len(obj)` reported the field count; object equality compared attributes only, so two objects of DIFFERENT nominal types with matching fields compared equal; and ordering likewise ignored the type name. Fixture `test/lambda/object_content.ls`; baseline 4083/4083, exact tier parity. |
| S2.1.3 (v1, shipped state; superseded by S2.1.3v2) | **Partially implemented 2026-09-03.** Content patterns, the `<T attrs, content>` literal on both tiers, content-pattern inheritance, and tag-name output (markup formats emit `<TypeName …>` via the shared element handler; JSON keeps its `"@"` type key and gains the element `"_"` content key) all work. **Not implemented:** schema-driven input — `input(doc, schema: …)` and a document declaring its own schema still yield structural elements, never objects — and object construction from a schema is unverifiable end-to-end today ([LR12-1](../vibe/Lambda_Issue_Ledger.md)). The **content-arity check is no longer missing**: `validate_against_element_type` enforces `content_length` (`validator/validate.cpp`, both the fast verdict and the reporting path), and since the flip a content-bearing nominal type wears `LMD_TYPE_ELEMENT` and so dispatches into it. Validator tests now load, parse type annotations, and register direct-parser type declarators; the `validate` CLI reaches real validation for the shipped `schema_comprehensive.ls` + `test_data_valid.json` pair instead of failing root lookup. That pair currently reports its independent `element`-versus-`map` mismatch. LR13-3's root-*selection* policy remains open, and these targets still sit in `test-lambda-full`, not `test-lambda-baseline`. |
| S5.1.4v2, S9.1.5v2, S10.4.3v2, S10.5.3v2 | **Ruled 2026-09-03, not implemented.** No container carries a node identity and no `===` exists; the DOM package compares wrappers structurally (`test/lambda/dom_api_core.ls`). The carrier, the id-preserving operation set, and the universal addressing scheme are open (SO39, DO25). |
| S6.1.1 | `fn_lt` uses `strcmp` (NUL-unsafe) and accepts symbols; two-layer invalid-comparison treatment not landed. |
| S6.2.1 | `sort()` coerces to float (`sort(["b","a","c"])` → `[nan,nan,nan]`); total order not implemented in `sort`/`order by`. |
| S7.1.1v2, S7.1.3v2 | Core computed array/map/element reads now return `null` for invalid keys and writes return the hard `ItemError`/`T^` channel; typed-array and mask paths share the same checked key boundary. A broader access-site audit remains for specialized editor/host surfaces. Slice-offset rules (RF3D) landed with regression tests. |
| S7.2.2–S7.2.4 | `last` keyword, `limit last N`, and `{limit:}/{last:}` options not implemented; ArrayNum negative-index audit outstanding. |
| S7.3.1 | Strict null propagation + `skip_null` option pending. |
| S7.4.4 | Skip-edge errors currently surface the bare `ITEM_ERROR` singleton — rich payload pending. |
| S7.6.1 | The one- and two-arm postfix handler grammar and MIR/interpreter lowering conform to S7.6.1v4/S7.6.2v3/S7.6.6v2, including nested `^`/`~` scope restoration, direct raised-`pn` outcome routing, and rich-error preservation. |
| S7.6.5 | Retired `^err` destructuring and prefix `^expr` error tests are removed from the grammar, AST/runtime, and active `.ls` corpus. The handler-local `^` remains scoped to the selected error arm. |
| S7.6.7 | Landed 2026-08-17: statement-position `pn_call() ^ { error_body }` uses explicit ordinary completions before and after suspension; durable native-fault targets cover the S7.11 carve-out without retaining a recovery frame or jump buffer across a yield. Value-producing handlers over possibly-suspending `pn` calls remain rejected. |
| S7.7.1–S7.7.6 | TE-18 declaration-boundary skip pending (routing, case-7 tiers, edge sites). `for x: T in e` does not parse yet — case 1 is `let`/`var`-only until the grammar is extended. |
| S7.8.1 | TE-17 lane gating pending (predicates exist, gate does not). Known violation V1: `fn_array_set` silently despecializes a declared `int[]` — the dominance invariant (S7.7.2) is false today. The `may_defect` effect split must land before routing or every unanalyzed call costs a native lane. |
| S7.10.5 | RF5 audit: several vectorized ops return generic arrays where typed `ArrayNum` is required; a few error-channel violations open (`query`, `url_resolve`, invalid `push`/`splice`). |
| S7.11.4 | Exec recovery implemented on POSIX. **Blocking hazard H1**: batch mode overwrites the stack-overflow handler, so fault capture differs between batch and standalone runs. Windows SEH never exercised. |
| S8.2.1v3, S8.2.2v2, S9.1.6 | Core MIR Direct and AST-interpreter computed access now enforce fixed array/map/element key domains (the v3 object face is not built — see the S2.1.3v2 row), including exact integral float/decimal normalization, empty-string names, and no array-to-map promotion. Specialized editor/host access sites still need the same audit. Empty-string map keys are now semantically valid, but their known JSON round-trip corruption remains to be fixed. `at` membership now conforms: `1 at [10,20,30]` is false, matching S8.2.2v2 (this row previously recorded it as still true). |
| S8.1.3 | **Conformant as of 2026-08-24.** The paired `at` form bound both names to the key (a silent wrong answer); fixed in `build_ast`, one fix covering both tiers. Full record: [LR02-R9](../vibe/Lambda_Issue_Ledger.md). |
| S8.3.2 | Streams (and hence stream `len`) not implemented. |
| S9.1.3, S9.1.4, S9.2.2–S9.2.4 | COW Stage 1 landed (`let`-finality real for Array/Map/Object/Element/VMap — **and, as of CW32v2 2026-08-29 on `nm-impl-work`, for plain ArrayNum**: binding aliases are O(1) mark-and-share, the eager bind clone is retired, marked roots' lane stores consult the shared bit once per store, and mask writes go through a preparing wrapper; fixture `cow_arraynum_alias.ls`, exact tier parity; mutable write-through views deliberately excluded — open/todo). Stage 2 pending: exclusivity checks (faces 1+3+4 landed, face 2 unreachable behind `E229`), capture-assignment compile errors, view-borrow confinement. The **module-`var` half of S9.2.4 needs no work** — it is vacuous by construction (S9.2.4v2); only the view-state half is outstanding. `var` params parse and mutate the caller's value today, but a *plain* param does so too — the snapshot half of S9.1.3 is **UNCONDITIONAL since the 2026-08-29 flip** (escape hatch retired; `is_proc_param` deleted) (CW29, COW doc §11.9; worktree `nm-impl-work`, pending merge): both tiers snapshot mutated plain params — flat, nested-path, and array writes all stay local (fixture `cow_param_snapshot.ls`); `var` is the sole write-through construct. Migration outcome: the 88-script sweep ceiling collapsed to **13 actual reliance sites** (7 ABI-pinning proc tests, 6 benchmarks — the SOM PRNG/out-param idiom), all migrated to `var` with goldens unchanged. **Mutated place-copy binds mark their value** (`var row = m.rows[i]` followed by a write through `row` is a true S9.1.2 snapshot on both tiers: the first write detaches), closing the get-modify aliasing half of C4.1; an UNMUTATED place copy stays a borrow — observationally identical to a copy (P6) — and expression-position reads still borrow, unobservable since no write occurs through an unnamed temporary. With CW32v2 landed, flagged ArrayNum-through-plain-param now snapshots too (probed both tiers); the residual write-through under the flag is only the declared typed-array *native-witness* path, whose raw pointer feeds a native body. |
| S9.2.3 | **Implemented on both tiers (CW30, 2026-08-29; on worktree branch `nm-impl-work`, pending merge).** The decision is compile-time and tier-shared (`AstLoopNode::snapshot_collection`, computed once at for-node completion): only a loop whose source roots at a mutable binding AND whose body may write that root pays — a head share-mark plus, in MIR, an independent rooted handle the loop walks (the call's result register, which is what dissolves the earlier register-aliasing blocker: the binding's register is free to take the detached replacement). Non-mutating loops emit nothing on either tier. Fixture: `test/lambda/proc/cow_snapshot_iteration.ls` (element write, push, map field, `var`-borrow-in-body, multi-level, clean loop) — identical output on JIT and T0; pre-change behavior was live iteration (`99` observed). |
| S9.2.4v2 (view-state half) | Not implemented — see the exclusivity row. |
| Exclusivity (S9.2 / CW §11.3) | **Two of four faces effectively hold.** Face 1 (two `var` args naming one variable) rejects via `E211`. Face 3 (path-prefix) rejects at the *conservative whole-base* granularity the design sanctions as its Stage-2 v1 — so `f(var t, var t.a)` is caught, and `f(var t.a, var t.b)` is caught too though the ruling would permit it. **Face 2 (receiver vs `var` arg) is unreachable**: a `pn` method with a `var` parameter cannot be dispatched at all (`E229`, "dynamic dispatch of a function with `var` parameters is deferred"), so the check would be dead code until that lands. The ratified path that lifts `E229` is CW33 (COW doc §11.10, 2026-08-29) — the `var`-param address ABI, under which this check becomes a slot-address compare in the callee prologue. **Face 4 closed at whole-base granularity (CW31, 2026-08-29; worktree branch `nm-impl-work`, pending merge)**: a `subview` binding records its ultimate base (`NameEntry::view_base`, chased through view-of-view), and the call-site check conflicts two `var` args sharing an effective root — overlapping subviews and view-vs-base both reject via `E211`; views of distinct bases pass. Disjoint tiles of one base also reject: the same sanctioned false positive as face 3, with the splitter ladder unchanged. Fixture: `test/lambda/negative/semantic/var_view_overlap.ls`. |
| S9.3.1 | **UNCONDITIONAL since 2026-08-29** — the flip landed and the escape hatch was retired the same day after the baseline soaked; `LAMBDA_COW_CAPTURE` is no longer consulted. Originally implemented behind the flag 2026-08-28. With the flag set, insertion captures by value at every point the ruling names — array element store, array literal, map field store, map/object/element literal — on both tiers: all four probes return the ruled `1`, and the two-node cycle is no longer constructible, restoring the totality S9.1.5v2 assumes. Capture is a *compile-time* decision, and only a NAMED value (identifier, or member/index read) is marked: a freshly produced container has no second observer at the insertion point, and marking one would make the universal builder shape `rows[i] = <fresh>` detach on first write. **Why it is opt-in:** element/field reads still borrow (the open C4.1 half), so once a slot holds a captured value the get-modify idiom `c = owner[i]` … `c[j] = v` writes a detached copy. Exactly four corpus scripts depend on that idiom (`proc_fill_gc_nested`, `awfy/{cd2_orig,deltablue,deltablue2}`); `awfy/richards3` — the sanctioned rewrite — passes with capture on. The nested-mutation design (§9.5.2, COW Appendix B.2) is what lets the flag become the default. With the flag unset, behavior is exactly the aliasing recorded below. Full record: [LR12-9](../vibe/Lambda_Issue_Ledger.md#lr12-9). |
| S10.2.2, S10.2.3 | `eq ne lt le gt ge` operators and the `vec_cmp` revert not landed; mask-consumption functions deferred. |
| S11.1.1 | Array-pattern composition unbuilt; `is [T]` inline parse crash open. |
| S11.2.3 | Match exhaustiveness checking unverified in the implementation. |
| S11.4.3 | `any \ error` has no working surface spelling (the `!` exclusion operator is broken for general types); it exists as the unwritten default only. |
| S11.4.5 | Landed check implements the superseded type-directional reject: an ANY-held `3.0` into an `int` boundary errors instead of admitting as `3`. Round-2 deliverable #1. |
| S11.4.6 | Constrained-type `is`/`fn_is`/validator divergence open; base-only interim is the shipped behavior. |
| S12.4.1–S12.4.3 | Resource model R1–R5 designed, not implemented. |
| S13.1.3v2 | Task mode and the ordinary `start(target, args, options)` call surface are implemented (2026-08-19). Thread/process modes are recognized and rejected as not implemented; process remains first, thread gated on the isolate-state audit and open item O-D. |
| S13.4.1, S13.4.2 | Pairwise reductions decided, not implemented (sequenced before concurrency work); stream parallelism pending with streams. |
| S14.2, S14.3 | Group-by and joins (S14.1) are implemented; verbs, `over(...)`, DataFrame, and the whole stream/plan system are pending (phases P3–P8). |
| S15.3 | `compile()`, closed environments, and `quote` unimplemented; C9 grammar worklist open (general expression children). |
| S16.1–S16.6 (all) | **Conformant on the S16 harness as of 2026-08-24** (C 123/123, Tree-sitter 118/118). The harness is a case sample, not a proof of total conformance, so the `*` marks stand. Residue: O3 (sibling Tree-sitter scanners), §7.17 (comment vs line-start guard, benign), and the O4 doc sweep — all in [Design_Syntax §4.5/§6](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep). |
| S16.4.1v3 | **v2 core conformant as of 2026-08-22; the v3 computed-key head (one balanced `[…]` group before the colon) is not implemented — tracked with S16.8.9.** Two inverse flips were fixed in `lambda/runtime/parser/lambda_parser.c`: `if_statement_body_is_map` bailed out on a `(` head (so the paren spelling rejected every map body in statement position), and `parse_for_expression` gated the map reading on `parenthesized` (so the *bare* `for` spelling rejected one the paren spelling accepted). Both spellings of `if` and `for` now agree; `while` correctly stays always-block per S16.4.3. |
| S16.4.2 | **Conformant as of 2026-08-22.** `control_body_brace_is_map` breaks the empty-brace tie in `if`/`for` bodies from `procedural_depth`; that depth now tracks the enclosing function's *effect kind* rather than a nesting count, so a `fn` inside a `pn` is fn context, and an arrow body is forced to fn context so `() => {}` mid-procedure is still the empty map. Verified across value, content, `if`, `for` (both spellings), arrow, and `pn` positions, plus fn-in-pn and arrow-in-pn nesting. |
| S16.6.6, S16.6.7 | **Conformant in both front ends as of 2026-08-24** (C 140/140, Tree-sitter 135/135, zero corpus movement). Enforcement mechanics and findings: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep) and §6 point 35. |
| S16.6.8, S16.6.9 | **Conformant as of 2026-08-24** (`E312` in `build_ast` per S16.6.5; C 152/152, Tree-sitter 135/135, baseline 3868/3868). Classifier subtleties (three-way recursive `ast_branch_kind`, NEUTRAL empty branch) and migration: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep) and §6 point 38 addendum. |
| S16.8.4, S16.8.8, S16.9.2, S16.9.4 | Not probed against the implementation; the `*` is precautionary, not a known defect. S16.8.1–S16.8.3, S16.8.5–S16.8.7, S16.9.1, S16.9.3 were spot-checked conformant on 2026-08-22 and ship unmarked — including the S16.9.3 element boundary-comma biconditional in all four of its cases. |
| S16.9.5 | **Parsing conformant as of 2026-08-25; the field/value distinction is not yet represented.** Residue: the marker wraps the field type in `OPERATOR_OPTIONAL` — the same representation `a: T?` produces — so the two spellings this ruling calls *distinct* are indistinguishable downstream until `ShapeEntry` carries a field-level flag; independently, the declaration binding checker treats an optional field as required for both spellings (`error[E205]`, pre-existing). History: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep). |
| S12.3.7 | **Ruled 2026-08-27, not implemented — currently a crash.** `fn sum(a) => 99` plus a call executes the interpreter tier with no printed result and dies at teardown (ASan dealloc, debug build); `len`/`min` shadows likewise. Needs one resolution point in `build_ast` (both tiers) plus the shadow warning. Tracked as [LR02-15](../vibe/Lambda_Issue_Ledger.md). |
| S16.8.9 | **Ruled 2026-09-05, not implemented.** Grammar `_key` (covers `map_item` and `attr_name`), the C parser's `parse_map`/`parse_element` and the two lookahead predicates (`braced_expression_is_map`, `element_attribute_starts`) that must skip one balanced `[…]` group, and a computed variant of the key-expression AST node lowered on both tiers through the keyed-spread path. Design record: [Design_Syntax §7.26](../vibe/Lambda_Design_Syntax.md). |
| S16.10 | **Ruled 2026-08-27, largely not implemented.** Current divergences: element tags reject keywords (`<if a:1>` errors, legal under S16.10.2); `import edit:` parses and every use fails; `import 'edit':` parses and creates an unreachable binding (use is silently null); `let if = 1` parses and uses fail; `let type = 1` parses and `type` silently reads the base type — the silent misread is the priority defect. E201 exists for `last` only and must extend to the whole table, in the C parser and the reference grammar. Migration: ~55 keyword-named corpus bindings (breakdown in [Design_Syntax §7.24](../vibe/Lambda_Design_Syntax.md)). Tracked as [LR02-14](../vibe/Lambda_Issue_Ledger.md). |
| int v5 (S4.1) | Substantially landed (lane, encoding, saturation, printing, goldens). Residue: `INT64_ERROR` collides with `INT_LANE_INF` (pre-cutover gate unsatisfied); ELEM_INT SIMD kernels partly gated; nullable lane (`INT_LANE_NULL`) partial; `IntLane`/ValueRep typing of the four i64 meanings pending (known silent bug class). |

## Appendix B — Open Design Issues

Numbered `SO#` (semantics-open) for stable reference; each links to its
record. (The prefix is the spec's own — distinct from the historic review
findings B1–B13 cited as `[B#]`, and from the `OI-#` ledger in
`vibe/Lambda_Issue_Ledger.md` §15.)

**Numerics**
- **SO1** Sized-lane `div`/`%`: [Number_Model §3.3.2](../vibe/Lambda_Semantics_Number_Model.md) says sized×sized `div` stays in the machine lane; this spec (S4.5.3, per Int_Type §2.2) says it leaves the lane — `3i8 div 0i8` needs an explicit call, and Number_Model needs a supersession note.
- **SO2** Int v5 §5 details: poison-algebra table ratification; finiteness-proof dataflow home; formal ruling on the 56-bit packing (de facto shipped) and the freed encoding octant; migration gates. [Int_Type §5]
- **SO3** The `int?` fourth lane value (`INT_LANE_NULL`) is undocumented in the Int_Type sentinel table; the `int | null` ABI for `index_of`/`ord` needs restating under the nullable lane.
- **SO4** Bitwise semantics were ruled (S4.1.2), but the interaction with the retired sparse band in old goldens needs a sweep.

**Errors and enforcement**
- **SO5** TE-17 transitivity: does discharging `(int | error)[]` re-narrow in place, or only by copy? Copy is the safe default. [TE-17 §Open]
- **SO6** Lazy/streaming `for` bodies vs typed-lane destinations (boxed-until-proven presumed, undecided); where containment materializes under deferred evaluation.
- **SO7** TE-5 R5 sticky `any`; validator schema-`any` uniformity.
- **SO8** Should `is` become value-aware? Deliberately undecided (S11.3.1v2 records the intentional asymmetry).
- **SO9** A surface spelling for `any \ error` (the `!` exclusion operator route is broken — measured 2026-08-24, `&` and `!` evaluate correctly in `is`/`match` pattern position but are rejected in `let`/parameter annotation position; LR02-9 in [`vibe/Lambda_Issue_Ledger.md`](../vibe/Lambda_Issue_Ledger.md)); closed named-map opt-in; constrained-type predicate enforcement; checked-cast surface (`as`/`as?`); generics; flow-sensitive narrowing — all out of scope or unowned.
- **SO10** A deep "does this data contain an error anywhere?" check (`valid(item)`-shaped) — real question, future design (S7.9.3).
- **SO11** Whether a non-null scalar iterates once (`len(5)`): `for (i in 5)` yields nothing while `5 |> ~` yields one item; the S8.3.1 law requires them to agree before `len(5)` is settled.
- **SO12** *(closed 2026-08-24 — ruled in as S8.1.3; record [LR02-R9](../vibe/Lambda_Issue_Ledger.md).)*

**Values, COW, resources**
- **SO13** COW granularity on large documents: node representation for spine-copying, refcount discipline for unique-path in-place update, and the gating benchmark. [C4.3]
- **SO14** Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or guaranteed get-modify-put. **Owner document as of 2026-08-28: [`vibe/Lambda_Design_Nested_Mutation.md`](../vibe/Lambda_Design_Nested_Mutation.md)** (CW22–CW28, PROPOSED). It rules that a place is a borrow and never a value (S9.2.2 generalized from slices to paths), that `var b = <place>` keeps copying, and that a mutated place copy becomes a compile error — the last being what gates the S9.3.1 default flip. On ratification these become S9.4 and this entry is struck. [C4.4]
- **SO15** Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping). The companion "module-`var`-as-borrow final rule" is **closed**: module-level `var` does not exist (S9.1.7 / RG14), so only the view-state case remains and S9.2.4v2 already rules it forbidden.
- **SO16** Close-error routing (double fault): proposed — normal-exit close failure becomes the `pn`'s error; on error exit the original wins, close error attached suppressed. To confirm. [Features §3.5.2]
- **SO17** Resource-carrying-type containment rules (when a wrapping value is itself resource-typed). [R3]
- **SO18** Snapshot iteration (C4.2d) — **implemented 2026-08-29 (CW30, worktree branch `nm-impl-work`, pending merge)**; see the S9.2.3 conformance row. Strike this entry when the branch merges.
- **SO19** Root and upward-parent navigation are resolved by S10.4.3v2,
  S10.5.3v2 / PTH10, PTH29: lineage lives in a navigation path, cursor, or
  zipper, never root/parent pointers on document values. Lateral-axis spellings
  and exact semantics remain open and must preserve that invariant.
- **SO39** Node identity (S5.1.4v2): which operations preserve an identity
  (a COW detach and an in-place `var` write are expected to; a constructor
  such as `{*: m}` or `<T *: o>` is expected not to), whether formatting can
  emit it, the concrete form of the universal id (document path + node id,
  across local and online documents), and whether `===` on two identity-less
  operands is `false` or a compile error. Deliberately unruled on
  2026-09-03; the carrier is DO25. [OB10]
- **SO40** ~~Whether `obj is element` holds for an object (structural face) as `obj is map` does.~~ **ANSWERED by the D2.6.6v2 representation flip, and no longer open.** The question presupposed the superseded model in which a nominal value was always element-shaped and `map` was the odd case. Under S2.1.1v3 an object wears its DECLARED structural kind, so the structural test simply reports that kind: an attribute-only type yields maps, and `is map` is true while `is element` is false; a type with a content pattern yields elements, and the reverse holds. `is object` is true either way, which is the orthogonality S2.1.1v3 rules. Pinned by `test/lambda/object_nominal.ls` — `[p is P, p is object, p is map, p is element]` is `[true, true, true, false]`, and the content-bearing type gives the same shape with the last two swapped. S11.3.1v2 still keeps `is` nominal for the `object` type itself, which is matched by record identity and never by a tag. [OB2]
- **SO42** Instance type alteration (S2.1.5): the surface spelling, which
  declared fields must be satisfiable from the instance, and what happens to
  extras on reconstruction. Ruled to exist as reconstruction; otherwise
  undesigned. [OB18]
- **SO43** Type alteration — changing a nominal type during evaluation, and
  so every instance of it — is explicitly NOT ruled; it would relax S2.1.4(2)
  and is a different matter from SO42. [OB18]
- **SO41** Cross-reference form for document graphs under S9.1.5v2: an
  identity or key stored as data and resolved through its document, foreign-key
  style — the resolution API, its authority (S10.5.2), and its interaction with
  the symbol/path model are undesigned. [OB10]

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
- **SO28** Effect polymorphism (pure-iff-argument-pure HOFs). Partially
  answered 2026-08-24: S12.1.4 admits `call` (S12.3.4) as the first
  effect-polymorphic function. Whether the mechanism extends to
  user-declared signatures stays open; Flix-style Boolean effect
  polymorphism is the recorded minimal fix. [Features §3.6]
- **SO29** File write/append syntax (C6a: `into`/`onto` candidates); string interpolation syntax (note the `$` collision with quote splices); a set type; `assert`/`expect` unification.
- **SO31** The `<file>` element shape (name/size/mime, content as child) — pin with file-I/O spec.
- **SO32** Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns.
- **SO33** A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in assignment position — document or delete.
- **SO34** `emit()` vs `send()` — two event vocabularies coexist; state the boundary explicitly.

**Surface syntax**
- **SO35** A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics are argued together, and one source beats two. If the grammar surface outgrows a section, extract S16 into a formal syntax spec and leave pointers — not a second, competing statement. [Design_Syntax]
- **SO37** *(closed 2026-08-27 — ruled in as S17.2.1/S17.2.2: `lambda.sys.*`
  with a reserved `lambda` root.)*
- **SO38** Whether `that` over a **map** should keep the surviving keys
  (yielding a map) rather than dropping them (yielding an array of values, the
  current behaviour recorded in S10.1.5). The pipe has the same question, and
  the two should answer it together: `~#` binds the key in both, so the key is
  observable to the predicate but absent from the result. An S10 container-
  shape question. [S10.1.2, S10.1.5]
- **SO36** Whether a `pn` call may appear nested inside an expression
  (`(pn_func(), 123)`, `if (exists(path)) …`), or only as a bare statement /
  the whole RHS of a binding — the A-normal-form effect-sequencing
  discipline. Deliberately split off from S16.6.8/S16.6.9 and left open; the
  argument and cost survey live in
  [Design_Syntax §6 O5](../vibe/Lambda_Design_Syntax.md). An S12
  effect-boundary question. [S12.1]

## Appendix C — Decision-Record Index

| Section | Records | Where argued |
|---|---|---|
| S1 principles | C1–C18 distilled; Features §3.6 | `Lambda_Semantics_Formal.md`, `Lambda_Semantics_Features.md` |
| S2 value domain | C1, C1.6a, C2, C8.6-R; PTH1v2, PTH2v2, PTH3–PTH29; OB1–OB3, OB7–OB9, OB13–OB18 | `Lambda_Semantics_Formal.md`, `Lambda_Type_Path.md`, `Lambda_Type_Object.md` |
| S3 truthiness | C2, C17 | ibid.; `Lambda_Semantics_Formal2.md` |
| S4 numerics | C3, C13, C14b/c, C16, C17; int v5 | `Lambda_Semantics_Formal2.md`, `Lambda_Semantics_Int_Type.md`, `Lambda_Semantics_Number_Model.md` |
| S5 equality | C8, C8.5, C8.5a, C8.6, C8.6-R, C8.7, C9-4; OB4, OB10, OB16, OB19 | `Lambda_Semantics_Formal2.md`, `Lambda_Expr_Eq.md` (rationale only), `Lambda_Type_Object.md` |
| S6 ordering | C11, C11.4, C11.5; OB13 | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Object.md` |
| S7 absence/errors | C5, C5.3, C5.3b, C14, C14a, C15, C15a/b; TE-4, TE-9, TE-13, TE-15–TE-18; RF1–RF6; ER-D1–PD13; REH-D1–REH-D14 | `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Sys_Func.md`, `Lambda_Design_Exec_Recovery.md`, `Lambda_Design_Runtime_Error_Handling.md` |
| S8 membership | C5.3a, C5.3b; §8.0–8.3 records; OB4–OB5 | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Object.md` |
| S9 mutability | C4, C4.2a/b/c/e, C4.3, C5.3b, C12; CW16–CW28; RG14 | `Lambda_Semantics_Formal.md`, `Lambda_Semantics_Formal2.md`, `Lambda_Design_Runtime_COW.md`, `Lambda_Design_Nested_Mutation.md`, `Lambda_Design_Runtime_Globals.md` |
| S10 operators | C6, C6.2–C6.4, C10; PTH3, PTH5–PTH6, PTH9–PTH10, PTH25–PTH29 | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Path.md` |
| S11 types | C7, C8.5c; TE-1–TE-18; OB13 | ibid.; `Lambda_Design_Type_Enforcement.md`, `Lambda_Type_Object.md` |
| S12 effects/resources | Features §3.5–3.7; Procedural; Function_Arg; C19; OB5–OB6 | `Lambda_Semantics_Features.md`, `Lambda_Procedural.md`, `Lambda_Proc_Assignment.md`, `Lambda_Design_Function_Arg.md`, `Lambda_Type_Object.md` |
| S13 concurrency | K11–K32 | `Lambda_Design_Concurrency.md` |
| S14 data processing | PD9–PD16; FC1–FC11 | `Lambda_Design_Data_Processing.md`, `Lambda_Expr_For_Clauses2.md` |
| S15 metaprogramming | C9, C9a | `Lambda_Semantics_Formal2.md` |
| S16 surface syntax | Design_Syntax §3–§7 (39 decided points) | `Lambda_Design_Syntax.md` |
| S17 system library | C18 | `Lambda_Semantics_Formal2.md` |

The decision records preserve the full deliberations — every alternative that
lost and the arguments that did not persuade. This specification is their
distillation: the record governs the history; this document governs the
language.
