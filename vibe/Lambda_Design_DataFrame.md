# Proposal: Lambda DataFrame — Columnar Tables with Arrow Interop and CSV/TSV/SQL I/O

> **Scope.** Add a built-in **DataFrame** to Lambda — a columnar, named-table type in the spirit of R's `data.frame`, pandas, and Polars. Per the requirements, the DataFrame (1) is a **special/extended Lambda `Map`**, (2) stores data **columnar**, (3) has **Apache Arrow** support, and (4) loads/stores **CSV, TSV, and SQL tables**.

> **Status:** design proposal. Not yet implemented. It builds directly on the typed-array work (`ArrayNum`: N-D, views, vectorized math, masks, axis reductions — landed, baseline 3224/3224) and the existing relational-DB integration ([Lambda_IO_RDB.md](Lambda_IO_RDB.md): `input-rdb.cpp`, `rdb_query.*`, `lib/rdb.h`). The thesis below is that a Lambda DataFrame is *not* a new subsystem — it is a thin named-column layer over `ArrayNum`, glued to I/O paths that mostly already exist.

---

## 0. TL;DR

- A DataFrame is a **`Map` with `map_kind = MAP_KIND_DATAFRAME`** wrapping a native `DataFrame` struct — the same zero-overhead "native-backed Map" pattern that `Uint8Array`/DOM nodes already use ([lambda.h:526‑540](../lambda/lambda.h)). So `df` *is* a Map: `df.age` is member access that returns a column. **Constraint 1, satisfied by reuse.**
- Storage is **columnar**: an ordered list of named **columns**, each an independent typed buffer — a numeric/bool column is an `ArrayNum`; a text column is a string buffer; every column carries an optional **validity bitmap** for nulls. This is the R / Arrow / Polars model (independent typed columns), *not* pandas' historical BlockManager. **Constraint 2.**
- The column buffer layout is **Arrow-shaped on purpose** (contiguous data buffer + validity bitmap + offsets for strings), so a DataFrame exports/imports through the **Arrow C Data Interface** (two C structs, no library dependency) for zero-copy interchange with pandas/Polars/DuckDB. **Constraint 3.** The one new capability this forces — a **validity bitmap** — is *also* native `NA`/missing-value support, the gap the R comparison flagged. One feature, three payoffs.
- **I/O reuses what exists and adds the missing half:** load reuses `input-csv.cpp` (CSV; TSV = tab delimiter) and `input-rdb.cpp` (SQL tables over SQLite/PostgreSQL), materialized **columnar**; store adds a CSV/TSV **formatter** and a SQL **write path** (`CREATE TABLE` + bulk `INSERT`) — neither exists today. **Constraint 4.**
- Most *operations* come free: **filtering is `df[mask]`** (mask indexing, already built), **predicates are vectorized comparisons** (`df.age > 30` → a bool column, already built), **column math is vectorized ops**, **aggregation is axis reductions**. The genuinely new surface is small: a column-reference convention (`.field`), a handful of verbs, and `group`/`join`.

---

## 1. The DataFrame type — an extended `Map` (Constraint 1)

Lambda already has a battle-tested mechanism for "a `Map` that is really a native struct": the **native-backed Map**. A `JsTypedArray`, a DOM node, and a CSSOM rule are all `LMD_TYPE_MAP` values whose `map_kind` byte is stamped with a kind, whose `m->data` points at a native C struct, whose `m->type` points at a sentinel `TypeMap`, and whose `m->data_cap = 0` flags "native-backed." The DataFrame joins this family:

```c
// lambda.h — next free MAP_KIND after MAP_KIND_CSS_NAMESPACE = 12
MAP_KIND_DATAFRAME = 13,   // columnar table (native DataFrame* in m->data)
```

