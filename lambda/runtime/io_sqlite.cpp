// lambda.io.sqlite: host-authorized statements and S12.4.4v4 scoped transactions.
#if !defined(LAMBDA_NO_FILE_IO) && !defined(LAMBDA_IME_RESOURCE_IO) && !defined(LAMBDA_NO_TASKS)
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "io_sqlite.hpp"
#include "lambda-number-runtime.hpp"
#include "concurrency.h"
#include "write_set.hpp"
#include "../../lib/rdb_sqlite_tx.h"
#include "../../lib/sqlite/sqlite3.h"
#include "../../lib/arraylist.h"
#include "../../lib/memtrack.h"
#include "../../lib/utf.h"
#include <limits.h>
#include <math.h>
#include <string.h>

namespace {
constexpr int SQLITE_PARAM_LIMIT = 1024;
constexpr int SQLITE_COLUMN_LIMIT = 1024;
constexpr int SQLITE_ROW_LIMIT = 100000;
constexpr size_t SQLITE_CELL_LIMIT = 1000000;
constexpr size_t SQLITE_VALUE_BYTES_LIMIT = 16 * 1024 * 1024;
constexpr size_t SQLITE_SQL_BYTES_LIMIT = 1024 * 1024;
constexpr size_t SQLITE_PATH_BYTES_LIMIT = 32768;

struct SqliteTarget { char* path; bool existing; };
struct SqliteScope {
    LambdaTask* owner;
    Pool* pool;
    RdbSqliteTxn* tx;
    Item handle;
    bool rooted;
};
static char target_brand;
static char scope_brand;
static char attachment_key;

void destroy_target(void* data) {
    SqliteTarget* target = (SqliteTarget*)data;
    if (target) { mem_free(target->path); mem_free(target); }
}
static VMapVtable target_vtable = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, destroy_target, io_resource_trace};
static VMapVtable scope_vtable = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, io_resource_destroy, io_resource_trace};

bool require_threadsafe_sqlite(Item* error) {
    if (sqlite3_threadsafe()) return true;
    *error = io_fs_error(ERR_INVALID_STATE, "io.sqlite: SQLite thread safety is unavailable");
    return false;
}

SqliteScope* current_scope() {
    LambdaTask* task = lambda_task_running();
    return task ? (SqliteScope*)lambda_task_attachment_find(task, &attachment_key) : NULL;
}

// Invalidate the handle before closing, so no escaped value can reuse it.
int finish_scope(SqliteScope* scope, bool unwinding) {
    if (!scope) return RDB_ERROR;
    if (get_type_id(scope->handle) == LMD_TYPE_VMAP && scope->handle.vmap) {
        scope->handle.vmap->data = NULL;
        scope->handle.vmap->host_data = NULL;
    }
    if (scope->rooted) heap_unregister_gc_root(&scope->handle.item);
    int result = rdb_sqlite_tx_finish(scope->tx, unwinding);
    pool_destroy(scope->pool);
    mem_free(scope);
    return result;
}
void release_scope(void* data) { (void)finish_scope((SqliteScope*)data, true); }

