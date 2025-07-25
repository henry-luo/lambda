# Format Writer Guide

This document provides a guide for implementing new formatters in the Lambda project, incorporating lessons learned from implementing formatters for JSON, XML, HTML, YAML, TOML, etc.

## Overview

Formatters convert Lambda data structures (Items) into specific output formats. They follow a consistent pattern to ensure reliability and maintainability.

### Coding Guidelines
- Start comments in lowercase.
- Add debug prints to help debugging.
- Run tests with 5s timeout in case the code hang.

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
## Critical Lessons from TOML Formatter Rewrite

### Safe Lambda Data Structure Traversal

The TOML formatter rewrite revealed critical insights about preventing code hangs and safely traversing Lambda data structures:

#### 1. Proper Type Identification and Extraction

**Root Cause of Hangs**: Incorrect type identification and data extraction methods.

**The Lambda Type System Challenge**: Lambda uses a mixed type system where some types store values directly in the Item, while others store pointers. Misunderstanding this leads to crashes and infinite loops.

**Safe Type Extraction Pattern**:
```c
// 1. Always extract type ID first via bit shifting
TypeId type = (TypeId)(item >> 56);

// 2. Use appropriate extraction method based on type category
switch (type) {
    case LMD_TYPE_BOOL:
    case LMD_TYPE_INT:
        // Packed types - value stored directly in item
        bool val = get_bool_value(item);
        int64_t val = get_int_value(item);
        break;
        
    case LMD_TYPE_STRING:
    case LMD_TYPE_FLOAT:
        // Packed pointer types - use get_pointer()
        String* str = (String*)get_pointer(item);
        double* dptr = (double*)get_pointer(item);
        break;
        
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
        // Pure pointer types - item IS the pointer
        Array* arr = (Array*)item;
        Map* map = (Map*)item;
        break;
}
```

#### 2. Robust Field Count and Loop Termination

**Critical Insight**: Field count mismatches and infinite loops were major hang sources.

**Safe ShapeEntry Traversal Pattern**:
```c
static void format_toml_attrs_from_shape(StrBuf* sb, TypeMap* type_map, void* data, const char* parent_name) {
    if (!type_map || !type_map->shape || !data) {
        return;  // Critical: Always validate pointers
    }
    
    ShapeEntry* shape_entry = type_map->shape;
    int field_count = 0;
    
    // Triple safety: field pointer + counter + reasonable limit
    while (shape_entry && field_count < 56) {  // 56 matches expected max field count
        if (shape_entry->name && shape_entry->name->str && shape_entry->name->length > 0) {
            // Safe data extraction with validation
            void* field_data = ((char*)data) + shape_entry->byte_offset;
            TypeId field_type = shape_entry->type->type_id;
            
            // Process field safely...
        }
        
        // Critical: Always advance both pointer and counter
        shape_entry = shape_entry->next;
        field_count++;
    }
}
```

#### 3. Recursive Depth Limiting with Context Preservation

**Key Learning**: The TOML formatter needed to handle nested sections like `[database.credentials]` while preventing infinite recursion.

**Safe Recursive Pattern with Parent Context**:
```c
static void format_toml_attrs_from_shape(StrBuf* sb, TypeMap* type_map, void* data, 
                                       const char* parent_name) {
    // Implicit depth limiting through parent_name chain length
    if (!type_map || !type_map->shape || !data) return;
    
    // For nested maps that should become table sections
    if (should_format_as_table_section) {
        // Build full section name with parent context
        char full_section_name[256];  // Fixed buffer prevents runaway allocation
        if (parent_name && strlen(parent_name) > 0) {
            snprintf(full_section_name, sizeof(full_section_name), "%s.%.*s", 
                parent_name, (int)shape_entry->name->length, shape_entry->name->str);
        } else {
            snprintf(full_section_name, sizeof(full_section_name), "%.*s", 
                (int)shape_entry->name->length, shape_entry->name->str);
        }
        
        // Recursive call with full context - depth naturally limited by string length
        format_toml_attrs_from_shape(sb, map_type_map, map->data, full_section_name);
    }
}
```

#### 4. Fallback Logic and Type Validation

**Major Insight**: The TOML formatter required robust fallback logic when type detection fails.

