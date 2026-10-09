// Ordered stages and bounded value displays retain the original datum on each part.
import util: .util
import parse: .parse
import mark: .mark
import svg: .svg
import color: .color
import text: .text
import axis: .axis

fn value(row, ctx, options) => parse.channel_value(ctx.encoding.y, row,
    row[if (options.value_field != null) options.value_field else "value"])

pub fn funnel(data, ctx, options) {
    let stage_ctx=mark.part_context(ctx,options,"stage",data);
    let label_ctx=mark.part_context(ctx,options,"label",data);
    let connector_ctx=mark.part_context(ctx,options,"connector",data);
    let rows = if (options.sort != null) sort(data, {by: (row) => value(row, ctx, options),
        dir: if (options.sort == "descending") "desc" else "asc"}) else data;
    let values = [for (row in rows) value(row, ctx, options)];
    let largest = max([0.0, *values]);
    let gap = if (options.gap != null) options.gap else 2.0;
    let step = ctx.plot_h / max(1.0, float(len(rows)));
    let invalid=mark.part_error([stage_ctx,label_ctx,connector_ctx]);
    if (invalid is error) invalid
    else if (any([for (v in values) not util.finite_number(v) or v < 0]) or not util.finite_number(gap) or gap < 0 or gap >= step)
        error("chart: funnel requires finite nonnegative values and a gap smaller than the stage height")
    else svg.group_class("marks funnel", [for (i, row in rows,
        let a = if (largest > 0) ctx.plot_w * values[i] / largest else 0.0,
        let b = if (i + 1 < len(rows) and largest > 0) ctx.plot_w * values[i + 1] / largest else a,
        let top = float(i) * step, let bottom = top + step - gap,
        let label = parse.channel_value(ctx.encoding.text, row, row[if (options.label_field != null) options.label_field else "stage"])) (
        if (mark.part_selected(stage_ctx,row)) <path class: "funnel-stage", d: svg.line_path([[(ctx.plot_w - a) / 2.0, top], [(ctx.plot_w + a) / 2.0, top],
            [(ctx.plot_w + b) / 2.0, bottom], [(ctx.plot_w - b) / 2.0, bottom]]) ++ " Z",
            *:mark.style(stage_ctx, row, mark.part_options(options,"stage"), {fill: color.category10[i % 10], opacity: 1.0}), mark.tooltip(stage_ctx, row)>,
        if (options.connectors==true and i+1<len(rows) and mark.part_selected(connector_ctx,row))
            <line class:"funnel-connector",x1:ctx.plot_w/2.0,x2:ctx.plot_w/2.0,y1:bottom,y2:(i+1)*step,
                *:mark.style(connector_ctx,row,mark.part_options(options,"connector"),{fill:"none",stroke:"#aaa",'stroke-width':1,opacity:1.0},true)>,
        if (options.labels != false and mark.part_selected(label_ctx,row)) <text class: "funnel-label", x: ctx.plot_w / 2.0, y: (top + bottom) / 2.0,
            'text-anchor': "middle", 'dominant-baseline': "middle", 'font-size': 12,
            *:mark.style(label_ctx,row,mark.part_options(options,"label"),{fill:if (options.label_color != null) options.label_color else "#222",opacity:1.0}),
            if (label != null) string(label) else string(values[i])>)])
}

pub fn fraction(value, options) {
    let domain = options.domain;
    if (not (domain is array) or len(domain) != 2 or not all(domain |> util.finite_number(~)) or domain[0] == domain[1] or
        not util.finite_number(value)) error("chart: gauge and liquid require a finite value and a distinct two-bound domain")
    else {
        let t = util.inv_lerp(domain[0], domain[1], value);
        if (options.clamp == false and (t < 0 or t > 1)) error("chart: indicator value is outside its domain")
        else util.clamp_val(t, 0.0, 1.0)
    }
}

fn threshold_color(options, value, fallback) {
    let candidates = [for (threshold in options.thresholds where value >= threshold.value) threshold.color];
    if (len(candidates) > 0) candidates[len(candidates) - 1] else fallback
}

fn dial_scale(options, cx, cy, radius, thickness, start, end, theme) {
    let count = int(if (options.tick_count != null) options.tick_count else 4);
    let minor = int(if (options.minor_tick_count != null) options.minor_tick_count else 4);
    let font = text.style(options, "tick", 9);
    let inner = radius - thickness;
    let nodes = [for (i in 0 to (count * minor), let t = float(i) / float(count * minor),
        let angle = util.lerp(start, end, t), let major = i % minor == 0,
        let label_radius = max(0.0, inner - font.font_size - 8.0))
        <g class: if (major) "tick" else "gauge-minor-tick",
            <line x1: cx + (inner - 3.0) * math.cos(angle), y1: cy + (inner - 3.0) * math.sin(angle),
                x2: cx + (inner - (if (major) 8.0 else 5.0)) * math.cos(angle),
                y2: cy + (inner - (if (major) 8.0 else 5.0)) * math.sin(angle),
                stroke: if (options.tick_color != null) options.tick_color else theme.axis_tick_color,
                'stroke-width': if (major) 1.2 else 0.7, opacity: if (major) 0.8 else 0.45>;
            if (major) <text x: cx + label_radius * math.cos(angle), y: cy + label_radius * math.sin(angle),
                'text-anchor': "middle", 'dominant-baseline': "middle", *:text.attributes(font),
                fill: if (options.tick_label_color != null) options.tick_label_color else theme.axis_label_color,
                util.format_value(util.lerp(options.domain[0], options.domain[1], t), options.tick_format)>>];
    axis.fit_frame_labels(svg.group_class("gauge-scale", nodes))
}

