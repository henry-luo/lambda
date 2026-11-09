#if !defined(LAMBDA_NO_FILE_IO) && !defined(LAMBDA_IME_RESOURCE_IO) && !defined(LAMBDA_NO_TASKS)
#include "io_rdb_value.hpp"
#include "io_file_search.hpp"
#include "lambda-number-runtime.hpp"
#include "lib/arraylist.h"
#include "lib/memtrack.h"
#include "lib/utf.h"
#include <limits.h>
#include <math.h>
#include <string.h>

void io_rdb_params_free(IoRdbParams* bound) {
    if (!bound->values) return;
    for (int i = 0; i < bound->count; i++) {
        if (bound->values[i].type == RDB_TYPE_STRING || bound->values[i].type == RDB_TYPE_BLOB)
            mem_free((void*)bound->values[i].str_val);
    }
    mem_free(bound->values);
}

static bool copy_param_bytes(size_t* total, size_t max_bytes, RdbValue* out, const char* bytes, size_t length) {
    if (length > INT_MAX || length > max_bytes - *total || (!bytes && length)) return false;
    *total += length;
    out->str_len = (int)length;
    if (!length) { out->str_val = NULL; return true; }
    char* copy = (char*)mem_alloc(length + 1, MEM_CAT_EVAL);
    if (!copy) return false;
    memcpy(copy, bytes, length);
    copy[length] = '\0';
    out->str_val = copy;
    return true;
}

bool io_rdb_bind_values(Item params, IoRdbParams* out, int max_count, size_t max_bytes) {
    TypeId type = get_type_id(params);
    if (type == LMD_TYPE_NULL) return true;
    if ((type != LMD_TYPE_ARRAY && type != LMD_TYPE_ARRAY_NUM) || vector_is_ndim(params)) return false;
    int64_t count = vector_length(params);
    if (count < 0 || count > max_count) return false;
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
                    !copy_param_bytes(&out->bytes, max_bytes, target, value.get_chars(), value.get_len())) return false;
        } else if (kind == LMD_TYPE_BINARY) {
            target->type = RDB_TYPE_BLOB;
            Binary* binary = value.get_binary();
            if (!copy_param_bytes(&out->bytes, max_bytes, target, (const char*)binary_data(binary), binary_length(binary))) return false;
        } else return false;
    }
    return true;
}

struct IoRdbRow {
    int count;
    RdbValue* values;
};

struct IoRdbRows {
    ArrayList* entries;
    int max_rows;
    int max_columns;
    size_t max_cells;
    size_t max_bytes;
    size_t cells;
    size_t bytes;
};

static void rdb_row_free(IoRdbRow* row) {
    if (!row) return;
    for (int i = 0; i < row->count; i++) {
        if (row->values[i].type == RDB_TYPE_STRING || row->values[i].type == RDB_TYPE_BLOB)
            mem_free((void*)row->values[i].str_val);
    }
    mem_free(row->values);
    mem_free(row);
}

IoRdbRows* io_rdb_rows_new(int max_rows, int max_columns, size_t max_cells, size_t max_bytes) {
    if (max_rows <= 0 || max_columns <= 0 || !max_cells || !max_bytes) return NULL;
    IoRdbRows* rows = (IoRdbRows*)mem_calloc(1, sizeof(IoRdbRows), MEM_CAT_EVAL);
    if (!rows) return NULL;
    rows->entries = arraylist_new(8);
    if (!rows->entries) { mem_free(rows); return NULL; }
    rows->max_rows = max_rows;
    rows->max_columns = max_columns;
    rows->max_cells = max_cells;
    rows->max_bytes = max_bytes;
    return rows;
}

bool io_rdb_rows_append(IoRdbRows* rows, const RdbValue* values, int count) {
    if (!rows || count < 0 || count > rows->max_columns || (count && !values) ||
            arraylist_length(rows->entries) >= rows->max_rows ||
            (size_t)count > rows->max_cells - rows->cells) return false;
    IoRdbRow* row = (IoRdbRow*)mem_calloc(1, sizeof(IoRdbRow), MEM_CAT_EVAL);
    if (!row) return false;
    row->count = count;
    row->values = count ? (RdbValue*)mem_calloc((size_t)count, sizeof(RdbValue), MEM_CAT_EVAL) : NULL;
    if (count && !row->values) { mem_free(row); return false; }
    size_t copied = 0;
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        row->values[i] = values[i];
        RdbValue* cell = &row->values[i];
        if (cell->type == RDB_TYPE_STRING || cell->type == RDB_TYPE_BLOB) {
            cell->str_val = NULL;
            if (values[i].str_len < 0 || (!values[i].str_val && values[i].str_len) ||
                    (size_t)values[i].str_len > rows->max_bytes - rows->bytes - copied ||
                    (cell->type == RDB_TYPE_STRING && values[i].str_len &&
                     !utf8_valid(values[i].str_val, (size_t)values[i].str_len))) {
                ok = false;
            } else if (values[i].str_len) {
                char* bytes = (char*)mem_alloc((size_t)values[i].str_len, MEM_CAT_EVAL);
                if (!bytes) ok = false;
                else {
                    memcpy(bytes, values[i].str_val, (size_t)values[i].str_len);
                    cell->str_val = bytes;
                    copied += (size_t)values[i].str_len;
                }
            }
        }
    }
    if (ok) ok = arraylist_append(rows->entries, row) != 0;
    if (!ok) { rdb_row_free(row); return false; }
    rows->cells += (size_t)count;
    rows->bytes += copied;
    return true;
}

