import chart: lambda.chart.chart
import network: lambda.chart.network
import util: lambda.chart.util
fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn near(a, b) => abs(a - b) < 0.000001
let graph = {nodes: [{id: "a"}, {id: "b"}, {id: "c"}, {id: "d"}], links: [
    {source: "a", target: "b", value: 3}, {source: "a", target: "c", value: 1},
    {source: "b", target: "d", value: 3}, {source: "c", target: "d", value: 1}]};
let sankey = network.layout(graph, 300, 200, {kind: "sankey"});
let chord = network.layout(graph, 300, 200, {kind: "chord"});
let force = network.layout(graph, 300, 200, {kind: "force_graph", seed: 17, iterations: 20});
let pinned = network.layout({*:graph, nodes: [{id: "a", fx: 27, fy: 49}, *slice(graph.nodes, 1, 4)]}, 300, 200, {kind: "force_graph", iterations: 20});
let spec = {width: 300, height: 200, padding: 0, data: graph, encoding: {}};
let markup = chart.render(<chart width: 300, height: 200, padding: 0, <data values: graph> <mark type: "sankey", labels: false>>);
let checks = {
    source_identity: sankey.nodes[0].row == graph.nodes[0] and sankey.links[0].row == graph.links[0],
    stages: sankey.nodes[0].x < sankey.nodes[1].x and sankey.nodes[1].x == sankey.nodes[2].x and sankey.nodes[2].x < sankey.nodes[3].x,
    proportional_nodes: near(sankey.nodes[1].height, 3 * sankey.nodes[2].height),
    proportional_links: near(sankey.links[0].width, 3 * sankey.links[1].width),
    flow_conservation: near(sankey.nodes[0].height, sankey.links[0].width + sankey.links[1].width),
    disjoint_stages: sankey.nodes[1].y + sankey.nodes[1].height <= sankey.nodes[2].y,
    chord_weight: near(chord.nodes[1].end - chord.nodes[1].start, 3 * (chord.nodes[2].end - chord.nodes[2].start)),
    chord_ribbons: len(chord.links) == 4 and near(chord.links[0].a1 - chord.links[0].a0, chord.links[0].b1 - chord.links[0].b0),
    chord_matrix: len(network.layout({nodes: [{id: 1}, {id: 2}], matrix: [[0, 2], [3, 0]]}, 200, 200, {kind: "chord"}).links) == 2,
    deterministic_force: force == network.layout(graph, 300, 200, {kind: "force_graph", seed: 17, iterations: 20}),
    different_seed: force != network.layout(graph, 300, 200, {kind: "force_graph", seed: 18, iterations: 20}),
    force_bounds: all([for (node in force.nodes) node.x >= 0 and node.x <= 300 and node.y >= 0 and node.y <= 200]),
    pinned: pinned.nodes[0].x == 27 and pinned.nodes[0].y == 49,
    svg_families: all([for (kind in ["sankey", "chord", "force_graph"],
        let image = chart.render_spec({*:spec, mark: {kind: kind, iterations: 10, labels: false}}))
        len(classes(image, "graph-node")) == 4 and len(classes(image, "graph-link")) == 4]),
    native_markup: len(classes(markup, "graph-node")) == 4,
    composed: len(classes(chart.render_spec({width: 300, height: 200, layer: [{*:spec, mark: {kind: "sankey", labels: false}}]}), "graph-link")) == 4,
    cycle: network.layout({*:graph, links: [*graph.links, {source: "d", target: "a", value: 1}]}, 300, 200, {kind: "sankey"}) is error,
    missing_endpoint: network.layout({*:graph, links: [{source: "a", target: "z", value: 1}]}, 300, 200, {kind: "sankey"}) is error,
    duplicate_id: network.layout({*:graph, nodes: [*graph.nodes, {id: "a"}]}, 300, 200) is error,
    invalid_weight: network.layout({*:graph, links: [{source: "a", target: "b", value: -1}]}, 300, 200, {kind: "chord"}) is error,
    matrix_dimensions: network.layout({nodes: [{id: 1}, {id: 2}], matrix: [[1]]}, 200, 200, {kind: "chord"}) is error,
    invalid_iteration: network.layout(graph, 300, 200, {kind: "force_graph", iterations: 1.5}) is error,
    invalid_pin: network.layout({nodes: [{id: 1, fx: "x"}], links: []}, 300, 200, {kind: "force_graph"}) is error,
    empty: network.layout({nodes: [], links: []}, 300, 200).nodes == []
};
[for (label, passed in checks where passed != true) string(label)]
