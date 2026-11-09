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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uv.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

extern "C" {
#include "lib/rdb.h"
#include "lib/rdb_spool.h"
#include "lib/file.h"
#include "lib/rdb_sqlite_tx.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include "lib/str.h"
#include "lib/sqlite/sqlite3.h"
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

TEST_F(RdbTest, DetectDriver_MariadbScheme) {
    EXPECT_STREQ(rdb_detect_driver("mariadb://localhost/mydb"), "mysql");
}

TEST_F(RdbTest, DetectDriver_DuckdbNotSupported) {
    // DuckDB is out of scope (RDB13): neither its scheme nor its files are claimed
    EXPECT_EQ(rdb_detect_driver("duckdb://data.ddb"), nullptr);
    EXPECT_EQ(rdb_detect_driver("warehouse.ddb"), nullptr);
    EXPECT_EQ(rdb_detect_driver("warehouse.duckdb"), nullptr);
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

TEST_F(RdbTest, ReadonlySqliteRejectsExternalFilesAndStateChanges) {
    const char* attached = "temp/rdb_readonly_attach.db";
    const char* vacuumed = "temp/rdb_readonly_vacuum.db";
    unlink(attached);
    unlink(vacuumed);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    const char* forbidden[] = {
        "ATTACH DATABASE 'temp/rdb_readonly_attach.db' AS other",
        "VACUUM INTO 'temp/rdb_readonly_vacuum.db'",
        "PRAGMA journal_mode = WAL", "PRAGMA foreign_keys = OFF",
        "CREATE TEMP TABLE hidden(x)", "INSERT INTO authors VALUES (9, 'blocked', NULL, 0, 1)",
        "SELECT load_extension('missing')"
    };
    for (const char* sql : forbidden) EXPECT_EQ(rdb_prepare(conn, sql), nullptr) << sql;
    EXPECT_NE(access(attached, F_OK), 0);
    EXPECT_NE(access(vacuumed, F_OK), 0);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT name FROM authors ORDER BY id");
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(rdb_step(stmt), RDB_ROW);
    rdb_finalize(stmt);
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

TEST_F(RdbTypesTest, ReadBlobDistinguishesBytesEmptyAndNull) {
    RdbStmt* stmt = rdb_prepare(conn, "SELECT col_blob, X'', NULL FROM types_test");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    RdbValue val = rdb_column_value(stmt, 0);
    EXPECT_EQ(val.type, RDB_TYPE_BLOB);
    EXPECT_FALSE(val.is_null);
    ASSERT_EQ(val.str_len, 4);
    const unsigned char expected[] = {0xde, 0xad, 0xbe, 0xef};
    ASSERT_NE(val.str_val, nullptr);
    EXPECT_EQ(memcmp(val.str_val, expected, sizeof(expected)), 0);
    RdbValue empty = rdb_column_value(stmt, 1);
    EXPECT_EQ(empty.type, RDB_TYPE_BLOB);
    EXPECT_FALSE(empty.is_null);
    EXPECT_EQ(empty.str_len, 0);
    RdbValue missing = rdb_column_value(stmt, 2);
    EXPECT_EQ(missing.type, RDB_TYPE_NULL);
    EXPECT_TRUE(missing.is_null);
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
    uv_sem_t* open_entered;
    uv_sem_t* open_resume;
    uv_sem_t* schema_entered;
    uv_sem_t* schema_resume;
    uv_sem_t* prepare_entered;
    uv_sem_t* prepare_resume;
    uv_sem_t* fetch_entered;
    uv_sem_t* fetch_resume;
    uv_sem_t* finalize_entered;
    uv_sem_t* finalize_resume;
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
    if (fake.open_entered) {
        uv_sem_post(fake.open_entered);
        uv_sem_wait(fake.open_resume);
    }
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
static int fake_load_schema(void*, const RdbHostAPI*, void*, RdbSchema*) {
    if (fake.schema_entered) {
        uv_sem_post(fake.schema_entered);
        uv_sem_wait(fake.schema_resume);
    }
    return RDB_OK;
}
static int fake_prepare(void*, const char*, void** out) {
    if (fake.prepare_entered) { uv_sem_post(fake.prepare_entered); uv_sem_wait(fake.prepare_resume); }
    *out = &fake_stmt_token;
    return RDB_OK;
}
static int fake_bind(void*, int, const RdbParam*) { return RDB_OK; }
static int fake_step(void*) {
    if (fake.fetch_entered) { uv_sem_post(fake.fetch_entered); uv_sem_wait(fake.fetch_resume); }
    return RDB_DONE;
}
static int fake_column_count(void*) { return 0; }
static int fake_column_desc(void*, int, RdbColumnDesc*) { return RDB_ERROR; }
static RdbValue fake_value(void*, int) { RdbValue v; memset(&v, 0, sizeof(v)); v.is_null = true; return v; }
static void fake_finalize(void*) {
    if (fake.finalize_entered) { uv_sem_post(fake.finalize_entered); uv_sem_wait(fake.finalize_resume); }
    fake.finalizes++;
}
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
    d.column_desc = fake_column_desc;
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
    RdbTarget last_target;
};
static FakeRegistry registry;

static int reg_authorize(const char*, const RdbTarget* targets, int count) {
    if (count == 1) registry.last_target = targets[0];
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

struct OpenWorkRace { RdbOpenWork* work; int status; const int* cancelled; };
static void open_work_thread(void* user) {
    OpenWorkRace* race = (OpenWorkRace*)user;
    race->status = rdb_open_work_run(race->work, race->cancelled);
}

TEST_F(RdbRegistryTest, OpenWorkerRegistersBeforeWorkAndClosesOnTeardown) {
    RdbOpenWork* work = rdb_open_work_begin(pool, "fakedb://bob:pw@db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(registry.adds, 1);
    EXPECT_EQ(fake.opens, 0);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.open_entered = &entered;
    fake.open_resume = &resume;
    OpenWorkRace race = {work, RDB_ERROR, NULL};
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, open_work_thread, &race), 0);
    uv_sem_wait(&entered);
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 0);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(race.status, RDB_OK);
    EXPECT_EQ(rdb_open_work_end(work, true), nullptr);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(registry.removes, 0);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
}

TEST_F(RdbRegistryTest, OpenWorkerRejectsCancellationAndUnauthorisedPeer) {
    RdbOpenWork* work = rdb_open_work_begin(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    int cancelled = 1;
    EXPECT_EQ(rdb_open_work_run(work, &cancelled), RDB_ERROR);
    EXPECT_EQ(rdb_open_work_end(work, true), nullptr);
    EXPECT_EQ(fake.opens, 0);
    EXPECT_EQ(fake.closes, 0);
    EXPECT_EQ(registry.removes, 1);
    fake.wrong_peer = true;
    work = rdb_open_work_begin(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(rdb_open_work_run(work, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_open_work_end(work, true), nullptr);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(registry.removes, 2);
}

TEST_F(RdbRegistryTest, OpenWorkerClosesConnectionWhenCancelledDuringOpen) {
    RdbOpenWork* work = rdb_open_work_begin(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.open_entered = &entered;
    fake.open_resume = &resume;
    int cancelled = 0;
    OpenWorkRace race = {work, RDB_OK, &cancelled};
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, open_work_thread, &race), 0);
    uv_sem_wait(&entered);
    __atomic_store_n(&cancelled, 1, __ATOMIC_RELEASE);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(race.status, RDB_ERROR);
    EXPECT_EQ(rdb_open_work_end(work, true), nullptr);
    EXPECT_EQ(fake.closes, 1);
    EXPECT_EQ(registry.removes, 1);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
}

TEST_F(RdbTest, OpenWorkerOpensSqliteReadOnly) {
    OpenWorkRace race = {rdb_open_work_begin(pool, TEST_DB_PATH, "sqlite", true), RDB_ERROR, NULL};
    ASSERT_NE(race.work, nullptr);
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, open_work_thread, &race), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(race.status, RDB_OK);
    RdbConn* conn = rdb_open_work_end(race.work, true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT COUNT(*) FROM authors");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 3);
    rdb_finalize(stmt);
    rdb_close(conn);
}

struct PrepareWorkRace { RdbPrepareWork* work; int status; };
static void prepare_work_thread(void* user) {
    PrepareWorkRace* race = (PrepareWorkRace*)user;
    race->status = rdb_prepare_work_run(race->work, NULL);
}
struct FetchRows { int count; int64_t ids[8]; };
static bool capture_fetch_row(void* user, int count, const RdbValue* values) {
    FetchRows* rows = (FetchRows*)user;
    if (count < 1 || rows->count >= 8 || values[0].type != RDB_TYPE_INT) return false;
    rows->ids[rows->count++] = values[0].int_val;
    return true;
}
struct FetchWorkRace { RdbFetchWork* work; int status; };
static void fetch_work_thread(void* user) {
    FetchWorkRace* race = (FetchWorkRace*)user;
    race->status = rdb_fetch_work_run(race->work, NULL);
}

TEST_F(RdbTest, StatementWorkersReturnBoundedRowsAndDuplicateMetadata) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbParam param = {};
    param.type = RDB_TYPE_INT;
    param.int_val = 2;
    PrepareWorkRace prepare = {rdb_prepare_work_begin(conn,
        "SELECT id AS n, name AS n FROM authors WHERE id >= ?1 ORDER BY id", &param, 1), RDB_ERROR};
    ASSERT_NE(prepare.work, nullptr);
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, prepare_work_thread, &prepare), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(prepare.status, RDB_OK);
    RdbStmt* stmt = rdb_prepare_work_end(prepare.work, true);
    ASSERT_NE(stmt, nullptr);
    Pool* metadata = pool_create();
    ASSERT_NE(metadata, nullptr);
    FetchRows rows = {};
    FetchWorkRace fetch = {rdb_fetch_work_begin(stmt, 1, 10, metadata, capture_fetch_row, &rows), RDB_ERROR};
    ASSERT_NE(fetch.work, nullptr);
    ASSERT_EQ(uv_thread_create(&thread, fetch_work_thread, &fetch), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(fetch.status, RDB_OK);
    RdbFetchResult result = {};
    ASSERT_EQ(rdb_fetch_work_end(fetch.work, &result), RDB_OK);
    ASSERT_EQ(result.column_count, 2);
    EXPECT_STREQ(result.columns[0].name, "n");
    EXPECT_STREQ(result.columns[1].name, "n");
    EXPECT_EQ(result.row_count, 1);
    EXPECT_FALSE(result.done);
    fetch = {rdb_fetch_work_begin(stmt, 2, 10, metadata, capture_fetch_row, &rows), RDB_ERROR};
    ASSERT_NE(fetch.work, nullptr);
    ASSERT_EQ(uv_thread_create(&thread, fetch_work_thread, &fetch), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(fetch.status, RDB_OK);
    ASSERT_EQ(rdb_fetch_work_end(fetch.work, &result), RDB_OK);
    EXPECT_EQ(result.row_count, 1);
    EXPECT_TRUE(result.done);
    EXPECT_EQ(rows.count, 2);
    EXPECT_EQ(rows.ids[0], 2);
    EXPECT_EQ(rows.ids[1], 3);
    rdb_finalize(stmt);
    rdb_close(conn);
    EXPECT_STREQ(result.columns[0].name, "n");
    pool_destroy(metadata);
}

TEST_F(RdbTest, StatementWorkerRejectsMissingOrExtraParameters) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbParam param = {};
    param.type = RDB_TYPE_INT;
    param.int_val = 1;
    RdbPrepareWork* work = rdb_prepare_work_begin(conn, "SELECT ?1, ?2", &param, 1);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(rdb_prepare_work_run(work, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_prepare_work_end(work, true), nullptr);
    work = rdb_prepare_work_begin(conn, "SELECT 1", &param, 1);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(rdb_prepare_work_run(work, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_prepare_work_end(work, true), nullptr);
    rdb_close(conn);
}

TEST_F(RdbTest, SqlitePrepareRejectsSecondStatementBeforePreparingIt) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", false);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(rdb_prepare(conn, "SELECT 1; PRAGMA user_version=42"), nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "PRAGMA user_version");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 0);
    rdb_finalize(stmt);
    stmt = rdb_prepare(conn, "SELECT ';' AS text; /* ; */ -- ;\n ;");
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(rdb_step(stmt), RDB_ROW);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbRegistryTest, PrepareWorkerFinalizesIfRegistryClosesDuringWork) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    PrepareWorkRace race = {rdb_prepare_work_begin(conn, "SELECT 1", NULL, 0), RDB_ERROR};
    ASSERT_NE(race.work, nullptr);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.prepare_entered = &entered;
    fake.prepare_resume = &resume;
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, prepare_work_thread, &race), 0);
    uv_sem_wait(&entered);
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 0);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(race.status, RDB_OK);
    EXPECT_EQ(rdb_prepare_work_end(race.work, true), nullptr);
    EXPECT_EQ(fake.finalizes, 1);
    EXPECT_EQ(fake.closes, 1);
    rdb_close(conn);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
}

TEST_F(RdbRegistryTest, FetchWorkerPinsStatementUntilRegistryTeardownFinishes) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT 1");
    ASSERT_NE(stmt, nullptr);
    Pool* metadata = pool_create();
    ASSERT_NE(metadata, nullptr);
    FetchRows rows = {};
    FetchWorkRace race = {rdb_fetch_work_begin(stmt, 1, 10, metadata, capture_fetch_row, &rows), RDB_ERROR};
    ASSERT_NE(race.work, nullptr);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.fetch_entered = &entered;
    fake.fetch_resume = &resume;
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, fetch_work_thread, &race), 0);
    uv_sem_wait(&entered);
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 0);
    EXPECT_EQ(fake.finalizes, 0);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(race.status, RDB_OK);
    RdbFetchResult result = {};
    EXPECT_EQ(rdb_fetch_work_end(race.work, &result), RDB_ERROR);
    EXPECT_EQ(fake.finalizes, 1);
    EXPECT_EQ(fake.closes, 1);
    rdb_close(conn);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
    pool_destroy(metadata);
}

