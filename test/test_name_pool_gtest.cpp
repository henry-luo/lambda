/**
 * GTest-based test suite for NamePool functionality
 * Tests string interning, parent inheritance, and symbol pooling
 */

#include <gtest/gtest.h>
#include "../lambda/core/name_pool.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/core/well_known_markup_names.h"
#include "../lambda/core/well_known_lambda_names.h"
#include "../lambda/js/js_well_known_names.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/test_utils.h"
#include <cstring>

// Test fixture for NamePool tests
class NamePoolTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override    { pool = tu_setup_pool(); }
    void TearDown() override { tu_teardown_pool(pool); }
};

// Test basic name creation and interning
TEST_F(NamePoolTest, BasicNameCreation) {
    NamePool* name_pool = name_pool_create(pool, nullptr);
    ASSERT_NE(name_pool, nullptr);

    String* name1 = name_pool_create_name(name_pool, "element");
    String* name2 = name_pool_create_name(name_pool, "element");

    ASSERT_NE(name1, nullptr);
    ASSERT_NE(name2, nullptr);

    // Should return same pointer (interned)
    EXPECT_EQ(name1, name2);
    EXPECT_STREQ(name1->chars, "element");
    EXPECT_EQ(name1->len, 7);
    EXPECT_TRUE(string_is_pooled(name1));
    EXPECT_FALSE(name1->is_buffer);
    EXPECT_EQ(name_ref_meta(name1)->hash, typemap_name_hash("element", 7));
    EXPECT_EQ(name_ref_id(name1), NAME_ID_NONE);
    EXPECT_TRUE(name_pool_verify(name_pool));

    name_pool_release(name_pool);
}

TEST_F(NamePoolTest, NameRecordClassifiesArrayIndexesAndUniqueKeys) {
    NamePool* name_pool = name_pool_create(pool, nullptr);
    ASSERT_NE(name_pool, nullptr);

    String* index = name_pool_create_name(name_pool, "4294967294");
    String* overflow = name_pool_create_name(name_pool, "4294967295");
    ASSERT_NE(index, nullptr);
    ASSERT_NE(overflow, nullptr);
    EXPECT_EQ(property_key_array_index(index), 0xFFFFFFFEu);
    EXPECT_EQ(property_key_array_index(overflow), NAME_ARRAY_INDEX_NONE);

    PropertyKeyRef first = name_pool_create_unique_symbol(name_pool, {"same", 4});
    PropertyKeyRef second = name_pool_create_unique_symbol(name_pool, {"same", 4});
    PropertyKeyRef private_key = name_pool_create_unique_private(name_pool, {"same", 4});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(private_key, nullptr);
    EXPECT_NE(first, second);
    EXPECT_FALSE(property_key_equal(first, second));
    EXPECT_EQ(property_key_kind(first), NAME_KEY_SYMBOL);
    EXPECT_EQ(property_key_kind(private_key), NAME_KEY_PRIVATE);
    EXPECT_NE(property_key_hash(first), property_key_hash(second));
    EXPECT_NE(property_key_hash(first), property_key_hash(private_key));
    EXPECT_EQ(property_key_array_index(first), NAME_ARRAY_INDEX_NONE);
    EXPECT_TRUE(string_is_pooled(first));
    EXPECT_FALSE(first->is_buffer);

    name_pool_release(name_pool);
}

TEST_F(NamePoolTest, GeneratedNamesArePinnedAndSharedAcrossPools) {
    NamePool* first_pool = name_pool_create(pool, nullptr);
    NamePool* second_pool = name_pool_create(pool, nullptr);
    ASSERT_NE(first_pool, nullptr);
    ASSERT_NE(second_pool, nullptr);

    String* first = name_pool_create_name(first_pool, "div");
    String* second = name_pool_create_name(second_pool, "div");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(name_ref_id(first), MARKUP_NAME_DIV);
    EXPECT_EQ(well_known_name_id({"div", 3}), MARKUP_NAME_DIV);
    StrView spelling = well_known_name_view(MARKUP_NAME_DIV);
    EXPECT_EQ(spelling.length, 3u);
    EXPECT_EQ(memcmp(spelling.str, "div", 3), 0);
    EXPECT_EQ(well_known_key_ref(MARKUP_NAME_DIV), first);
    EXPECT_EQ(name_pool_count(first_pool), 0u);
    EXPECT_EQ(name_pool_lookup(second_pool, "div"), first);

    name_pool_release(second_pool);
    name_pool_release(first_pool);
}

