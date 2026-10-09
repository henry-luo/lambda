// Hierarchical node/link and circle layouts reuse the validated source forest.
import hierarchy: .hierarchy
import util: .util
import svg: .svg
import mark: .mark
import color: .color
import parse: .parse

fn leaf_count(node) => if (len(node.children) == 0) 1 else sum(node.children |> leaf_count(~))

fn tree_nodes(nodes, offset = 0, depth = 0, parent = null) {
    [for (i, node in nodes,
        let first = offset + sum([for (previous in slice(nodes, 0, i)) leaf_count(previous)]),
        let descendants = tree_nodes(node.children, first, depth + 1, node.id),
        let children = descendants |: ~.parent == node.id,
        let x = if (len(children) == 0) float(first) + 0.5 else avg(children |> ~.u)) (
        {*:node, parent: parent, u: x, depth: depth}, *descendants)]
}

fn distance(a, b) => math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2)

// Tangencies supply finite candidates; choose the smallest enclosing bounding circle.
fn tangent(a, b, radius) {
    let d = distance(a, b);
    let ar = a.r + radius; let br = b.r + radius;
    let x = if (d > 0) (d * d + ar * ar - br * br) / (2.0 * d) else 0.0;
    let squared = ar * ar - x * x;
    if (d <= 0 or squared < -0.000000001) [] else [for (side in [-1.0, 1.0],
        let y = math.sqrt(max(0.0, squared)) * side)
        {x: a.x + x * (b.x - a.x) / d - y * (b.y - a.y) / d,
            y: a.y + x * (b.y - a.y) / d + y * (b.x - a.x) / d, r: radius}]
}

fn enclosure(circles) {
    let left = min(circles |> ~.x - ~.r); let right = max(circles |> ~.x + ~.r);
    let top = min(circles |> ~.y - ~.r); let bottom = max(circles |> ~.y + ~.r);
    let center = {x: (left + right) / 2.0, y: (top + bottom) / 2.0};
    {*:center, r: max([for (circle in circles) distance(center, circle) + circle.r])}
}

fn pack_siblings(nodes, index = 0, placed = []) {
    if (index >= len(nodes)) placed else {
        let node = nodes[index];
        let candidates = if (len(placed) == 0) [{x: 0.0, y: 0.0, r: node.r}]
            else [for (circle in placed) {x: circle.x + circle.r + node.r, y: circle.y, r: node.r},
                for (i, a in placed) for (j, b in placed where j > i) for (point in tangent(a, b, node.r)) point];
        let clear = [for (candidate in candidates where all([for (circle in placed)
            distance(candidate, circle) + 0.000000001 >= candidate.r + circle.r])) candidate];
        let chosen = sort(clear, (point) => enclosure([*placed, point]).r)[0];
        pack_siblings(nodes, index + 1, [*placed, {*:node, *:chosen}])
    }
}

fn packed(node, padding) {
    if (len(node.children) == 0) {*:node, r: math.sqrt(node.value), packed: []}
    else {
        let children = pack_siblings([for (child in node.children) packed(child, padding)]);
        let bound = enclosure(children);
        {*:node, r: bound.r + padding, packed: [for (child in children) {*:child, x: child.x - bound.x, y: child.y - bound.y}]}
    }
}

fn flatten(nodes, x, y, unit, depth = 0, parent = null) => [for (node in nodes,
    let px = x + node.x * unit, let py = y + node.y * unit) (
        {*:node, x: px, y: py, r: node.r * unit, parent: parent, depth: depth},
        *flatten(node.packed, px, py, unit, depth + 1, node.id))]

