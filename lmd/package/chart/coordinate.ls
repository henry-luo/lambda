// Normalized spatial grammar; rendering and inverse picking use the same model.
import util: .util
import parse: .parse
import svg: .svg
import scale: .scale
import cfg: .config
import paths: .path_geometry
import geometry: .geometry
import mark: .mark
import text: .text
import color: .color
import affine: .svg_transform
import axis:.axis

pub fn enabled(options) => options is string or (options != null and len(parse.attributes(options)) > 0)
fn pair(value) => (value is array and len(value) == 2 and all(value |> util.finite_number(~))) or false
fn value(options, key, fallback) => if (options[key] != null) options[key] else fallback

fn transform_error(step) {
    let kind = step.type;
    let center = value(step, "center", [0.5, 0.5]);
    if (not contains(["transpose", "reflect_x", "reflect_y", "rotate", "scale", "translate", "fisheye"], kind))
        error("chart: unsupported coordinate transform")
    else if (not pair(center)) error("chart: coordinate center must contain two finite values")
    else if (kind == "rotate" and not util.finite_number(step.angle)) error("chart: coordinate rotation requires a finite angle")
    else if (kind == "scale" and not pair(value(step, "factors", [value(step, "x", 1.0), value(step, "y", 1.0)])))
        error("chart: coordinate scaling requires two finite factors")
    else if (kind == "translate" and not pair(value(step, "offset", [value(step, "x", 0.0), value(step, "y", 0.0)])))
        error("chart: coordinate translation requires two finite offsets")
    else if (kind == "fisheye" and (not pair(value(step, "focus", [0.5, 0.5])) or
        any(value(step, "focus", [0.5, 0.5]) |> ~ < 0 or ~ > 1) or
        not util.finite_number(value(step, "distortion", 2.0)) or value(step, "distortion", 2.0) < 0))
        error("chart: fisheye requires a normalized focus and nonnegative distortion")
    else if (step.dimensions != null and (not (step.dimensions is array) or
        any(step.dimensions |> not contains(["x", "y"], ~)))) error("chart: coordinate transform dimensions must be x or y")
    else null
}

pub fn configure(options, width, height) {
    let settings = if (options is string) {type: options} else parse.attributes(options);
    let model = {type: "cartesian", width: width, height: height, inner_radius: 0.0, outer_radius: 1.0,
        start_angle: -util.PI / 2.0, end_angle: 3.0 * util.PI / 2.0, precision: 0.75,
        turns: [0.0, 3.0], track_width: 0.1, transform: [], *:settings};
    let invalid = util.first_error([
        if (not contains(["cartesian", "polar", "theta", "radial", "parallel", "radar", "helix", "geo"], model.type))
            error("chart: unsupported coordinate family"),
        if (not util.finite_number(width) or width <= 0 or not util.finite_number(height) or height <= 0)
            error("chart: coordinate requires positive finite dimensions"),
        if (not util.finite_number(model.inner_radius) or not util.finite_number(model.outer_radius) or
            model.inner_radius < 0 or model.inner_radius >= model.outer_radius or model.outer_radius > 1)
            error("chart: coordinate radii must satisfy 0 <= inner < outer <= 1"),
        if (not util.finite_number(model.start_angle) or not util.finite_number(model.end_angle) or model.start_angle == model.end_angle or
            not util.finite_number(model.precision) or model.precision <= 0) error("chart: coordinate requires finite distinct angles and positive precision"),
        if (model.type == "helix" and (not pair(model.turns) or model.turns[0] >= model.turns[1] or
            not util.finite_number(model.track_width) or model.track_width <= 0 or model.track_width > model.outer_radius - model.inner_radius))
            error("chart: helix requires increasing turns and a positive bounded track width"),
        if (not (model.transform is array)) error("chart: coordinate transforms must be an ordered array")
            else for (step in model.transform) transform_error(step)]);
    if (invalid is error) invalid else model
}

fn fisheye(x, focus, distortion, inverse) {
    let side = if (x < focus) focus else 1.0 - focus;
    let t = if (side > 0) abs(x - focus) / side else 0.0;
    let mapped = if (inverse) t / (distortion + 1.0 - distortion * t) else (distortion + 1.0) * t / (distortion * t + 1.0);
    focus + (if (x < focus) -1.0 else 1.0) * side * mapped
}

