# Lambda

A powerful scripting language and document processing engine with JIT compilation capabilities.

## Overview

Lambda is a modern scripting language that combines:
- **Document Processing**: Native support for parsing and transforming various document formats (JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, reStructuredText, INI)
- **JIT Compilation**: Built on MIR (Medium Internal Representation) for high-performance execution
- **Memory Pool Management**: Advanced memory management with custom allocators
- **Interactive REPL**: Full-featured Read-Eval-Print Loop for interactive development
- **Tree-sitter Integration**: Robust syntax parsing and analysis

## Features

### Document Format Support
- **Input Formats**: JSON, XML, HTML, Markdown, PDF, CSV, YAML, TOML, LaTeX, RTF, reStructuredText, INI
- **Output Formats**: JSON, Markdown, and more
- **Native Parsing**: Built-in parsers for all supported formats with no external dependencies

### Language Features
- **Modern Syntax**: Clean, expressive syntax inspired by functional programming
- **Type System**: Strong typing with type inference
- **Memory Safe**: Advanced memory pool management prevents common memory issues
- **JIT Performance**: Near-native performance through MIR compilation
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

```bash
./compile-lambda.sh
```

### Running

**Interactive REPL:**
```bash
./lambda
```

**Run a script:**
```bash
./lambda script.ls
```

**REPL Commands:**
- `:help` - Show help
- `:quit` - Exit REPL
- `:clear` - Clear history

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
   ./compile-lambda.sh
   ```

## Language Examples

### Document Processing
```lambda
// Parse JSON and convert to Markdown
let data = parse_json("data.json")
let markdown = format_md(data)
write_file("output.md", markdown)

// Process CSV data
let csv = parse_csv("data.csv")
let filtered = filter(csv, row => row.age > 25)
write_json("filtered.json", filtered)
```

### Data Transformation
```lambda
// Transform nested data structures
let config = parse_yaml("config.yaml")
let processed = map(config.servers, server => {
    host: server.hostname,
    port: server.port ?? 8080,
    secure: server.ssl == true
})
```

### Interactive Analysis
```lambda
λ> let data = parse_json("sample.json")
λ> data.users.length
42
λ> filter(data.users, u => u.active).map(u => u.name)
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

## Testing

Run the comprehensive test suite:

```bash
cd test
./test_lib.sh
```

**Test Coverage:**
- 63 tests total, all passing ✅
- Memory pool stress testing
- Document format parsing
- String manipulation
- Error handling
- Performance benchmarks

## Dependencies

### Core Dependencies
- **MIR**: JIT compilation infrastructure
- **Tree-sitter**: Syntax parsing
- **Lexbor**: HTML/XML processing
- **zlog**: Logging (optional)
- **GMP**: Arbitrary precision arithmetic

### Development Dependencies
- **Criterion**: Testing framework
- **CMake**: Build system
- **pkg-config**: Dependency management

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| macOS    | ✅ Full | Native development platform |
| Linux    | ✅ Full | Ubuntu 20.04+ tested |
| Windows  | ✅ Cross-compilation | MinGW-w64 toolchain |

## Documentation

- [Compilation Guide](COMPILATION.md) - Detailed build instructions
- [Language Reference](docs/language.md) - Complete language specification
- [API Documentation](docs/api.md) - Built-in function reference
- [Examples](examples/) - Sample scripts and use cases

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature-name`
3. Make your changes and add tests
4. Run the test suite: `cd test && ./test_lib.sh`
5. Commit your changes: `git commit -am 'Add feature'`
6. Push to the branch: `git push origin feature-name`
7. Submit a pull request

### Development Guidelines

- Follow C99/C17 standards
- Add tests for new features
- Update documentation
- Ensure cross-platform compatibility
- Use memory pools for allocations

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **MIR Project**: JIT compilation infrastructure
- **Tree-sitter**: Incremental parsing framework
- **Lexbor**: Fast HTML/XML parsing library
- **Criterion**: C testing framework
- **Contributors**: Thanks to all who have contributed to this project

## Support

- **Issues**: [GitHub Issues](https://github.com/henry-luo/lambda/issues)
- **Discussions**: [GitHub Discussions](https://github.com/henry-luo/lambda/discussions)
- **Wiki**: [Project Wiki](https://github.com/henry-luo/lambda/wiki)

---

**Lambda** - *Powerful document processing and scripting language with JIT compilation*
