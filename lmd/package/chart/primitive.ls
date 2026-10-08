// Endpoint and authored geometry marks use the ordinary appearance/source-record contract.
import mark: .mark
import svg: .svg
import util: .util
import parse: .parse
import color: .color

pub fn link_path(a, b, options = {}) {
    let horizontal = options.orientation != "vertical";
    let mid = if (horizontal) (a[0] + b[0]) / 2.0 else (a[1] + b[1]) / 2.0;
    if (options.curve == "orthogonal" or options.shape == "orthogonal") svg.line_path([a,
        if (horizontal) [mid, a[1]] else [a[0], mid], if (horizontal) [mid, b[1]] else [b[0], mid], b])
    else if (options.curve == "curve" or options.curve == "curved" or options.shape == "curve")
        svg.M(a[0], a[1]) ++ " " ++ (if (horizontal) svg.C(mid, a[1], mid, b[1], b[0], b[1])
            else svg.C(a[0], mid, b[0], mid, b[0], b[1]))
    else svg.line_path([a, b])
}

pub fn render(data, ctx, options) {
    let kind = options.kind;
    let items = if (kind == "polygon") [for (group in mark.series(data, ctx, color.default_color),
        let points = [for (row in group.items) [mark.center_coordinate(ctx, "x", row), mark.center_coordinate(ctx, "y", row)]],
        let holes = group.items[0][if (options.holes_field != null) options.holes_field else "holes"],
        let valid = all([for (point in points) all(point |> util.finite_number(~))]))
        if (not valid or len(points) < 3) error("chart: polygon requires at least three finite vertices")
        else <path class: "polygon", d: svg.line_path(points) ++ " Z" ++ join([for (ring in holes) " " ++ svg.line_path(ring) ++ " Z"], ""),
            'fill-rule': "evenodd", *:mark.style(ctx, group.items[0], options,
                {fill: group.color, stroke: "none", 'stroke-width': 1, opacity: 1.0}), mark.tooltip(ctx, group.items[0])>]
    else [for (row in data) (
        let a = [mark.center_coordinate(ctx, "x", row), mark.center_coordinate(ctx, "y", row)],
        let direction = parse.channel_value(ctx.encoding.direction, row, row[if (options.direction_field != null) options.direction_field else "direction"]),
        let magnitude = parse.channel_value(ctx.encoding.magnitude, row, row[if (options.magnitude_field != null) options.magnitude_field else "magnitude"]),
        let b = if (kind == "vector") [a[0] + magnitude * math.cos(direction), a[1] - magnitude * math.sin(direction)]
            else [mark.coordinate(ctx, "x2", row), mark.coordinate(ctx, "y2", row)],
        let d = if (kind == "path") parse.channel_value(ctx.encoding.path, row,
            if (options.d != null) options.d else row[if (options.path_field != null) options.path_field else "path"]) else link_path(a, b, options),
        let appearance = mark.style(ctx, row, options, {fill: "none", stroke: color.default_color,
            'stroke-width': if (options.width != null) options.width else mark.appearance(ctx, "size", row, 1.5), opacity: 1.0}, true),
        if (kind == "path" and not (d is string)) error("chart: authored path requires SVG path text")
        else if (kind != "path" and (not all(a |> util.finite_number(~)) or not all(b |> util.finite_number(~))))
            error("chart: link/vector requires finite endpoint positions")
        else <g class: kind, <path d: d, *:appearance, mark.tooltip(ctx, row)>
            if (kind == "vector" or options.arrow == true) svg.arrow_head(a[0], a[1], b[0], b[1], appearance.stroke,
                if (options.arrow_size != null) options.arrow_size else 8.0)>)];
    let failure = util.first_error(items);
    if (failure is error) failure else svg.group_class("marks " ++ kind, items)
}
