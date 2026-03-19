// Test: chart with text and rule annotations
import chart: lambda.package.chart.chart

let spec =
<chart width: 400, height: 300, title: "Annotated Chart";
    <data;
        <row x: 10, y: 30>
        <row x: 20, y: 60>
        <row x: 30, y: 45>
        <row x: 40, y: 80>
        <row x: 50, y: 55>
    >
    <encoding;
        <x field: "x", dtype: "quantitative">
        <y field: "y", dtype: "quantitative">
    >
    <mark type: "point">
    <annotation;
        <text_note x: 40, y: 80, text: "Peak", color: "red",
                   font_size: 12, anchor: "start", dx: 5, dy: -5>
        <rule_note y: 50, color: "#888", stroke_dash: "4 2">
    >
>

let svg = chart.render_spec(spec)
format(svg, 'xml')
