// Test: rule mark (horizontal reference lines) from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    data: { values: [
        {threshold: 25},
        {threshold: 50}
    ]},
    mark: {'type': "rule", color: "#e45756"},
    encoding: {
        y: {field: "threshold", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
