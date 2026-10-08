// Ordered stages and bounded value displays retain the original datum on each part.
import util: .util
import parse: .parse
import mark: .mark
import svg: .svg
import color: .color

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

pub fn render(data, ctx, options) {
    let row = if (len(data) > 0) data[0] else {};
    let parts=map([for (part in ["value","track","pointer","target","label"]) for (value in [part,
        mark.part_context(ctx,options,part,[row])]) value]);
    let actual = if (options.value != null) options.value else value(row, ctx, options);
    let domain = if (options.domain != null) options.domain else ctx.encoding.y.scale.domain;
    let settings = {*:options, domain: domain};
    let t = fraction(actual, settings);
    let target = if (options.target != null) fraction(options.target, settings) else null;
    let invalid = util.first_error([t, target, mark.part_error([for (key,part in parts) part]),for (threshold in options.thresholds)
        if (not util.finite_number(threshold.value) or threshold.color == null) error("chart: indicator thresholds require finite values and colors")]);
    let cx = ctx.plot_w / 2.0; let cy = ctx.plot_h / 2.0;
    let radius = min(ctx.plot_w, ctx.plot_h) / 2.0;
    let thickness = if (options.thickness != null) options.thickness else radius * 0.15;
    let start = if (options.start_angle != null) options.start_angle else -util.PI;
    let end = if (options.end_angle != null) options.end_angle else 0.0;
    let fill = threshold_color(options, actual, mark.appearance(ctx, "color", row, color.default_color));
    let appearance = mark.style(parts.value, row, mark.part_options(options,"value"), {fill: fill, opacity: 1.0});
    let track=mark.style(parts.track,row,mark.part_options(options,"track"),{fill:"#eee",opacity:1.0});
    let pointer=mark.style(parts.pointer,row,mark.part_options(options,"pointer"),{fill:"none",stroke:"#222",'stroke-width':2,opacity:1.0},true);
    let target_style=mark.style(parts.target,row,mark.part_options(options,"target"),{fill:"none",stroke:"#222",'stroke-width':1,opacity:1.0},true);
    if (invalid is error) invalid
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
            if (mark.part_selected(parts.track,row)) <path class: "gauge-track", d: svg.arc_path(cx, cy, radius - thickness, radius, start, end), *:track>
            if (mark.part_selected(parts.value,row)) <path class: "gauge-value", d: svg.arc_path(cx, cy, radius - thickness, radius, start, util.lerp(start, end, t)), *:appearance, mark.tooltip(parts.value, row)>
            if (options.pointer != false and mark.part_selected(parts.pointer,row)) <line class: "gauge-pointer", x1: cx, y1: cy,
                x2: cx + (radius - thickness) * math.cos(util.lerp(start, end, t)),
                y2: cy + (radius - thickness) * math.sin(util.lerp(start, end, t)), *:pointer>
            if (target != null and mark.part_selected(parts.target,row)) <line class: "gauge-target",
                x1: cx + (radius - thickness * 1.5) * math.cos(util.lerp(start, end, target)),
                y1: cy + (radius - thickness * 1.5) * math.sin(util.lerp(start, end, target)),
                x2: cx + radius * math.cos(util.lerp(start, end, target)), y2: cy + radius * math.sin(util.lerp(start, end, target)), *:target_style>>,
        if (options.labels != false and mark.part_selected(parts.label,row)) <text class: "indicator-label", x: cx, y: cy + (if (options.kind == "gauge") 20 else 0),
            'text-anchor': "middle", 'dominant-baseline': "middle", 'font-size': 18,
            *:mark.style(parts.label,row,mark.part_options(options,"label"),{fill:"#222",opacity:1.0}),util.format_value(actual, options.format)>])
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
