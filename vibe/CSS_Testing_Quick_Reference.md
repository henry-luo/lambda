# CSS Testing Quick Reference

## 📋 Summary

This is a quick reference for the comprehensive CSS testing strategy for Lambda. See `CSS_Testing_Strategy.md` for full details.

## 🏗️ Proposed Test Architecture

```
5 Testing Layers:

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

Layer 4: Regression Tests
├── Issue #001: Class selector null values
├── Issue #002: .container tokenization
├── Issue #003: Properties as selectors
└── (Future bugs captured here)

Layer 5: Property Validation Tests (50+ tests)
├── Color properties
├── Length properties
├── Display properties
├── Shorthand properties
└── Custom properties
```

## 🏗️ Test Structure

```
test/css/
├── unit/                                    # Comprehensive unit tests
│   ├── test_css_tokenizer_unit.cpp         # 100+ tokenizer tests
│   ├── test_css_parser_unit.cpp            # 80+ parser tests
│   ├── test_css_integration_unit.cpp       # 60+ integration tests
│   ├── test_css_properties.cpp             # 50+ property tests
│   └── test_css_regression.cpp             # Bug regression tests
├── fixtures/                                # Test data files
│   ├── valid/                              # Valid CSS samples
│   └── invalid/                            # Invalid CSS samples
└── helpers/
    └── css_test_helpers.hpp                # Shared test utilities
```

## 🎯 Test Coverage Goals

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

## 🎯 Current Status

✅ **Completed**:
- Tokenizer bug fixes (`.container` tokenization)
- Parser bug fixes (selector value extraction)
- Integration bug fixes (error recovery)
- Basic test infrastructure (11 existing test files)

🚧 **Proposed**:
- Comprehensive unit test suite (5 new test files)
- Test helper library
- Fixture management system
- Regression test coverage
- 90%+ code coverage

## 📞 Getting Help

- Review full strategy in `CSS_Testing_Strategy.md`
- Check test helper API in `test/css/helpers/css_test_helpers.hpp`
- Look at example tests in `test/css/unit/test_css_tokenizer_unit.cpp`
- Examine existing tests in `test/test_css_*_gtest.cpp`
