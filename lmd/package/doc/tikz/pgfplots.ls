// PGFPlots coordinate-list axes over the shared chart scale and SVG helpers.
import opts: .options
import labels: .labels
import expression: .expression
import scale: lambda.chart.scale
import chart_axis: lambda.chart.axis
import svg: lambda.chart.svg

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn plotted_points(plot) => children_named(plot, "point")

fn sample_domain(plot, axis_node) any^ {
    let raw = opts.value(plot, "domain", opts.value(axis_node, "domain", "-5:5"))
    let parts = split(raw, ":")
    let valid_parts = if (len(parts) == 2) true
        else raise error("PGFPlots function domain must be min:max")
    let low = opts.numeric_value(parts[0])^
    let high = opts.numeric_value(parts[1])^
    if (low < high) [low, high]
    else raise error("PGFPlots function domain must increase")
}

fn sample_count(plot, axis_node) int^ {
    let raw = opts.value(plot, "samples", opts.value(axis_node, "samples", "25"))
    let count_number = opts.numeric_value(raw)^
    if (count_number < 2.0 or count_number > 1001.0 or
        float(int(count_number)) != count_number)
        raise error("PGFPlots samples must be an integer from 2 to 1001")
    else int(count_number)
}

fn sample_curve(y_tree, x_tree, low, high, count, index, acc) any^ {
    if (index >= count) acc
    else {
        let x = low + (high - low) * float(index) / float(count - 1)
        let x_value = if (x_tree == null) x else expression.evaluate(x_tree, x)^
        let y = expression.evaluate(y_tree, x)^
        if (not (x_value == x_value) or abs(x_value) > 1e100 or
            not (y == y) or abs(y) > 1e100)
            raise error("PGFPlots expression produced a non-finite sample")
        else sample_curve(y_tree, x_tree, low, high, count, index + 1,
            [*acc, {x: x_value, y: y}])^
    }
}

fn resolve_plots(plots, axis_node, index, acc) any^ {
    if (index >= len(plots)) acc
    else {
        let plot = plots[index]
        let points = if (plot.input_kind == "expression" or
                          plot.input_kind == "parametric") {
            let domain = sample_domain(plot, axis_node)^
            let count = sample_count(plot, axis_node)^
            if (plot.input_kind == "parametric") {
                let x_wrappers = children_named(plot, "x_expression")
                let y_wrappers = children_named(plot, "y_expression")
                if (len(x_wrappers) != 1 or len(y_wrappers) != 1)
                    raise error("PGFPlots parametric expression pair is missing")
                else sample_curve(y_wrappers[0][0], x_wrappers[0][0],
                    domain[0], domain[1], count, 0, [])^
            } else {
                let trees = [for (child in plot
                    where child is element and string(name(child)) != "option" and
                        string(name(child)) != "point" and
                        string(name(child)) != "node") child]
                if (len(trees) == 1)
                    sample_curve(trees[0], null, domain[0], domain[1], count, 0, [])^
                else raise error("PGFPlots function tree is missing")
            }
        } else plotted_points(plot)
        resolve_plots(plots, axis_node, index + 1,
            [*acc, {source: plot, points: points}])^
    }
}

fn axis_domain(points, coordinate, low, high) {
    let values = if (coordinate == "x") [for (point in points) float(point.x)]
        else [for (point in points) float(point.y)]
    let lo = if (low != null) float(low) else float(min(values))
    let hi = if (high != null) float(high) else float(max(values))
    if (lo == hi) [lo - 1.0, hi + 1.0] else [lo, hi]
}

fn series_color(plot, index) string^ {
    let cycle = ["blue", "red", "green", "purple", "orange"]
    let checked = opts.check(plot, ["blue", "red", "green", "orange", "purple", "gray",
        "black", "darkgreen", "color", "only marks", "domain", "samples", "mark",
        "thick"])^
    let fallback = cycle[index % len(cycle)]
    opts.color(plot, fallback)^
}

