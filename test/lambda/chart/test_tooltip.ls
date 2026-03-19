// Test: scatter chart with tooltip
import chart: lambda.package.chart.chart

let spec =
<chart width: 400, height: 300;
    <data;
        <row x: 10, y: 20, label: "Alpha">
        <row x: 30, y: 50, label: "Beta">
        <row x: 50, y: 35, label: "Gamma">
    >
    <encoding;
        <x field: "x", dtype: "quantitative">
        <y field: "y", dtype: "quantitative">
        <tooltip field: "label">
    >
    <mark type: "point">
>

let svg = chart.render_spec(spec)
format(svg, 'xml')