**Enhanced Type Detection with Fallbacks**:
```c
case LMD_TYPE_ARRAY: {
    Array* arr = (Array*)get_pointer(field_value);
    if (arr && arr->length > 0 && arr->length < 1000 && arr->items != NULL) {
        // Process valid array
        for (long i = 0; i < arr->length && i < 10; i++) {
            Item item = arr->items[i];
            TypeId item_type = (TypeId)(item >> 56);
            
            // Handle each item with type validation
            switch (item_type) {
                case LMD_TYPE_STRING:
                case LMD_TYPE_INT:
                case LMD_TYPE_BOOL:
                case LMD_TYPE_FLOAT:
                    // Handle known types
                    break;
                default: {
                    // Enhanced fallback for untyped items
                    void* item_ptr = get_pointer(item);
                    
                    // Try array interpretation with validation
                    Array* potential_array = (Array*)item_ptr;
                    if (potential_array && 
                        potential_array->length > 0 && 
                        potential_array->length < 1000 && 
                        potential_array->items != NULL) {
                        
                        // Validate first item to ensure array structure is reasonable
                        Item first_test = potential_array->items[0];
                        if ((first_test & 0xFF00000000000000ULL) != 0xFF00000000000000ULL) {
                            // Process as nested array
                        }
                    }
                    
                    // Try map interpretation
                    Map* potential_map = (Map*)item_ptr;
                    if (potential_map && potential_map->type && potential_map->data) {
                        // Process as inline table
                    }
                    
                    // Final fallback
                    strbuf_append_format(sb, "\"[type_%d]\"", (int)item_type);
                    break;
                }
            }
        }
    } else {
        strbuf_append_str(sb, "[]");  // Safe empty array fallback
    }
    break;
}
```

#### 5. Memory Safety and Pointer Validation

**Critical Pattern**: Always validate pointers before dereferencing, especially in complex nested structures.

**Safe Pointer Validation**:
```c
// Always validate pointer chains
Map* map = *(Map**)field_data;
if (map && map->type && map->data) {
    TypeMap* map_type_map = (TypeMap*)map->type;
    if (map_type_map->length > 0 && map_type_map->shape) {
        // Safe to process
    }
}

// Validate string pointers
String* str = *(String**)field_data;
if (str && str->len > 0) {
    // Safe to access str->chars
} else {
    // Safe fallback
    strbuf_append_str(sb, "\"\"");
}
```

#### 6. Format-Specific Decision Logic

**TOML-Specific Learning**: The formatter needed intelligent decisions about when to format maps as table sections vs inline tables.

**Smart Formatting Decisions**:
```c
// Criteria for table section formatting
bool should_format_as_table_section = false;
if (map_type_map->length >= 3) {  // 3 or more fields
    should_format_as_table_section = true;
} else {
    // Check for complex content (arrays or nested maps) even with fewer fields
    ShapeEntry* check_shape = map_type_map->shape;
    int check_count = 0;
    while (check_shape && check_count < map_type_map->length) {
        if (check_shape->type && 
            (check_shape->type->type_id == LMD_TYPE_ARRAY || 
             check_shape->type->type_id == LMD_TYPE_MAP)) {
            should_format_as_table_section = true;
            break;
        }
        check_shape = check_shape->next;
        check_count++;
    }
}
```

### Anti-Patterns That Cause Hangs

Based on the TOML formatter debugging experience:

1. **Wrong Type Extraction**: Using `get_pointer()` on pure pointer types like arrays/maps
2. **Missing Pointer Validation**: Not checking if pointers are valid before dereferencing
3. **Infinite Field Traversal**: Not checking both field pointer AND counter in loops
4. **Unlimited Recursion**: No depth limiting in recursive functions
5. **Invalid Array Processing**: Not validating array structure before iteration
6. **Memory Access Violations**: Dereferencing invalid pointers in complex nested structures

### Debugging Techniques That Work

**Essential for preventing hangs**:

1. **Incremental Testing**: Start with simple data, gradually add complexity
2. **Timeout Testing**: Always use `timeout 10s` during development
3. **Pointer Validation Logging**: Log pointer values before dereferencing
4. **Type ID Logging**: Log extracted type IDs to verify correct extraction
5. **Field Count Monitoring**: Log field counts vs expected counts
6. **Memory Pool Monitoring**: Watch for "Block not found in free list" warnings

### Lambda Type System: Packed vs Pointer Types

**Critical Understanding**: The Lambda type system has two distinct categories that must be handled differently:

**Packed Types** (value stored directly in Item):
- `LMD_TYPE_NULL`, `LMD_TYPE_BOOL`, `LMD_TYPE_INT`

