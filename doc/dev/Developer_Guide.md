# Lambda Script — Developer Guide

This guide is for developers working on the **Lambda and Radiant C++ runtime engine** itself — building the compiler, modifying the parser, extending the JIT backend, and developing the layout engine.

It is **not** a guide for writing Lambda Script programs. For the Lambda language reference and scripting documentation, see the [Language Reference](../Lambda_Reference.md) and other docs under `doc/`.

---

## 1. Installing from Source

### Prerequisites

| Tool | Purpose | Install |
|------|---------|---------|
| C/C++ compiler (GCC/Clang) | Build toolchain | macOS: `xcode-select --install`; Linux: `sudo apt install build-essential` |
| Make | Build orchestration | Included with compiler toolchain |
| Git | Source control | macOS: `brew install git`; Linux: `sudo apt install git` |
| Node.js + npm | Tree-sitter CLI (`npx`) | macOS: `brew install node`; Linux: `sudo apt install nodejs npm` |
| CMake | Some dependency builds | macOS: `brew install cmake`; Linux: `sudo apt install cmake` |
| pkg-config | Library discovery | macOS: `brew install pkg-config`; Linux: `sudo apt install pkg-config` |
| Python 3 | Build script generation | Usually pre-installed |
| Premake5 | Generates Makefiles from config | macOS: `brew install premake`; Linux: download from premake.github.io |

### Clone and Build

```bash
git clone <repo-url> && cd Jubily

# 1. Install platform dependencies (pick one)
./setup-mac-deps.sh        # macOS
./setup-linux-deps.sh      # Linux (Ubuntu)
./setup-windows-deps.sh    # Windows (MSYS2)

# 2. Build
make build

# 3. Verify
./lambda.exe --help
```

The build produces `lambda.exe` in the project root.

---

## 2. Setting Up Dependencies

The setup scripts handle all dependency installation automatically. Here is what each platform requires and what the scripts do.

### macOS (`setup-mac-deps.sh`)

Requires **Homebrew**. The script installs:

- **Build tools**: Xcode CLI tools, cmake, pkg-config, meson, ninja
- **TLS**: mbedtls@3 (for libcurl HTTPS support)
- **Fonts/graphics**: freetype, expat (for fontconfig), GLFW, ThorVG (built from source)
- **Compression**: zlib, bzip2, brotli (for WOFF2)
- **Networking**: libcurl (built from source with mbedTLS), nghttp2, libevent
- **Unicode**: utf8proc (built from `build_temp/`)
- **Decimal math**: mpdecimal
- **Memory**: rpmalloc (built from source in `mac-deps/`)
- **Images**: libjpeg-turbo (built from `build_temp/`), libpng, libgif
- **Regex**: RE2 (built from `build_temp/re2-noabsl/`)
- **JIT**: MIR (built from `build_temp/mir/`)
- **Tree-sitter**: Core library + Lambda/JavaScript/LaTeX grammars (built from `lambda/tree-sitter*/`)
- **Node packages**: jsdom (test comparison), puppeteer (browser layout tests)

```bash
# Full setup
./setup-mac-deps.sh

# Clean intermediate build artifacts
./setup-mac-deps.sh --clean
```

### Linux (`setup-linux-deps.sh`)

Uses **apt** for system packages, builds remaining dependencies from source.

System packages installed via apt:
```
libcurl4-openssl-dev  libmpdec-dev  libutf8proc-dev  libssl-dev
zlib1g-dev  libnghttp2-dev  libncurses5-dev  build-essential
libglfw3-dev  libfreetype6-dev  libfontconfig1-dev  libexpat1-dev
libpng-dev  libbz2-dev  libturbojpeg0-dev  libgif-dev  gettext
libgl1-mesa-dev  libglu1-mesa-dev  libegl1-mesa-dev
```

### Windows (`setup-windows-deps.sh`)

Requires **MSYS2** with the MINGW64 or CLANG64 environment. Uses `pacman` for package management.

### Dependency Architecture

All dependency paths are defined in `build_lambda_config.json`. The build pipeline is:

