# Lambda Design: Type Narrowing, Specialization, and Generics

**Status**: DRAFT (rev 7, 2026-09-15; TG20 two-tier binding RATIFIED, TGO1 CLOSED; TG18 container/unbound binding and TG19 `<:` operator RATIFIED, TGO4/TGO11/TGO13 CLOSED; TGO2 CLOSED — the binder keyword is `as`; TG5 RATIFIED; TG6v2 bound is a direct field of the binder record; TG14v2 inter-binder relations are ordinary `that` value predicates; TG15 `as` precedence; TG16 `that` before `as`; TG17 binder scope, return-position and `var` rules; TGO7 let/var binders confirmed out; TGO11–TGO13 added; Appendix S superseded rulings; Appendix I implementation notes. rev 6, 2026-08-08: TG4 RATIFIED, TG10 three binder levels, TG12 "Type Binder" RATIFIED, TG13 unification road reserved, TG14, §6 catalog → TGO10)
**Ledger prefix**: `TG#` (decisions), `TGO#` (open issues)
**Related**:
- `doc/Lambda_Formal_Semantics.md` — S1.6 (representation invisible), S1.7 (one
  symbol one concept), S4.2.2 (value types as its narrowest), SO9 (generics /
  flow-sensitive narrowing unowned), SO33 (aspirational generics text in docs)
- `doc/Lambda_Formal_Design.md` — D3.3.3 (narrowing dies with binding), D3.4.2
  (structural shape identity), D7 (modules)
- `vibe/Lambda_Design_Type_Enforcement.md` — TE-15/17/18 (boundary enforcement)
- `vibe/Lambda_Design_Dual_Func_Compiling.md` — DF8 (check lives in callee),
  DF9 (entry equivalence / boxed generic entry)
- `vibe/idea/Lambda_vs_TypeScript.md` — the comparison that seeded this design

**Thesis.** Lambda does not need a C++/TS-style generics sublanguage. Active
flow narrowing + automatic per-call-site specialization (the Julia model)
covers the performance motivation entirely; checked bindings and type-valued
arguments cover return-position polymorphism; `that` contracts cover value
constraints. The one irreducible feature of generics — **the relational
type link between parameter and return positions, stated in a signature** —
is something pre/post-conditions cannot express statically. This doc defines
that feature for Lambda as a small extension of first-class types: type-valued
parameters plus inline type-variable binders.

---

## 1. Prior Art

### 1.1 C++ templates — implicit structural generics, blame disaster

C++ templates are duck-typed generics: the body is re-checked (and re-compiled)
per instantiation. This proves two things relevant to Lambda:

- **Per-instantiation checking works** — it is exactly what "inference +
  specialization instead of declared generics" means, and it delivers full
  performance.
- **Its failure mode is error blame**: errors surface *inside* the callee, N
  levels deep, only for instantiations that actually occur. C++20 concepts
  were invented to bolt declared constraints back on. Lesson: inference-only
  needs a *declared-relation* escape hatch for library boundaries, or it
  reproduces SFINAE hell.

### 1.2 TypeScript — erased, unsound, maximal type-level power

TS generics (`<T>` with constraints, conditional/mapped/template-literal
types, `infer`) are the most expressive mainstream system — and fully erased:
the runtime can violate every annotation. TS's narrowing (discriminated
unions, guards, `never` exhaustiveness) is its best feature and the model for
what SO9's flow-narrowing item should deliver. Lesson: type-level
*computation* (mapped/conditional types) is where TS's complexity budget went;
Lambda deliberately does not follow (TG11).

### 1.3 ML family — unification and parametricity

Hindley-Milner infers principal polymorphic types via unification; `∀T. T → T`
provably returns its argument (parametricity, free theorems). Lambda cannot
and does not want parametricity: runtime `is` inspects types by design, and
types are values. Lesson taken instead: the *relational signature* idea.
Lesson rejected: symmetric unification of type variables — it is the source
of inference surprises (see TG4).

### 1.4 Julia — the reference model for §2

Dynamic language; every function is generic by default; the JIT specializes
per concrete argument-type tuple; method dispatch is on runtime types;
parametric types exist but most user code writes none. Julia demonstrates that
**automatic specialization fully replaces the performance motivation for
declared generics**. Its known costs — specialization explosion, dynamic-
dispatch cliffs when inference loses concreteness — inform TG8.

### 1.5 Zig — `comptime T: type`, generics as ordinary parameters

Zig has no generics syntax: `fn f(comptime T: type, a: T) T`. Types are
first-class compile-time values; generic functions are just functions taking
type parameters; instantiation is memoized comptime evaluation. This is the
closest precedent for §4: Lambda adopts the same shape, minus the `comptime`
keyword (Lambda types are runtime-real, so the dynamic tier exists too), plus
inference via binders (`as T`) which Zig lacks (all type args explicit).

### 1.6 Go — constraints as interfaces, deliberate minimalism

Go generics (1.18) are type parameters with interface constraints, no
variance, no specialization mandate, no type-level computation. Confirms that
a minimal relational system covers real-world library needs. Go's separate
type-parameter list `[T any]` is the syntax Lambda avoids by reusing the value
parameter list.

### 1.7 Contract systems — Eiffel, Racket

Pre/post-conditions check **value-level predicates at runtime per call**.
Racket's higher-order contracts even assign blame across module boundaries.
But no contract system expresses "result type = element type of the input"
*statically* — a postcondition `type(result) == type(xs[0])` is checked after
the fact, per call, and gives the checker nothing to propagate. This is the
precise gap §4 fills, and the reason contracts and generics are orthogonal,
not competitors.

---

## 2. Auto Narrowing and Specialization (details to be expanded)

The performance leg. Julia is the reference. This section is the frame; each
subsection expands into its own design pass later.

### 2.1 Flow-sensitive narrowing (closes half of SO9)

- `if (x is T) { ... }` statically retypes `x` to `T` (meet with its declared
  type) inside the guarded branch. Same for `match` arms with type patterns,
  `is` guards in conditions, and exclusion (`x is !null` → non-null branch).
- Narrowing is a property of a **binding within a scope** and dies with it —
  this is already ruled: D3.3.3. No narrowing survives assignment through a
  container, escape into a closure, or a call boundary.
- Union consumption is the payoff: `int | string` parameters become pleasant
  without any generics at all.
