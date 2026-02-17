// Test: bar chart from Vega-Lite JSON
import vega: .lambda.package.chart.vega
import chart: .lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Sales by Category",
    data: { values: [
        {category: "A", amount: 28},
        {category: "B", amount: 55},
        {category: "C", amount: 43}
    ]},
    mark: {"type": "bar"},
    encoding: {
        x: {field: "category", "type": "nominal"},
        y: {field: "amount", "type": "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
