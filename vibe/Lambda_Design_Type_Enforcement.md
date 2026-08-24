# Lambda — Type Support Design: Enforcement First

**Status:** ROUND 1 IMPLEMENTED (commit `274625d56`) — the core boundary catalog is enforced;
round-2 items remain open (value-aware numeric admission, `any \ error` fn contracts +
firewall, `or`-narrowing, fault channel), tracked in
[`vibe/impl/Lambda_Impl_Type_Enforce.md`](Lambda_Impl_Type_Enforce.md). The annotation-performance
work explicitly deferred by §1/§9 remains a separate follow-on. TE-15 (soft-error containment:
skip to the closest safe boundary) and TE-16 (the `^ { }` handler; legacy `^err` and prefix
error tests retired) were decided 2026-08-01. The handler grammar/runtime slice and the Rev 7
grammar-conformance/corpus migration are landed. The optional two-arm form
`e ^ { error_arm } ~ { value_arm }` was decided and landed 2026-08-17. Rev 6
(2026-08-12) adopts the S7.6.2v2/S7.6.3v2 postfix-primary grammar: handlers and
propagation own their mandatory carets, prefix-handler shorthand is retired, and `call_expr`
no longer owns an optional caret. Rev 5 (2026-08-06) assigns the handler-local error to `^`
(`^`, `^.field`, `^[index]`) and leaves `~` under its existing current-value rules.
**Date:** 2026-07-29; revised 2026-07-30; TE-15 and TE-16 added 2026-08-01;
postfix grammar revised 2026-08-12; legacy syntax retirement landed and two-arm handler
syntax decided 2026-08-17
**Scope:** making declared types *binding* — statically checked where provable, runtime-enforced
where not, never silently dropped or lossy. This document is **only about enforcement
(correctness)**. Leveraging annotations for faster code is the explicit *next* stage and is
touched here only where enforcement is its prerequisite.
**Related:** `vibe/Lambda_Issue_Type_Support.md` (TS-1…TS-9 issue ledger; evidence),
`doc/Lambda_Formal_Semantics.md` (semantic authority), `doc/Lambda_Type.md`,
`vibe/Lambda_Issues_Outstanding.md` OI-5 (MIR value-representation contract),
`doc/dev/lambda/LR_13_Schema_Validator.md` (validator design), and
[`vibe/Lambda_Design_Sys_Func.md`](Lambda_Design_Sys_Func.md) for
system-function admission policy.
**Evidence basis:** §§5–6 preserve the 2026-07-29 pre-implementation survey and its release
reproductions, so they remain useful as the before-state. §8.1 records the shipped implementation
and regression coverage as of 2026-07-30.

---

## 1. Goal and non-goals

**Goal.** A type annotation in Lambda is a **contract on the binding**, in the tradition of Go —
not a gradual-typing hint in the tradition of TypeScript. Every annotated boundary must resolve
to exactly one of three outcomes:

1. **STATIC-PROVEN** — the checker proves the value's type; zero runtime validation cost.
2. **STATIC-REJECTED** — the checker proves a mismatch; compile error.
3. **DEFERRED** — the source is genuinely dynamic (`any`-typed expression, parsed input data);
   a runtime check runs **at the boundary**. Success establishes the declared `T`; mismatch
   produces a diagnostic-carrying type-error *value* and does not establish or mutate the typed
   destination (TE-9).

What must never happen is the fourth outcome Lambda has today: the annotation silently ignored,
silently lossy, or silently corrupting (see §5.4 — all three are measured, including declared-int
bindings that print raw `String*` pointer bits).

Annotated and inferred bindings are deliberately different:

```lambda
let x: T = e   // contract boundary: after success, x is T; failure yields error before x exists
let x = e      // inference only: x receives e's effective type, which may be T | error
```

The second form is where batch-friendly error values flow without a panic or an imposed
must-handle channel. It must not be used to weaken the first form's contract.

**Non-goals of this stage:**

- Performance leverage of annotations (typed lanes, direct field offsets, two-entry call
  specialization). That is the next stage; this doc only establishes the guarantees that make it
  sound. TS-3/TS-4/TS-5/TS-6 stay in the issues ledger for that stage.
- Schema-driven typing of XML input (default XML is untyped/semi-typed; with a schema the data
  could be precisely typed — **KIV**, out of scope here).
- Flow-sensitive narrowing (`if (x is int) …` refining `x`), generics, and arity overloading
  (TS-8). Noted in §10 as open questions.

---

## 2. Prior art

### 2.1 TypeScript — gradual, erased, silent at the dynamic boundary

TS has no notion of "statically undecidable": every expression gets a type, and the not-knowable
cases are *spelled* as types with opposite policies. `any` is assignable in both directions —
passing an `any` into `f(b: number)` compiles silently, and since types are fully erased, nothing
checks at runtime either: `f(JSON.parse(s))` with `"abc"` inside computes `"abc" + 1 → "abc1"`.
`unknown` (added in TS 3.0 as the safe spelling) is the opposite: a compile error until narrowed.
A statically *known* wrong argument is always an error.

TS can afford the silent `any` hole for one structural reason: **types never affect
representation**. Every JS value is uniformly dynamic; a type lie produces a logic bug, never
corruption. Even so, the ecosystem spent a decade clawing the hole back (`unknown`,
`noImplicitAny`, typescript-eslint's `no-unsafe-argument`). The lesson is not "silence is fine";
it is "silence is only *survivable* under full erasure — and still regretted".

### 2.2 Python — same surface model, and both enforcement branches in one ecosystem

Python type hints (PEP 484) are the same gradual model: `Any` flows silently exactly like TS
`any`; `object` is the `unknown` analogue; checkers are external, optional and plural (mypy,
Pyright), and CPython itself never looks at annotations. But annotations *survive to runtime* as
data, and the ecosystem split into two branches that mark out Lambda's design space:

- **Hints as lint** (mypy over CPython): advisory, erased-in-effect, silent `Any` flow —
  affordable only because the runtime is uniformly boxed and every operation dynamically checks
  anyway. Hints never make CPython faster.
- **Hints as representation** (mypyc, Cython): the compiler uses `int` annotations to unbox into
  native ints — and is thereby **forced** to check at every typed/untyped boundary. A
  mypyc-compiled function type-checks its arguments when called from interpreted code and raises
  `TypeError` on mismatch. Nobody on this branch trusts silently, because a lie corrupts memory
  instead of producing `"abc1"`.

A third data point: CPython's own specializing interpreter (PEP 659) deliberately ignores hints
and specializes on *observed* types behind guards — annotations were judged too unreliable to
drive codegen. Lambda's inference + ICs are that mechanism; the ledger's TS-3 (an `int[]`
annotation downgrading an inferred `ARRAY_NUM` local to ANY) is Lambda accidentally proving the
same point.

### 2.3 Go — the direction Lambda wants

Go is the model this proposal follows. Types always drive representation; there is no silent
dynamic flow. The dynamic boundary is *explicit and always checked*: crossing out of
`interface{}`/`any` requires a type assertion `x.(T)` that panics (or returns `ok=false` in the
two-value form), and dynamic data enters typed structs through `json.Unmarshal`, which validates
against the destination type at the boundary and returns an error. Inside the boundary, code
trusts types completely — that trust is what the boundary check purchases.

### 2.4 The rule all three triangulate

> **An annotation may drive representation only if its boundary is enforced. An unenforced
> annotation must remain advisory — and must never override what inference already proved.**

Lambda today violates both halves: annotations drive representation (native lanes, typed shape
packing) without enforcement (§5.4), and annotations *downgrade* proven inference (TS-3's
ANY-downgrade). This document fixes the first half; the perf stage collects the winnings on the
second.

### 2.5 The `any` × error intersection (added 2026-07-30, backing TE-5)

How each design handles the meeting point of the dynamic/top type and failure — the question
TE-5's untyped-defaults decision answers for Lambda.

**TypeScript and Python never face the question**, because both keep failure out of the value
domain: errors are thrown exceptions, and neither type system tracks them anywhere — TS has no
`throws` clause (deliberately, permanently rejected) and Python has no `raises` annotation
(repeatedly proposed, rejected). Their `any`/`Any` trivially contain error *objects* as inert
data, but "failure" as a state never inhabits the value space. The instructive fragments:

- **TS's one failure-boundary type migrated from trusting to safe**: the `catch` variable was
  `any`; TS 4.4 strict made it **`unknown`**, forcing narrowing (`instanceof Error`) — the
  engage-explicitly instinct. Promise rejections remain untyped, which is why the ecosystem
  reinvented errors-as-values in userland (`neverthrow`, fp-ts `Either`).
- **Python's `except ValueError as e:` types the caught error precisely** (the clause filters
  by class) — engagement-at-the-boundary types the error. And Python's *batch* world is the
  strongest witness: pandas/NumPy abandoned exceptions for propagating data-level failure
  (NaN/NA flow; `fillna(0)` is Lambda's `or 0`; `np.seterr` demotes raise→propagate) — and
  replayed §10.6's packed-container problem with a worse resolution: an `int64` column meeting
  NA silently flips to `float64`, where Lambda's `int[]` refuses with a typed error.

**Koka answers it rigorously — with inference instead of a top type.** Every function type
carries an effect row (`total` = empty, `exn`, `div`, … up to `io`); **total is the inferred
default**, and `exn` is *earned* by code that raises — effects are never ambient. Declared
signatures are firewalls (annotating `: total` rejects a raising body — §10.7 independently
discovered), and the two channels exist with a first-class converter: `try : (() -> <exn|e> a)
-> e error<a>` reifies the effect into a value — literally Lambda's wrapper idiom (`^err`).
Koka has **no `any`**: the unknown is an effect-row *variable* (`map : (list<a>, f : a -> e b)
-> e list<b>`), solved rather than assumed. Lambda, being gradual, cannot row-track through
dynamic data — TE-5's `any \ error` default is the gradual approximation of Koka's row
variable defaulting empty, with checked boundaries (R3) standing in for Koka's proofs.

**Pony answers it structurally — a data-only top by construction.** Partial functions are
marked `?` on the signature *and at every call site* (`divide(a, b)?` inside `try…else…end`) —
tightness one notch beyond Lambda's immediate-expression rule. The soft alternative is a union
return (`(Item | None)`), giving authors the same enforced-vs-flowing menu as `T^` vs
`T | error`. Pony's `error` carries **no payload at all** (a bare control signal — the polar
opposite of §10.4's rich objects, and unavailable to Lambda's batch-diagnostics domain), and
because failure is never reified as a value, Pony's `Any` contains only data: "any excludes
error" achieved by never letting error be a value. Lambda cannot take that route — C4/C14 make
errors genuine data (they sit in arrays, flow through pipelines) — so the top splits instead:
`any` includes error; `any \ error` is the unwritten default. (Pony also bans partiality on
actor behaviours — across the async boundary, failure must become values/messages.)

Family footnote: **Unison** (abilities in signatures only when used, total otherwise) and
**Flix** (Boolean effect formulas, pure by default) confirm the pattern; **OCaml 5** is the
cautionary tale — effect *handlers* without effect *types*, so nothing tracks what performs
effects.

| Design point | TypeScript | Python | Koka | Pony | Lambda (decided) |
|---|---|---|---|---|---|
| Failure is… | thrown, untracked | raised, untracked | `exn` effect row | `?` partiality signal | a value (`error`) + `^` channel |
| Fn types carry failure? | no (`throws` rejected) | no (`raises` rejected) | yes, inferred rows | yes, `?` marked | yes — `T^` / `T \| error`, inferred `\| error` |
| Unannotated default | n/a (untracked) | n/a (untracked) | `total` (inferred) | total (`?` opt-in) | clean / `any \ error` (TE-5) |
| Top/dynamic type vs error | `any` incl. Error objects; `catch` → `unknown` | `Any`/`object` incl. exception objects | no top — row variables track | `Any` data-only (error not a value) | `any` includes error; `any \ error` default |
| Engage boundary | `catch` (narrow `unknown`) | `except T as e` (typed) | handlers; `try` → `error<a>` | call-site `?` + `try…else` | `^err`, `^`, `match`, `or`, typed binding |
| Error payload | Error objects | exception objects | exception values | **none** | rich object (§10.4) |
| Batch failure | userland Result libs | pandas NA + `fillna` | `error<a>`/`maybe` values | union returns | soft `\| error` flow + `or` rescue |

The family's consistent lesson, which TE-5 instantiates gradually: **the absence of failure
must be the unmarked case, and its presence must be spelled at a boundary someone can point
to** — Koka proves it by inference, Pony by syntax, Lambda by optimistic defaults over checked
boundaries.

---

## 3. Lambda's model: type is binding

Decision items are numbered **TE-n** (type enforcement) and cross-reference the TS-n issue
ledger.

**TE-1 — Annotations are semantic contracts, not hints.** `x: T` means: on successful passage
through the boundary, `x` *is* a `T`. A failed check produces the boundary's error result before
the binding is established; it never stores `error` in `x`. This holds identically for `let`,
`var`, parameters, returns, and typed fields of named map types. `var` permits the value to
change; it does not weaken or replace an explicit binding type. Every later whole-value
assignment or interior update through `var x: T` must leave the new value conforming to `T`.
By contrast, an unannotated `let x = e` or `var x = e` simply infers `e`'s effective type,
including `T | error` when `e` is open; an unannotated `var` may acquire a different inferred
type/shape after a later assignment.

TE-1 does **not** assign one global physical representation to a semantic type. The boxed path
may carry `int` as an `Item`, while an explicitly selected native function variant may carry it
as a raw integer lane. Physical representation becomes an additional local invariant only where
the implementation deliberately selects one — currently native/unboxed function bodies and
explicitly typed packed map fields (TE-3).

**TE-2 — Three-outcome resolution at every annotated boundary.** Each boundary is resolved as
STATIC-PROVEN, STATIC-REJECTED, or DEFERRED (runtime-checked) — never silently trusted:

- `S <: T` under Lambda's subtype relation → STATIC-PROVEN;
- a statically known source that is not a subtype of `T` → STATIC-REJECTED (explicit unions must
  be narrowed first; partial overlap does not silently become a runtime cast);
- only a genuinely dynamic source (`any`, unresolved runtime input/shape) → DEFERRED.

The static subtype relation, runtime value match, and explicit checked conversion are related
operations with shared numeric/type primitives, but they are not falsely identified as one
operation (TE-6).

**TE-3 — Representation invariants are local and proof-backed.** Semantic `Type*`/TypeId does
not globally determine a value's carrier. Instead, every emitter operation that requires a
physical representation must be dominated by one of:

1. a statically proven boundary plus a conversion into that carrier;
2. a successful runtime check/conversion; or
3. construction directly into an explicitly typed physical slot.

Within an unboxed function version, a proven `int` parameter may therefore remain a native
integer throughout the body. Within a map layout, an explicitly typed `int` field may remain an
unboxed field slot. The boxed version and generic maps remain valid alternative carriers of the
same semantic types. Recording and exploiting these physical proofs is deliberately separated
from enforcement correctness and revisited in the implementation/performance stage.

Maps additionally maintain a strong local layout invariant: every stored field is encoded
according to the map's **current runtime `ShapeEntry`**, and the current shape describes those
bytes exactly. A store must never place a differently encoded value into an old slot while
leaving the old shape in force. If a legal write changes a field's physical type, the runtime
builds/selects a new shape and repacks the map before committing the replacement. COW may reuse
a uniquely owned container header, but that optimization is unobservable; a shared value is
detached and the replacement is installed back into the owning binding or parent path.

**TE-4 — Runtime mismatch produces a rich diagnostic-carrying error value.** A failed DEFERRED
check yields a proper error object/value (decided 2026-07-30; see TE-9), naming the boundary,
expected type, actual type/value, source location, and validator path where applicable. It flows
like any Lambda error value — dischargeable with a braced handler, postfix `^`, `or`, or
`x is error` — and a script whose uncontained result is an error fails with that diagnostic.
Never `null`, never `0`, never pointer bits, never a silent pass-through. The earlier inline
code-only error form is rejected: a bare code cannot satisfy this diagnostic contract.

**TE-4A — Type-enforcement failure and system-function admission are different decisions.**
TE-4's “never `null`” rule applies when a value crosses a declared or deferred type
boundary: `let x: T = e`, a typed parameter or return, a typed field, or an
explicit runtime assertion. If the value cannot satisfy `T`, the boundary is
non-admissive and must produce a rich, case-specific type/value diagnostic.

The system-function contract is one layer earlier. A built-in may explicitly
classify a particular invalid type/value or empty domain as **admissive** when
the operation has a meaningful absence result and silent composition is the
more useful behavior. The target contracts include `arr[-1] -> null`,
`argmin([]) -> null`, and `varg(non_integer) -> null`. These results are not
successful claims that the input satisfied a declared type; they are the
built-in's declared nullable
result. Assigning such a result to a non-nullable `T` still invokes TE-4 and
must fail with a type diagnostic.

When absence would hide a malformed source, unreadable resource, or violated
operation contract, the system function is **non-admissive** and returns an
error value or raises through `T^E`. `parse` on malformed input and `input` on
a failed load are examples. Non-admissive paths have the same diagnostic duty
as TE-4: an anonymous `ItemError` or a bare log line is implementation debt,
not a conforming result.

This distinction keeps the two designs consistent:

| Boundary | Policy |
|---|---|
| Declared/deferred type boundary | Always non-admissive on mismatch; rich error, never graceful `null` |
| Admissive system-function operation | Contract-declared `null`, `[]`, or `""` for a safe no-answer/miss |
| Non-admissive system-function operation | Detailed `error`, or raised `T^E` for effectful operations |

The classification is semantic and function-specific; it must not be inferred
from “wrong type” alone. The authoritative system-function rules and examples
are maintained in [`Lambda_Design_Sys_Func.md`](Lambda_Design_Sys_Func.md).

**TE-5 — `any` is the top type and the gradual gate.** `any` includes `error`; `T → any` is
always assignable (though boxing need not be physically cost-free). `any → T` is DEFERRED and
uses a runtime type assertion that is **value-aware for numerics** (corrected 2026-07-30 —
see TE-6): a value whose mathematical value exactly embeds in the target passes and is
re-represented (`float(3.0) → int` succeeds as int 3); inexact values fail with the rich
error. Explicit conversion functions own lossy conversions.

`any \ error` is the non-error top type. It is the success type produced when channel-agnostic
braced handling or postfix `^` strips errors from an `any` outcome. The schema validator's
historical `any` pattern is intentionally treated as this non-error validation pattern
(`any \ error`): validation asks whether a value is usable data, while the core language type
`any` remains the true top type. This validator-specific spelling is documented until the
validator gains a distinct surface alias.

**Untyped defaults are `any \ error` — DECIDED 2026-07-30 (user restatement).** Error-ness
must always be *explicit* in a type: it enters only via `T | error`, `T^`, `error`, or an
explicitly written `any` (which includes error, above). The implicit world defaults total —
and, the restatement's sharpening, **function interfaces enforce the default**:

```lambda
fn f(a, b) { ... }         // implicitly f(a: any\error, b: any\error) -> any\error — ENFORCED:
                           //   if inference finds the return may be an error, that is a
                           //   reported type error (contain it, or declare `| error` / `any`)
fn f(a: any, b: any) any   // explicit any — the deliberate opt-in to accept/return errors
let a = ...                // a takes whatever the expression's inferred type is (incl. T | error)
var b                      // bare mutable declaration: truly `any`, error allowed
```

Calls enforce the default too: **if an argument bound to an `any \ error` param is an error,
the function is not entered — the call's result is that error** (the TE-8 short-circuit as
call semantics). Consequently, inside such a function a `match a { case error: … }` arm is
provably dead (lint candidate); a function that wants to *receive* errors declares `a: any`.

Rules that make it precise (revised 2026-07-30 after re-examination):
- **R1 — this is a rule about dynamic *reads*, and bindings inherit it.** `let x = a()` with
  `a` declared `int | error` still infers `x : int | error` — precise unions propagate (the
  decided binding rule and batch soft-flow are unchanged). The `any \ error` default is the
  inferred type of expressions that previously inferred `any`: untyped params, member access
  on dynamic maps, elements of opaque containers.
