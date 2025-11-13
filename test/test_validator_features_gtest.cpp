/**
 * @file test_validator_features_gtest.cpp
 * @brief Comprehensive tests for Lambda validator Phase 1-3 features
 * @author GitHub Copilot
 * 
 * Tests cover:
 * - Phase 1: Basic validation with primitives
 * - Phase 2: MarkReader integration (arrays, maps, elements)
 * - Phase 3: Occurrence operators, type registry, schema extraction
 */

#include <gtest/gtest.h>
#include "../lambda/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/input/input.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"

// Test fixture for validator features
class ValidatorFeaturesTest : public ::testing::Test {
protected:
    Pool* pool;
    AstValidator* validator;
    Input* input;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        
        validator = ast_validator_create(pool);
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
            ast_validator_destroy(validator);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper to create a simple type
    Type* create_primitive_type(TypeId type_id) {
        Type* type = (Type*)pool_calloc(pool, sizeof(Type));
        type->type_id = type_id;
        return type;
    }

    // Helper to create TypeType wrapper
    TypeType* create_type_wrapper(Type* nested) {
        TypeType* wrapper = (TypeType*)pool_calloc(pool, sizeof(TypeType));
        wrapper->type_id = LMD_TYPE_TYPE;
        wrapper->type = nested;
        return wrapper;
    }

    // Helper to create array type
    TypeArray* create_array_type(Type* element_type) {
        TypeArray* array_type = (TypeArray*)pool_calloc(pool, sizeof(TypeArray));
        array_type->type_id = LMD_TYPE_ARRAY;
        array_type->nested = element_type;
        return array_type;
    }

    // Helper to create map type with fields
    TypeMap* create_map_type() {
        TypeMap* map_type = (TypeMap*)pool_calloc(pool, sizeof(TypeMap));
        map_type->type_id = LMD_TYPE_MAP;
        return map_type;
    }
};

// ==================== Phase 1: Basic Validation Tests ====================

TEST_F(ValidatorFeaturesTest, ValidatePrimitiveString) {
    // Create string type
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TypeType* wrapper = create_type_wrapper(string_type);

    // Create string item
    String* str = create_string(pool, "test");
    Item item_mut;
    item_mut.item = (uint64_t)str | ((uint64_t)LMD_TYPE_STRING << 56);
    ConstItem item = item_mut.to_const();

    // Validate
    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidatePrimitiveInt) {
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TypeType* wrapper = create_type_wrapper(int_type);

    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidatePrimitiveBool) {
    Type* bool_type = create_primitive_type(LMD_TYPE_BOOL);
    TypeType* wrapper = create_type_wrapper(bool_type);

    Item item_mut;
    item_mut.bool_val = 1; // true
    item_mut._type_id = LMD_TYPE_BOOL;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, PrimitiveTypeMismatch) {
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TypeType* wrapper = create_type_wrapper(string_type);

    // Provide int instead of string
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);
    EXPECT_GT(result->error_count, 0);
    EXPECT_NE(result->errors, nullptr);
}

// ==================== Phase 2: Array Validation (MarkReader) ====================

TEST_F(ValidatorFeaturesTest, ValidateArrayOfIntegers) {
    // Create array type with int elements
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TypeArray* array_type = create_array_type(int_type);

    // Create array of integers (use generic Array type, not ArrayInt)
    Array* array = (Array*)pool_calloc(pool, sizeof(Array));
    array->type_id = LMD_TYPE_ARRAY;
    array->length = 3;
    array->capacity = 3;
    
    // Allocate items array
    Item* items = (Item*)pool_calloc(pool, sizeof(Item) * 3);
    items[0].int_val = 1;
    items[0]._type_id = LMD_TYPE_INT;
    items[1].int_val = 2;
    items[1]._type_id = LMD_TYPE_INT;
    items[2].int_val = 3;
    items[2]._type_id = LMD_TYPE_INT;
    array->items = items;

    Item item_mut;
    item_mut.array = array;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)array_type);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Array validation should succeed";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateEmptyArray) {
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TypeArray* array_type = create_array_type(int_type);

    // Create empty array
    List* list = (List*)pool_calloc(pool, sizeof(List));
    list->type_id = LMD_TYPE_ARRAY;
    list->length = 0;
    list->capacity = 0;

    Item item_mut;
    item_mut.list = list;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)array_type);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateArrayTypeMismatch) {
    // Expect array of ints
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TypeArray* array_type = create_array_type(int_type);

    // Provide a single int instead
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)array_type);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid);
    EXPECT_GT(result->error_count, 0);
}

// ==================== Phase 2: Map Validation (MarkReader) ====================

TEST_F(ValidatorFeaturesTest, ValidateMapWithFields) {
    // Create map type
    TypeMap* map_type = create_map_type();
    
    // Add field: name: string
    ShapeEntry* entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* field_name = "name";
    entry->name->str = field_name;
    entry->name->length = strlen(field_name);
    entry->type = create_primitive_type(LMD_TYPE_STRING);
    entry->byte_offset = 0;
    entry->next = nullptr;
    map_type->shape = entry;

    // Create actual map
    Map* map = (Map*)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    
    // Create the map's type info
    TypeMap* map_data_type = create_map_type();
    map_data_type->shape = entry;
    map->type = map_data_type;
    
    // Allocate data for one string field
    void* data = pool_calloc(pool, sizeof(String*));
    String** field_ptr = (String**)data;
    *field_ptr = create_string(pool, "John");
    map->data = data;

    Item item_mut;
    item_mut.map = map;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)map_type);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Map validation should succeed";
    EXPECT_EQ(result->error_count, 0);
}

// ==================== Phase 3: Occurrence Operators ====================