static void finalize_work_thread(void* data) { rdb_finalize_work_run((RdbFinalizeWork*)data); }
TEST_F(RdbRegistryTest, FinalizeWorkerKeepsConnectionAliveAcrossTeardown) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT 1");
    ASSERT_NE(stmt, nullptr);
    RdbFinalizeWork* work = rdb_finalize_work_begin(stmt);
    ASSERT_NE(work, nullptr);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.finalize_entered = &entered;
    fake.finalize_resume = &resume;
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, finalize_work_thread, work), 0);
    uv_sem_wait(&entered);
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 0);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(fake.finalizes, 1);
    rdb_finalize_work_end(work);
    EXPECT_EQ(fake.closes, 1);
    rdb_finalize(stmt);
    EXPECT_EQ(fake.finalizes, 1);
    rdb_close(conn);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
}

TEST_F(RdbRegistryTest, OpenWorkerRefusesUnregisteredHandleAndRejectedResult) {
    fake.skip_register = true;
    RdbOpenWork* work = rdb_open_work_begin(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(rdb_open_work_run(work, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_open_work_end(work, true), nullptr);
    EXPECT_EQ(fake.closes, 1);
    fake.skip_register = false;
    work = rdb_open_work_begin(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(work, nullptr);
    EXPECT_EQ(rdb_open_work_run(work, NULL), RDB_OK);
    EXPECT_EQ(rdb_open_work_end(work, false), nullptr);
    EXPECT_EQ(fake.closes, 2);
    EXPECT_EQ(registry.removes, 2);
}

TEST_F(RdbRegistryTest, SqliteRejectsAmbiguousOrTruncatedTargetsBeforeAuthorization) {
    rdb_sqlite_register();
    registry.refuse_authorize = true;
    char path[sizeof(((RdbTarget*)nullptr)->path) + 1];
    memset(path, 'x', sizeof(path));
    path[sizeof(path) - 2] = '\0';
    // A representable path reaches the hook unchanged; an oversized path must
    // never be authorized as its truncated prefix.
    EXPECT_EQ(rdb_open(pool, path, "sqlite", false), nullptr);
    EXPECT_EQ(registry.refused_authorizations, 1);
    EXPECT_STREQ(registry.last_target.path, path);
    path[sizeof(path) - 2] = 'x';
    path[sizeof(path) - 1] = '\0';
    EXPECT_EQ(rdb_open(pool, path, "sqlite", false), nullptr);
    EXPECT_EQ(rdb_sqlite_tx_begin(pool, path), nullptr);
    EXPECT_EQ(rdb_open(pool, "file::memory:?mode=memory", "sqlite", false), nullptr);
    EXPECT_EQ(rdb_sqlite_tx_begin(pool, "file::memory:?mode=memory"), nullptr);
    EXPECT_EQ(registry.refused_authorizations, 1);
    EXPECT_EQ(registry.adds, 0);
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

class RdbSpoolTest : public ::testing::Test {
protected:
    char directory[128] = "temp/rdb-spool-XXXXXX";
    FileAnchor* anchor = nullptr;
    void SetUp() override {
        ensure_temp_dir();
        ASSERT_NE(mkdtemp(directory), nullptr);
        FileAnchorOptions options = {FILE_ANCHOR_PRIVATE};
        anchor = file_anchor_open(directory, &options);
        ASSERT_NE(anchor, nullptr);
    }
    void TearDown() override {
        file_anchor_close(anchor);
        EXPECT_EQ(rmdir(directory), 0);
    }
};

struct SpoolCheck { int seen; };
static bool check_spooled_row(void* context, int count, const RdbValue* values) {
    SpoolCheck* check = (SpoolCheck*)context;
    EXPECT_EQ(count, 6);
    if (count != 6) return false;
    if (check->seen == 0) {
        const char text[] = {'a', '\0', 'b'};
        const unsigned char binary[] = {0, 255};
        EXPECT_TRUE(values[0].is_null);
        EXPECT_EQ(values[1].type, RDB_TYPE_INT); EXPECT_EQ(values[1].int_val, INT64_MIN);
        EXPECT_EQ(values[2].type, RDB_TYPE_FLOAT); EXPECT_EQ(values[2].float_val, -12.5);
        EXPECT_EQ(values[3].type, RDB_TYPE_STRING); EXPECT_EQ(values[3].str_len, 3);
        if (values[3].str_len != 3) return false;
        EXPECT_EQ(memcmp(values[3].str_val, text, sizeof(text)), 0);
        EXPECT_EQ(values[4].type, RDB_TYPE_BLOB); EXPECT_EQ(values[4].str_len, 2);
        if (values[4].str_len != 2) return false;
        EXPECT_EQ(memcmp(values[4].str_val, binary, sizeof(binary)), 0);
        EXPECT_EQ(values[5].type, RDB_TYPE_BOOL); EXPECT_TRUE(values[5].bool_val);
    } else {
        EXPECT_EQ(values[3].type, RDB_TYPE_STRING); EXPECT_EQ(values[3].str_len, 0);
        EXPECT_EQ(values[4].type, RDB_TYPE_BLOB); EXPECT_EQ(values[4].str_len, 0);
        EXPECT_FALSE(values[5].bool_val);
    }
    check->seen++;
    return true;
}

static bool count_spool_entry(void* context, const char*, const FileRevision*) {
    (*(int*)context)++;
    return true;
}

TEST_F(RdbSpoolTest, TypedRowsRemainRandomlyReadableAndAnonymous) {
    RdbSpool* spool = rdb_spool_open(anchor, 4096, 8, 6);
    ASSERT_NE(spool, nullptr);
    int entries = 0;
    ASSERT_TRUE(file_anchor_visit_strict(anchor, 16, count_spool_entry, &entries));
    EXPECT_EQ(entries, 0);
    const char text[] = {'a', '\0', 'b'};
    const unsigned char binary[] = {0, 255};
    RdbValue first[6] = {};
    first[0].type = RDB_TYPE_NULL; first[0].is_null = true;
    first[1].type = RDB_TYPE_INT; first[1].int_val = INT64_MIN;
    first[2].type = RDB_TYPE_FLOAT; first[2].float_val = -12.5;
    first[3].type = RDB_TYPE_STRING; first[3].str_val = text; first[3].str_len = sizeof(text);
    first[4].type = RDB_TYPE_BLOB; first[4].str_val = (const char*)binary; first[4].str_len = sizeof(binary);
    first[5].type = RDB_TYPE_BOOL; first[5].bool_val = true;
    RdbValue second[6]; memcpy(second, first, sizeof(first));
    second[3].str_val = nullptr; second[3].str_len = 0;
    second[4].str_val = nullptr; second[4].str_len = 0;
    second[5].bool_val = false;
    RdbSpoolRow rows[] = {{6, first}, {6, second}};
    ASSERT_EQ(rdb_spool_append(spool, rows, 2), RDB_SPOOL_OK);
    EXPECT_EQ(rdb_spool_count(spool), 2U);
    EXPECT_GT(rdb_spool_bytes(spool), 0U);
    SpoolCheck check = {};
    EXPECT_EQ(rdb_spool_read(spool, 0, 2, check_spooled_row, &check), RDB_SPOOL_OK);
    EXPECT_EQ(check.seen, 2);
    check.seen = 1;
    EXPECT_EQ(rdb_spool_read(spool, 1, 1, check_spooled_row, &check), RDB_SPOOL_OK);
    EXPECT_EQ(rdb_spool_read(spool, 2, 1, check_spooled_row, &check), RDB_SPOOL_INVALID);
    rdb_spool_close(spool);
}

TEST_F(RdbSpoolTest, QuotaAndInvalidBatchesLeaveThePrefixReadable) {
    RdbSpool* spool = rdb_spool_open(anchor, 30, 4, 1);
    ASSERT_NE(spool, nullptr);
    RdbValue value = {}; value.type = RDB_TYPE_INT; value.int_val = 7;
    RdbSpoolRow row = {1, &value};
    ASSERT_EQ(rdb_spool_append(spool, &row, 1), RDB_SPOOL_OK);
    uint64_t bytes = rdb_spool_bytes(spool);
    EXPECT_EQ(rdb_spool_append(spool, &row, 1), RDB_SPOOL_LIMIT);
    EXPECT_EQ(rdb_spool_count(spool), 1U); EXPECT_EQ(rdb_spool_bytes(spool), bytes);
    RdbValue invalid = {}; invalid.type = RDB_TYPE_STRING; invalid.str_len = -1;
    RdbSpoolRow bad = {1, &invalid};
    EXPECT_EQ(rdb_spool_append(spool, &bad, 1), RDB_SPOOL_INVALID);
    auto read = [](void* context, int count, const RdbValue* values) -> bool {
        if (count != 1 || values[0].type != RDB_TYPE_INT || values[0].int_val != 7) return false;
        (*(int*)context)++;
        return true;
    };
    int seen = 0;
    EXPECT_EQ(rdb_spool_read(spool, 0, 1, read, &seen), RDB_SPOOL_OK);
    EXPECT_EQ(seen, 1);
    rdb_spool_close(spool);
}

#include "lib/rdb_tunnel.h"
#include <pthread.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

enum FakeServerMode {
    FAKE_PG_REFUSE_TLS,     // read SSLRequest, answer 'N', then echo
    FAKE_ECHO,              // echo from the first byte
    FAKE_MYSQL_NO_TLS,      // send a greeting without CLIENT_SSL, then echo
    FAKE_PG_STALL,          // hold the SSLRequest without answering
    FAKE_PG_STALL_TLS,      // accept SSLRequest but never finish TLS
    FAKE_MYSQL_STALL,       // hold the connection without a greeting
    FAKE_MYSQL_TRICKLE,     // send a greeting header slower than the total deadline
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
    } else if (fs->mode == FAKE_PG_STALL || fs->mode == FAKE_PG_STALL_TLS) {
        if (recv(fd, buf, 8, MSG_WAITALL) == 8 && fs->mode == FAKE_PG_STALL_TLS) {
            send(fd, "S", 1, 0);
        }
        while (recv(fd, buf, sizeof(buf), 0) > 0) {}
        close(fd);
        return NULL;
    } else if (fs->mode == FAKE_MYSQL_TRICKLE) {
#ifdef SO_NOSIGPIPE
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        const uint8_t header[4] = { 40, 0, 0, 0 };
        for (size_t i = 0; i < 44; i++) {
            uint8_t byte = i < sizeof(header) ? header[i] : 0;
            if (send(fd, &byte, 1, MSG_NOSIGNAL) != 1) break;
            usleep(70000);
        }
        while (recv(fd, buf, sizeof(buf), 0) > 0) {}
        close(fd);
        return NULL;
    } else if (fs->mode == FAKE_MYSQL_STALL) {
        while (recv(fd, buf, sizeof(buf), 0) > 0) {}
        close(fd);
        return NULL;
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

TEST(RdbTunnel, StalledProtocolAndTlsUpgradesTimeOut) {
    const FakeServerMode modes[] = { FAKE_PG_STALL, FAKE_PG_STALL_TLS, FAKE_MYSQL_STALL, FAKE_MYSQL_TRICKLE };
    for (FakeServerMode mode : modes) {
        FakeServer fs;
        ASSERT_TRUE(fake_server_start(&fs, mode));
        RdbTarget target = loopback_target(fs.port);
        RdbTunnelSpec spec = tunnel_spec(mode >= FAKE_MYSQL_STALL ? RDB_WIRE_MYSQL : RDB_WIRE_POSTGRES,
                                         RDB_TLS_REQUIRE);
        spec.connect_timeout_ms = 150;
        char endpoint[1024];
        RdbTunnel* tunnel = rdb_tunnel_open(&target, &spec, endpoint, sizeof(endpoint));
        ASSERT_NE(tunnel, nullptr);
        char sock[1100];
        if (spec.protocol == RDB_WIRE_POSTGRES) snprintf(sock, sizeof(sock), "%s/.s.PGSQL.%d", endpoint, fs.port);
        else snprintf(sock, sizeof(sock), "%s", endpoint);
        int fd = connect_local(sock);
        ASSERT_GE(fd, 0);
        struct pollfd waiting = { fd, POLLIN, 0 };
        EXPECT_EQ(poll(&waiting, 1, 2000), 1) << "mode " << mode;
        char byte;
        EXPECT_EQ(recv(fd, &byte, 1, 0), 0) << "mode " << mode;
        close(fd);
        rdb_tunnel_close(tunnel);
        fake_server_stop(&fs);
    }
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

/* ══════════════════════════════════════════════════════════════════════
 * §36 OS trust store (Q5): the platform roots load once and include a
 *     well-known public root
 * ══════════════════════════════════════════════════════════════════════ */

#include "lib/trust_store.h"
#include <mbedtls/x509_crt.h>

TEST(TrustStore, LoadsPlatformRoots) {
    if (getenv("SSL_CERT_FILE")) GTEST_SKIP() << "SSL_CERT_FILE replaces the platform store";
    struct mbedtls_x509_crt* roots = trust_store_roots();
    ASSERT_NE(roots, nullptr);
    EXPECT_GT(trust_store_root_count(), 50);
    // ISRG Root X1 (Let's Encrypt) ships in every current platform store
    bool found = false;
    char subject[512];
    for (const mbedtls_x509_crt* c = roots; c && c->raw.len && !found; c = c->next) {
        if (mbedtls_x509_dn_gets(subject, sizeof(subject), &c->subject) > 0) {
            found = strstr(subject, "ISRG Root X1") != NULL;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(trust_store_roots(), roots);      // loaded once, shared
}

/* ══════════════════════════════════════════════════════════════════════
 * §37 Live cancel (§13.5.4): cancel from another thread interrupts an
 *     in-flight query and leaves the session usable. Needs the built
 *     rdb-drivers module and LAMBDA_TEST_PG_URI / LAMBDA_TEST_MYSQL_URI
 *     (make test-rdb-drivers-local); skipped otherwise.
 * ══════════════════════════════════════════════════════════════════════ */

#ifndef _WIN32

#include "../lambda/jube/jube.h"
#include <dlfcn.h>
#include <time.h>

static const JubeModuleDef* live_module = NULL;

/** the module's drivers, loaded straight from the dylib (no Jube registry) */
static const RdbDriver* live_resolver(const char* name) {
    if (!live_module) {
        void* image = dlopen("modules/rdb-drivers/rdb-drivers.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!image) image = dlopen("modules/rdb-drivers/rdb-drivers.so", RTLD_NOW | RTLD_LOCAL);
        typedef const JubeModuleDef* (*EntryFn)(void);
        EntryFn entry = image ? (EntryFn)dlsym(image, "jube_module") : NULL;
        const JubeModuleDef* def = entry ? entry() : NULL;
        if (!def || def->init(NULL) != 0) return NULL;   // the module ignores the Jube host
        live_module = def;
    }
    for (size_t i = 0; i < live_module->rdb_driver_count; i++) {
        if (strcmp(live_module->rdb_drivers[i]->name, name) == 0) return live_module->rdb_drivers[i];
    }
    return NULL;
}

TEST(RdbModuleUriTest, MysqlResolvesBracketedIpv6AndRejectsInvalidPorts) {
    const RdbDriver* driver = live_resolver("mysql");
    if (!driver) GTEST_SKIP() << "rdb-drivers module not built";
    RdbTarget target = {};
    int count = 0;
    EXPECT_EQ(driver->resolve_targets("mysql://u:p@[2001:db8::1]:3307/db", &target, 1, &count), RDB_OK);
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(target.host, "2001:db8::1");
    EXPECT_EQ(target.port, 3307);
    EXPECT_EQ(driver->resolve_targets("mysql://u:p@[fe80::1%25en0]/db", &target, 1, &count), RDB_OK);
    EXPECT_STREQ(target.host, "fe80::1%en0");
    EXPECT_EQ(target.port, 3306);
    const char* invalid[] = {"mysql://[::1/db", "mysql://[::1]extra/db", "mysql://[::1]:/db",
        "mysql://[::1]:65536/db", "mysql://host:12x/db", "mysql://::1/db",
        "mysql://host%00.attacker/db", "mysql://host%2/db", "mysql://user:p%00x@host/db"};
    for (const char* uri : invalid) {
        SCOPED_TRACE(uri);
        EXPECT_EQ(driver->resolve_targets(uri, &target, 1, &count), RDB_ERROR);
        EXPECT_EQ(count, 0);
    }
    char oversized[300];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    char uri[400];
    str_fmt(uri, sizeof(uri), "mysql://%s/db", oversized);
    EXPECT_EQ(driver->resolve_targets(uri, &target, 1, &count), RDB_ERROR);
    str_fmt(uri, sizeof(uri), "mysql://%s@host/db", oversized);
    EXPECT_EQ(driver->resolve_targets(uri, &target, 1, &count), RDB_ERROR);
    str_fmt(uri, sizeof(uri), "mysql://host/%s", oversized);
    EXPECT_EQ(driver->resolve_targets(uri, &target, 1, &count), RDB_ERROR);
    str_fmt(uri, sizeof(uri), "mysql://host/db?%s=1", oversized);
    EXPECT_EQ(driver->resolve_targets(uri, &target, 1, &count), RDB_ERROR);
    EXPECT_EQ(driver->resolve_targets("mysql://host/db", nullptr, 0, &count), RDB_ERROR);
    EXPECT_EQ(count, 0);
}

static double live_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct LiveCancel {
    RdbConn* conn;
    int rc;
};

static void* live_cancel_main(void* arg) {
    LiveCancel* lc = (LiveCancel*)arg;
    usleep(500 * 1000);     // let the query reach the server first
    lc->rc = rdb_cancel(lc->conn);
    return NULL;
}

/** run a 30 s sleep, cancel it after 0.5 s, then check the session still answers */
static void live_cancel_check(const char* env, const char* driver, const char* sleep_sql) {
    const char* uri = getenv(env);
    if (!uri) GTEST_SKIP() << env << " not set (make test-rdb-drivers-local)";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver(driver)) GTEST_SKIP() << "rdb-drivers module not built (make build-rdb-drivers)";
    Pool* pool = pool_create();
    RdbConn* conn = rdb_open(pool, uri, driver, true);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->driver->caps & RDB_CAP_CANCEL);

    RdbStmt* stmt = rdb_prepare(conn, sleep_sql);
    ASSERT_NE(stmt, nullptr);
    LiveCancel lc = { conn, RDB_ERROR };
    pthread_t canceller;
    ASSERT_EQ(pthread_create(&canceller, NULL, live_cancel_main, &lc), 0);
    double start = live_now();
    rdb_step(stmt);         // PostgreSQL fails with 57014; MySQL's SLEEP returns 1
    double elapsed = live_now() - start;
    pthread_join(canceller, NULL);
    rdb_finalize(stmt);
    EXPECT_EQ(lc.rc, RDB_OK);
    EXPECT_LT(elapsed, 10.0) << "the query ran to completion instead of being cancelled";

    RdbStmt* again = rdb_prepare(conn, "SELECT 1");
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(rdb_step(again), RDB_ROW);
    rdb_finalize(again);
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveCancel, PostgresqlInterruptsSleep) {
    live_cancel_check("LAMBDA_TEST_PG_URI", "postgresql", "SELECT pg_sleep(30)");
}

TEST(RdbLiveCancel, MysqlInterruptsSleep) {
    live_cancel_check("LAMBDA_TEST_MYSQL_URI", "mysql", "SELECT SLEEP(30)");
}

TEST(RdbLiveStreaming, PostgresqlRowsMetadataAndEarlyClose) {
    const char* uri = getenv("LAMBDA_TEST_PG_URI");
    if (!uri) GTEST_SKIP() << "LAMBDA_TEST_PG_URI not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver("postgresql")) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    RdbConn* conn = rdb_open(pool, uri, "postgresql", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->driver->caps & RDB_CAP_STREAMING);

    RdbStmt* stmt = rdb_prepare(conn,
        "SELECT n::bigint AS number, decode('0001ff','hex') AS bytes, NULL::text AS empty "
        "FROM generate_series(1, 5000) AS n");
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(rdb_column_count(stmt), 3);
    RdbColumnDesc desc;
    ASSERT_EQ(rdb_column_desc(stmt, 0, &desc), RDB_OK);
    const char* first_name = desc.name;
    EXPECT_STREQ(first_name, "number");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 1);
    RdbValue bytes = rdb_column_value(stmt, 1);
    ASSERT_EQ(bytes.type, RDB_TYPE_BLOB);
    ASSERT_FALSE(bytes.is_null);
    ASSERT_EQ(bytes.str_len, 3);
    EXPECT_EQ(memcmp(bytes.str_val, "\x00\x01\xff", 3), 0);
    EXPECT_TRUE(rdb_column_value(stmt, 2).is_null);

    // Another statement cannot consume the wire while this result is active.
    RdbStmt* competing = rdb_prepare(conn, "SELECT 2");
    ASSERT_NE(competing, nullptr);
    EXPECT_EQ(rdb_step(competing), RDB_ERROR);
    rdb_finalize(competing);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 2);
    EXPECT_STREQ(first_name, "number");
    rdb_finalize(stmt);

    // Cancelling an unfinished stream drains it so the connection can be reused.
    stmt = rdb_prepare(conn, "SELECT 1 AS answer WHERE false");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_column_count(stmt), 1);
    ASSERT_EQ(rdb_column_desc(stmt, 0, &desc), RDB_OK);
    EXPECT_STREQ(desc.name, "answer");
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    stmt = rdb_prepare(conn, "SELECT 3");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 3);
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveStreaming, MysqlRowsMetadataAndEarlyClose) {
    const char* uri = getenv("LAMBDA_TEST_MYSQL_URI");
    if (!uri) GTEST_SKIP() << "LAMBDA_TEST_MYSQL_URI not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver("mysql")) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    RdbConn* conn = rdb_open(pool, uri, "mysql", true);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->driver->caps & RDB_CAP_STREAMING);

    RdbStmt* stmt = rdb_prepare(conn,
        "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<100) "
        "SELECT n AS number, IF(n=2,REPEAT('y',4000),REPEAT('x',2048)) AS value, "
        "UNHEX('0001ff') AS bytes, NULL AS null_value FROM seq");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_column_count(stmt), 4);
    RdbColumnDesc desc;
    ASSERT_EQ(rdb_column_desc(stmt, 0, &desc), RDB_OK);
    const char* first_name = desc.name;
    EXPECT_STREQ(first_name, "number");
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 1);
    RdbValue value = rdb_column_value(stmt, 1);
    ASSERT_FALSE(value.is_null);
    ASSERT_EQ(value.str_len, 2048);
    EXPECT_EQ(value.str_val[0], 'x');
    EXPECT_EQ(value.str_val[2047], 'x');
    RdbValue bytes = rdb_column_value(stmt, 2);
    ASSERT_EQ(bytes.type, RDB_TYPE_BLOB);
    ASSERT_FALSE(bytes.is_null);
    ASSERT_EQ(bytes.str_len, 3);
    EXPECT_EQ(memcmp(bytes.str_val, "\x00\x01\xff", 3), 0);
    EXPECT_TRUE(rdb_column_value(stmt, 3).is_null);
    EXPECT_EQ(rdb_prepare(conn, "SELECT 2"), nullptr);

    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 2);
    value = rdb_column_value(stmt, 1);
    ASSERT_EQ(value.str_len, 4000);
    EXPECT_EQ(value.str_val[0], 'y');
    EXPECT_EQ(value.str_val[3999], 'y');
    EXPECT_STREQ(first_name, "number");
    rdb_finalize(stmt);

    stmt = rdb_prepare(conn, "SELECT 1 AS answer WHERE false");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_column_count(stmt), 1);
    ASSERT_EQ(rdb_column_desc(stmt, 0, &desc), RDB_OK);
    EXPECT_STREQ(desc.name, "answer");
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    stmt = rdb_prepare(conn, "SELECT 3");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 3);
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

