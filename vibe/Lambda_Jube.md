# Proposal: Split Lambda into Two Build Configurations

## Summary

Split the current monolithic `build_lambda_config.json` into two distinct build configurations:

| Config | Binary | Focus |
|--------|--------|-------|
| **lambda** | `lambda.exe` | Lambda + JS/TS + Radiant (HTML/CSS/SVG/JS) |
| **lambda-jube** | `lambda-jube.exe` | Polyglot runtime — everything in Lambda plus Python, Bash, Ruby, and future languages |

"Jube" = **J**avaScript, r**U**by, **B**ash, python — a polyglot **jube** (gem/jewel) that wraps all supported language runtimes into one binary.

---

## Motivation

1. **Binary size** — The current `lambda.exe` (~21MB debug) bundles 5 tree-sitter parsers and 4 language runtimes that many users don't need. A focused Lambda build drops ~56 extra source files and 4 parser libraries.

2. **Build time** — Fewer compilation units means faster incremental builds for the core Lambda+JS+Radiant workflow.

3. **Separation of concerns** — Lambda's core value is functional scripting + document processing + Radiant layout. JS/TS support is tightly coupled to Radiant (DOM, CSSOM, event loop). Python/Bash/Ruby are independent language runtimes that don't interact with Radiant.

4. **Testing** — Focused baselines: `test-lambda-baseline` validates core + JS/TS + Radiant; `test-jube-baseline` adds cross-language tests.

5. **Future extensibility** — New language runtimes (PHP, Lua, etc.) go into `lambda-jube` without bloating the core binary.

---

## Architecture: What Goes Where

### Lambda (core) — `lambda.exe`

**Language runtimes:**
- Lambda scripting (`lambda/`)
- JavaScript (`lambda/js/` — 21 files)
- TypeScript (`lambda/ts/` — 7 files)

**Document pipeline:**
- Input parsers (`lambda/input/`, `lambda/input/css/`, `lambda/input/html5/`, `lambda/input/markup/`)
- Output formatters (`lambda/format/`)
- Validators (`lambda/validator/`)

**Radiant layout engine:**
- Full CSS layout + rendering (`radiant/`, `radiant/pdf/`, `radiant/webdriver/`)

**Tree-sitter parsers (5):**

| Parser | Needed for |
|--------|-----------|
| tree-sitter | Core dependency |
| tree-sitter-lambda | Lambda scripts |
| tree-sitter-javascript | JS runtime |
| tree-sitter-typescript | TS runtime |
| tree-sitter-latex | LaTeX input |
| tree-sitter-latex-math | LaTeX math |

**Serve module (subset):**
- Core server: `server.*`, `router.*`, `middleware.*`, `worker_pool.*`
- HTTP: `http_request.*`, `http_response.*`, `body_parser.*`, `cookie.*`, `static_handler.*`, `tls_handler.*`
- API: `openapi.*`, `swagger_ui.*`, `schema_validator.*`, `rest.*`
- Language backends: `backend_lambda.cpp`, `backend_js.cpp` only
- Express compat: `express_compat.*`
- Transport: `uds_transport.*`, `ipc_proto.*`
- Utilities: `serve_types.*`, `serve_utils.*`, `mime.*`

**Libraries:** All current libraries (curl, MIR, mpdec, freetype, ThorVG, glfw, libpng, libuv, etc.)

**Defines:** Remove `LAMBDA_RUBY`. Add nothing new — this becomes the default.

### Lambda Jube (polyglot) — `lambda-jube.exe`

**Everything in Lambda, plus:**

**Additional language runtimes:**
- Python (`lambda/py/` — 17 files)
- Bash (`lambda/bash/` — 8 files)
- Ruby (`lambda/rb/` — 10 files)

**Additional tree-sitter parsers (3):**

| Parser | Version |
|--------|---------|
| tree-sitter-python | 0.25.0 |
| tree-sitter-ruby | 0.23.1 |
| tree-sitter-bash | 0.25.1 |

**Additional serve backends:**
- `backend_python.cpp`, `backend_bash.cpp`
- `flask_compat.*`, `asgi_bridge.*`, `wsgi_bridge.py`

**Defines:** `LAMBDA_JUBE`, `LAMBDA_RUBY` (existing)

---

## Implementation Plan