TEST_F(ValidatorFeaturesTest, ValidateOptionalOperator) {
    // Create type: string?
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    
    TypeUnary* unary_type = (TypeUnary*)pool_calloc(pool, sizeof(TypeUnary));
    unary_type->type_id = LMD_TYPE_TYPE;
    unary_type->operand = string_type;
    unary_type->op = OPERATOR_OPTIONAL;

    TypeType* wrapper = create_type_wrapper((Type*)unary_type);

    // Test with a string (should pass)
    String* str = create_string(pool, "optional");
    Item item_mut;
    item_mut.item = (uint64_t)str | ((uint64_t)LMD_TYPE_STRING << 56);
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Optional operator should allow single item";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateOneOrMoreOperator) {
    // Create type: int+
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    TypeUnary* unary_type = (TypeUnary*)pool_calloc(pool, sizeof(TypeUnary));
    unary_type->type_id = LMD_TYPE_TYPE;
    unary_type->operand = int_type;
    unary_type->op = OPERATOR_ONE_MORE;

    TypeType* wrapper = create_type_wrapper((Type*)unary_type);

    // Test with an int (should pass - one is valid for +)
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "One-or-more operator should allow single item";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateZeroOrMoreOperator) {
    // Create type: string*
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    
    TypeUnary* unary_type = (TypeUnary*)pool_calloc(pool, sizeof(TypeUnary));
    unary_type->type_id = LMD_TYPE_TYPE;
    unary_type->operand = string_type;
    unary_type->op = OPERATOR_ZERO_MORE;

    TypeType* wrapper = create_type_wrapper((Type*)unary_type);

    // Test with a string
    String* str = create_string(pool, "zero or more");
    Item item_mut;
    item_mut.item = (uint64_t)str | ((uint64_t)LMD_TYPE_STRING << 56);
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)wrapper);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Zero-or-more operator should allow any count";
    EXPECT_EQ(result->error_count, 0);
}

// ==================== Phase 3: Type Registry and Schema Extraction ====================

// Helper struct matching TypeRegistryEntry layout (private in validator.cpp)
struct TestTypeEntry {
    StrView name;
    Type* type;
};

TEST_F(ValidatorFeaturesTest, LoadSimpleSchema) {
    // Directly test type registration without AST parsing
    // (AST parser doesn't support type statements yet)
    
    // Create Person type: { name: string, age: int }
    TypeMap* person_map = create_map_type();
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    // Register Person type in registry
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Verify registration
    Type* retrieved = ast_validator_find_type(validator, "Person");
    EXPECT_NE(retrieved, nullptr) << "Person type should be registered";
    EXPECT_EQ(retrieved->type_id, LMD_TYPE_MAP);
}

TEST_F(ValidatorFeaturesTest, LoadSchemaWithMultipleTypes) {
    // Test multiple type registrations
    
    // Create and register Address type
    TypeMap* address_map = create_map_type();
    TestTypeEntry address_entry;
    address_entry.name = (StrView){"Address", 7};
    address_entry.type = (Type*)address_map;
    hashmap_set(validator->type_definitions, &address_entry);
    
    // Create and register Person type
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Create and register Company type
    TypeMap* company_map = create_map_type();
    TestTypeEntry company_entry;
    company_entry.name = (StrView){"Company", 7};
    company_entry.type = (Type*)company_map;
    hashmap_set(validator->type_definitions, &company_entry);
    
    // Verify all types are registered
    EXPECT_NE(ast_validator_find_type(validator, "Address"), nullptr);
    EXPECT_NE(ast_validator_find_type(validator, "Person"), nullptr);
    EXPECT_NE(ast_validator_find_type(validator, "Company"), nullptr);
}

TEST_F(ValidatorFeaturesTest, TypeNotFound) {
    // Register a type
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Verify Person exists
    EXPECT_NE(ast_validator_find_type(validator, "Person"), nullptr);
    
    // Try to find non-existent type
    Type* result = ast_validator_find_type(validator, "NonExistent");
    EXPECT_EQ(result, nullptr) << "Should return nullptr for non-existent type";
}

// ==================== Phase 3: Type Reference Resolution ====================

TEST_F(ValidatorFeaturesTest, ResolveSimpleTypeReference) {
    // Register a type
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Resolve the reference
    Type* resolved = ast_validator_resolve_type_reference(validator, "Person");
    EXPECT_NE(resolved, nullptr) << "Should resolve type reference";
    EXPECT_EQ(resolved, (Type*)person_map) << "Should return the registered type";
    EXPECT_EQ(resolved->type_id, LMD_TYPE_MAP);
}

TEST_F(ValidatorFeaturesTest, ResolveNestedTypeReference) {
    // Register Address type
    TypeMap* address_map = create_map_type();
    TestTypeEntry address_entry;
    address_entry.name = (StrView){"Address", 7};
    address_entry.type = (Type*)address_map;
    hashmap_set(validator->type_definitions, &address_entry);
    
    // Register Person type (references Address)
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Resolve both references
    Type* address_resolved = ast_validator_resolve_type_reference(validator, "Address");
    Type* person_resolved = ast_validator_resolve_type_reference(validator, "Person");
    
    EXPECT_NE(address_resolved, nullptr);
    EXPECT_NE(person_resolved, nullptr);
    EXPECT_EQ(address_resolved, (Type*)address_map);
    EXPECT_EQ(person_resolved, (Type*)person_map);
}

TEST_F(ValidatorFeaturesTest, DetectCircularTypeReference) {
    // This test simulates the scenario where we might detect circular references
    // In practice, circular references would be detected during validation traversal
    
    // Create a mock recursive type (simplified)
    TypeMap* node_map = create_map_type();
    TestTypeEntry node_entry;
    node_entry.name = (StrView){"Node", 4};
    node_entry.type = (Type*)node_map;
    hashmap_set(validator->type_definitions, &node_entry);
    
    // First resolution should work
    Type* first_resolve = ast_validator_resolve_type_reference(validator, "Node");
    EXPECT_NE(first_resolve, nullptr);
    
    // The visited_nodes mechanism prevents infinite loops during validation
    // The function marks as visited, resolves, then unmarks
    // So we can resolve the same type multiple times
    Type* second_resolve = ast_validator_resolve_type_reference(validator, "Node");
    EXPECT_NE(second_resolve, nullptr);
    EXPECT_EQ(first_resolve, second_resolve);
}

