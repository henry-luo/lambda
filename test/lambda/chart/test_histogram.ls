// Test: histogram from Vega-Lite JSON with bin + count
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Score Distribution",
    data: { values: [
        {score: 55}, {score: 62}, {score: 68},
        {score: 72}, {score: 75}, {score: 78},
        {score: 80}, {score: 82}, {score: 85},
        {score: 88}, {score: 90}, {score: 95}
    ]},
    mark: {'type': "bar"},
    encoding: {
        x: {field: "score", bin: true, 'type': "quantitative"},
        y: {aggregate: "count", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
