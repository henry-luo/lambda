// chart/mark.ls — Mark renderers for the chart library
// Each mark type transforms data + scales into SVG elements.
// All mark functions take (data, ctx, mark_config) where ctx is a render context map.

import util: .util
import svg: .svg
import color: .color
import scale: .scale
import parse: .parse
import cfg: .config
import records: .records
import paint: .paint
import statistics: .statistics

pub fn coordinate(ctx, channel_name, row, fallback = null) float | null | error {
    let channel = parse.channel_definition(parse.get_channel(ctx.encoding, channel_name), row);
    let value = parse.channel_value(channel, row, row[ctx[channel_name ++ "_field"]]);
    let axis = if (contains(["x2", "y2", "theta2", "radius2"], channel_name)) slice(channel_name, 0, len(channel_name) - 1) else channel_name;
    let mapping = ctx[axis ++ "_scale"];
    if (value == null) fallback
    else if (channel.value != null) float(value)
    else if (mapping) float(scale.scale_apply(mapping, value)) else float(value)
}

fn has_position(ctx, channel_name) =>
    ctx[channel_name ++ "_field"] != null or parse.get_channel(ctx.encoding, channel_name) != null

pub fn center_coordinate(ctx, key, row) {
    let mapping = ctx[key ++ "_scale"];
    coordinate(ctx, key, row) + (if (mapping.bandwidth != null) mapping.bandwidth / 2.0 else 0.0)
}

pub fn appearance(ctx, channel_name, row, fallback) {
    let channel = parse.channel_definition(ctx.encoding[channel_name], row);
    let field = if (channel.field) channel.field else ctx[channel_name ++ "_field"];
    let value = parse.channel_value(channel, row, if (field != null) row[field] else fallback);
    let mapping = ctx[channel_name ++ "_scale"];
    if (channel.value != null or value == null or mapping == null) value
    else scale.scale_apply(mapping, value)
}

pub fn tooltip(ctx, row) {
    let channel = ctx.encoding.tooltip;
    if (channel == null and ctx.tooltip_field != null) <title string(row[ctx.tooltip_field])>
    else if (channel == null) null
    else {
        let fields = if (channel is array) channel else if (channel.fields != null)
            [for (field in channel.fields) {field: field}] else [channel];
        <title join([for (field in fields)
            (if (len(fields) > 1) (if (field.title != null) field.title else field.field) ++ ": " else "") ++
                util.format_value(parse.channel_value(field, row), field.format,
                    if (field._temporal) "temporal" else field.dtype, field.scale.timezone)], "\n")>
    }
}

// Composite parts use the same channel/mark cascade as primitive marks.
pub fn style(ctx, row, options, defaults, linear = false) {
    let fill = if (linear) defaults.fill else appearance(ctx, "color", row,
        if (options.fill != null) options.fill else if (options.color != null) options.color else defaults.fill);
    let stroke_fallback = if (options.stroke != null) options.stroke
        else if (linear) appearance(ctx, "color", row, if (options.color != null) options.color else defaults.stroke)
        else defaults.stroke;
    cfg.settings({*:defaults, fill: paint.value(fill, ctx._paints),
        opacity: appearance(ctx, "opacity", row, if (options.opacity != null) options.opacity else defaults.opacity),
        stroke: paint.value(appearance(ctx, "stroke", row, stroke_fallback), ctx._paints),
        'stroke-width': if (options.stroke_width != null) options.stroke_width else defaults["stroke-width"],
        'stroke-dasharray': if (options.stroke_dash != null) options.stroke_dash else defaults["stroke-dasharray"]})
}

pub fn series(data, ctx, fallback) {
    let field = if (ctx.detail_field != null) ctx.detail_field else ctx.color_field;
    [for (partition in records.group_by(data, if (field != null) [field] else []))
        {items: partition.rows, color: appearance(ctx, "color", partition.rows[0], fallback)}]
}

// ============================================================
// Bar mark
// ============================================================

