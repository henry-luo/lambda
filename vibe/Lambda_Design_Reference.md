# Lambda Reference Type, Forcing, and Identity

**Date:** 2026-09-15; revised 2026-09-16 and 2026-09-17 (three-tier update model: CRUD statements build a write-only next version, `commit`/`rollback`, MVCC — a commit advances the head, bindings and cursors keep their version; the immediate write-through `put` of 2026-09-16 is superseded)
**Status:** PROPOSED — rulings taken in review, **not ratified** into the formal
specs, nothing implemented
**Decision IDs:** PTH30–PTH80 (extends the path/reference series whose home is
[`Lambda_Type_Path.md`](Lambda_Type_Path.md) §13)
**Open issue IDs:** PTH-O2–PTH-O18 (PTH-O4, PTH-O7, PTH-O9, PTH-O11, PTH-O12, PTH-O14, PTH-O16 resolved; PTH-O15 deferred)
**Scope:** the `reference` type (symbol ∪ path), the postfix `#` force step
and its fragment sugar, prefix `&` address-of, `===`, document-node identity
and its carrier, the three-tier update model (read / `var` value / CRUD +
transaction: `put` (`= v` / `v before` / `v after` / `v into`, comma-joined), `del`, `output`, `commit`, `rollback`, `open`), `temp.`
documents, and the `~key` focal accessor respell.
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
| §4.7 three tiers, CRUD statements, `commit`/`rollback`, cursors, `open` | new S9.4 "Document updates"; S14.3.1v2 (`#`/`input` no longer promise eager reading); S12 effect table (CRUD, `commit` are pn-family); S2.4.4v2 (`open` rebinds the relative base lexically) |
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

let n = doc.a.b.c#                    // Tier 1: reads the HEAD version
var d = doc#                          // Tier 2: a value snapshot; d.a = 1 is local (S9.1); put d.a = 1 is an ERROR
put doc.a.b.c#x = 1                   // Tier 3 OUTSIDE open: autocommits at once (shell style)
for (v in doc#) { put v.a = 0 }       // outside open: one commit per iteration; the loop keeps ITS version
open t = doc {                        // Tier 3 INSIDE open: one bounded transaction
    for (v in t) { put v.a = 0 }      //   same writes, one commit
    put x before t.items[0], y into t.items   // insert before a node; append; one statement
    commit                            //   explicit; or implicit at block end, rollback on error
}                                     // the head advanced: the next doc.a.b.c# yields the new node
n === doc.a.b.c#                      // false; &n = doc.a.b.c@v0, &(doc.a.b.c#) = doc.a.b.c@v1

&expr                                 // address-of: the node's reference, or null
a === b                               // reference equality: &a != null and &a == &b (versions included)
var t = temp('scratch', v)            // create an in-memory document now; t is a Tier-2 snapshot of its head
open t = temp('scratch') { … }        // or work on it in Tier 3; its address is temp.'scratch'

refs |> ~#                            // force each; ~key is the current key/index (was ~#)
```

The six rules:

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
> it is placed in a `temp.` document; scalars never have one. A generation
> is minted only by `commit`; a path is never attached to a node — `#`
> attaches at forcing time, to the **head** version.

> **R5 — The current key is `~key`.** `~#` is retired so that `#` means force
> everywhere (S1.7). `~word` is the focal-accessor family, of which `~~` is
> the existing member.

> **R6 — Three tiers.** Tier 1: `let`/`for`/`#` read the head. Tier 2:
> `var` values with copy-on-write (S9.1) — local, never a document write.
> Tier 3: `put`, `del`, `output` build a **write-only next version**;
> inside `open { }` it is one bounded transaction (`commit`, or implicit
> at block end), outside `open` every statement autocommits. A commit
> **advances** what the next force yields; every binding, including a
> running loop, keeps the version it was given (MVCC). The two versions
> never mix, and neither do the tiers' operators or roots: `=`, `push`, `splice`
> are Tier 2 only and a `var` is the only Tier-2 root; `put`, `del`,
> `output` are Tier 3 only and never take a `var` root.

Rejected (record in §7): `a -> b`, auto-forcing `.`/`[]`/`for`, `@` as the
force glyph, `ref()` as address-of, `~.#` / `~_` / `~@` as the key accessor,
the immediate write-through `put` (visible on the next force, updating its
binding), read-your-writes, a static ban on `commit` inside `for`, and both
extremes for a modified node — identity that survives a COW detach
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
  bytewise within the `path` band by canonical spelling (S2.4.2v5).
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

**Printing (PTH-O9, resolved 2026-09-17).** `&x` prints as its
**location** only — `server.dir.'page.html'.body.0` — and a printed
reference re-read is that location, which forces to the head. The
generation is not printed: the next version is never observable, so no
program can name a generation, and `@v` exists only in internal dumps and
logs. Consequently `&x#` for an `x` from an older generation yields the
head node, not `x`; `===` still distinguishes them.

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

### 4.3 Generations: `commit` mints identity, local mutation drops it (PTH43v2, PTH50–PTH52)

Document nodes are Input-arena owned, immutable, and non-moving (D4.3.1), so
**the container pointer is the identity carrier**: the owning document maps
the pointer to `(parent, key, generation)`, and `&` walks that table to the
root to spell the path. No header word — DO25's carrier question closes.

```lambda
let n = doc.a.b.c#       // Tier 1: the head node
put doc#a.b.c.x = 1      // Tier 3: the NEXT version gets a new c and spine; unreadable yet
doc.a.b.c#.x             // still the head value
commit                   // next → head; generation 1 stamped on the nodes the write set created
let m = doc.a.b.c#       // the new head node
n === m                  // false
&n, &m                   // doc.a.b.c@v0, doc.a.b.c@v1  (conceptual spelling, PTH-O9)
&(doc.a.b.z#)            // doc.a.b.z@v0 — untouched sibling, the same node

var d = doc#             // Tier 2: value snapshot (S9.1.2), shares every node for reading
d.a.b.c === m            // true — no copy has happened yet
d.a.b.c.x = 2            // LOCAL: COW copies the spine into d; the copies are plain values
&d.a.b.c                 // null — no identity, no location; &d.a.b.z is still doc.a.b.z@v0
doc.a.b.c#.x             // 1 — a Tier-2 write is never a document write
put d.a.b.c.x = 3        // ERROR: a var is never a CRUD root — use open (PTH69v2)
```

**A path is never attached to a node.** `doc.a.b.c` is a lazy reference
(R1); `#` attaches it at forcing time to the **head** version. The rulings:

- **Identity is location plus generation (PTH43v2).** A document keeps a
  commit counter; each `commit` stamps the nodes the write set created with
  the next value. The tag is per node, not per document: an untouched
  subtree keeps its old stamp and *is* the same node. Generations are
  evaluation-local, like `temp.` references (SO20). The `@v` spelling is
  conceptual; the printing format is PTH-O9.
- **The head is what the next force yields (PTH50v3).** `#` and `input`
  bind the head at the moment they run, as `let n = doc.a#` binds a node
  and `for (v in doc#)` evaluates its source once; `commit` **advances**
  the head, and nothing already bound changes — a version persists while
  anything references it (S9.1 applied to versions). A change made to the
  store by another process is not observed until this evaluation next
  commits or explicitly reloads; presenting that stable head and keeping
  a superseded version readable while a binding or cursor holds it is the
  store's business (PTH67v2).
- **Tier-2 copies are plain values (PTH51v3, PTH52v3).** `var d = doc#`
  shares every node for reading, so `&d.a` and `d.a === doc.a#` hold while
  nothing has been copied; the first plain write through `d` copies the
  spine into `d` (S9.1.2), and the copies carry **nothing** — no
  generation, no location — so `&` is `null` and `===` is false. Nothing
  must be minted on a copy: for a uniquely owned value COW may write in
  place or copy, and if a copy minted an identity that choice would decide
  `===` (S1.6). `d.a.b.c.~~` as an expression still resolves (lineage is
  in the expression, S10.4.3v2); a bound local copy has no parent. (A
  fourth draft had copies inherit their location so that `put d.b = 2`
  could find `doc.b`; with `put` on a `var` root banned, PTH69v2, that
  carrier has no user and is withdrawn.)
- **Next-version nodes are the same kind of thing (PTH61).** Nodes the
  write set builds carry a location and no generation until `commit`, and
  are unreadable until then; `rollback` drops them.

This **revises the expectation SO39 recorded** (that a detach preserves the
*same* identity): a committed write set is new data (S1.4) and new data is
a new generation; a local mutation is new data and has no identity. D2.6.8
keeps its point — identity is never a raw address compared across contexts
— and loses its "data the container carries" half: identity is a relation
the document holds. The alternatives are recorded in §7.

### 4.7 The three tiers: reading, values, document updates (PTH55, PTH58–PTH70)

Lambda's model of a document has three tiers, and they do not mix:

| Tier | Forms | Sees | Writes |
|---|---|---|---|
| 1 — read | `let`, `for`, `#`, `input`, pipes, `that` | the **head** version | nothing |
| 2 — value | `var`, `=`, COW (S9.1) | a snapshot taken at binding | the binding only; never a document |
| 3 — update | `put`, `del`, `output`; `commit`, `rollback`; `open { }` | nothing — the next version is **write-only** | the **next** version |

The design is modelled on relational CRUD and transactions: reads run
against the head as a snapshot, writes accumulate into a next version, and
`commit` swaps. Because a version is an immutable value, a Tier-1 loop can
never observe a half-built next version and the update-while-iterating
hazard cannot arise.

```lambda
put doc#count = doc#count + 1     // outside open: its own transaction, committed at once

open doc {                          // bounded transaction
    for (v in doc#) {               // opens a cursor on doc; v is a head node
        put v.a = val               // into the next version; v.a still reads the old value
        del v.stale                 // likewise
    }                               // cursor closed
    put first before doc#items[0], last into doc#items
    commit                          // next → head; generations minted; `doc#` now yields the new head
}                                   // (a block with no commit commits here; an unhandled error rolls back)