**Packed Pointer Types** (type packed, data accessed via pointer):
- `LMD_TYPE_STRING`, `LMD_TYPE_INT64`, `LMD_TYPE_FLOAT`
- Use `get_pointer()` to extract data pointer

**Pure Pointer Types** (the Item IS the pointer):
- `LMD_TYPE_MAP`, `LMD_TYPE_ARRAY`, `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ELEMENT`
- Cast item directly: `Array* arr = (Array*)item;`

**Example from TOML formatter**:
```c
case LMD_TYPE_STRING: {
    String* str = (String*)get_pointer(item);  // Use get_pointer()
    format_toml_string(sb, str);
    break;
}
case LMD_TYPE_ARRAY: {
    Array* arr = (Array*)item;  // Direct cast - item IS the pointer
    format_toml_array(sb, arr, type);
    break;
}
```

**Reference**: Study `print_item_with_depth()` in `print.c` for correct Lambda type traversal patterns.

### Safe Map Traversal Pattern

The updated YAML and TOML formatters demonstrate the correct pattern for `ShapeEntry` traversal:

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

### Infinite Recursion Prevention

**Critical Issue**: Complex data structures can cause formatters to hang due to infinite recursion.

**Solution**: Implement depth limiting in recursive functions:
```c
static void format_toml_map_items_with_depth(StrBuf* sb, TypeMap* map_type, 
                                           void* map_data, const char* section_prefix, int depth) {
    if (!map_type || !map_data || depth > 10) {  // prevent infinite recursion
        return;
    }
    
    // Process map items...
    
    // Recursive call with incremented depth
    format_toml_map_items_with_depth(sb, nested_map_type, nested_data, key_name, depth + 1);
}
```

**Best Practice**: Always add depth limiting to any recursive formatter function.

### Recommended Implementation Pattern

Based on the successful direct traversal YAML and TOML implementations:

1. **Implement format-specific item handler** that processes each Lambda data type correctly
2. **Use safe ShapeEntry traversal** for maps with proper loop termination
3. **Handle Lambda type system properly** - distinguish packed vs pointer types
4. **Prevent infinite recursion** with depth limiting in recursive functions
5. **Add comprehensive debug logging** to help with development and troubleshooting

**Example**: See the updated `format-yaml.c` and `format-toml.c` for complete implementations of this pattern.

### Data Type Handling Best Practices

**Follow the Reference Implementation**: Always reference `print_item_with_depth()` in `print.c` for correct Lambda type traversal patterns.

**Correct Type Extraction**:
```c
// For packed types
bool val = get_bool_value(item);
int64_t val = get_int_value(item);

// For packed pointer types
String* str = (String*)get_pointer(item);
double* dptr = (double*)get_pointer(item);

// For pure pointer types - item IS the pointer
Array* arr = (Array*)item;
Map* mp = (Map*)item;
```

**Common Mistake**: Using `get_pointer()` for pure pointer types like arrays and maps - this was the root cause of the TOML formatter hang.

## Best Practices

### Memory Management
- Use `strbuf_new_pooled(pool)` for all allocations
- Free temporary StrBuf objects with `strbuf_free()`
- Let `format_data()` handle final string registration

### Reliability (Critical)
- **Use direct traversal for non-JSON formats** - provides proper format control
- **Understand Lambda type system** - distinguish packed vs pointer types correctly
- **Reference print_item_with_depth()** - use it as the authoritative guide for type traversal
- **Implement safe ShapeEntry traversal** with both counter and pointer checks
- **Add depth limiting** to prevent infinite recursion in complex nested structures
- **Convert field data to proper Lambda Items** based on field type
- **Add debug logging** for development and troubleshooting
- Test with comprehensive nested data structures and use timeouts to catch hangs

### Development Process
1. **Study the Lambda type system** - understand packed vs pointer types
2. **Reference print_item_with_depth()** - use as guide for correct type handling
3. Start with direct traversal approach using `format_<type>_item()` pattern
4. Implement safe map traversal using `ShapeEntry` iteration with depth limiting
5. Add format-specific features (headers, escaping, indentation, etc.)
6. Add comprehensive debug logging and use timeouts during testing
7. Test with both simple and complex nested data

### Common Pitfalls and Solutions

**Hanging/Infinite Loops**:
- **Cause**: Incorrect pointer type handling or missing depth limiting
- **Solution**: Study `print_item_with_depth()`, add depth checks, use timeouts in testing

