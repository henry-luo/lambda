// Pure graph snapshots: source rows and endpoint identity survive every layout.
import util: .util
import svg: .svg
import mark: .mark
import color: .color
import parse: .parse

pub fn normalize(data, options = {}) {
    let nodes = data.nodes;
    let matrix = data.matrix;
    let links = if (matrix != null and nodes is array and matrix is array) [for (i, row in matrix)
        for (j, value in row where value > 0) {source: nodes[i][if (options.node_field != null) options.node_field else "id"],
            target: nodes[j][if (options.node_field != null) options.node_field else "id"], value: value}]
        else data.links;
    let id_field = if (options.node_field != null) options.node_field else "id";
    let source_field = if (options.source_field != null) options.source_field else "source";
    let target_field = if (options.target_field != null) options.target_field else "target";
    let value_field = if (options.value_field != null) options.value_field else "value";
    let ids = nodes |> ~[id_field];
    let weighted = options.kind != "force_graph";
    let invalid = util.first_error([
        if (not (nodes is array) or not (links is array)) error("chart: graph requires nodes and links arrays"),
        if (matrix != null and (not (matrix is array) or len(matrix) != len(nodes) or
            any([for (row in matrix) not (row is array) or len(row) != len(nodes) or
                any([for (value in row) not util.finite_number(value) or value < 0])])))
            error("chart: chord matrix must be square, finite and nonnegative"),
        if (len(util.unique_vals(ids)) != len(ids) or any([for (id in ids)
            not (id is string or id is symbol or util.finite_number(id))])) error("chart: graph requires unique node IDs"),
        for (link in links) if (index_of(ids, link[source_field]) == null or index_of(ids, link[target_field]) == null)
            error("chart: graph link endpoint does not identify a node")
        else if (weighted and (not util.finite_number(link[value_field]) or link[value_field] < 0))
            error("chart: graph weights must be finite and nonnegative")]);
    if (invalid is error) invalid else {nodes: [for (i, row in nodes) {id: ids[i], row: row, index: i}],
        links: [for (i, row in links) {row: row, index: i, source: index_of(ids, row[source_field]),
            target: index_of(ids, row[target_field]), value: if (weighted) float(row[value_field]) else 1.0}]}
}

// Longest-path ranking rejects cycles after exactly |V| relaxation passes.
fn rank_pass(graph, ranks, remaining) {
    let next = [for (i, rank in ranks) max([rank, for (link in graph.links where link.target == i) ranks[link.source] + 1])];
    if (next == ranks) ranks else if (remaining <= 0) error("chart: Sankey stage ordering must be acyclic")
    else rank_pass(graph, next, remaining - 1)
}

fn sankey(graph, width, height, options) {
    let ranks = rank_pass(graph, [for (node in graph.nodes) 0], len(graph.nodes));
    let node_width = if (options.node_width != null) options.node_width else 12.0;
    let gap = if (options.node_padding != null) options.node_padding else 10.0;
    let weights = [for (node in graph.nodes) max([
        sum([for (link in graph.links where link.source == node.index) link.value]),
        sum([for (link in graph.links where link.target == node.index) link.value])])];
    let stages = if (ranks is error) [] else util.unique_vals(ranks);
    let ratios = [for (rank in stages, let indices = [for (i, r in ranks where r == rank) i],
        let total = sum([for (i in indices) weights[i]]) where total > 0)
        max(0.0, height - gap * (len(indices) - 1)) / total];
    let unit = if (len(ratios) > 0) min(ratios) else 0.0;
    if (ranks is error) ranks
    else if (not util.finite_number(node_width) or node_width <= 0 or node_width > width or
        not util.finite_number(gap) or gap < 0) error("chart: invalid Sankey node dimensions")
    else {
        let nodes = [for (i, node in graph.nodes,
            let peers = [for (j, r in ranks where r == ranks[i]) j],
            let preceding = peers |: ~ < i,
            let occupied = sum([for (j in peers) weights[j]]) * unit + gap * (len(peers) - 1))
            {*:node, x: float(ranks[i]) / max(1.0, float(max(ranks))) * (width - node_width),
                y: (height - occupied) / 2.0 + sum([for (j in preceding) weights[j]]) * unit + len(preceding) * gap,
                width: node_width, height: weights[i] * unit, value: weights[i]}];
        {nodes: nodes, links: [for (link in graph.links,
            let a = nodes[link.source], let b = nodes[link.target],
            let before = graph.links |: ~.index < link.index,
            let sy = a.y + sum([for (other in before where other.source == link.source) other.value]) * unit,
            let ty = b.y + sum([for (other in before where other.target == link.target) other.value]) * unit)
            {*:link, x1: a.x + node_width, x2: b.x, y1: sy, y2: ty, width: link.value * unit}]}
    }
}

