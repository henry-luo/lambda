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

fn sample_expression(tree, low, high, count, index, acc) any^ {
    if (index >= count) acc
    else {
        let x = low + (high - low) * float(index) / float(count - 1)
        let y = expression.evaluate(tree, x)^
        if (not (y == y) or abs(y) > 1e100)
            raise error("PGFPlots expression produced a non-finite sample")
        else sample_expression(tree, low, high, count, index + 1,
            [*acc, {x: x, y: y}])^
    }
}

fn resolve_plots(plots, axis_node, index, acc) any^ {
    if (index >= len(plots)) acc
    else {
        let plot = plots[index]
        let points = if (plot.input_kind == "expression") {
            let domain = sample_domain(plot, axis_node)^
            let count = sample_count(plot, axis_node)^
            let trees = [for (child in plot
                where child is element and string(name(child)) != "option" and
                    string(name(child)) != "point") child]
            if (len(trees) == 1)
                sample_expression(trees[0], domain[0], domain[1], count, 0, [])^
            else raise error("PGFPlots function tree is missing")
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
        "black", "color", "only marks", "domain", "samples", "mark"])^
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

fn render_series(series_list, axis_node, index, xs, ys, acc) any^ {
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
        let path = if (marks_only) null
            else <path d: svg.line_path(transformed), fill: "none",
                stroke: color, 'stroke-width': 1.5>
        let marks = if (mark == "none") [] else [for (point in transformed)
            point_marker(point, color, mark)]
        let next = if (path == null) [*acc, *marks] else [*acc, path, *marks]
        render_series(series_list, axis_node, index + 1, xs, ys, next)^
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

fn render_cartesian_axis(axis_node, options) any^ {
    let checked = opts.check(axis_node, ["xmin", "xmax", "ymin", "ymax", "width", "height",
        "xlabel", "ylabel", "grid", "domain", "samples", "mark", "axis x line",
        "axis y line", "xlabel near ticks", "ylabel near ticks", "xticklabel style"])^
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
    let outside = [for (point in points
        where point.x < xdomain[0] or point.x > xdomain[1] or
              point.y < ydomain[0] or point.y > ydomain[1]) point]
    let in_bounds = if (len(outside) == 0) true
        else raise error("PGFPlots clipping outside axis limits is not supported yet")
    let text_width_px = if (options == null) null else options.text_width_px
    let width = opts.dimension_px(opts.value(axis_node, "width", "8cm"), text_width_px)^
    let height = opts.dimension_px(opts.value(axis_node, "height", "5cm"), text_width_px)^
    let x_line = opts.value(axis_node, "axis x line", "bottom")
    let y_line = opts.value(axis_node, "axis y line", "left")
    let tick_label_style = opts.value(axis_node, "xticklabel style", null)
    if (x_line != "bottom" or y_line != "left")
        raise error("only bottom/left PGFPlots axis lines are supported")
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
    let grid = opts.value(axis_node, "grid", "none")
    if (grid != "none" and grid != "major" and grid != "both")
        raise error("unsupported PGFPlots grid option: " ++ grid)
    let grid_mode = if (grid == "both") "major" else grid
    let config = {tick_count: 5}
    let grid_x = if (grid_mode == "major") chart_axis.x_axis_grid(xs, pw, ph, config) else null
    let grid_y = if (grid_mode == "major") chart_axis.y_axis_grid(ys, pw, ph, config) else null
    let plot_elements = render_series(series_list, axis_node, 0, xs, ys, [])^
    // Keep plots in the axis coordinate space; nested SVG viewports shift in Radiant.
    let plot_view = <g
        for (part in plot_elements) part>
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: total_height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(total_height),
        <g transform: svg.translate(left, top),
            if (grid_x != null) grid_x
            if (grid_y != null) grid_y
            plot_view
            chart_axis.x_axis(xs, pw, ph, config, null)
            chart_axis.y_axis(ys, pw, ph, config, null)
        >
    >
    let x_label_y = if (opts.has(axis_node, "xlabel near ticks")) height - 22.0
        else total_height - 6.0
    let y_label_x = if (opts.has(axis_node, "ylabel near ticks")) left / 2.0 else 12.0
    let x_label = label_at(opts.value(axis_node, "xlabel", null),
        left + pw / 2.0, x_label_y)^
    let y_label = label_at(opts.value(axis_node, "ylabel", null),
        y_label_x, top + ph / 2.0, "transform:translate(-50%,-50%) rotate(-90deg);")^
    let legend = render_legend(entries, plots, 0, left, [])^
    let style = "position:relative;display:inline-block;width:" ++ string(width) ++
        "px;height:" ++ string(total_height) ++ "px;vertical-align:bottom;";
    <div class: "tikz-axis", style: style,
        graphic
        if (x_label != null) x_label
        if (y_label != null) y_label
        for (entry in legend) entry
    >
}

pub fn render_axis(axis_node, options = null) any^ =>
    if (string(name(axis_node)) == "polaraxis") render_polar_axis(axis_node)^
    else render_cartesian_axis(axis_node, options)^