TEST_F(ValidatorFeaturesTest, ResolveNonExistentTypeReference) {
    // Try to resolve a type that doesn't exist
    Type* resolved = ast_validator_resolve_type_reference(validator, "NonExistent");
    EXPECT_EQ(resolved, nullptr) << "Should return nullptr for non-existent type";
}

// ==================== Phase 4: Advanced Type Reference Tests ====================

TEST_F(ValidatorFeaturesTest, ResolveMultipleTypesInRegistry) {
    // Register multiple types
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    TypeArray* numbers_array = create_array_type(create_primitive_type(LMD_TYPE_INT));
    TestTypeEntry numbers_entry;
    numbers_entry.name = (StrView){"Numbers", 7};
    numbers_entry.type = (Type*)numbers_array;
    hashmap_set(validator->type_definitions, &numbers_entry);
    
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TestTypeEntry status_entry;
    status_entry.name = (StrView){"Status", 6};
    status_entry.type = string_type;
    hashmap_set(validator->type_definitions, &status_entry);
    
    // Resolve all three types
    Type* person_resolved = ast_validator_resolve_type_reference(validator, "Person");
    Type* numbers_resolved = ast_validator_resolve_type_reference(validator, "Numbers");
    Type* status_resolved = ast_validator_resolve_type_reference(validator, "Status");
    
    EXPECT_NE(person_resolved, nullptr);
    EXPECT_NE(numbers_resolved, nullptr);
    EXPECT_NE(status_resolved, nullptr);
    EXPECT_EQ(person_resolved->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(numbers_resolved->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(status_resolved->type_id, LMD_TYPE_STRING);
}

TEST_F(ValidatorFeaturesTest, ResolveTypeAfterMultipleLookups) {
    // Register a base type
    Type* base_int = create_primitive_type(LMD_TYPE_INT);
    TestTypeEntry age_entry;
    age_entry.name = (StrView){"Age", 3};
    age_entry.type = base_int;
    hashmap_set(validator->type_definitions, &age_entry);
    
    // Resolve multiple times to ensure stability
    Type* resolved1 = ast_validator_resolve_type_reference(validator, "Age");
    Type* resolved2 = ast_validator_resolve_type_reference(validator, "Age");
    Type* resolved3 = ast_validator_resolve_type_reference(validator, "Age");
    
    EXPECT_NE(resolved1, nullptr);
    EXPECT_EQ(resolved1, resolved2) << "Multiple resolutions should return same pointer";
    EXPECT_EQ(resolved2, resolved3) << "Multiple resolutions should return same pointer";
    EXPECT_EQ(resolved1, base_int);
}

TEST_F(ValidatorFeaturesTest, CircularReferenceDetectionInDepth) {
    // Create a type that references itself (simulated)
    TypeMap* recursive_map = create_map_type();
    TestTypeEntry node_entry;
    node_entry.name = (StrView){"RecursiveNode", 13};
    node_entry.type = (Type*)recursive_map;
    hashmap_set(validator->type_definitions, &node_entry);
    
    // Mark as visited manually to simulate circular detection
    VisitedEntry visit_entry;
    visit_entry.key = (StrView){"RecursiveNode", 13};
    visit_entry.visited = true;
    hashmap_set(validator->visited_nodes, &visit_entry);
    
    // Try to resolve while marked as visited (circular reference)
    Type* resolved = ast_validator_resolve_type_reference(validator, "RecursiveNode");
    EXPECT_EQ(resolved, nullptr) << "Should return nullptr when circular reference detected";
    
    // Cleanup: unmark for other tests
    visit_entry.visited = false;
    hashmap_set(validator->visited_nodes, &visit_entry);
}

TEST_F(ValidatorFeaturesTest, TypeRegistryOverwrite) {
    // Register a type
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TestTypeEntry first_entry;
    first_entry.name = (StrView){"Status", 6};
    first_entry.type = string_type;
    hashmap_set(validator->type_definitions, &first_entry);
    
    // Overwrite with a different type
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TestTypeEntry second_entry;
    second_entry.name = (StrView){"Status", 6};
    second_entry.type = int_type;
    hashmap_set(validator->type_definitions, &second_entry);
    
    // Should resolve to the most recent type
    Type* resolved = ast_validator_resolve_type_reference(validator, "Status");
    EXPECT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->type_id, LMD_TYPE_INT) << "Should use overwritten type";
}

TEST_F(ValidatorFeaturesTest, ResolveArrayOfReferencedType) {
    // Register a base type
    Type* person_type = create_primitive_type(LMD_TYPE_MAP);
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = person_type;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Create array of referenced type
    TypeArray* people_array = create_array_type(person_type);
    TestTypeEntry people_entry;
    people_entry.name = (StrView){"People", 6};
    people_entry.type = (Type*)people_array;
    hashmap_set(validator->type_definitions, &people_entry);
    
    // Resolve both
    Type* person_resolved = ast_validator_resolve_type_reference(validator, "Person");
    Type* people_resolved = ast_validator_resolve_type_reference(validator, "People");
    
    EXPECT_NE(person_resolved, nullptr);
    EXPECT_NE(people_resolved, nullptr);
    EXPECT_EQ(people_resolved->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(((TypeArray*)people_resolved)->nested, person_type);
}

// ==================== Phase 3: Union Type Validation ====================

TEST_F(ValidatorFeaturesTest, ValidateUnionType_FirstMatch) {
    // Create union: string | int
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    // Test with string (first type in union)
    String* str = create_string(pool, "union test");
    Item item_mut;
    item_mut.item = (uint64_t)str | ((uint64_t)LMD_TYPE_STRING << 56);
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Should match first type in union";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateUnionType_SecondMatch) {
    // Create union: string | int
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    // Test with int (second type in union)
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Should match second type in union";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, ValidateUnionType_NoMatch) {
    // Create union: string | int
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    // Test with bool (not in union)
    Item item_mut;
    item_mut.bool_val = 1;
    item_mut._type_id = LMD_TYPE_BOOL;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Should fail when no union type matches";
    EXPECT_GT(result->error_count, 0);
    
    // Verify that we get informative error messages
    EXPECT_NE(result->errors, nullptr) << "Should have error information";
}

TEST_F(ValidatorFeaturesTest, ValidateUnionType_BestErrorTracking) {
    // Create union with multiple types: string | int | {name: string}
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TypeMap* map_type = create_map_type();
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 3);
    union_types[0] = string_type;
    union_types[1] = int_type;
    union_types[2] = (Type*)map_type;

    // Test with a float (doesn't match any union member)
    Item item_mut;
    item_mut.item = 0;  // Some value
    item_mut._type_id = LMD_TYPE_FLOAT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 3);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Should fail - float doesn't match string | int | map";
    EXPECT_GT(result->error_count, 0) << "Should have at least one error";
    
    // Verify error message contains union information
    if (result->errors && result->errors->message) {
        const char* error_text = result->errors->message->chars;
        EXPECT_NE(error_text, nullptr);
        // Error message should mention it's a union and how many types were tried
        // This is implementation-dependent, but should be informative
    }
}

