import chart: lambda.chart.chart
import vega: lambda.chart.vega

fn descendants(node, tag) {
    if (not (node is element)) [] else [
        for (item in [node] where name(item) == tag) item,
        for (child in content(node)) for (item in descendants(child, tag)) item]
}
let points = {padding: 0, mark: {kind: "point"}, data: [{x: 0, y: 0}, {x: 10, y: 10}], encoding: {
    x: {field: "x", dtype: "quantitative", axis: null}, y: {field: "y", dtype: "quantitative", axis: null}}};
let responsive = {*:points, width: "container", height: "container"};
let small = chart.render_spec(responsive, {width: 400, height: 200});
let large = chart.render_spec(responsive, {width: 800, height: 400});
let ratio = chart.render_spec({*:points, width: "container", aspect_ratio: 2}, {width: 600, height: 999});
let reverse_ratio = chart.render_spec({*:points, width: "auto", height: 200, aspect_ratio: 2});
let categorical = {*:points, data: [{x: "a", y: 1}, {x: "b", y: 2}, {x: "c", y: 3}],
    encoding: {*:points.encoding, x: {field: "x", dtype: "nominal", axis: null}}};
let automatic = chart.render_spec({*:categorical, width: "auto", height: 100});
let stepped = chart.render_spec({*:categorical, width: {step: 30}, height: 100});
let guided = chart.render_spec({*:categorical, width: {step: 30}, height: 200, encoding: {
    x: {field: "x", dtype: "nominal"}, y: {field: "y", dtype: "quantitative"}}});
let transformed = chart.render_spec({*:categorical, width: "auto", height: 100,
    transform: [{type: "filter", test: "datum.y > 1"}]});
let domain = chart.render_spec({*:categorical, width: {step: 30}, height: 100, encoding: {
    *:categorical.encoding, x: {*:categorical.encoding.x, scale: {domain: ["a", "b", "c", "d"]}}}});
let layered = chart.render_spec({padding: 0, width: {step: 30}, height: 100, data: categorical.data,
    layer: [{mark: {kind: "point"}, encoding: categorical.encoding}, {mark: {kind: "line"}, encoding: categorical.encoding}]});
let horizontal = chart.render_spec({concat: "horizontal", spacing: 10, children: [responsive, responsive]}, {width: 600, height: 200});
let mixed = chart.render_spec({concat: "horizontal", spacing: 10, children: [{*:points, width: 100, height: 200}, responsive]}, {width: 600, height: 200});
let vertical = chart.render_spec({concat: "vertical", spacing: 10, children: [responsive, responsive]}, {width: 300, height: 410});
let nested = chart.render_spec({concat: "horizontal", spacing: 10, children: [
    {concat: "horizontal", spacing: 10, children: [responsive, responsive]}, responsive]}, {width: 600, height: 200});
let repeated = chart.render_spec({repeat_column: ["x", "y"], template: {*:points, encoding: {
    *:points.encoding, x: {*:points.encoding.x, field: {repeat: "column"}}}}}, {width: 500, height: 200});
let markup = chart.render(<hconcat width: "container", height: "container", spacing: 10,
    <chart padding: 0, <data values: points.data> <mark type: "point"> <encoding <x field: "x", dtype: "quantitative", axis: null>>>
    <chart padding: 0, <data values: points.data> <mark type: "point"> <encoding <x field: "x", dtype: "quantitative", axis: null>>>>,
    {width: 500, height: 200});
let vl = chart.render_spec(vega.convert({width: "container", height: "auto", aspectRatio: 2, padding: 0,
    data: {values: points.data}, mark: "point", encoding: {x: {field: "x", type: "quantitative", axis: null}}}), {width: 500});
let cloud = chart.render_spec({width: "container", height: "container", padding: 0, mark: {kind: "wordcloud", min_font_size: 12, max_font_size: 12},
    data: [{text: "Lambda", weight: 1}]}, {width: 300, height: 100});
let faceted = chart.render_spec({*:categorical, width: {step: 30}, height: 100, facet: {field: "y", columns: 3}});
let facet_layers = chart.render_spec({padding: 0, width: "container", height: "container", data: categorical.data,
    facet: {field: "y", columns: 3}, layer: [{mark: {kind: "point"}, encoding: categorical.encoding}]}, {width: 120, height: 100});
let checks = {
    size_small: small.width == 400 and small.height == 200,
    size_large: large.width == 800 and large.height == 400,
    marks_reflow: descendants(small, 'circle')[1].cx == 400 and descendants(large, 'circle')[1].cx == 800,
    vertical_reflow: descendants(small, 'circle')[0].cy == 200 and descendants(large, 'circle')[0].cy == 400,
    viewbox: large.viewBox == "0 0 800 400",
    ratio: ratio.width == 600 and ratio.height == 300,
    ratio_reverse: reverse_ratio.width == 400 and reverse_ratio.height == 200,
    implicit_context: chart.render_spec(points, {width: 500, height: 250}).width == 500,
    fixed_precedence: chart.render_spec({*:points, width: 250, height: 100}, {width: 500, height: 200}).width == 250,
    automatic: automatic.width == 60 and automatic.height == 100,
    step: stepped.width == 90,
    guided_step: guided is element and guided.width > 90,
    transform_cardinality: transformed.width == 40,
    explicit_domain: domain.width == 120,
    layer_step: layered is element and layered.width == 90,
    concat: horizontal.width == 600 and horizontal.height == 200 and descendants(horizontal, 'svg')[1].width == 295,
    fixed_child: descendants(mixed, 'svg')[1].width == 100 and descendants(mixed, 'svg')[2].width == 490,
    vertical_concat: vertical.height == 410 and descendants(vertical, 'svg')[1].height == 200,
    nested: nested.width == 600 and descendants(nested, 'svg')[2].width == 142.5,
    repeat: repeated is element and repeated.width == 500 and repeated.height == 200,
    markup: markup is element and markup.width == 500 and descendants(markup, 'svg')[1].width == 245,
    vega: vl is element and vl.width == 500 and vl.height == 250,
    wordcloud: cloud is element and cloud.width == 300 and cloud.height == 100,
    facet_steps: faceted is element and descendants(faceted, 'svg')[1].width == 90,
    facet_layers: facet_layers is element and descendants(facet_layers, 'svg')[1].width == 120,
    tiny: chart.render_spec(responsive, {width: 30, height: 20}).width == 30,
    server_fallback: chart.render_spec(responsive).width == 400,
    bad_width: chart.render_spec({*:points, width: -1}) is error,
    bad_context: chart.render_spec(points, {width: 0}) is error,
    bad_ratio: chart.render_spec({*:points, aspect_ratio: 0}) is error,
    bad_step: chart.render_spec({*:points, width: {step: 30}}) is error,
    bad_space: chart.render_spec({concat: "horizontal", spacing: 20, children: [responsive, responsive]}, {width: 10}) is error,
    fixed_overflow: chart.render_spec({concat: "horizontal", children: [{*:points, width: 800}]}, {width: 100}) is error
};
[for (label, passed in checks where passed != true) string(label)]
