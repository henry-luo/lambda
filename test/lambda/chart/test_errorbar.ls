// Test: error bar chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Measurements with Error",
    data: { values: [
        {experiment: "A", low: 10, high: 20},
        {experiment: "B", low: 15, high: 30},
        {experiment: "C", low: 8, high: 18},
        {experiment: "D", low: 22, high: 35}
    ]},
    mark: {'type': "errorbar"},
    encoding: {
        x: {field: "experiment", 'type': "nominal"},
        y: {field: "low", 'type': "quantitative"},
        y2: {field: "high"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
