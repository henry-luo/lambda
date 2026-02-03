# CSS & DOM Testing Quick Reference

## 📋 Summary

This is a quick reference for the comprehensive CSS and DOM testing strategy for Lambda. See `CSS_Testing_Strategy.md` for full details.

## 🏗️ Test Architecture

```
6 Testing Layers:

Layer 1: Tokenizer Unit Tests (100+ tests)
├── Basic token types
├── Numeric tokenization (CRITICAL: .5 vs .container)
├── String/URL tokenization
├── Function tokenization
├── Unicode and escapes
├── Edge cases
└── Regression tests

Layer 2: Parser Unit Tests (80+ tests)
├── Simple selectors (element, class, ID, universal)
├── Compound selectors
├── Multiple selectors
├── Declaration parsing
├── Value parsing
├── Rule parsing
└── Error handling

Layer 3: Integration Unit Tests (60+ tests)
├── Stylesheet parsing
├── Error recovery (brace depth tracking)
├── Feature detection
├── Cascade priority
└── Style application

Layer 4: DOM Unit Tests (130+ tests) ✅
├── DomElement (98 tests)
│   ├── Creation, attributes, classes
│   ├── Style management and cascade
│   ├── Selector matching (simple, compound, complex)
│   ├── Pseudo-class matching
│   ├── Tree navigation and manipulation
│   └── Performance and edge cases
├── DomText (9 tests) ✅ NEW
│   ├── Creation and destruction
│   ├── Content get/set operations
│   └── Tree integration
├── DomComment (6 tests) ✅ NEW
│   ├── Comment node creation
│   ├── DOCTYPE node creation
│   └── XML declaration support
├── Mixed DOM Trees (11 tests) ✅ NEW
│   ├── Elements + Text + Comments
│   ├── Sibling navigation across types
│   └── Tree traversal with type checking
├── Node Type Utilities (4 tests) ✅ NEW
│   ├── dom_node_get_type()
│   ├── dom_node_is_element/text/comment()
└── Memory Management (3 tests) ✅ NEW

Layer 5: Regression Tests
├── Issue #001: Class selector null values
├── Issue #002: .container tokenization
├── Issue #003: Properties as selectors
└── (Future bugs captured here)

Layer 6: Property Validation Tests (50+ tests)
├── Color properties
├── Length properties
├── Display properties
├── Shorthand properties
└── Custom properties
```

## 🏗️ Test Structure

```
test/
├── test_css_dom_integration.cpp            # 130+ DOM integration tests ✅
│   ├── 98 DomElement tests
│   ├── 9 DomText tests (NEW)
│   ├── 6 DomComment tests (NEW)
│   ├── 11 Mixed DOM tree tests (NEW)
│   ├── 4 Node type utility tests (NEW)
│   └── 3 Memory management tests (NEW)
├── test_lambda_domnode_gtest.cpp           # 12 DomNode wrapper tests
├── test_html_css_gtest.cpp                 # End-to-end integration tests
└── css/
    ├── unit/                               # Comprehensive unit tests
    │   ├── test_css_tokenizer_unit.cpp     # 100+ tokenizer tests
    │   ├── test_css_parser_unit.cpp        # 80+ parser tests
    │   ├── test_css_properties.cpp         # 50+ property tests
    │   └── test_css_integration_unit.cpp   # 60+ integration tests
    └── helpers/
        └── css_test_helpers.hpp            # Shared test utilities
```

## 🎯 Test Coverage Summary

### DOM Testing Coverage ✅
| Component | Tests | File | Status |
|-----------|-------|------|--------|
| DomElement | 98 | `test_css_dom_integration.cpp` | ✅ Complete |
| DomText | 9 | `test_css_dom_integration.cpp` | ✅ Complete |
| DomComment | 6 | `test_css_dom_integration.cpp` | ✅ Complete |
| Mixed Trees | 11 | `test_css_dom_integration.cpp` | ✅ Complete |
| Node Utilities | 4 | `test_css_dom_integration.cpp` | ✅ Complete |
| Memory | 3 | `test_css_dom_integration.cpp` | ✅ Complete |
| **Total** | **131** | | ✅ **Comprehensive** |

### CSS Testing Coverage (Proposed)
| Layer | File | Target | Priority |
|-------|------|--------|----------|
| Tokenizer | `css_tokenizer.c` | 95%+ | High |
| Parser | `css_parser.c` | 90%+ | High |
| Integration | `css_integration.c` | 85%+ | Medium |
| Properties | `css_properties.c` | 80%+ | Medium |

