# Lambda For-Expression Clauses 2 — `group by` and join `on`

**Status:** S1 implemented for single-source `group by ... into g`; join `on` remains deferred
**Date:** 2026-07-10 (revised 2026-07-11 ×3: S1 implemented — group-by surface reworked per user as `group by KEY [as ALIAS], ... into g` where **`g` is an element**: keys = attributes, members = children. The interim XQuery loop-var regrouping was withdrawn (loop var would change type item→list); FC1/FC3 finalized, FC9 tightened)
**Context:** extracted from `Lambda_Design_Data_Processing.md` (§4.1, §5.1) as the *settled, implement-first* subset of the relational layer. This doc covers only the **for-expression surface**: the `group by` clause and the join `on` clause. The DataFrame type, the pipe-verb surface (`group()`/`agg()`/`join()`), and window functions (`over(...)`) remain in the data-processing proposal — they need more consideration and are **not** part of this doc's scope. Successor to `Lambda_Expr_For_Clauses.md`, which shipped `where`/`order by`/`limit`/`offset` (implemented and tested, `test/lambda/for_clauses_test.ls`) and put `group by ... as` into the grammar.

---

## 1. Current state (audited 2026-07-10)

| Piece | Grammar | AST | Transpile/Eval | Tests |
|---|---|---|---|---|
| `where` / `order by` / `limit` / `offset` | ✅ `grammar.js` `for_*_clause` | ✅ | ✅ | ✅ `for_clauses_test.ls` |
| `group by ... into g` (single source) | ✅ `group_key_spec` + `into` form generated from `grammar.js` | ✅ per-key alias/inference, duplicate-name errors, `g` registered as `TYPE_ELMT`, post-group scope isolated | ✅ C and MIR transpilers materialize real `<group>` elements via shared canonical key hashing; groups emit in first-appearance order; post-group `order by`/`limit`/`offset` operate on groups | ✅ `test/lambda/for_group_test.ls` |
| join `on` / optional side `?` | ❌ | ❌ | ❌ | ❌ |

So S1 `group by` is now live for the single-source row engine. Join `on` is still a **small grammar addition** plus a hash-join engine both features share.

---

## 2. `group by` clause

### 2.1 Syntax — revised 2026-07-11 (user-decided)

```
for ( BINDINGS [, let N = E ...]
      [where COND]
      [group by KEY_EXPR [as ALIAS] [, KEY_EXPR [as ALIAS] ...] into NAME]
      [order by ...] [limit ...] [offset ...] )
    BODY
```

- **`as ALIAS`** names each individual grouping key (optional — see the FC9 inference rule);
- **`into NAME`** binds the **group element**: grouping keys are its *attributes* (one per key, under its alias/inferred name), and the group's members are its *children*. One value carries the whole group, using the element duality Lambda already has (`Element : List` + attribute map, lambda.hpp:445) — no new "group value" shape.
- **The loop variable goes out of scope** after `group by` (LINQ `into` behavior). The interim XQuery regrouping idea — rebinding the loop var to the member list — was **withdrawn**: the variable would change type from item (before the clause) to list (after), a type instability worse than the ergonomic gain. Everything lives under `g`.

```lambda
// single key — inferred attr: g.region; members are g's children
for (x in sales group by x.region into g)
    { region: g.region, total: sum(g |> ~["amount"]) }

// multiple keys — named attributes, no positional g.key[i]
for (o in orders group by o.year, o.month into g)
    { year: g.year, month: g.month, n: len(g) }

// computed key — alias required (not inferable)
for (w in words group by len(w) as wlen into g)
    { length: g.wlen, n: len(g) }

// grouping by the loop item itself — alias required (no trailing field)
for (w in doc.words
     where len(w) > 3
     group by w as word into g
     order by len(g) desc
     limit 10)
    { word: g.word, freq: len(g) }

// a group IS data: elements format/query directly
for (x in sales group by x.region into g) g
// → list of <group region:...; ...member items...> elements
```

The same clauses apply to the for-**statement** form (`for_stam` shares `for_clauses` in the grammar).

