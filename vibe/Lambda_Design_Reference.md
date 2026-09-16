# Lambda Reference Type, Forcing, and Identity

**Date:** 2026-09-15; revised 2026-09-16 (author rulings 1–5 recorded; `~key` confirmed; write-through = new generation; versioned identity; local mutation = no identity; `put` is the write-through keyword)
**Status:** PROPOSED — rulings taken in review, **not ratified** into the formal
specs, nothing implemented
**Decision IDs:** PTH30–PTH58 (extends the path/reference series whose home is
[`Lambda_Type_Path.md`](Lambda_Type_Path.md) §13)
**Open issue IDs:** PTH-O2–PTH-O12 (PTH-O4, PTH-O11, PTH-O12 resolved)
**Scope:** the `reference` type (symbol ∪ path), the postfix `#` force step
and its fragment sugar, prefix `&` address-of, `===`, document-node identity
and its carrier, the `put` write-through statement, `temp.` documents, and
the `~key` focal accessor respell.
Out of scope: the resolver/mount model (PTH16–PTH19), the DOM package's own
node API, remote transport.

> **Normative anchors:** [S1.4](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (mutation is visible), [S1.6](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (representation is invisible), [S1.7](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (one symbol, one concept), [S1.8](../doc/Lambda_Formal_Semantics.md#s1-core-principles)
> (strings are never code), [S2.4](../doc/Lambda_Formal_Semantics.md#s24-paths)
> (paths; S2.4.3v3 evaluation contracts; S2.4.4 resolution is not forcing),
> [S5.1.4v2](../doc/Lambda_Formal_Semantics.md#s51-equality) (identity is
> data), [S7.4.5](../doc/Lambda_Formal_Semantics.md#s74-error-channels)
> (`input` raises), [S9.1.5v2](../doc/Lambda_Formal_Semantics.md#s91-values)
> (no reference cells), [S10.1.2](../doc/Lambda_Formal_Semantics.md#s101-union-pipe-and-filter)
> (pipe binds `~`), [S10.4](../doc/Lambda_Formal_Semantics.md#s104-parent-navigation)
> / [S10.5](../doc/Lambda_Formal_Semantics.md#s105-root-navigation) (postfix
> navigation), [S16.2.2v2 / S16.2.3v2](../doc/Lambda_Formal_Semantics.md#s162-line-start-tokens)
> (line-start token classes), and open items SO39, SO41, DO25, D2.6.8.
> The formal specs win where this record disagrees.

**Proposed spec linkage (on ratification):**

| This record | Spec target |
|---|---|
| §2 reference type; `path` leaves `symbol` | S2.1.1v4, new S2.4.6, S6.2.1v2 |
| §3 `#` force step, fragment sugar, longest-prefix forcing | new S10.6; S2.4.4 impl note; S7.4.5 cross-ref; S16.2.2v3 (`#`, `===` join the continuation set) |
| §4 `&`, identity, `===`, `temp.` | S5.1.4v3, S9.1.5v3, D2.6.8v2; closes SO41, DO25; revises SO39; S16.2.3v3 (`&` joins the dual-role set) |
| §5 `~key` respell | S10.1.2v2, S10.1.5v2 |
| §6 forcing a symbol | new S10.6.3 |

---

## 1. Decision

```lambda
type reference = symbol | path        // URI = URN | URL

let p = server.dir.'page.html'        // a path is a reference and stays LAZY
p.body.0                              // still a path: steps append, nothing is read
p#                                    // force: the document at p        (like *ptr)
p#body.0                              // force, then navigate the value   (sugar for p#.body.0)
p.body.0#                             // extend the path across the doc boundary, then force
p#cfg.link#x                          // a forced member holding a path is itself lazy: force again

var n = doc.a.b.c#                    // n holds the node as it is NOW
put doc.a.b.c#x = 1                   // write THROUGH the reference: a new node, next generation
var m = doc.a.b.c#                    // m is the new node: n === m false; &n = doc.a.b.c@v0, &m = doc.a.b.c@v1
var d = doc#                          // a value snapshot: shares the nodes for reading
d.a.b.c.x = 2                         // LOCAL: plain = never writes through; doc.a.b.c# is unchanged
put d.a.b.c.x = 3                     // writes doc.a.b.c.x through AND d sees it: d.a.b.c.x is 3

&expr                                 // address-of: the node's reference, or null
a === b                               // reference equality: &a != null and &a == &b (versions included)
output(v, temp.'scratch')             // give runtime data identity by placing it in a temp document

refs |> ~#                            // force each; ~key is the current key/index (was ~#)
```

The five rules:

> **R1 — References are lazy values.** A path is never read by `.`, `[]`,
> `for`, the pipe, or any other form. Only `#` forces (S2.4.3v3, S2.4.4).

> **R2 — `#` is the force step, postfix, at member precedence.** `p#name`,
> `p#123`, `p#[e]` are sugar for `p#.name`, `p#.123`, `p#[e]`. Forcing a
> path forces the longest prefix that names a document and navigates the
> rest in memory; `a.b#c.d`, `a.b.c.d#`, and `a.b#.c.d` yield one value.

> **R3 — `&expr` is address-of and `===` is reference equality.** `&` reads
> the reference a document node carries — location plus generation,
> conceptually `doc.a.b.c@v0` — or `null`; `a === b` ≡ `&a != null and
> &a == &b`.

> **R4 — Only containers inside a document have identity.** Their identity
> is their path within the document; runtime-constructed data has none until
> it is placed in a `temp.` document; scalars never have one. Modifying a
> **Write-through is spelled `put`** (`put doc#a.b.c.x = 1`, `put d.a.b.c.x
> = 1`) and creates new nodes stamped with the next generation, visible only
> in this evaluation, which the target's binding also sees; plain `=` is
> always a **local** write, whose copies keep their location but carry no
> identity. A path is never
> attached to a node: `#` attaches at forcing time, to the **latest**
> generation in this evaluation.

> **R5 — The current key is `~key`.** `~#` is retired so that `#` means force
> everywhere (S1.7). `~word` is the focal-accessor family, of which `~~` is
> the existing member.

Rejected (record in §7): `a -> b`, auto-forcing `.`/`[]`/`for`, `@` as the
force glyph, `ref()` as address-of, `~.#` / `~_` / `~@` as the key accessor,
and both extremes for a modified node — identity that survives a COW detach
unchanged, and no identity at all after one.

---

## 2. The `reference` type

### 2.1 The URI reading

| Web | Lambda | What it is |
|---|---|---|
| URN | `symbol` | a namespaced, location-independent *name* |
| URL | `path` | a rooted or provider-anchored *location* (S2.4.5v2) |
| URI | `reference` | either |

S2.4.3v3 already says paths, names, symbols, and member expressions "use this
one reference scheme but retain distinct evaluation contracts", and S5.1.4v2
already gives a node identity the URL-plus-fragment shape, "the document path
plus the node's id within it". The `reference` type names that structure and
adds no value kind.

### 2.2 `path` leaves `symbol` (PTH30)

S2.1.1v3 lists `symbol (with path as a special symbol)` and S6.2.1 writes
`symbol (path ⊂ symbol)`; under that lattice `symbol | path` collapses. The
runtime never implemented the subtype (probed 2026-09-15: `/.data is symbol`
is `false`, `type(/.data)` is `path`, `LMD_TYPE_PATH` is its own tag), so
PTH30 records what ships:

- `path` is a scalar type disjoint from `symbol`; `reference` is the type
  alias `symbol | path` in the sys namespace (S17.2.1), user-first shadowable
  (S12.3.7).
- The S6.2.1 band becomes `… < datetime < symbol < path < string < …`,
  bytewise within the `path` band by canonical spelling (S2.4.2v4).
- `==` across symbol and path is false by type, as between symbol and string.
- `x is path` must resolve; today it warns `unresolved type name 'path'`.

An alias rather than a nominal supertype: a supertype would make every symbol
operation (indexing, slicing, `\symbol(…)` islands) rule on paths; the alias
dissolves at each use site into the two contracts S2.4.3v3 keeps distinct.

---

## 3. The force step `#`

### 3.1 Postfix, at member precedence (PTH31, PTH32)

Lambda navigation is a left-to-right chain at one tier — `./` (S10.5.1) and
`.~~` (S10.4.1) are postfix steps — and the force step joins that chain. It
is a step that crosses from the reference to its target, which is why C's
`->` is unnecessary: with a standalone postfix force there is nothing to
fuse with member access.

```lambda
p#               // the value p names
p#.a             // (p#).a — member a of the VALUE
p.a#             // (p.a)# — extend the PATH by step a, then force
p#[k]            // dynamic key on the value
p#./             // root of the value's hierarchy (S10.5.2), not of the path
p#^              // force, then propagate the raised error (S7.6.1v4)
```

`#` binds at member/index precedence, left-associative, above `^`.

### 3.2 Fragment sugar (PTH33)

Immediately after `#` one step may omit its dot — the URI fragment form:

| Spelling | Means | Key class |
|---|---|---|
| `p#name` | `p#.name` | `NameKey` |
| `p#123` | `p#.123` | `IntKey` |
| `p#'1'` | `p#.'1'` | `NameKey("1")` (PTH14) |
| `p#[e]` | `p#[e]` | dynamic (S8.2.1v4) |
| `p#` | the whole target | — |

No whitespace is permitted between `#` and its fragment step; `p# name` is a
syntax error, as `~ key` is. Steps after the fragment take dots as usual,
because they act on the forced value.

### 3.3 Longest-prefix forcing across document boundaries (PTH34)

A path may run from a server to the deepest node of a document with no `#`
at the boundary — `server.dir.file.node1.child3` is one reference. Forcing
it:

> The resolver forces the **longest prefix that names a document** through
> its provider, then applies the remaining steps as in-memory navigation
> (S7.1). A provider may push the trailing steps down as a partial read; the
> result must be identical (S1.6).

Consequently `file#node1.child3`, `file.node1.child3#`, and
`file#.node1.child3` are one value. Absence of any trailing step is `null`
under S7.1.1v3 in every spelling; an unreadable document raises under S7.4.5
in every spelling.

### 3.4 Nested references stay lazy (PTH35)

`.` after a forced value is member access, but `.` on a **path-valued
member** is step append again (R1 has no exceptions):

```lambda
p#cfg.link.x     // cfg.link holds a path: .x EXTENDS that path, reads nothing
p#cfg.link#x     // force the nested reference, then read x
```

### 3.5 What forcing yields (PTH36, PTH37)

| Reference | `#` yields | Absence / failure |
|---|---|---|
| absolute or rooted path to a provider (`file`, `http`, logical `/` mounted to one) | the parsed document — exactly `input(p)`, format auto-detected — then the trailing steps | **raises** `T^E`: `input`/`fetch` are pn-family effectful readers (S7.4.5), permitted in expression position; `p# ^ { ^ }` acknowledges |
| `sys.*` path | the system value | `null` |
| `temp.*` path | the stored temp document, then trailing steps | `null` |
| relative path `\.a.b` | qualified through the active base (S2.4.1v2), then as above | as the qualified kind |
| symbol | §6 | `null` |
| non-reference operand | compile error when statically typed; `error()` otherwise (S7.10) | — |

`p#` on a provider path is **sugar for `input(p)`** plus navigation; no new
I/O path enters the runtime. `input(p, format)` remains the explicit-format
spelling and `exists(p)` the no-force probe.

### 3.6 Resolution stays free of I/O (PTH38)

Everything left of `#` is address resolution (S2.4.4, PTH16–PTH19): building
`p.a.b`, qualifying `/`, comparing or hashing paths. Only `#` forces. This is
what keeps the scheme from becoming an implicit-I/O loophole in the `fn`/`pn`
effect model ([Type_Path §2.6](Lambda_Type_Path.md#26-resolution-is-not-forcing)).

### 3.7 Line-start class (PTH39)

`#` has no prefix role, so it is a pure continuation token and joins the
S16.2.2v2 set alongside `!` and `|>`: a line beginning `#name` continues the
chain above it. `===` joins the same set beside `==`.

---

## 4. Address-of, identity, and `===`

### 4.1 `&expr` (PTH40)

Prefix `&` reads the reference the operand carries as its identity, or
`null` (sys-fn absence rule, S7.4.5; never a raise). It performs no I/O and
mints no identity.

```lambda
let doc = server.dir.'page.html'#
&doc.body.0                  // server.dir.'page.html'.body.0  — the path you navigated
&doc.body.0# == doc.body.0   // round trip
&1, &"s", &{a: 1}            // null: scalars and runtime literals carry none
```

Precedence: prefix binds looser than postfix, so `&p#name` is `&(p#name)`,
the reference of the fragment's node, and `&x.y` is `&(x.y)` — as in C.

**Spec cost, stated up front.** `&` is infix set intersection and sits in
the S16.2.2v2 continuation set. A prefix role makes it dual-role, so it moves
to the S16.2.3v2 banned set (declared "final"; this is a v3 revision of both
rulings). After an open-tail statement a line beginning `&x` is a syntax
error repaired by `;` — the same treatment as `-x`. Address-of is rarely a
line's first token, so the cost is accepted.

### 4.2 Only document containers have identity (PTH41, PTH42)

> **Identity is the container's path within its document.** A container
> reached by navigating a document keeps its identity as long as the
> navigation result is a container; a scalar result has none. Data
> constructed at runtime has no identity and compares by value only, until
> it is placed in a document (§4.4).

This is S5.1.4v2's own scope — "loaded from or created within an addressable
document" — made precise on three points:

- **Scalars never carry identity.** `&doc.body.0.title` is `null` when
  `title` is a string; `doc.title.~~` is `null` (S10.4.1 absence). This is
  semantics and cost at once: a scalar read yields an `Item` and an identity
  on it would have to be manufactured per read, while a container read
  yields the node itself.
- **Bindings keep it.** `let n = doc.body.0; &n` and `n.~~` work: the
  binding holds the same container. Constructing a new value from it —
  `{*: n}`, `[n]`, spread, a literal — yields runtime data with no identity.
- **The document, not the node, knows the parent.** Parent and root
  navigation from a bound node resolve through the owning document's node
  table (S10.4.3v2, S10.5.3v2: lineage lives outside the container; no
  parent pointers are added to Lambda values).

### 4.3 Generations: write-through mints identity, local mutation drops it (PTH43, PTH49–PTH52)

Document nodes are Input-arena owned, immutable, and non-moving (D4.3.1), so
**the container pointer is the identity carrier**: the owning document maps
the pointer to `(parent, key, generation)`, and `&` walks that table to the
root to spell the path. No header word — DO25's carrier question closes.

```lambda
var n = doc.a.b.c#       // force: the node as the evaluation currently sees it
put doc#a.b.c.x = 1      // WRITE-THROUGH: c and its spine are new nodes, generation 1
var m = doc.a.b.c#       // force again: the latest generation
n === m                  // false
&n, &m                   // doc.a.b.c@v0, doc.a.b.c@v1  (conceptual spelling)
&(doc.a.b.z#)            // doc.a.b.z@v0 — untouched sibling, the same node
n.x, m.x                 // old value, 1 — n is immutable like every value (S9.1)

var d = doc#             // value snapshot (S9.1.2): shares every node for reading
d.a.b.c === m            // true — no copy has happened yet
d.a.b.c.x = 2            // LOCAL: COW copies the spine into d; the copies keep their
&d.a.b.c                 //   LOCATION (doc.a.b.c) but no generation, so & is null
doc.a.b.c#.x             // 1 — the location never saw d's write
put d.a.b.c.x = 3        // through d: doc.a.b.c gets generation 2 AND d.a.b.c is that node
d.a.b.c === doc.a.b.c#   // true again
```

**A path is never attached to a node.** `doc.a.b.c` is a lazy reference
(R1); `#` attaches it at forcing time to the latest generation the
evaluation holds. The rulings:

- **Identity is location plus generation (PTH43v2, confirmed 2026-09-16; printing format open, PTH-O9).** A document keeps a
  write-through counter; each write-through stamps the nodes it creates
  with the next value. The tag is per node, not per document: an untouched
  subtree keeps its old stamp and *is* the same node. Generations are
  evaluation-local, like `temp.` references (SO20): a spelled `@v1` means
  nothing to another evaluation. The `@v` spelling is conceptual; whether
  `&` prints it and how is PTH-O9.
- **`put` is the write-through statement (PTH49v2, PTH55).** `put target
  = v` detaches the spine from the document root to the target's location
  inside the evaluation's **document context** (§A), stamps the new nodes
  with the next generation, and the next force of that location yields
  them. It is an effect on external-looking state, pn-family like `output`
  (S7.4.5), statement position only, and the only operation that changes
  what a location resolves to. **Plain `=` never writes through**:
  `doc#a.b.c = 1` is a compile error whose diagnostic names `put`. The
  keyword was chosen over an optional marker so that `=` keeps one meaning
  — a binding write with no external effect (§4.7).
- **`put` locates its target by the node's location (PTH56).** If the
  target contains `#`, the location is the reference with the post-`#`
  steps re-appended (PTH34 extended to writes: `put doc#a.b.c.x = 1`,
  `put doc.a.b.c#x = 1`, `put doc.a.b.c.x# = 1` write one location).
  Otherwise the target is rooted at a `var` binding whose value is a
  document-derived container, and the location is that container's plus
  the member steps. A target with no location — a runtime literal, a value
  `output` copied into a temp document without re-forcing — is a compile
  error where the type is static and a raise otherwise.
- **`put` also updates the binding (PTH57).** After `put d.a.b.c = 1`,
  `d.a.b.c` reads the new value and `d.a.b.c === doc.a.b.c#`. If `d` is an
  unmodified snapshot its nodes *are* the context's nodes, so `put` creates
  the new spine once and re-attaches `d` to it — no copy at all, and
  `d === doc#` holds. If `d` has local edits, the new context nodes are
  spliced into `d`'s local spine: the snapshot is updated. The root binding
  must be `var`, since its reads change; `put` through a `let` is a compile
  error.
- **Plain `=` is a local write; copies keep their location, not their
  identity (PTH51v2, PTH52v2).** `var d = doc#` shares every node for
  reading; the first plain write through `d` copies the spine into `d`
  (S9.1.2). The copies **inherit the location** of the nodes they copy —
  `d.a.b.c` still knows it stands for `doc.a.b.c`, which is what lets a
  later `put d.b = 2` find `doc.b` — but carry **no generation**, so `&` is
  `null` and `===` is false. `doc.a.b.c#` is unaffected: two copies of one
  document must not alias through the context (S1.4). This is what keeps
  S1.6: for a uniquely owned value COW may write in place or copy, and the
  *generation* is the one datum that must never be minted on a copy; the
  location copies deterministically either way. `d.a.b.c.~~` as an
  expression still resolves (lineage is in the expression, S10.4.3v2), and
  a bound local copy resolves its parent through its carried location.
- **A location is loaded once per evaluation (PTH50).** Two forces with no
  write-through between them yield the same node. External changes are not
  observed until an explicit reload — document management outside this
  record; determinism (S13.4) is the reason.

Other evaluations, concurrent processes, and the file keep the unchanged
nodes and identities until the working generation is saved back
(PTH-O11). Read the document context as an editor buffer: the location is
where the node lives, the buffer holds this evaluation's latest generation,
and `#` reads the buffer.

**`input` is the same read (PTH53).** `input(p)` ≡ `p#`: it reads the
context's latest generation, so `input(doc)`, a write-through, `input(doc)`
returns two different roots. `input(p, params)` — a format, a schema — is a
**different document**, a separate context entry keyed by location plus
params; write-through on `p#` never touches it and vice versa. How `&`
spells a node of a parameterized document is PTH-O10.

This **revises the expectation SO39 recorded** (that a detach preserves the
*same* identity): a write-through is new data (S1.4) and new data is a new
generation; a local mutation is new data and has no identity. D2.6.8 keeps
its point — identity is never a raw address compared across contexts — and
loses its "data the container carries" half: identity is a relation the
document holds. The alternatives are recorded in §7.

### 4.7 Why a mandatory keyword (PTH58)

The pair `var d = doc#; d.a.b.c = 1` (local) against a write-through on the
same location can read as two versions of one document. Two designs were
weighed (the record is PTH-O12):

- an **optional** marker (`set doc#a.b.c = 1` beside `doc#a.b.c = 1`) —
  rejected: two spellings for one effect, and unenforced emphasis decays
  into style;
- a **mandatory** keyword with `=` never writing through — adopted, because
  it buys an invariant: **`=` has no external effect, ever**. Every
  write-through is a `put` statement, visible as such and confined to `pn`
  like every other effect.

`put` was preferred to `set`: a statement-head keyword cannot be a binding
name (S16.10.1), and `set` is the natural name for a set value beside the
`&`/`!` set operators. `put` frees the surface of the C-style asymmetry
too: instead of "a target that contains `#`", the rule is "a target of
`put`", and PTH57 closes the surprise the identity-targeting form would
otherwise have (a write through `d` that `d` does not see).

### 4.4 `temp.` documents (PTH44)

Runtime data gains identity by being placed in a document under the `temp.`
absolute root (S2.4.5v2), an in-memory provider with no new construct:

```lambda
output(v, temp.'scratch')            // store v as the document temp.'scratch'
let t = temp.'scratch'#              // force it: t's containers now have identity
&t.rows.2                            // temp.'scratch'.rows.2
```

Storing copies `v` into the document's arena (the same path an input parse
takes), so the stored nodes are document-owned and pointer-identified like
any other. The document lives as long as the evaluation's resolver holds it;
`temp.` references are meaningful only within that evaluation (cross-isolate
lifetime is SO20). Whether `output` to `temp.` is the only minting spelling
is PTH-O7.

### 4.5 `===` (PTH45)

> `a === b` ≡ `&a != null and &a == &b`, where a reference is location plus
> generation (§4.3). In practice it is pointer equality, which is sound
> because document nodes never move, a write-through always copies, and
> local copies carry no identity.

- A new operator, joining the S16.2.2v2 continuation set beside `==`.
- Identity-less operands compare **false**, never a compile error (S1.9:
  equality is total) — this settles the SO39 sub-question.
- `&a == &b` is path equality under S2.4.2v4 plus fragment equality. Rooted
  and absolute spellings of one target are distinct paths (PTH24), so `===`
  across a rooted and an absolute load of the same file is false — the
  price of context-free value equality (S5.1.4), recorded, not hidden.
- Two forces with no write-through between them yield the same node
  (PTH50), so `p#body.0 === p#body.0` is true; across a write-through it is
  false, because the generation differs.
- No `!==` is minted: `not (a === b)` (S10.3.1, words over sigils); PTH-O6
  records the option.
- No `ref_eq` and no address comparison exist (S5.1.4v2; D2.6.8).

### 4.6 What this closes

- **SO41** — a cross-reference in a document graph is `&node` stored as data
  and read back with `#`: the foreign-key reading of S9.1.5v2, now with both
  operations spelled.
- **SO39** — resolved as: identity is preserved by binding, argument passing,
  and any navigation whose result is a container; absent for scalar reads
  and constructors; **replaced** by a new generation on write-through and
  **dropped** by local mutation (revised expectation, §4.3); `===` on identity-less operands is false; the
  universal id form is the dotted path.
  Remaining: the id-attribute fragment spelling (PTH-O3) and which attribute
  is the id (PTH-O5).
- **DO25** — the carrier is the arena pointer plus the document's node table.

---

## 5. Loops, pipes, and the `~key` respell

### 5.1 Iteration takes a value (PTH46)

`for`, the mapping pipe, `that`, and every iteration form take a value; a
reference is a scalar that iterates as itself (S10.1.2). To iterate the
target, force it:

```lambda
for (v in p#) { … }
p# |> f(~)
refs |> ~#                 // a list of references → their targets
refs |> ~# that ~.ok
```

### 5.2 `~#` becomes `~key`

With `#` meaning force, `~#` reads as "force the current item" — and that
is the idiom above. S1.7 forbids the second reading, so the current key/index
accessor is respelled:

```lambda
['a', 'b'] |> {index: ~key, value: ~}
{a: 1}     |> {key: ~key, val: ~}
```

`~word` is the **focal-accessor family**: a fused token, `~` immediately
followed by an identifier; `~~` is its existing member (S10.4.2). An unknown
accessor is a compile error, never a binding lookup; `~ key` with a space
remains two tokens. The family was chosen over a sigil because it is an open
namespace that can never collide with a user binding (no binding starts with
`~`) or with an operator glyph (the sigil space is nearly spent), which is
the S12.3.7 forward-compatibility property applied to accessors, and
because S10.3.1 prefers words where chaining is not at stake. `~key` binds
on the same discipline as `~#` did (S10.1.2, S10.1.5, scoped by S10.1.3).

### 5.3 Options considered for the key accessor (record)

Four spellings were on the table on 2026-09-16; `~key` was chosen.

| Option | Reading | For | Against |
|---|---|---|---|
| `~.#` | "the `#` step of `~`" | keeps the `#` mnemonic for *key* | re-spends `#` as a step name the moment `#` means force — the S1.7 collision it was meant to escape; reads as `~.` then `#` |
| `~_` | "the slot of `~`" | one character | cryptic; `_` is an identifier character, so it lexes as `~` then a name; no room for siblings |
| `~@` | "where `~` is at" | good mnemonic; single free glyph; fused token like `~#` was | spends the last unused ASCII operator glyph on a niche accessor; a fused token still fixes what bare `@` may later mean |
| **`~key`** | "the key of `~`" | S10.3.1 words over sigils; an **open family** (`~word`) that can never capture a user binding or collide with an operator; `~~` already has this shape | one more token class in both parsers; two characters longer than a sigil |

The deciding property is the open namespace: a future accessor is a new
word after `~`, never a new glyph, which is the S12.3.7 forward-compatibility
rule applied to accessors.

---

## 6. Forcing a symbol (PTH47, PTH48)

A symbol "does not implicitly read bindings" (S2.4.3v3; S16.10.1's
`'edit'.x`). An explicit force needs a ruling on what it resolves against. A
URN needs a resolver mapping the name to a location; Lambda's is the
immutable evaluation resolver of S2.4.4.

> `'name'#` resolves `name` through the active resolver's identity/namespace
> table — a document's id table when the active base is a document, a
> provider's name table when it is a provider — and yields the identified
> value or `null`. It never reads a lexical binding, a module export, or the
> sys-func registry.

The lexical reading is rejected outright: S1.8 (strings are never code — a
name-to-binding lookup is the half of `eval` the ruling exists to exclude,
and it makes every rename a behaviour change); S2.4.3v3 (names are statically
qualified, and S1.6 demands the dynamic answer equal a static one, which a
lexical lookup has none of); S1.4 (a reflection primitive is how global
mutable state usually grows). Which attribute a document treats as its id is
the document model's business (PTH-O5).

---

## 7. Rejected alternatives

| Option | Why not |
|---|---|
| **`a -> b`** | Fuses force with member access (S1.7); needs a second spelling for "force, whole" (`-> ~`), which is the tell. Postfix force removes the need for it. |
| **Auto-force on `.` / `[]`** | `.` on a path is step append (S2.4.1v2); auto-force would also read a binding through a symbol (S16.10.1) and do I/O during address resolution (S2.4.4). |
| **Auto-force in `for` only** | A `for`-only implicit read (S1.7) and implicit-I/O site (S2.4.4). |
| **`@` as the force glyph** (first draft) | Sound, but `#` is the URI fragment glyph and makes `p#name` self-explanatory; `@` stays unspent. Reversed 2026-09-16. |
| **`ref(v)` for address-of** (first draft) | Withdrawn for `&expr` at the author's ruling; the dual-role line-start cost is recorded in §4.1 and accepted. |
| **`~.#`, `~_`, `~@`** for the key accessor | See §5.3 for the full comparison; `~key` won on the open-namespace property. |
| **Prefix `*p`** | `*` is spread in containers and the wildcard step in paths; `[*p]` would be ambiguous. |
| **Postfix `!`** | Infix set exclusion; §7.1 of Design_Syntax just removed its prefix role. |
| **`p()` as force** | A path resolving to a function is then ambiguous between force and call. |
| **Nominal `reference` supertype** | §2.2: forces every symbol operation to rule on paths. |
| **The same identity surviving a COW detach** | Would make the modified node and the saved node "the same node" while their contents differ, and would need the old identity copied into a carrier on every detach (DO25). R4 says mutation is new data. |
| **Forcing a location yields the saved original until save-back** (second draft) | Made `p#` stale inside the very evaluation that wrote through `p`; `#` attaches to the latest generation at forcing time (PTH49–PTH50). |
| **A COW detach mints a new identity** (third draft) | Ties identity to a representation event: for a uniquely owned value, copy-versus-in-place would decide `===` (S1.6). Generations come from write-through only; local copies have none (PTH52). |
| **Optional write-through marker** (`set` beside bare `=`) | Two spellings for one effect; unenforced emphasis. Adopted instead: mandatory `put`, `=` never writes through (§4.7). |
| **`set` as the keyword** | Bans `set` as a binding name (S16.10.1) next to the set operators; `put` chosen. |
| **`&n == &m` across a write-through** (third draft) | Made `===` weaker than `&` equality. With generations in the reference, `===` ≡ `&a == &b` again (PTH43v2). |
| **Identity on scalars** | An `Item` would need a manufactured identity per read; semantically a scalar is a value, not a place. |

---

## 8. Migration and conformance

- **`~#` → `~key`.** Two test scripts (`test/lambda/pipe_where.ls` and one
  other), `doc/Lambda_Expr_Stam.md` §"Key/Index Access", the cheatsheet, and
  the grammar token `current_expr`. `~#` becomes a syntax error whose
  diagnostic names `~key` (the S10.3.1v2 pattern for `where`).
- **`path ⊂ symbol`.** No fallout: the runtime already answers `false`;
  `is path` gains a resolvable type name.
- **Sort order.** Paths move after symbols; no known golden sorts a mixed
  list.
- **New tokens** `#` (postfix + fragment), `&` (prefix), `===`; no existing
  program spells any of them outside strings and comments.
- **S16 classes.** `#`, `===` → S16.2.2v3 continuation set; `&` → S16.2.3v3
  dual-role set. Both rulings need a v3 revision and the C parser's
  line-start tables updated in step.
- **Goldens for `p#`.** `p#` and `input(p)` print identically on both tiers
  (S1.6; tier parity is the standing gate).

---

## 9. Decision ledger

| ID | Ruling (proposed; author-confirmed 2026-09-16 where marked ✓) |
|---|---|
| **PTH30** | `path` is a scalar type disjoint from `symbol`; `reference` is the alias `symbol \| path`; S6.2.1 band `symbol < path < string`. |
| **PTH31** ✓ | References are lazy: nothing forces implicitly — not `.`, `[]`, `for`, the pipe, `that`, or any iteration form. |
| **PTH32** ✓ | `#` is the force step: postfix, member/index precedence, left-associative, chaining with `.`, `[]`, `./`, `.~~`; `p#.a` reads the target's member, `p.a#` forces the extended path. |
| **PTH33** ✓ | Fragment sugar: `p#name`, `p#123`, `p#'q'`, `p#[e]` ≡ `p#.name` etc.; no whitespace between `#` and the fragment. |
| **PTH34** ✓ | Forcing resolves the longest document-naming prefix through its provider, then navigates the rest in memory; `a.b#c.d` ≡ `a.b.c.d#` ≡ `a.b#.c.d` (S1.6). |
| **PTH35** ✓ | A forced member that holds a path is lazy: `p#cfg.link.x` extends it, `p#cfg.link#x` reads through it. |
| **PTH36** | Forcing a provider path is `input(p)` (pn-family, raises `T^E`, S7.4.5) plus trailing navigation; `exists(p)` and `input(p, format)` unchanged. |
| **PTH37** | Forcing an in-memory reference (`sys.*`, `temp.*`, a symbol) is a total read: absence is `null` (S7.1.1v3); forcing a non-reference is a static type error or `error()`. |
| **PTH38** | Address resolution left of `#` performs no I/O (S2.4.4 restated for the operator). |
| **PTH39** | `#` and `===` join the S16.2.2 continuation set (v3). |
| **PTH40** ✓ | `&expr` is address-of: the reference the operand carries, else `null`; prefix, binds looser than postfix; `&` joins the S16.2.3 dual-role set (v3). |
| **PTH41** ✓ | Only containers inside a document have identity; a navigation result that is a container keeps it, a scalar result has none; runtime-constructed data has none. |
| **PTH42** ✓ | Identity is the container's path within its document; parent/root navigation from a bound node resolves through the document's node table, never through pointers in the node. |
| **PTH43v2** ✓ | Identity is location plus generation (`doc.a.b.c@v0`, conceptual); the carrier is the node pointer plus the document's node table. A write-through stamps the nodes it creates with the document's next generation; untouched subtrees keep theirs and are the same nodes; generations are evaluation-local; other readers see the unchanged document (revises the SO39 expectation). |
| **PTH44** ✓ | `temp.` is the in-memory provider root; `output(v, temp.'name')` stores a document whose containers gain identity for the evaluation's lifetime. |
| **PTH45v2** ✓ | `a === b` ≡ `&a != null and &a == &b` with generations included; identity-less operands compare false; no `!==`, no address comparison across contexts. |
| **PTH46** | Iteration over a target is `for (v in p#)` / `refs \|> ~#`. |
| **PTH47** ✓ | The current key/index accessor is `~key` (options: §5.3); `~#` is retired. `~word` is the fused focal-accessor family (`~~` included); unknown accessors are compile errors. |
| **PTH49v2** ✓ | Write-through is the `put` statement: `put target = v` detaches the spine inside the document context and stamps the next generation; pn-family, statement position; the only operation that advances a location. Plain `=` never writes through; `doc#a.b.c = 1` is a compile error naming `put`. |
| **PTH51v2** ✓ | A binding of a forced value is a snapshot that shares nodes for reading; a plain `=` write through it copies locally (S9.1.2), never advances the location, and the copies **inherit their location**. |
| **PTH52v2** | Local copies carry a location but **no generation**, hence no identity: `&` is null, `===` false. The generation is the one datum never minted on a copy, so COW copy-versus-in-place is unobservable (S1.6). |
| **PTH55** ✓ | `=` has no external effect, ever; every write-through is spelled `put`. |
| **PTH56** ✓ | `put` locates its target by the reference (post-`#` steps re-appended, PTH34) or by the carried location of the target's `var`-rooted document-derived container; a target with no location is a static error or a raise. |
| **PTH57** ✓ | `put` updates the target's binding: an unmodified snapshot is re-attached to the new generation with no copy (`d === doc#`); a locally edited one has the new nodes spliced in; the root binding must be `var`. |
| **PTH58** ✓ | The keyword is `put`, mandatory, not `set` (binding-name capture) and not an optional marker (two spellings). |
| **PTH54** ✓ | `output(d, p)` ≡ `put p# = d` followed by a flush to the provider; save-back keeps the identity of nodes that still carry it. |
| **PTH53** ✓ | `input(p)` ≡ `p#` (reads the latest generation); `input(p, params)` is a distinct document in the context keyed by location plus params, independent of `p#`'s generations. |
| **PTH50** | A location is loaded once per evaluation: repeated forces with no write-through between them yield the same node; external changes need an explicit reload (document management). |
| **PTH48** | `'name'#` resolves through the resolver's identity/namespace table; it never reads a lexical binding, a module export, or the sys-func registry (S1.8, S2.4.3v3, S1.4). |

---

## 10. Open issues

- **PTH-O2 — grammar placement.** `#` as a member-tier postfix with the
  fragment production; `&` as a prefix at unary tier; `===` at the equality
  tier; `~key` as a token. Closed-tail rule (§7.14 of Design_Syntax): `p#`
  and `p#name` end in postfixable tokens, so a following `.ident` line
  continues (S16.2.4v2).
- **PTH-O3 — id-attribute fragment.** The positional path is the baseline
  identity for every format. Where a document model declares an id key
  (HTML `id`, `xml:id`, a schema key), a stable id-based fragment is wanted:
  its step spelling inside the dotted path is open. It blocks nothing in
  this record; `&` spells the positional form until ruled.
- **PTH-O4 — RESOLVED** by PTH41–PTH43 (identity-preserving operations).
- **PTH-O5 — which attribute is the id.** Owned by the input/document
  design (S5.1.4v2: identity is data a document assigns).
- **PTH-O6 — `!==`.** Not minted; `not (a === b)` under S10.3.1. Reopen if
  usage shows the negation is common.
- **PTH-O9 — printing and forcing a versioned reference.** Whether `&`
  prints the generation (`doc.a.b.c@v0` is conceptual; `@` is the free
  glyph) and what `doc.a.b.c@v0#` yields once v1 exists — `null` (the
  generation is gone) unless the context keeps history, which would be an
  undo log with unbounded memory. Recommendation: `null`; an unversioned
  location forces to the latest.
- **PTH-O10 — parameterized-input identity.** How `&` spells a node of an
  `input(p, params)` document (the URI query `?` is the analogue).
- **PTH-O11 — RESOLVED 2026-09-16.** `output(d, p)` is `put p# = d` plus a
  flush to the provider: the context write-through (next generation for the
  nodes that differ; nodes of `d` still carrying `p`-identity are the same
  nodes and keep it) followed by persistence. Recorded as PTH54.
- **PTH-O12 — a write-through keyword.** Raised 2026-09-16: `var d = doc#;
  d.a.b.c = 1` (local, S9.1.2) beside `doc#a.b.c = 1` (write-through) may
  read as two versions of one document, and `set d.a.b.c = 1` was floated
  to emphasise the effect. Options recorded:
  (a) **no keyword** — the snapshot rule is the one every `var` follows and
  `#` already marks the target; an *optional* keyword is a second spelling
  for one effect, and `set` as a statement head would ban `set` as a binding
  name (S16.10.1);
  (b) **mandatory keyword, `=` never writes through** — `doc#a.b.c = 1` is
  an error naming the keyword; the keyword targets by *identity*, so
  `put d.a.b.c = 1` writes to `&d.a.b.c`'s location and errors on an
  identity-less node; its surprise is that `d.a.b.c` still reads the old
  value afterwards, `d` being a snapshot.
  **RESOLVED 2026-09-16: (b), keyword `put`**, strengthened so that the
  binding also sees the write (PTH55–PTH58, §4.7).
- **PTH-O8 — `put` grammar.** A new statement `put target = v` whose target
  is a reference-rooted chain with `#` or a `var`-rooted member chain; the
  checker rejects it in `fn` (effect), rejects a `let` root (PTH57), and
  reports a location-less target statically where it can (PTH56). Whether a
  `put` may add a member the node's key domain does not admit follows
  S9.1.6 unchanged. `put` joins the barred binding names (Design_Syntax
  Appendix K).
- **PTH-O7 — minting a temp document.** `output(v, temp.'name')` is the
  proposed and only spelling; a dedicated constructor is not minted unless
  the provider form proves awkward.

---

## Appendix A — Implementation notes (brief)

- **Type.** `LMD_TYPE_PATH` exists and `fn_is` already answers `false` for
  `path is symbol`. PTH30 needs: `path` registered as a type-pattern name
  (today "unresolved type name 'path', using ANY"), the `reference` alias in
  the sys type table, the `path` band in the S6.2.1 comparator.
- **`#`.** Lowers to the existing `input` dispatch for provider paths and
  the existing `sys.*` resolution for system paths, followed by the ordinary
  member/index navigation on the result; the interpreter and MIR Direct
  share one forcing helper (rule 13), reached from the postfix-step lowering
  next to `./` and `.~~`. Longest-prefix split (PTH34) lives in the
  resolver: it already classifies the provider-owned prefix.
- **Identity.** No per-node storage. Each `Input`/document owns a node table
  keyed by container pointer giving `(parent, key)`; `&` walks it to the
  root. Pointer-to-document lookup is an arena-range test over the loaded
  documents (Input arenas are non-moving). Only write-through registers
  new containers (with the `(parent, key)` the detach already holds);
  a local detach registers nothing. `===` is pointer equality on both
  tiers.
- **Document context.** A per-evaluation table `(location, params) →
  (current root, generation counter)` plus the node table above, whose
  entries carry the generation stamp. `#` looks the location up (loading on
  first use, PTH50); write-through copies the spine within the context,
  stamps and registers the new nodes, and replaces the root entry. Old
  nodes stay valid while any value references them: arena-owned originals
  live for the Input's lifetime, written-through spines are GC-owned.
  A local COW copy (PTH51v2) is registered with its inherited location and
  **no** generation — the detach already holds `(parent, key)` — so `put`
  can find its location while `&` and `===` treat it as identity-less.
- **`put`.** Resolve the location (PTH56); detach the context spine and
  stamp; then re-attach or splice the binding (PTH57): if the binding's
  root pointer equals the context's previous root, rebind to the new root;
  otherwise replace the target node inside the binding's local spine.
- **`temp.`** A provider whose `output` copies the value into a fresh Input
  arena via `MarkBuilder` and registers its node table; `#` on a `temp.`
  path returns the root without parsing.
- **`~key`.** Replace the `current_expr` token `~#` with a `~` + identifier
  fused token whose identifier is validated against the accessor table
  (`key`, and `~~` as today); both parsers (`grammar.js`, `parse.c`).