```
build_lambda_config.json
        │
        ▼  (utils/generate_premake.py)
premake5.{mac,lin,win}.lua    ← DO NOT EDIT (auto-generated)
        │
        ▼  (premake5)
build/premake/Makefile
        │
        ▼  (make)
build/lib/*.a  →  lambda.exe
```

> **Rule**: Never edit `premake5.*.lua` files directly. Edit `build_lambda_config.json` and run `make` to regenerate.

---

## 3. Building and Running Tests

### Build Commands

```bash
make build              # Incremental debug build (default)
make clean-all          # Clean all build artifacts
make build-test         # Build all test executables
```

Build uses **ccache** automatically when available, and parallelizes across all CPU cores.

### Running Tests

```bash
# All tests
make test               # Run ALL tests (baseline + extended)
make test-all-baseline  # ALL baseline suites (must pass 100%)

# Core suites — run these after any engine changes
make test-lambda-baseline    # Lambda core functionality
make test-radiant-baseline   # Radiant layout engine
make test-input-baseline     # Input parsers (HTML5 WPT, CommonMark, YAML)

# Targeted suites
make test-mir                # MIR JIT compilation tests
make test-lambda             # Lambda runtime tests
make test-std                # Lambda Standard Tests (custom runner)
make test-tex                # TeX typesetting
make test-tex-baseline       # TeX baseline tests
make test-tex-dvi            # TeX DVI comparison
make test-pdf                # PDF rendering
make test-layout             # CSS layout integration (Puppeteer-based)
make test-extended           # Extended/ongoing feature tests

# Stress testing
make test-fuzzy              # Fuzzy tests (~5 min, mutation + random input)
make test-fuzzy-extended     # Extended fuzzy tests (~1 hour)
```

### Running a Single Test

```bash
./test/test_lambda_gtest.exe --gtest_filter=TestSuite.TestCase
```

All GTest executables support standard GTest flags (`--gtest_filter`, `--gtest_list_tests`, `--gtest_repeat`, etc.).

### Test Structure

```
test/
├── test_*_gtest.cpp      # GTest unit tests (130+ files)
├── lambda/               # Lambda script integration tests
│   ├── *.ls              # Test scripts
│   └── *.txt             # Expected output (must match 1:1)
├── layout/               # CSS layout regression tests
│   ├── data/             # Test HTML files
│   ├── reference/        # Expected view trees
│   ├── compare-layout.js # Puppeteer-based comparator
│   └── extract_browser_references.js
├── std/                  # Lambda Standard Tests
│   ├── boundary/
│   ├── integration/
│   ├── negative/
│   └── performance/
├── test_benchmark.sh     # Performance benchmarks
├── test_fuzz.sh          # Fuzz testing script
└── test_memory.sh        # Memory leak detection
```

### Writing a New Unit Test

1. Create `test/test_myfeature_gtest.cpp` using the GTest pattern:

```cpp
#include <gtest/gtest.h>

class MyFeatureTest : public ::testing::Test {
protected:
    void SetUp() override { /* setup */ }
    void TearDown() override { /* cleanup */ }
};

TEST_F(MyFeatureTest, BasicCase) {
    // test logic
    EXPECT_EQ(expected, actual);
}
```

2. Add to `build_lambda_config.json` under a test project, then run `make`.

### Writing a Lambda Integration Test

1. Create `test/lambda/my_test.ls` with a Lambda script.
2. Create `test/lambda/my_test.txt` with the expected output (exact match).
3. Run with `make test-lambda-baseline` to verify.

> **Rule**: Every new `.ls` test script **must** have a corresponding `.txt` expected-output file.

### Layout Regression Tests

```bash
make layout suite=baseline         # Run all baseline layout tests
make layout test=file_name         # Test a specific HTML file against browser reference
make capture-layout                # Re-capture browser references via Puppeteer
```

---

## 4. Working with Tree-sitter Grammar

The Lambda language grammar is defined in Tree-sitter and drives the entire parsing pipeline.

### Dependency Chain

