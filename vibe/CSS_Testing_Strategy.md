# CSS Parsing Unit Testing Strategy for Lambda

## Overview

This document proposes a comprehensive, structured unit testing approach for Lambda's CSS parsing system. The strategy is designed to provide thorough coverage across all CSS parsing layers while maintaining clarity, maintainability, and integration with Lambda's existing build system.

## Current State Analysis

### Existing Components

Lambda's CSS parsing is organized into three distinct layers:

1. **Tokenizer Layer** (`lambda/input/css/css_tokenizer.c`)
   - Converts CSS text into token streams
   - Handles Unicode, CSS3+ syntax, escape sequences
   - Existing tests: `test_css_tokenizer_gtest.cpp` (basic smoke tests)

2. **Parser Layer** (`lambda/input/css/css_parser.c`)
   - Parses selectors, declarations, rules from tokens
   - Handles selector types (element, class, ID, universal)
   - Existing tests: `test_css_parser_gtest.cpp` (minimal coverage)

3. **Integration Layer** (`lambda/input/css/css_integration.c`)
   - Orchestrates parsing, applies stylesheets to DOM
   - Handles error recovery, feature detection
   - Existing tests: `test_css_integration_gtest.cpp` (workflow tests)

### Current Test Infrastructure

- **Framework**: Google Test (GTest) for C++ tests
- **Build System**: Premake5-based Makefile generation via `build_lambda_config.json`
- **Memory Management**: Pool-based allocation with rpmalloc
- **Test Location**: `/test/` directory with naming convention `test_*_gtest.cpp`
- **Existing CSS Tests**: 11 test files covering various aspects (tokenizer, parser, integration, frameworks, files)

## Proposed Testing Architecture

### Layer 1: Tokenizer Unit Tests

**File**: `test/test_css_tokenizer_unit.cpp`

**Purpose**: Comprehensive tokenization validation at the lowest level

**Test Categories**:

```cpp
// 1. Basic Token Types
TEST(CssTokenizerUnit, BasicIdentifiers)
TEST(CssTokenizerUnit, Numbers)
TEST(CssTokenizerUnit, Dimensions)
TEST(CssTokenizerUnit, Percentages)
TEST(CssTokenizerUnit, Strings)
TEST(CssTokenizerUnit, HashTokens)
TEST(CssTokenizerUnit, Delimiters)

// 2. CSS3+ Features
TEST(CssTokenizerUnit, CustomProperties)
TEST(CssTokenizerUnit, CSS4Functions)
TEST(CssTokenizerUnit, UnicodeEscapes)
TEST(CssTokenizerUnit, UnicodeIdentifiers)

// 3. Edge Cases
TEST(CssTokenizerUnit, UnterminatedStrings)
TEST(CssTokenizerUnit, InvalidEscapes)
TEST(CssTokenizerUnit, EmptyInput)
TEST(CssTokenizerUnit, OnlyWhitespace)
TEST(CssTokenizerUnit, LargeInput)

// 4. Numeric Tokenization
TEST(CssTokenizerUnit, DecimalNumbers)
TEST(CssTokenizerUnit, ClassSelectorDotHandling)  // Critical: .container vs .5
TEST(CssTokenizerUnit, SignedNumbers)
TEST(CssTokenizerUnit, ScientificNotation)

// 5. Function Tokenization
TEST(CssTokenizerUnit, CalcFunction)
TEST(CssTokenizerUnit, VarFunction)
TEST(CssTokenizerUnit, ColorFunctions)
TEST(CssTokenizerUnit, NestedFunctions)

// 6. Error Recovery
TEST(CssTokenizerUnit, BadUrl)
TEST(CssTokenizerUnit, BadString)
TEST(CssTokenizerUnit, UnexpectedEOF)
```

**Key Test Patterns**:

```cpp
// Parameterized tests for token type validation
struct TokenTestCase {
    const char* input;
    CssTokenType expected_type;
    const char* expected_value;
    size_t expected_count;
};

class TokenizerParameterizedTest : public ::testing::TestWithParam<TokenTestCase> {};

TEST_P(TokenizerParameterizedTest, TokenType) {
    auto test_case = GetParam();
    Pool* pool = pool_create();

    size_t count;
    CssToken* tokens = css_tokenize(test_case.input, strlen(test_case.input), pool, &count);

    ASSERT_EQ(count, test_case.expected_count);
    EXPECT_EQ(tokens[0].type, test_case.expected_type);
    if (test_case.expected_value) {
        EXPECT_STREQ(tokens[0].value, test_case.expected_value);
    }

    pool_destroy(pool);
}

INSTANTIATE_TEST_SUITE_P(
    BasicTokens,
    TokenizerParameterizedTest,
    ::testing::Values(
        TokenTestCase{"div", CSS_TOKEN_IDENT, "div", 1},
        TokenTestCase{".container", CSS_TOKEN_DELIM, ".", 2},  // DELIM + IDENT
        TokenTestCase{".5", CSS_TOKEN_NUMBER, "0.5", 1},
        TokenTestCase{"#id", CSS_TOKEN_HASH, "id", 1},
        TokenTestCase{"10px", CSS_TOKEN_DIMENSION, "10", 1}
    )
);
```

### Layer 2: Parser Unit Tests

**File**: `test/test_css_parser_unit.cpp`

**Purpose**: Test selector and declaration parsing logic in isolation

**Test Categories**:

```cpp
// 1. Simple Selector Parsing
TEST(CssParserUnit, ElementSelector)
TEST(CssParserUnit, ClassSelector)
TEST(CssParserUnit, IDSelector)
TEST(CssParserUnit, UniversalSelector)

// 2. Compound Selectors (Future)
TEST(CssParserUnit, ElementWithClass)
TEST(CssParserUnit, MultipleClasses)
TEST(CssParserUnit, AttributeSelectors)

// 3. Multiple Selectors
TEST(CssParserUnit, CommaSeparatedSelectors)
TEST(CssParserUnit, WhitespaceHandling)

// 4. Declaration Parsing
TEST(CssParserUnit, SimpleDeclaration)
TEST(CssParserUnit, MultipleDeclarations)
TEST(CssParserUnit, ImportantFlag)
TEST(CssParserUnit, CustomProperties)

// 5. Value Parsing
TEST(CssParserUnit, ColorValues)
TEST(CssParserUnit, LengthValues)
TEST(CssParserUnit, KeywordValues)
TEST(CssParserUnit, FunctionValues)

// 6. Rule Parsing
TEST(CssParserUnit, CompleteRule)
TEST(CssParserUnit, MultipleRules)
TEST(CssParserUnit, NestedDeclarations)

// 7. Error Handling
TEST(CssParserUnit, MissingClosingBrace)
TEST(CssParserUnit, InvalidSelector)
TEST(CssParserUnit, InvalidProperty)
TEST(CssParserUnit, MalformedDeclaration)
```

**Key Test Patterns**:

```cpp
// Test fixture with helper methods
class CssParserUnitTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
    }

    void TearDown() override {
        pool_destroy(pool);
    }

    // Helper: Tokenize and parse selector
    CssSimpleSelector* ParseSelector(const char* css) {
        size_t token_count;
        CssToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        int pos = 0;
        return css_parse_simple_selector_from_tokens(tokens, &pos, token_count, pool);
    }

    // Helper: Parse complete rule
    CssRule* ParseRule(const char* css) {
        size_t token_count;
        CssToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        return css_parse_rule_from_tokens(tokens, token_count, pool);
    }

    // Helper: Validate selector
    void AssertSelector(CssSimpleSelector* sel, CssSelectorType type, const char* value) {
        ASSERT_NE(sel, nullptr);
        EXPECT_EQ(sel->type, type);
        EXPECT_STREQ(sel->value, value);
    }
};

TEST_F(CssParserUnitTest, ElementSelector) {
    auto selector = ParseSelector("div");
    AssertSelector(selector, CSS_SELECTOR_TYPE_ELEMENT, "div");
}

TEST_F(CssParserUnitTest, ClassSelector) {
    auto selector = ParseSelector(".container");
    AssertSelector(selector, CSS_SELECTOR_TYPE_CLASS, "container");
}
```

