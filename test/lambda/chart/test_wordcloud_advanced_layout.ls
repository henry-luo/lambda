import cloud: lambda.chart.wordcloud

// Independent corner projections verify the closed-form collision implementation.
fn corners(word, padding = 0) {
    let c = math.cos(word.rotation * math.pi / 180.0);
    let s = math.sin(word.rotation * math.pi / 180.0);
    [for (sx in [-1, 1], sy in [-1, 1]) {
        x: word.x + (sx * (word.text_width + padding) * c - sy * (word.text_height + padding) * s) / 2.0,
        y: word.y + (sx * (word.text_width + padding) * s + sy * (word.text_height + padding) * c) / 2.0
    }]
}

fn overlapping(a, b, padding) {
    let ac = corners(a, padding);
    let bc = corners(b, padding);
    let separating = [for (r in [a.rotation, b.rotation], offset in [0, 90]) (
        let nx = math.cos((r + offset) * math.pi / 180.0),
        let ny = math.sin((r + offset) * math.pi / 180.0),
        let ap = [for (p in ac) p.x * nx + p.y * ny],
        let bp = [for (p in bc) p.x * nx + p.y * ny],
        max(ap) <= min(bp) + 0.000001 or max(bp) <= min(ap) + 0.000001
    )];
    not contains(separating, true)
}

fn all_inside(result) {
    let rx = (if (result.shape == "circle") min([result.width, result.height]) else result.width) / 2.0 - result.margin;
    let ry = (if (result.shape == "circle") min([result.width, result.height]) else result.height) / 2.0 - result.margin;
    let outside = [for (word in result.words, p in corners(word)) (
        let x = abs(p.x - result.width / 2.0) / rx,
        let y = abs(p.y - result.height / 2.0) / ry,
        if (result.shape == "rectangle") x > 1.000001 or y > 1.000001
        else if (result.shape == "diamond") x + y > 1.000001
        else x * x + y * y > 1.000001
    )];
    not contains(outside, true)
}

fn no_overlap(result) => len([for (i, a in result.words, j, b in result.words
    where i < j and overlapping(a, b, result.padding)) true]) == 0

fn by_text(result, text) => [for (word in result.words where word.text == text) word][0]

let words = [{text: "Lambda", weight: 100}, {text: "Data", weight: 70},
    {text: "Types", weight: 50}, {text: "Code", weight: 40},
    {text: "SVG", weight: 30}, {text: "Maps", weight: 20},
    {text: "Pure", weight: 10}, {text: "JIT", weight: 5}]
let opts = {width: 600, height: 400, max_font_size: 40, padding: 4,
    rotations: [-45, 30, 15, 120, 180, -180], seed: 17}
let shaped = [for (shape in ["rectangle", "ellipse", "circle", "diamond"])
    cloud.layout(words, {*:opts, shape: shape})^]
let repeated = cloud.layout(words, opts)^
let reseeded = cloud.layout(words, {*:opts, seed: 18})^
let rectangular = cloud.layout(words, {*:opts, spiral: "rectangular", step: 8})^
let parallel = cloud.layout([{text: "Diagonal", weight: 1}, {text: "Diagonal", weight: 1}],
    {rotations: [45], min_font_size: 32, max_font_size: 32, padding: 3})^
let parallel_a = parallel.words[0]
let parallel_b = parallel.words[1]
let sizes = [for (size_scale in ["linear", "sqrt", "log"])
    cloud.layout([{text: "Low", weight: 1}, {text: "Middle", weight: 25}, {text: "High", weight: 100}],
        {size_scale: size_scale, min_font_size: 12, max_font_size: 64})^]
let styled = cloud.layout([
    {text: "WWW", weight: 2, font_size: 48, rotation: -22.5, font_family: "serif", font_weight: 700, custom: "retained"},
    {text: "WWW", weight: 1, font_size: 24, rotation: 180}
], {rotations: [30], font_family: "monospace"})^
let large_in_circle = cloud.layout([{text: "MMMM", weight: 1}],
    {width: 800, height: 140, min_font_size: 64, max_font_size: 64, shape: "circle"})^
let same_in_rectangle = cloud.layout([{text: "MMMM", weight: 1}],
    {width: 800, height: 140, min_font_size: 64, max_font_size: 64})^
// Derive the circle from measured fonts so this containment test is platform independent.
let angled_word = [{text: "Diagonal", weight: 1, rotation: 45, font_size: 42}]
let reference = cloud.layout(angled_word)^
let box = reference.words[0]
let inner_radius = math.sqrt(box.text_width * box.text_width + box.text_height * box.text_height) / 2.0
let outer_radius = math.sqrt(box.width * box.width + box.height * box.height) / 2.0
let diameter = inner_radius + outer_radius + 16.0
let tight_circle = cloud.layout(angled_word, {shape: "circle", width: diameter, height: diameter})^
// A ring's corners can leave a thin viewport before its horizontal midpoint fits.
let wide_word = [{text: "MMMMMMMMMM", weight: 2, font_size: 40}]
let wide_reference = cloud.layout(wide_word)^
let wide_box = wide_reference.words[0]
let thin = cloud.layout([*wide_word, {text: ".", weight: 1, font_size: 4}],
    {width: wide_box.width * 1.3 + 16.0, height: wide_box.height + 16.0,
        spiral: "rectangular", step: 8, max_steps: 20000})^
let first_only = cloud.layout(words, {*:opts, spiral: "rectangular", max_steps: 1})^;
[
    not contains([for (result in shaped) len(result.words) == len(words)], false),
    not contains([for (result in shaped) all_inside(result)], false),
    not contains([for (result in shaped) no_overlap(result)], false),
    shaped[0] == repeated, repeated != reseeded,
    len(rectangular.words) == len(words), all_inside(rectangular), no_overlap(rectangular),
    rectangular == cloud.layout(words, {*:opts, spiral: "rectangular", step: 8})^,
    len(parallel.words) == 2, no_overlap(parallel),
    abs(parallel_a.x - parallel_b.x) < (parallel_a.width + parallel_b.width) / 2.0 and
        abs(parallel_a.y - parallel_b.y) < (parallel_a.height + parallel_b.height) / 2.0,
    abs(by_text(sizes[0], "Middle").font_size - (12.0 + 52.0 * 24.0 / 99.0)) < 0.000001,
    by_text(sizes[0], "Middle").font_size < by_text(sizes[1], "Middle").font_size,
    by_text(sizes[1], "Middle").font_size < by_text(sizes[2], "Middle").font_size,
    not contains([for (result in sizes) by_text(result, "Low").font_size == 12 and
        by_text(result, "High").font_size == 64], false),
    styled.words[0].font_size == 48 and styled.words[1].font_size == 24,
    styled.words[0].text_width > styled.words[1].text_width,
    styled.words[0].rotation == -22.5 and styled.words[1].rotation == 180,
    styled.words[0].font_family == "serif" and styled.words[1].font_family == "monospace",
    styled.words[0].font_weight == 700 and styled.words[0].custom == "retained",
    len(large_in_circle.words) == 0 and large_in_circle.unplaced[0].reason == "too_large",
    len(same_in_rectangle.words) == 1,
    len(tight_circle.words) == 1 and all_inside(tight_circle),
    outer_radius > diameter / 2.0 - 8.0,
    len(thin.words) == 2 and all_inside(thin) and no_overlap(thin),
    len(first_only.words) == 1 and len(first_only.unplaced) == len(words) - 1
]