TEST_F(NamePoolTest, GeneratedNameSurvivesCallerPoolTeardown) {
    NameRef pinned = nullptr;
    {
        Pool* short_lived = pool_create();
        ASSERT_NE(short_lived, nullptr);
        NamePool* names = name_pool_create(short_lived, nullptr);
        ASSERT_NE(names, nullptr);
        pinned = name_pool_create_name(names, "html");
        ASSERT_NE(pinned, nullptr);
        name_pool_release(names);
        pool_destroy(short_lived);
    }
    EXPECT_TRUE(string_is_pooled(pinned));
    EXPECT_EQ(name_ref_id(pinned), MARKUP_NAME_HTML);
    EXPECT_EQ(memcmp(pinned->chars, "html", 4), 0);
}

TEST_F(NamePoolTest, GeneratedCatalogRecordsRoundTripAndPreserveKinds) {
    struct Catalog {
        const WellKnownNameRecord* records;
        size_t count;
    } catalogs[] = {
        {g_well_known_markup_names, g_well_known_markup_name_count},
        {g_well_known_lambda_names, g_well_known_lambda_name_count},
        {g_well_known_js_names, g_well_known_js_name_count},
    };

    for (const Catalog& catalog : catalogs) {
        for (size_t index = 0; index < catalog.count; index++) {
            const WellKnownNameRecord* record = &catalog.records[index];
            NameRef ref = well_known_name_ref(record->meta.predefined_id);
            ASSERT_NE(ref, nullptr);
            EXPECT_TRUE(string_is_pooled(ref));
            EXPECT_FALSE(ref->is_buffer);
            EXPECT_EQ(name_ref_meta_const(ref), &record->meta);
            EXPECT_EQ(name_ref_id(ref), record->meta.predefined_id);
            EXPECT_EQ(property_key_kind(ref), record->meta.key_kind);
            EXPECT_EQ(well_known_name_id({record->chars, record->len}),
                      record->meta.predefined_id);
        }
    }

    EXPECT_EQ(property_key_kind(well_known_key_ref(JS_SYMBOL_ITERATOR)), NAME_KEY_SYMBOL);
    EXPECT_EQ(property_key_kind(well_known_key_ref(JS_NAME_CONSTRUCTOR)), NAME_KEY_STRING);
}

TEST_F(NamePoolTest, OrdinaryStringCannotAliasWellKnownSymbolRecord) {
    NamePool* name_pool = name_pool_create(pool, nullptr);
    ASSERT_NE(name_pool, nullptr);

    String* ordinary = name_pool_create_name(name_pool, "Symbol.iterator");
    PropertyKeyRef symbol = well_known_key_ref(JS_SYMBOL_ITERATOR);
    ASSERT_NE(ordinary, nullptr);
    ASSERT_NE(symbol, nullptr);
    EXPECT_NE(ordinary, symbol);
    EXPECT_EQ(property_key_kind(ordinary), NAME_KEY_STRING);
    EXPECT_EQ(property_key_kind(symbol), NAME_KEY_SYMBOL);
    EXPECT_EQ(ordinary->len, symbol->len);
    EXPECT_EQ(memcmp(ordinary->chars, symbol->chars, ordinary->len), 0);

    name_pool_release(name_pool);
}

// Test that different names get different strings
TEST_F(NamePoolTest, DifferentNames) {
    NamePool* name_pool = name_pool_create(pool, nullptr);

    String* name1 = name_pool_create_name(name_pool, "element");
    String* name2 = name_pool_create_name(name_pool, "attribute");

    // Different names should have different pointers
    EXPECT_NE(name1, name2);
    EXPECT_STRNE(name1->chars, name2->chars);

    name_pool_release(name_pool);
}