**Incorrect Data Extraction**:
- **Cause**: Using wrong extraction method for Lambda type
- **Solution**: Follow the packed vs pointer type pattern, reference print.c

**Memory Issues**:
- **Cause**: Improper use of data extraction macros
- **Solution**: Use `get_pointer()` only for packed pointer types, not pure pointer types

## Testing

Create comprehensive test files like `test/lambda/input_<name>.ls`:
- Test all data types (strings, numbers, booleans, arrays, maps)
- Include nested structures and edge cases
- Verify output format correctness
- **Use timeouts during testing** to catch hanging formatters

### Debugging Techniques

**Essential for Formatter Development**:

1. **Add Debug Prints**: Include debug output to trace execution flow
```c
printf("format_toml_item: item %p, type = %d\n", (void*)item, type);
```

2. **Use Timeouts**: Test with timeout commands to catch hangs
```bash
timeout 10s ./lambda.exe test/lambda/input_toml.ls
```

3. **Test Incrementally**: Start with simple data, gradually add complexity
   - Test null/empty values first
   - Add simple arrays and maps
   - Finally test deeply nested structures

4. **Reference Implementation**: When debugging type issues, compare with `print_item_with_depth()`

5. **Memory Management**: Watch for free list warnings that indicate memory issues

## Implementation Quick Reference

### File References
- **JSON Formatter**: `format-json.c` - Basic reference implementation
- **YAML Formatter**: `format-yaml.c` - **Updated direct traversal implementation** (recommended pattern)
- **TOML Formatter**: `format-toml.c` - **Latest implementation with proper Lambda type handling**
- **XML Formatter**: `format-xml.c` - Direct traversal for structured output
- **Registration**: `format.c` - Add new formatters to `format_data()`
- **Lambda Type Reference**: `print.c` - See `print_item_with_depth()` for authoritative type handling

### Key Implementation Functions
- **Lambda Type Reference**: `print_item_with_depth()` in `print.c` - **Essential reference for correct type handling**
- **Data extraction macros**: See `format-json.c` for `get_pointer`, `get_int_value`, etc.
- **Main formatter**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
- **Item handler**: `static void format_<name>_item(StrBuf* sb, Item item, int indent_level)`
- **Map traversal**: Safe `ShapeEntry` iteration pattern with depth limiting

### Lambda Type System Handling
- **Packed Types**: `LMD_TYPE_NULL`, `LMD_TYPE_BOOL`, `LMD_TYPE_INT` - value in item
- **Packed Pointer Types**: `LMD_TYPE_STRING`, `LMD_TYPE_INT64`, `LMD_TYPE_FLOAT` - use `get_pointer()`
- **Pure Pointer Types**: `LMD_TYPE_MAP`, `LMD_TYPE_ARRAY`, `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ELEMENT` - direct cast
- **Reference Implementation**: Always consult `print_item_with_depth()` for correct patterns

## Conclusion

The Lambda formatter system prioritizes reliability and format-specific output quality. The key lessons from YAML and TOML development:

1. **Master the Lambda type system** - understand packed vs pointer types to prevent hangs and crashes
2. **Use print_item_with_depth() as reference** - it's the authoritative guide for correct type traversal
3. **Use direct traversal for non-JSON formats** - provides proper format control and avoids unnecessary JSON conversion
4. **Implement safe ShapeEntry traversal** - check both loop counter and field pointer to prevent infinite loops
5. **Add depth limiting to recursive functions** - essential for handling complex nested structures
6. **Handle data type conversion properly** - convert raw field data to Lambda Items based on field type
7. **Add comprehensive debug logging** - essential for development and troubleshooting
8. **Test with timeouts** - use 5-10 second timeouts to catch hanging formatters early
9. **Follow memory management patterns** - use pooled allocations and free temporary objects

**Critical Success Factors**:
- **Type System Mastery**: The distinction between packed and pointer types is fundamental
- **Reference Implementation**: Always consult `print_item_with_depth()` when in doubt
- **Depth Limiting**: Essential for preventing infinite recursion in complex data
- **Comprehensive Testing**: Test with nested data and use timeouts to catch issues

For new formatters, start with the updated TOML implementation pattern (`format-toml.c`) which demonstrates proper Lambda type handling, and adapt the format-specific logic for your target format. The TOML formatter represents the most recent and robust implementation incorporating all learned lessons.