- *To expand*: exhaustiveness analysis over closed unions; narrowing through
  `let` rebinding; interaction with `var` mutation (kills narrowing on any
  write the checker cannot see through).

### 2.2 Per-call-site inference

- When the callee body is available, the checker instantiates the signature
  per call site with the concrete argument types and propagates the result
  type — implicit structural generics, as C++ templates and Julia both do.
- Unannotated functions therefore behave generically with zero syntax.
- *To expand*: memoization of per-signature analysis results; recursion
  cutoff (widen to declared/ANY at fixpoint); relationship to the existing
  MIR-Direct inference passes.

### 2.3 JIT specialization (Julia model, Lambda constraints)

- Specialization is an **optimization, never a semantic**: S1.6 —
  representation is invisible. A specialized body and the generic body must be
  observationally identical.
- Every function keeps its boxed/ANY generic entry (DF9 entry equivalence);
  specializations are added per concrete argument-type tuple, memoized by type
  identity. The generic entry is the always-correct fallback — no dispatch
  cliff can produce wrong answers, only slower ones.
- Guard against Julia's costs: cap specialization count per function;
  instantiation cache keyed by type identity so the admission relation is
  computed once (lesson from the tune16 B2.2 witness-param defect: never
  re-derive an admission per call).
- Boundary checks that the static tier proves infallible are elided (TE-17:
  native lanes gate on provable infallibility); the dynamic tier keeps them
  (TE-18: checks at declaration boundaries, callee-side per DF8).
- *To expand*: specialization key (argument types only vs. also `type`-valued
  argument values — a `T: type` argument whose value is statically known is a
  comptime key, §4); tiering/warmup policy; cache interaction with MIR module
  cache L1/L3 epochs.

---

## 3. The `parse()` Use Case: Lambda vs. Generics

The canonical TS argument for explicit generics is return-position
polymorphism: `parse<User>(json)` — the type appears in no value argument, so
inference has nothing to infer from. Lambda dissolves this case twice over,
without generics:

### 3.1 Checked binding / checked cast (validation)

```lambda
import User                       // type from a schema module
let u: User = parse(json)         // annotation-driven runtime check
let v = parse(json) as User       // explicit checked cast, same effect
```

`parse` returns untyped data; the **binding boundary** performs the check
(TE-18 declaration-boundary enforcement). This is strictly more honest than
TS, whose `parse<User>` *asserts* and verifies nothing at runtime. No type
parameter needed: the type flows from the annotation, and failure follows the
normal error discipline (TE-15 skip-to-boundary; `^ { ... }` handlers per
TE-16).

### 3.2 Type-valued argument (type-directed behavior)

Validation-after-the-fact cannot *steer* parsing: schema-directed construction
(build a `datetime` because the schema says so, apply field defaults, select
sized-int storage, reject unknown fields) needs the type as an **input**:

```lambda
let u = parse(User, json)         // type as ordinary argument
```

This is already the established Lambda idiom — the validator takes its schema
as an argument (`lambda.exe validate data.json -s schema.ls`). A plain
`type`-valued parameter does the job; first-class types (types are values)
make it ordinary code, not a generics feature.

### 3.3 Scorecard

| Need | TS | Lambda |
|---|---|---|
| Caller names the result type | `parse<User>(json)` (erased, unchecked) | `let u: User = parse(json)` (checked) |
| Type steers the function | not expressible (types erased) | `parse(User, json)` — type as value |
| Failure semantics | `any` + hope | error value, TE-15/16 discipline |

Conclusion: **return-position polymorphism is a non-motivation for generics
in Lambda.** What remains is the relational link — §4.

---

## 4. Type Relations: The Design

### 4.1 The admission

Pre/post-conditions (`that` clauses) check **value-level predicates,
dynamically, per call**. They cannot state — statically, propagatably, once —
the *type-level relation* that generics state in a signature:

> the result type **equals** the element type of the input; these two
> parameters have the **same** type.

`fn sum(xs: number[]) number` plus any amount of contracts still returns
`number` for an `int[]` input; every caller re-narrows. A postcondition
`that (type(~) == type(xs[0]))` encodes the relation but is checked after the
fact and feeds the checker nothing. **This relational capability is the one
genuine, irreducible feature of generics**, and the only one Lambda adopts.
It buys exactly three things (established in the analysis leading to this
doc):

1. **Static relational propagation** — `sum(ints)` types as `int` at the call
   site, no cast.
2. **Once-for-all body verification** — the body checks against abstract `T`
   within its bound, for every admissible instantiation, not just the ones
   that ran (the C++/§1.1 blame lesson).
3. **Opaque-boundary signatures** — a published module or MIR-cached artifact
   (D7, cache L2/L3) declares the relation without shipping the body; a
   concrete signature (`fn (any[]) any`) cannot.

### 4.2 The mechanism: two spellings, one semantic core

#### TG1 — No generics sublanguage

No `<T>` parameter lists, no variance annotations, no type-level computation.
The entire feature is: *type-valued parameters* (already implied by
first-class types) plus *inline binders* that infer them.

#### TG2 — Explicit type-valued parameters

```lambda
fn f(T: type, a: T) T => ...
f(int, 1)                          // caller passes the type
fn Pair(T: type) type => {a: T, b: T}   // parameterized aliases = fns returning types
```

A `type`-typed parameter may appear in **later** parameters' annotations and
in the return annotation (dependent-parameter scoping — the only new scoping
rule). Used when the caller must supply the type: type-directed functions
(§3.2), return-only positions, `type`-returning functions.

#### TG3 — Inline binder `as T` is sugar for an implicit type parameter

```lambda
fn f(a: number as T, b: T) T
// desugars to:
fn f(T: type that (~ <: number), a: T, b: T) T   // T inferred from a, elided at call sites
```

The annotated type (`number`) is the **bound**; `T` names the argument's
actual type. Call sites pass no type: `f(1, 2)`. One semantic core (TG2's
type values) with two spellings; `as T` is the spelling application and
library code will actually write. The desugaring is **explanatory**: it
fixes the meaning, not the enforcement mechanism — the bound is checked
directly from the binder record (TG6v2), never by evaluating a `that`
predicate. The keyword `as` is ratified (TGO2 closed 2026-09-15).

#### TG4 — First-occurrence-binds; no unification **[RATIFIED 2026-08-08]**

