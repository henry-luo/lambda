/**
 * @file rdb_sqlite.c
 * @brief SQLite backend driver for the generic RDB API.
 *
 * Implements the RdbDriver table (rdb_abi.h) using the vendored SQLite
 * amalgamation; it reaches the host only through RdbHostAPI, like a module driver.
 * Schema introspection uses PRAGMA queries; queries use the standard
 * sqlite3_prepare_v2 / sqlite3_step / sqlite3_column_* API.
 */

#include "rdb.h"
#include "rdb_sqlite_tx.h"
#include "memtrack.h"
#include "sqlite/sqlite3.h"
#include "log.h"
#include "strbuf.h"
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════
 * Connection
 * ══════════════════════════════════════════════════════════════════════ */

static const RdbDialect sqlite_dialect = { RDB_PLACEHOLDER_QNUM, '"' };

/** a SQLite database is a file, or memory for ":memory:" and "" */
static bool sqlite_target(const char* uri, RdbTarget* out) {
    memset(out, 0, sizeof(*out));
    if (!uri[0] || strcmp(uri, ":memory:") == 0) {
        out->kind = RDB_PEER_MEMORY;
        return true;
    }
    // The registry must authorize the exact path SQLite opens. SQLite URI
    // handling can be enabled globally even without SQLITE_OPEN_URI.
    size_t length = strlen(uri);
    if (strncmp(uri, "file:", 5) == 0 || length >= sizeof(out->path)) return false;
    out->kind = RDB_PEER_FILE;
    memcpy(out->path, uri, length + 1);
    return true;
}

static int sqlite_resolve_targets(const char* uri, RdbTarget* out, int cap, int* out_count) {
    if (!uri || cap < 1) return RDB_ERROR;
    if (!sqlite_target(uri, &out[0])) return RDB_ERROR;
    *out_count = 1;
    return RDB_OK;
}

static int sqlite_open(const RdbHostAPI* host, void* open_ctx, const char* uri,
                       const RdbOpenOptions* opts, void** out_conn) {
    RdbTarget peer;
    if (!uri || !sqlite_target(uri, &peer)) return RDB_ERROR;
    bool no_create = opts->struct_size >= offsetof(RdbOpenOptions, sqlite_no_create) + sizeof(opts->sqlite_no_create)
        && opts->sqlite_no_create;
    int flags = opts->readonly
        ? SQLITE_OPEN_READONLY
        : (SQLITE_OPEN_READWRITE | (no_create ? 0 : SQLITE_OPEN_CREATE));
    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(uri, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: open failed: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return RDB_ERROR;
    }
    // JA16.1: the host owns the connection from here on
    RdbConnInfo info;
    memset(&info, 0, sizeof(info));
    info.peer = peer;
    info.socket_fd = -1;
    snprintf(info.server_version, sizeof(info.server_version), "%s", sqlite3_libversion());
    if (host->conn_register(open_ctx, db, &info) != RDB_OK) {
        sqlite3_close(db);
        return RDB_ERROR;
    }
    *out_conn = db;
    return RDB_OK;
}

static void sqlite_close(void* conn) {
    if (conn) sqlite3_close((sqlite3*)conn);
}

static int sqlite_ping(void* conn) {
    return conn ? RDB_OK : RDB_CONN_LOST;
}

static int sqlite_cancel(void* conn) {
    // sqlite3_interrupt is the one SQLite call documented safe from any thread
    sqlite3_interrupt((sqlite3*)conn);
    return RDB_OK;
}

/** schema metadata goes through the host arena (RdbHostAPI.meta_*) */
typedef struct {
    const RdbHostAPI* host;
    void*             meta;
} SqliteMeta;

static void* meta_calloc(const SqliteMeta* m, size_t size) {
    return m->host->meta_alloc(m->meta, size);
}

static const char* meta_dup(const SqliteMeta* m, const char* s) {
    return m->host->meta_strdup(m->meta, s ? s : "");
}

/** PRAGMA <name>("<ident>") with the identifier quoted for SQLite */
static StrBuf* sqlite_pragma(const char* pragma, const char* ident) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "PRAGMA ");
    strbuf_append_str(sb, pragma);
    strbuf_append_char(sb, '(');
    rdb_append_ident(sb, &sqlite_dialect, ident);
    strbuf_append_char(sb, ')');
    return sb;
}

/* ══════════════════════════════════════════════════════════════════════
 * Type mapping
 * ══════════════════════════════════════════════════════════════════════ */

/** case-insensitive prefix match */
static bool ci_prefix(const char* s, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        s++; prefix++;
    }
    return true;
}

static RdbType sqlite_map_type(const char* type_decl) {
    if (!type_decl || type_decl[0] == '\0') return RDB_TYPE_STRING;

    if (ci_prefix(type_decl, "INTEGER")  ||
        ci_prefix(type_decl, "INT")      ||
        ci_prefix(type_decl, "BIGINT")   ||
        ci_prefix(type_decl, "SMALLINT") ||
        ci_prefix(type_decl, "TINYINT"))     return RDB_TYPE_INT;

    if (ci_prefix(type_decl, "BOOLEAN"))     return RDB_TYPE_BOOL;

    if (ci_prefix(type_decl, "REAL")   ||
        ci_prefix(type_decl, "DOUBLE") ||
        ci_prefix(type_decl, "FLOAT"))       return RDB_TYPE_FLOAT;

    if (ci_prefix(type_decl, "DECIMAL") ||
        ci_prefix(type_decl, "NUMERIC"))     return RDB_TYPE_DECIMAL;

    if (ci_prefix(type_decl, "DATE")      ||
        ci_prefix(type_decl, "DATETIME")  ||
        ci_prefix(type_decl, "TIMESTAMP"))   return RDB_TYPE_DATETIME;

    if (ci_prefix(type_decl, "JSON"))        return RDB_TYPE_JSON;
    if (ci_prefix(type_decl, "BLOB"))        return RDB_TYPE_BLOB;

    return RDB_TYPE_STRING; /* default: TEXT affinity */
}

/* ══════════════════════════════════════════════════════════════════════
 * Schema introspection
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Count tables + views from sqlite_master.
 */