```c
typedef struct DataFrame {
    int64_t   nrows;
    int32_t   ncols;
    Column*   columns;      // ordered array, length ncols
    HashMap*  by_name;      // column name → index, for df.field lookup
    // index column (pandas-style row labels) — deferred to a later phase
} DataFrame;

typedef enum { COL_NUM, COL_BOOL, COL_STR } ColumnKind;

typedef struct Column {
    String*    name;        // namepool-owned column name
    ColumnKind kind;
    union {
        ArrayNum* num;      // COL_NUM / COL_BOOL — numeric & boolean lanes (reuses §typed-array)
        StrColumn* str;     // COL_STR — Arrow-style { int32* offsets; char* data } UTF-8 buffer
    };
    uint8_t*   validity;    // NULL ⇒ no nulls; else Arrow-compatible 1-bit-per-row null bitmap
    int64_t    null_count;
} Column;
```

The wrapping `Map` is built exactly as the typed-array constructors build theirs:

```c
Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
m->map_kind = MAP_KIND_DATAFRAME;
m->type     = &dataframe_type_marker;   // sentinel TypeMap
m->data     = df;                       // native DataFrame*
m->data_cap = 0;                        // native-backed
return (Item){ .map = m };
```

Consequences — all inherited from the existing pattern, no new machinery:

- **`df` is a `Map`.** Anything that accepts a map accepts a DataFrame; `get_type_id(df) == LMD_TYPE_MAP`.
- **`df.age` / `df["age"]`** resolve through the **exotic property gate** (the same gate that synthesizes `ta.length` from a `JsTypedArray`): a name that matches a column returns the column; otherwise normal map semantics. Metadata properties (`df.columns`, `df.shape`, `df.nrows`) are synthesized, not stored.
- **Lazy upgrade.** If a user writes a non-column property onto a DataFrame, the existing "native-backed → upgraded Map" path (`js_upgrade_native_backed_map_for_properties`) repoints `m->data` to a real field buffer and moves the `DataFrame*` under an internal `__df__` key — so DataFrames compose with arbitrary metadata without a bespoke mechanism.
- **GC.** A `gc_finalize_dataframe` mirrors `gc_finalize_typed_array` (free columns + buffers when the Map is swept), reusing the `gc_native_seen_t` dedup set.

This is the most direct possible reading of "a DataFrame shall be a special/extended type of `Map`": it reuses the exact subtype mechanism the runtime already ships.

---

## 2. Columnar storage (Constraint 2)

### 2.1 Why columnar, and which columnar model

Data analysis touches **few columns across many rows** (filter on one column, aggregate another). Columnar storage makes those values contiguous — cache- and SIMD-friendly, compressible, and skippable when unread. (This is the OLAP access pattern; see the row-vs-column rationale that motivates every analytical engine being columnar.)

Lambda adopts the **R / Arrow / Polars** model — *independent typed column buffers* — not pandas' historical **BlockManager** (which packs same-dtype columns into shared 2-D blocks and pays for it in copies and "consolidation"). Independent columns are simpler, make per-column type changes cheap, and map 1:1 onto Arrow arrays (§3). Notably, pandas itself is migrating toward this model in its Arrow backend, so Lambda starts where the ecosystem is converging.

### 2.2 Columns reuse the typed-array layer

A numeric or boolean column **is an `ArrayNum`**. That means the entire operation surface built this session is already the DataFrame's column engine:

| DataFrame need | Provided by (already built) |
|---|---|
| predicate `df.age > 30` → bool column | vectorized ordering comparison → `ELEM_BOOL` mask |
| filter `df[mask]` | `arr[mask]` boolean mask indexing |
| computed column `df.w / df.h ^ 2` | vectorized `ArrayNum` arithmetic + broadcasting |
| `avg(.age)`, `sum`, `min`, `max`, `count` | axis reductions (`sum`/`avg`/`min`/`max`) |
| numeric literal column auto-typing | `[…]` → `ArrayNum` promotion (incl. `ELEM_BOOL`) |

Only **string columns** fall outside `ArrayNum`. They use an Arrow-style `StrColumn { int32* offsets; char* data }` (a single UTF-8 byte buffer + an offsets array) rather than an array of `String*` pointers — so text columns are contiguous, compact, and Arrow-exportable. (A "native" `Array<String*>` representation is the ergonomic fallback for building; it materializes to the offset/data buffer on first Arrow export or bulk op.)

### 2.3 Missing values — the validity bitmap

