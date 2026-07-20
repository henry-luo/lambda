# Lambda Jube: Polyglot Runtime

Lambda ships as two build configurations from a single codebase:

| Binary | Description |
|--------|-------------|
| `lambda.exe` | **Core** — Lambda + JavaScript/TypeScript + Radiant (HTML/CSS/SVG layout) |
| `lambda-jube.exe` | **Polyglot** — everything in core, plus Python, Bash, and Ruby |

## Why Two Builds?

Lambda's core value is functional scripting, document processing, and the Radiant layout engine. JavaScript and TypeScript are tightly coupled to Radiant (DOM, CSSOM, event loop) and ship with core.

Python, Bash, and Ruby are independent language runtimes that don't interact with Radiant. Bundling them into every build adds ~40 source files, 3 tree-sitter parsers, and several megabytes to the binary. Splitting them out keeps the core build focused and fast.

## Why "Jube"?

- **Joy & jubilee** — programming should be joyful.
- **Vibe** — vibe coding in a polyglot runtime.
- **Candy metaphor** — a jube is a small, self-contained sweet. Each language runtime is a sandboxed "jube" bundled into one binary. In the same spirit as Java Bean, Ruby Gem, and Docker Pod.
- **Backronym** — **J**avaScript, r**U**by, **B**ash, Typ**E**script.

---

## Architecture

### What's in Each Build

**Lambda (core) — `lambda.exe`**

- Lambda functional scripting runtime
- JavaScript engine (`lambda/js/`)
- TypeScript transpiler (`lambda/ts/`)
- Document input pipeline (JSON, XML, HTML, Markdown, YAML, LaTeX, PDF, CSV, TOML, …)
- Output formatters (JSON, HTML, Markdown, YAML, …)
- Schema validation (`lambda/validator/`)
- Radiant CSS layout engine (`radiant/`)
- Serve module with Lambda and JS backends
- MIR Direct JIT (`transpile-mir.cpp` → `mir.c`)
- Tree-sitter parsers: lambda, javascript, typescript, latex, latex-math

**Lambda Jube (polyglot) — `lambda-jube.exe`**

Everything in core, plus:

- Python runtime (`lambda/py/` — 17 files)
- Bash runtime (`lambda/bash/` — 8 files)
- Ruby runtime (`lambda/rb/` — 10 files)
- Serve backends for Python and Bash (`backend_python.cpp`, `flask_compat.*`, `asgi_bridge.*`)
- Tree-sitter parsers: python, ruby, bash

### Feature Flags

The split is implemented with compile-time feature flags:

| Flag | Scope | Guards |
|------|-------|--------|
| `LAMBDA_PYTHON` | Jube only | Python runtime, imports, CLI handler |
| `LAMBDA_BASH` | Jube only | Bash runtime, imports, CLI handler |
| `LAMBDA_RUBY` | Jube only | Ruby runtime, imports, CLI handler |
| `LAMBDA_JUBE` | Jube only | Polyglot runtime integration |

The flags follow a consistent pattern across the codebase:

```cpp
// lambda/main.cpp
#ifdef LAMBDA_PYTHON
#include "py/py_transpiler.hpp"
#endif

#ifdef LAMBDA_BASH
#include "bash/bash_transpiler.hpp"
#include "bash/bash_runtime.h"
#endif

#ifdef LAMBDA_RUBY
#include "rb/rb_transpiler.hpp"
#endif
```

Files guarded:
- `lambda/main.cpp` — language includes and CLI command handlers
- `lambda/sys_func_registry.c` — language-specific function registrations
- `lambda/build_ast.cpp` — `load_py_module` and parser references
- `lambda/transpiler.hpp` — `jit_compile_to_mir()` and `load_py_module` declarations
- `lambda/module_registry.cpp` — cross-language import helpers
- `lambda/serve/language_backend.cpp` — conditional backend registration

### Build Configuration