Grammar rework (`grammar.js` — replaces the shipped `for_group_clause`, which never transpiled, so nothing breaks):

```js
group_key_spec: $ => seq(
    field('key', $._expr),
    optional(seq('as', field('alias', $.identifier)))
),
for_group_clause: $ => seq(
    'group', 'by',
    field('spec', $.group_key_spec), repeat(seq(',', field('spec', $.group_key_spec))),
    'into', field('name', $.identifier)
),
```

**FC1-F — the group binding form (final; supersedes FC1 v1 `as g`+`g.key`/`g.items` and the interim FC1-R regrouping form).** Design rationale:

- **`g` is an element** — the group *is* one value with keys as named attributes (`g.year`, `g.month` — no positional `g.key[i]`) and members as children (`len(g)`, `g[0]`, `g |> ~["amount"]` under the current pipe-projection syntax). This reuses Lambda's signature dual-natured type instead of inventing a group shape; the earlier "no dual-natured group value" objection was about a *new* magic value, and an element is not new.
- **A group is a document node**: it formats, outputs, and query-expresses like any element. `group by` literally turns flat data into markup — the XML/XQuery heritage without XQuery's variable magic.
- **Element tag**: fixed symbol `'group'` (predictable for formatting/validation; the binding name identifies it in code, the tag identifies it in output). *(user-confirmed 2026-07-11)*
- `as` = per-key alias, `into` = group binding (LINQ's `into` keyword).

### 2.1b Key naming — the FC9 inference rule (tightened 2026-07-11)

When `as` is omitted, the key's attribute name on `g` is inferred **only** from a trailing field access:

1. **Trailing field access** → the field name: `group by o.year` → `g.year`; `group by o.date.year` → `g.year`.
2. **Anything else** — including a bare loop variable (`group by w`), calls, arithmetic, comparisons — → **compile error** demanding `as`. No auto-generated `key1`/`expr1` names. (A for-expr key always references the loop variable, so unlike SQL there is no bare-column case; the bare *loop-var* case `group by w as word` takes an alias, which also avoids `w`-the-key shadowing confusion.)
3. **Collision** — two keys resolving to the same name (`a.region, b.region`, or an alias clashing with an inferred name) → compile error demanding an alias.

Why not generate names? Surveying SQL: the standard leaves unnamed derived columns *implementation-dependent*, and practice is a wasteland — PostgreSQL falls back to `?column?`, MySQL/SQLite/DuckDB use the raw expression text (not an identifier), SQL Server yields an unreferenceable `(No column name)`, and BigQuery's positional `f0_, f1_` renumber silently when keys are added or reordered. The workable precedent is **LINQ's projection-initializer inference** (member access → member name, else explicit alias required) and Polars' leftmost-column-name rule — which is exactly rules 1–3.

### 2.2 Semantics

- **FC2 — key equality is C8 value equality with C2 numeric-tower coherence.** `1` and `1.0` land in the same group. Multi-key: grouping compares the key *tuple* element-wise by value equality; the keys surface as **named attributes** on `g` (FC1-F/FC9), not a positional list. **Null keys are allowed and form one group** (R/SQL-`GROUP BY` style; note the deliberate asymmetry with joins, where null keys never match — FC6).
- **FC3-F — scoping after `group by` (final): loop variables and per-tuple `let` bindings go out of scope**; only the `into` binding (plus enclosing scope) is visible in `order by`/`limit`/`offset` and the body. The XQuery-regrouping alternative (loop var rebinds to the member list) was considered and **withdrawn** — a variable silently changing type from item to list across a clause boundary is a type instability Lambda should not carry. Members are reached through `g`'s children (`g |> ~["amount"]`, `g[0]`). Multi-source comprehensions: children are the joined tuples (maps of the source bindings) — spec'd precisely with the join work in S3.
- **FC4 — deterministic group order:** groups are emitted in **first-appearance order of their key** in the (post-`where`) input. `order by` then reorders groups explicitly. No hash-order leakage.
- **Clause order** (already the grammar's documented fixed order): `where` filters *rows* before grouping; `order by`/`limit`/`offset` after `group by` apply to *groups*.
- **Works over any sequence** — arrays of maps, ranges, element children, RDB rows: `for (p in doc? [para] group by p.style as g) ...`. No frame required.

### 2.3 What v1 does **not** include (deferred, stays in the data-processing doc)

- A `having`-style post-group filter clause (workaround: pipe the result — `(for (...) ...) that (...)` — or a nested `if`-shaped body is not possible, so filtering groups pre-projection needs the pipe).
- Post-group `let` clauses (grammar currently only allows `let` before the clause block).
- The aggregate vocabulary additions (`count()`, `n_distinct`, `first`/`last`, `collect`) — existing `len`/`sum`/`avg`/`min`/`max`/`median`/... over the rebound member list cover v1.

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
- **group by:** single pass over the post-`where` tuple stream, bucketing tuples per key (insertion-ordered buckets → FC4 for free); then **materialize one real `Element` per group** (MarkBuilder: tag `'group'`, attributes from the key values under FC9 names, children = the bucket's member Items — a copy of 64-bit handles, cheap); then evaluate `order by`/`limit`/`offset`/body per group.
- **Two implementation phases (user-confirmed 2026-07-11):**
  - **Phase 1 — real Element.** Materialize as above. One handle-copy per member; every existing element code path (iteration, indexing, `len`, formatters, query expressions) works for free. This is what S1 ships.
  - **Phase 2 — `VElmt`, a separate virtual type.** Zero-copy group views over the hash bucket. **`VElmt` is its own type, not an extension of `VMap`** — mirroring how `Element` is separate from `Map` in the concrete world. The existing `VMap` (lambda.hpp:460) stays map-only (`get/set/count/keys/key_at/value_at/destroy`, presents as `"map"`); `VElmt` gets its own vtable covering both natures — attribute side (`attr_get`, `attr_keys`, `attr_count`) and child side (`child_at`, `child_count`) — its own `LMD_TYPE_VELMT` tag presenting as `"element"` (the same transparency pattern as `LMD_TYPE_VMAP` → `"map"`, lambda-data.cpp:117), and dispatch in every element-consuming path. Group backing: `data` = the bucket, `child_at` indexes it directly, attrs = the key values. Phase 2 switches group construction from MarkBuilder materialization to a `VElmt` over the bucket; it also serves the Jube native-module projection track and the columnar/frame work.
- **join `on`:** classic hash join — build a hash table on the **new source** keyed by its side of the equalities, then iterate the prior tuple stream in order and probe (FC7 for free). Left join emits the null-padded tuple on probe miss.
- Both paths are row-engine only (tuples of Items) — no columnar/SIMD work in this doc; that arrives with the DataFrame track.

S1 implementation lives in the transpiler paths (`transpile.cpp` + `transpile-mir.cpp`) with runtime helpers in `lambda-data-runtime.cpp` (`lambda_item_hash`, `lambda_item_compare`, `fn_group_by_keys*`). The future join path should reuse the same canonical key hash/compare helpers.

---

## 5. Implementation plan

| Step | Contents | Gate |
|---|---|---|
| **S0** | Canonical value hash (C8/C2-consistent) as a runtime helper | ✅ implemented as `lambda_item_hash` / `lambda_item_compare`; reused by `VMap` |
| **S1** | `group by ... into g`: grammar rework (`group_key_spec` + `into`; `make generate-grammar`); build_ast — FC9 name inference/collision errors, register `g` as element type, loop vars/lets invalid post-group (FC3-F); transpile.cpp + transpile-mir.cpp codegen with real group-element materialization | ✅ `test/lambda/for_group_test.ls` + expected `.txt`; `make test-lambda-baseline` green (3298/3298) |
| **S2** | Grammar: `?` marker + `on` condition on `loop_expr`; `make generate-grammar`; AST (`AstJoinOn` on the loop node, FC5 equi-conjunction validation with clear error) | parse + rejection tests |
| **S3** | Hash-join codegen (inner + left), both transpilers | `test/lambda/for_join_test.ls` + expected `.txt`; baseline green |
| **S4** | Docs: `doc/Lambda_Expr_Stam.md` query-expression section + cheatsheet | — |
| **S5** | Phase 2: `VElmt` — new virtual element type (own vtable: `attr_get`/`attr_keys`/`attr_count` + `child_at`/`child_count`; `LMD_TYPE_VELMT` → `"element"`); dispatch in element-consuming paths; switch group construction to zero-copy `VElmt` over the bucket | element-semantics parity tests (same `.ls` goldens pass under Phase 1 and Phase 2); baseline green |

Per repo rules: every new `.ls` test gets its expected-result `.txt`; `make test-lambda-baseline` must stay 100%.

---

## 6. Settled decisions (FC ledger)

- **FC1-F ✓** — group form is `group by KEY [as ALIAS], ... into g`: **`g` is an element** — keys = named attributes, members = children, tag `'group'`. Final 2026-07-11; supersedes FC1 v1 (`as g` + `g.key`/`g.items`) and interim FC1-R (XQuery loop-var regrouping, withdrawn for item→list type instability). §2.1
- **FC2 ✓** — key equality = C8 value equality + C2 numeric coherence; multi-key compares the key tuple element-wise, surfaced as named attributes on `g`; null keys form one group. §2.2
- **FC3-F ✓** — after `group by`, loop variables and per-tuple lets are **out of scope**; only the `into` element remains; members reached via `g`'s children. §2.2
- **FC4 ✓** — groups emit in first-appearance key order (deterministic). §2.2
- **FC5 ✓** — `on` = conjunction of equalities only; non-equi `on` is a compile error pointing at `where`. §3.2
- **FC6 ✓** — `c? in src on ...` = left (null-padded) join; null join keys never match; v1 = inner + left only, full/right outer deferred to the verb surface. §3.2
- **FC7 ✓** — join output preserves prior-stream order, stable. §3.2
- **FC8 ✓** — canonical value hash is the shared prerequisite step (S0). §4
- **FC9 ✓** — omitted `as`: key name inferred from a **trailing field access only**; anything else — including a bare loop variable — is a compile error demanding `as`; name collisions are compile errors. No generated `key1`/`expr1` names (BigQuery's `f0_` wart; SQL has no usable convention — LINQ inference is the model). §2.1b *(user-confirmed)*
- **FC10 ✓** — group element tag = fixed symbol `'group'`. §2.1 *(user-confirmed)*
- **FC11 ✓** — two-phase construction: Phase 1 materializes a real `Element` (S1); Phase 2 implements **`VElmt` as a separate virtual type from `VMap`** — parallel to the `Map`/`Element` separation — and switches groups to zero-copy views (S5). §4 *(user-confirmed)*

**Deferred (remain open in `Lambda_Design_Data_Processing.md`):** DataFrame type, pipe-verb surface (`group()`/`agg()`/`join()` and `~.field` verb scoping), window functions (`over(...)`, D14/D16), aggregate vocabulary, `having`-style group filter, post-group `let`, full/right/semi/anti joins, as-of join, pivot/melt, lazy streams (P8). (`VElmt` is **not** deferred — it is committed as Phase 2 / S5 per FC11; it additionally serves the Jube native-module projection track and the columnar work.)

---

## Appendix — relationship to other docs

| Doc | Relationship |
|---|---|
| `Lambda_Expr_For_Clauses.md` | predecessor: designed `where`/`order by`/`limit`/`offset` (shipped) and the old `group by ... as g` surface (grammar shipped but never transpiled; **replaced** by FC1-R's `as`-alias + `into` form) |
| `Lambda_Design_Data_Processing.md` | source of the extracted designs (§4.1 group by, §5.1 join `on`); its §4.1 sketch is superseded by FC1-R/FC9; everything columnar/verb/window stays there |
| `doc/Lambda_Formal_Semantics.md` | C8 value equality and C2 numeric-tower rules are the normative authority for key equality/hash |
