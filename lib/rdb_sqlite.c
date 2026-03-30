/**
 * @file rdb_sqlite.c
 * @brief SQLite backend driver for the generic RDB API.
 *
 * Implements the RdbDriver vtable using the vendored SQLite amalgamation.
 * Schema introspection uses PRAGMA queries; queries use the standard
 * sqlite3_prepare_v2 / sqlite3_step / sqlite3_column_* API.
 */

#include "rdb.h"
#include "sqlite3.h"
#include "log.h"
#include "strbuf.h"
#include <string.h>
#include <ctype.h>

/* ══════════════════════════════════════════════════════════════════════
 * Internal statement wrapper — embeds back-pointer to RdbConn (first
 * field) so the generic rdb.c layer can reach the driver vtable.
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    RdbConn*       conn;    /* MUST be first — generic layer reads this */
    sqlite3_stmt*  stmt;
} SqliteStmt;

/* ══════════════════════════════════════════════════════════════════════
 * Connection
 * ══════════════════════════════════════════════════════════════════════ */

static int sqlite_open(RdbConn* conn, const char* uri, bool readonly) {
    int flags = readonly
        ? SQLITE_OPEN_READONLY
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(uri, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: failed to open '%s': %s", uri, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return RDB_ERROR;
    }
    conn->handle = db;
    log_debug("rdb sqlite: opened '%s' (readonly=%d)", uri, readonly);
    return RDB_OK;
}