```
grammar.js  ──→  tree-sitter generate  ──→  parser.c + grammar.json + node-types.json
                                                  │
                                        utils/update_ts_enum.sh
                                                  │
                                                  ▼
                                            ts-enum.h  (enum sym_*/field_* identifiers)
                                                  │
                                                  ▼
                                build_ast.cpp  (matches CST nodes → typed AST)
                                                  │
                                                  ▼
                          transpile.cpp / transpile-mir.cpp  (code generation)
```

The Makefile tracks this entire chain automatically. When `grammar.js` changes, `make` triggers `tree-sitter generate` → `update_ts_enum.sh` → recompile dependent sources.

### Key Files

| File | Role |
|------|------|
| `lambda/tree-sitter-lambda/grammar.js` | **Source of truth** — the Lambda grammar definition |
| `lambda/tree-sitter-lambda/src/parser.c` | Auto-generated parser (NEVER edit manually) |
| `lambda/ts-enum.h` | Auto-generated enum mapping of grammar symbols (NEVER edit) |
| `utils/update_ts_enum.sh` | Script that extracts enums from `parser.c` → `ts-enum.h` |
| `lambda/build_ast.cpp` | Consumes CST using `sym_*` enums, builds typed AST |

### How to Modify the Grammar

1. **Edit** `lambda/tree-sitter-lambda/grammar.js` — define new rules, tokens, or precedences.

2. **Regenerate** the parser:
   ```bash
   make generate-grammar
   ```
   This runs `npx tree-sitter-cli@0.24.7 generate` inside `lambda/tree-sitter-lambda/` and then runs `update_ts_enum.sh` to update `ts-enum.h`.

3. **Update AST builder** — in `lambda/build_ast.cpp`, handle the new node types using the auto-generated `sym_*` enum values from `ts-enum.h`:
   ```cpp
   case sym_my_new_node:
       // build AST node for the new grammar rule
       break;
   ```

4. **Update transpiler(s)** — add code generation in `lambda/transpile.cpp` (C path) and/or `lambda/transpile-mir.cpp` (direct MIR path) for the new AST node.

5. **Build and test**:
   ```bash
   make build
   make test-lambda-baseline    # Must pass 100%
   ```

> **Important**: Never edit `parser.c` or `ts-enum.h` manually. They are fully auto-generated. The Makefile dependency tracking ensures they stay in sync with `grammar.js`.

### Grammar Structure

The grammar (`grammar.js`) defines:

- **Literals**: integers, floats, decimals, base64, datetime, time, strings
- **Binary operators**: arithmetic (`+`, `-`, `*`, `/`, `div`, `%`, `^`), comparison (`==`, `!=`, `<`, `<=`, `>=`, `>`), logical (`and`, `or`), pipe (`|`, `|>`, `|>>`), range (`to`), set operations (`&`, `!`), type (`is`, `in`), filter (`that`)
- **Type expressions**: union (`|`), intersection (`&`), exclusion (`!`)
- **Attribute context handling**: relational operators excluded when inside element tags to avoid ambiguity

### The `ts-enum.h` Enum

Generated by `utils/update_ts_enum.sh`, this header contains two enums extracted from `parser.c`:

- **`enum ts_symbol_identifiers`**: Maps grammar symbols to integer IDs (e.g., `sym_identifier = 1`, `sym_string = 5`, `anon_sym_PLUS = 31`)
- **`enum ts_field_identifiers`**: Maps field names to IDs

These enums are the bridge between the Tree-sitter parse tree and the Lambda compiler's AST builder.

### Additional Grammars

Lambda also includes Tree-sitter grammars for input parsing:

| Grammar | Location | Purpose |
|---------|----------|---------|
| JavaScript | `lambda/tree-sitter-javascript/` | JSX/JavaScript input parsing |
| LaTeX | `lambda/tree-sitter-latex/` | LaTeX document parsing |
| LaTeX Math | `lambda/tree-sitter-latex-math/` | LaTeX math expression parsing |

These follow the same `grammar.js` → `parser.c` pattern and are built as static libraries.

---

## 5. Working with MIR (JIT Compilation)