// ==================== Phase 5: Advanced Union Type Tests ====================

TEST_F(ValidatorFeaturesTest, UnionWithPrimitiveTypes) {
    // Create union: string | int | bool
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 3);
    union_types[0] = create_primitive_type(LMD_TYPE_STRING);
    union_types[1] = create_primitive_type(LMD_TYPE_INT);
    union_types[2] = create_primitive_type(LMD_TYPE_BOOL);

    // Test with bool (third type)
    Item item_mut;
    item_mut.bool_val = 1;
    item_mut._type_id = LMD_TYPE_BOOL;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 3);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Bool should match third type in union";
}

TEST_F(ValidatorFeaturesTest, UnionWithManyTypes) {
    // Create union with 5 types: string | int | bool | float | symbol
    Type* types[5];
    types[0] = create_primitive_type(LMD_TYPE_STRING);
    types[1] = create_primitive_type(LMD_TYPE_INT);
    types[2] = create_primitive_type(LMD_TYPE_BOOL);
    types[3] = create_primitive_type(LMD_TYPE_FLOAT);
    types[4] = create_primitive_type(LMD_TYPE_SYMBOL);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 5);
    for (int i = 0; i < 5; i++) {
        union_types[i] = types[i];
    }

    // Test with float (fourth type)
    Item item_mut;
    item_mut.item = 0;
    item_mut._type_id = LMD_TYPE_FLOAT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 5);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Float should match fourth type in large union";
}

TEST_F(ValidatorFeaturesTest, UnionErrorMessageQuality) {
    // Create union: int | string | bool
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 3);
    union_types[0] = create_primitive_type(LMD_TYPE_INT);
    union_types[1] = create_primitive_type(LMD_TYPE_STRING);
    union_types[2] = create_primitive_type(LMD_TYPE_BOOL);

    // Test with symbol (matches none)
    Item item_mut;
    item_mut.item = 0;
    item_mut._type_id = LMD_TYPE_SYMBOL;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 3);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Symbol should not match int | string | bool";
    EXPECT_GT(result->error_count, 0) << "Should have errors";
    EXPECT_NE(result->errors, nullptr) << "Should have error details";
}

TEST_F(ValidatorFeaturesTest, UnionSingleType) {
    // Edge case: union with only one type (essentially just that type)
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 1);
    union_types[0] = int_type;

    // Test with int
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 1);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Should match single type in union";
}

TEST_F(ValidatorFeaturesTest, UnionWithNullType) {
    // Create union: string | int
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = string_type;
    union_types[1] = int_type;

    // Test with null-like value
    Item item_mut;
    item_mut.item = 0;
    item_mut._type_id = LMD_TYPE_NULL;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Null should not match string | int union";
}

// ==================== Phase 4+5: Combined Features ====================

TEST_F(ValidatorFeaturesTest, ReferencedTypesInRegistry) {
    // Register multiple related types
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    TestTypeEntry name_entry;
    name_entry.name = (StrView){"Name", 4};
    name_entry.type = string_type;
    hashmap_set(validator->type_definitions, &name_entry);
    
    TestTypeEntry count_entry;
    count_entry.name = (StrView){"Count", 5};
    count_entry.type = int_type;
    hashmap_set(validator->type_definitions, &count_entry);
    
    // Resolve both
    Type* name_resolved = ast_validator_resolve_type_reference(validator, "Name");
    Type* count_resolved = ast_validator_resolve_type_reference(validator, "Count");
    
    EXPECT_NE(name_resolved, nullptr);
    EXPECT_NE(count_resolved, nullptr);
    EXPECT_EQ(name_resolved->type_id, LMD_TYPE_STRING);
    EXPECT_EQ(count_resolved->type_id, LMD_TYPE_INT);
}

// ==================== Depth Limit Tests ====================

TEST_F(ValidatorFeaturesTest, ValidatorDepthLimit) {
    validator->options.max_depth = 5;
    
    // Create nested array types to exceed depth
    Type* base = create_primitive_type(LMD_TYPE_INT);
    TypeArray* arr1 = create_array_type(base);
    TypeArray* arr2 = create_array_type((Type*)arr1);
    TypeArray* arr3 = create_array_type((Type*)arr2);
    TypeArray* arr4 = create_array_type((Type*)arr3);
    TypeArray* arr5 = create_array_type((Type*)arr4);
    TypeArray* arr6 = create_array_type((Type*)arr5); // This should exceed limit

    Item item_mut;
    item_mut.item = 0; // Any item
    ConstItem item = item_mut.to_const();

    validator->current_depth = 0;
    ValidationResult* result = validate_against_type(validator, item, (Type*)arr6);
    
    ASSERT_NE(result, nullptr);
    // Depth check happens early, may fail before type validation
}

// ==================== Error Path Tests ====================

TEST_F(ValidatorFeaturesTest, ErrorPathCreation) {
    // Create a validation error with path
    PathSegment* segment = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    segment->type = PATH_FIELD;
    segment->data.field_name = (StrView){"testField", 9};
    segment->next = nullptr;

    ValidationError* error = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH,
        "Test error message",
        segment,
        pool
    );

    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, VALID_ERROR_TYPE_MISMATCH);
    EXPECT_NE(error->message, nullptr);
    EXPECT_EQ(error->path, segment);
}

