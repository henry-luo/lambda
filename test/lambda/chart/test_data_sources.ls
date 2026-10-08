import chart: lambda.chart.chart
import vega: lambda.chart.vega
let native = chart.render(<chart width: 200, height: 150, padding: 0,
    <data url: "test/lambda/chart/wordcloud_data.json", format: 'json'>
    <mark type: "wordcloud", min_font_size: 12, max_font_size: 12>>)
let converted = chart.render_spec(vega.convert({width: 200, height: 150, padding: 0,
    data: {url: "test/lambda/chart/wordcloud_data.json", format: {type: "json"}},
    mark: {type: "wordcloud", min_font_size: 12, max_font_size: 12}}))
let missing = chart.render(<chart <data name: "missing"> <mark type: "point">>)
let absent_file = chart.render(<chart <data url: "test/lambda/chart/no_such_chart_data.json"> <mark type: "point">>);
[
    len(content(native[1][0][0])) == 2, native["data-unplaced"] == 0,
    format(native, 'xml') == format(converted, 'xml'), missing is error, absent_file is error
]
