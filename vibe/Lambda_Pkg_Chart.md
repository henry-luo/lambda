# Lambda Chart Package — Design

**Status:** Consolidated working design, 2026-10-08. Captures the current chart
and wordcloud contracts, including specialized extensions. Optional
compatibility expansion and scope boundaries are identified in the final section.

**Scope:** Declarative statistical charts, chart composition, and weighted
word/tag clouds producing SVG. This document replaces the separate chart
proposal, round-two report, and advanced-features proposal. Implementation
plans, module inventories, debugging history, and test reports are omitted.

**Specification linkage:** [D7.2.1–D7.2.4](../doc/Lambda_Formal_Design.md#d72-script-packages)
cover source packages and the shipped namespace;
[S12.1.1v2](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system)
covers pure functions;
[S6.2.2v3–S6.2.3](../doc/Lambda_Formal_Semantics.md#s62-the-lambda-total-order)
cover exact ordering and stability;
[S7.4.2](../doc/Lambda_Formal_Semantics.md#s74-the-three-failure-channels)
covers raised validation errors. Chart-specific choices below remain working
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

The design follows these principles:

- **Declarative and composable.** A specification describes the desired
  visualization; small charts combine into richer views and dashboards.
- **Functional.** Rendering returns a value, without writing files or mutating
  a live document. Data can be supplied directly or read from a declared source; export remains
  procedural (S12.1.1v2).
- **Progressive detail.** Inferred scales, axes, legends, and styling make simple
  charts concise; explicit settings refine the presentation.
- **SVG output.** The result can be serialized, embedded in HTML or another
  Lambda document, displayed by Radiant, or exported through the rendering CLI.
- **Static presentation.** Data-driven highlighting and SVG hover titles fit
  this model. Reactive signals, brushing, zooming, and event-driven selections
  are outside the present design.

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
| `lambda.chart.chart` → `render_spec(spec, viewport = null)` | Render a specification map; native elements and the same optional viewport are accepted |
| `lambda.chart.vega` → `convert(vl)` | Adapt a parsed Vega-Lite map to a chart specification map |
| `lambda.chart.wordcloud` → `layout(words, opts = null)` | Return resolved cloud options, placed words, and unplaced words; raised-error return `map^` |
| `lambda.chart.wordcloud` → `render(words, opts = null)` | Return a cloud SVG element; raised-error return `element^` |

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

The adapter remains experimental. Field predicates, static expression strings,
and the transform vocabulary below are converted and applied. Pure Lambda
callbacks can also express calculations. Unknown expressions, functions, and
transforms produce value-error diagnostics (S7.4.1). Full Vega-Lite
compatibility remains outside the contract (§10).

## 4. Marks and chart families

Marks describe how records become visible. A chart family can be a primitive
mark, a mark with particular encodings, or a composition of marks.

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
mark-specific; compatibility boundaries are recorded in §10.

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
map tiles, and interactive navigation are outside this contract.

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
§10 records compatibility boundaries beyond this vocabulary.

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

Static conditions refine a channel's fallback definition per record. A
condition may supply a field predicate or a pure Lambda `test` callback
(S11.4.11). Field predicates support `equal`, `gt`, `gte`, `lt`, `lte`,
`range`, `oneOf`, and `valid`, with `and`, `or`, and `not` composition.
An array of conditions selects the first matching case. Conditions can
supply constants or field values for position, color, opacity, size,
stroke, and shape. `test` also accepts a static Vega expression string.

Static expressions bind `datum` to the current record. They support nested
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
| `calculate` | `as`, `op`, `field`, optional `field2`, or `expression` as a static Vega string or pure Lambda callback; copy, convert, or combine fields |
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

## 10. Outstanding capabilities and scope boundaries

As of 2026-10-08, wordcloud markup integration, position-scale controls,
encoding aggregation/binning, multi-field flattening, horizontal and ranged
marks, point symbols, static predicates, shared facet domains, repeat color
consistency, basic guide controls, continuous-color legends, theme cascading,
window calculations, lookup joins, density estimates, fitted trends, and
waterfall composition are implemented. Independent layer scales, layered
stack extents, size/shape legends, arc-legend placement, aggregate categorical
sorting, broader numeric formats, midpoint-aware color, calendar ticks,
time-unit grouping, composite styles/tooltips, shaded regions, and plot
clipping are also supported. Font-based guide measurement, pixel-width label
limits, truncation at grapheme boundaries, and label collision checks across
guides and annotations are supported. Mark gradients, radial gradients, hatch
patterns, and their composition/legend integration are also supported.
Wiggle baselines and inside-out streamgraph ordering are supported.
Static expression-string filters, calculations, and highlight conditions,
plus join aggregates, pivots, imputation, explicit stacks, and quantiles,
are supported.
Container input, automatic/discrete-step dimensions, aspect ratios, and
multi-view size allocation are supported.
Named time zones and daylight-saving-aware calendar ticks, grouping, and
labels are supported.
Scale, guide, dataflow, and configuration resolution across nested views are
supported.
Violin densities, slope comparisons, variable-width trails, and positioned
images are supported.
Radar, parallel coordinates, treemaps, sunbursts, ranged radial arcs, and
GeoJSON charts with all five adopted projections are supported.
No adopted chart capability remains unimplemented. Compatibility may be
broadened beyond the explicitly documented static subset:

### Optional compatibility expansion

| Area | Remaining capability |
|---|---|
| Vega-Lite compatibility | Additional expression helpers and transform options beyond the documented static subset; full Vega-Lite conformance is not promised |

These charts retain declarative input and SVG output. Event-driven
selections, a Vega signal runtime, force simulation, and Voronoi/contour
transforms were not adopted into the present chart scope and are not implied
by this list.
