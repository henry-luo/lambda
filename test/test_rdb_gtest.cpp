/**
 * @file test_rdb_gtest.cpp
 * @brief GTest unit tests for the generic RDB API and SQLite driver.
 *
 * Tests cover:
 *   - Driver registration and auto-detection
 *   - SQLite connection open/close
 *   - Schema introspection (columns, indexes, foreign keys, reverse FKs)
 *   - Type mapping (INTEGER, REAL, TEXT, DATETIME, JSON, BOOLEAN, BLOB)
 *   - Query execution (prepare, step, column_value, finalize)
 *   - Parameter binding
 *   - Row counting
 *   - Error handling (bad paths, unknown tables)
 *
 * All test databases are created programmatically in ./temp/ and cleaned
 * up in fixture TearDown.
 */

#include <gtest/gtest.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
#include "../lib/rdb.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/sqlite3.h"
}

/* ══════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_PATH = "temp/test_rdb.db";
static const char* TEST_DB_EMPTY = "temp/test_rdb_empty.db";

/** ensure ./temp/ directory exists */
static void ensure_temp_dir(void) {
    mkdir("temp", 0755);
}

/** create a sample database with two related tables and a view */
static void create_test_database(const char* path) {
    sqlite3* db = NULL;
    int rc = sqlite3_open(path, &db);
    ASSERT_EQ(rc, SQLITE_OK) << "Failed to create test database";

    const char* ddl =
        "CREATE TABLE authors ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  born DATE,"
        "  rating REAL DEFAULT 0.0,"
        "  active BOOLEAN DEFAULT 1"
        ");"
        "CREATE TABLE books ("
        "  id INTEGER PRIMARY KEY,"
        "  title TEXT NOT NULL,"
        "  author_id INTEGER NOT NULL,"
        "  price DECIMAL(8,2),"
        "  published DATETIME,"
        "  metadata JSON,"
        "  cover BLOB,"
        "  FOREIGN KEY (author_id) REFERENCES authors(id)"
        ");"
        "CREATE INDEX idx_books_author ON books(author_id);"
        "CREATE UNIQUE INDEX idx_books_title ON books(title);"
        "CREATE VIEW active_authors AS SELECT * FROM authors WHERE active = 1;"
        "INSERT INTO authors (id, name, born, rating, active) VALUES "
        "  (1, 'Alice', '1985-03-15', 4.8, 1),"
        "  (2, 'Bob', '1990-07-22', 3.5, 1),"
        "  (3, 'Charlie', '1978-11-01', 4.2, 0);"
        "INSERT INTO books (id, title, author_id, price, published, metadata) VALUES "
        "  (1, 'Lambda Calculus', 1, 29.99, '2023-01-15 10:30:00', "
        "   '{\"tags\":[\"math\",\"cs\"],\"pages\":320}'),"
        "  (2, 'Type Theory', 1, 39.99, '2023-06-20 14:00:00', "
        "   '{\"tags\":[\"logic\"],\"pages\":450}'),"
        "  (3, 'Functional Programming', 2, 24.50, '2024-02-10 09:00:00', NULL);";

    char* errmsg = NULL;
    rc = sqlite3_exec(db, ddl, NULL, NULL, &errmsg);
    ASSERT_EQ(rc, SQLITE_OK) << "DDL failed: " << (errmsg ? errmsg : "unknown");
    if (errmsg) sqlite3_free(errmsg);

    sqlite3_close(db);
}

/** create an empty database (no tables) */
static void create_empty_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);
    sqlite3_close(db);
}

/* ══════════════════════════════════════════════════════════════════════
 * Test fixture
 * ══════════════════════════════════════════════════════════════════════ */

class RdbTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_test_database(TEST_DB_PATH);
        create_empty_database(TEST_DB_EMPTY);
    }

    void TearDown() override {
        pool_destroy(pool);
        unlink(TEST_DB_PATH);
        unlink(TEST_DB_EMPTY);
    }
};

