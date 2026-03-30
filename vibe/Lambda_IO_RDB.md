# Lambda IO: Relational Database Integration

## 1. Overview

This proposal adds **transparent relational database access** to the Lambda runtime, starting with SQLite. A database file is treated as a first-class Lambda data source — conceptually a document containing named tables, each table an array of maps. Lambda's existing `for` clauses (`where`, `order by`, `limit`, `offset`) map naturally to SQL queries, enabling declarative data access without writing SQL.

```lambda
let db = input(@./library.db)

// db is a structured element: <db name:..., schema:{...}, data:{...}>
// tables accessed via db.data namespace
db.data.book             // the book table
db.data.book[0]          // first row

// query books — for clause compiles to SQL
for (b in db.data.book where b.year >= 2020 order by b.title limit 10) b

// transform results in Lambda (post-SQL)
for (b in db.data.book where b.genre == "sci-fi")
    <card title: b.title, author: b.author_name>
```

### Design Principles

1. **Transparent** — databases accessed through the same `input()` function as JSON, CSV, YAML
2. **Functional** — read-only (SELECT) in this phase; no mutations, no side effects
3. **Lazy** — table data loaded on demand, not eagerly; query pushdown minimizes I/O
4. **Typed** — database schema auto-mapped to Lambda types at connection time
5. **Composable** — SQL handles filtering/sorting/pagination; Lambda handles transformation

---

## 2. Data Model Mapping

### 2.1 Database → Lambda Structure

A SQLite database file maps to a Lambda **element** with two top-level namespaces — `schema` (metadata) and `data` (table contents):

```
SQLite file           →  <db> element
  schema              →  map of table schemas, indexes, triggers, functions
  data                →  map of named table elements
    table             →  <table> element (tag = table name) containing rows
      row             →  child map (tag = table name, fields = columns)
        column        →  map field
```

Concrete example — a `library.db` with tables `book` and `author`:

```lambda
let db = input(@./library.db)

// db structure (conceptual — rows are lazy, not materialized):
// <db name: 'library.db',
//     schema: {
//         book: {columns: [{name: 'id', type: int, pk: true}, {name: 'title', type: string}, ...],
//                indexes: [...],
//                foreign_keys: [{column: 'author_id', ref_table: 'author', ref_column: 'id'}],
//                ...},
//         author: {columns: [{name: 'id', type: int, pk: true}, {name: 'name', type: string}, ...],
//                  indexes: [...],
//                  reverse_fks: [{from_table: 'book', from_column: 'author_id', column: 'id'}],
//                  ...},
//         book_stats: {view: true, columns: [...]},    // views included
//         ...
//     },
//     data: {
//         book: <table name: 'book'                    // table element
//             <book id: 1, title: "Dune", author_id: 3, year: 1965>
//             <book id: 2, title: "Neuromancer", author_id: 5, year: 1984>
//             ...
//         >,
//         author: <table name: 'author'                // table element
//             <author id: 3, name: "Frank Herbert", born: 1920>
//             <author id: 5, name: "William Gibson", born: 1948>
//             ...
//         >,
//         book_stats: <table name: 'book_stats'        // view — same interface as table
//             ...
//         >,
//     }
// >
```

### 2.2 Table & View Access

Tables and views are accessed through the `data` namespace:

```lambda
db.data.book         // the book table element (lazy — no rows loaded yet)
db.data.author       // the author table element
db.data.book_stats   // a view — accessed identically to a table
```

Schema metadata is accessed through the `schema` namespace:

```lambda
db.schema.book                  // book table schema: {columns: [...], indexes: [...], ...}
db.schema.book.columns          // [{name: 'id', type: int, pk: true}, ...]
db.schema.book_stats.view       // true (distinguishes views from tables)
```

Each table/view element behaves as an **iterable array of maps**:

```lambda
db.data.book[0]      // first row: <book id: 1, title: "Dune", ...>
len(db.data.book)    // row count (SELECT COUNT(*))
db.data.book | ~.title  // all titles: ["Dune", "Neuromancer", ...]
```

**Views** are exposed identically to tables. SQLite views respond to `SELECT` the same way — the `for` clause→SQL compilation works unchanged. The only difference is that views appear with `view: true` in the schema.

### 2.3 Column Type Mapping

SQLite column affinities map to Lambda types:

| SQLite Affinity | SQLite Types | Lambda Type | Notes |
|-----------------|-------------|-------------|-------|
| INTEGER | `INTEGER`, `INT`, `BIGINT`, `SMALLINT`, `TINYINT` | `int` | |
| INTEGER | `BOOLEAN` | `int` | 0/1; Lambda `bool` coercion available |
| REAL | `REAL`, `DOUBLE`, `FLOAT`, `NUMERIC` | `float` | |
| TEXT | `TEXT`, `VARCHAR`, `CHAR`, `CLOB` | `string` | |
| TEXT | `DATE`, `DATETIME`, `TIMESTAMP` | `datetime` | Auto-parsed (see §2.3.1) |
| TEXT | `JSON` | `map \| array` | Auto-parsed (see §2.3.2) |
| BLOB | `BLOB` | — | Deferred to Phase 2 |
| NUMERIC | `DECIMAL` | `decimal` | |
| NULL | — | `null` | |

**Nullable columns** map to union types: a nullable `TEXT` column becomes `string | null`.

#### 2.3.1 Datetime Column Mapping

SQLite stores dates as TEXT (ISO-8601) or INTEGER (Unix timestamp). When a column's declared type is `DATE`, `DATETIME`, or `TIMESTAMP`, the runtime auto-converts to Lambda `datetime`:

```lambda
// Column declared as: created_at DATETIME NOT NULL
db.data.book[0].created_at          // datetime value: 2024-03-15T10:30:00
type(db.data.book[0].created_at)    // 'datetime'

// TEXT format recognized: "YYYY-MM-DD", "YYYY-MM-DD HH:MM:SS", ISO-8601
// INTEGER format recognized: Unix timestamp (seconds since epoch)
```

The SQL query layer preserves the raw SQLite representation; conversion to `datetime` happens at the row→Lambda map boundary. For `where` clause pushdown, datetime comparisons emit SQL that compares against the stored format (text or integer) using SQLite's built-in date functions when needed.

#### 2.3.2 JSON Column Support

SQLite supports JSON via its [JSON1 extension](https://www.sqlite.org/json1.html) (included in the amalgamation). When a column's declared type is `JSON`, the runtime **auto-parses** the stored JSON text into Lambda data structures:

```lambda
// Column declared as: metadata JSON
// Stored value: '{"tags": ["sci-fi", "classic"], "rating": 4.5}'

db.data.book[0].metadata             // {tags: ["sci-fi", "classic"], rating: 4.5}
db.data.book[0].metadata.tags        // ["sci-fi", "classic"]
db.data.book[0].metadata.rating      // 4.5
type(db.data.book[0].metadata)       // 'map'
```

JSON parsing reuses Lambda's existing `input-json.cpp` parser. The parsed result is a standard Lambda map/array — all pipe operators, `for` clauses, and pattern matching work on it:

```lambda
// Filter on JSON field — NOT pushed to SQL (evaluated in Lambda post-filter)
for (b in db.data.book where "sci-fi" in b.metadata.tags) b.title

// Future: SQLite json_extract() pushdown
// for (b in db.data.book where json(b.metadata, '$.rating') > 4.0) b.title
// → SELECT * FROM book WHERE json_extract(metadata, '$.rating') > 4.0
```

**Type mapping**: JSON values map to Lambda types as follows:

| JSON type | Lambda type |
|-----------|------------|
| `object`  | `map`      |
| `array`   | `array`    |
| `string`  | `string`   |
| `number` (integer) | `int` |
| `number` (float) | `float` |
| `true`/`false` | `bool` |
| `null`    | `null`     |

### 2.4 Schema → Lambda Type

The database schema is introspected at connection time and auto-mapped to Lambda types:

```lambda
// Auto-generated from CREATE TABLE book (id INTEGER PRIMARY KEY, title TEXT NOT NULL, 
//   created_at DATETIME, metadata JSON, ...)
type Book = {id: int, title: string, author_id: int, year: int, 
             genre: string | null, created_at: datetime, metadata: map | null}

// Auto-generated from CREATE TABLE author (id INTEGER PRIMARY KEY, name TEXT NOT NULL, ...)
type Author = {id: int, name: string, born: int | null}
```

These types are available for pattern matching, validation, and documentation:

```lambda
type(db.data.book[0])         // Book
db.data.book[0] is Book       // true
```

Row maps carry the table's shape, so field access is O(1) via shaped slot lookup (same as Lambda's existing `ShapePool` mechanism).

### 2.5 FK Relationship Navigation

Foreign key constraints are introspected at schema load time. When a row map field matches a FK target table, the runtime **auto-resolves** the relationship:

```lambda
// book.author_id references author.id (FK constraint)
let b = db.data.book[0]

b.author_id         // 3 (raw FK column — always available)
b.author            // <author id: 3, name: "Frank Herbert", born: 1920> (auto-resolved)
b.author.name       // "Frank Herbert" (traverse into resolved row)
```

