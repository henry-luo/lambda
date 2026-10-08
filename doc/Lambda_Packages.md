# Lambda Packages

Lambda ships a set of **packages**: libraries written in Lambda Script itself, distributed as source with the runtime and imported under the reserved `lambda.*` namespace (D7.2.4). Some are libraries you call from your own scripts: charts, graph layout and diagram rendering, math and LaTeX typesetting, PDF rendering and OpenAPI tools. Others are parts of the engine that happen to be written in Lambda: the `lambda edit` application, its editor model, and the browser behaviour behind HTML forms, links and editing in Radiant. This page lists them all, explains how their import paths resolve, and documents the public API of each library.

> **Related Documentation**:
> - [Lambda Modules](Lambda_Modules.md) — `import` syntax, `pub` exports and module resolution
> - [Lambda CLI](Lambda_CLI.md) — the commands that load packages for you
> - [Math Support](Math_Support.md) — the math typesetting package in depth
> - [Markup and Data Formats](Markup_Formats_Support.md) — the parsers that produce the graph, LaTeX, PDF and math trees these packages consume
> - [HTML, CSS and SVG Support](HTML_CSS_SVG_Support.md) — the Radiant engine that lays out and draws package output
> - [Reactive UI](Reactive_UI.md) — the `view` templates and `on` handlers the `dom` package is written in

---

## Table of Contents

