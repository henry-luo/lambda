import chart: lambda.chart.chart
import svg: lambda.chart.svg
import paths: lambda.chart.path_geometry
import geometry: lambda.chart.geometry
import text: lambda.chart.text
import collision: lambda.chart.collision

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn words(node) => [for (child in elements(node) where name(child) == 'text') join(content(child), "")]
fn clear_text(node) {
    let boxes = text.svg_bounds(node);
    not any([for (i, box in boxes) for (j, other in boxes where j > i) collision.overlaps(box, other, 1.99)])
}
fn needle_tracks(image, fraction) {
    let pointer = classes(image, "gauge-pointer")[0];
    let hub = (elements(pointer) |: name(~) == 'circle')[0];
    let outline = paths.sample((elements(pointer) |: name(~) == 'path')[0].d)[0].points;
    let tip = (sort(outline, (point) => 0.0 - paths.distance(point, [hub.cx, hub.cy])))[0];
    let length = paths.distance(tip, [hub.cx, hub.cy]);
    let angle = -3.141592653589793 * (1.0 - fraction);
    length > 0.0 and abs((tip[0] - hub.cx) / length - math.cos(angle)) < 0.0001 and
        abs((tip[1] - hub.cy) / length - math.sin(angle)) < 0.0001
}

let spec = (input("test/lambda/chart/dashboard_data.json")^.charts |: ~.label == "Target gauge")[0].spec;
let image = chart.render_spec(spec);
let changed = chart.render_spec({*:spec, data: [{value: 43}], mark: {*:spec.mark, target: 61}});
let zero = chart.render_spec({*:spec, data: [{value: 0}]});
let hidden = chart.render_spec({*:spec, mark: {*:spec.mark, pointer: false, labels: false, ticks: false}});
let sparse = chart.render_spec({*:spec, mark: {*:spec.mark, tick_count: 4.0, minor_tick_count: 1.0}});
let clockwise = paths.sample(svg.arc_path(0, 0, 20, 30, 0, -3.141592653589793, true))[0].points;
let full = paths.sample(svg.arc_path(0, 0, 20, 30, 0, -6.283185307179586, true));
let checks = {
    numeric_scale: words(classes(image, "gauge-scale")[0]) == ["0", "25", "50", "75", "100"],
    scale_labels_clear: clear_text(classes(image, "gauge-scale")[0]),
    readout_clear: clear_text(classes(image, "indicator-readout")[0]),
    datum_drives_needle: needle_tracks(image, 0.72) and needle_tracks(changed, 0.43),
    datum_drives_readout: words(classes(changed, "indicator-readout")[0]) == ["43%", "Target 61%"],
    gradient_paint_resolves: starts_with(classes(image, "gauge-value")[0].fill, "url(#") and
        len(elements(image) |: name(~) == 'linearGradient') > 0,
    zero_has_no_progress_cap: classes(zero, "gauge-value")[0].d == "",
    clockwise_caps: min(clockwise |> ~[1]) < -29.9 and max(clockwise |> ~[1]) <= 5.01 and geometry.simple_ring(clockwise),
    full_circle_keeps_hole: len(full) == 2 and all(full |> ~.closed) and
        geometry.area(full[0].points) * geometry.area(full[1].points) < 0.0,
    decoration_controls: len(classes(hidden, "gauge-pointer")) == 0 and len(classes(hidden, "gauge-scale")) == 0 and
        len(classes(hidden, "indicator-readout")) == 0,
    integral_tick_counts: words(classes(sparse, "gauge-scale")[0]) == ["0", "25", "50", "75", "100"],
    invalid_tick_counts: chart.render_spec({*:spec, mark: {*:spec.mark, tick_count: 0}}) is error and
        chart.render_spec({*:spec, mark: {*:spec.mark, minor_tick_count: 1.5}}) is error,
    invalid_hub: chart.render_spec({*:spec, mark: {*:spec.mark, pointer_hub_radius: -1}}) is error,
    deterministic: chart.render_spec(spec) == image
};
[for (label, passed in checks where passed != true) string(label)]
