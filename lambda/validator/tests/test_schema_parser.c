#include <criterion/criterion.h>
#include <criterion/new/assert.h>
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
}

// Test fixtures
static VariableMemPool* test_pool = NULL;
static SchemaParser* parser = NULL;

void parser_test_setup(void) {
    test_pool = make_pool_new();
    parser = schema_parser_create(test_pool);
}

void parser_test_teardown(void) {
    if (parser) {
        schema_parser_destroy(parser);
        parser = NULL;
    }
    if (test_pool) {
        pool_delete(&test_pool);
        test_pool = NULL;
    }
}

TestSuite(parser_tests, .init = parser_test_setup, .fini = parser_test_teardown);

// ==================== Schema Parser Creation Tests ====================

Test(parser_tests, create_parser) {
    cr_assert_not_null(parser, "Parser should be created successfully");
    cr_assert_not_null(parser->type_registry, "Parser should have type registry");
    cr_assert_not_null(parser->type_definitions, "Parser should have type definitions list");
}

// ==================== Basic Type Parsing Tests ====================

Test(parser_tests, parse_primitive_type) {
    const char* schema_source = "type SimpleString = string";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_PRIMITIVE, "Should parse as primitive type");
    
    SchemaPrimitive* prim_data = (SchemaPrimitive*)schema->schema_data;
    cr_assert_eq(prim_data->primitive_type, LMD_TYPE_STRING, "Should parse as string type");
}

Test(parser_tests, parse_union_type) {
    const char* schema_source = "type StringOrInt = string | int";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_UNION, "Should parse as union type");
    
    SchemaUnion* union_data = (SchemaUnion*)schema->schema_data;
    cr_assert_eq(union_data->type_count, 2, "Union should have 2 types");
    cr_assert_not_null(union_data->types, "Union should have types array");
}

Test(parser_tests, parse_array_type) {
    const char* schema_source = "type StringArray = string*";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_ARRAY, "Should parse as array type");
    
    SchemaArray* array_data = (SchemaArray*)schema->schema_data;
    cr_assert_eq(array_data->occurrence, '*', "Should parse '*' occurrence");
    cr_assert_not_null(array_data->element_type, "Array should have element type");
}

Test(parser_tests, parse_map_type) {
    const char* schema_source = "type PersonMap = {name: string, age: int}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_MAP, "Should parse as map type");
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    cr_assert_eq(map_data->field_count, 2, "Map should have 2 fields");
    cr_assert_not_null(map_data->fields, "Map should have fields");
}

Test(parser_tests, parse_element_type) {
    const char* schema_source = "type HeaderElement = <header level: int, text: string>";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_ELEMENT, "Should parse as element type");
    
    SchemaElement* element_data = (SchemaElement*)schema->schema_data;
    cr_assert_str_eq(element_data->tag.str, "header", "Should parse element tag");
    cr_assert_not_null(element_data->attributes, "Element should have attributes");
}

// ==================== Complex Type Parsing Tests ====================

Test(parser_tests, parse_nested_type) {
    const char* schema_source = 
        "type NestedType = {\n"
        "  items: {name: string, values: int*}*,\n"
        "  metadata: {title: string, tags: string*}\n"
        "}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Nested schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_MAP, "Should parse as map type");
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    cr_assert_eq(map_data->field_count, 2, "Map should have 2 fields");
    
    // Find the 'items' field
    SchemaMapField* items_field = map_data->fields;
    while (items_field && !strview_equals(items_field->name, strview_from_cstr("items"))) {
        items_field = items_field->next;
    }
    
    cr_assert_not_null(items_field, "Should find 'items' field");
    cr_assert_eq(items_field->type->schema_type, LMD_SCHEMA_ARRAY, "'items' should be array type");
}

Test(parser_tests, parse_occurrence_types) {
    const char* schema_source = 
        "type OccurrenceTypes = {\n"
        "  optional: string?,\n"
        "  one_or_more: string+,\n"
        "  zero_or_more: string*\n"
        "}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_MAP, "Should parse as map type");
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    cr_assert_eq(map_data->field_count, 3, "Map should have 3 fields");
    
    // Check occurrence modifiers
    SchemaMapField* field = map_data->fields;
    while (field) {
        if (strview_equals(field->name, strview_from_cstr("optional"))) {
            cr_assert_eq(field->type->schema_type, LMD_SCHEMA_OCCURRENCE, "Optional field should be occurrence type");
            SchemaOccurrence* occ_data = (SchemaOccurrence*)field->type->schema_data;
            cr_assert_eq(occ_data->modifier, '?', "Should parse '?' modifier");
        }
        field = field->next;
    }
}

