# Lambda Formal Semantics — Findings & Design Review

**Status:** working document for discussion
**Date:** 2026-07-03 (probed against master `lambda.exe`, MIR-Direct path, REPL + script mode)
**Companions:**
- [Lambda_Semantics_DSL_Proposal.md](Lambda_Semantics_DSL_Proposal.md) — the proposed native semantics sub-DSL; its §13 assesses base-language readiness against these findings.
- [Lambda_Semantics.md](Lambda_Semantics.md) — the existing PLT Redex model of Lambda and its fixture-verification bridge; the A-findings below are candidates for that project's designer-ruling workflow (§4.7 there), and several corroborate disagreements the Redex bridge already surfaced (`list` vs `array` in `is`, `type(error_value)`, typed-array coercion).

This document records a close review of Lambda's type system and semantics with two
questions in mind: *where is behavior ambiguous, undefined, or divergent from the
docs?* (Part A — empirically verified), and *where would a different design be
better?* (Part B — opinion). Findings carry stable IDs (`A1`…, `B1`…) for reference
in discussion and triage.

Everything in Part A was verified by running the probes shown; nothing is
speculative.

---

## Part A — Ambiguities, undefined behavior, doc–implementation divergence

### A1. Integer overflow: three inconsistent regimes

| Expression | Result | Regime |
|---|---|---|
| `36028797018963967` (2⁵⁵−1) | `36028797018963967` | int56 max, fits |
| `36028797018963967 + 1` | **`error`** | int overflow → runtime error |
| `127i8 + 1i8` | **`-128`** | sized ints → silent two's-complement wrap |
| `255u8 + 1u8` | **`0`** | same |
| `9223372036854775807` (literal) | **`9.22337e+18`** | oversized literal → **silent float conversion, precision lost at parse time** |

Three different overflow behaviors coexist, none documented. The literal case is the
most dangerous: it is silent data corruption before execution even begins.
(`2**62` → `4.61169e+18` is separate and fine: `**` is documented float-producing.)

**Why it matters:** any formal rule for `+` must state which regime applies; a
semantics model cannot be written until this is decided (see B2).

### A2. Division and modulo: undocumented choices, inconsistent failure

| Expression | Result |
|---|---|
| `1/0` | `inf` (IEEE, no error) |
| `0.0/0.0` | `nan` |
| `1 div 0` | **`error`** |
| `-7 div 2` | `-3` (truncation toward zero) |
| `-7 % 2` | `-1` (dividend sign, C-style) |
| `7 % -2` | `1` |

`/` follows IEEE while `div` raises — defensible, but unstated. Truncating `div` and
C-style `%` are choices languages differ on (Python floors: `-7 // 2 == -4`,
`-7 % 2 == 1`). The moment a Python model exists, Lambda's own convention must be
precisely stated to keep meta- and object-language arithmetic distinct.

### A3. `"" ≡ null`: two kinds of empty string (REVISED after deeper probing)

The identification is applied only to **literals**, not to runtime values — so the
language currently has *two distinct empty strings*:

| Expression | Result | |
|---|---|---|
| `"" == null` | `true` | literal `""` normalizes to null at compile time |
| `"" is string` | `false` | |
| `match "" { case string: ... }` | takes `default` arm | |
| `d.name == null` (where `d.name` is `""` from JSON input) | **`false`** | runtime empty string is a real string |
| `d.name is string` | **`true`** | |
| `len(d.name)` | `0` | |
| **`d.name == ""`** | **`false`** | **runtime `""` ≠ literal `""`** |
| `split("a,,b", ",")` | `["a", "", "b"]` | runtime `""` survives in data |
| `format(input('{"name":"","note":null}'), 'json')` | `"name": ""`, `"note": null` | round-trip preserves the distinction |
| `[] == null`, `{} == null` | `false`, `false` | empty containers never normalize |

Consequences:

- **An empty field from data equals nothing writable.** `d.name == ""` is false
  (literal is null) and `d.name == null` is false (runtime value is a string) — the
  most common data-cleaning comparison there is cannot be expressed with a literal.
  Only `len(x) == 0` reaches it.
- The intended design (`""` as string's null) is **not implemented uniformly and
  cannot be**: input parsers and string functions produce genuine empty strings, so
  either every boundary normalizes them (destroying `""`-vs-`null` round-trip
  fidelity, which the formatter currently preserves) or the identity fractures as it
  has. There is no third option.
- Generic code over `string` (match arms, `is`-guards) silently misses literal `""`
  but accepts data-derived `""` — the same value classifies differently by
  provenance.
- The identification is **asymmetric** across types: only scalar literals (`""`,
  `''`) normalize; empty containers do not, and `0` is truthy.

Docs acknowledge the literal normalization (`Lambda_Type.md` § Constrained Types) but
not the two-empty-strings consequence. See B1.

### A4. `symbol == string` raises instead of returning false (doc divergence)

Docs (`Lambda_Cheatsheet.md`): `'name' == "name"  // false: symbol != string`.
Implementation:

```
> 'a' == "a"
(eval):1: == not found        ← runtime error, not false
```

A core operator diverges from its documentation. Either behavior is defensible
(strict "incomparable types are an error" vs. total structural equality) — but the
two sources currently disagree, and `==` is described as total structural deep
equality elsewhere in the same docs.

### A5. Container equality is not reflexive; array equality is representation-sensitive

- `[nan] == [nan]` → `false`. IEEE NaN leaking through structural equality is
  conventional, but it makes `==` non-reflexive on containers, and it will bite
  fixture goldens that contain `nan`.
- Known issue (task_38782787): ArrayNum `==` is representation-sensitive rather than
  value-based — two arrays with equal numeric contents can compare unequal depending
  on unboxed representation. Same root family as A8.

**Why it matters:** `==` is the verification harness's comparison primitive (both the
Redex bridge and the proposed DSL harness). Its formal definition — reflexivity
carve-outs, representation independence — must be written down before goldens are
checked against any model.

### A6. Out-of-bounds behavior: three outcomes, and error values flow unchecked

```lambda
let a = [1,2,3]
[a[10], a[-1], "hello"[99]]     // → [error, error, null]
```

- Array OOB and negative index produce an **error value** that flows silently — into
  an array literal, past `let`, with no compile-time complaint.
- String OOB produces **`null`** instead.