for (v in doc#) {                   // OUTSIDE open: legal — the loop iterates the version it forced
    put v.a = 1                     //   each put autocommits: one generation per iteration (use open
    doc.a#                          //   for one); a fresh force inside the body yields the NEW head
}

open d = server.dir.'data' {   // scoped transaction over the documents under data;
    let n = d.'cfg.json'.retries   //   d is the opened directory: # implied, reads see the head
    put d.'cfg.json'.retries = n + 1, d.'log.txt' = ""   // edit a child doc; create another
    let p = \.'cfg.json'           //   \. is relative to the target (S2.4.4v2); p is a lazy path
    commit                         // ends this write set; a new one begins
}                                  // block exit: commit or rollback per PTH66
```

The rulings:

- **`=` has no external effect, ever (PTH55).** Every document write is a
  CRUD statement, pn-family like `output` (S7.4.5), statement position
  only; `doc#a.b.c = 1` is a compile error whose diagnostic names `put`.
  A statement keyword was chosen over an optional marker (PTH58, record in
  PTH-O12): one spelling per effect, and `put` rather than `set` because a
  statement head cannot be a binding name (S16.10.1) and `set` is the
  natural name for a set value.
- **CRUD statements (PTH60v3).** `put target = v` upserts at a location;
  `put v before target` / `put v after target` insert at a node, `put v
  into target` adds to a container (§4.7.1); `del target` removes; `output(v, p)` is the whole-document `put p# = v`
  (PTH54v2). `before` and `after` are clause words inside the statement,
  like `in` in a `for` header, so they stay bindable (S16.10.1) — the
  reason they were preferred to an `ins` keyword. **Edits may be joined by
  commas into one statement**: `put x before v, v.a = 1, w into s` records
  the edits in written order, exactly as three statements would; `del a,
  b` likewise. `,` is the interior separator and `;`/newline closes the
  statement (S16 §7.11, §7.18), and a top-level comma can only separate
  clauses because every value form that contains one is bracketed. Value
  operands are ordinary expressions evaluated at the statement — they read
  the head. A `target` is a reference with `#` (post-`#` steps re-appended,
  PTH34 extended to writes: `put doc#a.b = 1`, `put doc.a#b = 1`, `put
  doc.a.b# = 1` name one location), an `open` alias (PTH75v2), or a
  Tier-1 binding of a head node — a `for` variable, a `let` — plus member
  steps (PTH69v2). **A `var` root is a compile error** naming `open`: a
  `var` is Tier 2, and a CRUD statement never changes what its target
  expression reads, so a `var` root could only mislead. A target with no
  location is a compile error where the type is static and a raise
  otherwise.
- **The next version is write-only (PTH61).** There is no
  read-your-writes: `put doc#n = doc#n + 1` twice in one write set leaves
  `n + 1`, because both right-hand sides read the head. This is deliberate.
  Read-your-writes lives in Tier 2: build the value in a `var`, which does
  see its own writes, then `put doc# = d`. Stating this in the reference
  manual is part of the ruling, or it will be filed as a defect.
