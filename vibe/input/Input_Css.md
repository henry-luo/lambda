# CSS Parser and Formatter Enhancement Plan

## Current State Analysis

### Existing Implementation
The current CSS parser and formatter in Lambda has the following characteristics:

**Parser (`input-css.cpp`):**
- Hand-written recursive descent parser (1,321 lines)
- Character-by-character parsing with manual tokenization
- Hardcoded CSS function recognition (50+ functions listed)
- Basic CSS3 support for colors, units, functions
- Limited property validation
- Complex nested parsing logic for at-rules, selectors, and declarations

**Formatter (`format-css.cpp`):**
- Simple tree-walking formatter (349 lines)
- Basic indentation and structure formatting
- Limited CSS property knowledge
- No validation or optimization capabilities

**Limitations:**
- No formal tokenizer - parsing is character-based and error-prone
- Hardcoded CSS knowledge scattered throughout parser code
- Limited CSS3+ support compared to `css_grammar.txt` specification
- No comprehensive test coverage
- Tight coupling between parsing logic and CSS knowledge
- No property validation or value type checking

## Enhancement Objectives

1. **Tokenizer-Based Architecture**: Replace character-based parsing with formal tokenization
2. **Definition-Driven Design**: Centralize CSS knowledge in data structures
3. **Full CSS3+ Support**: Implement complete CSS specification from `css_grammar.txt`
4. **Comprehensive Testing**: Create robust test suite with validation
5. **Maintainable Codebase**: Clean separation of concerns and modular design

## Phase 1: Tokenizer Implementation ✅ COMPLETED

### 1.1 CSS Tokenizer Design ✅ COMPLETED
Created a state machine-based tokenizer supporting all CSS token types:

**Token Types:**
```c
typedef enum {
    CSS_TOKEN_IDENT,           // identifiers, keywords
    CSS_TOKEN_FUNCTION,        // function names followed by (
    CSS_TOKEN_AT_KEYWORD,      // @media, @keyframes, etc.
    CSS_TOKEN_HASH,            // #colors
    CSS_TOKEN_STRING,          // "quoted strings"
    CSS_TOKEN_URL,             // url() values
    CSS_TOKEN_NUMBER,          // numeric values
    CSS_TOKEN_DIMENSION,       // numbers with units (10px, 2em)
    CSS_TOKEN_PERCENTAGE,      // percentage values (50%)
    CSS_TOKEN_UNICODE_RANGE,   // U+0000-FFFF
    CSS_TOKEN_INCLUDE_MATCH,   // ~=
    CSS_TOKEN_DASH_MATCH,      // |=
    CSS_TOKEN_PREFIX_MATCH,    // ^=
    CSS_TOKEN_SUFFIX_MATCH,    // $=
    CSS_TOKEN_SUBSTRING_MATCH, // *=
    CSS_TOKEN_COLUMN,          // ||
    CSS_TOKEN_WHITESPACE,      // spaces, tabs, newlines
    CSS_TOKEN_COMMENT,         // /* comments */
    CSS_TOKEN_COLON,           // :
    CSS_TOKEN_SEMICOLON,       // ;
    CSS_TOKEN_COMMA,           // ,
    CSS_TOKEN_LEFT_SQUARE,     // [
    CSS_TOKEN_RIGHT_SQUARE,    // ]
    CSS_TOKEN_LEFT_PAREN,      // (
    CSS_TOKEN_RIGHT_PAREN,     // )
    CSS_TOKEN_LEFT_CURLY,      // {
    CSS_TOKEN_RIGHT_CURLY,     // }
    CSS_TOKEN_DELIM,           // single character delimiters
    CSS_TOKEN_EOF,             // end of input
    CSS_TOKEN_ERROR            // tokenization error
} CSSTokenType;

typedef struct {
    CSSTokenType type;
    const char* start;
    size_t length;
    union {
        double number_value;
        int hash_type;  // id vs unrestricted
        char delimiter;
    } data;
} CSSToken;
```

