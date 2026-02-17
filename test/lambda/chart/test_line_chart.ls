// Test: line chart from Vega-Lite JSON
import vega: .lambda.package.chart.vega
import chart: .lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    data: { values: [
        {x: 0, y: 10},
        {x: 1, y: 30},
        {x: 2, y: 25},
        {x: 3, y: 45},
        {x: 4, y: 40}
    ]},
    mark: {"type": "line", point: true},
    encoding: {
        x: {field: "x", "type": "quantitative"},
        y: {field: "y", "type": "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