pub fn bar(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let plot_h = ctx.plot_h;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let opacity_scale = ctx.opacity_scale;
    let opacity_field = ctx.opacity_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let y2_field = if (ctx.y2_field) ctx.y2_field else null;
    let is_stacked = if (ctx.is_stacked) ctx.is_stacked else false;
    let x_offset_field = if (ctx.x_offset_field) ctx.x_offset_field else null;
    let x_offset_cats = if (ctx.x_offset_cats) ctx.x_offset_cats else null;
    let n_groups = if (x_offset_cats) len(x_offset_cats) else 1;
    let fill = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let base_opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;
    let rx = if (mark_config and mark_config.corner_radius != null) mark_config.corner_radius else 0;
    let tooltip_field = if (ctx.tooltip_field) ctx.tooltip_field else null;

    let bars = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let y2_pos = if (has_position(ctx, "y2")) coordinate(ctx, "y2", d) else null,
        let raw_bar_w = if (mark_config and mark_config.width != null) float(mark_config.width)
            else if (x_scale.bandwidth) abs(x_scale.bandwidth) else 20.0,
        let sub_gap = if (n_groups > 1) 2.0 else 0.0,
        let bar_w = if (n_groups > 1)
            (raw_bar_w - sub_gap * float(n_groups - 1)) / float(n_groups)
        else raw_bar_w,
        let offset_idx = if (x_offset_field and x_offset_cats)
            util.find_index(x_offset_cats, d[x_offset_field])
        else 0,
        let center_x = if (x_scale.bandwidth) x_pos + x_scale.bandwidth / 2.0 else x_pos,
        let x_final = center_x - raw_bar_w / 2.0 + float(offset_idx) * (bar_w + sub_gap),
        let y1_pos = if (is_stacked)
            min([float(scale.scale_apply(y_scale, float(d["_y1"]))), float(scale.scale_apply(y_scale, float(d["_y0"])) )])
        else if (has_position(ctx, "y2"))
            min(coordinate(ctx, "y", d), y2_pos)
        else min([coordinate(ctx, "y", d), float(scale.scale_apply(y_scale, 0.0))]),
        let y0_pos = if (is_stacked)
            max([float(scale.scale_apply(y_scale, float(d["_y0"]))), float(scale.scale_apply(y_scale, float(d["_y1"])) )])
        else if (has_position(ctx, "y2"))
            max(coordinate(ctx, "y", d), y2_pos)
        else max([coordinate(ctx, "y", d), float(scale.scale_apply(y_scale, 0.0))]),
        let bar_h = y0_pos - y1_pos,
        let bar_fill = appearance(ctx, "color", d, fill),
        let bar_opacity = appearance(ctx, "opacity", d, base_opacity),
        <rect x: x_final, y: y1_pos, width: bar_w, height: bar_h,
            *:style(ctx, d, mark_config, {fill: bar_fill, opacity: bar_opacity}), rx: rx,
            tooltip(ctx, d)>
    )];

    svg.group_class("marks bars", bars)
}

// render horizontal bars
pub fn bar_horizontal(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let fill = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;

    let bars = [for (d in data) (
        let y_pos = coordinate(ctx, "y", d),
        let x_pos = if (ctx.is_stacked) float(scale.scale_apply(x_scale, d._y1)) else coordinate(ctx, "x", d),
        let x_end = if (ctx.is_stacked) float(scale.scale_apply(x_scale, d._y0))
            else if (has_position(ctx, "x2")) coordinate(ctx, "x2", d) else float(scale.scale_apply(x_scale, 0.0)),
        let bar_fill = appearance(ctx, "color", d, fill),
        let bar_h = if (y_scale.bandwidth) abs(y_scale.bandwidth) else 20.0,
        <rect x: min([x_pos, x_end]), y: if (y_scale.bandwidth < 0) y_pos - bar_h else y_pos,
              width: abs(x_pos - x_end), height: bar_h,
              *:style(ctx, d, mark_config, {fill: bar_fill, opacity: opacity}),
              *:cfg.settings({rx: mark_config.corner_radius}), tooltip(ctx, d)>
    )];

    svg.group_class("marks bars-horizontal", bars)
}