static int sqlite_count_tables(sqlite3* db) {
    const char* sql = "SELECT COUNT(*) FROM sqlite_master "
                      "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%'";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

/**
 * Load column metadata for a table via PRAGMA table_info.
 */
static int sqlite_load_columns(sqlite3* db, const SqliteMeta* m, RdbTable* tbl) {
    StrBuf* sb = sqlite_pragma("table_info", tbl->name);

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sb->str, -1, &stmt, NULL) != SQLITE_OK) {
        strbuf_free(sb);
        return RDB_ERROR;
    }
    strbuf_free(sb);

    // first pass: count columns
    int col_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) col_count++;
    sqlite3_reset(stmt);

    if (col_count == 0) {
        sqlite3_finalize(stmt);
        return RDB_OK;
    }

    tbl->columns = (RdbColumn*)meta_calloc(m, (size_t)col_count * sizeof(RdbColumn));
    tbl->column_count = col_count;

    // second pass: populate
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < col_count) {
        // PRAGMA table_info: cid, name, type, notnull, dflt_value, pk
        const char* name     = (const char*)sqlite3_column_text(stmt, 1);
        const char* type_str = (const char*)sqlite3_column_text(stmt, 2);
        int notnull          = sqlite3_column_int(stmt, 3);
        int pk               = sqlite3_column_int(stmt, 5);

        tbl->columns[i].name        = meta_dup(m, name ? name : "");
        tbl->columns[i].type_decl   = meta_dup(m, type_str ? type_str : "");
        tbl->columns[i].type        = sqlite_map_type(type_str);
        tbl->columns[i].nullable    = (notnull == 0);
        tbl->columns[i].primary_key = (pk > 0);
        tbl->columns[i].pk_index    = pk;
        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

/**
 * Load index metadata for a table via PRAGMA index_list + PRAGMA index_info.
 */
static int sqlite_load_indexes(sqlite3* db, const SqliteMeta* m, RdbTable* tbl) {
    StrBuf* sb = sqlite_pragma("index_list", tbl->name);

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sb->str, -1, &stmt, NULL) != SQLITE_OK) {
        strbuf_free(sb);
        return RDB_OK; // indexes are optional
    }
    strbuf_free(sb);

    // count indexes
    int idx_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) idx_count++;
    sqlite3_reset(stmt);

    if (idx_count == 0) {
        sqlite3_finalize(stmt);
        return RDB_OK;
    }

    tbl->indexes = (RdbIndex*)meta_calloc(m, (size_t)idx_count * sizeof(RdbIndex));
    tbl->index_count = idx_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < idx_count) {
        // PRAGMA index_list: seq, name, unique, origin, partial
        const char* idx_name = (const char*)sqlite3_column_text(stmt, 1);
        int is_unique        = sqlite3_column_int(stmt, 2);

        tbl->indexes[i].name   = meta_dup(m, idx_name ? idx_name : "");
        tbl->indexes[i].unique = (is_unique != 0);

        // load columns for this index
        StrBuf* sb2 = sqlite_pragma("index_info", idx_name ? idx_name : "");

        sqlite3_stmt* col_stmt = NULL;
        if (sqlite3_prepare_v2(db, sb2->str, -1, &col_stmt, NULL) == SQLITE_OK) {
            // count columns in this index
            int col_count = 0;
            while (sqlite3_step(col_stmt) == SQLITE_ROW) col_count++;
            sqlite3_reset(col_stmt);

            if (col_count > 0) {
                tbl->indexes[i].columns = (const char**)meta_calloc(m, (size_t)col_count * sizeof(const char*));
                tbl->indexes[i].column_count = col_count;
                int j = 0;
                while (sqlite3_step(col_stmt) == SQLITE_ROW && j < col_count) {
                    // PRAGMA index_info: seqno, cid, name
                    const char* col_name = (const char*)sqlite3_column_text(col_stmt, 2);
                    tbl->indexes[i].columns[j] = meta_dup(m, col_name ? col_name : "");
                    j++;
                }
            }
            sqlite3_finalize(col_stmt);
        }
        strbuf_free(sb2);
        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

/**
 * Load foreign key metadata for a table via PRAGMA foreign_key_list.
 */
static int sqlite_load_foreign_keys(sqlite3* db, const SqliteMeta* m, RdbTable* tbl) {
    StrBuf* sb = sqlite_pragma("foreign_key_list", tbl->name);

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sb->str, -1, &stmt, NULL) != SQLITE_OK) {
        strbuf_free(sb);
        return RDB_OK; // FKs are optional
    }
    strbuf_free(sb);

    // count FKs
    int fk_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) fk_count++;
    sqlite3_reset(stmt);

    if (fk_count == 0) {
        sqlite3_finalize(stmt);
        return RDB_OK;
    }

    tbl->foreign_keys = (RdbForeignKey*)meta_calloc(m, (size_t)fk_count * sizeof(RdbForeignKey));
    tbl->fk_count = fk_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < fk_count) {
        // PRAGMA foreign_key_list: id, seq, table, from, to, on_update, on_delete, match
        const char* ref_table  = (const char*)sqlite3_column_text(stmt, 2);
        const char* from_col   = (const char*)sqlite3_column_text(stmt, 3);
        const char* to_col     = (const char*)sqlite3_column_text(stmt, 4);

        tbl->foreign_keys[i].column    = meta_dup(m, from_col ? from_col : "");
        tbl->foreign_keys[i].ref_table = meta_dup(m, ref_table ? ref_table : "");
        tbl->foreign_keys[i].ref_column = meta_dup(m, to_col ? to_col : "");

        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

/**
 * Parse trigger timing from CREATE TRIGGER DDL text.
 * Returns a host-arena string "BEFORE", "AFTER", or "INSTEAD OF".
 */
static const char* trigger_parse_timing(const char* sql, const SqliteMeta* m) {
    if (!sql) return meta_dup(m, "AFTER");
    // "INSTEAD OF" must be checked before "AFTER" to avoid false positives
    char upper[32];
    const char* p = sql;
    // scan for timing keyword; it always appears in the preamble before "ON"
    while (*p) {
        // check INSTEAD OF (case-insensitive, length 10)
        if ((p[0]=='I'||p[0]=='i') && (p[1]=='N'||p[1]=='n') &&
            (p[2]=='S'||p[2]=='s') && (p[3]=='T'||p[3]=='t') &&
            (p[4]=='E'||p[4]=='e') && (p[5]=='A'||p[5]=='a') &&
            (p[6]=='D'||p[6]=='d')) {
            return meta_dup(m, "INSTEAD OF");
        }
        // check BEFORE (length 6)
        if ((p[0]=='B'||p[0]=='b') && (p[1]=='E'||p[1]=='e') &&
            (p[2]=='F'||p[2]=='f') && (p[3]=='O'||p[3]=='o') &&
            (p[4]=='R'||p[4]=='r') && (p[5]=='E'||p[5]=='e')) {
            return meta_dup(m, "BEFORE");
        }
        // check AFTER (length 5)
        if ((p[0]=='A'||p[0]=='a') && (p[1]=='F'||p[1]=='f') &&
            (p[2]=='T'||p[2]=='t') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='R'||p[4]=='r')) {
            return meta_dup(m, "AFTER");
        }
        p++;
    }
    (void)upper;
    return meta_dup(m, "AFTER");
}

