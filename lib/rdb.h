/**
 * @file rdb.h
 * @brief Generic relational database access API.
 *
 * Database-agnostic C API for connecting to relational databases,
 * introspecting schemas, and executing read-only queries. Each backend
 * (SQLite in-host; PostgreSQL and MySQL/MariaDB in the `rdb-drivers` Jube
 * module) implements the RdbDriver table from rdb_abi.h; the rest of the
 * system works through this header exclusively.
 *
 * Connections are host-owned (JA16.1): every native connection a driver
 * opens is registered before use, and only this layer closes it.
 *
 * See vibe/Lambda_IO_RDB.md for design rationale (§13 for drivers,
 * connection management and column batches).
 */

#ifndef LIB_RDB_H
#define LIB_RDB_H

#include "rdb_abi.h"
#include "mempool.h"
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════
 * Connection handle
 * ══════════════════════════════════════════════════════════════════════ */

/** heap-owned lifecycle record; the registry's close authority holds it */
typedef struct RdbConnLife RdbConnLife;

/** opaque prepared statement handle */
typedef struct RdbStmt RdbStmt;

typedef struct RdbConn {
    const RdbDriver* driver;
    void*           handle;     /* driver connection handle (sqlite3*, PGconn*, ...) */
    Pool*           pool;       /* pool for schema / metadata allocations */
    RdbSchema       schema;
    bool            readonly;
    const char*     uri;        /* redacted connection URI (pool-owned, RDB9) */
    uint32_t        rid;        /* host registry id; 0 when no runtime owns it */
    RdbConnInfo     info;       /* peer facts reported at registration */
    RdbConnLife*    life;       /* NULL once closed */
} RdbConn;

/* ══════════════════════════════════════════════════════════════════════
 * Public API — connection lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Open a database connection.
 * @param pool   memory pool for metadata allocations
 * @param uri    file path (SQLite) or connection URI (PostgreSQL, etc.)
 * @param type   explicit driver name, or NULL to auto-detect from URI
 * @param readonly  open in read-only mode?
 * @return connection handle, or NULL on failure
 */
RdbConn* rdb_open(Pool* pool, const char* uri, const char* type, bool readonly);
/* SQLite writable open without SQLITE_OPEN_CREATE, through the same host registry. */
RdbConn* rdb_open_sqlite_existing(Pool* pool, const char* path);

/** Open in three phases: prepare and finish on the owning thread, run on a
 * joined worker. Preparation authorizes the target and reserves its host
 * registry record. Cancellation is checked before and after the driver call;
 * a caller must still join that call before finishing or destroying `pool`. */
typedef struct RdbOpenWork RdbOpenWork;
RdbOpenWork* rdb_open_work_begin(Pool* pool, const char* uri, const char* type, bool readonly);
int rdb_open_work_run(RdbOpenWork* work, const int* cancelled);
RdbConn* rdb_open_work_end(RdbOpenWork* work, bool accept);

/** Close a database connection; finalizes any statements still live. */
void rdb_close(RdbConn* conn);

/** Optional connection ops; RDB_UNSUPPORTED when the backend lacks one. */
int rdb_ping(RdbConn* conn);
int rdb_reset(RdbConn* conn);
int rdb_cancel(RdbConn* conn);
int rdb_set_timeout(RdbConn* conn, int64_t statement_ms);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — schema access
 * ══════════════════════════════════════════════════════════════════════ */

/** Load / refresh schema metadata into conn->schema. */
int rdb_load_schema(RdbConn* conn);

/** Owner-prepared schema work. `result_pool` belongs exclusively to the worker
 * until run returns and must outlive the returned schema. The owner joins the
 * worker before end, including during teardown. No connection operations may
 * overlap this work. */
typedef struct RdbSchemaWork RdbSchemaWork;
RdbSchemaWork* rdb_schema_work_begin(RdbConn* conn, Pool* result_pool);
int rdb_schema_work_run(RdbSchemaWork* work, const int* cancelled);
int rdb_schema_work_end(RdbSchemaWork* work, RdbSchema* out);

/** Look up table by name (NULL if not found). */
RdbTable* rdb_get_table(RdbConn* conn, const char* table_name);

/** Look up column by name within a table (NULL if not found). */
RdbColumn* rdb_get_column(RdbTable* table, const char* column_name);

/** Stable normalized name for a result-column type. */
const char* rdb_type_name(RdbType type);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — SQL rendering through the driver dialect (RDB5)
 * ══════════════════════════════════════════════════════════════════════ */

/** Dialect of a connection (the SQLite dialect when the driver has none). */
const RdbDialect* rdb_dialect(const RdbConn* conn);

/** Append a quoted identifier, doubling any embedded quote character. */
void rdb_append_ident(StrBuf* sb, const RdbDialect* dialect, const char* name);

/** Append the placeholder for 1-based parameter `index`. */
void rdb_append_placeholder(StrBuf* sb, const RdbDialect* dialect, int index);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — query execution
 * ══════════════════════════════════════════════════════════════════════ */

/** Prepare a parameterized SQL statement. */
RdbStmt* rdb_prepare(RdbConn* conn, const char* sql);

/** The parameters and their strings must remain alive through run. Begin/end
 * are owner-thread operations; run is performed by a worker joined before end.
 * No other operation may use the connection during this work. */
typedef struct RdbPrepareWork RdbPrepareWork;
RdbPrepareWork* rdb_prepare_work_begin(RdbConn* conn, const char* sql,
                                       const RdbParam* params, int count);
int rdb_prepare_work_run(RdbPrepareWork* work, const int* cancelled);
RdbStmt* rdb_prepare_work_end(RdbPrepareWork* work, bool accept);

