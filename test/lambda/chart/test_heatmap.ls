// Test: heatmap (rect mark) from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 300,
    height: 300,
    title: "Activity Heatmap",
    data: { values: [
        {day: "Mon", hour: "9am", count: 5},
        {day: "Mon", hour: "12pm", count: 12},
        {day: "Mon", hour: "3pm", count: 8},
        {day: "Tue", hour: "9am", count: 3},
        {day: "Tue", hour: "12pm", count: 15},
        {day: "Tue", hour: "3pm", count: 10},
        {day: "Wed", hour: "9am", count: 7},
        {day: "Wed", hour: "12pm", count: 9},
        {day: "Wed", hour: "3pm", count: 14}
    ]},
    mark: {'type': "rect"},
    encoding: {
        x: {field: "day", 'type': "ordinal"},
        y: {field: "hour", 'type': "ordinal"},
        color: {field: "count", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
