# Lambda Formal Semantics — Specification

**Status:** normative — an Architecture Decision Record for the Lambda language
**Basis:** decision records C1–C13 (with findings A1–A10 and the probe evidence) in
[`vibe/Lambda_Semantics_Formal.md`](../vibe/Lambda_Semantics_Formal.md) and
[`vibe/Lambda_Semantics_Formal2.md`](../vibe/Lambda_Semantics_Formal2.md).
**Scope:** this document specifies what Lambda's semantics **is by decision** — not
what any given build implements. Where the implementation disagrees, the
implementation is wrong; the conformance work is tracked in
[`vibe/Lambda_Semantics_Impl_Plan.md`](../vibe/Lambda_Semantics_Impl_Plan.md), and
the executable formal model is specified in
[`vibe/Lambda_Semantics_DSL_Proposal.md`](../vibe/Lambda_Semantics_DSL_Proposal.md).
**Style:** each section states the rule in the declarative present, followed by a
concise *Rationale*. The full arguments — including the alternatives that lost and
why — live in the decision records cited as `[C#]`.

---

## 1. Core Principles

Lambda's semantics rests on a small set of axioms. Every rule in this document is
an instance of one or more of them; new design questions should be answered from
them first.

**P1 — Emptiness is not nothingness.** An empty value is a value (`""` is a
string; an empty box is a box). Absence is expressed by `null` alone. [C1, C2]

**P2 — Values versus containers.** Scalars are identified by content alone;
containers are *things* that hold content. The distinction drives truthiness (an
empty container is still something) and equality shape. [C2]

**P3 — Exact until you ask for float.** Integer and decimal arithmetic is exact;
inexactness enters only through the float type, explicitly. Numbers get bigger,
and only float makes them blurrier. [C3, C13]

**P4 — Mutation is visible or it does not exist.** Values never alias. Mutability
is a property of bindings, marked by `var` — never a property of values.
[C4, C12]

**P5 — Set-oriented: absence flows, failure raises.** Lambda processes sets of
data like SQL processes rows: absence (`null`) lets the computation continue;
errors exist only where explicitly raised. [C5]

**P6 — Representation is invisible.** Boxing, copy-on-write sharing, unboxed
numeric arrays, JIT type inference, and decimal width are implementation
strategies; none may be observable in results. Every violation is a bug by
definition. [B7, C4, C8, C13]

**P7 — One symbol, one concept.** Each operator spelling has a single meaning
everywhere (`|` is union; `|>` is pipe; `in` is value membership; `at` is key
membership). Operators over containers mean one thing across the map/list duality,
because elements are both. [C6, C5.3a]

**P8 — Strings are never code.** Programs enter the system as source files or as
constructed AST values; no API accepts a runtime string for execution. [C9]

**P9 — Equality is the root relation.** `==` is a total equivalence (with two
designed poison carve-outs); the sort order totally refines it; hashing respects
it; printing distinguishes exactly what it distinguishes. [C8, C11]

---

## 2. The Value Domain

### 2.1 Types

Scalars: `null`, `bool`, `int`, `integer`, `int64`/`i64`, `uint64`/`u64`,
`i8 i16 i32 u8 u16 u32` (sized storage ints), `f16 f32`, `float`/`f64`,
`decimal` (two tiers, §4.4), `string`, `symbol` (with
`path` as a special symbol), `binary`, `datetime` (with `date`/`time` sub-kinds).
Containers: `range`, `list`, `array` (with transparently unboxed numeric arrays),
`map`, `element` (simultaneously a list of children and a map of attributes),
`object` (nominally-typed map). First-class: `function`, `type`, `error`.

### 2.2 Empty and solid values

- `""` **is a string**: a genuine value of type `string` with `len 0`.
  `"" == null` is false; `"" is string` is true. [C1]
- **`symbol` and `binary` are solid types**: every value has `len ≥ 1`. The
  literals `''` and `b''` do not exist — writing them is a compile error
  ("empty symbol does not exist; use `null`"). Runtime operations that would
  produce a zero-length symbol or binary produce `null`. [C1, C1.6a]
- **Element content is normalized**: an empty text child is dropped at
  construction (`<e "">` ≡ `<e>`) — a tree-construction rule, independent of
  string equality (the XQuery/XPath Data Model position). [C1]
- **An empty file is an object, not an empty binary**: file reads yield a
  `<file>` element whose content is absent when the file is empty. Existence
  lives in the container; content lives in the value. [C2]

*Rationale.* Identifying `""` with `null` made the string type not closed under
its own operations and broke round-trips (the deciding argument was interop: JS,
Python, JSON, and XML all have real empty strings). Symbols are identifiers —
the empty name names nothing — so their empty case is removed *syntactically*,
making the invariant visible rather than silently enforced. [C1, C1.6a]

### 2.3 Maps are ordered; equality is not

