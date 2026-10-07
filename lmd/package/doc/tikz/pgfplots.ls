// PGFPlots coordinate-list axes over the shared chart scale and SVG helpers.
import opts: .options
import labels: .labels
import expression: .expression
import pgfmath: .pgfmath
import util: lambda.latex.util
import scale: lambda.chart.scale
import chart_axis: lambda.chart.axis
import svg: lambda.chart.svg
import plotdata: .plotdata
import kinds: .plotkinds

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn plotted_points(plot) => children_named(plot, "point")

fn sample_domain(plot, axis_node, program_data) any^ {
    let axis_domain = if (axis_node == null) "-5:5"
        else opts.value(axis_node, "domain", "-5:5")
    let raw = opts.value(plot, "domain", axis_domain)
    let parts = split(raw, ":")
    let valid_parts = if (len(parts) == 2) true
        else raise error("PGFPlots function domain must be min:max")
    let low = if (program_data == null) opts.numeric_value(parts[0])^
        else pgfmath.evaluate_source(parts[0], program_data)^
    let high = if (program_data == null) opts.numeric_value(parts[1])^
        else pgfmath.evaluate_source(parts[1], program_data)^
    if (low < high) [low, high]
    else raise error("PGFPlots function domain must increase")
}

fn sample_count(plot, axis_node) int^ {
    let axis_samples = if (axis_node == null) "25"
        else opts.value(axis_node, "samples", "25")
    let raw = opts.value(plot, "samples", axis_samples)
    let count_number = opts.numeric_value(raw)^
    if (count_number < 2.0 or count_number > 1001.0 or
        float(int(count_number)) != count_number)
        raise error("PGFPlots samples must be an integer from 2 to 1001")
    else int(count_number)
}

fn sample_curve(y_tree, x_tree, low, high, count, index, acc,
                program_data, sample_variable) any^ {
    if (index >= count) acc
    else {
        let x = low + (high - low) * float(index) / float(count - 1)
        let point = sample_point(y_tree, x_tree, x, program_data, sample_variable)^
        let x_value = if (point == null) null else point.x
        let y = if (point == null) null else point.y
        if (x_value == null or y == null)
            sample_curve(y_tree, x_tree, low, high, count, index + 1,
                acc, program_data, sample_variable)^
        else if (not (x_value == x_value) or abs(x_value) > 1e100 or
            not (y == y) or abs(y) > 1e100)
            raise error("PGFPlots expression produced a non-finite sample")
        else sample_curve(y_tree, x_tree, low, high, count, index + 1,
            [*acc, {x: x_value, y: y}], program_data, sample_variable)^
    }
}

fn sample_point(y_tree, x_tree, x, program_data, sample_variable) any^ {
    evaluate_point(y_tree, x_tree, x, program_data, sample_variable) ^ {
        if (expression.undefined_sample(^.message)) null else raise ^
    }
}

fn evaluate_point(y_tree, x_tree, x, program_data, sample_variable) any^ {
    // Definitions are part of a sample; undefined accumulators discard that sample too.
    let context = pgfmath.context_at(program_data, x, sample_variable)^;
    {x: if (x_tree == null) x else expression.evaluate_sample(x_tree, x, context)^,
     y: expression.evaluate_sample(y_tree, x, context)^}
}

pub fn plot_points(plot, axis_node = null, program_data = null, base_uri = null) any^ {
    let points = if (plot.input_kind == "expression" or
                          plot.input_kind == "parametric") {
            let domain = sample_domain(plot, axis_node, program_data)^
            let count = sample_count(plot, axis_node)^
            let raw_variable = opts.value(plot, "variable", null)
            let sample_variable = if (raw_variable == null) null
                else if (starts_with(trim(raw_variable), "\\"))
                    slice(trim(raw_variable), 1, len(trim(raw_variable)))
                else trim(raw_variable)
            if (plot.input_kind == "parametric") {
                let x_wrappers = children_named(plot, "x_expression")
                let y_wrappers = children_named(plot, "y_expression")
                if (len(x_wrappers) != 1 or len(y_wrappers) != 1)
                    raise error("PGFPlots parametric expression pair is missing")
                else {
                    let selected = pgfmath.for_expression(program_data,
                        [x_wrappers[0][0], y_wrappers[0][0]])
                    sample_curve(y_wrappers[0][0], x_wrappers[0][0],
                        domain[0], domain[1], count, 0, [], selected,
                        sample_variable)^
                }
            } else {
                let trees = [for (child in plot
                    where child is element and string(name(child)) != "option" and
                        string(name(child)) != "point" and
                        string(name(child)) != "node") child]
                if (len(trees) == 1)
                    sample_curve(trees[0], null, domain[0], domain[1],
                        count, 0, [], pgfmath.for_expression(program_data, trees[0]),
                        sample_variable)^
                else raise error("PGFPlots function tree is missing")
            }
        } else if (plot.input_kind == "table") plotdata.table_points(plot, base_uri)^
        else plotted_points(plot)
    if (len(points) == 0) raise error("PGFPlots plot has no finite coordinates")
    else points
}

