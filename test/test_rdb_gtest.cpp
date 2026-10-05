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
        "CREATE TRIGGER trg_books_before_insert BEFORE INSERT ON books BEGIN SELECT 1; END;"
        "CREATE TRIGGER trg_books_after_update AFTER UPDATE ON books BEGIN SELECT 1; END;"
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

/* ══════════════════════════════════════════════════════════════════════
 * §14 Type Mapping Edge Cases
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_TYPES = "temp/test_rdb_types.db";

static void create_types_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE types_test ("
        "  col_integer INTEGER,"
        "  col_int INT,"
        "  col_bigint BIGINT,"
        "  col_smallint SMALLINT,"
        "  col_tinyint TINYINT,"
        "  col_boolean BOOLEAN,"
        "  col_real REAL,"
        "  col_double DOUBLE,"
        "  col_float FLOAT,"
        "  col_decimal DECIMAL(10,2),"
        "  col_numeric NUMERIC,"
        "  col_date DATE,"
        "  col_datetime DATETIME,"
        "  col_timestamp TIMESTAMP,"
        "  col_json JSON,"
        "  col_blob BLOB,"
        "  col_text TEXT,"
        "  col_varchar VARCHAR(255),"
        "  col_char CHAR(10),"
        "  col_clob CLOB,"
        "  col_notype"
        ");"
        "INSERT INTO types_test VALUES ("
        "  42, 100, 9999999999, 32000, 127, 1, 3.14, 2.718, 1.5, "
        "  19.99, 123.45, '2024-03-15', '2024-03-15 10:30:00', "
        "  '2024-03-15T08:00:00Z', '{\"key\":\"value\"}', X'DEADBEEF', "
        "  'hello', 'world', 'fixed', 'large text', 'untyped'"
        ");";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

class RdbTypesTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_types_database(TEST_DB_TYPES);
        conn = rdb_open(pool, TEST_DB_TYPES, "sqlite", true);
        ASSERT_NE(conn, nullptr);
        rdb_load_schema(conn);
    }

    void TearDown() override {
        rdb_close(conn);
        pool_destroy(pool);
        unlink(TEST_DB_TYPES);
    }
};

TEST_F(RdbTypesTest, MapInteger) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_integer");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
}

TEST_F(RdbTypesTest, MapInt) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_int");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
}

TEST_F(RdbTypesTest, MapBigint) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_bigint");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
}

TEST_F(RdbTypesTest, MapSmallint) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_smallint");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
}

TEST_F(RdbTypesTest, MapTinyint) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_tinyint");
    EXPECT_EQ(col->type, RDB_TYPE_INT);
}

TEST_F(RdbTypesTest, MapBoolean) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_boolean");
    EXPECT_EQ(col->type, RDB_TYPE_BOOL);
}

TEST_F(RdbTypesTest, MapReal) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_real");
    EXPECT_EQ(col->type, RDB_TYPE_FLOAT);
}

TEST_F(RdbTypesTest, MapDouble) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_double");
    EXPECT_EQ(col->type, RDB_TYPE_FLOAT);
}

TEST_F(RdbTypesTest, MapFloat) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_float");
    EXPECT_EQ(col->type, RDB_TYPE_FLOAT);
}

TEST_F(RdbTypesTest, MapDecimal) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_decimal");
    EXPECT_EQ(col->type, RDB_TYPE_DECIMAL);
}

TEST_F(RdbTypesTest, MapNumeric) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_numeric");
    EXPECT_EQ(col->type, RDB_TYPE_DECIMAL);
}

TEST_F(RdbTypesTest, MapDate) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_date");
    EXPECT_EQ(col->type, RDB_TYPE_DATETIME);
}

TEST_F(RdbTypesTest, MapDatetime) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_datetime");
    EXPECT_EQ(col->type, RDB_TYPE_DATETIME);
}

TEST_F(RdbTypesTest, MapTimestamp) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_timestamp");
    EXPECT_EQ(col->type, RDB_TYPE_DATETIME);
}

TEST_F(RdbTypesTest, MapJson) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_json");
    EXPECT_EQ(col->type, RDB_TYPE_JSON);
}

TEST_F(RdbTypesTest, MapBlob) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_blob");
    EXPECT_EQ(col->type, RDB_TYPE_BLOB);
}

TEST_F(RdbTypesTest, MapText) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_text");
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
}

TEST_F(RdbTypesTest, MapVarchar) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_varchar");
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
}

TEST_F(RdbTypesTest, MapChar) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_char");
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
}

TEST_F(RdbTypesTest, MapClob) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_clob");
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
}

TEST_F(RdbTypesTest, MapNotype) {
    RdbColumn* col = rdb_get_column(rdb_get_table(conn, "types_test"), "col_notype");
    // empty type declaration → default STRING
    EXPECT_EQ(col->type, RDB_TYPE_STRING);
}

TEST_F(RdbTypesTest, ReadIntegerValue) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_integer FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_INT);
    EXPECT_EQ(val.int_val, 42);
    rdb_finalize(stmt);
}

TEST_F(RdbTypesTest, ReadBigintValue) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_bigint FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.int_val, 9999999999LL);
    rdb_finalize(stmt);
}

TEST_F(RdbTypesTest, ReadRealValue) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_real FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(val.float_val, 3.14);
    rdb_finalize(stmt);
}

TEST_F(RdbTypesTest, ReadBlobIsDeferred) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_blob FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_BLOB);
    EXPECT_TRUE(val.is_null); // deferred to Phase 2
    rdb_finalize(stmt);
}

TEST_F(RdbTypesTest, ReadDatetimeAsText) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_datetime FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_STREQ(val.str_val, "2024-03-15 10:30:00");
    rdb_finalize(stmt);
}

TEST_F(RdbTypesTest, ReadJsonAsText) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_json FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_STREQ(val.str_val, "{\"key\":\"value\"}");
    rdb_finalize(stmt);
}

/* ══════════════════════════════════════════════════════════════════════
 * §15 Schema with Multiple Foreign Keys
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_FK = "temp/test_rdb_fk.db";

static void create_fk_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE departments (id INTEGER PRIMARY KEY, name TEXT);"
        "CREATE TABLE roles (id INTEGER PRIMARY KEY, title TEXT);"
        "CREATE TABLE employees ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT,"
        "  department_id INTEGER,"
        "  role_id INTEGER,"
        "  manager_id INTEGER,"
        "  FOREIGN KEY (department_id) REFERENCES departments(id),"
        "  FOREIGN KEY (role_id) REFERENCES roles(id),"
        "  FOREIGN KEY (manager_id) REFERENCES employees(id)"
        ");"
        "INSERT INTO departments VALUES (1, 'Engineering'), (2, 'Sales');"
        "INSERT INTO roles VALUES (1, 'Developer'), (2, 'Manager');"
        "INSERT INTO employees VALUES (1, 'Alice', 1, 2, NULL);"
        "INSERT INTO employees VALUES (2, 'Bob', 1, 1, 1);"
        "INSERT INTO employees VALUES (3, 'Carol', 2, 1, 1);";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

class RdbFkTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_fk_database(TEST_DB_FK);
        conn = rdb_open(pool, TEST_DB_FK, "sqlite", true);
        ASSERT_NE(conn, nullptr);
        rdb_load_schema(conn);
    }

    void TearDown() override {
        rdb_close(conn);
        pool_destroy(pool);
        unlink(TEST_DB_FK);
    }
};

TEST_F(RdbFkTest, MultipleForeignKeys) {
    RdbTable* emp = rdb_get_table(conn, "employees");
    ASSERT_NE(emp, nullptr);
    EXPECT_EQ(emp->fk_count, 3);
}

TEST_F(RdbFkTest, FkLinkName_StripId) {
    RdbTable* emp = rdb_get_table(conn, "employees");
    // department_id → link_name "department"
    bool found = false;
    for (int i = 0; i < emp->fk_count; i++) {
        if (strcmp(emp->foreign_keys[i].column, "department_id") == 0) {
            EXPECT_STREQ(emp->foreign_keys[i].link_name, "department");
            EXPECT_STREQ(emp->foreign_keys[i].ref_table, "departments");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(RdbFkTest, FkLinkName_RoleId) {
    RdbTable* emp = rdb_get_table(conn, "employees");
    bool found = false;
    for (int i = 0; i < emp->fk_count; i++) {
        if (strcmp(emp->foreign_keys[i].column, "role_id") == 0) {
            EXPECT_STREQ(emp->foreign_keys[i].link_name, "role");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(RdbFkTest, SelfReferentialFk) {
    RdbTable* emp = rdb_get_table(conn, "employees");
    bool found = false;
    for (int i = 0; i < emp->fk_count; i++) {
        if (strcmp(emp->foreign_keys[i].column, "manager_id") == 0) {
            EXPECT_STREQ(emp->foreign_keys[i].ref_table, "employees");
            EXPECT_STREQ(emp->foreign_keys[i].ref_column, "id");
            EXPECT_STREQ(emp->foreign_keys[i].link_name, "manager");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(RdbFkTest, ReverseFks_Departments) {
    RdbTable* dept = rdb_get_table(conn, "departments");
    ASSERT_NE(dept, nullptr);
    // employees.department_id → departments
    EXPECT_EQ(dept->reverse_fk_count, 1);
    EXPECT_STREQ(dept->reverse_fks[0].ref_table, "employees");
}

TEST_F(RdbFkTest, ReverseFks_Roles) {
    RdbTable* roles = rdb_get_table(conn, "roles");
    ASSERT_NE(roles, nullptr);
    EXPECT_EQ(roles->reverse_fk_count, 1);
    EXPECT_STREQ(roles->reverse_fks[0].ref_table, "employees");
}

TEST_F(RdbFkTest, ReverseFks_SelfRef) {
    RdbTable* emp = rdb_get_table(conn, "employees");
    // employees.manager_id → employees (self-referential)
    EXPECT_GE(emp->reverse_fk_count, 1);
    bool found = false;
    for (int i = 0; i < emp->reverse_fk_count; i++) {
        if (strcmp(emp->reverse_fks[i].ref_table, "employees") == 0) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

/* ══════════════════════════════════════════════════════════════════════
 * §16 Composite Primary Keys and Multi-Column Indexes
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_COMPOSITE = "temp/test_rdb_composite.db";

static void create_composite_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE order_items ("
        "  order_id INTEGER NOT NULL,"
        "  product_id INTEGER NOT NULL,"
        "  quantity INTEGER,"
        "  price REAL,"
        "  PRIMARY KEY (order_id, product_id)"
        ");"
        "CREATE INDEX idx_order_items_price_qty ON order_items(price, quantity);"
        "INSERT INTO order_items VALUES (1, 100, 2, 9.99);"
        "INSERT INTO order_items VALUES (1, 200, 1, 19.99);"
        "INSERT INTO order_items VALUES (2, 100, 3, 9.99);";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

class RdbCompositeTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_composite_database(TEST_DB_COMPOSITE);
        conn = rdb_open(pool, TEST_DB_COMPOSITE, "sqlite", true);
        ASSERT_NE(conn, nullptr);
        rdb_load_schema(conn);
    }

    void TearDown() override {
        rdb_close(conn);
        pool_destroy(pool);
        unlink(TEST_DB_COMPOSITE);
    }
};

TEST_F(RdbCompositeTest, CompositePrimaryKeys) {
    RdbTable* tbl = rdb_get_table(conn, "order_items");
    ASSERT_NE(tbl, nullptr);

    RdbColumn* order_id = rdb_get_column(tbl, "order_id");
    RdbColumn* product_id = rdb_get_column(tbl, "product_id");
    EXPECT_TRUE(order_id->primary_key);
    EXPECT_TRUE(product_id->primary_key);
    // pk_index should be > 0 for both
    EXPECT_GT(order_id->pk_index, 0);
    EXPECT_GT(product_id->pk_index, 0);
    // they should have different pk_index values
    EXPECT_NE(order_id->pk_index, product_id->pk_index);
}

TEST_F(RdbCompositeTest, NonPkColumn) {
    RdbTable* tbl = rdb_get_table(conn, "order_items");
    RdbColumn* qty = rdb_get_column(tbl, "quantity");
    EXPECT_FALSE(qty->primary_key);
    EXPECT_EQ(qty->pk_index, 0);
}

TEST_F(RdbCompositeTest, MultiColumnIndex) {
    RdbTable* tbl = rdb_get_table(conn, "order_items");
    bool found = false;
    for (int i = 0; i < tbl->index_count; i++) {
        if (strcmp(tbl->indexes[i].name, "idx_order_items_price_qty") == 0) {
            EXPECT_EQ(tbl->indexes[i].column_count, 2);
            EXPECT_FALSE(tbl->indexes[i].unique);
            EXPECT_STREQ(tbl->indexes[i].columns[0], "price");
            EXPECT_STREQ(tbl->indexes[i].columns[1], "quantity");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(RdbCompositeTest, RowCountComposite) {
    EXPECT_EQ(rdb_row_count(conn, "order_items"), 3);
}

/* ══════════════════════════════════════════════════════════════════════
 * §17 NULL Handling in Result Rows
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_NULLS = "temp/test_rdb_nulls.db";

static void create_nulls_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE nullable_test ("
        "  id INTEGER PRIMARY KEY,"
        "  int_col INTEGER,"
        "  text_col TEXT,"
        "  real_col REAL,"
        "  date_col DATE"
        ");"
        "INSERT INTO nullable_test VALUES (1, NULL, NULL, NULL, NULL);"
        "INSERT INTO nullable_test VALUES (2, 42, 'hello', 3.14, '2024-01-01');"
        "INSERT INTO nullable_test VALUES (3, NULL, 'world', NULL, '2024-06-15');";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

class RdbNullTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_nulls_database(TEST_DB_NULLS);
        conn = rdb_open(pool, TEST_DB_NULLS, "sqlite", true);
        ASSERT_NE(conn, nullptr);
        rdb_load_schema(conn);
    }

    void TearDown() override {
        rdb_close(conn);
        pool_destroy(pool);
        unlink(TEST_DB_NULLS);
    }
};

TEST_F(RdbNullTest, AllNullRow) {
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT int_col, text_col, real_col, date_col FROM nullable_test WHERE id = 1");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    for (int c = 0; c < 4; c++) {
        RdbValue val = rdb_column_value(stmt, c);
        EXPECT_EQ(val.type, RDB_TYPE_NULL) << "Column " << c << " should be NULL";
        EXPECT_TRUE(val.is_null) << "Column " << c << " is_null should be true";
    }
    rdb_finalize(stmt);
}

TEST_F(RdbNullTest, AllPopulatedRow) {
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT int_col, text_col, real_col, date_col FROM nullable_test WHERE id = 2");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue v0 = rdb_column_value(stmt, 0);
    EXPECT_EQ(v0.type, RDB_TYPE_INT);
    EXPECT_EQ(v0.int_val, 42);

    RdbValue v1 = rdb_column_value(stmt, 1);
    EXPECT_EQ(v1.type, RDB_TYPE_STRING);
    EXPECT_STREQ(v1.str_val, "hello");

    RdbValue v2 = rdb_column_value(stmt, 2);
    EXPECT_EQ(v2.type, RDB_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(v2.float_val, 3.14);

    RdbValue v3 = rdb_column_value(stmt, 3);
    EXPECT_EQ(v3.type, RDB_TYPE_STRING);
    EXPECT_STREQ(v3.str_val, "2024-01-01");

    rdb_finalize(stmt);
}

TEST_F(RdbNullTest, MixedNulls) {
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT int_col, text_col, real_col FROM nullable_test WHERE id = 3");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);

    RdbValue v0 = rdb_column_value(stmt, 0);
    EXPECT_TRUE(v0.is_null);

    RdbValue v1 = rdb_column_value(stmt, 1);
    EXPECT_EQ(v1.type, RDB_TYPE_STRING);
    EXPECT_STREQ(v1.str_val, "world");

    RdbValue v2 = rdb_column_value(stmt, 2);
    EXPECT_TRUE(v2.is_null);

    rdb_finalize(stmt);
}

TEST_F(RdbNullTest, NullableColumnMetadata) {
    RdbTable* tbl = rdb_get_table(conn, "nullable_test");
    // SQLite's PRAGMA table_info reports notnull=0 for INTEGER PRIMARY KEY
    // (rowid alias can technically accept NULL in edge cases), so we check
    // that explicitly-unconstrained columns are nullable.
    RdbColumn* id = rdb_get_column(tbl, "id");
    RdbColumn* int_col = rdb_get_column(tbl, "int_col");
    RdbColumn* text_col = rdb_get_column(tbl, "text_col");

    EXPECT_TRUE(id->primary_key);     // confirm it IS the primary key
    EXPECT_TRUE(int_col->nullable);
    EXPECT_TRUE(text_col->nullable);
}

/* ══════════════════════════════════════════════════════════════════════
 * §18 Large Integer / Edge Value Tests
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_EDGE = "temp/test_rdb_edge.db";

static void create_edge_database(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE edge_values ("
        "  id INTEGER PRIMARY KEY,"
        "  big_pos INTEGER,"
        "  big_neg INTEGER,"
        "  zero_int INTEGER,"
        "  tiny_float REAL,"
        "  neg_float REAL,"
        "  empty_str TEXT,"
        "  unicode_str TEXT"
        ");"
        "INSERT INTO edge_values VALUES "
        "  (1, 9223372036854775807, -9223372036854775808, 0, "
        "   0.000001, -99999.99, '', '日本語テスト');";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

class RdbEdgeTest : public ::testing::Test {
protected:
    Pool* pool;
    RdbConn* conn;

    void SetUp() override {
        ensure_temp_dir();
        pool = pool_create();
        rdb_sqlite_register();
        create_edge_database(TEST_DB_EDGE);
        conn = rdb_open(pool, TEST_DB_EDGE, "sqlite", true);
        ASSERT_NE(conn, nullptr);
    }

    void TearDown() override {
        rdb_close(conn);
        pool_destroy(pool);
        unlink(TEST_DB_EDGE);
    }
};

TEST_F(RdbEdgeTest, MaxInt64) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT big_pos FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.int_val, INT64_MAX);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, MinInt64) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT big_neg FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.int_val, INT64_MIN);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, ZeroInt) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT zero_int FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_INT);
    EXPECT_EQ(val.int_val, 0);
    EXPECT_FALSE(val.is_null);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, TinyFloat) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT tiny_float FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_NEAR(val.float_val, 0.000001, 1e-7);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, NegativeFloat) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT neg_float FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_DOUBLE_EQ(val.float_val, -99999.99);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, EmptyString) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT empty_str FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_EQ(val.str_len, 0);
    EXPECT_STREQ(val.str_val, "");
    EXPECT_FALSE(val.is_null);
    rdb_finalize(stmt);
}

TEST_F(RdbEdgeTest, UnicodeString) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT unicode_str FROM edge_values");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_STRING);
    EXPECT_STREQ(val.str_val, "日本語テスト");
    EXPECT_GT(val.str_len, 0);
    rdb_finalize(stmt);
}

/* ══════════════════════════════════════════════════════════════════════
 * §19 Read-Write Mode
 * ══════════════════════════════════════════════════════════════════════ */