pub fn layout(data, width, height, options = {}) {
    let roots = hierarchy.forest(data, options);
    let padding = if (options.node_padding != null) options.node_padding else 0.0;
    let orientation = if (options.orientation != null) options.orientation else "vertical";
    if (roots is error) roots
    else if (not util.finite_number(width) or not util.finite_number(height) or width <= 0 or height <= 0 or
        not util.finite_number(padding) or padding < 0) error("chart: hierarchy requires positive dimensions and nonnegative padding")
    else if (not contains(["vertical", "horizontal", "radial"], orientation)) error("chart: unknown tree orientation")
    else if (len(roots) == 0) []
    else if (options.kind == "pack") {
        let circles = pack_siblings([for (root in roots) packed(root, padding)]);
        let bound = enclosure(circles);
        let unit = if (bound.r > 0) min(width, height) / (2.0 * bound.r) else 0.0;
        flatten(circles, width / 2.0 - bound.x * unit, height / 2.0 - bound.y * unit, unit)
    } else {
        let nodes = tree_nodes(roots);
        let count = sum(roots |> leaf_count(~));
        let depth = max(1.0, float(max(nodes |> ~.depth)));
        [for (node in nodes,
            let u = node.u / float(count), let v = float(node.depth) / depth,
            let radius = v * min(width, height) / 2.0,
            let angle = u * util.TAU - util.PI / 2.0)
            {*:node, x: if (orientation == "radial") width / 2.0 + radius * math.cos(angle)
                else if (orientation == "horizontal") v * width else u * width,
                y: if (orientation == "radial") height / 2.0 + radius * math.sin(angle)
                else if (orientation == "horizontal") u * height else v * height}]
    }
}

pub fn render(data, ctx, options) {
    let polar=ctx._coordinate!=null and ctx._coordinate.type!="cartesian";
    let laid_out = layout(data, ctx.plot_w, ctx.plot_h, if (polar) {*:options,orientation:"vertical"} else options);
    let nodes=if (polar) [for (node in laid_out) {*:node,y:ctx.plot_h-node.y}] else laid_out;
    let nc = mark.part_context(ctx, options, "node", nodes |> ~.row);
    let lc = mark.part_context(ctx, options, "link", nodes |> ~.row);
    let label_ctx = mark.part_context(ctx, options, "label", nodes |> ~.row);
    let invalid=util.first_error([nodes,mark.part_error([nc,lc,label_ctx])]);
    if (invalid is error) invalid else svg.group_class("marks " ++ options.kind, [
        if (options.kind == "tree") for (node in nodes where node.parent != null and mark.part_selected(lc,node.row),
            let parent = (nodes |: ~.id == node.parent)[0],
            let horizontal = options.orientation == "horizontal",
            let mid = if (horizontal) (parent.x + node.x) / 2.0 else (parent.y + node.y) / 2.0)
            <path class: "tree-link", 'data-source': string(parent.id), 'data-target': string(node.id),
                d: if (options.link == "straight" or options.orientation == "radial") svg.line_path([[parent.x, parent.y], [node.x, node.y]])
                    else if (options.link == "orthogonal") svg.line_path([[parent.x, parent.y],
                        if (horizontal) [mid, parent.y] else [parent.x, mid],
                        if (horizontal) [mid, node.y] else [node.x, mid], [node.x, node.y]])
                    else svg.M(parent.x, parent.y) ++ " " ++ (if (horizontal) svg.C(mid, parent.y, mid, node.y, node.x, node.y)
                        else svg.C(parent.x, mid, node.x, mid, node.x, node.y)),
                *:mark.style(lc, node.row, mark.part_options(options, "link"),
                    {fill: "none", stroke: "#aaa", 'stroke-width': 1, opacity: 1.0}, true)>,
        for (node in nodes where mark.part_selected(nc,node.row)) <circle class: options.kind ++ "-node", 'data-node': string(node.id), cx: node.x, cy: node.y,
            r: if (options.kind == "pack") node.r else if (options.node_radius != null) options.node_radius else 4.0,
            *:mark.style(nc, node.row, mark.part_options(options, "node"),
                {fill: color.category10[node.depth % 10], stroke: "white", 'stroke-width': 1, opacity: if (options.kind == "pack") 0.5 else 1.0}),
            mark.tooltip(nc, node.row)>,
        if (options.labels != false) for (node in nodes where (options.kind != "pack" or len(node.children) == 0) and mark.part_selected(label_ctx,node.row))
            <text class: "cluster-label", x: node.x + (if (options.kind == "tree") 6 else 0), y: node.y,
                'text-anchor': if (options.kind == "tree") "start" else "middle", 'dominant-baseline': "middle", 'font-size': 11,
                *:mark.style(label_ctx, node.row, mark.part_options(options, "label"), {fill: "#222", opacity: 1.0}),
                string(node.row[if (options.label_field != null) options.label_field else if (options.node_field != null) options.node_field else "id"])>])
}
