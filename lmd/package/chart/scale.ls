// chart/scale.ls — Scale engine for the chart library
// Scales map data values (domain) to visual values (range).
// Scale objects are pure data maps. Use scale_apply / scale_ticks / scale_invert
// dispatch functions to operate on them.

import util: .util
import color: .color
import parse: .parse
import records: .records
import calendar: .calendar

// shared visual channels use the same constructors for views and named composite parts.
pub fn visual_mapping(key, channel, data) {
    if (channel == null or not parse.option_enabled(channel, "scale") or channel.value != null) null
    else if (key == "color" or key == "stroke") infer_color_scale(channel, data)
    else if (key == "shape") shape_scale(channel, data)
    else if (key == "theta") angular_scale(channel, data)
    else if (key == "radius") radius_scale(channel, data)
    else visual_scale(channel, data, if (key == "opacity") 0.2 else 20.0, if (key == "opacity") 1.0 else 200.0)
}

// ============================================================
// Scale constructors (return pure data maps)
// ============================================================

pub fn linear_scale(dlo, dhi, rlo, rhi) {
    { kind: "linear", domain: [float(dlo), float(dhi)], 'range': [float(rlo), float(rhi)] }
}

pub fn linear_scale_nice(values, rlo, rhi, include_zero: bool) {
    let raw_min = min(values);
    let raw_max = max(values);
    let lo = if (include_zero and raw_min > 0.0) 0.0 else float(raw_min);
    let hi = float(raw_max);
    let nice = util.nice_domain(lo, hi);
    linear_scale(nice[0], nice[1], rlo, rhi)
}

pub fn log_scale(dlo, dhi, rlo, rhi, b) {
    { kind: "log", domain: [float(dlo), float(dhi)], 'range': [float(rlo), float(rhi)], base: float(b) }
}

pub fn sqrt_scale(dlo, dhi, rlo, rhi) {
    { kind: "sqrt", domain: [float(dlo), float(dhi)], 'range': [float(rlo), float(rhi)] }
}

pub fn band_scale(categories, rlo, rhi, pad) {
    let n = len(categories);
    let range_span = float(rhi) - float(rlo);
    let total_padding = float(pad) * float(n + 1);
    // Padding follows range direction; subtracting it from a signed span enlarged inverted bands.
    let direction = if (range_span < 0) -1.0 else 1.0;
    let band_w = if (n > 0) max([0.0, abs(range_span) - total_padding]) / float(n) else abs(range_span);
    { kind: "band", domain: categories, 'range': [float(rlo), float(rhi)],
        bandwidth: direction * band_w, padding: direction * float(pad) }
}

pub fn point_scale(categories, rlo, rhi, pad) {
    let n = len(categories);
    let range_span = float(rhi) - float(rlo);
    let directed_padding = if (range_span < 0) 0.0 - float(pad) else float(pad);
    let pad_total = directed_padding * 2.0;
    let step = if (n > 1) (range_span - pad_total) / float(n - 1) else 0.0;
    { kind: "point", domain: categories, 'range': [float(rlo), float(rhi)], step: step, padding: directed_padding }
}

pub fn ordinal_scale(categories, range_values) {
    { kind: "ordinal", domain: categories, 'range': range_values }
}

// temporal scale: linear over unix milliseconds with temporal metadata
pub fn temporal_scale(values, rlo, rhi) {
    configured_scale(values, rlo, rhi, "temporal")
}

// ============================================================
// Dispatch: scale_apply
// ============================================================

fn numeric_value(value, kind) float | error {
    if (kind == "temporal" and not (value is int or value is i64 or value is float))
        calendar.timestamp(value)
    else float(value)
}

fn power_value(value, exponent) =>
    (if (value < 0) -1.0 else 1.0) * (abs(value) ** exponent)

fn continuous_value(sc, value) {
    let converted = numeric_value(value, sc.kind);
    if (sc.kind == "log") math.log(converted) / math.log(sc.base)
    else if (sc.kind == "sqrt") power_value(converted, 0.5)
    else if (sc.kind == "pow") power_value(converted, sc.exponent)
    else converted
}

