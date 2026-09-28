// Named TikZ nodes use Radiant's measured-child layout for shape bounds and anchors.
import radiant
import opts: .options
import labels: .labels
import svg: lambda.chart.svg

let PX_PER_CM = 96.0 / 2.54
let MARGIN = 16.0

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn node_shape(node) string^ {
    let checked = opts.check(node, ["draw", "rounded corners", "diamond", "aspect"])^
    let diamond = opts.has(node, "diamond")
    if (opts.has(node, "aspect") and not diamond)
        raise error("TikZ aspect requires a diamond node")
    else if (diamond and opts.has(node, "rounded corners"))
        raise error("rounded diamond nodes are not supported")
    else if (diamond) "diamond"
    else if (opts.has(node, "rounded corners")) "rounded"
    else "rectangle"
}

fn node_aspect(node) float^ {
    let aspect = opts.numeric_value(opts.value(node, "aspect", "1"))^
    if (aspect <= 0.0) raise error("TikZ diamond aspect must be positive")
    else aspect
}

fn node_html(node) any^ {
    let shape = node_shape(node)^
    let aspect = node_aspect(node)^
    let label = labels.prepare(node.source)^;
    <span class: "tikz-node-label",
        'data-tikz-kind': "node", 'data-node-id': node.id,
        'data-x': string(node.x), 'data-y': string(node.y),
        'data-shape': shape, 'data-aspect': string(aspect),
        'data-draw': if (opts.has(node, "draw")) "true" else "false",
        style: "display:inline-block;white-space:nowrap;padding:4px 8px;" ++
            "font-size:16px;line-height:1.3;vertical-align:top;",
        label.element>
}

fn edge_html(path) any^ {
    if (path.action != "draw") raise error("only drawn TikZ paths are supported")
    let checked = opts.check(path, ["black", "blue", "red", "green", "orange",
        "purple", "gray", "color", "-{Latex}"])^
    let points = children_named(path, "point")
    if (len(points) != 2 or points[0].ref == null or points[1].ref == null)
        raise error("named TikZ paths require two named node endpoints")
    let color = opts.color(path, "black")^;
    <span 'data-tikz-kind': "edge", 'data-from': points[0].ref,
        'data-to': points[1].ref,
        'data-color': color,
        'data-arrow-end': if (opts.has(path, "-{Latex}")) "true" else "false",
        style: "display:block;width:0;height:0;overflow:hidden;" ++
            "visibility:hidden;pointer-events:none;">
}

fn child_html(child) any^ {
    let kind = string(name(child))
    if (kind == "node") node_html(child)^
    else edge_html(child)^
}

fn measured_nodes(children) => [for (index, child in children
    where child.attrs["data-tikz-kind"] == "node") {
    id: child.attrs["data-node-id"], index: index,
    x: float(child.attrs["data-x"]) * PX_PER_CM,
    y: 0.0 - float(child.attrs["data-y"]) * PX_PER_CM,
    label_width: float(child.width), label_height: float(child.height),
    shape: child.attrs["data-shape"],
    aspect: float(child.attrs["data-aspect"]),
    draw: child.attrs["data-draw"] == "true"
}]

fn node_width(node) => if (node.shape == "diamond")
    node.aspect * (node.label_width / node.aspect + node.label_height + 4.0)
    else node.label_width

fn node_height(node) => if (node.shape == "diamond")
    node.label_width / node.aspect + node.label_height + 4.0
    else node.label_height

fn node_bounds(nodes) {
    let left = min([for (node in nodes) node.x - node_width(node) / 2.0])
    let right = max([for (node in nodes) node.x + node_width(node) / 2.0])
    let top = min([for (node in nodes) node.y - node_height(node) / 2.0])
    let bottom = max([for (node in nodes) node.y + node_height(node) / 2.0])
    {left: left, right: right, top: top, bottom: bottom}
}

fn shifted_nodes(nodes, dx, dy) => [for (node in nodes) {*:node,
    x: node.x + dx, y: node.y + dy}]

fn find_node(nodes, id) any^ {
    let matches = [for (node in nodes where node.id == id) node]
    if (len(matches) == 1) matches[0]
    else raise error("unknown TikZ node: " ++ string(id))
}

fn border_point(node, toward) {
    let vx = toward.x - node.x
    let vy = toward.y - node.y
    let half_width = node_width(node) / 2.0
    let half_height = node_height(node) / 2.0
    // intersect the center ray with the visible shape, not the label box.
    let factor = if (node.shape == "diamond")
        1.0 / (abs(vx) / half_width + abs(vy) / half_height)
        else min([if (vx == 0.0) 1e100 else half_width / abs(vx),
                  if (vy == 0.0) 1e100 else half_height / abs(vy)])
    {x: node.x + vx * factor, y: node.y + vy * factor}
}