**Resolution rules**:
- `b.author` — if `author_id` references `author.id`, strip the `_id` suffix to derive the link name `author`
- If the FK column is just `author` (no `_id` suffix), access via `b.author` returns the resolved row (not the raw int); use `b["author"]` for the raw value
- **Many-to-one** (book → author): returns a single row map
- **One-to-many** (author → books): returns a lazy array of related rows

```lambda
// Reverse navigation: author → their books
let a = db.data.author[0]
a.book              // [<book id:1, ...>, <book id:2, ...>] (all books by this author)
a.book | ~.title    // ["Dune", "Foundation"]
```

**Performance**: FK lookups are lazy (triggered on field access) and cached. Inside `for` clauses, the query compiler batches FK lookups into JOINs (see §7.2, Idea 1).

---

## 3. Query Mapping: `for` Clause → SQL

The key insight: Lambda's `for` clause syntax has direct SQL equivalents.

### 3.1 Clause-to-SQL Mapping

| Lambda `for` clause | SQL equivalent |
|---------------------|---------------|
| `for (x in db.table)` | `SELECT * FROM table` |
| `where <condition>` | `WHERE <condition>` |
| `order by x.col` | `ORDER BY col ASC` |
| `order by x.col desc` | `ORDER BY col DESC` |
| `limit N` | `LIMIT N` |
| `offset N` | `OFFSET N` |
| `let y = x.col` | column aliasing / client-side binding |

### 3.2 Expression-to-SQL Mapping

Within `where` clauses, Lambda expressions map to SQL:

| Lambda expression | SQL expression |
|-------------------|---------------|
| `x.col == val` | `col = val` |
| `x.col != val` | `col != val` |
| `x.col > val` | `col > val` |
| `x.col >= val` | `col >= val` |
| `x.col < val` | `col < val` |
| `x.col <= val` | `col <= val` |
| `cond1 and cond2` | `cond1 AND cond2` |
| `cond1 or cond2` | `cond1 OR cond2` |
| `not cond` | `NOT cond` |
| `x.col in [a, b, c]` | `col IN (a, b, c)` |
| `starts_with(x.col, "pre")` | `col LIKE 'pre%'` |
| `ends_with(x.col, "suf")` | `col LIKE '%suf'` |
| `contains(x.col, "sub")` | `col LIKE '%sub%'` |
| `x.col == null` | `col IS NULL` |
| `x.col != null` | `col IS NOT NULL` |

### 3.3 Examples

```lambda
// Simple filter → SELECT * FROM book WHERE year >= 2000
for (b in db.data.book where b.year >= 2000) b

// Multi-condition → SELECT * FROM book WHERE genre = 'sci-fi' AND year >= 2000
for (b in db.data.book where b.genre == "sci-fi" and b.year >= 2000) b

// Sorted + paginated → SELECT * FROM book ORDER BY title ASC LIMIT 20 OFFSET 40
for (b in db.data.book order by b.title limit 20 offset 40) b

// Column projection (body selects fields) — still fetches all from SQL, but Lambda narrows
for (b in db.data.book where b.year >= 2000) {title: b.title, year: b.year}

// let binding — computed client-side after SQL fetch
for (b in db.data.book where b.year >= 2000, let age = 2026 - b.year)
    {title: b.title, age: age}
```

### 3.4 Query Boundary: SQL vs. Lambda

Not every expression in a `for` clause can be pushed to SQL. The compiler applies a **pushdown analysis**:

- **Pushable** (compiled to SQL): comparisons on table columns against literals/constants, `and`/`or`/`not`, `in` with literal lists, string prefix/suffix/contains, null checks, `order by` on columns, `limit`, `offset`
- **Not pushable** (evaluated in Lambda): `let` bindings, function calls on results, cross-table references, body expression (the mapping/transformation)

When a `where` clause mixes pushable and non-pushable conditions, the compiler **splits** the clause: pushable predicates become SQL WHERE, non-pushable predicates become a Lambda post-filter.

```lambda
// Mixed: b.year >= 2000 → SQL; len(b.title) > 10 → Lambda post-filter
for (b in db.data.book where b.year >= 2000 and len(b.title) > 10) b.title

// Compiles to:
//   SQL:    SELECT * FROM book WHERE year >= 2000
//   Lambda: filter by len(~.title) > 10, then map to ~.title
```

---

## 4. Connection & Lifecycle

### 4.1 The `input()` API for Databases

Databases are opened through Lambda's existing `input()` function — the same entry point used for JSON, CSV, YAML, and all other formats. The two-argument form is:

```lambda
input(target, type_or_options)
```

| Argument | Type | Description |
|----------|------|-------------|
| `target` | `string` | File path (for SQLite/DuckDB) or connection URI (for client-server databases). Supports `@` path literals (`@./relative`, `@/absolute`). |
| `type_or_options` | `string \| map` | Either a format string (`'sqlite'`, `'postgresql'`) or an options map (`{type: 'sqlite', ...}`). When omitted, auto-detected from file extension or URI scheme. |

Internally this routes to `fn_input2(target_item, type)` — the same C++ function that handles all `input()` calls. When `type` is a map, the runtime extracts the `type` field (matching the existing options-map pattern used by other input formats like `{type: 'markdown', flavor: 'commonmark'}`).

### 4.2 Opening a Database

**Three calling patterns** (all produce the same `<db>` element):

```lambda
// 1. Auto-detect — extension .db, .sqlite, .sqlite3 detected as SQLite
let db = input(@./library.db)
let db = input(@./data.sqlite3)

// 2. Explicit format string — required for non-standard extensions
let db = input(@./myfile.dat, 'sqlite')
let db = input(@./warehouse.ddb, 'duckdb')

// 3. Options map — full control over connection parameters
let db = input(@./library.db, {type: 'sqlite', cache_size: 5000})
```

**URI-based connections** (client-server databases):

```lambda
// PostgreSQL — URI scheme auto-detects the driver
let db = input('postgresql://user:pass@localhost:5432/mydb')
let db = input('postgres://user:pass@host/db', {type: 'postgresql', timeout: 5000})

// MySQL
let db = input('mysql://user:pass@localhost:3306/mydb')

// Explicit type overrides scheme detection
let db = input('some-custom-url', 'postgresql')
```

### 4.3 Connection Options

The options map supports common fields (all backends) plus backend-specific fields:

**Common options** (all backends):

```lambda
let db = input(@./library.db, {
    type: 'sqlite',           // driver name (required if auto-detect fails)
    readonly: true,           // default: true — enforced in phase 1 (read-only)
    cache: true,              // default: true — cache query results in memory
    cache_size: 1000,         // max cached result sets (default: 1000, LRU eviction)
    timeout: 5000,            // connection timeout in milliseconds (default: 5000)
    schema: 'public'          // schema/namespace to use (default: backend-specific)
})
```

**SQLite-specific options**:

```lambda
let db = input(@./library.db, {
    type: 'sqlite',
    journal_mode: 'wal',      // WAL mode for better concurrent read performance
    busy_timeout: 3000        // milliseconds to wait if database is locked
})
```

**PostgreSQL-specific options** (future):

```lambda
let db = input('postgresql://localhost/mydb', {
    type: 'postgresql',
    user: 'app_user',         // override URI user
    password: 'secret',       // override URI password (prefer URI or env var)
    ssl: true,                // require SSL connection
    schema: 'public',         // PostgreSQL schema (default: 'public')
    pool_size: 4              // connection pool size (default: 1)
})
```

**Options extraction in C++** — follows the existing `fn_input2` pattern:

```cpp
// in fn_input2: when type argument is a map...
if (get_type_id(type) == LMD_TYPE_MAP) {
    // extract 'type' field → driver name (e.g. "sqlite", "postgresql")
    Item input_type = map_get(type.map, "type");
    const char* type_str = input_type ? to_c_str(input_type) : NULL;

    // remaining fields passed through to rdb_open() as connection options
    // readonly, cache, timeout, etc. extracted by input-rdb.cpp
}
```

### 4.4 Driver Auto-detection

When no explicit `type` is provided, the runtime selects a driver by:

| Priority | Method | Example | Driver |
|----------|--------|---------|--------|
| 1 | URI scheme | `postgresql://...` | `"postgresql"` |
| 2 | URI scheme | `mysql://...` | `"mysql"` |
| 3 | URI scheme | `duckdb://...` | `"duckdb"` |
| 4 | File extension | `.db`, `.sqlite`, `.sqlite3` | `"sqlite"` |
| 5 | File extension | `.ddb`, `.duckdb` | `"duckdb"` |
| 6 | Explicit type | `input(path, 'sqlite')` | `"sqlite"` |
| 7 | Options map | `input(path, {type: 'sqlite'})` | `"sqlite"` |

If no driver is detected, `input()` returns an error: `"rdb: cannot detect driver for 'path'"`.

### 4.5 Lifecycle

