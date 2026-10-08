import chart: lambda.chart.chart
import vega: lambda.chart.vega
import svg: lambda.chart.svg

fn elements(tree) => if (tree is element) [tree, for (child in content(tree)) for (item in elements(child)) item] else []
fn areas(tree) => elements(tree) |: ~.class == "marks areas"
fn paths(tree) => content(areas(tree)[0]) |> ~.d
let data = [{x: 0, g: "A", v: 1}, {x: 1, g: "A", v: 2}, {x: 2, g: "A", v: 1},
    {x: 0, g: "B", v: 3}, {x: 1, g: "B", v: 1}, {x: 2, g: "B", v: 4}]
fn spec(rows = data, mode = "wiggle", order = null, y_options = {}) => <chart width: 300, height: 200, padding: 0,
    <data values: rows> <mark type: "area", opacity: 1>
    <encoding <x field: "x", dtype: "quantitative", axis: null, scale: {nice: false}>
        <y field: "v", dtype: "quantitative", stack: mode, stack_order: order, axis: null, scale: {nice: false, *:y_options}>
        <color field: "g", dtype: "nominal", legend: null, scale: {domain: ["A", "B"], range: ["red", "blue"]}>>>
let image = chart.render(spec())
let expected = [svg.area_path([[0, 404.0 / 3.0], [150, 108], [300, 160]],
        [[0, 524.0 / 3.0], [150, 188], [300, 200]], null),
    svg.area_path([[0, 44.0 / 3.0], [150, 68], [300, 0]],
        [[0, 404.0 / 3.0], [150, 108], [300, 160]], null)]
let unsorted = chart.render(spec([data[2], data[0], data[1], data[5], data[3], data[4]]))
let centered = chart.render(spec(data, "center"))
let inverted = chart.render(spec(data, "wiggle", null, {reverse: true}))
let sparse = chart.render(spec([{x: 0, g: "A", v: 1}, {x: 2, g: "A", v: 3},
    {x: 0, g: "A", v: 2}, {x: 1, g: "B", v: 4}]))
let dense = chart.render(spec([{x: 0, g: "A", v: 3}, {x: 1, g: "A", v: 0}, {x: 2, g: "A", v: 3},
    {x: 0, g: "B", v: 0}, {x: 1, g: "B", v: 4}, {x: 2, g: "B", v: 0}]))
let layered = chart.render(<chart width: 300, height: 200, padding: 0,
    <layer spec()>>)
let inherited = chart.render(<chart *:map(spec()),
    spec()[0]; spec()[2]; <layer <chart <mark type: "area", opacity: 1>>>>)
let facet = chart.render(<chart *:map(spec()),
    spec()[0]; spec()[1]; spec()[2]; <facet field: "g", columns: 2>>)
let converted = chart.render_spec(vega.convert({width: 300, height: 200, padding: 0, data: {values: data},
    mark: {type: "area", opacity: 1}, encoding: {
        x: {field: "x", type: "quantitative", axis: null, scale: {nice: false}},
        y: {field: "v", type: "quantitative", stack: "wiggle", stackOrder: "none", axis: null, scale: {nice: false}},
        color: {field: "g", type: "nominal", legend: null, scale: {domain: ["A", "B"], range: ["red", "blue"]}}}}))
let temporal = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [for (row in data) {*:row, date: [t'2026-01-01', t'2026-01-02', t'2026-01-03'][row.x]}]>
    <mark type: "area", opacity: 1>
    <encoding <x field: "date", dtype: "temporal", axis: null, scale: {nice: false}>
        <y field: "v", dtype: "quantitative", stack: "wiggle", axis: null, scale: {nice: false}>
        <color field: "g", dtype: "nominal", legend: null, scale: {domain: ["A", "B"], range: ["red", "blue"]}>>>)
let palette = chart.render(<chart width: 300, height: 200, <data values: data> <mark type: "area">
    <encoding <x field: "x", dtype: "quantitative"> <y field: "v", dtype: "quantitative", stack: "wiggle", stack_order: "reverse">
        <color field: "g", dtype: "nominal", scale: {domain: ["A", "B"], range: ["red", "blue"]}>>>)
fn categorical(ordering, domain = null) => chart.render(<chart *:map(spec()),
    <data values: [for (row in [data[2], data[0], data[1], data[5], data[3], data[4]])
        {*:row, category: ["later", "first", "middle"][row.x]}]>
    spec()[1]; <encoding <x field: "category", dtype: "nominal", sort: ordering, axis: null,
        scale: {padding: 0, domain: domain}> spec()[2][1]; spec()[2][2]>>)
let repeated = chart.render(<repeat <column ["v", "w"]>
    <chart *:map(spec()), <data values: [for (row in data) {*:row, w: row.v * 2}]>
        spec()[1]; <encoding spec()[2][0];
            <y field: {repeat: "column"}, dtype: "quantitative", stack: "wiggle", axis: null, scale: {nice: false}>
            spec()[2][2]>>>)
let invalid = spec([{x: 0, g: "A", v: -1}])
let checks = [
    {name: "analytic streamgraph paths", ok: paths(image) == expected},
    {name: "unsorted input follows ascending positions", ok: paths(unsorted) == expected},
    {name: "wiggle differs from center", ok: paths(centered) != expected},
    {name: "native stack-order override", ok: paths(chart.render(spec(data, "wiggle", "reverse"))) != expected},
    {name: "reversed y scale", ok: paths(inverted)[0] == svg.area_path([[0, 196.0 / 3.0], [150, 92], [300, 40]],
        [[0, 76.0 / 3.0], [150, 12], [300, 0]], null)},
    {name: "sparse and duplicate samples render dense equivalents", ok: paths(sparse) == paths(dense)},
    {name: "layered stream shares stack geometry", ok: paths(layered) == expected and paths(inherited) == expected},
    {name: "facets render independently", ok: len(areas(facet)) == 2 and len(content(areas(facet)[0])) == 1},
    {name: "Vega conversion preserves native stack extension", ok: paths(converted) == expected},
    {name: "temporal streamgraph", ok: paths(temporal) == expected},
    {name: "categorical sort controls sample sequence", ok: paths(categorical(["later", "first", "middle"])) == expected},
    {name: "categorical domain takes precedence", ok: paths(categorical(["middle", "first", "later"], ["later", "first", "middle"])) == expected},
    {name: "repeat streamgraphs", ok: len(areas(repeated)) == 2 and paths(repeated) == expected and
        (content(areas(repeated)[1]) |> ~.d) == expected},
    {name: "series ordering retains palette assignments", ok: areas(palette)[0][0].fill == "red" and areas(palette)[0][1].fill == "blue"},
    {name: "negative chart values rejected", ok: chart.render(invalid) is error},
    {name: "invalid layered stream retains diagnostic", ok: chart.render(<chart <layer invalid>>) is error},
    {name: "invalid concat child retains diagnostic", ok: chart.render(<hconcat spec(); invalid>) is error},
    {name: "unknown mode rejected", ok: chart.render(spec(data, "unknown")) is error},
    {name: "unknown order rejected", ok: chart.render(spec(data, "wiggle", "random")) is error},
    {name: "wiggle bar rejected", ok: chart.render(<chart spec()[0]; <mark type: "bar"> spec()[2]>) is error},
    {name: "wiggle requires color grouping", ok: chart.render(<chart spec()[0]; spec()[1];
        <encoding spec()[2][0]; spec()[2][1]>>) is error}
];
[for (check in checks where check.ok != true) check.name]