## 🚀 Quick Start

### Run All CSS Unit Tests

```bash
# Build all CSS tests
make build-test

# Run all CSS unit tests
make test-css-unit

# Run specific test suite
./test/css/unit/test_css_tokenizer_unit.exe

# Run with filter
./test/css/unit/test_css_tokenizer_unit.exe --gtest_filter=*Regression*
```

### Create New Test

```cpp
#include <gtest/gtest.h>
#include "../helpers/css_test_helpers.hpp"

using namespace CssTestHelpers;

class MyTestSuite : public ::testing::Test {
protected:
    PoolGuard pool;  // Automatic memory management
};

TEST_F(MyTestSuite, TestName) {
    // Arrange
    Parser parser(pool.get());

    // Act
    auto selector = parser.ParseSelector(".container");

    // Assert
    ASSERT_CSS_SELECTOR(selector, CSS_SELECTOR_TYPE_CLASS, "container");
}
```

## 🛠️ Test Helper Utilities

### Memory Management

```cpp
PoolGuard pool;  // RAII wrapper - auto cleanup
Pool* p = pool.get();
```

### Tokenization

```cpp
Parser parser(pool);
auto tokens = parser.Tokenize("div { color: red; }");

// Check token count
EXPECT_EQ(tokens.count(), 7);

// Check specific token
ASSERT_CSS_TOKEN(tokens[0], CSS_TOKEN_IDENT, "div");
```

### Selector Parsing

```cpp
auto selector = parser.ParseSelector(".container");
ASSERT_CSS_SELECTOR(selector, CSS_SELECTOR_TYPE_CLASS, "container");

// Specialized assertions
SelectorAssertions::AssertClass(selector, "container");
SelectorAssertions::AssertElement(selector, "div");
SelectorAssertions::AssertID(selector, "header");
```

### Declaration Parsing

```cpp
auto decl = parser.ParseDeclaration("color: red !important");
ASSERT_CSS_DECLARATION(decl, "color", "red");
DeclarationAssertions::AssertImportant(decl, true);
```

### Rule Parsing

```cpp
auto rule = parser.ParseRule(".btn { color: blue; }");
ASSERT_CSS_RULE(rule, 1, 1);  // 1 selector, 1 declaration
```

## 📝 Test Naming Convention

```cpp
// Pattern: Component_Behavior_Condition_ExpectedResult
TEST(CssTokenizer, TokenizeNumber_LeadingDecimal_ReturnsNumberToken)
TEST(CssParser, ParseSelector_ClassWithDot_ReturnsClassSelector)
TEST(CssIntegration, ParseStylesheet_WithErrors_ContinuesParsing)
```

## 🔍 Assertion Macros

```cpp
// Token assertions
ASSERT_CSS_TOKEN(token, CSS_TOKEN_IDENT, "div");
ASSERT_CSS_TOKEN_TYPE(token, CSS_TOKEN_NUMBER);

// Selector assertions
ASSERT_CSS_SELECTOR(sel, CSS_SELECTOR_TYPE_CLASS, "container");

// Declaration assertions
ASSERT_CSS_DECLARATION(decl, "color", "red");

// Rule assertions
ASSERT_CSS_RULE(rule, selector_count, declaration_count);
```

## 🧪 Test Categories

### Tokenizer Tests (100+ tests)

1. **Basic Tokens** - Identifiers, numbers, strings, delimiters
2. **Numeric Edge Cases** - `.5` vs `.container` (CRITICAL)
3. **Functions** - `rgb()`, `calc()`, `var()`
4. **Unicode** - UTF-8 identifiers, escape sequences
5. **Error Recovery** - Unterminated strings, malformed input

### Parser Tests (80+ tests)

1. **Selectors** - Element, class, ID, universal
2. **Declarations** - Properties, values, `!important`
3. **Rules** - Complete rule parsing
4. **Multiple Selectors** - Comma-separated selectors
5. **Error Handling** - Invalid syntax recovery

### Integration Tests (60+ tests)

1. **Stylesheet Parsing** - Single/multiple rules
2. **Error Recovery** - Brace depth tracking
3. **Cascade** - Inline vs external, specificity
4. **External CSS** - File loading
5. **Feature Detection** - CSS3+ features

