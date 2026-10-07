# Lambda Script — Developer Guide

> **Last verified against tree:** 2026-08-23 *(initial stamp from git history)*

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

The setup scripts handle all dependency installation automatically:

```bash
./setup-mac-deps.sh        # macOS
./setup-linux-deps.sh      # Linux (Ubuntu/Debian)
./setup-windows-deps.sh    # Windows (MSYS2)
```

Pass `--clean` to any script to remove intermediate build artifacts.

### Common Dependencies (All Platforms)

These libraries and tools are required on every platform.

#### Build Tools

| Tool | Purpose |
|------|---------|
| C/C++ compiler (GCC or Clang) | Build toolchain (C++17) |
| Make | Build orchestration |
| CMake | Dependency builds (RE2, ThorVG, etc.) |
| Python 3 | Premake config generation (`utils/generate_premake.py`) |
| Premake5 | Generates platform Makefiles from `build_lambda_config.json` |
| Node.js + npm | Tree-sitter CLI via `npx` for parser generation |
| pkg-config | Library discovery |
| Meson + Ninja | ThorVG build |
| Git | Version control, dependency source cloning |
| xxd | Binary data embedding |
| ccache | Compiler cache (auto-used if available) |

#### Runtime Libraries

| Library | Source | Purpose |
|---------|--------|---------|
| **tree-sitter** | Built from `lambda/tree-sitter/` | Incremental parser framework. Built using the **amalgamated `lib.c`** approach — compiles to a single object file with **no ICU/Unicode library dependency**. |
| **tree-sitter-lambda** | Built from `lambda/tree-sitter-lambda/` | Optional Lambda reference grammar for the `lambda-cst` differential verifier |
| **tree-sitter-javascript** | Built from `lambda/tree-sitter-javascript/` | JavaScript/JSX reference grammar for `lambda-cst` only |
| **tree-sitter-typescript** | Built from `lambda/tree-sitter-typescript/` | TypeScript reference grammar for `lambda-cst` only |
| **tree-sitter-latex** | Built from `lambda/tree-sitter-latex/` | LaTeX document parsing |
| **tree-sitter-latex-math** | Built from `lambda/tree-sitter-latex-math/` | LaTeX math expression parsing |
| **MIR** | Built from `mac-deps/mir/` | JIT compiler backend (Medium Internal Representation) |
| **RE2** | Vendored at `lib/re2/` (`lib/re2/VENDOR.md`), built into `build_temp/re2_build/` | Regular expression engine (pinned 2023-03-01, no Abseil dependency) |
| **utf8proc** | Built from `build_temp/utf8proc/` | Unicode text processing and normalization |
| **mpdecimal** | System package or built from source | Multi-precision decimal arithmetic |
| **rpmalloc** | Built from `mac-deps/rpmalloc-src/` | High-performance memory allocator |
| **libcurl** | Built from source with mbedTLS | HTTP/HTTPS client library |
| **mbedTLS** | System package | TLS/SSL for libcurl |
| **zlib** | System package | Gzip compression (HTTP, PDF, etc.) |
| **bzip2** | System package | Alternative compression format |

#### Radiant Engine Libraries

| Library                 | Purpose                                                  |
| ----------------------- | -------------------------------------------------------- |
| **ThorVG** (v1.0-pre34) | Vector graphics rendering (SW engine, built from source) |
| **FreeType**            | Font rendering                                           |
| **GLFW**                | OpenGL window and context management                     |
| **libpng**              | PNG image support                                        |
| **libjpeg-turbo**       | JPEG image support                                       |
| **giflib**              | GIF image support                                        |
| **Brotli**              | WOFF2 font decompression                                 |
| **WOFF2**               | Web Open Font Format 2 decompression (built from source) |

#### Development / Testing

| Library | Purpose |
|---------|---------|
| **Google Test** | C++ unit testing framework |
| **jsdom** (npm) | JavaScript DOM for test comparison |
| **Puppeteer** (npm) | Browser automation for layout reference capture |

