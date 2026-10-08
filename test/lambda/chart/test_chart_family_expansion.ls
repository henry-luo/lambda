import chart: lambda.chart.chart
import cluster: lambda.chart.cluster
import field: lambda.chart.field
import indicator: lambda.chart.indicator
import scale: lambda.chart.scale
import util: lambda.chart.util
fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn near(a, b) => abs(a - b) < 0.000001
fn distance(a, b) => math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2)
let tree = [{id: "root"}, {id: "a", parent: "root", value: 3}, {id: "b", parent: "root", value: 1}, {id: "c", parent: "a", value: 2}, {id: "d", parent: "a", value: 1}];
let packed = cluster.layout(tree, 400, 300, {kind: "pack"});
let leaves = packed |: len(~.children) == 0;
let nodes = cluster.layout(tree, 400, 300, {kind: "tree"});
let horizontal = cluster.layout(tree, 300, 400, {kind: "tree", orientation: "horizontal"});
let points = [{x: 0, y: 0}, {x: 1, y: 1}, {x: 0.5, y: 0.5}];
let grid = field.density(points, {bandwidth: 0.2, resolution: [5, 5], extent: [[-1, 2], [-1, 2]]});
let contour = field.contours(grid, [0.2]);
let swarm_data = [{group: "a", value: 10}, {group: "a", value: 10}, {group: "a", value: 10}, {group: "b", value: 12}];
let enc = {x: {field: "group", dtype: "nominal", axis: null}, y: {field: "value", dtype: "quantitative", axis: null}};
let ctx = {encoding: enc, x_field: "group", y_field: "value", x_scale: scale.position_scale(enc.x, swarm_data, 0, 200, "point", true),
    y_scale: scale.position_scale(enc.y, swarm_data, 200, 0, "point", false), plot_w: 200, plot_h: 200};
let swarm = field.beeswarm(swarm_data, ctx, {size: 36, padding: 1});
let spec = {width: 300, height: 200, padding: 0, encoding: {}};
fn lollipop(data, options) => {layer: [
    {mark: {kind: "rule"}, encoding: {x: {field: "x", dtype: "quantitative"}, y: {value: 0}, y2: {field: "y", dtype: "quantitative"}}},
    {mark: {kind: "point", size: 50}, encoding: {x: {field: "x", dtype: "quantitative"}, y: {field: "y", dtype: "quantitative"}}}]};
let composite = chart.render_spec({*:spec, data: points, mark: {kind: "composite", expand: lollipop}});
let checks = {
    tree_count: len(nodes) == 5,
    tree_centered: near(nodes[0].u, (nodes[1].u + nodes[4].u) / 2.0),
    tree_orientation: all([for (i, node in nodes) near(node.x, horizontal[i].y) and near(node.y, horizontal[i].x)]),
    pack_leaf_area: near(leaves[0].r ** 2, 2.0 * leaves[1].r ** 2),
    pack_containment: all([for (node in packed where node.parent != null, let parent = (packed |: ~.id == node.parent)[0]) distance(node, parent) + node.r <= parent.r + 0.000001]),
    pack_siblings: all([for (i, a in packed) for (j, b in packed where i < j and a.parent == b.parent) distance(a, b) + 0.000001 >= a.r + b.r]),
    pack_bounds: all([for (node in packed) node.x - node.r >= -0.000001 and node.y - node.r >= -0.000001 and node.x + node.r <= 400.000001 and node.y + node.r <= 300.000001]),
    cluster_svg: all([for (kind in ["tree", "pack"]) len(classes(chart.render_spec({*:spec, data: tree, mark: {kind: kind}}), kind ++ "-node")) == 5]),
    density_count: len(grid.cells) == 25,
    density_symmetry: near(grid.cells[0].density, grid.cells[24].density),
    contours: len(contour) == 1 and len(contour[0].polygons) > 0 and all([for (polygon in contour[0].polygons) all([for (point in polygon) point[2] >= 0.199999])]),
    density_svg: len(classes(chart.render_spec({*:spec, data: points, encoding: {x: {field: "x", dtype: "quantitative", axis: null}, y: {field: "y", dtype: "quantitative", axis: null}}, mark: {kind: "density", resolution: 5}}), "density-cell")) == 25,
    contour_svg: len(classes(chart.render_spec({*:spec, data: points, encoding: {x: {field: "x", dtype: "quantitative", axis: null}, y: {field: "y", dtype: "quantitative", axis: null}}, mark: {kind: "density", resolution: 5, contours: true}}), "density-contour")) == 8,
    swarm_count: len(swarm) == 4,
    swarm_measure_preserved: all([for (point in swarm) point.y == scale.scale_apply(ctx.y_scale, point.row.value)]),
    swarm_nonoverlap: all([for (i, a in swarm) for (j, b in swarm where i < j and a.row.group == b.row.group) distance(a, b) + 0.000001 >= a.radius + b.radius + 1.0]),
    swarm_svg: len(classes(chart.render_spec({*:spec, data: swarm_data, encoding: enc, mark: {kind: "point", beeswarm: true}}), "marks beeswarm")) == 1,
    indicator_fraction: indicator.fraction(25, {domain: [0, 100]}) == 0.25 and indicator.fraction(200, {domain: [0, 100]}) == 1.0,
    reversed_domain: indicator.fraction(25, {domain: [100, 0]}) == 0.75,
    indicators: all([for (kind in ["gauge", "liquid"]) chart.render_spec({*:spec, data: [{value: 25}], mark: {kind: kind, domain: [0, 100]}}) is element]),
    linear_gauge: len(classes(chart.render_spec({*:spec, data: [{value: 25}], mark: {kind: "gauge", domain: [0, 100], shape: "linear", target: 50}}), "gauge-linear")) == 1,
    increasing_funnel: len(classes(chart.render_spec({*:spec, data: [{value: 2}, {value: 4}, {value: 1}], mark: {kind: "funnel"}}), "funnel-stage")) == 3,
    primitive_marks: all([for (kind in ["link", "vector", "path", "polygon"])
        chart.render_spec({*:spec, data: [{x: 0, y: 0, x2: 100, y2: 100, direction: 0.5, magnitude: 20, path: "M0 0 L10 10"},
            {x: 10, y: 20, x2: 100, y2: 100, direction: 0.5, magnitude: 20, path: "M0 0 L10 10"},
            {x: 20, y: 0, x2: 100, y2: 100, direction: 0.5, magnitude: 20, path: "M0 0 L10 10"}],
            encoding: {x: {field: "x", dtype: "quantitative", axis: null}, y: {field: "y", dtype: "quantitative", axis: null},
                x2: {field: "x2"}, y2: {field: "y2"}}, mark: {kind: kind}}) is element]),
    composite_expansion: len(classes(composite, "marks points")) == 1,
    recursive_factory: chart.render_spec({*:spec, data: points, mark: {kind: "composite", expand: (data, options) => {mark: {kind: "composite", expand: lollipop}}}}) is error,
    bad_density_bandwidth: field.density(points, {bandwidth: 0}) is error,
    bad_density_grid: field.density(points, {resolution: [0, 3]}) is error,
    bad_density_value: field.density([{x: "a", y: 2}], {bandwidth: 1, extent: [[0, 1], [0, 1]]}) is error,
    bad_indicator_domain: indicator.fraction(10, {domain: [1, 1]}) is error,
    unclamped_outside: indicator.fraction(200, {domain: [0, 100], clamp: false}) is error
};
[for (label, passed in checks where passed != true) string(label)]
