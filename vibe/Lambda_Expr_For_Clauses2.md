# Lambda For-Expression Clauses 2 — `group by` and join `on`

**Status:** settled design — ready to implement
**Date:** 2026-07-10
**Context:** extracted from `Lambda_Design_Data_Processing.md` (§4.1, §5.1) as the *settled, implement-first* subset of the relational layer. This doc covers only the **for-expression surface**: the `group by` clause and the join `on` clause. The DataFrame type, the pipe-verb surface (`group()`/`agg()`/`join()`), and window functions (`over(...)`) remain in the data-processing proposal — they need more consideration and are **not** part of this doc's scope. Successor to `Lambda_Expr_For_Clauses.md`, which shipped `where`/`order by`/`limit`/`offset` (implemented and tested, `test/lambda/for_clauses_test.ls`) and put `group by ... as` into the grammar.

---

## 1. Current state (audited 2026-07-10)

| Piece | Grammar | AST | Transpile/Eval | Tests |
|---|---|---|---|---|
| `where` / `order by` / `limit` / `offset` | ✅ `grammar.js` `for_*_clause` | ✅ | ✅ | ✅ `for_clauses_test.ls` |
| `group by K [, K] as g` | ✅ `for_group_clause` (grammar.js:799) | ✅ `build_group_clause` (build_ast.cpp:7055) — registers `g` as `TYPE_MAP` | ❌ `transpile.cpp:2425` errors "GROUP BY not yet implemented"; nothing in transpile-mir | ❌ none |
| join `on` / optional side `?` | ❌ | ❌ | ❌ | ❌ |

So `group by` is a **completion task** (semantics + transpiler + tests over shipped grammar), and join `on` is a **small grammar addition** plus a hash-join engine both features share.

---

## 2. `group by` clause

### 2.1 Syntax — as already shipped in the grammar

```
for ( BINDINGS [, let N = E ...]
      [where COND]
      [group by KEY_EXPR [, KEY_EXPR ...] as NAME]
      [order by ...] [limit ...] [offset ...] )
    BODY
```

```lambda
// single key
for (x in sales group by x.region as g)
    { region: g.key, total: sum(g.items | ~.amount) }

// multiple keys — g.key is a list [k1, k2]
for (o in orders group by o.year, o.month as g)
    { year: g.key[0], month: g.key[1], n: len(g.items) }

// full clause pipeline: filter rows → group → order/limit groups
for (w in doc.words
     where len(w) > 3
     group by w as g
     order by len(g.items) desc
     limit 10)
    { word: g.key, freq: len(g.items) }
```

The same clauses apply to the for-**statement** form (`for_stam` shares `for_clauses` in the grammar).

**FC1 — binding form is `as g` with `g.key` / `g.items` (settles the surface).** The data-processing doc §4.1 sketched LINQ's `group by K into g` with `g` itself iterable; the grammar that actually shipped (from `Lambda_Expr_For_Clauses.md`) uses `as g` binding a plain map `{ key: ..., items: [...] }`. This proposal adopts the shipped form:

- zero grammar work — `for_group_clause` and the AST registration of `g: TYPE_MAP` already exist;
- `g.items` is an ordinary list — every existing collection function (`sum`, `avg`, `len`, `min`, `max`, pipes, nested `for`) works on it with no new "group value" type;
- explicit beats magic: no XQuery-style variable regrouping, and no dual-natured value that is "iterable but also has `.key`".

`Lambda_Design_Data_Processing.md` §4.1 is superseded on this point.

### 2.2 Semantics

