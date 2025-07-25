# Format Writer Guide

This document provides a guide for implementing new formatters in the Lambda project, incorporating lessons learned from implementing formatters for JSON, XML, HTML, YAML, TOML, etc.

## Overview

Formatters convert Lambda data structures (Items) into specific output formats. They follow a consistent pattern to ensure reliability and maintainability.

### Coding Guidelines
- Start comments in lowercase.
- Add debug prints to help debugging.
- Run tests with 3s timeout in case the code hang.

## Architecture

### Core Components

1. **Main Formatter Function**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
2. **Registration**: Add to `format_data()` dispatcher in `format.c`
3. **Data Type Support**: Handle all Lambda data types (see `transpiler.h` for complete enum)

## Implementation Approaches

Based on experience with existing formatters, there are two proven approaches:

### Approach 1: Direct Traversal (Recommended)

**Best for**: Non-JSON formats that require specific output structure (YAML, XML, TOML, etc.).

**Advantages**: 
- Complete control over output format
- No intermediate JSON conversion overhead
- Proper handling of format-specific requirements (indentation, syntax, etc.)
- Used successfully in the updated YAML formatter

**Pattern**: 
1. Implement `format_<type>_item()` function that handles all Lambda data types
2. Use `ShapeEntry` traversal for maps with proper loop termination checks
3. Handle arrays, strings, numbers, and other types directly
4. See `format-yaml.c` for complete implementation

### Approach 2: JSON Intermediate (Legacy)

**Best for**: Formats that are very similar to JSON structure.

**Pattern**: 
1. Use `print_named_items()` to generate JSON-like intermediate output
2. Post-process to convert to target format
3. Note: Only use when the target format closely matches JSON structure

## Implementation Steps

### 1. File Structure

Create `lambda/format/format-<name>.c` following the pattern in existing formatters:
- Include `../transpiler.h`
- Declare `print_named_items` function
- Use existing data extraction macros (see `format-json.c`)

### 2. Core Functions

**Main function**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
- See `format-json.c` for basic structure
- Handle root-level maps, arrays, and scalar types

**Item formatter**: `static void format_<name>_item(StrBuf* sb, Item item, int indent_level)`
- Handle all Lambda data types using switch statement on `get_type_id()`
- Reference the updated `format-yaml.c` for complete type handling

**Map formatter**: `static void format_<name>_map_items(StrBuf* sb, TypeMap* map_type, void* map_data, int indent_level)`
- Traverse `ShapeEntry` linked list safely
- Handle nested maps, field name formatting, and proper loop termination

**Helper functions**: Format-specific utilities for strings, numbers, containers
- Reference string escaping patterns in existing formatters

### 3. Registration

Add to `format_data()` function in `format.c`:
```c
else if (strcmp(type->chars, "<name>") == 0) {
    result = format_<name>(ctx->heap->pool, item);
}
```
## Critical Lessons from YAML Formatter Development

### Direct Traversal vs JSON Intermediate

**Key Insight**: The approach should match the output format requirements, not be driven by implementation convenience.

**For Non-JSON Formats**: Use direct traversal for proper format-specific output structure.
- **YAML Example**: Requires specific indentation, array bullets (`-`), and field separators (`:`)
- **XML Example**: Requires element tags, attributes, and hierarchical structure
- **Solution**: Implement `format_<type>_item()` that handles each Lambda data type appropriately

**For JSON-like Formats**: `print_named_items()` may be appropriate only if the output closely matches JSON structure.

### Safe Map Traversal Pattern

The updated YAML formatter demonstrates the correct pattern for `ShapeEntry` traversal:

```c
ShapeEntry *field = map_type->shape;
for (int i = 0; i < map_type->length && field; i++) {
    // Process field
    if (!field->name) {
        // Handle nested map
    } else {
        // Handle named field with proper type conversion
    }
    field = field->next;  // Safe traversal
}
```

**Key Points**:
- Always check both loop counter AND field pointer
- Handle both named fields and nested maps
- Convert raw data to proper Item format based on field type
- Include proper debug logging for troubleshooting

### Recommended Implementation Pattern

Based on the successful direct traversal YAML implementation:

1. **Implement format-specific item handler** that processes each Lambda data type
2. **Use safe ShapeEntry traversal** for maps with proper loop termination
3. **Handle data type conversion** from raw field data to Lambda Items
4. **Add comprehensive debug logging** to help with development and troubleshooting

**Example**: See the updated `format-yaml.c` for a complete implementation of this pattern.

## Best Practices

### Memory Management
- Use `strbuf_new_pooled(pool)` for all allocations
- Free temporary StrBuf objects with `strbuf_free()`
- Let `format_data()` handle final string registration

### Reliability (Critical)
- **Use direct traversal for non-JSON formats** - provides proper format control
- **Implement safe ShapeEntry traversal** with both counter and pointer checks
- **Convert field data to proper Lambda Items** based on field type
- **Add debug logging** for development and troubleshooting
- Test with comprehensive nested data structures (see `test/input/test.yaml`)

### Development Process
1. Start with direct traversal approach using `format_<type>_item()` pattern
2. Implement safe map traversal using `ShapeEntry` iteration
3. Add format-specific features (headers, escaping, indentation, etc.)
4. Add comprehensive debug logging
5. Test with both simple and complex nested data

## Testing

Create comprehensive test files like `test/lambda/input_<name>.ls`:
- Test all data types (strings, numbers, booleans, arrays, maps)
- Include nested structures and edge cases
- Verify output format correctness

## Implementation Quick Reference

### File References
- **JSON Formatter**: `format-json.c` - Basic reference implementation
- **YAML Formatter**: `format-yaml.c` - **Updated direct traversal implementation** (recommended pattern)
- **XML Formatter**: `format-xml.c` - Direct traversal for structured output
- **Registration**: `format.c` - Add new formatters to `format_data()`

### Key Implementation Functions
- **Data extraction macros**: See `format-json.c` for `get_pointer`, `get_int_value`, etc.
- **Main formatter**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
- **Item handler**: `static void format_<name>_item(StrBuf* sb, Item item, int indent_level)`
- **Map traversal**: Safe `ShapeEntry` iteration pattern from updated YAML formatter

### Data Type Handling
- **Extraction macros**: See `format-json.c` for `get_pointer`, `get_int_value`, etc.
- **Type enumeration**: Full list in `transpiler.h` enum `EnumTypeId`
- **String handling**: Each formatter implements format-specific escaping

## Conclusion

The Lambda formatter system prioritizes reliability and format-specific output quality. The key lessons from YAML development:

1. **Use direct traversal for non-JSON formats** - provides proper format control and avoids unnecessary JSON conversion
2. **Implement safe ShapeEntry traversal** - check both loop counter and field pointer to prevent infinite loops
3. **Handle data type conversion properly** - convert raw field data to Lambda Items based on field type
4. **Memory leaks** - Free temporary StrBuf objects
5. **Missing null checks** - Validate all pointer parameters
6. **Add comprehensive debug logging** - essential for development and troubleshooting
7. **Test with complex nested data early** - catches traversal and formatting issues
8. **Follow memory management patterns** - use pooled allocations and free temporary objects

For new formatters, start with the updated YAML implementation pattern (direct traversal) and adapt the format-specific logic for your target format. Only consider the JSON intermediate approach if your target format is extremely similar to JSON structure.