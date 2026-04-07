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
 *   - Value conversion (int, float, decimal, string, null, datetime, JSON auto-parse)
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
#include "../lambda/lambda-decimal.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"

// Forward-declare the RDB input functions (C++ linkage, matching input-parsers.h)
Input* input_rdb_from_path(const char* pathname, const char* type);
const char* rdb_detect_format(const char* pathname, const char* type);

extern "C" {
#include "../lib/sqlite/sqlite3.h"
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
        "  weight DECIMAL(10,3),"
        "  in_stock BOOLEAN DEFAULT 1,"
        "  created_at DATETIME,"
        "  tags JSON,"
        "  FOREIGN KEY (category_id) REFERENCES categories(id)"
        ");"
        "CREATE VIEW cheap_products AS "
        "  SELECT * FROM products WHERE price < 20.0;"
        "CREATE INDEX idx_products_name ON products(name);"
        "CREATE TRIGGER trg_products_before_insert BEFORE INSERT ON products BEGIN SELECT 1; END;"
        "CREATE TRIGGER trg_products_after_delete AFTER DELETE ON products BEGIN SELECT 1; END;"
        "INSERT INTO categories VALUES (1, 'Electronics'), (2, 'Books');"
        "INSERT INTO products VALUES "
        "  (1, 'Widget', 1, 9.99, 0.250, 1, '2024-01-15 08:00:00', "
        "   '[\"sale\",\"popular\"]'),"
        "  (2, 'Gadget', 1, 49.99, 1.500, 1, '2024-02-20 10:30:00', "
        "   '{\"color\":\"blue\"}'),"
        "  (3, 'Novel', 2, 14.99, NULL, 0, '2024-03-10 12:00:00', NULL),"
        "  (4, 'Orphan', NULL, 5.99, NULL, 1, NULL, NULL);";

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

TEST_F(InputRdbTest, DbTableNames) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    ItemReader names_item = el.get_attr("table_names");
    EXPECT_TRUE(names_item.isArray());
    ArrayReader names = names_item.asArray();
    EXPECT_EQ(names.length(), 3);
    // sorted alphabetically (SQLite ORDER BY name)
    EXPECT_STREQ(names.get(0).cstring(), "categories");
    EXPECT_STREQ(names.get(1).cstring(), "cheap_products");
    EXPECT_STREQ(names.get(2).cstring(), "products");
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
    MapReader prod_schema = schema.get("products").asMap();
    // products has idx_products_name
    ItemReader idx_item = prod_schema.get("indexes");
    EXPECT_TRUE(idx_item.isArray());
    ArrayReader idxs = idx_item.asArray();
    EXPECT_GE(idxs.length(), 1);

    // find idx_products_name
    bool found = false;
    for (int i = 0; i < (int)idxs.length(); i++) {
        MapReader idx = idxs.get(i).asMap();
        if (idx.get("name").cstring() &&
            strcmp(idx.get("name").cstring(), "idx_products_name") == 0) {
            EXPECT_FALSE(idx.get("unique").asBool());
            found = true;
        }
    }
    EXPECT_TRUE(found) << "idx_products_name not found";

    // categories has no explicit named indexes
    MapReader cat_schema = schema.get("categories").asMap();
    EXPECT_FALSE(cat_schema.has("indexes"));
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
    EXPECT_EQ(prods.length(), 4);  // Widget, Gadget, Novel, Orphan(null FK)
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
    EXPECT_TRUE(dt.isDatetime());
    DateTime dtime = dt.asDatetime();
    EXPECT_EQ(DATETIME_GET_YEAR(&dtime), 2024);
    EXPECT_EQ(DATETIME_GET_MONTH(&dtime), 1u);
    EXPECT_EQ(dtime.day, 15);
    EXPECT_EQ(dtime.hour, 8);
    EXPECT_EQ(dtime.minute, 0);
    EXPECT_EQ(dtime.second, 0);
}