int io_rdb_rows_count(const IoRdbRows* rows) {
    return rows ? arraylist_length(rows->entries) : 0;
}

RdbSpoolStatus io_rdb_rows_spool(const IoRdbRows* rows, RdbSpool* spool) {
    if (!rows || !spool) return RDB_SPOOL_INVALID;
    int count = arraylist_length(rows->entries);
    RdbSpoolRow* batch = count ? (RdbSpoolRow*)mem_calloc((size_t)count, sizeof(RdbSpoolRow), MEM_CAT_EVAL) : NULL;
    if (count && !batch) return RDB_SPOOL_MEMORY;
    for (int i = 0; i < count; i++) {
        IoRdbRow* row = (IoRdbRow*)arraylist_get(rows->entries, i);
        batch[i] = {row->count, row->values};
    }
    RdbSpoolStatus status = rdb_spool_append(spool, batch, count);
    mem_free(batch);
    return status;
}

bool io_rdb_rows_materialize(const IoRdbRows* rows, Rooted<Array*>* destination) {
    if (!rows || !destination) return false;
    RootFrame roots(1);
    Rooted<Array*> row(roots, (Array*)NULL);
    for (int r = 0; r < arraylist_length(rows->entries); r++) {
        IoRdbRow* source = (IoRdbRow*)arraylist_get(rows->entries, r);
        row.set(array_plain());
        if (!row.get()) return false;
        for (int i = 0; i < source->count; i++)
            if (!io_rdb_append_value(row.get(), &source->values[i])) return false;
        int64_t before = destination->get()->length;
        array_push_verbatim(destination->get(), (Item){.array = row.get()});
        if (destination->get()->length != before + 1) return false;
    }
    return true;
}

void io_rdb_rows_free(IoRdbRows* rows) {
    if (!rows) return;
    for (int i = 0; i < arraylist_length(rows->entries); i++)
        rdb_row_free((IoRdbRow*)arraylist_get(rows->entries, i));
    arraylist_free(rows->entries);
    mem_free(rows);
}

bool io_rdb_append_value(Array* destination, const RdbValue* value) {
    if (!destination || !value) return false;
    RootFrame roots(2);
    Rooted<Array*> row(roots, destination);
    Rooted<Item> cell(roots, ItemNull);
    if (value->is_null || value->type == RDB_TYPE_NULL) cell.set(ItemNull);
    else if (value->type == RDB_TYPE_BOOL) cell.set((Item){.item = b2it(value->bool_val)});
    else if (value->type == RDB_TYPE_INT) {
        // The source survives until array insertion rehomes a wide scalar.
        cell.set(value->int_val >= INT53_MIN && value->int_val <= INT53_MAX
            ? (Item){.item = i2it(value->int_val)} : (Item){.item = l2it(&value->int_val)});
    } else if (value->type == RDB_TYPE_FLOAT) {
        if (!isfinite(value->float_val)) return false;
        cell.set(lambda_float_ptr_to_item(&value->float_val));
    } else if (value->type == RDB_TYPE_STRING || value->type == RDB_TYPE_BLOB) {
        if (value->str_len < 0 || (!value->str_val && value->str_len)) return false;
        const char* bytes = value->str_val ? value->str_val : "";
        if (value->type == RDB_TYPE_STRING) {
            if (!utf8_valid(bytes, (size_t)value->str_len)) return false;
            cell.set(io_fs_make_string(bytes, (size_t)value->str_len));
        } else {
            Binary* binary = heap_binary_from_bytes(bytes, value->str_len);
            cell.set(binary ? (Item){.item = x2it(binary)} : ItemError);
        }
    } else return false;
    if (item_is_error(cell.get())) return false;
    int64_t before = row.get()->length;
    array_push_verbatim(row.get(), cell.get());
    return row.get()->length == before + 1;
}
#endif
