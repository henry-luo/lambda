// Test: arc (pie) chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 300,
    height: 300,
    title: "Market Share",
    data: { values: [
        {category: "A", value: 40},
        {category: "B", value: 30},
        {category: "C", value: 20},
        {category: "D", value: 10}
    ]},
    mark: {'type': "arc"},
    encoding: {
        theta: {field: "value", 'type': "quantitative"},
        color: {field: "category", 'type': "nominal"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
