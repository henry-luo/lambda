# 10. Packages and Beyond

This last chapter tours what ships around the language: packages written in Lambda for math, charts and diagrams, modules for splitting your own programs into files, the document editor, and the JavaScript and Python runtimes. It ends with a map of the reference documentation.

## Typesetting Math

`parse(text, 'math')` reads a LaTeX formula into a `<math>` element tree, and the bundled `math` package typesets that tree as HTML. Save this as `frac.ls`:

```lambda
// frac.ls
import math: lambda.doc.math.math

let ast = parse("\\frac{a}{b}", 'math')^
ast
format(ast, {type: 'math', flavor: 'ascii'})
math.render_inline(ast)
```

```bash
lambda frac.ls
```

```text partial
<math
  <fraction cmd: "\\frac", numer: <group
      "a">, denom: <group
      "b">>>
"(a)/(b)"
<span class: "lm_latex",
```

The output is trimmed after the first line of the rendered formula. The backslash is doubled because it is inside a string literal, and since the formula is a tree like any other document, `format` can write it back in another notation, here ASCII math.

`import math: lambda.doc.math.math` loads the package from the runtime's package folder and names it `math`; like every `lambda.*` import, it needs `LAMBDA_HOME` when you work outside the Lambda folder ([Chapter 1](01_Getting_Started.md#get-lambda)). `render_inline` returns a tree of `<span>` elements positioned by the package's stylesheet, so a page that shows math includes `math.stylesheet()`. Save this page as `formula.ls`:

```lambda
// formula.ls
import math: lambda.doc.math.math

let quad = parse("x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}", 'math')^;
<html
    <head <style math.stylesheet()>>
    <body <p "The roots of a quadratic are " math.render_inline(quad) ".">>
>
```

```bash
lambda render formula.ls -o formula.png
```

`render_display` typesets a formula in display style, and `render_standalone` embeds the stylesheet in its result. [Math_Support.md](../Math_Support.md) lists the supported LaTeX commands and the render options.

## Charts and Diagrams

The `chart` package turns a Vega-Lite chart description, a map, into an `<svg>` element. This script totals the units in `sales.csv` by region and charts them. Save it as `chart.ls`:

```lambda
// chart.ls
import vega: lambda.chart.vega
import chart: lambda.chart.chart

let sales = input("sales.csv")^
let totals = [for (s in sales group by s.region into g)
    {region: g.region, units: sum(content(g) |> int(~.units))}]
let spec = {
    width: 360, height: 220,
    title: "Units sold by region",
    data: {values: totals},
    mark: {type: "bar"},
    encoding: {
        x: {field: "region", type: "nominal"},
        y: {field: "units", type: "quantitative"}
    }
}
chart.render_spec(vega.convert(spec))
```

The chart is an ordinary element value, so the script prints it:

```bash
lambda chart.ls
```

```text partial
<svg xmlns: "http://www.w3.org/2000/svg", width: 360, height: 220, viewBox: "0 0 360 220",
```

A script whose result is an `<svg>` element renders like a page:

```bash
lambda render chart.ls -o chart.png
```

`vega.convert` turns the Vega-Lite map into the package's own chart description, filling in defaults such as the size, and `chart.render_spec` draws it; bar, line, area, point, arc and several other marks are supported. The `graph` package, which laid out the Mermaid flowchart in [Chapter 8](08_Rendering_and_Viewing.md#markdown-and-diagrams), also reads D2, Graphviz DOT and Structurizr. Other packages render LaTeX documents and PDF files, and the editor and the DOM behaviours are packages too; [Lambda_Packages.md](../Lambda_Packages.md) describes them all.

## Your Own Modules

The `import` that loads a package also loads your own files. Every script file is a module, and the declarations marked `pub` are what it exports. Save this helper as `utils.ls`:

```lambda
// utils.ls
pub fn decade(year: int) => string(year - year % 10) ++ "s"

pub fn initials(name: string) => join(split(name, " ") |> ~[0], "")

fn shout(s: string) => upper(s) ++ "!"
```

Save this script as `report.ls` in the same folder:

```lambda no-run file=report.ls
// no-run: imports utils.ls, which must sit in the same folder
import .utils

let books = input("books.json")^
for (b in books order by b.year) [initials(b.author), decade(b.year), b.title]
```

```bash
lambda report.ls
```

```text
["DK", "1960s", "The Art of Computer Programming"]
["HA", "1980s", "Structure and Interpretation of Computer Programs"]
["AH", "1990s", "The Pragmatic Programmer"]
["RM", "2000s", "Clean Code"]
["MK", "2010s", "Designing Data-Intensive Applications"]
```

- `import .utils` loads `utils.ls` from the folder of the importing file (the leading `.`) and binds its public names, `decade` and `initials`, directly.
- `shout` has no `pub`, so it stays private to `utils.ls`.
- `import u: .utils` binds the module under a name instead, so that you write `u.decade(1968)`. That is the form you used for packages, whose paths start with `lambda.`.

[Lambda_Modules.md](../Lambda_Modules.md) covers the import forms, how paths resolve, and importing JavaScript modules.

## The Editor

```bash
lambda edit notes.md
```