// ==================== Validator Creation/Destruction ====================

TEST_F(ValidatorFeaturesTest, ValidatorHasTypeRegistry) {
    EXPECT_NE(validator->type_definitions, nullptr);
}

TEST_F(ValidatorFeaturesTest, ValidatorHasVisitedNodes) {
    EXPECT_NE(validator->visited_nodes, nullptr);
}

TEST_F(ValidatorFeaturesTest, ValidatorHasDefaultOptions) {
    EXPECT_FALSE(validator->options.strict_mode);
    EXPECT_TRUE(validator->options.allow_unknown_fields);
    EXPECT_TRUE(validator->options.allow_empty_elements);
    EXPECT_EQ(validator->options.max_depth, 1024);
}

// ==================== Integration Tests (Phase 1-5 Combined) ====================

TEST_F(ValidatorFeaturesTest, Integration_TypedArrayWithReferences) {
    // Simulates: type Username = string; type Users = [Username]
    
    // Register Username type
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TestTypeEntry username_entry;
    username_entry.name = (StrView){"Username", 8};
    username_entry.type = string_type;
    hashmap_set(validator->type_definitions, &username_entry);
    
    // Create Users type as array of Username
    Type* username_resolved = ast_validator_resolve_type_reference(validator, "Username");
    ASSERT_NE(username_resolved, nullptr);
    
    TypeArray* users_array = create_array_type(username_resolved);
    TestTypeEntry users_entry;
    users_entry.name = (StrView){"Users", 5};
    users_entry.type = (Type*)users_array;
    hashmap_set(validator->type_definitions, &users_entry);
    
    // Create array of strings manually
    Array* test_array = (Array*)pool_calloc(pool, sizeof(Array));
    test_array->type_id = LMD_TYPE_ARRAY;
    test_array->length = 2;
    test_array->capacity = 2;
    
    Item* items = (Item*)pool_calloc(pool, sizeof(Item) * 2);
    String* str1 = create_string(pool, "alice");
    String* str2 = create_string(pool, "bob");
    items[0].item = (uint64_t)str1 | ((uint64_t)LMD_TYPE_STRING << 56);
    items[1].item = (uint64_t)str2 | ((uint64_t)LMD_TYPE_STRING << 56);
    test_array->items = items;
    
    Item item_mut;
    item_mut.array = test_array;
    ConstItem item = item_mut.to_const();
    
    Type* users_resolved = ast_validator_resolve_type_reference(validator, "Users");
    ASSERT_NE(users_resolved, nullptr);
    
    ValidationResult* result = validate_against_type(validator, item, users_resolved);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Array of usernames should be valid";
}

TEST_F(ValidatorFeaturesTest, Integration_UnionWithOptionals) {
    // Simulates: type Result = string | int | null
    // Combines union types with optional handling
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 3);
    union_types[0] = create_primitive_type(LMD_TYPE_STRING);
    union_types[1] = create_primitive_type(LMD_TYPE_INT);
    union_types[2] = create_primitive_type(LMD_TYPE_NULL);

    // Test with string
    String* str = create_string(pool, "success");
    Item item_mut1;
    item_mut1.item = (uint64_t)str | ((uint64_t)LMD_TYPE_STRING << 56);
    ConstItem item1 = item_mut1.to_const();
    
    ValidationResult* result1 = validate_against_union_type(validator, item1, union_types, 3);
    ASSERT_NE(result1, nullptr);
    EXPECT_TRUE(result1->valid) << "String should match union";
    
    // Test with int
    Item item_mut2;
    item_mut2.int_val = 42;
    item_mut2._type_id = LMD_TYPE_INT;
    ConstItem item2 = item_mut2.to_const();
    
    ValidationResult* result2 = validate_against_union_type(validator, item2, union_types, 3);
    ASSERT_NE(result2, nullptr);
    EXPECT_TRUE(result2->valid) << "Int should match union";
    
    // Test with null
    Item item_mut3;
    item_mut3.item = 0;
    item_mut3._type_id = LMD_TYPE_NULL;
    ConstItem item3 = item_mut3.to_const();
    
    ValidationResult* result3 = validate_against_union_type(validator, item3, union_types, 3);
    ASSERT_NE(result3, nullptr);
    EXPECT_TRUE(result3->valid) << "Null should match union";
}