static const char* TEST_DB_RW = "temp/test_rdb_rw.db";

TEST_F(RdbTest, OpenReadWrite) {
    // create a writable database
    sqlite3* db = NULL;
    sqlite3_open(TEST_DB_RW, &db);
    sqlite3_exec(db, "CREATE TABLE t (id INTEGER)", NULL, NULL, NULL);
    sqlite3_close(db);

    Pool* p = pool_create();
    RdbConn* conn = rdb_open(p, TEST_DB_RW, "sqlite", false);
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->readonly);

    // should be able to insert
    RdbStmt* stmt = rdb_prepare(conn, "INSERT INTO t VALUES (1)");
    ASSERT_NE(stmt, nullptr);
    int rc = rdb_step(stmt);
    EXPECT_EQ(rc, RDB_DONE);
    rdb_finalize(stmt);

    // verify insert
    stmt = rdb_prepare(conn, "SELECT id FROM t");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.int_val, 1);
    rdb_finalize(stmt);

    rdb_close(conn);
    pool_destroy(p);
    unlink(TEST_DB_RW);
}

/* ══════════════════════════════════════════════════════════════════════
 * §20 Multiple Statements on Same Connection
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, MultipleStatementsSequential) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);

    // first query
    RdbStmt* s1 = rdb_prepare(conn, "SELECT COUNT(*) FROM authors");
    ASSERT_EQ(rdb_step(s1), RDB_ROW);
    EXPECT_EQ(rdb_column_value(s1, 0).int_val, 3);
    rdb_finalize(s1);

    // second query on same connection
    RdbStmt* s2 = rdb_prepare(conn, "SELECT COUNT(*) FROM books");
    ASSERT_EQ(rdb_step(s2), RDB_ROW);
    EXPECT_EQ(rdb_column_value(s2, 0).int_val, 3);
    rdb_finalize(s2);

    rdb_close(conn);
}

TEST_F(RdbTest, JoinQuery) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);

    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT b.title, a.name FROM books b "
        "JOIN authors a ON b.author_id = a.id "
        "ORDER BY b.id");
    ASSERT_NE(stmt, nullptr);

    // first row
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue title = rdb_column_value(stmt, 0);
    RdbValue author = rdb_column_value(stmt, 1);
    EXPECT_STREQ(title.str_val, "Lambda Calculus");
    EXPECT_STREQ(author.str_val, "Alice");

    // count remaining rows
    int rows = 1;
    while (rdb_step(stmt) == RDB_ROW) rows++;
    EXPECT_EQ(rows, 3);

    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, AggregateQuery) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT AVG(rating) FROM authors");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_FLOAT);
    // avg of 4.8, 3.5, 4.2 = 4.166...
    EXPECT_NEAR(val.float_val, 4.1666, 0.01);
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, GroupByQuery) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT author_id, COUNT(*) FROM books GROUP BY author_id ORDER BY author_id");
    ASSERT_NE(stmt, nullptr);

    // Alice has 2 books
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 1);
    EXPECT_EQ(rdb_column_value(stmt, 1).int_val, 2);

    // Bob has 1 book
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 2);
    EXPECT_EQ(rdb_column_value(stmt, 1).int_val, 1);

    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, SubqueryBind) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT title FROM books WHERE author_id = ("
        "  SELECT id FROM authors WHERE name = ?"
        ") ORDER BY title");
    ASSERT_NE(stmt, nullptr);
    rdb_bind_string(stmt, 1, "Alice");

    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_STREQ(rdb_column_value(stmt, 0).str_val, "Lambda Calculus");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_STREQ(rdb_column_value(stmt, 0).str_val, "Type Theory");
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);

    rdb_finalize(stmt);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §21 Schema Reload
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, SchemaReload) {
    Pool* p = pool_create();
    // create a db and add a table after initial schema load
    sqlite3* db = NULL;
    const char* path = "temp/test_rdb_reload.db";
    sqlite3_open(path, &db);
    sqlite3_exec(db, "CREATE TABLE t1 (id INTEGER)", NULL, NULL, NULL);
    sqlite3_close(db);

    RdbConn* conn = rdb_open(p, path, "sqlite", false);
    rdb_load_schema(conn);
    EXPECT_EQ(conn->schema.table_count, 1);

    // add another table via raw sqlite from outside
    db = (sqlite3*)conn->handle;
    sqlite3_exec(db, "CREATE TABLE t2 (id INTEGER)", NULL, NULL, NULL);

    // reload schema should pick up new table
    rdb_load_schema(conn);
    EXPECT_EQ(conn->schema.table_count, 2);

    rdb_close(conn);
    pool_destroy(p);
    unlink(path);
}

/* ══════════════════════════════════════════════════════════════════════
 * §22 Error After Bad Prepare
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, ErrorMsgAfterBadPrepare) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    rdb_prepare(conn, "SELECT * FROM nonexistent_table");
    const char* msg = rdb_error_msg(conn);
    ASSERT_NE(msg, nullptr);
    // should mention the table
    EXPECT_NE(strstr(msg, "nonexistent_table"), nullptr);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §23 Trigger Schema
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, TriggerCount) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);
    RdbTable* books = rdb_get_table(conn, "books");
    ASSERT_NE(books, nullptr);
    EXPECT_EQ(books->trigger_count, 2);
    // authors table has no triggers
    RdbTable* authors = rdb_get_table(conn, "authors");
    ASSERT_NE(authors, nullptr);
    EXPECT_EQ(authors->trigger_count, 0);
    rdb_close(conn);
}

TEST_F(RdbTest, TriggerDetails) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);
    RdbTable* books = rdb_get_table(conn, "books");
    ASSERT_NE(books, nullptr);
    ASSERT_EQ(books->trigger_count, 2);
    // triggers are ordered as returned by sqlite_master (insertion order)
    EXPECT_STREQ(books->triggers[0].name, "trg_books_before_insert");
    EXPECT_STREQ(books->triggers[0].timing, "BEFORE");
    EXPECT_STREQ(books->triggers[0].event, "INSERT");
    EXPECT_STREQ(books->triggers[1].name, "trg_books_after_update");
    EXPECT_STREQ(books->triggers[1].timing, "AFTER");
    EXPECT_STREQ(books->triggers[1].event, "UPDATE");
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §24 Function Schema
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(RdbTest, FunctionCount) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);
    // SQLite always exposes built-in + extension functions
    EXPECT_GT(conn->schema.function_count, 0);
    rdb_close(conn);
}

TEST_F(RdbTest, FunctionDetails) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    rdb_load_schema(conn);
    ASSERT_GT(conn->schema.function_count, 0);

    // find a well-known built-in function: 'abs'
    bool found_abs = false;
    for (int i = 0; i < conn->schema.function_count; i++) {
        RdbFunction* fn = &conn->schema.functions[i];
        if (strcmp(fn->name, "abs") == 0 && fn->narg == 1) {
            found_abs = true;
            EXPECT_TRUE(fn->builtin);
            EXPECT_STREQ(fn->type, "scalar");
            EXPECT_EQ(fn->narg, 1);
            break;
        }
    }
    EXPECT_TRUE(found_abs) << "expected to find built-in 'abs' function";

    // find a well-known aggregate: 'sum'
    bool found_sum = false;
    for (int i = 0; i < conn->schema.function_count; i++) {
        RdbFunction* fn = &conn->schema.functions[i];
        if (strcmp(fn->name, "sum") == 0) {
            found_sum = true;
            EXPECT_STREQ(fn->type, "window");  // sum is a window function in SQLite
            break;
        }
    }
    EXPECT_TRUE(found_sum) << "expected to find 'sum' function";

    // all functions have non-empty names
    for (int i = 0; i < conn->schema.function_count; i++) {
        EXPECT_NE(conn->schema.functions[i].name, nullptr);
        EXPECT_GT(strlen(conn->schema.functions[i].name), (size_t)0);
    }
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §30 Credential redaction (RDB9)
 * ══════════════════════════════════════════════════════════════════════ */

