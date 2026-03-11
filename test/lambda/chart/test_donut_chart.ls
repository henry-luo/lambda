// Test: donut chart (arc with inner radius) from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 300,
    height: 300,
    title: "Revenue Split",
    data: { values: [
        {segment: "Online",  revenue: 50},
        {segment: "Retail",  revenue: 30},
        {segment: "Partner", revenue: 20}
    ]},
    mark: {'type': "arc", innerRadius: 60},
    encoding: {
        theta: {field: "revenue", 'type': "quantitative"},
        color: {field: "segment", 'type': "nominal"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