/**
 * Parse trigger event from CREATE TRIGGER DDL text.
 * Returns a host-arena string "INSERT", "UPDATE", or "DELETE".
 */
static const char* trigger_parse_event(const char* sql, const SqliteMeta* m) {
    if (!sql) return meta_dup(m, "INSERT");
    const char* p = sql;
    while (*p) {
        if ((p[0]=='I'||p[0]=='i') && (p[1]=='N'||p[1]=='n') &&
            (p[2]=='S'||p[2]=='s') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='R'||p[4]=='r') && (p[5]=='T'||p[5]=='t')) {
            return meta_dup(m, "INSERT");
        }
        if ((p[0]=='D'||p[0]=='d') && (p[1]=='E'||p[1]=='e') &&
            (p[2]=='L'||p[2]=='l') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='T'||p[4]=='t') && (p[5]=='E'||p[5]=='e')) {
            return meta_dup(m, "DELETE");
        }
        if ((p[0]=='U'||p[0]=='u') && (p[1]=='P'||p[1]=='p') &&
            (p[2]=='D'||p[2]=='d') && (p[3]=='A'||p[3]=='a') &&
            (p[4]=='T'||p[4]=='t') && (p[5]=='E'||p[5]=='e')) {
            return meta_dup(m, "UPDATE");
        }
        p++;
    }
    return meta_dup(m, "INSERT");
}

/**
 * Load trigger metadata for a table from sqlite_master.
 */
static int sqlite_load_triggers(sqlite3* db, const SqliteMeta* m, RdbTable* tbl) {
    const char* sql = "SELECT name, sql FROM sqlite_master "
                      "WHERE type = 'trigger' AND tbl_name = ?1";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return RDB_OK;  // triggers are optional
    }
    sqlite3_bind_text(stmt, 1, tbl->name, -1, SQLITE_STATIC);

    // count triggers
    int trig_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) trig_count++;
    sqlite3_reset(stmt);

    if (trig_count == 0) {
        sqlite3_finalize(stmt);
        return RDB_OK;
    }

    tbl->triggers = (RdbTrigger*)meta_calloc(m, (size_t)trig_count * sizeof(RdbTrigger));
    tbl->trigger_count = trig_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < trig_count) {
        const char* name     = (const char*)sqlite3_column_text(stmt, 0);
        const char* ddl_sql  = (const char*)sqlite3_column_text(stmt, 1);

        tbl->triggers[i].name   = meta_dup(m, name ? name : "");
        tbl->triggers[i].timing = trigger_parse_timing(ddl_sql, m);
        tbl->triggers[i].event  = trigger_parse_event(ddl_sql, m);
        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

/* ─── SQL function introspection (database-level) ─── */

static const char* func_type_str(const char* type_code, const SqliteMeta* m) {
    if (!type_code || !type_code[0]) return meta_dup(m, "scalar");
    switch (type_code[0]) {
        case 'w': return meta_dup(m, "window");
        case 'a': return meta_dup(m, "aggregate");
        default:  return meta_dup(m, "scalar");
    }
}

static int sqlite_load_functions(sqlite3* db, const SqliteMeta* m, RdbSchema* schema) {
    const char* sql = "SELECT name, builtin, type, narg FROM pragma_function_list "
                      "ORDER BY name, narg";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_debug("rdb sqlite: pragma_function_list not available");
        schema->function_count = 0;
        schema->functions = NULL;
        return RDB_OK;  // functions are optional
    }

    // count functions
    int func_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) func_count++;
    sqlite3_reset(stmt);

    if (func_count == 0) {
        sqlite3_finalize(stmt);
        schema->function_count = 0;
        schema->functions = NULL;
        return RDB_OK;
    }

    schema->functions = (RdbFunction*)meta_calloc(m, (size_t)func_count * sizeof(RdbFunction));
    schema->function_count = func_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < func_count) {
        const char* name    = (const char*)sqlite3_column_text(stmt, 0);
        int         builtin = sqlite3_column_int(stmt, 1);
        const char* type    = (const char*)sqlite3_column_text(stmt, 2);
        int         narg    = sqlite3_column_int(stmt, 3);

        schema->functions[i].name    = meta_dup(m, name ? name : "");
        schema->functions[i].builtin = (builtin != 0);
        schema->functions[i].type    = func_type_str(type, m);
        schema->functions[i].narg    = narg;
        i++;
    }
    sqlite3_finalize(stmt);

    log_debug("rdb sqlite: loaded %d functions", schema->function_count);
    return RDB_OK;
}