/* ══════════════════════════════════════════════════════════════════════
 * §1 Driver Registration & Detection
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, DriverRegistration) {
    const RdbDriver* drv = rdb_get_driver("sqlite");
    ASSERT_NE(drv, nullptr);
    EXPECT_STREQ(drv->name, "sqlite");
}

TEST_F(RdbTest, DriverLookupUnknown) {
    const RdbDriver* drv = rdb_get_driver("oracle");
    EXPECT_EQ(drv, nullptr);
}

TEST_F(RdbTest, DriverLookupNull) {
    const RdbDriver* drv = rdb_get_driver(NULL);
    EXPECT_EQ(drv, nullptr);
}

TEST_F(RdbTest, DetectDriver_SqliteExtDb) {
    EXPECT_STREQ(rdb_detect_driver("data.db"), "sqlite");
}

TEST_F(RdbTest, DetectDriver_SqliteExtSqlite) {
    EXPECT_STREQ(rdb_detect_driver("data.sqlite"), "sqlite");
}

TEST_F(RdbTest, DetectDriver_SqliteExtSqlite3) {
    EXPECT_STREQ(rdb_detect_driver("data.sqlite3"), "sqlite");
}

TEST_F(RdbTest, DetectDriver_PostgresScheme) {
    EXPECT_STREQ(rdb_detect_driver("postgresql://localhost/mydb"), "postgresql");
}

TEST_F(RdbTest, DetectDriver_PostgresShortScheme) {
    EXPECT_STREQ(rdb_detect_driver("postgres://user:pw@host/db"), "postgresql");
}

TEST_F(RdbTest, DetectDriver_MysqlScheme) {
    EXPECT_STREQ(rdb_detect_driver("mysql://localhost/mydb"), "mysql");
}

TEST_F(RdbTest, DetectDriver_DuckdbScheme) {
    EXPECT_STREQ(rdb_detect_driver("duckdb://data.ddb"), "duckdb");
}

TEST_F(RdbTest, DetectDriver_DuckdbExtDdb) {
    EXPECT_STREQ(rdb_detect_driver("warehouse.ddb"), "duckdb");
}

TEST_F(RdbTest, DetectDriver_DuckdbExtDuckdb) {
    EXPECT_STREQ(rdb_detect_driver("warehouse.duckdb"), "duckdb");
}

TEST_F(RdbTest, DetectDriver_Unknown) {
    EXPECT_EQ(rdb_detect_driver("data.csv"), nullptr);
}

TEST_F(RdbTest, DetectDriver_Null) {
    EXPECT_EQ(rdb_detect_driver(NULL), nullptr);
}

/* ══════════════════════════════════════════════════════════════════════
 * §2 Connection Lifecycle
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, OpenClose) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_NE(conn->handle, nullptr);
    EXPECT_NE(conn->driver, nullptr);
    EXPECT_TRUE(conn->readonly);
    EXPECT_STREQ(conn->uri, TEST_DB_PATH);
    rdb_close(conn);
}

TEST_F(RdbTest, OpenAutoDetect) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, NULL, true);
    ASSERT_NE(conn, nullptr);
    rdb_close(conn);
}

TEST_F(RdbTest, OpenNonexistentFile) {
    RdbConn* conn = rdb_open(pool, "temp/nonexistent.db", "sqlite", true);
    // SQLite in READONLY mode will fail for nonexistent files
    EXPECT_EQ(conn, nullptr);
}

TEST_F(RdbTest, OpenUnknownDriver) {
    RdbConn* conn = rdb_open(pool, "data.mdb", "mssql", true);
    EXPECT_EQ(conn, nullptr);
}

TEST_F(RdbTest, OpenNullDetect) {
    // file with no recognizable extension
    RdbConn* conn = rdb_open(pool, "temp/mystery", NULL, true);
    EXPECT_EQ(conn, nullptr);
}

TEST_F(RdbTest, CloseNull) {
    // should not crash
    rdb_close(NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 * §3 Schema Introspection
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, SchemaLoadSuccess) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(rdb_load_schema(conn), RDB_OK);
    // 2 tables + 1 view = 3
    EXPECT_EQ(conn->schema.table_count, 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SchemaEmptyDb) {
    RdbConn* conn = rdb_open(pool, TEST_DB_EMPTY, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(conn->schema.table_count, 0);
    rdb_close(conn);
}

TEST_F(RdbTest, GetTable_Authors) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);

    RdbTable* tbl = rdb_get_table(conn, "authors");
    ASSERT_NE(tbl, nullptr);
    EXPECT_STREQ(tbl->name, "authors");
    EXPECT_FALSE(tbl->is_view);
    EXPECT_EQ(tbl->column_count, 5); // id, name, born, rating, active
    rdb_close(conn);
}

TEST_F(RdbTest, GetTable_Books) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);

    RdbTable* tbl = rdb_get_table(conn, "books");
    ASSERT_NE(tbl, nullptr);
    EXPECT_EQ(tbl->column_count, 7); // id, title, author_id, price, published, metadata, cover
    rdb_close(conn);
}

TEST_F(RdbTest, GetTable_View) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);

    RdbTable* view = rdb_get_table(conn, "active_authors");
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->is_view);
    rdb_close(conn);
}

TEST_F(RdbTest, GetTable_NotFound) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);
    EXPECT_EQ(rdb_get_table(conn, "nonexistent"), nullptr);
    rdb_close(conn);
}

TEST_F(RdbTest, GetTable_Null) {
    EXPECT_EQ(rdb_get_table(NULL, "books"), nullptr);
}

/* ══════════════════════════════════════════════════════════════════════
 * §4 Column Metadata
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, ColumnMetadata_Id) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");

    RdbColumn* col = rdb_get_column(tbl, "id");
    ASSERT_NE(col, nullptr);
    EXPECT_STREQ(col->name, "id");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
    EXPECT_TRUE(col->primary_key);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Name) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");

    RdbColumn* col = rdb_get_column(tbl, "name");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
    EXPECT_FALSE(col->nullable);   // NOT NULL
    EXPECT_FALSE(col->primary_key);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Rating) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");

    RdbColumn* col = rdb_get_column(tbl, "rating");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_FLOAT);
    EXPECT_TRUE(col->nullable);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Born) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");

    RdbColumn* col = rdb_get_column(tbl, "born");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_DATETIME);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Active) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");

    RdbColumn* col = rdb_get_column(tbl, "active");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_BOOL);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Metadata) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "books");

    RdbColumn* col = rdb_get_column(tbl, "metadata");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_JSON);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Cover) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "books");

    RdbColumn* col = rdb_get_column(tbl, "cover");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_BLOB);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnMetadata_Price) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "books");

    RdbColumn* col = rdb_get_column(tbl, "price");
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->type, RDB_TYPE_DECIMAL);
    rdb_close(conn);
}

TEST_F(RdbTest, GetColumn_NotFound) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "authors");
    EXPECT_EQ(rdb_get_column(tbl, "nonexistent"), nullptr);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §5 Index Metadata
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, IndexCount) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "books");
    ASSERT_NE(tbl, nullptr);
    // idx_books_author + idx_books_title + sqlite_autoindex (PK)
    EXPECT_GE(tbl->index_count, 2);
    rdb_close(conn);
}

TEST_F(RdbTest, IndexDetails) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* tbl = rdb_get_table(conn, "books");

    // find idx_books_title
    bool found_title_idx = false;
    for (int i = 0; i < tbl->index_count; i++) {
        if (strcmp(tbl->indexes[i].name, "idx_books_title") == 0) {
            EXPECT_TRUE(tbl->indexes[i].unique);
            EXPECT_EQ(tbl->indexes[i].column_count, 1);
            EXPECT_STREQ(tbl->indexes[i].columns[0], "title");
            found_title_idx = true;
        }
    }
    EXPECT_TRUE(found_title_idx) << "idx_books_title not found";
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §6 Foreign Key Metadata
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, ForeignKeys) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* books = rdb_get_table(conn, "books");
    ASSERT_NE(books, nullptr);

    EXPECT_EQ(books->fk_count, 1);
    EXPECT_STREQ(books->foreign_keys[0].column, "author_id");
    EXPECT_STREQ(books->foreign_keys[0].ref_table, "authors");
    EXPECT_STREQ(books->foreign_keys[0].ref_column, "id");
    // link_name: strip _id from "author_id" → "author"
    EXPECT_STREQ(books->foreign_keys[0].link_name, "author");
    rdb_close(conn);
}

TEST_F(RdbTest, ReverseForeignKeys) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* authors = rdb_get_table(conn, "authors");
    ASSERT_NE(authors, nullptr);

    // authors should have a reverse FK from books
    EXPECT_EQ(authors->reverse_fk_count, 1);
    EXPECT_STREQ(authors->reverse_fks[0].ref_table, "books");
    EXPECT_STREQ(authors->reverse_fks[0].ref_column, "author_id");
    EXPECT_STREQ(authors->reverse_fks[0].link_name, "books");
    rdb_close(conn);
}

TEST_F(RdbTest, NoForeignKeys) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    RdbTable* authors = rdb_get_table(conn, "authors");
    EXPECT_EQ(authors->fk_count, 0);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §7 Query Execution
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, PrepareAndStep) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);

    RdbStmt* stmt = rdb_prepare(conn, "SELECT id, name FROM authors ORDER BY id");
    ASSERT_NE(stmt, nullptr);

    int rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_ROW);

    RdbValue id_val = rdb_column_value(stmt, 0);
    EXPECT_EQ(id_val.type, RDB_TYPE_INT);
    EXPECT_EQ(id_val.int_val, 1);

    RdbValue name_val = rdb_column_value(stmt, 1);
    EXPECT_EQ(name_val.type, RDB_TYPE_STRING);
    EXPECT_STREQ(name_val.str_val, "Alice");

    // step to second row
    rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_ROW);
    RdbValue id2 = rdb_column_value(stmt, 0);
    EXPECT_EQ(id2.int_val, 2);

    // step to third row
    rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_ROW);

    // step past last row
    rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_DONE);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, ColumnCount) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT id, name, rating FROM authors");
    ASSERT_NE(stmt, nullptr);

    EXPECT_EQ(rdb_column_count(stmt), 3);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, PrepareBadSql) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "INVALID SQL STATEMENT");
    EXPECT_EQ(stmt, nullptr);
    rdb_close(conn);
}

TEST_F(RdbTest, PrepareNull) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, NULL);
    EXPECT_EQ(stmt, nullptr);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §8 Parameter Binding
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, BindInt) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT name FROM authors WHERE id = ?");
    ASSERT_NE(stmt, nullptr);

    EXPECT_EQ(rdb_bind_int(stmt, 1, 2), RDB_OK);

    int rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_STREQ(val.str_val, "Bob");

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BindFloat) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT name FROM authors WHERE rating > ?");
    ASSERT_NE(stmt, nullptr);

    EXPECT_EQ(rdb_bind_float(stmt, 1, 4.0), RDB_OK);

    int count = 0;
    while (rdb_step(stmt) == RDB_ROW) count++;
    // Alice (4.8) and Charlie (4.2) have rating > 4.0
    EXPECT_EQ(count, 2);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BindString) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT id FROM authors WHERE name = ?");
    ASSERT_NE(stmt, nullptr);

    EXPECT_EQ(rdb_bind_string(stmt, 1, "Charlie"), RDB_OK);

    int rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.int_val, 3);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BindNull) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT id FROM books WHERE metadata IS ?");
    ASSERT_NE(stmt, nullptr);

    EXPECT_EQ(rdb_bind_null(stmt, 1), RDB_OK);

    int count = 0;
    while (rdb_step(stmt) == RDB_ROW) count++;
    // Book 3 has NULL metadata
    EXPECT_EQ(count, 1);

    rdb_finalize(stmt);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §9 Value Types from Result Set
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, ValueType_Integer) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT id FROM authors LIMIT 1");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_INT);
    EXPECT_FALSE(val.is_null);
    EXPECT_EQ(val.int_val, 1);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, ValueType_Float) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT rating FROM authors WHERE id = 1");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(val.float_val, 4.8);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, ValueType_Text) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT name FROM authors WHERE id = 1");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_STREQ(val.str_val, "Alice");
    EXPECT_EQ(val.str_len, 5);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, ValueType_Null) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT metadata FROM books WHERE id = 3");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_NULL);
    EXPECT_TRUE(val.is_null);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, ValueType_JsonStored) {
    // JSON columns are stored as TEXT in SQLite
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT metadata FROM books WHERE id = 1");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue val = rdb_column_value(stmt, 0);
    // at the raw RDB level, SQLite returns TEXT for JSON columns
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_NE(val.str_val, nullptr);
    EXPECT_TRUE(val.str_len > 0);

    rdb_finalize(stmt);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §10 Row Count
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, RowCount) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);

    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    EXPECT_EQ(rdb_row_count(conn, "books"), 3);

    rdb_close(conn);
}

TEST_F(RdbTest, RowCountUnknownTable) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_load_schema(conn);
    EXPECT_EQ(rdb_row_count(conn, "nonexistent"), -1);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §11 Error Messages
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, ErrorMsg) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);

    const char* msg = rdb_error_msg(conn);
    EXPECT_NE(msg, nullptr);

    rdb_close(conn);
}

TEST_F(RdbTest, ErrorMsgNull) {
    EXPECT_STREQ(rdb_error_msg(NULL), "rdb: unknown error");
}

/* ══════════════════════════════════════════════════════════════════════
 * §12 Finalize NULL safety
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, FinalizeNull) {
    // should not crash
    rdb_finalize(NULL);
}

TEST_F(RdbTest, StepNull) {
    EXPECT_EQ(rdb_step(NULL), RDB_ERROR);
}

TEST_F(RdbTest, ColumnValueNull) {
    RdbValue val = rdb_column_value(NULL, 0);
    EXPECT_TRUE(val.is_null);
}

TEST_F(RdbTest, ColumnCountNull) {
    EXPECT_EQ(rdb_column_count(NULL), 0);
}

/* ══════════════════════════════════════════════════════════════════════
 * §13 Multi-row Iteration
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, IterateAllAuthors) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT * FROM authors ORDER BY id");
    ASSERT_NE(stmt, nullptr);

    int rows = 0;
    while (rdb_step(stmt) == RDB_ROW) rows++;
    EXPECT_EQ(rows, 3);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, IterateAllBooks) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT * FROM books ORDER BY id");
    ASSERT_NE(stmt, nullptr);

    int rows = 0;
    while (rdb_step(stmt) == RDB_ROW) rows++;
    EXPECT_EQ(rows, 3);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, SelectFromView) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT * FROM active_authors");
    ASSERT_NE(stmt, nullptr);

    int rows = 0;
    while (rdb_step(stmt) == RDB_ROW) rows++;
    // 2 active authors (Alice=1, Bob=1; Charlie=0)
    EXPECT_EQ(rows, 2);

    rdb_finalize(stmt);
    rdb_close(conn);
}