static void live_streaming_commands(const char* env, const char* driver) {
    const char* uri = getenv(env);
    if (!uri) GTEST_SKIP() << env << " not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver(driver)) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    RdbConn* conn = rdb_open(pool, uri, driver, false);
    ASSERT_NE(conn, nullptr);
    const char* commands[] = {
        "CREATE TEMPORARY TABLE dbench_stream_commands (id INT)",
        "INSERT INTO dbench_stream_commands VALUES (1)",
        "UPDATE dbench_stream_commands SET id=2 WHERE id=1"
    };
    for (const char* command : commands) {
        RdbStmt* stmt = rdb_prepare(conn, command);
        ASSERT_NE(stmt, nullptr);
        EXPECT_EQ(rdb_step(stmt), RDB_DONE) << command;
        rdb_finalize(stmt);
    }
    RdbStmt* stmt = rdb_prepare(conn, "SELECT id FROM dbench_stream_commands");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 2);
    EXPECT_EQ(rdb_step(stmt), RDB_DONE);
    rdb_finalize(stmt);
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveStreaming, PostgresqlCommands) {
    live_streaming_commands("LAMBDA_TEST_PG_URI", "postgresql");
}

TEST(RdbLiveStreaming, PostgresqlCopyDoesNotLeaveSessionInCopyMode) {
    const char* uri = getenv("LAMBDA_TEST_PG_URI");
    if (!uri) GTEST_SKIP() << "LAMBDA_TEST_PG_URI not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver("postgresql")) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    RdbConn* conn = rdb_open(pool, uri, "postgresql", false);
    ASSERT_NE(conn, nullptr);
    const char* queries[] = {
        "COPY (SELECT n FROM generate_series(1, 10000) n) TO STDOUT",
        "CREATE TEMP TABLE dbench_copy_test (id integer)",
        "COPY dbench_copy_test FROM STDIN"
    };
    for (int i = 0; i < 3; i++) {
        RdbStmt* stmt = rdb_prepare(conn, queries[i]);
        ASSERT_NE(stmt, nullptr);
        EXPECT_EQ(rdb_step(stmt), i == 1 ? RDB_DONE : RDB_ERROR);
        rdb_finalize(stmt);
        stmt = rdb_prepare(conn, "SELECT 1");
        ASSERT_NE(stmt, nullptr);
        EXPECT_EQ(rdb_step(stmt), RDB_ROW);
        rdb_finalize(stmt);
    }
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveStreaming, MysqlCommands) {
    live_streaming_commands("LAMBDA_TEST_MYSQL_URI", "mysql");
}

#endif  // _WIN32

/* Scoped SQLite primitives: real files, host-owned connections and no Lambda API yet. */
extern "C" {
}

static int tx_sql(RdbSqliteTxn* tx, const char* sql, const RdbValue* params = NULL,
                  int count = 0, RdbSqliteRowFn row = NULL, void* user = NULL) {
    return rdb_sqlite_tx_execute(tx, sql, strlen(sql), params, count, row, user);
}

typedef struct TxRowCheck {
    int rows;
    bool matches;
} TxRowCheck;

static bool tx_integer_row(void* user, int count, const char* const* names,
                           const RdbValue* values) {
    TxRowCheck* check = (TxRowCheck*)user;
    check->rows++;
    check->matches = count == 1 && strcmp(names[0], "n") == 0 &&
        values[0].type == RDB_TYPE_INT && values[0].int_val == 42;
    return true;
}