```
   input(@./lib.db)                     input object GC'd / script exit
        │                                         │
        ▼                                         ▼
   ┌──────────┐    ┌──────────┐    ┌──────┐    ┌───────┐
   │ rdb_open │───►│ load     │───►│ lazy │───►│ close │
   │          │    │ schema   │    │ use  │    │       │
   └──────────┘    └──────────┘    └──────┘    └───────┘
   connection       schema          queries     rdb_close()
   established      introspected    on demand   frees handle
```

- **Open**: `rdb_open()` called once per `input()` invocation. Returns immediately after opening the handle and loading schema metadata. No row data is fetched.
- **Schema load**: `rdb_load_schema()` populates `conn->schema` — column types, indexes, foreign keys. This runs a small number of metadata queries (e.g., `PRAGMA table_info` for SQLite, `information_schema` for PostgreSQL).
- **Lazy use**: Table data is not touched until script code accesses `db.data.<table>`. Individual queries prepared/executed on demand.
- **Connection pooling**: If the same URI appears in multiple `input()` calls within one script, the runtime reuses the existing `RdbConn*` (keyed by normalized URI + driver name).
- **Cleanup**: `rdb_close()` called when the `Input` object is garbage-collected, or at script exit. Prepared statements are finalized before the connection handle is destroyed.
- **Read-only enforcement** (Phase 1): SQLite opened with `SQLITE_OPEN_READONLY`; PostgreSQL uses `SET default_transaction_read_only = ON`. Prevents accidental mutation.

### 4.6 Error Handling

Connection and query errors surface through Lambda's standard `T^E` error handling:

```lambda
let db = input(@./missing.db)          // error: file not found
let db = input(@./corrupt.db)          // error: not a valid SQLite database
let db = input('postgresql://bad:1234/x')  // error: connection refused

// Explicit error handling with ^ propagation
fn load_books() Book[]^ {
    let db = input(@./library.db)^     // propagate error if connection fails
    for (b in db.data.book) b
}

// Catch and recover
let result = input(@./data.db)
match result {
    db^   => for (b in db.data.book) b    // success path
    err^  => []                            // fallback on error
}
```

Error messages include the driver name for diagnostics: `"rdb sqlite: failed to open 'path': not a database"`.

---

## 5. Lazy Loading & Caching

### 5.1 Lazy Evaluation Strategy

Table data is **never eagerly loaded**. The runtime tracks what has been accessed:

| Access pattern | SQL generated | Rows materialized |
|----------------|--------------|-------------------|
| `db.data.book` (reference only) | None | 0 |
| `len(db.data.book)` | `SELECT COUNT(*) FROM book` | 0 |
| `db.data.book[0]` | `SELECT * FROM book LIMIT 1` | 1 |
| `db.data.book[5]` | `SELECT * FROM book LIMIT 1 OFFSET 5` | 1 |
| `for (b in db.data.book) ...` | `SELECT * FROM book` | streamed |
| `for (b in db.data.book where b.year > 2000 limit 10) ...` | `SELECT * FROM book WHERE year > 2000 LIMIT 10` | ≤10 |

### 5.2 Streaming Iteration

For-clause iteration over tables uses a **cursor-based approach** via the generic `rdb_*` API — rows are fetched and converted to Lambda maps one at a time. This avoids materializing the entire table in memory:

```
for (b in db.data.book where ...) <transform b>

→ rdb_prepare(conn, "SELECT * FROM book WHERE ...")
→ rdb_bind_*(stmt, ...)               // bind parameter values
→ while rdb_step(stmt) == RDB_ROW:
    → rdb_column_value(stmt, i) for each column
    → rdb_value_to_item() → Lambda map (arena-allocated via MarkBuilder)
    → evaluate body expression
    → append result to output array
→ rdb_finalize(stmt)
```

### 5.3 Result Caching

Query results are cached by **normalized SQL string** as key:

- First execution: run SQL, cache result array
- Subsequent identical queries: return cached array (zero-copy, same arena Items)
- Cache eviction: LRU with configurable max entries
- Cache invalidation: not needed in phase 1 (read-only); future write support would clear cache on mutation

```lambda
// These two calls hit the cache on the second invocation:
let sci_fi = for (b in db.data.book where b.genre == "sci-fi") b
let more   = for (b in db.data.book where b.genre == "sci-fi") b    // cache hit
```

---

## 6. Implementation Architecture

### 6.1 Design: Generic RDB Layer + Backend Drivers

The implementation separates a **database-agnostic C+ API** (`lib/rdb.h`) from **backend-specific drivers** (SQLite first, PostgreSQL/MySQL/DuckDB later). All Lambda runtime code — the input plugin, the for-clause SQL compiler, the lazy-loading machinery — talks exclusively to the generic API. Backend-specific code is encapsulated behind a **driver vtable**.

```
┌─────────────────────────────────────────────────────────────┐
│  Lambda Runtime (input plugin, for-clause compiler, eval)   │
│  Talks only to lib/rdb.h API                                │
├─────────────────────────────────────────────────────────────┤
│  lib/rdb.h          — generic RDB C+ API (structs, vtable)  │
│  lib/rdb.c          — shared logic (param binding, caching)  │
├───────────────┬───────────────┬──────────────┬──────────────┤
│ lib/rdb_sqlite.c  │ lib/rdb_pg.c  │ lib/rdb_mysql.c │ ...      │
│ (SQLite driver)   │ (future)      │ (future)        │          │
├───────────────┴───────────────┴──────────────┴──────────────┤
│  lib/sqlite3.c    libpq           libmysqlclient    ...     │
│  (vendored)       (system)        (system)                  │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Component Overview

```
lib/rdb.h                         — generic RDB API (structs, driver vtable, public functions)
lib/rdb.c                         — shared implementation (param binding, cache, row conversion)
lib/rdb_sqlite.h                  — SQLite driver header
lib/rdb_sqlite.c                  — SQLite driver implementation
lib/sqlite3.c + lib/sqlite3.h     — SQLite amalgamation (vendored, ~250KB)
lambda/input/input-rdb.cpp        — Lambda input plugin (database → Lambda element tree)
lambda/input/input-rdb.h          — public C API for MIR JIT
lambda/input/rdb_query.cpp        — for-clause → SQL compiler (database-agnostic)
lambda/input/rdb_query.h          — query builder API
test/test_rdb.cpp                 — GTest unit tests (generic API)
test/test_rdb_sqlite.cpp          — GTest unit tests (SQLite-specific)
test/lambda/io_rdb_*.ls           — Integration test scripts
```

### 6.3 Generic RDB C+ API (`lib/rdb.h`)

#### 6.3.1 Column Type Enum

```c
// database-agnostic column type classification
typedef enum {
    RDB_TYPE_INT,           // integer
    RDB_TYPE_FLOAT,         // floating point
    RDB_TYPE_DECIMAL,       // decimal / numeric
    RDB_TYPE_STRING,        // text / varchar
    RDB_TYPE_DATETIME,      // date, datetime, timestamp
    RDB_TYPE_JSON,          // json text (auto-parsed)
    RDB_TYPE_BOOL,          // boolean
    RDB_TYPE_BLOB,          // binary (deferred)
    RDB_TYPE_NULL,          // null
    RDB_TYPE_UNKNOWN        // unmapped type
} RdbType;
```

#### 6.3.2 Schema Structs

```c
// column metadata
typedef struct {
    const char* name;       // column name (owned by pool)
    const char* type_decl;  // raw declared type string, e.g. "VARCHAR(255)"
    RdbType     type;       // normalized type enum
    bool        nullable;   // allows NULL?
    bool        primary_key;// part of primary key?
    int         pk_index;   // position in composite PK (0 if not PK)
} RdbColumn;

// index metadata
typedef struct {
    const char* name;       // index name
    bool        unique;     // is unique index?
    int         column_count;
    const char** columns;   // column names (array, owned by pool)
} RdbIndex;

// foreign key metadata
typedef struct {
    const char* column;     // FK column in this table (e.g. "author_id")
    const char* ref_table;  // referenced table name (e.g. "author")
    const char* ref_column; // referenced column name (e.g. "id")
    const char* link_name;  // derived navigation name (e.g. "author" — stripped _id suffix)
} RdbForeignKey;

// table/view metadata
typedef struct {
    const char* name;       // table or view name
    bool        is_view;    // true for views, false for tables
    int         column_count;
    RdbColumn*  columns;    // array of columns (owned by pool)
    int         index_count;
    RdbIndex*   indexes;    // array of indexes (owned by pool)
    int         fk_count;
    RdbForeignKey* foreign_keys;  // outgoing FKs (owned by pool)
    int         reverse_fk_count;
    RdbForeignKey* reverse_fks;   // incoming FKs from other tables (owned by pool)
} RdbTable;

// database schema
typedef struct {
    int         table_count;
    RdbTable*   tables;     // array of table/view descriptors (owned by pool)
} RdbSchema;
```

#### 6.3.3 Query Parameter Binding

```c
// single bound parameter value
typedef struct {
    RdbType type;
    union {
        int64_t     int_val;
        double      float_val;
        const char* str_val;    // not owned — must outlive the query
        bool        bool_val;
    };
} RdbParam;

