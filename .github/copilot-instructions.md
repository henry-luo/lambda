# GitHub Copilot Instructions for Lambda Script

## CRITICAL Rules for AI Agents

These rules MUST be followed. Violations are considered errors.

1. **NEVER write files to `/tmp`**. Use `./temp/` for ALL temporary files.
2. **NEVER use `std::string`, `std::vector`, `std::map`**, or any other `std::` types. Use `./lib` equivalents (`Str`, `ArrayList`, `HashMap`, etc.).
3. **NEVER use `printf`/`fprintf`/`std::cout`** for debugging. Use `log_debug()`, `log_info()`, `log_error()` from `lib/log.h`.
4. **NEVER manually edit `parser.c`**. Grammar regeneration is automatic from `grammar.js` via `make generate-grammar`.
5. **NEVER modify `log.conf`**.
6. **NEVER manually edit `.lua` build files**. Edit `build_lambda_config.json`, then run `make`.
7. After adding a new Lambda unit test script `*.ls`, ALWAYS add the corresponding expected result `*.txt` file.
8. C++17 standard. Start each log line with a distinct prefix/phrase for easy searching.

| DON'T | DO |
|-------|-----|
| Write to `/tmp` | Write to `./temp/` |
| `std::string s = "hello"` | Use `Str` from `lib/str.h` |
| `printf("debug: %d", x)` | `log_debug("debug: %d", x)` |
| `std::vector<int> v` | Use `ArrayList` from `lib/arraylist.h` |
| Edit `parser.c` manually | Edit `grammar.js` then `make generate-grammar` |
| Edit `premake5.mac.lua` manually | Edit `build_lambda_config.json` then `make` |

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
- **Input**: `lambda/input/` - parsers for JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, etc. Each uses `MarkBuilder` (`lambda/mark_builder.hpp`) to construct Lambda data structures.
- **Output**: `lambda/format/` - formatters (JSON, Markdown, HTML, YAML, etc.)
- **Validation**: `lambda/validator/` - schema-based type validation
- **CSS Engine**: `lambda/input/css/` - CSS parser and cascade resolver

### Radiant Layout Engine (`radiant/`)
CSS layout and rendering engine for HTML/CSS document presentation.
- **DOM/View Tree**: `DomNode` → `DomText`/`DomElement` (both DOM and layout views)
- **Layout**: `layout_block.cpp`, `layout_inline.cpp`, `layout_flex.cpp`, `layout_grid.cpp`, `layout_table.cpp`
- **CSS Resolution**: `resolve_css_style.cpp`
- **Core structs**: `view.hpp` (view hierarchy), `layout.hpp` (`LayoutContext`, `BlockContext`, `Linebox`)

## Lambda CLI Commands

```bash
./lambda.exe                          # Start interactive REPL (default)
./lambda.exe script.ls                # Run a functional Lambda script (JIT with C2MIR)
./lambda.exe --help                   # Show help message for Lambda CLI
./lambda.exe run script.ls            # Run a procedural Lambda script with main() procedure
./lambda.exe validate data.json -s schema.ls     # Validate against custom schema
./lambda.exe convert input.json -t yaml -o output.yaml      # Format conversion
./lambda.exe layout page.html                    # Run CSS layout, output view tree
./lambda.exe render page.html -o output.svg      # Render to SVG/PDF/PNG
./lambda.exe view page.html                      # Open HTML in browser window
make layout suite=baseline                       # Run layout regression tests
make layout test=file_name                       # Compare specific html file layout against browser
```

## Build System & Development Workflow

### Build Commands
```bash
make build              # Incremental build (Premake5-based, fastest)
make clean-all          # Clean all build artifacts
```

**Build architecture**: `build_lambda_config.json` → `utils/generate_premake.py` → Premake5 Lua → Makefiles

### Testing
```bash
make build-test               # Build all test executables
make test                     # Run ALL tests (baseline + extended)
make test-lambda-baseline     # Lambda core functionalities (must pass 100%, when changes maked to Lambda engine)
make test-radiant-baseline    # Radiant core functionalities (must pass 100%, when changes maked to Radiant engine)
```
**Running single test**: `./test/test_some_unit_test.exe --gtest_filter=TestSuite.TestCase`

### Grammar & Parser
```bash
make generate-grammar   # Regenerate parser from grammar.js
```
Grammar source: `lambda/tree-sitter-lambda/grammar.js`. Enum mapping: `lambda/ts-enum.h` (auto-generated).

## Coding Conventions

Lambda adopts a **C+** coding convention - a subset of C++ that is C compatible. See [`doc/dev/C_Plus_Convention.md`](../doc/dev/C_Plus_Convention.md) for details.

- **Memory**: Use `pool_calloc()`, `arena_alloc()` for Lambda objects. Use `MarkBuilder`/`MarkReader` for constructing/reading Lambda data structures.
- **Logging**: Use `log_debug()`/`log_info()`/`log_error()` from `lib/log.h` → outputs to `./log.txt`
- **Naming**: `snake_case` for C/C++ functions, `PascalCase` for classes
- **Comments**: Start inline comments in lowercase: `// process the next token`
- **Error handling**: Return `ItemNull` or `ItemError`, log errors with `log_error()`

## Key Entry Points

| Area | Start here |
|------|-----------|
| Core data types | `lambda/lambda-data.hpp`, `lambda/lambda.h` (C API for MIR) |
| Runtime evaluation | `lambda/lambda-eval.cpp` |
| Memory management | `lambda/lambda-mem.cpp` |
| AST & parsing | `lambda/build_ast.cpp`, `lambda/parse.c` |
| JIT compilation | `lambda/transpile-mir.cpp`, `lambda/mir.c` |
| Data construction | `lambda/mark_builder.hpp`, `lambda/mark_reader.hpp`, `lambda/mark_editor.hpp` |
| Input parsers | `lambda/input/input.cpp` (dispatcher), `lambda/input/input-*.cpp` |
| Output formatters | `lambda/format/` |
| CSS & layout | `radiant/layout.cpp`, `radiant/layout_*.cpp` |
| Custom lib types | `lib/str.h`, `lib/strbuf.h`, `lib/arraylist.h`, `lib/hashmap.h`, `lib/mempool.h` |
| Tests | `test/*.cpp` (GTest), `test/lambda/*.ls` (integration), `test/layout/` (HTML/CSS) |

### Debugging a Crash
1. Check `./log.txt` for execution trace
2. Run with debugger: `lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- extra CLI arguments`

## Lambda Language Documentation
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