Maps store keys **in source/insertion order** — for round-trip fidelity,
deterministic `for (k at m)` iteration, and order-significant formats (CSS).
Map **equality compares keys unordered** (§5.4). This is the two-level model:
representation order is data; identity is content. [C8.6-R]

---

## 3. Truthiness

The falsy set is exactly:

> **`null` · `false` · `error` values · `""`**

Everything else is truthy — including **all numbers** (`0`, `-0.0`, `nan`),
**all containers** (`[]`, `{}`, elements, ranges — empty or not), all datetimes,
symbols, binaries, functions, and types.

Formally: `truthy(v) = false  iff  v = null ∨ v = false ∨ v is error ∨
(v is string ∧ len(v) = 0)`.

*Rationale.* Falsy-0 is C's boolless legacy; languages with real booleans chose
truthy-0 (Ruby, Lua, Lisp). Truthy-0 also keeps `or` a safe coalescing operator —
`config.port or 8080` never clobbers a legitimate 0 — so Lambda never needs a
separate `??`. Containers are truthy because an empty box is empty but is not
nothing (P2); the resulting system is JavaScript's object rule with the C-legacy
number mistakes removed. Consequence to teach: `if (results)` does **not** ask
"any results?" — write `len(results) > 0`. [C2]

---

## 4. Numerics

### 4.1 The two-tier integer model