## 📊 DOM Test Details

### DomElement Tests (98 tests)
Comprehensive coverage of core DOM element functionality:

**Basic Operations** (4 tests):
- `CreateDomElement` - Element creation and initialization
- `DomElementAttributes` - Set/get/has/remove attributes
- `DomElementClasses` - Add/remove/toggle/has classes
- `DomElementIdAttribute` - ID attribute handling

**Style Management** (2 tests):
- `ApplyDeclaration` - Apply CSS declarations to elements
- `StyleVersioning` - Style versioning and invalidation

**Tree Navigation** (7 tests):
- `AppendChild` - Append child elements
- `MultipleChildren` - Multiple children with sibling links
- `InsertBefore` - Insert child before reference
- `RemoveChild` - Remove child from parent
- `StructuralQueries` - First/last/only child, child index

**Selector Matching** (15+ tests):
- Type selectors (`div`, `span`)
- Class selectors (`.class`, multiple classes)
- ID selectors (`#id`)
- Attribute selectors (all 7 types: `[attr]`, `[attr="val"]`, `[attr~="val"]`, `[attr|="val"]`, `[attr^="val"]`, `[attr$="val"]`, `[attr*="val"]`)
- Universal selector (`*`)
- Compound selectors (e.g., `div.class#id[attr]`)

**Pseudo-Classes** (10+ tests):
- User action: `:hover`, `:active`, `:focus`, `:visited`
- Input states: `:enabled`, `:disabled`, `:checked`, `:required`, `:optional`, `:valid`, `:invalid`, `:read-only`, `:read-write`
- Structural: `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:nth-last-child()`

**Combinators** (6 tests):
- Descendant (`div p`)
- Child (`div > p`)
- Adjacent sibling (`h1 + p`)
- General sibling (`h1 ~ p`)

**Advanced Features** (20+ tests):
- Quirks mode (case-insensitive class/attribute matching)
- Hybrid attribute storage (array → hash map transition)
- Selector caching
- Performance tests
- Edge cases (null params, empty strings, special chars)

**Integration** (10+ tests):
- Complete style application
- Inline styles with specificity
- Complex cascades
- Form element hierarchy
- Table structures

### DomText Tests (9 tests) ✅ NEW
**Creation & Basic Operations**:
- `DomText_Create` - Basic text node creation
- `DomText_CreateEmpty` - Empty text node
- `DomText_CreateNull` - Null parameter handling
- `DomText_SetContent` - Update text content
- `DomText_SetContentEmpty` - Clear text content
- `DomText_SetContentNull` - Null content handling

**Edge Cases**:
- `DomText_LongContent` - Long text content handling
- `DomText_SpecialCharacters` - Newlines, tabs, special chars

### DomComment Tests (6 tests) ✅ NEW
**Node Creation**:
- `DomComment_CreateComment` - Standard HTML comments
- `DomComment_CreateDoctype` - DOCTYPE declarations
- `DomComment_CreateXMLDeclaration` - XML declarations

**Edge Cases**:
- `DomComment_EmptyContent` - Empty comment content
- `DomComment_NullParameters` - Null parameter handling
- `DomComment_MultilineContent` - Multiline comment content

### Node Type Utility Tests (4 tests) ✅ NEW
**Type Detection**:
- `NodeType_GetType` - Get node type enum
- `NodeType_GetTypeNull` - Null parameter handling
- `NodeType_IsElement` - Element type check
- `NodeType_IsText` - Text node type check
- `NodeType_IsComment` - Comment/DOCTYPE type check

### Mixed DOM Tree Tests (11 tests) ✅ NEW
**Basic Mixed Trees**:
- `MixedTree_ElementWithTextChild` - Element containing text
- `MixedTree_ElementWithCommentChild` - Element containing comment
- `MixedTree_ElementTextElement` - Alternating elements and text
- `MixedTree_AllNodeTypes` - Elements + text + comments together

**Navigation & Manipulation**:
- `MixedTree_NavigateSiblings` - Sibling navigation across types
- `MixedTree_RemoveTextNode` - Remove text node from tree
- `MixedTree_InsertTextBefore` - Insert text before element
- `MixedTree_MultipleTextNodes` - Multiple consecutive text nodes