Item target_value(Item path, bool existing) {
    if (get_type_id(path) != LMD_TYPE_STRING || !path.get_len() ||
            path.get_len() > SQLITE_PATH_BYTES_LIMIT || memchr(path.get_chars(), 0, path.get_len()) ||
            !utf8_valid(path.get_chars(), path.get_len()))
        return io_fs_error(ERR_TYPE_MISMATCH, "io.sqlite.database: expected a valid database path");
    RootFrame roots(1);
    Rooted<Item> held(roots, path);
    SqliteTarget* target = (SqliteTarget*)mem_calloc(1, sizeof(SqliteTarget), MEM_CAT_EVAL);
    if (!target) return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite.database: allocation failed");
    target->existing = existing;
    target->path = mem_dup_n(held.get().get_chars(), held.get().get_len(), MEM_CAT_EVAL);
    VMap* value = target->path ? io_resource_handle_new(&target_vtable, &target_brand, target) : NULL;
    if (!value) { destroy_target(target); return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite.database: allocation failed"); }
    return (Item){.vmap = value};
}

Item open_target(Item target_value) {
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("io.sqlite.database", &denied) || !require_threadsafe_sqlite(&denied)) return denied;
    LambdaTask* owner = lambda_task_running();
    if (!owner || current_scope()) return io_fs_error(ERR_INVALID_OPERATION, "io.sqlite: transaction scope is unavailable or nested");
    RootFrame roots(1);
    Rooted<Item> held(roots, target_value);
    SqliteTarget* target = (SqliteTarget*)held.get().vmap->host_data;
    if (!target || !target->path) return io_fs_error(ERR_INVALID_OPERATION, "io.sqlite: invalid database target");
    SqliteScope* scope = (SqliteScope*)mem_calloc(1, sizeof(SqliteScope), MEM_CAT_EVAL);
    if (!scope) return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite: transaction allocation failed");
    scope->owner = owner;
    scope->pool = pool_create();
    scope->tx = scope->pool ? (target->existing ? rdb_sqlite_tx_begin_existing(scope->pool, target->path)
                                              : rdb_sqlite_tx_begin(scope->pool, target->path)) : NULL;
    if (!scope->tx) {
        if (scope->pool) pool_destroy(scope->pool);
        mem_free(scope);
        return io_fs_error(ERR_IO_ERROR, "io.sqlite: cannot open transaction");
    }
    VMap* handle = io_resource_handle_new(&scope_vtable, &scope_brand, scope);
    if (!handle) { (void)finish_scope(scope, true); return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite: handle allocation failed"); }
    scope->handle = (Item){.vmap = handle};
    heap_register_gc_root(&scope->handle.item);
    scope->rooted = true;
    if (!lambda_task_attachment_install(owner, &attachment_key, scope, release_scope)) {
        (void)finish_scope(scope, true);
        return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite: cannot own transaction");
    }
    return scope->handle;
}

Item close_scope(Bool unwinding) {
    LambdaTask* owner = lambda_task_running();
    SqliteScope* scope = owner ? (SqliteScope*)lambda_task_attachment_take(owner, &attachment_key) : NULL;
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.sqlite: no active transaction");
    int result = finish_scope(scope, unwinding == BOOL_TRUE);
    return result == RDB_OK ? ItemNull : io_fs_error(ERR_IO_ERROR, "io.sqlite: transaction rolled back or commit failed");
}

struct BoundValues {
    RdbValue* values;
    int count;
    size_t bytes;
};

void release_values(BoundValues* bound) {
    if (!bound->values) return;
    for (int i = 0; i < bound->count; i++) {
        if (bound->values[i].type == RDB_TYPE_STRING || bound->values[i].type == RDB_TYPE_BLOB)
            mem_free((void*)bound->values[i].str_val);
    }
    mem_free(bound->values);
}

bool copy_bytes(size_t* total, RdbValue* out, const char* bytes, size_t length) {
    if (length > INT_MAX || length > SQLITE_VALUE_BYTES_LIMIT - *total || (!bytes && length)) return false;
    *total += length;
    out->str_len = (int)length;
    if (!length) { out->str_val = NULL; return true; }
    char* copy = (char*)mem_alloc(length, MEM_CAT_EVAL);
    if (!copy) return false;
    memcpy(copy, bytes, length);
    out->str_val = copy;
    return true;
}

bool bind_values(Item params, BoundValues* out) {
    TypeId type = get_type_id(params);
    if (type == LMD_TYPE_NULL) return true;
    if ((type != LMD_TYPE_ARRAY && type != LMD_TYPE_ARRAY_NUM) || vector_is_ndim(params)) return false;
    int64_t count = vector_length(params);
    if (count < 0 || count > SQLITE_PARAM_LIMIT) return false;
    out->count = (int)count;
    if (!count) return true;
    out->values = (RdbValue*)mem_calloc((size_t)count, sizeof(RdbValue), MEM_CAT_EVAL);
    if (!out->values) return false;
    for (int i = 0; i < out->count; i++) {
        Item value = vector_get(params, i);
        RdbValue* target = &out->values[i];
        TypeId kind = get_type_id(value);
        if (kind == LMD_TYPE_NULL) { target->type = RDB_TYPE_NULL; target->is_null = true; }
        else if (kind == LMD_TYPE_BOOL) { target->type = RDB_TYPE_BOOL; target->bool_val = it2b(value); }
        else if (kind == LMD_TYPE_INT || kind == LMD_TYPE_INT64 || kind == LMD_TYPE_UINT64 ||
                kind == LMD_TYPE_FLOAT || kind == LMD_TYPE_NUM_SIZED) {
            LambdaNumericRuntimePart numeric;
            if (!lambda_numeric_runtime_part(value, &numeric)) return false;
            if (kind == LMD_TYPE_FLOAT || (kind == LMD_TYPE_NUM_SIZED && numeric.kind == LAMBDA_NUM_PART_FLOAT)) {
                target->type = RDB_TYPE_FLOAT;
                target->float_val = numeric.float_value;
                if (!isfinite(target->float_val)) return false;
            } else {
                target->type = RDB_TYPE_INT;
                if (!lambda_item_to_int64_exact(value, &target->int_val)) return false;
            }
        } else if (kind == LMD_TYPE_STRING) {
            target->type = RDB_TYPE_STRING;
            if (!utf8_valid(value.get_chars(), value.get_len()) ||
                    !copy_bytes(&out->bytes, target, value.get_chars(), value.get_len())) return false;
        } else if (kind == LMD_TYPE_BINARY) {
            target->type = RDB_TYPE_BLOB;
            Binary* binary = value.get_binary();
            if (!copy_bytes(&out->bytes, target, (const char*)binary_data(binary), binary_length(binary))) return false;
        } else return false;
    }
    return true;
}

// Worker-owned rows never contain Lambda Items or borrowed SQLite cells.
struct NativeRow {
    BoundValues cells;
};
struct SqliteJob {
    Pool* pool;
    RdbSqliteTxn* tx;
    SqliteScope* scope;
    RdbSqliteWork* work;
    char* path;
    char* sql;
    size_t sql_len;
    BoundValues params;
    ArrayList* rows;
    size_t row_bytes;
    size_t row_cells;
    bool query;
};

void free_job(SqliteJob* job) {
    if (!job) return;
    // Scheduler teardown joins the worker before this orphan release runs.
    if (job->work) {
        (void)rdb_sqlite_tx_work_end(job->work); job->work = NULL;
        if (job->scope) rdb_sqlite_tx_mark_failed(job->scope->tx);
    }
    if (job->tx && !job->scope) { (void)rdb_sqlite_tx_finish(job->tx, true); job->tx = NULL; }
    if (job->pool) pool_destroy(job->pool);
    for (int i = 0; job->rows && i < arraylist_length(job->rows); i++) {
        NativeRow* row = (NativeRow*)arraylist_get(job->rows, i);
        release_values(&row->cells);
        mem_free(row);
    }
    if (job->rows) arraylist_free(job->rows);
    release_values(&job->params);
    mem_free(job->path);
    mem_free(job->sql);
    mem_free(job);
}

bool collect_row(void* user, int count, const char* const* names, const RdbValue* values) {
    (void)names;
    SqliteJob* job = (SqliteJob*)user;
    if (count < 0 || count > SQLITE_COLUMN_LIMIT || arraylist_length(job->rows) >= SQLITE_ROW_LIMIT ||
            (size_t)count > SQLITE_CELL_LIMIT - job->row_cells) return false;
    job->row_cells += (size_t)count;
    NativeRow* row = (NativeRow*)mem_calloc(1, sizeof(NativeRow), MEM_CAT_EVAL);
    if (!row) return false;
    row->cells.count = count;
    row->cells.values = count ? (RdbValue*)mem_calloc((size_t)count, sizeof(RdbValue), MEM_CAT_EVAL) : NULL;
    if (count && !row->cells.values) { mem_free(row); return false; }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        row->cells.values[i] = values[i];
        RdbValue* cell = &row->cells.values[i];
        if (cell->type == RDB_TYPE_STRING || cell->type == RDB_TYPE_BLOB) {
            cell->str_val = NULL;
            if (values[i].str_len < 0 || (values[i].type == RDB_TYPE_STRING &&
                    (!values[i].str_val || !utf8_valid(values[i].str_val, (size_t)values[i].str_len)))) {
                ok = false;
            } else {
                ok = copy_bytes(&job->row_bytes, cell, values[i].str_val, (size_t)values[i].str_len);
            }
        }
    }
    if (ok) ok = arraylist_append(job->rows, row) != 0;
    if (!ok) { release_values(&row->cells); mem_free(row); }
    return ok;
}

