import chart: lambda.chart.chart
import text: lambda.chart.text
import collision: lambda.chart.collision

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn guides(image) => (elements(image) |: ~.class == "coordinate-guides")[0]
fn separated(image) {
    let boxes = text.svg_bounds(guides(image));
    not any([for (i, box in boxes) for (j, other in boxes where j > i) collision.overlaps(box, other, 1.99)])
}
fn tick_text(image, key) {
    let axis = (elements(image) |: ~.class == "axis " ++ key ++ "-axis")[0];
    [for (tick in content(axis) where tick.class == "tick")
        for (label in elements(tick) where name(label) == 'text') join(content(label), "")]
}
fn screen_ticks(image, size = 5.0) {
    let lines = [for (tick in elements(image) where tick.class == "tick") for (line in content(tick) where name(line) == 'line') line];
    len(lines) > 0 and all(lines |> abs(math.sqrt((~.x2 - ~.x1) ** 2 + (~.y2 - ~.y1) ** 2) - size) < 0.001)
}

let spec = (input("test/lambda/chart/dashboard_data.json")^.charts |: ~.label == "Helix coordinate")[0].spec;
let image = chart.render_spec(spec);
let dark = chart.render_spec({*:spec, config: {theme: "dark"}});
let variants = [for (coordinate in [
    {*:spec.coordinate, turns: [0.25, 3.25], start_angle: 0.4},
    {*:spec.coordinate, transform: [{type: "reflect_x"}]},
    {*:spec.coordinate, transform: [{type: "rotate", angle: 0.3}]}])
    chart.render_spec({*:spec, width: 500, height: 450, coordinate: coordinate})];
let large = chart.render_spec({*:spec, width: 640, height: 480,
    encoding: {*:spec.encoding,
        x: {*:spec.encoding.x, title: "Elapsed time", axis: {label_font_size: 18, label_angle: 30, format: ".1f"}},
        y: {*:spec.encoding.y, title: "Measured value", axis: {label_font_size: 18, format: ".1f"}}}});
let custom = chart.render_spec({*:spec, encoding: {*:spec.encoding,
    x: {*:spec.encoding.x, axis: {values: [1, 3, 5], tick_size: 9, domain: false, title_enabled: false}},
    y: {*:spec.encoding.y, axis: {values: [2, 6], tick_size: 9, domain: false, title_enabled: false}}}});
let hidden = chart.render_spec({*:spec, encoding: {*:spec.encoding,
    x: {*:spec.encoding.x, axis: {labels: false, ticks: false}}, y: {*:spec.encoding.y, axis: null}}});
let checks = {
    dashboard_digits_clear: separated(image),
    progression_labels_retained: len(tick_text(image, "x")) >= 4,
    progression_endpoints: "1" in tick_text(image, "x") and "5" in tick_text(image, "x"),
    track_endpoints: "2" in tick_text(image, "y") and "6" in tick_text(image, "y"),
    pixel_tick_size: screen_ticks(image),
    grid_below_labels: max([for (i, node in content(guides(image)) where node.class == "coordinate-grid") i]) <
        min([for (i, node in content(guides(image)) where node.class == "coordinate-axis") i]),
    background_halo: all([for (node in elements(guides(image)) where name(node) == 'text')
        node.stroke == "white" and node["paint-order"] == "stroke fill"]) and
        all([for (node in elements(guides(dark)) where name(node) == 'text') node.stroke == "#333"]) and separated(dark),
    transformed_guides_clear: all(variants |> separated(~) and screen_ticks(~)),
    large_formatted_labels_clear: separated(large),
    custom_tick_geometry: screen_ticks(custom, 9.0) and separated(custom) and
        tick_text(custom, "x") == ["1", "3", "5"] and tick_text(custom, "y") == ["2", "6"] and
        not any([for (node in elements(custom) where node.class == "coordinate-axis")
            any(elements(node) |> name(~) == 'path')]),
    viewport_fit: all(text.svg_bounds(image) |> ~.left >= 19.7 and ~.right <= 280.3 and ~.top >= 19.7 and ~.bottom <= 250.3),
    hidden_guides: len(tick_text(hidden, "x")) == 0 and
        len(elements(hidden) |: ~.class == "axis y-axis") == 0,
    deterministic: chart.render_spec(spec) == image
};
[for (label, passed in checks where passed != true) string(label)]
