// Test: hconcat chart composition - two charts side by side
import chart: lambda.package.chart.chart

let data_a = [
    <row cat: "X", val: 10>,
    <row cat: "Y", val: 30>,
    <row cat: "Z", val: 20>
]

let data_b = [
    <row x: 1, y: 10>,
    <row x: 2, y: 25>,
    <row x: 3, y: 15>
]

let spec =
<hconcat;
    <chart width: 200, height: 150;
        <data values: data_a>
        <mark type: "bar">
        <encoding;
            <x field: "cat", dtype: "nominal">
            <y field: "val", dtype: "quantitative">
        >
    >
    <chart width: 200, height: 150;
        <data values: data_b>
        <mark type: "line">
        <encoding;
            <x field: "x", dtype: "quantitative">
            <y field: "y", dtype: "quantitative">
        >
    >
>

let svg = chart.render(spec)
format(svg, 'xml')
