# GitHub Copilot Instructions for Lambda Script

## Project Overview

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation. Built from scratch in C/C++ with JIT compilation using MIR (Medium Internal Representation) and reference counting memory management.

### Key Characteristics
- **Language Type**: Pure functional scripting language with modern syntax
- **Implementation**: Custom C/C++ runtime with Tree-sitter based parsing
- **Compilation**: JIT compilation via MIR for near-native performance
- **Memory Management**: Reference counting with three-tier string allocation (NamePool, arena, heap)
- **Target Use Cases**: Data processing, document transformation, mathematical computation, CSS layout
- **Platforms**: macOS, Linux, Windows (MINGW64 preferred for avoiding Universal CRT)

## Architecture & Core Data Model

### Runtime Data Representation (`lambda/lambda-data.hpp`)
Lambda uses **tagged pointers/values** in 64-bit `Item` type:
- **Simple scalars** (null, bool, int): packed directly with TypeId in high bits
- **Compound scalars** (int64, float, datetime, decimal, symbol, string, binary): tagged pointers
  - Numerics stored in `num_stack` at runtime
  - Strings/symbols/decimals/datetimes are heap-allocated with reference counting
- **Container types** (list, array, map, element, range): direct pointers extending `struct Container`
  - All start with `TypeId` field for runtime type identification
  - Heap-allocated with reference counting
- **Maps**: Packed structs with linked list of `ShapeEntry` defining fields
- **Elements**: Extend lists and act as maps simultaneously (dual nature)

Access type with `get_type_id(Item)` - handles all variants uniformly.

### Memory Management Strategy (`lambda/Lamdba_Runtime.md`, `lambda/lambda-mem.cpp`)

**Three-tier string allocation** - choose the right function:

1. **Names (structural identifiers)** - `heap_create_name()` or `builder.createName()`
   - **Always pooled** in NamePool (string interning)
   - Use for: map keys, element tags, attribute names, function names, variable names
   - Benefit: same string → same pointer (identity comparison, memory sharing)
   - Supports parent-child hierarchy for schema inheritance

2. **Symbols (short identifiers)** - `heap_create_symbol()`
   - **Conditionally pooled** (only if ≤32 chars)
   - Use for: symbol literals (`'mySymbol`), enum-like values
   - Long symbols fall back to arena allocation

3. **Strings (content data)** - `heap_strcpy()` or `builder.createString()`
   - **Never pooled** (arena allocated)
   - Use for: user content, text data, free-form strings
   - Fastest allocation, no hash lookup overhead

**Rule of thumb**: Structural names → `createName()`, content data → `createString()`

### Core System Architecture
- **Parser**: Tree-sitter grammar (`lambda/tree-sitter-lambda/grammar.js` → auto-generates `parser.c`)
- **AST Builder**: `lambda/build_ast.cpp` - constructs typed AST from Tree-sitter CST
- **Transpiler**: `lambda/transpile.cpp` (C code) + `lambda/transpile-mir.cpp` (MIR JIT)
- **Runtime**: `lambda/lambda-eval.cpp` - interpreter execution + `lambda/mir.c` - JIT compilation
- **Type System**: `lambda/lambda-data.hpp` - 20+ built-in types with inference

### Document Processing Pipeline
- **Input**: `lambda/input/` - 15+ parsers (JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, etc.)
  - Each parser uses `MarkBuilder` (`lambda/mark_builder.hpp`) to construct Lambda data structures
  - Common pattern: parse → build Items → set Input root
- **Output**: `lambda/format/` - multiple formatters (JSON, Markdown, HTML, YAML, etc.)
- **Validation**: `lambda/validator/` - schema-based type validation with error reporting
- **CSS Engine**: `lambda/input/css/` - complete CSS parser, cascade resolver
- **Radiant Engine**: `radiant/` - HTML/CSS/SVG layout and rendering with FreeType, GLFW, FontConfig

## Build System & Development Workflow

### Build Commands
```bash
make build              # Incremental build (Premake5-based, fastest)
make debug              # Debug build with AddressSanitizer
make release            # Optimized release build (-O3 + LTO)
make rebuild            # Force complete rebuild
make clean              # Clean build artifacts
```

**Build architecture**:
- JSON config (`build_lambda_config.json`) → Python generator (`utils/generate_premake.py`) → Premake5 Lua → Makefiles
- Supports parallel compilation (auto-detects cores, max 8 jobs)
- Auto-regenerates parser when `grammar.js` changes (dependency tracking via Make)