static void sqlite_close(RdbConn* conn) {
    if (conn->handle) {
        sqlite3_close((sqlite3*)conn->handle);
        conn->handle = NULL;
    }
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
static int sqlite_load_columns(sqlite3* db, Pool* pool, RdbTable* tbl) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "PRAGMA table_info(\"");
    strbuf_append_str(sb, tbl->name);
    strbuf_append_str(sb, "\")");

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

    tbl->columns = (RdbColumn*)pool_calloc(pool, (size_t)col_count * sizeof(RdbColumn));
    tbl->column_count = col_count;

    // second pass: populate
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < col_count) {
        // PRAGMA table_info: cid, name, type, notnull, dflt_value, pk
        const char* name     = (const char*)sqlite3_column_text(stmt, 1);
        const char* type_str = (const char*)sqlite3_column_text(stmt, 2);
        int notnull          = sqlite3_column_int(stmt, 3);
        int pk               = sqlite3_column_int(stmt, 5);

        tbl->columns[i].name        = pool_strdup(pool, name ? name : "");
        tbl->columns[i].type_decl   = pool_strdup(pool, type_str ? type_str : "");
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
static int sqlite_load_indexes(sqlite3* db, Pool* pool, RdbTable* tbl) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "PRAGMA index_list(\"");
    strbuf_append_str(sb, tbl->name);
    strbuf_append_str(sb, "\")");

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

    tbl->indexes = (RdbIndex*)pool_calloc(pool, (size_t)idx_count * sizeof(RdbIndex));
    tbl->index_count = idx_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < idx_count) {
        // PRAGMA index_list: seq, name, unique, origin, partial
        const char* idx_name = (const char*)sqlite3_column_text(stmt, 1);
        int is_unique        = sqlite3_column_int(stmt, 2);

        tbl->indexes[i].name   = pool_strdup(pool, idx_name ? idx_name : "");
        tbl->indexes[i].unique = (is_unique != 0);

        // load columns for this index
        StrBuf* sb2 = strbuf_new();
        strbuf_append_str(sb2, "PRAGMA index_info(\"");
        strbuf_append_str(sb2, idx_name ? idx_name : "");
        strbuf_append_str(sb2, "\")");

        sqlite3_stmt* col_stmt = NULL;
        if (sqlite3_prepare_v2(db, sb2->str, -1, &col_stmt, NULL) == SQLITE_OK) {
            // count columns in this index
            int col_count = 0;
            while (sqlite3_step(col_stmt) == SQLITE_ROW) col_count++;
            sqlite3_reset(col_stmt);

            if (col_count > 0) {
                tbl->indexes[i].columns = (const char**)pool_calloc(pool, (size_t)col_count * sizeof(const char*));
                tbl->indexes[i].column_count = col_count;
                int j = 0;
                while (sqlite3_step(col_stmt) == SQLITE_ROW && j < col_count) {
                    // PRAGMA index_info: seqno, cid, name
                    const char* col_name = (const char*)sqlite3_column_text(col_stmt, 2);
                    tbl->indexes[i].columns[j] = pool_strdup(pool, col_name ? col_name : "");
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
static int sqlite_load_foreign_keys(sqlite3* db, Pool* pool, RdbTable* tbl) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "PRAGMA foreign_key_list(\"");
    strbuf_append_str(sb, tbl->name);
    strbuf_append_str(sb, "\")");

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

    tbl->foreign_keys = (RdbForeignKey*)pool_calloc(pool, (size_t)fk_count * sizeof(RdbForeignKey));
    tbl->fk_count = fk_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < fk_count) {
        // PRAGMA foreign_key_list: id, seq, table, from, to, on_update, on_delete, match
        const char* ref_table  = (const char*)sqlite3_column_text(stmt, 2);
        const char* from_col   = (const char*)sqlite3_column_text(stmt, 3);
        const char* to_col     = (const char*)sqlite3_column_text(stmt, 4);

        tbl->foreign_keys[i].column    = pool_strdup(pool, from_col ? from_col : "");
        tbl->foreign_keys[i].ref_table = pool_strdup(pool, ref_table ? ref_table : "");
        tbl->foreign_keys[i].ref_column = pool_strdup(pool, to_col ? to_col : "");

        // derive link_name: strip _id suffix from column name
        const char* col_name = from_col ? from_col : "";
        size_t col_len = strlen(col_name);
        if (col_len > 3 && strcmp(col_name + col_len - 3, "_id") == 0) {
            char* link = (char*)pool_alloc(pool, col_len - 3 + 1);
            memcpy(link, col_name, col_len - 3);
            link[col_len - 3] = '\0';
            tbl->foreign_keys[i].link_name = link;
        } else {
            // use ref_table name as fallback
            tbl->foreign_keys[i].link_name = pool_strdup(pool, ref_table ? ref_table : "");
        }
        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

/**
 * Populate reverse_fks for all tables by scanning forward FKs.
 * Must be called after all tables have their foreign_keys populated.
 */
static void sqlite_build_reverse_fks(Pool* pool, RdbSchema* schema) {
    // first pass: count reverse FKs per table
    for (int t = 0; t < schema->table_count; t++) {
        RdbTable* tbl = &schema->tables[t];
        for (int f = 0; f < tbl->fk_count; f++) {
            RdbTable* ref = NULL;
            for (int r = 0; r < schema->table_count; r++) {
                if (strcmp(schema->tables[r].name, tbl->foreign_keys[f].ref_table) == 0) {
                    ref = &schema->tables[r];
                    break;
                }
            }
            if (ref) ref->reverse_fk_count++;
        }
    }

    // allocate reverse FK arrays
    for (int t = 0; t < schema->table_count; t++) {
        RdbTable* tbl = &schema->tables[t];
        if (tbl->reverse_fk_count > 0) {
            tbl->reverse_fks = (RdbForeignKey*)pool_calloc(pool,
                (size_t)tbl->reverse_fk_count * sizeof(RdbForeignKey));
            tbl->reverse_fk_count = 0; // reset for filling
        }
    }

    // second pass: fill reverse FKs
    for (int t = 0; t < schema->table_count; t++) {
        RdbTable* tbl = &schema->tables[t];
        for (int f = 0; f < tbl->fk_count; f++) {
            RdbForeignKey* fk = &tbl->foreign_keys[f];
            RdbTable* ref = NULL;
            for (int r = 0; r < schema->table_count; r++) {
                if (strcmp(schema->tables[r].name, fk->ref_table) == 0) {
                    ref = &schema->tables[r];
                    break;
                }
            }
            if (ref) {
                int idx = ref->reverse_fk_count;
                ref->reverse_fks[idx].column    = pool_strdup(pool, fk->ref_column);
                ref->reverse_fks[idx].ref_table = pool_strdup(pool, tbl->name);
                ref->reverse_fks[idx].ref_column = pool_strdup(pool, fk->column);
                ref->reverse_fks[idx].link_name = pool_strdup(pool, tbl->name);
                ref->reverse_fk_count++;
            }
        }
    }
}

/**
 * Parse trigger timing from CREATE TRIGGER DDL text.
 * Returns a pool-owned string "BEFORE", "AFTER", or "INSTEAD OF".
 */
static const char* trigger_parse_timing(const char* sql, Pool* pool) {
    if (!sql) return pool_strdup(pool, "AFTER");
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
            return pool_strdup(pool, "INSTEAD OF");
        }
        // check BEFORE (length 6)
        if ((p[0]=='B'||p[0]=='b') && (p[1]=='E'||p[1]=='e') &&
            (p[2]=='F'||p[2]=='f') && (p[3]=='O'||p[3]=='o') &&
            (p[4]=='R'||p[4]=='r') && (p[5]=='E'||p[5]=='e')) {
            return pool_strdup(pool, "BEFORE");
        }
        // check AFTER (length 5)
        if ((p[0]=='A'||p[0]=='a') && (p[1]=='F'||p[1]=='f') &&
            (p[2]=='T'||p[2]=='t') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='R'||p[4]=='r')) {
            return pool_strdup(pool, "AFTER");
        }
        p++;
    }
    (void)upper;
    return pool_strdup(pool, "AFTER");
}

