// chart/axis.ls — Axis generation for the chart library
// Generates SVG elements for x and y axes with ticks, labels, and titles.

import util: .util
import svg: .svg
import scale: .scale

// ============================================================
// Default axis configuration
// ============================================================

pub let default_axis_config = {
    tick_size: 5,
    tick_count: 8,
    label_font_size: 11,
    label_offset: 3,
    title_font_size: 13,
    title_padding: 30,
    domain_color: "#888",
    tick_color: "#888",
    grid_color: "#e0e0e0",
    label_color: "#333",
    title_color: "#333",
    enabled: true, domain: true, ticks: true, labels: true, label_angle: 0
}

// ============================================================
// Merge config helper: override defaults with provided config
// ============================================================

fn merge_config(config) {
    if config != null { {*:default_axis_config, *:config} }
    else { default_axis_config }
}

// ============================================================
// Temporal tick label formatting
// ============================================================

fn temporal_auto_format(lo_ms, hi_ms) {
    let span = hi_ms - lo_ms;
    let ms_day = 86400000.0;
    let ms_month = ms_day * 30.0;
    let ms_year = ms_day * 365.0;
    if (span > ms_year * 2.0) "YYYY"
    else if (span > ms_month * 2.0) "MMM YYYY"
    else if (span > ms_day * 2.0) "MMM DD"
    else if (span > 3600000.0 * 2.0) "hh:mm"
    else "hh:mm:ss"
}

fn format_tick_label(sc, tv, config = null) {
    if (sc.kind == "temporal")
        datetime(i64(tv)).format(if (config and config.format != null) config.format else temporal_auto_format(sc.domain[0], sc.domain[1]))
    else util.format_value(tv, config.format)
}

fn tick_values(sc, config) {
    let values = if (config.values != null) config.values else scale.scale_ticks(sc, config.tick_count);
    if (config.tick_min_step != null and len(values) > 1)
        [for (index, value in values where index == 0 or abs(float(value) - float(values[index - 1])) >= config.tick_min_step) value]
    else values
}

fn tick_label(sc, value, config) {
    let label = format_tick_label(sc, value, config);
    let limit = if (config.label_limit != null) int(floor(config.label_limit / (float(config.label_font_size) * 0.6))) else len(label);
    if (len(label) > limit) slice(label, 0, max([0, limit - 1])) ++ "…" else label
}

