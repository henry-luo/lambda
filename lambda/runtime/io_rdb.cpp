// Task-owned, read-only RDB session. S12.4.2/S12.4.3 and D11.1.2.
#if !defined(LAMBDA_NO_FILE_IO) && !defined(LAMBDA_IME_RESOURCE_IO) && !defined(LAMBDA_NO_TASKS)
#include "io_rdb.hpp"
#include "io_rdb_value.hpp"
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "write_set.hpp"
#include "lib/rdb.h"
#include "lib/memtrack.h"
#include "lib/utf.h"
#include <limits.h>
#include <string.h>
#include <uv.h>

namespace {
constexpr size_t URI_LIMIT = 8192;
constexpr int SCHEMA_ROW_LIMIT = 100000;
constexpr size_t SCHEMA_BYTES_LIMIT = 16 * 1024 * 1024;
constexpr size_t SQL_LIMIT = 1024 * 1024;
constexpr int PARAM_LIMIT = 1024;
constexpr int FETCH_ROW_LIMIT = 256;
constexpr int FETCH_COLUMN_LIMIT = 1024;
constexpr uint64_t SPOOL_BYTE_LIMIT = 256ULL * 1024 * 1024;
constexpr uint64_t SPOOL_ROW_LIMIT = 1000000;
struct Target { char* uri; char* driver; };
struct Scope {
    LambdaTask* owner; Pool* pool; RdbConn* conn; RdbStmt* statement; Item handle; bool rooted;
    FileAnchor* export_anchor; FileAtomicWriter* export_writer;
    RdbSpool* spool; bool spool_failed; bool spool_sealed; bool fetched_any;
};
struct OpenJob { Scope* scope; RdbOpenWork* work; };
// Every cancellable job begins with this header; its native handle stays pinned.
struct Interrupt { const RdbDriver* driver; void* connection; int worker_finished; };
struct SchemaJob { Interrupt interrupt; Pool* pool; RdbSchemaWork* work; };
struct PrepareJob { Interrupt interrupt; IoRdbParams values; RdbParam* params; RdbPrepareWork* work; };
struct FetchJob { Interrupt interrupt; Pool* pool; IoRdbRows* rows; RdbFetchWork* work; };
struct FinalizeJob { RdbFinalizeWork* work; };
static char target_brand, scope_brand, attachment_key;

void cancel_native(void* data) {
    Interrupt* target = (Interrupt*)data;
    if (!target->driver->cancel) return;
    // An interrupt sent just before a query starts can otherwise be lost.
    while (!__atomic_load_n(&target->worker_finished, __ATOMIC_ACQUIRE)) {
        (void)target->driver->cancel(target->connection);
        if (!__atomic_load_n(&target->worker_finished, __ATOMIC_ACQUIRE)) uv_sleep(10);
    }
}

void wipe(char* text) {
    if (!text) return;
    size_t length = strlen(text);
    volatile char* bytes = text;
    while (length--) *bytes++ = 0;
    mem_free(text);
}
void destroy_target(void* data) {
    Target* target = (Target*)data;
    if (target) { wipe(target->uri); mem_free(target->driver); mem_free(target); }
}
static VMapVtable target_vtable = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, destroy_target, io_resource_trace};
static VMapVtable scope_vtable = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, io_resource_destroy, io_resource_trace};

Scope* current_scope() {
    LambdaTask* task = lambda_task_running();
    return task ? (Scope*)lambda_task_attachment_find(task, &attachment_key) : NULL;
}
Scope* scope_for(Item handle) {
    Scope* scope = current_scope();
    return scope && get_type_id(handle) == LMD_TYPE_VMAP && handle.vmap &&
        handle.vmap->host_type == &scope_brand && handle.vmap->host_data == scope ? scope : NULL;
}
void abort_export(Scope* scope);
void free_scope(Scope* scope) {
    if (!scope) return;
    abort_export(scope);
    rdb_spool_close(scope->spool);
    if (get_type_id(scope->handle) == LMD_TYPE_VMAP && scope->handle.vmap) {
        scope->handle.vmap->data = NULL;
        scope->handle.vmap->host_data = NULL;
    }
    if (scope->rooted) heap_unregister_gc_root(&scope->handle.item);
    if (scope->statement) rdb_finalize(scope->statement);
    if (scope->conn) rdb_close(scope->conn);
    if (scope->pool) pool_destroy(scope->pool);
    mem_free(scope);
}
void release_scope(void* data) { free_scope((Scope*)data); }

