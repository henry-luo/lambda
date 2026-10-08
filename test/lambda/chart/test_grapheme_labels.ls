import radiant
import text: lambda.chart.text
import axis: lambda.chart.axis
import scale: lambda.chart.scale
import leg: lambda.chart.legend

// Expected clusters exercise UAX #29 context, not merely combining-mark categories.
let cases = [
    {source: "", expected: []},
    {source: "ab", expected: ["a", "b"]},
    {source: "éx", expected: ["é", "x"]},
    {source: "́̀x", expected: ["́̀", "x"]},
    {source: "a\r\nb", expected: ["a", "\r\n", "b"]},
    {source: "a\u0000b", expected: ["a", "\u0000", "b"]},
    {source: "한x", expected: ["한", "x"]},
    {source: "✈️x", expected: ["✈️", "x"]},
    {source: "1️⃣x", expected: ["1️⃣", "x"]},
    {source: "👍🏽x", expected: ["👍🏽", "x"]},
    {source: "👨‍👩‍👧‍👦x", expected: ["👨‍👩‍👧‍👦", "x"]},
    {source: "🇸🇬🇸🇪🇦", expected: ["🇸🇬", "🇸🇪", "🇦"]},
    {source: "क्‍षx", expected: ["क्‍ष", "x"]}
]
let clusters = ["é", "한", "✈️", "1️⃣", "👍🏽", "👨‍👩‍👧‍👦", "🇸🇬", "क्‍ष"]
let font = text.style({label_font_size: 24})
let widths = text.measure([for (cluster in clusters) cluster ++ "…"], font)
let fitted = [for (index, cluster in clusters) text.fit([cluster ++ "WWWWWWWW"], font, text.span(widths[index]) + 0.01)[0]]
let first_width = text.span(text.measure(["é…"], font)[0]) + 0.01
let guide = axis.prepare(scale.point_scale(["éWWWWWWWW"], 0, 100, 0),
    {label_font_size: 24, label_limit: first_width})
let legend = leg.color_legend(["éWWWWWWWW"], scale.ordinal_scale(["éWWWWWWWW"], ["red"]), null,
    {label_font_size: 24, label_limit: first_width})
let checks = [
    for (index, item in cases) {name: "cluster " ++ string(index), ok: radiant.graphemes(item.source) == item.expected},
    for (index, item in fitted) {name: "fit " ++ string(index), ok: item.text == clusters[index] ++ "…" and
        text.span(item.metric) <= text.span(widths[index]) + 0.011},
    {name: "axis uses whole clusters", ok: guide._labels[0].text == "é…"},
    {name: "legend uses whole clusters", ok: legend[0][1][0] == "é…"},
    {name: "input spelling preserved", ok: join(radiant.graphemes("é"), "") == "é"},
    {name: "indivisible first cluster", ok: text.fit(["👨‍👩‍👧‍👦WWWWWWWW"], font,
        text.span(text.measure(["…"], font)[0]) + 0.01)[0].text == "…"}
];
[for (check in checks where check.ok != true) check.name]
