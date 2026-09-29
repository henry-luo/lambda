// Public native TikZ entry point; graphics islands are parsed as data.
import opts: .options
import labels: .labels
import plots: .pgfplots
import named: .named
import svg: lambda.chart.svg
import math_css: lambda.doc.math.css

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
        "darkgreen", "purple", "gray", "color", "fill", "thick", "very thick"])^
    let color = opts.color(path, "black")^
    let points = children_named(path, "point")
    let mapped = [for (point in points)
        [left + (float(point.x) - min_x) * px_per_cm,
         top + (max_y - float(point.y)) * px_per_cm]];
    let stroke_width = if (opts.has(path, "very thick")) 1.7
        else if (opts.has(path, "thick")) 1.1 else 0.6
    let fill = if (opts.has(path, "fill")) color else "none"
    if (path.shape == "rectangle") {
        let x = min([mapped[0][0], mapped[1][0]])
        let y = min([mapped[0][1], mapped[1][1]])
        let width = abs(mapped[1][0] - mapped[0][0])
        let height = abs(mapped[1][1] - mapped[0][1]);
        <rect x: x, y: y, width: width, height: height,
            fill: fill, stroke: color, 'stroke-width': stroke_width>
    } else if (path.shape == "ellipse")
        <ellipse cx: mapped[0][0], cy: mapped[0][1],
            rx: float(path.rx) * px_per_cm, ry: float(path.ry) * px_per_cm,
            fill: fill, stroke: color, 'stroke-width': stroke_width>
    else <path d: svg.line_path(mapped), fill: fill, stroke: color,
        'stroke-width': stroke_width>
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
    let ellipses = [for (node in nodes where node.shape == "ellipse") node]
    let ellipse_x = [for (ellipse in ellipses,
        point in children_named(ellipse, "point")) [
            float(point.x) - float(ellipse.rx), float(point.x) + float(ellipse.rx)]]
    let ellipse_y = [for (ellipse in ellipses,
        point in children_named(ellipse, "point")) [
            float(point.y) - float(ellipse.ry), float(point.y) + float(ellipse.ry)]]
    let x_values = [for (point in [*path_coordinates, *node_coordinates]) float(point.x),
        for (bounds in ellipse_x, edge in bounds) float(edge)]
    let y_values = [for (point in [*path_coordinates, *node_coordinates]) float(point.y),
        for (bounds in ellipse_y, edge in bounds) float(edge)]
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

fn render_picture(picture, options) any^ {
    let tag = string(name(picture))
    let valid = if (tag == "tikzpicture") opts.check(picture, [">"])^
        else if (tag == "tikz_picture") true
        else raise error("expected TikZ picture")
    let arrow_tip = opts.value(picture, ">", null)
    if (arrow_tip != null and arrow_tip != "stealth")
        raise error("unsupported TikZ arrow tip")
    let axes = [for (child in picture
        where child is element and
            (string(name(child)) == "axis" or
             string(name(child)) == "semilogxaxis" or
             string(name(child)) == "semilogyaxis" or
             string(name(child)) == "loglogaxis" or
             string(name(child)) == "polaraxis")) child]
    let paths = drawing_nodes(picture)
    let content = [for (child in picture where child is element and
        string(name(child)) != "option") child]
    let named_refs = [for (child in paths,
        point in if (string(name(child)) == "path") children_named(child, "point") else []
        where point.ref != null) point]
    let named_nodes = [for (child in paths where string(name(child)) == "node" and
        (opts.has(child, "draw") or opts.has(child, "diamond") or
         len([for (reference in named_refs where reference.ref == child.id) reference]) > 0)) child]
    if (len(axes) == 1 and len(paths) == 0 and len(content) == 1)
        plots.render_axis(axes[0], options)^
    else if (len(axes) == 0 and len(paths) > 0 and len(paths) == len(content))
        if (len(named_nodes) > 0 or len(named_refs) > 0)
            named.render(picture)^
        else render_drawing(picture)^
    else raise error("mixed or scoped TikZ pictures are not supported yet")
}

fn render_parsed(parsed, options = null) any^ {
    let wrapper = children_named(parsed, "tikzpicture")
    if (len(wrapper) == 0) render_picture(parsed, options)^
    else if (len(wrapper) == 1) render_picture(wrapper[0], options)^
    else <div class: "tikz-fragment-gallery",
        for (picture in wrapper)
            <div style: "margin-bottom:20px;", render_picture(picture, options)^>
    >
}

pub fn render(source) any^ {
    render_parsed(parse(source, {type: "tikz"})^, null)^
}

// Direct .pgf documents use the parsed TikZ tree, including multiple pictures.
pub fn render_document(ast, options) any^ {
    let graphic = render_parsed(ast, options)^
    let math_stylesheet = math_css.get_stylesheet(options);
    <html lang: "en",
        <head
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title "TikZ/PGF Picture">
            <style math_stylesheet>
        >
        <body style: "margin:0;padding:24px;background:white;color:#222;" ++
            "font-family:Georgia,serif;",
            graphic>
    >
}

pub fn render_ast(graphics_island) any^ {
    if (graphics_island == null or graphics_island.raw_source == null)
        raise error("TikZ graphics island has no preserved source")
    render(graphics_island.raw_source)^
}