static int sqlite_load_schema(void* conn, const RdbHostAPI* host, void* meta,
                              RdbSchema* out_schema) {
    sqlite3* db = (sqlite3*)conn;
    SqliteMeta meta_ctx = { host, meta };
    const SqliteMeta* m = &meta_ctx;

    // enumerate tables and views
    const char* sql = "SELECT name, type FROM sqlite_master "
                      "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
                      "ORDER BY name";
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("rdb sqlite: schema query failed: %s", sqlite3_errmsg(db));
        return RDB_ERROR;
    }

    int table_count = sqlite_count_tables(db);
    if (table_count == 0) {
        sqlite3_finalize(stmt);
        out_schema->table_count = 0;
        out_schema->tables = NULL;
        return RDB_OK;
    }

    out_schema->tables = (RdbTable*)meta_calloc(m, (size_t)table_count * sizeof(RdbTable));
    out_schema->table_count = table_count;

    int t = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && t < table_count) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        const char* type = (const char*)sqlite3_column_text(stmt, 1);

        out_schema->tables[t].name    = meta_dup(m, name ? name : "");
        out_schema->tables[t].is_view = (type && strcmp(type, "view") == 0);

        sqlite_load_columns(db, m, &out_schema->tables[t]);
        sqlite_load_indexes(db, m, &out_schema->tables[t]);
        sqlite_load_foreign_keys(db, m, &out_schema->tables[t]);
        sqlite_load_triggers(db, m, &out_schema->tables[t]);

        log_debug("rdb sqlite: table '%s' (%d cols, %d idx, %d fk, %d trig, view=%d)",
                  out_schema->tables[t].name,
                  out_schema->tables[t].column_count,
                  out_schema->tables[t].index_count,
                  out_schema->tables[t].fk_count,
                  out_schema->tables[t].trigger_count,
                  out_schema->tables[t].is_view);
        t++;
    }
    sqlite3_finalize(stmt);

    // load database-level SQL functions
    sqlite_load_functions(db, m, out_schema);

    log_debug("rdb sqlite: loaded schema with %d tables/views, %d functions",
              out_schema->table_count, out_schema->function_count);
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Query execution — statement handles are raw sqlite3_stmt pointers
 * ══════════════════════════════════════════════════════════════════════ */

static int sqlite_prepare(void* conn, const char* sql, void** out_stmt) {
    sqlite3* db = (sqlite3*)conn;
    sqlite3_stmt* raw = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &raw, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: prepare failed: %s", sqlite3_errmsg(db));
        return RDB_ERROR;
    }
    *out_stmt = raw;
    return RDB_OK;
}

static int sqlite_bind_param(void* stmt, int index, const RdbParam* param) {
    sqlite3_stmt* raw = (sqlite3_stmt*)stmt;
    int rc;
    switch (param->type) {
        case RDB_TYPE_INT:
            rc = sqlite3_bind_int64(raw, index, param->int_val);
            break;
        case RDB_TYPE_FLOAT:
            rc = sqlite3_bind_double(raw, index, param->float_val);
            break;
        case RDB_TYPE_STRING:
        case RDB_TYPE_DATETIME:
        case RDB_TYPE_JSON:
            rc = sqlite3_bind_text(raw, index, param->str_val, -1, SQLITE_TRANSIENT);
            break;
        case RDB_TYPE_BOOL:
            rc = sqlite3_bind_int(raw, index, param->bool_val ? 1 : 0);
            break;
        case RDB_TYPE_NULL:
            rc = sqlite3_bind_null(raw, index);
            break;
        default:
            return RDB_ERROR;
    }
    return (rc == SQLITE_OK) ? RDB_OK : RDB_ERROR;
}

static int sqlite_step(void* stmt) {
    int rc = sqlite3_step((sqlite3_stmt*)stmt);
    if (rc == SQLITE_ROW)  return RDB_ROW;
    if (rc == SQLITE_DONE) return RDB_DONE;
    log_error("rdb sqlite: step error: %s",
              sqlite3_errmsg(sqlite3_db_handle((sqlite3_stmt*)stmt)));
    return RDB_ERROR;
}

static int sqlite_column_count(void* stmt) {
    return sqlite3_column_count((sqlite3_stmt*)stmt);
}

static int sqlite_column_desc(void* stmt, int col, RdbColumnDesc* out) {
    sqlite3_stmt* raw = (sqlite3_stmt*)stmt;
    if (col < 0 || col >= sqlite3_column_count(raw)) return RDB_ERROR;
    out->name = sqlite3_column_name(raw, col);
    // expression columns have no declared type; the host types them from data
    const char* decl = sqlite3_column_decltype(raw, col);
    out->type = decl ? sqlite_map_type(decl) : RDB_TYPE_UNKNOWN;
    return RDB_OK;
}

static RdbValue sqlite_column_value(void* stmt, int col_index) {
    sqlite3_stmt* raw = (sqlite3_stmt*)stmt;
    RdbValue val;
    memset(&val, 0, sizeof(val));

    int col_type = sqlite3_column_type(raw, col_index);
    switch (col_type) {
        case SQLITE_INTEGER:
            val.type    = RDB_TYPE_INT;
            val.int_val = sqlite3_column_int64(raw, col_index);
            break;
        case SQLITE_FLOAT:
            val.type      = RDB_TYPE_FLOAT;
            val.float_val = sqlite3_column_double(raw, col_index);
            break;
        case SQLITE_TEXT:
            val.type    = RDB_TYPE_STRING;
            val.str_val = (const char*)sqlite3_column_text(raw, col_index);
            val.str_len = sqlite3_column_bytes(raw, col_index);
            break;
        case SQLITE_BLOB:
            val.type    = RDB_TYPE_BLOB;
            val.is_null = true; /* defer BLOB to Phase 2 */
            break;
        case SQLITE_NULL:
            val.type    = RDB_TYPE_NULL;
            val.is_null = true;
            break;
        default:
            val.type    = RDB_TYPE_UNKNOWN;
            val.is_null = true;
            break;
    }
    return val;
}

static void sqlite_finalize(void* stmt) {
    if (stmt) sqlite3_finalize((sqlite3_stmt*)stmt);
}