### Layer 3: Integration Unit Tests

**File**: `test/test_css_integration_unit.cpp`

**Purpose**: Test end-to-end stylesheet parsing and application

**Test Categories**:

```cpp
// 1. Stylesheet Parsing
TEST(CssIntegrationUnit, EmptyStylesheet)
TEST(CssIntegrationUnit, SingleRule)
TEST(CssIntegrationUnit, MultipleRules)
TEST(CssIntegrationUnit, ExternalStylesheet)

// 2. Error Recovery
TEST(CssIntegrationUnit, RecoverFromInvalidRule)
TEST(CssIntegrationUnit, SkipInvalidDeclarations)
TEST(CssIntegrationUnit, BraceDepthTracking)
TEST(CssIntegrationUnit, PartialParseSuccess)

// 3. Feature Detection
TEST(CssIntegrationUnit, DetectCustomProperties)
TEST(CssIntegrationUnit, DetectPseudoClasses)
TEST(CssIntegrationUnit, DetectMediaQueries)

// 4. Cascade Priority
TEST(CssIntegrationUnit, InlineVsExternal)
TEST(CssIntegrationUnit, SelectorSpecificity)
TEST(CssIntegrationUnit, ImportanceOverride)

// 5. Style Application
TEST(CssIntegrationUnit, ApplyToSingleElement)
TEST(CssIntegrationUnit, ApplyToMultipleElements)
TEST(CssIntegrationUnit, CascadeResolution)
```

### Layer 4: Regression Tests

**File**: `test/test_css_regression.cpp`

**Purpose**: Capture and prevent regressions from past bugs

**Test Structure**:

```cpp
// Each test captures a specific bug from previous sessions
TEST(CssRegression, Issue001_ClassSelectorNull) {
    // Bug: Class selector values were showing as (null)
    // Fixed: Extract from token->start/length instead of token->value
    Pool* pool = pool_create();

    const char* css = ".container { width: 100%; }";
    CssRule* rule = /* parse */;

    ASSERT_NE(rule, nullptr);
    ASSERT_NE(rule->selectors[0], nullptr);
    EXPECT_EQ(rule->selectors[0]->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(rule->selectors[0]->value, "container");

    pool_destroy(pool);
}

TEST(CssRegression, Issue002_DotTokenization) {
    // Bug: .container tokenized as DIMENSION instead of DELIM+IDENT
    // Fixed: Separate logic for '.' followed by digit vs identifier
    Pool* pool = pool_create();

    size_t count;
    CssToken* tokens = css_tokenize(".container", 10, pool, &count);

    ASSERT_GE(count, 2);
    EXPECT_EQ(tokens[0].type, CSS_TOKEN_DELIM);
    EXPECT_EQ(tokens[0].data.delimiter, '.');
    EXPECT_EQ(tokens[1].type, CSS_TOKEN_IDENT);
    EXPECT_STREQ(tokens[1].value, "container");

    pool_destroy(pool);
}

TEST(CssRegression, Issue003_PropertyAsSelector) {
    // Bug: Property names like 'max-width' treated as element selectors
    // Fixed: Proper brace depth tracking in error recovery
    Pool* pool = pool_create();

    const char* css = R"(
        .valid { color: red; }
        .invalid { color }
        .next { margin: 10px; }
    )";

    CssStylesheet* sheet = css_enhanced_parse_stylesheet(css, pool);

    // Should successfully parse .valid and .next, skipping .invalid
    EXPECT_EQ(sheet->rule_count, 2);

    pool_destroy(pool);
}
```

### Layer 5: Property-Based Testing

**File**: `test/test_css_properties.cpp`