**Complex Scenarios**:
- `MixedTree_NestedWithText` - Nested elements with text nodes
- `MixedTree_CommentsBetweenElements` - Comments interspersed
- `MixedTree_DoctypeAtStart` - DOCTYPE node handling

### Memory Management Tests (3 tests) ✅ NEW
- `Memory_TextNodeDestroy` - Text node cleanup
- `Memory_CommentNodeDestroy` - Comment node cleanup
- `Memory_MixedTreeCleanup` - Pool-based cleanup for mixed trees

## 📊 Parameterized Tests

For testing multiple similar cases:

```cpp
struct TestCase {
    const char* input;
    CssTokenType expected_type;
    const char* expected_value;
};

class ParameterizedTest : public ::testing::TestWithParam<TestCase> {};

TEST_P(ParameterizedTest, TestName) {
    auto test_case = GetParam();
    // Test implementation
}

INSTANTIATE_TEST_SUITE_P(
    TestGroup,
    ParameterizedTest,
    ::testing::Values(
        TestCase{"div", CSS_TOKEN_IDENT, "div"},
        TestCase{".container", CSS_TOKEN_DELIM, "."}
        // ... more cases
    )
);
```

## 🏃 Implementation Phases

### ✅ Phase 0: DOM Foundation (COMPLETED)
- 98 DomElement tests covering all core functionality
- 9 DomText tests for text node operations
- 6 DomComment tests for comments/DOCTYPE
- 11 Mixed DOM tree tests
- 4 Node type utility tests
- 3 Memory management tests
- **Total: 131 comprehensive DOM tests**

### Phase 1: Foundation (Week 1)
- Create directory structure
- Implement test helpers
- Set up fixtures
- Update build config

### Phase 2: Tokenizer Tests (Week 2)
- 100+ tokenizer tests
- Target: 95% coverage

### Phase 3: Parser Tests (Week 3)
- 80+ parser tests
- Target: 90% coverage

### Phase 4: Integration & Properties (Week 4)
- Integration tests
- Property validation tests

## 📦 Build Configuration

Add to `build_lambda_config.json` in `test_suites` → `input` → `tests`:

```json
{
    "source": "test/css/unit/test_css_tokenizer_unit.cpp",
    "name": "🎨 CSS Tokenizer Unit Tests (Comprehensive)",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_tokenizer_unit.exe"
}
```


## 📚 Related Documentation

- **Full Strategy**: `CSS_Testing_Strategy.md` (complete details)
- **Test Helpers**: `test/css/helpers/css_test_helpers.hpp` (API documentation)
- **Example Tests**: `test/css/unit/test_css_tokenizer_unit.cpp` (100+ examples)

## 🔗 Key Files

| File | Purpose |
|------|---------|
| `lambda/input/css/css_tokenizer.c` | Tokenizer implementation |
| `lambda/input/css/css_parser.c` | Parser implementation |
| `lambda/input/css/css_integration.c` | Integration layer |
| `test/css/helpers/css_test_helpers.hpp` | Test utilities |
| `test/css/unit/*.cpp` | Unit test suites |
| `test/css/fixtures/` | Test data files |

## 💡 Tips

- Run tests frequently during development
- Use `--gtest_filter` to run specific tests
- Check coverage with `make test-css-coverage`
- Add regression test immediately when bug is found
- Keep tests fast (<1 second each)
- Use fixtures for complex test data

## 🔧 DOM API Reference (Tested)

### DomText API ✅
```c
// Creation & Destruction
DomText* dom_text_create(Pool* pool, const char* text);
void dom_text_destroy(DomText* text_node);

// Content Operations
const char* dom_text_get_content(const DomText* text_node);
bool dom_text_set_content(DomText* text_node, const char* new_text);

// Structure
typedef struct DomText {
    DomNodeType node_type;       // Always DOM_NODE_TEXT
    char* text;                  // Text content
    size_t length;               // Content length
    DomElement* parent;          // Parent element
    void* next_sibling;          // Next sibling (any type)
    void* prev_sibling;          // Previous sibling (any type)
    Pool* pool;                  // Memory pool
} DomText;
```

