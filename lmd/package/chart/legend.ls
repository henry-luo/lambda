// Legends retain measured geometry with the visual mapping used to draw their entries.
import svg: .svg
import util: .util
import scale: .scale
import parse: .parse
import cfg: .config
import text: .text
import paint: .paint

pub let default_legend_config = {
    symbol_size: 10, symbol_padding: 5, label_font_size: 11, title_font_size: 12,
    row_height: 20, title_padding: 6, orient: "right", label_color: "#333", title_color: "#333"
}

fn merge_config(config) => {*:default_legend_config, *:cfg.settings(config)}

fn prepare(values, title, config) {
    let options = merge_config(config);
    let label_font = text.style(options, "label", 11);
    let title_font = text.style(options, "title", 12, 700);
    let labels = text.fit([for (value in values) util.format_value(value, options.format)], label_font, options.label_limit);
    let titles = if (title != null) text.measure([string(title)], title_font) else [];
    let title_metric = if (titles is array and len(titles) > 0) titles[0] else text.empty_metric;
    {*:options, labels: if (labels is error) [] else labels, label_font: label_font, title_font: title_font,
        title_metric: title_metric, title_width: text.span(title_metric),
        top: if (title != null) title_metric.height + options.title_padding else 0.0,
        _error: util.first_error([labels, titles])}
}

fn legend_title(title, geometry) => if (title != null)
    <text x: 0.0 - geometry.title_metric.left, y: 0.0 - geometry.title_metric.top,
        *:text.attributes(geometry.title_font), fill: geometry.title_color, title> else null

fn symbol_geometry(values, mapping, kind, title, config) {
    if (config._geometry != null) config._geometry else {
        let options = prepare(values, title, config);
        let columns = max([1, min([len(values), if (options.columns != null) int(options.columns)
            else if (options.direction == "horizontal") len(values) else 1])]);
        let width_unit = kind == "size" and options._size_unit == "width";
        let symbol_height = if (kind == "size") max([options.symbol_size,
            for (value in values) if (width_unit) max([0.0, scale.scale_apply(mapping, value)])
                else 2.0 * math.sqrt(max([0.0, scale.scale_apply(mapping, value)]) / util.PI)])
            else options.symbol_size;
        let symbol_extent = if (width_unit) options.symbol_size * 2.0 else symbol_height;
        let label_width = max([0.0, for (label in options.labels) text.span(label.metric)]);
        let column_width = symbol_extent + options.symbol_padding + label_width +
            (if (options.column_padding != null) options.column_padding else 10.0);
        let row_height = max([options.row_height, symbol_height + 4.0,
            for (label in options.labels) label.metric.height + 4.0]);
        {*:options, columns: columns, symbol_extent: symbol_extent, column_width: column_width,
            row_height: row_height, width: max([columns * column_width, options.title_width]),
            height: options.top + ceil(float(len(values)) / float(columns)) * row_height}
    }
}

pub fn symbol_legend(values, mapping, kind, title, config) {
    let geo = symbol_geometry(values, mapping, kind, title, config);
    if (geo._error is error) geo._error else
    <g class: if (kind == "color") "legend" else "legend " ++ kind ++ "-legend",
        legend_title(title, geo)
        for (index, value in values,
            let x = float(index % geo.columns) * geo.column_width,
            let y = geo.top + floor(float(index) / float(geo.columns)) * geo.row_height,
            let area = if (kind == "size") scale.scale_apply(mapping, value) else float(geo.symbol_size) ** 2.0,
            let shape = if (kind == "shape") scale.scale_apply(mapping, value)
                else if (geo.symbol_type != null) geo.symbol_type else if (kind == "color") "square" else "circle",
            let fill = if (geo.symbol_fill_color != null) geo.symbol_fill_color
                else if (kind == "color") scale.scale_apply(mapping, value) else "#4e79a7",
            let label = geo.labels[index])
            <g class: "legend-entry", transform: svg.translate(x, y),
                *:(if (geo._interaction == null) {} else {
                    'data-chart-legend': format({*:geo._interaction, row: map([geo._interaction.field, value])}, 'json')}),
                if (kind == "size" and geo._size_unit == "width")
                    <line x1: 0, y1: geo.row_height / 2.0, x2: geo.symbol_extent, y2: geo.row_height / 2.0,
                        stroke: paint.value(if (geo.symbol_stroke_color != null) geo.symbol_stroke_color else fill, geo._paints),
                        'stroke-width': area, *:cfg.settings({opacity: geo.symbol_opacity})>
                else svg.symbol_mark(shape, geo.symbol_extent / 2.0, geo.row_height / 2.0, area,
                    cfg.settings({fill: paint.value(fill, geo._paints), stroke: paint.value(geo.symbol_stroke_color, geo._paints),
                        'stroke-width': geo.symbol_stroke_width, opacity: geo.symbol_opacity}));
                <text x: geo.symbol_extent + geo.symbol_padding - label.metric.left,
                    y: (geo.row_height - label.metric.top - label.metric.bottom) / 2.0,
                    *:text.attributes(geo.label_font), fill: geo.label_color, label.text>
            >
    >
}