**Purpose**: Test CSS property validation and value parsing

**Test Categories**:

```cpp
// 1. Color Properties
TEST(CssProperties, ColorKeywords)
TEST(CssProperties, HexColors)
TEST(CssProperties, RGBColors)
TEST(CssProperties, RGBAColors)
TEST(CssProperties, HSLColors)

// 2. Length Properties
TEST(CssProperties, PixelLengths)
TEST(CssProperties, EmLengths)
TEST(CssProperties, RemLengths)
TEST(CssProperties, ViewportUnits)
TEST(CssProperties, PercentageLengths)

// 3. Display Properties
TEST(CssProperties, DisplayValues)
TEST(CssProperties, PositionValues)
TEST(CssProperties, FlexProperties)
TEST(CssProperties, GridProperties)

// 4. Shorthand Properties
TEST(CssProperties, Margin)
TEST(CssProperties, Padding)
TEST(CssProperties, Border)
TEST(CssProperties, Background)

// 5. Custom Properties
TEST(CssProperties, CustomPropertyDeclaration)
TEST(CssProperties, CustomPropertyUsage)
TEST(CssProperties, CustomPropertyFallback)
```

## Test Organization Structure

```
test/
├── css/
│   ├── unit/
│   │   ├── test_css_tokenizer_unit.cpp       # Layer 1: Tokenizer
│   │   ├── test_css_parser_unit.cpp          # Layer 2: Parser
│   │   ├── test_css_integration_unit.cpp     # Layer 3: Integration
│   │   ├── test_css_properties.cpp           # Layer 5: Properties
│   │   └── test_css_regression.cpp           # Layer 4: Regressions
│   │
│   ├── fixtures/
│   │   ├── valid/                            # Valid CSS files for testing
│   │   │   ├── simple.css
│   │   │   ├── complex.css
│   │   │   ├── cascade.css
│   │   │   └── framework_subset.css
│   │   │
│   │   └── invalid/                          # Invalid CSS for error testing
│   │       ├── unterminated_string.css
│   │       ├── missing_brace.css
│   │       └── invalid_selector.css
│   │
│   └── helpers/
│       ├── css_test_helpers.hpp              # Shared test utilities
│       └── css_test_fixtures.cpp             # Fixture management
│
└── (existing test files...)
```

## Implementation Plan

### Phase 1: Foundation (Week 1)

1. Create test directory structure
2. Implement `css_test_helpers.hpp` with common utilities:
   ```cpp
   namespace CssTestHelpers {
       // Memory management
       class PoolGuard {
           Pool* pool;
       public:
           PoolGuard() : pool(pool_create()) {}
           ~PoolGuard() { pool_destroy(pool); }
           Pool* get() { return pool; }
       };

       // Token comparison
       void AssertToken(const CssToken* token, CssTokenType type,
                       const char* value = nullptr);

       // Selector validation
       void AssertSelector(const CssSimpleSelector* sel,
                          CssSelectorType type, const char* value);

       // Declaration validation
       void AssertDeclaration(const CssDeclaration* decl,
                             const char* property, const char* value);
   }
   ```

3. Set up fixture files
4. Update `build_lambda_config.json` with new test entries

### Phase 2: Tokenizer Tests (Week 2)

1. Implement `test_css_tokenizer_unit.cpp`
2. 100+ test cases covering:
   - All token types
   - Edge cases
   - Unicode support
   - Error recovery
3. Achieve >95% code coverage of `css_tokenizer.c`

### Phase 3: Parser Tests (Week 3)

1. Implement `test_css_parser_unit.cpp`
2. 80+ test cases covering:
   - All selector types
   - Declaration parsing
   - Value parsing
   - Rule parsing
3. Achieve >90% code coverage of `css_parser.c`

### Phase 4: Integration & Properties (Week 4)

1. Implement `test_css_integration_unit.cpp`
2. Implement `test_css_properties.cpp`
3. 60+ integration test cases
4. 50+ property validation test cases