**Tokenizer API:**
```c
// Core tokenizer functions
CSSToken* css_tokenize(const char* input, size_t length, VariableMemPool* pool);
void css_free_tokens(CSSToken* tokens);
const char* css_token_type_to_str(CSSTokenType type);

// Token stream utilities
typedef struct {
    CSSToken* tokens;
    size_t current;
    size_t length;
} CSSTokenStream;

CSSTokenStream* css_token_stream_create(CSSToken* tokens, size_t length, VariableMemPool* pool);
CSSToken* css_token_stream_current(CSSTokenStream* stream);
CSSToken* css_token_stream_peek(CSSTokenStream* stream, size_t offset);
bool css_token_stream_advance(CSSTokenStream* stream);
bool css_token_stream_consume(CSSTokenStream* stream, CSSTokenType expected);
```

### 1.2 Implementation Files ✅ COMPLETED
- ✅ `lambda/input/css_tokenizer.h` - Tokenizer interface and types
- ✅ `lambda/input/css_tokenizer.c` - Tokenizer implementation
- ✅ `test/test_css_tokenizer.cpp` - Tokenizer unit tests

### 1.3 Test Coverage ✅ COMPLETED
- ✅ All CSS token types with edge cases
- ✅ Error recovery and malformed input handling
- ✅ Performance benchmarking with large stylesheets
- ✅ Unicode and escape sequence handling

## Phase 2: Definition-Driven Parser ✅ COMPLETED

### 2.1 CSS Property Database ✅ COMPLETED
Created comprehensive CSS property definitions with validation rules:

**Property Definition Structure:**
```c
typedef enum {
    CSS_VALUE_KEYWORD,      // named values (auto, none, inherit)
    CSS_VALUE_LENGTH,       // length values with units
    CSS_VALUE_PERCENTAGE,   // percentage values
    CSS_VALUE_NUMBER,       // numeric values
    CSS_VALUE_INTEGER,      // integer values
    CSS_VALUE_COLOR,        // color values
    CSS_VALUE_URL,          // URL values
    CSS_VALUE_STRING,       // string values
    CSS_VALUE_FUNCTION,     // function calls
    CSS_VALUE_CUSTOM_IDENT, // custom identifiers
    CSS_VALUE_ANGLE,        // angle values
    CSS_VALUE_TIME,         // time values
    CSS_VALUE_FREQUENCY,    // frequency values
    CSS_VALUE_RESOLUTION    // resolution values
} CSSValueType;

typedef struct {
    const char* name;
    CSSValueType type;
    const char** allowed_keywords;  // NULL-terminated array
    struct {
        double min;
        double max;
        bool has_min;
        bool has_max;
    } numeric_range;
    bool inherited;
    const char* initial_value;
    bool animatable;
} CSSPropertyDef;

typedef struct {
    const char* name;
    const char** values;  // NULL-terminated array
} CSSKeywordGroup;
```

**Property Database:**
```c
// Global property definitions based on css_grammar.txt
extern const CSSPropertyDef css_properties[];
extern const size_t css_properties_count;

// Keyword groups for reusable value sets
extern const CSSKeywordGroup css_keyword_groups[];
extern const size_t css_keyword_groups_count;

// Property lookup functions
const CSSPropertyDef* css_find_property(const char* name);
bool css_validate_property_value(const CSSPropertyDef* prop, Item value);
const char** css_get_keyword_group(const char* group_name);
```

### 2.2 Parser Refactoring ✅ COMPLETED
Rewrote parser to use tokenizer and property database:

**New Parser Architecture:**
```c
typedef struct {
    CSSTokenStream* tokens;
    VariableMemPool* pool;
    Element* current_rule;
    Array* errors;
} CSSParser;

// Core parsing functions
Item css_parse_stylesheet(CSSParser* parser);
Item css_parse_rule(CSSParser* parser);
Item css_parse_at_rule(CSSParser* parser);
Item css_parse_qualified_rule(CSSParser* parser);
Array* css_parse_selector_list(CSSParser* parser);
Item css_parse_selector(CSSParser* parser);
Array* css_parse_declaration_list(CSSParser* parser);
Item css_parse_declaration(CSSParser* parser);
Item css_parse_value(CSSParser* parser, const CSSPropertyDef* prop);

// Value parsing with validation
Item css_parse_length(CSSParser* parser);
Item css_parse_color(CSSParser* parser);
Item css_parse_function(CSSParser* parser);
Item css_parse_keyword(CSSParser* parser, const char** allowed_keywords);
```