static bool tx_bytes_row(void* user, int count, const char* const* names,
                         const RdbValue* values) {
    (void)names;
    TxRowCheck* check = (TxRowCheck*)user;
    const char bytes[] = {'a', '\0', 'b'};
    check->rows++;
    check->matches = count == 3 && values[0].type == RDB_TYPE_STRING &&
        values[0].str_len == 3 && memcmp(values[0].str_val, bytes, 3) == 0 &&
        values[1].type == RDB_TYPE_BLOB && values[1].str_len == 3 &&
        memcmp(values[1].str_val, bytes, 3) == 0 && values[2].type == RDB_TYPE_BLOB &&
        values[2].str_len == 0 && !values[2].is_null;
    return true;
}

static bool tx_reject_row(void* user, int count, const char* const* names,
                          const RdbValue* values) {
    (void)user; (void)count; (void)names; (void)values;
    return false;
}

static bool tx_foreign_key_row(void* user, int count, const char* const* names,
                               const RdbValue* values) {
    (void)names;
    TxRowCheck* check = (TxRowCheck*)user;
    check->rows++;
    check->matches = count == 4 && values[0].type == RDB_TYPE_STRING &&
        values[0].str_val && strcmp(values[0].str_val, "deferred_fk") == 0;
    return true;
}

static bool tx_integrity_row(void* user, int count, const char* const* names,
                             const RdbValue* values) {
    (void)names;
    TxRowCheck* check = (TxRowCheck*)user;
    check->rows++;
    check->matches = count == 1 && values[0].type == RDB_TYPE_STRING &&
        values[0].str_val && strcmp(values[0].str_val, "ok") == 0;
    return true;
}

TEST_F(RdbTest, SqliteSingleStatementAutocommitsAndRollsBackFailure) {
    const char* create = "CREATE TABLE autocommit_entries (n INTEGER NOT NULL)";
    ASSERT_EQ(rdb_sqlite_execute_once(TEST_DB_PATH, create, strlen(create), NULL, 0, NULL, NULL), RDB_OK);
    const char* insert = "INSERT INTO autocommit_entries VALUES (?1)";
    RdbValue value = {};
    value.type = RDB_TYPE_INT;
    value.int_val = 42;
    ASSERT_EQ(rdb_sqlite_execute_once(TEST_DB_PATH, insert, strlen(insert), &value, 1, NULL, NULL), RDB_OK);
    const char* rejected = "INSERT INTO autocommit_entries VALUES (NULL)";
    EXPECT_EQ(rdb_sqlite_execute_once(TEST_DB_PATH, rejected, strlen(rejected), NULL, 0, NULL, NULL), RDB_ERROR);
    const char* returning = "INSERT INTO autocommit_entries VALUES (17) RETURNING n";
    EXPECT_EQ(rdb_sqlite_execute_once(TEST_DB_PATH, returning, strlen(returning), NULL, 0,
                                      tx_reject_row, NULL), RDB_ERROR);
    const char* query = "SELECT n FROM autocommit_entries";
    TxRowCheck check = {};
    EXPECT_EQ(rdb_sqlite_execute_once(TEST_DB_PATH, query, strlen(query), NULL, 0,
                                      tx_integer_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 1);
    EXPECT_TRUE(check.matches);
}

TEST_F(RdbTest, SqliteTransactionCommitsAndReadsOwnWrites) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE entries (n INTEGER NOT NULL)"), RDB_OK);
    RdbValue value = {};
    value.type = RDB_TYPE_INT;
    value.int_val = 42;
    ASSERT_EQ(tx_sql(tx, "INSERT INTO entries VALUES (?1)", &value, 1), RDB_OK);
    TxRowCheck check = {};
    ASSERT_EQ(tx_sql(tx, "SELECT n FROM entries", NULL, 0, tx_integer_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 1);
    EXPECT_TRUE(check.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT n FROM entries");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 42);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteExistingTransactionDoesNotCreateMissingFile) {
    ASSERT_EQ(unlink(TEST_DB_EMPTY), 0);
    EXPECT_EQ(rdb_sqlite_tx_begin_existing(pool, TEST_DB_EMPTY), nullptr);
    EXPECT_NE(access(TEST_DB_EMPTY, F_OK), 0);
    EXPECT_EQ(rdb_sqlite_tx_begin_existing(pool, ":memory:"), nullptr);
    EXPECT_EQ(rdb_sqlite_tx_begin_existing(pool, "file::memory:?mode=memory"), nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'Existing')"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteTransactionFailurePoisonsEvenWhenHandled) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'valid')"), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (11, NULL)"), RDB_ERROR);
    TxRowCheck check = {};
    EXPECT_EQ(tx_sql(tx, "SELECT 42 AS n", NULL, 0, tx_integer_row, &check), RDB_OK);
    EXPECT_TRUE(check.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteTransactionUnwindRollsBackAndReleasesLock) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'pending')"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, true), RDB_OK);
    tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'committed')"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteTransactionPreservesTextAndBlobBytes) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    const char bytes[] = {'a', '\0', 'b'};
    RdbValue params[3] = {};
    params[0].type = RDB_TYPE_STRING;
    params[0].str_val = bytes;
    params[0].str_len = 3;
    params[1].type = RDB_TYPE_BLOB;
    params[1].str_val = bytes;
    params[1].str_len = 3;
    params[2].type = RDB_TYPE_BLOB;
    TxRowCheck check = {};
    EXPECT_EQ(tx_sql(tx, "SELECT ?1, ?2, ?3", params, 3, tx_bytes_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 1);
    EXPECT_TRUE(check.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteTransactionRejectsUncontrolledStatements) {
    const char* statements[] = {
        "SELECT 1; SELECT 2", "ATTACH DATABASE ':memory:' AS other", "PRAGMA foreign_keys = OFF",
        "PRAGMA foreign_key_check = 'authors'", "PRAGMA integrity_check = 'authors'", "PRAGMA integrity_check = 1",
        "BEGIN", "SAVEPOINT nested", "SELECT load_extension('missing')"
    };
    for (const char* sql : statements) {
        RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
        ASSERT_NE(tx, nullptr);
        EXPECT_EQ(tx_sql(tx, sql, NULL, 0, tx_integer_row, NULL), RDB_ERROR) << sql;
        EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR) << sql;
    }
}

TEST_F(RdbTest, SqliteForeignKeyCheckReadsTheTransactionSnapshot) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE deferred_fk (author_id INTEGER REFERENCES authors(id) DEFERRABLE INITIALLY DEFERRED)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pragma_foreign_key_check (n INTEGER)"), RDB_OK);
    TxRowCheck check = {};
    ASSERT_EQ(tx_sql(tx, "PRAGMA main.foreign_key_check", NULL, 0, tx_foreign_key_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 0);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO deferred_fk VALUES (999)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "PRAGMA main.foreign_key_check", NULL, 0, tx_foreign_key_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 1);
    EXPECT_TRUE(check.matches);
    ASSERT_EQ(tx_sql(tx, "DELETE FROM deferred_fk"), RDB_OK);
    check = {};
    ASSERT_EQ(tx_sql(tx, "PRAGMA main.foreign_key_check", NULL, 0, tx_foreign_key_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 0);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteIntegrityCheckReadsTheTransactionSnapshot) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pragma_integrity_check (n INTEGER CHECK(n > 0))"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO pragma_integrity_check VALUES (1)"), RDB_OK);
    TxRowCheck check = {};
    ASSERT_EQ(tx_sql(tx, "PRAGMA main.integrity_check", NULL, 0, tx_integrity_row, &check), RDB_OK);
    EXPECT_EQ(check.rows, 1);
    EXPECT_TRUE(check.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteTransactionRejectsArityAndForeignKeyErrors) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "SELECT ?1", NULL, 0, tx_integer_row, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO books (id, title, author_id) VALUES (8, 'bad', 999)"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbTest, SqliteTransactionResultConsumerFailureRollsBack) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'pending')"), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "SELECT 1", NULL, 0, tx_reject_row, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteTransactionCommitFailureRollsBackSchemaAndData) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE deferred_child (id INTEGER REFERENCES authors(id) DEFERRABLE INITIALLY DEFERRED)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO deferred_child VALUES (999)"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_get_table(conn, "deferred_child"), nullptr);
    rdb_close(conn);
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static sqlite3_vfs* session_original_vfs;
static sqlite3_vfs session_no_move_vfs;
static sqlite3_io_methods session_no_move_methods;
static int (*session_original_control)(sqlite3_file*, int, void*);

static int session_no_move_control(sqlite3_file* file, int operation, void* argument) {
    if (operation == SQLITE_FCNTL_HAS_MOVED) return SQLITE_NOTFOUND;
    return session_original_control(file, operation, argument);
}

static int session_no_move_open(sqlite3_vfs*, const char* name, sqlite3_file* file,
        int flags, int* output_flags) {
    int rc = session_original_vfs->xOpen(session_original_vfs, name, file, flags, output_flags);
    if (rc == SQLITE_OK && (flags & SQLITE_OPEN_MAIN_DB) && file->pMethods) {
        session_no_move_methods = *file->pMethods;
        session_original_control = file->pMethods->xFileControl;
        session_no_move_methods.xFileControl = session_no_move_control;
        file->pMethods = &session_no_move_methods;
    }
    return rc;
}

TEST_F(RdbTest, SqliteFileSessionFailsClosedWhenVfsCannotDetectMoves) {
    session_original_vfs = sqlite3_vfs_find(NULL);
    ASSERT_NE(session_original_vfs, nullptr);
    session_no_move_vfs = *session_original_vfs;
    session_no_move_vfs.zName = "lambda-test-no-file-movement";
    session_no_move_vfs.xOpen = session_no_move_open;
    ASSERT_EQ(sqlite3_vfs_register(&session_no_move_vfs, 1), SQLITE_OK);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
    EXPECT_EQ(session, nullptr);
    if (session) (void)rdb_sqlite_file_session_close(session);
    // The pre-existing unbound API retains its compatibility behavior.
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
    EXPECT_NE(tx, nullptr);
    if (tx) EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(sqlite3_vfs_unregister(&session_no_move_vfs), SQLITE_OK);
    EXPECT_EQ(sqlite3_vfs_register(session_original_vfs, 1), SQLITE_OK);
}

TEST_F(RdbTest, SqliteMovedFileRollsBackAndRejectsFurtherWork) {
    const char* moved = "temp/test_rdb_moved.db";
    const char* spare = "temp/test_rdb_replacement.db";
    unlink(moved);
    for (int phase = 0; phase < 3; phase++) {
        unlink(spare);
        create_test_database(spare);
        RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
        ASSERT_NE(tx, nullptr);
        TxRowCheck row = {};
        ASSERT_EQ(tx_sql(tx, "SELECT 42 AS n", NULL, 0, tx_integer_row, &row), RDB_OK);
        if (phase == 1) ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'pending')"), RDB_OK);
        ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
        ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
        if (phase == 0) EXPECT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'wrong')"), RDB_ERROR);
        if (phase == 2) {
            uint8_t digest[32];
            EXPECT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 1024 * 1024, digest), RDB_ERROR);
        }
        EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
        // Both the replacement and the detached original must retain their rows.
        RdbConn* replacement = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
        RdbConn* original = rdb_open(pool, moved, "sqlite", true);
        ASSERT_NE(replacement, nullptr);
        ASSERT_NE(original, nullptr);
        ASSERT_EQ(rdb_load_schema(replacement), RDB_OK);
        ASSERT_EQ(rdb_load_schema(original), RDB_OK);
        EXPECT_EQ(rdb_row_count(replacement, "authors"), 3);
        EXPECT_EQ(rdb_row_count(original, "authors"), 3);
        rdb_close(replacement);
        rdb_close(original);
        ASSERT_EQ(unlink(TEST_DB_PATH), 0);
        ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    }
}

TEST_F(RdbTest, SqliteCloneRejectsMovedSourceBeforeAndAfterCopy) {
    const char* moved = "temp/test_rdb_clone_moved.db";
    const char* spare = "temp/test_rdb_clone_replacement.db";
    unlink(moved);
    for (int phase = 0; phase < 2; phase++) {
        unlink(spare);
        create_test_database(spare);
        RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
        ASSERT_NE(clone, nullptr);
        if (phase == 1) rdb_sqlite_clone_run(clone, NULL);
        ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
        ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
        if (phase == 0) rdb_sqlite_clone_run(clone, NULL);
        EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
        ASSERT_EQ(unlink(TEST_DB_PATH), 0);
        ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    }
}

TEST_F(RdbTest, SqliteRetargetedSymlinkRejectsCommit) {
    const char* link = "temp/test_rdb_link.db";
    unlink(link);
    ASSERT_EQ(symlink("test_rdb.db", link), 0);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, link);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'pending')"), RDB_OK);
    ASSERT_EQ(unlink(link), 0);
    ASSERT_EQ(symlink("test_rdb_empty.db", link), 0);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* original = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(original, nullptr);
    ASSERT_EQ(rdb_load_schema(original), RDB_OK);
    EXPECT_EQ(rdb_row_count(original, "authors"), 3);
    rdb_close(original);
    unlink(link);
}

TEST_F(RdbTest, SqliteFileSessionOwnsItsAnchorAcrossTransactionsAndCopies) {
    EXPECT_EQ(rdb_sqlite_file_session_open(pool, ":memory:"), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_open(pool, "file:unexpected"), nullptr);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(rdb_sqlite_file_session_current(session));
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_clone(pool, session, 1024 * 1024), nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'session')"), RDB_OK);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    TxRowCheck row = {};
    ASSERT_EQ(tx_sql(tx, "SELECT count(*)+38 AS n FROM authors", NULL, 0, tx_integer_row, &row), RDB_OK);
    EXPECT_TRUE(row.matches);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_ERROR);
    rdb_sqlite_clone_run(clone, NULL);
    tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    row = {};
    ASSERT_EQ(tx_sql(tx, "SELECT count(*)+38 AS n FROM authors", NULL, 0, tx_integer_row, &row), RDB_OK);
    EXPECT_TRUE(row.matches);
    // A captured clone retains its source until its transaction ends.
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    EXPECT_FALSE(rdb_sqlite_file_session_current(session));
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
}

