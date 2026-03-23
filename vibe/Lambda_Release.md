# Lambda Release Packaging — Design & Implementation Proposal

## 1. Overview

This proposal covers the changes needed to package Lambda for distribution. There are five
concerns to address:

1. **Binary naming** — rename `lambda.exe` to `lambda` on macOS and Linux.
2. **Runtime asset directory** — copy `./lambda/` runtime assets (packages, input scripts,
   CSS/fonts) to `./lmd/` in the release image.
3. **Configurable lambda home path** — replace all hardcoded `"lambda/…"` paths in the engine
   with a runtime-configurable `g_lambda_home`, defaulting to `"./lambda"` in dev and `"./lmd"`
   in the release binary.
4. **LaTeX CSS and font path handling** — ensure `cmd_layout.cpp` uses `g_lambda_home`.
5. **Schema validation path handling** — ensure `ast_validate.cpp` uses `g_lambda_home`.

---

## 2. Binary Naming (macOS / Linux)

Currently `prepare_release.sh` copies `lambda.exe` to `./release/` with that name. Executable
files on POSIX systems do not have an `.exe` extension.

**Change to `prepare_release.sh`:**

```bash
# After building the release binary, copy with platform-appropriate name
if [[ "$OSTYPE" == "darwin"* ]] || [[ "$OSTYPE" == "linux-gnu"* ]]; then
    cp ./lambda.exe ./release/lambda
    echo "    Copied lambda.exe as release/lambda"
else
    cp ./lambda.exe ./release/lambda.exe
    echo "    Copied lambda.exe to release/"
fi
```

The existing `LAMBDA_EXE` macro in test sources already handles the platform difference
(`./lambda.exe` on Windows, `./lambda` elsewhere), so no test changes are needed for the
development build.

---

## 3. Runtime Asset Directory (`./lambda/` → `./lmd/`)

The directory is renamed from `lambda` to `lmd` to avoid a **name clash with the `lambda`
executable itself**. On macOS and Linux the binary is installed as `lambda` (no `.exe`
extension). If the runtime-asset directory were also named `lambda`, any shell or tool that
uses tab-completion, `PATH` lookup, or glob expansion in the installation directory would see
an ambiguous collision between the file `lambda` (the binary) and the directory `lambda/`
(the assets). Using `lmd` (short for *Lambda Module Directory*) keeps the two artefacts
clearly distinct and avoids this class of problem entirely.

In the release image the `./lambda/` source directory should not be present — it contains
source files, the grammar, headers, and other non-runtime material. Only the runtime assets
need to be shipped:

| Source (dev build) | Release image | Contents |
|--------------------|---------------|----------|
| `lambda/package/` | `lmd/package/` | Lambda Script packages (chart, latex, math) |
| `lambda/input/*.ls` | `lmd/input/*.ls` | Built-in input format scripts |
| `lambda/input/*.css` | `lmd/input/*.css` | Built-in input CSS |
| `lambda/input/latex/css/` | `lmd/input/latex/css/` | KaTeX + article CSS |
| `lambda/input/latex/fonts/` | `lmd/input/latex/fonts/` | KaTeX + CMU fonts |

**Change to `prepare_release.sh` Step 2:**

Replace every occurrence of `./release/lambda/` with `./release/lmd/` and every source reference
`./lambda/` with the same source path (dev files do not move) but copy to the `lmd` destination:

```bash
# Step 2: Copy Lambda runtime assets into release/lmd/
mkdir -p ./release/lmd/input

cp ./lambda/input/*.ls  ./release/lmd/input/ 2>/dev/null || true
cp ./lambda/input/*.css ./release/lmd/input/ 2>/dev/null || true

mkdir -p ./release/lmd/input/latex/css
cp ./lambda/input/latex/css/*.css ./release/lmd/input/latex/css/

rm -rf ./release/lmd/input/latex/fonts
cp -r  ./lambda/input/latex/fonts ./release/lmd/input/latex/fonts

rm -rf ./release/lmd/package
cp -r  ./lambda/package ./release/lmd/package
```

---

## 4. Configurable Lambda Home Path (`g_lambda_home`)

### 4.1 Problem

Several files in the engine hardcode `"lambda/input/…"` or resolve imports from a directory
named `"lambda"`:

| File | Hardcoded string |
|------|-----------------|
| `lambda/build_ast.cpp` (absolute import) | `./lambda/package/…` (implicit: dots → slashes from `./`) |
| `lambda/validator/ast_validate.cpp` | `"lambda/input/html5_schema.ls"`, `"lambda/input/eml_schema.ls"`, etc. |
| `radiant/cmd_layout.cpp` | `"lambda/input/markdown.css"`, `"lambda/input/wiki.css"`, `"lambda/input/latex/css/article.css"`, `"lambda/input/latex/css/katex.css"` |

