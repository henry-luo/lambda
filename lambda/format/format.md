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

### Memory Management
- Use `strbuf_new_pooled(pool)` for all allocations
- Free temporary StrBuf objects with `strbuf_free()`
- Let `format_data()` handle final string registration

### Testing and Debugging
- **Test Incrementally**: Start with simple data, gradually add complexity
- **Add Debug Prints**: Include debug output to trace execution flow
- Create comprehensive test files like `test/lambda/input_<name>.ls`
- Test all data types (strings, numbers, booleans, arrays, maps)
- Include nested structures and edge cases
- Verify output format correctness
- **Use timeouts during testing** to catch hanging formatters (`timeout 5s`)
- **Reference Implementation**: When debugging type issues, compare with `print_item_with_depth()`

### File References
- **Basic Pattern**: `format-json.c` - Basic reference implementation
- **Direct Traversal**: `format-yaml.c` - Updated direct traversal implementation
- **Latest Best Practices**: `format-toml.c` - Most recent implementation with proper Lambda type handling
- **Registration**: `format.c` - Add new formatters to `format_data()`
- **Lambda Type Reference**: `print.c` - See `print_item_with_depth()` for authoritative type handling
