// Test: bar chart with conditional color
import chart: lambda.package.chart.chart

let spec =
<chart width: 400, height: 300, title: "Status Report";
    <data;
        <row item: "A", value: 80, status: "pass">
        <row item: "B", value: 45, status: "fail">
        <row item: "C", value: 90, status: "pass">
        <row item: "D", value: 30, status: "fail">
    >
    <encoding;
        <x field: "item", dtype: "nominal">
        <y field: "value", dtype: "quantitative">
        <color value: "#999",
               condition: {field: "status", equal: "pass", value: "#4CAF50"}>
    >
    <mark type: "bar">
>

let svg = chart.render_spec(spec)
format(svg, 'xml')
