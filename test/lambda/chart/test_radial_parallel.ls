import chart: lambda.chart.chart
import scale: lambda.chart.scale

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn tags(node, tag) => elements(node) |: name(~) == tag
fn classes(node, class_name) => elements(node) |: ~.class == class_name
let data = [for (index, category in ["a", "b", "c", "d"]) {category: category, score: index + 1}];
let base = {width: 200, height: 200, padding: 0, data: data, mark: {kind: "radar", labels: false, grid: false}, encoding: {
    theta: {field: "category", dtype: "nominal", scale: {domain: ["a", "b", "c", "d"]}}, radius: {field: "score", dtype: "quantitative", scale: {domain: [0, 4], range: [0, 80]}}}};
let radar = chart.render_spec(base);
let reverse_angle = chart.render_spec({*:base, encoding: {*:base.encoding, theta: {*:base.encoding.theta, scale: {reverse: true}}}});
let reverse_radius = chart.render_spec({*:base, encoding: {*:base.encoding, radius: {*:base.encoding.radius,
    scale: {*:base.encoding.radius.scale, reverse: true}}}});
let grouped = chart.render_spec({*:base, data: [for (group in ["one", "two"]) for (row in data) {*:row, group: group}],
    mark: {*:base.mark, filled: true}, encoding: {*:base.encoding, color: {field: "group", dtype: "nominal", legend: null}, tooltip: {field: "group"}}});
let markup = chart.render(<chart width: 300, height: 300, padding: 0,
    <data values: data> <mark type: "radar">
    <encoding <theta field: "category", dtype: "nominal"> <radius field: "score", dtype: "quantitative">>>);
let small = {*:base, encoding: {*:base.encoding, radius: {field: "score", dtype: "quantitative", scale: {nice: false}}}};
let large = {*:small, data: [for (row in data) {*:row, score: row.score * 10}]};
let shared = chart.render_spec({concat: "horizontal", resolve: {scale: {radius: "shared"}}, children: [small, large]});
let independent = chart.render_spec({concat: "horizontal", children: [small, large]});
let parallel_base = {width: 200, height: 100, padding: 0, data: [{a: 0, b: 100, c: 0.5}, {a: 10, b: 0, c: 1}],
    mark: {kind: "parallel", fields: ["a", "b", {field: "c", scale: {domain: [0, 1], nice: false}}], labels: false, axes: false}};
let parallel = chart.render_spec(parallel_base);
let parallel_reverse = chart.render_spec({*:parallel_base, mark: {*:parallel_base.mark,
    fields: ["a", {field: "b", scale: {reverse: true}}, {field: "c", scale: {domain: [0, 1], nice: false}}]}});
let parallel_guides = chart.render_spec({*:parallel_base, width: 400, height: 250,
    mark: {kind: "parallel", fields: ["a", {field: "b", title: "Second", format: ".0f"}, "c"]}});
let colored = chart.render_spec({*:parallel_base, data: [{a: 0, b: 100, c: 0.5, group: "one"}, {a: 10, b: 0, c: 1, group: "two"}],
    encoding: {color: {field: "group", dtype: "nominal", legend: null}, tooltip: {field: "group"}}});
let missing = chart.render_spec({*:parallel_base, data: [*parallel_base.data, {a: 999, b: 5}]});
let arc_spec = {width: 200, height: 200, padding: 0, data: [{start: 0, end: 1, outer: 80, inner: 40}], mark: {kind: "arc"}, encoding: {
    theta: {field: "start", dtype: "quantitative", scale: null}, theta2: {field: "end"},
    radius: {field: "outer", dtype: "quantitative", scale: null}, radius2: {field: "inner"}}};