// query with bound parameters
typedef struct {
    const char* sql;            // parameterized SQL string
    int         param_count;
    RdbParam*   params;         // array of bound values
} RdbQuery;
```

#### 6.3.4 Result Row Iteration

```c
// opaque statement handle (wraps sqlite3_stmt*, PGresult*, etc.)
typedef struct RdbStmt RdbStmt;

// row value accessor — reads current row's column by index
typedef struct {
    RdbType type;               // actual runtime type of this cell
    bool    is_null;
    union {
        int64_t     int_val;
        double      float_val;
        const char* str_val;    // valid until next rdb_stmt_step() or rdb_stmt_finalize()
        int         str_len;
        bool        bool_val;
    };
} RdbValue;
```

#### 6.3.5 Driver Vtable

```c
// forward declarations
typedef struct RdbConn RdbConn;
typedef struct RdbDriver RdbDriver;

// driver operations — each backend implements this vtable
typedef struct RdbDriver {
    const char* name;                           // "sqlite", "postgresql", "mysql", ...

    // connection
    int     (*open)(RdbConn* conn, const char* uri, bool readonly);
    void    (*close)(RdbConn* conn);

    // schema introspection
    int     (*load_schema)(RdbConn* conn, RdbSchema* out_schema);

    // query execution
    int     (*prepare)(RdbConn* conn, const char* sql, RdbStmt** out_stmt);
    int     (*bind_param)(RdbStmt* stmt, int index, const RdbParam* param);
    int     (*step)(RdbStmt* stmt);             // returns: RDB_ROW, RDB_DONE, RDB_ERROR
    int     (*column_count)(RdbStmt* stmt);
    RdbValue(*column_value)(RdbStmt* stmt, int col_index);
    void    (*finalize)(RdbStmt* stmt);

    // row count (optional — may not be efficient for all backends)
    int64_t (*row_count)(RdbConn* conn, const char* table_name);

    // error info
    const char* (*error_msg)(RdbConn* conn);
} RdbDriver;

// step() return codes
#define RDB_ROW   100
#define RDB_DONE  101
#define RDB_ERROR (-1)
#define RDB_OK    0
```

#### 6.3.6 Connection Handle

```c
// database connection (database-agnostic)
typedef struct RdbConn {
    const RdbDriver* driver;    // vtable for this backend
    void*           handle;     // backend-specific handle (sqlite3*, PGconn*, etc.)
    Pool*           pool;       // memory pool for schema/metadata allocations
    RdbSchema       schema;     // introspected schema (populated by load_schema)
    bool            readonly;
    const char*     uri;        // connection URI / file path (pool-owned copy)
} RdbConn;
```

#### 6.3.7 Public API Functions

```c
//--- connection lifecycle ---

// open a database connection; driver selected by URI scheme or explicit type
// uri: file path for SQLite, "postgresql://..." for PG, etc.
// type: explicit driver name (nullable — auto-detect from URI if NULL)
RdbConn*    rdb_open(Pool* pool, const char* uri, const char* type, bool readonly);
void        rdb_close(RdbConn* conn);

//--- schema access ---

// load/refresh schema metadata into conn->schema
int         rdb_load_schema(RdbConn* conn);

// lookup table by name (returns NULL if not found)
RdbTable*   rdb_get_table(RdbConn* conn, const char* table_name);

// lookup column by name within a table (returns NULL if not found)
RdbColumn*  rdb_get_column(RdbTable* table, const char* column_name);

//--- query execution ---

// prepare a parameterized query
RdbStmt*    rdb_prepare(RdbConn* conn, const char* sql);

// bind parameter at 1-based index
int         rdb_bind_int(RdbStmt* stmt, int index, int64_t value);
int         rdb_bind_float(RdbStmt* stmt, int index, double value);
int         rdb_bind_string(RdbStmt* stmt, int index, const char* value);
int         rdb_bind_null(RdbStmt* stmt, int index);
int         rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param);

// step to next row; returns RDB_ROW, RDB_DONE, or RDB_ERROR
int         rdb_step(RdbStmt* stmt);

// read column value from current row (0-based column index)
RdbValue    rdb_column_value(RdbStmt* stmt, int col_index);
int         rdb_column_count(RdbStmt* stmt);

// finalize (free) a prepared statement
void        rdb_finalize(RdbStmt* stmt);

//--- convenience ---

// get row count for a table (SELECT COUNT(*))
int64_t     rdb_row_count(RdbConn* conn, const char* table_name);

// get human-readable error message from last operation
const char* rdb_error_msg(RdbConn* conn);

//--- driver registration ---

// register a driver (called at startup / init)
void        rdb_register_driver(const char* name, const RdbDriver* driver);

// lookup driver by name
const RdbDriver* rdb_get_driver(const char* name);
```

### 6.4 SQLite Driver Implementation (`lib/rdb_sqlite.c`)

The SQLite driver implements the `RdbDriver` vtable:

```c
#include "rdb.h"
#include "sqlite3.h"

//--- connection ---

static int sqlite_open(RdbConn* conn, const char* uri, bool readonly) {
    int flags = readonly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(uri, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: failed to open '%s': %s", uri, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return RDB_ERROR;
    }
    conn->handle = db;
    return RDB_OK;
}

static void sqlite_close(RdbConn* conn) {
    if (conn->handle) {
        sqlite3_close((sqlite3*)conn->handle);
        conn->handle = NULL;
    }
}

//--- schema introspection ---

static RdbType sqlite_map_type(const char* type_decl) {
    // normalize declared type → RdbType
    if (!type_decl || type_decl[0] == '\0')         return RDB_TYPE_STRING;
    if (str_ieq_const(type_decl, "INTEGER")
        || str_ieq_const(type_decl, "INT")
        || str_ieq_const(type_decl, "BIGINT")
        || str_ieq_const(type_decl, "SMALLINT")
        || str_ieq_const(type_decl, "TINYINT"))     return RDB_TYPE_INT;
    if (str_ieq_const(type_decl, "BOOLEAN"))         return RDB_TYPE_BOOL;
    if (str_ieq_const(type_decl, "REAL")
        || str_ieq_const(type_decl, "DOUBLE")
        || str_ieq_const(type_decl, "FLOAT"))        return RDB_TYPE_FLOAT;
    if (str_ieq_const(type_decl, "DECIMAL")
        || str_ieq_const(type_decl, "NUMERIC"))      return RDB_TYPE_DECIMAL;
    if (str_ieq_const(type_decl, "DATE")
        || str_ieq_const(type_decl, "DATETIME")
        || str_ieq_const(type_decl, "TIMESTAMP"))    return RDB_TYPE_DATETIME;
    if (str_ieq_const(type_decl, "JSON"))            return RDB_TYPE_JSON;
    if (str_ieq_const(type_decl, "BLOB"))            return RDB_TYPE_BLOB;
    return RDB_TYPE_STRING;  // default: TEXT affinity
}

static int sqlite_load_schema(RdbConn* conn, RdbSchema* out_schema) {
    sqlite3* db = (sqlite3*)conn->handle;
    Pool* pool = conn->pool;

    // 1. enumerate tables and views
    //    SELECT name, type FROM sqlite_master
    //        WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%'
    //        ORDER BY name

    // 2. for each table/view:
    //    PRAGMA table_info(table_name) → cid, name, type, notnull, dflt_value, pk
    //    PRAGMA index_list(table_name) → seq, name, unique, origin, partial
    //    for each index: PRAGMA index_info(index_name) → seqno, cid, name

    // 3. allocate RdbTable array from pool, populate columns/indexes
    // (detailed implementation omitted for brevity — follows standard SQLite PRAGMA pattern)

    return RDB_OK;
}

//--- query execution ---

typedef struct {
    RdbStmt     base;           // embedded base (so RdbStmt* can point here)
    sqlite3_stmt* stmt;         // SQLite prepared statement
    RdbConn*    conn;           // back-pointer to connection
} SqliteStmt;

static int sqlite_prepare(RdbConn* conn, const char* sql, RdbStmt** out_stmt) {
    sqlite3* db = (sqlite3*)conn->handle;
    sqlite3_stmt* raw = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &raw, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: prepare failed: %s", sqlite3_errmsg(db));
        return RDB_ERROR;
    }
    SqliteStmt* s = (SqliteStmt*)pool_calloc(conn->pool, sizeof(SqliteStmt));
    s->stmt = raw;
    s->conn = conn;
    *out_stmt = (RdbStmt*)s;
    return RDB_OK;
}