### macOS-Specific (`setup-mac-deps.sh`)

Requires **Homebrew**. Most libraries installed via `brew install`:

```
mbedtls@3  freetype  expat  glfw  libjpeg-turbo  giflib  libpng
zlib  bzip2  pkg-config  cmake  meson  ninja  libevent  premake
```

Built from source in `mac-deps/`:
- libcurl (with mbedTLS), nghttp2, ThorVG, Brotli, WOFF2, rpmalloc, MIR

System frameworks: OpenGL, Cocoa, IOKit, CoreFoundation, CoreServices

### Linux-Specific (`setup-linux-deps.sh`)

Uses **apt** for system packages:

```
build-essential  libcurl4-openssl-dev  libmpdec-dev  libutf8proc-dev
libssl-dev  zlib1g-dev  libnghttp2-dev  libncurses5-dev  libevent-dev
libbrotli-dev  libfreetype6-dev  libexpat1-dev
libglfw3-dev  libpng-dev  libbz2-dev  libturbojpeg0-dev  libgif-dev
libgl1-mesa-dev  libglu1-mesa-dev  libegl1-mesa-dev  gettext
```

Built from source in `build_temp/`:
- Google Test, utf8proc, RE2, ThorVG, WOFF2

### Windows-Specific (`setup-windows-deps.sh`)

Requires **MSYS2** with CLANG64 (preferred) or MINGW64 environment. Uses `pacman`:

```
base-devel  mingw-w64-*-clang (or -gcc)   mingw-w64-*-cmake
mingw-w64-*-ninja  mingw-w64-*-mpdecimal  mingw-w64-*-mbedtls
mingw-w64-*-freetype  mingw-w64-*-glfw    mingw-w64-*-libpng
mingw-w64-*-libjpeg-turbo  mingw-w64-*-giflib  mingw-w64-*-nodejs
mingw-w64-*-meson  mingw-w64-*-pkgconf    ccache  git  vim  jq
```

Built from source in `win-native-deps/`:
- libcurl (minimal, no HTTP/2), ThorVG, WOFF2

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
                                                  ▼
                                  lambda-cst differential verifier
