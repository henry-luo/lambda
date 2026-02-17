// chart/axis.ls — Axis generation for the chart library
// Generates SVG elements for x and y axes with ticks, labels, and titles.

import util: .lambda.package.chart.util
import svg: .lambda.package.chart.svg
import scale: .lambda.package.chart.scale

// ============================================================
// Default axis configuration
// ============================================================

pub default_axis_config = {
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
    title_color: "#333"
}

// ============================================================
// Merge config helper: override defaults with provided config
// ============================================================

fn merge_config(config) {
    if config != null { {*config, *default_axis_config} }
    else { default_axis_config }
}

// ============================================================
// X-axis (bottom)
// ============================================================

pub fn x_axis(sc, pw, ph, config, title_text) {
    let cfg = merge_config(config);
    let tick_values = scale.scale_ticks(sc, cfg.tick_count);
    let is_band = sc.kind == "band";
    let band_offset = if (is_band) float(sc.bandwidth) / 2.0 else 0.0;

    // domain line
    let domain_line = svg.line(0, ph, pw, ph, cfg.domain_color, 1);

    // tick marks and labels
    let tick_elements = (for (tv in tick_values)
        (let x_pos = float(scale.scale_apply(sc, tv)) + band_offset,
        if (x_pos >= -1.0 and x_pos <= float(pw) + 1.0)
            <g class: "tick", transform: svg.translate(x_pos, ph);
                svg.line(0, 0, 0, cfg.tick_size, cfg.tick_color, 1);
                <text x: 0,
                      y: cfg.tick_size + cfg.label_offset + cfg.label_font_size,
                      "text-anchor": "middle",
                      "font-size": cfg.label_font_size,
                      fill: cfg.label_color;
                    string(tv)
                >
            >
        else null)
    ) that (~ != null);

    // title
    let title_el = if (title_text)
        <text x: float(pw) / 2.0,
              y: float(ph) + float(cfg.title_padding) + float(cfg.title_font_size),
              "text-anchor": "middle",
              "font-size": cfg.title_font_size,
              fill: cfg.title_color;
            title_text
        >
    else null;

    let children0 = [domain_line, *tick_elements];
    let children = if (title_el) [*children0, title_el] else children0;
    svg.group_class("axis x-axis", children)
}

// ============================================================
// X-axis with grid lines
// ============================================================

pub fn x_axis_grid(sc, pw, ph, config) {
    let cfg = merge_config(config);
    let tick_values = scale.scale_ticks(sc, cfg.tick_count);
    let is_band = sc.kind == "band";
    let band_offset = if (is_band) float(sc.bandwidth) / 2.0 else 0.0;

    let grid_lines = (for (tv in tick_values)
        (let x_pos = float(scale.scale_apply(sc, tv)) + band_offset,
        if (x_pos > 0.0 and x_pos < float(pw))
            <line x1: x_pos, y1: 0, x2: x_pos, y2: ph,
                  stroke: cfg.grid_color, "stroke-width": 1,
                  "stroke-dasharray": "4,4">
        else null)
    ) that (~ != null);

    svg.group_class("grid x-grid", grid_lines)
}

// ============================================================
// Y-axis (left)
// ============================================================

pub fn y_axis(sc, pw, ph, config, title_text) {
    let cfg = merge_config(config);
    let tick_values = scale.scale_ticks(sc, cfg.tick_count);
    let is_band = sc.kind == "band";
    let band_offset = if (is_band) float(sc.bandwidth) / 2.0 else 0.0;

    // domain line
    let domain_line = svg.line(0, 0, 0, ph, cfg.domain_color, 1);

    // tick marks and labels
    let tick_elements = (for (tv in tick_values)
        (let y_pos = float(scale.scale_apply(sc, tv)) + band_offset,
        if (y_pos >= -1.0 and y_pos <= float(ph) + 1.0)
            <g class: "tick", transform: svg.translate(0, y_pos);
                svg.line(0, 0, 0 - cfg.tick_size, 0, cfg.tick_color, 1);
                <text x: 0 - cfg.tick_size - cfg.label_offset,
                      y: cfg.label_font_size / 3.0,
                      "text-anchor": "end",
                      "font-size": cfg.label_font_size,
                      fill: cfg.label_color;
                    string(tv)
                >
            >
        else null)
    ) that (~ != null);

    // title (rotated 90 degrees)
    let title_el = if (title_text)
        <text x: 0.0 - float(cfg.title_padding) - float(cfg.label_font_size),
              y: float(ph) / 2.0,
              "text-anchor": "middle",
              "font-size": cfg.title_font_size,
              fill: cfg.title_color,
              transform: svg.rotate(-90, 0.0 - float(cfg.title_padding) - float(cfg.label_font_size), float(ph) / 2.0);
            title_text
        >
    else null;

    let children0 = [domain_line, *tick_elements];
    let children = if (title_el) [*children0, title_el] else children0;
    svg.group_class("axis y-axis", children)
}

// ============================================================
// Y-axis with grid lines
// ============================================================

pub fn y_axis_grid(sc, pw, ph, config) {
    let cfg = merge_config(config);
    let tick_values = scale.scale_ticks(sc, cfg.tick_count);
    let is_band = sc.kind == "band";
    let band_offset = if (is_band) float(sc.bandwidth) / 2.0 else 0.0;

    let grid_lines = (for (tv in tick_values)
        (let y_pos = float(scale.scale_apply(sc, tv)) + band_offset,
        if (y_pos > 0.0 and y_pos < float(ph))
            <line x1: 0, y1: y_pos, x2: pw, y2: y_pos,
                  stroke: cfg.grid_color, "stroke-width": 1,
                  "stroke-dasharray": "4,4">
        else null)
    ) that (~ != null);

    svg.group_class("grid y-grid", grid_lines)
}

// ============================================================
// Compute space needed for axes
// ============================================================

pub fn estimate_y_axis_width(sc, config) {
    let cfg = merge_config(config);
    let ticks = scale.scale_ticks(sc, cfg.tick_count);
    let label_lens = for (tv in ticks) len(string(tv));
    let max_label_len = if (len(label_lens) > 0) max(label_lens) else 3;
    float(max_label_len) * 7.0 + float(cfg.tick_size) + 10.0
}

pub fn estimate_x_axis_height(config, has_title: bool) {
    let cfg = merge_config(config);
    let base = float(cfg.tick_size + cfg.label_font_size + 8);
    if (has_title) base + float(cfg.title_font_size) + 8.0
    else base
}
