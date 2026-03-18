// Test: dark theme bar chart
import chart: lambda.package.chart.chart

let spec =
<chart width: 400, height: 300, title: "Sales by Region";
    <config theme: "dark">
    <data;
        <row region: "North", sales: 120>
        <row region: "South", sales: 85>
        <row region: "East", sales: 145>
        <row region: "West", sales: 100>
    >
    <encoding;
        <x field: "region", dtype: "nominal">
        <y field: "sales", dtype: "quantitative">
    >
    <mark type: "bar">
>

let svg = chart.render_spec(spec)
format(svg, 'xml')
