import radiant
import text: lambda.chart.text
import axis: lambda.chart.axis
import scale: lambda.chart.scale
import chart: lambda.chart.chart
import leg: lambda.chart.legend
import config: lambda.chart.config
import layout: lambda.chart.layout
import vega: lambda.chart.vega

fn elements(tree) => if (tree is element) [tree, for (child in content(tree)) for (item in elements(child)) item] else []
fn tags(tree, tag) => elements(tree) |: name(~) == tag
fn labelled_axis(tree) => tags(tree, 'g') |: ~.class == "tick"
fn labels(tree) => [for (tick in labelled_axis(tree)) for (label in tags(tick, 'text')) label]
fn close(a, b) => abs(a - b) < 0.001

// The repository font gives an independent, fixed advance: 1000 units at 1000 units/em.
let pinned_source = "<html><body><svg><style>@font-face{font-family:ChartMetricTest;src:url('test/ui/svg_font_assets/rectangle.ttf')}</style><text font-family='ChartMetricTest' font-size='20' xml:space='preserve'>AA</text><text font-family='ChartMetricTest' font-size='40'>AA</text><text font-family='ChartMetricTest' font-size='20'></text></svg></body></html>"
let pinned = radiant.measure_svg_text(pinned_source, 300, 100)
let font = text.style({font_family: "Arial", label_font_size: 24})
let metrics = text.measure(["iiii", "WWWW", "AV", "A V", "日本語", "é", "", "  "], font)
let larger = text.measure(["WWWW"], {*:font, font_size: 48})
let italic = text.measure(["fj"], {*:font, font_style: "italic"})
let spaced = text.measure(["A V"], {*:font, letter_spacing: 2, word_spacing: 3})
let fitted = text.fit(["iiiiiiii", "WWWWWWWW", "日本語の長いラベル"], font, 70.0)
let tiny = text.fit(["hello"], font, 0.0)
let category_scale = scale.point_scale(["iiiiiiii", "WWWWWWWW"], 0, 150, 0)
let prepared = axis.prepare(category_scale, {label_font_size: 24, label_limit: 70}, "Title")
let normal = axis.geometry(category_scale, 150, 100, prepared, true)
let rotated = axis.geometry(category_scale, 150, 100, {*:prepared, label_angle: 60}, true)
let reversed = scale.point_scale(["WWWW", "iiii", "MMMM", "nnnn", "oooo"], 90, 0, 0)
let crowded_x = axis.x_axis(reversed, 90, 90, {label_font_size: 24, label_overlap: "hide"}, null)
let crowded_y = axis.y_axis(reversed, 90, 90, {label_font_size: 24, label_overlap: "hide"}, null)
let roomy = axis.x_axis(reversed, 90, 90, {label_font_size: 24, label_overlap: false}, null)
let chosen_x = labels(crowded_x) |> join(content(~), "")
let chosen_y = labels(crowded_y) |> join(content(~), "")
let x_boxes = sort(axis.geometry(reversed, 90, 90, axis.prepare(reversed,
    {label_font_size: 24, label_overlap: "hide"}), true).rows |: ~.text in chosen_x, (row) => row.position)
let y_boxes = sort(axis.geometry(reversed, 90, 90, axis.prepare(reversed,
    {label_font_size: 24, label_overlap: "hide"}), false).rows |: ~.text in chosen_y, (row) => row.position)
let custom = chart.render(<chart width: 500, height: 300, padding: 0, title: "Measured title",
    <config font: "serif", title_font_size: 40>
    <data values: [{category: "Wide category label", value: 10}, {category: "Narrow", value: 20}]>
    <mark type: "bar">
    <encoding <x field: "category", dtype: "nominal", axis: {label_font_family: "monospace", label_font_size: 22,
        label_font_weight: 700, label_angle: 45, title_font_size: 26}>
        <y field: "value", dtype: "quantitative">
        <color field: "category", dtype: "nominal", legend: {label_font_size: 24, title_font_size: 30}>>
>)
let custom_labels = labels(custom)
let custom_title = tags(custom, 'text') |: content(~) == ["Measured title"]
let color_scale = scale.ordinal_scale(["iiii", "WWWW"], ["red", "blue"])
let legend_narrow = leg.plans({color: {field: "category", dtype: "nominal", legend: {values: ["iiii"]}}},
    {color_scale: color_scale}, config.light_theme)[0]
let legend_wide = leg.plans({color: {field: "category", dtype: "nominal", legend: {values: ["WWWW"]}}},
    {color_scale: color_scale}, config.light_theme)[0]
let huge_legend = leg.plans({color: {field: "category", dtype: "nominal", legend: {label_font_size: 80}}},
    {color_scale: color_scale}, config.light_theme)[0]
let converted = vega.convert({mark: "point", encoding: {x: {field: "x", type: "quantitative",
    axis: {labelFont: "serif", labelFontWeight: 700, labelFontStyle: "italic", labelSeparation: 8}}}})
let top = axis.x_axis(category_scale, 150, 100, {orient: "top", label_angle: -45, label_font_size: 24}, "top")
let top_geometry = axis.geometry(category_scale, 150, 100, axis.prepare(category_scale,
    {orient: "top", label_angle: -45, label_font_size: 24}, "top"), true)