fn point_marker(point, color, mark) {
    if (mark == "x") <path d: svg.M(point[0] - 3.0, point[1] - 3.0) ++ " " ++
            svg.L(point[0] + 3.0, point[1] + 3.0) ++ " " ++
            svg.M(point[0] - 3.0, point[1] + 3.0) ++ " " ++
            svg.L(point[0] + 3.0, point[1] - 3.0),
            fill: "none", stroke: color, 'stroke-width': 1.2>
    else if (mark == "o") <circle cx: point[0], cy: point[1], r: 2.8,
        fill: "white", stroke: color, 'stroke-width': 1.2>
    else svg.circle(point[0], point[1], 2.8, color)
}

fn checked_mark(plot, axis_node) string^ {
    let marks_only = opts.has(plot, "only marks")
    let mark = opts.value(plot, "mark", opts.value(axis_node, "mark",
        if (marks_only) "*" else "none"))
    if (mark == "none" or mark == "x" or mark == "*" or mark == "o") mark
    else raise error("unsupported PGFPlots mark: " ++ mark)
}

fn clip_bound(p, q, interval) {
    if (interval == null) null
    else if (p == 0.0) if (q < 0.0) null else interval
    else {
        let t = q / p
        let lo = if (p < 0.0) max([interval[0], t]) else interval[0]
        let hi = if (p > 0.0) min([interval[1], t]) else interval[1]
        if (lo > hi) null else [lo, hi]
    }
}

fn clipped_segment(a, b, width, height) {
    let dx = b[0] - a[0]
    let dy = b[1] - a[1]
    let left = clip_bound(0.0 - dx, a[0], [0.0, 1.0])
    let right = clip_bound(dx, width - a[0], left)
    let top = clip_bound(0.0 - dy, a[1], right)
    let bottom = clip_bound(dy, height - a[1], top)
    if (bottom == null) null
    else [[a[0] + bottom[0] * dx, a[1] + bottom[0] * dy],
          [a[0] + bottom[1] * dx, a[1] + bottom[1] * dy]]
}

fn clipped_path(points, width, height, index, acc) {
    if (index >= len(points)) acc
    else {
        let segment = clipped_segment(points[index - 1], points[index], width, height)
        let next = if (segment == null) acc else acc ++ " " ++
            svg.M(segment[0][0], segment[0][1]) ++ " " ++
            svg.L(segment[1][0], segment[1][1])
        clipped_path(points, width, height, index + 1, next)
    }
}

fn render_series(series_list, axis_node, index, xs, ys, width, height, acc) any^ {
    if (index >= len(series_list)) acc
    else {
        let plot = series_list[index].source
        let color = series_color(plot, index)^
        let points = series_list[index].points
        let transformed = [for (point in points)
            [float(scale.scale_apply(xs, point.x)),
             float(scale.scale_apply(ys, point.y))]]
        let marks_only = opts.has(plot, "only marks")
        let mark = checked_mark(plot, axis_node)^
        let path_data = if (marks_only) ""
            else clipped_path(transformed, width, height, 1, "")
        let path = if (path_data == "") null
            else <path d: path_data, fill: "none",
                stroke: color, 'stroke-width': if (opts.has(plot, "thick")) 2.1 else 1.5>
        let marks = if (mark == "none") [] else [for (point in transformed
            where point[0] >= 0.0 and point[0] <= width and
                point[1] >= 0.0 and point[1] <= height)
            point_marker(point, color, mark)]
        let next = if (path == null) [*acc, *marks] else [*acc, path, *marks]
        render_series(series_list, axis_node, index + 1, xs, ys, width, height, next)^
    }
}

fn optional_number(node, key) any^ {
    let raw = opts.value(node, key, null)
    if (raw == null) null else opts.numeric_value(raw)^
}

fn label_at(raw, x, y, extra = "") any^ {
    if (raw == null) null
    else labels.positioned(labels.prepare(raw)^, x, y, extra)
}

fn legend_nodes(axis_node) => [for (child in axis_node
    where child is element and string(name(child)) == "legend_entry") child]