fn transform(point, step, inverse = false) {
    let center = value(step, "center", [0.5, 0.5]);
    let angle = value(step, "angle", 0.0) * (if (inverse) -1.0 else 1.0);
    let factors = value(step, "factors", [value(step, "x", 1.0), value(step, "y", 1.0)]);
    let offset = value(step, "offset", [value(step, "x", 0.0), value(step, "y", 0.0)]);
    let focus = value(step, "focus", [0.5, 0.5]);
    let x = point[0] - center[0]; let y = point[1] - center[1];
    let dimensions=if (step.dimensions!=null) step.dimensions else ["x","y"];
    let partial=contains(dimensions,"x") != contains(dimensions,"y");
    let result = if (inverse and step.type=="rotate" and partial)
        if (contains(dimensions,"x")) [center[0]+(x-y*math.sin(angle))/math.cos(angle),point[1]]
        else [point[0],center[1]+(y+x*math.sin(angle))/math.cos(angle)]
        else if (step.type == "transpose") reverse(point)
        else if (step.type == "reflect_x") [1.0 - point[0], point[1]]
        else if (step.type == "reflect_y") [point[0], 1.0 - point[1]]
        else if (step.type == "rotate") [center[0] + x * math.cos(angle) - y * math.sin(angle), center[1] + x * math.sin(angle) + y * math.cos(angle)]
        else if (step.type == "scale") [for (axis in [0, 1]) center[axis] + (point[axis] - center[axis]) *
            (if (inverse) 1.0 / factors[axis] else factors[axis])]
        else if (step.type == "translate") [for (axis in [0, 1]) point[axis] + offset[axis] * (if (inverse) -1.0 else 1.0)]
        else [for (axis in [0, 1]) fisheye(point[axis], focus[axis], value(step, "distortion", 2.0), inverse)];
    [for (axis in [0, 1]) if (step.dimensions == null or contains(step.dimensions, if (axis == 0) "x" else "y")) result[axis] else point[axis]]
}

fn transforms(point, steps, index = 0, inverse = false) => if (index >= len(steps)) point else
    transforms(transform(point, steps[index], inverse), steps, index + 1, inverse)

pub fn project(model, normalized) {
    let point = transforms(normalized, model.transform);
    let swapped = contains(["theta", "radial"], model.type) and model.direct_polar != true;
    let u = point[if (swapped) 1 else 0]; let v = point[if (swapped) 0 else 1];
    let angle = if (model.type == "helix") model.start_angle + util.TAU * util.lerp(model.turns[0], model.turns[1], u)
        else util.lerp(model.start_angle, model.end_angle, u);
    let radius = min(model.width, model.height) / 2.0 * (if (model.type == "helix") model.inner_radius +
        (model.outer_radius - model.inner_radius - model.track_width) * u + model.track_width * v
        else util.lerp(model.inner_radius, model.outer_radius, v));
    if (model is error) model else if (model.type == "cartesian") [point[0] * model.width, (1.0 - point[1]) * model.height]
    else [model.width / 2.0 + radius * math.cos(angle), model.height / 2.0 + radius * math.sin(angle)]
}

pub fn invert(model, displayed) {
    let singular = any([for (step in model.transform,let dimensions=if (step.dimensions!=null) step.dimensions else ["x","y"],
        let partial=contains(dimensions,"x") != contains(dimensions,"y"))
        if (step.type=="scale") any([for (i,key in ["x","y"] where contains(dimensions,key))
            value(step,"factors",[value(step,"x",1.0),value(step,"y",1.0)])[i]==0])
        else partial and (step.type=="transpose" or step.type=="rotate" and abs(math.cos(step.angle))<0.000000000001)]);
    let dx = displayed[0] - model.width / 2.0; let dy = displayed[1] - model.height / 2.0;
    let angle = math.atan2(dy, dx);
    let span = model.end_angle - model.start_angle;
    let turns = (angle - model.start_angle) / util.TAU;
    let wrapped = turns - floor(turns);
    let u = wrapped * util.TAU / abs(span);
    let radius = math.sqrt(dx * dx + dy * dy) / (min(model.width, model.height) / 2.0);
    let point = if (model.type == "cartesian") [displayed[0] / model.width, 1.0 - displayed[1] / model.height]
        else [if (span > 0) u else (if (u == 0) 0.0 else util.TAU / abs(span) - u),
            util.inv_lerp(model.inner_radius, model.outer_radius, radius)];
    let unswapped = if (contains(["theta", "radial"], model.type) and model.direct_polar != true) reverse(point) else point;
    if (model is error) model else if (singular or contains(["parallel", "radar", "helix", "geo"], model.type))
        error("chart: coordinate does not provide a unique two-dimensional inverse")
    else transforms(unswapped, reverse(model.transform), 0, true)
}

