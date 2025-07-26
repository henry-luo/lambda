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
static SchemaValidator* validator = NULL;

void doc_schema_test_setup(void) {
    test_pool = make_pool_new();
    validator = schema_validator_create(test_pool);
    
    // Register doc schema validators
    register_doc_schema_validators(validator);
}

void doc_schema_test_teardown(void) {
    if (validator) {
        schema_validator_destroy(validator);
        validator = NULL;
    }
    if (test_pool) {
        pool_delete(&test_pool);
        test_pool = NULL;
    }
}

TestSuite(doc_schema_tests, .init = doc_schema_test_setup, .fini = doc_schema_test_teardown);

// ==================== Doc Schema Type Tests ====================

Test(doc_schema_tests, load_doc_schema) {
    // Load the actual doc schema from our doc_schema.ls file
    const char* doc_schema_source = 
        "type Document = {\n"
        "  meta: DocumentMeta,\n"
        "  body: BlockElement*\n"
        "}\n"
        "\n"
        "type DocumentMeta = {\n"
        "  title?: string,\n"
        "  author?: string*,\n"
        "  date?: string,\n"
        "  version?: string,\n"
        "  references?: Reference*\n"
        "}\n"
        "\n"
        "type Reference = {\n"
        "  id: string,\n"
        "  title: string,\n"
        "  author?: string*,\n"
        "  year?: int,\n"
        "  url?: string\n"
        "}\n"
        "\n"
        "type BlockElement = \n"
        "  | <header level: int, text: string>\n"
        "  | <paragraph content: InlineElement*>\n"
        "  | <list type: \"ordered\" | \"unordered\", items: ListItem*>\n"
        "  | <table headers: string*, rows: TableRow*>\n"
        "  | <codeblock language?: string, code: string>\n"
        "  | <quote content: InlineElement*, attribution?: string>\n"
        "\n"
        "type InlineElement =\n"
        "  | string\n"
        "  | <emphasis text: string>\n"
        "  | <strong text: string>\n"
        "  | <link url: string, text: string>\n"
        "  | <citation ref: string>\n"
        "  | <code text: string>\n"
        "\n"
        "type ListItem = {\n"
        "  content: InlineElement*,\n"
        "  children?: ListItem*\n"
        "}\n"
        "\n"
        "type TableRow = {\n"
        "  cells: string*\n"
        "}\n";
    
    int result = schema_validator_load_schema(validator, doc_schema_source, "DocSchema");
    cr_assert_eq(result, 0, "Doc schema should load successfully");
}