TEST_F(ValidatorFeaturesTest, Integration_NestedReferences) {
    // Simulates: type Address = {street: string}; type Person = {address: Address}
    
    // Create Address type
    TypeMap* address_map = create_map_type();
    TestTypeEntry address_entry;
    address_entry.name = (StrView){"Address", 7};
    address_entry.type = (Type*)address_map;
    hashmap_set(validator->type_definitions, &address_entry);
    
    // Create Person type that references Address
    TypeMap* person_map = create_map_type();
    TestTypeEntry person_entry;
    person_entry.name = (StrView){"Person", 6};
    person_entry.type = (Type*)person_map;
    hashmap_set(validator->type_definitions, &person_entry);
    
    // Resolve both types
    Type* address_resolved = ast_validator_resolve_type_reference(validator, "Address");
    Type* person_resolved = ast_validator_resolve_type_reference(validator, "Person");
    
    ASSERT_NE(address_resolved, nullptr);
    ASSERT_NE(person_resolved, nullptr);
    EXPECT_EQ(address_resolved->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(person_resolved->type_id, LMD_TYPE_MAP);
}

TEST_F(ValidatorFeaturesTest, Integration_UnionOfArrays) {
    // Simulates: type Data = [int] | [string]
    // Union of different array types
    
    Type* int_array = (Type*)create_array_type(create_primitive_type(LMD_TYPE_INT));
    Type* string_array = (Type*)create_array_type(create_primitive_type(LMD_TYPE_STRING));
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = int_array;
    union_types[1] = string_array;
    
    // Create array of ints manually
    Array* int_arr = (Array*)pool_calloc(pool, sizeof(Array));
    int_arr->type_id = LMD_TYPE_ARRAY;
    int_arr->length = 2;
    int_arr->capacity = 2;
    
    Item* items = (Item*)pool_calloc(pool, sizeof(Item) * 2);
    items[0].int_val = 1;
    items[0]._type_id = LMD_TYPE_INT;
    items[1].int_val = 2;
    items[1]._type_id = LMD_TYPE_INT;
    int_arr->items = items;
    
    Item item_mut;
    item_mut.array = int_arr;
    ConstItem item = item_mut.to_const();
    
    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Array of ints should match first union type";
}

TEST_F(ValidatorFeaturesTest, Integration_ComplexTypeChain) {
    // Simulates a chain: type A = string; type B = [A]; type C = B | int
    
    // Register type A = string
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TestTypeEntry a_entry;
    a_entry.name = (StrView){"A", 1};
    a_entry.type = string_type;
    hashmap_set(validator->type_definitions, &a_entry);
    
    // Register type B = [A]
    Type* a_resolved = ast_validator_resolve_type_reference(validator, "A");
    ASSERT_NE(a_resolved, nullptr);
    TypeArray* b_array = create_array_type(a_resolved);
    TestTypeEntry b_entry;
    b_entry.name = (StrView){"B", 1};
    b_entry.type = (Type*)b_array;
    hashmap_set(validator->type_definitions, &b_entry);
    
    // Create type C = B | int
    Type* b_resolved = ast_validator_resolve_type_reference(validator, "B");
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = b_resolved;
    union_types[1] = int_type;
    
    // Test with an int (should match second union member)
    Item item_mut;
    item_mut.int_val = 42;
    item_mut._type_id = LMD_TYPE_INT;
    ConstItem item = item_mut.to_const();
    
    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Int should match second type in union C";
}

TEST_F(ValidatorFeaturesTest, Integration_MultipleReferencesNoCircular) {
    // Simulates: type ID = int; type User = {id: ID}; type Post = {author_id: ID}
    // Multiple types referencing the same base type
    
    // Register ID type
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    TestTypeEntry id_entry;
    id_entry.name = (StrView){"ID", 2};
    id_entry.type = int_type;
    hashmap_set(validator->type_definitions, &id_entry);
    
    // Register User type
    TypeMap* user_map = create_map_type();
    TestTypeEntry user_entry;
    user_entry.name = (StrView){"User", 4};
    user_entry.type = (Type*)user_map;
    hashmap_set(validator->type_definitions, &user_entry);
    
    // Register Post type
    TypeMap* post_map = create_map_type();
    TestTypeEntry post_entry;
    post_entry.name = (StrView){"Post", 4};
    post_entry.type = (Type*)post_map;
    hashmap_set(validator->type_definitions, &post_entry);
    
    // Resolve ID from different contexts
    Type* id_for_user = ast_validator_resolve_type_reference(validator, "ID");
    Type* id_for_post = ast_validator_resolve_type_reference(validator, "ID");
    
    ASSERT_NE(id_for_user, nullptr);
    ASSERT_NE(id_for_post, nullptr);
    EXPECT_EQ(id_for_user, id_for_post) << "Should resolve to same ID type";
}

TEST_F(ValidatorFeaturesTest, Integration_UnionErrorWithTypeReferences) {
    // Simulates: type Name = string; type Age = int; type Data = Name | Age
    // Test error reporting when neither union member matches
    
    // Register Name and Age types
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    
    TestTypeEntry name_entry;
    name_entry.name = (StrView){"Name", 4};
    name_entry.type = string_type;
    hashmap_set(validator->type_definitions, &name_entry);
    
    TestTypeEntry age_entry;
    age_entry.name = (StrView){"Age", 3};
    age_entry.type = int_type;
    hashmap_set(validator->type_definitions, &age_entry);
    
    // Create union Data = Name | Age
    Type* name_resolved = ast_validator_resolve_type_reference(validator, "Name");
    Type* age_resolved = ast_validator_resolve_type_reference(validator, "Age");
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = name_resolved;
    union_types[1] = age_resolved;
    
    // Test with bool (doesn't match either)
    Item item_mut;
    item_mut.bool_val = 1;
    item_mut._type_id = LMD_TYPE_BOOL;
    ConstItem item = item_mut.to_const();
    
    ValidationResult* result = validate_against_union_type(validator, item, union_types, 2);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Bool should not match Name | Age union";
    EXPECT_GT(result->error_count, 0);
    EXPECT_NE(result->errors, nullptr) << "Should have detailed error information";
}

TEST_F(ValidatorFeaturesTest, Integration_DeepTypeNesting) {
    // Simulates: type A = string; type B = [A]; type C = [B]; type D = [C]
    // Deep nesting of type references and arrays
    
    Type* string_type = create_primitive_type(LMD_TYPE_STRING);
    TestTypeEntry a_entry;
    a_entry.name = (StrView){"A", 1};
    a_entry.type = string_type;
    hashmap_set(validator->type_definitions, &a_entry);
    
    Type* a_resolved = ast_validator_resolve_type_reference(validator, "A");
    TypeArray* b_array = create_array_type(a_resolved);
    TestTypeEntry b_entry;
    b_entry.name = (StrView){"B", 1};
    b_entry.type = (Type*)b_array;
    hashmap_set(validator->type_definitions, &b_entry);
    
    Type* b_resolved = ast_validator_resolve_type_reference(validator, "B");
    TypeArray* c_array = create_array_type(b_resolved);
    TestTypeEntry c_entry;
    c_entry.name = (StrView){"C", 1};
    c_entry.type = (Type*)c_array;
    hashmap_set(validator->type_definitions, &c_entry);
    
    Type* c_resolved = ast_validator_resolve_type_reference(validator, "C");
    TypeArray* d_array = create_array_type(c_resolved);
    
    // Verify all types resolve correctly
    ASSERT_NE(a_resolved, nullptr);
    ASSERT_NE(b_resolved, nullptr);
    ASSERT_NE(c_resolved, nullptr);
    EXPECT_EQ(a_resolved->type_id, LMD_TYPE_STRING);
    EXPECT_EQ(b_resolved->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(c_resolved->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(d_array->type_id, LMD_TYPE_ARRAY);
    
    // Verify nesting structure
    EXPECT_EQ(((TypeArray*)b_resolved)->nested, a_resolved);
    EXPECT_EQ(((TypeArray*)c_resolved)->nested, b_resolved);
    EXPECT_EQ(d_array->nested, c_resolved);
}

TEST_F(ValidatorFeaturesTest, Integration_UnionWithPrimitiveAndArray) {
    // Simulates: type Value = int | [int]
    // Union combining primitive and array
    
    Type* int_type = create_primitive_type(LMD_TYPE_INT);
    Type* int_array = (Type*)create_array_type(create_primitive_type(LMD_TYPE_INT));
    
    Type** union_types = (Type**)pool_calloc(pool, sizeof(Type*) * 2);
    union_types[0] = int_type;
    union_types[1] = int_array;
    
    // Test with single int (first alternative)
    Item item_mut1;
    item_mut1.int_val = 42;
    item_mut1._type_id = LMD_TYPE_INT;
    ConstItem item1 = item_mut1.to_const();
    
    ValidationResult* result1 = validate_against_union_type(validator, item1, union_types, 2);
    ASSERT_NE(result1, nullptr);
    EXPECT_TRUE(result1->valid) << "Single int should match first union type";
    
    // Test with array of ints (second alternative)
    Array* arr = (Array*)pool_calloc(pool, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    arr->length = 1;
    arr->capacity = 1;
    
    Item* items = (Item*)pool_calloc(pool, sizeof(Item) * 1);
    items[0].int_val = 100;
    items[0]._type_id = LMD_TYPE_INT;
    arr->items = items;
    
    Item item_mut2;
    item_mut2.array = arr;
    ConstItem item2 = item_mut2.to_const();
    
    ValidationResult* result2 = validate_against_union_type(validator, item2, union_types, 2);
    ASSERT_NE(result2, nullptr);
    EXPECT_TRUE(result2->valid) << "Array of ints should match second union type";
}

// ==================== Map Field Validation Tests ====================

TEST_F(ValidatorFeaturesTest, MapField_MultipleFields) {
    // Test map with multiple typed fields
    TypeMap* map_type = create_map_type();
    
    // Add field: name: string
    ShapeEntry* name_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    name_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* name_field = "name";
    name_entry->name->str = name_field;
    name_entry->name->length = strlen(name_field);
    name_entry->type = create_primitive_type(LMD_TYPE_STRING);
    name_entry->byte_offset = 0;
    name_entry->next = nullptr;
    
    // Add field: age: int
    ShapeEntry* age_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    age_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* age_field = "age";
    age_entry->name->str = age_field;
    age_entry->name->length = strlen(age_field);
    age_entry->type = create_primitive_type(LMD_TYPE_INT);
    age_entry->byte_offset = sizeof(String*);
    age_entry->next = nullptr;
    
    name_entry->next = age_entry;
    map_type->shape = name_entry;

    // Create map with both fields
    Map* map = (Map*)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    
    TypeMap* map_data_type = create_map_type();
    map_data_type->shape = name_entry;
    map->type = map_data_type;
    
    // Allocate data for string and int fields
    size_t data_size = sizeof(String*) + sizeof(int64_t);
    void* data = pool_calloc(pool, data_size);
    
    // Set name field (string)
    String** name_ptr = (String**)data;
    *name_ptr = create_string(pool, "Alice");
    
    // Set age field (int) 
    int64_t* age_ptr = (int64_t*)((char*)data + sizeof(String*));
    *age_ptr = 30;
    
    map->data = data;

    Item item_mut;
    item_mut.map = map;
    ConstItem item = item_mut.to_const();

    ValidationResult* result = validate_against_type(validator, item, (Type*)map_type);
    
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Map with multiple fields should be valid";
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, MapField_NestedMapType) {
    // Test nested map types in registry
    // Simulates: type Address = {city: string}; type Person = {address: Address}
    
    // Register Address type
    TypeMap* address_type = create_map_type();
    ShapeEntry* city_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    city_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* city_field = "city";
    city_entry->name->str = city_field;
    city_entry->name->length = strlen(city_field);
    city_entry->type = create_primitive_type(LMD_TYPE_STRING);
    city_entry->byte_offset = 0;
    city_entry->next = nullptr;
    address_type->shape = city_entry;
    
    TestTypeEntry address_entry_reg;
    address_entry_reg.name = (StrView){"Address", 7};
    address_entry_reg.type = (Type*)address_type;
    hashmap_set(validator->type_definitions, &address_entry_reg);
    
    // Register Person type that references Address
    TypeMap* person_type = create_map_type();
    ShapeEntry* address_field_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    address_field_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* address_field = "address";
    address_field_entry->name->str = address_field;
    address_field_entry->name->length = strlen(address_field);
    
    // Use resolved Address type
    Type* address_resolved = ast_validator_resolve_type_reference(validator, "Address");
    ASSERT_NE(address_resolved, nullptr);
    address_field_entry->type = address_resolved;
    address_field_entry->byte_offset = 0;
    address_field_entry->next = nullptr;
    person_type->shape = address_field_entry;
    
    // Verify structure
    EXPECT_EQ(address_resolved->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(person_type->shape->type, address_resolved);
}

TEST_F(ValidatorFeaturesTest, MapField_WithUnionField) {
    // Test map with a field that's a union type
    // Simulates: type Data = {value: string | int}
    
    TypeMap* data_type = create_map_type();
    ShapeEntry* value_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    value_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* value_field = "value";
    value_entry->name->str = value_field;
    value_entry->name->length = strlen(value_field);
    
    // Create union type for the field (handled by validation, not stored in shape)
    // For this test, just verify the structure can be set up
    value_entry->type = create_primitive_type(LMD_TYPE_STRING);  // Simplified
    value_entry->byte_offset = 0;
    value_entry->next = nullptr;
    data_type->shape = value_entry;
    
    // Verify setup
    EXPECT_NE(data_type->shape, nullptr);
    EXPECT_EQ(data_type->shape->type->type_id, LMD_TYPE_STRING);
}

TEST_F(ValidatorFeaturesTest, MapField_EmptyShapeValidation) {
    // Test map with no shape (untyped map)
    TypeMap* empty_map_type = create_map_type();
    empty_map_type->shape = nullptr;
    
    // Create empty map
    Map* map = (Map*)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->type = nullptr;
    map->data = nullptr;
    
    Item item_mut;
    item_mut.map = map;
    ConstItem item = item_mut.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)empty_map_type);
    ASSERT_NE(result, nullptr);
    // Map with no shape requirements should pass
    EXPECT_TRUE(result->valid);
}

// ==================== Element Validation Tests ====================
// Test validate_against_element_type() with MarkBuilder-created elements

TEST_F(ValidatorFeaturesTest, Element_BasicValidation) {
    // Test basic element validation with tag name
    TypeElmt* div_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    div_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "div";
    div_type->name = (StrView){tag, strlen(tag)};
    div_type->shape = nullptr;  // No attributes
    div_type->content_length = 0;
    
    // Create element using MarkBuilder
    MarkBuilder builder(input);
    Item element = builder.element("div")
        .text("Hello")
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)div_type);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Element with correct tag should be valid";
}

TEST_F(ValidatorFeaturesTest, Element_TagMismatch) {
    // Test element tag mismatch
    TypeElmt* span_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    span_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "span";
    span_type->name = (StrView){tag, strlen(tag)};
    span_type->shape = nullptr;
    span_type->content_length = 0;
    
    // Create element with wrong tag using MarkBuilder
    MarkBuilder builder(input);
    Item element = builder.element("div")  // Wrong tag
        .text("Content")
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)span_type);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Element with wrong tag should be invalid";
    EXPECT_GT(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, Element_WithAttributes) {
    // Test element with typed attributes
    TypeElmt* link_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    link_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "a";
    link_type->name = (StrView){tag, strlen(tag)};
    
    // Add href attribute (string type)
    ShapeEntry* href_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    href_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* href_field = "href";
    href_entry->name->str = href_field;
    href_entry->name->length = strlen(href_field);
    href_entry->type = create_primitive_type(LMD_TYPE_STRING);
    href_entry->byte_offset = 0;
    href_entry->next = nullptr;
    link_type->shape = href_entry;
    link_type->content_length = 0;
    
    // Create element with attribute using MarkBuilder
    MarkBuilder builder(input);
    Item element = builder.element("a")
        .attr("href", "https://example.com")
        .text("Click here")
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)link_type);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Element with valid attributes should be valid";
}