pub fn pixel(model, point) => project(model, [point[0] / model.width, 1.0 - point[1] / model.height])

fn intervals(model) => if (model.type == "helix") max(8, int(ceil((model.turns[1] - model.turns[0]) * 8.0))) else 8
pub fn line(model, points, closed = false) {
    let segments = [for (i in 0 to (len(points) - (if (closed) 1 else 2)),
        let a = points[i], let b = points[(i + 1) % len(points)])
        paths.sample_curve((t) => pixel(model, geometry.interpolate(a, b, t)), model.precision, intervals(model))];
    let invalid = util.first_error(segments);
    if (invalid is error) invalid else [for (i, segment in segments) for (j, point in segment where i == 0 or j > 0) point]
}

fn projected_path(model, points, closed = false) {
    let projected = line(model, points, closed);
    if (projected is error) projected else svg.line_path(projected) ++ (if (closed) " Z" else "")
}

// Mark sizes and glyphs stay in pixels; ranged regions project their complete boundary.
pub fn warp(node, model, marker = false, inherited = affine.identity) {
    if (not (node is element) or node is error) node else {
        let tag = name(node);
        let attrs = map([for (key, value in map(node) where string(key) != "transform") for (part in [string(key), value]) part]);
        let local = affine.matrix(node.transform);
        let matrix = if (local is error) local else affine.multiply(inherited, local);
        let marker = marker or node.class == "marks points" or node.class == "marks beeswarm";
        let children = [for (child in content(node)) warp(child, model, marker, matrix)];
        let center = if (tag == 'circle' or tag == 'ellipse') [node.cx, node.cy]
            else if (tag == 'rect' and marker) [node.x + node.width / 2.0, node.y + node.height / 2.0] else [node.x, node.y];
        let projected = if (center[0] != null and center[1] != null) pixel(model, affine.project(matrix, center)) else null;
        let invalid = util.first_error([matrix, *children]);
        if (invalid is error) invalid
        else if (tag == 'circle') <circle *:attrs, cx: projected[0], cy: projected[1], *children>
        else if (tag == 'ellipse') <ellipse *:attrs, cx: projected[0], cy: projected[1], *children>
        else if (tag == 'text' and projected != null) (
            let angle = math.atan2(matrix[1], matrix[0]) * 180.0 / util.PI,
            svg.rebuild(tag, {*:attrs, x: projected[0], y: projected[1],
                *:(if (angle != 0) {transform: "rotate(" ++ util.fmt_num(angle) ++ " " ++ util.fmt_num(projected[0]) ++ " " ++ util.fmt_num(projected[1]) ++ ")"} else {})}, children))
        else if (tag == 'image' and projected != null) (
            let x = pixel(model, affine.project(matrix, [node.x + node.width, node.y])),
            let y = pixel(model, affine.project(matrix, [node.x, node.y + node.height])),
            svg.rebuild(tag, {*:attrs, x: 0.0, y: 0.0, transform: affine.css([
                (x[0] - projected[0]) / node.width, (x[1] - projected[1]) / node.width,
                (y[0] - projected[0]) / node.height, (y[1] - projected[1]) / node.height, projected[0], projected[1]])}, children))
        else if (tag == 'rect' and marker) <rect *:attrs, x: projected[0] - node.width / 2.0, y: projected[1] - node.height / 2.0, *children>
        else if (tag == 'rect' or tag == 'line') (
            let points = if (tag == 'rect') [[node.x, node.y], [node.x + node.width, node.y],
                [node.x + node.width, node.y + node.height], [node.x, node.y + node.height]] else [[node.x1, node.y1], [node.x2, node.y2]],
            let d = projected_path(model, points |> affine.project(matrix, ~), tag == 'rect'),
            if (d is error) d else <path *:attrs,*:(if (tag=='line') {fill:"none"} else {}),d:d,*children>)
        else if (tag == 'path') (
            let source = paths.sample(node.d, (point) array | error => affine.project(matrix, point)),
            let points = [for (line in source) for (point in line.points) point],
            let anchor = if (len(points) > 0) [(min(points |> ~[0]) + max(points |> ~[0])) / 2.0,
                (min(points |> ~[1]) + max(points |> ~[1])) / 2.0] else [0.0, 0.0],
            let displayed = pixel(model, anchor),
            let lines = if (marker) [for (line in source) {*:line, points: [for (point in line.points)
                [displayed[0] + point[0] - anchor[0], displayed[1] + point[1] - anchor[1]]]}]
                else paths.sample(node.d, (point) => pixel(model, affine.project(matrix, point)), model.precision, intervals(model)),
            if (source is error) source else if (lines is error) lines else <path *:attrs, d: paths.path(lines), *children>)
        else svg.rebuild(tag, attrs, children)
    }
}