/** Worker callback receives values borrowed until the next step. */
typedef bool (*RdbRowFn)(void* user, int count, const RdbValue* values);
typedef struct RdbFetchWork RdbFetchWork;
typedef struct RdbFetchResult {
    int column_count;
    RdbColumnDesc* columns;   /* result_pool-owned; names are copied */
    int row_count;
    bool done;                /* false means more rows may be available */
} RdbFetchResult;
RdbFetchWork* rdb_fetch_work_begin(RdbStmt* stmt, int max_rows, int max_columns,
                                  Pool* result_pool, RdbRowFn row, void* user);
int rdb_fetch_work_run(RdbFetchWork* work, const int* cancelled);
int rdb_fetch_work_end(RdbFetchWork* work, RdbFetchResult* out);

/** Finalization can drain a network result, so run may block. Begin/end run
 * on the owner; if the worker never ran, end performs the cleanup itself. */
typedef struct RdbFinalizeWork RdbFinalizeWork;
RdbFinalizeWork* rdb_finalize_work_begin(RdbStmt* stmt);
void rdb_finalize_work_run(RdbFinalizeWork* work);
void rdb_finalize_work_end(RdbFinalizeWork* work);

/** Bind parameter at 1-based index. */
int rdb_bind_int(RdbStmt* stmt, int index, int64_t value);
int rdb_bind_float(RdbStmt* stmt, int index, double value);
int rdb_bind_string(RdbStmt* stmt, int index, const char* value);
int rdb_bind_null(RdbStmt* stmt, int index);
int rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param);

/** Step to next row. Returns RDB_ROW, RDB_DONE, or an error code. */
int rdb_step(RdbStmt* stmt);

/** Read column value from current row (0-based column index). */
RdbValue rdb_column_value(RdbStmt* stmt, int col_index);

/** Get number of columns in result set. */
int rdb_column_count(RdbStmt* stmt);

/** Describe a result column (name + declared type). */
int rdb_column_desc(RdbStmt* stmt, int col_index, RdbColumnDesc* out);

/** Finalize (free) a prepared statement. */
void rdb_finalize(RdbStmt* stmt);

/** Get row count for a table (SELECT COUNT(*)); -1 on error. */
int64_t rdb_row_count(RdbConn* conn, const char* table_name);

/** Human-readable error message from last operation. */
const char* rdb_error_msg(RdbConn* conn);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — column batches (RDB7)
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Describe the result as an Arrow struct schema (one child per column).
 * Call once after prepare/bind. The caller owns `out` and must release it.
 */
int rdb_result_schema(RdbStmt* stmt, struct ArrowSchema* out);

/**
 * Fetch up to `max_rows` rows as one Arrow struct array. Returns RDB_ROW
 * with a batch, RDB_DONE when exhausted, or an error code. Drivers without
 * RDB_CAP_COLUMNAR are served by the host shredding adapter, so callers see
 * one interface. Batches are independent of the statement: the caller owns
 * `out` and releases it, before or after rdb_finalize().
 */
int rdb_fetch_batch(RdbStmt* stmt, int64_t max_rows, struct ArrowArray* out);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — credential redaction (RDB9)
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Copy `uri` into `out` with every credential masked as "***": URI userinfo
 * passwords, password-like query parameters, and libpq key/value
 * `password=` values. Always NUL-terminates; returns `out`.
 */
char* rdb_redact_uri(const char* uri, char* out, size_t cap);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — driver registration and host hooks
 * ══════════════════════════════════════════════════════════════════════ */

/** Register a driver; rejects tables older than the minimum ABI. */
bool rdb_register_driver(const RdbDriver* driver);

/** Look up driver by name, consulting the resolver on a miss (RDB4). */
const RdbDriver* rdb_get_driver(const char* name);

/** Auto-detect driver from URI / file extension. Returns NULL if unknown. */
const char* rdb_detect_driver(const char* uri);

/** Resolver for drivers not yet registered (installed by the runtime). */
typedef const RdbDriver* (*RdbDriverResolver)(const char* name);
void rdb_set_driver_resolver(RdbDriverResolver resolver);

/**
 * Host connection registry (JA16.1–JA16.3), installed by the runtime.
 * Without hooks every target is allowed and connections are owned solely by
 * their caller (lib-only tools and unit tests).
 */
typedef struct RdbRegistryHooks {
    /* authorise every resolved target before the driver connects */
    int (*authorize)(const char* driver, const RdbTarget* targets, int count);
    /* record a live connection; *out_rid = 0 when no runtime owns it.
       *out_owner names the registry (runtime table) holding the row.
       RDB_ERROR refuses the connection. */
    int (*add)(RdbConnLife* life, const char* redacted_uri, uint32_t* out_rid, void** out_owner);
    /* release the registry row; the registry then calls rdb_conn_life_close */
    void (*remove)(void* owner, uint32_t rid);
} RdbRegistryHooks;
void rdb_set_registry_hooks(const RdbRegistryHooks* hooks);
// Resolve and authorize a concrete target without opening it.
bool rdb_target_authorized(const char* uri, const char* type);

/** The registry's close callback: closes the native connection exactly once. */
void rdb_conn_life_close(RdbConnLife* life);

/** Pin a live connection during a synchronous native callback; release after
 *  finalizing its statements. These calls do not provide thread synchronization. */
RdbConnLife* rdb_conn_life_hold(RdbConn* conn);
void rdb_conn_life_release(RdbConnLife* life);

/* ══════════════════════════════════════════════════════════════════════
 * Backend registration entry points
 * ══════════════════════════════════════════════════════════════════════ */

void rdb_sqlite_register(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_RDB_H */
