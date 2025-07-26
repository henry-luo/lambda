#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mem_pool/mem_pool.h>
#include "../validator.h"

// Test fixtures
static VariableMemPool* test_pool;

void setup(void) {
    pool_variable_init(&test_pool, 1024, 10);
}

void teardown(void) {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}ion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../validator.h"

// Test fixtures and setup
static VariableMemPool* test_pool = NULL;
static SchemaValidator* validator = NULL;

void validator_test_setup(void) {
    test_pool = make_pool_new();
    validator = schema_validator_create(test_pool);
}

void validator_test_teardown(void) {
    if (validator) {
        schema_validator_destroy(validator);
        validator = NULL;
    }
    if (test_pool) {
        pool_delete(&test_pool);
        test_pool = NULL;
    }
}

TestSuite(validator_tests, .init = validator_test_setup, .fini = validator_test_teardown);

// ==================== Basic Validator Tests ====================

Test(validator_tests, create_validator) {
    cr_assert_not_null(validator, "Validator should be created successfully");
    cr_assert_not_null(validator->pool, "Validator should have a memory pool");
    cr_assert_not_null(validator->schemas, "Validator should have schema registry");
    cr_assert_not_null(validator->context, "Validator should have validation context");
}

Test(validator_tests, create_validation_result) {
    ValidationResult* result = create_validation_result(test_pool);
    
    cr_assert_not_null(result, "Validation result should be created");
    cr_assert_eq(result->valid, true, "New validation result should be valid");
    cr_assert_eq(result->error_count, 0, "New result should have no errors");
    cr_assert_eq(result->warning_count, 0, "New result should have no warnings");
    cr_assert_null(result->errors, "New result should have no error list");
    cr_assert_null(result->warnings, "New result should have no warning list");
}

// ==================== Schema Type Creation Tests ====================

Test(validator_tests, create_primitive_schema) {
    TypeSchema* schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    
    cr_assert_not_null(schema, "Primitive schema should be created");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_PRIMITIVE, "Should be primitive schema type");
    cr_assert_not_null(schema->schema_data, "Schema should have data");
    
    SchemaPrimitive* prim_data = (SchemaPrimitive*)schema->schema_data;
    cr_assert_eq(prim_data->primitive_type, LMD_TYPE_STRING, "Should store correct primitive type");
}

Test(validator_tests, create_array_schema) {
    TypeSchema* element_schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    TypeSchema* array_schema = create_array_schema(element_schema, 0, -1, test_pool);
    
    cr_assert_not_null(array_schema, "Array schema should be created");
    cr_assert_eq(array_schema->schema_type, LMD_SCHEMA_ARRAY, "Should be array schema type");
    
    SchemaArray* array_data = (SchemaArray*)array_schema->schema_data;
    cr_assert_eq(array_data->element_type, element_schema, "Should store element type");
}

Test(validator_tests, create_union_schema) {
    List* types = list_new(test_pool);
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* int_schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    
    list_add(types, (Item)string_schema);
    list_add(types, (Item)int_schema);
    
    TypeSchema* union_schema = create_union_schema(types, test_pool);
    
    cr_assert_not_null(union_schema, "Union schema should be created");
    cr_assert_eq(union_schema->schema_type, LMD_SCHEMA_UNION, "Should be union schema type");
    
    SchemaUnion* union_data = (SchemaUnion*)union_schema->schema_data;
    cr_assert_eq(union_data->type_count, 2, "Should have correct type count");
}

// ==================== Primitive Validation Tests ====================

