import chart: lambda.chart.chart
import coordinate: lambda.chart.coordinate
import paths: lambda.chart.path_geometry
import util: lambda.chart.util
import affine: lambda.chart.svg_transform
import text: lambda.chart.text
fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn near(a, b) => abs(a - b) < 0.000001
fn close(a, b) => near(a[0], b[0]) and near(a[1], b[1])
fn title_direction(image, label) {
    let titles = elements(image) |: name(~) == 'text' and content(~) == [label];
    let matrix = affine.matrix(titles[0].transform);
    [matrix[0], matrix[1]]
}
let cartesian = coordinate.configure({type: "cartesian"}, 300, 200);
let polar = coordinate.configure({type: "polar", inner_radius: 0.2, outer_radius: 0.8}, 300, 200);
let transforms = [{type: "transpose"}, {type: "reflect_x"}, {type: "rotate", angle: 0.3}, {type: "scale", factors: [0.8, 1.2]}, {type: "translate", offset: [0.1, -0.2]}, {type: "fisheye", focus: [0.4, 0.7], distortion: 2.0}];
let transformed = coordinate.configure({type: "cartesian", transform: transforms}, 300, 200);
let curve = paths.sample("M0 0 C0 100 100 100 100 0 Z");
let arc = paths.sample("M10 0 A10 10 0 1 1 -10 0 A10 10 0 1 1 10 0 Z");
let spec = {width: 300, height: 240, padding: 20, data: [{a: "A", value: 2}, {a: "B", value: 4}, {a: "C", value: 3}],
    mark: {kind: "bar"}, encoding: {x: {field: "a", dtype: "nominal", title: "category"}, y: {field: "value", dtype: "quantitative"}}};
let image = chart.render_spec({*:spec, coordinate: {type: "polar"}});
let circular_titles = [for (kind in ["theta", "radial"]) chart.render_spec({*:spec,
    coordinate: {type: kind, inner_radius: 0.15}, encoding: {x: spec.encoding.y, y: spec.encoding.x}})];
let transposed = chart.render_spec({*:spec, coordinate: {transform: [{type: "transpose"}]},
    encoding: {*:spec.encoding, x: {*:spec.encoding.x, axis: {label_angle: 45}}}});
let restored = chart.render_spec({*:spec, coordinate: {transform: [{type: "transpose"}, {type: "transpose"}, {type: "reflect_x"}]}});
let letter_title = chart.render_spec({*:spec, coordinate: {transform: [{type: "transpose"}]},
    encoding: {*:spec.encoding, x: {*:spec.encoding.x, title: "x"}}});
let transposed_ticks = [for (tick in classes(transposed, "tick")) for (child in content(tick) where name(child) == 'text') child];
let markup = chart.render(<chart width: 300, height: 240, padding: 20, <data values: spec.data> <mark type: "bar">
    <coordinate type: "polar"> <encoding <x field: "a", dtype: "nominal", title: "category"> <y field: "value", dtype: "quantitative">>>);
let vector_spec = {width: 300, height: 300, padding: 25, data: [{a: 1, b: 3, c: 5}, {a: 2, b: 2, c: 4}], mark: {kind: "line"},
    encoding: {position: [{field: "a"}, {field: "b"}, {field: "c"}]}};
