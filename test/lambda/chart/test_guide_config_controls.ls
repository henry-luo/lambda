import chart: lambda.chart.chart
import svg: lambda.chart.svg

let image = chart.render(<chart width: 400, height: 300, padding: 0, title: "Styled",
    <config theme: {background: "#123456", axis_label_color: "purple", mark_color: "orange"},
        font: "serif", mark_opacity: 0>
    <data values: [{x: 0, y: 1}, {x: 5, y: 2}, {x: 10, y: 3}]>
    <mark type: "point", size: 50>
    <encoding
        <x field: "x", dtype: "quantitative", scale: {domain: [0, 10]},
            axis: {values: [0, 5, 10], domain: false, ticks: false, label_angle: 45, title: null}>
        <y field: "y", dtype: "quantitative", axis: null>
    >
>)
let plot = image[1]
let x_axis = plot[1]
let raw = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{x: 12, y: 34, color: "red"}]> <mark type: "point">
    <encoding
        <x field: "x", dtype: "quantitative", scale: null>
        <y field: "y", dtype: "quantitative", scale: false>
        <color field: "color", dtype: "nominal", scale: null, legend: null>
    >
>)
let constant = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{}]> <mark type: "point">
    <encoding <x value: 12> <y value: 34>>
>)
let presentation = chart.render(<chart width: 500, height: 400,
    <config theme: "presentation">
    <data values: [{x: 1, y: 2}, {x: 2, y: 4}]> <mark type: "point">
    <encoding <x field: "x", dtype: "quantitative"> <y field: "y", dtype: "quantitative">>
>)
let nested = chart.render(<chart width: 200, height: 150, padding: 0,
    <config theme: {mark: {color: "purple"}}, point: {size: 100}, mark: {opacity: 0}>
    <data values: [{}]> <mark type: "point"> <encoding <x value: 20> <y value: 30>>
>)[1][0][0];

[
    image[0].fill == "#123456", image["font-family"] == "serif",
    plot[0][0].fill == "orange", plot[0][0].opacity == 0,
    len(content(plot)) == 2, len(content(x_axis)) == 3,
    name(x_axis[0]) == 'g', len(content(x_axis[0])) == 1,
    x_axis[0][0].fill == "purple", x_axis[0][0].transform == svg.rotate(45, x_axis[0][0].x, x_axis[0][0].y),
    len(content(raw[1])) == 1, raw[1][0][0].cx == 12, raw[1][0][0].cy == 34,
    raw[1][0][0].fill == "red", len(content(raw)) == 2,
    constant[1][0][0].cx == 12, constant[1][0][0].cy == 34,
    presentation[1][1][1][1]["font-size"] == 16,
    nested.fill == "purple", nested.opacity == 0, abs(nested.r * nested.r * 3.141592653589793 - 100) < 0.000001
]