Test(validator_tests, validate_string_primitive) {
    TypeSchema* schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    String* test_string = create_string("hello", 5, test_pool);
    Item test_item = s2it(test_string);
    
    ValidationResult* result = validate_primitive(test_item, schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid string should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(validator_tests, validate_string_primitive_type_mismatch) {
    TypeSchema* schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    Item test_item = i2it(42);  // Integer instead of string
    
    ValidationResult* result = validate_primitive(test_item, schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Type mismatch should fail validation");
    cr_assert_eq(result->error_count, 1, "Should have one error");
    cr_assert_not_null(result->errors, "Should have error list");
    cr_assert_eq(result->errors->code, VALID_ERROR_TYPE_MISMATCH, "Should be type mismatch error");
}

Test(validator_tests, validate_int_primitive) {
    TypeSchema* schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    Item test_item = i2it(42);
    
    ValidationResult* result = validate_primitive(test_item, schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid integer should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

// ==================== Array Validation Tests ====================

Test(validator_tests, validate_array_empty) {
    TypeSchema* element_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* array_schema = create_array_schema(element_schema, 0, -1, test_pool);
    
    List* empty_list = list_new(test_pool);
    Item array_item = l2it(empty_list);
    
    ValidationResult* result = validate_array(array_item, array_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Empty array should be valid");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(validator_tests, validate_array_with_valid_elements) {
    TypeSchema* element_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* array_schema = create_array_schema(element_schema, 0, -1, test_pool);
    
    List* test_list = list_new(test_pool);
    list_add(test_list, s2it(create_string("hello", 5, test_pool)));
    list_add(test_list, s2it(create_string("world", 5, test_pool)));
    
    Item array_item = l2it(test_list);
    
    ValidationResult* result = validate_array(array_item, array_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Array with valid elements should pass");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(validator_tests, validate_array_with_invalid_element) {
    TypeSchema* element_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* array_schema = create_array_schema(element_schema, 0, -1, test_pool);
    
    List* test_list = list_new(test_pool);
    list_add(test_list, s2it(create_string("hello", 5, test_pool)));
    list_add(test_list, i2it(42));  // Invalid: integer in string array
    
    Item array_item = l2it(test_list);
    
    ValidationResult* result = validate_array(array_item, array_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Array with invalid element should fail");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
}

Test(validator_tests, validate_array_occurrence_plus_empty) {
    TypeSchema* element_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* array_schema = create_array_schema(element_schema, 1, -1, test_pool);
    
    // Set occurrence to '+' (at least one element required)
    SchemaArray* array_data = (SchemaArray*)array_schema->schema_data;
    array_data->occurrence = '+';
    
    List* empty_list = list_new(test_pool);
    Item array_item = l2it(empty_list);
    
    ValidationResult* result = validate_array(array_item, array_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Empty array with '+' occurrence should fail");
    cr_assert_eq(result->error_count, 1, "Should have one error");
    cr_assert_eq(result->errors->code, VALID_ERROR_OCCURRENCE_ERROR, "Should be occurrence error");
}

// ==================== Union Validation Tests ====================

Test(validator_tests, validate_union_first_type_matches) {
    List* types = list_new(test_pool);
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* int_schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    
    list_add(types, (Item)string_schema);
    list_add(types, (Item)int_schema);
    
    TypeSchema* union_schema = create_union_schema(types, test_pool);
    String* test_string = create_string("hello", 5, test_pool);
    Item test_item = s2it(test_string);
    
    ValidationResult* result = validate_union(test_item, union_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "String matching first union type should pass");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(validator_tests, validate_union_second_type_matches) {
    List* types = list_new(test_pool);
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* int_schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    
    list_add(types, (Item)string_schema);
    list_add(types, (Item)int_schema);
    
    TypeSchema* union_schema = create_union_schema(types, test_pool);
    Item test_item = i2it(42);
    
    ValidationResult* result = validate_union(test_item, union_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Integer matching second union type should pass");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(validator_tests, validate_union_no_type_matches) {
    List* types = list_new(test_pool);
    TypeSchema* string_schema = create_primitive_schema(LMD_TYPE_STRING, test_pool);
    TypeSchema* int_schema = create_primitive_schema(LMD_TYPE_INT, test_pool);
    
    list_add(types, (Item)string_schema);
    list_add(types, (Item)int_schema);
    
    TypeSchema* union_schema = create_union_schema(types, test_pool);
    Item test_item = f2it(3.14);  // Float not in union
    
    ValidationResult* result = validate_union(test_item, union_schema, validator->context);
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, false, "Type not in union should fail");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
}

// ==================== Error Path Tests ====================

Test(validator_tests, error_path_field) {
    PathSegment* path = create_path_segment(PATH_FIELD, test_pool);
    path->data.field_name = strview_from_cstr("test_field");
    
    String* path_str = format_validation_path(path, test_pool);
    
    cr_assert_not_null(path_str, "Path string should be created");
    cr_assert_str_eq(path_str->chars, ".test_field", "Field path should be formatted correctly");
}

Test(validator_tests, error_path_index) {
    PathSegment* path = create_path_segment(PATH_INDEX, test_pool);
    path->data.index = 42;
    
    String* path_str = format_validation_path(path, test_pool);
    
    cr_assert_not_null(path_str, "Path string should be created");
    cr_assert_str_eq(path_str->chars, "[42]", "Index path should be formatted correctly");
}

Test(validator_tests, error_path_nested) {
    PathSegment* field_path = create_path_segment(PATH_FIELD, test_pool);
    field_path->data.field_name = strview_from_cstr("items");
    
    PathSegment* index_path = create_path_segment(PATH_INDEX, test_pool);
    index_path->data.index = 1;
    index_path->next = field_path;
    
    PathSegment* inner_field_path = create_path_segment(PATH_FIELD, test_pool);
    inner_field_path->data.field_name = strview_from_cstr("name");
    inner_field_path->next = index_path;
    
    String* path_str = format_validation_path(inner_field_path, test_pool);
    
    cr_assert_not_null(path_str, "Path string should be created");
    cr_assert_str_eq(path_str->chars, ".name[1].items", "Nested path should be formatted correctly");
}

// ==================== Schema Loading Tests ====================

Test(validator_tests, load_simple_schema) {
    const char* schema_source = 
        "type SimpleString = string\n"
        "type SimpleInt = int\n";
    
    int result = schema_validator_load_schema(validator, schema_source, "test_schema");
    
    cr_assert_eq(result, 0, "Schema should load successfully");
    
    // Check that schema is stored in registry
    StrView schema_key = strview_from_cstr("test_schema");
    TypeSchema* loaded_schema = NULL;
    bool found = hashmap_get(validator->schemas, &schema_key, &loaded_schema);
    
    cr_assert_eq(found, true, "Schema should be found in registry");
    cr_assert_not_null(loaded_schema, "Loaded schema should not be null");
}

// ==================== Integration Tests ====================

Test(validator_tests, validate_document_with_schema) {
    // Load a simple schema
    const char* schema_source = "type TestDoc = {title: string, content: string}";
    int load_result = schema_validator_load_schema(validator, schema_source, "TestDoc");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Create a test document that matches the schema
    Map* doc_map = map_new(test_pool);
    map_set(doc_map, s2it(create_string("title", 5, test_pool)), 
            s2it(create_string("Test Title", 10, test_pool)));
    map_set(doc_map, s2it(create_string("content", 7, test_pool)), 
            s2it(create_string("Test content", 12, test_pool)));
    
    Item doc_item = m2it(doc_map);
    
    ValidationResult* result = validate_document(validator, doc_item, "TestDoc");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid document should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

// ==================== Utility Function Tests ====================

Test(validator_tests, is_compatible_type_exact_match) {
    bool result = is_compatible_type(LMD_TYPE_STRING, LMD_TYPE_STRING);
    cr_assert_eq(result, true, "Exact type match should be compatible");
}

Test(validator_tests, is_compatible_type_number_int) {
    bool result = is_compatible_type(LMD_TYPE_INT, LMD_TYPE_NUMBER);
    cr_assert_eq(result, true, "Int should be compatible with number");
}

Test(validator_tests, is_compatible_type_number_float) {
    bool result = is_compatible_type(LMD_TYPE_FLOAT, LMD_TYPE_NUMBER);
    cr_assert_eq(result, true, "Float should be compatible with number");
}

Test(validator_tests, is_compatible_type_any) {
    bool result = is_compatible_type(LMD_TYPE_STRING, LMD_TYPE_ANY);
    cr_assert_eq(result, true, "Any type should accept anything");
}

Test(validator_tests, is_compatible_type_mismatch) {
    bool result = is_compatible_type(LMD_TYPE_STRING, LMD_TYPE_INT);
    cr_assert_eq(result, false, "String should not be compatible with int");
}

// ==================== Helper Functions ====================

// Helper function to create primitive schemas for testing
TypeSchema* create_primitive_schema(TypeId primitive_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_PRIMITIVE;
    
    SchemaPrimitive* prim_data = (SchemaPrimitive*)pool_calloc(pool, sizeof(SchemaPrimitive));
    prim_data->primitive_type = primitive_type;
    schema->schema_data = prim_data;
    
    return schema;
}

// Helper function to create array schemas for testing
TypeSchema* create_array_schema(TypeSchema* element_type, long min_len, long max_len, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_ARRAY;
    
    SchemaArray* array_data = (SchemaArray*)pool_calloc(pool, sizeof(SchemaArray));
    array_data->element_type = element_type;
    array_data->occurrence = (min_len > 0) ? '+' : '*';
    schema->schema_data = array_data;
    
    return schema;
}

// Helper function to create union schemas for testing
TypeSchema* create_union_schema(List* types, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_UNION;
    
    SchemaUnion* union_data = (SchemaUnion*)pool_calloc(pool, sizeof(SchemaUnion));
    union_data->type_count = (int)types->length;
    union_data->types = (TypeSchema**)pool_calloc(pool, 
        sizeof(TypeSchema*) * union_data->type_count);
    
    for (int i = 0; i < union_data->type_count; i++) {
        union_data->types[i] = (TypeSchema*)list_get(types, i);
    }
    
    schema->schema_data = union_data;
    return schema;
}
