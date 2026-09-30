# Lambda Script

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform](https://img.shields.io/badge/Platform-macOS%20|%20Linux%20|%20Windows-brightgreen)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Runtime: 20 MB](https://img.shields.io/badge/Runtime-20%20MB-orange)
![HTML5: 100%](https://img.shields.io/badge/HTML5-100%25-success)
![CommonMark: 100%](https://img.shields.io/badge/CommonMark-100%25-success)
![YAML 1.2: 100%](https://img.shields.io/badge/YAML_1.2-100%25-success)

A general-purpose, cross-platform, functional scripting language and document processing engine in a single **~20 MB** executable, built from scratch in C/C++.

Lambda is designed for two things at once:

1) an expressive functional language for transforming data and documents, and
2) an end-to-end [document pipeline](doc/Lambda_Doc_Pipeline.md) (parse → validate/transform → layout → render/view).

[![Lambda and Radiant document pipeline](doc/img/lambda-radiant-pipeline.svg)](doc/Lambda_Doc_Pipeline.md)

Internally, Lambda treats documents as structured data. Different input formats (Markdown, Wiki, HTML/XML, JSON/YAML/TOML/CSV, LaTeX, PDF, …) are parsed into a unified Lambda/Mark node tree, transformed with Lambda scripts, validated with schemas, and then rendered via the Radiant HTML/CSS/SVG/JS layout engine.

> Note: Lambda Script is still evolving — syntax/semantics and implementation details may change.
> A stable subset of the literal data model is separately formalised and released as
> [Mark Notation](https://github.com/henry-luo/mark).

## Demo

<p align="center">
  <img src="doc/img/demo.png" width="49%" />
  <img src="doc/img/demo2.png" width="49%" />
</p>
<p align="center">
  <img src="doc/img/demo3.png" width="80%" />
</p>

**Try it:** download the Lambda binary from the [Releases](https://github.com/henry-luo/lambda/releases) page, unzip, and run:
```bash
lambda view
```

## Features

**Lambda script (pure functional runtime)**
- **Pure-functional core** with immutable data structures (arrays, maps, elements) and first-class functions and types.
- **Expressive pipe operator** (`|>`, with the filter stage `|:`) for fluent set-oriented data transformation pipelines with inline mapping and filtering.
- **Vector arithmetic** with automatic broadcasting — apply scalar operations to entire collections.
- **Powerful for-expressions** with `where`, `order by`, `limit`, `offset` clauses for SQL-like data querying.
- **Interactive REPL** for exploration and debugging.

**Markup input parsing & formatting**
- **Multi-format parsing**: JSON, XML, HTML, Markdown, Wiki, YAML/TOML/INI, CSV, LaTeX, PDF, and more.
- **One universal representation**: parse disparate syntaxes into a common Lambda/Mark node tree.
- **Conversion pipeline**: convert between formats using `lambda convert` (auto-detect input formats when possible).
- **DOM-centric tooling**: designed to treat "documents as data/objects", not just as text.

**Type system & schema validation**
- **Rich type system** with type inference and explicit type annotations, similar to and beyond that of TypeScript.
- **Schema-based validation** for structured data and document trees (including element schemas for HTML/XML-like structures).

![Type Hierarchy](doc/img/type_hierarchy.svg)

**Radiant HTML/CSS/SVG/JS layout, rendering & viewer**
- **Browser-compatible layout engine** supporting html block, inline, flex, grid, and tables.
- **Unified interactive viewer** via `lambda view`:
   - HTML/CSS/SVG with JavaScript — the embedded LambdaJS engine covers ES2024 (test262), the browser DOM and a Node.js compatibility layer
   - XML (treated as HTML with CSS styling)
   - Markdown / Wiki (rendered with styling)
   - LaTeX (`.tex`) via conversion to HTML
   - Lambda script (`.ls`) evaluated to HTML and rendered (think of PHP)
- **Render targets**: SVG / PDF / PNG / JPEG output via `lambda render`.

## Language Highlights

#### Elements (Markup Literals)

First-class markup syntax for document generation:

```lambda
let card = <div class: "card",
    <h2 "Title">
    <p "Content here.">
>
format(card, 'html')
```

#### Vector Arithmetic

Scalar operations automatically broadcast over collections:

```lambda
1 + [2, 3];          // [3, 4]       — scalar + array
[1, 2] * 2;          // [2, 4]       — array * scalar
[1, 2] + [3, 4];     // [4, 6]       — element-wise
[1, 2] ** 2;         // [1, 4]       — element-wise power
[1, 2, 3] eq 2       // [false, true, false] — element-wise comparison
```

#### Pipe Operator & Data Pipelines

The pipe operator `|>` enables fluent data transformations, and its filter stage `|:` keeps the items a test accepts. Use `~` to reference the current item:

```lambda
let users = [{name: "Alice", age: 30}, {name: "Bob", age: 15}, {name: "Carol", age: 41}]

// Map: double each element
[1, 2, 3] |> ~ * 2;                  // [2, 4, 6]

// Extract fields
users |> ~.name;                     // ["Alice", "Bob", "Carol"]

// Filter with '|:'
[1, 2, 3, 4, 5] |: ~ > 3;            // [4, 5]

// Chain operations: filter → map → aggregate
users |: ~.age >= 18 |> ~.name |> len   // 2 — count adult names
```

#### For-Expressions with SQL-like Clauses

Powerful comprehensions with `let`, `where`, `order by`,`group by`, `limit`, `offset`:

```lambda
// Filter and transform
for (x in data where x > 0) x * 2

// With local bindings
for (x in data, let sq = x * x where sq > 10) sq

// Sorting and pagination
for (x in items order by x.price desc limit 5) x.name
```

#### Rich Type System

```lambda
// Type annotations
let x: int = 42
let items: string[] = ["a", "b"]

// Union and optional types
type Result = int | error
type Name = string?

// Element type patterns
type Link = <a href: string; string>
type Article = <article title: string; string, Section*>

// Function types
fn add(a: int, b: int) int => a + b
```

#### Pattern-based Matching & Query

Match expressions support `value`, `range`, `type`, and constrained patterns:

```lambda
fn describe(x) => match x {
    case null:             "nothing"
    case 0:                "zero"              // literal value
    case 1 to 9:           "small number"      // range
    case int that ~ > 9:   "big number"        // type + constraint
    case string:           "text: " ++ ~       // type
    case int[]:            "int array"         // collection type
    default:               "something else"
}
```

The `?` query operator searches data trees by type or structure, similar to jQuery:

```lambda
html?<img>                    // all <img> descendants
html?<div class: string>      // <div>s with a class attribute
data?{status: "ok"}           // maps where status == "ok"
html[body][div]?<a>           // direct path then recursive search (given type body = <body>, type div = <div>)
```

## Quick Start

### Install From Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/henry-luo/lambda.git
   cd lambda
   ```

2. **Install dependencies:**
   ```bash
   ./setup-mac-deps.sh       # macOS
   ./setup-linux-deps.sh     # Linux
   ./setup-windows-deps.sh   # Windows (under MSYS2)
   ```

3. **Build:**

Lambda uses a Premake5-based build system generated from `build_lambda_config.json`.

```bash
make build             # Incremental build (recommended)
make release           # Optimized release build
make test              # Run unit test
make clean-all         # Clean build artifacts
```

### CLI Commands

The build produces `lambda.exe` at the repo root; release bundles ship it as `lambda`.

```bash
lambda                                          # interactive REPL
lambda <script.ls>                              # run a functional script
lambda run <script.ls>                          # run a procedural script
lambda validate <file> [-s <schema.ls>]         # validate against a schema
lambda convert <input> -t <to> -o <output>      # format conversion
lambda layout <file.html>                       # print the CSS layout tree
lambda render <input> -o <output.svg|pdf|png>   # render to image
lambda view <file.html|file.md|file.ls|...>     # open in interactive viewer
lambda edit <file.md|file.html|file.svg>        # edit a document and save it back
lambda fetch <url> [-o file]                    # download a URL
lambda js <script.js>                           # run JavaScript on LambdaJS
lambda --help                                   # show help
```

Tip: `lambda <command> --help` prints detailed options and examples.

## Examples

### Document Processing
```lambda
// Parse JSON and convert to Markdown (`^` propagates a read error)
let data = input("data.json", 'json')^
format(data, 'markdown')

// Process CSV data
let csv = input("data.csv", 'csv')^
for (row in csv where row.age > 25) row
```

### Interactive CLI/REPL
```text
λ> let data = input("sample.json", 'json')^
λ> len(data.users)
42
λ> for (u in data.users where u.active) u.name
("Alice", "Bob", "Charlie")
```

## Benchmark Results

Lambda's MIR JIT compiler is benchmarked across 6 standard benchmark suites (R7RS, AWFY, BENG, KOSTYA, LARCENY, JetStream) — 56 unique benchmarks in total — against Node.js (V8 JIT), QuickJS, and CPython.

| vs. Engine       |        Geo. Mean Ratio | Lambda Wins | Total |
| ---------------- | ---------------------: | :---------: | :---: |
| **Node.js (V8)** |              **1.05×** |     28      |  56   |
| **QuickJS**      | **0.12×** (8× faster)  |     49      |  53   |
| **CPython 3.13** | **0.08×** (13× faster) |     47      |  55   |

> Ratio < 1.0 = Lambda is faster.

**Highlights:**
- Competitive with Node.js V8 overall. Excels on micro-benchmarks, tight numeric loops and recursive workloads (R7RS: 0.44×, AWFY micro: 0.05–0.30×).
- **13× faster than CPython** across the board (wins 47/55 benchmarks).

See the [full benchmark report](test/benchmark/Overall_Result4.md) for per-benchmark details, memory profiling, and cross-engine comparisons.

## Standards Conformance

| Standard | Pass Rate | Details |
|----------|----------:|---------|
| **HTML5** (html5lib/WPT) | **100%** | 1,560+ test cases from 63 html5lib test files |
| **CSS 2.1** (W3C test suite) | **98.2%** | 1,788 / 1,821 baseline tests passing |
| **CommonMark** | **100%** | 662 / 662 specification tests passing |
| **YAML 1.2** (official test suite) | **100%** | 231 / 231 tests passing |

## Documentation

### Language Reference

| Document                                            | Description                                         |
| --------------------------------------------------- | --------------------------------------------------- |
| [Tutorial](doc/tutorial/README.md)                  | Ten chapters from installation to a reactive app, with checked examples |
| [Cheatsheet](doc/Lambda_Cheatsheet.md)              | Quick reference for syntax and common patterns      |
| [Lambda Reference](doc/Lambda_Reference.md)         | Language overview, the documentation index, modules, and examples |
| [Syntax](doc/Lambda_Syntax.md)                      | Statements, line continuation, reserved words, symbols, namespaces |
| [Data & Collections](doc/Lambda_Data.md)            | Literals, paths, arrays, maps, elements, ranges, document updates |
| [Type System](doc/Lambda_Type.md)                   | Types, unions, object types, constraints, string patterns |
| [Expressions & Statements](doc/Lambda_Expr_Stam.md) | Operators, pipes, queries, control flow, and comprehensions |
| [Functions](doc/Lambda_Func.md)                     | Function declarations, closures, and procedures     |
| [Procedural Programming](doc/Lambda_Procedural.md)  | `var`, assignment, value semantics, I/O, `main()`, concurrency |
| [Error Handling](doc/Lambda_Error_Handling.md)      | `raise`, `T^E`, postfix `^`, the `^ { }` handler, error codes |
| [String Patterns](doc/Lambda_String_Pattern.md)     | The pattern language inside `\(…)` and pattern-aware string functions |
| [Modules](doc/Lambda_Modules.md)                    | Imports, `pub` exports, built-in, package and JavaScript modules |
| [Concurrency](doc/Lambda_Concurrency.md)            | Tasks, mailboxes, `select`, timeouts and cancellation |
| [System Functions](doc/Lambda_Sys_Func.md)          | Built-in functions (math, string, collection, I/O, concurrency) |
| [Packages](doc/Lambda_Packages.md)                  | Libraries written in Lambda that ship with the runtime (math, chart, graph, LaTeX, PDF, …) |
| [CLI Reference](doc/Lambda_CLI.md)                  | Commands, flags, and usage for the Lambda CLI       |
| [Validator Guide](doc/Lambda_Validator.md)    | Schema-based validation with `lambda validate`      |
| [Document Pipeline](doc/Lambda_Doc_Pipeline.md)     | The Mark data model and the convert/validate/render/view/edit workflows |
| [Markup & Data Format Support](doc/Markup_Formats_Support.md) | Supported input and output formats and how they map to Lambda/Mark |
| [Doc Schema](doc/Doc_Schema.md)                     | Schema for lightweight markup (Markdown, Wiki, RST) |
| [HTML, CSS and SVG Support](doc/HTML_CSS_SVG_Support.md) | What the Radiant layout and rendering engine supports |
| [Math Support](doc/Math_Support.md)                 | LaTeX and ASCII math input and rendering            |
| [Reactive UI](doc/Reactive_UI.md)                   | `view`/`edit` templates, `apply()` and event handlers |
| [Formal Semantics](doc/Lambda_Formal_Semantics.md)  | Normative semantics specification — S-numbered rulings; the semantic authority when docs or implementation disagree |

### Developer Documentation

| Document                                              | Description                                                                            |
| ----------------------------------------------------- | -------------------------------------------------------------------------------------- |
| [Developer Guide](doc/dev/Developer_Guide.md)         | Build from source, dependencies, testing, Tree-sitter grammar, MIR JIT                 |
| [Formal Design](doc/Lambda_Formal_Design.md)          | Normative design/implementation specification — D-numbered rulings for the core runtime; the design authority when docs or implementation disagree |
| [Documentation Convention](doc/Doc_Convention.md)     | How Lambda documentation is organized — document tiers, authority order, and style conventions |
| [C+ Coding Convention](doc/dev/C_Plus_Convention.md)  | C/C++ coding convention                                                                |
| [Lambda Core Runtime Design](doc/dev/lambda/LR_00_Overview.md) | Detailed design of the core runtime — compilation pipeline, value & type model, the MIR-Direct transpiler, MIR JIT, memory & GC, builtins, error handling, Mark API, and the procedural runtime |
| [Radiant Engine Design](doc/dev/radiant/RAD_00_Overview.md) | Detailed design of the HTML/CSS layout, rendering, and interaction engine — view/DOM model, CSS resolution, layout (block/inline/flex/grid/table), rendering pipeline, SVG, events, editing, state, shell, JS scripting, and media/webview (index to the RAD_01–RAD_22 set) |
| [LambdaJS Support](doc/JS_DOM_Support.md)             | Experimental JavaScript JIT engine and browser DOM — supported features and benchmarks |
| [LambdaJS Runtime Design](doc/dev/js/JS_00_Overview.md) | Detailed design of the embedded JavaScript engine — compilation pipeline, value model, runtime, standard library, RegExp, async/modules, DOM, and Node.js compatibility |

## Platform Support

| Platform | Status | Notes                       |
| -------- | ------ | --------------------------- |
| macOS    | ✅ Full | Native development platform |
| Linux    | ✅ Full | Ubuntu 20.04+ tested        |
| Windows  | ✅ Full | Native build via MSYS2      |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **MIR Project**: JIT compilation infrastructure
- **Tree-sitter**: Incremental parsing framework
- **ThorVG**: SVG vector graphics library
