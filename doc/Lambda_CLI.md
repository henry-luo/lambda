# Lambda CLI Reference

The command-line interface of the Lambda runtime. In a development tree the binary is `./lambda.exe`; release bundles ship it as `lambda`, which is the spelling used below.

## Synopsis

```
lambda                                      # Start interactive REPL
lambda <script.ls> [options]                # Run a functional script
lambda <command> [options] [arguments]      # Run a subcommand
lambda js [file.js] [options]               # Run JavaScript
lambda ts <file.ts>                         # Run TypeScript
```

## Default Behavior

When invoked with no arguments, Lambda starts the **REPL** (Read-Eval-Print Loop).

When invoked with a `.ls` script file (and no subcommand), Lambda compiles and executes the script: it starts on the AST interpreter and compiles hot functions with the MIR JIT (`--tier` selects a tier explicitly). Run JavaScript with `lambda js <file.js>` and TypeScript with `lambda ts <file.ts>`.

---

## Global Options

These options apply when running a script directly (i.e., `lambda <script.ls>`).

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| | `--max-errors N` | Max type errors before stopping (0 = unlimited) | `10` |
| | `--optimize=N` / `--opt-level=N` | MIR optimization level; large modules may automatically use the interpreter | `2` |
| `-O0` … `-O3` | | Optimization level shorthand: 0 = debug with stack traces, 1 = basic, 2 = full, 3 = aggressive | |
| | `--tier=auto\|jit\|interp` | Execution tier: `auto` interprets and compiles hot functions, `jit` compiles everything, `interp` never compiles. `LAMBDA_EXEC_BACKEND` is the environment equivalent | `auto` |
| | `--mir-interp` | Run the JIT's output on the MIR interpreter instead of native code | |
| | `--dry-run` | Skip real I/O; return fabricated results for network/filesystem operations | `false` |
| | `--static-warning` | Relaxed mode: report static type errors as warnings and keep running (syntax errors still fail; the result may contain error values) | |
| | `--no-drain` | Return without draining spawned tasks | |

`--help` (`-h`) is recognized only as the first argument: `lambda --help`.

### Optimization Levels

| Level | Description |
|-------|-------------|
| `0` | Debug mode with stack trace support |
| `1` | Basic optimizations |
| `2` | Full optimizations (default) |
| `3` | Aggressive optimizations |

---

## Diagnostic Flags (Global)

These flags are recognized **before** any subcommand and stripped from the argument list, so they work in every mode (REPL, script, `run`, `convert`, `layout`, `render`, …).

| Flag | Description |
|------|-------------|
| `--no-log` | Disable all logging for this invocation |
| `--mem-dump[=PATH]` | On process exit, write a JSON snapshot of the memory context and log a leak report. `PATH` defaults to `./temp/mem_snapshot.json` |

### Memory Dump (`--mem-dump`)

Lambda centralizes pool/arena/heap allocation under a single rooted **memory context** (see [`vibe/Memory_Context.md`](../vibe/Memory_Context.md)). `--mem-dump` captures that context at process exit and produces:

- **A JSON snapshot** — a flat array of allocator nodes, each with `id`, `parent_id` (its backing allocator), `doc_id`, `kind` (`pool` / `arena` / `heap` / `nursery` / `cache` / `jit` / …), `role` (`input` / `ast` / `view` / `render` / `font` / …), `label`, and `bytes_reserved` / `bytes_in_use` / `alloc_count`.
- **A leak report** — logged to `./log.txt` with the `MEMCTX-LEAK:` prefix, listing every allocator still live at exit (with its role, label, and size).

```bash
lambda --mem-dump script.ls                          # dump to ./temp/mem_snapshot.json
lambda --mem-dump=./temp/mem.json layout page.html   # dump to a custom path
```

Example snapshot:

```json
{
  "captured_seq": 3, "count": 2,
  "total_reserved": 60507, "total_in_use": 60507,
  "nodes": [
    { "id": 2, "parent_id": 0, "doc_id": 0, "kind": "pool", "role": "input",
      "label": "input.global_pool", "bytes_reserved": 55835, "bytes_in_use": 55835, "alloc_count": 392 },
    { "id": 3, "parent_id": 0, "doc_id": 0, "kind": "pool", "role": "ast",
      "label": "script.result", "bytes_reserved": 4672, "bytes_in_use": 4672, "alloc_count": 5 }
  ]
}
```

**Notes:**

- Place the flag **before** the subcommand — it is parsed globally and stripped from `argv`.
- The snapshot reflects allocators still alive at the exit cleanup point; transient pools created and freed during the run are correctly **absent**.
- The `convert` subcommand returns before the shared exit path, so its dump is empty (the conversion itself still completes normally). Script mode, `layout`, and `render` reach the exit hook.

---

## Commands

### `run` — Run a Procedural Script

Executes a Lambda script with `main()` procedure entry point. If `main()`
returns a non-null value, that value is printed to stdout.

```
lambda run [options] <script.ls>
```

**Options:** `--dry-run`, `--no-drain`, `--static-warning`, `--mir-interp` and `--tier=` as in script mode. `--max-errors`, `--optimize` and `-O*` are **not** accepted by `run`.