Test(doc_schema_tests, validate_simple_document) {
    // Load doc schema first
    const char* doc_schema_source = 
        "type Document = {\n"
        "  meta: {title: string},\n"
        "  body: <paragraph content: string>*\n"
        "}\n";
    
    int load_result = schema_validator_load_schema(validator, doc_schema_source, "Document");
    cr_assert_eq(load_result, 0, "Schema should load successfully");
    
    // Create a simple valid document
    Map* meta = map_new(test_pool);
    map_set(meta, s2it(create_string("title", 5, test_pool)),
            s2it(create_string("Test Document", 13, test_pool)));
    
    Element* paragraph = element_new(test_pool);
    paragraph->tag = create_string("paragraph", 9, test_pool);
    element_set_attr(paragraph, "content", s2it(create_string("Hello world", 11, test_pool)));
    
    List* body = list_new(test_pool);
    list_add(body, elmt2it(paragraph));
    
    Map* document = map_new(test_pool);
    map_set(document, s2it(create_string("meta", 4, test_pool)), m2it(meta));
    map_set(document, s2it(create_string("body", 4, test_pool)), l2it(body));
    
    ValidationResult* result = validate_document(validator, m2it(document), "Document");
    
    cr_assert_not_null(result, "Validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid document should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

// ==================== Citation Validation Tests ====================

Test(doc_schema_tests, validate_citations_valid_references) {
    // Create a document with valid citations
    Map* meta = map_new(test_pool);
    
    // Add references to meta
    List* references = list_new(test_pool);
    Map* ref1 = map_new(test_pool);
    map_set(ref1, s2it(create_string("id", 2, test_pool)), 
            s2it(create_string("ref1", 4, test_pool)));
    map_set(ref1, s2it(create_string("title", 5, test_pool)), 
            s2it(create_string("Test Paper", 10, test_pool)));
    list_add(references, m2it(ref1));
    
    map_set(meta, s2it(create_string("references", 10, test_pool)), l2it(references));
    
    // Create document with citation
    Element* citation = element_new(test_pool);
    citation->tag = create_string("citation", 8, test_pool);
    element_set_attr(citation, "ref", s2it(create_string("ref1", 4, test_pool)));
    
    Element* paragraph = element_new(test_pool);
    paragraph->tag = create_string("paragraph", 9, test_pool);
    List* content = list_new(test_pool);
    list_add(content, s2it(create_string("This is a test ", 15, test_pool)));
    list_add(content, elmt2it(citation));
    element_set_content(paragraph, content);
    
    List* body = list_new(test_pool);
    list_add(body, elmt2it(paragraph));
    
    Map* document = map_new(test_pool);
    map_set(document, s2it(create_string("meta", 4, test_pool)), m2it(meta));
    map_set(document, s2it(create_string("body", 4, test_pool)), l2it(body));
    
    ValidationResult* result = validate_citations(m2it(document), validator->context);
    
    cr_assert_not_null(result, "Citation validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid citations should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(doc_schema_tests, validate_citations_missing_reference) {
    // Create a document with citation referencing non-existent reference
    Map* meta = map_new(test_pool);
    List* references = list_new(test_pool);  // Empty references
    map_set(meta, s2it(create_string("references", 10, test_pool)), l2it(references));
    
    // Create document with citation to non-existent reference
    Element* citation = element_new(test_pool);
    citation->tag = create_string("citation", 8, test_pool);
    element_set_attr(citation, "ref", s2it(create_string("nonexistent", 11, test_pool)));
    
    Element* paragraph = element_new(test_pool);
    paragraph->tag = create_string("paragraph", 9, test_pool);
    List* content = list_new(test_pool);
    list_add(content, elmt2it(citation));
    element_set_content(paragraph, content);
    
    List* body = list_new(test_pool);
    list_add(body, elmt2it(paragraph));
    
    Map* document = map_new(test_pool);
    map_set(document, s2it(create_string("meta", 4, test_pool)), m2it(meta));
    map_set(document, s2it(create_string("body", 4, test_pool)), l2it(body));
    
    ValidationResult* result = validate_citations(m2it(document), validator->context);
    
    cr_assert_not_null(result, "Citation validation result should be returned");
    cr_assert_eq(result->valid, false, "Invalid citations should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
}

// ==================== Header Hierarchy Tests ====================

Test(doc_schema_tests, validate_header_hierarchy_valid) {
    // Create a document with valid header hierarchy (h1 -> h2 -> h3)
    Element* h1 = element_new(test_pool);
    h1->tag = create_string("header", 6, test_pool);
    element_set_attr(h1, "level", i2it(1));
    element_set_attr(h1, "text", s2it(create_string("Title", 5, test_pool)));
    
    Element* h2 = element_new(test_pool);
    h2->tag = create_string("header", 6, test_pool);
    element_set_attr(h2, "level", i2it(2));
    element_set_attr(h2, "text", s2it(create_string("Section", 7, test_pool)));
    
    Element* h3 = element_new(test_pool);
    h3->tag = create_string("header", 6, test_pool);
    element_set_attr(h3, "level", i2it(3));
    element_set_attr(h3, "text", s2it(create_string("Subsection", 10, test_pool)));
    
    List* body = list_new(test_pool);
    list_add(body, elmt2it(h1));
    list_add(body, elmt2it(h2));
    list_add(body, elmt2it(h3));
    
    ValidationResult* result = validate_header_hierarchy(l2it(body), validator->context);
    
    cr_assert_not_null(result, "Header validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid header hierarchy should pass");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(doc_schema_tests, validate_header_hierarchy_skip_level) {
    // Create a document with invalid header hierarchy (h1 -> h3, skipping h2)
    Element* h1 = element_new(test_pool);
    h1->tag = create_string("header", 6, test_pool);
    element_set_attr(h1, "level", i2it(1));
    element_set_attr(h1, "text", s2it(create_string("Title", 5, test_pool)));
    
    Element* h3 = element_new(test_pool);
    h3->tag = create_string("header", 6, test_pool);
    element_set_attr(h3, "level", i2it(3));
    element_set_attr(h3, "text", s2it(create_string("Subsection", 10, test_pool)));
    
    List* body = list_new(test_pool);
    list_add(body, elmt2it(h1));
    list_add(body, elmt2it(h3));
    
    ValidationResult* result = validate_header_hierarchy(l2it(body), validator->context);
    
    cr_assert_not_null(result, "Header validation result should be returned");
    cr_assert_eq(result->valid, false, "Invalid header hierarchy should fail");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
}

// ==================== Table Validation Tests ====================

Test(doc_schema_tests, validate_table_consistency_valid) {
    // Create a valid table with consistent column counts
    List* headers = list_new(test_pool);
    list_add(headers, s2it(create_string("Name", 4, test_pool)));
    list_add(headers, s2it(create_string("Age", 3, test_pool)));
    list_add(headers, s2it(create_string("City", 4, test_pool)));
    
    List* row1_cells = list_new(test_pool);
    list_add(row1_cells, s2it(create_string("John", 4, test_pool)));
    list_add(row1_cells, s2it(create_string("25", 2, test_pool)));
    list_add(row1_cells, s2it(create_string("NYC", 3, test_pool)));
    
    List* row2_cells = list_new(test_pool);
    list_add(row2_cells, s2it(create_string("Jane", 4, test_pool)));
    list_add(row2_cells, s2it(create_string("30", 2, test_pool)));
    list_add(row2_cells, s2it(create_string("LA", 2, test_pool)));
    
    Map* row1 = map_new(test_pool);
    map_set(row1, s2it(create_string("cells", 5, test_pool)), l2it(row1_cells));
    
    Map* row2 = map_new(test_pool);
    map_set(row2, s2it(create_string("cells", 5, test_pool)), l2it(row2_cells));
    
    List* rows = list_new(test_pool);
    list_add(rows, m2it(row1));
    list_add(rows, m2it(row2));
    
    Element* table = element_new(test_pool);
    table->tag = create_string("table", 5, test_pool);
    element_set_attr(table, "headers", l2it(headers));
    element_set_attr(table, "rows", l2it(rows));
    
    ValidationResult* result = validate_table_consistency(elmt2it(table), validator->context);
    
    cr_assert_not_null(result, "Table validation result should be returned");
    cr_assert_eq(result->valid, true, "Valid table should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(doc_schema_tests, validate_table_consistency_inconsistent_columns) {
    // Create a table with inconsistent column counts
    List* headers = list_new(test_pool);
    list_add(headers, s2it(create_string("Name", 4, test_pool)));
    list_add(headers, s2it(create_string("Age", 3, test_pool)));
    list_add(headers, s2it(create_string("City", 4, test_pool)));
    
    List* row1_cells = list_new(test_pool);
    list_add(row1_cells, s2it(create_string("John", 4, test_pool)));
    list_add(row1_cells, s2it(create_string("25", 2, test_pool)));
    // Missing third column
    
    List* row2_cells = list_new(test_pool);
    list_add(row2_cells, s2it(create_string("Jane", 4, test_pool)));
    list_add(row2_cells, s2it(create_string("30", 2, test_pool)));
    list_add(row2_cells, s2it(create_string("LA", 2, test_pool)));
    list_add(row2_cells, s2it(create_string("Extra", 5, test_pool)));
    // Extra column
    
    Map* row1 = map_new(test_pool);
    map_set(row1, s2it(create_string("cells", 5, test_pool)), l2it(row1_cells));
    
    Map* row2 = map_new(test_pool);
    map_set(row2, s2it(create_string("cells", 5, test_pool)), l2it(row2_cells));
    
    List* rows = list_new(test_pool);
    list_add(rows, m2it(row1));
    list_add(rows, m2it(row2));
    
    Element* table = element_new(test_pool);
    table->tag = create_string("table", 5, test_pool);
    element_set_attr(table, "headers", l2it(headers));
    element_set_attr(table, "rows", l2it(rows));
    
    ValidationResult* result = validate_table_consistency(elmt2it(table), validator->context);
    
    cr_assert_not_null(result, "Table validation result should be returned");
    cr_assert_eq(result->valid, false, "Inconsistent table should fail validation");
    cr_assert_gt(result->error_count, 0, "Should have at least one error");
}

// ==================== Metadata Validation Tests ====================

Test(doc_schema_tests, validate_metadata_completeness_valid) {
    Map* meta = map_new(test_pool);
    map_set(meta, s2it(create_string("title", 5, test_pool)),
            s2it(create_string("Complete Document", 17, test_pool)));
    
    List* authors = list_new(test_pool);
    list_add(authors, s2it(create_string("John Doe", 8, test_pool)));
    map_set(meta, s2it(create_string("author", 6, test_pool)), l2it(authors));
    
    map_set(meta, s2it(create_string("date", 4, test_pool)),
            s2it(create_string("2024-01-01", 10, test_pool)));
    
    ValidationResult* result = validate_metadata_completeness(m2it(meta), validator->context);
    
    cr_assert_not_null(result, "Metadata validation result should be returned");
    cr_assert_eq(result->valid, true, "Complete metadata should pass validation");
    cr_assert_eq(result->error_count, 0, "Should have no errors");
}

Test(doc_schema_tests, validate_metadata_completeness_missing_title) {
    Map* meta = map_new(test_pool);
    // Missing title - should generate warning
    
    List* authors = list_new(test_pool);
    list_add(authors, s2it(create_string("John Doe", 8, test_pool)));
    map_set(meta, s2it(create_string("author", 6, test_pool)), l2it(authors));
    
    ValidationResult* result = validate_metadata_completeness(m2it(meta), validator->context);
    
    cr_assert_not_null(result, "Metadata validation result should be returned");
    // This should generate a warning but still be valid
    cr_assert_eq(result->valid, true, "Missing title should only generate warning");
    cr_assert_gt(result->warning_count, 0, "Should have at least one warning");
}
