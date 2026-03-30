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
#include "../lambda-decimal.hpp"
#include "../../lib/rdb.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/file.h"
#include "../../lib/hashmap.h"
#include <string.h>

// helper: get a map attribute by key using direct shape lookup.
// Reconstructs tagged-pointer Items from the map's packed data buffer.
// The data buffer stores raw C values (map_put strips tags); we must re-tag.
static Item rdb_map_attr(Item data, const char* key) {
    if (!data.item || !key) return ItemNull;
    int key_len = (int)strlen(key);
    TypeId tid = get_type_id(data);
    TypeMap* tm = NULL;
    void* map_data = NULL;
    if (tid == LMD_TYPE_MAP) {
        Map* m = data.map;
        tm = (TypeMap*)m->type;
        map_data = m->data;
    } else if (tid == LMD_TYPE_ELEMENT) {
        Element* e = data.element;
        tm = (TypeMap*)e->type;
        map_data = e->data;
    }
    if (!tm || !map_data) return ItemNull;

    ShapeEntry* se = typemap_hash_lookup(tm, key, key_len);
    if (!se) return ItemNull;

    void* field_ptr = (char*)map_data + se->byte_offset;
    TypeId field_tid = se->type->type_id;
    Item result;
    switch (field_tid) {
        case LMD_TYPE_NULL:
        case LMD_TYPE_UNDEFINED:
            return ItemNull;
        case LMD_TYPE_BOOL:
            result.item = b2it(*(bool*)field_ptr);
            return result;
        case LMD_TYPE_INT:
            result.item = i2it(*(int64_t*)field_ptr);
            return result;
        // tagged-pointer scalars: buffer stores raw value, re-tag via pointer to buffer
        case LMD_TYPE_INT64:
            result.item = l2it((int64_t*)field_ptr);
            return result;
        case LMD_TYPE_FLOAT:
            result.item = d2it((double*)field_ptr);
            return result;
        case LMD_TYPE_DTIME:
            result.item = k2it((DateTime*)field_ptr);
            return result;
        // tagged-pointer heap types: buffer stores a raw pointer, re-tag it
        case LMD_TYPE_DECIMAL:
            result.item = c2it(*(Decimal**)field_ptr);
            return result;
        case LMD_TYPE_STRING:
            result.item = s2it(*(String**)field_ptr);
            return result;
        case LMD_TYPE_SYMBOL:
            result.item = y2it(*(Symbol**)field_ptr);
            return result;
        case LMD_TYPE_BINARY:
            result.item = x2it(*(String**)field_ptr);
            return result;
        default:
            // container types (array, map, element, etc.) are self-describing:
            // their structs start with TypeId, so raw pointers are valid Items
            result.item = *(uint64_t*)field_ptr;
            return result;
    }
}

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

        case RDB_TYPE_DATETIME: {
            if (val.type != RDB_TYPE_STRING || !val.str_val) return ItemNull;
            // try ISO-8601 first (with 'T' separator), then Lambda format (space separator)
            DateTime* dt = datetime_parse_iso8601(builder.pool(), val.str_val);
            if (!dt) dt = datetime_parse_lambda(builder.pool(), val.str_val);
            if (dt) return {.item = k2it(dt)};
            // fallback: return as string if parsing fails
            return builder.createStringItem(val.str_val);
        }

        case RDB_TYPE_JSON:
            // auto-parse JSON column content via existing JSON parser
            if (val.type == RDB_TYPE_STRING && val.str_val && val.str_len > 0) {
                return parse_json_to_item(builder.input(), val.str_val);
            }
            return ItemNull;

        case RDB_TYPE_DECIMAL: {
            // SQLite stores DECIMAL columns as REAL, INTEGER, or TEXT at the storage level.
            // Use arena-safe decimal creation to avoid GC collecting the value before use.
            // Prefer string representation for exact precision; fall back to float/int.
            if (val.type == RDB_TYPE_STRING && val.str_val) {
                Item dec = decimal_from_string_arena(val.str_val, builder.arena());
                if (dec._type_id == LMD_TYPE_DECIMAL) return dec;
            }
            if (val.type == RDB_TYPE_FLOAT) {
                Item dec = decimal_from_double_arena(val.float_val, builder.arena());
                if (dec._type_id == LMD_TYPE_DECIMAL) return dec;
                return builder.createFloat(val.float_val);
            }
            if (val.type == RDB_TYPE_INT) {
                Item dec = decimal_from_int64_arena(val.int_val, builder.arena());
                if (dec._type_id == LMD_TYPE_DECIMAL) return dec;
                return builder.createInt(val.int_val);
            }
            return ItemNull;
        }

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
        return builder.array().final();
    }

    // build array of row maps
    ArrayBuilder arr = builder.array();

    int64_t row_count = 0;
    int rc;
    while ((rc = rdb_step(stmt)) == RDB_ROW) {
        MapBuilder row = builder.map();
        for (int c = 0; c < tbl->column_count; c++) {
            RdbValue val = rdb_column_value(stmt, c);
            Item item = rdb_value_to_item(builder, val, tbl->columns[c].type);
            row.put(tbl->columns[c].name, item);
        }
        arr.append(row.final());
        row_count++;
    }

    rdb_finalize(stmt);
    return arr.final();
}