struct SessionPublicationMove { const char* moved; const char* spare; int calls; bool move; };
static int session_publication_move(void* user, const void* data, size_t size) {
    SessionPublicationMove* state = (SessionPublicationMove*)user;
    if (!data || !size) return RDB_ERROR;
    state->calls++;
    if (state->move && (rename(TEST_DB_PATH, state->moved) || rename(state->spare, TEST_DB_PATH)))
        return RDB_ERROR;
    return RDB_OK;
}

TEST_F(RdbTest, SqliteFileSessionCloneChecksSourceThroughPublication) {
    const char* moved = "temp/test_rdb_session_publish_moved.db";
    const char* spare = "temp/test_rdb_session_publish_replacement.db";
    unlink(moved);
    for (int phase = 0; phase < 3; phase++) {
        unlink(spare);
        create_test_database(spare);
        RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
        ASSERT_NE(session, nullptr);
        RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
        ASSERT_NE(clone, nullptr);
        rdb_sqlite_clone_run(clone, NULL);
        RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
        ASSERT_NE(tx, nullptr);
        SessionPublicationMove state = {moved, spare, 0, phase != 0};
        if (phase == 0) {
            ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
            ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
        }
        if (phase == 2) {
            EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, session_publication_move, &state), RDB_SQLITE_UNCERTAIN);
            EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
        } else {
            EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, session_publication_move, &state),
                      phase == 0 ? RDB_ERROR : RDB_SQLITE_UNCERTAIN);
        }
        EXPECT_EQ(state.calls, phase == 0 ? 0 : 1);
        EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
        ASSERT_EQ(unlink(TEST_DB_PATH), 0);
        ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    }
}

TEST_F(RdbTest, SqliteFileSessionRejectsReplacementBetweenTransactions) {
    const char* moved = "temp/test_rdb_session_moved.db";
    const char* spare = "temp/test_rdb_session_replacement.db";
    unlink(moved); unlink(spare);
    create_test_database(spare);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
    ASSERT_NE(session, nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
    ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
    EXPECT_FALSE(rdb_sqlite_file_session_current(session));
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_clone(pool, session, 1024 * 1024), nullptr);
    ASSERT_EQ(unlink(TEST_DB_PATH), 0);
    ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    // Restoring the same still-open file is distinct from opening a copy.
    EXPECT_TRUE(rdb_sqlite_file_session_current(session));
    tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
}

TEST_F(RdbTest, SqliteFileSessionRejectsMoveDuringWriteOrClone) {
    const char* moved = "temp/test_rdb_session_active_moved.db";
    const char* spare = "temp/test_rdb_session_active_replacement.db";
    unlink(moved); unlink(spare);
    create_test_database(spare);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
    ASSERT_NE(session, nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'pending')"), RDB_OK);
    ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
    ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    ASSERT_EQ(unlink(TEST_DB_PATH), 0);
    ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    RdbConn* original = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(original, nullptr);
    ASSERT_EQ(rdb_load_schema(original), RDB_OK);
    EXPECT_EQ(rdb_row_count(original, "authors"), 3);
    rdb_close(original);
    create_test_database(spare);
    RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    ASSERT_EQ(rename(TEST_DB_PATH, moved), 0);
    ASSERT_EQ(rename(spare, TEST_DB_PATH), 0);
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    ASSERT_EQ(unlink(TEST_DB_PATH), 0);
    ASSERT_EQ(rename(moved, TEST_DB_PATH), 0);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
}

TEST_F(RdbTest, SqliteFileSessionDetectsRetargetedSymlink) {
    const char* link = "temp/test_rdb_session_link.db";
    unlink(link);
    ASSERT_EQ(symlink("test_rdb.db", link), 0);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, link);
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(unlink(link), 0);
    ASSERT_EQ(symlink("test_rdb_empty.db", link), 0);
    EXPECT_FALSE(rdb_sqlite_file_session_current(session));
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_clone(pool, session, 1024 * 1024), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    unlink(link);
}

TEST_F(RdbTest, SqliteFileSessionCloneIncludesCommittedWalWrites) {
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, TEST_DB_PATH);
    ASSERT_NE(session, nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id,name) VALUES (10,'wal')"), RDB_OK);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    TxRowCheck row = {};
    ASSERT_EQ(tx_sql(tx, "SELECT count(*)+38 AS n FROM authors", NULL, 0, tx_integer_row, &row), RDB_OK);
    EXPECT_TRUE(row.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
}
#endif

TEST_F(RdbTest, SqliteTransactionTreatsParametersAsData) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    const char text[] = "value'); DROP TABLE authors; --";
    RdbValue param = {};
    param.type = RDB_TYPE_STRING;
    param.str_val = text;
    param.str_len = (int)strlen(text);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, ?1)", &param, 1), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 4);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteTransactionRejectsEmbeddedNulInSql) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    const char sql[] = "SELECT 1\0; DELETE FROM authors";
    EXPECT_EQ(rdb_sqlite_tx_execute(tx, sql, sizeof(sql) - 1, NULL, 0, tx_integer_row, NULL), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbRegistryTest, SqliteTransactionTeardownRollsBackBeforeLateFinish) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_transaction_registry.db";
    unlink(path);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, path);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(registry.adds, 1);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pending (n INTEGER)"), RDB_OK);
    // Context teardown owns the native connection even if the transaction outlives it.
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    EXPECT_EQ(registry.removes, 0);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sqlite_master WHERE name='pending'", -1, &stmt, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink(path);
}

static bool tx_teardown_row(void* user, int count, const char* const* names,
                            const RdbValue* values) {
    (void)count; (void)names; (void)values;
    rdb_conn_life_close((RdbConnLife*)user);
    return true;
}

TEST_F(RdbRegistryTest, SqliteTransactionTeardownDuringRowDefersClose) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_transaction_row_teardown.db";
    unlink(path);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, path);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pending (n INTEGER)"), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "SELECT 1", NULL, 0, tx_teardown_row, registry.life), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sqlite_master WHERE name='pending'", -1, &stmt, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink(path);
}

struct SqliteWorkRace {
    RdbSqliteWork* work;
    uv_sem_t entered;
    uv_sem_t resume;
    int cancelled;
    int result;
    bool waited;
    const char* sql;
};

static bool sqlite_work_wait_row(void* user, int count, const char* const* names,
                                 const RdbValue* values) {
    (void)count; (void)names; (void)values;
    SqliteWorkRace* race = (SqliteWorkRace*)user;
    if (!race->waited) {
        race->waited = true;
        uv_sem_post(&race->entered);
        uv_sem_wait(&race->resume);
    }
    return true;
}

static void sqlite_work_thread(void* user) {
    SqliteWorkRace* race = (SqliteWorkRace*)user;
    race->result = rdb_sqlite_tx_work_execute(race->work, race->sql, strlen(race->sql),
        NULL, 0, sqlite_work_wait_row, race, &race->cancelled);
}

TEST_F(RdbRegistryTest, SqliteWorkerPinDefersRegistryTeardown) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_worker_teardown.db";
    unlink(path);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, path);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pending (n INTEGER)"), RDB_OK);
    SqliteWorkRace race = {};
    race.work = rdb_sqlite_tx_work_begin(tx);
    race.sql = "SELECT 42";
    ASSERT_NE(race.work, nullptr);
    ASSERT_EQ(uv_sem_init(&race.entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&race.resume, 0), 0);
    uv_thread_t worker;
    ASSERT_EQ(uv_thread_create(&worker, sqlite_work_thread, &race), 0);
    uv_sem_wait(&race.entered);
    // The registry closes while SQLite is active; the owner-held pin defers close.
    rdb_conn_life_close(registry.life);
    uv_sem_post(&race.resume);
    ASSERT_EQ(uv_thread_join(&worker), 0);
    EXPECT_EQ(race.result, RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_work_end(race.work), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    uv_sem_destroy(&race.entered);
    uv_sem_destroy(&race.resume);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sqlite_master WHERE name='pending'", -1, &stmt, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    unlink(path);
}

TEST_F(RdbTest, SqliteWorkerInterruptsRunningStatement) {
    ASSERT_NE(sqlite3_threadsafe(), 0);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'pending')"), RDB_OK);
    SqliteWorkRace race = {};
    race.work = rdb_sqlite_tx_work_begin(tx);
    race.sql = "WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n + 1 FROM seq WHERE n < 100000000) SELECT n FROM seq";
    ASSERT_NE(race.work, nullptr);
    ASSERT_EQ(uv_sem_init(&race.entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&race.resume, 0), 0);
    uv_thread_t worker;
    ASSERT_EQ(uv_thread_create(&worker, sqlite_work_thread, &race), 0);
    uv_sem_wait(&race.entered);
    __atomic_store_n(&race.cancelled, 1, __ATOMIC_RELEASE);
    uv_sem_post(&race.resume);
    ASSERT_EQ(uv_thread_join(&worker), 0);
    EXPECT_EQ(race.result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_work_end(race.work), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    uv_sem_destroy(&race.entered);
    uv_sem_destroy(&race.resume);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteWorkerCancellationPoisonsTransaction) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE pending (n INTEGER)"), RDB_OK);
    RdbSqliteWork* work = rdb_sqlite_tx_work_begin(tx);
    ASSERT_NE(work, nullptr);
    int cancelled = 1;
    const char* sql = "INSERT INTO pending VALUES (1)";
    EXPECT_EQ(rdb_sqlite_tx_work_execute(work, sql, strlen(sql), NULL, 0, NULL, NULL, &cancelled), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_work_end(work), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT COUNT(*) AS n FROM sqlite_master WHERE name='pending'");
    ASSERT_NE(stmt, nullptr);
    ASSERT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 0);
    rdb_finalize(stmt);
    rdb_close(conn);
}

TEST_F(RdbRegistryTest, HeldConnectionDefersCloseUntilFinalRelease) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    RdbConnLife* first = rdb_conn_life_hold(conn);
    RdbConnLife* second = rdb_conn_life_hold(conn);
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first, second);
    rdb_close(conn);
    EXPECT_EQ(registry.removes, 1);
    EXPECT_EQ(fake.closes, 0);
    EXPECT_EQ(rdb_ping(conn), RDB_CONN_LOST);
    rdb_conn_life_release(first);
    EXPECT_EQ(fake.closes, 0);
    rdb_conn_life_release(second);
    EXPECT_EQ(fake.closes, 1);
}

struct SchemaWorkRace { RdbSchemaWork* work; int status; };
static void schema_work_thread(void* user) {
    SchemaWorkRace* race = (SchemaWorkRace*)user;
    race->status = rdb_schema_work_run(race->work, NULL);
}

TEST_F(RdbRegistryTest, SchemaWorkerPinsConnectionThroughRegistryTeardown) {
    RdbConn* conn = rdb_open(pool, "fakedb://db.example/x", "fakedb", true);
    ASSERT_NE(conn, nullptr);
    Pool* result_pool = pool_create();
    ASSERT_NE(result_pool, nullptr);
    SchemaWorkRace race = {rdb_schema_work_begin(conn, result_pool), RDB_ERROR};
    ASSERT_NE(race.work, nullptr);
    uv_sem_t entered, resume;
    ASSERT_EQ(uv_sem_init(&entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&resume, 0), 0);
    fake.schema_entered = &entered;
    fake.schema_resume = &resume;
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, schema_work_thread, &race), 0);
    uv_sem_wait(&entered);
    rdb_conn_life_close(registry.life);
    EXPECT_EQ(fake.closes, 0);
    uv_sem_post(&resume);
    uv_thread_join(&thread);
    EXPECT_EQ(race.status, RDB_OK);
    RdbSchema output = {};
    EXPECT_EQ(rdb_schema_work_end(race.work, &output), RDB_ERROR);
    EXPECT_EQ(fake.closes, 1);
    rdb_close(conn);
    uv_sem_destroy(&entered);
    uv_sem_destroy(&resume);
    pool_destroy(result_pool);
}

TEST_F(RdbTest, SchemaWorkerReturnsIndependentMetadataAndHonorsCancellation) {
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    Pool* result_pool = pool_create();
    ASSERT_NE(result_pool, nullptr);
    RdbSchemaWork* work = rdb_schema_work_begin(conn, result_pool);
    ASSERT_NE(work, nullptr);
    ASSERT_EQ(rdb_schema_work_run(work, NULL), RDB_OK);
    RdbSchema result = {};
    ASSERT_EQ(rdb_schema_work_end(work, &result), RDB_OK);
    rdb_close(conn);
    bool found_books = false;
    for (int i = 0; i < result.table_count; i++) {
        if (strcmp(result.tables[i].name, "books") == 0) {
            found_books = result.tables[i].column_count == 7 && result.tables[i].fk_count == 1;
        }
    }
    EXPECT_TRUE(found_books);
    conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    work = rdb_schema_work_begin(conn, result_pool);
    ASSERT_NE(work, nullptr);
    int cancelled = 1;
    EXPECT_EQ(rdb_schema_work_run(work, &cancelled), RDB_ERROR);
    EXPECT_EQ(rdb_schema_work_end(work, &result), RDB_ERROR);
    rdb_close(conn);
    pool_destroy(result_pool);
}

