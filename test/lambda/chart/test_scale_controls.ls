import chart: lambda.chart.chart
import scale: lambda.chart.scale

let data = [{x: 1, y: 1}, {x: 10, y: 10}, {x: 100, y: 100}]
let log = scale.position_scale({field: "x", dtype: "quantitative", scale: {type: "log", domain: [1, 100], range: [0, 200]}}, data, 0, 1, "point", true)
let reversed = scale.position_scale({field: "x", dtype: "quantitative", scale: {domain: [0, 100], reverse: true, clamp: true, nice: false}}, data, 0, 200, "point", true)
let power = scale.configured_scale([0, 10], 0, 100, "pow", {exponent: 2, nice: false})
let rooted = scale.configured_scale([-4, 4], 0, 100, "sqrt", {nice: false})
let bands = scale.configured_scale(["A", "B"], 0, 100, "band", {padding_inner: 0.2, padding_outer: 0.1, reverse: true})
let inverted = scale.band_scale(["A", "B"], 100, 0, 4)
let zero_off = scale.position_scale({field: "y", dtype: "quantitative", zero: false, scale: {nice: false}}, [{y: 12}, {y: 18}], 0, 100, "bar", false)
let ranged = scale.position_scale({field: "y", dtype: "quantitative", scale: {nice: false}}, [{y: 12, high: 30}, {y: 18, high: 40}], 0, 100, "errorbar", false, "high")
let sorted = scale.position_scale({field: "label", dtype: "nominal", sort: "descending"}, [{label: "A"}, {label: "B"}], 0, 100, "point", true)
let image = chart.render(<chart width: 400, height: 300,
    <data values: data> <mark type: "point">
    <encoding
        <x field: "x", dtype: "quantitative", scale: {type: "log", domain: [1, 100]}>
        <y field: "y", dtype: "quantitative", scale: {domain: [0, 100], reverse: true}>
    >
>)
let points = image[1][0];
[
    scale.scale_apply(log, 1) == 0, abs(scale.scale_apply(log, 10) - 100) < 0.000001,
    scale.scale_apply(log, 100) == 200, abs(scale.scale_invert(log, 100) - 10) < 0.000001,
    scale.scale_apply(reversed, 25) == 150, scale.scale_apply(reversed, 200) == 0,
    scale.scale_apply(reversed, -100) == 200, scale.scale_invert(reversed, 50) == 75,
    scale.scale_apply(power, 5) == 25, scale.scale_invert(power, 25) == 5,
    scale.scale_apply(rooted, -1) == 25, scale.scale_invert(rooted, 25) == -1,
    bands.bandwidth == 40, scale.scale_apply(bands, "A") == 55, scale.scale_apply(bands, "B") == 5,
    abs(inverted.bandwidth) == 44, scale.scale_apply(inverted, "A") == 96,
    scale.scale_apply(inverted, "B") == 48,
    zero_off.domain == [12, 18], ranged.domain == [12, 40], sorted.domain == ["B", "A"],
    points[0].cx == 0, abs(points[1].cx * 2 - points[2].cx) < 0.000001,
    points[0].cy < points[1].cy and points[1].cy < points[2].cy
]