Both builds are defined in a single `build_lambda_config.json`. The Jube variant extends the default config with additional source dirs, defines, and libraries:

```jsonc
// build_lambda_config.json (jube platform variant)
{
    "platforms": {
        "jube": {
            "output": "lambda-jube.exe",
            "source_dirs": ["lambda/py", "lambda/bash", "lambda/rb"],
            "defines": ["LAMBDA_JUBE", "LAMBDA_PYTHON", "LAMBDA_BASH", "LAMBDA_RUBY"],
            "exclude_source_files": ["lambda/transpile.cpp", "lambda/transpile-call.cpp"],
            "additional_libraries": [
                {"name": "tree-sitter-python", "lib": "lambda/tree-sitter-python/libtree-sitter-python.a"},
                {"name": "tree-sitter-ruby",   "lib": "lambda/tree-sitter-ruby/libtree-sitter-ruby.a"},
                {"name": "tree-sitter-bash",   "lib": "lambda/tree-sitter-bash/libtree-sitter-bash.a"}
            ]
        }
    }
}
```

The top-level config excludes `lambda/py`, `lambda/bash`, `lambda/rb` from `source_dirs` and does not define any language flags. Both builds exclude the retired `transpile.cpp` and `transpile-call.cpp` C2MIR backend, using `transpile_shared.cpp` for utilities shared with MIR Direct.

---

## Build & Test Commands

### Core Lambda

```bash
make build                  # Debug build → lambda.exe
make release                # Release build → release/lambda.exe
make build-test             # Build core + test executables
make test                   # Run core tests (baseline + extended)
make test-lambda-baseline   # Lambda baseline only (must pass 100%)
make test-radiant-baseline  # Radiant baseline only (must pass 100%)
```

### Lambda Jube

```bash
make build-jube             # Debug build → lambda-jube.exe
make release-jube           # Release build → release/lambda-jube.exe
make build-jube-test        # Build jube + test executables
make test-jube              # Run jube-specific tests (Python, Bash, Ruby)
make test-all               # Run ALL test suites (core + jube)
```

### CLI Differences

Core `lambda.exe`:
```bash
lambda script.ls              # Run Lambda script (MIR Direct JIT)
lambda run script.ls          # Run procedural script
lambda view page.html         # Open in Radiant viewer
lambda convert input.json -t yaml -o output.yaml
```

Jube `lambda-jube.exe` adds:
```bash
lambda-jube py script.py      # Run Python script
lambda-jube bash script.sh    # Run Bash script
lambda-jube rb script.rb      # Run Ruby script
```

---

## JIT Compilation

### MIR Direct (core — default)

```
Lambda AST → transpile-mir.cpp → MIR API calls → MIR optimize → native code
```

The primary JIT path. Translates Lambda AST directly into MIR intermediate representation via API calls. Faster compilation, no intermediate files, no C parsing overhead. Available in both `lambda.exe` and `lambda-jube.exe`.

### Retired C2MIR source

```
Lambda AST → transpile.cpp → C source code → c2mir_compile() → MIR → native code
```

This was the original JIT path. Its source remains in the repository as legacy
reference code, but it is excluded from core and Jube build configurations and
from the generated test projects. `--c2mir` is therefore not an available
backend in supported binaries; numeric and runtime changes are gated on MIR
Direct only.

---

## Estimated Binary Sizes

| Build | Tree-sitter Parsers | Est. Release Size |
|-------|:-------------------:|:-----------------:|
| Lambda (core) | 6 | ~11 MB |
| Lambda Jube | 9 | ~16 MB |

---

## Platform Matrix

| Platform | Lambda | JS/TS | Radiant | Python | Bash | Ruby |
|----------|:------:|:-----:|:-------:|:------:|:----:|:----:|
| `default` (lambda.exe) | ✅ | ✅ | ✅ | — | — | — |
| `jube` (lambda-jube.exe) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `cli` | ✅ | ✅ | — | — | — | — |