### Phase 5: Regression & Maintenance (Week 5)

1. Implement `test_css_regression.cpp` with all known issues
2. Create test documentation
3. Set up continuous integration
4. Code coverage reporting

## Build System Integration

### Update `build_lambda_config.json`

Add to `test_suites` → `input` → `tests`:

```json
{
    "source": "test/css/unit/test_css_tokenizer_unit.cpp",
    "name": "🎨 CSS Tokenizer Unit Tests (Comprehensive)",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_tokenizer_unit.exe"
},
{
    "source": "test/css/unit/test_css_parser_unit.cpp",
    "name": "🎨 CSS Parser Unit Tests (Comprehensive)",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_parser_unit.exe"
},
{
    "source": "test/css/unit/test_css_integration_unit.cpp",
    "name": "🎨 CSS Integration Unit Tests (Comprehensive)",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_integration_unit.exe"
},
{
    "source": "test/css/unit/test_css_properties.cpp",
    "name": "🎨 CSS Property Validation Tests",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_properties.exe"
},
{
    "source": "test/css/unit/test_css_regression.cpp",
    "name": "🐛 CSS Regression Tests",
    "dependencies": ["lambda-input-full"],
    "libraries": ["gtest", "gtest_main", "rpmalloc"],
    "binary": "test_css_regression.exe"
}
```

### Makefile Targets

```makefile
# Run all CSS unit tests
test-css-unit: test_css_tokenizer_unit.exe test_css_parser_unit.exe \
               test_css_integration_unit.exe test_css_properties.exe \
               test_css_regression.exe
	@echo "Running CSS tokenizer unit tests..."
	@./test/test_css_tokenizer_unit.exe
	@echo "Running CSS parser unit tests..."
	@./test/test_css_parser_unit.exe
	@echo "Running CSS integration unit tests..."
	@./test/test_css_integration_unit.exe
	@echo "Running CSS property tests..."
	@./test/test_css_properties.exe
	@echo "Running CSS regression tests..."
	@./test/test_css_regression.exe

# Run CSS tests with coverage
test-css-coverage:
	@llvm-cov gcov lambda/input/css/*.c
	@lcov --capture --directory . --output-file coverage.info
	@genhtml coverage.info --output-directory coverage_html
```

## Test Execution Strategy

### Local Development

```bash
# Build specific test suite
make test_css_tokenizer_unit.exe

# Run specific test suite
./test/test_css_tokenizer_unit.exe

# Run all CSS unit tests
make test-css-unit

# Run with verbose output
./test/test_css_tokenizer_unit.exe --gtest_verbose

# Run specific test case
./test/test_css_tokenizer_unit.exe --gtest_filter=CssTokenizerUnit.ClassSelector*
```

### Continuous Integration

```yaml
# GitHub Actions workflow (example)
name: CSS Tests
on: [push, pull_request]

jobs:
  css-unit-tests:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build CSS tests
        run: make build-test
      - name: Run CSS unit tests
        run: make test-css-unit
      - name: Generate coverage
        run: make test-css-coverage
      - name: Upload coverage
        uses: codecov/codecov-action@v2
```

## Coverage Goals

| Component | Target Coverage | Priority |
|-----------|----------------|----------|
| Tokenizer | 95%+ | High |
| Parser | 90%+ | High |
| Integration | 85%+ | Medium |
| Properties | 80%+ | Medium |
| Error paths | 70%+ | Medium |

## Success Metrics

1. **Coverage**: >90% code coverage across CSS parsing modules
2. **Reliability**: All tests pass consistently across platforms
3. **Speed**: Full test suite runs in <30 seconds
4. **Maintainability**: Clear test names, good documentation
5. **Regression Prevention**: All known bugs have test cases

## Testing Best Practices

### 1. Test Naming Convention

```cpp
// Pattern: TEST(Component, Behavior_Condition_ExpectedResult)
TEST(CssTokenizer, TokenizeIdentifier_BasicASCII_ReturnsIdentToken)
TEST(CssParser, ParseSelector_ClassWithDot_ReturnsClassSelector)
TEST(CssIntegration, ParseStylesheet_InvalidRule_ContinuesParsing)
```

