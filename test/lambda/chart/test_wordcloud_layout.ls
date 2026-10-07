import cloud: lambda.chart.wordcloud

let words = [
    {text: "Lambda", weight: 40}, {text: "Documents", weight: 25},
    {text: "Charts", weight: 15}, {text: "Data", weight: 30},
    {text: "Types", weight: 20}, {text: "Functions", weight: 18},
    {text: "SVG", weight: 10}, {text: "Layout", weight: 12},
    {text: "Unicode", weight: 8}, {text: "Packages", weight: 6}
]
let opts = {width: 800, height: 500, margin: 12, padding: 5, rotations: [0, 90, -90]}
let result = cloud.layout(words, opts)^
let repeat_result = cloud.layout(words, opts)^
let outside = [for (word in result.words where
    word.x - word.width / 2 < opts.margin or
    word.y - word.height / 2 < opts.margin or
    word.x + word.width / 2 > opts.width - opts.margin or
    word.y + word.height / 2 > opts.height - opts.margin) word.text]
let overlapping = [for (i, a in result.words, j, b in result.words where j > i and
    abs(a.x - b.x) < (a.width + b.width) / 2 + opts.padding and
    abs(a.y - b.y) < (a.height + b.height) / 2 + opts.padding) [a.text, b.text]]
let rotations_ok = len([for (w in result.words where
    (w.rotation == 0 and (w.width != w.text_width or w.height != w.text_height)) or
    (w.rotation != 0 and (w.width != w.text_height or w.height != w.text_width))) w]) == 0
let sized = cloud.layout([{text: "iii", weight: 1}, {text: "WWW", weight: 1}],
    {min_font_size: 24, max_font_size: 24})^
let ties = cloud.layout([{text: "first", weight: 2}, {text: "second", weight: 2},
    {text: "third", weight: 2}])^
let crowded = cloud.layout([{text: "One", weight: 1}, {text: "Two", weight: 1},
    {text: "Three", weight: 1}], {width: 300, height: 200, max_font_size: 20, max_steps: 1})^
let oversized = cloud.layout([{text: "An oversized phrase", weight: 1}],
    {width: 50, height: 50, min_font_size: 32, max_font_size: 32})^
let fractional = cloud.layout([{text: "A", weight: 1}], {width: 200.5, height: 100.5})^
let wide = cloud.layout([{text: "Lower", weight: 9007199254740992i64},
    {text: "Higher", weight: 9007199254740993i64}])^
let empty = cloud.layout([])^;
[
    len(result.words) == len(words), len(result.unplaced) == 0,
    result == repeat_result, len(outside) == 0, len(overlapping) == 0, rotations_ok,
    result.words[0].text == "Lambda", result.words[0].font_size == 64,
    result.words[len(result.words) - 1].font_size == 12,
    sized.words[0].width < sized.words[1].width,
    [for (w in ties.words) w.text] == ["first", "second", "third"],
    len(crowded.words) == 1 and len(crowded.unplaced) == 2,
    crowded.unplaced[0].reason == "no_space",
    len(oversized.words) == 0 and oversized.unplaced[0].reason == "too_large",
    fractional.words[0].x == 100.25 and fractional.words[0].y == 50.25,
    wide.words[0].text == "Higher" and wide.words[0].weight == 9007199254740993i64,
    wide.words[0].font_size == 64 and wide.words[1].font_size == 64,
    len(empty.words) == 0 and len(empty.unplaced) == 0
]
