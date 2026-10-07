import cloud: lambda.chart.wordcloud

let words = [{text: "A&B <C>", weight: 2, color: "#123456"},
    {text: "café 中文", weight: 1}]
let opts = {width: 600.5, height: 300.5, max_font_size: 32,
    font_family: "serif", font_weight: 700, colors: ["red", "blue"], rotations: [90, 0]}
let image = cloud.render(words, opts)^
let xml = format(image, 'xml')
let first_word = image[0][0][1]
let second_word = image[0][1][1]
let blank = cloud.render([])^
let full = cloud.render([{text: "First", weight: 1}, {text: "Second", weight: 1}],
    {max_steps: 1})^;
[
    name(image) == 'svg', image.width == 600.5, image.height == 300.5,
    image.viewBox == "0 0 600.5 300.5", image["data-unplaced"] == 0,
    len(content(image[0])) == 2, name(first_word) == 'text',
    first_word[0] == "A&B <C>", second_word[0] == "café 中文",
    first_word.fill == "#123456", second_word.fill == "blue",
    first_word["font-family"] == "serif", first_word["font-weight"] == 700,
    contains(image[0][0].transform, "rotate(90)"),
    contains(xml, "A&amp;B &lt;C&gt;"), contains(xml, "café 中文"),
    len(content(blank[0])) == 0, full["data-unplaced"] == 1
]