fn seed_step(value) => (value * 48271) % 2147483647
fn randoms(seed, count, acc = []) => if (count <= 0) acc else
    randoms(seed_step(seed), count - 1, [*acc, float(seed_step(seed)) / 2147483647.0])

fn force_step(graph, nodes, width, height, options, remaining, total) {
    if (remaining <= 0) nodes else {
        let k = math.sqrt(width * height / max(1.0, float(len(nodes))));
        let temperature = min(width, height) * 0.1 * float(remaining) / float(total);
        let next = [for (i, node in nodes,
            let repulsion = [for (j, other in nodes where i != j,
                let dx = node.x - other.x, let dy = node.y - other.y,
                let squared = max(0.01, dx * dx + dy * dy)) [dx * k * k / squared, dy * k * k / squared]],
            let attraction = [for (edge in graph.links where edge.source == i or edge.target == i,
                let other = nodes[if (edge.source == i) edge.target else edge.source],
                let dx = other.x - node.x, let dy = other.y - node.y,
                let distance = math.sqrt(dx * dx + dy * dy)) [dx * distance / k, dy * distance / k]],
            let dx = sum(repulsion |> ~[0]) + sum(attraction |> ~[0]) + (width / 2.0 - node.x) * 0.1,
            let dy = sum(repulsion |> ~[1]) + sum(attraction |> ~[1]) + (height / 2.0 - node.y) * 0.1,
            let distance = max(0.01, math.sqrt(dx * dx + dy * dy)),
            let step = min(distance, temperature) / distance)
            {*:node, x: if (node.row.fx != null) node.row.fx else util.clamp_val(node.x + dx * step, 0.0, width),
                y: if (node.row.fy != null) node.row.fy else util.clamp_val(node.y + dy * step, 0.0, height)}];
        force_step(graph, next, width, height, options, remaining - 1, total)
    }
}

fn force(graph, width, height, options) {
    let iterations = if (options.iterations != null) options.iterations else 100;
    let seed = if (options.seed != null) options.seed else 1;
    let invalid = util.first_error([
        if (not util.finite_number(seed) or floor(seed) != seed) error("chart: force seed must be a finite integer"),
        if (not util.finite_number(iterations) or floor(iterations) != iterations or iterations < 0)
            error("chart: force iterations must be a nonnegative integer"),
        for (node in graph.nodes) for (field in ["x", "y", "fx", "fy"])
            if (node.row[field] != null and not util.finite_number(node.row[field])) error("chart: force positions must be finite")]);
    if (invalid is error) invalid else {
        let samples = randoms(1 + int(abs(seed)) % 2147483646, len(graph.nodes) * 2);
        let nodes = [for (i, node in graph.nodes) {*:node,
            x: if (node.row.fx != null) node.row.fx else if (node.row.x != null) node.row.x else samples[i * 2] * width,
            y: if (node.row.fy != null) node.row.fy else if (node.row.y != null) node.row.y else samples[i * 2 + 1] * height}];
        {nodes: force_step(graph, nodes, width, height, options, int(iterations), max(1, int(iterations))), links: graph.links}
    }
}

fn chord(graph, width, height, options) {
    let gap = if (options.pad_angle != null) options.pad_angle else 0.03;
    let weights = [for (node in graph.nodes) sum([for (link in graph.links
        where link.source == node.index or link.target == node.index) link.value *
            (if (link.source == link.target) 2.0 else 1.0)])];
    let total = sum(weights);
    if (not util.finite_number(gap) or gap < 0 or gap * len(weights) >= util.TAU)
        error("chart: chord padding must leave positive angular space")
    else {
        let unit = if (total > 0) (util.TAU - gap * len(weights)) / total else 0.0;
        let nodes = [for (i, node in graph.nodes) {*:node,
            start: sum(slice(weights, 0, i)) * unit + i * gap, end: sum(slice(weights, 0, i + 1)) * unit + i * gap}];
        {nodes: nodes, links: [for (link in graph.links,
            let before = graph.links |: ~.index < link.index,
            let a = nodes[link.source].start + sum([for (edge in before)
                (if (edge.source == link.source) edge.value else 0.0) + (if (edge.target == link.source) edge.value else 0.0)]) * unit,
            let b = nodes[link.target].start + sum([for (edge in before)
                (if (edge.source == link.target) edge.value else 0.0) + (if (edge.target == link.target) edge.value else 0.0)]) * unit +
                (if (link.source == link.target) link.value * unit else 0.0))
            {*:link, a0: a, a1: a + link.value * unit, b0: b, b1: b + link.value * unit}]}
    }
}