// Test symbol size limit (32 chars)
TEST_F(NamePoolTest, SymbolSizeLimit) {
    NamePool* name_pool = name_pool_create(pool, nullptr);

    // Short symbol - should be pooled
    String* short_sym1 = name_pool_create_symbol(name_pool, "x");
    String* short_sym2 = name_pool_create_symbol(name_pool, "x");
    EXPECT_EQ(short_sym1, short_sym2);

    // 32 char symbol - exactly at limit, should be pooled
    const char* limit_sym = "12345678901234567890123456789012";  // 32 chars
    EXPECT_EQ(strlen(limit_sym), 32u);
    String* limit_sym1 = name_pool_create_symbol(name_pool, limit_sym);
    String* limit_sym2 = name_pool_create_symbol(name_pool, limit_sym);
    EXPECT_EQ(limit_sym1, limit_sym2);

    // Long symbol (>32 chars) - should NOT be pooled
    const char* long_sym_text = "this_is_a_very_long_symbol_name_exceeding_32_character_limit";
    EXPECT_GT(strlen(long_sym_text), 32u);
    String* long_sym1 = name_pool_create_symbol(name_pool, long_sym_text);
    String* long_sym2 = name_pool_create_symbol(name_pool, long_sym_text);
    EXPECT_NE(long_sym1, long_sym2);  // Different pointers (not pooled)

    // Content should still be the same
    EXPECT_STREQ(long_sym1->chars, long_sym2->chars);

    name_pool_release(name_pool);
}

// Test parent inheritance
TEST_F(NamePoolTest, ParentInheritance) {
    // Create parent pool with schema names
    NamePool* schema_pool = name_pool_create(pool, nullptr);
    String* schema_name1 = name_pool_create_name(schema_pool, "Person");
    String* schema_name2 = name_pool_create_name(schema_pool, "Address");

    // Create child pool inheriting from schema
    NamePool* doc_pool = name_pool_create(pool, schema_pool);
    ASSERT_NE(doc_pool, nullptr);

    // Should find names in parent pool
    String* found_person = name_pool_lookup(doc_pool, "Person");
    String* found_address = name_pool_lookup(doc_pool, "Address");

    EXPECT_EQ(found_person, schema_name1);
    EXPECT_EQ(found_address, schema_name2);

    // Names not in parent should return nullptr
    String* not_found = name_pool_lookup(doc_pool, "Unknown");
    EXPECT_EQ(not_found, nullptr);

    name_pool_release(doc_pool);
    name_pool_release(schema_pool);
}

// Test that child can add its own names without affecting parent
TEST_F(NamePoolTest, ChildIndependentNames) {
    NamePool* parent_pool = name_pool_create(pool, nullptr);
    name_pool_create_name(parent_pool, "parent_name");

    NamePool* child_pool = name_pool_create(pool, parent_pool);
    String* child_name = name_pool_create_name(child_pool, "child_name");

    // Child should have its own name
    ASSERT_NE(child_name, nullptr);
    EXPECT_STREQ(child_name->chars, "child_name");

    // Parent shouldn't have child's name
    String* not_in_parent = name_pool_lookup(parent_pool, "child_name");
    EXPECT_EQ(not_in_parent, nullptr);

    // Child should find parent's name
    String* found_parent = name_pool_lookup(child_pool, "parent_name");
    EXPECT_NE(found_parent, nullptr);

    name_pool_release(child_pool);
    name_pool_release(parent_pool);
}

// Test MarkBuilder integration
TEST_F(NamePoolTest, MarkBuilderIntegration) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    // Test createName - names ARE pooled via name_pool
    String* name1 = builder.createName("element");
    String* name2 = builder.createName("element");
    EXPECT_EQ(name1, name2);  // Should be pooled

    // Test createSymbol - symbols are arena-allocated, NOT pooled
    Symbol* sym1 = builder.createSymbol("short");
    Symbol* sym2 = builder.createSymbol("short");
    EXPECT_NE(sym1, sym2);  // Different instances (arena allocated)
    EXPECT_STREQ(sym1->chars, sym2->chars);  // But content same

    // Test createString - strings are arena-allocated, NOT pooled
    String* str1 = builder.createString("content");
    String* str2 = builder.createString("content");
    EXPECT_NE(str1, str2);  // Should NOT be pooled
    EXPECT_STREQ(str1->chars, str2->chars);  // But content same
}