### 2.3 AST Structure Enhancement ✅ COMPLETED
Defined clean AST structure for CSS with source position information:

```c
typedef struct {
    const char* file;
    size_t line;
    size_t column;
    size_t offset;
} CSSSourcePos;

typedef struct {
    CSSSourcePos start;
    CSSSourcePos end;
} CSSSourceRange;

// Enhanced element creation with source tracking
Element* css_create_element_with_source(Input* input, const char* type, CSSSourceRange range);
void css_add_source_range(Element* element, CSSSourceRange range);
```

## Phase 3: Full CSS3+ Support (4 weeks)

### 3.1 Complete Property Implementation
Implement all properties from `css_grammar.txt`:

**Property Categories:**
- **Layout**: `display`, `position`, `float`, `clear`, `flex-*`, `grid-*`
- **Box Model**: `width`, `height`, `margin`, `padding`, `border`
- **Typography**: `font-*`, `text-*`, `line-height`, `letter-spacing`
- **Colors & Backgrounds**: `color`, `background-*`, `opacity`
- **Transforms & Animations**: `transform`, `transition-*`, `animation-*`
- **Advanced**: `filter`, `mask`, `clip-path`, `scroll-*`

**Implementation Files:** ✅ COMPLETED
- ✅ `lambda/input/css_properties.h` - Property definitions header
- ✅ `lambda/input/css_properties.c` - Property database implementation
- ✅ `lambda/input/css_parser.h` - Parser interface
- ✅ `lambda/input/css_parser.c` - Parser implementation

### 3.2 Advanced Selector Support
Implement complete CSS3+ selector syntax:

**Selector Types:**
- Type selectors: `div`, `span`
- Class selectors: `.class`
- ID selectors: `#id`
- Attribute selectors: `[attr]`, `[attr="value"]`, `[attr^="prefix"]`
- Pseudo-classes: `:hover`, `:nth-child()`, `:not()`
- Pseudo-elements: `::before`, `::after`, `::first-line`
- Combinators: ` ` (descendant), `>` (child), `+` (adjacent), `~` (sibling)

**Specificity Calculation:**
```c
typedef struct {
    int id_count;
    int class_count;
    int type_count;
    int total;
} CSSSpecificity;

CSSSpecificity css_calculate_specificity(const char* selector);
int css_compare_specificity(CSSSpecificity a, CSSSpecificity b);
```

### 3.3 At-Rule Support
Complete implementation of CSS at-rules:

**At-Rule Types:**
- `@media` - Media queries with full syntax support
- `@keyframes` - Animation keyframes
- `@font-face` - Font loading
- `@import` - Stylesheet imports
- `@supports` - Feature queries
- `@layer` - Cascade layers
- `@container` - Container queries
- `@page` - Paged media
- `@namespace` - XML namespaces

### 3.4 CSS Functions and Values
Implement advanced CSS functions:

**Mathematical Functions:**
- `calc()`, `min()`, `max()`, `clamp()`
- `round()`, `mod()`, `rem()`, `sin()`, `cos()`, `tan()`

**Color Functions:**
- `rgb()`, `rgba()`, `hsl()`, `hsla()`
- `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`
- `color()`, `color-mix()`

**Transform Functions:**
- `translate()`, `rotate()`, `scale()`, `skew()`
- `matrix()`, `perspective()`

**Filter Functions:**
- `blur()`, `brightness()`, `contrast()`, `grayscale()`
- `hue-rotate()`, `invert()`, `opacity()`, `saturate()`, `sepia()`

## Phase 4: Enhanced Formatter (2 weeks)

### 4.1 Configurable Formatting Engine
Create flexible formatter with style options:

**Formatting Options:**
```c
typedef struct {
    int indent_size;
    bool use_tabs;
    bool minify;
    bool sort_properties;
    bool preserve_comments;
    bool single_line_rules;
    int max_line_length;
    bool space_around_combinators;
    bool space_after_colon;
    bool trailing_semicolon;
} CSSFormatOptions;

// Formatter API
String* css_format_with_options(VariableMemPool* pool, Item stylesheet, CSSFormatOptions* options);
String* css_format_minified(VariableMemPool* pool, Item stylesheet);
String* css_format_pretty(VariableMemPool* pool, Item stylesheet);
```

