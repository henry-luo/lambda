import chart: lambda.chart.chart
import paths: lambda.chart.path_geometry
import geometry: lambda.chart.geometry
import svg: lambda.chart.svg
import primitive: lambda.chart.primitive
import animation: lambda.chart.animation

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn tags(node, tag) => elements(node) |: name(~) == tag
fn near(a, b) => abs(a - b) < 0.001
fn close(a, b) => near(a[0], b[0]) and near(a[1], b[1])
fn normalized(point) => point |> ~ / paths.distance([0.0, 0.0], point)
let base = {width: 200, height: 120, padding: 0, encoding: {
    x: {field: "x", scale: null, axis: null}, y: {field: "y", scale: null, axis: null},
    x2: {field: "x2"}, y2: {field: "y2"}}};
let row = {x: 20, y: 80, x2: 180, y2: 20};
fn render(options = {}, data = row) => chart.render_spec({*:base, data: [data], mark: {kind: "link", arrow: true, *:options}})
fn outline(image) {
    let vertices = paths.parse(tags(image, 'path')[1].d) |> ~.end;
    slice(vertices, 0, len(vertices) - 1)
}
fn head_base(image) {
    let vertices = outline(image);
    let middle = int((len(vertices) - 1) / 2);
    [vertices[middle], vertices[middle + 1]]
}
fn axis(image) {
    let vertices = outline(image);
    let center = geometry.interpolate(vertices[1], vertices[len(vertices) - 1], 0.5);
    normalized([vertices[0][0] - center[0], vertices[0][1] - center[1]])
}
fn aligned(a, b) => a[0] * b[0] + a[1] * b[1] > 0.995
fn inside(vertices, point) => geometry.contains_point(vertices, point) or
    min([for (i, a in vertices) geometry.distance_segment(point, a, vertices[(i + 1) % len(vertices)])]) < 0.05
fn follows_curve(image, source) {
    let vertices = outline(image);
    let middle = int((len(vertices) - 1) / 2);
    let curve = paths.sample(source, (point) => point, 0.01)[0].points;
    all([for (i in 1 to middle, let center = geometry.interpolate(vertices[i], vertices[len(vertices) - i], 0.5))
        min([for (j in 1 to (len(curve) - 1)) geometry.distance_segment(center, curve[j - 1], curve[j])]) < 0.03])
}
fn covered_join(image) {
    let body = tags(image, 'path')[0];
    let segments = paths.parse(body.d);
    let terminal = segments[len(segments) - 1];
    let tangent = normalized([terminal.end[0] - terminal.b[0], terminal.end[1] - terminal.b[1]]);
    all([for (side in [-0.5, 0.5], let offset = side * body["stroke-width"])
        inside(outline(image), [terminal.end[0] - offset * tangent[1], terminal.end[1] + offset * tangent[0]])])
}
let curved = render({curve: "curve", width: 10, opacity: 0.4});
let source = primitive.link_path([20.0, 80.0], [180.0, 20.0], {curve: "curve"});
let shaft = paths.parse(tags(curved, 'path')[0].d);
let terminal = shaft[len(shaft) - 1];
let variants = [for (width in [1.5, 4, 10, 18]) for (orientation in ["horizontal", "vertical"])
    for (data in [row, {x: 180, y: 20, x2: 20, y2: 80}]) render({curve: "curve", width: width, orientation: orientation}, data)];
let styled = (elements(curved) |: ~.class == "link")[0];
let explicit = render({arrow_size: 12});
let zero = render({}, {*:row, x2: row.x, y2: row.y});
let vector_zero = render({kind: "vector"}, {*:row, direction: 0, magnitude: 0});
let quadratic = render({kind: "path", d: "M20 80 Q20 20 180 20"});
let arc = render({kind: "path", d: "M20 80 A40 40 0 0 1 100 80"});
let corrected_arc = paths.arrow_geometry("M20 80 A10 10 0 0 1 100 80", 8, 1.5);
let short = paths.arrow_geometry("M0 0 L2 0", 8, 10);
let subpaths = paths.arrow_geometry("M0 0 L10 0 M20 20 L100 20", 8);
let stateful = primitive.path_mark("M20 80 L180 20", {fill: "none", stroke: "orange", 'stroke-width': 10, opacity: 0.4,
    tabindex: "0", 'data-focus-key': "link",
    'data-chart-state': "{\"stroke\":\"orange\",\"opacity\":0.4}"}, {kind: "link", arrow: true});