TEST_F(InputRdbTest, ValueConversion_Decimal) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();

    MapReader prod0 = prods.get(0).asMap();
    ItemReader weight0 = prod0.get("weight");
    EXPECT_EQ(weight0.getType(), LMD_TYPE_DECIMAL);
    char* weight0_str = decimal_to_string(weight0.item());
    ASSERT_NE(weight0_str, nullptr);
    EXPECT_STREQ(weight0_str, "0.25");
    decimal_free_string(weight0_str);

    MapReader prod2 = prods.get(2).asMap();
    ItemReader weight2 = prod2.get("weight");
    EXPECT_TRUE(weight2.isNull());
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
    // Widget (9.99), Orphan (5.99), and Novel (14.99) are < 20.0
    EXPECT_EQ(cheap.length(), 3);
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

/* ══════════════════════════════════════════════════════════════════════
 * §11 FK Forward Navigation (product → category)
 * ══════════════════════════════════════════════════════════════════════ */

// product row has a "category" attribute (link_name from stripped _id suffix)
TEST_F(InputRdbTest, FkForward_ProductHasCategoryAttr) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod0 = prods.get(0).asMap();
    EXPECT_TRUE(prod0.has("category"));
}

// product 0 (Widget, category_id=1) → category.name == "Electronics"
TEST_F(InputRdbTest, FkForward_WidgetCategory) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod0 = prods.get(0).asMap();

    ItemReader cat_item = prod0.get("category");
    EXPECT_TRUE(cat_item.isMap());

    MapReader cat = cat_item.asMap();
    EXPECT_STREQ(cat.get("name").cstring(), "Electronics");
    EXPECT_EQ(cat.get("id").asInt(), 1);
}

// product 1 (Gadget, category_id=1) → same category "Electronics"
TEST_F(InputRdbTest, FkForward_GadgetCategory) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod1 = prods.get(1).asMap();

    MapReader cat = prod1.get("category").asMap();
    EXPECT_STREQ(cat.get("name").cstring(), "Electronics");
}

// product 2 (Novel, category_id=2) → category "Books"
TEST_F(InputRdbTest, FkForward_NovelCategory) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod2 = prods.get(2).asMap();

    MapReader cat = prod2.get("category").asMap();
    EXPECT_STREQ(cat.get("name").cstring(), "Books");
    EXPECT_EQ(cat.get("id").asInt(), 2);
}

// original FK column is still present alongside the navigation attribute
TEST_F(InputRdbTest, FkForward_OriginalColumnPreserved) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();
    MapReader prod0 = prods.get(0).asMap();

    // category_id column still present
    EXPECT_EQ(prod0.get("category_id").asInt(), 1);
    // and the FK navigation attribute too
    EXPECT_TRUE(prod0.has("category"));
}

/* ══════════════════════════════════════════════════════════════════════
 * §12 FK Reverse Navigation (category → products[])
 * ══════════════════════════════════════════════════════════════════════ */

// category row has a "products" attribute (reverse FK array)
TEST_F(InputRdbTest, FkReverse_CategoryHasProductsAttr) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader cat0 = cats.get(0).asMap();
    EXPECT_TRUE(cat0.has("products"));
}

// Electronics (id=1) has 2 products: Widget and Gadget
TEST_F(InputRdbTest, FkReverse_ElectronicsProducts) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader cat0 = cats.get(0).asMap();

    EXPECT_STREQ(cat0.get("name").cstring(), "Electronics");

    ItemReader prods_item = cat0.get("products");
    EXPECT_TRUE(prods_item.isArray());

    ArrayReader prods = prods_item.asArray();
    EXPECT_EQ(prods.length(), 2);

    // verify product names (Widget and Gadget, category_id=1)
    MapReader p0 = prods.get(0).asMap();
    MapReader p1 = prods.get(1).asMap();
    EXPECT_STREQ(p0.get("name").cstring(), "Widget");
    EXPECT_STREQ(p1.get("name").cstring(), "Gadget");
}

// Books (id=2) has 1 product: Novel
TEST_F(InputRdbTest, FkReverse_BooksProducts) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader cat1 = cats.get(1).asMap();

    EXPECT_STREQ(cat1.get("name").cstring(), "Books");

    ArrayReader prods = cat1.get("products").asArray();
    EXPECT_EQ(prods.length(), 1);
    EXPECT_STREQ(prods.get(0).asMap().get("name").cstring(), "Novel");
}