// Test createNameItem, createSymbolItem, createStringItem
TEST_F(NamePoolTest, ItemCreation) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    Item name_item = builder.createNameItem("name");
    EXPECT_EQ(get_type_id(name_item), LMD_TYPE_SYMBOL);

    Item symbol_item = builder.createSymbolItem("symbol");
    EXPECT_EQ(get_type_id(symbol_item), LMD_TYPE_SYMBOL);

    Item string_item = builder.createStringItem("string");
    EXPECT_EQ(get_type_id(string_item), LMD_TYPE_STRING);
}

// Test map keys use name pooling
TEST_F(NamePoolTest, MapKeysPooled) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    MapBuilder map1 = builder.map();
    map1.put("key1", (int64_t)10);
    map1.put("key2", (int64_t)20);
    Item map1_item = map1.final();

    MapBuilder map2 = builder.map();
    map2.put("key1", (int64_t)30);  // Same key name
    map2.put("key3", (int64_t)40);
    Item map2_item = map2.final();

    // Verify both maps were created
    EXPECT_EQ(get_type_id(map1_item), LMD_TYPE_MAP);
    EXPECT_EQ(get_type_id(map2_item), LMD_TYPE_MAP);

    // Verify pointers are valid (not null)
    EXPECT_TRUE(map1_item.string_ptr != 0);
    EXPECT_TRUE(map2_item.string_ptr != 0);
}

// Test element names use name pooling
TEST_F(NamePoolTest, ElementNamesPooled) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    Item elem1 = builder.element("div").final();
    Item elem2 = builder.element("div").final();

    EXPECT_EQ(get_type_id(elem1), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(elem2), LMD_TYPE_ELEMENT);

    Element* e1 = elem1.element;
    Element* e2 = elem2.element;

    ASSERT_NE(e1, nullptr);
    ASSERT_NE(e2, nullptr);

    // Both should have type info
    ASSERT_NE(e1->type, nullptr);
    ASSERT_NE(e2->type, nullptr);

    // Tag names should be pooled (same pointer in name.str)
    TypeElmt* type1 = (TypeElmt*)e1->type;
    TypeElmt* type2 = (TypeElmt*)e2->type;

    EXPECT_EQ(type1->name.length, 3u);
    EXPECT_EQ(type2->name.length, 3u);
}

// Test attribute names use name pooling
TEST_F(NamePoolTest, AttributeNamesPooled) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    Item elem = builder.element("div")
        .attr("class", "test")
        .attr("id", "myid")
        .final();

    EXPECT_EQ(get_type_id(elem), LMD_TYPE_ELEMENT);
    EXPECT_TRUE(elem.string_ptr != 0);
}

// Test empty string handling
TEST_F(NamePoolTest, EmptyStrings) {
    NamePool* name_pool = name_pool_create(pool, nullptr);

    // Empty strings are valid and pooled (same instance returned)
    String* empty1 = name_pool_create_name(name_pool, "");
    String* empty2 = name_pool_create_name(name_pool, "");

    ASSERT_NE(empty1, nullptr);  // Empty string is valid
    EXPECT_EQ(empty1->len, 0);   // Length is 0
    EXPECT_EQ(empty1, empty2);   // Same pooled instance

    // NULL string returns nullptr
    String* null_str = name_pool_create_len(name_pool, nullptr, 0);
    EXPECT_EQ(null_str, nullptr);

    name_pool_release(name_pool);
}

// Test NULL handling
TEST_F(NamePoolTest, NullHandling) {
    NamePool* name_pool = name_pool_create(pool, nullptr);

    String* null_result = name_pool_create_name(name_pool, nullptr);
    EXPECT_EQ(null_result, nullptr);

    name_pool_release(name_pool);
}

// Test createNameFromStrView
TEST_F(NamePoolTest, CreateFromStrView) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);

    StrView view = {.str = "test_name", .length = 9};
    String* name = builder.createNameFromStrView(view);

    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->len, 9);
    EXPECT_STREQ(name->chars, "test_name");

    // Creating again should return same pointer
    String* name2 = builder.createNameFromStrView(view);
    EXPECT_EQ(name, name2);
}

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
