import cloud: lambda.chart.wordcloud

fn rejected(words, opts = null) => (cloud.layout(words, opts) ^ { null }) == null

let good = [{text: "Word", weight: 1}];
[
    rejected(good, {shape: "heart"}), rejected(good, {shape: 42}),
    rejected(good, {spiral: "random"}), rejected(good, {size_scale: "power"}),
    rejected(good, {seed: -1}), rejected(good, {seed: 2147483647}),
    rejected(good, {seed: 1.5}), rejected(good, {seed: "1"}),
    rejected(good, {rotations: [181]}), rejected(good, {rotations: [-181]}),
    rejected(good, {rotations: [inf]}), rejected(good, {rotations: ["45"]}),
    rejected([{text: "Word", weight: 1, rotation: nan}]),
    rejected([{text: "Word", weight: 1, rotation: "45"}]),
    rejected([{text: "Word", weight: 1, font_size: 0}]),
    rejected([{text: "Word", weight: 1, font_size: -1}]),
    rejected([{text: "Word", weight: 1, font_size: inf}]),
    rejected([{text: "Word", weight: 1, font_family: " "}]),
    rejected([{text: "Word", weight: 1, font_family: "serif;display:none"}]),
    rejected([{text: "Word", weight: 1, font_weight: 99}]),
    rejected([{text: "Word", weight: 1, font_weight: "bold"}]),
    rejected([], {shape: "heart"}), rejected([], {seed: -1})
]