static int sqlite_bind_param(RdbStmt* stmt, int index, const RdbParam* param) {
    sqlite3_stmt* raw = ((SqliteStmt*)stmt)->stmt;
    switch (param->type) {
        case RDB_TYPE_INT:    return sqlite3_bind_int64(raw, index, param->int_val) == SQLITE_OK ? RDB_OK : RDB_ERROR;
        case RDB_TYPE_FLOAT:  return sqlite3_bind_double(raw, index, param->float_val) == SQLITE_OK ? RDB_OK : RDB_ERROR;
        case RDB_TYPE_STRING:
        case RDB_TYPE_DATETIME:
        case RDB_TYPE_JSON:   return sqlite3_bind_text(raw, index, param->str_val, -1, SQLITE_TRANSIENT) == SQLITE_OK ? RDB_OK : RDB_ERROR;
        case RDB_TYPE_BOOL:   return sqlite3_bind_int(raw, index, param->bool_val ? 1 : 0) == SQLITE_OK ? RDB_OK : RDB_ERROR;
        case RDB_TYPE_NULL:   return sqlite3_bind_null(raw, index) == SQLITE_OK ? RDB_OK : RDB_ERROR;
        default:              return RDB_ERROR;
    }
}

static int sqlite_step(RdbStmt* stmt) {
    int rc = sqlite3_step(((SqliteStmt*)stmt)->stmt);
    if (rc == SQLITE_ROW)  return RDB_ROW;
    if (rc == SQLITE_DONE) return RDB_DONE;
    return RDB_ERROR;
}

static RdbValue sqlite_column_value(RdbStmt* stmt, int col_index) {
    sqlite3_stmt* raw = ((SqliteStmt*)stmt)->stmt;
    RdbValue val = {0};

    int col_type = sqlite3_column_type(raw, col_index);
    switch (col_type) {
        case SQLITE_INTEGER:
            val.type = RDB_TYPE_INT;
            val.int_val = sqlite3_column_int64(raw, col_index);
            break;
        case SQLITE_FLOAT:
            val.type = RDB_TYPE_FLOAT;
            val.float_val = sqlite3_column_double(raw, col_index);
            break;
        case SQLITE_TEXT:
            val.type = RDB_TYPE_STRING;
            val.str_val = (const char*)sqlite3_column_text(raw, col_index);
            val.str_len = sqlite3_column_bytes(raw, col_index);
            break;
        case SQLITE_NULL:
            val.type = RDB_TYPE_NULL;
            val.is_null = true;
            break;
        default:
            val.type = RDB_TYPE_UNKNOWN;
            break;
    }
    return val;
}

static int sqlite_column_count(RdbStmt* stmt) {
    return sqlite3_column_count(((SqliteStmt*)stmt)->stmt);
}

static void sqlite_finalize(RdbStmt* stmt) {
    sqlite3_finalize(((SqliteStmt*)stmt)->stmt);
    // SqliteStmt itself is pool-allocated — freed when pool is destroyed
}

static int64_t sqlite_row_count(RdbConn* conn, const char* table_name) {
    // validate table_name exists in schema (prevent injection)
    if (!rdb_get_table(conn, table_name)) return -1;

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "SELECT COUNT(*) FROM ");
    strbuf_append_str(sb, table_name);      // safe: validated against schema
    // ... prepare, step, read int64, finalize ...
    strbuf_free(sb);
    return count;
}

static const char* sqlite_error_msg(RdbConn* conn) {
    return sqlite3_errmsg((sqlite3*)conn->handle);
}

//--- driver registration ---

static const RdbDriver sqlite_driver = {
    .name         = "sqlite",
    .open         = sqlite_open,
    .close        = sqlite_close,
    .load_schema  = sqlite_load_schema,
    .prepare      = sqlite_prepare,
    .bind_param   = sqlite_bind_param,
    .step         = sqlite_step,
    .column_count = sqlite_column_count,
    .column_value = sqlite_column_value,
    .finalize     = sqlite_finalize,
    .row_count    = sqlite_row_count,
    .error_msg    = sqlite_error_msg,
};

void rdb_sqlite_register(void) {
    rdb_register_driver("sqlite", &sqlite_driver);
}
```

### 6.5 Shared RDB Logic (`lib/rdb.c`)

The generic layer handles:

```c
#include "rdb.h"

//--- driver registry (static array, max 8 drivers) ---

#define RDB_MAX_DRIVERS 8
static struct { const char* name; const RdbDriver* driver; } rdb_drivers[RDB_MAX_DRIVERS];
static int rdb_driver_count = 0;

void rdb_register_driver(const char* name, const RdbDriver* driver) {
    if (rdb_driver_count >= RDB_MAX_DRIVERS) {
        log_error("rdb: max drivers exceeded");
        return;
    }
    rdb_drivers[rdb_driver_count].name = name;
    rdb_drivers[rdb_driver_count].driver = driver;
    rdb_driver_count++;
}

const RdbDriver* rdb_get_driver(const char* name) {
    for (int i = 0; i < rdb_driver_count; i++) {
        if (str_eq_const(rdb_drivers[i].name, name)) return rdb_drivers[i].driver;
    }
    return NULL;
}

//--- auto-detect driver from URI ---

static const char* rdb_detect_driver(const char* uri) {
    // scheme-based detection
    if (str_starts_with(uri, "postgresql://") || str_starts_with(uri, "postgres://"))
        return "postgresql";
    if (str_starts_with(uri, "mysql://"))    return "mysql";
    if (str_starts_with(uri, "duckdb://"))   return "duckdb";

    // extension-based detection (file paths)
    if (str_ends_with(uri, ".db") || str_ends_with(uri, ".sqlite") || str_ends_with(uri, ".sqlite3"))
        return "sqlite";

    return NULL;
}

//--- connection lifecycle ---

RdbConn* rdb_open(Pool* pool, const char* uri, const char* type, bool readonly) {
    const char* driver_name = type ? type : rdb_detect_driver(uri);
    if (!driver_name) {
        log_error("rdb: cannot detect driver for '%s'", uri);
        return NULL;
    }
    const RdbDriver* driver = rdb_get_driver(driver_name);
    if (!driver) {
        log_error("rdb: driver '%s' not registered", driver_name);
        return NULL;
    }

    RdbConn* conn = (RdbConn*)pool_calloc(pool, sizeof(RdbConn));
    conn->driver = driver;
    conn->pool = pool;
    conn->readonly = readonly;
    // copy URI into pool
    size_t uri_len = strlen(uri);
    char* uri_copy = (char*)pool_alloc(pool, uri_len + 1);
    memcpy(uri_copy, uri, uri_len + 1);
    conn->uri = uri_copy;

    if (driver->open(conn, uri, readonly) != RDB_OK) {
        log_error("rdb: failed to open connection to '%s'", uri);
        return NULL;
    }
    return conn;
}

void rdb_close(RdbConn* conn) {
    if (conn && conn->driver) {
        conn->driver->close(conn);
    }
}

//--- schema access ---

int rdb_load_schema(RdbConn* conn) {
    return conn->driver->load_schema(conn, &conn->schema);
}

RdbTable* rdb_get_table(RdbConn* conn, const char* table_name) {
    for (int i = 0; i < conn->schema.table_count; i++) {
        if (str_eq_const(conn->schema.tables[i].name, table_name))
            return &conn->schema.tables[i];
    }
    return NULL;
}

RdbColumn* rdb_get_column(RdbTable* table, const char* column_name) {
    for (int i = 0; i < table->column_count; i++) {
        if (str_eq_const(table->columns[i].name, column_name))
            return &table->columns[i];
    }
    return NULL;
}

//--- query convenience wrappers ---

RdbStmt* rdb_prepare(RdbConn* conn, const char* sql) {
    RdbStmt* stmt = NULL;
    if (conn->driver->prepare(conn, sql, &stmt) != RDB_OK) return NULL;
    return stmt;
}

int  rdb_bind_int(RdbStmt* s, int i, int64_t v)     { RdbParam p = {.type=RDB_TYPE_INT,    .int_val=v};   return s ? rdb_bind_param(s, i, &p) : RDB_ERROR; }
int  rdb_bind_float(RdbStmt* s, int i, double v)     { RdbParam p = {.type=RDB_TYPE_FLOAT,  .float_val=v}; return s ? rdb_bind_param(s, i, &p) : RDB_ERROR; }
int  rdb_bind_string(RdbStmt* s, int i, const char* v) { RdbParam p = {.type=RDB_TYPE_STRING, .str_val=v};  return s ? rdb_bind_param(s, i, &p) : RDB_ERROR; }
int  rdb_bind_null(RdbStmt* s, int i)                { RdbParam p = {.type=RDB_TYPE_NULL};                  return s ? rdb_bind_param(s, i, &p) : RDB_ERROR; }

int      rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param) {
    // retrieve conn from stmt (backend stores back-pointer)
    // delegate to driver->bind_param
    return RDB_OK; // actual impl delegates to driver
}

int      rdb_step(RdbStmt* stmt)                  { return stmt ? ((RdbConn*)NULL)->driver->step(stmt) : RDB_ERROR; }      // actual: stored conn->driver
RdbValue rdb_column_value(RdbStmt* stmt, int col)  { /* delegate to driver->column_value */ return (RdbValue){0}; }
int      rdb_column_count(RdbStmt* stmt)           { /* delegate to driver->column_count */ return 0; }
void     rdb_finalize(RdbStmt* stmt)               { /* delegate to driver->finalize */ }

int64_t  rdb_row_count(RdbConn* conn, const char* table_name) {
    return conn->driver->row_count(conn, table_name);
}