/* ═══════════════════════════════════════════════════════════════════════
 * FK auto-navigation helpers
 * ═══════════════════════════════════════════════════════════════════════ */

// entry for PK index hashmap: pk_value (int64) → row index in the array
struct PkEntry {
    int64_t pk_value;
    int row_index;
};

static uint64_t pk_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const PkEntry* e = (const PkEntry*)item;
    // simple hash for int64 keys
    uint64_t h = (uint64_t)e->pk_value;
    h ^= seed0;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= seed1;
    return h;
}

static int pk_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return ((const PkEntry*)a)->pk_value == ((const PkEntry*)b)->pk_value ? 0 : 1;
}

// find the index of the primary key column in a table (first PK column, -1 if none)
static int rdb_find_pk_column(RdbTable* tbl) {
    for (int c = 0; c < tbl->column_count; c++) {
        if (tbl->columns[c].primary_key) return c;
    }
    return -1;
}

// find the index of a named column in a table (-1 if not found)
static int rdb_find_column(RdbTable* tbl, const char* name) {
    for (int c = 0; c < tbl->column_count; c++) {
        if (strcmp(tbl->columns[c].name, name) == 0) return c;
    }
    return -1;
}

// find a table index in the schema by name (-1 if not found)
static int rdb_find_table_index(RdbSchema* schema, const char* name) {
    for (int t = 0; t < schema->table_count; t++) {
        if (strcmp(schema->tables[t].name, name) == 0) return t;
    }
    return -1;
}

// entry for reverse FK grouping: pk_value → ArrayBuilder of related rows
struct RevFkEntry {
    int64_t pk_value;
    int count;
    int capacity;
    Item* rows;  // dynamically grown array of row Items
};

static uint64_t revfk_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RevFkEntry* e = (const RevFkEntry*)item;
    uint64_t h = (uint64_t)e->pk_value;
    h ^= seed0;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= seed1;
    return h;
}

static int revfk_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return ((const RevFkEntry*)a)->pk_value == ((const RevFkEntry*)b)->pk_value ? 0 : 1;
}

/**
 * Fetch table rows with FK forward navigation.
 * For each FK in the table, adds a link_name attribute to each row map
 * that resolves to the referenced row in the target table.
 *
 * @param builder     MarkBuilder for creating Lambda Items
 * @param conn        RDB connection
 * @param tbl         table schema
 * @param schema      full database schema (for looking up referenced tables)
 * @param table_data  array of already-built table Item arrays (indexed by table index)
 * @param pk_indexes  array of PK index hashmaps (indexed by table index, may be NULL)
 */
