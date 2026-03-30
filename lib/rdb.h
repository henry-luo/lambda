/**
 * @file rdb.h
 * @brief Generic relational database access API.
 *
 * Database-agnostic C API for connecting to relational databases,
 * introspecting schemas, and executing read-only queries. Each backend
 * (SQLite, PostgreSQL, etc.) implements the RdbDriver vtable; the rest
 * of the system works through this header exclusively.
 *
 * See vibe/Lambda_IO_RDB.md for design rationale.
 */

#ifndef LIB_RDB_H
#define LIB_RDB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════
 * Return codes
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_OK    0
#define RDB_ERROR (-1)
#define RDB_ROW   100
#define RDB_DONE  101

/* ══════════════════════════════════════════════════════════════════════
 * Column type enum
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_TYPE_NULL = 0,
    RDB_TYPE_INT,
    RDB_TYPE_FLOAT,
    RDB_TYPE_STRING,
    RDB_TYPE_DECIMAL,
    RDB_TYPE_DATETIME,
    RDB_TYPE_JSON,
    RDB_TYPE_BOOL,
    RDB_TYPE_BLOB,
    RDB_TYPE_UNKNOWN
} RdbType;

/* ══════════════════════════════════════════════════════════════════════
 * Schema metadata structs
 * ══════════════════════════════════════════════════════════════════════ */

/** column metadata */
typedef struct {
    const char* name;           /* column name (pool-owned) */
    const char* type_decl;      /* raw declared type, e.g. "VARCHAR(255)" */
    RdbType     type;           /* normalised type enum */
    bool        nullable;       /* allows NULL? */
    bool        primary_key;    /* part of primary key? */
    int         pk_index;       /* position in composite PK (0 if not PK) */
} RdbColumn;

/** index metadata */
typedef struct {
    const char* name;           /* index name */
    bool        unique;         /* unique index? */
    int         column_count;
    const char** columns;       /* column names (array, pool-owned) */
} RdbIndex;

/** trigger metadata */
typedef struct {
    const char* name;           /* trigger name */
    const char* timing;         /* "BEFORE", "AFTER", or "INSTEAD OF" */
    const char* event;          /* "INSERT", "UPDATE", or "DELETE" */
} RdbTrigger;

/** foreign key metadata */
typedef struct {
    const char* column;         /* FK column in this table */
    const char* ref_table;      /* referenced table name */
    const char* ref_column;     /* referenced column name */
    const char* link_name;      /* derived navigation name (stripped _id suffix) */
} RdbForeignKey;

/** table / view metadata */
typedef struct {
    const char* name;           /* table or view name */
    bool        is_view;        /* true for views */
    int         column_count;
    RdbColumn*  columns;        /* array (pool-owned) */
    int         index_count;
    RdbIndex*   indexes;        /* array (pool-owned) */
    int         fk_count;
    RdbForeignKey* foreign_keys;    /* outgoing FKs (pool-owned) */
    int         reverse_fk_count;
    RdbForeignKey* reverse_fks;     /* incoming FKs from other tables (pool-owned) */
    int         trigger_count;
    RdbTrigger* triggers;           /* array (pool-owned) */
} RdbTable;

/** database schema (all tables + views) */
typedef struct {
    int         table_count;
    RdbTable*   tables;         /* array (pool-owned) */
} RdbSchema;

/* ══════════════════════════════════════════════════════════════════════
 * Query parameter binding
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    RdbType type;
    union {
        int64_t     int_val;
        double      float_val;
        const char* str_val;
        bool        bool_val;
    };
} RdbParam;

/* ══════════════════════════════════════════════════════════════════════
 * Result row value
 * ══════════════════════════════════════════════════════════════════════ */

/** opaque prepared statement handle */
typedef struct RdbStmt RdbStmt;

/** single cell value from current row */
typedef struct {
    RdbType type;
    bool    is_null;
    union {
        int64_t     int_val;
        double      float_val;
        struct {
            const char* str_val;  /* valid until next step() or finalize() */
            int         str_len;
        };
        bool        bool_val;
    };
} RdbValue;

