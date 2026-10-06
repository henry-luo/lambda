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
#include "sqlite/sqlite3.h"
#include "log.h"
#include "strbuf.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════
 * Connection
 * ══════════════════════════════════════════════════════════════════════ */

static const RdbDialect sqlite_dialect = { RDB_PLACEHOLDER_QNUM, '"' };

/** a SQLite database is a file, or memory for ":memory:" and "" */
static void sqlite_target(const char* uri, RdbTarget* out) {
    memset(out, 0, sizeof(*out));
    if (!uri[0] || strcmp(uri, ":memory:") == 0) {
        out->kind = RDB_PEER_MEMORY;
        return;
    }
    out->kind = RDB_PEER_FILE;
    snprintf(out->path, sizeof(out->path), "%s", uri);
}

static int sqlite_resolve_targets(const char* uri, RdbTarget* out, int cap, int* out_count) {
    if (!uri || cap < 1) return RDB_ERROR;
    sqlite_target(uri, &out[0]);
    *out_count = 1;
    return RDB_OK;
}

static int sqlite_open(const RdbHostAPI* host, void* open_ctx, const char* uri,
                       const RdbOpenOptions* opts, void** out_conn) {
    int flags = opts->readonly
        ? SQLITE_OPEN_READONLY
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
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
    sqlite_target(uri, &info.peer);
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