Lambda Script source files use the canonical notation `import lambda.package.chart.chart`,
which the absolute-import resolver maps to `./lambda/package/chart/chart.ls`. In the release
image the directory is `./lmd/`, breaking all such imports.

### 4.2 Solution — `g_lambda_home` global

Add a single global in `lambda/runner.cpp` (or a new `lambda/lambda-env.cpp`):

```c
// lambda_home is the directory containing Lambda's runtime assets (package/, input/).
// Dev default: "./lambda"   Release default: "./lmd"
// Can be overridden via the LAMBDA_HOME environment variable.
const char* g_lambda_home = "./lambda";

void lambda_home_init(void) {
    const char* env = getenv("LAMBDA_HOME");
    if (env && env[0]) {
        g_lambda_home = env;   // env var takes priority
        return;
    }
    // Future: auto-detect from executable path
}
```

Declare in `lambda/transpiler.hpp` (or `lambda/lambda.h`):

```c
extern const char* g_lambda_home;   // e.g. "./lambda" or "./lmd"
void lambda_home_init(void);
```

### 4.3 Initialisation in `main.cpp`

Call `lambda_home_init()` early in `main()`, before any `runtime_init` or import resolution:

```c
int main(int argc, char** argv) {
    lambda_home_init();    // ← add this line
    // ... existing init ...
}
```

For the release binary, pass `LAMBDA_HOME=./lmd` at install time (e.g. via a wrapper script),
or set `g_lambda_home = "./lmd"` as the compile-time default in release builds via a build flag:

```makefile
# In the release Premake config / Makefile:
CXXFLAGS += -DLAMBDA_HOME_DEFAULT='"./lmd"'
```

```c
#ifdef LAMBDA_HOME_DEFAULT
const char* g_lambda_home = LAMBDA_HOME_DEFAULT;
#else
const char* g_lambda_home = "./lambda";
#endif
```

### 4.4 Helper function

Add a small helper (inline or in `lambda-env.cpp`) to build a full path into `g_lambda_home`:

```c
// Build a path relative to g_lambda_home.
// Returns a malloc'd string; caller must free.
// Example: lambda_home_path("input/html5_schema.ls") → "./lambda/input/html5_schema.ls"
static inline char* lambda_home_path(const char* rel) {
    size_t home_len = strlen(g_lambda_home);
    size_t rel_len  = strlen(rel);
    char* out = (char*)malloc(home_len + 1 + rel_len + 1);
    if (!out) return NULL;
    memcpy(out, g_lambda_home, home_len);
    out[home_len] = '/';
    memcpy(out + home_len + 1, rel, rel_len + 1);
    return out;
}
```

---

## 5. LaTeX CSS and Font Path Handling

**File:** `radiant/cmd_layout.cpp`

Three CSS paths are hardcoded and need to use `g_lambda_home`:

```
"lambda/input/markdown.css"          → lambda_home_path("input/markdown.css")
"lambda/input/wiki.css"              → lambda_home_path("input/wiki.css")
"lambda/input/latex/css/article.css" → lambda_home_path("input/latex/css/article.css")
"lambda/input/latex/css/katex.css"   → lambda_home_path("input/latex/css/katex.css")
```

The `import_base_dir` assignments in `cmd_layout.cpp` and `main.cpp` set the root from which
relative package imports resolve. They must also be set to the **parent directory** of
`g_lambda_home` (usually `"./"`), which is already what they do. No change needed there unless
`g_lambda_home` is an absolute path — in that case, compute the parent dynamically.

**Concrete change pattern:**

```cpp
// Before:
const char* css_filename = "lambda/input/markdown.css";
char* css_content = read_text_file(css_filename);

// After:
char* css_filename = lambda_home_path("input/markdown.css");
char* css_content = read_text_file(css_filename);
// ... use css_filename ...
free(css_filename);
```

Font files are resolved by the CSS engine via `@font-face src:` URLs that are relative to the
CSS file's `origin_url`. As long as the CSS files are loaded from the correct `g_lambda_home`
path (step above), the font resolution in `radiant/font_face.cpp` automatically works without
further changes — it already uses `stylesheet->origin_url` as the base for relative font URLs.

---

## 6. Schema Validation Path Handling

**File:** `lambda/validator/ast_validate.cpp`

Five schema file paths are hardcoded:

```
"lambda/input/html5_schema.ls"   → lambda_home_path("input/html5_schema.ls")
"lambda/input/eml_schema.ls"     → lambda_home_path("input/eml_schema.ls")
"lambda/input/ics_schema.ls"     → lambda_home_path("input/ics_schema.ls")
"lambda/input/vcf_schema.ls"     → lambda_home_path("input/vcf_schema.ls")
"lambda/input/doc_schema.ls"     → lambda_home_path("input/doc_schema.ls")
```