// ============================================================
// Line mark
// ============================================================

pub fn line_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let detail_field = if (ctx.detail_field) ctx.detail_field else null;
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let stroke_w = if (mark_config and mark_config.stroke_width != null) mark_config.stroke_width else 2.0;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;
    let show_points = if (mark_config and mark_config.point) mark_config.point else false;

    let series = series(data, ctx, stroke_color);

    let line_elements = [for (s in series) (
        let points = [for (d in s.items) (
            let x_pos = coordinate(ctx, "x", d),
            let y_pos = coordinate(ctx, "y", d),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, y_pos])],
        let d = svg.line_path(points, mark_config.interpolate),
        let line_el = <path d: d, *:style(ctx, s.items[0], mark_config,
            {fill: "none", stroke: s.color, 'stroke-width': stroke_w, opacity: opacity}, true), tooltip(ctx, s.items[0])>,
        let point_els = if (show_points)
            [for (index, p in points)
                <circle cx: p[0], cy: p[1], r: 3, fill: paint.value(s.color, ctx._paints),
                        stroke: "white", 'stroke-width': 1, tooltip(ctx, s.items[index])>]
        else [],
        [line_el, *point_els]
    )];

    // flatten the nested arrays
    let all = [for (group in line_elements) for (el in group) el];
    svg.group_class("marks lines", all)
}

// Slope comparisons share line styling and grouping, with exactly one observation at each end.
pub fn slope_mark(data, ctx, options) {
    let positions = util.unique_vals([for (row in data) parse.channel_value(ctx.encoding.x, row)]);
    let groups = series(data, ctx, color.default_color);
    let invalid = [for (group in groups where len(group.items) != 2 or
        len(util.unique_vals([for (row in group.items) parse.channel_value(ctx.encoding.x, row)])) != 2) group];
    if (len(data) == 0) svg.group_class("marks slopes", [])
    else if (len(positions) != 2 or len(invalid) > 0) error("chart: slope requires two comparison positions and one row per entity at each position")
    else svg.group_class("marks slopes", content(line_mark([for (group in groups)
        for (row in sort(group.items, {by: (row) => center_coordinate(ctx, "x", row)})) row], ctx, options)))
}

fn trail_sample(row, ctx, options) {
    let width = appearance(ctx, "size", row, if (options.size != null) options.size else 2.0);
    let x = center_coordinate(ctx, "x", row);
    let y = center_coordinate(ctx, "y", row);
    if (not util.finite_number(width) or width < 0) error("chart: trail width must be finite and nonnegative")
    else if (not util.finite_number(x) or not util.finite_number(y)) error("chart: trail positions must be finite")
    else {x: x, y: y, width: width, row: row}
}

pub fn trail_mark(data, ctx, options) {
    let groups = [for (group in series(data, ctx, color.default_color))
        [for (row in group.items) trail_sample(row, ctx, options)]];
    let failure = util.first_error([for (group in groups) for (sample in group) sample]);
    if (failure is error) failure
    else if (options.interpolate != null and options.interpolate != "linear") error("chart: trail supports linear interpolation")
    else svg.group_class("marks trails", [for (samples in groups where len(samples) > 0) (
        // One compound path paints overlaps once, keeping translucent round joins uniform.
        let segments = [
            for (index in 1 to (len(samples) - 1),
                let a = samples[index - 1], let b = samples[index],
                let length = math.sqrt((b.x - a.x) ** 2 + (b.y - a.y) ** 2) where length > 0) (
                let nx = (a.y - b.y) / length,
                let ny = (b.x - a.x) / length,
                let corners = [[a.x - nx * a.width / 2.0, a.y - ny * a.width / 2.0],
                    [b.x - nx * b.width / 2.0, b.y - ny * b.width / 2.0],
                    [b.x + nx * b.width / 2.0, b.y + ny * b.width / 2.0],
                    [a.x + nx * a.width / 2.0, a.y + ny * a.width / 2.0]],
                svg.line_path(corners) ++ " Z"),
            for (sample in samples where sample.width > 0)
                svg.arc_path(sample.x, sample.y, 0.0, sample.width / 2.0, 0.0, util.TAU)],
        <path d: join(segments, " "), *:style(ctx, samples[0].row, options,
            {fill: color.default_color, stroke: "none", opacity: 1.0}), tooltip(ctx, samples[0].row)>)])
}

