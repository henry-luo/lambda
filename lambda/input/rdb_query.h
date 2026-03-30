/**
 * @file rdb_query.h
 * @brief Database-agnostic SQL query builder for Lambda's for-clause → SQL compilation.
 *
 * Provides a structured API to build parameterized SELECT queries from
 * Lambda's for-clause components (where, order by, limit, offset).
 * All table/column names are validated against the RDB schema to prevent
 * SQL injection.  User-supplied values are always bound as parameters.
 *
 * See vibe/Lambda_IO_RDB.md §3 and §6.7 for design rationale.
 */

#ifndef LAMBDA_INPUT_RDB_QUERY_H
#define LAMBDA_INPUT_RDB_QUERY_H

#include "../../lib/rdb.h"
#include "../../lib/strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════
 * Query expression types — represents a WHERE condition tree
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_EXPR_LITERAL,       /* constant value (int, float, string, null, bool) */
    RDB_EXPR_COLUMN,        /* table column reference */
    RDB_EXPR_COMPARE,       /* binary comparison: ==, !=, <, <=, >, >= */
    RDB_EXPR_AND,           /* logical AND of two sub-expressions */
    RDB_EXPR_OR,            /* logical OR of two sub-expressions */
    RDB_EXPR_NOT,           /* logical NOT of a sub-expression */
    RDB_EXPR_IN,            /* column IN (value, value, ...) */
    RDB_EXPR_IS_NULL,       /* column IS NULL */
    RDB_EXPR_IS_NOT_NULL,   /* column IS NOT NULL */
    RDB_EXPR_LIKE,          /* column LIKE pattern */
} RdbExprKind;

typedef enum {
    RDB_CMP_EQ,             /* = */
    RDB_CMP_NE,             /* != */
    RDB_CMP_LT,             /* < */
    RDB_CMP_LE,             /* <= */
    RDB_CMP_GT,             /* > */
    RDB_CMP_GE,             /* >= */
} RdbCmpOp;

typedef enum {
    RDB_LIKE_STARTS_WITH,   /* 'val%'   — starts_with(col, val) */
    RDB_LIKE_ENDS_WITH,     /* '%val'   — ends_with(col, val) */
    RDB_LIKE_CONTAINS,      /* '%val%'  — contains(col, val) */
} RdbLikeKind;

typedef struct RdbExpr RdbExpr;

struct RdbExpr {
    RdbExprKind kind;
    union {
        /* RDB_EXPR_LITERAL */
        RdbParam literal;

        /* RDB_EXPR_COLUMN */
        const char* column_name;

        /* RDB_EXPR_COMPARE */
        struct {
            RdbCmpOp    op;
            RdbExpr*    left;   /* typically a column */
            RdbExpr*    right;  /* typically a literal */
        } compare;

        /* RDB_EXPR_AND, RDB_EXPR_OR */
        struct {
            RdbExpr* left;
            RdbExpr* right;
        } logic;

        /* RDB_EXPR_NOT */
        struct {
            RdbExpr* operand;
        } not_expr;

        /* RDB_EXPR_IN */
        struct {
            const char* column_name;
            int         value_count;
            RdbParam*   values;     /* array of literal values */
        } in_expr;

        /* RDB_EXPR_IS_NULL, RDB_EXPR_IS_NOT_NULL */
        struct {
            const char* column_name;
        } null_check;

        /* RDB_EXPR_LIKE */
        struct {
            const char*  column_name;
            const char*  pattern;   /* the search string (not the SQL pattern) */
            RdbLikeKind  like_kind;
        } like;
    };
};

/* ══════════════════════════════════════════════════════════════════════
 * ORDER BY descriptor
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char* column_name;
    bool        descending;     /* false = ASC (default), true = DESC */
} RdbOrderBy;

/* ══════════════════════════════════════════════════════════════════════
 * Query descriptor — fully describes a SELECT query
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char*     table_name;     /* FROM table */
    RdbExpr*        where_expr;     /* WHERE clause (NULL = no filter) */
    int             order_count;
    RdbOrderBy*     order_by;       /* ORDER BY clauses (NULL = no ordering) */
    int64_t         limit;          /* LIMIT value (-1 = no limit) */
    int64_t         offset;         /* OFFSET value (-1 = no offset) */
} RdbQueryDesc;

/* ══════════════════════════════════════════════════════════════════════
 * Built query — the result of rdb_query_build()
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    char*       sql;            /* parameterized SQL string (caller frees) */
    int         param_count;
    RdbParam*   params;         /* array of bound values (caller frees) */
} RdbBuiltQuery;

/* ══════════════════════════════════════════════════════════════════════
 * Expression builder helpers — allocate from pool
 * ══════════════════════════════════════════════════════════════════════ */

RdbExpr* rdb_expr_column(Pool* pool, const char* column_name);
RdbExpr* rdb_expr_int(Pool* pool, int64_t value);
RdbExpr* rdb_expr_float(Pool* pool, double value);
RdbExpr* rdb_expr_string(Pool* pool, const char* value);
RdbExpr* rdb_expr_bool(Pool* pool, bool value);
RdbExpr* rdb_expr_null(Pool* pool);

RdbExpr* rdb_expr_compare(Pool* pool, RdbCmpOp op, RdbExpr* left, RdbExpr* right);
RdbExpr* rdb_expr_and(Pool* pool, RdbExpr* left, RdbExpr* right);
RdbExpr* rdb_expr_or(Pool* pool, RdbExpr* left, RdbExpr* right);
RdbExpr* rdb_expr_not(Pool* pool, RdbExpr* operand);
RdbExpr* rdb_expr_in(Pool* pool, const char* column_name, int value_count, RdbParam* values);
RdbExpr* rdb_expr_is_null(Pool* pool, const char* column_name);
RdbExpr* rdb_expr_is_not_null(Pool* pool, const char* column_name);
RdbExpr* rdb_expr_like(Pool* pool, const char* column_name, const char* pattern, RdbLikeKind kind);

/* ══════════════════════════════════════════════════════════════════════
 * Query building
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Build a parameterized SQL SELECT from a query descriptor.
 *
 * Validates all table/column names against the provided schema.
 * Returns RDB_OK on success, RDB_ERROR on validation failure.
 *
 * @param pool       allocation pool for temporaries
 * @param schema     database schema for name validation
 * @param desc       query descriptor
 * @param out_query  output: built query with SQL string + params
 * @return RDB_OK or RDB_ERROR
 */
int rdb_query_build(Pool* pool, RdbSchema* schema, const RdbQueryDesc* desc,
                    RdbBuiltQuery* out_query);

/**
 * Execute a built query against a connection and return results via callback.
 * The statement is prepared, parameters bound, stepped, and finalized.
 *
 * @param conn       active database connection
 * @param query      built query from rdb_query_build()
 * @param table      table metadata (for column type mapping)
 * @param row_cb     callback invoked per row (stmt, col_count, user_data)
 * @param user_data  passed through to row_cb
 * @return number of rows processed, or -1 on error
 */
typedef void (*RdbRowCallback)(RdbStmt* stmt, int col_count, void* user_data);

int64_t rdb_query_exec(RdbConn* conn, const RdbBuiltQuery* query,
                       RdbRowCallback row_cb, void* user_data);

/**
 * Free a built query's dynamically allocated fields.
 */
void rdb_query_free(RdbBuiltQuery* query);

#ifdef __cplusplus
}
#endif

#endif /* LAMBDA_INPUT_RDB_QUERY_H */