pub fn clip(image, model, scope = "chart") {
    let rectangle=[[0.0,0.0],[model.width,0.0],[model.width,model.height],[0.0,model.height]];
    let boundary = if (model.type=="geo") svg.line_path(rectangle)++" Z" else projected_path(model, rectangle, true);
    let id = scope ++ "-coordinate-" ++ util.resource_key(model);
    if (boundary is error) boundary else <g class: "coordinate-clip", <defs <clipPath id: id, clipPathUnits: "userSpaceOnUse", <path d: boundary>>>;
        <g 'clip-path': "url(#" ++ id ++ ")", image>>
}

pub fn compatible(kind, model) {
    if (model is error) model
    else if (contains(["treemap", "pack", "force_graph", "wordcloud", "image"], kind) and model.type != "cartesian")
        error("chart: layout-space mark requires a Cartesian coordinate")
    else if (contains(["sankey", "chord", "gauge", "liquid"], kind) and model.type != "cartesian")
        error("chart: composite mark does not support this coordinate override")
    else if (contains(["geoshape", "geo"], kind) and model.type != "geo") error("chart: geographic marks require a geographic coordinate")
    else if (model.type == "geo" and not contains(["geoshape", "geo", "vector"], kind)) error("chart: geographic coordinate requires geographic geometry")
    else null
}

// Explicit coordinate ranges are normalized; the intermediate SVG uses pixel units.
pub fn channel(channel, size, vertical = false) {
    if (channel.scale.range == null) channel else {*:channel,
        scale: {*:parse.attributes(channel.scale), range: [for (v in channel.scale.range) if (vertical) (1.0 - v) * size else v * size]}}
}

pub fn guides(model, encoding, x_scale, y_scale, theme) {
    let guides=[for (key in ["x", "y"],
        let mapping = if (key == "x") x_scale else y_scale,
        let channel = encoding[key], let guide = cfg.axis_config(theme, channel)
        where mapping != null and guide.enabled) (
        let angular=if (contains(["theta","radial"],model.type) and model.direct_polar!=true) "y" else "x",
        let options=if (model.type!="cartesian" and key==angular) {*:guide,orient:if (key=="x") "top" else "right"} else guide,
        let title=if (channel.title!=null) channel.title else channel.field,
        let prepared=axis.prepare(mapping,options,title),
        let ticks=prepared._values,
        let drawn=if (key=="x") axis.x_axis(mapping,model.width,model.height,options,title)
            else axis.y_axis(mapping,model.width,model.height,options,title),
        let projected=if (drawn is error) drawn else warp(drawn,model),
        if (projected is error) projected else <g class:"coordinate-axis",projected>,
        for (tick in ticks,
            let value = scale.scale_apply(mapping, tick) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0))
            if (guide.grid != false) <path class: "coordinate-grid", d: projected_path(model,
                if (key == "x") [[value, 0.0], [value, model.height]] else [[0.0, value], [model.width, value]]),
                fill: "none", stroke: if (guide.grid_color != null) guide.grid_color else "#ddd">)];
    let failure=util.first_error(guides);
    if (failure is error) failure else svg.group_class("coordinate-guides",guides)
}