static Item rdb_fetch_table_with_fks(MarkBuilder& builder, RdbConn* conn,
                                     RdbTable* tbl, RdbSchema* schema,
                                     Item* table_data, HashMap** pk_indexes) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "SELECT * FROM \"");
    strbuf_append_str(sb, tbl->name);
    strbuf_append_str(sb, "\"");

    RdbStmt* stmt = rdb_prepare(conn, sb->str);
    strbuf_free(sb);

    if (!stmt) {
        log_error("rdb fk: failed to prepare SELECT for table '%s'", tbl->name);
        return builder.array().final();
    }

    // pre-resolve FK column indexes and target table info
    struct FkInfo {
        int fk_col_idx;           // index of FK column in this table
        int ref_table_idx;        // index of referenced table in schema
        const char* link_name;    // navigation attribute name
        HashMap* ref_pk_index;    // PK index of referenced table
    };

    int fk_info_count = 0;
    FkInfo fk_info[32];  // max 32 FKs per table

    for (int f = 0; f < tbl->fk_count && fk_info_count < 32; f++) {
        RdbForeignKey* fk = &tbl->foreign_keys[f];
        int col_idx = rdb_find_column(tbl, fk->column);
        int ref_idx = rdb_find_table_index(schema, fk->ref_table);
        if (col_idx < 0 || ref_idx < 0) continue;
        if (!pk_indexes[ref_idx]) continue;  // referenced table has no PK index
        if (table_data[ref_idx].item == 0) continue;  // referenced table not yet built

        fk_info[fk_info_count].fk_col_idx = col_idx;
        fk_info[fk_info_count].ref_table_idx = ref_idx;
        fk_info[fk_info_count].link_name = fk->link_name;
        fk_info[fk_info_count].ref_pk_index = pk_indexes[ref_idx];
        fk_info_count++;
    }

    // build array of row maps with FK navigation
    ArrayBuilder arr = builder.array();
    int rc;
    while ((rc = rdb_step(stmt)) == RDB_ROW) {
        MapBuilder row = builder.map();

        // regular columns
        for (int c = 0; c < tbl->column_count; c++) {
            RdbValue val = rdb_column_value(stmt, c);
            Item item = rdb_value_to_item(builder, val, tbl->columns[c].type);
            row.put(tbl->columns[c].name, item);
        }

        // FK forward navigation: resolve each FK to the referenced row
        for (int f = 0; f < fk_info_count; f++) {
            RdbValue fk_val = rdb_column_value(stmt, fk_info[f].fk_col_idx);
            if (fk_val.is_null) {
                // null FK → null navigation
                row.putNull(fk_info[f].link_name);
                continue;
            }

            // look up referenced row by PK value
            int64_t fk_int = fk_val.int_val;
            PkEntry lookup = {.pk_value = fk_int, .row_index = -1};
            const PkEntry* found = (const PkEntry*)hashmap_get(
                fk_info[f].ref_pk_index, &lookup);

            if (found && found->row_index >= 0) {
                // get the referenced row from the already-built table array
                Array* ref_arr = table_data[fk_info[f].ref_table_idx].array;
                if (ref_arr && found->row_index < ref_arr->length) {
                    row.put(fk_info[f].link_name, ref_arr->items[found->row_index]);
                } else {
                    row.putNull(fk_info[f].link_name);
                }
            } else {
                row.putNull(fk_info[f].link_name);
            }
        }

        arr.append(row.final());
    }

    rdb_finalize(stmt);
    return arr.final();
}

/**
 * Build PK index for a table using a specific PK column name.
 * Maps pk_value (int64) → row index in the array.
 */
