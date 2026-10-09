import chart: lambda.chart.chart
import animation: lambda.chart.animation
import text: lambda.chart.text
import collision: lambda.chart.collision

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn titles(image, label) => elements(image) |: name(~) == 'text' and content(~) == [label]
fn axis(image, key) => (elements(image) |: ~.class == "axis " ++ key ++ "-axis")[0]
fn tick_count(image) => len(content(axis(image, "y")) |: ~.class == "tick")
fn near(a, b) => abs(a - b) < 0.0001
fn single_titles(image) => len(titles(image, "value")) == 1 and len(titles(image, "category")) == 1
fn readable_ticks(image) => all([for (tick in elements(image) where tick.class == "tick")
    len(elements(tick) |: name(~) == 'text') <= 1]);
fn spaced_ticks(image, key) {
    let boxes = text.svg_bounds(<g *[for (tick in content(axis(image, key)) where tick.class == "tick") tick]>);
    not any([for (i, box in boxes) for (j, other in boxes where j > i) collision.overlaps(box, other, 1.99)])
}

let rows = [for (i, value in [3, 6, 4, 5, 2]) {id: string(i), category: ["A", "B", "C", "D", "E"][i], value: value}];
let spec = {width: 300, height: 270, padding: 20, data: rows, mark: {kind: "bar"},
    encoding: {x: {field: "category", dtype: "nominal"}, y: {field: "value", dtype: "quantitative"}, key: {field: "id"}}};
let point = {*:spec, mark: {kind: "point", size: 100}};
let source = chart.render_spec(spec);
let target = chart.render_spec(point);
let story = {*:spec, timeline: {keyframes: [{at: 0, spec: {}}, {at: 1000, spec: {mark: point.mark}}]}};
let reverse = {*:point, timeline: {keyframes: [{at: 0, spec: {}}, {at: 1000, spec: {mark: spec.mark}}]}};
let frames = [for (clock_ms in [0, 250, 500, 750, 1000]) chart.render_frame(story, clock_ms)];
let halfway = frames[2];
let changed = chart.render_frame({*:point, encoding: {*:point.encoding, y: {*:point.encoding.y, title: "score"}}},
    {time_ms: 150, previous: spec});
let no_domain = chart.render_frame({*:point, encoding: {*:point.encoding, y: {*:point.encoding.y, axis: {domain: false}}}},
    {time_ms: 150, previous: spec});
let no_x = chart.render_frame({*:point, encoding: {*:point.encoding, x: {*:point.encoding.x, axis: null}}},
    {time_ms: 150, previous: spec});
let dashboard = input("test/lambda/chart/dashboard_data.json")^;
let sample = (dashboard.charts |: ~.label == "Keyed transition sample")[0];
let keyed_frames = [for (clock_ms in [0, 250, 499, 500, 750, 1000])
    chart.render_frame(sample.spec, {time_ms: clock_ms, previous: sample.previous})];
let morph_sample = (dashboard.charts |: ~.label == "Keyframe morph sample")[0];
// cover the dashboard's exact keyframes on both sides of the text switch.
let morph_frames = [for (clock_ms in [0, 250, 499, 500, 501, 750, 1000])
    chart.render_frame(morph_sample.spec, {time_ms: clock_ms, previous: morph_sample.previous})];
let no_tick_lines = chart.render_frame({*:point, encoding: {*:point.encoding, y: {*:point.encoding.y, axis: {ticks: false}}}},
    {time_ms: 150, previous: spec});
let transposed_frame = chart.render_frame({*:story, coordinate: {transform: [{type: "transpose"}]}}, 500);
let polar_frame = chart.render_frame({*:story, coordinate: {type: "polar"}}, 500);
let custom = <g class: "tick", <text x: 10, y: 20,
    'data-chart-animation': format({update: {type: "morph", delay: 0, duration: 1000, boundary: 0.75}}, 'json'), "$2.00">>;
let custom_source = <g class: "tick", <text x: 0, y: 10, "$0.60">>;
let before_switch = animation.sample(custom_source, custom, 600);
let after_switch = animation.sample(custom_source, custom, 800);
let checks = {
    different_tick_counts: tick_count(source) != tick_count(target),
    one_title_at_every_frame: all(frames |> single_titles(~)),
    reverse_tick_count_change: all([for (clock_ms in [250, 500, 750], let image = chart.render_frame(reverse, clock_ms))
        single_titles(image) and readable_ticks(image)]),
    title_position_interpolates: near(titles(halfway, "value")[0].x, (titles(source, "value")[0].x + titles(target, "value")[0].x) / 2.0),
    title_never_paired_with_tick: not any([for (node in elements(axis(halfway, "y")) where node.class == "chart-crossfade")
        len(titles(node, "value")) > 0]),
    changed_title_crossfades: len(titles(changed, "value")) == 1 and len(titles(changed, "score")) == 1 and
        any([for (node in elements(changed) where node.class == "chart-crossfade")
            len(titles(node, "value")) == 1 and len(titles(node, "score")) == 1]),
    domain_visibility_keeps_title: single_titles(no_domain),
    axis_visibility_keeps_channel: len(titles(no_x, "category")) == 0 and len(titles(no_x, "value")) == 1 and
        len(elements(no_x) |: ~.class == "axis y-axis") == 1,
    direct_svg_sampling: single_titles(animation.sample(source, target, 150)),
    one_tick_label_per_frame: all(frames |> readable_ticks(~)) and all(keyed_frames |> readable_ticks(~)),
    tick_labels_do_not_overlap: all(keyed_frames |> spaced_ticks(~, "y") and spaced_ticks(~, "x")),
    dashboard_morph_tick_labels: all(morph_frames |> readable_ticks(~) and single_titles(~) and
        spaced_ticks(~, "y") and spaced_ticks(~, "x")),
    projected_tick_spacing: all([for (image in [transposed_frame, polar_frame])
        readable_ticks(image) and spaced_ticks(image, "x") and spaced_ticks(image, "y")]),
    tick_line_visibility_keeps_label: readable_ticks(no_tick_lines) and single_titles(no_tick_lines),
    declared_text_boundary: content((elements(before_switch) |: name(~) == 'text')[0]) == ["$0.60"] and
        content((elements(after_switch) |: name(~) == 'text')[0]) == ["$2.00"],
    tick_geometry_still_interpolates: near((elements(before_switch) |: name(~) == 'text')[0].x, 6.0) and
        near((elements(before_switch) |: name(~) == 'text')[0].y, 16.0),
    deterministic: chart.render_frame(story, 500) == halfway
};
[for (label, passed in checks where passed != true) string(label)]