### DomComment API ✅
```c
// Creation & Destruction
DomComment* dom_comment_create(Pool* pool, DomNodeType node_type,
                               const char* tag_name, const char* content);
void dom_comment_destroy(DomComment* comment_node);

// Content Operations
const char* dom_comment_get_content(const DomComment* comment_node);

// Structure
typedef struct DomComment {
    DomNodeType node_type;       // DOM_NODE_COMMENT or DOM_NODE_DOCTYPE
    char* tag_name;              // "comment", "!DOCTYPE", "?xml", etc.
    char* content;               // Comment/DOCTYPE content
    size_t length;               // Content length
    DomElement* parent;          // Parent element
    void* next_sibling;          // Next sibling (any type)
    void* prev_sibling;          // Previous sibling (any type)
    Pool* pool;                  // Memory pool
} DomComment;
```

### Node Type Utilities ✅
```c
// Type Detection
typedef enum DomNodeType {
    DOM_NODE_ELEMENT = 1,
    DOM_NODE_TEXT = 3,
    DOM_NODE_COMMENT = 8,
    DOM_NODE_DOCUMENT = 9,
    DOM_NODE_DOCTYPE = 10
} DomNodeType;

DomNodeType dom_node_get_type(const void* node);
bool dom_node_is_element(const void* node);
bool dom_node_is_text(const void* node);
bool dom_node_is_comment(const void* node);
```

### DomElement Updates ✅
```c
// Enhanced DomElement structure (now supports mixed children)
typedef struct DomElement {
    DomNodeType node_type;       // Always DOM_NODE_ELEMENT
    char* tag_name;
    char* id;
    // ... other fields ...
    void* first_child;           // First child (any type)
    void* next_sibling;          // Next sibling (any type)
    void* prev_sibling;          // Previous sibling (any type)
    DomElement* parent;
    Pool* pool;
} DomElement;

// Tree manipulation (now accepts all node types via void*)
bool dom_element_append_child(DomElement* parent, DomElement* child);
bool dom_element_insert_before(DomElement* parent, DomElement* new_child, DomElement* ref_child);
bool dom_element_remove_child(DomElement* parent, DomElement* child);
```

### Usage Example
```c
// Create mixed DOM tree
Pool* pool = pool_create(8192);
DomElement* div = dom_element_create(pool, "div", nullptr);
DomText* text = dom_text_create(pool, "Hello ");
DomElement* strong = dom_element_create(pool, "strong", nullptr);
DomText* strong_text = dom_text_create(pool, "World");
DomComment* comment = dom_comment_create(pool, DOM_NODE_COMMENT, "comment", " End ");

// Build tree: <div>Hello <strong>World</strong><!-- End --></div>
dom_element_append_child(div, (DomElement*)text);
dom_element_append_child(div, strong);
dom_element_append_child(strong, (DomElement*)strong_text);
dom_element_append_child(div, (DomElement*)comment);

// Navigate with type checking
void* child = div->first_child;
while (child) {
    if (dom_node_is_text(child)) {
        DomText* text_node = (DomText*)child;
        printf("Text: %s\n", dom_text_get_content(text_node));
        child = text_node->next_sibling;
    } else if (dom_node_is_element(child)) {
        DomElement* elem = (DomElement*)child;
        printf("Element: %s\n", elem->tag_name);
        child = elem->next_sibling;
    } else if (dom_node_is_comment(child)) {
        DomComment* comm = (DomComment*)child;
        printf("Comment: %s\n", dom_comment_get_content(comm));
        child = comm->next_sibling;
    }
}

pool_destroy(pool);  // Cleanup all nodes
```

## 🎯 Current Status

✅ **Completed**:
- **DOM Implementation**: DomText, DomComment, mixed DOM trees
- **DOM Tests**: 131 comprehensive tests covering all node types
- **Test Coverage**: Elements, text nodes, comments, mixed trees, utilities
- Tokenizer bug fixes (`.container` tokenization)
- Parser bug fixes (selector value extraction)
- Integration bug fixes (error recovery)
- Basic test infrastructure (14 existing test files)

🚧 **Proposed**:
- Comprehensive CSS unit test suite (5 new test files)
- Test helper library
- Fixture management system
- Regression test coverage
- 90%+ code coverage

## 📞 Getting Help

- Review full strategy in `CSS_Testing_Strategy.md`
- Check test helper API in `test/css/helpers/css_test_helpers.hpp`
- Look at example tests in `test/css/unit/test_css_tokenizer_unit.cpp`
- Examine existing tests in `test/test_css_*_gtest.cpp`
- **DOM tests**: See `test/test_css_dom_integration.cpp` for 131 comprehensive examples
