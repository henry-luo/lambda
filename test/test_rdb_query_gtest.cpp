/**
 * @file test_rdb_query_gtest.cpp
 * @brief GTest tests for the RDB SQL query builder.
 *
 * Tests cover:
 *   - SELECT * (no clauses)
 *   - WHERE with comparisons (==, !=, <, <=, >, >=)
 *   - WHERE with AND, OR, NOT
 *   - WHERE with IN list
 *   - WHERE with IS NULL / IS NOT NULL
 *   - WHERE with LIKE (starts_with, ends_with, contains)
 *   - ORDER BY (ASC, DESC, multiple columns)
 *   - LIMIT / OFFSET
 *   - Parameter binding validation
 *   - Schema validation (unknown table/column → error)
 *   - Query execution through rdb_query_exec()
 */

#include <gtest/gtest.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

extern "C" {
#include "../lib/rdb.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/sqlite/sqlite3.h"
}

#include "../lambda/input/rdb_query.h"

/* ══════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB = "temp/test_rdb_query.db";

static void ensure_temp_dir(void) {
    mkdir("temp", 0755);
}

static void create_query_test_db(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE books ("
        "  id INTEGER PRIMARY KEY,"
        "  title TEXT NOT NULL,"
        "  author TEXT,"
        "  year INTEGER,"
        "  price REAL,"
        "  genre TEXT,"
        "  in_stock BOOLEAN DEFAULT 1"
        ");"
        "INSERT INTO books VALUES (1, 'Dune', 'Frank Herbert', 1965, 12.99, 'sci-fi', 1);"
        "INSERT INTO books VALUES (2, 'Neuromancer', 'William Gibson', 1984, 9.99, 'sci-fi', 1);"
        "INSERT INTO books VALUES (3, '1984', 'George Orwell', 1949, 8.99, 'dystopia', 1);"
        "INSERT INTO books VALUES (4, 'Snow Crash', 'Neal Stephenson', 1992, 14.99, 'sci-fi', 0);"
        "INSERT INTO books VALUES (5, 'Foundation', 'Isaac Asimov', 1951, 11.99, 'sci-fi', 1);"
        "INSERT INTO books VALUES (6, 'Brave New World', 'Aldous Huxley', 1932, 7.99, 'dystopia', 1);"
        "INSERT INTO books VALUES (7, 'The Road', 'Cormac McCarthy', 2006, 13.99, NULL, 1);";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

/* ══════════════════════════════════════════════════════════════════════
 * Fixture
 * ══════════════════════════════════════════════════════════════════════ */

class RdbQueryTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        create_query_test_db(TEST_DB);

        rdb_sqlite_register();
        pool = pool_create();
        conn = rdb_open(pool, TEST_DB, "sqlite", true);
        ASSERT_NE(conn, nullptr);
        ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    }

    void TearDown() override {
        if (conn) rdb_close(conn);
        if (pool) pool_destroy(pool);
        unlink(TEST_DB);
    }

    RdbBuiltQuery build(const RdbQueryDesc& desc) {
        RdbBuiltQuery q;
        memset(&q, 0, sizeof(q));
        int rc = rdb_query_build(pool, &conn->schema, &desc, &q);
        EXPECT_EQ(rc, RDB_OK);
        return q;
    }
};