Every column carries an optional **validity bitmap**: `NULL` means "no nulls" (the fast path), otherwise one bit per row (1 = valid, 0 = null), Arrow's exact representation. This is the single most important addition the DataFrame forces, and it pays off three ways at once:

1. **Native `NA`** — the statistical essential the R comparison flagged Lambda as lacking. `avg(.age)` can skip nulls; `df[.age == null]` selects missing rows.
2. **No float-upcast wart** — unlike classic (NumPy-backed) pandas, an integer column with a missing value stays integer; nullity is orthogonal to the value buffer, so `[1, null, 3]` is an `int64` column with one null bit set, not `[1.0, NaN, 3.0]`.
3. **Arrow compatibility** (§3) — the bitmap *is* Arrow's null buffer.

NA propagation follows R semantics: arithmetic with a null yields null; reductions take an explicit `skip_na` (default true for `avg`/`sum`, matching `na.rm=TRUE`).

---

## 3. Apache Arrow support (Constraint 3)

### 3.1 Arrow-shaped columns

Each `Column` is laid out to match an **Arrow array** so that interop is a pointer hand-off, not a re-encode:

| Column kind | Arrow array | Buffers |
|---|---|---|
| `COL_NUM` (int/float lanes) | primitive (`Int64`, `Float64`, `Int32`, …) | validity bitmap + data buffer |
| `COL_BOOL` | `Bool` | validity bitmap + data buffer — **caveat: Arrow packs bools 1-bit; Lambda `ELEM_BOOL` is 1 byte** → bool columns convert, not zero-copy |
| `COL_STR` | `Utf8` / `LargeUtf8` | validity bitmap + **offsets** buffer + UTF-8 **data** buffer |

A whole DataFrame maps to an Arrow **RecordBatch** (a schema + one child array per column). The element-kind overlap with `ArrayNum` is already strong (the `ELEM_INT8…FLOAT64` set lines up with Arrow primitives — see the typed-array Arrow analysis), so most columns need no conversion.

### 3.2 The C Data Interface — zero dependency, zero copy

The cheapest, most portable bridge is the **Arrow C Data Interface**: two POSIX C structs (`ArrowSchema`, `ArrowArray`) with a release callback. It requires **no Arrow library dependency** — Lambda just fills in the structs from its column buffers and hands them over.

```c
// export: DataFrame → Arrow C Data Interface (zero-copy where layout matches)
void dataframe_export_arrow(DataFrame* df, ArrowSchema* out_schema, ArrowArray* out_array);
// import: borrow an Arrow array from pandas/Polars/DuckDB as a DataFrame
Item   dataframe_import_arrow(ArrowSchema* schema, ArrowArray* array);
```

With this alone, a Lambda DataFrame and a Polars/pandas/DuckDB frame can share columns in-process without serialization. A later phase can add **Arrow IPC/Feather** (on-disk) and **Parquet** (compressed on-disk) readers/writers by linking the Arrow C++/Rust library — but the C Data Interface delivers the headline interop with no dependency.

### 3.3 The load-bearing caveat: moving GC vs stable Arrow buffers

This is the same constraint the typed-array→Arrow analysis surfaced, and it applies identically here. `ArrayNum` element data lives in the **compacting** GC data zone — the collector relocates buffers and rewrites pointers ([gc_heap.c:1203‑1231](../lib/gc/gc_heap.c)) unless `is_view`/`is_pinned` is set. Arrow consumers hold raw buffer pointers and expect them to **never move**. Therefore:

- **Export (Lambda → Arrow):** pin the column buffers (`is_pinned`) for the lifetime of the exported array (zero-copy), *or* copy them into non-moving (`mem_alloc`) buffers (safe default). The Arrow release callback unpins/frees.
- **Import (Arrow → Lambda):** the foreign buffer is already stable and non-moving; wrap each column as an `is_view` `ArrayNum` over it (the existing external-base view path), keeping the foreign array alive via the release callback. Zero-copy and natural — exactly as JS→Lambda was the easy direction.

So Arrow export is "pin or copy"; import is free. This asymmetry is documented, not hidden.

---

## 4. I/O — CSV, TSV, and SQL tables (Constraint 4)