fn edge_geometry(child, nodes) any^ {
    let from = find_node(nodes, child.attrs["data-from"])^
    let to = find_node(nodes, child.attrs["data-to"])^
    if (from.x == to.x and from.y == to.y)
        raise error("named TikZ edge has coincident node centers")
    else {
        let start = border_point(from, to)
        let end = border_point(to, from)
        if (start.x == end.x and start.y == end.y)
            raise error("named TikZ edge has no visible length")
        else {start: start, end: end, color: child.attrs["data-color"],
            arrow_end: child.attrs["data-arrow-end"] == "true"}
    }
}

fn arrow_head(edge) {
    let dx = edge.end.x - edge.start.x
    let dy = edge.end.y - edge.start.y
    let length = math.sqrt(dx * dx + dy * dy)
    let ux = dx / length
    let uy = dy / length
    let base_x = edge.end.x - ux * 8.0
    let base_y = edge.end.y - uy * 8.0
    let wing_x = uy * 3.0
    let wing_y = 0.0 - ux * 3.0;
    <path d: svg.M(edge.end.x, edge.end.y) ++ " " ++
        svg.L(base_x + wing_x, base_y + wing_y) ++ " " ++
        svg.L(base_x - wing_x, base_y - wing_y) ++ " Z",
        fill: edge.color>
}

fn edge_svg(edge) {
    let line = <path d: svg.M(edge.start.x, edge.start.y) ++ " " ++
        svg.L(edge.end.x, edge.end.y), fill: "none", stroke: edge.color,
        'stroke-width': 1.2>;
    if (edge.arrow_end) <g line arrow_head(edge)>
    else <g line>
}

fn child_placement(index, placed) {
    let matches = [for (node in placed where node.index == index) node]
    if (len(matches) == 1) {
        index: index,
        x: matches[0].x - matches[0].label_width / 2.0,
        y: matches[0].y - matches[0].label_height / 2.0,
        z: 0
    } else {index: index, x: 0.0, y: 0.0, z: -2}
}

fn shape_svg(node) {
    if (not node.draw) null
    else if (node.shape == "diamond") {
        let half_width = node_width(node) / 2.0
        let half_height = node_height(node) / 2.0;
        <path d: svg.M(node.x, node.y - half_height) ++ " " ++
            svg.L(node.x + half_width, node.y) ++ " " ++
            svg.L(node.x, node.y + half_height) ++ " " ++
            svg.L(node.x - half_width, node.y) ++ " Z",
            fill: "white", stroke: "black", 'stroke-width': 1.2>
    } else <rect x: node.x - node_width(node) / 2.0,
        y: node.y - node_height(node) / 2.0,
        width: node_width(node), height: node_height(node),
        rx: if (node.shape == "rounded") 5.0 else 0.0,
        ry: if (node.shape == "rounded") 5.0 else 0.0,
        fill: "white", stroke: "black", 'stroke-width': 1.2>
}

pn layout(parent, children, ctx) any^ {
    let nodes = measured_nodes(children)
    if (len(nodes) == 0) raise error("named TikZ picture has no nodes")
    let bounds = node_bounds(nodes)
    let dx = MARGIN - bounds.left
    let dy = MARGIN - bounds.top
    let placed = shifted_nodes(nodes, dx, dy)
    let edges = [for (child in children
        where child.attrs["data-tikz-kind"] == "edge") edge_geometry(child, placed)^]
    let width = bounds.right - bounds.left + 2.0 * MARGIN
    let height = bounds.bottom - bounds.top + 2.0 * MARGIN
    let edge_shapes = [for (edge in edges) edge_svg(edge)]
    let shape_elements = [for (node in placed where node.draw) shape_svg(node)]
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
        style: "overflow:visible;pointer-events:none;",
        for (item in [*edge_shapes, *shape_elements]) item
    >
    return {width: width, height: height,
        placements: [for (index, child in children) child_placement(index, placed)],
        paint_layers: [{z: -1, content: graphic}]}
}

pub fn render(picture) any^ {
    let children = [for (child in picture where child is element) child]
    let nodes = [for (child in children where string(name(child)) == "node") child]
    let ids = [for (node in nodes where node.id != null) node.id]
    let duplicates = [for (index, id in ids
        where len([for (earlier in slice(ids, 0, index) where earlier == id) earlier]) > 0) id]
    if (len(duplicates) > 0) raise error("duplicate TikZ node: " ++ duplicates[0])
    let references = [for (path in children,
        point in if (string(name(path)) == "path") children_named(path, "point") else []
        where point.ref != null) point.ref]
    let missing = [for (ref in references
        where len([for (id in ids where id == ref) id]) == 0) ref]
    if (len(missing) > 0) raise error("unknown TikZ node: " ++ missing[0])
    let installed = radiant.register_layout("lambda-tikz", layout);
    <span class: "tikz-picture tikz-named-picture",
        'data-radiant-layout': "lambda-tikz",
        style: "position:relative;display:inline-block;vertical-align:bottom;",
        for (child in children) child_html(child)^
    >
}