`lambda edit` opens a Markdown, HTML or SVG file in an editing window with a toolbar. Markdown and HTML open in a rich-text editor, and SVG in a drawing editor with shape, text, fill and stroke tools. Its central promise is that saving loses nothing: a part of the file the editor has no model for, such as raw HTML, a footnote or display math, shows as a view-only frame and is written back exactly as it was read, and a file that could not be saved without loss is refused rather than opened. Cmd+S (Ctrl+S on Windows and Linux) saves in the same format, through a temporary file that replaces the original only once the write has succeeded; if the file changed on disk in the meantime, Save offers to overwrite, reload or save under another name. The `edit` section of [Lambda_CLI.md](../Lambda_CLI.md#edit--document-editor) has the details.

## JavaScript

Lambda includes a JavaScript engine, LambdaJS, which runs on the same runtime as Lambda itself. `lambda js -e` evaluates a snippet:

```bash norun
lambda js -e "console.log(1 + 2)"
```

```text
3
```

A file runs with `lambda js`. LambdaJS provides Node.js-style modules such as `fs`, so this script reads the same `books.json`. Save it as `hello.js`:

```javascript file=hello.js
// hello.js
const fs = require("fs");
const books = JSON.parse(fs.readFileSync("books.json", "utf8"));
const classics = books.filter(b => b.tags.includes("classic"));
console.log(`${books.length} books, ${classics.length} classics:`);
for (const b of classics) console.log(`- ${b.title} (${b.year})`);
```

```bash
lambda js hello.js
```

```text
5 books, 2 classics:
- Structure and Interpretation of Computer Programs (1985)
- The Art of Computer Programming (1968)
```

Page scripts run on the same engine inside `lambda view`, with the standard DOM APIs, and `lambda ts app.ts` runs TypeScript. [JS_DOM_Support.md](../JS_DOM_Support.md) lists what is supported, from the language and Node.js modules to the DOM.

## Python

Python runs through a hosted-language module, `lang-python`, which the full release bundle ships beside the executable; in a source checkout, `make build-lang-python` builds it. Without the module, the commands below print a hosted-language-unavailable message. Save this as `hello.py`:

```python file=hello.py
# hello.py
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

squares = [n * n for n in range(1, 6)]
print(f"Hello from Python: {squares}")
print("fib(30) =", fib(30))
```

```bash
lambda py hello.py
```

```text
Hello from Python: [1, 4, 9, 16, 25]
fib(30) = 832040
```

`lambda hello.py` does the same, choosing the language by the file extension. Python support is at an early stage: [Python_Support.md](../Python_Support.md) lists which parts of the language and the standard library work today.

## Where to Go Next

The tutorial showed each feature once. The reference documentation covers everything:

| Topic | Document |
|---|---|
| The whole language at a glance | [Lambda_Reference.md](../Lambda_Reference.md), [Lambda_Cheatsheet.md](../Lambda_Cheatsheet.md) |
| Syntax, data and types | [Lambda_Syntax.md](../Lambda_Syntax.md), [Lambda_Data.md](../Lambda_Data.md), [Lambda_Type.md](../Lambda_Type.md), [Lambda_String_Pattern.md](../Lambda_String_Pattern.md) |
| Expressions, functions and procedures | [Lambda_Expr_Stam.md](../Lambda_Expr_Stam.md), [Lambda_Func.md](../Lambda_Func.md), [Lambda_Procedural.md](../Lambda_Procedural.md) |
| Errors, concurrency and document updates | [Lambda_Error_Handling.md](../Lambda_Error_Handling.md), [Lambda_Concurrency.md](../Lambda_Concurrency.md), [Lambda_Document_Updates.md](../Lambda_Document_Updates.md) |
| Built-in functions and the command line | [Lambda_Sys_Func.md](../Lambda_Sys_Func.md), [Lambda_CLI.md](../Lambda_CLI.md) |
| Modules and packages | [Lambda_Modules.md](../Lambda_Modules.md), [Lambda_Packages.md](../Lambda_Packages.md) |
| Documents, formats and schemas | [Lambda_Doc_Pipeline.md](../Lambda_Doc_Pipeline.md), [Markup_Formats_Support.md](../Markup_Formats_Support.md), [Doc_Schema.md](../Doc_Schema.md), [Lambda_Validator_Guide.md](../Lambda_Validator_Guide.md) |
| Rendering, math and interactive pages | [HTML_CSS_SVG_Support.md](../HTML_CSS_SVG_Support.md), [Math_Support.md](../Math_Support.md), [Reactive_UI.md](../Reactive_UI.md) |
| Other languages | [JS_DOM_Support.md](../JS_DOM_Support.md), [Python_Support.md](../Python_Support.md) |
| The normative rulings behind the language | [Lambda_Formal_Semantics.md](../Lambda_Formal_Semantics.md) |

## What You Learned

- `parse(text, 'math')` reads a formula, and the `math` package typesets it as HTML for a page.
- The `chart` package turns a Vega-Lite map into an `<svg>` element; the `graph` package lays out diagrams.
- Every file is a module: `pub` exports a declaration, and `import .name` or `import alias: .name` loads it.
- `lambda edit` edits Markdown, HTML and SVG and saves them without loss.
- `lambda js` runs JavaScript, with Node.js-style modules, and `lambda py` runs Python when its module is installed.

That completes the tutorial. The [tutorial index](README.md) lists all ten chapters, if you want to revisit one.