Lambda's data sources are **row-oriented** today (a parsed CSV or a queried SQL table is an *array of maps*). A DataFrame is **columnar**. So loading is fundamentally a **shred** (rows → columns) and storing is an **unshred** (columns → rows). Half the machinery exists; this section adds the other half.

### 4.1 Load

| Source | Today | DataFrame addition |
|---|---|---|
| **CSV** | `input-csv.cpp` parses to rows | columnar materialization + **type inference** per column (int/float/bool/date/string) + null detection (empty / `NA`) |
| **TSV** | — | CSV parser parameterized with `\t` delimiter (TSV = tab-separated CSV) |
| **SQL table** | `input-rdb.cpp` + `lib/rdb.h` (SQLite/PostgreSQL driver vtable, read-only) loads tables as arrays of maps; `for`-clauses compile to SQL ([Lambda_IO_RDB.md](Lambda_IO_RDB.md)) | a column-wise fetch path: `step()` row-by-row but append into column builders, mapping `RdbColumn` SQL types → `ColumnKind` + validity from SQL `NULL` |

Two construction routes (see §5.1): `frame(input("data.csv"))` shreds the row-array a parser already produces (simple, reuses everything), or a **direct columnar reader** builds columns during the parse scan (skips row materialization — a fast path for large files). The SQL path naturally complements the existing RDB integration: `frame(db.data.book)` turns a (lazily SQL-queried) table into a columnar frame for analytics, and column types come from the RDB schema introspection that already exists.

### 4.2 Store

Both are **new** (the RDB layer is read-only today; no CSV formatter exists):

- **CSV / TSV writer** — a new `format-csv.cpp` (register `"csv"` / `"tsv"` in the format dispatcher), iterating columns row-major with proper quoting/escaping, configurable delimiter, header row, and a chosen NA spelling (empty or `NA`). `format(df, "csv")` / `output(df, "data.tsv")`.
- **SQL write-back** — extend the `RdbDriver` vtable with a write path: `CREATE TABLE` from the DataFrame schema (column name + inferred SQL type + nullability) and a parameterized bulk `INSERT` (batched, transactional), reusing the injection-safe parameter binding `rdb_query.cpp` already implements. `df |> to_sql(db, "people")`.

### 4.3 Type inference and round-tripping

CSV is untyped text, so load infers per-column types by scanning (all-integer → `Int64`; integer-or-empty → `Int64` + nulls; numeric-with-`.` → `Float64`; `true`/`false` → `Bool`; ISO dates → a date column; else `Utf8`), with an optional explicit schema override. NA round-trips losslessly via the validity bitmap (empty cell ↔ null bit). SQL types map through the `RdbColumn` metadata, so SQL→DataFrame→SQL preserves types and nullability.

---

## 5. Syntax

The design goal: a DataFrame grammar that *reuses* the typed-array primitives and Lambda's pipe, with one small new convention for column references. (Bare `a > 0` at statement level needs parens due to the `<`/`>` markup ambiguity; inside `df[...]` and verb arguments it does not.)

### 5.1 Construction

```lambda
// columnar literal (storage-shaped)
let df = frame{ name: ["Ana","Bo","Cy"], age: [30, 25, 41], city: ["NYC","LA","NYC"] }

// row literal — Lambda-native: a list of records, transposed (shredded) into columns
let df = frame([ {name:"Ana", age:30, city:"NYC"}, {name:"Bo", age:25, city:"LA"} ])

// from I/O (§4)
let df = frame(input("people.csv"))      // CSV
let df = frame(input("people.tsv"))      // TSV
let db = input(@./library.db)            // existing RDB integration
let books = frame(db.data.book)          // SQL table → columnar frame
```

### 5.2 Column references — leading-dot `.field`

Lambda has `obj.field` (member access on an explicit object). The DataFrame grammar adds a **leading dot with no object**, meaning "the `field` column of the frame in scope" — explicit (no R-style non-standard evaluation), terse (no `row =>` lambda), and a natural extension of existing `.field`:

```lambda
df.age           // explicit: the age column (an ArrayNum)
.age             // contextual: the age column of the piped / current frame
```

