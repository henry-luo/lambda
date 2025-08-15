# Lambda Script - Claude AI Context

## Project Overview

Lambda Script is a general-purpose, cross-platform functional scripting language designed for data processing and document presentation. Built from scratch in C with JIT compilation and reference counting memory management.

### Key Characteristics
- **Language Type**: Pure functional scripting language
- **Implementation**: Built from scratch in C
- **Compilation**: JIT compilation using MIR (Medium Internal Representation)
- **Memory Management**: Reference counting with custom pooled allocators
- **Parsing**: Tree-sitter based syntax parsing
- **Target Platforms**: macOS, Linux, Windows (cross-compilation)

## Core Features

### Document Processing Engine
- **Input Formats**: JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, RST, INI, vCard, Email (EML), Calendar (ICS), AsciiDoc, Man pages, Textile, Wiki markup
- **Output Formats**: JSON, Markdown, HTML, and more
- **Native Parsing**: Built-in parsers with no external dependencies
- **Schema Validation**: Advanced type system with schema-based validation

### Language Runtime
- **JIT Compilation**: Near-native performance through MIR JIT
- **Interactive REPL**: Full Read-Eval-Print Loop with history
- **Memory Safety**: Variable-size memory pools with reference counting
- **Type System**: Strong typing with type inference
- **Functional Programming**: Pure functional approach with immutable data structures

### Technical Architecture
- **Parser**: Tree-sitter grammar for robust syntax analysis
- **Runtime Engine**: MIR-based JIT compilation for high performance
- **Memory Pools**: Custom variable-size pools with coalescing
- **Document Processors**: Native parsers for 15+ document formats
- **Cross-platform Build**: Sophisticated build system supporting multiple targets

## Project Structure

### Core Components

#### `/lambda/` - Core Language Runtime
- **Core Files**:
  - `lambda.h` / `lambda.c` - Main runtime and type system
  - `main.cpp` - CLI entry point and argument processing
  - `runner.cpp` - Script execution and JIT compilation management
  - `transpiler.hpp` / `transpile.cpp` - AST to C code transpilation
  - `mir.c` - MIR JIT integration and function binding
  - `parse.c` - Tree-sitter parser integration

- **Memory Management**:
  - `lambda-mem.c` - Memory pool implementation
  - `hashmap.c` - Hash table data structures
  - `arraylist.c` / `buffer.c` - Dynamic collections

- **Data Structures**:
  - Runtime items (Item type) - 64-bit tagged pointers/values
  - Containers: List, Array, Map, Element, Range
  - Type system: TypeId enumeration with runtime type info
  - Memory pools with reference counting

#### `/lambda/input/` - Document Input Processing
- **Parser Modules**:
  - `input-{format}.c` - Format-specific parsers (html, json, xml, md, pdf, csv, etc.)
  - `input-common.c` - Common parsing utilities
  - `mime-detect.c` / `mime-types.c` - MIME type detection

- **Schema System**:
  - `{format}_schema.ls` - Lambda schema definitions
  - `schema_validator.ls` - Validation engine
  - `validator/` - C-based schema validation system

#### `/lambda/format/` - Output Formatting
- `format-{type}.c` - Output formatters (json, md, html, xml, yaml, etc.)
- `format.c` - Common formatting utilities

#### `/lambda/tree-sitter-lambda/` - Language Grammar
- Tree-sitter grammar definition for Lambda syntax
- Parser generation and integration
- Example scripts and test cases

#### `/lambda/typeset/` - Typesetting System
- **Core Engine**: Device-independent view tree system
- **Layout**: Advanced layout algorithms for documents
- **Renderers**: Multi-format output (SVG, HTML, PDF, PNG, TeX)
- **Integration**: Lambda element tree to view tree conversion

#### `/radiant/` - HTML/CSS Renderer
- Native HTML/CSS/SVG renderer built from scratch
- Lexbor-based HTML/CSS parsing
- Complete layout engine (block, inline, flexbox)
- Hardware-accelerated rendering via OpenGL/GLFW
- Font rendering with FreeType and FontConfig

### Supporting Infrastructure

#### `/build/` - Build System
- Advanced JSON-configured build system
- Cross-platform compilation support
- Dependency management with automatic detection
- Parallel builds with intelligent job limiting

#### `/windows-deps/` - Cross-compilation Support
- MinGW-w64 Windows cross-compilation setup
- Automated dependency building (MIR, lexbor, tree-sitter)
- Fallback systems for failed builds

#### `/wasm-deps/` - WebAssembly Support
- WASM compilation configuration
- JavaScript interface for Lambda WASM module
- Browser and Node.js runtime integration

#### `/test/` - Test Suite
- Lambda script test cases (`.ls` files)
- Unit tests using Criterion framework
- Integration tests for document processing
- Performance and memory leak testing

#### `/lib/` - Utility Libraries
- HTTP/HTTPS server implementation
- Common utilities and helper functions
- Platform abstraction layers

## Language Syntax

### Basic Syntax Examples

