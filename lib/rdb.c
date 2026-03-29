/**
 * @file rdb.c
 * @brief Generic relational database layer — driver registry, connection
 *        lifecycle, and convenience wrappers that delegate to the active
 *        RdbDriver vtable.
 */

#include "rdb.h"
#include "log.h"
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
 * Driver registry (static array, max 8 drivers)
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_MAX_DRIVERS 8

static struct {
    const char*       name;
    const RdbDriver*  driver;
} rdb_drivers[RDB_MAX_DRIVERS];

static int rdb_driver_count = 0;

void rdb_register_driver(const char* name, const RdbDriver* driver) {
    if (rdb_driver_count >= RDB_MAX_DRIVERS) {
        log_error("rdb: max drivers exceeded");
        return;
    }
    // skip if already registered
    for (int i = 0; i < rdb_driver_count; i++) {
        if (strcmp(rdb_drivers[i].name, name) == 0) return;
    }
    rdb_drivers[rdb_driver_count].name   = name;
    rdb_drivers[rdb_driver_count].driver = driver;
    rdb_driver_count++;
    log_debug("rdb: registered driver '%s'", name);
}

const RdbDriver* rdb_get_driver(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < rdb_driver_count; i++) {
        if (strcmp(rdb_drivers[i].name, name) == 0) return rdb_drivers[i].driver;
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Auto-detect driver from URI / file extension
 * ══════════════════════════════════════════════════════════════════════ */

const char* rdb_detect_driver(const char* uri) {
    if (!uri) return NULL;
    size_t len = strlen(uri);

    // scheme-based detection
    if (len > 14 && strncmp(uri, "postgresql://", 13) == 0) return "postgresql";
    if (len > 11 && strncmp(uri, "postgres://", 11) == 0)   return "postgresql";
    if (len > 8  && strncmp(uri, "mysql://", 8) == 0)       return "mysql";
    if (len > 9  && strncmp(uri, "duckdb://", 9) == 0)      return "duckdb";

    // extension-based detection
    if (len > 3 && strcmp(uri + len - 3, ".db") == 0)        return "sqlite";
    if (len > 7 && strcmp(uri + len - 7, ".sqlite") == 0)    return "sqlite";
    if (len > 8 && strcmp(uri + len - 8, ".sqlite3") == 0)   return "sqlite";
    if (len > 4 && strcmp(uri + len - 4, ".ddb") == 0)       return "duckdb";
    if (len > 7 && strcmp(uri + len - 7, ".duckdb") == 0)    return "duckdb";

    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Connection lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

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
    if (!conn) {
        log_error("rdb: allocation failed for RdbConn");
        return NULL;
    }
    conn->driver   = driver;
    conn->pool     = pool;
    conn->readonly = readonly;

    // copy URI into pool
    size_t uri_len = strlen(uri);
    char* uri_copy = (char*)pool_alloc(pool, uri_len + 1);
    if (uri_copy) {
        memcpy(uri_copy, uri, uri_len + 1);
    }
    conn->uri = uri_copy;

    if (driver->open(conn, uri, readonly) != RDB_OK) {
        log_error("rdb: failed to open connection to '%s'", uri);
        return NULL;
    }

    log_debug("rdb: opened '%s' with driver '%s' (readonly=%d)", uri, driver_name, readonly);
    return conn;
}

void rdb_close(RdbConn* conn) {
    if (conn && conn->driver) {
        log_debug("rdb: closing connection to '%s'", conn->uri ? conn->uri : "(null)");
        conn->driver->close(conn);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * Schema access
 * ══════════════════════════════════════════════════════════════════════ */

int rdb_load_schema(RdbConn* conn) {
    if (!conn || !conn->driver || !conn->driver->load_schema) return RDB_ERROR;
    return conn->driver->load_schema(conn, &conn->schema);
}

RdbTable* rdb_get_table(RdbConn* conn, const char* table_name) {
    if (!conn || !table_name) return NULL;
    for (int i = 0; i < conn->schema.table_count; i++) {
        if (strcmp(conn->schema.tables[i].name, table_name) == 0)
            return &conn->schema.tables[i];
    }
    return NULL;
}

RdbColumn* rdb_get_column(RdbTable* table, const char* column_name) {
    if (!table || !column_name) return NULL;
    for (int i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, column_name) == 0)
            return &table->columns[i];
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Query execution — convenience wrappers delegating to driver vtable
 * ══════════════════════════════════════════════════════════════════════ */

RdbStmt* rdb_prepare(RdbConn* conn, const char* sql) {
    if (!conn || !conn->driver || !sql) return NULL;
    RdbStmt* stmt = NULL;
    if (conn->driver->prepare(conn, sql, &stmt) != RDB_OK) return NULL;
    return stmt;
}

int rdb_bind_int(RdbStmt* stmt, int index, int64_t value) {
    RdbParam p = {0};
    p.type    = RDB_TYPE_INT;
    p.int_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_float(RdbStmt* stmt, int index, double value) {
    RdbParam p = {0};
    p.type      = RDB_TYPE_FLOAT;
    p.float_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_string(RdbStmt* stmt, int index, const char* value) {
    RdbParam p = {0};
    p.type    = RDB_TYPE_STRING;
    p.str_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_null(RdbStmt* stmt, int index) {
    RdbParam p = {0};
    p.type = RDB_TYPE_NULL;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param) {
    if (!stmt || !param) return RDB_ERROR;
    // each backend stores a back-pointer to conn in stmt; the driver
    // vtable is reached through the driver field on the conn, but since
    // stmt is opaque we need the backend to implement bind_param using
    // its own internal pointers.  So we access the driver through the
    // first field of the stmt struct (by convention, every backend embeds
    // an RdbConn* as the first member).
    RdbConn** conn_ptr = (RdbConn**)stmt;
    RdbConn* conn = *conn_ptr;
    if (!conn || !conn->driver) return RDB_ERROR;
    return conn->driver->bind_param(stmt, index, param);
}

int rdb_step(RdbStmt* stmt) {
    if (!stmt) return RDB_ERROR;
    RdbConn** conn_ptr = (RdbConn**)stmt;
    RdbConn* conn = *conn_ptr;
    if (!conn || !conn->driver) return RDB_ERROR;
    return conn->driver->step(stmt);
}

RdbValue rdb_column_value(RdbStmt* stmt, int col_index) {
    RdbValue val = {0};
    val.type = RDB_TYPE_NULL;
    val.is_null = true;
    if (!stmt) return val;
    RdbConn** conn_ptr = (RdbConn**)stmt;
    RdbConn* conn = *conn_ptr;
    if (!conn || !conn->driver) return val;
    return conn->driver->column_value(stmt, col_index);
}

int rdb_column_count(RdbStmt* stmt) {
    if (!stmt) return 0;
    RdbConn** conn_ptr = (RdbConn**)stmt;
    RdbConn* conn = *conn_ptr;
    if (!conn || !conn->driver) return 0;
    return conn->driver->column_count(stmt);
}

void rdb_finalize(RdbStmt* stmt) {
    if (!stmt) return;
    RdbConn** conn_ptr = (RdbConn**)stmt;
    RdbConn* conn = *conn_ptr;
    if (!conn || !conn->driver) return;
    conn->driver->finalize(stmt);
}

int64_t rdb_row_count(RdbConn* conn, const char* table_name) {
    if (!conn || !conn->driver || !conn->driver->row_count) return -1;
    return conn->driver->row_count(conn, table_name);
}

const char* rdb_error_msg(RdbConn* conn) {
    if (!conn || !conn->driver || !conn->driver->error_msg) return "rdb: unknown error";
    return conn->driver->error_msg(conn);
}
