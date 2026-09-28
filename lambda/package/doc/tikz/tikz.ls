// Public native TikZ entry point; graphics islands are parsed as data.
import opts: .options
import labels: .labels
import plots: .pgfplots
import named: .named
import svg: lambda.chart.svg

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn drawing_nodes(picture) => [for (child in picture
    where child is element and
        (string(name(child)) == "path" or string(name(child)) == "node")) child]

fn path_points(nodes) => [for (node in nodes,
    point in if (string(name(node)) == "path") node else []
    where point is element and string(name(point)) == "point") point]

fn node_points(nodes) => [for (node in nodes
    where string(name(node)) == "node") {x: node.x, y: node.y}]

fn draw_path(path, min_x, max_y, left, top, px_per_cm) any^ {
    if (path.action != "draw")
        raise error("only drawn TikZ paths are supported")
    let checked = opts.check(path, ["black", "blue", "red", "green", "orange",
        "purple", "gray", "color"])^
    let color = opts.color(path, "black")^
    let points = children_named(path, "point")
    let mapped = [for (point in points)
        [left + (float(point.x) - min_x) * px_per_cm,
         top + (max_y - float(point.y)) * px_per_cm]];
    <path d: svg.line_path(mapped), fill: "none", stroke: color,
        'stroke-width': 1.2>
}

fn draw_paths(nodes, index, min_x, max_y, left, top, px_per_cm, acc) any^ {
    if (index >= len(nodes)) acc
    else {
        let child = nodes[index]
        let rendered = if (string(name(child)) == "path")
            draw_path(child, min_x, max_y, left, top, px_per_cm)^
        else null
        let next = if (rendered == null) acc else [*acc, rendered]
        draw_paths(nodes, index + 1, min_x, max_y, left, top, px_per_cm, next)^
    }
}

fn positioned_nodes(nodes, index, min_x, max_y, left, top, px_per_cm, acc) any^ {
    if (index >= len(nodes)) acc
    else {
        let child = nodes[index]
        let rendered = if (string(name(child)) == "node") {
            let checked = opts.check(child, [])^
            labels.positioned(labels.prepare(child.source)^,
                left + (float(child.x) - min_x) * px_per_cm,
                top + (max_y - float(child.y)) * px_per_cm)
        } else null
        let next = if (rendered == null) acc else [*acc, rendered]
        positioned_nodes(nodes, index + 1, min_x, max_y, left, top, px_per_cm, next)^
    }
}

fn render_drawing(picture) any^ {
    let nodes = drawing_nodes(picture)
    let path_coordinates = path_points(nodes)
    let node_coordinates = node_points(nodes)
    let x_values = [for (point in [*path_coordinates, *node_coordinates]) float(point.x)]
    let y_values = [for (point in [*path_coordinates, *node_coordinates]) float(point.y)]
    if (len(x_values) == 0) raise error("TikZ picture has no drawable coordinates")
    let min_x = float(min(x_values))
    let max_x = float(max(x_values))
    let min_y = float(min(y_values))
    let max_y = float(max(y_values))
    let px_per_cm = 96.0 / 2.54
    let left = 24.0
    let top = 24.0
    let width = (max_x - min_x) * px_per_cm + left * 2.0
    let height = (max_y - min_y) * px_per_cm + top * 2.0
    let paths = draw_paths(nodes, 0, min_x, max_y, left, top, px_per_cm, [])^
    let label_elements = positioned_nodes(nodes, 0, min_x, max_y,
        left, top, px_per_cm, [])^
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
        for (path in paths) path>
    let style = "position:relative;display:inline-block;width:" ++ string(width) ++
        "px;height:" ++ string(height) ++ "px;vertical-align:bottom;";
    <span class: "tikz-picture", style: style,
        graphic
        for (label in label_elements) label
    >
}

fn render_picture(picture) any^ {
    let tag = string(name(picture))
    let valid = if (tag == "tikzpicture") opts.check(picture, [])^
        else if (tag == "tikz_picture") true
        else raise error("expected TikZ picture")
    let axes = [for (child in picture
        where child is element and
            (string(name(child)) == "axis" or
             string(name(child)) == "semilogxaxis" or
             string(name(child)) == "semilogyaxis" or
             string(name(child)) == "loglogaxis")) child]
    let paths = drawing_nodes(picture)
    let named_nodes = [for (child in paths where string(name(child)) == "node" and
        (child.id != null or opts.has(child, "draw") or opts.has(child, "diamond"))) child]
    let named_refs = [for (child in paths,
        point in if (string(name(child)) == "path") children_named(child, "point") else []
        where point.ref != null) point]
    if (len(axes) == 1 and len(paths) == 0 and len(picture) == 1)
        plots.render_axis(axes[0])^
    else if (len(axes) == 0 and len(paths) > 0 and len(paths) == len(picture))
        if (len(named_nodes) > 0 or len(named_refs) > 0)
            named.render(picture)^
        else render_drawing(picture)^
    else raise error("mixed or scoped TikZ pictures are not supported yet")
}

pub fn render(source) any^ {
    let parsed = parse(source, {type: "tikz"})^
    let wrapper = children_named(parsed, "tikzpicture")
    if (len(wrapper) == 1 and len(parsed) == 1) render_picture(wrapper[0])^
    else render_picture(parsed)^
}

pub fn render_ast(graphics_island) any^ {
    if (graphics_island == null or graphics_island.raw_source == null)
        raise error("TikZ graphics island has no preserved source")
    render(graphics_island.raw_source)^
}
