# Lambda For-Expression Clauses

**Status:** shipped. The row clauses `where`, `let`, `order by`, `limit` and `offset` are implemented on both execution tiers (`test/lambda/for_clauses_test.ls`). `group by … into` and join `on` (FC1–FC11) are ratified as S14.1.1v2, S14.1.2v2 and S14.1.3 and implemented on both tiers. The 2026-10-20 revision (FC1-B) made a group's keys bindings and its `into` name an array of rows, and fixed the conformance gaps listed in [Appendix B](#appendix-b--conformance-check-2026-10-20). Open: zero-copy groups (FC11, step S5), what `limit` counts, and the generator-after-`let` question ([i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>)).

**Dates:** first proposal 2026-02-11 (`where`/`order by`/`limit`/`offset`/`let`, and an early `group by … as g`). Group/join design 2026-07-10, extracted from `Lambda_Design_Data_Processing.md` §4.1/§5.1, revised 2026-07-11 (FC1-F, FC3-F, FC9) and 2026-07-12 (S3b). Group content normalization 2026-10-02. Consolidated 2026-10-20 from `Lambda_Expr_For_Clauses.md` and `Lambda_Expr_For_Clauses2.md` (deleted; its FC ledger lives here). Revised 2026-10-20 (FC1-B, FC3-B, FC11 v2, FC10 retired; S14.1.1v2, S14.1.2v2).

**Scope:** the header clauses of `for` — bindings, `let`, `where`, `group by`, join `on`, `order by`, `limit`, `offset` — over the row engine (tuples of Items). The DataFrame type, the pipe-verb surface (`group()`/`agg()`/`join()`), window functions (`over(...)`) and lazy streams stay in `Lambda_Design_Data_Processing.md` (S14.2*, S14.3).

**User reference:** `doc/Lambda_Expr_Stam.md` § Extended For-Expression Clauses; `doc/Lambda_Cheatsheet.md`; tutorial chapter 3.

**Spec linkage:**

| Topic (this doc) | Ruling |
|---|---|
| A for-expression is a list whatever its clauses (§3) | S2.5.2v2 |
| Binding axes `in`/`at`/`of` and arity (§3.2) | S8.1.1v2, S8.1.3v2 |
| Iterating a `var` walks the entry-time value (§3.2) | S9.2.3* |
| Clause words stay legal binding names; the clause wins (§3.4) | S16.10.1v3* |
| `where` is a header word only; the pipe filter is `\|:` (§4.2, §8) | S10.3.1v3, S10.1.6 |
| Clause placement around `group by`; each clause once (§4.4) | S14.1.2v2 |
| `order by` uses the total order; stable; `desc` is full reversal (§5.1) | S6.2.1, S6.2.2v3, S6.2.3 |
| `limit last N` (§5.2) | S7.2.2, S7.2.3 |
| Keys bound by name, `into` binds the array of rows (FC1-B; §6.1) | S14.1.1v2 |
| Key equality, null group, group order, scoping, key names (FC2–FC4, FC3-B, FC9; §6.2–§6.3) | S14.1.2v2; S5.2.1v2, S5.6.1v2*, S5.6.2 |
| Join `on`: equality conjunctions, `?` side, null keys, order (FC5–FC7; §7) | S14.1.3 |
| Join tuples share runtime shapes (Appendix A) | D3.4.3v5 |
| Virtual array carrier for zero-copy groups (FC11) | D7.4.5v2* |
| Laziness belongs to the data, not to `for` (§5.3) | S14.3.1v3* |
| Generators after a `let` (§4.1) | **no ruling yet** — [i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>) |

---

## Contents

