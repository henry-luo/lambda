# Lambda Script

A general-purpose, cross-platform. pure functional scripting language for data processing and presentation.

The script engine is built from scratch in C, with JIT compilation, and reference counting memory mgt.

*(Note: this project and the Lambda Script is still in its early stage of development. It's syntax, semantics and implementation may change substantially.  However, a stable subset of it, the literal data format, is separately formalised and released under [Mark Notation](https://github.com/henry-luo/mark).)*

## Overview

Lambda is a modern scripting language that combines:
- **Document Processing**: Native support for parsing and transforming various document formats (JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, reStructuredText, INI)
- **JIT Compilation**: Built on MIR (Medium Internal Representation) for high-performance execution
- **Memory Pool Management**: memory management with reference counting and custom pooled allocators.
- **Interactive REPL**: Read-Eval-Print Loop for interactive development
- **Tree-sitter Integration**: Fast syntax parsing and language prototyping based on Tree-sitter parser generator.

## Features

### Document Format Support
- **Input Formats**: JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, RST, INI
- **Output Formats**: JSON, Markdown, and more
- **Native Parsing**: Built-in parsers for all supported formats with no external dependencies

### Language Features
- **Modern Syntax**: Clean, expressive syntax inspired by functional programming
- **Type System**: Strong typing with type inference
- **Memory Safe**: Advanced memory pool management prevents common memory issues
- **JIT Performance**: Near-native performance through MIR JIT compilation
- **Interactive Development**: Full REPL with history and auto-completion

### Technical Architecture
- **Tree-sitter Parser**: Robust syntax analysis and error recovery
- **Custom Memory Pools**: Variable-size memory pool with advanced allocation strategies
- **Cross-platform**: Supports macOS, Linux, and Windows
- **Comprehensive Testing**: Full test suite with Criterion framework

## Quick Start

### Prerequisites

**macOS:**
```bash
./setup-mac-deps.sh
```

**Linux (Ubuntu):**
```bash
./setup-linux-deps.sh
```

**Windows:**
```bash
./setup-windows-deps.sh
```

### Building

**Lambda project (default):**
```bash
./compile.sh
```

**Radiant sub-project:**
```bash
./compile.sh build_radiant_config.json
```

**Cross-compilation for Windows:**
```bash
# Lambda for Windows
./compile.sh --platform=windows
```

**Help and options:**
```bash
./compile.sh --help
```

### Running

**Show help (default):**
```bash
./lambda
```

**Interactive REPL:**
```bash
./lambda --repl
```

**Interactive REPL with MIR JIT:**
```bash
./lambda --repl --mir
```

**Run a script:**
```bash
./lambda script.ls
```

**Run a script with MIR JIT:**
```bash
./lambda --mir script.ls
```

**REPL Commands:**
- `.help` - Show help
- `.quit` - Exit REPL
- `.clear` - Clear history

## Installation

### From Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/henry-luo/lambda.git
   cd lambda
   ```

2. **Install dependencies:**
   ```bash
   # macOS
   ./setup-mac-deps.sh
   
   # Linux
   ./setup-linux-deps.sh
   
   # Windows
   ./setup-windows-deps.sh
   ```

3. **Build:**
   ```bash
   # Build lambda project
   ./compile.sh
   
   # Cross-compile for Windows
   ./compile.sh --platform=windows
   ```

## Language Examples

### Document Processing
```lambda
// Parse JSON and convert to Markdown
let data = input("data.json", 'json')
format(data, 'markdown')

// Process CSV data
let csv = input("data.csv", 'csv')
for (row in csv) {
  if (row.age > 25) row
}
```

### Interactive Analysis
```lambda
λ> let data = input("sample.json", 'json')
λ> data.users.length
42
λ> for (u in data.users) { if (u.active) u.name }
["Alice", "Bob", "Charlie"]
```

## Architecture

### Core Components

- **Parser**: Tree-sitter based language parser with error recovery
- **Runtime**: MIR-based JIT compilation engine
- **Memory Management**: Custom variable-size memory pools
- **Document Processors**: Native parsers for 13+ document formats
- **Standard Library**: Rich set of built-in functions for data processing

### Memory Management

Lambda uses advanced memory pool management:
- **Variable Memory Pools**: Efficient allocation with best-fit algorithms
- **Reference Counting**: Automatic memory cleanup
- **Pool Coalescing**: Prevents memory fragmentation
- **Safety Checks**: Protection against memory corruption

### Performance

- **JIT Compilation**: Near-native performance through MIR
- **Memory Pools**: Reduced allocation overhead
- **Optimized Parsing**: Fast document processing
- **Lazy Evaluation**: Efficient handling of large datasets

## Build System

Lambda uses a unified build system that supports multiple projects and platforms:

### Supported Projects
- **Lambda**: Main scripting language and document processing engine
- **Radiant**: GUI framework and rendering engine

### Supported Platforms
- **Native**: macOS, Linux compilation using system tools
- **Cross-compilation**: Windows binaries using MinGW-w64

### Configuration
- **JSON-based**: Flexible configuration files for each project
- **Platform-specific**: Override settings for different target platforms
- **Modular**: Separate library and dependency management

## Testing

Run the comprehensive test suite:

```bash
cd test
./test_lib.sh
```

Test coverage is still very limited at the moment.

## Dependencies

### Core Dependencies
- **MIR**: JIT compilation infrastructure
- **Tree-sitter**: Syntax parsing
- **Lexbor**: HTML/XML processing
- **zlog**: Logging (optional)
- **GMP**: Arbitrary precision arithmetic

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS    | ✅ Full | Native development platform |
| Linux    | ✅ Full | Ubuntu 20.04+ tested |
| Windows  | ✅ Cross-compilation | MinGW-w64 toolchain |

## Documentation

- [Compilation Guide](COMPILATION.md) - Detailed build instructions
- [Language Reference](docs/language.md) - Todo
- [Examples](examples/) - Todo

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **MIR Project**: JIT compilation infrastructure
- **Tree-sitter**: Incremental parsing framework
- **Lexbor**: Fast HTML/XML parsing library
- **Criterion**: C testing framework

## Support

- **Issues**: [GitHub Issues](https://github.com/henry-luo/lambda/issues)


