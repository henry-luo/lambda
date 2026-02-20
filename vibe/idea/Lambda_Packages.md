# Lambda Package Ideas

Candidate packages for Lambda, ported/adapted from Node.js, Python, R, and other ecosystems. Each leverages Lambda's strengths: pure functional design, element trees, SVG/PDF output, and data transformation pipelines.

The existing `chart` package (a Vega-Lite port) demonstrates the pattern: adapt the _concepts and specification model_ from a mature library, but implement idiomatically using Lambda's element syntax, pipes, comprehensions, and immutable data.

---

## Tier 1 — Natural Extensions

These play directly to Lambda's strengths (pure math, element trees, SVG output, data processing).

### `diagram` — Graph/Diagram Layout Engine

| | |
|---|---|
| **Port From** | Graphviz / dagre (Node.js) / D2 layout concepts |
| **What It Does** | Takes graph data (nodes + edges) → computes positions via layout algorithms → outputs SVG element trees |
| **Why It Fits** | Lambda already _parses_ DOT, D2, and Mermaid via `input()`, but has no layout engine to position nodes. This closes the major gap from parsed graph → rendered SVG. Force-directed, hierarchical, and layered layouts are pure math — no DOM needed |
| **Key Algorithms** | Sugiyama (layered/hierarchical), force-directed (Fruchterman-Reingold), tree layout, circular layout |
| **API Sketch** | `diagram.layout(graph, {algorithm: "hierarchical"})` → positioned graph → `diagram.render(positioned)` → `<svg>` element tree |
| **Scope** | ~10-15 modules: layout algorithms, edge routing, label placement, SVG rendering |

### `dataframe` — Tabular Data Wrangling

| | |
|---|---|
| **Port From** | dplyr (R) / Pandas (Python) concepts |
| **What It Does** | Structured operations on tabular data: `group_by`, `pivot`, `join`, `window`, `spread/gather`, `mutate`, `summarize` |
| **Why It Fits** | Lambda's comprehensions (`for ... where ... order by`) and pipes (`data \| ~.field`) make this idiomatic. The chart package already does basic transforms (filter, aggregate, bin) — this generalizes them into a reusable data toolkit |
| **API Sketch** | `data \| df.group_by('category') \| df.summarize({total: sum(~.value)}) \| df.pivot('year', 'category', 'total')` |
| **Key Operations** | `group_by`, `summarize`, `mutate`, `pivot_wider`, `pivot_longer`, `left_join`, `inner_join`, `window` (rank, lag, lead, cumulative), `fill_na`, `distinct` |
| **Scope** | ~5-8 modules: core operations, joins, pivots, window functions, type inference |

### `stat` — Statistical Modeling Toolkit

| | |
|---|---|
| **Port From** | statsmodels (Python) / R stats |
| **What It Does** | Linear/polynomial regression, correlation, moving averages, interpolation, curve fitting, hypothesis testing, distributions |
| **Why It Fits** | Lambda already has `sum`, `avg`, `median`, `variance`, `quantile`, `dot`, `norm`. This extends into modeling. Outputs are data (coefficients, p-values, fitted values) that feed naturally into `chart` for visualization |
| **API Sketch** | `stat.lm(data, {y: "sales", x: ["price", "ads"]})` → `{coefficients: [...], r_squared: 0.87, residuals: [...]}` |
| **Key Functions** | `lm` (linear model), `cor` (correlation matrix), `moving_avg`, `interpolate`, `polyfit`, `t_test`, `chi_squared`, `normal_dist`, `bootstrap` |
| **Scope** | ~6-10 modules: regression, correlation, distributions, tests, smoothing, resampling |

### `table` — Data Table Renderer

| | |
|---|---|
| **Port From** | AG Grid / Tabulator concepts |
| **What It Does** | Renders tabular data as styled HTML or SVG tables with column alignment, header styling, cell formatting, zebra striping, column widths |
| **Why It Fits** | Formatted data tables are the most common data display after charts. Pure data in → styled element tree out. Outputs to HTML (via Radiant for PDF) or SVG. Complements `chart` for dashboards and reports |
| **API Sketch** | `table.render(data, {columns: [{field: "name", width: 200}, {field: "score", align: "right", format: ".2f"}]})` → `<table>` element tree |
| **Key Features** | Auto column widths, number formatting, conditional cell styling, sortable headers, pagination metadata, totals/subtotals rows |
| **Scope** | ~4-6 modules: layout, formatting, styling, SVG renderer, HTML renderer |

