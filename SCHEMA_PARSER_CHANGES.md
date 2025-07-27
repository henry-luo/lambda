# Lambda Schema Parser - Tree-sitter Integration

## Overview

The Lambda schema parser has been updated to use Tree-sitter for parsing schema definitions from source code, similar to the existing Lambda expression parser. This enables proper syntax tree-based parsing instead of simple text processing.

## Key Changes

### 1. Parser Infrastructure

- **SchemaParser Structure**: Extended to include Tree-sitter parser and current parsing state
  - `TSParser* parser` - Tree-sitter parser instance  
  - `const char* current_source` - Source code being parsed
  - `TSTree* current_tree` - Current syntax tree
  - `VariableMemPool* pool` - Memory pool for allocations

### 2. Tree-sitter Integration

- **parse_schema_from_source()**: Now uses `lambda_parse_source()` to generate syntax trees
- **Node Processing**: Schema types are built by traversing Tree-sitter AST nodes
- **Symbol Handling**: Uses Tree-sitter symbols (SYM_INT, SYM_STRING, etc.) to identify node types

### 3. Schema Building Functions

All schema building functions now work with Tree-sitter nodes:

- **build_primitive_schema()**: Handles primitive types (int, string, bool, etc.)
- **build_union_schema()**: Parses union types (`Type1 | Type2`)
- **build_array_schema()**: Parses array type definitions
- **build_map_schema()**: Parses map/object type definitions
- **build_reference_schema()**: Handles type references
- **build_element_schema()**: Parses XML-like element types

### 4. Helper Functions

- **get_node_source()**: Extracts source text from Tree-sitter nodes
- **build_type_definition()**: Creates complete type definitions with names and schemas

## Usage Example

```c
// Create parser
VariableMemPool pool;
pool_init(&pool, 4096);
SchemaParser* parser = schema_parser_create(&pool);

// Parse schema from source
const char* schema = "string | int";
TypeSchema* parsed_schema = parse_schema_from_source(parser, schema);

// Use the parsed schema for validation
// ... validation logic ...

// Cleanup
schema_parser_destroy(parser);
pool_destroy(&pool);
```

## Integration with Existing Code

The schema parser now follows the same patterns as the existing Lambda expression parser:

1. Uses the same Tree-sitter parser (`lambda_parser()`)
2. Uses the same symbol definitions (SYM_* constants)
3. Uses the same field access patterns (FIELD_* constants)
4. Follows the same memory management patterns

## Future Enhancements

- **Advanced Type Parsing**: Support for more complex type expressions
- **Error Reporting**: Better error messages with source location information
- **Schema Validation**: Validate schema definitions for correctness
- **Type Registry**: Cache and lookup parsed type definitions
- **Import/Export**: Support for modular schema definitions
