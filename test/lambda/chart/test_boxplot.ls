// Test: box plot chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Distribution by Group",
    data: { values: [
        {group: "A", value: 10},
        {group: "A", value: 15},
        {group: "A", value: 20},
        {group: "A", value: 25},
        {group: "A", value: 30},
        {group: "A", value: 50},
        {group: "B", value: 5},
        {group: "B", value: 12},
        {group: "B", value: 18},
        {group: "B", value: 22},
        {group: "B", value: 28}
    ]},
    mark: {'type': "boxplot"},
    encoding: {
        x: {field: "group", 'type': "nominal"},
        y: {field: "value", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
