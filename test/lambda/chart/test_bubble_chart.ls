// Test: bubble chart (point mark with size encoding) from Vega-Lite JSON
import vega: lambda.package.chart.vega
import chart: lambda.package.chart.chart

let vl = {
    width: 400,
    height: 300,
    title: "GDP vs Life Expectancy",
    data: { values: [
        {country: "US", gdp: 60000, life_exp: 78, pop: 330},
        {country: "UK", gdp: 42000, life_exp: 81, pop: 67},
        {country: "JP", gdp: 40000, life_exp: 84, pop: 126},
        {country: "BR", gdp: 9000, life_exp: 75, pop: 212},
        {country: "IN", gdp: 2100, life_exp: 69, pop: 1380}
    ]},
    mark: {'type': "point"},
    encoding: {
        x: {field: "gdp", 'type': "quantitative"},
        y: {field: "life_exp", 'type': "quantitative"},
        size: {field: "pop", 'type': "quantitative"}
    }
}

let spec = vega.convert(vl)
let svg = chart.render_spec(spec)
format(svg, 'xml')