### Phase 1: Introduce Feature Flags

Add compile-time guards for Python, Bash, and Ruby (Ruby already has `LAMBDA_RUBY`):

| Flag | Guards |
|------|--------|
| `LAMBDA_PYTHON` | `lambda/py/` includes, `backend_python.cpp`, Python CLI handler |
| `LAMBDA_BASH` | `lambda/bash/` includes, `backend_bash.cpp`, Bash CLI handler |
| `LAMBDA_RUBY` | Already exists — no changes needed |
| `LAMBDA_JUBE` | Master flag: implies all three above |

Apply the existing `LAMBDA_RUBY` pattern to Python and Bash:

```cpp
// main.cpp
#ifdef LAMBDA_PYTHON
#include "py/py_transpiler.hpp"
#endif

#ifdef LAMBDA_BASH
#include "bash/bash_transpiler.hpp"
#include "bash/bash_runtime.h"
#endif
```

```c
// sys_func_registry.c
#ifdef LAMBDA_PYTHON
    // register Python system functions
#endif
```

**Files to modify:**
- `lambda/main.cpp` — wrap Python/Bash includes and CLI handlers in `#ifdef`
- `lambda/sys_func_registry.c` — wrap language-specific registrations
- `lambda/serve/language_backend.cpp` — conditionally register backends
- `lambda/module_registry.cpp` — guard cross-language import helpers

### Phase 2: Build Config Changes

#### Option A: Single config file with variants (recommended)

Extend `build_lambda_config.json` with a new `jube` platform variant, similar to how `cli` is defined:

```jsonc
{
    "platforms": {
        "jube": {
            "description": "Polyglot runtime: Lambda + JS/TS + Python + Bash + Ruby + Radiant",
            "output": "lambda-jube.exe",
            "source_dirs": [
                // inherit all from top-level, explicitly include:
                "lambda/py",
                "lambda/bash",
                "lambda/rb"
            ],
            "defines": [
                "LAMBDA_JUBE",
                "LAMBDA_PYTHON",
                "LAMBDA_BASH",
                "LAMBDA_RUBY"
            ],
            "libraries": [
                // all from top-level, plus:
                "tree-sitter-python",
                "tree-sitter-ruby",
                "tree-sitter-bash"
            ],
            "includes": [
                // all from top-level, plus:
                "lambda/tree-sitter-python/bindings/c",
                "lambda/tree-sitter-ruby/bindings/c",
                "lambda/tree-sitter-bash/bindings/c"
            ],
            "build_dir": "build_jube"
        }
    }
}
```

Then modify the **top-level** config to be the focused Lambda build:

1. Remove from top-level `source_dirs`: `lambda/py`, `lambda/bash`, `lambda/rb`
2. Remove from top-level `defines`: `LAMBDA_RUBY`
3. Remove from top-level `libraries`: `tree-sitter-python`, `tree-sitter-ruby`, `tree-sitter-bash`
4. Remove from top-level `includes`: the Python/Ruby/Bash tree-sitter binding paths

#### Option B: Two separate config files

- `build_lambda_config.json` — focused Lambda build
- `build_jube_config.json` — polyglot build (imports/extends Lambda config)

Option A is preferred since the existing `generate_premake.py` already supports `--variant` for platform selection.

### Phase 3: Makefile Targets

```makefile
# Core Lambda (default)
make build              # Debug build — Lambda + JS/TS + Radiant
make build-release      # Release build (compile only)
make release            # Release build + prepare release artifacts

# Polyglot Jube
make build-jube         # Debug build — full polyglot
make release-jube       # Release build

# Testing — Core
make build-test         # Build lambda (core) + all test executables
make test               # Build + run lambda (core) tests (excludes jube)
make test-lambda-baseline  # Lambda baseline tests only

# Testing — Jube
make build-jube-test    # Build lambda-jube + all test executables
make test-jube          # Build + run jube-specific tests (Python, Bash, Ruby)

# Testing — All
make test-all           # Run ALL test suites (baseline + extended, including jube)
```

`test/test_run.sh` supports `--target=jube` and `--exclude-target=jube` flags for suite filtering.

### Phase 4: Serve Module Split

The serve module has language-specific backends that should be guarded:

```
lambda/serve/backend_lambda.cpp    → always compiled
lambda/serve/backend_js.cpp        → always compiled (JS is in core)
lambda/serve/backend_python.cpp    → #ifdef LAMBDA_PYTHON
lambda/serve/backend_bash.cpp      → #ifdef LAMBDA_BASH
lambda/serve/flask_compat.cpp      → #ifdef LAMBDA_PYTHON
lambda/serve/asgi_bridge.cpp       → #ifdef LAMBDA_PYTHON
lambda/serve/wsgi_bridge.py        → only deployed with jube
```

### Phase 5: Update CLI Platform Configs

The existing `cli` platform already excludes Python/Bash/Ruby/TS. Ensure consistency:

| Platform | Languages | Radiant | Serve |
|----------|-----------|---------|-------|
| `default` (lambda) | Lambda, JS, TS | Yes | Yes (Lambda+JS backends) |
| `jube` | Lambda, JS, TS, Python, Bash, Ruby | Yes | Yes (all backends) |
| `cli` | Lambda, JS | No | Yes (Lambda+JS backends) |
| `release` | Same as default | Yes | Yes |
| `release-jube` | Same as jube | Yes | Yes |

---

## Source File Inventory

### Files exclusive to Jube (not compiled in Lambda core)

**Python runtime (17 files):**
```
lambda/py/py_transpiler.cpp      lambda/py/py_transpiler.hpp
lambda/py/py_ast_builder.cpp     lambda/py/py_ast_builder.hpp
lambda/py/py_builtins.cpp        lambda/py/py_builtins.hpp
lambda/py/py_class.cpp           lambda/py/py_class.hpp
lambda/py/py_scope.cpp           lambda/py/py_scope.hpp
lambda/py/py_stdlib.cpp          lambda/py/py_stdlib.hpp
lambda/py/py_print.cpp           lambda/py/py_print.hpp
lambda/py/py_async.cpp           lambda/py/py_bigint.cpp
lambda/py/py_bigint.hpp
```

**Bash runtime (8 files):**
```
lambda/bash/bash_transpiler.cpp  lambda/bash/bash_transpiler.hpp
lambda/bash/bash_ast_builder.cpp lambda/bash/bash_ast_builder.hpp
lambda/bash/bash_builtins.cpp    lambda/bash/bash_builtins.hpp
lambda/bash/bash_runtime.c       lambda/bash/bash_runtime.h
lambda/bash/bash_scope.cpp       lambda/bash/bash_scope.hpp
```

**Ruby runtime (10 files):**
```
lambda/rb/rb_transpiler.cpp      lambda/rb/rb_transpiler.hpp
lambda/rb/rb_ast_builder.cpp     lambda/rb/rb_ast_builder.hpp
lambda/rb/rb_builtins.cpp        lambda/rb/rb_builtins.hpp
lambda/rb/rb_class.cpp           lambda/rb/rb_class.hpp
lambda/rb/rb_scope.cpp           lambda/rb/rb_scope.hpp
lambda/rb/rb_print.cpp
```

**Additional tree-sitter parsers (3 libraries):**
```
lambda/tree-sitter-python/libtree-sitter-python.a
lambda/tree-sitter-ruby/libtree-sitter-ruby.a
lambda/tree-sitter-bash/libtree-sitter-bash.a
```

**Jube-only serve backends (5 files):**
```
lambda/serve/backend_python.cpp
lambda/serve/backend_bash.cpp
lambda/serve/flask_compat.cpp    lambda/serve/flask_compat.hpp
lambda/serve/asgi_bridge.cpp     lambda/serve/asgi_bridge.hpp
lambda/serve/wsgi_bridge.py
```

**Total: ~40 source files + 3 static libraries removed from core Lambda.**

---

## Estimated Size Impact

| Build | Tree-sitter libs | Source files | Est. binary (release) |
|-------|-----------------|-------------|----------------------|
| Lambda (current) | 9 | ~350+ | ~8MB |
| Lambda (focused) | 6 | ~310 | ~6.5MB |
| Lambda Jube | 9 | ~350+ | ~8MB |

---

## Migration Checklist

