import cloud: lambda.chart.wordcloud

fn rejected(words, opts) {
    let result = cloud.layout(words, opts) ^ { null };
    result == null
}

let good = [{text: "Word", weight: 1}];
[
    rejected(null, null), rejected(good, "bad"),
    rejected([{text: "", weight: 1}], null), rejected([{text: "   ", weight: 1}], null),
    rejected([{text: "two\nlines", weight: 1}], null),
    rejected([{text: "Word", weight: 0}], null),
    rejected([{text: "Word", weight: -1}], null),
    rejected([{text: "Word", weight: nan}], null),
    rejected([{text: "Word", weight: inf}], null),
    rejected([{text: "Word", weight: "1"}], null),
    rejected(good, {width: 0}), rejected(good, {height: -1}),
    rejected(good, {width: nan}), rejected(good, {padding: -1}),
    rejected(good, {margin: 200}), rejected(good, {min_font_size: 80, max_font_size: 20}),
    rejected(good, {rotations: []}), rejected(good, {rotations: [45]}),
    rejected(good, {colors: []}), rejected(good, {colors: [12]}),
    rejected(good, {step: 0}), rejected(good, {max_steps: 0}),
    rejected(good, {font_weight: "bold"}), rejected(good, {font_family: "serif;display:none"})
]
