# Writing a Proper Formatter

This document summarizes the requirements and best practices for writing a formatter in the Lambda system.

## Overview

A formatter takes parsed data (as an `Item`) and converts it to a formatted string representation. The Lambda system currently supports JSON, YAML, and TOML formatters.

## Essential Components

### 1. Function Signature
```c
String* format_xyz(Item item)
```
Where `xyz` is the format name (json, yaml, toml, etc.).

### 2. Format Registration
The formatter must be registered in `lambda/format/format.c`:
```c
if (strcmp(format_str, "xyz") == 0) {
    return format_xyz(item);
}
```

### 3. Build System Integration
Add the formatter to the Makefile:
```makefile
SOURCES += lambda/format/format-xyz.c
```

## Core Implementation Requirements

### 1. Safe Data Access
- Use `print_named_items()` for map traversal instead of manual iteration
- This function handles memory management and type safety automatically
- Example from working formatters:
```c
void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);
```

### 2. Type Extraction Macros
Use the standard macros for extracting values from Items:
```c
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))
#define get_bool_value(item) ((bool)((item) & 0xFF))
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)
```

### 3. String Buffer Management
- Use `StrBuf` for building output strings
- Always call `strbuf_to_string()` to create the final result
- Handle memory allocation properly

## Implementation Patterns

### Pattern 1: Direct Formatting (JSON, simple cases)
For simple formats, directly format the data structure:
```c
String* format_json(Item item) {
    StrBuf* sb = strbuf_new();
    format_json_item(sb, item, 0);
    return strbuf_to_string(sb);
}
```

### Pattern 2: JSON-to-Format Conversion (YAML, TOML)
For complex formats, leverage the JSON formatter and convert:
```c
String* format_yaml(Item item) {
    // Use print_named_items for safe traversal
    StrBuf* temp_sb = strbuf_new();
    Map* map = (Map*)get_pointer(item);
    TypeMap* type_map = (TypeMap*)map->data;
    print_named_items(temp_sb, type_map, map);
    
    // Convert JSON-like output to YAML
    String* json_output = strbuf_to_string(temp_sb);
    StrBuf* yaml_sb = strbuf_new();
    convert_json_to_yaml(yaml_sb, json_output);
    return strbuf_to_string(yaml_sb);
}
```

## Critical Debugging Insights

### Memory Management Issues
- **Problem**: Manual map traversal can cause memory corruption
- **Solution**: Always use `print_named_items()` for map iteration
- **Symptoms**: "Block not found in free list" errors, hangs during formatting

### String Escaping
Each format requires specific string escaping rules:
- **JSON**: Escape `"`, `\`, newlines, tabs
- **YAML**: Handle multiline strings, special characters, document markers
- **TOML**: Escape quotes, handle multiline strings with proper delimiters

### Type Handling
- Integers: Use `get_int_value()` macro for safe extraction
- Floats: Handle special values (inf, -inf, nan)
- Booleans: Use `get_bool_value()` macro
- Strings: Always check for null pointers before accessing

## Testing Strategy

### 1. Create Comprehensive Test Files
- Simple cases: basic types, empty structures
- Complex cases: nested objects, arrays, special characters
- Edge cases: unicode, special values, large data

### 2. Test Script Pattern
```lambda
let data = input('./test/input/test.format', 'format')
"Parsing result:"
data
"Formatting as target:"
format(data, 'target')
"Test completed."
```

### 3. Debugging Process
1. Test parsing first with JSON output to isolate issues
2. Add debug logging if formatter hangs
3. Use timeout commands to capture partial output
4. Compare with reference implementations

## Common Pitfalls

### 1. Manual Map Iteration
❌ **Don't do this:**
```c
// Manual iteration - can cause memory issues
ShapeEntry* entries = type_map->data.shape.entries;
for (int i = 0; i < count; i++) {
    // Direct access can hang or corrupt memory
}
```

✅ **Do this instead:**
```c
// Safe traversal using tested infrastructure
print_named_items(strbuf, type_map, map_data);
```

### 2. Ignoring Memory Warnings
- "Block not found in free list" warnings indicate memory management issues
- "Invalid block size detected" errors suggest corruption
- These often point to unsafe data structure access

### 3. Incomplete String Handling
- Always handle null strings gracefully
- Implement proper escaping for the target format
- Consider multiline string formatting requirements

## Successful Examples

### YAML Formatter
- Uses `print_named_items()` for safe traversal
- Converts JSON-like output to YAML format
- Handles document markers and proper indentation
- **Status**: Working correctly

### JSON Formatter
- Direct implementation with recursive formatting
- Proper string escaping and type handling
- **Status**: Reference implementation, fully working

### TOML Formatter
- **Status**: Partially working (parsing works, formatting has memory issues)
- **Issue**: Needs conversion to use `print_named_items()` pattern
- **Fix**: Apply the same pattern as YAML formatter

## Best Practices Summary

1. **Always use `print_named_items()` for map traversal**
2. **Test parsing separately from formatting**
3. **Handle edge cases gracefully (null strings, special values)**
4. **Implement proper string escaping for the target format**
5. **Use timeout commands during development to catch hangs**
6. **Follow the JSON-to-format conversion pattern for complex formats**
7. **Create comprehensive test files covering all data types**
8. **Add debug logging temporarily when troubleshooting**

## Format-Specific Notes

### YAML
- Requires document start marker (`---`)
- Uses indentation for structure
- Comments start with `#` (lowercase as requested)
- Handles multiline strings with `|` or `>` indicators

### TOML
- Uses `key = value` syntax
- Tables defined with `[table.name]`
- Arrays use `[item1, item2, item3]` syntax
- Strings must be properly quoted and escaped

### JSON
- Strict syntax requirements
- All strings must be quoted and escaped
- No comments allowed
- Trailing commas not permitted

This documentation reflects lessons learned from implementing and debugging multiple formatters in the Lambda system.