### 2. AAA Pattern (Arrange-Act-Assert)

```cpp
TEST_F(CssParserUnitTest, ParseDeclaration_WithImportant) {
    // Arrange
    const char* css = "color: red !important";

    // Act
    CssDeclaration* decl = ParseDeclaration(css);

    // Assert
    ASSERT_NE(decl, nullptr);
    EXPECT_STREQ(decl->property, "color");
    EXPECT_STREQ(decl->value, "red");
    EXPECT_TRUE(decl->important);
}
```

### 3. Parameterized Tests for Data-Driven Testing

```cpp
struct ColorTestCase {
    const char* input;
    uint8_t expected_r, expected_g, expected_b, expected_a;
};

class ColorParsingTest : public ::testing::TestWithParam<ColorTestCase> {};

TEST_P(ColorParsingTest, ParseColor) {
    auto test_case = GetParam();
    CssColor color = ParseColor(test_case.input);

    EXPECT_EQ(color.r, test_case.expected_r);
    EXPECT_EQ(color.g, test_case.expected_g);
    EXPECT_EQ(color.b, test_case.expected_b);
    EXPECT_EQ(color.a, test_case.expected_a);
}

INSTANTIATE_TEST_SUITE_P(
    StandardColors,
    ColorParsingTest,
    ::testing::Values(
        ColorTestCase{"red", 255, 0, 0, 255},
        ColorTestCase{"#ff0000", 255, 0, 0, 255},
        ColorTestCase{"rgb(255, 0, 0)", 255, 0, 0, 255},
        ColorTestCase{"rgba(255, 0, 0, 0.5)", 255, 0, 0, 128}
    )
);
```

### 4. Fixture Management

```cpp
class CssTestFixture : public ::testing::Test {
protected:
    Pool* pool;
    CssEngine* engine;

    void SetUp() override {
        pool = pool_create();
        engine = css_engine_create(pool);
    }

    void TearDown() override {
        css_engine_destroy(engine);
        pool_destroy(pool);
    }

    // Load CSS file from fixtures
    const char* LoadFixture(const char* filename) {
        char path[256];
        snprintf(path, sizeof(path), "test/css/fixtures/%s", filename);
        return LoadFile(path, pool);
    }
};
```

## Documentation Requirements

Each test file should include:

```cpp
/**
 * CSS [Component] Unit Tests
 *
 * Purpose: [Brief description]
 *
 * Coverage:
 * - [Feature 1]
 * - [Feature 2]
 * - ...
 *
 * Test Categories:
 * 1. [Category 1] - [Description]
 * 2. [Category 2] - [Description]
 *
 * Known Limitations:
 * - [Limitation 1]
 * - [Limitation 2]
 *
 * Related Files:
 * - lambda/input/css/[source_file].c
 * - lambda/input/css/[header_file].h
 */
```

## Maintenance Plan

### Monthly Tasks

1. Review and update regression tests with new bugs
2. Check coverage reports and improve low-coverage areas
3. Update test fixtures with real-world CSS examples

### Quarterly Tasks

1. Performance benchmarking of test suite
2. Review and refactor test code for clarity
3. Update documentation with new patterns

### Per-Release Tasks

1. Run full test suite on all platforms
2. Verify all regression tests still pass
3. Update test data for new CSS features

## Conclusion

This structured approach to CSS parsing unit tests provides:

1. **Comprehensive Coverage**: Tests at every layer from tokenization to integration
2. **Maintainability**: Clear organization and naming conventions
3. **Regression Prevention**: Dedicated regression test suite
4. **Integration**: Seamless integration with existing Lambda build system
5. **Scalability**: Easy to add new tests as CSS support expands

By following this strategy, Lambda's CSS parsing will have industrial-strength test coverage, ensuring reliability and making future development safer and faster.
