# Lambda vs TypeScript: Type System Comparison

**Status**: informal review / idea note (2026-08-07)
**Sources**: `doc/Lambda_Type.md`, `doc/Lambda_Formal_Semantics.md` (S#), `doc/Lambda_Formal_Design.md` (D#)

A comparison of Lambda Script's type system against TypeScript's, with an honest
accounting of where each is stronger.

---

## 1. Where the two systems fundamentally differ

**Lambda's types are real; TypeScript's are erased.** TS types vanish at compile
time and are deliberately unsound (variance holes, `any` laundering, unchecked
casts) — the running JS can violate every annotation. Lambda types are enforced
at runtime boundaries (the TE enforcement ledger, `vibe/Lambda_Design_Type_Enforcement.md`),
and types are first-class runtime values that can be stored, passed, and
compared:

```lambda
let T = int
fn validate(value, expected_type: type) => value is expected_type
```

TS has nothing like this — type-level `typeof` is not a value, and a type
cannot be passed to a function.

**Lambda is structural with a nominal escape hatch by design** — D3.4.2: shape
identity is structural, not nominal; object types (`type Point { ... }`) opt
into nominal checking. TS is structural-only and simulates nominal typing with
branded-type hacks.

---

## 2. What Lambda does that TS can't (or does badly)

- **Refinement types**: `int that (~ > 0)`, field-level and object-level
  constraints on object types. TS has no value-level predicates in types at all.
- **Occurrence / cardinality types**: `int[5]`, `int[2, 10]`, `T+` (non-empty).
  TS can fake fixed-size with tuples but has no bounded-size or non-empty array
  types.
- **String/symbol patterns as types**: `string Email = \w+ "@" ...` usable in
  annotations, `is` checks, and `match` arms. TS template literal types are a
  weaker, purely compile-time cousin.
- **Negation and exclusion types**: `!null`, `number ! float`. TS's
  `Exclude<T, U>` operates only over union members — it cannot express
  "anything but string".
- **Element types** (`<a href: string; string>`) — document schemas live in the
  type language itself; TS models markup only via JSX's bolt-on typing.
- **Semantics with a spec**: total equality and ordering (S1.9), a principled
  numeric promotion lattice (S4.4.1 — subsumption iff every source value embeds
  exactly in the target). TS inherits JS's `==`/`NaN` mess and just annotates it.

---

## 3. What TS does better

1. **Generics / parametric polymorphism.** The big one. Lambda has none — SO9
   explicitly lists generics as out of scope/unowned, and SO33 flags the
   aspirational `fn identity<T>` text in the docs as "document or delete." TS
   generics with constraints, inference, and higher-order flow
   (`map<T, U>(f: (t: T) => U)`) let reusable collection code be typed
   precisely; in Lambda those results come back as `any` or rely on
   builtin-specific inference.
2. **Flow-sensitive narrowing.** TS's control-flow analysis (discriminated
   unions, `typeof`/`in` guards, assertion functions, exhaustiveness via
   `never`) is arguably its best feature. Lambda's is also SO9-unowned —
   `if (x is string)` does not statically retype `x` in the branch, and D3.3.3
   deliberately limits even container element-type narrowing to a single
   binding's scope. TS's narrowing is deeper and more ergonomic.
3. **Type-level computation**: conditional types, mapped types, `keyof`,
   `infer`, template-literal manipulation. Unsound and abusable, but it lets
   libraries type APIs (ORMs, routers, builders) that Lambda's type language
   simply cannot express.
4. **Utility / transform types**: `Partial`, `Pick`, `Omit`, `Readonly` —
   deriving one shape from another. Lambda types are written out longhand;
   there is no way to compute "same map minus field x".
5. **Gradual adoption and ecosystem**: `.d.ts` files typing a foreign untyped
   ecosystem, `// @ts-expect-error`, per-file strictness, and IDE tooling
   (rename, find-references, hover types) that is a decade ahead. Lambda's
   static checker exists but the language-server story is not comparable.
6. **Error messages as a product**: TS invests enormously in diagnostics,
   related spans, and fix suggestions.

---

## 4. Summary

TS is a much more powerful *static* type system bolted onto an unsound runtime;
Lambda is a sound, runtime-real type system with richer *value-level* types
(refinements, patterns, cardinality) but almost no *type-level* abstraction.

The two gaps that would sting a TS user most in Lambda are exactly the two the
spec already tracks as open (SO9): **generics** and **flow-sensitive
narrowing**. If either is ever prioritized, narrowing is the higher-leverage
one — it makes union types (which Lambda already has) pleasant to consume,
whereas generics mostly matter for library authors.