/* ══════════════════════════════════════════════════════════════════════
 * §1 SELECT * (no clauses)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, SelectAll) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\"");
    EXPECT_EQ(q.param_count, 0);
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §2 WHERE — comparisons
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereEqual) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"),
        rdb_expr_string(pool, "sci-fi"));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"genre\" = ?1)");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_EQ(q.params[0].type, RDB_TYPE_STRING);
    EXPECT_STREQ(q.params[0].str_val, "sci-fi");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereComparison) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_GE,
        rdb_expr_column(pool, "year"),
        rdb_expr_int(pool, 2000));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"year\" >= ?1)");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_EQ(q.params[0].type, RDB_TYPE_INT);
    EXPECT_EQ(q.params[0].int_val, 2000);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereLessThan) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_LT,
        rdb_expr_column(pool, "price"),
        rdb_expr_float(pool, 10.0));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"price\" < ?1)");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_EQ(q.params[0].type, RDB_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(q.params[0].float_val, 10.0);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereNotEqual) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_NE,
        rdb_expr_column(pool, "genre"),
        rdb_expr_string(pool, "dystopia"));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"genre\" != ?1)");
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §3 WHERE — logical operators
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereAnd) {
    RdbExpr* left = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "sci-fi"));
    RdbExpr* right = rdb_expr_compare(pool, RDB_CMP_GE,
        rdb_expr_column(pool, "year"), rdb_expr_int(pool, 1980));
    RdbExpr* expr = rdb_expr_and(pool, left, right);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE ((\"genre\" = ?1) AND (\"year\" >= ?2))");
    EXPECT_EQ(q.param_count, 2);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereOr) {
    RdbExpr* left = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "sci-fi"));
    RdbExpr* right = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "dystopia"));
    RdbExpr* expr = rdb_expr_or(pool, left, right);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE ((\"genre\" = ?1) OR (\"genre\" = ?2))");
    EXPECT_EQ(q.param_count, 2);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereNot) {
    RdbExpr* inner = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "dystopia"));
    RdbExpr* expr = rdb_expr_not(pool, inner);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (NOT (\"genre\" = ?1))");
    EXPECT_EQ(q.param_count, 1);
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §4 WHERE — IN list
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereIn) {
    RdbParam values[3];
    memset(values, 0, sizeof(values));
    values[0].type = RDB_TYPE_STRING; values[0].str_val = "sci-fi";
    values[1].type = RDB_TYPE_STRING; values[1].str_val = "dystopia";
    values[2].type = RDB_TYPE_STRING; values[2].str_val = "fantasy";

    RdbExpr* expr = rdb_expr_in(pool, "genre", 3, values);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (\"genre\" IN (?1, ?2, ?3))");
    EXPECT_EQ(q.param_count, 3);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereInEmpty) {
    RdbExpr* expr = rdb_expr_in(pool, "genre", 0, NULL);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (0)");
    EXPECT_EQ(q.param_count, 0);
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §5 WHERE — NULL checks
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereIsNull) {
    RdbExpr* expr = rdb_expr_is_null(pool, "genre");

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"genre\" IS NULL)");
    EXPECT_EQ(q.param_count, 0);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereIsNotNull) {
    RdbExpr* expr = rdb_expr_is_not_null(pool, "genre");

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" WHERE (\"genre\" IS NOT NULL)");
    EXPECT_EQ(q.param_count, 0);
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §6 WHERE — LIKE (starts_with, ends_with, contains)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereLikeStartsWith) {
    RdbExpr* expr = rdb_expr_like(pool, "title", "The", RDB_LIKE_STARTS_WITH);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (\"title\" LIKE ?1 ESCAPE '\\')");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_STREQ(q.params[0].str_val, "The%");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereLikeEndsWith) {
    RdbExpr* expr = rdb_expr_like(pool, "title", "World", RDB_LIKE_ENDS_WITH);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (\"title\" LIKE ?1 ESCAPE '\\')");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_STREQ(q.params[0].str_val, "%World");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, WhereLikeContains) {
    RdbExpr* expr = rdb_expr_like(pool, "title", "Crash", RDB_LIKE_CONTAINS);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (\"title\" LIKE ?1 ESCAPE '\\')");
    EXPECT_EQ(q.param_count, 1);
    EXPECT_STREQ(q.params[0].str_val, "%Crash%");
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §7 ORDER BY
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, OrderByAsc) {
    RdbOrderBy order;
    order.column_name = "title";
    order.descending = false;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.order_count = 1;
    desc.order_by = &order;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" ORDER BY \"title\" ASC");
    EXPECT_EQ(q.param_count, 0);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, OrderByDesc) {
    RdbOrderBy order;
    order.column_name = "year";
    order.descending = true;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.order_count = 1;
    desc.order_by = &order;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" ORDER BY \"year\" DESC");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, OrderByMultiple) {
    RdbOrderBy orders[2];
    orders[0].column_name = "genre";
    orders[0].descending = false;
    orders[1].column_name = "year";
    orders[1].descending = true;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.order_count = 2;
    desc.order_by = orders;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" ORDER BY \"genre\" ASC, \"year\" DESC");
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §8 LIMIT / OFFSET
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, LimitOnly) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = 5;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" LIMIT 5");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, LimitOffset) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = 10;
    desc.offset = 20;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" LIMIT 10 OFFSET 20");
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, OffsetWithoutLimit) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = -1;
    desc.offset = 5;

    RdbBuiltQuery q = build(desc);
    // offset without limit: just SELECT * FROM table OFFSET 5
    EXPECT_STREQ(q.sql, "SELECT * FROM \"books\" OFFSET 5");
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §9 Combined clauses
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, WhereOrderByLimitOffset) {
    RdbExpr* where = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "sci-fi"));

    RdbOrderBy order;
    order.column_name = "title";
    order.descending = false;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = where;
    desc.order_count = 1;
    desc.order_by = &order;
    desc.limit = 10;
    desc.offset = 0;

    RdbBuiltQuery q = build(desc);
    EXPECT_STREQ(q.sql,
        "SELECT * FROM \"books\" WHERE (\"genre\" = ?1) ORDER BY \"title\" ASC LIMIT 10");
    EXPECT_EQ(q.param_count, 1);
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §10 Schema validation errors
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, UnknownTable) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "nonexistent";
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q;
    memset(&q, 0, sizeof(q));
    int rc = rdb_query_build(pool, &conn->schema, &desc, &q);
    EXPECT_EQ(rc, RDB_ERROR);
}

TEST_F(RdbQueryTest, UnknownColumnInWhere) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "nonexistent_col"),
        rdb_expr_string(pool, "value"));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q;
    memset(&q, 0, sizeof(q));
    int rc = rdb_query_build(pool, &conn->schema, &desc, &q);
    EXPECT_EQ(rc, RDB_ERROR);
}

TEST_F(RdbQueryTest, UnknownColumnInOrderBy) {
    RdbOrderBy order;
    order.column_name = "nonexistent_col";
    order.descending = false;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.order_count = 1;
    desc.order_by = &order;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q;
    memset(&q, 0, sizeof(q));
    int rc = rdb_query_build(pool, &conn->schema, &desc, &q);
    EXPECT_EQ(rc, RDB_ERROR);
}

/* ══════════════════════════════════════════════════════════════════════
 * §11 Query execution against real SQLite
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbQueryTest, ExecSelectAll) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 7);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecWhereGenre) {
    RdbExpr* expr = rdb_expr_compare(pool, RDB_CMP_EQ,
        rdb_expr_column(pool, "genre"), rdb_expr_string(pool, "sci-fi"));

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 4); // Dune, Neuromancer, Snow Crash, Foundation
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecWhereYearRange) {
    RdbExpr* left = rdb_expr_compare(pool, RDB_CMP_GE,
        rdb_expr_column(pool, "year"), rdb_expr_int(pool, 1980));
    RdbExpr* right = rdb_expr_compare(pool, RDB_CMP_LE,
        rdb_expr_column(pool, "year"), rdb_expr_int(pool, 2000));
    RdbExpr* expr = rdb_expr_and(pool, left, right);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 2); // Neuromancer (1984), Snow Crash (1992)
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecWhereIsNull) {
    RdbExpr* expr = rdb_expr_is_null(pool, "genre");

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 1); // The Road (genre NULL)
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecWhereIn) {
    RdbParam values[2];
    memset(values, 0, sizeof(values));
    values[0].type = RDB_TYPE_INT; values[0].int_val = 1965;
    values[1].type = RDB_TYPE_INT; values[1].int_val = 1984;

    RdbExpr* expr = rdb_expr_in(pool, "year", 2, values);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 2); // Dune (1965), Neuromancer (1984)
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecLikeContains) {
    RdbExpr* expr = rdb_expr_like(pool, "title", "Crash", RDB_LIKE_CONTAINS);

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.where_expr = expr;
    desc.limit = -1;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 1); // Snow Crash
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecLimit) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = 3;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 3);
    rdb_query_free(&q);
}

TEST_F(RdbQueryTest, ExecLimitOffset) {
    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.limit = 2;
    desc.offset = 5;

    RdbBuiltQuery q = build(desc);
    int64_t rows = rdb_query_exec(conn, &q, NULL, NULL);
    EXPECT_EQ(rows, 2); // 7 total, offset 5, limit 2 → rows 6 and 7
    rdb_query_free(&q);
}

/* ══════════════════════════════════════════════════════════════════════
 * §12 Callback-based row reading
 * ══════════════════════════════════════════════════════════════════════ */

