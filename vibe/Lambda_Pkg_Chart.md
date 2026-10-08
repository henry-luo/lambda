# Lambda Chart Package — Design

**Status:** Consolidated working design, 2026-10-08. Captures the current chart
and wordcloud contracts; incomplete capabilities and proposed extensions are
identified in the final section.

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
| `lambda.chart.chart` → `render(chart_el)` | Render a native `<chart>`, `<hconcat>`, `<vconcat>`, or `<repeat>` specification to an SVG element |
| `lambda.chart.chart` → `render_spec(spec)` | Render a specification map; native elements are also accepted |
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

The adapter remains experimental. Field predicates, aggregation, binning,
folding, and flattening are converted and applied. Static Lambda callbacks
can express calculations when constructing a specification as Lambda values.
Vega expression strings and unknown transforms produce a diagnostic rather
than silently changing the meaning of a chart. Full Vega-Lite compatibility
remains outside the contract (§10).

## 4. Marks and chart families

Marks describe how records become visible. A chart family can be a primitive
mark, a mark with particular encodings, or a composition of marks.

| Mark or family | Design contract |
|---|---|
| `bar` | Vertical or horizontal categorical comparisons; grouped vertical bars use `x_offset`; ranges use `y`/`y2` or `x`/`x2` |
| `line` | Connect ordered observations; color or `detail` distinguishes series; optional points, step interpolation, or smooth cardinal curves |
| `area` | Fill under a series, between y/y2 endpoints, or between stacked boundaries; uses the line interpolation vocabulary |
| `point` | Scatter and bubble plots; circle, square, diamond, triangle-up, triangle-down, and cross symbols |
| `arc` | Pie slices proportional to `theta`; `inner_radius` produces a donut |
| `rect` | Heatmap cells or rectangular ranges defined by x/x2 and y/y2, typically colored by a quantity |
| `text` | Labels positioned by x/y, with content from the `text` channel |
| `rule` | Horizontal or vertical reference lines and segments between x/y and x2/y2 endpoints |
| `tick` | Short marks at observations |
| `boxplot` | Per-category median, quartiles, whiskers, and outliers; the default whisker fence is 1.5 times the interquartile range |
| `errorbar` | Capped interval between supplied y/y2 bounds |
| `errorband` | Filled interval between supplied y/y2 curves |
| `wordcloud` | Weighted words with measured text layout; `text`, `size`, and optional `color` encodings; see §9 |
| Histogram | Bin a quantitative x field and count observations per bin, displayed as bars |
| Candlestick/OHLC | Layer a high–low rule with an open–close bar; color can distinguish rising and falling values |

Point size represents area in square pixels. Mark appearance includes color,
opacity, stroke, stroke width, and mark-specific properties such as bar width
and corner radius, line points, arc radii and padding, and text font size.
Encoding values refine the corresponding default appearance. Support is
mark-specific; the remaining styling and channel gaps are recorded in §10.

### Stacking and grouping

Bar and area charts with a color grouping default to zero-based stacking,
unless `x_offset` requests grouped bars or stacking is explicitly disabled.
The y channel selects the stack mode:

| `stack` setting | Meaning |
|---|---|
| `"zero"` | Accumulate each group from a zero baseline |
| `"normalize"` | Express each stack as proportions totaling one |
| `"center"` | Center each stack around its midpoint |
| `false` or `"none"` | Disable stacking |

An omitted setting permits automatic stacking; `null` is not an explicit
opt-out. Grouped bars compare series side by side within each x category.
Ranged bars describe two endpoints rather than a zero baseline and do not
automatically stack. Horizontal bars use a quantitative x channel and a
categorical y channel. Positive and negative values accumulate on opposite
sides of zero; normalization divides by the total absolute contribution.
The `order` encoding controls record/drawing order; an explicit color-domain
or color-sort array controls series order within a stack.

## 5. Encodings, scales, and color

An encoding binds a field and its data classification to a visual channel.
Its design vocabulary also includes constant `value`, literal `datum`, title,
format, sort order, scale, guide settings, aggregation, binning, stacking, and
conditions. Parsing a setting does not guarantee full rendering support;
§10 identifies incomplete parts of this vocabulary.

| Channel | Meaning |
|---|---|
| `x`, `y` | Cartesian position |
| `x2`, `y2` | Secondary endpoints for ranged geometry |
| `x_offset` | Subgroup position within a categorical x band |
| `color` | Categorical or quantitative color; also distinguishes series |
| `size` | Quantitative symbol area |
| `opacity` | Quantitative transparency |
| `stroke` | Stroke-color encoding |
| `shape` | Point-symbol shape |
| `order` | Record and drawing order, also supplying stack series order |
| `theta` | Angular share for arc marks |
| `text` | Label content |
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
explicit domain-order array.

### Color