Each is assigned to a `const char* schema_file` local variable and later passed to
`run_script_at()`. Replace each literal with a `lambda_home_path()` call and free after use:

```cpp
// Before:
schema_file = "lambda/input/html5_schema.ls";

// After:
schema_file = lambda_home_path("input/html5_schema.ls");
needs_free  = true;   // free schema_file at end of function
```

---

## 7. Package Import Resolution (`build_ast.cpp`)

**Current behaviour** for absolute imports (no leading dot):

```
import lambda.package.chart.chart
  → strbuf_append_format(buf, "./%.*s", module_length, module_str)
  → replace '.' with '/'
  → append ".ls"
  → result: ./lambda/package/chart/chart.ls
```

The first path component `lambda` maps directly to the `./lambda` directory. When the release
uses `./lmd`, this breaks.

**Proposed fix:** After constructing the initial path `./lambda/package/chart/chart.ls`, replace
the `lambda` component with the last path component of `g_lambda_home`:

```cpp
// In the absolute-import block, after building the initial buf:
// Replace the first path segment (after "./") with the basename of g_lambda_home.
// e.g. if g_lambda_home = "./lmd", buf = "./lambda/package/..." → "./lmd/package/..."
const char* home = g_lambda_home;
// skip leading "./"
if (home[0] == '.' && home[1] == '/') home += 2;
size_t home_len = strlen(home);
// the generated path always starts with "./" followed by the original first segment
// find the end of the first segment (stop at '/')
char* segment_end = strchr(buf->str + 2, '/');
if (segment_end) {
    // replace "./lambda" with "./lmd" (or whatever g_lambda_home's basename is)
    StrBuf* fixed = strbuf_new();
    strbuf_append_str(fixed, "./");
    strbuf_append_len(fixed, home, home_len);
    strbuf_append_str(fixed, segment_end); // rest of path unchanged
    strbuf_free(buf);
    buf = fixed;
}
```

This preserves backward compatibility: scripts written with `import lambda.package.*` continue
to work in both dev and release without any source changes.

---

## 8. File Changes Summary

| File | Change |
|------|--------|
| `utils/prepare_release.sh` | Rename binary to `lambda` on macOS/Linux; copy assets to `./release/lmd/` |
| `lambda/transpiler.hpp` (or `lambda/lambda.h`) | Declare `extern const char* g_lambda_home` and `lambda_home_init()` |
| `lambda/runner.cpp` (or new `lambda/lambda-env.cpp`) | Define `g_lambda_home`, implement `lambda_home_init()`, implement `lambda_home_path()` |
| `lambda/main.cpp` | Call `lambda_home_init()` at startup |
| `lambda/build_ast.cpp` | Replace first path segment in absolute imports with `g_lambda_home` basename |
| `lambda/validator/ast_validate.cpp` | Replace hardcoded `"lambda/input/…"` schema paths |
| `radiant/cmd_layout.cpp` | Replace hardcoded `"lambda/input/…"` CSS paths |

---

## 9. Testing

After the changes:

1. **Dev build** — run `make test-lambda-baseline` and `make test-radiant-baseline`. All tests
   must still pass with the default `g_lambda_home = "./lambda"`.
2. **Release simulation** — temporarily set `LAMBDA_HOME=./lmd` (after running
   `utils/prepare_release.sh` to populate `./release/lmd/`) and run a script that imports a
   package:

   ```bash
   LAMBDA_HOME=./lmd ./lambda.exe test.ls   # test a package import
   ./lambda.exe layout test/html/live-demo.html   # test KaTeX fonts + CSS
   ./lambda.exe validate test/input/test.xml       # test schema paths
   ```

3. **Release binary** — run `./release/lambda` (or `./release/lambda.exe` on Windows) and
   verify that packages, schemas, and CSS all load correctly from `./lmd/`.

---

## 10. Open Questions

- **Absolute path for `g_lambda_home`** — if Lambda is installed to `/usr/local/lib/lambda`, the
  `"./lmd"` relative path won't work. A future step can auto-detect the install prefix from the
  executable path (`argv[0]`).
- **`import lambda.*` namespace vs. `import lmd.*`** — the proposal keeps `lambda` as the
  canonical module namespace in Lambda scripts (always write `import lambda.package.*`) and
  maps it to `g_lambda_home` at resolution time. An alternative is to make the first segment
  a configurable alias, but that adds complexity for no real benefit.
- **Windows** — `lambda.exe` naming is kept as-is on Windows. The `lambda_home_path()` helper
  should use platform path separators if needed; currently Lambda always uses `/` internally
  (consistent with the rest of the codebase).