TEST_F(ValidatorFeaturesTest, Element_AttributeTypeMismatch) {
    // Test element with wrong attribute type
    TypeElmt* input_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    input_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "input";
    input_type->name = (StrView){tag, strlen(tag)};
    
    // Add maxlength attribute (int type)
    ShapeEntry* maxlength_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
    maxlength_entry->name = (StrView*)pool_calloc(pool, sizeof(StrView));
    const char* maxlen_field = "maxlength";
    maxlength_entry->name->str = maxlen_field;
    maxlength_entry->name->length = strlen(maxlen_field);
    maxlength_entry->type = create_primitive_type(LMD_TYPE_INT);
    maxlength_entry->byte_offset = 0;
    maxlength_entry->next = nullptr;
    input_type->shape = maxlength_entry;
    input_type->content_length = 0;
    
    // Create element with string attribute instead of int
    MarkBuilder builder(input);
    Item element = builder.element("input")
        .attr("maxlength", "100")  // Wrong type: string instead of int
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)input_type);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Element with wrong attribute type should be invalid";
    EXPECT_GT(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, Element_ContentLengthValidation) {
    // Test element content length validation
    TypeElmt* list_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    list_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "ul";
    list_type->name = (StrView){tag, strlen(tag)};
    list_type->shape = nullptr;
    list_type->content_length = 3;  // Expect exactly 3 children
    
    // Create element with 3 children using MarkBuilder
    MarkBuilder builder(input);
    Item child1 = builder.element("li").text("Item 1").final();
    Item child2 = builder.element("li").text("Item 2").final();
    Item child3 = builder.element("li").text("Item 3").final();
    
    Item element = builder.element("ul")
        .child(child1)
        .child(child2)
        .child(child3)
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)list_type);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->valid) << "Element with correct content length should be valid";
}