This is the only genuinely new piece of surface syntax. It mirrors Polars' `pl.col.age`, but terser.

### 5.3 Verbs — mostly typed-array ops in disguise

```lambda
df[.age > 30]                          // filter  — IS arr[mask] lifted to a frame
df[.city == "NYC" and .age > 30]       // compound predicate
df.{name, age}                         // select / project columns
df.{name, age} as {who, years}         // project + rename
df with { bmi: .weight / .height ^ 2,  // mutate: add computed columns
          adult: .age >= 18 }          //   (vectorized ops produce the new columns)
sort(df, .age desc, .name)             // order by
df | group(.city)                      // group
   | agg{ n: count(), avg_age: avg(.age), oldest: max(.age) }   // summarise
join(a, b, on: .id)                    // inner join (left/outer variants)
```

`df[mask]` filtering falls straight out of the `arr[mask]` semantics already implemented — the single strongest sign this grammar fits Lambda specifically.

### 5.4 A pipeline, and the Rosetta stone

```lambda
let report =
  frame(input("people.csv"))
  | where(.age > 30)                   // filter  (mask indexing underneath)
  | with { decade: .age / 10 |> floor }// mutate  (vectorized + pipe)
  | group(.city)
  | agg{ count: count(), avg_age: avg(.age) }
  | sort(.avg_age desc)

to_csv(report, "report.csv")           // store back (§4.2)
```

The same query across ecosystems (code block, since the syntaxes contain `|`):

```
SQL      SELECT city, AVG(age) FROM t WHERE age > 30 GROUP BY city
dplyr    t |> filter(age > 30) |> group_by(city) |> summarise(avg = mean(age))
pandas   t[t.age > 30].groupby("city")["age"].mean()
Polars   t.filter(pl.col.age > 30).group_by("city").agg(pl.col.age.mean())
Lambda   t | where(.age > 30) | group(.city) | agg{ avg: avg(.age) }
```

Lambda reads closest to **dplyr** (pipe + named verbs) and works like **Polars** under the hood (`.age` ≈ `pl.col.age` — explicit column expressions, no NSE).

---

## 6. Implementation plan (phased, test-gated)

**Phase 1 — the type + columnar storage.** `MAP_KIND_DATAFRAME`, the `DataFrame`/`Column` structs, `frame{…}` / `frame(rows)` construction, `df.field` column access via the exotic gate, GC finalizer. Numeric/bool columns reuse `ArrayNum`; string columns get `StrColumn`. *Tests:* construct, project, round-trip to/from row-lists.

**Phase 2 — the validity bitmap (NA).** Add the per-column null bitmap, null-aware reductions (`skip_na`), `null` literal in columns, `df[.x == null]`. *Tests:* missing-data reductions vs R semantics.

**Phase 3 — operations.** Wire the verbs: `where`/`df[mask]` (mask indexing), `select`, `with` (vectorized columns), `sort`, then `group`+`agg` (the hash-partition engine), then `join`. The `.field` contextual-column scoping in the transpiler is the load-bearing change here. *Tests:* the dplyr/pandas Rosetta queries.

**Phase 4 — CSV/TSV I/O.** Columnar CSV/TSV reader with type inference + nulls; new `format-csv.cpp` writer. *Tests:* round-trip CSV/TSV with types and NA preserved.

**Phase 5 — SQL I/O.** Columnar fetch from the existing RDB drivers (read); extend the `RdbDriver` vtable with `CREATE TABLE` + bulk `INSERT` (write). *Tests:* SQLite round-trip (table → frame → table).

**Phase 6 — Arrow.** Arrow C Data Interface export (pin-or-copy) and import (zero-copy view); the bool bit/byte conversion. *Tests:* round-trip a frame through pyarrow / DuckDB via the C Data Interface. Later: Feather/Parquet via the Arrow library.

Each phase is independently shippable; Phases 1–3 stand alone as an in-memory analytics layer even before any new I/O.

---

## 7. Risks & open questions

