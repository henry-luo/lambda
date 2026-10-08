import chart: lambda.chart.chart
import transform: lambda.chart.transform
import vega: lambda.chart.vega
import parse: lambda.chart.parse

fn has_fill(node, fill) {
    if (not (node is element)) false
    else node.fill == fill or len([for (child in content(node) where has_fill(child, fill)) true]) > 0
}
let data = [{label: "a", value: 2}, {label: "b", value: 4}, {label: "c", value: 6}];
let vl = {data: {values: data}, mark: "point", transform: [
    {filter: "datum.value >= 4"}, {calculate: "datum.value * 2", as: "double"}], encoding: {
        x: {field: "label", type: "nominal"}, y: {field: "double", type: "quantitative"},
        color: {condition: {test: "datum.double > 10", value: "red"}, value: "blue"}}};
let converted = vega.convert(vl);
let transformed = transform.apply_transforms(data, converted.transform);
let image = chart.render_spec(converted);
let native = chart.render(<chart <data values: data> <mark type: "point">
    <transform <filter test: "datum.value >= 4"> <calculate as: "double", expression: "datum.value * 2">>
    <encoding <x field: "label", dtype: "nominal"> <y field: "double", dtype: "quantitative">
        <color condition: [{test: "datum.double > 10", value: "red"}, {test: "true", value: "blue"}], value: "black">>>);
let layered = chart.render_spec(vega.convert({*:vl, layer: [{mark: "point"}, {mark: "text", encoding: {
    text: {field: "label"}, color: {condition: {test: "datum.double > 10", value: "green"}, value: "blue"}}}]}));
let checks = {
    transforms: transformed == [{label: "b", value: 4, double: 8.0}, {label: "c", value: 6, double: 12.0}],
    rendering: image is element and has_fill(image, "red") and has_fill(image, "blue"),
    markup: native is element and has_fill(native, "red") and has_fill(native, "blue"),
    layer: layered is element and has_fill(layered, "green"),
    first_condition: parse.channel_value({condition: [{test: "true", value: 1}, {test: "datum.missing.x", value: 2}]}, {}) == 1,
    logical: parse.test_predicate({'and': ["datum.value > 1", {'not': "datum.value > 10"}]}, data[0]) == true,
    callback: parse.test_predicate((row) => row.value == 2, data[0]) == true,
    bad_filter: transform.apply_transforms([], [{type: "filter", test: "datum."}]) is error,
    bad_calculate: transform.apply_transforms([], [{type: "calculate", as: "x", expression: "unknown(1)"}]) is error,
    runtime_filter: transform.apply_transforms(data, [{type: "filter", test: "datum.missing.value"}]) is error,
    runtime_calculate: transform.apply_transforms(data, [{type: "calculate", as: "x", expression: "datum.missing.value"}]) is error,
    condition_parse: chart.render_spec(vega.convert({*:vl, encoding: {color: {condition: {test: "datum.", value: "red"}}}})) is error,
    condition_runtime: chart.render_spec(vega.convert({*:vl, encoding: {color: {condition: {test: "datum.missing.x", value: "red"}}}})) is error,
    condition_empty: chart.render_spec(vega.convert({*:vl, data: {values: []}, encoding: {color: {condition: {test: "datum.", value: "red"}}}})) is error,
    unused_branch: chart.render_spec(vega.convert({*:vl, data: {values: []}, encoding: {color: {condition: [
        {test: "true", value: "red"}, {test: "datum.", value: "blue"}]}}})) is error,
    short_circuit: parse.test_predicate({'or': [true, "datum.missing.value"]}, {}) == true,
    scaled_datum_color: has_fill(chart.render_spec({width: 100, height: 100, padding: 0, data: [{}], mark: {kind: "point"},
        encoding: {x: {value: 20}, y: {value: 20}, color: {datum: "blue", dtype: "nominal", legend: null}}}), "#4e79a7"),
    literal_value_color: has_fill(chart.render_spec({width: 100, height: 100, padding: 0, data: [{}], mark: {kind: "point"},
        encoding: {x: {value: 20}, y: {value: 20}, color: {value: "blue"}}}), "blue"),
    shared_datum_domain: has_fill(chart.render_spec({width: 100, height: 100, padding: 0, data: [{}], layer: [
        {mark: {kind: "point"}, encoding: {x: {value: 20}, y: {value: 20}, color: {datum: "one", dtype: "nominal", legend: null}}},
        {mark: {kind: "point"}, encoding: {x: {value: 30}, y: {value: 30}, color: {datum: "two", dtype: "nominal", legend: null}}}]}), "#f28e2b")
};
[for (label, passed in checks where passed != true) string(label)]