static const char* redact(const char* uri) {
    static char buf[512];
    return rdb_redact_uri(uri, buf, sizeof(buf));
}

TEST(RdbRedact, UriUserinfoPassword) {
    EXPECT_STREQ(redact("postgresql://bob:s3cret@db.local:5432/shop"),
              "postgresql://bob:***@db.local:5432/shop");
    EXPECT_STREQ(redact("postgres://bob@db.local/shop"), "postgres://bob@db.local/shop");
    // the last '@' ends the userinfo; an unescaped '/' stays inside the secret
    EXPECT_STREQ(redact("mysql://u:p@ss/x@h/db"), "mysql://u:***@h/db");
}

TEST(RdbRedact, QueryParameters) {
    EXPECT_STREQ(redact("mysql://u@h/db?ssl-mode=REQUIRED&password=x&PWD=y#frag"),
              "mysql://u@h/db?ssl-mode=REQUIRED&password=***&PWD=***#frag");
}

TEST(RdbRedact, LibpqKeyValue) {
    EXPECT_STREQ(redact("host=localhost user=bob password=secret dbname=x"),
              "host=localhost user=bob password=*** dbname=x");
    EXPECT_STREQ(redact("host=h password = 'se cret\\'x' port=5"),
              "host=h password = *** port=5");
}

