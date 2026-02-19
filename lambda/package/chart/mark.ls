// chart/mark.ls — Mark renderers for the chart library
// Each mark type transforms data + scales into SVG elements.
// All mark functions take (data, ctx, mark_config) where ctx is a render context map.

import util: .lambda.package.chart.util
import svg: .lambda.package.chart.svg
import color: .lambda.package.chart.color
import scale: .lambda.package.chart.scale

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
    let fill = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let base_opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 1.0;
    let rx = if (mark_config and mark_config.corner_radius) mark_config.corner_radius else 0;

    let bars = (for (d in data) (
        let x_val = d[x_field],
        let y_val = float(d[y_field]),
        let x_pos = float(scale.scale_apply(x_scale, x_val)),
        let y_pos = float(scale.scale_apply(y_scale, y_val)),
        let bar_fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, d[color_field])
        else fill,
        let bar_opacity = if (opacity_scale and opacity_field)
            float(scale.scale_apply(opacity_scale, float(d[opacity_field])))
        else base_opacity,
        let bar_w = if (x_scale.bandwidth) x_scale.bandwidth else 20.0,
        let bar_h = plot_h - y_pos,
        <rect x: x_pos, y: y_pos, width: bar_w, height: bar_h,
              fill: bar_fill, opacity: bar_opacity, rx: rx>
    ));

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
    let opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 1.0;

    let bars = (for (d in data) (
        let y_val = d[y_field],
        let x_val = float(d[x_field]),
        let y_pos = float(scale.scale_apply(y_scale, y_val)),
        let x_pos = float(scale.scale_apply(x_scale, x_val)),
        let bar_fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, d[color_field])
        else fill,
        let bar_h = if (y_scale.bandwidth) y_scale.bandwidth else 20.0,
        <rect x: 0, y: y_pos, width: x_pos, height: bar_h,
              fill: bar_fill, opacity: opacity>
    ));

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
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let stroke_w = if (mark_config and mark_config.stroke_width) mark_config.stroke_width else 2.0;
    let opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 1.0;
    let show_points = if (mark_config and mark_config.point) mark_config.point else false;

    // group by color field if present
    let series = if (color_field)
        (let groups = util.unique_vals(data | ~[color_field]),
        (for (g in groups) {
            key: g,
            items: data that ~[color_field] == g,
            color: if (color_scale) scale.scale_apply(color_scale, g) else stroke_color
        }))
    else [{key: null, items: data, color: stroke_color}];

    let line_elements = (for (s in series) (
        let points = (for (d in s.items) (
            let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
            let y_pos = float(scale.scale_apply(y_scale, d[y_field])),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, y_pos])),
        let d = svg.line_path(points),
        let line_el = <path d: d, fill: "none", stroke: s.color,
                            "stroke-width": stroke_w, opacity: opacity>,
        let point_els = if (show_points)
            (for (p in points)
                <circle cx: p[0], cy: p[1], r: 3, fill: s.color,
                        stroke: "white", "stroke-width": 1>)
        else [],
        [line_el, *point_els]
    ));

    // flatten the nested arrays
    let all = (for (group in line_elements) for (el in group) el);
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
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 0.5;

    let series = if (color_field)
        (let groups = util.unique_vals(data | ~[color_field]),
        (for (g in groups) {
            key: g,
            items: data that ~[color_field] == g,
            color: if (color_scale) scale.scale_apply(color_scale, g) else fill_color
        }))
    else [{key: null, items: data, color: fill_color}];

    let area_elements = (for (s in series) (
        let top_points = (for (d in s.items) (
            let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
            let y_pos = float(scale.scale_apply(y_scale, d[y_field])),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, y_pos])),
        let bottom_points = (for (d in s.items) (
            let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
            let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
            [x_pos + bw, plot_h])),
        let d = svg.area_path(top_points, bottom_points),
        <path d: d, fill: s.color, opacity: opacity, stroke: "none">
    ));

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
    let fill_color = if (mark_config and mark_config.color) mark_config.color else color.default_color;
    let base_opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 1.0;
    let base_size = if (mark_config and mark_config.size) mark_config.size else 30;
    let base_r = sqrt(float(base_size) / util.PI);

    let points = (for (d in data) (
        let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
        let y_pos = float(scale.scale_apply(y_scale, d[y_field])),
        let bw_x = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let bw_y = if (y_scale.bandwidth) y_scale.bandwidth / 2.0 else 0.0,
        let pt_fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, d[color_field])
        else fill_color,
        let pt_r = if (size_scale and size_field)
            (let sv = float(d[size_field]),
            sqrt(float(scale.scale_apply(size_scale, sv)) / util.PI))
        else base_r,
        let pt_opacity = if (opacity_scale and opacity_field)
            float(scale.scale_apply(opacity_scale, float(d[opacity_field])))
        else base_opacity,
        <circle cx: x_pos + bw_x, cy: y_pos + bw_y, r: pt_r,
                fill: pt_fill, opacity: pt_opacity,
                stroke: "white", "stroke-width": 0.5>
    ));

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
    let opacity = if (mark_config and mark_config.opacity) mark_config.opacity else 1.0;
    let pad = if (mark_config and mark_config.pad_angle) mark_config.pad_angle else 0.0;

    // compute total
    let total = sum(data | float(~[theta_field]));

    // build cumulative angle array
    let angle_data = (for (i in 0 to (len(data) - 1)) (
        let val = float(data[i][theta_field]),
        let start = if (i == 0) 0.0
            else
                (let preceding = (for (j in 0 to (i - 1)) float(data[j][theta_field])),
                sum(preceding) / total * util.TAU),
        let end = start + val / total * util.TAU,
        {index: i, start: start + pad / 2.0, end: end - pad / 2.0, datum: data[i]}
    ));

    let arcs = (for (a in angle_data) (
        let fill = if (color_scale and color_field)
            scale.scale_apply(color_scale, a.datum[color_field])
        else color.pick_color(color.category10, a.index),
        let d = svg.arc_path(cx, cy, inner_radius, outer_radius, a.start - util.PI / 2.0, a.end - util.PI / 2.0),
        <path d: d, fill: fill, opacity: opacity, stroke: "white", "stroke-width": 1>
    ));

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
    let font_size = if (mark_config and mark_config.font_size) mark_config.font_size else 11;
    let fill_color = if (mark_config and mark_config.color) mark_config.color else "#333";

    let texts = (for (d in data) (
        let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
        let y_pos = float(scale.scale_apply(y_scale, d[y_field])),
        let bw_x = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let label = if (text_field) string(d[text_field]) else string(d[y_field]),
        <text x: x_pos + bw_x, y: y_pos - 4.0,
              "text-anchor": "middle", "font-size": font_size, fill: fill_color;
            label
        >
    ));

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
    let stroke_color = if (mark_config and mark_config.color) mark_config.color else "#888";
    let stroke_w = if (mark_config and mark_config.stroke_width) mark_config.stroke_width else 1.0;
    let dash = if (mark_config and mark_config.stroke_dash) mark_config.stroke_dash else null;

    let rules = (for (d in data)
        if (y_field and not x_field)
            // horizontal rule
            (let y_pos = float(scale.scale_apply(y_scale, d[y_field])),
            if (dash)
                <line x1: 0, y1: y_pos, x2: plot_w, y2: y_pos,
                      stroke: stroke_color, "stroke-width": stroke_w,
                      "stroke-dasharray": dash>
            else
                svg.line(0, y_pos, plot_w, y_pos, stroke_color, stroke_w))
        else if (x_field and not y_field)
            // vertical rule
            (let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
            if (dash)
                <line x1: x_pos, y1: 0, x2: x_pos, y2: plot_h,
                      stroke: stroke_color, "stroke-width": stroke_w,
                      "stroke-dasharray": dash>
            else
                svg.line(x_pos, 0, x_pos, plot_h, stroke_color, stroke_w))
        else null
    ) that (~ != null);

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
    let tick_length = if (mark_config and mark_config.size) mark_config.size else 12;
    let stroke_w = if (mark_config and mark_config.stroke_width) mark_config.stroke_width else 1.5;
    let half = float(tick_length) / 2.0;

    let ticks = (for (d in data) (
        let x_pos = float(scale.scale_apply(x_scale, d[x_field])),
        let bw = if (x_scale.bandwidth) x_scale.bandwidth / 2.0 else 0.0,
        let y_pos = if (y_field) float(scale.scale_apply(y_scale, d[y_field])) else plot_h,
        svg.line(x_pos + bw, y_pos - half, x_pos + bw, y_pos + half, stroke_color, stroke_w)
    ));

    svg.group_class("marks ticks", ticks)
}
