# Lambda Reference Type, Forcing, and Address-of

**Date:** 2026-09-15
**Status:** PROPOSED — nothing ruled, nothing implemented
**Decision IDs:** PTH30–PTH41 (extends the path/reference series whose home is
[`Lambda_Type_Path.md`](Lambda_Type_Path.md) §13)
**Open issue IDs:** PTH-O2–PTH-O5
**Scope:** the `reference` type (symbol ∪ path), the explicit force operator,
address-of, and how the three close the node-identity holes SO39 / SO41 / DO25.
Out of scope: the resolver/mount model (PTH16–PTH19), the DOM package's own
node API, and remote transport.

> **Normative anchors:** [S1.7](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (one symbol, one concept), [S1.8](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (strings are never code), [S2.4](../doc/Lambda_Formal_Semantics.md#s24-paths)
> (paths, S2.4.3v3 evaluation contracts, S2.4.4 resolution is not forcing),
> [S5.1.4v2](../doc/Lambda_Formal_Semantics.md#s51-equality) (identity is data),
> [S7.4.5](../doc/Lambda_Formal_Semantics.md#s74-error-channels) (`input`
> raises), [S9.1.5v2](../doc/Lambda_Formal_Semantics.md#s91-values) (no
> reference cells; cross-references are data),
> [S10.4](../doc/Lambda_Formal_Semantics.md#s104-parent-navigation) /
> [S10.5](../doc/Lambda_Formal_Semantics.md#s105-root-navigation) (postfix
> navigation steps), and open items SO39, SO41, DO25.
> The formal specs win where this record disagrees.

**Proposed spec linkage (on ratification):**

| This record | Spec target |
|---|---|
| §2 reference type, split of `path` out of `symbol` | S2.1.1v4 (scalar list), new S2.4.6, S6.2.1v2 (order band) |
| §3 force operator | new S10.6 "Forcing", S2.4.4 impl note, S7.4.5 cross-ref |
| §4 address-of and `===` | S5.1.4v3, S9.1.5v3; closes SO41, narrows SO39, informs DO25 |
| §5 loops and pipes | S10.1.2 note (no `for`/pipe auto-force) |
| §6 forcing a symbol | new S10.6.3; S1.8 cross-ref |

---

## 1. Decision (proposed)

Lambda gains one type alias, one postfix operator, and one system function:

```lambda
type reference = symbol | path      // URI = URN | URL

let p = /.'data.json'               // path:   a URL-like locator (S2.4.5v2)
let s = 'sec-intro'                 // symbol: a URN-like name   (S16.8.7)

p@                                  // force: the value at p  (≡ input(p)^E contract)
p@.rows[0]                          // force, then ordinary member/index steps
p.rows@                             // extend the PATH by .rows, then force
for (v in p@) { … }                 // explicit force; for never forces on its own
paths |> ~@                         // force each

ref(node)                           // address-of: the node's reference, or null
ref(a) == ref(b)                    // what `===` means (S5.1.4v2 identity)
```

The three rules:

> **A reference is a value that names something; forcing is the one explicit
> operation that reads what it names.** Nothing forces implicitly — not `.`,
> not `[]`, not `for`, not the pipe (S2.4.3v3, S2.4.4).

> **Postfix `@` is the force step.** It sits at member/index precedence and
> chains left-to-right like `./` and `.~~` (S10.5.1, S10.4.1), so ordinary
> `.`/`[]` steps apply to its result with no second operator.

> **`ref(v)` is address-of.** It yields the reference a value carries as its
> identity (S5.1.4v2) or `null`; `===` is equality of non-null references.

Rejected: `a -> b` (fuses force with member access), auto-forcing `.`/`[]`
(they already mean path-step append), auto-forcing `for` (a `for`-only
implicit-I/O site), and the `#` glyph (collides with `~#`). §7 has the record.

---

## 2. The `reference` type

### 2.1 The URI reading

| Web | Lambda | What it is |
|---|---|---|
| URN | `symbol` | a namespaced, location-independent *name* |
| URL | `path` | a rooted or provider-anchored *location* (S2.4.5v2) |
| URI | `reference` | either |

This is already the shape S2.4.3v3 describes — "paths, names, symbols, and
member expressions use this one reference scheme but retain distinct
evaluation contracts" — and it is exactly the shape S5.1.4v2 gives a node
identity: *document path plus the node's id within it*, which is a URL plus a
fragment. The `reference` type names that existing structure; it adds no
value kind.

### 2.2 `path` leaves `symbol` (PTH30)

Today S2.1.1v3 lists `symbol (with path as a special symbol)` and the S6.2.1
order writes `symbol (path ⊂ symbol)`. Under that lattice `symbol | path`
collapses to `symbol` and the URI reading is vacuous. The proposal is:

- `path` is a scalar type **disjoint** from `symbol`. `p is symbol` is false
  for every path; `p is path` and `p is reference` are true.
- `symbol` keeps its S2.2.2 solid-type contract and its S8.2.1v4 `NameKey`
  role unchanged. `'a' is reference` is true; `'a' is path` is false.
- `reference` is a **type alias**, `symbol | path`, in the sys namespace
  (S17.2.1), shadowable user-first like any built-in name (S12.3.7).
- The S6.2.1 band becomes `… < datetime < symbol < path < string < …`:
  paths sort after symbols as their own band, bytewise within the band by
  canonical spelling (S2.4.2v4 normalization).
- `==` across the two is false by type, as between symbol and string today
  (`'a' == "a"` is false, S5.1.4v2 content-plus-type).

The runtime already carries a separate `LMD_TYPE_PATH` tag with its own
`type()` name `"path"`; the split is a spec correction more than a runtime
change (Appendix A).

### 2.3 Why an alias and not a nominal supertype

A nominal `reference` supertype would need every symbol operation (indexing,
slicing, pattern islands `\symbol(…)`, S10.6.1) to say whether it lifts to
paths. An alias asks nothing of the operations: `reference` is a union type
like any other (S10.1.1), usable in annotations, patterns, and `is`, and it
dissolves at every use site into the two contracts S2.4.3v3 already keeps
distinct.

---

## 3. The force operator

### 3.1 Shape: postfix, then ordinary steps (PTH31, PTH32)

Forcing is spelled as a **postfix step**, not a binary operator, for the same
reason `./` and `.~~` are postfix: Lambda's navigation is a left-to-right chain
at one precedence tier (S10.4.1, S10.5.1), and a postfix force slots into that
chain without inventing a second member operator.

```lambda
p@              // the value p names
p@.a            // (p@).a   — member a OF THE VALUE
p.a@            // (p.a)@   — extend the path by step a, THEN force
p@[k]           // dynamic key on the value
p@./            // root of the value's hierarchy (S10.5.2), not of the path
(p@)@           // force twice: legal iff the first result is itself a reference
```

The two readings of `p@.a` versus `p.a@` are both meaningful and both
needed: the first reads a member of a loaded document, the second addresses a
sub-target and loads only that (a provider that supports partial reads, a
`sys.*` key, a fragment). C's `->` exists precisely because `(*p).a` cannot
express this pair tersely; a postfix force makes `->` unnecessary.

`@` binds at member/index precedence, left-associative, above `^`
(S7.6.1v4): `p@^` is "force, then propagate the raised error".

### 3.2 The glyph (PTH33)

`@` is the only single ASCII operator glyph the grammar does not use: it is
excluded from identifier characters (`grammar.js` `alpha`) and appears in no
production. Mnemonic: *at* — the value at the address (`user@host`).

It has **no prefix role**, so under S16.2.2/S16.2.3 it is a pure postfix
token and never enters the dual-role banned set. This matters: §7.1 of
[Design_Syntax](Lambda_Design_Syntax.md) removed unary `!` because a glyph
with both a prefix and an infix role is a line-start trap. `@` cannot regress
that.

Considered and not chosen: the step-shaped spelling `p.@` (reads as "the
step named `@`", but force is not a step *within* the reference; it crosses
from the reference to its target), and the glyphs `#`, `*`, `->`, `!`, `&`
(§7).

### 3.3 What forcing yields (PTH34, PTH35)

Forcing is the S2.4.4 "separate operation with its declared effect/error
contract". One operator, one contract per reference kind:

| Reference | Force yields | Absence / failure |
|---|---|---|
| absolute or rooted path to an external provider (`file`, `http`, logical `/` mounted to one) | the parsed document, exactly `input(p)` with format auto-detection | **raises** `T^E` — `input`/`fetch` are pn-family effectful readers (S7.4.5); permitted in expression position; `p@ ^ { ^ }` acknowledges |
| `sys.*` path | the system value (already resolved today) | `null` (S7.1.1v3) |
| relative path `\.a.b` | qualified through the active base first (S2.4.1v2), then as above | as the qualified kind |
| path with a node fragment (§4) | the identified node of the forced document | `null` if the document has no such id |
| symbol | §6 | `null` |
| non-reference operand | compile error where the type is static; `error()` otherwise (S7.10) | — |

`p@` for an external target is therefore **sugar for `input(p)`** and inherits
its effect class: it may appear inside an `fn` in expression position exactly
as `input` may today (S7.4.5's wrapper idiom is unchanged). `input(p, format)`
remains the spelling for an explicit format; `exists(p)` remains the no-force
probe. No new I/O path enters the runtime.

### 3.4 Resolution stays free of I/O (PTH36)

Everything left of `@` is address resolution (PTH16–PTH19, S2.4.4): building
`p.a.b`, qualifying `/`, comparing or hashing paths. Only `@` forces. This is
what keeps the reference scheme from becoming an implicit-I/O loophole in the
`fn`/`pn` effect model ([Type_Path §2.6](Lambda_Type_Path.md#26-resolution-is-not-forcing)),
and it is why options that force on `.` or in `for` (§7) are not merely
inconvenient but contrary to S2.4.4.

---

## 4. Address-of and identity

### 4.1 `ref(v)` (PTH37)

`ref(v)` is a system `fn` returning the reference `v` carries as its node
identity (S5.1.4v2, D2.6.8), or `null` when `v` carries none. It performs no
I/O and creates no reference: identity is data the container already holds
(S9.1.5v2), and `ref` only reads it — which is why the S7.4.5 sys-`fn`
contract (absence → `null`) applies, not a raise.

```lambda
let doc = /.'page.html'@
let h = doc.body[0]
ref(h)                    // /.'page.html' + fragment step for h's id (PTH-O3 for the spelling)
ref(h)@ == h              // true: forcing the identity re-reads the node
ref(1)                    // null: scalars carry no identity
ref({a: 1})               // null: a fresh literal carries none (SO39)
```

It is a function, not a sigil, on purpose. `&` is the infix set-intersection
operator; adding a prefix role would make it dual-role at line start — the
§7.1 trap again. S10.3.1 prefers words over sigils where no chain ergonomics
are at stake, and address-of is never chained.

### 4.2 `===` is reference equality (PTH38)

S5.1.4v2 defines `===` on identity and leaves its non-identity case to SO39.
With references as values the definition is closed:

> `a === b` ≡ `ref(a) != null and ref(a) == ref(b)`.

- Two identity-less operands compare **false**, never a compile error —
  resolving the SO39 sub-question in favour of totality (S1.9: equality is
  the root relation and total).
- `==` on references is path equality under S2.4.2v4 normalization plus
  fragment equality; rooted and absolute spellings of one target stay
  distinct (PTH24), so `===` across a rooted and an absolute load of the same
  document is false. This is the price of context-free value equality
  (S5.1.4) and is recorded, not hidden.
- No `ref_eq` and no address comparison enter the language (S5.1.4v2 last
  sentence; D2.6.8).

### 4.3 What this closes

- **SO41** (cross-reference form for document graphs): a cross-reference is a
  `reference` value stored as data and resolved by forcing — the foreign-key
  reading S9.1.5v2 already prescribes, now with a spelled resolution
  operation (`@`) and a spelled way to mint one (`ref`).
- **SO39** narrows to the two questions this record leaves open: which
  operations preserve identity (PTH-O4) and the fragment spelling (PTH-O3).
  The `===`-on-identity-less question is resolved above.
- **DO25** (carrier) is unchanged in substance but gains a requirement: the
  carrier must be readable as a `reference` value by `ref` without
  allocation of a new identity.

---

## 5. Loops and pipes (PTH39)

`for`, the mapping pipe, `that`, and every other iteration form take a
**value**. A reference is a scalar value that iterates as itself (S10.1.2:
scalars pipe as a whole value). To iterate the target, force it:

```lambda
for (v in p@) { … }
p@ |> f(~)
paths |> ~@              // a list of references → their targets
paths |> ~@ that ~.ok    // force, then filter the targets
```

Auto-forcing in `for` alone (the user's loop option 3) is rejected: it would
make the loop header the one implicit-I/O site in the language (S2.4.4) and a
`for`-specific reading of a value (S1.7). The `-> ~` spelling (loop option 1)
is the symptom that a fused force-and-member operator cannot express "force,
whole": with a standalone postfix force there is nothing to fuse.

`~@` reads naturally because `@` is postfix on the current item, exactly as
`~.a` and `~^` are. This is the reason `#` cannot be the glyph: `~#` is
already the current key (S10.1.2, S10.1.5), and `refs |> ~#` is the very
idiom a force glyph would be spelled with.

---

## 6. Forcing a symbol (PTH40, PTH41)

A symbol is a `NameKey` that "does not implicitly read bindings" (S2.4.3v3;
S16.10.1's `'edit'.x`). An *explicit* force of a symbol therefore needs a
ruling on what it resolves against. The URN analogy says: a URN needs a
resolver that maps the name to a location. Lambda has exactly one such thing,
the immutable evaluation resolver of S2.4.4.

**Ruling (proposed).** `'name'@` resolves `name` through the active resolver's
**identity/namespace table** — a document's id table when the active base is
a document, a provider's name table when it is a provider — and yields the
identified value or `null`. It **never** reads a lexical binding, a module
export by name, or the sys-func registry.

Why the lexical reading is rejected outright:

- S1.8 — strings are never code. A symbol is not a string, but "the value
  bound to the name this symbol spells" is dynamic scope lookup, the half of
  `eval` that S1.8 exists to keep out. It would also make every renaming a
  behaviour change.
- S2.4.3v3 — names are *statically* namespace-qualified. A runtime lookup by
  symbol has no static qualification to be identical to (S1.6 demands the
  static and dynamic answers agree); the id-table reading has one, because a
  document's id set is data.
- S1.4 — no global mutable state; a lexical reflection primitive is the usual
  way one grows.

Under this ruling `'sec-intro'@` inside a template rendering a document is
the element with that id, `\.'page.html'@.'sec-intro'@` is the same via an
explicit document, and in a plain module with no id-bearing base it is `null`.
Whether a document declares its id attribute (`id`, `xml:id`, a schema-named
key) is the document model's business (S5.1.4v2: identity is data a document
assigns), recorded as PTH-O5.

---

## 7. Rejected alternatives

| Option | Why not |
|---|---|
| **`a -> b`** (C/C++ deref-member) | Fuses two concepts — force and member access — into one glyph (S1.7). Needs a second spelling for "force, whole" (the proposed `-> ~`), which is the tell. `->` is free in the grammar but reads as an arrow/mapping to every reader, and C only has it because `(*p).b` was unbearable; a postfix force removes the need. |
| **`a # b`** (URL fragment glyph) | `~#` is the current key/index (S10.1.2, S10.1.5). Not a lexical clash (`~#` is one token) but a conceptual one under S1.7, and a practical one: `refs \|> ~#` is the force-each idiom. Respelling `~#` is spec churn to buy a glyph. The fragment reading is kept where it belongs: the node-id step *inside* a path (PTH-O3). |
| **Overload `.` / `[]` with auto-force** | `.` on a path is already **step append** (S2.4.1v2, S2.4.2v4): `p.b` is a longer path, not a read. Auto-forcing would also read a binding through a symbol (S16.10.1) and perform I/O during address resolution (S2.4.4). Three rulings, not one, say no. |
| **Auto-force in `for` only** | A `for`-only implicit read (S1.7) and an implicit-I/O site (S2.4.4). |
| **Prefix `*p`** | `*` is spread in containers and the wildcard step in paths (§7.10 of Design_Syntax); `[*p]` would be ambiguous between spread and force. |
| **Postfix `!`** | `!` is infix set exclusion and type complement; §7.1 just removed its prefix role to end a dual-role trap. |
| **Prefix `&v`** for address-of | `&` is infix set intersection; prefix role → dual-role banned set (§7.1 lesson). A function costs nothing here because address-of is never chained. |
| **Nominal `reference` supertype** | §2.3: forces every symbol operation to rule on paths; the alias asks nothing. |
| **`p()` call syntax as force** | A path that resolves to a function value is then ambiguous between "force" and "force and call"; S2.4.3v3 keeps paths as lazy target handles, not thunks. |

---

## 8. Migration and conformance

- **`path ⊂ symbol` tests.** None. Probed 2026-09-15 on the debug build:
  `/.data is symbol` is **already `false`**, `type(/.data)` is `path`, and
  `x is path` does not resolve at all (`type-pattern: unresolved type name
  'path', using ANY`, result `false`). The runtime never implemented the
  S2.1.1v3 subtype; PTH30 records what ships and adds the missing `path`
  type name. No `.ls` in `test/lambda/` tests `is path`, and the `is symbol`
  probes are on symbol literals only.
- **Sort order.** Mixed symbol/path sorts move paths after symbols. Any
  golden that sorts a mixed list must be re-captured; none is known.
- **`@` token.** New postfix token; no existing program contains `@` outside
  strings and comments (it is not an identifier character), so no source
  changes spelling.
- **`ref`, `reference` names.** Both enter the sys namespace user-first
  (S12.3.7): an existing `let ref = …` keeps working and shadows the
  built-in module-lexically.
- **Goldens for `p@`.** `p@` and `input(p)` must print identically across
  both tiers (S1.6; tier parity is the standing gate).

---

## 9. Decision ledger

| ID | Ruling (proposed) |
|---|---|
| **PTH30** | `path` is a scalar type disjoint from `symbol`; `reference` is the type alias `symbol \| path`. The S6.2.1 band is `symbol < path < string`. |
| **PTH31** | Nothing forces implicitly: not `.`, `[]`, `for`, the pipe, `that`, or any iteration form. A reference is a scalar value everywhere it is not explicitly forced. |
| **PTH32** | Forcing is a postfix step at member/index precedence, left-associative, chaining with `.`, `[]`, `./`, `.~~`; `p@.a` reads a member of the target, `p.a@` forces the extended path. |
| **PTH33** | The force step is spelled `@`. It has no prefix role and never enters the S16.2.3 dual-role set. |
| **PTH34** | Forcing an external-provider path is `input(p)`: pn-family, raises `T^E` (S7.4.5), permitted in expression position; `exists(p)` remains the no-force probe and `input(p, format)` the explicit-format spelling. |
| **PTH35** | Forcing an in-memory reference (`sys.*`, a node fragment, a symbol) is a total read: absence is `null` (S7.1.1v3). Forcing a non-reference is a static type error or `error()`. |
| **PTH36** | Address resolution left of `@` performs no I/O (restates S2.4.4 for the operator). |
| **PTH37** | `ref(v)` is a system `fn` returning the reference `v` carries as its identity, or `null`; it allocates no identity and performs no I/O. No sigil form is minted. |
| **PTH38** | `a === b` ≡ `ref(a) != null and ref(a) == ref(b)`; identity-less operands compare `===`-false (resolves that SO39 sub-question). No address comparison exists. |
| **PTH39** | Iteration over a target is spelled `for (v in p@)` / `refs \|> ~@`; `for` and the pipe never force. |
| **PTH40** | `'name'@` resolves through the active resolver's identity/namespace table (a document's id table, a provider's name table); absence is `null`. |
| **PTH41** | Forcing a symbol never reads a lexical binding, a module export, or the sys-func registry (S1.8, S2.4.3v3, S1.4). |

---

## 10. Open issues

- **PTH-O2 — grammar placement of `@`.** Confirm `@` as a member-tier postfix
  in `grammar.js` and the C parser, including the closed-tail rule (§7.14 of
  Design_Syntax): `p@` ends in a postfixable token, so a following line
  starting with `.ident` continues the chain (S16.2.4v2). Decide whether
  `p @` with whitespace is accepted (recommendation: no, like `^`).
- **PTH-O3 — fragment step spelling.** The node-id step inside a path is the
  concrete form of the universal id (SO39). Candidates: a third canonical key
  class beside `NameKey`/`IntKey` (S8.2.1v4) spelled `.#id` — the URI fragment
  glyph, arguably the *same* concept as `~#` (a key), which would satisfy
  S1.7 rather than violate it — or a reserved attribute step. Needs its own
  ruling; blocks `ref` printing, not the operator.
- **PTH-O4 — identity-preserving operations.** Which operations keep a node's
  identity (SO39: COW detach and in-place `var` write expected yes;
  constructors `{*: m}` / `<T *: o>` expected no). Unchanged by this record;
  listed because `ref` makes the answer observable.
- **PTH-O5 — which attribute is the id.** Per document model (S5.1.4v2:
  identity is data a document assigns): HTML `id`, XML `xml:id`, a
  schema-declared key. Owned by the input/document design, not here.

---

## Appendix A — Implementation notes (brief)

- `LMD_TYPE_PATH` already exists as a tag distinct from `LMD_TYPE_SYMBOL`
  (`lambda/lambda.h`), `type()` already answers `"path"`, and `fn_is`
  already answers `false` for `path is symbol` (probed 2026-09-15). PTH30 is
  a spec correction plus three small additions: register `path` as a
  resolvable type name in type patterns (today it warns "unresolved type
  name 'path', using ANY"), add the `reference` alias to the sys type table,
  and place the `path` band in the S6.2.1 comparator.
- `@` lowers to the existing `input` dispatch for provider paths and to the
  existing `sys.*` resolution for system paths on both tiers; no new I/O
  code. The interpreter and MIR Direct must share one forcing helper (rule
  13), reached from the postfix-step lowering next to `./` and `.~~`.
- `ref(v)` reads the DO25 carrier. Until DO25 lands it returns `null` for
  every value, which is spec-conformant (no value carries identity yet) and
  lets `===` ship as the PTH38 definition immediately.
- Symbol forcing (PTH40) needs a resolver hook: the active base exposes an
  optional id table. Documents loaded by `input` populate it from the
  PTH-O5 attribute; modules expose none.