```

The Makefile tracks this reference-grammar chain automatically. The normal
Lambda compiler uses the C recursive-descent/Pratt parser and direct AST sink;
the generated Lambda CST is built only for `lambda-cst` verification.

### Key Files

| File | Role |
|------|------|
| `lambda/tree-sitter-lambda/grammar.js` | **Source of truth** — the Lambda grammar definition |
| `lambda/tree-sitter-lambda/src/parser.c` | Auto-generated reference parser (NEVER edit manually) |
| `test/lambda_parser_poc_diff.c` | Isolated Tree-sitter/C parser differential verifier |
| `lambda/runtime/parser/lambda_parser.c` | Production recursive-descent/Pratt parser |
| `lambda/runtime/build_ast.cpp` | Direct span-based AST sink and semantic construction |

### How to Modify the Grammar

1. **Edit** `lambda/tree-sitter-lambda/grammar.js` — define new rules, tokens, or precedences.

2. **Regenerate** the parser:
   ```bash
   make generate-grammar
   ```
   This runs the pinned Tree-sitter CLI inside `lambda/tree-sitter-lambda/` to regenerate the reference parser artifacts.

3. **Update the differential verifier** if the grammar change affects the
   reference parser's acceptance or CST shape. Production syntax changes are
   implemented in `lambda/runtime/parser/lambda_parser.c` and semantic AST
   changes in `lambda/runtime/build_ast.cpp`.

4. **Update the transpiler** — add MIR generation in `lambda/transpile-mir.cpp` for the new AST node. The former C2MIR source generator was removed and is not an implementation target.

5. **Build and test**:
   ```bash
   make build
   make test-lambda-baseline    # Must pass 100%
   ```

> **Important**: Never edit generated `parser.c`, `grammar.json`, or
> `node-types.json` manually. They are regenerated from `grammar.js`.

### Grammar Structure

The grammar (`grammar.js`) defines:

- **Literals**: integers, floats, decimals, base64, datetime, time, strings
- **Binary operators**: arithmetic (`+`, `-`, `*`, `/`, `div`, `%`, `^`), comparison (`==`, `!=`, `<`, `<=`, `>=`, `>`), logical (`and`, `or`), pipe (`|>`), filter (`|:`), range (`to`), set operations (`|`, `&`, `!`), type (`is`, `in`), proviso (`that`)
- **Type expressions**: union (`|`), intersection (`&`), exclusion (`!`)
- **Attribute context handling**: relational operators excluded when inside element tags to avoid ambiguity

### Additional Grammars

Lambda also includes Tree-sitter grammars for document parsing and isolated
language-reference checks:

| Grammar | Location | Purpose |
|---------|----------|---------|
| JavaScript | `lambda/tree-sitter-javascript/` | `lambda-cst` JavaScript/JSX reference grammar |
| TypeScript | `lambda/tree-sitter-typescript/` | `lambda-cst` TypeScript reference grammar |
| LaTeX | `lambda/tree-sitter-latex/` | LaTeX document parsing |
| LaTeX Math | `lambda/tree-sitter-latex-math/` | LaTeX math expression parsing |

These follow the same `grammar.js` → `parser.c` pattern. The JS/TS archives
are built only by `lambda-cst`; normal Lambda builds use the first-party C
parser under D8.1.3v10.

---

## 5. Working with MIR (JIT Compilation)

Lambda uses [MIR](https://github.com/vnmakarov/mir) (Medium Internal Representation) by Vladimir Makarov for JIT compilation, achieving near-native performance.

### Supported JIT Path and Archived Backend

#### Archived: C2MIR (AST → C source → MIR → machine code)

```
Lambda AST  ──→  transpile.cpp  ──→  C source code string
                                            │
                                   mir.c (getc_func feeds C2MIR)
                                            │
                                            ▼
                                   C2MIR compiler  ──→  MIR IR  ──→  machine code
```

- The former Lambda C source generator (`transpile.cpp` and
  `transpile-call.cpp`) and its `getc_func()` bridge have been removed.
- The diagram is retained only to explain the historical architecture; current
  builds use the direct MIR path below.

#### Supported: Direct MIR (AST → MIR IR → machine code)

```
Lambda AST  ──→  transpile-mir.cpp  ──→  MIR instructions (MIR_MOV, MIR_ADD, MIR_CALL, ...)
                                                    │
                                                    ▼
                                           MIR generator  ──→  machine code