#ifndef _WIN32
static void live_open_worker(const char* env, const char* driver) {
    const char* uri = getenv(env);
    if (!uri) GTEST_SKIP() << env << " not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver(driver)) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    OpenWorkRace race = {rdb_open_work_begin(pool, uri, driver, true), RDB_ERROR, NULL};
    ASSERT_NE(race.work, nullptr);
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, open_work_thread, &race), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(race.status, RDB_OK);
    RdbConn* conn = rdb_open_work_end(race.work, true);
    ASSERT_NE(conn, nullptr);
    RdbStmt* stmt = rdb_prepare(conn, "SELECT 1");
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(rdb_step(stmt), RDB_ROW);
    EXPECT_EQ(rdb_column_value(stmt, 0).int_val, 1);
    rdb_finalize(stmt);
    rdb_close(conn);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveOpenWork, PostgresqlConnection) {
    live_open_worker("LAMBDA_TEST_PG_URI", "postgresql");
}
TEST(RdbLiveOpenWork, MysqlConnection) {
    live_open_worker("LAMBDA_TEST_MYSQL_URI", "mysql");
}

static void live_schema_worker(const char* env, const char* driver) {
    const char* uri = getenv(env);
    if (!uri) GTEST_SKIP() << env << " not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver(driver)) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    Pool* result_pool = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_NE(result_pool, nullptr);
    RdbConn* conn = rdb_open(pool, uri, driver, true);
    ASSERT_NE(conn, nullptr);
    SchemaWorkRace race = {rdb_schema_work_begin(conn, result_pool), RDB_ERROR};
    ASSERT_NE(race.work, nullptr);
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, schema_work_thread, &race), 0);
    uv_thread_join(&thread);
    ASSERT_EQ(race.status, RDB_OK);
    RdbSchema schema = {};
    ASSERT_EQ(rdb_schema_work_end(race.work, &schema), RDB_OK);
    rdb_close(conn);
    ASSERT_GT(schema.table_count, 0);
    ASSERT_GT(schema.tables[0].column_count, 0);
    EXPECT_NE(schema.tables[0].columns[0].name, nullptr);
    pool_destroy(result_pool);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveSchemaWork, PostgresqlMetadata) {
    live_schema_worker("LAMBDA_TEST_PG_URI", "postgresql");
}
TEST(RdbLiveSchemaWork, MysqlMetadata) {
    live_schema_worker("LAMBDA_TEST_MYSQL_URI", "mysql");
}

static void live_statement_workers(const char* env, const char* driver) {
    const char* uri = getenv(env);
    if (!uri) GTEST_SKIP() << env << " not set";
    rdb_set_driver_resolver(live_resolver);
    if (!rdb_get_driver(driver)) GTEST_SKIP() << "rdb-drivers module not built";
    Pool* pool = pool_create();
    Pool* metadata = pool_create();
    ASSERT_NE(pool, nullptr);
    ASSERT_NE(metadata, nullptr);
    RdbConn* conn = rdb_open(pool, uri, driver, true);
    ASSERT_NE(conn, nullptr);
    const char* queries[] = {
        "SELECT 1 AS n, 10 AS n UNION ALL SELECT 2, 20 UNION ALL SELECT 3, 30",
        "SELECT 1 AS n, 10 AS n WHERE 1=0"
    };
    for (int i = 0; i < 2; i++) {
        PrepareWorkRace prepare = {rdb_prepare_work_begin(conn, queries[i], NULL, 0), RDB_ERROR};
        ASSERT_NE(prepare.work, nullptr);
        uv_thread_t thread;
        ASSERT_EQ(uv_thread_create(&thread, prepare_work_thread, &prepare), 0);
        uv_thread_join(&thread);
        ASSERT_EQ(prepare.status, RDB_OK);
        RdbStmt* stmt = rdb_prepare_work_end(prepare.work, true);
        ASSERT_NE(stmt, nullptr);
        FetchRows rows = {};
        RdbFetchResult result = {};
        int batches = i ? 1 : 2;
        for (int b = 0; b < batches; b++) {
            FetchWorkRace fetch = {rdb_fetch_work_begin(stmt, 2, 10, metadata, capture_fetch_row, &rows), RDB_ERROR};
            ASSERT_NE(fetch.work, nullptr);
            ASSERT_EQ(uv_thread_create(&thread, fetch_work_thread, &fetch), 0);
            uv_thread_join(&thread);
            ASSERT_EQ(fetch.status, RDB_OK);
            ASSERT_EQ(rdb_fetch_work_end(fetch.work, &result), RDB_OK);
            ASSERT_EQ(result.column_count, 2);
            EXPECT_STREQ(result.columns[0].name, "n");
            EXPECT_STREQ(result.columns[1].name, "n");
        }
        EXPECT_TRUE(result.done);
        EXPECT_EQ(rows.count, i ? 0 : 3);
        rdb_finalize(stmt);
    }
    rdb_close(conn);
    pool_destroy(metadata);
    pool_destroy(pool);
    rdb_set_driver_resolver(NULL);
}

TEST(RdbLiveStatementWork, PostgresqlResults) {
    live_statement_workers("LAMBDA_TEST_PG_URI", "postgresql");
}
TEST(RdbLiveStatementWork, MysqlResults) {
    live_statement_workers("LAMBDA_TEST_MYSQL_URI", "mysql");
}
#endif

TEST_F(RdbRegistryTest, SqliteTransactionHonorsHostAuthorization) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_transaction_refused.db";
    unlink(path);
    registry.refuse_authorize = true;
    EXPECT_EQ(rdb_sqlite_tx_begin(pool, path), nullptr);
    const char* sql = "CREATE TABLE forbidden (n INTEGER)";
    EXPECT_EQ(rdb_sqlite_execute_once(path, sql, strlen(sql), NULL, 0, NULL, NULL), RDB_ERROR);
    EXPECT_EQ(registry.refused_authorizations, 2);
    EXPECT_EQ(registry.adds, 0);
    EXPECT_NE(access(path, F_OK), 0);
}

typedef struct TxReentryCheck {
    RdbSqliteTxn* tx;
    int inner_result;
} TxReentryCheck;

static bool tx_reenter_row(void* user, int count, const char* const* names,
                           const RdbValue* values) {
    (void)count; (void)names; (void)values;
    TxReentryCheck* check = (TxReentryCheck*)user;
    check->inner_result = tx_sql(check->tx, "DELETE FROM authors");
    return true;
}

TEST_F(RdbTest, SqliteTransactionRejectsReentryAndRemainsRollbackOnly) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'pending')"), RDB_OK);
    TxReentryCheck check = {tx, RDB_OK};
    EXPECT_EQ(tx_sql(tx, "SELECT 1", NULL, 0, tx_reenter_row, &check), RDB_OK);
    EXPECT_EQ(check.inner_result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteTransactionAcceptsTriggerBodyAsSingleStatement) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE tx_source (n INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE tx_audit (n INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "CREATE TRIGGER tx_after_insert AFTER INSERT ON tx_source "
        "BEGIN INSERT INTO tx_audit VALUES (NEW.n); END;"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO tx_source VALUES (42)"), RDB_OK);
    TxRowCheck check = {};
    EXPECT_EQ(tx_sql(tx, "SELECT n FROM tx_audit", NULL, 0, tx_integer_row, &check), RDB_OK);
    EXPECT_TRUE(check.matches);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

struct SnapshotCheck {
    int calls;
    int outcome;
    bool contents_match;
    RdbConnLife* close_during_publish;
    RdbSqliteTxn* reenter_during_publish;
    int reenter_result;
};

static int check_snapshot(void* user, const void* data, size_t size) {
    SnapshotCheck* check = (SnapshotCheck*)user;
    check->calls++;
    sqlite3* db = NULL;
    unsigned char* copy = (unsigned char*)sqlite3_malloc64(size);
    if (!copy || sqlite3_open(":memory:", &db) != SQLITE_OK) {
        sqlite3_free(copy);
        if (db) sqlite3_close(db);
        return RDB_ERROR;
    }
    memcpy(copy, data, size);
    int result = sqlite3_deserialize(db, "main", copy, size, size, SQLITE_DESERIALIZE_FREEONCLOSE);
    sqlite3_stmt* stmt = NULL;
    if (result == SQLITE_OK && sqlite3_prepare_v2(db, "SELECT value FROM sample", -1, &stmt, NULL) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 42)
        check->contents_match = true;
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (check->close_during_publish) rdb_conn_life_close(check->close_during_publish);
    if (check->reenter_during_publish) check->reenter_result = tx_sql(check->reenter_during_publish, "SELECT 1");
    return check->outcome;
}

TEST_F(RdbTest, SqliteMemorySnapshotPublishesOnlyAfterCommit) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES (42)"), RDB_OK);
    SnapshotCheck check = {};
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), RDB_OK);
    EXPECT_EQ(check.calls, 1);
    EXPECT_TRUE(check.contents_match);
}

TEST_F(RdbTest, SqliteMemorySnapshotRollsBackAndLimitsBeforePublication) {
    SnapshotCheck check = {};
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO missing VALUES (1)"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), RDB_ERROR);
    EXPECT_EQ(check.calls, 0);
    tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 16, check_snapshot, &check), RDB_ERROR);
    EXPECT_EQ(check.calls, 0);
    tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors (id, name) VALUES (10, 'pending')"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), RDB_ERROR);
    EXPECT_EQ(check.calls, 0);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "authors"), 3);
    rdb_close(conn);
}

TEST_F(RdbTest, SqliteClonePreimageDoesNotPublishOnSqliteOutOfMemory) {
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    SnapshotCheck check = {};
    sqlite3_int64 previous_soft = sqlite3_soft_heap_limit64(-1);
    sqlite3_int64 previous_hard = sqlite3_hard_heap_limit64(1);
    int result = rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot, &check);
    sqlite3_hard_heap_limit64(previous_hard);
    sqlite3_soft_heap_limit64(previous_soft);
    EXPECT_EQ(result, RDB_ERROR);
    EXPECT_EQ(check.calls, 0);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbTest, SqliteMemorySnapshotDoesNotPublishOnSqliteOutOfMemory) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES (42)"), RDB_OK);
    SnapshotCheck check = {};
    sqlite3_int64 previous_soft = sqlite3_soft_heap_limit64(-1);
    sqlite3_int64 previous_hard = sqlite3_hard_heap_limit64(1);
    int result = rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check);
    sqlite3_hard_heap_limit64(previous_hard);
    sqlite3_soft_heap_limit64(previous_soft);
    EXPECT_EQ(result, RDB_ERROR);
    EXPECT_EQ(check.calls, 0);
}

TEST_F(RdbTest, SqliteMemorySnapshotReportsUncertainPublication) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES (42)"), RDB_OK);
    SnapshotCheck check = {};
    check.outcome = 1;
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), 1);
    EXPECT_EQ(check.calls, 1);
    EXPECT_TRUE(check.contents_match);
}

TEST_F(RdbTest, SqliteMemorySnapshotRejectsPublicationReentry) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES (42)"), RDB_OK);
    SnapshotCheck check = {};
    check.reenter_during_publish = tx;
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), RDB_SQLITE_UNCERTAIN);
    EXPECT_EQ(check.reenter_result, RDB_ERROR);
    EXPECT_EQ(check.calls, 1);
    EXPECT_TRUE(check.contents_match);
}

TEST_F(RdbRegistryTest, SqliteMemorySnapshotPinsAcrossRegistryTeardown) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES (42)"), RDB_OK);
    SnapshotCheck check = {};
    check.close_during_publish = registry.life;
    // A published image remains published even if registry teardown interrupts
    // the owner's post-publication check; the status must describe uncertainty.
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &check), 1);
    EXPECT_EQ(check.calls, 1);
    EXPECT_TRUE(check.contents_match);
}

struct CloneCount { int count; int64_t value; };
static bool clone_count_row(void* user, int count, const char* const* names, const RdbValue* values) {
    (void)names;
    CloneCount* result = (CloneCount*)user;
    if (count != 1 || values[0].type != RDB_TYPE_INT) return false;
    result->count++;
    result->value = values[0].int_val;
    return true;
}

static int write_snapshot_callback(void* user, const void* data, size_t size) {
    return write_binary_file((const char*)user, (const char*)data, size) == 0 ? RDB_OK : RDB_ERROR;
}

TEST_F(RdbTest, SqliteCloneCopiesAConsistentReadSnapshot) {
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "INSERT INTO authors(id,name) VALUES (10,'later')", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    CloneCount check = {};
    EXPECT_EQ(tx_sql(tx, "SELECT COUNT(*) FROM authors", NULL, 0, clone_count_row, &check), RDB_OK);
    EXPECT_EQ(check.count, 1);
    EXPECT_EQ(check.value, 3);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
}

