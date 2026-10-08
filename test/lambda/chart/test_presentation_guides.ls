import chart: lambda.chart.chart
import vega: lambda.chart.vega
import axis: lambda.chart.axis
import scale: lambda.chart.scale

fn elements(tree) => if (tree is element) [tree, for (child in content(tree)) for (item in elements(child)) item] else []
fn with_class(tree, label) => elements(tree) |: ~.class == label
fn tags(tree, tag) => elements(tree) |: name(~) == tag

let records = [{x: 0, low: 0, high: 0, category: "A"}, {x: 1, low: 10, high: 1000, category: "B"}]
let independent = chart.render_spec(vega.convert({width: 450, height: 300, padding: 0, data: {values: records},
    resolve: {scale: {y: "independent"}}, encoding: {x: {field: "x", type: "quantitative", axis: null}},
    layer: [{mark: "point", encoding: {y: {field: "low", type: "quantitative", scale: {nice: false}}}},
        {mark: "point", encoding: {y: {field: "high", type: "quantitative", scale: {nice: false}}}}]}))
let independent_points = tags(independent, 'circle')
let shared = chart.render_spec(vega.convert({width: 450, height: 300, padding: 0, data: {values: records},
    encoding: {x: {field: "x", type: "quantitative", axis: null}},
    layer: [{mark: "point", encoding: {y: {field: "low", type: "quantitative"}}},
        {mark: "point", encoding: {y: {field: "high", type: "quantitative"}}}]}))
let shared_points = tags(shared, 'circle')
let guides = chart.render(<chart width: 600, height: 400, padding: 0,
    <data values: records> <mark type: "point">
    <encoding <x field: "x", dtype: "quantitative", axis: null> <y field: "low", dtype: "quantitative", axis: null>
        <size field: "high", dtype: "quantitative", scale: {domain: [0, 1000], nice: false},
            legend: {values: [0, 500, 1000], format: ".2~s", orient: "right"}>
        <shape field: "category", dtype: "nominal", scale: {domain: ["B", "A"], range: ["square", "diamond"]},
            legend: {orient: "bottom", columns: 2, symbol_fill_color: "purple"}>
    >
>)
let size_legend = with_class(guides, "legend size-legend")[0]
let shape_legend = with_class(guides, "legend shape-legend")[0]
let hidden = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: records> <mark type: "point">
    <encoding <x value: 10> <y value: 10>
        <size field: "high", dtype: "quantitative", legend: null>
        <shape field: "category", dtype: "nominal", legend: false>>
>)
let arc = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{category: "A", value: 1}, {category: "B", value: 2}]>
    <mark type: "arc">
    <encoding <theta field: "value", dtype: "quantitative">
        <color field: "category", dtype: "nominal", legend: {orient: "bottom", columns: 2}>
        <opacity value: 0> <stroke value: "purple"> <tooltip fields: ["category", "value"]>>
>)
let arc_legend = with_class(arc, "legend")[0]
let arc_paths = tags(arc, 'path')
let error_bars = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{x: 1, lo: 2, hi: 4}]> <mark type: "errorbar", stroke_width: 3>
    <encoding <x field: "x", dtype: "quantitative", axis: null> <y field: "lo", dtype: "quantitative", axis: null>
        <y2 field: "hi"> <color value: "red"> <opacity value: 0> <tooltip field: "hi", format: ".1f">>
>)
let error_lines = tags(error_bars, 'line')
let texts = chart.render(<chart width: 100, height: 100, padding: 0,
    <data values: [{label: "hello"}]> <mark type: "text", font_family: "serif", font_weight: 700>
    <encoding <x value: 10> <y value: 10> <text field: "label"> <color value: "red">
        <opacity value: 0> <stroke value: "blue"> <tooltip field: "label">>
>)
let text = tags(texts, 'text')[0]
let band = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{x: 0, lo: 1, hi: 2, group: "A"}, {x: 0, lo: 3, hi: 4, group: "B"},
        {x: 1, lo: 2, hi: 3, group: "A"}, {x: 1, lo: 4, hi: 5, group: "B"}]>
    <mark type: "errorband">
    <encoding <x field: "x", dtype: "quantitative", axis: null> <y field: "lo", dtype: "quantitative", axis: null>
        <y2 field: "hi"> <color field: "group", dtype: "nominal", legend: null> <tooltip field: "group">>