static const char* sqlite_error_msg(void* conn) {
    if (!conn) return "rdb sqlite: no connection";
    return sqlite3_errmsg((sqlite3*)conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * Driver table + registration
 * ══════════════════════════════════════════════════════════════════════ */

static const RdbDriver sqlite_driver = {
    .struct_size     = sizeof(RdbDriver),
    .api_version     = RDB_DRIVER_API_VERSION,
    .name            = "sqlite",
    .dialect         = &sqlite_dialect,
    .caps            = RDB_CAP_CANCEL | RDB_CAP_STREAMING,
    .resolve_targets = sqlite_resolve_targets,
    .open            = sqlite_open,
    .close           = sqlite_close,
    .ping            = sqlite_ping,
    .cancel          = sqlite_cancel,
    .load_schema     = sqlite_load_schema,
    .prepare         = sqlite_prepare,
    .bind_param      = sqlite_bind_param,
    .step            = sqlite_step,
    .column_count    = sqlite_column_count,
    .column_desc     = sqlite_column_desc,
    .column_value    = sqlite_column_value,
    .finalize        = sqlite_finalize,
    .error_msg       = sqlite_error_msg,
};

void rdb_sqlite_register(void) {
    rdb_register_driver(&sqlite_driver);
}

/* ══════════════════════════════════════════════════════════════════════
 * Scoped host transaction primitives (S12.4.4v2)
 * ══════════════════════════════════════════════════════════════════════ */

struct RdbSqliteTxn {
    RdbConn* conn;
    bool rollback_only;
    bool busy;
    bool preimage_available;
    bool statement_alter;
    bool stepping_alter;
};

/* SQL cannot change the connection's transaction or reach another database. */
static int sqlite_tx_authorize(void* user, int action, const char* arg1,
                               const char* arg2, const char* db, const char* source) {
    RdbSqliteTxn* tx = (RdbSqliteTxn*)user;
    (void)db; (void)source;
    if (action == SQLITE_ALTER_TABLE && tx) tx->statement_alter = true;
    // SQLite checks existing rows while stepping an ALTER on a STRICT table.
    if (action == SQLITE_PRAGMA && tx && tx->stepping_alter && arg1 && arg2 &&
            sqlite3_stricmp(arg1, "quick_check") == 0) return SQLITE_OK;
    // This read-only check lets callers validate all foreign keys in one snapshot.
    if (action == SQLITE_PRAGMA && arg1 && !arg2 &&
            sqlite3_stricmp(arg1, "foreign_key_check") == 0) return SQLITE_OK;
    if (action == SQLITE_ATTACH || action == SQLITE_DETACH || action == SQLITE_TRANSACTION ||
        action == SQLITE_SAVEPOINT || action == SQLITE_PRAGMA || action == SQLITE_CREATE_VTABLE ||
        action == SQLITE_DROP_VTABLE) return SQLITE_DENY;
    if (action == SQLITE_FUNCTION && arg2 && sqlite3_stricmp(arg2, "load_extension") == 0)
        return SQLITE_DENY;
    return SQLITE_OK;
}

static RdbSqliteTxn* sqlite_tx_wrap(Pool* pool, RdbConn* conn) {
    sqlite3* db = (sqlite3*)conn->handle;
    int enabled = 0;
    if (sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_FKEY, 1, &enabled) != SQLITE_OK ||
        !enabled || sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        rdb_close(conn);
        return NULL;
    }
    RdbSqliteTxn* tx = (RdbSqliteTxn*)pool_calloc(pool, sizeof(RdbSqliteTxn));
    if (!tx) {
        rdb_close(conn);
        return NULL;
    }
    tx->conn = conn;
    if (sqlite3_set_authorizer(db, sqlite_tx_authorize, tx) != SQLITE_OK) {
        rdb_close(conn);
        return NULL;
    }
    return tx;
}

static RdbSqliteTxn* sqlite_tx_begin(Pool* pool, const char* path, bool existing) {
    if (!pool || !path || !path[0]) return NULL;
    // URI interpretation can be enabled globally; an existing-file open must
    // not silently turn a URI into a transient in-memory database.
    if (existing && (strcmp(path, ":memory:") == 0 || strncmp(path, "file:", 5) == 0)) return NULL;
    rdb_sqlite_register();
    RdbConn* conn = existing ? rdb_open_sqlite_existing(pool, path) : rdb_open(pool, path, "sqlite", false);
    return conn ? sqlite_tx_wrap(pool, conn) : NULL;
}

RdbSqliteTxn* rdb_sqlite_tx_begin(Pool* pool, const char* path) {
    return sqlite_tx_begin(pool, path, false);
}

RdbSqliteTxn* rdb_sqlite_tx_begin_existing(Pool* pool, const char* path) {
    return sqlite_tx_begin(pool, path, true);
}

/* An explicit byte length preserves embedded NULs and distinguishes empty blobs. */
static int sqlite_tx_bind(sqlite3_stmt* stmt, int index, const RdbValue* value) {
    if (value->is_null || value->type == RDB_TYPE_NULL) return sqlite3_bind_null(stmt, index);
    switch (value->type) {
    case RDB_TYPE_INT: return sqlite3_bind_int64(stmt, index, value->int_val);
    case RDB_TYPE_BOOL: return sqlite3_bind_int(stmt, index, value->bool_val ? 1 : 0);
    case RDB_TYPE_FLOAT:
        return isfinite(value->float_val) ? sqlite3_bind_double(stmt, index, value->float_val) : SQLITE_MISMATCH;
    case RDB_TYPE_STRING:
        if (value->str_len < 0 || (!value->str_val && value->str_len)) return SQLITE_MISMATCH;
        return sqlite3_bind_text64(stmt, index, value->str_val ? value->str_val : "",
                                   (sqlite3_uint64)value->str_len, SQLITE_TRANSIENT, SQLITE_UTF8);
    case RDB_TYPE_BLOB:
        if (value->str_len < 0 || (!value->str_val && value->str_len)) return SQLITE_MISMATCH;
        if (!value->str_len) return sqlite3_bind_zeroblob(stmt, index, 0);
        return sqlite3_bind_blob64(stmt, index, value->str_val, (sqlite3_uint64)value->str_len, SQLITE_TRANSIENT);
    default: return SQLITE_MISMATCH;
    }
}

struct RdbSqliteWork {
    RdbSqliteTxn* tx;
    RdbConnLife* pin;
    sqlite3* db;
    int result;
    bool executed;
};

RdbSqliteWork* rdb_sqlite_tx_work_begin(RdbSqliteTxn* tx) {
    if (!tx || !tx->conn || rdb_ping(tx->conn) != RDB_OK) return NULL;
    if (tx->busy) { tx->rollback_only = true; return NULL; }
    RdbConnLife* pin = rdb_conn_life_hold(tx->conn);
    if (!pin) { tx->rollback_only = true; return NULL; }
    RdbSqliteWork* work = (RdbSqliteWork*)mem_calloc(1, sizeof(RdbSqliteWork), MEM_CAT_EVAL);
    if (!work) { tx->rollback_only = true; rdb_conn_life_release(pin); return NULL; }
    tx->busy = true;
    work->tx = tx;
    work->pin = pin;
    work->db = (sqlite3*)tx->conn->handle;
    work->result = RDB_ERROR;
    return work;
}

static int sqlite_tx_progress(void* data) {
    const int* cancelled = (const int*)data;
    return __atomic_load_n(cancelled, __ATOMIC_ACQUIRE) != 0;
}

struct RdbSqliteClone {
    Pool* pool;
    RdbConn* source;
    RdbConn* destination;
    RdbConnLife* source_pin;
    RdbConnLife* destination_pin;
    sqlite3* source_db;
    sqlite3* destination_db;
    size_t limit;
    bool ran;
    bool ok;
};

void rdb_sqlite_clone_discard(RdbSqliteClone* clone) {
    if (!clone) return;
    if (clone->source) rdb_close(clone->source);
    if (clone->destination) rdb_close(clone->destination);
    if (clone->source_pin) rdb_conn_life_release(clone->source_pin);
    if (clone->destination_pin) rdb_conn_life_release(clone->destination_pin);
    mem_free(clone);
}

RdbSqliteClone* rdb_sqlite_clone_prepare(Pool* pool, const char* source, size_t limit) {
    if (!pool || !source || !*source || !limit || !strcmp(source, ":memory:")) return NULL;
    rdb_sqlite_register();
    RdbSqliteClone* clone = (RdbSqliteClone*)mem_calloc(1, sizeof(RdbSqliteClone), MEM_CAT_EVAL);
    if (!clone) return NULL;
    clone->pool = pool;
    clone->limit = limit;
    clone->source = rdb_open(pool, source, "sqlite", true);
    clone->source_pin = clone->source ? rdb_conn_life_hold(clone->source) : NULL;
    clone->destination = clone->source_pin ? rdb_open(pool, ":memory:", "sqlite", false) : NULL;
    clone->destination_pin = clone->destination ? rdb_conn_life_hold(clone->destination) : NULL;
    if (!clone->source_pin || !clone->destination_pin || rdb_ping(clone->source) != RDB_OK ||
            rdb_ping(clone->destination) != RDB_OK) {
        rdb_sqlite_clone_discard(clone);
        return NULL;
    }
    clone->source_db = (sqlite3*)clone->source->handle;
    clone->destination_db = (sqlite3*)clone->destination->handle;
    return clone;
}

static bool sqlite_clone_scalar(sqlite3* db, const char* sql, sqlite3_int64* out) {
    sqlite3_stmt* stmt = NULL;
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) == SQLITE_INTEGER;
    if (ok) *out = sqlite3_column_int64(stmt, 0);
    if (stmt && sqlite3_finalize(stmt) != SQLITE_OK) ok = false;
    return ok;
}

