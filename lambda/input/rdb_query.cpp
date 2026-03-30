/**
 * @file rdb_query.cpp
 * @brief Database-agnostic SQL query builder for Lambda's for-clause → SQL compilation.
 *
 * Generates parameterized SQL SELECT statements from structured query
 * descriptors.  All table/column names are validated against the schema;
 * user-supplied values are always bound as SQL parameters (never interpolated).
 *
 * See vibe/Lambda_IO_RDB.md §3, §6.7, §6.8 for design details.
 */

#include "rdb_query.h"
#include "../../lib/log.h"
#include <string.h>
#include <stdlib.h>

extern "C" {

/* ══════════════════════════════════════════════════════════════════════
 * Expression builder helpers
 * ══════════════════════════════════════════════════════════════════════ */

RdbExpr* rdb_expr_column(Pool* pool, const char* column_name) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_COLUMN;
    e->column_name = column_name;
    return e;
}

RdbExpr* rdb_expr_int(Pool* pool, int64_t value) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LITERAL;
    e->literal.type = RDB_TYPE_INT;
    e->literal.int_val = value;
    return e;
}

RdbExpr* rdb_expr_float(Pool* pool, double value) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LITERAL;
    e->literal.type = RDB_TYPE_FLOAT;
    e->literal.float_val = value;
    return e;
}

RdbExpr* rdb_expr_string(Pool* pool, const char* value) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LITERAL;
    e->literal.type = RDB_TYPE_STRING;
    e->literal.str_val = value;
    return e;
}

RdbExpr* rdb_expr_bool(Pool* pool, bool value) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LITERAL;
    e->literal.type = RDB_TYPE_BOOL;
    e->literal.bool_val = value;
    return e;
}

RdbExpr* rdb_expr_null(Pool* pool) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LITERAL;
    e->literal.type = RDB_TYPE_NULL;
    return e;
}

RdbExpr* rdb_expr_compare(Pool* pool, RdbCmpOp op, RdbExpr* left, RdbExpr* right) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_COMPARE;
    e->compare.op = op;
    e->compare.left = left;
    e->compare.right = right;
    return e;
}

RdbExpr* rdb_expr_and(Pool* pool, RdbExpr* left, RdbExpr* right) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_AND;
    e->logic.left = left;
    e->logic.right = right;
    return e;
}

RdbExpr* rdb_expr_or(Pool* pool, RdbExpr* left, RdbExpr* right) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_OR;
    e->logic.left = left;
    e->logic.right = right;
    return e;
}

RdbExpr* rdb_expr_not(Pool* pool, RdbExpr* operand) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_NOT;
    e->not_expr.operand = operand;
    return e;
}

RdbExpr* rdb_expr_in(Pool* pool, const char* column_name, int value_count, RdbParam* values) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_IN;
    e->in_expr.column_name = column_name;
    e->in_expr.value_count = value_count;
    e->in_expr.values = values;
    return e;
}

RdbExpr* rdb_expr_is_null(Pool* pool, const char* column_name) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_IS_NULL;
    e->null_check.column_name = column_name;
    return e;
}

RdbExpr* rdb_expr_is_not_null(Pool* pool, const char* column_name) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_IS_NOT_NULL;
    e->null_check.column_name = column_name;
    return e;
}

RdbExpr* rdb_expr_like(Pool* pool, const char* column_name, const char* pattern, RdbLikeKind kind) {
    RdbExpr* e = (RdbExpr*)pool_calloc(pool, sizeof(RdbExpr));
    e->kind = RDB_EXPR_LIKE;
    e->like.column_name = column_name;
    e->like.pattern = pattern;
    e->like.like_kind = kind;
    return e;
}