// category original columns are preserved after reverse FK rebuild
TEST_F(InputRdbTest, FkReverse_OriginalColumnsPreserved) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();
    MapReader cat0 = cats.get(0).asMap();

    EXPECT_EQ(cat0.get("id").asInt(), 1);
    EXPECT_STREQ(cat0.get("name").cstring(), "Electronics");
}

/* ══════════════════════════════════════════════════════════════════════
 * §13 Null FK Handling
 * ══════════════════════════════════════════════════════════════════════ */

// product with NULL category_id has null for the forward FK navigation attr
TEST_F(InputRdbTest, FkForward_NullFkIsNull) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader prods = data.get("products").asArray();

    // product[3] is Orphan with category_id = NULL
    MapReader orphan = prods.get(3).asMap();
    EXPECT_STREQ(orphan.get("name").cstring(), "Orphan");
    EXPECT_TRUE(orphan.get("category_id").isNull());
    EXPECT_TRUE(orphan.get("category").isNull());
}

// orphan product does NOT appear in any category's reverse FK products array
TEST_F(InputRdbTest, FkReverse_OrphanNotInAnyCategory) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader data = el.get_attr("data").asMap();
    ArrayReader cats = data.get("categories").asArray();

    // Electronics: Widget + Gadget (NOT Orphan)
    ArrayReader elec_prods = cats.get(0).asMap().get("products").asArray();
    EXPECT_EQ(elec_prods.length(), 2);

    // Books: Novel only
    ArrayReader books_prods = cats.get(1).asMap().get("products").asArray();
    EXPECT_EQ(books_prods.length(), 1);
}

/* ══════════════════════════════════════════════════════════════════════
 * §14 Trigger Schema
 * ══════════════════════════════════════════════════════════════════════ */

// products table has 2 triggers
TEST_F(InputRdbTest, SchemaTriggerCount) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader prod_schema = schema.get("products").asMap();
    ItemReader trigs_item = prod_schema.get("triggers");
    EXPECT_TRUE(trigs_item.isArray());
    ArrayReader trigs = trigs_item.asArray();
    EXPECT_EQ(trigs.length(), 2);
}

TEST_F(InputRdbTest, SchemaTriggerDetails) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader prod_schema = schema.get("products").asMap();
    ArrayReader trigs = prod_schema.get("triggers").asArray();

    MapReader t0 = trigs.get(0).asMap();
    EXPECT_STREQ(t0.get("name").cstring(), "trg_products_before_insert");
    EXPECT_STREQ(t0.get("timing").cstring(), "BEFORE");
    EXPECT_STREQ(t0.get("event").cstring(), "INSERT");

    MapReader t1 = trigs.get(1).asMap();
    EXPECT_STREQ(t1.get("name").cstring(), "trg_products_after_delete");
    EXPECT_STREQ(t1.get("timing").cstring(), "AFTER");
    EXPECT_STREQ(t1.get("event").cstring(), "DELETE");
}

// categories table has no triggers
TEST_F(InputRdbTest, SchemaTriggerAbsent) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    MapReader schema = el.get_attr("schema").asMap();
    MapReader cat_schema = schema.get("categories").asMap();
    EXPECT_FALSE(cat_schema.has("triggers"));
}

/* ══════════════════════════════════════════════════════════════════════
 * §15 Function Schema (database-level)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputRdbTest, FunctionsPresent) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    ItemReader funcs_item = el.get_attr("functions");
    EXPECT_TRUE(funcs_item.isArray());
    ArrayReader funcs = funcs_item.asArray();
    EXPECT_GT(funcs.length(), 0);
}

TEST_F(InputRdbTest, FunctionDetails) {
    Input* input = input_rdb_from_path(TEST_DB, "sqlite");
    ElementReader el(input->root);
    ArrayReader funcs = el.get_attr("functions").asArray();

    // find 'abs' — a well-known scalar built-in
    bool found_abs = false;
    for (int i = 0; i < funcs.length(); i++) {
        MapReader fn = funcs.get(i).asMap();
        if (strcmp(fn.get("name").cstring(), "abs") == 0
            && fn.get("narg").asInt() == 1) {
            found_abs = true;
            EXPECT_STREQ(fn.get("type").cstring(), "scalar");
            EXPECT_TRUE(fn.get("builtin").asBool());
            break;
        }
    }
    EXPECT_TRUE(found_abs) << "expected 'abs' function in db.functions";
}
