# Porting Old Parsers to New Unified Markup Parser

This guide documents the process of migrating legacy format-specific parsers to the new unified markup parser (`input-markup.cpp`), based on the successful RST parser integration.

## Overview

The unified markup parser (`input_markup`) provides a single entry point for parsing multiple markup formats, offering better consistency, maintainability, and feature sharing across formats.

## Migration Process

### 1. Feature Analysis and Comparison

**Objective**: Identify all features supported by the old parser that need to be preserved.

**Steps**:
1. Read through the old parser implementation thoroughly
2. Document all supported syntax elements:
   - Block-level elements (headings, lists, tables, code blocks, etc.)
   - Inline elements (emphasis, links, code spans, etc.)  
   - Format-specific constructs (directives, references, etc.)
3. Compare against existing unified parser capabilities
4. Identify missing features that need to be implemented

**Example from RST migration**:
```cpp
// Old RST parser had these features we needed to preserve:
- Headings with underline markers (=, -, `, :, ', ", ~, ^, _, *, +, #, <, >)
- Multiple list types (bullet, enumerated, definition)
- Literal blocks and code spans
- Directives (.. directive:: arguments)
- Tables (simple and grid formats)
- Comments (.. comment text)
- Inline markup (*emphasis*, ``literals``, references_)
```

### 2. Extend the Unified Parser

**Location**: `lambda/input/input-markup.cpp`

**Add Format Detection**:
```cpp
// In detect_markup_format() function
if (has_rst_markers(content)) {
    return MARKUP_RST;
}
```

**Implement Format-Specific Logic**:
```cpp
// Add format-specific parsing functions
static Item parse_rst_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_rst_inline_content(Input *input, const char* text);

// Add format branch in input_markup()
case MARKUP_RST:
    return parse_rst_content(input, content);
```

### 3. Implement Missing Features

**Block Elements**: Add parsers for format-specific block constructs
```cpp
static Item parse_rst_directive(Input *input, char** lines, int* current_line, int total_lines) {
    // Implementation for .. directive:: syntax
    Element* directive = create_rst_element(input, "directive");
    add_attribute_to_element(input, directive, "name", directive_name);
    // ... parsing logic
    return {.item = (uint64_t)directive};
}
```

**Inline Elements**: Add parsers for format-specific inline markup
```cpp
static Item parse_rst_emphasis(Input *input, const char* text, int* pos) {
    // Implementation for *emphasis* and **strong** syntax
    const char* tag_name = (star_count >= 2) ? "strong" : "em";
    Element* emphasis_elem = create_rst_element(input, tag_name);
    // ... parsing logic
    return {.item = (uint64_t)emphasis_elem};
}
```

### 4. Update Input Routing

**Location**: `lambda/input/input.cpp`

**Update the routing logic** to use the unified parser:
```cpp
// Replace old parser calls:
else if (strcmp(effective_type, "rst") == 0) {
    parse_rst(input, source);  // OLD
}

// With unified parser calls:
else if (strcmp(effective_type, "rst") == 0) {
    input->root = input_markup(input, source);  // NEW
}
```

**Remove forward declarations** of old parser functions:
```cpp
// Remove these lines:
void parse_rst(Input* input, const char* rst_string);
```

### 5. Update Test Files

**Consolidate test files**:
- Merge multiple format test files into comprehensive ones
- Update file references in test code
- Ensure all format features are covered in tests

**Update test file references**:
```cpp
// In test files, update references:
"test/input/test.rst" → "test/input/comprehensive_test.rst"
```

**Example changes**:
```cpp
// test_validator.c
test_auto_schema_detection_helper("test/input/comprehensive_test.rst", ...);

// test_mime_detect.c  
"test/input/comprehensive_test.rst",
```

### 6. Schema Alignment

**Ensure compatibility** with existing schemas:
- The new parser should produce the same element structure as expected by `doc_schema.ls`
- Add required attributes (e.g., `level` for headings, `style` for lists)
- Maintain consistent element naming and nesting

**Example**:
```cpp
// Add schema-required attributes
char level_str[10];
snprintf(level_str, sizeof(level_str), "%d", level);
add_attribute_to_element(input, header, "level", level_str);
```

### 7. Build and Test

**Build the project**:
```bash
make clean && make
```

**Run tests to verify**:
```bash
make test
```

**Verify specific format tests pass**:
- Format-specific roundtrip tests
- Validator tests with document schema
- MIME detection tests

## Common Patterns

### Element Creation
```cpp
Element* element = create_rst_element(input, "tag_name");
add_attribute_to_element(input, element, "attr_name", "attr_value");
```

### Text Content
```cpp
Item text_content = parse_inline_content(input, text);
if (text_content.item != ITEM_NULL) {
    list_push((List*)element, text_content);
    ((TypeElmt*)element->type)->content_length++;
}
```

### Line-by-Line Processing
```cpp
while (*current_line < total_lines) {
    const char* line = lines[*current_line];
    
    if (is_empty_line(line)) {
        (*current_line)++;
        continue;
    }
    
    // Process line...
    (*current_line)++;
}
```

## Benefits of Migration

1. **Consistency**: Unified parsing approach across formats
2. **Maintainability**: Single codebase for markup parsing logic
3. **Feature Sharing**: Common utilities and helpers
4. **Performance**: Reduced code duplication and better optimization opportunities
5. **Extensibility**: Easier to add new markup formats

## Validation Checklist

- [ ] All old parser features implemented in unified parser
- [ ] Input routing updated to use `input_markup`
- [ ] Old parser function calls removed
- [ ] Test files updated and consolidated
- [ ] All format-specific tests passing
- [ ] Validator tests passing with document schema
- [ ] MIME detection tests passing
- [ ] Build completes without errors

## Example: Complete RST Migration

The RST parser migration serves as the reference implementation:

**Files Modified**:
- `lambda/input/input-markup.cpp` - Added RST parsing logic
- `lambda/input/input.cpp` - Updated routing to use unified parser
- `test/test_validator.c` - Updated file references
- `test/test_mime_detect.c` - Updated file references  
- `test/test_input.c` - Updated file references
- `test/input/comprehensive_test.rst` - Consolidated test file

**Results**:
- ✅ All RST-specific tests pass
- ✅ Validator tests pass with doc_schema.ls
- ✅ MIME detection tests pass
- ✅ Build successful with no errors
- ✅ Feature parity maintained with old parser

## Future Considerations

When porting additional parsers (AsciiDoc, Textile, etc.), follow this same pattern:

1. Analyze old parser features
2. Extend unified parser with format detection and parsing logic
3. Update input routing
4. Consolidate and update tests
5. Verify schema alignment
6. Build and validate

This systematic approach ensures consistent, maintainable, and reliable parser migrations.
