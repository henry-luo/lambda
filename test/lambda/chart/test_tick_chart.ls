// Test: tick mark chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    data: { values: [
        {x: "A", y: 10},
        {x: "B", y: 30},
        {x: "C", y: 25},
        {x: "D", y: 45},
        {x: "E", y: 35}
    ]},
    mark: {'type': "tick"},
    encoding: {
        x: {field: "x", 'type': "nominal"},
        y: {field: "y", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