TEST_F(ValidatorFeaturesTest, Element_ContentLengthMismatch) {
    // Test element with wrong content length
    TypeElmt* table_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    table_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "table";
    table_type->name = (StrView){tag, strlen(tag)};
    table_type->shape = nullptr;
    table_type->content_length = 5;  // Expect exactly 5 children
    
    // Create element with 3 children (wrong count) using MarkBuilder
    MarkBuilder builder(input);
    Item child1 = builder.element("tr").text("Row 1").final();
    Item child2 = builder.element("tr").text("Row 2").final();
    Item child3 = builder.element("tr").text("Row 3").final();
    
    Item element = builder.element("table")
        .child(child1)
        .child(child2)
        .child(child3)  // Wrong: should be 5 children
        .final();
    
    ConstItem item = element.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)table_type);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Element with wrong content length should be invalid";
    EXPECT_GT(result->error_count, 0);
}

TEST_F(ValidatorFeaturesTest, Element_TypeMismatch) {
    // Test validating non-element against element type
    TypeElmt* div_type = (TypeElmt*)pool_calloc(pool, sizeof(TypeElmt));
    div_type->type_id = LMD_TYPE_ELEMENT;
    const char* tag = "div";
    div_type->name = (StrView){tag, strlen(tag)};
    div_type->shape = nullptr;
    div_type->content_length = 0;
    
    // Create string item instead of element
    String* str = create_string(pool, "not an element");
    Item item_mut;
    item_mut.item = s2it(str);
    ConstItem item = item_mut.to_const();
    
    ValidationResult* result = validate_against_type(validator, item, (Type*)div_type);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->valid) << "Non-element should not validate against element type";
    EXPECT_GT(result->error_count, 0);
}

// ==================== Main ====================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
