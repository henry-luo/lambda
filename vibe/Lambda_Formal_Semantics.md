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

### A3. `"" ≡ null`: the string type is not closed under its own operations

| Expression | Result |
|---|---|
| `"" == null` | `true` |
| `"" is string` | **`false`** |
| `"" is null` | `true` |
| `len("")`, `len(null)` | `0`, `0` |
| `[] == null` | `false` |
| `{} == null` | `false` |

Consequences:

- `string` is not closed: `replace(s, "a", "")` applied to `"a"` yields a value that
  is *not a string*. Any function typed `fn f(s: string) string` can be broken by a
  value its own operations produced.
- `++` has no identity element within the type.
- The identification is **asymmetric**: only scalars (`""`, `''`) normalize to null;
  empty containers do not.

Docs acknowledge the normalization (`Lambda_Type.md` § Constrained Types) but not its
consequences. See B1.

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

### A7. Aliasing under mutation: three regimes, none documented

Probe (`pn` context):

```lambda
var a = [1,2,3];  let b = a;  a[0] = 99      // b[0] == 99    → arrays: REFERENCE
var c = <Counter>; let d = c; c.increment()  // d.value == 0  → objects: COPY
var e = <div class:"x">; let f = e; e.class = "y"  // f.class == "y" → elements: REFERENCE
```

Plus the documented third regime: closures assigning captured `var`s get a
**writable copy** (`Lambda_Func.md` § Mutable Captures).

So "does `let b = a` alias?" currently depends on the runtime TypeId of the value.
Nothing in the docs specifies which containers have reference vs. value semantics.
This is the single largest semantic underspecification in the language. See B3.

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

## Triage summary

| ID | Finding | Suggested disposition |
|----|---------|----------------------|
| A1 | Overflow trichotomy | **Decide & fix** (B2); literal→float is a bug regardless |
| A2 | div/mod conventions | **Document** (behavior itself defensible) |
| A3 | `"" ≡ null` leaks | **Design decision needed** (B1) |
| A4 | `'a' == "a"` raises | **Fix or re-document** — impl and docs must agree |
| A5 | `==` non-reflexive / repr-sensitive | Document NaN; **fix ArrayNum ==** (task_38782787) |
| A6 | OOB trichotomy, silent error values | **Decide & fix** (B8) |
| A7 | Aliasing trichotomy | **Document now, converge later** (B3) |
| A8 | Covariance log-only failure | **Fix failure mode**; decide assignment-subtyping rule |
| A9 | REPL rollback replay | **Bug fix** (tooling) |
| A10 | Spec gaps (generics, `as`, open maps, match order, `~`) | **Document**; delete aspirational generics text or implement |
| B1–B8 | Design recommendations | Discussion — this document's purpose |

Every "decide" row above is a decision that formal rules (the Redex model of
[Lambda_Semantics.md](Lambda_Semantics.md), or the proposed native DSL) will force
anyway. Resolving them first means the formal model describes the language you
*want*, not the accidents you have.