pub fn position_fields(position, fallback = null) {
    let fields = if (position is array) position else if (position.fields != null) position.fields else fallback;
    [for (field in fields) if (field is string) {field: field, dtype: "quantitative"} else field]
}

// Position-vector marks, axis guides and brushes share these independently scaled field axes.
pub fn vector_axes(data, position, model) {
    let fields = position_fields(position, model.fields);
    let plane = {*:model, type: if (model.type == "parallel") "cartesian" else "polar", direct_polar: true};
    let axes = [for (i, field in fields,
        let u = if (model.type == "parallel") 0.1 + 0.8 * float(i) / max(1.0, float(len(fields) - 1)) else float(i) / len(fields),
        let a = project(plane, [u, if (model.type == "parallel") 0.08 else 0.0]),
        let b = project(plane, [u, if (model.type == "parallel") 0.92 else 0.85]),
        let length = math.sqrt((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2),
        let mapping = scale.configured_scale(data |> ~[field.field], 0.0, length, "linear", field.scale, field.zero == true))
        {*:field, mapping: mapping, a: a, b: b, length: length}];
    let failure = util.first_error([for (axis in axes) axis.mapping,
        if (len(fields) < (if (model.type == "parallel") 2 else 3) or len(util.unique_vals(fields |> ~.field))!=len(fields) or any([for (field in fields) not (field.field is string)]))
            error("chart: position vector requires ordered field definitions"),
        if (any(axes |> not util.finite_number(~.length) or ~.length<=0)) error("chart: position axes must have distinct endpoints"),
        if (any([for (row in data) any([for (field in fields) not util.finite_number(row[field.field])])]))
            error("chart: position vector values must be finite")]);
    if (failure is error) failure else axes
}

pub fn vector_marks(data, ctx, options, model) {
    let axes = vector_axes(data, ctx.encoding.position, model);
    let font = text.style(options, "label", 11);
    let labels = [for (axis in axes) if (axis.title != null) axis.title else axis.field];
    let metrics = text.measure(labels, font);
    let invalid = util.first_error([axes, metrics]);
    if (invalid is error) invalid else svg.group_class(if (model.type == "parallel") "marks coordinate-parallel" else "marks coordinate-radar", [
        if (options.axes != false) for (i, axis in axes) <g 'data-chart-axis': axis.field,
            <line x1: axis.a[0], y1: axis.a[1], x2: axis.b[0], y2: axis.b[1], stroke: "#aaa">;
            <line x1: axis.a[0], y1: axis.a[1], x2: axis.b[0], y2: axis.b[1], stroke: "transparent", 'stroke-width': 12>;
            for (tick in scale.scale_ticks(axis.mapping, 4),
                let point = geometry.interpolate(axis.a, axis.b, scale.scale_apply(axis.mapping, tick) / axis.length))
                <text x: point[0] + 4, y: point[1], *:text.attributes(font), util.format_value(tick, axis.format)>;
            if (options.labels != false) <text x: axis.b[0], y: axis.b[1] - metrics[i].bottom - 5,
                'text-anchor': "middle", *:text.attributes(font), labels[i]>>,
        for (row in data,
            let points = [for (axis in axes) geometry.interpolate(axis.a, axis.b, scale.scale_apply(axis.mapping, row[axis.field]) / axis.length)])
            <path class: if (model.type == "parallel") "parallel-series" else "radar-series",
                d: svg.line_path(points) ++ (if (model.type == "parallel") "" else " Z"),
                *:mark.style(ctx, row, options, {fill: if (options.filled == true) color.default_color else "none",
                    stroke: color.default_color, 'stroke-width': 1.5, opacity: 0.7}, options.filled != true), mark.tooltip(ctx, row)>])
}