>)
let box = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [for (v in [1, 2, 3, 100]) {category: "A", value: v}]>
    <mark type: "boxplot", color: "red", stroke: "purple", opacity: 0>
    <encoding <x field: "category", dtype: "nominal", axis: null> <y field: "value", dtype: "quantitative", axis: null,
        scale: {reverse: true}> <tooltip field: "_median", format: ".1f">>
>)
let box_rect = with_class(box, "marks boxplots")[0][4]
let clipped = chart.render(<chart width: 300, height: 200, padding: 0, clip: true,
    <data values: [{x: -10, y: 20}, {x: 10, y: 0}]> <mark type: "point">
    <encoding <x field: "x", dtype: "quantitative", scale: {domain: [0, 10]}, axis: null>
        <y field: "y", dtype: "quantitative", scale: {domain: [0, 10]}, axis: null>>
    <annotation <region_note x: 2, x2: 5, y: 2, y2: 8, opacity: 0.3, text: "range">>
>)
let region = with_class(clipped, "annotation-region")[0]
let clip = with_class(clipped, "plot-clip")[0]
let stack_rows = [{x: "A", group: "one", v: 3}, {x: "A", group: "two", v: 7}]
let stack_layer = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: stack_rows>
    <encoding <x field: "x", dtype: "nominal", axis: null> <y field: "v", dtype: "quantitative", axis: null>
        <color field: "group", dtype: "nominal", legend: null>>
    <layer <chart <mark type: "bar">> <chart <mark type: "point">>>
