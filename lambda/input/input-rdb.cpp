/**
 * @file input-rdb.cpp
 * @brief Lambda input plugin for relational databases.
 *
 * Bridges the generic lib/rdb.h API → Lambda data structures via
 * MarkBuilder.  This file is entirely database-agnostic — it never
 * references SQLite, PostgreSQL, or any other backend directly.
 *
 * Entry point: input_rdb_from_path() — called from input.cpp when the
 * type is "sqlite" (or another registered RDB driver) or the file
 * extension matches a known database format.
 */

#include "input.hpp"
#include "input-parsers.h"
#include "../mark_builder.hpp"
#include "../../lib/rdb.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/file.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════
 * One-time driver bootstrap
 * ═══════════════════════════════════════════════════════════════════════ */

static bool rdb_drivers_registered = false;

static void rdb_ensure_drivers(void) {
    if (rdb_drivers_registered) return;
    rdb_drivers_registered = true;
    rdb_sqlite_register();
    // future: rdb_pg_register(), rdb_duckdb_register(), etc.
}

/* ═══════════════════════════════════════════════════════════════════════
 * Value conversion: RdbValue → Lambda Item
 * ═══════════════════════════════════════════════════════════════════════ */

static Item rdb_value_to_item(MarkBuilder& builder, RdbValue val, RdbType declared_type) {
    if (val.is_null) return ItemNull;

    switch (declared_type) {
        case RDB_TYPE_INT:
            return builder.createInt(val.int_val);

        case RDB_TYPE_FLOAT:
            return builder.createFloat(val.float_val);

        case RDB_TYPE_BOOL:
            return builder.createBool(val.int_val != 0);

        case RDB_TYPE_STRING:
            return builder.createStringItem(val.str_val);

        case RDB_TYPE_DATETIME:
            // datetime strings are stored as ISO-8601 text in SQLite;
            // for now, return as string — future: parse_datetime_string()
            return builder.createStringItem(val.str_val);

        case RDB_TYPE_JSON:
            // auto-parse JSON column content via existing JSON parser
            if (val.str_val && val.str_len > 0) {
                return parse_json_to_item(builder.input(), val.str_val);
            }
            return ItemNull;

        case RDB_TYPE_DECIMAL:
            // decimal strings — for now, return as string
            return builder.createStringItem(val.str_val);

        case RDB_TYPE_BLOB:
            // deferred to Phase 2
            return ItemNull;

        default:
            // fallback: treat as string
            if (val.type == RDB_TYPE_STRING && val.str_val) {
                return builder.createStringItem(val.str_val);
            }
            return ItemNull;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Fetch all rows from a table, producing an array of maps
 * ═══════════════════════════════════════════════════════════════════════ */

static Item rdb_fetch_table(MarkBuilder& builder, RdbConn* conn, RdbTable* tbl) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "SELECT * FROM \"");
    strbuf_append_str(sb, tbl->name);
    strbuf_append_str(sb, "\"");

    RdbStmt* stmt = rdb_prepare(conn, sb->str);
    strbuf_free(sb);

    if (!stmt) {
        log_error("rdb input: failed to prepare SELECT for table '%s'", tbl->name);
        return builder.createArray();
    }

    ArrayBuilder rows = builder.array();
    int rc;
    while ((rc = rdb_step(stmt)) == RDB_ROW) {
        MapBuilder row = builder.map();
        for (int c = 0; c < tbl->column_count; c++) {
            RdbValue val = rdb_column_value(stmt, c);
            Item item = rdb_value_to_item(builder, val, tbl->columns[c].type);
            row.put(tbl->columns[c].name, item);
        }
        rows.append(row.final());
    }

    rdb_finalize(stmt);
    return rows.final();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Build schema metadata map for a single table
 * ═══════════════════════════════════════════════════════════════════════ */

static Item rdb_build_table_schema(MarkBuilder& builder, RdbTable* tbl) {
    MapBuilder tbl_schema = builder.map();

    if (tbl->is_view) {
        tbl_schema.put("view", true);
    }

    // columns array
    ArrayBuilder cols = builder.array();
    for (int c = 0; c < tbl->column_count; c++) {
        RdbColumn* col = &tbl->columns[c];
        MapBuilder col_map = builder.map();
        col_map.put("name", col->name);
        col_map.put("type", col->type_decl);
        col_map.put("nullable", col->nullable);
        col_map.put("pk", col->primary_key);
        cols.append(col_map.final());
    }
    tbl_schema.put("columns", cols.final());

    // indexes array
    if (tbl->index_count > 0) {
        ArrayBuilder idxs = builder.array();
        for (int i = 0; i < tbl->index_count; i++) {
            RdbIndex* idx = &tbl->indexes[i];
            MapBuilder idx_map = builder.map();
            idx_map.put("name", idx->name);
            idx_map.put("unique", idx->unique);
            ArrayBuilder idx_cols = builder.array();
            for (int j = 0; j < idx->column_count; j++) {
                idx_cols.append(idx->columns[j]);
            }
            idx_map.put("columns", idx_cols.final());
            idxs.append(idx_map.final());
        }
        tbl_schema.put("indexes", idxs.final());
    }

    // foreign keys
    if (tbl->fk_count > 0) {
        ArrayBuilder fks = builder.array();
        for (int f = 0; f < tbl->fk_count; f++) {
            RdbForeignKey* fk = &tbl->foreign_keys[f];
            MapBuilder fk_map = builder.map();
            fk_map.put("column", fk->column);
            fk_map.put("ref_table", fk->ref_table);
            fk_map.put("ref_column", fk->ref_column);
            fks.append(fk_map.final());
        }
        tbl_schema.put("foreign_keys", fks.final());
    }

    return tbl_schema.final();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Main entry point — called from input.cpp
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Open a database file and produce a <db> element.
 *
 * @param pathname  absolute path to the database file
 * @param type      explicit driver name (e.g. "sqlite"), or NULL to auto-detect
 * @return Input with root = <db name:..., schema:{...}, data:{...}>
 */
Input* input_rdb_from_path(const char* pathname, const char* type) {
    rdb_ensure_drivers();

    if (!pathname) {
        log_error("rdb input: pathname is NULL");
        return NULL;
    }

    // create Input through InputManager
    Url* abs_url = url_parse(pathname);
    Input* input = InputManager::create_input(abs_url);
    if (abs_url) url_destroy(abs_url);
    if (!input) {
        log_error("rdb input: failed to create Input for '%s'", pathname);
        return NULL;
    }

    // open connection via generic RDB API
    RdbConn* conn = rdb_open(input->pool, pathname, type, /*readonly=*/true);
    if (!conn) {
        log_error("rdb input: failed to open database '%s'", pathname);
        input->root = ItemNull;
        return input;
    }

    // load schema
    if (rdb_load_schema(conn) != RDB_OK) {
        log_error("rdb input: failed to load schema for '%s'", pathname);
        rdb_close(conn);
        input->root = ItemNull;
        return input;
    }

    MarkBuilder builder(input);

    // build schema map
    MapBuilder schema_map = builder.map();
    for (int t = 0; t < conn->schema.table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        schema_map.put(tbl->name, rdb_build_table_schema(builder, tbl));
    }

    // build data map — eagerly load all tables for Phase 1
    // (lazy loading deferred to later; simple eager approach first)
    MapBuilder data_map = builder.map();
    for (int t = 0; t < conn->schema.table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        data_map.put(tbl->name, rdb_fetch_table(builder, conn, tbl));
    }

    // extract filename for the db element name
    const char* basename = pathname;
    const char* slash = strrchr(pathname, '/');
    if (slash) basename = slash + 1;
    #ifdef _WIN32
    const char* bslash = strrchr(pathname, '\\');
    if (bslash && bslash > slash) basename = bslash + 1;
    #endif

    // build top-level <db> element
    ElementBuilder db_el = builder.element("db");
    db_el.attr("name", basename);
    db_el.attr("schema", schema_map.final());
    db_el.attr("data", data_map.final());
    db_el.attr("table_count", (int64_t)conn->schema.table_count);

    input->root = db_el.final();

    // close connection — all data has been materialized into arena
    rdb_close(conn);

    log_debug("rdb input: loaded '%s' with %d tables", pathname, conn->schema.table_count);
    return input;
}

/**
 * Detect whether a file path or type string should be handled by the RDB
 * module. Returns the driver name (e.g. "sqlite") or NULL.
 */
const char* rdb_detect_format(const char* pathname, const char* type) {
    rdb_ensure_drivers();

    // explicit type string matches a known driver?
    if (type) {
        if (rdb_get_driver(type)) return type;
    }

    // auto-detect from file extension / URI scheme
    if (pathname) {
        return rdb_detect_driver(pathname);
    }

    return NULL;
}
