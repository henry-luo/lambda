# Lambda Chart Library — Design Proposal

## Table of Contents

1. [Overview](#overview)
2. [Prior Art](#prior-art)
   - [D3.js](#d3js)
   - [ECharts](#echarts)
   - [Plotly](#plotly)
   - [Chart.js](#chartjs)
   - [Mermaid](#mermaid)
   - [Vega and Vega-Lite](#vega-and-vega-lite)
   - [Selection: Vega-Lite](#selection-vega-lite)
3. [Design of the Lambda Chart Library](#design-of-the-lambda-chart-library)
   - [Design Philosophy](#design-philosophy)
   - [Architecture](#architecture)
   - [Pipeline](#pipeline)
   - [Module Structure](#module-structure)
   - [Usage Examples](#usage-examples)
4. [Schema of Key Chart Elements](#schema-of-key-chart-elements)
   - [Top-Level Chart Element](#top-level-chart-element)
   - [Data](#data)
   - [Mark](#mark)
   - [Encoding](#encoding)
   - [Scale](#scale)
   - [Axis](#axis)
   - [Legend](#legend)
   - [Transform](#transform)
   - [Composition](#composition)
   - [Config (Theming)](#config-theming)
5. [SVG Generation](#svg-generation)
6. [Implementation Roadmap](#implementation-roadmap)

---

## Overview

The Lambda Chart Library (`chart`) is a **pure Lambda Script library** that transforms declarative chart specifications — expressed as Lambda elements — into SVG output. The library leverages Lambda's native element syntax, functional data transformations, and the existing `format(data, 'svg)` output pipeline.

**Goals:**
- Declarative chart specification using Lambda elements and maps
- Produce well-formed SVG output suitable for display, embedding, and PDF export
- Cover common chart types: bar, line, area, point/scatter, arc/pie, and compositions
- Pure functional implementation — no procedural side effects in the chart engine
- Incremental design — start simple, extend toward full Grammar of Graphics coverage

---

## Prior Art

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

## Design of the Lambda Chart Library

### Design Philosophy

1. **Lambda-native:** Chart specifications are Lambda elements and maps — first-class data in the language, not external JSON or a DSL string
2. **Functional pipeline:** The chart engine is a pure function: `chart spec → SVG element tree`
3. **Composable grammar:** Follow the Grammar of Graphics: data → transforms → scales → marks → guides (axes/legends)
4. **Progressive complexity:** Simple charts require minimal spec; advanced charts unlock more options
5. **SVG-first:** Output is an SVG element tree that can be serialized with `format(chart, 'html)`, embedded in documents, or rendered via Radiant

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Lambda Script                         │
│                                                          │
│  let my_chart = <chart width: 600, height: 400;         │
│      <data values: sales_data>                           │
│      <mark type: "bar">                                  │
│      <encoding;                                          │
│          <x field: "category", type: "nominal">          │
│          <y field: "amount", type: "quantitative">       │
│      >                                                   │
│  >;                                                      │
│                                                          │
│  let svg = chart.render(my_chart);                       │
│                                                          │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│               Chart Library Pipeline                     │
│                                                          │
│  1. Parse & Validate   ─── schema check on elements      │
│  2. Data Resolution    ─── resolve data values/url       │
│  3. Transform          ─── aggregate, filter, calculate   │
│  4. Scale Inference    ─── auto-detect scales from data   │
│  5. Scale Construction ─── domain → range mapping         │
│  6. Mark Generation    ─── data points → SVG primitives   │
│  7. Guide Generation   ─── axes, legends, titles          │
│  8. Layout             ─── position all components        │
│  9. SVG Assembly       ─── compose final SVG element      │
│                                                          │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│                  SVG Element Tree                        │
│                                                          │
│  <svg width: 600, height: 400, xmlns: "...";            │
│      <g class: "axes"; ...>                              │
│      <g class: "marks"; ...>                             │
│      <g class: "legend"; ...>                            │
│  >                                                       │
│                                                          │
│  → format(svg, 'html)  → string                         │
│  → format(svg, 'svg)   → string                         │
│  → render via Radiant   → display                        │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### Pipeline

The chart rendering pipeline consists of these stages, each implemented as a pure function:

| Stage | Input | Output | Description |
|-------|-------|--------|-------------|
| **Parse** | `<chart>` element | Validated chart spec map | Extract and validate attributes from the element tree |
| **Data resolve** | Data spec | Array of records | Load inline values, or reference external data |
| **Transform** | Records + transform specs | Transformed records | Filter, aggregate, calculate, bin, sort |
| **Scale inference** | Encoding + data | Scale definitions | Auto-detect scale types from data and encoding types |
| **Scale build** | Scale defs + data | Scale functions | Build domain→range mapping functions (linear, ordinal, time, log, etc.) |
| **Mark render** | Data + scales + mark spec | SVG mark elements | Map each data point to SVG primitives (rect, circle, path, line, text) |
| **Guide render** | Scales + axis/legend spec | SVG guide elements | Generate axis lines, ticks, labels, legend entries |
| **Layout** | All rendered groups | Positioned groups | Apply padding, position axes around the plot area |
| **Assemble** | All SVG groups | `<svg>` element | Wrap in SVG root with dimensions and metadata |

### Module Structure

```
lambda/package/chart/
├── chart.ls              # Main entry point: chart.render(spec) → SVG
├── parse.ls              # Parse and validate <chart> element tree
├── transform.ls          # Data transforms (aggregate, bin, filter, calculate, sort, fold, flatten)
├── scale.ls              # Scale construction (linear, ordinal, log, sqrt, band, point)
├── mark.ls               # Mark renderers (bar, line, area, point, arc, text, rule, tick)
├── axis.ls               # Axis generation (ticks, labels, gridlines)
├── legend.ls             # Legend generation (categorical and gradient)
├── layout.ls             # Layout and positioning (cartesian and arc)
├── color.ls              # Color palettes and schemes (category10, sequential, diverging)
├── svg.ls                # SVG element construction helpers and path generators
└── util.ls               # Math utilities (nice numbers, tick intervals, etc.)
```

### Usage Examples

#### Simple Bar Chart

```lambda
import chart: .lambda.package.chart.chart;

let sales = [
    {category: "A", amount: 28},
    {category: "B", amount: 55},
    {category: "C", amount: 43},
    {category: "D", amount: 91},
];

let my_chart = <chart width: 600, height: 400, title: "Sales by Category";
    <data values: sales>
    <mark type: "bar">
    <encoding;
        <x field: "category", type: "nominal">
        <y field: "amount", type: "quantitative">
    >
>;

let svg = chart.render(my_chart);

// Output as HTML string
print(format(svg, 'html))
```

#### Line Chart with Multiple Series

```lambda
let temps = [
    {month: "Jan", city: "NYC", temp: 32},
    {month: "Feb", city: "NYC", temp: 35},
    {month: "Jan", city: "LA",  temp: 58},
    {month: "Feb", city: "LA",  temp: 61},
    // ...
];

let my_chart = <chart width: 700, height: 400;
    <data values: temps>
    <mark type: "line">
    <encoding;
        <x field: "month", type: "ordinal">
        <y field: "temp", type: "quantitative", title: "Temperature (°F)">
        <color field: "city", type: "nominal">
    >
>;
```

#### Pie Chart

```lambda
let shares = [
    {segment: "Desktop", share: 62},
    {segment: "Mobile",  share: 31},
    {segment: "Tablet",  share: 7},
];

let my_chart = <chart width: 400, height: 400, title: "Device Share";
    <data values: shares>
    <mark type: "arc">
    <encoding;
        <theta field: "share", type: "quantitative">
        <color field: "segment", type: "nominal">
    >
>;
```

#### Layered Chart (Line + Point)

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: time_series>
    <layer;
        <chart;
            <mark type: "line">
            <encoding;
                <x field: "date", type: "temporal">
                <y field: "price", type: "quantitative">
            >
        >
        <chart;
            <mark type: "point">
            <encoding;
                <x field: "date", type: "temporal">
                <y field: "price", type: "quantitative">
            >
        >
    >
>;
```

#### Faceted Chart

```lambda
let my_chart = <chart width: 200, height: 200;
    <data values: iris_data>
    <mark type: "point">
    <encoding;
        <x field: "sepal_length", type: "quantitative">
        <y field: "sepal_width", type: "quantitative">
        <color field: "species", type: "nominal">
    >
    <facet field: "species", type: "nominal", columns: 3>
>;
```

#### Chart with Transforms

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: raw_events>
    <transform;
        <filter expr: "~.status == 'active'">
        <aggregate;
            <agg op: "count", as: "num_events">
            <group field: "region">
        >
        <sort field: "num_events", order: "descending">
    >
    <mark type: "bar">
    <encoding;
        <x field: "region", type: "nominal", sort: "-y">
        <y field: "num_events", type: "quantitative">
    >
>;
```

---

## Schema of Key Chart Elements

This section defines the schema for each chart element. Attributes are listed with their types and defaults. The notation follows Lambda's type system.

### Top-Level Chart Element

The `<chart>` element is the root of every chart specification.

```lambda
<chart
    // --- Dimensions ---
    width: int = 400,             // chart width in pixels
    height: int = 300,            // chart height in pixels
    padding: int | {top: int, right: int, bottom: int, left: int} = 20,

    // --- Metadata ---
    title?: string | <title>,     // chart title (string or element for formatting)
    description?: string,         // accessible description

    // --- Children (order-independent) ---
    ;
    <data ...>                    // data specification (required)
    <mark ...>                    // mark type (required for single-view)
    <encoding; ...>               // encoding channels
    <transform; ...>              // data transforms
    <config ...>                  // theme/style configuration
    <layer; ...>                  // composition: overlay multiple charts
    <facet ...>                   // composition: split into sub-plots
    <concat; ...>                 // composition: arrange charts in grid
>
```

#### Title Element

When a structured title is needed:

```lambda
<title
    text: string,                 // title text
    anchor: "start" | "middle" | "end" = "middle",
    font_size: int = 16,
    font_weight: "normal" | "bold" = "bold",
    color: string = "#333",
    offset: int = 10              // distance from chart area
>
```

---

### Data

The `<data>` element specifies the dataset.

```lambda
<data
    // --- Source (one of) ---
    values: array,                // inline array of records
    url?: string,                 // URL or file path to load (future)
    name?: string,                // named dataset reference (future)

    // --- Format hint ---
    format?: "json" | "csv"       // parse format for url source
>
```

**Data records** are arrays of maps (Lambda maps). Each map is one row:

```lambda
let data = [
    {x: 1, y: 10, category: "A"},
    {x: 2, y: 20, category: "B"},
    {x: 3, y: 15, category: "A"},
];
```

---

### Mark

The `<mark>` element specifies the visual representation of data points.

```lambda
<mark
    type: MarkType,               // required: the mark type

    // --- Default visual properties ---
    color?: string,               // default fill color
    opacity?: float = 1.0,        // default opacity [0, 1]
    stroke?: string,              // stroke color
    stroke_width?: float = 1.0,   // stroke width
    fill?: string,                // fill color (overrides color)
    cursor?: string,              // CSS cursor type

    // --- Mark-specific ---
    interpolate?: Interpolation,  // line/area interpolation method
    point?: bool = false,         // show points on line/area marks
    corner_radius?: int = 0,      // bar corner radius
    inner_radius?: int = 0,       // arc inner radius (donut)
    outer_radius?: int,           // arc outer radius
    pad_angle?: float = 0,        // arc padding angle in radians
    size?: int = 30,              // point mark size (area in pixels²)
    shape?: PointShape = "circle" // point mark shape
>
```

#### Mark Types (`MarkType`)

| Type | SVG Primitive | Description |
|------|--------------|-------------|
| `"bar"` | `<rect>` | Rectangular bars for bar/column charts |
| `"line"` | `<path>` | Connected line segments |
| `"area"` | `<path>` | Filled area under a line |
| `"point"` | `<circle>` / `<path>` | Individual data points / scatter |
| `"arc"` | `<path>` | Arcs for pie/donut charts |
| `"rect"` | `<rect>` | Rectangles for heatmaps |
| `"rule"` | `<line>` | Reference lines (horizontal/vertical) |
| `"tick"` | `<line>` | Short tick marks |
| `"text"` | `<text>` | Text labels positioned by data |
| `"boxplot"` | composite | Box-and-whisker plots |
| `"errorbar"` | composite | Error bars |

#### Interpolation Methods (`Interpolation`)

For `line` and `area` marks:

| Method | Description |
|--------|-------------|
| `"linear"` | Straight line segments (default) |
| `"step"` | Step function (horizontal then vertical) |
| `"step-before"` | Step function (vertical then horizontal) |
| `"step-after"` | Step function (horizontal then vertical, trailing) |
| `"monotone"` | Monotone cubic spline |
| `"natural"` | Natural cubic spline |

#### Point Shapes (`PointShape`)

For `point` marks:

`"circle"` | `"square"` | `"cross"` | `"diamond"` | `"triangle-up"` | `"triangle-down"`

---

### Encoding

The `<encoding>` element maps data fields to visual channels. Each child element is a **channel**.

```lambda
<encoding;
    <x ...>                       // horizontal position
    <y ...>                       // vertical position
    <color ...>                   // fill color
    <size ...>                    // mark size
    <shape ...>                   // mark shape (point marks)
    <opacity ...>                 // mark opacity
    <stroke ...>                  // stroke color
    <theta ...>                   // angle (arc marks)
    <radius ...>                  // radius (arc marks)
    <text ...>                    // text label content
    <tooltip ...>                 // tooltip content
    <order ...>                   // drawing order / stack order
    <detail ...>                  // grouping without visual encoding
    <x2 ...>                      // secondary x (ranges)
    <y2 ...>                      // secondary y (ranges)
>
```

#### Channel Definition

Each encoding channel has the following schema:

```lambda
<x                                // (or y, color, size, shape, etc.)
    // --- Data binding (one of) ---
    field: string,                // field name in data record
    value: any,                   // constant value
    datum: any,                   // literal datum (positioned on scale)

    // --- Type (required when field is used) ---
    type: DataType,               // data type classification

    // --- Scale ---
    scale?: <scale ...> | null,   // custom scale; null disables scale

    // --- Axis / Legend ---
    axis?: <axis ...> | null,     // custom axis; null disables axis
    legend?: <legend ...> | null, // custom legend; null disables legend

    // --- Formatting ---
    title?: string,               // axis/legend title (defaults to field name)
    format?: string,              // number/date format string

    // --- Sorting ---
    sort?: "ascending" | "descending" | array | null,

    // --- Aggregation ---
    aggregate?: AggregateOp,      // aggregate operation on this field

    // --- Stacking ---
    stack?: "zero" | "normalize" | "center" | null,

    // --- Binning ---
    bin?: bool | <bin ...>,       // bin continuous data
>
```

#### Data Types (`DataType`)

| Type | Description | Example Fields | Default Scale |
|------|-------------|---------------|---------------|
| `"quantitative"` | Continuous numeric | revenue, temperature | `linear` |
| `"ordinal"` | Ordered discrete | rating, size_category | `point` |
| `"nominal"` | Unordered discrete | country, product_type | `point` (x/y), `ordinal` (color) |
| `"temporal"` | Date/time values | date, timestamp | `time` |

#### Aggregate Operations (`AggregateOp`)

`"count"` | `"sum"` | `"mean"` | `"median"` | `"min"` | `"max"` | `"stdev"` | `"variance"` | `"distinct"` | `"q1"` | `"q3"`

#### Bin Element

```lambda
<bin
    maxbins: int = 10,            // target max number of bins
    step?: float,                 // exact bin step size
    nice?: bool = true            // snap to nice boundaries
>
```

---

### Scale

Scales map data values (domain) to visual values (range). They can be specified inline within an encoding channel or referenced separately.

```lambda
<scale
    type: ScaleType = "linear",   // scale type

    // --- Domain ---
    domain?: array,               // explicit domain [min, max] or [cat1, cat2, ...]
    domain_min?: number,          // override minimum
    domain_max?: number,          // override maximum
    zero?: bool,                  // include zero in domain (default: true for bar)
    nice?: bool = true,           // extend domain to nice values
    clamp?: bool = false,         // clamp output to range

    // --- Range ---
    range?: array,                // explicit range [lo, hi] or [color1, color2, ...]
    scheme?: string,              // named color scheme (for color scales)

    // --- Behavior ---
    reverse?: bool = false,       // reverse the scale
    round?: bool = false,         // round output to integers
    padding?: float = 0,          // band/point scale padding
    padding_inner?: float,        // band scale inner padding
    padding_outer?: float,        // band scale outer padding

    // --- Log/Pow ---
    base?: float = 10,            // log scale base
    exponent?: float = 1          // pow scale exponent
>
```

#### Scale Types (`ScaleType`)

| Type | Domain | Range | Description |
|------|--------|-------|-------------|
| `"linear"` | Continuous | Continuous | Linear interpolation |
| `"log"` | Continuous | Continuous | Logarithmic |
| `"pow"` | Continuous | Continuous | Power / polynomial |
| `"sqrt"` | Continuous | Continuous | Square root (pow with exp=0.5) |
| `"time"` | Temporal | Continuous | Time-aware linear |
| `"ordinal"` | Discrete | Discrete | Categorical mapping |
| `"band"` | Discrete | Continuous | Equal-width bands (bar charts) |
| `"point"` | Discrete | Continuous | Evenly-spaced points |

#### Color Schemes

Named schemes for the `scheme` attribute:

| Category | Schemes |
|----------|---------|
| **Categorical** | `"category10"`, `"category20"`, `"tableau10"`, `"set1"`, `"set2"`, `"set3"`, `"pastel1"`, `"dark2"` |
| **Sequential** | `"blues"`, `"greens"`, `"reds"`, `"oranges"`, `"purples"`, `"greys"`, `"viridis"`, `"plasma"`, `"inferno"`, `"magma"` |
| **Diverging** | `"redblue"`, `"redyellowgreen"`, `"blueorange"`, `"spectral"` |

---

### Axis

The `<axis>` element customizes an axis. When omitted, axes are auto-generated from encoding and scale.

```lambda
<axis
    // --- Visibility ---
    grid?: bool = false,          // show grid lines
    domain?: bool = true,         // show axis domain line
    ticks?: bool = true,          // show ticks
    labels?: bool = true,         // show tick labels
    title?: string | null,        // axis title (null hides)

    // --- Ticks ---
    tick_count?: int,             // target number of ticks
    tick_size?: int = 5,          // tick length in pixels
    tick_min_step?: float,        // minimum step between ticks
    values?: array,               // explicit tick values

    // --- Labels ---
    label_angle?: float = 0,      // label rotation in degrees
    label_align?: "left" | "center" | "right",
    label_font_size?: int = 11,
    label_limit?: int = 200,      // max label width before truncation
    label_format?: string,        // number/date format string
    label_overlap?: "truncate" | "hide" | "rotate",

    // --- Title ---
    title_font_size?: int = 13,
    title_font_weight?: "normal" | "bold" = "normal",
    title_padding?: int = 8,

    // --- Style ---
    domain_color?: string = "#888",
    tick_color?: string = "#888",
    grid_color?: string = "#e0e0e0",
    grid_dash?: array,            // dash pattern e.g. [4, 4]
    grid_opacity?: float = 1.0,

    // --- Position ---
    orient?: "top" | "bottom" | "left" | "right",  // axis position
    offset?: int = 0              // pixel offset from default position
>
```

---

### Legend

The `<legend>` element customizes a legend. Auto-generated when color, size, or shape encoding is used.

```lambda
<legend
    // --- Visibility ---
    title?: string | null,        // legend title

    // --- Position ---
    orient?: "right" | "left" | "top" | "bottom" | "top-left" | "top-right"
             | "bottom-left" | "bottom-right" = "right",
    offset?: int = 10,            // pixel offset from chart area

    // --- Symbols ---
    symbol_type?: PointShape = "circle",
    symbol_size?: int = 100,      // symbol area in px²
    symbol_stroke_width?: float = 1,

    // --- Labels ---
    label_font_size?: int = 11,
    label_limit?: int = 160,
    label_format?: string,

    // --- Title ---
    title_font_size?: int = 13,
    title_font_weight?: "normal" | "bold" = "bold",

    // --- Layout ---
    direction?: "vertical" | "horizontal" = "vertical",
    columns?: int,                // wrap legend items into columns
    row_padding?: int = 4,
    column_padding?: int = 8
>
```

---

### Transform

The `<transform>` element contains an ordered sequence of data transformations applied before encoding.

```lambda
<transform;
    // Each child is a transform operation, applied in order:
    <filter ...>
    <calculate ...>
    <aggregate ...>
    <bin ...>
    <sort ...>
    <fold ...>
    <flatten ...>
    <lookup ...>
    <window ...>
>
```

#### Filter Transform

```lambda
<filter
    expr: string                  // Lambda expression using ~ for current row
                                  // e.g., "~.revenue > 1000"
>
```

#### Calculate Transform

```lambda
<calculate
    expr: string,                 // Lambda expression
    as: string                    // name for the new field
                                  // e.g., expr: "~.revenue * ~.quantity", as: "total"
>
```

#### Aggregate Transform

```lambda
<aggregate;
    // Aggregate operations
    <agg op: AggregateOp, field?: string, as: string>
    // ...

    // Group-by fields
    <group field: string>
    <group field: string>
>
```

Example:

```lambda
<aggregate;
    <agg op: "mean", field: "temperature", as: "avg_temp">
    <agg op: "count", as: "n">
    <group field: "city">
    <group field: "month">
>
```

#### Bin Transform

```lambda
<bin
    field: string,                // field to bin
    as: string,                   // output field name (creates as and as_end)
    maxbins: int = 10,
    step?: float,
    nice?: bool = true
>
```

#### Sort Transform

```lambda
<sort
    field: string | array,        // field(s) to sort by
    order: "ascending" | "descending" = "ascending"
>
```

#### Fold Transform

Unpivot wide-format data into long format:

```lambda
<fold
    fields: array,                // fields to fold: ["col_a", "col_b", "col_c"]
    as: (string, string)          // output names: ("key", "value")
                                  // default: ("key", "value")
>
```

#### Flatten Transform

Flatten array-valued fields:

```lambda
<flatten
    fields: array,                // fields containing arrays: ["tags"]
    as?: array                    // output field names
>
```

#### Window Transform

Compute running/cumulative aggregates:

```lambda
<window;
    <win op: "rank" | "row_number" | "sum" | "mean" | "count",
         field?: string,
         as: string>
    <sort field: string, order?: "ascending" | "descending">
    <group field?: string>        // partition by
    <frame from?: int, to?: int>  // window frame: e.g., from: -2, to: 2
>
```

---

### Composition

Lambda Chart supports four composition modes to build multi-view visualizations.

#### Layer

Overlay multiple marks in the same plot area (shared scales):

```lambda
<chart width: 600, height: 400;
    <data values: dataset>
    <layer;
        <chart;
            <mark type: "area", opacity: 0.3>
            <encoding; ... >
        >
        <chart;
            <mark type: "line">
            <encoding; ... >
        >
        <chart;
            <mark type: "point">
            <encoding; ... >
        >
    >
>
```

#### Facet

Split data into sub-plots by a field:

```lambda
<chart;
    <data values: dataset>
    <mark type: "point">
    <encoding; ... >
    <facet
        field: string,            // field to facet by
        type: DataType,           // data type of the field
        columns?: int = 3,        // number of columns in grid
        spacing?: int = 20,       // spacing between sub-plots in pixels
        header?: <header title_font_size?: int, label_font_size?: int>
    >
>
```

#### Concat (Horizontal / Vertical)

Arrange independent charts in a grid:

```lambda
<hconcat spacing: 20;            // horizontal concatenation
    <chart; ... >
    <chart; ... >
    <chart; ... >
>

<vconcat spacing: 20;            // vertical concatenation
    <chart; ... >
    <chart; ... >
>
```

#### Repeat

Repeat a chart template across a set of fields:

```lambda
<repeat
    row?: array,                  // fields to repeat across rows
    column?: array                // fields to repeat across columns
;
    <chart;
        <mark type: "point">
        <encoding;
            <x field: {repeat: "column"}, type: "quantitative">
            <y field: {repeat: "row"}, type: "quantitative">
        >
    >
>
```

---

### Config (Theming)

The `<config>` element provides global styling defaults.

```lambda
<config
    // --- Typography ---
    font?: string = "sans-serif",
    font_size?: int = 11,
    title_font_size?: int = 16,

    // --- Colors ---
    background?: string = "white",
    color?: string = "#4c78a8",   // default mark color

    // --- Marks ---
    mark_opacity?: float = 1.0,
    bar_corner_radius?: int = 0,
    point_size?: int = 30,
    line_stroke_width?: float = 2,

    // --- Axes ---
    axis_grid?: bool = false,
    axis_domain_color?: string = "#888",
    axis_tick_color?: string = "#888",
    axis_label_font_size?: int = 11,

    // --- Legend ---
    legend_orient?: string = "right",
    legend_title_font_size?: int = 13,

    // --- Padding ---
    padding?: int | {top: int, right: int, bottom: int, left: int} = 20
>
```

**Predefined themes** (future):

```lambda
// Apply a built-in theme
<chart;
    <config theme: "dark">
    ...
>
```

| Theme | Description |
|-------|-------------|
| `"default"` | Light background, blue palette |
| `"dark"` | Dark background, bright palette |
| `"minimal"` | No grid, thin axes, muted colors |
| `"presentation"` | Larger fonts, bolder colors |

---

## SVG Generation

The chart library generates SVG as a Lambda element tree. The structure follows standard SVG conventions:

```lambda
<svg xmlns: "http://www.w3.org/2000/svg", width: 600, height: 400;
    // Background
    <rect width: 600, height: 400, fill: "white">

    // Chart group (translated by padding)
    <g transform: "translate(50, 20)";

        // Axes layer
        <g class: "axis x-axis";
            <line x1: 0, y1: 360, x2: 500, y2: 360, stroke: "#888">
            <g class: "tick"; ... >
        >
        <g class: "axis y-axis";
            <line x1: 0, y1: 0, x2: 0, y2: 360, stroke: "#888">
            <g class: "tick"; ... >
        >

        // Grid layer (if enabled)
        <g class: "grid"; ... >

        // Marks layer
        <g class: "marks";
            <rect x: 10, y: 100, width: 40, height: 260, fill: "#4c78a8">
            <rect x: 60, y: 50,  width: 40, height: 310, fill: "#4c78a8">
            // ...
        >

        // Title
        <text x: 250, y: -5, text-anchor: "middle", font-size: 16;
            "Sales by Category"
        >
    >

    // Legend (outside chart group)
    <g class: "legend", transform: "translate(520, 20)"; ... >
>
```

Key SVG generation mappings:

| Mark Type | SVG Element(s) | Key Attributes |
|-----------|----------------|----------------|
| `bar` | `<rect>` | `x`, `y`, `width`, `height`, `fill` |
| `line` | `<path>` | `d` (path data with M, L, C commands), `stroke`, `fill: none` |
| `area` | `<path>` | `d` (closed path), `fill`, `opacity` |
| `point` | `<circle>` | `cx`, `cy`, `r`, `fill` |
| `arc` | `<path>` | `d` (arc path via A command), `fill` |
| `rect` | `<rect>` | `x`, `y`, `width`, `height`, `fill` |
| `rule` | `<line>` | `x1`, `y1`, `x2`, `y2`, `stroke` |
| `tick` | `<line>` | `x1`, `y1`, `x2`, `y2`, `stroke` |
| `text` | `<text>` | `x`, `y`, `text-anchor`, `font-size` |

---

## Implementation Roadmap

### Phase 1 — Core (MVP)

Single-view charts with basic marks and auto-scales.

| Task | Description |
|------|-------------|
| Scale engine | `linear`, `band`, `ordinal` scales with domain/range inference |
| Bar mark | Vertical and horizontal bars |
| Line mark | Single and multi-series lines |
| Point mark | Scatter plots |
| Auto-axes | Axis generation with ticks, labels, title |
| SVG assembly | Padding, positioning, SVG root element |
| `chart.render()` | Main entry point, end-to-end pipeline |

**Deliverable:** Bar charts, line charts, scatter plots with auto-generated axes.

### Phase 2 — Extended Marks & Encoding

| Task | Description |
|------|-------------|
| Area mark | Filled area charts |
| Arc mark | Pie and donut charts |
| Text mark | Data labels |
| Rule mark | Reference lines |
| Color encoding | Categorical and sequential color mapping |
| Size encoding | Variable mark sizes |
| Opacity encoding | Transparency mapping |
| Legends | Auto-generated color/size/shape legends |

**Deliverable:** Full mark coverage, color-encoded multi-series charts, pie charts.

### Phase 3 — Transforms & Scales

| Task | Description |
|------|-------------|
| Filter transform | Filter data rows |
| Aggregate transform | Group-by aggregation |
| Calculate transform | Computed fields |
| Bin transform | Histogram binning |
| Log / sqrt / pow scales | Non-linear scales |
| Time scale | Date/time axis with smart ticks |
| Stacking | Stacked bar and area charts |

**Deliverable:** Histograms, aggregated charts, stacked charts, log-scale plots.

### Phase 4 — Composition & Theming

| Task | Description |
|------|-------------|
| Layer composition | Overlay multiple marks |
| Facet composition | Small multiples |
| Concat (h/v) | Dashboard-style layouts |
| Config / theming | Global style configuration |
| Color schemes | Named palettes (category10, viridis, etc.) |
| Grid lines | Optional background grid |

**Deliverable:** Multi-view dashboards, themed charts, publication-ready output.

### Phase 5 — Advanced (Future)

| Task | Description |
|------|-------------|
| Repeat composition | Scatter plot matrix |
| Window transforms | Running totals, ranks |
| Box plot mark | Statistical box plots |
| Error bar mark | Error ranges |
| Fold / flatten transforms | Data reshaping |
| Responsive sizing | Auto-sizing to container |
| Gradient fills | Linear and radial gradients |
| Annotations | Text and shape annotations |

---

*This proposal is based on Vega-Lite v5 as the design reference, adapted for Lambda Script's element-based syntax, functional programming model, and SVG output pipeline.*

---

## Implementation Progress (as of 2025-02-17)

### Status Summary

The chart library is **functional end-to-end** for Phase 1 (Core MVP) chart types. All 12 modules are implemented, compile via MIR JIT, and produce correct SVG output. A Vega-Lite JSON converter module (`vega.ls`) has been added to enable ingestion of Vega-Lite specifications. Three automated tests (bar, line, scatter) are registered in the baseline test suite and passing.

| Phase | Status | Notes |
|-------|--------|-------|
| **Phase 1 — Core (MVP)** | ✅ Complete | Bar, line, point marks; auto-axes; linear/band/point scales; SVG assembly |
| **Phase 2 — Extended Marks** | 🔶 Partial | Area, arc, text, rule, tick marks implemented. Legends implemented (color + gradient). Size/opacity encoding not yet wired. |
| **Phase 3 — Transforms & Scales** | 🔶 Partial | Filter and sort transforms working. Log/sqrt scales implemented. Aggregate, calculate, bin, fold, flatten transforms **stubbed** (return data unchanged). Stacking not implemented. |
| **Phase 4 — Composition** | 🔶 Partial | Layer composition working. Facet, concat, repeat not implemented. Config/theming not implemented. Grid lines working. |
| **Phase 5 — Advanced** | ❌ Not started | — |

### Module Inventory

12 modules, ~2,180 lines of Lambda Script, 57 public functions, 18 public values.

| Module | Lines | Public API | Status |
|--------|-------|------------|--------|
| `chart.ls` | 379 | `render(chart_el)`, `render_spec(spec)` | ✅ Working — main entry points |
| `parse.ls` | 159 | `parse_chart`, `get_channel`, `has_channel` | ✅ Working — element tree → spec map |
| `mark.ls` | 328 | `bar`, `bar_horizontal`, `line_mark`, `area_mark`, `point_mark`, `arc_mark`, `text_mark`, `rule_mark`, `tick_mark` | ✅ 9 mark types implemented |
| `scale.ls` | 190 | `linear_scale`, `linear_scale_nice`, `log_scale`, `sqrt_scale`, `band_scale`, `point_scale`, `ordinal_scale`, `scale_apply`, `scale_ticks`, `scale_invert`, `infer_scale`, `infer_color_scale` | ✅ 7 scale types |
| `axis.ls` | 189 | `x_axis`, `y_axis`, `x_axis_grid`, `y_axis_grid`, `estimate_y_axis_width`, `estimate_x_axis_height` | ✅ Working |
| `legend.ls` | 141 | `color_legend`, `gradient_legend`, `legend_width`, `legend_height` | ✅ Working |
| `layout.ls` | 114 | `compute_layout`, `compute_arc_layout` | ✅ Working |
| `color.ls` | 103 | `get_scheme`, `pick_color`, `sequential_color` + 13 palette constants | ✅ 13 palettes |
| `svg.ls` | 159 | 19 SVG helper functions (elements, path commands, transforms) | ✅ Working |
| `transform.ls` | 82 | `apply_transforms`, `compute_agg` | 🔶 Only filter + sort; aggregate/calculate/bin/fold/flatten stubbed |
| `util.ls` | 163 | 15 math/collection utilities | ✅ Working (with `unique_vals` workaround) |
| `vega.ls` | 174 | `convert(vl)` | ✅ Vega-Lite JSON → Lambda chart spec |

### Vega-Lite Converter (`vega.ls`)

A new module not in the original proposal. Converts Vega-Lite JSON maps into the Lambda chart spec format consumed by `chart.render_spec()`. Key mappings:

| Vega-Lite | Lambda Chart |
|-----------|-------------|
| `mark.type` | `mark.kind` |
| `encoding.*.type` | `encoding.*.dtype` |
| `"quantitative"` / `"Q"` | `"quantitative"` |
| `"nominal"` / `"N"` | `"nominal"` |
| `"ordinal"` / `"O"` | `"ordinal"` |
| `"temporal"` / `"T"` | `"temporal"` |
| camelCase keys (e.g., `strokeWidth`) | snake_case keys (e.g., `stroke_width`) |
| `mark: "bar"` (string shorthand) | `mark: {kind: "bar"}` |
| `layer: [...]` | Layer composition via `render_layered` |

Usage: `let spec = vega.convert(vega_lite_json)` then `chart.render_spec(spec)`.

### Test Suite

3 end-to-end tests registered in the baseline test runner (`test_lambda_gtest.exe`):

| Test | Input | Chart Type | Data Points | Verified Output |
|------|-------|------------|-------------|-----------------|
| `test_bar_chart` | Inline Vega-Lite JSON | Bar (nominal × quantitative) | 3 categories (A/B/C) | ✅ 3,050 bytes SVG |
| `test_line_chart` | Inline Vega-Lite JSON | Line with point markers | 5 points (x: 0–4) | ✅ 4,861 bytes SVG |
| `test_scatter_chart` | Inline Vega-Lite JSON | Scatter (point mark) | 5 points (x: 10–50) | ✅ 4,826 bytes SVG |

All tests follow the pipeline: **Vega-Lite map → `vega.convert()` → `chart.render_spec()` → `format(svg, 'xml')`**.

Tests are registered in `test/test_lambda_gtest.cpp` via the `"test/lambda/chart"` directory entry in `FUNCTIONAL_TEST_DIRECTORIES`. Each `.ls` script has a corresponding `.txt` expected-output file.

### Issues Encountered

#### Resolved

| # | Issue | Root Cause | Resolution |
|---|-------|-----------|------------|
| 1 | **Cross-module `: float` type annotations break JIT** | MIR compilation fails when `pub fn` parameters or return types use `: float` | Removed all `: float` annotations from pub function signatures; use un-annotated params |
| 2 | **Tuple-pipe mapping broken cross-module** | `tuple \| module.fn(~)` fails at MIR compilation | Replaced with explicit `for (x in tuple) module.fn(x)` comprehensions |
| 3 | **`unique()` broken for string arrays** | Built-in `unique()` only returns the first element when given string arrays; works correctly for integer arrays | Implemented recursive `unique_vals()` in `util.ls` using linear-scan `has_val()` helper; replaced all 12 call sites across 5 modules |
| 4 | **X-axis tick labels show `"<error>"`** | `util.fmt_num(tv)` called on string values from band scales (nominal categories) | Changed to `string(tv)` in `axis.ls` — works universally for both numeric and string tick values |
| 5 | **`if`/`else` blocks parsed as map literals** | `{ }` inside if-expressions treated as map constructor, not code blocks | Rewrote all conditional logic as parenthesized if-expressions: `(if (cond) expr1 else expr2)` |
| 6 | **Reserved words as map keys** | `.type` field access fails because `type` is a reserved Lambda keyword | Used bracket notation: `obj["type"]` instead of `obj.type` |
| 7 | **`for` comprehension concatenates string results** | When the body of a `for` expression returns strings, results are concatenated instead of collected into a tuple | Used recursive array-building pattern with `[*result, item]` instead of `for` comprehension for string-producing operations |
| 8 | **`let` with `if_expr` inside `for` comprehension causes MIR failure** | `let label = (if (cond) A else B)` as a `for`-local binding crashes MIR compilation | Avoided the pattern; used `string(tv)` universally instead of conditional formatting |
| 9 | **`input()` with `^err` destructuring breaks module resolution** | `let vl^err = input(...)` in test scripts causes "Failed to find import item for module" error at MIR compilation, even though all imported modules compile individually | Switched test scripts to inline data (map literals) instead of file I/O; `input()` with `^err` appears to interfere with the JIT module linker |
| 10 | **Computed map keys not supported** | `{[key_var]: val}` syntax does not exist in Lambda | Cannot dynamically construct maps with variable keys; must use spread `{*base, field: val}` with known field names |
| 11 | **Map spread segfault** | `{...m}` causes a segfault | Use `{*m}` instead |
| 12 | **Functions limited to 8 parameters** | MIR JIT fails for functions with >8 parameters | Restructured functions to use context maps (`ctx`) instead of many individual parameters |

#### Remaining / Known Limitations

| # | Issue | Impact | Workaround |
|---|-------|--------|------------|
| 1 | **5 transform types not implemented** (aggregate, calculate, bin, fold, flatten) | Cannot do in-chart data aggregation, computed fields, or histograms | Pre-process data in user scripts before passing to chart; transforms return data unchanged |
| 2 | **No computed key support in Lambda** | Cannot dynamically construct maps with variable field names | Limits transform implementation; `calculate` and `aggregate` transforms require building maps with dynamic `as` field names |
| 3 | **`unique()` still broken for strings** | Upstream Lambda runtime bug | Workaround in place (`util.unique_vals`); should be fixed in the runtime |
| 4 | **`input()` + `^err` breaks JIT module linking** | Cannot load test data from external JSON files in test scripts | Use inline data in test scripts; `input()` works in standalone scripts but fails when combined with multi-module imports |
| 5 | **No stacking support** | Cannot render stacked bar or stacked area charts | Not yet implemented in the mark/scale pipeline |
| 6 | **No facet / concat / repeat composition** | Only single-view and layer compositions work | Multi-view dashboards not yet possible |
| 7 | **No temporal (time) scale** | Date/time axes not supported | Temporal data must be converted to numeric values manually |
| 8 | **Gradient legend uses discrete rectangles** | No SVG `<linearGradient>` element generation | Visual approximation with 20 small `<rect>` elements; acceptable for most use cases |
| 9 | **Band/point scale index lookup is O(n)** | Uses linear scan `for` comprehension instead of hash map | Acceptable for typical category counts (<100); would need optimization for large categorical datasets |
| 10 | **`: float` annotations unusable on public functions** | Cross-module JIT compilation fails | All numeric parameters left un-annotated; type inference handles it correctly at runtime |