```lambda
// Data types
let data = {
    numbers: [1, 2, 3],
    strings: ["hello", "world"],
    boolean: true,
    null_value: null,
    nested: {key: "value"}
}

// Functions
fn process(items) {
    for (item in items) {
        if (item > 0) item * 2
        else 0
    }
}

// Document processing
let doc = input("document.json", 'json')
let result = format(process(doc.data), 'markdown')
```

### Advanced Features
- **Comprehensions**: `for (x in list, y in range) x + y`
- **Pattern Matching**: Destructuring and conditional expressions
- **Ranges**: `1 to 10` syntax for sequence generation
- **Symbols**: `'symbol` literal syntax
- **String Interpolation**: Multi-line strings with embedded expressions

## Build System

### Build Configuration
- **JSON-based Configuration**: Flexible build configurations in JSON
- **Multi-project Support**: Lambda, Radiant, and other sub-projects
- **Platform Targets**: Native (macOS/Linux) and cross-compilation (Windows)
- **Dependency Management**: Automatic detection and building

### Key Commands
```bash
# Setup dependencies
./setup-mac-deps.sh     # macOS
./setup-linux-deps.sh   # Linux  
./setup-windows-deps.sh # Windows cross-compilation

# Build projects
make                           # Default Lambda build
./compile.sh build_radiant_config.json # Radiant renderer
./compile.sh --platform=windows        # Windows cross-compilation

# Run Lambda
./lambda                             # interactive REPL
./lambda --mir script.ls             # JIT compiled execution
./lambda script.ls                   # script execution
./lambda --help                      # print help message
./lambda --transpile-only script.ls  # only transpile the script to C code
```

## Dependencies

### Core Runtime Dependencies
- **MIR**: JIT compilation infrastructure
- **Tree-sitter**: Incremental parsing framework
- **GMP**: Arbitrary precision arithmetic (minimal usage)
- **zlog**: Logging system (optional)

### Document Processing
- **Lexbor**: HTML/XML parsing (lexbor_static.a ~3.7MB)
- **Various parsers**: Native implementations for most formats

### Radiant Renderer Dependencies
- **FreeType**: Font rasterization and text metrics
- **FontConfig**: System font discovery
- **GLFW**: Window management and OpenGL context
- **ThorVG**: SVG rendering
- **STB**: Image loading

## Memory Architecture

### Memory Management Strategy
- **Variable Memory Pools**: Efficient allocation with best-fit algorithms
- **Reference Counting**: Automatic memory cleanup for runtime objects
- **Pool Coalescing**: Prevention of memory fragmentation
- **Safety Checks**: Protection against memory corruption

### Data Representation
- **Items**: 64-bit values combining data and type information
- **Tagged Pointers**: High bits store type IDs
- **Container Types**: Direct pointers to heap-allocated structures
- **Scalar Packing**: Simple types packed directly into Item values

## Performance Characteristics

### JIT Compilation
- **MIR Backend**: Professional-grade JIT compilation
- **Native Performance**: Near-C performance for computational code
- **Incremental Compilation**: Fast startup for interactive use

### Memory Performance
- **Pool Allocation**: Reduced malloc/free overhead
- **Reference Counting**: Predictable memory cleanup
- **Cache-friendly**: Packed data structures

### Document Processing
- **Streaming Parsers**: Memory-efficient for large documents
- **Parallel Processing**: Multi-threaded where applicable
- **Optimized Algorithms**: Efficient parsing and transformation

## Development Status

### Completed Components ✅
- Core runtime with JIT compilation
- Tree-sitter parser integration  
- Memory pool management system
- Document input processing (15+ formats)
- Output formatting system
- Schema validation framework
- Cross-platform build system
- WASM compilation support
- Basic typesetting system
- Radiant HTML/CSS renderer

### In Development 🔄
- Advanced schema validation features
- Complete typesetting layout algorithms
- Performance optimizations
- Extended standard library
- IDE integration (VS Code extension)

### Planned Features 📋
- Package management system
- Advanced debugging tools
- More output formats
- Cloud deployment options
- Educational tooling

## Related Projects

- **Mark Notation**: Stable subset of Lambda for data interchange - https://github.com/henry-luo/mark
- **Tree-sitter Lambda**: Grammar definition for Lambda syntax

## Usage Patterns

### Data Processing Pipeline
```lambda
// Load, transform, and output data
let source = input("data.csv", 'csv')
let filtered = for (row in source) { if (row.active) row }
let summary = aggregate(filtered, 'category')
format(summary, 'json')
```

### Document Conversion
```lambda
// Convert between document formats
let doc = input("article.md", 'markdown')
let enriched = enhance_metadata(doc)
format(enriched, 'html')
```

### Schema Validation
```lambda
// Validate document structure
let schema = input("schema.ls", 'lambda')
let document = input("data.json", 'json')
validate(document, schema)
```

This context provides Claude AI with comprehensive understanding of the Lambda Script project's architecture, capabilities, and current implementation status for effective assistance with development tasks.
