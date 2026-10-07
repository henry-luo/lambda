// View with: ./lambda.exe view test/demo/wordcloud.ls
import cloud: lambda.chart.wordcloud

let words = [
    {text: "Lambda", weight: 100}, {text: "Documents", weight: 65},
    {text: "Functional", weight: 55}, {text: "Charts", weight: 45},
    {text: "Data", weight: 40}, {text: "SVG", weight: 32},
    {text: "Types", weight: 28}, {text: "Layout", weight: 24},
    {text: "Packages", weight: 21}, {text: "Unicode", weight: 18},
    {text: "café", weight: 15}, {text: "中文", weight: 12},
    {text: "Pure", weight: 10}, {text: "JIT", weight: 8},
    {text: "Maps", weight: 6}, {text: "Arrays", weight: 4}
]
let base = {width: 540, height: 340, margin: 16, padding: 3,
    min_font_size: 14, max_font_size: 54,
    colors: ["#315c90", "#c4623d", "#388278", "#8a5882", "#9d7935"]}
let examples = [
    {title: "Ellipse · angled words", caption: "Rotated text rectangles pack inside an ellipse.",
        opts: {*:base, shape: "ellipse", rotations: [-30, 0, 30], seed: 17}},
    {title: "Circle · rectangular spiral", caption: "A square spiral fills a circular boundary.",
        opts: {*:base, shape: "circle", spiral: "rectangular", step: 6,
            max_steps: 10000, max_font_size: 40, rotations: [0, 90], seed: 9}},
    {title: "Diamond · logarithmic sizing", caption: "Log scaling gives smaller weights more visual space.",
        opts: {*:base, shape: "diamond", size_scale: "log", max_font_size: 40, rotations: [0], seed: 23}},
    {title: "Rectangle · individual styles", caption: "The leading word overrides font, size, angle and color.",
        opts: {*:base, size_scale: "linear", rotations: [0, 0, 90], seed: 42}}
]

fn card(example, index) element^ {
    let data = if (index == 3) [
        {*:words[0], font_family: "serif", font_weight: 700, font_size: 62,
            rotation: -12, color: "#3b4970"},
        for (i, word in words where i > 0) word
    ] else words;
    <section class: "card",
        <h2 example.title>
        <p example.caption>
        cloud.render(data, example.opts)^>
}

<html
    <head
        <meta charset: "utf-8">
        <title "Lambda word cloud gallery">
        <style "
            html,body { margin:0; background:#f3f5f8; color:#24334b; font-family:sans-serif; }
            main { padding:32px; max-width:1160px; margin:0 auto; }
            h1 { margin:0 0 8px; font-size:32px; }
            .intro { margin:0 0 28px; color:#536176; }
            .gallery { display:grid; grid-template-columns:1fr 1fr; gap:20px; }
            .card { background:white; border:1px solid #dfe5ee; border-radius:12px; padding:16px; }
            h2 { margin:0 0 8px; font-size:18px; }
            p { margin:0 0 12px; font-size:14px; color:#647287; }
            svg { display:block; width:100%; height:auto; }
        ">>
    <body <main
        <h1 "Word clouds, shaped and styled">
        <p class: "intro", "Deterministic layouts with measured fonts, arbitrary rotations and selectable boundaries.">
        <div class: "gallery", for (i, example in examples) card(example, i)^>>>>