pub fn image_mark(data, ctx, options) {
    let images = [for (row in data) (
        let href = parse.channel_value(ctx.encoding.url, row, if (options.url != null) options.url else row.url),
        let x = center_coordinate(ctx, "x", row), let y = center_coordinate(ctx, "y", row),
        let x2 = if (has_position(ctx, "x2")) coordinate(ctx, "x2", row) else null,
        let y2 = if (has_position(ctx, "y2")) coordinate(ctx, "y2", row) else null,
        let width = if (x2 != null) abs(x2 - x) else if (options.width != null) options.width else 20.0,
        let height = if (y2 != null) abs(y2 - y) else if (options.height != null) options.height else 20.0,
        if (not (href is string) or len(href) == 0 or not util.finite_number(x) or not util.finite_number(y) or
            not util.finite_number(width) or not util.finite_number(height) or width < 0 or height < 0)
            error("chart: image requires a URL, finite position, and nonnegative dimensions")
        else <image href: href, x: if (x2 != null) min([x, x2]) else x - (if (options.align == "left") 0 else if (options.align == "right") width else width / 2.0),
            y: if (y2 != null) min([y, y2]) else y - (if (options.baseline == "top") 0 else if (options.baseline == "bottom") height else height / 2.0),
            width: width, height: height,
            preserveAspectRatio: if (options.preserve_aspect_ratio != null) options.preserve_aspect_ratio else if (options.aspect == false) "none" else "xMidYMid meet",
            opacity: appearance(ctx, "opacity", row, if (options.opacity != null) options.opacity else 1.0), tooltip(ctx, row)>)];
    let failure = util.first_error(images);
    if (failure is error) failure else svg.group_class("marks images", images)
}

pub fn violin_mark(data, ctx, options) {
    let horizontal = ctx.x_type == "quantitative";
    let measure = if (horizontal) "x" else "y";
    let category = if (horizontal) "y" else "x";
    let category_field = ctx[category ++ "_field"];
    let fields = util.unique_vals([category_field, for (field in [ctx.color_field, ctx.detail_field] where field != null) field]);
    let groups = [for (partition in records.group_by(data, fields)) (
        let samples = if (options.density_field != null) [for (row in partition.rows)
            {value: parse.channel_value(ctx.encoding[measure], row), density: row[options.density_field]}]
            // Private density records keep generated field names out of user data.
            else statistics.density([for (row in partition.rows) {value: parse.channel_value(ctx.encoding[measure], row)}],
                {field: "value", steps: options.steps, bandwidth: options.bandwidth, extent: options.extent}),
        {row: partition.rows[0], samples: samples})];
    let failure = util.first_error(groups |> ~.samples);
    let invalid = if (failure is error) [] else [for (group in groups) for (sample in group.samples
        where not util.finite_number(sample.value) or not util.finite_number(sample.density) or sample.density < 0) sample];
    let bandwidth = abs(ctx[category ++ "_scale"].bandwidth);
    let width = if (options.width != null) options.width else bandwidth;
    if (category_field == null or ctx[measure ++ "_field"] == null or ctx.encoding[measure].dtype != "quantitative" or
        ctx[category ++ "_scale"].kind != "band")
        error("chart: violin requires a categorical band axis and a quantitative measurement axis")
    else if (failure is error) failure
    else if (not util.finite_number(width) or width <= 0 or len(invalid) > 0 or
        (options.density_resolve != null and options.density_resolve != "shared" and options.density_resolve != "independent"))
        error("chart: invalid violin width, density, or density resolution")
    else svg.group_class("marks violins", [for (group in groups where len(group.samples) > 0) (
        let peak = if (options.density_resolve == "shared") max([for (candidate in groups) for (sample in candidate.samples) sample.density])
            else max(group.samples |> ~.density),
        let center = center_coordinate(ctx, category, group.row),
        let samples = sort(group.samples, {by: (sample) => sample.value}),
        let sides = [for (sign in [-1.0, 1.0]) [for (sample in samples) (
            let position = scale.scale_apply(ctx[measure ++ "_scale"], sample.value),
            let offset = if (peak > 0) sign * width / 2.0 * sample.density / peak else 0.0,
            if (horizontal) [position, center + offset] else [center + offset, position])]],
        <path d: svg.area_path(sides[0], sides[1], options.interpolate),
            *:style(ctx, group.row, options, {fill: color.default_color, stroke: "white", 'stroke-width': 1, opacity: 0.7}),
            tooltip(ctx, group.row)> )])
}