pub fn scale_apply(sc, value) {
    let result = if (sc.kind == "identity") value
    else if (sc.kind == "band" or sc.kind == "point") (
        let index = util.find_index(sc.domain, value),
        if (sc.offset != null) sc.offset + float(index) * sc.step
        else if (sc.kind == "band") sc.range[0] + sc.padding + float(index) * (sc.bandwidth + sc.padding)
        else sc.range[0] + sc.padding + float(index) * sc.step
    ) else if (sc.kind == "ordinal") (
        let index = util.find_index(sc.domain, value),
        sc.range[index % len(sc.range)]
    ) else if (sc.kind == "sequential-color" or sc.kind == "diverging-color") (
        let v = float(value),
        let d = sc.domain,
        let fraction = if (sc.kind == "diverging-color") (
            if ((d[0] <= d[2] and v <= d[1]) or (d[0] > d[2] and v >= d[1]))
                0.5 * util.inv_lerp(d[0], d[1], v)
            else 0.5 + 0.5 * util.inv_lerp(d[1], d[2], v))
            else util.inv_lerp(d[0], d[len(d) - 1], v),
        color.sequential_color(sc.scheme, if (sc.reverse) 1.0 - fraction else fraction)
    ) else if (sc.kind == "linear" or sc.kind == "log" or sc.kind == "sqrt" or
        sc.kind == "pow" or sc.kind == "temporal") (
        let lo = continuous_value(sc, sc.domain[0]),
        let hi = continuous_value(sc, sc.domain[len(sc.domain) - 1]),
        let fraction = util.inv_lerp(lo, hi, continuous_value(sc, value)),
        util.lerp(sc.range[0], sc.range[len(sc.range) - 1],
            if (sc.clamp) util.clamp_val(fraction, 0.0, 1.0) else fraction)
    ) else null;
    if (sc.round and (result is float)) round(result) else result
}

// ============================================================
// Dispatch: scale_ticks
// ============================================================

pub fn scale_ticks(sc, count) {
    if sc.kind == "linear" {
        util.nice_ticks(sc.domain[0], sc.domain[1], count)
    } else if sc.kind == "log" {
        let log_lo = math.log(sc.domain[0]) / math.log(sc.base);
        let log_hi = math.log(sc.domain[1]) / math.log(sc.base);
        let lo_exp = int(floor(log_lo));
        let hi_exp = int(ceil(log_hi));
        [for (e in lo_exp to hi_exp) sc.base ** float(e)]
    } else if (sc.kind == "sqrt" or sc.kind == "pow") {
        util.nice_ticks(sc.domain[0], sc.domain[1], count)
    } else if sc.kind == "band" {
        sc.domain
    } else if sc.kind == "point" {
        sc.domain
    } else if sc.kind == "ordinal" {
        sc.domain
    } else if (sc.kind == "sequential-color" or sc.kind == "diverging-color") {
        util.nice_ticks(sc.domain[0], sc.domain[len(sc.domain) - 1], count)
    } else if sc.kind == "temporal" {
        calendar.ticks(sc.domain[0], sc.domain[len(sc.domain) - 1], count,
            if (sc.timezone != null) sc.timezone else 0)
    } else {
        []
    }
}

// ============================================================
// Dispatch: scale_invert
// ============================================================

pub fn scale_invert(sc, pixel) {
    if (sc.kind == "identity") pixel
    else if (sc.kind == "linear" or sc.kind == "temporal" or sc.kind == "log" or sc.kind == "sqrt" or sc.kind == "pow") {
        let fraction = util.inv_lerp(sc.range[0], sc.range[len(sc.range) - 1], float(pixel));
        let value = util.lerp(continuous_value(sc, sc.domain[0]), continuous_value(sc, sc.domain[len(sc.domain) - 1]),
            if (sc.clamp) util.clamp_val(fraction, 0.0, 1.0) else fraction);
        if (sc.kind == "log") sc.base ** value
        else if (sc.kind == "sqrt") power_value(value, 2.0)
        else if (sc.kind == "pow") power_value(value, 1.0 / sc.exponent)
        else value
    } else null
}