struct CollectedRow {
    int64_t id;
    char title[256];
};

static void collect_row(RdbStmt* stmt, int col_count, void* user_data) {
    auto* rows = (CollectedRow*)user_data;
    // find the next free slot (id == 0)
    int idx = 0;
    while (rows[idx].id != 0) idx++;

    RdbValue id_val = rdb_column_value(stmt, 0);
    rows[idx].id = id_val.int_val;

    RdbValue title_val = rdb_column_value(stmt, 1);
    if (title_val.str_val) {
        strncpy(rows[idx].title, title_val.str_val, sizeof(rows[idx].title) - 1);
    }
}

TEST_F(RdbQueryTest, ExecWithCallback) {
    RdbOrderBy order;
    order.column_name = "title";
    order.descending = false;

    RdbQueryDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.table_name = "books";
    desc.order_count = 1;
    desc.order_by = &order;
    desc.limit = 3;
    desc.offset = -1;

    RdbBuiltQuery q = build(desc);

    CollectedRow rows[3];
    memset(rows, 0, sizeof(rows));

    int64_t count = rdb_query_exec(conn, &q, collect_row, rows);
    EXPECT_EQ(count, 3);

    // first 3 books alphabetically: 1984, Brave New World, Dune
    EXPECT_STREQ(rows[0].title, "1984");
    EXPECT_STREQ(rows[1].title, "Brave New World");
    EXPECT_STREQ(rows[2].title, "Dune");

    rdb_query_free(&q);
}
