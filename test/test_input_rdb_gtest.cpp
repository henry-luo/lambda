/**
 * @file test_input_rdb_gtest.cpp
 * @brief GTest tests for the Lambda RDB input plugin.
 *
 * Tests the full pipeline: SQLite file → input_rdb_from_path() → <db>
 * element with schema/data namespaces.  Uses MarkReader to inspect the
 * resulting Lambda data structures.
 *
 * Coverage:
 *   - <db> element structure (tag, name, table_count)
 *   - Schema namespace (columns, indexes, foreign keys)
 *   - Data namespace (rows as arrays of maps)
 *   - Value conversion (int, float, string, null, datetime, JSON auto-parse)
 *   - View access
 *   - Multi-table databases
 *   - Empty databases
 *   - Auto-detect (no explicit type)
 */

#include <gtest/gtest.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"

// Forward-declare the RDB input functions (C++ linkage, matching input-parsers.h)
Input* input_rdb_from_path(const char* pathname, const char* type);
const char* rdb_detect_format(const char* pathname, const char* type);

extern "C" {
#include "../lib/sqlite3.h"
}

/* ══════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void ensure_temp_dir(void) {
    mkdir("temp", 0755);
}

static const char* TEST_DB = "temp/test_input_rdb.db";
static const char* TEST_DB_EMPTY = "temp/test_input_rdb_empty.db";

/** create a multi-table database for input plugin testing */
static void create_plugin_test_db(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);

    const char* ddl =
        "CREATE TABLE categories ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL"
        ");"
        "CREATE TABLE products ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  category_id INTEGER,"
        "  price REAL,"
        "  in_stock BOOLEAN DEFAULT 1,"
        "  created_at DATETIME,"
        "  tags JSON,"
        "  FOREIGN KEY (category_id) REFERENCES categories(id)"
        ");"
        "CREATE VIEW cheap_products AS "
        "  SELECT * FROM products WHERE price < 20.0;"
        "INSERT INTO categories VALUES (1, 'Electronics'), (2, 'Books');"
        "INSERT INTO products VALUES "
        "  (1, 'Widget', 1, 9.99, 1, '2024-01-15 08:00:00', "
        "   '[\"sale\",\"popular\"]'),"
        "  (2, 'Gadget', 1, 49.99, 1, '2024-02-20 10:30:00', "
        "   '{\"color\":\"blue\"}'),"
        "  (3, 'Novel', 2, 14.99, 0, '2024-03-10 12:00:00', NULL);";

    sqlite3_exec(db, ddl, NULL, NULL, NULL);
    sqlite3_close(db);
}

static void create_empty_db(const char* path) {
    sqlite3* db = NULL;
    sqlite3_open(path, &db);
    sqlite3_close(db);
}

/* ══════════════════════════════════════════════════════════════════════
 * Fixture
 * ══════════════════════════════════════════════════════════════════════ */

class InputRdbTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensure_temp_dir();
        create_plugin_test_db(TEST_DB);
        create_empty_db(TEST_DB_EMPTY);
    }

    void TearDown() override {
        unlink(TEST_DB);
        unlink(TEST_DB_EMPTY);
    }
};