- [ ] Add `LAMBDA_PYTHON` and `LAMBDA_BASH` feature flags following `LAMBDA_RUBY` pattern
- [ ] Guard Python/Bash includes and CLI handlers in `main.cpp`
- [ ] Guard language-specific registrations in `sys_func_registry.c`
- [ ] Guard serve backends with feature flags
- [ ] Remove `lambda/py`, `lambda/bash`, `lambda/rb` from top-level `source_dirs`
- [ ] Remove `LAMBDA_RUBY` from top-level `defines`
- [ ] Remove Python/Ruby/Bash tree-sitter from top-level `libraries` and `includes`
- [ ] Add `jube` platform variant to `build_lambda_config.json`
- [ ] Add `build-jube` / `release-jube` Makefile targets
- [ ] Verify `make build` produces working `lambda.exe` without polyglot languages
- [ ] Verify `make build-jube` produces working `lambda-jube.exe` with all languages
- [ ] Update test targets: ensure `test-lambda-baseline` passes on focused build
- [ ] Add `test-jube-baseline` covering Python/Bash/Ruby test suites
- [ ] Update CI pipeline (if any) to build and test both configurations
- [ ] Update `--help` output to reflect available languages per binary

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Feature flag creep makes code hard to read | Keep flags coarse-grained (per-language, not per-feature). Use `LAMBDA_JUBE` as master flag. |
| Cross-language module imports break in core Lambda | `module_registry.cpp` already resolves by file extension — unknown extensions return "unsupported language" error gracefully. |
| Serve backends reference missing language code | Each `backend_*.cpp` is already a separate file — guarding with `#ifdef` at the include level is clean. |
| Users expect all languages in `lambda.exe` | Clear naming: `lambda` = core, `lambda-jube` = polyglot. Document in `--help` and README. |
| Maintaining two configs diverges over time | Single `build_lambda_config.json` with `jube` as a platform variant (like `cli`) keeps everything in one place. |

---

## Open Questions

1. **Should `lambda/ts/` stay in core?** TypeScript transpiles down to JS and is tightly coupled to the JS runtime. Recommendation: **yes, keep in core** — TS is part of the web platform story alongside Radiant.

2. **Should `lambda/serve/` live in Jube only?** The serve module is useful for Lambda+JS alone (Express-style servers). Recommendation: **keep core serve in Lambda, polyglot backends in Jube**.

3. **Should LaTeX tree-sitter parsers stay in core?** LaTeX parsing is part of the document processing pipeline, not a language runtime. Recommendation: **yes, keep in core**.

4. **Naming alternatives?** `lambda-poly`, `lambda-all`, `lambda-multi`. "Jube" is short, memorable, and has a nice backronym.

---

## Implementation Log

### Phase 1-3: Language Runtimes Split (✅ Done)

Feature flags `LAMBDA_PYTHON`, `LAMBDA_BASH`, `LAMBDA_RUBY`, `LAMBDA_JUBE` applied across:
- `lambda/main.cpp` — guarded includes and CLI handlers
- `lambda/sys_func_registry.c` — guarded language-specific registrations
- `lambda/build_ast.cpp` — guarded `load_py_module` and Python/Bash/Ruby parser references
- `lambda/transpiler.hpp` — guarded `load_py_module` declaration

Jube variant added to `build_lambda_config.json` with additional `source_dirs`, `defines`, `libraries`, `includes`.

Makefile targets: `build-jube`, `release-jube`, `build-jube-test`, `test-jube`, `test-all`.
Test suite: `--target=jube` / `--exclude-target=jube` support in `test/test_run.sh`.

### Phase 4: C2MIR Transpiler Split (✅ Done)

Moved the legacy C2MIR JIT path (Lambda AST → C code → c2mir_compile → MIR → native) exclusively into `lambda-jube`.

**New feature flag:** `LAMBDA_C2MIR` — defined only in jube variant.

**Build config changes:**
- `transpile.cpp` and `transpile-call.cpp` excluded from core lambda via `exclude_source_files`
- New `transpile_shared.cpp` extracts shared utility functions (`write_fn_name`, `write_var_name`, `write_fn_name_ex`, `needs_fn_call_wrapper`, `has_typed_params`) needed by both transpile-mir.cpp and module_registry.cpp
- Jube variant has its own `exclude_source_files` that excludes `transpile_shared.cpp` (since functions are already in `transpile.cpp`)

