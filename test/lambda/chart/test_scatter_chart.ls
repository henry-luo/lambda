// Test: scatter chart from Vega-Lite JSON
import vega: .lambda.package.chart.vega
import chart: .lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    data: { values: [
        {x: 10, y: 20, size: 5},
        {x: 20, y: 40, size: 10},
        {x: 30, y: 15, size: 8},
        {x: 40, y: 50, size: 12},
        {x: 50, y: 35, size: 6}
    ]},
    mark: "point",
    encoding: {
        x: {field: "x", "type": "quantitative"},
        y: {field: "y", "type": "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