Lambda uses [MIR](https://github.com/vnmakarov/mir) (Medium Internal Representation) by Vladimir Makarov for JIT compilation, achieving near-native performance.

### Two JIT Paths

Lambda provides two compilation paths from AST to machine code:

#### Path 1: C2MIR (AST → C source → MIR → machine code)

```
Lambda AST  ──→  transpile.cpp  ──→  C source code string
                                            │
                                   mir.c (getc_func feeds C2MIR)
                                            │
                                            ▼
                                   C2MIR compiler  ──→  MIR IR  ──→  machine code
```

- `lambda/transpile.cpp` generates C source code from the Lambda AST.
- `lambda/mir.c` feeds this C source into the C2MIR compiler character-by-character via `getc_func()`.
- The C2MIR frontend parses the C code into MIR IR, which is then compiled to native machine code.

#### Path 2: Direct MIR (AST → MIR IR → machine code)

```
Lambda AST  ──→  transpile-mir.cpp  ──→  MIR instructions (MIR_MOV, MIR_ADD, MIR_CALL, ...)
                                                    │
                                                    ▼
                                           MIR generator  ──→  machine code
```

- `lambda/transpile-mir.cpp` emits MIR IR instructions directly via the MIR C API.
- No intermediate C code is involved — instructions like `MIR_MOV`, `MIR_ADD`, `MIR_CALL` are built directly.

Both paths share the same set of runtime functions registered in `mir.c`.

### Key Files

| File | Role |
|------|------|
| `lambda/transpile.cpp` | Generates C source from Lambda AST (C2MIR path) |
| `lambda/transpile-mir.cpp` | Emits MIR IR directly from Lambda AST (direct path) |
| `lambda/mir.c` | C bridge: runtime function registry + C2MIR feeder |
| `lambda/lambda.h` | C API header — function signatures callable from JIT code |
| `include/mir.h` | MIR library API (upstream, v0.2) |
| `build_temp/mir/` | MIR library source (built into `libmir.a`) |

### Runtime Function Registry (`mir.c`)

The `func_list[]` table in `mir.c` maps ~150+ function names to function pointers, making them available to JIT-compiled code. Categories include:

- **Collection constructors**: `new_array`, `new_list`, `new_map`, `new_element`
- **Arithmetic/math**: `op_add`, `op_sub`, `op_mul`, `op_div`, `op_mod`, `op_pow`, `math_sin`, `math_cos`, etc.
- **String operations**: `str_concat`, `str_len`, `str_slice`, `str_find`, etc.
- **Type operations**: `get_type_id`, `to_int`, `to_float`, `to_string`, `to_decimal`
- **Pipe support**: `pipe_map_len`, `pipe_map_val`, `pipe_map_key`
- **Error handling**: `raise_error`, `get_error`

When adding new runtime functions:

1. **Implement** the function in C (in an appropriate source file).
2. **Declare** it in `lambda/lambda.h` so MIR-compiled code can reference it.
3. **Register** it in `func_list[]` in `lambda/mir.c`:
   ```c
   {"my_new_func", (void*)my_new_func},
   ```
4. **Call** from transpiled code — either emit a C function call in `transpile.cpp`, or emit a `MIR_CALL` instruction in `transpile-mir.cpp`.

### MIR Transpiler Architecture (`transpile-mir.cpp`)

The `MirTranspiler` struct manages the direct MIR emission context:

- **MIR context/module/function**: Manages IR construction lifecycle
- **Import cache**: HashMap of `MirImportEntry` (proto + import pairs) — avoids duplicate imports
- **Variable scope stack**: 64 levels deep for nested scopes
- **Loop label stack**: 32 levels for nested loop break/continue
- **Register counter**: Sequential allocation of MIR virtual registers
- **Pipe context registers**: `pipe_item_reg`, `pipe_index_reg` for pipe expression evaluation
- **TCO support**: `tco_func`, `tco_label` for tail-call optimization
- **Closure support**: `current_closure`, `env_reg` for captured variable environments

### Type Mapping

Lambda types map to MIR types as follows:

| Lambda Type | MIR Type | Width |
|------------|----------|-------|
| `float` | `MIR_T_D` | 64-bit double |
| All others (int, string, list, map, ...) | `MIR_T_I64` | 64-bit integer (tagged pointers) |

This reflects Lambda's tagged-pointer data model where most values are 64-bit `Item` values carrying both type and data information.

### Running MIR-specific Tests

```bash
make test-mir                    # Run MIR JIT test suite
./lambda.exe script.ls           # Run a script with JIT (default mode)
```

### Debugging JIT Code

1. Check `./log.txt` for execution trace — JIT operations are logged.
2. The generated C source (C2MIR path) is written to `_transpiled_*.c` files in the project root for inspection.
3. Use `lldb` for native debugging:
   ```bash
   lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- script.ls
   ```

---

## 6. Working with Radiant (Layout & Rendering Engine)

Radiant is Lambda's HTML/CSS layout and rendering engine. It implements browser-compatible layout algorithms (block, inline, flex, grid, table) and renders to SVG, PDF, PNG, and an interactive viewer window.

For the full design document, see [Radiant_Layout_Design.md](../Radiant_Layout_Design.md).

### Architecture Overview

```
HTML/CSS Input
      │
      ▼
DOM Parsing  (lambda/input/input-html.cpp)
      │
      ▼
CSS Cascade  (lambda/input/css/css_engine.cpp, resolve_css_style.cpp)
      │
      ▼
Layout Dispatch  (radiant/layout_block.cpp)
      │
      ├── Block/Inline  (layout_block.cpp, layout_inline.cpp)
      ├── Flexbox        (layout_flex_multipass.cpp)
      ├── Grid           (layout_grid_multipass.cpp)
      └── Table          (layout_table.cpp)
      │
      ▼
View Tree
      │
      ├── SVG/PDF/PNG render  (render_svg.cpp, render_pdf.cpp)
      └── Interactive window  (window.cpp, GLFW + ThorVG)
```

### Key Files

| File | Role |
|------|------|
| `radiant/view.hpp` | View hierarchy, property structs, type enums |
| `radiant/layout.hpp` | Core structs: `LayoutContext`, `BlockContext`, `Linebox` |
| `radiant/layout.cpp` | Common layout utilities (line height, style resolution) |
| `radiant/layout_block.cpp` | Block layout + layout mode dispatch entry point |
| `radiant/layout_inline.cpp` | Inline layout and line box management |
| `radiant/layout_text.cpp` | Text measurement and word wrapping |
| `radiant/layout_flex.cpp` | Core 9-phase flex algorithm |
| `radiant/layout_flex_multipass.cpp` | Flex entry point, pass orchestration |
| `radiant/layout_grid.cpp` | Grid track sizing and item placement |
| `radiant/layout_grid_multipass.cpp` | Grid entry point, pass orchestration |
| `radiant/layout_table.cpp` | Table layout algorithm |
| `radiant/layout_positioned.cpp` | CSS absolute/fixed positioning |
| `radiant/intrinsic_sizing.cpp` | Min/max content size calculation |
| `radiant/resolve_css_style.cpp` | CSS property resolution and cascade |
| `radiant/render.cpp` | Render dispatch |
| `radiant/render_svg.cpp` | SVG output renderer |
| `radiant/render_pdf.cpp` | PDF output renderer |
| `radiant/window.cpp` | GLFW interactive viewer window |
| `lambda/input/css/css_engine.cpp` | CSS parser and cascade engine |
| `lambda/input/css/selector_matcher.cpp` | CSS selector matching |
| `lambda/input/input-html.cpp` | HTML5 parser |

### Unified DOM/View Tree

Radiant uses a single tree where DOM nodes **are** their own view representations:

- `DomNode` → base class
- `DomText` → text node (extends to `ViewText`)
- `DomElement` → element node (extends to `ViewElement` → `ViewSpan` → `ViewBlock`)

This avoids parallel tree synchronization — parent/sibling pointers serve both DOM and view purposes. All positions are **relative to the parent's border box**.

### Layout Mode Dispatch

The `layout_block()` function in `layout_block.cpp` is the central dispatch point:

```cpp
switch (display.inner) {
    case CSS_VALUE_FLOW:       layout_block_content(lycon, block);   break;
    case CSS_VALUE_FLEX:       layout_flex_content(lycon, block);    break;
    case CSS_VALUE_GRID:       layout_grid_content(lycon, block);    break;
    case CSS_VALUE_TABLE:      layout_table_content(lycon, elmt, display); break;
}
```

### How to Add a New CSS Property

1. **Add the CSS value** — define new enum values in `lambda/input/css/css_value.hpp` if needed.
2. **Parse it** — handle the property in `lambda/input/css/css_value_parser.cpp`.
3. **Store it** — add a field to the appropriate property struct in `radiant/view.hpp` (`BlockProp`, `BoundaryProp`, `InlineProp`, `FlexProp`, `GridProp`, etc.).
4. **Resolve it** — apply the property during cascade in `radiant/resolve_css_style.cpp`.
5. **Use it in layout** — consume the property value in the relevant layout file.
6. **Test it** — add an HTML test file and capture a browser reference (see below).

### How to Add a New Layout Mode

1. Create `radiant/layout_newmode.cpp` and `radiant/layout_newmode.hpp`.
2. Add the entry point function (e.g., `layout_newmode_content()`).
3. Add a dispatch case in `layout_block.cpp`.
4. Add the source file to `build_lambda_config.json` (the `radiant` source directory already auto-includes new files; only add if in a subdirectory).
5. Build and add layout regression tests.

### Layout Testing Workflow

Radiant layout tests compare the engine's view tree output against browser-rendered reference data captured via Puppeteer.

```bash
# Run all layout baseline tests (must pass 100%)
make test-radiant-baseline

# Run a specific layout test
make layout test=table_simple

# Run tests matching a pattern
make layout pattern=float

# Run a test suite
make layout suite=baseline
```

#### Test structure

```
test/layout/
├── data/                     # Test HTML files organized by suite
│   ├── baseline/             # Baseline suite (must pass 100%)
│   │   ├── basic-text.html
│   │   ├── flex-wrap.html
│   │   └── ...
│   └── extended/             # Extended/experimental tests
├── reference/                # Browser-captured reference data
│   ├── baseline/             # Expected view trees (JSON)
│   └── ...
├── test_radiant_layout.js    # Test runner (Node.js)
├── compare-layout.js         # View tree comparison logic
└── extract_browser_references.js  # Puppeteer reference extractor
```

#### Adding a new layout test

1. **Create the HTML file** in `test/layout/data/<suite>/`:
   ```html
   <!DOCTYPE html>
   <style>
     .box { width: 100px; height: 50px; background: red; }
   </style>
   <div class="box">Hello</div>
   ```

2. **Capture the browser reference**:
   ```bash
   make capture-layout test=my_new_test
   # Or force re-capture:
   make capture-layout test=my_new_test force=1
   ```

3. **Run the test** to verify Radiant matches the browser:
   ```bash
   make layout test=my_new_test
   ```

4. **Debug** if the test fails:
   ```bash
   # Output the view tree for inspection
   ./lambda.exe layout test/layout/data/baseline/my_new_test.html

   # Render to image for visual comparison
   ./lambda.exe render test/layout/data/baseline/my_new_test.html -o temp/debug.svg
   ```

### Rendering

Radiant supports multiple render targets:

| Target | File | CLI |
|--------|------|-----|
| SVG | `render_svg.cpp` | `./lambda.exe render page.html -o out.svg` |
| PDF | `render_pdf.cpp` | `./lambda.exe render page.html -o out.pdf` |
| PNG/JPEG | `render_img.cpp` | `./lambda.exe render page.html -o out.png` |
| Interactive viewer | `window.cpp` | `./lambda.exe view page.html` |

### Debugging Layout Issues

1. **View tree output** — `./lambda.exe layout file.html` prints the computed view tree with positions, dimensions, and box model values.
2. **Render to SVG** — visual inspection is often fastest: `./lambda.exe render file.html -o temp/debug.svg`.
3. **Log output** — check `./log.txt` for layout trace messages. Use `log_debug()` in layout code for additional instrumentation.
4. **Compare against browser** — `make layout test=file_name` shows element-by-element position/size differences (1–2px tolerance for floating-point variations).
5. **Debugger** — `lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- layout file.html`