static HashMap* rdb_build_pk_index_for(Item table_array, const char* pk_col_name) {
    if (table_array.item == 0 || !pk_col_name) return NULL;
    Array* arr = table_array.array;
    if (!arr || arr->length == 0) return NULL;

    HashMap* idx = hashmap_new(sizeof(PkEntry), (size_t)(arr->length < 8 ? 16 : arr->length * 2),
                               0x12345678ULL, 0x9ABCDEF0ULL,
                               pk_hash, pk_compare, NULL, NULL);
    if (!idx) return NULL;

    for (int64_t r = 0; r < arr->length; r++) {
        Item row = arr->items[r];
        Item pk_item = rdb_map_attr(row, pk_col_name);
        if (pk_item.item == 0 || pk_item.item == ItemNull.item) continue;

        int64_t pk_val;
        TypeId tid = get_type_id(pk_item);
        if (tid == LMD_TYPE_INT) {
            pk_val = pk_item.get_int56();
        } else if (tid == LMD_TYPE_INT64) {
            pk_val = *(int64_t*)pk_item.item;
        } else {
            continue;  // only support integer PKs for now
        }

        PkEntry entry = {.pk_value = pk_val, .row_index = (int)r};
        hashmap_set(idx, &entry);
    }

    return idx;
}

/**
 * Build reverse FK arrays for a target table's rows.
 * For each row in the target table, collects all referencing rows from
 * the source table and returns a new array with the reverse FK attribute added.
 *
 * @param builder      MarkBuilder
 * @param target_array existing target table rows (will be rebuilt)
 * @param target_tbl   target table schema
 * @param source_array source table rows (the table holding the FK)
 * @param rfk          the reverse FK descriptor (from target's perspective)
 * @param target_pk_col PK column name of the target table
 */