// ============================================================
// Area mark
// ============================================================

pub fn area_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let plot_h = ctx.plot_h;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let detail_field = if (ctx.detail_field) ctx.detail_field else null;
    let is_stacked = if (ctx.is_stacked) ctx.is_stacked else false;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 0.5;

    let series = series(data, ctx, fill_color);

    let area_elements = [for (s in series) (
        let top_points = [for (d in s.items) (
            let x_pos = coordinate(ctx, "x", d),
            let y_pos = if (is_stacked)
                float(scale.scale_apply(y_scale, float(d["_y1"])))
            else coordinate(ctx, "y", d),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, y_pos])],
        let bottom_points = [for (d in s.items) (
            let x_pos = coordinate(ctx, "x", d),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            let y_bottom = if (is_stacked)
                float(scale.scale_apply(y_scale, float(d["_y0"])))
            else if (has_position(ctx, "y2")) coordinate(ctx, "y2", d) else plot_h,
            [x_pos + bw, y_bottom])],
        let d = svg.area_path(top_points, bottom_points, mark_config.interpolate),
        <path d: d, *:style(ctx, s.items[0], mark_config, {fill: s.color, opacity: opacity, stroke: "none"}),
            tooltip(ctx, s.items[0])>
    )];

    svg.group_class("marks areas", area_elements)
}

// ============================================================
// Point (scatter) mark
// ============================================================

pub fn point_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let size_scale = ctx.size_scale;
    let size_field = ctx.size_field;
    let opacity_scale = ctx.opacity_scale;
    let opacity_field = ctx.opacity_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let tooltip_field = if (ctx.tooltip_field) ctx.tooltip_field else null;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let base_opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;
    let base_size = if (mark_config and mark_config.size != null) mark_config.size else 30;
    let base_r = math.sqrt(float(base_size) / util.PI);

    let points = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let y_pos = coordinate(ctx, "y", d),
        let bw_x = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let bw_y = if (y_scale.bandwidth) y_scale.bandwidth / 2.0 else 0.0,
        let pt_size = appearance(ctx, "size", d, base_size),
        let shape = appearance(ctx, "shape", d, if (mark_config.shape != null) mark_config.shape else "circle"),
        svg.symbol_mark(shape, x_pos + bw_x, y_pos + bw_y, pt_size,
            style(ctx, d, mark_config, {fill: fill_color, opacity: base_opacity, stroke: "white", 'stroke-width': 0.5}),
            [tooltip(ctx, d)])
    )];

    svg.group_class("marks points", points)
}

// ============================================================
// Arc (pie/donut) mark
// ============================================================

// Pie shares and explicit intervals use the same radial validation and appearance.
fn arc_element(row, start_angle, end_angle, ctx, options, fill = color.default_color) {
    let outer = coordinate(ctx, "radius", row, ctx.outer_radius);
    let inner = coordinate(ctx, "radius2", row, ctx.inner_radius);
    if (not util.finite_number(start_angle) or not util.finite_number(end_angle) or not util.finite_number(inner) or
        not util.finite_number(outer) or end_angle < start_angle or inner < 0 or outer < inner)
        error("chart: arcs require ordered finite angles and nonnegative ordered radii")
    else <path d: svg.arc_path(ctx.cx, ctx.cy, inner, outer, start_angle - util.PI / 2.0, end_angle - util.PI / 2.0),
        *:style(ctx, row, options, {fill: fill, opacity: if (options.opacity != null) options.opacity else 1.0,
            stroke: "white", 'stroke-width': 1}), tooltip(ctx, row)>
}

