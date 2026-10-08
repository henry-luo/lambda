// chart/legend.ls — Legend generation for the chart library
// Renders color, size, and shape legends as SVG elements.

import svg: .svg
import util: .util
import scale: .scale
import parse: .parse
import cfg: .config

// ============================================================
// Default legend configuration
// ============================================================

pub let default_legend_config = {
    symbol_size: 10,
    symbol_padding: 5,
    label_font_size: 11,
    title_font_size: 12,
    row_height: 20,
    title_padding: 6,
    orient: "right",
    label_color: "#333",
    title_color: "#333"
}

// ============================================================
// Merge config helper
// ============================================================

fn merge_config(config) {
    if config != null { {*:default_legend_config, *:config} }
    else { default_legend_config }
}

// ============================================================
// Color legend
// ============================================================

pub fn color_legend(categories, color_scale, title_text, config) {
    if (custom_symbols(config))
        symbol_legend(categories, color_scale, "color", title_text, config)
    else legacy_color_legend(categories, color_scale, title_text, config)
}

fn custom_symbols(config) => config.direction != null or config.columns != null or config.symbol_type != null or
    config.format != null or config.symbol_fill_color != null or config.symbol_stroke_color != null or
    config.symbol_stroke_width != null or config.symbol_opacity != null

fn legend_title(title_text, config) => if (title_text != null)
    <text x: 0, y: float(config.title_font_size), 'font-size': config.title_font_size,
        'font-weight': "bold", fill: config.title_color, title_text> else null

fn legacy_color_legend(categories, color_scale, title_text, config) {
    let cfg = merge_config(config);
    let sym_size = cfg.symbol_size;
    let row_h = cfg.row_height;

    let y_start = if (title_text) float(cfg.title_font_size) + cfg.title_padding else 0.0;

    // title
    let title_el = legend_title(title_text, cfg);

    // legend entries
    let entries = [for (i in 0 to (len(categories) - 1))
        (let cat = categories[i],
        let y_pos = y_start + float(i) * row_h,
        let c = scale.scale_apply(color_scale, cat),
        <g class: "legend-entry", transform: svg.translate(0, y_pos),
            <rect x: 0, y: 0,
                  width: sym_size, height: sym_size,
                  fill: c, rx: 2>
            <text x: float(sym_size) + cfg.symbol_padding,
                  y: float(sym_size) - 1.0,
                  'font-size': cfg.label_font_size,
                  fill: cfg.label_color,
                string(cat)
            >
        >)
    ];

    let children = if (title_el) [title_el, *entries] else entries;
    svg.group_class("legend", children)
}

// ============================================================
// Continuous (gradient) legend
// ============================================================

pub fn gradient_legend(sc, title_text, config) {
    if (config.direction == "horizontal") horizontal_gradient(sc, title_text, config)
    else vertical_gradient(sc, title_text, config)
}