// Only the owner task allocates and roots Lambda values (D5.3.1–D5.3.3).
bool materialize_rows(SqliteJob* job, Rooted<Array*>* result) {
    RootFrame roots(2);
    Rooted<Array*> row(roots, (Array*)NULL);
    Rooted<Item> cell(roots, ItemNull);
    for (int r = 0; r < arraylist_length(job->rows); r++) {
        NativeRow* source = (NativeRow*)arraylist_get(job->rows, r);
        row.set(array_plain());
        if (!row.get()) return false;
        for (int i = 0; i < source->cells.count; i++) {
            RdbValue* v = &source->cells.values[i];
            int64_t wide = 0;
            double real = 0;
            if (v->is_null || v->type == RDB_TYPE_NULL) cell.set(ItemNull);
            else if (v->type == RDB_TYPE_INT) {
                wide = v->int_val;
                cell.set(wide >= INT53_MIN && wide <= INT53_MAX ? (Item){.item = i2it(wide)} : (Item){.item = l2it(&wide)});
            } else if (v->type == RDB_TYPE_FLOAT) {
                real = v->float_val;
                if (!isfinite(real)) return false;
                cell.set(lambda_float_ptr_to_item(&real));
            } else if (v->type == RDB_TYPE_STRING) {
                cell.set(io_fs_make_string(v->str_val ? v->str_val : "", (size_t)v->str_len));
            } else if (v->type == RDB_TYPE_BLOB) {
                Binary* binary = heap_binary_from_bytes(v->str_val ? v->str_val : "", v->str_len);
                cell.set(binary ? (Item){.item = x2it(binary)} : ItemError);
            } else return false;
            if (item_is_error(cell.get())) return false;
            int64_t before = row.get()->length;
            array_push_verbatim(row.get(), cell.get());
            if (row.get()->length != before + 1) return false;
        }
        int64_t before = result->get()->length;
        array_push_verbatim(result->get(), (Item){.array = row.get()});
        if (result->get()->length != before + 1) return false;
    }
    return true;
}