// Both axes share guide visibility, placement, and label policy.
fn render_axis(sc, pw, ph, config, title_text, horizontal) {
    let cfg = merge_config(config);
    if (cfg.enabled == false or sc.kind == "identity") null
    else {
        let values = tick_values(sc, cfg);
        let band_offset = if (sc.kind == "band") sc.bandwidth / 2.0 else 0.0;
        let far_side = if (horizontal) cfg.orient == "top" else cfg.orient == "right";
        let baseline = if (horizontal) (if (far_side) 0.0 else ph) else if (far_side) pw else 0.0;
        let direction = if (horizontal) (if (far_side) -1.0 else 1.0) else if (far_side) 1.0 else -1.0;
        let domain_line = if (cfg.domain == false) null else if (horizontal)
            svg.line(0, baseline, pw, baseline, cfg.domain_color, 1)
            else svg.line(baseline, 0, baseline, ph, cfg.domain_color, 1);
        let angle = if (cfg.label_angle != null) cfg.label_angle else 0;
        let label_attrs = if (angle != 0) {transform: "rotate(" ++ util.fmt_num(angle) ++ ")"} else {};
        let label_anchor = if (cfg.label_align == "left") "start" else if (cfg.label_align == "right") "end"
            else if (cfg.label_align == "center" or horizontal) "middle" else if (far_side) "start" else "end";
        let max_label = max([0, for (value in values) len(tick_label(sc, value, cfg))]);
        let stride = if (cfg.label_overlap == "hide" and horizontal)
            max([1, int(ceil(float(max_label * len(values)) * cfg.label_font_size * 0.6 / max([1.0, pw])))]) else 1;
        let tick_elements = [for (index, value in values) (
            let position = float(scale.scale_apply(sc, value)) + band_offset,
            let extent = if (horizontal) pw else ph,
            if (position >= -1.0 and position <= extent + 1.0)
                <g class: "tick", transform: if (horizontal) svg.translate(position, baseline) else svg.translate(baseline, position),
                    if (cfg.ticks != false) (if (horizontal)
                        svg.line(0, 0, 0, direction * cfg.tick_size, cfg.tick_color, 1)
                        else svg.line(0, 0, direction * cfg.tick_size, 0, cfg.tick_color, 1));
                    if (cfg.labels != false and index % stride == 0)
                        <text x: if (horizontal) 0 else direction * (cfg.tick_size + cfg.label_offset),
                            y: if (horizontal) direction * (cfg.tick_size + cfg.label_offset + cfg.label_font_size)
                                else cfg.label_font_size / 3.0,
                            'text-anchor': label_anchor, 'font-size': cfg.label_font_size, fill: cfg.label_color,
                            *:label_attrs, tick_label(sc, value, cfg)>
                >
            else null
        )] |: (~ != null);
        let title = if (cfg.title_enabled == false) null else if (cfg.title != null) cfg.title else title_text;
        let title_position = if (horizontal) baseline + direction * (cfg.title_padding + cfg.title_font_size)
            else baseline + direction * (cfg.title_padding + cfg.label_font_size);
        let title_el = if (title) (
            if (horizontal) <text x: float(pw) / 2.0, y: title_position,
                'text-anchor': "middle", 'font-size': cfg.title_font_size, fill: cfg.title_color, title>
            else <text x: title_position, y: float(ph) / 2.0,
                'text-anchor': "middle", 'font-size': cfg.title_font_size, fill: cfg.title_color,
                transform: svg.rotate(if (far_side) 90 else -90, title_position, float(ph) / 2.0), title>
        ) else null;
        let children = [for (item in [domain_line, *tick_elements, title_el] where item != null) item];
        let result = svg.group_class(if (horizontal) "axis x-axis" else "axis y-axis", children);
        if (cfg.offset != null and cfg.offset != 0)
            svg.group(if (horizontal) svg.translate(0, cfg.offset) else svg.translate(cfg.offset, 0), [result])
        else result
    }
}

pub fn x_axis(sc, pw, ph, config, title_text) => render_axis(sc, pw, ph, config, title_text, true)
pub fn y_axis(sc, pw, ph, config, title_text) => render_axis(sc, pw, ph, config, title_text, false)

fn render_grid(sc, pw, ph, config, horizontal) {
    let cfg = merge_config(config);
    let values = tick_values(sc, cfg);
    let band_offset = if (sc.kind == "band") sc.bandwidth / 2.0 else 0.0;
    let lines = [for (value in values) (
        let position = float(scale.scale_apply(sc, value)) + band_offset,
        let extent = if (horizontal) pw else ph,
        if (position > 0.0 and position < extent)
            <line x1: if (horizontal) position else 0, y1: if (horizontal) 0 else position,
                x2: if (horizontal) position else pw, y2: if (horizontal) ph else position,
                stroke: cfg.grid_color, 'stroke-width': 1,
                'stroke-dasharray': if (cfg.grid_dash != null) cfg.grid_dash else "4,4">
        else null
    )] |: (~ != null);
    svg.group_class(if (horizontal) "grid x-grid" else "grid y-grid", lines)
}

pub fn x_axis_grid(sc, pw, ph, config) => render_grid(sc, pw, ph, config, true)
pub fn y_axis_grid(sc, pw, ph, config) => render_grid(sc, pw, ph, config, false)

// ============================================================
// Compute space needed for axes
// ============================================================

pub fn estimate_y_axis_width(sc, config) {
    let cfg = merge_config(config);
    let ticks = scale.scale_ticks(sc, cfg.tick_count);
    let label_lens = [for (tv in ticks) len(format_tick_label(sc, tv, cfg))];
    let max_label_len = if (len(label_lens) > 0) max(label_lens) else 3;
    float(max_label_len) * 7.0 * float(cfg.label_font_size) / 11.0 + float(cfg.tick_size) + 10.0
}

pub fn estimate_x_axis_height(config, has_title: bool) {
    let cfg = merge_config(config);
    let base = float(cfg.tick_size + cfg.label_font_size + 8);
    if (has_title) base + float(cfg.title_font_size) + 8.0
    else base
}
