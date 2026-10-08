import cloud: lambda.chart.wordcloud

let image = cloud.render([
    {text: "Custom", weight: 2, font_size: 38, font_family: "serif", font_weight: 800, rotation: 22.5, color: "#abcdef"},
    {text: "Default", weight: 1}
], {width: 500, height: 300, shape: "ellipse", spiral: "rectangular", seed: 91,
    rotations: [-30], size_scale: "log", max_font_size: 24})^
let custom = image[0][0][1]
let standard = image[0][1][1]
let xml = format(image, 'xml');
[
    image["data-unplaced"] == 0, len(content(image[0])) == 2,
    custom["font-size"] == 38, custom["font-family"] == "serif",
    custom["font-weight"] == 800, custom.fill == "#abcdef",
    standard["font-size"] == 12, standard["font-family"] == "sans-serif",
    contains(image[0][0].transform, "rotate(22.5)"),
    contains(image[0][1].transform, "rotate(-30)"),
    contains(xml, "font-weight=\"800\""), custom[0] == "Custom"
]