let checks = {
    cartesian: close(coordinate.project(cartesian, [0.2, 0.3]), [60, 140]),
    polar: close(coordinate.project(polar, [0.25, 0.5]), [200, 100]),
    polar_inverse: close(coordinate.invert(polar, coordinate.project(polar, [0.3, 0.7])), [0.3, 0.7]),
    cartesian_inverse: close(coordinate.invert(transformed, coordinate.project(transformed, [0.2, 0.3])), [0.2, 0.3]),
    ordered_transforms: coordinate.project(transformed, [0.2, 0.3]) != coordinate.project(coordinate.configure({transform: reverse(transforms)}, 300, 200), [0.2, 0.3]),
    theta_transpose: close(coordinate.project(coordinate.configure({type: "theta"}, 200, 200), [0.5, 0.25]), [150, 100]),
    curve_closed: len(curve) == 1 and curve[0].closed == true and len(curve[0].points) > 4,
    curve_endpoints: curve[0].points[0] == [0.0, 0.0] and curve[0].points[len(curve[0].points) - 1] == [0.0, 0.0],
    redundant_close: paths.sample("M0 0 L10 0 L0 10 L0 0 Z")[0] ==
        {points: [[0.0, 0.0], [10.0, 0.0], [0.0, 10.0], [0.0, 0.0]], closed: true},
    arc_radius: all([for (point in arc[0].points) near(point[0] ** 2 + point[1] ** 2, 100.0)]),
    relative_path: paths.parse("m10 20 l5 -5 h10 v10 z")[4].end == [10.0, 20.0],
    smooth_path: len(paths.sample("M0 0 Q10 20 20 0 T40 0 C45 0 45 20 50 20 S60 0 70 0")) == 1,
    subpaths: len(paths.sample("M0 0 L5 5 M10 10 L20 20 Z")) == 2,
    projected_bar_boundaries: len((elements(image) |: name(~) == 'path' and ~.width != null)) == 3,
    coordinate_guides: len(classes(image, "coordinate-axis")) == 2 and len(classes(image, "coordinate-grid")) > 2,
    circular_category_title: all([for (circular in circular_titles)
        len(elements(circular) |: name(~) == 'text' and content(~) == ["category"]) == 1 and
        close(title_direction(circular, "category"), [1.0, 0.0])]),
    circular_category_spacing: all([for (circular in circular_titles,
        let boxes = text.svg_bounds(classes(circular, "axis y-axis")[0]))
        boxes[len(boxes) - 1].top >= max(slice(boxes, 0, len(boxes) - 1) |> ~.bottom) + 9.99]),
    transposed_value_title: close(title_direction(transposed, "value"), [1.0, 0.0]),
    transposed_category_title: close(title_direction(transposed, "category"), [0.0, -1.0]),
    transposed_letter_title: close(title_direction(letter_title, "x"), [1.0, 0.0]),
    composed_title_orientation: close(title_direction(restored, "value"), [0.0, -1.0]) and close(title_direction(restored, "category"), [1.0, 0.0]),
    tick_label_angle_preserved: len([for (tick in transposed_ticks, let matrix = affine.matrix(tick.transform)
        where near(matrix[0], math.sqrt(0.5)) and near(matrix[1], math.sqrt(0.5))) tick]) == 3,
    markup_parity: image == markup,
    family_rendering: all([for (kind in ["cartesian", "polar", "theta", "radial", "helix"])
        chart.render_spec({*:spec, coordinate: {type: kind}}) is element]),
    coordinate_layer: chart.render_spec({width: 300, height: 240, padding: 20, layer: [spec, {*:spec, mark: {kind: "point"}, coordinate: {type: "polar"}}]}) is element,
    incompatible_layers: chart.render_spec({width: 300, height: 240, layer: [{*:spec, coordinate: {type: "polar"}}, {*:spec, coordinate: {type: "cartesian"}}]}) is error,
    parallel_vectors: chart.render_spec({*:vector_spec, coordinate: {type: "parallel"}}) is element,
    radar_vectors: len(classes(chart.render_spec({*:vector_spec, coordinate: {type: "radar"}}), "radar-series")) == 2,
    incompatible_layout: chart.render_spec({*:spec, mark: {kind: "wordcloud"}, coordinate: {type: "polar"}}) is error,
    bad_coordinate: coordinate.configure({type: "unknown"}, 200, 200) is error,
    bad_transform: coordinate.configure({transform: [{type: "unknown"}]}, 200, 200) is error,
    bad_radii: coordinate.configure({type: "polar", inner_radius: 0.8, outer_radius: 0.2}, 200, 200) is error,
    bad_helix: coordinate.configure({type: "helix", turns: [3, 1]}, 200, 200) is error,
    singular_inverse: coordinate.invert(coordinate.configure({transform: [{type: "scale", factors: [0, 1]}]}, 200, 200), [20, 30]) is error,
    ambiguous_inverse: coordinate.invert(coordinate.configure({type: "helix"}, 200, 200), [20, 30]) is error,
    malformed_path: paths.parse("M0 0 C1 2") is error,
    invalid_arc_flag: paths.parse("M0 0 A1 1 0 2 0 1 1") is error,
    sampling_error: paths.sample_curve((t) error => error("bad point")) is error
};
[for (label, passed in checks where passed != true) string(label)]
