import chart: lambda.chart.chart

let data = [<row text: "One", weight: 2, alternate: "First", group: "A">,
    <row text: "Two", weight: 1, alternate: "Second", group: "B">]
let base = <chart width: 300, height: 200, padding: 0,
    <data values: data> <mark type: "wordcloud", max_font_size: 24>
>
let horizontal = chart.render(<hconcat spacing: 0, for (child in [base, base]) child>)
let vertical = chart.render(<vconcat spacing: 7, for (child in [base, base]) child>)
let faceted = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: data> <mark type: "wordcloud", max_font_size: 24>
    <facet field: "group", columns: 2>
>)
let repeated = chart.render(<repeat
    <column ["text", "alternate"]>
    <chart width: 300, height: 200, padding: 0,
        <data values: data> <mark type: "wordcloud", max_font_size: 24>
        <encoding <text field: {repeat: "column"}> <size field: "weight">>
    >
>)
let layered = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: data>
    <layer
        <chart <mark type: "wordcloud", max_font_size: 24>>
        <chart <mark type: "wordcloud", max_font_size: 24, color: "red">>
    >
>)
let bad_layer = chart.render(<chart <data values: [{text: "Bad", weight: 0}]>
    <layer <chart <mark type: "wordcloud">>>
>);
[
    horizontal.width == 600, horizontal.height == 200,
    vertical.width == 300, vertical.height == 407,
    contains(format(horizontal, 'xml'), "wordcloud"),
    len(content(faceted[1][1][1][0][0])) == 1,
    len(content(faceted[2][1][1][0][0])) == 1,
    contains(format(repeated[1], 'xml'), "One"),
    contains(format(repeated[2], 'xml'), "First"),
    len(content(layered[1][0])) == 2,
    layered[1][0][1][0][0][1].fill == "red",
    bad_layer is error
]