static Item rdb_add_reverse_fk(MarkBuilder& builder, Item target_array,
                                RdbTable* target_tbl, Item source_array,
                                RdbForeignKey* rfk, const char* target_pk_col) {
    Array* tgt_arr = target_array.array;
    Array* src_arr = source_array.array;
    if (!tgt_arr || !src_arr || tgt_arr->length == 0) return target_array;

    // build a grouping map: target_pk_value → list of source row items
    HashMap* groups = hashmap_new(sizeof(RevFkEntry),
                                  (size_t)(tgt_arr->length < 8 ? 16 : tgt_arr->length * 2),
                                  0xAAAAAAAAULL, 0xBBBBBBBBULL,
                                  revfk_hash, revfk_compare, NULL, NULL);
    if (!groups) return target_array;

    // rfk fields (from target's perspective):
    // rfk->ref_table = source table name (stored in "from_table" in schema output)
    // rfk->ref_column = FK column in source table  (stored in "from_column")
    // rfk->column = PK column in target table
    // rfk->link_name = reverse nav name (e.g., "products")
    const char* src_fk_col_name = rfk->ref_column;  // FK column in source table

    // group source rows by their FK column value
    for (int64_t r = 0; r < src_arr->length; r++) {
        Item src_row = src_arr->items[r];
        Item fk_item = rdb_map_attr(src_row, src_fk_col_name);
        if (fk_item.item == 0 || fk_item.item == ItemNull.item) continue;

        int64_t fk_val;
        TypeId tid = get_type_id(fk_item);
        if (tid == LMD_TYPE_INT) {
            fk_val = fk_item.get_int56();
        } else if (tid == LMD_TYPE_INT64) {
            fk_val = *(int64_t*)fk_item.item;
        } else {
            continue;
        }

        RevFkEntry lookup = {.pk_value = fk_val, .count = 0, .capacity = 0, .rows = NULL};
        const RevFkEntry* existing = (const RevFkEntry*)hashmap_get(groups, &lookup);

        RevFkEntry entry;
        if (existing) {
            entry = *existing;
        } else {
            entry.pk_value = fk_val;
            entry.count = 0;
            entry.capacity = 4;
            entry.rows = (Item*)malloc(4 * sizeof(Item));
            if (!entry.rows) continue;
        }

        if (entry.count >= entry.capacity) {
            entry.capacity *= 2;
            Item* new_rows = (Item*)realloc(entry.rows, entry.capacity * sizeof(Item));
            if (!new_rows) continue;
            entry.rows = new_rows;
        }
        entry.rows[entry.count++] = src_row;
        hashmap_set(groups, &entry);
    }

    // rebuild target rows with reverse FK array attributes
    ArrayBuilder new_arr = builder.array();
    for (int64_t r = 0; r < tgt_arr->length; r++) {
        Item old_row = tgt_arr->items[r];
        MapBuilder new_row = builder.map();

        // copy all existing attributes from old row
        Map* old_map = old_row.map;
        TypeMap* tm = (TypeMap*)old_map->type;
        ShapeEntry* se = tm->shape;
        while (se) {
            if (se->name) {
                Item val = rdb_map_attr(old_row, se->name->str);
                if (val.item != 0 && val.item != ItemNull.item) {
                    new_row.put(se->name->str, val);
                } else {
                    // preserve null entries too (nullable columns)
                    new_row.putNull(se->name->str);
                }
            }
            se = se->next;
        }

        // add reverse FK array
        Item pk_item = rdb_map_attr(old_row, target_pk_col);
        if (pk_item.item != 0 && pk_item.item != ItemNull.item) {
            int64_t pk_val;
            TypeId tid = get_type_id(pk_item);
            if (tid == LMD_TYPE_INT) {
                pk_val = pk_item.get_int56();
            } else if (tid == LMD_TYPE_INT64) {
                pk_val = *(int64_t*)pk_item.item;
            } else {
                pk_val = 0;
            }

            RevFkEntry lookup = {.pk_value = pk_val, .count = 0, .capacity = 0, .rows = NULL};
            const RevFkEntry* group = (const RevFkEntry*)hashmap_get(groups, &lookup);
            if (group && group->count > 0) {
                ArrayBuilder rev_arr = builder.array();
                for (int i = 0; i < group->count; i++) {
                    rev_arr.append(group->rows[i]);
                }
                new_row.put(rfk->link_name, rev_arr.final());
            } else {
                // no related rows → empty array
                new_row.put(rfk->link_name, builder.array().final());
            }
        } else {
            new_row.put(rfk->link_name, builder.array().final());
        }

        new_arr.append(new_row.final());
    }

    // free group entries
    size_t iter = 0;
    void* grp_item;
    while (hashmap_iter(groups, &iter, &grp_item)) {
        RevFkEntry* e = (RevFkEntry*)grp_item;
        free(e->rows);
    }
    hashmap_free(groups);

    return new_arr.final();
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
            fk_map.put("link_name", fk->link_name);
            fks.append(fk_map.final());
        }
        tbl_schema.put("foreign_keys", fks.final());
    }

    // reverse foreign keys (incoming FKs from other tables)
    if (tbl->reverse_fk_count > 0) {
        ArrayBuilder rfks = builder.array();
        for (int f = 0; f < tbl->reverse_fk_count; f++) {
            RdbForeignKey* rfk = &tbl->reverse_fks[f];
            MapBuilder rfk_map = builder.map();
            rfk_map.put("from_table", rfk->ref_table);
            rfk_map.put("from_column", rfk->ref_column);
            rfk_map.put("column", rfk->column);
            rfk_map.put("link_name", rfk->link_name);
            rfks.append(rfk_map.final());
        }
        tbl_schema.put("reverse_fks", rfks.final());
    }

    // triggers
    if (tbl->trigger_count > 0) {
        ArrayBuilder trigs = builder.array();
        for (int i = 0; i < tbl->trigger_count; i++) {
            RdbTrigger* trig = &tbl->triggers[i];
            MapBuilder trig_map = builder.map();
            trig_map.put("name", trig->name);
            trig_map.put("timing", trig->timing);
            trig_map.put("event", trig->event);
            trigs.append(trig_map.final());
        }
        tbl_schema.put("triggers", trigs.final());
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

    int table_count = conn->schema.table_count;

    // build schema map
    MapBuilder schema_map = builder.map();
    for (int t = 0; t < table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        schema_map.put(tbl->name, rdb_build_table_schema(builder, tbl));
    }

    /* ═══════════════════════════════════════════════════════════════
     * Multi-pass data loading with FK auto-navigation
     *
     * Pass 1: Determine build order (topological sort by FK deps)
     * Pass 2: Load tables in order, building PK indexes and
     *         resolving forward FKs during row construction
     * Pass 3: Add reverse FK arrays to target tables
     * ═══════════════════════════════════════════════════════════════ */

    // table_data[t] holds the built Item array for table index t
    Item* table_data = (Item*)pool_calloc(input->pool,
                                          table_count * sizeof(Item));
    // pk_indexes[t] holds the PK hashmap for table t (or NULL)
    HashMap** pk_indexes = (HashMap**)pool_calloc(input->pool,
                                                   table_count * sizeof(HashMap*));
    // track which tables have been built
    bool* built = (bool*)pool_calloc(input->pool, table_count * sizeof(bool));

    // pass 1: topological build order — tables with no FKs first,
    // then tables whose FK targets are all built, repeat until done
    int built_count = 0;
    for (int round = 0; round < table_count + 1 && built_count < table_count; round++) {
        for (int t = 0; t < table_count; t++) {
            if (built[t]) continue;

            RdbTable* tbl = &conn->schema.tables[t];

            // check if all FK targets are already built
            bool deps_ready = true;
            if (tbl->fk_count > 0 && round < table_count) {
                for (int f = 0; f < tbl->fk_count; f++) {
                    int ref_idx = rdb_find_table_index(&conn->schema,
                                                       tbl->foreign_keys[f].ref_table);
                    if (ref_idx >= 0 && !built[ref_idx]) {
                        deps_ready = false;
                        break;
                    }
                }
            }
            // on the last round, build remaining (handles cycles)

            if (!deps_ready) continue;

            // fetch this table's rows
            if (tbl->fk_count > 0) {
                table_data[t] = rdb_fetch_table_with_fks(
                    builder, conn, tbl, &conn->schema, table_data, pk_indexes);
            } else {
                table_data[t] = rdb_fetch_table(builder, conn, tbl);
            }

            // build PK index for this table (needed if other tables reference it)
            int pk_col = rdb_find_pk_column(tbl);
            if (pk_col >= 0) {
                pk_indexes[t] = rdb_build_pk_index_for(
                    table_data[t], tbl->columns[pk_col].name);
            }

            built[t] = true;
            built_count++;
        }
    }

    // pass 3: add reverse FK arrays to target tables
    for (int t = 0; t < table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        if (tbl->reverse_fk_count == 0) continue;

        int pk_col = rdb_find_pk_column(tbl);
        if (pk_col < 0) continue;
        const char* pk_col_name = tbl->columns[pk_col].name;

        for (int f = 0; f < tbl->reverse_fk_count; f++) {
            RdbForeignKey* rfk = &tbl->reverse_fks[f];
            int src_idx = rdb_find_table_index(&conn->schema, rfk->ref_table);
            if (src_idx < 0 || table_data[src_idx].item == 0) continue;

            table_data[t] = rdb_add_reverse_fk(
                builder, table_data[t], tbl, table_data[src_idx],
                rfk, pk_col_name);
        }
    }

    // build data map from the built table arrays
    MapBuilder data_map = builder.map();
    for (int t = 0; t < table_count; t++) {
        RdbTable* tbl = &conn->schema.tables[t];
        if (table_data[t].item != 0) {
            data_map.put(tbl->name, table_data[t]);
        } else {
            data_map.put(tbl->name, builder.array().final());
        }
    }

    // free PK index hashmaps
    for (int t = 0; t < table_count; t++) {
        if (pk_indexes[t]) hashmap_free(pk_indexes[t]);
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