TEST(RdbRedact, PathsUnchanged) {
    EXPECT_STREQ(redact("temp/test_rdb.db"), "temp/test_rdb.db");
    EXPECT_STREQ(redact("/data/a=b.db"), "/data/a=b.db");
    EXPECT_STREQ(redact(":memory:"), ":memory:");
}

TEST(RdbRedact, TruncatesWithTerminator) {
    char buf[8];
    rdb_redact_uri("postgresql://bob:secret@h/db", buf, sizeof(buf));
    EXPECT_STREQ(buf, "postgre");
}

/* ══════════════════════════════════════════════════════════════════════
 * §31 Dialect rendering (RDB5)
 * ══════════════════════════════════════════════════════════════════════ */

TEST(RdbDialect, IdentifierQuotesAreDoubled) {
    RdbDialect ansi = { RDB_PLACEHOLDER_DOLLAR, '"' };
    RdbDialect mysql = { RDB_PLACEHOLDER_QMARK, '`' };
    StrBuf* sb = strbuf_new();
    rdb_append_ident(sb, &ansi, "we\"ird");
    strbuf_append_char(sb, ' ');
    rdb_append_ident(sb, &mysql, "a`b");
    EXPECT_STREQ(sb->str, "\"we\"\"ird\" `a``b`");
    strbuf_free(sb);
}

