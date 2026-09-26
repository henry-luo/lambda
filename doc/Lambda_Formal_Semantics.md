# Lambda Formal Semantics — Specification

**Spec version:** 43.0.1 (2026-09-26)

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
PTH1v2, PTH2v4, PTH3, PTH4v2, PTH5–PTH15, PTH13.1–PTH13.2, PTH16v3, PTH17–PTH20, PTH21v2,
PTH22–PTH27, PTH28v2, PTH29
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
  `for (i in x)` performs; `len` is shallow; for a sequence literal,
  `len([x₁, …, xₙ]) = Σ count(xᵢ or 1)`. [S8.3]
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

- **S2.1.1v4** Scalars: `null`, `bool`, `int`, `integer`, `i64`, `u64`,
  sized ints `i8 i16 i32 u8 u16 u32`, `f16 f32`, `float`/`f64`,
  `decimal`, `string`, `symbol` (with `path` as a special symbol), `binary`,
  `datetime` (with `date`/`time` sub-kinds); `string`, `symbol`, and
  `binary` are the text scalars, placed as one value and walked as sequences
  (S2.5.8). Containers: `array` (transparently unboxed numeric variants) with
  its two specialized kinds `range` and `list` — `list <: array`,
  `range <: array`, and `type()` names the specific kind (S2.5.1v2) —
  `map`, `element` (a list of children *and* a map of attributes). First-class: `function`, `type`,
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
  tree-construction rule (the XDM position), independent of string equality;
  one case of the content model's S2.6.2. [C1]
- **S2.2.4** An empty file is a childless `<file>` element, not an empty
  binary: existence lives in the container, content in the value. [C2]

### S2.3 Ordered storage, unordered equality

- **S2.3.1** Maps store keys in source/insertion order — round-trip fidelity,
  deterministic iteration, order-significant formats — while map **equality
  compares keys unordered** (§S5.4). Representation order is data; identity is
  content. [C8.6-R]

### S2.4 Paths

