# JSX and MDX Implementation Plan for Lambda

## Overview

This document outlines the comprehensive plan for implementing JSX parser and formatter support in the Lambda project, including standalone JSX support and MDX (Markdown + JSX) hybrid format support.

## Architecture Analysis

Based on existing Lambda codebase patterns, the implementation follows these key principles:

### Lambda Runtime Data Model
- **Input Structure**: Uses `Input*` with memory pool (`VariableMemPool*`) for allocation
- **Element Creation**: Elements created via `input_create_element()` with tag names
- **Attribute Handling**: Attributes added via `input_add_attribute_to_element()`
- **String Management**: Strings created via `input_create_string()` from memory pool
- **Map Operations**: Key-value pairs stored via `map_put()` function
- **Type System**: Uses `TypeId`, `TypeMap`, `ShapeEntry` for structured data

### Parser Pattern
- Function signature: `void parse_jsx(Input* input, const char* jsx_string)`
- Memory allocation from `input->pool` using `pool_calloc()`
- Element tree construction using Lambda's type system
- Error handling with graceful degradation

### Formatter Pattern
- Function signature: `String* format_jsx(VariableMemPool* pool, Item root_item)`
- Uses `StringBuf*` for efficient string building
- Recursive element traversal with `format_element()` pattern
- Attribute formatting with proper escaping

## Phase 1: Standalone JSX Parser Implementation

### 1.1 File Structure
```
lambda/input/input-jsx.cpp    - JSX parser implementation (includes JSX expression parsing)
lambda/format/format-jsx.cpp  - JSX formatter implementation
test/test_jsx_roundtrip.cpp   - JSX roundtrip tests
test/input/*.jsx              - JSX test files
```

**Note**: HTML script parsing remains in `input-html.cpp` with separate escaping rules.

### 1.2 JSX Expression Parsing (JSX-specific)

#### JSX JavaScript Expression Parsing
JSX expressions have specific escaping rules different from HTML script content. Focus on brace matching and JavaScript syntax:

#### JSX Expression Parser (in `input-jsx.cpp`)
JSX-specific JavaScript expression parsing functions:

```c
// JSX JavaScript expression parsing (JSX-specific)
typedef struct {
    int brace_depth;
    bool in_string;
    bool in_template_literal;
    char string_delimiter;
    bool escaped;
} JSXExpressionState;

// JSX expression parsing functions (JSX-specific)
String* parse_jsx_expression_content(Input* input, const char** js_expr, const char* end);
Element* create_jsx_js_expression_element(Input* input, const char* js_content);
static bool is_jsx_expression_complete(const char* js_expr, size_t length);
static void skip_jsx_string_literal(const char** js_expr, const char* end, char delimiter);
static void skip_jsx_template_literal(const char** js_expr, const char* end);
static bool is_jsx_identifier_char(char c);
static bool is_jsx_whitespace(char c);
```

### 1.3 JSX Parser Architecture (`input-jsx.cpp`)

#### Core Functions
```c
// Main parser entry point
void parse_jsx(Input* input, const char* jsx_string);

// JSX-specific parsing functions with string-based JS expressions
static Element* parse_jsx_element(Input* input, const char** jsx, const char* end);
static Element* parse_jsx_fragment(Input* input, const char** jsx, const char* end);
static void parse_jsx_attributes(Input* input, Element* element, const char** jsx, const char* end);
static String* parse_jsx_attribute_value(Input* input, const char** jsx, const char* end);
static Element* parse_jsx_expression(Input* input, const char** jsx, const char* end);
static String* parse_jsx_text_content(Input* input, const char** jsx, const char* end);
static bool is_jsx_identifier_char(char c);
static bool is_jsx_whitespace(char c);
static void skip_jsx_whitespace(const char** jsx, const char* end);
```

#### JSX Element Structure
JSX elements will be represented as Lambda elements with these attributes:
- `tag`: Element tag name (e.g., "div", "Component")
- `type`: "jsx_element" | "jsx_fragment" | "jsx_text"
- `props`: Map of JSX props/attributes
- `children`: Array of child elements
- `self_closing`: Boolean for self-closing tags
- `is_component`: Boolean (true for PascalCase components)