- **`commit` and `rollback` (PTH62).** `commit` makes the next version the
  head and mints the generation (PTH43v2); `rollback` discards it. Both are
  statements, pn-family.
- **The write set is the enclosing `open` block; outside one, each
  statement is its own (PTH63v2).** Inside `open { }` all CRUD statements
  across all documents form one write set, committed by `commit` or at
  block exit (PTH66v2). Outside any `open`, a CRUD statement is a
  one-statement transaction that **autocommits at once** — the shell case:
  `put cfg#retries = 3` takes effect immediately, with no pending state
  to remember. At `commit` a write set is applied **in program order** and
  **validated then**: an operation the target's
  key domain rejects (S9.1.6 — a `put` whose parent is absent, a `put … before`
  onto a name target, a `del` of an absent location) makes `commit`
  raise and the whole set rolls back. Validation cannot be earlier: the
  statement site cannot read the next version, and an earlier statement
  may have created the parent.
- **Cursors keep their version; `commit` never waits and never raises
  for them (PTH64v2, MVCC).** An iteration over a forced document — `for`,
  a pipe, `that`, a comprehension — iterates the value its source
  expression produced, which is the version the force bound; a `commit`
  during the iteration advances the head for later forces and leaves the
  iteration untouched. A `for` body may therefore `put` and `commit`
  freely, inside or outside `open`; outside `open` each statement is its
  own commit, so a loop of writes costs one generation per iteration —
  `open` is the remedy for efficiency and atomicity, not for correctness.
  The obligation is on the implementation: a reader that defers reading
  must pin the version it started on, and a store that cannot keep a
  superseded version readable materialises eagerly (an S1.6 strategy
  choice). Waiting, as a database does on a lock, is not an option: the
  cursor holder and the committer are one thread of control, so a wait
  would deadlock; and raising (the previous draft, §7) punished eager
  readers for the sake of lazy ones. The set-oriented statement (PTH-O18)
  remains as the ergonomic and atomic form of a loop of writes.
- **Targets are head-anchored (PTH71).** Because only the head can be
  read, a target can only name a head node: where the head has a node at
  the target's location, the edit is recorded against that node and applied
  relative to it, however the sequence has shifted by then. Two `put …
  before v` on one node land in program order. A location the head lacks
  resolves to a pending node an earlier statement in the set created (`put
  doc#a.b = {}` then `put doc#a.b.c = 1`), else `commit` raises. Conflicting
  edits to one head node — `del v` then `put v.x = 1` — raise at `commit`;
  a later `put v = …` over an earlier one replaces it. The anchor is the
  node itself, so after a mid-loop `commit` an **untouched** node is still
  shared into the new head and an edit anchored to it stays valid however
  positions shifted, while a node an earlier commit **replaced** is no
  longer in the head: an edit anchored to it raises at `commit` as a
  **stale-anchor conflict** — the same optimistic first-committer-wins
  rule the store applies between evaluations (PTH67v2). This is what makes
  `for (v in doc#items) { put x before v; put v.a = 1 }` mean what it says.
- **The tiers do not share operators (PTH72v2, PTH73).** `=`, `push`,
  `splice`, and every other mutating sys function are **Tier 2 only**: on a
  `var` root they do what they do today, on any other root they remain an
  error — never a write-set edit in disguise. `put`, `del`, `output` are
  **Tier 3 only**: `doc#a.b.c = val` and `push(doc#items, v)` are compile
  errors naming `put`. The reason is auditability: a reader finds every
  document write in a `pn` body by scanning for three words, and never has
  to resolve a root's binding kind to know which version a write went to.
  (An earlier draft let `push` on a non-`var` root enqueue an edit; it was
  withdrawn on 2026-09-17 for giving no explicit CRUD signal.)
- **`#` does not promise eager reading (PTH65).** `#` turns a reference
  into what it refers to. How much of the target is materialised, and when,
  is the implementation's choice — per step included: `doc#a.b.c` may read
  only the first level of `doc` and keep `b.c` deferred — as long as every
  observed result is the same (S1.6).
  `input(p)` ≡ `p#` (PTH53v2) inherits this, so S14.3.1's "input is eager"
  becomes an implementation note (S14.3.1v2); `stream()` remains the
  explicitly lazy *plan* value. PTH64 is what keeps S1.6: an eager and a
  lazy reader raise at the same `commit`, because the cursor is semantic.
  The error *site* for content errors under deferred reading is PTH-O13.
- **Block exit (PTH66v2).** An `open` block requires an explicit `commit`
  or commits implicitly at its end; if it exits by an unhandled runtime
  error it **always** rolls back. Nothing is ever pending at evaluation
  end — outside a block every statement has already committed, and an
  evaluation cannot end inside a block except by an error — so the
  configurable end-of-evaluation default of the first draft is withdrawn
  (PTH-O16 resolved).
- **Persistence is the store's (PTH67).** `commit` advances the head of
  this evaluation's document context. Whether and when the document store
  persists a committed head, and how it presents a stable head between
  commits, are outside the language specification. The one requirement on
  a store is that requirement, in two halves: the head an evaluation reads
  must not change under it between its own commits, and a version a
  binding or cursor still holds must stay readable after a later commit
  (write-and-rename for files, retained arenas in memory; a store that
  cannot materialises eagerly). Between evaluations the store's conflict
  policy is its own; the natural pairing with snapshot reads is optimistic,
  first committer wins and the other raises.
