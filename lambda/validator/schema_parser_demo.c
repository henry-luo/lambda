/**
 * @file schema_parser_demo.c
 * @brief Demo of the Tree-sitter based schema parser
 */

#include "validator.h"
#include <stdio.h>

int main() {
    // Create a memory pool
    VariableMemPool pool;
    pool_init(&pool, 4096);
    
    // Create schema parser
    SchemaParser* parser = schema_parser_create(&pool);
    if (!parser) {
        printf("Failed to create schema parser\n");
        return 1;
    }
    
    // Test schema source
    const char* schema_source = "string | int";
    
    // Parse the schema
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    if (schema) {
        printf("Successfully parsed schema: %s\n", schema_source);
        printf("Schema type ID: %d\n", schema->schema_type);
    } else {
        printf("Failed to parse schema: %s\n", schema_source);
    }
    
    // Test type definition parsing
    TypeDefinition* def = build_type_definition(parser, (TSNode){0}); // null node for demo
    if (def) {
        printf("Successfully created type definition\n");
        printf("Type name: %.*s\n", (int)def->name.length, def->name.str);
    }
    
    // Clean up
    schema_parser_destroy(parser);
    pool_destroy(&pool);
    
    printf("Schema parser demo completed successfully!\n");
    return 0;
}