#### JavaScript Expression Elements
JavaScript expressions (`{...}`) will be represented as special Lambda elements:
- **Element structure**: `<js "expression_content">`
- **Tag name**: `"js"`
- **Content**: Raw JavaScript expression as string
- **No AST parsing**: Expression stored as escaped string content

#### JSX Expression Handling Examples
```jsx
{obj.prop}           → <js "obj.prop">
{func(a, b)}         → <js "func(a, b)">
{condition ? a : b}  → <js "condition ? a : b">
{`Hello ${name}`}    → <js "`Hello ${name}`">
```

#### Expression Parsing Rules
- **Brace matching**: Parse until matching closing brace `}`
- **String escaping**: Handle quotes, template literals, escaped characters
- **Nested braces**: Support object literals `{key: value}` within expressions
- **No JSX nesting**: JSX within JS expressions treated as string content

#### JSX Fragment Support
JSX fragments (`<>...</>`) will be represented as:
- Element with `tag:"jsx_fragment"`
- `type:"jsx_fragment"`
- `children` array containing fragment contents

### 1.4 JSX Formatter Architecture (`format-jsx.cpp`)

#### Core Functions with JavaScript Support
```c
// Main formatter entry point
String* format_jsx(VariableMemPool* pool, Item root_item);

// JSX-specific formatting functions
static void format_jsx_element(StringBuf* sb, Element* elem);
static void format_jsx_fragment(StringBuf* sb, Element* elem);
static void format_jsx_attributes(StringBuf* sb, Element* elem);
static void format_jsx_attribute_value(StringBuf* sb, String* value);
static void format_jsx_text_content(StringBuf* sb, String* text);
static void format_jsx_children(StringBuf* sb, Element* elem);

// JavaScript expression formatting
static void format_js_expression_element(StringBuf* sb, Element* js_elem);
static void escape_js_expression_content(StringBuf* sb, String* js_content);
static bool is_js_expression_element(Element* elem);
```

#### Formatting Rules
- **Self-closing tags**: `<Component />` format
- **Attribute formatting**: Proper spacing and quote handling
- **Expression formatting**: `{expression}` with proper escaping
- **Fragment formatting**: `<>...</>` or `<React.Fragment>...</React.Fragment>`
- **Indentation**: Configurable indentation (default 2 spaces)
- **Line breaking**: Smart line breaking for readability

### 1.5 HTML Parser Enhancement for Script Content

#### Current HTML Parser Analysis
The existing HTML parser (`input-html.cpp`) handles script tags as raw text elements. We should enhance it to parse script content as `<script "content">` elements.

#### HTML Script vs JSX Expression Escaping Differences

**HTML Script Content (`<script>...</script>`):**
- **End delimiter**: Must handle `</script>` within strings/comments
- **HTML entities**: May contain HTML entities that need unescaping
- **CDATA sections**: May contain `<![CDATA[...]]>` sections
- **HTML comments**: `<!-- -->` within script content
- **No brace matching**: Content until `</script>` tag

**JSX Expressions (`{...}`):**
- **Brace matching**: Must match opening `{` with closing `}`
- **Nested braces**: Object literals `{key: value}` within expressions
- **String literals**: Handle `"..."`, `'...'`, and template literals
- **No HTML entities**: Pure JavaScript syntax
- **No CDATA**: JSX expressions are pure JS

#### Separate Parsing Approach
Due to different escaping rules, keep HTML script and JSX expression parsing separate:

#### HTML Parser Changes (`input-html.cpp`)
```c
// Enhanced script tag handling for <script "content"> elements
static Element* parse_script_element_enhanced(Input* input, const char** html, String* tag_name) {
    Element* element = input_create_element(input, tag_name->chars);
    
    // Parse attributes first
    parse_attributes(input, element, html);
    
    // Skip to content
    if (**html == '>') (*html)++;
    
    // Parse script content with HTML-specific escaping
    StringBuf* content_sb = input->sb;
    stringbuf_reset(content_sb);
    
    const char* script_end = "</script>";
    size_t script_end_len = strlen(script_end);
    
    while (**html) {
        // Check for closing tag (case-insensitive)
        if (strncasecmp(*html, script_end, script_end_len) == 0) {
            break;
        }
        
        // Handle HTML entities within script content
        if (**html == '&') {
            // Parse HTML entity (existing HTML entity parsing logic)
            // ... handle &lt; &gt; &amp; etc.
        }
        
        // Handle CDATA sections
        if (strncmp(*html, "<![CDATA[", 9) == 0) {
            // Skip CDATA markers but include content
            *html += 9;
            while (**html && strncmp(*html, "]]>", 3) != 0) {
                stringbuf_append_char(content_sb, **html);
                (*html)++;
            }
            if (**html) *html += 3; // Skip ]]>
            continue;
        }
        
        stringbuf_append_char(content_sb, **html);
        (*html)++;
    }
    
    // Create script content as string and add to element
    if (content_sb->length > sizeof(uint32_t)) {
        String* script_content = stringbuf_to_string(content_sb);
        Item content_item = {.item = s2it(script_content)};
        list_push((List*)element, content_item);
    }
    
    // Skip closing tag
    if (**html && strncasecmp(*html, script_end, script_end_len) == 0) {
        *html += script_end_len;
    }
    
    return element;
}
```

### 1.6 Integration Points

#### Input System Integration (`input.cpp`)
Add JSX format detection and parsing:
```c
// Add to input.cpp parser dispatch
if (strcmp(type->chars, "jsx") == 0) {
    parse_jsx(input, source);
    return input;
}
```

#### Format System Integration (`format.h` and main formatter)
Add JSX formatter to format dispatch:
```c
// Add to format.h
String* format_jsx(VariableMemPool* pool, Item root_item);

// Add to main format dispatcher
if (strcmp(type->chars, "jsx") == 0) {
    return format_jsx(pool, root_item);
}
```

## Phase 2: MDX Parser Implementation

### 2.1 MDX Architecture Overview

MDX combines Markdown with JSX, requiring:
- **Hybrid parsing**: Markdown parser with JSX element detection
- **Context switching**: Switch between Markdown and JSX parsing modes
- **Element integration**: JSX elements embedded in Markdown structure
- **Format mapping**: `type:'markdown'`, `flavor:'mdx'`

### 2.2 MDX Parser Strategy

#### Approach: Enhanced Markdown Parser
Extend existing markdown parser (`input-markup.cpp`) with JSX detection:

```c
// Add to input-markup.cpp
static bool is_jsx_element_start(const char* text);
static Element* parse_mdx_jsx_element(Input* input, const char** text, const char* end);
static void parse_mdx_content(Input* input, const char* content);
```

#### MDX Detection Logic
- **JSX Element Detection**: Look for `<[A-Z]` (components) or `<[a-z]` (HTML elements)
- **JSX Expression Detection**: Look for `{` not in code blocks
- **Context Preservation**: Maintain Markdown context (lists, headers, etc.)
- **Nesting Rules**: Handle JSX nested in Markdown and vice versa

#### MDX Element Structure
MDX elements extend Markdown elements:
- Standard Markdown elements (headers, lists, etc.)
- JSX elements with `mdx_context` attribute
- Mixed content elements with both Markdown and JSX children

### 2.3 MDX Formatter Strategy

#### Approach: Enhanced Markdown Formatter
Extend existing markdown formatter (`format-md.cpp`) with JSX support:

```c
// Add to format-md.cpp
static void format_mdx_jsx_element(StringBuf* sb, Element* elem);
static void format_mdx_mixed_content(StringBuf* sb, Element* elem);
static bool is_mdx_jsx_element(Element* elem);
```

#### MDX Formatting Rules
- **Markdown preservation**: Standard Markdown formatting rules
- **JSX integration**: Proper JSX formatting within Markdown context
- **Line breaking**: Smart line breaks around JSX elements
- **Indentation**: Respect both Markdown and JSX indentation rules

## Phase 3: Testing Strategy