pub fn arc_mark(data, ctx, options) {
    let ranged = ctx.encoding.theta2 != null;
    let weights = if (ranged) [] else [for (row in data) parse.channel_value(ctx.encoding.theta, row)];
    let pad = if (options.pad_angle != null) options.pad_angle else 0.0;
    let invalid = [for (weight in weights where not util.finite_number(weight) or weight < 0) weight];
    let total = if (len(invalid) == 0) sum(weights) else 0.0;
    if (not ranged and (len(invalid) > 0 or not util.finite_number(total))) error("chart: pie weights and their total must be finite and nonnegative")
    else if (not util.finite_number(pad) or pad < 0) error("chart: arc padding must be finite and nonnegative")
    else {
        let arcs = [for (index, row in data where ranged or (total > 0 and weights[index] > 0)) (
            let base_angle = if (ranged) 0.0 else sum(slice(weights, 0, index)) / total * util.TAU,
            let start_angle = if (ranged) coordinate(ctx, "theta", row) else base_angle + pad / 2.0,
            let end_angle = if (ranged) coordinate(ctx, "theta2", row) else base_angle + weights[index] / total * util.TAU - pad / 2.0,
            let fill = if (ranged) color.default_color else color.pick_color(color.category10, index),
            arc_element(row, start_angle, end_angle, ctx, options, fill))];
        let failure = util.first_error(arcs);
        if (failure is error) failure else svg.group_class("marks arcs", arcs)
    }
}

// ============================================================
// ============================================================
// Text mark
// ============================================================

pub fn text_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let text_field = ctx.text_field;
    let font_size = if (mark_config and mark_config.font_size != null) mark_config.font_size else 11;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else "#333";

    let texts = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let y_pos = coordinate(ctx, "y", d),
        let bw_x = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let label = util.format_value(parse.channel_value(ctx.encoding.text, d, d[y_field]),
            ctx.encoding.text.format, if (ctx.encoding.text._temporal) "temporal" else ctx.encoding.text.dtype,
            ctx.encoding.text.scale.timezone),
        <text x: x_pos + bw_x, y: y_pos - 4.0,
              'text-anchor': if (mark_config.align == "left") "start" else if (mark_config.align == "right") "end" else "middle",
              'font-size': appearance(ctx, "size", d, font_size),
              *:cfg.settings({'font-family': mark_config.font_family, 'font-weight': mark_config.font_weight}),
              *:style(ctx, d, mark_config, {fill: fill_color}),
            label; tooltip(ctx, d)
        >
    )];

    svg.group_class("marks text-labels", texts)
}

// ============================================================
// Rule mark (reference lines)
// ============================================================

pub fn rule_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let plot_w = ctx.plot_w;
    let plot_h = ctx.plot_h;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let y2_field = if (ctx.y2_field) ctx.y2_field else null;
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else "#888";
    let stroke_w = if (mark_config and mark_config.stroke_width != null) mark_config.stroke_width else 1.0;
    let dash = if (mark_config and mark_config.stroke_dash) mark_config.stroke_dash else null;

    let rules = [for (d in data) (
        let has_x = has_position(ctx, "x"),
        let has_y = has_position(ctx, "y"),
        let x_start = coordinate(ctx, "x", d, 0.0),
        let y_start = coordinate(ctx, "y", d, 0.0),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let x_end = coordinate(ctx, "x2", d, if (has_x) x_start else plot_w),
        let y_end = coordinate(ctx, "y2", d, if (has_y) y_start else plot_h),
        let shifted_x = if (has_x and has_y) bw else 0.0,
        let base = svg.line(x_start + shifted_x, y_start, x_end + shifted_x, y_end,
            appearance(ctx, "stroke", d, appearance(ctx, "color", d, stroke_color)), stroke_w),
        let attrs = if (dash != null) {*:map(base), 'stroke-dasharray': dash} else map(base),
        if (has_x or has_y) <line *:attrs,
            *:style(ctx, d, mark_config, {stroke: stroke_color, 'stroke-width': stroke_w}, true), tooltip(ctx, d)> else null
    )] |: (~ != null);

    svg.group_class("marks rules", rules)
}

