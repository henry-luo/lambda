// Test: stacked area chart from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "Revenue Over Time",
    data: { values: [
        {month: "Jan", product: "A", revenue: 100},
        {month: "Feb", product: "A", revenue: 120},
        {month: "Mar", product: "A", revenue: 150},
        {month: "Jan", product: "B", revenue: 80},
        {month: "Feb", product: "B", revenue: 90},
        {month: "Mar", product: "B", revenue: 110}
    ]},
    mark: {'type': "area"},
    encoding: {
        x: {field: "month", 'type': "nominal"},
        y: {field: "revenue", 'type': "quantitative"},
        color: {field: "product", 'type': "nominal"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