- **Scoped transaction: `open` (PTH68v2, PTH74, PTH75).** `open target {
  … }` and `open v = target { … }`, where the target is a document, a set
  of documents, or a directory, open those documents for the block: the
  block is a transaction scope, its exit is a commit-or-rollback point
  under PTH66v2, and a `commit` inside it ends the current write set and
  begins a new one. This is the block form of the S14 `open()` scoped
  resource (block-exit auto-close). **The transaction scope is dynamic
  (PTH78)**: a `pn` called from inside the block contributes to the
  block's write set, as a stored procedure joins its caller's transaction,
  and `commit`/`rollback` with no transaction open raise. The relative
  base (PTH74) is lexical by contrast, because path resolution is static
  (S2.4.3v3). Two further effects:
  **(PTH74)** a single-document or single-directory target becomes the
  **active relative base** for the block, so `\.a.b` inside means
  `target.a.b` — a lexically scoped refinement of S2.4.4's immutable
  resolver (S2.4.4v2): nothing global mutates, the base is a property of
  the block; a document *set* has no single base and leaves it unchanged;
  **(PTH75v3)** the optional alias `v` is a **reference with `#` implied
  at the end of every navigation chain through it**: `v.a.b` ≡ `(&v).a.b#`,
  bare `v` ≡ `(&v)#`, `put v.a = x` targets the location `(&v).a`, and
  `&v` is the target reference itself. **The block also confines CRUD
  targets (PTH80)**: a `put`/`del`/`output` inside it whose target lies
  outside the opened document, set, or directory is a compile error where
  the target is a literal reference and a raise otherwise. Defining the
  alias on the reference rather
  than on the forced value is what makes a directory target work: a forced
  directory is its listing, an array of entry maps (§3.11 of
  [Markup_Formats_Support](../doc/Markup_Formats_Support.md),
  `lambda/input/input-dir.cpp`), on which `d.'cfg.json'` would be an
  invalid NameKey; as a reference chain it names the child document and
  forces it under the longest-prefix rule (PTH34). A relative path inside
  the block (`\.a.b`) is still a lazy reference and needs `#` to read: the
  alias is an address with a force attached, the path is an address. The
  target may be a reference or any location-carrying document value (a
  `temp(…)` result), taken through `&`. The `=` is a binding introducer, as
  in `let`, not an assignment. Nesting is **deferred** (PTH-O15).

- **Directories (PTH77).** A directory is a document whose forced value
  is its listing (read-only shape, `input(dir, 'dir')`) and whose CRUD key
  domain is `NameKey` → child document: `put d.'f.json' = v` creates or
  replaces a file, `del d.'f.json'` removes one, and `before`/`after`/
  `into` do not apply to a directory (no positions; a child is named).
  The `io.*` functions (`io.mkdir`, `io.delete`, `io.copy`, …) stay what
  they are — immediate store operations outside the write set, document
  management under PTH67 — and are not a second CRUD spelling (PTH-O17
  records the boundary).

#### 4.7.1 Insertion (PTH70v4)

`put target = v` names a location, and a location in a sequence is a
position, so `put items[3] = v` **replaces** position 3. Insertion is the
same keyword with a clause: `before`/`after` take a **node anchor**,
`into` takes a **container**:

| Statement | Node / position target `s[i]`, `v` | Container target `s`, `m`, `el` | Name target `m.k` |
|---|---|---|---|
| `put t = v` | replace the head node (`i == len` appends, S9.1.6) | replace the whole container | upsert key `k` (a new key appends in insertion order, S2.3.1) |
| `put v before t` | insert before the head node | — | — |
| `put v after t` | insert after the head node | — | — |
| `put v into t` | — | **add as a member**: sequence → append; map ← map value → upsert each key; element → a map value merges into the attributes, any other value appends as a child | — |
| `del t` | remove the head node | remove the container | remove key `k`; raises at commit if absent |

`before`/`after` always anchor to the **head**: `put x before v` names the
head node `v` however the next version has shifted by the time the set is
applied (PTH71); they need an existing node, so they cannot address an
empty container. `into` is total on containers and is what replaces `push`
in Tier 3; the value's kind selecting an element's face is the rule the
`<T attrs, content>` literal already uses (S2.1.3). A first draft also let
`before s`/`after s` on the sequence itself mean prepend/append; it was
withdrawn for `into` so that appending has one spelling and anchors are
always nodes.

`t` may be a `#` expression or a location-carrying node such as a `for`
variable, so `for (v in doc#items) { if (p(v)) put x before v }` inserts in
front of every matching head node; positions shift only in the next
version. Element
children take the sequence column, attributes the name column. There is
no strict insert (fail if the key exists); test the head first if needed.
A separate `ins` keyword was considered and dropped: it would bar `ins` as
a binding name, whereas `before`/`after` are clause words.

### 4.4 `temp.` documents (PTH44v2, PTH76)

Runtime data gains identity by being placed in a document under the `temp.`
absolute root (S2.4.5v2), an in-memory provider. Creation is a sys
function, **immediate**, outside the write set — it is document
management like `io.mkdir`, not a Tier-3 edit, which is what lets the
document be read in the same evaluation without a `commit`:

```lambda
var t = temp('scratch', v)     // create temp.'scratch' with head v; returns the head document
t.rows.2 === temp.'scratch'#rows.2    // true: t shares the head nodes (Tier 2, unmodified)
&t.rows.2                      // temp.'scratch'.rows.2
t.rows.2.x = 1                 // Tier 2: local copy; the document is unchanged
open t = temp('scratch') { put t.rows.2.x = 1; commit }   // Tier 3: the document changes
```

- `temp(name, content)` creates the document with `content` as its head
  and returns that head (a location-carrying value); it raises if the name
  exists. `temp(name)` returns the existing head, creating an empty
  document (root `{}`) if there is none. The pairing is the one `input(p)`
  and `p#` already have: the function acts, the dotted form `temp.'name'`
  addresses. (`temp` therefore heads both a call and a scheme root; the
  two are lexically distinct, `temp(` against `temp.`, and `sys.temp`, the
  OS temporary directory, is unrelated.)
- The document lives as long as the evaluation's document context holds
  it; `temp.` references are meaningful only within that evaluation
  (cross-isolate lifetime is SO20).
- `output(v, temp.'name')` remains available as the Tier-3 spelling, a
  whole-document `put` that becomes visible at `commit` (PTH54v2).

### 4.5 `===` (PTH45)

> `a === b` ≡ `&a != null and &a == &b`, where a reference is location plus
> generation (§4.3). In practice it is pointer equality, which is sound
> because document nodes never move, a `commit` always creates new nodes, and
> local copies carry no identity.

- A new operator, joining the S16.2.2v2 continuation set beside `==`.
- Identity-less operands compare **false**, never a compile error (S1.9:
  equality is total) — this settles the SO39 sub-question.
- `&a == &b` is path equality under S2.4.2v5 plus fragment equality. Rooted
  and absolute spellings of one target are distinct paths (PTH24), so `===`
  across a rooted and an absolute load of the same file is false — the
  price of context-free value equality (S5.1.4), recorded, not hidden.
- Two forces with no `commit` between them yield the same node (PTH50v2),
  so `p#body.0 === p#body.0` is true; across a `commit` that touched the
  node it is false, because the generation differs.
- No `!==` is minted: `not (a === b)` (S10.3.1, words over sigils); PTH-O6
  records the option.