### 4.2 Source Map Support
Generate source maps for debugging:

```c
typedef struct {
    const char* source_file;
    Array* mappings;  // Array of CSSSourceMapping
} CSSSourceMap;

typedef struct {
    size_t generated_line;
    size_t generated_column;
    size_t source_line;
    size_t source_column;
} CSSSourceMapping;

CSSSourceMap* css_generate_source_map(VariableMemPool* pool, Item stylesheet, CSSFormatOptions* options);
String* css_source_map_to_json(VariableMemPool* pool, CSSSourceMap* map);
```

### 4.3 Property Sorting and Optimization
Implement intelligent property organization:

**Property Groups:**
- Layout properties first
- Box model properties
- Typography properties
- Visual properties
- Animation properties

**Optimization Features:**
- Shorthand property consolidation
- Redundant property removal
- Color value optimization
- Unit normalization

## Phase 5: Comprehensive Testing (2 weeks)

### 5.1 Unit Test Suite Structure
Create comprehensive test coverage:

**Test Files:**
- `test/test_css_tokenizer.cpp` - Tokenizer tests
- `test/test_css_parser.cpp` - Parser tests
- `test/test_css_properties.cpp` - Property validation tests
- `test/test_css_formatter.cpp` - Formatter tests
- `test/test_css_integration.cpp` - End-to-end tests
- `test/test_css.cpp` - Main test runner

**Test Categories:**
```cpp
// Tokenizer tests
TEST(CSSTokenizer, BasicTokens)
TEST(CSSTokenizer, Numbers)
TEST(CSSTokenizer, Strings)
TEST(CSSTokenizer, Functions)
TEST(CSSTokenizer, AtRules)
TEST(CSSTokenizer, ErrorRecovery)

// Parser tests  
TEST(CSSParser, SimpleRules)
TEST(CSSParser, ComplexSelectors)
TEST(CSSParser, MediaQueries)
TEST(CSSParser, Keyframes)
TEST(CSSParser, PropertyValidation)
TEST(CSSParser, ErrorHandling)

// Formatter tests
TEST(CSSFormatter, BasicFormatting)
TEST(CSSFormatter, Minification)
TEST(CSSFormatter, PropertySorting)
TEST(CSSFormatter, SourceMaps)

// Integration tests
TEST(CSSIntegration, RoundtripParsing)
TEST(CSSIntegration, RealWorldCSS)
TEST(CSSIntegration, PerformanceBenchmarks)
```

### 5.2 Test Data and Fixtures
Create comprehensive test data:

**Test CSS Files:**
- `test/css/basic.css` - Simple CSS rules
- `test/css/complex.css` - Advanced CSS3 features
- `test/css/bootstrap.css` - Real-world framework
- `test/css/malformed.css` - Error cases
- `test/css/edge-cases.css` - Boundary conditions

**Property Test Data:**
- All properties from `css_grammar.txt`
- Valid and invalid value combinations
- Edge cases and boundary values
- Browser compatibility scenarios

### 5.3 Performance Benchmarking
Establish performance baselines:

**Benchmarks:**
- Tokenization speed (tokens/second)
- Parsing speed (rules/second)
- Memory usage (bytes/rule)
- Formatter speed (characters/second)

**Target Performance:**
- Sub-millisecond parsing for typical stylesheets (<1000 rules)
- Linear memory usage scaling
- Zero memory leaks
- Graceful degradation with malformed input

## Implementation Guidelines

### 6.1 Code Organization
```
lambda/input/
├── css_tokenizer.h          # Tokenizer interface
├── css_tokenizer.c          # Tokenizer implementation
├── css_parser.h             # Parser interface  
├── css_parser.c             # Parser implementation
├── css_properties.h         # Property definitions
├── css_properties.c         # Property database
├── css_values.h             # Value type definitions
├── css_values.c             # Value parsing/validation
├── css_ast.h                # AST node definitions
├── css_ast.c                # AST utilities
└── input-css.cpp            # Main CSS input interface

lambda/format/
├── css_formatter.h          # Formatter interface
├── css_formatter.c          # Formatter implementation
└── format-css.cpp           # Main CSS format interface

test/
├── test_css_tokenizer.cpp   # Tokenizer tests
├── test_css_parser.cpp      # Parser tests
├── test_css_properties.cpp  # Property tests
├── test_css_formatter.cpp   # Formatter tests
├── test_css_integration.cpp # Integration tests
└── test_css.cpp             # Main test runner
```