TEST(RdbDialect, PlaceholderStyles) {
    RdbDialect qnum = { RDB_PLACEHOLDER_QNUM, '"' };
    RdbDialect dollar = { RDB_PLACEHOLDER_DOLLAR, '"' };
    RdbDialect qmark = { RDB_PLACEHOLDER_QMARK, '`' };
    StrBuf* sb = strbuf_new();
    rdb_append_placeholder(sb, &qnum, 3);
    rdb_append_placeholder(sb, &dollar, 12);
    rdb_append_placeholder(sb, &qmark, 7);
    EXPECT_STREQ(sb->str, "?3$12?");
    strbuf_free(sb);
}

/* ══════════════════════════════════════════════════════════════════════
 * §32 Column batches through the shredding adapter (RDB7)
 * ══════════════════════════════════════════════════════════════════════ */

static int64_t arrow_i64(const struct ArrowArray* col, int64_t row) {
    return ((const int64_t*)col->buffers[1])[row];
}

static const char* arrow_str(const struct ArrowArray* col, int64_t row) {
    static char buf[256];
    const int32_t* offsets = (const int32_t*)col->buffers[1];
    const char* data = (const char*)col->buffers[2];
    snprintf(buf, sizeof(buf), "%.*s", (int)(offsets[row + 1] - offsets[row]), data + offsets[row]);
    return buf;
}

static bool arrow_valid(const struct ArrowArray* col, int64_t row) {
    const uint8_t* validity = (const uint8_t*)col->buffers[0];
    return !validity || (validity[row / 8] >> (row % 8)) & 1;
}