TEST_F(RdbTest, SqliteSnapshotDigestChecksTheCompleteReadSnapshot) {
    uint8_t original[32] = {}, repeated[32] = {}, cloned[32] = {}, published[32] = {};
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    CloneCount count = {};
    ASSERT_EQ(tx_sql(tx, "SELECT COUNT(*) FROM authors", NULL, 0, clone_count_row, &count), RDB_OK);
    ASSERT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 1024 * 1024, original), RDB_OK);
    ASSERT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 1024 * 1024, repeated), RDB_OK);
    EXPECT_EQ(memcmp(original, repeated, sizeof(original)), 0);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 1024 * 1024, cloned), RDB_OK);
    const char* path = "temp/test_rdb_digest_preimage.db";
    ASSERT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, write_snapshot_callback, (void*)path), RDB_OK);
    RdbSqliteTxn* reopened = rdb_sqlite_tx_begin_existing(pool, path);
    ASSERT_NE(reopened, nullptr);
    ASSERT_EQ(rdb_sqlite_tx_snapshot_digest(reopened, 1024 * 1024, published), RDB_OK);
    EXPECT_EQ(memcmp(cloned, published, sizeof(cloned)), 0);
    EXPECT_EQ(rdb_sqlite_tx_finish(reopened, false), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO authors(id,name) VALUES (99,'pending')"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 1024 * 1024, repeated), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(rdb_sqlite_tx_snapshot_digest(tx, 16, repeated), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

#ifdef __APPLE__
typedef ssize_t (*DigestPreadFn)(int, void*, size_t, off_t);
static DigestPreadFn digest_original_pread;
static int digest_read_fault;
static int digest_read_faults;

static ssize_t digest_fault_pread(int fd, void* data, size_t size, off_t offset) {
    if (__atomic_exchange_n(&digest_read_fault, 0, __ATOMIC_ACQ_REL)) {
        __atomic_add_fetch(&digest_read_faults, 1, __ATOMIC_RELAXED);
        errno = EIO;
        return -1;
    }
    return digest_original_pread(fd, data, size, offset);
}

TEST_F(RdbTest, SqliteSnapshotDigestRejectsPageReadFailure) {
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE digest_payload(data BLOB); INSERT INTO digest_payload VALUES (zeroblob(8388608))", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin_existing(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    CloneCount count = {};
    ASSERT_EQ(tx_sql(tx, "SELECT COUNT(*) FROM authors", NULL, 0, clone_count_row, &count), RDB_OK);
    sqlite3_vfs* vfs = sqlite3_vfs_find(NULL);
    ASSERT_NE(vfs, nullptr);
    ASSERT_GE(vfs->iVersion, 3);
    digest_original_pread = (DigestPreadFn)vfs->xGetSystemCall(vfs, "pread");
    ASSERT_NE(digest_original_pread, nullptr);
    ASSERT_EQ(vfs->xSetSystemCall(vfs, "pread", (sqlite3_syscall_ptr)digest_fault_pread), SQLITE_OK);
    digest_read_faults = 0;
    __atomic_store_n(&digest_read_fault, 1, __ATOMIC_RELEASE);
    uint8_t digest[32] = {};
    int result = rdb_sqlite_tx_snapshot_digest(tx, 16 * 1024 * 1024, digest);
    __atomic_store_n(&digest_read_fault, 0, __ATOMIC_RELEASE);
    EXPECT_EQ(vfs->xSetSystemCall(vfs, "pread", (sqlite3_syscall_ptr)digest_original_pread), SQLITE_OK);
    EXPECT_EQ(digest_read_faults, 1);
    EXPECT_EQ(result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}
#endif

TEST_F(RdbTest, SqliteCloneRejectsMissingOversizedCancelledAndBrokenSources) {
    const char* absent = "temp/test_rdb_clone_absent.db";
    unlink(absent);
    EXPECT_EQ(rdb_sqlite_clone_prepare(pool, absent, 1024 * 1024), nullptr);
    EXPECT_NE(access(absent, F_OK), 0);
    EXPECT_EQ(rdb_sqlite_clone_prepare(pool, ":memory:", 1024 * 1024), nullptr);
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 16);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    int cancelled = 1;
    rdb_sqlite_clone_run(clone, &cancelled);
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA foreign_keys=OFF; INSERT INTO books(id,title,author_id) VALUES (99,'orphan',999)", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    const char* corrupt = "temp/test_rdb_clone_corrupt.db";
    const char bytes[] = "this is not a SQLite database";
    ASSERT_EQ(write_binary_file(corrupt, bytes, sizeof(bytes)), 0);
    clone = rdb_sqlite_clone_prepare(pool, corrupt, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    unlink(corrupt);
}

// Clone publication can register a third, short-lived checked-image connection.
struct CloneRegistry { RdbConnLife* lives[3]; int count; RdbConnLife* revoke_on_add; };
static CloneRegistry clone_registry;
static int clone_registry_add(RdbConnLife* life, const char*, uint32_t* rid, void** owner) {
    if ((size_t)clone_registry.count >= sizeof(clone_registry.lives) / sizeof(clone_registry.lives[0])) return RDB_ERROR;
    clone_registry.lives[clone_registry.count++] = life;
    *rid = clone_registry.count;
    *owner = &clone_registry;
    if (clone_registry.revoke_on_add) {
        RdbConnLife* victim = clone_registry.revoke_on_add;
        clone_registry.revoke_on_add = NULL;
        rdb_conn_life_close(victim);
    }
    return RDB_OK;
}
static void clone_registry_remove(void* owner, uint32_t rid) {
    CloneRegistry* registry = (CloneRegistry*)owner;
    if (rid >= 1 && rid <= (uint32_t)registry->count) rdb_conn_life_close(registry->lives[rid - 1]);
}
static const RdbRegistryHooks clone_registry_hooks = { reg_authorize, clone_registry_add, clone_registry_remove };

static RdbConnLife* session_authorize_victim;
static RdbSqliteFileSession* session_authorize_target;
static int session_authorize_close_result;
static int session_authorize_skip;
static bool session_authorize_reject_after_close;
static RdbSqliteWork* session_authorize_work;
static int session_authorize_work_result;
static int session_authorize_revoke(const char* driver, const RdbTarget* targets, int count) {
    if (session_authorize_victim) {
        RdbConnLife* victim = session_authorize_victim;
        session_authorize_victim = NULL;
        rdb_conn_life_close(victim);
    }
    if (session_authorize_target && session_authorize_skip > 0) {
        session_authorize_skip--;
    } else if (session_authorize_target) {
        RdbSqliteFileSession* target = session_authorize_target;
        session_authorize_target = NULL;
        session_authorize_close_result = rdb_sqlite_file_session_close(target);
        if (session_authorize_reject_after_close) return RDB_ERROR;
    }
    if (session_authorize_work) {
        RdbSqliteWork* work = session_authorize_work;
        session_authorize_work = NULL;
        session_authorize_work_result = rdb_sqlite_tx_work_end(work);
    }
    return reg_authorize(driver, targets, count);
}
static const RdbRegistryHooks session_authorize_hooks = {
    session_authorize_revoke, clone_registry_add, clone_registry_remove
};

struct CloneTeardownRace { RdbSqliteClone* clone; uv_sem_t entered; uv_sem_t resume; };
static void clone_teardown_worker(void* user) {
    CloneTeardownRace* race = (CloneTeardownRace*)user;
    uv_sem_post(&race->entered);
    uv_sem_wait(&race->resume);
    rdb_sqlite_clone_run(race->clone, NULL);
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static void session_registry_database(const char* path, bool seed) {
    unlink(path);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    const char* sql = seed ? "CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)" :
        "CREATE TABLE sample(value INTEGER)";
    ASSERT_EQ(sqlite3_exec(db, sql, NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
}

static void session_registry_expect_count(const char* path, int expected) {
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(db, "SELECT count(*) FROM sample", -1, &stmt, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), expected);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

TEST_F(RdbRegistryTest, SqliteFileSessionRechecksHostAuthorization) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_policy.db";
    session_registry_database(path, false);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    registry.refuse_authorize = true;
    EXPECT_FALSE(rdb_sqlite_file_session_current(session));
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_clone(pool, session, 1024 * 1024), nullptr);
    registry.refuse_authorize = false;
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(1)"), RDB_OK);
    registry.refuse_authorize = true;
    EXPECT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(2)"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    registry.refuse_authorize = false;
    RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    registry.refuse_authorize = true;
    EXPECT_EQ(rdb_sqlite_clone_finish(clone), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    registry.refuse_authorize = false;
    session_registry_expect_count(path, 0);
    unlink(path);
}

TEST_F(RdbRegistryTest, SqliteFileSessionAuthorizationCanRevokeFinishingTransaction) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_policy_revoke.db";
    session_registry_database(path, false);
    memset(&clone_registry, 0, sizeof(clone_registry));
    session_authorize_victim = NULL;
    session_authorize_target = NULL;
    session_authorize_work = NULL;
    rdb_set_registry_hooks(&session_authorize_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    session_authorize_target = session;
    session_authorize_close_result = RDB_OK;
    EXPECT_TRUE(rdb_sqlite_file_session_current(session));
    EXPECT_EQ(session_authorize_close_result, RDB_ERROR);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(1)"), RDB_OK);
    ASSERT_EQ(clone_registry.count, 2);
    session_authorize_victim = clone_registry.lives[1];
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    EXPECT_EQ(session_authorize_victim, nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    session_registry_expect_count(path, 0);
    unlink(path);
}

TEST_F(RdbRegistryTest, SqliteFileSessionRejectsCloseDuringTransactionAcquisition) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_begin_reentrant.db";
    session_registry_database(path, false);
    memset(&clone_registry, 0, sizeof(clone_registry));
    session_authorize_victim = NULL;
    session_authorize_target = NULL;
    session_authorize_work = NULL;
    rdb_set_registry_hooks(&session_authorize_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    // Skip the session check and reenter from the transaction's open authorization.
    session_authorize_target = session;
    session_authorize_skip = 1;
    session_authorize_close_result = RDB_OK;
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    EXPECT_EQ(session_authorize_target, nullptr);
    EXPECT_EQ(session_authorize_close_result, RDB_ERROR);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(1)"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    session_authorize_target = session;
    session_authorize_skip = 1;
    session_authorize_reject_after_close = true;
    EXPECT_EQ(rdb_sqlite_file_session_begin(pool, session), nullptr);
    session_authorize_reject_after_close = false;
    EXPECT_EQ(session_authorize_close_result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    session_registry_expect_count(path, 1);
    unlink(path);
}

static int session_publication_revoke_policy(void* user, const void* data, size_t size) {
    int* calls = (int*)user;
    if (!data || !size) return RDB_ERROR;
    (*calls)++;
    registry.refuse_authorize = true;
    return RDB_OK;
}

TEST_F(RdbRegistryTest, SqliteFileSessionCloneReportsPolicyRevocationDuringPublication) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_publish_policy.db";
    session_registry_database(path, true);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    RdbSqliteClone* clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    int calls = 0;
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024,
        session_publication_revoke_policy, &calls), RDB_SQLITE_UNCERTAIN);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    registry.refuse_authorize = false;
    session_registry_expect_count(path, 1);
    unlink(path);
}

TEST_F(RdbRegistryTest, SqliteFileSessionRevocationPoisonsActiveTransaction) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_revoked.db";
    session_registry_database(path, false);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    RdbSqliteWork* work = rdb_sqlite_tx_work_begin(tx);
    ASSERT_NE(work, nullptr);
    ASSERT_EQ(clone_registry.count, 2);
    rdb_conn_life_close(clone_registry.lives[0]);
    EXPECT_FALSE(rdb_sqlite_file_session_current(session));
    int cancelled = 0;
    const char sql[] = "INSERT INTO sample VALUES(1)";
    (void)rdb_sqlite_tx_work_execute(work, sql, sizeof(sql) - 1, NULL, 0, NULL, NULL, &cancelled);
    EXPECT_EQ(rdb_sqlite_tx_work_end(work), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    session_registry_expect_count(path, 0);
    unlink(path);
}

TEST_F(RdbRegistryTest, SqliteFileSessionRejectsReentrantWorkCompletion) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_reentrant_work.db";
    session_registry_database(path, false);
    memset(&clone_registry, 0, sizeof(clone_registry));
    session_authorize_victim = NULL;
    session_authorize_target = NULL;
    session_authorize_work = NULL;
    rdb_set_registry_hooks(&session_authorize_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    RdbSqliteTxn* tx = rdb_sqlite_file_session_begin(pool, session);
    ASSERT_NE(tx, nullptr);
    RdbSqliteWork* work = rdb_sqlite_tx_work_begin(tx);
    ASSERT_NE(work, nullptr);
    const char sql[] = "INSERT INTO sample VALUES(1)";
    int cancelled = 0;
    ASSERT_EQ(rdb_sqlite_tx_work_execute(work, sql, sizeof(sql) - 1, NULL, 0, NULL, NULL, &cancelled), RDB_OK);
    session_authorize_work = work;
    session_authorize_work_result = RDB_OK;
    EXPECT_EQ(rdb_sqlite_tx_work_end(work), RDB_OK);
    EXPECT_EQ(session_authorize_work, nullptr);
    EXPECT_EQ(session_authorize_work_result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    session_registry_expect_count(path, 1);
    unlink(path);
}

TEST_F(RdbRegistryTest, SqliteFileSessionClonePinsAnchorAcrossTeardown) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_session_clone_revoked.db";
    session_registry_database(path, true);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    RdbSqliteFileSession* session = rdb_sqlite_file_session_open(pool, path);
    ASSERT_NE(session, nullptr);
    CloneTeardownRace race = {};
    race.clone = rdb_sqlite_file_session_clone(pool, session, 1024 * 1024);
    ASSERT_NE(race.clone, nullptr);
    ASSERT_EQ(clone_registry.count, 2);
    ASSERT_EQ(uv_sem_init(&race.entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&race.resume, 0), 0);
    uv_thread_t worker;
    ASSERT_EQ(uv_thread_create(&worker, clone_teardown_worker, &race), 0);
    uv_sem_wait(&race.entered);
    rdb_conn_life_close(clone_registry.lives[0]);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_ERROR);
    uv_sem_post(&race.resume);
    ASSERT_EQ(uv_thread_join(&worker), 0);
    EXPECT_EQ(rdb_sqlite_clone_finish(race.clone), nullptr);
    EXPECT_EQ(rdb_sqlite_file_session_close(session), RDB_OK);
    uv_sem_destroy(&race.entered);
    uv_sem_destroy(&race.resume);
    unlink(path);
}
#endif

TEST_F(RdbRegistryTest, SqliteClonePinsBothConnectionsAcrossRegistryTeardown) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_clone_teardown.db";
    unlink(path);
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(path, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    CloneTeardownRace race = {};
    race.clone = rdb_sqlite_clone_prepare(pool, path, 1024 * 1024);
    ASSERT_NE(race.clone, nullptr);
    ASSERT_EQ(clone_registry.count, 2);
    ASSERT_EQ(uv_sem_init(&race.entered, 0), 0);
    ASSERT_EQ(uv_sem_init(&race.resume, 0), 0);
    uv_thread_t worker;
    ASSERT_EQ(uv_thread_create(&worker, clone_teardown_worker, &race), 0);
    uv_sem_wait(&race.entered);
    // Teardown closes both registry entries while the worker owns their pins.
    rdb_conn_life_close(clone_registry.lives[0]);
    rdb_conn_life_close(clone_registry.lives[1]);
    uv_sem_post(&race.resume);
    ASSERT_EQ(uv_thread_join(&worker), 0);
    EXPECT_EQ(rdb_sqlite_clone_finish(race.clone), nullptr);
    uv_sem_destroy(&race.entered);
    uv_sem_destroy(&race.resume);
    unlink(path);
}

struct SnapshotFileCheck { const char* path; bool contents_match; };
static int check_snapshot_file(void* user, const void* data, size_t size) {
    SnapshotFileCheck* check = (SnapshotFileCheck*)user;
    if (write_binary_file(check->path, (const char*)data, size) != 0) return RDB_ERROR;
    sqlite3* db = NULL;
    if (sqlite3_open(check->path, &db) != SQLITE_OK) { if (db) sqlite3_close(db); return RDB_ERROR; }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM sample", -1, &stmt, NULL) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 42)
        check->contents_match = true;
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return check->contents_match ? RDB_OK : RDB_ERROR;
}

TEST_F(RdbTest, SqliteCloneIncludesUncheckpointedWalData) {
    sqlite3* writer = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &writer), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(writer, "PRAGMA journal_mode=WAL; CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES (42)", NULL, NULL, NULL), SQLITE_OK);
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    SnapshotFileCheck check = {"temp/test_rdb_clone_wal_snapshot.db", false};
    unlink(check.path);
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot_file, &check), RDB_OK);
    EXPECT_TRUE(check.contents_match);
    EXPECT_EQ(sqlite3_close(writer), SQLITE_OK);
    unlink(check.path);
}