fn render_legend(entries, plots, index, left, acc) any^ {
    if (index >= len(entries)) acc
    else {
        let prepared = labels.prepare(entries[index])^
        let color = series_color(plots[index], index)^
        let y = 7.0 + float(index) * 22.0
        let sample = if (opts.has(plots[index], "only marks"))
            <span style: "display:inline-block;width:18px;margin-right:4px;" ++
                "color:" ++ color ++ ";text-align:center;", "●">
            else <span style: "display:inline-block;width:18px;border-top:2px solid " ++
                color ++ ";vertical-align:middle;margin-right:4px;">
        let el = <span class: "tikz-legend-entry",
            style: "position:absolute;left:" ++ string(left) ++
                "px;top:" ++ string(y) ++ "px;white-space:nowrap;",
            sample
            prepared.element
        >
        render_legend(entries, plots, index + 1, left, [*acc, el])^
    }
}

fn polar_point(point, center_x, center_y, radius_px, max_radius) {
    let angle = float(point.x) * 3.141592653589793 / 180.0
    let radius = float(point.y) * radius_px / max_radius;
    [center_x + radius * math.cos(angle), center_y - radius * math.sin(angle)]
}

fn render_polar_series(series_list, index, center_x, center_y,
                       radius_px, max_radius, acc) any^ {
    if (index >= len(series_list)) acc
    else {
        let plot = series_list[index].source
        let color = series_color(plot, index)^
        let points = [for (point in series_list[index].points)
            polar_point(point, center_x, center_y, radius_px, max_radius)]
        let next = [*acc, <path d: svg.line_path(points), fill: "none",
            stroke: color, 'stroke-width': 1.5>]
        render_polar_series(series_list, index + 1, center_x, center_y,
            radius_px, max_radius, next)^
    }
}

fn render_polar_axis(axis_node) any^ {
    let checked = opts.check(axis_node, ["width", "height", "title", "domain", "samples"])^
    let plots = children_named(axis_node, "plot")
    if (len(plots) == 0) raise error("PGFPlots polar axis has no plots")
    let series_list = resolve_plots(plots, axis_node, 0, [])^
    let all_points = [for (series in series_list, point in series.points) point]
    let max_radius = max([for (point in all_points) abs(float(point.y))])
    if (max_radius <= 0.0) raise error("PGFPlots polar radius must be nonzero")
    let width = opts.dimension_px(opts.value(axis_node, "width", "8cm"))^
    let height = opts.dimension_px(opts.value(axis_node, "height", "8cm"))^
    let title_source = opts.value(axis_node, "title", null)
    let title = if (title_source == null) null else labels.plain_title(title_source)^
    let title_band = if (title == null) 0.0 else 32.0
    let center_x = width / 2.0
    let center_y = title_band + height / 2.0
    let radius_px = (min([width, height]) - 48.0) / 2.0
    if (radius_px <= 0.0) raise error("PGFPlots polar axis dimensions are too small")
    let angles = [0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0]
    let radial_grid = [for (angle in angles) {
        x: center_x + radius_px * math.cos(angle * 3.141592653589793 / 180.0),
        y: center_y - radius_px * math.sin(angle * 3.141592653589793 / 180.0)
    }]
    let curves = render_polar_series(series_list, 0, center_x, center_y,
        radius_px, max_radius, [])^
    let total_height = height + title_band;
    <div class: "tikz-axis tikz-polar-axis",
        style: "position:relative;display:inline-block;width:" ++ string(width) ++
            "px;height:" ++ string(total_height) ++ "px;vertical-align:bottom;",
        <svg xmlns: "http://www.w3.org/2000/svg", width: width,
            height: total_height,
            viewBox: "0 0 " ++ string(width) ++ " " ++ string(total_height),
            <circle cx: center_x, cy: center_y, r: radius_px / 2.0,
                fill: "none", stroke: "#ddd", 'stroke-width': 1.0>
            <circle cx: center_x, cy: center_y, r: radius_px,
                fill: "none", stroke: "#aaa", 'stroke-width': 1.0>
            for (point in radial_grid)
                <path d: svg.M(center_x, center_y) ++ " " ++
                    svg.L(point.x, point.y), fill: "none",
                    stroke: "#ddd", 'stroke-width': 1.0>
            for (curve in curves) curve
        >
        if (title != null) <span class: "tikz-polar-title",
            style: "position:absolute;left:50%;top:2px;" ++
                "transform:translateX(-50%);white-space:nowrap;font-weight:bold;" ++
                "font-size:18px;", title>
    >
}

