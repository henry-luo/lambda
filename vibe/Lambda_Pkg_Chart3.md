# Lambda Chart Library — Advanced Charts & Features Proposal

**Date:** 2025-03-18
**Baseline:** Current — Phase 1 complete, Phase 2–3 partial, Phase 4–5 not started
**Reference:** Vega v5 / Vega-Lite v5 specification

---

## Table of Contents

1. [Motivation](#motivation)
2. [Reference: Vega Capabilities Beyond Vega-Lite](#reference-vega-capabilities-beyond-vega-lite)
3. [Proposed Advanced Chart Types](#proposed-advanced-chart-types)
   - [Grouped Bar Chart](#grouped-bar-chart)
   - [Stacked Bar / Stacked Area](#stacked-bar--stacked-area)
   - [Histogram](#histogram)
   - [Heatmap](#heatmap)
   - [Box Plot](#box-plot)
   - [Error Bar / Error Band](#error-bar--error-band)
   - [Waterfall Chart](#waterfall-chart)
   - [Radial / Radar Chart](#radial--radar-chart)
   - [Sunburst Chart](#sunburst-chart)
   - [Treemap](#treemap)
   - [Bubble Chart](#bubble-chart)
   - [Slope Chart](#slope-chart)
   - [Candlestick / OHLC Chart](#candlestick--ohlc-chart)
   - [Density / Violin Plot](#density--violin-plot)
   - [Parallel Coordinates](#parallel-coordinates)
4. [Proposed Advanced Features](#proposed-advanced-features)
   - [Stacking Engine](#stacking-engine)
   - [Facet Composition](#facet-composition)
   - [Concat Composition (hconcat / vconcat)](#concat-composition)
   - [Repeat Composition](#repeat-composition)
   - [Temporal Scale & Time Formatting](#temporal-scale--time-formatting)
   - [Selection & Conditional Encoding](#selection--conditional-encoding)
   - [Tooltip Generation](#tooltip-generation)
   - [Annotation Layer](#annotation-layer)
   - [Responsive Sizing](#responsive-sizing)
   - [SVG Gradients & Patterns](#svg-gradients--patterns)
   - [Config / Theming System](#config--theming-system)
   - [Window Transform](#window-transform)
   - [Lookup Transform](#lookup-transform)
   - [Regression & Loess Transforms](#regression--loess-transforms)
   - [Projection (Geo) Support](#projection-geo-support)
5. [Schema Additions](#schema-additions)
   - [Stack Schema](#stack-schema)
   - [Facet Schema](#facet-schema)
   - [Window Transform Schema](#window-transform-schema)
   - [Selection Schema](#selection-schema)
   - [Projection Schema](#projection-schema)
   - [Tooltip Schema](#tooltip-schema)
   - [Annotation Schema](#annotation-schema)
6. [SVG Generation Additions](#svg-generation-additions)
7. [Module Changes](#module-changes)
8. [Implementation Roadmap](#implementation-roadmap)
9. [Test Plan](#test-plan)

---

## Motivation

The Lambda Chart Library currently supports 9 mark types, 7 scale types, 7 transforms, and single-view + layer composition. This covers the most common chart types (bar, line, area, scatter, pie/donut) but falls short of the full analytical visualization spectrum.

**Vega** (the low-level grammar underlying Vega-Lite) offers capabilities that go well beyond what Vega-Lite exposes: custom mark layout, signal-driven reactivity, explicit scale/axis/legend control, geographic projections, force-directed layouts, and composable group marks. While Lambda Chart does not need to replicate Vega's full imperative signal system, several Vega concepts map cleanly to Lambda's functional model and would significantly expand the library's utility.

This proposal covers:
- **15 advanced chart types** not currently possible
- **15 advanced features** to complete the charting pipeline
- A phased roadmap building on the existing module structure

---

## Reference: Vega Capabilities Beyond Vega-Lite

Vega v5 provides several categories of functionality that Vega-Lite abstracts away or omits. The table below maps Vega concepts to their proposed Lambda Chart equivalents:

| Vega Concept | Vega Mechanism | Lambda Chart Adaptation |
|-------------|----------------|------------------------|
| **Group marks** | Nested `group` marks with their own scales/axes | Facet and concat composition elements |
| **Custom mark layout** | `from.facet` + explicit x/y/width/height | `compute_layout()` extension for multi-view |
| **Signals** | Reactive variables driven by events | Not adopted — Lambda is non-interactive; use `<config>` for parameterization |
| **Projections** | Geographic projections (mercator, etc.) | `<projection>` element for geo charts (future) |
| **Force layout** | Physics-based simulation | Not adopted — requires iterative mutation |
| **Voronoi, contour** | Spatial transforms | Not adopted in initial phases |
| **Tree/hierarchy marks** | Treemap, sunburst, tree layout | Tree layout transforms + arc/rect marks |
| **Cross/flatten/lookup** | Data merge transforms | `<lookup>` transform (join data sources) |
| **Window aggregation** | Running totals, ranks, percentiles | `<window>` transform |
| **Regression/loess** | Statistical model fitting | `<regression>` and `<loess>` transforms |
| **Gradient fills** | SVG `<linearGradient>`, `<radialGradient>` | SVG gradient definitions in `svg.ls` |
| **Custom axis/legend** | Full control over axis/legend marks | Extended `<axis>` and `<legend>` attributes |
| **Mark clipping** | Clip marks to chart area | `clip-path` on mark groups |
| **Trail mark** | Variable-width path | `<mark type: "trail">` |
| **Image mark** | Positioned images | `<mark type: "image">` (future) |
| **Geo marks** | GeoJSON shape rendering | `<mark type: "geoshape">` (future) |

---

## Proposed Advanced Chart Types

### Grouped Bar Chart

Side-by-side bars for comparing categories across groups.

**Vega approach:** Uses `group` marks with nested `rect` marks and `band` scale with inner padding. Vega-Lite uses `xOffset` encoding channel.

**Lambda spec:**

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: quarterly_sales>
    <mark type: "bar">
    <encoding;
        <x field: "quarter", type: "nominal">
        <y field: "revenue", type: "quantitative">
        <color field: "product", type: "nominal">
        <x_offset field: "product", type: "nominal">
    >
>;
```

**Implementation:**
- Add `x_offset` encoding channel to `parse.ls`
- In `mark.ls`, when `x_offset` is present, subdivide the band width by the number of offset categories
- Each sub-bar is positioned at `band_x + offset_index * sub_width`
- Uses existing `band_scale` with an additional inner `band_scale` for offset

**SVG output:** Multiple `<rect>` elements per x-category, positioned side-by-side.

---

### Stacked Bar / Stacked Area

Bars or areas stacked on top of each other to show part-to-whole relationships.

**Vega approach:** Vega uses a `stack` transform that computes `y0` and `y1` fields. Marks then use `y` and `y2` encoding to span from y0 to y1.

**Lambda spec:**

```lambda
// Stacked bar
let chart1 = <chart width: 600, height: 400;
    <data values: sales_by_region>
    <mark type: "bar">
    <encoding;
        <x field: "quarter", type: "nominal">
        <y field: "revenue", type: "quantitative", stack: "zero">
        <color field: "region", type: "nominal">
    >
>;

// Normalized (100%) stacked bar
let chart2 = <chart width: 600, height: 400;
    <data values: sales_by_region>
    <mark type: "bar">
    <encoding;
        <x field: "quarter", type: "nominal">
        <y field: "revenue", type: "quantitative", stack: "normalize">
        <color field: "region", type: "nominal">
    >
>;

// Stacked area
let chart3 = <chart width: 600, height: 400;
    <data values: time_series>
    <mark type: "area">
    <encoding;
        <x field: "date", type: "ordinal">
        <y field: "value", type: "quantitative", stack: "zero">
        <color field: "series", type: "nominal">
    >
>;
```

**Implementation:**
- New `stack.ls` module (see [Stacking Engine](#stacking-engine))
- Modify `bar()` and `area_mark()` in `mark.ls` to accept `y0`/`y1` baselines
- Add `stack` attribute to encoding channel schema

---

### Histogram

Bin continuous data and display frequency distribution.

**Vega approach:** Vega-Lite uses `bin: true` on the x-encoding plus `aggregate: "count"` on y.

**Lambda spec:**

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: measurements>
    <mark type: "bar">
    <encoding;
        <x field: "value", type: "quantitative", bin: true>
        <y aggregate: "count", type: "quantitative">
    >
>;
```

**Implementation:**
- Leverage existing `bin` transform in `transform.ls`
- Auto-insert bin + aggregate transforms when `bin: true` appears in encoding
- Generate bars spanning `[bin_start, bin_end)` using `x` and `x2` encoding

---

### Heatmap

Two-dimensional grid of colored cells for visualizing matrix data.

**Vega approach:** Uses `rect` mark with `x` (ordinal), `y` (ordinal), and `color` (quantitative) encodings.

**Lambda spec:**

```lambda
let my_chart = <chart width: 500, height: 400;
    <data values: correlation_matrix>
    <mark type: "rect">
    <encoding;
        <x field: "var_x", type: "ordinal">
        <y field: "var_y", type: "ordinal">
        <color field: "correlation", type: "quantitative",
               scale: <scale scheme: "redblue", domain: [-1, 1]>>
    >
>;
```

**Implementation:**
- Wire `rect` mark through the standard mark pipeline (currently listed but not dispatched from `chart.ls`)
- Use `band_scale` for x and y positioning
- Map `color` to a sequential or diverging color scale
- Add diverging color scale interpolation to `color.ls`

---

### Box Plot

Statistical summary showing median, quartiles, and outliers.

**Vega approach:** Vega-Lite provides `boxplot` as a composite mark that expands to rect (box) + rule (whiskers) + tick (median) + point (outliers).

**Lambda spec:**

```lambda
let my_chart = <chart width: 500, height: 400;
    <data values: exam_scores>
    <mark type: "boxplot", extent: 1.5>
    <encoding;
        <x field: "subject", type: "nominal">
        <y field: "score", type: "quantitative">
    >
>;
```

**Implementation:**
- New composite mark renderer `boxplot_mark()` in `mark.ls`
- Internally computes min, q1, median, q3, max per group using `compute_agg()`
- Generates: `<rect>` for IQR box, `<line>` for whiskers, `<line>` for median, `<circle>` for outliers
- `extent` controls whisker length as multiple of IQR (default 1.5; Vega convention)
- Outlier detection: points beyond `q1 - extent * IQR` or `q3 + extent * IQR`

---

### Error Bar / Error Band

Display uncertainty or variability around data points.

**Vega approach:** Vega-Lite `errorbar` / `errorband` composite marks.

**Lambda spec:**

```lambda
// Error bars on a point chart
let my_chart = <chart width: 600, height: 400;
    <data values: experiment_results>
    <layer;
        <chart;
            <mark type: "point">
            <encoding;
                <x field: "condition", type: "nominal">
                <y field: "mean_value", type: "quantitative">
            >
        >
        <chart;
            <mark type: "errorbar">
            <encoding;
                <x field: "condition", type: "nominal">
                <y field: "ci_low", type: "quantitative">
                <y2 field: "ci_high">
            >
        >
    >
>;
```

**Implementation:**
- `errorbar_mark()`: generates `<line>` from y to y2-value, plus horizontal cap ticks
- `errorband_mark()`: generates `<path>` (filled area) between y and y2 curves
- Both use existing x and y scales; `y2` encoding channel already in the schema

---

### Waterfall Chart

Shows cumulative effect of sequential positive and negative values.

**Vega approach:** Vega implements waterfall as a bar chart with computed running totals using window transforms.

**Lambda spec:**

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: financial_changes>
    <transform;
        <window;
            <win op: "sum", field: "amount", as: "running_total">
        >
        <calculate as: "y0", op: "-", field: "running_total", field2: "amount">
    >
    <mark type: "bar", color_positive: "#4c78a8", color_negative: "#e45756">
    <encoding;
        <x field: "label", type: "nominal">
        <y field: "y0", type: "quantitative">
        <y2 field: "running_total">
        <color field: "amount", type: "quantitative",
               scale: <scale domain: [0], range: ["#e45756", "#4c78a8"]>>
    >
>;
```

**Implementation:**
- Requires `<window>` transform (see [Window Transform](#window-transform))
- Uses existing `bar` mark with `y` + `y2` encoding to set both endpoints
- Conditional coloring via diverging color scale keyed on sign

---

### Radial / Radar Chart

Multivariate data displayed on axes radiating from a center point.

**Vega approach:** Vega constructs radar charts using `line` marks with polar coordinates via manual trigonometric calculation in `transform`.

**Lambda spec:**

```lambda
let my_chart = <chart width: 400, height: 400;
    <data values: skill_ratings>
    <mark type: "line", interpolate: "linear-closed">
    <encoding;
        <theta field: "skill", type: "nominal">
        <radius field: "rating", type: "quantitative">
        <color field: "person", type: "nominal">
    >
    <config axis_grid: true>
>;
```

**Implementation:**
- New `radar_layout()` in `layout.ls` computes polar-to-cartesian mapping
- Add `radius` encoding channel for radial distance from center
- `theta` channel distributes categories evenly around 360°
- Axis rendering as radial spokes + concentric grid circles
- `"linear-closed"` interpolation closes the polygon path
- Each series becomes a filled polygon with `opacity: 0.3`

---

### Sunburst Chart

Hierarchical data displayed as concentric rings of arcs.

**Vega approach:** Vega uses `arc` mark with hierarchy transforms (`stratify`, `partition`) to compute angular and radial extents.

**Lambda spec:**

```lambda
let my_chart = <chart width: 500, height: 500;
    <data values: org_hierarchy>
    <transform;
        <hierarchy type: "partition",
                   id_field: "id",
                   parent_field: "parent",
                   value_field: "size">
    >
    <mark type: "arc">
    <encoding;
        <theta field: "start_angle", type: "quantitative">
        <theta2 field: "end_angle">
        <radius field: "inner_radius", type: "quantitative">
        <radius2 field: "outer_radius">
        <color field: "depth", type: "ordinal">
    >
>;
```

**Implementation:**
- New `hierarchy.ls` module for tree layout transforms (partition, treemap algorithms)
- `partition` transform computes angular extent per node based on value proportion
- Radial extent determined by tree depth
- Reuses existing `arc_mark()` with `theta`/`theta2`/`radius`/`radius2` channels
- Add `theta2` and `radius2` encoding channels

---

### Treemap

Hierarchical data displayed as nested rectangles proportional to value.

**Vega approach:** Vega uses `stratify` + `treemap` transforms to compute `x0`, `y0`, `x1`, `y1` for each leaf node, then renders with `rect` marks.

**Lambda spec:**

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: disk_usage>
    <transform;
        <hierarchy type: "treemap",
                   id_field: "path",
                   parent_field: "parent",
                   value_field: "size",
                   method: "squarify">
    >
    <mark type: "rect">
    <encoding;
        <x field: "x0", type: "quantitative", scale: null>
        <y field: "y0", type: "quantitative", scale: null>
        <x2 field: "x1">
        <y2 field: "y1">
        <color field: "category", type: "nominal">
        <text field: "name">
    >
>;
```

**Implementation:**
- Add `treemap` layout algorithm in `hierarchy.ls` (squarify algorithm)
- Computes `x0, y0, x1, y1` for each rectangle fitting proportional to value
- Rect mark with `x/y/x2/y2` encoding, `scale: null` to use raw pixel coordinates
- Optional text labels centered in each rectangle

---

### Bubble Chart

Scatter plot where point size encodes a third variable.

**Lambda spec:**

```lambda
let my_chart = <chart width: 600, height: 400;
    <data values: country_stats>
    <mark type: "point">
    <encoding;
        <x field: "gdp", type: "quantitative", scale: <scale type: "log">>
        <y field: "life_expectancy", type: "quantitative">
        <size field: "population", type: "quantitative",
              scale: <scale range: [20, 2000]>>
        <color field: "continent", type: "nominal">
        <tooltip field: "country">
    >
>;
```

**Implementation:**
- Already partially supported — `point_mark` reads `size` from encoding
- Need to wire `size_scale` properly in `chart.ls` (linear scale from data domain to pixel-area range)
- Point radius = `sqrt(size_area / pi)` for perceptually accurate sizing
- Tooltip support (see [Tooltip Generation](#tooltip-generation))

---

### Slope Chart

Compare values between two time points with connecting lines.

**Vega approach:** Constructed via `line` mark with two-point data per entity.

**Lambda spec:**

```lambda
let my_chart = <chart width: 400, height: 500;
    <data values: rankings>
    <mark type: "line", point: true>
    <encoding;
        <x field: "year", type: "ordinal">
        <y field: "rank", type: "quantitative",
           scale: <scale reverse: true>>
        <color field: "team", type: "nominal">
        <detail field: "team">
    >
>;
```

**Implementation:**
- Wire `detail` encoding channel (group data by field without visual mapping)
- `reverse: true` on scale controls inverted axes (rank 1 at top)
- Add `detail` channel parsing in `parse.ls`
- Line mark already groups by color channel; `detail` provides additional grouping

---

### Candlestick / OHLC Chart

Financial chart showing open, high, low, close prices.

**Vega approach:** Composed from `rule` marks (high-low whisker) and `bar` marks (open-close body) via layer composition.

**Lambda spec:**

```lambda
let my_chart = <chart width: 700, height: 400, title: "Stock Price";
    <data values: stock_data>
    <layer;
        // High-low whisker
        <chart;
            <mark type: "rule">
            <encoding;
                <x field: "date", type: "ordinal">
                <y field: "low", type: "quantitative">
                <y2 field: "high">
            >
        >
        // Open-close body
        <chart;
            <mark type: "bar", width: 8>
            <encoding;
                <x field: "date", type: "ordinal">
                <y field: "open", type: "quantitative">
                <y2 field: "close">
                <color field: "direction", type: "nominal",
                       scale: <scale domain: ["up", "down"],
                                     range: ["#26a69a", "#ef5350"]>>
            >
        >
    >
>;
```

**Implementation:**
- Uses existing `rule_mark` and `bar` mark via `layer` composition
- Requires `y2` encoding to be wired in both marks (rule already uses it; bar needs extension)
- Data preprocessing to compute `direction: (if (close >= open) "up" else "down")`
- Fixed bar width via `width` mark property

---

### Density / Violin Plot

Kernel density estimation visualized as a filled curve or mirrored density.

**Vega approach:** Vega uses `density` transform to compute KDE, then renders with `area` marks. Violin plots layer two mirrored area marks.

**Lambda spec:**

```lambda
// Density plot
let chart1 = <chart width: 600, height: 400;
    <data values: observations>
    <transform;
        <density field: "value", bandwidth: 0.5>
    >
    <mark type: "area">
    <encoding;
        <x field: "value", type: "quantitative">
        <y field: "density", type: "quantitative">
    >
>;

// Violin plot
let chart2 = <chart width: 600, height: 400;
    <data values: observations>
    <transform;
        <density field: "value", groupby: "category", bandwidth: 0.5>
    >
    <mark type: "area", orient: "horizontal">
    <encoding;
        <x field: "category", type: "nominal">
        <y field: "value", type: "quantitative">
        <size field: "density", type: "quantitative">
    >
>;
```

**Implementation:**
- New `density` transform using Gaussian kernel density estimation
- KDE algorithm: for each evaluation point, sum Gaussian kernels centered at data points
- Bandwidth selection: Silverman's rule of thumb or explicit `bandwidth` parameter
- Violin: mirror the density curve on both sides of a central axis per category

---

### Parallel Coordinates

Multiple axes for comparing multivariate data.

**Vega approach:** Vega constructs parallel coordinates via `fold` transform + `line` mark with `detail` encoding.

**Lambda spec:**

```lambda
let my_chart = <chart width: 700, height: 400;
    <data values: car_specs>
    <transform;
        <fold fields: ["mpg", "cylinders", "displacement", "horsepower", "weight"]>
    >
    <mark type: "line", opacity: 0.3>
    <encoding;
        <x field: "key", type: "nominal">
        <y field: "value", type: "quantitative">
        <detail field: "name">
        <color field: "origin", type: "nominal">
    >
    <config
        axis_independent_y: true
    >
>;
```

**Implementation:**
- Uses existing `fold` transform to unpivot columns into key-value pairs
- Each row in original data becomes a polyline across the parallel axes
- `detail` encoding groups points into lines per original record
- Challenge: each axis may need independent scale normalization (`axis_independent_y` config)
- New `normalize_parallel()` transform that maps each field's range to [0, 1]

---

## Proposed Advanced Features

### Stacking Engine

**Priority:** High — blocks stacked bar, stacked area, stream graphs

**Vega reference:** Vega's `stack` transform computes `y0` and `y1` fields for each data point, supporting `"zero"`, `"center"`, `"normalize"` offsets and various sort orders.

**Design:**

New module `stack.ls`:

```lambda
// stack.ls — Compute stacking offsets for marks

// Apply stack transform to data
// Returns data with added _y0 and _y1 fields
pub fn apply_stack(data, y_field, group_field, stack_mode, sort_field) = ...

// Stack modes:
// "zero"      — stack from zero baseline (standard stacked bar/area)
// "normalize" — normalize each stack to [0, 1] (100% stacked)
// "center"    — center the stack around the midpoint (stream graph)

// Internal: group data by x value, then accumulate offsets
fn stack_groups(groups, y_field, mode) = ...
```

**Integration points:**
- `chart.ls`: detect `stack` attribute in y-encoding, insert stack transform before mark rendering
- `mark.ls`: `bar()` and `area_mark()` use `_y0` field as baseline instead of 0
- `scale.ls`: y-domain computed from max `_y1` value across all stacks

---

### Facet Composition

**Priority:** High — enables small multiples, one of the most powerful analytical patterns

**Vega reference:** Vega uses `group` marks with a `from.facet` data source. Vega-Lite uses `facet`, `row`, `column` channels.

**Design:**

```lambda
// Row facet
<chart width: 200, height: 150;
    <data values: dataset>
    <mark type: "point">
    <encoding;
        <x field: "x_val", type: "quantitative">
        <y field: "y_val", type: "quantitative">
    >
    <facet field: "category", type: "nominal", columns: 3, spacing: 20>
>

// Row + Column facet
<chart width: 150, height: 150;
    <data values: dataset>
    <mark type: "point">
    <encoding;
        <x field: "x_val", type: "quantitative">
        <y field: "y_val", type: "quantitative">
    >
    <facet row_field: "region", column_field: "year",
           row_type: "nominal", column_type: "ordinal">
>
```

**Implementation:**
- `parse.ls`: extract `<facet>` element attributes
- `chart.ls`: when facet is present, partition data by facet field(s), render each partition as an independent sub-chart, then arrange sub-charts in a grid layout
- `layout.ls`: new `compute_facet_layout(n_facets, columns, sub_width, sub_height, spacing)` function
- Each sub-chart shares the same scales (computed globally) for comparability
- Facet headers (labels) generated as `<text>` elements above/left of each sub-plot
- SVG structure: outer `<svg>` with `<g>` groups positioned via `translate()`

---

### Concat Composition

**Priority:** Medium — enables dashboard-style layouts

**Vega reference:** Vega-Lite provides `hconcat` and `vconcat` for horizontal/vertical chart concatenation. Each sub-chart is independent (own data, scales, marks).

**Design:**

```lambda
// Already in the schema from Chart1.md
<hconcat spacing: 20;
    <chart; ...chart A... >
    <chart; ...chart B... >
>

<vconcat spacing: 20;
    <chart; ...chart C... >
    <chart; ...chart D... >
>
```

**Implementation:**
- `parse.ls`: detect `<hconcat>` / `<vconcat>` as top-level elements
- Render each child `<chart>` independently via `render()`
- Position sub-SVGs in a row (hconcat) or column (vconcat) with spacing
- Each sub-chart gets its own viewport, no shared scales
- Allow nested concat for grid layouts: `<vconcat; <hconcat; ...> <hconcat; ...> >`

---

### Repeat Composition

**Priority:** Medium — enables scatter plot matrices and parameterized chart grids

**Vega reference:** Vega-Lite `repeat` specifies a set of fields and repeats a chart template for each combination.

**Design:**

```lambda
<repeat
    column: ["sepal_length", "sepal_width", "petal_length"],
    row: ["sepal_length", "sepal_width", "petal_length"];
    <chart width: 150, height: 150;
        <data values: iris>
        <mark type: "point", size: 5>
        <encoding;
            <x field: {repeat: "column"}, type: "quantitative">
            <y field: {repeat: "row"}, type: "quantitative">
            <color field: "species", type: "nominal">
        >
    >
>
```

**Implementation:**
- Generate the cartesian product of row × column field lists
- For each combination, substitute `{repeat: "column"}` and `{repeat: "row"}` in the template
- Render each variant chart, arrange in a grid
- Shared color scales across all sub-charts for consistency

---

### Temporal Scale & Time Formatting

**Priority:** High — currently the biggest gap in scale coverage

**Vega reference:** Vega provides `time` and `utc` scale types with time-unit binning (year, month, week, day, hour, minute, second), localized tick formatting, and multi-level tick labels.

**Design:**

```lambda
// Time scale inferred from type: "temporal"
<encoding;
    <x field: "date", type: "temporal",
       time_unit: "yearmonth",
       axis: <axis format: "%b %Y", label_angle: -45>>
    <y field: "value", type: "quantitative">
>
```

**Scale implementation in `scale.ls`:**

```lambda
// time_scale: maps datetime values to pixel range
// domain: [min_date, max_date] from data
// range: [0, plot_width]
pub fn time_scale(domain, range) = ...

// Time-aware tick generation
pub fn time_ticks(domain, target_count) = ...
// Auto-selects appropriate interval: years, months, weeks, days, hours, minutes
```

**Time units** (for binning temporal data):

| Unit | Groups by | Format example |
|------|----------|---------------|
| `"year"` | Year | "2024" |
| `"yearmonth"` | Year + month | "Jan 2024" |
| `"yearmonthdate"` | Full date | "2024-01-15" |
| `"month"` | Month (1–12) | "January" |
| `"day"` | Day of week (0–6) | "Monday" |
| `"hours"` | Hour (0–23) | "14:00" |
| `"hoursminutes"` | Hour + minute | "14:30" |

**Dependencies:**
- Lambda's `datetime` type and `date_*` system functions for date parsing and arithmetic
- `format_date()` or equivalent for tick label formatting

---

### Selection & Conditional Encoding

**Priority:** Low (non-interactive) — but useful for static highlighting

**Vega reference:** Vega-Lite selections drive interactive filtering, zooming, and highlighting. Vega exposes full signal/event-stream reactivity.

For Lambda's non-interactive SVG output, we adapt selections as **static highlight conditions:**

```lambda
<encoding;
    <color
        condition: <cond test: "~.year == 2024", value: "#e45756">,
        value: "#ccc">
    <opacity
        condition: <cond test: "~.revenue > 1000", value: 1.0>,
        value: 0.3>
>
```

**Implementation:**
- `condition` attribute on any encoding channel
- Evaluates `test` expression per data point
- If true, uses `condition.value`; otherwise uses fallback `value`
- No interactivity — purely data-driven conditional mapping
- Implementation in `chart.ls`: resolve condition before scale application

---

### Tooltip Generation

**Priority:** Medium — improves chart usability in HTML context

**Vega reference:** Vega-Lite generates `<title>` SVG elements or custom HTML tooltips.

**Design:**

```lambda
<encoding;
    <tooltip field: "name">                          // single field
    <tooltip fields: ["name", "value", "category"]>  // multiple fields
>
```

**Implementation:**
- Generate `<title>` child element inside each mark's SVG element
- Content: field name(s) and value(s) formatted as text
- Browsers natively display `<title>` as hover tooltip
- No JavaScript required — pure SVG behavior
- `svg.ls`: add `svg_title(text)` helper

---

### Annotation Layer

**Priority:** Medium — adds explanatory context to charts

**Vega reference:** Vega uses `rule` and `text` marks with `datum` encoding (literal data values positioned on scales).

**Design:**

```lambda
<chart width: 600, height: 400;
    <data values: time_series>
    <mark type: "line">
    <encoding; ...>
    <annotations;
        <rule x_value: "2024-03", stroke: "red", stroke_dash: [4, 4]>
        <text x_value: "2024-03", y_value: 150,
              text: "Product Launch", anchor: "start", dx: 5>
        <rect x_value: "2024-01", x2_value: "2024-03",
              fill: "#eee", opacity: 0.5>
    >
>
```

**Implementation:**
- New `annotation.ls` module
- Annotations use data-space coordinates, mapped through the chart's scales
- Rendered as standard SVG elements in a dedicated layer between grid and marks
- Types: `<rule>` (reference line), `<text>` (label), `<rect>` (shaded region)

---

### Responsive Sizing

**Priority:** Low

**Vega reference:** Vega-Lite supports `width: "container"` and `height: "container"` for responsive sizing.

**Design:**

```lambda
<chart width: "auto", height: "auto", aspect_ratio: 1.6;
    ...
>
```

**Implementation:**
- `"auto"` width/height uses a default (e.g., 600×375) with `viewBox` set on the SVG root
- `viewBox` attribute enables browser-side scaling: `<svg viewBox="0 0 600 375">`
- `aspect_ratio` constrains height relative to width
- `preserveAspectRatio="xMidYMid meet"` for proportional scaling

---

### SVG Gradients & Patterns

**Priority:** Medium — improves legend quality and enables advanced mark fills

**Vega reference:** Vega supports `linearGradient`, `radialGradient` in mark fill/stroke, and SVG `<pattern>` for hatching.

**Design in `svg.ls`:**

```lambda
// Generate SVG <defs> block with gradient definitions
pub fn svg_linear_gradient(id, stops) = ...
// stops: [{offset: 0, color: "#f00"}, {offset: 1, color: "#00f"}]

pub fn svg_radial_gradient(id, stops) = ...

// Hatch pattern for accessibility (colorblind-friendly)
pub fn svg_hatch_pattern(id, angle, color, spacing) = ...
```

**Integration:**
- Add `<defs>` section to SVG root in assembly phase
- Gradient legends use `url(#gradient_id)` fill instead of discrete rectangles
- Mark fills can reference gradients: `fill: "gradient:blues"`

---

### Config / Theming System

**Priority:** Medium

**Vega reference:** Vega and Vega-Lite support a top-level `config` object that sets defaults for all marks, axes, legends, titles, and scales.

**Design:**

```lambda
// Predefined themes
let dark_theme = {
    background: "#1a1a2e",
    color: "#e0e0e0",
    font: "monospace",
    axis_domain_color: "#555",
    axis_tick_color: "#555",
    axis_label_color: "#aaa",
    axis_title_color: "#ccc",
    grid_color: "#333",
    title_color: "#fff",
    mark_color: "#0f94d2",
    legend_label_color: "#aaa"
};

// Apply theme
let my_chart = <chart width: 600, height: 400;
    <config theme: dark_theme>
    <data values: data>
    <mark type: "bar">
    <encoding; ...>
>;
```

**Implementation:**
- `config.ls` module: merge user config with defaults, resolve theme maps
- Config cascades: theme defaults → config element → encoding-level overrides
- All rendering functions receive a `config` map parameter
- Built-in themes: `default`, `dark`, `minimal`, `presentation`
- Axis, legend, mark renderers read config for colors, fonts, sizes

---

### Window Transform

**Priority:** High — enables running totals, cumulative distributions, rankings

**Vega reference:** Vega provides `window` transform with frame control, sort order, and partition-by.

**Schema already defined in Chart1.md.** Implementation:

```lambda
// window.ls or extend transform.ls
fn apply_window(data, ops, sort_field, sort_order, group_field, frame) = ...
```

**Supported operations:**

| Operation | Description |
|-----------|-------------|
| `"row_number"` | Row index within partition |
| `"rank"` | Rank (with ties) |
| `"dense_rank"` | Rank without gaps |
| `"sum"` | Running sum |
| `"mean"` | Running mean |
| `"count"` | Running count |
| `"min"` | Running minimum |
| `"max"` | Running maximum |
| `"first_value"` | First value in frame |
| `"last_value"` | Last value in frame |
| `"lag"` | Previous row's value |
| `"lead"` | Next row's value |
| `"ntile"` | Quantile bucket assignment |
| `"percent_rank"` | Percentile rank [0, 1] |
| `"cumulative_dist"` | Cumulative distribution value |

**Frame control:**
- `frame: [null, 0]` — unbounded preceding to current row (cumulative)
- `frame: [-2, 2]` — 2 rows before to 2 rows after (sliding window)
- `frame: [null, null]` — entire partition (default)

---

### Lookup Transform

**Priority:** Medium — enables data joins

**Vega reference:** Vega provides `lookup` transform to join data from a secondary source based on key matching.

```lambda
<transform;
    <lookup from_data: region_names,
            from_key: "code",
            fields: ["name", "population"],
            key: "region_code">
>
```

**Implementation:**
- Build a lookup map from `from_data` keyed by `from_key`
- For each row in main data, find matching row and copy specified `fields`
- Equivalent to a left-join on `key = from_key`

---

### Regression & Loess Transforms

**Priority:** Low — analytical overlay for scatter plots

**Vega reference:** Vega-Lite provides `regression` (linear, polynomial) and `loess` (local regression) transforms.

```lambda
<transform;
    <regression
        method: "linear" | "poly" | "exp" | "log" | "pow",
        x_field: "x",
        y_field: "y",
        order: 2,         // polynomial order (for "poly")
        as: ["rx", "ry"]  // output field names
    >
>
```

**Implementation:**
- Linear: least-squares fit (closed-form solution)
- Polynomial: matrix inversion or iterative fitting up to order 5
- Generate N evaluation points along x-range for smooth curve
- Output as new data rows with computed x/y values, suitable for layering with `line` mark

---

### Projection (Geo) Support

**Priority:** Low (future) — requires GeoJSON parsing

**Vega reference:** Vega supports `mercator`, `equirectangular`, `albersUsa`, `orthographic` and 20+ other map projections. GeoJSON features are projected and rendered as `path` elements.

```lambda
<chart width: 800, height: 500;
    <data url: "world.geojson", format: "geojson">
    <mark type: "geoshape">
    <encoding;
        <color field: "gdp_per_capita", type: "quantitative",
               scale: <scale scheme: "viridis">>
    >
    <projection type: "mercator", scale: 100, center: [0, 30]>
>
```

**Implementation (future):**
- GeoJSON input parser (integrate with Lambda's `input` system)
- Projection math module: lat/lon → x/y for mercator, equirectangular, etc.
- `geoshape_mark()`: project GeoJSON polygons to SVG `<path>` elements
- Choropleth coloring via standard color encoding

---

## Schema Additions

### Stack Schema

Added as attribute on encoding channels:

```lambda
<y field: "value", type: "quantitative",
   stack: "zero" | "normalize" | "center" | null
>
```

| Mode | Description |
|------|-------------|
| `"zero"` | Stack from zero baseline (default for bar/area with color) |
| `"normalize"` | Scale each stack to [0, 1] range |
| `"center"` | Center stacks around midpoint (stream graph) |
| `null` | Disable stacking (overlay) |

---

### Facet Schema

```lambda
<facet
    // Single-field facet
    field: string,                // field to partition by
    type: DataType,               // data type

    // Two-field facet (row × column)
    row_field?: string,
    row_type?: DataType,
    column_field?: string,
    column_type?: DataType,

    // Layout
    columns?: int = 3,            // wrap after N columns (single-field only)
    spacing?: int = 20,           // gap between sub-plots (pixels)

    // Headers
    header?: <header
        title_font_size?: int = 13,
        label_font_size?: int = 11,
        label_color?: string = "#333"
    >
>
```

---

### Window Transform Schema

```lambda
<window;
    // Operations (one or more)
    <win op: WindowOp,
         field?: string,          // source field (not needed for row_number, rank)
         as: string,              // output field name
         param?: int              // op-specific parameter (e.g., lag offset)
    >

    // Sort (required for rank, row_number, lag, lead)
    <sort field: string, order?: "ascending" | "descending">

    // Partition (optional)
    <group field: string>

    // Frame (optional, default: [null, null])
    <frame from?: int | null, to?: int | null>
>
```

---

### Selection Schema

```lambda
<cond
    test: string,                 // Lambda expression using ~ for current row
    value: any                    // value when test is true
>
```

Used via `condition` attribute on encoding channels:

```lambda
<color
    condition: <cond test: "~.year == 2024", value: "#e45756">,
    value: "#ccc"
>
```

---

### Projection Schema

```lambda
<projection
    type: "mercator" | "equirectangular" | "albers" | "orthographic" | "natural_earth",
    center?: [longitude, latitude],
    scale?: number,
    translate?: [x, y],
    rotate?: [lambda, phi, gamma],
    clip_angle?: number
>
```

---

### Tooltip Schema

As encoding channel:

```lambda
<tooltip
    field?: string,               // single field
    fields?: array,               // multiple fields: ["name", "value"]
    format?: string               // number format for values
>
```

---

### Annotation Schema

```lambda
<annotations;
    // Vertical or horizontal reference line
    <rule
        x_value?: any,            // data-space x coordinate
        y_value?: any,            // data-space y coordinate
        stroke?: string = "#666",
        stroke_width?: float = 1,
        stroke_dash?: array       // e.g., [4, 4]
    >

    // Text label
    <text
        x_value: any,             // data-space x
        y_value: any,             // data-space y
        text: string,             // label content
        anchor?: "start" | "middle" | "end" = "start",
        dx?: int = 0,             // pixel x offset
        dy?: int = 0,             // pixel y offset
        font_size?: int = 11,
        color?: string = "#333"
    >

    // Shaded region
    <rect
        x_value?: any,
        x2_value?: any,
        y_value?: any,
        y2_value?: any,
        fill?: string = "#eee",
        opacity?: float = 0.3
    >
>
```

---

## SVG Generation Additions

New SVG elements and patterns required:

| Feature | SVG Output | Module |
|---------|-----------|--------|
| Gradients | `<defs><linearGradient>...</linearGradient></defs>` | `svg.ls` |
| Patterns | `<defs><pattern>...</pattern></defs>` | `svg.ls` |
| Clip paths | `<defs><clipPath><rect.../></clipPath></defs>` | `svg.ls` |
| Tooltip | `<title>` inside mark elements | `svg.ls` |
| Grouped bars | Multiple `<rect>` per x-band | `mark.ls` |
| Stacked bars | `<rect>` with computed y-offsets | `mark.ls` |
| Box plots | `<rect>` + `<line>` + `<circle>` composite | `mark.ls` |
| Error bars | `<line>` + cap `<line>` elements | `mark.ls` |
| Radar grid | Concentric `<circle>` + radial `<line>` | `axis.ls` |
| Sunburst arcs | Nested `<path>` arcs at varying radii | `mark.ls` |
| Treemap rects | Positioned `<rect>` + `<text>` labels | `mark.ls` |
| Geoshape paths | `<path>` from projected GeoJSON | `mark.ls` (future) |
| Facet groups | `<g transform="translate(...)">` per sub-plot | `layout.ls` |
| Annotations | `<line>`, `<text>`, `<rect>` in annotation layer | `annotation.ls` |

---

## Module Changes

Summary of new and modified modules:

| Module | Change | Description |
|--------|--------|-------------|
| `chart.ls` | **Modify** | Add stacking, facet, concat, repeat dispatch; config resolution; annotation rendering |
| `parse.ls` | **Modify** | Parse new elements: `<facet>`, `<annotations>`, `<projection>`, `<cond>`; new channels: `x_offset`, `x2`, `y2`, `theta2`, `radius`, `radius2`, `detail`, `tooltip` |
| `mark.ls` | **Modify** | Add: `boxplot_mark()`, `errorbar_mark()`, `errorband_mark()`, `rect_mark()` dispatch; extend `bar()` for stacking + y2 + grouped; extend `area_mark()` for stacking |
| `scale.ls` | **Modify** | Add: `time_scale()`, `time_ticks()`; `scale_reverse()` support |
| `axis.ls` | **Modify** | Time axis formatting; radar axis (radial spokes + concentric rings) |
| `legend.ls` | **Modify** | Gradient legend with SVG `<linearGradient>`; size legend with variable symbols |
| `transform.ls` | **Modify** | Add: window, lookup, density, regression transform dispatch |
| `layout.ls` | **Modify** | Add: `compute_facet_layout()`, `compute_concat_layout()`, `radar_layout()` |
| `color.ls` | **Modify** | Add diverging scale interpolation; more schemes (warm, cool, turbo, cividis) |
| `svg.ls` | **Modify** | Add: `svg_linear_gradient()`, `svg_radial_gradient()`, `svg_pattern()`, `svg_clip_path()`, `svg_title()`, `svg_defs()` |
| `util.ls` | **Modify** | Add: KDE kernel functions, matrix utilities for regression |
| `vega.ls` | **Modify** | Handle new Vega-Lite features: stack, facet, concat, repeat, window, selection |
| `stack.ls` | **New** | Stacking engine: `apply_stack()` |
| `hierarchy.ls` | **New** | Tree layout algorithms: partition (sunburst), squarify (treemap) |
| `config.ls` | **New** | Theme system: merge configs, built-in themes, cascading defaults |
| `annotation.ls` | **New** | Annotation rendering: reference lines, labels, shaded regions |
| `window.ls` | **New** | Window transform: running aggregates, rank, lag/lead |

---

## Implementation Roadmap

### Phase A — Stacking & Composition (High Priority)

Foundation for multi-series and multi-view charts.

| Task | Module | Dependencies | Complexity | Status |
|------|--------|-------------|------------|--------|
| Stacking engine | `stack.ls` | — | Medium | ✅ Done |
| Stacked bar mark | `mark.ls` | stack.ls | Low | ✅ Done |
| Stacked area mark | `mark.ls` | stack.ls | Low | ✅ Done |
| Facet composition | `chart.ls`, `layout.ls`, `parse.ls` | — | High | ❌ |
| hconcat / vconcat | `chart.ls`, `layout.ls`, `parse.ls` | — | Medium | ❌ |
| Repeat composition | `chart.ls`, `parse.ls` | concat | Medium | ❌ |
| Grouped bar (x_offset) | `mark.ls`, `parse.ls` | — | Medium | ✅ Done |

**Deliverable:** Stacked bar, stacked area, grouped bar, faceted small multiples, dashboard layouts.

### Phase B — Statistical & Composite Marks

Complex marks composed from primitives.

| Task | Module | Dependencies | Complexity |
|------|--------|-------------|------------|
| Box plot | `mark.ls` | aggregate (q1/q3) | Medium |
| Error bar | `mark.ls` | — (uses y/y2) | Low |
| Error band | `mark.ls` | — (uses area) | Low |
| Histogram (bin + count) | `chart.ls` | bin transform | Low |
| Heatmap (rect mark) | `mark.ls` | diverging color | Low |
| Candlestick | layer (rule + bar) | y2 wiring | Low |
| Bubble chart | `mark.ls`, `chart.ls` | size scale wiring | Low |

**Deliverable:** Statistical charts: box plots, histograms, heatmaps, error bars, candlesticks.

### Phase C — Temporal & Theming

Time-series support and visual polish.

| Task | Module | Dependencies | Complexity |
|------|--------|-------------|------------|
| Temporal scale | `scale.ls` | datetime support | High |
| Time axis formatting | `axis.ls` | time scale | Medium |
| Config / theming | `config.ls`, all renderers | — | Medium |
| SVG gradients | `svg.ls`, `legend.ls` | — | Low |
| SVG clip paths | `svg.ls` | — | Low |
| Tooltip generation | `svg.ls`, `mark.ls` | — | Low |
| Annotation layer | `annotation.ls` | — | Medium |
| Conditional encoding | `chart.ls` | — | Medium |
| Detail channel | `parse.ls`, `mark.ls` | — | Low |

**Deliverable:** Time-series charts, dark/light themes, annotated charts, tooltips.

### Phase D — Advanced Transforms

Analytical transforms for derived data.

| Task | Module | Dependencies | Complexity |
|------|--------|-------------|------------|
| Window transform | `window.ls` | — | High |
| Lookup transform | `transform.ls` | — | Medium |
| Density (KDE) | `transform.ls`, `util.ls` | — | High |
| Linear regression | `transform.ls`, `util.ls` | — | Medium |
| Polynomial regression | `transform.ls`, `util.ls` | matrix math | High |
| Loess | `transform.ls`, `util.ls` | — | High |

**Deliverable:** Waterfall charts, running totals, trend lines, density plots, cumulative distributions.

### Phase E — Hierarchical & Specialized

Advanced chart types requiring new layout algorithms.

| Task | Module | Dependencies | Complexity |
|------|--------|-------------|------------|
| Treemap (squarify) | `hierarchy.ls`, `mark.ls` | — | High |
| Sunburst | `hierarchy.ls`, `mark.ls` | arc_mark | High |
| Radar chart | `layout.ls`, `axis.ls`, `mark.ls` | — | High |
| Parallel coordinates | `chart.ls`, `axis.ls` | fold, detail | Medium |
| Slope chart | `mark.ls` | detail, scale reverse | Low |
| Violin plot | `mark.ls`, `transform.ls` | density (KDE) | High |

**Deliverable:** Treemaps, sunbursts, radar charts, parallel coordinates, violin plots.

### Phase F — Geo & Future (Long-term)

Geographic visualization and advanced features.

| Task | Module | Dependencies | Complexity |
|------|--------|-------------|------------|
| GeoJSON input parser | `input/` | — | High |
| Map projections | `projection.ls` | — | High |
| Geoshape mark | `mark.ls` | projection, GeoJSON | High |
| Choropleth maps | chart pipeline | geoshape + color encoding | Medium |
| Responsive sizing | `chart.ls`, `svg.ls` | — | Low |
| Stream graph | `mark.ls` | stack center mode | Medium |
| Trail mark | `mark.ls` | — | Medium |

**Deliverable:** Geographic maps, responsive charts, stream graphs.

---

## Test Plan

Each new chart type and feature requires:

1. **Unit test script** (`test/lambda/chart/test_<name>.ls`) — constructs a chart spec, renders to SVG, outputs the result
2. **Expected output file** (`test/lambda/chart/test_<name>.txt`) — verified SVG string
3. **Visual verification** — render SVG in browser to visually confirm correctness before capturing expected output

### Planned Tests by Phase

| Phase | Tests |
|-------|-------|
| **A** | `test_stacked_bar`, `test_stacked_area`, `test_grouped_bar`, `test_facet`, `test_hconcat`, `test_vconcat`, `test_repeat` |
| **B** | `test_boxplot`, `test_errorbar`, `test_histogram`, `test_heatmap`, `test_candlestick`, `test_bubble` |
| **C** | `test_temporal_axis`, `test_theme_dark`, `test_annotation`, `test_tooltip`, `test_conditional_color` |
| **D** | `test_window_running_sum`, `test_window_rank`, `test_lookup_join`, `test_density`, `test_regression_line` |
| **E** | `test_treemap`, `test_sunburst`, `test_radar`, `test_parallel_coords`, `test_slope`, `test_violin` |
| **F** | `test_choropleth` (when geo support lands) |

### Regression Safety

All existing tests (bar, line, scatter, arc, area, donut, text, rule, tick, layered) must continue to pass after each phase. Run `make test-lambda-baseline` after every module change.

---

## Implementation Progress

**Last updated:** 2025-03-18

### Phase A — Stacking & Composition

| Task | Status | Notes |
|------|--------|-------|
| Stacking engine | ✅ Done | New `stack.ls` module: `apply_stack(data, y_field, group_field, x_field, mode)` with "zero", "normalize", "center" modes |
| Stacked bar mark | ✅ Done | `bar()` in `mark.ls` uses `_y0`/`_y1` fields for stacked positioning |
| Stacked area mark | ✅ Done | `area_mark()` in `mark.ls` uses `_y0`/`_y1` for stacked area baselines |
| Grouped bar (x_offset) | ✅ Done | `x_offset` channel parsed in `parse.ls` and `vega.ls`; `bar()` subdivides band width by group count |
| Facet composition | ❌ Not started | |
| hconcat / vconcat | ❌ Not started | |
| Repeat composition | ❌ Not started | |

### Files Changed

| File | Change | Details |
|------|--------|---------|
| `lambda/package/chart/stack.ls` | **New** | Stacking engine (67 lines). Computes cumulative `_y0`/`_y1` offsets per group. Uses `{*:row, *:extra}` spread to add stack fields while preserving original data. |
| `lambda/package/chart/chart.ls` | **Modified** | Added `import stack: .stack`. Rewrote `render_single()` to detect stacking via `detect_stack_mode()`, apply `stack.apply_stack()`, build stacked y-scale via `build_stacked_y_scale()`, extract `x_offset` channel, and pass `is_stacked`/`x_offset_field`/`x_offset_cats` in mark context. |
| `lambda/package/chart/mark.ls` | **Modified** | `bar()`: extracts `is_stacked`, `x_offset_field`, `x_offset_cats` from ctx; computes `n_groups` for grouped bars; subdivides band width with 2px sub-gap; uses `_y0`/`_y1` for stacked bar y-positioning. `area_mark()`: extracts `is_stacked`; stacked areas use `_y1` for top points, `_y0` for bottom baseline. Added `find_cat_index()` helper. |
| `lambda/package/chart/parse.ls` | **Modified** | `parse_encoding()` now extracts `x_offset`, `x2`, `y2`, `detail`, `tooltip` channels via `find_child()`. |
| `lambda/package/chart/vega.ls` | **Modified** | `convert_encoding()` maps Vega-Lite `xOffset`/`x_offset`, `x2`, `y2`, `detail`, `tooltip` channels. `convert_channel()` converts `stack: false` → `"none"` for explicit stack disable. |

### Tests Added

| Test | Description |
|------|-------------|
| `test/lambda/chart/test_stacked_bar_chart.ls` | Stacked bar with 3 categories × 2 products, auto-stacked via color encoding |
| `test/lambda/chart/test_grouped_bar_chart.ls` | Grouped bar with `xOffset` channel and `stack: false`, side-by-side bars |
| `test/lambda/chart/test_stacked_area_chart.ls` | Stacked area with 3 months × 2 products, stacked baselines |

### Test Results

All **649 baseline tests pass** (643 pre-existing + 6 new across MIR-direct and C2MIR backends). Zero regressions.

### Implementation Notes

- **Auto-stacking behavior**: Bar and area marks with a `color` encoding automatically stack to `"zero"` mode, matching Vega-Lite defaults. This is disabled when `x_offset` is present (grouped bars) or when `stack: false`/`"none"` is explicitly set on the y-channel.
- **Map spread syntax**: Lambda uses `{*:map1, *:map2}` for map merging (not `{*map1, key: val}`). The `add_stack_fields()` function creates a separate `{_y0: y0, _y1: y1}` map and spreads it with the original row.
- **Stack detection**: `detect_stack_mode()` in `chart.ls` checks `y_ch.stack` for explicit modes, falls back to `"zero"` auto-detection for bar/area with color encoding but no x_offset.

---

*This proposal extends the Lambda Chart Library from its current Vega-Lite–inspired core toward full Vega-level capability, adapted for Lambda Script's pure functional model and SVG output pipeline.*
