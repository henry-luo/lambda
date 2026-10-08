import stack: lambda.chart.stack
import chart: lambda.chart.chart
let data = [{x: "A", group: "first", value: 3}, {x: "A", group: "second", value: 5},
    {x: "A", group: "negative", value: -4}, {x: "A", group: "first", value: 2}]
let ordered = stack.apply_stack(data, "value", "group", "x", "zero", ["second", "first", "negative"])
let normalized = stack.apply_stack(data, "value", "group", "x", "normalize")
let centered = stack.apply_stack(data, "value", "group", "x", "center")
let image = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: data> <mark type: "bar">
    <encoding <x field: "value", dtype: "quantitative", axis: null> <y field: "x", dtype: "nominal", axis: null>
        <color field: "group", dtype: "nominal", legend: null>>
>)
let bars = image[1][0];
[
    ordered[0]._y0 == 5, ordered[0]._y1 == 8, ordered[1]._y0 == 0, ordered[1]._y1 == 5,
    ordered[2]._y0 == 0, ordered[2]._y1 == -4, ordered[3]._y0 == 8, ordered[3]._y1 == 10,
    abs(normalized[2]._y1 + 4.0 / 14.0) < 0.000001,
    abs(normalized[1]._y1 - 10.0 / 14.0) < 0.000001,
    centered[2]._y1 == -7, centered[1]._y1 == 7,
    bars.class == "marks bars-horizontal", len(content(bars)) == 4,
    bars[0].width > 0, abs(bars[3].x - bars[0].x - bars[0].width) < 0.000001,
    bars[2].x < bars[0].x, abs(bars[1].x - bars[3].x - bars[3].width) < 0.000001
]