struct CloneConcurrentWriter {
    const char* path;
    uv_sem_t started;
    int result;
    int commits;
};

static void clone_concurrent_writer(void* data) {
    CloneConcurrentWriter* writer = (CloneConcurrentWriter*)data;
    sqlite3* db = NULL;
    writer->result = sqlite3_open(writer->path, &db);
    if (writer->result == SQLITE_OK) sqlite3_busy_timeout(db, 2000);
    for (int i = 0; writer->result == SQLITE_OK && i < 100; i++) {
        writer->result = sqlite3_exec(db,
            "BEGIN IMMEDIATE; UPDATE twin_a SET n=n+1; UPDATE twin_b SET n=n+1; COMMIT", NULL, NULL, NULL);
        if (writer->result == SQLITE_OK) writer->commits++;
        if (i == 0) uv_sem_post(&writer->started);
        uv_sleep(1);
    }
    if (!writer->commits) uv_sem_post(&writer->started);
    if (db) sqlite3_close(db);
}

TEST_F(RdbTest, SqliteCloneKeepsConcurrentTransactionsTogether) {
    sqlite3* db = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL; CREATE TABLE twin_a(n INTEGER); CREATE TABLE twin_b(n INTEGER); INSERT INTO twin_a VALUES(0); INSERT INTO twin_b VALUES(0)", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(db), SQLITE_OK);
    CloneConcurrentWriter writer = {};
    writer.path = TEST_DB_PATH;
    ASSERT_EQ(uv_sem_init(&writer.started, 0), 0);
    uv_thread_t thread;
    ASSERT_EQ(uv_thread_create(&thread, clone_concurrent_writer, &writer), 0);
    uv_sem_wait(&writer.started);
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    if (!clone) {
        uv_thread_join(&thread); uv_sem_destroy(&writer.started);
        FAIL() << "Could not prepare concurrent snapshot";
        return;
    }
    rdb_sqlite_clone_run(clone, NULL);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    if (!tx) {
        uv_thread_join(&thread); uv_sem_destroy(&writer.started);
        FAIL() << "Could not finish concurrent snapshot";
        return;
    }
    CloneCount check = {};
    EXPECT_EQ(tx_sql(tx, "SELECT (SELECT n FROM twin_a)=(SELECT n FROM twin_b)", NULL, 0, clone_count_row, &check), RDB_OK);
    EXPECT_EQ(check.count, 1);
    EXPECT_EQ(check.value, 1);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    ASSERT_EQ(uv_thread_join(&thread), 0);
    EXPECT_EQ(writer.result, SQLITE_OK);
    EXPECT_GT(writer.commits, 1);
    uv_sem_destroy(&writer.started);
}

TEST_F(RdbTest, SqlitePreMigrationImageRemainsIndependentOfLaterWrites) {
    sqlite3* source = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &source), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(source, "PRAGMA journal_mode=WAL; CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)", NULL, NULL, NULL), SQLITE_OK);
    sqlite3* memory = NULL;
    ASSERT_EQ(sqlite3_open(":memory:", &memory), SQLITE_OK);
    sqlite3_backup* copy = sqlite3_backup_init(memory, "main", source, "main");
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(sqlite3_backup_step(copy, -1), SQLITE_DONE);
    ASSERT_EQ(sqlite3_backup_finish(copy), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(memory, "BEGIN; SELECT * FROM sample", NULL, NULL, NULL), SQLITE_OK);
    sqlite3_int64 size = 0;
    unsigned char* image = sqlite3_serialize(memory, "main", &size, 0);
    ASSERT_NE(image, nullptr);
    ASSERT_GT(size, 0);
    const char* path = "temp/test_rdb_pre_migration.db";
    ASSERT_EQ(write_binary_file(path, (const char*)image, (size_t)size), 0);
    sqlite3_free(image);
    ASSERT_EQ(sqlite3_exec(memory, "INSERT INTO sample VALUES(7)", NULL, NULL, NULL), SQLITE_OK);
    sqlite3* backup = NULL;
    ASSERT_EQ(sqlite3_open(path, &backup), SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    ASSERT_EQ(sqlite3_prepare_v2(backup, "SELECT count(*), sum(value) FROM sample", -1, &stmt, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 1);
    EXPECT_EQ(sqlite3_column_int(stmt, 1), 42);
    sqlite3_finalize(stmt);
    sqlite3_close(backup);
    sqlite3_exec(memory, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(memory);
    sqlite3_close(source);
    unlink(path);
}

static bool create_preimage_source(void) {
    sqlite3* source = NULL;
    if (sqlite3_open(TEST_DB_PATH, &source) != SQLITE_OK) { if (source) sqlite3_close(source); return false; }
    bool ok = sqlite3_exec(source, "CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)", NULL, NULL, NULL) == SQLITE_OK;
    return sqlite3_close(source) == SQLITE_OK && ok;
}

static RdbSqliteTxn* open_preimage_clone(Pool* pool) {
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, TEST_DB_PATH, 1024 * 1024);
    if (!clone) return NULL;
    rdb_sqlite_clone_run(clone, NULL);
    return rdb_sqlite_clone_finish(clone);
}

TEST_F(RdbTest, SqliteClonePreimageRequiresAnUnchangedSnapshot) {
    ASSERT_TRUE(create_preimage_source());
    RdbSqliteTxn* tx = open_preimage_clone(pool);
    ASSERT_NE(tx, nullptr);
    CloneCount check = {};
    ASSERT_EQ(tx_sql(tx, "SELECT count(*) FROM sample", NULL, 0, clone_count_row, &check), RDB_OK);
    SnapshotFileCheck backup = {"temp/test_rdb_preimage.db", false};
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot_file, &backup), RDB_OK);
    EXPECT_TRUE(backup.contents_match);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(7)"), RDB_OK);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    unlink(backup.path);

    tx = open_preimage_clone(pool);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "INSERT INTO sample VALUES(7)"), RDB_OK);
    backup.contents_match = false;
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot_file, &backup), RDB_ERROR);
    EXPECT_FALSE(backup.contents_match);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbTest, SqliteClonePreimageFailurePoisonsTheScope) {
    ASSERT_TRUE(create_preimage_source());
    for (int outcome = -1; outcome <= 1; outcome += 2) {
        RdbSqliteTxn* tx = open_preimage_clone(pool);
        ASSERT_NE(tx, nullptr);
        SnapshotCheck check = {};
        check.outcome = outcome;
        EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot, &check), outcome);
        EXPECT_EQ(check.calls, 1);
        EXPECT_TRUE(check.contents_match);
        EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    }
}

struct PreimageReentry { RdbSqliteTxn* tx; int result; };
static int preimage_try_reentry(void* user, const void*, size_t) {
    PreimageReentry* attempt = (PreimageReentry*)user;
    attempt->result = tx_sql(attempt->tx, "INSERT INTO sample VALUES(7)");
    return RDB_OK;
}

TEST_F(RdbTest, SqliteClonePreimagePreventsPublicationReentry) {
    ASSERT_TRUE(create_preimage_source());
    RdbSqliteTxn* tx = open_preimage_clone(pool);
    ASSERT_NE(tx, nullptr);
    PreimageReentry attempt = {tx, RDB_OK};
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, preimage_try_reentry, &attempt), 1);
    EXPECT_EQ(attempt.result, RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbTest, SqliteClonePreimageRestoresSqlAuthorization) {
    ASSERT_TRUE(create_preimage_source());
    RdbSqliteTxn* tx = open_preimage_clone(pool);
    ASSERT_NE(tx, nullptr);
    SnapshotCheck check = {};
    ASSERT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot, &check), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "PRAGMA foreign_keys=OFF"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
}

TEST_F(RdbTest, SqliteClonePreimageIncludesUncheckpointedWalData) {
    sqlite3* writer = NULL;
    ASSERT_EQ(sqlite3_open(TEST_DB_PATH, &writer), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(writer, "PRAGMA journal_mode=WAL; CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)", NULL, NULL, NULL), SQLITE_OK);
    RdbSqliteTxn* tx = open_preimage_clone(pool);
    ASSERT_NE(tx, nullptr);
    SnapshotFileCheck backup = {"temp/test_rdb_preimage_wal.db", false};
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot_file, &backup), RDB_OK);
    EXPECT_TRUE(backup.contents_match);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    sqlite3_close(writer);
    unlink(backup.path);
}

TEST_F(RdbTest, SqliteExecuteAllowsStrictAlterMetadataButRejectsReturnedRows) {
    RdbSqliteTxn* tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE strict_parent(id INTEGER PRIMARY KEY) STRICT"), RDB_OK);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE strict_child(id INTEGER PRIMARY KEY) STRICT"), RDB_OK);
    EXPECT_EQ(tx_sql(tx, "ALTER TABLE strict_child ADD COLUMN parent_id INTEGER REFERENCES strict_parent(id)"), RDB_OK);
    ASSERT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_OK);
    tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "PRAGMA quick_check(strict_child)"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    tx = rdb_sqlite_tx_begin(pool, TEST_DB_PATH);
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx_sql(tx, "INSERT INTO strict_parent VALUES(1) RETURNING id"), RDB_ERROR);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    RdbConn* conn = rdb_open(pool, TEST_DB_PATH, "sqlite", true);
    ASSERT_NE(conn, nullptr);
    ASSERT_EQ(rdb_load_schema(conn), RDB_OK);
    EXPECT_EQ(rdb_row_count(conn, "strict_parent"), 0);
    rdb_close(conn);
}

TEST_F(RdbRegistryTest, SqliteClonePreimageReportsTeardownAfterPublication) {
    ensure_temp_dir();
    const char* path = "temp/test_rdb_preimage_teardown.db";
    unlink(path);
    sqlite3* source = NULL;
    ASSERT_EQ(sqlite3_open(path, &source), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(source, "CREATE TABLE sample(value INTEGER); INSERT INTO sample VALUES(42)", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(source), SQLITE_OK);
    memset(&clone_registry, 0, sizeof(clone_registry));
    rdb_set_registry_hooks(&clone_registry_hooks);
    RdbSqliteClone* clone = rdb_sqlite_clone_prepare(pool, path, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    RdbSqliteTxn* tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    SnapshotCheck check = {};
    check.close_during_publish = clone_registry.lives[1];
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot, &check), 1);
    EXPECT_TRUE(check.contents_match);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    // Registry authorization for the checked image can revoke the source before publication.
    memset(&clone_registry, 0, sizeof(clone_registry));
    clone = rdb_sqlite_clone_prepare(pool, path, 1024 * 1024);
    ASSERT_NE(clone, nullptr);
    rdb_sqlite_clone_run(clone, NULL);
    tx = rdb_sqlite_clone_finish(clone);
    ASSERT_NE(tx, nullptr);
    clone_registry.revoke_on_add = clone_registry.lives[1];
    SnapshotCheck revoked = {};
    EXPECT_EQ(rdb_sqlite_tx_preimage(tx, 1024 * 1024, check_snapshot, &revoked), RDB_ERROR);
    EXPECT_EQ(revoked.calls, 0);
    EXPECT_EQ(rdb_sqlite_tx_finish(tx, false), RDB_ERROR);
    memset(&clone_registry, 0, sizeof(clone_registry));
    tx = rdb_sqlite_tx_begin(pool, ":memory:");
    ASSERT_NE(tx, nullptr);
    ASSERT_EQ(tx_sql(tx, "CREATE TABLE sample (value INTEGER)"), RDB_OK);
    clone_registry.revoke_on_add = clone_registry.lives[0];
    SnapshotCheck final_revoked = {};
    EXPECT_EQ(rdb_sqlite_tx_finish_snapshot(tx, false, 1024 * 1024, check_snapshot, &final_revoked), RDB_ERROR);
    EXPECT_EQ(final_revoked.calls, 0);
    unlink(path);
}