fn vertical_gradient(sc, title_text, config) {
    let cfg = merge_config(config);
    let bar_w = if (cfg.gradient_thickness != null) cfg.gradient_thickness else 15;
    let bar_h = if (cfg.gradient_length != null) cfg.gradient_length else 120;
    let y_start = if (title_text) float(cfg.title_font_size) + cfg.title_padding else 0.0;

    // title
    let title_el = legend_title(title_text, cfg);

    // gradient bar rendered as a series of small rects
    let n_steps = 20;
    let step_h = float(bar_h) / float(n_steps);
    let gradient_rects = [for (i in 0 to (n_steps - 1))
        (let t = 1.0 - float(i) / float(n_steps - 1),
        let c = scale.scale_apply(sc, util.lerp(sc.domain[0], sc.domain[len(sc.domain) - 1], t)),
        <rect x: 0, y: y_start + float(i) * step_h,
              width: bar_w, height: step_h + 1.0,
              fill: c, stroke: "none">)
    ];

    // labels at top and bottom
    let domain = sc.domain;
    let labels = [
        <text x: float(bar_w) + 4.0, y: y_start + 10.0,
              'font-size': cfg.label_font_size, fill: cfg.label_color,
            if (cfg.format != null) util.format_value(domain[len(domain) - 1], cfg.format) else util.fmt_num(domain[len(domain) - 1])
        >,
        <text x: float(bar_w) + 4.0, y: y_start + float(bar_h),
              'font-size': cfg.label_font_size, fill: cfg.label_color,
            if (cfg.format != null) util.format_value(domain[0], cfg.format) else util.fmt_num(domain[0])
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
        max(categories |> len(string(~)))
    else 5;
    float(cfg.symbol_size) + cfg.symbol_padding + float(max_len) * 7.0 * float(cfg.label_font_size) / 11.0 + 10.0
}

pub fn legend_height(categories, config, has_title: bool) {
    let cfg = merge_config(config);
    let title_h = if (has_title) float(cfg.title_font_size) + cfg.title_padding else 0.0;
    title_h + float(len(categories)) * cfg.row_height
}

fn horizontal_gradient(sc, title_text, config) {
    let options = merge_config(config);
    let width = if (options.gradient_length != null) options.gradient_length else 120.0;
    let height = if (options.gradient_thickness != null) options.gradient_thickness else 15.0;
    let top = if (title_text != null) options.title_font_size + options.title_padding else 0.0;
    <g class: "legend gradient-legend",
        legend_title(title_text, options)
        for (i in 0 to 19) <rect x: float(i) * width / 20.0, y: top, width: width / 20.0 + 1.0,
            height: height, stroke: "none", fill: scale.scale_apply(sc,
                util.lerp(sc.domain[0], sc.domain[len(sc.domain) - 1], float(i) / 19.0))>
        for (index in [0, len(sc.domain) - 1]) <text x: if (index == 0) 0 else width,
            y: top + height + options.label_font_size + 3.0,
            'text-anchor': if (index == 0) "start" else "end", 'font-size': options.label_font_size,
            fill: options.label_color, util.format_value(sc.domain[index], options.format)>
    >
}

fn symbol_geometry(values, mapping, kind, title_text, config) {
    let options = merge_config(config);
    let columns = max([1, min([len(values), if (options.columns != null) int(options.columns)
        else if (options.direction == "horizontal") len(values) else 1])]);
    let symbol_extent = if (kind == "size") max([options.symbol_size,
        for (value in values) 2.0 * math.sqrt(max([0.0, scale.scale_apply(mapping, value)]) / util.PI)])
        else options.symbol_size;
    let label_width = max([0, for (value in values) len(util.format_value(value, options.format))]) *
        options.label_font_size * 7.0 / 11.0;
    let column_width = symbol_extent + options.symbol_padding + label_width +
        (if (options.column_padding != null) options.column_padding else 10.0);
    let row_height = max([options.row_height, symbol_extent + 4.0]);
    let top = if (title_text != null) options.title_font_size + options.title_padding else 0.0;
    {*:options, columns: columns, symbol_extent: symbol_extent, column_width: column_width,
        row_height: row_height, top: top,
        width: max([columns * column_width,
            if (title_text != null) len(title_text) * options.title_font_size * 0.6 else 0.0]),
        height: top + ceil(float(len(values)) / float(columns)) * row_height}
}

pub fn symbol_legend(values, mapping, kind, title_text, config) {
    let geometry = symbol_geometry(values, mapping, kind, title_text, config);
    <g class: if (kind == "color") "legend" else "legend " ++ kind ++ "-legend",
        legend_title(title_text, geometry)
        for (index, value in values,
            let x = float(index % geometry.columns) * geometry.column_width,
            let y = geometry.top + floor(float(index) / float(geometry.columns)) * geometry.row_height,
            let area = if (kind == "size") scale.scale_apply(mapping, value) else float(geometry.symbol_size) ** 2.0,
            let shape = if (kind == "shape") scale.scale_apply(mapping, value)
                else if (geometry.symbol_type != null) geometry.symbol_type else if (kind == "color") "square" else "circle",
            let fill = if (geometry.symbol_fill_color != null) geometry.symbol_fill_color
                else if (kind == "color") scale.scale_apply(mapping, value) else "#4e79a7")
            <g class: "legend-entry", transform: svg.translate(x, y),
                svg.symbol_mark(shape, geometry.symbol_extent / 2.0, geometry.symbol_extent / 2.0, area,
                    cfg.settings({fill: fill, stroke: geometry.symbol_stroke_color,
                        'stroke-width': geometry.symbol_stroke_width, opacity: geometry.symbol_opacity}));
                <text x: geometry.symbol_extent + geometry.symbol_padding,
                    y: geometry.symbol_extent / 2.0 + geometry.label_font_size / 3.0,
                    'font-size': geometry.label_font_size, fill: geometry.label_color,
                    util.format_value(value, geometry.format)>
            >
    >
}

// A plan carries both guide geometry and its exact visual mapping.
pub fn plans(encoding, mappings, theme) {
    [for (kind in ["color", "size", "shape"],
        let channel = encoding[kind],
        let mapping = mappings[kind ++ "_scale"],
        let options = cfg.legend_config(theme, channel)
        where channel.field != null and options.enabled and mapping != null and mapping.kind != "identity") (
        let continuous = mapping.kind == "sequential-color" or mapping.kind == "diverging-color",
        let values = if (options.values != null) options.values else if (kind == "size")
            scale.scale_ticks(mapping, if (options.tick_count != null) options.tick_count else 5) |:
                (~ >= min(mapping.domain) and ~ <= max(mapping.domain)) else mapping.domain,
        let title = if (not options.title_enabled) null else if (options.title != null) options.title
            else if (channel.title != null) channel.title else channel.field,
        let configured = {*:options, format: if (options.format != null) options.format else channel.format},
        let geometry = if (kind != "color" or custom_symbols(configured))
            symbol_geometry(values, mapping, kind, title, configured) else null,
        let vertical = configured.direction != "horizontal",
        let width = if (continuous) (
            if (vertical) legend_width(values, configured)
            else if (configured.gradient_length != null) configured.gradient_length else 120.0)
            else if (geometry != null) geometry.width else legend_width(values, configured),
        let height = if (continuous) (
            if (vertical) (if (configured.gradient_length != null) configured.gradient_length + 30.0 else 150.0)
            else (if (configured.gradient_thickness != null) configured.gradient_thickness else 15.0) +
                (if (title != null) 18.0 else 0.0) + 20.0)
            else if (geometry != null) geometry.height else legend_height(values, configured, true),
        {kind: kind, mapping: mapping, values: values, title: title, config: configured,
            orient: if (configured.orient != null) configured.orient else "right", width: width, height: height}
    )]
}

pub fn render_plan(plan) {
    if (plan.kind == "color" and (plan.mapping.kind == "sequential-color" or plan.mapping.kind == "diverging-color"))
        gradient_legend(plan.mapping, plan.title, plan.config)
    else if (plan.kind == "color") color_legend(plan.values, plan.mapping, plan.title, plan.config)
    else symbol_legend(plan.values, plan.mapping, plan.kind, plan.title, plan.config)
}

pub fn render_plans(plans, geometry) {
    if (len(plans) == 0) null
    else if (len(plans) == 1) render_plan(plans[0])
    else <g class: "legends", for (plan in geometry.guides)
        <g transform: svg.translate(plan.x, plan.y), render_plan(plan)>>
}