/* ══════════════════════════════════════════════════════════════════════
 * Internal: parameter collector
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_MAX_PARAMS 128

typedef struct {
    StrBuf*   sql;
    int       param_count;
    RdbParam  params[RDB_MAX_PARAMS];
} QueryBuilder;

static int qb_add_param(QueryBuilder* qb, const RdbParam* param) {
    if (qb->param_count >= RDB_MAX_PARAMS) {
        log_error("rdb query: too many parameters (max %d)", RDB_MAX_PARAMS);
        return RDB_ERROR;
    }
    qb->params[qb->param_count] = *param;
    // own a copy of string values so they survive until rdb_query_free
    if (param->type == RDB_TYPE_STRING && param->str_val) {
        qb->params[qb->param_count].str_val = strdup(param->str_val);
    }
    qb->param_count++;
    // append placeholder: ?1, ?2, etc.
    char buf[16];
    snprintf(buf, sizeof(buf), "?%d", qb->param_count);
    strbuf_append_str(qb->sql, buf);
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Internal: schema validation helpers
 * ══════════════════════════════════════════════════════════════════════ */

static RdbTable* find_table(RdbSchema* schema, const char* name) {
    for (int i = 0; i < schema->table_count; i++) {
        if (strcmp(schema->tables[i].name, name) == 0)
            return &schema->tables[i];
    }
    return NULL;
}

static bool validate_column(RdbTable* table, const char* col_name) {
    for (int c = 0; c < table->column_count; c++) {
        if (strcmp(table->columns[c].name, col_name) == 0) return true;
    }
    return false;
}

/* ══════════════════════════════════════════════════════════════════════
 * Internal: emit WHERE expression as SQL into the QueryBuilder
 * ══════════════════════════════════════════════════════════════════════ */