static bool sqlite_clone_integrity(sqlite3* db) {
    sqlite3_stmt* stmt = NULL;
    bool ok = sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) == SQLITE_TEXT;
    const unsigned char* text = ok ? sqlite3_column_text(stmt, 0) : NULL;
    ok = ok && text && !strcmp((const char*)text, "ok") && sqlite3_step(stmt) == SQLITE_DONE;
    if (stmt && sqlite3_finalize(stmt) != SQLITE_OK) ok = false;
    if (!ok) return false;
    stmt = NULL;
    ok = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_DONE;
    if (stmt && sqlite3_finalize(stmt) != SQLITE_OK) ok = false;
    return ok;
}

void rdb_sqlite_clone_run(RdbSqliteClone* clone, const int* cancelled) {
    if (!clone || clone->ran) return;
    clone->ran = true;
    sqlite3* source = clone->source_db;
    sqlite3* destination = clone->destination_db;
    if (cancelled) {
        sqlite3_progress_handler(source, 1000, sqlite_tx_progress, (void*)cancelled);
        sqlite3_progress_handler(destination, 1000, sqlite_tx_progress, (void*)cancelled);
    }
    bool ok = (!cancelled || !__atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) &&
        sqlite3_exec(source, "BEGIN", NULL, NULL, NULL) == SQLITE_OK;
    sqlite3_int64 schema_count = 0, pages = 0, page_size = 0;
    // A schema read pins the source snapshot before sizing and copying it.
    if (ok) ok = sqlite_clone_scalar(source, "SELECT COUNT(*) FROM sqlite_schema", &schema_count) &&
        sqlite_clone_scalar(source, "PRAGMA page_count", &pages) &&
        sqlite_clone_scalar(source, "PRAGMA page_size", &page_size) &&
        pages > 0 && page_size > 0 && (uint64_t)pages <= clone->limit / (uint64_t)page_size;
    sqlite3_backup* backup = ok ? sqlite3_backup_init(destination, "main", source, "main") : NULL;
    if (ok && !backup) ok = false;
    if (backup) {
        int status = SQLITE_OK;
        while (status == SQLITE_OK && (!cancelled || !__atomic_load_n(cancelled, __ATOMIC_ACQUIRE)))
            status = sqlite3_backup_step(backup, 64);
        int finished = sqlite3_backup_finish(backup);
        ok = status == SQLITE_DONE && finished == SQLITE_OK &&
            (!cancelled || !__atomic_load_n(cancelled, __ATOMIC_ACQUIRE));
    }
    if (sqlite3_exec(source, "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) ok = false;
    if (ok) ok = sqlite_clone_integrity(destination);
    if (cancelled) {
        sqlite3_progress_handler(source, 0, NULL, NULL);
        sqlite3_progress_handler(destination, 0, NULL, NULL);
        if (__atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) ok = false;
    }
    clone->ok = ok;
}

RdbSqliteTxn* rdb_sqlite_clone_finish(RdbSqliteClone* clone) {
    if (!clone || !clone->ran || !clone->ok || rdb_ping(clone->source) != RDB_OK ||
            rdb_ping(clone->destination) != RDB_OK) {
        rdb_sqlite_clone_discard(clone);
        return NULL;
    }
    rdb_close(clone->source);
    clone->source = NULL;
    rdb_conn_life_release(clone->source_pin);
    clone->source_pin = NULL;
    if (rdb_ping(clone->destination) != RDB_OK) {
        rdb_sqlite_clone_discard(clone);
        return NULL;
    }
    RdbSqliteTxn* tx = sqlite_tx_wrap(clone->pool, clone->destination);
    if (tx) tx->preimage_available = true;
    clone->destination = NULL; // wrapping owns the connection on success and failure
    rdb_conn_life_release(clone->destination_pin);
    clone->destination_pin = NULL;
    mem_free(clone);
    return tx;
}

static int sqlite_tx_execute_core(RdbSqliteWork* work, const char* sql, size_t sql_len,
        const RdbValue* params, int param_count, RdbSqliteRowFn row_fn, void* user,
        const int* cancelled, bool check_live) {
    RdbSqliteTxn* tx = work->tx;
    sqlite3* db = work->db;
    if (!sql || !sql_len || sql_len > INT_MAX || memchr(sql, '\0', sql_len) ||
            param_count < 0 || (param_count && !params)) return RDB_ERROR;
    if (cancelled) {
        if (__atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) return RDB_ERROR;
        sqlite3_progress_handler(db, 1000, sqlite_tx_progress, (void*)cancelled);
    }
    sqlite3_stmt* stmt = NULL;
    sqlite3_stmt* extra = NULL;
    const char* tail = NULL;
    RdbValue* values = NULL;
    const char** names = NULL;
    tx->statement_alter = false;
    int rc = sqlite3_prepare_v3(db, sql, (int)sql_len, 0, &stmt, &tail);
    if (rc != SQLITE_OK || !stmt || !tail) goto fail;
    /* Preparing the tail catches a second statement without executing either. */
    rc = sqlite3_prepare_v3(db, tail, (int)(sql + sql_len - tail), 0, &extra, NULL);
    if (rc != SQLITE_OK || extra) goto fail;
    // A pre-migration backup must precede every statement that can write.
    if (!sqlite3_stmt_readonly(stmt)) tx->preimage_available = false;
    if (sqlite3_bind_parameter_count(stmt) != param_count) goto fail;
    for (int i = 0; i < param_count; i++) {
        if (sqlite_tx_bind(stmt, i + 1, &params[i]) != SQLITE_OK) goto fail;
    }
    int count = sqlite3_column_count(stmt);
    if (count && row_fn) {
        values = (RdbValue*)mem_calloc((size_t)count, sizeof(RdbValue), MEM_CAT_EVAL);
        names = (const char**)mem_calloc((size_t)count, sizeof(const char*), MEM_CAT_EVAL);
        if (!values || !names) goto fail;
        for (int i = 0; i < count; i++) {
            names[i] = sqlite3_column_name(stmt, i);
            if (!names[i]) goto fail;
        }
    }
    tx->stepping_alter = tx->statement_alter;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // SQLite can expose result metadata for DDL that returns no rows.
        if (!row_fn) goto fail;
        for (int i = 0; i < count; i++) {
            values[i] = sqlite_column_value(stmt, i);
            if (values[i].type == RDB_TYPE_STRING && !values[i].str_val) goto fail;
            if (sqlite3_column_type(stmt, i) == SQLITE_BLOB) {
                values[i].type = RDB_TYPE_BLOB;
                values[i].is_null = false;
                values[i].str_val = (const char*)sqlite3_column_blob(stmt, i);
                values[i].str_len = sqlite3_column_bytes(stmt, i);
            }
            if (values[i].type == RDB_TYPE_FLOAT && !isfinite(values[i].float_val)) goto fail;
        }
        if (!row_fn(user, count, names, values) ||
                (check_live && rdb_ping(tx->conn) != RDB_OK)) goto fail;
    }
    tx->stepping_alter = false;
    if (rc != SQLITE_DONE) goto fail;
    rc = sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_OK) goto fail;
    mem_free(values);
    mem_free(names);
    if (cancelled) sqlite3_progress_handler(db, 0, NULL, NULL);
    return RDB_OK;
fail:
    tx->stepping_alter = false;
    if (extra) sqlite3_finalize(extra);
    if (stmt) sqlite3_finalize(stmt);
    mem_free(values);
    mem_free(names);
    if (cancelled) sqlite3_progress_handler(db, 0, NULL, NULL);
    return RDB_ERROR;
}

