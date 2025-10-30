# HTML Parser Negative Test Suite

## Overview

The `test_html_negative_gtest.cpp` file contains comprehensive negative test cases for the Lambda HTML parser (`lambda/input/input-html.cpp`). These tests verify that the parser handles invalid HTML input gracefully according to HTML5 specification error handling rules.

## Test Coverage

The test suite includes **125 test cases** organized into the following categories:

### 1. Malformed Tags (8 tests)
- Unclosed tags
- Mismatched tags
- Improperly nested tags
- Extra closing tags
- Empty tag names
- Invalid characters in tag names
- Missing closing brackets
- Spaces in tag names

### 2. Invalid Attributes (8 tests)
- Unclosed quotes in attribute values
- Mismatched quotes (single vs double)
- Attributes with `=` but no value
- Duplicate attributes
- Equals sign without attribute name
- Special characters in attribute names
- Whitespace around equals signs

### 3. Invalid Entity References (6 tests)
- Unknown named entities
- Entities missing semicolons
- Numeric entities out of Unicode range
- Invalid hexadecimal entities
- Bare ampersands without entity references

### 4. Invalid Nesting (5 tests)
- Block elements inside inline elements
- Paragraphs nested in paragraphs
- List items without parent lists
- Table cells without rows
- Nested forms

### 5. Invalid Comments (4 tests)
- Unclosed comments
- Malformed comment closing syntax
- Double hyphens inside comments
- Empty comments

### 6. Invalid DOCTYPE (4 tests)
- Malformed DOCTYPE declarations
- Unclosed DOCTYPE
- Multiple DOCTYPE declarations
- DOCTYPE after content

### 7. Invalid Void Elements (3 tests)
- Void elements with closing tags (e.g., `<br></br>`)
- Void elements with content
- Nested void elements

### 8. Invalid Script/Style Elements (3 tests)
- Unclosed script tags
- Script tags with partial closing tags inside content
- Unclosed style tags

### 9. Extreme/Edge Cases (8 tests)
- Deeply nested tags (20+ levels)
- Very long attribute values (10,000+ characters)
- Very long text content (100,000+ characters)
- Elements with many attributes (100+)
- Empty documents
- Documents with only whitespace
- Null bytes in content

### 10. Invalid Characters (3 tests)
- Special characters in tag names
- Control characters in content
- Invalid UTF-8 sequences

### 11. Invalid Table Structure (3 tests)
- Direct `<tr>` in `<table>` (should create implicit `<tbody>`)
- `<td>` without parent `<tr>`
- Mixed content in tables

### 12. HTML5 Specific Error Cases (5 tests)
- Misplaced start tags
- Misplaced end tags
- End of file while parsing tag
- End of file while parsing attribute
- Closing slash in wrong place

### 13. Mixed Valid/Invalid Content (3 tests)
- Valid content after invalid content
- Invalid content in middle of valid content
- Multiple different error types

### 14. Additional Attribute Edge Cases (7 tests)
- Attribute names starting with numbers
- Attribute that is just '='
- Multiple equals signs in attribute
- Nested quotes of same type
- Tabs in attribute values
- Newlines in unquoted attributes
- Attribute names with only whitespace

### 15. Tag Name Edge Cases (7 tests)
- Tag names with dashes (custom elements)
- Tag names with underscores
- Tag names starting with dash
- Tag names that are all numbers
- Tag names with dots
- Tag names with colons (namespace style)
- Extremely long tag names (1000+ chars)

### 16. Nesting and Structure Corner Cases (6 tests)
- Buttons nested inside buttons
- Anchors nested inside anchors
- Labels nested inside labels
- Headings nested inside headings
- Option elements without select parent
- Dt/dd elements without dl parent

### 17. Content Edge Cases (5 tests)
- Text with only special characters
- Text with repeated entities
- Text with mixed newline types (CRLF, LF, CR)
- Text with zero-width characters
- Text with bidirectional text markers

### 18. Whitespace Handling Corner Cases (5 tests)
- No space between attributes
- No space before self-closing slash
- Excessive whitespace between attributes
- Whitespace in middle of tag name
- Form feed character in content