TEST_F(RdbTest, BatchSchemaFromDeclaredTypes) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT books.id AS id, title, price, published, metadata, rating FROM books "
        "JOIN authors ON authors.id = books.author_id ORDER BY books.id");
    ASSERT_NE(stmt, nullptr);
    struct ArrowSchema schema;
    ASSERT_EQ(rdb_result_schema(stmt, &schema), RDB_OK);
    EXPECT_STREQ(schema.format, "+s");
    ASSERT_EQ(schema.n_children, 6);
    EXPECT_STREQ(schema.children[0]->format, "l");
    EXPECT_STREQ(schema.children[0]->name, "id");
    EXPECT_STREQ(schema.children[1]->format, "u");
    EXPECT_STREQ(schema.children[2]->format, "u");   // DECIMAL travels as tagged text
    ASSERT_NE(schema.children[2]->metadata, nullptr);
    EXPECT_EQ(memcmp(schema.children[2]->metadata + 4 + 4 + 15 + 4, "decimal", 7), 0);
    EXPECT_STREQ(schema.children[5]->format, "g");
    schema.release(&schema);
    EXPECT_EQ(schema.release, nullptr);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BatchFetchSplitsRowsAndMarksNulls) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT id, title, price, metadata FROM books ORDER BY id");
    ASSERT_NE(stmt, nullptr);

    struct ArrowArray batch;
    ASSERT_EQ(rdb_fetch_batch(stmt, 2, &batch), RDB_ROW);
    EXPECT_EQ(batch.length, 2);
    ASSERT_EQ(batch.n_children, 4);
    EXPECT_EQ(arrow_i64(batch.children[0], 0), 1);
    EXPECT_EQ(arrow_i64(batch.children[0], 1), 2);
    EXPECT_STREQ(arrow_str(batch.children[1], 1), "Type Theory");
    EXPECT_STREQ(arrow_str(batch.children[2], 0), "29.99");
    batch.release(&batch);

    ASSERT_EQ(rdb_fetch_batch(stmt, 2, &batch), RDB_ROW);
    EXPECT_EQ(batch.length, 1);
    EXPECT_STREQ(arrow_str(batch.children[1], 0), "Functional Programming");
    EXPECT_STREQ(arrow_str(batch.children[2], 0), "24.5");
    EXPECT_FALSE(arrow_valid(batch.children[3], 0));
    EXPECT_EQ(batch.children[3]->null_count, 1);
    batch.release(&batch);

    EXPECT_EQ(rdb_fetch_batch(stmt, 2, &batch), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BatchTypesExpressionColumnsFromFirstRow) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT COUNT(*) AS n, 'x' || 'y' AS s FROM books");
    ASSERT_NE(stmt, nullptr);
    struct ArrowSchema schema;
    ASSERT_EQ(rdb_result_schema(stmt, &schema), RDB_OK);
    EXPECT_STREQ(schema.children[0]->format, "l");
    EXPECT_STREQ(schema.children[1]->format, "u");
    schema.release(&schema);
    // the row consumed to type the columns is still delivered
    struct ArrowArray batch;
    ASSERT_EQ(rdb_fetch_batch(stmt, 10, &batch), RDB_ROW);
    EXPECT_EQ(batch.length, 1);
    EXPECT_EQ(arrow_i64(batch.children[0], 0), 3);
    EXPECT_STREQ(arrow_str(batch.children[1], 0), "xy");
    batch.release(&batch);
    EXPECT_EQ(rdb_fetch_batch(stmt, 10, &batch), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, BatchOutlivesStatementAndConnection) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT name FROM authors ORDER BY id");
    struct ArrowArray batch;
    ASSERT_EQ(rdb_fetch_batch(stmt, 100, &batch), RDB_ROW);
    rdb_finalize(stmt);
    rdb_close(conn);
    EXPECT_EQ(batch.length, 3);
    EXPECT_STREQ(arrow_str(batch.children[0], 2), "Charlie");
    batch.release(&batch);
}

/* ══════════════════════════════════════════════════════════════════════
 * §33 Host-owned connections (JA16.1–JA16.3) with a fake driver
 * ══════════════════════════════════════════════════════════════════════ */

struct FakeDriverState {
    int  opens;
    int  closes;
    int  finalizes;
    bool skip_register;     // contract violation: open without registering
    bool wrong_peer;        // connects somewhere it was not authorised for
};
static FakeDriverState fake;
static int fake_stmt_token;

static int fake_resolve(const char* uri, RdbTarget* out, int cap, int* out_count) {
    (void)uri;
    if (cap < 1) return RDB_ERROR;
    memset(out, 0, sizeof(*out));
    out->kind = RDB_PEER_TCP;
    out->port = 5432;
    snprintf(out->host, sizeof(out->host), "db.example");
    *out_count = 1;
    return RDB_OK;
}

static int fake_open(const RdbHostAPI* host, void* open_ctx, const char* uri,
                     const RdbOpenOptions* opts, void** out_conn) {
    (void)opts;
    fake.opens++;
    void* native = &fake;
    if (!fake.skip_register) {
        RdbConnInfo info;
        memset(&info, 0, sizeof(info));
        int n = 0;
        fake_resolve(uri, &info.peer, 1, &n);
        if (fake.wrong_peer) info.peer.port = 6543;
        info.socket_fd = -1;
        if (host->conn_register(open_ctx, native, &info) != RDB_OK) {
            fake.closes++;      // a refused driver closes its own connection
            return RDB_ERROR;
        }
    }
    *out_conn = native;
    return RDB_OK;
}

static void fake_close(void* conn) { (void)conn; fake.closes++; }
static int fake_load_schema(void*, const RdbHostAPI*, void*, RdbSchema*) { return RDB_OK; }
static int fake_prepare(void*, const char*, void** out) { *out = &fake_stmt_token; return RDB_OK; }
static int fake_bind(void*, int, const RdbParam*) { return RDB_OK; }
static int fake_step(void*) { return RDB_DONE; }
static int fake_column_count(void*) { return 0; }
static RdbValue fake_value(void*, int) { RdbValue v; memset(&v, 0, sizeof(v)); v.is_null = true; return v; }
static void fake_finalize(void*) { fake.finalizes++; }
static const char* fake_error(void*) { return "fake"; }

static RdbDriver make_fake_driver(const char* name) {
    RdbDriver d;
    memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(RdbDriver);
    d.api_version = RDB_DRIVER_API_VERSION;
    d.name = name;
    d.resolve_targets = fake_resolve;
    d.open = fake_open;
    d.close = fake_close;
    d.load_schema = fake_load_schema;
    d.prepare = fake_prepare;
    d.bind_param = fake_bind;
    d.step = fake_step;
    d.column_count = fake_column_count;
    d.column_value = fake_value;
    d.finalize = fake_finalize;
    d.error_msg = fake_error;
    return d;
}

static RdbDriver fake_driver = make_fake_driver("fakedb");

struct FakeRegistry {
    int adds;
    int removes;
    int refused_authorizations;
    bool refuse_authorize;
    RdbConnLife* life;
};
static FakeRegistry registry;

static int reg_authorize(const char*, const RdbTarget*, int) {
    if (registry.refuse_authorize) registry.refused_authorizations++;
    return registry.refuse_authorize ? RDB_ERROR : RDB_OK;
}
static int reg_add(RdbConnLife* life, const char*, uint32_t* rid, void** owner) {
    registry.adds++;
    registry.life = life;
    *rid = 42;
    *owner = &registry;
    return RDB_OK;
}
static void reg_remove(void* owner, uint32_t rid) {
    EXPECT_EQ(owner, &registry);
    EXPECT_EQ(rid, 42u);
    registry.removes++;
    rdb_conn_life_close(registry.life);     // the registry is the close authority
}
static const RdbRegistryHooks test_hooks = { reg_authorize, reg_add, reg_remove };

class RdbRegistryTest : public ::testing::Test {
protected:
    Pool* pool;
    void SetUp() override {
        pool = pool_create();
        memset(&fake, 0, sizeof(fake));
        memset(&registry, 0, sizeof(registry));
        ASSERT_TRUE(rdb_register_driver(&fake_driver));
        rdb_set_registry_hooks(&test_hooks);
    }
    void TearDown() override {
        rdb_set_registry_hooks(NULL);
        pool_destroy(pool);
    }
};