// ============================================================
// Tick mark (short ticks at data positions)
// ============================================================

pub fn tick_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let plot_h = ctx.plot_h;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let tick_length = if (mark_config and mark_config.size != null) mark_config.size else 12;
    let stroke_w = if (mark_config and mark_config.stroke_width != null) mark_config.stroke_width else 1.5;
    let half = float(tick_length) / 2.0;

    let ticks = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let y_pos = if (y_field) coordinate(ctx, "y", d) else plot_h,
        <line x1: x_pos + bw, y1: y_pos - half, x2: x_pos + bw, y2: y_pos + half,
            *:style(ctx, d, mark_config, {stroke: stroke_color, 'stroke-width': stroke_w}, true), tooltip(ctx, d)>
    )];

    svg.group_class("marks ticks", ticks)
}

// ============================================================
// Box plot (composite mark: rect box + whisker lines + median line + outlier circles)
// ============================================================

pub fn boxplot_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let extent = if (mark_config and mark_config.extent != null) float(mark_config.extent) else 1.5;
    let box_w = if (x_scale.bandwidth) abs(x_scale.bandwidth) * 0.6 else 20.0;

    // group data by x field
    let groups = util.unique_vals(data |> ~[x_field]);
    let elements = [for (g in groups) (
        let items = data |: ~[x_field] == g,
        let vals = [for (d in items) float(d[y_field])],
        let sorted_vals = [for (v in vals order by v) v],
        let q1 = math.quantile(sorted_vals, 0.25),
        let q3 = math.quantile(sorted_vals, 0.75),
        let med = math.quantile(sorted_vals, 0.5),
        let iqr = q3 - q1,
        let lo_fence = q1 - extent * iqr,
        let hi_fence = q3 + extent * iqr,
        let whisker_lo = min(sorted_vals |: ~ >= lo_fence),
        let whisker_hi = max(sorted_vals |: ~ <= hi_fence),
        let outliers = sorted_vals |: (~ < lo_fence or ~ > hi_fence),
        let x_pos = float(scale.scale_apply(x_scale, g)),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth else 30.0,
        let cx = x_pos + bw / 2.0,
        let box_x = cx - box_w / 2.0,
        let fill = appearance(ctx, "color", items[0], fill_color),
        let summary = {*:parse.attributes(items[0]), _q1: q1, _q3: q3, _median: med,
            _min: whisker_lo, _max: whisker_hi},
        let line_attrs = style(ctx, summary, mark_config, {stroke: "#333", 'stroke-width': 1}, true),
        let y_q1 = float(scale.scale_apply(y_scale, q1)),
        let y_q3 = float(scale.scale_apply(y_scale, q3)),
        let y_med = float(scale.scale_apply(y_scale, med)),
        let y_wlo = float(scale.scale_apply(y_scale, whisker_lo)),
        let y_whi = float(scale.scale_apply(y_scale, whisker_hi)),
        // box rect (q1 to q3)
        let box_rect = <rect x: box_x, y: min([y_q1, y_q3]), width: box_w, height: abs(y_q1 - y_q3),
            *:style(ctx, summary, mark_config, {fill: fill, opacity: 0.8, stroke: "#333", 'stroke-width': 1}), tooltip(ctx, summary)>,
        // median line
        let med_line = <line x1: box_x, y1: y_med, x2: box_x + box_w, y2: y_med,
                             *:style(ctx, summary, mark_config, {stroke: "#333", 'stroke-width': 2}, true), tooltip(ctx, summary)>,
        // lower whisker
        let wlo_line = <line x1: cx, y1: y_q1, x2: cx, y2: y_wlo,
                             *:line_attrs, tooltip(ctx, summary)>,
        let wlo_cap = <line x1: cx - box_w / 4.0, y1: y_wlo, x2: cx + box_w / 4.0, y2: y_wlo,
                            *:line_attrs, tooltip(ctx, summary)>,
        // upper whisker
        let whi_line = <line x1: cx, y1: y_q3, x2: cx, y2: y_whi,
                             *:line_attrs, tooltip(ctx, summary)>,
        let whi_cap = <line x1: cx - box_w / 4.0, y1: y_whi, x2: cx + box_w / 4.0, y2: y_whi,
                            *:line_attrs, tooltip(ctx, summary)>,
        // outlier circles
        let outlier_els = [for (row in items where row[y_field] < lo_fence or row[y_field] > hi_fence)
            <circle cx: cx, cy: float(scale.scale_apply(y_scale, row[y_field])), r: 3,
                fill: "none", *:style(ctx, row, mark_config, {stroke: "#333", 'stroke-width': 1}, true), tooltip(ctx, row)>],
        [wlo_line, wlo_cap, whi_line, whi_cap, box_rect, med_line, *outlier_els]
    )];

    let all = [for (group in elements) for (el in group) el];
    svg.group_class("marks boxplots", all)
}

