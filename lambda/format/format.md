# Format Writer Guide

This document provides a comprehensive guide for implementing new formatters in the Lambda project.

## Overview

Formatters in Lambda are responsible for converting Lambda data structures (Items) into specific output formats like JSON, XML, HTML, YAML, etc. They follow a consistent pattern and interface to ensure reliability and maintainability.

## Architecture

### Core Components

1. **Main Formatter Function**: `String* format_<name>(VariableMemPool* pool, Item root_item)`
2. **Registration**: Add to `format_data()` dispatcher in `format.c`
3. **Helper Functions**: Format-specific utility functions
4. **Data Type Handling**: Support for all Lambda data types

### Data Types to Handle

The Lambda runtime supports these data types (defined in `transpiler.h`):

```c
enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    
    // Scalar types
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,      // 56-bit signed integer stored directly
    LMD_TYPE_INT64,    // 64-bit integer stored as pointer
    LMD_TYPE_FLOAT,    // Double stored as pointer
    LMD_TYPE_DECIMAL,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_STRING,
    LMD_TYPE_BINARY,
    
    // Container types
    LMD_TYPE_LIST,
    LMD_TYPE_ARRAY,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    // ...
};
```

## Implementation Pattern

### 1. File Structure

Create `lambda/format/format-<name>.c`:

```c
#include "../transpiler.h"

// Declare the print_named_items function from print.c
void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// Data extraction macros (use existing ones)
#define get_pointer(item) ((void*)((item) & 0x00FFFFFFFFFFFFFF))
#define get_bool_value(item) ((bool)((item) & 0xFF))
#define get_int_value(item) ((int64_t)(((int64_t)((item) & 0x00FFFFFFFFFFFFFF)) << 8) >> 8)

// Helper functions
static void format_string_<name>(StrBuf* sb, String* str);
static void format_number_<name>(StrBuf* sb, Item item);
static void format_item_<name>(StrBuf* sb, Item item, int indent_level);

// Main formatter function
String* format_<name>(VariableMemPool* pool, Item root_item);
```

### 2. Main Formatter Function Template

```c
String* format_<name>(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    // Add format-specific headers/preamble
    strbuf_append_str(sb, "<format_header>");
    
    // Get the root item type
    TypeId type = get_type_id((LambdaItem)root_item);
    
    // Handle different root types
    switch (type) {
    case LMD_TYPE_MAP:
        format_map_<name>(sb, (Map*)root_item, 0);
        break;
    case LMD_TYPE_ARRAY:
        format_array_<name>(sb, (Array*)root_item, 0);
        break;
    default:
        format_item_<name>(sb, root_item, 0);
        break;
    }
    
    // Add format-specific footers/postamble
    strbuf_append_str(sb, "<format_footer>");
    
    return strbuf_to_string(sb);
}
```

### 3. Data Type Handling

#### Scalar Types

```c
static void format_item_<name>(StrBuf* sb, Item item, int indent_level) {
    TypeId type = get_type_id((LambdaItem)item);
    
    switch (type) {
    case LMD_TYPE_NULL:
        strbuf_append_str(sb, "<null_representation>");
        break;
        
    case LMD_TYPE_BOOL: {
        bool val = get_bool_value(item);
        strbuf_append_str(sb, val ? "<true_repr>" : "<false_repr>");
        break;
    }
    
    case LMD_TYPE_INT: {
        int64_t val = get_int_value(item);
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64, val);
        strbuf_append_str(sb, buf);
        break;
    }
    
    case LMD_TYPE_FLOAT: {
        double* dptr = (double*)get_pointer(item);
        if (dptr) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.15g", *dptr);
            strbuf_append_str(sb, buf);
        }
        break;
    }
    
    case LMD_TYPE_STRING: {
        String* str = (String*)get_pointer(item);
        format_string_<name>(sb, str);
        break;
    }
    
    // Handle container types...
    }
}
```

#### Container Types - Two Approaches

**Approach 1: Use `print_named_items` (Recommended for complex formats)**

```c
static void format_map_<name>(StrBuf* sb, Map* mp, int indent_level) {
    if (!mp || !mp->type) {
        strbuf_append_str(sb, "<empty_map>");
        return;
    }
    
    TypeMap* map_type = (TypeMap*)mp->type;
    
    // Use the proven print_named_items function
    StrBuf* temp_sb = strbuf_new_pooled(pool);
    print_named_items(temp_sb, map_type, mp->data);
    String* temp_str = strbuf_to_string(temp_sb);
    
    // Convert the JSON-like output to your format
    convert_to_<name>_format(sb, temp_str->chars);
    
    strbuf_free(temp_sb);
}
```

**Approach 2: Manual Traversal (For simple formats)**