1. [Package Overview](#1-package-overview)
2. [Importing a Package](#2-importing-a-package)
3. [Writing Your Own Packages](#3-writing-your-own-packages)
4. [chart — Charts](#4-chart--charts)
5. [graph — Graph Layout and Diagrams](#5-graph--graph-layout-and-diagrams)
6. [math — Math Typesetting](#6-math--math-typesetting)
7. [latex — LaTeX to HTML](#7-latex--latex-to-html)
8. [pdf — PDF Rendering](#8-pdf--pdf-rendering)
9. [openapi — OpenAPI Tools](#9-openapi--openapi-tools)
10. [Engine Packages: edit, editor, dom](#10-engine-packages-edit-editor-dom)
11. [Tests](#11-tests)

---

## 1. Package Overview

| Package | Import path | Status | What it does | Used by the CLI |
|---------|-------------|--------|--------------|-----------------|
| `chart` | `lambda.chart.chart`, `lambda.chart.vega`, `lambda.chart.wordcloud` | Library | Declarative charts and weighted word clouds, rendered as SVG elements | No command of its own; `lambda render` and `lambda view` display a script whose result is a chart |
| `graph` | `lambda.graph.layout`, `lambda.graph.transform`, `lambda.graph.structurizr.structurizr` | Library | Layered graph layout, and diagram rendering for Mermaid, Graphviz DOT, D2 and Structurizr sources | `lambda render`, `view`, `layout` and `convert -t html` on `.mmd`, `.dot`/`.gv`, `.d2`, `.dsl`/`.structurizr` |
| `math` | `lambda.doc.math.math` | Library | Typesets LaTeX math as HTML | Markdown math in `lambda view`, `layout` and `render`; math inside LaTeX documents |
| `latex` | `lambda.latex.latex` | Library | Renders LaTeX documents as HTML | `lambda convert x.tex -t html`; `lambda view`, `layout` and `render` on `.tex`/`.latex` |
| `pdf` | `lambda.pdf.pdf` | Library, experimental | Renders PDF pages as SVG, and whole documents as HTML | `lambda view`, `layout` and `render` on `.pdf` |
| `openapi` | `lambda.openapi.openapi`, `lambda.openapi.server` | Experimental | Route listing, Lambda type generation, validation and Swagger UI pages for OpenAPI specs | None |
| `slide` | `lambda.slide` | Library, experimental | Slide elements, cues/effects, presenter console/navigation tools, themes/layouts/masters, snapshots and handouts; see [Slide Presentations](Lambda_Slide.md) | `lambda view deck.slides` |
| `edit` | `lambda.edit.edit` | Engine internal | The document-authoring application | `lambda edit` |
| `editor` | `lambda.editor.mod_editor` | Engine internal | The editing model: documents, selections, transactions, history | `lambda edit`, through `edit` |
| `dom` | `lambda.dom.dom` | Engine internal | Browser behaviour for HTML: form controls, links, focus, `<details>`, editing | `lambda view` on interactive pages |

The engine packages are importable like any other, but their API follows the engine and is not stable; this page describes them only briefly (§10).

---

## 2. Importing a Package

A package is a directory of modules. You import one module by its full dotted path, usually with an alias:

```lambda
import chart: lambda.chart.chart
import structurizr: lambda.graph.structurizr.structurizr
import tex: lambda.doc.math.math

sys.lambda.home          // "./lmd" by default
```

Without an alias, the module's `pub` names come into scope unqualified:

```lambda
import lambda.graph.layout

let r = compute({nodes: [{id: "a"}, {id: "b"}], edges: [{from: "a", to: "b"}]})
r.nodes |> ~.id          // ["a", "b"]
```

### 2.1 How paths resolve

The `lambda.*` root is reserved for everything Lambda ships (D7.2.4). Shipped package paths map onto the `package/` directory of the Lambda home:

| Import path | Loads |
|-------------|-------|
| `lambda.<package>.<module>` | `<LAMBDA_HOME>/package/<package>/<module>.ls` |
| `lambda.slide` | `<LAMBDA_HOME>/package/slide.ls` (explicit public module) |
| `lambda.<package>.<dir>.<module>` | `<LAMBDA_HOME>/package/<package>/<dir>/<module>.ls` |
| `lambda.doc.math.<module>` | `<LAMBDA_HOME>/package/math/<module>.ls` |
| `lambda.math`, `lambda.io` | The built-in `math` and `io` modules, which are not packages |
| `lambda.sys.<name>` | A system function, reachable even when a script shadows its name (S17.2.1, S17.2.2) |

- **Name an existing module.** There is no implicit directory index, so `import chart: lambda.chart` fails with E217; write `lambda.chart.chart`. `lambda.slide` has an explicit `package/slide.ls` entry file.
- **`lambda.doc.*` holds only the math typesetting package today.** It sits there so that `lambda.math` can stay the built-in math module (D7.2.4); the LaTeX package is `lambda.latex`, not `lambda.doc.latex`.
- **An alias is a binding name**, so it can be neither a keyword nor `lambda` itself (S16.10.1v2). Choose another alias for the `edit` package:

```lambda error=E100
import edit: lambda.edit.edit
```

- **Packages are compiled from source.** A module is compiled the first time a process imports it and is then shared by every script that imports it; compiled code is only a local cache (D7.2.3).

### 2.2 Where packages live

The Lambda home directory is chosen at startup:

1. The `LAMBDA_HOME` environment variable, when set.
2. Otherwise `./lmd`, relative to the **current working directory**, not to the executable. A source checkout and a release bundle share this layout.

`sys.lambda.home` reports the directory in use. A release (`make release`) copies the whole `lmd/` tree to `release/lmd/`, next to the `lambda` executable. Run release scripts from that directory, or point `LAMBDA_HOME` at `lmd` with an absolute path:

```bash
cd release && ./lambda report.ls                          # finds ./lmd/package
LAMBDA_HOME=/opt/lambda/lmd /opt/lambda/lambda report.ls  # from any directory
```

Run from a directory that has no `lmd/`, without `LAMBDA_HOME`, and every `lambda.*` package import fails with E217.

---

## 3. Writing Your Own Packages

Any `.ls` file is a package in this sense: a compilation unit whose `pub` declarations (`pub fn`, `pub pn`, `pub let`, `pub type`) are its exports (D7.2.1). Its module-scope bindings are immutable, so `var` belongs inside a `pn`. Import it with a leading dot, which resolves from the importing file's directory, so the pair below works from any working directory:

```lambda no-run
// no-run: shows two files in one block
// myapp/lib/stats.ls
pub fn mean(xs) => sum(xs) / len(xs)
pub fn spread(xs) => max(xs) - min(xs)
fn helper(x) => x * 2           // not exported

// myapp/main.ls
import stats: .lib.stats

let xs = [3, 5, 10]
stats.mean(xs)                  // 6
stats.spread(xs)                // 7
```

Keep your packages next to the scripts that use them. The `lambda.*` namespace is reserved for what Lambda ships (D7.2.4), and there is no package manager, registry or search path for third-party packages: share a package by sharing its source files. See [Lambda_Modules.md](Lambda_Modules.md) for the full import rules.

---

## 4. `chart` — Charts

The chart package turns a declarative chart specification into an SVG element tree. It follows the Vega-Lite grammar: rows of data, a mark type, and encoding channels that map data fields to position, colour and size. A specification is either a `<chart>` element or a Vega-Lite document converted with `lambda.chart.vega`.

```lambda
import chart: lambda.chart.chart

let spec = <chart width: 360, height: 240, title: "Sales by Region",
    <data
        <row region: "North", sales: 120>
        <row region: "South", sales: 85>
        <row region: "East", sales: 145>
    >
    <mark type: "bar">
    <encoding
        <x field: "region", dtype: "nominal">
        <y field: "sales", dtype: "quantitative">
    >
>

chart.render(spec)       // <svg xmlns: "http://www.w3.org/2000/svg", width: 360, height: 240, …>
```

The script's result is the chart, so the CLI can print, rasterize or display it directly:

```bash
lambda sales.ls                        # print the <svg> element tree
lambda render sales.ls -o sales.png    # render it to PNG (or .svg, .pdf, .jpg)
lambda view sales.ls                   # open it in a window
```

To write an SVG file, serialize the element and `output` it from a procedure, then run the script with `lambda run points.ls`. This one writes a 300×200 `points.svg` with three `<circle>` marks:

```lambda
import chart: lambda.chart.chart

pn main() {
    let spec = <chart width: 300, height: 200, title: "Points",
        <data values: [{x: 1, y: 2}, {x: 2, y: 4}, {x: 3, y: 3}]>
        <mark type: "point">
        <encoding
            <x field: "x", dtype: "quantitative">
            <y field: "y", dtype: "quantitative">
        >
    >
    output(format(chart.render(spec), 'xml'), "points.svg")^
}
```

### 4.1 Vega-Lite specifications

`vega.convert` accepts a parsed Vega-Lite document, either written as a Lambda map or read with `input("chart.json")^`. The Vega-Lite `type` key is written as is, since keywords are allowed as map keys (S16.10.2):

```lambda
import vega: lambda.chart.vega
import chart: lambda.chart.chart

let vl = {
    title: "Monthly visits",
    data: {values: [{month: "Jan", visits: 120}, {month: "Feb", visits: 150}, {month: "Mar", visits: 90}]},
    mark: "line",
    encoding: {
        x: {field: "month", type: "ordinal"},
        y: {field: "visits", type: "quantitative"}
    }
}
let svg = chart.render_spec(vega.convert(vl))
name(svg)                // 'svg'
svg.width                // 400, the default width
```

> **Experimental.** The converter supports inline/named/file/URL data, common transforms, and layer/facet/concat/repeat composition. Vega expression strings and unsupported transforms return a diagnostic; use field predicates or Lambda data preparation for those cases.

### 4.2 API

| Function | Description |
|----------|-------------|
| `chart.render(chart_el)` | Renders a `<chart>`, `<hconcat>`, `<vconcat>` or `<repeat>` element as an `<svg>` element |
| `chart.render_spec(spec)` | Renders a specification map, such as the result of `vega.convert`; an element is passed on to `render` |
| `vega.convert(vl)` | Converts a parsed Vega-Lite document (a map) into a specification map for `render_spec` |

### 4.3 Specification elements

| Element | Attributes and children |
|---------|-------------------------|
| `<chart>` | `width` (default 400), `height` (300), `padding` (a number or a `{top, right, bottom, left}` map, default 20), `title`, and the children below |
| `<data>` | Inline `values: [...]` or row children; `name` selects the chart's `datasets` map; `url` and optional `format` use Lambda input loading |
| `<mark>` | `type` (or `kind`): `bar`, `line`, `area`, `point`, `text`, `rule`, `tick`, `rect`, `arc`, `boxplot`, `errorbar`, `errorband`, `wordcloud`; an unknown type draws points. Style attributes: `color`, `opacity`, `fill`, `stroke`, `stroke_width`, `stroke_dash`, `interpolate`, `point`, `corner_radius`, `inner_radius`, `outer_radius`, `pad_angle`, `size`, `shape`, `font_size` |
| `<encoding>` | One child per channel: `<x>`, `<y>`, `<x2>`, `<y2>`, `<color>`, `<size>`, `<opacity>`, `<theta>`, `<text>`, `<stroke>`, `<x_offset>`, `<detail>`, `<tooltip>`, `<shape>`, `<order>`. Each takes `field`, `dtype` (`quantitative`, `nominal`, `ordinal` or `temporal`), `value`, `datum`, `title`, `aggregate`, `bin`, `stack`, `sort`, `zero`, `scale`, `axis`, `legend`, `format` and `condition` |
| `<transform>` | Ordered `filter`, `sort`, `aggregate`, `calculate`, `bin`, `fold`, `flatten`, `window`, `lookup`, `density`, `regression`, and `loess` steps; analytical options are described below |
| `<config>` | `theme`: `light`, `dark`, `minimal`, `presentation`, or a custom map; nested `mark`/mark-type/`axis`/`legend` settings or equivalent prefixed attributes; `font`; `axis_grid: true` for horizontal grids |
| `<layer>` | `<chart>` children drawn over one another |
| `<facet>` | `field` with wrapping `columns` (default 3), or `row`/`column` field definitions; `spacing` (default 20); common scale domains by default |
| `<annotation>` | `<text_note x, y, text, color, font_size, anchor, dx, dy>` and `<rule_note x or y, color, stroke_width, stroke_dash>` children |
| `<hconcat>`, `<vconcat>` | `spacing` (default 20); `<chart>` children placed side by side or stacked |
| `<repeat>` | A `<row [fields]>` and/or `<column [fields]>` child plus one `<chart>` template whose channels say `field: {repeat: "row"}` or `field: {repeat: "column"}` |

A pie or donut chart is an `arc` mark with a `theta` channel, plus `inner_radius` for a donut; grouped bars use an `x_offset` channel. Aggregate operations are `count`, `sum`, `mean` (or `average`), `median`, `min`, `max`, `distinct`, `q1`, `q3`, `stdev` and `variance`. A colour channel picks a palette with `scale: {scheme: "set1"}`: `category10` is the default for categories and `blues` for quantities, and `category20`, `set1`, `pastel1`, `dark2`, `greens`, `reds`, `oranges`, `purples`, `greys`, `red_blue` and `spectral` are also available. `scale: {domain: [...], range: [...]}` assigns colours explicitly.

#### Analytical transforms

`lambda.chart.transform.apply_transforms(rows, steps, datasets = null)` also
applies the transform vocabulary directly. `steps` is a `<transform>` element
or an array of maps carrying `type`. Invalid options return value errors
(S7.4.1); later steps stop at the first diagnostic.

| Step | Options and result |
|---|---|
| `<window>` | `groupby`, multi-field `sort`, inclusive `frame` (default `[null, 0]`), `ignore_peers`; `<agg op, field, as, param>` children add aggregate, ranking, lag/lead, or first/last/nth-value fields without changing source order |
| `<lookup>` | `field`, `from: {data, key, fields}`, optional parallel `as` names and `default`; a left join against inline, named, or file/URL data. Omit foreign `fields` and supply one `as` name to embed the matching record |
| `<density>` | `field`, optional `groupby`, `bandwidth` (zero estimates it), `extent`, `steps`, `as`, `counts`, `cumulative`, `resolve: "shared"`; Gaussian estimates default to `value`/`density` output |
| `<regression>` | `x`, `y`, optional `groupby`, `method` (`linear`, `log`, `exp`, `pow`, `quad`, `poly`), polynomial `order`, `extent`, `steps`, `as`, `params`; curve records or `coef`/`r_squared` model parameters |
| `<loess>` | `x`, `y`, optional `groupby`, `bandwidth` in `(0, 1]` (default 0.3), `as`; locally weighted trend records |

Windows support aggregate operations plus `row_number`, `rank`, `dense_rank`,
`percent_rank`, `cume_dist`, `ntile`, `lag`, `lead`, `first_value`, `last_value`,
and zero-based `nth_value`. Tied sort keys share ranks and expand frames unless
`ignore_peers` is true. Sorting is stable (S6.2.3). Lookup uses exact value
equality (S5.1.1, S5.4.1), selects the first duplicate match, and fills missing
matches with `default` (null). Numeric summaries and statistical transforms
ignore nonfinite/nonnumeric values; invalid or rank-deficient fits return a
diagnostic. Vega-Lite conversion supports these transforms, including its
`window` operation array and `regression`/`loess` with `on` syntax.

Running-sum windows followed by a calculation of each prior total produce
waterfall charts with ordinary `bar` marks and `y`/`y2` ranges. Density and
fitted-trend outputs use ordinary line/area marks. See the
[chart design](../vibe/Lambda_Pkg_Chart.md#6-data-transformations) for full contracts
and a runnable waterfall example.

### 4.4 Word and tag clouds

Wordcloud is also a chart markup type. The `text` channel selects the word,
`size` selects its positive weight, and optional `color` controls appearance.
Records default to `text`/`weight` when channels are omitted. Mark options are
the same as the direct API below; chart dimensions, padding, title, transforms,
and composition apply normally.

```lambda
import chart: lambda.chart.chart

chart.render(<chart width: 400, height: 260, padding: 10,
    <data values: [{label: "Lambda", count: 30}, {label: "Charts", count: 12}]>
    <mark type: "wordcloud", shape: "ellipse", seed: 17>
    <encoding <text field: "label"> <size field: "count", dtype: "quantitative">>
>)
```

Chart entry points return value errors on invalid data or options, while the
direct wordcloud APIs below raise errors (S7.4.1–S7.4.2).

`lambda.chart.wordcloud` lays out weighted words and renders an SVG element.
It ships as Lambda source under the same package namespace (D7.2.1–D7.2.4).
Each input record has a nonempty, single-line `text`, a finite positive
`weight` (`int`, `i64` or `float`), and an optional `color` string. Phrases and
Unicode text are supported. Records can also override `font_size`,
`font_family`, `font_weight` and `rotation`; other record fields are retained
as metadata. An explicit positive `font_size` overrides weight-based sizing,
including the global font-size range.

```lambda
import cloud: lambda.chart.wordcloud

cloud.render([
    {text: "Lambda", weight: 40},
    {text: "Documents", weight: 25},
    {text: "Charts", weight: 15}
], {width: 600, height: 400})^
```

Save the example as `words.ls`; `lambda render words.ls -o words.png` or
`lambda view words.ls` displays the result. The raised-error return requires
`^` propagation or a handler (S7.4.2).

| Function | Result |
|----------|--------|
| `cloud.layout(words, opts = null)` | A map containing resolved options, placed `words` and `unplaced` words |
| `cloud.render(words, opts = null)` | An SVG element; `data-unplaced` records the number of words that could not fit |

The layout sorts by descending weight, retaining source order among ties
(S6.2.3), and defaults to square-root size scaling. Linear and logarithmic
scales are also available. Equal weights use `max_font_size`. Word colours
and default rotations follow source indices. Placement follows a deterministic
spiral and checks measured, oriented text rectangles with padding. Angled
words can have overlapping axis-aligned bounds while their text rectangles
remain separate. Results repeat for the same inputs, options and available
fonts; fonts can differ across machines.

Placed records retain `text`, `weight`, `color` and source `index`, and add
`font_size`, `font_family`, `font_weight`, `rotation`, center coordinates
`x`/`y`, axis-aligned bounds `width`/`height` of the rotated rectangle,
unrotated `text_width`/`text_height` and a text `baseline`. Unplaced records
carry a `reason`: `"too_large"` for a rectangle that cannot fit the selected shape, or
`"no_space"` when the bounded search finds no placement. Empty input is valid.
Invalid data or options raise an error (S7.4.2). Fields starting with `_` are
internal geometry data and should not be used by callers.

| Option | Default | Meaning |
|--------|---------|---------|
| `width`, `height` | `600`, `400` | Positive viewport dimensions; fractional dimensions are preserved |
| `margin` | `8` | Empty space inside the viewport edges |
| `padding` | `2` | Minimum gap between word boxes |
| `min_font_size`, `max_font_size` | `12`, `64` | Font-size range in CSS pixels |
| `size_scale` | `"sqrt"` | `"sqrt"`, `"linear"` or `"log"` weight-to-size mapping |
| `font_family` | `"sans-serif"` | CSS font family or family list |
| `font_weight` | `400` | Integer CSS weight from 100 to 900 |
| `colors` | Tableau 10 | Nonempty array of colour strings; a record's `color` overrides it |
| `rotations` | `[0]` | Nonempty array of finite angles from −180 to 180 degrees; cycles by source index |
| `shape` | `"rectangle"` | `"rectangle"`, `"ellipse"`, `"circle"` or `"diamond"` boundary |
| `spiral` | `"archimedean"` | `"archimedean"` or `"rectangular"` candidate path |
| `seed` | `0` | Integer from 0 to 2147483646; changes each word's spiral phase without global randomness; zero retains the original path |
| `step` | `4` | Distance between Archimedean turns or rectangular grid points in CSS pixels |
| `max_steps` | `4000` | Positive integer candidate limit per word |

Text is measured in one headless Radiant pass through the reusable
`radiant.measure_html(html, width, height)` function. It returns copied
`width`, `height` and `baseline` metrics for each direct body element, in order
(`null` for an element without a layout box), then releases the temporary
document. The package uses each word's actual font family, weight and size in
its SVG. Shape containment checks all four corners of each rotated rectangle;
circle diameter uses the smaller available viewport dimension. Collision
detection uses padded text rectangles, rather than glyph masks.

```lambda
import cloud: lambda.chart.wordcloud

cloud.render([
    {text: "Lambda", weight: 40, font_weight: 700, rotation: -15},
    {text: "Documents", weight: 25},
    {text: "Charts", weight: 15}
], {width: 600, height: 400, shape: "ellipse", rotations: [-30, 0, 30],
    size_scale: "log", seed: 17})^
```

The viewable gallery at `test/demo/wordcloud.ls` compares four configurations:
`./lambda.exe view test/demo/wordcloud.ls`. Export it with
`./lambda.exe render test/demo/wordcloud.ls -o temp/wordcloud_gallery.png`.

---

## 5. `graph` — Graph Layout and Diagrams

The graph package lays out directed graphs in ranked layers, in the manner of Dagre and Graphviz `dot`, and renders diagram sources through Radiant. It is what `lambda render`, `lambda view`, `lambda layout` and `lambda convert -t html` use for Mermaid (`.mmd`), Graphviz (`.dot`, `.gv`), D2 (`.d2`) and Structurizr (`.dsl`, `.structurizr`) files.

### 5.1 Layout

`lambda.graph.layout` works on plain data: a map of `nodes` (each with an `id`, and optionally `width` and `height`, default 80 × 40) and `edges` (`from`, `to`):

```lambda
import layout: lambda.graph.layout

let result = layout.compute({
    nodes: [{id: "a", width: 100, height: 40}, {id: "b"}, {id: "c"}],
    edges: [{from: "a", to: "b"}, {from: "a", to: "c"}]
}, {node_sep: 40, rank_sep: 60})
result.width                             // 200
result.height                            // 140
result.nodes |> [~.id, ~.x, ~.y]         // [["a", 100, 20], ["b", 40, 120], ["c", 160, 120]]
```

The result holds `width` and `height`; `nodes`, positioned by their centres with `rank` and `order`; `edges`, each with a `points` list of `{x, y}` route points; and `placements`, the top-left corner of each node. Options, which may also be given on the input map: `direction` (`"TB"` by default, or `"LR"`, `"BT"`, `"RL"`), `node_sep` (60), `rank_sep` (80), `edge_sep` (10), `route_mode`, `use_splines`, `ordering`, `new_rank`, `compound` and `max_iterations` (8).

### 5.2 Rendering diagrams

A diagram source parses into a `<graph>` element with `input(path, {type: 'graph', flavor: …})` or `parse(text, …)` (see [Markup_Formats_Support.md](Markup_Formats_Support.md)). `lambda.graph.transform` turns it into HTML that Radiant lays out with its `lambda-graph` custom layout, and can render that to SVG:

```lambda
import transform: lambda.graph.transform

let g = parse("flowchart LR\n  A[Start] --> B{Check}\n  B -->|yes| C[Done]", {type: 'graph', flavor: 'mermaid'})^
let html = transform.to_html(g, {theme: "dark"})
html.class                                   // "lambda-graph lambda-graph-theme-dark"
let installed = transform.install()          // register the lambda-graph layout with Radiant first
let svg = transform.render_svg(g, 480, 160)
type(svg)                                    // string
```

`install()` must run before `render_svg`: without the custom layout the nodes are laid out as plain inline boxes and no edges are drawn. Other HTML renderers do not know the `lambda-graph` layout either, so the HTML from `to_html` is meant for Radiant. From the command line:

```bash
lambda render flow.mmd -o flow.svg                           # light palette
lambda render flow.mmd -o flow.png -t github-dark            # a dark palette
lambda convert flow.mmd -t html -o flow.html                 # the <graph> HTML shown above
lambda render arch.dsl -o containers.svg --view-key Containers   # one Structurizr view
```

### 5.3 API

`lambda.graph.transform`:

| Function | Description |
|----------|-------------|
| `install()` | Registers the `lambda-graph` custom layout with Radiant; returns `true` |
| `to_html(graph, opts = null)` | Normalizes a parsed graph and returns the `<graph>` HTML element. Options: `theme` (`"light"` by default; `dark`, `zinc-dark`, `github-dark`, `nord`, `dracula`, `tokyo-night`, `one-dark` and `catppuccin-mocha` select the dark palette), `direction`, `route_mode`, `node_sep`, `rank_sep`, `edge_sep` |
| `render_svg(graph, width: int, height: int, opts = null)` | Lays out `to_html(graph, opts)` in a viewport of that size and returns SVG markup as a string |
| `render_scene(graph, width: int, height: int, opts = null)` | Returns `{svg, scene}`, the SVG plus a renderer-neutral scene description |
| `scene_from_svg(svg)` | Extracts the scene description from rendered SVG |
| `compare_scenes(actual, expected, policy = null)` | Compares two scenes, for regression tests |

`lambda.graph.layout`: `compute(input, opts = null)` as above, `make_options()` for the default option map, and `layout(input, opts = null)`, an older name for `compute`.

`lambda.graph.normalize`: `normalize(graph)` converts a parsed source graph into the package's canonical graph form and returns `{graph, diagnostics, valid}`; `validate(graph)` returns the diagnostics alone, each a `{code, severity, message, path, source}` map.

`lambda.graph.model` reads a graph without normalizing it: `nodes(graph)`, `edges(graph)`, `subgraphs(graph)`, `ports(node)`, `direction(graph)`, `title(graph)`, `description(graph)`, `diagnostics(graph)`, `style_rules(graph)`, `classes_for(graph, node_id)` and similar helpers. For the flowchart above, `model.direction(g)` is `"LR"` and `model.edges(g) |> [~.from, ~.to, ~.label]` is `[["A", "B", null], ["B", "C", "yes"]]`.

`lambda.graph.structurizr.structurizr` handles Structurizr workspaces: `normalize(source)` builds a workspace from a parsed `.dsl` file; `validate_source(source)` and `validate(workspace)` check it; `view_keys(workspace)` lists its views; `project(workspace, key = null)` and `project_all(workspace)` produce the graph for one view or for all of them; `to_html(workspace, key = null, opts = null)` renders a view, the first one when `key` is `null`.

`lambda.graph.document.to_html(source, options)` is the entry point the CLI calls for every flavor, reading `view_key` and `theme` from `options`. `lambda.graph.graph` is a compatibility facade over `lambda.graph.layout`; new code should import `layout` or `transform` directly.

---

## 6. `math` — Math Typesetting

The math package typesets a parsed math tree (from `parse(text, 'math')` or `input(path, 'math')`) as HTML `<span>` elements styled by its own stylesheet. [Math_Support.md §2](Math_Support.md#2-rendering-math-to-html) documents it in full; this is a summary.

```lambda
import math: lambda.doc.math.math

let ast = parse("\\sqrt{x^2 + 1}", 'math')^
let el = math.render_display(ast)
el.class                                                   // "lm_latex"
contains(format(el, 'html'), "lm_sqrt")                    // true
contains(math.stylesheet({font: "katex"}), "KaTeX_Main")   // true
```

| Function | Description |
|----------|-------------|
| `render_math(ast, options)` | Renders with options `display` (bool), `standalone` (bool: embed the stylesheet) and `color` |
| `render_inline(ast)` | Inline (text) style |
| `render_display(ast)` | Display (block) style |
| `render_standalone(ast)` | Display style with the stylesheet embedded in a `<style>` element |
| `stylesheet(options = null)` | The CSS as a string; `{font: "katex"}` selects the KaTeX fonts |

`lambda view`, `lambda layout` and `lambda render` typeset the `$…$` and `$$…$$` math of Markdown documents through this package, and the `latex` package uses it for the math inside LaTeX documents.

---

## 7. `latex` — LaTeX to HTML

The latex package renders a parsed LaTeX document (`input(path, 'latex')` or `parse(text, 'latex')`) as HTML: the `article`, `book` and `report` document classes, sectioning with numbering, lists, tables, footnotes, boxes, colour, spacing, the `picture` environment, `\newcommand` macros, and math through the `math` package.

Common `\usepackage` declarations select the shipped script adapters for `amsmath`, `amssymb`, `graphicx`, `hyperref`, `geometry`, `xcolor`, `booktabs`, `biblatex`, `babel`, `enumitem`, `microtype`, `siunitx`, and TikZ. Package names select static source modules under **D7.2.1–D7.2.4**; document text is not executed as code (**S1.8**). The [package compatibility matrix](../vibe/Lambda_Pkg_Latex3.md#6-compatibility-matrix) lists supported commands and output limits.

The bounded `biblatex` profile reads multiple local `.bib` resources, supports ten common entry types, six built-in citation/bibliography styles, source-order citation commands, `\nocite`, filtered and scoped `\printbibliography`, and English/German/French strings. It handles common BibTeX strings and inheritance, creates internal citation links, and reports missing or unsupported requests with source locations (**D7.1.2v2, S12.4.1, S7.4.1–S7.4.4**). `backend=biber` selects this local profile; no Biber process or arbitrary style file is run. See [Phase 2 behavior and boundaries](../vibe/Lambda_Pkg_Latex3.md#7-phase-2--common-biblatex-workflows-implemented).

For PDF export, `hyperref` links and anchors become link annotations and destinations, and its title, author, subject and keywords become PDF Info metadata. `geometry` supplies fixed `@page` paper size and margins for a one-page export; automatic page breaking still requires the separate paged path. `graphicx` trim/clip uses the same supported CSS `clip-path` shapes in SVG and PDF. These remain Lambda-script package policies under **D7.2.1–D7.2.4**; unsupported options and commands follow **S7.4.1–S7.4.4** diagnostics.

Phase III adds bounded theorem/proof, table, language, TikZ/PGFPlots, `natbib`, `caption`/`subcaption`, English `cleveref`, and unhighlighted `listings` profiles. Raw code and document-local `filecontents` bibliographies are preserved. `hyperref` headings and plain-text `\pdfbookmark` titles become hierarchical PDF outlines. `render_result.assets` records image/bibliography paths, origin, availability and source offsets; availability checks local existence, not successful decoding or remote fetching. See the [sample compatibility report](../test/latex/samples/COMPATIBILITY.md).

For the shared paged PDF path, pass `{target: "pdf", paged: true}` while generating the standalone HTML, then export it with `lambda render --paged`. The bounded `fancyhdr`/`lastpage` profile supplies text, running marks and page counters. Continuous output diagnoses unresolved page counters. Paged tables and columns, native Thai shaping/line breaking, exact font substitutions, and TeX typography remain explicit limits (**S7.4.1–S7.4.4**). Unicode PDF glyphs use font outlines and are not searchable text; full ToUnicode font embedding is a separate host capability. Lua and arbitrary package/style-file execution are excluded (**S1.8**).

```lambda
import latex: lambda.latex.latex

let src = "\\documentclass{article}\n\\begin{document}\n\\section{Intro}\nHello \\textbf{world}.\n\\end{document}"
let ast = parse(src, 'latex')^
let body = latex.render(ast, null)
name(body)                               // 'article'
body.class                               // "latex-document latex-article"
let page = latex.render(ast, {standalone: true})
name(page)                               // 'html'
```

| Function | Description |
|----------|-------------|
| `render(ast, options)` | Renders to an element tree: the `<article>` body (inside a `<div class: "latex-output">` together with a footnotes section when there are footnotes), or a complete `<html>` page with the LaTeX and math stylesheets when `options.standalone` is `true` |
| `render_result(ast, options)` | Returns `{body, elements, stylesheet, metadata, packages, diagnostics, assets}`. Diagnostics have a code, package, item, message, and source offset under **S7.4.1–S7.4.4** |
| `render_to_html(ast, options)` | The same, serialized to an HTML string |
| `render_document(ast, options)` | The entry point the CLI uses; like `render`, but defaults to a standalone page unless `options.standalone` is explicitly `false` |
| `render_default(ast)` | `render(ast, null)` |
| `render_file(path)` | Parses a LaTeX file and renders it |
| `render_file_to_html(path)` | Parses a LaTeX file and returns an HTML string |
| `render_string(source)`, `render_string_to_html(source)` | Meant to render LaTeX source text; see the note below |

`options` may be `null`. `standalone` selects a complete page; `font_option: "katex"` selects KaTeX math fonts; `base_uri` resolves relative graphics and bibliography resources; `target: "pdf"` or `target: "svg"` enables output-specific diagnostics. The file entry points derive `base_uri` from the file path. Parsed-document CLI transforms receive a neutral `source_path` option from the host, which the LaTeX package uses as the resource base when `base_uri` is absent (**D7.1.2v2**). The document class comes from `\documentclass` in the source. On the command line, `lambda convert paper.tex -t html -o paper.html` writes the rendered body without the LaTeX stylesheets, `--full-document` writes the standalone page instead, and `--font-option katex` passes the font option; `lambda view`, `layout` and `render` always render the standalone page.

`render_string` and `render_string_to_html` parse source text directly. For resources referenced from a source string, parse it and call `render` or `render_result` with an explicit `base_uri`.

---

## 8. `pdf` — PDF Rendering

The pdf package renders the object tree that `input(path, 'pdf')` produces. Each page's content stream is interpreted into SVG: text (embedded fonts become `@font-face` rules), vector paths, images and shadings. A whole document becomes an HTML page with one SVG per page plus a selectable text layer. The example reads a sample file from the Lambda source tree:

```lambda
import pdf: lambda.pdf.pdf

let doc = input("test/input/test.pdf", 'pdf')^
pdf.pdf_metadata(doc).Title              // "Test PDF Document"
pdf.pdf_page_count(doc)                  // 1
let page = pdf.pdf_to_svg(doc, 0, null)
page.viewBox                             // "0 0 612 792"
let html = pdf.pdf_to_html(doc, {title: "Test PDF"})
name(html)                               // 'html'
```

To save the HTML, `output` it from a procedure:

```lambda
import pdf: lambda.pdf.pdf

pn main() {
    let doc = input("test/input/test.pdf", 'pdf')^
    output(pdf.pdf_to_html(doc, {title: "Test PDF"}), "test_pdf.html")^
}
```

| Function | Description |
|----------|-------------|
| `pdf_to_svg(pdf, page_index, opts)` | Renders one page (0-based) as an `<svg>` element; a missing page renders a placeholder |
| `pdf_to_html(pdf, opts)` | Renders the document as an `<html>` element whose body holds one `<div class: "pdf-page">` per page |
| `pdf_page_count(pdf)` | The number of pages |
| `pdf_metadata(pdf)` | The document information dictionary (`Title`, `Author`, `Subject`, `Keywords`, `Creator`, `Producer`, `CreationDate`, `ModDate`) as a map, best effort |

`opts` may be `null`. Options: `title` (the HTML title, default `"PDF Document"`), `css` (replaces the default page stylesheet), `background` (the page colour, default `"white"`), `show_label` (draws a "Page n" label on each page), `max_pages` (how many pages `pdf_to_html` renders, default 48), and `id_prefix` (the generated SVG resource namespace, default `"pdf"`; use distinct prefixes when embedding multiple PDFs in one DOM tree). Each page extends this namespace with its zero-based page index, so its clipping paths, patterns and shading resources cannot collide with other pages' definitions.

> **Experimental.** Rendering fidelity varies from file to file. `lambda view`, `lambda layout` and `lambda render` pass no options, so they show at most the first 48 pages of a document.

---

## 9. `openapi` — OpenAPI Tools

> **Experimental.** No tests cover this package yet, and its body validation does not work (see below).

The openapi package works on a parsed OpenAPI 3 document. Given this `pets.yaml`:

```yaml
openapi: 3.0.3
info:
  title: Pet Store
  version: 1.0.0
paths:
  /pets:
    get:
      summary: List pets
      operationId: listPets
      tags: [pets]
      parameters:
        - name: limit
          in: query
          required: true
          schema:
            type: integer
    post:
      summary: Create a pet
      operationId: createPet
      requestBody:
        content:
          application/json:
            schema:
              $ref: '#/components/schemas/Pet'
      responses:
        '201':
          description: Created
components:
  schemas:
    Pet:
      type: object
      required: [name]
      properties:
        name:
          type: string
        age:
          type: integer
```

```lambda
import openapi: lambda.openapi.openapi

let spec = input("pets.yaml")^
openapi.routes(spec) |> [~.method, ~.path, ~.operation_id]
// [["get", "/pets", "listPets"], ["post", "/pets", "createPet"]]
openapi.schema_for(spec, "Pet")
// "type Pet = {\n    name: string,\n    age: int?\n}"
openapi.validate_params(spec, "/pets", "get", {limit: "10"}).valid     // true
openapi.validate_params(spec, "/pets", "get", {}).errors[0].message    // "required query parameter missing"
```

`lambda.openapi.openapi`:

| Function | Description |
|----------|-------------|
| `load_spec(path)` | Reads a YAML or JSON spec with `input(path)` |
| `parse_spec(source, fmt)` | Parses spec text with `parse(source, fmt)`; `fmt` is `'yaml'` or `'json'` |
| `routes(spec)` | One `{path, method, summary, operation_id, tags}` map per operation |
| `params(spec, path, method)` | The parameters of one operation, as `{name, location, required, schema_type}` maps |
| `schema_for(spec, type_name)` | One `components.schemas` entry as Lambda type source (`"type Pet = {…}"`), or `null` |
| `to_lambda_schema(spec)` | Every component schema as Lambda type source |
| `validate_params(spec, path, method, params)` | Checks that the required parameters are present; returns `{valid, errors}` |
| `validate_request(spec, path, method, body)` | Checks a request body against its schema; returns `{valid, errors}` |
| `validate_response(spec, path, method, status, body)` | Checks a response body for a status code such as `"200"`; returns `{valid, errors}` |
| `spec_json(spec)` | The spec as a JSON string |
| `docs_html(spec, spec_url)` | A Swagger UI page (an HTML string) that loads the spec from `spec_url`, `"/openapi.json"` when `null` |
| `docs_html_inline(spec)` | A Swagger UI page with the spec embedded |
| `docs_redoc(spec, spec_url)` | A Redoc page |

`load_spec` and `parse_spec` declare no error return, so a postfix `^` after them is rejected (E200) and a failed load arrives as an error value. To propagate the failure, call `input` or `parse` with `^` yourself, as the example does. The Swagger UI and Redoc pages load their scripts and styles from unpkg.com and cdn.redoc.ly.

> **Experimental.** `validate_request` and `validate_response` do not work in this release: they report a `"type mismatch"` for every value checked against an `object`, `array` or primitive schema. A string response checked against `type: string` fails with `expected: "string", actual: "string"`. `validate_params` works, but it only checks that required parameters are present.

`lambda.openapi.server` bundles these for an HTTP server:

| Function | Description |
|----------|-------------|
| `init(spec_path, base_path)` | Loads a spec and precomputes `{spec, docs_html, spec_json, routes, base_path}` |
| `handle_request(ctx, path, method)` | Answers `get` on `<base_path>/openapi.json` and `<base_path>/docs` with a `{status, headers, body}` map; `null` for any other path |
| `check_request(ctx, path, method, body)`, `check_response(ctx, path, method, status, body)`, `check_params(ctx, path, method, params)` | The validators above, applied to `ctx.spec` |
| `error_response(result)`, `server_error_response(result)` | A 400 or 500 JSON response built from a validation result |

> **Not yet implemented.** `lambda` has no HTTP server that calls `lambda.openapi.server`; a script can call the handlers itself, but nothing serves them.

---

## 10. Engine Packages: edit, editor, dom

These packages implement parts of the engine in Lambda. They load automatically when a command needs them, and their API follows the engine: it is not stable, and scripts should not depend on it.

### 10.1 `edit` — the `lambda edit` application

`lambda.edit.edit` is the document-authoring application behind `lambda edit` (see [Lambda_CLI.md](Lambda_CLI.md)). The CLI passes it a local path, and `open_document(path, options)` does the rest: it picks a format by suffix (`format_for_path(path)`, over `formats`: Markdown `.md`/`.markdown`, HTML `.html`/`.htm`, SVG `.svg`), reads and imports the file, refuses a document it could not write back without loss, and builds the editing page with its toolbar and rich-text or drawing surface. The format adapters are `lambda.edit.markdown`, `lambda.edit.html` and `lambda.edit.svg`; saving and reloading live in `lambda.edit.session`.

### 10.2 `editor` — the editing model

`lambda.editor.*` is the rich-text editing model: documents as plain node trees (`mod_doc`), schemas for Markdown, strict CommonMark, an HTML subset and drawings (`mod_md_schema`, `mod_doc_schema`, `mod_drawing_schema`), invertible steps and transactions (`mod_step`, `mod_transaction`), undo history (`mod_history`), editing commands (`mod_commands`), paste handling, decorations and collaboration primitives (`mod_collab`). `lambda.editor.mod_editor` is the facade: `edit_open(doc, schema, selection)` creates an editor value, `edit_exec(editor, command)` applies a command built by an `edit_cmd_*` constructor such as `edit_cmd_insert_text(text)` or `edit_cmd_history_undo()`, and `edit_mount` binds the editor to a Radiant surface. Its modules are usually imported without an alias (`import lambda.editor.mod_editor`). The `edit` package uses it for every format it opens.

### 10.3 `dom` — browser behaviour for HTML

`lambda.dom.*` implements the user-agent behaviour of HTML documents in Radiant, written as `view` templates with `on` handlers (see [Reactive_UI.md](Reactive_UI.md)): form controls and their state, constraint validation, form submission and `application/x-www-form-urlencoded` encoding, link navigation, sequential focus and `autofocus`, `<details>` and `<summary>`, context menus, keyboard activation, caret movement, scrolling keys, IME composition, ARIA reflection, and the editing of `contenteditable` regions, including `designMode`, `execCommand` and the `queryCommand*` functions. Radiant loads `lambda.dom.dom` once per document, the first time an event reaches an element the package governs, and registers its templates as behaviour templates on the page's elements rather than as the page's own templates. Editing policy belongs to this package, never to native code (D7.2.5).

---

## 11. Tests

A `.ls` script in these directories runs in the Lambda runtime test suite (`test/test_lambda_gtest.exe`) when it has an expected-output `.txt` file beside it; the `test/ui/` event scripts run in the UI automation suite.

| Package | Tests |
|---------|-------|
| `chart` | `test/lambda/chart/` |
| `graph` | `test/lambda/graph/mermaid/`, `test/lambda/graph/graphviz/`, `test/lambda/graph/structurizr/`; `test/lambda/graph_layout*.ls` and `test/lambda/graph_transform_*.ls` |
| `math` | `test/lambda/math/` |
| `latex` | `test/lambda/latex/` |
| `pdf` | `test/lambda/pdf/` |
| `openapi` | None yet |
| `edit` | `test/lambda/edit/`, `test/lambda/proc/edit_session_save.ls`; UI scripts `test/ui/edit_*.json` |
| `editor` | `test/lambda/editor/`, `test/lambda/editing/` |
| `dom` | `test/lambda/dom_*.ls`; UI scripts `test/ui/dom_pkg_*.json` |
| The `lambda.*` namespace | `test/lambda/lambda_namespace.ls` |
