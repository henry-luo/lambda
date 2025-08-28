# GitHub Copilot Instructions for Lambda Script

## Project Overview

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation. Built from scratch in C with JIT compilation using MIR (Medium Internal Representation) and reference counting memory management.

### Key Characteristics
- **Language Type**: Pure functional scripting language with modern syntax
- **Implementation**: Custom C runtime with Tree-sitter based parsing
- **Compilation**: JIT compilation via MIR for near-native performance
- **Memory Management**: Advanced variable-size memory pools with reference counting
- **Target Use Cases**: Data processing, document transformation, mathematical computation
- **Platforms**: macOS, Linux, Windows (cross-compilation support)

## Architecture & Components

### Core System
- **Parser**: Tree-sitter based language parser (`lambda/tree-sitter-lambda/`)
- **Runtime**: MIR-based JIT transpiler (`lambda/transpiler.hpp`, `lambda/lambda-eval.cpp`)
- **Memory**: Variable memory pool system (`lib/mem-pool/`)
- **Type System**: Strong typing with inference (`lambda/lambda-data.hpp`)

### Document Processing
- **Input Parsers**: 13+ formats (JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, etc.) in `lambda/input/`
- **Output Formatters**: Multiple output formats in `lambda/format/`
- **Math Parser**: Advanced mathematical expression parser supporting LaTeX, Typst, ASCII syntax

### Validation & Analysis
- **Schema Validator**: Type-safe document validation (`lambda/validator/`)
- **Unicode Support**: Comprehensive Unicode handling with ICU integration (`lambda/unicode_string.cpp`)

## Language Syntax & Features

### Basic Syntax
```lambda
// Data types and literals
let numbers = [1, 2, 3]
let strings = ["hello", "world"]
let data = {key: "value", count: 42}
let symbols = 'symbol
let dates = t'2025-01-01'
let binary = b'\xDEADBEEF'

// Functions and expressions
fn process(items: [int]) {
    for (item in items) item * 2
}

// Conditional expressions
let result = if (x > 0) "positive" else "non-positive"

// Document processing
let doc = input("file.json", 'json')
let output = format(transform(doc), 'markdown')
```

### Advanced Features
- **Type System**: `type Person < name: string, age: int >`
- **Pattern Matching**: Destructuring in let expressions and function parameters
- **Comprehensions**: `for (x in list, y in range) x + y`
- **Ranges**: `1 to 10` syntax for sequence generation
- **Elements**: Document structure `<element attr: value; content>`

## Development Guidelines

### Code Style & Safety
- **Memory Safety**: Always use proper pool allocation and reference counting
- **Type Safety**: Leverage type system macros for field access
- **Defensive Programming**: Validate pointers and range check arrays
- **Error Handling**: Graceful fallbacks and proper error propagation
- **Comment**: Start inline-level comments in lowercase

### Build System
- **Primary**: Use `make build` for incremental builds
- **Cross-platform**: `make build-windows` for Windows cross-compilation
- **Configuration**: JSON-based build configs (`build_lambda_config.json`)
- **Testing**: Comprehensive test suite with `make test`

## Unicode & Internationalization

The system has configurable Unicode support levels:
- **Level 0**: ASCII-only (minimal overhead)
- **Level 1**: Compact ICU integration (default, ~2-4MB)

When working with Unicode:
```c
// Use Unicode-aware comparison functions
CompResult result = equal_comp_unicode(a_item, b_item);
UnicodeCompareResult cmp = string_compare_unicode(str1, len1, str2, len2);
```

### Running Tests
```bash
make build-test         # Build all the test executables
make test               # Run all tests
```

## Common Patterns

### Input Processing
```c
// Parse document with format detection
Item parsed = input_auto_detect(pool, filename);
if (parsed.type_id == LMD_TYPE_ERROR) {
    // Handle parsing error
}
```

### Output Generation
```c
// Generate formatted output
String* json_output = format_json(pool, root_item);
String* md_output = format_markdown(pool, root_item);
```

### Type Validation
```c
// Schema-based validation
ValidationResult result = validate_document(doc, schema);
if (result.is_valid) {
    // Document conforms to schema
}
```

## File Organization

- `lambda/` - Core language runtime and parsers
- `lib/` - Shared libraries (memory pools, string buffers)
- `test/` - Test suites and sample files
- `test/input` - Test suites input data files
- `test/lambda` - Tests in Lambda script
- `test/*.c` - Tests in Criterion C/C++ code
- `typeset/` - Document typesetting system
- `build/` - Build artifacts (auto-generated)
- `*.json` - Build configuration files
- `temp/` - Temporary files (auto-generated)

## Key Considerations



