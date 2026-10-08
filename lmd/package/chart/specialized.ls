// Multi-axis marks share the chart's appearance, measured text, and SVG guide primitives.
import mark: .mark
import svg: .svg
import scale: .scale
import parse: .parse
import cfg: .config
import text: .text
import axis: .axis
import color: .color
import util: .util

fn polar(cx, cy, radius, angle) => [cx + radius * math.cos(angle - util.PI / 2.0), cy + radius * math.sin(angle - util.PI / 2.0)]

pub fn radar(data, ctx, options) {
    let angular = scale.angular_scale(ctx.encoding.theta, data);
    let radial = scale.radius_scale(ctx.encoding.radius, data);
    let categories = angular.domain;
    let guide = cfg.axis_config(ctx._theme, ctx.encoding.radius);
    let font = text.style(guide, "label", 11);
    let labels = text.measure([for (category in categories) string(category)], font);
    let failure = util.first_error([angular, radial, labels]);
    let outer = min([ctx.plot_w / 2.0 - (if (options.labels == false) 0.0 else max([0.0, for (label in labels) text.span(label)])) - 6.0,
        ctx.plot_h / 2.0 - (if (options.labels == false) 0.0 else max([0.0, for (label in labels) label.height])) - 6.0]);
    let groups = mark.series(data, ctx, color.default_color);
    let invalid = [for (group in groups,
        let keys = [for (row in group.items) parse.channel_value(ctx.encoding.theta, row)]
        where len(keys) != len(categories) or len(util.unique_vals(keys)) != len(categories) or
            len([for (row in group.items where not util.finite_number(parse.channel_value(ctx.encoding.radius, row))) true]) > 0) group];
    let count = if (options.tick_count != null) options.tick_count else 5;
    if (failure is error) failure
    else if (ctx.encoding.theta.field == null or ctx.encoding.radius.field == null or
        (ctx.encoding.theta.dtype != "nominal" and ctx.encoding.theta.dtype != "ordinal") or ctx.encoding.radius.dtype != "quantitative")
        error("chart: radar requires categorical theta and quantitative radius fields")
    else if (len(data) == 0) svg.group_class("marks radar", [])
    else if (len(categories) < 3 or len(invalid) > 0 or outer <= 0 or not util.finite_number(count) or count < 1 or floor(count) != count)
        error("chart: radar requires at least three categories, complete series, and space for its labels")
    else {
        let cx = ctx.plot_w / 2.0;
        let cy = ctx.plot_h / 2.0;
        let mapping = scale.radius_scale(ctx.encoding.radius, data, 0.0, outer);
        let ticks = scale.scale_ticks(mapping, int(count)) |: (~ >= min(mapping.domain) and ~ <= max(mapping.domain));
        let tick_labels = [for (tick in ticks) util.format_value(tick, if (guide.format != null) guide.format else ctx.encoding.radius.format)];
        let tick_metrics = text.measure(tick_labels, font);
        let tick_angle = (scale.scale_apply(angular, categories[0]) + scale.scale_apply(angular, categories[1])) / 2.0;
        if (tick_metrics is error) tick_metrics else svg.group_class("marks radar", [
            if (options.grid != false and guide.grid != false) <g class: "radar-grid",
                for (tick in ticks, let radius = scale.scale_apply(mapping, tick) where radius > 0)
                    <circle cx: cx, cy: cy, r: radius, fill: "none", stroke: if (guide.grid_color != null) guide.grid_color else "#ddd">
                for (category in categories, let point = polar(cx, cy, outer, scale.scale_apply(angular, category)))
                    <line x1: cx, y1: cy, x2: point[0], y2: point[1], stroke: if (guide.domain_color != null) guide.domain_color else "#aaa">>,
            for (group in groups) (
                let points = [for (category in categories,
                    let row = (group.items |: parse.channel_value(ctx.encoding.theta, ~) == category)[0])
                    polar(cx, cy, scale.scale_apply(mapping, parse.channel_value(ctx.encoding.radius, row)), scale.scale_apply(angular, category))],
                <path class: "radar-series", d: svg.line_path(points) ++ " Z",
                    *:mark.style(ctx, group.items[0], options,
                        {fill: if (options.filled == true) group.color else "none", stroke: group.color, 'stroke-width': 2, opacity: 1.0}, options.filled != true),
                    'fill-opacity': if (options.fill_opacity != null) options.fill_opacity else 0.2, mark.tooltip(ctx, group.items[0])>),
            if (options.labels != false) <g class: "radar-labels",
                for (index, category in categories,
                    let point = polar(cx, cy, outer + 6.0, scale.scale_apply(angular, category)),
                    let cosine = math.cos(scale.scale_apply(angular, category) - util.PI / 2.0))
                    <text x: point[0], y: point[1] - (labels[index].top + labels[index].bottom) / 2.0,
                        'text-anchor': if (abs(cosine) < 0.0000000001) "middle" else if (cosine > 0) "start" else "end",
                        *:text.attributes(font), fill: if (guide.label_color != null) guide.label_color else "#333", string(category)>
                for (index, tick in ticks, let point = polar(cx, cy, scale.scale_apply(mapping, tick), tick_angle))
                    <text x: point[0] + 3.0, y: point[1] - (tick_metrics[index].top + tick_metrics[index].bottom) / 2.0,
                        *:text.attributes(font), fill: if (guide.label_color != null) guide.label_color else "#333", tick_labels[index]>>])
    }
}