### Testing Strategy
```bash
make build-test         # Build all test executables
make test               # Run ALL tests (baseline + extended)
make test-baseline      # Core functionality only (must pass 100%)
make test-extended      # HTTP/HTTPS, ongoing features
make test-library       # Library-specific tests
make test-validator     # Schema validation tests
```

**Test organization**:
- `test/*.exe` - GTest C++ unit tests (e.g., `test_lambda_gtest.exe`)
- `test/input/` - test data
- `test/lambda/` - Lambda script integration tests
- `test/layout/` - CSS layout engine tests (browser reference comparison)

**Running single test**: `./test/test_lambda_gtest.exe --gtest_filter=TestSuite.TestCase`

### Grammar & Parser Development
```bash
make generate-grammar   # Regenerate parser from grammar.js
make clean-grammar      # Remove generated files (parser.c, ts-enum.h)
```

**Auto-regeneration**: Makefile tracks `grammar.js` → `parser.c` → `ts-enum.h` → source files. Parser regenerates automatically on grammar changes.

**Tree-sitter integration**:
- Grammar: `lambda/tree-sitter-lambda/grammar.js`
- Generated parser: `lambda/tree-sitter-lambda/src/parser.c`
- Enum mapping: `lambda/ts-enum.h` (auto-generated via `utils/update_ts_enum.sh`)

## Coding Conventions & Best Practices

### Memory Safety Rules
1. **Always use pool allocation** for Lambda objects: `pool_calloc()`, `arena_alloc()`
2. **Reference counting**: Increment refs when storing, decrement when releasing
3. **Validate pointers** before dereferencing: `if (!ptr) return ItemNull;`
4. **Range check arrays**: Verify indices before access
5. **Use MarkBuilder** for constructing complex structures (handles memory correctly)

### Debugging & Logging
```cpp
// Use structured logging (lib/log.h) - outputs to ./log.txt
log_debug("Processing item: type=%d, value=%p", type_id, ptr);
log_info("Loaded document: %zu bytes", size);
log_error("Parse failed at line %d: %s", line, msg);

// NEVER use printf/fprintf/std::cout for debugging
// Configure logging levels in log.conf
```

### Code Style
- **Comments**: Start inline comments in lowercase: `// process the next token`
- **Naming**: `snake_case` for C and C++ methods, `PascalCase` for classes
- **Error handling**: Return `ItemNull` or `ItemError`, log errors with `log_error()`
- **Defensive coding**: Check all allocations, validate inputs, handle edge cases

### Common Patterns

**Creating a map**:
```cpp
MarkBuilder builder(input);
MapBuilder mb = builder.createMap();
mb.put("name", builder.createString("Alice"));
mb.put("age", builder.createInt(30));
Item map = mb.final();
```

**Parsing input**:
```cpp
Input* input = input_create(pool, arena);
parse_json(input, json_string);  // Sets input->root
Item doc = input->root;
```

**Working with Items**:
```cpp
TypeId type = get_type_id(item);
if (type == LMD_TYPE_STRING) {
    String* str = (String*)item.pointer;  // unpack from tagged pointer
    // Access string data...
} else if (type == LMD_TYPE_MAP) {
    Map* map = item.map;
    // Access map data...
}
```

## File Organization & Key Files

### Core Runtime
- `lambda/lambda.h` - Core data structures (C API for MIR)
- `lambda/lambda-data.hpp` - Full C++ API and type definitions
- `lambda/lambda-data.cpp` - Runtime data manipulation
- `lambda/lambda-mem.cpp` - Memory allocation (pools, arenas, name pooling)
- `lambda/lambda-eval.cpp` - Interpreter execution engine

### Parsing & Compilation
- `lambda/parse.c` - Tree-sitter integration wrapper
- `lambda/build_ast.cpp` - AST construction from CST
- `lambda/transpile.cpp` - AST → C code transpilation
- `lambda/transpile-mir.cpp` - AST → MIR JIT compilation
- `lambda/mir.c` - MIR runtime integration

### Data Construction
- `lambda/mark_builder.hpp/cpp` - High-level builder for Lambda data (use this!)
- `lambda/mark_reader.hpp/cpp` - Data traversal and querying
- `lambda/mark_editor.hpp/cpp` - In-place data modification
- `lambda/name_pool.hpp/cpp` - String interning and pooling
- `lambda/shape_pool.hpp/cpp` - Map shape caching

### Input Parsers (all in `lambda/input/`)
- `input.cpp` - Main input dispatcher
- `input-json.cpp`, `input-xml.cpp`, `input-html.cpp` - Common formats
- `input-css.cpp` - CSS parser (uses `lambda/input/css/` engine)
- `input-math.cpp` - Mathematical expression parser