Categorical color maps discrete values to a palette; quantitative color maps a
numeric extent through a sequential palette. An explicit color domain/range
pair assigns chosen colors to chosen categories.

Current named palettes include `category10` (the default categorical palette,
Tableau 10), `category20`, `set1`, `pastel1`, `dark2`, `blues`, `greens`, `reds`,
`oranges`, `purples`, `greys`, `red_blue`, and `spectral`. Quantitative color
defaults to `blues`. A diverging palette name alone does not establish an
explicit midpoint-aware diverging scale; that extension remains outstanding.

Static conditions refine a channel's fallback definition per record. A
condition may supply a field predicate or a pure Lambda `test` callback
(S11.4.11). Field predicates support `equal`, `gt`, `gte`, `lt`, `lte`,
`range`, `oneOf`, and `valid`, with `and`, `or`, and `not` composition.
An array of conditions selects the first matching case. Conditions can
supply constants or field values for position, color, opacity, size,
stroke, and shape. General Vega expression strings remain outstanding (§10).

## 6. Data transformations

A `<transform>` contains operations applied in source order before visual
encoding. This order matters: filtering before aggregation answers a different
question from filtering the aggregate result.

| Operation | Specification and meaning |
|---|---|
| `filter` | `field`, `op`, `value`, or a `test` using the condition predicate vocabulary; retain matching rows |
| `sort` | `field`, `order`, each a scalar or parallel array, or `sort: [{field, order}, ...]`; stable ordering with mixed directions |
| `aggregate` | `<group field: ...>` and `<agg op: ..., field: ..., as: ...>` children; produce grouped summaries |
| `calculate` | `as`, `op`, `field`, optional `field2`, or a pure `expression` callback; copy, convert, or combine fields |
| `bin` | `field`, optional `as`, `maxbins`, `step`; discretize a continuous field into intervals |
| `fold` | `fields`, optional `as`; turn wide records into key/value records, defaulting to `key` and `value` |
| `flatten` | `fields`, optional `as`; zip array-valued fields into rows, extending to the longest array and padding shorter fields with null |
| `window` | `groupby`, `sort`, `frame`, `ignore_peers`; `<agg op, field, as, param>` children add ordered calculations while preserving source row order |
| `lookup` | `field` (or `lookup`), `from: {data, key, fields}`, optional `as` and `default`; extend each primary record with matching foreign fields |
| `density` | `field`, optional `groupby`, `bandwidth`, `extent`, `steps`, `as`, `counts`, `cumulative`, `resolve`; emit sampled Gaussian density or cumulative probability records |
| `regression` | `x`, `y`, optional `groupby`, `method`, `order`, `extent`, `steps`, `as`, `params`; emit fitted trend records or model parameters |
| `loess` | `x`, `y`, optional `groupby`, `bandwidth`, `as`; emit a locally weighted trend at each distinct observed predictor value |

Aggregate operations are `count`, `sum`, `mean`/`average`, `median`, `min`,
`max`, `distinct`, `q1`, `q3`, `stdev`, `variance`, `valid`, and `missing`.
Numeric summaries ignore null, nonnumeric, NaN, and infinite observations;
`count` counts rows, and an empty numeric summary is null except for `sum`,
which is zero. Calculate operations
include `+`, `-`, `*`, `/`, `copy`, `string`, `float`, and `int`; arbitrary
computations can instead be written as Lambda expressions before rendering.

The histogram shorthand is `bin: true` on x and `aggregate: "count"` on y.
Encoding-level binning accepts `true` or a map with `step`/`maxbins`.
Encoding-level aggregation applies the listed aggregate operations, grouping
by the other encoded fields and any facet partition fields. The histogram
shorthand retains discrete bin labels and counts.

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
ordinary line/area encodings; a dedicated mirrored violin layout remains
outstanding (§10).

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
shown by the layers rather than implying that different ranges are comparable.

For a facet, the chart's width and height describe each cell. The outer view
includes the cell grid, headers, and spacing. The intended comparison model
uses common x/y and color domains across cells. A chart may request an
independent channel with `resolve: {scale: {y: "independent"}}` (or another
channel name). Facet partitions are retained during aggregation.

Concatenated charts keep their own data, scales, and guides. Nested horizontal
and vertical concatenations build dashboard layouts.

A native repeat has `<row [fields]>` and/or `<column [fields]>` children plus a
`<chart>` template. Its channel fields use `{repeat: "row"}` or
`{repeat: "column"}`. Each row/column combination becomes a view. Color domains are shared across repeated views, and substitution applies to
every channel in the chart template, including wordcloud text and size.
Substitution also reaches nested layer encodings, conditional fields, and
tooltip arrays. Parent transforms run before child transforms; nested layers
inherit data, encoding channels, datasets, and configuration before drawing
their leaves in the shared plot.

## 8. Presentation, guides, annotations, and output

