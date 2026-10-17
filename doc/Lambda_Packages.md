# Lambda Packages

Lambda ships a set of **packages**: libraries written in Lambda Script itself, distributed as source with the runtime and imported under the reserved `lambda.*` namespace (D7.2.4v3). Some are libraries you call from your own scripts: charts, graph layout and diagram rendering, math and LaTeX typesetting, PDF rendering and OpenAPI tools. Others are parts of the engine that happen to be written in Lambda: the `lambda edit` application, its editor model, and the browser behaviour behind HTML forms, links and editing in Radiant. This page lists them all, explains how their import paths resolve, and documents the public API of each library.

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
10. [Engine Packages: edit, editor, dom, doc](#10-engine-packages-edit-editor-dom-doc)
11. [map — Geographic Maps](#11-map--geographic-maps)
12. [grammar — Language Grammars](#12-grammar--language-grammars)
13. [Tests](#13-tests)
14. [eps — EPS Rendering](#14-eps--eps-rendering)
15. [docx — Word DOCX Import](#15-docx--word-docx-import)
16. [docbook — DocBook to HTML/CSS](#16-docbook--docbook-to-htmlcss)
17. [doc.wiki — Wiki writers](#17-docwiki--wiki-writers)
18. [doc.misc — Source format writers](#18-docmisc--source-format-writers)
19. [doc.ebook — EPUB reader](#19-docebook--epub-reader)

---

## 1. Package Overview

| Package | Import path | Status | What it does | Used by the CLI |
|---------|-------------|--------|--------------|-----------------|
| `image` | `lambda.image`, `lambda.image.edit` | Initial native `u8` profile | Raster Velmts, layered composition, animation, PSD/GIF I/O and snapshot editing/history; see [Image Package](Lambda_Image.md) | `input(path, 'psd')` and automatic PSD detection |
| `doc.wiki` | `lambda.doc.wiki` | Initial static profiles | Wiki-family writers, shared tasks, and loss reports | `lambda convert -t wiki:<flavor>` |
| `doc.misc` | `lambda.doc.misc` | Initial source profiles | AsciiDoc, man, Typst, EML, vCard, and iCalendar writers | `lambda convert -t asciidoc` / `man` / `typst` / `eml` / `vcf` / `ics` |
| `doc.ebook` | `lambda.doc.ebook` | Initial horizontal LTR EPUB profile | EPUB 2/3 publications, chapters, navigation, search and an interactive reader | `lambda view book.epub`; also `layout`, `render`, and file-browser preview |
| `doc.diff` | `lambda.doc.diff` | Initial unified review profile | Grammar-based patch parsing, local Git comparisons, virtual review and complete static HTML | `lambda view change.patch`; also `.diff`, `layout` and `render` |
| `ui.dtna` | `lambda.ui.dtna` | Experimental, partial native catalog | Ant-inspired components, collections, scoped tokens and native interaction; see [Lambda UI](Lambda_UI.md) | `lambda view test/ui/dtna_gallery.ls` |
| `ui.bold` | `lambda.ui.bold` | Experimental, initial native subset | Neobrutalist components with thick borders, hard shadows, scoped tokens and shared native controls; see [bold UI](Lambda_UI_Bold.md) | `lambda view test/ui/bold_gallery.ls` |
| `ui.wired` | `lambda.ui.wired` | Experimental, initial native subset | Hand-drawn SVG pencil outlines, paper surfaces, hatched progress, scoped tokens and native controls; see [wired UI](Lambda_UI_Wired.md) | `lambda view test/ui/wired_gallery.ls` |
| `ui.clay` | `lambda.ui.clay` | Experimental, broad native theme | Claymorphism surfaces and 59 components/subcomponents sharing the Ant-inspired behavior layer; see [clay UI](Lambda_UI_Clay.md) | `lambda view test/ui/clay_gallery.ls` |
| `ui.blade` | `lambda.ui.blade` | Experimental, broad native theme | High-resolution fantasy materials, 64 components/subcomponents, inventory slots, abilities and 18 original vector icons; see [blade UI](Lambda_UI_Blade.md) | `lambda view test/ui/blade_gallery.ls` |
| `chart` | `lambda.chart.chart`, `lambda.chart.vega`, `lambda.chart.wordcloud` | Library | Declarative SVG charts, scientific analysis and native 3D figures | No command of its own; `lambda render` and `lambda view` display a script whose result is a chart |
| `map` | `lambda.map` | Library, initial offline subset | Native `<geomap>` viewports with typed GeoJSON, camera interaction, feature queries and vector export | `lambda render` and `lambda view` display a script whose result is a map |
| `physics` | `lambda.physics`, `lambda.physics.scene` | Experimental 3D rigid-body profile | Native sphere/box dynamics, Scene3D controls, batch simulation and trajectory analysis; see [Physics](Lambda_Physics.md) | `lambda demo scene3d` |
| `music` | `lambda.music` | Experimental notation and MIDI profile | Native MusicXML/MXL, SVG sheet music, SMF import/export and SoundFont playback; see [MusicXML and MIDI](Lambda_Music.md) | `lambda view test/music/_player.ls` |
| `graph` | `lambda.graph.layout`, `lambda.graph.transform`, `lambda.graph.structurizr.structurizr` | Library | Layered graph layout, and diagram rendering for Mermaid, Graphviz DOT, D2 and Structurizr sources | `lambda render`, `view`, `layout` and `convert -t html` on `.mmd`, `.dot`/`.gv`, `.d2`, `.dsl`/`.structurizr` |
| `math` | `lambda.doc.math.math` | Library | Typesets LaTeX math as HTML | Markdown math in `lambda view`, `layout` and `render`; math inside LaTeX documents |
| `latex` | `lambda.doc.latex.latex` | Library | Renders LaTeX documents as HTML | `lambda convert x.tex -t html`; `lambda view`, `layout` and `render` on `.tex`/`.latex` |
| `docx` | `lambda.doc.docx` | Library, initial semantic import profile | Reads Word DOCX into elements, HTML fragments, text, metadata and diagnostics (§14) | Import from a Lambda script; direct DOCX viewing remains archive browsing |
| `docbook` | `lambda.doc.docbook` | Library, initial core HTML profile | Native port of xslTNG 2.8.5 for DocBook 5.x HTML/CSS, including chunked publications (§16) | `lambda view file.xml` recognizes DocBook roots; scripts export HTML publications |
| `pdf` | `lambda.doc.pdf.pdf` | Library, experimental | Renders PDF pages as SVG, and whole documents as HTML | `lambda view`, `layout` and `render` on `.pdf` |
| `eps` | `lambda.doc.eps.eps` | Library, initial vector profile | Interprets EPS PostScript and produces standalone SVG | `convert -t svg`, `view`, `layout` and `render` on `.eps`/`.epsi` |
| `grammar` | `lambda.grammar.xpath`, `lambda.grammar.xquery`, `lambda.grammar.jq`, `lambda.grammar.rnc` | Library, experimental | Grammars for other languages, written as Lambda `grammar` declarations: XPath 3.1, XQuery 3.1, jq 1.7.1 and RELAX NG compact parse to Lambda element trees | None |
| `openapi` | `lambda.openapi.openapi`, `lambda.openapi.server` | Experimental | Route listing, Lambda type generation, validation and Swagger UI pages for OpenAPI specs | None |
| `slide` | `lambda.slide` | Library, experimental | Slide elements, cues/effects, presenter console/navigation tools, themes/layouts/masters, snapshots and handouts; see [Slide Presentations](Lambda_Slide.md) | `lambda view deck.slides` |
| `doc` | `lambda.doc.doc_viewer` | Engine internal | The document viewer and project browser, with its startup splash and icon font | Bare `lambda view`; `lambda demo` adds the startup splash |
| `edit` | `lambda.edit.edit` | Engine internal | The document-authoring application | `lambda edit` |
| `editor.draw` | `lambda.editor.draw`, `lambda.editor.draw.view` | Library, initial native profile | Programmable vertices/edges, ports, custom paths, topology, shared transactions, archives, SVG and retained canvases | `lambda demo draw`; see [drawing API](../lmd/package/editor/draw/README.md) |
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

The `lambda.*` root is reserved for everything Lambda ships (D7.2.4v3). Shipped package paths map onto the `package/` directory of the Lambda home:

| Import path | Loads |
|-------------|-------|
| `lambda.<package>.<module>` | `<LAMBDA_HOME>/package/<package>/<module>.ls` |
| `lambda.ui.dtna` | `<LAMBDA_HOME>/package/ui/dtna.ls` (explicit public module) |
| `lambda.ui.bold` | `<LAMBDA_HOME>/package/ui/bold.ls` (explicit public module) |
| `lambda.ui.wired` | `<LAMBDA_HOME>/package/ui/wired.ls` (explicit public module) |
| `lambda.ui.clay` | `<LAMBDA_HOME>/package/ui/clay.ls` (explicit public module) |
| `lambda.ui.blade` | `<LAMBDA_HOME>/package/ui/blade.ls` (explicit public module) |
| `lambda.slide` | `<LAMBDA_HOME>/package/slide.ls` (explicit public module) |
| `lambda.map` | `<LAMBDA_HOME>/package/map.ls` (explicit public module) |
| `lambda.physics` | `<LAMBDA_HOME>/package/physics.ls` (explicit public module) |
| `lambda.music` | `<LAMBDA_HOME>/package/music.ls` (explicit public module) |
| `lambda.<package>.<dir>.<module>` | `<LAMBDA_HOME>/package/<package>/<dir>/<module>.ls` |
| `lambda.doc.<package>.<module>` | `<LAMBDA_HOME>/package/doc/<package>/<module>.ls` (including `math`, `pdf`, and `latex`) |
| `lambda.math`, `lambda.io` | The built-in `math` and `io` modules, which are not packages |
| `lambda.sys.<name>` | A system function, reachable even when a script shadows its name (S17.2.1, S17.2.2) |

- **Name an existing module.** There is no implicit directory index, so `import chart: lambda.chart` fails with E217; write `lambda.chart.chart`. `lambda.slide` has an explicit `package/slide.ls` entry file.
- **`lambda.doc.*` groups document packages and the bundled viewer.** Math, PDF and LaTeX live in `package/doc/{math,pdf,latex}/`, matching their `lambda.doc.*` imports (D7.2.4v3). The viewer and its resources live in `package/doc/`. `lambda.math` remains the built-in math module.
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

Keep your packages next to the scripts that use them. The `lambda.*` namespace is reserved for what Lambda ships (D7.2.4v3), and there is no package manager, registry or search path for third-party packages: share a package by sharing its source files. See [Lambda_Modules.md](Lambda_Modules.md) for the full import rules.

---

## 4. `chart` — Charts

The chart package turns a declarative chart specification into an SVG element tree. It follows the Vega-Lite grammar: rows of data, a mark type, and encoding channels that map data fields to position, colour and size. A specification is either a `<chart>` element or a Vega-Lite document converted with `lambda.chart.vega`.

Scientific grids/meshes, streamlines, spectral analysis, and native 3D figures
are described in [Scientific Charts](Lambda_Chart_Plot.md). `chart.figure`
returns SVG for 2D or a native scene with SVG guides for 3D; `chart.render`
retains its SVG-only contract. The pure numerical modules are
`lambda.chart.mesh`, `lambda.chart.stream`, and `lambda.chart.signal`.

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

> **Experimental.** The converter supports inline/named/file/URL data, the documented transforms, parameter expressions and selections, and layer/facet/concat/repeat composition. Unsupported expression syntax, helpers, or transforms return a diagnostic; use Lambda data preparation for requests outside the supported subset.

### 4.2 API

| Function | Description |
|----------|-------------|
| `chart.render(chart_el)` | Renders a `<chart>`, `<hconcat>`, `<vconcat>` or `<repeat>` element as an `<svg>` element |
| `chart.render_spec(spec, viewport = null, st = null)` | Renders a specification map or native element; optional parameter-state snapshot derives an interactive SVG presentation |
| `chart.model(spec, viewport = null, options = {})` | Creates a chart source element; retain and `apply` it to preserve chart state across parent rerenders |
| `chart.interactive(spec, viewport = null, options = {})` | Applies a Lambda view template with independent interaction state, SVG, and bound input controls |
| `chart.render_frame(spec, frame, viewport = null, st = null)` | Samples SVG at an explicit `time_ms`; a `previous` specification or SVG supplies the source of an update transition (S12.1.1v2) |
| `chart.figure(spec, viewport = null)` | Constructs a 2D SVG or native 3D scientific figure; see [supported marks and limits](Lambda_Chart_Plot.md) |
| `chart.figure_frame(spec, frame, viewport = null)` | Existing 2D sampling; static figures and exact timeline endpoints for 3D |
| `vega.convert(vl)` | Converts a parsed Vega-Lite document (a map) into a specification map for `render_spec` |

### 4.3 Specification elements

| Element | Attributes and children |
|---------|-------------------------|
| `<chart>` | `width` (default 400), `height` (300), `padding` (a number or a `{top, right, bottom, left}` map, default 20), `title`, `clip`, `projection`, and the children below |
| `<data>` | Inline `values: [...]` or row children; `name` selects the chart's `datasets` map; `url` and optional `format` use Lambda input loading |
| `<mark>` | `type` (or `kind`): `bar`, `line`, `area`, `point`, `text`, `rule`, `tick`, `rect`, `arc`, `boxplot`, `errorbar`, `errorband`, `wordcloud`, `violin`, `slope`, `trail`, `image`, `radar`, `parallel`, `treemap`, `sunburst`, `geoshape` (`geo`), `link`, `polygon`, `path`, `vector`, `sankey`, `chord`, `tree`, `pack`, `force_graph`, `funnel`, `gauge`, `liquid`, `density`; `point` also supports `beeswarm: true`; an unknown type draws points. Style attributes: `color`, `opacity`, `fill`, `stroke`, `stroke_width`, `stroke_dash`, `interpolate`, `point`, `corner_radius`, `inner_radius`, `outer_radius`, `pad_angle`, `size`, `shape`, `font_size`, `font_family`, `font_weight`, `clip` |
| `<encoding>` | One child per channel: `<x>`, `<y>`, `<x2>`, `<y2>`, `<color>`, `<size>`, `<opacity>`, `<theta>`, `<theta2>`, `<radius>`, `<radius2>`, `<longitude>`, `<latitude>`, `<text>`, `<stroke>`, `<x_offset>`, `<detail>`, `<tooltip>`, `<shape>`, `<order>`, `<url>`, `<position>`, `<key>`, `<group_key>`, `<path>`, `<direction>`, `<magnitude>`, and phase timing channels such as `<update_duration>`. Each takes `field`, `dtype` (`quantitative`, `nominal`, `ordinal` or `temporal`; `geojson` for a shape field), `value`, `datum`, `title`, `aggregate`, `bin`, `time_unit`, `stack`, `stack_order`, `sort`, `zero`, `scale`, `axis`, `legend`, `format` and `condition` |
| `<coordinate>` | `type`: `cartesian`, `polar`, `theta`, `radial`, `parallel`, `radar`, `helix`, or `geo`; normalized radii, angular bounds, and an ordered `transform` array |
| `<interaction>` | Named behavior children such as `<element_select param: "picked">`; false disables inherited behaviors |
| `<timeline>` | `<keyframe at: ..., spec: ...>` children; `autoplay`, `duration`, `repeat`, `direction`, and `fill` |
| `<params>` | `<param name: ...>` declarations, or supply a chart `params` array; variables, point/interval selections, and bindings |
| `<transform>` | Ordered `filter`, `sort`, `aggregate`, `joinaggregate`, `calculate`, `bin`, `timeunit`, `fold`, `flatten`, `pivot`, `impute`, `stack`, `quantile`, `window`, `lookup`, `density`, `regression`, and `loess` steps; analytical options are described below |
| `<config>` | `theme`: `light`, `dark`, `minimal`, `presentation`, or a custom map; nested `mark`/mark-type/`axis`/`legend` settings or equivalent prefixed attributes; `font`; `axis_grid: true` for horizontal grids |
| `<layer>` | `<chart>` children drawn over one another |
| `<facet>` | `field` with wrapping `columns` (default 3), or `row`/`column` field definitions; `spacing` (default 20); common scale domains by default |
| `<annotation>` | `<text_note x, y, text, color, font_size, anchor, dx, dy>`, `<rule_note x or y, color, stroke_width, stroke_dash>`, and `<region_note x, x2, y, y2, color, opacity, text>` children |
| `<hconcat>`, `<vconcat>` | `spacing` (default 20); `<chart>` children placed side by side or stacked |
| `<repeat>` | A `<row [fields]>` and/or `<column [fields]>` child plus one `<chart>` template whose channels say `field: {repeat: "row"}` or `field: {repeat: "column"}` |

The native map grammar additionally accepts `mark: {kind: "composite", expand:
factory, parts: {...}}`. A pure factory returns a finite layer composition;
named parts share encodings, visual states, behaviors, and animation.
`interaction` presets compile into ordinary chart parameters. `state` supplies
default, active/inactive, and selected/unselected styles. `animate` supplies
keyed enter/update/exit transitions and ordered group timing. `timeline`
provides keyframe views with play, pause, resume, seek, reverse, and cancel
commands. Live clocks and selection stores belong to the chart view template;
handlers receive `chart_interaction`, `chart_change`, and `chart_animation`
notifications (S9.1.4, S12.1.3). Pass `{reduced_motion: true}` as the third
argument of `model`/`interactive` for immediate target presentation.

See the [consolidated chart design](../vibe/Lambda_Pkg_Chart.md) for graph input,
coordinate compatibility, behavior options, derived density records, and
animation identity/fallback contracts. The
[chart dashboard](../test/lambda/chart/chart_dashboard.ls) showcases the chart
families, coordinate modes, wordcloud, visual states, and frozen transitions.
The [live gallery](../test/lambda/chart/_interactive_gallery.ls) demonstrates
related graph selection, polar/field-axis brushing, geographic navigation,
and chart-local playback controls.

A `violin` mark mirrors a category's estimated density; `density_field`
accepts precomputed densities. A `slope` mark connects each detail entity's
pair of comparison positions. A `trail` mark uses `size` as pixel width
(default scaled range `[1, 10]`) and supports linear interpolation with round
joins; its size legend shows widths. An `image` mark takes a `url` channel,
mark URL, or row URL, plus pixel `width`/`height` or x2/y2 bounds. These marks
retain styles, tooltips, clipping, and composition. See the
[mark design](../vibe/Lambda_Pkg_Chart.md#distribution-comparison-and-image-marks)
for validation and options (S7.4.1).

A `radar` uses categorical `theta` and quantitative `radius`, with one row per
category in each color/detail series. `filled: true` fills the closed polygon.
A `parallel` declares at least two `fields`, each a field name or
`{field, title, scale, axis, format, zero}`; each axis has its own scale.
An `arc` with `theta2` draws angular intervals; `radius`/`radius2` supply radial
bounds. Disabled scales use radians and pixels directly. See the
[radial and multi-axis contract](../vibe/Lambda_Pkg_Chart.md#radial-and-multi-axis-charts).

`treemap` and `sunburst` accept flat hierarchy records, with `node_field`,
`parent_field`, and `value_field` defaulting to `id`, `parent`, and `value`.
Leaves have nonnegative weights (default one); internal weights sum children.
Null parents identify roots; unknown parents, duplicate IDs, and cycles are
errors. `node_padding`/`header_height` control treemaps;
`inner_radius`/`outer_radius` control sunbursts. Measured labels and the
original rows' color/tooltips remain available. See the
[hierarchy contract](../vibe/Lambda_Pkg_Chart.md#hierarchical-charts).

`geoshape` accepts GeoJSON FeatureCollections, Features, geometries, or records
with a `geometry` field. Feature properties are available directly to ordinary
color and tooltip encodings. Longitude/latitude channels can supply points.
Mark or chart `projection` selects `mercator`, `equirectangular`, `albers`,
`orthographic`, or `natural_earth`, with optional center, scale, translation,
padding, precision, and Albers parallels. Polygon holes and projection clipping
are preserved. See the [geographic contract](../vibe/Lambda_Pkg_Chart.md#geographic-charts)
for geometry rules and boundaries (S7.4.1).

A pie or donut chart is an `arc` mark with a `theta` channel, plus `inner_radius` for a donut; grouped bars use an `x_offset` channel. Aggregate operations are `count`, `sum`, `mean` (or `average`), `median`, `min`, `max`, `distinct`, `q1`, `q3`, `stdev` and `variance`. A colour channel picks a palette with `scale: {scheme: "set1"}`: `category10` is the default for categories and `blues` for quantities, and `category20`, `set1`, `pastel1`, `dark2`, `greens`, `reds`, `oranges`, `purples`, `greys`, `red_blue` and `spectral` are also available. `scale: {domain: [...], range: [...]}` assigns colours explicitly.

Additional palettes are `set2`, `viridis`, `plasma`, `inferno`, and `magma`.
Quantitative `scale: {domain: [-10, 30], domain_mid: 0}` maps the explicit
midpoint to a diverging palette's center. A categorical `sort: {field: "sales",
op: "sum", order: "descending"}` orders categories by grouped totals, with
stable ties (S6.2.3).

Size and shape encodings produce legends as color does. Legend settings
include `orient`, `direction`, `columns`, `values`, `format`, and symbol
appearance. Arc legends honor placement. Numeric labels share formats such
as `,.2f`, `.1%`, `.3e`, `.3g`, and `.3~s` across guides, text, and tooltips.
Layered charts can request `resolve: {scale: {y: "independent"}}`; shared
scales include full stacked extents. Resolution reaches nested layers,
facets, concatenations, and repeats; an inner independent request starts a
new group. `resolve.axis` and `resolve.legend` choose shared or independent
guides; a shared guide appears in the first eligible view, and independent
scales always have independent guides. All composition forms inherit data,
encodings, datasets, and configuration; transforms run once at their own
boundary. See the [composition design](../vibe/Lambda_Pkg_Chart.md#7-chart-composition)
for defaults and common scale policy.
`clip: true` on a chart or mark clips
plotted content without clipping its axes or legends.

Area charts support `stack: "wiggle"` on y for streamgraphs. Values must be
finite and nonnegative; x, y, and color fields are required. Missing series/x
samples contribute zero, and duplicates add. Numeric and temporal x values
are ordered ascending; categorical x follows its domain or sort order.

The quantitative channel's `stack_order` accepts a series-value array,
`"none"`, `"reverse"`, `"ascending"`, `"descending"`, or `"inside_out"`.
It overrides the color-domain or color-sort order and preserves palette
assignments. Wiggle defaults to inside-out when those explicit orders are
absent. Ascending ties retain input order; descending reverses the full
ascending order (S6.2.3). Invalid controls or wiggle data return value errors
(S7.4.1). See the [stacking design](../vibe/Lambda_Pkg_Chart.md#stacking-and-grouping)
for the full contract.

Mark `fill`, `stroke`, and `color` also accept gradient and hatch maps. A
gradient is `{gradient: "linear" | "radial", stops: [{offset, color, opacity?}, ...]}`;
linear direction uses `x1`/`y1`/`x2`/`y2`, and radial gradients additionally use
`r1`/`r2`. Coordinates are relative to the mark's bounding box. The default
linear direction runs left to right; radial defaults run from the center to
radius `0.5`. Optional `spread` selects `pad`, `repeat`, or `reflect`.
Offsets and stop opacity lie in `[0, 1]`; stops must be ordered.

`{pattern: "hatch", color: "black", spacing: 8, angle: 45, stroke_width: 1}`
draws repeating stripes. Optional `background` fills the tile, `opacity`
controls stripe opacity, and `cross: true` adds perpendicular stripes.
Gradients and hatches work through conditional color values, identity color
fields, and categorical palette ranges, including their legend symbols.
They also work on annotations, backgrounds, and titles, and across chart
compositions. Invalid paint maps return value errors (S7.4.1). See the
[chart design](../vibe/Lambda_Pkg_Chart.md#gradient-and-hatch-paints) for full
defaults and examples.

Temporal scales use calendar-aligned ticks. `scale.timezone` accepts a fixed UTC
offset in minutes or an IANA name such as `"America/New_York"` (default zero).
The pinned IANA 2026e rules make daylight-saving days and repeated/skipped
hours independent of the host's local zone (S12.1.1v2).
`time_unit: "yearmonth"` on a channel groups
records before encoding aggregation; cyclic `month`, `day`/`weekday`, and
`hours` are also supported, and an `utc` prefix selects UTC. A direct
`<timeunit field: "date", unit: "yearmonth", as: "month">` transform retains
the source field; its `timezone` accepts the same names and offsets.
`Z`/`ZZ` label tokens show the actual offset. See the
[chart design](../vibe/Lambda_Pkg_Chart.md#5-encodings-scales-and-color) for
local-time gap and overlap rules.

Chart axes, legends, titles, and facet headings use measured SVG font bounds.
The default family is `"Arial"`; chart `font` and guide `label_font_*` /
`title_font_*` settings control both measurement and output. `label_limit` fits
text and an ellipsis into a pixel width, preserving whole grapheme clusters
(including combining marks, emoji sequences, and flags).
`label_overlap: "hide"` culls against measured positions on either axis and
across axes, titles, legends, and text annotations within a view;
`label_separation` sets the gap. Text annotations accept the same overlap
controls, font family/size/weight/style, and letter/word spacing. Fixed text
retains its authored position; optional notes precede optional axis labels.
Plot clipping removes invisible annotation bounds from collision checks.
Legend rows and margins accommodate the
selected fonts. Different available fonts can produce different geometry;
complex-script shaping follows Radiant's current SVG support.

The direct read-only API `radiant.measure_text(requests, faces)` accepts an
array of `{text, font}` records. `font` contains `font_family`, `font_size`
(CSS pixels), `font_weight`, `font_style`, and optional `letter_spacing` /
`word_spacing`. `faces` is `null` for installed fonts, or an array of
`{font_family, data: binary, font_weight?, font_style?}` snapshots acquired by
Lambda IO (**D7.1.2v2**). Chart labels use this API without constructing HTML.
Results include advance/width, height, baseline, separate `ink` and `logical`
bounds, and `resolved_fonts` (the actual face families used, including fallback).
An empty string has zero bounds and an empty font list. Placement and fallback
are shared with Radiant's SVG painter; this does not add a new shaping engine.
`radiant.font_metrics(font, faces)` returns ascent, positive descent, line gap /
height, x-height, cap-height, space width, units per em, underline thickness,
OS/2 subscript/superscript baseline offsets, and resolved family.
Both APIs return copied values or `null` on invalid input/failure
(**D4.2.2v2, D7.4.6, S12.1.1v2**). Batch-local font caches expire with the query.

The existing read-only host function
`radiant.measure_svg_text(html, width, height)` measures each direct `<text>`
child of the first SVG in the HTML body, preserving request order. Other SVG
children can supply font resources. It returns copied `width` (advance),
`height`, `baseline`, and baseline-relative `left`, `top`, `right`, `bottom`
bounds covering character cells and visible glyphs. Empty text has zero
metrics; invalid input or failed measurement returns null. Viewport dimensions
must be positive integers. This follows **D7.4.6** / **S12.1.1v2**; only scalar
facts escape the transient document under **D4.2.2v2**.

`radiant.graphemes(text)` returns the original text as an array of extended
grapheme clusters, following the bundled Unicode segmentation data. Empty
text returns `[]`; invalid UTF-8 returns null. It preserves spelling without
normalization and is read-only under **D7.4.6** / **S12.1.1v2**. Chart truncation
uses these boundaries; ordinary `split` retains its **S17.1.1** contract.

Both `chart.render(spec, viewport = null)` and `chart.render_spec(spec, viewport = null)`
accept optional `{width, height}` container dimensions. Use `width`/`height`
as `"container"` or `"auto"`, or request a discrete `{step: pixels}`.
`aspect_ratio` derives an automatic dimension. Rendering with new dimensions
recomputes marks and guides; it remains pure (S12.1.1v2). Explicit numeric
dimensions retain precedence. Concat/repeat distribute available space;
facet dimensions describe cells. See the
[sizing contract](../vibe/Lambda_Pkg_Chart.md#sizing-and-available-space).

#### Analytical transforms

`lambda.chart.transform.apply_transforms(rows, steps, datasets = null)` also
applies the transform vocabulary directly. `steps` is a `<transform>` element
or an array of maps carrying `type`. Invalid options return value errors
(S7.4.1); later steps stop at the first diagnostic.

| Step | Options and result |
|---|---|
| `<window>` | `groupby`, multi-field `sort`, inclusive `frame` (default `[null, 0]`), `ignore_peers`; `<agg op, field, as, param>` children add aggregate, ranking, lag/lead, or first/last/nth-value fields without changing source order |
| `<joinaggregate>` | `groupby` and `<agg>` children, or a `joinaggregate` operation array; attach whole-group summaries while retaining source rows |
| `<pivot>` | `pivot`, `value`, `groupby`, `op` (default `sum`), `limit`; turn field values into aggregate columns |
| `<impute>` | `impute`, `key`, `groupby`, `keyvals`, `method`, `value`, `frame`; fill null fields and missing group/key combinations |
| `<stack>` | `stack`, `groupby`, `sort`, `offset: "zero"/"center"/"normalize"`, `as`; add start/end fields without replacing source rows |
| `<quantile>` | `quantile`, `groupby`, `probs` or `step`, `as`; emit empirical probability/quantile records |
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

Filter `test`, calculate `expression`, and channel condition `test` accept
Vega expression strings as well as pure Lambda callbacks (S12.1.1v2).
Expressions bind declared parameter values and `datum` and support nested fields, literals, operators,
short-circuit conditions, and common mathematical, type, string, and array
helpers. Missing fields remain distinct from null inside expressions.
Unknown syntax/helpers and evaluation failures return value errors
(S7.4.1), including invalid expressions with empty data. Event selectors also
bind `event`. Assignments, method calls, signals, and ambient globals are
outside this expression subset.
See the [expression contract](../vibe/Lambda_Pkg_Chart.md#color).

Running-sum windows followed by a calculation of each prior total produce
waterfall charts with ordinary `bar` marks and `y`/`y2` ranges. Density and
fitted-trend outputs use ordinary line/area marks. See the
[chart design](../vibe/Lambda_Pkg_Chart.md#6-data-transformations) for full contracts
and a runnable waterfall example.

### Declarative interactions

Use `chart.interactive` to display a stateful chart in a Lambda view. It adopts
Vega-Lite's `params` grammar. Parameter values and per-view selection stores
belong to each applied chart template; `on` handlers update them and rendering
stays pure (S9.1.4, S12.1.3, D6.2.3v2).

```lambda
import chart: lambda.chart.chart
import vega: lambda.chart.vega

let spec = vega.convert({
    data: {values: [{group: "A", x: 2, y: 3}, {group: "B", x: 7, y: 8}]},
    params: [{name: "picked", select: {type: "point", fields: ["group"]}}],
    mark: "point",
    encoding: {
        x: {field: "x", type: "quantitative"},
        y: {field: "y", type: "quantitative"},
        color: {condition: {param: "picked", empty: false, value: "red"}, value: "grey"}
    }
});
<html <body chart.interactive(spec)>>
```

Native markup accepts the same `params` array or a `<params <param ...>>`
child. Ordinary `chart.render` produces the initial static snapshot. For a
chart nested in a parent that rerenders, create `chart.model(spec)` once, keep
that source value in the parent model, and call `apply(~.chart)` in its body.
This retains state on the same chart instance (S9.1.4); see the
[source/template identity contract](../vibe/Lambda_Design_Reactive_UI.md#5-instance-state-and-identity).

- `select: {type: "point"}` selects hit records. `fields` or `encodings`
  projects the stored tuples. Click replaces; Shift-click toggles by default.
  `nearest: true` supports nearest-point hover with `on: "pointerover"`.
- `select: {type: "interval", encodings: ["x"]}` creates a horizontal brush.
  Omit `encodings` for both positional axes. Drag within the brush translates;
  the wheel zooms. `clear` defaults to `"dblclick"`; `false` disables clearing.
- `{filter: {param: "brush"}}` links a view's data to a selection, including
  sibling views. Conditions use the same `param` predicate. Empty selections
  match all records unless `empty: false`. `resolve` accepts `"global"`,
  `"union"`, or `"intersect"` for selection stores from multiple views.
- Variable declarations use `{name, value}` or read-only `{name, expr}`.
  Expressions can reference declared parameter names, and option values can
  use `{expr: "..."}`.
- `bind: {input: "range", min: 0, max: 100, step: 1}` generates a control.
  Text, number, checkbox, select, and radio inputs are also supported; point
  selections can bind each projected field. `bind: "legend"` selects from a
  categorical legend. Interval `bind: "scales"` enables continuous-scale pan
  and pointer-centered zoom.

Event selectors support filters such as `"click[event.shiftKey]"`, unions,
and captured interval streams such as
`"[pointerdown, window:pointerup] > window:pointermove"`. A chart emits
`chart_change` with projected parameter values to a handling ancestor. A
`chart_parameter` event delivered to the chart accepts `param`, `value`, and
an optional projected `field`, directly or in custom event `detail`, for
programmatic updates. Wordcloud words
participate in point selections through their source records.

See [interaction contracts and remaining compatibility gaps](../vibe/Lambda_Pkg_Chart.md#10-declarative-interactions)
for projection, initialization, binding, and event-stream details. Timed
streams, general external/window subscriptions, external DOM input bindings,
and rich HTML tooltip overlays are not implemented.

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
It ships as Lambda source under the same package namespace (D7.2.1–D7.2.4v3).
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

### 4.5 Historical and event timelines

`lambda.chart.timeline` presents historical events as chronological cards, a
time axis, or an active story. It uses the ordinary source-package and view
mechanisms (**D7.2.1–D7.2.4v3/D7.2.6, S2.4.3v3/S12.1.3**).
Implementation is in progress against the
[timeline proposal](../vibe/Lambda_Pkg_Chart_Timeline.md); the
[acceptance inventory](../test/timeline/reference_inventory.json) records open
features and host coverage. Native Radiant is the current interactive host.
The revised proposal excludes browser delivery, automatic resource lifecycle,
export, and accessibility/localization gates; existing compatibility APIs remain.

```lambda
import timeline: lambda.chart.timeline

timeline.render({id:"project-history",view:"cards",placement:"alternate",
    events:[
        {id:"idea",at:"2019-03",title:"The idea",content:"A small beginning."},
        {id:"launch",at:"2022-09",title:"Launch",content:"The first public release."}
    ]})
```

Elements use `<timeline.chart>` with qualified children such as
`<timeline.event>`, `<timeline.scale>`, and `<timeline.cards>`. Maps and elements
normalize to the same schema. The package supports astronomical BCE/CE dates,
partial dates, exact numeric deep time, explicit timezones, duration events,
eras, nested groups, hidden intervals, curved coordinates, and quantitative
chart overlays. Rendering consumes explicit state and resource results;
procedural handlers own effects (**S12.1.1v2, S9.1.4/S9.3.1**).

Retained models reuse source normalization and an exact interval index across
window, selection, and playback changes. Query/filter/group changes rebuild
visibility while retaining the source index. Layout clusters projected candidates
before measuring rich cards; `plan.work` reports index use, examined records,
candidates, and measured cards. Dense identical-date clusters still examine all
their members (**S6.2.2v3/S6.2.3**).

Set `animate: true` or `animate: {duration: 300, delay: 0, easing: 'linear',
enter: true, update: true, exit: true, guides: true}` to animate keyed event
replacement, geometry, windows, and guides. Interruption starts from the displayed
geometry and opacity; outgoing cards retire at completion. `motion: 'reduced'`
or `'none'`, or `model(spec, viewport, {reduced_motion: true})`, settles immediately.
For a deterministic sample, `render_frame(spec, {time_ms: 100, previous: {plan,
svg}}, viewport)` consumes an explicit previous snapshot
(**S12.1.1v2/S12.1.3, S9.1.4/S9.3.1**).

Open the native [animation sample](../test/ui/timeline/keyed_motion.ls) with
`./lambda.exe view test/ui/timeline/keyed_motion.ls`. Additional pages show
[rich cards and dtna styling](../test/ui/timeline/cards.ls),
[clustering](../test/ui/timeline/cluster.ls), and
[dark-theme presentation](../test/ui/timeline/presentation.ls), and a
[10,000-event chronology](../test/ui/timeline/scalability.ls).

`search.highlight: true` preserves rich inline elements and their attributes,
including matches split across element boundaries. Case and diacritic options
retain authored Unicode text. Clearing the query restores the source content.
Adjacent rich children and scalar text share the same offsets for matching and
highlighting; separate tags never form a combined match. Script, style, and
template contents are excluded. Render callbacks receive the same chart/locale
context as cards; callback errors and non-boolean predicate results are diagnosed.
The pure host helper `radiant.with_content(source, children)` replaces only an
element's normalized content, retaining its qualified name, nominal identity,
and full attribute contracts. Declared content patterns still apply; invalid
replacements return a value error. `radiant.present_content(source, children)`
instead constructs a structural presentation with the same qualified tag and
attribute values; its new content is unconstrained. Highlighting uses that
operation so string-only source types can render marks without changing their
source or claiming that the presentation is still a nominal instance
(**S2.1.4, S2.6.2–S2.6.4, S17.4.1, D4.5.2**).

Filtering immediately reconciles the active event, keeps selection, and restores
the nearest visible event after an empty result. Collapsing a focused group moves
focus to its visible group header. Controlled fields remain authoritative and
emit change requests (**S9.1.4/S9.3.1, S12.1.3**).

`locale` controls bundled English, Chinese, French, and Arabic messages, dates,
era labels, numeric interpolation, export headings, and default writing direction.
`direction` overrides the locale; a scale's `locale` overrides inherited date
formatting. `messages` supports strings, pure callbacks, and plural form maps;
`scale.format` and `scale.formatter` customize labels without changing chronology.
Unsupported languages use deterministic English fallbacks; further reference
language coverage remains open (**S12.1.1v2/S12.1.3**).

| Entry point | Purpose |
|---|---|
| `validate(spec)` | Normalize and validate an element or map; return a value error on invalid input. |
| `render(spec, viewport?, snapshot?)` | Pure HTML/SVG presentation. |
| `model(spec, viewport?, options?)` | Stable logical source for `apply()` and native interaction. |
| `interactive(spec, viewport?, options?)` | Apply a model with instance-owned interaction state. |
| `initial_state(spec)`, `update(state, action, context)` | Immutable state and a shared reducer returning state, notifications, and requests. |
| `apply_edit(spec, request)` | Recheck permissions, token source revision, and the proposed patch; return a validated immutable source candidate. |
| `bookmark(spec, id)` | Encode a stable event/introduction address for the chart's URL fragment. |
| `layout(spec, viewport, snapshot?, resources?)` | Measured geometry and chronology projection. |
| `render_svg(spec, viewport, snapshot?, options?)` | Chart-vector presentation with an explicit rich-content conversion policy. |
| `render_frame(spec, frame, viewport?, snapshot?)` | Presentation at an explicit slideshow time in milliseconds. |
| `convert(data, format, options?)` | Convert already loaded TimelineJS, React Chrono, vis, JSON, CSV/TSV, or spreadsheet records. |
| `capabilities(host?)` | Report host/media capabilities. |
| `load(request, options?)` | Procedurally fetch a requested resource, retaining its token and revision. |
| `export_data(spec, format?, options?, snapshot?)` | JSON records, CSV text, XLSX ZIP tree, or accessible HTML table. |
| `export_document(spec, format?, viewport?, options?, snapshot?)` | Frozen HTML or SVG source; HTML also supplies PNG/JPEG/PDF rendering. |
| `save(spec, destination, format?, viewport?, options?, snapshot?)` | Procedurally write HTML, SVG, PNG, JPEG, PDF, JSON, CSV, or XLSX. |

Exports select `scope: "full"`, `"current"`, `"window"`, or `"selected"` and
accept explicit state/time, expansion, field order, headings, and media frames.
Live media needs a poster, recorded frame, authored fallback, explicit external
resource policy, or `unsupported: "text"|"omit"`. Source export retains authored
date precision and exact metadata. Native vector export preserves the rich card
presentation; the direct chart-vector API keeps its explicit conversion policy.

Story mode uses one sequence for initialization, first/previous/next/last,
quick jump, keyboard navigation, and slideshow playback. `story.order` is an
array of unique event IDs. `story.title_slide: true` uses the chart title and
description; an event record supplies a custom introduction, rich content, and
media. Its default ID is `"__title__"`; give it another ID if an event uses that
name. Introductions have no chronology coordinate and stay outside exported
event records. `story.panel_order` orders the `"text"`, `"media"`, and
`"children"` sections. Slideshow `delay` and `transition_duration` are
milliseconds; frozen frames use the same delay, easing, and transition sample
as live playback (**S12.1.1v2/S12.1.3**).

`story.bookmarks: true` requires an authored chart ID and stable event IDs. Links
use `#timeline=<encoded-chart-id>/<encoded-event-id>`; UTF-8 and reserved
characters are form-encoded. Native hosts restore external fragment changes
and back/forward traversal automatically. A pure `{type:"bookmark",fragment:...}`
action uses the same resolver; unrelated chart fragments are ignored, and an
unknown event in this chart's address returns a value error. Controlled active
state emits a change request before a committed activation can write the URL.

`slideshow.media_end: true` waits for audio/video completion at a slide boundary.
`media_timeout` bounds the wait (default 30,000 ms), and pause freezes its budget.
`media_command` requests carry event/media IDs, a session token, and source
revision. An application player replies with a `media_event` action containing
those same fields and `status:"ended"|"error"`. Retired tokens are ignored.
`media_options.active_playback` can start active media independently of slideshow
playback. Native hosts execute local audio/video commands after DOM rendering,
poll completion and authored clip ends, and report `timeline_media` notifications
with the accepted session identity and status. Native decoder state survives
rerenders and resize; source changes replace it. `model(spec, viewport, {media:false})`
leaves command execution to the application. Inferred or explicit remote
providers use application controllers. Native decoding remains macOS-specific;
cross-platform and hosted-player parity are still open (**D7.5.3, D4.5.1v4**).

A retained native model accepts a dispatched `timeline_action` with
`{type:"source_update", spec:next_spec, expected_revision:old_revision}`.
The chart ID must stay the same. Resource requests carry revision/token guards,
deadlines, and explicit cancellation requests; refresh keeps the last valid
dataset while its replacement loads. Applications currently own asynchronous
execution and disposal of those requests (**S9.1.4, S13.1.1v2–S13.1.5**).

The pure `update` API accepts the same source action and returns the accepted
`source` alongside state, requests, and notifications. A stale expected revision
returns the existing state before validating the replacement. Removed activation
chooses the nearest surviving event in the previous chronology; selection prunes
removed IDs and emits its normal notification. Focus follows a removed focused
event while external control focus stays in place. A retained playing/paused
slide keeps its fractional playhead when order or duration changes, and moving
an active dated event can scroll the unchanged window span into view.
Loaded-data completions use the same policy; subsequent actions resolve IDs from
the recorded dataset. Changed records retire pending edits even when their
authored revision is unchanged, preventing a late validation from applying an
obsolete proposal.

The DTNA timeline translates its compatibility props and supplies its skin over
this package. Its `timeline_options` property exposes the shared specification.
Animation keyframes belong to `lambda.chart.playback`; the seven previous
animation functions exported from `lambda.chart.timeline` remain forwarding
aliases. Historical `<timeline.chart>` and contextual chart `<timeline>`
keyframes have distinct schemas.

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
let svg = transform.render_svg(g, 480, 160)
type(svg)                                    // string
```

Radiant automatically connects the `lambda-graph` layout to the package callback when it encounters the generated HTML (**D7.5.3v2**); no installation call is needed. This also works when serialized HTML is opened in a fresh Radiant process. Other HTML renderers do not implement this layout, so the HTML from `to_html` is meant for Radiant. From the command line:

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

The math package typesets a parsed math tree as self-contained inline SVG,
using SVG text with the selected font and optional OpenType MATH data (**RAD07-L3**). The default reuses
bundled CMU Serif and the existing small KaTeX symbol/alphabet faces; geometry
comes directly from those fonts. No new font asset is distributed.
[Math_Support.md §2](Math_Support.md#2-rendering-math-to-html) documents the API.

```lambda
import math: lambda.doc.math.math

let ast = parse("\\sqrt{x^2 + 1}", 'math')^
let el = math.render_display(ast)
el.class                              // "lambda-math"
contains(format(el, 'html'), "<text") // true
```

| Function | Description |
|----------|-------------|
| `render_math(ast, options)` | Renders with `display`, `color`, `font_family`, `fonts`, and optional CSS-pixel `font_size` |
| `render_box(ast, options)` | Returns the emitted element and full-precision width/height/depth in em |
| `render_box_element(box)` | Emits an already measured box without remeasurement |
| `render_inline(ast)` | Inline (text) style |
| `render_display(ast)` | Display (block) style |
| `render_standalone(ast)` | Self-contained display style SVG |
| `stylesheet(options = null)` | Returns `""`; each SVG embeds its required font declarations |
| `to_mathml(ast, options = null)` | Presentation MathML element tree; optional `display` and inert TeX `source` annotation |
| `parse_mathml(source)` | Strict MathML XML tree with namespaces validated and token whitespace preserved |

The font-independent `lambda.doc.math.mathml` module exposes the same conversion
and parsing APIs without importing Radiant. Runtime
`format(ast, {type: 'math', flavor: 'mathml'})` and CLI `-t math-mathml`
use its exporter. It supports common fractions, scripts/limits, roots,
groups/fences, alphabets, simple text, accents, spacing, phantoms and matrices;
unsupported TeX constructs report errors. The renderer also accepts a documented
Presentation MathML subset through its separate Core policy, sharing measured
SVG geometry with TeX. Radiant paints that subset in HTML while preserving the
MathML DOM; full CSS and accessibility integration remain open (**D7.2.8**).

`fonts` uses the same binary face records as Radiant's metrics API. An explicit
`font_family` without `fonts` uses installed fonts. MATH is optional: ordinary
fonts use their measured geometry and MathML Core fallback layout constants;
italic/bold letters resolve the corresponding ordinary style faces when the
Unicode math alphabet is absent. Symbols missing from the face use normal
font fallback, retaining the resolved face and style in SVG text. Bundled and
supplied font bytes are embedded as `@font-face` rules for the faces actually
used. Unencoded OpenType stretch variants and assembly parts retain paths;
rules remain rectangles. Explicit installed fonts must also be available to
the viewer.
`radiant.math_metrics(font, codepoints, faces)` exposes copied glyph
metrics/outlines and normal `font_metrics` for every usable face. `has_math`
reports table availability; `constants` is `null` when absent. When available,
it additionally exposes MATH constants, italic corrections, accent attachments,
corner kerns, variants and assembly connectors. `font.fallback: true` enables
codepoint fallback (off by default), and each glyph reports its actual family, weight, style and size.
Invalid font data returns `null`; a codepoint with no glyph returns a null entry.
Distances use the requested CSS pixel size; MATH percentages remain percentages.
Algorithms stay in Lambda and font parsing stays in `lib/font` (**D7.1.1**).

The legacy `lambda.doc.math.mathlive` renderer and its fixed metrics tables
have been removed; use `lambda.doc.math.math`.

`lambda view`, `lambda layout` and `lambda render` typeset the `$…$` and `$$…$$` math of Markdown documents through this package, and the `latex` package uses it for the math inside LaTeX documents.

---

## 7. `latex` — LaTeX to HTML

The latex package renders a parsed LaTeX document as HTML. Parse with `latex.parse_file(path)` or `latex.parse_source(text, base)`. Every LaTeX parse first runs Lambda's TeX expansion engine (**D7.2.7v2**): document macros, `.sty`/`.cls` files beside the document, bundled programming packages such as `etoolbox`, `xkeyval` and expl3, and Lambda's own bindings for `physics`, the `mathtools` macros, `xspace`, `csvsimple`, `unicode-math`'s alphabets and the configuration macros of `tocloft`, `authblk`, `titling`, `minitoc`, `glossaries`, `nomencl` and `acronym` run as TeX, while Radiant keeps layout. A bare `input(path, 'latex')` also expands, but without the package's adapter registry, so parse through the package when you render. The package covers the `article`, `book` and `report` document classes and profiles of the common others (KOMA-Script, memoir, the AMS classes, IEEEtran, llncs, elsarticle, revtex, acmart, `letter` and `exam`), sectioning with numbering, lists, tables, footnotes, boxes, colour, spacing, the `picture` environment, `\newcommand` macros, and math through the `math` package.

Common `\usepackage` declarations select the shipped script adapters for `amsmath`, `amssymb`, `graphicx`, `hyperref`, `geometry`, `xcolor`, `booktabs`, `biblatex`, `babel`, `enumitem`, `microtype`, `siunitx`, and TikZ. Package names select static source modules under **D7.2.1–D7.2.4v3**; TeX source runs only as TeX inside the engine's sandbox and never becomes Lambda code (**S1.8, D7.2.7v2**). The [package compatibility matrix](../vibe/Lambda_Pkg_Latex.md#9-compatibility-matrix) lists supported commands and output limits.

The bounded `biblatex` profile reads multiple local `.bib` resources, supports ten common entry types, six built-in citation/bibliography styles, source-order citation commands, `\nocite`, filtered and scoped `\printbibliography`, and English/German/French strings. It handles common BibTeX strings and inheritance, creates internal citation links, and reports missing or unsupported requests with source locations (**D7.1.2v2, S12.4.1, S7.4.1–S7.4.4**). `backend=biber` selects this local profile; no Biber process or arbitrary style file is run. See [Phase 2 behavior and boundaries](../vibe/latex/Lambda_Design_BibTeX_CSL.md#21-the-built-in-biblatex-and-natbib-profile).

A document that declares `\usepackage[style=apa]{biblatex}`, `style=ieee`, or `\usepackage[authordate]{biblatex-chicago}` (or its default notes style) is formatted by the matching bundled CSL style automatically. The `csl-style-alias` diagnostic names the CSL style used, and biblatex options or style customizations that CSL cannot follow are reported rather than applied. An experimental CSL processor is also available directly through `render_result(ast, {base_uri: ".", citeproc: {style: "apa", locale: "en-US"}})`. It is implemented entirely in Lambda script within `lambda.doc.latex` (**D7.2.1–D7.2.4v3, S1.8, S12.4.1v2**). It accepts local BibTeX/BibLaTeX or CSL JSON resources, evaluates custom CSL XML, and bundles `ieee`, `apa`, `chicago-author-date`, and `chicago-fullnote-bibliography` styles plus English/German/French locales. It preserves citation links, scoped prints, and note-style footnotes. CSL mode rejects conflicting legacy style/sorting options and reports unsupported constructs. Locale collation and complete CSL conformance remain outstanding. See the [CSL API, resource options and coverage](../lmd/package/doc/latex/citeproc/README.md).

For PDF export, `hyperref` links and anchors become link annotations and destinations, and its title, author, subject and keywords become PDF Info metadata. `geometry` supplies fixed `@page` paper size and margins for a one-page export; automatic page breaking still requires the separate paged path. `graphicx` trim/clip uses the same supported CSS `clip-path` shapes in SVG and PDF. These remain Lambda-script package policies under **D7.2.1–D7.2.4v3**; unsupported options and commands follow **S7.4.1–S7.4.4** diagnostics.

Phase III adds bounded theorem/proof, table, language, TikZ/PGFPlots, `natbib`, `caption`/`subcaption`, English `cleveref`, and unhighlighted `listings` profiles. Raw code and document-local `filecontents` bibliographies are preserved. `hyperref` headings and plain-text `\pdfbookmark` titles become hierarchical PDF outlines. `render_result.assets` records image/bibliography paths, origin, availability and source offsets; availability checks local existence, not successful decoding or remote fetching. See the [sample compatibility report](../test/latex/samples/COMPATIBILITY.md).

For the shared paged PDF path, pass `{target: "pdf", paged: true}` while generating the standalone HTML, then export it with `lambda render --paged`. The bounded `fancyhdr`/`lastpage` profile supplies text, running marks and page counters. Continuous output diagnoses unresolved page counters. Paged tables and columns, native Thai shaping/line breaking, exact font substitutions, and TeX typography remain explicit limits (**S7.4.1–S7.4.4**). Unicode PDF glyphs use font outlines and are not searchable text; full ToUnicode font embedding is a separate host capability. Lua, biblatex style files, and package files from a TeX installation are excluded (**S1.8, D7.2.7**).

```lambda
import latex: lambda.doc.latex.latex

let src = "\\documentclass{article}\n\\begin{document}\n\\section{Intro}\nHello \\textbf{world}.\n\\end{document}"
let ast = latex.parse_source(src, null)
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
| `parse_file(path)`, `parse_source(source, base)` | Parse LaTeX through the TeX engine with the package's adapter registry; `base` (a file path) resolves files beside the document. Return `null` when the source cannot be read or parsed |
| `render_document(ast, options)` | Like `render`, but defaults to a standalone page unless `options.standalone` is explicitly `false` |
| `render_document_file(path, options)` | The entry point the CLI and Radiant use: reads, parses and renders a file as `render_document` does, raising an error when the file cannot be read |
| `render_default(ast)` | `render(ast, null)` |
| `render_file(path)` | Parses a LaTeX file and renders it |
| `render_file_to_html(path)` | Parses a LaTeX file and returns an HTML string |
| `render_string(source)`, `render_string_to_html(source)` | Meant to render LaTeX source text; see the note below |

`options` may be `null`. `standalone` selects a complete page; `base_uri` resolves relative graphics and bibliography resources; `target: "pdf"` or `target: "svg"` enables output-specific diagnostics. The file entry points derive `base_uri` from the file path. `render_document_file` sets `source_path`, which the LaTeX package uses as the resource base when `base_uri` is absent (**D7.1.2v2**). The document class comes from `\documentclass` in the source. On the command line, `lambda convert paper.tex -t html -o paper.html` writes the rendered body without the LaTeX stylesheets, `--full-document` writes the standalone page instead, and the legacy `--font-option` switch does not change the math package's font selection; `lambda view`, `layout` and `render` always render the standalone page.

`render_string` and `render_string_to_html` parse source text directly. For resources referenced from a source string, parse it and call `render` or `render_result` with an explicit `base_uri`.

---

## 8. `pdf` — PDF Rendering

The pdf package renders the object tree that `input(path, 'pdf')` produces. Each page's content stream is interpreted into SVG: text (embedded fonts become `@font-face` rules), vector paths, images and shadings. A whole document becomes an HTML page with one SVG per page plus a selectable text layer. The example reads a sample file from the Lambda source tree:

```lambda
import pdf: lambda.doc.pdf.pdf

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
import pdf: lambda.doc.pdf.pdf

pn main() {
    let doc = input("test/input/test.pdf", 'pdf')^
    output(pdf.pdf_to_html(doc, {title: "Test PDF"}), "test_pdf.html")^
}
```

| Function | Description |
|----------|-------------|
| `pdf_to_svg(pdf, page_index, opts)` | Renders one page (0-based) as an `<svg>` element; a missing page renders a placeholder |
| `pdf_to_html(pdf, opts)` | Renders the document as an `<html>` element whose body holds one `<div class: "pdf-page">` per page |
| `pdf_to_document(pdf, opts)` | Configured native loader entry point: selects fixed pages when `opts.paged` is `true`, otherwise continuous HTML; returns an error on failed paged intake |
| `pdf_to_fixed_pages(pdf, opts)` | Returns native `r:fixed-pages` with HTML/SVG image content, source geometry and complete/ranged imports; returns an error for unsupported geometry/resources or exhausted page budgets |
| `pdf_page_count(pdf)` | The number of pages |
| `pdf_metadata(pdf)` | The document information dictionary (`Title`, `Author`, `Subject`, `Keywords`, `Creator`, `Producer`, `CreationDate`, `ModDate`) as a map, best effort |
| `render_source(source, options)` | Purely constructs a PDF `<file>` from an HTML element or source path using a private, script-free layout and Lambda callbacks over schema-backed views; `options.width`/`height` set the integer CSS viewport (defaults 800×600) |

Generation uses `fn` throughout (S2.6.6v3). Save the completed value with
`output(file, destination, {atomic: true})^` in a procedure. The output module
also exposes `lambda.doc.pdf.output.render(source, options)`; the procedural
`export` / `export_source` wrappers have been removed. `binary(file)` returns
owned PDF bytes, and completed PDF files can be composed inside another PDF
file without retaining their original child values.

The experimental output path is also available directly as
`lambda.doc.pdf.output.render`:

```lambda
import pdf: lambda.doc.pdf.output
pn main() {
    let file = pdf.render("page.html", {width: 800, height: 600})^;
    return output(file, "page.pdf", {atomic: true})^
}
```

This experimental output profile supports continuous and selected paged
output (`{paged: true, pages: "1,3"}`), solid rectangular
backgrounds and borders, positioned glyph outlines, image pixels with alpha
masks, translucent fills/text, transforms and shared native clip geometry.
The native walker owns traversal; the Lambda schema exposes properties and
typed nullable references, without sibling/parent links (**S11.5 / D3.5**).
Lambda writes drawing operators, resources, stream lengths and cross-reference
offsets. Text outlines preserve appearance but provide no searchable text.
Rounded or decorated boundaries, decorated text,
gradients, inline vector scenes, markers, effects, links and document metadata
are not yet supported by this route. Unsupported drawing callbacks return
errors before the destination is written. The production `lambda render`
PDF route remains the native writer while migration continues.

Fixed intake accepts `import_pages` (`"all"` or positive source ordinals/ranges such as `"2,49-53"`) and `max_pages` (a positive admitted-page budget, default 10,000). It validates original media/crop/bleed/trim/art boxes and signed quarter-turn rotation, retains the original one-based source ordinal, and embeds self-contained SVG vector resources. Native commands use `lambda view file.pdf --paged` and `lambda render file.pdf --paged -o result.pdf`; `--import-pages` and `--import-page-limit` select source pages and set the import budget. Preview `--pages` and output `--export-pages` address the resulting physical sequence independently. Failed imports preserve an existing output file. This adapter uses the configured package bridge under **D7.5.3**, dependency policy under **D7.1.2v2**, and the common selected-page lifetime under **D4.5.1v4 / D4.1.4v5**. Reversed/empty boxes, non-default `UserUnit` and unresolved image/form handles currently receive explicit profile errors. PDF page-label number trees retain decimal/Roman/repeated-letter styles, prefixes, starts, Unicode and empty labels through source selection and PDF export. Invalid label trees fail paged intake. Text selection and complete content-stream conformance remain outstanding.

`opts` may be `null`. Options: `title` (the HTML title, default `"PDF Document"`), `css` (replaces the default page stylesheet), `background` (the page colour, default `"white"`), `show_label` (draws a "Page n" label on each page), `max_pages` (how many pages `pdf_to_html` renders, default 48), and `id_prefix` (the generated SVG resource namespace, default `"pdf"`; use distinct prefixes when embedding multiple PDFs in one DOM tree). Each page extends this namespace with its zero-based page index, so its clipping paths, patterns and shading resources cannot collide with other pages' definitions.

> **Experimental.** Rendering fidelity varies from file to file. Continuous PDF presentation shows at most 48 pages by default; `view --paged` and `render --paged` use complete or explicitly ranged fixed-page intake.

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

## 10. Engine Packages: edit, editor, dom, doc

These packages implement parts of the engine in Lambda. They load automatically when a command needs them, and their internal APIs follow the engine. The `lambda.editor.draw` facade in §10.2.1 is available to applications; other internal interfaces remain unstable.

### 10.1 `edit` — the `lambda edit` application

`lambda.edit.edit` is the document-authoring application behind `lambda edit` (see [Lambda_CLI.md](Lambda_CLI.md)). The CLI passes it a local path, and `open_document(path, options)` does the rest: it picks a format by suffix (`format_for_path(path)`, over `formats`: Markdown `.md`/`.markdown`, HTML `.html`/`.htm`, SVG `.svg`), reads and imports the file, refuses a document it could not write back without loss, and builds the editing page with its toolbar and rich-text or drawing surface. The format adapters are `lambda.edit.markdown`, `lambda.edit.html` and `lambda.edit.svg`; saving and reloading live in `lambda.edit.session`.

### 10.2 `editor` — the editing model

`lambda.editor.*` is the rich-text editing model: documents as plain node trees (`mod_doc`), schemas for Markdown, strict CommonMark, an HTML subset and drawings (`mod_md_schema`, `mod_doc_schema`, `mod_drawing_schema`), invertible steps and transactions (`mod_step`, `mod_transaction`), undo history (`mod_history`), editing commands (`mod_commands`), paste handling, decorations and collaboration primitives (`mod_collab`). `lambda.editor.mod_editor` is the facade: `edit_open(doc, schema, selection)` creates an editor value, `edit_exec(editor, command)` applies a command built by an `edit_cmd_*` constructor such as `edit_cmd_insert_text(text)` or `edit_cmd_history_undo()`, and `edit_mount` binds the editor to a Radiant surface. Its modules are usually imported without an alias (`import lambda.editor.mod_editor`). The `edit` package uses it for every format it opens.

### 10.2.1 `editor.draw` — vertices and edges

`lambda.editor.draw` builds identified vertices and edges, queries connectivity,
validates connections, applies shared editor transactions, and exports SVG.
It supports twelve built-in shapes, declarative custom polygon descriptors,
transformed named ports, straight/orthogonal/curved routes, waypoints, custom
line/cubic paths, loops and parallel edges, plus version-4 archives with legacy
v1–v3 reading. `lambda.editor.draw.view.surface(document, {id, width, height})`
provides an independent retained canvas. The Draw demo reuses the package while
keeping its application shell and reading existing v1–v3 files. Package ownership follows
**D7.2.4v3**, with pure model edits and procedural presentation under
**S1.4, S12.1.3**. See the [API and support boundary](../lmd/package/editor/draw/README.md).

### 10.3 `dom` — browser behaviour for HTML

`lambda.dom.*` implements the user-agent behaviour of HTML documents in Radiant, written as `view` templates with `on` handlers (see [Reactive_UI.md](Reactive_UI.md)): form controls and their state, constraint validation, form submission and `application/x-www-form-urlencoded` encoding, link navigation, sequential focus and `autofocus`, `<details>` and `<summary>`, context menus, keyboard activation, caret movement, scrolling keys, IME composition, ARIA reflection, and the editing of `contenteditable` regions, including `designMode`, `execCommand` and the `queryCommand*` functions. Radiant loads `lambda.dom.dom` once per document, the first time an event reaches an element the package governs, and registers its templates as behaviour templates on the page's elements rather than as the page's own templates. Editing policy belongs to this package, never to native code (D7.2.5).

#### Native DOM API colours

`import dom` exposes the native DOM API used by those behaviour packages and
applications. Its published signatures distinguish reads (`fn`) from effects
(`pn`), as required by **D7.4.6**. Reads remain `fn` even when the node can be
mutated later. Property reads such as `node.id` likewise need no procedural
context. Both `fn` and `pn` callers can use `fn` APIs; only `pn` callers can
use `pn` APIs (**S12.1.1v3**).

| Published signature | Behavior |
|---|---|
| `fn get_attribute(n: any, name: string) -> string\|null` | Read an attribute. |
| `fn root_node(n: any) -> any` | Read the containing tree's root. |
| `fn get_element_by_id(root: any, id: string) -> any` | Find a node; absence is `null`. |
| `fn query_selector(root: any, selector: string) -> any` | Find the first matching node; absence is `null`. |
| `fn query_selector_all(root: any, selector: string) -> array` | Return a snapshot of the matching node sequence (**S9.2.2**). |
| `fn document_selection(doc: any) -> any` | Read the document's selection handle. |
| `fn selection_boundaries(s: any) -> map` | Read selection endpoints. |
| `pn set_attribute(n: any, name: string, value: string) -> null` | Write an attribute and run its reflection hooks. |
| `pn focus_set(n: any, from_keyboard: bool) -> bool` | Move focus. |
| `pn scroll_into_view(n: any) -> bool` | Change scroll position. |
| `pn check_validity(form: any) -> bool` | Run validation, which can dispatch `invalid` events. |

These signatures come from [the DOM catalogue](../lambda/dom/dom_api.def),
which also supplies the native module registration. Its compile-time checks
require `pn` exactly on `DOM_F_MUTATES` rows. The snapshot rule fixes collection
membership for iteration; it does not deep-copy each returned DOM node.

Native members preserve the same distinction: `node.get_attribute(name)` is
`fn`, while `node.set_attribute(name, value)`, `range.set_start(node, offset)`
and selection writes are `pn`. A member callee resolved at runtime still
reports E224 when an `fn` attempts to invoke a procedure (**S12.1.1v3**).

The engine-specific DOM helpers follow the same rule (**D7.4.6**):

| Radiant signature | Behavior |
|---|---|
| `fn local_point(node: any, x: number, y: number) -> array\|null` | Read the inverse viewport transform. |
| `fn geomap_snapshot(node: any) -> any` | Copy the last painted map frame. |
| `fn geomap_query_displayed(node: any, point: array, options: map) -> any` | Query the last painted map frame. |
| `pn layout(node: dom_node) -> bool` | Update the live document's layout. |
| `pn free(node: dom_node) -> null` | Release the live document and invalidate its node handles. |

See [DOM reads and procedural calls](Lambda_Func.md#dom-reads-and-procedural-calls)
for a query helper, a focus helper and an E224 example.

---

### 10.4 `doc` — the bundled document viewer

Bare `lambda view` loads `package/doc/doc_viewer.ls` from Lambda home;
`lambda demo` with no name opens the adjacent `doc_viewer.html` startup splash.
Named demos use `lambda demo <name>`; see the [demo catalog](../test/demo/README.md).
The viewer browses the current working directory and loads documents on selection. Its
Seti icon font and license ship in `package/doc/icons/`, and its bundled math fonts
come from `package/doc/math/fonts/`. These application resources belong to the shipped
`lambda.doc` package (D7.2.4v3); UI event fixtures remain under `test/ui/`.

---

## 11. `map` — Geographic Maps

`lambda.map` is an explicit public package entry under D7.2.4v3. It produces a
native Radiant `<geomap>` viewport. The initial implementation accepts inline,
typed GeoJSON and a bounded subset of MapLibre Style Specification version 8:
ordered `background`, `fill`, `line` and `circle` layers with hex/transparent
colors, opacity, line width/caps/joins, fill outlines, circle radius/strokes,
visibility and zoom limits.
Paint expressions and boolean feature filters use the shared native evaluator.

```lambda
import maps: lambda.map

let point = {type: "Feature", id: "origin", properties: {},
    geometry: {type: "Point", coordinates: [0, 0]}};
let spec = maps.geomap([
    maps.source("places", point),
    maps.layer("background", "background", null, {'background-color': "#eef3f6"}),
    maps.layer("places", "circle", "places", {'circle-color': "#e63946", 'circle-radius': 8})
], {width: 400, height: 240, center: [0, 0], zoom: 2});

maps.interactive(spec)
```

Use `spec` directly for a static viewport. `interactive(spec)` applies a reactive
view with independent camera/selection state, captured pointer dragging, wheel
and double-click zoom, and arrow/plus/minus/Home/Escape navigation. Retain
`model(spec, options)` and `apply` it to
preserve that view's state across parent rerenders (S12.1.1v2, S12.1.3).

| Function | Result |
|---|---|
| `geomap(children, options = {})`, `source(id, data)`, `layer(id, kind, source = null, paint = {}, layout = {})` | Construct typed map elements |
| `normalize(model)`, `validate(model)` | A normalized map / `true`, or an ordinary error value for invalid or unsupported input (S7.4.1) |
| `from_style(style, options = {})` | Convert the supported version 8 style subset into `<geomap>` |
| `project(camera, position)`, `unproject(camera, point)` | Convert longitude/latitude degrees and viewport CSS pixels |
| `fit_bounds(camera, bounds, padding = 0.0)`, `update(camera, event)` | Return a new camera; bounds may cross the dateline |
| `query_source(model, source_id)`, `query_rendered(model_or_frame, point_or_box, options = {})` | Source features / indexed painted hits, in reverse paint order |
| `plan(model, viewport = null)`, `render_frame(frame)` | Immutable camera/path/feature/index snapshot / its SVG element |
| `snapshot(node)`, `query_displayed(node, point_or_box, options = {})` | Functions reading the last painted native frame (D7.4.6) |
| `model(spec, options = {})`, `interactive(spec, options = {})` | A reactive view model / its applied native viewport |
| `to_svg(model, viewport = null)` | An SVG element from the same native paint compiler (D7.5.3) |

The camera uses Web Mercator, fractional zoom from −2 to 22, bearing, zero pitch
and one world copy. Geographic calculations use a 512 CSS pixel world at zoom
zero; device density changes raster resolution independently. Document PNG,
SVG and PDF export use the native renderer. `to_svg` can supply an explicit
`{width, height}` viewport.

Supported expressions include `literal`, `get`, `has`, `id`, `geometry-type`,
`zoom`, `coalesce`, comparisons, `all`, `any`, `!`, `case`, `match`, `step`,
numeric linear `interpolate`, and `number`/`string`/`boolean` assertions.
Paint zoom belongs at the input of an outer `step` or `interpolate`; filters
use integer zoom. Invalid runtime property values use property defaults and
produce diagnostics. Static type errors return ordinary errors (S7.4.1).
The [supported manifest](../test/map/README.md) records limits and reference tests.

Query coordinates are viewport CSS pixels: `[x,y]` or a rectangle
`[[left,top],[right,bottom]]`. Options select `layers` and a `radius` tolerance.
Historical frame values retain their geometry and feature metadata across
resize/removal (D4.5.2, D5.3.3). Input coordinates use shared inverse CSS-plane
mapping, including rotation/skew/perspective.

Optional interaction options include `controls:true`, `feature_list:true`,
`label`, fit `bounds:[west,south,east,north]` and `padding`. Procedural
`on_select`/`on_hover` callbacks receive `{node,features,point,lnglat}`;
`on_camera` receives `{node,camera}` after input changes the camera.
Controls and selected-feature text use ordinary HTML.

URL/tile sources, MVT, symbols, labels, autonomous resize notifications and
camera animation remain open. Compiled-style reuse across different cameras
also remains open. JavaScript support is Phase 2. See the
[implementation record](../vibe/impl/Lambda_Impl_Map.md) for coverage, limits,
engine prerequisites and validation evidence.

## 12. `grammar` — Language Grammars

Each module declares a `pub grammar` that `parse(text, g)` runs (see
`parse` in [Lambda_Sys_Func.md](Lambda_Sys_Func.md)). The packages parse
only: they turn source text into a Lambda element tree, and evaluating that
tree is up to the caller.

### 12.1 `lambda.grammar.xpath` — XPath 3.1

```lambda
import lambda.grammar.xpath

let tree = parse("//book[@year > 2000]/title", xpath)^
let step = xpath.AxisStep
"child::a" is step              // true: a rule is a type
parse("$x + 1", xpath, {spans: true})^   // start/end byte offsets on every element
```

The tree follows the W3C XPath 3.1 grammar with one node per construct, not
per precedence level:

| XPath | Tree |
|-------|------|
| `a + b` | `<Expr left: <Expr …>, op: '+', right: <Expr …>>`; every binary operator, including `/`, `//`, `!`, `||`, `to`, comparisons, `union`/`|`, `intersect`/`except` |
| `-a` | `<Expr op: '-', operand: <Expr …>>` |
| `/a`, `//a`, `/` | `<Expr op: '/', right: …>`, `<Expr op: '//', right: …>`, `<Expr root: '/'>` |
| `$f(1)`, `$x[1]`, `$m?k` | `<Expr base: …, args: <ArgumentList …>>`, `… predicate: <Predicate …>`, `… lookup: <Lookup key: "k">` |
| `x instance of T`, `x cast as T` | `<Expr left: …, op: 'instance', type: <SequenceType …>>`, `op: 'cast', type: <SingleType …>` |
| `$s => f()` | `<Expr left: …, op: '=>', function: "f", args: <ArgumentList>>` |
| `child::a[1]`, `@id`, `..`, `a` | `<AxisStep axis: 'child', test: <NameTest "a">, <Predicate …>>`, `abbrev: '@'`, `abbrev: '..'`, `test:` only |
| `text()`, `element(x)` | `test: <TextTest>`, `test: <ElementTest name: "x">` (and the other kind tests) |
| `f(1, ?)`, `f#2` | `<FunctionCall name: "f", args: <ArgumentList <Expr …> <ArgumentPlaceholder>>>`, `<NamedFunctionRef name: "f", arity: "2">` |
| literals, `$x`, `.` | `<IntegerLiteral "1">`, `<DecimalLiteral "1.5">`, `<DoubleLiteral "1e3">`, `<StringLiteral value: "a">`, `<VarRef name: "x">`, `<ContextItemExpr>` |
| `for`, `let`, `some`/`every`, `if` | `<ForExpr <Binding var: "i", in: …> … return: …>`, `<LetExpr <LetBinding var:, value:> … return:>`, `<QuantifiedExpr quantifier: 'some', <Binding …> satisfies: …>`, `<IfExpr condition:, then:, else:>` |
| `function($a) { … }`, `map { }`, `[ ]`, `array { }` | `<InlineFunctionExpr <Param name: "a"> … body: <EnclosedExpr …>>`, `<MapConstructor <MapConstructorEntry key:, value:>…>`, `<SquareArrayConstructor …>`, `<CurlyArrayConstructor <EnclosedExpr …>>` |

Every expression node is an `Expr` element; a leaf keeps one `<Expr …>`
around its own node. Operators and keywords are symbols; names and literal
text are strings; `StringLiteral`'s `value` is the text between the quotes,
with doubled quotes left as written. The whole expression is `<XPath …>`,
holding one `Expr` per comma-separated item.

Known differences from the spec: `(: … :)` comments do not nest, and the
parser accepts a few forms the spec restricts (an ExprSingle such as `for`
as an operand of a binary operator; chained `instance of`).

### 12.2 `lambda.grammar.xquery` — XQuery 3.1

```lambda
import lambda.grammar.xquery

parse("for $b in //book order by $b/title return <t>{ $b/title }</t>", xquery)^
```

The grammar imports `lambda.grammar.xpath`, so every XPath shape in §12.1
appears unchanged inside an XQuery tree. The whole query is
`<Module <VersionDecl …>? <MainModule …>>` or `<LibraryModule <ModuleDecl …> …>`;
a main module holds its prolog declarations, then `<QueryBody <Expr …>…>`.

| XQuery | Tree |
|--------|------|
| `for … let … where … group by … order by … count … return` | `<FLWORExpr <ForClause <ForBinding var: "x", in: …>> <LetClause <LetBinding var:, value:>> <WhereClause …> <GroupByClause <GroupingSpec …>> <OrderByClause <OrderSpec direction: 'descending', …>> <CountClause var:> return: …>`; window clauses are `<TumblingWindowClause …>` / `<SlidingWindowClause …>` with `start:`/`end:` conditions |
| `switch`, `typeswitch`, `try` | `<SwitchExpr operand: …, <SwitchCaseClause case: …, return: …> default: …>`, `<TypeswitchExpr operand: …, <CaseClause var:?, <SequenceType …>, return:> default:>`, `<TryCatchExpr body: …, <CatchClause <NameTest …> body: …>>` |
| `<a href="x{1}">t {e} u</a>` | `<DirElemConstructor name: "a", end: "a", <DirAttribute name: "href", value: <DirAttributeValue "x" <EnclosedExpr …>>> "t " <EnclosedExpr …> " u">`; content text keeps its whitespace and stays as written (`&amp;`, `{{`); comments, PIs and CDATA are `<DirCommentConstructor "<!--…-->">`, `<DirPIConstructor …>`, `<CDataSection …>` |
| `element n { }`, `attribute`, `text`, `document`, … | `<CompElemConstructor name: "n", body: <EnclosedExpr …>>` (a computed name is `name: <EnclosedExpr …>`), and likewise for the other computed constructors |
| ``` ``[a `{ e }` b]`` ``` | `<StringConstructor "``[a `{" <StringInterpolation <Expr …>> "}` b]``">`: the text pieces keep their delimiters |
| prolog | `<NamespaceDecl prefix:, uri:>`, `<VarDecl name:, type:?, value:>`, `<FunctionDecl name:, <Annotation …> <Param …> return:?, body:>`, `<ModuleImport prefix:?, uri:, location:?>`, and one element per setter |

Known differences from the spec: comments do not nest; an end tag is not
checked against its start tag; whitespace between attributes is optional.

### 12.3 `lambda.grammar.jq` — jq 1.7.1

```lambda
import lambda.grammar.jq

parse(".[] | select(.age > 30) | {name, age}", jq)^
```

Ported from jq's own bison grammar: one `Exp` node per operator, in jq's
precedence order (`|` loosest, then `,`, `//`, assignments, `or`, `and`,
comparisons, `+ -`, `* / %`, postfix `?`, `try`).

| jq | Tree |
|----|------|
| `a \| b`, `a + b`, `a // b`, `.x \|= f` | `<Exp left: <Exp …>, op: '\|', right: <Exp …>>` (every binary operator) |
| `-a`, `a?` | `<Exp op: '-', operand: …>`, `<Exp operand: …, op: '?'>` |
| `.`, `..`, `.a`, `."k"`, `.[0]`, `.[1:2]`, `.[]`, `.a?` | `<Term <Identity>>`, `<Term <Recurse>>`, `<Term field: "a">`, `<Term field: <String "k">>`, `<Term base: <Term …>, index: <Exp …>>`, `slice: <Slice from:, to:>`, `iterate: <Iterate>`, `optional: '?'` |
| literals, `$x`, `f(a; b)`, `@csv` | `<Number "1">`, `<String "a" <Interpolation <Exp …>> "b">`, `<Variable name: "$x">`, `<FunctionCall name: "f", <Exp …> <Exp …>>`, `<Format name: "@csv">` |
| `[e]`, `{a, "b": 1, (.k): v}` | `<Array <Exp …>>`, `<Object <Pair key: "a"> <Pair key: <String "b">, value: <ObjVal …>> …>` |
| `def f(g; $x): e; body` | `<Exp def: <FuncDef name: "f", <Param name: "g"> <Param name: "$x">, body: …>, body: …>` |
| `e as $x \| body`, `reduce`, `foreach`, `if`, `try`, `label` | `<Exp source: <Term …>, <Variable …>, body: …>`, `<Reduce source:, init:, update:>`, `<Foreach … extract:?>`, `<If condition:, then:, <Elif …>, else:?>`, `<Exp body:, catch:?>`, `<Exp label: "$out", body:>` |

The program is `<Program <Module …>? <Import …>* body: <Exp …>>` (a library
holds `<FuncDef …>` children instead). String text is raw: escapes are not
decoded.

### 12.4 `lambda.grammar.rnc` — RELAX NG compact syntax

```lambda
import lambda.grammar.rnc

parse("start = element doc { attribute id { xsd:ID }, text* }", rnc)^
```

| RNC | Tree |
|-----|------|
| `p, q`, `p & q`, `p \| q` | `<Group …>`, `<Interleave …>`, `<Choice …>`; the three do not mix without parentheses, and parentheses leave no node |
| `p?`, `p*`, `p+` | `<Repeat pattern: …, op: '?'>` |
| `element n { p }`, `attribute n { p }` | `<Element name: <Name "n">, …>`; name classes are `<Name …>`, `<NsName prefix: "ns">`, `<AnyName except: …>`, `<NameChoice …>` |
| `xsd:int { minInclusive = "1" } - "5"`, `"v"`, `string "v"` | `<Data type: "xsd:int", <Param name:, value:> except: …>`, `<Value value: <Literal "v">>`, `<Value type: "string", value: …>` |
| `start = p`, `name \|= p`, `div`, `include` | `<Start assign: '=', …>`, `<Define name: "name", assign: '\|=', …>`, `<Div …>`, `<Include uri:, inherit:?, …>` |
| declarations | `<NamespaceDecl prefix:, uri:>`, `<DefaultNamespaceDecl …>`, `<DatatypesDecl …>` |
| annotations, `##` | `<Annotations <AnnotationAttribute …> <AnnotationElement …>>`, `<FollowAnnotation …>`, `<Documentation "## …">`, wrapped with the pattern in `<Annotated pattern: …>` |

A `<Literal>` holds the joined text of its `~` segments without quotes. Not
supported: `\x{…}` escapes.

## 13. Tests

A `.ls` script in these directories runs in the Lambda runtime test suite (`test/test_lambda_gtest.exe`) when it has an expected-output `.txt` file beside it; the `test/ui/` event scripts run in the UI automation suite.

| Package | Tests |
|---------|-------|
| `chart` | `test/lambda/chart/` |
| `ui.dtna` | `test/lambda/ui_dtna/`; native UI fixtures in `test/ui/dtna/` (`make test-ui-dtna`) |
| `ui.bold` | `test/lambda/ui_bold/`; native controls in `test/ui/bold/` and `test/ui/bold_gallery.json` (`make test-ui-bold`) |
| `ui.wired` | `test/lambda/ui_wired/`; native controls/geometry in `test/ui/wired/` and `test/ui/wired_gallery.json` (`make test-ui-wired`) |
| `ui.clay` | `test/lambda/ui_clay/`; native controls/collections/scopes in `test/ui/clay/` and the two galleries (`make test-ui-clay`) |
| `ui.blade` | `test/lambda/ui_blade/`; native RPG/collection/scope/responsive fixtures in `test/ui/blade/` and two galleries (`make test-ui-blade`) |
| `map` | `test/lambda/map/`; native rendering and input in `test/test_map_gtest.cpp`; PNG/SVG/PDF fixtures in `test/map/` (`make test-map`, `make test-map-export`) |
| `graph` | `test/lambda/graph/mermaid/`, `test/lambda/graph/graphviz/`, `test/lambda/graph/structurizr/`; `test/lambda/graph_layout*.ls` and `test/lambda/graph_transform_*.ls` |
| `math` | `test/lambda/math/` |
| `music` | `test/lambda/music/`; score fixtures and viewer example in `test/music/` |
| `latex` | `test/lambda/latex/` |
| `docx` | `test/lambda/docx/`; synthetic and pinned reference documents in `test/input/docx/` |
| `docbook` | `test/lambda/docbook/`; original XML and pinned xslTNG bodies in `test/input/docbook/`; direct/source-tab viewing in `test/ui/docbook_source.json` and `test/ui/doc_viewer_docbook_preview.json` |
| `grammar` | `test/lambda/grammar/xpath.ls` (the grammar module itself: `test/lambda/grammar/`) |
| `pdf` | `test/lambda/pdf/` |
| `eps` | `test/lambda/eps/`; native parsing/scanning in `test/test_input_eps_gtest.cpp` |
| `openapi` | None yet |
| `edit` | `test/lambda/edit/`, `test/lambda/proc/edit_session_save.ls`; UI scripts `test/ui/edit_*.json` |
| `editor` | `test/lambda/editor/`, `test/lambda/editing/` |
| `dom` | `test/lambda/dom_*.ls`; UI scripts `test/ui/dom_pkg_*.json` |
| The `lambda.*` namespace | `test/lambda/lambda_namespace.ls` |

---

## 14. `eps` — EPS Rendering

The EPS package lives under the document namespace (**D7.2.4v3**). A C+ parser
reads the EPS envelope and scans source objects; Lambda Script executes the
PostScript machine and emits portable SVG. The initial profile supports vector
paths, colors, strokes, transforms, clipping, procedures, common stack/control
operators, local VM saves, base-font text and raster images. External resources and full
PostScript LanguageLevel conformance remain outside this profile; an executed
unsupported operator raises an error.

```lambda no-run
import eps: lambda.doc.eps.eps

pn main() {
    let doc = input("artwork.eps", 'eps')^
    let picture = eps.eps_to_svg(doc, {id_prefix: "artwork"})^
    output(format(picture, 'xml')^, "artwork.svg")^
}
```

`eps_metadata(doc)` reads declared metadata without executing the artwork.
`interpret(doc, options)` returns drawing records, definitions, geometry,
diagnostics and statistics; `display_to_svg(display, options)` converts that
result to an element. `eps_to_svg` composes the two. Execution is procedural
(**S12.1.2**); handle failures with a procedure statement handler
(**S7.6.7v4**) or propagate with `^`.

Options include `id_prefix`, `scale`, `background` and a `limits` map. Use
separate ID prefixes when combining SVG fragments. Output is transparent by
default, with dimensions in points. The exact supported operator names are
available as `eps.supported_operators`; the initial dictionary-key profile
accepts names and its logical operators accept booleans. Auto/JIT are the
supported package execution tiers.

Font setup supports `findfont`, `scalefont`, `makefont`, `setfont`, `currentfont`
and `selectfont` for the Standard 14 names and four Palatino variants, sharing
the PDF font mappings. `show`, `stringwidth`, `ashow`, `widthshow` and
`awidthshow` support printable ASCII StandardEncoding text. Glyph advances
come from Radiant's installed font metrics; explicit SVG glyph origins preserve
those advances. Font selection follows graphics and VM saves. Unknown names
raise `invalidfont`; reencoding and embedded/custom font programs remain open.

Legacy `image`/`colorimage` support 1/2/4/8-bit gray, RGB and CMYK samples,
interleaved or planar, with strings, in-memory files and procedure callbacks.
`currentfile`, byte/hex/line reads, `token` and ASCIIHex/ASCII85/RunLength filters
share one source cursor, so inline pixels are consumed before lexical scanning
resumes. PNG encoding and filter codecs reuse native PDF helpers. The default
ceilings include 10 million VM steps, 16 million pixels and 64 MiB of cumulative
decoded bytes; lower caller limits remain enforced.

```bash
./lambda.exe convert artwork.eps -t svg -o artwork.svg
./lambda.exe render artwork.eps -o artwork.png
./lambda.exe layout artwork.eps
./lambda.exe view artwork.eps
```

See the [EPS design](../vibe/Lambda_Pkg_EPS.md) and
[implementation record](../vibe/impl/Lambda_Impl_EPS.md) for source contracts,
budgets, tests and remaining resource/font/image work.

---

## 15. `docx` — Word DOCX Import

`lambda.doc.docx` is a shipped script module under D7.2.1/D7.2.4v3. It reads a
local `.docx` path or a captured ZIP file tree and converts the document's main
story to semantic HTML elements. The source package is read through Lambda's
bounded ZIP input (S12.4.1v2/S14.3.1v2); conversion follows relationships and
namespace URIs rather than fixed member paths or namespace prefixes.

```lambda
import docx: lambda.doc.docx
let result = docx.convert("test/input/docx/minimal.docx")^;
result.document
```

`convert(source, options = null)` returns
`{document, html, text, metadata, diagnostics}`. `document` is an `<article
class="docx-document">` element, `html` its escaped HTML fragment, and `text`
the reading-order text. Paragraphs end with two newlines; table rows use tabs
between cells and newlines between rows. Images do not contribute prose text.
Available core properties such as title and creator appear in `metadata`.

`to_elements`, `to_html`, and `extract_text` take the same arguments and return
the corresponding field. Use `convert` when the caller needs diagnostics.

Supported content includes paragraphs; headings derived from styles/outline
levels; inherited and direct bold, italic, underline, strike, superscript and
subscript; tabs and breaks; simple lists with numbering starts, continuation
and nesting, including lists with hidden markers; tables with grid spans and
vertical merges; hyperlinks and simple bookmarks; embedded raster images;
and clustered column charts rendered from their cached category/series values.
Charts reuse `lambda.chart.chart` for SVG layout (D7.2.4v3), retain explicit RGB
series colors and titles, and contribute a data table to extracted text.
Paragraph-relative column anchors support left/right square wrapping,
centered largest-side wrapping, and top/bottom wrapping at paragraph top.
Signed column-start and paragraph offsets with square wrapping on both sides
use CSS float margins (D7.2.4v3); this remains a flowing approximation of Word.
No-wrap column/paragraph anchors with explicit offsets retain their extents
and front/behind-text layer order. Inset raster crops keep their saved frames.
Flat DrawingML rectangles support validated solid RGB/theme fills with no
outline; a supported modern compatibility branch takes precedence over its
fallback. Decorative rectangles are hidden from accessibility text (S1.8,
D7.2.4v3).
The viewer also reads the document theme, inherited and direct run properties
(font family, size, color, tracking and capitalization), paragraph spacing,
table grid widths, cell shading and borders, row heights, and page size/margins.
Values are converted from OOXML units into CSS; the named font must be
available to the host font resolver for a matching face. CSS remains a flow
preview and does not reproduce Word's full layout engine.
Footnotes, endnotes and comments become numbered links and appended ordered
sections, with each kind numbered in first-reference order and repeated
references sharing one entry (D7.2.4v3). Links in annotations resolve against
that annotation part's relationships. Extracted text includes reference marks
and labelled annotation text after the main body. Text-box paragraphs/tables
are retained after their containing paragraph, with a placement diagnostic.
Both Transitional and Strict Word
namespace families are recognized. HTML is flow content, with semantic break
markers and no Word pagination guarantee.

Content controls display their saved `sdtContent` at inline, block, row and
cell boundaries, preserving surrounding formatting and reading order. Control
properties are editing metadata; bindings and placeholder definitions are not
evaluated. Footnote/endnote parts containing only separator definitions do not
represent document notes and are excluded from annotation content.
Simple and complex fields display their saved text and links, including cached
TOC/page-reference results. Instructions are never executed or refreshed
(S1.8). Table rows retain omitted leading/trailing grid columns as empty HTML
slots, preserving the alignment of financial tables (D7.2.4v3).

| Option | Default | Meaning |
|--------|---------|---------|
| `style_map` | `{}` | Style ID/name to `p`, `h1`–`h6`, `blockquote`, `pre`, or `{tag, class}`. |
| `presentation` | `"semantic"` | `"source"` adds validated Word theme, run, table, and page CSS; the viewer selects it. |
| `include_empty_paragraphs` | `false` | Preserve empty paragraph elements and text boundaries. |
| `images` | `"data-uri"` | Embed supported internal images; `"omit"` reports their omission. |
| `id_prefix` | `""` | Prefix bookmark/annotation IDs and internal anchor links. |
| `unsupported` | `"warn"` | Return diagnostics; `"error"` rejects a conversion with diagnostics. |

Diagnostics have `{severity, code, part, message}`. Unsupported visible
constructs are reported. Headers/footers, comment ranges/author metadata, custom
note numbering, revision semantics,
field execution, other chart types, shapes beyond flat rectangles, equations, page-relative floating
placement, DOCX writing,
encrypted documents, and `.docm` are outside this profile. External resources
are never fetched; emitted hyperlink schemes are validated. XML parts use
strict UTF-8 XML with significant whitespace and reject DTDs and embedded NULs.
Unknown options, unsafe mappings, invalid package targets, required malformed
XML, and style cycles raise ordinary Lambda errors.

Package errors use codes 7600–7607: invalid source, options, package, missing
part, XML, target, unsupported content in strict mode, and style cycle.
Underlying source/ZIP errors keep their existing codes. All 17 pinned Mammoth
DOCX reference documents import; the external-image and floating-text-box
fixtures report their unsupported resources/geometry. Mammoth's embedded
style-map extension does not override Lambda's explicit `style_map` option
(S1.8, D7.2.4v3). See the [fixture inventory](../test/input/docx/README.md).

Design and phased scope: [DOCX package](../vibe/Lambda_Pkg_Docx.md).

`lambda view report.docx` presents the imported Word document with the shared
`lambda.doc.docx.view` presentation module (D7.2.4v3). The bundled file browser
also previews DOCX and exposes its converted HTML on the Source tab. Use
`lambda view report.docx --archive` for the underlying ZIP tree. Unsupported
content is listed above the preview; Word pagination is outside this profile.

## 16. docbook — DocBook to HTML/CSS

Open the original sample directly with
`./lambda.exe view test/input/docbook/showcase.xml`. The XML viewer recognizes
DocBook namespaces and uses the package's HTML/CSS presentation (D7.2.4v3);
the document browser keeps the original XML in its Source tab.
The usual extension is `.xml`; the namespace
`http://docbook.org/ns/docbook` identifies DocBook. The current direct-view
roots are `article`, `book`, `set`, `part`, and `reference`.

`lambda.doc.docbook` is an initial native Lambda port of the official DocBook
xslTNG 2.8.5 HTML/CSS transformation (D7.2.4v3). Java and XSLT are used only to
regenerate reference fixtures; package execution has no Java dependency.

```lambda
import docbook: lambda.doc.docbook
let xml = "<article xmlns='http://docbook.org/ns/docbook' version='5.2'><info><title>Hello</title></info><para>DocBook in Lambda.</para></article>"
let source = parse(xml, {type:"xml", flavor:"document"})^
let publication = docbook.convert(source)^
publication.document
```

| API | Contract |
|-----|----------|
| `convert(source, options = null)` | Pure conversion of a parsed XML tree or source snapshot into a publication. |
| `to_html(source, options = null)` | Pure conversion/serialization to one complete HTML string; rejects chunked mode. |
| `read(path, options = null)` | Procedure acquiring strict XML and referenced local media into an immutable snapshot. |
| `write(publication, destination)` | Procedure writing all HTML pages and captured assets into a local directory. |

Pure conversion receives all resources as data; it never fetches missing input
(S12.1.1v3). Parse strings explicitly, preserving significant XML whitespace;
`flavor:"document"` retains it and rejects DTDs. A snapshot has `document`,
`source_uri`, `base_uri`, and a `resources` map keyed by resolved URI with
`{bytes, media_type}` entries. Unavailable media produces a diagnostic.

```lambda
import docbook: lambda.doc.docbook
pn main() {
    let source = docbook.read("test/input/docbook/book.xml")^
    let publication = docbook.convert(source, {output:"chunked"})^
    docbook.write(publication, "temp/docbook-example")^
}
```

A publication contains `document` (the entry page), `entry`, ordered `pages`
(`{path, document}`), `assets`, `toc`, `metadata`, `diagnostics`, and
`compatibility`. The latter identifies the upstream commit, support profile
and intentional differences. `external_stylesheets` lists the original CSS's
external Google Fonts imports; those font bytes are not bundled or fetched
by conversion.

| Option | Default | Meaning |
|--------|---------|---------|
| `output` | `"single"` | `"chunked"` computes page ownership, navigation, and cross-file links. |
| `target` | `"browser"` | `"radiant"` currently returns the same HTML/CSS; renderer support is separate. |
| `unsupported` | `"warn"` | `"error"` rejects diagnosed unsupported content. |
| `parameters` | `{}` | Native values keyed by supported original xslTNG parameter names. |
| `customization` | `{}` | Pure handlers keyed by DocBook local name; each receives `(node, context, next)`. |

Supported parameter groups include TOC controls, component/section numbering,
generated-ID root/separator, CSS use/resource prefix, verbatim presentation and
trimming/line numbering, chunk section depth/navigation/note renumbering, and ID-based chunk
filenames. Boolean parameters take `bool` values. `resource-base-uri` is a
relative publication directory ending in `/` (or empty). Unknown parameters
and invalid types fail validation. Syntax highlighting currently accepts only
`"none"`; this is an explicit difference from upstream's default.
`verbatim-number-every-nth` and `verbatim-number-minlines` take positive
integers; `verbatim-number-first-line` is boolean, and
`verbatim-number-separator` is a string. Upstream's explicit element styles
take precedence over `verbatim-style-default` for the covered vocabulary.

The current profile covers core article/book structures, common prose and
inlines, basic/compact/nested/variable lists, ordinary footnotes, admonitions,
formal objects, HTML tables and CALS row/column spans, generated English
headings/TOCs/references, text verbatim blocks, captured images and multi-page
export. Extended vocabulary, full localization, bibliography/glossary/index
generation, inclusion/transclusion/profiling, advanced CALS/media layout,
table-specific footnotes, foreign inline SVG/MathML and optional browser scripts
remain incomplete. Unsupported content and selected unsupported attributes
are diagnosed. Full xslTNG or DocBook schema coverage is not claimed.

Errors use package codes 7700–7707 for invalid source, options, namespace,
duplicate ID, strict unsupported content, invalid table, missing resource, and
invalid path. Underlying input/parse errors retain their original codes.
Recoverable diagnostics include severity, code, message, source URI and
occurrence path; line/column are nullable (S7.4.2, S7.4.4).

See the [design](../vibe/Lambda_Pkg_Docbook.md),
[implementation record](../vibe/impl/Lambda_Impl_Docbook.md), and
[upstream/support record](../lmd/package/doc/docbook/UPSTREAM.md). The registered
Lambda tests include the offline single-page and chunked official-body parity
gate; expected bodies are refreshed separately using the pinned processor.

## 17. doc.wiki — Wiki writers

`lambda.doc.wiki` serializes Mark Doc to MediaWiki, DokuWiki, TikiWiki, TWiki,
Vimwiki, XWiki 2.1, ZimWiki, Creole 1.0, Jira, and Confluence wiki markup.
A shared Lambda Script traversal uses declarative syntax profiles; native code
routes calls and owns package invocation (D7.1.1, D7.2.4v3).

```lambda
import wiki: lambda.doc.wiki
let document = parse("= Title =\n\n**Text**", "wiki:creole")^
wiki.format_report(document, {flavor:"jira"})^
```

| Export | Result |
|---|---|
| `format_report(document, options = null)` | `{text, diagnostics}`; returns a report even when there are losses |
| `format(document, options = null)` | Serialized string; errors on losses by default |
| `format_file(path, options = null)` | Reads with `input_type` / `input_flavor`, then formats |

Options are `flavor` (default `mediawiki`) and `loss` (`error` or `allow`).
`format_file` additionally accepts `input_type` (default `auto`) and
`input_flavor` (default empty). Unknown options and flavors are errors.
`format_report` reports unsupported nodes, attributes, task states/progress,
source dialect mismatches, and delimiter collisions. Saved TODO markers are
reused only if they still match the current task. It preserves native coarse
progress ranges rather than inventing exact percentages.

Diagnostics have `code`, `severity`, `element`, and `message` fields.
`format` defaults to `loss:"error"` and raises on the first diagnostic;
`loss:"allow"` returns the text. `format_report` returns diagnostics in
either mode. Malformed task attributes produce `invalid-task` diagnostics.

```lambda
import wiki: lambda.doc.wiki
wiki.format_file("notes.txt", {
    input_type:"wiki", input_flavor:"zimwiki", flavor:"creole", loss:"allow"
})^
```

The same writer is selected by `format(document,"wiki:creole")`, formatted
`output`, and `lambda convert -t wiki:creole`. The native MediaWiki formatter
has been retired. These are initial static profiles; builds and package/schema
compilation passed, while conformance and regression suites remain pending.
See [dialect coverage and limits](Markup_Formats_Support.md#15-wiki-dialect-coverage)
and the [working design](../vibe/input/Input_Wiki.md).

## 18. doc.misc — Source format writers

`lambda.doc.misc` writes Mark Doc as **AsciiDoc**, **troff/man**, or **Typst**,
and serializes email/contact/calendar maps as **EML**, **vCard**, or
**iCalendar**. All six writers are Lambda Script source modules (D7.2.4v3).

```lambda
import writers: lambda.doc.misc
let document = parse("# Title\n\nSome **text**.", "markdown")^
writers.format_report(document, {type:"asciidoc"})^
writers.man(document, {title:"LAMBDA", section:"1"})^
writers.typst(document)^
```

| Export | Result |
|---|---|
| `format_report(value, options = null)` | `{text, diagnostics}`, including reported losses |
| `format(value, options = null)` | Source string; rejects losses by default |
| `format_file(path, options = null)` | Reads with `input_type` / `input_flavor`, then formats |
| `asciidoc`, `man`, `typst`, `eml`, `vcf`, `ics` | Convenience functions accepting `(value, options = null)` |

Options are `type` (default `asciidoc`) and `loss` (`error`, default, or
`allow`). Man additionally accepts `title`, `section`, `date`, `source`, and
`manual`. File conversion adds `input_type` and `input_flavor`. Diagnostics
have `code`, `severity`, `element`, and `message`. Invalid data and unsupported
encodings remain errors even with `loss:"allow"`.

The same writers handle built-in `format(value, "asciidoc")`, formatted file
`output`, and CLI conversion. `--loss error|allow` applies to these six targets
as well as wiki output. Default file output is strict; to permit losses,
format with package options first, then write the resulting text.

For VCF/ICS, retained `entries` take precedence over `properties`, which take
precedence over convenience fields. Edit entries to retain parameters and
repeated values. EML retains ordered `header_entries` and authoritative
`raw_body`; remove `raw_body` to rebuild MIME content. See
[source format support and examples](Script_Formats_Support.md) for record
shapes, MIME authoring, limits, and verification results. EML/VCF/ICS round
trips pass; structural readback tests expose gaps in the three prose readers.
Native tool conformance remains unverified.

## 19. doc.ebook — EPUB reader

`lambda.doc.ebook` loads EPUB 2/3 publications and displays their XHTML/SVG
chapters in Radiant. Publication parsing and chapter projection are pure;
`read` captures a local file procedurally (S12.1.1v3, S12.1.3, D7.1.2v2).
ZIP members retain their captured source and decode lazily
(S12.4.1v2, S14.3.1v2). No ebook decoder dependency is required.

```lambda
import ebook: lambda.doc.ebook
let book = ebook.parse(input("book.epub", "zip")^)^
let first = ebook.chapter(book, 0)^;
{title:book.metadata.titles, chapters:len(book.reading_order), text:first.text}
```

| Export | Result |
|---|---|
| `parse(snapshot, options = null)` | Publication map from immutable ZIP bytes or a captured ZIP root |
| `read(path, options = null)` | Procedural local-file acquisition and publication parsing |
| `chapter(book, target, options = null)` | Sanitized HTML/document, source text/map, diagnostics and fixed-layout viewport; target is a spine index or canonical href |
| `locator(book, chapter, offset)` | Versioned source locator; offsets count Unicode scalars |
| `locate(book, locator)` | Validated locator for the same captured publication and source passage |
| `search(book, query, options = null)` | Exact, case-sensitive whole-book matches with snippets and locators; `limit` defaults to 100, maximum 1,000 |
| `reader(book, options = null)` | Interactive reader element for embedding; persistence is off by default |

When embedding `reader`, include `lambda.doc.ebook.reader.stylesheet` in the
host document's style element. Native viewing and browser preview include it
automatically.

Load options are `rendition` (zero-based OCF package index) and
`unsupported:"error"`. Unknown options are rejected; chapter options are currently reserved. The publication
includes metadata/cover, manifest, occurrence-preserving reading order,
contents/landmarks/publisher page links, resources, protection diagnostics,
and a SHA-256 snapshot identity. Chapters resolve packaged CSS imports,
images, SVG and unprotected fonts without extraction. Active content and
external resources are excluded.

Open a book with `lambda view book.epub`. The reader provides chapter/page
controls, internal links, history, contents, bookmarks, search, font size and
family, line spacing, margins, publisher-style selection and three themes.
A compact toolbar opens search and the **Aa** reading-settings panel; chapter
and page controls stay in the footer. Contents highlight the current chapter,
and the sidebar can be toggled on narrow windows. Enter submits a book search.
Reflowable chapters support scrolling and generated column pages. Fixed-layout
XHTML/SVG pages retain a required source viewport and support fit/zoom.
Selecting EPUB in the file browser opens the same reader; its Source tab shows
the OPF package. `layout` and `render` project the initial reader viewport,
rather than exporting an entire book.

Standalone viewing saves preferences, bookmarks and the last source location
atomically under `LAMBDA_EBOOK_STATE_DIR`, or the host's XDG/LOCALAPPDATA state
directory (HOME fallback). State is keyed by snapshot fingerprint, so moving
an unchanged book preserves its position. Saved passages are validated before
resume. The embedded browser preview does not save state.

This is an initial horizontal LTR profile. Continuous scrolling between
chapters, two-page spreads, RTL/vertical pagination, CFI interchange, obfuscated
fonts, scripted content, media overlays and DRM decoding remain unsupported.
Protected required spine content raises a package error; protected optional
resources use a diagnostic and fallback. EPUB package errors occupy
7800–7809; underlying ZIP/XML errors retain their codes (S7.4.2, S7.4.4).
MOBI and Kindle AZW/AZW3 are outside this package's scope.

See the [proposal](../vibe/Lambda_Pkg_EPUB.md),
[implementation and verification record](../vibe/impl/Lambda_Impl_EPUB.md), and
[authored fixture inventory](../test/input/ebook/README.md).

## 20. doc.diff — Git and patch review

`lambda.doc.diff` parses Git two-way and conventional unified patches and
reviews staged, unstaged, HEAD-to-worktree and two-revision comparisons. It
offers a virtual changed-file list and unified surface, hunk navigation, search,
selection/copy, lazy file loading and refresh. `to_html` exports every loaded
file and row. Binary, mode, rename, symlink, submodule and conflict changes keep
their own metadata and summaries. Repository access is procedural; parsing and
presentation are pure (S12.1.1v3/S12.1.3, D7.2.4v3/D7.2.6).

```lambda no-run
import diff: lambda.doc.diff
diff.repository(".", {mode: "unstaged"})^
```

Open that script with `lambda view review.ls`, or open `.patch`/`.diff` directly.
See [Git and patch review](Lambda_Diff.md) for the API, options, model and limits.