1. [Motivation](#1-motivation)
2. [Design inspiration](#2-design-inspiration)
3. [The `for` header](#3-the-for-header)
4. [Row clauses: `let` and `where`](#4-row-clauses-let-and-where)
5. [Ordering and windowing: `order by`, `limit`, `offset`](#5-ordering-and-windowing-order-by-limit-offset)
6. [`group by`](#6-group-by)
7. [Join `on`](#7-join-on)
8. [Relationship to the pipe](#8-relationship-to-the-pipe)
9. [Worked examples](#9-worked-examples)
10. [FC ledger — settled decisions](#10-fc-ledger--settled-decisions)
11. [Deferred and open](#11-deferred-and-open)
- [Appendix A — Implementation](#appendix-a--implementation)
- [Appendix B — Conformance check 2026-10-20](#appendix-b--conformance-check-2026-10-20)
- [Appendix S — Superseded designs](#appendix-s--superseded-designs)
- [Appendix R — References](#appendix-r--references)

---

## 1. Motivation

A bare `for` iterates: `for (x in data) x * 2`. Real data processing also filters, sorts, groups, joins, pages and names intermediate values. Without header clauses each of those becomes a nested call or a body-level `if` that leaves nulls behind:

```lambda
// without clauses: the reader has to unwind it inside-out
sort(for (u in users) if (u.age >= 18) {name: u.name, age: u.age} else null, ...) ...
```

With clauses the query reads in the order it runs:

```lambda
for (u in users
     where u.age >= 18
     order by u.age
     limit 10)
  {name: u.name, age: u.age}
```

The design goals: **declarative** (say what, not how), **one pipeline** (each clause has one well-defined input), **familiar** (SQL/XQuery/LINQ vocabulary), **no new value kinds** (a group is a set of bindings over an array, a join row is an element), and **deterministic** (no hash-order leakage anywhere).

---

## 2. Design inspiration

| Source | What it shows | What Lambda took | What Lambda left |
|---|---|---|---|
| XQuery FLWOR (`for $b in //book let $p := … where … order by … return …`) | a clause pipeline over a tuple stream; the closest model, and Lambda shares its markup heritage | the clause pipeline; `where`/`order by`; grouping keys bound as variables | the grouping-variable magic, where non-key variables silently become sequences after `group by` (FC3-B) |
| SQL `SELECT … FROM … WHERE … GROUP BY … HAVING … ORDER BY … LIMIT … OFFSET` | `WHERE` before grouping, sort and page after; `JOIN … ON` | the phase order; `limit`/`offset`; `ON` restricted to equalities (FC5); null join keys never match (FC6) | `HAVING` (deferred, §11); SQL's unnamed derived columns (FC9) |
| C# LINQ (`from u in users let … where … orderby … group … by … into g select …`) | a query syntax embedded in an expression language | `let`; the `into` continuation keyword; projection-initializer name inference (FC9) | `IGrouping`'s `Key` property — Lambda binds each key by its own name (FC1-B) |
| Scala `for { u <- users; if …; n = … } yield …` | guards and bindings inline with generators | comma-separated generators are a cross product | `yield`; the body is the projection |
| Haskell `[ e \| x <- xs, guard, let y = … ]` | comprehensions with guards and lets | generators then lets | interleaving generators after lets (open, [i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>)) |

| Feature | Lambda | XQuery | SQL | LINQ | Scala |
|---|---|---|---|---|---|
| Iterate | `for (x in xs)` | `for $x in …` | `FROM` | `from x in` | `x <- xs` |
| Bind | `, let y = …` | `let $y := …` | — | `let y = …` | `y = …` |
| Filter | `where …` | `where …` | `WHERE` | `where …` | `if …` |
| Group | `group by k [as a] into g` | `group by $k` | `GROUP BY` | `group … by … into g` | `.groupBy(…)` |
| Join | `c in cs on o.k == c.k` | nested `for` + `where` | `JOIN … ON` | `join … on … equals …` | nested generators + guard |
| Left join | `c? in cs on …` | `allowing empty` | `LEFT JOIN` | `join … into` + `DefaultIfEmpty` | — |
| Sort | `order by e [asc\|desc]` | `order by …` | `ORDER BY` | `orderby …` | `.sortBy(…)` |
| Page | `limit n`, `limit last n`, `offset n` | positional predicate | `LIMIT`/`OFFSET` | `.Take`/`.Skip` | `.take`/`.drop` |
| Output | body expression | `return …` | `SELECT` | `select …` | `yield …` |

---

## 3. The `for` header

### 3.1 Surface

One header serves the expression form `for (head) expr` and the braced form `for head { … }` (parentheses optional before a braced body). A for-expression is a **list whatever its clauses** (S2.5.2v2), so its items spread where it lands: `[for (i in 1 to 2) (i, i)]` has four items.

```
head      = binding {',' binding} {',' 'let' NAME '=' expr} {clause}
binding   = [INDEX [':' type] ','] NAME ['?'] ('in' | 'at' | 'of') expr ['on' expr]
clause    = 'where' expr
          | 'group' 'by' key_spec {',' key_spec} 'into' NAME
          | 'order' 'by' expr ['asc' | 'desc'] {',' expr ['asc' | 'desc']}
          | 'limit' ['last'] expr
          | 'offset' expr
key_spec  = postfix_expr ['as' NAME]
```

A group key is a postfix-level expression (member access, call, subscript); an infix key needs parentheses — `group by (x % 2) as parity into g`. The grammar accepts clauses in any order; the resolver enforces their placement (§4.4).

### 3.2 Bindings

- **Axes** (S8.1.1v2): `in` walks member values, `at` walks name keys, `of` walks integer positions. The one-name form binds what the axis is named after; the two-name form `i, x in c` adds the key (S8.1.3v2). An index annotation names a key class: `for (i: int, x in c)` is `for (i, x of c)`.
- **Several comma sources** are stages, left to right. A source without `on` is a **cross product** with the rows so far (nested-loop order: prior order, then the new source's order). A source with `on` is a **hash join** against the rows so far (§7). The two interleave freely.
- A loop over a `var` container walks its entry-time value; a body write detaches (S9.2.3*).

### 3.3 Clauses at a glance

| Clause | Purpose | Example |
|---|---|---|
| `, let n = e` | per-row binding | `, let total = o.qty * o.price` |
| `where c` | keep rows whose `c` is truthy | `where u.active and u.age >= 18` |
| `group by k [as a], … into g` | run the rest once per group | `group by s.region into g` |
| `x? in src on a == b` | equi-join (left join with `?`) | `c? in customers on o.cust == c.id` |
| `order by e [asc\|desc], …` | stable sort | `order by p.dept, p.salary desc` |
| `limit n` / `limit last n` | first / last n | `limit 10` |
| `offset n` | skip n | `offset 20` |

### 3.4 Keywords

`where order by group into limit offset asc desc as on` are contextual: they open a clause only inside a `for` header. They can never begin a construct, so they stay legal binding names, and where both readings fit the clause wins (S16.10.1v3*). Only `asc` and `desc` name directions.

---

## 4. Row clauses: `let` and `where`

### 4.1 `let`

`, let name = expr` follows the bindings and computes a value once per row. Each `let` sees every binding and every earlier `let`; the name is visible in `where`, `order by` and the body (in a grouped `for`, only up to `group by` — §6.3).

```lambda
// compute once, use in where and body
for (x in [1, 2, 3, 4, 5], let doubled = x * 2 where doubled > 4) doubled
// (6, 8, 10)

// lets chain
for (o in orders,
     let subtotal = sum(o.items |> ~.price * ~.qty),
     let tax = subtotal * 0.08,
     let total = subtotal + tax
     where total > 100)
  {id: o.id, total: total}
```

All generators come before all `let`s; a generator after a `let` (`for (v in vs, let k = f(v), e in g(v, k))`) is a parse error today. Whether to relax that is open — [i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>), Options A and B. Both options keep the `where`/`group by`/`order by`/`limit` tail phase-ordered, which is what §4.4 enforces.

### 4.2 `where`

`where cond` keeps a row only when `cond` is truthy. It runs after every binding and `let` of the row exists, and before grouping. Combine conditions with `and`/`or` in one `where`.

```lambda
for (x in 1 to 10 where x % 2 == 0) x
// (2, 4, 6, 8, 10)

for (k, v at {a: 1, b: 5, c: 2} where v > 2) k
// ('b')
```

`where` drops the row; an `if` in the body keeps it and yields `null`, which the list keeps (S2.5.1v2):

```lambda
for (x in [-1, 1, -2, 2] where x > 0) x * 2   // (2, 4)
for (x in [-1, 1, -2, 2]) if (x > 0) x * 2     // (null, 2, null, 4)
```

`where` is **only** a header word. The binary `where` filter was retired — first for `that`, now for `|:` — and an infix `where` is a compile error naming `|:` (S10.3.1v3, S10.1.6).

### 4.3 Scoping

| Clause | Sees | Introduces |
|---|---|---|
| bindings | enclosing scope, earlier bindings | value names (and index/key names) |
| `let` | bindings, earlier lets | one name |
| `where` | bindings + lets | — |
| `group by` keys | bindings + lets | the key names and the `into` name; **closes the row scope** |
| `order by`, body — ungrouped | bindings + lets | — |
| `order by`, body — grouped | key names, the `into` name, enclosing scope | — |
| `limit`, `offset` | enclosing scope (evaluated once) | — |

### 4.4 The clause pipeline

Clauses execute in one fixed logical order:

```
1. bindings        — generate rows: cross-product and hash-join stages, left to right
2. let             — per-row bindings, in order
3. where           — keep truthy rows
4. group by        — partition the kept rows; the row scope closes
5. order by        — stable sort of rows (or groups)
6. offset, limit   — window: offset first, then limit, whatever their textual order
7. body            — the projection
```

The reasoning for a fixed pipeline (the ledger's Option A): each stage has one well-defined input — `where` filters after every binding exists, `order by` sorts a settled row set, `limit` counts settled rows — so the clauses compose predictably and the order is statable in one line.

**Placement is enforced at the group boundary** (S14.1.2v2, 2026-10-20). Row clauses (`let`, `where`) come before `group by`, group clauses (`order by`, `limit`, `offset`) after it, and each clause appears at most once; anything else is E239. The grammar accepts clauses in any order and evaluation is phase-fixed, so before this rule a `where` after `group by` filtered rows with the group names unbound (every row dropped), a post-group `let` ran per row, an `order by` written before `group by` sorted groups anyway, and a second `where`, `limit` or `offset` silently replaced the first. The second-`where` case was live in three chart-package headers (`where A, let … where B`), whose `A` had never run. `let` and `where` may still appear in either textual order before `group by`; all lets bind before the `where` runs.

---

## 5. Ordering and windowing: `order by`, `limit`, `offset`

### 5.1 `order by`

`order by e [asc | desc] {, e [asc | desc]}` sorts the rows (or, after `group by`, the groups). Ascending is the default; each later key breaks the ties of the earlier ones and carries its own direction.

- The order is the **Lambda total order**, not `<` (S6.2.1): `null < false < true < number < datetime < symbol < string < binary < sequence < map < object < element < type < function < nan < error`. Mixed-kind keys therefore always sort, and **null sorts first** ascending.
- The sort is **stable**; equal keys keep input order (S6.2.2v3). `desc` is **full reversal** of the one order — no pinning exceptions, so null sorts last descending (S6.2.3).
- Strings sort bytewise by UTF-8, with no locale collation (S6.2.2v3).
- A row moves as a unit: a body that yields several items keeps them together, in their order.

```lambda
for (x in [3, null, 1, 2] order by x) x        // (null, 1, 2, 3)
for (x in [3, null, 1, 2] order by x desc) x   // (3, 2, 1, null)
for (s in ["banana", "fig", "apple"] order by len(s)) s   // ("fig", "apple", "banana")
for (e in employees order by e.dept asc, e.salary desc) e.name
for (x in [2, 1, 3] order by x) (x, x * 10)    // (1, 10, 2, 20, 3, 30)
```

### 5.2 `limit` and `offset`

`offset m` skips the first m results; `limit n` keeps at most n. Both apply after `where`, `group by` and `order by`, and compose as offset-then-limit whatever their textual order. `limit last n` keeps the last n, in original order — the second of `last`'s two homes (S7.2.3). An offset past the end yields an empty list; a limit beyond the length keeps everything.

```lambda
for (x in 1 to 100 offset 5 limit 10) x   // (6, 7, …, 15)
for (x in 1 to 10 limit last 3) x         // (8, 9, 10)
for (it in items order by it.id limit 20 offset 40) it   // page 3 of 20
```

The window counts the items of the result stream, so a body yielding several items per row is limited by items, not rows (open, §11).

### 5.3 Evaluation

The header is eager over its input: `order by` and `group by` need every row, and the row engine collects all admitted rows before windowing (Appendix A). Laziness is a property of the data, not of `for`: a stream source records the `for` stage onto a plan, and terminals force it (S14.3.1v3*).

---

## 6. `group by`

### 6.1 Keys are bindings, the group is an array (FC1-B)

```
group by KEY_EXPR [as NAME] {, KEY_EXPR [as NAME]} into NAME
```

After `group by`, the rest of the expression — `order by`, `limit`, `offset`, the body — runs **once per group**, with:

- **each key bound under its name** — the `as` name, or the name FC9 infers (§6.2);
- **the `into` name bound to the group's rows**: an **array**, in input order, holding each row verbatim — nulls and strings included, nothing merged (S14.1.1v2).

```lambda
// single key — `region` is the key, `g` the rows
for (x in sales group by x.region into g)
  {region: region, total: sum(g |> ~.amount), orders: len(g)}

// several keys — each bound by its own name
for (o in orders group by o.year, o.month into g)
  {year: year, month: month, n: len(g)}

// a computed key takes a name; an infix key takes parentheses
for (p in people group by (p.age div 10) as decade into g)
  {decade: decade, n: len(g)}

// grouping strings by themselves counts every occurrence
for (w in words group by w as word into g order by len(g) desc)
  {word: word, freq: len(g)}

// emitting g itself gives one array per group
for (x in [3, 1, 3] group by x as v into g) g     // ([3, 3], [1])
```

`g` is an ordinary array, so `len(g)` counts the rows, `g[0]` is the first, `g |> ~.amount` projects a field and `g |: …` filters.

**Why this form** (FC1-B, user-decided 2026-10-20, replacing FC1-F):

- **A group is rows, not content.** FC1-F made a group an element, keys as attributes and members as children. Once element content was normalized (S2.6.2–S2.6.4, 2026-10-02), that representation lost rows: adjacent string members merged into one child and null members disappeared, so the most natural query — counting words — reported 1 for every word. The fault is in the representation, not the normalization: any element, concrete or virtual, inherits content semantics, so the zero-copy `velmt` view FC11 planned would have had to choose between hiding rows and breaking S14.1.1. An array has sequence semantics (S2.5.1v2): it keeps exactly what was put in.
- **Keys are named values, not a structure.** Binding each key by name keeps FC1-F's best property — `year` and `month`, never a positional `key[1]` — without a carrier type. Key values need no key domain: `2.5`, datetimes, booleans, `null`, tuples all bind as they are.
- **`len(g)` means the row count.** Under FC1-F it counted the key attributes too (S8.3.1v3), and counting needed `len(content(g))` (the [LR09-9](<Lambda_Issue_Ledger (fixed).md#lr09-9>) wart).
- **No reserved name.** Nothing in the group can collide with a member field or a user key name except the user's own names, which FC9 checks.
- **Zero-copy is direct.** A group is a slice of the hash bucket; FC11's Phase 2 is a `varray` over the bucket, with no content normalization to reconcile.

**Alternatives considered** (2026-10-20):

| Option | Shape | Why not |
|---|---|---|
| (A) keys as bindings, `g` an array | `{region: region, n: len(g)}` | **chosen** |
| (B) `g` a map with a members field | `g.region`, `g.members` | needs a reserved field name that can collide with a key alias; a map key must be a name, so non-name key values need a second rule |
| (C) the comprehension yields one `vmap` from key to rows | `{west: [...], east: [...]}` | a `vmap` admits only `NameKey`/`IntKey` keys (S8.2.1v4), so `2.5`, datetimes, booleans, `null` and multi-key tuples have no key without stringifying, which breaks FC2 equality; and one map value replaces the per-group body, losing named multi-key access. It is the right shape for a future `group()` verb, and it is what the runtime builds internally |
| (D) keep the element, stop normalizing group content | `g.region`, `content(g)` | an element whose content is not normalized is a second kind of element; S2.6 would need an exception |

What FC1-B gives up: "a group is a document node" — `for (…) g` no longer prints `<group …>` markup. A query that wants markup builds it: `<group region: region, *g>`.

### 6.2 Key naming — the FC9 rule

When `as` is omitted, a key's name is inferred **only** from a trailing field access (S14.1.2v2):

1. **Trailing field access** → that field's name: `group by o.year` → `year`; `group by o.date.year` → `year`.
2. **Anything else** — a bare loop variable (`group by w`), a call, arithmetic, a comparison — is a **compile error demanding `as`** (E239). No generated `key1`/`expr1` names. A for-expression key always mentions the loop variable, so unlike SQL there is no bare-column case; the bare loop-variable key takes a name (`group by w as word`), which also avoids `w`-the-key shadowing `w`-the-row.
3. **Collisions** — two keys with one name (`a.region, b.region`, or an alias equal to an inferred name), or a key named like the `into` binding — are a compile error demanding `as` (E209).

Key names are bindings, so they follow binding rules: a reserved word cannot be one (`group by x.type` needs `as kind`, S16.10.1v3*), and a name equal to a system function shadows it in the group body, with the usual warning (S12.3.7).

Why not generate names: SQL leaves unnamed derived columns implementation-dependent, and practice shows it. PostgreSQL falls back to `?column?`; MySQL, SQLite and DuckDB use the raw expression text, which is not an identifier; SQL Server yields an unreferenceable `(No column name)`; BigQuery's positional `f0_, f1_` renumber silently when keys are added or reordered. The workable precedents are LINQ's projection-initializer inference (member access gives the member name, anything else needs an explicit name) and Polars' leftmost-column-name rule, which are exactly rules 1–3.

### 6.3 Semantics

- **FC2 — key equality is `==`** (S14.1.2v2, S5.6.1v2*): numeric-tower coherent, so `1` and `1.0` land in one group (S5.2.1v2); several keys compare the key tuple element-wise; **null keys form one group**, and each `nan` key stands alone. Hashing is consistent with `==` (S5.6.2). A key binds the value of its group's first row, so `1` and `1.0` group under `1` when `1` comes first. Null grouping is deliberately asymmetric with joins, where null keys never match (FC6).
- **FC3-B — the row scope closes at `group by`.** Loop variables and per-row `let`s go out of scope; the key names and the `into` binding (plus the enclosing scope) are visible to `order by` and the body. The XQuery alternative — rebinding the loop variable to the member list — stays withdrawn: a variable that silently changes type from item to list across a clause boundary is a type instability Lambda should not carry. FC1-B introduces only fresh names.
- **FC4 — deterministic group order.** Groups emit in **first-appearance order of their key** in the post-`where` rows; `order by` then reorders them explicitly. No hash-order leakage.
- **Phases.** `where` filters **rows** before grouping; `order by`, `offset` and `limit` after `group by` apply to **groups**.
- **Any row source.** Arrays of maps, ranges, element children, RDB rows, joined tuples — no frame required. After a join, the rows are the join tuples (§7.3).

---

## 7. Join `on`

### 7.1 Surface

An optional `on COND` on a comma source, plus an optional `?` after the binding name:

```lambda
// inner equi-join — a hash join
for (o in orders, c in customers on o.cust_id == c.id)
  {id: o.id, name: c.name, total: o.total}

// left join: unmatched o's appear once with c = null
for (o in orders, c? in customers on o.cust_id == c.id)
  {id: o.id, name: if (c != null) c.name else "unknown"}

// several keys: a conjunction of equalities
for (a in xs, b in ys on a.k1 == b.k1 and a.k2 == b.k2) {a: a, b: b}

// chained: each `on` joins its source to the rows so far
for (o in orders, c in customers on o.cust_id == c.id,
     r in regions on c.region_id == r.id)
  {order: o.id, region: r.name}

// index/key bindings survive the join
for (i, o in orders, c in customers on o.cust_id == c.id) {pos: i, name: c.name}

// join and cross-product stages interleave
for (o in orders, c in customers on o.cust_id == c.id, tag in tags)
  {order: o.id, cust: c.name, tag: tag}
```

`on` is contextual inside the binding list; the statement-level `on` of event handlers is a different context.

### 7.2 Semantics

- **FC5 — `on` is a conjunction of equality tests** between the new source and the rows so far, each `==` mentioning the new source on exactly one side (S14.1.3). That is what a hash join executes. A non-equi `on` is a **compile error pointing at `where`** (E239), never a silent O(n·m) nested loop or an empty result.
- **FC6 — `?` marks the null-padded side.** `c? in customers on …` reads "maybe a c": every prior row is kept, padded with `c = null` (and a null index binding) when nothing matches — a left join. Right join is spelled by swapping the sources. Full outer (a spelling such as `c?*` that also emits unmatched `c`s) is **deferred**: v1 ships inner and left; full and right outer wait for the verb surface (`how:`), where they are options rather than syntax. **Null join keys never match** (SQL semantics), the deliberate asymmetry with FC2.
- **FC7 — deterministic order.** The output preserves the prior (probe-side) row order, stable; several matches from the new source emit in that source's order.
- Key equality and hashing are the same `==`-consistent canonical hash as grouping (FC8).

### 7.3 Rows as tuples

Once a `for` has more than one source, a row is a **tuple element**, tag `tuple`, whose attributes are the bindings by name (`o`, `c`, and index/key names). The body, `let`, `where` and `order by` read the names directly; a later `on` can reference any earlier binding because the tuple carries it. A key-only `at` source binds the key as its value. Grouping a join collects the tuples into the group's array:

```lambda
for (o in orders, c in customers on o.cust_id == c.id group by c.name into g)
  {customer: name, orders: len(g), total: sum(g |> ~.o.total)}
```

---

## 8. Relationship to the pipe

The header clauses complement the pipe family; they do not replace it. The pipe works on the current item `~`; the header works on named bindings.

| Need | Prefer | Shape |
|---|---|---|
| One transformation | pipe | `data \|> ~ * 2` |
| One filter | pipe filter | `data \|: ~ > 0` |
| Named rows, several sources, joins | `for` | `for (o in orders, c in customers on …) …` |
| Grouping, sorting, paging together | `for` | `for (x in data group by … order by … limit …) …` |
| A chain of small steps | pipe | `data \|> ~.a \|: ~ > 0 \|> sum(~)` |

They compose in both directions:

```lambda
// for as a pipe source
(for (x in data where x > 0 order by x) x) |> ~ * 2

// equivalent forms
users |: ~.age >= 18 |> upper(~.name)
for (u in users where u.age >= 18) upper(u.name)

// a post-group filter (no `having` yet) — pipe the group results
(for (r in rows group by r.k into g) {k: k, n: len(g)}) |: ~.n > 1
```

---

## 9. Worked examples

```lambda
// active users by name
for (u in users where u.active order by u.last, u.first)
  {name: u.first ++ " " ++ u.last, email: u.email}

// page 3 of search results, 25 per page
for (r in results order by r.relevance desc limit 25 offset 50)
  {title: r.title, url: r.url}

// sales summary by region, biggest first
for (s in sales
     where s.year == 2025
     group by s.region into g
     order by sum(g |> ~.amount) desc)
  {region: region,
   total: sum(g |> ~.amount),
   orders: len(g),
   average: avg(g |> ~.amount)}

// word frequency, top 10
for (w in doc_words
     where len(w) > 3
     group by w as word into g
     order by len(g) desc, word
     limit 10)
  {word: word, freq: len(g)}

// orders per customer name; orders with no customer group under a null name
for (o in orders, c? in customers on o.cust_id == c.id
     group by c.name into g)
  {customer: name, n: len(g)}

// nested sources with a filter and a two-key sort
for (d in departments, e in d.employees
     where e.years >= 5
     order by d.name, e.salary desc)
  {department: d.name, employee: e.name, salary: e.salary}

// computed lets feeding a filter, sort and window
for (x in 1 to 20, let sq = x * x
     where x > 3 and x < 15
     order by sq desc
     limit 3 offset 2)
  sq
// (144, 121, 100)
```

---

## 10. FC ledger — settled decisions

- **FC1-B ✓** — `group by KEY [as NAME], … into g`: after grouping, **each key is bound by name** and **`g` is the array of the group's rows**, kept verbatim. User-decided 2026-10-20; supersedes FC1-F (a group is an element), FC1 v1 (`as g` with `g.key`/`g.items`) and the interim FC1-R (loop-variable regrouping). → S14.1.1v2. §6.1
- **FC2 ✓** — key equality is `==` with numeric-tower coherence; several keys compare the key tuple element-wise; null keys form one group. → S14.1.2v2, S5.6.1v2*. §6.3
- **FC3-B ✓** — after `group by`, loop variables and per-row lets are out of scope; the key names and the `into` binding remain. Revised 2026-10-20 from FC3-F (only the `into` element remained). → S14.1.2v2. §6.3
- **FC4 ✓** — groups emit in first-appearance key order. → S14.1.2v2. §6.3
- **FC5 ✓** — `on` is a conjunction of equalities only; a non-equi `on` is a compile error pointing at `where`. → S14.1.3. §7.2
- **FC6 ✓** — `c? in src on …` is the left (null-padded) join; null join keys never match; v1 is inner and left only, full/right outer deferred to the verb surface. → S14.1.3. §7.2
- **FC7 ✓** — join output preserves prior-row order, stable. → S14.1.3. §7.2
- **FC8 ✓** — one canonical value hash, consistent with `==` (S5.6.2), serves grouping and joining; it was the shared prerequisite step S0. Appendix A
- **FC9 ✓** — an omitted `as` infers a key name from a trailing field access only; anything else, including a bare loop variable, is a compile error demanding `as`; names colliding with each other or with the `into` name are compile errors; no generated names (user-confirmed 2026-07-11; the `into` collision added 2026-10-20 with FC1-B). → S14.1.2v2. §6.2
- **FC10 ✗ retired 2026-10-20** — the group element's tag `group`. FC1-B has no group element. Appendix S
- **FC11 ✓ v2** — two-phase group construction. Phase 1 materializes each group's rows as a plain array (shipped). Phase 2 makes `g` a zero-copy **`varray`** view over the hash bucket (D7.4.5v2*), step S5. Revised 2026-10-20: the v1 plan was a `velmt` view, which would have inherited element content semantics (§6.1). §11

---

## 11. Deferred and open

**Deferred to `Lambda_Design_Data_Processing.md`:** the DataFrame type; the pipe-verb surface (`group()`/`agg()`/`join()`, `~.field` verb scoping — S14.2.2*); window functions (`over(...)`); the aggregate vocabulary (`count()`, `n_distinct`, `first`/`last`, `collect`); full, right, semi, anti and as-of joins; pivot/melt; lazy streams (S14.3). A `group()` verb is the natural home for the key-to-rows map shape (option C, §6.1).

**Deferred within this surface:**

- **A `having`-style group filter.** Workaround: pipe the group results through `|:` (§8). A `where` after `group by` is E239, not a silent row filter.
- **Post-group `let`.** `let` belongs to the row phase; group-level values are computed in the body. A `let` after `group by` is E239.

**Open:**

- **Generators after a `let`** — [i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>): keep the fixed generator/let prefix (Option A) or let generators follow `let`s (Option B). The tail after the lets is phase-ordered under both, and S14.1.2v2 now enforces that part.
- **What `limit` counts.** The window trims the result stream, so a body that yields several items per row is limited by items, not rows: `for (x in [1, 2, 3] limit 2) (x, x)` gives `(1, 1)`. SQL's reading and the pipeline argument in §4.4 count rows; `order by` already moves rows as units (§5.1). Unruled.
- **FC11 Phase 2 (S5).** Zero-copy `g` as a `varray` over the bucket. The `varray` carrier exists (D7.4.5v2*); groups still copy their row handles into a plain array.

---

## Appendix A — Implementation

**Parser.** `parser_parse_for_header` in `lambda/runtime/parser/lambda_parser.c`: bindings, then comma-introduced `let`s, then any sequence of clauses. `lambda/tree-sitter-lambda/grammar.js` (`_loop_head`, `loop_expr`, `for_clauses`, `group_key_spec`) mirrors it for the isolated `lambda-cst` verifier only.

**AST.** `lambda/runtime/build_ast.cpp`:

- `syntax_for_clause` records each clause as a part; `resolve_for_part` attaches the parts in source order, first checking `for_part_misplacement` (E239 for a misplaced or repeated clause, or more than 63 order keys).
- `enter_for_group_scope` closes the row scope; `register_for_group_bindings` registers each key name (`AstGroupKey::entry`, typed `any`) and the `into` name (`AstGroupClause::entry`, typed array), reporting E209 for a duplicate name.
- `infer_group_key_alias` applies FC9 rule 1, and the `LSF_GROUP_KEY` resolve step reports E239 when it finds no name.
- `build_join_key_specs` splits `on` into equi-key pairs and reports E239 for any other shape (FC5).
- `ast_order_desc_mask` (`ast.hpp`) turns the order specs into the key count and a per-key direction mask.

**Runtime helpers**, shared by the T0 interpreter (`interp.cpp`) and MIR Direct (`transpile-mir.cpp`):

- `lambda_item_hash` / `lambda_item_compare` (`lambda-data-runtime.cpp`) — the FC8 canonical hash and equality, also used by `vmap`.
- `fn_group_by_keys(rows, keys, key_count)` (`lambda-data-runtime.cpp`) — one pass bucketing rows by key in insertion order (FC4); returns one record per group, `[members, key_1, …, key_n]`, with `members` a plain array filled by the verbatim append and a multi-key tuple split into its parts.
- `fn_join_seed_tuples`, `fn_hash_join_tuples`, `fn_cross_join_tuples` (`lambda-data-runtime.cpp`) — tuple elements (tag `tuple`, on the runtime shape tree, D3.4.3v5) with parallel index arrays; the hash stage builds on the new source and probes in prior order (FC7), padding with null on a left-join miss; the cross stage expands nested-loop.
- `fn_order_rows(rows, keys, desc_mask, key_count)` (`lambda-vector.cpp`) — stable merge sort of the rows by one key per row (a tuple when there are several specs), compared spec by spec under the total order; the rows hold one body value each, and the result spreads them back into a fresh stream. `for_window` then applies offset, limit and `limit last`.

**Both tiers** share three shapes: one key builder for group, order and join keys (`interp_eval_key_item` / `mir_box_key_item`: the value for one expression, an array tuple for several); one group stage (`interp_for_emit_groups` / `mir_emit_group_stage`) that binds the `into` array and each key name per record and runs the body; and, with `order by`, a verbatim push of each body value so that a multi-item body sorts as one row.

**Evaluation order.** Rows run bindings → lets → `where`; the body is evaluated for every admitted row, and its sort key recorded beside it, **before** the stable sort and the window. The body therefore runs even for rows a `limit` later drops; there is no early termination. A grouped `for` collects the admitted rows and keys, builds the group records, then evaluates `order by` and the body per group. Mid-loop handoff from T0 to compiled code covers only a plain `for` statement — one source, no join, `group`, `order`, `limit` or `offset` (D8.1.1v18*).

**Tests.** `test/lambda/for_clauses_test.ls` (row clauses; tests 46–47 cover multi-key order and multi-item bodies), `for_group_test.ls` (keys as bindings, null rows, string members, word count, group windowing), `for_join_test.ls`, `for_join_s3b_test.ls` (index/key bindings, mixed stages), `for_join_group_test.ls`, `list_kind_for_clauses.ls` (S2.5.2v2 list kind), `io_sqlite_for_clauses.ls` (RDB rows). Negatives in `test/lambda/negative/semantic/`: `for_group_key_name.ls`, `for_group_key_collision.ls`, `for_join_non_equi.ls`, `for_clause_placement.ls` (`test_lambda_errors_gtest`).

**Implementation steps (group/join track):**

| Step | Contents | Status |
|---|---|---|
| S0 | canonical value hash (FC8) | ✅ `lambda_item_hash` / `lambda_item_compare` |
| S1 | `group by … into g`: grammar, FC9 inference, FC3 scoping, group materialization | ✅ 2026-07-11 as group elements (FC1-F); the FC9 errors were only logged and were lost with the tree-sitter AST builder (24e74b45ee, 2026-08-23) |
| S2 | `?` marker and `on` on bindings; equi-key pairs; FC5 validation | ✅ 2026-07-11; FC5 was only logged until S6 |
| S3 | hash join, inner and left | ✅ 2026-07-11, `for_join_test.ls` |
| S3b | index/key bindings in join tuples; join and cross-product stages interleaved | ✅ 2026-07-12, `for_join_s3b_test.ls` |
| S4 | user docs (`doc/Lambda_Expr_Stam.md`, cheatsheet) | ✅ 2026-07-12 |
| S6 | FC1-B: group records, keys as bindings, `into` array; E239/E209 for FC5, FC9 and clause placement; multi-key and multi-item `order by`; three chart-package headers fixed; docs, tutorial, fixtures | ✅ 2026-10-20, both tiers |
| S5 | FC11 Phase 2: `g` as a zero-copy `varray` over the bucket, with parity tests (the same goldens pass under both phases) | open |

S1–S3b originally shipped through both the C2MIR and MIR Direct back ends; C2MIR has since been removed, and the T0 interpreter now carries the same paths.

---

## Appendix B — Conformance check 2026-10-20

Probed with the debug `lambda.exe` on master (81105648b) during the consolidation, under both `LAMBDA_EXEC_BACKEND=interp` and `jit`, and fixed the same day by S6 except where noted.

| # | Case | Was | Now |
|---|---|---|---|
| B1 | `group by len(w) into g` (no `as`, not a field access) | accepted; an attribute with an empty name | E239 (FC9) |
| B2 | `group by r.x, r.x into g` | accepted; one attribute | E209 (FC9) |
| B3 | `on a < b` | `log_error` only; the script ran and yielded `[]` | E239 (FC5) |
| B4 | `order by p.d asc, p.s desc` | only the first key applied — evaluation read only the first `AstOrderSpec` on both tiers | every key applies, each with its direction |
| B5 | `order by x` with a body yielding several items per row | `sort_by_keys: values length 6 != keys length 3`; output left unsorted | rows sort as units |
| B6 | `where a where b` | the later `where` replaced the earlier — live in three chart-package headers | E239; the chart headers apply both conditions |
| B7 | `… group by … into g where …` | ran pre-group with the group names unbound; every row dropped | E239 |
| B8 | `… group by … into g, let n = len(g)` | ran per row with `g` unbound | E239 |
| B9 | a group of string members | members merged into one child, null members dropped (S14.1.1 v1 with S2.6) | FC1-B: `g` keeps every row |
| B10 | `limit` with a multi-item body | counts items | unchanged; open (§11) |

---

## Appendix S — Superseded designs

- ~~FC1-F: `group by KEY [as ALIAS], … into g` binds **a group element**: keys as attributes, members as children; `g.region`, `content(g)`, `len(content(g))`; "a group is a document node"~~ — 2026-07-11, S14.1.1 v1. **Replaced by FC1-B** (2026-10-20): element content normalization merged string members and dropped null rows (§6.1).
- ~~FC10: the group element's tag is the fixed symbol `group`~~ — 2026-07-11. **Retired with FC1-B.**
- ~~FC11 v1 Phase 2: a `velmt` virtual element over the bucket, attribute face for keys and child face for members~~ — 2026-07-11. **Replaced** by a `varray` view (FC11 v2): an element view inherits content semantics.
- ~~FC3-F: after `group by`, only the `into` element remains in scope~~ — 2026-07-11. **Replaced by FC3-B**: the key names remain too.
- ~~Wrap string rows in maps before grouping (`doc_words |> {word: ~}`) so equal words stay separate members~~ — 2026-10-20 consolidation workaround. **Moot under FC1-B.**
- ~~`group by KEY, … as g`, binding a map `{key: …, items: [...]}`; `g.key` is a value or an array of values; `g.items` is the member array~~ — the 2026-02 proposal; grammar shipped but never transpiled. Replaced by FC1-F, then FC1-B.
- ~~FC1-R: rebind the loop variable to the member list after `group by`, XQuery style~~ — interim 2026-07-11. **Withdrawn** for the item→list type instability (FC3-B, §6.3).
- ~~Shorthand `~key` / `~items` for the current group~~ — 2026-02 future idea. **Moot**: keys are bound by name and `g` holds the rows.
- ~~Direction aliases `ascending` / `descending`~~ — 2026-02. **Not adopted**; only `asc` / `desc` (§3.4).
- ~~XQuery-style `let` between clauses without a comma (`for (x in xs let y = … where …)`), and post-group `let`~~ — 2026-02. **Not adopted**: `let` is comma-introduced after the bindings (§4.1); post-group `let` is deferred and now E239 (§11).
- ~~A fixed textual clause order in the grammar (`bindings let? where? group? order? limit? offset?`)~~ — 2026-02 grammar sketch. **Replaced**: the grammar accepts any order, evaluation is phase-fixed, and the resolver enforces placement around `group by` (§4.4).
- ~~Binary `where` as the pipe filter (`data where ~.active`), sharing the word with the header clause~~ — **retired** by S10.3.1v3: the filter is `|:` (S10.1.6) and `that` is the proviso (§8).
- ~~Desugaring to `sort(…)`/`take(…)` over `{__val, __sort}` wrapper maps, and `group_by(data, key_fn)` + nested `for`~~ — 2026-02 strategy. **Replaced** by direct row collection with shared runtime helpers (Appendix A).
- ~~Lazy `where`/`limit` with early termination when no `order by`/`group by` is present~~ — 2026-02. **Not built**; the row engine is eager, and laziness belongs to stream data (S14.3.1v3*, §5.3).

---

## Appendix R — References

| Doc | Relationship |
|---|---|
| `doc/Lambda_Formal_Semantics.md` §S14.1 | the normative rulings this doc argues (S14.1.1v2, S14.1.2v2, S14.1.3), plus S2.5.2v2, S5.6, S6.2, S7.2.3, S10.3.1v3 |
| `doc/Lambda_Expr_Stam.md` § Extended For-Expression Clauses | the user reference |
| `Lambda_Design_Data_Processing.md` | source of the group/join extraction (§4.1, §5.1); keeps the DataFrame, verb, window and stream design |
| `Lambda_Issue_Ledger.md` [i8-genafterlet](<Lambda_Issue_Ledger.md#i8-genafterlet>) | open generator-after-`let` question |
| `Lambda_Design_Type_Virtual.md` | the `varray` carrier for FC11 Phase 2 |

- [XQuery 3.1 FLWOR expressions](https://www.w3.org/TR/xquery-31/#id-flwor-expressions)
- [PostgreSQL SELECT](https://www.postgresql.org/docs/current/sql-select.html)
- [C# LINQ query syntax](https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/concepts/linq/query-syntax-and-method-syntax-in-linq)
- [Scala for-comprehensions](https://docs.scala-lang.org/tour/for-comprehensions.html)
- [Haskell list comprehensions](https://wiki.haskell.org/List_comprehension)
- [Python generator expressions (PEP 289)](https://peps.python.org/pep-0289/)
