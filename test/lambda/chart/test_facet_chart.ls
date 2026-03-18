// Test: facet chart composition - grid of sub-charts partitioned by a field
import chart: lambda.package.chart.chart

let data = [
    <row cat: "A", x: 1, y: 10>,
    <row cat: "A", x: 2, y: 20>,
    <row cat: "A", x: 3, y: 15>,
    <row cat: "B", x: 1, y: 30>,
    <row cat: "B", x: 2, y: 25>,
    <row cat: "B", x: 3, y: 35>
]

let spec =
<chart width: 200, height: 150;
    <data values: data>
    <mark type: "point">
    <encoding;
        <x field: "x", dtype: "quantitative">
        <y field: "y", dtype: "quantitative">
    >
    <facet field: "cat", columns: 2>
>

let svg = chart.render(spec)
format(svg, 'xml')
