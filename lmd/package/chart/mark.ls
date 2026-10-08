// chart/mark.ls — Mark renderers for the chart library
// Each mark type transforms data + scales into SVG elements.
// All mark functions take (data, ctx, mark_config) where ctx is a render context map.

import util: .util
import svg: .svg
import color: .color
import scale: .scale
import parse: .parse

fn coordinate(ctx, channel_name, row, fallback = null) float | null | error {
    let channel = parse.channel_definition(parse.get_channel(ctx.encoding, channel_name), row);
    let value = parse.channel_value(channel, row, row[ctx[channel_name ++ "_field"]]);
    let axis = if (channel_name == "x2") "x" else if (channel_name == "y2") "y" else channel_name;
    let mapping = ctx[axis ++ "_scale"];
    if (value == null) fallback
    else if (channel.value != null) float(value)
    else if (mapping) float(scale.scale_apply(mapping, value)) else float(value)
}

fn has_position(ctx, channel_name) =>
    ctx[channel_name ++ "_field"] != null or parse.get_channel(ctx.encoding, channel_name) != null

pub fn appearance(ctx, channel_name, row, fallback) {
    let channel = parse.channel_definition(ctx.encoding[channel_name], row);
    let field = if (channel.field) channel.field else ctx[channel_name ++ "_field"];
    let value = parse.channel_value(channel, row, if (field != null) row[field] else fallback);
    let mapping = ctx[channel_name ++ "_scale"];
    if (channel.value != null or value == null or mapping == null) value
    else scale.scale_apply(mapping, value)
}

fn tooltip(ctx, row) {
    let channel = ctx.encoding.tooltip;
    if (channel == null and ctx.tooltip_field != null) <title string(row[ctx.tooltip_field])>
    else if (channel == null) null
    else {
        let fields = if (channel is array) channel else if (channel.fields != null)
            [for (field in channel.fields) {field: field}] else [channel];
        <title join([for (field in fields)
            (if (len(fields) > 1) (if (field.title != null) field.title else field.field) ++ ": " else "") ++
                util.format_value(parse.channel_value(field, row), field.format, field.dtype)], "\n")>
    }
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
            fill: bar_fill, opacity: bar_opacity, rx: rx,
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
              fill: bar_fill, opacity: appearance(ctx, "opacity", d, opacity), tooltip(ctx, d)>
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

    // group by detail field first, then color field
    let group_field = if (detail_field) detail_field else color_field;
    let series = if (group_field)
        (let groups = util.unique_vals(data |> ~[group_field]),
        [for (g in groups) (
            let group_items = data |: ~[group_field] == g,
            {
                key: g,
                items: group_items,
                color: appearance(ctx, "color", group_items[0], stroke_color)
            })])
    else [{key: null, items: data, color: appearance(ctx, "color", data[0], stroke_color)}];

    let line_elements = [for (s in series) (
        let points = [for (d in s.items) (
            let x_pos = coordinate(ctx, "x", d),
            let y_pos = coordinate(ctx, "y", d),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, y_pos])],
        let d = svg.line_path(points, mark_config.interpolate),
        let line_el = <path d: d, fill: "none", stroke: appearance(ctx, "stroke", s.items[0], s.color),
                            'stroke-width': stroke_w, opacity: appearance(ctx, "opacity", s.items[0], opacity),
                            tooltip(ctx, s.items[0])>,
        let point_els = if (show_points)
            [for (p in points)
                <circle cx: p[0], cy: p[1], r: 3, fill: s.color,
                        stroke: "white", 'stroke-width': 1>]
        else [],
        [line_el, *point_els]
    )];

    // flatten the nested arrays
    let all = [for (group in line_elements) for (el in group) el];
    svg.group_class("marks lines", all)
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

    // group by detail field first, then color field
    let group_field = if (detail_field) detail_field else color_field;
    let series = if (group_field)
        (let groups = util.unique_vals(data |> ~[group_field]),
        [for (g in groups) (
            let group_items = data |: ~[group_field] == g,
            {
                key: g,
                items: group_items,
                color: appearance(ctx, "color", group_items[0], fill_color)
            })])
    else [{key: null, items: data, color: appearance(ctx, "color", data[0], fill_color)}];

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
        <path d: d, fill: s.color, opacity: appearance(ctx, "opacity", s.items[0], opacity), stroke: "none", tooltip(ctx, s.items[0])>
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
        let pt_fill = appearance(ctx, "color", d, fill_color),
        let pt_size = appearance(ctx, "size", d, base_size),
        let pt_opacity = appearance(ctx, "opacity", d, base_opacity),
        let shape = appearance(ctx, "shape", d, if (mark_config.shape != null) mark_config.shape else "circle"),
        let stroke = appearance(ctx, "stroke", d, if (mark_config.stroke != null) mark_config.stroke else "white"),
        svg.symbol_mark(shape, x_pos + bw_x, y_pos + bw_y, pt_size,
            {fill: pt_fill, opacity: pt_opacity, stroke: stroke,
                'stroke-width': if (mark_config.stroke_width != null) mark_config.stroke_width else 0.5},
            [tooltip(ctx, d)])
    )];

    svg.group_class("marks points", points)
}

// ============================================================
// Arc (pie/donut) mark
// ============================================================

