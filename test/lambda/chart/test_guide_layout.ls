import chart: lambda.chart.chart
import parse_chart: lambda.chart.parse
import layout: lambda.chart.layout
import scale: lambda.chart.scale
import axis: lambda.chart.axis

let spec = <chart width: 400, height: 300, padding: 0,
    <data values: [{x: 0, y: 0, value: 0}, {x: 1, y: 1, value: 10}]> <mark type: "point">
    <encoding <x field: "x", dtype: "quantitative", axis: {orient: "top"}>
        <y field: "y", dtype: "quantitative", axis: {orient: "right"}>
        <color field: "value", dtype: "quantitative", legend: {orient: "left", title: null}>>
>
let geometry = layout.compute_layout(parse_chart.parse_chart(spec), scale.linear_scale(0, 1, 0, 400),
    scale.linear_scale(0, 1, 300, 0), true, [0, 10])
let image = chart.render(spec)
let gradient = image[2][0]
let percentage = axis.x_axis(scale.linear_scale(0, 1, 0, 100), 100, 100,
    {format: ".0%", values: [0, 1]}, null)
let reversed = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{x: "A", y: 1}, {x: "B", y: 2}]> <mark type: "bar">
    <encoding <x field: "x", dtype: "nominal", scale: {reverse: true}, axis: null>
        <y field: "y", dtype: "quantitative", axis: null>>
>)[1][0];
[
    geometry.plot_x > 0, geometry.plot_y > 0, geometry.right_margin > 0, geometry.bottom_margin == 0,
    geometry.legend_x == 0, image[1][1][0].y1 == 0, image[1][2][0].x1 > 0,
    gradient.class == "legend gradient-legend", name(gradient[0]) == 'rect', len(content(gradient)) == 22,
    gradient[0].fill == "#084594", gradient[19].fill == "#deebf7",
    percentage[1][1][0] == "0%", percentage[2][1][0] == "100%",
    reversed[0].width > 0, reversed[1].width > 0, reversed[0].x > reversed[1].x
]