// ============================================================
// Error bar mark (vertical line with cap ticks)
// ============================================================

pub fn errorbar_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let y2_field = if (ctx.y2_field) ctx.y2_field else null;
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else "#333";
    let stroke_w = if (mark_config and mark_config.stroke_width != null) mark_config.stroke_width else 1.5;
    let cap_w = 6.0;

    let bars = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let cx = x_pos + bw,
        let y_lo = coordinate(ctx, "y", d),
        let y_hi = if (y2_field)
            coordinate(ctx, "y2", d)
        else y_lo,
        let attrs = style(ctx, d, mark_config, {stroke: stroke_color, 'stroke-width': stroke_w}, true),
        let stem = <line x1: cx, y1: y_lo, x2: cx, y2: y_hi, *:attrs, tooltip(ctx, d)>,
        let cap_lo = <line x1: cx - cap_w, y1: y_lo, x2: cx + cap_w, y2: y_lo,
                           *:attrs, tooltip(ctx, d)>,
        let cap_hi = <line x1: cx - cap_w, y1: y_hi, x2: cx + cap_w, y2: y_hi,
                           *:attrs, tooltip(ctx, d)>,
        [stem, cap_lo, cap_hi]
    )];

    let all = [for (group in bars) for (el in group) el];
    svg.group_class("marks errorbars", all)
}

// ============================================================
// Error band mark (filled area between y and y2)
// ============================================================

pub fn errorband_mark(data, ctx, mark_config) {
    let secondary = if (ctx.encoding.y2 != null) ctx.encoding.y2 else ctx.encoding.y;
    let band_context = {*:ctx, y2_field: if (ctx.y2_field != null) ctx.y2_field else ctx.y_field,
        encoding: {*:parse.attributes(ctx.encoding), y2: secondary}};
    let result = area_mark(data, band_context, {*:mark_config,
        opacity: if (mark_config.opacity != null) mark_config.opacity else 0.3});
    svg.group_class("marks errorbands", content(result))
}

// ============================================================
// Rect mark (positioned rectangles for heatmaps)
// ============================================================

pub fn rect_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;

    let rects = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let y_pos = coordinate(ctx, "y", d),
        let x_end = coordinate(ctx, "x2", d, x_pos + (if (x_scale.bandwidth) x_scale.bandwidth else 20.0)),
        let y_end = coordinate(ctx, "y2", d, y_pos + (if (y_scale.bandwidth) y_scale.bandwidth else 20.0)),
        let w = abs(x_end - x_pos),
        let h = abs(y_end - y_pos),
        let rect_y = min([y_pos, y_end]),
        <rect x: min([x_pos, x_end]), y: rect_y, width: w, height: h,
              *:style(ctx, d, mark_config, {fill: fill_color, opacity: opacity, stroke: "white", 'stroke-width': 0.5}),
              tooltip(ctx, d)>
    )];

    svg.group_class("marks rects", rects)
}