- **Flex `int`**: 53-bit symmetric range **±(2⁵³ − 1)** — exactly the integer
  subset of `float64` (JavaScript's safe-integer bound). On overflow, arithmetic
  **promotes to float** (correctly rounded); types are sticky (no demotion).
  The symmetric range means the flex tier has no `MinInt`: negation and `abs`
  are total.
- **Machine ints** (`i8`…`i64`, `u8`…`u64`): Go-aligned — runtime overflow
  **wraps** (two's complement); constant/literal overflow is a **compile
  error**; division by zero returns `error()` (a deliberate divergence from
  Go's panic — the §7.3 fn-return rule overrides the Go alignment on this
  corner); `MinInt / -1` wraps.

*Rationale.* `int ⊂ float64` makes int↔float conversion always lossless, kills
the mixed-arithmetic precision-loss class, and lets the JIT hold ints in double
registers with zero observable difference (P6). The rejected alternative —
promotion to decimal — was tried and abandoned: it never fixed non-overflow
int/float mixing, and the decimal×float lattice is costly. [C3]

### 4.2 Literals are strict; data always fits

- An unsuffixed integer literal that exceeds the `int` range is a **compile
  error** — never a silent conversion. Suffixes (`i64`, `u64`, sized numeric
  suffixes, `n`, `m`, float form) express intent explicitly. The suffix alone
  names the type: `n` is `integer` always (`1n`, `1e3n`), `m` is `decimal`
  always (`100m`, `1.5m`, `1.5e-2m`). A fractional or negative-exponent
  spelling with `n` (`1.0n`, `1e-3n`) is a **compile error** naming `m` —
  `n` requires an integer-valued spelling.
- **Data cannot be rejected**: input parsers place integer tokens in the
  smallest exact home — `int`, else `int64`, else `decimal` — never silently in
  float. [C3, C13]

### 4.3 Mixed-operation promotion lattice

> numeric operations use the exact-embedding lattice: `int` can enter both
> `float` and `integer`; `integer` and `float` meet at `decimal`.

The exact meeting type is contagious. **A float entering the decimal world is
converted by one rule: it denotes its shortest round-trip decimal** (§4.5), and
the operation proceeds in decimal arithmetic. [C3, C8.5a, C13]

### 4.4 Decimal storage tiers

`decimal` is one source-level type with invisible storage tiers. Values that fit
the fixed decimal tier may use it; larger exact literals and results use the
extended tier. Literal digits are preserved exactly regardless of tier. Precision
control belongs on operations (`decimal(x, prec:, rounding:)`, `quantize`,
`round`) rather than on a second literal suffix. The decimal literal suffix is
`m` (all numeric spellings, including integer-valued: `100m`); the retired
uppercase `N` suffix is not part of the grammar.

Decimal `+`, `-`, and `*` are exact and may grow storage; division and other
inexact operations round at the documented decimal context. `integer ÷ integer`
exits to `decimal`, not floor division.

*Rationale.* The third instance of the two-tier philosophy. Precision carried in
the type is the value-semantics-compatible design; Python's context — mutable
global state that changes what arithmetic means — is the cautionary alternative.
A rational type (exact division) was consciously rejected: no data format
round-trips `p/q`. [C13]

### 4.5 Float ↔ decimal: the shortest-round-trip convention

**A float denotes its shortest round-trip decimal** — the fewest digits that
parse back to exactly the same double. This one conversion governs *every*
mixed float↔decimal operation: `==`, ordering, the total order, arithmetic,
hashing. Consequences:

- `0.1m == 0.1` → true; `0.1 - 0.1m` → `0m`.
- `0.1 + 0.2 == 0.3m` → **false**: the float sum *is* a different number
  (`0.30000000000000004`); the convention aligns values with their decimal
  twins — it cannot make float arithmetic decimal.
- **Decimal contagion is the escape**: one decimal operand promotes the whole
  chain — `0.1m + 0.2 == 0.3m` → **true**.

*Rationale.* The naive alternative (round the decimal to a double and compare)
destroys transitivity — many decimals map to one float. Shortest-round-trip is
injective, so equality remains an equivalence; it matches the conversion Java
blesses (`BigDecimal.valueOf`) and what serialization does universally; and it
is cheaper than the exact-binary-expansion alternative (≤ 17 digits always fits
decimal128). [C8.5a]

### 4.6 Float printing

`print`/`format` render a float as its shortest round-trip decimal
(`print(0.3)` → `0.3`; `print(0.1 + 0.2)` → `0.30000000000000004`). The printer
is **injective**: distinct doubles print distinctly — artifacts are visible,
never hidden — and print→parse round-trips are exact. With §4.5, WYSIWYG
equality holds literally: **two numbers are equal iff they print the same.**
[C8.5a]

### 4.7 Division and modulo

Division by zero never raises, at any width or tier:

- **A literal zero divisor is a compile error** (`x div 0`, `x % 0`) —
  statically-visible nonsense, caught early like the other literal-strictness
  rules (Go's constant layer). The rules below govern *computed* zeros.
- `/` is float-producing true division and follows IEEE: `1/0` → `inf`,
  `0.0/0.0` → `nan`.
- `div` and `%` are integer operations; integers have no `inf`/`nan`, so a zero
  divisor **returns `error()`** (§7.3 poison — flows through pipelines,
  rescued by `or`, never aborts). Applies to flex `int` and machine ints alike.
- Vectorized integer division over numeric arrays: a zero divisor poisons the
  **whole operation's result with a single `error`** (carrying the first
  offending index), never per-lane `[error, …]` — a fixed-width lane cannot
  hold poison, a boxed escape would make the result's type depend on runtime
  data (P6/type stability), and one operation has one failure. Partial-result
  salvage is explicit: pre-mask divisors with `b eq 0`, substitute or filter,
  then divide.

`div` truncates toward zero; `%` takes the dividend's sign (C convention;
contrast Python's flooring — relevant when modeling guest languages).
[A2, C3, C14b]

---

## 5. Equality

### 5.1 The complete definition

`==` is **total deep value equality** over all value pairs:

- **Cross-family** comparison returns `false` — never an error.
- **Within-family**: deep, structural, value-based comparison, with
  cross-representation numeric equality (§4.5) and sequence-family unification
  (§5.3).
- **Two designed poison carve-outs**: `nan` and `error` values never equal
  anything, including themselves. These are the only exceptions to
  reflexivity. **Poison is unequal, not untypeable**: classification is a
  different relation — `err is error` → true, `nan is float` → true, and
  `match` type arms (which dispatch via `is`) catch them:
  `match int(s) { case error: fallback  case int: ~ * 2 }`. Errors reach
  `default` only when no `error` arm exists.
- `!=` is the exact negation.
- Structural recursion is depth-limited; exceeding the limit **raises** (a
  wrong `false` would be silent; a hang would be worse).
- `==` is the *only* equality: values have no identity, so no `===` exists or
  ever will. [C8, C8.5, C4]

*Rationale for totality*: a raising `==` violates the error discipline (§7) and
aborts set processing (`data that (~.id == 'x')` must not-match mixed rows, not
crash). *Rationale for poison*: an error or nan compared equal could flow into
an `if` branch silently; `error == error → false` mirrors `nan` by design.
[C8, C8.5]

### 5.2 Numbers

Numeric equality is by mathematical value across all representations:
`1 == 1.0 == 1n == 1m`; `0.1m == 0.1` per §4.5; `-0.0 == 0.0`. Decimal storage width
is representation, not value identity. `nan != nan` (IEEE, and the poison rule).

### 5.3 Sequences

`range`, `list`, and `array` form **one sequence family**: `(1 to 3) == [1,2,3]`.
Equality is element-wise in order. Unboxed numeric arrays are representation
only (P6) — equality is value-based, never layout-based.

### 5.4 Maps, objects, elements

- **Map equality is key-unordered**: `{a:1, b:2} == {b:2, a:1}` — consistent
  with JSON and XML, the major document standards (even though storage is
  ordered, §2.3). Consequence, accepted: `a == b` does not imply
  `format(a) == format(b)` for maps.
- **Objects** are a distinct nominal family: a plain map never equals an
  object.
- **Element equality** = tag + namespace, attributes (as a map — unordered,
  matching XML InfoSet's "attribute order is not significant"), children
  (ordered — document order is meaning). [C8.6-R]

### 5.5 Functions

Function equality is **intensional**: same definition site + deep-equal
captures. Site = static AST node identity `(module, node)` — never a memory
address (addresses break under COW copying and JIT recompilation). Dynamically
constructed functions (§10) use the **content hash of their normalized AST**
(positions stripped, bound variables alpha-normalized) — so
`(x) => x + 1 == (y) => y + 1`, and compiling the same AST twice yields equal
functions. `f == f` is always true. [C8.7, C9]

### 5.6 Dedup and grouping

`unique`, `set` semantics, and grouping are **defined by `==` with no special
cases**: nulls group together (`null == null` is true); each `nan`/`error`
stands alone (poison values are never duplicates). Lambda thereby avoids SQL's
DISTINCT contradiction outright. [C8.6]

### 5.7 Hashing

Wherever values are hashed: `a == b ⟹ hash(a) == hash(b)`. Maps hash in
canonically sorted key order; numbers hash via their canonical value across
representations (`1`, `1.0`, `1n`, `1m` hash equal). [C8.6-R, C8.5a]

---

## 6. Ordering and Sort

### 6.1 Two relations, by design

**The total order says where things go on a shelf; `<` says which is
smaller.** Every value has a shelf position — everything sorts (§6.2) — but
only some families have *magnitude*, and the comparison operators
(`< <= > >=`) answer only magnitude questions:

- **Comparable**: numbers (magnitude is their essence), strings (dictionary
  order is a domain concept), datetimes (before/after is the natural semantics
  of time; same-kind by instant, `date` vs `datetime` via day-start coercion,
  `time` vs the others → `error()`).
- **Not comparable — `error()`**: symbols and binaries (identifiers and blobs
  have places, not sizes; `'a' < 'b'` and `hash1 < hash2` are filing needs the
  shelf already serves), booleans, containers, and all cross-family pairs.
  Statically-visible invalid comparisons are compile errors (the two-layer
  pattern); dynamic ones return `error()`.
- Poison stays incomparable (`nan < x` false both ways, IEEE; `error` operands
  taint); `null < x` → `null` (absorption, one rule with `null + 1` → null).

`sort` and `order by` use the separate **total order** of §6.2 — the same
two-relation split as IEEE `totalOrder` vs `<`, Java `compare` vs `<`, and SQL
`ORDER BY` vs `WHERE NULL`. [C11, C11.5]

### 6.2 The Lambda total order

> **null < false < true < number (by value) < datetime < symbol (path ⊂ symbol)
> < string < binary < sequence (range = list = array) < map < object < element
> < type < function < nan < error**

- **The governing principle: the total order totally refines `==`** — equal
  values always tie; ties are resolved by stability.
- Numbers order by mathematical value across all representations — no
  representation ranks.
- Within-band: `false < true`; strings/symbols/binaries bytewise UTF-8
  (= codepoint order; no locale collation); sequences lexicographic; maps via
  canonically sorted keys; elements by tag, attributes, children; datetimes by
  time value.
- `desc` is **full reversal** — one pure order, no pinning exceptions.
  Sort is stable.
- Mental model: *null is less than everything (absence); nan and error are
  beyond everything (broken).*

*Rationale.* Lambda's document kin (jq, MongoDB, CouchDB, SQLite, Erlang's term
order) all define a total cross-type order — heterogeneous `any[]` must sort
deterministically; erroring on mixed data (Python 3's choice) targets operator
comparisons, which Lambda keeps strict anyway. Null-first follows the document
camp; poison-last expresses "broken sorts after everything real". [C11]

---

## 7. Absence and Errors

### 7.1 Reads are total; absence is null

Out-of-bounds and negative array indexing, missing map keys, and string
out-of-range all yield **`null`**. Null **propagates through chained access**
(`data.users[5].name` → null end-to-end — built-in optional chaining) and
through scalar arithmetic (`null + 1` → null). Slices **clamp** to bounds.
`arr[i] or default` is the coalescing idiom.

*Rationale.* Lambda is set-oriented like SQL: null lets set processing continue
where a raised error would abort the whole computation — a 10,000-record
transform should produce 10,000 results with nulls where data was absent, not
die at record 4,371. [C5]

### 7.2 Writes are checked

An out-of-bounds write is a **raised error** — not null, not a silent no-op.
Reads ask a question; writes issue a command, and a command that silently does
nothing hides bugs. Growth is explicit (`push`/`splice`). [C5]

### 7.3 The two error channels: fn return, pn raise

**The general rule of Lambda error design: `fn` return error; `pn` raise
error.**

- **`pn` failures raise**: `T^E` signatures, `^` propagation, `let v^err`
  destructuring, compile-enforced call sites. Commands halt on failure
  (`output`, `io.*`, `cmd`).
- **System `fn` failures are values** — fns never raise, keeping them chainable
  and set-friendly (a raise aborts a whole set operation; a value flows
  per-item). The channel is declared in the signature — `T?` or `T | error`,
  never `T^E` on a system fn — and chosen by one principle:
  *absence in / no answer → `null`* (`int(null)`, `avg([])`, lookups);
  *present but invalid → `error()`* (`int("abc")`, `1 div 0`) — loud poison
  with a diagnostic. Both are falsy, so `f(x) or default` rescues both
  uniformly.
- **`input`/`fetch` are effectful readers — pn-family, and they raise**
  (`T^E`, compile-enforced), though permitted in expression position. This is
  classification, not exception: reading the filesystem/network is an effect,
  so these were never computational fns; the rule stays absolute while the
  taxonomy tells the truth. Rationale: batch input that "hopes it goes
  through" is too optimistic — I/O failure must be consciously handled, and it
  strikes at the *head* of a pipeline (loading the set), where aborting loses
  nothing and unacknowledged poison would surface far from its cause.
  *Boundary → raise; interior → return.* Set-oriented input is a deliberate
  opt-in via the **wrapper idiom** — `let d^err = input(f)` inside a user fn
  converts the raised channel to a returned value
  (`fn my_input(f) { let d^err = input(f); if (^err) err else d }`), then map
  over the wrapper. No `try` construct exists or is needed.
- **System/resource faults are unchecked exceptions**: stack overflow, memory
  exhaustion, the `==` depth limit raise from anywhere — including inside
  `fn` — but are invisible to fn signatures, propagate transparently through
  fn frames, and are handled at an enclosing `pn`'s `^err` boundary or a
  global handler; unhandled, the script aborts with a report. (Java's
  `Error`-class split; the type `T | error ^ E` cannot arise, by design.)
- **The channels in one line**: *raised errors are control flow; returned
  errors are data.* `^err` binds only raised errors; returned `error()` is
  caught by falsiness or `is error`.
- **The error invariant: every error value is deliberate** — constructed by
  `error()`, returned by a declared `T | error` builtin, or raised in a
  `T^E` context. Accidental emission is a bug. System exceptions never appear
  as values in fn results.
- User fns *may* declare `T^E` and `raise`; the documented style follows the
  system rule (fns return, pns raise).

*Rationale.* C5's read/write asymmetry lifted to functions: fn is the query
world (failures are data), pn is the command world (failures halt). IEEE is the
precedent — float ops return poison (`nan`) rather than trapping so pipelines
continue; `error()` is the universal nan, and the poison machinery (§5.1
never-equal, §3 falsy, §6.2 sorts-last, arithmetic tainting) was built for it.
Accepted cost: poison-returning fns forgo compile-time handling enforcement —
the price of chainability, softened by poison being loud rather than silent.
Note: resource-fault timing is exempt from P6 — when a stack limit fires may
differ across execution tiers. [C5, C14]

### 7.4 Array indexing and the `last` keyword

Indexing is **0-based**, and an index is a *position, not a direction*:

- **Negative indices carry no meaning.** `a[-1]` is an out-of-range read →
  `null`, exactly like `a[len]`. Both failure directions of a computed index
  are symmetric absence.
- **Reaching from the end is a named operation**: within a subscript, the
  reserved word **`last`** denotes `len(container) − 1` of the innermost
  enclosing subscript's container (innermost-wins, like `~`). Ordinary
  arithmetic applies: `a[last]`, `a[last - 1]`, `a[5 to last]`,
  `a[last div 2]` — on arrays, lists, ranges, and strings. Outside a
  subscript, `last` is a syntax error. There is no `last()` function, `.last`
  property, or `first` keyword — one concept, one spelling (P7); `0` is the
  first index.
- On an empty sequence `last = −1`, so `a[last]` → null (absence, no special
  case) and `a[last] = v` raises (§7.2).
- **The two-homes rule**: `last` has exactly two grammatical homes — the
  subscript expression above, and a **modifier inside `limit` clauses**
  (`for (x in xs limit last 10) x` — the last 10, in original order; the
  `desc` precedent: clause syntax, not an expression). It is never a value and
  never a range (`last N` as a first-class "relative range" is rejected — it
  would have no container to anchor to), and it is a syntax error everywhere
  else. There is no `first` keyword or `limit first` — plain `limit N` and
  `[0 to N-1]` already *are* first-N; front-anchored is the default direction.
- Corollaries: functions never take the `last` *keyword* (no anchor in call
  position) and never accept *signed counts* (`take`/`drop`, `find`/`replace`
  limits — sign-punning through the back door). The blessed API-level mirror
  of the clause pair is the **named-option pair**, and it is **the standard
  convention for every function that takes a limit/range option**:
  `{limit: n}` = first n, `{last: n}` = last n (e.g.
  `replace(s, ",", " and", {last: 1})`); both together = error; `{limit: 0}`
  means zero (a count counts — no Python-style zero-sentinel); unlimited is
  expressed by *absence* (option omitted or `null`), never by `-1` or `0`. No
  function invents its own limit spelling. On N-D numeric arrays, `last`
  resolves against the leading axis (`img[last]` = last row), matching
  iteration. [C15b]

*Rationale.* Negative indexing rewards an underflow bug (`a[i-1]` at `i = 0`)
with a plausible wrong answer while C5 forgives overflow as visible null — an
incoherent asymmetry, amplified because Lambda indices are typically computed
from data. Negative indices are not even a universal idiom (R's `x[-1]` means
*exclusion*). Julia's explicit end-marker is the adopted precedent —
JavaScript's ES2022 `.at(-1)` confirms the need while also refusing to touch
`[]`. `last` is chosen over `end` because `end` is ambiguous between
last-valid-index and one-past-the-end — doubly loaded against Lambda's
inclusive ranges. [C15]

### 7.5 Aggregation: strict null propagation

A `null` in an aggregation input makes the aggregate `null` — uniformly
(`sum`, `avg`, `min`, `max`, statistics). Skipping is always **explicit**:
the general idiom `xs[!null]`, or the `skip_null` option on
denominator-sensitive statistical functions.

Empty-collection aggregates: `sum([])` = 0 and `prod([])` = 1 (monoid
identities); identity-less aggregates (`avg`, `min`, `max`) over empty input
yield `null`.

*Rationale.* Lambda's own `+` propagates null, and `sum = reduce(+, v, 0)` must
stay literally true — SQL's skip contradicts its own scalar algebra (`1 + NULL`
is `NULL`, yet `SUM` skips), a standard criticism. Skipping silently changes
`avg`'s denominator; R and Julia force acknowledgment for the same reason.
[C5.3]

---

## 8. Membership and Iteration: the `in`/`at` Axis

`in` and `at` are the language's value/key axis, used identically in iteration
and membership:

| | values (list facet) | keys (map facet) |
|---|---|---|
| iteration | `for (x in coll)` | `for (k at m)` |
| membership | `x in coll` | `k at m` |
| on elements | children | attributes |

`i at arr` tests index possession (`0 ≤ i < len`) — the checked-write pre-check
idiom. Operand order is uniform: member left, container right.

*Rationale.* An element is both a map and a list, so any container operator
must mean one thing across the duality (P7) — `in` stays value-membership
everywhere, and key-membership gets its own operator. Reusing `at` makes the
membership pair *be* the iteration pair (self-teaching, no new keyword), and a
keyword operator is shadow-proof where a method (`m.has(k)`) could be shadowed
by wild data. [C5.3a]

---

## 9. Mutability: Mutable Value Semantics

### 9.1 The model

**Values never alias. Mutability is a property of bindings, not values, and
`var` is its only marker.**

1. **`let` is final**: nothing reachable through a `let` binding ever changes.
2. **Bindings and assignment copy, observably** — for every container kind.
   Implementation is copy-on-write on reference counts; **sharing must be
   unobservable** (P6 — a verifiable property).
3. **`var` parameters are the sole sharing construct**: `pn f(var a: T)` is an
   inout borrow. Compile checks: arguments must be `var` (never `let` or a
   temporary); **exclusivity** (the same `var`, or overlapping paths, cannot
   feed two `var` params); `pn` methods require a `var` receiver;
   **invariance** — a `var` parameter's argument type must match exactly
   (§9.2).
4. **Closures are immutable values**: capture copies at creation (snapshot);
   assignment to a captured name — including interior mutation through it —
   is a compile error. State lives in objects with `pn` methods, module
   `var`s, or view state; never inside a function value.
5. **No reference cells**; structural `==` is the only equality.

A planned, deferred relaxation: a *non-escaping* nested `pn` (name used only in
call position) is not a value — it may access enclosing `var`s directly
(equivalent to inlining; the closure-style parser use case). [C4, C4.2a]

*Rationale.* `pn` becomes locally imperative but observably functional. The
counter paradox decides closure immutability: a mutable capture makes a
`let`-bound closure change behavior per call, violating rule 1. Value
semantics also makes deep `==` total (cycles are unconstructible) and gives
concurrency a race-free substrate. Precedents: Swift, Koka/Perceus, Hylo;
R/Matlab for the domain. [C4]

### 9.2 Covariance: where values copy — invariance: where they're borrowed

`int[] <: any[]` holds for `is` checks, reads, value parameters, and
**assignment** (which copies, so the classic covariant-array hole cannot
arise). `var` parameters — the only aliasing channel — are **invariant**:
passing `var xs: int[]` to `pn f(var a: any[])` is a compile error.
Representation widening (unboxed → boxed) happens lazily at COW-copy time.

*Rationale.* The Java array-store hole requires aliasing; C4 removed aliasing
everywhere but the borrow channel, so the borrow channel carries the
restriction (Rust's `&mut` shape). A8 thus resolves as a corollary of C4.
[C12]

---

## 10. Operators

### 10.1 Union and pipe

- **`|` means union/alternative — everywhere**: type expressions, match
  or-patterns, string patterns, and value expressions (types are first-class:
  `let T = int | string`).
- **`|>` is the pipe**, dual-mode on a syntactic test: if the body contains a
  **free `~`**, it is a mapping pipe (binds `~` per item); with no free `~`,
  it is whole-value application (`data |> sum` ≡ `sum(data)`). The dispatch is
  decided at parse time. A `~`-free body that is not callable is a type error.
- File write/append syntax is deferred; `output(data, file)` is the interim.

*Rationale.* The collision was one-directional — union needs expression
position; pipe never needs type position — and keeping `|` overloaded would
have required semantic (type-directed) disambiguation, poison for a formal
grammar. `|>` is the world-standard pipe spelling. [C6]

### 10.2 Vectorization

- **Arithmetic `+ - * /` is vectorized** (element-wise with broadcasting):
  vector arithmetic is mathematics.
- **Bare comparisons `< <= > >=` are scalar-only** — never element-wise. The
  killing exhibit: `if ([1,2,3] > 99)` took the then-branch (a mask is a
  container, containers are truthy — silently wrong, worse than NumPy's
  raise).
- **Element-wise comparison has its own keyword operators:
  `eq ne lt le gt ge`** (XPath lexemes, continuing the `div` borrowing) —
  `img gt threshold` yields a bool mask. Element-wise `eq`/`ne` are permitted
  precisely because they are *distinct operators*; bare `==` is untouched.
  Note the deliberate inversion: in XPath these keywords are the scalar forms;
  in Lambda they are the element-wise forms — nobody types `gt` by accident,
  which is the point. [C10]
- **Mask consumption is explicit and non-magical.** `sum(mask)` counts true
  lanes; `a[mask]` is boolean indexing, with a full-shape mask selecting
  matching elements in row-major order and a 1-D mask on an N-D array selecting
  leading-axis slices. `and`/`or` remain scalar short-circuit truth operators,
  not element-wise mask combinators. Dedicated mask-combination functions such
  as `mask_and`/`mask_or` are deferred; until then, mask-producing code should
  stay readable by naming intermediate masks or using explicit selection.
  [C10.3]
- **Condition-position lints protect the truthiness boundary.** A direct
  element-wise comparison in `if`/`while`/`where` condition position warns and
  suggests `any(...)`, `all(...)`, `sum(mask)`, or `a[mask]`. A statically-known
  container condition also warns because every container is truthy under §3;
  the warning asks for an explicit scalar predicate such as `len(...)` or
  `any(...)`. These diagnostics are hints, not semantic errors. [C10.2/C2.3]

### 10.3 Keyword-operator inventory

`and or not is in to div that where at eq ne lt le gt ge` — Lambda is a
keyword-operator language; new operators prefer words over sigils.

---

## 11. Patterns and Types

### 11.1 Types compose like values

A bracket type is a **structural pattern** whose positions mix values and
types freely:

```lambda
[1, int, "str"]   // array of exactly 3: the value 1, any int, the value "str"
[int]             // array of exactly one int  (TypeScript's reading, not Haskell's)
[int*]  int[]     // array of zero or more ints
```

`[int]` = exactly-one is forced by compositionality (banning it would break
composition at n = 1) and is enforced: `let xs: [int] = [1,2,3]` is a compile
error whose message teaches ("did you mean `int[]`?"); a lint hints on bare
`[T]` occurrences that `[T]` is an exact-one pattern and `T[]` is the
homogeneous-array spelling. [C7]

### 11.2 Match dispatch

**Lambda's `match` is a type match, not a mere value match** — unlike
C/Java-family `switch`/`case`, which compare values. Arms are tried in order;
first match wins:

- **Type arms dispatch via `is`** (`case int:`, `case error:`, `case Circle:`)
  — classification, not equality.
- **Literal arms dispatch via `==`** (`case 200:`, `case "hello":`).
- **Constrained arms** combine both: `is` then the predicate
  (`case int that (~ > 0):`, `case error that (~.code == 304):`).
- Consequence of the split: **poison is unequal, not untypeable** — `nan` and
  `error` match their type arms (`case float:` catches nan; `case error:`
  catches errors) even though they `==` nothing (§5.1). Errors reach
  `default` only when no `error` arm exists.
- **The `type(x)` trap** (documented deliberately): `match type(err)
  { case error: … }` falls to `default` — the scrutinee is a *type value*,
  and `type(err) is error` is false even though `type(err) == error` is true
  (type-value equality is a different relation from classification). There is
  no `==` fallback for type-valued scrutinees — that would make arm meaning
  depend on the scrutinee's runtime type. The rule: **`match x` already is
  Lambda's typeof-switch** — match the value, not its type; `match type(x)` is
  a habit imported from JS's `switch (typeof x)`, and in Lambda a type value
  matches only `case type:`.
- **The `error` name duality**: the bare keyword `error` denotes the error
  *type* — `type(error)` → `type` — which is what appears in `case error:`
  arms and `is error` checks; the *constructor* `error("…")` creates an error
  **value** — `type(error("…"))` → `error`. Same name, two roles, split along
  the same classification/value axis.

### 11.3 Structural `is`, nominal objects

`is` checks are structural for maps/arrays/elements (extra fields permitted;
key lookup by name — order-insensitive) and nominal for object types. Type
matching and value equality answer different questions with different order
sensitivities (§2.3, §5.4) — both by design.

---

## 12. Metaprogramming

### 12.1 Code as data

Lambda is homoiconic through elements: the canonical AST is an element tree in
the ambient **`lm.` namespace** (`<lm.if …>`, `<lm.var …>`, `<lm.add …>`) —
namespaced because AST tags must be distinguishable from the world's documents
(HTML has a real `<var>` element; XSLT's `xsl:` prefix is the precedent).
Quoted-symbol tags (`<'if' …>`) exist as general grammar orthogonality for
ad-hoc keyword-named elements; they denote the plain symbol.

Element literals are **inverted quasiquotation**: expression children evaluate
(splices); element-literal children are structure (quoted). *The angle bracket
is the quote mark.* A `quote { … }` authoring form (parse-time-checked, with
`$`-splices) is planned but deferred. [C9, C9a]

### 12.2 Construction and execution

- `input(f, 'lambda')` parses Lambda source into the `lm.` AST.
- `compile(ast, env?) fn^` compiles a **closed** function: it sees the stdlib
  plus explicitly passed bindings only — never ambient scope. Failures are
  ordinary `T^E` errors. Compilation is deterministic and pure.
- **Strings are never code** (P8): `compile` accepts AST values only; no
  string-eval API exists in any form.
- Dynamically constructed functions take their identity from the **content
  hash of the normalized AST** (§5.5) — value semantics for code.

*Rationale.* The Lisp lineage (code-as-data + eval) with the injection class
removed wholesale and the environment made explicit (the one good lesson of
JS's `new Function`). One AST schema is shared by loaded, hand-built, and
quoted code — homoiconicity with validator-checked terms, which Lisp never
had. [C9]

### 12.3 Introspection

`name(item)` is the shadow-proof accessor for intrinsic names — element tag,
function declaration name, type name, an object's type name — returning `null`
for unnamed values. (The `.name` property remains but is shadowed by user
attributes; operators and functions, not properties, are the reliable surface
over open containers.) [C9a]

---

## 13. Verifiable Properties

These invariants are the checkable face of P6 and P9. The formal model and the
differential test harness verify them; any observed violation is a bug, never
a semantics change:

1. **Boxing invisibility** — tagged/unboxed representation choices never affect
   results.
2. **COW unobservability** — sharing until first mutation is undetectable;
   `let`-finality holds absolutely.
3. **Inference unobservability** (the gradual guarantee) — erasing all inferred
   types and running boxed must produce identical results to the typed JIT
   path.
4. **Equality laws** — `==` is a total equivalence modulo the two poison
   carve-outs; cross-representation numeric ties; `a == b ⟹ hash(a) == hash(b)`.
5. **Order refinement** — the total order refines `==`; sort is stable;
   `desc` is exact reversal.
6. **Printer injectivity** — distinct floats print distinctly; print→parse is
   exact; two numbers print the same iff equal.
7. **Error containment** — every error value is deliberate: constructed by
   `error()`, returned by a declared `T | error` signature, or raised in a
   `T^E` context. No accidental emission; system exceptions never appear as
   values in `fn` results. (Resource-fault *timing* is exempt from invariant 3
   — when a stack limit fires may differ across tiers.)

---

## 14. Decision Index

| Section | Decisions | Where argued |
|---|---|---|
| §2 value domain, `""`, solid types | C1, C1.6a, C2 | `Lambda_Semantics_Formal.md` |
| §3 truthiness | C2 | ibid. |
| §4.1–4.3 integers, literals, lattice | C3 | ibid. |
| §4.4 decimal tiers | C13 | `Lambda_Semantics_Formal2.md` |
| §4.5–4.6 float↔decimal, printing | C8.5a | ibid. |
| §5 equality | C8, C8.5, C8.6, C8.6-R, C8.7 | ibid. |
| §6 total order | C11 | ibid. |
| §7 absence, errors, indexing, aggregation | C5, C5.3, C14, C15 | ibid. |
| §8 in/at | C5.3a | ibid. |
| §9 mutability, covariance | C4, C4.2a, C12 | both |
| §10 operators | C6, C10 | `Lambda_Semantics_Formal2.md` |
| §11 patterns | C7 | ibid. |
| §12 metaprogramming | C9, C9a | ibid. |

The decision records preserve the full deliberations — including every
alternative that lost and the arguments that did not persuade. This
specification is their distillation; when in doubt, the record governs the
history and this document governs the language.
