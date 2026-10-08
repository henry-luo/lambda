import chart: lambda.chart.chart
import vega: lambda.chart.vega

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn tags(node, tag) => elements(node) |: name(~) == tag
fn classes(node, class_name) => elements(node) |: ~.class == class_name
let data = [{x: 0, low: 0, high: 0, group: "A"}, {x: 10, low: 10, high: 100, group: "B"}];
let base = {width: 200, height: 100, padding: 0, mark: {kind: "point"}, data: data, encoding: {
    x: {field: "x", dtype: "quantitative", axis: null, scale: {nice: false}},
    y: {field: "low", dtype: "quantitative", axis: null, scale: {nice: false}}}};
let high = {*:base, encoding: {*:base.encoding, y: {*:base.encoding.y, field: "high"}}};
let shared = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}}, children: [base, high]});
let independent = chart.render_spec({concat: "horizontal", children: [base, high]});
let nested = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}}, children: [
    {concat: "vertical", children: [base, base]}, high]});
let overridden = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}}, children: [
    {concat: "vertical", resolve: {scale: {y: "independent"}}, children: [base, high]}, high]});
let nested_layers = chart.render_spec({width: 200, height: 100, padding: 0, data: data, layer: [
    {mark: {kind: "point"}, encoding: base.encoding},
    {resolve: {scale: {y: "independent"}}, layer: [
        {mark: {kind: "point"}, encoding: base.encoding}, {mark: {kind: "point"}, encoding: high.encoding}]}]});
let guided = {*:base, height: 200, encoding: {*:base.encoding, y: {*:base.encoding.y, axis: {title: null}}}};
let guided_high = {*:guided, encoding: {*:guided.encoding, y: {*:guided.encoding.y, field: "high"}}};
let shared_axes = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}, axis: {y: "shared"}}, children: [guided, guided_high]});
let independent_axes = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}, axis: {y: "independent"}}, children: [guided, guided_high]});
let forced_axes = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "independent"}, axis: {y: "shared"}}, children: [guided, guided_high]});
let colors_a = {*:base, data: [{x: 1, low: 1, group: "A"}], encoding: {*:base.encoding, color: {field: "group", dtype: "nominal"}}};
let colors_b = {*:colors_a, data: [{x: 2, low: 2, group: "B"}]};
let shared_colors = chart.render_spec({concat: "horizontal", resolve: {scale: {color: "shared"}, legend: {color: "shared"}}, children: [colors_a, colors_b]});
let independent_colors = chart.render_spec({concat: "horizontal", resolve: {scale: {color: "independent"}}, children: [colors_a, colors_b]});
let independent_legends = chart.render_spec({concat: "horizontal", resolve: {scale: {color: "shared"}, legend: {color: "independent"}}, children: [colors_a, colors_b]});
let disabled_first = chart.render_spec({concat: "horizontal", resolve: {scale: {color: "shared"}, legend: {color: "shared"}}, children: [
    {*:colors_a, encoding: {*:colors_a.encoding, color: {*:colors_a.encoding.color, legend: null}}}, colors_b]});
let no_scale = chart.render_spec({concat: "horizontal", resolve: {scale: {color: "shared"}}, children: [
    {*:colors_a, encoding: {*:colors_a.encoding, color: {field: "group", dtype: "nominal", scale: null, legend: null}}}, colors_b]});
let faceted_layers = chart.render_spec({width: 200, height: 100, padding: 0, data: [
    {group: "A", x: 0, low: 0}, {group: "A", x: 10, low: 10}, {group: "B", x: 0, low: 0}, {group: "B", x: 10, low: 100}],
    facet: {field: "group", columns: 2}, layer: [{mark: {kind: "point"}, encoding: base.encoding}]});
let repeat_shared = chart.render_spec({repeat_column: ["low", "high"], resolve: {scale: {y: "shared"}}, data: data,
    template: {*:base, data: null, encoding: {*:base.encoding, y: {*:base.encoding.y, field: {repeat: "column"}}}}});
let repeat_independent = chart.render_spec({repeat_column: ["low", "high"], data: data,
    template: {*:base, data: null, encoding: {*:base.encoding, y: {*:base.encoding.y, field: {repeat: "column"}}}}});
let inherited = chart.render_spec({concat: "horizontal", data: data, encoding: base.encoding, config: {point: {color: "red", size: 100}},
    transform: [{type: "calculate", as: "low", expression: "datum.low + 1"}], children: [
        {concat: "vertical", transform: [{type: "filter", test: "datum.low > 1"}], children: [
            {width: 200, height: 100, padding: 0, mark: {kind: "point"}, encoding: {y: {*:base.encoding.y, scale: {domain: [0, 20], nice: false}}}},
            {width: 200, height: 100, padding: 0, mark: {kind: "point"}, config: {point: {color: "blue"}}}]}]});