### 3.1 JSX Roundtrip Tests (`test/test_jsx_roundtrip.cpp`)

#### Test Structure
Following existing test patterns from `test_input_roundtrip.cpp`:

```c
// Test cases structure
Test(jsx_roundtrip, basic_elements) {
    // Test basic JSX elements
}

Test(jsx_roundtrip, jsx_expressions) {
    // Test JSX expressions
}

Test(jsx_roundtrip, jsx_fragments) {
    // Test JSX fragments
}

Test(jsx_roundtrip, complex_nesting) {
    // Test complex nested structures
}
```

#### Test Files (`test/input/`)
```
test/input/
├── basic_element.jsx          - Simple JSX elements
├── jsx_expressions.jsx       - JSX expressions and props  
├── jsx_fragments.jsx         - JSX fragments
├── complex_nesting.jsx       - Complex nested structures
├── components.jsx            - React components
├── mixed_content.jsx         - Mixed JSX and text content
├── js_expressions.jsx        - JavaScript expressions as <js "content">
├── complex_expressions.jsx   - Complex JavaScript expressions in JSX
└── string_escaping.jsx       - String escaping in JS expressions
```

### 3.2 MDX Roundtrip Tests

#### Test Structure
```c
Test(mdx_roundtrip, markdown_with_jsx) {
    // Test Markdown with embedded JSX
}

Test(mdx_roundtrip, jsx_with_markdown) {
    // Test JSX with embedded Markdown
}

Test(mdx_roundtrip, complex_mdx) {
    // Test complex MDX documents
}
```

#### Test Files (`test/input/`)
```
test/input/
├── basic_mdx.mdx             - Basic MDX with simple JSX
├── components_in_md.mdx      - React components in Markdown
├── markdown_in_jsx.mdx       - Markdown content in JSX
├── complex_mdx.mdx           - Complex MDX documents
└── frontmatter_mdx.mdx       - MDX with frontmatter
```

### 3.3 Test Implementation Pattern

Following existing roundtrip test patterns:

```c
// Roundtrip test implementation
static void test_jsx_roundtrip_file(const char* filename) {
    // Read original file
    char* original = read_file_content(filename);
    cr_assert_not_null(original, "Failed to read test file: %s", filename);
    
    // Parse JSX
    String* type = create_lambda_string("jsx");
    Input* input = input_from_source(original, NULL, type, NULL);
    cr_assert_not_null(input, "Failed to parse JSX from: %s", filename);
    
    // Format back to JSX
    String* formatted = format_jsx(input->pool, input->root);
    cr_assert_not_null(formatted, "Failed to format JSX for: %s", filename);
    
    // Compare results (with semantic equivalence for JSX)
    bool equivalent = are_jsx_semantically_equivalent(original, formatted->chars);
    cr_assert(equivalent, "JSX roundtrip failed for: %s\nOriginal:\n%s\nFormatted:\n%s", 
              filename, original, formatted->chars);
    
    free(original);
}
```

## Phase 4: Build System Integration

### 4.1 Build Configuration Updates

#### Update `build_lambda_config.json`
Add JSX test target:
```json
{
  "test_jsx": {
    "type": "executable",
    "sources": [
      "test/test_jsx_roundtrip.cpp",
      "lambda/input/input-jsx.cpp",
      "lambda/format/format-jsx.cpp"
    ],
    "libraries": ["criterion"],
    "defines": ["CRITERION_AVAILABLE"]
  }
}
```

#### Update `premake5.lua`
Add JSX files to main lambda target and create test target.

### 4.2 Makefile Integration
Add JSX test targets:
```makefile
build-test-jsx: premake
	$(MAKE) -C build test_jsx

test-jsx: build-test-jsx
	./test/test_jsx.exe
```

## Phase 5: Documentation and Examples

### 5.1 Usage Documentation

