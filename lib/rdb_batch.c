/**
 * @file rdb_batch.c
 * @brief Host shredding adapter: turns a row driver's step/column_value
 *        stream into Arrow C Data Interface column batches (RDB7), so
 *        rdb_fetch_batch() is one interface for row and column drivers.
 *
 * Column types are fixed once per statement: the declared type when the
 * driver reports one, otherwise the type of the first row's value (that row
 * is held as a lookahead and emitted first), and utf8 when still unknown.
 * Batches own every buffer, so they outlive the statement (Arrow release
 * semantics).
 */

#include "rdb_batch.h"
#include "log.h"
#include "memtrack.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SHRED_I64,
    SHRED_F64,
    SHRED_BOOL,
    SHRED_UTF8,
    SHRED_BINARY,
} ShredKind;

typedef struct {
    char*     name;
    ShredKind kind;
    RdbType   tag;      /* DECIMAL / DATETIME / JSON keep their meaning as field metadata */
} ShredColumn;

struct RdbBatchShred {
    int          column_count;
    ShredColumn* columns;
    RdbValue*    lookahead;     /* owned copy of the row consumed while typing columns */
    bool         done;
};

/* ══════════════════════════════════════════════════════════════════════
 * Column typing
 * ══════════════════════════════════════════════════════════════════════ */

static ShredKind shred_kind_for(RdbType type) {
    switch (type) {
        case RDB_TYPE_INT:   return SHRED_I64;
        case RDB_TYPE_FLOAT: return SHRED_F64;
        case RDB_TYPE_BOOL:  return SHRED_BOOL;
        case RDB_TYPE_BLOB:  return SHRED_BINARY;
        default:             return SHRED_UTF8;
    }
}

static bool shred_type_known(RdbType type) {
    return type != RDB_TYPE_UNKNOWN && type != RDB_TYPE_NULL;
}

static bool shred_is_text(RdbType type) {
    return type == RDB_TYPE_STRING || type == RDB_TYPE_DECIMAL ||
        type == RDB_TYPE_DATETIME || type == RDB_TYPE_JSON;
}

/** cell payloads that point into driver memory valid only until the next step */
static bool shred_has_bytes(RdbType type) {
    return shred_is_text(type) || type == RDB_TYPE_BLOB;
}

static void shred_free_row(RdbValue* row, int count) {
    if (!row) return;
    for (int i = 0; i < count; i++) {
        if (!row[i].is_null && row[i].str_val && shred_has_bytes(row[i].type)) {
            mem_free((void*)row[i].str_val);
        }
    }
    mem_free(row);
}

/** copy the current row; driver strings are only valid until the next step */
static RdbValue* shred_copy_row(RdbStmt* stmt, int count) {
    RdbValue* row = (RdbValue*)mem_calloc((size_t)(count > 0 ? count : 1), sizeof(RdbValue),
                                          MEM_CAT_INPUT_OTHER);
    if (!row) return NULL;
    for (int i = 0; i < count; i++) {
        row[i] = rdb_column_value(stmt, i);
        if (row[i].is_null || !row[i].str_val) continue;
        if (shred_has_bytes(row[i].type)) {
            char* copy = (char*)mem_alloc((size_t)row[i].str_len + 1, MEM_CAT_INPUT_OTHER);
            memcpy(copy, row[i].str_val, (size_t)row[i].str_len);
            copy[row[i].str_len] = '\0';
            row[i].str_val = copy;
        }
    }
    return row;
}

void rdb_batch_shred_free(RdbBatchShred* st) {
    if (!st) return;
    for (int i = 0; i < st->column_count; i++) mem_free(st->columns[i].name);
    mem_free(st->columns);
    shred_free_row(st->lookahead, st->column_count);
    mem_free(st);
}