```c
static void format_map_<name>(StrBuf* sb, Map* mp, int indent_level) {
    if (!mp || !mp->type) return;
    
    TypeMap* map_type = (TypeMap*)mp->type;
    ShapeEntry *field = map_type->shape;
    
    for (int i = 0; i < map_type->length; i++) {
        if (!field) break;
        
        void* data = ((char*)mp->data) + field->byte_offset;
        
        // Handle field name and value
        if (field->name) {
            // Format field name
            strbuf_append_format(sb, "<field_format>", 
                                (int)field->name->length, field->name->str);
            
            // Format field value based on type
            switch (field->type->type_id) {
            case LMD_TYPE_STRING: {
                String* str = *(String**)data;
                format_string_<name>(sb, str);
                break;
            }
            case LMD_TYPE_INT: {
                Item item = *(Item*)data;
                format_number_<name>(sb, item);
                break;
            }
            // ... handle other types
            }
        }
        
        field = field->next;
    }
}
```

### 4. String Escaping

Each format requires different string escaping rules:

```c
static void format_string_<name>(StrBuf* sb, String* str) {
    if (!str) return;
    
    strbuf_append_str(sb, "<string_delimiter>");
    
    const char* s = str->chars;
    size_t len = str->len;
    
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '"':
            strbuf_append_str(sb, "<escaped_quote>");
            break;
        case '\\':
            strbuf_append_str(sb, "<escaped_backslash>");
            break;
        case '\n':
            strbuf_append_str(sb, "<escaped_newline>");
            break;
        // ... other escaping rules
        default:
            strbuf_append_char(sb, c);
            break;
        }
    }
    
    strbuf_append_str(sb, "<string_delimiter>");
}
```

### 5. Registration

Add your formatter to `lambda/format/format.c`:

```c
// Add function declaration at top
String* format_<name>(VariableMemPool* pool, Item root_item);

// Add to format_data function
String* format_data(Context* ctx, Item item, String* type) {
    String* result = NULL;
    // ... existing conditions ...
    else if (strcmp(type->chars, "<name>") == 0) {
        result = format_<name>(ctx->heap->pool, item);
    }
    // ... rest of function
}
```

## Best Practices

### Memory Management

1. **Always use pooled allocations**: `strbuf_new_pooled(pool)`
2. **Free temporary buffers**: Call `strbuf_free()` on temporary StrBuf objects
3. **Return ownership**: The returned String* is owned by the caller
4. **Register with heap**: The `format_data` function automatically registers results

### Error Handling

1. **Check null pointers**: Always validate input parameters
2. **Handle empty containers**: Provide sensible defaults for empty maps/arrays
3. **Graceful degradation**: Return valid output even for unsupported types

### Performance

1. **Use `print_named_items`**: For complex formats, leverage existing traversal code
2. **Minimize allocations**: Reuse StrBuf objects when possible
3. **Avoid string copying**: Use `strbuf_append_*` functions directly

### Debugging

1. **Add debug prints**: Use `printf()` and `fflush(stdout)` for debugging
2. **Test incrementally**: Start with simple data structures
3. **Compare with JSON**: Use JSON formatter as reference implementation

## Example Formats

### JSON-like Format
- Use `print_named_items` and minimal post-processing
- Example: Current YAML formatter

### Structured Format (XML/HTML)
- Manual traversal with proper nesting
- Handle attributes vs content
- Example: XML formatter

### Flat Format (CSV)
- Extract scalar values only
- Handle nested structures by flattening
- Requires custom traversal logic

## Testing

Create test files in `test/lambda/`:

```javascript
// test/lambda/format_<name>_test.ls
let test_data = {
    name: "test",
    count: 42,
    active: true,
    nested: {
        value: "nested_value"
    },
    list: [1, 2, 3]
}

"Testing <name> formatter:"
format(test_data, '<name>')
```

## Common Pitfalls

1. **Memory leaks**: Not freeing temporary StrBuf objects
2. **Infinite loops**: Incorrect ShapeEntry traversal in manual approach
3. **Type casting errors**: Using wrong Item extraction macros
4. **String buffer overflow**: Not checking StrBuf capacity (rare with dynamic buffers)
5. **Missing null checks**: Not validating pointers before dereferencing

## Format-Specific Considerations

### YAML
- Indentation matters
- Proper scalar quoting rules
- Document markers (`---`)
- Comments start with `#`

### XML
- Element vs attribute distinction
- Proper escaping of `&`, `<`, `>`
- Self-closing tags for empty elements
- XML declaration header

### HTML
- DOCTYPE declaration
- Proper nesting structure
- CSS/JavaScript escaping
- Semantic markup

### CSV
- Field delimiter handling
- Quote escaping rules
- Header row generation
- Flattening nested structures

## Conclusion

The Lambda formatter system is designed to be extensible and robust. By following this guide and using the existing `print_named_items` infrastructure, you can create reliable formatters for any text-based output format.

The key to success is:
1. Start simple with scalar types
2. Use proven traversal methods (`print_named_items`)
3. Handle edge cases gracefully
4. Test thoroughly with complex nested data
5. Follow memory management best practices
