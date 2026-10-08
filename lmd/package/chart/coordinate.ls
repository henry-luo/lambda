// Normalized spatial grammar; rendering and inverse picking use the same model.
import util: .util
import parse: .parse
import svg: .svg
import scale: .scale
import cfg: .config
import paths: .path_geometry
import geometry: .geometry

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
    let result = if (step.type == "transpose") reverse(point)
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
    let singular = any([for (step in model.transform where step.type == "scale")
        contains(value(step, "factors", [value(step, "x", 1.0), value(step, "y", 1.0)]), 0)]);
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
pub fn warp(node, model, marker = false) {
    if (not (node is element) or node is error) node else {
        let tag = name(node); let attrs = map(node);
        let marker = marker or node.class == "marks points" or node.class == "marks beeswarm";
        let children = [for (child in content(node)) warp(child, model, marker)];
        let center = if (tag == 'circle' or tag == 'ellipse') [node.cx, node.cy] else [node.x, node.y];
        let projected = if (center[0] != null and center[1] != null) pixel(model, center) else null;
        if (tag == 'circle') <circle *:attrs, cx: projected[0], cy: projected[1], *children>
        else if (tag == 'ellipse') <ellipse *:attrs, cx: projected[0], cy: projected[1], *children>
        else if (tag == 'text' or tag == 'image' or (tag == 'rect' and marker))
            element(tag, {*:attrs, x: projected[0], y: projected[1]}, children)
        else if (tag == 'rect') (
            let d = projected_path(model, [[node.x, node.y], [node.x + node.width, node.y],
                [node.x + node.width, node.y + node.height], [node.x, node.y + node.height]], true),
            if (d is error) d else <path *:attrs, d: d, *children>)
        else if (tag == 'line') (
            let d = projected_path(model, [[node.x1, node.y1], [node.x2, node.y2]]),
            if (d is error) d else <path *:attrs, d: d, *children>)
        else if (tag == 'path' and not marker) (
            let lines = paths.sample(node.d, (point) => pixel(model, point), model.precision, intervals(model)),
            if (lines is error) lines else <path *:attrs, d: paths.path(lines), *children>)
        else element(tag, attrs, children)
    }
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
    svg.group_class("coordinate-guides", [for (key in ["x", "y"],
        let mapping = if (key == "x") x_scale else y_scale,
        let channel = encoding[key], let guide = cfg.axis_config(theme, channel)
        where mapping != null and guide.enabled) (
        let ticks = scale.scale_ticks(mapping, if (guide.tick_count != null) guide.tick_count else 5),
        let baseline = if (key == "x") [[0.0, model.height], [model.width, model.height]] else [[0.0, 0.0], [0.0, model.height]],
        <path class: "coordinate-axis", d: projected_path(model, baseline), fill: "none", stroke: "#888">,
        for (tick in ticks,
            let value = scale.scale_apply(mapping, tick) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0),
            let origin = if (key == "x") [value, model.height] else [0.0, value],
            let point = pixel(model, origin)) (
            if (guide.grid != false) <path class: "coordinate-grid", d: projected_path(model,
                if (key == "x") [[value, 0.0], [value, model.height]] else [[0.0, value], [model.width, value]]),
                fill: "none", stroke: if (guide.grid_color != null) guide.grid_color else "#ddd">,
            <text class: "coordinate-label", x: point[0] + (if (key == "y") -5 else 0), y: point[1] + (if (key == "x") 12 else 0),
                'text-anchor': if (key == "x") "middle" else "end", 'font-size': if (guide.label_font_size != null) guide.label_font_size else 11,
                fill: "#333", util.format_value(tick, if (guide.format != null) guide.format else channel.format, channel.dtype, channel.scale.timezone)>))])
}
