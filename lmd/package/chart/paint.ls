// compile declarative paints once per view; all marks and legends share the same resources.
import util: .util
import svg: .svg

fn option(value, key, fallback) => if (value[key] != null) value[key] else fallback
fn unit(value) => util.finite_number(value) and value >= 0 and value <= 1
fn color(value) => value is string and len(trim(value)) > 0

fn normalize_paint(value) {
    if (value.gradient == "linear" or value.gradient == "radial") {
        let radial = value.gradient == "radial";
        let coordinates = [for (key in ["x1", "y1", "x2", "y2"])
            option(value, key, if (radial) 0.5 else if (key == "x2") 1.0 else 0.0)];
        let r1 = option(value, "r1", 0.0);
        let r2 = option(value, "r2", 0.5);
        let spread = option(value, "spread", "pad");
        let stops = value.stops;
        if (not (stops is array) or len(stops) == 0)
            error("chart: gradient stops must be a nonempty array")
        else if (len([for (stop in stops where not (stop is map) or not unit(stop.offset) or
            not color(stop.color) or not unit(option(stop, "opacity", 1.0))) true]) > 0)
            error("chart: gradient stops require offsets and opacity in [0, 1] and color strings")
        else if (len([for (index, stop in stops where index > 0 and stop.offset < stops[index - 1].offset) true]) > 0)
            error("chart: gradient stops must be ordered by offset")
        else if (len(coordinates |: not util.finite_number(~)) > 0 or
            (radial and (not util.finite_number(r1) or not util.finite_number(r2) or r1 < 0 or r2 < r1)))
            error("chart: invalid gradient coordinates or radii")
        else if (not (spread in ["pad", "repeat", "reflect"])) error("chart: invalid gradient spread")
        else {gradient: value.gradient, x1: coordinates[0], y1: coordinates[1],
            x2: coordinates[2], y2: coordinates[3], r1: r1, r2: r2, spread: spread,
            stops: [for (stop in stops) {offset: stop.offset, color: stop.color, opacity: option(stop, "opacity", 1.0)}]}
    } else if (value.pattern == "hatch") {
        let spacing = option(value, "spacing", 8.0);
        let width = option(value, "stroke_width", 1.0);
        let angle = option(value, "angle", 45.0);
        let stroke = option(value, "color", "#888");
        let opacity = option(value, "opacity", 1.0);
        let cross = option(value, "cross", false);
        if (not util.finite_number(spacing) or spacing <= 0 or not util.finite_number(width) or width < 0 or
            not util.finite_number(angle) or not color(stroke) or not unit(opacity) or not (cross is bool) or
            (value.background != null and not color(value.background))) error("chart: invalid hatch pattern")
        else {pattern: "hatch", spacing: spacing, stroke_width: width, angle: angle, color: stroke,
            opacity: opacity, cross: cross, background: value.background}
    } else error("chart: paint must be a color string, a linear/radial gradient, or a hatch pattern")
}

fn definition(id, paint) {
    if (paint.gradient == "linear") svg.linear_gradient(id, paint.x1, paint.y1, paint.x2, paint.y2,
        paint.stops, {gradientUnits: "objectBoundingBox", spreadMethod: paint.spread})
    else if (paint.gradient == "radial") svg.radial_gradient(id, paint.x2, paint.y2, paint.r2,
        paint.x1, paint.y1, paint.r1, paint.stops,
        {gradientUnits: "objectBoundingBox", spreadMethod: paint.spread})
    else {
        let size = paint.spacing;
        // opposite tile edges each contribute half a stripe, avoiding seams when repeated.
        let vertical = svg.M(0, 0) ++ " " ++ svg.L(0, size) ++ " " ++ svg.M(size, 0) ++ " " ++ svg.L(size, size);
        let horizontal = if (paint.cross) " " ++ svg.M(0, 0) ++ " " ++ svg.L(size, 0) ++
            " " ++ svg.M(0, size) ++ " " ++ svg.L(size, size) else "";
        <pattern id: id, patternUnits: "userSpaceOnUse", width: size, height: size,
            patternTransform: "rotate(" ++ util.fmt_num(paint.angle) ++ ")",
            (if (paint.background != null) <rect width: size, height: size, fill: paint.background>);
            <path d: vertical ++ horizontal, fill: "none", stroke: paint.color,
                'stroke-width': paint.stroke_width, 'stroke-opacity': paint.opacity>>
    }
}

fn resource_key(paint) string | error {
    let key = if (paint.gradient != null) [paint.gradient, paint.x1, paint.y1, paint.x2, paint.y2,
        paint.r1, paint.r2, paint.spread, [for (stop in paint.stops) [stop.offset, stop.color, stop.opacity]]]
        else [paint.pattern, paint.spacing, paint.stroke_width, paint.angle, paint.color,
            paint.opacity, paint.cross, paint.background];
    util.resource_key(key)
}

pub fn plan(values, scope = "chart") {
    let sources = util.unique_vals(values |: ~ is map);
    let normalized = [for (value in sources) normalize_paint(value)];
    let keys = [for (paint in normalized) if (paint is error) paint else resource_key(paint)];
    let failure = util.first_error([*normalized, *keys,
        if (len(values |: ~ != null and not (~ is map) and not (~ is string)) > 0)
            error("chart: paint must be a color string or paint map")]);
    // a composition assigns a distinct scope to each view; identical paints share one definition within it.
    let items = if (failure is error) [] else [for (index, paint in normalized)
        {source: sources[index], id: scope ++ "-paint-" ++ keys[index], paint: paint}];
    {items: items, _error: failure}
}

pub fn value(value, plan) {
    if (not (value is map)) value
    else if (plan._error is error) plan._error else {
        let matches = plan.items |: ~.source == value;
        if (len(matches) > 0) "url(#" ++ matches[0].id ++ ")"
        else error("chart: unresolved paint")
    }
}

pub fn definitions(plan) => if (len(plan.items) > 0)
    svg.defs([for (group in util.group_by(plan.items, "id")) definition(group.key, group.items[0].paint)]) else null