**Files guarded with `#ifdef LAMBDA_C2MIR`:**
- `lambda/mir.c` — `#include "c2mir.h"`, `c2mir_init()`, `jit_compile_to_mir()`, `c2mir_finish()`
- `lambda/runner.cpp` — C2MIR branch in `transpile_script()`, `transpile_ast_root` declaration, `lambda_lambda_h_len` extern
- `lambda/main.cpp` — `--c2mir` flag parsing (3 locations), `--transpile-dir`/`--transpile-only` flags, C2MIR branches in `run_repl()` and `run_script_file()`, help text
- `lambda/main-repl.cpp` — `print_help()` C2MIR references
- `lambda/transpiler.hpp` — `jit_compile_to_mir()` declaration

**Tests moved to jube suite:**
- `test_c2mir_gtest` — C2MIR JIT execution tests
- `test_transpile_patterns_gtest` — transpiled C code pattern verification
- `test_lambda_helpers.hpp` updated to use `lambda-jube.exe` for C2MIR test runs

### Phase 5: Release Debug Dump Cleanup (✅ Done)

Removed debug/diagnostic side-channel file writes from release builds:

| File | Change |
|------|--------|
| `radiant/view_pool.cpp` `print_view_tree()` | `./view_tree.txt` and `./test_output/view_tree_*.txt` writes wrapped in `#ifndef NDEBUG` |
| `radiant/view_pool.cpp` `print_view_tree_json()` | `./test_output/view_tree_*.json` write wrapped in `#ifndef NDEBUG`; default output changed from `/tmp/view_tree.json` to `./temp/view_tree.json` (only in debug) |
| `lambda/shape_pool.cpp` `shape_pool_print_stats()` | Replaced `printf()` with `log_debug()`, wrapped in `#ifndef NDEBUG` |
| `radiant/ui_context.cpp` | Replaced diagnostic `printf()` for framebuffer/viewport info with `log_info()` |

Already correctly guarded (no changes needed):
- MIR text dumps in `transpile-mir.cpp` — already `#ifndef NDEBUG`
- Phase profile in `runner.cpp` — gated by `LAMBDA_PROFILE=1` env var
- `_transpiled*.c` file writes — gated by `--transpile-dir` CLI flag or dev mode
- AST dumps via `log_debug()` — controlled by log system
- render_dvi.cpp math dumps — gated by CLI `dump_ast`/`dump_boxes` flags

### Phase 6: Remaining Debug/Dump Code Audit

Full audit of all debug/diagnostic output in `lambda/` and `radiant/` that should be switched off for release.

#### Unguarded Debug Output Cleanup (✅ Done)

| #   | File                     | Line(s)   | Change |
| --- | ------------------------ | --------- | ------ |
| 1   | `lambda/runner.cpp`      | 455       | `print_ts_root()` wrapped in `#ifndef NDEBUG` — was traversing entire CST unconditionally |
| 2   | `lambda/mir.c`           | 162       | Removed hardcoded `enable_debug = true;` — now controlled by `LAMBDA_C2MIR_DEBUG` env var |
| 3   | `lambda/main.cpp`        | 2180–2182 | Moved temp SVG from `/tmp/` to `./temp/`; debug copy gated with `#ifndef NDEBUG` |
| 4   | `radiant/ui_context.cpp` | 87        | `printf("Running in headless mode")` → `log_info()` |
| 5   | `radiant/ui_context.cpp` | 121       | `printf("Scale Factor")` → merged into existing `log_info()` |
| 6   | `radiant/render_img.cpp` | 75        | `printf("Successfully saved PNG")` → `log_info()` |
| 7   | `radiant/render_img.cpp` | 138       | `printf("Successfully saved JPEG")` → `log_info()` |
| 8   | `radiant/surface.cpp`    | 301       | `printf("Cleaning up cached images")` → `log_debug()` |
| 9   | `radiant/event_sim.cpp`  | 2513      | Removed duplicate `fprintf(stderr)` — already logged via `log_info()` |
| 10  | `radiant/pdf/pages.cpp`  | 225       | `fprintf(stderr, "Found Page node...")` → `log_debug()` |
| 11  | `radiant/pdf/pages.cpp`  | 351       | Removed duplicate `fprintf(stderr)` — merged info into `log_info()` |

#### Dead Code — Unused Print Functions (✅ Done)

