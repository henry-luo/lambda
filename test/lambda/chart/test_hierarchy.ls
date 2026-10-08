import chart: lambda.chart.chart
import hierarchy: lambda.chart.hierarchy
import util: lambda.chart.util

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn near(a, b) => abs(a - b) < 0.000001
let data = [{id: "root", value: 999}, {id: "A", parent: "root", value: 3}, {id: "B", parent: "root", value: 1}];
let flat = slice(data, 1, 3) |> {*:~, parent: null};
let tree = hierarchy.forest(data);
let rectangles = hierarchy.layout(data, 200, 100, {kind: "treemap", node_padding: 0});
let leaves = rectangles |: len(~.children) == 0;
let padded = hierarchy.layout(data, 200, 100, {kind: "treemap", node_padding: 4, header_height: 12});
let radial = hierarchy.layout(data, 200, 200, {kind: "sunburst", inner_radius: 20});
let spec = {width: 200, height: 200, padding: 0, data: data, mark: {kind: "treemap", node_padding: 0, labels: false},
    encoding: {tooltip: {field: "id"}}};
let image = chart.render_spec(spec);
let sun = chart.render_spec({*:spec, mark: {kind: "sunburst", inner_radius: 20, labels: false}});
let colored = chart.render_spec({*:spec, encoding: {color: {field: "id", dtype: "nominal", legend: null}}});
let markup = chart.render(<chart width: 300, height: 200, padding: 0,
    <data values: [{key: 1}, {key: 2, parent: 1, weight: 2}, {key: 3, parent: 1, weight: 1}]>
    <mark type: "treemap", node_field: "key", value_field: "weight">>);
let many = hierarchy.layout([for (index in 1 to 20) {id: index, value: index}], 300, 200, {kind: "treemap", node_padding: 0});
let zero = hierarchy.layout([{id: "a", value: 0}, {id: "b", value: 1}], 200, 100);
let checks = {
    internal_sum: tree[0].value == 4 and tree[0].height == 1,
    source_rows: tree[0].row.value == 999 and tree[0].children[0].row == data[1],
    rectangle_count: len(rectangles) == 3,
    root_bounds: rectangles[0].x == 0 and rectangles[0].y == 0 and rectangles[0].width == 200 and rectangles[0].height == 100,
    proportional_area: near(leaves[0].width * leaves[0].height, 15000.0) and near(leaves[1].width * leaves[1].height, 5000.0),
    area_conservation: near(sum(many |> ~.width * ~.height), 60000.0),
    no_overlap: len([for (i, a in many) for (j, b in many where i < j and
        min(a.x + a.width, b.x + b.width) > max(a.x, b.x) + 0.000001 and
        min(a.y + a.height, b.y + b.height) > max(a.y, b.y) + 0.000001) true]) == 0,
    padding: padded[1].x >= 4 and padded[1].y >= 16 and padded[1].x + padded[1].width <= 196.000001,
    radial_levels: radial[0].inner == 20 and radial[0].outer == 60 and radial[1].inner == 60 and radial[1].outer == 100,
    angular_weight: near(radial[1].end - radial[1].start, util.TAU * 0.75),
    full_circle: near(radial[0].end, util.TAU),
    forest: len(hierarchy.forest(flat)) == 2,
    empty: hierarchy.layout([], 200, 100) == [],
    zero_omitted: len(zero) == 1 and zero[0].id == "b",
    all_zero: hierarchy.layout([{id: 1, value: 0}], 200, 100) == [],
    default_leaf_weight: hierarchy.forest([{id: 1}, {id: 2, parent: 1}, {id: 3, parent: 1}])[0].value == 2,
    rectangle_svg: len(classes(image, "treemap-node")) == 3,
    tooltip: (elements(image) |: name(~) == 'title')[1][0] == "A",
    sunburst_svg: len(classes(sun, "sunburst-node")) == 3 and contains(classes(sun, "sunburst-node")[0].d, "A20 20"),
    sunburst_labels: chart.render_spec({*:spec, mark: {kind: "sunburst", inner_radius: 20}}) is element,
    encoded_color: classes(colored, "treemap-node")[0].fill != classes(colored, "treemap-node")[1].fill,
    markup_and_labels: len(classes(markup, "treemap-node")) == 3 and len(classes(markup, "hierarchy-label")) == 2,
    layered: len(classes(chart.render_spec({width: 200, height: 200, padding: 0, layer: [spec]}), "treemap-node")) == 3,
    cycle: hierarchy.forest([{id: 1, parent: 2}, {id: 2, parent: 1}]) is error,
    self_cycle: hierarchy.forest([{id: 1, parent: 1}]) is error,
    duplicate: hierarchy.forest([{id: 1}, {id: 1}]) is error,
    missing_parent: hierarchy.forest([{id: 1, parent: 2}]) is error,
    bad_id: hierarchy.forest([{value: 1}]) is error,
    bad_weight: hierarchy.forest([{id: 1, value: -1}]) is error,
    weight_overflow: hierarchy.forest([{id: 1}, {id: 2, parent: 1, value: 1e308}, {id: 3, parent: 1, value: 1e308}]) is error,
    bad_padding: hierarchy.layout(data, 200, 100, {node_padding: -1}) is error,
    bad_radii: hierarchy.layout(data, 200, 200, {kind: "sunburst", inner_radius: 120}) is error
};
[for (label, passed in checks where passed != true) string(label)]