let ranged = chart.render_spec(arc_spec);
let layered_arc = chart.render_spec({width: 200, height: 200, padding: 0, layer: [arc_spec]});
let checks = {
    radar_path: classes(radar, "radar-series")[0].d == "M100 80 L140 100 L100 160 L20 100 Z",
    radar_no_guides: len(tags(radar, 'circle')) == 0 and len(tags(radar, 'text')) == 0,
    angular_reverse: starts_with(classes(reverse_angle, "radar-series")[0].d, "M80 100"),
    radial_reverse: starts_with(classes(reverse_radius, "radar-series")[0].d, "M100 40"),
    closed_groups: len(classes(grouped, "radar-series")) == 2 and ends_with(classes(grouped, "radar-series")[1].d, " Z"),
    radar_fill: classes(grouped, "radar-series")[0].fill == "#4e79a7" and classes(grouped, "radar-series")[1].stroke == "#f28e2b",
    radar_tooltips: tags(grouped, 'title')[0][0] == "one" and tags(grouped, 'title')[1][0] == "two",
    radial_markup: len(classes(markup, "radar-grid")) == 1 and len(tags(markup, 'line')) == 4 and len(tags(markup, 'text')) >= 4,
    shared_radius: starts_with(classes(shared, "radar-series")[0].d, "M100 97.65") and starts_with(classes(shared, "radar-series")[1].d, "M100 76.5"),
    independent_radius: classes(independent, "radar-series")[0].d == classes(independent, "radar-series")[1].d,
    parallel_paths: len(classes(parallel, "parallel-series")) == 2,
    independent_axes: classes(parallel, "parallel-series")[0].d == "M0 100 L100 0 L200 50",
    parallel_reverse: classes(parallel_reverse, "parallel-series")[0].d == "M0 100 L100 100 L200 50",
    parallel_guides: len(classes(parallel_guides, "axis y-axis")) == 3 and tags(parallel_guides, 'text')[len(tags(parallel_guides, 'text')) - 2][0] == "Second",
    parallel_color: classes(colored, "parallel-series")[0].stroke == "#4e79a7" and classes(colored, "parallel-series")[1].stroke == "#f28e2b",
    parallel_tooltip: tags(colored, 'title')[0][0] == "one",
    missing_rows: len(classes(missing, "parallel-series")) == 2 and classes(missing, "parallel-series")[0].d == classes(parallel, "parallel-series")[0].d,
    empty_radar: chart.render_spec({*:base, data: []}) is element,
    empty_parallel: chart.render_spec({*:parallel_base, data: []}) is element,
    missing_category: chart.render_spec({*:base, data: slice(data, 0, 3)}) is error,
    duplicate_category: chart.render_spec({*:base, data: [*data, data[0]]}) is error,
    bad_radius: chart.render_spec({*:base, data: [*slice(data, 0, 3), {category: "d", score: "bad"}]}) is error,
    too_few_axes: chart.render_spec({*:parallel_base, mark: {kind: "parallel", fields: ["a"]}}) is error,
    bad_field_definition: chart.render_spec({*:parallel_base, mark: {kind: "parallel", fields: ["a", 1]}}) is error,
    bad_angular_range: scale.angular_scale({field: "category", dtype: "nominal", scale: {range: [0]}}, data) is error,
    ranged_arc: contains(tags(ranged, 'path')[0].d, "A80 80") and contains(tags(ranged, 'path')[0].d, "A40 40"),
    layered_arc: len(classes(layered_arc, "marks arcs")) == 1 and tags(layered_arc, 'path')[0].d == tags(ranged, 'path')[0].d,
    actual_inner_radius: contains(tags(chart.render_spec({width: 200, height: 200, padding: 0, data: [{v: 1}],
        mark: {kind: "arc", inner_radius: 80}, encoding: {theta: {field: "v", dtype: "quantitative"}}}), 'path')[0].d, "A80 80"),
    bad_arc_radius: chart.render_spec({*:arc_spec, mark: {kind: "arc", inner_radius: 120}}) is error,
    radial_pie: contains(tags(chart.render_spec({*:arc_spec, data: [{start: 1, outer: 80, inner: 40}],
        encoding: {*:arc_spec.encoding, theta2: null}}), 'path')[0].d, "A40 40"),
    zero_pie: len(tags(chart.render_spec({width: 200, height: 200, padding: 0, data: [{v: 0}], mark: {kind: "arc"},
        encoding: {theta: {field: "v", dtype: "quantitative"}}}), 'path')) == 0,
    negative_pie: chart.render_spec({width: 200, height: 200, padding: 0, data: [{v: -1}], mark: {kind: "arc"},
        encoding: {theta: {field: "v", dtype: "quantitative"}}}) is error
};
[for (label, passed in checks where passed != true) string(label)]