/**
 * Parse trigger event from CREATE TRIGGER DDL text.
 * Returns a pool-owned string "INSERT", "UPDATE", or "DELETE".
 */
static const char* trigger_parse_event(const char* sql, Pool* pool) {
    if (!sql) return pool_strdup(pool, "INSERT");
    const char* p = sql;
    while (*p) {
        if ((p[0]=='I'||p[0]=='i') && (p[1]=='N'||p[1]=='n') &&
            (p[2]=='S'||p[2]=='s') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='R'||p[4]=='r') && (p[5]=='T'||p[5]=='t')) {
            return pool_strdup(pool, "INSERT");
        }
        if ((p[0]=='D'||p[0]=='d') && (p[1]=='E'||p[1]=='e') &&
            (p[2]=='L'||p[2]=='l') && (p[3]=='E'||p[3]=='e') &&
            (p[4]=='T'||p[4]=='t') && (p[5]=='E'||p[5]=='e')) {
            return pool_strdup(pool, "DELETE");
        }
        if ((p[0]=='U'||p[0]=='u') && (p[1]=='P'||p[1]=='p') &&
            (p[2]=='D'||p[2]=='d') && (p[3]=='A'||p[3]=='a') &&
            (p[4]=='T'||p[4]=='t') && (p[5]=='E'||p[5]=='e')) {
            return pool_strdup(pool, "UPDATE");
        }
        p++;
    }
    return pool_strdup(pool, "INSERT");
}

/**
 * Load trigger metadata for a table from sqlite_master.
 */
static int sqlite_load_triggers(sqlite3* db, Pool* pool, RdbTable* tbl) {
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

    tbl->triggers = (RdbTrigger*)pool_calloc(pool, (size_t)trig_count * sizeof(RdbTrigger));
    tbl->trigger_count = trig_count;

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < trig_count) {
        const char* name     = (const char*)sqlite3_column_text(stmt, 0);
        const char* ddl_sql  = (const char*)sqlite3_column_text(stmt, 1);

        tbl->triggers[i].name   = pool_strdup(pool, name ? name : "");
        tbl->triggers[i].timing = trigger_parse_timing(ddl_sql, pool);
        tbl->triggers[i].event  = trigger_parse_event(ddl_sql, pool);
        i++;
    }
    sqlite3_finalize(stmt);
    return RDB_OK;
}