- No `ref_eq` and no address comparison exist (S5.1.4v2; D2.6.8).

### 4.6 What this closes

- **SO41** — a cross-reference in a document graph is `&node` stored as data
  and read back with `#`: the foreign-key reading of S9.1.5v2, now with both
  operations spelled.
- **SO39** — resolved as: identity is preserved by binding, argument passing,
  and any navigation whose result is a container; absent for scalar reads
  and constructors; **replaced** by a new generation at `commit` and
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

## 6A. Prior art

Recorded 2026-09-17 for the record. Grouped by which part of the model each
system resembles; the last column says where Lambda differs.

### 6A.1 Write-only next version, current version readable, no read-your-writes

| System | What matches | Where Lambda differs |
|---|---|---|
| **VHDL / Verilog** | The operator split. VHDL variable assignment `:=` is immediate and local; signal assignment `<=` takes effect at the end of the delta cycle. Verilog's blocking `=` / non-blocking `<=` are the same pair. Every right-hand side reads the current state, so `a <= b; b <= a` swaps — "the two versions do not mix" with two distinct operators (PTH55, PTH61, PTH72v2). | Lambda's boundary is `commit`/block end, not a simulation cycle; targets are locations in documents, not signals. |
| **Bloom / Dedalus** (temporal Datalog) | `<=` writes now, `<+` inserts and `<-` deletes at the *next* timestep; next-timestep facts are invisible until it begins (PTH61). | Lambda has no logical time; the "next timestep" is the explicit transaction. |
| **TLA+** | Unprimed `x` is the current state, primed `x'` the next; an action relates the two and never reads `x'` as a current value — the `@v0`/`@v1` reading of PTH43v2. | TLA+ constrains `x'`; Lambda builds it. |
| **XQuery Update Facility** | The pending update list: insert/delete/replace/rename accumulate and are applied atomically at the end of the query; no expression observes it. The nearest ancestor of the write set (PTH63v2). | XQUF applies at query end only; Lambda has explicit `commit`, bounded `open` blocks, and per-statement autocommit outside them. |
| **Firestore batched writes, Ecto.Multi** (Elixir), **Terraform** plan/apply | A write-only batch that cannot be read, committed as a unit; plan builds the next state, apply commits. | Same shape; Lambda's batch is implicit in the block. |

### 6A.2 Versioned immutable store, identity = location + revision

| System | What matches | Where Lambda differs |
|---|---|---|
| **Datomic** | The database is a value; reads take a snapshot; a transaction yields the next value; identity is entity + time, with `as-of` for old generations (PTH43v2, PTH50v2). | Lambda does not expose old generations at the surface (PTH-O9: `@v` is internal). |
| **Irmin** (OCaml), **Dolt**, **TerminusDB** | Git-shaped stores: a node's identity is its path at a commit. Subversion's peg revision `path@rev` is the `@v` spelling almost literally. | Lambda's generation is evaluation-local (SO20); no branches or merges. |
| **GemStone/S**, **Core Data** managed-object contexts | A context holds changes invisible to other contexts until `save`, with rollback (PTH67, PTH66v2). | Both allow read-your-writes inside the context; Lambda does not (PTH61). |

### 6A.3 Set-oriented update statements

| System | What matches | Where Lambda differs |
|---|---|---|
| **SQL** | `UPDATE … WHERE`, `DELETE … WHERE`; statement-level snapshot; Halloween-problem guards. | Lambda's statement is a `for` header (PTH-O18). |
| **Cypher** (Neo4j) | `MATCH … WHERE … SET …` / `MATCH … WHERE … DELETE …` — clause for clause the `for … where … put/del` form. | — |
| **SPARQL Update** | `DELETE … INSERT … WHERE …`. | — |
| **jq** | `.items[] \|= f`, `del(.a.b)`, `setpath`: path-addressed edits on an immutable document yielding the next document. | jq returns the next document as a value; Lambda commits it to a location. |
| **JSON Patch** (RFC 6902) | add/remove/replace/move/copy applied in order, `test` as validation — a write-set format (PTH63v2). Its known pitfall, array indices shifting under earlier inserts, is the reason for head anchoring (PTH71). | JSON Patch chose sequential application and lives with the pitfall; Lambda anchors every edit to a head node. |

### 6A.4 Cursor stability

| System | What matches | Where Lambda differs |
|---|---|---|
| **Prolog** logical update view | A running predicate iterates the clause set as it was at call time, however `assert`/`retract` interleave (PTH64v2). | The same answer: the iteration keeps its version. Lambda's version is a value, so no freezing machinery is needed. |
| DB **cursor stability** isolation; SQL engines' Halloween-problem guards | The same concern at the statement level. | — |

### 6A.5 Tier 2 on its own

| System | What matches | Where Lambda differs |
|---|---|---|
| **Swift** | `let` / `var` with copy-on-write value types — Tier 1 and Tier 2 exactly (S9.1). | No document tier. |
| **Immer** (JavaScript) | `produce(base, draft => …)` builds the next version by structural sharing while `base` is untouched (PTH51v3). | The draft is readable (read-your-writes); Lambda's next version is not. |
| **Clojure** persistent data, `db` values | Structural sharing; the database as a value. | Clojure STM (`dosync`/`alter`) and Haskell STM both read their own writes. |

### 6A.6 What has no known precedent