`as T` marks **the** binding site. Every other occurrence of `T` is a
**check**, not a constraint to be solved. `f(1, 2.5)` under
`fn f(a: number as T, b: T)` means `T := int`, then `b: T` rejects `2.5`.

Rationale: symmetric unification (ML/TS) is where inference surprises live
(`max(1, 2.5)`: int? float? number? union?). The explicit binder resolves the
ambiguity syntactically and keeps the checker a propagator, not a solver.
A solver is more powerful, but also more complicated — **we keep it simple
(first)**. This asymmetry is a feature; do not erode it later with a
"helpful" join-inference pass. If mixed-type ergonomics ever genuinely hurt,
the TG4-preserving fix is at the bound level (author writes plain `number`
with no binder when no relation is needed) — or, in the limit, the explicit
multi-binder unification form reserved by TG13; never an implicit
unification pass over single-binder signatures.

Consequences accepted with the ruling:
- Order dependence: `max(1, 2.5)` fails while `max(2.5, 1)` passes
  (`T := float`; `1 : float` embeds). Binder placement is therefore a
  deliberate API-design choice — put the binder on the parameter that should
  *dictate* the type.
- Blame quality: the failure is local and self-explanatory ("`b` must have
  the same type as `a` (int); got float"), versus unification's
  silently-widened `T` surfacing downstream.

#### TG5 — Binders compose inside type expressions **[RATIFIED 2026-09-15]**

Top-level binding cannot express the flagship examples; the binder must be
legal at any position within a type expression, binding that sub-position's
type (a declaration-slot-only placement was considered and rejected the same
day — see Appendix S; precedence inside the expression is TG15):

```lambda
fn sum(xs: (number as T)[]) T          // T = element type, NOT the array type
fn first(xs: (any as T)[]) T
fn map(xs: (any as T)[], f: fn (T) (any as U)) U[]
fn keys(m: {(any as K): any}) K[]
```

This is the main grammar + checker work in the feature.

#### TG6v2 — Bounds are checked directly from the binder record **[RATIFIED 2026-09-15]**

The bound is the annotated type at the binder (`number as T` ⇒ `T <: number`).
It is stored as a **direct field of the binder record** and enforced by the
ordinary boundary admission of the value against that type — the same path a
plain `a: number` takes — immediately before `T` is bound. The `that`-refinement
reading in TG3 explains the meaning only; no predicate is evaluated for a
bound. Rationale: constrained-type predicate enforcement at boundaries is
itself unowned (SO9; today `runtime_boundary_unwrap_type` strips a constrained
type to its base and `is` checks the base only), and a bound is a *type*, not
an arbitrary predicate — routing it through predicates would make the feature
depend on work it does not need and would leave bounds silently unenforced in
the meantime. The subtype-test operator on type values (`<:`, spelled
provisionally) is still required, by TG9 (`T` as a value) and by TG14v2's
relations; it stays an independently useful type-value API.

#### TG7 — Two tiers, one contract

- **Static tier** (argument type statically known): the checker binds `T`
  at compile time, verifies the body abstractly against the bound
  (once-for-all, §4.1 point 2), propagates the relational result type, and
  elides runtime checks it proves infallible (TE-17).
- **Dynamic tier** (argument statically `any`): `T` binds at the call
  boundary at runtime to the argument's type; subsequent `T` positions
  (other params, return) are checked as boundary contracts in the callee
  (DF8, TE-18). Lambda thus gets **runtime-relational contracts** — a
  capability erased-generics languages cannot offer at all.

Same declared contract, same observable outcomes; only error *timing*
differs (compile-time vs. first violating call), consistent with the general
static/dynamic enforcement doctrine.

#### TG8 — Specialization discipline

Instantiation and specialization follow §2.3: boxed generic entry always
exists (DF9); specializations are memoized by type identity; a statically
known `type`-valued argument (a type literal or module-level type binding) is
part of the specialization key — Zig's comptime tier as an optimization
rather than a semantic. S1.6 makes all of it unobservable.

#### TG9 — `T` is a value in body scope

The bound `T` is an ordinary `type`-typed binding inside the body:
`let b: T = ...`, `x is T`, `T.element`, passing `T` to other functions. This
falls out of TG2/TG3 and is a genuine capability beyond every erased system.

#### TG10 — Binders operate at three levels

Binders live in the **type expression**, not in a function-declaration-only
construct. The same `as T` mechanism therefore applies at three levels:

**Level 1 — `fn` definition** (the base case, §4.2 throughout): `T` relates
parameter and return positions; binds per **call**, at the call boundary.

```lambda
fn sum(xs: (number as T)[]) T
```

**Level 2 — `type` definition**: `T` relates **fields within one type**,
expressing a per-value invariant a plain structural type cannot state:

```lambda
type Cell = {a: number as T, b: fn (c: T) T, next: {value: T}?}
// invariant: whatever numeric type field `a` has, field `b` maps
// that same type to itself, and next.value carries it too
```

Binding is per **value**: when a value is checked against `Cell` (TG4
order — binder site first), `T := type(v.a)`, then the remaining
`T`-positions are checked against it. Statically, the relation propagates
when the checker knows a field's type: given `c: Cell` with `c.a : int`
proven, `c.b` types as `fn (int) int`. Shape identity stays structural
(D3.4.2) — the binder adds a relational *constraint* on the shape, not a
nominal identity; two `Cell` values may bind different `T`s (existential
reading: each value carries *some* `T <: number`, consistently).

**Level 3 — interface / schema definition**: the combination of levels 1 and
2 — a module surface (D7) or validator schema whose type aliases and function
signatures share relational structure:

```lambda
type Fold = fn (xs: (any as T)[], init: any as A, f: fn (A, T) A) A
let op: Fold = ...                 // rank-2: polymorphism survives storage

// schema: a numeric series whose ops match its element type
type Series = {
    data: (number as T)[],
    scale: fn (x: T) T,
    origin: T
}
```

A signature or schema carrying its binders is self-contained — this closes
the separate-compilation case (§4.1 point 3) with no body access; typed
variables and fields holding polymorphic functions work because the binder
scopes over the function type itself; and the validator gets relational
schema checks (level 2 semantics) for free, since schema validation is
already per-value checking.

Scope rule across all levels: a binder scopes over the **entire enclosing
type expression / signature / type body** in which it appears; it never
escapes it (the D3.3.3 discipline — the relation lives and dies with the
construct that declared it).

#### TG11 — Type-level computation is permanently out of scope

No conditional types, no mapped types, no `keyof`/`infer` equivalents.
Shape-deriving needs (`Partial`, `Omit`) are served — if ever — by ordinary
functions on type values (`fn(T: type) type`, TG2), evaluated like any other
code, not by a second type-level language.

#### TG13 — The unification road stays open, behind an explicit syntax

TG4 rejects unification as the *default*, not forever. If Lambda one day
wants the ML solver path, the surface is already reserved:

```lambda
fn f(a: number as T, b: number as T, c: T) T
//              ^^^^            ^^^^  — T bound at MULTIPLE sites
```

**Multiple binder occurrences of the same name explicitly call for
unification**: `T` must be solved to satisfy every binder site (equality, or
join within the shared bound — the solver policy is a future decision).
Occurrences *without* `as` remain what they always are: **checks** against
the solved `T`.

This keeps the two regimes cleanly separated and forward-compatible:

- **One binder** (TG4): bind-then-check, no solver — semantics never changes,
  regardless of whether TG13 is ever implemented. Existing code is
  unaffected by the extension.
- **Two or more binders**: the author has visibly *asked* for constraint
  solving; the complexity is opt-in, local to the signature, and readable at
  a glance.

Until the solver path is actually wanted, multiple binders for one name are
a **compile error** ("duplicate binder for `T`; multiple binders are
reserved for unification, which is not yet supported") — the syntax must not
be given any interim meaning, or the road closes. This subsumes the
duplicate-binder half of TGO7.

#### TG14v2 — Inter-binder relations are ordinary value predicates **[REVISED 2026-09-15]**

Type relations richer than same-`T` equality need no clause of their own.
Because `T` is a real value in scope from its binder site rightward (TG9,
TG17) and relations are **checks, not constraints to solve**, a relation is
simply a `that` value predicate on a later slot that mentions the earlier
bound name:

```lambda
fn f(a: t as T, b: t that (T <: type(~)) as U) U
// T binds from a; b's predicate is checked (T already bound); then U binds

fn concat(xs: (any as T)[], ys: (any as U)[] that (T <: U)) U[]
// U binds while the array type admits (binder nested in the base);
// the trailing predicate then relates the two bound element types
```

Rules:

- **Spelling**: the predicate is the existing `that` clause, written **before**
  `as` when both sit at the same level (TG16). No `that` follows a binder
  name; the `b: t as U that (T <: U)` form of rev 6 is retired (Appendix S).
- **Evaluation order**: a predicate may mention only names bound **at or
  before** its evaluation point — binders of earlier parameters (left to
  right, as in TG2's dependent scoping) and binders nested inside its own base
  type (they bind while the base admits, before the predicate runs). Forward
  references are a compile error; they would reintroduce solving through the
  back door (TG13's territory).
- **Generality**: `that` takes arbitrary expressions over type values, so
  `that (T.element == U)` or `that (U != null)` cost nothing extra. Surface
  grammar work is nil beyond TG6v2's `<:`.
- **Oracle agreement**: `type(~)` in a predicate must answer the same type the
  binder would bind — TGO13.
- **Static tier is best-effort, dynamic tier is authoritative**: with
  relations as assumptions, abstract body checking enters bounded-
  quantification territory (F-sub), where full static reasoning is famously
  undecidable. The checker verifies what it can and defers the rest to the
  runtime boundary check (TG7) — safe, because the dynamic check is total.
  This "incomplete static, total dynamic" posture is the same one the rest
  of the enforcement design already takes (TE-17/TE-18).

Adoption note: TG14v2's dynamic tier ships with P1–P2 (it is the existing
`that` machinery over type values); its static-tier reasoning is open-ended
and lands incrementally with P3.

#### TG15 — `as` is the loosest operator in a type expression **[RATIFIED 2026-09-15]**

Inside a type expression `as` binds looser than `that` (TG16), the set
operators `|`, `&`, `!`, and every occurrence suffix (`?`, `+`, `*`, `[]`,
`[n]`, `[n, m]`). Parentheses scope it. Consequently:

| Spelling | `T` binds to |
|---|---|
| `number[] as T` | the array type |
| `(number as T)[]` | the element type |
| `int \| float as T` | the union |
| `int that (~ > 0) as T` | the constrained type |
| `(int as T) \| string` | the `int` arm only |

Rationale: the binder is read as "this whole thing, as `T`", which is the
natural English reading and matches the `for … as alias` use of the keyword;
binding a sub-position is the deliberate, parenthesized choice. A binder
therefore never needs its own precedence entry in the reference grammar — it
is a suffix on a complete type expression or parenthesized group.

#### TG16 — `that` precedes `as` **[RATIFIED 2026-09-15]**

When a value constraint and a binder apply at the same level, the order is
`type that (predicate) as Name`: `that` binds tighter than `as`, so
`t that (P) as U` means `(t that (P)) as U`. Inside the predicate `~` is the
**value** being admitted, exactly as in every other `that` clause (S1.7: one
spelling, one meaning). A `that` after a binder name is a syntax error.
TG14v2 relations use this same clause; nothing else is needed.

#### TG17 — Binder scope, placement and lifetime **[RATIFIED 2026-09-15]**

- **Scope**: a bound name is a function-local **immutable** binding, scoped
  like a `let` in the body — visible from its binder site rightward through
  the rest of the signature (later parameters, return contract) and
  throughout the body. It is a real `type`-valued value there (TG9).
- **Collisions**: a binder name must not shadow a base type, a type alias in
  scope, or a parameter of the same signature (types and parameters share one
  value namespace; `fn f(T: type, a: number as T)` is TG13's duplicate,
  `a: number as int` is an error). Shadowing of enclosing-scope names follows
  the ordinary `let` rule.
- **Return position**: a binder in the return contract (`fn f() number as T`)
  has no value source and is a compile error. Return positions are checks.
- **`var` parameters**: `T` binds once, at entry, per call (TG10 level 1). A
  body write that changes the parameter's type never re-binds `T`; the
  CW33 home transport makes such writes caller-visible but not binder-visible.
- **`let`/`var` binders** (`let a: number as N = …`): not supported. The same
  power is `let N = type(a)` under TG9 with the oracle of TGO13, the checker
  already knows a `let` initializer's static type (that is §2.1's job), and a
  `var` form would reopen TGO9. Confirms the TGO7 residue.

#### TG18 — Container binding and the unbound binder **[RATIFIED 2026-09-15]**

- **Nested binders bind from the first element.** For `xs: (number as T)[]`
  the **first element binds** `T`; every later element is a **check** against
  it. This is TG4's first-occurrence rule applied inside the container — the
  join-of-elements alternative was rejected for the same reason unification
  was (silent widening, surprise at the use site). A typed carrier whose leaf
  lane is already proven binds that lane without walking elements (D3.3.3v3
  certificate); the observable answer is identical. Closes TGO4.
- **An empty container leaves `T` unbound.**
- **A binder site that admission never reaches stays unbound.** The
  container rule is one instance of a general one: a binder nested under an
  occurrence with zero instances, under an optional arm that absorbed `null`,
  or under a union arm not taken (`(int as T) | string` given a string) is
  never visited, so it binds nothing.
- **The bound is the whole written type (TG15).** `a: number? as T` bounds
  `T` by `number?`; a present `null` reaches the binder and binds
  `T := null` (S4.2.2, TG4), after which `b: T` admits only `null`. An author
  who wants the relation on the non-null arm writes `a: (number as T)?`:
  `null` is absorbed by the `?` arm, the binder is not reached, `T` stays
  unbound and behaves as `number`.
- **Optional parameters and defaults.** `a?: number as T` is structurally
  `a: (number as T)?` — the `?` marker on the name wraps the whole
  annotation, binder included, and an **omitted** argument is filled with
  `null` (`doc/Lambda_Func.md`), which the outer arm absorbs; so omission
  leaves `T` unbound whether or not the runtime can tell omission from an
  explicit `null`. A **fired default** (`a: number as T = 0`) likewise leaves
  `T` unbound. Omission is spelled by trailing omission or a named argument
  (`pick(b: 2)`); there is no positional hole syntax. Closes TGO11.
- **Unbound means "the bound".** Wherever a binder ended up unbound, the
  bound name behaves as its bound type — the full expression at the binder
  site: later `T` positions in the signature admit against it, the return
  contract checks against it, and in the body `T` evaluates to it (TG9).
  `sum([])` under `fn sum(xs: (number as T)[]) T` therefore returns a
  `number`; `pick(b: 2)` under `fn pick(a?: number? as T, b: T) T` checks
  `b` and the result against `number?`. A binder with bound `any` that is
  unbound is `any` — exactly what the signature promised without the binder.

#### TG19 — `<:` is a type operator **[RATIFIED 2026-09-15]**

`A <: B` is a new binary operator on type values, yielding `bool`. It holds
when **every value `A` admits, `B` admits** — the existing declared-boundary
admission relation lifted to type values, so it is implemented over the same
contract-compatibility relation the checker already uses
(`lambda_type_contract_semantically_compatible`), never as a second
subtyping. Consequences: `int <: number`, `int <: int | string`,
`{a: int, b: int} <: {a: int}` (structural, D3.4.2) hold; a nominal record
relates to its base by its record pointer (S2.1.4); `T <: T` holds for
every `T`. It is the operator TG6v2 names, TG14v2's predicates use, and TG9
exposes to ordinary code. Closes the `<:` half of TGO13; the oracle half is
closed by TG18 (containers) and S4.2.2 (scalars): `type()` must report the
same answer the binder binds, or a dedicated oracle function must be exposed
— the choice is an implementation detail recorded in Appendix I.

#### TG20 — The static tier binds the expression's type, the dynamic tier binds the value's narrowest type **[RATIFIED 2026-09-15]**

The two tiers of TG7 bind `T` from different evidence, and that is the
ruling, not a defect:

- **Static tier**: `T` binds to the **static type of the argument
  expression** at the binder position — what the checker knows, which may be
  a declared or inferred abstraction such as `number`.
- **Dynamic tier**: `T` binds to the **narrowest runtime type of the value**
  (S4.2.2), as TG18's oracle computes it.

```lambda
fn max(a: number as T, b: T) T => if (a > b) a else b

let x: number = 1
max(x, 2.5)     // static: T := number, b: number admits 2.5 — passes
max(1, 2.5)     // dynamic (literals, or any-typed caller): T := int,
                // b: int rejects 2.5 — fails with TG4's blame
```

Rationale: this is Lambda's declaration-versus-value story applied to
binders. A declared `number` is a promise the author made and the static
tier honours it as written; a bare value has only itself to offer and binds
exactly what it is. The alternative — making the static tier also bind the
narrowest type — would require the checker to prove value narrowness it
cannot see (`x` above holds an int, but its declaration says `number`), and
making the dynamic tier bind declared types would require values to carry
declarations they do not have. Either direction invents information.

Consequences accepted with the ruling:
- A call's outcome can depend on the **caller's declarations**, not only on
  the values passed — the same way every declared-boundary check in Lambda
  already does (TE-17: a statically proven check is elided, so the dynamic
  narrower check never runs for the proven call). This is not an S1.6
  representation leak: declarations are visible language, representation is
  not.
- Authors who want value-exact relations regardless of caller declarations
  spell the bound narrowly (`int as T`); authors who want declaration-level
  relations declare their arguments. Binder placement (TG4) and bound choice
  are both API decisions.
- Diagnostics name the tier that bound `T` ("`T` bound to `number` from the
  declaration of `x`" versus "`T` bound to `int` from the value `1`") so a
  passing and a failing call with equal values are explainable. Closes TGO1.

### 4.3 Open issues

- **TGO1 — CLOSED 2026-09-15 by TG20**: static binds the expression's
  static type, dynamic binds the value's narrowest type; the `max` example
  and the accepted caller-declaration dependence are recorded there.
- **TGO2 — CLOSED 2026-09-15: the binder keyword is `as`.** The S1.7
  concern was latent, not live: no expression-position checked cast exists
  (SO9 lists the `as`/`as?` cast surface as unowned). Consequence of the
  ruling: if a checked cast is ever built it must take another spelling.
- **TGO3 — Elision/inference for explicit `T: type` params.** Should
  `fn f(T: type, a: T)` allow `f(1)` when `T` is inferable? Zig says no
  (explicit always); TG3's binder makes it mostly moot. Deferred; hold the
  door open in the grammar.
- **TGO4 — CLOSED 2026-09-15 by TG18**: the first element binds, later
  elements check, an empty container leaves `T` unbound.
- **TGO5 — Abstract-body admissibility.** Static-tier once-for-all checking
  needs a table of operations valid for `T <: bound` (e.g. `+` for
  `T <: number` — including poison behavior per S4.2). Define per-bound
  operation sets rather than re-deriving per body. **Interim for P3's first
  cut**: the checker types `T` as its bound inside the body. Sound because
  `T` embeds in the bound; it forgoes only the relational precision that the
  operation tables would add.
- **TGO6 — Exported-API annotation discipline.** Inferred public signatures
  drift with body edits. For exported functions (D7 module surface), a lint
  requiring declared signatures — including binders where the contract is
  relational — instead of inferred ones. Convention vs. enforced: undecided.
- **TGO7 — CLOSED.** The duplicate-binder half is settled by TG13 (two
  `as T` in one signature is a compile error reserved for future explicit
  unification); the `let`/`var` half is settled by TG17 (not supported,
  2026-09-15).
- **TGO8 — SO9/SO33 bookkeeping.** This doc claims the generics and
  flow-narrowing items from SO9; the aspirational `fn identity<T>` text
  flagged by SO33 should be replaced by TG2/TG3 forms in `doc/Lambda_Type.md`
  when this design is adopted. Formal-spec updates (new S#/D# rulings +
  semver bumps) required at adoption per the citation convention.
- **TGO9 — Level-2 binding under mutation.** For a `var` value of a
  relational type (TG10 level 2), does writing field `a` re-bind `T` (and
  thus re-validate every other `T`-position), or is the bound-at-construction
  `T` fixed for the value's lifetime? Re-bind is the value-semantics-
  consistent answer (S1.4: each mutation yields a visibly new value, which is
  then re-checked like any boundary), but the write-time cost of re-checking
  sibling fields needs the TE-17 infallibility machinery to be tolerable.
  Decide alongside TGO4.
- **TGO10 — Type-operation additions (assess §6 catalog).** Decide which
  type-value operations to add. Candidate minimal kernel: A1 `T.fields`,
  A2 `T.field(name)`, B1 map-type construction API, plus completing A3/A4
  accessors and the already-committed `<:` (TG6). Also owns the B6 ruling:
  recursive type-function identity/memoization/fixpoint (D3.4.2 structural
  identity is the presumptive answer). Separately: close the SO9
  implementation gap for binary `&`/`!` type operators, which the catalog
  assumes. Nothing in §6 is committed until this assessment.
- **TGO11 — CLOSED 2026-09-15 by TG18**: an omitted argument or a fired
  default leaves `T` unbound; unbound behaves as the bound.
- **TGO12 — Binder records across the module/cache boundary.** The §4.1
  point 3 motivation (opaque signatures for D7 modules and MIR cache L2/L3
  artifacts) requires the binder record and bound-name references to
  serialize through the const pool (CP). Constrains the representation to be
  pointer-free/relocatable (slot indices, not `Type*` back-pointers). Later
  phase, but decide before Appendix I's layout hardens.
- **TGO13 — CLOSED 2026-09-15 by TG18/TG19**: the oracle is S4.2.2 for
  scalars and first-element-binds for containers; `<:` is the admission
  relation lifted to type values. Residue (implementation, Appendix I):
  `type()` today collapses arrays to `array` and anonymous maps to `map`
  (`fn_type`), so either it is aligned with the binder oracle or a dedicated
  oracle function is exposed for TG14v2 predicates.

---

## 5. Terminology

#### TG12 — The construct is named **Type Binder** **[RATIFIED 2026-08-08]**

Candidates considered, and why each was kept or rejected:

| Term | Verdict | Reasoning |
|---|---|---|
| **type alias** | **rejected** | Already taken: `type UserId = int` (doc/Lambda_Type.md § Type Declarations) is Lambda's type alias. An alias is a second name for a *known* type — both sides determined. `as T` names an *unknown*-until-bound type. Reusing the word would merge two near-opposite features under one name — the S1.7 (one symbol, one concept) violation in vocabulary form. |
| **type variable** | **rejected** | Standard ML/PL jargon, but it carries exactly the mindset TG4 ratified against: in the unification tradition all occurrences of a type variable are symmetric and a solver finds the assignment. TG4 semantics is bind-once-then-check — `T` behaves like a `let`, not a `var`. In a language where `var` specifically means *mutable*, calling a bind-once construct a "variable" invites the wrong intuition twice over. |
| **generics** | **umbrella only** | Kept for discoverability — it is what users search for, what the TS/C++ comparison is conducted in, and this doc's own filename; "Lambda supports generics without a generics sublanguage" is the honest pitch. But as the *construct* name it over-promises: it imports expectations of `<T>` lists, variance, and mapped/conditional types — everything TG1/TG11 refuse. Never used in the grammar or the reference docs for the construct itself. |
| **type parameter** | **kept, for TG2 only** | The explicit form `fn f(T: type, ...)` literally is a parameter the caller passes — "type parameter" is its honest name. Wrong for the `as T` form (nothing is passed; level-2 per-value binding has no call at all). |
| **type binder** | **SELECTED** | The only candidate that *encodes the ratified ruling*: TG4's content is the asymmetry between the one **binder** site (`number as T`) and the **check** sites (every other `T`). "Binder" makes the asymmetry part of the vocabulary — "the binder binds, the uses check" cannot be misread, whereas "variable" and "parameter" both suggest symmetric or caller-supplied roles. Coherent across all three TG10 levels (binds per call at level 1, per value at level 2, both at level 3) and with TG9 (`T` is a real binding of a type value in scope, like any `let`). Fits Lambda's plain-descriptive naming style ("occurrence modifiers", "constrained types", "string patterns"). |

Final vocabulary — use these consistently in all docs, diagnostics, and
grammar node names:

| Term | Refers to |
|---|---|
| **generics** (umbrella only) | the feature area — docs, marketing, SO9 bookkeeping |
| **type parameter** | explicit `T: type` parameter (TG2) — the caller passes the type |
| **type binder** | the `as T` site (TG3–TG5) — infers and binds the type |
| **bound type name** | `T` itself, in scope after binding (TG9) |

TG3 ties the vocabulary together the same way it ties the semantics:
*a type binder is sugar for an implicit type parameter.*

---

## 6. Type Computation Catalog

Type-level computation must be properly designed, not inherited by accident.
Lambda's basic approach stands (TG11): **compose types as values, in ordinary
code** — no second type-level language. But that approach is only as complete
as the *operations available on type values*. This section catalogs the
common type computations from TS/ML/Rust practice, maps each to Lambda's
current capability, and marks the gaps. Assessment of which gaps to fill is
TGO10 — nothing here is decided yet.

### Prior art: what TS's type-level computation actually buys (and costs)

Two admissions from the TS comparison (`vibe/idea/Lambda_vs_TypeScript.md`),
recorded here as the motivating prior art for this catalog:

1. **Type-level computation** — conditional types, mapped types, `keyof`,
   `infer`, template-literal manipulation. Unsound and abusable, but it lets
   libraries type APIs — ORMs that compute the result-row type from a query,
   routers that extract path-parameter names from a route string, fluent
   builders whose type grows per call — that Lambda's type language today
   simply cannot express. TS grew this second language *because its types
   are erased*: the checker is the only place computation over types can
   happen at all. The costs are equally established: an untyped, untestable,
   undebuggable sublanguage, accidental Turing-completeness held in check by
   recursion-depth caps, and type-level programs that take seconds per call
   site. (§1.2 covers the broader TS assessment.)

2. **Utility/transform types** — `Partial`, `Pick`, `Omit`, `Readonly`:
   deriving one shape from another. Lambda types are currently written out
   longhand; there is no way to compute "same map, minus field x." This is
   the single most-used slice of TS's type-level power — everyday
   application code uses `Partial<T>`, while only library authors write
   `infer` chains — so it is the bar any Lambda answer must clear first.

The catalog below decomposes both into primitive operations. Point 2 reduces
to A1 (field enumeration) + B1 (map-type construction); point 1's library
idioms reduce to the same kernel plus A2–A6 introspection and ordinary
control flow (C3) — evaluated as code (TG11), not re-derived symbolically by
a checker.

### 6.0 Type-value operations Lambda has today

| Operation | Spelling | Status |
|---|---|---|
| type of a value | `type(x)` | exists |
| type equality | `==` / `!=` | exists |
| value-matches-type | `x is T` | exists |
| union | `T1 \| T2` | exists |
| negation / exclusion | `!T` / `T1 ! T2` | designed; binary `&`/`!` type ops **unimplemented** (SO9) |
| intersection | `T1 & T2` | designed (string patterns); general type `&` unimplemented (SO9) |
| optional / occurrence | `T?` `T*` `T+` `T[n]` `T[n,m]` | exists |
| constraint | `T that (pred)` | exists |
| array element type | `T.element` | exists (arrays) |
| subtype test | `T1 <: T2` | **ratified** (TG19), unimplemented |
| type functions | `fn (T: type) type` | implied by TG2 |

### 6.1 Category A — Introspection (reading a type apart)

| # | Computation | TS spelling | Lambda today | Gap |
|---|---|---|---|---|
| A1 | **field enumeration** | `keyof T` (name union) | — | **missing.** Proposed shape: `T.fields` → `{name: symbol, type: type, optional: bool}[]` — richer than `keyof` (TS needs `T[K]` as a second step). The foundation: every mapped-type idiom starts here. |
| A2 | **field type projection** | `T["name"]` (indexed access) | — | **missing.** Proposed: `T.field('name)` → `type`. |
| A3 | **element / content types** | `T[number]`, `infer E` | `T.element` for arrays | **partial.** Missing: map value types, element (markup) attr vs content types, range/tuple positions. |
| A4 | **function type parts** | `Parameters<T>`, `ReturnType<T>` | — | **missing.** Proposed: `T.params` → `type[]`, `T.result` → `type`. |
| A5 | **union member enumeration** | distributive conditionals | — | **missing.** Proposed: `T.members` → `type[]` (singleton list for non-unions). |
| A6 | **kind classification** | `T extends X ? ...` chains | `T == array` etc. | **partial.** Comparisons against kind constants work; a direct `T.kind` (→ symbol) would replace `extends`-chain idioms. |

### 6.2 Category B — Construction and transformation (building types)

| # | Computation | TS spelling | Lambda today | Gap |
|---|---|---|---|---|
| B1 | **map type from computed field list** | mapped types `{[K in keyof T]: ...}` | type literals only | **missing — the single biggest enabler.** A type-construction API (the type-value mirror of `MarkBuilder`): build a map/element type from a runtime list of `{name, type, optional}`. Given A1 + B1, the entire TS mapped-type family (`Partial`, `Readonly`, `Pick`, `Omit`, `Nullable`, `Getters`) is an ordinary `for` loop in a `fn (T: type) type`. |
| B2 | **field add / remove / rename** | `Omit`, `Pick`, `&` merges | — | sugar over A1 + B1; no dedicated operators needed. |
| B3 | **occurrence rewrap of computed types** | `T \| null`, `T[]` | operators exist | should already work on type *values* (`let U = T; U?`) — needs a conformance test, not a design. |
| B4 | **computed field names** | template literal types `` `get${K}` `` | strings/symbols are values | covered by ordinary string ops + symbol conversion once B1 exists. No type-level string sublanguage needed — this is TG11 working as intended. |
| B5 | **tuple/length arithmetic** | tuple spreads, `["length"]` tricks | occurrence counts in types | **likely refuse**: dependent-arithmetic territory (`int[n+m]` proofs); TS itself only has it by accident. Revisit only with a concrete use case. |
| B6 | **recursive type functions** | `type List<T> = ...` recursive generics | `fn List(T: type) type => {head: T, tail: List(T)?}` — writable, semantics unruled | **needs a ruling**, not an operator: identity (`List(int)` twice ⇒ same type — D3.4.2 structural identity should decide), memoization, and fixpoint/termination for self-referential bodies. Part of TGO10. |

### 6.3 Category C — Predicates and relations (deciding about types)

| # | Computation | TS spelling | Lambda today | Gap |
|---|---|---|---|---|
| C1 | equality | (structural, implicit) | `==` | exists. |
| C2 | subtype / assignability | `extends` in constraints | `<:` (TG19) | ratified; required by TG6v2/TG14v2 and TG9 regardless of this catalog. |
| C3 | conditional selection | `T extends X ? A : B` | `if` / `match` over type values (TG9) | **exists** — ordinary control flow replaces the entire conditional-types feature. |
| C4 | union algebra / filtering | distributive conditionals, `Exclude` | `\|`, `!`, `&` + A5 | design exists; `&`/binary-`!` implementation gap (SO9). `Exclude<T,U>` = filter over `T.members` — needs A5. |
| C5 | join / meet (least common supertype) | inference-internal | — | **missing; defer.** Only consumer today would be TG13's solver policy — decide there, not here. |

### 6.4 Reading of the catalog

Three observations, ahead of the TGO10 assessment:

1. **The kernel is small.** A1 (field enumeration) + A2 (projection) + B1
   (map-type construction) + the already-proposed `<:` unlock essentially the
   whole TS mapped/conditional catalog as ordinary Lambda code. A3–A6 are
   completions of the same introspection surface. Everything in category C
   either exists or belongs to another decision.
2. **The static-tier caveat applies throughout** (§4 bucket analysis): a
   computed type is opaque to the checker until evaluated — `Partial(T)` for
   *unbound* `T` is dynamic-tier only. The catalog does not change TG7's
   posture; it only makes the dynamic/comptime tier expressive.
3. **Introspection API doubles as the reflection API.** A1–A6 are equally the
   basis for schema tooling, validators, and formatters that walk types —
   design them once, as one surface, not per-consumer.

---

## 7. Adoption Order (sketch)

1. **P0** — Subtype-test operator on type values (`<:`, TG19) + dependent
   parameter scoping for explicit `T: type` params (TG2). Small, independently
   useful, unblocks everything.
2. **P1** — Dynamic tier: runtime binding + boundary contracts (TG7 dynamic
   half). Pure runtime work, no checker changes beyond scoping.
3. **P2** — `as T` binder sugar incl. nested positions (TG3/TG5) — grammar +
   desugaring, with TG15/TG16 precedence in both the reference grammar and
   the first-party parser (the annotation token scanner and the type-pattern
   text parser both need the keyword), TG17 scope/collision diagnostics.
4. **P3** — Static tier: abstract body checking + relational propagation
   (TG7 static half; needs §2.1/§2.2 groundwork).
5. **P4** — Specialization keying on static type values (TG8; rides the
   existing dual-func/specialization infrastructure).

Flow-sensitive narrowing (§2.1) proceeds in parallel; it is the higher-
leverage half of SO9 and this design compounds with it.

---

## Appendix S — Superseded rulings

Struck-through wording is kept for the record only; the body states the
current ruling.

- ~~**TG6 (rev 1–6)** — Bounds are constraints on the type value: the bound is
  sugar for a `that` refinement on the `type` value (TG3's desugaring),
  enforced as a predicate.~~ Replaced by **TG6v2** (2026-09-15): the bound is
  a direct field of the binder record, admitted like any declared type.
- ~~**TG14 (rev 6)** — Inter-binder relations are a `that` clause attached
  *after* the binder name: `fn f(a: t as T, b: t as U that (T <: U)) U`;
  `fn concat(xs: (any as T)[], ys: (any as U that (T <: U))[]) U[]`.~~
  Replaced by **TG14v2** (2026-09-15): relations are ordinary `that` value
  predicates written before `as` (TG16); the post-name clause is a syntax
  error.
- ~~**Declaration-slot placement (proposed and withdrawn 2026-09-15)** — `as`
  attaches only to a declaration slot (parameter, field, named fn-type
  parameter) and is never part of a type expression, so `(T as b)[]` is
  illegal and element relations would need type projections (`T.element`)
  in annotation position.~~ Withdrawn the same day in favour of TG5 as
  written plus the TG15 precedence rule; projections in annotation position
  remain a TGO10 catalog question, not a binder requirement.
- ~~**TGO2 (open)** — the `as` spelling violates S1.7; shortlist `as` vs a
  `T <: number` binder-first form.~~ Closed 2026-09-15: `as`.

## Appendix I — Implementation notes (proposal, not ratified)

Kept brief per `doc/Doc_Convention.md` §4; the full plan is
`vibe/impl/Lambda_Impl_Type_Binder.md` (phases TG-P0 … TG-P4).

- **Representation** (D3.1.1v3 kinds): two new `TypeKind`s — a *binder*
  (site: bound type, name, slot index within its signature, no predicate; the
  bound is checked directly per TG6v2) and a *bound-name reference* (use:
  slot index). An explicit `T: type` parameter (TG2) is a binder whose site is
  the argument itself, so both spellings share one representation and TG3's
  "sugar" is literal in the data. Slot indices, not back-pointers, so the
  records can serialize (TGO12).
- **Binding environment**: each signature carries a binder count; a call
  frame owns `Type* env[count]`, every slot initialised to its **bound**
  (TG18: unbound behaves as the bound, so no separate unbound state is
  needed, and an `a?:` parameter's binder sits under the nullable wrapper
  that `parameter_contract_for_declared` already adds, so a filled-in `null`
  never reaches it). The runtime admitter (`lambda_type_check` /
  `runtime_type_admit_value`) takes the env: at a binder site it admits the
  value against the bound, then stores the narrowest type in the slot — for
  a container, from the proven leaf lane or else from the first element,
  leaving the slot untouched when empty; at a reference it admits against
  the slot. The oracle should be one function shared with `type()` or
  exposed beside it, so TG14v2 predicates agree with the binder (TGO13
  residue). The three boundary
  paths that already exist — the runtime check, the JIT parameter/return
  boundary emitters, and the interpreter's parameter boundary — all thread
  the same env (DF8: the check lives in the callee). The body reads a bound
  name as a `type`-valued local (a register in the JIT, a frame slot in T0).
- **Dependent scoping**: today a name in type position that resolves to a
  `T: type` parameter yields the *meta-type* `type`, so `a: T` silently means
  "`a` is a type value". The type-pattern parser's identifier path must
  instead emit a bound-name reference to that parameter's slot.
- **Lanes**: a parameter whose contract contains a binder or reference has no
  static native lane; it stays a boxed Item exactly like an `any` parameter
  under D8.1.1's satellite admission. Specialization keyed on the env is P4.
- **Static tier** (P3): substitute known argument types into references in
  the return contract at call sites; type `T` as its bound inside the body
  (TGO5 interim).
- **Diagnostics**: duplicate binder (TG13), forward reference (TG14v2),
  collision / return-position binder (TG17), and the local blame message of
  TG4 ("`b` must have the same type as `a` (int); got float").
- **Fixtures**: `test/lambda/type_binder_*.ls` with `.txt` goldens, byte-
  identical across the three tiers.