fn gauge_pointer(ctx, options, appearance, cx, cy, length, angle, thickness) {
    let needle = options.pointer_shape == "needle";
    let ux = math.cos(angle); let uy = math.sin(angle);
    let hub = if (options.pointer_hub_radius != null) options.pointer_hub_radius else max(3.0, thickness * 0.45);
    let half_width = hub * 0.45;
    // needle and pivot share one part so state and animation are applied once.
    svg.group_class("gauge-pointer", [<g *:appearance,
        if (needle) <path d: svg.line_path([[cx - uy * half_width, cy + ux * half_width],
            [cx + ux * length, cy + uy * length], [cx + uy * half_width, cy - ux * half_width]]) ++ " Z">
        else <line x1: cx, y1: cy, x2: cx + ux * length, y2: cy + uy * length, 'stroke-linecap': "round">;
        if (options.pointer_hub == true) <g 'pointer-events': "none",
            <circle cx: cx, cy: cy, r: hub, fill: ctx._theme.background,
                stroke: if (needle) appearance.fill else appearance.stroke, 'stroke-width': 1.3>;
            <circle cx: cx, cy: cy, r: hub * 0.45, fill: if (needle) appearance.fill else appearance.stroke, stroke: "none">>>])
}

pub fn render(data, ctx, options) {
    let row = if (len(data) > 0) data[0] else {};
    let parts=map([for (part in ["value","track","pointer","target","label"]) for (value in [part,
        mark.part_context(ctx,options,part,[row])]) value]);
    let actual = if (options.value != null) options.value else value(row, ctx, options);
    let domain = if (options.domain != null) options.domain else ctx.encoding.y.scale.domain;
    let settings = {*:options, domain: domain};
    let t = fraction(actual, settings);
    let target = if (options.target != null) fraction(options.target, settings) else null;
    let tick_count = if (options.tick_count != null) options.tick_count else 4;
    let minor_count = if (options.minor_tick_count != null) options.minor_tick_count else 4;
    let invalid = util.first_error([t, target, mark.part_error([for (key,part in parts) part]),
        if (options.ticks == true and (not util.finite_number(tick_count) or tick_count < 1 or floor(tick_count) != tick_count or
            not util.finite_number(minor_count) or minor_count < 1 or floor(minor_count) != minor_count)) error("chart: gauge tick counts must be positive integers"),
        if (options.pointer_shape != null and not contains(["line", "needle"], options.pointer_shape)) error("chart: unknown gauge pointer shape"),
        if (options.pointer_hub_radius != null and (not util.finite_number(options.pointer_hub_radius) or options.pointer_hub_radius <= 0)) error("chart: gauge hub radius must be positive"),
        for (threshold in options.thresholds)
        if (not util.finite_number(threshold.value) or threshold.color == null) error("chart: indicator thresholds require finite values and colors")]);
    let cx = ctx.plot_w / 2.0; let cy = ctx.plot_h / 2.0;
    let radius = min(ctx.plot_w, ctx.plot_h) / 2.0;
    let thickness = if (options.thickness != null) options.thickness else radius * 0.15;
    let start = if (options.start_angle != null) options.start_angle else -util.PI;
    let end = if (options.end_angle != null) options.end_angle else 0.0;
    let fill = threshold_color(options, actual, mark.appearance(ctx, "color", row, color.default_color));
    let appearance = mark.style(parts.value, row, mark.part_options(options,"value"), {fill: fill, opacity: 1.0});
    let track=mark.style(parts.track,row,mark.part_options(options,"track"),{fill:"#eee",opacity:1.0});
    let needle = options.pointer_shape == "needle";
    let pointer=mark.style(parts.pointer,row,mark.part_options(options,"pointer"),
        {fill:if (needle) "#222" else "none",stroke:if (needle) "none" else "#222",'stroke-width':2,opacity:1.0},not needle);
    let target_style=mark.style(parts.target,row,mark.part_options(options,"target"),{fill:"none",stroke:"#222",'stroke-width':1,opacity:1.0},true);
    let label_options = mark.part_options(options, "label");
    let label_font = text.style(label_options, "label", 18);
    let unit = if (options.label_unit != null) string(options.label_unit) else "";
    let label = util.format_value(actual, options.format) ++ unit;
    let label_metric = text.measure([label], label_font);
    let label_y = cy + (if (options.kind != "gauge") 0.0 else max(20.0, label_metric[0].height / 2.0 + 12.0));
    let target_font = text.style(label_options, "target_label", 11);
    let target_y = label_y + label_metric[0].height / 2.0 + target_font.font_size / 2.0 + 8.0;
    let failure = util.first_error([invalid, label_metric]);
    if (failure is error) failure
    else if (not util.finite_number(thickness) or thickness <= 0 or thickness > radius or
        not util.finite_number(start) or not util.finite_number(end) or start == end or abs(end - start) > util.TAU)
        error("chart: invalid indicator thickness or angle range")
    else svg.group_class("marks " ++ options.kind, [
        if (options.kind == "liquid") liquid(row, parts.value, parts.track, settings, t, appearance, track, radius)
        else if (options.shape == "linear") <g class: "gauge-linear",
            if (mark.part_selected(parts.track,row)) <rect class:"gauge-track",x: 0, y: cy - thickness / 2.0, width: ctx.plot_w, height: thickness, *:track>
            if (mark.part_selected(parts.value,row)) <rect class: "gauge-value", x: 0, y: cy - thickness / 2.0, width: ctx.plot_w * t, height: thickness, *:appearance, mark.tooltip(parts.value, row)>
            if (target != null and mark.part_selected(parts.target,row)) <line class:"gauge-target",x1: ctx.plot_w * target, x2: ctx.plot_w * target, y1: cy - thickness, y2: cy + thickness, *:target_style>>
        else <g class: "gauge-arc",
            if (mark.part_selected(parts.track,row)) <path class: "gauge-track", d: svg.arc_path(cx, cy, radius - thickness, radius, start, end, options.rounded == true), *:track>
            if (options.ticks == true and mark.part_selected(parts.track,row)) dial_scale(settings, cx, cy, radius, thickness, start, end, ctx._theme)
            if (mark.part_selected(parts.value,row)) <path class: "gauge-value", d: svg.arc_path(cx, cy, radius - thickness, radius, start, util.lerp(start, end, t), options.rounded == true), *:appearance, mark.tooltip(parts.value, row)>
            if (options.pointer != false and mark.part_selected(parts.pointer,row)) gauge_pointer(ctx, options, pointer, cx, cy,
                max(0.0, radius - thickness - (if (options.ticks == true) 26.0 else 0.0)), util.lerp(start, end, t), thickness)
            if (target != null and mark.part_selected(parts.target,row)) <line class: "gauge-target",
                x1: cx + (radius - thickness * 1.5) * math.cos(util.lerp(start, end, target)),
                y1: cy + (radius - thickness * 1.5) * math.sin(util.lerp(start, end, target)),
                x2: cx + radius * math.cos(util.lerp(start, end, target)), y2: cy + radius * math.sin(util.lerp(start, end, target)),
                'stroke-linecap': "round", *:target_style>>,
        if (options.labels != false and mark.part_selected(parts.label,row)) <g class: "indicator-readout",
            *:mark.style(parts.label,row,label_options,{fill:"#222",opacity:1.0}),
            <text class: "indicator-label", x: cx, y: label_y,
                'text-anchor': "middle", 'dominant-baseline': "middle", *:text.attributes(label_font), label>;
            if (options.kind == "gauge" and options.target_label == true and target != null)
                <text class: "gauge-target-label", x: cx, y: target_y, 'text-anchor': "middle", 'dominant-baseline': "middle",
                    *:text.attributes(target_font), fill: if (options.target_label_color != null) options.target_label_color else ctx._theme.axis_label_color,
                    "Target " ++ util.format_value(options.target, options.format) ++ unit>>])
}