void run_job(LambdaOffload* offload, void* data) {
    SqliteJob* job = (SqliteJob*)data;
    (void)rdb_sqlite_tx_work_execute(job->work, job->sql, job->sql_len,
        job->params.values, job->params.count, job->query ? collect_row : NULL,
        job->query ? job : NULL, lambda_offload_cancel_flag(offload));
}
void release_orphan(void* data) { free_job((SqliteJob*)data); }

Item sqlite_statement(Item path, Item sql, Item params, bool query) {
    SqliteScope* scope = current_scope();
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("io.sqlite", &denied) || !require_threadsafe_sqlite(&denied)) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        return denied;
    }
    if (write_set_transaction_open_here())
        return io_fs_error(ERR_INVALID_OPERATION, "io.sqlite: SQL cannot run in a document update block");
    bool scope_handle = get_type_id(path) == LMD_TYPE_VMAP && path.vmap &&
        path.vmap->host_type == &scope_brand && scope && path.vmap->host_data == scope;
    if (scope && !scope_handle) {
        rdb_sqlite_tx_mark_failed(scope->tx);
        return io_fs_error(ERR_INVALID_OPERATION, "io.sqlite: use the active transaction handle");
    }
    bool valid_path = scope_handle || (get_type_id(path) == LMD_TYPE_STRING && path.get_len() &&
        path.get_len() <= SQLITE_PATH_BYTES_LIMIT && !memchr(path.get_chars(), 0, path.get_len()) &&
        utf8_valid(path.get_chars(), path.get_len()));
    if (!valid_path || get_type_id(sql) != LMD_TYPE_STRING || !sql.get_len() ||
            sql.get_len() > SQLITE_SQL_BYTES_LIMIT || !utf8_valid(sql.get_chars(), sql.get_len())) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        return io_fs_error(ERR_TYPE_MISMATCH, "io.sqlite: expected a database target and SQL text");
    }
    RootFrame roots(5);
    Rooted<Item> held_path(roots, path), held_sql(roots, sql), held_params(roots, params), outcome(roots, ItemNull);
    Rooted<Array*> result(roots, query ? array_plain() : NULL);
    if (query && !result.get()) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite: cannot allocate result");
    }
    SqliteJob* job = (SqliteJob*)mem_calloc(1, sizeof(SqliteJob), MEM_CAT_EVAL);
    if (!job) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        return io_fs_error(ERR_OUT_OF_MEMORY, "io.sqlite: cannot allocate job");
    }
    job->query = query;
    job->scope = scope;
    if (!scope) job->path = mem_dup_n(held_path.get().get_chars(), held_path.get().get_len(), MEM_CAT_EVAL);
    job->sql = mem_dup_n(held_sql.get().get_chars(), held_sql.get().get_len(), MEM_CAT_EVAL);
    job->sql_len = held_sql.get().get_len();
    job->rows = query ? arraylist_new(8) : NULL;
    bool valid = (scope || job->path) && job->sql && (!query || job->rows) && bind_values(held_params.get(), &job->params);
    if (!valid) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        free_job(job);
        return io_fs_error(ERR_TYPE_MISMATCH, "io.sqlite: invalid SQL parameters or allocation");
    }
    // Authorization and registration happen on the owning task, never on the worker.
    if (scope) job->tx = scope->tx;
    else {
        job->pool = pool_create();
        job->tx = job->pool ? rdb_sqlite_tx_begin(job->pool, job->path) : NULL;
    }
    job->work = job->tx ? rdb_sqlite_tx_work_begin(job->tx) : NULL;
    if (!job->work) {
        if (scope) rdb_sqlite_tx_mark_failed(scope->tx);
        free_job(job);
        return io_fs_error(ERR_IO_ERROR, "io.sqlite: cannot open transaction");
    }
    static const LambdaOffloadOps ops = {run_job, NULL, release_orphan};
    outcome.set(lambda_task_offload(&ops, job, 0));
    int status = rdb_sqlite_tx_work_end(job->work);
    job->work = NULL;
    bool ok = !item_is_error(outcome.get()) && status == RDB_OK;
    if (ok && query) ok = materialize_rows(job, &result);
    int finish = RDB_OK;
    if (scope) {
        if (!ok) rdb_sqlite_tx_mark_failed(scope->tx);
    } else {
        finish = rdb_sqlite_tx_finish(job->tx, !ok);
        job->tx = NULL;
    }
    free_job(job);
    if (item_is_error(outcome.get())) return outcome.get();
    if (!ok || finish != RDB_OK) return io_fs_error(ERR_IO_ERROR, "io.sqlite: statement failed or result exceeded its limits");
    return query ? (Item){.array = result.get()} : ItemNull;
}

} // namespace

bool io_sqlite_is_target(Item value) {
    return get_type_id(value) == LMD_TYPE_VMAP && value.vmap && value.vmap->host_type == &target_brand;
}
bool io_sqlite_scope_open_here(void) { return current_scope() != NULL; }
Item io_sqlite_open(Item target) { return open_target(target); }
Item io_sqlite_close(Bool unwinding) { return close_scope(unwinding); }
extern "C" Item pn_io_sqlite_database(Item path) { return target_value(path, false); }
extern "C" Item pn_io_sqlite_existing(Item path) { return target_value(path, true); }

extern "C" Item pn_io_sqlite_execute3(Item path, Item sql, Item params) { return sqlite_statement(path, sql, params, false); }
extern "C" Item pn_io_sqlite_execute2(Item path, Item sql) { return sqlite_statement(path, sql, ItemNull, false); }
extern "C" Item pn_io_sqlite_query3(Item path, Item sql, Item params) { return sqlite_statement(path, sql, params, true); }
extern "C" Item pn_io_sqlite_query2(Item path, Item sql) { return sqlite_statement(path, sql, ItemNull, true); }

#endif