fn axis_position(node, xs, ys, pw, ph) any^ {
    if (node.coord_system == "axis")
        [float(scale.scale_apply(xs, node.x)),
         float(scale.scale_apply(ys, node.y))]
    else if (node.coord_system == "axis-description")
        [float(node.x) * pw, (1.0 - float(node.y)) * ph]
    else raise error("axis annotation requires an axis coordinate system")
}

fn annotation_style(node, tangent = null) string^ {
    let checked = opts.check(node, ["black", "blue", "red", "green", "darkgreen",
        "orange", "purple", "gray", "color", "text width", "font", "align",
        "above", "below", "left", "right", "pos", "sloped"])^
    let color = opts.color(node, "black")^
    let font = opts.value(node, "font", null)
    let alignment = opts.value(node, "align", null)
    let text_width = opts.value(node, "text width", null)
    let width_style = if (text_width == null) ""
        else "width:" ++ string(opts.dimension_px(text_width)^) ++ "px;white-space:normal;"
    let font_style = if (font == null) "" else "font-size:12px;"
    let align_style = if (alignment == null) "" else "text-align:right;"
    let angle = if (opts.has(node, "sloped") and tangent != null)
        math.atan2(tangent[1], tangent[0]) * 180.0 / 3.141592653589793
        else 0.0
    let slope_style = if (opts.has(node, "sloped"))
        "transform:translate(-50%,-50%) rotate(" ++ string(angle) ++ "deg);"
        else ""
    let style = "color:" ++ color ++ ";" ++ width_style ++
        font_style ++ align_style ++ slope_style
    if (font != null and font != "\\footnotesize")
        raise error("unsupported TikZ annotation font")
    else if (alignment != null and alignment != "flush right")
        raise error("unsupported TikZ annotation alignment")
    else if (opts.has(node, "sloped") and tangent == null)
        raise error("sloped annotation requires a path")
    else style
}

fn annotation_label(node, point, left, top, tangent = null) any^ {
    let style = annotation_style(node, tangent)^
    let width_source = opts.value(node, "text width", null)
    let half_width = if (width_source == null) 18.0
        else opts.dimension_px(width_source)^ / 2.0
    let x_shift = if (opts.has(node, "right")) half_width + 6.0
        else if (opts.has(node, "left")) 0.0 - half_width - 6.0 else 0.0
    let y_shift = if (opts.has(node, "above")) -12.0
        else if (opts.has(node, "below")) 12.0 else 0.0
    labels.positioned(labels.prepare(node.source)^,
        left + point[0] + x_shift, top + point[1] + y_shift, style)
}

fn axis_definitions(axis_node) => [for (child in axis_node
    where child is element and
        (string(name(child)) == "node" or string(name(child)) == "coordinate")) child]

fn axis_path_point(point, definitions, xs, ys, pw, ph) any^ {
    let named = if (point.ref == null)
        raise error("axis path requires named coordinates") else true
    let matches = [for (item in definitions where item.id == point.ref) item]
    if (len(matches) != 1) raise error("unknown or duplicate axis coordinate: " ++ point.ref)
    else axis_position(matches[0], xs, ys, pw, ph)^
}