let frame = animation.sample(stateful, stateful, 0);
let checks = {
    curve_tangent: aligned(axis(curved), [1.0, 0.0]),
    vertical_curve_tangent: aligned(axis(render({curve: "curve", orientation: "vertical"})), [0.0, -1.0]),
    reverse_curve_tangent: aligned(axis(render({curve: "curve"}, {x: 180, y: 20, x2: 20, y2: 80})), [-1.0, 0.0]),
    orthogonal_tangent: close(axis(render({curve: "orthogonal"})), [1.0, 0.0]),
    zero_final_leg: close(axis(render({curve: "orthogonal"}, {*:row, x2: row.x})), [0.0, -1.0]),
    straight_tangent: close(axis(render()), normalized([160.0, -60.0])),
    endpoint_preserved: close(outline(curved)[0], [180.0, 20.0]),
    proportional_head: paths.distance(head_base(curved)[0], head_base(curved)[1]) >= 15.0 and
        paths.distance(head_base(curved)[0], head_base(curved)[1]) <= 25.0,
    curved_head: len(outline(curved)) > 3 and follows_curve(curved, source),
    head_base_meets_shaft: inside(outline(curved), terminal.end),
    simple_head: geometry.simple_ring([*outline(curved), outline(curved)[0]]),
    shaft_trimmed: paths.distance(terminal.end, [180.0, 20.0]) > 0,
    cubic_retained: len(shaft) == 2 and terminal.kind == "C",
    stroke_join_covered: covered_join(curved),
    varied_widths_and_directions: all(variants |> covered_join(~) and
        geometry.simple_ring([*outline(~), outline(~)[0]])),
    uniform_opacity: styled.opacity == 0.4 and all(content(styled) |> ~.opacity == 1.0),
    frame_state: frame.opacity == 0.4 and all(content(frame) |> ~.opacity == 1.0) and frame[1].fill == "orange" and frame[1].stroke == "none",
    single_keyboard_target: len(elements(stateful) |: ~.tabindex != null) == 1 and stateful[1]["data-focus-key"] == null,
    head_color: tags(curved, 'path')[1].fill == tags(curved, 'path')[0].stroke and tags(curved, 'path')[1].stroke == "none",
    explicit_size: near(paths.distance(outline(explicit)[0], geometry.interpolate(head_base(explicit)[0], head_base(explicit)[1], 0.5)), 12.0),
    no_arrow: len(tags(render({arrow: false}), 'path')) == 1,
    zero_link: len(tags(zero, 'path')) == 1 and not contains(format(zero, 'xml'), "nan"),
    zero_vector: len(tags(vector_zero, 'path')) == 1 and not contains(format(vector_zero, 'xml'), "nan"),
    quadratic_tangent: aligned(axis(quadratic), [1.0, 0.0]) and follows_curve(quadratic, "M20 80 Q20 20 180 20"),
    arc_tangent: aligned(axis(arc), [0.0, 1.0]) and paths.parse(tags(arc, 'path')[0].d)[1].kind == "A" and
        follows_curve(arc, "M20 80 A40 40 0 0 1 100 80"),
    corrected_arc_radius: paths.parse(corrected_arc.body)[1].radius == [40.0, 40.0],
    short_link: short.body == "" and short.length == 2.0 and short.to == [2.0, 0.0],
    separate_subpaths: len(paths.parse(subpaths.body) |: ~.kind == "M") == 2 and subpaths.to == [100.0, 20.0],
    disabled_size: paths.arrow_geometry("M0 0 L10 0", 0).body == "M0 0 L10 0",
    move_only: paths.arrow_geometry("M0 0", 8).length == 0,
    degenerate_head: svg.arrow_head(1, 1, 1, 1, "red") == null,
    invalid_size: render({arrow_size: -1}) is error
};
[for (label, passed in checks where passed != true) string(label)]