// ============================================================
// Scale inference from encoding
// ============================================================

// Construct inferred and explicit scales through one domain/range policy.
pub fn configured_scale(values, rlo, rhi, default_kind, options = null, include_zero = false) {
    let requested = if (options and options.type != null) options.type else default_kind;
    let kind = if (requested == "time" or requested == "utc") "temporal" else requested;
    let explicit_domain = if (options) options.domain else null;
    let raw_range = if (options and options.range != null) options.range else [rlo, rhi];
    let output_range = if (options and options.reverse) [raw_range[len(raw_range) - 1], raw_range[0]] else raw_range;
    let categorical = kind == "band" or kind == "point" or kind == "ordinal";
    let zone = if (requested == "utc") 0 else calendar.offset(options);
    let domain = if (categorical) (
        if (explicit_domain != null) explicit_domain else util.unique_vals(values)
    ) else (
        let raw_values = if (explicit_domain != null) explicit_domain
            else if (len(values) > 0) values else [0.0, 1.0],
        let numbers = [for (value in raw_values) numeric_value(value, kind)],
        let zero = if (options and options.zero != null) options.zero else include_zero,
        let lo = if (explicit_domain != null) numbers[0] else min(numbers),
        let hi = if (explicit_domain != null) numbers[len(numbers) - 1] else max(numbers),
        let bounds = [if (zero and kind != "log" and kind != "temporal") min([lo, 0.0]) else lo,
            if (zero and kind != "log" and kind != "temporal") max([hi, 0.0]) else hi],
        let nice = if (options and options.nice != null) options.nice
            else explicit_domain == null and kind != "log",
        let base = if (options and options.base != null) options.base else 10.0,
        let extended = if (nice and kind == "log")
            [base ** floor(math.log(bounds[0]) / math.log(base)), base ** ceil(math.log(bounds[1]) / math.log(base))]
            else if (nice and kind == "temporal") calendar.nice_extent(bounds[0], bounds[1],
                if (calendar.valid_offset(zone)) zone else 0)
            else if (nice) util.nice_domain(bounds[0], bounds[1]) else bounds,
        let failure = util.first_error(numbers),
        if (failure is error) failure else
            [if (options and options.domain_min != null) numeric_value(options.domain_min, kind) else extended[0],
                if (options and options.domain_max != null) numeric_value(options.domain_max, kind) else extended[1]]
    );
    let common = {kind: kind, domain: domain, range: output_range,
        clamp: options and options.clamp, round: options and options.round,
        base: if (options and options.base != null) options.base else 10.0,
        timezone: zone,
        exponent: if (options and options.exponent != null) options.exponent else 1.0};
    if (domain is error) domain
    else if (kind == "temporal" and not calendar.valid_offset(zone)) error("chart: invalid temporal timezone; use UTC offset minutes or an IANA zone name")
    else if (kind == "band" or kind == "point") {
        let configured_padding = options and (options.padding != null or options.padding_inner != null or options.padding_outer != null);
        if (not configured_padding) {
            let result = if (kind == "band") band_scale(domain, output_range[0], output_range[1], 4.0)
                else point_scale(domain, output_range[0], output_range[1], 20.0);
            {*:common, *:result}
        } else {
            let inner = if (kind == "point") 1.0 else if (options.padding_inner != null) options.padding_inner
                else if (options.padding != null) options.padding else 0.0;
            let outer = if (options.padding_outer != null) options.padding_outer
                else if (options.padding != null) options.padding else 0.0;
            let span = abs(float(output_range[1]) - float(output_range[0]));
            let step = span / max([1.0, float(len(domain)) - inner + 2.0 * outer]);
            let bandwidth = step * (1.0 - inner);
            {*:common, bandwidth: if (kind == "band") bandwidth else null,
                step: if (output_range[1] < output_range[0]) 0.0 - step else step,
                offset: if (output_range[1] < output_range[0]) output_range[0] - outer * step - bandwidth
                    else output_range[0] + outer * step}
        }
    } else common
}

