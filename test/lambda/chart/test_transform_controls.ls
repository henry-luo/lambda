import tr: lambda.chart.transform
import chart: lambda.chart.chart
import vega: lambda.chart.vega

let flat = tr.apply_transforms([{id: "A", x: [1, 2], y: [3]}, {id: "B", x: [], y: [4]}],
    <transform <flatten fields: ["x", "y"], as: ["a", "b"]>>)
let filtered = tr.apply_transforms([{x: null}, {x: 0}, {x: 1}, {x: 2}],
    <transform <filter test: {'and': [{field: "x", valid: true}, {field: "x", oneOf: [0, 2]}]}>>)
let grouped = tr.apply_transforms([{a: "x|||y", b: "z", v: 1}, {a: "x", b: "y|||z", v: 2}],
    <transform <aggregate <group field: "a"> <group field: "b"> <agg op: "sum", field: "v", as: "total">>>)
let aggregate = tr.prepare_encoding([{cat: "A", v: 1}, {cat: "A", v: 3}, {cat: "B", v: 9}],
    {x: {field: "cat", dtype: "nominal"}, y: {field: "v", dtype: "quantitative", aggregate: "mean"}})
let binned = tr.prepare_encoding([{v: 7}, {v: 7}],
    {x: {field: "v", dtype: "quantitative", bin: {step: 5}}, y: {aggregate: "count"}})
let native = chart.render(<chart width: 300, height: 200,
    <data values: [{cat: "A", v: 1}, {cat: "A", v: 3}, {cat: "B", v: 9}]> <mark type: "bar">
    <encoding <x field: "cat", dtype: "nominal"> <y field: "v", dtype: "quantitative", aggregate: "sum">>
>)
let converted = vega.convert({width: 300, height: 200,
    data: {values: [{cat: "A", v: 1}, {cat: "A", v: 3}, {cat: "B", v: 9}]},
    transform: [{filter: {field: "v", gte: 1}}, {calculate: (row) => row.v * 2, as: "twice"},
        {aggregate: [{op: "sum", field: "twice", as: "v"}], groupby: ["cat"]}],
    mark: "bar", encoding: {x: {field: "cat", type: "nominal"}, y: {field: "v", type: "quantitative"}}})
let transformed = chart.render_spec(converted)
let named = chart.render(<chart width: 200, height: 150, padding: 0, datasets: {sample: [{text: "Lambda", weight: 1}]},
    <data name: "sample"> <mark type: "wordcloud", min_font_size: 12, max_font_size: 12>>)
let invalid = chart.render_spec(vega.convert({data: {values: [{x: 1}]}, mark: "point",
    transform: [{filter: "datum.x >"}]}));
[
    len(flat) == 3, flat[0].a == 1, flat[0].b == 3, flat[1].a == 2, flat[1].b == null,
    flat[2].a == null, flat[2].b == 4, flat[2].id == "B",
    len(filtered) == 2, filtered[0].x == 0, filtered[1].x == 2,
    len(grouped) == 2, grouped[0].total == 1, grouped[1].total == 2,
    aggregate.data[0].v_mean == 2, aggregate.data[1].v_mean == 9, aggregate.encoding.y.field == "v_mean",
    len(binned.data) == 1, binned.data[0].v_bin == 5, binned.data[0]._count == 2,
    len(content(native[1][0])) == 2, len(content(transformed[1][0])) == 2,
    named["data-unplaced"] == 0, len(content(named[1][0][0])) == 1,
    invalid is error,
    tr.apply_transforms([{x: 1}], <transform <bin field: "x", step: 0>>) is error,
    tr.apply_transforms([], <transform <unknown>>) is error
]