int rdb_sqlite_tx_work_execute(RdbSqliteWork* work, const char* sql, size_t sql_len,
        const RdbValue* params, int param_count, RdbSqliteRowFn row_fn, void* user,
        const int* cancelled) {
    if (!work || work->executed) return RDB_ERROR;
    work->executed = true;
    // Multiple workers/connections require a mutex-enabled SQLite build.
    if (!sqlite3_threadsafe()) return RDB_ERROR;
    work->result = sqlite_tx_execute_core(work, sql, sql_len, params, param_count,
        row_fn, user, cancelled, false);
    return work->result;
}

int rdb_sqlite_tx_work_end(RdbSqliteWork* work) {
    if (!work) return RDB_ERROR;
    RdbSqliteTxn* tx = work->tx;
    int result = work->result;
    if (!work->executed || rdb_ping(tx->conn) != RDB_OK) result = RDB_ERROR;
    if (result != RDB_OK) tx->rollback_only = true;
    tx->busy = false;
    rdb_conn_life_release(work->pin);
    mem_free(work);
    return result;
}

int rdb_sqlite_tx_execute(RdbSqliteTxn* tx, const char* sql, size_t sql_len,
                          const RdbValue* params, int param_count,
                          RdbSqliteRowFn row_fn, void* user) {
    RdbSqliteWork* work = rdb_sqlite_tx_work_begin(tx);
    if (!work) return RDB_ERROR;
    work->executed = true;
    work->result = sqlite_tx_execute_core(work, sql, sql_len, params, param_count,
        row_fn, user, NULL, true);
    return rdb_sqlite_tx_work_end(work);
}

void rdb_sqlite_tx_mark_failed(RdbSqliteTxn* tx) {
    if (tx) tx->rollback_only = true;
}