fn liquid(row, ctx, track_ctx, options, t, appearance, track, radius) {
    let cx = ctx.plot_w / 2.0; let cy = ctx.plot_h / 2.0;
    let surface = cy + radius - 2.0 * radius * t;
    let amplitude = if (options.wave == false or t == 0 or t == 1) 0.0
        else if (options.wave_amplitude != null) options.wave_amplitude else radius * 0.04;
    let elapsed = if (ctx._time != null) ctx._time else 0.0;
    let outline = [for (i in 0 to 128, let a = util.TAU * float(i) / 128.0) [cx + radius * math.cos(a), cy + radius * math.sin(a)]];
    let wave = [for (i in 0 to 128, let x = cx - radius + 2.0 * radius * float(i) / 128.0,
        let y = surface + amplitude * math.sin(util.TAU * float(i) / 64.0 + elapsed / 500.0),
        let bound = math.sqrt(max(0.0, radius * radius - (x - cx) ** 2)),
        let clipped = util.clamp_val(y, cy - bound, cy + bound)) [x, clipped]];
    let bottom = [for (i in 0 to 128, let a = util.PI * float(i) / 128.0) [cx + radius * math.cos(a), cy + radius * math.sin(a)]];
    if (not util.finite_number(amplitude) or amplitude < 0) error("chart: liquid wave amplitude must be finite and nonnegative")
    else <g class: "liquid-level",
        if (mark.part_selected(track_ctx,row)) <path class:"liquid-track",d: svg.line_path(outline) ++ " Z", *:track>
        if (mark.part_selected(ctx,row)) <path class: "liquid-value", d: svg.line_path([*wave, *bottom]) ++ " Z", *:appearance, mark.tooltip(ctx, row)>>
}