const char* rdb_error_msg(RdbConn* conn) {
    return conn->driver->error_msg(conn);
}
```

### 6.6 Lambda Input Plugin (`lambda/input/input-rdb.cpp`)

The Lambda input plugin bridges `lib/rdb.h` → Lambda data structures. It is **entirely generic** — never references SQLite directly:

```cpp
#include "input.hpp"
#include "../mark_builder.hpp"
#include "lib/rdb.h"

// convert an RdbValue to a Lambda Item, respecting column type metadata
static Item rdb_value_to_item(MarkBuilder& builder, RdbValue val, RdbType declared_type) {
    if (val.is_null) return ItemNull;
    switch (declared_type) {
        case RDB_TYPE_INT:      return builder.createInt(val.int_val);
        case RDB_TYPE_FLOAT:    return builder.createFloat(val.float_val);
        case RDB_TYPE_BOOL:     return builder.createBool(val.int_val != 0);
        case RDB_TYPE_STRING:   return builder.createStringItem(val.str_val);
        case RDB_TYPE_DATETIME: return parse_datetime_string(builder, val.str_val);   // → Lambda datetime
        case RDB_TYPE_JSON:     return parse_json_to_item(builder.input(), val.str_val); // reuse JSON parser
        case RDB_TYPE_DECIMAL:  return parse_decimal_string(builder, val.str_val);
        default:                return builder.createStringItem(val.str_val);
    }
}

void input_rdb_parse(Input* input, const char* uri, const char* type) {
    // open connection via generic API
    RdbConn* conn = rdb_open(input->pool, uri, type, /*readonly=*/true);
    if (!conn) {
        log_error("rdb: failed to open database: %s", uri);
        input->root = ItemError;
        return;
    }

    rdb_load_schema(conn);
    MarkBuilder builder(input);

    // build schema map
    auto schema_map = builder.map();
    for (int t = 0; t < conn->schema.table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        auto tbl_schema = builder.map();
        if (tbl->is_view) tbl_schema.put("view", ItemTrue);
        // columns array
        auto cols = builder.array();
        for (int c = 0; c < tbl->column_count; c++) {
            RdbColumn* col = &tbl->columns[c];
            cols.append(builder.map()
                .put("name", builder.createStringItem(col->name))
                .put("type", rdb_type_to_item(builder, col->type))
                .put("nullable", builder.createBool(col->nullable))
                .put("pk", builder.createBool(col->primary_key))
                .build());
        }
        tbl_schema.put("columns", cols.build());
        // indexes array (similar pattern)
        schema_map.put(tbl->name, tbl_schema.build());
    }

    // build data map — lazy table proxies
    auto data_map = builder.map();
    for (int t = 0; t < conn->schema.table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        auto table_el = builder.element(tbl->name);
        table_el.put("name", builder.createStringItem(tbl->name));
        // attach lazy proxy marker + RdbConn* + RdbTable* as internal metadata
        // (implementation detail: stored in element's extension slot)
        data_map.put(tbl->name, table_el.build());
    }

    // build top-level element
    const char* filename = file_path_basename(uri);
    auto db_el = builder.element("db");
    db_el.put("name", builder.createStringItem(filename));
    db_el.put("schema", schema_map.build());
    db_el.put("data", data_map.build());

    input->root = db_el.build();
    // stash RdbConn* in input's extension data for query execution
}
```

### 6.7 For-Clause SQL Compilation (`lambda/input/rdb_query.cpp`)

The query compiler is **database-agnostic** — it generates standard SQL and uses `lib/rdb.h` to execute:

```
AST for-expression
  ├── binding: b
  ├── source: db.data.book     ← detect: is this an RDB table proxy?
  ├── where: b.year >= 2000    ← analyze: pushable to SQL?
  ├── order_by: b.title asc    ← pushable
  ├── limit: 10                ← pushable
  └── body: {title: b.title}   ← Lambda post-processing
```

**Detection heuristic**: The source expression resolves to an element carrying the RDB table proxy marker (set by `input-rdb.cpp`).

**Compilation flow** (all via generic `rdb_*` calls):

1. **Analyze** `where` clause — classify each sub-expression as SQL-pushable or not
2. **Generate** parameterized SQL: `SELECT * FROM book WHERE year >= ?1 ORDER BY title ASC LIMIT ?2`
3. **Prepare** via `rdb_prepare(conn, sql)`
4. **Bind** values via `rdb_bind_int()`, `rdb_bind_string()`, etc. (prevents injection)
5. **Iterate** via `rdb_step(stmt)` — returns `RDB_ROW` per row
6. **Convert** each row via `rdb_column_value()` → `rdb_value_to_item()` → Lambda map
7. **Post-filter** any non-pushable conditions in Lambda
8. **Finalize** via `rdb_finalize(stmt)`

### 6.8 SQL Injection Prevention

All user-supplied values are bound as **SQL parameters** via `rdb_bind_*()` — never interpolated into the SQL string. Table and column names are validated against `conn->schema` (populated by `rdb_load_schema()`), not taken from user strings.

### 6.9 Input Plugin Registration

```cpp
// in input.cpp — add to format dispatcher
if (str_eq(type, "sqlite") || str_eq(type, "postgresql") || str_eq(type, "mysql")
    || rdb_detect_format(url)) {
    input_rdb_parse(input, path, type);
    return;
}
```

Extension detection delegates to `rdb_detect_driver()` in `lib/rdb.c`.

---

## 7. Ideas from Other Data Access Tools

This section surveys novel patterns from existing ORM/data-access tools and identifies which ones fit Lambda's functional, declarative model.

### 7.1 Survey

| Tool | Key Idea | How It Works |
|------|----------|-------------|
| **PostgREST** | FK auto-navigation | Introspects `pg_constraint` for foreign keys; allows nested resource embedding via FK name — `GET /books?select=title,author(name)` auto-joins. Relationships discovered from schema, not manually declared. |
| **PostgREST** | Operator-based filtering | Filter operators in query params: `?year=gte.2000&genre=eq.sci-fi` → `WHERE year >= 2000 AND genre = 'sci-fi'`. Already covered by Lambda's `for`/`where`. |
| **PostgREST** | Column selection | `?select=title,year` → `SELECT title, year`. Equivalent to Lambda's body expression projection. |
| **PostgREST** | JSON traversal | `?select=metadata->tags` → `json_extract_path()`. Pushes JSON access into SQL. |
| **Prisma** | Type narrowing per query | Return type narrows based on selected fields — if you select `{title, year}`, the result type is `{title: string, year: int}`, not the full `Book` type. |
| **EdgeDB** | Links (not FK columns) | Relationships are first-class "links" between types: `author: Author` instead of raw `author_id: int`. Navigation is `book.author.name`, not a manual join on IDs. |
| **LINQ (C#)** | Language-level query AST | Query expressions are part of the language syntax, translated to SQL at execution time. Lambda's `for` clause already does this. |
| **Drizzle** | Opt-in eager loading | `db.query.users.findMany({ with: { posts: true } })` — explicitly opt into loading related data. Avoids N+1 by batching. |
| **Hasura/GraphQL** | Nested aggregation | `books_aggregate { count, avg { price } }` — aggregate operations pushed to SQL as `SELECT COUNT(*), AVG(price)`. |
| **SQLAlchemy** | Hybrid properties | Computed fields that evaluate in Python OR push to SQL depending on query context. |
| **jOOQ** | Type-safe SQL builder | Schema-generated types guarantee column name/type safety at compile time. Lambda's `ShapePool`-based typed rows achieve similar safety. |

### 7.2 Ideas Adopted

Three ideas are incorporated into this proposal:

#### Idea 1: FK Relationship Auto-Navigation (from PostgREST / EdgeDB)

**The most impactful idea.** Foreign key constraints are introspected at schema load time and exposed as **navigable links** on row maps. Instead of manually joining on IDs, you traverse the FK name:

```lambda
// WITHOUT FK navigation — manual nested query
for (b in db.data.book where b.genre == "sci-fi")
    let a = for (x in db.data.author where x.id == b.author_id) x
    {title: b.title, author: a[0].name}

// WITH FK navigation — auto-resolve via FK name
for (b in db.data.book where b.genre == "sci-fi")
    {title: b.title, author: b.author.name}
//                           ^^^^^^^^^ follows book.author_id → author.id
```

The runtime detects that `author_id` references `author.id` (from `PRAGMA foreign_key_list` or `pg_constraint`), and when `b.author` is accessed:

1. Recognizes `author` matches the FK target table name
2. Generates: `SELECT * FROM author WHERE id = ?` with `b.author_id` bound
3. Returns the related row (single object for many-to-one, array for one-to-many)
4. Caches the result — subsequent accesses to `b.author` for the same `author_id` return cached row

**Reverse navigation** (one-to-many) also works:
```lambda
// From author → their books (reverse FK)
for (a in db.data.author)
    {name: a.name, books: a.book | ~.title}
//                        ^^^^^^ auto: SELECT * FROM book WHERE author_id = a.id
```

**Schema representation** — FKs appear in `db.schema`:
```lambda
db.schema.book.foreign_keys
// [{column: 'author_id', ref_table: 'author', ref_column: 'id'}]