### CSS & Layout (Radiant engine in `radiant/`)
- `lambda/input/css/css_parser.cpp` - CSS syntax parser
- `lambda/input/css/css_engine.cpp` - Cascade resolver
- `radiant/layout.cpp` - Layout engine coordinator
- `radiant/layout_flex.cpp` - Flexbox implementation
- `radiant/layout_block.cpp` - Block layout
- `test/layout/` - Layout test harness (browser reference comparison)

### Libraries (`lib/`)
- `mempool.c/h` - Variable-size memory pool
- `arena.c/h` - Arena allocator (linear allocation)
- `strbuf.c/h` - Dynamic string buffer
- `arraylist.c/h` - Dynamic array
- `hashmap.c/h` - Hash table
- `log.c/h` - Structured logging

### Testing (`test/`)
- `test_run.sh` - Main test runner (parallel execution)
- `test/*.cpp` - GTest unit tests
- `test/lambda/*.ls` - Lambda script integration tests
- `test/input/` - Test data files
- `test/layout/` - CSS layout tests and browser references

## Common Development Tasks

### Adding a New Input Format
1. Create parser in `lambda/input/input-{format}.cpp`
2. Implement `parse_{format}(Input* input, const char* content)`
3. Use `MarkBuilder` to construct Lambda data structures
4. Register format in `input.cpp` dispatcher
5. Add MIME type to `lib/mime-types.c`
6. Add tests in `test/test_input_roundtrip_gtest.cpp`

### Adding a New Built-in Function
1. Define signature in `lambda/lambda.h` (for MIR) and `lambda/lambda-data.hpp` (for C++)
2. Implement in `lambda/lambda-proc.cpp` or `lambda/lambda-eval.cpp`
3. Register in function table (`lambda/runner.cpp`)
4. Add tests in `test/test_lambda_proc_gtest.cpp`
5. Document in `doc/Lambda_Reference.md`

### Debugging a Crash
1. Build debug version: `make debug` (enables AddressSanitizer)
2. Enable debug logging in `log.conf`: set level to `DEBUG`
3. Run with debugger: `lldb ./lambda.exe` or `gdb ./lambda.exe`
4. Check `./log.txt` for detailed execution trace
5. Use `log_debug()` liberally to trace execution flow

### Investigating Memory Issues
1. Run with AddressSanitizer: `make debug && ./lambda.exe script.ls`
2. Check for leaks with valgrind (Linux): `valgrind --leak-check=full ./lambda.exe`
3. Review reference counting: ensure `heap_incref()`/`heap_decref()` are balanced
4. Verify pool usage: check `pool_calloc()` returns are checked

## Platform-Specific Notes

### Windows/MSYS2
- **Prefer MINGW64** over CLANG64 to avoid Universal CRT dependencies
- Use `make build-mingw64` to enforce MINGW64 environment
- Tree-sitter: uses system library (`pacman -S mingw-w64-x86_64-libtree-sitter`)
- Cross-compilation: `./setup-windows-deps.sh` installs MinGW toolchain

### macOS
- Uses Homebrew for dependencies: `./setup-mac-deps.sh`
- Clang is default compiler
- ccache enabled if available (faster rebuilds)

### Linux
- GCC or Clang supported
- Install dependencies: `./setup-linux-deps.sh`

## Integration Points & External Dependencies

### MIR (JIT Compiler)
- Location: `lambda/mir.c` (embedded in repo)
- API: See `include/mir.h`
- Lambda → MIR transpilation in `lambda/transpile-mir.cpp`
- Functions exposed to JIT code via function table

### Tree-sitter (Parser Generator)
- Grammar: `lambda/tree-sitter-lambda/grammar.js`
- Library: `lambda/tree-sitter/` (built from source or system package)
- Integration: `lambda/parse.c` wraps Tree-sitter API

### External Libraries
- **FreeType**: Font rasterization (Radiant engine)
- **FontConfig**: System font discovery (macOS/Linux)
- **GLFW**: Window management (Radiant engine)
- **ThorVG**: Vector graphics rendering (Radiant engine)
- **GTest**: Unit testing framework (dev dependency)

## Notes & Constraints
- C++17 standard (use modern features: `std::optional`, structured bindings, etc.)
- Grammar regeneration is automatic - don't manually edit `parser.c`
- Log file location: `./log.txt` (configure levels in `log.conf`)
- **Token limit**: 1,000,000 tokens per session. So don't worry about running short of tokens.
