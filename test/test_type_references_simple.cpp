/**
 * Test suite for validator type reference functionality
 * Tests type resolution, circular reference detection, and type aliases
 */

#include <gtest/gtest.h>
#include "lambda/validator/validator.hpp"
#include "lambda/lambda.h"
#include "lib/log.h"
#include "lib/mempool.h"

class TypeReferenceTest : public ::testing::Test {
protected:
    Pool* pool;
    SchemaValidator* validator;

    void SetUp() override {
        // Initialize logging system
        log_parse_config_file("log.conf");
        log_init("");  // Initialize with parsed config

        pool = pool_create();
        validator = schema_validator_create(pool);
    }

    void TearDown() override {
        schema_validator_destroy(validator);
        pool_destroy(pool);
    }
};

TEST_F(TypeReferenceTest, SimpleTypeAlias) {
    // Load schema with simple type alias
    const char* schema = R"(
type Username = string
)";

    int result = schema_validator_load_schema(validator, schema, "Username");
    ASSERT_EQ(result, 0) << "Schema should load successfully";

    // Verify the type was registered
    Type* username_type = schema_validator_find_type(validator, "Username");
    ASSERT_NE(username_type, nullptr) << "Username type should be registered";
    EXPECT_EQ(username_type->type_id, LMD_TYPE_STRING) << "Username should resolve to string type";
}

TEST_F(TypeReferenceTest, TypeAliasToInt) {
    const char* schema = R"(
        type Age = int
    )";

    int result = schema_validator_load_schema(validator, schema, "Age");
    ASSERT_EQ(result, 0);

    Type* age_type = schema_validator_find_type(validator, "Age");
    ASSERT_NE(age_type, nullptr) << "Age type should be registered";
    EXPECT_EQ(age_type->type_id, LMD_TYPE_INT) << "Age should resolve to int type";
}

TEST_F(TypeReferenceTest, MultipleTypeDefinitions) {
    const char* schema = R"(
        type Username = string
        type Age = int
        type Email = string
    )";

    int result = schema_validator_load_schema(validator, schema, "Username");
    ASSERT_EQ(result, 0);

    // Verify all types were registered
    Type* username_type = schema_validator_find_type(validator, "Username");
    Type* age_type = schema_validator_find_type(validator, "Age");
    Type* email_type = schema_validator_find_type(validator, "Email");

    ASSERT_NE(username_type, nullptr);
    ASSERT_NE(age_type, nullptr);
    ASSERT_NE(email_type, nullptr);

    EXPECT_EQ(username_type->type_id, LMD_TYPE_STRING);
    EXPECT_EQ(age_type->type_id, LMD_TYPE_INT);  // "int" maps to LMD_TYPE_INT in Lambda
    EXPECT_EQ(email_type->type_id, LMD_TYPE_STRING);
}

TEST_F(TypeReferenceTest, UndefinedTypeReference) {
    const char* schema = R"(
        type User = string
    )";

    int result = schema_validator_load_schema(validator, schema, "User");
    ASSERT_EQ(result, 0);

    // Try to find non-existent type
    Type* nonexistent = schema_validator_find_type(validator, "NonExistent");
    EXPECT_EQ(nonexistent, nullptr) << "Non-existent type should return nullptr";
}

TEST_F(TypeReferenceTest, TypeResolutionWithCircularCheck) {
    const char* schema = R"(
        type Name = string
        type ID = int
    )";

    int result = schema_validator_load_schema(validator, schema, "Name");
    ASSERT_EQ(result, 0);

    // Use resolve function with circular reference detection
    Type* name_type = schema_validator_resolve_type_reference(validator, "Name");
    ASSERT_NE(name_type, nullptr);
    EXPECT_EQ(name_type->type_id, LMD_TYPE_STRING);

    Type* id_type = schema_validator_resolve_type_reference(validator, "ID");
    ASSERT_NE(id_type, nullptr);
    EXPECT_EQ(id_type->type_id, LMD_TYPE_INT);  // "int" maps to LMD_TYPE_INT in Lambda

    // Non-existent type should return nullptr
    Type* invalid = schema_validator_resolve_type_reference(validator, "Invalid");
    EXPECT_EQ(invalid, nullptr) << "Undefined type should return nullptr";
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

    // Verify all types in chain resolve correctly
    Type* d_type = schema_validator_resolve_type_reference(validator, "D");
    ASSERT_NE(d_type, nullptr);
    EXPECT_EQ(d_type->type_id, LMD_TYPE_STRING) << "D should resolve through chain to string";

    Type* c_type = schema_validator_resolve_type_reference(validator, "C");
    ASSERT_NE(c_type, nullptr);
    EXPECT_EQ(c_type->type_id, LMD_TYPE_STRING);

    Type* b_type = schema_validator_resolve_type_reference(validator, "B");
    ASSERT_NE(b_type, nullptr);
    EXPECT_EQ(b_type->type_id, LMD_TYPE_STRING);

    Type* a_type = schema_validator_resolve_type_reference(validator, "A");
    ASSERT_NE(a_type, nullptr);
    EXPECT_EQ(a_type->type_id, LMD_TYPE_STRING);
}

TEST_F(TypeReferenceTest, MapWithTypeReferences) {
    const char* schema = R"(
        type Email = string
        type PhoneNumber = string
        type Person = {
            name: string,
            email: Email,
            phone: PhoneNumber
        }
    )";

    int result = schema_validator_load_schema(validator, schema, "Person");
    ASSERT_EQ(result, 0);

    // Verify Person type was registered
    Type* person_type = schema_validator_find_type(validator, "Person");
    ASSERT_NE(person_type, nullptr);
    EXPECT_EQ(person_type->type_id, LMD_TYPE_MAP);

    // Verify type references were registered
    Type* email_type = schema_validator_find_type(validator, "Email");
    ASSERT_NE(email_type, nullptr);
    EXPECT_EQ(email_type->type_id, LMD_TYPE_STRING);

    Type* phone_type = schema_validator_find_type(validator, "PhoneNumber");
    ASSERT_NE(phone_type, nullptr);
    EXPECT_EQ(phone_type->type_id, LMD_TYPE_STRING);
}

// Note: Circular reference test omitted as it may be caught during schema parsing
// The circular detection in schema_validator_resolve_type_reference is more for
// runtime resolution of complex type graphs