>)
let stacked_bars = with_class(stack_layer, "marks bars")[0]
let normal_height = axis.estimate_x_axis_height({}, false, scale.point_scale(["Long categorical label"], 0, 100, 0))
let rotated_height = axis.estimate_x_axis_height({label_angle: 60}, false, scale.point_scale(["Long categorical label"], 0, 100, 0))
let horizontal = chart.render(<chart width: 400, height: 250,
    <data values: records> <mark type: "point"> <encoding <x value: 20> <y value: 20>
        <color field: "high", dtype: "quantitative", scale: {domain: [0, 1000]},
            legend: {direction: "horizontal", orient: "top", format: ".2~s"}>>
>)
let gradient = with_class(horizontal, "legend gradient-legend")[0]
let styled_guide = chart.render(<chart <data values: records> <mark type: "point">
    <encoding <color field: "category", dtype: "nominal", legend: {symbol_fill_color: "pink",
        symbol_stroke_color: "purple", symbol_stroke_width: 2, symbol_opacity: 0.5}>>
>)
let guide_symbol = tags(with_class(styled_guide, "legend")[0], 'rect')[0]
let invalid_leaf = chart.render(<chart <data values: [{text: "Invalid", weight: 0}]>
    <layer <chart <mark type: "wordcloud", clip: true> <annotation <text_note text: "note">>>>
>)
let sorted_layer = chart.render(<chart <data values: records>
    <encoding <x field: "category", dtype: "nominal", axis: null, sort: {field: "high", order: "descending"}>
        <y field: "low", dtype: "quantitative", axis: null>>
    <layer <chart <mark type: "point">> <chart <mark type: "line">>>
>)
let filled = chart.render(<chart <data values: [{x: 1, y: 1}]>
    <mark type: "point", fill: "purple", stroke_dash: "2,3">
    <encoding <x field: "x", dtype: "quantitative", axis: null> <y field: "y", dtype: "quantitative", axis: null>>
>)
let cloud_layer = chart.render(<chart width: 400, height: 250,
    <encoding <color field: "category", dtype: "nominal">>
    <layer
        <chart <data values: [{text: "Cloud", weight: 1, category: "A"}]>
            <mark type: "wordcloud", font_size: 12> <encoding <size field: "weight", dtype: "quantitative">>>
        <chart <data values: [{category: "B"}]> <mark type: "point"> <encoding <x value: 20> <y value: 20>>>>
>)
let checks = {
    independent_positions: independent_points[1].cy == independent_points[3].cy,
    independent_axes: len(with_class(independent, "axis y-axis")) == 2,
    opposite_axes: with_class(independent, "axis y-axis")[0][0].x1 == 0 and
        with_class(independent, "axis y-axis")[1][0].x1 > 0,
    shared_positions: shared_points[1].cy > shared_points[3].cy,
    shared_axis: len(with_class(shared, "axis y-axis")) == 1,
    size_entries: len(with_class(size_legend, "legend-entry")) == 3,
    size_mapping: tags(size_legend, 'circle')[0].r < tags(size_legend, 'circle')[2].r,
    size_format: tags(size_legend, 'text')[3][0] == "1k",
    shape_entries: len(with_class(shape_legend, "legend-entry")) == 2,
    shape_mapping: tags(shape_legend, 'rect')[0].fill == "purple" and len(tags(shape_legend, 'path')) == 1,
    shape_columns: with_class(shape_legend, "legend-entry")[0].transform != with_class(shape_legend, "legend-entry")[1].transform,
    suppression: len(with_class(hidden, "legend size-legend")) == 0 and len(with_class(hidden, "legend shape-legend")) == 0,
    arc_bottom: len(with_class(arc_legend, "legend-entry")) == 2 and contains(arc[2].transform, "translate(0, "),
    arc_style: arc_paths[0].opacity == 0 and arc_paths[0].stroke == "purple",
    arc_tooltip: tags(arc_paths[0], 'title')[0][0] == "category: A\nvalue: 1",
    error_style: len(error_lines) == 3 and error_lines[0].stroke == "red" and error_lines[2].opacity == 0 and error_lines[1]["stroke-width"] == 3,
    error_tooltips: len(tags(error_bars, 'title')) == 3 and tags(error_bars, 'title')[0][0] == "4.0",
    text_style: text.fill == "red" and text.opacity == 0 and text.stroke == "blue" and text["font-family"] == "serif",
    text_tooltip: tags(text, 'title')[0][0] == "hello",
    band_series: len(tags(band, 'path')) == 2 and tags(band, 'path')[0].fill != tags(band, 'path')[1].fill,
    band_tooltip: len(tags(band, 'title')) == 2,
    box_style: box_rect.fill == "red" and box_rect.stroke == "purple" and box_rect.opacity == 0 and box_rect.height >= 0,
    box_summary: tags(box_rect, 'title')[0][0] == "2.5",
    clip_viewport: clip.width == 300 and clip.height == 200 and clip.overflow == "hidden",
    clip_geometry: tags(clip, 'circle')[0].cx < 0,
    region_geometry: region.x == 60 and region.width == 90 and region.y == 40 and region.height == 120,
    region_style: region.opacity == 0.3 and tags(region, 'title')[0][0] == "range",
    stack_extent: stacked_bars[1].y == 0 and stacked_bars[0].height + stacked_bars[1].height == 200,
    stack_join: stacked_bars[1].y + stacked_bars[1].height == stacked_bars[0].y,
    rotated_margin: rotated_height > normal_height,
    horizontal_gradient: gradient[1].y == gradient[20].y and gradient[1].x < gradient[20].x,
    horizontal_format: tags(gradient, 'text')[2][0] == "1k",
    symbol_style: guide_symbol.fill == "pink" and guide_symbol.stroke == "purple" and
        guide_symbol["stroke-width"] == 2 and guide_symbol.opacity == 0.5,
    layer_diagnostic: invalid_leaf is error,
    layer_sort: tags(sorted_layer, 'circle')[0].cx > tags(sorted_layer, 'circle')[1].cx,
    mark_fill: tags(filled, 'circle')[0].fill == "purple" and tags(filled, 'circle')[0]["stroke-dasharray"] == "2,3",
    cloud_guides: len(with_class(cloud_layer, "legend size-legend")) == 0 and len(with_class(cloud_layer, "legend")) == 1,
    cloud_color: tags(with_class(cloud_layer, "wordcloud")[0], 'text')[0].fill == "#4e79a7" and
        tags(with_class(cloud_layer, "marks points")[0], 'circle')[0].fill == "#f28e2b",
    bad_color_domain: chart.render(<chart <data values: records> <mark type: "point">
        <encoding <color field: "high", dtype: "quantitative", scale: {domain: [0, 1], domain_mid: 2}>>>) is error
};
[for (label, passed in checks where passed != true) string(label)]
