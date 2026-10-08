import chart: lambda.chart.chart
import svg: lambda.chart.svg
import util: lambda.chart.util

let points = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{x: 20, y: 30, label: "A", value: 0.25}, {x: 80, y: 90, label: "B", value: 0.75}]>
    <mark type: "point">
    <encoding
        <x field: "x", scale: null> <y field: "y", scale: null>
        <color condition: {test: (row) => row.value > 0.5, value: "red"}, value: "blue">
        <size condition: {field: "label", equal: "A", value: 100}, value: 64>
        <opacity condition: {field: "value", lt: 0.5, value: 0}, value: 0.8>
        <shape condition: {field: "label", equal: "A", value: "square"}, value: "diamond">
        <stroke value: "black">
        <tooltip fields: ["label", "value"]>
    >
>)
let symbols = points[1][0]
let range_rect = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{x: 80, y: 90, xend: 20, yend: 30}]> <mark type: "rect", stroke: "black", stroke_width: 2>
    <encoding <x field: "x", scale: null> <y field: "y", scale: null>
        <x2 field: "xend"> <y2 field: "yend"> <color value: "orange">>
>)[1][0][0]
let rule = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{}]> <mark type: "rule", stroke_dash: "3,2">
    <encoding <x value: 10> <y value: 20> <x2 value: 90> <y2 value: 80> <stroke value: "purple">>
>)[1][0][0]
let horizontal = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{cat: "A", value: -4}, {cat: "B", value: 8}]> <mark type: "bar">
    <encoding <x field: "value", dtype: "quantitative", axis: null>
        <y field: "cat", dtype: "nominal", axis: null>>
>)[1][0]
let formatted = chart.render(<chart width: 200, height: 150, padding: 0,
    <data values: [{x: 20, y: 30, value: 0.25}]> <mark type: "text">
    <encoding <x value: 20> <y value: 30> <text field: "value", format: ".1%"> <color value: "green">>
>)[1][0][0];
[
    name(symbols[0]) == 'rect', symbols[0].x == 15, symbols[0].y == 25, symbols[0].width == 10,
    symbols[0].fill == "blue", symbols[1].fill == "red", symbols[0].opacity == 0,
    symbols[1].opacity == 0.8, symbols[1].stroke == "black", name(symbols[1]) == 'path',
    symbols[0][0][0] == "label: A\nvalue: 0.25",
    range_rect.x == 20, range_rect.y == 30, range_rect.width == 60, range_rect.height == 60,
    range_rect.fill == "orange", range_rect.stroke == "black", range_rect["stroke-width"] == 2,
    rule.x1 == 10, rule.y1 == 20, rule.x2 == 90, rule.y2 == 80, rule.stroke == "purple",
    rule["stroke-dasharray"] == "3,2", horizontal.class == "marks bars-horizontal",
    horizontal[0].width > 0, horizontal[1].width > horizontal[0].width,
    horizontal[0].height > 0, horizontal[0].y > horizontal[1].y,
    formatted[0] == "25.0%", formatted.fill == "green",
    svg.line_path([[0, 0], [10, 20]], "step") == "M0 0 L5 0 L5 20 L10 20",
    svg.line_path([[0, 0], [10, 20]], "step-before") == "M0 0 L0 20 L10 20",
    index_of(svg.line_path([[0, 0], [10, 20], [20, 0]], "cardinal"), "C") != null,
    util.format_value(12.3, ".2f") == "12.30", util.format_value(-0.125, ".1%") == "-12.5%"
]
