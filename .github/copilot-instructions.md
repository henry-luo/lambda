# GitHub Copilot Instructions for Lambda Script

## Project Overview

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation, built from scratch in C/C++ with JIT compilation.

### Key Characteristics
- **Language Type**: Pure functional scripting language with modern syntax
- **Implementation**: Custom C/C++ runtime with Tree-sitter based parsing
- **Compilation**: JIT compilation via MIR for near-native performance
- **Memory Management**: Reference counting with three-tier string allocation (namepool, arena, heap)
- **Target Use Cases**: Data processing, document transformation, mathematical computation, CSS layout and rendering
- **Input Formats**: JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, CSV, TOML, etc.
- **Output Formats**: JSON, HTML, Markdown, YAML, PDF, SVG, PNG, etc.
- **Platforms**: macOS, Linux, Windows

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

### Radiant Layout Engine (`radiant/`)

Radiant is the CSS layout and rendering engine for HTML/CSS document presentation.

**Architecture:**
- **Unified DOM/View Tree**: `DomNode` → `DomText`/`DomElement` serve as both DOM and layout views
- **Relative Coordinates**: All positions relative to parent's border box
- **Layout Context**: `LayoutContext` struct coordinates all layout state

**Layout Sub-flows** (dispatched in `layout_block.cpp`):
- **Block/Inline**: `layout_block_content()`, `layout_inline()` - standard CSS flow
- **Flexbox**: `layout_flex_content()` in `layout_flex_multipass.cpp` - 9-phase algorithm
- **Grid**: `layout_grid_content()` in `layout_grid_multipass.cpp` - track sizing + item placement
- **Table**: `layout_table_content()` in `layout_table.cpp` - auto/fixed layout

**Key Files:**
| File | Purpose |
|------|---------|
| `view.hpp` | View hierarchy: `ViewBlock`, `ViewSpan`, property structs |
| `layout.hpp` | Core structs: `LayoutContext`, `BlockContext`, `Linebox` |
| `layout_block.cpp` | Block layout, mode dispatch |
| `layout_inline.cpp` | Inline/text layout, line breaking |
| `layout_flex.cpp` | Flexbox algorithm |
| `layout_grid.cpp` | Grid track sizing |
| `layout_table.cpp` | Table layout |
| `resolve_css_style.cpp` | CSS cascade and property resolution |

**Property Structures** (allocated via `alloc_*_prop()` from pool):
- `BlockProp`, `BoundaryProp` (box model), `InlineProp`, `FontProp`
- `FlexProp`/`FlexItemProp`, `GridProp`/`GridItemProp`, `TableProp`

## Lambda CLI Commands

### Basic Usage
```bash
./lambda.exe                          # Start interactive REPL (default)
./lambda.exe script.ls                # Run a functional Lambda script (JIT with C2MIR)
./lambda.exe --help                   # Show help message for Lambda CLI
./lambda.exe run script.ls            # Run a procedural Lambda script with main() procedure
```

### Document Validation
```bash
./lambda.exe validate data.json -s schema.ls     # Validate against custom schema
./lambda.exe validate data.json                  # Validate with doc_schema.ls (default)
```

### Format Conversion
```bash
./lambda.exe convert input.json -t yaml -o output.yaml      # Auto-detect input format
./lambda.exe convert input.md -f markdown -t html -o out.html  # Explicit formats
```

### Layout Analysis & Rendering
```bash
make layout suite=baseline                       # Run layout regression tests
make layout test=file_name                       # Compare specific html file layout against browser
./lambda.exe layout page.html                    # Runs CSS layout, and output view tree
./lambda.exe render page.html -o output.svg      # Render to SVG
./lambda.exe render page.html -o output.pdf      # Render to PDF
./lambda.exe render page.html -o output.png      # Render to PNG/JPEG
./lambda.exe view                                # Open default (test/html/index.html)
./lambda.exe view page.html                      # Open HTML in browser window
```

## Build System & Development Workflow

### Build Commands
```bash
make build              # Incremental build (Premake5-based, fastest)
make clean-all          # Clean all build artifacts
```

**Build architecture**:
- JSON config (`build_lambda_config.json`) → Python generator (`utils/generate_premake.py`) → Premake5 Lua → Makefiles
- Auto-regenerates parser when `grammar.js` changes (dependency tracking via Make)

### Testing Strategy
```bash
make build-test               # Build all test executables
make test                     # Run ALL tests (baseline + extended)
make test-lambda-baseline     # Lambda core functionalities (must pass 100%, when changes maked to Lambda engine)
make test-radiant-baseline     # Radiant core functionalities (must pass 100%, when changes maked to Radiant engine)
```

