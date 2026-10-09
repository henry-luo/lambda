import chart: lambda.chart.chart
import text: lambda.chart.text
import paths: lambda.chart.path_geometry
import geometry: lambda.chart.geometry
import collision: lambda.chart.collision

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn corners(box) => [[box.left, box.top], [box.right, box.top], [box.right, box.bottom], [box.left, box.bottom]]
fn inside(box, point) => point[0] >= box.left and point[0] <= box.right and point[1] >= box.top and point[1] <= box.bottom
fn clear_series(box, points, closed) {
    let rectangle = corners(box);
    let segments = [for (i, point in points where closed or i < len(points) - 1) [point, points[(i + 1) % len(points)]]];
    (not closed or not any(rectangle |> geometry.contains_point(points, ~))) and not any(points |> inside(box, ~)) and
    not any([for (i, a in rectangle) for (segment in segments)
        geometry.intersects(a, rectangle[(i + 1) % 4], segment[0], segment[1])]) and
    min([for (a in rectangle) for (segment in segments) geometry.distance_segment(a, segment[0], segment[1]),
        for (b in points) for (i, a in rectangle) geometry.distance_segment(b, a, rectangle[(i + 1) % 4])]) >= 9.99
}
fn labels_clear(image) {
    let axes = elements(image) |: ~["data-chart-axis"] != null;
    let labels = [for (axis in axes, let texts = content(axis) |: name(~) == 'text') texts[len(texts) - 1]];
    let ticks = [for (axis in axes, let texts = content(axis) |: name(~) == 'text')
        for (tick in slice(texts, 0, len(texts) - 1)) tick];
    let boxes = text.svg_bounds(<g *labels>);
    let tick_boxes = text.svg_bounds(<g *ticks>);
    let series = [for (node in elements(image) where contains(["radar-series", "parallel-series"], node.class)) paths.sample(node.d)[0]];
    all([for (box in boxes) all([for (line in series) clear_series(box, line.points, line.closed)]) and
        not any([for (tick in tick_boxes) collision.overlaps(box, tick, 2.0)])]) and
    all(labels |> ~.transform == null)
}

let data = [{id: "A", speed: 4, quality: 7, cost: 2, reliability: 0.6},
    {id: "B", speed: 7, quality: 4, cost: 6, reliability: 0.9},
    {id: "C", speed: 5, quality: 6, cost: 4, reliability: 0.75}];
let fields = [{field: "speed"}, {field: "quality"}, {field: "cost"}];
let spec = {width: 300, height: 270, padding: 20, title: "Radar field axes", data: data,
    coordinate: {type: "radar"}, mark: {kind: "line", filled: true},
    encoding: {position: fields, color: {field: "id", dtype: "nominal"}, key: {field: "id"}}};
let image = chart.render_spec(spec);
let large = chart.render_spec({*:spec, width: 640, height: 480, mark: {*:spec.mark, label_font_size: 18, label_font_weight: 700},
    encoding: {*:spec.encoding, position: [{field: "speed", title: "Processing speed"}, {field: "quality", title: "Measured quality"},
        {field: "cost", title: "Running cost"}, {field: "reliability", title: "Reliability"}]}});
let rotated = chart.render_spec({*:spec, width: 480, height: 400,
    coordinate: {type: "radar", start_angle: 0.4, end_angle: 0.4 + 6.283185307179586, transform: [{type: "reflect_x"}]}});
let overflow = chart.render_spec({*:spec, width: 480, height: 400, encoding: {*:spec.encoding,
    position: [for (field in fields) {*:field, scale: {domain: [0, 5], nice: false}}]}});
let hidden_labels = chart.render_spec({*:spec, mark: {*:spec.mark, labels: false}});
let hidden_axes = chart.render_spec({*:spec, mark: {*:spec.mark, axes: false}});
let parallel_spec = (input("test/lambda/chart/dashboard_data.json")^.charts |: ~.label == "Parallel field axes")[0].spec;
let parallel = chart.render_spec(parallel_spec);
let parallel_large = chart.render_spec({*:parallel_spec, width: 640, height: 480,
    mark: {*:parallel_spec.mark, label_font_size: 18, label_font_weight: 700},
    data: data, encoding: {*:parallel_spec.encoding, position: [
        {field: "speed", title: "Processing speed"}, {field: "quality", title: "Measured quality"},
        {field: "cost", title: "Running cost"}, {field: "reliability", title: "Reliability"}]}});
let parallel_transformed = [for (transform in [[{type: "transpose"}], [{type: "reflect_y"}], [{type: "rotate", angle: 0.3}]])
    chart.render_spec({*:parallel_spec, width: 480, height: 400, coordinate: {type: "parallel", transform: transform}})];
let parallel_overflow = chart.render_spec({*:parallel_spec, width: 480, height: 400, encoding: {*:parallel_spec.encoding,
    position: [for (field in parallel_spec.encoding.position) {*:field, scale: {domain: [0, 5], nice: false}}]}});
let title = (elements(image) |: name(~) == 'text' and content(~) == [spec.title])[0];
let title_box = text.svg_bounds(title)[0];
let all_boxes = text.svg_bounds(image);
let plot = [for (node in content(image) where name(node) == 'g' and len(elements(node) |: ~.class == "marks coordinate-radar") > 0) node][0];
let checks = {
    three_spokes: labels_clear(image),
    four_spokes_large_font: labels_clear(large),
    rotated_and_reflected: labels_clear(rotated),
    data_outside_domain: labels_clear(overflow),
    parallel_field_titles: labels_clear(parallel),
    parallel_large_font: labels_clear(parallel_large),
    parallel_transformed: all(parallel_transformed |> labels_clear(~)),
    parallel_outside_domain: labels_clear(parallel_overflow),
    chart_title_padding: abs(title_box.top - 20.0) < 0.01,
    chart_title_clear: not any([for (box in text.svg_bounds(plot)) collision.overlaps(box, title_box, 5.0)]),
    viewport_fit: all(all_boxes |> ~.left >= 19.7 and ~.right <= 280.3 and ~.top >= 19.7 and ~.bottom <= 250.3),
    hidden_labels: not any([for (node in elements(hidden_labels) where name(node) == 'text') contains(["speed", "quality", "cost"], node[0])]),
    hidden_axes: len(elements(hidden_axes) |: ~["data-chart-axis"] != null) == 0 and len(elements(hidden_axes) |: ~.class == "radar-series") == 3
};
[for (label, passed in checks where passed != true) string(label)]