static int emit_expr(QueryBuilder* qb, RdbTable* table, const RdbExpr* expr) {
    if (!expr) return RDB_ERROR;

    switch (expr->kind) {
        case RDB_EXPR_LITERAL:
            return qb_add_param(qb, &expr->literal);

        case RDB_EXPR_COLUMN:
            if (!validate_column(table, expr->column_name)) {
                log_error("rdb query: unknown column '%s' in table '%s'",
                          expr->column_name, table->name);
                return RDB_ERROR;
            }
            strbuf_append_str(qb->sql, "\"");
            strbuf_append_str(qb->sql, expr->column_name);
            strbuf_append_str(qb->sql, "\"");
            return RDB_OK;

        case RDB_EXPR_COMPARE: {
            strbuf_append_str(qb->sql, "(");
            if (emit_expr(qb, table, expr->compare.left) != RDB_OK) return RDB_ERROR;
            switch (expr->compare.op) {
                case RDB_CMP_EQ: strbuf_append_str(qb->sql, " = ");  break;
                case RDB_CMP_NE: strbuf_append_str(qb->sql, " != "); break;
                case RDB_CMP_LT: strbuf_append_str(qb->sql, " < ");  break;
                case RDB_CMP_LE: strbuf_append_str(qb->sql, " <= "); break;
                case RDB_CMP_GT: strbuf_append_str(qb->sql, " > ");  break;
                case RDB_CMP_GE: strbuf_append_str(qb->sql, " >= "); break;
            }
            if (emit_expr(qb, table, expr->compare.right) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, ")");
            return RDB_OK;
        }

        case RDB_EXPR_AND:
            strbuf_append_str(qb->sql, "(");
            if (emit_expr(qb, table, expr->logic.left) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, " AND ");
            if (emit_expr(qb, table, expr->logic.right) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, ")");
            return RDB_OK;

        case RDB_EXPR_OR:
            strbuf_append_str(qb->sql, "(");
            if (emit_expr(qb, table, expr->logic.left) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, " OR ");
            if (emit_expr(qb, table, expr->logic.right) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, ")");
            return RDB_OK;

        case RDB_EXPR_NOT:
            strbuf_append_str(qb->sql, "(NOT ");
            if (emit_expr(qb, table, expr->not_expr.operand) != RDB_OK) return RDB_ERROR;
            strbuf_append_str(qb->sql, ")");
            return RDB_OK;

        case RDB_EXPR_IN: {
            if (!validate_column(table, expr->in_expr.column_name)) {
                log_error("rdb query: unknown column '%s' in IN clause", expr->in_expr.column_name);
                return RDB_ERROR;
            }
            if (expr->in_expr.value_count <= 0) {
                // empty IN list → always false
                strbuf_append_str(qb->sql, "(0)");
                return RDB_OK;
            }
            strbuf_append_str(qb->sql, "(\"");
            strbuf_append_str(qb->sql, expr->in_expr.column_name);
            strbuf_append_str(qb->sql, "\" IN (");
            for (int i = 0; i < expr->in_expr.value_count; i++) {
                if (i > 0) strbuf_append_str(qb->sql, ", ");
                if (qb_add_param(qb, &expr->in_expr.values[i]) != RDB_OK) return RDB_ERROR;
            }
            strbuf_append_str(qb->sql, "))");
            return RDB_OK;
        }

        case RDB_EXPR_IS_NULL:
            if (!validate_column(table, expr->null_check.column_name)) {
                log_error("rdb query: unknown column '%s' in IS NULL", expr->null_check.column_name);
                return RDB_ERROR;
            }
            strbuf_append_str(qb->sql, "(\"");
            strbuf_append_str(qb->sql, expr->null_check.column_name);
            strbuf_append_str(qb->sql, "\" IS NULL)");
            return RDB_OK;

        case RDB_EXPR_IS_NOT_NULL:
            if (!validate_column(table, expr->null_check.column_name)) {
                log_error("rdb query: unknown column '%s' in IS NOT NULL", expr->null_check.column_name);
                return RDB_ERROR;
            }
            strbuf_append_str(qb->sql, "(\"");
            strbuf_append_str(qb->sql, expr->null_check.column_name);
            strbuf_append_str(qb->sql, "\" IS NOT NULL)");
            return RDB_OK;

        case RDB_EXPR_LIKE: {
            if (!validate_column(table, expr->like.column_name)) {
                log_error("rdb query: unknown column '%s' in LIKE", expr->like.column_name);
                return RDB_ERROR;
            }
            strbuf_append_str(qb->sql, "(\"");
            strbuf_append_str(qb->sql, expr->like.column_name);
            strbuf_append_str(qb->sql, "\" LIKE ");

            // build LIKE pattern with appropriate wildcards
            StrBuf* pat = strbuf_new();
            if (expr->like.like_kind == RDB_LIKE_ENDS_WITH ||
                expr->like.like_kind == RDB_LIKE_CONTAINS) {
                strbuf_append_str(pat, "%");
            }
            // escape any SQL LIKE special chars in the user pattern
            const char* p = expr->like.pattern;
            while (p && *p) {
                if (*p == '%' || *p == '_' || *p == '\\') {
                    strbuf_append_char(pat, '\\');
                }
                strbuf_append_char(pat, *p);
                p++;
            }
            if (expr->like.like_kind == RDB_LIKE_STARTS_WITH ||
                expr->like.like_kind == RDB_LIKE_CONTAINS) {
                strbuf_append_str(pat, "%");
            }

            RdbParam like_param;
            memset(&like_param, 0, sizeof(like_param));
            like_param.type = RDB_TYPE_STRING;
            like_param.str_val = pat->str;
            if (qb_add_param(qb, &like_param) != RDB_OK) {
                strbuf_free(pat);
                return RDB_ERROR;
            }
            strbuf_free(pat);

            // add ESCAPE clause so our escaping of % and _ works
            strbuf_append_str(qb->sql, " ESCAPE '\\'");
            strbuf_append_str(qb->sql, ")");
            return RDB_OK;
        }
    }
    return RDB_ERROR;
}

/* ══════════════════════════════════════════════════════════════════════
 * Public: build parameterized SQL from query descriptor
 * ══════════════════════════════════════════════════════════════════════ */