pub fn color_legend(values, mapping, title, config) => symbol_legend(values, mapping, "color", title, config)

fn gradient_geometry(mapping, title, config) {
    if (config._geometry != null) config._geometry else {
        let options = prepare([mapping.domain[0], mapping.domain[len(mapping.domain) - 1]], title, config);
        let horizontal = options.direction == "horizontal";
        let label_width = max([0.0, for (label in options.labels) text.span(label.metric)]);
        let label_height = max([0.0, for (label in options.labels) label.metric.height]);
        let length = max([if (options.gradient_length != null) options.gradient_length else 120.0,
            if (horizontal) sum([for (label in options.labels) text.span(label.metric)]) + 4.0 else 2.0 * label_height + 4.0]);
        let thickness = if (options.gradient_thickness != null) options.gradient_thickness else 15.0;
        {*:options, horizontal: horizontal, length: length, thickness: thickness,
            width: max([options.title_width, if (horizontal) length else thickness + 4.0 + label_width]),
            height: options.top + (if (horizontal) thickness + 3.0 + label_height else length)}
    }
}

pub fn gradient_legend(mapping, title, config) {
    let geo = gradient_geometry(mapping, title, config);
    if (geo._error is error) geo._error else
    <g class: "legend gradient-legend",
        legend_title(title, geo)
        for (index in 0 to 19,
            let t = float(index) / 19.0,
            let position = float(index) * geo.length / 20.0)
            <rect x: if (geo.horizontal) position else 0.0, y: geo.top + (if (geo.horizontal) 0.0 else position),
                width: if (geo.horizontal) geo.length / 20.0 + 1.0 else geo.thickness,
                height: if (geo.horizontal) geo.thickness else geo.length / 20.0 + 1.0,
                stroke: "none", fill: scale.scale_apply(mapping,
                    util.lerp(mapping.domain[0], mapping.domain[len(mapping.domain) - 1], if (geo.horizontal) t else 1.0 - t))>
        for (index, label in geo.labels)
            <text x: if (geo.horizontal) (if (index == 0) 0.0 - label.metric.left
                    else geo.length + label.metric.width - label.metric.right) else geo.thickness + 4.0 - label.metric.left,
                y: geo.top + (if (geo.horizontal) geo.thickness + 3.0 - label.metric.top
                    else if (index == 0) geo.length - label.metric.bottom else 0.0 - label.metric.top),
                'text-anchor': if (geo.horizontal and index != 0) "end" else "start",
                *:text.attributes(geo.label_font), fill: geo.label_color, label.text>
    >
}

pub fn legend_width(values, config) {
    let options = prepare(values, null, config);
    float(options.symbol_size) + options.symbol_padding + max([0.0, for (label in options.labels) text.span(label.metric)]) + 10.0
}

pub fn legend_height(values, config, has_title: bool) {
    let options = prepare(values, if (has_title) config.title else null, config);
    options.top + float(len(values)) * max([options.row_height, for (label in options.labels) label.metric.height + 4.0])
}

pub fn plans(encoding, mappings, theme) {
    [for (kind in ["color", "size", "shape"],
        let channel = encoding[kind], let mapping = mappings[kind ++ "_scale"],
        let options = cfg.legend_config(theme, channel)
        where channel.field != null and options.enabled and mapping != null and mapping.kind != "identity") (
        let continuous = mapping.kind == "sequential-color" or mapping.kind == "diverging-color",
        let values = if (options.values != null) options.values else if (kind == "size")
            scale.scale_ticks(mapping, if (options.tick_count != null) options.tick_count else 5) |:
                (~ >= min(mapping.domain) and ~ <= max(mapping.domain)) else mapping.domain,
        let title = if (not options.title_enabled) null else if (options.title != null) options.title
            else if (channel.title != null) channel.title else channel.field,
        let configured = {*:options, _size_unit: channel._size_unit,
            _interaction: if (channel._interaction != null) {*:channel._interaction, channel: kind} else null,
            format: if (options.format != null) options.format else channel.format},
        let geometry = if (continuous) gradient_geometry(mapping, title, configured)
            else symbol_geometry(values, mapping, kind, title, configured),
        {kind: kind, mapping: mapping, values: values, title: title, config: {*:configured, _geometry: geometry},
            orient: if (configured.orient != null) configured.orient else "right",
            width: geometry.width, height: geometry.height, _error: geometry._error}
    )]
}

pub fn render_plan(plan) {
    if (plan.kind == "color" and (plan.mapping.kind == "sequential-color" or plan.mapping.kind == "diverging-color"))
        gradient_legend(plan.mapping, plan.title, plan.config)
    else symbol_legend(plan.values, plan.mapping, plan.kind, plan.title, plan.config)
}

pub fn render_plans(plans, geometry) {
    let failure = util.first_error([for (plan in plans) plan._error]);
    if (failure is error) failure
    else if (len(plans) == 0) null
    else if (len(plans) == 1) render_plan(plans[0])
    else <g class: "legends", for (plan in geometry.guides)
        <g transform: svg.translate(plan.x, plan.y), render_plan(plan)>>
}