/* ══════════════════════════════════════════════════════════════════════
 * Driver vtable
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct RdbConn RdbConn;
typedef struct RdbDriver RdbDriver;

struct RdbDriver {
    const char* name;       /* "sqlite", "postgresql", ... */

    /* connection */
    int     (*open)(RdbConn* conn, const char* uri, bool readonly);
    void    (*close)(RdbConn* conn);

    /* schema introspection */
    int     (*load_schema)(RdbConn* conn, RdbSchema* out_schema);

    /* query execution */
    int     (*prepare)(RdbConn* conn, const char* sql, RdbStmt** out_stmt);
    int     (*bind_param)(RdbStmt* stmt, int index, const RdbParam* param);
    int     (*step)(RdbStmt* stmt);             /* returns RDB_ROW / RDB_DONE / RDB_ERROR */
    int     (*column_count)(RdbStmt* stmt);
    RdbValue(*column_value)(RdbStmt* stmt, int col_index);
    void    (*finalize)(RdbStmt* stmt);

    /* row count (optional) */
    int64_t (*row_count)(RdbConn* conn, const char* table_name);

    /* error info */
    const char* (*error_msg)(RdbConn* conn);
};

/* ══════════════════════════════════════════════════════════════════════
 * Connection handle
 * ══════════════════════════════════════════════════════════════════════ */

struct RdbConn {
    const RdbDriver* driver;
    void*           handle;     /* backend-specific (sqlite3*, PGconn*, ...) */
    Pool*           pool;       /* pool for schema / metadata allocations */
    RdbSchema       schema;
    bool            readonly;
    const char*     uri;        /* connection URI / file path (pool-owned) */
};

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

/** Close a database connection. */
void rdb_close(RdbConn* conn);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — schema access
 * ══════════════════════════════════════════════════════════════════════ */

/** Load / refresh schema metadata into conn->schema. */
int rdb_load_schema(RdbConn* conn);

/** Look up table by name (NULL if not found). */
RdbTable* rdb_get_table(RdbConn* conn, const char* table_name);

/** Look up column by name within a table (NULL if not found). */
RdbColumn* rdb_get_column(RdbTable* table, const char* column_name);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — query execution
 * ══════════════════════════════════════════════════════════════════════ */

/** Prepare a parameterized SQL statement. */
RdbStmt* rdb_prepare(RdbConn* conn, const char* sql);

/** Bind parameter at 1-based index. */
int rdb_bind_int(RdbStmt* stmt, int index, int64_t value);
int rdb_bind_float(RdbStmt* stmt, int index, double value);
int rdb_bind_string(RdbStmt* stmt, int index, const char* value);
int rdb_bind_null(RdbStmt* stmt, int index);
int rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param);

/** Step to next row. Returns RDB_ROW, RDB_DONE, or RDB_ERROR. */
int rdb_step(RdbStmt* stmt);

/** Read column value from current row (0-based column index). */
RdbValue rdb_column_value(RdbStmt* stmt, int col_index);

/** Get number of columns in result set. */
int rdb_column_count(RdbStmt* stmt);

/** Finalize (free) a prepared statement. */
void rdb_finalize(RdbStmt* stmt);

/** Get row count for a table (SELECT COUNT(*)). */
int64_t rdb_row_count(RdbConn* conn, const char* table_name);

/** Human-readable error message from last operation. */
const char* rdb_error_msg(RdbConn* conn);

/* ══════════════════════════════════════════════════════════════════════
 * Public API — driver registration
 * ══════════════════════════════════════════════════════════════════════ */

/** Register a driver (called at startup). */
void rdb_register_driver(const char* name, const RdbDriver* driver);

/** Look up driver by name. */
const RdbDriver* rdb_get_driver(const char* name);

/** Auto-detect driver from URI / file extension. Returns NULL if unknown. */
const char* rdb_detect_driver(const char* uri);

/* ══════════════════════════════════════════════════════════════════════
 * Backend registration entry points (each driver provides one)
 * ══════════════════════════════════════════════════════════════════════ */

void rdb_sqlite_register(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_RDB_H */
