# Lambda Chart Package — Design

**Status:** Consolidated working design, 2026-10-09. The adopted AntV
G2-inspired expansion is implemented: additional chart families, reusable
coordinates, configurable interaction behaviors, and data-driven animation.
The final section records optional compatibility work and scope boundaries.

**Scope:** Declarative statistical, hierarchical, flow, network, and geographic
charts, weighted word/tag clouds, composable coordinates, and animated
interactive Lambda views, all producing SVG. This document replaces the
separate chart proposal, round-two report, and advanced-features proposal.
Implementation plans, module inventories, debugging history, and test reports
are omitted.

**Specification linkage:** [D7.2.1–D7.2.4](../doc/Lambda_Formal_Design.md#d72-script-packages)
cover source packages and the shipped namespace;
[S12.1.1v2](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system)
covers pure functions;
[S6.2.2v3–S6.2.3](../doc/Lambda_Formal_Semantics.md#s62-the-lambda-total-order)
cover exact ordering and stability;
[S7.4.1](../doc/Lambda_Formal_Semantics.md#s74-the-three-failure-channels)
covers value errors returned for invalid chart specifications.
[S9.1.4](../doc/Lambda_Formal_Semantics.md#s91-the-model),
[S12.1.3](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system), and
[D6.2.3v2](../doc/Lambda_Formal_Design.md#d62-function-values-and-closures) govern
instance-owned template state and procedural event handlers. Chart-specific choices below remain working
design rather than new formal-spec rulings.

## 1. Purpose and design principles

The chart package turns data and a declarative specification into an SVG
element tree. Specifications are ordinary Lambda values, so callers can
construct, transform, compose, and embed charts using the language itself.

Vega-Lite is the primary design reference: data, marks, encodings, scales,
guides, and composition form a compact grammar of graphics. Selected Vega
concepts extend that grammar where needed. Lambda uses native elements and
functional data operations, with a compatibility adapter for Vega-Lite maps.
Full Vega/Vega-Lite compatibility is not a requirement.

[AntV G2](https://g2.antv.antgroup.com/en/manual/introduction/what-is-g2)
is the complementary reference for composable marks and coordinates,
interaction behaviors and visual states, and data-driven animation. Lambda
adopts these concepts through ordinary specifications and Lambda view state;
neither G2 specification compatibility nor its JavaScript rendering runtime
is part of the contract. The original Prior Art section below is preserved.

The design follows these principles:

- **Declarative and composable.** A specification describes the desired
  visualization; small charts combine into richer views and dashboards.
  Reusable composite marks and coordinate transformations expand that vocabulary.
- **Functional.** Rendering returns a value, without writing files or mutating
  a live document. Data can be supplied directly or read from a declared source; export remains
  procedural (S12.1.1v2).
- **Progressive detail.** Inferred scales, axes, legends, and styling make simple
  charts concise; explicit settings refine the presentation.
- **SVG output.** The result can be serialized, embedded in HTML or another
  Lambda document, displayed by Radiant, or exported through the rendering CLI.
- **Declarative interaction.** Vega-Lite parameters describe selection,
  brushing, linked views, input bindings, and pan/zoom. A Lambda view template
  owns each chart instance's state; event handlers update that state and pure
  rendering derives the next presentation (S9.1.4, S12.1.3, D6.2.3v2).
- **Behaviors share parameter state.** Hover, selection, filtering, and their
  visual feedback are configurable behaviors over those same parameters.
  Visual element states are derived from the chart's state and source data.
- **Animation has explicit time and identity.** Stable keys relate data across
  frames. Transitions and keyframes sample an explicit playback state; only
  view event handlers advance live playback (S12.1.1v2, S12.1.3).

Word clouds belong to the chart family because they visualize weighted
categories. They do not imply graph edges, a ranking model, or automatic text
analysis.

## 2. Prior Art

### D3.js

**What it is:** A JavaScript library for producing dynamic, interactive data visualizations via direct DOM manipulation.

**Pros:**
- Extremely flexible — can produce any visualization imaginable
- Fine-grained control over every SVG element
- Massive ecosystem of examples and extensions
- De facto standard for custom web visualizations

**Cons:**
- **Imperative, not declarative** — users write step-by-step DOM manipulation code, not specs
- No schema or markup format to reference; it's a toolkit, not a grammar
- Steep learning curve; requires understanding SVG, selections, joins, transitions
- Tightly coupled to the browser DOM; not suitable as a spec reference

**Verdict:** Excellent as an implementation reference for SVG rendering details (path generators, scale math, axis tick algorithms), but **not suitable as a design reference** for a declarative chart library.

---

### ECharts

**What it is:** A full-featured charting library by Apache with a JSON-based configuration API.

**Pros:**
- Very rich chart type coverage (50+ chart types including 3D, maps, gauges)
- JSON option object — somewhat declarative
- Good default aesthetics and animation
- Strong CJK locale support

**Cons:**
- **Canvas-primary rendering** — SVG support is secondary and incomplete
- The options API is sprawling and inconsistent; hundreds of nested config keys
- Configuration is not formally specified — no JSON Schema, just documentation
- Many features are imperative (event handlers, dynamic updates)
- Tight coupling to its own runtime; the "spec" is not portable

**Verdict:** Feature-rich but the API is too large, inconsistent, and Canvas-oriented to serve as a clean design reference.

---

### Plotly

**What it is:** A charting library (Python/JS/R) with a JSON schema for figure descriptions.

**Pros:**
- Has a formal JSON schema for figures (`plotly.js/dist/plot-schema.json`)
- Good coverage of statistical and scientific chart types
- Multi-language bindings (Python, R, Julia, JS)
- Produces both SVG and Canvas output

**Cons:**
- The schema is enormous (~40,000 lines) — every trace type has its own namespace
- Deeply nested configuration with many implicit defaults
- Mix of declarative layout + imperative trace updates
- Tight coupling to its own rendering engine

**Verdict:** The formal schema is a plus, but the sheer size and trace-centric model make it too heavyweight as a reference.

---

### Chart.js

**What it is:** A simple, popular JavaScript charting library using Canvas.

**Pros:**
- Simple API, easy to learn
- Good defaults and animations
- Lightweight

**Cons:**
- **Canvas-only** — no SVG output
- Limited chart types (8 basic types)
- Imperative JavaScript API, not a declarative spec
- No formal schema

**Verdict:** Too simple and Canvas-only. Not suitable as a reference for a declarative SVG chart library.

---

### Mermaid

**What it is:** A text-based diagramming tool that generates SVG from Markdown-like DSL syntax.

**Pros:**
- Text-based declarative syntax
- SVG output
- Good for flowcharts, sequence diagrams, Gantt charts
- Easy to embed in Markdown

**Cons:**
- **Very limited chart types** — only pie charts and basic XY charts for data visualization
- No formal grammar or schema for chart configurations
- Not designed for data-driven visualization
- No scales, axes, legends, or encoding concepts

**Verdict:** Good inspiration for simple text-to-SVG conversion, but far too limited for a general-purpose chart library.

---

### Vega and Vega-Lite

**What it is:** A pair of declarative visualization grammars from the UW Interactive Data Lab.

- **Vega** is the low-level grammar: explicit scales, axes, marks, signals, layout
- **Vega-Lite** is the high-level grammar: concise specs that compile down to Vega

**Pros:**
- **Rigorously formal** — both have complete JSON Schemas
- **Declarative JSON specifications** — map directly to Lambda's element/map data model
- **Grammar of Graphics foundation** — principled, composable, extensible
- **SVG as primary output** — exactly our target
- **Well-scoped core concepts** (~20 for Vega-Lite): mark, encoding, scale, axis, legend, selection, transform, layer, facet, concat, repeat
- **Excellent documentation** with hundreds of examples
- **Vega-Lite compiles to Vega** — provides a studied reference for how high-level specs lower to concrete rendering instructions
- **Active academic research** — peer-reviewed design decisions

**Cons:**
- Vega (full) is complex — the low-level spec has many concepts (signals, event streams, projections)
- Some features assume a JavaScript/browser runtime (tooltips, interactive selections)
- Vega-Lite's compilation step adds conceptual overhead if studying the full pipeline

**Verdict:** Vega-Lite is the ideal reference. It is the only library that combines:
1. A **formal, published JSON Schema**
2. A **declarative specification format** that maps naturally to Lambda elements
3. **SVG as the primary output target**
4. A **principled Grammar of Graphics design** that is complete yet tractable

---

### Selection: Vega-Lite

We choose **Vega-Lite** as the primary design reference for the Lambda Chart Library.

| Criterion | Vega-Lite | Nearest Alternative |
|-----------|-----------|-------------------|
| Declarative spec format | JSON → Lambda elements | ECharts (JSON, but sprawling) |
| Formal schema | Full JSON Schema | Plotly (too large) |
| SVG output | Primary target | D3 (toolkit, not spec) |
| Chart type coverage | 15+ mark types, compositions | Chart.js (8 types, Canvas) |
| Composability | layer, facet, concat, repeat | ECharts (limited) |
| Design foundation | Grammar of Graphics | — |
| Spec complexity | ~20 core concepts | Vega full (~50 concepts) |

We will **adapt, not clone** Vega-Lite. The Lambda Chart Library will use Lambda's native element syntax instead of JSON, leverage Lambda's functional transforms (pipes, `for`, `where`) instead of Vega-Lite's transform array, and produce SVG elements directly as Lambda element trees rather than going through a separate rendering runtime.

---

## 3. Public interface and input forms

The package ships as Lambda source under `lambda.chart.*` (D7.2.1–D7.2.4).
Its primary interfaces are:

| Interface | Contract |
|---|---|
| `lambda.chart.chart` → `render(chart_el, viewport = null)` | Render a native `<chart>`, `<hconcat>`, `<vconcat>`, or `<repeat>` specification to an SVG element; optional viewport supplies container dimensions |
| `lambda.chart.chart` → `render_spec(spec, viewport = null, st = null)` | Render a specification map or native element; optional parameter-state snapshot derives an interactive SVG presentation |
| `lambda.chart.chart` → `model(spec, viewport = null, options = {})` | Return a chart source element to retain and `apply` across parent rerenders |
| `lambda.chart.chart` → `interactive(spec, viewport = null, options = {})` | Apply a Lambda view template with independent chart interaction state and generated input controls |
| `lambda.chart.vega` → `convert(vl)` | Adapt a parsed Vega-Lite map to a chart specification map |
| `lambda.chart.wordcloud` → `layout(words, opts = null)` | Return resolved cloud options, placed words, and unplaced words; raised-error return `map^` |
| `lambda.chart.wordcloud` → `render(words, opts = null)` | Return a cloud SVG element; raised-error return `element^` |

`chart.render_frame(spec, frame, viewport = null, st = null)`
returns a pure SVG sample of an animated specification (§12). A frame supplies
elapsed `time` in milliseconds and, for a data-update transition, a `previous`
snapshot containing its specification and parameter state, plus the sampled
presentation when interrupting an earlier transition. No wall-clock read
or live playback is implied by this function (S12.1.1v2). The existing `model`
and `interactive` interfaces host behavior and playback state. Their optional
`options` map accepts `reduced_motion: true`; existing calls remain valid.

The user-facing API and examples are in
[Lambda Packages §4](../doc/Lambda_Packages.md#4-chart--charts).

### Native chart specifications

A single-view chart combines a dataset, one mark, and encoding channels:

```lambda
import chart: lambda.chart.chart

let spec = <chart width: 400, height: 300, title: "Sales by Region",
    <data values: [
        {region: "North", sales: 120},
        {region: "South", sales: 85},
        {region: "East", sales: 145}
    ]>
    <mark type: "bar">
    <encoding
        <x field: "region", dtype: "nominal">
        <y field: "sales", dtype: "quantitative">
    >
>

chart.render(spec)
```

Native channels use **`dtype`** for the data classification; a mark uses
**`type`**, with `kind` accepted as an alias. Vega-Lite maps retain their own
`type` keys. These are distinct input forms, not interchangeable spellings
throughout a specification.

| Chart property or child | Meaning |
|---|---|
| `width`, `height` | View dimensions in pixels; defaults are 400 × 300 |
| `padding` | Outer spacing; default 20, or a map with `top`, `right`, `bottom`, and `left` |
| `title` | Chart title |
| `<data>` | Inline `values`, child row elements, a named dataset through `name`, or a file/URL through `url` with optional `format` |
| `<mark>` | Visual representation and default appearance |
| `<encoding>` | Field-to-visual mappings |
| `<transform>` | Ordered data transformations |
| `<config>` | Presentation defaults and theme selection |
| `<layer>`, `<facet>` | Overlay or partition the view |
| `<annotation>` | Explanatory labels and reference lines |
| `params` or `<params>` | Named variables and point/interval selections (§10) |

The following properties configure interaction, coordinates, and animation:

| Chart property or child | Meaning |
|---|---|
| `id` | Stable authored view name for linking behaviors, events, and animation identity |
| `coordinate` or `<coordinate>` | Reusable coordinate family and ordered transformations (§11) |
| `interaction` or `<interaction>` | Named configurable behaviors (§10) |
| `state` | Appearance of default, active/inactive, and selected/unselected elements (§10) |
| `animate` | Enter, update, exit, and group transition policies (§12) |
| `timeline` or `<timeline>` | Keyframe views and playback options (§12) |

A chart may supply a `datasets` map for named sources. Source loading follows
Lambda `input` behavior; loading failures return chart value errors
(S7.4.1–S7.4.2). Rendering and conversion entry points retain their value-error
API, while the direct wordcloud APIs use raised errors.

Records can be maps or row elements. Data fields are selected by name. Callers
may prepare records with Lambda expressions before handing them to the chart;
the declarative transform vocabulary is a convenience, not a separate
expression language.

### Vega-Lite input

`vega.convert` accepts an already parsed map, including one loaded by the
caller with `input`. It adapts inline, named, and file/URL data; marks and
encoding channels; layers, facets, concatenations, and repeats; configuration;
and the supported transform vocabulary.

The adapter remains experimental. Parameters, selection predicates, expression strings,
and the transform vocabulary below are converted and applied. Pure Lambda
callbacks can also express calculations. Unknown expressions, functions, and
transforms produce value-error diagnostics (S7.4.1). Full Vega-Lite
compatibility remains outside the contract (§13). New Lambda extensions are
specified explicitly below; their presence does not make a specification
portable to Vega-Lite or G2.

## 4. Marks and chart families

Marks describe how records become visible. A chart family can be a primitive
mark, a mark with particular encodings, or a composition of marks.
The following table describes the implemented baseline.

| Mark or family | Design contract |
|---|---|
| `bar` | Vertical or horizontal categorical comparisons; grouped vertical bars use `x_offset`; ranges use `y`/`y2` or `x`/`x2` |
| `line` | Connect ordered observations; color or `detail` distinguishes series; optional points, step interpolation, or smooth cardinal curves |
| `area` | Fill under a series, between y/y2 endpoints, or between stacked boundaries; uses the line interpolation vocabulary |
| `point` | Scatter and bubble plots; circle, square, diamond, triangle-up, triangle-down, and cross symbols |
| `arc` | Pie slices proportional to `theta`, or intervals between `theta`/`theta2`; `radius`/`radius2` can specify outer/inner bounds |
| `rect` | Heatmap cells or rectangular ranges defined by x/x2 and y/y2, typically colored by a quantity |
| `text` | Labels positioned by x/y, with content from the `text` channel |
| `rule` | Horizontal or vertical reference lines and segments between x/y and x2/y2 endpoints |
| `tick` | Short marks at observations |
| `boxplot` | Per-category median, quartiles, whiskers, and outliers; the default whisker fence is 1.5 times the interquartile range |
| `errorbar` | Capped interval between supplied y/y2 bounds |
| `errorband` | Filled interval between supplied y/y2 curves |
| `wordcloud` | Weighted words with measured text layout; `text`, `size`, and optional `color` encodings; see §9 |
| `violin` | Mirrored category densities, estimated from measurements or supplied through `density_field`; vertical and horizontal forms |
| `slope` | Entity-grouped lines between two comparison positions, including reversed ranking scales |
| `trail` | Linear paths with widths interpolated between observations, with round joins and caps |
| `image` | URL-backed images positioned by x/y, with optional x2/y2 bounds |
| `radar` | Closed series on categorical angular axes and a quantitative radial scale, with optional fill and radial guides |
| `parallel` | One polyline per record across independently normalized quantitative axes |
| `treemap` | Hierarchical weights represented by proportional nested rectangles |
| `sunburst` | Hierarchical weights represented by concentric angular partitions |
| `geoshape` | GeoJSON points, lines, and polygon regions with a projection and ordinary color encodings; `geo` is an alias |
| Histogram | Bin a quantitative x field and count observations per bin, displayed as bars |
| Candlestick/OHLC | Layer a high–low rule with an open–close bar; color can distinguish rising and falling values |

Point size represents area in square pixels. Mark appearance includes color,
opacity, stroke, stroke width, and mark-specific properties such as bar width
and corner radius, line points, arc radii and padding, and text font size.
Encoding values refine the corresponding default appearance. Support is
mark-specific; compatibility boundaries are recorded in §13.

Mark `fill`, `stroke`, and `color` accept solid colors, linear/radial gradients,
and hatch patterns. Color encodings can supply the same paints as constants,
conditional values, identity-scale field values, or categorical palette entries.
Legend symbols retain their palette paint. Line colors apply to strokes and
optional point overlays; wordcloud colors apply to text. See §8 for the paint
vocabulary.

### Distribution, comparison, and image marks

A violin uses a categorical band axis and a quantitative measurement axis.
`steps`, `bandwidth`, and `extent` control automatic density estimates;
`density_field` instead uses supplied nonnegative densities. `width` is the
full maximum width in pixels, defaulting to the category band. Densities are
normalized independently per category/series by default;
`density_resolve: "shared"` gives them one common density-to-width mapping.
Color and detail distinguish overlaid distributions within a category.

A slope uses exactly two x comparison positions and one observation per
entity at each position. `detail`, or color when detail is absent, identifies
entities. Endpoints follow their displayed x order. Ordinary y scale controls,
including reversal for ranks, apply.

A trail's `size` channel measures width in pixels, with a default scaled range
of `[1, 10]`; a constant mark `size` defaults to two pixels. Widths taper
linearly between observations, and joins and caps are round. One series paints
with one style, using its first observation as ordinary lines do. Size legends
show line widths. Point-area and trail-width scales require independent size
resolution when combined. Trail interpolation is currently linear.

An image gets its URL from the `url` channel, mark `url`, or the row's `url`
field. Without range endpoints, `width`/`height` default to 20 pixels and the
image is centered on x/y. `align` selects `left`, `center`, or `right`;
`baseline` selects `top`, `middle`, or `bottom`. Range endpoints define the
image rectangle regardless of direction. `preserve_aspect_ratio` defaults to
`"xMidYMid meet"`; `aspect: false` stretches to the rectangle. The renderer
emits the URL into SVG; it does not load the image during chart generation.
These marks retain ordinary appearance, tooltips, clipping, and composition.
Invalid dimensions, densities, widths, or incomplete slope pairs return
value errors (S7.4.1).

### Radial and multi-axis charts

A radar requires a nominal/ordinal `theta` field with at least three categories
and a quantitative `radius` field. Each color/detail series supplies exactly
one finite observation per category. Angular domain/sort/reversal and radial
domain/range/reversal apply. Series close back to the first category;
`filled: true` adds polygon fill, with `fill_opacity` defaulting to 0.2.
`labels`, `grid`, and `tick_count` control measured category labels and radial
guides. Empty data is valid; missing/duplicate categories return value errors
(S7.4.1).

A parallel mark declares `fields: ["a", "b", ...]`, with at least two axes.
An entry may instead be `{field, title, scale, axis, format, zero}`. Each field
has its own quantitative scale, excluding zero by default. Rows missing any
finite axis value are omitted before deriving domains. `axes: false` and
`labels: false` hide guides and field headings. Color, opacity, tooltips, and
line interpolation retain their ordinary meanings.

An arc with `theta2` uses start/end angles instead of pie shares. Numeric
angular scales default to `[0, 2π]`; categorical angular scales space their
domain uniformly. A disabled scale uses radians directly. `radius` and
`radius2` select outer and inner radii; a disabled radial scale uses pixels.
Angles must be finite and ordered, and radii satisfy `0 <= inner <= outer`.
Absent radial channels use mark `outer_radius`/`inner_radius`, with the outer
radius defaulting to half the smaller plot dimension. Authored radii are
preserved. Full circles and annuli are valid, including inside layers.

### Hierarchical charts

Treemaps and sunbursts consume flat records. `node_field`, `parent_field`, and
`value_field` default to `"id"`, `"parent"`, and `"value"`; a size encoding's
field supplies the weight field when `value_field` is absent. Node IDs are
unique strings, symbols, or finite numbers. A null/missing parent identifies
a root; multiple roots form a forest. Every non-null parent must identify a
node, and parent cycles are invalid.

Leaf weights are finite and nonnegative, defaulting to one when absent.
Internal weights are the sum of children, without counting the internal
record's own value again. Zero-weight branches have no visible geometry.
Treemaps place larger siblings first and preserve their area proportions;
`node_padding` defaults to one pixel and `header_height` to zero. Sunbursts
preserve sibling input order, divide angles in proportion to weight, and use
equal radial bands for hierarchy levels. `inner_radius` defaults to zero;
`outer_radius` defaults to half the smaller plot dimension.

Original rows supply color and tooltip encodings. Without a color encoding,
each root and its descendants share a palette color. `label_field` defaults
to the node field. `labels: false` hides labels; otherwise measured labels
are truncated for their available width and omitted when they do not fit
their rectangle or radial partition. Treemap headings appear when a positive
`header_height` is supplied. Invalid hierarchies or geometry return value
errors (S7.4.1).

### Geographic charts

`geoshape` accepts GeoJSON FeatureCollections, Features, geometries, or arrays
of records containing a `geometry` field. `geometry_field` changes that field;
`shape: {field, dtype: "geojson"}` is another explicit selection. Feature
properties are directly available to color and tooltip encodings, with
GeoJSON metadata taking precedence on name collisions. Null geometry and
empty coordinate arrays produce no marks. Longitude/latitude channels can
instead construct point geometry from records.

Supported geometry types are Point, MultiPoint, LineString, MultiLineString,
Polygon, MultiPolygon, and GeometryCollection. Positions use finite
longitude/latitude in degrees within ±180/±90; altitude is ignored. Polygon
rings are closed and simple, with exterior first and holes afterward.
Holes are retained regardless of ring winding. Edges interpolate in
longitude/latitude as specified by [RFC 7946 §3.1.1](https://www.rfc-editor.org/rfc/rfc7946#section-3.1.1).
Data crossing the antimeridian should use the split geometry described by
[RFC 7946 §3.1.9](https://www.rfc-editor.org/rfc/rfc7946#section-3.1.9).

The mark or chart `projection` accepts a name or an options map. Supported
names are `"mercator"` (default), `"equirectangular"`, `"albers"`,
`"orthographic"`, and `"natural_earth"` (`"naturalEarth1"` is an alias).
Options include `center: [longitude, latitude]`, positive pixel `scale`,
`translate: [x, y]`, `padding`, positive pixel `precision` (default 0.75), and
Albers `parallels` (default `[29.5, 45.5]`). Defaults fit the world extent to
the plot. An explicit center places that geographic location at the plot
center unless translation is supplied. Orthographic shows the visible
hemisphere; Mercator clips to the conventional square world extent,
approximately ±85.05° latitude. The five families follow their standard
[projection definitions](https://d3js.org/d3-geo/projection).

Geographic paths retain projection-boundary clipping and curved outlines.
Point sizes represent area in square pixels, as for ordinary points. Color
scales provide choropleths; source boundaries support strokes, and opacity,
tooltips, clipping, and composition remain available. Invalid geometry and
projection options return value errors (S7.4.1). TopoJSON, inverse projection,
and map tiles are outside the implemented contract. Projection-parameter
navigation is available through `pan_zoom` (§11).

### Additional chart families

The expanded vocabulary takes inspiration from G2's
[composable marks](https://g2.antv.antgroup.com/en/manual/core/mark/overview)
and [Sankey diagrams](https://g2.antv.antgroup.com/en/manual/extra-topics/graph/sankey).
It adopts the following families in addition to the baseline above:

| Mark or family | Design contract |
|---|---|
| `link` | Connect source and target positions with straight, curved, or orthogonal paths; optional arrowheads and encoded width |
| `polygon` | Closed ordered vertices, with series/detail grouping, holes, fill, stroke, and source-record identity |
| `path` | Authored SVG path geometry with ordinary appearance, tooltips, selection, and keyed animation |
| `vector` | Position plus direction and magnitude, with an arrowhead; supports Cartesian and projected scientific vector plots |
| `sankey` | Directed weighted flows, with proportional node and ribbon widths, ordered stages, labels, and node/link selection |
| `chord` | Weighted relationships arranged around a circle, with proportional node arcs and connecting ribbons |
| `tree` | Parent–child hierarchy with tidy Cartesian or radial placement, orientation controls, and configurable links |
| `pack` | Hierarchical circle packing with area proportional to leaf weight and nested containment |
| `force_graph` | Seeded, bounded graph layout with node/link encodings, optional pinned positions, and deterministic snapshots |
| `funnel` | Ordered stages whose widths represent values; labels and connectors retain each stage's source record |
| `gauge` | A value within an explicit domain, shown by an arc or linear track with optional target, thresholds, and pointer |
| `liquid` | A bounded fill-level chart for a value/domain, with an optional decorative wave driven by explicit animation time |
| Beeswarm | Point marks displaced along a declared categorical dimension to avoid overlap while preserving the measurement coordinate |
| `density` | Two-dimensional density estimates displayed as colored cells or level-set contours; accepts explicit bandwidth, extent, and resolution |

Sankey, chord, and force-graph inputs accept `{nodes: [...], links: [...]}`.
Nodes have unique string, symbol, or finite numeric IDs; links resolve
`source` and `target` IDs, and weighted flows require finite nonnegative
`value`. Field-name options permit alternate schemas. A missing endpoint is
an error. Node and link
encodings refer to their respective original records. Chord inputs may also
use a square nonnegative matrix with an explicit, matching node order.
Sankey stage ordering must be acyclic; cycles are diagnosed rather than
silently broken. Force graphs and chords may contain cycles.

Tree and pack reuse the flat hierarchy input and validation contract above.
Funnel stage values are finite and nonnegative. Input order is preserved
unless an explicit sort is supplied; increasing stages remain increasing
and are not coerced into decreasing values. Gauges and liquid charts require
a finite domain with distinct bounds; `clamp`
controls out-of-domain values. Density inputs require finite x/y values,
positive bandwidth, and positive integer grid dimensions. These failures
use chart value errors (S7.4.1).

Gauge presentation supports `rounded` arc caps, `ticks`, `pointer_shape:
"needle"`, and an optional `pointer_hub`. `tick_count` sets the major scale
divisions and `minor_tick_count` the subdivisions within each division. Scale
labels use the declared domain and measured collision selection. Value,
track, pointer, target, and label parts accept the shared paints and styles,
including gradient threshold colors. `label_unit` adds an explicit display
unit, `target_label` shows the target below the value, and the `label_`,
`tick_`, and `target_label_` font controls style those readouts. Readout spacing
adjusts to the chosen value font.

Density cells and contour levels expose derived records with `density` or
`level`, stable `id` values (`cell:i:j` or `level:value`), and `source_rows`.
Positional fields on cells contain their centers; source data determines the
positional domains. A key field on a density mark identifies these derived
glyphs, allowing selection and animation without assigning every cell to an
arbitrary input sample. Vector direction is in radians; magnitude is in plot
pixels for ordinary coordinates and longitude/latitude units for geographic
vectors. Polygon holes are finite plot-space rings. Authored paths are
validated SVG path text, including curves, arcs, and subpaths.
Arrowheads taper along the final path curvature and meet the shaft centrally.
Their default proportions follow the encoded stroke width; `arrow_size`
requests a head length in plot pixels, limited by the available path length.
The tip stays at the declared endpoint.

Layout is a pure function of data, options, viewport, and an explicit seed
(S12.1.1v2). Force layout uses a declared iteration bound, never elapsed
wall-clock time as a stopping rule. Authored order breaks equal-weight ties
(S6.2.2v3–S6.2.3). Pinned coordinates and saved layout snapshots support
reproducible output. Animated rearrangement uses §12; static rendering does
not start a simulation loop. Density contours expand the previous scope
boundary; Voronoi transforms and general simulation APIs remain excluded.

### Reusable composite marks

A reusable composite is an ordinary pure Lambda function returning a chart
or mark composition (S12.1.1v2). It can be imported and called with data and
options, just like any other package function (D7.2.1–D7.2.4). An explicit
`mark: {kind: "composite", expand: fn_value, options: {...}}` allows a pure
factory to participate where a single mark is expected. Its result is a
finite chart/layer composition; recursive expansion and incompatible
coordinates produce value errors (S7.4.1). There is no mutable global mark
registry (S9.1.4).

Composite marks expose stable named `parts`, such as `node`, `link`, `label`,
`track`, and `pointer`. Each part accepts ordinary encodings, appearance,
visual states, and animation overrides. Generated parts inherit the parent
dataflow and coordinate unless explicitly scoped to a child view. They retain
their source rows, stable keys, and part names for picking, filtering, and
animation. A Sankey ribbon therefore reports its link record, while its node
rectangle reports its node record.

Existing candlesticks, error bands, and boxplots remain convenient families.
The shared composition contract also supports point-plus-line charts,
lollipop charts, bullet charts, and labeled flow diagrams without a separate
rendering or interaction model for every family. Existing radar and parallel
marks remain compatibility conveniences over the coordinate grammar (§11).

### Stacking and grouping

Bar and area charts with a color grouping default to zero-based stacking,
unless `x_offset` requests grouped bars or stacking is explicitly disabled.
The quantitative channel (y for area/vertical bars, x for horizontal bars)
selects the stack mode:

| `stack` setting | Meaning |
|---|---|
| `"zero"` | Accumulate each group from a zero baseline |
| `"normalize"` | Express each stack as proportions totaling one |
| `"center"` | Center each stack around its midpoint |
| `"wiggle"` | Shift an area stack's baseline to minimize weighted layer movement |
| `false` or `"none"` | Disable stacking |

An omitted setting permits automatic stacking; `null` is not an explicit
opt-out. Grouped bars compare series side by side within each x category.
Ranged bars describe two endpoints rather than a zero baseline and do not
automatically stack. Horizontal bars use a quantitative x channel and a
categorical y channel. Positive and negative values accumulate on opposite
sides of zero; normalization divides by the total absolute contribution.
The `order` encoding controls record/drawing order; an explicit color-domain
or color-sort array controls series order within a stack.

`stack_order` on the quantitative channel overrides that series order. It
accepts an array of series values, `"none"` for first appearance, `"reverse"`,
`"ascending"` or `"descending"` by total value, and `"inside_out"`. An array
may be partial: unlisted observed series follow in first-appearance order;
duplicate and unobserved entries do not add series. Ascending totals and
peak-position ties are stable; descending totals reverse the complete
ascending order, including ties (S6.2.3). These controls preserve color and
legend assignments.

Wiggle stacks require area marks with x, quantitative y, and color-group
fields, and finite nonnegative values. Their default order is inside-out
unless an explicit color-domain or color-sort array supplies the order.
Inside-out balances early-peaking series near the center and later-peaking
series toward the outside, following the
[D3 streamgraph model](https://d3js.org/d3-shape/stack#stackOrderInsideOut).
An explicit `stack_order` takes precedence. The initial baseline is zero;
later baselines follow the weighted change rather than centering every
column independently.

Numeric and temporal samples follow ascending x; categorical samples follow
their scale domain or sort order. Each color series shares the observed x
grid. Missing samples have zero thickness, and duplicate series/x samples
add their values while retaining the first record's other attributes.
All-zero columns keep the previous baseline. The y scale covers the resulting
stack endpoints, including negative baseline positions. Layers, facets, and
repeats apply the same stack contract. Invalid modes, orders, or wiggle data
return chart value errors (S7.4.1).

## 5. Encodings, scales, and color

An encoding binds a field and its data classification to a visual channel.
Its design vocabulary also includes constant `value`, literal `datum`, title,
format, sort order, scale, guide settings, aggregation, binning, stacking, and
conditions. Each setting follows its documented mark and scale contract;
§13 records compatibility boundaries beyond this vocabulary.

| Channel | Meaning |
|---|---|
| `x`, `y` | Cartesian position |
| `x2`, `y2` | Secondary endpoints for ranged geometry |
| `x_offset` | Subgroup position within a categorical x band |
| `color` | Categorical or quantitative color; also distinguishes series |
| `size` | Quantitative symbol area, trail width, or hierarchy weight |
| `opacity` | Quantitative transparency |
| `stroke` | Stroke-color encoding |
| `shape` | Point-symbol shape, or a GeoJSON field with `dtype: "geojson"` |
| `order` | Record and drawing order, also supplying stack series order |
| `theta` | Arc share/start angle, or radar category |
| `theta2` | Arc end angle |
| `radius`, `radius2` | Outer/inner radial positions |
| `longitude`, `latitude` | Geographic point coordinates in degrees |
| `text` | Label content |
| `url` | Image resource URL |
| `detail` | Series grouping without assigning a visual appearance |
| `tooltip` | Hover-title content |

**Design additions** extend the channel vocabulary with `position` (an array
of field definitions for parallel/radar coordinates), `key`, and `group_key`
(element correspondence across updates and keyframes). Animation timing
channels are described in §12. Graph marks additionally accept explicit
source/target ID or endpoint field definitions; composite part encodings
select the appropriate node, link, or label records.

### Data classifications and scale behavior

| `dtype` | Meaning | Default position mapping |
|---|---|---|
| `quantitative` | Numeric magnitude | Linear continuous scale |
| `nominal` | Unordered categories | Bands for categorical bars/cells; points for other categorical positions |
| `ordinal` | Ordered discrete values | Categorical positions with an order |
| `temporal` | Dates and timestamps | Continuous time positions with date/time labels |

Scales map a data domain to a visual range. Position scales can be linear,
logarithmic, square-root, power, band, point, ordinal, or temporal. They are
inferred from the channel and mark, and may be overridden with `scale.type`.
Logarithmic scales use a positive domain. Square-root and power scales
preserve the sign of negative values.

Quantitative domains are derived from data, with readable bounds and ticks.
Bar domains normally include zero. Ranged marks consider both endpoints, and
stacked charts consider the full stack extent. Temporal axes choose a readable
date/time label style for the displayed span.

The scale design includes explicit domains and ranges, zero inclusion, nice
bounds, clamping, reversal, rounding, band/point padding, logarithmic base, and
power exponent. Native scale maps use `domain`, `range`, `zero`, `nice`,
`clamp`, `reverse`, `round`, `padding`, `padding_inner`, `padding_outer`,
`base`, and `exponent`. A channel `value` is a visual constant (pixels for
position); a `datum` participates in the data scale. `scale: null` or `false`
uses values directly. Categorical sort may be ascending, descending, or an
explicit domain-order array. A sort map `{field, op, order}` orders categories
by an aggregate of another field; `op` defaults to `sum`. Ties retain first
appearance (S6.2.3), and the order is resolved before encoding aggregation
can remove the sort field.

Temporal scales use calendar-aligned millisecond, second, minute, hour, day,
week, month, or year ticks. Month and year boundaries follow the calendar,
including leap years. `scale.timezone` accepts a fixed UTC offset in minutes
or an IANA zone name such as `"America/New_York"`, defaulting to zero;
`scale.type: "utc"` denotes UTC. Input timestamps with offsets are placed by
their instant (S6.1.1). Zone rules use the pinned IANA 2026e release rather
than the machine's local time-zone setting (S12.1.1v2).

Calendar ticks follow local boundaries: days can span 23 or 25 hours,
nonexistent subday boundaries are omitted, and repeated subday boundaries
appear as distinct instants. A skipped midnight starts its calendar day at
the first valid local time; wholly skipped dates contribute no extra tick.
Grouping resolves ambiguous local times to their earlier occurrence and
shifts nonexistent local times forward by the actual clock change. `Z` and
`ZZ` label tokens show the instant's real offset, including historical
second offsets when needed. Unknown zones return value errors (S7.4.1).

### Color

Categorical color maps discrete values to a palette; quantitative color maps a
numeric extent through a sequential palette. An explicit color domain/range
pair assigns chosen colors to chosen categories.

Named palettes include `category10` (the default categorical palette,
Tableau 10), `category20`, `set1`, `set2`, `pastel1`, `dark2`, `blues`, `greens`,
`reds`, `oranges`, `purples`, `greys`, `red_blue`, `spectral`, `viridis`,
`plasma`, `inferno`, and `magma`. Quantitative color defaults to `blues`.
Palette mapping selects the nearest palette entry.

A quantitative `scale.domain_mid` or a three-value domain specifies a
diverging scale. The two sides map separately to the lower and upper halves
of the palette, so an asymmetric domain still places its explicit midpoint
at the palette center. The midpoint must lie strictly inside the domain.
The default diverging palette is `red_blue_midpoint`, whose center is neutral.
Explicit ranges and reversal work for quantitative and categorical color.
This follows the [Vega-Lite midpoint model](https://vega.github.io/vega-lite/docs/scale.html#domain).

Conditions refine a channel's fallback definition per record. A
condition may supply a field predicate or a pure Lambda `test` callback
(S11.4.11). Field predicates support `equal`, `gt`, `gte`, `lt`, `lte`,
`range`, `oneOf`, and `valid`, with `and`, `or`, and `not` composition.
An array of conditions selects the first matching case. Conditions can
supply constants or field values for position, color, opacity, size,
stroke, and shape. `test` also accepts a Vega expression string. A
`{param: "name", empty: false, value: ...}` condition tests a declared
selection; omitting `empty` makes an empty selection match all records (§10).

Expressions bind `datum` to the current record and declared parameter names
to their current values. Event filters additionally bind `event`; no ambient
state is available. Expressions support nested
dot/bracket field access, number/string/boolean/null/array/object literals,
arithmetic, comparison, bitwise and logical operators, and conditional
`test ? yes : no` or `if(test, yes, no)` expressions. Logical and conditional
branches short-circuit. Missing fields remain distinct from explicit null
within an expression, including `isDefined` and strict comparisons.

Supported helpers cover type checks and conversions; common mathematical
functions such as `abs`, `sqrt`, `pow`, `log`, `min`, `max`, and `clamp`;
string operations such as `lower`, `upper`, `trim`, `slice`, `split`, and
`replace`; and array/object operations such as `length`, `extent`, `inrange`,
`join`, `reverse`, `pluck`, and `merge`. `format` uses the numeric-format
contract shared with guides. This is a deterministic, datum-scoped subset of
the [Vega expression language](https://vega.github.io/vega/docs/expressions/),
with no assignments, method calls, ambient globals, or signals. String
operations use Unicode character boundaries (S2.5.8). Container equality
uses Lambda value equality (S5.4.1). Invalid syntax and unknown
helpers are diagnosed even with empty data; evaluation failures stop the
chart with a value error (S7.4.1). Pure Lambda callbacks retain Lambda's
ordinary semantics (S12.1.1v2).

## 6. Data transformations

A `<transform>` contains operations applied in source order before visual
encoding. This order matters: filtering before aggregation answers a different
question from filtering the aggregate result.

| Operation | Specification and meaning |
|---|---|
| `filter` | `field`, `op`, `value`, or a `test` using the condition predicate vocabulary; retain matching rows |
| `sort` | `field`, `order`, each a scalar or parallel array, or `sort: [{field, order}, ...]`; stable ordering with mixed directions |
| `aggregate` | `<group field: ...>` and `<agg op: ..., field: ..., as: ...>` children; produce grouped summaries |
| `calculate` | `as`, `op`, `field`, optional `field2`, or `expression` as a Vega expression string or pure Lambda callback; copy, convert, or combine fields |
| `bin` | `field`, optional `as`, `maxbins`, `step`; discretize a continuous field into intervals |
| `fold` | `fields`, optional `as`; turn wide records into key/value records, defaulting to `key` and `value` |
| `flatten` | `fields`, optional `as`; zip array-valued fields into rows, extending to the longest array and padding shorter fields with null |
| `window` | `groupby`, `sort`, `frame`, `ignore_peers`; `<agg op, field, as, param>` children add ordered calculations while preserving source row order |
| `joinaggregate` | `groupby`, `joinaggregate: [{op, field, as}, ...]` or `<agg>` children; attach whole-partition summaries to every source row |
| `pivot` | `pivot` (or `field`), `value`, optional `groupby`, `op`, `limit`; turn unique field values into aggregate columns |
| `impute` | `impute` (or `field`), `key`, optional `groupby`, `keyvals`, `method`, `value`, `frame`; fill null values and missing group/key combinations |
| `stack` | `stack` (or `field`), `groupby`, optional `sort`, `offset`, `as`; attach ordered stack start/end fields without replacing source records |
| `quantile` | `quantile` (or `field`), optional `groupby`, `probs`, `step`, `as`; emit empirical probability/value pairs |
| `lookup` | `field` (or `lookup`), `from: {data, key, fields}`, optional `as` and `default`; extend each primary record with matching foreign fields |
| `density` | `field`, optional `groupby`, `bandwidth`, `extent`, `steps`, `as`, `counts`, `cumulative`, `resolve`; emit sampled Gaussian density or cumulative probability records |
| `regression` | `x`, `y`, optional `groupby`, `method`, `order`, `extent`, `steps`, `as`, `params`; emit fitted trend records or model parameters |
| `loess` | `x`, `y`, optional `groupby`, `bandwidth`, `as`; emit a locally weighted trend at each distinct observed predictor value |
| `timeunit` | `field`, `unit`, `as`, optional offset or IANA `timezone`; add a calendar-grouping field while retaining the source field |

Aggregate operations are `count`, `sum`, `mean`/`average`, `median`, `min`,
`max`, `distinct`, `q1`, `q3`, `stdev`, `variance`, `valid`, and `missing`.
Numeric summaries ignore null, nonnumeric, NaN, and infinite observations;
`count` counts rows, and an empty numeric summary is null except for `sum`,
which is zero. Calculate operations
include `+`, `-`, `*`, `/`, `copy`, `string`, `float`, and `int`; arbitrary
computations can instead be written as Lambda expressions before rendering.

Join aggregates preserve source order and fields. Pivot columns use ascending
names; `limit: 0` retains all columns, and `op` defaults to `sum`. Missing
cells follow the aggregate's empty-input contract.

Imputation uses all observed keys plus optional `keyvals`, supplied as an
array or an exclusive-stop `{start, stop, step}` sequence. It preserves source
order and attributes, then appends missing records containing the grouping
and key fields. `method` is `value` (default, with default value zero),
`mean`, `median`, `min`, or `max`. Statistical methods use an inclusive
row-offset `frame` over keys in ascending order, defaulting to the whole
partition. Without grouping, explicit `keyvals` are required to add records.

The stack transform supports `zero`, `center`, and `normalize` offsets,
defaults to `zero`, and preserves source order after sorting the stack
contributions. `as` is two names, or one name plus its `_end` companion;
the default names are `y0`/`y1`. Positive and negative contributions stack
on separate sides of zero. Quantiles ignore nonfinite/nonnumeric values,
retain grouping fields, and default to `prob`/`value` names. Explicit `probs`
lie in `(0,1)`; otherwise `step` (default 0.01) samples from half a step up
to one, excluding one. Invalid transform options return value errors
(S7.4.1).

The histogram shorthand is `bin: true` on x and `aggregate: "count"` on y.
Encoding-level binning accepts `true` or a map with `step`/`maxbins`.
Encoding-level aggregation applies the listed aggregate operations, grouping
by the other encoded fields and any facet partition fields. The histogram
shorthand retains discrete bin labels and counts.

`time_unit` on an encoding performs calendar grouping before aggregation.
Chronological units include `year`, `yearquarter`, `yearmonth`, and
`yearmonthdate`, with optional `hours`, `minutes`, and `seconds` suffixes.
Cyclic units include `quarter`, `month`, `monthdate`, `date`, `day`/`weekday`,
`hours`, `minutes`, and `seconds`, and supported adjacent time combinations.
Cyclic units use a canonical leap year; weekdays run Sunday through Saturday.
An `utc` prefix selects UTC; a map `{unit, timezone}` selects an offset or IANA zone.
Group keys are timestamps, and default labels describe the selected unit.
Unsupported units and invalid offsets return value errors (S7.4.1).

Sorting follows Lambda's exact total order and stability contracts
(S6.2.2v3–S6.2.3). Input order defines the sequence of connected line/area
observations unless the caller or a transform orders them explicitly.

### Ordered calculations and joins

Windows partition by `groupby` fields and use a stable multi-field `sort`.
Supported operations include all aggregate operations plus `row_number`,
`rank`, `dense_rank`, `percent_rank`, `cume_dist`, `ntile`, `lag`, `lead`,
`first_value`, `last_value`, and `nth_value`. `param` supplies a bucket count
for `ntile`, a nonnegative offset for lag/lead (default one), or a zero-based
position for `nth_value`.

A frame uses inclusive row offsets: `[null, 0]` is cumulative, `[-2, 2]`
is a sliding neighborhood, and `[null, null]` covers the partition. The default
is cumulative. Tied sort keys share ranks and expand frame boundaries unless
`ignore_peers: true`; without a sort, rows retain their observed order and
are distinct peers. Ranks and lag/lead use the partition independently of the
frame. These controls follow the supported
[Vega window vocabulary](https://vega.github.io/vega/docs/transforms/window/).
Nonreflexive group keys never coalesce, and their source rows are retained
(S5.1.2).

Lookup is a left join using exact key equality (S5.1.1, S5.2.1v2, S5.4.1).
`from.data` accepts inline values, a named dataset, or a file/URL source.
The shorter `from: {name, key, fields}` form also selects a named dataset.
Foreign `fields` retain their names unless `as` supplies parallel aliases;
omitting `fields` requires one `as` name and embeds the matching record.
Missing matches use `default` (null by default). Duplicate foreign keys select
the first source record. Numeric and string keys remain distinct, and an
existing output field is replaced.

A waterfall chart combines cumulative windows with ranged bars:

```lambda
import chart: lambda.chart.chart

chart.render(<chart width: 420, height: 260,
    <data values: [{step: "Start", change: 10}, {step: "Loss", change: -4},
        {step: "Gain", change: 2}]>
    <transform
        <window ignore_peers: true, <agg op: "sum", field: "change", as: "end">>
        <calculate as: "start", expression: (row) => row.end - row.change>>
    <mark type: "bar">
    <encoding <x field: "step", dtype: "ordinal">
        <y field: "start", dtype: "quantitative"> <y2 field: "end">
        <color value: "green", condition: {field: "change", lt: 0, value: "red"}>>
>)
```

### Distributions and fitted trends

Density returns `value`/`density` fields by default, with group fields retained.
`bandwidth: 0` (the default) estimates bandwidth from the observations;
positive bandwidth supplies it explicitly. `steps` selects an exact uniform
sample count; otherwise a count between `minsteps` (25) and `maxsteps` (200)
is used. An omitted extent uses the observations' range; a constant range
emits one sample. `resolve: "shared"` uses one extent and grid across groups,
which supports stacked density curves. `counts` scales probabilities by the
group count, and `cumulative` returns cumulative probability.

Regression methods are `linear`, `log`, `exp`, `pow`, `quad`, and `poly`;
the longer names `logarithmic`, `exponential`, `power`, `quadratic`, and
`polynomial` are aliases. Polynomial `order` defaults to three. Exponential
and power models fit in log-response space; logarithmic and power models
use log predictors. Nonpositive values are excluded where logarithms require
positive inputs. `params: true` returns `coef` (intercept first) and
`r_squared`, rather than curve points; Vega conversion uses `rSquared`.
An underdetermined or rank-deficient fit returns a diagnostic.

Loess uses local linear fits with distance weights. Its `bandwidth` is the
fraction of observations in a neighborhood, in `(0, 1]`, defaulting to 0.3.
Repeated predictor values share one fitted output. Regression and loess
default to the input `x`/`y` field names; `as` supplies two alternatives.
All three transforms retain grouping fields, ignore nonfinite/nonnumeric
observations, and return an empty array for empty valid input. They feed
ordinary line/area encodings; violin marks also accept precomputed density
curves.

## 7. Chart composition

| Form | Meaning |
|---|---|
| `<layer>` inside a chart | Overlay child charts in one plot, inheriting parent data unless overridden; shared positional scales and guides relate the marks |
| `<facet>` inside a chart | Partition by `field` with wrapping `columns`, or by `row`/`column` field definitions for a grid; `spacing` controls gaps |
| `<hconcat>` | Arrange independent child charts horizontally |
| `<vconcat>` | Arrange independent child charts vertically |
| `<repeat>` | Apply a chart template to row/column field combinations to form a matrix |

Layers support combinations such as line plus point, an uncertainty band plus
line, or candlestick bodies plus whiskers. Shared scales must cover the values
shown by the layers, including both stack endpoints. Each layer applies the
same ordering and stacking policy as a single view.

`resolve: {scale: {y: "independent"}}` gives each layer its own y scale; x,
color, size, and shape can likewise be independent. Independent position
scales have separate axes, alternating sides by default; explicit axis
orientation is honored. Repeated axes on one side receive separate space.
`resolve.axis` and `resolve.legend` can request independent guides while
retaining shared scales. Resolution applies across nested layers, facets,
concatenations, and repeats, after each view's transforms and encoding
aggregation. Shared position domains include range and stack endpoints;
shared color, size, shape, stroke, and opacity domains include participating
views' values. The first participating channel supplies the group's scale
policy, including any explicit domain. Incompatible channel types return a
value error (S7.4.1). Constants and disabled scales do not contribute domains.

An explicit `shared` request reaches descendant views; an inner `independent`
request starts separate groups at that boundary. Independent scales always
have independent guides. Shared guides appear once, in the first eligible
view with that guide enabled; disabled guides and wordclouds do not consume a
legend's place. Multi-view axes and legends remain per view by default;
layers merge them by default. Explicit guide orientation and formatting
come from the representative channel.

For a facet, the chart's width and height describe each cell. The outer view
includes the cell grid, headers, and spacing. The intended comparison model
uses common x/y and color domains across cells. A chart may request an
independent channel with `resolve: {scale: {y: "independent"}}` (or another
channel name). Facet partitions are retained during aggregation.

Concatenated charts keep separate scales and guides by default; explicit
resolution requests share them. Nested horizontal and vertical
concatenations build dashboard layouts.

A native repeat has `<row [fields]>` and/or `<column [fields]>` children plus a
`<chart>` template. Its channel fields use `{repeat: "row"}` or
`{repeat: "column"}`. Each row/column combination becomes a view. Color domains are shared across repeated views, and substitution applies to
every channel in the chart template, including wordcloud text and size.
Substitution also reaches nested layer encodings, conditional fields, and
tooltip arrays. All composition forms inherit data, encoding channels,
datasets, and configuration. Parent transforms run once before child
transforms on inherited records; an explicit child source starts its own
dataflow. Child configuration overrides inherited settings recursively,
preserving explicit zero and false values. Layers draw their prepared leaves
in one plot.

## 8. Presentation, guides, annotations, and output

### Sizing and available space

Numeric `width` and `height` specify the overall SVG viewport. Both rendering
interfaces accept an optional `{width, height}` viewport input. A dimension
set to `"container"` uses the corresponding supplied dimension; without one,
it falls back to 400 by 300. Omitted dimensions also use supplied space,
while explicitly numeric dimensions retain their requested size.

`"auto"` uses available container space when supplied. Otherwise a discrete
position channel receives 20 pixels per category, configurable with
`config.view.step`; a continuous channel uses the 400/300 default.
`{step: number}` requests that many plot pixels per category explicitly.
Cardinality follows transformed data and explicit scale domains. Measured
axes, legends, titles, and padding are added around a step-sized plot.
An `aspect_ratio` derives an automatic or omitted dimension from the other
dimension, after guide space is included. Two explicitly numeric dimensions
take precedence; two explicit discrete-step constraints cannot also impose
an aspect ratio.

Container dimensions trigger fresh mark, scale, guide, label-collision, and
wordcloud layout. A caller supplies updated dimensions when the surrounding
layout changes; the renderer remains a pure function of its inputs
(S12.1.1v2). The returned SVG has the resolved numeric dimensions and `viewBox`.

Concatenations share their available main-axis space among flexible children
after spacing and fixed children are accounted for. Nested concatenations
apply the same rule. Repeat grids divide supplied space into cells.
Facet dimensions describe each cell; the grid reserves the largest rendered
cell extent, including automatically sized guides. Discrete-step requests on
a concat or repeat supply defaults to its cells. Invalid dimensions, steps,
padding, ratios, or impossible container budgets return value errors
(S7.4.1).

```lambda
chart.render_spec({
    width: "container", height: "auto", aspect_ratio: 2,
    data: [{x: 0, y: 1}, {x: 1, y: 3}], mark: {kind: "line"},
    encoding: {x: {field: "x", dtype: "quantitative"},
        y: {field: "y", dtype: "quantitative"}}
}, {width: 640})
```

Axes provide a domain line, ticks, labels, and optional field titles.
Per-channel `axis` settings control orientation, domain/tick/label visibility,
explicit `values`, `tick_count`, label angle/limit/overlap, formatting,
typography, and title. `axis: null` or `false` suppresses the guide. Rotated
labels contribute their measured, rotated bounds to the axis margins. Guide
labels use the resolved font family, size, weight, style, and spacing; emitted
SVG text carries the same settings. `label_limit` fits a label and its ellipsis
into a pixel width, preserving whole grapheme clusters. Combining marks,
emoji sequences, and regional-indicator flag pairs stay together. A limit too
small for the ellipsis produces an empty label. `label_overlap: "hide"` (or
`true`) culls labels using their measured bounds at the actual tick positions,
for horizontal and vertical axes and reversed scales. It retains both endpoint
labels when they fit; `label_separation` sets the minimum gap in CSS pixels.
Within a view, the same overlap policy also checks labels against other axes,
guide titles, legends, the chart title, and text annotations. Independent-axis
offsets and text rotation contribute to these comparisons. Titles, legends,
and labels whose overlap policy is disabled retain their authored content.
Optional annotations take priority over optional axis labels; within each
kind, earlier annotations or guides take priority. Tick marks remain when
their labels are culled. Plot clipping excludes invisible annotation bounds
from collision checks.

Temporal labels accept datetime format strings; `hh`/`h` use 24-hour time and `HH` uses
12-hour time, following the [datetime contract](Lambda_Type_Datetime.md#formatting).
`tick_count: {interval: "month", step: 1}` requests an explicit calendar interval.

Numeric formats shared by axes, legends, text, and tooltips include fixed-point
`f`, percentage `%`, scientific `e`, significant-digit `g`/`r`, SI-prefix `s`,
and integer `d`. Examples are `,.2f`, `.3e`, `.3g`, and `.3~s`. Supported
modifiers are grouping `,`, a positive sign `+` or leading space, currency `$`,
parenthesized negatives `(`, and fractional-zero trimming `~`. This is a
bounded subset of [D3 numeric formats](https://d3js.org/d3-format), not its
full alignment, padding, locale, or alternate-base grammar.

Categorical color legends associate labels with symbols; quantitative color
uses a continuous-color bar and endpoint labels. A channel's `legend` map
sets typography and title; Cartesian and arc views support placement at `left`,
`right`, `top`, or `bottom`;
`legend: null` or `false` suppresses it. Horizontal grids can be requested
through `axis_grid: true`; channel axis settings can request or suppress grids.
Size and shape legends use the marks' actual scale mappings. Multiple guides
reserve separate space. Symbol legends accept `direction`, `columns`,
`column_padding`, explicit `values`, `format`, `symbol_type`, symbol size,
fill/stroke colors, stroke width, and opacity. Continuous legends support
horizontal or vertical direction, `gradient_length`, and `gradient_thickness`.

Axis labels and titles, legend labels and titles, chart titles, and facet
headings reserve space from actual font metrics and visible glyph bounds,
including italic overhangs and fallback glyphs. Legend rows grow to accommodate
large fonts and symbols. The chart's default font is `"Arial"`; `font` changes
the inherited family. Guide `label_font_family`, `label_font_size`,
`label_font_weight`, and `label_font_style` settings override it; the equivalent
`title_font_*` settings control guide titles. `label_letter_spacing` and
`label_word_spacing` are measured and emitted together.

Measurement follows Radiant's SVG text capabilities. General complex-script
shaping remains a font-engine limitation. Available fonts affect chart geometry;
viewing SVG with a different resolved font can change text extents. The same
font resources are needed for reproducible output. Host measurement is a
read-only function under **D7.4.6** and **S12.1.1v2**; a measurement failure
returns a chart diagnostic under **S7.4.1**.

Configuration supplies background, typography, mark, axis, grid, legend,
and title defaults. Presets are `light`, `dark`, `minimal`, and `presentation`;
`theme` may also be a custom map. The cascade is theme, chart configuration,
general mark defaults, mark-type defaults, then explicit mark and channel
settings. Configuration accepts nested sections such as
`mark: {opacity: 0.5}` and `point: {size: 80}`, or flat names such as
`mark_opacity`, `point_size`, and `axis_label_color`. Explicit zero and false
settings are retained. `font` sets the chart's inherited font family.

Annotations add explanation in data coordinates without adding a data series.
The current native form is `<annotation>` containing:

- `<text_note>` with `x`/`y` or plot-local `px`/`py`, `text`, and optional color,
  font family/size/weight/style, letter/word spacing, anchor, and `dx`/`dy`
  offsets. Text uses measured bounds with the emitted font. Authored notes
  retain their positions; `label_overlap: "hide"` or `true` permits culling
  against fixed guides and other notes, with `label_separation` setting the gap.
- `<rule_note>` with `x` or `y`, color, stroke width, and optional dash pattern.
- `<region_note>` with optional `x`/`x2` and `y`/`y2` bounds, color, opacity,
  stroke, and hover-title `text`. Omitted bounds extend to the plot edges.

A `tooltip` channel supplies SVG hover titles for primitive marks and the
parts of arc, boxplot, errorbar, and errorband marks. Native
`fields: ["name", "value"]` lists several
fields; Vega-Lite tooltip arrays can specify per-field titles and formats.
Text marks use the text channel's constant/field value and format. Composite
parts inherit color, opacity, stroke, and stroke width through the same
cascade as primitive marks. Connected paths use their series' first record
for appearance and tooltip content. Box summaries expose `_q1`, `_q3`,
`_median`, `_min`, and `_max`; outlier titles use the original observation.
Error bands group by detail or color like ordinary areas.

`clip: true` on a chart or mark confines plotted content to the plot rectangle;
axes, legends, and titles retain their own space. Configuration may set
`view: {clip: true}`.

The output is an ordinary SVG element tree with dimensions and a `viewBox`.
Rendering conceptually resolves data and transforms, derives scales and layout,
and combines marks, guides, titles, and annotations. No separate browser
charting runtime is required. Callers serialize standalone SVG with
`format(result, 'xml')`, embed the element in a document, or use `lambda view`
and `lambda render` on a script returning it. File writes remain procedural
(S12.1.1v2).

### Gradient and hatch paints

A gradient uses the [Vega-Lite gradient vocabulary](https://vega.github.io/vega-lite/docs/gradient.html):
`{gradient: "linear" | "radial", stops: [{offset, color, opacity?}, ...]}`.
Stops require nonempty color strings and nondecreasing offsets in `[0, 1]`;
equal offsets give a sharp transition. Optional stop opacity also lies in
`[0, 1]`, with zero preserving transparency.

Linear `x1`, `y1`, `x2`, and `y2` default to `0`, `0`, `1`, and `0`.
Radial paints use the first point and `r1` for the inner circle and the second
point and `r2` for the outer circle; both centers default to `(0.5, 0.5)`,
and the radii to `0` and `0.5`. Radii must be nonnegative with `r1 <= r2`.
Coordinates are relative to each painted shape's bounding box, following
[SVG paint-server coordinates](https://www.w3.org/TR/SVG2/pservers.html).
The optional `spread` is `"pad"` (default), `"repeat"`, or `"reflect"`.

A hatch uses `{pattern: "hatch", color, background?, spacing, angle,
stroke_width, opacity, cross}`. Defaults are gray stripes, a transparent
background, 8 CSS pixels of spacing, a 45-degree rotation of vertical stripes,
1-pixel strokes, full stripe opacity, and `cross: false`. `cross: true` adds
perpendicular stripes. Spacing is positive; stroke width is nonnegative.
Mark opacity applies to the complete painted mark, while hatch opacity applies
only to its stripes. Invalid paint options return a value error (S7.4.1).

```lambda
<mark type: "bar", fill: {gradient: "linear", x1: 0, y1: 1, x2: 0, y2: 0,
    stops: [{offset: 0, color: "#deebf7"}, {offset: 1, color: "#3182bd"}]}>
```

Paints also work for annotation colors/strokes, chart backgrounds and titles,
and explicit legend symbol fills/strokes. They retain their appearance in
layers, facets, repeats, and nested concatenations. Gradient fills are distinct
from the continuous-color legend bar and do not change the scale's mapping.


## 9. Word and tag clouds

Wordcloud supports both `<mark type: "wordcloud">` through the chart grammar
and direct `layout`/`render` APIs through `lambda.chart.wordcloud`.
The markup form maps `text` to the word, `size` to its positive weight, and
optional `color` to its appearance. Without those channels, records use
`text`, `weight`, and optional `color` directly. Mark attributes accept the
layout options below; chart width, height, padding, title, and configuration
supply the surrounding view. It uses no x/y axes or legend.

```lambda
import chart: lambda.chart.chart

chart.render(<chart width: 600, height: 400, padding: 12,
    <data values: [{label: "Lambda", count: 40}, {label: "Charts", count: 15}]>
    <mark type: "wordcloud", shape: "ellipse", rotations: [-30, 0, 30], seed: 17>
    <encoding <text field: "label"> <size field: "count", dtype: "quantitative">>
>)
```

The markup form supports transforms, layers, facets, concatenations, and
repeats. Invalid cloud data or options return a chart value error; the direct
APIs retain their raised-error contract (S7.4.1–S7.4.2). Both forms report
unplaced words with `data-unplaced` on the SVG root.

### Input and sizing contract

Each word record supplies:

| Field | Contract |
|---|---|
| `text` | Nonempty single-line string; phrases and Unicode are supported; no newlines, carriage returns, or tabs |
| `weight` | Finite positive `int`, `i64`, or `float` |
| `color` | Optional nonempty color string overriding the palette |
| `font_size` | Optional finite positive size overriding the weight-derived size, even outside the global size range |
| `font_family`, `font_weight` | Optional per-record font settings |
| `rotation` | Optional finite angle from −180 to 180 degrees overriding the default rotation |
| Other fields | Preserved as caller metadata |

Words are considered in descending weight order. Integer ordering remains
exact, and equal-weight words retain source order (S6.2.2v3–S6.2.3). Default
colors and rotations are assigned by source index, so sorting does not change
those assignments.

Weight-to-font-size mapping can be square-root, linear, or logarithmic. Equal
weights use `max_font_size`. Text dimensions reflect the selected font family,
weight, and size, including per-record overrides.

### Layout options

| Option | Default | Contract |
|---|---|---|
| `width`, `height` | `600`, `400` | Finite positive viewport dimensions; fractional values are preserved |
| `margin` | `8` | Nonnegative inset leaving a positive usable viewport |
| `padding` | `2` | Nonnegative minimum gap between word rectangles |
| `min_font_size`, `max_font_size` | `12`, `64` | Finite positive CSS pixel sizes, with minimum no greater than maximum |
| `size_scale` | `"sqrt"` | `"sqrt"`, `"linear"`, or `"log"` |
| `font_family` | `"sans-serif"` | Nonempty CSS family or family list, excluding declaration/block delimiters |
| `font_weight` | `400` | Integer CSS weight from 100 to 900 |
| `colors` | Tableau 10 | Nonempty array of nonempty color strings |
| `rotations` | `[0]` | Nonempty array of finite angles from −180 to 180 degrees, cycled by source index |
| `shape` | `"rectangle"` | `"rectangle"`, `"ellipse"`, `"circle"`, or `"diamond"` |
| `spiral` | `"archimedean"` | `"archimedean"` or `"rectangular"` search path |
| `seed` | `0` | Integer from 0 to 2147483646; selects repeatable placement variation; zero preserves the default path |
| `step` | `4` | Finite positive spacing in CSS pixels between spiral turns or rectangular grid points |
| `max_steps` | `4000` | Positive integer candidate limit per word |

All dimensions and sizes are in CSS pixels. Viewport dimensions must also fit
the supported signed 32-bit measurement range.

### Placement and result contract

Placement seeks positions outward from the center using the selected spiral.
It keeps measured, rotated text rectangles inside the selected boundary and
separate by the requested padding. All four corners must fit the shape. A
circle uses the smaller viewport dimension. Collision geometry is based on
text rectangles, not individual glyph outlines; angled words may have
overlapping axis-aligned bounds while their oriented rectangles remain apart.

The same records, options, and available fonts produce the same layout.
Seed variation is local to the call and does not consume global randomness.
Different installed fonts may produce different measurements and placements.
The bounded search does not promise an optimal packing or placement of every
word.

`layout` returns resolved options plus `words` and `unplaced` arrays. Placed
records retain their metadata and include:

- Source `index`, resolved color, font size/family/weight, and rotation.
- `x` and `y`, the center of the word's rectangle.
- `width` and `height`, the axis-aligned extent after rotation.
- `text_width`, `text_height`, and `baseline`, the unrotated text metrics.

Unplaced records retain their word information and a `reason`: `"too_large"`
when the word rectangle cannot fit the selected boundary, or `"no_space"`
when the bounded search finds no available position. Fields beginning with
`_` are private and are outside the public result contract.

Empty input is valid. Invalid data, invalid options, and failed text
measurement raise errors; callers must propagate or handle the `T^` return
(S7.4.2). Overflow is a normal layout result rather than a validation error.

`render` uses the same layout contract and returns an SVG containing placed
words with their resolved fonts, rotations, colors, and preserved text. Each
word has a title containing its text and weight. The root carries
`role: "img"`, an accessible word-cloud label, and `data-unplaced` with the
omitted-word count. Callers needing the omitted records use `layout`.

```lambda
import cloud: lambda.chart.wordcloud

cloud.render([
    {text: "Lambda", weight: 40, font_weight: 700, rotation: -15},
    {text: "Documents", weight: 25},
    {text: "Charts", weight: 15}
], {
    width: 600, height: 400, shape: "ellipse",
    rotations: [-30, 0, 30], size_scale: "log", seed: 17
})^
```

## 10. Declarative interactions

The interaction grammar follows [Vega-Lite parameters](https://vega.github.io/vega-lite/docs/parameter.html)
and [selections](https://vega.github.io/vega-lite/docs/selection.html).
`chart.interactive(spec, viewport)` produces a Lambda view. Each applied chart
owns its parameter values, selection stores, and active gesture as template
state. Selected tuples and interval extents retain the identity of their
originating chart view or element. Two applications of the same specification
have independent state (S9.1.4, D6.2.3v2). When a parent template rerenders,
retain `chart.model(spec, viewport)` as a source value and call `apply` on that
same model, so its chart state survives the parent update. The model/template
identity contract is described in [Reactive UI §5](Lambda_Design_Reactive_UI.md#5-instance-state-and-identity);
its durable source-path identity follows RS7.

Only `on` handlers change that state; chart rendering, predicates, expressions,
and data transformations remain pure (S12.1.3). Interaction policy belongs to
the chart package through the native DOM/event interface (D7.2.5). This design
does not require a Vega signal runtime. `render` remains useful for a static
snapshot, including declared initial parameter values.

### Parameters and selections

Native charts accept a `params` array or `<params <param ...>>` child. The
Vega-Lite adapter preserves the same declarations:

```lambda
params: [
    {name: "cutoff", value: 10, bind: {input: "range", min: 0, max: 100}},
    {name: "picked", select: {type: "point", fields: ["category"]}},
    {name: "brush", select: {type: "interval", encodings: ["x"]}}
]
```

Names are unique within one chart instance and must be nonreserved expression
identifiers. Variable parameters use `value` for initialization; an `expr`
parameter derives a read-only value from other parameters. Unknown dependencies
and cycles produce diagnostics (S7.4.1).

| Selection | Contract |
|---|---|
| `point` | Select projected `fields` or `encodings` from hit records; without a projection, retain the record. Click replaces the selection; Shift-click toggles a tuple by default. `toggle: false` disables toggling, and an expression can customize it. |
| `interval` | Brush data-space extents on positional `x`/`y` encodings. Drag creates a brush; dragging inside an existing brush translates it and the wheel zooms it. `translate: false` and `zoom: false` disable those operations. Categorical brushes retain selected domain values. |
| `nearest: true` | A point selection chooses the closest record in the projected positional encodings, including a hover stream such as `on: "pointerover"`. |
| Initial `value` | Point selections accept an array of projected tuples. Interval selections accept a map of encoding names to extents, such as `{x: [2, 6]}`. |
| `clear` | Double-click clears ordinary selections by default. An event stream overrides it; `false` disables clearing. |
| `resolve` | `global` (default) retains one selection across views; `union` matches any nonempty view store; `intersect` matches every nonempty view store. |

Parameter state drives channel conditions, transform filters, and interval
scale domains. Variable predicates use Vega expression truthiness; selection
predicates test record membership. Predicates use `{param: "brush"}` and support `and`, `or`, and
`not`. `empty: false` makes an empty selection match no records. A scale may
use `domain: {param: "brush", encoding: "x"}` or specify the projected `field`.
These dependencies update together when state changes, including linked views
in layers, facets, concatenations, and repeats. Wordcloud words expose their
source records to point selections as ordinary chart elements do.

Expressions see a variable's current value, a point selection's projected
field arrays and `vlPoint.or` tuples, or an interval's projected field extents.
Channel `{expr: "..."}` options, calculations, filters, and conditions use the same
parameter environment.

### Bindings and event streams

[Bindings](https://vega.github.io/vega-lite/docs/bind.html) connect parameter
state to chart-local controls or guides:

- Input bindings generate range, number, text, checkbox, select, or radio
  controls. `min`, `max`, `step`, `options`, `labels`, and `name` customize them.
  Point selections bind individual projected fields; controls preserve the
  original option values, including numbers and booleans.
- `bind: "legend"` connects a point selection with one projected field or
  encoding to categorical legend entries. Direct plot selection is disabled
  unless `on` explicitly restores it.
- `bind: "scales"` connects an interval to continuous positional domains.
  Drag pans and the wheel zooms around the pointer in the scale's coordinate
  system. Discrete scale binding is diagnosed rather than silently coerced.

`on`, `clear`, `translate`, and `zoom` accept event selectors with filters,
comma-separated unions, or event-stream objects. Interval streams support
`[start, end] > move` and object `between` lifecycles. Mouse, pointer, keyboard,
input, change, and wheel events form the supported vocabulary. Captured drag
streams continue beyond a plot boundary; `view:` and `window:` selectors refer
to that chart's event delivery. Event-filter expressions see `event`, `datum`,
and declared parameter values. Unsupported events and timed stream options
produce diagnostics (S7.4.1).

A chart emits `chart_change` with its projected parameter values to the nearest
handling ancestor template. An enclosing template can handle that event for
application-level linking. A `chart_parameter` event delivered to the chart
accepts `param`, `value`, and an optional projected `field` to update a parameter
(either directly or in a custom event’s `detail`). Computed parameters
remain read-only. Native SVG titles are the implemented tooltip presentation.

### Configurable behaviors

G2's [interaction behaviors and events](https://g2.antv.antgroup.com/en/manual/core/interaction/overview)
and [element states](https://g2.antv.antgroup.com/en/manual/core/state)
provide the reference for a convenience layer over Lambda's parameter grammar.
Behaviors describe how input changes a parameter and how that parameter
affects presentation or filtering. They do not introduce a second selection
store or move mutation into rendering (S9.1.4, S12.1.3).

`interaction` is a map of behavior names to options. `false` disables a
behavior; `true` uses its documented default event streams, projection, and
generated chart-local parameter. An explicit `param` names an existing
parameter and enables reuse in predicates, scale domains, and linked views.
Chart defaults are inherited; mark and composite-part options override the
same behavior, preserving explicit false values. The native child form is
`<interaction <element_select param: "picked"> ...>`.

```lambda
params: [
    {name: "hovered", select: {type: "point", fields: ["category"]}},
    {name: "picked", select: {type: "point", fields: ["category"]}},
    {name: "brush", select: {type: "interval", encodings: ["x"]}}
],
interaction: {
    element_highlight: {param: "hovered", group_by: ["category"]},
    element_select: {param: "picked", toggle: "event.shiftKey"},
    brush_filter: {param: "brush", targets: ["detail"], rescale: false}
},
state: {
    active: {stroke: "#222", stroke_width: 2},
    inactive: {opacity: 0.3},
    selected: {fill: "#e15759"},
    unselected: {opacity: 0.5}
}
```

| Behavior | Contract |
|---|---|
| `element_highlight` | Hover/focus updates a point parameter and derives active/inactive appearance; optional grouping by x, color, or explicit fields |
| `element_select` | Activation replaces or toggles a point parameter; supports multiple selection, custom streams, and clear/reset |
| `legend_highlight` | Legend hover/focus highlights records sharing the projected legend value |
| `legend_filter` | Legend activation changes a point parameter used as an inclusion filter, with explicit empty-selection policy |
| `brush_highlight` | A positional interval derives highlighted and dimmed records without filtering the source |
| `brush_filter` | A positional interval filters declared target views; optional rescaling occurs from the filtered result |
| `brush_axis_highlight`, `brush_axis_filter` | Per-field interval constraints for parallel/radar axes; all active constraints must match |
| `pan_zoom` | Gesture and wheel events update the interval/scale or supported projection parameters (§11) |
| `crosshair` | A variable parameter records a displayed cursor or nearest datum position; guides use the view's coordinate |
| `tooltip` | Enable/disable and format source-record hover information; native SVG titles remain the baseline presentation |

Options include stable behavior `id`, `param`, projected `fields`/`encodings`,
`group_by`, `on`, `clear`, `toggle`, `resolve`, `empty`, and target view IDs,
where applicable. Type/projection mismatches are errors (S7.4.1). Explicit
behavior streams override a parameter's default gesture for that behavior;
the same event is reduced once when multiple declarations refer to the same
parameter. Direct parameter predicates continue to work without a behavior.

Default highlights use pointer-over/focus-in and clear on pointer-out/focus-out.
Point selection uses click or Enter/Space activation, Shift toggling, and
double-click clear. Brushes use captured pointer-down/move/up lifecycles;
pan/zoom adds wheel input. Filters target their own view and retain domains
unless configured otherwise. Generated parameters are scoped by view,
mark/part, and behavior identity; an explicit `param` is required to reference
one from an authored expression or another view.

Keyboard activation and focus can use the same normalized event streams as
pointer activation. Stable element keys retain focus through chart updates.
Behaviors address source records or composite parts, so highlighting a flow
node can include its incident links when an explicit relationship is declared.
Wordclouds participate in point/legend behaviors through their source rows;
they do not acquire fictitious positional axes for interval brushing.

### Visual states, filtering, and linking

`state` defines `default`, `active`, `inactive`, `selected`, and `unselected`
style maps on charts, marks, or composite parts. Theme/mark appearance and
encoding conditions form the base. State styles then apply in the fixed
order default, inactive, active, unselected, selected, with later states
overriding only the properties they specify. Hover/focus and selection may
coexist; selected appearance has priority over hover appearance.

State flags are chart/element state projections. Active requires a nonempty
highlight and is the union of applicable matches; inactive requires a
nonempty highlight and no match. Selected/unselected follow the same rule
for selection parameters. Empty parameters leave those visual flags unset;
filter and condition predicates separately honor their declared `empty`
policy. Pure rendering derives these flags from stored parameters and
element keys. DOM classes or style mutations are
not an alternative source of selection truth (S12.1.3).

Behavior filtering creates a derived dataset after the view's declared data
transforms; it does not mutate source data. Membership comes from the
parameter's stored tuples/extents, rather than being reconstructed from
already filtered marks. Filters retain other view constraints and combine
by intersection unless the author declares a logical predicate. Resetting
a behavior restores its parameter's declared initial value and recomputes
its constraint. Clearing empties the selection and applies its declared
empty-selection policy.

`rescale: false` retains pre-behavior domains. With `rescale: true`, domains
follow the filtered data; an active brush keeps its starting coordinate
frame until gesture completion, then applies the new domains. This prevents
the brush from moving its own measurement frame while it is being dragged.

Targets use unique authored view IDs within one chart model. Generated facet
and repeat instances qualify those IDs with partition values or repeated
field names. A bare authored ID addresses its generated instances together;
a qualified ID addresses one instance. The qualified form appends a compact
JSON partition key: `detail["Europe"]` for a single-field facet,
`detail[["Europe",2026]]` for a row/column facet, and
`detail[["sales","profit"]]` for a repeated field pair. Generated instances
retain their identity when the input partition order changes. Compositions can share an explicit
parameter while applying different appearance or
filter effects in each view. Independent chart instances remain isolated;
an enclosing Lambda template links them through chart events and parameter
updates. No module-global selection state or implicit global event bus is
introduced (S9.1.4, D6.2.3v2).

### Interaction events and custom behaviors

The existing `chart_change` and `chart_parameter` contracts remain available.
`chart_interaction` additionally reports a behavior's start, update, end, or
clear phase, including its ID/type, view ID and qualified instance ID, element key, composite part,
source datum, parameter name, previous/current values, and input origin.
Positions include both displayed coordinates and data coordinates where an
inverse is defined. Native input details are normalized as values rather
than requiring a JavaScript event object.

Observers handle these events in an enclosing template's `on` handler
(S12.1.3). `chart_action` delivers an explicit behavior command such as set,
toggle, clear, or reset; playback commands share this entry point (§12).
Commands and pointer/keyboard input use the same parameter validation and
state transitions. A notification is observational and is not automatically
replayed as a command. Unchanged parameter values do not emit another
`chart_change`, avoiding feedback in linked charts.

A custom behavior is a pure function of the normalized event, source data,
parameter snapshot, and coordinate context. It returns proposed parameter
updates and named notifications. The chart's procedural handler validates
and applies those values; effectful application work stays in `on` handlers
(S12.1.1v2, S12.1.3). Behaviors may be imported as ordinary Lambda values;
they cannot retain hidden mutable closure or module state (S9.1.4,
D6.2.3v2). Unsupported streams and invalid commands produce diagnostics
instead of silently falling back.

## 11. Reusable coordinate grammar

The coordinate grammar follows G2's separation of
[scales and coordinate transformations](https://g2.antv.antgroup.com/en/manual/core/coordinate/overview).
A scale maps a data value into a normalized position. A coordinate maps
normalized positions into the plot. This separation lets the same bar, line,
area, point, or link participate in more than one spatial representation.

```lambda
<coordinate type: "polar", start_angle: -1.5707963267948966,
    end_angle: 4.71238898038469, inner_radius: 0.2, outer_radius: 1,
    transform: [{type: "transpose"}]>
```

The map form is `coordinate: {type: "polar", ...}`. Native option names use
snake case. A coordinate is inherited from the enclosing view and may be
overridden at an independent child-view boundary. One layered plot has one
coordinate definition; conflicting mark-level definitions are errors rather
than an implicit first/last-mark choice (S7.4.1). Independent layer scales
remain valid when they share the same spatial coordinate.

### Families, units, and transformation order

| Coordinate | Contract |
|---|---|
| `cartesian` | Default normalized x/y mapping to the plot rectangle; supports horizontal or vertical orientation through transpose |
| `polar` | Angular position and radial position; supports radial bars, polar lines/areas, rose charts, and polar scatter plots |
| `theta` | Polar shorthand with transposed positional roles for proportional angular intervals, such as pie/donut charts |
| `radial` | Radial category bands and angular measurements, including radial comparisons |
| `parallel` | A `position` vector, independently scaled by field, mapped across parallel axes; a line connects each record's dimensions |
| `radar` | At least three ordered angular axes, with independently scaled position-vector values or long-form theta/radius series |
| `helix` | Multiple angular turns with a radial progression and bounded track width, for cyclic time-series displays |
| `geo` | Longitude/latitude passed through the existing geographic projection contract (§4) |

For ordinary two-dimensional coordinates, scales first produce normalized
`u`/`v` values; categorical channels retain their band bounds. Entries in
`coordinate.transform` then apply in authored order, followed by the selected
coordinate family and plot placement. Parallel/radar position vectors retain
one scale per field before their coordinate mapping. Color, size, opacity,
and other nonpositional channels are unaffected by this spatial pipeline.
Explicit coordinate specifications use normalized positional scale ranges;
legacy pixel-range specifications retain their meaning when no coordinate
is supplied. An unknown coordinate or transform is a value error (S7.4.1).

The transformation vocabulary is `transpose`, `reflect_x`, `reflect_y`,
`rotate`, `scale`, `translate`, and `fisheye`. Rotation uses radians around an
explicit normalized center, defaulting to `[0.5, 0.5]`; scale factors and
translation offsets use normalized units. Fisheye specifies normalized focus
and nonnegative distortion. Each transform declares the dimensions it acts
on. Singular transforms cannot be used by behaviors that require an inverse;
invalid parameters and incompatible dimensions are value errors (S7.4.1).

Angular options use radians. Coordinate `inner_radius` and `outer_radius` are
fractions of half the smaller plot dimension, satisfying
`0 <= inner_radius < outer_radius <= 1`. Helix additionally supplies turn
bounds and a positive track width. These normalized coordinate radii are
distinct from existing mark radii and disabled radius scales, whose authored
values remain pixels. Existing arc, radar, parallel, and geographic
specifications preserve their documented meaning when no explicit coordinate
is supplied. Explicit `theta`/`radius` encodings select polar roles directly;
conflicting x/y and theta/radius definitions are diagnosed.

### Geometry, guides, and composition

Ranges retain both endpoints throughout projection. A bar in polar
coordinates becomes an angular/radial region, rather than a Cartesian
rectangle with only its center moved. Lines, areas, ribbons, vector endpoints,
axes, grid lines, and annotations all use the same coordinate definition.
Curved boundaries respect a declared pixel `precision`, including at the
angular seam. Explicit clipping applies to the coordinate's plot boundary.

Helix guides keep tick lengths and text spacing in screen pixels. Progression
labels follow the outer spiral; the compact track range places its endpoints
on opposite sides. Titles clear the plot, and measured label selection shares
one collision space across both axes, retaining endpoints when they fit.
Labels use the chart background as a halo to keep spiral gridlines distinct
from the digits.
`label_overlap: false` retains explicitly requested labels.

Parallel and radar coordinates generate their own field axes and labels from
the same scales as the marks. Field titles stay upright and clear plotted
lines and tick labels with measured spacing, including after coordinate
transforms. Facets/repeats reuse coordinate options while
resolving each cell's viewport. Shared data domains do not force shared pixel
positions across differently sized views. Geographic projection retains its
longitude/latitude units, clipping, and supported projection families.

| Mark family | Coordinate compatibility |
|---|---|
| Bar, line, area, point, tick, text, link, vector, polygon | Compatible two-dimensional families; ranged shapes and connected series preserve their topology |
| Parallel/radar position vectors | Parallel or radar coordinates with matching field definitions |
| Tree | Cartesian layout or radial layout with an explicit polar coordinate |
| Treemap, pack, force graph, wordcloud, positioned images | Cartesian layout space; an incompatible inherited coordinate requires an independent child view |
| Sankey, chord, gauge, liquid | Their declared composite geometry; arbitrary coordinate overrides are diagnosed unless the composite explicitly supports them |
| Geoshape | Geographic projection; ordinary categorical/quantitative color and appearance remain available |
| Authored SVG path | Plot-space geometry by default; `space: "coordinate"` opts into a compatible coordinate mapping |

Changing coordinates preserves source records and stable keys. Selection,
filtering, and transitions therefore refer to data identity rather than the
current position or SVG path string.

### Picking, brushing, and scale binding

Point picking and nearest selection operate on displayed geometry and return
the originating record, element key, and composite part. Coordinate inverses
map interaction positions back to data scales where that inverse is defined.
Distance for nearest selection is measured in displayed pixels.

Cartesian interval brushes follow the transformed axes. Polar brushes use
angular/radial extents and draw a wedge or annulus; seam-crossing angular
extents retain their wrap direction instead of selecting the complementary
region. These are coordinate-aware extensions of the interval parameter
contract, not new mutable selection objects. Parallel/radar axis brushes
store extents by projected field; constraints across axes combine by
intersection. Selection predicates continue to consume those named parameters.

Pan/zoom scale binding requires continuous channels and a supported inverse.
Unsupported combinations, such as treating a packed hierarchy as two
invertible quantitative axes, produce diagnostics (S7.4.1). Geographic
navigation changes declared projection center/scale parameters; it does not
reinterpret longitude/latitude as ordinary Cartesian scale domains. General
geographic region brushing remains outside the adopted contract.

## 12. Animation and timelines

The animation design takes G2's
[enter/update/exit transitions](https://g2.antv.antgroup.com/en/manual/core/animate/overview),
[path morphing](https://g2.antv.antgroup.com/en/manual/core/animate/morphing),
and [keyframe correspondence](https://g2.antv.antgroup.com/en/manual/core/composition/timing-keyframe)
as references. Lambda represents the policy as specification values and owns
playback in a chart view/template instance (S9.1.4, D6.2.3v2).

### Transition declarations and data-driven timing

`animate` is available on charts, marks, and composite parts. `false` disables
animation at that scope; omitted animation preserves immediate rendering.
Chart defaults are inherited, and mark/part settings override them.

```lambda
animate: {
    enter: {type: "fade", duration: 300},
    update: {type: "morph", duration: 500, easing: "ease_in_out"},
    exit: {type: "fade", duration: 200},
    group: {by: ["region"], stagger: 40, order: "ascending"}
}
```

Enter applies to newly present keyed elements, update to surviving elements,
and exit to removed elements. Transition types include `fade`, `grow_x`,
`grow_y`, `scale`, `path_reveal`, `wave`, and `morph`. `delay`, `duration`,
and `easing` control timing. Durations/delays are finite nonnegative
milliseconds. Easing accepts named curves or a pure function over normalized
progress, producing finite progress in `[0, 1]`; endpoints remain exactly
the authored start and target states.
Invalid timing or geometry returns a value error (S7.4.1).

Timing may be a constant, a declared parameter expression, or a pure function
of the source record, its index, and its group. Encoding channels
`enter_delay`, `enter_duration`, `update_delay`, `update_duration`,
`exit_delay`, and `exit_duration` provide field-driven timing. These values
are sampled at the start of a transition so live parameter changes do not
silently reschedule an in-flight animation. A subsequent update starts a new
transition from the currently displayed state.

Group animations use declared grouping fields, stable ordering, and a
nonnegative `stagger`. `mode: "parallel"` advances groups together with their
offsets; `mode: "sequence"` places each group after the previous group's
completion. Explicit per-element delay is added to its group's offset.
Equal-order groups preserve source order (S6.2.2v3–S6.2.3). There is no
implicit random ordering or dependence on callback completion time.

### Identity, interpolation, and morphing

`key` identifies a record within a mark/part and view; duplicate animated
keys are errors. Updates and morphs require an explicit key, while enter-only
animation may use stable source order. `group_key` relates aggregated and
individual records across keyframes. A source/template instance and authored
view/part identity scope these keys; moving a record on screen never creates
a new identity.

Numeric geometry, opacity, compatible transforms, and colors interpolate.
Discrete values switch at a declared boundary. Tick label text switches as a
single string at that boundary, keeping numbers readable as tick positions
move. Animated tick labels use measured glyph bounds to omit labels that
temporarily collide, retaining endpoints when they fit. Other text content
crossfades while retaining font measurement; compatible paints interpolate
their parameters, and incompatible paint kinds crossfade. Scales, guides,
labels, clipping, and annotations sample the same frame as the marks,
preventing a target-state axis from being drawn over source-state geometry.

Path morphing matches corresponding keyed shapes and preserves closed/open
path status, subpaths, winding, and holes. Compatible paths may have different
vertex counts. Incompatible topology or unrelated geometry uses a documented
crossfade fallback, rather than generating self-intersections or inventing
record matches. Explicit `group_key` correspondence supports one-to-many
splits and many-to-one merges; ambiguous many-to-many groups crossfade unless
the author supplies finer keys. Selecting `fallback: "error"` requests a
diagnostic instead.

Interaction visual states refine animated base appearance. Picking uses the
current displayed geometry and the surviving element's current record.
Exiting elements are decorative and do not remain selectable; selection
stores retain their declared values until a handler clears or replaces them.

### Keyframe views and playback

`timeline` contains ordered keyframes and chart-local playback options:

```lambda
timeline: {
    autoplay: true, repeat: 2, direction: "alternate",
    keyframes: [
        {at: 0, spec: overview},
        {at: 1200, spec: detail},
        {at: 2400, spec: comparison}
    ]
}
```

The native form is `<timeline ... <keyframe at: 0, spec: overview> ...>`.
Keyframe times are finite nonnegative milliseconds, start at zero, and
increase strictly. Each keyframe supplies a chart/view specification;
inherited chart data/configuration may be refined there. Keys and group keys
relate marks across adjacent views. A keyframe may change data, encodings,
scales, coordinates, or mark families, subject to the correspondence and
morph-fallback rules above. Recursive timelines are invalid.

Playback supports `play`, `pause`, `resume`, `seek`, `reverse`, and `cancel`.
`repeat` is a positive integer or `"infinite"`; directions are `normal`,
`reverse`, `alternate`, and `reverse_alternate`. An explicit duration may
rescale the timeline while preserving relative keyframe times. Normal repeat
wraps to its starting frame; a smooth return requires an authored closing
keyframe or alternate playback. `fill: "forwards"` retains the terminal
frame by default; `fill: "none"` restores the initial presentation after
completion. Seeking samples the requested timeline position immediately;
pausing preserves it, while cancel restores the initial presentation.

Live playback time, phase, previous/target snapshots, and paused position are
chart template state. Native frame events advance that state through `on`
handlers; rendering only samples the supplied state/time (S12.1.3).
`chart_action` accepts playback commands, and `chart_animation` reports
start, pause, resume, seek, end, and cancel events. Per-frame updates do not
emit parameter `chart_change` events. One chart-owned timeline synchronizes
its marks and linked child views; independent charts have independent clocks.

A new data update interrupts from the visible frame without jumping back to
an earlier source. Stable source models retain playback across parent
rerenders; removing the chart cancels it. Reduced-motion mode resolves to an
immediate target frame or an explicitly requested short fade. It never
changes the target data or selection meaning.

Static `render`/`render_spec` return a stable target snapshot and do not start
playback; a timeline defaults to its last declared keyframe, independent of
repeat/direction, including infinite playback. `render_frame` samples
an explicit time, with a previous specification/state snapshot when a data
update must be interpolated. Given the same inputs and time, it returns the
same SVG (S12.1.1v2). SVG/PDF/raster exports are therefore frozen samples;
exporting a frame sequence requires an explicit sequence of sampling times.
No browser animation runtime or ambient wall clock is required for export.

## 13. Outstanding capabilities and scope boundaries

The adopted expansion in §§4 and 10–12 is implemented. No capability from
that proposal remains deferred. Static SVG export, live Lambda view state,
wordcloud, and the original chart grammar share the same public chart entry
points. Optional work below extends compatibility or presentation beyond the
adopted contract.

### Optional compatibility expansion

| Area | Remaining capability |
|---|---|
| Vega-Lite compatibility | Additional expression helpers and transform options beyond the documented subset; full Vega-Lite conformance is not promised |
| Event streams | Debounce/throttle timers, CSS-targeted streams, and general document/window subscriptions outside chart-captured gestures |
| Input binding | Binding to pre-existing external DOM controls through `element` selectors |
| Tooltip presentation | Rich HTML or cursor-following tooltip overlays beyond SVG titles |

These charts retain declarative input and SVG output. Full Vega/Vega-Lite or
G2 specification conformance, a Vega signal runtime, G2 JavaScript plugins,
alternate Canvas/WebGL chart backends, 3D scenes, general simulation APIs,
and Voronoi transforms remain outside the adopted chart scope. The bounded
force-graph and density-contour chart families above supersede the earlier
exclusion of force and contour charts.