Without a script, `lambda run` starts a procedural REPL session (see [REPL](#repl)).

**Example:**

```bash
lambda run script.ls
```

---

### `validate` — Validate Data Against a Schema

Validates a data file against a Lambda schema — one file per invocation.

```
lambda validate [-s <schema>] [-f <format>] [options] <file>
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-s <schema>` | | Schema file (`.ls`) | Auto-selected based on format |
| `-f <format>` | | Input format | Auto-detect from extension |
| | `--strict` | Accepted, but has no effect yet | `false` |
| | `--max-errors N` | Stop after N errors | `100` |
| | `--max-depth N` | Maximum validation depth for nested structures | `100` |
| | `--allow-unknown` | Accepted, but has no effect: map types are open, so undeclared fields always pass | `false` |
| `-h` | `--help` | Show help | |

**Input formats.** Auto-detected from the extension: `.json`, `.csv`, `.ini`, `.toml`, `.yaml`/`.yml`, `.xml`, `.md`/`.markdown`, `.rst`, `.html`/`.htm`, `.wiki`, `.adoc`/`.asciidoc`, `.1`–`.9` (man pages), `.eml`, `.ics`, `.vcf`, `.textile`/`.txtl`, `.mark`. Formats without an auto-detected extension take `-f`: `latex`, `rtf`, `pdf`, `text`.

Mark auto-detection recognizes only `.mark` (D2.9.1); use `-f mark` for
arbitrary filenames, including files without an extension (D2.9.2).

**Built-in schemas** (no `-s` needed):

| Format | Default Schema |
|--------|---------------|
| `html` | `html5_schema.ls` |
| `eml` | `eml_schema.ls` |
| `ics` | `ics_schema.ls` |
| `vcf` | `vcf_schema.ls` |
| `asciidoc`, `man`, `markdown`, `rst`, `textile`, `wiki` | `doc_schema.ls` |
| `.ls` files | Built-in AST validation |

Formats such as `json`, `xml`, `yaml`, `csv`, `ini`, `toml`, `latex`, `rtf`, `pdf`, and `text` require an explicit schema via `-s`. With a custom schema the **root type** is the type named `Document` if the schema defines one, otherwise the last type defined in the file. See [Lambda_Validator.md](Lambda_Validator.md).

**Examples:**

```bash
lambda validate data.json -s schema.ls
lambda validate page.html
lambda validate --strict config.yaml -s config_schema.ls --max-errors 50
```

---

### `convert` — Format Conversion

Convert data between supported formats.

```
lambda convert <input> [-f <from>] -t <to> -o <output> [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-f <from>` | `--from` | Input format | Auto-detect |
| `-t <to>` | `--to` | Output format (**required**) | |
| `-o <output>` | `--output` | Output file path (**required**) | |
| | `--full-document` | For LaTeX→HTML: generate complete HTML document with CSS | `false` |
| | `--view-key <key>` | Select a named view when the source defines several | |
| | `--font-option default\|katex` | Math font set for LaTeX→HTML output | `default` |
| | `--pipeline legacy\|unified` | Accepted for compatibility; currently has no effect | |
| `-h` | `--help` | Show help | |

**Supported output formats:**

`mark`, `json`, `xml`, `html`, `yaml`, `toml`, `ini`, `css`, `jsx`, `mdx`, `latex`, `rst`, `org`, `wiki`, `textile`, `text`, `markdown`/`md`, `math-ascii`, `math-latex`, `math-typst`, `math-mathml`, `properties`

The input format flag `-f` supports colon-separated `type:flavor` syntax (e.g., `graph:mermaid`).

**Examples:**

```bash
lambda convert input.json -t yaml -o output.yaml
lambda convert doc.md -t html -o doc.html
lambda convert formula.tex -t html -o formula.html --full-document
lambda convert data.xml -f xml -t json -o data.json
```

---

### `layout` — HTML/CSS Layout Analysis

Run the CSS layout engine and output the computed layout tree.

```
lambda layout <file> [more files...] [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-o` | `--output FILE` | Output file for layout results | stdout |
| | `--output-dir DIR` | Output directory for batch mode (required for multiple files) | |
| | `--view-output FILE` | Custom output path for `view_tree.json` (single file mode) | |
| `-c` | `--css FILE` | External CSS file to apply (HTML only) | |
| `-vw` | `--viewport-width WIDTH` | Viewport width in pixels | `1200` |
| `-vh` | `--viewport-height HEIGHT` | Viewport height in pixels | `800` |
| | `--stream-layout-results` | Batch mode: stream each result to stdout instead of writing `--output-dir` | |
| | `--css-at-head-end` | Apply `--css` at the end of `<head>` instead of before the document's own styles | |
| | `--font-dir DIR` | Additional font directory (repeatable, up to 16) | |
| | `--timing-output FILE` | Write phase timings as JSON (single file mode) | |
| | `--view-memory-profile FILE` | Write a view-tree memory profile (single file mode) | |
| | `--event-log FILE` / `--state-dump FILE` | Replay simulated events and dump the resulting state (testing) | |
| | `--post-load-settle-ms N` | Wait N ms for scripts and animations after load | |
| | `--disable-animations` / `--auto-close` | Testing switches | |
| | `--continue-on-error` | Continue processing on errors in batch mode | `false` |
| | `--summary` | Print summary statistics | `false` |
| | `--debug` | Enable debug output | `false` |
| `-h` | `--help` | Show help | |

Batch mode (several inputs) needs `--output-dir` or `--stream-layout-results`. Unknown options are silently ignored.

**Supported input formats:** `.html`/`.htm`, `.tex`/`.latex`, `.ls`

**Examples:**

```bash
lambda layout page.html
lambda layout page.html -o layout.txt
lambda layout page.html -vw 800 -vh 600
lambda layout *.html --output-dir results/ --summary
```

---

### `render` — Render to Image or Document

Render HTML, LaTeX, or diagram files to SVG, PDF, PNG, or JPEG. What each output format can draw is compared in [HTML_CSS_SVG_Support.md](HTML_CSS_SVG_Support.md#17-output-targets).

```
lambda render <input> -o <output> [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-o` | `--output` | Output file path (**required**; format inferred from extension) | |
| `-vw` | `--viewport-width` | Viewport width in CSS pixels | Auto-size to content |
| `-vh` | `--viewport-height` | Viewport height in CSS pixels | Auto-size to content |
| `-s` | `--scale` | Raster export density; does not change logical layout (RSC7) | `1.0` |
| | `--pixel-ratio` | Device scale for HiDPI/Retina displays; legacy option spelling (RSC7) | `1.0` |
| `-t` | `--theme <name>` | Color theme for graph diagrams | light |
| | `--view-key <key>` | Select a named view when the source defines several | |
| `-h` | `--help` | Show help | |

**Supported input formats:**

| Extension | Format |
|-----------|--------|
| `.html`, `.htm` | HTML |
| `.tex`, `.latex` | LaTeX |
| `.ls` | Lambda Script |
| `.pdf` | PDF (PDF to PDF is a copy; other targets go through the Lambda PDF package) |
| `.mmd` | Mermaid diagram |
| `.d2` | D2 diagram |
| `.dot`, `.gv` | GraphViz diagram |
| `.structurizr`, `.dsl` | Structurizr C4 diagram |

**Supported output formats:** `.svg`, `.pdf`, `.png`, `.jpg`/`.jpeg`

**Default viewport sizes** (when not auto-sizing):

| Output | Width | Height |
|--------|-------|--------|
| SVG, PNG, JPEG | 1200 | 800 |
| PDF | 800 | 1200 |

**Themes** (graph diagrams): the dark palettes `zinc-dark`, `dark`, `tokyo-night`, `nord`, `dracula`, `catppuccin-mocha`, `one-dark` and `github-dark`; any other name, including `light`, `zinc-light`, `github-light`, `solarized-light` and `catppuccin-latte`, selects the light palette. Theme names are not validated.

**Examples:**

```bash
lambda render page.html -o output.svg
lambda render doc.tex -o doc.pdf
lambda render page.html -o screenshot.png -vw 1920 -vh 1080 --pixel-ratio 2.0
lambda render diagram.mmd -o diagram.svg -t github-dark
lambda render graph.d2 -o graph.png
```

---

### `demo` — Named Demos and Document Viewer

```
lambda demo [name] [view options]
lambda demo --list
```

With no name, opens the bundled `lambda.doc` viewer with its startup splash from
`<LAMBDA_HOME>/package/doc/doc_viewer.html` (D7.2.4). Named demos launch through
`view`: `lambda demo tetris` is equivalent to
`lambda view test/demo/tetris/tetris.ls`.

Names include `tetris`, `superlambda`, `doom`, `scene3d` (also `ringworld`),
`observatory`, `three-gallery`, `asset-gallery`, `shared-animation`, `slides`
and `wordcloud`. Run named demos from the checkout or unpacked release root;
their relative paths and local assets are under `test/demo/`.
See the [demo catalog](../test/demo/README.md) for entry points and controls.

`--list`, `--help` and `-h` show the available demos. An unknown name reports an
error and the catalog. Other options pass through to `view` unchanged.

```bash
lambda demo scene3d
lambda demo tetris --headless --event-file test/ui/tetris_autoplay.json
```

---

### `view` — Interactive Document Viewer

Open a document in an interactive viewer window.

```
lambda view [document_file] [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| | `--event-file <file.json>` | Load simulated events from JSON for testing | |
| | `--event-result <file.json>` | Write a machine-readable event result | |
| | `--event-log <file>` / `--state-dump <file>` | Record events / dump interaction state (testing) | |
| | `--headless` | Run without creating a window | |
| | `--view-key <key>` | Select a named view when the source defines several | |
| | `--font-dir <dir>` | Additional font directory (repeatable, up to 16) | |
| `-h` | `--help` | Show help | |

Unknown options are silently ignored.

**Default file:** `<LAMBDA_HOME>/package/doc/doc_viewer.ls`, the bundled
`lambda.doc` document viewer (D7.2.4). It browses the current working directory.
`lambda demo` opens its `doc_viewer.html` startup splash. Both commands use
`./lmd` as the default Lambda home and honor `LAMBDA_HOME`.

**Directory and archive browsing:** `lambda view directory/` and
`lambda view archive.zip` open the bundled file tree rooted at that source.
ZIP32/ZIP64 files, including ZIP-backed DOCX and JAR packages, are detected by
their content even when renamed or extensionless. Expand directories and
archive rows, then select members to read source, inspect structured data or
preview documents/images. Archive capture is eager; member decompression stays
lazy (**S12.4.1v2/S14.3.1v2**). Members are read directly from the retained
archive without extraction. Invalid archives and deferred member failures use
the existing input errors.

**Supported formats:**

`.pdf`, `.html`/`.htm`, `.md`/`.markdown`, `.tex`/`.latex`, `.ls`, `.xml`, `.rst`, `.wiki`, `.svg`, `.mmd`, `.d2`, `.dot`/`.gv`, `.structurizr`/`.dsl`, `.png`, `.jpg`/`.jpeg`, `.gif`, `.json`, `.yaml`/`.yml`, `.toml`, `.txt`, `.csv`, `.ini`, `.conf`, `.cfg`, `.log`

Also accepts **HTTP/HTTPS URLs**: an HTML page is loaded directly by the browsing shell (with its scripts and stylesheets); any other content is fetched to a temporary file, its type detected from the `Content-Type` header, and opened from there.

**Keyboard controls:**

| Key | Action |
|-----|--------|
| ESC | Close window |
| Q | Quit viewer |

**Examples:**

```bash
lambda view page.html
lambda view report.pdf
lambda view https://example.com
lambda view diagram.mmd
lambda view documents/
lambda view assets.zip
lambda view report.docx
lambda view application.jar
```

---

### `edit` — Document Editor

Open an existing local document in an editing window with a toolbar at the
top, and save it back to its file.

```
lambda edit <document_file> [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| | `--event-file <file.json>` | Load simulated events from JSON for testing | |
| | `--event-result <file.json>` | Write a machine-readable event result | |
| | `--event-log <file>` / `--state-dump <file>` | Record events / dump editor state (testing) | |
| | `--headless` | Run without creating a window | |
| | `--font-dir <dir>` | Additional font directory (repeatable, up to 16) | |
| `-h` | `--help` | Show help | |

Remote URLs are rejected: `edit` opens local files only.

**Supported formats:** the `lambda.edit` package chooses the editor from the
file's suffix.

| Suffix | Editor |
|--------|--------|
| `.md`, `.markdown` | Rich text; front matter, raw HTML, math, and blocks the editor cannot edit (footnotes, for example) are kept as written |
| `.html`, `.htm` | Rich text for the body; the head, attributes, scripts, and elements the editor does not edit are kept as written and never run |
| `.svg` | Drawing: select, move, resize, rectangle/ellipse/line/text tools, fill and stroke, duplicate, delete, paint order, zoom |

**View-only parts:** a part the rich-text editor cannot edit shows as it
renders, inside a dashed frame, and Save writes it back exactly as it was read.
In Markdown this covers a top-level block holding something the editor has no
model for (a footnote reference, say) or that its writer would respell, raw
HTML blocks, and display math; lines no block claims, such as link reference
definitions, show as their source. Inline raw HTML shows its rendering in the
running text; a lone tag or a comment, which renders nothing on its own, shows
as its source. Math shows its TeX source (`lambda view` renders it). The
rendering is display only: scripts, styles, event handlers, frames, and
navigation are removed, and form controls are disabled. Typing into a
view-only part is refused; Delete or Backspace with the caret in it removes
the whole part, and Undo restores it. A file the editor still cannot write
back without loss is not opened; the error says what would change, and
`lambda view` still shows it.

**Saving:** Save writes the same format back through a temporary file that
replaces the original only after the write succeeded. If the file changed on
disk since it was opened, Save stops and offers Overwrite, Reload, or Save As.
Save As takes a path relative to the document's folder, keeps the format, and
never creates folders. Closing a window with unsaved changes asks to Save,
Discard, or Cancel.

**Keyboard controls:**

| Key | Action |
|-----|--------|
| Cmd/Ctrl+S | Save |
| Cmd/Ctrl+Shift+S | Save As |
| Cmd/Ctrl+Z | Undo (Shift for redo) |
| ESC | Cancel the current dialog or drawing gesture, then clear the selection (does not close the window) |
| Delete, arrows | Drawing: delete or nudge the selection (Shift: by 10) |
| Cmd/Ctrl+D | Drawing: duplicate the selection |
| V, R, E, L, T | Drawing: Select, Rectangle, Ellipse, Line, Text tool |

**Examples:**

```bash
lambda edit README.md
lambda edit site/index.html
lambda edit diagram.svg
```

---

### `fetch` — HTTP/HTTPS Resource Download

Download a remote resource via HTTP or HTTPS.

```
lambda fetch <url> [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-o` | `--output <file>` | Save output to file | stdout |
| `-t` | `--timeout <ms>` | Request timeout in milliseconds | `30000` (30s) |
| `-v` | `--verbose` | Show detailed progress and timing | `false` |
| `-h` | `--help` | Show help | |

**Examples:**

```bash
lambda fetch https://example.com/data.json
lambda fetch https://example.com/data.json -o data.json
lambda fetch https://api.example.com/endpoint -t 5000 -v
```

---

### `js` — JavaScript

Run a JavaScript program on LambdaJS (see [JS_DOM_Support.md](JS_DOM_Support.md)). Pass the JavaScript file after the `js` subcommand.

```
lambda js [file.js] [options]
```

**Options:**

| Flag | Description |
|------|-------------|
| `-e`, `--eval <source>` | Evaluate a snippet |
| `-p`, `--print <source>` | Evaluate a snippet and print its result |
| `-i`, `--interactive` | Interactive session |
| `--input-type=module` | Read an ES module from stdin when no file is given |
| `--document <file.html>` | Load an HTML document for DOM API access |
| `--unhandled-rejections=strict\|none` | Unhandled Promise rejection policy |
| `--stack-size=KB` (also `--stack_size=`) | JavaScript stack size |
| `--opt-level=N` | JIT optimization level for JavaScript |
| `--diagnose` | Print diagnostics about the run |
| `--tls-min-v1.3`, `--tls-max-v1.2` | TLS version bounds (Node compatibility) |
| `-h`, `--help` | Show help |

`lambda js` with no file and no `-e`/`-p` does nothing and exits 0. `--mir-interp` is rejected (exit 9). A module whose top-level `await` is still pending at exit ends with status 13. Unknown `--` options are passed through silently.

**Examples:**

```bash
lambda js app.js
lambda js -e "console.log([1, 2, 3].map(x => x * 2))"
lambda js app.js --document index.html
```

---

### `ts` — TypeScript

```
lambda ts <file.ts>
```

Runs a TypeScript file on LambdaJS. Type annotations are stripped; there is no type checking.

---

### Other commands

- `math` — retired. It prints a message pointing at Lambda script math rendering and exits with status 1.
- `serve` — parses its options but is not implemented; exits with status 1.
- `replay --event-log <file.jsonl> [document] [--assert-state|--record|--headless|--window|--font-dir DIR]` — replays a recorded interaction against a document (testing).
- `render-batch`, `test-batch`, `js-test-batch`, `--test*`, `--emit-ast-dump <path>`, `--emit-js-ast-dump <path>` — internal harness and development commands.

---

## REPL

When Lambda is started with no arguments, it enters the interactive REPL. `lambda run` without a script starts a **procedural** session instead: entries are statements of one persistent procedure body, so `var` bindings persist and procedures can be called, and the session warns that entries carry out effects.

**Prompt:** `λ> ` (UTF-8 terminals) or `> ` (fallback)
**Continuation prompt:** `.. ` — shown while a bracket, string or comment is open, or while the entry is incomplete (`let x =`)

Each entry runs once against a persistent interpreter session; earlier entries are never re-run. An entry that fails is reported and rolled back whole. An entry made only of declarations or statements prints nothing; any other entry prints its value. Redefining a session name is an error (E209); `clear` starts a fresh session. Ctrl-C interrupts a running entry and rolls it back.

### REPL Commands

| Command | Description |
|---------|-------------|
| `quit`, `q`, `exit` | Exit the REPL |
| `help`, `h` | Show help |
| `clear` | Start a fresh session (drops every binding) |
| `.env` | List the session's bindings and their values |
| `.type <expr>` | Show an expression's static type without running it |
| `.time <entry>` | Run an entry and report its wall time |
| `.load <file>` | Run a file as one entry (rolled back whole on failure) |
| `.save <file>` | Write the session's accepted entries to a file |
| Tab | Complete session names, system functions and keywords (interactive terminal) |

An interactive terminal keeps its input history in `~/.lambda_history`; `LAMBDA_REPL_HISTORY` names another file, and an empty value turns history persistence off.

---

## Environment Variables

| Variable | Values | Description |
|----------|--------|-------------|
| `LAMBDA_HOME` | path | Runtime asset directory: packages, schemas, fonts. Default: `./lmd` (source checkout and release bundle alike), **relative to the current working directory** — set it to an absolute path to run `lambda` from anywhere |
| `LAMBDA_EXEC_BACKEND` | `auto`, `jit`, `interp` | Execution tier, as `--tier=`. The REPL always uses its persistent interpreter session; the tier only decides when hot functions compile (`jit`: at the first call) |
| `LAMBDA_REPL_HISTORY` | path | REPL history file (default `~/.lambda_history`; empty disables) |
| `JUBE_MODULE_PATH` | path | Where Node modules are discovered (default: `./modules` beside the executable) |
| `LAMBDA_LOG_LEVEL` | level name | Minimum log level written to `log.txt` |
| `LAMBDA_LOG_FILE` | path | Log file location (default: `./log.txt`) |
| `LAMBDA_SCRIPT_CACHE` | `0`/`1` | Enable or disable the parsed-script cache |
| `LAMBDA_DISABLE_MIR_CACHE` | set | Disable the compiled-module cache |
| `LAMBDA_PROFILE` | set | Print profiling information at exit |
| `LAMBDA_JS_LARGE_INTERP` | set | Force the interpreter for large JavaScript modules |
| `MEMTRACK_MODE` | `OFF`, `STATS`, `DEBUG` | Memory tracker mode (default: `OFF` in release builds, `STATS` in debug builds) |

Diagnostic variables for engine development: `LAMBDA_MEMORY_STATS`, `LAMBDA_RSS_REPORT`, `VIEW_MEM_STATS`, `VIEW_MEM_STAGES`, `VIEW_PAUSE_BEFORE_EXIT`, `LAMBDA_COMPILER_TIMING`, `JS_TRANSPILE_TIMING`, `LAMBDA_MIR_DUMP_PATH`, `LAMBDA_GC_FORCE_EVERY`, `LAMBDA_GC_POISON_FREED`, `LAMBDA_JS_EXEC_TIMEOUT_SECONDS`.

---

## Logging

Lambda logs to `./log.txt`. Logging is configured via `log.conf` (if present). Use `log_debug()`, `log_info()`, and `log_error()` levels.

---

## Quick Reference

```bash
# Start REPL
lambda

# Run a functional script
lambda script.ls

# Run a procedural script with main()
lambda run script.ls

# Validate a file
lambda validate data.json -s schema.ls

# Convert between formats
lambda convert input.json -t yaml -o output.yaml

# Layout HTML/CSS
lambda layout page.html

# Render to SVG/PDF/PNG
lambda render page.html -o output.svg

# Open in viewer (or edit a document)
lambda view page.html
lambda edit notes.md

# Fetch a URL
lambda fetch https://example.com -o page.html

# Run JavaScript, TypeScript
lambda js app.js
lambda ts app.ts

# Dump the memory context as JSON at exit (+ leak report in log.txt)
lambda --mem-dump script.ls
```