// ==================== Reference Type Tests ====================

Test(parser_tests, parse_type_reference) {
    const char* schema_source = 
        "type Person = {name: string, age: int}\n"
        "type PersonList = Person*";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    
    // Should have parsed both type definitions
    cr_assert_eq(parser->type_definitions->length, 2, "Should have parsed 2 type definitions");
    
    // Check that PersonList references Person
    TypeDefinition* person_list_def = (TypeDefinition*)list_get(parser->type_definitions, 1);
    cr_assert_str_eq(person_list_def->name.str, "PersonList", "Second definition should be PersonList");
    cr_assert_eq(person_list_def->schema_type->schema_type, LMD_SCHEMA_ARRAY, "PersonList should be array");
    
    SchemaArray* array_data = (SchemaArray*)person_list_def->schema_type->schema_data;
    cr_assert_eq(array_data->element_type->schema_type, LMD_SCHEMA_REFERENCE, "Element should be reference");
}

// ==================== Error Handling Tests ====================

Test(parser_tests, parse_invalid_syntax) {
    const char* invalid_schema = "type Invalid = {invalid syntax here";
    
    TypeSchema* schema = parse_schema_from_source(parser, invalid_schema);
    
    cr_assert_null(schema, "Invalid syntax should return null");
}

Test(parser_tests, parse_unknown_primitive) {
    const char* schema_source = "type UnknownType = unknownprimitive";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    // This should either return null or create a reference type
    if (schema) {
        cr_assert_eq(schema->schema_type, LMD_SCHEMA_REFERENCE, "Unknown type should be treated as reference");
    }
}

// ==================== Multiple Type Definitions Tests ====================

Test(parser_tests, parse_multiple_definitions) {
    const char* schema_source = 
        "type Person = {name: string, age: int}\n"
        "type Company = {name: string, employees: Person*}\n"
        "type Document = {title: string, author: Person, company?: Company}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed successfully");
    cr_assert_eq(parser->type_definitions->length, 3, "Should have parsed 3 type definitions");
    
    // Check that all types are in the registry
    TypeDefinition* person_def = (TypeDefinition*)list_get(parser->type_definitions, 0);
    TypeDefinition* company_def = (TypeDefinition*)list_get(parser->type_definitions, 1);
    TypeDefinition* document_def = (TypeDefinition*)list_get(parser->type_definitions, 2);
    
    cr_assert_str_eq(person_def->name.str, "Person", "First definition should be Person");
    cr_assert_str_eq(company_def->name.str, "Company", "Second definition should be Company");
    cr_assert_str_eq(document_def->name.str, "Document", "Third definition should be Document");
}

// ==================== Schema Validation Tests ====================

Test(parser_tests, validate_parsed_schema_structure) {
    const char* schema_source = 
        "type ComplexType = {\n"
        "  id: string,\n"
        "  metadata: {\n"
        "    title: string,\n"
        "    tags: string*,\n"
        "    created: int\n"
        "  },\n"
        "  items: <item id: string, value: int | string>*\n"
        "}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Complex schema should be parsed successfully");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_MAP, "Should be map type");
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    cr_assert_eq(map_data->field_count, 3, "Should have 3 top-level fields");
    
    // Verify field structure
    SchemaMapField* field = map_data->fields;
    bool found_id = false, found_metadata = false, found_items = false;
    
    while (field) {
        if (strview_equals(field->name, strview_from_cstr("id"))) {
            found_id = true;
            cr_assert_eq(field->type->schema_type, LMD_SCHEMA_PRIMITIVE, "id should be primitive");
        } else if (strview_equals(field->name, strview_from_cstr("metadata"))) {
            found_metadata = true;
            cr_assert_eq(field->type->schema_type, LMD_SCHEMA_MAP, "metadata should be map");
        } else if (strview_equals(field->name, strview_from_cstr("items"))) {
            found_items = true;
            cr_assert_eq(field->type->schema_type, LMD_SCHEMA_ARRAY, "items should be array");
        }
        field = field->next;
    }
    
    cr_assert(found_id, "Should find 'id' field");
    cr_assert(found_metadata, "Should find 'metadata' field");
    cr_assert(found_items, "Should find 'items' field");
}

// ==================== Helper Functions ====================

// Helper function to create StrView from C string
StrView strview_from_cstr(const char* str) {
    StrView view = {.str = str, .length = str ? strlen(str) : 0};
    return view;
}

// Helper function to compare StrViews
bool strview_equals(StrView a, StrView b) {
    if (a.length != b.length) return false;
    return memcmp(a.str, b.str, a.length) == 0;
}
