// Test: repeat chart composition - scatter plot matrix
import chart: lambda.package.chart.chart

let data = [
    <row a: 1, b: 2, c: 3>,
    <row a: 4, b: 5, c: 6>,
    <row a: 7, b: 8, c: 9>
]

let spec =
<repeat;
    <row; ["a", "b"]>
    <column; ["b", "c"]>
    <chart width: 150, height: 150;
        <data values: data>
        <mark type: "point">
        <encoding;
            <x field: {repeat: "column"}, dtype: "quantitative">
            <y field: {repeat: "row"}, dtype: "quantitative">
        >
    >
>

let svg = chart.render(spec)
format(svg, 'xml')