#### JSX Usage Examples
```javascript
// Basic JSX element
<div className="container">
  <h1>Hello World</h1>
  <p>This is JSX content</p>
</div>

// JSX with expressions (parsed as <js "expression">)
<div>
  <h1>{title}</h1>           // → <h1><js "title"></h1>
  <p>Count: {count + 1}</p>  // → <p>Count: <js "count + 1"></p>
</div>

// JSX fragments
<>
  <h1>Fragment Content</h1>
  <p>No wrapper element</p>
</>

// React components with JS expressions
<MyComponent prop1="value" prop2={expression}>  // → prop2=<js "expression">
  <ChildComponent />
</MyComponent>
```

#### MDX Usage Examples
```markdown
# My MDX Document

This is regular Markdown content.

<MyComponent prop="value">
  ## This is Markdown inside JSX
  
  - List item 1
  - List item 2
</MyComponent>

More Markdown content here.

<div className="highlight">
  **Bold text** in JSX context
</div>
```

### 5.2 Lambda Script Integration

#### JSX Input Example
```lambda
let jsx_content = input("components/Button.jsx", 'jsx')
jsx_content
```

#### MDX Input Example
```lambda
let mdx_doc = input("docs/guide.mdx", 'markdown', 'mdx')
mdx_doc
```

## Phase 6: Advanced Features (Future)

### 6.1 JSX Extensions
- **TypeScript JSX**: Support for TSX files
- **JSX Pragma**: Support for custom JSX pragmas
- **JSX Runtime**: Support for new JSX transform

### 6.2 MDX Extensions
- **Frontmatter**: YAML/TOML frontmatter support
- **Import/Export**: ES6 import/export statements
- **MDX Plugins**: Plugin architecture for extensions

### 6.3 Performance Optimizations
- **Streaming Parser**: For large JSX/MDX files
- **Incremental Parsing**: For real-time editing
- **Memory Optimization**: Reduce memory footprint

## Implementation Timeline

### Week 1-2: JSX Parser Foundation
- Implement basic JSX parser (`input-jsx.cpp`)
- Create JSX element structure and parsing logic
- Handle JSX expressions and attributes

### Week 3-4: JSX Formatter Implementation
- Implement JSX formatter (`format-jsx.cpp`)
- Create proper JSX output formatting
- Handle edge cases and formatting rules

### Week 5-6: MDX Integration
- Extend Markdown parser for MDX support
- Implement MDX formatter extensions
- Handle context switching between Markdown and JSX

### Week 7-8: Testing and Integration
- Create comprehensive test suites
- Implement roundtrip testing
- Build system integration and documentation

### Week 9-10: Polish and Optimization
- Performance optimization
- Edge case handling
- Documentation and examples

## Success Criteria

### JSX Implementation Success
- ✅ Parse all valid JSX syntax correctly
- ✅ Format JSX with proper indentation and spacing
- ✅ Handle JSX expressions, fragments, and components
- ✅ 100% roundtrip test success rate
- ✅ Integration with Lambda input system

### MDX Implementation Success
- ✅ Parse MDX documents with embedded JSX
- ✅ Maintain Markdown formatting while supporting JSX
- ✅ Handle complex nesting scenarios
- ✅ 100% MDX roundtrip test success rate
- ✅ Proper `type:'markdown'`, `flavor:'mdx'` mapping

### Quality Metrics
- **Test Coverage**: >95% code coverage
- **Performance**: Parse/format within 100ms for typical files
- **Memory Usage**: Efficient memory pool utilization
- **Compatibility**: Support for React JSX patterns
- **Robustness**: Graceful error handling and recovery

## Risk Mitigation

### Technical Risks
- **Parsing Complexity**: JSX has complex grammar rules
  - *Mitigation*: Incremental implementation with comprehensive testing
- **Context Switching**: MDX requires complex context management
  - *Mitigation*: Clear state machine design and extensive testing
- **Performance**: Large JSX/MDX files may be slow
  - *Mitigation*: Streaming parser and performance profiling

### Integration Risks
- **Lambda Data Model**: Complex integration with existing system
  - *Mitigation*: Follow existing patterns and extensive integration testing
- **Memory Management**: Proper memory pool usage
  - *Mitigation*: Follow existing memory patterns and leak testing

This comprehensive plan provides a structured approach to implementing JSX and MDX support in the Lambda project, following established patterns and ensuring robust, well-tested functionality.
