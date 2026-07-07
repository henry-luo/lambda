# Lambda Data Processing — The Relational Layer (group by · join · window · DataFrame)

**Status:** design proposal — for discussion
**Date:** 2026-07-07
**Context:** follows the data-processing gap audit (2026-07-07 session) on top of the six-language feature comparison (`Lambda_Semantics_Features.md` Parts 2–3). The audit's conclusion: Lambda is a document-processing language with strong numerics; it is **thin precisely at the relational/columnar layer** — grouped aggregation, joins, window functions, and the tabular type that hosts them. This proposal covers that layer as one coherent design. It **absorbs** `Lambda_DataFrame.md` (columnar frame over `ArrayNum`) as its substrate and extends it with the missing relational operations.

**Scope corrections from the audit** (things Lambda already has, contrary to first impressions):
- **Regex** — supported through **string patterns** (`doc/Lambda_Type.md` §String Patterns): named, regex-like pattern types integrated into the type system.
- **Charts** — `lambda/package/chart/` is a working Vega-style chart library (13 mark types incl. bar/line/area/point/arc/boxplot/errorbar/errorband/rect, with scales/axes/legends/stacking) rendering to SVG.
- **SQL source** — basic SQLite connectivity exists via `input('file.db')` (`input-rdb.cpp`, driver vtable, read-only; `Lambda_IO_RDB.md`).

---

## 1. Scope and non-goals