pub fn position_scale(channel, data, rlo, rhi, mark_type, is_x, secondary = null) {
    if (not channel) null
    else if (channel.value != null) {kind: "identity", domain: [], range: [rlo, rhi]}
    else if (not channel.field and channel.datum == null) null
    else {
        let values = if (channel.field) data |> ~[channel.field] else [channel.datum];
        let all_values = if (secondary and secondary.value == null) [*values, for (row in data,
            let value = if (secondary is string) row[secondary] else parse.channel_value(secondary, row)
            where value != null) value] else values;
        let default_kind = if (channel.dtype == "quantitative" or (channel.dtype == null and (channel.datum is int or channel.datum is float))) "linear"
            else if (channel.dtype == "temporal") "temporal"
            else if (mark_type == "bar" or mark_type == "rect" or mark_type == "violin" or (mark_type == "boxplot" and is_x)) "band"
            else "point";
        let sorted_values = if (channel.sort != null and channel.field != null)
            records.categories(data, channel.field, channel.sort) else all_values;
        let include_zero = if (channel.zero != null) channel.zero else mark_type == "bar";
        if (not parse.option_enabled(channel, "scale")) {kind: "identity", domain: [], range: [rlo, rhi]}
        else configured_scale(sorted_values, rlo, rhi, default_kind, channel.scale, include_zero)
    }
}

pub fn infer_scale(channel, data, rlo, rhi) {
    position_scale(channel, data, rlo, rhi, "bar", true)
}

pub fn infer_color_scale(channel, data) {
    let field_name = channel.field;
    let data_type = channel.dtype;
    let values = channel_values(channel, data);
    let scheme_name = if (channel.scale and channel.scale.scheme) channel.scale.scheme
        else null;
    let explicit_domain = if (channel.scale and channel.scale.domain) channel.scale.domain
        else null;
    let explicit_range = if (channel.scale and channel.scale.range) channel.scale.range
        else null;

    if (not parse.option_enabled(channel, "scale")) {kind: "identity", domain: [], range: []}
    else if (data_type == "quantitative")
        (let numeric = values |: util.finite_number(~),
        let ext = if (explicit_domain != null) explicit_domain else if (len(numeric) > 0) util.extent(numeric) else [0, 1],
        let lo = if (channel.scale.domain_min != null) channel.scale.domain_min else ext[0],
        let hi = if (channel.scale.domain_max != null) channel.scale.domain_max else ext[len(ext) - 1],
        let mid = if (channel.scale.domain_mid != null) channel.scale.domain_mid else if (len(ext) == 3) ext[1] else null,
        let scheme = if (explicit_range != null) explicit_range else if (scheme_name) color.get_scheme(scheme_name)
            else if (mid != null) color.red_blue_midpoint else color.blues,
        if (mid != null and (not util.finite_number(mid) or mid <= min([lo, hi]) or mid >= max([lo, hi])))
            error("chart: color domain_mid must lie inside the domain")
        else {kind: if (mid != null) "diverging-color" else "sequential-color",
            domain: if (mid != null) [lo, mid, hi] else [lo, hi], scheme: scheme, reverse: channel.scale.reverse})
    else
        (let cats = if (explicit_domain != null) explicit_domain else if (field_name != null) records.categories(data, field_name, channel.sort)
            else util.unique_vals(values),
        let scheme = if (scheme_name) color.get_scheme(scheme_name) else color.category10,
        let palette = if (explicit_range != null) explicit_range else scheme,
        ordinal_scale(cats, if (channel.scale.reverse) reverse(palette) else palette))
}