### 6.2 Dependencies and Integration
- **Memory Management**: Use existing `VariableMemPool` system
- **String Handling**: Use existing `StringBuf` and `String` types
- **Data Structures**: Use existing `Array` and `Element` types
- **Error Handling**: Integrate with Lambda's error reporting system
- **Build System**: Update `build_lambda_config.json` for new files

### 6.3 Backward Compatibility
- Maintain existing `parse_css()` and `format_css()` function signatures
- Preserve current AST structure where possible
- Ensure existing tests continue to pass
- Provide migration path for any breaking changes

## Current Progress Status

### ✅ Phase 1 & 2 Completed (December 2024)
**Tokenizer-Based Parser Implementation:**
- ✅ Complete CSS tokenizer with all token types
- ✅ Tokenizer-based parser architecture
- ✅ CSS property database with 50+ properties
- ✅ Property validation and value type checking
- ✅ AST generation for CSS stylesheets
- ✅ Memory pool integration
- ✅ Comprehensive test suite (tokenizer, parser, integration)
- ✅ Real-world CSS file parsing (simple.css, stylesheet.css)
- ✅ CSS function parsing (rgba, linear-gradient, etc.)
- ✅ Robust error handling and crash prevention

**Key Achievements:**
- Zero segmentation faults in CSS function parsing
- Stable parsing of complex CSS constructs
- Property database includes box-shadow, flexbox, border properties
- Test coverage includes GTest and Criterion frameworks
- Clean test directory organization

### 🔄 Next Steps (Phase 3)
**Remaining Work for Full CSS3+ Support:**
- Advanced selector parsing (pseudo-classes, combinators)
- Complete at-rule implementation (@media, @keyframes, etc.)
- CSS3+ functions (calc, min, max, clamp)
- Enhanced formatter with configurable options
- Performance optimization and benchmarking

## Success Metrics

### 7.1 Functional Requirements
- ✅ Core CSS parsing and tokenization
- ✅ Property validation system
- ✅ CSS function parsing (rgba, linear-gradient)
- 🔄 Complete CSS3+ specification compliance
- 🔄 Advanced selector syntax support
- 🔄 Comprehensive at-rule support

### 7.2 Performance Requirements
- ✅ Stable parsing without crashes
- ✅ Memory pool integration
- ✅ Graceful error recovery
- 🔄 Sub-millisecond parsing benchmarks
- 🔄 Linear memory usage validation

### 7.3 Quality Requirements
- ✅ Comprehensive test coverage for core functionality
- ✅ All critical tests passing
- ✅ Clean, maintainable code architecture
- ✅ Organized test directory structure
- 🔄 Performance benchmarks and optimization

## Risk Mitigation

### 8.1 Technical Risks
- **Complexity**: Break implementation into small, testable phases
- **Performance**: Establish benchmarks early and monitor continuously
- **Memory**: Use existing pool allocator and test thoroughly
- **Compatibility**: Maintain backward compatibility throughout

### 8.2 Schedule Risks
- **Scope Creep**: Stick to defined phases and requirements
- **Testing**: Write tests alongside implementation, not after
- **Integration**: Test integration points early and often
- **Documentation**: Document as you go, not at the end

## Current Status Summary

**✅ Phases 1-2 Complete:** The CSS parser has been successfully transformed from a character-based parser to a robust tokenizer-based system with comprehensive property validation. Critical stability issues have been resolved, and the parser now handles real-world CSS files without crashes.

**🔄 Phase 3 In Progress:** Advanced CSS3+ features, enhanced formatting, and performance optimization remain to be implemented.

**Key Accomplishments:**
- Stable, crash-free CSS parsing
- Property database with validation
- CSS function support (rgba, gradients)
- Comprehensive test coverage
- Clean, maintainable architecture

The foundation is solid for completing full CSS3+ specification compliance in the remaining phases.
