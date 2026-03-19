// Test: candlestick chart (OHLC stock chart) using layered composition
// Rule mark: high-low whisker (y to y2)
// Bar mark: open-close body (y to y2) colored by direction
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let raw_data = [
    {date: "Mon", open: 100.0, high: 115.0, low: 95.0,  close: 110.0, direction: "up"},
    {date: "Tue", open: 110.0, high: 118.0, low: 105.0, close: 108.0, direction: "down"},
    {date: "Wed", open: 108.0, high: 120.0, low: 102.0, close: 117.0, direction: "up"},
    {date: "Thu", open: 117.0, high: 122.0, low: 110.0, close: 112.0, direction: "down"},
    {date: "Fri", open: 112.0, high: 125.0, low: 108.0, close: 122.0, direction: "up"}
]

let vl = {
    width: 400,
    height: 300,
    title: "Stock Price - Candlestick",
    data: { values: raw_data },
    layer: [
        {
            mark: {'type': "rule"},
            encoding: {
                x: {field: "date", 'type': "ordinal"},
                y: {field: "low",  'type': "quantitative"},
                y2: {field: "high"}
            }
        },
        {
            mark: {'type': "bar", width: 8},
            encoding: {
                x: {field: "date", 'type': "ordinal"},
                y: {field: "open", 'type': "quantitative"},
                y2: {field: "close"},
                color: {
                    field: "direction",
                    'type': "nominal",
                    scale: {domain: ["up", "down"], range: ["#26a69a", "#ef5350"]}
                }
            }
        }
    ]
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