**Running single test**: e.g. `./test/test_some_unit_test.exe --gtest_filter=TestSuite.TestCase`

### Grammar & Parser Development
```bash
make generate-grammar   # Regenerate parser from grammar.js
make clean-grammar      # Remove generated files (parser.c, ts-enum.h)
```

**Tree-sitter integration**:
- Grammar: `lambda/tree-sitter-lambda/grammar.js`
- Enum mapping: `lambda/ts-enum.h` (auto-generated via `utils/update_ts_enum.sh`)

## Coding Conventions & Best Practices

### Memory Safety Rules
1. **Always use pool allocation** for Lambda objects: `pool_calloc()`, `arena_alloc()`
2. **Use MarkBuilder and MarkReader** for constructing complex Lambda/Mark structures and reading them (handles memory correctly)

### Debugging & Logging
```cpp
// Use structured logging (lib/log.h) - outputs to ./log.txt
log_debug("Processing item: type=%d, value=%p", type_id, ptr);
log_info("Loaded document: %zu bytes", size);
log_error("Parse failed at line %d: %s", line, msg);
```
NEVER use printf/fprintf/std::cout for debugging

### Code Style
- **Comments**: Start inline comments in lowercase: `// process the next token`
- **Naming**: `snake_case` for C and C++ methods, `PascalCase` for classes
- **Error handling**: Return `ItemNull` or `ItemError`, log errors with `log_error()`

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

### Libraries (`lib/`)
- `mempool.c/h` - Variable-size memory pool
- `arena.c/h` - Arena allocator (linear allocation)
- `strbuf.c/h` - Dynamic string buffer
- `arraylist.c/h` - Dynamic array
- `hashmap.c/h` - Hash table
- `log.c/h` - Structured logging

### Testing (`test/`)
- `test_run.sh` - Main test runner (parallel execution)
- `test/*.exe` - GTest C++ unit tests (e.g., `test_lambda_gtest.exe`)
- `test/*.cpp` - GTest unit tests
- `test/lambda/*.ls` - Lambda script integration tests
- `test/input/` - Test data files
- `test/layout/` - CSS layout tests and browser references

### External Libraries
- **FreeType**: Font rasterization (Radiant engine)
- **GLFW**: Window management (Radiant engine)
- **ThorVG**: Vector graphics rendering (Radiant engine)
- **GTest**: Unit testing framework (dev dependency)

### MIR (JIT Compiler)
- Location: `lambda/mir.c` (embedded in repo)
- API: See `include/mir.h`
- Lambda → MIR transpilation in `lambda/transpile-mir.cpp`
- Functions exposed to JIT code via function table

### Debugging a Crash
1. Use `log_debug()` liberally to trace execution flow
2. Check `./log.txt` for detailed execution trace
3. Run with debugger: `lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- extra CLI arguments`

## Notes & Constraints
- C++17 standard.
- Don't use std::string or std::* containers, like std::vector or std::map. Use ./lib equivalents.
- Grammar regeneration is automatic - don't manually edit `parser.c`
- Log file location: `./log.txt` (configure levels in `log.conf`). Don't change log config. Start each log line with a distinct prefix/phrase for easy searching.
- Lambda language documentation:
- **Lambda Reference Docs**:
  - `doc/Lambda_Reference.md` — Language overview and quick reference
  - `doc/Lambda_Data.md` — Literals and collections (primitives, arrays, lists, maps, elements, ranges)
  - `doc/Lambda_Type.md` — Type system (union types, function types, type patterns)
  - `doc/Lambda_Expr_Stam.md` — Expressions and statements (operators, pipes, control flow)
  - `doc/Lambda_Func.md` — Functions (`fn`, `pn`, closures, higher-order functions)
  - `doc/Lambda_Error_Handling.md` — Error handling (`raise`, `T^E` return types, `?` propagation, `let a^err` destructuring)
  - `doc/Lambda_Sys_Func.md` — System functions (type, math, string, collection, I/O, date/time)
  - `doc/Lambda_Validator_Guide.md` — Schema-based data validation
  - `doc/Lambda_Cheatsheet.md` — Quick syntax cheatsheet
  - `doc/Radiant_Layout_Design.md` — Radiant CSS layout engine design
- After adding a new Lambda unit test script *.ls, don't forget to add the correspoding expected result file *.txt.
- For any temporal files, create them under `./temp` directory, instead of `/tmp` dir.
- **Token limit**: 10,000,000 tokens per session. So don't worry about running short of tokens.