db.schema.author.reverse_fks
// [{from_table: 'book', from_column: 'author_id', column: 'id'}]
```

**SQL optimization**: When FK navigation occurs inside a `for` clause, the query compiler can batch it into a JOIN:
```lambda
// This:
for (b in db.data.book) {title: b.title, author: b.author.name}
// Compiles to:
//   SELECT b.*, a.name FROM book b JOIN author a ON b.author_id = a.id
// instead of N+1 individual lookups
```

See §2.5 for data model details and §6.3.2 for the `RdbForeignKey` struct.

#### Idea 2: Aggregate Pushdown (from Hasura / GraphQL)

When pipe expressions apply aggregate functions (`count`, `sum`, `avg`, `min`, `max`) to a database table, the runtime pushes them to SQL instead of fetching all rows:

```lambda
db.data.book | count                          // → SELECT COUNT(*) FROM book
db.data.book | ~.price | sum                  // → SELECT SUM(price) FROM book
db.data.book | ~.price | avg                  // → SELECT AVG(price) FROM book
db.data.book | ~.year  | min                  // → SELECT MIN(year) FROM book

// with filter
for (b in db.data.book where b.genre == "sci-fi") b | ~.price | sum
// → SELECT SUM(price) FROM book WHERE genre = 'sci-fi'
```

This reuses the same pushdown analysis from §3.4 — if the entire expression is SQL-reducible, emit a single aggregate query.

#### Idea 3: Query-Driven Type Narrowing (from Prisma)

When a `for` body selects specific fields, the **result type narrows** to only those fields:

```lambda
// Full row → type is Book
for (b in db.data.book) b
// type: Book[] = {id: int, title: string, author_id: int, year: int, ...}[]

// Projected → type narrows to selected fields
for (b in db.data.book) {title: b.title, year: b.year}
// type: {title: string, year: int}[]

// With FK navigation → includes resolved type
for (b in db.data.book) {title: b.title, author: b.author.name}
// type: {title: string, author: string}[]
```

This enables Lambda's type checker to catch field-access errors on query results at analysis time (or in editor tooling), and enables future column projection pushdown (`SELECT title, year` instead of `SELECT *`).

### 7.3 Ideas Deferred

| Idea | Reason |
|------|--------|
| **Hybrid properties** (SQLAlchemy) | Lambda's `let` bindings already serve this role — computed client-side; future SQL pushdown is a compiler optimization, not a language feature |
| **Opt-in eager loading** (Drizzle) | FK auto-navigation with JOIN batching subsumes this; no need for explicit `with:` syntax |
| **JSON traversal pushdown** (PostgREST) | Already planned for Phase 2 (`json_extract()` pushdown); doesn't need special syntax |

---

## 8. Interaction with Pipes and Transformations

Database query results are standard Lambda arrays — all existing pipe operators and collection functions work:

```lambda
let books = for (b in db.data.book where b.year >= 2000) b

// pipe transformations
books | ~.title                     // extract titles
books | ~.year | sum / len(books)   // average year (Lambda arithmetic)

// aggregate pushdown — entire pipe compiles to SQL (see §7.2, Idea 2)
db.data.book | count                         // → SELECT COUNT(*) FROM book
db.data.book | ~.price | sum                 // → SELECT SUM(price) FROM book
db.data.book | ~.year  | min                 // → SELECT MIN(year) FROM book

// FK auto-navigation (see §2.5, §7.2 Idea 1)
for (b in db.data.book where b.genre == "sci-fi")
    {title: b.title, author: b.author.name}
//  → SELECT b.*, a.name FROM book b JOIN author a ON b.author_id = a.id WHERE b.genre = 'sci-fi'

// reverse FK: author → their books
for (a in db.data.author)
    {name: a.name, titles: a.book | ~.title}

// nested for + transformation (manual join — still works)
for (b in db.data.book where b.genre == "sci-fi")
    let a = for (x in db.data.author where x.id == b.author_id) x
    <card title: b.title, author: a[0].name>

// spread into new structures
<library
    for (b in db.data.book order by b.title)
        <book title: b.title, year: b.year>
>

// JSON column access — metadata was auto-parsed from JSON text
for (b in db.data.book where b.year >= 2000)
    {title: b.title, tags: b.metadata.tags, rating: b.metadata.rating}

// group by (Lambda-side, post-fetch)
let by_genre = for (b in db.data.book, let g = b.genre)
    group(b, g)