TEST_F(RdbRegistryTest, OpenRegistersAndCloseGoesThroughRegistry) {
    RdbConn* conn = rdb_open(pool, "fakedb://bob:pw@db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->rid, 42u);
    EXPECT_STREQ(conn->uri, "fakedb://bob:***@db.example/x");
    EXPECT_EQ(conn->info.peer.port, 5432);
    EXPECT_EQ(registry.adds, 1);
    rdb_close(conn);
    EXPECT_EQ(registry.removes, 1);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(conn->life, nullptr);
}

TEST_F(RdbRegistryTest, CloseCascadesLiveStatements) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* a = rdb_prepare(conn, "SELECT 1");
    RdbStmt* b = rdb_prepare(conn, "SELECT 2");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    rdb_finalize(a);
    EXPECT_EQ(fake.finalizes, 1);
    rdb_close(conn);                        // b is finalized by the cascade
    EXPECT_EQ(fake.finalizes, 2);
    rdb_finalize(b);                        // a late finalize is a no-op
    EXPECT_EQ(fake.finalizes, 2);
    EXPECT_EQ(rdb_step(b), RDB_ERROR);
}

TEST_F(RdbRegistryTest, TeardownClosesLeakedConnection) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT 1");
    // context teardown runs the registry's close callback without the owner
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(fake.finalizes, 1);
    EXPECT_EQ(rdb_step(stmt), RDB_ERROR);
    EXPECT_EQ(rdb_ping(conn), RDB_CONN_LOST);
    // the owner's late close releases its record without touching the registry
    rdb_close(conn);
    EXPECT_EQ(registry.removes, 0);
    EXPECT_EQ(fake.closes, 1);
}

TEST_F(RdbRegistryTest, OpenWithoutRegistrationIsRejected) {
    fake.skip_register = true;
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    EXPECT_EQ(conn, nullptr);
    EXPECT_EQ(fake.opens, 1);
    EXPECT_EQ(fake.closes, 1);              // the host closed the unregistered handle
    EXPECT_EQ(registry.adds, 0);
}

TEST_F(RdbRegistryTest, UnauthorisedPeerIsRefused) {
    fake.wrong_peer = true;
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    EXPECT_EQ(conn, nullptr);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(registry.adds, 0);
}

TEST_F(RdbRegistryTest, AuthorisationHappensBeforeConnecting) {
    registry.refuse_authorize = true;
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    EXPECT_EQ(conn, nullptr);
    EXPECT_EQ(registry.refused_authorizations, 1);
    EXPECT_EQ(fake.opens, 0);
}

TEST_F(RdbRegistryTest, OptionalOpsReportUnsupported) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(rdb_ping(conn), RDB_UNSUPPORTED);
    EXPECT_EQ(rdb_cancel(conn), RDB_UNSUPPORTED);
    EXPECT_EQ(rdb_set_timeout(conn, 1000), RDB_UNSUPPORTED);
    rdb_close(conn);
}

/* ══════════════════════════════════════════════════════════════════════
 * §34 Driver tables and the resolver (RDB3/RDB4)
 * ══════════════════════════════════════════════════════════════════════ */

static RdbDriver resolved_driver = make_fake_driver("resolvedb");
static int resolver_calls = 0;

static const RdbDriver* test_resolver(const char* name) {
    resolver_calls++;
    return strcmp(name, "resolvedb") == 0 ? &resolved_driver : NULL;
}

TEST(RdbDriverTable, ResolverRegistersOnMiss) {
    rdb_set_driver_resolver(test_resolver);
    resolver_calls = 0;
    EXPECT_EQ(rdb_get_driver("resolvedb"), &resolved_driver);
    EXPECT_EQ(rdb_get_driver("resolvedb"), &resolved_driver);
    EXPECT_EQ(resolver_calls, 1);           // registered after the first miss
    EXPECT_EQ(rdb_get_driver("nosuchdb"), nullptr);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbDriverTable, IncompatibleTablesAreRejected) {
    RdbDriver old_abi = make_fake_driver("oldabi");
    old_abi.api_version = 1;
    EXPECT_FALSE(rdb_register_driver(&old_abi));
    RdbDriver truncated = make_fake_driver("truncated");
    truncated.struct_size = offsetof(RdbDriver, prepare);
    EXPECT_FALSE(rdb_register_driver(&truncated));
    RdbDriver missing_open = make_fake_driver("noopen");
    missing_open.open = NULL;
    EXPECT_FALSE(rdb_register_driver(&missing_open));
    EXPECT_EQ(rdb_get_driver("oldabi"), nullptr);
}

/* ══════════════════════════════════════════════════════════════════════
 * §35 Host TLS bridge without TLS (RDB11): protocol negotiation paths
 *     against fake servers on the loopback interface
 * ══════════════════════════════════════════════════════════════════════ */

#ifndef _WIN32     // the bridge's AF_UNIX listener is POSIX-only so far

#include "../lib/rdb_tunnel.h"
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

enum FakeServerMode {
    FAKE_PG_REFUSE_TLS,     // read SSLRequest, answer 'N', then echo
    FAKE_ECHO,              // echo from the first byte
    FAKE_MYSQL_NO_TLS,      // send a greeting without CLIENT_SSL, then echo
};

struct FakeServer {
    int listen_fd;
    int port;
    FakeServerMode mode;
    pthread_t thread;
    bool saw_ssl_request;
};

static const uint8_t fake_mysql_greeting[] = {
    // protocol 10, "8.0\0", conn id, 8 bytes auth data, filler, caps low (no CLIENT_SSL)
    10, '8', '.', '0', 0, 1, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 0, 0xff, 0xf7,
};

static void* fake_server_main(void* arg) {
    FakeServer* fs = (FakeServer*)arg;
    int fd = accept(fs->listen_fd, NULL, NULL);
    if (fd < 0) return NULL;
    uint8_t buf[256];
    if (fs->mode == FAKE_PG_REFUSE_TLS) {
        ssize_t n = recv(fd, buf, 8, MSG_WAITALL);
        fs->saw_ssl_request = n == 8 && buf[4] == 0x04 && buf[5] == 0xd2 && buf[6] == 0x16 && buf[7] == 0x2f;
        send(fd, "N", 1, 0);
    } else if (fs->mode == FAKE_MYSQL_NO_TLS) {
        uint8_t header[4] = { sizeof(fake_mysql_greeting), 0, 0, 0 };
        send(fd, header, 4, 0);
        send(fd, fake_mysql_greeting, sizeof(fake_mysql_greeting), 0);
    }
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) send(fd, buf, (size_t)n, 0);
    close(fd);
    return NULL;
}

