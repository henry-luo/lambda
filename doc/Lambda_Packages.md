# Lambda Packages

Lambda ships a set of **packages**: libraries written in Lambda Script itself, distributed as source with the runtime and imported under the reserved `lambda.*` namespace (D9.3.4). Some are libraries you call from your own scripts: charts, graph layout and diagram rendering, math and LaTeX typesetting, PDF rendering and OpenAPI tools. Others are parts of the engine that happen to be written in Lambda: the `lambda edit` application, its editor model, and the browser behaviour behind HTML forms, links and editing in Radiant. This page lists them all, explains how their import paths resolve, and documents the public API of each library.

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
15. [docx — Word DOCX Import and Export](#15-docx--word-docx-import-and-export)
16. [docbook — DocBook to HTML/CSS](#16-docbook--docbook-to-htmlcss)
17. [doc.wiki — Wiki writers](#17-docwiki--wiki-writers)
18. [doc.misc — Source format writers](#18-docmisc--source-format-writers)
19. [doc.ebook — EPUB reader](#19-docebook--epub-reader)
20. [doc.diff — Git and patch review](#20-docdiff--git-and-patch-review)
21. [doc.readability — Article extraction](#21-docreadability--article-extraction)
22. [scene3d.motion — Character animation](#22-scene3dmotion--character-animation)
23. [archive — Web archiving](#23-archive--web-archiving)
24. [mail — Mail sync and store](#24-mail--mail-sync-and-store)
25. [oauth — Sign-in for native apps](#25-oauth--sign-in-for-native-apps)
26. [procart — Procedural art](#26-procart--procedural-art)
27. [scene3d — Native 3D scenes](#27-scene3d--native-3d-scenes)
28. [color — Pure color utilities](#28-color--pure-color-utilities)
29. [pptx — PowerPoint import](#29-pptx--powerpoint-import)
30. [paint — Painting with brushes](#30-paint--painting-with-brushes)

---

## 1. Package Overview

| Package | Import path | Status | What it does | Used by the CLI |
|---------|-------------|--------|--------------|-----------------|
| `sync` | `lambda.sync.core`, `lambda.sync.text`, `lambda.sync.mark`, `lambda.sync.session` | Experimental replication prototype | Pure Text/Mark replication, compact checkpoints, external-import branches and saved-version model; [scope and API](../lmd/package/sync/README.md) | `./lambda.exe test/lambda/sync_example.ls`; no production sync command |
| `image` | `lambda.image`, `lambda.image.edit` | Initial native `u8` profile | Raster Velmts, layered composition, animation, PSD/GIF I/O and snapshot editing/history; see [Image Package](Lambda_Image.md) | `input(path, 'psd')` and automatic PSD detection |
| `ime.chinese` | `lambda.ime.chinese` | M4 Traditional profile | Simplified and OpenCC-prepared Traditional pinyin packs, pure candidates/session reduction, partial selection, punctuation, paging, settings and explicit learning persistence; the DOM adapter adds native candidates, profile selection and window/document opt-in. See [Chinese IME](Lambda_IME_Chinese.md). Namespace: D9.3.4. | `./lambda.exe view test/demo/ime/chinese.ls` (prepare the local pack first) |
| `doc.wiki` | `lambda.doc.wiki` | Initial static profiles | Wiki-family writers, shared tasks, and loss reports | `lambda convert -t wiki:<flavor>` |
| `doc.misc` | `lambda.doc.misc` | Initial source profiles | AsciiDoc, man, Typst, EML, vCard, and iCalendar writers | `lambda convert -t asciidoc` / `man` / `typst` / `eml` / `vcf` / `ics` |
| `doc.ebook` | `lambda.doc.ebook` | Initial horizontal LTR EPUB profile | EPUB 2/3 publications, chapters, navigation, search and an interactive reader | `lambda view book.epub`; also `layout`, `render`, and file-browser preview |
| `doc.diff` | `lambda.doc.diff` | Initial unified review profile | Grammar-based patch parsing, local Git comparisons, virtual review and complete static HTML | `lambda view change.patch`; also `.diff`, `layout` and `render` |
| `archive` | `lambda.archive` | Phase 1 (pages, renderings, search, media and gallery engines, job service) | Archives what a link points to — a page with its resources, a video or audio stream with subtitles, a gallery, a file — into a searchable library of ZIP snapshots with readable HTML, Markdown, PDF and thumbnails; all in Lambda, no external tools; see [§23](#23-archive--web-archiving) | `lambda demo archive` |
| `doc.readability` | `lambda.doc.readability` | Library; passes Mozilla's 130 test pages | Port of Mozilla Readability.js 0.6.0: a page's main article, title, byline, excerpt and site metadata, and the `readerable` check | None; import from a Lambda script |
| `ui.dtna` | `lambda.ui.dtna` | Experimental, partial native catalog | Ant-inspired components, collections, scoped tokens and native interaction; see [Lambda UI](Lambda_UI.md) | `lambda view test/ui/dtna_gallery.ls` |
| `ui.workspace` | `lambda.ui.workspace` | Experimental, initial desktop subset | Desktop controls, scoped tokens, toolbars, panes and status surfaces; see [Workspace UI](Lambda_UI_Workspace.md) | `test/lambda/ui_workspace.ls` |
| `ui.bold` | `lambda.ui.bold` | Experimental, initial native subset | Neobrutalist components with thick borders, hard shadows, scoped tokens and shared native controls; see [bold UI](Lambda_UI_Bold.md) | `lambda view test/ui/bold_gallery.ls` |
| `ui.wired` | `lambda.ui.wired` | Experimental, initial native subset | Hand-drawn SVG pencil outlines, paper surfaces, hatched progress, scoped tokens and native controls; see [wired UI](Lambda_UI_Wired.md) | `lambda view test/ui/wired_gallery.ls` |
| `ui.clay` | `lambda.ui.clay` | Experimental, broad native theme | Claymorphism surfaces and 59 components/subcomponents sharing the Ant-inspired behavior layer; see [clay UI](Lambda_UI_Clay.md) | `lambda view test/ui/clay_gallery.ls` |
| `ui.blade` | `lambda.ui.blade` | Experimental, broad native theme | High-resolution fantasy materials, 64 components/subcomponents, inventory slots, abilities and 18 original vector icons; see [blade UI](Lambda_UI_Blade.md) | `lambda view test/ui/blade_gallery.ls` |
| `chart` | `lambda.chart.chart`, `lambda.chart.vega`, `lambda.chart.wordcloud` | Library | Declarative SVG charts, scientific analysis and native 3D figures | No command of its own; `lambda render` and `lambda view` display a script whose result is a chart |
| `map` | `lambda.map` | Library, initial offline subset | Native `<geomap>` viewports with typed GeoJSON, camera interaction, feature queries and vector export | `lambda render` and `lambda view` display a script whose result is a map |
| `scene3d` | `lambda.scene3d` | Native macOS profile | Declarative meshes, cameras, PBR materials, procedural/HDR environments, shadows, asset loading and animation, GLSL shader materials and GPU simulations; see [§27](#27-scene3d--native-3d-scenes) | `lambda view` / `render` on a script; `lambda demo dreamhome` |
| `scene3d.runtime` | `lambda.scene3d.runtime` | Initial native coordinator | Live scene play/pause, bounded fixed stepping, authored-state seek/reset, time scale and runtime snapshots; see [runtime controls](#live-runtime-controls) | Event handlers in `lambda view` |
| `scene3d.viewer` | `lambda.scene3d.viewer` | Native macOS profile | Retained pointer, keyboard and toolbar camera controls over a Scene3D DOM node (**D9.5.6**) | Lambda Scene3D demo views |
| `scene3d.outdoor` | `lambda.scene3d.outdoor` | Native macOS profile | Fog, thin-foliage lighting, shared lit wind and physical ripple-water helpers; see [Outdoor Scene3D](Lambda_Scene3d_Outdoor.md) | `lambda view test/demo/scene3d/outdoors.ls` |
| `scene3d.effects` | `lambda.scene3d.effects` | Native macOS profile | Five animated GLSL image effects, raster/SVG textures and generated SVG sources; see the [effects guide](Lambda_Scene3d_Effects.md) | `lambda demo scene3d` → Make waves |
| `scene3d.motion` | `lambda.scene3d.motion`, `lambda.scene3d.motion.humanoid` | Experimental character profile | FBX and BVH characters in one `<character>` schema: humanoid roles, retargeting, clip operations, mannequins and controller playback in `<scene3d>`; see [§22](#22-scene3dmotion--character-animation) | `input(path, {type: 'character'})`; `lambda view` on a script |
| `physics` | `lambda.physics`, `lambda.physics.scene` | Experimental 3D rigid-body profile | Native primitive/convex dynamics, joints, capsule movement, physical bones and live ragdoll handoff/recovery/replay; mesh/heightfield environments, queries, batch simulation and trajectory analysis. See [Physics](Lambda_Physics.md). | `lambda demo scene3d` |
| `navigation` | `lambda.navigation` | Native graph/grid/surface navigation | Pure versioned descriptions and bounded weighted A* in 2D/3D, filters, diagonal clearance and separate partial/budget outcomes. `lambda.navigation.scene` adds live cached assets, atomic edits and generation checks through the existing Radiant host boundary (S12.1.1v3 / D5.5.2 / D9.5.6). Triangulated surfaces add grounded funnel paths; 3D cell bakes add radius/headroom/step clearance and compatible-profile queries. Source-selected background bake jobs support cancellation, generation-checked publication and saved compiled cells with provenance. The polygon profile merges clear cells into convex regions and uses grounded portal/funnel paths. Explicit links retain projected action endpoints, clearance, costs and traversal indices, with live connectivity edits over retained bakes. Baked-surface capsule agents now use the shared scene clock, fair path budgets, controller feedback, explicit link handoff and bounded priority-aware local avoidance. Bounded dirty-tile jobs reuse raw clearance cells and publish immutable, generation-checked domains. Broader agent profiles remain pending. See [Navigation](Lambda_Navigation.md). | `test/lambda/navigation/paths.ls` |
| `procart` | `lambda.procart`, `lambda.procart.{rng,noise,field,sample,geom,grid,lsystem,text,wfc,terrain,adapt}` | Library with native batch kernels | Deterministic procedural art: keyed randomness, value/Perlin/simplex/cellular noise with fractals and domain warp, scalar fields, Poisson and low-discrepancy sampling, Delaunay/Voronoi/hull/Lloyd, caves, dungeons and mazes on tile grids, L-systems with a turtle, Tracery-style text grammars and Markov names, wave function collapse, eroded terrain with rivers and biomes, and image/chart/Scene3D/SVG adapters; see [§26](#26-procart--procedural-art) | None; import from a Lambda script |
| `procart.scene` | `lambda.procart.{scene,adjust,primitive,rules,schema}` | Library | Stored drawing grammar: nine primitives, inherited transforms/colors, weighted recursive rules, cumulative repetition, global limits and caller-registered semantic objects; see [§26](#26-procart--procedural-art) | Procart → Form studies |
| `procart.plant` | `lambda.procart.{plant,apex,turtle3d,plant_schema}` | Library | Per-level tree architecture, directional growth, tapered branches, instanced leaves, shared-skeleton LODs and parametric L-system plants; see [§26](#26-procart--procedural-art) | Procart → Tree studio |
| `procart.environment` / `procart.herb` | `lambda.procart.environment`, `lambda.procart.herb` | Library | Global solar/weather data, responsive flowers, reeds and rooted wind; see [§26](#26-procart--procedural-art) | Procart → Meadow |
| `procart.surface` | `lambda.procart.surface` | Library | Reusable seeded surface patterns, role bindings, leaf/branch UVs and object-space texture atlases; see [§26](#26-procart--procedural-art) | Procart → Form studies → Grain, bark & veins |
| `procart.watercolor` | `lambda.procart.watercolor` | Library with procedures | Curtis et al.'s watercolor simulation, ported from Washes: water flow over a paper height field, pigment transport, adsorption and granulation, edge darkening, drying and Kubelka-Munk glazing; see [§26](#26-procart--procedural-art) | Procart → Watercolor |
| `color` | `lambda.color` | Library | Shared hex/RGB/RGBA parsing, HSV conversion and byte-quantized RGB hex formatting; see [§28](#28-color--pure-color-utilities) | Used by slides, UI palettes and procedural art |
| `io.path` | `lambda.io.path` | Library | Pure lexical path operations, including resolution and caller-supplied home expansion | Finder and Word document opening |
| `music` | `lambda.music` | Experimental notation and MIDI profile | Native MusicXML/MXL, SVG sheet music, SMF import/export and SoundFont playback; see [MusicXML and MIDI](Lambda_Music.md) | `lambda view test/music/_player.ls` |
| `graph` | `lambda.graph.layout`, `lambda.graph.transform`, `lambda.graph.structurizr.structurizr` | Library | Layered graph layout, and diagram rendering for Mermaid, Graphviz DOT, D2 and Structurizr sources | `lambda render`, `view`, `layout` and `convert -t html` on `.mmd`, `.dot`/`.gv`, `.d2`, `.dsl`/`.structurizr` |
| `math` | `lambda.doc.math.math` | Library | Typesets LaTeX math as HTML | Markdown math in `lambda view`, `layout` and `render`; math inside LaTeX documents |
| `latex` | `lambda.doc.latex.latex` | Library | Renders LaTeX documents as HTML | `lambda convert x.tex -t html`; `lambda view`, `layout` and `render` on `.tex`/`.latex` |
| `docx` | `lambda.doc.docx` | Library, bounded import and export profiles | Reads DOCX and writes editable Word packages from HTML elements (§15) | `lambda view report.docx`; `--archive` browses package parts |
| `pptx` | `lambda.doc.pptx` | Library, initial editable import profile | Reads PowerPoint into slide elements, HTML handouts, text, metadata and diagnostics (§28) | `lambda view deck.pptx`; also `layout`, `render`, document-browser preview and Slides Open |
| `xlsx` | `lambda.doc.xlsx` | Library, bounded workbook import | Reads spreadsheet values, formulas and basic styles into a semantic workbook (§31) | Sheet demo Open |
| `docbook` | `lambda.doc.docbook` | Library, initial core HTML profile | Native port of xslTNG 2.8.5 for DocBook 5.x HTML/CSS, including chunked publications (§16) | `lambda view file.xml` recognizes DocBook roots; scripts export HTML publications |
| `pdf` | `lambda.doc.pdf.pdf` | Library, experimental | Renders PDF pages as SVG, and whole documents as HTML | `lambda view`, `layout` and `render` on `.pdf` |
| `eps` | `lambda.doc.eps.eps` | Library, initial vector profile | Interprets EPS PostScript and produces standalone SVG | `convert -t svg`, `view`, `layout` and `render` on `.eps`/`.epsi` |
| `grammar` | `lambda.grammar.xpath`, `lambda.grammar.xquery`, `lambda.grammar.jq`, `lambda.grammar.rnc` | Library, experimental | Grammars for other languages, written as Lambda `grammar` declarations: XPath 3.1, XQuery 3.1, jq 1.7.1 and RELAX NG compact parse to Lambda element trees | None |
| `openapi` | `lambda.openapi.openapi`, `lambda.openapi.server` | Experimental | Route listing, Lambda type generation, validation and Swagger UI pages for OpenAPI specs | None |
| `slide` | `lambda.slide` | Library, experimental | Slide elements, cues/effects, presenter console/navigation tools, themes/layouts/masters, a deck clock with host control, snapshots and handouts; see [Slide Presentations](Lambda_Slide.md) | `lambda view deck.slides` |
| `doc` | `lambda.doc.doc_viewer` | Engine internal | The document viewer and project browser, with its startup splash and icon font | Bare `lambda view`; `lambda view` on `doc_viewer.html` adds the startup splash |
| `editor.app` | `lambda.editor.app` | Engine internal | The document-authoring application | `lambda edit` |
| `editor.draw` | `lambda.editor.draw`, `lambda.editor.draw.view` | Library, initial native profile | Programmable vertices/edges, ports, custom paths, topology, shared transactions, archives, SVG and retained canvases | `lambda demo draw`; see [drawing API](../lmd/package/editor/draw/README.md) |
| `editor.raster` | `lambda.editor.raster`, `lambda.editor.raster.view` | Library, initial layered profile | Raster layer/group commands, masks, translation, shared history and retained canvas presentation | `lambda demo photos`; see [raster API](../lmd/package/editor/raster/README.md) |
| `editor` | `lambda.editor.model` | Engine internal | The editing model: documents, selections, transactions, history | `lambda edit`, through `editor.app` |
| `dom` | `lambda.dom.dom` | Engine internal | Browser behaviour for HTML: form controls, links, focus, `<details>`, editing | `lambda view` on interactive pages |

The engine packages are importable like any other, but their API follows the engine and is not stable; this page describes them only briefly (§10).

---

## 2. Importing a Package

Each `.ls` script is a package; directories group related packages (D9.3.1).
Import a script by its full dotted path, usually with an alias:

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

The `lambda.*` root is reserved for everything Lambda ships (D9.3.4). Shipped package paths map onto the `package/` directory of the Lambda home:

| Import path | Loads |
|-------------|-------|
| `lambda.<package>.<module>` | `<LAMBDA_HOME>/package/<package>/<module>.ls` |
| `lambda.ui.dtna` | `<LAMBDA_HOME>/package/ui/dtna.ls` (explicit public module) |
| `lambda.ui.workspace` | `<LAMBDA_HOME>/package/ui/workspace.ls` (explicit desktop UI module) |
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
- **`lambda.doc.*` groups document packages and the bundled viewer.** Math, PDF and LaTeX live in `package/doc/{math,pdf,latex}/`, matching their `lambda.doc.*` imports (D9.3.4). The viewer and its resources live in `package/doc/`. `lambda.math` remains the built-in math module.
- **An alias is a binding name**, so it can be neither a keyword nor `lambda` itself (S16.10.1v2). Choose another alias for the `edit` package:

```lambda error=E100
import edit: lambda.editor.app
```

- **Packages are compiled from source.** A module is compiled the first time a process imports it and is then shared by every script that imports it; compiled code is only a local cache (D9.3.3).

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

Any `.ls` file is a package in this sense: a compilation unit whose `pub` declarations (`pub fn`, `pub pn`, `pub let`, `pub type`) are its exports (D9.3.1). Its module-scope bindings are immutable, so `var` belongs inside a `pn`. Import it with a leading dot, which resolves from the importing file's directory, so the pair below works from any working directory:

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

Keep your packages next to the scripts that use them. The `lambda.*` namespace is reserved for what Lambda ships (D9.3.4), and there is no package manager, registry or search path for third-party packages: share a package by sharing its source files. See [Lambda_Modules.md](Lambda_Modules.md) for the full import rules.

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
Lambda IO (**D9.1.2**). Chart labels use this API without constructing HTML.
Results include advance/width, height, baseline, separate `ink` and `logical`
bounds, and `resolved_fonts` (the actual face families used, including fallback).
An empty string has zero bounds and an empty font list. Placement and fallback
are shared with Radiant's SVG painter; this does not add a new shaping engine.
`radiant.font_metrics(font, faces)` returns ascent, positive descent, line gap /
height, x-height, cap-height, space width, units per em, underline thickness,
OS/2 subscript/superscript baseline offsets, and resolved family.
Both APIs return copied values or `null` on invalid input/failure
(**D5.2.2, D7.1.1, S12.1.1v2**). Batch-local font caches expire with the query.

The existing read-only host function
`radiant.measure_svg_text(html, width, height)` measures each direct `<text>`
child of the first SVG in the HTML body, preserving request order. Other SVG
children can supply font resources. It returns copied `width` (advance),
`height`, `baseline`, and baseline-relative `left`, `top`, `right`, `bottom`
bounds covering character cells and visible glyphs. Empty text has zero
metrics; invalid input or failed measurement returns null. Viewport dimensions
must be positive integers. This follows **D7.1.1** / **S12.1.1v2**; only scalar
facts escape the transient document under **D5.2.2**.

`radiant.graphemes(text)` returns the original text as an array of extended
grapheme clusters, following the bundled Unicode segmentation data. Empty
text returns `[]`; invalid UTF-8 returns null. It preserves spelling without
normalization and is read-only under **D7.1.1** / **S12.1.1v2**. Chart truncation
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
stays pure (S9.1.4, S12.1.3, D7.2.3).

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
It ships as Lambda source under the same package namespace (D9.3.1–D9.3.4).
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
mechanisms (**D9.3.1–D9.3.4/D9.3.5, S2.4.3v3/S12.1.3**).
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
(**S2.1.4, S2.6.2–S2.6.4, S17.4.1, D5.5.2**).

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
cross-platform and hosted-player parity are still open (**D9.5.6, D5.5.1**).

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

Radiant automatically connects the `lambda-graph` layout to the package callback when it encounters the generated HTML (**D9.5.6**); no installation call is needed. This also works when serialized HTML is opened in a fresh Radiant process. Other HTML renderers do not implement this layout, so the HTML from `to_html` is meant for Radiant. From the command line:

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
| `to_latex(ast)` | Writes a TeX/AsciiMath AST as LaTeX source |
| `to_ascii(ast)` | Writes the existing AsciiMath subset, including command spellings and function-word coalescing |
| `format(ast, options = null)` | Formats as `latex` (default), `ascii` or `mathml`, selected by `options.flavor`; Typst math returns an error |

`lambda.doc.math.format` exposes the three source-formatting functions without
importing Radiant. Runtime `format(ast, 'math')`, the `math-latex`, `math-ascii`
and `math-mathml` targets, procedural `output` and CLI conversion all use this
package. Native IO retains generic XML serialization; math traversal and
conversion live entirely in Lambda (**D9.1.1**, **D9.3.4**, **D10.2.3**).

The font-independent `lambda.doc.math.mathml` module exposes the same conversion
and parsing APIs without importing Radiant. Runtime
`format(ast, {type: 'math', flavor: 'mathml'})` and CLI `-t math-mathml`
use its exporter. It supports common fractions, scripts/limits, roots,
groups/fences, alphabets, simple text, accents, spacing, phantoms and matrices;
unsupported TeX constructs report errors. The renderer also accepts a documented
Presentation MathML subset through its separate Core policy, sharing measured
SVG geometry with TeX. Radiant paints that subset in HTML while preserving the
MathML DOM; full CSS and accessibility integration remain open (**D10.2.3**).

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
Algorithms stay in Lambda and font parsing stays in `lib/font` (**D9.1.1**).

The legacy `lambda.doc.math.mathlive` renderer and its fixed metrics tables
have been removed; use `lambda.doc.math.math`.

`lambda view`, `lambda layout` and `lambda render` typeset the `$…$` and `$$…$$` math of Markdown documents through this package, and the `latex` package uses it for the math inside LaTeX documents.

---

## 7. `latex` — LaTeX to HTML

The latex package renders a parsed LaTeX document as HTML. Parse with `latex.parse_file(path)` or `latex.parse_source(text, base)`. Every LaTeX parse first runs Lambda's TeX expansion engine (**D10.2.2**): document macros, `.sty`/`.cls` files beside the document, bundled programming packages such as `etoolbox`, `xkeyval` and expl3, and Lambda's own bindings for `physics`, the `mathtools` macros, `xspace`, `csvsimple`, `unicode-math`'s alphabets and the configuration macros of `tocloft`, `authblk`, `titling`, `minitoc`, `glossaries`, `nomencl` and `acronym` run as TeX, while Radiant keeps layout. A bare `input(path, 'latex')` also expands, but without the package's adapter registry, so parse through the package when you render. The package covers the `article`, `book` and `report` document classes and profiles of the common others (KOMA-Script, memoir, the AMS classes, IEEEtran, llncs, elsarticle, revtex, acmart, `letter` and `exam`), sectioning with numbering, lists, tables, footnotes, boxes, colour, spacing, the `picture` environment, `\newcommand` macros, and math through the `math` package.

Common `\usepackage` declarations select the shipped script adapters for `amsmath`, `amssymb`, `graphicx`, `hyperref`, `geometry`, `xcolor`, `booktabs`, `biblatex`, `babel`, `enumitem`, `microtype`, `siunitx`, and TikZ. Package names select static source modules under **D9.3.1–D9.3.4**; TeX source runs only as TeX inside the engine's sandbox and never becomes Lambda code (**S1.8, D10.2.2**). The [package compatibility matrix](../vibe/Lambda_Pkg_Latex.md#9-compatibility-matrix) lists supported commands and output limits.

The bounded `biblatex` profile reads multiple local `.bib` resources, supports ten common entry types, six built-in citation/bibliography styles, source-order citation commands, `\nocite`, filtered and scoped `\printbibliography`, and English/German/French strings. It handles common BibTeX strings and inheritance, creates internal citation links, and reports missing or unsupported requests with source locations (**D9.1.2, S12.4.1, S7.4.1–S7.4.4**). `backend=biber` selects this local profile; no Biber process or arbitrary style file is run. See [Phase 2 behavior and boundaries](../vibe/latex/Lambda_Design_BibTeX_CSL.md#21-the-built-in-biblatex-and-natbib-profile).

A document that declares `\usepackage[style=apa]{biblatex}`, `style=ieee`, or `\usepackage[authordate]{biblatex-chicago}` (or its default notes style) is formatted by the matching bundled CSL style automatically. The `csl-style-alias` diagnostic names the CSL style used, and biblatex options or style customizations that CSL cannot follow are reported rather than applied. An experimental CSL processor is also available directly through `render_result(ast, {base_uri: ".", citeproc: {style: "apa", locale: "en-US"}})`. It is implemented entirely in Lambda script within `lambda.doc.latex` (**D9.3.1–D9.3.4, S1.8, S12.4.1v2**). It accepts local BibTeX/BibLaTeX or CSL JSON resources, evaluates custom CSL XML, and bundles `ieee`, `apa`, `chicago-author-date`, and `chicago-fullnote-bibliography` styles plus English/German/French locales. It preserves citation links, scoped prints, and note-style footnotes. CSL mode rejects conflicting legacy style/sorting options and reports unsupported constructs. Locale collation and complete CSL conformance remain outstanding. See the [CSL API, resource options and coverage](../lmd/package/doc/latex/citeproc/README.md).

For PDF export, `hyperref` links and anchors become link annotations and destinations, and its title, author, subject and keywords become PDF Info metadata. `geometry` supplies fixed `@page` paper size and margins for a one-page export; automatic page breaking still requires the separate paged path. `graphicx` trim/clip uses the same supported CSS `clip-path` shapes in SVG and PDF. These remain Lambda-script package policies under **D9.3.1–D9.3.4**; unsupported options and commands follow **S7.4.1–S7.4.4** diagnostics.

Phase III adds bounded theorem/proof, table, language, TikZ/PGFPlots, `natbib`, `caption`/`subcaption`, English `cleveref`, and unhighlighted `listings` profiles. Raw code and document-local `filecontents` bibliographies are preserved. `hyperref` headings and plain-text `\pdfbookmark` titles become hierarchical PDF outlines. `render_result.assets` records image/bibliography paths, origin, availability and source offsets; availability checks local existence, not successful decoding or remote fetching. See the [sample compatibility report](../test/latex/samples/COMPATIBILITY.md).

For the shared paged PDF path, pass `{target: "pdf", paged: true}` while generating the standalone HTML, then export it with `lambda render --paged`. The bounded `fancyhdr`/`lastpage` profile supplies text, running marks and page counters. Continuous output diagnoses unresolved page counters. Paged tables and columns, native Thai shaping/line breaking, exact font substitutions, and TeX typography remain explicit limits (**S7.4.1–S7.4.4**). Unicode PDF glyphs use searchable text with embedded font programs and ToUnicode maps. Lua, biblatex style files, and package files from a TeX installation are excluded (**S1.8, D10.2.2**).

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

`options` may be `null`. `standalone` selects a complete page; `base_uri` resolves relative graphics and bibliography resources; `target: "pdf"` or `target: "svg"` enables output-specific diagnostics. The file entry points derive `base_uri` from the file path. `render_document_file` sets `source_path`, which the LaTeX package uses as the resource base when `base_uri` is absent (**D9.1.2**). The document class comes from `\documentclass` in the source. On the command line, `lambda convert paper.tex -t html -o paper.html` writes the rendered body without the LaTeX stylesheets, `--full-document` writes the standalone page instead, and the legacy `--font-option` switch does not change the math package's font selection; `lambda view`, `layout` and `render` always render the standalone page.

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

This output profile supports continuous and selected paged
output (`{paged: true, pages: "1,3"}`), backgrounds and borders,
positioned PDF text, image pixels with alpha masks, translucent fills/text,
transforms and shared native clip geometry.
The native walker owns traversal; the Lambda schema exposes properties and
typed nullable references, without sibling/parent links (**S11.5 / D4.4**).
Lambda writes drawing operators, resources, stream lengths and cross-reference
offsets. Text uses selected embedded fonts, ToUnicode and logical ActualText,
so ordinary text remains searchable and selectable. Affine transforms remain
PDF text; native/variable/color fonts may use generated Type 3 font programs.
Decorated text and simple opacity groups preserve text operators. Complex
graphics use vector resources or shared native raster captures, with text
semantics retained where available. Links, metadata, destinations, outlines and
page labels are written by the package. The production `lambda render` PDF
route uses this same package (**S2.6.6v3 / D3.5.1**). Font subsetting and
complex SVG text paint coverage remain follow-up work.

Fixed intake accepts `import_pages` (`"all"` or positive source ordinals/ranges such as `"2,49-53"`) and `max_pages` (a positive admitted-page budget, default 10,000). It validates original media/crop/bleed/trim/art boxes and signed quarter-turn rotation, retains the original one-based source ordinal, and embeds self-contained SVG vector resources. Native commands use `lambda view file.pdf --paged` and `lambda render file.pdf --paged -o result.pdf`; `--import-pages` and `--import-page-limit` select source pages and set the import budget. Preview `--pages` and output `--export-pages` address the resulting physical sequence independently. Failed imports preserve an existing output file. This adapter uses the configured package bridge under **D9.5.6**, dependency policy under **D9.1.2**, and the common selected-page lifetime under **D5.5.1 / D5.1.4**. Reversed/empty boxes, non-default `UserUnit` and unresolved image/form handles currently receive explicit profile errors. PDF page-label number trees retain decimal/Roman/repeated-letter styles, prefixes, starts, Unicode and empty labels through source selection and PDF export. Invalid label trees fail paged intake. Text selection and complete content-stream conformance remain outstanding.

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

These packages implement parts of the engine in Lambda. They load automatically when a command needs them, and their internal APIs follow the engine. The `lambda.editor.draw` and `lambda.editor.raster` facades in §10.2.1–§10.2.2 are available to applications; other internal interfaces remain unstable.

### 10.1 `edit` — the `lambda edit` application

`lambda.editor.app` is the document-authoring application behind `lambda edit` (see [Lambda_CLI.md](Lambda_CLI.md)). The CLI passes it a local path, and `open_document(path, options)` does the rest: it picks a format by suffix (`format_for_path(path)`, over `formats`: Markdown `.md`/`.markdown`, HTML `.html`/`.htm`, SVG `.svg`), reads and imports the file, refuses a document it could not write back without loss, and builds the editing page with its toolbar and rich-text or drawing surface. The format adapters are `lambda.editor.format.markdown`, `lambda.editor.format.html` and `lambda.editor.format.svg`; saving and reloading live in `lambda.editor.session`.

### 10.2 `editor` — the editing model

`lambda.editor.*` provides the editing model: documents (`core.doc`), schemas (`schema.markdown`, `schema.document`, `draw.schema`), invertible steps and transactions (`core.step`, `core.transaction`), history (`core.history`), commands, paste handling, decorations and collaboration. `lambda.editor.model` is the model facade: `edit_open(doc, schema, selection)` creates an editor value, `edit_exec(editor, command)` applies an `edit_cmd_*` command, and `edit_mount` binds the model to a Radiant surface. Import it with `import lambda.editor.model`. The CLI application uses this model for the formats it opens.

Shared desktop editors also use `lambda.editor.table.design` for table style
choices and cell appearance; `lambda.editor.math.render` for equation artwork;
and `lambda.editor.graphics.{chart,shapes,diagram,wordart}` for graphic
construction. Validators take explicit bounds, with the existing office
profile available from `lambda.editor.graphics.dimensions`. Application code
owns its document transactions and object placement.

### 10.2.1 `editor.draw` — vertices and edges

`lambda.editor.draw` builds identified vertices and edges, queries connectivity,
validates connections, applies shared editor transactions, and exports SVG.
It supports twelve built-in shapes, declarative custom polygon descriptors,
transformed named ports, straight/orthogonal/curved routes, waypoints, custom
line/cubic paths, loops and parallel edges, plus version-4 archives with legacy
v1–v3 reading. `lambda.editor.draw.view.surface(document, {id, width, height})`
provides an independent retained canvas. The Draw demo reuses the package while
keeping its application shell and reading existing v1–v3 files. Package ownership follows
**D9.3.4**, with pure model edits and procedural presentation under
**S1.4, S12.1.3**. See the [API and support boundary](../lmd/package/editor/draw/README.md).

### 10.2.2 `editor.raster` — layered raster editing

`lambda.editor.raster` uses the shared `lambda.image` document tree. Its
`new_document`, `open_session` and `exec` functions provide stable layer identities,
checked commands, grouping, translation, alignment/distribution, masks, retained
adjustments and undo/redo.
`layers`, `lookup`, `commands` and `command_state` support independent inspectors.
`lambda.editor.raster.interaction` groups pointer previews into one transaction;
the optional `.view` module presents copied pixels in a retained native canvas.
Package placement follows **D9.3.4**, snapshots follow **S9.1.2 / S9.2.2**, and
procedural presentation follows **S12.1.3 / D5.5.2**.

The [Photos demo](../test/demo/photos/README.md) supplies the workspace and file
dialogs. It supports tiled painting/selections, crop previews, affine transforms,
retouch, editable artwork/styles, saved coverage, tabs and bounded action batches.
Editable `.photos` projects preserve the supported scene; PSD interchange retains
its qualified subset. PNG/JPEG exports derive resized pixels with explicit
transparency/background policy and JPEG quality. Native samples remain `u8`;
high-depth/ICC editing and background jobs are outstanding. See the
[API and support boundary](../lmd/package/editor/raster/README.md).

### 10.3 `dom` — browser behaviour for HTML

`lambda.dom.*` implements the user-agent behaviour of HTML documents in Radiant, written as `view` templates with `on` handlers (see [Reactive_UI.md](Reactive_UI.md)): form controls and their state, constraint validation, form submission and `application/x-www-form-urlencoded` encoding, link navigation, sequential focus and `autofocus`, `<details>` and `<summary>`, context menus, keyboard activation, caret movement, scrolling keys, IME composition, ARIA reflection, and the editing of `contenteditable` regions, including `designMode`, `execCommand` and the `queryCommand*` functions. Radiant loads `lambda.dom.dom` once per document, the first time an event reaches an element the package governs, and registers its templates as behaviour templates on the page's elements rather than as the page's own templates. Editing policy belongs to this package, never to native code (D10.2.1).

#### Native DOM API colours

`import dom` exposes the native DOM API used by those behaviour packages and
applications. Its published signatures distinguish reads (`fn`) from effects
(`pn`), as required by **D7.1.1**. Reads remain `fn` even when the node can be
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
| `pn clipboard_read() -> map` | Read one owned native clipboard snapshot as `{html, text, image}` (nullable strings). `image` is an embedded raster data URI, independent of the HTML alternative. Image-only clipboards also become HTML with an embedded image. Packages choose paste policy (**D10.2.1**, **D7.1.1**). |
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

The engine-specific DOM helpers follow the same rule (**D7.1.1**):

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

Bare `lambda view` loads `package/doc/doc_viewer.ls` from Lambda home; the
adjacent `doc_viewer.html` is its startup splash. `lambda demo` with no name opens
the demo index, and named demos use `lambda demo <name>`; see the
[demo catalog](../test/demo/README.md).
The viewer browses the current working directory and loads documents on selection. Its
Seti icon font and license ship in `package/doc/icons/`, and its bundled math fonts
come from `package/doc/math/fonts/`. These application resources belong to the shipped
`lambda.doc` package (D9.3.4); UI event fixtures remain under `test/ui/`.

---

## 11. `map` — Geographic Maps

`lambda.map` is an explicit public package entry under D9.3.4. It produces a
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
| `snapshot(node)`, `query_displayed(node, point_or_box, options = {})` | Functions reading the last painted native frame (D7.1.1) |
| `model(spec, options = {})`, `interactive(spec, options = {})` | A reactive view model / its applied native viewport |
| `to_svg(model, viewport = null)` | An SVG element from the same native paint compiler (D9.5.6) |

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
resize/removal (D5.5.2, D6.3.3). Input coordinates use shared inverse CSS-plane
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
| `ui.workspace` | `test/lambda/ui_workspace.ls`; native controlled-input integration in `test/ui/controlled_bindings.json` |
| `ui.bold` | `test/lambda/ui_bold/`; native controls in `test/ui/bold/` and `test/ui/bold_gallery.json` (`make test-ui-bold`) |
| `ui.wired` | `test/lambda/ui_wired/`; native controls/geometry in `test/ui/wired/` and `test/ui/wired_gallery.json` (`make test-ui-wired`) |
| `ui.clay` | `test/lambda/ui_clay/`; native controls/collections/scopes in `test/ui/clay/` and the two galleries (`make test-ui-clay`) |
| `ui.blade` | `test/lambda/ui_blade/`; native RPG/collection/scope/responsive fixtures in `test/ui/blade/` and two galleries (`make test-ui-blade`) |
| `map` | `test/lambda/map/`; native rendering and input in `test/test_map_gtest.cpp`; PNG/SVG/PDF fixtures in `test/map/` (`make test-map`, `make test-map-export`) |
| `scene3d` | `test/lambda/scene3d/`; native viewer/control and unit cases (`make test-scene3d`); Dreamhome model and UI replays in `test/demo/dreamhome/tests/` and `test/ui/dreamhome/` |
| `graph` | `test/lambda/graph/mermaid/`, `test/lambda/graph/graphviz/`, `test/lambda/graph/structurizr/`; `test/lambda/graph_layout*.ls` and `test/lambda/graph_transform_*.ls` |
| `math` | `test/lambda/math/` |
| `music` | `test/lambda/music/`; score fixtures and viewer example in `test/music/` |
| `latex` | `test/lambda/latex/` |
| `docx` | `test/lambda/docx/`; synthetic and pinned reference documents in `test/input/docx/` |
| `pptx` | `test/lambda/pptx/`; reproducible synthetic and pinned independent documents in `test/input/pptx/`; `test/ui/pptx_player.json`, `slide_pptx_import.json` and `doc_viewer_pptx_pane.json` |
| `xlsx` | `test/lambda/xlsx_import.ls`, `test/demo/sheet/tests/sheet_xlsx_test.ls`; synthetic fixtures in `test/input/xlsx/`; `test/ui/sheet_xlsx.json` |
| `archive` | `test/lambda/archive/` (plan, pages, renderings and search, media and gallery engines over the local fixture site `site/`, service); the demo's model golden and replay in `test/demo/archive/tests/` |
| `doc.readability` | `test/lambda/readability/` (API and Readability.js-checked pages); the Mozilla corpus by hand with `./lambda.exe run test/lambda/readability/_corpus.ls` |
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

The EPS package lives under the document namespace (**D9.3.4**). A C+ parser
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

## 15. `docx` — Word DOCX Import and Export

`lambda.doc.docx` is a shipped script module under D9.3.1/D9.3.4. It reads a
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
Charts reuse `lambda.chart.chart` for SVG layout (D9.3.4), retain explicit RGB
series colors and titles, and contribute a data table to extracted text.
Paragraph-relative column anchors support left/right square wrapping,
centered largest-side wrapping, and top/bottom wrapping at paragraph top.
Signed column-start and paragraph offsets with square wrapping on both sides
use CSS float margins (D9.3.4); this remains a flowing approximation of Word.
No-wrap column/paragraph anchors with explicit offsets retain their extents
and front/behind-text layer order. Inset raster crops keep their saved frames.
Flat DrawingML rectangles support validated solid RGB/theme fills with no
outline; a supported modern compatibility branch takes precedence over its
fallback. Decorative rectangles are hidden from accessibility text (S1.8,
D9.3.4).
The viewer also reads the document theme, inherited and direct run properties
(font family, size, color, tracking and capitalization), paragraph spacing,
table grid widths, cell shading and borders, row heights, and page size/margins.
Values are converted from OOXML units into CSS; the named font must be
available to the host font resolver for a matching face. CSS remains a flow
preview and does not reproduce Word's full layout engine.
Footnotes, endnotes and comments become numbered links and appended ordered
sections, with each kind numbered in first-reference order and repeated
references sharing one entry (D9.3.4). Links in annotations resolve against
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
slots, preserving the alignment of financial tables (D9.3.4).

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
placement,
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
(S1.8, D9.3.4). See the [fixture inventory](../test/input/docx/README.md).

### DOCX export

`export_document(document, options = null)` accepts HTML text or an HTML-style
element tree and returns `{archive, diagnostics}`. `archive` is a constructed
ZIP `<fs>` tree with real WordprocessingML paragraphs, runs, styles, numbering,
relationships and media; Word can edit the resulting document. This extends the
script package under D9.3.1/D9.3.4 and uses central IO under D11.1.2.

```lambda
import docx: lambda.doc.docx
pn main() {
    let report = docx.save(<article <h1 "Report"><p "Editable in Word">>,
        "temp/report.docx", {title:"Report", unsupported:"error"})^;
    return report.diagnostics
}
```

`save(document, path, options = null)` is procedural and returns
`{path, diagnostics}` after an atomic, deterministic ZIP write. Strict export
finishes conversion before publishing, so rejected conversions preserve the
existing destination. A pure caller can use `export_document` and pass its
`archive` to `output(..., {format:'zip', atomic:true, deterministic:true})`.
Generic `format(value, 'docx')` is not provided.

The export profile includes paragraphs/headings, Unicode, tabs and breaks,
bold/italic/underline/strike/subscript/superscript, inline font family/size/color,
tracking and shading, paragraph alignment/spacing/indentation, page breaks,
ordered/bullet lists with starts/restarts/nesting, table grids with column
widths and merged cells, safe links/bookmarks, raster pictures and page geometry.
PNG, JPEG, GIF, BMP and TIFF bytes are embedded and deduplicated. Pictures use
data URIs or local paths with an explicit `base` folder; remote images are
never fetched. Named fonts must be installed in the application opening the file.

Export options are separate from import options:

| Option | Default | Meaning |
|--------|---------|---------|
| `title`, `creator`, `subject`, `description` | Source title / `Lambda` / absent | Core document properties. |
| `unsupported` | `"warn"` | Return `export-*` diagnostics; `"error"` rejects diagnosed losses (7606). |
| `base` | absent | Explicit base folder for local picture paths. |
| `styles` | `{}` | Tag name to CSS declarations, e.g. `{body:"font-family:Georgia;font-size:11pt"}`. Inline declarations take precedence. |
| `page` | A4, 96 px margins | `width`, `height`, `top`, `right`, `bottom`, `left`, in CSS pixels (96 dpi). |

This is a new document, without unknown original OOXML parts. External CSS
stylesheets, full CSS layout, arbitrary drawings/charts/equations, annotations,
tracked revision semantics, header/footer stories and Word pagination are
outside this export profile. Unsupported elements normally retain visible text
or picture alt text with diagnostics. The Word demo reports editor-only losses
above the document and retains `.word.json` for complete editable snapshots.
`lambda convert report.html -t docx -o report.docx` uses this writer; its default
is strict, and `--loss allow` permits diagnosed degradation.

Design and phased scope: [DOCX package](../vibe/Lambda_Pkg_Docx.md).

`lambda view report.docx` presents the imported Word document with the shared
`lambda.doc.docx.view` presentation module (D9.3.4). The bundled file browser
also previews DOCX and exposes its converted HTML on the Source tab. Use
`lambda view report.docx --archive` for the underlying ZIP tree. Unsupported
content is listed above the preview; Word pagination is outside this profile.

## 16. docbook — DocBook to HTML/CSS

Open the original sample directly with
`./lambda.exe view test/input/docbook/showcase.xml`. The XML viewer recognizes
DocBook namespaces and uses the package's HTML/CSS presentation (D9.3.4);
the document browser keeps the original XML in its Source tab.
The usual extension is `.xml`; the namespace
`http://docbook.org/ns/docbook` identifies DocBook. The current direct-view
roots are `article`, `book`, `set`, `part`, and `reference`.

`lambda.doc.docbook` is an initial native Lambda port of the official DocBook
xslTNG 2.8.5 HTML/CSS transformation (D9.3.4). Java and XSLT are used only to
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
routes calls and owns package invocation (D9.1.1, D9.3.4).

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
**iCalendar**. All six writers are Lambda Script source modules (D9.3.4).

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
`read` captures a local file procedurally (S12.1.1v3, S12.1.3, D9.1.2).
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
| `chapter(book, target, options = null)` | Sanitized HTML/document, source text/map, diagnostics, fixed-layout viewport and kept highlights; target is a spine index or canonical href |
| `locator(book, chapter, offset)` | Versioned source locator; offsets count Unicode scalars |
| `locate(book, locator)` | Validated locator for the same captured publication and source passage |
| `search(book, query, options = null)` | Whole-book (or `chapter`: one spine index) matches with locators, `chapter`, and `before`/`match`/`after` excerpts cut at word boundaries; exact case unless `match_case:false`, any position unless `whole_word:true`; `limit` defaults to 100, maximum 1,000 |
| `reader(book, options = null)` | Interactive reader element for embedding; persistence is off by default |

When embedding `reader`, include `lambda.doc.ebook.reader.stylesheet` in the
host document's style element. Native viewing and browser preview include it
automatically.

Load options are `rendition` (zero-based OCF package index) and
`unsupported:"error"`. Unknown options are rejected. The chapter option
`highlights` takes `{id, spine_occurrence, start, end, color, text}` records
(chapter-text offsets); those whose `text` still matches render as `<mark>`
runs, and the rest are dropped. The publication
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
and the sidebar can be toggled on narrow windows. Enter submits a search; its
toggles match case, match whole words only, and limit the search to the current
chapter. Book-wide results start with their chapter number, and every result
highlights the matched text.
Selecting a passage opens a color bar above it; a color highlights it, and a
highlight that overlaps others absorbs them. A click on a highlight recolors or
removes it, and the sidebar lists highlights with their chapters.
Reflowable chapters scroll, fit one page to the window, or show two pages side
by side with a gutter; three toolbar icons switch between them. Paper textures (antique with a rule frame and corner
flourishes, parchment, linen, newsprint, ivory) sit behind the text, and paged
layouts animate page turns: slide left, slide up, a turn that folds the page
toward the spine, a flip that folds the page from its corner like paper (its
corners curl on hover and can be dragged to turn the page), or dissolve. Fixed-layout
XHTML/SVG pages retain a required source viewport and support fit/zoom.
Selecting EPUB in the file browser opens the same reader; its Source tab shows
the OPF package. `layout` and `render` project the initial reader viewport,
rather than exporting an entire book.

Standalone viewing saves preferences, bookmarks, highlights and the last source location
atomically under `LAMBDA_EBOOK_STATE_DIR`, or the host's XDG/LOCALAPPDATA state
directory (HOME fallback). State is keyed by snapshot fingerprint, so moving
an unchanged book preserves its position. Saved passages are validated before
resume. The embedded browser preview does not save state.

This is an initial horizontal LTR profile. Continuous scrolling between
chapters, RTL/vertical pagination, CFI interchange, obfuscated
fonts, scripted content, media overlays and DRM decoding remain unsupported.
Protected required spine content raises a package error; protected optional
resources use a diagnostic and fallback. EPUB package errors occupy
7800–7809; underlying ZIP/XML errors retain their codes (S7.4.2, S7.4.4).
MOBI and Kindle AZW/AZW3 are outside this package's scope.

`lambda demo book` hosts the reader in an ebook library of Project Gutenberg
classics with progress, collections and search; see
[its guide](../test/demo/book/README.md).

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
presentation are pure (S12.1.1v3/S12.1.3, D9.3.4/D9.3.5).

```lambda no-run
import diff: lambda.doc.diff
diff.repository(".", {mode: "unstaged"})^
```

Open that script with `lambda view review.ls`, or open `.patch`/`.diff` directly.
See [Git and patch review](Lambda_Diff.md) for the API, options, model and limits.

## 21. doc.readability — Article extraction

`lambda.doc.readability` finds the main article of a web page, as Firefox Reader View does: it is a port of Mozilla Readability.js 0.6.0 that passes all 130 of Mozilla's test pages. `parse` takes an HTML string or a parsed document and returns the article as an element (`content`) and as HTML (`html`), with its `title`, `byline`, `excerpt`, `site_name`, `published_time`, `lang`, `dir`, `text` and `length`, or `null` when the page has no content. `readerable` is Readability's quick check that a page is worth parsing. Both are pure; `read` loads a file first (D9.3.4/D9.3.5).

```lambda no-run
import rd: lambda.doc.readability
let page = input("article.html", 'html')^
let article = rd.parse(page, {url: "https://example.com/news/article.html"})^;
[article.title, article.byline, article.excerpt]
article.html   // the article as HTML; article.content is the same as an element
```

| Option | Default | Meaning |
|---|---|---|
| `url` | none | The document URL; links and image sources become absolute against it (or a `<base href>`) |
| `char_threshold` | 500 | Text an article needs before the extraction stops relaxing its rules |
| `top_candidates` | 5 | Top candidates compared when choosing the article container |
| `max_elems` | 0 | Raise an error for pages with more elements (0: no limit) |
| `classes_to_preserve`, `keep_classes` | `[]`, `false` | Class names kept on the article (`page` always), or keep them all |
| `disable_json_ld` | `false` | Ignore JSON-LD metadata |
| `link_density_modifier` | 0 | Added to the link-density limits for removing link-heavy blocks |
| `allowed_video` | YouTube, Vimeo and others | A string pattern for embeds that are kept |

`readerable(source, {min_content_length: 140, min_score: 20})` accepts the same thresholds as `isProbablyReaderable`. Design, fidelity rules and limits: [Lambda_Pkg_Readability.md](../vibe/Lambda_Pkg_Readability.md).

`./lambda.exe demo readability` shows both functions at work: a browser window whose reader icon swaps each page for its extracted article ([demo guide](../test/demo/readability/README.md)).

## 22. scene3d.motion — Character animation

`lambda.scene3d.motion` turns rigged characters and motion capture into animated `<scene3d>` content. FBX (binary 7.1–7.7 and ASCII 7.x) and BVH files convert once into a renderer-independent `<character>` element: a skeleton of named joints with rest poses and VRM 1.0 humanoid roles, baked clips whose channels name joints, and an optional skinned body in character space (metres, Y-up, +Z forward). Clips therefore move between characters by joint name or role. Every function except playback is pure (S12.1.1v3); a `<motion-controller>` hands playback to Radiant.

```lambda no-run
import s: lambda.scene3d
import motion: lambda.scene3d.motion

let hero = motion.load("hero.fbx", {rig: 'mixamo'})^
let walk = motion.load("walk.bvh", {rig: 'cmu', scale: 0.01})^
let rigged = motion.attach(hero, {walk: motion.in_place(walk)^})^
let group = motion.scene(rigged, {id: "hero"})^
s.scene([s.camera({id: "cam", position: [0.0, 1.4, 4.0]}), s.light('ambient'),
    motion.control(group, [motion.motion_state('walk', "walk")], {state: 'walk'})^], {camera: "cam"})
```

| Function | Purpose |
|---|---|
| `load(source, options)` | `input(source, {type: 'character'})` plus `format`, `scale` (to metres), `up` (`'y'` or `'z'`), `rig` and `clip` (keep one clip) |
| `validate(character)` | The schema types plus unique joints, roles and clip names, key counts and skin indices |
| `humanoid(character, rig)` | Roles from the `'mixamo'`, `'cmu'` or `'vrm'` profile, or a `{joint: role}` map |
| `attach(character, {name: source})` | Adds each source's first clip under `name`: copied when rest rotations match, otherwise retargeted; root motion scales by leg length |
| `retarget(clip, from, to, options)` | Transfers each joint's world rotation change from rest; `rest_frame` lets a key stand in for the source rest pose |
| `in_place(character, axes)`, `mirror(character)`, `trim(character, from, to)` | Hold the root's horizontal translation, swap sides and reflect across the YZ plane, or keep a span of keys rebased to 0 |
| `pose(character, clip, key)` | Each joint's world matrix at a key, or at rest |
| `mannequin(character, options)` | A skinned bone figure for a bodiless skeleton such as BVH |
| `scene(character, options)` | A `<group>` with a skeleton, body meshes and one `<animation-clip>` per clip (`id`, `transform`, `autoplay`) |
| `control(group, states, options)`, `motion_state(name, clip, options)` | A `<motion-controller>`: the template requests `state` and `cue`; Radiant crossfades, follows `next` after a `once` state and fires `statechange` |

`input(x.fbx)` and `input(x.bvh)` also give the raw `<fbx>` record tree and the `<bvh>` hierarchy and motion matrix. Limits: 1,024 joints and clips per character, 256 bones per rendered skeleton and 1,024 tracks per clip. FBX conversion follows profile 1 (Mixamo- and Blender-style files); blend shapes, constraints, cameras, lights and extra animation layers are omitted with warnings, and inheritance other than RSrs is an error. Design and acceptance: [Lambda_Pkg_Motion.md](../vibe/Lambda_Pkg_Motion.md); implementation record: [Lambda_Impl_Motion.md](../vibe/impl/Lambda_Impl_Motion.md).

## 23. archive — Web archiving

`lambda.archive` downloads what a link points to, keeps it in several formats, and maintains a searchable library, in the spirit of ArchiveBox and SingleFile. Every engine is Lambda code; it runs no external tool and links no library (design: [Lambda_Pkg_Archive.md](../vibe/Lambda_Pkg_Archive.md), AR4v2).

```lambda no-run
import archive: lambda.archive

pn main() {
    let lib = archive.library("temp/archive", {config: input("archive.yaml", 'yaml')^})^
    let snap = archive.add(lib, "https://example.org/news/story", {tags: ["reading"],
        progress: pn (m) => { print(m.stage, " ", m.received, "/", m.total, "\n") }})^
    print(snap.title, " → ", snap.id, "\n")
    for (hit in archive.search(lib, "\"street trees\" -opinion", {kind: 'page', limit: 10})^) {
        print(hit.snapshot.title, ": ", hit.snippet, "\n")
    }
}
```

| Kind | How it is archived | Snapshot |
|---|---|---|
| page | the page and its stylesheets, images, icons and fonts, rewritten to local paths; scripts and handlers removed | `snapshot.zip` (`index.html`, `source.html`, `readable.html`, `article.md`, `assets/`, `manifest.json`), `text.txt`, `thumb.png`, `page.pdf` |
| video, audio | a direct file, or the medium a page exposes (`<video>`/`<audio>`/`<source>`, `og:video`, `<link rel=enclosure>`, JSON-LD); HLS streams reassembled from their segments (fMP4 or MPEG-TS, byte ranges, the highest variant up to `media.max_height`); WebVTT tracks become the transcript | `media/` with the file(s), subtitles, poster and `info.json`; a media page in the ZIP |
| gallery | a page's images over `gallery.min_pixels` (the widest `srcset` candidate, the full size behind a thumbnail, `og:image`), with captions | `media/` with the images and thumbnails; a gallery page |
| image, file | the payload itself | `media/<name>`; an image page or a file card |

A page whose media cannot be taken (an encrypted stream, a player that loads by script) is archived as a page with the reason in `note` and the media in `remote`. The kind comes from the configuration's rules, a built-in host table, the extension, or the response's content type. Renderings — the snapshot page, Readability's article and its Markdown, the search text, the PDF and the thumbnail — are made by a child `lambda run` worker, so a window never waits on them.

| Function | Effect |
|---|---|
| `library(root, {config})` | open or create a library; the configuration is checked first (`problems(config)` lists its faults) |
| `plan(url, config)` | the kind, engine and settings for a URL, without IO |
| `add(lib, url, options)` / `job(…)` | archive one URL; `options.progress` receives `{type: 'progress', tag, stage, received, total, rate}`; only a failed download raises |
| `service(lib, notify)` | a task that runs up to `defaults.jobs` jobs at once from `{op: 'add' \| 'refresh' \| 'cancel' \| 'stop'}` requests and reports `queued`, `progress`, `done` and `failed` |
| `refresh(lib, id, formats?)` | re-derive outputs from the stored capture, without the network |
| `search(lib, query, options?)` | full-text search over `text.txt` (`io.text_search` syntax), filtered like `snapshots` |
| `snapshots(lib, filter?)`, `remove(lib, id)`, `doctor(lib)`, `with_record(lib, record)` | list, delete, check the library, fold a record into a `Library` value |
| `snapshot_html(capture)`, `readable(capture)`, `markdown(capture)` | the pure derivations over a capture |

Each snapshot folder's `snapshot.json` is the record and `index.json` a cache rebuilt from the folders. Outputs succeed or fail on their own, each with its status. `lambda demo archive` is the interactive front end ([demo guide](../test/demo/archive/README.md)).

## 24. mail — Mail sync and store

`lambda.mail` is the Lambda side of the mail demo: it turns what `lambda.io.mail` returns into mailboxes and messages, and keeps them in a store of raw `.eml` files with one Mark index per mailbox (design: [Lambda_Demo_Mail.md](../vibe/Lambda_Demo_Mail.md) §5.4, ML7v3). The protocol stays in [`lambda.io.mail`](Lambda_Sys_Func.md#lambdaiomail).

```lambda no-run
import wire: lambda.io.mail
import sync: lambda.mail.sync

pn main() {
    let s = wire.open("imaps://imap.example.com", {user: "ada@example.com", password: "app-password"})^
    for (box in sync.mailboxes(s)^) { print(box.name, " ", box.role, "\n") }
    let index = sync.sync_mailbox(s, "temp/mail-store", "b01", "INBOX", false)^
    for (e in index.entries) { print(e.uid, " ", e.subject, "\n") }
}
```

| Module | Contents |
|---|---|
| `lambda.mail.imap` | `mailbox_name` (modified UTF-7), `list_entry`, `select_state`, `fetch_items`, `uid_set`, and the `is_list`/`is_fetch` tests over response tokens |
| `lambda.mail.mime` | `text_body` (format=flowed unwrapped), `html_body`, `html_text` (an HTML body as plain text), `readable_text`, `attachments`, `cid_parts`, `snippet`, `search_text` over a message read with `input(…, 'eml')` |
| `lambda.mail.store` | store paths, `safe_name` (distinct file names, case-insensitive file systems included), `server_id`, `load_index`, `save_index` (atomic), and the index updates `merge_entries`, `refresh_entries`, `with_files`, `with_flag` |
| `lambda.mail.sync` | `capabilities`, `mailboxes`, `sync_mailbox` (headers of new messages, flags of known ones, expunges, UIDVALIDITY resets), `fetch_message` (the body and its search sidecar), and the shared `file_message`, `header_entry` and `body_entry` (a row's preview and attachment count) |
| `lambda.mail.pop3` | A POP3 account as a download source (ML9): `sync_inbox(session, root, progress)` downloads each message the server lists by UIDL that the local Inbox lacks, once, and leaves mail on the server; `mark_seen` keeps read state in the store; `listing`, `missing`, `message_id`, `octets` |
| `lambda.mail.compose` | New messages, replies and forwards (§4.6): `reply(message, me, all)` and `forward(message)` give a draft with the quote, `Re:`/`Fwd:` subject, recipients (Reply All without your address or duplicates), `In-Reply-To` and `References`; `quote`, `quote_runs`, `html_of_text` (nested `<blockquote type="cite">`), `recipients` (typed address lists), `problem`, `rfc5322_date`, and `eml(draft, from, date, id)`, the `multipart/alternative` RFC 5322 text through `lambda.doc.misc` |
| `lambda.mail.sanitize` | `clean(html, {remote, cids})` → `{html, blocked}`: a standalone document for a message frame, with scripts, `on*` attributes, embedded documents, forms and frame navigation removed, remote loads dropped unless `remote` (`blocked` counts them), `cid:` URLs inlined, and `<noscript>` content parsed and cleaned; `cid_urls`, `data_url`, `clean_css` |
| `lambda.mail.providers` | `PRESETS` (Gmail, iCloud, Fastmail, Proton Mail Bridge, Outlook.com, Yahoo, with POP3 servers where the host offers them), `preset_for(address)`, `imap_url`, `pop3_url`, `smtp_url`, `session_options` and `tls_option` for `lambda.io.mail`, and `autoconfig(address)` / `from_autoconfig(doc, address)` for Mozilla's ISPDB |

An index entry is `{uid, file, flags, date, internaldate, from, to, subject, message_id, size, thread, has_file}`. List rows come from each message's header block, fetched with `BODY.PEEK[HEADER.FIELDS (…)]` and decoded by the `eml` reader, so subjects and names arrive decoded.

The sanitizer is the second layer of ML5v2. The first is the frame itself: a `sandbox` frame loads with scripting disabled in Radiant. Blocking is best-effort for CSS written with escapes or `image-set()` strings, which is why the engine-level policy stays the goal (MQ14).

## 25. oauth — Sign-in for native apps

`lambda.oauth` signs a person in with an OAuth 2.0 provider the way native apps do (RFC 8252), with PKCE (RFC 7636). The system browser shows the provider's own sign-in page; a [`lambda.io.loopback`](Lambda_Sys_Func.md#lambdaioloopback) listener on 127.0.0.1 receives the redirect; and the code is exchanged for tokens, so the app never sees the person's password. The mail demo signs in to Gmail and Outlook.com this way (design: [Lambda_Demo_Mail.md](../vibe/Lambda_Demo_Mail.md) M4).

```lambda no-run
import oauth: lambda.oauth

pn main() {
    let google = {authorize_url: "https://accounts.google.com/o/oauth2/v2/auth",
        token_url: "https://oauth2.googleapis.com/token", scope: "https://mail.google.com/ openid email",
        userinfo_url: "https://openidconnect.googleapis.com/v1/userinfo",
        params: {access_type: "offline", prompt: "consent"}}
    let client = {id: "….apps.googleusercontent.com", secret: "…"}
    let tokens = oauth.sign_in(google, client, {})^
    // who signed in: {email, name, …}
    let me = oauth.userinfo(google, tokens.access_token)^
    // later: a fresh access token without the browser
    let renewed = oauth.refresh(google, client, tokens.refresh_token)^
}
```

| Function | Contents |
|---|---|
| `sign_in(provider, client, options)` | Opens the provider's page in the system browser and returns `{access_token, refresh_token, expires_in, scope}`. Options: `login_hint`, `seconds` (the wait for the person, default 300), and `open`, a `pn` that shows the URL instead of the system browser. A refusal, a mismatched `state` or a failed exchange raises with the provider's reason |
| `userinfo(provider, access_token)` | Who signed in, from the provider's OpenID Connect userinfo endpoint (`provider.userinfo_url`): `{email, name, …}` as the provider sends it. The token's scope must include `openid` and `email` |
| `refresh(provider, client, refresh_token)` | New tokens. Google omits `refresh_token` and keeps the old one valid; Microsoft rotates it |
| `open_browser(url)` | Opens a URL in the system browser: `open` on macOS, `start` on Windows, `xdg-open` elsewhere |
| `code_challenge`, `base64url`, `form`, `authorize_url`, `tokens_of`, `query_value` | The pure parts: the S256 challenge, URL and body encoding, and token replies |

A provider is `{authorize_url, token_url, scope, params}`, plus `userinfo_url` for `userinfo`, where `params` are extra authorization parameters. A client is `{id, secret}`, the app's registration with the provider. Google requires a secret even for desktop clients, while a Microsoft public client has none; a null secret is not sent. The verifier and `state` come from `uuid()`, and the challenge from `hash`. Keep refresh tokens in [`lambda.io.secret`](Lambda_Sys_Func.md#lambdaiosecret).

## 26. procart — Procedural art

`lambda.procart` builds content from a seed instead of by hand: noise for terrain and textures, scalar fields, and the random choices behind them. Every function is a pure `fn` (S12.1.1v3), so the same seed always gives the same result. Exact-tier functions give bit-identical results on every platform and execution tier, because they use only integer operations and correctly rounded arithmetic. The tolerance-tier exceptions are `rng.normal`, `rng.exponential`, free-angle L-system turtles and Lloyd relaxation (design: [Lambda_Pkg_Proc_Art.md](../vibe/Lambda_Pkg_Proc_Art.md) PG2). The modules below retain Lambda references; native batch kernels accelerate WFC, Delaunay and hydraulic erosion. The showcase `./lambda.exe demo procart` builds a whole world from one seed, with Voronoi districts and an optional latitude/altitude climate.

The package was renamed to `lambda.procart`; update former `lambda.procgen`
imports to this path. Its facade and module names retain their existing APIs.
The [Meadow demo](../test/demo/procart/meadow.ls) loads a separate Mark document
with semantic `group`, `flower`, `tree`, `stone` and `grass` objects. Its
[stored-scene schema](../vibe/Lambda_Pkg_Proc_Art.md#pg11--stored-procedural-art)
is implemented in the demo; the resolver and botanical generators are not yet
package exports.

**Drawing grammar (P7).** `lambda.procart.scene` adds `normalize(document,
registry={})`, `expand(document,registry={})` and
`to_scene3d(model,presentation={})`. Version-2 `<procart>` documents contain
resources, groups, primitives, rule calls and semantic objects. Supported
primitives are `box`, `grid`, `sphere`, `line`, `point`, `triangle`, `mesh`,
`cylinder` and `tube`. Common `scale`, `position`, degree-based `rotate`, `hue`,
color and opacity adjustments inherit through groups and calls. Existing
`rotation` remains radians and is exclusive with `rotate`.

```lambda
import art: lambda.procart.scene
let design = <procart version:2,seed:2026,settings:{max_depth:16,max_objects:1000},
    <resources>;
    <repeat id:"ribbon",count:12,step:{position:[0.5,0.1,0.0],rotate:12,scale:0.95,hue:10},color:"#cc744f",
        <box scale:[0.9,0.1]>>>
let model = art.expand(design)^;
art.to_scene3d(model)
```

`expand` returns `{resources,parts,stats,seed,settings}`. Each part retains its
source identity, geometry reference, matrix, final color and opacity. The
adapter batches instances; presentation accepts camera options, lights,
background, viewport dimensions and mesh options. Root `settings` controls
`max_depth`, `min_size`, `max_size`, `max_objects`, `max_vertices`, `max_steps`, `max_surface_pixels`
and per-kind primitive defaults. Depth/size limits retire branches and report
statistics; work-budget overflow raises an error.

Semantic recipes require a caller-supplied registry keyed by
`generator@version`, with `{validate,generate}` functions. The
[Form studies demo](../test/demo/procart/grammar.ls) provides adapters using the
existing botanical/mineral generators. The standalone envelope is
`lmd/package/procart/schema.ls`; runtime admission also checks attributes,
references, versions, defaults and budgets (**S11.4.6**). The complete contract
is [PG12](../vibe/Lambda_Design_Procart.md#9-p7--composable-drawing-grammar).

Recursive rules declare a `name`, and invocation uses that name as the tag:

```lambda
import art: lambda.procart.scene
let design = <procart version:2,seed:2026,settings:{max_depth:24},
    <resources
        <rule name:"R1",
            <variant name:"continue",
                <R1 id:"next",position:[0.9,0,0],rotate:[0,6,6],scale:0.99,saturation:0.99>;
                <sphere scale:2>>>>;
    <R1 id:"start",color:"#77a955">>;
art.to_scene3d(art.expand(design)^)
```

Each rule invocation chooses one named variant using its weight (default 1)
and seeded key. Variant names determine stable ordering and random identity.
Optional drawing-node `id` attributes identify instances; recipes and geometry
prototypes retain required reference IDs. Rule names are case-sensitive and
cannot shadow built-in tags. The structural schema checks the fixed envelopes;
the expander checks document-defined drawing tags and their attributes,
references and descendants (**S11.4.6**). Naming contract: [PG12.11](../vibe/Lambda_Design_Procart.md#911-definition-names-and-direct-invocation-tags-pg1211).

**Plant architecture (P8).** `lambda.procart.plant` separates growth from mesh
detail. `skeleton(key,parameters={})` returns branches with parent IDs, levels,
centerlines and radii, plus oriented leaf attachments. Per-level parameters
control length, radius, child count, attachment start, angle, taper, sections,
gnarliness, spiral twist and child-length falloff. `leaves` controls count per
terminal branch, dimensions, placement, angle, size variance, broad/needle/maple shape
and palette; `force:{direction,strength}` bends growth toward a fixed direction
in specimen coordinates.

```lambda
import plant: lambda.procart.plant
import rng: lambda.procart.rng
import art: lambda.procart.scene
let architecture = plant.skeleton(rng.key(2026), {
    levels:[{length:3.5,radius:0.18,children:5},
            {length:1.2,radius:0.05,children:0,angle:55}],
    leaves:{count:18}, force:{direction:[0,1,0],strength:0.1}})^
let models = plant.lods(architecture, [
    {segments:10},
    {segments:5,section_stride:3,leaf_stride:3,leaf_scale:1.45}])^;
art.to_scene3d(models[0], {position:[8,6,10],target:[0,2,0]})
```

`mesh(skeleton,detail={})` and `lods(skeleton,details)` return ordinary Procart
models with `resources`, `parts`, `stats`, `skeleton` and `detail`. Lower detail
preserves branch endpoints and selects a stable leaf subset; it never reruns
growth. `generate(key,parameters={})` combines skeleton creation and default
meshing. Automatic camera-distance switching is left to the application.

`architecture(document)` reads a standalone literal `<tree version:1,
generator:"plant.tree",generator_version:1,seed:2026,parameters:{...}>` with no
children; `from_document(document,detail={})` also meshes it. The structural
schema is `lmd/package/procart/plant_schema.ls`; runtime admission checks the
generator version and parameter ranges (**S11.4.6**). `plant.registry()` supplies
`plant.tree@1` and `plant.apex@1` handlers for semantic tree recipes in P7 scenes.
Standalone specimens derive their key directly from the seed, while composed
instances follow the scene hierarchy. All four saved presets are in the
[Tree studio](../test/demo/procart/trees.ls).

**Reusable surfaces (P9).** P7 documents may store `<pattern>` resources and bind
them to whole objects with `surface`, or generated roles with `surfaces`:

```lambda
import art: lambda.procart.scene
import plant: lambda.procart.plant
let design=<procart version:2,seed:2026,
 <resources
  <pattern id:"bark-grain",generator:"bark",version:1>
  <pattern id:"veins",generator:"leaf-veins",version:1,
      parameters:{colors:["#e9ecb9","#719653"],frequency:7,width:0.025}>
  <recipe id:"oak",kind:"tree",generator:"plant.tree",version:1,parameters:{}>>;
 <tree recipe:"oak",surfaces:{
     bark:{pattern:"bark-grain",mapping:"branch",roughness:0.95},
     leaf:{pattern:"veins",mapping:"leaf",resolution:128}}>>;
art.to_scene3d(art.expand(design,plant.registry())^)
```

Version-1 generators are `noise`, `stripes`, `marble`, `bark` and `leaf-veins`.
Parameters include two opaque `colors`, `frequency`, `warp` and `width`.
Optional pattern `seed` pins appearance independently of the scene seed.
Bindings accept `pattern`, `mapping`, coordinate `scale`/`offset`, texture
`resolution` (8–256), and scalar `roughness`/`metalness`. `surface:"id"` is a
shorthand. Bindings inherit through groups/rules; role bindings take precedence.
`surface:null` clears the whole-object fallback and `surfaces:{leaf:null}` clears
that role including the fallback. Admission checks types, references and budgets
(**S11.4.6**); explicit-null overrides use name membership (**S8.1.1v2**).

`leaf` coordinates cross the blade and run base-to-tip; `branch` coordinates wrap
the bark and follow original skeleton distance, preserving retained rings across
detail settings. `uv` uses existing UVs; `object` samples local buffer positions
and bakes an atlas. Missing coordinates or incompatible organ roles raise errors.
Nonidentity UV scale/offset requires explicit buffer UVs. Object mapping also
requires an explicit buffer, with at most 4,096 triangles. Pattern RGB multiplies
the object's final material tint; opacity stays independent.

Unique jobs share baked PNG resources and consume `max_surface_pixels` (default
262,144; host cap 1,048,576), plus `max_vertices` for mapped geometry copies.
Stats add `surface_pixels` and `surface_textures`. Native rendering uses existing
Scene3D standard materials; color patterns and scalar material properties are
supported. Normal maps, displacement and attached moss/thorn geometry are later
work. The saved [surface study](../test/demo/procart/samples/surfaces.mark) shows
stones, bark and leaves; [PG14](../vibe/Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14)
defines the complete schema and coordinate contracts.

`lsystem.derive(axiom,rules,order,options={})` adds contextual parametric
productions: a rule callback receives `(module,{left,right,index,generation,key})`.
Neighbours come from the previous generation, including bracket tokens;
replacements are simultaneous. `options` accepts `seed`, a raw `key`, and
`max_symbols` (at most 100,000). Existing `lsystem.expand` and the 2D turtle
retain their contracts. The new spatial turtle starts along +Y:

```lambda
import lsys: lambda.procart.lsystem
import turtle: lambda.procart.turtle3d
import plant: lambda.procart.plant
import art: lambda.procart.scene
let modules = lsys.derive([["Bud",1.2]], {
    Bud:(m,context)=>[["F",m[1],0.03],"[",["&",35],"L",
                     ["Bud",m[1]*0.7],"]",["/",137.5],"L"]}, 4)^
let architecture = turtle.skeleton(modules,{radius:0.08})^;
art.to_scene3d(plant.mesh(architecture)^)
```

`turtle3d.skeleton` accepts `F(length,tip_radius)`, `f(length)`, yaw `+/-`,
pitch `&/^`, roll `/` and backslash, `|`, radius `!`, leaf `L(length,width)`
and bracketed state. Commands are strings or arrays such as `["F",1.2,0.03]`;
angles use degrees. Options set the initial position, step, angle, radius,
colors, leaf dimensions and force. Unsupported developmental symbols draw
nothing. `apex.skeleton(key,parameters={})` supplies a bounded, versioned bud
model with numeric generation/decay/phyllotaxis controls. It uses the same
turtle and mesher. Contracts and limits: [PG13](../vibe/Lambda_Design_Procart.md#10-p8--plant-architecture-and-developmental-models-pg13).

```lambda
import rng: lambda.procart.rng
import noise: lambda.procart.noise
import field: lambda.procart.field
import adapt: lambda.procart.adapt

let world = rng.key(2026)
let gen = noise.make(rng.derive(world, "height"), {kind: 'simplex', octaves: 5, frequency: 1.0 / 40.0})^
// a falloff mask turns the noise into an island
let land = field.normalize(noise.field(gen, 96, 64)) * 1.2 - field.radial(96, 64) * 0.7
let picture = adapt.image(field.normalize(land), adapt.terrain())^
rng.pick(rng.derive(world, "name"), ["Ardent", "Brume", "Cinder"])
```

**Environment and responsive plants (P10 / PG15).** One optional global
`<environment>` child supplies latitude (degrees north), altitude (metres), day
of year (1..365), local solar `time` (0..24), `humidity` (0..1), weather
(`sunny`, `cloudy`, `rainy`, `foggy`, `windy`), `wind_speed` (m/s),
`wind_direction` (degrees clockwise from north, blowing toward that bearing),
and `lighting:{sun,sky,ambient}` multipliers. The same element works in the
Meadow demo. Conditions are stored data and pure samples (**S12.1.1v3**).

```lambda
import art: lambda.procart.scene
import herbs: lambda.procart.herb
let source=<procart version:2,seed:2026,
    <resources <recipe id:"gold",kind:"flower",generator:"plant.herb",version:1,
        parameters:{species:"sunflower",height:1.7}>>;
    <environment latitude:45.0,altitude:120.0,day:172,time:9.0,weather:"windy">;
    <flower recipe:"gold">>;
art.to_scene3d(art.expand(source,herbs.registry())^)
```

`environment.normalize` admits an attribute map or element, `description`
creates a saved element, `from_document` reads it, and `sample` resolves solar
coordinates and weather. `lights` lowers the sampled lighting; `response`
returns species opening, head direction, humidity droop and flexibility.
`wind` gives a deterministic rooted quaternion at a time in seconds;
`wind_track` emits an eight-second native track using that sampler.

`herb.generate(key,parameters,environment_sample=null,frame=identity)` returns
shared resources and organ parts. `plant.herb@1` supports sunflower, water-lily,
daisy, poppy and cosmos through `<flower>`, and reed through `<grass>`.
Parameters: `species`, `height`, `size`, `petals`, `petal`, `center`, `leaf`,
`stem`. Sunflower heads artistically track the sun; day-flowering lilies close
at night and partly in rain. Seeded stature and placement remain stable when
conditions change. Registered generators receive `environment` and `frame`.

With nonzero wind, `to_scene3d` emits rooted plant groups and existing native
animation clips. `motion:false` in presentation stops autoplay. Explicit
presentation lights/background override the environment. Drawing primitives
and stones remain static. The [expanded Meadow](../test/demo/procart/meadow.ls)
adds a pond, six flower varieties and weather controls that preserve its camera.
This is an illustrative weather/plant model. Units, defaults, limits and scope:
[PG15](../vibe/Lambda_Design_Procart.md#12-p10--environment-and-a-living-meadow-pg15).

**Keyed randomness.** A random value is a hash of a `u64` key, and keys are derived by name, so nothing threads seed state through the code. Results do not depend on evaluation order, and adding a draw does not shift the others.

| Function | Result |
|---|---|
| `rng.key(seed)` | A root key from an int or string seed |
| `rng.derive(k, path)` | A child key from one part or an array of parts (`"trees"`, `["chunk", 12, -3]`); `rng.at(k, i)` is the int case |
| `rng.unit(k)`, `rng.between(k, lo, hi)`, `rng.below(k, n)`, `rng.chance(k, p)` | A float in [0, 1), a float in [lo, hi), an int in [0, n), a bool |
| `rng.pick`, `rng.weighted`, `rng.shuffle`, `rng.sample`, `rng.perm`, `rng.units` | Choices and permutations |
| `rng.stream(k)` | `[value, next]`, the same sequence as `math.random` |
| `rng.normal(k, mean?, deviation?)`, `rng.exponential(k, rate?)` | Deviates that use `log`/`cos`: deterministic, but cross-platform only to libm accuracy |

**Noise.** `noise.make(key, spec)` prepares a generator, and `noise.sample(gen, x, y)`, `noise.sample3(gen, x, y, z)` and `noise.field(gen, width, height)` evaluate it. Spec fields: `kind` (`'simplex'` default, `'perlin'`, `'value'`, `'white'`, `'cellular'`), `frequency` (default 1/32), `fractal` (`'none'`, `'fbm'`, `'ridged'`, `'billow'`; giving `octaves` defaults it to `'fbm'`), `octaves` (1–16), `lacunarity` (2.0), `gain` (0.5), and for cellular noise `cell` (`'f1'`, `'f2'`, `'f2-f1'`), `metric` (`'euclidean'`, `'manhattan'`, `'chebyshev'`) and `jitter` (1.0). `warp: {amplitude, frequency}` offsets each sample point by simplex noise first. An unknown kind or fractal, or bad octaves, raises an error. `period: [px, py]` (or `[px, py, pz]` for 3D) makes the complete generator tile at those positive world-coordinate lengths, including warp and fractals. Periodic sampling blends four translated samples in 2D or eight in 3D with a quintic weight. The point kernels (`noise.value2`, `perlin2`, `simplex2`, `cellular2`, `white2` and their `3` forms) take a table from `noise.table(key)`.

**Sampling** returns `[x, y]` points in a `{width, height}` domain. `sample.poisson(key, {width, height, radius, attempts?})` keeps every pair at least `radius` apart and fills the domain nearly maximally. Optional `density` is a field in [0,1], bilinearly sampled over the domain: zero forbids points, otherwise local radius is `min(max_radius, radius / sqrt(density))`. Each pair respects the larger local radius. `max_radius` defaults to four times `radius` and may be at most sixteen times it. `sample.jitter(key, {width, height, cell, jitter?})` places one point per cell. `sample.halton(key, n, domain)` and `sample.r2(key, n, domain)` are low-discrepancy sequences shifted by the key, and `sample.radical_inverse(i, base)` is the underlying digit reversal. `sample.best_candidate(key, {width, height, count, candidates?})` is Mitchell's blue noise, and `sample.weighted(key, field, n)` draws points with density proportional to a field.

**Geometry.** `geom.delaunay(points)` returns `{points, triangles, halfedges, hull}` in Delaunator's layout: counterclockwise index triples, edge `3t + s` paired with its opposite or `-1`, and the counterclockwise hull. Exact predicates make collinear, cocircular and repeated points safe; a repeated point keeps its first index. It accepts at most 20,000 points. A native local-cavity kernel preserves the exact ordering of the quadratic Lambda reference, which remains available as `geom.delaunay_reference(points)`. `geom.neighbors(mesh)`, `geom.next_edge(e)` and `geom.prev_edge(e)` navigate it. `geom.voronoi(mesh, {bounds?: [x0, y0, x1, y1]})` gives each point's cell clipped to the bounds (default: the points' bounding box). `geom.hull(points)` is the convex hull without collinear points, and `geom.lloyd(points, {iterations?, bounds?})` relaxes points toward their cells' centroids.

```lambda
import rng: lambda.procart.rng
import sample: lambda.procart.sample
import geom: lambda.procart.geom

let sites = sample.poisson(rng.key(1), {width: 200.0, height: 120.0, radius: 15.0})^
let mesh = geom.delaunay(sites)^
let cells = geom.voronoi(mesh, {bounds: [0.0, 0.0, 200.0, 120.0]})^;
[len(sites), len(mesh.triangles), len(cells)]
```

**Tile grids** are `int[][]` rows, `tiles[y][x]`, with 0 for floor and 1 for wall.

| Function | Result |
|---|---|
| `grid.automaton(key, {width, height, fill?, rule?, steps?, edge?})` | A cave: `grid.random_fill` (each cell a wall with probability `fill`, default 0.45), then `grid.evolve` with a life-like `rule` (default `"B5678/S45678"`) for `steps` generations (default 5). Cells beyond the edge count as `edge` (default 1) |
| `grid.walk(key, {width, height, fill?, steps?, start?})` | A drunkard's walk carving `fill` (default 0.4) of the interior; the border stays wall and the floor is connected |
| `grid.bsp(key, {width, height, min_size?, max_depth?})` | Binary space partition leaves `[x, y, w, h]` that tile the rectangle |
| `grid.dungeon(key, {width, height, min_size?, max_depth?, margin?})` | `{tiles, rooms, corridors}`: a room in each BSP leaf, joined by L-shaped corridors so every room is reachable |
| `grid.maze(key, {width, height, method?})`, `grid.maze_edges(...)` | A perfect maze over `width` × `height` cells, as (2 × width + 1) × (2 × height + 1) tiles or as spanning-tree edges between cells `y × width + x`. `method` is `'backtracker'` (default), `'kruskal'`, `'prim'` or `'wilson'` (uniform over all mazes). At most 10,000 cells |
| `grid.regions(tiles, floor?)`, `grid.keep_largest(tiles, floor?, wall?)` | 4-connected regions: `{labels, sizes}` with `-1` for non-floor; keep only the largest region |
| `grid.distances(tiles, sources, floor?)` | Steps from the nearest source `[x, y]`, `-1` where unreachable |

```lambda
import rng: lambda.procart.rng
import grid: lambda.procart.grid

let cave = grid.keep_largest(grid.automaton(rng.key(5), {width: 60, height: 30})^)
let maze = grid.maze(rng.key(5), {width: 12, height: 8, method: 'wilson'})^;
[for (row in maze) join([for (v in row) if (v == 1) "#" else " "], "")]
```

**L-systems.** `lsystem.expand(axiom, rules, order, key?)` rewrites a string, or an array of modules (`"F"` or `["F", 2.0]`). A rule is a replacement; an array of `[weight, body]` pairs (stochastic, which needs the key); or a function of the module (parametric). Expansion stops with an error past 100,000 symbols. `lsystem.turtle(sequence, {step?, angle?, heading?, start?, draw?, move?, reverse?, width?, width_decay?})` returns `{segments, end}`. Each segment is `[x0, y0, x1, y1, depth]`, with `[` and `]` saving and restoring the turtle, `F`/`G` drawing, `f` moving, `+`/`-` turning by `angle` degrees and `|` turning around. With `width`, a sixth segment value gives `width` times `width_decay` (default 1) for every bracket depth. Omitting it preserves five-value segments. A module's parameter overrides the step or angle. `lsystem.polylines(segments)` joins connected segments. TikZ's `lindenmayersystems` uses this module.

**Text.** `text.expand(grammar, key, rule?)` expands a Tracery-style grammar from `"#origin#"`: `#symbol#` picks one of the symbol's rules, and modifiers `.capitalize`, `.capitalizeAll`, `.s`, `.a`, `.ed`, `.inQuotes` and `.comma` follow Tracery's. `[name:a,b]` pushes options, `[name:POP]` pops them, and a backslash escapes. Unknown symbols and recursion deeper than 64 raise errors. `text.markov(samples, order?)` builds a character model, and `text.name(key, model, {min?, max?, novel?})` draws a new name from it.

```lambda
import rng: lambda.procart.rng
import lsystem: lambda.procart.lsystem
import text: lambda.procart.text

let plant = lsystem.expand("X", {X: "F[+X][-X]FX", F: "FF"}, 4)^
let shape = lsystem.turtle(plant, {angle: 25.0, heading: 90.0})^
let story = text.expand({origin: "#hero.capitalize# found #thing.a#.", hero: ["ada", "ming"], thing: ["owl", "ember"]}, rng.key(3))^;
[len(shape.segments), story]
```

**Wave function collapse** fills a grid so that every pair of neighbours is allowed by a model:
- `wfc.tiled(tiles, adjacency, weights?)` takes rules `[a, b, "x"]` (b may sit right of a) and `[a, b, "y"]` (b below a).
- `wfc.tiled_from_sample(sample)` learns tiles, weights and adjacency from an example grid.
- `wfc.overlapping(sample, {n?, symmetry?, periodic_input?})` makes Gumin's overlapping model: every n × n window (default 3), with up to 8 rotations and reflections.

`wfc.run(key, model, {width, height, periodic?, retries?})` returns a grid of pattern indices, retrying after a contradiction (default 10 attempts) and then raising. `wfc.render(model, grid)` turns the indices into sample values, and `wfc.generate` does both. The solver collapses the smallest domain first, so lopsided tile sets tend to flood from their first pick. It is capped at 10,000 cells. A native counting kernel accelerates positive `int`/`float` weights with prefixes below 2^53; other weight domains and runtimes without the kernel use the retained Lambda solver. `wfc.run_reference(key, model, options)` explicitly selects that oracle. Both paths preserve the same keyed choices and exact grids (PG7.3); the 64 × 64 overlapping benchmark runs in 0.282 s on a release AUTO build.

```lambda
import rng: lambda.procart.rng
import wfc: lambda.procart.wfc

let model = wfc.tiled_from_sample([[0, 0, 1, 2], [0, 0, 1, 2], [1, 1, 1, 1]])^
let tiles = wfc.generate(rng.key(4), model, {width: 16, height: 8})^;
[for (row in tiles) join([for (v in row) string(v)], "")]
```

**Terrain** works on height fields in [0, 1]:
- `terrain.heightmap(key, {width, height, island?, octaves?, frequency?, warp?})` is warped fBm noise, blended toward an island dome by `island` (0 to 1).
- `terrain.thermal(height, {iterations?, talus?, rate?})` slides material off slopes steeper than `talus` and conserves mass.
- `terrain.hydraulic(key, height, {droplets?, batch?, ...})` runs Beyer-style droplets in batches. A batch never pushes a cell outside its neighbourhood's range, so heights stay bounded. The native batch kernel matches `terrain.hydraulic_reference(key, height, options)` exactly.
- `terrain.fill(height, {sea_level?, epsilon?})` raises pits until every cell drains to the sea or the map edge. `terrain.accumulation(height)` counts the cells draining through each cell (D8).
- `terrain.rivers(height, {sea_level?, threshold?})` returns `{accumulation, filled, segments, polylines}`, where each segment is `[x0, y0, x1, y1, flow]`; `polylines` joins segments into arrays of `[x,y]` points.
- `terrain.moisture(key, water)` mixes noise with nearness to water. `terrain.temperature(height, {latitude?, latitude_span?, lapse_rate?, sea_level?})` returns values in [0,1]: warmth falls with absolute latitude and altitude. Latitude is normalized from −1 to 1 (poles), centered at 0 by default with a span of 2; lapse rate defaults to 0.6.
- `terrain.biomes(height, moisture, {sea_level?, temperature?})^` names each cell from mapgen2's table, using the optional matching [0,1] temperature field as its climate axis. Without temperature it keeps the elevation table. `terrain.biome_colors()` gives the colours.
- `terrain.world(key, {width, height, ...})` runs the whole recipe and returns `{height, rivers, river_polylines, moisture, temperature, biomes}`. Set `climate: true` to generate temperature and use climate biomes; otherwise `temperature` is null.

```lambda
import rng: lambda.procart.rng
import terrain: lambda.procart.terrain

let world = terrain.world(rng.key(6), {width: 48, height: 32})^;
[len(world.rivers), world.biomes[16][24]]
```

**Fields** are `float[][]` rows, `f[y][x]`, the shape of chart grid data, so ordinary arithmetic combines them (`0.6 * a + 0.4 * b`). `field` adds `make`, `transform`, `width`, `height`, `bounds`, `normalize`, `remap`, `clamp`, `terrace`, `threshold`, `radial`, bilinear `sample` and box `blur`. Additional field operations return errors for malformed or nonfinite inputs:

| Function | Result |
|---|---|
| `field.curve(f, [[x0,y0], [x1,y1], ...])` | Piecewise-linear shaping with increasing input stops, clamped at the endpoints |
| `field.gradient(f, [dx,dy]?)`, `field.slope(f, [dx,dy]?)` | `{dx,dy}` derivative fields or gradient magnitude; positive spacing defaults to `[1,1]`, with one-sided edges and zero on singleton axes |
| `field.distance_transform(mask)` | Exact Euclidean distance to the nearest nonzero/true cell; `inf` if absent. At most 4,096 cells on either axis and one million total |
| `field.contours(f, levels, options?)` | Chart isolines `{level, points, closed}` using shared mesh topology; field at least 2 × 2 |

**Adapters** hand fields to other packages: `adapt.pixels` and `adapt.image` (an `lambda.image` raster, through a colormap such as `adapt.gray()` or `adapt.terrain()`) and `adapt.chart_grid` (`lambda.chart` grid data). `adapt.heightfield(f, {dx?, dz?, height_scale?})^` returns flat `positions`, `indices` and `uvs` for `lambda.scene3d.geometry`, with +Y up; it needs at least 2 × 2 cells. `adapt.svg(polylines, {view_box?, width?, height?, stroke?, stroke_width?})^` builds a pure SVG element.

---

**Animal specimens (P11 / PG16).** `lambda.procart.animal` builds authored
butterfly, bird, frog, fish, bee and rabbit families. `normalize(document)^` and
`generate(document)^` accept a stand-alone semantic tag with `version:1`,
`generator_version:1`, a seed and parameter map. Output is ordinary geometry,
parts and hinge descriptions; `nodes(model,phase?,motion?,prefix?)` lowers it to
Scene3D children. `pose_nodes(model,{state,speed},phase?,motion?,prefix?)^`
selects admitted resting, walking/hopping, grazing, flight/perching, swimming or
calling presentation; it reuses geometry/materials and emits native hinge/pose
tracks. It does not implement movement decisions or consume resources.
Size is an authored metre scale; body/head/appendage/tail
proportions are ratios. Shape/palette/pattern controls are not measured physiology.

`procart.surface` adds `spots`, `feathers` and `scales` to its existing seeded
sampler/binding/bake contract. `procart.butterfly_atlas` supplies the preserved
16-cell demo atlas and customizable `wing_svg(options)^`, sharing contour, vein,
eye and border helpers. `primitive.blade` supplies curved petal/feather/fin buffers.
`herb.generate` adds an optional fifth `pose:{openness?}` argument (0–1) without
changing the four existing arguments. Pure descriptions follow **S12.1.1v3**.
See [PG16](../vibe/Lambda_Design_Procart.md#13-p11--procedural-animal-specimens-and-markings-pg16)
for admission and supported profiles. Eden owns model editing/saving and biology;
P7's general drawing vocabulary is unchanged by this stand-alone profile.

**Watercolor.** `lambda.procart.watercolor` ports the simulation core of the
[Washes](https://github.com/castavridis/washes-js) engine (MIT; notice in
`lmd/package/procart/WASHES_LICENSE.txt`), an implementation of Curtis et al.,
*Computer-Generated Watercolor* (SIGGRAPH 1997). Unlike the rest of the package it
is mostly procedural: a sheet changes in place every time step, so `stamp`,
`wet_sheet`, `dry`, `step`, `render` and `swatches` are `pn`s that take the sheet as
a `var` argument (**S9.1.3v4**). Pigments, the paper and stroke trails are pure
(**S12.1.1v3**).

```lambda
import wc: lambda.procart.watercolor
let Sheet = wc.Sheet
pn main() {
    var sheet: Sheet = wc.sheet(240, 160, 2026, 2.5, 0.62)^   // width, height, paper seed, px per cell, wetness
    for (p in wc.trail(40.0, 80.0, 200.0, 80.0, 12.0)^) {
        wc.stamp(sheet, {kind:"pigment",channel:2,x:p[0],y:p[1],radius:12.0,strength:0.4})^
    }
    var k: int = 0
    while (k < 300) { wc.step(sheet, wc.adsorption(wc.CLASSIC))^; k = k + 1 }
    var rgba: float[] = wc.canvas(sheet)^
    wc.render(rgba, sheet, wc.optics(wc.CLASSIC))^
    save(wc.pixels(sheet, rgba), "wash.png")^
}
```

A sheet is a record with one `float[]` of 21 per-cell planes; `wc.PAPER`, `WET`,
`PRESSURE`, `U`, `V`, `SUSPENDED` (+0..2) and `DEPOSITED` (+0..2) name them and
`plane(sheet, index)^` views one. `sheet(width, height, seed, scale=2.5, wetness=0)^`
scales Washes' constants from 2 display pixels per cell and admits up to 250,000
cells; `blank(sheet, wetness)^` starts again on the same paper. A brush is
`{kind:"pigment"|"water"|"lift", x, y, radius, strength, channel}` in cells;
`stamp(sheet, brush, paint_load=1, water_load=1)`. `step(sheet, adsorption,
settings)` takes `adsorption(pigments)` and `{evaporation, drying, edges, tilt}`
(defaults `wc.SETTINGS`). `dry(sheet, level=0)^` removes water above `level`: 0
settles every suspended pigment, a level under the 0.04 flow threshold leaves the
sheet damp. `render(rgba, sheet, optics(pigments), full=true, water=false)`
composites into an RGBA `float[]`; `pixels(sheet, rgba)` gives `(H, W, 4)` ubyte
pixels for `dom.set_canvas_pixels` or `save`. Palettes: `CLASSIC` (Curtis'
Quinacridone Rose, Hansa Yellow and Cerulean Blue), `GOUACHE` and `EARTH`;
`pigment(name, rgb, density, staining, granulation)` converts an sRGB colour to
K and S. Semi-Lagrangian advection conserves pigment only approximately, as in
Washes. The [Watercolor demo](../test/demo/procart/watercolor.ls) paints live.

**Articulated avatars (P12 / PG17).** `lambda.procart.avatar` accepts a versioned
`<avatar>` recipe for plush animals, robots, humans, fairies or genies;
`generator_version:3` selects a chibi style (large head, glossy eyes, blush,
mitten hands) on the same rig, and `upgrade(recipe,3)^` moves older recipes to it.
`normalize(recipe)^` validates proportions/colours, `generate(recipe)^` builds
stable parts and nested joints, `resources()` supplies shared geometry, and
`nodes(model,action="idle",playing=true,prefix="avatar",phase=0,pace=1)^`
emits idle/walk/run/play/wave/sit poses and native animation clips. Supply
resources once and a unique prefix per character. Height is the standing
envelope in metres; other shape controls are authored ratios. Eden owns named
presets, editing, saved casts and garden tours. These are articulated rigid
parts; generation is pure (**S12.1.1v3**), without biological effects.
See [PG17](../vibe/Lambda_Design_Procart.md#14-p12--articulated-character-recipes-pg17).

Reusable native-audio helpers follow **D9.5.6–D11.3.1**:
`lambda.audio.pcm.wav(samples,rate=24000)^` is a procedural mono 16-bit WAV encoder
(8–96 kHz, clipped samples, original Soundflow quantization).
`lambda.audio.voices.start(owner,voices,bytes,gain=1,limit=14)` reaps ended handles,
bounds overlap to 1–32 voices and returns `{voices,failed,opened,closed}`;
`close(owner,voices)` releases them. Applications own cue scheduling, caches and
mute/pause lifetime. Soundflow and Eden share these helpers rather than copying
encoders or voice loops. No spatial-panning or field-recording claim is implied.


---

## 27. scene3d — Native 3D scenes

`lambda.scene3d` builds ordinary Lambda elements for Radiant's native
`<scene3d>` viewport. Scene construction and validation are pure
(**S12.1.1v3**); the source package lives under **D9.3.4**, while Radiant
owns rendering, GPU resources and the published image (**D5.5.1 /
D5.5.2**). The qualified graphics profile is native macOS OpenGL. A headless
build without graphics reports graphics unavailable; pure validation still
works. See the [scene design](../vibe/radiant/Radiant_Design_WebGL.md#4-lambda-package-and-scene-elements)
for the complete vocabulary and embedding rules.

The [outdoor profile](Lambda_Scene3d_Outdoor.md) adds height/distance fog,
thin-leaf lighting, shadow-consistent instanced wind, bounded directional cascades
and physical water helpers. Eden retains terrain, water volumes and ecological
stepping; native motion only changes presentation.

### Construction and validation

Save this as a script and open it with `./lambda.exe view path/to/scene.ls`:

```lambda
import s: lambda.scene3d

let scene = s.scene([
    s.camera({id: "camera", position: [3.0, 2.5, 4.0], target: [0.0, 0.5, 0.0]}),
    s.light('directional', {position: [3.0, 5.0, 2.0], target: [0.0, 0.0, 0.0],
        intensity: 0.8, 'cast-shadow': true, 'shadow-normal-bias': 0.02}),
    s.resources([s.environment('room')^]),
    s.mesh(s.box([5.0, 0.1, 5.0]), s.material('lambert', {color: "#dddddd"})),
    s.mesh(s.sphere(12, 24),
        s.material('standard', {color: "#b97836", metalness: 1.0, roughness: 0.25}),
        {position: [0.0, 0.6, 0.0]})
], {width: 640, height: 400, camera: "camera", environment: "room",
    'tone-mapping': 'neutral', exposure: 1.0})
s.normalize(scene)^
```

Constructors return elements; `normalize(scene)^` validates and supplies
defaults, while `validate(scene)^` returns true or raises a diagnostic.
Coordinates are right-handed, +Y up; rotations are radians and camera FOV is
degrees. CSS determines the viewport size; `viewBox` and
`preserveAspectRatio` fit its logical projected frame as for SVG.

| Helper | Result |
|---|---|
| `scene(children, options?)`, `group(children, options?)` | Scene root or transform hierarchy |
| `lod(levels, distances, options?)` | Distance-selected group levels with retained resources and hysteresis |
| `camera(options?)`, `light(kind, options?)` | Perspective/orthographic camera; ambient, hemisphere, directional, point or spot light |
| `box(size?, options?)`, `plane(size?, options?)` | Primitive geometry |
| `sphere(rings?, segments?, options?)` | Unit-diameter buffer sphere with normals, UVs and indices; defaults 16 × 32 |
| `geometry(positions, options?)` | Buffer geometry; options include normals, `uvs`/`uvs1`, RGB/RGBA `colors`, indices and xyzw tangents |
| `material(kind?, options?)`, `texture(source, options?)` | Material (default basic) or image texture |
| `mesh(geometry, material, options?)`, `resources(children)` | Renderable mesh or shared resource definitions |
| `points(geometry, material, options?)`, `sprite(material, options?)` | Unlit point clouds and camera-facing markers |
| `line(geometry, material, options?)`, `line_segments(geometry, material, options?)` | Continuous bevel ribbons or independent segments |
| `environment(kind, options?)^` | Procedural room/sky or local HDR image environment |
| `load(source, options?)^` | Native scene asset; see the [supported format profiles](../vibe/radiant/Radiant_Design_WebGL.md#renderable-format-profiles) |
| `clip(label, tracks, options?)`, `track(path, times, values, kind?, options?)` | Declarative animation clip/track; [character animation](#22-scene3dmotion--character-animation) adds motion tools |
| `transform(position?, rotation?, scale?)^`, `multiply(a, b)^`, `point(matrix, position)^` | Pure matrix construction, composition and point transformation |

### Masked and reference-pose animation layers

A `keyframe-track` accepts `weight` in [0,1] (default 1). Its contribution is
that mask multiplied by the clip action's current weight/fade. A zero mask
omits that track; normal blending retains authored base-pose contribution when
the total weight is below one. Mask bone position/quaternion/scale tracks
individually to select an upper-body overlay.

For an `additive:true` clip, optional `reference` contains one numeric value per
track component. Numeric/vector/color samples contribute `sample - reference`;
quaternions contribute `inverse(reference) * sample`, weighted along the shortest
arc and applied after the base rotation. Quaternion references are normalized
and must be nonzero. Scale tracks use additive component offsets. Boolean and
string tracks reject references. Without a reference, additive clips retain
their existing precomputed-delta interpretation.

```lambda
import s: lambda.scene3d
s.clip("recoil", [
    s.track("chest.position", [0.0, 0.2], [0.0, 1.0, 0.0, 0.0, 1.1, 0.0],
        'vector', {weight: 0.5, reference: [0.0, 1.0, 0.0]})
], {additive: true})
```

Native admission copies masks/reference poses, bounded by 1,024 tracks per
clip and 256 actions. The native `animation_action_set_blend` operation replaces
a complete action's per-track configuration atomically without resetting
playback/fades; the next sample applies it. Invalid weights, references or
counts retain working settings.
Clip/track edits are staged before publication. Stable clip ID, duration,
additive/autoplay mode, equivalent target/property bindings and value types
preserve playheads, weights, fades, warps, schedules and activation order.
Equivalent tracks may change declaration order, including alongside clip-order
changes. Repeated tracks on one binding match by relative occurrence order. Key values,
interpolation, masks and references can change within that profile. A changed
authored mask/reference replaces its native override; untouched tracks retain
their overrides. New/incompatible clips use their declared initial state while
compatible peers continue. Controllers retain playback when their ordered
states, clips, transitions, fades and rates remain compatible; requested
state/cue changes still take precedence. Invalid descriptions or failed pose
samples retain the previous native owner and clock until repaired. Native
action pointers must be reacquired after an admitted edit. Reconciliation
preserves the scene's automatic/manual mode and samples at zero elapsed time.
Mapped translation/quaternion root totals and compatible native overrides remain
with their semantic bindings. Source revisions still invalidate pending command
batches; channel indices name the current declaration order. Skeleton replacement
and remapping remain separate pending profiles. Native callers of
`animation_mixer_inherit` may provide a bounded target-to-source channel
permutation in each `AnimationActionPair`; a null map and zero count retain
ordered matching. Invalid spans, duplicate/out-of-range indices, value types or
root roles reject before playback/binding changes. Native maps are borrowed for
the call; inherited masks/reference values and root totals own their copies.
Capabilities expose `animation_masks`, `animation_reference_additive`,
`animation_reconciliation`, `animation_track_mapping`, `animation_snapshots`, `animation_markers`,
`animation_event_provenance` and `animation_event_replay`; `animation_graphs`
remains false. The root-motion and authored cue-replay profiles below are
implemented; wider M5 profiles and qualification remain outstanding
(**S12.1.1v3 / S12.1.5v2 / D5.5.2 / D9.5.6**).

### Native animation markers and timing events

Direct Scene3D declarations may put `animation-marker` children alongside a
clip's tracks. Markers require unique nonempty `name` values (at most 127 UTF-8
bytes), nondecreasing `time` values within the clip duration, and optional
`min-weight` in [0,1] (default 0.5). Native admission copies all marker data;
invalid marker edits retain working playback. Marker edits within a compatible
clip retain its action identity, clock, fades and event sequence. Existing
package clip builders retain their track-only input contract; marker factories
are deferred to M13.

```html
<animation-clip id="walk" duration="1" autoplay="true">
  <keyframe-track path="hips.position" times="0 1" values="0 0 0 1 0 0"></keyframe-track>
  <animation-marker name="left-foot" time="0.25"></animation-marker>
  <animation-marker name="right-foot" time="0.75"></animation-marker>
</animation-clip>
```

The scene root receives queued `marker`, `loop` and `finished` events after a
successful native sample (**S12.1.5v2 / D5.5.2 / D6.3.3**). Event fields are
`clip`, numeric `action`, `marker` (empty on loop/finish), crossing `time` in
mixer seconds, `clipTime`, `iteration`, physical `direction` (+1/-1), normalized
`weight`, and `loopDelta` (aggregate signed wraps on loop events). `revision`
and `sequence` are exact decimal strings. Revisions identify admitted native
plans; sequences increase in crossing order and survive compatible plan edits.
These counters belong to the current native owner; replay/reset may replace
that owner. Keep a scene/owner session key when storing traces across resets.

Marker intervals exclude the starting pose and include the reached pose.
Repeat wraps can emit both distinct end and beginning markers; ping-pong turns
emit their shared endpoint once. Paused and zero-time samples emit no cues.
Loop notifications retain their existing aggregate count and identify the last
crossed boundary. A completion identifies the terminating boundary, even if
an update ends later. At a repeat wrap, the outgoing endpoint precedes the
incoming endpoint. Other equal-time records retain active-action order and
declaration order within each traversal segment; legacy native loop/finish
callbacks retain their action order.

A marker fires only when its contribution is strictly greater than
`min-weight`. At the crossing time, contribution is the positive action weight
including its fade, multiplied by its largest track mask, divided by the larger
of one and the total such contribution from active actions. The default admits
only a majority contributor; an exact half-weight tie emits neither cue.
Lower thresholds explicitly permit overlapping cues. This bounded clip-wide
profile does not infer semantic joint groups. Time warps retain the mixer's
existing endpoint-sampled rate profile.

Admission is bounded by 64 markers per clip, 256 queued records per sample,
257 traversal segments per action and 65,536 marker probes per sample. Exceeding
an event/work budget rejects before native animation clocks or bindings advance; callers
must submit smaller updates. Failed pose sampling publishes no timing records.
Native callbacks may release their action or mixer because event payloads own
their marker text. General pose-write rollback is not added by this contract.

Pose seek/reset emits no marker, loop, completion or controller-state cues.
Internal completion transitions still reconstruct controller playback; explicit
cue replay remains future work. Host event delivery uses the existing traced
queue and does not call arbitrary scripts inside native sampling
(**D9.5.6**).

### Native root-translation extraction

A direct `keyframe-track` declaration may set `root-motion="true"` on a
`position` track. A clip admits at most one such vector3 track and one quaternion root track
(described below), and must be nonadditive. Other tracks in that clip keep their ordinary pose behavior.
Tracks sharing a target position must all agree on the root role; mixed pose
and extracted use is rejected. Existing package builders retain their current
input contract; additional construction helpers belong to M13.

```html
<animation-clip id="walk" duration="1">
  <keyframe-track path="hips.position" times="0 1" values="0 0 0 2 0 0"
      root-motion="true"></keyframe-track>
</animation-clip>
```

The extracted channel retains its authored base position in the visual pose.
For each traversed clip interval, its translation is `sample(end) - sample(start)`.
Repeat resets do not add a jump back to the first key; ping-pong reversals and
reverse playback use the same traversal intervals as marker crossings. Finite
completion includes its last interval. Existing interpolation kernels supply
the samples, including cubic tracks. Each update admits at most 257 traversal
segments per action and rejects overflow before native animation clocks or
bindings advance.

For roots targeting the same position, displacement is
`sum(weight * displacement) / max(1, sum(weight))`. Here weight is the positive
action weight/fade at the update endpoint, multiplied by that track's mask.
Paused contributors supply zero displacement while retaining their weight.
The existing endpoint-sampled fade/rate profile applies: changing the step
partition during a varying envelope is not an exact displacement integral.
Physical consumption uses fixed ticks. Translation is expressed in the source
parent frame; quaternion extraction uses that same frame.

`animation_snapshot(node).root_motion` contains copied records with `node`,
`property`, unsigned `binding`, `epoch`, `sequence`, vector3 `delta` and vector3
`total`. Delta describes the last successful sample; total accumulates for
that admitted source. Reads do not consume either value. Zero-time samples
clear delta and retain total. Compatible clip/key/mask edits retain total and
playback; adding or changing root sources changes the native epoch while
retaining totals of compatible sources. Invalid
roles, traversal work or nonfinite displacement retain working native data.

Pose seek changes the root epoch and resets delta/total instead of extracting
skipped movement, including controller-chain reconstruction. Explicit native
world replay retains displacement sampling for physical consumption.
Counters belong to the current native owner and can restart when replay/reset
reconstructs it; preserve the scene/owner session identity when storing traces.
Snapshots copy numbers under the existing precise-root and host-boundary
contracts (**S12.1.5v2 / D10.1.7 / D5.5.2 / D6.3.3 / D9.5.6**).

The `animation_root_translation` capability advertises this extraction profile.
The separate `root_motion_capsule` and `root_motion_replay` capabilities
advertise the physical profile below. Quaternion extraction and capsule turns
have separate capabilities; wider native M5 plans remain pending.

### Native root-rotation extraction

A `quaternion` track may set `root-motion="true"`. A nonadditive clip admits one
quaternion root alongside its optional position root. Other tracks keep their
ordinary pose behavior. A target quaternion cannot mix extracted and ordinary
pose roles. Euler `rotation` tracks are ordinary pose tracks; author quaternion
keys for extracted turns.

```html
<keyframe-track path="hips.quaternion" times="0 1"
    values="0 0 0 1 0 .7071067811865475 0 .7071067811865476"
    root-motion="true"></keyframe-track>
```

The visual quaternion retains its authored base orientation. Each traversed
interval extracts `end * inverse(start)` in the source parent's frame. Changes
compose on the left in traversal order, including nonidentity starts and
changing rotation axes. Repeat resets add no reverse jump; reverse, ping-pong,
finite completion, zero-time samples and silent seek use the same bounded
traversal as translation/markers. Existing track interpolation, including
normalized cubic sampling, supplies the endpoints.

Contributing quaternion deltas use the existing incremental weighted shortest-arc
slerp profile. Weights include action/fade and track mask. Contributions below
unit total weight blend with identity; totals above one normalize. Paused
contributors supply identity while retaining weight. Multiple-axis blends
retain the declared active-action order, as pose blending does. Endpoint-sampled
weights/rates retain the existing fixed-step envelope contract.

`animation_snapshot().root_motion` records for property `quaternion` contain
four-component `[x,y,z,w]` `delta`/`total` arrays. Position records retain three
components. Total is cumulative orientation, modulo full turns; it is not an
unwrapped angle or a turn counter. Zero-time samples set quaternion delta to
identity and retain total. Seek resets quaternion total to identity and changes
the shared root epoch. Compatible key edits preserve totals/playback; rejected
roles/work retain working native clocks and copied data. The
`animation_root_rotation` capability advertises full 3D quaternion extraction
(**S12.1.5v2 / D5.5.2 / D6.3.3 / D9.5.6**).

### Physical root translation

A `rigid-body` with `type="kinematic-position"` may set
`root-motion-source="hips"`. The source names admitted extracted `position`
and/or `quaternion` bindings on the same node. The body must have exactly one
solid capsule, no enabled joint and no navigation/ragdoll controller. At most 32 bodies may use this profile. Parent
transforms convert the source-local translation into world displacement;
rendering retains the source's in-place base pose and the body's achieved pose.

```html
<rigid-body id="walker" type="kinematic-position" root-motion-source="hips">
  <collider id="capsule" shape="capsule" radius=".25" height="1"></collider>
  <group id="hips"><!-- character visuals --></group>
</rigid-body>
```

Use the existing character movement options as body attributes prefixed with
`root-motion-`: `up`, `skin`, `slope-angle`, `step-height`, `snap-distance`,
`max-slides`, `recovery-iterations`, `max-iterations` and `geometry-budget`.
Defaults, numeric limits, capsule orientation and precision follow the shared
character controller. The movement phase is additionally capped at 65536
geometry tests across all root controllers in one tick; preflight admission probes retain each
controller's configured query budget. Queries use the current sampled world;
future obstacle motion and pushing retain the character controller's limits.

Each fixed tick samples animation, computes all root capsule movements, submits
one native target batch, runs physics and publishes feedback. Clip time
continues while blocked; blocked intent is discarded rather than accumulated.
The root controller owns its body's native targets: direct character moves,
explicit target commands and a second navigation/ragdoll writer are rejected.
Teleport/world replacement and source epoch changes rebase consumed totals.
Compatible key changes retain playheads and consumption.

Physics pause also suspends coupled automatic animation. Explicit scene or
physics tick batches use the shared coordinator; direct animation-only advance
and pose seek reject on a coupled owner. `seek`/`reset` reconstruct a separate
world and mixer from authored initial state and publish after successful quiet
replay. Replay uses current authored clips and emits no marker/contact cues.

`animation_snapshot(node).root_controllers` copies one record per body:
`body`, `source`, `tick`, `world_identity`, `epoch`, `sequence`, `initialized`,
`blocked`, `grounded`, `wall`, `ceiling`, `stepped`, `geometry_tests` and vector3
`requested`, `achieved`, `position`, `recovery`, `platform_motion`. It also copies
four-component `requested_rotation`, `achieved_rotation` and body `quaternion`
arrays. Turn deltas use world axes; body quaternion is the completed world
orientation. Translation-only controllers report identity turn deltas. Feedback
belongs to the last completed physics tick; repeated reads do not advance it,
and saved records survive later ticks and replay. Root-source/profile/parameter
admission retains committed controller feedback on rejection. Whole-scene
transactions spanning simultaneous physics and animation edits remain a wider
M1 extension (**S12.1.5v2 / D10.1.7 / D5.5.2 / D6.3.3 / D9.5.6**).

A source quaternion is consumed in the same target transaction as translation.
Translation uses the source parent's pre-turn axes. When that parent follows
the controlled body, achieved heading rotates the next tick's translation.
Rotation-only sources are supported. The capsule's up axis
stays fixed: turns must preserve configured `root-motion-up`, and the capsule
centre must lie on that pivot axis. Source-parent transforms must be proper
rigid frames with positive uniform scale. Nonuniform/reflected frames and
off-axis pivots reject during admission; an out-of-profile sampled tilt rejects
before any root body target or completed feedback commits. The shared runtime
faults the failed tick; use explicit reset/replay after correcting such a sample.

The admitted up-axis turn preserves the capsule collision volume, so the
existing character query remains valid while translation blocks/slides. Tiny
float-frame roundoff is removed after up-axis admission. This profile does not
perform arbitrary rotating-shape sweeps or push dynamic obstacles. The
`root_motion_capsule_turns` capability identifies this fixed-up consumer;
full 3D extraction remains available independently. Compatible rotation-key
edits retain achieved heading and consumption. Quiet world replay uses current
authored keys; reset selects authored initial orientation.

### Tick-targeted native animation commands

`animation_commands(node, batch)` queues copied native playback commands through
`radiant-dom`; it does not evaluate a logical graph or advance a scene. Read
`animation_snapshot(node)` for the current `session`, `generation`,
`service_generation`, source `revision`, `world_identity`, completed `tick`,
`next_command_tick`, `command_sequence` and per-clip `identity` values. These
counters accept exact unsigned integers, nonnegative signed integers or complete
unsigned decimal strings. Float counter values reject. Tokens belong to the
current process/runtime; reconstruct envelopes for recorded replay.

A batch contains those five session/generation/source/world fields, plus a
strictly increasing nonzero `sequence`, explicit target `tick` and `commands`
array. At most 32 batches may be pending; each has 1–256 commands and targets an
unsampled future tick no more than 100000 ticks ahead. Use `next_command_tick`
rather than guessing the next boundary: an animation-only frame may already have
sampled part of the current tick. Late targets reject rather than being shifted.
Batches execute by target tick, retaining submission order for a shared tick.
A sequence cannot be reused even after its batch applies or is invalidated.

Each command supplies string/symbol `op`, `clip` and the clip's opaque
`identity`. Native compatibility-controller clips reject this second writer.
The command array executes in order within a disposable playback/pose stage:

| `op` | Fields and effect |
|---|---|
| `play` | Optional bool `reset` (false); activate, enable and unpause the clip. Reset selects its initial phase/envelopes first. |
| `stop`, `reset` | Stop releases active bindings and resets playback; reset retains activation while resetting phase/envelopes. |
| `weight` | Finite nonnegative `value`; existing fade weighting and blend normalization still apply. |
| `rate` | Finite signed `value`; replaces time scale and cancels a prior warp. |
| `pause` | Bool `paused` (true); holds that clip's playhead. |
| `phase` | Finite `value` in `[0, duration]`; selects clip seconds and restarts its loop counter without traversing skipped motion/cues. |
| `mask` | Zero-based exactly integral numeric `channel` or unsigned decimal string, below the clip's track count; finite `value` in `[0,1]`. Preserves other masks/reference poses. |
| `fade` | Finite nonnegative `duration`, bool `fade_in` (true); uses the existing native envelope. |
| `warp` | Finite `from`, `to`, nonnegative `duration`; requires a nonzero current rate. |
| `crossfade` | Another `other` clip and `other_identity`, nonnegative `duration`, bool `reset` and bool `warp` (both false); activates the destination and applies native crossfade/optional rate warp. |

Bounded channel indices use the shared exact-integer reader, including integral
JS numeric literals; fractional/nonfinite/out-of-range values reject. Opaque
session/generation/action counters retain their strict counter reader.
Scalar values use finite native numeric promotion. Immutable key storage stays
with its scene owner; playback, masks/references, marker caches, root totals and
binding values are copied. The stage preserves captured base poses, validates
its pose/controller profile and admits the next fixed tick's timing/root/cue
work before publication. Rejected application preserves live animation state.
Phase commands retain cumulative root totals and emit no skipped footsteps.
Source changes, owner replacement and reset/replay invalidate pending generations;
world identity is also checked at delivery. Compatible template rebuilding and
camera-only edits retain their existing owner contracts.

Queue acceptance returns the copied runtime snapshot. `pending_commands` counts
owned batches; `command_sequence` is the highest accepted sequence. Runtime
snapshots also copy `commands_applied`, `commands_rejected`,
`last_command_sequence`, `last_command_tick`, `last_command_applied` and
`command_diagnostic`. Rejection counters describe previously accepted batches;
malformed/stale submissions return an immediate error. Application receipts
refer to the command phase, before later simulation stages. They do not promise
that subsequent physics/contact work completes; inspect `faulted` and the
completed scene/physics tick for that result. Whole-tick cross-service rollback
remains a wider M1 extension (**S12.1.5v2 / D5.5.2 / D6.3.3**).

Commands run before navigation, animation and physics, with no script callbacks
inside those stages. Queuing does not start an otherwise manually controlled
clock: use `play` or explicit `step`. Pending automatic commands can wake an
explicitly playing inactive animation service; paused/root-coupled physics keeps
its existing ownership. While commands are pending, automatic delivery uses
bounded fixed ticks; once they drain, animation-only views resume exact selected
frame sampling. Repeated reads and zero elapsed time do not consume batches
(**D10.1.7**).

Reset/replay reconstructs authored state and drops pending batches. Previously
applied command history is not replayed implicitly; M13 owns recording,
retargeting and logical decisions. Runtime capabilities `command_batches` and
`animation_command_batches` advertise this queue/animation profile; other
service command kinds remain separate native components. The thin runtime
wrapper adds no graph algorithm (**S12.1.1v3 / D9.5.6**).

### Explicit authored animation-cue replay

`replay_animation_events(node, from, to, deliver = true)` reconstructs the
current authored animation/physics timeline privately and selects cues in
`(from, to]`. Both endpoints must be exact fixed-tick boundaries, ordered and
nonnegative, within the existing total replay budget of 100000 ticks. The
selected window admits at most 256 copied marker/loop/completion records;
overflow or failed reconstruction returns an error without queuing a prefix.
A silent prefix does not spend that record budget. Empty windows succeed.

The reconstruction uses current declarations, including masks, contribution
thresholds, signed controller rates, ping-pong turns and one-shot handoffs. It
does not apply subsequent native commands or replay application decisions.
Recording/resubmitting those decisions remains M13. This explicit operation
preserves live playheads, poses, root totals, world identity, pause/automatic
mode, fractional clock debt and pending command batches. Ordinary seek/reset
continues to emit no replay cues (**S12.1.1v3 / S12.1.5v2 / D10.1.7 / D5.5.2**).

The copied result contains `session`, `generation`, source `revision`,
`command_revision`, live `world_identity`, `replay_sequence`, `from_tick`,
`to_tick`, normalized `from`/`to`, `authored: true`, `count` and `events`.
Each event records `type`, `clip`, copied `marker`, exact `action` and window
`sequence`, plus `time`, `clip_time`, `iteration`, `direction`, `weight` and
`loop_delta`. Action identities name the currently admitted source clips;
these records describe the reconstructed authored timeline, not live history.
The animation snapshot's `event_replays` counts successful requests, including
queries and empty windows. Compatible source refresh retains this counter;
a reconstructed native owner starts its own domain.

`deliver: false` returns records without scheduling notifications. With
`deliver: true`, `delivery_requested` is true and a nonempty result sets
`delivery_queued` after admitting one traced host job containing all records.
An empty result queues no job. Those fields describe queue admission, not
handler completion. Reconstruction and event construction finish before any
handler runs. Delivery follows existing DOM event rules; handler effects are
not a transaction and document teardown invalidates the queued target
(**D6.3.3 / D9.5.6**).

Replayed DOM events have `replay: true`, decimal-string `replaySequence` and
window-local decimal-string `sequence`. Live events have `replay: false` and
`replaySequence: "0"`; their ordinary event sequence remains independent.
Both expose exact decimal-string `actionIdentity` alongside legacy numeric
`action`, plus copied decimal-string `sceneSession` and `sceneGeneration`.
Together with `replaySequence`, these distinguish queued records across native
owner resets even when the local request counter restarts. Event names, times, contribution and source `revision` use the same
native builder as live delivery. Native records and queued values own their
copies after private worlds/clip pools are destroyed or source descriptions
change. `animation_event_replay` advertises this authored replay capability;
logical graph or command-history replay is a separate later package feature.

### Retained camera controls

Import `lambda.scene3d.viewer` to add a retained Lambda view around a Scene3D
element. The viewer updates the live DOM camera through the `radiant-dom` host
boundary (**D9.5.6**); its event handlers and camera procedures are procedural
(**S12.1.1v3**).

| Function or procedure | Behavior |
|---|---|
| `source(scene, lens, initial = null, labels = {}, chrome = true)` | Build a retained viewer source from a scene and prepared camera. Set `initial` to restore a live pose and `chrome` to false to omit the standard toolbar and help. |
| `interactive(scene, lens, initial = null, labels = {}, chrome = true)` | Apply the viewer source directly. |
| `controls(labels = {})` | Build the standard orbit, zoom and reset toolbar for use with a viewer. |
| `snapshot(owner, id, fallback)` | Read the live camera with the given DOM id into a prepared camera, using `fallback` for other fields. |
| `set_camera(owner, id, next)`, `command(owner, id, action)` | Update the retained camera or issue an orbit, zoom, reset, preset-view or interaction-mode command. |

Dragging orbits; Shift-drag or pan mode pans; the wheel zooms. Arrow keys
orbit, `+` and `-` zoom, and `R` resets. Set `pan_arrows` in `labels` to pan
with unshifted arrow keys. The options `min_distance`, `max_distance`,
`min_polar` and `max_polar` constrain camera movement; `after` inserts custom
elements after the scene, and the remaining label fields customize the toolbar
and help. Retain `source` in a binding when a parent view regenerates its
content, and use `snapshot` before replacing a scene descriptor to preserve the
live pose.

### Live runtime controls

Import `lambda.scene3d.runtime` to control a live scene DOM node. Every operation
is procedural (**S12.1.1v3**) and uses the existing `radiant-dom` host boundary
(**D9.5.6**); constructing a scene does not invoke these procedures.

| Procedure | Behavior |
|---|---|
| `play(node)`, `pause(node)` | Start the prepared animation/physics services, or pause their shared scene clock. |
| `step(node, ticks = 1)` | Pause and advance all prepared native services by a bounded whole tick count, at most 100000 per call. |
| `seek(node, seconds)`, `reset(node)` | Pause and reconstruct authored initial physics plus animation at the selected fixed-tick boundary; reset selects zero. Replay is bounded to 100000 ticks and emits no replay contact or animation cues. |
| `snapshot(node)` | Read `tick`, `time`, `timestep`, `debt`, `time_scale`, `paused`, `active_services` and `faulted` without advancing the scene. |
| `animation_commands(node, batch)` | Queue copied tick-targeted native playback commands; return acceptance metadata without stepping. |
| `particle_commands(node, batch)` | Queue copied, bounded particle spawn requests for an unsampled future tick; return shared command acceptance metadata. |
| `replay_animation_events(node, from, to, deliver = true)` | Reconstruct an authored cue window privately; return copied records and optionally admit deferred host delivery. |
| `animation_snapshot(node)` | Return copied command identities/counters, mixer time, plan revision, automatic mode and per-clip identities, playheads, weights/rates, loop counts, activation/pause/schedule and fade/warp flags, plus copied translation/quaternion root motion and completed controller feedback. |
| `time_scale(node, value)` | Set a finite nonnegative scene-clock multiplier; zero parks its scheduler. |
| `capabilities(node)` | Report currently implemented coordinator profiles and false for the listed pending profiles, including broad navigation, convex CCD and animation graphs. |

`animation_snapshot` uses `animation_read` at the existing host boundary. It
admits pending descriptions and copies timing data without advancing a tick or
mixer playhead. Clip identities and plan revisions are unsigned 64-bit values;
treat them as opaque within the current native animation owner. Explicit
replay/reset may reconstruct that owner and restart these counters.
Saved snapshots retain their values after later edits or playback. They do
not serialize the solver or implement package logical-state replay.

Active physics uses bounded fixed substeps with retained tick debt. Legacy
animation-only views retain exact selected-frame sampling (**D10.1.7**) through
the same scheduler entry. Compatible template regeneration, camera changes and
graphics recovery preserve native scene state (**D5.5.3 / S12.1.5v2**).
Seek/reset replays authored input; it does not restore later commands or a saved
solver checkpoint. The remaining gaming features and qualification limits are
tracked in the [gaming implementation record](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md).

### Shared decoded audio foundation

The first M7 provider component connects existing `dom.audio_open`,
`audio_play`, `audio_pause`, `audio_close` and `audio_state` procedures to the
same document submix as MIDI and speech. `dom.audio_clock(owner)` counts rendered
frames on that submix. The scene integration below adds sources/listeners,
Doppler, bounded bus/DSP graphs, native sample-time cues and explicit offline
PCM/WAV jobs, named MIDI/speech routes and macOS video-track output with named
scene routes. Prepared MIDI/speech/video snapshots now join explicit offline jobs;
explicit microphone acquisition uses the host capture seam below
(**D9.5.6 / D11.3.1 / D11.3.2**).

Opening encoded bytes prepares owned 48000-Hz interleaved stereo float PCM
without opening an output device. The portable decoder supports PCM WAV, MP3
and FLAC; macOS retains platform decoding, including AAC/M4A, through Core Audio.
Codec availability remains platform-dependent. Source bytes may be released
after open. Invalid, unsupported or over-budget input returns `null` and leaves
existing clips intact. Decoding and channel/rate conversion happen before the
audio callback (**D5.5.2**).

Native limits are 256 MiB of encoded input and 256 MiB of retained PCM per clip,
512 MiB of decoded-player/stream storage and 128 combined decoded clips and
video-stream reservations per document.
Ready, paused and ended clips retain their PCM until explicit close or document
teardown. Only playing clips occupy a voice in the existing global 96-voice
table, shared with MIDI and speech; exhaustion rejects the new play without
retiring accepted peers. PCM storage is bounded; full streaming and peak-memory/
release-performance qualification remain separate work.

Play accepts a finite gain from zero to one, resumes a paused cursor and restarts
an ended clip. Pause removes its callback voice while retaining the cursor.
Ended voices retire during ordinary host polling; they cannot keep offline
frames pending indefinitely. Close invalidates the document-scoped token and
releases callback readers before freeing PCM. Device retirement sets affected
clips to `error`; a fresh open/play can join the recovered output service.

Offline output (`LAMBDA_AUDIO_OUTPUT=offline`) uses the same native mixer without
a device. Idle-to-active transitions anchor to the requesting document's clock,
so the first frame includes elapsed playback time and does not consume earlier
idle time. A document's own realm supplies its clock even when another realm is
thread-active.
The mixer only adds prepared samples; it allocates nothing, fetches no resources
and enters neither DOM/GC nor script. Numeric conversion/resampling can differ
from the previous platform player's output.

Local documents retain ordinary audible starts. Network documents require their
own sticky activation or the host grant; muted playback and decoding retain
**D11.3.1** behavior. These existing play rows keep their start gate with the
host's offline output backend; explicit buffer/file jobs are described below.
See the [gaming implementation record](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md)
for qualification and remaining scope.

### Shared HTML video audio

On macOS, existing HTML media commands decode an audio-bearing video's first
audio track into the document submix. AVPlayer remains silent. Consumed stereo
PCM at 48000 Hz supplies media time and frame selection; starvation holds that
time instead of silently skipping samples. Pause retains the cursor, seek
replaces queued PCM, looping refills the bounded stream and mute/volume affect
the shared output. Silent videos retain the platform video clock.

The native profile admits finite media up to 24 hours, a two-second PCM ring
and one decoded packet of at most 65536 frames. Control-thread polling prepares
PCM; the audio callback only consumes owned samples (**D5.5.2**). Each stream
reserves its ring storage against the document's shared reader/byte limits.
Decoder failure, voice admission failure and output loss fail the media owner;
no second audible player takes over. Removal and document teardown retire the
reader before releasing its storage.

Network play, unmute and a positive gain increase from zero require the same
sticky activation as other audible output (**D11.3.1**). A refused command returns
`false`; JavaScript reports `NotAllowedError`. Removing authored `muted` without
activation pauses playback. These checks also apply to the host's offline
backend. Named video routes and prepared export snapshots are described below. Microphone acquisition,
other platform backends and release/device qualification remain pending.

### Native Scene3D clips and spatial sources

The `scene3d/runtime` procedures `audio_snapshot(node)`, `audio_clip(node,
request)`, `audio_clip_drop(node, request)` and `audio_command(node, request)`
use the existing procedural host boundary (**D9.5.6**). Acquiring an encoded
clip is explicit; a pure `audio-source` description does not fetch or decode.
Every mutation requires the snapshot's opaque `session`; preserve native u64
values (JS BigInts) or exact decimal strings rather than converting through JS
Number (**S4.9.1**).

Acquire with `{session, id, expected: 0, bytes}` (use `0n` in JS). Replace with the current clip's
`identity` as `expected`; an absent/stale identity or failed decode preserves the
old asset. Drop with `{session, id, identity}`. A scene admits 16 assets and 32
sources; assets and source players share the document's 128-player/512-MiB
limits and global 96-voice bound. Each source has an independent cursor/filter/
meter over immutable shared PCM, charged once per sample owner. Replacing or
dropping an asset leaves already playing readers on their retained samples
until stop/restart or teardown (**D5.5.2**).

Declare a leaf `audio-source` with `id`, `clip` and ordinary transform attributes
beneath the scene or a transformed group, body or bone. IDs must be unique
within that scene. `spatial` accepts `none` (preserves stereo), `2d` (world XY)
or the default `3d`. Positional modes average the two input channels to mono
before applying spatial gains. A scene selects a camera/listener with `audio_listener`;
otherwise its one explicit `audio-listener` wins, then the active camera.
Listener orientation uses the renderer's target/up and world-pose rules. A
positional source needs a valid listener. Audio ignores visual visibility/
culling and survives graphics-context recovery.

Source controls are finite `gain` [0,1], `pitch` [0.125,8], `smoothing` seconds
[0,10] (default 0.01), and `lowpass` Hz [20,24000]. The mixer smooths stereo
gains, pitch and the one-pole filter coefficient per output sample. A `loop`
source wraps its sample cursor. `distance_model` is `inverse` (default),
`linear`, or `none`; `ref_distance` defaults to 1, `max_distance` to 1000 and
`rolloff` to 1. Distances clamp to the reference/maximum interval. Inverse gain
is `ref/(ref+rolloff*(distance-ref))`; linear gain is
`max(0,1-rolloff*(distance-ref)/(max-ref))`. Equal-power stereo panning follows
the listener's world right vector. Cone half-angles `cone_inner`/`cone_outer`
are radians [0,pi] (default pi), with `cone_gain` [0,1] outside the outer cone;
the source forward axis is local -Z in 3D and +X in 2D.

World `velocity` on a source/listener, or `audio_velocity` on a camera, overrides
native motion estimation. `doppler` defaults true; `audio_speed_of_sound` on
the scene defaults to 343 world units/second. Doppler uses
`(c+listener_radial)/(c+source_radial)`, radial clamps of +/-0.95c and the pitch
limits above. Positions/velocities are bounded to +/-1e6 per component. Fixed
physics ticks and selected animation-only frames supply poses in scene seconds;
repeated reads do not erase estimated velocity. Authored same-time jumps reset
estimation. The `teleport` command also skips the next positive-time estimate.
Selected animation snapshots retain **D10.1.7** timing.

Commands use `{session, source, identity, action}` with the source snapshot's
identity and `play`, `pause`, `resume`, `stop` or `teleport`. Play starts a fresh
cursor on the selected asset; pause/resume retain the cursor, while stop releases
the source player. Rejected starts preserve existing audio. Invalid authored
edits preserve admitted targets and still permit explicit pause/stop/drop.
Scene pause/time scale affect pose sampling; source transport and pitch remain
explicit. A successful scene seek/reset stops sources and resets their velocity
estimators, retaining acquired assets. Compatible template rebuilds preserve
identities/cursors; removed sources, scenes and documents retire callback
readers before freeing native storage (**D5.5.2 / D6.3.3**).

Snapshots copy clips, source transport/pose/target and current gain/pitch,
cursor/duration, rendered-frame count and peak/RMS meters, listener metadata,
scene time, document audio clock and native storage accounting. They expose no
DOM, graphics or player tokens. Network playback and audible gain changes use
authored gain for **D11.3.1**, including distant/attenuated sources and the host
offline backend. Device loss marks active players `error`; explicit play can
prepare a new reader from the retained asset. The cue and explicit render-job
contracts below define their additional behavior. Named video routes are
described below; HRTF/surround and ratified capture remain separate components. Music/voice/
occlusion policy remains deferred M13.

### Native Scene3D bus routing and DSP

Declare `audio-bus` nodes under a scene, with unique `id` names. A source's
`bus` selects one of them; its default is `master`. Once a scene declares buses,
its implicit `master` routes into the existing document submix. An explicit
`audio-bus id="master"` controls that scene's master. Other documents and default
MIDI/speech/decoded voices retain their document routes. Definitions prepare
native storage without opening an output device; source play retains **D11.3.1**.
Use existing procedural DOM attribute/child edits for live bus controls, and
`runtime.audio_snapshot(node)` to inspect the admitted plan (**D9.5.6**).

A scene admits 16 buses including master, four sends and four ordered inserts
per bus, with at most 8 MiB retained graph storage. Each document has eight scene
graph owners under its one submix; all sources still share the existing global
96-voice bound. Nodes default to `parent="master"`, `gain="1"`,
`smoothing="0.01"`, `mute="false"`, `solo="false"`; master has no parent.
Gain is [0,4] and smoothing is [0,10] seconds. Add leaf `audio-send` children
with `bus="target"` and `gain` [0,1] (default 1). Parent and explicit sends sum
after inserts and the bus fader. Bus/send gain edits smooth per output sample.
Solo selects direct inputs beneath soloed parents and permits their downstream
send/parent paths; mute silences the bus fader. Shared return histories remain
shared. Unknown names, duplicate IDs, cycles (including sidechain dependencies),
invalid parameters and storage/connection limits reject the candidate while
preserving the admitted graph and source targets.

Leaf `audio-effect` children run in declaration order before the bus fader:

| `type` | Controls and native profile |
|---|---|
| `lowpass`, `highpass` | Stereo biquad; `frequency` [20,22000] Hz, default 1000, and `q` [0.1,20], default approximately 0.7071. |
| `eq` | Peaking biquad with the same frequency/Q controls and `gain_db` [-24,24], default 0. |
| `delay` | Stereo sample ring; `seconds` [1/48000,2], default 0.1 (rounded to whole frames), `feedback` [0,0.95], default 0.3, and `wet` [0,1], default 0.5. |
| `reverb` | Reuses the shared Freeverb room; `wet` [0,1], default 0.5. |
| `compressor` | Stereo peak detector, hard knee; `threshold` [-96,0] dB, default -18, `ratio` [1,20], default 4, `attack` [0,2] seconds, default 0.01, `release` [0.001,10], default 0.1, and `makeup` [0,24] dB, default 0. An optional `sidechain="bus"` reads that earlier bus's post-fader signal. |
| `limiter` | Instant stereo-linked peak ceiling; `ceiling` [0.001,1], default 0.98. The window's final mixed output also retains its existing [-1,1] safety clamp. Lookahead/oversampling are separate quality profiles. |

Compilation, storage and filter coefficient preparation occur on the main
owner; publication and source routes change under the existing submix boundary.
The callback uses fixed scratch and native DSP state, with no allocation,
blocking, resource IO, DOM/GC or script. Retained bus names preserve opaque
identities; admitted removal invalidates a route, and re-addition gets a new
identity. Matching insert positions/kinds retain history across parameter
changes; changing delay length or insert kind starts fresh state. Compatible
template rebuilds and graphics recovery preserve routing and histories.
Removed/stale routes stay silent rather than falling through to the dry output
(**D5.5.2 / D6.3.3 / S4.9.1**).

Actual delay/filter/reverb work keeps the submix alive after its final source
voice retires. Delay/filter activity uses a 1e-12 quiet floor; the reused reverb
retains its existing 1e-20 denormal flush. Main-owner polling continues for
online and offline transports, and inactive documents do not consume device
clock frames. Explicit native offline capture can request silent intervals.
Source stop permits existing graph tails to finish; scene seek/reset clears
them. Device loss resets histories and work, and requires explicit source play
to recover. Nonfinite graph output marks it faulted and silences that graph;
scene reset clears the fault. Physical hotplug and callback deadlines remain
separate qualification work.

MIDI preparation now retains its synth without a voice/device. Play admits a
voice; pause/end removes it while retaining cursor and reverb-send controls.
Reverb tails continue after MIDI voice retirement. A muted MIDI transport may
play without activation; positive audible starts and live gain changes retain
**D11.3.1**. Graph/source snapshots copy ordered insert parameters, send target/
current gains, bus peak/RMS/frame meters, routing generation/storage/activity and
fault state. Prepared provider export uses the render jobs below; explicit
microphone capture uses its own document grant. Routing factories and music/voice/occlusion
policy remain deferred M13.

### Native Scene3D sample-time cues

`runtime.audio_schedule(node, request)` admits copied transport commands through
the existing host waist (**D9.5.6**, proposal §9.2). Read `audio_snapshot(node)`
first. A request contains its `session`, `cue_epoch` as `epoch`, the next
`cue_sequence + 1` as `sequence`, and an `events` array of 1–32 maps:

| Field | Contract |
|-------|----------|
| `source`, `identity` | Source ID and exact current source identity. |
| `op` | `play`, `pause`, `resume` or `stop`. Resume requires a paused player at delivery. Stop rewinds to ready. |
| `clip_identity` | Required only for play; identifies the current asset named by the source's authored `clip`. |
| `clock` | `audio` (default) or `scene`. |
| `frame` | Audio clock: exact nonnegative document sample index, below `UINT64_MAX`, at 48000 Hz. |
| `seconds` | Scene clock: finite nonnegative scene timestamp. Supply either this or `frame`, according to `clock`. |
| `late` | `reject` (default), `now` (next available sample boundary) or `drop` (completed `dropped_late` receipt). |

All peers are validated and readers prepared before publication. Rejection
preserves earlier admitted commands and source transports. A scene retains at
most 64 pending/retiring records; each source with pending work reserves one of
the existing 96 shared native voices. Prepared readers count toward the ordinary
128 document players and shared PCM budget. Overflow rejects the whole batch.
Matching sample deadlines execute in event-ID order. A contended voice produces
`dropped_busy`; the callback never waits or delivers that command later.

An identical retry of the last admitted sequence returns the same event IDs,
including after delivery, cancellation or clip removal. A changed canonical
payload at that sequence, an older sequence, a gap, or a stale session/epoch is
rejected. This is a bounded last-batch retry contract. IDs and sample frames
remain exact through the foreign membrane (**S4.9.1**); JavaScript callers use
BigInt for wide identifiers and sample counters.

Scene timestamps map from a sampled scene/document-clock anchor, rounding the
computed positive sample offset upward. Scene pause, zero time scale and scene
faults hold future scene cues; play/scale changes remap them, and an explicit
paused step can release a reached cue. Pending scene cues wake the existing
coordinator without adding a solver phase. Direct audio-frame cues follow the
document sample clock independently of scene pause/scale. Source playback also
continues independently. Pitch follows authored pitch and Doppler by default;
`pitch_follow="true"` additionally multiplies it by scene time scale, clamped to
the source's 0.125–8 range. Selected animation sampling retains **D10.1.7**.

Queued play retains its admitted PCM across asset replacement/drop, and uses
the source's latest published spatial/gain/pitch parameters at delivery.
Positive authored-gain play/resume and raising an armed muted source's gain
require the existing document activation (**D11.3.1**). Template and graphics
recovery retain cues. Immediate source transport cancels its remaining cues;
source removal cancels that source, device loss cancels pending delivery, and
successful scene seek/reset cancels all pending cues and advances `cue_epoch`.
No implicit replay follows those operations.

`runtime.audio_cues_cancel(node, {session, epoch, event})` cancels one pending
event; omit `event` or pass zero to cancel all. Cancellation remains available
after invalid authored edits. Snapshots copy `audio_frame`, `cue_epoch`,
`cue_sequence`, `cue_next_id`, last-batch `cue_receipt` IDs, `cue_pending` and
the most recent 256 `cue_history` rows plus `cue_history_dropped`. Rows include
event/source/clip identities, operation/clock, effective `requested` sample,
`actual` completion sample, `held` and `status`. Pending `actual` is zero.
Delivery, admission and cancellation are distinct observations; history can
overwrite old rows. Source rows add `armed` and `pitch_follow`.

Native readers and commands have explicit ownership (**D5.5.2 / D6.3.3**).
Preparation and retired-reader cleanup occur on the main owner; the callback
only applies copied commands and swaps retained PCM readers at split-buffer
deadlines. Prepared provider snapshots join render jobs below; microphone
acquisition uses the explicit capture seam below. Physical device and release
callback/memory qualification remain M7/M12 work; cue selection and voice
policy remain deferred M13.

### Native Scene3D MIDI and speech routes

Existing document MIDI sessions and speech utterances can explicitly join an
admitted scene bus through `runtime.audio_provider_route(node, request)`.
This reuses their native transports and the document submix; it creates no
second mixer or playback clock (**D9.5.6 / D5.5.2**).

| Procedure | Request and behavior |
|-----------|----------------------|
| `audio_provider_read(node, request)` | `{session, provider, token}` reads a copied provider routing snapshot. `provider` is `midi`, `speech`, `video` or `capture`; `token` identifies an existing provider in this document. |
| `audio_provider_route(node, request)` | Adds `expected`, the provider's current route, plus `bus` and that bus's exact `identity` from `audio_snapshot`. Returns the new snapshot after atomic publication. Omit `bus` or pass null, with zero/omitted `identity`, to select the ordinary document dry route. |

Snapshots contain `session`, `provider`, `token`, `route`, `route_valid`,
`state`, `position` in seconds, authored `gain`, and `loss` (`none`,
`route_lost` or `device_lost`). Session/provider/bus identities and the expected
route use exact counters; JavaScript uses BigInt (**S4.9.1**). Stale session,
provider, bus and expected-route tickets reject without changing the transport.
Providers are document-owned and may route between that document's scenes.
Each submix admits at most 128 retained provider route references.

MIDI retains its route while ready, paused or ended so a later explicit play
uses the same bus. Speech retains its route while queued, rendering or paused,
including before its voice exists; each utterance selects its own route when
the shared speech voice starts it. Speech end/cancel/error releases the route
reference. MIDI close and explicit detach release theirs. Detaching does not
restart a transport. Named routes use the bus's gain/mute/solo, sends and ordered
inserts. Named MIDI inputs omit the old document reverb send, which would bypass
the bus fader; returning to the default route restores that send. Already
generated document reverb tails retain their ordinary lifetime.

Removing a retained bus from a replacement graph rejects before source controls
publish. Detach or rebind its providers first. Removing the graph owner instead
silences its routed voices atomically and makes retained providers fail with
`route_lost`; it does not fall back to audible default output. Failed providers
remain queryable through their ordinary document rows and require a new session
or utterance for playback. Read/default-detach remains available after invalid
authored scene edits (**D5.5.2 / D6.3.3**).

Route preparation for inactive/paused providers opens no device. Audible
play/resume, active positive-gain rewiring (including default detach), and
authored bus edits affecting active positive-gain providers retain **D11.3.1**
activation. These checks use provider state as well as DSP tail state, so a dry
MIDI/speech input cannot evade the gate. Muted preparation/playback and bus
edits with paused providers remain allowed.
Prepared providers can also supply copied PCM to the explicit render jobs below.
Ratified capture remains native M7 work; music/voice policy remains deferred M13.

### Native Scene3D video routes

On macOS, an admitted audio-bearing HTML media source exposes an exact
`audio_token` in `dom.media_info(node)`; unavailable/silent sources expose null.
Use that ticket with the provider read/route procedures above and
`provider: "video"`. It identifies the selected native source in its requesting
document, independently of scene/bus identities (**S4.9.1 / D9.5.6**). A foreign
document's ticket or a retired source ticket rejects.

Routes can be assigned before PCM starts, while paused, or during playback.
Rewiring preserves media time and queued samples. `gain` includes the media's
mute state and volume, before the scene bus fader. HTML media commands retain
transport control; no second player is created. Active audible rewires use the
same **D11.3.1** activation gate. Authored bus edits affecting active video also
check its positive-gain intent, including when a muted dry bus has no DSP tail.
Early denied network play with `preload=none`
preserves its visible `NotAllowedError` while readiness is polled.

A retained DOM element keeps its selected decoder, cursor and route ticket
when detached; removal pauses playback, and reattachment preserves them.
Source reload/replacement, native disposal and document teardown retire the
ticket and release route references (**D5.5.2 / D6.3.3**). Bus deletion is fenced
by ready/paused references. Graph-owner removal faults ready, paused and playing
providers with `route_lost`; no default output takes over. Default detach can
release the reference, but playback requires source reload after the fault.
The shared finite-media/PCM limits above still apply. Prepared video snapshots
join offline jobs below. Other video backends and physical-device/deadline
qualification remain pending.

### Native Scene3D offline audio render jobs

`runtime.audio_render_open(node, request)` creates an explicit output-free native
job (**D11.3.1 / D9.5.6**, proposal §9.2). Supply the current scene `session`,
an output length `frames`, and clip `events`, prepared `providers`, or both.
Clip events use the cue schema above; source/clip identities come from
`audio_snapshot`. Optional `scene_time` (default
0) and positive finite `time_scale` (default 1) define the private scene-clock
anchor. Audio-frame events count from zero; scene events map through that anchor.
Deadlines must fit within the selected interval, including its final boundary.

A `providers` entry contains `{provider, token, position?, bus?}`. Kinds are
`midi`, `speech` and `video`; tickets identify an existing prepared provider in
this document. `position` defaults to zero and selects a fresh source seek;
`bus` defaults to `master` in the copied scene graph. Position must fit that
provider's finite duration. Foreign/stale, loading/rendering, cancelled and
failed providers reject. Video requires the macOS native backend. Speech uses
an already completed engine render; opening a job does not initiate synthesis
or resume its live utterance.

Before a job becomes ready, each provider materializes an owned stereo 48-kHz
PCM interval of the requested output length on the control thread. MIDI copies
volume/speed/mute/solo into a fresh synth/seek state; speech copies normalized,
gain-applied engine PCM; video reuses its timestamp-gap/overlap, mute/volume and
loop kernel with an independent decoder. At the copied interval’s input EOF,
non-looping sources contribute silence through the remaining output frames.
MIDI's legacy document reverb send is outside this copied bus graph; named
inserts/sends provide the job's effects. Readers use fresh decoder, synth/filter
and DSP histories. Later live controls, source close/reload/removal, or device
loss cannot alter admitted PCM.

Provider intervals start at private audio frame zero through the existing cue
kernel. Job cue history includes their starts, with identities private to that
job. Combined clip/provider sources are limited to 32, and explicit clip events
plus provider starts to 64. Omit `events` or supply an empty array for a
provider-only job. Each provider snapshot reserves `frames * 8` PCM bytes plus
reader metadata within the existing document PCM/reader limits; output storage
has its separate limits below. Preparation is finite and synchronous; explicit
render steps remain capped at 4096 frames. Physical/release latency and peak
memory qualification remain open (**D5.5.2 / D6.3.3 / D11.3.1**).

Opening copies the referenced authored audio descriptions and current selected
native poses, listener and bus/insert/send graph into private readers and fresh
DSP histories. It publishes no live source/graph edits. In particular, a network
document may export a positive-gain authored edit while its admitted live player
remains muted and unactivated. Job cue timing and opt-in pitch-following use
the job's virtual scale. Source/clip identity changes before admission reject
the plan. Later authored edits, asset replacement/drop, live pause/seek/reset,
device mixing and device retirement do not alter admitted job output. Source
and listener poses/parameters remain frozen for that job; future scene-motion
replay is not part of this profile.

| Procedure | Request and behavior |
|-----------|----------------------|
| `audio_render_open(node, request)` | Returns a job snapshot in `ready` state. |
| `audio_render_step(node, request)` | `{session, job, frames}` advances at most 4096 frames; omitted `frames` means 4096. Stops at the selected output length. A completed job stays complete on another step. |
| `audio_render_snapshot(node, request)` | `{session, job}` reads copied progress, bounds, diagnostics and cue receipts. |
| `audio_render_read(node, request)` | `{session, job, offset, frames}` copies up to 4096 produced stereo frames as little-endian float32 `data`; offset defaults to zero. `format:"wav_header"` instead returns a 58-byte IEEE-float WAV header with its `fact` count. |
| `audio_render_file(node, request, target)` | Streams a completed job's header and bounded PCM chunks through ordinary `output` write/append operations (**S12.1.1v3**). It adds no filesystem authority. |
| `audio_render_cancel(node, request)` | Releases readers, DSP, clock and output storage, retaining a `cancelled` snapshot until close. |
| `audio_render_close(node, request)` | Releases the handle and all remaining job storage; later use fails. |

Step/read requests contain exact bounded integral counts; JavaScript Number
integers are accepted within those bounds. Session/job identities retain the
exact counter/BigInt rules (**S4.9.1**). A snapshot contains `session`, `job`,
`state`, `frames`, `rendered`, reserved `bytes`, `sample_rate:48000`, `channels:2`,
`diagnostic`, `pending` and `history`. States are `ready`, `running`, `complete`,
`cancelled` and `error`. Read packets contain copied `job`, `offset`, `frames`
and `data`; a header packet reports zero PCM frames. Result buffers survive
job close. Reads reject ranges beyond produced frames.

Each document retains at most four job handles, 64 MiB of output per job and
128 MiB across jobs. The existing 128 decoded-player/512-MiB PCM limits, 96
shared voice records, submix table and 8-MiB graph bound also apply. Admission
is atomic; quota failure preserves live playback and earlier jobs. Each plan
references at most 32 combined clip/provider sources. Completion retires readers/DSP while retaining
output; cancellation also releases output, and close releases the handle slot.
Scene removal or document teardown closes its jobs. No job advances from the
device callback or ordinary frame polling; callers explicitly step it. The
host-wide offline playback backend retains audible-output gating.

Native jobs reuse the submix, cue and DSP kernel with explicit reader ownership
(**D5.5.2 / D6.3.3**). File output uses the existing IO provider; microphone
acquisition uses the separate input permission below. Release
deadlines/peak memory, cross-platform execution and broader combined-scene
qualification remain M12 work.

### Explicit microphone capture

**D11.3.2** requires an explicit procedure plus both an OS permission and a
host grant for the requesting document. `view --microphone document.html`
grants the initial document; it does not start capture or grant audible output.
Embedded hosts use `radiant_capture_grant(document, granted)`. Child frames and
navigation never inherit this grant. The host owns grant UI; request fields
such as `granted` cannot grant permission. macOS supplies the native input
backend and the CLI embeds its microphone purpose string. Other platforms
report `unavailable` until a host supplies an OS-authorized native backend.

`dom.audio_capture(owner, request)` is a procedure returning a copied map or
`null` for an invalid operation/ticket. It shares the existing host waist
(**D9.5.6**) and supports:

| Request | Behavior |
|---|---|
| `{op: "open"}` | Mint a document-local ticket; request OS consent only after the host grant. A second pending/active acquisition is rejected. |
| `{op: "status", token: ticket}` | Poll permission/device state and copy diagnostics. |
| `{op: "read", token: ticket, frames: 4096}` | Copy up to 4096 contiguous frames into `data`, an interleaved stereo `f32le` binary at 48000 Hz. Empty reads return an empty binary. |
| `{op: "monitor", token: ticket, gain: 0.5}` | Enable separate dry monitoring through the document submix; positive gain requires **D11.3.1** output permission. Zero disables monitoring. Gain is finite and within `[0,1]`. |
| `{op: "stop", token: ticket}` | Retire the device and monitor, release native PCM and keep terminal diagnostics. |

States are `denied`, `pending`, `capturing`, `unavailable`, `stopped`, `revoked`,
`lost` or `error`. A ticket is valid only for its owning document's latest
acquisition. Tokens and sample counters cross JS as exact BigInts (**S4.9.1**).
JS can inspect the returned binary with `new Uint8Array(packet.data)`.
`first_frame` identifies the first copied sample and `next_frame` the device
callback timeline. `start_ms` is the document-clock acquisition anchor, not a
hardware presentation timestamp. Polling meters/counters is advisory while
the producer runs. `captured` counts delivered callback frames; `dropped`
counts queue overflow, callback-limit discards and explicit timestamp gaps.

Recording and monitoring have independent two-second bounded queues, charged
against the existing document PCM/reader quota. Each callback processes at most
4096 frames without allocating, waiting or calling scripts/GC. Overflow drops
new input and records its count; reads stop at timestamp gaps instead of
inventing silence. Monitoring starts off, and `monitor_dropped` reports its
independent overflow. Output-device loss disables monitoring while recording
continues; `monitor_lost` requires an explicit re-enable. Input-device loss,
OS/host revocation, stop and document teardown join the producer before freeing
queues (**D5.5.2 / D6.3.3**); unread native PCM is discarded. Source code keeps
only copied records/binaries. Hardware timing and physical permission/hotplug
qualification remain outside this profile.

An explicitly acquired pending/active capture also joins the shared scene
provider seam with `provider: "capture"`. Use `audio_provider_route` with its
capture `token`, current `expected` route, scene `session`, and a bus's exact
`identity`; `audio_provider_read` copies the resulting route, validity and loss.
The route applies to monitoring through the bus's gain/mute/solo, sends and
ordered effects. It never changes raw recording PCM or advances either reader.
Preparation while permission is pending opens no device. Positive monitoring,
audible rewiring/default detachment, and bus edits affecting a positive monitor
retain **D11.3.1**; a host input grant under **D11.3.2** cannot authorize output.

The route remains pinned while monitoring is off, so removing a retained bus
rejects until explicit detach, rebind or capture close. Graph-owner loss silences
monitoring with `route_lost`, retains a queryable stale route, and leaves input
recording active. Explicitly rebind/detach and re-enable monitoring to recover.
Output-device loss similarly disables monitoring while keeping its valid bus
binding and recording. Stop, input loss, denial, revocation and document teardown
release the route reference before PCM/quota ownership retires. Stale, foreign,
terminal and over-capacity route requests preserve the existing binding.
Capture `position` reports the input sample timeline in seconds; exact frame
counters remain available from `dom.audio_capture`. Live capture is explicitly
rejected as a prepared offline-job provider; export never consumes recording
data or implicitly acquires input (**D5.5.2 / D6.3.3 / S4.9.1**).

### Native CPU/GPU mesh particles

`runtime.particle_snapshot(node)` reads the native emitter service through
`radiant-dom`. Declare `particle-emitter` directly under a scene, group or rigid
body. The bounded CPU and GPU profiles render basic meshes or planar billboards using native
instanced vertex fetch; geometry/material references and inline definitions work
as for meshes. Opaque, masked and center-sorted transparent presentation are
supported. Shader, lit and deformed particle meshes reject. Builders/presets
remain M13 (**S12.1.1v3 / S12.1.5v2 / D5.5.2 / D9.5.6**).

```html
<particle-emitter id="sparks" backend="cpu" space="local" seed="42"
    shape="cone" angle="30" speed="3" capacity="256" burst="20" rate="10"
    lifetime="2" lifetime-min="1" gravity="0 -9.81 0" size=".05">
  <geometry type="box" size="1 1 1"></geometry>
  <material type="basic" color="#ffcc33"></material>
  <particle-size-curve times="0 .5 1" values="1 .5 0"></particle-size-curve>
  <particle-rotation-curve times="0 1" values="0 0 0 0 0 6.2831853"></particle-rotation-curve>
  <particle-noise strength=".5 1 .5" frequency="2"></particle-noise>
</particle-emitter>
```

| Field | Native profile |
|---|---|
| `backend`, `space` | `cpu` (default) or `gpu`; `space="local"` (default) follows the current emitter transform, while `world` retains each birth frame. |
| `presentation` | `mesh` (default) or `billboard`; billboard geometry lies in its local XY plane and faces the current viewing camera. |
| `id`, `seed` | Unique nonempty ID, at most 127 bytes; exact unsigned seed (default 0). String seeds use complete unsigned decimals, including all 64 bits. |
| `capacity`, `overflow` | Default 256, range 1–16384; `drop-new` (default), `recycle-oldest`, or `error`. At most 32 emitters and 65536 stored particles per scene. |
| `rate`, `burst` | Births per second (default 10) with carried fractional credit; initial/per-cycle burst (default 0, at most 4096). |
| `duration`, `loop` | Duration 0 means unbounded emission; positive durations stop or repeat (`loop` defaults true). |
| `emitting` | Defaults true; false stops new births while live particles continue aging. Scene runtime pause/step/play controls the shared clock. |
| `lifetime`, `lifetime-min` | Positive maximum lifetime (default 1); omitted minimum follows it, otherwise lifetimes are uniformly sampled within the admitted interval. |
| `shape` | `point` (default), `box`, `sphere`, `cone`, or `mesh` (see surface intake below). Box `extent` is full width/height/depth (default `1 1 1`); sphere `radius` defaults 1 and samples volume uniformly. |
| `velocity`, `spread` | Initial local velocity (default zero), plus an isotropic velocity vector with magnitude `spread` (default 0). |
| `angle`, `speed` | Cone velocity points around local +Y within this half-angle in degrees (default 30, at most 180), with added speed (default 1). |
| `gravity`, `drag` | Acceleration in simulation space (default `0 -9.81 0`) and nonnegative exponential velocity drag (default 0); shared fixed ticks use the analytical drag/gravity integral. |
| `size`, `particle-size-curve` | Positive initial uniform mesh scale (default 1), multiplied by the current linear over-life curve. At most one curve with 2–16 strictly increasing times from 0 to 1 and matching nonnegative values; default multiplier 1. Zero-sized particles remain alive but draw no instance. |
| `particle-rotation-curve` | Optional linear Euler XYZ rotation in radians, default zero. One curve with 2–16 strictly increasing normalized times from 0 to 1, and three signed values per key (absolute value at most 1000000). Values interpolate directly without angle wrapping; `0` to `6.2831853` makes one full turn. The mesh transform is birth basis × `Rx * Ry * Rz` × uniform size; particle position, velocity and sphere collision radius are unaffected. |
| `particle-velocity-curve` | Optional three-axis velocity offset in simulation-space units per second, default zero; one curve with 2–16 strictly increasing normalized times from 0 to 1, with three signed values per key in [-1000000,1000000]. See the integration contract below. |

#### Particle billboard presentation

`presentation="billboard"` gives CPU/GPU planar geometry a camera-facing world
matrix. Geometry/material references, vertex colors, UV0/UV1, material UV
transforms, texture sheets and color/opacity curves retain the shared mesh
paths. Native positions, velocities, age, spawn sequence and collision radii
are unchanged (**D10.1.7 / D5.5.2 / D6.3.3 / D9.5.6**). For example:

```html
<particle-emitter id="cutouts" backend="gpu" presentation="billboard"
    capacity="256" burst="20" rate="0" lifetime="2" gravity="0 0 0" size=".1">
  <geometry type="plane" size="1 1 1"></geometry>
  <material type="basic" color="#ffffff" alpha-mode="mask" side="double"></material>
  <particle-rotation-curve times="0 1" values="0 0 0 0 0 3.1415927"></particle-rotation-curve>
</particle-emitter>
```

Primitive planes and planar buffer triangles work; all source Z coordinates
must be zero. The basic-material/deformation/instancing restrictions above
still apply. Dimensions are world units: inherited X/Y axis lengths multiply
sampled particle size and the geometry's coordinates. Local particles use the
current emitter basis lengths; world particles retain their birth basis
lengths. Mirrored bases retain unsigned dimensions and camera-facing winding.
The committed rotation curve's Z component supplies in-plane roll; X/Y values
remain in copied rotation samples without tilting the billboard. The current
look-at camera provides an orthonormal view, including under transformed
camera parents. Perspective distance affects projected size normally.

Compatible presentation switches apply upon admission, retaining simulation
and adding no GPU checkpoint. Size/rotation/color/atlas curve edits keep their
existing next-successful-tick publication rules. Camera edits update the draw
without advancing the native clock. Templates, authored replay and context
recovery preserve the admitted presentation and committed pose. Emitter
snapshots copy `presentation` as `mesh` or `billboard`.

Camera-independent sphere bounds enclose roll and camera reorientation, even
under nonuniform emitter scale. Authored bounds retain their explicit override
and must cover the camera-facing footprint. Opaque/masked billboards use the viewing
camera's orientation in shadow draws, with shared atlas/alpha sampling; camera
edits invalidate their shadow maps. Double-sided materials cover both faces of
thin planar casters. Transparent billboards retain per-emitter center ordering
and the existing no-shadow profile.

CPU native storage reserves three floats per capacity slot for committed
scale/roll. Rendering packs these into spare affine components of the retained
80-byte palette record and restores the matrix components before use. GPU
draws read scale/roll from existing birth and pose texels; update targets stay
288 bytes per padded slot. Camera-only attribute edits do not rebuild geometry, add ordinary
particle readback or create a computed CPU GPU-motion mirror
(**D5.5.2 / D9.5.6**).

#### Native particle spawn commands

`runtime.particle_commands(node, batch)` queues explicit CPU/GPU births through
the existing shared scene command queue. Native payloads own their copied
numbers and identities; later edits to the submitted map cannot change them.
There is no script callback inside the particle update. Event-to-effect rules
and preset composition remain M13 (**D5.5.2 / D6.3.3 / D9.5.6**).

Take `session`, `generation` and `service_generation` from
`runtime.particle_snapshot(node)`, and the target's `id`/`identity` from its
emitter row. Supply a strictly increasing shared command `sequence` and an
unsampled future `tick` within 100000 ticks. The ordinary runtime snapshot
provides `command_sequence` and the current tick. Keep opaque identities as
exact Lambda integers or the JavaScript BigInts returned by the bridge.
The batch shape is:

```lambda
{
  session: particles.session, generation: particles.generation,
  service_generation: particles.service_generation,
  sequence: next_sequence, tick: target_tick,
  commands: [{op: "spawn", emitter: target.id, identity: target.identity,
              count: 12, depth: 1, position: [0, 1, 0], velocity: [0, 2, 0]}]
}
```

A batch contains 1–128 `spawn` records. `count` is a whole number from 1 to 4096;
`depth` defaults to zero and must be a whole number from 0 to 8. Optional
`position` replaces this request's birth origin in the target's simulation
space; shape offsets still use the emitter's birth basis. Omission preserves
the authored origin. Optional `velocity` adds simulation-space initial dynamic
velocity after that basis, defaulting to zero. Each vector has three finite
float coordinates in [-1000000,1000000]. World emitters therefore accept copied
world contact positions directly; local emitters use local origins and axes.
These overrides affect new births only. Sampled over-life velocity remains a
separate transport component.

Explicit requests can spawn while authored `emitting` is false or its duration
has ended. They do not unpause the shared clock. The queue can wake an otherwise
idle automatic particle service. Due records preserve target-tick order and
submission order within a tick. Authored rate/burst attempts come first within
each emitter, followed by its explicit records in queue order. All attempts
retain the emitter's seeded sequence, including drop/recycle behavior.

The scene shares the existing 32 pending command-batch limit. All due particle
records together admit at most 128 requests and 4096 authored-plus-explicit
spawn attempts for the next fixed tick. A due batch that exceeds these limits
rejects atomically, retaining earlier accepted requests. Existing emitter
capacity policies apply during simulation. A later `error` capacity policy,
collision limit or failed peer can fault that phase without publishing births
or consuming admitted requests. Shared command receipts describe admission,
rather than completed simulation; particle snapshots expose
`pending_spawn_requests` and `pending_spawn_count` separately.

Native owner/source generation changes invalidate future tickets. Emitter-
description changes also discard admitted requests; structural emitter changes
replace their native identities. Equivalent
templates and graphics loss preserve compatible identities and queued work.
Authored seek/reset discards command-created births and pending requests.
Copied particle samples, trail queries and CPU contacts expose birth `depth` for
later package rules. Depth is caller-supplied bounded provenance; no recursive
rule evaluator or automatic event mapping runs natively. GPU origins and added
velocity remain in immutable birth recipes, with no ordinary particle readback
or CPU motion mirror. Timing follows the Scene3D proposal's §3.2/M6; copied
ownership and template authority retain **D5.5.2 / D6.3.3 / D9.5.6 /
S12.1.5v2**. Capabilities report `particles_spawn_commands` and
`particles_child_queues` for this bounded profile.

#### Bounded native CPU/GPU particle trails

Add one leaf `particle-trail` to a CPU or GPU emitter for camera-facing ribbon history.
The head keeps its mesh/billboard and simulation contract. A successful fixed
update appends one post-collision sample per living particle, including an
age-zero birth sample. A zero-size living head can keep a visible trail.
Expiry, collision kill and recycling end that head's trail; retired tails and
effect composition remain outside this profile. Capabilities report
`particles_cpu_trails=true`, `particles_gpu_trails=true` and `particles_trails=true`
(**D10.1.7 / D5.5.2 / D9.5.6**).

```html
<particle-trail segments="16" lifetime=".5" width=".05" fade="true"></particle-trail>
```

`segments` is a whole number from 1 to 63 (default 16), reserving 2–64 points
per physical particle slot. `lifetime` is the visible time window and `width`
is the full ribbon width; both are positive finite floats at most 1000000,
defaulting to `.5` and `.05`. `fade` defaults true and multiplies each endpoint's
alpha by `1 - sample_age / lifetime`. The oldest segment clips to the exact
window boundary; near-plane clipping and width expansion share the line renderer.
Local trails use simulation coordinates and follow the current emitter frame;
world trails retain sampled world positions and a width in world units.
Local width scales by the largest current emitter basis-column length.

Ribbons use the head's committed color, opacity and atlas frame with the same
basic material and UV transforms. Generated U is relative sample age divided
by the window, V spans the width, and UV0/UV1 match. Head styles update at the
next successful tick. Transparent ribbons sort by head centers within their
emitter; segment intersections retain the existing transparency limits.
Ribbons neither cast nor receive shadows; heads retain their shadow behavior.
Historical centers and the camera-facing width participate in culling.
GPU bounds retain a bounded ring of conservative native center envelopes,
including zero-size heads, without copying device positions. Authored bounds
override these bounds and must enclose the entire ribbon.

Width/window/fade edits preserve retained points. Changing `segments`, adding
or removing the child resets derived trail history while preserving head
position, velocity, age and sequence. Whole-particle-phase failure publishes
neither new head state nor trail samples. Templates and graphics recovery
retain native history; seek/reset reconstruct it from admitted authored input
(**S12.1.5v2 / D5.5.2 / D6.3.3**).

`runtime.particle_trail(node, emitter_id, index=0)` copies one living particle's
newest-first history, with at most 64 `{position, age}` records. `age` is time
since the sample, rather than the head's birth age. The result includes emitter,
sequence, simulation space, configuration and generation metadata. It includes
retained points outside the current window, so increasing the window can reveal
them; changing the returned values cannot edit native history. Whole JavaScript
numbers are accepted as bounded indices. Missing emitters, disabled trails and
nonliving indices return errors (**D5.5.2 / D6.3.3 / D9.5.6**).

At most 1048576 padded point slots are admitted per scene. Storage rounds
capacity up to rows of 64 heads (or one shorter row), with at most 4096 point
texels per row. CPU point rings cost 32 native bytes plus 16 device bytes per
padded point;
GPU point rings cost 32 device bytes plus 32 native bytes of immutable checkpoint
anchors per padded point. Bounded style/order/slot/template storage is additional.
`storage_bytes` includes native trail ownership; GPU `gpu_bytes` also includes
both device point images. Emitter snapshots copy `trail_segments` and
`trail_point_slots`. This is a storage/work bound, not a release-performance or
peak-memory qualification claim.

GPU updates append candidate positions in a third fragment pass alongside the
existing pose/velocity passes. Head poses and trail rings publish together after
whole-phase admission. Ordinary updates, draws and context recovery perform no
particle readback and keep no computed CPU motion mirror. Explicit trail queries
read one bounded device row; explicit motion-control edits checkpoint immutable
trail anchors along with head motion. Width/window/fade/segment edits add no
motion checkpoint. Analytical motion retains at most the configured point count
of actual tick partitions; stateful noise/velocity/collision keeps its existing
4096-tick motion journal. Its exhaustion faults the clock while preserving
committed trails. Recovery replays retained partitions and ring heads over the
anchors, including births, recycled slots and mixed row layouts. CPU/GPU point
precision follows their existing float profiles (**D10.1.7 / D5.5.2 / D5.5.3 /
D6.3.3 / D9.5.6**).

#### Particle velocity-over-life curves

`particle-velocity-curve` adds authored transport to native dynamic velocity;
it neither accumulates its full value every tick nor replaces force-driven
motion or collision impulses (**D10.1.7 / D5.5.2 / D6.3.3**). For example:

```html
<particle-velocity-curve times="0 .25 1" values="0 0 0 2 0 0 -1 0 0"></particle-velocity-curve>
```

The vector curve interpolates linearly at normalized age `age/lifetime`.
Every fixed update holds its starting-age value `C` through free motion and
collision remainder. With no collision, displacement is the existing
gravity/noise/drag integral of dynamic velocity plus `C*dt`; only dynamic
velocity decays under drag. At the committed age, copied `velocity` is dynamic
velocity plus the curve sampled at that age. New births report the age-zero
offset immediately. Consequently transport displacement depends on the fixed
tick partition; it does not integrate across intervening keys continuously.
The recovery journal retains that actual partition.

Offsets use the selected simulation-space axes: local emitters use local axes,
and world emitters use world axes independently of frozen affine birth bases.
Collision sweeps and contact velocities use the combined motion. Bounce/slide
impulses change retained dynamic velocity; the curve does not erase those
impulses on the next update. Resting/sliding contact removes inward force and
transport for the remainder of that update. GPU analytic stopped states report
zero velocity. Conservative bounds include the curve envelope and its full
variation when impacts redirect motion. Stateful position accumulation adds
roundoff padding derived from the retained update count; its bound does not
assume a single closed-form float evaluation.

Compatible curve edits/removal apply at the next successful fixed tick,
retaining position and dynamic velocity. GPU edits checkpoint dynamic device
motion once through the explicit-edit path. Before that tick, copied samples
and context recovery retain committed controls; authored replay uses current
admitted controls, and equivalent templates retain state. Nonzero GPU offsets
use the same bounded 4096-tick native recovery journal as noise/analytic
collision; exhausted history rejects publication. All-zero curves retain the
ordinary analytical path. GPU velocity targets store dynamic velocity;
explicit copied queries add the committed authored offset through the shared
native sampler. No ordinary-tick particle readback, computed CPU GPU-motion
mirror or target growth is added: padded update storage stays 288 bytes per
slot. The two native record arrays retain an additional three-float dynamic
velocity per slot; `storage_bytes` includes actual padding and history cost
(**D5.5.2 / D6.3.3 / D9.5.6**).

`particle-noise` adds bounded smooth temporal value-noise acceleration in
simulation-space axes (**D10.1.7 / D5.5.2 / D6.3.3**). At most one child is
accepted. `strength` gives three nonnegative acceleration amplitudes (default
`1 1 1`, each at most 1000000); `frequency` gives lattice intervals per second
of particle age (default 1, range 0–1024). Frequency times the maximum lifetime,
including retained particles from before a compatible lifetime edit, must not
exceed 65535. Frequency zero gives a constant keyed acceleration per particle.
An absent child or zero strength disables noise.

The kernel keys draws by seed, attempted spawn sequence, lattice interval and
axis channels 9–11, using the shared SplitMix fold. It interpolates signed draws
with cubic smoothstep, samples at each fixed update's starting age, and holds
that acceleration through gravity/drag integration and collision remainder.
CPU and GPU noise draws both use the top 24 bits; float age/interpolation and
integration differences remain observable. This temporal field has no spatial
or curl dependency. Changing an emitter transform does not rotate world-space
noise. Conservative GPU bounds include the full acceleration envelope.

Noise edits take effect on the next update without restarting live particles.
GPU insertion, removal, strength or frequency edits checkpoint device motion
once through the existing explicit-edit path. Noise-only GPU motion uses the
same 4096-tick native recovery journal as analytic collision, with no ordinary
tick readback or computed CPU motion mirror. Context recovery replays the
retained tick partition; expired/recycled slots release old history. Exhausted
history rejects the whole particle phase without publishing a partial tick.
The existing `gpu_history_ticks` and storage snapshots expose these costs.

#### Particle color, opacity and presentation ordering

`particle-color-curve` contains three linear RGB multipliers per normalized
lifetime key; `particle-opacity-curve` contains one opacity multiplier. Both
accept 2–16 strictly increasing `times` from 0 to 1, matching `values` in [0,1],
and at most one child of each kind. Defaults are white RGB and opacity one.
They reuse the same linear sampling as size/rotation curves. For example:

```html
<particle-color-curve times="0 .5 1" values="1 0 0 0 1 0 0 0 1"></particle-color-curve>
<particle-opacity-curve times="0 1" values="1 0"></particle-opacity-curve>
```

The sampled RGBA multiplies material, vertex and texture factors. RGB values
are linear-light multipliers; the ordinary display target handles sRGB
encoding after blending. Material `alpha-mode="opaque"` ignores final alpha;
`mask` discards below its cutoff and draws surviving fragments opaquely,
including alpha-tested shadows. Otherwise an opacity curve containing a value
below one selects transparent presentation. Color/opacity edits retain motion,
sequence and age, taking effect on the next successful fixed tick without a
GPU motion checkpoint. Copied sample `color` is RGBA before material factors
(**D10.1.7 / D5.5.2 / D6.3.3**).

Transparent particles sort back-to-front by simulated center in the current
view, separately inside each emitter. Equal depths use current palette/device
slot order. Sorting changes presentation indices only. Cross-emitter and
particle/ordinary-mesh intersections retain draw-record ordering; this profile
does not globally interleave individual particles or sort intersecting
fragments. Transparent particles do not cast shadows; use masked coverage for
shadowed cutouts. Ordinary opaque depth and existing tone/output behavior are
preserved.

The same device bitonic network reads native CPU matrix/color palettes or GPU
poses. It performs no particle readback and retains no computed GPU motion
mirror. With `N` the next power of two of capacity, sorting a visible emitter
with at least two drawn particles uses `1 + log2(N)*(log2(N)+1)/2` fragment
passes: at most 106 at capacity 16384. Two RGBA32F index targets use `32*N`
bytes. CPU presentation also uploads one retained five-texel matrix/RGBA
palette (80 bytes per padded slot); the instance stream stores a physical and
draw index (eight bytes per slot). These renderer resources are separate from
native simulation storage. `Scene3dStats.particle_sort_passes` counts device
ordering passes; particle snapshot `gpu_passes` continues to count update
kernels. Built-in ordering uses the scene context's native program ownership,
not the authored shader quota (**D5.5.2 / D9.5.6**).

Sort targets and palettes are preallocated at projection and reused across
ordinary frames. Context loss rebuilds them from retained native state; camera
changes reorder presentation without advancing simulation. Capacity bounds
live-particle overdraw and network work, but does not promise release frame
time for high overdraw or complex geometry. Presets and effect policy remain
M13.

#### Particle texture sheets

One optional `particle-sheet` child selects atlas cells on CPU/GPU from fixed
particle age (**D10.1.7 / D5.5.2 / D6.3.3**). Cells use top-left row-major
indices, starting at zero. The default grid is one cell, preserving ordinary
material UVs.

| Attribute | Contract |
|---|---|
| `columns`, `rows` | Integers 1–256, default 1; at most 65536 cells. |
| `first`, `count` | First cell (default 0) and positive contiguous range length (default all remaining cells); the range must fit the grid. |
| `mode` | `lifetime` (default) or `fps`. |
| `cycles` | Lifetime mode only: cycles through the range per lifetime, default 1. |
| `fps` | FPS mode only: frames advanced per second of particle age, default 10. |
| `loop` | Wrap inside the range (default true); false clamps at its final cell. |
| `random-start` | Choose a keyed starting offset inside the range, default false. |
| `inset` | Cell-relative inset on all four sides, in [0,.5), default 0. |

For example, this visits a 2×2 atlas at ten frames per second:

```html
<particle-sheet columns="2" rows="2" mode="fps" fps="10"></particle-sheet>
```

Rates are finite in [0,1024]; explicitly supplying the other mode's rate
attribute rejects. In the float arithmetic shared by CPU/GPU, lifetime mode
requires `cycles*count <= 16777215`, and FPS mode requires
`fps*lifetime <= 16777215` for both new and retained particles. An advancing
multi-cell lifetime sheet also requires a lifetime that remains positive as
a float. Static/single-cell sheets preserve existing CPU sub-float lifetimes.

The frame is `first + floor(age*fps)` or
`first + floor((age/lifetime)*(cycles*count))`, with a keyed offset added before
wrapping/clamping within `count`. Zero rate holds that offset. Random starts
use the seed, attempted birth sequence and channel 9 with the same top-24-bit
SplitMix draw on both backends; dropped/recycled slots do not shift later
keys. Float age and phase rounding remain observable at frame boundaries.

The common material UV helper selects UV0/UV1 and applies its material
transform before mapping into the chosen atlas cell and applying inset.
Visible and alpha-tested shadow draws share that mapping. Texture wrapping
still applies to the complete atlas: transformed UVs outside a cell may reach
another cell. Inset or authored gutters control filtering bleed; automatic
tile/mipmap isolation and cross-frame blending are outside this profile.

Compatible sheet edits publish frame and grid at the next successful fixed
tick, preserving motion and adding no GPU checkpoint. Before then, rendering
and context recovery retain committed controls. Authored replay uses current
admitted controls; equivalent templates retain state. CPU native storage adds
one frame integer per capacity slot; its existing 80-byte rendering palette
packs frame into an unused affine lane restored before transformation. GPU
poses use the rotation texel's unused fourth component, keeping update storage
at 288 bytes per padded slot. Ordinary GPU ticks and rendering add no particle
readback (**D5.5.2 / D6.3.3 / D9.5.6**).

Scalar controls are finite and bounded by 1000000, with the stricter ranges
above. Each scene tick admits at most 4096 attempted births, including drops and
recycled slots. Random draws depend on seed, attempted spawn sequence and
channel, so dropping one particle does not perturb later draws. Existing
particles age/expire before new births; births commit at the tick end with age
zero. Initial and crossed-cycle bursts share that boundary quantization.
Idle/finished emitters park their local clocks independently of active peers.
Motion, lifetime and size checks stage every emitter before any particle state
publishes. Capacity `error`, exhausted work/counters or nonfinite motion fault
the particle stage explicitly; the snapshot includes a diagnostic. This is
particle-stage atomicity; earlier physics work is outside that transaction.

Particles run after physics on the shared coordinator. Active emitters require
fixed ticks rather than animation-only fractional sampling (**D10.1.7**).
Storage is preallocated to capacity. CPU instances update one retained matrix
buffer's live prefix. GPU instances fetch position/scale and rotation directly from float
targets using a retained slot-ID attribute, including shadow draws. Culling never
suspends simulation. Snapshot `center_low`/`center_high` bound rendered centers
(exact CPU union, conservative GPU envelope); the renderer includes geometry and
maximum curve scale in motion bounds, or uses authored `bounds`.

Compatible edits retain particles, local time, fractional credit and sequences;
rate/shape/velocity/lifetime/initial-size edits affect subsequent births.
Gravity/drag, size, rotation, color, opacity and velocity curves and texture sheets apply on the next live update. GPU gravity/drag and velocity-curve edits
checkpoint existing device positions/velocities once, preserving continuity; this
explicit source-edit readback is separate from ordinary ticks. Capacity, seed, simulation-space or
backend changes restart that emitter. Equivalent template rebuilds follow existing
node correspondence; removals retire storage, and context recovery rebuilds
GPU resources from retained native metadata/birth descriptions and motion anchors,
without advancing time or requiring a readback. Private candidates also follow
context loss; retiring their scene owner invalidates their graphics borrows.
Scene seek/reset privately reconstructs
current authored descriptions on fixed ticks (at most 100000), then publishes
only an admitted reconstruction. Stale native replay candidates reject.
Recorded control history remains separate M13 work.

Snapshots copy scene/session/service generations, snapshot generation, tick,
automatic mode, diagnostics and emitter counts/counters/storage, local time,
center bounds and at most 64 samples per emitter. `samples_truncated` reports
omission. Emitter snapshots include `space`, admitted `presentation` and committed `sheet_grid` as
`[columns, rows, inset]`. Samples contain sequence, age, lifetime,
position/velocity in that simulation space, size, local `rotation` radians and
RGBA `color` multipliers before material factors, plus numeric atlas `frame`
(0–65535, an ordinary integer in JS). Unsigned counters remain exact (BigInts in JS, **S4.9.1**); e.g. use
`String(snapshot.emitters[0].sequence)`. Returned data owns its values and can be
edited without changing the native owner (**D5.5.2 / D6.3.3**).

GPU spawning, analytical gravity/drag integration, all five curve kinds and texture-sheet sampling execute in
GLSL 330 fragment passes on RGBA32F targets; compute shaders are unnecessary.
CPU metadata determines birth attempts, admission, lifetime expiry and exact
unsigned sequence keys. It retains no computed GPU motion mirror. Device draws
use the top 24 bits of the same SplitMix64 random key; CPU draws use 53 bits.
Float arithmetic and transcendental operations can differ across devices and
from CPU output. Cross-device bit-exact poses/pixels are not promised.

Ordinary GPU ticks perform no particle readback. `particle_snapshot` explicitly
reads only rows needed by its first 64 samples; motion checkpoints can read every
active row. Snapshots include `backend`, `gpu_readbacks` (pose/velocity row
transfers), `gpu_passes`, `gpu_recoveries` and `gpu_bytes`, with zero GPU counters
for CPU emitters. `storage_bytes` includes native metadata and readback scratch.
Each GPU emitter uses six float targets: metadata, nine-texel birth descriptions,
and two pose outputs (three texels each: position/scale, rotation and RGBA) plus two velocity
outputs. Padded device storage is 288
bytes per slot; native and mesh buffers are accounted separately. Private replay
stages its own device state before publication, so peak usage includes that
candidate. Functional quotas do not imply a release frame-time qualification.

Capabilities `particles_cpu_mesh`, `particles_gpu`, `particles_gpu_mesh`,
`particles_seeded`, `particles_size_curves`, `particles_rotation_curves`,
`particles_world_space`, `particles_mesh_surface`, `particles_noise`,
`particles_color_curves`, `particles_opacity_curves`, `particles_depth_ordering`,
`particles_texture_sheet`, `particles_velocity_curves` and `particles_billboards` identify the bounded profiles.
GPU depth collision, trails and child-spawn queues remain M6. `particles_collision` and
`particles_trails` remain false. See the
[GPU component](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md#m6-native-gpu-mesh-emitter-kernels--2026-10-31).

Rotation is evaluated in the particle's local mesh axes, inside its retained
affine world birth frame. CPU matrices and the shared GPU visible/shadow shader
use the same Euler convention. Conservative GPU geometry spheres enclose all
rotations, including geometry offset from its origin. Curves follow current
admitted descriptions on fixed ticks; changing a curve preserves age, lifetime,
sequence and motion. Samples copy device orientation on explicit GPU queries.
Context recovery reconstructs the committed curves/poses without advancing time;
equivalent templates retain them, and authored replay uses current curves.
No angular dynamics or angular collision response is implied by mesh rotation
(**S12.1.5v2 / D10.1.7 / D5.5.2 / D6.3.3 / D9.5.6**).

### Mesh-surface particle emission

Both backends accept `shape="mesh"` with `surface="geometry-id"` referencing one
unambiguous static `geometry` definition in the scene. The emission surface is
separate from the geometry drawn for each particle. Existing box/plane/buffer
intake and ID lookup are shared with native geometry/navigation consumers;
source placement is the emitter's local frame, not a mesh node's placement
(**S12.1.5v2 / D9.1.1–D9.1.3 / D9.5.6**).

```html
<geometry id="canopy" type="plane" size="4 2 1"></geometry>
<particle-emitter id="petals" backend="gpu" shape="mesh" surface="canopy"
    space="world" normal-speed=".5" capacity="1024" rate="100" lifetime="3">
  <geometry type="plane" size=".05 .05 1"></geometry>
  <material type="basic" color="#ffc0cb"></material>
</particle-emitter>
```

Triangles are selected by their local area; barycentric points are uniform within
the selected triangle. Indexed and nonindexed buffers work. Geometric zero-area
faces are omitted; an all-degenerate source rejects. Signed `normal-speed`
(default 0, magnitude at most 1000000) adds velocity along the triangle winding's
local unit face normal, before the ordinary birth-frame transform. Vertex normal
streams do not alter this flat face-normal rule. Shape selection and barycentric
draws use dedicated seed channels 6–8, preserving other birth channels. CPU/GPU
use the same admitted float area table, with their documented 53/24-bit random
precision. World mode transforms the sampled local distribution once; nonuniform
world scale does not reweight the local areas.

Intake admits at most 4096 input faces, 12288 vertices/indices and coordinates
within ±1000000 per component. Deformed sources require an explicitly supplied
static geometry snapshot. Positive area prefixes must remain strictly increasing
in the shared float representation; distributions losing a positive weight to
rounding reject. Tables copy only admitted triangle coordinates, flat normals
and area prefixes, owning them independently of DOM/input/decoder pools
(**D5.5.2 / D6.3.3**).

Compatible surface edits affect subsequent births. Existing particles retain
their original recipes, including through GPU context loss. Up to eight distinct
live/current tables per emitter and 65536 retained triangles per scene are
admitted; equivalent content reuses a table. Old tables retire once no live birth
or current description references them. Exhausted versions, invalid geometry or
scene-budget failure rejects description admission before replacing live state.
Changing away from `mesh` retains earlier mesh births until they expire.

CPU sampling uses the native area table. GPU selection/barycentric sampling and
motion execute in fragment passes; there is no computed CPU birth/motion mirror
or ordinary-tick readback. A mesh GPU emitter uses one additional 512×256 RGBA32F
surface atlas (2097152 bytes), allocated when first needed and retained until
emitter disposal or context loss. Recovery allocates it only if tables remain.
Changed banks upload only their owned padded rows; live banks cannot be reused.
Ordinary births allocate no native per-particle table/storage. Native tables pad
rows to 128 triangles; their storage is included in `storage_bytes` and reported
as `surface_versions`, `surface_triangles`, and `surface_bytes` in copied emitter
snapshots. GPU atlas bytes are included in `gpu_bytes`.

Authored replay reconstructs the current admitted surface, rather than old edit
history. Geometry-dependent candidates reject unadmitted or subsequently changed
source epochs. Private candidates retain their native tables until disposal,
including after the displayed scene retires. Builders/presets and geometry
snapshot factories remain M13. See the
[surface component](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md#m6-native-cpugpu-mesh-surface-emission--2026-10-31).

### World-space particle emission

Both native backends accept `space="world"`. Births sample the emitter after the
completed animation/physics phases of that fixed tick. Shape coordinates, local
initial velocity and the mesh basis take that affine frame once; stored motion
then remains in world space. Later emitter/ancestor movement, rotation or scale
changes affect new births. Gravity is a world vector in this mode; drag is scalar.
No carrier velocity is implicitly added. Mesh size multiplies the retained birth
basis, including nonuniform scale/shear (**D10.1.7 / S12.1.5v2**).

Visible and shadow draws use retained world poses without applying the current
emitter transform again. GPU random births and motion stay on the device; three
additional RGBA birth rows retain the affine frame. Native bounds transform the
whole shape/velocity envelope and include the retained mesh basis. Ordinary
ticks still perform no particle readback and allocate no new per-particle storage.
GPU motion checkpoints preserve world positions/velocities and the birth basis;
context recovery reconstructs both. CPU scene collision uses the same world
coordinates and leaves its radius independent of mesh scale.

Frames must be finite and invertible. One emitter batch currently requires the
same winding across live birth frames; changing handedness while older opposite-
handed particles remain rejects the particle phase explicitly. Fully mirrored
batches are supported. Finite motion with an overflowing mesh basis also rejects
before publication. Wider mixed-winding presentation remains a profile limitation.
Changing `space` restarts that emitter, since retained coordinates have a different
meaning; equivalent template correspondence preserves unchanged-space particles.

World-space replay uses the existing private animation/physics coordinator and
samples each birth on that timeline. It leaves the displayed world untouched.
Like collision-dependent replay, publication uses coordinated scene seek after
checking source, clock, animation and physics/world dependencies; a particle-only
native handoff rejects. Replay reconstructs current authored descriptions, not
later control history. Samples and the copied `space` field obey the same owned
value/precise-root rules as other particle snapshots (**D5.5.2 / D6.3.3 / D9.5.6**).
See the [world-space component](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md#m6-native-world-space-cpugpu-emission--2026-10-31).

### Native CPU particle scene collisions

A CPU mesh emitter can contain one empty `particle-collision` description. It
sweeps an inline sphere through the admitted scene physics world after physics
completes each fixed tick. No particle body/collider is inserted into the world.
Primitives, convex shapes, meshes and heightfields use the shared native query
kernels (**S12.1.5v2 / D10.1.7 / D5.5.2 / D9.5.6**).

```html
<particle-collision profile="scene" response="bounce" radius=".05" skin=".001"
    restitution=".5" friction=".1" bounce-threshold=".1" max-impacts="4"
    cast-iterations="128" membership="4294967295" filter="4294967295"
    sensors="false" ignore-owner="true" initial-overlap="kill"
    events="true" event-capacity="256"></particle-collision>
```

`profile` defaults to `scene`; `response` is `bounce` (default), `slide` or
`kill`. Radius defaults to .01 world units, independent of mesh scale/size
curves. Skin is positive and at most radius, defaulting to `min(.001, radius/10)`.
Restitution and friction are in [0,1]: restitution controls reflected inward
speed, and friction removes that fraction of tangential relative velocity per
impact. Bounce threshold suppresses small rebounds; resting/sliding contacts
remove inward acceleration for the remainder of the tick. Surface response
uses the target's sampled linear/angular velocity. Defaults are restitution .5,
friction 0 and threshold .1. Scalar lengths/speeds are bounded by 1000000.

Queries use two-way 32-bit membership/filter masks, omit sensors by default and
normally exclude the emitter's nearest ancestor rigid body. Local motion is
transformed into the sampled world frame and response is transformed back;
invertible affine emitter frames, including nonuniform scales, are admitted.
Collision paths are piecewise-linear sweeps of analytical gravity/drag endpoints
against sampled targets. Curved-trajectory and future target-motion CCD remain
separate profiles. An initial sphere overlap has no invented normal: it kills
(default), or faults with `initial-overlap="error"`. Births check overlap at age
zero; admitted births killed there count as spawned and release capacity.

At most 1–16 impacts per particle (default 4) and 1–128 cast iterations (default
128) are admitted. Each particle tick shares a 65536-unit query budget, charging
all world colliders (at least one unit) per sweep, plus a separate 65536 tested-
triangle budget. Overflow, nonconvergence, nonfinite motion or retained contact
queue exhaustion rejects the whole particle phase before publication and faults
the coordinator. Earlier physics is outside that transaction. Snapshots expose
`collisions`, `killed`, `collision_queries`, `collision_work`,
`collision_triangles` and `pending_events` per emitter.

Contact queues reserve 256 records per CPU emitter, including emitters without
collision enabled, so compatible collision toggles do not replace live particles.
`event-capacity` admits 1–4096 records per emitter, with a scene reserve limit of
16384 records; storage and staging buffers are preallocated and included in
`storage_bytes`. Changing this capacity restarts that emitter. `events="false"`
disables recording for decorative effects; collision counters still advance.
No script callback executes within the solver.

`runtime.particle_events(node, emitter = null, limit = 256)` copies contact
packets with session/generation/tick metadata and selected emitter queues.
Limit is 1–1024 per emitter; `truncated` and `pending` report unread rows.
Events include monotonic sequence (starting at 1), particle spawn sequence,
tick, query-world identity, numeric body/collider IDs, world center/contact
point/normal, incoming/outgoing velocity, kind (`impact`/`kill`), overlap flag
and a fraction within the fixed tick. Birth overlaps use fraction 0. Unsigned
fields remain exact BigInts in JS (**S4.9.1**). Returned values own their data
(**D5.5.2 / D6.3.3**).

After processing copied rows, call
`runtime.acknowledge_particle_events(node, emitter, through, session, generation)`
with that packet's emitter ID, copied `through` sequence and session/generation.
Only the acknowledged prefix is consumed; duplicate acknowledgement is harmless.
Stale identities, future sequence numbers and missing emitters reject. Scene
reset/replay invalidates old packets. Compatible edits, template correspondence
and graphics recovery retain queued history; descriptions changing runtime
generation require a fresh packet before acknowledgement.

Collision replay reconstructs private animation/physics on the shared native
coordinator, then samples each completed private world. It leaves displayed state
untouched and suppresses gameplay contact recording. Coordinated scene seek
validates source/runtime/physics/world/animation dependencies before restoring
that timeline; collision candidates cannot publish through a particle-only
native handoff. Live legacy physics stepping/reset also uses the coordinator
when collision particles depend on it. Recorded control-history replay remains
M13. Capabilities `particles_cpu_scene_collision` and `particles_contact_batches`
identify this profile; GPU emitters use the separate analytic profile below. See the
[collision component](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md#m6-native-cpu-scene-collision-and-contact-batches--2026-10-31).

### Native GPU analytic particle collisions

A GPU mesh emitter accepts one `particle-collision profile="analytic"` with
1–8 explicit planes/spheres in simulation coordinates. Local emitters use local
coordinates; world emitters use world coordinates independently of later emitter
movement. These volumes are separate from physics bodies and camera depth.
Device motion remains native-owned (**S12.1.5v2 / D10.1.7 / D5.5.2 / D9.5.6**).

```html
<particle-collision profile="analytic" response="bounce" radius=".05"
    skin=".001" restitution=".8" friction=".1" max-impacts="4">
  <particle-plane normal="0 1 0" offset="-1"></particle-plane>
  <particle-sphere center="2 0 0" radius=".5"></particle-sphere>
</particle-collision>
```

Planes keep the particle center on the side `dot(normal,position) >= offset +
radius`; nonzero normals normalize on admission, and offset measures distance
along that normalized normal. Spheres exclude their interior plus particle
radius. Each fixed tick sweeps the analytically integrated displacement, chooses
the earliest hit and integrates remaining time after response. Acceleration is
approximated by each swept chord, as in the CPU scene-query profile. Volumes are
static for the tick; their motion contributes no surface velocity.

`bounce` and `slide` share CPU radius/skin/restitution/friction/bounce-threshold
and 1–16 impact limits. Defaults are radius `.01`, skin `min(.001,radius*.1)`,
restitution `.5`, friction `0`, threshold `.1` and four impacts. Initial overlap
stops a visual particle; exhausted impact work freezes its last valid position.
Copied samples expose `collision_status`: `running`, `initial-overlap` or
`impact-limit`. Stopped slots remain occupied and visible until expiry/reset;
GPU collision produces no gameplay contact packets or authoritative collision
counts. `kill`, scene filters/query options, enabled events and overlap policies
other than `stop` reject.

Ordinary ticks keep pose/velocity on the device without particle readback.
Compatible gravity/drag/volume/response edits checkpoint device motion through
the existing explicit-edit path. Context recovery replays bounded native tick
timestamps and current birth recipes without advancing the scene. Expired/reused
births retire old history; `gpu_history_ticks` exposes the retained count.
Exceeding 4096 retained ticks rejects the particle phase before publication with
a diagnostic. Seek/reset reconstruct current authored descriptions; release
frame-time/peak-memory qualification remains open. Capabilities
`particles_gpu_analytic_collision` and `particles_gpu_collision_replay` identify
this profile. Depth-buffer collision remains outstanding. See the
[implementation record](../vibe/impl/Radiant_Impl_Scene3D_Gaming.md#m6-gpu-analytic-particle-collision--2026-11-02).

### Points, sprites, lines and picking

These drawables share mesh transforms, visibility, instancing and resource
references, using `basic` materials and optional color textures. Point `size`
and line `width` default to 1 logical pixel; geometry `point-sizes` multiplies
each source point. Sprites default to world `size:[1,1]` and
`center:[0.5,0.5]`. Set `size-mode:'screen'/'world'` explicitly to select units.
Screen size stays stable under camera movement and 1×/2× output. They do not
cast or receive shadows. Continuous lines have butt ends and bounded bevel
joins, clipping before expansion at the near plane.

Geometry `drawable-uvs:'surface'` generates billboard UV0 or ribbon V;
`'source'` preserves source UV0 (used by glTF point/line loading). UV1 always
retains source coordinates. glTF modes 0, 1 and 3 use the selected unlit
profile with warnings for omitted PBR lighting. Their styling is not exported.

`lambda.scene3d.picking.hit(camera,node,geometry,x,y,world?)` returns a source
vertex/segment hit or null. Pass a prepared `scene3d.camera`, resolved buffer
geometry and the final world matrix for the object or instance. It shares
camera/ray/triangle helpers and tests point radii, anchors and clipped ribbons;
texture alpha and scene occlusion are outside this query. See the
[profile and return fields](../vibe/radiant/Radiant_Design_WebGL.md#points-sprites-and-lines-m6-selected-profile).
Construction and picking retain **S12.1.1v3 / D9.3.4**.

### Visibility and object LOD

Scenes and drawables default to `frustum-culling:true`. Static bounds include
hierarchy/instance transforms and marker/line extents. Skinned, morphed or
custom-shader objects stay drawable unless they provide a conservative local
`bounds:[minX,minY,minZ,maxX,maxY,maxZ]` covering every deformation. Culling
retains resources, and each shadow/environment view makes its own decision.

`lod([group0,group1,group2],[0,10,30],{hysteresis:0.1})` retains every level
and chooses one by world distance from its origin to the camera. Thresholds
start at 0 and strictly increase; 1–16 group levels are accepted. Moving away
crosses the next threshold × (1+h); moving nearer crosses the current
threshold × (1−h). Hysteresis accepts [0,0.5), defaults to 0.1 and survives
context recovery and corresponding template edits. Nested LODs and inactive
level poses are retained. Put lights, rigs, physics, simulations and clips
outside the geometric level groups.

Shadows use level 0; environment captures select by capture-origin distance.
LOD selection is not exported to glTF. See the
[qualified profile](../vibe/radiant/Radiant_Design_WebGL.md#conservative-visibility-and-object-lod-m7-selected-profile)
for bounds, counters and limits (**S12.1.1v3 / D5.5.2 / D9.3.4**).
The interactive fixture is [test/scene3d/lod.ls](../test/scene3d/lod.ls).

### Materials and lighting

`material('standard', options)` uses metallic-roughness PBR. `metalness` and
`roughness` are [0,1], defaulting to 0 and 1. `basic` is unlit; `lambert`
provides diffuse lighting. All accept color, opacity, side, base-color
`texture`, and emission (`emissive` × `emissive-intensity`). Standard maps
name shared texture IDs:

| Attribute | Meaning |
|---|---|
| `metal-rough-texture` | Linear data: G roughness, B metalness, multiplied by the factors |
| `normal-texture`, `normal-scale` | Tangent-space normal map; scale defaults to 1. Authored tangents are optional; derivatives supply the frame otherwise |
| `occlusion-texture`, `occlusion-strength` | R channel darkens ambient/environment lighting; strength defaults to 1 |
| `emissive-texture` | sRGB map modulates emission |

Base color and emission use sRGB decoding; data maps use linear sampling.
A texture's `color-space:'srgb'/'linear'` overrides that choice. Materials
accept `alpha-mode:'opaque'/'blend'/'mask'`; mask discards below
`alpha-cutoff` (default 0.5, greater than 0 and at most 1).

`material('physical', options)` adds clearcoat and sheen to standard shading.
Both factors default to 0 and accept [0,1]. `clearcoat-roughness` defaults to
0, `sheen-roughness` to 1 (both [0,1]); `sheen-color` defaults to white.
`clearcoat-texture` reads R, `clearcoat-roughness-texture` reads G and
`clearcoat-normal-texture` has its own tangent-space normal and finite
`clearcoat-normal-scale` (default 1). `sheen-color-texture` reads sRGB RGB;
`sheen-roughness-texture` reads linear A. These maps use the same independent
UV selectors/transforms as standard maps. Factor/color tracks reuse existing
playback without relinking (**S12.1.1v3, D10.1.7**). See the
[qualified profile](../vibe/radiant/Radiant_Design_WebGL.md#physical-materials-m4-selected-profile).

The scene's `tone-mapping` is `'none'` by default, or `'neutral'`/`'aces'`;
`exposure` defaults to 1. `max-pixel-ratio` caps the density a scene renders
at: with `'max-pixel-ratio': 1.0` a full-window scene on a 2× display renders
one pixel per CSS pixel, and the GPU upsamples the frame before the page
composites it. Multisampled HDR targets cost about 92 bytes per rendered pixel,
so the cap keeps large viewports inside the graphics budget and cuts fill
cost; it is unlimited by default. Native light units retain Lambert's historical
brightness. glTF punctual lights divide intensity by π on import and
multiply by π on export (SP1 in the [PBR record](../vibe/impl/Radiant_Scene3d_PBR_Shadows.md#10-decisions-for-the-user)).
There are at most eight directional/point/spot lights, plus ambient and one
hemisphere light per scene/environment.

Opaque and masked geometry retain linear HDR radiance until an output pass
tone-maps each antialiasing sample. The authored background keeps its display
color, and transparent surfaces blend afterward against the opaque depth.
Rendering still publishes one document-owned image per changed frame
(**D5.5.1 / D5.5.2**); repeated fixed-time captures reuse that frame
(**D10.1.7**).

`physical` also supports transmission: `transmission` [0,1] defaults to 0,
`ior` ≥1 to 1.5 and `thickness` ≥0 to 0 mesh units. `attenuation-color` defaults
to white; optional positive `attenuation-distance` is in world units, with
omission meaning no absorption. `transmission-texture` reads linear R and
`thickness-texture` linear G. Their coordinate selectors/transforms are independent.
Tracks reuse existing playback, including transmission tracks without an authored
factor. Negative/out-of-range absorption colors diagnose when sampled.

The native glass profile shares one opaque-scene capture, uses bounded rough
screen refraction plus environment/background fallback, and renders before
ordinary alpha blending. It supports opaque/masked coverage; alpha-blended
transmission is diagnosed. Glass objects sample the same opaque image and do
not refract each other. HTML behind the viewport is not a refraction source.
See the [limits and defaults](../vibe/radiant/Radiant_Design_WebGL.md#transmission-m5-selected-profile).

### Shader materials and GPU simulations

Lambda describes the scene and its GLSL; the GPU does per-frame work such as vertex deformation and particle simulation ([plan](../vibe/impl/Radiant_Impl_Scene3d_Shaders.md)). `shader_material(vertex, fragment, uniforms?, options?)` takes GLSL 330 bodies. The host declares the vertex inputs `position`, `normal`, `uv`, `color`, `tangent` and `instance_matrix`, the uniforms `model_matrix`, `view_matrix`, `projection_matrix` and `camera_position`, and the output `fragment_color` (linear; premultiplied when `transparent`). The `uniforms` map gives every other uniform the shader declares: numbers, number arrays, `#rrggbb` colors (linear), a texture ID for a `sampler2D`, or `"<simulation>.<variable>"`. A missing value or a type mismatch is a scene diagnostic. Options include `side`, `transparent` and `alpha-to-coverage`. A keyframe track on `<material id>.uniforms.<name>` animates a uniform, and re-renders that change only uniform values keep the linked program.

`scene3d.model.INSTANCE_LIMIT` names the 16384-instance per-mesh bound.
Procart scene lowering splits larger equal-material groups into bounded meshes,
preserving source IDs, matrices and material options (**S12.1.1v3 / D9.4.5**).

With a shader material, a mesh's `instance-count` (up to 16384) draws instances the vertex shader places itself (`gl_InstanceID`), and `instance-data` maps up to six names to 1–4 numbers per instance. Shader meshes are drawn as one batch and do not cast shadows.

`simulation(id, width, height, variables, uniforms?, options?)` keeps `width × height` RGBA float records per `variable(name, shader, initial?)`. Each step runs every variable's fragment `shader` body, which reads all variables' previous records through samplers named after them (plus `resolution` and `delta`) and writes `fragment_color`. The simulation advances in fixed `timestep` steps (default 1/60 s) toward its numeric `time` uniform, usually driven by a track; time going backwards restarts it from `initial` (zeros when omitted). Its state survives re-renders that change only uniform values.

```lambda
import s: lambda.scene3d
let DRIFT = "void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    fragment_color = texelFetch(position, at, 0) + vec4(0.0, 0.5 * delta, 0.0, 0.0);
}"
let PLACE = "uniform sampler2D records;
void main() { gl_Position = projection_matrix * view_matrix * vec4(position * 0.2 + texelFetch(records, ivec2(gl_InstanceID, 0), 0).xyz, 1.0); }"
s.validate(s.scene([
    s.camera({id: "main", position: [0.0, 0.0, 6.0]}),
    s.simulation("rise", 4, 1, [s.variable("position", DRIFT, [for (i in 0 to 3) *[i - 1.5, 0.0, 0.0, 1.0]])]),
    <mesh 'instance-count': 4, <geometry type: 'plane'>
        s.shader_material(PLACE, "void main() { fragment_color = vec4(1.0); }", {records: "rise.position"})>,
    s.clip("run", [s.track("rise.uniforms.time", [0.0, 10.0], [0.0, 10.0])], {duration: 10.0, autoplay: true})
]))^
```

The [Butterflies demo](../test/demo/scene3d/butterflies/README.md) builds a 4,096-butterfly swarm and flexible wings this way.

### Environments and shadows

`environment('room', options)^` and `environment('sky', options)^` create
lighting scenes. `environment('image', {src: "studio.hdr"})^` uses a local
Radiance RGBE equirectangular image instead. The ID defaults to the kind;
set the scene's `environment` to that ID and optionally
`environment-intensity` (default 1). Environment content is captured for
lighting and reflections, never drawn as scene geometry or background.
An image environment cannot also hold content. HDR input accepts
`-Y H +X W`, flat or adaptive run-length scanlines, up to 4096 pixels per
axis; other orientations, XYZE and non-HDR images are rejected.

Directional, spot and point lights opt into shadows with `cast-shadow:true`;
meshes cast and receive by default. At most four lights cast shadows.
`shadow-map-size` defaults to 2048 for directional and 1024 otherwise;
the integer range is 512–4096, with point cube faces capped at 1024.
`shadow-bias`, `shadow-normal-bias`, `shadow-softness` (0–16 texels,
default 1.5), `shadow-intensity` (0–1, default 1) and directional
`shadow-extent` (half-size; 0/default fits the scene) control the maps.
Opaque/masked meshes cast; blended glass and depth-only materials do not.
Camera-only moves reuse scene projection, environment and shadow caches.

Native tracks also animate a standard material's `metalness` and `roughness`,
any built-in material's `emissive` color, and a light's `shadow-softness`.
Address a material directly by ID or through a mesh, such as
`"chair.material.roughness"`; a light uses `"sun.shadow-softness"`. Emissive
track values are linear RGB. Samples obey the same factor/softness ranges
as authored values, and stopping playback restores the authored state.

### Native postprocessing

`lambda.scene3d.postprocessing` constructs an ordered color-pass chain. Place
one `post.chain(children)` directly in the scene; `s.postprocessing(children)`
is the same constructor. Each chain accepts at most eight effects. Construction
and validation stay pure (**S12.1.1v3 / D9.3.4**); Radiant owns execution,
retained targets and shader programs (**D5.5.1 / D5.5.2**).

```lambda
import s: lambda.scene3d
import post: lambda.scene3d.postprocessing

let scene = s.scene([
    s.camera({id: "main"}),
    s.mesh(s.sphere(), s.material('basic', {color: "#110a04",
        emissive: "#ff8844", 'emissive-intensity': 4.0})),
    post.chain([post.effect('bloom', {strength: 0.7, threshold: 1.0, radius: 4.0}),
        post.effect('fxaa')])
], {camera: "main", width: 640, height: 400, background: "#101820"})
s.normalize(scene)^
```

| Constructor / effect | Supported profile |
|---|---|
| `post.effect('bloom', options?)` | Opaque HDR radiance above luminance `threshold` glows through a half-resolution separable blur. `strength` defaults to 1, `threshold` to 1, and `radius` to 2 logical viewport pixels; all are finite and nonnegative. Zero strength preserves the original image. An opaque viewport background is required; forward transparent objects do not contribute to the HDR glow. |
| `post.effect('fxaa')` | A directional FXAA profile filters display color after tone mapping and transparent draws. It smooths shader/image edges without adding temporal history; it does not claim the full quality profile of Three.js's current FXAA addon. |
| `post.shader(fragment, uniforms?, options?)` | GLSL 330 fragment body using host `color_input` (`sampler2D`), `resolution` (output width/height), and `fragment_color`. Supply `void main()` and declare authored uniforms as with a shader material. The pass preserves input alpha. |

Shader effects default to `domain:'linear'`, before tone mapping. Set
`domain:'display'` for tone-mapped linear color; texture/framebuffer conversion
handles sRGB storage. All linear effects precede display effects. Bloom is
linear and FXAA is display-only; invalid orders produce diagnostics. Custom
passes expose color, numeric uniforms, scene textures and simulation samplers,
with the existing reflection/type limits; depth and arbitrary attachments are
not exposed by this profile.

Give an effect an `id` to animate a shader uniform with
`"grade.uniforms.amount"` or a bloom parameter with `"glow.strength"`,
`"glow.threshold"` or `"glow.radius"`. The shared clip clock drives these
properties; an additional animation loop is unnecessary. Uniform-only template
edits reuse compatible programs and pass targets. See the runnable lifecycle
fixture [postprocessing.ls](../test/scene3d/postprocessing.ls) and
[the implementation record](../vibe/impl/Radiant_Impl_Scene3d_Three_Features.md).

### Texture coordinates and vertex alpha

Buffer geometry accepts `uvs` and `uvs1` (two numbers per vertex) and `colors`
(three or four per vertex). RGB is sRGB; alpha is linear. Built-in materials
multiply vertex, material and texture alpha for blend/mask modes; opaque mode
ignores all three alpha inputs. Optional streams occupy GPU bytes only when used.

Each built-in map references its own texture definition. Texture options
`'uv-set':0/1` select its coordinates and `'uv-transform':[a,b,c,d,e,f]` apply
`[a*u+b*v+c, d*u+e*v+f]` in the native lower-origin convention. The default is
identity on UV0. An explicitly requested missing set is diagnosed. Normal maps
use the selected/transformed frame, including rotations and mirrors. Custom
GLSL receives raw `uv`/`uv1`; `color` is vec3 for RGB/default geometry and vec4
for RGBA geometry. See the [fidelity fixture](../test/scene3d/model-fidelity.ls).
These remain pure scene values (**S12.1.1v3, D9.3.4**).

### Assets, export and limits

`load` supports selected OBJ/MTL, glTF/GLB, STL, PLY and A3D profiles.
glTF imports standard factors, base-color/metal-rough/normal/occlusion/emissive
maps, alpha masks, tangents, UV0/UV1, RGB/RGBA vertex colors and camera nodes.
Each map preserves its own `KHR_texture_transform` and coordinate selection.
`KHR_materials_clearcoat`, `KHR_materials_sheen`, `KHR_materials_transmission`,
`KHR_materials_ior` (IOR ≥1) and `KHR_materials_volume` import/export represented factors,
maps and independent normal scale. A glTF sheen color folds the native sheen
factor into its linear RGB value on static export.
Explicit viewport `camera` selection wins over imported cameras; omitted
perspective `zfar` uses `infinite-far:true`. Orthographic cameras permit near 0. MTL's `Pr`, `Pm`, `map_Pr`, `map_Pm` and `norm`
select standard shading, independently of `illum` order. Scalar images read
R and pack into linear G roughness/B metalness; both images must share
dimensions and wrap modes. Missing map channels are white, and a metallic
map without an explicit `Pm` factor uses 1. `norm` supports `-bm` normal
scale. Other physical extensions and unsupported texture options retain
their existing warnings/errors.

`format(scene, 'gltf')^` returns JSON with an embedded buffer;
`format(scene, 'glb')^` returns GLB bytes. Export preserves supported PBR
factors/maps, their UV0/UV1 selectors and TRS transforms, RGB/RGBA colors,
cameras, masks and tangents; image URIs remain references, including
in GLB. Export supports a static subset: skeletons, deformation, animation
and depth-only materials raise errors. Environment resources, tone mapping
and shadow settings are not encoded. See the
[export profile](../vibe/radiant/Radiant_Design_WebGL.md#gltf-and-glb-export)
for the precise mapping and refusals.

Anisotropy, iridescence, dispersion, cascaded shadows, global illumination/SSAO and
depth-dependent postprocessing remain follow-on scope. The
[Dreamhome demo](../test/demo/dreamhome/README.md) combines the implemented
PBR, room lighting and sun shadows. Its release frame measurement and
remaining aggregate-gate failures are in the
[implementation record](../vibe/impl/Radiant_Scene3d_PBR_Shadows.md#11-progress).

## 28. color — Pure color utilities

`lambda.color` provides shared pure color operations (**S12.1.1v3**). Parsing
was extracted from the slide library, and HSV/hex conversion from UI palette
helpers, so those consumers and Procart share the same arithmetic.

| Function | Result |
|---|---|
| `parse(value,path="value")` | Normalized RGBA array, or error. Accepts hex `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()/rgba()`, `transparent`, or a normalized four-component array. |
| `try_parse(value)` | The same normalized RGBA array, or `null` for unsupported input; probes paint compatibility without raising a diagnostic (S7.4.1–S7.4.2). |
| `hsv(rgba)` | `{h,s,v}` from normalized encoded RGB channels; hue is in degrees. |
| `rgb(h,s,v,alpha=1.0)` | Normalized RGBA from HSV components; hue wraps. Saturation/value and alpha are supplied normalized. |
| `hex(rgba)` | `#rrggbb`, clamping/rounding RGB channels to bytes; alpha is returned separately by the caller's color model. |
| `hex_byte(value)` | Two lowercase hexadecimal digits for an integer byte in `0..255`; shared by UI tokens and DOCX export. |

```lambda
import colors: lambda.color
let rgba = colors.parse("#cc744f")^
let hsv = colors.hsv(rgba);
colors.hex(colors.rgb(hsv.h+10,hsv.s,hsv.v))
```

## 29. pptx — PowerPoint import

`lambda.doc.pptx` imports local `.pptx` paths or captured ZIP roots into the
editable `lambda.slide` model (**D9.3.1/D9.3.4**). It shares DOCX's bounded
OPC ZIP/XML capture and checked relationship resolution (**D11.1.2**); prefixes
and relocated part names do not determine document meaning.

```lambda
import pptx: lambda.doc.pptx
import slide: lambda.slide
let result = pptx.convert("test/input/pptx/features.pptx")^;
slide.page(result.document)^
```

`convert(source, options=null)` returns `{document,html,text,metadata,diagnostics}`.
`document` is an editable `<presentation>`, `html` a complete static handout,
`text` the reading-order slide text (excluding speaker notes), and `metadata`
the available core properties. Paragraphs use newlines; table columns use tabs.
`to_elements`, `to_html` and `extract_text` select the corresponding fields.

Options are `images:"data-uri"` (default) or `"omit"`, and
`unsupported:"warn"` (default) or `"error"`. Nonfatal issues retain usable
content and return `{severity,code,part,message}` diagnostics. Fundamental
package/XML failures raise errors in the 7700–7706 range. External resources
are not fetched; safe external text hyperlinks survive and unsupported/unsafe
links keep their visible labels. Image bytes are embedded in the imported deck.

The initial profile preserves slide order/size, shared theme colors/fonts,
solid backgrounds, ordinary master/layout placeholder inheritance and
non-placeholder decorations, paragraph/run formatting, character and decimal
list markers, simple rectangles/ellipses/lines, numeric custom paths (including
holes and Bézier curves), linear and point-focus radial gradients with alpha,
stretched picture fills and opaque-endpoint duotone image effects. Raster images
and bounded crops, ordinary table cells, groups, cached clustered-column charts
and speaker notes are also retained. DrawingML colors support ordered shade/tint,
hue/saturation/luminance and alpha modifiers.
Layout/master content is materialized as editable objects. SmartArt, arbitrary
guide-based shape geometry, complex table merging/styles, nonuniform text scaling,
tiled or noncircular gradients, pattern fills, advanced effects, OLE/media, animation timing, transitions
and exact PowerPoint text fitting remain partial or unsupported and can produce
warnings. PPTX writing is outside this profile.

`lambda view deck.pptx` uses the retained Slides player. `layout` and `render`
use the same document transform; `view deck.pptx --archive` exposes package parts.
Recoverable warnings start in a collapsed yellow summary. Repeated code/message
pairs share a row with occurrence counts; the expanded list scrolls over the
canvas without moving the player controls. The API retains every diagnostic.
The document browser previews slides and keeps converted HTML on its Source tab.
Slides **Open** imports PPTX in a cancellable process, displays warnings in yellow,
retains the earlier deck after failures, and saves the editable copy as `.slides`.
See [the profile and acceptance cases](../vibe/Lambda_Pkg_Pptx.md) and
[fixture provenance](../test/input/pptx/README.md).

## 30. paint — Painting with brushes

`lambda.paint` paints with brushes on tiled typed-array layers. A Radiant-native engine
renders the per-pixel work, so painting is available in `lambda.exe` and
`lambda-headless` but not in `lambda-cli` (**D9.2.2**). It provides layers in a pixel medium
and an oil-like pigment medium, round, libmypaint-model and bristle brushes, MyPaint brush
import, smudge and blend modes, strokes with continuations, and presentation with impasto
lighting. Design:
[Lambda_Pkg_Paint.md](../vibe/Lambda_Pkg_Paint.md). Progress:
[implementation record](../vibe/impl/Lambda_Impl_Paint.md).

```lambda
import paint: lambda.paint

let canvas = paint.layer(640, 480)^
let ink = paint.brush({size: 12.0, hardness: 0.7, spacing: 0.15})^
let result = paint.stroke_step(canvas, ink, [0.1, 0.2, 0.6], [[40, 60, 0.3], [200, 90, 0.9], [360, 70, 0.5]])^
let pixels = paint.present(result.layer)^          // (480, 640, 4) u8 sRGB
```

**Layers.** A layer is a map whose `tiles` hold one 64 × 64 tile per grid cell. An `rgba`
tile is an `f32` `(64, 64, 4)` array of premultiplied colour, in linear light by default
(`blend_space: 'srgb'` keeps encoded values). A `coverage` tile is a `(64, 64)` array of
`f32` or `u8` samples. An untouched tile is `null`. Functions return new layers that share
every untouched tile, so keeping an old layer value is a cheap undo snapshot. The procedures
`stroke` and `feed` write a layer in place instead (see below).
`from_pixels`, `import_pixels`, `present` and `pixel` move samples in and out.

**Brushes and strokes.** `brush(options)` builds a brush. Kind `'round'`, the default, is a
simple dab brush:

| Option | Meaning |
|---|---|
| `size` | Diameter in pixels |
| `hardness` | Core of the falloff |
| `spacing` | Dab step as a fraction of the diameter |
| `flow` | Per-dab gain, scaled by pen pressure |
| `opacity` | Per-dab alpha in build-up mode, stroke alpha in wash mode |
| `aspect`, `angle` | Ellipse ratio, and its angle in degrees |
| `aa` | Anti-aliased rim width |
| `profile` | `'mypaint'` or `'linear'` falloff |
| `mode` | `'buildup'` composites every dab; `'wash'` accumulates coverage and composites once |
| `erase` | Removes paint instead of adding it |
| `lock_alpha` | Recolours without adding coverage |
| `blend` | `'normal'`, `'multiply'`, `'screen'`, `'overlay'`, `'darken'`, `'lighten'` or `'add'` |
| `tip` | A bitmap tip: an `(h, w)` coverage array that fills the dab's ellipse box in place of the procedural falloff |
| `grain` | In [0, 1]: how far the stroke's paper texture modulates coverage; the dab reaches paper peaks fully and valleys by `1 - grain` |
| `angle_jitter` | Up to this many degrees of rotation per dab, so a bitmap tip does not repeat visibly; keyed by dab position, so a split stroke still equals the whole |

Kind `'mypaint'` follows libmypaint's brush model. Its `settings` map libmypaint setting names
to a number or to `{base, inputs}`, where each input (`pressure`, `speed1`, `random`,
`tilt_declination`, `barrel_rotation`, and so on) maps to 2 to 64 `[x, y]` points with
ascending `x`. Unset settings take libmypaint's defaults, so `brush({kind: 'mypaint'})` is
libmypaint's default brush. The dynamics run in Lambda: speed and direction filters, slow
tracking, keyed random offsets, and dabs spaced by radius and by time. They also cover colour
shifts, smudge, eraser, lock alpha, colorize and posterize. A setting the engine does not
render yet, such as `snap_to_pixel`, is an error. `seed` selects the keyed randomness, so
repeating a stroke repeats its dabs.

```lambda
let pencil = paint.brush_from_myb(input("pencil.myb", 'json'))^
let line = paint.stroke_step(canvas, pencil.brush, [0.2, 0.2, 0.2], samples)^
```

`brush_from_myb(data, options)` turns a parsed version 3 `.myb` file into `{brush, ignored}`.
`ignored` names the settings the file changes but the engine does not render yet. The stroke
colour replaces `color_h`, `color_s` and `color_v`.

Both kinds take `tip` and `grain`. `paint.tip(kind, {size, seed})` builds a tip: `'chalk'` (a
grainy disc), `'flat'` (a wide bar for a flat or calligraphic brush) or `'speckle'` (scattered
grains). `paint.paper(kind, {size, seed})` builds a tileable paper height field in [0, 1]:
`'smooth'`, `'cold_press'`, `'rough'` or `'canvas'`. Pass it to a stroke as
`stroke(layer, brush, colour, points, {paper})` or `stroke_step(..., previous, {paper})`; without
paper, grain has no effect. Equal kinds, sizes and seeds give equal planes.

Points are pen samples `[x, y, pressure, tilt_x, tilt_y, twist, time]`. Only `x` and `y` are
required. Tilt and twist are in W3C degrees, and time is in seconds; a sample without one
follows the previous sample by 10 ms. In a Radiant handler, `dom.pointer_samples(evt)`
returns a pointer event's pen samples in this layout, with `x` and `y` in viewport CSS
pixels ([Lambda_Sys_Func](Lambda_Sys_Func.md)). Colours are sRGB `[r, g, b]` or `[r, g, b, a]`.

`stroke_step(source, brush, colour, points, previous)` continues a stroke batch by batch.
Round-brush dabs sit at fixed arc-length steps from the stroke's first point, and a mypaint
brush carries its dynamics state, so a split stroke paints exactly the pixels of the whole
stroke. Its result carries:

- `layer`: the preview;
- `coverage`: the coverage accumulated so far, in wash mode;
- `smudge`: the smudge buckets of a smudging mypaint brush, passed back to the next batch;
- `rect`: the pixels this batch may have changed;
- `bounds`: the pixels the stroke so far may have changed;
- `work`: the candidate pixels charged so far, bounded at 128 million per stroke.

**Pigment layers.** `layer(w, h, {format: 'pigment', paper})` is a wet paint film over the
sRGB `paper` colour (white by default). Each tile is an `f32` `(64, 64, 11)` array holding the
film's Kubelka–Munk absorption and scattering per colour band, its thickness, the paper's
reflectance and the dried relief. On a pigment layer a stroke's colour becomes a pigment,
`pigment(colour)` (`{K, S}`), and brushes take two more options:

| Option | Meaning |
|---|---|
| `amount` | Pigment load per unit coverage (1); a small amount gives a thin film the paper shows through |
| `thickness` | Paint body per unit coverage, which becomes relief (equal to `amount` by default) |

Wet paint mixes by concentration, so blue over wet yellow is green, and the order does not
matter. `dried(layer)` folds the wet film into the paper and keeps its appearance; strokes over
dried paint lie over it instead of mixing. `dry_layer(var layer)` does the same in place.
`present(layer, {lighting: paint.lighting({azimuth, elevation, relief, diffuse, specular,
shininess})})` lights the paint's relief; a flat film keeps its colour. Wash mode, lock alpha,
colorize, posterize, blend modes, colour shifts and `background` are rgba-only and are errors
on a pigment layer.

**Bristle brushes.** `brush({kind: 'bristle', ...})` draws one trail per hair, on either
medium. The hairs are spread across the brush, which turns with the stroke direction and
widens with pressure. Each hair lags by its stiffness, spends its paint load along the trail,
so a long stroke thins out, and can pick up the paint it crosses. The options are `size`,
`spacing`, `opacity`, `flow`, `aa`, `erase` and `grain` as for round brushes, plus:

| Option | Meaning |
|---|---|
| `count` | Number of hairs (24) |
| `spread` | How far across the brush they reach, in radii (1) |
| `width` | Each trail's width in pixels |
| `stiffness`, `stiffness_jitter` | How far a hair lags behind the brush, in [0, 0.99], and its variation |
| `load`, `load_jitter` | The paint each hair starts with, and its variation |
| `depletion` | Load spent per pixel of trail at full strength |
| `pickup` | In [0, 1]: how much of the paint under a hair it takes into its ink |
| `pressure_spread` | How far pen pressure widens the brush |
| `seed` | Hair placement; equal seeds paint equal strokes |

A bristle stroke's continuation carries each hair's state in `bristles`, so a split stroke
equals the whole.

**In-place strokes.** `stroke(var layer, brush, colour, points, options)` is a procedure
(`pn`). It paints a whole stroke into the layer and returns `{rect, work}`, with the pixels
`stroke_step` gives from `null`. A build-up stroke writes the layer's tiles through
`radiant.paint_stroke`, which copies a tile only while it is shared (**D7.6.3**, **D7.6.4**). A
value of the layer kept before the stroke keeps its pixels (**S9.1.2**). A wash stroke
composites its coverage over the layer as it was, so it always produces a new layer. A
function cannot call a procedure (**S12.1.1v3**), so pure code uses `stroke_step`.

```lambda
pn main() {
    var canvas = paint.layer(640, 480)^
    let ink = paint.brush({size: 12.0, hardness: 0.7})^
    let r = paint.stroke(canvas, ink, [0.1, 0.2, 0.6], [[40, 60, 0.3], [200, 90, 0.9]])^   // {rect, work}
    return paint.present(canvas)^
}
```

`dabs(brush, colour, points)` exposes the engine's dab table: 34 `f32` columns per dab.
Unknown options are errors (**S17.8.1**).

`adjust_brush(brush, {size, opacity})` returns the same brush at another diameter or opacity. A
mypaint brush keeps its input curves: `size` sets `radius_logarithmic` and `opacity` sets `opaque`.

An interactive session is a value holding one paint layer over a paper colour, the stroke in
progress, the rectangle still to be shown, and stroke-level undo and redo:

```lambda
var s = paint.session(1200, 800)^                      // {paper: [r, g, b], history_budget}
s = paint.begin(s, evt.pointerId, brush, [0.1, 0.2, 0.5])^
paint.feed(s, evt.pointerId, paint.in_layer(dom.pointer_samples(evt), box, s))^   // in place: {rect, work}
let f = paint.frame(s)^                                // in place: {rect, pixels}
if (f.rect != null) dom.set_canvas_pixels_rect(canvas, f.pixels, f.rect)^
s = paint.end(s, evt.pointerId)^                       // one undo entry
```

- `feed(var session, pointer, samples)` is a procedure that continues the stroke in place and
  returns the batch's `{rect, work}`. Its first batch copies the tiles it touches, which the
  stroke's undo entry keeps; later batches write those tiles in place while nothing else holds
  the session. Placing the session into another container between batches, such as a view
  state rebuilt as `{*: st, ps: s}`, shares it (**S9.3.1**), so the next batch copies its tiles
  again. Keep it in place instead: pass `st.ps` to `feed` and `frame`, and update other fields
  with member writes such as `st.token = 0`. Compute the samples first, since a plain argument
  that reads `st` keeps the borrow a snapshot.
- `in_layer(samples, box, session, pressure)` moves pen samples from viewport CSS pixels into
  layer pixels for a canvas shown in `box` (`dom.bounding_box`). A non-null `pressure` replaces
  each sample's pressure; a demo might pass 1.0 for a mouse.
- `frame(var session)` is a procedure that returns the dirty rectangle as `(h, w, 4)` sRGB `u8`
  samples over the paper, `{rect, pixels}`, and clears it in place; both are `null` when nothing
  changed. A new session's first frame is the whole canvas.
- `undo`, `redo`, `can_undo`, `can_redo`, `clear`, `cancel(s, pointer)` and `mark(s, rect)`
  (present again) complete the history. A history entry keeps the previous layer, whose
  untouched tiles are shared. Entries are dropped oldest first once the tiles they keep exceed
  `history_budget` bytes (256 MiB by default); the newest entry always stays. `undo` during a
  stroke cancels it.
- `color_at(s, x, y)` is the sRGB colour shown at a layer pixel, for a colour picker.
- `session(w, h, {texture})` and `with_texture(s, texture)` set the paper texture that every
  stroke's grain reads: an `(h, w)` height array, or a spec `{kind, seed, size}` that
  `paint.paper` builds. A painting file keeps a spec and rebuilds the texture when opened.
- One stroke paints at a time: `begin` from another pointer first ends the stroke in progress.
- `session(w, h, {medium: 'pigment', lighting})` paints on a pigment layer over the paper.
  `lighting` takes `paint.lighting` options, or `false` for flat colour; a light is on by
  default. Wet strokes mix until `dry(var session)` sets them. `with_lighting(s, lighting)`
  changes the light, and a painting file keeps the pigment layer.

[`lambda demo paint`](../test/demo/paint/README.md) is built on this session.

A painting is saved as a `.painting` file, a ZIP container in the style of Krita's `.kra`. It
stores the layer's pixels, so opening it decodes them in one pass and never replays strokes:

```lambda
paint.save_painting(s, "study.painting")^      // bytes written, replaced atomically
let back = paint.open_painting("study.painting")^   // a session with nothing to undo
paint.export_png(back, "study.png")^           // flattened over the paper
```

The entries are `mimetype` (`application/x-lambda-painting`, first), `painting.mark` (the
`<painting>` element: size, paper, and a `<layer>` naming its tile entry), `layers/paint.tiles`
(the present tiles as little-endian samples with a tile index; empty tiles are omitted),
`preview.png` (scaled down when the painting exceeds 16 million pixels, the image raster limit, which
also bounds `export_png`) and a `thumbnail.png` at most 256 pixels on its longer side.
`painting_tree(s)` returns the file's `<fs>` tree without writing it, and `painting_document(s)`
returns the `<painting>` element. `flattened(s)` gives the pixels `export_png` writes, and
`png(pixels)` encodes any `(h, w, 4)` sRGB `u8` array. Opening checks the mimetype, the version
and that the tiles match the layer's size. Painting files need Radiant, so `lambda-cli` cannot
read or write them.

`lambda.image`'s `paint_stroke`, `paint_stroke_step` and `retouch` use this brush, with the
linear profile, for their stroke coverage.


## 31. xlsx — Spreadsheet workbook import

`lambda.doc.xlsx` is a shipped script package (**D9.3.4**) that reads a local
`.xlsx` path or a ZIP root captured with `input(path, "zip")`. It uses the
shared OPC reader and its relationship and archive admission rules (**D11.1.2**).

```lambda
import xlsx: lambda.doc.xlsx
let workbook = xlsx.convert("budget.xlsx")^
workbook.sheets[0].cells
```

`convert(source)` and `load(source)` return `{title, date1904, sheets, metadata,
diagnostics}`. Each sheet has a title, part path, sparse zero-based cells, column
width ranges, row heights, merged ranges, hidden dimensions, frozen prefixes,
split-pane geometry and selection, conditional rules and diagnostics. Cells expose their coordinates, XLSX
type, typed value, raw value, formula and formula type, cached-value presence,
and the supported style projection. Rich shared/inline text is flattened. The
reader accepts ordinary non-macro XLSX workbooks and rejects invalid or missing
package parts. Diagnostics report selected unsupported features; their absence
does not guarantee a lossless import.

The [Sheet demo](../test/demo/sheet/README.md) converts that model to its own
editable workbook and opens it through **Open**. It supports at most 50 sheets,
100,000 rows and 1,000 columns per sheet, plus its existing cell and operation
limits. Imported text, numeric and boolean values, formula source, common
font/fill/alignment and number styles, and row/column dimensions are retained
where the demo supports them. Common built-in and custom date/time formats are
classified and displayed as ISO dates and 24-hour times using the workbook's
1900 or 1904 calendar; this normalizes presentation rather than reproducing the
format code. Formulas retain that workbook date system and can reference other
sheets. Common formulas recalculate in Sheet;
otherwise a saved XLSX result is shown until an edit invalidates imported caches.
Sheet displays imported merged ranges, hidden dimensions, frozen prefixes and
split panes with independently scrolling views. It applies formula-expression
rules, numeric cell-comparison rules with constant or formula operands, text and
blank conditional rules, and RGB differential font/fill styles, in priority
order with `stopIfTrue`; these styles recalculate with cell values. Conditional
formulas use the Sheet formula profile and dynamically dispatched calls
(**S12.3.4v2**, **D7.2.2**). Rules outside Sheet's bounded grid are
clipped with diagnostics.

This is not a lossless XLSX editor. Shared/array formula expansion, charts and
drawings, arbitrary custom format fidelity, theme colours and many conditional
format types are outside this profile. Diagnostics identify unsupported
conditional rules, protected sheets and workbook structures. Sheet can move
imported split dividers by pointer or keyboard; creating or removing splits is
not supported, and live scroll positions are transient. Sheet saves its conversion as `.sheet.json`, not XLSX; the separate chart exporter writes a limited XLSX table.
