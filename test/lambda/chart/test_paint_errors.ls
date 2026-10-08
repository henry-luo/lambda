import chart: lambda.chart.chart

let linear = {gradient: "linear", stops: [{offset: 0, color: "red"}, {offset: 1, color: "blue", opacity: 0}]}
let radial = {gradient: "radial", r1: 0.1, r2: 0.7, stops: linear.stops}
let hatch = {pattern: "hatch", color: "black", spacing: 10}
fn rejected(value) => chart.render(<chart <data values: [{x: "A", y: 1}]>
    <mark type: "bar", fill: value>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>>) is error
let checks = [
    {name: "missing stops rejected", ok: rejected({gradient: "linear"})},
    {name: "empty stops rejected", ok: rejected({gradient: "radial", stops: []})},
    {name: "unknown gradient rejected", ok: rejected({gradient: "conic", stops: linear.stops})},
    {name: "unknown pattern rejected", ok: rejected({pattern: "dots"})},
    {name: "unknown paint map rejected", ok: rejected({red: 1})},
    {name: "numeric paint rejected", ok: rejected(42)},
    {name: "offset range rejected", ok: rejected({*:linear, stops: [{offset: 2, color: "red"}]})},
    {name: "unordered stops rejected", ok: rejected({*:linear, stops: [{offset: 1, color: "red"}, {offset: 0, color: "blue"}]})},
    {name: "nonfinite stop rejected", ok: rejected({*:linear, stops: [{offset: nan, color: "red"}]})},
    {name: "nonstring stop color rejected", ok: rejected({*:linear, stops: [{offset: 0, color: 123}]})},
    {name: "invalid stop opacity rejected", ok: rejected({*:linear, stops: [{offset: 0, color: "red", opacity: -1}]})},
    {name: "nonfinite gradient coordinate rejected", ok: rejected({*:linear, x1: inf})},
    {name: "invalid gradient spread rejected", ok: rejected({*:linear, spread: "cycle"})},
    {name: "negative radius rejected", ok: rejected({*:radial, r1: -1})},
    {name: "inverted radial circles rejected", ok: rejected({*:radial, r1: 2})},
    {name: "zero hatch spacing rejected", ok: rejected({*:hatch, spacing: 0})},
    {name: "negative hatch stroke rejected", ok: rejected({*:hatch, stroke_width: -1})},
    {name: "nonfinite hatch angle rejected", ok: rejected({*:hatch, angle: nan})},
    {name: "invalid hatch cross rejected", ok: rejected({*:hatch, cross: 1})},
    {name: "invalid hatch background rejected", ok: rejected({*:hatch, background: linear})},
    {name: "hard edge stops allowed", ok: not rejected({*:linear, stops: [{offset: 0.5, color: "red"}, {offset: 0.5, color: "blue"}]})},
    {name: "transparent hatch allowed", ok: not rejected({pattern: "hatch", opacity: 0, stroke_width: 0})},
    {name: "invalid layered paint retains diagnostic", ok: chart.render(<chart
        <data values: [{x: "A", y: 1}]>
        <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>
        <layer <chart <mark type: "bar", fill: {gradient: "linear"}>>>>) is error}
];
[for (check in checks where check.ok != true) check.name]