static bool fake_server_start(FakeServer* fs, FakeServerMode mode) {
    memset(fs, 0, sizeof(*fs));
    fs->mode = mode;
    fs->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (bind(fs->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            listen(fs->listen_fd, 4) < 0 ||
            getsockname(fs->listen_fd, (struct sockaddr*)&addr, &len) < 0) {
        return false;
    }
    fs->port = ntohs(addr.sin_port);
    return pthread_create(&fs->thread, NULL, fake_server_main, fs) == 0;
}

static void fake_server_stop(FakeServer* fs) {
    shutdown(fs->listen_fd, SHUT_RDWR);
    close(fs->listen_fd);
    pthread_join(fs->thread, NULL);
}

static RdbTarget loopback_target(int port) {
    RdbTarget t;
    memset(&t, 0, sizeof(t));
    t.kind = RDB_PEER_TCP;
    t.port = port;
    snprintf(t.host, sizeof(t.host), "127.0.0.1");
    return t;
}

static RdbTunnelSpec tunnel_spec(RdbWireProtocol protocol, RdbTlsMode tls) {
    RdbTunnelSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.struct_size = sizeof(spec);
    spec.protocol = protocol;
    spec.tls = tls;
    return spec;
}

static int connect_local(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/** send "ping" and return how many bytes of the echo came back (0 = EOF) */
static ssize_t round_trip(int fd, char* reply, size_t cap) {
    send(fd, "ping", 4, 0);
    return recv(fd, reply, cap, MSG_WAITALL);
}

TEST(RdbTunnel, PostgresPreferFallsBackToPlaintextWhenRefused) {
    FakeServer fs;
    ASSERT_TRUE(fake_server_start(&fs, FAKE_PG_REFUSE_TLS));
    RdbTarget target = loopback_target(fs.port);
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_POSTGRES, RDB_TLS_PREFER);
    char endpoint[1024];
    RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
    ASSERT_NE(tunnel, nullptr);
    char sock[1100];
    snprintf(sock, sizeof(sock), "%s/.s.PGSQL.%d", endpoint, fs.port);
    int fd = connect_local(sock);
    ASSERT_GE(fd, 0);
    char reply[4];
    EXPECT_EQ(round_trip(fd, reply, sizeof(reply)), 4);
    EXPECT_EQ(memcmp(reply, "ping", 4), 0);
    EXPECT_TRUE(fs.saw_ssl_request);
    close(fd);
    rdb_tunnel_close(tunnel);
    fake_server_stop(&fs);
    struct stat st;
    EXPECT_NE(stat(endpoint, &st), 0);      // the private directory is gone
}

TEST(RdbTunnel, PostgresRequireFailsWhenRefused) {
    FakeServer fs;
    ASSERT_TRUE(fake_server_start(&fs, FAKE_PG_REFUSE_TLS));
    RdbTarget target = loopback_target(fs.port);
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_POSTGRES, RDB_TLS_REQUIRE);
    char endpoint[1024];
    RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
    ASSERT_NE(tunnel, nullptr);
    char sock[1100];
    snprintf(sock, sizeof(sock), "%s/.s.PGSQL.%d", endpoint, fs.port);
    int fd = connect_local(sock);
    ASSERT_GE(fd, 0);
    char reply[4];
    EXPECT_EQ(round_trip(fd, reply, sizeof(reply)), 0);   // the bridge hangs up
    close(fd);
    rdb_tunnel_close(tunnel);
    fake_server_stop(&fs);
}

TEST(RdbTunnel, DisableRelaysWithoutNegotiation) {
    FakeServer fs;
    ASSERT_TRUE(fake_server_start(&fs, FAKE_ECHO));
    RdbTarget target = loopback_target(fs.port);
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_POSTGRES, RDB_TLS_DISABLE);
    char endpoint[1024];
    RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
    ASSERT_NE(tunnel, nullptr);
    char sock[1100];
    snprintf(sock, sizeof(sock), "%s/.s.PGSQL.%d", endpoint, fs.port);
    int fd = connect_local(sock);
    ASSERT_GE(fd, 0);
    char reply[4];
    EXPECT_EQ(round_trip(fd, reply, sizeof(reply)), 4);
    EXPECT_EQ(memcmp(reply, "ping", 4), 0);
    close(fd);
    rdb_tunnel_close(tunnel);
    fake_server_stop(&fs);
}

TEST(RdbTunnel, MysqlPreferForwardsGreetingWithoutTls) {
    FakeServer fs;
    ASSERT_TRUE(fake_server_start(&fs, FAKE_MYSQL_NO_TLS));
    RdbTarget target = loopback_target(fs.port);
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_MYSQL, RDB_TLS_PREFER);
    char endpoint[1024];
    RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
    ASSERT_NE(tunnel, nullptr);
    int fd = connect_local(endpoint);
    ASSERT_GE(fd, 0);
    uint8_t packet[4 + sizeof(fake_mysql_greeting)];
    ASSERT_EQ(recv(fd, packet, sizeof(packet), MSG_WAITALL), (ssize_t)sizeof(packet));
    EXPECT_EQ(packet[0], sizeof(fake_mysql_greeting));
    EXPECT_EQ(memcmp(packet + 4, fake_mysql_greeting, sizeof(fake_mysql_greeting)), 0);
    char reply[4];
    EXPECT_EQ(round_trip(fd, reply, sizeof(reply)), 4);
    close(fd);
    rdb_tunnel_close(tunnel);
    fake_server_stop(&fs);
}

TEST(RdbTunnel, MysqlRequireFailsWithoutServerTls) {
    FakeServer fs;
    ASSERT_TRUE(fake_server_start(&fs, FAKE_MYSQL_NO_TLS));
    RdbTarget target = loopback_target(fs.port);
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_MYSQL, RDB_TLS_REQUIRE);
    char endpoint[1024];
    RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
    ASSERT_NE(tunnel, nullptr);
    int fd = connect_local(endpoint);
    ASSERT_GE(fd, 0);
    uint8_t packet[64];
    EXPECT_EQ(recv(fd, packet, sizeof(packet), 0), 0);      // nothing reaches the client
    close(fd);
    rdb_tunnel_close(tunnel);
    fake_server_stop(&fs);
}

TEST(RdbTunnel, RejectsFileTargets) {
    RdbTarget target;
    memset(&target, 0, sizeof(target));
    target.kind = RDB_PEER_FILE;
    RdbTunnelSpec spec = tunnel_spec(RDB_WIRE_POSTGRES, RDB_TLS_DISABLE);
    char endpoint[1024];
    EXPECT_EQ(rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint)), nullptr);
}

#endif  // _WIN32
