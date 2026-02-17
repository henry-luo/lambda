// chart/legend.ls — Legend generation for the chart library
// Renders color, size, and shape legends as SVG elements.

import svg: .lambda.package.chart.svg
import util: .lambda.package.chart.util
import scale: .lambda.package.chart.scale

// ============================================================
// Default legend configuration
// ============================================================

pub default_legend_config = {
    symbol_size: 10,
    symbol_padding: 5,
    label_font_size: 11,
    title_font_size: 12,
    row_height: 20,
    title_padding: 6,
    orient: "right"
}

// ============================================================
// Merge config helper
// ============================================================

fn merge_config(config) {
    if config != null { {*config, *default_legend_config} }
    else { default_legend_config }
}

// ============================================================
// Color legend
// ============================================================

pub fn color_legend(categories, color_scale, title_text, config) {
    let cfg = merge_config(config);
    let sym_size = cfg.symbol_size;
    let row_h = cfg.row_height;

    let y_start = if (title_text) float(cfg.title_font_size) + cfg.title_padding else 0.0;

    // title
    let title_el = if (title_text)
        <text x: 0, y: float(cfg.title_font_size),
              "font-size": cfg.title_font_size,
              "font-weight": "bold",
              fill: "#333";
            title_text
        >
    else null;

    // legend entries
    let entries = (for (i in 0 to (len(categories) - 1))
        (let cat = categories[i],
        let y_pos = y_start + float(i) * row_h,
        let c = scale.scale_apply(color_scale, cat),
        <g class: "legend-entry", transform: svg.translate(0, y_pos);
            <rect x: 0, y: 0,
                  width: sym_size, height: sym_size,
                  fill: c, rx: 2>
            <text x: float(sym_size) + cfg.symbol_padding,
                  y: float(sym_size) - 1.0,
                  "font-size": cfg.label_font_size,
                  fill: "#333";
                string(cat)
            >
        >)
    );

    let children = if (title_el) [title_el, *entries] else entries;
    svg.group_class("legend", children)
}

// ============================================================
// Continuous (gradient) legend
// ============================================================

pub fn gradient_legend(sc, title_text, config) {
    let cfg = merge_config(config);
    let bar_w = 15;
    let bar_h = 120;
    let y_start = if (title_text) float(cfg.title_font_size) + cfg.title_padding else 0.0;

    // title
    let title_el = if (title_text)
        <text x: 0, y: float(cfg.title_font_size),
              "font-size": cfg.title_font_size,
              "font-weight": "bold",
              fill: "#333";
            title_text
        >
    else null;

    // gradient bar rendered as a series of small rects
    let n_steps = 20;
    let step_h = float(bar_h) / float(n_steps);
    let gradient_rects = (for (i in 0 to (n_steps - 1))
        (let t = 1.0 - float(i) / float(n_steps),
        let c = if (sc.scheme) (let idx = int(t * float(len(sc.scheme) - 1)), sc.scheme[idx]) else "#ccc",
        <rect x: 0, y: y_start + float(i) * step_h,
              width: bar_w, height: step_h + 1.0,
              fill: c, stroke: "none">)
    );

    // labels at top and bottom
    let domain = sc.domain;
    let labels = [
        <text x: float(bar_w) + 4.0, y: y_start + 10.0,
              "font-size": cfg.label_font_size, fill: "#333";
            util.fmt_num(domain[1])
        >,
        <text x: float(bar_w) + 4.0, y: y_start + float(bar_h),
              "font-size": cfg.label_font_size, fill: "#333";
            util.fmt_num(domain[0])
        >
    ];

    let children = if (title_el)
        [title_el, *gradient_rects, *labels]
    else
        [*gradient_rects, *labels];
    svg.group_class("legend gradient-legend", children)
}

// ============================================================
// Compute legend dimensions for layout
// ============================================================

pub fn legend_width(categories, config) {
    let cfg = merge_config(config);
    let max_len = if (len(categories) > 0)
        max(categories | len(string(~)))
    else 5;
    float(cfg.symbol_size) + cfg.symbol_padding + float(max_len) * 7.0 + 10.0
}

pub fn legend_height(categories, config, has_title: bool) {
    let cfg = merge_config(config);
    let title_h = if (has_title) float(cfg.title_font_size) + cfg.title_padding else 0.0;
    title_h + float(len(categories)) * cfg.row_height
}
