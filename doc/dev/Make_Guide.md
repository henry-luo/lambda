# Make Targets Guide

Complete reference for all `make` targets in the Lambda project.

Run `make help` for a quick summary, or see below for full details.

---

## Build Targets

| Target | Description |
|--------|-------------|
| `build` | **Default target.** Incremental debug build via Premake. Auto-detects platform and compiler toolchain. Builds tree-sitter libs, RE2, and regenerates parser/embed headers if needed. |
| `debug` | Debug build with AddressSanitizer enabled. |
| `release` | Optimized release build. Runs `clean-all` first, then compiles with LTO, dead code elimination, symbol visibility control, and strips debug symbols. Output: `lambda_release.exe`. |
| `rebuild` | Force complete rebuild (`clean-all` + `build`). |
| `lambda` | Alias for `build`. |
| `all` | Alias for `lambda`. |
| `lambda-cli` | Headless CLI-only release build. Excludes Radiant layout engine, GUI, font rendering, image codecs. Output: `lambda-cli.exe`. |
| `build-wasm` | Build WebAssembly version via `compile-wasm.sh`. |
| `build-mingw64` | Windows-only: enforce MINGW64 environment build to avoid Universal CRT dependencies. |

### Options

| Variable | Description |
|----------|-------------|
| `JOBS=N` | Number of parallel compilation jobs (default: all CPU cores). |

### Examples

```bash
make                    # incremental debug build (default)
make build JOBS=4       # build with 4 parallel jobs
make debug              # debug build with AddressSanitizer
make release            # optimized release build
make rebuild            # force full rebuild
make lambda-cli         # headless CLI release build
```

---

## Clean Targets

| Target                      | Description                                                                                                                                                                                                                                                                                                   |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `clean`                     | Remove compiled object files (`build/obj`, `build/lib`), legacy `.o`/`.d` files, executables (`lambda.exe`, `lambda_debug.exe`, `lambda_release.exe`), and transpiled temp files. **Does not** remove tree-sitter libraries, RE2 library, or build directories themselves. Fast — allows incremental rebuild. |
| `clean-test`                | Remove test executables, test output directories, `.dSYM` bundles, and test build JSON files.                                                                                                                                                                                                                 |
| `clean-grammar`             | Remove auto-generated grammar and embed files: `parser.c`, `ts-enum.h`, `lambda-embed.h`.                                                                                                                                                                                                                     |
| `clean-premake`             | Remove Premake build directory (`build/premake`) and generated `.lua`/`.make` files.                                                                                                                                                                                                                          |
| `clean-all`                 | Deep clean. Runs `clean-premake` + `clean-test`, then removes all build directories, all tree-sitter `.a` and `.o` files, and the RE2 library. Everything must be rebuilt from scratch after this.                                                                                    |
| `distclean`                 | Most thorough clean. Runs `clean-all` + `clean-grammar` + `clean-test`, then removes all `.exe` files.                                                                                                                                                                                                        |

### Cleanup Hierarchy

```
distclean
  └─ clean-all
  │    ├─ clean-premake
  │    ├─ clean-test
  │    ├─ remove build directories
  │    ├─ remove tree-sitter .a and .o files (all grammars)
  │    └─ remove RE2 library
  └─ clean-grammar
```

---

## Grammar & Parser Targets

| Target | Description |
|--------|-------------|
| `generate-grammar` | Regenerate parser and `ts-enum.h` from `grammar.js`. Usually triggered automatically when `grammar.js` changes. |
| `tree-sitter-libs` | Build all tree-sitter static libraries (tree-sitter, tree-sitter-lambda, tree-sitter-javascript, tree-sitter-latex, tree-sitter-latex-math). The core tree-sitter library is built using the amalgamated `lib.c` approach with no ICU dependency. Auto-regenerates parsers if `grammar.js` files changed. |

### Auto-generation Rules

The build system automatically manages the dependency chain:

```
grammar.js → parser.c → ts-enum.h → C/C++ source files
lambda.h   → lambda-embed.h
```

When `grammar.js` is modified, `parser.c` and `ts-enum.h` are regenerated before compilation.

---

## Premake Build System

| Target | Description |
|--------|-------------|
| `generate-premake` | Generate `premake5.lua` from `build_lambda_config.json` using `utils/generate_premake.py`. |
| `clean-premake` | Clean Premake build artifacts and generated Lua files. |
| `build-test` | Build all test executables using Premake (debug mode). Also builds `lambda-input` DLLs. If a release build was active, restores the release `lambda.exe` after building tests. |
| `build-lambda-input` | Build lambda-input DLLs (prerequisite for `build-test`). |

---

## Test Targets

### Core Test Suites