```

- `lambda/transpile-mir.cpp` emits MIR IR instructions directly via the MIR C API.
- No intermediate C code is involved — instructions like `MIR_MOV`, `MIR_ADD`, `MIR_CALL` are built directly.

The vendored MIR dependency still contains its upstream C2MIR frontend, but
Lambda no longer feeds generated Lambda C into it.

### Key Files

| File | Role |
|------|------|
| `doc/dev/lambda/LR_06_C_Transpiler.md` | Historical C2MIR design record (implementation removed) |
| `lambda/transpile-mir.cpp` | Emits MIR IR directly from Lambda AST (direct path) |
| `lambda/mir.c` | MIR context, runtime import registry, linking, and code generation |
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
4. **Call** from transpiled code — emit a `MIR_CALL` instruction in `transpile-mir.cpp`.

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
2. Inspect `temp/mir_dump.txt` for the generated MIR in a debug build.
3. Use `lldb` for native debugging:
   ```bash
   lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- script.ls
   ```

---

## 6. Working with Radiant (Layout & Rendering Engine)

Radiant is Lambda's HTML/CSS layout and rendering engine. It implements browser-compatible layout algorithms (block, inline, flex, grid, table) and renders to SVG, PDF, PNG, and an interactive viewer window.

For the full design document set, see [the Radiant engine design overview](radiant/RAD_00_Overview.md).

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

---

## 7. Worktrees and Agent Gotchas

Build, test and checkout behaviour that is easy to misread as a regression. Several agents usually share one clone, its main checkout and its git worktrees.

### 7.1 Test Data

External corpora live in the sibling repo `../lambda-test` (`js262`, `layout`, `markdown`, `pdf`, `render`, …) and in the git-ignored `ref/`. Wire them up once per clone:

```bash
./setup-test.sh     # needs ../lambda-test; override with LAMBDA_TEST_DIR=<dir>
```

- Clones WPT blob-less into `ref/wpt` and checks out the pinned commit (`WPT_COMMIT=<sha>` overrides). It refuses to switch commits if `ref/wpt` has local changes.
- Links each top-level directory of `../lambda-test` as `test/<name> -> ../../lambda-test/<name>`. It skips existing paths and stops on a symlink that points somewhere else.
- `test/jquery-ui`, `test/markdown` and `test/media` are **tracked** links. `test/js262`, `test/layout`, `test/render` and `test/pdf` are git-ignored.
- Other reference checkouts under `ref/` (e.g. `ref/test262`) are not fetched by the script.

### 7.2 Working in a Git Worktree

A fresh worktree has none of the ignored directories. Worktrees that Claude Code creates (`EnterWorktree`, agent isolation) get `build_temp`, `node_modules`, `mac-deps` and `ref` linked automatically by `worktree.symlinkDirectories` in `.claude/settings.json`; for any other worktree, link them to the main checkout **before the first build**. A failed build leaves a real, empty `build_temp/`, and `ln -s` would then nest the link inside it, so `rmdir` it first.

```bash
MAIN=<main checkout>
ln -s "$MAIN/build_temp"   build_temp     # re2_build, utf8proc, jpeg-turbo (build_lambda_config.json)
ln -s "$MAIN/mac-deps"     mac-deps
ln -s "$MAIN/node_modules" node_modules   # pinned tree-sitter CLI
ln -s "$MAIN/ref"          ref
ln -s "$MAIN/test/js262"   test/js262     # likewise test/layout, test/render, test/pdf
```

- `test/layout` holds the fonts used by the UI-automation preflight. Without the link, the UI runner fails before it runs anything.
- `build_temp/re2_build` is shared. A fresh checkout's newer `lib/re2` mtimes make `make` re-run CMake and Ninja in the shared directory. Before the first build, backdate the worktree's copies (e.g. `find lib/re2 -type f -exec touch -t 202601010000 {} +`).
- The links show as `??`, because `build_*/`, `mac-deps/`, `node_modules/` and `ref/` only match directories. Stage files by name, and never `git add -A`.
- Tracked links (`test/markdown`, `test/jquery-ui`, `test/media`) point to `../../lambda-test/*`, which does not resolve under `.claude/worktrees/<name>/`. Fixtures that use them fail in a worktree. Run those fixtures from the main checkout.
- Worktree paths contain `.claude/`, so a dot appears in every absolute path. Before calling a failure pre-existing, run the base binary **from its own checkout's cwd**.
- **Never run `make release` in a worktree.** That includes anything that depends on it: `release-node-*`, `release-rdb-drivers` and `package-*`. Its `clean-all` runs `rm -rf build_temp/re2_build` through the link and deletes the shared RE2 build. Use `make build-release-compile` instead. Build release modules with `make -C build/premake config=release_native <project>`.
- The stash stack (`refs/stash`) is shared by every worktree. Prefer a WIP commit. If you do stash, use `git stash push -m <tag>` and `git stash apply <sha>`, never a bare `pop`.

Removing a worktree:

```bash
# inside it: drop the links (no -r, no trailing slash, which would follow the link into MAIN)
rm build_temp mac-deps node_modules ref test/js262
git status --porcelain --ignore-submodules=none     # must be empty
# from the main checkout
git worktree remove --force <path>   # an initialized test/yaml submodule needs --force;
                                     # a locked worktree needs --force --force
git branch -d <branch>
```

### 7.3 Build Gotchas

- **macOS `/usr/bin/make` is GNU Make 3.81.** It compares mtimes in whole seconds, so `sleep 2` between before/after steps. It has no `&:` grouped targets, so the tree-sitter-lambda generate rule becomes three independent targets. `make -j generate-grammar` can run `tree-sitter generate` concurrently; run it without `-j`.
- **Same-second source swaps.** A `cp` that lands in the same second as the last build leaves the old object linked. Delete `build/obj/<project>/native/<debug|release>/<file>.o` and rebuild. Then confirm which binary you have, with `cmp` or a probe whose output differs between the two versions.
- **Renamed or moved sources.** Premake never deletes orphaned `.o` files and `ar` never drops members. Purge both, or the link silently uses stale code (seen as bogus "undefined symbol" errors that name the *old* object):
  ```bash
  find build -name "<old_stem>*.o" -delete
  for a in $(find build -name "*.a"); do ar t "$a" | grep -q "^<old_stem>" && rm -f "$a"; done
  ```
  `make build` builds only the `lambda` project. Test archives under `build/lib/` (e.g. `liblambda-rt-cpp.a`) are relinked by `make build-test` or the baseline targets.
- **Debug and release share `lambda.exe`.**
  - `make test-lambda-baseline` runs `make debug` and always leaves a debug binary.
  - `make build-test` rebuilds the flavour of the last top-level build (the `.lambda_release_build` marker).
  - After `make build-release-compile`, `make build` does not relink the debug binary. Run `rm lambda.exe` first.
  - `make release` runs `clean-all`, which deletes `test/*.exe`. Follow it with `make build-test`.
  - Before timing, check the binary: `strings lambda.exe | grep -c 'VALIDATOR] validate_against_base_type'` prints `0` for release.
- **Node modules are separate DSOs.** `modules/node-{core,fs,net,crypto}` compile against `JsRuntimeState` (`lambda/js/js_runtime_state.hpp`), and `make build` does not rebuild them. After a layout change they read wrong offsets, with no link error. Rebuild them with `make build-node-core build-node-fs build-node-net build-node-crypto`, on **both** sides of an A/B. A fresh checkout needs the same rebuild before `require("fs")` works.
- **Archive relinking.** `utils/generate_premake.py` overrides premake's `ldDeps`, so every `.a` in a binary's `linkoptions` is a link prerequisite (`make test-premake-generator` checks this). If a binary seems to hold an old archive, check `grep '^ *LDDEPS' build/premake/<project>.make`.
- **Tree-sitter CLI.** `package.json` pins `tree-sitter-cli` 0.25.10. The Lambda grammar needs 0.25+ (ABI 15), so run `npm install` in a checkout that still has an older CLI. The CLI caches compiled grammars by language name in a per-user cache. Parsing a scratch copy of the grammar can therefore swap the parser other checkouts see. Set `TREE_SITTER_LIBDIR=<own dir>` for each copy; `test/ts_s16_conformance.sh` already uses `temp/tree-sitter-lib`.

### 7.4 Test Gotchas

- **Baseline flakiness under load.** `make test-lambda-baseline` runs suites in parallel, and `test-batch` gives each script 60 s. Heavy scripts can time out, and the failing set changes from run to run. Rerun the test on its own before calling it a regression:
  ```bash
  ./test/test_lambda_gtest.exe --gtest_filter='*<name>*'
  ```
  Radiant layout and page suites flake the same way. `test_page_load_gtest` reports a timeout as exit code `-1`, which looks like a crash. Repeat a run 3× per configuration before you attribute a delta. Don't swap `lambda.exe` while a baseline is running.
- **Goldens run on the `auto` tier,** so JIT-only defects pass the baseline. Sweep each tier through the real harness, which honours `.mac.txt` overrides, `run` mode and negative tests. Hand-rolled per-file sweeps report many false mismatches.
  ```bash
  LAMBDA_EXEC_BACKEND=jit    ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*'
  LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*'
  ```
- **Slow benchmarks.** Entries in `SLOW_BENCHMARK_TESTS` (`test/test_lambda_helpers.hpp`) are dropped at discovery, so no `--gtest_filter` selects them, despite the comment there. No gate checks them; run the script directly and diff it against its `.txt`.
- **T0 interpreter lists.** `test/lambda/interp_p0_subset.txt` and `interp_excluded.txt` together partition the corpus. Regenerate both with `make interp-sweep`, never with `tier_sweep.py` alone. `make test-lambda-interp` (`test_interp_gtest`) is in the extended suite, not the baseline.
- **Whole-corpus compile check.** Goldens never compile package or benchmark modules that no test executes. `./lambda.exe --emit-ast-dump <file.ls>` builds and type-checks one module without running it, and exits 1 on error. To diff error codes, run it over `git ls-files '*.ls'` with a HEAD binary and with the new one. BSD `xargs -I` caps the command at 255 bytes and silently drops files, so pass the binary in an env var with `xargs -n 1`.
- **Display-list stubs.** `test_display_list_gtest`, `test_retained_display_list_gtest` and `test_state_store_gtest` link `test/test_{display_list,retained_display_list,state_store}_stubs.cpp` instead of the ThorVG backend. A new `rdt_*` call in display-list code needs a stub in all three files. `make build` passes, and only `make build-test` fails to link.
- **`test/mir/mir_budgets.json`** is edited by hand and holds per-profile budgets (`default`, `darwin-debug-v3`). Never `git checkout` it to undo one edit: that also reverts every other uncommitted budget change.
- **REPL tests** expect the `λ>` prompt, which needs a UTF-8 `LANG`/`LC_ALL`. In a shell without one, three tests fail on a `>` prompt.
- **UI simulation** failures are logged as `event_sim: <assert> FAIL …` in `./log.txt`, so run without `--no-log` when you debug one.
- **Benchmarks.** Time mode in `test/benchmark/run_benchmarks.py` and `run_standard_benchmarks.py` requires `--typed` (untyped `<bench>.ls` plus typed `<bench>2.ls`) or `--legacy`. Status `ok` means exit 0, no `FAIL` marker and a parsed `__TIMING__`; the output is **not** diffed against goldens. Time a release binary (§7.3).

### 7.5 Environment Knobs

Renamed 2026-10-03. Current binaries no longer read the old names.

| Variable | Values (default) | Old name |
|----------|------------------|----------|
| `LAMBDA_EXEC_BACKEND` | `interp` \| `auto` \| `jit` (`auto`) | `LAMBDA_TIER` |
| `LAMBDA_FUNC_JIT_THRESHOLD` | calls before promotion (5) | `LAMBDA_JIT_THRESHOLD` |
| `LAMBDA_LOOP_JIT_THRESHOLD` | loop back-edges before handoff (10000) | `LAMBDA_JIT_BACKEDGE` |
| `JS_EXEC_BACKEND` | `ast` \| `auto` \| `mir` (`auto`) | `JS_EXECUTION_BACKEND` |
| `JS_FUNC_JIT_THRESHOLD` | (5) | `JS_JIT_THRESHOLD` |
| `JS_LOOP_JIT_THRESHOLD` | (10000) | `JS_JIT_BACKEDGE` |

Archived binaries in the git-ignored `test/benchmark/exe/` (see its `MANIFEST.md`) read only the old names. `run_benchmarks.py` and `run_paired_benchmarks.py` set both names.

Other knobs: `LAMBDA_TEST_DIR` and `WPT_COMMIT` for `setup-test.sh`, `TREE_SITTER_LIBDIR` (§7.3), and `LAMBDA_ROOT`. The Makefile sets `LAMBDA_ROOT` for the render and layout Node runners. Set it yourself when you run `test/render/test_radiant_render.js` directly, because that script resolves its root through the `test/render` symlink.