// Shared view mappings use the same policies as individual mark contexts.
pub fn shared_position(layers, channel_name, rlo, rhi) {
    let candidates = [for (layer in layers,
        let channel = layer.encoding[channel_name]
        where channel != null and (channel.field != null or channel.datum != null))
        {channel: channel, mark: layer.mark, data: layer.data}];
    if (len(candidates) == 0) null
    else {
        let first = candidates[0];
        let channel = first.channel;
        let initial = position_scale(channel, first.data, rlo, rhi, first.mark.kind, channel_name == "x");
        let values = [for (layer in layers where not (layer.data is error))
            for (key in (if (layer.stack_mode != null and layer.stack_axis == channel_name) ["_y0", "_y1"] else [channel_name, channel_name ++ "2"]),
                let current = layer.encoding[key]
                where (current != null and current.value == null) or key == "_y0" or key == "_y1")
                for (row in layer.data,
                    let value = if (key == "_y0" or key == "_y1") row[key] else parse.channel_value(current, row)
                    where value != null) value];
        let has_ranges = len([for (layer in layers where layer.encoding[channel_name ++ "2"] != null) true]) > 0;
        let zero = if (channel.zero != null) channel.zero
            else not has_ranges and len([for (candidate in candidates where candidate.mark.kind == "bar") true]) > 0;
        // Rebuilding a shared domain must preserve the channel's resolved categorical order.
        let ordered = if (channel.sort != null and contains(["band", "point", "ordinal"], initial.kind))
            records.categories([for (value in values) {value: value}], "value", channel.sort) else values;
        if (initial is error) initial
        else if (initial.kind == "identity") initial
        else configured_scale(ordered, rlo, rhi, initial.kind, channel.scale, zero)
    }
}

pub fn shape_scale(channel, data) {
    if (channel.field == null and channel.datum == null) null
    else if (channel.dtype == "geojson") {kind: "identity"}
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity"}
    else configured_scale(channel_values(channel, data), 0, 1, "ordinal",
        {*:parse.attributes(channel.scale), range: if (channel.scale.range != null) channel.scale.range
            else ["circle", "square", "diamond", "triangle-up", "cross", "triangle-down"]})
}

pub fn visual_scale(channel, data, rlo, rhi) {
    if (channel == null or (channel.field == null and channel.datum == null)) null
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity"}
    else configured_scale(channel_values(channel, data), rlo, rhi, "linear", channel.scale)
}

pub fn channel_values(channel, data) => if (channel.field != null) data |> ~[channel.field] else [channel.datum]

fn polar_values(channel, data, secondary) => [
    for (value in channel_values(channel, data) where value != null) value,
    for (row in (if (secondary != null and secondary.value == null) data else []),
        let value = parse.channel_value(secondary, row) where value != null) value]

pub fn radius_scale(channel, data, rlo = 0.0, rhi = 1.0, secondary = null) {
    if (channel == null) null
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity", domain: [], range: []}
    else configured_scale(polar_values(channel, data, secondary),
        rlo, rhi, "linear", channel.scale, true)
}

pub fn angular_scale(channel, data, secondary = null) {
    if (channel == null) null
    else if (not parse.option_enabled(channel, "scale")) {kind: "identity", domain: [], range: []}
    else if (channel.dtype == "nominal" or channel.dtype == "ordinal") {
        let domain = if (channel.scale.domain != null) channel.scale.domain else records.categories(data, channel.field, channel.sort);
        let angles = if (channel.scale.range != null) channel.scale.range else [for (index in 0 to (len(domain) - 1)) float(index) * util.TAU / float(len(domain))];
        if (not (angles is array) or len(angles) < len(domain) or len([for (angle in angles where not util.finite_number(angle)) angle]) > 0)
            error("chart: angular range requires a finite angle for each category")
        else ordinal_scale(domain, if (channel.scale.reverse) reverse(angles) else angles)
    } else configured_scale(polar_values(channel, data, secondary),
        0.0, util.TAU, "linear", {*:parse.attributes(channel.scale), nice: if (channel.scale.nice != null) channel.scale.nice else false})
}
