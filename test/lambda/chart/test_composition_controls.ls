import chart: lambda.chart.chart
import vega: lambda.chart.vega

let layered = chart.render(<chart width: 300, height: 200, padding: 0,
    <layer
        <chart <data values: [{x: 0, y: 0}, {x: 10, y: 10}]> <mark type: "point">
            <encoding <x field: "x", dtype: "quantitative"> <y field: "y", dtype: "quantitative">>>
        <chart <data values: [{other_x: 100, low: -10, high: 20}]> <mark type: "rule">
            <encoding <x field: "other_x", dtype: "quantitative"> <y field: "low", dtype: "quantitative"> <y2 field: "high">>>
    >
>)
let marks = layered[1][0]
let grid = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{region: "A", season: "Summer", x: 0, y: 0}, {region: "B", season: "Summer", x: 10, y: 20},
        {region: "A", season: "Winter", x: 5, y: 10}, {region: "B", season: "Winter", x: 7, y: 14}]>
    <mark type: "point">
    <encoding <x field: "x", dtype: "quantitative"> <y field: "y", dtype: "quantitative">>
    <facet row: {field: "region"}, column: {field: "season"}, spacing: 0>
>)
let vl_grid = chart.render_spec(vega.convert({width: 200, height: 150, padding: 0, spacing: 0,
    data: {values: [{region: "A", x: 0, y: 0}, {region: "B", x: 10, y: 20}]},
    facet: {field: "region"}, columns: 2,
    spec: {mark: "point", encoding: {x: {field: "x", type: "quantitative"}, y: {field: "y", type: "quantitative"}}}}))
let bad = <chart width: 100, height: 100, padding: 0,
    <data values: [{text: "bad", weight: -1}]> <mark type: "wordcloud">>
let repeated = chart.render_spec(vega.convert({repeat: {column: ["a", "b"]},
    data: {values: [{a: 1, b: 2, y: 0}, {a: 3, b: 4, y: 1}]},
    spec: {width: 200, height: 150, mark: "point", encoding: {
        x: {field: {repeat: "column"}, type: "quantitative"}, y: {field: "y", type: "quantitative"}}}}));
[
    len(content(marks[0])) == 2, len(content(marks[1])) == 1,
    marks[1][0].x1 > marks[0][1].cx, marks[1][0].y1 > marks[0][0].cy, marks[1][0].y2 < marks[0][1].cy,
    grid.width == 400, grid.height == 336, len(content(grid)) == 5,
    grid[1][0][0] == "A / Summer", grid[2][0][0] == "A / Winter", grid[3][0][0] == "B / Summer",
    grid[1][1][1][0][0].cy > grid[3][1][1][0][0].cy,
    vl_grid.width == 400, len(content(vl_grid)) == 3,
    chart.render(<hconcat for (child in [bad, bad]) child>) is error,
    chart.render(<chart width: 100, height: 100, padding: 0,
        <data values: [{group: "A", text: "bad", weight: -1}]> <mark type: "wordcloud"> <facet field: "group">>) is error,
    repeated.width == 410, len(content(repeated)) == 3
]
