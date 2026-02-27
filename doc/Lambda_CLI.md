# Lambda CLI Reference

Lambda Script Interpreter v1.0

## Synopsis

```
lambda                                      # Start interactive REPL
lambda <script.ls> [options]                # Run a functional script (JIT compiled)
lambda <command> [options] [arguments]      # Run a subcommand
```

## Default Behavior

When invoked with no arguments, Lambda starts the **REPL** (Read-Eval-Print Loop).

When invoked with a `.ls` script file (and no subcommand), Lambda compiles and executes the script using JIT compilation (C2MIR).

---

## Global Options

These options apply when running a script directly (i.e., `lambda <script.ls>`).

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-h` | `--help` | Show help message | |
| | `--mir` | Use MIR JIT compilation instead of C2MIR | `false` |
| | `--transpile-only` | Transpile to C code only (no execution) | `false` |
| | `--transpile-dir DIR` | Directory for transpiled C output files | `temp` |
| | `--max-errors N` | Max type errors before stopping (0 = unlimited) | `10` |
| | `--optimize=N` | MIR JIT optimization level | `2` |
| `-O0` | | Optimization level 0 (debug, stack traces) | |
| `-O1` | | Optimization level 1 (basic) | |
| `-O2` | | Optimization level 2 (full) | |
| `-O3` | | Optimization level 3 | |
| | `--dry-run` | Skip real I/O; return fabricated results for network/filesystem operations | `false` |

### Optimization Levels

| Level | Description |
|-------|-------------|
| `0` | Debug mode with stack trace support |
| `1` | Basic optimizations |
| `2` | Full optimizations (default) |
| `3` | Aggressive optimizations |

---

## Commands

### `run` — Run a Procedural Script

Executes a Lambda script with `main()` procedure entry point.

```
lambda run [options] <script.ls>
```

**Options:**

| Flag | Long Form | Description |
|------|-----------|-------------|
| `-h` | `--help` | Show help |
| | `--mir` | Use MIR JIT compilation |
| | `--transpile-dir DIR` | Directory for transpiled C output |

**Example:**

```bash
lambda run script.ls
lambda run --mir script.ls
```

---

### `validate` — Validate Data Against a Schema

Validates one or more data files against a Lambda schema.

```
lambda validate [-s <schema>] [-f <format>] [options] <file> [files...]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-s <schema>` | | Schema file (`.ls`) | Auto-selected based on format |
| `-f <format>` | | Input format | Auto-detect from extension |
| | `--strict` | Strict mode — all optional fields must be present or null | `false` |
| | `--max-errors N` | Stop after N errors | `100` |
| | `--max-depth N` | Maximum validation depth for nested structures | `100` |
| | `--allow-unknown` | Allow fields not defined in schema | `false` |
| `-h` | `--help` | Show help | |

**Supported input formats** (auto-detected from extension):

`json`, `csv`, `ini`, `toml`, `yaml`/`yml`, `xml`, `markdown`/`md`, `rst`, `html`/`htm`, `latex`, `rtf`, `pdf`, `wiki`, `asciidoc`/`adoc`, `man`, `eml`, `ics`, `vcf`, `textile`/`txtl`, `mark`/`mk`/`m`, `text`

**Built-in schemas** (no `-s` needed):

| Format | Default Schema |
|--------|---------------|
| `html` | `html5_schema.ls` |
| `eml` | `eml_schema.ls` |
| `ics` | `ics_schema.ls` |
| `vcf` | `vcf_schema.ls` |
| `asciidoc`, `man`, `markdown`, `rst`, `textile`, `wiki` | `doc_schema.ls` |
| `.ls` files | Built-in AST validation |

Formats such as `json`, `xml`, `yaml`, `csv`, `ini`, `toml`, `latex`, `rtf`, `pdf`, and `text` require an explicit schema via `-s`.

**Examples:**

```bash
lambda validate data.json -s schema.ls
lambda validate page.html
lambda validate --strict config.yaml -s config_schema.ls --max-errors 50
lambda validate file1.json file2.json -s schema.ls
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
| | `--pipeline legacy\|unified` | Pipeline selection | |
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
| | `--flavor FLAVOR` | LaTeX rendering flavor: `latex-js` or `tex-proper` | `latex-js` |
| | `--continue-on-error` | Continue processing on errors in batch mode | `false` |
| | `--summary` | Print summary statistics | `false` |
| | `--debug` | Enable debug output | `false` |
| | `--help` | Show help | |

**Supported input formats:** `.html`/`.htm`, `.tex`/`.latex`, `.ls`