These files contain AST/debug print functions that have **no external callers**. They used raw `printf()` and would pollute stdout if ever called. Now gated with `#ifndef NDEBUG` — entire file contents excluded from release builds.

| File | Functions | Guard |
|------|-----------|-------|
| `lambda/rb/rb_print.cpp` | `print_rb_ast_node()`, `print_rb_ast_root()` | Whole file wrapped in `#ifndef NDEBUG` |
| `lambda/py/py_print.cpp` | `print_py_ast_node()`, `print_py_ast_root()` | Whole file wrapped in `#ifndef NDEBUG` |
| `lambda/js/js_print.cpp` | `print_js_ast_node()`, `print_js_ast_root()` | Whole file wrapped in `#ifndef NDEBUG` |
| `lambda/input/css/css_style_node.cpp` | `style_tree_print()`, `style_node_print_cascade()`, `css_specificity_print()` | Function bodies wrapped in `#ifndef NDEBUG` |

#### Already Properly Guarded (No Changes Needed)

**MIR text dumps** — all guarded with `#ifndef NDEBUG`:

| File | Line | Output file | Extra guard |
|------|------|-------------|-------------|
| `lambda/transpile-mir.cpp` | ~11248 | `temp/mir_dump.txt` | — |
| `lambda/js/transpile_js_mir.cpp` | ~18563 | `temp/ts_mir_dump.txt` | `JS_MIR_DUMP` env var |
| `lambda/js/transpile_js_mir.cpp` | ~18788 | `temp/js_mir_dump.txt` | `JS_MIR_DUMP` env var |
| `lambda/py/transpile_py_mir.cpp` | ~7525 | `temp/py_mir_dump.txt` | — |
| `lambda/rb/transpile_rb_mir.cpp` | ~3663 | `temp/rb_mir_dump.txt` | — |
| `lambda/bash/transpile_bash_mir.cpp` | ~4964 | `temp/bash_mir_dump.txt` | — |

**View tree dumps** — guarded with `#ifndef NDEBUG`:
- `radiant/view_pool.cpp` `print_view_tree()` → `./view_tree.txt`, `./test_output/view_tree_*.txt`
- `radiant/view_pool.cpp` `print_view_tree_json()` → `./temp/view_tree.json`, `./test_output/view_tree_*.json`

**Shape pool stats** — guarded with `#ifndef NDEBUG`:
- `lambda/shape_pool.cpp` `shape_pool_print_stats()` → `log_debug()`

**Phase profile** — runtime-gated:
- `lambda/runner.cpp` `profile_dump_to_file()` → `temp/phase_profile.txt` — gated by `LAMBDA_PROFILE=1` env var

**Transpiled C file writes** — CLI-gated:
- `_transpiled*.c` file writes — gated by `--transpile-dir` CLI flag

**Lambda AST dump** — feature-flag-gated:
- `lambda/runner.cpp:534` `print_ast_root()` — inside `#ifdef LAMBDA_C2MIR` block (jube-only)

**Tree-sitter CST / Lambda AST print functions** — log-level-gated:
- `lambda/print.cpp` `print_ts_node()`/`print_ts_root()` — outputs via `log_debug()`
- `lambda/print.cpp` `print_ast_node()`/`print_ast_root()` — outputs via `log_debug()`

**Math rendering dumps** — CLI-flag-gated:
- `radiant/render_dvi.cpp` — `fprintf(stderr, "=== Math AST ===...")` gated by `dump_ast` CLI flag
- `radiant/render_dvi.cpp` — `fprintf(stderr, "=== TexNode Box Structure ===...")` gated by `dump_boxes` CLI flag

**Error output (acceptable)** — uses `fprintf(stderr, ...)` for critical initialization failures:
- `radiant/ui_context.cpp:70` — FreeType init failure
- `radiant/ui_context.cpp:101,108` — GLFW init / window creation failure
- `radiant/render_dvi.cpp` — math parse/typeset/render errors (CLI tool)
- `lambda/build_ast.cpp:7490,7586` — module import errors

**CLI help text (intentional)**:
- `radiant/radiant.cpp:18-22` — `printf()` for `--help` output
- `radiant/webdriver/cmd_webdriver.cpp:27-62` — `printf()` for `--help` output
