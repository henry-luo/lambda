// Test: temporal x-axis with datetime data
import chart: lambda.package.chart.chart

let spec =
<chart width: 400, height: 300, title: "Temperature Over Time";
    <data;
        <row date: "2024-01-01", temp: 5>
        <row date: "2024-04-01", temp: 15>
        <row date: "2024-07-01", temp: 28>
        <row date: "2024-10-01", temp: 12>
    >
    <encoding;
        <x field: "date", dtype: "temporal">
        <y field: "temp", dtype: "quantitative">
    >
    <mark type: "line">
>

let svg = chart.render_spec(spec)
format(svg, 'xml')
