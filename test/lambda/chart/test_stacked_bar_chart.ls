// Test: stacked bar chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Sales by Category and Product",
    data: { values: [
        {category: "A", product: "P1", amount: 20},
        {category: "A", product: "P2", amount: 10},
        {category: "B", product: "P1", amount: 30},
        {category: "B", product: "P2", amount: 25},
        {category: "C", product: "P1", amount: 15},
        {category: "C", product: "P2", amount: 35}
    ]},
    mark: {'type': "bar"},
    encoding: {
        x: {field: "category", 'type': "nominal"},
        y: {field: "amount", 'type': "quantitative"},
        color: {field: "product", 'type': "nominal"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