pub fn layout(data, width, height, options = {}) {
    let graph = normalize(data, options);
    if (graph is error) graph
    else if (not util.finite_number(width) or not util.finite_number(height) or width <= 0 or height <= 0)
        error("chart: graph layout requires positive finite dimensions")
    else if (len(graph.nodes) == 0) graph
    else if (options.kind == "sankey") sankey(graph, width, height, options)
    else if (options.kind == "chord") chord(graph, width, height, options)
    else force(graph, width, height, options)
}

fn part_context(ctx, options, part) => {*:ctx,
    encoding: {*:ctx.encoding, *:parse.attributes(options.parts[part].encoding)}, _part: part}
fn part_options(options, part) => {*:options, *:parse.attributes(options.parts[part])}

fn ribbon(link) {
    let mid = (link.x1 + link.x2) / 2.0;
    svg.M(link.x1, link.y1) ++ " " ++ svg.C(mid, link.y1, mid, link.y2, link.x2, link.y2) ++ " " ++
        svg.L(link.x2, link.y2 + link.width) ++ " " ++ svg.C(mid, link.y2 + link.width,
            mid, link.y1 + link.width, link.x1, link.y1 + link.width) ++ " Z"
}

fn radial(radius, angle) => [radius * math.cos(angle - util.PI / 2.0), radius * math.sin(angle - util.PI / 2.0)]
fn chord_ribbon(link, radius) {
    let a0 = radial(radius, link.a0); let a1 = radial(radius, link.a1);
    let b0 = radial(radius, link.b0); let b1 = radial(radius, link.b1);
    svg.M(a0[0], a0[1]) ++ " " ++ svg.A(radius, radius, 0, if (link.a1 - link.a0 > util.PI) 1 else 0, 1, a1[0], a1[1]) ++
        " Q 0 0 " ++ util.fmt_num(b0[0]) ++ " " ++ util.fmt_num(b0[1]) ++ " " ++
        svg.A(radius, radius, 0, if (link.b1 - link.b0 > util.PI) 1 else 0, 1, b1[0], b1[1]) ++
        " Q 0 0 " ++ util.fmt_num(a0[0]) ++ " " ++ util.fmt_num(a0[1]) ++ " Z"
}

pub fn render(data, ctx, options) {
    let graph = layout(data, ctx.plot_w, ctx.plot_h, options);
    let nc = part_context(ctx, options, "node"); let lc = part_context(ctx, options, "link");
    let no = part_options(options, "node"); let lo = part_options(options, "link");
    let radius = min(ctx.plot_w, ctx.plot_h) / 2.0 - 12.0;
    if (graph is error) graph else svg.group_class("marks " ++ options.kind, [
        <g class: "links", transform: if (options.kind == "chord") svg.translate(ctx.plot_w / 2.0, ctx.plot_h / 2.0) else null,
            for (edge in graph.links,
                let a = graph.nodes[edge.source], let b = graph.nodes[edge.target],
                let linear = options.kind == "force_graph",
                let appearance = mark.style(lc, edge.row, lo,
                    {fill: if (linear) "none" else color.category10[edge.source % 10], stroke: "#aaa", 'stroke-width': 1.5, opacity: 0.5}, linear))
                <path class: "graph-link", 'data-source': string(a.id), 'data-target': string(b.id),
                    d: if (options.kind == "sankey") ribbon(edge) else if (options.kind == "chord") chord_ribbon(edge, radius)
                        else svg.line_path([[a.x, a.y], [b.x, b.y]]), *:appearance, mark.tooltip(lc, edge.row)>>,
        for (i, node in graph.nodes,
            let appearance = mark.style(nc, node.row, no, {fill: color.category10[i % 10], stroke: "white", 'stroke-width': 1, opacity: 1.0}))
            <g class: "graph-node", 'data-node': string(node.id),
                if (options.kind == "sankey") <rect x: node.x, y: node.y, width: node.width, height: node.height, *:appearance, mark.tooltip(nc, node.row)>
                else if (options.kind == "chord") <path d: svg.arc_path(ctx.plot_w / 2.0, ctx.plot_h / 2.0,
                    radius, radius + 10.0, node.start - util.PI / 2.0, node.end - util.PI / 2.0), *:appearance, mark.tooltip(nc, node.row)>
                else <circle cx: node.x, cy: node.y, r: math.sqrt(max(0.0, mark.appearance(nc, "size", node.row, 80.0)) / util.PI),
                    *:appearance, mark.tooltip(nc, node.row)>
                if (options.labels != false and options.kind != "chord") <text x: node.x + (if (options.kind == "sankey") node.width + 4 else 7),
                    y: node.y + (if (options.kind == "sankey") node.height / 2.0 else 0.0), 'dominant-baseline': "middle",
                    'font-size': 11, fill: "#222", string(node.row[if (options.label_field != null) options.label_field else if (options.node_field != null) options.node_field else "id"])>>])
}