### `geo` — Geographic/Map Visualization

| | |
|---|---|
| **Port From** | D3-geo / topojson (Node.js) |
| **What It Does** | Map projections (Mercator, orthographic, Albers, etc.) converting geographic coordinates → SVG paths. Choropleth maps, bubble maps, point overlays |
| **Why It Fits** | Map projections are elegant pure math functions. GeoJSON is JSON (Lambda parses it natively). Output is SVG paths via element trees. Pairs beautifully with `chart` for data dashboards |
| **API Sketch** | `geo.project(geojson, {projection: "mercator"}) \| geo.choropleth({fill: ~.properties.population, scale: "blues"})` → `<svg>` |
| **Key Features** | 10+ projections, GeoJSON/TopoJSON input, choropleth fills, point/bubble overlays, graticule lines, map labels |
| **Scope** | ~8-12 modules: projections, path generation, choropleth, topology, labels, graticules |

### `tree` — Hierarchy Visualization

| | |
|---|---|
| **Port From** | D3-hierarchy (Node.js) |
| **What It Does** | Visualizes hierarchical/tree data as treemaps, sunburst diagrams, dendrograms, icicle charts, circle packing |
| **Why It Fits** | Pure recursive tree algorithms → SVG. Natural fit for a functional language. Lambda's element trees are already hierarchical — visualizing tree structures is a meta-fit |
| **API Sketch** | `tree.treemap(hierarchy, {size: [800, 600], value: ~.size})` → `<svg>` element tree |
| **Key Layouts** | Treemap (squarified, binary, slice-dice), sunburst, dendrogram, icicle, circle packing, indented tree |
| **Scope** | ~6-8 modules: hierarchy construction, layout algorithms, rendering, labeling |

---

## Tier 2 — Data Processing & Utilities

These leverage Lambda's data processing capabilities for practical tooling.

### `diff` — Structural Diffing

| | |
|---|---|
| **Port From** | jsondiffpatch (Node.js) / deepdiff (Python) |
| **What It Does** | Computes structural diffs between data structures (maps, lists, element trees). Produces patch objects. Applies patches |
| **Why It Fits** | Lambda processes JSON, XML, YAML, HTML — diffing these structures is a natural extension. Useful for version comparison, change tracking, configuration auditing |
| **API Sketch** | `diff.compare(old_data, new_data)` → `{added: [...], removed: [...], changed: [...]}` |
| **Scope** | ~3-5 modules: diff algorithm, patch format, tree diff, visualization |

### `units` — Unit Conversion

| | |
|---|---|
| **Port From** | Pint (Python) / convert-units (Node.js) |
| **What It Does** | Physical quantity conversion (length, mass, temperature, volume, speed, etc.), currency, time |
| **Why It Fits** | Pure functions, useful in data processing pipelines. Pairs well with `dataframe` for normalizing measurement data |
| **API Sketch** | `units.convert(5, 'km', 'miles')` → `3.10686` |
| **Scope** | ~2-4 modules: unit definitions, converter, compound units, formatting |

### `text` — Text Analysis

| | |
|---|---|
| **Port From** | natural (Node.js) / NLTK concepts (Python) |
| **What It Does** | Word frequency, n-grams, readability scores (Flesch-Kincaid, etc.), Levenshtein distance, tokenization, stemming, TF-IDF |
| **Why It Fits** | Operates on Lambda's string type. Text analysis is pure functional — string in, data out. Useful for document processing pipelines |
| **API Sketch** | `text.readability(document)` → `{flesch_kincaid: 8.2, word_count: 1543, ...}` |
| **Scope** | ~5-7 modules: tokenizer, frequency, distance metrics, readability, stemmer, TF-IDF |

### `qr` — QR Code / Barcode Generator

| | |
|---|---|
| **Port From** | qrcode (Python) / qrcode-generator (Node.js) |
| **What It Does** | Encodes data into QR codes or barcodes, outputs SVG path elements |
| **Why It Fits** | Pure algorithmic encoding → SVG element trees. Perfect fit: math → markup. Useful for document generation, labels, invoices |
| **API Sketch** | `qr.encode("https://example.com", {size: 200, correction: "M"})` → `<svg>` element tree |
| **Scope** | ~3-4 modules: QR encoding, barcode formats (Code128, EAN), SVG renderer |