**Examples:**

```bash
lambda layout page.html
lambda layout page.html -o layout.txt
lambda layout page.html -vw 800 -vh 600
lambda layout doc.tex --flavor tex-proper
lambda layout *.html --output-dir results/ --summary
```

---

### `render` — Render to Image or Document

Render HTML, LaTeX, or diagram files to SVG, PDF, PNG, or JPEG.

```
lambda render <input> -o <output> [options]
```

**Options:**

| Flag | Long Form | Description | Default |
|------|-----------|-------------|---------|
| `-o` | `--output` | Output file path (**required**; format inferred from extension) | |
| `-vw` | `--viewport-width` | Viewport width in CSS pixels | Auto-size to content |
| `-vh` | `--viewport-height` | Viewport height in CSS pixels | Auto-size to content |
| `-s` | `--scale` | User zoom scale factor | `1.0` |
| | `--pixel-ratio` | Device pixel ratio for HiDPI/Retina displays | `1.0` |
| `-t` | `--theme <name>` | Color theme for graph diagrams | `zinc-dark` |
| `-h` | `--help` | Show help | |

**Supported input formats:**

| Extension | Format |
|-----------|--------|
| `.html`, `.htm` | HTML |
| `.tex`, `.latex` | LaTeX |
| `.ls` | Lambda Script |
| `.mmd` | Mermaid diagram |
| `.d2` | D2 diagram |
| `.dot`, `.gv` | GraphViz diagram |

**Supported output formats:** `.svg`, `.pdf`, `.png`, `.jpg`/`.jpeg`

**Default viewport sizes** (when not auto-sizing):

| Output | Width | Height |
|--------|-------|--------|
| SVG, PNG, JPEG | 1200 | 800 |
| PDF | 800 | 1200 |

**Available themes:**

`tokyo-night`, `nord`, `dracula`, `catppuccin-mocha`, `one-dark`, `github-dark`, `github-light`, `solarized-light`, `catppuccin-latte`, `zinc-dark`, `zinc-light`, `dark`, `light`

**Examples:**

```bash
lambda render page.html -o output.svg
lambda render doc.tex -o doc.pdf
lambda render page.html -o screenshot.png -vw 1920 -vh 1080 --pixel-ratio 2.0
lambda render diagram.mmd -o diagram.svg -t github-dark
lambda render graph.d2 -o graph.png
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
| `-h` | `--help` | Show help | |

**Default file:** `test/html/index.html` (when no file is specified)

**Supported formats:**

`.pdf`, `.html`/`.htm`, `.md`/`.markdown`, `.tex`/`.latex`, `.ls`, `.xml`, `.rst`, `.wiki`, `.svg`, `.mmd`, `.d2`, `.dot`/`.gv`, `.png`, `.jpg`/`.jpeg`, `.gif`, `.json`, `.yaml`/`.yml`, `.toml`, `.txt`, `.csv`, `.ini`, `.conf`, `.cfg`, `.log`

Also accepts **HTTP/HTTPS URLs** — fetches content, detects type from `Content-Type` header, and injects `<base>` tag for HTML.

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

### `js` — JavaScript Transpiler

Run JavaScript code through the Lambda JavaScript transpiler.

```
lambda js [file.js] [--document page.html]
```

**Options:**

| Flag | Description |
|------|-------------|
| `--document <file.html>` | Load an HTML document for DOM API access |
| `-h`, `--help` | Show help |

If no file is provided, runs built-in test cases.

**Examples:**

```bash
lambda js app.js
lambda js app.js --document index.html
```

---

### `math` — Math Rendering (Deprecated)

```
lambda math
```

This command is **deprecated**. Use `lambda run <script.ls>` to render math formulas instead.

---

## REPL

When Lambda is started with no arguments, it enters the interactive REPL.

**Prompt:** `λ> ` (UTF-8 terminals) or `> ` (fallback)
**Continuation prompt:** `.. `

### REPL Commands

| Command | Description |
|---------|-------------|
| `.quit`, `.q`, `.exit` | Exit the REPL |
| `.help`, `.h` | Show help |
| `.clear` | Clear REPL history |

---

## Environment Variables

| Variable | Values | Description |
|----------|--------|-------------|
| `MEMTRACK_MODE` | `OFF`, `STATS`, `DEBUG` | Memory tracker mode (default: `STATS`) |

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

# Open in viewer
lambda view page.html

# Fetch a URL
lambda fetch https://example.com -o page.html

# Run JavaScript
lambda js app.js
```