- **FC2 — key equality is C8 value equality with C2 numeric-tower coherence.** `1` and `1.0` land in the same group. Multi-key: `g.key` is a list of the key values, compared element-wise by value equality. **Null keys are allowed and form one group** (R/SQL-`GROUP BY` style; note the deliberate asymmetry with joins, where null keys never match — FC6).
- **FC3 — scoping after `group by`:** the loop variables and per-tuple `let` bindings go **out of scope**; only the group name (plus enclosing scope) is visible in `order by`/`limit`/`offset` and the body. This is LINQ's `into` behavior — the tuple stream after grouping *is* groups, and pretending the row variable still means something is exactly the XQuery magic Lambda rejects. An inner filter over members is a nested expression on `g.items`.
- **FC4 — deterministic group order:** groups are emitted in **first-appearance order of their key** in the (post-`where`) input. `order by` then reorders groups explicitly. No hash-order leakage.
- **Clause order** (already the grammar's documented fixed order): `where` filters *rows* before grouping; `order by`/`limit`/`offset` after `group by` apply to *groups*.
- **Works over any sequence** — arrays of maps, ranges, element children, RDB rows: `for (p in doc? [para] group by p.style as g) ...`. No frame required.

### 2.3 What v1 does **not** include (deferred, stays in the data-processing doc)

- A `having`-style post-group filter clause (workaround: pipe the result — `(for (...) ...) that (...)` — or a nested `if`-shaped body is not possible, so filtering groups pre-projection needs the pipe).
- Post-group `let` clauses (grammar currently only allows `let` before the clause block).
- The aggregate vocabulary additions (`count()`, `n_distinct`, `first`/`last`, `collect`) — existing `len`/`sum`/`avg`/`min`/`max`/`median`/... over `g.items` cover v1.

---

## 3. Join `on` clause

### 3.1 Syntax — new grammar (small)

An optional `on COND` attached to a comma-joined source, plus an optional `?` marker on the binding name:

```lambda
// equi-join — hash join under the hood
for (o in orders, c in customers on o.cust_id == c.id)
    { id: o.id, name: c.name, total: o.total }

// left join: unmatched o's appear once with c = null
for (o in orders, c? in customers on o.cust_id == c.id)
    { id: o.id, name: if (c != null) c.name else "unknown" }

// multi-key equi-join: conjunction of equalities
for (a in xs, b in ys on a.k1 == b.k1 and a.k2 == b.k2)
    { ... }

// chained joins — each `on` joins its source to the tuple stream so far
for (o in orders, c in customers on o.cust_id == c.id,
     r in regions on c.region_id == r.id)
    { ... }

// without `on`, comma sources stay what they are today: cross product (unchanged)
for (x in xs, y in ys) { x: x, y: y }
```

Grammar sketch (`grammar.js`):

```js
// loop_expr gains: optional '?' after the binding name, optional on-condition
loop_expr: seq(
    field('name', $.identifier), optional(field('optional', '?')),
    'in', field('as', $._expr),
    optional(seq('on', field('on', $._expr)))
)
```

`on` is a contextual keyword inside the for-binding list (it already exists as the `event_handler` keyword at statement level; no conflict — different context).

### 3.2 Semantics

- **FC5 — `on` is restricted to a conjunction of equality tests** between the new source and the prior tuple stream (each `==` must reference the new source on exactly one side). That is what a hash join can execute. Non-equi conditions belong in a following `where`; the compiler **rejects** non-equi `on` with a clear error rather than silently going O(n·m).
- **FC6 — the `?` marker is the null-padded (optional) side.** `c? in customers on ...` reads "maybe a c": prior tuples with no match appear once with `c = null` (left join). Marking the *prior* side's binding (`o? in orders, c in customers on ...` is not expressible — instead mark both current-source styles):
  - `c? in customers on ...` → **left** join (keep all prior tuples);
  - `c in customers on ...` with `?` on the *first* source — not a thing; instead:
  - **right** join = swap the sources; **full outer** = `c? in customers` **plus** trailing unmatched-`c` emission, spelled `c?* in customers` — **deferred**; v1 ships inner + left only. Full/right outer wait for the verb surface (`how:`), where they are natural options rather than syntax.
  - **Null join keys never match** (SQL semantics) — the deliberate asymmetry with FC2's null grouping, documented in both places.
- **FC7 — deterministic output order:** the joined tuple stream preserves the **prior (probe-side) order**, stable; multiple matches from the new source emit in that source's order.
- Key equality/hash = C8 value equality with C2 numeric coherence, same as grouping (one canonical-hash implementation serves both).

---

## 4. Engine — one hash core for both features

- **Canonical value hash** consistent with C8 equality (and C2: `1` and `1.0` hash identically), over `lib/hashmap.h`. This is the shared prerequisite (**FC8**, = P0 of the data-processing proposal). Scalar keys, plus lists of scalars for multi-key. (The `ArrayNum ==` representation-sensitivity task matters for the *frame* engines later; for-clause keys are scalar/list Items, so it does not gate this doc — but the canonical hash must be built to the same spec.)
- **group by:** single pass over the post-`where` tuple stream, bucketing tuples per key (insertion-ordered buckets → FC4 for free); then evaluate `order by`/`limit`/`offset`/body per group.
- **join `on`:** classic hash join — build a hash table on the **new source** keyed by its side of the equalities, then iterate the prior tuple stream in order and probe (FC7 for free). Left join emits the null-padded tuple on probe miss.
- Both paths are row-engine only (tuples of Items) — no columnar/SIMD work in this doc; that arrives with the DataFrame track.

Implementation lives in the transpiler paths (`transpile.cpp` + `transpile-mir.cpp`) with runtime helpers in `lambda-eval.cpp` (e.g. `fn_group_tuples`, `fn_hash_join_build/probe`), mirroring how `order by` is implemented today.

---

## 5. Implementation plan

| Step | Contents | Gate |
|---|---|---|
| **S0** | Canonical value hash (C8/C2-consistent) as a runtime helper | hash/equality property tests |
| **S1** | `group by ... as g`: transpile.cpp + transpile-mir.cpp codegen over the existing AST; FC3 scope enforcement in build_ast (loop vars invalid post-group) | `test/lambda/for_group_test.ls` + expected `.txt`; baseline green |
| **S2** | Grammar: `?` marker + `on` condition on `loop_expr`; `make generate-grammar`; AST (`AstJoinOn` on the loop node, FC5 equi-conjunction validation with clear error) | parse + rejection tests |
| **S3** | Hash-join codegen (inner + left), both transpilers | `test/lambda/for_join_test.ls` + expected `.txt`; baseline green |
| **S4** | Docs: `doc/Lambda_Expr_Stam.md` query-expression section + cheatsheet | — |

Per repo rules: every new `.ls` test gets its expected-result `.txt`; `make test-lambda-baseline` must stay 100%.

---

## 6. Settled decisions (FC ledger)

- **FC1 ✓** — group binding is `as g`, `g = { key, items }` (adopts shipped grammar; supersedes the data-processing doc's `into g` sketch). §2.1
- **FC2 ✓** — key equality = C8 value equality + C2 numeric coherence; multi-key → `g.key` is a list; null keys form one group. §2.2
- **FC3 ✓** — after `group by`, loop variables and per-tuple lets are out of scope; only the group binding remains. §2.2
- **FC4 ✓** — groups emit in first-appearance key order (deterministic). §2.2
- **FC5 ✓** — `on` = conjunction of equalities only; non-equi `on` is a compile error pointing at `where`. §3.2
- **FC6 ✓** — `c? in src on ...` = left (null-padded) join; null join keys never match; v1 = inner + left only, full/right outer deferred to the verb surface. §3.2
- **FC7 ✓** — join output preserves prior-stream order, stable. §3.2
- **FC8 ✓** — canonical value hash is the shared prerequisite step (S0). §4

**Deferred (remain open in `Lambda_Design_Data_Processing.md`):** DataFrame type, pipe-verb surface (`group()`/`agg()`/`join()` and `~.field` verb scoping), window functions (`over(...)`, D14/D16), aggregate vocabulary, `having`-style group filter, post-group `let`, full/right/semi/anti joins, as-of join, pivot/melt, lazy streams (P8).

---

## Appendix — relationship to other docs

| Doc | Relationship |
|---|---|
| `Lambda_Expr_For_Clauses.md` | predecessor: designed `where`/`order by`/`limit`/`offset` (shipped) and the `group by ... as` surface (grammar shipped, semantics completed here) |
| `Lambda_Design_Data_Processing.md` | source of the extracted designs (§4.1 group by, §5.1 join `on`); its §4.1 `into g` form is superseded by FC1; everything columnar/verb/window stays there |
| `doc/Lambda_Formal_Semantics.md` | C8 value equality and C2 numeric-tower rules are the normative authority for key equality/hash |
