# Claude Instructions for Lambda Script

## CRITICAL Rules for AI Agents

These rules MUST be followed. Violations are considered errors.

1. **NEVER hard code or work around**. Fix code only when have found the root cause. It's ok to leave existing code as failed.
2. **NEVER write files to `/tmp`**. Use `./temp/` dir for ALL temporary files.
3. **NEVER use `std::string`, `std::vector`, `std::map`**, or any other `std::` types. Use `./lib` equivalents (`Str`, `ArrayList`, `HashMap`, etc.).
4. **NEVER use `printf`/`fprintf`/`std::cout`** for debugging. Use `log_debug()`, `log_info()`, `log_error()` from `lib/log.h`.
5. **NEVER manually edit `parser.c`**. Grammar regeneration is automatic from `grammar.js` via `make generate-grammar`.
6. **NEVER modify `log.conf`**.
7. **NEVER manually edit `.lua` build files**. Edit `build_lambda_config.json`, then run `make`.
8. After adding a new Lambda unit test script `*.ls`, ALWAYS add the corresponding expected result `*.txt` file.
9. Follow C++17 standard. Start each log line with a distinct prefix/phrase for easy searching.
10. **NEVER use debug build for performance testing**. Use release build (`make release`).
11. **In `radiant/` layout code, NEVER use `int` for position/dimension variables**. All layout dimensions are `float`. If an `(int)` cast is truly needed (e.g., string length, repeat count), mark it with `// INT_CAST_OK: <reason>`. Run `make lint ARGS='--rule ^no-int-cast-radiant$'` to verify (or `make lint` for the full sweep).
12. When fixing a bug, ALWAYS add a brief code comment at the fix point explaining the root cause or invariant being protected. Do not add generic narration.
13. **NEVER duplicate code.** Grep for an existing helper before writing one. At the 3rd near-identical variant (type/kind/case), extract the shared shape first. To reuse another file's `static`, promote it to the module header — never copy it.
14. **The legacy C2MIR path is FROZEN.** and new runtime/ABI/design work do NOT need C2MIR support.

| DON'T | DO |
|-------|-----|
| Write to `/tmp` | Write to `./temp/` |
| `std::string s = "hello"` | Use `Str` from `lib/str.h` |
| `printf("debug: %d", x)` | `log_debug("debug: %d", x)` |
| `std::vector<int> v` | Use `ArrayList` from `lib/arraylist.h` |
| Edit `parser.c` manually | Edit `grammar.js` then `make generate-grammar` |
| Edit `premake5.mac.lua` manually | Edit `build_lambda_config.json` then `make` |
| `int width = (int)block->width` | `float width = block->width` |
| Copy a `static` helper into another file | Promote it to the module header, then call it |
| Add a 3rd/4th copy of a per-type/kind/case block | Extract a parameterized helper or table first |
| Modify `transpile.cpp` or extend `--c2mir` | Evolve only MIR Direct (`transpile-mir.cpp`) |

## Project Overview

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation, built from scratch in C/C++ with JIT compilation.

### Key Characteristics
- **Language Type**: Pure functional scripting language with modern syntax
- **Implementation**: Custom C/C++ runtime with Tree-sitter based parsing
- **Compilation**: JIT compilation via MIR for near-native performance
- **Memory Management**: Garbage collection with three-tier string allocation (namepool, arena, GC heap)
- **Target Use Cases**: Data processing, document transformation, mathematical computation, CSS layout and rendering
- **Input Formats**: JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, CSV, TOML, etc.
- **Output Formats**: JSON, HTML, Markdown, YAML, PDF, SVG, PNG, etc.
- **Platforms**: macOS, Linux, Windows

## Architecture & Core Data Model

### Runtime Data Representation (`lambda/lambda-data.hpp`)
Lambda uses **tagged pointers/values** in 64-bit `Item` type:
- **Simple scalars** (null, bool, int): packed directly with TypeId in high bits
- **Compound scalars** (int64, float, datetime, decimal, symbol, string, binary): tagged pointers
  - Full-width integers and out-of-band floats use number homes or destination-owned scalar storage
  - Dynamic datetimes are GC-managed; static parser-built datetimes are owned by the `Input` arena
  - Other pointer scalars use the lifetime owner selected by the runtime or Mark-building path
- **Container types** (array, map, element, range): direct pointers extending `struct Container`
  - All start with `TypeId` field for runtime type identification
  - Runtime containers are GC-managed; static Mark containers are Input-owned
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
- **Coherent subsystem headers**: `view.hpp` (view/style/animation data), `layout.hpp` (`LayoutContext`, `BlockContext`, `Linebox`), `render.hpp` (paint/display/render/media APIs), `event.hpp` (input/events), `radiant.hpp` (application shell/public API)

