// chart/vega.ls — Adapt the supported declarative Vega-Lite subset to native chart specifications.

import parse: .parse
import cfg: .config

let aliases = {
    strokeWidth: "stroke_width", cornerRadius: "corner_radius", innerRadius: "inner_radius",
    outerRadius: "outer_radius", padAngle: "pad_angle", fontSize: "font_size", fontWeight: "font_weight",
    fontFamily: "font_family", strokeDash: "stroke_dash", xOffset: "x_offset", timeUnit: "time_unit",
    paddingInner: "padding_inner", paddingOuter: "padding_outer", domainMin: "domain_min",
    domainMax: "domain_max", domainMid: "domain_mid", tickCount: "tick_count", tickSize: "tick_size",
    tickMinStep: "tick_min_step", labelAngle: "label_angle", labelOverlap: "label_overlap",
    labelLimit: "label_limit", labelFontSize: "label_font_size", labelColor: "label_color",
    labelFont: "label_font_family", labelFontWeight: "label_font_weight", labelFontStyle: "label_font_style",
    titleFont: "title_font_family", titleFontWeight: "title_font_weight", titleFontStyle: "title_font_style",
    labelPadding: "label_offset", labelSeparation: "label_separation",
    titleFontSize: "title_font_size", titleColor: "title_color", titlePadding: "title_padding",
    gridColor: "grid_color", gridWidth: "grid_width", gridDash: "grid_dash", domainColor: "domain_color",
    symbolSize: "symbol_size", symbolPadding: "symbol_padding", rowHeight: "row_height",
    symbolType: "symbol_type", symbolFillColor: "symbol_fill_color", symbolStrokeColor: "symbol_stroke_color",
    symbolStrokeWidth: "symbol_stroke_width", symbolOpacity: "symbol_opacity", columnPadding: "column_padding",
    gradientLength: "gradient_length", gradientThickness: "gradient_thickness",
    preserveAspectRatio: "preserve_aspect_ratio",
    ignorePeers: "ignore_peers"
}

fn normalize(options) {
    if (options is map) map([for (key, value in options)
        for (item in [if (aliases[string(key)] != null) aliases[string(key)] else string(key), normalize(value)]) item])
    else if (options is array) [for (item in options) normalize(item)]
    else options
}

pub fn convert(vl) {
    let raw_padding = if (vl.padding != null) vl.padding else 20;
    let padding = if (raw_padding is int or raw_padding is float)
        {top: raw_padding, right: raw_padding, bottom: raw_padding, left: raw_padding} else raw_padding;
    let common = {
        width: if (vl.width != null) vl.width else 400,
        height: if (vl.height != null) vl.height else 300,
        _width_specified: vl.width != null, _height_specified: vl.height != null,
        aspect_ratio: if (vl.aspect_ratio != null) vl.aspect_ratio else vl.aspectRatio,
        padding: padding,
        title: if (vl.title is string) vl.title else vl.title.text,
        data: vl.data.values, data_source: vl.data, datasets: vl.datasets,
        mark: convert_mark(vl.mark), encoding: convert_encoding(vl.encoding),
        transform: convert_transforms(vl.transform), config: convert_config(vl.config),
        resolve: normalize(vl.resolve),
        projection: normalize(vl.projection),
        layer: if (vl.layer != null) [for (layer in vl.layer) convert(layer)] else null,
        facet: null
    };
    if (vl.hconcat != null or vl.vconcat != null) {
        let horizontal = vl.hconcat != null;
        {*:common, concat: if (horizontal) "horizontal" else "vertical",
            spacing: if (vl.spacing != null) vl.spacing else 20,
            children: [for (child in (if (horizontal) vl.hconcat else vl.vconcat))
                convert(child)]}
    } else if (vl.facet != null and vl.spec != null) {
        let child = convert(inherit(vl, vl.spec));
        {*:child, title: common.title, facet: {*:normalize(vl.facet),
            columns: if (vl.columns != null) vl.columns else vl.facet.columns,
            spacing: vl.spacing}}
    } else if (vl.repeat != null and vl.spec != null) {
        let repeated = if (vl.repeat is array) {column: vl.repeat} else vl.repeat;
        {*:common, repeat_row: repeated.row, repeat_column: repeated.column,
            template: convert(vl.spec)}
    } else common
}