- **R2 — untyped positions are not error-transparent, and that is the right batch default.**
  TE-8's short-circuit opt-in list is *explicit* forms only (`T^`, `T | error`, `any`,
  `error`); an error dynamically reaching an untyped position short-circuits through. Pleasant
  consequence: mapping an untyped `fn` over `[1, error, 3]` yields `[f(1), error, f(3)]` —
  errors pass through unprocessed, values process; a handler that wants to *receive* the error
  opts in with `p: any` / `match`.
- **R3 — `any \ error` is a static-ledger optimism only; it never drives representation.** It
  feeds signatures, clean/open inference, and the §10.7 firewall — but every transition of an
  any-provenance value into a native lane remains a TE-8 checked boundary. So a laundered
  error (the *mainline* batch lifecycle: soft errors stored into containers/maps per §10.6,
  shuttled through dynamic structures, read back out through these very reads) degrades
  gracefully into an ordinary error value at the next check — never a sentinel, never
  corruption. The gap is confined to the ledger: some expressions typed `int` are dynamically
  `int | error`, surfaced at the next checked boundary or the script result. (This is the same
  shape as the validator's usable-data `any` policy: optimistic reading, safe because
  enforcement sits underneath. It also means the perf stage must NOT use `any \ error` to
  choose error-incapable representations on faith — only behind the same checks.)
- **R4 — container literals keep error-ness visible.** `[1, error("m"), 3]` infers element
  type `int | error`; the default applies to *opaque* containers only.
- **R5 (to confirm) — sticky `any` for explicit-`any` flows.** Reads through an
  explicitly-`any` value yield `any`, not `any \ error`, keeping that provenance
  type-visible. Note its correct size: stickiness cannot close the mainline stored-error leak
  (no explicit `any` is involved there) — R3's ledger-optimism + checked transitions carry the
  safety; stickiness just keeps deliberate `any` honest.

Rationale: without this, `any`-includes-error poisons the untyped world — everything touching
dynamic data types as `… | error`, every undeclared function infers open, and §10.7's firewall
fires on essentially all declared-plain-`T` code. With it, the provenance principle reaches the
lattice top (every error-admitting type is human-written), the validator's usable-data reading
of `any` becomes an instance of the general rule rather than a boundary exception, and
`any \ error`'s missing surface spelling is largely moot — it is the unwritten default.
Precedent: effect systems default total (Koka's effect rows, Pony's partial `?`); no design
defaults the unknown to "might fail" — full five-way comparison in §2.5. Follow-up to settle:
whether the validator's *explicit* schema-`any` flips to include error for uniformity with
annotation-`any`, or stays documented as the validation-space reading.

### The three-channel taxonomy — the error model in one statement (2026-07-30, user)

> **Lambda has exactly three failure channels. Two are types — always visible in the
> signature, always enforced. The third is never a type — the system owns it.**

| Channel | Spelling | Discipline | Designed for |
|---|---|---|---|
| **Value errors** | `T \| error` | soft — errors flow as data, no caller obligation; detected at type boundaries (`is`, `match`, annotated bindings, firewalls) | fn code: pipelines, batch, for-loops, containers |
| **Raised errors** | `T^` / `T^E` | enforcing — engaged at the immediate expression (`^err`, `^`, `match`, `or`, error-admitting destination); `raise` licensed only here | pn code, I/O boundaries |
| **System faults** | *(none — never in types)* | unchecked — stack exhaustion, out-of-memory, `==` depth-limit; transparent through fn frames; caught at a pn `^err` boundary or the global handler (C14) | the runtime |

**Why the clean default is enforced at every fn boundary — the null parallel (user,
2026-07-30).** Lambda already makes *absence* explicit at signatures: plain `T` excludes
`null`, and nullable must be spelled `T?` (TE-11, ships in P0). Error is as dangerous as null
— arguably more: a stray null yields absence, while a mishandled error is a poisoned
computation carrying a diagnosis nobody read. So error receives exactly the same treatment:
plain `T` (and the implicit `any \ error` default) excludes `error`, and error-possibility
must be spelled. **Null and error are the two failure dimensions of a signature — both
explicit, both visible, both enforced:**

```lambda
fn f() T             // present and valid — no null, no error; enforced
fn f() T?            // may be absent
fn f() T | error     // may have failed — soft, value-flowing
fn f() T^            // may have failed — handling enforced at call sites
```

---

## 4. Where typed data comes from

Lambda values acquire types from four sources; the enforcement design treats them differently
because their guarantees differ.

**4.1 Literals and expressions — inference, already precise.** Numeric literals, string/symbol
literals, array literals (`ARRAY_NUM` inference), map literals with their shapes. Inference-typed
values are STATIC-PROVEN material; nothing in this proposal weakens inference (and the perf stage
depends on not weakening it — §2.4).

**4.2 Input parsers — typed by the format's syntax.** Surveyed 2026-07-29:

- All numeric text routes through one shared scanner that promotes **int → int64 → decimal
  exactly** (`lambda/input/input-utils.cpp:153`; big integers become `decimal`, never lossy
  doubles). Float syntax (`1.0`, `1e3`, `-0`) becomes `float`.
- **JSON** produces exactly {null, bool, int, float, decimal, string, array, map}
  (`input-json.cpp:81`).
- **Mark** is the richest: adds symbols, `t'…'` datetime, `b'…'` binary, `n`/`m` numeric
  suffixes (`input-mark.cpp:520`).
- **TOML** produces strings/bools/ints/floats/decimals (`input-toml.cpp:518`). *Side-finding:
  TOML datetimes are currently unsupported — a `1979-05-27T07:32:00Z` value falls into the number
  path and yields `ITEM_ERROR` (`input-toml.cpp:611→343→396`). Filed as a side issue; not this
  doc's scope.*
- **XML is the untyped/semi-typed special case:** attribute values and text content are *always*
  `string` (`input-xml.cpp:184`, `:681`) — no inference. With a schema, XML data could be
  precisely typed at parse time; **KIV**, explicitly out of scope here (user decision
  2026-07-29).

**4.3 The consequence that shapes this design.** Format syntax can only ever establish
**primitives and generic containers**. No parser can establish a *user-defined* type — `type
Config = {host: string, port: int}` names a shape that JSON syntax knows nothing about.
Therefore the annotated binding over parsed data,

```lambda
let cfg: Config = input("config.json", 'json')?
```

is **the canonical DEFERRED boundary** — Lambda's `json.Unmarshal` moment — and requires runtime
validation of dynamic data against a user-defined type. That machinery already exists (§5.3);
it is just not wired to bindings.

**4.4 User annotations.** The subject of this document: today they are the *least* trustworthy
source of type information (§5.4 shows them ignored, lossy, or corrupting depending on the
boundary), which is exactly backwards for the Go direction.

---

## 5. Pre-implementation survey (historical baseline)

This section describes the implementation that motivated this proposal, before the enforcement
work recorded in §8.1. Its words "today", "unchecked", and line references are historical; they
must not be read as a description of the current runtime.

### 5.1 Static checker map (surveyed)

**Diagnostic plumbing.** All front-end type diagnostics flow through two helpers in
`lambda/runtime/build_ast.cpp`: `record_type_error` (`:952`), which hardcodes
`ERR_TYPE_MISMATCH` — so **every type diagnostic surfaces as E201** — and
`record_semantic_error` (`:982`) with an explicit code. The error-code space already has what a
real checker needs and never uses: `ERR_ARGUMENT_TYPE_MISMATCH=207`,
`ERR_RETURN_TYPE_MISMATCH=208`, `ERR_INVALID_INDEX=213`, `ERR_INVALID_MEMBER_ACCESS=214`,
`ERR_UNDEFINED_TYPE=204`, `ERR_UNDEFINED_FIELD=205` (`lambda/runtime/lambda-error.h:71-99`) are
referenced nowhere in `build_ast.cpp`.

**The one assignability relation that exists.** `types_compatible_with_full`
(`build_ast.cpp:694-736`; `types_compatible` at `:906` is a thin wrapper):

- NULL type on either side → compatible ("unknown types are compatible").
- `ANY` param accepts everything; `ANY` arg passes into any typed param — the code comment says
  *"any arg can pass to typed param (runtime check)"*, **promising a runtime check that does not
  exist**. This comment is the whole gap in one line.
- Numeric×numeric → `lambda_numeric_kind_exactly_embeds` (`lambda/runtime/lambda-number.hpp:346-383`):
  identical kinds; sized-int widening by width/signedness; `INT → {INT64, INTEGER, FLOAT,
  DECIMAL}`; `INTEGER → DECIMAL`; `{F32, FLOAT} → {FLOAT, DECIMAL}`. **FLOAT→INT is rejected**
  (`:379-382`) — the exact-embedding lattice is value-safe by construction.
- Same `type_id`; typed-array annotation compatibility; union arms (only reachable when the
  extended type survives — see below); `number` accepts any numeric.
- It never compares map/element **shapes**, never compares function signatures, never inspects
  `TypeConstrained` predicates.

**Where checks actually run today** (complete list):

| # | Boundary | Site | Notes / escape hatches |
|---|----------|------|------------------------|
| 1 | Call args, value params | `build_ast.cpp:2673-2690` | `types_compatible_with_full` + typed-array fallback; NULL arg type skips silently; loop stops at shorter of args/params, so arity mismatch isn't type-checked here |
| 2 | Call args, `var` (inout) params | `:2660-2670` | `type_exact_match` — stricter, correctly so |
| 3 | Constant sized-int conversion `i8(x)` | `:2596-2612` | overflow check on constants only |
| 4 | Declaration, `[T]` array-arity | `:5124-5141` | only for literal-array RHS |
| 5 | Declaration, typed-array element type | `:5142-5177` | only when annotation is `TYPE_KIND_UNARY` **and** RHS is a literal array |
| 6 | Re-assignment to annotated `var` | `:7995-8027` | ANY/NULL value short-circuits; `NUM_SIZED`/`UINT64` destination accepts *any* numeric source |
| 7 | Fn body vs declared return | `:8379-8392` | **vacuous for braced bodies**: `build_content` types every content block `ANY` (`:8607`), and ANY passes — the check can only ever fire on single-expression bodies |