pub fn parallel(data, ctx, options) {
    let fields = if (options.fields is array) [for (entry in options.fields) if (entry is string) {field: entry} else entry] else [];
    let invalid = [for (entry in fields where not (entry is map) or not (entry.field is string)) entry];
    let font = text.style({font_family: options.font_family, label_font_size: options.label_font_size}, "label", 11);
    let labels = text.measure([for (field in fields) if (field.title != null) field.title else field.field], font);
    let label_height = if (options.labels == false) 0.0 else max([0.0, for (label in labels) label.height + 8.0]);
    let height = ctx.plot_h - label_height;
    let complete = [for (row in data where len([for (field in fields where not util.finite_number(row[field.field])) true]) == 0) row];
    let models = [for (field in fields) scale.configured_scale([for (row in complete) row[field.field]],
        height, 0.0, "linear", field.scale, if (field.zero != null) field.zero else false)];
    let guides = [for (index, field in fields) cfg.axis_config(ctx._theme,
        {axis: field.axis, axis_enabled: options.axes != false and parse.option_enabled(field, "axis"), format: field.format, dtype: "quantitative"})];
    let margin = max([0.0, for (index, model in models where guides[index].enabled) axis.estimate_y_axis_width(model, guides[index], null),
        for (label in (if (options.labels == false) [] else labels)) text.span(label) / 2.0]);
    let width = ctx.plot_w - 2.0 * margin;
    let failure = util.first_error([labels, *models]);
    if (len(fields) < 2 or len(invalid) > 0) error("chart: parallel requires at least two field definitions")
    else if (failure is error) failure
    else if (height <= 0 or width <= 0) error("chart: parallel axes and labels do not fit the plot")
    else {
        let positions = [for (index in 0 to (len(fields) - 1)) margin + width * float(index) / float(len(fields) - 1)];
        svg.group_class("marks parallel", [
            for (index, model in models where guides[index].enabled)
                svg.group(svg.translate(positions[index], 0.0), [axis.y_axis(model, 0.0, height, guides[index], null)]),
            for (row in complete) <path class: "parallel-series", d: svg.line_path([for (index, field in fields)
                [positions[index], scale.scale_apply(models[index], row[field.field])]], options.interpolate),
                *:mark.style(ctx, row, options, {fill: "none", stroke: color.default_color, 'stroke-width': 1.5, opacity: 0.7}, true), mark.tooltip(ctx, row)>,
            if (options.labels != false) for (index, field in fields)
                <text x: positions[index], y: ctx.plot_h - labels[index].bottom, 'text-anchor': "middle", *:text.attributes(font),
                    fill: if (ctx._theme.axis_label_color != null) ctx._theme.axis_label_color else "#333",
                    if (field.title != null) field.title else field.field>])
    }
}
