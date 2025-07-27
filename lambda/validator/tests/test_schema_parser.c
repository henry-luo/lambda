#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <mem_pool/mem_pool.h>
#include "../validator.h"

// Test fixtures
static VariableMemPool* test_pool = NULL;
static SchemaParser* parser = NULL;

void parser_test_setup(void) {
    pool_variable_init(&test_pool, 1024, 10);
    if (test_pool) {
        parser = schema_parser_create(test_pool);
    }
}

void parser_test_teardown(void) {
    if (parser) {
        schema_parser_destroy(parser);
        parser = NULL;
    }
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

TestSuite(parser_tests, .init = parser_test_setup, .fini = parser_test_teardown);

// ==================== Tree-sitter Integration Tests ====================

Test(parser_tests, tree_sitter_parser_integration) {
    // Test that schema parser uses the same Tree-sitter parser as Lambda
    cr_assert_not_null(parser, "Parser should be initialized");
    cr_assert_not_null(parser->base.parser, "Tree-sitter parser should be initialized");
    
    // Test parsing a simple schema
    const char* schema_source = "type SimpleString = string";
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Schema should be parsed with Tree-sitter");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_PRIMITIVE, "Should recognize primitive type");
}

Test(parser_tests, tree_sitter_node_source_extraction) {
    // Test the get_node_source helper function
    const char* schema_source = "type TestType = int";
    
    // This test assumes parse_schema_from_source stores the source
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    cr_assert_not_null(schema, "Schema should parse successfully");
    
    // Test that Tree-sitter successfully parsed the type
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_PRIMITIVE, "Should parse as primitive");
}

Test(parser_tests, tree_sitter_symbol_recognition) {
    // Test recognition of different Tree-sitter symbols
    const char* int_schema = "type IntType = int";
    const char* string_schema = "type StringType = string";  
    const char* float_schema = "type FloatType = float";
    
    TypeSchema* int_type = parse_schema_from_source(parser, int_schema);
    TypeSchema* string_type = parse_schema_from_source(parser, string_schema);
    TypeSchema* float_type = parse_schema_from_source(parser, float_schema);
    
    cr_assert_not_null(int_type, "Int schema should parse");
    cr_assert_not_null(string_type, "String schema should parse");
    cr_assert_not_null(float_type, "Float schema should parse");
    
    // All should be primitive types but with different underlying types
    cr_assert_eq(int_type->schema_type, LMD_SCHEMA_PRIMITIVE, "Int should be primitive");
    cr_assert_eq(string_type->schema_type, LMD_SCHEMA_PRIMITIVE, "String should be primitive");
    cr_assert_eq(float_type->schema_type, LMD_SCHEMA_PRIMITIVE, "Float should be primitive");
}

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

Test(parser_tests, tree_sitter_union_type_parsing) {
    // Test Tree-sitter parsing of union types with binary expressions
    const char* schema_source = "type StringOrInt = string | int";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Union schema should be parsed with Tree-sitter");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_UNION, "Should parse binary expression as union");
    
    SchemaUnion* union_data = (SchemaUnion*)schema->schema_data;
    cr_assert_eq(union_data->type_count, 2, "Union should have 2 types from binary expression");
}

Test(parser_tests, tree_sitter_nested_complex_types) {
    // Test Tree-sitter parsing of nested complex type structures
    const char* schema_source = "type ComplexType = {users: string*, metadata: {version: int, author: string}}";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Complex nested schema should parse with Tree-sitter");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_MAP, "Should parse as map type");
}

Test(parser_tests, tree_sitter_array_with_occurrence) {
    // Test Tree-sitter parsing of array types with occurrence modifiers
    const char* zero_or_more = "type ZeroOrMore = string*";
    const char* one_or_more = "type OneOrMore = string+";
    const char* optional = "type Optional = string?";
    
    TypeSchema* star_schema = parse_schema_from_source(parser, zero_or_more);
    TypeSchema* plus_schema = parse_schema_from_source(parser, one_or_more);
    TypeSchema* question_schema = parse_schema_from_source(parser, optional);
    
    cr_assert_not_null(star_schema, "* occurrence should parse");
    cr_assert_not_null(plus_schema, "+ occurrence should parse");  
    cr_assert_not_null(question_schema, "? occurrence should parse");
}

Test(parser_tests, tree_sitter_element_parsing) {
    // Test Tree-sitter parsing of element types
    const char* schema_source = "type HeaderElement = <header level: int, text: string>";
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    
    cr_assert_not_null(schema, "Element schema should parse with Tree-sitter");
    cr_assert_eq(schema->schema_type, LMD_SCHEMA_ELEMENT, "Should parse as element type");
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
    TypeDefinition* person_list_def = (TypeDefinition*)parser->type_definitions->data[1];
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
    TypeDefinition* person_def = (TypeDefinition*)parser->type_definitions->data[0];
    TypeDefinition* company_def = (TypeDefinition*)parser->type_definitions->data[1];
    TypeDefinition* document_def = (TypeDefinition*)parser->type_definitions->data[2];
    
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