fn axis_path_geometry(path, definitions, xs, ys, pw, ph) any^ {
    let drawn = if (path.action != "draw" and not opts.has(path, "draw"))
        raise error("only drawn PGFPlots annotation paths are supported") else true
    let checked = opts.check(path, ["draw", "->", "<->", "rounded corners",
        "black", "blue", "red", "green", "darkgreen", "orange", "purple",
        "gray", "color"])^
    let points = children_named(path, "point")
    let endpoint_count = if (len(points) != 2)
        raise error("axis annotations require two endpoints") else true
    let start = axis_path_point(points[0], definitions, xs, ys, pw, ph)^
    let end = axis_path_point(points[1], definitions, xs, ys, pw, ph)^
    let distinct = if (start[0] == end[0] and start[1] == end[1])
        raise error("PGFPlots annotation path endpoints coincide") else true
    let bend = if (path.line_mode == "horizontal-vertical") [end[0], start[1]]
        else if (path.line_mode == "vertical-horizontal") [start[0], end[1]]
        else null
    let route = if (bend == null or
        (bend[0] == start[0] and bend[1] == start[1]) or
        (bend[0] == end[0] and bend[1] == end[1])) [start, end]
        else [start, bend, end]
    let color = opts.color(path, "black")^
    let path_data = if (len(route) == 3 and opts.has(path, "rounded corners")) {
        let first_length = math.sqrt((bend[0] - start[0]) * (bend[0] - start[0]) +
            (bend[1] - start[1]) * (bend[1] - start[1]))
        let last_length = math.sqrt((end[0] - bend[0]) * (end[0] - bend[0]) +
            (end[1] - bend[1]) * (end[1] - bend[1]))
        let radius = min([4.0, first_length / 2.0, last_length / 2.0])
        let before = [bend[0] - (bend[0] - start[0]) * radius / first_length,
                      bend[1] - (bend[1] - start[1]) * radius / first_length]
        let after = [bend[0] + (end[0] - bend[0]) * radius / last_length,
                     bend[1] + (end[1] - bend[1]) * radius / last_length]
        // Trim both legs and join them with one smooth curve at the corner.
        svg.M(start[0], start[1]) ++ " " ++ svg.L(before[0], before[1]) ++
            " " ++ svg.C(bend[0], bend[1], bend[0], bend[1],
                after[0], after[1]) ++ " " ++ svg.L(end[0], end[1])
    } else svg.line_path(route)
    let path_svg = <path d: path_data, fill: "none", stroke: color,
        'stroke-width': 1.3>
    let arrow_end = opts.has(path, "->") or opts.has(path, "<->")
    let arrow_start = opts.has(path, "<->")
    let label_nodes = children_named(path, "node")
    {shape: <g path_svg
        if (arrow_start) svg.arrow_head(route[1][0], route[1][1],
            start[0], start[1], color)
        if (arrow_end) svg.arrow_head(route[len(route) - 2][0],
            route[len(route) - 2][1], end[0], end[1], color)>,
     label: if (len(label_nodes) == 0) null
        else {node: label_nodes[0], point: [(start[0] + end[0]) / 2.0,
            (start[1] + end[1]) / 2.0]}}
}

fn plot_annotations(series_list, index, xs, ys, left, top, acc) any^ {
    if (index >= len(series_list)) acc
    else {
        let annotations = children_named(series_list[index].source, "node")
        let points = series_list[index].points
        let additions = [for (node in annotations) {
            let fraction = opts.numeric_value(opts.value(node, "pos", "0.5"))^
            let position = if (fraction >= 0.0 and fraction <= 1.0) fraction
                else raise error("TikZ annotation position must be in [0,1]")
            let i = min([int(position * float(len(points) - 1)), len(points) - 2])
            let a = [float(scale.scale_apply(xs, points[i].x)),
                     float(scale.scale_apply(ys, points[i].y))]
            let b = [float(scale.scale_apply(xs, points[i + 1].x)),
                     float(scale.scale_apply(ys, points[i + 1].y))]
            let local = position * float(len(points) - 1) - float(i)
            let point = [a[0] + local * (b[0] - a[0]),
                         a[1] + local * (b[1] - a[1])]
            annotation_label(node, point, left, top,
                [b[0] - a[0], b[1] - a[1]])^
        }]
        plot_annotations(series_list, index + 1, xs, ys, left, top,
            [*acc, *additions])^
    }
}

