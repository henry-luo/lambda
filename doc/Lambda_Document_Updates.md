# Lambda Document Updates

Lambda reads documents with the force step `#`, gives the nodes inside them an identity, and changes them only through a small set of update statements — `put`, `del`, `output`, grouped into transactions with `open`, `commit` and `rollback`. This document describes that model: references and forcing, node identity, the three tiers of reads and writes, the update statements, transactions, and in-memory `temp.` documents.

> **Related Documentation**:
> - [Lambda Data](Lambda_Data.md#path-literals) — path literals, the value side of a reference
> - [Lambda Procedural](Lambda_Procedural.md) — `var`, assignment and value semantics (Tier 2)
> - [Lambda System Functions](Lambda_Sys_Func.md#inputoutput-functions) — `input`, `output`, `temp`

> **Status.** The model is designed in [`vibe/Lambda_Design_Reference.md`](../vibe/Lambda_Design_Reference.md) (rulings PTH30–PTH80) and implemented; its ratification into the formal semantics as S9.4 "Document updates" is pending, so this document cites the design IDs (`PTH#`). Differences between the design and the current build are marked **Not yet implemented**.

---

## Table of Contents

1. [References and the Force Step `#`](#references-and-the-force-step-)
2. [Identity: `&` and `===`](#identity--and-)
3. [Three Tiers](#three-tiers)
4. [Update Statements](#update-statements)
5. [Transactions: `open`, `commit`, `rollback`](#transactions-open-commit-rollback)
6. [The Next Version Is Write-Only](#the-next-version-is-write-only)
7. [Loops Keep Their Version](#loops-keep-their-version)
8. [`temp.` Documents](#temp-documents)
9. [Documents on Disk](#documents-on-disk)
10. [Rules and Errors](#rules-and-errors)

---

## References and the Force Step `#`

A `path` is a **reference**: it names a location and reads nothing. Building `p.a.b`, comparing paths, and passing them around never touches the store. Only the postfix **force step** `#` crosses from the reference to its target (PTH32, PTH34).

```lambda
let p = \.data.'page.json'   // a path; nothing is read
p.body.0                     // still a path: steps append
p#                           // the document at p (exactly `input(p)`)
p#body.0                     // force, then navigate the value
p.body.0#                    // extend the path, then force — the same value
```

Immediately after `#` one step may omit its dot, the URI fragment form: `p#name` is `p#.name`, `p#3` is `p#.3`. No space is allowed between `#` and its fragment step. Forcing a file or URL path is exactly `input(p)` with the format detected from the target (PTH53v2), so it raises on failure like `input` does.

`reference` is the type `symbol | path` — a URI is a URN or a URL:

```lambda
/.etc.hosts is path;       // true
/.etc.hosts is symbol;     // false — `path` is its own scalar type
/.etc.hosts is reference;  // true
'name' is reference        // true
```

## Identity: `&` and `===`

Containers **inside a document** carry an identity: their path within it. Prefix `&` reads that identity, or `null`; `===` compares two of them (PTH40–PTH42).

```lambda
let doc = \.'page.json'#;   // page.json holds {"a": {"b": {"c": 1}}, "title": "T"}
&doc.a.b;         // \.'page.json'.a.b — the node's path within its document
&doc.title;       // null — a scalar is a value, not a place
&{x: 1};          // null — runtime-constructed data has none
(&doc.a.b)#;      // {c: 1} — round trip: the reference forces back to the node

doc.a === doc.a;  // true
doc.a === doc.b;  // false
1 === 1           // false — identity-less operands compare false, never error
```

`&` binds looser than the postfix steps, so `&x.y` is `&(x.y)`, as in C. Because `&` is also the infix intersection operator, a line that *starts* with `&` after a complete statement needs a `;` on the line before (S16.2.3v3), as above.

> **Not yet implemented.** The round trip works for file and URL documents. For a `temp.` document, `&` returns the right reference (`temp.page.a.b`), but forcing it fails with error 400; address the node through the document instead, as `temp.'page'#a.b`.

## Three Tiers

A document has three tiers, and they do not mix (PTH55, PTH58–PTH70).

| Tier | Forms | Sees | Writes |
|---|---|---|---|
| 1 — read | `let`, `for`, `#`, `input`, pipes (`\|>`, `\|:`), `that` | the **head** version | nothing |
| 2 — value | `var`, `=`, copy-on-write | a snapshot taken at binding | the binding only, never a document |
| 3 — update | `put`, `del`, `output`; `commit`, `rollback`; `open { }` | nothing | the **next** version |

`=` never writes a document (PTH55). Every document write is one of three statements — `put`, `del`, `output` — so a reader finds them by scanning for three words.

## Update Statements

| Statement | Meaning |
|---|---|
| `put target = v` | Upsert at a location: a position is replaced, a key is inserted or updated |
| `put v into target` | Add a member: append to a sequence, upsert into a map, add an element child |
| `put v before node` / `put v after node` | Insert next to a node of a sequence |
| `del target` | Remove a location |
| `put a = 1, b = 2` | Comma-joined edits: one statement, applied in written order |
| `output(v, target)` | Write a whole document (see [Documents on Disk](#documents-on-disk)) |

A target is a reference with `#` (`put doc#a.b = 1`, `put doc.a#b = 1` and `put doc.a.b# = 1` name one location), the alias of an `open` block, or a `let`/`for` binding of a head node, plus member steps (PTH60v3, PTH69v2).

```lambda
let made = temp('inventory', {count: 1, stale: true, rows: [{id: 1}, {id: 2}]})^;
put temp.'inventory'#count = temp.'inventory'#count + 1;   // upsert
del temp.'inventory'#stale;                              // remove a key
put {id: 3} into temp.'inventory'#rows;                  // append to a sequence
let rows = temp.'inventory'#rows;
put {id: 0} before rows.0, {id: 4} after rows.2;         // insert around head nodes
temp.'inventory'#
```

```text
{
  count: 2,
  rows: [{
      id: 0
    }, {
      id: 1
    }, {
      id: 2
    }, {
      id: 3
    }, {
      id: 4
    }]
}
```

`before` and `after` anchor to a head **node**, so they find it however the sequence has shifted by the time the write set is applied (PTH71v2). The anchor must be a container: a scalar item such as a string has no identity, and anchoring to one raises error 201 (*'before' and 'after' need a document node inside a sequence*).

Update statements are procedure-family effects, like `output` (PTH55): inside a function, write them in a `pn`. At a script's top level each one runs as its own transaction — the shell case.

## Transactions: `open`, `commit`, `rollback`

Outside `open`, each update statement is its own transaction and commits at once. Inside `open`, every update statement forms **one write set**, applied by `commit` or at the end of the block (PTH62, PTH63v2, PTH66v2):

```lambda
let scratch = temp('scratch', {n: 1})^
open t = temp.'scratch' {
    put t.n = 5;
    put t.tag = 'x';
    commit                    // or: the block commits at its end
}
temp('scratch')               // {n: 5, tag: 'x'}
```

The `open` alias is the opened document, with `#` implied; `&t` recovers its address. `rollback` discards the write set, and an error that leaves the block rolls it back:

```lambda
pn fail() int^ { raise error("stop") }

pn change() int^ {
    open t = temp.'scratch' {
        put t.n = 77;
        fail()^                  // the error leaves the block: the write set rolls back
    }
    1
}

pn main() {
    let made = temp('scratch', {n: 1})^
    change() ^ { print("change failed: ", ^.message, "\n") }
    temp('scratch').n            // 1
}
```

```text
change failed:  stop 
1
```

## The Next Version Is Write-Only

Edits build a **write-only next version**. Nothing a reader can see changes until the commit, and every value operand reads the head — so two increments in one write set leave `head + 1`, not `head + 2` (PTH61):

```lambda
let counter = temp('counter', {n: 1})^
open c = temp.'counter' {
    put c.n = temp.'counter'#n + 1;
    put c.n = temp.'counter'#n + 1
}
temp.'counter'#n            // 2: both right-hand sides read the head
```

This is deliberate. Read-your-writes lives in Tier 2: build the new value in a `var`, which does see its own writes, then write it with one `put doc# = d`.

## Loops Keep Their Version

A commit **advances** what the next force yields, and nothing already bound changes (PTH50v3, PTH64v2). A loop keeps the version it started on, so it reaches every row even while each iteration commits:

```lambda
let made = temp('rows', {rows: [{n: 1}, {n: 2}]})^
for (r in temp.'rows'#rows) { put r.seen = true }
temp.'rows'#rows            // [{n: 1, seen: true}, {n: 2, seen: true}]
```

Outside `open` each `put` in the loop commits on its own — one version per iteration. Wrap the loop in `open` to make it one transaction.

## `temp.` Documents

Runtime data gains identity by being placed in a document under `temp.`, an in-memory provider whose documents live for the evaluation (PTH44v2, PTH76). `temp(name, content)` creates one and returns its head; it raises if the name is taken. `temp(name)` returns the existing head, creating an empty document when there is none.

```lambda
let t = temp('scratch', {rows: [1, 2]})^;
t.rows === temp.'scratch'#rows;          // true — the same node
temp('scratch');                         // the existing head: {rows: [1, 2]}
open s = temp.'scratch' { put s.rows = [3] }
temp('scratch')                          // {rows: [3]}
```

Creating a `temp.` document is immediate and outside any write set, like `io.mkdir`, so it is readable in the same evaluation without a `commit`.

## Documents on Disk

Forcing a file path reads the file (`p#` is `input(p)`), and update statements change the document that later forces see.

> **Not yet implemented.** A commit to a file-backed document updates the head that later forces in the same run see, but it does not write the file back to disk yet. To persist the result, write the head out with `output`:

```lambda
pn main() {
    let p = \.'settings.json'
    open d = p { put d.retries = 3 }
    output(p#, "settings.json")^            // persist the committed head
    input("settings.json", 'json')^        // {retries: 3, …}
}
```

## Rules and Errors

| Situation | Result |
|---|---|
| `doc#a.b = 1` (assignment to a forced location) | not an update: `=` never writes a document; use `put` |
| A `var` as the root of a target (`var t = …; put t.a = 2`) | compile error E200: a `var` is Tier 2; use `open v = <document> { put v… }` |
| `del` of a key the head does not have | error 200 when the write set is applied |
| `put v into` a location that does not exist | error 201: the target has no location |
| `before`/`after` anchored to a scalar | error 201: the anchor must be a document node inside a sequence |
| An error that leaves an `open` block | the write set rolls back |

`put`, `del`, `commit`, `rollback` and `open` are reserved as binding names. They remain legal as **data** names — a map key, a member, an element tag, an event name — so `{open: true}`, `m.open` and `<del "x">` are unchanged. `before`, `after` and `into` are clause words inside the statement and stay bindable (S16.10).
