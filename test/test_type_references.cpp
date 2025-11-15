/**
 * Test type references in validator
 */
#include <gtest/gtest.h>
#include "../lambda/validator/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/lambda.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/hashmap.h"

class TypeReferenceTest : public ::testing::Test {
protected:
    Pool* pool;
    SchemaValidator* validator;

    void SetUp() override {
        log_set_level(LOG_LEVEL_DEBUG);
        pool = pool_create();
        validator = schema_validator_create(pool);
        ASSERT_NE(validator, nullptr);
    }

    void TearDown() override {
        if (validator) {
            schema_validator_destroy(validator);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    Item create_string(const char* value) {
        size_t len = strlen(value);
        String* str = (String*)pool_calloc(pool, sizeof(String) + len + 1);
        str->len = len;
        strcpy(str->chars, value);

        Item item;
        item.pointer = str;
        item._type_id = LMD_TYPE_STRING;
        return item;
    }

    Item create_int(int value) {
        Item item;
        item.int_val = value;
        item._type_id = LMD_TYPE_INT;
        return item;
    }

    Item create_map() {
        Map* map = (Map*)pool_calloc(pool, sizeof(Map));
        map->data = hashmap_new(sizeof(MapField), 0, 0, 0,
                                map_field_hash, map_field_compare, nullptr, nullptr);
        Item item;
        item.pointer = map;
        item._type_id = LMD_TYPE_MAP;
        return item;
    }

    void add_map_field(Map* map, const char* key, Item value) {
        MapField entry;
        entry.key = create_string(key);
        entry.value = value;
        hashmap_set(map->data, &entry);
    }

    ValidationResult* validate_by_name(Item item, const char* type_name) {
        Type* type = schema_validator_find_type(validator, type_name);
        if (!type) {
            ValidationResult* result = create_validation_result(pool);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Type not found: %s", type_name);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, nullptr, pool));
            return result;
        }
        return schema_validator_validate_type(validator, item.to_const(), type);
    }
};

TEST_F(TypeReferenceTest, SimpleTypeAlias) {
    // Load schema with simple type alias
    const char* schema = R"(
        type Username = string
    )";

    int result = schema_validator_load_schema(validator, schema, "Username");
    ASSERT_EQ(result, 0);

    // Validate a string against Username type
    Item string_item = create_string("alice");
    ValidationResult* validation = schema_validator_validate_type(validator, string_item,
        schema_validator_find_type(validator, "Username"));

    ASSERT_NE(validation, nullptr);
    EXPECT_TRUE(validation->valid) << "Username (string alias) should validate string";
    EXPECT_EQ(validation->error_count, 0);
}

TEST_F(TypeReferenceTest, TypeAliasToInt) {
    const char* schema = R"(
        type Age = int
    )";

    int result = schema_validator_load_schema(validator, schema, "Age");
    ASSERT_EQ(result, 0);

    // Valid: int against Age
    Item int_item = create_int(30);
    ValidationResult* validation = validate_by_name( int_item, "Age");

    ASSERT_NE(validation, nullptr);
    EXPECT_TRUE(validation->valid) << "Age (int alias) should validate int";

    // Invalid: string against Age
    Item string_item = create_string("thirty");
    validation = validate_by_name( string_item, "Age");

    ASSERT_NE(validation, nullptr);
    EXPECT_FALSE(validation->valid) << "Age (int alias) should reject string";
    EXPECT_GT(validation->error_count, 0);
}

TEST_F(TypeReferenceTest, NestedTypeReferences) {
    const char* schema = R"(
        type Username = string
        type Age = int
        type User = {
            username: Username,
            age: Age
        }
    )";

    int result = schema_validator_load_schema(validator, schema, "User");
    ASSERT_EQ(result, 0);

    // Create valid user
    Item map_item = create_map();
    add_map_field(map_item.map, "username", create_string("alice"));
    add_map_field(map_item.map, "age", create_int(30));

    ValidationResult* validation = validate_by_name( map_item, "User");

    ASSERT_NE(validation, nullptr);
    EXPECT_TRUE(validation->valid) << "Valid User object should pass validation";
    EXPECT_EQ(validation->error_count, 0);
}