fn render_cartesian_axis(axis_node, options) any^ {
    let checked = opts.check(axis_node, ["xmin", "xmax", "ymin", "ymax", "width", "height",
        "xlabel", "ylabel", "grid", "domain", "samples", "mark", "axis x line",
        "axis y line", "axis line style", "xlabel near ticks", "ylabel near ticks",
        "xticklabel style"])^
    let plots = children_named(axis_node, "plot")
    if (len(plots) == 0) raise error("PGFPlots axis has no coordinate plots")
    let series_list = resolve_plots(plots, axis_node, 0, [])^
    let points = [for (series in series_list, point in series.points) point]
    let kind = string(name(axis_node))
    let xlog = kind == "semilogxaxis" or kind == "loglogaxis"
    let ylog = kind == "semilogyaxis" or kind == "loglogaxis"
    let invalid_log = [for (point in points
        where (xlog and point.x <= 0.0) or (ylog and point.y <= 0.0)) point]
    if (len(invalid_log) > 0) raise error("PGFPlots log axis requires positive coordinates")
    let xdomain = axis_domain(points, "x", optional_number(axis_node, "xmin")^,
        optional_number(axis_node, "xmax")^)
    let ydomain = axis_domain(points, "y", optional_number(axis_node, "ymin")^,
        optional_number(axis_node, "ymax")^)
    if (xdomain[0] >= xdomain[1] or ydomain[0] >= ydomain[1])
        raise error("PGFPlots axis limits must increase")
    if ((xlog and xdomain[0] <= 0.0) or (ylog and ydomain[0] <= 0.0))
        raise error("PGFPlots log limits must be positive")
    let text_width_px = if (options == null) null else options.text_width_px
    let width = opts.dimension_px(opts.value(axis_node, "width", "8cm"), text_width_px)^
    let height = opts.dimension_px(opts.value(axis_node, "height", "5cm"), text_width_px)^
    let x_line = opts.value(axis_node, "axis x line", "bottom")
    let y_line = opts.value(axis_node, "axis y line", "left")
    let tick_label_style = opts.value(axis_node, "xticklabel style", null)
    if ((x_line != "bottom" and x_line != "middle") or
        (y_line != "left" and y_line != "middle"))
        raise error("unsupported PGFPlots axis line position")
    if (tick_label_style != null and
            tick_label_style != "/pgf/number format/1000 sep=")
        raise error("unsupported PGFPlots x tick label style")
    let legend_entries = legend_nodes(axis_node)
    let surplus_explicit = [for (index, entry in legend_entries
        where index >= len(plots) and entry.from_list != true) entry]
    if (len(surplus_explicit) > 0)
        raise error("PGFPlots has more legend entries than plots")
    let entries = [for (index, entry in legend_entries
        where index < len(plots)) entry.source]
    // keep legends in a separate band so rising curves and scatter points stay visible.
    let legend_band = if (len(entries) > 0) 6.0 + float(len(entries)) * 22.0 else 0.0
    let total_height = height + legend_band
    // logarithmic ticks need room for values such as 1e-05 beside the y title.
    let left = if (ylog) 80.0 else 64.0
    let top = 20.0 + legend_band
    let pw = width - left - 18.0
    let ph = height - 20.0 - 43.0
    if (pw <= 0.0 or ph <= 0.0) raise error("PGFPlots axis dimensions are too small")
    let xs = if (xlog) scale.log_scale(xdomain[0], xdomain[1], 0.0, pw, 10.0)
        else scale.linear_scale(xdomain[0], xdomain[1], 0.0, pw)
    let ys = if (ylog) scale.log_scale(ydomain[0], ydomain[1], ph, 0.0, 10.0)
        else scale.linear_scale(ydomain[0], ydomain[1], ph, 0.0)
    let x_origin = if (y_line == "middle") {
        if (xlog or xdomain[0] > 0.0 or xdomain[1] < 0.0)
            raise error("PGFPlots middle y axis requires zero in the x domain")
        else float(scale.scale_apply(xs, 0.0))
    } else 0.0
    let y_origin = if (x_line == "middle") {
        if (ylog or ydomain[0] > 0.0 or ydomain[1] < 0.0)
            raise error("PGFPlots middle x axis requires zero in the y domain")
        else float(scale.scale_apply(ys, 0.0))
    } else ph
    let raw_axis_style = opts.value(axis_node, "axis line style", null)
    let axis_parts = if (raw_axis_style == null) []
        else [for (part in split(raw_axis_style, ",")) trim(part)]
    let unsupported_axis_parts = [for (part in axis_parts
        where part != "<->" and not starts_with(part, "color=")) part]
    if (len(unsupported_axis_parts) > 0)
        raise error("unsupported PGFPlots axis line style: " ++ unsupported_axis_parts[0])
    let axis_colors = [for (part in axis_parts where starts_with(part, "color="))
        slice(part, len("color="), len(part))]
    let axis_color = if (len(axis_colors) == 0) "#888"
        else opts.color_value(axis_colors[len(axis_colors) - 1])^
    let axis_arrows = len([for (part in axis_parts where part == "<->") part]) > 0
    let grid = opts.value(axis_node, "grid", "none")
    if (grid != "none" and grid != "major" and grid != "both")
        raise error("unsupported PGFPlots grid option: " ++ grid)
    let grid_mode = if (grid == "both") "major" else grid
    let config = {tick_count: 5, domain_color: axis_color, tick_color: axis_color}
    let grid_x = if (grid_mode == "major") chart_axis.x_axis_grid(xs, pw, ph, config) else null
    let grid_y = if (grid_mode == "major") chart_axis.y_axis_grid(ys, pw, ph, config) else null
    let plot_elements = render_series(series_list, axis_node, 0, xs, ys, pw, ph, [])^
    let definitions = axis_definitions(axis_node)
    let paths = children_named(axis_node, "path")
    let annotation_paths = [for (path in paths)
        axis_path_geometry(path, definitions, xs, ys, pw, ph)^]
    let direct_labels = [for (node in definitions where string(name(node)) == "node")
        annotation_label(node, axis_position(node, xs, ys, pw, ph)^, left, top)^]
    let path_labels = [for (item in annotation_paths where item.label != null)
        annotation_label(item.label.node, item.label.point, left, top)^]
    let series_labels = plot_annotations(series_list, 0, xs, ys, left, top, [])^
    // Keep plots in the axis coordinate space; nested SVG viewports shift in Radiant.
    let plot_view = <g
        for (part in plot_elements) part>
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: total_height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(total_height),
        <g transform: svg.translate(left, top),
            if (grid_x != null) grid_x
            if (grid_y != null) grid_y
            plot_view;
            for (item in annotation_paths) item.shape;
            <g transform: svg.translate(0.0, y_origin - ph),
                chart_axis.x_axis(xs, pw, ph, config, null)>;
            <g transform: svg.translate(x_origin, 0.0),
                chart_axis.y_axis(ys, pw, ph, config, null)>;
            if (axis_arrows)
                <g
                    svg.arrow_head(8.0, y_origin, 0.0, y_origin, axis_color)
                    svg.arrow_head(pw - 8.0, y_origin, pw, y_origin, axis_color)
                    svg.arrow_head(x_origin, 8.0, x_origin, 0.0, axis_color)
                    svg.arrow_head(x_origin, ph - 8.0, x_origin, ph, axis_color)
                >
        >
    >
    let x_label_y = if (x_line == "middle") top + y_origin
        else if (opts.has(axis_node, "xlabel near ticks")) height - 22.0
        else total_height - 6.0
    let y_label_x = if (y_line == "middle") left + x_origin
        else if (opts.has(axis_node, "ylabel near ticks")) left / 2.0 else 12.0
    let x_label = label_at(opts.value(axis_node, "xlabel", null),
        if (x_line == "middle") left + pw - 9.0 else left + pw / 2.0,
        x_label_y)^
    let y_label = label_at(opts.value(axis_node, "ylabel", null),
        y_label_x, if (y_line == "middle") top + 8.0 else top + ph / 2.0,
        if (y_line == "middle") "" else
            "transform:translate(-50%,-50%) rotate(-90deg);")^
    let legend = render_legend(entries, plots, 0, left, [])^
    let style = "position:relative;display:inline-block;width:" ++ string(width) ++
        "px;height:" ++ string(total_height) ++ "px;vertical-align:bottom;";
    <div class: "tikz-axis", style: style,
        graphic
        if (x_label != null) x_label
        if (y_label != null) y_label
        for (entry in legend) entry
        for (label in [*series_labels, *direct_labels, *path_labels]) label
    >
}

pub fn render_axis(axis_node, options = null) any^ =>
    if (string(name(axis_node)) == "polaraxis") render_polar_axis(axis_node)^
    else render_cartesian_axis(axis_node, options)^