The combination — three tiers that share no operators or roots, a
write-only next version, and MVCC where the versions are ordinary
immutable values so that a cursor keeps its version with no extra
machinery — is not known in one system. The nearest
whole-system relatives are VHDL for the operator discipline and Datomic for
the identity model.

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
| **Forcing a location yields the saved original until save-back** (second draft) | Made `p#` stale inside the very evaluation that wrote through `p`; `#` attaches to the head at forcing time (PTH50v2). |
| **Immediate write-through `put`, visible on the next force and updating its binding** (fourth draft, PTH49v2/PTH57) | Superseded 2026-09-17 by the three-tier model: writes build a write-only next version and become visible only at `commit` (PTH61–PTH62). The binding-update rule existed only to hide stale reads that the tier model now makes explicit. |
| **`put v before s` / `put v after s` on the sequence itself as prepend/append** (fifth draft) | Two spellings for append once `into` exists, and a positional anchor should be a node; `into` is also total on empty containers (PTH70v4). |
| **`ins` keyword for insertion** | A statement head bars a binding name (S16.10.1); `put v before t` / `put v after t` use bindable clause words and keep one keyword per effect (PTH70v2). |
| **Sequential application of the write set against the evolving next version** | Positional insertion would shift indices under later statements in the same set; head-anchored targets (PTH71) make `put x before v; put v.a = 1` mean what it says. |
| **Mutating sys functions enqueue write-set edits on a non-`var` root** (fourth draft, PTH72) | No explicit CRUD signal at the call site; withdrawn for the clear cut `=`/`push`/`splice` = Tier 2, `put`/`del`/`output` = Tier 3 (PTH72v2). Append is `put v into s`. |
| **`put` on a `var` root** (fourth draft, PTH56/PTH69) | Mixes the tiers at the root: a Tier-2 value naming a Tier-3 target. Banned (PTH69v2); `open` is the spelling. |
| **Tier-2 copies inherit their location** (fourth draft, PTH51v2) | Existed only to serve `put` on a `var` root; withdrawn with it (PTH51v3). |
| **`open` alias binds the lazy reference** (fifth draft, PTH75) | `#` is implied on an `open` target, so the alias is the opened document; the lazy address is `\.…` or `&v` (PTH75v2). |
| **`=` on a reference root as a `put` in disguise** | The tier of a write would depend on the root's binding kind — the confusion PTH-O12 exists to remove; PTH55/PTH73 keep `=` local. |
| **One evaluation-wide pending write set** (sixth draft, PTH63/PTH66) | Shell-style single statements would accumulate an invisible pending transaction; replaced by bounded `open` blocks plus per-statement autocommit outside them (PTH63v2). |
| **`commit` raises while a cursor is open; a dangling CRUD statement under a cursor raises** (seventh draft, PTH64/PTH79) | Withdrawn 2026-09-17 for MVCC: a commit advances what the next force yields, and a running loop keeps the version it forced, as every `for` keeps its source value. Waiting would deadlock (one thread of control); raising punished eager readers for the sake of lazy ones. |
| **Deferring a dangling statement's autocommit until the outermost cursor closes** | Moot under MVCC (PTH64v2). |
| **Read-your-writes in the write set** | The two versions would mix; Tier 2 (`var`) already provides it, and a write-only next version is what makes cursors trivially stable (PTH61). |
| **Static ban on `commit` inside `for`** | Undecidable in general (the loop source may be a `let` bound earlier); a run-time raise on an open cursor is deterministic and complete (PTH64). |
| **`#` / `input` as an eagerness promise** | Dereference is a semantic act; buffering is representation. The semantic cursor (PTH64) keeps S1.6 either way (PTH65). |
| **Implicit rollback at evaluation end by default** | RDB autocommit is the expected default; rollback stays the unconditional rule under an unhandled error (PTH66). |
| **A COW detach mints a new identity** (third draft) | Ties identity to a representation event: for a uniquely owned value, copy-versus-in-place would decide `===` (S1.6). Generations come from write-through only; local copies have none (PTH52). |
| **Optional write-through marker** (`set` beside bare `=`) | Two spellings for one effect; unenforced emphasis. Adopted instead: CRUD statement keywords, `=` never writes a document (PTH55). |
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
- **New statement keywords** `put`, `del`, `commit`, `rollback`, and the
  block form of `open`: barred as binding names (S16.10.1, Design_Syntax
  Appendix K); `before`/`after` are clause words and stay bindable.
  `commit` and `del` are plausible variable names in existing scripts;
  grep before ratification. `into` joins `before`/`after` as a bindable
  clause word.
- **Mutating sys functions on a non-`var` root** stay errors (PTH72v2);
  their diagnostic gains "use `put`". No existing passing script is
  affected.
- **`open` as a block form** with an optional `v =` alias (the opened
  document, `#` implied) and a relative base rebinding (PTH74–PTH75v2):
  S2.4.4 gains a v2 note.