- **String columns are the awkward case.** Numeric columns are `ArrayNum` and get everything for free; text columns need their own `StrColumn` buffer + the Arrow offset/data layout, and don't benefit from the vectorized numeric fast paths. Decision: native `Array<String*>` for building, Arrow offset/data buffer as the canonical storage (materialize on first bulk op / export).
- **`.field` scoping.** Resolving a leading-dot against the frame in pipe scope is a real transpiler change and the linchpin of the ergonomics. Needs care to not collide with member access or break existing `.x` parses.
- **The Index question.** pandas' row `Index` (label alignment, `MultiIndex`, `DatetimeIndex`) is powerful but its most confusing feature; R/Polars largely omit it. **Recommendation: ship without a row index first** (positional rows, like Polars), add an optional index later only if demand appears.
- **Moving-GC ↔ Arrow.** As in §3.3 — export must pin-or-copy. This is the cost of letting a GC'd runtime share buffers; it's inherent, not a defect.
- **Mutability.** Lambda is functional; verbs return **new** frames (copy-on-modify, like R — *not* pandas' mutable views with their `SettingWithCopyWarning` footgun). In-place column mutation is out of scope.
- **Relationship to `Lambda_IO_RDB.md`.** That proposal makes a SQL table a lazily-queried *row* source (`for … in db.data.book`); this one makes a *columnar* frame for analytics (`frame(db.data.book)`). They compose: RDB is the lazy row gateway, DataFrame is the in-memory columnar materialization. The SQL *write* path proposed here also extends the RDB layer from read-only to read-write.

## 8. Non-goals (this proposal)

- Row index / `MultiIndex` / time-series resampling (pandas-style) — deferred.
- Lazy / out-of-core / multi-threaded query engine (Polars/DuckDB-style) — in-memory, eager first.
- Parquet/Feather on-disk formats — after the C Data Interface lands (needs the Arrow library).
- A full join optimizer, window functions, pivot/melt — start with inner/left join + group/agg; grow as needed.
- N-D / tensor columns — columns are 1-D (a DataFrame is a table, not a tensor; `ArrayNum`'s N-D stays a separate capability).

## 9. Success criteria

- `frame(...)` builds a columnar `MAP_KIND_DATAFRAME` from a columnar literal, a list-of-records, a CSV/TSV file, and a SQL table; `df.col` returns a column; `df` is accepted anywhere a `Map` is.
- The dplyr/pandas Rosetta query (`where → group → agg → sort`) runs and matches expected output, built on `arr[mask]` + vectorized comparisons + axis reductions.
- A column with missing values stays its declared type (no float upcast) and round-trips NA through CSV and SQLite losslessly.
- A DataFrame round-trips through the Arrow C Data Interface to pyarrow/DuckDB and back with types and nulls intact (bool via documented conversion).
- `to_csv` / `to_sql` write a frame back to CSV/TSV and a SQLite table.

---

## Appendix A — surfaces this builds on

| Surface | Location |
|---|---|
| Native-backed `Map` pattern + `MAP_KIND` enum | [lambda.h:526‑540](../lambda/lambda.h); JS typed-array constructors |
| `ArrayNum` columns (14 element kinds, masks, axis reductions, vectorized cmp) | [lambda-vector.cpp](../lambda/lambda-vector.cpp), [lambda-data-runtime.cpp](../lambda/lambda-data-runtime.cpp) (Typed Array 1/2) |
| Boolean mask indexing `arr[mask]` → filter | [lambda-vector.cpp `fn_mask_index`](../lambda/lambda-vector.cpp) |
| CSV input parser | [input-csv.cpp](../lambda/input/input-csv.cpp) |
| RDB integration (SQLite/PostgreSQL driver vtable, read-only, for→SQL) | [input-rdb.cpp](../lambda/input/input-rdb.cpp), [rdb_query.cpp](../lambda/input/rdb_query.cpp), [lib/rdb.h](../lib/rdb.h), [Lambda_IO_RDB.md](Lambda_IO_RDB.md) |
| Compacting GC + view/pinned skip (the Arrow stable-buffer constraint) | [gc_heap.c:1203‑1231](../lib/gc/gc_heap.c) |
| Arrow design constraints (element-kind map, C Data Interface, moving-GC) | [Lambda_Typed_Array3.md](Lambda_Typed_Array3.md) §3, §7 |