bool valid_text(Item value, size_t limit) {
    return get_type_id(value) == LMD_TYPE_STRING && value.get_len() && value.get_len() <= limit &&
        !memchr(value.get_chars(), 0, value.get_len()) && utf8_valid(value.get_chars(), value.get_len());
}
Item make_target(Item uri, Item driver) {
    if (!valid_text(uri, URI_LIMIT) || !valid_text(driver, 32))
        return io_fs_error(ERR_TYPE_MISMATCH, "io.rdb.database: expected a target and driver name");
    RootFrame roots(2);
    Rooted<Item> held_uri(roots, uri), held_driver(roots, driver);
    Target* target = (Target*)mem_calloc(1, sizeof(Target), MEM_CAT_EVAL);
    if (!target) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.database: allocation failed");
    target->uri = mem_dup_n(held_uri.get().get_chars(), held_uri.get().get_len(), MEM_CAT_EVAL);
    target->driver = mem_dup_n(held_driver.get().get_chars(), held_driver.get().get_len(), MEM_CAT_EVAL);
    bool valid = target->uri && target->driver &&
        (!strcmp(target->driver, "sqlite") || !strcmp(target->driver, "mysql") || !strcmp(target->driver, "postgresql"));
    // SQLite URI interpretation must not turn a file target into an alternate resource.
    if (valid && !strcmp(target->driver, "sqlite") && !strncmp(target->uri, "file:", 5)) valid = false;
    if (!valid) { destroy_target(target); return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.database: unsupported target or driver"); }
    VMap* result = io_resource_handle_new(&target_vtable, &target_brand, target);
    if (!result) { destroy_target(target); return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.database: allocation failed"); }
    return (Item){.vmap = result};
}

void run_open(LambdaOffload* offload, void* data) {
    OpenJob* job = (OpenJob*)data;
    (void)rdb_open_work_run(job->work, lambda_offload_cancel_flag(offload));
}
void release_open(void* data) {
    OpenJob* job = (OpenJob*)data;
    if (!job) return;
    if (job->work) (void)rdb_open_work_end(job->work, false);
    free_scope(job->scope);
    mem_free(job);
}
Item open_target(Item value) {
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("io.rdb.database", &denied)) return denied;
    LambdaTask* owner = lambda_task_running();
    if (!owner || current_scope() || write_set_transaction_open_here())
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb: session scope is unavailable or nested");
    RootFrame roots(2);
    Rooted<Item> held(roots, value), outcome(roots, ItemNull);
    Target* target = (Target*)held.get().vmap->host_data;
    if (!target) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb: invalid target");
    OpenJob* job = (OpenJob*)mem_calloc(1, sizeof(OpenJob), MEM_CAT_EVAL);
    Scope* scope = (Scope*)mem_calloc(1, sizeof(Scope), MEM_CAT_EVAL);
    if (!job || !scope) { mem_free(job); mem_free(scope); return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb: allocation failed"); }
    job->scope = scope;
    scope->owner = owner;
    scope->handle = ItemNull;
    scope->pool = pool_create();
    if (!strcmp(target->driver, "sqlite")) rdb_sqlite_register();
    job->work = scope->pool ? rdb_open_work_begin(scope->pool, target->uri, target->driver, true) : NULL;
    if (!job->work) { release_open(job); return io_fs_error(ERR_IO_ERROR, "io.rdb: target is unavailable or not authorized"); }
    static const LambdaOffloadOps ops = {run_open, NULL, release_open};
    outcome.set(lambda_task_offload(&ops, job, 0));
    scope->conn = rdb_open_work_end(job->work, !item_is_error(outcome.get()));
    job->work = NULL;
    if (item_is_error(outcome.get()) || !scope->conn) {
        release_open(job);
        return item_is_error(outcome.get()) ? outcome.get() : io_fs_error(ERR_IO_ERROR, "io.rdb: connection failed");
    }
    VMap* handle = io_resource_handle_new(&scope_vtable, &scope_brand, scope);
    if (!handle) { release_open(job); return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb: handle allocation failed"); }
    scope->handle = (Item){.vmap = handle};
    heap_register_gc_root(&scope->handle.item);
    scope->rooted = true;
    if (!lambda_task_attachment_install(owner, &attachment_key, scope, release_scope)) {
        release_open(job);
        return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb: cannot own session");
    }
    mem_free(job);
    return scope->handle;
}

void run_schema(LambdaOffload* offload, void* data) {
    SchemaJob* job = (SchemaJob*)data;
    (void)rdb_schema_work_run(job->work, lambda_offload_cancel_flag(offload));
    __atomic_store_n(&job->interrupt.worker_finished, 1, __ATOMIC_RELEASE);
}
void release_schema(void* data) {
    SchemaJob* job = (SchemaJob*)data;
    if (!job) return;
    if (job->work) (void)rdb_schema_work_end(job->work, NULL);
    if (job->pool) pool_destroy(job->pool);
    mem_free(job);
}

bool schema_string(RdbValue* value, const char* text) {
    if (!text) { value->type = RDB_TYPE_NULL; value->is_null = true; return true; }
    size_t length = strlen(text);
    if (length > INT_MAX) return false;
    value->type = RDB_TYPE_STRING;
    value->str_val = text;
    value->str_len = (int)length;
    return true;
}
bool copy_schema(IoRdbRows* rows, const RdbSchema* schema) {
    for (int t = 0; t < schema->table_count; t++) {
        const RdbTable* table = &schema->tables[t];
        int count = table->column_count ? table->column_count : 1;
        for (int c = 0; c < count; c++) {
            const RdbColumn* col = table->column_count ? &table->columns[c] : NULL;
            RdbValue value[6] = {};
            if (!schema_string(&value[0], table->name) || !schema_string(&value[2], col ? col->name : NULL) ||
                    !schema_string(&value[3], col ? col->type_decl : NULL)) return false;
            value[1].type = RDB_TYPE_BOOL; value[1].bool_val = table->is_view;
            value[4].type = RDB_TYPE_BOOL; value[4].bool_val = col && col->nullable;
            value[5].type = RDB_TYPE_INT; value[5].int_val = col ? col->pk_index : 0;
            if (!io_rdb_rows_append(rows, value, 6)) return false;
        }
    }
    return true;
}
bool copy_details(IoRdbRows* rows, const RdbSchema* schema) {
    for (int t = 0; t < schema->table_count; t++) {
        const RdbTable* table = &schema->tables[t];
        for (int i = 0; i < table->index_count; i++) {
            const RdbIndex* index = &table->indexes[i];
            int count = index->column_count ? index->column_count : 1;
            for (int c = 0; c < count; c++) {
                RdbValue value[5] = {};
                if (!schema_string(&value[0], table->name) || !schema_string(&value[1], "index") ||
                    !schema_string(&value[2], index->name) ||
                    !schema_string(&value[3], index->column_count ? index->columns[c] : NULL)) return false;
                value[4].type = RDB_TYPE_BOOL; value[4].bool_val = index->unique;
                if (!io_rdb_rows_append(rows, value, 5)) return false;
            }
        }
        for (int f = 0; f < table->fk_count; f++) {
            const RdbForeignKey* fk = &table->foreign_keys[f];
            RdbValue value[5] = {};
            if (!schema_string(&value[0], table->name) || !schema_string(&value[1], "foreign_key") ||
                !schema_string(&value[2], fk->column) || !schema_string(&value[3], fk->ref_table) ||
                !schema_string(&value[4], fk->ref_column) || !io_rdb_rows_append(rows, value, 5)) return false;
        }
    }
    return true;
}
Item schema_value(Item handle, bool details) {
    Scope* scope = scope_for(handle);
    if (!scope)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.schema: expected this task's active session");
    if (scope->statement)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.schema: finish the current result first");
    RootFrame roots(3);
    Rooted<Item> held(roots, handle), outcome(roots, ItemNull);
    Rooted<Array*> result(roots, array_plain());
    if (!result.get()) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.schema: result allocation failed");
    SchemaJob* job = (SchemaJob*)mem_calloc(1, sizeof(SchemaJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.schema: job allocation failed");
    job->interrupt = {scope->conn->driver, scope->conn->handle, 0};
    job->pool = pool_create();
    job->work = job->pool ? rdb_schema_work_begin(scope->conn, job->pool) : NULL;
    if (!job->work) { release_schema(job); return io_fs_error(ERR_IO_ERROR, "io.rdb.schema: session is unavailable"); }
    static const LambdaOffloadOps ops = {run_schema, NULL, release_schema, cancel_native};
    outcome.set(lambda_task_offload(&ops, job, 0));
    RdbSchema schema = {};
    int status = rdb_schema_work_end(job->work, &schema);
    job->work = NULL;
    int columns = details ? 5 : 6;
    IoRdbRows* rows = io_rdb_rows_new(SCHEMA_ROW_LIMIT, columns, (size_t)SCHEMA_ROW_LIMIT * columns, SCHEMA_BYTES_LIMIT);
    bool ok = !item_is_error(outcome.get()) && status == RDB_OK && rows &&
        (details ? copy_details(rows, &schema) : copy_schema(rows, &schema)) && io_rdb_rows_materialize(rows, &result);
    io_rdb_rows_free(rows);
    release_schema(job);
    if (item_is_error(outcome.get())) return outcome.get();
    return ok ? (Item){.array = result.get()} : io_fs_error(ERR_IO_ERROR, "io.rdb.schema: load failed or result exceeded its limits");
}

bool prepare_params(PrepareJob* job, Item params) {
    if (!io_rdb_bind_values(params, &job->values, PARAM_LIMIT, SCHEMA_BYTES_LIMIT)) return false;
    int count = job->values.count;
    job->params = count ? (RdbParam*)mem_calloc((size_t)count, sizeof(RdbParam), MEM_CAT_EVAL) : NULL;
    if (count && !job->params) return false;
    for (int i = 0; i < count; i++) {
        RdbValue* source = &job->values.values[i];
        RdbParam* target = &job->params[i];
        target->type = source->type;
        if (source->type == RDB_TYPE_INT) target->int_val = source->int_val;
        else if (source->type == RDB_TYPE_FLOAT) target->float_val = source->float_val;
        else if (source->type == RDB_TYPE_BOOL) target->bool_val = source->bool_val;
        else if (source->type == RDB_TYPE_STRING) {
            // The current common driver ABI has no parameter byte length.
            if (source->str_len && memchr(source->str_val, 0, (size_t)source->str_len)) return false;
            target->str_val = source->str_val ? source->str_val : "";
        } else if (source->type != RDB_TYPE_NULL) return false;
    }
    return true;
}
void run_prepare(LambdaOffload* offload, void* data) {
    PrepareJob* job = (PrepareJob*)data;
    (void)rdb_prepare_work_run(job->work, lambda_offload_cancel_flag(offload));
    __atomic_store_n(&job->interrupt.worker_finished, 1, __ATOMIC_RELEASE);
}
void release_prepare(void* data) {
    PrepareJob* job = (PrepareJob*)data;
    if (!job) return;
    if (job->work) (void)rdb_prepare_work_end(job->work, false);
    io_rdb_params_free(&job->values);
    mem_free(job->params);
    mem_free(job);
}
Item execute_value(Item handle, Item sql, Item params) {
    Scope* scope = scope_for(handle);
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.execute: expected this task's active session");
    if (scope->statement) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.execute: finish the current result first");
    if (!valid_text(sql, SQL_LIMIT)) return io_fs_error(ERR_TYPE_MISMATCH, "io.rdb.execute: expected SQL text");
    rdb_spool_close(scope->spool);
    scope->spool = NULL; scope->spool_failed = false; scope->spool_sealed = false; scope->fetched_any = false;
    RootFrame roots(4);
    Rooted<Item> held_handle(roots, handle), held_sql(roots, sql), held_params(roots, params), outcome(roots, ItemNull);
    PrepareJob* job = (PrepareJob*)mem_calloc(1, sizeof(PrepareJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.execute: allocation failed");
    job->interrupt = {scope->conn->driver, scope->conn->handle, 0};
    char* text = mem_dup_n(held_sql.get().get_chars(), held_sql.get().get_len(), MEM_CAT_EVAL);
    bool valid = text && prepare_params(job, held_params.get());
    job->work = valid ? rdb_prepare_work_begin(scope->conn, text, job->params, job->values.count) : NULL;
    mem_free(text);
    if (!job->work) {
        release_prepare(job);
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.execute: invalid parameters or unavailable session");
    }
    static const LambdaOffloadOps ops = {run_prepare, NULL, release_prepare, cancel_native};
    outcome.set(lambda_task_offload(&ops, job, 0));
    scope->statement = rdb_prepare_work_end(job->work, !item_is_error(outcome.get()));
    job->work = NULL;
    bool ok = scope->statement != NULL;
    release_prepare(job);
    if (item_is_error(outcome.get())) return outcome.get();
    return ok ? ItemNull : io_fs_error(ERR_IO_ERROR, "io.rdb.execute: statement preparation failed");
}

bool capture_row(void* data, int count, const RdbValue* values) {
    return io_rdb_rows_append((IoRdbRows*)data, values, count);
}
void run_fetch(LambdaOffload* offload, void* data) {
    FetchJob* job = (FetchJob*)data;
    (void)rdb_fetch_work_run(job->work, lambda_offload_cancel_flag(offload));
    __atomic_store_n(&job->interrupt.worker_finished, 1, __ATOMIC_RELEASE);
}
void release_fetch(void* data) {
    FetchJob* job = (FetchJob*)data;
    if (!job) return;
    if (job->work) (void)rdb_fetch_work_end(job->work, NULL);
    io_rdb_rows_free(job->rows);
    if (job->pool) pool_destroy(job->pool);
    mem_free(job);
}
bool append_array(Array* destination, Array* value) {
    RootFrame roots(2);
    Rooted<Array*> held_destination(roots, destination), held_value(roots, value);
    int64_t before = held_destination.get()->length;
    array_push_verbatim(held_destination.get(), (Item){.array = held_value.get()});
    return held_destination.get()->length == before + 1;
}
Item fetch_value(Item handle, Item requested) {
    Scope* scope = scope_for(handle);
    if (!scope || !scope->statement)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.fetch: expected an active result");
    if (scope->spool_failed)
        return io_fs_error(ERR_IO_ERROR, "io.rdb.fetch: result spool is incomplete");
    int64_t limit = 0;
    if (!lambda_item_to_int64_exact(requested, &limit) || limit < 1 || limit > FETCH_ROW_LIMIT)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.fetch: batch size must be between 1 and 256");
    RootFrame roots(6);
    Rooted<Item> held(roots, handle), outcome(roots, ItemNull);
    Rooted<Array*> result(roots, array_plain()), columns(roots, array_plain()), rows(roots, array_plain()), column(roots, (Array*)NULL);
    if (!result.get() || !columns.get() || !rows.get())
        return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.fetch: result allocation failed");
    FetchJob* job = (FetchJob*)mem_calloc(1, sizeof(FetchJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.fetch: job allocation failed");
    job->interrupt = {scope->conn->driver, scope->conn->handle, 0};
    job->pool = pool_create();
    job->rows = io_rdb_rows_new((int)limit, FETCH_COLUMN_LIMIT, (size_t)limit * FETCH_COLUMN_LIMIT, SCHEMA_BYTES_LIMIT);
    job->work = job->pool && job->rows ? rdb_fetch_work_begin(scope->statement, (int)limit,
        FETCH_COLUMN_LIMIT, job->pool, capture_row, job->rows) : NULL;
    if (!job->work) { release_fetch(job); return io_fs_error(ERR_IO_ERROR, "io.rdb.fetch: result is unavailable"); }
    static const LambdaOffloadOps ops = {run_fetch, NULL, release_fetch, cancel_native};
    outcome.set(lambda_task_offload(&ops, job, 0));
    RdbFetchResult fetched = {};
    int status = rdb_fetch_work_end(job->work, &fetched);
    job->work = NULL;
    bool ok = !item_is_error(outcome.get()) && status == RDB_OK;
    scope->fetched_any = true;
    // A consumed batch must enter the spool intact before the caller sees it.
    if (scope->spool && !scope->spool_sealed && (!ok || io_rdb_rows_spool(job->rows, scope->spool) != RDB_SPOOL_OK)) {
        scope->spool_failed = true;
        ok = false;
    }
    for (int i = 0; ok && i < fetched.column_count; i++) {
        column.set(array_plain());
        RdbValue name = {}, type = {};
        ok = column.get() && schema_string(&name, fetched.columns[i].name) &&
            schema_string(&type, rdb_type_name(fetched.columns[i].type)) &&
            io_rdb_append_value(column.get(), &name) && io_rdb_append_value(column.get(), &type) &&
            append_array(columns.get(), column.get());
    }
    RdbValue done = {};
    done.type = RDB_TYPE_BOOL; done.bool_val = fetched.done;
    if (ok) ok = io_rdb_rows_materialize(job->rows, &rows) && append_array(result.get(), columns.get()) &&
        append_array(result.get(), rows.get()) && io_rdb_append_value(result.get(), &done);
    if (!ok && scope->spool && !scope->spool_sealed) scope->spool_failed = true;
    release_fetch(job);
    if (item_is_error(outcome.get())) return outcome.get();
    return ok ? (Item){.array = result.get()} : io_fs_error(ERR_IO_ERROR,
        scope->spool_failed ? "io.rdb.fetch: result spool is incomplete or exceeded its quota" :
        "io.rdb.fetch: query failed or batch exceeded its limits");
}

Item spool_begin_value(Item handle, Item path) {
    Scope* scope = scope_for(handle);
    if (!scope || !scope->statement || scope->spool || scope->fetched_any)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.spool_begin: expected an unfetched result");
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("io.rdb.spool_begin", &denied)) return denied;
    if (lambda_dry_run_enabled())
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.spool_begin: unavailable in dry-run mode");
    RootFrame roots(2);
    Rooted<Item> held_handle(roots, handle), held_path(roots, path);
    FileAnchor* parent = NULL;
    char* leaf = NULL;
    // A private temporary spool has no durable publication requirement.
    Item opened = io_resource_create_anchor(held_path.get(), "io.rdb.spool_begin", scope->owner, 0, &parent, &leaf);
    if (item_is_error(opened)) return opened;
    FileAnchor* directory = file_anchor_directory(parent, leaf, true, FILE_ANCHOR_PRIVATE);
    mem_free(leaf);
    file_anchor_close(parent);
    if (!directory) return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.spool_begin: cannot open private spool directory");
    scope->spool = rdb_spool_open(directory, SPOOL_BYTE_LIMIT, SPOOL_ROW_LIMIT, FETCH_COLUMN_LIMIT);
    file_anchor_close(directory);
    if (!scope->spool) return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.spool_begin: cannot create result spool");
    return ItemNull;
}

Item spool_read_value(Item handle, Item first_item, Item count_item) {
    Scope* scope = scope_for(handle);
    int64_t first = 0, count = 0;
    if (!scope || !scope->spool || !lambda_item_to_int64_exact(first_item, &first) || first < 0 ||
            !lambda_item_to_int64_exact(count_item, &count) || count < 0 || count > FETCH_ROW_LIMIT ||
            (uint64_t)first > rdb_spool_count(scope->spool) ||
            (uint64_t)count > rdb_spool_count(scope->spool) - (uint64_t)first)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.spool_read: invalid result window");
    RootFrame roots(2);
    Rooted<Item> held(roots, handle);
    Rooted<Array*> result(roots, array_plain());
    IoRdbRows* rows = io_rdb_rows_new(FETCH_ROW_LIMIT, FETCH_COLUMN_LIMIT,
        (size_t)FETCH_ROW_LIMIT * FETCH_COLUMN_LIMIT, SCHEMA_BYTES_LIMIT);
    bool ok = result.get() && rows && rdb_spool_read(scope->spool, (uint64_t)first, (int)count, capture_row, rows) == RDB_SPOOL_OK &&
        io_rdb_rows_materialize(rows, &result);
    io_rdb_rows_free(rows);
    return ok ? (Item){.array = result.get()} : io_fs_error(ERR_IO_ERROR, "io.rdb.spool_read: cannot read result window");
}

Item spool_close_value(Item handle) {
    Scope* scope = scope_for(handle);
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.spool_close: expected this task's active session");
    rdb_spool_close(scope->spool);
    scope->spool = NULL; scope->spool_failed = false; scope->spool_sealed = false;
    return ItemNull;
}

Item spool_seal_value(Item handle) {
    Scope* scope = scope_for(handle);
    if (!scope || !scope->spool || scope->spool_failed)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.spool_seal: expected a complete retained prefix");
    // Complete export may drain an arbitrarily long cursor without growing the prefix.
    scope->spool_sealed = true;
    return ItemNull;
}

void run_finalize(LambdaOffload* offload, void* data) {
    (void)offload;
    rdb_finalize_work_run(((FinalizeJob*)data)->work);
}
void release_finalize(void* data) {
    FinalizeJob* job = (FinalizeJob*)data;
    if (!job) return;
    rdb_finalize_work_end(job->work);
    mem_free(job);
}
Item finish_value(Item handle) {
    Scope* scope = scope_for(handle);
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.finish: expected this task's active session");
    if (!scope->statement) return ItemNull;
    RootFrame roots(2);
    Rooted<Item> held(roots, handle), outcome(roots, ItemNull);
    FinalizeJob* job = (FinalizeJob*)mem_calloc(1, sizeof(FinalizeJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.rdb.finish: job allocation failed");
    job->work = rdb_finalize_work_begin(scope->statement);
    if (!job->work) { mem_free(job); return io_fs_error(ERR_IO_ERROR, "io.rdb.finish: result is unavailable"); }
    scope->statement = NULL;
    static const LambdaOffloadOps ops = {run_finalize, NULL, release_finalize};
    outcome.set(lambda_task_offload(&ops, job, 0));
    release_finalize(job);
    return outcome.get();
}

void abort_export(Scope* scope) {
    if (scope->export_writer) file_anchor_write_abort(scope->export_writer);
    if (scope->export_anchor) file_anchor_close(scope->export_anchor);
    scope->export_writer = NULL;
    scope->export_anchor = NULL;
}

Item export_begin_value(Item handle, Item target) {
    Scope* scope = scope_for(handle);
    if (!scope || (!scope->statement && !scope->spool) || scope->export_writer)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_begin: expected a retained result and no export");
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("io.rdb.export_begin", &denied)) return denied;
    if (lambda_dry_run_enabled())
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_begin: unavailable in dry-run mode");
    RootFrame roots(2);
    Rooted<Item> held_handle(roots, handle), held_target(roots, target);
    char* leaf = NULL;
    Item opened = io_resource_create_anchor(held_target.get(), "io.rdb.export_begin", scope->owner,
                                             FILE_ANCHOR_FULL_SYNC, &scope->export_anchor, &leaf);
    if (item_is_error(opened)) return opened;
    scope->export_writer = file_anchor_write_begin(scope->export_anchor, leaf, NULL);
    mem_free(leaf);
    if (!scope->export_writer) {
        abort_export(scope);
        return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.export_begin: cannot stage a new destination");
    }
    return ItemNull;
}

Item export_write_value(Item handle, Item content) {
    Scope* scope = scope_for(handle);
    if (!scope || !scope->export_writer)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_write: no staged export");
    TypeId type = get_type_id(content);
    if (type != LMD_TYPE_STRING && type != LMD_TYPE_BINARY)
        return io_fs_error(ERR_TYPE_MISMATCH, "io.rdb.export_write: expected text or binary");
    const void* bytes = NULL;
    size_t size = 0;
    if (type == LMD_TYPE_STRING) {
        String* text = content.get_safe_string();
        if (text) { bytes = text->chars; size = text->len; }
    } else {
        Binary* binary = content.get_safe_binary();
        if (binary) { bytes = binary_data(binary); size = binary_length(binary); }
    }
    if (size > SCHEMA_BYTES_LIMIT) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_write: chunk too large");
    if (!bytes && !size) bytes = "";
    if (lambda_task_cancel_requested(scope->owner) || !bytes || !file_anchor_write_chunk(scope->export_writer, bytes, size)) {
        abort_export(scope);
        return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.export_write: staged write failed");
    }
    return ItemNull;
}

Item export_commit_value(Item handle) {
    Scope* scope = scope_for(handle);
    if (!scope || !scope->export_writer)
        return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_commit: no staged export");
    FileAtomicWriter* writer = scope->export_writer;
    scope->export_writer = NULL;
    int status = file_anchor_write_commit(writer);
    abort_export(scope);
    if (status == 1) return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.export_commit: file published but directory sync failed");
    if (status != 0) return io_fs_error(ERR_FILE_WRITE_ERROR, "io.rdb.export_commit: file was not published");
    return ItemNull;
}

Item export_abort_value(Item handle) {
    Scope* scope = scope_for(handle);
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb.export_abort: expected this task's active session");
    abort_export(scope);
    return ItemNull;
}
} // namespace

bool io_rdb_is_target(Item value) {
    return get_type_id(value) == LMD_TYPE_VMAP && value.vmap && value.vmap->host_type == &target_brand;
}
bool io_rdb_scope_open_here(void) { return current_scope() != NULL; }
Item io_rdb_open(Item target) { return open_target(target); }
Item io_rdb_close(Bool unwinding) {
    (void)unwinding;
    LambdaTask* task = lambda_task_running();
    Scope* scope = task ? (Scope*)lambda_task_attachment_take(task, &attachment_key) : NULL;
    if (!scope) return io_fs_error(ERR_INVALID_OPERATION, "io.rdb: no active session");
    free_scope(scope);
    return ItemNull;
}
// S11.4.3: preserve an error operand before acquiring or mutating resources.
extern "C" Item pn_io_rdb_database(Item uri, Item driver) { GUARD_ERROR2(uri, driver); return make_target(uri, driver); }
extern "C" Item pn_io_rdb_schema(Item handle) { GUARD_ERROR1(handle); return schema_value(handle, false); }
extern "C" Item pn_io_rdb_details(Item handle) { GUARD_ERROR1(handle); return schema_value(handle, true); }
extern "C" Item pn_io_rdb_execute2(Item handle, Item sql) { GUARD_ERROR2(handle, sql); return execute_value(handle, sql, ItemNull); }
extern "C" Item pn_io_rdb_execute3(Item handle, Item sql, Item params) { GUARD_ERROR3(handle, sql, params); return execute_value(handle, sql, params); }
extern "C" Item pn_io_rdb_fetch(Item handle, Item count) { GUARD_ERROR2(handle, count); return fetch_value(handle, count); }
extern "C" Item pn_io_rdb_finish(Item handle) { GUARD_ERROR1(handle); return finish_value(handle); }
extern "C" Item pn_io_rdb_spool_begin(Item handle, Item path) { GUARD_ERROR2(handle, path); return spool_begin_value(handle, path); }
extern "C" Item pn_io_rdb_spool_read(Item handle, Item first, Item count) { GUARD_ERROR3(handle, first, count); return spool_read_value(handle, first, count); }
extern "C" Item pn_io_rdb_spool_close(Item handle) { GUARD_ERROR1(handle); return spool_close_value(handle); }
extern "C" Item pn_io_rdb_spool_seal(Item handle) { GUARD_ERROR1(handle); return spool_seal_value(handle); }
extern "C" Item pn_io_rdb_export_begin(Item handle, Item path) { GUARD_ERROR2(handle, path); return export_begin_value(handle, path); }
extern "C" Item pn_io_rdb_export_write(Item handle, Item content) { GUARD_ERROR2(handle, content); return export_write_value(handle, content); }
extern "C" Item pn_io_rdb_export_commit(Item handle) { GUARD_ERROR1(handle); return export_commit_value(handle); }
extern "C" Item pn_io_rdb_export_abort(Item handle) { GUARD_ERROR1(handle); return export_abort_value(handle); }
#endif