fn inherit(parent, child) {
    {*:child, data: if (child.data != null) child.data else parent.data,
        datasets: if (child.datasets != null) child.datasets else parent.datasets,
        width: if (child.width != null) child.width else parent.width,
        height: if (child.height != null) child.height else parent.height,
        padding: if (child.padding != null) child.padding else parent.padding,
        transform: [for (step in parent.transform) step, for (step in child.transform) step],
        encoding: {*:parse.attributes(parent.encoding), *:parse.attributes(child.encoding)},
        resolve: cfg.inherit(parent.resolve, child.resolve),
        config: cfg.inherit(parent.config, child.config)}
}

fn convert_mark(mark_json) {
    if (mark_json is string) parse.parse_mark({type: mark_json})
    else if (mark_json != null) parse.parse_mark({*:normalize(mark_json),
        font_family: if (mark_json.font != null) mark_json.font else normalize(mark_json).font_family})
    else null
}

fn convert_encoding(enc) {
    if (enc == null) {}
    else map([for (key, channel in enc)
        for (item in [if (string(key) == "xOffset") "x_offset" else string(key),
            if (channel is array) [for (entry in channel) convert_channel(entry)] else convert_channel(channel)]) item])
}

fn convert_channel(channel) {
    let options = normalize(channel);
    let kind = channel.type;
    parse.parse_channel({*:options,
        dtype: if (kind == "Q") "quantitative" else if (kind == "N") "nominal"
            else if (kind == "O") "ordinal" else if (kind == "T") "temporal" else kind,
        stack: if (channel.stack == false) "none" else channel.stack,
        axis_enabled: parse.option_enabled(channel, "axis"),
        legend_enabled: parse.option_enabled(channel, "legend"),
        scale_enabled: parse.option_enabled(channel, "scale")})
}

fn convert_config(config) {
    if (config == null) null
    else {
        let normalized = normalize(config);
        let flattened = map([for (section, settings in normalized where settings is map)
            for (key, value in settings) for (item in [string(section) ++ "_" ++ string(key), value]) item]);
        {*:normalized, *:flattened,
            axis_grid: if (config.axis.grid != null) config.axis.grid else normalized.axis_grid}
    }
}

// Expressions remain static and datum-scoped; signals and event handlers are outside this adapter.
fn convert_transforms(transforms) {
    if (transforms == null) null
    else <transform for (step in transforms) convert_transform(step)>
}

fn convert_transform(step) {
    if (parse.has_attribute(step, "filter")) <filter test: step.filter>
    else if (step.aggregate != null) <aggregate
        for (field in step.groupby) <group field: field>
        for (agg in step.aggregate) <agg op: agg.op, field: agg.field, as: agg.as>>
    else if (step.bin != null) <bin field: step.field,
        as: if (step.as is array) step.as[0] else step.as,
        as_end: if (step.as is array) step.as[1] else null,
        maxbins: step.bin.maxbins, step: step.bin.step>
    else if (step.calculate != null) <calculate as: step.as, expression: step.calculate>
    else if (step.fold != null) <fold fields: step.fold, as: step.as>
    else if (step.flatten != null) <flatten fields: step.flatten, as: step.as>
    else if (step.window != null) {type: "window", *:normalize(step)}
    else if (step.joinaggregate != null) {type: "joinaggregate", *:normalize(step)}
    else if (step.pivot != null) {type: "pivot", *:step}
    else if (step.impute != null) {type: "impute", *:step}
    else if (step.stack != null) {type: "stack", *:step}
    else if (step.quantile != null) {type: "quantile", *:step}
    // Foreign rows and fallback values are data; option-name aliases must not rewrite their keys.
    else if (step.lookup != null) {*:step, type: "lookup"}
    else if (step.density != null) {type: "density", *:normalize(step), field: step.density}
    else if (step.regression != null) {type: "regression", *:normalize(step),
        x: step.on, y: step.regression, r_squared_name: "rSquared"}
    else if (step.loess != null) {type: "loess", *:normalize(step), x: step.on, y: step.loess}
    else if (step.timeUnit != null) {type: "timeunit", field: step.field, unit: step.timeUnit, as: step.as}
    else if (step.type != null) step
    else error("chart: unsupported Vega transform")
}