let source_override = chart.render_spec({concat: "horizontal", data: data, datasets: {other: [{x: 1, low: 2}]}, children: [
    {*:base, data: null, data_source: {name: "other"}}, {*:base, data: []}]});
let reversed = chart.render_spec({*:colors_a, encoding: {*:colors_a.encoding, color: {*:colors_a.encoding.color,
    scale: {domain: ["A", "B"], range: ["red", "blue"], reverse: true}}}});
let continuous_reverse = chart.render_spec({*:base, encoding: {*:base.encoding, color: {field: "low", dtype: "quantitative",
    scale: {domain: [0, 10], range: ["red", "blue"], reverse: true}, legend: null}}});
let bad = chart.render_spec({concat: "horizontal", resolve: {scale: {y: "shared"}}, children: [base,
    {*:base, encoding: {*:base.encoding, y: {field: "group", dtype: "nominal"}}}]});
let vg = chart.render_spec(vega.convert({data: {values: data}, transform: [{calculate: "datum.low + 1", as: "low"}],
    config: {point: {color: "red"}}, hconcat: [{width: 200, height: 100, padding: 0,
        transform: [{filter: "datum.low > 1"}], mark: "point", encoding: {x: {field: "x", type: "quantitative", axis: null}}}]}));
let checks = {
    shared_concat: tags(shared, 'circle')[1].cy == 90 and tags(shared, 'circle')[3].cy == 0,
    independent_concat: tags(independent, 'circle')[1].cy == 0 and tags(independent, 'circle')[3].cy == 0,
    nested_shared: tags(nested, 'circle')[1].cy == 90 and tags(nested, 'circle')[3].cy == 90,
    nested_override: tags(overridden, 'circle')[1].cy == 0 and tags(overridden, 'circle')[3].cy == 0,
    nested_layer_override: tags(nested_layers, 'circle')[1].cy == 0 and tags(nested_layers, 'circle')[3].cy == 0 and tags(nested_layers, 'circle')[5].cy == 0,
    one_axis: len(classes(shared_axes, "axis y-axis")) == 1,
    separate_axes: len(classes(independent_axes, "axis y-axis")) == 2,
    scale_forces_guides: len(classes(forced_axes, "axis y-axis")) == 2,
    shared_color_domain: tags(shared_colors, 'circle')[0].fill == "#4e79a7" and tags(shared_colors, 'circle')[1].fill == "#f28e2b",
    shared_legend: len(classes(shared_colors, "legend")) == 1 and len(classes(shared_colors, "legend-entry")) == 2,
    independent_color: tags(independent_colors, 'circle')[0].fill == tags(independent_colors, 'circle')[1].fill,
    separate_legends: len(classes(independent_legends, "legend")) == 2,
    enabled_representative: len(classes(disabled_first, "legend")) == 1,
    identity_excluded: tags(no_scale, 'circle')[1].fill == "#4e79a7",
    facet_layer_domain: tags(faceted_layers, 'circle')[1].cy == 90 and tags(faceted_layers, 'circle')[3].cy == 0,
    repeat_shared: tags(repeat_shared, 'circle')[1].cy == 90 and tags(repeat_shared, 'circle')[3].cy == 0,
    repeat_independent: tags(repeat_independent, 'circle')[1].cy == 0 and tags(repeat_independent, 'circle')[3].cy == 0,
    inherited_filter: len(tags(inherited, 'circle')) == 2,
    inherited_transform_once: abs(tags(inherited, 'circle')[0].cy - 45) < 0.00000001,
    inherited_config: tags(inherited, 'circle')[0].fill == "red" and tags(inherited, 'circle')[1].fill == "blue",
    inherited_size: tags(inherited, 'circle')[0].r == tags(inherited, 'circle')[1].r,
    override_data: len(tags(source_override, 'circle')) == 1,
    ordinal_reverse: tags(reversed, 'circle')[0].fill == "blue",
    continuous_reverse: tags(continuous_reverse, 'circle')[0].fill == "blue" and tags(continuous_reverse, 'circle')[1].fill == "red",
    incompatible_types: bad is error,
    invalid_resolution: chart.render_spec({concat: "horizontal", resolve: {scale: {y: "bad"}}, children: [base]}) is error,
    invalid_spec: chart.render_spec(null) is error,
    empty_facets: chart.render_spec({*:base, data: [], facet: {row: "group"}}).height >= 0,
    invalid_spacing: chart.render_spec({*:base, facet: {field: "group", spacing: -1}}) is error,
    inherited_vega: len(tags(vg, 'circle')) == 1 and tags(vg, 'circle')[0].fill == "red"
};
[for (label, passed in checks where passed != true) string(label)]