static int sqlite_load_schema(RdbConn* conn, RdbSchema* out_schema) {
    sqlite3* db = (sqlite3*)conn->handle;
    Pool* pool = conn->pool;

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

    out_schema->tables = (RdbTable*)pool_calloc(pool, (size_t)table_count * sizeof(RdbTable));
    out_schema->table_count = table_count;

    int t = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && t < table_count) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        const char* type = (const char*)sqlite3_column_text(stmt, 1);

        out_schema->tables[t].name    = pool_strdup(pool, name ? name : "");
        out_schema->tables[t].is_view = (type && strcmp(type, "view") == 0);

        sqlite_load_columns(db, pool, &out_schema->tables[t]);
        sqlite_load_indexes(db, pool, &out_schema->tables[t]);
        sqlite_load_foreign_keys(db, pool, &out_schema->tables[t]);
        sqlite_load_triggers(db, pool, &out_schema->tables[t]);

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

    // build reverse FK cross-references
    sqlite_build_reverse_fks(pool, out_schema);

    log_debug("rdb sqlite: loaded schema with %d tables/views", out_schema->table_count);
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Query execution
 * ══════════════════════════════════════════════════════════════════════ */

static int sqlite_prepare(RdbConn* conn, const char* sql, RdbStmt** out_stmt) {
    sqlite3* db = (sqlite3*)conn->handle;
    sqlite3_stmt* raw = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &raw, NULL);
    if (rc != SQLITE_OK) {
        log_error("rdb sqlite: prepare failed: %s", sqlite3_errmsg(db));
        return RDB_ERROR;
    }
    SqliteStmt* s = (SqliteStmt*)pool_calloc(conn->pool, sizeof(SqliteStmt));
    s->conn = conn;
    s->stmt = raw;
    *out_stmt = (RdbStmt*)s;
    return RDB_OK;
}

static int sqlite_bind_param(RdbStmt* stmt, int index, const RdbParam* param) {
    sqlite3_stmt* raw = ((SqliteStmt*)stmt)->stmt;
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

static int sqlite_step(RdbStmt* stmt) {
    int rc = sqlite3_step(((SqliteStmt*)stmt)->stmt);
    if (rc == SQLITE_ROW)  return RDB_ROW;
    if (rc == SQLITE_DONE) return RDB_DONE;
    log_error("rdb sqlite: step error: %s",
              sqlite3_errmsg(sqlite3_db_handle(((SqliteStmt*)stmt)->stmt)));
    return RDB_ERROR;
}

static int sqlite_column_count(RdbStmt* stmt) {
    return sqlite3_column_count(((SqliteStmt*)stmt)->stmt);
}

static RdbValue sqlite_column_value(RdbStmt* stmt, int col_index) {
    sqlite3_stmt* raw = ((SqliteStmt*)stmt)->stmt;
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

static void sqlite_finalize(RdbStmt* stmt) {
    SqliteStmt* s = (SqliteStmt*)stmt;
    if (s && s->stmt) {
        sqlite3_finalize(s->stmt);
        s->stmt = NULL;
    }
    // SqliteStmt itself is pool-allocated — freed when pool is destroyed
}

static int64_t sqlite_row_count(RdbConn* conn, const char* table_name) {
    // validate table_name against schema to prevent injection
    if (!rdb_get_table(conn, table_name)) {
        log_error("rdb sqlite: row_count for unknown table '%s'", table_name);
        return -1;
    }

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "SELECT COUNT(*) FROM \"");
    strbuf_append_str(sb, table_name);
    strbuf_append_str(sb, "\"");

    sqlite3_stmt* stmt = NULL;
    int64_t count = -1;
    if (sqlite3_prepare_v2((sqlite3*)conn->handle, sb->str, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    strbuf_free(sb);
    return count;
}

static const char* sqlite_error_msg(RdbConn* conn) {
    if (!conn->handle) return "rdb sqlite: no connection";
    return sqlite3_errmsg((sqlite3*)conn->handle);
}

/* ══════════════════════════════════════════════════════════════════════
 * Driver vtable + registration
 * ══════════════════════════════════════════════════════════════════════ */

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