- **S2.4.1v2*** Path values use dotted steps. `/` selects the logical global
  reference root and `\` the active relative reference root (S16.9.4): a
  resolver may qualify `/.a.b` as `file.hostname.a.b` (projected as `/a/b`) or
  as `http.hostname.a.b`; `\.a.b` remains relative to the active base. `~~` is
  the parent step, never a root: `\.~~.a` is parent-relative and `\.~~.~~.a`
  applies two parent steps. The retired `/a`, `..a`, bare-dot `.a.b`, and
  compound `_..` spellings do not exist. Quoted, wildcard, and dynamic steps
  retain their ordinary dotted/indexed forms. [PTH1v2, PTH2v4, PTH3, PTH4v2,
  PTH11, PTH17, S16.9.4]
- **S2.4.2v5*** Every hierarchical reference is a typed root plus ordered
  operations; a root alone is a complete path (`/`, `\`). Ordinary keys are
  `NameKey` or non-negative `IntKey`, and an integer key may come first:
  `\.1` and `/.1` are `IntKey(1)`. A subscript on a path is the dotted step
  with a computed key and never forces the target: `p[k]` ≡ `p.k`, so
  `(/)[1]`, `/[1]`, and `/.1` are one path, as are `\[1]` and `\.1`. Inside
  a path literal every step is a key step: `\[1].name` is `\.1.name`, never
  a property read. A dot is always followed by a step, so `\.` alone and
  `.[` anywhere (`/.[1]`, `\.[1]`) are errors. A dynamic subscript is
  evaluated and normalized through S8.2.1v4 before lookup.
  A key-domain mismatch is an invalid member access: its read yields `null`
  under S7.1.1v3 and its write raises through the hard `T^` channel under
  S7.1.3v2/S7.4.2. It never implies container conversion. Root, parent, and
  wildcard navigation are operation kinds, so `a.1.b` and `a.'1'.b` remain
  distinct. Postfix root `./` discards descendant steps back to the logical or
  provider/authority anchor; unresolved relative root selection remains as
  `\./`. Parent steps apply left-to-right: they remove a preceding child,
  accumulate at the relative root, and clamp at an anchored root. Equality,
  hashing, printing, target resolution, and
  `base ++ relative_suffix` observe the same normalization. [S1.6, S8.2.1v4,
  PTH7–PTH9, PTH12–PTH14, PTH13.1–PTH13.2, PTH25, PTH28v2]
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
- **S2.4.5v2*** Paths have three root forms: rooted `/.a.b`, relative `\.a.b`,
  and absolute `SchemeName...`. Rooted paths qualify logical `/` through the
  active resolver; absolute paths name their provider/authority directly and
  bypass that mount. File absolutes use `file./.a.b` for `/a/b` on the current
  machine and `file.hostname.a.b` for `/a/b` on a named machine; consequently
  `file.a.b` selects host `a` and path key `b`. A registered scheme name
  heading a dotted chain is reserved as an absolute-path root. Rooted and
  absolute path values retain distinct root kinds even when they resolve to
  the same qualified target. All three forms use the same typed operations.
  [S5.1.4v2, PTH21–PTH28]

### S2.5 Lists and blocks

- **S2.5.1v2** **A list spreads; an array does not.** A list is a
  specialized array, as a range is: `list <: array`, `type((1, 2))` is
  `list`, `(1, 2) is int[]` holds, and the three are one sequence family
  (S5.3.1) that differs in one bit. A list auto-spreads: wherever it sits as
  an item — in a list, an array literal, or content (S2.6.3) — its items are
  spliced in place, recursively, so `[(1, 2), 3]` has three items and
  `((1, 2), (3, 4))` is `(1, 2, 3, 4)`: **a list never nests**. An array or a
  range is always one item, whatever it holds. A list does **not** normalize
  its items: `(1, null, 2)` keeps three and `("a", "b")` two; normalization
  happens only where a list lands in content (S2.6).
- **S2.5.2v2** **A for-expression produces a list, whatever its clauses**
  — `where`, `order by`, `limit`/`offset`, `group by` included — so its
  results spread where it lands: `[for (i in 1 to 2) (i, i)]` has four items.
- **S2.5.3** **A block `{…}` produces a list** of its statements' results, in
  order — not merely its last expression: `{ 1; 2 }` is the list `(1, 2)`.
  A `pn` body is the exception: it follows the procedural convention and,
  without `return`, yields its last expression (S12.1.2).
- **S2.5.4v2** **Declarations produce no item.** `let`/`var` bindings and
  `type` and function (`fn`/`pn`) declarations bind names and contribute
  nothing to the enclosing block, list, or content — as `let` does in a
  list: `(let x = 5, x + 1)` has the single item `x + 1`, and
  `{ let y = 1; y + 1 }` the single item `y + 1`. A `let` is an expression
  **only** as an item of such a parenthesized list, anywhere among its
  items; elsewhere it is a statement, so `[let x = 1, x]` and
  `f(let x = 1)` are syntax errors. [USER 2026-09-24, Design_Syntax §7.18]
- **S2.5.5v2** **A list has at least two items.** A one-item list *is* its
  item and the empty list *is* `null`: `(x)` ≡ `x` and `()` ≡ `null`, so
  `(((e)))` is always `e`; blocks collapse alike (`{ let y = 1; y + 1 }` is
  `2`, a block of declarations only is `null`). Collapse counts items after
  spreading and never drops one: `(1, null)` stays two items. **Void versus
  null**: a list *expression* sitting in an item position splices whatever it
  has, including nothing, while a list *value* — bound, passed, returned,
  stored, compared — has collapsed, and a collapsed empty list is the `null`
  value, placed as an item like any other: `[for (x in []) x, 1]` is `[1]`,
  but `let e = for (x in []) x; [e, 1]` is `[null, 1]` and `push(a, e)`
  pushes `null`. A non-text scalar has no content (S8.3.1v3), so a computed
  one-item list is invisible to `for`, `len`, and indexing —
  `len(for (x in [10]) x)` is `len(10)` = 0. Wrap when a stable collection is
  wanted: `[for …]` always yields `[]` when empty; a function result is
  already an array and never collapses (S2.5.7v4).
- **S2.5.6** **A list is transient; where it lands decides.** The
  persistent data model — maps, elements, objects, arrays, scalars: whatever
  can be serialized — holds no lists. A list in a *sequence-item position* (a
  list or array literal, content, an insertion by `push`/`splice`/`a[i] = v`)
  splices (S2.5.1v2); a list in a *single-value slot of a persistent
  container* (a map field, an attribute, an object field) is stored as the
  array it is — `{f: (1, 2)}.f` is `[1, 2]`; everywhere else — binding,
  argument, return, operand, subscript — it passes through unchanged, and an
  annotation checks it without changing its kind (S11.1.6v2). A list prints and
  formats as its array; the spread bit is never serialized; a top-level list
  is content (S2.6.1).
- **S2.5.7v4** **A list is built, never computed: functions and pipes return
  an array for any sequence input; operators keep the operand kind.** The
  list kind (S2.5.1v2) belongs to construction — a list literal, a `for`
  expression (S2.5.2v2), a block (S2.5.3), content (S2.6), a query (S8.2.4v3) —
  because those compose values and content and must splice where they land.
  Everything that *processes* a sequence returns an array: the mapping pipe
  and `|:` (S10.1.2v4, S10.1.6), `sort`, `reverse`, `unique`, `take`, `drop`,
  `slice`, the positional selections `[i to j]`, `[mask]` and `[[i, j]]`
  (S8.2.4v3), `zip`, `fill`, `split`, `find`, `varg()`, rest
  parameters, `content(e)`, `keys`/`values`/`names` (S8.4.1v2), and the
  vectorized sys funcs (S7.10.5v3): `(1, 2, 3) |> ~ * 2` is `[2, 4, 6]`,
  `take((1, 2, 3), 1)` is `[1]`, `zip(list, list)` is an array of pairs,
  `fill(2, (1, 2))` is `[1, 2, 1, 2]`. A function result is therefore `T[]`:
  it never collapses (S2.5.5v2 collapses built lists only), it is sound under
  a `T[]` contract, and it chains through a multi-step pipe; a splice into
  content is written `*(…)` or as a `for`. **Operators are broadcast
  notation, not processing, and keep the operand kind**: a scalar operand
  leaves the kind alone, list `⊕` list is a list, and any array or range
  operand gives an array — `(1, 2, 3) + 1` is `(2, 3, 4)`, `[1, 2, 3] + 1` is
  `[2, 3, 4]`, `(1, 2, 3) + [1, 2, 3]` is `[2, 4, 6]` (the list is demoted),
  `(1, 2) ++ 3` is `(1, 2, 3)`, `(1, 2) ++ [3]` is `[1, 2, 3]`. This is sound
  because no operator shrinks: `+ - * /` (S10.2.1), the mask comparisons
  (S10.2.2), and `++` (S10.6.1) return at least as many items as a list
  operand had, so an operator result is never a collapsing one-item list.
  Text follows S2.5.8 (a text kind is a type, not a bit). [Expr_Pipe §F.6]
- **S2.5.8** **Text is placed as one value and walked as a sequence** —
  exactly like an array. Iteration, `len`, indexing, `in`, the pipe, `|:`,
  and every sequence operation see a string or symbol as its code points and
  a binary as its bytes (S8.3.1v3, S8.1.1): `"a" in "cat"` is character
  membership (substring search is `contains`), `reverse("abc")` is `"cba"`,
  `"abc" |: ~ != "b"` is `"ac"`. A sequence operation over a text value
  yields that text kind when every result item is of that kind — concatenated:
  `"abc" |> upper(~)` is `"ABC"`, `"abc" |> ~ ++ "-"` is `"a-b-c-"` — and an
  array otherwise: `"abc" |> ord(~)` is `[97, 98, 99]`; an empty result is
  `""`. For `++` and for placement a text value is one item (S10.6.1).

### S2.6 Content

- **S2.6.1*** **Element content and script top level share one content
  model.** Content is an ordered sequence of items — in general an array of
  any values — that *normalizes* as it is built (S2.6.2–S2.6.4) and stays
  normalized (S2.6.5); a script body is the content of a virtual
  `<file>`/`<script>` element (S16.7.1). Parsed documents are content too:
  an input parser yields normalized content, and strings that must stay
  apart are carried inside a non-merging item — e.g. an array,
  `<cmd ["a", "b"]>` — never as adjacent content strings. Normalization
  belongs to content alone: collections never normalize, lists included
  (S2.5.1v2), so `[1, null, "", 2]` keeps all four items. [C1; Design_Syntax §7.23, §7.27]
- **S2.6.2** **Absent and empty items are dropped.** A `null` contributes
  nothing, however it arose — written literally, read from a missing key
  (S7.1.1v3), or produced by an `else`-less `if` (S16.6.3) — and neither does
  an empty string `""` (S2.2.3). An empty binary cannot arise (S2.2.2).
- **S2.6.3** **Lists spread into content; arrays do not (S2.5.1v2).** A list
  item — a for-expression's or a block's result among them (S2.5.2v2, S2.5.3) —
  splices its items inline, recursively, each spliced item normalized as if
  written in place; an array or a range stays one item. So
  `<e for (i in 1 to 2) (i, i)>` has four items, while `<e [1, null]>` and
  `<e 1 to 3>` have one each.
- **S2.6.4** **Adjacent strings merge, adjacent binaries merge, nothing else
  does.** After S2.6.2 and S2.6.3, each run of consecutive `string` items
  becomes one string and each run of consecutive `binary` items one binary —
  the two array-like scalars. A string never merges with a binary. Every
  other item is kept as it is: symbols, numbers, and all other values never
  merge, and a non-merging item keeps its neighbours apart (`"a" 'x' "b"` is
  three items; so is `"a" b'\x01' "b"`). Dropping precedes merging, so
  `"a" null "b"` is `"ab"`.
- **S2.6.5** **Content stays normalized under mutation.** Every content
  write — child assignment `e[i] = v` (S12.2.2), insertion, removal — leaves
  content satisfying S2.6.2–S2.6.4, exactly as if the result had been built
  afresh: writing `null` or `""` removes the child, a written list spreads in
  place, a string or binary written beside one of its kind merges with it,
  and removing an item that kept two strings apart merges them. Positions
  after the write may therefore shift.

---

## S3 Truthiness

- **S3.1** The falsy set is exactly **`null` · `false` · error values · `""`**.
  Everything else is truthy — all numbers (`0`, `-0.0`, `nan`), all containers
  (empty or not), datetimes, symbols, binaries, functions, types. [C2]
- **S3.2** **Truthiness is decidable from the type tag alone** — a design
  invariant, not an accident. Any proposal to make a *value* of a truthy type
  falsy must clear this bar; `nan` stays truthy because poison carries no
  falsy tag. [C17]
- **S3.3v2** Consequences to teach: truthy-0 keeps `or` a safe coalescing
  operator (no `??` needed); `if (results)` does not mean "any results" for
  an array — write `len(results) > 0`; for a list result the empty case is
  `null`, so `results or default` works, but a one-item list is its item and
  the item's own truthiness applies — a single `false`, `""`, `null`, or
  error result reads as no result (S2.5.5v2); `a div b or 0` does not rescue
  a zero divisor (poison is truthy) — guard the divisor or test `is nan`.
  [C2, C17]

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

- **S5.2.1v2** Numeric equality is exact mathematical-value equality across
  ranks: `1 == 1.0 == 1n == 1.0m == 1.00m`; `-0.0 == 0.0`; storage width and
  decimal scale are non-semantic metadata, not identity; `decimal.inf == inf`.
  The numeric order has the same ties, so neither `1.0m < 1.00m` nor
  `1.00m < 1.0m`. [C8.5, C8.6a, C16]

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
  kinds (S2.1.1v4) — by type name, then attributes, then content.* [C11.4,
  OB4, OB13]
- **S6.2.3** Sort is stable; `desc` is **full reversal** — one pure order, no
  pinning exceptions. [C11.4]

---

## S7 Absence and Errors

*Total reads, checked writes, deliberate failures.*

### S7.1 Reads are total; writes are checked

- **S7.1.1v3*** Every invalid member/index **read yields `null`**. This
  includes out-of-bounds or negative sequence/string positions, missing named
  members, a key outside the base's domain (`array["name"]`, `array[5.5]`,
  `named_map[1]`), and a non-error base with no applicable member face. No invalid
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
  failures covered by S7.1.1v3. *Reads ask a question; writes issue a command,
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
  context, so a bare field name in `v` reads `~.name` (S10.1.7v2). Its type is
  `type(h) | type(v)`. An error raised while evaluating
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

- **S7.10.1v3** Result shape is fixed by the contract, never by cardinality,
  for array, list, and text inputs: zero-to-many → `[]`; zero-or-one →
  `null`; string-valued no-content → `""`; non-admissive invalid input →
  `error`. The four are distinct and must stay distinguishable. A list input
  is processed as the array it is and yields an array result (S2.5.7v4) —
  `take((1, 2, 3), 1)` is `[1]`, never the collapsing item.
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
- **S7.10.5v3*** Vectorized sys funcs are sequence-in, array-out
  (S2.5.7v4): array or list in → array out, one lane never collapsing to a
  scalar and zero lanes a typed empty `[]`; `null` in is `null` out, so
  `null ⊕ x` is `null`; lane exceptions produce lane values (`nan`/`inf`),
  never a shape change. The vectorized *operators* keep the operand kind
  instead (S2.5.7v4, S10.2.1).
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

- **S8.2.1v4*** Every container has a fixed key domain. The sequence face of
  arrays, lists, and ranges uses non-negative `IntKey`s. An `int`, `float`, or
  `decimal` subscript normalizes to an `IntKey` only when its value is finite,
  mathematically integral, exact in the index integer domain, and
  non-negative: `a[5]`, `a[5.0]`, and `a[5.00n]` select the same member.
  Fractional values such as `5.5`, negative integral positions, strings, and
  symbols do not name sequence members. A structural map's named face accepts
  string and symbol subscripts as `NameKey`s; the empty string is a valid,
  distinct name rather than absence. A **VMap** has exactly two canonical key
  classes: `NameKey` (a symbol key, with string and symbol spellings
  normalized by exact contents) and `IntKey` (any finite, mathematically
  integral numeric value). Thus `1`, `1.0`, `1n`, `1.0m`, and `1.00m` name one
  VMap entry; fractional numbers, `nan`, infinities, booleans, and every other
  kind name none. VMap `IntKey`s are map keys, not sequence positions, so they
  may be negative. An element or an object exposes both faces: an `IntKey`
  selects a content child, while a `NameKey` selects an attribute.
  Any subscript not admitted by the selected container face is an invalid
  member access: the read yields `null`, while the write raises a hard error
  under S7.1.3v2/S7.4.2. On an object a `NameKey` subscript is the dynamic
  form of dot — `obj["m"]` resolves exactly as `obj.m` does (S12.3.3v2),
  reaching the type's methods before yielding `null`.
  `for (k, v in c)` exposes the resulting canonical key uniformly
  (`[for (k, v in [10,20]) k]` → `[0, 1]`). [C5.3b, C8.6a]
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

- **S8.2.4v3*** **Two subscript worlds: a type key answers as XPath, a positional selection as NumPy.** `e[T]` walks `e`'s content as `for (x in e)` does — a list, range, array, map (its values) or element (attribute values, then children) — and keeps each `x` that is `T` and is not `null`; it answers as a subscript does: the match itself for one, `null` for none, a list for two or more — the run `T*` (S11.1.6v2), collapsing by S2.5.5v2 — so a lone match needs no unwrapping (`doc[<title>]`) and `if (e?T)` is the absence idiom; `count(e?T)` is the match count (S8.3.3v3), `len` is not. A list is stepped set-at-a-time: `e[T]` on a list applies the step to each item and splices, so `html[table][tr][td]` reads as XPath's `//table/tr/td` at any number of tables; a scalar item has no content, so `(1, "a", 2)[int]` is `null` where the array `[1, "a", 2][int]` is `(1, 2)` (an array is a container, a list a sequence: S2.5.1v2). `e?T` / `e.?T` (S7.6.4) is `e[T]` applied recursively in document order, the root excluded / included — on a list, each item's descendants, so `html?<div>?<div>` is the divs nested in divs. A `null` value never matches, even under `T | null` or `null`. The result is a value, never an item-position producer: `[e?T, 9]` with no match is `[null, 9]`; `*e?T` splices whatever there is. **Positional selections are NumPy's**: a range `[i to j]` (S7.1.2), a boolean mask, and an index array `[[1, 3, 5]]` yield an array whatever the count — a range clamps and is `[]` when empty; an index array keeps its own length, and an out-of-range or negative position contributes `null` (S7.1.1v3, S7.2.1) — and they compose with a type key: `e[T][1 to 3]` slices the run, on a lone match that value's own content. *Why two worlds: a type key names members, and the one expected match must not need `[0]`; a positional key names a window, whose shape must follow the key, not the data (R's `drop = TRUE`). Aligning them breaks one or the other — ruled 2026-09-26 after an array reading of the type key was tried and reverted.* [Expr_Query §4.1]

### S8.3 `len`

- **S8.3.1v3** The law: **`len(x)` is the number of iterations `for (i in x)`
  performs.** Consequences, not separate rules: `len("str")` = 3 (text walks
  by code point, S2.5.8); `len(5)` = **0** — a non-text scalar has no
  content, so `for (i in 5)` iterates nothing, while the mapping pipe and
  `|:` treat a scalar as one member by their own rule (S10.1.2v4,
  S10.1.6): a placement, not an iteration;
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
- **S8.3.3v3** Two lengths: `len` measures content; `count(x)` is the size
  of the run `x` is (S2.5.5v2, S11.1.6v2) — `0` for `null`, `len(x)` for a
  list, `1` for any other value (an array or text is one item) — and an error
  propagates, as for `len` (S8.3.1v3). For a sequence literal,
  `len([x₁, …, xₙ]) = Σ count(xᵢ or 1)`: a written `null` is one item of a
  literal although a run of none is `null`. A list splices
  wherever it lands as an item, bound or not (S2.5.1v2, S2.5.6): with
  `let r = for (x in [2, 3]) x`, `len([1, r, 4])` is 4. An array counts as
  one item — `[for …]` keeps a collection whole. [§8.3 record, Design_Syntax
  §7.27]

### S8.4 Projections

- **S8.4.1v2** `keys(c)` ≡ `[for (k, v in c) k]`; `values(c)` ≡
  `[for (k, v in c) v]`; `names(c)` ≡ `[for (k at c) k]` — arrays: a
  projection of a persistent container is an array (S2.5.7v4), never a
  collapsing list (`keys({a: 1})` is `['a']`, `keys({})` is `[]`). Defined by
  the comprehensions they abbreviate; deliberately not built in until a call
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
  S12.2.1. [S8.2.1v4, S9.1.2, C5.3b]

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
- **S10.1.2v4** The pipe is dual-mode on a parse-time syntactic test: a body
  with a **free `~`** is a mapping pipe (binds `~` per item; `~#` is the
  current key/index); with no free `~` it is whole-value application
  (`data |> sum` ≡ `sum(data)`; extra args append: `data |> take(3)` ≡
  `take(data, 3)`). A `~`-free non-callable body is a type error. The mapping
  pipe walks what `for … in` walks — an array or list by item, a map by
  value, an element by attribute values then children (S8.1.2v2), text by
  code point (S2.5.8) — and its result is an **array** for every sequence
  source, a list included (S2.5.7v4: `(1, 2, 3) |> ~ * 2` is `[2, 4, 6]`),
  text excepted, which gives its own kind when every result item belongs to
  it (S2.5.8). A non-sequence scalar pipes as one value: `5 |> ~ + 1` is `6`.
  Dual mode belongs to `|>` alone: the filter stage `|:` (S10.1.6) shares the
  walk and the kind rule but is single-mode — its body must mention `~`. [C6]
- **S10.1.3** `~` is lexically scoped to the RHS of its pipe; nested
  constructs shadow, **innermost-wins** — uniformly for pipes, `|:` filters,
  `that` provisos, match arms, and `last` (S7.2.2); reach an outer item via a
  `let`.
- **S10.1.4** File write/append syntax is deferred; `output(data, file)` is
  the interim. [C6a]
- **S10.1.5v3** **`that` is the single-value proviso.** `x that p` is `x`
  when `p` — with `~` bound to `x` — is truthy, and **`null`** otherwise: a
  failed proviso is absence (S7.1), never an error, so `x that p` composes
  with the whole `?` family (`(x that p) ?? d`, truthiness in `if`) and is
  typed `T?`. The left operand is **one item whatever it is**; a collection
  is not walked — `xs that len(~) > 2` is all of `xs` or `null`. This is the
  same predicate, binding, and word as the type constraint `T that cond`
  (S11), with one difference of channel: the expression *asks* and answers
  with the value or nothing, the type contract *asserts* and fails as a type
  contract does. A bare name in `p` may read a field of `x` (S10.1.7v2). It
  sits at the pipe precedence tier, left-associative, so
  `c |> f that p` applies the proviso to the piped result. Filtering the
  members of a sequence is `|:` (S10.1.6): the former filter reading of
  `that` (S10.1.5v2) is superseded, and an old filter site `xs that p` is
  still legal under the new meaning — the fixture sweep converts every one.
  The filter was spelled `where` before `that` (S10.3.1v3). [Expr_Pipe §F.5]
- **S10.1.6** **`|:` is the filter stage of the pipe family.** `c |: p` walks
  exactly what `c |> body` walks (S10.1.2v4) — an array or list by item, a map
  by value, an element by attribute values then children (S8.1.2v2), text by
  code point (S2.5.8), a non-sequence scalar as one member — binds `~` to the
  member and `~#` to its key or index (S10.1.3), and keeps the members for
  which `p` is truthy. **Its result kind is the pipe's, by reference**, not a
  rule of its own: as S10.1.2v4 and S2.5.7v4 give the mapping pipe, `|:`
  returns an **array** of the survivors for every sequence source — a list
  included, so `(1, 2, 3) |: ~ > 2` is `[3]` and never collapses — `[]` when
  none survive (a map's keys are dropped, SO38); text gives its own kind,
  `""` when empty (S2.5.8); a non-sequence scalar gives itself or `null`. **`|:` is single-mode:** a body with no free
  `~` is a compile error — `xs |: is_even` is rejected, the idiom is
  `xs |: is_even(~)` — because `|>` reads a `~`-free body as whole-value
  application (S10.1.2v4) and the two pipes must not read the same text two
  ways; for the same reason its body never reads a bare name as a field
  (S10.1.7v2). Pipe precedence tier, left-associative: `c |: p |> f |: q` chains left
  to right. The spelling is set-builder "such that" (`{x ∈ S | P(x)}`,
  `{x ∈ S : P(x)}`), which it shares with the type-position `that`; the
  arrow is deliberately absent because a filter selects and does not
  transform. Static type: `T[]` for a source of element type `T` (range →
  `int[]`, map or element → `any[]`), text → its kind, scalar → `T?`; sound
  for a list source too, since the result is an array. [Expr_Pipe §F.2–§F.4,
  §F.6]
- **S10.1.7v2** **A body about one subject may leave `~` implicit; the pipe
  family always spells it.** A body that binds `~` to one subject, its
  current item, may read a field of it by the field's bare name, the OOP
  receiver convention. Such bodies are a method (S8.2.3), an object-level
  `that` constraint and a type constraint `T that cond` in any position
  (S11), the `that` proviso (S10.1.5v3), a `match` arm (S11.2.1), and a
  handler's value arm `e ^ { h } ~ { v }` (S7.6.1v4).

  So `user that age >= 18` and `match user { case map: age >= 18 }` both
  read `~.age`, and `~` may still be spelled. The current item is scoped to
  its body: a nested body binds its own, spelled or implicit, and when it
  ends the outer binding is back. In
  `order that (match kind { case map: total }) + total == 9`, the first
  `total` is `kind`'s and the second is the order's.

  Declared fields (a method, an object constraint) are names of the body's
  scope and shadow outer bindings. Elsewhere the current item has no
  declared shape, so a bare name is the member `~.name` (S12.3.3v2) only
  when no binding claims it: not a scope name, an `import math` constant, or
  a module or namespace prefix (`m.sqrt(x)`). An implicit read is a read
  only. It never supplies a callee — `xs that len(~) > 2` calls `len`, not
  `~.len` — and never a write target: `x = 1` in an arm assigns the
  variable `x` or fails, and never stores into the matched value.

  A pipe-family body (`|>`, `|:`) reads bare names as ordinary names, even
  nested in one of these bodies. `|>` tells mapping from application by the
  free `~` its text spells (S10.1.2v4), and `|:` must read that text the
  same way (S10.1.6). A field supplied behind the text would also let a
  misspelled `xs |: is_evne` pass E238 as a silent `[]`. For the same
  reason, a `~` that a nested body binds, spelled or implicit, is never free
  in an enclosing pipe body (S10.1.3): `xs |> match (1) { case int: ~ * 10 }`
  is whole-value application, not a mapping. [Expr_Pipe §F.7]

### S10.2 Vectorization

- **S10.2.1** Arithmetic `+ - * /` is vectorized (element-wise with
  broadcasting) — vector arithmetic is mathematics. Kind follows the operator
  rule of S2.5.7v4: a scalar operand leaves the kind alone (`(1, 2, 3) + 1`
  is `(2, 3, 4)`), an array or range operand gives an array
  (`(1, 2, 3) + [1, 2, 3]` is `[2, 4, 6]`). [C10]
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

- **S10.3.1v3** `and or not is in to div that at eq ne lt le gt ge` — Lambda is
  a keyword-operator language; new operators prefer words over sigils. `where`
  is **not** among them: binary `where` was retired as the filter spelling
  (first for `that`, S10.1.5v2; the filter is now `|:`, S10.1.6, and `that`
  is the proviso, S10.1.5v3), and `where` survives only as a `for`-header
  clause word. A `where` in infix position is the retired spelling and is a
  compile error naming `|:`, never a silent reinterpretation. [Grammar_Reduce2
  appendix; Expr_Pipe §F.3]

### S10.4 Parent navigation

- **S10.4.1*** Postfix `.~~` is the parent-navigation step at the ordinary
  member/index precedence tier; it chains left-to-right (`value.~~.~~.name`).
  A value domain or active traversal context supplies the parent relation;
  absence of one yields `null` and chains by S7.1.1v3. Path values use S2.4.2v5.
  A field named `.parent` remains an ordinary member, not a syntactic alias.
  [PTH3, PTH5, PTH9]
- **S10.4.2*** Bare `~~` is exactly `~.~~`: it is valid exactly where `~` is
  bound, selects the innermost current-value context, and counts as a free
  `~` for the S10.1.2v4 mapping-pipe test. Thus `~~.~~.a` means
  `~.~~.~~.a`; it never denotes a relative path, whose form starts with `\.`.
  [S10.1.2v4,
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
  relation yields `null` and chains by S7.1.1v3. [PTH26–PTH28]
- **S10.5.3v2*** Dynamic root navigation is occurrence-based. Its lineage
  lives in the evaluation context as a navigation path, cursor, or zipper; it
  never adds root/parent pointers to Lambda values, and node identity
  (S5.1.4v2) is not lineage. [S1.4, S1.6, S9.1, S9.3, PTH29, OB10]

### S10.6 Concatenation

- **S10.6.1** **`++` concatenates sequences and appends scalars.** Sequence
  `++` sequence concatenates, the kind following S2.5.7v4's operator rule: `(1, 2) ++ (3, 4)`
  is a list; any array or range operand gives an array. Sequence `++` scalar
  appends the scalar as one item and scalar `++` sequence prepends it:
  `[1, 2] ++ 3` is `[1, 2, 3]`, `(1, 2) ++ 3` is `(1, 2, 3)`, `3 ++ [1, 2]`
  is `[3, 1, 2]`; strings, symbols, and binaries are scalars here
  (`[1] ++ "ab"` is `[1, "ab"]`). Scalar `++` scalar is text concatenation:
  two values of one text kind keep it (`"a" ++ "b"` is `"ab"`, `'a' ++ 'b'`
  is `'ab'`, binary `++` binary is a binary); otherwise both render as text
  and the result is a string (`1 ++ 2` is `"12"`). `null` is `()` and so the
  identity: `x ++ null` and `null ++ x` are `x`. Maps and elements are not
  sequences — `m1 ++ m2` is an error; merge is `{*: m1, *: m2}` (S12.3.5v2).
  Paths keep S2.4.2v4's `base ++ relative_suffix`. [Design_Syntax §7.27]

---

## S11 Types and Patterns

### S11.1 Types compose like values

- **S11.1.1v3*** A bracket type is a structural pattern whose positions mix
  values, types, and occurrence runs freely: `[1, int, "str"]`,
  `[1, int*, 2]`; `[int]` is **exactly one int** (TypeScript's reading —
  forced by compositionality at n = 1) and enforced with a teaching message.
  `T[]` is the homogeneous-array contract — every logical element satisfies
  `T`, at any length, lists included since a list is an array (S2.5.1v2) —
  and `T[n]` is `T[]` with a fixed length, so `(1, 2) is int[2]`; repeated
  postfixes preserve rank (`T[][]` means an array of `T[]`, not a flattened
  leaf array; `int[2][3]` is three arrays of two). Counts on a *run* are the
  occurrence family (S11.1.6v2): a counted array is spelled `[T{n,m}]`, never
  `T[n,m]`. A lint applies to bare `[T]` in annotation position only. [C7]
- **S11.1.2v2** String structural patterns are delimited islands: `\( ... )`
  denotes a string-domain pattern and `\symbol( ... )` denotes a
  symbol-domain pattern. Inside an island, quoted literals are strings, `d`,
  `w`, `s`, `a`, `.`, and `...` are the reserved pattern atoms, whitespace is
  concatenation, and the existing union, grouping, occurrence (`? + *
  {n,m}`, S11.1.6v2), negation, and `to` rules apply. A pattern's tag is part of its type value: matching checks
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
  character-range member as a one-codepoint string. [S7.1.1v3, S11.2.1]
- **S11.1.4v2** The binary type relation is spelled `<:`. Its operands are
  type values and it returns `bool`: `A <: B` holds exactly when every value
  admitted by `A` is admitted by `B`. It is neither value membership (`is`)
  nor magnitude comparison (`<`); non-type operands are a type error. The
  relation is the conversion-free static subtype relation, not declared
  boundary compatibility: a boundary may convert a value, but conversion
  never makes a type's value set a subtype. It includes structural and
  nominal-base admission. Function-type variance is pending; until ruled,
  function-type operands are rejected. [S1.7, S6.1.1, S11.3.1v2, D3.2.1,
  D3.2.5v2]
- **S11.1.5v2*** **Three function types: `fn`, `pn`, and their union
  `function`.** `fn` and `pn` types are **disjoint** by effect bit;
  `function` is the base type of both. `f is fn` and `f is pn` test the bit,
  and `f is function` holds for every function value. A contract
  `fn (...)` admits only pure functions and `pn (...)` only procedures —
  admitting a `pn` at an `fn` contract would break S12.1.1v2's promise —
  while `function` or `function (...)` admits either. Since `fn` and `pn`
  are strictly narrower than `function`, a binder over a function value
  (S11.4.8v2) selects a coloured type, and the join of `fn` with `pn` is
  `function`. **A function type's signature always spells its parameter
  list**, empty or not: `fn () int`, `fn (x: int) int` — the shorthand
  `fn int` is retired. The return type is **optional**: `fn ()` and
  `fn (x: int)` constrain the parameters and leave the return `any`. When
  present, the return type starts on the line of the signature's `)`
  (S16.2.3v3), and it may itself be a signature:
  `fn (x: int) fn (y: int) int`. The `<:` relation over function-type
  operands stays pending under S11.1.4v2. [C20-1, C20.2; signature:
  USER 2026-09-24]
- **S11.1.6v3** **Two type families, split by concept: a run and an array.**
  The *occurrence* family — `T?`, `T*`, `T+`, `T{n,m}` (`T{n}` exactly,
  `T{n+}` at least) — describes a run of `T`s, which is
  what a list is; the *array* family — `T[]`, `T[n]`, `T[n][m]` — describes
  an array. In a **sequence-pattern slot** (inside `[…]`, `(…)`, a
  `\(…)` island, or an element pattern's content section) an occurrence is a run of the sequence's items and zero
  items is **void**: `[1, int*, 2]` matches `[1, 2]` and `[1, 5, 6, 2]`,
  never `[1, null, 2]` and never `[1, [5, 6], 2]` — an array item is one
  item, where a list would have spliced. As a **boundary type** (a binding,
  parameter, return, field, `is`, or `match` arm) an occurrence admits
  exactly the values a run *is* (S2.5.5v2): `null` for zero, a bare `T` for
  one, and a sequence of two or more `T` — a list, or its stored array image
  (S2.5.6) — for more. Hence `T?` ≡ `T | null`, the nullable type; `T*` ≡
  `T{0+}` ≡ `null | T | (T, T, …) | [T, T, …]`; `T+` ≡ `T{1+}`; and
  `{f: int*}` admits `{f: null}`, `{f: 5}`, `{f: [5, 6]}` but not `{f: [5]}`
  or `{f: []}` — those are arrays, `int[]`. The families coincide at two or
  more items and differ only where no list exists: `int{2}` and `int[2]`
  admit the same values, `int{1}` is a bare `int` while `int[1]` is `[int]`,
  `int{0}` is `null` while `int[0]` is `[]`; `T*` and `T[]` are incomparable.
  A parenthesized type list `(P, Q)` is a list pattern and collapses like a
  list (`(int)` ≡ `int`, `()` ≡ `null`); standalone it admits the list or its
  array image. Neither family changes a value's kind at a boundary: a list
  admitted by `int[]` or `int*` stays a list. The spellings `T[n+]` and
  `T[n, m]` are retired, and so is regex's open count `T{n,}` — the open bound
  is `T{n+}`, echoing the bare `+` that means one or more. Each retired
  spelling is rejected with a diagnostic naming its replacement rather than
  reading as a different type. **Element content is a sequence-pattern slot** (ruled 2026-09-25): an element pattern's content section, `<tag attrs; c, d>`, matches the element's normalized content (S2.6) as `[c, d]` matches an array — item by item, runs included, and the whole content — so `<ul; <li>*>` admits any number of `li` children and nothing else, and open content is spelled with a trailing `any*`. A pattern without a content section (`<div>`, `<div a: int>`) leaves content unconstrained; the spelling of *must be empty* is SO46. [Type_Pattern §1.3, Design_Syntax §7.28, Shape_Transitions §7]

### S11.2 Match

- **S11.2.1** `match` is a **type match**: arms are type expressions tried in
  order, first match wins, no fall-through; literal arms are singleton types
  dispatched by `==`; type arms dispatch via `is`; constrained arms add a
  predicate (`case int that (~ > 0):`). `~` is the matched value in every
  arm body, narrowed to the matched type, and a bare field name there reads
  `~.name` (S10.1.7v2) — no destructuring sub-language.
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
  (S2.1.1v4). `is` is type-directional: `3.0 is int` is false even though a deferred `int`
  boundary admits `3.0` (S11.4.5) — membership asks what a value *is*; the
  boundary asks what it may *satisfy*. [C7, TE-6, OB13]

### S11.4 Declared types are contracts

Full record: [`Lambda_Design_Type_Enforcement.md`](../vibe/Lambda_Design_Type_Enforcement.md) (TE-1–TE-18).

- **S11.4.1v3** An annotation is a contract on the binding, not a hint.
  **Three outcomes, never a fourth**: statically proven, statically
  rejected, or a deferred runtime check whose failure produces a rich error
  (boundary, expected type, actual value, location) — never null, never a
  wrong value, never silence. Failure never establishes the binding;
  reassignment checks before commit and leaves the old value unchanged. A
  `T[]` boundary validates/adopts every logical element before publishing its
  representation, and a typed array mutation validates its new element before
  changing the owner. An annotation may drive representation only if its
  boundary is enforced. An implementation may reuse an admitted array's
  representation proof only while the actual carrier matches that proof;
  otherwise it re-admits before optimized access. This internal choice never
  changes the contract, values, or failure behavior. [S7.7.2, S7.10.6,
  D3.3.3v3, TE-1, TE-2, TE-4, TE-9]
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
- **S11.4.8v2** A function signature may establish a **type binder** with an
  explicit `T: type` parameter, a parameter-contract site `B as T`, or the
  non-optional parameter shorthand `x: as T`, which elaborates exactly to
  `x: any ! error as T`. The shorthand is admitted only immediately after a
  parameter's `:`; it is not a general leading type expression. The bound
  name scopes over later parameter annotations, the return annotation, and
  the function body; reading it in the body yields its selected first-class
  type value. At each invocation the binder site admits its value against `B`
  before selecting its narrowest type (S4.2.2); a later `T` contract admits
  against that selection. Repeated `as T` sites in one signature join their
  selections through the promotion/nominal-base relation and never fabricate
  a union; incompatible bounds or no join are errors. A nested site binds the
  encountered subvalue, and an empty container falls back to its declared
  bound. Binders are valid only in `fn`/`pn` parameter contracts: a return
  contract may *refer* to an established `T`, but may not introduce one.
  These rules are identical at every execution tier. [TG2–TG7, TG9, TG13v2,
  TG15–TG18, D3.1.4v2]
- **S11.4.9** A system-function registry row may declare a **type relation**
  from one call argument to its success result. The relation is instantiated
  from the argument's inferred type and may preserve it, select its element
  type, or construct an array over it. Source notation such as
  `fn fill(n: int, value: as T) T[]` describes that relation, but does **not**
  create an invocation binder, a body-scoped `T`, a runtime check, or a
  specialization key: system functions have one native implementation.
  Unknown inputs retain the row's open fallback; transforms may preserve only
  the source families their runtime implementation actually preserves (for
  example, range transforms materialize arrays). Error behavior remains the
  row's independently declared effect. The relation can improve static
  inference but cannot affect an accepted program's evaluation. [S11.4.8v2,
  SI3v2, S17.2.1, D3.3.5]
- **S11.4.10*** **Verified at the crossing, valid while unchanged.** A
  declared type — on `let`/`var`, a parameter, a return, or a nominal binding
  of an element — is verified when the value crosses it, and the verification
  remains valid as long as the data is not changed. Under a nominal contract a
  required field is a required field: it is always present in an admitted
  value, while the container stays open beyond the declared prefix (S11.4.6).
  Structural composition is free — `<elmt …>` may carry any attributes and
  content, including errors (S7.8) — and only the nominal binding verifies.
  A value that does not fulfil its nominal binding **surfaces as an error
  value at the binding site** (the soft form of S11.4.2); the binding is not
  established. Host-built values are outside this rule until they cross a
  boundary that admits them. [TE-19, D3.2.6; 2026-09-17, user]

---

## S12 Functions, Effects, Resources

### S12.1 The one-bit effect system

- **S12.1.1v2** `fn`/`pn` is a declared, compiler-checked, one-bit effect
  system: `fn` is pure and deterministic under any schedule; `pn` may have
  effects; **`fn` can never call `pn`** — statically where the callee is
  known, and at run time where it arrives as a value (S12.1.4v3(6)). **A
  script's top level is `fn` context**: procedural code belongs in `main()`.
  The run-time check is confined to the calls that need it — never the
  statically resolved ones. Accepted price: no reified effects — a `pn` call
  executes; there is no held, unexecuted effect value. [Features §3.6, C20.7]
- **S12.1.2** `break`, `continue`, `return`, `while`, and `var` declarations
  are `pn`-only; using them in an `fn` is a compile error. `return` inside
  `for` exits the function; a function without an explicit `return` returns
  its last expression. [Procedural]
- **S12.1.3** Reactive templates are the doctrine applied: template body =
  pure `fn` transformation; mutation only in `on` handlers (`pn`) — the Elm
  architecture enforced by the effect bit. [Features §3.7]
- **S12.1.4v3*** **Effect polymorphism: the `function` declaration.** A
  function declared with the keyword `function` takes its colour from its
  arguments rather than declaring one — *pure iff its function arguments are
  pure*:

  ```lambda
  function apply_all(f: function, xs) => for (x in xs) f(x)
  ```

  (1) **Colour per call:** a call is `pn` if any argument bound to a
  `function`-typed parameter is a `pn`, and `fn` otherwise; parameters typed
  `fn (...)` or `pn (...)` keep their fixed colour and do not vote.
  (2) **The body is checked as an `fn` body** (S12.1.2 applies, and no
  statically-known `pn` may be called): its only effects are calls through
  its polymorphic parameters — which is what makes (1) sound. (3) The colour
  is resolved **statically** when every polymorphic argument's colour is
  known and checked at **run time** otherwise; an `fn` caller passing a
  statically-known `pn` is a compile error, and the error convention follows
  the resolved colour as for `call` (S12.3.4). (4) A closure that escapes a
  `function` capturing a polymorphic parameter is typed `function`, and
  calling it is resolved under (3). (5) **A `function`'s own value is `fn`**
  (S11.1.5): it adds no effect of its own, so it may cross an `fn (...)`
  contract; soundness comes from (6). (6) **S12.1.1v2 holds for dynamic
  callees:** in `fn` context a call through a value is checked at run time —
  a `pn` callee is refused, and a `function` callee refuses a `pn` in a
  polymorphic slot — returning the error value. `call(f, args)` is the
  built-in instance with one polymorphic parameter. *Colour what changes the
  caller's contract* — the keyword makes the dependence visible in the
  declaration. [C20-2–C20-6, C20.6, Features §3.6]

### S12.2 Assignment

- **S12.2.1** `let` bindings and parameters are immutable; only `var`
  reassigns; shadowing is allowed. An unannotated `var` may change runtime
  type on reassignment; an annotated `var` constrains every assignment to
  the declared type (S11.4.1). Implicit widening at annotated positions is
  by **exact embedding** only (S4.4.1) — never lossy coercion; assignment
  never silently corrupts data. [Proc_Assignment]
- **S12.2.2** Element mutation is defined on both faces: `elem.attr = v` as
  map-field assignment; `elem[i] = v` as child assignment, which keeps content
  normalized (S2.6.5).

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
  `null` (S7.1.1v3). The **member-call form** `x.name(...)` adds one more
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
  `inner` is itself variadic. Its colour follows `f` (S12.1.4v3). Three
  consequences follow from its being dynamic by construction, and are
  accepted rather than worked around: arity is checked at run time, not
  statically (S12.3.1 still bounds the callee's own slots); the result type
  is `any`; and the call takes the dynamic ABI, not a direct-call fast path.
  A `pn` target reached from `fn` context is an error — statically when `f`'s
  colour is known, at run time otherwise. Error convention follows the
  resolved colour: an `fn`-coloured `call` **returns** an error value, a
  `pn`-coloured `call` **raises**. `args` must be an array; any other type is
  an error.
- **S12.3.5v2** **Spread splices into containers, never into an argument
  list; the spelling follows the container's shape.** Positional containers
  take the bare operator — `[*a, 3]`, `(*a, 3)`. **Keyed** containers take it
  in key position — `{*: m, w: 5}` and `<div *: attrs, id: "x">` — since a key
  slot needs a key, and `*` is S16.8.6v3's any-key of the unit family. `*x`
  splices the items of a sequence (list, array, range), splices nothing for
  `null` (it is `()`), and places a non-sequence value as one item — that
  is, `*x` is the list of `x`'s items, so it splices where it lands and
  collapses by S2.5.5v2 — and `[*xs]` packages any value as an array:
  `null` → `[]`, `5` → `[5]`, `(5, 6)` → `[5, 6]`, `[5, 6]` → `[5, 6]`;
  spreading never modifies its operand. In argument position `*x` passes its operand as one value; the
  expansion was considered and **rejected** — argument in
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
  A shadowed builtin stays reachable from inside the shadowing module as
  `lambda.sys.f` (S17.2.2). [S1.11, S12.3.3v2, Design_Syntax §7.25]

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
- **S16.1.2v2*** `;` is a **strict separator** between statements — never a
  terminator, and `,` obeys the same discipline in every list. A separator
  sits between two items: trailing separators (`{ a; b; }`, `[1, 2,]`,
  `f(a, b,)`) and empty slots (`{ a; ; b }`, `[1, , 2]`) are syntax errors.
  No separator discards a value: a block's value is the list of its
  statements' results (S2.5.3). [Design_Syntax §3.2, §7.27]
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
- **S16.2.3v3*** After an **open-tail** statement (S16.1.3v2), a line-start
  **dual-role** token — one that could either continue the expression or
  start a new one — is a **syntax error**. The set is final:
  `( [ - + * ^ / < .` — the whole arithmetic family `- + * /` is banned
  uniformly rather than freeing `+` alone (S16.8.5). One position adds a
  name: after a function-type signature with no return type on its line
  (`type F = fn (x: int)`), a line-start **name** could be the return type
  or begin the next statement, so it too is an error. A keyword that cannot
  begin a return type — a statement keyword, or a declaration head such as
  `fn f` — starts the next statement as usual. Neither reading wins by
  default; the repair is explicit (`;` to separate, or move the token to the
  end of the previous line to continue). This is what makes S16.1.1 hold:
  the parser never guesses, so a line break can never silently split or
  merge. [Design_Syntax §3.1, §3.3, §7.29]
- **S16.2.4v3*** One carve-out: `.` followed by a step other than a digit
  — a name, `'sym'`, `*`, `**`, `~~`, or `/` — at line start is **member or
  path continuation**, sanctioning full leading-dot fluent chains and
  broken paths (`\.a` ⏎ `.~~` is `\.a.~~`): once the relative path was
  respelled `\.a.b` (S16.9.4), no statement can start with any of them, so
  the joined line means the same (S16.1.1). `.` is sub-classified, not
  retired: `.digit` stays dual-role, since `a.5` is member access with an
  integer field. [Design_Syntax §3.4, §7.15]
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

- **S16.4.1v4*** **Interior decides, wherever braces are an expression.** A
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
  value is the list of its statements' results (S2.5.3), and its `let`s are
  block-scoped (`let x = { let y = 1 y + 1 }`), which is what gives arrow
  functions block bodies without a JS-style `({...})` quirk.
  [Design_Syntax §5.9, §7.27]
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
  between S16.4.1v4 and this ruling: interior decides exactly where a
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
  S16.4.1v4's doctrine from brace-disambiguation to statement-ness.
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
  content (S2.6) rather than accumulating like a list.
  [Design_Syntax §7.23]
- **S16.7.2v2** **Nulls and empty strings are dropped (S2.6.2).** If
  normalization leaves the content empty, the script's value is a single
  `null`; that residual is the only null observable at top level. A null is
  observed by placing it in a value (`let r = [s.b]`), never by writing it as
  a bare statement.
- **S16.7.3v2** **Lists spread and adjacent strings or binaries merge (S2.6.3,
  S2.6.4)**, after dropping, so `"a" ⏎ null ⏎ "b"` yields `"ab"` — the
  dropped null does not keep its neighbours apart.

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
  one text node (S2.6.4) — document construction, not expression-level
  concatenation. Explicit concatenation is `++`. [Design_Syntax §7.9]
- **S16.8.5** **Unary `+` is kept** (identity, plus string→number
  coercion), and `+` stays banned at line start: the arithmetic family
  `- + * /` is banned as a class, not per token. [Design_Syntax §7.12]
- **S16.8.6v3** **`*` is spread; `*` and `...` are two wildcard families,
  not one.** `*` is the unit wildcard (path segment, any-key, `T*`
  repetition, spread); `T{n,m}` is the counted repetition — `T{n}` exactly,
  `T{n+}` at least (S11.1.6v2) — which retires `T[n+]`, `T[n, m]`, and
  regex's `T{n,}`: braces carry the count, and `+` inside them means "or
  more" exactly as the bare suffix does;
  `...` is the elided run (pattern gap, rest parameters), with the normative
  equivalence `...` ≡ `any*`. Paths keep `*`/`**` — an ellipsis would
  collide with path dots. [Design_Syntax §7.10, §7.28]
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
  must stay inert (S16.4.1v4). The keyed for-splice `{for … [k]: v}` is not
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
  S16.1.2v2 has no exception. [Design_Syntax §7.11]
- **S16.9.4** **The relative path is spelled `\.a.b`** (rooted `/.a.b`
  unchanged): `\` is the relative root as `/` is the logical one, and unlike
  `./a.b` the spelling does not collide with S10.5.1's postfix root step.
  Vacating `.name` is what widens S16.2.4v3. `import .mod` is unaffected —
  its keyword introducer leaves no ambiguity to escape.
  [Design_Syntax §7.15]
- **S16.9.5** **`a?: T` marks an optional field** — the whole field may be
  absent — which is distinct from `a: T?`, where the field is present and
  its value nullable. The marker applies in every type-field position:
  object-type fields, pattern position, and map-type items.*
  [Design_Syntax §7.22]
- **S16.9.6** **`.` is the only import separator**: `import .a.b` and
  `import a.b`. Neither `\` nor `/` separates import names (`import .a\b`,
  `import .a/b` and `import \a` are errors), so a module path reads the same
  as a dotted name. [Design_Syntax §7.15]
- **S16.9.7** **A rest parameter `...` closes a parameter list** — a
  declaration's or an anonymous arrow's alike
  (`(x, ...) => x + len(varg())`): one at most, with nothing after it.
  `varg()` reads the innermost callable's own rest arguments.
  [USER 2026-09-24, Function_Param §3]

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
  including in line-start member continuation (S16.2.4v3). Subscripts are
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

- **S17.2.1** **System functions live at `lambda.sys.*`, and the prelude
  imports them unqualified.** `len`, `sum`, `print` and their siblings are
  members of one built-in module; an implicit prelude import makes them
  available with no qualification, so ordinary code is unchanged. The
  qualified spelling addresses the same registry row — there is one owner of
  the builtin name list, not a parallel table. [D7.2.4]
- **S17.2.2** **`lambda.sys.f` is the escape from a shadow.** Where a module
  shadows a system function under S12.3.7, the qualified spelling still
  reaches the builtin: `let sum = 5` leaves `lambda.sys.sum(xs)` working.
  It needs no new syntax — it is an ordinary member access
  on an imported module (S12.3.3v2). The **`lambda` namespace root is
  reserved**: it may not name a binding (S16.10.1v2), so the escape can
  never itself be captured — unlike Python's shadowable `builtins`.
  Qualification is unnecessary for the reserved core (`int`, `string`,
  `type`), which S16.10.1v2 already makes un-shadowable. [S12.3.7, D7.2.4]

### S17.3 Process information

- **S17.3.1** **`sys.proc.self.argv` is the sole argument-vector path.** It
  returns an array of the live command-line strings after Lambda's startup
  option filtering, beginning with the executable name; a subcommand and
  script path remain in their command-line positions when present.
  No alias is defined. [Lambda_IO_Sysinfo §2; IL2-I25]

### S17.4 String search positions

- **S17.4.1** **A text search result uses its source's code-point index
  domain.** The `index` field of every `find` match in a string or symbol,
  whether from a literal or a pattern, is its zero-based start position, compatible
  with `index_of`, string subscripts, and `slice`: `find("éabc", "abc")[0].index`
  is `1`. UTF-8 byte offsets are internal only. [S2.5.8, S7.2.1, C15b.1]

### S17.5 Sized integer conversions

- **S17.5.1** **Sized integer conversions use the type names as callable
  constructors.** The spellings are `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)`,
  `u8(x)`, `u16(x)`, `u32(x)`, and `u64(x)`. `i64(x)` is the sole signed
  full-width conversion spelling.
  [S2.1.1v4, Type_Int_Sized §1 decision 5, IL2-I11]

---

## Appendix A — Implementation Footnotes

Status of `*`-marked rulings as of 2026-08-24. Conformance plans:
[`Lambda_Impl_Error_Handling (done).md`](../vibe/impl/Lambda_Impl_Error_Handling%20(done).md),
[`Lambda_Impl_Error_Rework (done).md`](../vibe/impl/Lambda_Impl_Error_Rework%20(done).md),
[`Lambda_Impl_Int_Total (done).md`](../vibe/impl/Lambda_Impl_Int_Total%20(done).md),
[`Lambda_List_Fixes (done).md`](../vibe/impl/Lambda_List_Fixes%20(done).md).

| Ruling | Status |
|---|---|
| S2.4.1v2, S2.4.2v5, S2.4.3v3–S2.4.4, S2.4.5v2, S10.4.1–S10.4.3, S10.5.1–S10.5.3 | Implemented for the current path/name scope on 2026-08-19, with the S2.4.3v3 spelling re-verified on 2026-08-28: maximal namespace-qualified element/attribute names, the undelimited relative-path element child `<svg \.rect>` (no `;`, no comma), logical `/.a`, relative `\.a`, absolute `file./.a`/`file.host.a`/`http.host.a`, root `./`, parent `.~~`, contextual `~~`, typed key operations, and interpreter/MIR Direct occurrence carriers. **S2.4.2v5 conformant as of 2026-09-21** (JIT and interpreter identical): the bare roots `/` and `\`, and `\` prints as the empty relative path; integer first keys `\.1` and `/.1`; index steps (`/[1]` ≡ `/.1`, `\[1]` ≡ `\.1`, `p[k]` ≡ `p.k`, numeric keys normalized through S8.2.1v4); key steps throughout a path literal (`\[1].name` is `\.1.name`, `\.a['name']` is `\.a.name`); integer steps after `.`, `\`, `./`, `~~` and `.*` (`\.1.2`, `file./.1`). `\.` alone and `.[` anywhere are E100. Broken path lines were checked against S16.1.1. That day's probe defect, `\.1.x` evaluating to `file./`, is fixed ([Type_Path §10.1](../vibe/Lambda_Type_Path.md)). Fixtures: `path_roots_steps.ls`, `path_line_continuation.ls`, `path_index_capture.ls`, `member_int_steps.ls`, `proc/return_relative_path.ls`, and four negative syntax fixtures. S16 harnesses: C parser 197/197, reference grammar 180/180. Lambda baseline 3632/3633; the one failure is an unrelated JS trace-parser test. Still deferred: the default resolver qualifies logical roots to local `file./`, and generalized immutable mount tables, remote transport, network hostname discovery, and S8.2.1v4 key normalization outside path subscripts are not built. |
| S4.8.1 | Float printer is not yet shortest-round-trip (`0.1 + 0.2` prints `0.3`). |
| S5.3.1 | `ArrayNum ==` is representation-sensitive in known cases — ruled a bug; also gates the data-processing engines (P0/FC8). |
| S5.4.3 | Element `==` defect (map-cast layout bug) — priority fix in the C8.5 bug list. |
| S5.5.1 | Function self-equality defect open; normalized-AST hash awaits `compile()` (S15.3). |
| S8.3.1v3 (element arm) | **Conformant as of 2026-09-03 (USER ruling).** `fn_len`'s element arm and the JIT's `fn_len_e` both answer attribute count plus content-item count, so `len(<e a:1, b:2, "t">)` is 3 and equals what `for (x in e)` walks — attribute VALUES first, then content items. Structural and nominal elements no longer disagree, and `len_iter_law.ls` pins the law. The child axis it displaced is now spelled **`content(e)`** (2026-09-04, USER): a read-only ARRAY VIEW that shadow-copies the element's items pointer and length without copying the slots, so `len(content(e))` is the child count and `content(e)[i]` the child walk. See [LR09-9](../vibe/Lambda_Issue_Ledger.md) for the ruling, the rejected `e.content` / `size(e)` spellings, and the migration. Baseline 4090/4090, GC stress 93/93. |
| S8.2.3, S12.3.3v2 | **Conformant as of 2026-09-03**, with one deliberate substitution. `lambda_object_member` is the single resolver for both member lanes — the ANY lane (`fn_member`) and the static lane (`item_attr`), which previously diverged: a bare `obj.m` bound on one and read `null` on the other. It resolves the key domain, then the type's own methods, then the base chain; `lambda_object_find_method` is the one walk. A bare `fn` method now yields a receiver-captured closure on both tiers (T0 binds through the new `interp_bind_object_method` seam, since an un-JITted method has no `compiled_fn`), and `obj["m"]` resolves identically to `obj.m`. `len`, `in`, `at` and the projections were already key-domain-only and are unchanged. **Gap:** S12.3.3v2 rules a bare `pn` method reference a compile error; it is not rejected at all today. The rejection cannot live in the runtime member lane — MIR lowers a `pn` method *call* by lowering its callee through that same lane, so refusing there makes the call a silent no-op on the JIT tier. It needs an AST flag plus a validation point in build_ast, which alone can tell a bare reference from a sanctioned callee. Tracked as [LR02-18](../vibe/Lambda_Issue_Ledger.md). Fixtures: `test/lambda/object_method_value.ls`, `object_method_receiver.ls`, `proc/object_method_write.ls`; baseline 4082/4082. |
| S2.1.1v4, S2.1.3v2, S2.1.4, S2.1.5, S5.4.2v3, S6.2.2v3, S11.3.1v2 | **Conformant as of 2026-09-03; S2.1.3v2's inheritance clause since 2026-09-25.** A derived object type had taken its kind from its own content section, so `type U : B { extra: int }` under an element-kinded `B` built maps and `type V : P { string* }` under a map-kinded `P` built elements; a derived type now keeps its base's kind and inherits its content pattern unless it declares one (OB7), and content on a map-kinded base is a compile error (OB17). Nominal-ness is a property of the type descriptor, carried by a `TypeNominal` record allocated once per declaration and cached by an `is_nominal` base flag (D2.6.6v2, D2.6.11). A nominal value wears its DECLARED structural kind, so an attribute-only type yields maps and a type with a content pattern yields elements, and `is object` / `is map` read as the independent axes S2.1.1v4 rules. Nominal sameness is record identity rather than name equality, so two modules' `Point`s stay distinct while every shape grown from one declaration still answers `is T` — which is also what makes S2.1.4 part 3 work: an undeclared field grows the shape and the grown shape points at the same record. The object TypeId is gone from the enum entirely; `object` survives as a TYPE matched by pointer identity. Fixtures `test/lambda/object_nominal.ls` and `proc/object_open_instance.ls`; baseline 4085/4085, exact tier parity, stable under forced GC. Still open: schema-driven input producing objects (S2.1.3v2), and the S12.3.3v2 bare-`pn` rejection tracked as [LR02-18](../vibe/Lambda_Issue_Ledger.md). |
| S2.1.1v2, S5.4.2v2, S6.2.2v2, S8.1.2v2, S8.2.1v4, S8.3.1v3 (object arm) | **Shipped state under the superseded v2 rulings (2026-09-03).** `entity` is retired from all three keyword tables (C lexer `base_types`, `grammar.js` `_base_type_kw`, `is_type_keyword`) and the reference grammar is regenerated; `let entity = 1` is now legal, where it was `error[E201]`. Objects carry content (D2.6.6 — `Object` is an alias of `struct Element`) and conform across the whole surface: `len` is attributes + content, `in`/`at` walk attribute values then children, an IntKey subscript selects a child, equality is nominal type + unordered attributes + ordered content, ordering is type name then attributes then content, both clone paths copy content, and printing emits round-trippable `<T a: 1, "child">`. Three pre-existing defects were fixed on the way: `item_keys` had NO object arm, so `for (v in obj)` yielded nothing while `len(obj)` reported the field count; object equality compared attributes only, so two objects of DIFFERENT nominal types with matching fields compared equal; and ordering likewise ignored the type name. Fixture `test/lambda/object_content.ls`; baseline 4083/4083, exact tier parity. |
| S2.1.3 (v1, shipped state; superseded by S2.1.3v2) | **Partially implemented 2026-09-03.** Content patterns, the `<T attrs, content>` literal on both tiers, content-pattern inheritance, and tag-name output (markup formats emit `<TypeName …>` via the shared element handler; JSON keeps its `"@"` type key and gains the element `"_"` content key) all work. **Not implemented:** schema-driven input — `input(doc, schema: …)` and a document declaring its own schema still yield structural elements, never objects — and object construction from a schema is unverifiable end-to-end today ([LR13-9](../vibe/Lambda_Issue_Ledger.md#lr13-9)). The **content-arity check is no longer missing**: `validate_against_element_type` enforces `content_length` (`validator/validate.cpp`, both the fast verdict and the reporting path), and since the flip a content-bearing nominal type wears `LMD_TYPE_ELEMENT` and so dispatches into it. Validator tests now load, parse type annotations, and register direct-parser type declarators; the `validate` CLI reaches real validation for the shipped `schema_comprehensive.ls` + `test_data_valid.json` pair instead of failing root lookup. That pair currently reports its independent `element`-versus-`map` mismatch. LR13-3's root-*selection* policy remains open, and these targets still sit in `test-lambda-full`, not `test-lambda-baseline`. |
| S5.1.4v2, S9.1.5v2, S10.4.3v2, S10.5.3v2 | **Ruled 2026-09-03, not implemented.** No container carries a node identity and no `===` exists; the DOM package compares wrappers structurally (`test/lambda/proc/dom_api_core.ls`). The carrier, the id-preserving operation set, and the universal addressing scheme are open (SO39, DO25). |
| S6.1.1 | `fn_lt` uses `strcmp` (NUL-unsafe) and accepts symbols; two-layer invalid-comparison treatment not landed. |
| S6.2.1 | `sort()` coerces to float (`sort(["b","a","c"])` → `[nan,nan,nan]`); total order not implemented in `sort`/`order by`. |
| S7.1.1v3, S7.1.3v2 | Core computed array/map/element reads now return `null` for invalid keys and writes return the hard `ItemError`/`T^` channel; typed-array and mask paths share the same checked key boundary. A broader access-site audit remains for specialized editor/host surfaces. Slice-offset rules (RF3D) landed with regression tests. |
| S7.2.2–S7.2.4 | `last` keyword, `limit last N`, and `{limit:}/{last:}` options not implemented; ArrayNum negative-index audit outstanding. |
| S7.3.1 | Strict null propagation + `skip_null` option pending. |
| S7.4.4 | Skip-edge errors currently surface the bare `ITEM_ERROR` singleton — rich payload pending. |
| S7.6.1 | The one- and two-arm postfix handler grammar and MIR/interpreter lowering conform to S7.6.1v4/S7.6.2v3/S7.6.6v2, including nested `^`/`~` scope restoration, direct raised-`pn` outcome routing, and rich-error preservation. |
| S7.6.5 | Retired `^err` destructuring and prefix `^expr` error tests are removed from the grammar, AST/runtime, and active `.ls` corpus. The handler-local `^` remains scoped to the selected error arm. |
| S7.6.7 | Landed 2026-08-17: statement-position `pn_call() ^ { error_body }` uses explicit ordinary completions before and after suspension; durable native-fault targets cover the S7.11 carve-out without retaining a recovery frame or jump buffer across a yield. Value-producing handlers over possibly-suspending `pn` calls remain rejected. |
| S7.7.1–S7.7.6 | TE-18 declaration-boundary skip pending (routing, case-7 tiers, edge sites). `for x: T in e` does not parse yet — case 1 is `let`/`var`-only until the grammar is extended. |
| S7.8.1 | TE-17 lane gating pending (predicates exist, gate does not). Known violation V1: `fn_array_set` silently despecializes a declared `int[]` — the dominance invariant (S7.7.2) is false today. The `may_defect` effect split must land before routing or every unanalyzed call costs a native lane. |
| S7.10.5v2 (v1 residue) | RF5 audit: several vectorized ops return generic arrays where typed `ArrayNum` is required; a few error-channel violations open (`query`, `url_resolve`, invalid `push`/`splice`). |
| S7.1.1v3, S7.10.5v3 (numeric functions, unary operators) | **Conformant as of 2026-09-23 (USER ruling), both tiers.** A `null` argument makes a numeric function's result `null` -- `math.*`, `abs`/`round`/`floor`/`ceil`/`trunc`/`sign`, the scalar `min`/`max` pair, `clip` and `**` -- and so does the unary `-`/`+`; every one of them had returned `error`. The JIT's native libm, rounding and unary lowerings now test the argument's lane null: they had computed on the sentinel's bits, giving NaN for a `float[]` read out of range and, for an `int[]` read, the routine applied to 0.0 (`math.sqrt` gave 0, `-a[i]` gave -inf). `a[i] / 2` on an `int[]` read is null, not 0; a nullable float result boxes as null wherever it escapes (it printed as NaN, typed `int`); and a failed reassignment into a declared `float` is reported naming the binding on both tiers (S7.7.4). A `REAL_TO_FLOAT` sys-func result is typed `float` only when every argument is real (`math.pow(2, [1, 2])` had been typed `float` and unboxed as NaN). Fixtures: `proc/null_numeric_propagation.ls`, `negative/runtime/null_numeric_reassign_float.ls`. **Residue:** an untyped binding of a nullable float result (`let x = v[i] + 1.0`, or `var t = 0.0` reassigned `t + v[i]`) still loses the null on the JIT, because arithmetic inference drops D2.5.3's `?` and nothing marks the binding's lane nullable. |
| S7.11.4 | Exec recovery implemented on POSIX. **Blocking hazard H1**: batch mode overwrites the stack-overflow handler, so fault capture differs between batch and standalone runs. Windows SEH never exercised. |
| S8.2.1v4, S8.2.2v2, S9.1.6 | Core MIR Direct and AST-interpreter computed access now enforce fixed array/map/element key domains (the v4 object face is not built — see the S2.1.3v2 row), including exact integral float/decimal normalization, empty-string names, and no array-to-map promotion. VMap additionally admits its two canonical NameKey/IntKey classes and rejects fractional/poison keys. Specialized editor/host access sites still need the same audit. Empty-string map keys are now semantically valid, but their known JSON round-trip corruption remains to be fixed. `at` membership now conforms: `1 at [10,20,30]` is false, matching S8.2.2v2 (this row previously recorded it as still true). |
| S8.1.3 | **Conformant as of 2026-08-24.** The paired `at` form bound both names to the key (a silent wrong answer); fixed in `build_ast`, one fix covering both tiers. Full record: [LR02-R9](../vibe/Lambda_Issue_Ledger.md). |
| S8.3.2 | Streams (and hence stream `len`) not implemented. |
| S9.1.3, S9.1.4, S9.2.2–S9.2.4 | COW Stage 1 landed (`let`-finality real for Array/Map/Object/Element/VMap — **and, as of CW32v2 2026-08-29 on `nm-impl-work`, for plain ArrayNum**: binding aliases are O(1) mark-and-share, the eager bind clone is retired, marked roots' lane stores consult the shared bit once per store, and mask writes go through a preparing wrapper; fixture `cow_arraynum_alias.ls`, exact tier parity; mutable write-through views deliberately excluded — open/todo). Stage 2 pending: exclusivity checks (faces 1+3+4 landed, face 2 unreachable behind `E229`), capture-assignment compile errors, view-borrow confinement. The **module-`var` half of S9.2.4 needs no work** — it is vacuous by construction (S9.2.4v2); only the view-state half is outstanding. `var` params parse and mutate the caller's value today, but a *plain* param does so too — the snapshot half of S9.1.3 is **UNCONDITIONAL since the 2026-08-29 flip** (escape hatch retired; `is_proc_param` deleted) (CW29, COW doc §11.9; current tree): both tiers snapshot mutated plain params — flat, nested-path, and array writes all stay local (fixture `cow_param_snapshot.ls`); `var` is the sole write-through construct. Migration outcome: the 88-script sweep ceiling collapsed to **13 actual reliance sites** (7 ABI-pinning proc tests, 6 benchmarks — the SOM PRNG/out-param idiom), all migrated to `var` with goldens unchanged. **Mutated place-copy binds mark their value** (`var row = m.rows[i]` followed by a write through `row` is a true S9.1.2 snapshot on both tiers: the first write detaches), closing the get-modify aliasing half of C4.1; an UNMUTATED place copy stays a borrow — observationally identical to a copy (P6) — and expression-position reads still borrow, unobservable since no write occurs through an unnamed temporary. With CW32v2 landed, ArrayNum-through-plain-param snapshots too (probed both tiers); the residual write-through is only the declared typed-array *native-witness* path, whose raw pointer feeds a native body. |
| S9.2.3 | **Implemented on both tiers (CW30, 2026-08-29; on worktree branch `nm-impl-work`, pending merge).** The decision is compile-time and tier-shared (`AstLoopNode::snapshot_collection`, computed once at for-node completion): only a loop whose source roots at a mutable binding AND whose body may write that root pays — a head share-mark plus, in MIR, an independent rooted handle the loop walks (the call's result register, which is what dissolves the earlier register-aliasing blocker: the binding's register is free to take the detached replacement). Non-mutating loops emit nothing on either tier. Fixture: `test/lambda/proc/cow_snapshot_iteration.ls` (element write, push, map field, `var`-borrow-in-body, multi-level, clean loop) — identical output on JIT and T0; pre-change behavior was live iteration (`99` observed). |
| S9.2.4v2 (view-state half) | Not implemented — see the exclusivity row. |
| Exclusivity (S9.2 / CW §11.3) | **Two of four faces effectively hold.** Face 1 (two `var` args naming one variable) rejects via `E211`. Face 3 (path-prefix) rejects at the *conservative whole-base* granularity the design sanctions as its Stage-2 v1 — so `f(var t, var t.a)` is caught, and `f(var t.a, var t.b)` is caught too though the ruling would permit it. **Face 2 (receiver vs `var` arg) is unreachable**: a `pn` method with a `var` parameter cannot be dispatched at all (`E229`, "dynamic dispatch of a function with `var` parameters is deferred"), so the check would be dead code until that lands. The ratified path that lifts `E229` is CW33 (COW doc §11.10, 2026-08-29) — the `var`-param address ABI, under which this check becomes a slot-address compare in the callee prologue. **Face 4 closed at whole-base granularity (CW31, 2026-08-29; worktree branch `nm-impl-work`, pending merge)**: a `subview` binding records its ultimate base (`NameEntry::view_base`, chased through view-of-view), and the call-site check conflicts two `var` args sharing an effective root — overlapping subviews and view-vs-base both reject via `E211`; views of distinct bases pass. Disjoint tiles of one base also reject: the same sanctioned false positive as face 3, with the splitter ladder unchanged. Fixture: `test/lambda/negative/semantic/var_view_overlap.ls`. |
| S9.3.1 | **UNCONDITIONAL since 2026-08-29** — insertion capture is applied at the specified container insertion points on both tiers, and `LAMBDA_COW_CAPTURE` is no longer consulted. The implementation marks named values at capture sites; freshly produced containers are not marked because they have no second observer at that insertion point. Plain-parameter snapshots under **S9.1.3** are unconditional as well, with `var` remaining the sole write-through construct. Full implementation record: [LR12-R9](<../vibe/Lambda_Issue_Ledger (fixed).md#lr12-r9>). |
| S10.2.2, S10.2.3 | `eq ne lt le gt ge` operators and the `vec_cmp` revert not landed; mask-consumption functions deferred. |
| S11.1.1v3 (v2 core) | `T[]` and nested `T[][]` contracts are implemented at annotation and parameter boundaries, including scalar, sized-scalar, pointer, string, named-map, and nested lanes. Since 2026-09-23 (both tiers) ArrayNum views and N-D arrays also cross contracts with no exact packed lane (`number[]`, `float[][]`, `int?[]`, unions): admission presents their elements -- scalars, or leading-axis rows -- as a snapshot copy (S9.2.2), where it had rejected everything `is` accepted; and the JIT's `len` of an annotated N-D binding counts rows, not leaves. Fixture `proc/array_view_admission.ls`. **Counted axes enforced at typed boundaries since 2026-09-23 (both tiers):** once P5 made `T[n]` an array layer, the admission fast paths (lane, rank and certificate reuse) proved it from the leaf lane and rank alone, so `int[3]` admitted `[1, 2]` and `int[2][3]` any row count or row length. Admission now checks every counted axis first; a counted certificate re-checks the live lengths on every reuse (D3.3.3v3), since a push or splice changes them without a boundary, and the JIT no longer elides a counted contract's boundary on the strength of a binding's or record field's declared type. Fixtures `negative/runtime/array_count_*.ls`, `type_counted_rank.ls` §5. **Residue:** a record whose field is counted keeps its trusted shape across a push through that field, so a later record boundary admits the stale length (both tiers); whether such a push should itself raise (S7.8.1) is unruled. General structural array-pattern composition and the `is [T]` inline parse crash remain open. |
| S11.1.5v2 | **Colour half conformant as of 2026-09-18; the v2 signature syntax as of 2026-09-24.** `pn`/`pn (...)` parse in type position (C parser, type-pattern parser, Tree-sitter); `is fn`/`is pn`, `match` arms and parameter admission test the colour through one runtime rule, and the static boundary rejects a known wrong colour (E207) while deferring a `function`-typed source to the runtime check; system procedure references carry their signature on both tiers. Fixtures `test/lambda/proc/fn_pn_function_types.ls`, `negative/semantic/fn_pn_colour_mismatch.ls`, `negative/runtime/fn_pn_colour_mismatch.ls`. The v2 signature landed in the C statement parser, the type-pattern parser and Tree-sitter: `fn int` is rejected, `fn ()` and `fn (x: int)` parse with an `any` return, a signature may return a signature, and a line-start name after a return-less signature is E100 (S16.2.3v3). No corpus file used the retired shorthand. **Residue:** a binder over a function value still selects `function` (blocked on SO44); hosted (JS) function values read as `fn`; `function (...)` does not parse in type position in either front end; a declaration cannot spell a signature as its return type (`fn make() fn (y: int) int {…}` is rejected by both front ends — an alias works); a suffix on a function type is SO45. |
| S11.2.3 | Match exhaustiveness checking unverified in the implementation. |
| S11.4.3 | `any \ error` has no working surface spelling (the `!` exclusion operator is broken for general types); it exists as the unwritten default only. |
| S11.4.5 | **Conformant (verified 2026-09-23, both tiers):** an ANY-held `3.0` crossing an `int` boundary is admitted as `3`, and `3.5` fails with E201. The type-directional reject this row used to record is gone. |
| S11.1.6v2, S11.4.5 (native nullable lanes) | **Conformant as of 2026-09-23, both tiers:** a `float?` or `float \| null` contract admits an int as `float` does, re-represented (`5` becomes `5.0`), and `int?` admits an exactly integral float (`2.0` becomes `2`). The JIT had failed MIR verification on `let x: float? = 5`, and T0 had kept the int. A union of several numeric arms still admits by membership (`5` stays an int in `int \| float \| null`). Fixture: `proc/nullable_float_lane_admission.ls`. **Residue:** the sized and wide optionals (`i8?` to `u32?`, `i64?`) still admit by membership on both tiers, so `let t: i32? = 5` is rejected where `let t: i32 = 5` is admitted. |
| S11.4.6 | Constrained-type `is`/`fn_is`/validator divergence open; base-only interim is the shipped behavior. |
| S11.4.10 | Ruled 2026-09-17 (user). The boundary check and error-value surfacing exist; the "valid while unchanged" half is not exploited: the typed lane re-verifies presence and layout on every access (D3.2.6, D3.2.4v4 footnotes). |
| S12.1.4v3 | **Largely conformant as of 2026-09-18**, with S12.1.1v2 enforced statically in every `fn` context (module top level included) and dynamically at colour-guarded call sites only: an `fn`-context dynamic call whose callee is not statically `fn` checks it before dispatch (JIT: `lambda_fn_colour_guard_args`/`_list`; T0: the same rule in `eval_call`), while static calls, `pn`-context calls and `lambda_dynamic_call` itself carry no colour work. `function` declarations parse in both front ends (C parser; Tree-sitter `fn_stam`/`fn_expr_stam`); bodies are checked as `fn`; a post-build pass resolves each `fn`-context call's colour, rejecting a statically `pn` one (E224) and marking the rest with `LAMBDA_COLOUR_GUARD_*` bits; run-time checks ride the parameter-error short-circuit on direct calls and a consumed `Context::fn_colour_guard` word on dynamic dispatch, identically on both tiers. Fixtures `test/lambda/proc/function_colour_poly.ls`, `negative/semantic/function_colour_static.ls`, `function_body_is_fn.ls`, `function_body_var.ls`. **Residue:** (4) is conservative — a closure inside a `function` calling a captured polymorphic parameter is checked as plain `fn`, so it refuses a `pn` even when called from `pn` context; pipe-to-callable (`x \|> f`) and system-HOF callbacks (`map(f, xs)`) are not yet guarded; `function` object-type methods are not parsed. Since 2026-09-22, every system-function procedure is rejected statically in `fn` context. That covers built-in rows (`print`, `output`, `cmd`, `today`) and host-module `pn(...)` Jube signatures such as every DOM effect (D7.4.6) ([LR12-30](../vibe/Lambda_Issue_Ledger.md), fixed). |
| S12.4.1–S12.4.3 | Resource model R1–R5 designed, not implemented. |
| S13.1.3v2 | Task mode and the ordinary `start(target, args, options)` call surface are implemented (2026-08-19). Thread/process modes are recognized and rejected as not implemented; process remains first, thread gated on the isolate-state audit and open item O-D. |
| S13.4.1, S13.4.2 | Pairwise reductions decided, not implemented (sequenced before concurrency work); stream parallelism pending with streams. |
| S14.2, S14.3 | Group-by and joins (S14.1) are implemented; verbs, `over(...)`, DataFrame, and the whole stream/plan system are pending (phases P3–P8). |
| S15.3 | `compile()`, closed environments, and `quote` unimplemented; C9 grammar worklist open (general expression children). |
| S16.1–S16.6 (all) | **Conformant on the S16 harness as of 2026-08-24** (C 123/123, Tree-sitter 118/118). S16.2.4v3 was verified on 2026-09-21 in both front ends, with S16.1.1 checked by joining each broken path line: C 197/197, Tree-sitter 180/180. S16.2.3v3's signature case landed on 2026-09-24 in both front ends (C `error_signature_return_line_start`, Tree-sitter's zero-width `_fn_return` guard): C 302/302, Tree-sitter 285/285. The harness is a case sample, not a proof of total conformance, so the `*` marks stand. Residue: O3 (sibling Tree-sitter scanners), §7.17 (comment vs line-start guard, benign), and the O4 doc sweep — all in [Design_Syntax §4.5/§6](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep). |
| S16.4.1v4 | **v2 core conformant as of 2026-08-22; the v3 computed-key head (one balanced `[…]` group before the colon) is not implemented — tracked with S16.8.9.** Two inverse flips were fixed in `lambda/runtime/parser/lambda_parser.c`: `if_statement_body_is_map` bailed out on a `(` head (so the paren spelling rejected every map body in statement position), and `parse_for_expression` gated the map reading on `parenthesized` (so the *bare* `for` spelling rejected one the paren spelling accepted). Both spellings of `if` and `for` now agree; `while` correctly stays always-block per S16.4.3. |
| S16.4.2 | **Conformant as of 2026-08-22.** `control_body_brace_is_map` breaks the empty-brace tie in `if`/`for` bodies from `procedural_depth`; that depth now tracks the enclosing function's *effect kind* rather than a nesting count, so a `fn` inside a `pn` is fn context, and an arrow body is forced to fn context so `() => {}` mid-procedure is still the empty map. Verified across value, content, `if`, `for` (both spellings), arrow, and `pn` positions, plus fn-in-pn and arrow-in-pn nesting. |
| S16.6.6, S16.6.7 | **Conformant in both front ends as of 2026-08-24** (C 140/140, Tree-sitter 135/135, zero corpus movement). Enforcement mechanics and findings: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep) and §6 point 35. |
| S16.6.8, S16.6.9 | **Conformant as of 2026-08-24** (`E312` in `build_ast` per S16.6.5; C 152/152, Tree-sitter 135/135, baseline 3868/3868). Classifier subtleties (three-way recursive `ast_branch_kind`, NEUTRAL empty branch) and migration: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep) and §6 point 38 addendum. |
| S16.8.4, S16.8.8, S16.9.2 | Not probed against the implementation; the `*` is precautionary, not a known defect. S16.9.4 was probed on 2026-09-21 on both tiers and in both front ends, and ships unmarked. S16.8.1–S16.8.3, S16.8.5–S16.8.7, S16.9.1, S16.9.3 were spot-checked conformant on 2026-08-22 and ship unmarked — including the S16.9.3 element boundary-comma biconditional in all four of its cases. |
| S16.9.5 | **Parsing conformant as of 2026-08-25; the field/value distinction is not yet represented.** Residue: the marker wraps the field type in `OPERATOR_OPTIONAL` — the same representation `a: T?` produces — so the two spellings this ruling calls *distinct* are indistinguishable downstream until `ShapeEntry` carries a field-level flag; independently, the declaration binding checker treats an optional field as required for both spellings (`error[E205]`, pre-existing). History: [Design_Syntax §4.5](../vibe/Lambda_Design_Syntax.md) (2026-08-24 sweep). |
| S12.3.7 | **Conformant as of 2026-08-27.** Module-local bindings win over same-named system functions, including non-callable shadows, and the compiler emits the required warning; explicit `lambda.sys.*` qualification remains the escape from that shadow under S17.2.2. Regression and implementation record: [LR02-15](<../vibe/Lambda_Issue_Ledger (fixed).md#lr02-15>). |
| S2.5.7v2, S7.10.1v3, S7.10.5v3 | **Implemented 2026-09-25 on both tiers.** Reverses the list half of P2 of the list fixes (row above): every function and pipe returns an array for a list input. `take((10, 20, 30), 1)` is `[10]` and `take(…, 0)` is `[]`; `sort`/`reverse`/`unique`/`take`/`drop`/`slice`/`[i to j]`/`zip`/`fill`, the vectorized sys funcs (`math.pow` included, though it shares `**`), `\|>` and `\|:` finish through `seq_finish_array`, which clears the spread bit. `+ - * /`, unary `-`/`+`, the mask comparisons and `++` keep P2's operand-kind rule (`seq_finish_kind`). The checker lost its list branches too: `pipe_collection_result_type` types a list source's pipe and filter as arrays, and `SYS_RESULT_SELECTION_OF_ARGUMENT`, added by P2 only to leave a list selection open, is gone (`unique`/`take`/`drop` are collection transforms again). `varg()`, `content(e)`, `split` and `find` already returned arrays. The P2 fixtures that pinned list results flipped to arrays: `list_kind_transform`, `list_collapse_void`, `pipe_that_kind`, `pipe_spread`, `query`. Verified: `make test-lambda-baseline` 5902/5904, both failures environmental (`proc_markup_mutation` run from a worktree; a JS stdout/stderr interleaving flake that passes standalone); every golden 968/969 with the tier pinned to `jit` and to `interp`; the corpus compile scan unchanged. **Residue:** the set operators `\| & !` keep P2's rule (a list only for two lists), which S2.5.7v2 does not settle (SO48). Design record: [Expr_Pipe §F.6](../vibe/Lambda_Expr_Pipe.md). |
| S10.1.2v4, S10.1.5v3, S10.1.6, S10.1.7v2, S10.3.1v3 | **Implemented 2026-09-25 on both tiers.** The lexer takes `\|:` by longest match beside `\|` and `\|>`; `grammar.js` gains a `\|:` row on the pipe tier beside `that`. `OPERATOR_FILTER` (`\|:`) and `OPERATOR_THAT` (the proviso) replace `OPERATOR_WHERE`. A `\|:` body with no free `~` is E238, by the same test `\|>` uses for its mode (`has_current_item_ref`). The proviso binds `~` as a constraint binds its candidate, with `~key` null, and is typed `T?`. The infix-`where` diagnostic names `\|:`. A called bare name is never an implicit field, so `xs that len(~) > 2` calls `len`; the old rule read it as `~.len` in every `that` body. Migration: all 133 `that`-filter sites in 27 files (fixtures and the `chart`, `openapi` and `latex` packages) became `\|:` with byte-identical output on both tiers, except `that_implicit_name.ls`, rewritten for the proviso. Fixtures `pipe_filter.ls`, `that_proviso.ls`; negatives `filter_body_no_current.ls` (E238), `where_filter_retired.ls`, `pipe_filter_glued_case.ls`. A list source gives an array through both pipes, `(1, 2, 3) \|: ~ > 2` being `[3]` (the S2.5.7v2 row). Implicit fields follow S10.1.7v2. S10.1.7 was ruled 2026-09-25 and revised the same day as S10.1.7v2, which added match arms, handler value arms and type constraints and closed SO49 and [LR02-28](<../vibe/Lambda_Issue_Ledger (fixed).md#lr02-28>). The resolver's `in_that_clause` is on in every body that binds `~` to one subject: the proviso, an object-level or type constraint, a match arm, a handler's value arm. It is off in a `\|>` or `\|:` body, a match pattern, and a write target (`x = …`, `put`/`del`). Each body restores the outer setting when it ends, as `~` itself is restored. An `import math` constant, and a module or namespace prefix spelled as a member's object (`m.sqrt(x)`), resolve before the implicit read. `has_current_item_ref` now stops at a nested `\|>`/`\|:`/`that` body and at match arms, as it already did at a handler's value arm, so a `~` that a nested body binds never makes an enclosing `\|>` a mapping. This reverses [LR02-5](<../vibe/Lambda_Issue_Ledger (fixed).md#lr02-5>), which mapped `xs \|> match (1) { case int: (~) * 10 }`. Fixtures: `implicit_current_item.ls` (each body, nesting and restore, pipes, resolution order), `that_implicit_name.ls` §7 (before the fix, `user that len(items \|> price) == 2` read `price` as `~.price` of each item), and `match_arm_current_item.ls`. Verified on the merge with master: `make test-lambda-baseline` 5924/5925, the one failure environmental; every golden 978/979 with the tier pinned to `jit` and to `interp`. The corpus AST scan (2,025 files) changed no file outside those fixtures except `object_constraint_fail.ls`, whose field constraint now reads `~.value` but is never evaluated (S11.4.6). **Residue:** S10.1.2v4's type error for a `~`-free non-callable body is not checked, and the tiers disagree at run time: T0 fails and the JIT returns the body's value ([LR02-29](../vibe/Lambda_Issue_Ledger.md#lr02-29)). A binding from a proviso aliases its operand without copy-on-write, as `or` and `if` do ([LR12-35](../vibe/Lambda_Issue_Ledger.md)). Design record: [Expr_Pipe §F](../vibe/Lambda_Expr_Pipe.md). |
| S16.8.9 | **Ruled 2026-09-05, not implemented.** Grammar `_key` (covers `map_item` and `attr_name`), the C parser's `parse_map`/`parse_element` and the two lookahead predicates (`braced_expression_is_map`, `element_attribute_starts`) that must skip one balanced `[…]` group, and a computed variant of the key-expression AST node lowered on both tiers through the keyed-spread path. Design record: [Design_Syntax §7.26](../vibe/Lambda_Design_Syntax.md). |
| S17.2.1, S17.2.2 | **Conformant as of 2026-09-08.** `lambda.sys.*` resolves to the existing system-function registry, including the S12.3.7 shadow escape; `lambda.math`/`lambda.io` share the built-in module rows with their bare aliases; the shipped package tree uses the canonical `lambda.*` paths with math typesetting under `lambda.doc.math`; and `lambda` is rejected as a binding name by the direct lexer reservation check. Regression: `test/lambda/lambda_namespace.ls` plus `test/lambda/negative/semantic/lambda_namespace_root.ls`; focused probes passed on qualified built-ins and the document package. |
| S16.10 | **Conformant in both front ends as of 2026-09-24**, with one C residue. The C parser has enforced S16.10.1v2 since 2026-08-27: E201 at every binding site for the whole K.1 set and the reserved root `lambda`, a parse error for a keyword import alias, and keyword tags, attributes, keys and member steps. The reference grammar reserves the K.1 words (tree-sitter `reserved`, CLI 0.25.10), so a barred word is a syntax error in binding and value positions and a data name through `_keyword_name`; `lambda` is an identifier there, barred by E201 only. S16 harnesses with the new S16.10 cases: C 343/343, reference grammar 330/330. Corpus differential (1934 files): the grammar newly rejects only negative tests, five of which C rejects too; the other two rely on the unnamed `fn` that C misparses (LR02-21). Residue: C still reads `fn`, `view`, `edit`, `state` and `apply` as values ([LR02-21](../vibe/Lambda_Issue_Ledger.md)). Unruled edges: named-argument names, `not` and the named values ([LR02-23](../vibe/Lambda_Issue_Ledger.md)). History: [LR02-14](<../vibe/Lambda_Issue_Ledger (fixed).md>). |
| int v5 (S4.1) | Substantially landed (lane, encoding, saturation, printing, goldens). The prior `INT64_ERROR` collision is resolved in [LR03-R4](<../vibe/Lambda_Issue_Ledger (fixed).md#lr03-r4>). Residue: ELEM_INT SIMD kernels partly gated; nullable lane (`INT_LANE_NULL`) partial; `IntLane`/ValueRep typing of the four i64 meanings pending (known silent bug class). |
| S2.5.1v2, S2.5.2v2, S2.5.4v2, S2.5.5v2, S2.5.6, S2.5.7, S8.3.3v3 | **Conformant since P5 of the list fixes (2026-09-23, both tiers)**; the query results S2.5.5v2 waited on were ruled the same day (S8.2.4, next row). Official `make test-lambda-baseline` at close-out: 5828 passed, the one failure being an unrelated upstream URL golden; `make test-radiant-baseline` 3682 passed. **P1–P3 landed 2026-09-22/23.** One kind bit (`is_spreadable`; `is_content` is now LambdaJS's `is_js_arguments`); spreading is decided by the value, never by syntax; list producers finish by position — in item position they splice even nothing, elsewhere they collapse (empty → `null`, one item → the item), for-expressions included whatever their clauses; `(…)` lists and functional blocks no longer normalize (only content and the script root do); `()` parses as `null`; `type()` names `list` and `is list` tests the bit; a bound list spreads in an array literal. A let-group keeps every item besides its declarations (`(let x = 1, x, 2)` had kept only `2`), and a group or block of declarations alone is `null` (`(let x = 5)` had been `5`; `[{ let x = 5 }, 9]` was `[0, 9]` on the interpreter). Positional structures store a list as one item (D2.6.5v3): a for-expression passed as one argument stays one argument, and a list-valued `order by` or `group by` key is one key (both had been split — `order by (x, 0)` did not sort). The library (`lambda/package`) was migrated to `[for …]` wherever a for-result is data ([Lambda_List_Fixes §8](<../vibe/impl/Lambda_List_Fixes (done).md>)). P2: every sequence operation keeps its input's kind (S2.5.7, S7.10.1v2, S7.10.5v2) — `sort`/`reverse`/`unique`/`take`/`drop`/`slice`/`[i to j]`, element-wise arithmetic, masks, and unary functions return a list for a list and an array for an array or range, and a list result collapses at one item or none (`take((10, 20, 30), 1)` is `10`); `zip` and the set operators are lists only for two lists; `fill` follows its item (`fill(2, (1, 2))` is `(1, 2, 1, 2)`); `split` and `find` build arrays; the mapping pipe and `that` follow S10.1.2v2/S10.1.5v2 for list, array, range, map, element, scalar, and null sources (`5 that ~ > 3` is `5`, an array filtered to nothing is `[]`); `order by`, `limit`, and `offset` keep a for-expression a list (S2.5.2v2); an annotation admits a list without changing its kind (S11.1.6v2). The checker types a selection, filter, or pipe whose source may be a list as open, because the result may be an item or `null`. P3: a map field, an attribute, or an object field stores a list as its array image, in literals and in writes, while the source list keeps its kind (`let l = (1, 2); {f: l}.f` is `[1, 2]` and `[l, 9]` still has three items); an insertion splices a list — `a[i] = (8, 9)` replaces one item with two and `push(a, (5, 6))` appends two, typed arrays included (each item is admitted). P4: text walks as a sequence and `++` follows its table, and `*` builds the list of its operand's items instead of marking it (rows below). P5: the occurrence and array families are split, and an annotation of either admits a list without changing its kind. Query results (`e[T]`, `e?T`) had returned a list-bitted container at any length; S8.2.4 now makes them a run, which closes the last S2.5.5v2 gap (an array reading, S8.2.4v2, was ruled 2026-09-25 and reverted 2026-09-26 by S8.2.4v3, which keeps the run). S2.5.4v2 (2026-09-24): the C parser had parsed `let` as a prefix expression anywhere (`[let x = 1, x]`, `f(let x = 1)`); it now takes one only as a list item (E100 elsewhere, `negative/syntax/let_outside_list.ls`), and the reference grammar keeps `let` a keyword in every expression position. |
| S8.3.3v3 | **Ruled and implemented 2026-09-23 (both tiers).** `count` had been defined and never built; it is now a system function with `len`'s raw-int ABI, so an error operand is rejected at the same `any \ error` boundary and escalates identically. The ruling changed its meaning at one point only: `count(null)` is 0, the size of the empty run, where v2 counted a written `null` as one item — which is why the literal law now reads `Σ count(xᵢ or 1)`. `count` answers what neither `len` idiom can: `len([q])` miscounts no match as 1 and `len([*q])` counts a lone array match by its contents. Fixture `test/lambda/count_run.ls`, all three tiers and forced GC. Argument: [Design_Syntax §7.27](../vibe/Lambda_Design_Syntax.md). |
| S8.2.4v3 | **Ruled 2026-09-26 (USER); implemented 2026-09-26 on both tiers.** It reverts the array reading S8.2.4v2 of 2026-09-25, which was never built; the run itself had been conformant since 2026-09-23 (`fn_query`/`fn_child_query` finish through the list collapse and the checker types the result open). New in this ruling and now built: a list is stepped set-at-a-time — `child_query_collect` applies the step to each item, so a scalar item contributes nothing (`(1, "a", 2)[int]` had filtered to `(1, 2)`), while chains through element items already worked (`html[table][tr][td]` at two tables) — and `query_collect` applies `?`/`.?` to each item of a list instead of testing the items themselves; a `null` value never matches (`[1, null, 2]?(int | null)` had yielded three matches, the null hidden in printed output); `?` walks a range's values (`(1 to 3)?int` had found none); an index array is a gather and a bool array a mask over any sequence (`fn_index_select`: arrays, typed arrays, ranges, elements; `a[[1, 3, 5]]` had raised `the index array must be boolean`). Items nested in one another are not deduplicated: with no value identity (SI13), `html?<div>?<div>` reaches a doubly nested div once per enclosing one. The subscript defects found alongside are fixed with it: [LR07-31..35](<../vibe/Lambda_Issue_Ledger (fixed).md#lr07-31>). Fixtures `test/lambda/subscript_selection.ls` and `test/lambda/proc/subscript_last_scope.ls`, on all three tiers (`kTune27TierParity`) and in the forced-GC sweep; `query_kind.ls` now pins `type((1, "a", 2)[int])` as `null`. `make test-lambda-baseline` 5929/5929; `make test-radiant-baseline` 3692 passed, its one failure a Layout Page Suite runner error under the full parallel run that passed on two isolated reruns (51/51 measured, snapshot check clean). **Residue:** the virtual carriers VArray and Velmt are not walked by a type query although `for` walks them (D7.4.5v2); a gather over text is unruled and reads `null`; an inline `(int | null)` key is a set union, not a type ([LR07-36](../vibe/Lambda_Issue_Ledger.md#lr07-36), a question for S10.1.1). v1 (2026-09-23) closed [LR05-13](<../vibe/Lambda_Issue_Ledger (fixed).md#lr05-13>). Ruling record: [Expr_Query §4.1](../vibe/Lambda_Expr_Query.md). |
| S2.5.8, S7.10.1v2, S7.10.5v2, S8.4.1v2, S10.1.2v2, S10.1.5v2, S10.6.1 | **Ruled 2026-09-21/22; conformant since P4 of the list fixes (2026-09-23, both tiers)**, except `keys`/`values`/`names`, which are not built in (S8.4.1v2 — deliberate). Kind preservation for pipes, `that` and vectorized functions landed in P2 (row above). P4 text: a string or symbol is walked by code point and a binary by byte, so `reverse("abc")` is `"cba"`, `sort("cba")` is `"abc"`, `unique("aab")` is `"ab"`, `reverse(b'\x0102')` is `b'\x0201'`, and `"abc" that ~ != "b"` is `"ac"`; a filter over text always rebuilds that kind and a mapping rebuilds it only when every result item belongs to it (`"abc" |> upper(~)` is `"ABC"`, `"abc" |> ord(~)` is `[97, 98, 99]`), an empty result being `""`. `in` is code-point membership (`"a" in "cat"` true, `"at" in "cat"` false) and `contains` keeps the substring test. An out-of-range text subscript is `null`, not `""` (S7.2.1). `++` follows its table: a sequence operand decides the result before any text arm, so `[1] ++ "ab"` is `[1, "ab"]`, `[1, 2] ++ 3` is `[1, 2, 3]`, `3 ++ [1, 2]` is `[3, 1, 2]`, kind by S2.5.7; `null` is the identity and maps and elements are rejected. [LR05-9](../vibe/Lambda_Issue_Ledger.md) closed with it: the packed-array shortcut copied the right operand's payload under the left element type (`[1, 2] ++ [3.5]` was `[1, 2, inf]`), and a mixed pair now takes the generic item path. Migration: nine procedural fixtures used `"label=" ++ container` as string interpolation and now call `string(…)`. |
| S11.1.1v3, S11.1.2v2, S11.1.6v3, S16.8.6v3 | **Ruled 2026-09-22; conformant since P5 of the list fixes (2026-09-23, both tiers).** The two families are split by bracket: `T{n}`, `T{n,m}`, `T{n+}` count a run and `T[]`, `T[n]` describe an array, in the C parser, the reference grammar and the pattern islands (`\(d{3})`). As a boundary type an occurrence admits what a run is (S2.5.5v2): `null` for zero, a bare `T` for one, a sequence of two or more for more, so `null is int*` is true while `[5] is int*` and `[] is int*` are false and `{f: int*}` admits `{f: null}` and `{f: [5, 6]}` but not `{f: [5]}`. A run whose operand is itself a container takes that container as its one item, which is how `int[]?` still holds `[]`. In a sequence-pattern slot an occurrence is a run of the sequence's items and zero is void, matched by backtracking over the counts a run can take: `[1, int*, 2]` matches `[1, 2]` and `[1, 5, 6, 2]`, never `[1, null, 2]` or `[1, [5, 6], 2]`. The array family admits only arrays, so `5 is int[]` is now false, and a counted array checks its length (`int[0]` is `[]`). `<:` relates the families: `int <: int*`, `int? <: int*` and `int[2] <: int*` hold while `int[] <: int*` and `int* <: int[]` do not, and `list <: array` and `range <: array` hold where `array <: list` does not (S2.5.1v2). `T[n+]` and `T[n, m]` are rejected with a diagnostic naming their replacement (`test/lambda/negative/semantic/occurrence_retired_*.ls`). Two spelling notes: the open count is `T{n+}` (2026-09-23, USER) — regex's `T{n,}` is rejected with a diagnostic naming it, since an exact `T{n}` would be a silently different type — and a `{` count binds tight (`int{2}`, never `int {2}`), since a spaced brace opens a body or a block, and a **return** type never takes one at all — there the body wins, so a counted return type goes through a type alias. Migration: every retired spelling in the tree (`type_pattern.ls` 18 sites, `type_occurrence.ls`, five island fixtures) and the user docs. **The v3 element-content clause is conformant since 2026-09-25 (both tiers):** an element pattern's content section resolves to `TypeElmt::content_list`, and `validate_against_element_type` — the path `is` and the validator share — matches the children against it with the bracket-pattern matcher, where it had compared counts ([LR13-10](<../vibe/Lambda_Issue_Ledger (fixed).md#lr13-10>)). The same change stopped a structural slot — `<p>`, `{y: int}`, `[int, int]`, `int[]`, a function signature — from reducing to a TypeId test, which had let `[<li>]` match `[<p>]` in bracket patterns too. Fixture `test/lambda/element_content_pattern.ls`; baseline 5869/5869. [Impl_Element_Type_Sharing P0](../vibe/impl/Lambda_Impl_Element_Type_Sharing.md) |
| S12.3.5v2 | **Ruled 2026-09-22; conformant since P4 of the list fixes (2026-09-23, both tiers).** `*x` builds the list of x's items instead of marking x, so spreading never modifies its operand and the JIT no longer rewrites a pooled literal (`fn g() => [1, "y"]` returned a list on every later call — the tier divergence is pinned on all three tiers by `test/lambda/spread_star.ls`). A range is materialized (`[*(1 to 3), 9]` is `[1, 2, 3, 9]`), `*null` splices nothing, and a non-sequence — text included — is one item, so `[*xs]` packages any value as an array. Because it is a value, `*x` also finishes by position (S2.5.5v2): `let b = *[2, 3]` is the list `(2, 3)`. |
| S2.2.3, S2.6.1, S2.6.2, S2.6.4, S2.6.5 | **Ruled 2026-09-21; conformant since P3 of the list fixes (2026-09-23, both tiers)**, except (4) below — which is why S2.6.1 alone keeps its implementation mark. S2.6.3 conforms: lists spread recursively with their spliced nulls and `""` dropped, for-expression results spread, arrays and ranges stay one item, script top level and element literals agree; strings merge at construction and never with a binary. Closed by P3: (1) a lone `""` is dropped (`<e "">` has no children, and a bare `""` at the top level prints nothing); (2) adjacent binaries merge (`<e b'\x01' b'\x02'>` has one child); (3) content writes normalize — `e[1] = "m"` on `<e "x" 1 "y">` leaves one child `"xmy"`, `e[1] = null` removes the child and merges its neighbours, `e[0] = ""` removes it, `e[0] = (3, 4)` splices, and `push` on an element appends content. String merging no longer waits for an input context (D2.6.5v3). Open: (4) LaTeX input keeps consecutive strings unmerged (MarkBuilder's verbatim append, D2.6.5v3) — a transitional workaround until the LaTeX parser carries such runs in non-merging items. Argument: [Design_Syntax §7.27](../vibe/Lambda_Design_Syntax.md). |

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

**Values, COW, resources**
- **SO13** COW granularity on large documents: node representation for spine-copying, refcount discipline for unique-path in-place update, and the gating benchmark. [C4.3]
- **SO14** Nested-mutation ergonomics (`t.nodes[i].value`): path-shaped `var` borrows, `_modify`-style accessors, or guaranteed get-modify-put. **Owner document as of 2026-08-28: [`vibe/Lambda_Design_Nested_Mutation.md`](../vibe/Lambda_Design_Nested_Mutation.md)** (CW22–CW28, PROPOSED). It rules that a place is a borrow and never a value (S9.2.2 generalized from slices to paths), that `var b = <place>` keeps copying, and that a mutated place copy becomes a compile error — the last being what gates the S9.3.1 default flip. On ratification these become S9.4 and this entry is struck. [C4.4]
- **SO15** Exclusivity granularity endpoint (whole-base vs blessed splitters vs dynamic bookkeeping).
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
- **SO44** Binder depth on function values: whether a binder (S11.4.8v2)
  over a function value selects its full signature (`fn (int) int`) where
  known, or only its colour (`fn`/`pn`). The full signature is the literal
  S4.2.2 reading; it meets S11.1.4v2's pending function-type variance.
  [C20.2]
- **SO29** File write/append syntax (C6a: `into`/`onto` candidates); string interpolation syntax (note the `$` collision with quote splices); a set type; `assert`/`expect` unification.
- **SO31** The `<file>` element shape (name/size/mime, content as child) — pin with file-I/O spec.
- **SO32** Match extensions: pipe-context shorthand, string-pattern capture binding in arms, range patterns.
- **SO33** A10 residue: the aspirational generics text, `as` assertion semantics, and open-vs-closed map matching in assignment position — document or delete.
- **SO34** `emit()` vs `send()` — two event vocabularies coexist; state the boundary explicitly.

**Surface syntax**
- **SO35** A dedicated formal syntax document: S16 parks the surface-syntax rulings here because syntax and semantics are argued together, and one source beats two. If the grammar surface outgrows a section, extract S16 into a formal syntax spec and leave pointers — not a second, competing statement. [Design_Syntax]
- **SO38** Whether `|:` over a **map** should keep the surviving keys
  (yielding a map) rather than dropping them (yielding an array of values, the
  current behaviour recorded in S10.1.6). The pipe has the same question, and
  the two should answer it together: `~#` binds the key in both, so the key is
  observable to the predicate but absent from the result. An S10 container-
  shape question. [S10.1.2v4, S10.1.6]
- **SO36** Whether a `pn` call may appear nested inside an expression
  (`(pn_func(), 123)`, `if (exists(path)) …`), or only as a bare statement /
  the whole RHS of a binding — the A-normal-form effect-sequencing
  discipline. Deliberately split off from S16.6.8/S16.6.9 and left open; the
  argument and cost survey live in
  [Design_Syntax §6 O5](../vibe/Lambda_Design_Syntax.md). An S12
  effect-boundary question. [S12.1]
- **SO45** Whether a function type takes a suffix directly: `fn?`,
  `fn (x: int)?`, `fn ()[]`. S11.1.5v2 makes the return type optional, so
  after a return-less signature the suffix could only bind to the function
  type — while in `fn () int?` the same `?` belongs to the return type.
  Until ruled, the C parser rejects every direct suffix (it had accepted
  only `fn[]`), and grouping spells it: `(fn (x: int))?`, `(fn)[]`. The
  reference grammar rejects `?`, `+`, `*` and `[…]` there too, but reads a
  tight exact count as the type followed by a block statement: `fn (){2}` is
  `fn ()` then `{2}`, a silent misparse tracked as LR02-25. `function?` is
  unaffected, since `function` is a base type name. [S11.1.5v2,
  S11.1.6v2]
- **SO46** How an element pattern spells *content must be empty*. S11.1.6v3 leaves content unconstrained when a pattern has no content section, and today an empty section, `<div;>`, parses to the same thing; a present-but-empty content pattern would mean *no children*, but no spelling is ruled — `<div;>`, `<div; ()>` and `<div; null>` are candidates. Until ruled, an empty section stays unconstrained. [Shape_Transitions §7]
- **SO48** What the value set operators `|`, `&` and `!` return for list
  operands. S2.5.7v2 says operators keep the operand kind, but grounds that
  in "no operator shrinks", naming only `+ - * /`, the masks and `++`; union
  removes duplicates and intersection and exclusion drop items, so a list
  result can collapse — `(1, 2, 3) & (3, 4)` is `3`. Until ruled they keep
  P2's rule (a list only when both operands are lists); the alternative is
  the function rule, an array always. `**` is unnamed too: it never shrinks,
  yet it already returns an array for a list operand, unlike `+ - * /`.
  [S2.5.7v2, S10.1.1, S10.2.1]

## Appendix C — Decision-Record Index

| Section | Records | Where argued |
|---|---|---|
| S1 principles | C1–C18 distilled; Features §3.6 | `Lambda_Semantics_Formal.md`, `Lambda_Semantics_Features.md` |
| S2 value domain | C1, C1.6a, C2, C8.6-R; Design_Syntax §7.23, §7.27–§7.28; PTH1v2, PTH2v3, PTH3–PTH29; OB1–OB3, OB7–OB9, OB13–OB18 | `Lambda_Semantics_Formal.md`, `Lambda_Type_Path.md`, `Lambda_Type_Object.md` |
| S3 truthiness | C2, C17 | ibid.; `Lambda_Semantics_Formal2.md` |
| S4 numerics | C3, C13, C14b/c, C16, C17; int v5 | `Lambda_Semantics_Formal2.md`, `Lambda_Semantics_Int_Type.md`, `Lambda_Semantics_Number_Model.md` |
| S5 equality | C8, C8.5, C8.5a, C8.6, C8.6-R, C8.7, C9-4; OB4, OB10, OB16, OB19 | `Lambda_Semantics_Formal2.md`, `Lambda_Expr_Eq.md` (rationale only), `Lambda_Type_Object.md` |
| S6 ordering | C11, C11.4, C11.5; OB13 | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Object.md` |
| S7 absence/errors | C5, C5.3, C5.3b, C14, C14a, C15, C15a/b; TE-4, TE-9, TE-13, TE-15–TE-18; RF1–RF6; ER-D1–PD13; REH-D1–REH-D14 | `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Sys_Func.md`, `Lambda_Design_Exec_Recovery.md`, `Lambda_Design_Runtime_Error_Handling.md` |
| S8 membership | C5.3a, C5.3b; §8.0–8.3 records; OB4–OB5; Expr_Query §4.1 (S8.2.4v3) | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Object.md`, `Lambda_Expr_Query.md` |
| S9 mutability | C4, C4.2a/b/c/e, C4.3, C5.3b, C12; CW16–CW28; RG14 | `Lambda_Semantics_Formal.md`, `Lambda_Semantics_Formal2.md`, `Lambda_Design_Runtime_COW.md`, `Lambda_Design_Nested_Mutation.md`, `Lambda_Design_Runtime_Globals.md` |
| S10 operators | C6, C6.2–C6.4, C10; Design_Syntax §7.27; PTH3, PTH5–PTH6, PTH9–PTH10, PTH25–PTH29; Expr_Pipe §F.1–§F.7 (`|:` filter stage, `that` proviso, result kind, implicit fields) | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Syntax.md`, `Lambda_Type_Path.md`, `Lambda_Expr_Pipe.md` |
| S11 types | C7, C8.5c, C20; TE-1–TE-18; OB13; Type_Pattern §1.3; Design_Syntax §7.28 | ibid.; `Lambda_Design_Type_Enforcement.md`, `Lambda_Type_Object.md`, `Lambda_Type_Pattern.md`, `Lambda_Design_Syntax.md` |
| S12 effects/resources | Features §3.5–3.7; Procedural; Function_Arg; C19, C20; OB5–OB6 | `Lambda_Semantics_Formal2.md`, `Lambda_Semantics_Features.md`, `Lambda_Procedural.md`, `Lambda_Proc_Assignment.md`, `Lambda_Design_Function_Arg.md`, `Lambda_Type_Object.md` |
| S13 concurrency | K11–K32 | `Lambda_Design_Concurrency.md` |
| S14 data processing | PD9–PD16; FC1–FC11 | `Lambda_Design_Data_Processing.md`, `Lambda_Expr_For_Clauses2.md` |
| S15 metaprogramming | C9, C9a | `Lambda_Semantics_Formal2.md` |
| S16 surface syntax | Design_Syntax §3–§7 (39 decided points) | `Lambda_Design_Syntax.md` |
| S17 system library | C18, C15b.1; IL2-I11, IL2-I12, IL2-I25 | `Lambda_Semantics_Formal2.md`, `Lambda_Type_Int_Sized.md`, `Lambda_IO_Sysinfo.md` |

The decision records preserve the full deliberations — every alternative that
lost and the arguments that did not persuade. This specification is their
distillation: the record governs the history; this document governs the
language.