### 19. Comment Edge Cases (5 tests)
- Comment containing null byte
- Comment starting with dash
- Comment ending with extra dash
- Nested comment-like syntax
- Comment containing script tag

### 20. Entity Reference Corner Cases (7 tests)
- Entity without semicolon followed by number
- Entity that partially matches known entity
- Numeric entity for code point 0
- Numeric entity with leading zeros
- Hex entity with uppercase X
- Multiple ampersands in a row
- Entity reference in attribute name

### 21. Self-Closing and Void Element Corner Cases (4 tests)
- Self-closing with space before slash
- Self-closing with attributes after slash
- Void element with child elements
- Multiple self-closing slashes

### 22. Script/Style Content Corner Cases (4 tests)
- Script with HTML comment markers
- Script with CDATA section
- Style with @media query containing >
- Script with escaped closing tag

### 23. Case Sensitivity Corner Cases (3 tests)
- Mixed case attributes
- Mixed case tag names
- Mixed case entity names

### 24. Empty Elements and Attributes (3 tests)
- Element with only whitespace content
- Empty attribute name
- Many empty elements nested

### 25. Table Structure Corner Cases (4 tests)
- Table with only caption
- Table with multiple tbody elements
- Table with thead after tbody (wrong order)
- Table cell with colspan=0

### 26. Stress Tests (5 tests)
- Alternating valid and invalid tags
- Many unclosed tags in sequence
- Repeated same tag without closing
- Single character tag names
- Bracket characters in text without escaping

## Test Results

**Status:** ✅ All 125 tests passing

The test suite verifies that the HTML parser:
1. **Handles errors gracefully** - doesn't crash on malformed input
2. **Follows HTML5 spec** - implements error recovery mechanisms
3. **Provides useful feedback** - logs parse errors with line/column information
4. **Maintains robustness** - can parse partial or severely malformed HTML

## Running the Tests

### Build and run:
```bash
make build-test
./test/test_html_negative_gtest.exe
```

### Run specific test:
```bash
./test/test_html_negative_gtest.exe --gtest_filter="HtmlParserNegativeTest.MalformedUnclosedTag"
```

### Run test category:
```bash
./test/test_html_negative_gtest.exe --gtest_filter="HtmlParserNegativeTest.Malformed*"
```

## Expected Parser Behavior

The Lambda HTML parser follows these principles for invalid HTML:

1. **Auto-closing tags**: Unclosed tags are automatically closed at appropriate boundaries
2. **Error recovery**: Parser attempts to continue after errors
3. **Graceful degradation**: Returns partial results when possible
4. **Error reporting**: Logs detailed parse errors with location information
5. **Type safety**: Returns `ITEM_NULL` or `ITEM_ERROR` for truly invalid input

## HTML5 Spec Compliance

The parser implements HTML5 error handling rules including:
- Implicit element creation (e.g., `<tbody>` in tables)
- Tag auto-closing based on context
- Attribute handling (first value wins for duplicates)
- Entity reference parsing with fallbacks
- Comment and DOCTYPE tolerance
- Void element recognition

## Test Fixture Features

The `HtmlParserNegativeTest` fixture provides helper methods:

- `parseHtml(html)` - Parse HTML string and return result
- `findElementByTag(item, tag)` - Locate element by tag name
- `getTextContent(item)` - Extract text content from element
- `getAttr(element, attr)` - Get attribute value
- `countElementsByTag(item, tag)` - Count occurrences of tag

## Related Files

- **Parser Implementation**: `lambda/input/input-html.cpp`
- **Positive Tests**: `test/test_html_gtest.cpp`
- **Roundtrip Tests**: `test/test_html_roundtrip_gtest.cpp`
- **Build Configuration**: `build_lambda_config.json`

## Future Enhancements

Potential areas for additional test coverage:
- More complex nesting scenarios
- Additional HTML5 semantic elements with errors
- ARIA attribute validation
- Custom element validation
- Shadow DOM edge cases
- More international character scenarios
- Performance stress tests with extreme inputs