**In scope:**
1. **`group by`** — as a for-clause (completing the FLWOR set) and as a frame verb, over one shared engine.
2. **Joins** — relational join (today's `join()` is *string* join, `lambda-eval.cpp:4343`): for-clause `on` syntax + a `join` verb with the standard kinds.
3. **Window / rolling functions** — `lag`/`lead`/`rank`/cumulative/rolling aggregates with explicit `over(...)`.
4. **DataFrame** — `Lambda_DataFrame.md` Phases 1–5 absorbed as the columnar substrate; this proposal supplies the full verb semantics (its Phase 3) plus windows and pivot/melt.

**Deferred to separate proposals:**
- **Columnar/binary I/O** — Arrow C Data Interface, Parquet/Feather, Avro (was `Lambda_DataFrame.md` Phase 6). The *in-memory Arrow-shaped column layout* stays (it is storage design, not I/O, and costs nothing now); the interchange surface moves out.
- **Lazy execution / query planning** — direction **decided** (§8: `stream()` sources, laziness carried by the value, `on error` handling); the detailed engineering design note (plan representation, operator coverage, resume semantics) is still a separate work item before its phase (P8).
- Distribution/RNG/stats library breadth (the "more math packages" track) — orthogonal, accretes independently.

---

## 2. Current state (audited 2026-07-07)

**Already in place — the layer below this proposal:**
| Capability | Where |
|---|---|
| FLWOR for-clauses: `let` / `where` / `order by` / `limit` / `offset` | grammar.js `for_*_clause` |
| Vectorized arithmetic, comparisons → bool masks, `arr[mask]` filtering | `lambda-vector.cpp` (Typed Array work) |
| Axis reductions: `sum`/`avg`/`min`/`max`; stats: `mean`/`median`/`variance`/`quantile` | sys funcs |
| SIMD auto-vectorization on `ArrayNum`, mutable views | Typed Array 4 |
| CSV/TSV input; SQLite input via RDB driver vtable (read-only, for→SQL pushdown) | `input-csv.cpp`, `input-rdb.cpp` |
| String patterns (regex-family validation in the type system) | `doc/Lambda_Type.md` |
| Charts to SVG (13 marks, scales/axes/legends) | `lambda/package/chart/` |
| Hash maps, sorting infrastructure | `lib/hashmap.h`, existing `order by` |

**Missing — what this proposal adds:** `group by` (any form), relational join (any form), window/rolling functions (any form), pivot/melt, the DataFrame type itself (proposal exists, unimplemented).

**Known correctness prerequisite:** `ArrayNum ==` is currently representation-sensitive rather than value-equality (open task from Typed Array 4). Grouping and join keys hash/compare by value equality (C8) — the engines below must be built on honest value equality, and numeric keys must obey the C2 numeric-tower rules (`1 == 1.0` → same group). **Fixing/specifying key equality is a gating work item, not a footnote** (see D8).

---

## 3. Design principle — two surfaces, one semantics

Lambda has two query surfaces, and both matter:

- **The for-comprehension** (row view) — the XQuery/FLWOR heritage. Works over *any* sequence: arrays of maps, **element children**, ranges. This is the document-native surface: `group by` over parsed XML/HTML/JSON must work without converting anything to a frame.
- **The pipe + verbs** (column view) — the dplyr/Polars shape, hosted by the DataFrame: `df |> where(...) |> group(...) |> agg{...}`.

**Rule: the two surfaces have identical semantics and are tested against each other.** The generic row engine (works on any sequence, row-at-a-time, hash-based) and the columnar frame engine (vectorized, `ArrayNum`-backed) are two implementations of one specified behavior — the Rosetta test suite runs every query both ways and diffs the results. This keeps the document world and the table world from drifting apart, which is Lambda's distinctive claim: *one relational algebra over both documents and tables*.

---

## 4. `group by`

### 4.1 For-clause (completes FLWOR)

LINQ-style explicit grouping — no XQuery-style implicit variable regrouping (Lambda's no-NSE explicitness bias):

```lambda
for (x in sales group by x.region into g)
    { region: g.key, total: sum(for (s in g) s.amount) }

// multi-key: key becomes a map, compared by value equality (C8)
for (x in sales group by (x.region, x.year) into g)
    { region: g.key[0], year: g.key[1], n: count(g) }
```

- `group by KEY_EXPR into g` — `g` is a group value: `g.key` (the key), and `g` itself iterates the members (a list). `into g` is mandatory (no implicit rebinding of the loop variable — explicit beats magic).
- Clause ordering: `group by` sits after `where`, before `order by`/`limit`/`offset` (which then apply to *groups*): filter rows → group → order/limit groups. An inner `where` on members is just a nested comprehension over `g`.
- Key equality/hash = **C8 value equality** with C2 numeric-tower coherence (`1` and `1.0` land in one group). Composite keys are list/map values — C8 already defines their equality. Grouping by `null` is allowed (nulls form one group, R/SQL-style — decide vs. drop, see D2).
- Works over any sequence — element children included: `for (p in doc? [para] group by p.style into g) ...`.

### 4.2 Verb surface (frames — from `Lambda_DataFrame.md`, semantics pinned here)

```lambda
df |> group(.city) |> agg{ n: count(), avg_age: avg(.age), oldest: max(.age) }
df |> group(.city, .year) |> agg{ total: sum(.amount) }
```

- `group(...)` returns a *grouped frame* (a frame + partition index); `agg{...}` collapses to one row per group. Non-`agg` verbs on a grouped frame apply per-group (enabling grouped `with` — which is how window functions arrive in grouped form, §6).
- Aggregate vocabulary (both surfaces): existing reductions (`sum`/`avg`/`min`/`max`/`mean`/`median`/`variance`/`quantile`) plus new: `count()`, `count(pred)`, `first`/`last`, `collect` (members as list), `n_distinct`. All null-aware per the validity-bitmap semantics (`skip_na` defaults, `Lambda_DataFrame.md` §2.3).

### 4.3 Engine

One hash-partition engine (hash on canonical value-hash consistent with C8; `lib/hashmap.h`): single pass to bucket row indices per key, then aggregate. The frame path aggregates columns vectorized per partition (gather → `ArrayNum` reduction, SIMD where lanes allow); the generic path folds row-at-a-time. Sorted-group optimization (input already ordered by key) is a later optimization, not v1.

---

## 5. Joins

### 5.1 For-clause: `on`

```lambda
// equi-join of two sources — hash join under the hood
for (o in orders, c in customers on o.cust_id == c.id)
    { id: o.id, name: c.name, total: o.total }

// left join: unmatched o's appear once with c = null
for (o in orders, c? in customers on o.cust_id == c.id)
    { id: o.id, name: if (c != null) c.name else "unknown" }
```

- Comma-joined sources with `on COND` — `COND` restricted to a conjunction of equality tests between the two sources' expressions (what a hash join can execute). Non-equi conditions belong in a following `where` (documented; the compiler rejects non-equi `on` with a clear error rather than silently going O(n·m)).
- `c?` marks the *optional* (null-padded) side — reads as "maybe a c": `left` join when on the second source. Full outer = both marked. (Syntax to confirm — D3; alternative is keywords `left join`/`outer join`, closer to SQL but heavier in a comprehension.)
- Without `on`, comma sources remain what they are today: nested iteration (cross product) — unchanged, backward compatible.

### 5.2 Verb surface

```lambda
join(a, b, on: .id)                          // inner, same-named key
join(a, b, on: (.cust_id, .id))              // left key, right key
join(a, b, on: .id, how: 'left')             // 'inner' | 'left' | 'right' | 'full' | 'semi' | 'anti' | 'cross'
```

- Column-name collisions: non-key common columns get suffixes (`name`, `name_b`) with an explicit `suffix:` option — pandas/Polars convention (D4).
- **As-of join** (`asof_join(a, b, on: .time, by: .ticker)` — nearest-earlier match, the kdb+/Polars time-series workhorse) is Phase W2 of the window track (§6.4), since it shares the sorted-partition machinery.

### 5.3 Engine

Classic hash join: build on the smaller side (hash of key values, C8-consistent), probe from the larger; null keys never match (SQL semantics — D2 pairs this with the group-by null decision). Semi/anti fall out of the probe. The frame path gathers matched row indices then materializes columns vectorized; the generic path emits rows. Join output row order: probe-side order, stable (documented — determinism matters under C1-family reproducibility expectations).

---

## 6. Window and rolling functions

### 6.1 Surface — expression-level, explicit `over(...)`

Window functions live in expressions (inside `with`/`agg`/comprehension bodies), carrying their partition/order explicitly — SQL's design, minus the implicit-frame footguns; no hidden state from a grouped context is *required* (though a grouped frame supplies a default partition):

```lambda
df |> with {
    prev_close: lag(.close, 1) over(part: .ticker, order: .date),
    change:     .close - lag(.close, 1) over(part: .ticker, order: .date),
    rank_day:   rank(.volume) over(part: .date),
    avg_7d:     rolling(avg, .close, 7) over(part: .ticker, order: .date),
    cum_total:  cum_sum(.amount) over(order: .date)
}

// on a grouped frame, the partition defaults to the grouping key:
df |> group(.ticker) |> with { avg_7d: rolling(avg, .close, 7) over(order: .date) }
```

### 6.2 Function vocabulary

| Family | Functions |
|---|---|
| Offset | `lag(x, n=1, default=null)`, `lead(x, n=1, default=null)` |
| Ranking | `row_number()`, `rank()`, `dense_rank()`, `ntile(n)` |
| Cumulative | `cum_sum`, `cum_prod`, `cum_min`, `cum_max`, `cum_count` |
| Rolling | `rolling(aggfn, x, n)` for any aggregate in §4.2's vocabulary; `expanding(aggfn, x)` |

All null-aware (validity bitmap); rolling windows shorter than `n` yield null by default (`min_periods:` option to relax — pandas convention).

### 6.3 Engine

Per partition: sort by the `order` key (reuse `order by` machinery), then a single forward pass. Offset/ranking/cumulative are trivially one-pass; rolling aggregates use prefix sums (sum/avg) or a monotonic deque (min/max). Columnar path runs per-partition over `ArrayNum` slices — SIMD-friendly. The generic (non-frame) path supports the same functions inside for-comprehension bodies over any sequence (D5 scopes how much of `over(...)` the row engine carries in v1).

### 6.4 Phasing within this track

- **W1:** offset + ranking + cumulative + count-based rolling, `over(part, order)`.
- **W2:** time/value-range windows (`rolling(avg, .x, '7d')` over datetime order keys) + **as-of join** (shared sorted-partition machinery).

---

## 7. DataFrame — absorbed substrate

`Lambda_DataFrame.md` stands as written for the type and storage: `MAP_KIND_DATAFRAME` native-backed Map; independent typed columns (`ArrayNum` numeric/bool, Arrow-style offset/data string columns); validity bitmap as native NA; `frame{...}`/`frame(rows)`/`frame(input(...))` construction; `.field` contextual column references; verbs `where`/`select`/`with`/`sort`; CSV/TSV/SQL I/O phases. This proposal **modifies its plan as follows**:

1. **Phase 3 (operations) is superseded by §4–§6 here** — `group`/`agg`, `join` (all kinds), and windows land with full semantics shared with the for-clause surface, not as frame-only sketches.
2. **Adds pivot/reshape to the verb set** (the always-forgotten tidying half): `pivot_longer(df, cols, names_to, values_to)` / `pivot_wider(df, names_from, values_from)` — dplyr/tidyr naming, straightforward on columnar storage. Phase: after group/agg (wider is group+spread; longer is a column unstack).
3. **Phase 6 (Arrow C Data Interface / Parquet) moves out** to the deferred columnar-I/O proposal (§1). The Arrow-shaped column *layout* is unchanged — it costs nothing now and keeps the door open.
4. `.field` contextual columns (its §5.2) get one addition: inside `over(...)` and `on:` arguments, `.field` resolves against the frame in pipe scope exactly as in verbs — one scoping rule everywhere (D6).

The DataFrame's open questions (string-column materialization, `.field` transpiler scoping, no row index) carry over unchanged.

---

## 8. Lazy execution — DECIDED direction (2026-07-07): laziness is a property of the chain's data

The audit identified lazy/streaming execution as the remaining structural piece (no predicate/projection pushdown, no out-of-core; dataset size capped by RAM; `Lambda_Semantics_Features.md` §3.1 row 2). Three design shapes were reviewed:

### 8.1 Options reviewed

- **(a) A lazy data structure** — a `stream`/lazy-frame value; laziness carried by the *data*, flowing through existing surfaces. *Verdict: right core.* Laziness must travel with the chain, and a value type is the only carrier that composes — but it needs an ergonomic entry point.
- **(b) A lazy option on functions** — `input(path, stream: true)` etc. *Verdict: wrong as the mechanism, right as the doorway.* **Laziness is a property of a whole chain, not one call** — flags sprinkled across N functions produce N² confusion about what is actually deferred. But an explicit *source-level* switch is exactly how a lazy chain should begin.
- **(c) Lazy `|>` / `for` evaluation** — the operators themselves become lazy. *Verdict: ruled out.* A global semantic change to existing code: errors move (`T^E` surfaces at the forcing point, not where the pipe was written), `pn` effect timing changes, debugging becomes "why hasn't this run yet." Haskell spent decades teaching this lesson; Polars deliberately kept eager defaults for the same reason.

**Decision (user-confirmed): the hybrid — (a) entered via a source constructor.**

### 8.2 Surface: `input()` is eager, `stream()` is lazy (D9)

```lambda
let data = input("big.json")      // eager: parse + materialize, today's semantics
var s    = stream("big.json")     // lazy: a stream value over the same source specifiers
```

- `stream(src)` accepts the same source specifiers as `input()` (paths, URLs, DB sources) and returns a **stream value** instead of a materialized document. The pair is symmetric and self-describing — no options, no flags.
- `stream(x)` over an **in-memory value** (array, frame, element tree) is also legal: a value-backed lazy chain (useful for deferring an expensive verb pipeline, and the entry point for lazy-frame query optimization).
- **`|>` and `for` are unchanged.** They dispatch on the operand: eager in → eager out (today's semantics, untouched); stream in → the stage is *recorded onto a plan*, not executed. **Terminal operations force**: `sum`/`count`/`first(n)`/`avg`/… reductions, `output(...)`, `format(...)`, and explicit `collect()` (materialize to an array/frame).
- Plan construction never performs I/O and never errors; all work and all errors happen at the forcing point.

### 8.3 Two stream kinds: value-backed vs live-I/O (D10)

| | Value-backed stream | Live-I/O stream |
|---|---|---|
| Source | in-memory value, or a re-readable pure source | socket, process output, growing file, DB cursor |
| Nature | a true **value**: re-forcible, COW-clean under C4 | a **one-shot resource**: forcing consumes it |
| Where usable | `fn` and `pn` | **`pn`-only** (creation and forcing) |
| Error surface | errors only from the pipeline stages themselves | I/O errors abundant — hence `pn` + `on error` (§8.4) |

The `pn` confinement of live-I/O streams is the same effect discipline as everything else in the language: live streaming *is* I/O, I/O is `pn`, and such sources fail in many ways (network, permissions, truncation, encoding) — exactly the errors §8.4 routes.

### 8.4 Error handling: `T^E` at the forcing point + `on error` handler (D12)

Forcing a stream surfaces failures as error values per J3/`T^E` — never mid-pipeline exceptions. For streaming `pn`s, an **`on error` handler** intercepts stream faults at the procedure boundary, reusing the view/edit template event-handler form (`grammar.js` `event_handler`: `on name(param) { body }`, `pn` body semantics):

```lambda
pn process() {
    var s = stream(io_source)
    s |> clean |> summarize |> output("report.json")   // stages are fn; forcing at output
}
on error(e) {
    // stream/IO faults during forcing arrive here: log, substitute, or abort
    log("stream failed: ", e.code)
}
```

- Without a handler, the error propagates as an ordinary `T^E` return from the forcing call (`^`-propagation applies as usual). The handler is interception, not a new error channel.
- **To be specified before implementation (D12 sub-items):** resume semantics — is `on error` abort-only (the whole forcing fails after the handler runs), or can it *skip* a bad record and continue (per-record recovery is what long-running ingestion actually needs)? Handler scoping when one `pn` forces multiple streams; interaction with `defer`/`with`-style cleanup (features doc §3.1) — these belong in one design note with the resource-cleanup construct, since a failed stream must also release its source.

### 8.5 The optimizer's license

`fn` purity makes plan optimization *provable*: pure stages can be fused, reordered, predicate- and projection-pushed with compiler-verified legality (Polars/DuckDB must trust their UDFs; Lambda verifies). **`pn` stages are barriers** — effects keep their order, no fusion across. A plan whose stages are all `fn` is also exactly what concurrency level 0 (`par` map, K9) can consume for free parallelism later.

---

## 9. Implementation phasing

| Phase | Contents | Gate |
|---|---|---|
| **P0** | Key-equality foundation: canonical value hash consistent with C8/C2; fix/spec `ArrayNum ==` value-equality (open task) | equality/hash property tests |
| **P1** | `group by ... into` for-clause (generic row engine, hash-partition) + aggregate vocabulary | Rosetta suite v1 (for-clause side); baseline green |
| **P2** | For-clause `on` equi-join (+ `?` optional side); relational `join()` verb naming resolved vs string-`join` (D7) | join semantics suite |
| **P3** | DataFrame Phases 1–2 (`Lambda_DataFrame.md`: type, columns, NA bitmap) | its Phase-1/2 tests |
| **P4** | Frame verbs: `where`/`select`/`with`/`sort`/`group`/`agg`/`join` on the columnar engine | **Rosetta cross-check: every query runs on both engines, results diffed** |
| **P5** | Windows W1 (offset/rank/cumulative/rolling-n, `over`) on both surfaces | window suite |
| **P6** | Pivot longer/wider; CSV/TSV/SQL I/O (DataFrame Phases 4–5) | round-trip tests |
| **P7** | Windows W2: time-range rolling + as-of join | time-series suite |

| **P8** | Streams: `stream()` sources, plan recording on `|>`/`for`, terminal forcing, `on error`; fn-fusion/pushdown optimizer after correctness | stream suite; preceded by the §8.4 design note (resume semantics + cleanup interaction) |

P1–P2 are pure language-level wins (no DataFrame needed — they work on arrays-of-maps and element trees immediately). P3+ builds the columnar fast path. P8's direction is decided (§8) but it deliberately comes after the relational core: verbs must exist eagerly before they can be recorded onto plans.

## 10. Decisions to confirm (D) — proposed, awaiting user confirmation

- **D1** — `group by KEY into g` (LINQ-explicit), group value = iterable with `.key`; clause order `where → group by → order by/limit` (groups). §4.1.
- **D2** — Null keys: nulls form one group in `group by` (R/SQL-GROUP-BY-style); null join keys never match (SQL-style). Asymmetry is intentional and documented — confirm.
- **D3** — Left-join marker: `c?` optional-source syntax vs `left join` keywords. Proposal: `c?`. §5.1.
- **D4** — Join collision handling: suffix convention + `suffix:` option. §5.2.
- **D5** — v1 scope of `over(...)` on the generic (non-frame) engine: full parity, or frame-only for W1 with generic parity in W2?
- **D6** — One `.field` scoping rule across verbs, `on:`, `over(...)`. §7.4.
- **D7** — Naming: relational `join()` vs existing string `join(list, sep)` — overload by arity/types, or rename one (e.g. string side becomes `str.join`)? Breaking-change surface, needs a call.
- **D8** — P0 (canonical value hash + `ArrayNum` value-equality fix) gates P1; confirm it may land as an engine change under the semantics ledger (C8) without new syntax.

**Confirmed (2026-07-07) — the lazy-execution set:**

- **D9 ✓** — `input()` eager / `stream()` lazy: symmetric source pair, same source specifiers; `stream(x)` over in-memory values also legal. §8.2. *(user-confirmed)*
- **D10 ✓** — Hybrid lazy model: laziness carried by the stream value through **unchanged** `|>`/`for` (dispatch, not new operator semantics); terminal ops force; plan-build never errors or does I/O. Value-backed streams are values (re-forcible); live-I/O streams are one-shot resources, **`pn`-only**. §8.2–8.3. *(user-confirmed)*
- **D11 ✓** — `fn` stages fusible/reorderable/pushable (compiler-verified purity); `pn` stages are plan barriers. §8.5. *(user-confirmed by adopting the hybrid)*
- **D12 ✓ (direction)** — Stream faults handled via `on error(e) { ... }` on the enclosing `pn`, reusing the view/edit event-handler form; without a handler, normal `T^E` propagation. **Sub-items to spec:** abort-only vs skip-and-continue resume, multi-stream scoping, interaction with the future `defer`/`with` cleanup construct. §8.4. *(user-confirmed pattern; details open)*

---

## Appendix — relationship to other proposals

| Doc | Relationship |
|---|---|
| `Lambda_DataFrame.md` | absorbed; Phase 3 superseded by §4–6, Phase 6 deferred out, pivot added |
| `Lambda_IO_RDB.md` | unchanged; RDB stays the lazy row gateway; SQL write-back arrives via DataFrame Phase 5 |
| `Lambda_Semantics_Features.md` | this implements Part 3's `group by` row and the data-frame verb steal; the streaming row's direction is now decided here (§8, D9–D12) |
| `Lambda_Concurrency_Design.md` | level 0 (`par` map) naturally accelerates the columnar engine later; K5 flat sharing pairs with columnar buffers across isolates |
| Future: columnar/binary I/O proposal | Arrow C Data Interface, Parquet/Feather — deferred from here and from DataFrame Phase 6 |