int rdb_query_build(Pool* pool, RdbSchema* schema, const RdbQueryDesc* desc,
                    RdbBuiltQuery* out_query) {
    if (!schema || !desc || !out_query || !desc->table_name) {
        log_error("rdb query: NULL argument to rdb_query_build");
        return RDB_ERROR;
    }

    // validate table name against schema
    RdbTable* table = find_table(schema, desc->table_name);
    if (!table) {
        log_error("rdb query: unknown table '%s'", desc->table_name);
        return RDB_ERROR;
    }

    QueryBuilder qb;
    memset(&qb, 0, sizeof(qb));
    qb.sql = strbuf_new();

    // SELECT * FROM "table"
    strbuf_append_str(qb.sql, "SELECT * FROM \"");
    strbuf_append_str(qb.sql, desc->table_name);
    strbuf_append_str(qb.sql, "\"");

    // WHERE clause
    if (desc->where_expr) {
        strbuf_append_str(qb.sql, " WHERE ");
        if (emit_expr(&qb, table, desc->where_expr) != RDB_OK) {
            strbuf_free(qb.sql);
            return RDB_ERROR;
        }
    }

    // ORDER BY clause
    if (desc->order_count > 0 && desc->order_by) {
        strbuf_append_str(qb.sql, " ORDER BY ");
        for (int i = 0; i < desc->order_count; i++) {
            if (i > 0) strbuf_append_str(qb.sql, ", ");
            if (!validate_column(table, desc->order_by[i].column_name)) {
                log_error("rdb query: unknown column '%s' in ORDER BY", desc->order_by[i].column_name);
                strbuf_free(qb.sql);
                return RDB_ERROR;
            }
            strbuf_append_str(qb.sql, "\"");
            strbuf_append_str(qb.sql, desc->order_by[i].column_name);
            strbuf_append_str(qb.sql, "\"");
            strbuf_append_str(qb.sql, desc->order_by[i].descending ? " DESC" : " ASC");
        }
    }

    // LIMIT / OFFSET
    if (desc->limit >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), " LIMIT %lld", (long long)desc->limit);
        strbuf_append_str(qb.sql, buf);
    }
    if (desc->offset > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), " OFFSET %lld", (long long)desc->offset);
        strbuf_append_str(qb.sql, buf);
    }

    // copy SQL string
    size_t sql_len = strlen(qb.sql->str);
    out_query->sql = (char*)malloc(sql_len + 1);
    memcpy(out_query->sql, qb.sql->str, sql_len + 1);
    strbuf_free(qb.sql);

    // copy params
    out_query->param_count = qb.param_count;
    if (qb.param_count > 0) {
        out_query->params = (RdbParam*)malloc((size_t)qb.param_count * sizeof(RdbParam));
        memcpy(out_query->params, qb.params, (size_t)qb.param_count * sizeof(RdbParam));
    } else {
        out_query->params = NULL;
    }

    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Public: execute a built query
 * ══════════════════════════════════════════════════════════════════════ */

int64_t rdb_query_exec(RdbConn* conn, const RdbBuiltQuery* query,
                       RdbRowCallback row_cb, void* user_data) {
    if (!conn || !query || !query->sql) return -1;

    RdbStmt* stmt = rdb_prepare(conn, query->sql);
    if (!stmt) {
        log_error("rdb query exec: prepare failed for '%s'", query->sql);
        return -1;
    }

    // bind parameters
    for (int i = 0; i < query->param_count; i++) {
        if (rdb_bind_param(stmt, i + 1, &query->params[i]) != RDB_OK) {
            log_error("rdb query exec: bind failed for param %d", i + 1);
            rdb_finalize(stmt);
            return -1;
        }
    }

    // iterate rows
    int64_t row_count = 0;
    int rc;
    while ((rc = rdb_step(stmt)) == RDB_ROW) {
        int col_count = rdb_column_count(stmt);
        if (row_cb) row_cb(stmt, col_count, user_data);
        row_count++;
    }

    rdb_finalize(stmt);

    if (rc == RDB_ERROR) {
        log_error("rdb query exec: step error for '%s'", query->sql);
        return -1;
    }

    return row_count;
}

/* ══════════════════════════════════════════════════════════════════════
 * Public: free built query resources
 * ══════════════════════════════════════════════════════════════════════ */

void rdb_query_free(RdbBuiltQuery* query) {
    if (!query) return;
    if (query->sql) {
        free(query->sql);
        query->sql = NULL;
    }
    if (query->params) {
        for (int i = 0; i < query->param_count; i++) {
            if (query->params[i].type == RDB_TYPE_STRING && query->params[i].str_val) {
                free((void*)query->params[i].str_val);
            }
        }
        free(query->params);
        query->params = NULL;
    }
    query->param_count = 0;
}

} // extern "C"