- **S14.3.1v2.** `input()` loses its eagerness promise; goldens that depend
  on the *site* of a content error may move (PTH-O13).
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
| **PTH40** ✓ | `&expr` is address-of: the reference the operand carries, else `null`; prefix, binds looser than postfix; `&` joins the S16.2.3 dual-role set (v3). Prints as the location only; the generation is internal (PTH-O9). |
| **PTH41** ✓ | Only containers inside a document have identity; a navigation result that is a container keeps it, a scalar result has none; runtime-constructed data has none. |
| **PTH42** ✓ | Identity is the container's path within its document; parent/root navigation from a bound node resolves through the document's node table, never through pointers in the node. |
| **PTH43v2** ✓ | Identity is location plus generation (`doc.a.b.c@v0`, conceptual); the carrier is the node pointer plus the document's node table. `commit` stamps the nodes the write set created with the document's next generation; untouched subtrees keep theirs and are the same nodes; generations are evaluation-local; other readers see the unchanged document (revises the SO39 expectation). |
| **PTH44v2** ✓ | `temp.` is the in-memory provider root; its documents live for the evaluation and their containers carry identity. |
| **PTH76** ✓ | `temp(name, content)` creates `temp.name` immediately (outside the write set) with `content` as head and returns the head document, raising if it exists; `temp(name)` returns the existing head or creates an empty one. `var t = temp(…)` is a Tier-2 snapshot; `open t = temp(…) { }` works on it in Tier 3. |
| **PTH77** ✓ | A directory is a document whose forced value is its listing and whose CRUD key domain is NameKey → child document (`put d.'f' = v` creates/replaces, `del d.'f'` removes; no positional clauses); `io.*` remain immediate store operations outside the write set. |
| **PTH45v2** ✓ | `a === b` ≡ `&a != null and &a == &b` with generations included; identity-less operands compare false; no `!==`, no address comparison across contexts. |
| **PTH46** | Iteration over a target is `for (v in p#)` / `refs \|> ~#`. |
| **PTH47** ✓ | The current key/index accessor is `~key` (options: §5.3); `~#` is retired. `~word` is the fused focal-accessor family (`~~` included); unknown accessors are compile errors. |
| ~~PTH49v2~~ | Superseded 2026-09-17 by PTH60–PTH62: `put` builds the next version, which becomes visible only at `commit`. |
| **PTH51v3** ✓ | A `var` binding of a forced value is a snapshot that shares nodes for reading; a plain `=` write through it copies locally (S9.1.2) and never touches a document. |
| **PTH52v3** ✓ | Local copies carry nothing — no generation, no location: `&` is null, `===` false. Nothing is minted on a copy, so COW copy-versus-in-place is unobservable (S1.6). |
| **PTH55** ✓ | `=` has no external effect, ever; every document write is a CRUD statement. |
| ~~PTH56~~ | Superseded by PTH69 (no `var` root requirement). |
| ~~PTH57~~ | Dropped 2026-09-17: a CRUD statement never changes what its target expression reads (PTH61). |
| **PTH58** ✓ | The update keyword is `put`, not `set` (binding-name capture) and not an optional marker (two spellings). |
| **PTH54v2** ✓ | `output(d, p)` ≡ `put p# = d` (a whole-document entry in the write set); persistence is the store's (PTH67). |
| **PTH53v2** ✓ | `input(p)` ≡ `p#` (reads the head); `input(p, params)` is a distinct document in the context keyed by location plus params. |
| **PTH50v3** ✓ | The head is what the next force yields; `#`/`input` bind it at the moment they run, `commit` advances it, and nothing already bound changes; external changes are unobserved until the next commit or an explicit reload. |
| **PTH59** ✓ | Three tiers: Tier 1 (`let`/`for`/`#`/`input`) reads the head; Tier 2 (`var`, `=`, COW) is local value semantics and never a document write; Tier 3 (CRUD statements + transaction) builds the next version. The tiers do not mix. |
| **PTH60v3** ✓ | CRUD statements `put target = v` (upsert), `put v before target` / `put v after target` (insert), `del target` (delete), `output(v, p)` (whole-document put): pn-family, statement position; value operands read the head; `before`/`after` are bindable clause words; edits may be comma-joined into one statement in written order. |
| **PTH61** ✓ | The next version is write-only: no read-your-writes; it becomes readable only when `commit` makes it the head. Read-your-writes is Tier 2's job. |
| **PTH62** ✓ | `commit` makes the next version the head and mints the generation; `rollback` discards it. |
| **PTH63v2** ✓ | Inside `open { }` all CRUD statements across all documents form one write set; outside any `open` each statement is a one-statement transaction that autocommits at once. A write set is applied in program order and validated at `commit`, atomically: any rejected operation raises and rolls back the whole set. |
| **PTH64v2** ✓ | MVCC: an iteration over a forced document iterates the version its source bound; `commit` advances the head for later forces and never waits or raises for a cursor; a deferred reader pins its version. |
| **PTH65v2** ✓ | `#` turns a reference into what it refers to; how much is materialised and when — per step included — is representation (S14.3.1v2); the semantic cursor keeps S1.6. |
| **PTH66v2** ✓ | An `open` block commits explicitly or implicitly at its end, and always rolls back on an unhandled runtime error; nothing is pending at evaluation end (the configurable default is withdrawn). |
| **PTH67v2** ✓ | Persistence is outside the spec: `commit` advances this evaluation's head; the store decides durability, must present a stable head between the evaluation's commits, and must keep a version readable while a binding or cursor holds it. |
| **PTH68v3** ✓ | `open target { … }` / `open v = target { … }` (document, document set, directory) is a bounded transaction: explicit `commit` or implicit at block end, rollback on error; a `commit` inside ends the write set and begins a new one. Nesting deferred, write confinement open (PTH-O15). |
| **PTH78** | Transaction scope is dynamic: a `pn` called inside an `open` block joins its write set; `commit`/`rollback` with no transaction open raise. The relative base (PTH74) is lexical. |
| **PTH80** ✓ | `open` controls which CRUD targets are allowed: inside a block every `put`/`del`/`output` target must lie within the opened document, set, or directory — checked statically where the target is a literal reference, at run time otherwise; outside any block any target is allowed. |
| ~~PTH79~~ | Withdrawn 2026-09-17 under MVCC (PTH64v2): a loop of writes outside `open` is legal, one commit per iteration. |
| **PTH74** ✓ | A single-document or single-directory `open` target is the active relative base for its block (`\.a.b` ≡ `target.a.b`), a lexically scoped refinement of S2.4.4 (v2); a document set leaves the base unchanged. |
| **PTH75v3** ✓ | The `open` alias is a reference with `#` implied at the end of every navigation chain through it (`v.a.b` ≡ `(&v).a.b#`, bare `v` ≡ `(&v)#`); `put v.a = x` targets `(&v).a`; `&v` is the target reference; the target may be a reference or a location-carrying document value; relative paths in the block stay lazy; the `=` is a binding introducer. |
| **PTH69v2** ✓ | A CRUD target is a reference with `#` (post-`#` steps re-appended, PTH34), an `open` alias, or a Tier-1 binding of a head node (`let`, loop variable) plus member steps; a `var` root is a compile error naming `open`; a location-less target is a static error or a raise. |
| **PTH70v4** ✓ | Insertion is `put v before t` / `put v after t` around a head node, and `put v into t` as membership into a container (sequence append; map value into map upserts keys; element takes a map into attributes, anything else as a child); `put t = v` replaces/upserts; `del t` removes (absent → raises at commit); no strict insert; `ins`, `push`, and `before`/`after` on a bare sequence withdrawn. |
| **PTH71v2** ✓ | Write-set targets, including `before`/`after` anchors, are anchored to head nodes; an edit anchored to a node a later commit replaced raises at `commit` as a stale-anchor conflict: an edit is recorded against the head node at its location and applied relative to it; a location the head lacks resolves to a pending node created earlier in the set, else raises; conflicting edits to one head node raise at commit, a later whole-node `put` replaces an earlier one. |
| **PTH72v2** ✓ | The tiers share no operators: `=`, `push`, `splice` and every mutating sys function are Tier 2 only (error on a non-`var` root, naming `put`); `put`, `del`, `output` are Tier 3 only. |
| **PTH73** ✓ | `=` never writes a document: `doc#a.b.c = val` is a compile error naming `put`; on a non-`var` root `=` is an error, never an implicit `put` (restates PTH55). |
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
- **PTH-O9 — RESOLVED 2026-09-17.** `&` prints the location only; `@v`
  is internal (dumps, logs); a printed reference re-read forces to the
  head. No program can name a generation, so the stale-force question
  does not arise at the surface.
- **PTH-O10 — parameterized-input identity.** How `&` spells a node of an
  `input(p, params)` document (the URI query `?` is the analogue).
- **PTH-O11 — RESOLVED 2026-09-17.** `output(d, p)` is `put p# = d`, a
  whole-document entry in the write set (PTH54v2); persistence is the
  store's (PTH67).
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
  **RESOLVED 2026-09-16: (b), keyword `put`.** The 2026-09-16
  strengthening (the binding sees the write) was dropped on 2026-09-17 when
  the three-tier model made the next version write-only (PTH61).