This contradicts the error-discipline story ("Lambda refuses to compile code that
ignores errors", `Lambda_Error_Handling.md`): the `T^E`/`raise`/`^` enforcement
applies to user functions, but **built-in operators emit unchecked error values
freely**. Combined with errors being falsy, `a[i] or default` silently swallows what
was morally an exception.

### A7. Aliasing under mutation (REVISED after deeper probing)

Probe (`pn` context):

```lambda
var a = [1,2,3];  let b = a;  a[0] = 99      // b[0] == 99     → arrays: REFERENCE
var c = <Counter step:1>; let d = c; c.increment()
                                             // c.value == 1, d.value == 1 → objects: REFERENCE too
var e = <div class:"x">; let f = e; e.class = "y"  // f.class == "y" → elements: REFERENCE
let g = [4,5,6]; var h = g; h[0] = 77        // g[0] == 77 (!) → mutating a LET-bound value
```

**Revision:** the original probe concluded objects had COPY semantics
(`d.value == 0` after `c.increment()`) — that was an artifact of object-mutation
bugs, not copy semantics; the probe never read `c.value` back (also 0). With a
working mutation path, the `let` alias sees the mutation: **all containers alias**
(uniform reference semantics), and `let`-bound values are mutable through a `var`
alias — directly violating any "let is final" reading.

**Object pn-method mutation is separately broken** (bug cluster, probed):

- `var c = <Counter>` (all defaults): `c.increment()` silently no-ops
  (`c.value` stays 0); with an explicit attribute (`<Counter step: 5>`) it works.
- After creating aliases (`let d = c; var e = c`), a *second* `c.increment()` is
  silently lost (`c.value` stays 1) — mutation reliability depends on sharing state.
- `pn add(n: int) {...}` — a pn method **with a parameter** — fails to parse;
  only zero-arg pn methods compile.
- `frozen.increment()` on a `let`-bound object: no error, silently ineffective.

Plus the documented closure regime: docs claim captured `var`s get a **writable
copy** (`Lambda_Func.md` § Mutable Captures) — probing shows this behavior does not
exist: assignment to a capture in `fn` is a compile error (that then continues
executing with the assignment dropped), and in a nested `pn` it crashes at runtime
(`unknown add type: 0, 5`). The doc section is aspirational (A10 family). Capture
snapshots at creation (by value) do work correctly.

So the current state is: uniform reference aliasing, unreliable object mutation,
no working capture mutation, and nothing documented. See B3, and §C4 for the
resolution discussion.

### A8. Array covariance: `is` and assignment disagree; failure is a silent log

Docs: `int[]` is a subtype of `any[]` (`[1,2,3] is any[]` → true). But:

```lambda
var xs: int[] = [1, 2, 3]
var ys: any[] = xs        // [ERR!] ensure_typed_array: cannot coerce array[num] to any[]
ys[0] = "boom"            // [ERR!] fn_array_set: null or invalid array pointer
// execution CONTINUES; xs unchanged; no user-visible error raised
```

Three problems in one: (1) subtyping-for-`is` and subtyping-for-assignment disagree;
(2) the unboxed `ArrayNum` representation is observable in the type semantics (same
family as A5); (3) the failure mode is a **log-only error with silent continuation**
— `ys` is left invalid and the program keeps running, violating both the
static-typing claim and the error-enforcement discipline.

(The mutation hole itself is defended — covariant write-through never happens. It is
the *manner* of defense that is broken.)

### A9. REPL error handling: rollback replays the session

A runtime error in the REPL ("Error during execution. Last input rolled back.")
causes subsequent inputs to **re-execute and re-print the entire session history**,
garbling output; multi-input sessions after an error also left the parser stuck in
continuation (`.. .. ..`) state. Reproduced twice with independent inputs (A4's
probe, and an OOB probe). Implementation bug rather than semantics, but it makes the
REPL unusable as a probing/teaching tool precisely on the error paths that most need
probing.

### A10. Specification gaps (doc-level)

- **Generics:** `fn identity<T>(x: T) T` appears once (`Lambda_Type.md`
  § Inference Limitations) and nowhere else — no other doc, no grammar support
  apparent. Aspirational text presented as existing syntax.
- **`as` assertion:** mentioned once ("runtime error if wrong") with no definition of
  the error's type or catchability.
- **Open vs. closed map matching:** `{name: string}` already matches maps with extra
  fields (`obj is {name: string}` → true), yet `{name: string, ...}` is documented as
  the "open map" form. What the non-`...` form means in *assignment* position vs.
  `is` position is unspecified.
- **`type()` vs `is` granularity:** `type([1,2]) == array` (coarse) but
  `[1,2] is int[]` (fine). The two "what type is this" idioms disagree in general;
  which one `match` arms use is worth stating (empirically: `match` follows `is`).
  Related: Redex disagreements #3 (`list` vs `array` in `is`) and #5
  (`type(error_value)`) in [Lambda_Semantics.md](Lambda_Semantics.md) §4.7.
- **`match` arm ordering with overlapping types** (e.g. `case int` before
  `case number`): first-match wins, presumably — stated nowhere.
- **Nested `~` scoping:** a `that` inside a pipe inside an object method — which `~`
  binds? Presumably innermost-wins; unstated (see B4).

---

## Part B — Design opinions and recommendations

What the language gets *right* deserves stating first, because it should be
protected: symbols as a real type distinct from strings; elements as the universal
term/document structure; `T^E` error returns with falsy errors; occurrence types
unifying arrays, optionality, and repetition; string patterns inside the type system;
structural deep `==`. This is a coherent, data-first design with taste. The critiques
below are about corners, not the core.

### B1. Retire `"" ≡ null` (fixes A3)

The convenience ("empty means absent") is not worth losing closure of the string
type. Absence-coalescing is already served by `?` types and falsy-`or`. If full
retirement is too breaking, the minimal repair is: `"" is string` → true and
`"" == null` → false, keeping only truthiness (`if ("")` falls to else) — i.e. empty
string is *falsy* but not *null*. Symmetry argues the same for `''`.

### B2. One integer-overflow story (fixes A1)

Recommendation: **checked-error everywhere** — it matches the `T^E` philosophy, and
`int` already behaves this way. Sized types get explicit wrapping ops (a
`wrapping_add`-style builtin family) for the image-processing/FFI cases that need
them. Independently: an integer literal that does not fit `int` should be a
**compile error**, never a silent float. (If wrap-by-default on sized types is
retained deliberately for SIMD/typed-array performance, then *say so* in the docs and
in the formal rules — the point is one documented story, not three implicit ones.)

### B3. Specify aliasing per container kind; then unify (fixes A7)

First document reality (arrays/elements reference, objects copy, captures
writable-copy) — that alone removes the trap. Then converge: the cleanest target
consistent with Lambda's functional identity is **value semantics everywhere in
`fn`**, with reference-ness in `pn` made *explicit* (a `ref` type or `&`-style
marker) rather than an artifact of which C struct backs the container. Objects
copying while elements alias is indefensible as a steady state — they are siblings in
the data model.

### B4. `~` is overloaded past its budget (relates to A10)

Pipe current-item, `that`-subject, `match`-subject, object self — four binders, one
sigil. Each reads fine locally; nested combinations have no stated resolution rule.
Minimum fix: document innermost-wins and provide an escape (even just "use an
explicit lambda parameter" as the blessed idiom). Long-term, consider distinct sigils
or optional named binders for pipe vs. self.

### B5. `|` as union and pipe needs a stated disambiguation rule

Union type vs. pipe are distinguished by syntactic context (type position vs.
expression position) — but types are **first-class values**, so `let T = int | string`
is an expression whose `|` must mean union, while `xs | f` is a pipe. The actual rule
(operand static types? syntactic category?) is unwritten. Writing the formal grammar
forces this decision; it should be documented for users too, because
`let U = A | B` where `A`, `B` are variables holding types is legal-looking code
whose meaning currently depends on unstated resolution.

### B6. `[int]` as 1-tuple is a footgun

`int[]` = array of ints; `[int]` = tuple of exactly one int. Users will write `[int]`
meaning "list of ints" (it is the Haskell/Python-annotation spelling). Options: make
`[T]` mean `[T*]` and require explicit arity syntax for fixed-size, or keep semantics
but add a compiler hint when `[T]` appears in annotation position.

### B7. Inference must be unobservable (the gradual guarantee)

Twice now, *inferred* types changed *runtime results* (the pn param float-div
truncation; the JS→MIR numeric-inference bugs). Adopt as a design invariant: erasing
all inferred annotations (falling back to boxed/`any` execution) must never change a
program's observable result — inference buys performance only. Every violation is a
soundness bug by construction. This invariant is directly checkable by a semantics
model (the model has no inference; divergence = violation) and by differential
testing between the boxed interpreter path and the typed JIT path.

### B8. Error-value leaks from builtins (fixes A6, complements A8)

Built-in operations should participate in the same error discipline as user
functions: indexing OOB should either be typed as `T^` at use sites (heavy) or —
more pragmatically — produce `null` uniformly for out-of-range *reads* (matching
string behavior and the query philosophy of "absence is null"), reserving error
values for operations that cannot return (`div 0`, bad coercion). Whichever way:
one behavior, stated, and no silent error values flowing into containers.

---

## Part C — Design decision records

Resolved design questions, recorded **with the full back-and-forth** — the reasoning
and the arguments that failed are as important as the conclusion, both for future
archaeology ("why is it this way?") and because rejected arguments tend to be
re-proposed.

### C1. The `"" ≡ null` decision (2026-07-03) — RESOLVED: `""` is a string

**Decision:** Lambda accepts `""` as a genuine string value. Empty symbol `''` and
empty binary `b''` remain normalized to `null`. A `text` keyword/type may be added as
an alias for `string that (len(~) > 0)` for APIs that want solid strings.
**Deciding argument:** cross-language interop. **Follow-ups deferred:** truthiness of
`""`, literal/`==`/`is` mechanics, fixture migration, empty JSON keys (§C1.7).

#### C1.1 Opening position (review)

Finding A3 established that the identification is applied only to *literals*: source
`""` normalizes to null (`"" == null` true, `"" is string` false), while
runtime-produced empty strings (from JSON input, `split`, `replace`) are genuine
strings — so `d.name == ""` is **false** for an empty JSON field: it equals nothing
writable in source. The formatter meanwhile round-trips `""` vs `null` faithfully.
Initial recommendation (B1): retire the identification; make `""` a falsy string.

#### C1.2 The designer's case for `"" ≡ null`

1. **Universal null.** `null` should be the one absence value for all types —
   otherwise every type needs its own null: a null datetime? a null binary? If a
   "null datetime" is weird, why is a "null string" not equally weird? With
   `"" ≡ null` (and `'' ≡ null`, `b'' ≡ null`), null is a universal null for scalar values
   (NaN excepted).
2. **Leverage built-in null support.** The language already has optional types
   (`T?`), null checks, falsy null — a universal null value rides on all of it.
3. **Ergonomics.** C/C++/Java/JS force checking *both* `null` and `""` — tedious and
   verbose. One absence value means one check.
4. **Document normalization.** In document processing, `<e "">` should normalize to
   `<e>`, and `<e "" "text">` to `<e "text">`. Strings should be *solid* strings
   (with chars); text nodes should be solid text nodes.
5. *(refined in discussion)* **Two-scenario model.** Scenario 1: Lambda-internal —
   the type system should be coherent, simple, clean; `string` means solid string.
   Scenario 2: interop with the outside world, which undeniably has `""` — handle it
   there with a special extended value (e.g. a reserved symbol `'emstr'`), one of
   several extended types Lambda will need anyway (JS `undefined`, etc.).

#### C1.3 The review's counter-case

1. **The identification cannot be uniform.** Data sources deliver genuine empty
   strings. Either every boundary normalizes them (then `{"name": ""}` round-trips as
   `{"name": null}` — data corruption in Lambda's core domain), or the identity
   fractures into the two-empty-strings state of A3. Empirically found during the
   discussion: JSON `{"": 1}` already round-trips as `{"''": 1}` — real corruption
   from the normalization family.
2. **Sequence-like vs. point-like** (the answer to the symmetry argument). `string`,
   `binary`, and sliceable `symbol` are *sequences*; sequences have a natural empty
   case — the identity of concatenation, the base case of induction, the result of
   `s[2 to 1]`. `""` is not string's *null*; it is string's *zero*, as `0` is int's
   and `[]` is array's. `datetime`/`int`/`bool` are *point-like*: they have no empty,
   which is why `t''` is absurd — not because empty ≡ absent. The scalar/container
   line cuts through the middle of the sequence family; Lambda already keeps the
   zeros of `[]` and `{}` (both `!= null`) and treats `0` as truthy — strings were
   the sole exception. `b''` is the sharpest case: zero-length byte sequences arrive
   from the world (empty files, empty HTTP bodies), and `b'' ≡ null` conflates "empty
   data" with "no data". (NaN, conversely, is the cautionary tale of an in-band
   no-value living *inside* a type: `nan == nan` false poisons the algebra.)
3. **Null-vs-empty is a distinction optional types exist to express.**
   `name: string?` should have three states: absent, present-empty, present-non-empty
   (HTML `alt=""` vs missing `alt` is accessibility-meaningful; SQL `''` vs `NULL`;
   form submitted-empty vs not-submitted). The identification makes the middle state
   unrepresentable — it fights the optional-type machinery rather than leveraging it.
4. **Ergonomics achievable without the axiom.** (a) Non-nullable types kill most
   checks at the root: a param typed `string` (not `string?`) can never be null — the
   Java double-check pain stems from universal nullability, already fixed in Lambda.
   (b) Falsiness gives the one-check idiom: make `""` falsy and `if (name)`,
   `name or "default"` work — Python is the existence proof (`""` falsy yet
   `"" != None`, famously ergonomic).
5. **Precedent.** Oracle SQL's `'' = NULL` is the one large-scale precedent for the
   identification — universally regarded as Oracle's most notorious wart, contradicts
   ANSI SQL, adopted by no other database, unfixable for compat reasons.
6. **Document normalization conceded — but as a tree rule, not an equality rule.**
   The XQuery/XPath Data Model is the precedent: *no empty text nodes in trees*
   (normalized away at construction), yet `""` remains an atomic value distinct from
   the empty sequence `()`. "Text nodes are solid" and "`""` is a string" are
   compatible.
7. **The `'emstr'` escape hatch inverts the burden.** Recursion problem: `'emstr'` is
   a symbol, so `x is string` is false for a JS empty string — the classification
   problem recurs one level up; giving it a distinct string-like TypeId is an empty
   string with extra steps (and the runtime already has the representation: `String`
   with `len 0`). Scale problem: this is not future interop — LambdaJS ships today
   and JS traffics in `""` ubiquitously; Python/Ruby/Bash likewise; JSON/XML/CSV/SQL
   boundaries all need the shim. When 100% of boundaries need the escape hatch, the
   escape hatch is the design. (JS `undefined` is different: genuinely foreign,
   deserving an extended value. `""` is the zero of a type Lambda already has.)
8. **The erosion problem.** If `string` excludes empty, `split`/`slice`/`replace`
   must return `string?` — then every intermediate result of string processing is
   nullable and the null-check tedium returns *inside* Lambda; or the ops absorb null
   as a concat-identity/len-0 value (the current behavior: `"a" ++ null` → `"a"`,
   `len(null)` → 0) — in which case null operationally *is* an empty string under a
   misleading name, same algebra, minus round-trip fidelity.

#### C1.4 What did *not* persuade

The closing challenge — "what should `split("a,,b", ",")` return?" — did **not**
move the designer: under the solid-string model, `["a", null, "b"]` is perfectly
natural, and string functions returning null is accepted and consistent. Recorded
because it will be re-raised: within Scenario 1 the model is internally coherent;
the case against it does not rest on internal incoherence.

#### C1.5 What did persuade

**Interop.** JS, Python, and the other languages Lambda hosts and models all accept
`""` as a string. If Lambda string values are to pass between languages with little
or no hassle — LambdaJS today, the guest-language semantic models next, plus every
data boundary — accepting `""` as a string is effectively the only workable design
(C1.3 points 1 and 7).

#### C1.6 The decision

- `""` **is a string**: a genuine value of type `string` with `len 0`.
- **Symbols and binaries keep normalization**: `'' ≡ null` and `b'' ≡ null` stand.
  Rationale: symbols are identifiers — point-like in use (the empty name names
  nothing) even if sliceable in implementation; the review conceded this carve-out,
  noting the empty-JSON-key corner (§C1.7) needs an answer.
- **`text` type under consideration**: `type text = string that (len(~) > 0)` — the
  solid-string discipline recovered as a *visible refinement in signatures* ("parse,
  don't validate") rather than an ambient equality axiom. Lambda-native APIs (tag
  names, field names, document text) can require `text`.
- **Element normalization stays**: `<e "">` → `<e>` as a tree-construction rule
  (the XDM position), independent of string equality.

#### C1.6a Amendment (designer, 2026-07-04): the `''` literal is removed

The original ruling kept `''` (and `b''`) as literals *normalizing to null*.
Amended: **the literal spellings are removed — writing `''` or `b''` is a
compile error** (teaching hint: "empty symbol does not exist; use `null`").
Rationale: with `""` now a genuine string, `''` silently meaning null is a
confusion trap — a visual near-twin of `""` with opposite semantics. Banning
the spelling makes the solid-type invariant (`symbol`/`binary` have `len ≥ 1`)
syntactically visible rather than silently enforced. The *semantic* invariant
is unchanged: zero-length symbols/binaries still cannot exist; runtime
producers (slices, conversions) still yield null. `b''` included for symmetry
(review recommendation, same rationale). Implementation: impl plan item 3.3.

#### C1.7 Deferred follow-ups

1. **Truthiness of `""`** — review recommends falsy (preserves the one-check
   ergonomics that motivated the original design); to be decided.
2. Literal/operator mechanics: `""` literal denotes the empty string; `"" == null` →
   false; `"" is string` → true; `match "" { case string }` matches. Implementation
   touches parse-time literal normalization and `==`/`is`/match paths.
3. Fixture and stdlib migration: audit for reliance on `"" == null` (note: code
   comparing *data-derived* empties to `""` or `null` is already broken today, so
   part of the migration is bug-fixing).
4. Empty JSON keys: `{"": 1}` currently round-trips as `{"''": 1}` (corruption);
   with `'' ≡ null` retained for symbols, map keys from data need a defined answer
   (e.g. string keys for non-identifier keys).
5. Whether `b''` merits revisiting separately once file/network I/O semantics are
   formalized (empty file vs. missing file).
6. Doc updates: `Lambda_Data.md`, `Lambda_Type.md`, `Lambda_Cheatsheet.md` all state
   `"" == null` is true.

### C2. Truthiness of literals (2026-07-03) — RESOLVED: falsy = {null, false, error, ""}

**Decision (designer ruling):**

1. **All numbers are truthy** — including `0` and `nan`. The C family treating 0 as
   false is legacy (C had no bool; `int` was bool). Truthy-0 also keeps `or` a safe
   coalescing operator.
2. **`""` is falsy** — it is an *empty scalar*.
3. **There is no `b''` under Lambda** — stated as a type invariant: every `binary`
   value has `len ≥ 1`; `b''` and any zero-byte result normalize to `null`. (Same
   invariant holds for `symbol` per C1.)
4. **`[]` and `{}` are truthy** — *all containers are truthy*. An empty container is
   empty, but is not nothing: "an empty box is empty, but the box itself is not
   nothing." Being empty just means having no content; the container itself exists.
   Supporting examples: `[[]]` is a 2D array, `[null]` is a 1D array with one
   element, `[""]` is a 1D array — showing `[]` is fundamentally different from
   `null` and `""`.

So the complete falsy set is **`null`, `false`, `error`, `""`** — exactly one *data*
value in the whole language is falsy. Elements and ranges, as containers, are always
truthy (`if (html?<img>)` is always true — see follow-ups).

#### C2.1 The discussion

**Context.** With `""` accepted as a string (C1) and intended falsy, the question was
whether `0` should follow (C/JS/Python camp) or stay truthy (Ruby/Lua/Lisp camp).
Empirical baseline: current falsy set was `{null, false, error, ""-as-null}`; `0`,
`0.0`, `nan`, `[]`, `{}`, `<br>` all truthy; `0 or 99` → `0` (no clobber).

**Review's recommendation: 0 stays truthy** (adopted). Arguments:

- *Coherence with C1:* `""` is falsy because **empty** (`len == 0`), not because
  zero-like; `0` is not empty — it is a definite point value. The sequence/point
  taxonomy from C1 decides both questions consistently.
- *The `or` economy:* with truthy-0, `config.port or 8080` never clobbers a
  legitimate 0. JS's falsy-0 + `||` bug class forced JS to add `??` (nullish
  coalescing) as an admission of the mistake; Python has the trap with no fix.
  Truthy-0 means Lambda never needs a second coalescing operator.
- *Domain fit:* in a data-processing language, 0 is among the most common legitimate
  values (counts, indices, offsets, deltas); falsy-0 is the worst-fit convention.
- *Interop does not transfer from C1:* truthiness is behavior, not data — it never
  crosses a language boundary. JS's `ToBoolean` lives in the JS engine's/model's own
  rules. The argument that decided C1 is absent here.
- *Heritage:* falsy-0 descends from C's boolless int; real-bool languages chose
  truthy-0 (Ruby, Lua, Lisp/Clojure, Erlang). The Redex project had already recorded
  "0 is truthy" as a resolved designer ruling.
- *NaN truthy* (adopted): though Lambda treats failure as falsy and `nan` is float's
  in-band failure, falsy-nan would let `or` **silently rescue poisoned
  computations**; NaN should propagate loudly, with `is nan` as the explicit test.
  Bonus guarantee: "no number is ever falsy."

**Where the review was overruled — `[]`/`{}`.** The review leaned Python-style
(empty collections falsy), citing the emptiness principle, `if (matches)` ergonomics,
and Lambda's own empty-for→spreadable-null / `<e "">`→`<e>` precedents. The designer
ruled containers always truthy via the empty-box argument (above). On reflection the
ruling is the stronger position, for two reasons the review conceded:

- It preserves `or`-safety *fully*: `or` never clobbers a legitimate value of any
  type except `""` (where empty→default is almost always intended). The review's
  version reintroduced for collections the same clobber it used to defend truthy-0.
- It is more consistent than Python itself, which makes `[]` falsy but arbitrary
  empty user objects truthy — an inconsistency at the value/object line. The adopted
  system is essentially **JavaScript's object rule (objects never falsy) with the
  C-legacy number mistakes removed** — a spot no mainstream language occupies.

#### C2.2 Reconciling C1 and C2: the 2×2 model

The two discussions rest on the same insight — **emptiness ≠ nothingness** ("an empty
box is not nothing" is C1's "an empty string is not null", one level down; `[""]`
having one element proves both at once). Two orthogonal axes classify every type:

| | **value** (content-only identity) | **container** (a thing with identity — containers alias, A7) |
|---|---|---|
| **has an empty member** | `string`: `""` exists; empty content ⇒ **falsy** | `array`, `map`, `element`, `range`: empty exists; the box itself is something ⇒ **truthy** |
| **point-like (no empty member)** | `int`, `float`, `bool`, `datetime`: always truthy (`0`, `nan` included) | — |
| **empty member removed by fiat** | `symbol`, `binary`: `''`/`b''` ⇒ null (C1/C2.3); every member solid ⇒ always truthy | |

Formally: `truthy(v) = false  iff  v = null ∨ v = false ∨ v is error ∨ (v is string ∧ len(v) = 0)` —
a total function over the value domain, to be encoded as a judgment in the formal
model.

#### C2.3 Follow-ups

1. **Document the emptiness idiom prominently**: `if (results)` does NOT ask "any
   results?" — containers are always truthy; the blessed idiom is
   `len(results) > 0`. This is the #1 footgun for JS/Python-comers, especially with
   query results (`if (html?<img>)` is always true). Consider a lint hint:
   "condition is a container — always true; did you mean `len(...) > 0`?".
2. **State the solid-type invariants**: `binary` and `symbol` have `len ≥ 1` by
   construction; zero-byte/zero-char results (zero-length slices, conversions) yield
   `null`. The C1 "solid values" vision survives in these two types.
   **Empty files (resolved, designer ruling):** an empty file is modeled as a
   `<file>` *object with empty/null content* — a childless `<file>` element — not as
   an empty binary. The C2 box model does the work: the file object is the box
   (exists, truthy), the binary is the content; "empty file" (`<file>` with no
   content) and "missing file" (error) stay distinguishable, so solid `binary` costs
   nothing. Note the C1/C2 normalization rules compose to give this for free: text
   content `""` vanishes by `<e "">` → `<e>`, and `b''` → null, so empty text and
   binary files uniformly yield a childless `<file>`. To pin down with file-I/O
   specification: the `<file>` object shape (name/size/mime attributes, content as
   child).
3. Update docs (`Lambda_Expr_Stam.md`, error-handling truthiness section,
   cheatsheet) with the complete falsy set and the total `truthy()` definition;
   update the Redex model's truthiness accordingly (it already has 0-truthy).
4. Implementation delta vs. current behavior is minimal: only `""` changes (from
   falsy-because-null to falsy-as-string, pending C1's literal mechanics); `0`,
   `nan`, `[]`, `{}`, elements already behave as ruled.

### C3. Integer overflow and the numeric model (2026-07-03) — RESOLVED: 53-bit symmetric `int` promoting to float; Go-aligned machine tier

**Decision (designer ruling):**

- **Two-tier numeric model.** The flex tier is `int` alone; the machine tier is the
  sized types `i8`…`i64`, `u8`…`u64`.
- **`int` becomes 53-bit symmetric**: range **±(2⁵³ − 1)** (= JS
  `MAX_SAFE_INTEGER`), so every `int` is exactly representable as a `float64`.
  On overflow, `int` arithmetic **auto-promotes to float** (correctly-rounded
  float64 result). Types are sticky: once float, stays float; no auto-demotion.
- **Machine tier is Go-aligned**: runtime overflow wraps (two's complement, all
  ops); constant/literal overflow is a **compile error** (`let x: i8 = 200i8`
  rejected); division by zero → error; `MinInt / -1` wraps to `MinInt` (Go spec
  behavior, stated explicitly since C makes it UB).
- **Literal strictness**: an unsuffixed integer literal that exceeds the `int`
  range is a **compile error** — never a silent conversion. The programmer writes
  `...i64`, `...n` (decimal), or an explicit float literal.
- **Data takes the smallest exact home**: input parsers (JSON/CSV/…) cannot reject
  data, so integer tokens land in `int`, else `int64`, else `decimal` — always
  exact. (Large database IDs — the Twitter-snowflake class — survive intact,
  better than JS itself manages.)

#### C3.1 Current implementation behavior (probed — the bug catalog)

All verified against master `lambda.exe`:

| Probe | Result | Status under the ruling |
|---|---|---|
| `36028797018963967` (2⁵⁵−1) | fits | range shrinks to ±(2⁵³−1); values in (2⁵³, 2⁵⁵) need migration audit |
| `36028797018963967 + 1` | `error` (unchecked error value, flows silently — A6 family) | becomes float promotion |
| `3000000000 * 3000000000` | `error` | becomes float promotion |
| `127i8 + 1i8` → `-128`, `255u8 + 1u8` → `0` | silent wrap | **correct** — now blessed as the Go tier |
| `9223372036854775807` (literal) | **`9.22337e+18`** — silent float at parse time, precision lost | becomes a compile error (literal strictness) |
| `9000000000000000000 + 9000000000000000000` | `1.8e+19` — both operands corrupted *before* the `+` ran | fixed by literal strictness |
| `36028797018963966 + 0.5` | `3.60288e+16` — non-overflow int/float mixing silently loses precision (int in (2⁵³, 2⁵⁵) exceeds float mantissa) | **impossible by construction** once int ⊆ float64 |
| `2.0**53 == 2.0**53 + 1` | `true` | inherent float behavior past 2⁵³ — documented, not fixed |
| `9007199254740992 + 1` | `9007199254740993` **exact** | shows current int is *more* precise than float in [2⁵³, 2⁵⁵] — the range being given up |
| `type(3000000000 * 3000000000)` | `error` — `type()` propagates error values (Redex disagreement #5) | separate fix |
| `1/0` → `inf`, `1 div 0` → `error` (A2) | inconsistent failure modes, undocumented | A2 documentation still pending |

Summary of the pre-existing trichotomy (A1): `int` overflow → error value;
sized types → silent wrap; oversized literal → silent float. Three regimes, none
documented; the ruling reduces it to two *stated* regimes (promote / wrap) plus
compile-time strictness.

#### C3.2 The discussion arc (three rounds)

**Round 1 — checked errors (review's original B2, retracted).** Checked-error
everywhere matches the `T^E` philosophy, but is hostile ergonomics for a scripting
language: every `+` becomes raise-able, poisoning ordinary arithmetic. The current
state (unchecked error *values* flowing from overflow) was the worst of all worlds.
The designer's auto-promotion instinct was accepted as the right direction.

**Round 2 — promote to decimal (review's counter-proposal, ultimately not
adopted).** The case for decimal128 as the promotion target, recorded in full since
it may be re-proposed:

- float64 has a 53-bit mantissa vs (then) 56-bit int: every overflow result is
  ≥ 2⁵⁵ where float spacing (ulp) is 8 — promoted values snap to multiples of 8,
  and `(2⁵⁵−2)+2 == (2⁵⁵−2)+3` collides immediately at the boundary.
- decimal128 carries 34 significant digits ≈ 113 bits, and
  `int56 × int56 < 2¹¹⁰ ≈ 1.3×10³³ < 10³⁴` — so **every single-operation overflow
  result fits exactly**. Promotion would be lossless, always.
- Precedent: Scheme/Python promote exact→exact; you enter float only by explicit
  choice. JS's all-numbers-are-float needed the BigInt retrofit — the cautionary
  tale. Slogan: "integers get bigger, never blurrier."

**The designer's objections to decimal** (had tried this design before):

1. **The lattice problem**: float literals like `0.5` are `float`; with int→decimal
   promotion, int-mixed-with-float arithmetic becomes cumbersome in both the type
   system and the implementation (what is `decimal + 0.5`, at what precision? —
   Scheme needed its whole exact/inexact tower to manage this).
2. **Performance**: decimal cannot compete with hardware doubles; post-promotion
   chained arithmetic stays at decimal speed — a real cliff for hot loops.
3. Initial stance on float precision loss: "let it be — people must bear float
   precision loss anyway."

**Round 3 — the 53-bit alignment (designer's second thought, adopted).** Shrink
`int` from 56 to 53 bits to align with the float mantissa. The review endorsed this
over its own decimal proposal for a decisive reason: **decimal promotion never fixed
the mixing path.** Even with decimal, non-overflow expressions like
`36028797018963966 + 0.5` still silently corrupted the int operand (ints in
(2⁵³, 2⁵⁵) exceed the mantissa). The 53-bit alignment fixes conversion *and* mixing
*and* overflow in one stroke: **`int` is exactly the integer subset of `float64`** —
one numeric continuum; no value is ever changed by conversion, only by genuine,
documented float rounding.

Additional points in favor, recorded:

- **JIT / gradual-guarantee bonus (B7)**: with int ⊂ float64, the JIT can hold
  int-typed values in double registers and convert freely with zero observable
  difference — making inference-unobservability structurally cheap. The historic
  JS→MIR numeric-inference bug class existed precisely because int56 and double were
  not interchangeable; this removes the trap at the representation level.
- **Intuition transfer**: ±(2⁵³−1) is exactly JS's safe-integer boundary; JS
  engines' SMI/HeapNumber model lives on the same value space. Lambda adopts the
  coherent core of the JS number model while keeping what JS lacks — a real `int`
  type below the boundary and machine ints beside it.
- **Symmetric range deletes an edge-case family**: with ±(2⁵³−1) there is no MinInt
  in the flex tier — negation and `abs` are total; the `MinInt/-1` anomaly exists
  only in the machine tier, where Go's rule covers it.

**Costs accepted deliberately** (recorded so they are not relitigated):

- Overflow promotion is *approximately* correct, not exact: `(2⁵³−1) + 2` promotes
  to `2⁵³+1`, which rounds to `2⁵³` — so `x+1 == x+2` collisions begin immediately
  past the boundary for odd results. This is the trade against decimal's exactness,
  made for continuum coherence, hardware performance, and lattice simplicity.
  Precision-critical big-integer work opts into `i64`/`u64` or `decimal` explicitly.
- Sticky types are observable: `type()` distinguishes an `int` from an equal
  promoted `float` (JS hides this only by having a single number type).
- The range shrinks: current ints in (2⁵³, 2⁵⁵) — where `int` today is more precise
  than `float` — lose exact representation. Anything relying on that range is
  arguably already a latent mixing bug; still, fixtures and stdlib need an audit.

#### C3.3 Follow-ups

1. Implement: range change, promotion on overflow (replacing error values), literal
   strictness (both tiers), machine-tier constant checks, smallest-exact-home in
   input parsers.
2. Document the boundary: expose `math.max_int` (= 2⁵³−1) or similar; document that
   `int` arithmetic enters float past it; document A2's `/`-vs-`div` failure modes
   while in the area (still pending).
3. Migration audit: fixtures/stdlib for values in (2⁵³, 2⁵⁵).
4. Separate fix, surfaced by probing: `type(error_value)` propagates the error
   instead of returning a type (Redex disagreement #5).
5. Formal model (Stage 4): the flex-tier rule is
   `n₁ op n₂ → int(r) if |r| ≤ 2⁵³−1, else float64(r)`; machine tier is
   `mod 2ⁿ` — both now clean one-line rules, which
   was the point of forcing the decision.

#### C3.4 Designer addenda (for the record)

1. **Auto-promotion is a safety net, not a precision guarantee.** If a user wants
   precise integer arithmetic (beyond the `int` range), they should **start with
   `decimal` in the first place** (`n`-suffix literals). Depending on auto-promotion
   for precision is unreliable — past the boundary the arithmetic is float, with
   float's rounding. This is the blessed idiom to document alongside `math.max_int`.
2. **Implementation history of `int`'s width.** For a period, Lambda internally
   mapped `int` to **int32** — simple, but wasteful of the extra tag-payload bits —
   and was later widened to **int56** (the full tagged-pointer payload). This ruling
   narrows it to **53-bit symmetric**, trading 3 payload bits for exact float64
   alignment. Recorded so the width's trajectory (32 → 56 → 53) and the reasoning at
   each step aren't lost.

### C4. Aliasing and mutability: let/var/closure model (2026-07-04) — RESOLVED: mutable value semantics

**Decision (designer-approved in full): containers are values; mutability is a
property of bindings, not values; `var` is its only marker.**

1. **`let` is final.** Nothing reachable through a `let` binding ever changes —
   not by reassignment, not by mutation through any other binding, not by a method.
2. **Bindings and assignment copy, observably.** `var b = a` gives `b` an
   independent value regardless of whether `a` is `let` or `var`; the same holds for
   every binding form. Applies uniformly to arrays, maps, elements, and objects.
   *Implementation:* copy-on-write on the existing `ref_cnt` fields — share until
   first mutation, copy at the mutation point iff `ref_cnt > 1` (refcount-based, not
   binding-kind-based, so it covers values reached via fields/elements/captures with
   one mechanism). **COW must be unobservable** — a verifiable property in the
   formal model, sibling of boxing-invisibility.
3. **`var` parameters are the sole sharing construct**: `pn f(var a: T)` — the
   designer's syntax, chosen over a new `mut` keyword (no new keyword; Pascal's
   `procedure f(var x: T)` precedent; `var` becomes the single mutability marker:
   locally a mutable binding, in signatures a mutable/inout parameter). Sub-rules:
   - arguments to a `var` param must themselves be `var` — a `let` binding, literal,
     or temporary is a compile error (else `let`-finality re-breaks at the call
     boundary; Pascal lvalue rule, Swift `&` rule);
   - **exclusivity**: passing the same `var` (or overlapping paths, e.g. `x` and
     `x.field`) to two `var` params is a compile error (else aliasing returns inside
     the callee; Swift exclusive-access precedent);
   - `pn` methods mutate the receiver, so the **receiver must be `var`** —
     `frozen.increment()` on a `let` binding is a compile error;
   - `var` params pass as borrows (no refcount bump, no copy) — preserving the
     in-place hot path for `push`/`splice` and the awfy benchmarks, which gain
     honest `var` annotations in their signatures.
4. **Closures are immutable values.** Capture copies at creation (snapshot —
   verified working today). Assignment to a captured name is a **compile error**
   with a teaching message ("captures are immutable — use an object with a `pn`
   method, or a `var` parameter"). Plain (non-`var`) parameters are likewise
   `let`-like: not assignable.
5. **No reference cells.** Shared mutable state lives in module-level / view-instance
   `var` (the Elm/React shape Lambda's `edit` views already have). An explicit `ref`
   type is deferred until a real need appears.
6. **Structural `==` remains the only equality.** Values have no identity, so no
   `===`/identity operator ever needs to exist.

Model in two sentences: *values never alias; `var` is the only mutability marker —
locally a mutable binding, in signatures the language's single, explicit sharing
point. `pn` is thereby locally imperative but observably functional.*

#### C4.1 Current behavior (probed — the bug catalog)

The starting point was worse than finding A7 originally recorded (see A7 REVISED):

| Probe | Result | Verdict |
|---|---|---|
| `let g = [4,5,6]; var h = g; h[0] = 77` | `g[0] == 77` | **`let`-bound values are mutable through a `var` alias** — the canonical violation; becomes a regression fixture |
| `var a = [1,2,3]; var b = a; b[0] = 99` | `a[0] == 99` | var–var aliases too |
| `var c = <Counter step:1>; let d = c; c.increment()` | `c.value == 1, d.value == 1` | objects alias as well — A7's "objects copy" was an artifact of the bugs below |
| `var c = <Counter>` (all defaults); `c.increment()` | `c.value` stays 0 | default-constructed objects: pn-method mutation silently no-ops |
| after aliases exist, second `c.increment()` | lost (`c.value` stays 1) | mutation reliability depends on sharing state |
| `pn add(n: int) {...}` method | fails to parse | pn methods with parameters unsupported |
| `frozen.increment()` on `let` receiver | no error, no effect | silent no-op (C4 target: compile error) |
| assignment to capture in `fn` closure | compile error **that keeps executing**, assignment dropped | silent-continue family (A8) |
| capture mutation in nested `pn` | runtime crash `unknown add type: 0, 5` | broken codegen |
| `var x=1; let f=()=>x; x=2; f()` | `1` | capture snapshots at creation — correct, kept |

So current reality: uniform reference aliasing (all container kinds), unreliable
object mutation, no working capture mutation, and the docs' "Mutable Captures in
Procedural Closures" section (writable per-closure copy, counter → 1, 2) describing
behavior that never shipped (A10 aspirational-docs family). There was no working
behavior to preserve on the closure front, and the "three regimes" of the original
A7 were really one consistent-but-wrong behavior plus a bug cluster.

#### C4.2 The discussion arc

**Round 1 — review's recommendation: mutable value semantics.** One rule
("assignment copies, observably; mutation never visible through another binding"),
COW on the existing refcounts, an inout-style `mut` parameter for caller-visible
mutation, no reference cells, structural equality only. Why it fits Lambda: the
language was already ~90% there (pure `fn`; captures snapshot; copy-with-override
idioms `{*:base}` / `<Point *:p, x:v>` as the native "modify"); the formal model
needs a store **only for `var` bindings** — the single largest Stage-4
simplification available; and it matches the "pure functional scripting language"
identity, making `pn` locally imperative but observably functional. Precedents:
Swift (COW arrays/dicts/structs, `inout`), Koka/Perceus (functional-but-in-place),
Hylo (mutable value semantics as the whole language), R/Matlab (copy-on-modify runs
the world's data science — Lambda's domain). The alternative — uniform reference
semantics (Python/JS model) — was presented and rejected: trivial to implement and
familiar, but buys the aliasing-bug class forever, `let` protecting the binding but
never the contents, a store-everywhere formal model, an eventual identity-vs-equality
operator split, and a functional language that isn't.

**Round 2 — designer refinements (all adopted):**

1. `let` is final, always — elevated to the anchor rule.
2. `var b = a` where `a` is `let`-bound must copy before mutation ("if a is static,
   b should make a copy before assignment"). Review refined the trigger from
   binding-kind to **refcount at the mutation point** (handles all sharing shapes
   with one mechanism) and extended the rule to `var`-sourced assignment (else a
   two-regime system survives in miniature — and the probe showed var–var aliasing
   is equally broken today).
3. **`pn f(var a: T)` instead of `mut`** — designer's syntax, endorsed by review as
   strictly better: no new keyword, Pascal precedent, and `var` becomes the single
   mutability marker in the language.

**Round 3 — the closure question (designer): "if captures are copies, can they be
mutated?"** Resolution: no — compile error. The mutable-capture option (docs'
claimed per-closure writable copy) fails on rule 1 itself: a stateful `counter()`
returning 1, 2, 3 is a `let`-bound value changing observable behavior per call, and
closures acquire identity (the counter paradox: does `let c2 = counter` share or
fork the count? — both answers are bad). The per-call-copy option is value-semantics
legal but a confusion trap (the intuitive counter returns 1, 1, 1 silently).
Immutable captures match Java (effectively-final) and C++ (const by-value captures),
and the probes showed no working behavior was being given up. Idioms for state:
object with `pn` methods in a `var`, module-level `var`, view `state`, `var` local
inside the closure body, `var` params.

**Round 4 — the JS question (designer): "JS closures are mutable and used as
objects — how to model that if Lambda closures are immutable?"** Resolution: the
meta-/object-language distinction. A JS closure is never modeled *by* a Lambda
closure; it is modeled *as data* — an immutable term
`<jsclosure params; body, envaddr>` plus environment records in the model's
`<store>` cell. Mutability is a
property of the *configuration*, never of meta-language values; two JS closures
sharing an environment record share mutations through store-rewrite rules, and full
JS semantics (including `var`-in-loop capture bugs) falls out with no Lambda value
ever mutating. Meta-language immutability is *required*, not merely tolerable: rule
matching, COW, and derivation replay are sound only over immutable terms — which is
why K, Redex, and Coq are all immutable meta-languages happily modeling mutable
heaps. The LambdaJS engine is unaffected (its JS scopes are C++-internal), and the
Lambda-native replacement for the closure-as-object pattern is the object type with
`pn` methods — state declared as typed fields, mutation root visible as a `var`
binding, rather than smuggled into an invisible environment record. Working
example, verified (modulo the C4.1 bugs):

```lambda
type Counter {
    value: int = 0,
    step: int = 1;
    pn increment() { value = value + step }
    fn double() => value * 2
}
// var c = <Counter step: 1>;  c.increment()
```

#### C4.2a Clarification: nested `pn` and interior mutation through captures

Designer follow-up: is this allowed?

```lambda
pn a() {
    var b = [...]
    pn c() { b[0] = 123 }   // ?
}
```

**No — compile error.** The capture rule forbids not just rebinding (`b = ...`) but
*interior mutation through* a captured name (`b[0] = ...`, `b.f = ...`): a capture
is a `let`-like snapshot. Allowing it would either mutate `c`'s private copy
(silently useless) or open a hidden sharing channel into `a`'s `b` that bypasses the
`var`-param rule and detonates the moment `c` escapes. The blessed rewrite is
mechanical: `pn c(var arr: int[]) { arr[0] = 123 }; c(b)` — the mutation becomes
visible in the signature, preserving "`var` params are the only sharing construct."

Two notes recorded with the ruling:

- **Pascal-style relaxation deferred, not rejected — and agreed as needed**
  (designer): the canonical use case is a **closure-style parser** — one big
  function holding local `var` state (`pos`, `tokens`, `errors`) and many nested,
  *mutually recursive* `pn` helpers (`peek`/`advance`/`expect`/`parse_expr`/…) that
  read and mutate it. Under the strict rule, every helper threads the same
  `var` params through every signature and internal call site — explicitness that
  documents mutation at module boundaries becomes pure ceremony inside what is
  conceptually one function.
  **Spec sketch for the future relaxation**: a non-escaping nested `pn` is *not a
  value* — no closure is created; it is a named, parameterized block whose
  references to enclosing `var`s are direct up-level accesses into the enclosing
  activation (valid because it provably cannot outlive it; observationally
  equivalent to inlining). Escape check is syntactic: the `pn`'s name may appear
  **only in call position** — mutual recursion qualifies (calls by name); binding,
  passing, or returning the name is a compile error for any nested `pn` that
  mutates enclosing `var`s. Direct access also means such blocks see *current*
  values (no snapshot-timing surprise). The two-regime story stays clean:
  *closure = value = immutable captures; non-escaping nested `pn` = named block =
  direct access* — the strict rule governs function values, and these blocks never
  were values. Start strict; add the relaxation later (backward-compatible; the
  reverse would be breaking).
  Interim idiom for the parser case: the object form —
  `type Parser { src: string, pos: int; pn advance() ... }` with the helpers as
  pn methods — typed state, no threading, instantiable; currently blocked by the
  C4.1 bug that pn methods with parameters fail to parse, making that fix a
  prerequisite.
- **Snapshot timing**: captures snapshot at closure *creation* — a nested `pn` that
  merely reads `b` (as a closure, under the strict rule) sees `b` as of its
  declaration point, not the call. Helpers needing current values take parameters —
  or, once the relaxation lands, non-escaping blocks see current values directly.

#### C4.3 Costs accepted (recorded to avoid relitigation)

- **Migration**: `pn` code mutating through un-annotated params or `let`/`var`
  aliases breaks and needs `var` annotations or restructuring; the awfy benchmarks
  need signature updates; the editor/Radiant bridge needs an audit for element
  aliasing it may rely on.
- **COW perf cliffs**: a container with `ref_cnt > 1` copies on first mutation;
  accidental copies in hot loops are a real hazard (Swift lives with this).
  Mitigations: `var`-param borrows (no refcount bump), JIT uniqueness tracking, and
  a possible lint ("mutation of shared container copies — consider a `var` param").
  **Elements are the worst case** (whole-document copies) — editor workloads should
  be benchmarked before this ships.
- Users coming from JS/Python must learn that mutation does not travel — offset by
  it being the *point* of the model, and by `var` params making every place it does
  travel visible in a signature.

#### C4.4 Follow-ups

1. Implement: COW at mutation points (refcount-triggered); `var`-param grammar and
   the three compile checks (var-args-only, exclusivity, `var` receiver for `pn`
   methods); compile error for capture assignment (both `fn` and `pn` closures,
   replacing today's silent-continue and crash).
2. Fix the C4.1 object-mutation bug cluster (default-instance no-op, lost second
   mutation, pn methods with parameters not parsing) — needed under any semantics.
3. Migration audit: fixtures/stdlib for aliasing reliance; benchmark signatures;
   editor/Radiant element paths; element-COW performance benchmark.
4. Docs: delete/rewrite `Lambda_Func.md` § Mutable Captures (aspirational); write
   the aliasing/mutability section (this model); document the state idioms.
5. Formal model (Stage 4): store only for `var` bindings; **`let`-finality and
   COW-unobservability become verifiable properties**; the `let g/var h` probe
   becomes a regression fixture.

---

## Triage summary

| ID | Finding | Suggested disposition |
|----|---------|----------------------|
| A1 | Overflow trichotomy | **RESOLVED** (§C3): 53-bit symmetric `int` → float promotion; Go-aligned machine tier; literal strictness; smallest-exact-home for data |
| A2 | div/mod conventions | **Document** (behavior itself defensible); machine-tier corners (div-by-zero, MinInt/-1) now ruled in §C3 |
| A3 | `"" ≡ null` leaks | **RESOLVED** (§C1): `""` is a string; `''`/`b''` stay null; `text` type considered; follow-ups in §C1.7 |
| A4 | `'a' == "a"` raises | **Fix or re-document** — impl and docs must agree |
| A5 | `==` non-reflexive / repr-sensitive | Document NaN; **fix ArrayNum ==** (task_38782787) |
| A6 | OOB trichotomy, silent error values | **Decide & fix** (B8) |
| A7 | Aliasing (revised: uniform reference + broken object mutation) | **RESOLVED** (§C4): mutable value semantics — `let` final, bindings copy (COW), `var` params as sole sharing, closures immutable |
| A8 | Covariance log-only failure | **Fix failure mode**; decide assignment-subtyping rule |
| A9 | REPL rollback replay | **Bug fix** (tooling) |
| A10 | Spec gaps (generics, `as`, open maps, match order, `~`) | **Document**; delete aspirational generics text or implement |
| B1–B8 | Design recommendations | Discussion — this document's purpose. B1 resolved via §C1 (adopted for strings, rejected for symbols/binary). B2 superseded by §C3 (checked-error retracted; promotion + Go tier adopted). B3 resolved via §C4 (value semantics adopted; `var` params instead of `mut`) |

Every "decide" row above is a decision that formal rules (the Redex model of
[Lambda_Semantics.md](Lambda_Semantics.md), or the proposed native DSL) will force
anyway. Resolving them first means the formal model describes the language you
*want*, not the accidents you have.

---

## Summary — Overall Assessment (2026-07-04)

With C1–C4 decided, this document concludes on the core principles and designs.
Remaining open items (see below) continue in a follow-up document,
**Lambda_Semantics_Formal2.md**. The review's overall assessment:

### The language now has a theory of itself

The review began expecting a pleasant data model with fuzzy semantics — which the
A-findings confirmed. What exists after C1–C4 is rarer: a scripting language with a
small set of axioms that *answer questions on their own*. The four decisions didn't
accumulate — they compounded into principles:

- **Emptiness ≠ nothingness** (C1's box argument) — decided `""`, then decided
  truthiness, then settled the empty-file question *without new deliberation*:
  `<file>` with no content fell straight out of the model.
- **Values vs. containers** — one 2×2 that settled truthiness, and whose "value"
  column then anchored C4's value semantics.
- **Exact until you ask for float** — the numeric tier split, literal strictness,
  smallest-exact-home: one story from literals through data ingestion to overflow.
- **Mutation is visible or it doesn't exist** — `let` finality, `var` as the sole
  marker, sharing only in signatures, nothing mutable inside a value. The closure
  ruling, the nested-`pn` ruling, and the JS-modeling answer were all *derived* from
  this, not decided fresh.

That is the test of a designed language versus an accreted one: when a new question
arrives, do the existing principles answer it? Lambda now passes that test
repeatedly.

### Where it sits

The composite occupies a genuinely unoccupied point in the design space. No
mainstream *scripting* language has value semantics (Swift has it but is
compiled/OO; R has copy-on-modify but semantic chaos elsewhere). Nobody pairs a
JS-safe-integer flex tier with a Go machine tier. The truthiness table is
"JavaScript's object rule with the C legacy removed" — a spot every dynamic
language arguably *should* have landed on and none did. Errors as falsy values with
compile-time enforcement is cleaner than exceptions and cleaner than Go's
`if err != nil`. And all of it sits on a document-native data model, which is the
actual domain. The nearest relative in *spirit* is XQuery/XDM — but with a real
programming language attached.

### Standing concerns, in order

1. **The spec–implementation gap is now large and must not grow.** C1–C4 define
   real migrations: literal `""` mechanics, the int width change, COW, capture
   errors, the object-mutation bug cluster. The docs were already aspirational in
   places (A10); the C-log is aspirational *by design*. That is fine exactly as
   long as the decisions land as code and formal rules soon — "implement C1–C4 +
   encode as rules + verify fixtures" should be treated as one indivisible program
   of work.
2. **The error discipline still leaks at the builtin boundary** (A6/B8) — the last
   big *semantic* hole. `T^E` is one of the language's crown jewels, and OOB
   indexing quietly excreting error values undermines it. Ranked the next design
   discussion.
3. **Performance will pressure the semantics.** COW cliffs on elements, promotion
   checks, decimal paths — against a strong benchmark culture. The line to hold is
   the one this review established: representation tricks (boxing, sharing,
   inference) may do anything *as long as they are unobservable*, and the formal
   model exists to verify exactly that.
4. **The learning curve is real.** Mutation that doesn't travel, containers that
   are always truthy, `if (results)` not meaning "any results" — JS/Python muscle
   memory will fight all of it. Every point is defensible; the burden lands on
   error messages, lints, and docs. A language this principled deserves diagnostics
   that *teach the principle*, not just reject the code.

### Verdict

Before this exercise: "a very good data model with implementation-defined
semantics." Now: **one of the most coherent semantic cores of any scripting
language available for comparison** — with the caveat that a third of it currently
exists on paper rather than in the binary. The four C-records read like the
beginning of an actual language specification, which is precisely what the
semantics DSL needs as input: Stage 4's rules can be written against a language
whose every touched corner has a stated, argued, dated answer.

The thing to preserve above all is the *method* that got here: probe the
implementation, argue from principles, record the losing arguments alongside the
winners. The log is worth more than the decisions themselves — it is what will keep
the next ten decisions consistent with these four.

### Carried forward to Lambda_Semantics_Formal2.md

A2 (document div/mod conventions) · A4 (`symbol == string`) · A5 (ArrayNum `==`
fix) · A6/B8 (OOB + builtin error-value leaks) · A8 (covariance failure mode) ·
A9 (REPL) · A10 (spec gaps: generics, `as`, open/closed maps, match order) ·
B4 (`~` overloading) · B5 (`|` disambiguation) · B6 (`[int]` footgun) ·
B7 (inference unobservability as enforced invariant) — plus the C1–C4
implementation follow-ups listed in their respective sections.
