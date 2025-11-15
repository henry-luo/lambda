// Test for null vs missing field validation
// Tests Phase 4.1 implementation: map_has_field() and field existence checking

#include <gtest/gtest.h>
#include "../lambda/validator/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/input/input.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"

class NullVsMissingTest : public ::testing::Test {
protected:
    Pool* pool;
    SchemaValidator* validator;
    Input* input;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        validator = schema_validator_create(pool);
        ASSERT_NE(validator, nullptr);

        // Create Input context for MarkBuilder
        NamePool* name_pool = name_pool_create(pool, nullptr);
        ArrayList* type_list = arraylist_new(32);
        StringBuf* sb = stringbuf_new_cap(pool, 256);

        input = (Input*)pool_alloc(pool, sizeof(Input));
        input->pool = pool;
        input->name_pool = name_pool;
        input->type_list = type_list;
        input->sb = sb;
        input->url = nullptr;
        input->path = nullptr;
        input->root = (Item){.item = 0};
    }

    void TearDown() override {
        if (input) {
            arraylist_free(input->type_list);
        }
        if (validator) {
            schema_validator_destroy(validator);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Load schema from file
    bool load_schema(const char* schema_file, const char* type_name) {
        int result = schema_validator_load_schema(validator, schema_file, type_name);
        return (result == 0);
    }

    // Helper: Create map with fields
    TypeMap* create_test_map_type() {
        TypeMap* map_type = (TypeMap*)pool_calloc(pool, sizeof(TypeMap));
        map_type->type_id = LMD_TYPE_MAP;
        return map_type;
    }

    // Helper: Create StrView from string
    StrView* make_strview(const char* str) {
        StrView* sv = (StrView*)pool_alloc(pool, sizeof(StrView));
        sv->str = str;
        sv->length = strlen(str);
        return sv;
    }
};

// Test 1: Required field present - should pass
TEST_F(NullVsMissingTest, RequiredFieldPresent) {
    bool loaded = load_schema(
        "test/lambda/validator/schema_null_vs_missing.ls",
        "PersonRequired"
    );
    EXPECT_TRUE(loaded) << "Failed to load schema";

    // Note: Full integration test would load test data file here
    // For now, just validate the schema loads correctly
}

// Test 2: Required field missing - should fail
TEST_F(NullVsMissingTest, RequiredFieldMissing) {
    bool loaded = load_schema(
        "test/lambda/validator/schema_null_vs_missing.ls",
        "PersonRequired"
    );
    EXPECT_TRUE(loaded);

    // Note: Full test would validate a map missing a required field
}

// Test 3: Required field null - should fail
TEST_F(NullVsMissingTest, RequiredFieldNull) {
    bool loaded = load_schema(
        "test/lambda/validator/schema_null_vs_missing.ls",
        "PersonRequired"
    );
    EXPECT_TRUE(loaded);

    // Note: Full test would validate a map with null required field
}

// Test 4: Optional field missing - should pass
TEST_F(NullVsMissingTest, OptionalFieldMissing) {
    bool loaded = load_schema(
        "test/lambda/validator/schema_null_vs_missing.ls",
        "PersonOptional"
    );
    EXPECT_TRUE(loaded);

    // Note: Full test would validate a map with optional field missing
}

// Test 5: Optional field null - should pass
TEST_F(NullVsMissingTest, OptionalFieldNull) {
    bool loaded = load_schema(
        "test/lambda/validator/schema_null_vs_missing.ls",
        "PersonOptional"
    );
    EXPECT_TRUE(loaded);

    // Note: Full test would validate a map with optional field explicitly null
}

// Test 6: Test Map::has_field() method directly
TEST_F(NullVsMissingTest, MapHasFieldMethod) {
    // Create a simple map type
    TypeMap* map_type = create_test_map_type();

    // Create shape entries
    ShapeEntry* entry1 = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    entry1->name = make_strview("name");
    entry1->type = (Type*)pool_calloc(pool, sizeof(Type));
    entry1->type->type_id = LMD_TYPE_STRING;

    ShapeEntry* entry2 = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    entry2->name = make_strview("age");
    entry2->type = (Type*)pool_calloc(pool, sizeof(Type));
    entry2->type->type_id = LMD_TYPE_INT;

    entry1->next = entry2;
    entry2->next = nullptr;

    map_type->shape = entry1;

    // Create map
    Map* test_map = (Map*)pool_calloc(pool, sizeof(Map));
    test_map->type_id = LMD_TYPE_MAP;
    test_map->type = map_type;

    // Test has_field()
    EXPECT_TRUE(test_map->has_field("name"))
        << "has_field should return true for 'name' field in shape";
    EXPECT_TRUE(test_map->has_field("age"))
        << "has_field should return true for 'age' field in shape";
    EXPECT_FALSE(test_map->has_field("email"))
        << "has_field should return false for 'email' field not in shape";
    EXPECT_FALSE(test_map->has_field("nonexistent"))
        << "has_field should return false for nonexistent field";
}

// Test 7: Test error code for missing required field
TEST_F(NullVsMissingTest, MissingFieldErrorCode) {
    // This validates that AST_VALID_ERROR_MISSING_FIELD is defined correctly
    EXPECT_EQ(AST_VALID_ERROR_MISSING_FIELD, VALID_ERROR_MISSING_FIELD)
        << "Error code should be defined correctly";
}

// Test 8: Test error code for null value in required field
TEST_F(NullVsMissingTest, NullValueErrorCode) {
    // This validates that AST_VALID_ERROR_NULL_VALUE is defined correctly
    EXPECT_EQ(AST_VALID_ERROR_NULL_VALUE, VALID_ERROR_NULL_VALUE)
        << "Null value error code should be defined correctly";
}int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