pub fn arc_mark(data, ctx, mark_config) {
    let theta_field = ctx.theta_field;
    let color_scale = ctx.color_scale;
    let color_field = ctx.color_field;
    let cx = ctx.cx;
    let cy = ctx.cy;
    let inner_radius = ctx.inner_radius;
    let outer_radius = ctx.outer_radius;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 1.0;
    let pad = if (mark_config and mark_config.pad_angle) mark_config.pad_angle else 0.0;

    // compute total
    let total = sum(data |> float(~[theta_field]));

    // build cumulative angle array
    let angle_data = [for (i in 0 to (len(data) - 1)) (
        let val = float(data[i][theta_field]),
        let start = if (i == 0) 0.0
            else
                (let preceding = [for (j in 0 to (i - 1)) float(data[j][theta_field])],
                sum(preceding) / total * util.TAU),
        let end = start + val / total * util.TAU,
        {index: i, start: start + pad / 2.0, end: end - pad / 2.0, datum: data[i]}
    )];

    let arcs = [for (a in angle_data) (
        let fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, a.datum[color_field])
        else color.pick_color(color.category10, a.index),
        let d = svg.arc_path(cx, cy, inner_radius, outer_radius, a.start - util.PI / 2.0, a.end - util.PI / 2.0),
        <path d: d, fill: fill, opacity: opacity, stroke: "white", 'stroke-width': 1>
    )];

    svg.group_class("marks arcs", arcs)
}

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
            ctx.encoding.text.format, ctx.encoding.text.dtype),
        <text x: x_pos + bw_x, y: y_pos - 4.0,
              'text-anchor': "middle", 'font-size': font_size, fill: appearance(ctx, "color", d, fill_color),
            label
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
        if (has_x or has_y) <line *:attrs, tooltip(ctx, d)> else null
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
        svg.line(x_pos + bw, y_pos - half, x_pos + bw, y_pos + half, stroke_color, stroke_w)
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
        let fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, g)
        else fill_color,
        let y_q1 = float(scale.scale_apply(y_scale, q1)),
        let y_q3 = float(scale.scale_apply(y_scale, q3)),
        let y_med = float(scale.scale_apply(y_scale, med)),
        let y_wlo = float(scale.scale_apply(y_scale, whisker_lo)),
        let y_whi = float(scale.scale_apply(y_scale, whisker_hi)),
        // box rect (q1 to q3)
        let box_rect = <rect x: box_x, y: y_q3, width: box_w, height: y_q1 - y_q3,
                             fill: fill, opacity: 0.8, stroke: "#333", 'stroke-width': 1>,
        // median line
        let med_line = <line x1: box_x, y1: y_med, x2: box_x + box_w, y2: y_med,
                             stroke: "#333", 'stroke-width': 2>,
        // lower whisker
        let wlo_line = <line x1: cx, y1: y_q1, x2: cx, y2: y_wlo,
                             stroke: "#333", 'stroke-width': 1>,
        let wlo_cap = <line x1: cx - box_w / 4.0, y1: y_wlo, x2: cx + box_w / 4.0, y2: y_wlo,
                            stroke: "#333", 'stroke-width': 1>,
        // upper whisker
        let whi_line = <line x1: cx, y1: y_q3, x2: cx, y2: y_whi,
                             stroke: "#333", 'stroke-width': 1>,
        let whi_cap = <line x1: cx - box_w / 4.0, y1: y_whi, x2: cx + box_w / 4.0, y2: y_whi,
                            stroke: "#333", 'stroke-width': 1>,
        // outlier circles
        let outlier_els = [for (o in outliers)
            <circle cx: cx, cy: float(scale.scale_apply(y_scale, o)), r: 3,
                    fill: "none", stroke: "#333", 'stroke-width': 1>],
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
        let stem = <line x1: cx, y1: y_lo, x2: cx, y2: y_hi,
                         stroke: stroke_color, 'stroke-width': stroke_w>,
        let cap_lo = <line x1: cx - cap_w, y1: y_lo, x2: cx + cap_w, y2: y_lo,
                           stroke: stroke_color, 'stroke-width': stroke_w>,
        let cap_hi = <line x1: cx - cap_w, y1: y_hi, x2: cx + cap_w, y2: y_hi,
                           stroke: stroke_color, 'stroke-width': stroke_w>,
        [stem, cap_lo, cap_hi]
    )];

    let all = [for (group in bars) for (el in group) el];
    svg.group_class("marks errorbars", all)
}

// ============================================================
// Error band mark (filled area between y and y2)
// ============================================================

pub fn errorband_mark(data, ctx, mark_config) {
    let x_scale = ctx.x_scale;
    let y_scale = ctx.y_scale;
    let x_field = ctx.x_field;
    let y_field = ctx.y_field;
    let y2_field = if (ctx.y2_field) ctx.y2_field else null;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let opacity = if (mark_config and mark_config.opacity != null) mark_config.opacity else 0.3;

    let top_points = [for (d in data) (
        let x_pos = coordinate(ctx, "x", d),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let y_pos = coordinate(ctx, "y", d),
        [x_pos + bw, y_pos])];

    let bottom_points = if (y2_field)
        [for (d in data) (
            let x_pos = coordinate(ctx, "x", d),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            let y_pos = coordinate(ctx, "y2", d),
            [x_pos + bw, y_pos])]
    else top_points;

    let d = svg.area_path(top_points, bottom_points);
    let band = <path d: d, fill: fill_color, opacity: opacity, stroke: "none">;
    svg.group_class("marks errorbands", [band])
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
        let rect_fill = appearance(ctx, "color", d, fill_color),
        <rect x: min([x_pos, x_end]), y: rect_y, width: w, height: h,
              fill: rect_fill, opacity: appearance(ctx, "opacity", d, opacity),
              stroke: appearance(ctx, "stroke", d, if (mark_config.stroke != null) mark_config.stroke else "white"),
              'stroke-width': if (mark_config.stroke_width != null) mark_config.stroke_width else 0.5,
              tooltip(ctx, d)>
    )];

    svg.group_class("marks rects", rects)
}