**Unchecked entirely:** scalar declaration initializers, map/element literal fields vs a named
map type, union annotations on declarations, index expressions, member expressions, `is`
operands. There is no separate type-checking pass over the AST; checking happens (or doesn't)
inline during construction.

**Four structural root causes**, which the design in §7 must remove rather than patch around:

1. **One type slot, wrong build order.** `AstNamedNode::type` holds *either* the annotation *or*
   the inferred type — the annotation overwrites the initializer's type (`build_ast.cpp:5073`),
   and the initializer is built at `:5065` **before** the annotation is even parsed at `:5071`.
   There is no expected-type pushdown and, after the overwrite, nothing left to compare. The
   declaration checker cannot be "added" without first keeping both types alive at the site.
2. **Annotation-ness is `var`-only metadata.** `NameEntry::has_type_annotation` is written at
   exactly one place — `build_var_stam` (`:7830`). Functional `let` bindings don't set an entry
   at all; the MIR backend *re-derives* "was annotated" by comparing types against a numeric
   whitelist (`transpile-mir.cpp:6499-6508`). Enforcement needs the fact recorded, not guessed.
3. **Extended types survive only on parameters.** `TypeParam::full_type` carries union/
   occurrence/map types across the byte-copy at `build_ast.cpp:8087-8107`; declarations have no
   equivalent, which is why union *params* are checked and union *declarations* are ignored
   (t6).
4. **Named-shape adoption replaces instead of checking.** `build_assign_expr:5077-5121` rebuilds
   the literal's `ShapeEntry` chain taking each field's `type` **from the declared shape**,
   silently discarding the literal's inferred field types — no field-by-field comparison exists.
   At runtime `set_fields` (`lambda/core/lambda-data.cpp:667`) then switches on the *declared*
   type and calls `item.get_int56()` on a boxed `String` Item (`:709`), storing tagged-pointer
   bits — the t4 corruption, end to end.

**Backend confirmations for the catalog** (from the same survey): the declaration conversion
ladder (`transpile-mir.cpp:6603-6637`) has branches for NUM_SIZED / UINT64 / INT↔FLOAT
(`MIR_D2I` at `:6618-6623` is t13's silent truncation) / DECIMAL — and **no branch for a
non-numeric source**, so a `String*` register is `MIR_MOV`'d verbatim into an INT-tracked lane
and later boxed as an int (t3's pointer bits). `fn f() int { "abc" }` returns 0 because the
ANY-typed body is unboxed via `emit_unbox(…, LMD_TYPE_INT)` → `it2i` fall-through
(`transpile-mir.cpp:14877-14888`, `lambda-data.cpp:400`). The t2 `var`-vs-`let` difference is
front-end-identical; the observed `null` most plausibly comes from statement-value emission
(`AST_NODE_VAR_STAM`'s own value is `ITEM_NULL`, `transpile-mir.cpp:12139-12147`) — mechanism
plausible but not step-verified.

**`is` and `match` (the runtime test surface):** `is` performs no static operand checking and
lowers to either an inlined constrained-type test (`transpile-mir.cpp:4592-4677` — note the MIR
inline path **does** evaluate `T where …` constraint bodies at `:4601-4627`) or a call to
`fn_is`. That creates a three-way divergence for constrained types: MIR-inlined `is` evaluates
the constraint, `fn_is`'s own constrained branch returns true without evaluating it
(`lambda-eval.cpp:1139-1163`), and the validator rejects constrained types as unsupported
(§5.3). One more reason for TE-6's single relation.

### 5.2 Emitter and runtime-converter map (surveyed)

**The native surfaces.** Params may be carried raw for `INT, FLOAT, BOOL, STRING, INT64, UINT64`
(`is_native_param_type_id`, `lambda/lambda.h:1173-1177`); returns only for `INT, INT64, UINT64,
FLOAT, BOOL` (`mir_is_native_scalar_value_type`, `transpile-mir.cpp:1485-1491`) — with `STRING`
widened *ad hoc* at four call sites (`:2507`, `:2539`, `:8348`, `:11518`). That param/return
asymmetry is TS-2's root, now localized.

**One conversion primitive, no checked variant.** Every representation change funnels through
`emit_unbox` (`transpile-mir.cpp:2112-2160`) — **83 call sites** — whose `default:` arm silently
returns the register unchanged for unknown TypeIds (`:2158-2159`). There is no checked wrapper
anywhere. The boundary-relevant sites:

| Boundary group | Emission sites (17 primary) |
|---|---|
| Call arguments | `:10475` (ANY→native, **no tag test**), `:10480` (mismatch box/unbox), `:10493` (defaults); TCO twin `:10296`, `:10299`; ABI trampoline `:14028`; boxed prologue `:14694` |
| Declaration init | ladder `:6604-6634` (`:6636` DECIMAL arm; `emit_coerce_value_to_declared` `:6605`,`:6609`); reassignment twin `:11517` |
| Returns | explicit `:11314`,`:11318`; implicit body `:14882`,`:14886`; boxed `emit_coerce_boxed_to_declared` `:11327`,`:14907` (acts only for NUM_SIZED/UINT64 — INT/FLOAT/BOOL/STRING pass unchanged, `:2056-2069`) |

Secondary trusting sites needing eventual coverage: global-var load `:2503-2508`/`:2538-2541`,
member-access unbox against declared member type `:8346-8350`, method-`self` field hoist
`:14630-14644`, index-result re-tag `:8422-8432`.

**The call boundary in detail.** Three-arm ladder (`:10464-10502`): same-type passes raw;
ANY/NULL → `emit_unbox` **with no type test**; any other static mismatch → box-then-unbox, also
unchecked. The native-variant callee prologue does *nothing* — the declared type is simply
asserted over whatever bits arrived in the lane (`:14685-14690`). And a **missing argument with
no default is padded with a literal native `0`** (`:10499-10503`).

**The declaration ladder** (`:6604-6634`) handles NUM_SIZED, UINT64, FLOAT←INT (`MIR_I2D`),
INT←FLOAT (`MIR_D2I` — t13's silent truncation), and DECIMAL (box/unbox, the recently added
coercion). **There is no `else`**: `int ← string` falls through and the raw `String*` is
`MIR_MOV`'d into the INT-tracked I64 lane (`:6669-6676`) — t3's pointer bits. `type_to_mir` maps
everything but FLOAT to `MIR_T_I64` (`:355-364`), so MIR itself cannot object (the §0
unprobeability).

**Typed arrays are the one bright spot — with two holes.** `ensure_typed_array`
(`lambda-data-runtime.cpp:2978-3127`) really does check: unsupported element types (`bool[]`,
`string[]`) log and return NULL (`:3124-3126`), and mismatched elements on the generic-`Array`
source path are rejected per element (`:3063-3070` int, `:3084-3090` float, …). The NULL is
turned into a hard fail-stop by `emit_return_item_error_if_zero`
(`transpile-mir.cpp:1763-1776`, wired at `:6589`,`:6596`,`:14731`) — **the only declared-type
boundary in the entire emitter with a real runtime failure path**, and the template for TE-8.
The holes: (1) the ARRAY_NUM→ARRAY_NUM cross-convert path (`:3018-3054`) converts blindly with
no element checks; (2) TS-7 — the unsupported-element case is a *runtime* fail on a construct
the front end accepted, where it should use checked generic-array storage rather than making
the semantic type depend on packed-carrier support. And after the coercion succeeds,
the binding is downgraded to ANY (`:6598-6599`) and — worse — the stored value is a **raw
untagged container pointer**, not a tagged Item; it survives only because `Item::type_id()`
dereferences when the tag byte is 0 (`lambda/lambda.hpp:115-129`).

**Typed element writes are contract-dropping by design.** The declared element type only ever
*selects a fast path*, never a check (`:12333-12337`, `:12527-12533`); guard failure falls back
to `fn_array_set`, not to an error. `fn_array_set` (`lambda-eval.cpp:5784-5889`) handles a
mismatched value by calling `convert_specialized_to_generic` (`:5872-5878`), which rewrites the
array **in place** to a heterogeneous `LMD_TYPE_ARRAY` (`:5776-5778`) and stores the value — no
error, no log. So `var a: int[] = …; a[0] = "abc"` silently dissolves the annotation's
representation. (t8's observed `null`: the ANY-downgraded binding holds a raw untagged pointer,
the boxed-type guard shifts it `>>56` and gets 0, so every read takes the permanent slow lane
whose out-of-bounds arm yields `ItemNull` — `:8379`, `:9184-9198`.)

**Index reads:** out-of-bounds yields `ItemNull`, never an error (`:8379`); the
`BOXED_INT` result mover re-tags whatever was loaded as INT unconditionally (`:8422-8432`);
narrowed-but-unguarded ARRAY_NUM reads are raw memory reads with no check (`:8930-8950`).

**Converter fallback semantics** (`lambda/core/lambda-data.cpp`) — the value a wrong type
silently becomes:

| Converter | Mismatch result | Note |
|---|---|---|
| `it2i` (`:368-400`) | `0`; ERROR → `0`; inexact DECIMAL → `INT64_MAX` | "callers should check type before calling" — none do |
| `it2l` (`:409-435`) | `INT64_MAX` sentinel; **no ERROR arm** | same expression yields `0` or `INT64_MAX` depending on `int` vs `int64` annotation |
| `it2d` (`:316-341`) | `NaN` — comment: *"was 0.0 — silent data corruption"* | **precedent: this fallback was already fixed once, in the poison direction** |
| `it2b` (`:343-366`) | no failure value — objects are `true` | |
| `it2u` (`:444-453`) | `0`, raw C casts | |
| `it2s` (`:455-466`) | **`nullptr` handed to JIT'd code as a native STRING lane**; ERROR → static `"<error>"` | a `len()` on that lane is the TS-2 segfault family |

**Available trap machinery** (what TE-8/TE-9 can build on): the `ITEM_ERROR` value lane and
`emit_return_item_error_if_zero` (the ensure_typed_array template); `emit_return_if_item_error`
(`:1778-1790`); the `T^E` dual-lane return ABI (`RETURN_LANE_ERROR` epilogue `:1042-1063`) —
with the **key structural constraint** that a function whose `TypeFunc::can_raise` is false has
*no error lane at all*, and `emit_function_error_return` then degrades to returning
`ITEM_ERROR`'s bit pattern in the native lane (`:984-987`); `set_runtime_error_no_trace`
(extern "C", JIT-callable, `lambda-eval.cpp:116-133`) with `ERR_TYPE_MISMATCH` already used for
runtime operand errors (`lambda-eval.cpp:336-443`); `fn_error` registered in the JIT symbol
table (`sys_func_registry.c:538`). And one near-miss: `ts_assert_type`
(`lambda/ts/ts_runtime.cpp:124-145`, registered at `sys_func_registry.c:3010`) is a runtime
type assertion **already in the symbol table** — but soft by design (logs and returns the value
unchanged, "matches TS semantics") and never emitted by the Lambda MIR path. Lambda needs its
hard sibling.

### 5.3 Validator assets — what enforcement can reuse (surveyed)

The schema validator is the big reusable asset, and the survey's headline is that **no
parallel type-graph bridge is needed**. Runtime value-layout conversion is a separate question:

- **One `Type` representation, shared.** The validator consumes the exact `Type` hierarchy the
  AST builder produces for script `type X = …` definitions — `TypeMap`/`ShapeEntry`
  (`lambda/lambda-data.hpp:321`/`:302`), `TypeArray` (`:265`), `TypeElmt`, `TypeBinary`,
  `TypeUnary`, etc. Schema `.ls` files are loaded through the *real transpiler front end*
  (`SchemaValidator::load_schema`, `lambda/validator/doc_validator.cpp:201`), and the former
  parallel `TypeSchema` model is deleted (`validator.hpp:22-26`). Named references nested inside
  a definition are already resolved to direct `Type*` pointers at AST-build time
  (`build_ast.cpp:2880-2886`).
- **A runtime-callable `(Item, Type*)` entry point exists and is already used by the language.**
  `SchemaValidator::validate_type` (`doc_validator.cpp:536`), C wrapper
  `schema_validator_validate_type` (`doc_validator.cpp:776`). The `is` operator (`fn_is`,
  `lambda/runtime/lambda-eval.cpp:1115`) already calls it from JIT'd code for shapes, arrays,
  unions and occurrence types (`lambda-eval.cpp:1294` among others), against the per-context
  validator instance (`EvalContext::validator`, `lambda-data.hpp:112`, created at
  `runner.cpp:1448`). Runtime enforcement of user-defined types is therefore an *emission*
  question, not an infrastructure question.
- **Capabilities today:** primitives (exact tag), a numeric embedding lattice
  (`validator_internal.hpp:159-229`), map shapes with required/optional fields, arrays with
  element types, tuple patterns, occurrence modifiers (`?`/`+`/`*`/counts), unions (first-match,
  flattening cap 32), intersections/exclusions, anchored regex string patterns, element types
  with attributes.
- **Gaps that matter for enforcement duty** (each becomes work in §7/§8):
  - Maps are validated **open only** — extra fields are never detected; `allow_unknown_fields`
    and `strict_mode` are parsed and stored but read by zero validation functions
    (`validate.cpp:428` iterates only the schema's `ShapeEntry` chain).
  - `TypeConstrained` (`T where …`) falls to "Unsupported type" in the validator
    (`validate.cpp:661-664`), and `fn_is` **returns true after checking only the base type** —
    the constraint body is never evaluated (`lambda-eval.cpp:1139-1163`, TODOs at `:1158,:1161`).
  - Nominal `TypeObject` checks (inheritance walk, `constraint_fn`) exist **only** in `fn_is`
    (`lambda-eval.cpp:1267-1288`), not in the validator.
  - Numeric subsumption is implemented twice — `numeric_type_subsumes` in `fn_is` vs
    `validator_numeric_type_embeds` in the validator (`validator_internal.hpp:181`) — with a
    comment *claiming* they agree. TE-6 unifies.
  - Validation failure detail is invisible at runtime: `print_validation_result` writes to
    `log_debug` only (`error_reporting.cpp:346`); `fn_is` discards the error list. Fine for a
    boolean `is`, not fine for a failed binding — TE-4 needs the error path surfaced.
  - `input()` has **no schema hook**: its options map recognizes exactly `type` and `flavor`
    (`lambda-eval.cpp:2988,:3004`); the natural insertion point for an optional `schema:` key is
    right there (`:2981-3018`).

### 5.4 Measured behavior catalog (2026-07-29, release build; repro in `temp/tsd_t*.ls`)

The same declared type `int` meets a wrong value at seven different boundaries and produces
**five different silent failure modes and two correct rejections**:

| # | Boundary | Program | Result today | Failure mode |
|---|----------|---------|--------------|--------------|
| t14 | call argument, static | `fn f(x: int) {x}` … `f(3.5)` | `error[E201] argument 1 expected int, got float` | ✅ STATIC-REJECTED |
| t15 | re-assignment, static | `var x: int = 1` … `x = "abc"` | `error[E201] cannot assign string value to var 'x' of type int` | ✅ STATIC-REJECTED |
| — | call argument, dynamic | `a(xs[1])`, arg is `any` holding `"abc"` | returns as if `b` were `0` | silent **0** (`it2i` fall-through) |
| t1 | declared return, static | `fn f() int { "abc" }` | compiles; `f()` → `0` | silent **0** |
| t11 | declared return, dynamic | `fn f() int { g() }`, g returns `any` string | `f() + 1` → `1` | silent **0** |
| t3 | `let` declaration, static | `let x: int = "abc"` | `x` → `4563730592` | **raw `String*` pointer bits as int** |
| t13 | `let` declaration, static | `let x: int = 3.5` | `x` → `3` | silent **lossy truncation** (same conversion E201-rejected at t14!) |
| t2 | `var` declaration (pn), static | `var x: int = "abc"` | `x` → `null` | silent **null** (different path than t3!) |
| t10 | `let` declaration, dynamic | `let x: int = d.a`, JSON string | `x` → `"hello"` | annotation **ignored entirely** |
| t4 | named map type, literal | `type P = {x: int, y: int}; let p: P = {x: "abc", y: 2}` | `p.x` → `4832166160` | **pointer bits through a typed field** |
| t16 | named map type, dynamic | `let q: Q = d` (JSON map, field is string) | `q.a` → `0` | no validation at bind; silent 0 at field read |
| t8 | `int[]` element write | `var a: int[] = [1,2,3]; a[0] = "abc"` | `a[0]` → `null`, clean exit | silent **null** into typed lane |
| t6 | union annotation | `let x: int \| string = 3.5` | `x` → `3.5` | annotation ignored |
| t5 | `is` operator | `[1 is int, "a" is int, 3.5 is float, null is int]` | `[true, false, true, false]` | ✅ correct — the runtime *can* check |
| t7 | `as` cast | `3.5 as int` | syntax error | no cast operator exists |
| t9 | over-range int literal | `let x: int = 9007199254740993` | `error[E108] integer literal outside compact int range` | ✅ literal-range diagnostics exist |

Reading of the table:

- Exactly **two** boundaries are enforced (call arguments, re-assignments) — both statically,
  neither dynamically.
- The **declaration** family (TS-1) is the worst offender, with *three distinct* wrong behaviors
  for the same program shape (`let` → pointer bits, `var` → null, dynamic initializer → ignored).
  The pointer-bit leaks (t3, t4) are the §0 representation corruption made user-visible: the
  annotation selected an int representation, nothing enforced it, and a `String*` was
  reinterpreted.
- The static checker and the dynamic boundary **disagree in both directions**: `f(3.5)` is
  rejected statically but `let x: int = 3.5` truncates silently; `null is int` is `false` at
  runtime while `types_compatible` has a NULL escape hatch (§5.1).
- `is` (t5) proves the enforcement primitive already works — the runtime can decide these
  questions correctly today; it is simply never asked at the boundaries that matter.

---

### 5.5 Sys-function argument conventions (surveyed 2026-07-29)

Sys-function calls receive **no static argument checking** — the E201 argument check covers user
`fn`/`pn` only (it is guarded on `LMD_TYPE_FUNC`), and the registry's `first_param_type` field
(`sys_func_registry.c:250`) exists solely to disambiguate method-style dispatch, not to check
(it is `LMD_TYPE_ANY` for most entries). `len(123)`, `abs("abc")`, `sum("abc")` all compile
clean. At runtime there is no single convention — four coexisting patterns, all measured or
read:

1. **Silent wrong value** — `fn_len` returns `0` for scalars/null (`lambda-eval.cpp:3690`,
   `default:` arm), and `INT64_ERROR` — a sentinel in a native int lane — for error input.
   `[len(123), len(3.5), len(null)]` → `[0, 0, 0]`, and the registry's declared `&TYPE_INT`
   return means that `0` flows on as a statically-valid int.
2. **`set_runtime_error(ERR_TYPE_MISMATCH, …) + return ItemError`** — the good pattern
   (`fn_join`, `lambda-eval.cpp:336-446`), but rare: ~31 `set_runtime_error` sites against 88
   `return ItemError` sites in `lambda-eval.cpp`.
3. **`log_error` + `ItemError`** — `abs("abc")` logs `abs not supported for type: 13` (a raw
   TypeId, not even a name) and the script dies with `Script execution failed`.
4. **Naked `ItemError`** — `sum("abc")` (`fn_numeric_fold`) returns an error item carrying no
   message at all; the script dies with *zero* explanation.

Because these functions are `can_raise=false`, the error item just travels the value lane until
something surfaces it; `can_raise=true` (and with it the `T^E`/E228 handling discipline) is
reserved for I/O functions (`sys_func_registry.c:523`). So sys funcs are already "errors as
values" in shape — but mostly *anonymous* errors, which is the diagnosability failure TE-9's
diagnostic-carrying error values are designed to end. B13 records the boundary; enriching the registry with per-parameter
types (it carries only `first_param_type` today) is the prerequisite for static checking here,
and is left as an open question (§10).

## 6. Boundary inventory and gap analysis

Every place a declared type meets a value, with today's status and the TE-2 target outcome.
"static" = front-end check; "dynamic" = runtime check when the static type is ANY/unknown.

| # | Boundary | Static today | Dynamic today | Target |
|---|----------|--------------|---------------|--------|
| B1 | Declaration init, scalar (`let`/`var x: T = e`) | none (TS-1) | none — ladder falls through (pointer bits) or lossy (`D2I`) | check + checked convert |
| B2 | Declaration init, typed array (`x: int[] = e`) | literal-element check only (`:5142`) | `ensure_typed_array` — real errors, but cross-convert hole + ANY/raw-pointer downgrade | keep; close hole; keep the contract on the binding |
| B3 | Declaration init, named map / union type | none | none — shape adopted unchecked (t4), or annotation ignored (t16) | field check for literals; validator for dynamic |
| B4 | Re-assignment to annotated `var` | ✅ checked (`:7995`) | unchecked unbox (`:11517`) | checked boundary before commit |
| B5 | Call arguments | ✅ checked (`:2673`) | unchecked unbox ×7 sites; missing arg → native `0` | checked boundary; arity is a compile error |
| B6 | Declared return type | vacuous (ANY body defeats `:8379`) | unchecked unbox ×6 sites | per-return static check + checked boundary |
| B7a | Typed container element write (`a[i] = v`) | none | `fn_array_set` silently degrades the array in place | check before mutation; failure leaves the array unchanged |
| B7b | Map member/index write (`m.x = v`, `m[k] = v`) | none — builder records the left side as ANY | field setter dynamically retags/reshapes without preserving an annotated root contract | annotated root: check the post-state against its binding type before commit; unannotated root: permit shape evolution; every committed map keeps an exact runtime shape |
| B8 | Typed member read (declared field type) | n/a | unchecked unbox of `fn_member` result (`:8346`) | trust only a proven typed layout; otherwise read through the value's runtime shape |
| B9 | Parsed input → annotated binding | none | none | same as B3 — the canonical DEFERRED boundary |
| B10 | Global/module var round trip | n/a | store boxes by declared tid; load unboxes trusting it | trusted, *provided* B1 enforces the store side |
| B11 | `is` / `match` / constrained types | no operand checks | three divergent implementations (§5.1, §5.3) | shared subtype/match foundation (TE-6) |
| B12 | The `it2*` converter family | n/a | six different silent fallback values (§5.2) | boundaries stop calling them unchecked |
| B13 | Sys-function arguments | none — registry `first_param_type` is dispatch-only metadata | per-function ad hoc: silent value, logged `ItemError`, or message-less `ItemError` (§5.5) | registry-driven admission metadata; admissive contracts return their declared absence, while non-admissive failures use TE-9-quality diagnostics |

The pattern: **static checking exists exactly where someone once added it (B4, B5) and nowhere
else; dynamic checking exists exactly once (B2's coercion) and its result is then thrown away.**
Everything else trusts.

**B7b map-member assignment is a separate correctness boundary, not a variant of array
assignment.** The live builder creates `AST_NODE_MEMBER_ASSIGN_STAM`, gives its synthetic left
member `TYPE_ANY`, and performs no lookup against the root binding's declared `Type*`
(`build_ast.cpp:7916-7951`). The runtime setter already distinguishes same-physical-type stores
from type-changing stores: `fn_map_set` rebuilds a fresh `TypeMap` and repacks the data for the
latter (`lambda-eval.cpp:6798-6909,7221-7227`), while `map_set_cow` detaches shared maps
(`:6682-6693`) and the MIR assignment path installs the replacement back into the owning root
(`transpile-mir.cpp:12662-12699`). Enforcement must preserve that shape-evolution model rather
than freezing every map at its initialization shape.

There are two independent invariants:

1. **Binding invariant.** If the mutable root is annotated — `var p: Person = ...` — every
   post-state of `p`, including one produced by `p.age = v` or a nested/computed-key update,
   must conform to `Person`. `var` licenses rebinding, not type drift. A statically known
   violation is STATIC-REJECTED; a genuinely dynamic value/key is checked before commit.
2. **Map-layout invariant.** The resulting map's exact runtime shape must describe how each
   field is physically stored. A legal `int → string` field transition therefore creates or
   selects a new shape and repacks the map; it never writes string bits under an `int`
   `ShapeEntry`. The semantic binding type may be broader than that exact shape.

Those invariants produce these cases:

```lambda
type Person = {name: string, age: int}
type FlexiblePerson = {name: string, age: int | string}

var q = {name: "Ana", age: 30}
let before = q
q.age = "very old"                 // legal: q evolves to a new inferred map shape
                                    // before remains {name: "Ana", age: 30}

var p: Person = {name: "Ana", age: 30}
p.age = "very old"                 // compile error: the post-state is not Person

var f: FlexiblePerson = {name: "Ana", age: 30}
f.age = "very old"                 // legal: new exact shape, still FlexiblePerson
```

For a named literal key, the checker resolves the field through the annotated root's full
semantic type. For a computed key or dynamic value, the runtime checks the proposed post-state
against that same root contract. An unknown key is accepted only when the annotated map type is
open; the runtime extends the exact shape with a slot matching the stored value. A future
closed-map form rejects it. An unannotated root has no binding-contract check: its exact runtime
shape evolves with legal writes, and the binding's effective inferred type is updated or
honestly widened.

The operation is transactional. Evaluate and snapshot the right-hand side first, establish
that the resulting root value satisfies any annotation, then commit the COW replacement. On
mismatch, no replacement is installed, no field bytes or shape change, and the assignment
produces the rich error value. Nested writes apply the rule to the resulting root, not merely
to the final physical slot. A direct unboxed store is legal only after the semantic check and
the exact-layout proof both succeed. A uniquely owned container may be updated in place as an
unobservable COW optimization; observable semantics remain
`p′ = { *: p, field: value }`.

---

## 7. Enforcement design

### TE-6 — One subtype model, distinct operations

Use one shared type-walking/numeric foundation, but expose three deliberately distinct
operations:

1. `subtype(S, T)` — the static relation used by TE-2. It extends
   `lambda_numeric_kind_exactly_embeds` with shape/union/occurrence walking.
2. `matches(item, T)` — runtime membership used by DEFERRED checks and `is`/`match`; it asks
   whether the value's actual runtime type is a subtype of `T`.
3. `checked_convert(item, T)` — explicit conversion semantics owned by conversion functions
   (lossy and value-changing conversions live only here).

**Numeric boundary checks are value-aware — CORRECTED 2026-07-30 (user; overrides this
section's earlier Go-like-assertion wording).** At a DEFERRED boundary, a numeric value whose
*mathematical value* exactly embeds in the target is **allowed** and re-represented: an `any`
holding `float(3.0)` binds to `int` (as int 3); an integral `decimal` likewise
(`decimal_to_int64_exact` is the in-tree precedent). Inexact values fail with the rich TE-4
error (`3.5 → int`). This restores the original exact-value rule — the runtime analogue of
Swift's `Int(exactly:)`: *static knowledge rejects the class* (`let x: int = 3.0` stays a
compile error, §10.12), *dynamic checks judge the value*. Operationally the boundary check is
`matches(item, T)` extended with a value-aware numeric arm; `is`/`match` use plain `matches`
and remain type-directional (`3.0 is int` → `false`, verified 2026-07-30) — membership answers
"what is this value's type," the boundary answers "may this value satisfy this contract."
Whether `is` should also become value-aware is deliberately left undecided. Representation
conversion after the value-match (the check returns the re-represented value) is a check/
emitter detail, not a fourth relation.

*Implementation status (2026-07-30):* the freshly landed runtime check implements the
superseded reject version — `lambda_type_matches`'s numeric arm delegates to the
type-directional `numeric_type_subsumes` (`lambda-eval.cpp:1293-1300`), and `lambda_type_check`
(`:1442`) has no convert step; verified: an ANY-held `3.0` into `fn f(x: int)` errors
"type check at argument 1 … expected int, got float 3". Needs the value-aware numeric arm
(existing machinery: `validator_numeric_item_embeds`, `decimal_to_int64_exact`), with the
pass path returning the converted representation.

The shared foundation consolidates `numeric_type_subsumes` (`fn_is`),
`validator_numeric_type_embeds` (validator), and
`lambda_numeric_kind_exactly_embeds` (checker), while keeping the validator's documented
`any`-means-`any \ error` policy at its validation boundary. `FLOAT→INT` is never a *subtype*
(static, and `is`), which removes the declaration ladder's `MIR_D2I` arm (t13) — while the
value-aware boundary arm above may still admit an exactly-integral float *value* at DEFERRED
boundaries.

### TE-7 — Complete the static layer (fix the four root causes)

1. **Keep both types alive at declarations.** Reorder `build_assign_expr`: resolve the
   annotation first, build the initializer, run `subtype(init_type, declared_type)`, *then*
   store the declared type. Statically-known-wrong initializers become STATIC-REJECTED
   (`var s: int = "abc"` joins `x = "abc"` as E-errors). This closes TS-1.
2. **Record annotation-ness for every binding**, not just `var` — a `declared_type` that
   survives on the node (today's single overwritten slot is root cause 1; the emitter's
   whitelist re-derivation at `transpile-mir.cpp:6500-6508` is the workaround to delete).
3. **Extended types survive on declarations** the way they do on params (`full_type`
   equivalent), so union and occurrence annotations are checkable at declarations (t6).
4. **Named-shape adoption checks before it adopts**: field-by-field `subtype(literal field,
   declared field)` in `build_assign_expr:5077-5121`, with missing-required-field diagnostics.
   Open named types accept and preserve extra fields; a future closed form diagnoses them.
   Closes the t4 corruption at the front door.

Plus: per-return-site checking against the declared return type (the `:8379` check is kept but
no longer the only line of defense — each `return e` / final body expression is checked where
its type is known, killing the vacuous-ANY hole for the static half); call arity becomes a
diagnostic (today's argument loop stops at the shorter list and the emitter pads `0`).
`bool[]`, `string[]`, and other semantically valid typed arrays remain legal using checked
generic-array storage; lack of a packed `ArrayNum` carrier is an optimization limitation, not a
type-system error. Add B7b's annotated-root member-write check at the same static layer:
statically prove or reject the root's post-state, while leaving unannotated shape evolution
legal.

Diagnostics stop funneling everything into E201: the dormant codes exist and get used
(`ERR_ARGUMENT_TYPE_MISMATCH=207`, `ERR_RETURN_TYPE_MISMATCH=208`, `ERR_UNDEFINED_FIELD=205`,
…, `lambda-error.h:71-99`).

### TE-8 — The checked boundary (the DEFERRED half)

One boundary primitive, conceptually
`emit_checked_boundary(mt, reg, expected_type, site)`, replaces bare `emit_unbox` at the
boundary sites. It accepts the full `Type*`/type-list reference, not only a TypeId, so unions,
shapes, occurrences, and named types remain expressible. Emission shape:

```
actual = item_type_id(item)                 // canonical tag-0-aware query
if actual == error                     → pass it through unchanged — "error in, error out":
                                          the boundary's result IS that error (original
                                          diagnosis preserved; error-admitting targets keep it
                                          as a value, clean targets propagate it onward)
elif fast_simple_match(actual, expected) → establish proof and convert carrier if needed
elif item matches expected_type        → establish proof; preserve/normalize carrier as required
elif exact-value numeric admission (TE-6) → convert and return the re-represented value
else                                   → lambda_type_error(expected, item, site)
```

The runtime slow path returns either a proof-backed value suitable for the chosen carrier or a
proper diagnostic error object; it never returns `0`/`NaN`/`nullptr` on mismatch. The `it2*`
family remains available to pre-verified internal callers, but annotated boundaries stop being
routed through it unchecked.

Fast-path cost is one predictable branch on bits already in a register — the same shape as the
guards the IC machinery already emits everywhere. The perf gate in §8 holds it to noise on the
typed benchmark column.

Correctness does not depend on where a later backend chooses to place or deduplicate the check.
A boxed-entry prologue is the likely implementation for dynamic calls, while declarations,
returns, and writes remain site-local. TE-14 records this as a performance/implementation
direction rather than a semantic prerequisite.

### TE-9 — Failed checks produce error **values** (decided 2026-07-29; supersedes rev 1's panic recommendation)

Three options existed; two are rejected by decision:

1. **No panic mode.** Lambda's semantics must stay friendly to batch processing — one bad
   record in ten thousand must not kill the run. A type failure is a per-item outcome, not a
   process-fatal event.
2. **No forced `T^E`.** `T^E` is the *user's* declared channel; conscripting it for implicit
   checks would virally impose `^err`/E228 handling on every caller — error checking spilled
   all over Lambda code.
3. **Therefore: error return values.** A failed DEFERRED check yields a diagnostic-carrying
   error value in the value lane — Lambda's existing errors-as-values model (and the Jube
   C-ABI principle: errors as return values everywhere). At an annotated destination, that
   error is the boundary operation's result; it is not stored in the declared `T`.

**Annotated and inferred outcomes stay distinct.**

- `let x: T = e` checks `e`; success establishes `x: T`, while failure produces the error
  outcome before `x` exists.
- `let x = e` establishes `x` with `e`'s inferred effective type, including `T | error`.
- A statically proven call to `fn a(b: int) int` has result `int`. A dynamically checked call
  is open at the call boundary because parameter validation can fail, so that call expression
  has effective type `int | error`; on success the callee body still sees `b: int`.
- Every fn return is an effect firewall — a declared plain-`T`, or the implicit `any \ error`
  contract of an unannotated fn (2026-07-30 restatement): an error-possible body is a compile
  error, resolved by containing, disclosing `T | error`, or imposing `T^`.

Boxed storage is therefore required for inferred/open outcomes, not for a successfully
established annotated `T`. A future native variant may exploit the clean proof, but the semantic
rule does not depend on that optimization.

**Error short-circuit rule.** An error value arriving at any subsequent DEFERRED boundary
becomes that boundary's result without further checking — "error in, error out", the
`GUARD_ERROR1` convention generalized. Propagation is therefore *implicit*; containment is
*explicit* and placed where the user chooses (`let x^err = …`, postfix `^`, `or`,
`x is error`). This is what keeps the no-spill promise: nothing forces handling for an inferred
`T | error`, and errors surface at the batch boundary where the user aggregates results.

**Sys functions.** The normative convention is now the two-way admission policy in
`Lambda_Design_Sys_Func.md`: each function classifies its invalid type/value cases as either
**admissive** (return the documented result-domain absence, such as `null` for `argmin([])`)
or **non-admissive** (return a detailed `error`, or raise through `T^E`). Neither branch may
silently produce a wrong value or a message-less death. A future perf split mirrors this
contract: per-function clean/open versions (`len(non_error_data) int` vs
`len(any) int | error`) are selected only after the caller's static argument classification,
while admissive result types remain visible in the public signature.

Open results travel boxed, while a backend may use native lanes where cleanness and carrier
proofs establish that no error can occupy the lane. The exact entry/return ABI is deferred to
the implementation/performance stage (TE-14).

For validation failures at named-type boundaries (TE-10), the error value carries the
validator's path detail (`.field[3]: expected int, got string`) — which requires surfacing
`ValidationError` detail that today dies in `log_debug` (§5.3).

### TE-10 — User-defined types: the validator becomes the runtime enforcer

At a DEFERRED B3/B9 boundary (`let q: Q = <dynamic>`), emit a call to the **existing** entry
point `fn_is` already uses: `schema_validator_validate_type(ctx->validator, item,
const_type_with_tl(type_index))`. On failure → a TE-9 error value carrying the path detail,
with no `q` binding established.

No **type-graph** bridge is needed: schema types and script types are the same `Type*` graph.
That does not imply that a parsed map already has the declared map's packed physical layout.
Validation initially establishes the semantic named-type contract while reads continue through
the value's runtime shape. Canonicalizing/repacking a validated generic map into a declared
layout — including preservation of open extra fields — is a separate implementation/performance
decision required before direct-offset field access can use that layout.

- **Depth: deep, on first crossing.** The value either satisfies `Q` in full or the boundary
  yields a rich error without establishing the binding. (Witness caching to skip re-validation
  of already-validated subtrees is perf-stage work; correctness first.)
- **Openness: named map types are open by default — DECIDED 2026-07-29 (user).** Extra fields
  pass, matching the validator's current behavior, Go's unmarshal, and structural-width
  subtyping. A closed form (wiring the already-parsed `allow_unknown_fields`/`strict_mode`
  flags to actual checks) remains a possible future opt-in.
- **Constrained types (`T where …`) — DECIDED (§10.14)**: the validator is the checker and will
  eventually evaluate constraint predicates (the MIR-inlined `is` already does). The accepted
  interim scope is deliberately simpler: enforcement validates only the base `T`, and the
  predicate refinement remains a clearly documented validator follow-up. This does not block
  the base type-enforcement rollout.
- Nominal `TypeObject` checking (today `fn_is`-only) reuses the shared subtype/match foundation.

### TE-11 — Null policy

Today `null` passes `types_compatible`'s escape hatch statically, while `null is int` is
`false` at runtime — the two halves disagree. **Decision: plain `T` does not admit `null`;
optionality is spelled `T?`** (the occurrence form already exists in the grammar and the
validator honors it via `is_type_optional`). ANY/NULL boundary values hitting a plain-`T`
DEFERRED check yield an error and do not establish the binding. **DECIDED 2026-07-29 (user):
ships in P0** together with the rest of the
static layer — no warn-only interim release. Migration risk is handled by the P0 baseline gate:
any code relying on null-through-annotation surfaces there and is fixed with `T?`.

### TE-12 — What enforcement deliberately does not touch

Inference stays authoritative where there is no annotation (§4.1) — enforcement never inserts
checks on inferred-only paths. TS-9 (int→float overflow) stays as specified in
`Lambda_Formal_Semantics.md` §4.1. The COW/borrow semantics of map-typed locals (TS-4/C4), the
region-producer gate (TS-6), the ANY-downgrade fast-path losses (TS-3), and the dead
direct-field-offset path (TS-5) are all perf-stage items — though note TE-10 finally gives
named map annotations a *meaning* (a validated contract), resolving TS-5's "cost without
benefit" in the semantic direction. TE-3's local carrier proofs, plus an eventual layout
canonicalization decision, are what can later make the TS-3/TS-5 fast paths safe to re-enable.

### TE-13 — Unified discharge surface over the two error forms (revised 2026-07-29, user)

> **Historical record.** The pre-TE-16 analysis below retains the then-proposed `^err` and
> prefix-test spellings. The landed surface is the TE-16 ruling below: braced handlers,
> postfix propagation, and `is error`, per **S7.6.5v2**.

**Two error forms, distinguished by obligation — different types, not one type.** (This
revises the earlier draft's "one type, two provenances" identification.)

1. **`fn a() int^` — the enforcing/originating channel.** Raise-capable; callers **must**
   handle (E228). The `^` spelling is **explicit-only — inference never produces it.**
2. **`fn b() int | error` — the non-enforcing union.** Error is simply one of the possible
   values; it flows freely with no call-site obligation. **This is the form openness inference
   produces**, and it is the form the formal semantics already assigns to system-fn value
   channels (§7.3: "`T?` or `T | error`, never `T^E` on a system fn").

In **value positions** (bindings, parameters), the two spellings are **equivalent** — decided
2026-07-29: `let x: int^ = a()` and `let x: int | error = a()` mean the same thing; the binding
carries the outcome, value or error. `^` is semantically distinctive only as a *function
return* marker, where it adds the enforcing obligation and the `raise` license.

The provenance principle thereby becomes *syntactically manifest*: `^` is always a user-written
explicit fact, enforced strictly; `| error` is the inferable fact that flows. **`raise` goes
with `^` — DECIDED 2026-07-29 (user).** `raise` is licensed only by a declared `^`; in a
`T | error` function, user code constructs `error(...)` and **returns** it as an ordinary
value — `raise` there is a compile error (the existing plain-`T` raise restriction extends to
the union form). Channel ↔ verb: `^` raises, `| error` returns — exactly §7.3's "fn return
error; pn raise error" given surface spellings.

**Acknowledgment forms (DECIDED 2026-07-29 — generalization ratified): must-handle =
must-engage-explicitly, at the immediate expression.**
E228 guards against *unawareness*, not against deferred branching — so it is satisfied whenever
the enforcing call's result is received by a context that **textually** engages the error
possibility:

1. a braced handler or postfix-`^` propagation;
2. a binding, parameter, or declared return whose explicit type admits `error` — `T | error`,
   `T^` (equivalent in value positions), or `error` itself. The binding carries the outcome as
   a union, and ordinary assignability already enforces engagement-before-plain-`T` for both
   spellings — the §7.3 wrapper idiom in one step, and the batch idiom for collecting
   enforcing-call outcomes. The declared-return form counts **only for the call in return/tail
   position** (`return f()`, or the final expression) — see tightness below. **`any` never
   counts** — it admits error but acknowledges nothing;
3. a `match` with a `case error:` arm.

Bare `let x = a()` remains an error — Lambda stays stricter than Zig/Rust/Go, all of which
accept an untyped capture. The §10.7 firewall backstops the demotion path: an acknowledged
error still cannot silently escape a declared plain-`T` interface. Implementation: one added
condition at the existing E228 site (`build_ast.cpp:5197` — "declared type explicitly admits
error") plus a third suggestion in the diagnostic text.

#### Common binding acknowledgments

For ordinary variable binding, the three common forms are:

```lambda
let value = expr ^ { ^ }       // preserve the soft error value
let value = expr ^ { null }    // intentional fallback value
let value: T | error = pn_call // explicit union binding
```

The third form is an acknowledgment boundary under **S7.5.1**: success binds `T`, while a
hard raised error is captured as a soft `error` value in `value`. It neither re-raises nor
chooses a fallback. The first form uses the handler to preserve the error; the second
acknowledges it and replaces it with `null`, per **S7.6.1v3/S7.6.2v3**. These forms replace
the retired `let value^err = pn_call` split under **S7.6.5v2**.

**Acknowledgment tightness (DECIDED 2026-07-29, user): tight everywhere for `^` — keyed on the
form, not on fn vs pn.** The acknowledgment must be the **immediate expression surrounding the
call**; never distant, never scope-level. In particular, a declared error-admitting return does
*not* retroactively acknowledge non-tail calls:

```lambda
pn p() T | error {
    f()        // compile error if f is enforcing — discarded outcome, no acknowledgment
    g()        // same — and f's unhandled failure could be the very cause of g's
    return r   // the declared return covers only THIS expression, not the calls above
}
```

Rationale: the `^` channel is *designed* to demand explicit acknowledgment — primarily for pn
code, where an unengaged failure invalidates every subsequent command (§7.3: "commands halt on
failure"); fn may use `^` too, under the same discipline. No laxer rule for fn is needed,
because the relaxed pattern **already has its own spelling**: a callee that wants callers to
write `let a = b()` and let errors flow simply declares `T | error`. The two forms are the two
intended design patterns — the callee author chooses the caller discipline by choosing the
form:

| Pattern | Declare | Caller experience |
|---|---|---|
| Urgent — must acknowledge | `T^` | engage at the immediate expression, everywhere |
| Relaxed — value-flowing | `T \| error` | `let a = b()` is fine; the error propagates as a value |

**The soft form's contract (decided 2026-07-29, user): flow through the interior, detect at
the type boundary.** Softness is *desirable* for fn code — pipelines, batch processing,
for-loops, composed containers — because one returned error cascades like a normal value and
**does not abort** the surrounding computation. The cost, *by design*: the error is likely
embedded in the eventual result rather than surfacing upfront. The mitigation is the type
system itself — typed containers exclude error by element type (§10.6), so a type pattern
harvests embedded errors at whatever boundary the user chooses. All three legs verified live
2026-07-29:

```lambda
[123, error("m"), 456]              // → [123, error, 456] — composition holds the error
for (x in [1, "abc", 3]) int(x)     // → [1, error, 3]     — one bad element, batch continues
type IA = int[]
[1, error("m"), 3] is IA            // → false             — the type pattern detects it
[1, 2, 3] is IA                     // → true
```

So softness and enforcement are complementary halves, not a tension: errors flow freely
through the interior, and are detected exactly where the user asserts a clean type — an `is`
pattern, a `match` arm, an annotated (DEFERRED) binding, or the §10.7 firewall at a declared
interface. *Side-finding (grammar-checked 2026-07-29):* the `is` RHS is plain **expression space** — `is`
is a generic `binary_expr` table row with `operand = $._expr` (`grammar.js:76`, `:41-42`) —
which is why its type forms are restricted by design (bare identifiers, base types, `[T]`
literals; anything richer would collide with legal postfix parses: `x is int[3]` already
parses as `(x is int)[3]`). Two in-grammar precedents show type-space RHS is workable where
unambiguous: **`match` case patterns take the full `$._type_expr`** (`grammar.js:763`) —
`case int[]:` works *today* and detects embedded errors (verified:
`match(v){ case int[]: "clean" default: "has non-int" }` → `["clean", "has non-int"]`) — and
the query operator takes `$.primary_type` (`:489`). **Noted only — OUT OF SCOPE for this
proposal (user, 2026-07-29): supporting `x is (int[])` belongs to the pattern-grammar and
validator design.** Hand-off note for that effort: the parenthesized form is strictly additive
(`(int[])` is a syntax error everywhere today); mechanics would be a dedicated `is` rule with
RHS `choice($._expr, seq('(', $._type_expr, ')'))`, one GLR conflict at the paren boundary
(contents valid in both spaces, e.g. `(int)`) resolved by preferring the type parse — semantics
coincide, since the AST already resolves identifier exprs to types. Direct unparenthesized
`is int[]` should stay off the table (it would re-parse the currently-legal
`(x is int)[…]` postfix form). Until then the spellings are: a named type, or a `match` arm.
Same bucket, found 2026-07-30: the **`!` exclusion operator is broken for general types** —
`type NE = any ! error` fails at pattern compilation (`compile_pattern_to_regex: unknown node
type`, `invalid perl operator: (?!`): the exclusion is routed through the string-pattern regex
compiler. Consequence: the non-error top `any \ error` (TE-5, `Lambda_Formal_Semantics.md`
§7.3) currently has **no working surface spelling** — another item for the pattern-grammar
and validator design (aligned with TE-5's "until the validator gains a distinct surface
alias").

**`or`-rescue — RESOLVED 2026-07-29: no rule-bend needed; error-consuming `or` is already both
the spec and the implementation.** Specified three times over — truthiness (errors are falsy,
`Lambda_Formal_Semantics.md` §3), §7.3 ("both are falsy, so `f(x) or default` rescues both
uniformly"), and `Lambda_Error_Handling.md`'s falsy-errors section (`divide(10, x) or 0`) —
and verified live 2026-07-29: `int("abc") or 0` → `0`, `error("boom") or 5` → `5`,
`f(0) or 5` → `5` (user-fn div-by-zero — *historical: pre-C14c; `div 0` now yields `inf`,
which is truthy, so the `or`-rescue no longer applies to division*), `if (int("abc"))` →
falsy. The coherence argument:
`or` *is* truthy-select, and errors are falsy, so consumption follows from the definitions —
error-propagating `or` would require either error-truthy (absurd: `if` would enter) or a
special-cased `or` that breaks the identity. Two safety properties observed: **`0` is truthy**
in Lambda, so `int(s) or 0` has no JS-style zero-swallow footgun; and errors log at
origination (`runtime error [318]: boom`), so an `or`-consumed diagnostic leaves a `log.txt`
breadcrumb even though the program never sees it. `a() or 0` accordingly counts as E228
engagement (textually explicit consumption handling both branches). Pitfall documented
2026-07-29 in `Lambda_Error_Handling.md` §"Error Truthiness": bare `error` in expression
position is the **type** (truthy, not an error value) — `error or 0` → the type,
`error is error` → `false`, `error is type` → `true`; the `if`-condition lint catches the
`if (error)` form but not the `or`-operand form (lint-extension candidate).

**The `or`-typing rule (required by P0).** For the idiom to survive strict declarations, the
static type of `a or b` must narrow the falsy poison/absence members out of the left side:
`type(a or b) = (type(a) − {error, null}) | type(b)` — plain union arithmetic, no flow
analysis. Then `let n: int = int(s) or 0` types as `int` and passes the P0 checker; without
this rule the new strict declarations would reject the spec-blessed idiom.

**Operators are value-directed, not declaration-directed.** All the existing forms operate on
the error-ness of the value, so they work identically over both error forms:

| Form | Semantics (unchanged) | On `T \| error` (incl. inferred) |
|---|---|---|
| `let x^err = e` | on error: `x = null`, `err` = the error; else `err = null` | identical — the discharge point |
| `e^` (postfix) | unwrap success or return the error from the enclosing function | identical — makes the enclosing fn open (or rides its declared channel) |
| `^e` / `e is error` | boolean test | identical |
| `e or default` | errors are **falsy** → default idiom | identical — the batch one-liner: `a(xs[i]) or 0` |
| `raise` | requires a *declared* `T^` | **invalid** — a `T \| error` fn *returns* `error(...)` as a value; only `^` licenses `raise` |

Emission differs invisibly by callee kind — can_raise dual-lane reads the side channel, boxed
open results tag-test the Item — but the surface must never reveal which.

**Destructuring across mixed channels (decided 2026-07-29, user).** `^err` is **total over
error-ness and channel-agnostic**: it splits the outcome by whether the *value* is an error,
regardless of which channel delivered it — the enforcing `^E` channel, an error type inside
the value union, or an error hiding inside `any`. The two boundary cases that pin the rule:

```lambda
fn a() T | e1 ^ e2      // legal syntax (nobody would write it, but allowed):
                        //   union error e1 AND channel error e2
let b^err = a()         // b : T          — every error constituent stripped
                        // err : e1 | e2  — errors from BOTH channels land in err

fn c() any ^ e2
let b^err = c()         // b : any \ error, guaranteed non-error
                        // err : error | e2 — errors arriving via the any value or the channel
```

The general typing rule: `type(b)` = the success constituents of the source type (`T` in the
first case; `any \ error` in the second); `type(err)` = the source's error constituents ∪ the
channel's error type (∪ `null` on success). Note `any \ error` is exactly what the validator's
`any` already means — it matches everything except ERROR (§5.3) — so the checker's refinement
and validation positions agree. At runtime the split is one channel-agnostic test: `is error`
on the outcome (dual-lane callees: side-lane check plus value-tag test; boxed callees: tag
test alone). E228 applies whenever the signature carries any `^` channel, and the destructure
engages *everything* at once. **Postfix `^` behaves identically on the same mixed forms —
CONFIRMED 2026-07-29 (user):** it unwraps to the same stripped success type (`T`, or
`any \ error`) and propagates the same combined error set (`e1 | e2`), which rides the
enclosing function's error channel per the unified-channel rule. In binding position the
stripping is manifest in the type — `b` is error-free in both forms:

```lambda
let b^err = a()   // b : T (stripped); err : e1 | e2
let b = a()^      // b : T (stripped) — same type; errors propagated instead of captured
```

Postfix `^` is thus a *type-narrowing* operator in expression position: downstream of either
form, `b` is statically clean and eligible for native lanes — the two forms differ only in
where the error goes (captured locally vs propagated to the caller).

**Obligations attach to the form.** Must-handle (E228) applies to `T^`/`T^E` — the enforcing
spelling — and to pn/can_raise sys funcs; `T | error` never triggers it (the no-spill
decision). This is the simplest possible keying: no provenance metadata needed, the type
spelling *is* the obligation. Doc consequences for `Lambda_Error_Handling.md`: the `T` row of
the return-type table ("always succeeds — no error possible") holds for clean functions; an
open *expression or binding* is effectively `T | error`; every fn return — declared plain-`T`
or the implicit `any \ error` contract of an unannotated fn (2026-07-30 restatement) — is a
firewall, so an error-possible body is a compile error per §10.7; and "raise is the **only** way to
return an error from a function" becomes `^`-channel-specific — the `| error` form returns
`error(...)` values directly.

**Explicit vs implicit error-ness (decided 2026-07-29).** Type inference tracks *where* the
error-possibility came from, and enforcement keys on that provenance:

```lambda
fn f(a: T^) { let b: T = a }   // compile error — explicit T^ must be discharged first
fn g(a)     { let b: T = a }   // DEFERRED: success binds b:T; failure exits before b exists
```

An *explicitly declared* error possibility — either spelling, `T^` or `T | error` — must be
visibly engaged before the value can enter a plain-`T` position: `let b: T = a` is a compile
error for both. **When the user is explicit, we check explicitly.** Only *implicitly*-open
values (inferred openness, `any`) cross plain-`T` boundaries as DEFERRED checks. The complete
binding rule for `x = a()` where `a` is declared `int | error` (decided 2026-07-29):

```lambda
let x: int = a()            // compile error — explicit claim contradicts a's declared type
let x: int | error = a()    // legal — x carries the outcome, value or error
let x: int^ = a()           // same as the line above — equivalent in value positions
let x = a()                 // silent — x infers int | error, takes whatever a() returns
```

This is the same provenance split as the vacuous-`^` rule below — explicit facts are enforced
strictly, inferred facts flow. Consequence worth naming: `| error`-declared sys funcs (§7.3's
`int(s)` family) reject plain-typed bindings too — `let n: int = int(s)` errors; the idioms
are `int(s) or 0`, `^err` destructuring, or the union binding.

**Propagation into a declared channel — DECIDED 2026-07-29 (user).** System and user-declared
errors ride the **same** channel: every `R^E` is operationally `R^(E | error)`. `E` constrains
*user-raised* errors; defects flow implicitly. Caller consequence, to be documented: after
`let a^err = g()`, `err` may hold a *system* error (e.g. a type defect), not only the declared
`E` — code matching on `err.code` or shape must not assume `E`.

**Vacuous discharge — DECIDED 2026-07-29 (user): split by the provenance of the cleanness.**
`e^` where `e` is provably non-error differentiates two cases: (1) the operand's
error-possibility was in play and its cleanness is an *inference* result (an open-capable
callee currently proven clean) → **warn only, or even silent** — a defensive `^` survives
distant clean↔open flips; (2) the non-error nature is *explicit* (declared plain-`T`, no open
capability in play) → **static error**, as today ("'add' does not return errors"). *(Reading
of case 2 confirmed 2026-07-29.)*

**Interaction with TE-11:** after a failed `let x^err = e`, `x` holds `null` (current spec), so
the value binding is effectively `T?` until `err` is checked. Flow narrowing stays KIV; the
discipline is documented, not enforced.

**Short-circuit refinement (for TE-8/TE-9):** an error value short-circuits a checked boundary
only where the target type does not admit it. Targets `T^`, `T | error`, `any`, and `error`
receive the error *as a value* — an error-typed parameter is the explicit opt-in to
error-transparency (spelled `T | error` for non-enforcing acceptance).

**Implementation notes.** (1) The live propagation operator is postfix `^` (verified
2026-07-29: `input(...)^` works; `input(...)?` parses as the *query* operator and does not
propagate). The two E228 diagnostic texts advertising `d(...)?` are **fixed** (2026-07-29,
`build_ast.cpp:5199`, `:9017` → `d(...)^`); the error-handling guide is updated with TE-13's
channel-agnostic discharge rules. (2) The batch
idiom for per-item failures is simply a **plain array** holding successes and errors (§10.6's
type-level framing); typed `int[]` stays clean-only, and no new `(int^)[]` spelling is needed.

### TE-14 — Boxed/unboxed entry strategy is a later implementation decision

Enforcement correctness requires a safe path for every dynamic call; it does not require this
proposal to mandate a particular specialization topology. The simplest semantic anchor is a
boxed checked entry. A later implementation/performance phase may add an unboxed entry whose
parameters and returns use native carriers and whose body relies on TE-3's local proofs:

- only STATIC-PROVEN callers may enter an unchecked native version;
- dynamic callers use a checked path, likely with argument checks consolidated in a boxed
  prologue;
- the same semantic function result is observed regardless of entry/carrier choice;
- declared typed map slots likewise use unboxed physical storage only after the declared field
  check has succeeded.

Whether every function receives both versions, whether usage creates additional
specializations, and how open returns use boxed or side-channel ABIs are deferred. They are
performance decisions, not part of the language contract.

### TE-15 — Soft-error containment: skip to the closest safe boundary (decided 2026-08-01, user)

**Decision.** A soft error does not *propagate* through the expressions around it — it **skips**:
at the moment of origination, control transfers to the end of the closest statically-enclosing
*safe boundary*, and that boundary's position receives the error value. Everything between the
origination site and the boundary is never evaluated. This is the containment policy for the
whole family of soft/evaluation errors — failed deferred type checks, failed conversions and
casts, validation failures, and error values produced by `| error` computations — not a rule
for any one numeric case.

#### The problem it closes

§10.8 (revised 2026-07-30) already decided that a failed annotated binding "yields the boundary
error *before the binding exists* — `x` never holds an error." That promises control leaves the
straight-line path on failure but names no landing site, and the code after the binding was
compiled assuming `x: T`, so execution cannot simply continue. The gap is measurable today
(repro `temp/overflow_fn_test3.ls`, 2026-08-01): with `fn f(a: int) int { a + a*a }` and an
argument whose body result leaves the flex-int band, the *same call* yields a bare `ITEM_ERROR`
under boxed consumption (`let d = f(x)`, `f(x) or -1`) but a silently resurrected `float`
under native-arithmetic consumption (`f(x) + 1`). Mechanism: the native entry flattens its
boxed int-or-float result through `it2i` (truncated raw i64, no boundary check — the
`transpile-mir.cpp:12034` checked return never fires because the body's AST type is `INT`), and
the outcome then depends on whether the *caller* re-boxes (53-bit guard → bare error singleton)
or keeps computing in the native lane (next flex-int op promotes the out-of-band i64 back to
float). Same call, two results by consumption context — the fourth outcome §1 forbids, twice.

#### Options considered (2026-08-01)

1. **Status quo** — rejected: incoherent per the evidence above; the "error" produced is also
   the bare singleton, violating the rich-payload decision (§10 Q4).
2. **Require the user to spell `T | error`** — rejected: viral. If overflow (or any deferred
   check) counted as inferred error-possibility, every arithmetic-bearing plain-`T` fn would
   fail the §10.7 firewall and `| error` would spread through every signature — the `Result`
   pandemic, and not user-friendly.
3. **Redefine flex `int` as the float-representable integers** — **DECIDED 2026-08-01 as C16**
   (`Lambda_Semantics_Formal2.md` C16; spec §4.1/§4.2/§4.6/§4.7 updated), in the *tagged*
   variant: `int` remains a distinct runtime type (never a hint erased into float); arithmetic
   incl. `div`/`%` is closed and total via `int.inf`/`int.nan`; the overflow-promotes-to-float
   rule is deleted; unsuffixed literals stay restricted to ±(2⁵³−1). Consequence for TE-15:
   int *arithmetic* no longer originates defects at all — the mechanism below still governs
   casts, parsing, validation, and fractional→int admission.
4. **Handle it for the user — automatic containment.** Two granularities:
   - *Skip to end of function* — rejected. Every annotated `let` becomes an invisible early
     return (`T^` behavior without the spelling), and one bad item inside a `for` kills the
     whole batch because the jump target is past the loop.
   - *Skip to end of the closest block/element body* — **adopted**, generalized into the
     three-zone rule below. The containment region is not a policy choice but derived: it
     equals the variable's scope, which is exactly the static extent of the violated
     assumption. Code later in the block assumed `a: T`; code outside the block cannot name
     `a` at all, so scope itself proves it needs no protection.

#### The rule: three zones, one mechanism

Every expression position has a statically-known **error destination** — the nearest enclosing
construct that can *accept* an error value. Origination skips straight to it:

- **Zone 1 — binding boundary.** `let a: T = e` where the deferred check fails (or an error
  value arrives): the annotated binding is *not* an acceptor; the error skips to the end of the
  smallest enclosing block, and **the block evaluates to the error**. Subsequent statements in
  the block never run; outer blocks are safe by scoping.
- **Zone 2 — composition points.** List/map/element child positions *are* acceptors: the error
  lands as an ordinary data item and construction continues (§10 Q6's batch idiom, unchanged).
  `[1, int("x"), 3]` still composes to `[1, error, 3]` — the element position is the nearest
  acceptor, so the skip never reaches the block.
- **Zone 3 — expression interior.** Operators and calls between origination and destination
  have no acceptor positions of their own and are simply **skipped, never evaluated**. The
  error does not travel through them as a value.

The acceptor set is the closed list already enumerated by TE-8's short-circuit targets plus the
engagement forms: container element positions; `or`-left operands (skip lands at the rescue —
the right operand evaluates); `^err` destructuring and postfix `^` (capture/propagate);
`match`/`is` scrutinee positions with error classification; positions typed `any`, `error`,
`T | error`, `T^`; and unannotated bindings (which infer the union and hold the value, boxed).
The smallest enclosing block is the destination of last resort. The fn body is the outermost
block, so an uncontained error becomes the function's result — for *disclosed* soft errors this
is exactly §10.7 (an open body must disclose `| error` or contain); for **defect-channel**
errors (failed deferred boundary checks) it crosses a plain-`T` return on the unenumerated
system channel per `Lambda_Formal_Semantics.md` §7.3 — inference must **never** widen a
signature because of defect possibility, or option 2's pandemic returns through the back door.

#### Semantic consequences

- **Origination is a sequence point.** Evaluation of the containing expression stops
  immediately; operands and effects to its right never run (Rust/Swift argument behavior).
  Vacuous inside `fn` (no effects to observe); meaningful inside `pn`. Corollary: strict
  left-to-right evaluation order becomes *normative*, not incidental.
- **Rescue moves to the initializer.** Since failure exits the block, `a or 0` *after* the
  binding is unreachable by construction; the idioms are `let a: T = e or 0` and
  `let a^err = e` (engagement suppresses the skip and captures instead).
- **Batch friendliness is a theorem, not a hope.** A `for` body is a block, so a bad item
  yields one error item in the collected result and the loop continues:
  `for (x in xs) { let n: int = int(x.count); n * 2 }` → error elements for bad items, results
  for good ones.
- **Elements.** Child-expression errors remain child items (zone 2); only a failed *binding*
  declared inside the body collapses the element body to the error.
- **Payload.** The skip delivers the §10 Q4 rich error (code, message, expected/actual,
  boundary location) — the bare `ITEM_ERROR` singleton observed at today's re-box guard does
  not satisfy the contract. Exception: a defect rescued *by an `or` on the same edge* may skip
  materializing the error object (log-line breadcrumb only) as an optimization.

#### Implementation model: errors are control, not values

**Invariant.** An error exists only (a) boxed in an Item-typed lane or slot, or (b) transiently
on a resolved control edge from origination site to destination. Native/unboxed lanes are
error-free *by construction*; no operator in unboxed code ever receives an error operand — the
emitter, not the runtime, guarantees this.

Value-propagation through unboxed lanes was considered and rejected: it requires an in-band
sentinel (today's accidental out-of-band i64 *is* one, and its consumer-dependent meaning is
the measured divergence), or re-boxing every lane an error might cross (re-creates the flexint
ANY-poisoning this design exists to remove), or a polled side-flag (cost on every operation).

- **Destination stack.** The emitter maintains the acceptor context lexically while lowering
  (same shape as the online-exception emission-time tracker). Destinations are static; there is
  no dynamic unwinding — skips are intra-function branches, and fn boundaries use the ordinary
  return path.
- **Origination sites are a closed set**, each already computing its failure condition:
  `lambda_type_check` boundaries, fallible conversions/sys-funcs, and calls to open/`^`
  callees. (The flex-int promote edge was a fourth class; C16 deleted it — int arithmetic
  never leaves int.) Routing retargets existing branches — happy-path cost is zero. `any`-lane
  data cannot smuggle errors into native lanes because every `any`→native transition is already
  a checked boundary (TE-5 R3).
- **Cross-function ABI.** Interior routing reaches the fn's outermost region → error return;
  the *call site* is an origination site in the caller. Boxed-returning calls carry the error
  in the result Item; native-returning calls check the context error lane
  (`FN_ERROR_LANE_CONTEXT_ITEM`) — one load-and-branch after the call, the Swift-`throws`
  shape. A callee whose compiled body provably contains **zero** origination sites needs no
  caller-side branch; carry this as the emission-time effect bit
  (`FnEffectSummary.may_return_error`) — transitive in the implementation, invisible in types.
  **Polarity fix required:** today's `closed_item_result` gate (`transpile-mir.cpp:11355`)
  treats a *missing* variant analysis as "trusted clean, skip the error branch" — that is one
  half of the measured divergence. Unknown must mean defect-capable; branch-free only on proof.
- **Boxed lanes keep value contagion** (the helpers' existing "error in, error out") as an
  equivalent implementation wherever it is unobservable — i.e. pure `fn` code. `pn` bodies emit
  control-routing on both boxed and unboxed paths so the two agree on which effects ran.
  **Equivalence invariant (test target, DF9-style): contagion ≡ routing wherever observable.**
- **Landing pads.** One per region that contains origination sites: receive/box the error, root
  it, restore the side-number-stack watermark (`lambda_restore_number_frame_top` — the existing
  single-funnel machinery generalized per block).

#### Open sub-questions

- **Statement-position defects in `pn` (the hardest).** A failed reassignment whose block value
  is discarded — `acc = acc + x` inside a loop — must not evaporate with `acc` silently stale.
  Recommendation: escalate (raise channel / abort the loop); **undecided**.
- **Flex-int overflow policy — RESOLVED 2026-08-01 by C16** (option 3 above, tagged variant):
  int arithmetic is total, so it no longer produces defects; TE-15's zones stand unchanged
  for casts, parsing, validation, and fractional→int.
- **Skip is containment, not acknowledgement — RESOLVED by TE-16.** A raised (`T^E`) error
  is compile-time gated at the immediate expression and therefore never reaches the skip
  machinery; TE-15 governs defects and boundary-rejected soft errors only.

### TE-16 — The `^ { }` error handler; `let a^err` and `if (^err)` retired (decided 2026-08-01, revised 2026-08-17, user)

**Decision.** Local error handling uses one braced syntax with two
context-selected forms:

```lambda
let a = e ^ { … ^ … }      // expression form: ^ is the handled error
pn_call() ^ {               // statement form: handle, then continue
    … ^ …
}
```

The retirement of `let a^err = e` and the old prefix error test `if (^err)` is now landed: the
grammar, AST/runtime, and active `.ls` corpus no longer accept or use those forms. The formal
authority for this retirement is **S7.6.5v2**, alongside **S7.6.2v3/S7.6.3v2** for postfix
handler/propagation placement.
A bare `^`, `^.field`, or `^[index]` is now a current-error reference only inside an active
handler body. A handler always has an operand on its left; the prefix `^ { … } e` shorthand does
not exist. This keeps the error channel consistently spelled with `^` while leaving `~` available
for its existing current-value contexts. The formal authority is **S7.6.2v3/S7.6.3v2**.

#### Why the destructure had to go

`let a^err = e` is Go's `(v, err)` shape: a *product*, so both bindings live in one scope and
nothing prevents reading `a` when `err` is set. Its documented and implemented behavior is
that `a` is `null` on failure (verified 2026-08-01: failure → `[null, type.null, "boom"]`,
success → `[42, int, null]`), while its *static* type is `T` with error constituents stripped.
But TE-11 says plain `T` excludes null — so `a : T` was a claim the runtime did not honor,
the same defect class as TS-9's "static type said int, value is float" that C16 eliminated.
The doc already conceded it ("effectively `T?` until `err` is checked. Flow narrowing stays
KIV; the discipline is documented, not enforced"). Measured consequence: with `a` null,
`a + 1` yields `null` — an unchecked use produces a silently null-contaminated result rather
than failing loudly. Probing the representation-risk case (`string`-typed binding into
`len`) showed no crash — `a` stays boxed — so this was a typing hole, not a memory-safety
one; but the boxing is itself the cost, since a `T` that may hold null can never take the
native lane TE-3 wants to give it.

*Rejected repair:* flow-narrowing `a : T?` → `T` in the `else` branch of `if (^err)`. It
preserves the ergonomics but buys soundness with a dataflow analysis that must not have
holes. TE-16 is sound **by construction** — the failure path provably does not reach the
binding — which is the same reasoning that chose TE-15's block scoping over function scoping.

#### Why the plain skip is not sufficient either

TE-15's skip is *safe* but *blind*: `{ let a: T = e; … }` cannot tell whether an error
occurred and cannot access it. Skip is a fail-stop mechanism; `^ { }` is the handling
mechanism. Together they are the intended pair — **skip is the default (fail-safe, the error
becomes the block's value), and `^ { }` handles an error outcome produced while evaluating its
own operand and puts that error in hand.** It does not reach outward and absorb a later boundary
check imposed by the surrounding declaration.

#### Prior art (the shape being adopted)

Rust never lets a failed outcome and its success binding coexist in one scope: `Result<T, E>`
is a *sum*, and either the binding is scoped to a proven-success branch (`match`, `if let`)
or the failure path is required to leave the scope entirely (`let … else { }` whose block
must diverge; `?`). Lambda already had two of the three: `match` arms genuinely narrow the
scrutinee (verified 2026-08-01 — `case error:` binds `~` as the error, `case int:` binds it
as an int, so no scope holds "an int that might be null"), and postfix `^` is `?`. TE-16
supplies the third: `e ^ { }` is `let … else { }` in expression form; the statement-position
form is the ordinary command counterpart, where the handler body runs only on failure and control
then resumes after the handled statement.

#### The two context forms

- **Expression form.** `e ^ { h }` is evaluated as one ordinary expression, with
  `type(e ^ h) = (type(e) \ error) | type(h)`. It has no privileged relationship with a
  surrounding declaration. In `let a: T = e ^ { h }`, the handler expression finishes first;
  its result then crosses the normal `T` assignment boundary. A mismatch there behaves exactly
  like `let a: T = other_expr`: static rejection where provable, otherwise a deferred check and
  TE-18 skip. The outer check is **not** caught by the inner handler. This makes the form a
  shorthand for value-level error matching, not a contextual checked-cast construct.
- **Statement (`stam`) form.** `pn_call() ^ { H }` protects a procedural call whose value is
  discarded. Success continues with the next statement. On error, `H` runs as a statement body
  with `^` bound to the handled error; if `H` completes normally, execution likewise continues after the handled
  statement. No handler-result type is required. `return` or `raise` in `H` still acts on the
  enclosing procedure.
- **No implicit divergence rule.** In expression position the handler may return any value; its
  union participates in ordinary contextual typing. In statement position it may complete
  normally. Letting some enclosing block skip is a runtime outcome, never a static claim that the
  handler diverges.
- **`^` binds the handler error.** Bare `^`, `^.field`, and `^[index]` are valid only inside an
  active handler body and resolve to the innermost handler's error. The handler introduces no
  `~` binding: `~` remains the current item/index or object model supplied by its enclosing
  pipe, match, constraint, or view context. Nested handling is written
  `operand ^ { ... }`; a leading `^ { ... }` is not a handler.
- **Channel-agnostic**, per TE-13: `^ { }` discharges both raised (`T^E`) and soft
  (`T | error`) errors.

#### Extended two-arm handler syntax — six options and decision (decided 2026-08-17, user)

The one-arm handler is ideal when success should pass through unchanged, but explicit
error-versus-value branching is common enough to warrant a compact form. The six considered
surfaces were:

| Option | Surface | Assessment |
|---|---|---|
| 1 | `e ^ { error_arm } { value_arm }` | Rejected: two adjacent anonymous blocks are too cryptic; the second block has no visible role marker. |
| 2 | `e ^ { error_arm } else { value_arm }` | Rejected: `else` creates dangling/ownership ambiguity beside `if … else`, especially when the handler is the `if` body expression. |
| 3 | `e ^ { error_arm } default { value_arm }` | Viable runner-up: explicit and aligned with `match`, but `default` describes selection indirectly rather than naming the normal-value context. |
| 4 | `e ^ { value_arm } error { error_arm }` | Rejected: the first block reverses the established meaning of `^ { … }`; its interpretation would depend on a later suffix. |
| 5 | `e ^ { value_arm } catch { error_arm }` | Rejected: it has the same reversal and imports `try`/exception expectations into a channel-agnostic soft-or-raised error handler. |
| 6 | `e ^ { error_arm } ~ { value_arm }` | **Decided:** preserves the established error arm, marks the non-error arm with Lambda's current-value symbol, and needs neither a new keyword nor `else` ownership rules. |

**Decision: adopt option 6, `e ^ { error_arm } ~ { value_arm }`.** The operand is evaluated
exactly once. A soft or raised error selects the first arm and binds it to handler-local `^`;
every non-error result, including `null` and `false`, selects the second arm and binds it to
handler-local `~`. In the error arm, an enclosing `~` keeps its existing meaning. In the value
arm, the new `~` is innermost and shadows an enclosing current-value context. Errors raised by
either selected arm are fresh outcomes and are not swallowed by the same handler.

```lambda
pn_func(inner, options) ^ {
    // error arm: ^ is the raised pn error
    diagnostic(^)
} ~ {
    // value arm: ~ is the pn result
    transform(~)
}
```

This statement-position shape is expected to be the common `pn`-call flow: handle a directly
raised error in the first arm, or continue with the returned value in the second. Omitting the
`~` arm preserves the one-arm rule: success passes through unchanged. In expression position the
result type is `type(error_arm) | type(value_arm)`; in statement position the selected arm
executes and normal completion continues after the handler. The complete two-arm construct
remains primary-like at the member-access/postfix tier, so, for example,
`e ^ { h } ~ { v }.field` means `(e ^ { h } ~ { v }).field`.

`match` remains the long-term canonical and extensible branching form; the two-arm handler is
compact syntax for the frequent binary outcome split:

```lambda
match e {
    case error { /* error branch; handler form exposes this value as ^ */ }
    default { /* non-error branch; handler form exposes this value as ~ */ }
}
```

The semantic authority is **S7.6.1v3/S7.6.2v3** (with acknowledgment under **S7.5.1**).
The grammar, AST, MIR lowering, nested handler contexts, and expression/direct-`pn` regression
coverage landed on 2026-08-17.

#### Syntax: brace-delimited, and why

The handler **must** be braced. `^` followed by `{` is the handler; `^` followed by anything
else (or nothing) is postfix propagate. This is the lexical discriminator for both the
expression and statement forms; the surrounding AST context selects which form applies.

Per **S7.6.2v3/S7.6.3v2**, both forms are left-associative postfix-primary operations at the
same logical precedence tier as member (`.`) and query (`?`) access. Their operand is a primary
expression; parentheses explicitly widen it. The handler result is itself primary-like, so
postfix operations continue left-to-right. The mandatory caret is owned by the handler or
propagation construct, never by `call_expr`:

| Source | Required parse |
|---|---|
| `a + b ^ { h }` | `a + (b ^ { h })` |
| `(a + b) ^ { h }` | handle the complete addition |
| `a |> f() ^ { h }` | `a |> (f() ^ { h })` |
| `(a |> f()) ^ { h }` | handle the complete pipe expression |
| `e ^ { h }.field` | `(e ^ { h }).field` |
| `e.field ^ { h }` | `(e.field) ^ { h }` |
| `e ^ { h1 } ^ { h2 }` | `(e ^ { h1 }) ^ { h2 }` |
| `f()^ - 1` | `(f()^) - 1` |

The implementation must therefore use one postfix handler production over `primary_expr`, make
the handler result available to the ordinary postfix chain, and remove the call/literal/binary/
member and prefix-handler special cases. Postfix propagation follows the same ownership and
precedence model. This supersedes TE-16's earlier maximal-left binding and prefix shorthand.

The rejected alternative was the terser `e ^ expr` with "any expression after `^` is the
handler, EOL means propagate." Two measured problems: (1) `f(5)^ - 1` **parses today and
evaluates to 41** (propagate, then subtract; verified against the current build), and that
rule silently reinterprets it as rescue-with-`-1`, yielding 42 on the success path — a
semantic change to already-valid code with no diagnostic; (2) it requires newline sensitivity
in the expression grammar (the JavaScript-ASI hazard class), since otherwise
`let a = f()^` followed by a statement would swallow that statement as a handler. The brace
form needs neither. If a bare-expression form is ever wanted, the minimum safety measure is
to make `^` followed by a unary-capable token (`-`, `+`) a **compile error demanding
parentheses** rather than silently choosing the infix reading.

The verbosity cost is illusory because **`or` already owns the terse-default case**. The
division of labor:

| Form | Catches | Error accessible |
|---|---|---|
| `e or default` | falsy: error, null, false, `""` | no |
| `e ^ { … ^ … }` | error only | yes, as `^` |
| `e^` | error only | no — propagates |

Note this corrects a tempting mis-framing: `or` and `^` are **not** a soft/hard split. Both
work on both channels (TE-13's unified discharge; verified: `error("boom") or 5` → `5`). The
real distinction is coalescing-without-access versus error-specific-with-access — and since
`or` also swallows null and `""`, `^ { }` is the more precise instrument even for soft errors.

#### Retiring `if (^err)`

The migration spelling **`err is error`** is now the only error-test spelling (verified in the
2026-08-17 corpus migration). The handler-local current-error atom adds a distinct scoped use of
`^`: postfix propagation, braced handler delimitation, the type-level channel, and the
handler-local current error remain separate contexts. Outside a handler body, a bare `^` is not a
value expression.

#### Acknowledgement taxonomy for the enforcing channel

A raised `T^E` must be acknowledged at the immediate expression (TE-13 tightness) by exactly
one of:

1. `match` with a `case error:` arm;
2. `e ^ { … }` — handle here, optionally with `~ { … }` as the explicit non-error arm;
3. `e^` — propagate;
4. a receiving position that textually admits error — `let x: T^ = e`, `let x: T | error = e`,
   or a declared parameter/return of that shape.

`let x = e` alone never acknowledges (stricter than Zig/Rust/Go, per TE-13), and — the ruling
this section adds — **TE-15's skip does not acknowledge either.** Skip is automatic
containment for *defects*; it is not user engagement, so it cannot discharge an obligation
the user is required to make visible. Consequently a raised error is compile-time gated and
never reaches the skip machinery.

**The three regimes, stated once** (they are routinely conflated):

| Class | Mechanism | Compile-time obligation |
|---|---|---|
| soft error values (`T \| error`) | flow as data — the batch idiom | none |
| defects (failed boundary checks) | TE-15 skip to closest safe boundary | none (automatic) |
| raised errors (`T^E`) | must be acknowledged at the immediate expression | E228 |

#### Handler-protected `await` — rejected for now (decided 2026-08-06, user)

The handler must eventually cover the **system-fault** channel, not only value-error handling.
The retired destructure used `transpile_local_fault_expression` with a real
`LambdaRecoveryFrame` and inline `sigsetjmp`; that implementation path and its
`AstNamedNode.local_fault_safe` state are gone. The remaining design constraint is unchanged:
the jump buffer records a machine context inside the *currently executing* JIT activation, and
an `await` unwinds the native stack back to the scheduler, resuming later on a different frame.
A buffer captured before the suspension points at stack that no longer exists. The new handler
lowering must use `MayAwaitScan` to reject such operands rather than silently degrading capture.

**Decision: `e ^ { … }` over a possibly-suspending operand is a compile error.** Keep it simple
first. This is the existing restriction made *loud* rather than silent, so nothing regresses, and
it reuses the `MayAwaitScan` already written.

**Rejected: silently splitting the capability** (always catch value errors, catch faults only
when the operand cannot suspend). The same syntax would catch OOM in one place and not another,
decided by whether the operand happens to contain an `await` — the "same construct, two
behaviours by invisible context" pattern this design exists to eliminate. Worse for `^ { }` than
for the old destructure, because the handler is an expression that composes freely and
`(await f()) ^ { … }` is the canonical async shape.

**Follow-on, not now: segment the protected region per poll.** The frame never actually needs to
span a suspension — split the region at each `await` into straight-line segments and arm one
frame per segment. The handler label is stable (same function), so every segment lands in the
same place, and no jump buffer ever crosses a poll. This is cheaper than "represent fault
recovery across task polls" because it *avoids* crossing polls rather than supporting them; note
too that a fault inside the awaited task belongs to that task's own recovery frame, and a fault
after resume is on the new stack, which a re-armed segment frame covers. Going from compile
error to allowed is backward-compatible, so no code written under the rejection breaks later.

#### Migration

- Rewrite a value-producing recovery as `let a = e ^ { H }`. A legacy two-arm
  `let a^err = e; if (^err) { H } else { B }` becomes `e ^ { H } ~ { B }` when both branches
  are local expressions; use `match` for wider or multi-way branching. Rewrite a command-style
  capture as `pn_call() ^ { H }`; its handler may complete and execution continues afterward.
- Rewrite `if (^err)` as `if (err is error)`.
- Grammar: the retirement is landed. `handler_expr` and `propagate_expr` are postfix-primary
  forms over `primary_expr`; all prefix-handler and legacy destructure productions are removed;
  `current_error_expr` remains scoped to handler bodies. Run `make generate-grammar` after grammar
  changes (never hand-edit `parser.c`).
- E228 now advertises only postfix propagation, braced handling, and `or` recovery.
- **Lazy/streaming `for` bodies** — where containment materializes for deferred evaluation; KIV.
- **Engagement-set finalization** — the exact list of skip-suppressing forms, kept aligned with
  the E228 acknowledgment forms as those settle.

### TE-17 — Container acceptance is type-sensitive: native lanes gate on provable infallibility (decided 2026-08-06, user)

**Decision.** A container element position is an error *acceptor* only when its element
contract admits error. A **native-lane** element position is not an acceptor and never becomes
one at runtime; instead, a value that can only be inferred as `T | error` **cannot enter the
native lane at all** and is carried boxed until the error is discharged. Native-lane entry is
gated on *static* proof of error-freedom, so §2's native-lane invariant is preserved by
construction and no error-routing machinery is ever emitted inside a native lane. The cost is
that fewer values qualify for the fast lane.

#### The problem it closes

`Lambda_Impl_Error_Handling.md` C2 lists "container element positions (list/map/element
children)" as unconditional acceptors — the batch idiom. That is **unsatisfiable for typed
containers and contradicts §2 of the same plan.** `ArrayNum` element storage is a raw
`int64_t*` / `double*` / packed byte array ([`lambda.hpp:592`](../lambda/lambda.hpp#L592),
[`lambda.h:864`](../lambda/lambda.h#L864)); there is no Item word in an `int[]` slot to hold an
error. The same holds for every other native-lane destination, which C2's *positional* framing
misses entirely: declared map fields (packed struct storage), indexed stores `arr[i] = e`,
field stores, `push`/`splice`, and declared params and returns.

TE-13's implementation notes already settled the substance — *"the batch idiom for per-item
failures is simply a plain array holding successes and errors; typed `int[]` stays clean-only,
and no new `(int^)[]` spelling is needed."* TE-17 makes that rule general (it governs every
lane destination, not just array literals), states the mechanism, and records the rejected
alternatives so C2 cannot silently re-appear.

#### Options considered (2026-08-06)

1. **Error sentinel in the lane** — rejected. Not merely undesirable: **unavailable.** Under C16
   the int lane's out-of-domain encodings are spent on legitimate *values* — `INT_LANE_NAN`,
   `INT_LANE_INF`, `INT_LANE_NEG_INF` denote `int.nan`/`int.inf`
   ([`lambda.h:1396-1410`](../lambda/lambda.h#L1396)) — and `INT_LANE_NULL` /
   `FLOAT_LANE_NULL_BITS` / `SIZED_LANE_NULL` are valid only in *nullable* lanes
   ([`:1411-1428`](../lambda/lambda.h#L1411)). Dense sized lanes (`i8`…`u32`) have no spare bit
   pattern at all. Even with room, a sentinel records only *that* a failure happened, which
   contradicts TE-9's rich payloads, and it re-creates the in-band sentinel TE-15 rejected —
   whose consumer-dependent meaning was the measured O1 divergence. A lane that must be decoded
   for error-ness is a boxed Item wearing a native costume.
2. **Sidecar** (dense lane + sparse index→error table) — rejected. It solves the *write* and
   nothing else: on read-back, `a[i]` on a slot declared `int` yields an error, so the error
   enters the native lane anyway and every downstream consumer needs a check. §2 then collapses
   at every read instead of at one write. Dead end.
3. **Option A — route.** The lane store is an origination site; the defect skips outward to the
   next enclosing acceptor. Sound, but **abandons the whole container**: there is no
   partially-built `int[]`, so one bad item discards the batch. Retained only as the *fault*
   -regime behavior (below), not as the soft/raised-error policy.
4. **Option B — report.** Store a placeholder in the slot, append the failure to a construction
   error list, keep iterating. Preserves the batch idiom. Rejected on two counts. (a)
   **Mechanically impossible for the general case:** "store null" requires a spare encoding, and
   non-nullable native lanes have none (option 1's finding). (b) Even where a placeholder exists,
   the construction *succeeds-looking* — the array is produced and the program continues, so the
   failure is lost unless someone inspects the list. That is exactly the evaporating-defect
   failure mode TE-15 exists to prevent.
5. **Option C — conservative gating** — **DECIDED.** Do not make the lane carry errors and do
   not make lane code route them. Make the *type* carry the possibility, and let the existing
   static-inference obligation do the work.

#### The rule

Acceptance is read from the **destination contract**, never from the syntactic fact that an
expression sits inside a container. The predicate already exists and needs no new machinery:
[`lambda_type_accepts_error()`](../lambda/runtime/type_contract.hpp#L60) decides admission, and
[`lambda_type_lane_storage_desc()`](../lambda/runtime/type_contract.hpp#L63) decides
representation (it already "returns false for abstract/heterogeneous contracts that must remain
boxed"). Three outcomes, applied uniformly to every write destination — literal element,
indexed store, field store, `push`, param, return:

| Destination contract | Outcome |
|---|---|
| admits error (`any`, `error`, `T \| error`, `T^`, unannotated boxed slot) | **accept** — the error is the element; batch idiom, unchanged |
| native lane, source provably infallible | **enter the lane** — branch-free, §2 holds |
| native lane, source only inferable as `T \| error` | **cannot enter the lane** — carried boxed until discharged |

The third row is the whole of TE-17. `let x: int[] = e` where `e : int[] | error` does not
produce a checked lane store that might fail — `x` simply is not lane-eligible until the union
is narrowed, and the E228/§10.8 obligation the user already owes is what narrows it. Likewise a
fallible *element* expression is not typed into an `int` element slot and then rescued at
runtime; it fails to type, and the user discharges it locally:

```
[ f(x) ^ { 0 } for x in xs ]      // : int[], native lane preserved
[ f(x) for x in xs ]              // : (int | error)[], boxed, per-element errors retained
```

Both spellings are available, the choice is visible in the source, and neither requires the
lane to change shape. This is why C is preferred over A and B: the failure mode is a
**compile-time obligation the user fixes locally**, not a silent whole-batch abandonment (A) or
a silently successful-looking result (B).

#### Consequences

- **§2 is preserved verbatim, and TE-15's zone 2 is narrowed.** Container element positions are
  acceptors *for boxed element contracts only*. No skip target, landing pad, or error edge is
  ever emitted inside a native lane, and the impl plan's C2 must be restated as a contract
  predicate rather than a positional list.
- **The batch theorem becomes typed.** Per-element error retention is a capability of Item-lane
  containers. Typed containers are all-or-nothing by construction, which is the honest reading
  of "typed `int[]` stays clean-only."
- **Fault regime is unaffected and remains A-shaped.** System/resource faults are untyped and
  unwind through `LambdaRecoveryFrame`, abandoning any partially-built container. No batch-idiom
  loss, since faults are not resumable. This keeps the three regimes distinct: soft values flow
  as data, defects are gated by TE-17 before they can reach a lane, raised errors stay E228-gated.
- **Effect analysis becomes load-bearing for performance, not a peephole.** D4's ruling that
  *unknown ⇒ defect-capable* combines with TE-17 into a viral demotion: an unproven callee's
  result cannot feed a typed container, so every unanalyzed call can cost a native lane. The
  `may_defect` call-graph fixed point is therefore what buys the fast path back, and it must be
  built before, not after, the routing work. This is the main risk to watch.
- **`any`-sourced data needs an explicit discharge** to reach a typed container, since every
  `any`→native transition is already a deferred check (TE-5 R3). Ergonomically acceptable — the
  remedy is one `^ { }` — but it should be measured against the corpus before Phase C.
- **Diagnose the silent case.** An *annotated* `int[]` that cannot be satisfied is a compile
  error, which is visible. An *unannotated* literal silently infers `(int | error)[]` and boxes,
  losing SIMD and the typed-array path with no source-level signal. Emit a note when a container
  is boxed solely because of unproven fallibility.

#### Promoted to the formal design (2026-08-07)

TE-17's lane purity is now a normative design invariant: **DI20 — Native lanes are
error-free**, anchored in **§D2.8** of [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md)
(v1.5.0). D2.8.1 carries the representation argument and the two rejected escape hatches
(in-band sentinel, sidecar); D2.8.2 carries I2 plus the fault-regime exclusion; D2.8.3 carries
this decision and TE-18's dominance, and states that the `may_defect` split (D6.1.3) is a
*prerequisite* for lane routing. The semantics side was already written — S7.8.1 (acceptance)
and S7.7.2/S7.7.3 (dominance) — and is unchanged; D2.8 is the representational counterpart, the
error analogue of DI5 (null sentinels) and DI8 (ArrayNum null-freedom). Cite `DI20`/`D2.8.x`
rather than TE-17 for the lane rule from here on; TE-17 remains the record of the deliberation.

#### Open

- Whether the `T | error` demotion should be *transitive through containers* (does
  `(int | error)[]` disqualify the array from lane treatment forever, or does a discharging
  `match`/`^ { }` over the whole array re-narrow it to `int[]` in place, or only by copy?).
  Re-narrowing by copy is the safe default; in-place re-narrowing needs the COW exclusivity
  rules and should not be assumed.
- The interaction with lazy/streaming `for` bodies (already KIV in TE-15): with a typed-lane
  destination and a streaming source, representation cannot be chosen before consumption.
  Committing to boxed-until-proven is consistent with TE-17 and is the presumed answer.

### TE-18 — Skip is a declaration-boundary mechanism; the guard dominates the scope (decided 2026-08-06, user)

**Decision.** TE-15's skip applies at **declaration boundaries only** — `let` / `var` / `for`
loop variables, plus declared parameters and declared returns, plus the future `cast … as T`.
It does **not** apply inside expression composition. An expression whose operand can fail is
typed `T | error` and computed as such; the error travels as a value, guarded where the
representation requires it, and the user recovers with `e ^ { … }` where *they* choose.

**The invariant this buys.** A declaration with a native contract *guards once, on entry*, and
that guard **dominates every use of the binding in its scope**. Inside the scope the binding is
native-lane, unconditionally, with no per-use error check.

This restates rather than replaces TE-15: containment still exists, but its trigger set shrinks
to the boundaries that were already checking, so no new control mechanism enters expression
lowering. It is the original conservative design, made explicit.

#### Why expression interiors do not skip

Three reasons, in the user's ordering: (1) skipping complicates lowering — every operator
position becomes a potential control edge; (2) it makes a *soft* error behave like a hard one,
collapsing the distinction the three-regime taxonomy exists to maintain; (3) it performs a
recovery the user did not request, pre-empting the `^ { }` they might have written.

**And it costs nothing, because TE-17's I3 already does the work.** TE-15's motivating defect
was the O1 divergence: `let d = f(x)` yielding a bare `ITEM_ERROR` while `f(x) + 1` silently
resurrected a `float`, the same call giving two answers by consumption context. Under I3 that
is fixed without any interior skip — `f(x) : int | error` is **not lane-eligible**, so `f(x) + 1`
is computed boxed, where contagion yields the error, which is exactly what the binding yields.
Interior skip was machinery introduced to close a hole that the eligibility gate closes more
cheaply. Removing it is a simplification, not a weakening.

**Corollary retired.** TE-15's implementation note that *"strict left-to-right evaluation order
becomes normative"* was forced by interior skip (effects to the right of an origination had to
be provably unevaluated). With no interior skip, effects to the right of an error-valued operand
run normally, and evaluation order returns to being an ordinary design choice.

#### The boundary scenarios

| # | Form | Ruling |
|---|---|---|
| 1 | `let x: int[] = e` / `var x: int[] = e` / `for x: int[] in e` | Guard on assignment. Error ⇒ **skip to end of the enclosing block**, which yields the error; the binding is never established. Success ⇒ the binding is native-lane for its whole scope. |
| 2 | `fn f(x: int[])`, called as `f(e)` | Guard on the argument at the **call site**. Error ⇒ `f` is not entered and **the call expression evaluates to that error**. Success ⇒ `f`'s body runs native. Declared returns are the same shape, with the fn body as the region. |
| 3 | `e as T` (future; not yet in the language) | An inline declaration. Same as case 1: skip on error, continue native on success. |
| 4 | `[…]`, `{…}`, `<e …>` with no static type | Never enters a native lane. Accepts and contains error values — the batch idiom. |
| 5 | `a + e + b` — expression composition | **No skip.** The result type is `T \| error`; the error flows as a value. Guard only where representation demands it (I3). |
| 6 | `pn` bodies | One extra ruling only — case 7. |
| 7 | `x = e` — reassignment to a declared `var` | Its own ruling: skip to the **declaring block** of `x`, old value retained, **and diagnose** — see below. |
| 8 | `while (c) { … }` in a `pn` | Contributes **no region**. A skip inside it exits the loop, landing at the declaring block of the assigned binding. |
| 9 | inner callable assigns an outer-frame `var` | Failure stops at the mutating callable's boundary and returns an error. The call invalidates subsequent reads of the outer binding until the caller explicitly re-establishes it; `mutate(); use(x)` is rejected, while no later use and `x = mutate(); use(x)` are legal (S3). |

Cases 1–3 are the same mechanism (a declared boundary with a region behind it); 4 and 5 are the
same mechanism (values flowing through error-admitting positions); 7 is case 1's mechanism with
a diagnostic obligation case 1 does not have. Case 9 is the cross-frame termination rule: the
callee can return an error but cannot branch into another frame's declaring block.

#### Case 7 — reassignment is not case 1, and must be diagnosed

`var` is genuinely harder than `let`, and the difference is observability. When
`let x: T = e` fails, the skip happens **before the binding exists**: nothing was established,
the block yields the error, and no code the user wrote is silently abandoned in a surprising
state. When `x = e` fails mid-block, two things are true that have no analogue in case 1:

1. **`x` already exists and now holds a stale value** — the previous one, retained per §10.8.
2. **The rest of the block was written assuming the new value**, and is skipped.

So the skip here is not merely containment; it is the runtime **abandoning code the user wrote,
holding a value the user did not intend, on the user's behalf without being asked**. That is a
runtime error in substance even though it is handled. It must be reported, not merely logged as
an origination breadcrumb.

**Three tiers.**

- **Static, provable.** A reassignment whose RHS is provably `T | error` against a declared
  `var x: T` inherits the existing binding rule unchanged — the same **compile error** as
  `let x: int = a()` (decided 2026-07-29: "when the user is explicit, we check explicitly"). No
  new rule; confirm it applies to reassignment as well as declaration.
- **Static, deferred — new warning.** Where the RHS is only *deferred*-fallible (implicit
  openness, an `any` source, an unproven callee) **and the block has statements after the
  reassignment**, warn: this reassignment can fail, and if it does, the following N statements
  will not run and `x` will keep its previous value. Gate the warning on there actually being a
  tail to abandon — a reassignment in final position skips nothing and deserves no warning. This
  is the "parts of the following body might be violated" case, made precise.
- **Runtime — a report, not a breadcrumb.** When the skip fires, report at a severity above the
  ordinary origination log, naming both consequences: which binding kept its previous value, and
  how many statements of the block were not executed. Reporting only "an error originated here"
  hides exactly the part that distinguishes case 7 from case 1.

**The handler is not an assignment-boundary catch.** In `x = e ^ { … }`, the handler first
produces the complete RHS value; the ordinary `x` contract check happens afterward. Handling can
make the RHS statically clean and thereby remove the warning, but a later dynamic mismatch at the
assignment boundary still reports and skips exactly as it would without `^ { … }`.

#### Case 8 — `while`, and the rule that makes it a corollary

`while (c) { … }` is a `pn`-only *statement* (`while_stam` sits in `_statement` in
`grammar.js`, alongside `for_stam`). It declares nothing and **yields no value**. Two
independent arguments therefore say it cannot be a skip destination: it establishes no binding
whose scope could bound a region, and it has no result position in which an error could be
delivered. So a reassignment skip inside a `while` **exits the loop**, landing at the end of the
block where the assigned `var` was declared. The user's case-8 ruling is correct.

But it is not a special case. It falls out of the general rule, which case 8 is what forced into
focus:

> **Regions are created by declarations, not by control structures.** A skip lands at the end of
> the block that **declares the binding** whose establishment or assignment failed.

`while`, `if`, plain blocks, and `for_stam` contribute no region of their own; only the
declarations inside them do. This also **corrects case 7's destination**: "the enclosing block"
was ambiguous between the block enclosing the *statement* and the block declaring the *variable*.
It is the declaring block, and that choice is load-bearing:

> **Within one frame, no use of a binding observes a value left by a failed assignment.**
> Skipping to the declaring block ends the stale binding's scope at the same moment the skip
> lands. Cross-frame reassignment stops at the mutating callable's boundary, but statically
> invalidates the outer binding for later reads until the caller explicitly re-establishes it.

Spec §13 invariant 7 ("no binding holds a placeholder for a failure") still holds: the retained
outer value is a real previously-established `T`, not a null/error placeholder. S3 enforces the
stronger source-level rule by rejecting reads of that value after a hidden outer write.

#### The `acc = acc + f(x)` weak spot is closed — and the liveness proposal is withdrawn

The previous entry here proposed a liveness analysis: route out of a discarded region when the
failed binding is live across iterations. **Superseded — the declaring-block rule gives the same
answers syntactically, with no dataflow analysis at all**, because *where a binding is declared*
already encodes *whether it is carried across iterations*:

| Shape | Declared in | Skip lands | Behaviour |
|---|---|---|---|
| `acc = acc + f(x)` in a `for`/`while` body, `var acc` in the fn body | fn body | end of fn body | exits the loop; fn yields the error |
| `let y: int = f(x)` inside a loop body | iteration body | end of iteration body | loop continues — batch idiom |
| per-item check on the `for` variable itself (V6) | the `for` | end of iteration body | loop continues — batch theorem intact |

An accumulator is *by definition* declared outside the loop; a per-item temp is *by definition*
declared inside it. All four earlier rulings — case 1, case 7, case 8, V6 — collapse into the
one rule above, and every property that was wanted follows:

- the batch is killed for an accumulator, which is correct: a stale `acc` poisons every later
  iteration;
- the stale binding is never observable, because its scope ended;
- the defect cannot evaporate: the fn body's result *is* observed;
- one runtime report instead of n.

#### Three sub-cases the declaring-block rule does not cover directly (decided 2026-08-06, user)

**S1 — Element and field stores** (`arr[i] = f(x)`, `m.k = f(x)`). There is no binding being
assigned, and a partially-mutated container is **not** scoped away by leaving a block: it may be
aliased, or have been passed in as a parameter, so it outlives the region the skip would exit.
Scope exit therefore cannot restore the pre-failure state, and the invariant that makes case 7
safe ("the stale value's scope ends when the skip lands") does not apply.

**Ruling:** a failed element or field store is **reported**, using case 7's three tiers, and the
container is left in a documented partial state — it is not silently contained. The skip
destination remains the declaring block of the *container* binding, but the report is what
carries the truth, because the skip alone does not undo the mutation. TE-17 narrows this to
deferred checks only: a provably-fallible store is already a compile error, so the runtime case
is the `any`-sourced or unproven-callee residue.

**S2 — Module-level `var`.** The declaring block is the module body, so a failed reassignment
skips to the end of module initialization and the module fails to initialize. This is the
correct reading of the rule and is stated deliberately rather than inherited by accident: a
module whose top-level state is half-established must not become importable. Partial module
state is exactly the "stale binding visible to later code" failure at module scope.

**S3 — Reassignment from an inner function or closure** to a `var` declared in an outer frame.
The destination is in another frame, so the skip cannot be an intra-function branch. The accepted
rule is deliberately local: failure returns an error from the mutating callable and stops at that
callable's boundary. The call site receives the error as its ordinary result; it does **not**
re-apply the outer binding's declaring-block skip. Execution may continue after a discarded
result, but a call that may write captured `x` makes `x` unavailable for subsequent reads in the
caller until a definite explicit assignment re-establishes it:

```lambda
mutate()
use(x)       // compile error: x may have changed invisibly inside mutate
```

Two shapes are legal:

```lambda
mutate()     // mutate may read/use x internally
             // no later read of x in this caller

x = mutate() // the returned outcome explicitly re-establishes x
use(x)
```

The rule bans **invisible mutate-then-use flow**, not cross-frame mutation itself. Any read counts:
direct use, passing `x` as an argument, member/index access through `x`, capture by a later closure,
or reading it on the RHS of a purported re-establishment. An assignment re-establishes `x` only
when its RHS does not itself read the invalidated `x`; `x = x` is therefore not an escape.

This is ordinary definite-state analysis. At a control-flow merge `x` remains unavailable if any
reachable predecessor contains the hidden write without a definite re-establishment. A hidden
write in a loop therefore carries into later iterations. The analysis affects diagnostics only;
runtime routing remains local to `mutate`, and the previous runtime value remains a valid `T`.

The inner function is defect-capable, so `may_defect` must still treat outer-frame reassignment as
an effect. The compiler additionally needs the exact captured-write set for a known callee, not
only a boolean. An indirect call whose write set was erased must conservatively invalidate every
in-scope mutable binding that the callable may capture, or be rejected when that set cannot be
bounded soundly.

#### Violations found against the live tree (2026-08-06)

The dominance invariant was checked against the implementation. It holds, with these exceptions:

- **V1 — `fn_array_set` silently despecializes in place. REAL, LIVE, and the invariant's only
  hard counterexample.** A mismatched element store calls `convert_specialized_to_generic`
  (`lambda-eval.cpp`, in the `fn_array_set` ArrayNum arm — the `ELEM_FLOAT64`, `ELEM_INT64`,
  `ELEM_UINT64` and `ELEM_INT` cases all do it), converting a declared `int[]` into a generic
  `Array` *underneath a live binding*. The current mitigation is explicit in
  `mir_store_may_change_elem_type`'s comment (`transpile-mir.cpp`): *"the runtime elem-type
  guards on the inline paths cover the residue"* — i.e. **inline reads re-check the element type
  today, so guarded bodies are not in fact unconditionally native.** Under TE-17 a store to a
  declared container is a gated boundary: it either satisfies the contract or originates an
  error. Declared containers must never silently despecialize. Fixing V1 therefore **deletes the
  per-read elem-type guards — a performance win, not just a correctness fix.** (Inference-only
  narrowed arrays, which have no declaration to violate, may keep the fallback.)
- **V2 — `for x: int[] in e` does not parse.** `loop_expr` (`grammar.js`) admits a type
  annotation only on the *key* of the `for k, v in e` form (`index_type`, and only an
  `$.identifier`, not a type expression). The value variable has no annotation slot at all. Case
  1's `for` form needs the grammar extended, or case 1 must be restricted to `let`/`var` until
  it is.
- **V3 — reassignment inside the guarded scope is not covered by the entry guard.**
  `var x: int[] = e; …; x = g()` is a second boundary the declaration's guard does not dominate.
  **Ruling: reassignment to a declared binding is declaration-shaped** — it guards, and on
  failure skips to the end of the enclosing block with the *old* value retained (per §10.8).
  This is what preserves "the binding is always native", and it closes most of the open
  statement-position question in `Lambda_Impl_Error_Handling.md` §8.1.
- **V4 — case 2's "returns the error directly" must not become a hidden early return.** It is
  the *call expression* that evaluates to the error (case 5 contagion), not a non-local exit
  from the caller. Consequently an argument-check failure widens **the call site** to
  `T | error`, never the callee's signature — the §10.7 firewall is untouched.
- **V5 — "the body always executes in native lane" is too strong as phrased.** A body may
  contain arbitrary fallible code of its own. The precise claim is a *dominance* property: **the
  declared binding is native-lane at every use within its scope.** State it that way; the
  stronger reading is false and would be unimplementable.
- **V6 — the `for` skip target must be the iteration body, not the loop.** Skipping to the end
  of the whole loop would kill the batch on one bad item, which is the granularity TE-15
  explicitly rejected. Per-iteration skip means each body runs native while **the loop's
  *result* is `(T | error)[]` — Item-lane, per TE-17.** Body native, result boxed: that is where
  TE-15 and TE-17 meet, and it must be written down or someone will implement break-out-of-loop.

Nothing else in the checked set violated the invariant: `any`-provenance re-entry is already a
checked boundary (TE-5 R3), closures capture after the guard, COW preserves C4 value semantics,
and cross-suspension mutation is excluded by the isolate model.

#### What `pn` adds

**Exactly one ruling: case 7 (reassignment).** Everything else falls out of decisions already
made:

- A bare expression statement whose value is a soft error simply discards it, as any discarded
  value would be — consistent with "soft errors are values", and errors log at origination, so
  the breadcrumb survives.
- A raised `T^E` in statement position is already E228-gated and cannot reach here.
- A defect at a declaration boundary skips to the block, which is case 1.

Because interior skip is gone, the "contagion ≡ routing" equivalence obligation largely
dissolves too: expressions use contagion in both `fn` and `pn`, so there are no longer two
lowerings that must be proven to agree on which effects ran.

**Residue — closed.** The discarded-region problem (a failed reassignment inside a `pn` loop
body swallowing the defect) is resolved by the declaring-block rule: the skip leaves the loop
and lands where the binding was declared, which is an observed position. S1–S3 above settle the
three shapes the rule does not reach directly. TE-18 has no open items.

---

## 8. Phasing

Each phase gates on `make test-lambda-baseline` and `make test262-baseline` at 100% plus new
targeted tests (every new `*.ls` with its `*.txt` golden, per repo rule; negative compile-error
cases assert the diagnostic text). Performance measurements use `make release`, never a debug
binary.

### 8.1 Round-1 enforcement inventory (2026-07-30)

Round 1 implemented the boundaries summarized in this ledger. The retained P0–P4 narrative
below is historical implementation context, not a claim that every later semantic expansion is
complete. The revised implementation plan in
[`Lambda_Impl_Type_Enforce.md`](Lambda_Impl_Type_Enforce.md) is the current completion
authority for C14c, value-aware numeric admission, internal exclusion types, implicit clean
firewalls, and the remaining gaps.

| Boundary / invariant | Delivered implementation |
|---|---|
| Annotation metadata | `AstNamedNode` and `NameEntry` retain `declared_type` separately from the initializer/effective type. A `let` and a `var` therefore carry the same binding contract; only `var` permits later replacement. |
| Static boundary | The builder classifies annotated declarations, named-map literal fields, whole-value `var` reassignments, parameters, returns, typed map members/indices, and typed array elements as proven, rejected, or deferred. Known argument and return mismatches use E207 and E208; generic binding/write mismatches use E201. Plain `T` rejects known `null`; `T?` remains the spelling that admits it. |
| Deferred boundary | MIR routes dynamic declarations, calls, parameters, returns, array writes, and map writes through `lambda_type_check`. `lambda_type_matches` is also the `is` predicate path: it handles simple/numeric cases directly and asks the schema validator to match maps, arrays, unions, and occurrences. Failed shape checks retain the first validator path in the rich error. |
| Error result | `lambda_type_error` creates a diagnostic error object rather than returning a native conversion fallback. An incoming error is preserved at an unchecked clean boundary, so no typed destination is established with it. Core `any` admits error; the validator's historical `any` remains the intentionally non-error validation pattern. |
| Function contracts | Every emitted function records its `TypeFunc` signature. Direct and first-class calls enforce required/max/variadic arity, the boxed entry resolves omitted optional/default parameters before typed unboxing, and a variadic tail is marshalled as `varg()`. Dynamic calls with a wrong type or arity return rich E201/E206 values rather than entering the body. |
| Named map input | A literal is checked field by field before any layout decision. A dynamic `Person` binding is validated deeply against the same `Type*`; a failure such as `.age: expected int, got string` reports that path and never publishes the binding. Named map contracts are open: extra keys survive. |
| Typed array writes | A typed array write constructs and validates a candidate before replacement. A bad dynamic element cannot silently downgrade an `int[]` to a heterogeneous array. Array construction also keeps `any`, `null`, and error elements in generic storage rather than silently unboxing them into a compact numeric carrier. |
| Typed map writes and COW | `lambda_map_set_checked` and `lambda_map_path_set_checked` operate on a detached candidate root. The raw store may extend/repack the candidate's exact runtime shape, then the entire candidate is checked against the binding's semantic root contract. Only a successful candidate replaces the root/path; the old value and any shared snapshot remain unchanged on failure. This covers named members, computed keys, and nested writes. |
| Physical layout | A map's current `ShapeEntry` always describes its stored bytes. An unannotated root may legally evolve from `age: int` to `age: string`, which creates/reuses a matching exact shape. A typed root may undergo the same physical repack only when the resulting value still satisfies its semantic contract (for example an `int | string` field); `Person.age: int` rejects the change. |
| Constrained types | The accepted interim rule is implemented: checks enforce the base type and deliberately do not evaluate the predicate refinement. This is validator follow-up work, not a hole in the base binding contract. |

**Map member assignment rule, concretely.** For `var p: Person = ...`, `p.age = e` is never an
in-place overwrite of the old `Person` bytes with an arbitrary carrier. The runtime evaluates
`e`, clones the affected root/spine, writes it using a shape that matches its actual stored
value, validates the new root as `Person`, and only then installs that replacement. Thus a bad
dynamic `e` yields an error and leaves both `p` and `let snapshot = p` untouched. Conversely,
`var q = {name: "Ana", age: 30}; q.age = "very old"` has no declared root contract: it is a
legal COW shape evolution, and `snapshot` still observes the old `{age: 30}` map. This is the
semantic distinction between an inferred mutable value and an annotated mutable binding.

The generic native dynamic-call ABI has an existing physical eight-argument dispatch ceiling.
For a valid signature beyond that ceiling, a dynamic call reports an arity diagnostic rather
than bypassing its type contract; statically resolved calls retain their normal compiled ABI.
That implementation limit is separate from the annotation-enforcement semantics and is tracked
with the general first-class-call ABI work.

**Verification snapshot (2026-07-30).** `make build` and `make build-test` pass. Focused
coverage passes for static declaration/map/null/return rejection; deferred declaration, named
map, parameter, and return rejection; typed array/map writes; dynamic arity; dynamic
optional/default/variadic calls; and map COW snapshot isolation. The complete
`./test/test_lambda_gtest.exe` suite passes **622/622**. The required
`make test-lambda-baseline` gate is green: **2,104/2,104** input tests and
**1,542/1,542** Lambda-runtime tests. `make test262-baseline` is also green:
**40,261/40,261** baseline tests fully pass, with **0** non-fully-passing tests,
**0** failures, **0** retries, and **0** regressions. This is a Round-1 verification snapshot;
the revised enforcement scope remains active, while §9's performance work stays explicitly
deferred.

**P0 — Semantic foundation and static completion.** Establish TE-6's canonical
`subtype`/`matches` primitives and truth tables first, then TE-7 items 1–4: declaration reorder
and scalar check (closes TS-1), extended-type survival, named-shape field checks, per-return
checks, arity diagnostic, B7b annotated-root member checks, and new error codes. Semantically valid
typed arrays without packed carriers use generic storage rather than receiving a TS-7
diagnostic. **TE-11 null strictness ships here too** (plain `T` rejects `null`; `T?` is the
optional spelling), and the **`or`-typing narrowing rule**
(`type(a or b) = (type(a) − {error, null}) | type(b)`, TE-13) keeps
`let n: int = int(s) or 0` valid. Record declared/effective types explicitly; do not re-derive
annotation-ness from emitter whitelists. *Exit evidence:* t1/t3/t4/t6/t13/t14-family probes
become compile errors; the subtype/match truth-table tests and both baselines are green.

**P1 — Checked-boundary infrastructure.** Add the rich `lambda_type_error` object constructor
and `emit_checked_boundary` with a full expected `Type*`, error-preservation arm, and the
TE-6 runtime match (type-directional, plus exact-value numeric admission with
re-representation). Missing-argument padding is removed (arity is P0-static where the callee is
known; an error value otherwise), and effective-type computation (`T | error` for dynamic call
or inferred-open outcomes) is plumbed through the front end. Check placement may initially
follow the safest boxed/site-local implementation; TE-14 optimization is not an exit
dependency. *Exit evidence:* dynamic wrong arguments and t10 yield rich errors; annotated
declarations never bind error, failed calls never enter the body, and both baselines are green.

**P2 — Return honesty (closes TS-2's class).** Per-return checked boundary including boxed
returns (`emit_coerce_boxed_to_declared` gains the check for INT/FLOAT/BOOL/STRING); audit the
four ad-hoc `STRING` native-return widenings; a call expression's recorded static type is now
honest for every consumer — the declared type when clean, `T | error` when open.

**P3 — Named types at runtime (B3/B9).** TE-10: validator call emission at DEFERRED bindings;
validator hardening (base-type enforcement for constrained types, error-path surfacing,
openness default); optionally the `input(url, {schema: Q})` convenience (insertion point
`lambda-eval.cpp:2981-3018`) so parse-time failures carry file context. *Exit evidence:* t16 yields an error
value carrying `.a: expected int, got string`; no binding is established; the §4.3 `Config`
example works end-to-end without assuming the parsed map has been repacked.

**P4 — Container and member-write enforcement (B7a/B7b + B2's hole).** Writes through an
annotated `T[]` check the element. A map write through an annotated root checks that the whole
post-state still conforms to the root's declared `Type*`; all failures leave the destination
unchanged. Open extra fields are allowed by an open contract, but still extend the map with an
exact runtime shape/slot matching the stored value. Legal field-type transitions rebuild and
repack the shape, with the COW replacement propagated to the root or parent. Fast-path fallback
produces an error instead of degrading a typed contract; the ARRAY_NUM cross-convert path gets
element checks; and the B2 raw-pointer/ANY downgrade is replaced by an honest tagged value.
Unannotated containers keep today's flexible type/shape evolution — inference is not a binding
contract (TE-12). *Exit evidence:* unannotated `int → string` map reshaping, annotated static
and dynamic mismatch cases, union-field shape transitions, computed keys, open extra fields,
COW snapshot isolation, and no-mutation-on-failure tests pass.

**P5 — Legacy-consumer consolidation and optimization hand-off.** Migrate remaining `fn_is`,
validator, and emitter call sites onto P0's shared subtype/match foundation; delete superseded
unchecked boundary paths. Then measure and design TE-14's boxed/unboxed entry strategy,
validated-map canonicalization, direct field offsets, and witness caching without changing
semantics.

---

## 9. Out of scope / future stages

- **Perf leverage of annotations** (the next stage, gated on this one): TS-3 ANY-downgrade fix,
  TS-5 direct field offsets, two-entry per-callee specialization (raw entry for
  statically-proven callers, checking entry for ANY callers — the Sorbet shape, JIT-specialized
  so hot paths never see the check), validation-witness caching for named types, and the
  sys-func retrofit (registry enrichment, clean/open sys-func versions, the `len`-branch fix —
  §10.9, deferred there 2026-07-29).
- **XML schema-driven typing** — KIV per user decision; §4.2 records the substrate facts.
- **Flow-sensitive narrowing** (`if (x is int)` refining `x`), **generics**, **checked-cast
  surface** (`as` / `as?` — no cast operator exists today, t7), **arity overloading** (TS-8).
- **`x is (int[])` — parenthesized types on the `is` RHS**: noted with full grammar analysis in
  TE-13's side-finding; belongs to the **pattern-grammar and validator design**, not this
  proposal (user, 2026-07-29).
- **TS-9** int→float overflow policy — **CLOSED 2026-08-01 by C16** (`int` = the
  float64-representable integers, tagged; no overflow promotion exists to be silent about).
  See `Lambda_Semantics_Formal2.md` C16 and spec §4.1.
- Side-findings filed for separate handling: TOML datetime unsupported (§4.2); `it2l` missing
  ERROR arm and the `0`-vs-`INT64_MAX` asymmetry (§5.2) — both subsumed by TE-8 at boundaries
  but the raw converters may deserve their own cleanup. (The index-OOB → `ItemNull` behavior
  flagged in §5.2 turned out to be *conformant*: `Lambda_Formal_Semantics.md` §7.1 specifies
  reads-are-total / absence-is-null; only OOB *writes* raise.)

## 10. Open questions

1. **Trap class — DECIDED 2026-07-29** (user): error-return model, no panic, no forced `T^E`
   (TE-9 records it). The decision opens sub-questions 2–9:
2. **One discharge surface for two error channels — elaborated as TE-13, revised 2026-07-29
   to the two-form model.** `T^` = enforcing (E228, raise-capable, explicit-only); `T | error`
   = non-enforcing union (what inference produces). Discharge forms (`^err`, postfix `^`,
   `^e`/`is error`, `or`-defaults) are value-directed and work identically over both.
   Residuals (a)/(b)/(c) all **DECIDED**: the obligation split is *type-directed* — attached
   to the `^` spelling itself, closing (a); `R^(E | error)` widening per the unified channel;
   vacuous-`^` warn/silent when cleanness is inferred, static error when explicit.
   **CONFIRMED 2026-07-30:** `let x: int | error = a()` counts as immediate E228 engagement.
3. **Return ABI for open/native variants — DEFERRED 2026-07-30.** The language contract is
   representation-independent. TE-14 leaves boxed/unboxed entry count, check placement, and
   open-return side channels to the implementation/performance phase, subject to boxing
   invisibility and TE-3's local proof invariant.
4. **Error payload — DECIDED 2026-07-30 (user): proper rich error object/value.** Drop the
   inline code-only form. Every type-enforcement failure constructs an error with at least
   `code`, `message`, expected type, actual type/value summary, boundary/source location, and
   validator path when applicable. Boundary failure is cold; preserving the diagnostic
   contract takes precedence over allocation avoidance.
5. **Value-domain semantics of error values — RESOLVED 2026-07-29: already fully specified in
   `Lambda_Formal_Semantics.md`.** Checked against the doc: **equality** (§5.1) — `nan` and
   `error` are the two designed poison carve-outs, never equal to anything including
   themselves (`error == error → false`, mirroring nan; so `a(b) == 5` on an errored `a` is
   `false`, and the rationale explicitly weighs the swallow-vs-spread trade: totality keeps
   set processing alive, `is error`/`case error:` arms are the classification relation).
   **Total order** (§6.2) — `error` is the maximum: `… < type < function < nan < error`
   ("null is less than everything — absence; nan and error are beyond everything — broken");
   sort is stable, `desc` is full reversal. **Comparison** (§6.1) — `<` on error operands
   taints (returns `error()`). **Dedup/grouping** (§5.6) — each error stands alone, never a
   duplicate. **Null-vs-error taxonomy** (§7.1/7.2) — reads are total, absence is `null`
   (OOB *reads* → null, so the emitter's `MIR_INDEX_OOB_ITEM_NULL` conforms to spec); OOB
   *writes* raise. One implementation-fidelity nit remains: `it2d`'s ERROR→`NaN` fallback
   *degrades* error-poison to nan-poison — distinct classes under both `is` and the total
   order — which the TE-8 short-circuit (error stays error) fixes at boundaries.
6. **Errors in containers — DECIDED 2026-07-29 (user), with corrected framing: the exclusion
   is *type-level*, not representation-level.** `[a, error(...), b]` is allowed — a plain array
   holds an error as an ordinary element, and that plain array is the batch idiom for per-item
   results. The typed `int[]` excludes error *because its element type does*: constructing or
   writing `int[]` from data containing an error yields an error result (failed element check /
   TE-8 short-circuit). `ArrayNum` is an internal optimization invisible to the user ("to the
   user, it is array") — it cannot hold an error but can hold `NaN`, which is a float value,
   not an error; the representation choice never changes semantics. Shaped-map typed fields
   follow the same type-level rule (the t4 path).
7. **Clean-ness inference mechanics — DECIDED 2026-07-29 (user, incl. the division
   consequence): declared return types are effect firewalls; bindings are not (§10.8).**
   The rule: a function with an explicitly declared plain-`T` return whose body is open
   (calls an open function, or contains an error-originating operation whose result reaches
   the return) is a **compile error** — the author picks from a three-way menu: **contain**
   the openness locally (`^err` + handling, `or` default), **disclose** it as `T | error`
   (non-enforcing — callers see it, owe nothing), or **impose** it as `T^` (enforcing —
   callers must handle). Inference itself only ever produces `| error`, never `^` (TE-13
   two-form model). `-> any` is the explicit error-admitting escape (TE-5). **REVISED
   2026-07-30 (user restatement): undeclared returns are no longer silent** — an unannotated
   fn is implicitly contracted `(any \ error, …) -> any \ error` and enforced exactly like a
   declaration: inference finding an error-possible return is a reported type error. The
   firewall thus covers *every* function; the default contract is `any \ error`. Consequences:
   - **§10.7 mostly dissolves.** Silent clean→open cascades stop at the first declared frame
     (stability); recursion needs no fixpoint at all — every fn now has a contract, declared
     or the implicit `any \ error`, so assume it and check the body; `pub` fns need no effect
     metadata (recommend, not require, declared returns on `pub`). A function *value* invoked
     dynamically remains per-call-site open, as before.
   - **Fixes an inversion**: explicit `open_call()^` inside a plain-`T` fn is *already* a
     compile error (`Lambda_Error_Handling.md`); implicit propagation slipping through would
     have been backwards. Zig (`catch` forced in non-`!T` fns), Swift (`do-catch` in
     non-throwing), Rust agree.
   - **Emitter consequence**: declared-plain-`T` function bodies are clean-return by
     construction. A later native variant may exploit that fact; TE-14 deliberately leaves the
     carrier/ABI choice out of the semantic phase.
   - **No E228 spill**: inference never produces `^` at all — it produces `| error`, which
     never triggers must-handle. A function pushed open by its arithmetic discloses as
     `T | error` with zero caller impact; `^` is reserved for authors *choosing* to impose
     handling. Must-handle stays exactly where it is today (declared `^` and pn/I-O raisers,
     §7.3).
   - **Interior vs interface (settles §10.8's residual)**: unannotated bindings flow —
     `let x = open_call()` infers `T | error`. Annotated bindings are contracts —
     `let x: int = dynamic_call()` checks at the boundary, establishes `x: int` on success,
     and produces an error before `x` exists on failure. Declared returns remain effect
     firewalls.
   - **Consequence, REVISED by C14c (2026-07-30)**: division no longer error-originates at
     all — `/`, `div`, and `%` stay in number (`int div int → float`, IEEE `inf`/`nan` on a
     computed zero; `Lambda_Formal_Semantics.md` §4.7). The firewall's error-originating class
     is therefore exactly the set of `| error`/`^`-declared calls; pure-math bodies are clean
     by construction. (`fn avg(a: int, b: int) int { a div b }` still fails — but as a plain
     E208 return mismatch, body float vs declared int; the `float`-returning form passes.)
     Joins the D2I sweep as a pre-P0 audit, and the diagnostic must state *why* the body is
     open (first cause, e.g. "call to 'g' may return error"), making the "why open?"
     diagnostic part of the error UX rather than a nice-to-have.
   - TE-9's open-case wording is revised accordingly: the silent effective-`T | error`
     applies to *expressions and bindings* only; every fn return — declared, or implicitly
     `any \ error` — enforces (2026-07-30 restatement).
   - **Recursion and system faults — DECIDED 2026-07-30 (reaffirming C14, 2026-07-06): we
     handle it for the user.** `fn f() T` with recursive `f` does *not* force
     `T | recur_error`: infinite recursion / stack exhaustion is a **system/resource fault**,
     and per the C14 semantics ruling such faults are the **unchecked third channel** —
     raisable from anywhere including `fn`, invisible to fn types, transparent through frames,
     caught at a pn `^err` boundary or the global handler (Java `Error`-class precedent; the
     `==` depth-limit raise is the same class; the runtime hook `lambda_stack_overflow_error`
     already exists). Forcing a declared `recur_error` would be checked-exception spill for a
     fault no local caller can meaningfully handle. This also resolves a flagged tension:
     C14's "`T | error ^ E` must never exist" is *system-channel-specific* — the fault channel
     never appears in types — while TE-13's user-declared mixed form `T | e1 ^ e2` remains
     legal. *Implementation note (added 2026-07-30): the next implementation phase must handle
     the fault channel explicitly — stack exhaustion **and out-of-memory** (and the `==`
     depth-limit raise) route as unchecked faults: transparent unwind through fn frames,
     catchable at pn `^err` or the global handler, never typed error values. Detail that
     matters: OOM cannot allocate its own diagnostic, so fault objects must be
     pre-reserved/static — the §10.4 rich-error contract applies to the *checked* channels
     only.*
8. **Declarations and reassignment — REVISED 2026-07-30 (user): annotations are contracts;
   inference flows.** `let x: T = e` establishes only `x: T`: a DEFERRED success binds `T`, and
   failure yields the boundary error before the binding exists. Reassignment checks before
   commit and leaves the old value unchanged on failure. `let x = e` has no such declared
   boundary and infers `e`'s effective type, including `T | error`.

   An explicitly declared error possibility (`T^` or `T | error`) still cannot enter plain
   `T` without visible discharge/narrowing; that is STATIC-REJECTED. A genuinely dynamic
   `any` source is the DEFERRED case. Consequently a successfully established annotated
   `var x: T` may use a `T` carrier; it never needs boxed storage merely to hold a failed
   check, because the error is not stored in `x`.
9. **Sys-func registry metadata for the clean/open split (extends B13) — DEFERRED 2026-07-29
   (user) to the perf-tuning stage**, where the clean/open sys-func versions are built anyway.
   The TE-9 convention (error values, never silent wrong values) is normative *now* for any new
   or touched sys-func code; only the retrofit of the existing surface rides the perf stage.
   Scope when it lands: the registry must
   record per-param types (static checking), the success return type, whether the function can
   *originate* errors (partial `abs` vs total `len`), and its admission policy on inputs.
   `Lambda_Formal_Semantics.md` §7.3 and `Lambda_Design_Sys_Func.md` supply the adjudicating
   principle: system `fn` failures are values, channel `T?` or `T | error` (never `T^E` on a
   system fn), with admissive no-answer cases using `null`/`[]`/`""` and non-admissive cases
   using detailed `error()`. §7.3 also confirms `input`/`fetch` as pn-family raisers (E228
   conformant) and supplies the wrapper idiom for set-oriented input.
10. **Null strictness — DECIDED 2026-07-29 (user):** enforce `T?`-for-nullable immediately in
    P0; no warn-only release.
11. **Openness default for named map types — DECIDED 2026-07-29 (user): open** (TE-10). Whether
   a closed form / `allow_unknown_fields` ever becomes user-visible syntax is left for the
   future.
12. **`let x: int = 3.5`** — P0 makes it a compile error (relation says FLOAT↛INT). Any shipped
   scripts relying on the D2I truncation need a sweep before landing. Prior art (surveyed
   2026-07-29) is near-unanimous for the error: Java ("possible lossy conversion"), C#, Rust,
   Swift, Kotlin, Dart and mypy all reject at compile time regardless of value; Go and Zig
   reject fractional constants ("constant 3.5 truncated to integer") while allowing
   exact-valued ones (`3.0`); the only allow-camp is legacy C/C++ assignment-init (silent
   truncation — regarded as a defect; C++11 bans it as "narrowing" in brace-init, and
   `-Wconversion` exists to flag it) and SQL — which **rounds** rather than truncates
   (Postgres `CAST(3.5 AS int)` = 4). That the allow-camp cannot even agree on the semantics
   is itself the argument against silent conversion. Lambda's rule (per TE-5/TE-6 as
   corrected 2026-07-30): statically reject float-typed initializers including `3.0`; at a
   *deferred* `int` boundary the check is value-aware — an `any` value holding an
   exactly-integral float (`3.0`) passes and is re-represented, an inexact one (`3.5`) fails
   with the rich error. Explicit `int(...)` conversion owns lossy conversion policy.
13. **t2's `var`-path `null`** — mechanism narrowed but not step-verified (§5.1); verify while
   implementing P0 so the fix isn't aimed at a ghost.
14. **Constrained types — CONFIRMED 2026-07-30 (user): base-only interim is accepted.** The
    validator is the eventual predicate checker for `T where …`; until that validator work
    lands, enforcement checks the base `T` only. This is a documented, deliberately narrow
    validator deferral and does not block the core enforcement phases.
15. **`STRING` in the native-return set** — P2 audits the four ad-hoc widenings; the clean fix
   (widen `mir_is_native_scalar_value_type` or stop widening ad hoc) interacts with the perf
   stage's ABI plans; decide there, enforce honestly here.