- **PTH-O8 — CRUD statement grammar.** `put target = v`, `put v before
  target`, `put v after target`, `put v into target`, `del target`, `commit`, `rollback`, and the
  block form `open target { }`;
  the checker rejects them in `fn` (effect) and reports a location-less
  target statically where it can (PTH69v2) and rejects a `var` root. Key-domain admission follows
  S9.1.6, checked at commit (PTH63). `put`, `del`, `commit`, `rollback`,
  `open` join the barred binding names (Design_Syntax Appendix K);
  `before`/`after`/`into` do not.
- **PTH-O13 — error site under deferred reading.** With PTH65 a content
  error (a parse failure deep in a file) may surface at the first read
  that needs it rather than at the force. Recommendation: the force checks
  reachability (existence, permission) and raises there; content errors
  raise at the reading site under the same `T^E` contract. Needs a ruling
  because handlers are site-specific.
- **PTH-O14 — RESOLVED 2026-09-17.** Moot under MVCC (PTH64v2): `commit`
  never blocks on a cursor.
- **PTH-O15 — `open` nesting: DEFERRED 2026-09-17.** A nested `open` is a
  compile error until ruled (savepoints, or joining the outer write set;
  an inner base would shadow lexically, PTH74). Write confinement is ruled
  (PTH80).
- **PTH-O16 — RESOLVED 2026-09-17.** Withdrawn with PTH66v2: nothing is
  pending at evaluation end, so there is no default to configure.
- **PTH-O7 — RESOLVED 2026-09-17.** `temp(name, content)` is the
  constructor (PTH76); `output(v, temp.'name')` remains the Tier-3
  spelling. The provider form alone proved awkward: a Tier-3 write is
  unreadable until `commit`.
- **PTH-O18 — set-oriented update statement (raised 2026-09-17, open to
  design).** A third `for` form beside the comprehension and the block
  statement: a CRUD clause on the header and no body, the morph of SQL
  `UPDATE … WHERE` / `DELETE … WHERE` and the Lambda `for`:

  ```lambda
  for (v in doc#items) where v.stale put v.a = 1, v.b = f(v);
  for (v in doc#items) where v.stale del v;
  ```

  Semantics that follow from the rulings above and need no new ones: the
  header iterates the head under a cursor (PTH64); each element yields the
  listed edits, head-anchored (PTH71), values reading the head (PTH61);
  the statement is one transaction outside `open` (PTH63v2) that commits
  when it ends, and joins the enclosing write set inside `open`. Under MVCC
  (PTH64v2) it is no longer *needed* to write inside a loop; it is the
  ergonomic and atomic spelling (one generation instead of one per
  iteration). `where` is already a `for`-header
  clause word; `put`/`del` would join it in that position. **Ruled
  2026-09-17: `del` takes the clause form shown** (`for (v in c) where p
  del v;`), and **a header may carry both kinds of clause, comma-joined**
  as in a plain `put` (`… put v.a = 1, del v.tmp`; confirmed 2026-09-17).
  **The header is the existing `for` header, unchanged** (`_loop_head` in
  `grammar.js`: several loop declarations, `let` clauses, then `where`,
  `group by`, `order by`, `limit`, `offset`), so intermediate values and
  multiple sources are already provided and `order by … limit` gives the
  SQL `UPDATE … ORDER BY … LIMIT` form for free. What remains is only the
  grammar attachment: the CRUD clauses follow `for_clauses` in place of a
  body, and `;`/newline closes the statement. Note `into` is already the
  `group by … into name` collector; `put v into s` uses it in the same
  destination sense and the two cannot collide (one follows `group by`,
  the other `put <expr>`). The block-bodied `for` with
  `put` inside stays legal only within `open`.
- **PTH-O17 — `io.*` versus the write set.** `io.delete`, `io.copy`,
  `io.mkdir`, … act on the store immediately and are not transactional;
  `put`/`del` on a directory are. Whether an `io.*` call inside an `open`
  block on a document with pending writes should raise, like `commit`
  under an open cursor, is open. Recommendation: raise.

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
  documents (Input arenas are non-moving). Only `commit` registers new
  containers with a generation (with the `(parent, key)` the apply step
  holds); a local detach registers nothing. `===` is pointer equality on both
  tiers.
- **Document context.** A per-evaluation table `(location, params) →
  (head root, generation counter)` plus the node table
  above, whose entries carry the generation stamp. `#` looks the location
  up (loading on first use, PTH50v2). A local COW copy (PTH51v3) is not
  registered at all; only head and committed nodes are in the table.
- **Write set.** A log of `(anchor, op, value)` — the anchor is a node pointer; at `commit` an anchor not reachable from the current head is a stale-anchor conflict (PTH71v2) — in program order owned by
  the innermost open transaction — the dynamic `open` block, or a
  one-statement transaction created and committed by the statement itself
  outside any block (PTH63v2) — where the anchor is the head node's pointer when the head
  has one at the target location and otherwise the pending node created by
  an earlier entry (PTH71); a comma-joined `put` appends one entry per
  clause in written order (PTH60v3). `commit`: check every document in the log has zero open
  cursors (PTH64), else raise; apply the log in order onto COW copies of
  the affected heads, validating key domains as it goes (S9.1.6); on the
  first rejection discard the copies and raise; otherwise stamp the created
  nodes, register them, swap the head roots, clear the log, and notify the
  store (PTH67). Old heads stay valid while any value references them:
  arena-owned originals live for the Input's lifetime, committed spines are
  GC-owned. `rollback` clears the log.
- **Cursors.** Nothing to count: an iteration holds the version it
  forced as an ordinary value (arena-owned, GC-retained). A reader that
  defers reading (PTH65v2) must hold its version's snapshot — for a file
  store an open descriptor on the pre-commit inode, since commit replaces
  by write-and-rename.
- **`temp.`** A provider whose `temp(name, content)` copies the value into
  a fresh Input arena via `MarkBuilder`, registers its node table, and
  installs it as the location's head at once; `#` on a `temp.` path
  returns the root without parsing; Tier-3 writes to it go through the
  ordinary write set.
- **`&` printing.** The formatter emits the location; the generation is
  emitted only by the debug dump (`temp/mir_dump.txt` family) and
  `log_debug`.
- **`~key`.** Replace the `current_expr` token `~#` with a `~` + identifier
  fused token whose identifier is validated against the accessor table
  (`key`, and `~~` as today); both parsers (`grammar.js`, `parse.c`).
