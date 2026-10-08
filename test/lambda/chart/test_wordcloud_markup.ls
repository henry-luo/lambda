import chart: lambda.chart.chart
import cloud: lambda.chart.wordcloud
import vega: lambda.chart.vega

let rows = [
    {label: "Lambda", amount: 40, category: "A", font_weight: 700, rotation: -15},
    {label: "café 中文", amount: 25, category: "B"},
    {label: "Charts", amount: 15, category: "A"}
]
let spec = <chart width: 600.5, height: 400.5, padding: 0,
    <data values: rows>
    <mark type: "wordcloud", shape: "ellipse", rotations: [-30, 0, 30],
        size_scale: "log", seed: 17, max_font_size: 32, padding: 5,
        font_family: "serif", font_weight: 500, spiral: "rectangular">
    <encoding
        <text field: "label">
        <size field: "amount", dtype: "quantitative">
        <color field: "category", dtype: "nominal", scale: {domain: ["A", "B"], range: ["red", "blue"]}>
    >
>
let image = chart.render(spec)
let plot = image[1][0]
let expected = cloud.render([
    {text: "Lambda", weight: 40, color: "red", font_weight: 700, rotation: -15},
    {text: "café 中文", weight: 25, color: "blue"},
    {text: "Charts", weight: 15, color: "red"}
], {width: 600.5, height: 400.5, shape: "ellipse", rotations: [-30, 0, 30],
    size_scale: "log", seed: 17, max_font_size: 32, padding: 5,
    font_family: "serif", font_weight: 500, spiral: "rectangular"})^
let converted = chart.render_spec(vega.convert({width: 600.5, height: 400.5, padding: 0,
    data: {values: rows},
    mark: {type: "wordcloud", shape: "ellipse", rotations: [-30, 0, 30],
        size_scale: "log", seed: 17, max_font_size: 32, padding: 5,
        fontFamily: "serif", fontWeight: 500, spiral: "rectangular"},
    encoding: {text: {field: "label"}, size: {field: "amount", type: "quantitative"},
        color: {field: "category", type: "nominal", scale: {domain: ["A", "B"], range: ["red", "blue"]}}}}))
let filtered = chart.render(<chart width: 400, height: 250, title: "Cloud",
    <data <row text: "Keep", weight: 2> <row text: "Drop", weight: 1>>
    <transform <filter field: "weight", op: ">", value: 1>>
    <mark type: "wordcloud", font_size: 24, color: "purple">
>)
let invalid = chart.render(<chart <data values: [{text: "Invalid", weight: 0}]> <mark type: "wordcloud">>)
let no_room = chart.render(<chart width: 0, height: 100, <data values: []> <mark type: "wordcloud">>)
let empty = chart.render(<chart <data values: []> <mark type: "wordcloud">>)
let highlighted = chart.render(<chart width: 400, height: 300, padding: 0,
    <data values: rows> <mark type: "wordcloud", min_font_size: 12, max_font_size: 12>
    <encoding <text field: "label"> <size field: "amount">
        <color condition: {field: "amount", gt: 30, value: "pink"}, value: "blue">>
>);

[
    name(image) == 'svg', image.width == 600.5, image.height == 400.5,
    image["data-unplaced"] == 0, image.role == "img", image["aria-label"] == "Word cloud",
    plot == expected,
    contains(format(image, 'xml'), "café 中文"),
    converted[1][0] == expected,
    filtered["aria-label"] == "Cloud", len(content(filtered[1][0][0])) == 1,
    filtered[1][0][0][0][1].fill == "purple", filtered[1][0][0][0][1]["font-size"] == 24,
    invalid is error, no_room is error, len(content(empty[1][0][0])) == 0,
    highlighted[1][0][0][0][1].fill == "pink", highlighted[1][0][0][1][1].fill == "blue"
]