## Lambda CLI Commands

```bash
./lambda.exe                          # Start interactive REPL (default)
./lambda.exe script.ls                # Run a functional Lambda script (JIT with MIR Direct)
./lambda.exe --help                   # Show help message for Lambda CLI
./lambda.exe run script.ls            # Run a procedural Lambda script with main() procedure
./lambda.exe --c2mir script.ls        # Run with legacy C2MIR JIT compilation
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
make build              # Debug build (Premake5-based, fastest)
make release            # Release build (~8MB lambda.exe with optimizations)
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
- **Comments**: Start inline comments in lowercase: `// process the next token`. For bug fixes, add a short comment at the fix point explaining why the code is necessary, especially for lifecycle, ownership, memory, async, batching, parser, or platform edge cases. Prefer root-cause comments over restating what the code does.
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
| CSS, layout, rendering & interaction | `radiant/` — start with `radiant/view.hpp`, `radiant/layout.hpp`, `radiant/render.hpp`, `radiant/event.hpp`, `radiant/radiant.hpp`; detailed design in `doc/dev/radiant/RAD_00_Overview.md` (view/DOM model, CSS resolution, layout, rendering, SVG, events, editing, state, shell, JS scripting, media/webview) |
| LambdaJS (JS engine) | `lambda/js/` — detailed design in `doc/dev/js/JS_00_Overview.md` |
| Core runtime internals | `lambda/` (core) — detailed design in `doc/dev/lambda/LR_00_Overview.md` (value model, transpilers, MIR JIT, memory & GC, builtins) |
| Custom lib types | `lib/str.h`, `lib/strbuf.h`, `lib/arraylist.h`, `lib/hashmap.h`, `lib/mempool.h` |
| Tests | `test/*.cpp` (GTest), `test/lambda/*.ls` (integration), `test/layout/` (HTML/CSS) |

### Debugging a Crash
1. Check `./log.txt` for execution trace
2. Inspect the transpiled MIR — debug builds dump the JIT'd MIR to `temp/mir_dump.txt`; read it to debug Lambda script transpilation/codegen issues (boxing, type, comparison representation)

## Lambda Language Documentation
- `doc/Lambda_Formal_Semantics.md` — **Normative semantics specification (ADR)** — core principles, value domain, truthiness, numerics, equality, total order, absence/errors, mutability, operators, metaprogramming; the semantic authority when docs or implementation disagree (decision records in `vibe/Lambda_Semantics_Formal*.md`)
- `doc/Lambda_Reference.md` — Language overview and quick reference
- `doc/Lambda_Data.md` — Literals and collections (primitives, arrays, lists, maps, elements, ranges)
- `doc/Lambda_Type.md` — Type system (union types, function types, type patterns)
- `doc/Lambda_Expr_Stam.md` — Expressions and statements (operators, pipes, control flow)
- `doc/Lambda_Func.md` — Functions (`fn`, `pn`, closures, higher-order functions)
- `doc/Lambda_Error_Handling.md` — Error handling (`raise`, `T^E` return types, `?` propagation, `let a^err` destructuring)
- `doc/Lambda_Sys_Func.md` — System functions (type, math, string, collection, I/O, date/time)
- `doc/Lambda_Validator_Guide.md` — Schema-based data validation
- `doc/Lambda_Cheatsheet.md` — Quick syntax cheatsheet
- `doc/Lambda_Jube_Runtime.md` — Polyglot runtime build (Python, Bash, Ruby, C2MIR)
- `doc/dev/radiant/RAD_00_Overview.md` — Radiant engine detailed design — view/DOM model, CSS resolution, layout (block/inline/flex/grid/table/positioned), the rendering pipeline (paint IR, display list, painters, PDF/SVG), vector graphics, events, animation, editing, forms, interaction state, application shell, JS scripting, and media/webview (index to the RAD_01–RAD_22 set)
- `doc/dev/lambda/LR_00_Overview.md` — Lambda core-runtime detailed design — compilation pipeline, value & type model, the C and MIR-Direct transpilers, MIR JIT, memory & GC, runtime builtins, error handling, the Mark data API, the procedural runtime, and the schema validator (index to the LR_01–LR_13 set)
- `doc/dev/js/JS_00_Overview.md` — LambdaJS runtime detailed design — compilation pipeline, value model, runtime, standard library, RegExp, async/modules, DOM, and Node.js compatibility (index to the JS_01–JS_16 set)