```

---

## 9. Phase Plan

### Phase 1: Core Read-Only Access (This Proposal)

Status reflects the current repository state, not the intended end-state of the proposal.

| Feature | Priority | Complexity | Status |
|---------|----------|-----------|--------|
| Generic RDB C+ API (`lib/rdb.h`) | P0 | Medium | ✅ Implemented |
| SQLite driver (`lib/rdb_sqlite.c`) | P0 | Medium | ✅ Implemented |
| RDB connection via `input()` | P0 | Low | ✅ Implemented |
| Schema introspection → Lambda types | P0 | Medium | ✅ Implemented: columns, views, indexes, FKs (with `link_name` and `reverse_fks`), triggers (`name`, `timing`, `event`), SQL functions (`name`, `type`, `narg`, `builtin`), datetime, JSON, and decimal typing are all exposed |
| Table element structure | P0 | Medium | Partial: `db.data.<table>` is exposed, but rows are eagerly materialized arrays rather than lazy table proxies |
| Basic `for` → `SELECT * FROM table` | P0 | Medium | Partial: table rows are loaded with `SELECT *` during `input()`, but `for` clauses are not lowered to SQL |
| `where` → `WHERE` (comparisons, and/or/not) | P0 | Medium | Partial: SQL query builder (`rdb_query.h/cpp`) generates parameterized WHERE with `=`, `!=`, `<`, `<=`, `>`, `>=`, AND/OR/NOT; not yet wired to Lambda `for`-clause evaluator |
| `order by` → `ORDER BY` | P0 | Low | Partial: query builder generates `ORDER BY` with ASC/DESC and multi-column support; not yet wired to evaluator |
| `limit` / `offset` → `LIMIT` / `OFFSET` | P0 | Low | Partial: query builder generates `LIMIT`/`OFFSET` clauses; not yet wired to evaluator |
| Parameterized SQL (injection prevention) | P0 | Low | ✅ Implemented: `rdb_query_build()` emits `?N` positional placeholders; `rdb_query_exec()` binds via `rdb_bind_*` API; schema validation rejects unknown columns |
| Lazy loading (cursor-based iteration) | P0 | Medium | Not implemented |
| FK introspection (`RdbForeignKey` structs) | P0 | Medium | ✅ Implemented: includes `link_name` derivation (singular form of FK column minus `_id` suffix) |
| FK auto-navigation (many-to-one, lazy) | P0 | Medium | ✅ Implemented: eager forward FK resolution — `product.category` auto-resolved to referenced row map at load time |
| FK reverse navigation (one-to-many, lazy) | P1 | Medium | ✅ Implemented: reverse FK arrays — `category.products` auto-built as array of related rows at load time |
| FK JOIN batching inside `for` clauses | P1 | High | Not implemented |
| Aggregate pushdown (`count`, `sum`, `avg`, `min`, `max`) | P1 | Medium | Not implemented |
| Datetime column → Lambda `datetime` | P0 | Medium | ✅ Implemented |
| JSON column → Lambda `map`/`array` (auto-parse) | P0 | Medium | ✅ Implemented |
| Views exposed as table elements | P0 | Low | ✅ Implemented |
| Schema introspection (indexes, triggers, functions) | P0 | Medium | ✅ Implemented: indexes (`name`, `unique`, `columns`), triggers (`name`, `timing`, `event`), and SQL functions (`name`, `type` scalar/aggregate/window, `narg`, `builtin`) are all exposed with GTest and integration test coverage |
| `db.schema` / `db.data` namespace structure | P0 | Medium | ✅ Implemented |
| Result caching (LRU by query) | P1 | Medium | Not implemented |
| `in` list → `IN (...)` | P1 | Low | Partial: query builder generates parameterized `IN (?1, ?2, ...)` clauses; not yet wired to evaluator |
| String functions → `LIKE` | P1 | Low | Partial: query builder generates `LIKE` with `starts_with`/`ends_with`/`contains` patterns; not yet wired to evaluator |
| Null checks → `IS NULL` / `IS NOT NULL` | P1 | Low | Partial: query builder generates `IS NULL`/`IS NOT NULL`; not yet wired to evaluator |
| `len(db.data.table)` → `SELECT COUNT(*)` | P1 | Low | Not implemented |
| Index access `db.data.table[n]` → `LIMIT 1 OFFSET n` | P1 | Low | Not implemented |
| Vendor SQLite amalgamation in `lib/` | P0 | Low | ✅ Implemented |
| GTest unit tests (generic + SQLite) | P0 | Medium | ✅ Implemented: RDB driver (126 tests), input plugin (56 tests), query builder (34 tests) |
| Lambda integration test scripts (.ls + .txt) | P0 | Medium | ✅ Implemented: 9 scripts covering basic access, schema (columns, indexes, triggers, functions), types, views, data, autodetect, FK metadata, FK navigation (forward/reverse/null), and for-clause filtering/sorting (in-memory); SQL pushdown and lazy-loading scenarios are still absent |

### Phase 2: Advanced Queries (Future)

| Feature | Notes |
|---------|-------|
| `group by` → `GROUP BY` | With aggregate functions (`sum`, `count`, `avg`, `min`, `max`) |
| Column projection pushdown | `SELECT col1, col2` instead of `SELECT *` when body only uses specific fields |
| JSON field pushdown | `json_extract()` in WHERE clauses for JSON column filtering |
| BLOB column support | `binary` type mapping for `BLOB` columns (lazy load) |
| Cross-table joins | `for (b in db.data.book, a in db.data.author where b.author_id == a.id)` → `JOIN` |
| Query-driven type narrowing | Result type narrows to selected fields: `for (b in ..) {title: b.title}` → `{title: string}[]` |
| Subqueries | Nested `for` over same DB → correlated subquery |
| `distinct` | Deduplicate rows |
| Computed columns | `let` bindings on SQL expressions (e.g., `let full = a.first ++ " " ++ a.last`) |

### Phase 3: Write Support (Future)

| Feature | Notes |
|---------|-------|
| `insert` | Procedural `db.data.book.insert({title: "New", ...})` |
| `update` | `db.data.book.update(where: {id: 1}, set: {title: "Updated"})` |
| `delete` | `db.data.book.delete(where: {id: 1})` |
| Transactions | `db.transaction(fn() { ... })` |
| Schema creation | `db.create_table("name", {col: type, ...})` |

### Phase 4: Other Database Drivers (Future)

Adding a new backend requires only implementing the `RdbDriver` vtable — no changes to the Lambda input plugin, query compiler, or runtime. Each driver is a single `.c` file plus its client library.

| Database | Driver file | Client library | Connection URI |
|----------|------------|---------------|----------------|
| PostgreSQL | `lib/rdb_pg.c` | `libpq` (system) | `input(@postgresql://host/db)` |
| MySQL | `lib/rdb_mysql.c` | `libmysqlclient` (system) | `input(@mysql://host/db)` |
| DuckDB | `lib/rdb_duckdb.c` | `libduckdb` (vendored) | `input(@./data.duckdb)` |

---

## 10. SQLite Dependency

**Approach**: Vendor the SQLite amalgamation (`sqlite3.c` + `sqlite3.h`) directly into `lib/`.

- Single-file C library, ~250KB source
- No external dependency; compiles on all platforms
- Public domain license — no licensing concerns
- Version: latest stable (≥ 3.45.0)

**Build integration**: Add `sqlite3.c` to `build_lambda_config.json` under a new `sqlite` source group, compiled as C with `-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION`.

---

## 11. Testing Strategy

### Unit Tests

**Generic RDB API + SQLite Driver** (`test/test_rdb_gtest.cpp`, 126 tests):
```
§1  Driver Registration & Detection  (14 tests)
§2  Open/Close                       (6 tests)
§3  Schema Loading                   (2 tests)
§4  Table Lookup                     (5 tests)
§5  Index Metadata                   (2 tests)
§6  FK Metadata                      (3 tests)
§7  Prepare/Execute                  (4 tests)
§8  Column Count                     (2 tests)
§9  Bind Parameters                  (4 tests)
§10 Value Types                      (5 tests)
§11 Row Count                        (2 tests)
§12 Error Handling                   (5 tests)
§13 Iteration                        (3 tests)
§14 Advanced Queries                 (5 tests)
§15 Schema Reload                    (1 test)
§16 Column Metadata                  (12 tests)
§17 Type Mapping                     (18 tests)
§18 Value Reading                    (6 tests)
§19 FK Advanced                      (8 tests)
§20 Edge Cases                       (11 tests)
§21 Column Metadata Null             (1 test)
§22 Error After Bad Prepare          (1 test)
§23 Trigger Schema                   (2 tests)
§24 Function Schema                  (2 tests)
```

**Lambda Input Plugin** (`test/test_input_rdb_gtest.cpp`, 56 tests):
```
§1  Format Detection               (4 tests)
§2  Basic Element Structure        (4 tests)
§3  Schema Namespace               (8 tests — columns, FK, view flag, indexes, triggers)
§4  Data Namespace                 (5 tests)
§5  Value Conversion               (8 tests — int, string, float, bool, null, datetime, decimal, JSON)
§6  View Data                      (2 tests)
§7  Empty Database                 (1 test)
§8  Data Integrity                 (2 tests)
§9  Error Handling                 (2 tests)
§10 Second Row Field Access        (2 tests)
§11 FK Forward Navigation          (5 tests)
§12 FK Reverse Navigation          (4 tests)
§13 Null FK Handling               (2 tests)
§14 Trigger Schema                 (3 tests)
§15 Function Schema                (2 tests)
```

**Query Builder** (`test/test_rdb_query_gtest.cpp`, 34 tests):
```
RdbQueryTest.SelectAll              RdbQueryTest.OrderByMultiple
RdbQueryTest.WhereEqual             RdbQueryTest.LimitOnly
RdbQueryTest.WhereComparison        RdbQueryTest.LimitOffset
RdbQueryTest.WhereLessThan          RdbQueryTest.OffsetWithoutLimit
RdbQueryTest.WhereNotEqual          RdbQueryTest.WhereOrderByLimitOffset
RdbQueryTest.WhereAnd               RdbQueryTest.UnknownTable
RdbQueryTest.WhereOr                RdbQueryTest.UnknownColumnInWhere
RdbQueryTest.WhereNot               RdbQueryTest.UnknownColumnInOrderBy
RdbQueryTest.WhereIn                RdbQueryTest.ExecSelectAll
RdbQueryTest.WhereInEmpty           RdbQueryTest.ExecWhereGenre
RdbQueryTest.WhereIsNull            RdbQueryTest.ExecWhereYearRange
RdbQueryTest.WhereIsNotNull         RdbQueryTest.ExecWhereIsNull
RdbQueryTest.WhereLikeStartsWith    RdbQueryTest.ExecWhereIn
RdbQueryTest.WhereLikeEndsWith      RdbQueryTest.ExecLikeContains
RdbQueryTest.WhereLikeContains      RdbQueryTest.ExecLimit
RdbQueryTest.OrderByAsc             RdbQueryTest.ExecLimitOffset
RdbQueryTest.OrderByDesc            RdbQueryTest.ExecWithCallback
```

### Integration Tests (`test/lambda/io_sqlite_*.ls`, 9 scripts)

```
io_sqlite_basic.ls       — basic access and row retrieval
io_sqlite_schema.ls      — schema introspection (columns, types, indexes, triggers, functions, FKs, views)
io_sqlite_types.ls       — type mapping (int, float, string, datetime, JSON, null)
io_sqlite_view.ls        — view access
io_sqlite_data.ls        — data namespace and row content
io_sqlite_autodetect.ls  — driver auto-detection from file extension
io_sqlite_fk.ls          — FK metadata (link_name, reverse_fks)
io_sqlite_fk_nav.ls      — FK navigation (forward many-to-one, reverse one-to-many, null FK)
io_sqlite_for_clauses.ls — for-clause with where/order-by/limit (in-memory evaluation)
```

Each `.ls` file paired with a `.txt` expected-output file.

---

## 12. Open Questions

1. **Table name conflicts**: What if a table name collides with a reserved key in the `data` map (unlikely since `data` is a plain map)? Also: what if a table is named `schema` or `data`? Propose: bracket access `db.data["schema"]` as escape hatch.

2. **Multiple databases**: `ATTACH DATABASE` allows SQLite to query across databases. Should this be exposed? E.g., `input(@./db1.db, {attach: {other: @./db2.db}})`. Attached databases could appear as additional `data` sub-maps.

3. **Write support granularity**: Phase 3 proposes procedural writes, but Lambda is fundamentally functional. An alternative: model writes as transformations that return a new database state, e.g., `let db2 = db.data.book.with({title: "New", ...})`. This is more idiomatic but harder to implement efficiently.

4. **JSON pushdown**: In Phase 1, JSON column filtering runs in Lambda post-filter. Phase 2 could push `json_extract()` to SQL. Worth scoping early to ensure the AST analysis is extensible.

5. **Column projection**: Even in Phase 1, if the `for` body only references `b.title` and `b.year`, should we generate `SELECT title, year FROM book` instead of `SELECT *`? This reduces I/O for wide tables. Trade-off: more complex AST analysis vs. simpler `SELECT *`.

6. **Aggregate pushdown**: Lambda expressions like `sum(db.data.book | ~.price)` could be compiled to `SELECT SUM(price) FROM book` instead of fetching all rows. Worth considering for Phase 2 or even late Phase 1.

7. **Virtual tables**: SQLite virtual tables (FTS5, R-Tree) respond to SELECT but have special syntax for writes and configuration. Should they be exposed read-only alongside normal tables and views?