| Target | Description |
|--------|-------------|
| `test` | Run ALL test suites (baseline + extended). Alias for `test-all`. |
| `test-all` | Run ALL test suites (baseline + extended) in parallel. |
| `test-all-baseline` | Run ALL baseline test suites. **Must pass 100%.** |
| `test-lambda-baseline` | Run Lambda engine baseline tests only. **Must pass 100%.** |
| `test-input-baseline` | Run input parser baselines: HTML5 WPT, CommonMark, YAML, ASCII Math, LaTeX Math. |
| `test-radiant-baseline` | Alias for `test-layout-baseline`. |
| `test-layout-baseline` | Run Radiant layout baseline tests. **Must pass 100%.** |
| `test-extended` | Run extended test suites only (HTTP/HTTPS, ongoing features). |

### Specific Test Suites

| Target | Description |
|--------|-------------|
| `test-lambda` | Run Lambda runtime tests only. |
| `test-library` | Run library tests only. |
| `test-input` | Run input processing tests (MIME detection & math). |
| `test-validator` | Run validator tests only. |
| `test-mir` | Run MIR JIT tests only. |
| `test-std` | Run Lambda Standard Tests (custom test runner). |

### TeX / LaTeX Tests

| Target | Description |
|--------|-------------|
| `test-tex` | Run all TeX typesetting unit tests (output, AST, box, math layout, paragraph, integration). |
| `test-tex-baseline` | Run TeX baseline tests (core box/AST tests). |
| `test-tex-dvi` | Run TeX DVI comparison tests against reference (all). |
| `test-tex-dvi-baseline` | Run TeX DVI comparison baseline tests. **Must pass.** |
| `test-tex-dvi-extended` | Run TeX DVI comparison extended tests (work-in-progress). |
| `test-tex-reference` | Generate reference DVI files from `test/latex/test_*.tex` using system `latex`. |

### Math Tests

| Target | Description |
|--------|-------------|
| `test-math` | Run LaTeX Math test suite (all). |
| `test-math-baseline` | Run LaTeX Math baseline tests (DVI must pass 100%). |
| `test-math-extended` | Run LaTeX Math extended tests (semantic comparison). |
| `test-math-verbose` | Run LaTeX Math tests with verbose output. |
| `test-math-group` | Run tests for a specific group. Usage: `make test-math-group group=<name>`. |
| `test-math-single` | Run a single test. Usage: `make test-math-single test=<name>`. |
| `generate-math-references` | Generate reference files using MathLive + KaTeX. |
| `setup-math-tests` | Install npm dependencies for LaTeX Math tests. |

### PDF Tests

| Target | Description |
|--------|-------------|
| `test-pdf` | Run PDF rendering test suite (compare vs pdf.js). |
| `test-pdf-export` | Export pdf.js operator lists as JSON references. |
| `setup-pdf-tests` | Set up PDF test fixtures and npm dependencies. |

### Fuzzy / Fuzz Tests

| Target | Description |
|--------|-------------|
| `test-fuzzy` | Run fuzzy tests for robustness (5 minutes, mutation + random generation). |
| `test-fuzzy-extended` | Run extended fuzzy tests (1 hour). |
| `test-fuzz` | Alias for `test-fuzzy`. |

### Other Test Targets

| Target | Description |
|--------|-------------|
| `test-dev` | Run comprehensive tests in development mode (tolerates failures from incomplete features). |
| `test-verbose` | Run tests with verbose output. |
| `test-sequential` | Run tests sequentially (not parallel). |
| `test-coverage` | Run tests with code coverage analysis (requires `gcov` + `lcov`). |
| `test-memory` | Run memory leak detection tests. |
| `test-benchmark` | Run performance benchmark tests. |
| `test-integration` | Run end-to-end integration tests. |
| `test-ci` | Run CI test suite (`test` + `test-memory` + `test-integration`). |
| `test-windows` | Run CI tests for Windows executable. |
| `test-linux` | Run CI tests for Linux executable. |

### Test Examples

```bash
make test                          # run everything
make test-all-baseline             # baseline only (must pass 100%)
make test-lambda-baseline          # Lambda engine baseline
make test-layout-baseline          # Radiant layout baseline
make test-input-baseline           # input parser baselines
make test-fuzzy                    # 5-minute fuzz run
make test-math-single test=frac    # single math test
```

---

## Layout Engine Targets

| Target | Description |
|--------|-------------|
| `test-layout` | Run Lambda CSS layout integration tests. Accepts `suite=`, `test=`, `pattern=` parameters. |
| `layout` | Alias for `test-layout`. |
| `capture-layout` | Extract browser layout references using Puppeteer. Requires `test=<name>` or `suite=<name>`. |
| `compare-layout` | Run Radiant layout and compare with browser reference. Usage: `make compare-layout test=<name> [category=<cat>]`. |
| `resolve` | Compare Lambda CSS resolved properties against browser reference data. Usage: `make resolve TEST=<name>`. |
| `resolve-all` | Run CSS resolution verification on all baseline tests. |
| `layout-devtool` | Launch the Layout DevTool Electron app from `utils/layout-devtool`. |