bool rdb_sqlite_tx_live(RdbSqliteTxn* tx) {
    return tx && tx->conn && rdb_ping(tx->conn) == RDB_OK;
}

static unsigned char* sqlite_tx_serialize(sqlite3* db, size_t limit, size_t* output_size) {
    sqlite3_int64 size = 0;
    // NOCOPY reports the required size even if no contiguous image exists.
    (void)sqlite3_serialize(db, "main", &size, SQLITE_SERIALIZE_NOCOPY);
    if (size <= 0 || (uint64_t)size > limit) return NULL;
    sqlite3_int64 copied = 0;
    unsigned char* bytes = sqlite3_serialize(db, "main", &copied, 0);
    if (!bytes || copied != size) { sqlite3_free(bytes); return NULL; }
    *output_size = (size_t)copied;
    return bytes;
}

int rdb_sqlite_tx_preimage(RdbSqliteTxn* tx, size_t limit, RdbSqliteSnapshotFn publish, void* user) {
    if (!tx) return RDB_ERROR;
    if (!tx->conn || tx->busy || tx->rollback_only || !tx->preimage_available || !limit || !publish ||
            rdb_ping(tx->conn) != RDB_OK) {
        tx->rollback_only = true;
        return RDB_ERROR;
    }
    tx->preimage_available = false;
    RdbConnLife* hold = rdb_conn_life_hold(tx->conn);
    if (!hold) { tx->rollback_only = true; return RDB_ERROR; }
    // Prevent a host publication callback from reentering this transaction.
    tx->busy = true;
    sqlite3* db = (sqlite3*)tx->conn->handle;
    size_t size = 0;
    // SQLite serialization uses an internal pragma; restore the guard before callbacks.
    int rc = sqlite3_set_authorizer(db, NULL, NULL);
    unsigned char* bytes = rc == SQLITE_OK ? sqlite_tx_serialize(db, limit, &size) : NULL;
    rc = sqlite3_set_authorizer(db, sqlite_tx_authorize, tx);
    if (rc != SQLITE_OK) {
        // Never leave an unguarded connection usable after a failed restore.
        sqlite3_free(bytes);
        tx->rollback_only = true;
        tx->busy = false;
        rdb_close(tx->conn);
        rdb_conn_life_release(hold);
        return RDB_ERROR;
    }
    int result = bytes && rc == SQLITE_OK ? publish(user, bytes, size) : RDB_ERROR;
    sqlite3_free(bytes);
    if (rdb_ping(tx->conn) != RDB_OK || tx->rollback_only) result = result >= RDB_OK ? 1 : RDB_ERROR;
    tx->busy = false;
    rdb_conn_life_release(hold);
    if (result != RDB_OK) tx->rollback_only = true;
    return result;
}

static int sqlite_tx_finish(RdbSqliteTxn* tx, bool unwinding, size_t limit,
                            RdbSqliteSnapshotFn publish, void* user) {
    if (!tx || !tx->conn || tx->busy) return RDB_ERROR;
    if (rdb_ping(tx->conn) != RDB_OK) {
        // Registry teardown may have already closed the driver handle.
        rdb_close(tx->conn);
        tx->conn = NULL;
        return RDB_ERROR;
    }
    // Publication can invoke host authorization, including registry teardown.
    RdbConnLife* hold = publish ? rdb_conn_life_hold(tx->conn) : NULL;
    if (publish && !hold) {
        rdb_close(tx->conn);
        tx->conn = NULL;
        return RDB_ERROR;
    }
    sqlite3* db = (sqlite3*)tx->conn->handle;
    bool rollback = unwinding || tx->rollback_only;
    if (publish && tx->conn->info.peer.kind != RDB_PEER_MEMORY) rollback = true;
    /* Remove the SQL authorizer only for the host's own final transaction command. */
    int rc = sqlite3_set_authorizer(db, NULL, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_exec(db, rollback ? "ROLLBACK" : "COMMIT", NULL, NULL, NULL);
    if (rc != SQLITE_OK && !rollback) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    int published = RDB_OK;
    if (rc == SQLITE_OK && !rollback && publish) {
        size_t size = 0;
        unsigned char* bytes = sqlite_tx_serialize(db, limit, &size);
        published = bytes ? publish(user, bytes, size) : RDB_ERROR;
        sqlite3_free(bytes);
    }
    if (hold && rdb_ping(tx->conn) != RDB_OK)
        published = !rollback && rc == SQLITE_OK && published >= RDB_OK ? 1 : RDB_ERROR;
    rdb_close(tx->conn);
    tx->conn = NULL;
    if (hold) rdb_conn_life_release(hold);
    if (rc != SQLITE_OK || (tx->rollback_only && !unwinding) ||
            (rollback && !unwinding && !tx->rollback_only)) return RDB_ERROR;
    return published;
}

int rdb_sqlite_tx_finish(RdbSqliteTxn* tx, bool unwinding) {
    return sqlite_tx_finish(tx, unwinding, 0, NULL, NULL);
}

int rdb_sqlite_tx_finish_snapshot(RdbSqliteTxn* tx, bool unwinding, size_t limit,
                                   RdbSqliteSnapshotFn publish, void* user) {
    if (!publish || !limit) { (void)sqlite_tx_finish(tx, true, 0, NULL, NULL); return RDB_ERROR; }
    return sqlite_tx_finish(tx, unwinding, limit, publish, user);
}

int rdb_sqlite_execute_once(const char* path, const char* sql, size_t sql_len,
                            const RdbValue* params, int param_count,
                            RdbSqliteRowFn row_fn, void* user) {
    /* Each autocommit has a bounded lifetime, including failed opens. */
    Pool* pool = pool_create();
    if (!pool) return RDB_ERROR;
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, path);
    if (!tx) {
        pool_destroy(pool);
        return RDB_ERROR;
    }
    int result = rdb_sqlite_tx_execute(tx, sql, sql_len, params, param_count, row_fn, user);
    int finish = rdb_sqlite_tx_finish(tx, result != RDB_OK);
    pool_destroy(pool);
    return result == RDB_OK && finish == RDB_OK ? RDB_OK : RDB_ERROR;
}