### `color` — Color Utilities

| | |
|---|---|
| **Port From** | chroma.js (Node.js) / d3-color |
| **What It Does** | Color space conversions (RGB, HSL, Lab, HCL, Oklab), palette generation, accessibility contrast checking (WCAG), color interpolation, color blindness simulation |
| **Why It Fits** | Already partially in `chart/color.ls`. Worth extracting and expanding as a standalone package. Pure math functions on color values. Useful for chart theming, document styling, accessibility auditing |
| **API Sketch** | `color.contrast("#333", "#fff")` → `12.63`; `color.palette("blues", 7)` → `[...]`; `color.to_hsl("#ff6600")` → `{h: 24, s: 100, l: 50}` |
| **Scope** | ~3-5 modules: color spaces, palettes, contrast/accessibility, interpolation, named colors |

---

## Tier 3 — Document & Presentation

These leverage Lambda's full pipeline (data → element trees → Radiant layout → PDF/SVG output).

### `report` — Document Template Engine

| | |
|---|---|
| **Port From** | Jinja2 (Python) / Handlebars (Node.js) concepts |
| **What It Does** | Combines data + template → styled HTML/PDF reports with embedded charts, tables, headers, footers, page numbers |
| **Why It Fits** | Lambda's element trees + `format()` + Radiant CSS layout engine make it a natural report generator. Data processing → chart/table rendering → document composition → PDF output is the full pipeline |
| **API Sketch** | `report.render(template_el, data, {format: "pdf", output: "report.pdf"})` |
| **Scope** | ~5-8 modules: template parser, data binding, page layout, header/footer, TOC generation |

### `slide` — Presentation Generator

| | |
|---|---|
| **Port From** | reveal.js / Marp (Node.js) concepts |
| **What It Does** | Markdown or elements → slide decks rendered to SVG/PDF. Slide layouts, themes, speaker notes, transitions (as named styles) |
| **Why It Fits** | Lambda has markdown input, CSS layout (Radiant), PDF output. Slide generation is: parse content → lay out fixed-size pages → render. No interactivity needed for the output format |
| **API Sketch** | `slide.render(markdown_content, {theme: "dark", size: [1920, 1080]})` → PDF or SVG pages |
| **Scope** | ~4-6 modules: slide parser, layout engine, themes, master layouts |

### `calendar` — Calendar / Timeline Visualization

| | |
|---|---|
| **Port From** | FullCalendar / vis-timeline concepts |
| **What It Does** | Calendar grids (month/week/day views), Gantt-style timeline bars, event markers. Lambda datetime → SVG layout |
| **Why It Fits** | Lambda has datetime literals (`t'2025-01-01'`), datetime parsing, and date arithmetic. Calendar layout is grid math → SVG rectangles/text. Gantt charts are horizontal bar charts on a time axis |
| **API Sketch** | `calendar.month(2025, 6, events)` → `<svg>` month grid; `calendar.gantt(tasks)` → `<svg>` timeline |
| **Scope** | ~4-6 modules: date grid math, month/week/day views, Gantt layout, event rendering |

---

## Avoid

Packages that are **poor fits** for Lambda's model:

- **Real-time DOM interaction** — Lambda has no browser runtime; interactive widgets (drag-and-drop, live editing) don't apply
- **Mutable state machines** — Game engines, GUI frameworks with event loops contradict the pure functional model
- **Thin C FFI wrappers** — Lambda packages are `.ls` files; packages that are just bindings to native C libraries (image codecs, crypto primitives, database drivers) need engine-level support, not a package
- **Network servers** — HTTP frameworks, WebSocket servers require long-running event loops not suited to Lambda's execution model

---

## Priority Recommendation

| Priority | Package | Impact | Effort |
|----------|---------|--------|--------|
| 1 | **`diagram`** | High — closes the parse→render gap for graph formats | Medium-High |
| 2 | **`dataframe`** | High — makes Lambda a serious data tool, feeds into `chart` | Medium |
| 3 | **`stat`** | Medium-High — complements `dataframe` + `chart` for analytics | Medium |
| 4 | **`table`** | Medium — quick win, most common data display after charts | Low-Medium |
| 5 | **`geo`** | Medium — high wow-factor, elegant math, pairs with `chart` | Medium-High |
