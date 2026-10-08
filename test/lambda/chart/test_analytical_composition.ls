import chart: lambda.chart.chart
import vega: lambda.chart.vega
import tr: lambda.chart.transform
import cfg: lambda.chart.config

let records = [{a: 0, b: 10, y: 1}, {a: 1, b: 20, y: 3}, {a: 2, b: 30, y: 5}]
let encoding = {x: {field: "a", type: "quantitative"}, y: {field: "y", type: "quantitative"}}
let layered = chart.render_spec(vega.convert({width: 300, height: 200, padding: 0,
    data: {values: records}, encoding: encoding,
    layer: [{mark: "point"}, {mark: "line", transform: [{regression: "y", on: "a"}]}]}))
let native = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: records>
    <encoding <x field: "a", dtype: "quantitative"> <y field: "y", dtype: "quantitative">>
    <config point: {size: 100, color: "red"}>
    <layer <chart <mark type: "point">>
        <chart <transform <calculate as: "twice", expression: (row) => row.y * 2>>
            <encoding <y field: "twice", dtype: "quantitative">>
            <config point: {color: "blue"}>
            <layer <chart <mark type: "point">> <chart <mark type: "line">>>>>>)
let concat_spec = vega.convert({data: {values: records},
    transform: [{window: [{op: "sum", field: "y", as: "total"}], ignorePeers: true}],
    hconcat: [{width: 200, height: 150, mark: "point", encoding: {
        x: {field: "a", type: "quantitative"}, y: {field: "total", type: "quantitative"}}},
        {width: 200, height: 150, mark: "line", encoding: encoding,
            transform: [{filter: {field: "total", gt: 1}}]}]})
let concat = chart.render_spec(concat_spec)
let concat_parent_rows = tr.apply_transforms(records, concat_spec.transform)
let facet_spec = vega.convert({width: 200, height: 150, data: {values: records},
    transform: [{calculate: (row) => if (row.a < 2) "low" else "high", as: "group"}],
    facet: {field: "group"}, columns: 2, resolve: {scale: {y: "independent"}},
    spec: {mark: "point", encoding: encoding}})
let facet = chart.render_spec(facet_spec)
let repeat_spec = vega.convert({repeat: {column: ["a", "b"]}, width: 200, height: 150, padding: 0,
    data: {values: records}, transform: [{filter: {field: "y", gt: 1}}],
    spec: {encoding: {x: {field: {repeat: "column"}, type: "quantitative"},
        y: {field: "y", type: "quantitative"}},
        layer: [{mark: "point"}, {layer: [{mark: "line"}]}]}})
let repeated = chart.render_spec(repeat_spec)
let first_repeat = repeated[1][0]
let second_repeat = repeated[2][0]
let inherited = cfg.inherit({axis: {grid: true, label_font_size: 13}}, {axis: {grid: false}})
let repeated_colors = chart.render_spec(vega.convert({repeat: ["a", "b"], data: {values: records},
    transform: [{calculate: (row) => if (row.y > 2) "high" else "low", as: "category"}],
    spec: {width: 200, height: 150, mark: "point", encoding: {
        x: {field: {repeat: "column"}, type: "quantitative"}, y: {field: "y", type: "quantitative"},
        color: {field: "category", type: "nominal"}}}}))
let checks = {
    trend_layers: len(content(layered[1][0])) == 2,
    scatter_count: len(content(layered[1][0][0])) == 3,
    trend_count: len(content(layered[1][0][1])) == 1,
    trend_position: contains(layered[1][0][1][0].d, string(layered[1][0][0][0].cx)),
    native_parent_encoding: native[1][0][0][0].cx < native[1][0][0][2].cx,
    nested_layer_count: len(content(native[1][0])) == 3,
    nested_dataflow: native[1][0][1][2].cy < native[1][0][0][2].cy,
    nested_config_color: native[1][0][1][0].fill == "blue" and native[1][0][0][0].fill == "red",
    nested_config_size: native[1][0][1][0].r == native[1][0][0][0].r,
    concat_transform_order: len(content(concat_spec.transform)) == 1 and len(content(concat_spec.children[1].transform)) == 1,
    concat_transform_values: concat_parent_rows[2].total == 9,
    concat_child_filter: len(tr.apply_transforms(concat_parent_rows, concat_spec.children[1].transform)) == 2,
    concat_svg: concat.width == 420 and len(content(concat[1][0][1][0])) == 3,
    facet_inherited_transform: len(content(facet)) == 3,
    facet_resolution: facet_spec.resolve.scale.y == "independent",
    repeat_inherited_transform: len(content(first_repeat[1][0][0])) == 2,
    repeat_nested_layers: len(content(first_repeat[1][0])) == 2 and len(content(second_repeat[1][0])) == 2,
    repeat_field_substitution: first_repeat[1][1][len(content(first_repeat[1][1])) - 1][0] == "a" and
        second_repeat[1][1][len(content(second_repeat[1][1])) - 1][0] == "b",
    repeat_nested_path: starts_with(second_repeat[1][0][1][0].d, "M"),
    repeat_derived_colors: repeated_colors[1][0][1][0][0].fill != repeated_colors[1][0][1][0][1].fill,
    repeat_color_consistency: repeated_colors[1][0][1][0][1].fill == repeated_colors[2][0][1][0][1].fill,
    recursive_config: inherited.axis == {grid: false, label_font_size: 13}
};
[for (label, passed in checks where passed != true) string(label)]