Axes provide a domain line, ticks, labels, and optional field titles.
Per-channel `axis` settings control orientation, domain/tick/label visibility,
explicit `values`, `tick_count`, label angle/limit/overlap, formatting,
typography, and title. `axis: null` or `false` suppresses the guide. Fixed
numeric formats include `.2f` and `.1%`; temporal labels accept datetime
format strings. Calendar-aligned temporal ticks remain outstanding (§10).

Categorical color legends associate labels with symbols; quantitative color
uses a continuous-color bar and endpoint labels. A channel's `legend` map
sets typography and title; Cartesian views support placement at `left`,
`right`, `top`, or `bottom`;
`legend: null` or `false` suppresses it. Horizontal grids can be requested
through `axis_grid: true`; channel axis settings can request or suppress grids.
Size and shape legends and broader legend layout controls remain outstanding.

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
  font size, anchor, and `dx`/`dy` offsets.
- `<rule_note>` with `x` or `y`, color, stroke width, and optional dash pattern.

A `tooltip` channel supplies SVG hover titles for bars, points, rectangles,
rules, lines, and areas. Native `fields: ["name", "value"]` lists several
fields; Vega-Lite tooltip arrays can specify per-field titles and formats.
Text marks use the text channel's constant/field value and format. Wider
coverage across composite marks remains outstanding.

The output is an ordinary SVG element tree with dimensions and a `viewBox`.
Rendering conceptually resolves data and transforms, derives scales and layout,
and combines marks, guides, titles, and annotations. No separate browser
charting runtime is required. Callers serialize standalone SVG with
`format(result, 'xml')`, embed the element in a document, or use `lambda view`
and `lambda render` on a script returning it. File writes remain procedural
(S12.1.1v2).

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

## 10. Outstanding design capabilities — not yet fully implemented

As of 2026-10-08, wordcloud markup integration, position-scale controls,
encoding aggregation/binning, multi-field flattening, horizontal and ranged
marks, point symbols, static predicates, shared facet domains, repeat color
consistency, basic guide controls, continuous-color legends, theme cascading,
window calculations, lookup joins, density estimates, fitted trends, and
waterfall composition are implemented. Nested layer templates and inherited
composition transforms are also supported. The following gaps remain. These are feature
contracts and proposal boundaries, not an implementation roadmap.

### Completion of the existing chart design

| Area | Remaining capability |
|---|---|
| Vega-Lite compatibility | Vega expression-string evaluation, fuller condition/transform conversion, and complete inheritance and resolution controls across nested compositions; full Vega-Lite conformance is not promised |
| Encoding and formatting | Aggregate-based categorical sort definitions; wider numeric formatting beyond fixed-point/percentage formats; uniform style and condition coverage across text, arcs, and composite marks |
| Temporal analysis | Calendar-aligned ticks, UTC/time-zone handling, and time-unit grouping such as year, year-month, weekday, and hour |
| Stack control | Richer stream-graph presentation beyond centered stacks; consistent stacking and shared stack extents in layered charts |
| Composition | Independent layer-scale resolution and full guide/config resolution across nested views |
| Guides | Arc-legend placement; size/shape legends; broader legend direction/columns/symbol controls; comprehensive label collision handling and rotated-label margin measurement |
| Highlighting and annotations | Vega expression-string conditions, tooltip coverage for every composite mark, and shaded-region annotations |
| SVG presentation | Integrated gradient fills, radial gradients, hatch patterns, and plot clipping; continuous-color legend bars do not imply this broader fill contract |
| Color | Explicit midpoint-aware diverging scales and the wider proposed palette set, including viridis/plasma/inferno/magma and additional categorical schemes |
| Responsive sizing | Container-driven or automatic dimensions and aspect-ratio sizing beyond scaling a fixed SVG `viewBox` |

### Proposed specialized extensions

| Extension | Intended design |
|---|---|
| Violin charts | Mirrored per-category density layouts; density curve data is available through the density transform |
| Radar charts | Categorical angular axes and quantitative radius, with closed series, radial guides, and optional filled polygons |
| Treemaps and sunbursts | Hierarchical records identified by node/parent/value fields, shown as proportional nested rectangles or concentric angular partitions; requires hierarchy layout and additional range/radial channels |
| Slope charts | Entity/detail-grouped lines between two comparison positions, including reversed ranking scales |
| Parallel coordinates | One record per polyline across multiple independently normalized quantitative axes, with optional color grouping |
| Trail and image marks | Variable-width paths and positioned images |
| Geographic charts | GeoJSON shapes, projections, and choropleth color encoding; proposed projections include Mercator, equirectangular, Albers, orthographic, and Natural Earth |

These extensions retain declarative input and SVG output. Event-driven
selections, a Vega signal runtime, force simulation, and Voronoi/contour
transforms were not adopted into the present chart scope and are not implied
by this list.