static int shred_init(RdbStmt* stmt, RdbBatchShred** state) {
    if (*state) return RDB_OK;
    int count = rdb_column_count(stmt);
    RdbBatchShred* st = (RdbBatchShred*)mem_calloc(1, sizeof(RdbBatchShred), MEM_CAT_INPUT_OTHER);
    st->column_count = count;
    st->columns = (ShredColumn*)mem_calloc((size_t)(count > 0 ? count : 1), sizeof(ShredColumn),
                                           MEM_CAT_INPUT_OTHER);
    bool need_lookahead = false;
    for (int i = 0; i < count; i++) {
        RdbColumnDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.type = RDB_TYPE_UNKNOWN;
        if (rdb_column_desc(stmt, i, &desc) != RDB_OK) desc.type = RDB_TYPE_UNKNOWN;
        char fallback[32];
        snprintf(fallback, sizeof(fallback), "col%d", i);
        st->columns[i].name = mem_strdup(desc.name ? desc.name : fallback, MEM_CAT_INPUT_OTHER);
        st->columns[i].tag = desc.type;
        st->columns[i].kind = shred_kind_for(desc.type);
        if (!shred_type_known(desc.type)) need_lookahead = true;
    }
    if (need_lookahead) {
        // expression columns carry no declaration: type them from the first row
        int rc = rdb_step(stmt);
        if (rc == RDB_ROW) {
            st->lookahead = shred_copy_row(stmt, count);
        } else if (rc == RDB_DONE) {
            st->done = true;
        } else {
            rdb_batch_shred_free(st);
            return rc;
        }
        for (int i = 0; i < count; i++) {
            if (shred_type_known(st->columns[i].tag)) continue;
            RdbType seen = st->lookahead && !st->lookahead[i].is_null ?
                st->lookahead[i].type : RDB_TYPE_STRING;
            st->columns[i].tag = seen;
            st->columns[i].kind = shred_kind_for(seen);
        }
    }
    *state = st;
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Arrow schema export
 * ══════════════════════════════════════════════════════════════════════ */

static const char* shred_format(ShredKind kind) {
    switch (kind) {
        case SHRED_I64:    return "l";
        case SHRED_F64:    return "g";
        case SHRED_BOOL:   return "b";
        case SHRED_BINARY: return "z";
        default:           return "u";
    }
}

static const char* shred_tag_name(RdbType tag) {
    switch (tag) {
        case RDB_TYPE_DECIMAL:  return "decimal";
        case RDB_TYPE_DATETIME: return "datetime";
        case RDB_TYPE_JSON:     return "json";
        default:                return NULL;
    }
}

/** Arrow metadata: int32 pair count, then int32-length-prefixed key/value bytes */
static char* shred_metadata(RdbType tag) {
    const char* value = shred_tag_name(tag);
    if (!value) return NULL;
    static const char key[] = "lambda.rdb_type";
    int32_t pairs = 1, key_len = (int32_t)strlen(key), value_len = (int32_t)strlen(value);
    size_t size = 3 * sizeof(int32_t) + (size_t)key_len + (size_t)value_len;
    char* meta = (char*)mem_alloc(size, MEM_CAT_INPUT_OTHER);
    char* p = meta;
    memcpy(p, &pairs, sizeof(int32_t));     p += sizeof(int32_t);
    memcpy(p, &key_len, sizeof(int32_t));   p += sizeof(int32_t);
    memcpy(p, key, (size_t)key_len);        p += key_len;
    memcpy(p, &value_len, sizeof(int32_t)); p += sizeof(int32_t);
    memcpy(p, value, (size_t)value_len);
    return meta;
}

static void shred_schema_release(struct ArrowSchema* schema) {
    if (!schema || !schema->release) return;
    for (int64_t i = 0; i < schema->n_children; i++) {
        struct ArrowSchema* child = schema->children[i];
        if (child->release) child->release(child);
        mem_free(child);
    }
    mem_free(schema->children);
    mem_free((void*)schema->name);
    mem_free((void*)schema->metadata);
    schema->release = NULL;
}

int rdb_batch_shred_schema(RdbStmt* stmt, RdbBatchShred** state, struct ArrowSchema* out) {
    int rc = shred_init(stmt, state);
    if (rc != RDB_OK) return rc;
    RdbBatchShred* st = *state;
    memset(out, 0, sizeof(*out));
    out->format = "+s";
    out->n_children = st->column_count;
    out->children = (struct ArrowSchema**)mem_calloc((size_t)(st->column_count > 0 ?
        st->column_count : 1), sizeof(struct ArrowSchema*), MEM_CAT_INPUT_OTHER);
    for (int i = 0; i < st->column_count; i++) {
        struct ArrowSchema* child = (struct ArrowSchema*)mem_calloc(1, sizeof(struct ArrowSchema),
                                                                    MEM_CAT_INPUT_OTHER);
        child->format = shred_format(st->columns[i].kind);
        child->name = mem_strdup(st->columns[i].name, MEM_CAT_INPUT_OTHER);
        child->metadata = shred_metadata(st->columns[i].tag);
        child->flags = ARROW_FLAG_NULLABLE;
        child->release = shred_schema_release;
        out->children[i] = child;
    }
    out->release = shred_schema_release;
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Column builders
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t* validity;
    uint8_t* data;          /* fixed-width values, bit-packed bools, or utf8/binary bytes */
    size_t   data_len;
    size_t   data_cap;
    int32_t* offsets;       /* utf8/binary only */
    int64_t  null_count;
} ShredBuilder;

static void shred_builder_init(ShredBuilder* b, ShredKind kind, int64_t rows) {
    memset(b, 0, sizeof(*b));
    size_t bitmap = (size_t)((rows + 7) / 8);
    b->validity = (uint8_t*)mem_calloc(bitmap ? bitmap : 1, 1, MEM_CAT_INPUT_OTHER);
    if (kind == SHRED_I64 || kind == SHRED_F64) {
        b->data = (uint8_t*)mem_calloc((size_t)rows, 8, MEM_CAT_INPUT_OTHER);
    } else if (kind == SHRED_BOOL) {
        b->data = (uint8_t*)mem_calloc(bitmap ? bitmap : 1, 1, MEM_CAT_INPUT_OTHER);
    } else {
        b->offsets = (int32_t*)mem_calloc((size_t)rows + 1, sizeof(int32_t), MEM_CAT_INPUT_OTHER);
        b->data_cap = 256;
        b->data = (uint8_t*)mem_alloc(b->data_cap, MEM_CAT_INPUT_OTHER);
    }
}

static void shred_builder_free(ShredBuilder* b) {
    mem_free(b->validity);
    mem_free(b->data);
    mem_free(b->offsets);
}

static void shred_bytes(ShredBuilder* b, int64_t row, const char* s, size_t n) {
    if (b->data_len + n > b->data_cap) {
        while (b->data_len + n > b->data_cap) b->data_cap *= 2;
        b->data = (uint8_t*)mem_realloc(b->data, b->data_cap, MEM_CAT_INPUT_OTHER);
    }
    memcpy(b->data + b->data_len, s, n);
    b->data_len += n;
    b->offsets[row + 1] = (int32_t)b->data_len;
}

/** append one cell; RDB_ERROR when the value cannot be held by its column */
static int shred_append(ShredBuilder* b, const ShredColumn* col, int64_t row, const RdbValue* v) {
    bool variable = col->kind == SHRED_UTF8 || col->kind == SHRED_BINARY;
    if (variable) b->offsets[row + 1] = b->offsets[row];
    if (v->is_null || v->type == RDB_TYPE_NULL) {
        b->null_count++;
        return RDB_OK;
    }
    bool ok = true;
    switch (col->kind) {
        case SHRED_I64: {
            int64_t x = 0;
            if (v->type == RDB_TYPE_INT) x = v->int_val;
            else if (v->type == RDB_TYPE_BOOL) x = v->bool_val ? 1 : 0;
            else if (v->type == RDB_TYPE_FLOAT && v->float_val >= -9.2e18 && v->float_val <= 9.2e18 &&
                     v->float_val == (double)(int64_t)v->float_val) {
                x = (int64_t)v->float_val;
            } else ok = false;
            if (ok) memcpy(b->data + row * 8, &x, 8);
            break;
        }
        case SHRED_F64: {
            double x = 0;
            if (v->type == RDB_TYPE_FLOAT) x = v->float_val;
            else if (v->type == RDB_TYPE_INT) x = (double)v->int_val;
            else ok = false;
            if (ok) memcpy(b->data + row * 8, &x, 8);
            break;
        }
        case SHRED_BOOL: {
            bool x = false;
            if (v->type == RDB_TYPE_BOOL) x = v->bool_val;
            else if (v->type == RDB_TYPE_INT && (v->int_val == 0 || v->int_val == 1)) x = v->int_val;
            else ok = false;
            if (ok && x) b->data[row / 8] |= (uint8_t)(1u << (row % 8));
            break;
        }
        case SHRED_UTF8: {
            char buf[64];
            if (shred_is_text(v->type) && v->str_val) {
                shred_bytes(b, row, v->str_val, (size_t)v->str_len);
            } else if (v->type == RDB_TYPE_INT) {
                shred_bytes(b, row, buf, (size_t)snprintf(buf, sizeof(buf), "%" PRId64, v->int_val));
            } else if (v->type == RDB_TYPE_FLOAT) {
                // DBL_DIG digits round-trip decimal values stored as REAL
                shred_bytes(b, row, buf, (size_t)snprintf(buf, sizeof(buf), "%.15g", v->float_val));
            } else if (v->type == RDB_TYPE_BOOL) {
                shred_bytes(b, row, v->bool_val ? "true" : "false", v->bool_val ? 4 : 5);
            } else ok = false;
            break;
        }
        case SHRED_BINARY:
            if (shred_has_bytes(v->type) && v->str_val) shred_bytes(b, row, v->str_val, (size_t)v->str_len);
            else ok = false;
            break;
    }
    if (!ok) {
        log_error("rdb batch: column '%s' cannot hold a value of rdb type %d", col->name, (int)v->type);
        return RDB_ERROR;
    }
    b->validity[row / 8] |= (uint8_t)(1u << (row % 8));
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Arrow array export
 * ══════════════════════════════════════════════════════════════════════ */

static void shred_array_release(struct ArrowArray* array) {
    if (!array || !array->release) return;
    for (int64_t i = 0; i < array->n_children; i++) {
        struct ArrowArray* child = array->children[i];
        if (child->release) child->release(child);
        mem_free(child);
    }
    mem_free(array->children);
    for (int64_t i = 0; i < array->n_buffers; i++) mem_free((void*)array->buffers[i]);
    mem_free(array->buffers);
    array->release = NULL;
}

/** hand a builder's buffers to an Arrow child array; the builder is emptied */
static struct ArrowArray* shred_export_column(ShredBuilder* b, ShredKind kind, int64_t rows) {
    struct ArrowArray* child = (struct ArrowArray*)mem_calloc(1, sizeof(struct ArrowArray),
                                                              MEM_CAT_INPUT_OTHER);
    bool variable = kind == SHRED_UTF8 || kind == SHRED_BINARY;
    child->length = rows;
    child->null_count = b->null_count;
    child->n_buffers = variable ? 3 : 2;
    child->buffers = (const void**)mem_calloc(3, sizeof(void*), MEM_CAT_INPUT_OTHER);
    child->buffers[0] = b->validity;
    if (variable) {
        child->buffers[1] = b->offsets;
        child->buffers[2] = b->data;
    } else {
        child->buffers[1] = b->data;
    }
    child->release = shred_array_release;
    memset(b, 0, sizeof(*b));
    return child;
}

int rdb_batch_shred_fetch(RdbStmt* stmt, RdbBatchShred** state, int64_t max_rows,
                          struct ArrowArray* out) {
    int rc = shred_init(stmt, state);
    if (rc != RDB_OK) return rc;
    RdbBatchShred* st = *state;
    if (st->done && !st->lookahead) return RDB_DONE;

    int count = st->column_count;
    ShredBuilder* builders = (ShredBuilder*)mem_calloc((size_t)(count > 0 ? count : 1),
                                                       sizeof(ShredBuilder), MEM_CAT_INPUT_OTHER);
    for (int c = 0; c < count; c++) shred_builder_init(&builders[c], st->columns[c].kind, max_rows);

    int64_t rows = 0;
    rc = RDB_OK;
    if (st->lookahead) {
        for (int c = 0; c < count && rc == RDB_OK; c++) {
            rc = shred_append(&builders[c], &st->columns[c], rows, &st->lookahead[c]);
        }
        shred_free_row(st->lookahead, count);
        st->lookahead = NULL;
        rows++;
    }
    while (rc == RDB_OK && rows < max_rows && !st->done) {
        int step = rdb_step(stmt);
        if (step == RDB_DONE) {
            st->done = true;
            break;
        }
        if (step != RDB_ROW) {
            rc = step;
            break;
        }
        for (int c = 0; c < count && rc == RDB_OK; c++) {
            RdbValue v = rdb_column_value(stmt, c);
            rc = shred_append(&builders[c], &st->columns[c], rows, &v);
        }
        rows++;
    }
    if (rc != RDB_OK || rows == 0) {
        for (int c = 0; c < count; c++) shred_builder_free(&builders[c]);
        mem_free(builders);
        return rc != RDB_OK ? rc : RDB_DONE;
    }

    memset(out, 0, sizeof(*out));
    out->length = rows;
    out->n_buffers = 1;     // struct arrays carry only a (here absent) validity buffer
    out->buffers = (const void**)mem_calloc(1, sizeof(void*), MEM_CAT_INPUT_OTHER);
    out->n_children = count;
    out->children = (struct ArrowArray**)mem_calloc((size_t)(count > 0 ? count : 1),
                                                    sizeof(struct ArrowArray*), MEM_CAT_INPUT_OTHER);
    for (int c = 0; c < count; c++) {
        out->children[c] = shred_export_column(&builders[c], st->columns[c].kind, rows);
    }
    out->release = shred_array_release;
    mem_free(builders);
    return RDB_ROW;
}