TEST_F(TypeReferenceTest, UndefinedTypeReference) {
    const char* schema = R"(
        type User = {
            name: string
        }
    )";

    int result = schema_validator_load_schema(validator, schema, "User");
    ASSERT_EQ(result, 0);

    // Try to validate against non-existent type
    Item string_item = create_string("test");
    ValidationResult* validation = validate_by_name( string_item, "NonExistent");

    ASSERT_NE(validation, nullptr);
    EXPECT_FALSE(validation->valid) << "Undefined type should fail validation";
    EXPECT_GT(validation->error_count, 0);

    // Check error message mentions the type
    ASSERT_NE(validation->errors, nullptr);
    EXPECT_NE(validation->errors->message, nullptr);
    if (validation->errors->message && validation->errors->message->chars) {
        std::string error_msg(validation->errors->message->chars);
        EXPECT_NE(error_msg.find("NonExistent"), std::string::npos)
            << "Error should mention the undefined type name";
    }
}

TEST_F(TypeReferenceTest, CircularTypeReference) {
    // This would create a circular reference: A -> B -> A
    // The transpiler might catch this during AST building, but if it doesn't,
    // the validator should detect it
    const char* schema = R"(
        type A = B
        type B = A
    )";

    // Schema loading might fail or succeed depending on transpiler behavior
    int result = schema_validator_load_schema(validator, schema, "A");

    if (result == 0) {
        // If schema loaded, validation should detect circular reference
        Item string_item = create_string("test");
        ValidationResult* validation = validate_by_name( string_item, "A");

        ASSERT_NE(validation, nullptr);
        EXPECT_FALSE(validation->valid) << "Circular type reference should be detected";

        if (!validation->valid && validation->errors && validation->errors->message) {
            std::string error_msg(validation->errors->message->chars);
            printf("Circular reference error: %s\n", error_msg.c_str());
        }
    } else {
        // Schema loading failed - this is acceptable for circular references
        printf("Schema loading rejected circular reference (expected)\n");
    }
}

TEST_F(TypeReferenceTest, ChainedTypeReferences) {
    const char* schema = R"(
        type A = string
        type B = A
        type C = B
        type D = C
    )";

    int result = schema_validator_load_schema(validator, schema, "D");
    ASSERT_EQ(result, 0);

    // Validate through the chain: D -> C -> B -> A -> string
    Item string_item = create_string("test");
    ValidationResult* validation = validate_by_name( string_item, "D");

    ASSERT_NE(validation, nullptr);
    EXPECT_TRUE(validation->valid) << "Chained type references should resolve to base type";
    EXPECT_EQ(validation->error_count, 0);

    // Also test intermediate types
    validation = validate_by_name( string_item, "C");
    EXPECT_TRUE(validation->valid);

    validation = validate_by_name( string_item, "B");
    EXPECT_TRUE(validation->valid);

    validation = validate_by_name( string_item, "A");
    EXPECT_TRUE(validation->valid);
}

TEST_F(TypeReferenceTest, TypeReferenceInComplexMap) {
    const char* schema = R"(
        type Email = string
        type PhoneNumber = string
        type Address = {
            street: string,
            city: string,
            zip: string
        }
        type Person = {
            name: string,
            email: Email,
            phone: PhoneNumber,
            address: Address
        }
    )";

    int result = schema_validator_load_schema(validator, schema, "Person");
    ASSERT_EQ(result, 0);

    // Create nested map structure
    Item address_map = create_map();
    add_map_field(address_map.map, "street", create_string("123 Main St"));
    add_map_field(address_map.map, "city", create_string("Springfield"));
    add_map_field(address_map.map, "zip", create_string("12345"));

    Item person_map = create_map();
    add_map_field(person_map.map, "name", create_string("Alice"));
    add_map_field(person_map.map, "email", create_string("alice@example.com"));
    add_map_field(person_map.map, "phone", create_string("+1-555-0100"));
    add_map_field(person_map.map, "address", address_map);

    ValidationResult* validation = validate_by_name( person_map, "Person");

    ASSERT_NE(validation, nullptr);
    EXPECT_TRUE(validation->valid) << "Complex map with type references should validate";
    EXPECT_EQ(validation->error_count, 0);
}