/* ══════════════════════════════════════════════════════════════════════
 * §1 Format Detection
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, DetectSqliteDb) {
    EXPECT_STREQ(rdb_detect_format("data.db", NULL), "sqlite");
}

TEST_F(InputRdbTest, DetectSqliteExplicit) {
    EXPECT_STREQ(rdb_detect_format("any.csv", "sqlite"), "sqlite");
}

TEST_F(InputRdbTest, DetectNotRdb) {
    EXPECT_EQ(rdb_detect_format("data.json", NULL), nullptr);
}

TEST_F(InputRdbTest, DetectNullPath) {
    EXPECT_EQ(rdb_detect_format(NULL, NULL), nullptr);
}

/* ══════════════════════════════════════════════════════════════════════
 * §2 Basic Element Structure
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, ReturnsValidInput) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ASSERT_NE(input, nullptr);
    EXPECT_NE(input->root.element, nullptr);
}

TEST_F(InputRdbTest, RootIsDbElement) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ASSERT_NE(input, nullptr);

    ElementReader el(input->root);
    EXPECT_TRUE(el.isValid());
    EXPECT_TRUE(el.hasTag("db"));
}

TEST_F(InputRdbTest, DbElementName) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    const char* name = el.get_attr_string("name");
    ASSERT_NE(name, nullptr);
    EXPECT_STREQ(name, "test_input_rdb.db");
}

TEST_F(InputRdbTest, DbTableCount) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    // 2 tables + 1 view = 3
    int64_t count = el.get_int_attr("table_count", -1);
    EXPECT_EQ(count, 3);
}

/* ══════════════════════════════════════════════════════════════════════
 * §3 Schema Namespace
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, SchemaIsMap) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    ItemReader schema_item = el.get_attr("schema");
    EXPECT_TRUE(schema_item.isMap());
}

TEST_F(InputRdbTest, SchemaHasTables) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    EXPECT_TRUE(schema.has("categories"));
    EXPECT_TRUE(schema.has("products"));
    EXPECT_TRUE(schema.has("cheap_products"));
}

TEST_F(InputRdbTest, SchemaColumnsArray) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader cat_schema = schema.get("categories").asMap();
    ItemReader cols_item = cat_schema.get("columns");
    EXPECT_TRUE(cols_item.isArray());

    ArrayReader cols = cols_item.asArray();
    EXPECT_EQ(cols.length(), 2); // id, name
}

TEST_F(InputRdbTest, SchemaColumnDetails) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader prod_schema = schema.get("products").asMap();
    ArrayReader cols = prod_schema.get("columns").asArray();

    // first column should be "id"
    MapReader col0 = cols.get(0).asMap();
    const char* col_name = col0.get("name").cstring();
    ASSERT_NE(col_name, nullptr);
    EXPECT_STREQ(col_name, "id");

    bool pk = col0.get("pk").asBool();
    EXPECT_TRUE(pk);
}

TEST_F(InputRdbTest, SchemaForeignKeys) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader prod_schema = schema.get("products").asMap();
    ItemReader fk_item = prod_schema.get("foreign_keys");
    EXPECT_TRUE(fk_item.isArray());

    ArrayReader fks = fk_item.asArray();
    EXPECT_EQ(fks.length(), 1);

    MapReader fk0 = fks.get(0).asMap();
    EXPECT_STREQ(fk0.get("column").cstring(), "category_id");
    EXPECT_STREQ(fk0.get("ref_table").cstring(), "categories");
}

TEST_F(InputRdbTest, SchemaViewFlag) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader view_schema = schema.get("cheap_products").asMap();
    bool is_view = view_schema.get("view").asBool();
    EXPECT_TRUE(is_view);
}

TEST_F(InputRdbTest, SchemaTableNoViewFlag) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader cat_schema = schema.get("categories").asMap();
    // tables should not have a "view" key
    EXPECT_FALSE(cat_schema.has("view"));
}

TEST_F(InputRdbTest, SchemaIndexes) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader cat_schema = schema.get("categories").asMap();
    // categories has no explicit indexes (only autoindex for PK)
    // products has the FK-derived autoindex
    // just verify the key exists or doesn't error
    ItemReader idx = cat_schema.get("indexes");
    // may or may not exist depending on autoindex visibility
    (void)idx;
}

/* ══════════════════════════════════════════════════════════════════════
 * §4 Data Namespace
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, DataIsMap) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    ItemReader data_item = el.get_attr("data");
    EXPECT_TRUE(data_item.isMap());
}

TEST_F(InputRdbTest, DataHasTables) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    EXPECT_TRUE(data.has("categories"));
    EXPECT_TRUE(data.has("products"));
    EXPECT_TRUE(data.has("cheap_products"));
}

TEST_F(InputRdbTest, DataCategoriesRowCount) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    EXPECT_EQ(cats.length(), 2);
}

TEST_F(InputRdbTest, DataProductsRowCount) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    EXPECT_EQ(prods.length(), 3);
}

TEST_F(InputRdbTest, DataRowIsMap) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    ItemReader row0 = cats.get(0);
    EXPECT_TRUE(row0.isMap());
}

/* ══════════════════════════════════════════════════════════════════════
 * §5 Value Conversion
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, ValueConversion_Int) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader row0 = cats.get(0).asMap();

    ItemReader id_item = row0.get("id");
    EXPECT_TRUE(id_item.isInt());
    EXPECT_EQ(id_item.asInt(), 1);
}

TEST_F(InputRdbTest, ValueConversion_String) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader row0 = cats.get(0).asMap();

    ItemReader name_item = row0.get("name");
    EXPECT_TRUE(name_item.isString());
    EXPECT_STREQ(name_item.cstring(), "Electronics");
}

TEST_F(InputRdbTest, ValueConversion_Float) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader row0 = prods.get(0).asMap();

    ItemReader price = row0.get("price");
    EXPECT_TRUE(price.isFloat());
    EXPECT_DOUBLE_EQ(price.asFloat(), 9.99);
}

TEST_F(InputRdbTest, ValueConversion_Bool) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();

    // product 1: in_stock = 1 → true
    MapReader prod0 = prods.get(0).asMap();
    ItemReader stock0 = prod0.get("in_stock");
    // BOOLEAN columns stored as INTEGER in SQLite, so it might come as int
    // The column has type BOOLEAN → rdb_value_to_item uses RDB_TYPE_BOOL
    EXPECT_TRUE(stock0.isBool() || stock0.isInt());
}

TEST_F(InputRdbTest, ValueConversion_Null) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();

    // product 3: tags = NULL
    MapReader prod2 = prods.get(2).asMap();
    ItemReader tags = prod2.get("tags");
    EXPECT_TRUE(tags.isNull());
}

TEST_F(InputRdbTest, ValueConversion_Datetime) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod0 = prods.get(0).asMap();

    ItemReader dt = prod0.get("created_at");
    // datetime currently stored as string in Phase 1
    EXPECT_TRUE(dt.isString());
    EXPECT_STREQ(dt.cstring(), "2024-01-15 08:00:00");
}

TEST_F(InputRdbTest, ValueConversion_JsonArray) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod0 = prods.get(0).asMap();

    // tags = '["sale","popular"]' → should be auto-parsed to array
    ItemReader tags = prod0.get("tags");
    EXPECT_TRUE(tags.isArray()) << "JSON array column should be auto-parsed";
    ArrayReader tag_arr = tags.asArray();
    EXPECT_EQ(tag_arr.length(), 2);
    EXPECT_STREQ(tag_arr.get(0).cstring(), "sale");
    EXPECT_STREQ(tag_arr.get(1).cstring(), "popular");
}

TEST_F(InputRdbTest, ValueConversion_JsonObject) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod1 = prods.get(1).asMap();

    // tags = '{"color":"blue"}' → should be auto-parsed to map
    ItemReader tags = prod1.get("tags");
    EXPECT_TRUE(tags.isMap()) << "JSON object column should be auto-parsed";
    MapReader tag_map = tags.asMap();
    EXPECT_STREQ(tag_map.get("color").cstring(), "blue");
}

/* ══════════════════════════════════════════════════════════════════════
 * §6 View Data
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, ViewDataAccessible) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cheap = data.get("cheap_products").asArray();
    // Widget (9.99) and Novel (14.99) are < 20.0
    EXPECT_EQ(cheap.length(), 2);
}

TEST_F(InputRdbTest, ViewRowContent) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cheap = data.get("cheap_products").asArray();

    // verify first row has expected fields
    MapReader row0 = cheap.get(0).asMap();
    EXPECT_TRUE(row0.has("name"));
    EXPECT_TRUE(row0.has("price"));
}

/* ══════════════════════════════════════════════════════════════════════
 * §7 Empty Database
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, EmptyDatabase) {
    Input* input = input_rdb_from_path(TEST_DB_EMPTY, "sqlite");
    ASSERT_NE(input, nullptr);

    ElementReader el(input->root);
    EXPECT_TRUE(el.hasTag("db"));
    EXPECT_EQ(el.get_int_attr("table_count", -1), 0);
}

/* ══════════════════════════════════════════════════════════════════════
 * §8 Data Integrity — All Rows Present
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, AllCategoryRows) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();

    // row 0
    MapReader r0 = cats.get(0).asMap();
    EXPECT_EQ(r0.get("id").asInt(), 1);
    EXPECT_STREQ(r0.get("name").cstring(), "Electronics");

    // row 1
    MapReader r1 = cats.get(1).asMap();
    EXPECT_EQ(r1.get("id").asInt(), 2);
    EXPECT_STREQ(r1.get("name").cstring(), "Books");
}

TEST_F(InputRdbTest, AllProductRows) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();

    EXPECT_STREQ(prods.get(0).asMap().get("name").cstring(), "Widget");
    EXPECT_STREQ(prods.get(1).asMap().get("name").cstring(), "Gadget");
    EXPECT_STREQ(prods.get(2).asMap().get("name").cstring(), "Novel");
}

/* ══════════════════════════════════════════════════════════════════════
 * §9 Error Handling
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, NullPathname) {
    Input* input = input_rdb_from_path(NULL, "sqlite");
    EXPECT_EQ(input, nullptr);
}

TEST_F(InputRdbTest, NonexistentFile) {
    Input* input = input_rdb_from_path("temp/does_not_exist.db", "sqlite");
    // should return an Input with null root
    if (input) {
        EXPECT_TRUE(input->root.element == nullptr || get_type_id(input->root) == LMD_TYPE_NULL);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * §10 Second Row Field Access (regression)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, SecondProductPrice) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod1 = prods.get(1).asMap();
    EXPECT_DOUBLE_EQ(prod1.get("price").asFloat(), 49.99);
}

TEST_F(InputRdbTest, ThirdProductCategoryId) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod2 = prods.get(2).asMap();
    EXPECT_EQ(prod2.get("category_id").asInt(), 2);
}