### Layout Examples

```bash
make layout suite=baseline             # run baseline suite
make layout test=table_simple          # run one test (auto-discovers suite)
make layout pattern=float              # run tests matching pattern
make capture-layout suite=baseline     # capture browser references for baseline
make capture-layout test=table_007 force=1   # force re-capture single test
make compare-layout test=sample3 category=page
make resolve TEST=baseline_803_basic_margin
```

### Available Layout Suites

`basic`, `baseline`, `css2.1`, `flex`, `grid`, `yoga`, `wpt-css-box`, `wpt-css-images`, `wpt-css-tables`, `wpt-css-position`, `wpt-css-text`

---

## Static Analysis & Code Quality

| Target | Description |
|--------|-------------|
| `analyze` | Run static analysis with `scan-build`. |
| `analyze-verbose` | Detailed static analysis with extra checkers (security, bounds, etc.). |
| `analyze-single` | Run static analysis on individual files. |
| `analyze-direct` | Direct clang static analysis (bypasses build system). |
| `analyze-compile-db` | Use `compile_commands.json` for analysis (requires `bear`). |
| `analyze-binary` | Binary size analysis by library group. |
| `tidy` | Run `clang-tidy` analysis on C++ files. |
| `tidy-full` | Comprehensive `clang-tidy` with compile database. |
| `tidy-fix` | Run `clang-tidy` with automatic fixes (interactive, prompts before modifying). |
| `tidy-printf` | Convert `printf`/`fprintf(stderr)` to `log_debug()` using Clang AST. Usage: `make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]`. |
| `check` | Basic code checks (TODO/FIXME finder). |
| `format` | Format source code with `clang-format`. |
| `lint` | Run `cppcheck` linter on source files. |
| `generate-compile-db` | Generate `compile_commands.json` manually for clang-tidy. |

### Examples

```bash
make tidy-printf FILE='lambda/lambda-eval.cpp' DRY_RUN=1  # preview changes
make tidy-printf FILE='lambda/*.cpp' BACKUP=1              # with backups
make lint                                                   # run cppcheck
make format                                                 # auto-format code
```

---

## Typesetting Targets

| Target | Description |
|--------|-------------|
| `test-typeset` | Test typesetting system. |
| `test-typeset-math` | Test mathematical typesetting. |
| `test-typeset-markdown` | Test Markdown typesetting. |
| `test-typeset-refined` | Test refined typesetting (view tree architecture). |
| `test-typeset-all` | Run all typesetting tests. |
| `test-typeset-end-to-end` | End-to-end typesetting workflow test. |
| `test-typeset-c` | C-based end-to-end test using Lambda runtime directly (no MIR/JIT). |
| `test-typeset-minimal` | Minimal typesetting test without Lambda dependencies. |
| `test-typeset-simple` | Simple typesetting proof of concept. |
| `test-typeset-workflow` | Lambda typesetting workflow demonstration. |

---

## Utility Targets

| Target | Description |
|--------|-------------|
| `run` | Build and run `lambda.exe` interactively (REPL). |
| `install` | Copy `lambda.exe` to `/usr/local/bin/Lambda`. |
| `uninstall` | Remove from `/usr/local/bin/Lambda`. |
| `intellisense` | Update VS Code IntelliSense database (`compile_commands.json`). |
| `count-loc` | Count lines of code in the repository. |
| `cheatsheet` | Regenerate `Lambda_Cheatsheet.pdf` from Markdown (requires `pandoc`, `xelatex`). |
| `info` | Print project configuration info. |
| `download` | Download a web page to `test/layout/data/page/`. Usage: `make download <url>`. |

---

## Performance & Benchmarking

| Target | Description |
|--------|-------------|
| `bench-compile` | Run C/C++ compilation performance benchmark (single-file, template, multi-file, full build). |
| `benchmark` | Build benchmark (3 runs with parallel jobs). |
| `time-build` | Time a full rebuild. |

---

## Debugging & Info

| Target | Description |
|--------|-------------|
| `help` | Show summary of available make targets. |
| `env-debug` | Print environment detection variables (MSYSTEM, IS_MSYS2, IS_MINGW64, IS_CLANG64). |
| `print-vars` | Print Unicode support status. |
| `print-jobs` | Print detected CPU cores, parallel jobs, and link jobs settings. |
| `validate-build` | Validate build objects for testing. |