let upright_titles = [for (orient in ["left", "right"], letter in ["y", "θ", "é"]) (
    let options = axis.prepare(reversed, {orient: orient, title: letter}, "Value"),
    let geo = axis.geometry(reversed, 90, 90, options, false),
    let rendered = axis.y_axis(reversed, 90, 90, options, "Value"),
    let title = (tags(rendered, 'text') |: content(~) == [letter])[0],
    {name: "upright title " ++ orient ++ " " ++ letter, ok: geo.title_angle == 0 and title.transform == null and
        close(geo.title_bounds.right - geo.title_bounds.left, text.span(options._title_metric)) and
        (if (orient == "left") geo.title_bounds.right <= min(geo.rows |> ~.bounds.left) - options.title_padding
            else geo.title_bounds.left >= max(geo.rows |> ~.bounds.right) + options.title_padding)})]
let title_layout = layout.compute_layout({width: 300, height: 200, padding: {top: 0, right: 0, bottom: 0, left: 0},
    encoding: {}, config: {title_font_size: 60}, title: "Large"}, null, null, false, null)
let checks = [
    {name: "pinned font advance", ok: close(pinned[0].width, 40.0)},
    {name: "pinned baseline", ok: close(pinned[0].baseline, 16.0) and close(pinned[0].bottom, 4.0)},
    {name: "pinned scaling", ok: close(pinned[1].width, 80.0)},
    {name: "empty text", ok: pinned[2].width == 0 and pinned[2].height == 0},
    {name: "narrow and wide characters", ok: metrics[0].width < metrics[1].width},
    {name: "spaces affect advance", ok: metrics[2].width < metrics[3].width},
    {name: "authored spacing affects advance", ok: close(spaced[0].width, metrics[3].width + 9.0)},
    {name: "unicode fallback", ok: metrics[4].width > 0 and metrics[4].height > 0},
    {name: "combining text", ok: metrics[5].width > 0},
    {name: "empty metrics", ok: metrics[6].width == 0},
    {name: "preserved spaces", ok: metrics[7].width > 0},
    {name: "logical scaling", ok: close(larger[0].width, metrics[1].width * 2.0)},
    {name: "italic bounds contain advance", ok: italic[0].left <= 0 and italic[0].right >= italic[0].width},
    {name: "pixel limits", ok: len([for (label in fitted where text.span(label.metric) > 70.001) label]) == 0},
    {name: "different fitted prefixes", ok: len(fitted[0].text) > len(fitted[1].text)},
    {name: "unicode truncation", ok: ends_with(fitted[2].text, "…") and not contains(fitted[2].text, "�")},
    {name: "no room for ellipsis", ok: tiny[0].text == "" and tiny[0].metric.width == 0},
    {name: "prepared reuse", ok: axis.prepare(category_scale, prepared, "Title") == prepared},
    {name: "rotated extent", ok: rotated.extent > normal.extent},
    {name: "horizontal collisions", ok: len(labels(crowded_x)) < len(labels(roomy)) and len(labels(crowded_x)) > 0},
    {name: "vertical collisions", ok: len(labels(crowded_y)) < len(labels(roomy)) and len(labels(crowded_y)) > 0},
    {name: "horizontal selected bounds are separate", ok: len([for (index, row in x_boxes
        where index > 0 and row.bounds.left < x_boxes[index - 1].bounds.right + 1.999) row]) == 0},
    {name: "vertical selected bounds are separate", ok: len([for (index, row in y_boxes
        where index > 0 and row.bounds.top < y_boxes[index - 1].bounds.bottom + 1.999) row]) == 0},
    {name: "emitted guide font", ok: len(custom_labels |: ~["font-family"] == "monospace" and ~["font-weight"] == 700) == 2},
    {name: "emitted title font", ok: custom_title[0]["font-size"] == 40 and custom_title[0]["font-family"] == "serif"},
    {name: "measured legend widths", ok: legend_wide.width > legend_narrow.width},
    {name: "measured legend rows", ok: huge_legend.config._geometry.row_height > 80},
    {name: "vega font conversion", ok: converted.encoding.x.axis.label_font_family == "serif" and
        converted.encoding.x.axis.label_font_weight == 700 and converted.encoding.x.axis.label_separation == 8},
    {name: "top labels outside plot", ok: max(top_geometry.rows |> ~.bounds.bottom) < 0},
    *upright_titles,
    {name: "long vertical title retained", ok: axis.geometry(reversed, 90, 90,
        axis.prepare(reversed, {}, "Value"), false).title_angle == -90},
    {name: "title reserves measured height", ok: title_layout.top_margin > 60 and title_layout.title_y > 0},
    {name: "invalid native viewport", ok: radiant.measure_svg_text(pinned_source, 0, 100) == null},
    {name: "invalid native source", ok: radiant.measure_svg_text("<html><body>text</body></html>", 10, 10) == null},
    {name: "native requests skip body siblings", ok: len(radiant.measure_svg_text(
        "<html><body><p>ignored</p><svg><text>A</text></svg></body></html>", 10, 10)) == 1},
    {name: "empty native batch", ok: radiant.measure_svg_text("<html><body><svg/></body></html>", 10, 10) == []}
];
[for (check in checks where check.ok != true) check.name]