// Points as floats; symbolic values map to their index on that axis.
fn normalized_points(points, context) any^ => [for (point in points)
    {x: plotdata.component(point.x, point.x_source,
        if (context == null) null else context.symbols_x, "x")^,
     y: plotdata.component(point.y, point.y_source,
        if (context == null) null else context.symbols_y, "y")^,
     errors: plotdata.errors(point)}]

fn series_kind(plot, bars) =>
    if (plot.input_kind == "fill_between") "fill_between"
    else if (plot.closed_cycle == true) "area"
    else if (bars != null) "bar"
    else "line"

fn resolve_plots(plots, axis_node, index, acc, program_data, context = null) any^ {
    if (index >= len(plots)) acc
    else {
        let plot = plots[index]
        let kind = series_kind(plot, if (context == null) null else context.bars)
        let points = if (kind == "fill_between") []
            else normalized_points(plot_points(plot, axis_node, program_data,
                if (context == null) null else context.base_uri)^, context)^
        resolve_plots(plots, axis_node, index + 1,
            [*acc, {source: plot, points: points, kind: kind,
                errors: kinds.error_spec(plot)^}], program_data, context)^
    }
}

fn series_color(plot, index) string^ {
    let cycle = ["blue", "red", "green", "purple", "orange"]
    let checked = opts.check(plot, ["blue", "red", "green", "orange", "purple", "gray",
        "black", "darkgreen", "color", "only marks", "domain", "samples", "mark",
        "thick", "smooth", "fill", "draw", "name path", "forget plot",
        "error bars/.cd", *kinds.ERROR_KEYS,
        *[for (key in kinds.ERROR_KEYS) "error bars/" ++ key]])^
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

fn clipped_path(points, width, height, index, acc) any^ {
    if (index >= len(points)) acc
    else {
        let segment = clipped_segment(points[index - 1], points[index], width, height)
        let next = if (segment == null) acc else acc ++ " " ++
            svg.M(segment[0][0], segment[0][1]) ++ " " ++
            svg.L(segment[1][0], segment[1][1])
        clipped_path(points, width, height, index + 1, next)^
    }
}

fn catmull_component(a, b, c, d, t) {
    let square = t * t
    let cube = square * t
    0.5 * (2.0 * b + (c - a) * t +
        (2.0 * a - 5.0 * b + 4.0 * c - d) * square +
        (3.0 * b - a - 3.0 * c + d) * cube)
}

fn smooth_interpolated(first, left, right, following, step) {
    let t = float(step) / 4.0;
    [catmull_component(first[0], left[0], right[0], following[0], t),
     catmull_component(first[1], left[1], right[1], following[1], t)]
}

fn smooth_segment(points, index) any^ {
    let first = points[max([0, index - 1])]
    let left = points[index]
    let right = points[index + 1]
    let following = points[min([len(points) - 1, index + 2])];
    [for (step in 1 to 4)
        smooth_interpolated(first, left, right, following, step)]
}

fn smooth_segments(points, index) any^ =>
    [for (at in index to (len(points) - 2)) smooth_segment(points, at)^]

fn flattened_points(segments) =>
    [for (segment in segments, point in segment) point]

fn smooth_points(points, index, acc) any^ =>
    if (index >= len(points) - 1) acc
    else acc ++ flattened_points(smooth_segments(points, index)^)

fn polyline(points, closed) =>
    svg.line_path(points) ++ (if (closed) " Z" else "")

fn error_bars_svg(series, anchors, color, xs, ys, width, height) =>
    if (series.errors == null) []
    else [for (index, point in series.points,
        segment in (if (anchors[index] == null) []
            else kinds.error_segments(series.errors, point, anchors[index], xs, ys,
                width, height)))
        <path class: "tikz-error-bar", d: polyline(segment, false), fill: "none",
            stroke: color, 'stroke-width': 1.0>]

fn line_series_svg(series, axis_node, color, xs, ys, width, height) any^ {
    let plot = series.source
    let points = series.points
    let transformed = [for (point in points)
        [float(scale.scale_apply(xs, point.x)),
         float(scale.scale_apply(ys, point.y))]]
    // Sample a Catmull-Rom curve before clipping so smooth plots remain vector paths.
    let drawing_points = if (opts.has(plot, "smooth") and len(transformed) >= 3)
        smooth_points(transformed, 0, [transformed[0]])^
        else transformed
    let marks_only = opts.has(plot, "only marks")
    let mark = checked_mark(plot, axis_node)^
    let stroke_width = if (opts.has(plot, "thick")) 2.1 else 1.5
    let fill_value = opts.value(plot, "fill", null)
    // \closedcycle closes the plot down to the zero line before clipping.
    let area = if (series.kind != "area") null
        else kinds.clip_polygon(kinds.area_polygon(drawing_points, ys, height), width, height)
    let area_svg = if (area == null or len(area) < 3) null
        else <path class: "tikz-area", d: polyline(area, true),
            fill: if (fill_value == null or fill_value == "") "none"
                else opts.color_value(fill_value)^,
            stroke: color, 'stroke-width': stroke_width>
    let path_data = if (marks_only or series.kind == "area") ""
        else clipped_path(drawing_points, width, height, 1, "")^
    let path = if (path_data == "") null
        else <path d: path_data, fill: "none",
            stroke: color, 'stroke-width': stroke_width>
    let marks = if (mark == "none") [] else [for (point in transformed
        where point[0] >= 0.0 and point[0] <= width and
            point[1] >= 0.0 and point[1] <= height)
        point_marker(point, color, mark)];
    [for (item in [area_svg, path] where item != null) item, *marks,
     *error_bars_svg(series, transformed, color, xs, ys, width, height)]
}

fn bar_series_svg(series, bars, ordinal, total, color, xs, ys, width, height) any^ {
    let plot = series.source
    let fill_value = opts.value(plot, "fill", null)
    // PGFPlots fills default bars with the cycle color at 30% over white.
    let fill = if (fill_value == null or fill_value == "") color
        else opts.color_value(fill_value)^
    let shift = kinds.bar_shift(bars, ordinal, total)
    let rects = [for (point in series.points) kinds.bar_rect(point, bars, shift, xs, ys,
        width, height)]
    // Error bars sit on the bar end (the stacked top for stacked bars).
    let tops = {*:series, points: [for (point in series.points)
        if (bars.direction == "y") {*:point, y: point.top} else {*:point, x: point.top}]}
    let anchors = [for (index, point in series.points)
        if (rects[index] == null) null else rects[index].anchor];
    [*[for (rect in rects where rect != null)
        <rect class: "tikz-bar", x: rect.x, y: rect.y, width: rect.width,
            height: rect.height, fill: fill,
            'fill-opacity': if (fill_value == null) 0.3 else null,
            stroke: color, 'stroke-width': 0.8>],
     *error_bars_svg(tops, anchors, color, xs, ys, width, height)]
}

fn named_path_points(name_path, series_list, definitions) any^ {
    let plots = [for (series in series_list where series.kind != "fill_between" and
        opts.value(series.source, "name path", null) == name_path) series.points]
    let paths = [for (path in definitions where
        opts.value(path, "name path", null) == name_path) path]
    if (len(plots) > 0) plots[len(plots) - 1]
    else if (len(paths) > 0) [for (point in children_named(paths[len(paths) - 1], "point"))
        axis_data_point(point)^]
    else raise error("PGFPlots fill between has no path named " ++ name_path)
}

fn axis_data_point(point) any^ {
    if (point.coord_system != "axis")
        raise error("PGFPlots named paths need axis cs coordinates")
    else {x: float(point.x), y: float(point.y)}
}

fn fill_between_svg(series, series_list, paths, color, xs, ys, width, height) any^ {
    let fill_options = children_named(series.source, "fill_between")[0]
    let names = kinds.fill_between_names(fill_options)^
    let domain = kinds.soft_clip(fill_options)^
    let first_raw = named_path_points(names[0], series_list, paths)^
    let second_raw = named_path_points(names[1], series_list, paths)^
    let first = if (domain == null) first_raw
        else kinds.clip_domain(first_raw, domain[0], domain[1])
    let second = if (domain == null) second_raw
        else kinds.clip_domain(second_raw, domain[0], domain[1])
    let to_screen = (point) => [float(scale.scale_apply(xs, point.x)),
        float(scale.scale_apply(ys, point.y))]
    // The region runs along the first path and back along the second.
    let outline = [for (part in [first, reverse(second)], point in part) to_screen(point)]
    let region = kinds.clip_polygon(outline, width, height)
    if (len(region) < 3) []
    else [<path class: "tikz-fill-between", d: polyline(region, true), fill: color,
        stroke: "none">]
}

// Cycle colors and legend slots skip fill between plots, which consume no cycle entry.
fn series_styles(series_list) any^ {
    let ordinals = [for (index, series in series_list)
        len([for (earlier in slice(series_list, 0, index)
            where earlier.kind != "fill_between") earlier])];
    [for (index, series in series_list)
        {*:series, ordinal: ordinals[index],
         color: series_color(series.source, ordinals[index])^}]
}

fn render_series(styled, axis_node, bars, paths, xs, ys, width, height) any^ {
    let bar_count = len([for (series in styled where series.kind == "bar") series])
    let bar_ordinals = [for (index, series in styled)
        len([for (earlier in slice(styled, 0, index) where earlier.kind == "bar") earlier])];
    [for (index, series in styled, part in
        if (series.kind == "fill_between")
            fill_between_svg(series, styled, paths, series.color, xs, ys, width, height)^
        else if (series.kind == "bar")
            bar_series_svg(series, bars, bar_ordinals[index], bar_count, series.color,
                xs, ys, width, height)^
        else line_series_svg(series, axis_node, series.color, xs, ys, width, height)^) part]
}

fn optional_number(node, key) any^ {
    let raw = opts.value(node, key, null)
    if (raw == null) null else opts.numeric_value(raw)^
}

fn label_at(raw, x, y, extra = "") any^ {
    if (raw == null) null
    else labels.positioned(labels.prepare(raw)^, x, y, extra)
}

fn legend_nodes(axis_node) {
    let explicit = [for (child in axis_node
        where child is element and string(name(child)) == "legend_entry") child]
    let raw = opts.value(axis_node, "legend entries", null)
    if (raw == null) explicit
    else [*explicit, *[for (entry in util.split_top_level(raw, ",")
        where trim(entry) != "") {source: trim(entry), from_list: true}]]
}

fn render_legend(entries, plots, index, acc) any^ {
    if (index >= len(entries)) acc
    else {
        let prepared = labels.prepare(entries[index])^
        let series = plots[index]
        let color = series.color
        let swatch = series.kind == "bar" or series.kind == "area"
        let sample = if (swatch)
            <span style: "display:inline-block;width:12px;height:10px;margin-right:6px;" ++
                "vertical-align:middle;border:1px solid " ++ color ++ ";background:" ++
                color ++ ";opacity:0.6;">
            else if (opts.has(series.source, "only marks"))
            <span style: "display:inline-block;width:18px;margin-right:4px;" ++
                "color:" ++ color ++ ";text-align:center;", "●">
            else <span style: "display:inline-block;width:18px;border-top:2px solid " ++
                color ++ ";vertical-align:middle;margin-right:4px;">
        let el = <div class: "tikz-legend-entry",
            style: "white-space:nowrap;line-height:20px;",
            sample
            prepared.element
        >
        render_legend(entries, plots, index + 1, [*acc, el])^
    }
}

fn style_keys(style, allowed, label) any^ {
    let invalid = [for (key, value at style
        where len([for (candidate in allowed
            where string(key) == candidate) candidate]) == 0) string(key)]
    if (len(invalid) > 0)
        raise error("unsupported PGFPlots " ++ label ++ ": " ++ invalid[0])
    else style
}

fn legend_style(axis_node) any^ {
    let raw = opts.value(axis_node, "legend style", null)
    if (raw == null) {overlay: false, fill: null, font: null}
    else {
        let style = style_keys(util.parse_kv_options(raw),
            ["fill", "font", "anchor"], "legend style")^
        let anchor = if (style.anchor == null) "default" else trim(style.anchor)
        let valid_anchor = if (anchor != "default" and anchor != "north east")
            raise error("unsupported PGFPlots legend anchor: " ++ anchor) else true
        let font = if (style.font == null) null else trim(style.font)
        let valid_font = if (font != null and font != "\\scriptsize")
            raise error("unsupported PGFPlots legend font: " ++ font) else true
        {overlay: anchor == "north east",
         fill: if (style.fill == null) null else opts.color_value(style.fill)^,
         font: font}
    }
}

fn axis_background(axis_node) string^ {
    let raw = opts.value(axis_node, "axis background/.style", null)
    if (raw == null) "white"
    else {
        let style = style_keys(util.parse_kv_options(raw),
            ["fill"], "axis background")^
        if (style.fill == null) raise error("PGFPlots axis background needs fill")
        else opts.color_value(style.fill)^
    }
}

fn minor_tick_count(axis_node) int^ {
    let raw = opts.value(axis_node, "minor tick num", null)
    if (raw == null) 0
    else {
        let value = opts.numeric_value(raw)^
        if (value < 0.0 or value > 20.0 or float(int(value)) != value)
            raise error("PGFPlots minor tick num must be an integer from 0 to 20")
        else int(value)
    }
}

fn minor_ticks(axis_scale, count, horizontal, limit) {
    if (count == 0) null
    else {
        let major = scale.scale_ticks(axis_scale, 5);
        <g class: "minor-ticks",
            for (at in 0 to (len(major) - 2), between in 1 to count,
                 let value = major[at] +
                     (major[at + 1] - major[at]) * float(between) /
                     float(count + 1),
                 let position = float(scale.scale_apply(axis_scale, value))
                 where position >= 0.0 and position <= limit)
                if (horizontal)
                    <line x1: position, y1: 0, x2: position, y2: 3,
                        stroke: "#888", 'stroke-width': 0.8>
                else <line x1: 0, y1: position, x2: -3, y2: position,
                    stroke: "#888", 'stroke-width': 0.8>
        >
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

fn render_polar_axis(axis_node, picture) any^ {
    let checked = opts.check(axis_node, ["width", "height", "title", "domain", "samples"])^
    let plots = children_named(axis_node, "plot")
    if (len(plots) == 0) raise error("PGFPlots polar axis has no plots")
    let program_data = pgfmath.program(picture, axis_node)^
    let series_list = resolve_plots(plots, axis_node, 0, [], program_data)^
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
    if (point.coord_system != null) axis_position(point, xs, ys, pw, ph)^
    else {
        let named = if (point.ref == null)
            raise error("axis path requires named or axis coordinates") else true
        let matches = [for (item in definitions where item.id == point.ref) item]
        if (len(matches) != 1) raise error("unknown or duplicate axis coordinate: " ++ point.ref)
        else axis_position(matches[0], xs, ys, pw, ph)^
    }
}

fn axis_path_geometry(path, definitions, xs, ys, pw, ph) any^ {
    let drawn = if (path.action != "draw" and not opts.has(path, "draw"))
        raise error("only drawn PGFPlots annotation paths are supported") else true
    let checked = opts.check(path, ["draw", "->", "<->", "rounded corners",
        "black", "blue", "red", "green", "darkgreen", "orange", "purple",
        "gray", "color", "name path"])^
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

fn value_domain(values, low, high) {
    let lo = if (low != null) float(low) else float(min(values))
    let hi = if (high != null) float(high) else float(max(values))
    if (lo == hi) [lo - 1.0, hi + 1.0] else [lo, hi]
}

// Data extents along one axis: bars span base to top; errors widen points.
fn data_extents(series, point, axis, bars) {
    if (series.kind == "bar" and bars.direction == axis) {
        let top_point = if (axis == "y") {*:point, y: point.top} else {*:point, x: point.top};
        [point.base, *kinds.point_extents(series.errors, top_point, axis)]
    } else kinds.point_extents(series.errors, point, axis)
}

// `enlargelimits` / `enlarge x limits`: true (10%), a fraction, or {abs=value}.
fn enlargement(axis_node, axis) any^ {
    let raw = opts.value(axis_node, "enlarge " ++ axis ++ " limits",
        opts.value(axis_node, "enlargelimits", null))
    let text = if (raw == null) null else trim(raw)
    if (text == null or text == "false") null
    else if (text == "true" or text == "") {relative: 0.1, absolute: null}
    else if (starts_with(text, "abs="))
        {relative: null, absolute: opts.numeric_value(slice(text, 4, len(text)))^}
    else {relative: opts.numeric_value(text)^, absolute: null}
}

// Enlargement applies to automatically computed limits only.
fn enlarged(domain, spec, low_fixed, high_fixed, log) any^ {
    if (spec == null) domain
    else if (log) raise error("PGFPlots enlarged limits need a linear axis")
    else {
        let amount = if (spec.absolute != null) spec.absolute
            else (domain[1] - domain[0]) * spec.relative;
        [if (low_fixed) domain[0] else domain[0] - amount,
         if (high_fixed) domain[1] else domain[1] + amount]
    }
}

// Tick scale for an axis: symbolic coordinates label their index positions.
fn tick_scale(numeric_scale, symbol_list, tick_option, used, axis) any^ {
    let data_ticks = tick_option == "data"
    let valid_option = if (tick_option != null and not data_ticks)
        raise error("unsupported PGFPlots " ++ axis ++ "tick: " ++ tick_option) else true
    if (symbol_list == null) {
        if (data_ticks) raise error("PGFPlots " ++ axis ++ "tick=data needs symbolic " ++
            axis ++ " coords")
        else numeric_scale
    } else {
        let indices = if (data_ticks) [for (index, label in symbol_list
            where any([for (value in used) value == float(index)])) index]
            else [for (index in 0 to (len(symbol_list) - 1)) index]
        let contiguous = len(indices) > 0 and
            indices[len(indices) - 1] - indices[0] == len(indices) - 1
        if (not contiguous)
            raise error("PGFPlots " ++ axis ++ "tick=data needs contiguous symbolic coordinates")
        else scale.point_scale([for (index in indices) symbol_list[index]],
            float(scale.scale_apply(numeric_scale, float(indices[0]))),
            float(scale.scale_apply(numeric_scale, float(indices[len(indices) - 1]))), 0.0)
    }
}

fn invisible_named_path(path) =>
    path.action == "path" and not opts.has(path, "draw") and
        opts.value(path, "name path", null) != null

// Axis passes 1-3 (§6.2): validate keys, resolve and stack the series, and derive
// the data domains. Exported so tests can assert data semantics before drawing.
pub fn resolve_axis(axis_node, options = null, picture = null, extra_keys = []) any^ {
    let checked = opts.check(axis_node, ["xmin", "xmax", "ymin", "ymax", "width", "height",
        "xlabel", "ylabel", "grid", "domain", "samples", "mark", "axis x line",
        "axis y line", "axis line style", "xlabel near ticks", "ylabel near ticks",
        "xticklabel style", "tick align", "legend style", "legend entries",
        "axis background/.style", "title", "minor tick num", *kinds.AXIS_KEYS,
        "enlargelimits", "enlarge x limits", "enlarge y limits", "symbolic x coords",
        "symbolic y coords", "xtick", "ytick", *extra_keys])^
    let tick_align = opts.value(axis_node, "tick align", "outside")
    let valid_align = if (tick_align != "outside")
        raise error("unsupported PGFPlots tick alignment: " ++ tick_align) else true
    let plots = children_named(axis_node, "plot")
    let valid_plots = if (len(plots) == 0)
        raise error("PGFPlots axis has no coordinate plots") else true
    let program_data = pgfmath.program(picture, axis_node)^
    let bars = kinds.bar_mode(axis_node)^
    let symbols_x = plotdata.symbols(axis_node, "x")
    let symbols_y = plotdata.symbols(axis_node, "y")
    let context = {bars: bars, symbols_x: symbols_x, symbols_y: symbols_y,
        base_uri: plotdata.resource_base(options)}
    let resolved = resolve_plots(plots, axis_node, 0, [], program_data, context)^
    let series_list = if (bars == null) resolved else kinds.stack_bars(resolved, bars)
    let data_series = [for (series in series_list where series.kind != "fill_between") series]
    let valid_data = if (len(data_series) == 0)
        raise error("PGFPlots fill between needs plotted paths") else true
    let points = [for (series in data_series, point in series.points) point]
    let kind = string(name(axis_node))
    let xlog = kind == "semilogxaxis" or kind == "loglogaxis"
    let ylog = kind == "semilogyaxis" or kind == "loglogaxis"
    let valid_bars = if (bars != null and ((bars.direction == "y" and ylog) or
        (bars.direction == "x" and xlog)))
        raise error("PGFPlots bars need a linear value axis") else true
    let valid_symbols = if ((symbols_x != null and xlog) or (symbols_y != null and ylog))
        raise error("PGFPlots symbolic coordinates need a linear axis") else true
    let x_values = [for (series in data_series, point in series.points,
        value in data_extents(series, point, "x", bars)) value]
    let y_values = [for (series in data_series, point in series.points,
        value in data_extents(series, point, "y", bars)) value]
    let invalid_log = [for (value in x_values where xlog and value <= 0.0) value] ++
        [for (value in y_values where ylog and value <= 0.0) value]
    let valid_log = if (len(invalid_log) > 0)
        raise error("PGFPlots log axis requires positive coordinates") else true
    let xmin = optional_number(axis_node, "xmin")^
    let xmax = optional_number(axis_node, "xmax")^
    let ymin = optional_number(axis_node, "ymin")^
    let ymax = optional_number(axis_node, "ymax")^
    let xdomain = enlarged(value_domain(x_values, xmin, xmax), enlargement(axis_node, "x")^,
        xmin != null, xmax != null, xlog)^
    let ydomain = enlarged(value_domain(y_values, ymin, ymax), enlargement(axis_node, "y")^,
        ymin != null, ymax != null, ylog)^
    let valid_limits = if (xdomain[0] >= xdomain[1] or ydomain[0] >= ydomain[1])
        raise error("PGFPlots axis limits must increase") else true
    let valid_log_limits = if ((xlog and xdomain[0] <= 0.0) or (ylog and ydomain[0] <= 0.0))
        raise error("PGFPlots log limits must be positive") else true;
    {series: series_list, data_series: data_series, points: points, bars: bars,
     symbols_x: symbols_x, symbols_y: symbols_y, xlog: xlog, ylog: ylog,
     xdomain: xdomain, ydomain: ydomain}
}

fn render_cartesian_axis(axis_node, options, picture, extra_keys = []) any^ {
    let plan = resolve_axis(axis_node, options, picture, extra_keys)^
    let series_list = plan.series
    let data_series = plan.data_series
    let points = plan.points
    let bars = plan.bars
    let symbols_x = plan.symbols_x
    let symbols_y = plan.symbols_y
    let xlog = plan.xlog
    let ylog = plan.ylog
    let xdomain = plan.xdomain
    let ydomain = plan.ydomain
    let text_width_px = if (options == null) null else options.text_width_px
    let width = opts.dimension_px(opts.value(axis_node, "width", "8cm"), text_width_px)^
    let height = opts.dimension_px(opts.value(axis_node, "height", "5cm"), text_width_px)^
    // PGFPlots treats center as an alias for middle on both axes.
    let x_line_option = opts.value(axis_node, "axis x line", "bottom")
    let y_line_option = opts.value(axis_node, "axis y line", "left")
    let x_line = if (x_line_option == "center") "middle" else x_line_option
    let y_line = if (y_line_option == "center") "middle" else y_line_option
    let tick_label_style = opts.value(axis_node, "xticklabel style", null)
    let valid_lines = if ((x_line != "bottom" and x_line != "middle") or
        (y_line != "left" and y_line != "middle"))
        raise error("unsupported PGFPlots axis line position") else true
    let valid_tick_style = if (tick_label_style != null and
            tick_label_style != "/pgf/number format/1000 sep=")
        raise error("unsupported PGFPlots x tick label style") else true
    let styled = series_styles(series_list)^
    // Legend entries follow plots in order; fill between and forget plot take none.
    let legend_series = [for (series in styled where series.kind != "fill_between" and
        not opts.has(series.source, "forget plot")) series]
    let legend_entries = legend_nodes(axis_node)
    let surplus_explicit = [for (index, entry in legend_entries
        where index >= len(legend_series) and entry.from_list != true) entry]
    let valid_legend = if (len(surplus_explicit) > 0)
        raise error("PGFPlots has more legend entries than plots") else true
    let entries = [for (index, entry in legend_entries
        where index < len(legend_series)) entry.source]
    let legend_config = legend_style(axis_node)^
    let background = axis_background(axis_node)^
    let minor_count = minor_tick_count(axis_node)^
    let valid_minor = if (minor_count > 0 and (xlog or ylog))
        raise error("PGFPlots logarithmic minor ticks need logarithmic spacing") else true
    let title_source = opts.value(axis_node, "title", null)
    let title_prepared = if (title_source == null) null
        else labels.prepare(title_source)^
    let title_band = if (title_prepared == null) 0.0 else 30.0
    // A north-east legend sits inside the axes; default legends use a header band.
    let legend_band = if (len(entries) > 0 and not legend_config.overlay)
        6.0 + float(len(entries)) * 22.0 else 0.0
    let total_height = height + title_band + legend_band
    // logarithmic ticks need room for values such as 1e-05 beside the y title.
    let left = if (ylog) 80.0 else 64.0
    let top = 20.0 + title_band + legend_band
    let pw = width - left - 18.0
    let ph = height - 20.0 - 43.0
    let valid_size = if (pw <= 0.0 or ph <= 0.0)
        raise error("PGFPlots axis dimensions are too small") else true
    let xs = if (xlog) scale.log_scale(xdomain[0], xdomain[1], 0.0, pw, 10.0)
        else scale.linear_scale(xdomain[0], xdomain[1], 0.0, pw)
    let ys = if (ylog) scale.log_scale(ydomain[0], ydomain[1], ph, 0.0, 10.0)
        else scale.linear_scale(ydomain[0], ydomain[1], ph, 0.0)
    let x_ticks = tick_scale(xs, symbols_x, opts.value(axis_node, "xtick", null),
        [for (point in points) point.x], "x")^
    let y_ticks = tick_scale(ys, symbols_y, opts.value(axis_node, "ytick", null),
        [for (point in points) point.y], "y")^
    let x_origin = if (y_line == "middle") {
        // PGFPlots places a middle axis at the lower limit when zero is outside.
        if (xlog or xdomain[0] > 0.0 or xdomain[1] < 0.0) 0.0
        else float(scale.scale_apply(xs, 0.0))
    } else 0.0
    let y_origin = if (x_line == "middle") {
        if (ylog or ydomain[0] > 0.0 or ydomain[1] < 0.0) ph
        else float(scale.scale_apply(ys, 0.0))
    } else ph
    let raw_axis_style = opts.value(axis_node, "axis line style", null)
    let axis_parts = if (raw_axis_style == null) []
        else [for (part in split(raw_axis_style, ",")) trim(part)]
    let unsupported_axis_parts = [for (part in axis_parts
        where part != "<->" and not starts_with(part, "color=")) part]
    let valid_axis_style = if (len(unsupported_axis_parts) > 0)
        raise error("unsupported PGFPlots axis line style: " ++ unsupported_axis_parts[0])
        else true
    let axis_colors = [for (part in axis_parts where starts_with(part, "color="))
        slice(part, len("color="), len(part))]
    let axis_color = if (len(axis_colors) == 0) "#888"
        else opts.color_value(axis_colors[len(axis_colors) - 1])^
    let axis_arrows = len([for (part in axis_parts where part == "<->") part]) > 0
    let grid = opts.value(axis_node, "grid", "none")
    let valid_grid = if (grid != "none" and grid != "major" and grid != "both")
        raise error("unsupported PGFPlots grid option: " ++ grid) else true
    let grid_mode = if (grid == "both") "major" else grid
    let config = {tick_count: 5, domain_color: axis_color, tick_color: axis_color}
    let grid_x = if (grid_mode == "major") chart_axis.x_axis_grid(x_ticks, pw, ph, config)
        else null
    let grid_y = if (grid_mode == "major") chart_axis.y_axis_grid(y_ticks, pw, ph, config)
        else null
    let paths = children_named(axis_node, "path")
    let plot_elements = render_series(styled, axis_node, bars, paths, xs, ys, pw, ph)^
    let definitions = axis_definitions(axis_node)
    let annotation_paths = [for (path in paths where not invisible_named_path(path))
        axis_path_geometry(path, definitions, xs, ys, pw, ph)^]
    let direct_labels = [for (node in definitions where string(name(node)) == "node")
        annotation_label(node, axis_position(node, xs, ys, pw, ph)^, left, top)^]
    let path_labels = [for (item in annotation_paths where item.label != null)
        annotation_label(item.label.node, item.label.point, left, top)^]
    let series_labels = plot_annotations(data_series, 0, xs, ys, left, top, [])^
    // Keep plots in the axis coordinate space; nested SVG viewports shift in Radiant.
    let plot_view = <g
        for (part in plot_elements) part>
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: total_height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(total_height),
        <g transform: svg.translate(left, top),
            <rect x: 0, y: 0, width: pw, height: ph, fill: background>;
            if (grid_x != null) grid_x
            if (grid_y != null) grid_y
            plot_view;
            for (item in annotation_paths) item.shape;
            <g transform: svg.translate(0.0, y_origin - ph),
                chart_axis.x_axis(x_ticks, pw, ph, config, null)
                minor_ticks(xs, minor_count, true, pw)>;
            <g transform: svg.translate(x_origin, 0.0),
                chart_axis.y_axis(y_ticks, pw, ph, config, null)
                minor_ticks(ys, minor_count, false, ph)>;
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
    let legend_rows = render_legend(entries, legend_series, 0, [])^
    let legend_style_text = "position:absolute;" ++
        (if (legend_config.overlay)
            "right:24px;top:" ++ string(top + 8.0) ++ "px;"
         else "left:" ++ string(left) ++ "px;top:" ++
            string(title_band + 3.0) ++ "px;") ++
        (if (legend_config.fill == null) ""
         else "background:" ++ legend_config.fill ++ ";padding:3px 6px;") ++
        (if (legend_config.font == "\\scriptsize") "font-size:0.72em;" else "")
    let legend = if (len(entries) == 0) null
        else <div class: "tikz-legend", style: legend_style_text,
            for (row in legend_rows) row>
    let title = if (title_prepared == null) null
        else <div class: "tikz-axis-title",
            style: "position:absolute;left:0;top:0;width:100%;" ++
                "height:30px;text-align:center;white-space:nowrap;",
            title_prepared.element>
    let style = "position:relative;display:inline-block;width:" ++ string(width) ++
        "px;height:" ++ string(total_height) ++ "px;vertical-align:bottom;";
    <div class: "tikz-axis", style: style,
        'data-x-domain': string(xdomain[0]) ++ ":" ++ string(xdomain[1]),
        'data-y-domain': string(ydomain[0]) ++ ":" ++ string(ydomain[1]),
        graphic
        if (title != null) title
        if (x_label != null) x_label
        if (y_label != null) y_label
        if (legend != null) legend
        for (label in [*series_labels, *direct_labels, *path_labels]) label
    >
}

// `group style={group size=C by R, horizontal sep=..., vertical sep=...}`.
fn group_layout(group_node) any^ {
    let style = util.parse_kv_options(opts.value(group_node, "group style", ""))
    let invalid = [for (key, value at style where string(key) != "group size" and
        string(key) != "horizontal sep" and string(key) != "vertical sep") string(key)]
    let valid = if (len(invalid) > 0)
        raise error("unsupported PGFPlots group style: " ++ invalid[0]) else true
    let size = if (style["group size"] == null) null
        else [for (part in split(" " ++ trim(style["group size"]) ++ " ", " by ")) trim(part)]
    let valid_size = if (size == null or len(size) != 2)
        raise error("PGFPlots groupplot needs group size=<columns> by <rows>") else true
    let columns = opts.numeric_value(size[0])^
    let rows = opts.numeric_value(size[1])^
    let valid_counts = if (columns < 1.0 or rows < 1.0 or float(int(columns)) != columns or
        float(int(rows)) != rows or columns * rows > 64.0)
        raise error("PGFPlots group size must be positive integers") else true;
    {columns: int(columns), rows: int(rows),
     horizontal: opts.dimension_px(if (style["horizontal sep"] == null) "1cm"
        else style["horizontal sep"])^,
     vertical: opts.dimension_px(if (style["vertical sep"] == null) "1cm"
        else style["vertical sep"])^}
}

// groupplots: each member is an ordinary axis laid out on the group grid.
pub fn render_group(group_node, options = null, picture = null) any^ {
    let layout = group_layout(group_node)^
    let axes = children_named(group_node, "axis")
    let valid = if (len(axes) == 0 or len(axes) > layout.columns * layout.rows)
        raise error("PGFPlots groupplot has more plots than its group size") else true
    let rendered = [for (axis in axes)
        render_cartesian_axis(axis, options, picture, ["group style"])^];
    <div class: "tikz-groupplot",
        'data-group-size': string(layout.columns) ++ " by " ++ string(layout.rows),
        style: "display:inline-grid;grid-template-columns:repeat(" ++
            string(layout.columns) ++ ",auto);column-gap:" ++ string(layout.horizontal) ++
            "px;row-gap:" ++ string(layout.vertical) ++
            "px;align-items:start;vertical-align:bottom;",
        for (item in rendered) item
    >
}

pub fn render_axis(axis_node, options = null, picture = null) any^ =>
    if (string(name(axis_node)) == "polaraxis") render_polar_axis(axis_node, picture)^
    else render_cartesian_axis(axis_node, options, picture)^
