# Format Writer Guide

This document provides a guide for implementing new formatters in the Lambda project, incorporating lessons learned from implementing formatters for JSON, XML, HTML, YAML, TOML, etc.

## Overview

Formatters convert Lambda data structures (Items) into specific output formats. They follow a consistent pattern to ensure reliability and maintainability.

### Coding Guidelines
- Start comments in lowercase.
- **Add debug logging** for development and troubleshooting.
- **Test with comprehensive nested data structures** and use timeouts to catch hangs early
- **Back up the file** before major refactoring or rewrite. Remove the backup at the end of successful refactoring or rewrite.

## Architecture

### Core Components

**Primary Reference**: See `data_struct.md` for comprehensive documentation on Lambda data structures, types, and memory layout.
- **Type Detection**: Always use `get_type_id()` function from `LambdaItem` struct
- **Data Extraction**: Extract data pointer based on TypeId
- **Reference Implementation**: Always consult `print_item_with_depth()` for correct patterns

1. **Main Formatter Function**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
2. **Registration**: Add to `format_data()` dispatcher in `format.c`
3. **Data Type Support**: Handle all Lambda data types (see `transpiler.h` for complete enum)

## Implementation Approaches

### Approach 1: Direct Traversal (Recommended)

**Best for**: Non-JSON formats that require specific output structure (YAML, XML, TOML, etc.).

**Advantages**: 
- Complete control over output format
- No intermediate JSON conversion overhead
- Proper handling of format-specific requirements (indentation, syntax, etc.)

**Pattern**: 
1. Implement central `format_item()` function that handles all Lambda data types
2. Use `ShapeEntry` traversal for maps with proper loop termination checks
3. Handle arrays, strings, numbers, and other types directly
4. Reference `format-yaml.c` and `format-toml.c` for complete implementations

### Approach 2: JSON Intermediate (Legacy)

**Best for**: Formats that are very similar to JSON structure.

**Pattern**: 
1. Use `print_named_items()` to generate JSON-like intermediate output
2. Post-process to convert to target format
3. Note: Only use when the target format closely matches JSON structure

## Implementation Requirements

### 1. File Structure
- Create `lambda/format/format-<name>.c`
- Include `../transpiler.h`
- Reference existing formatters for patterns

### 2. Core Functions
- **Main function**: Handle root-level maps, arrays, and scalar types
- **Central item formatter**: Process all Lambda data types using `get_type_id()`
- **Map formatter**: Traverse `ShapeEntry` linked list safely with field count validation
- **Helper functions**: Format-specific utilities for strings, numbers, containers

### 3. Registration
- Add to `format_data()` function in `format.c`

## Critical Implementation Lessons

### Safe Lambda Data Structure Traversal

Recent formatter implementations revealed critical insights about preventing code hangs and safely traversing Lambda data structures.

### 1. Proper Type Identification and Extraction

**Root Cause of Hangs**: Incorrect type identification and data extraction methods are the primary cause of infinite loops and crashes.

**Critical Discovery**: Use `get_type_id()` function from `LambdaItem` struct for reliable type extraction, not manual bit shifting.

See `format-toml.c` for complete type extraction patterns.

### 2. Safe Field Traversal and Loop Termination

**Critical Requirements**:
- Use `type_map->length` as the authoritative field count
- Validate field count matches expectations
- Always advance both pointer and counter in ShapeEntry traversal

Reference `format-toml.c` for safe traversal implementation.

### 3. Robust Item Creation from Field Data

**Major Breakthrough**: Implement a robust `create_item_from_field_data()` function that properly handles the Lambda type system by converting raw field data to proper Lambda Items with correct type tagging.

See `format-toml.c` for complete implementation.

### 4. Depth Limiting and Recursion Prevention

- Implement depth limiting in recursive functions (depth > 10)
- Use fixed buffers to prevent runaway allocation
- Handle nested structures with parent context preservation

### 5. Modular Design Patterns

**Success Pattern**: Central `format_item()` function with format-specific helper functions:
- Single entry point for all data type formatting
- Consistent error handling and depth limiting
- Modular helper functions for complex types

Reference `format-toml.c` for complete modular design implementation.

### Memory Management
- Use `strbuf_new_pooled(pool)` for all allocations
- Free temporary StrBuf objects with `strbuf_free()`
- Let `format_data()` handle final string registration

## Testing and Debugging

### Testing and Debugging
- **Test Incrementally**: Start with simple data, gradually add complexity
- **Add Debug Prints**: Include debug output to trace execution flow
- Create comprehensive test files like `test/lambda/input_<name>.ls`
- Test all data types (strings, numbers, booleans, arrays, maps)
- Include nested structures and edge cases
- Verify output format correctness
- **Use timeouts during testing** to catch hanging formatters (`timeout 5s`)
- **Reference Implementation**: When debugging type issues, compare with `print_item_with_depth()`

## Implementation Quick Reference

### File References
- **Basic Pattern**: `format-json.c` - Basic reference implementation
- **Direct Traversal**: `format-yaml.c` - Updated direct traversal implementation
- **Latest Best Practices**: `format-toml.c` - Most recent implementation with proper Lambda type handling
- **Registration**: `format.c` - Add new formatters to `format_data()`
- **Lambda Type Reference**: `print.c` - See `print_item_with_depth()` for authoritative type handling

## Conclusion

The Lambda formatter system prioritizes reliability and format-specific output quality.

For new formatters, start with the latest implementation patterns in `format-toml.c` which demonstrate proper Lambda API usage and robust type handling, then adapt the format-specific logic for your target format.