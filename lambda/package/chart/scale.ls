// chart/scale.ls — Scale engine for the chart library
// Scales map data values (domain) to visual values (range).
// Scale objects are pure data maps. Use scale_apply / scale_ticks / scale_invert
// dispatch functions to operate on them.

import util: .lambda.package.chart.util
import color: .lambda.package.chart.color

// ============================================================
// Scale constructors (return pure data maps)
// ============================================================

pub fn linear_scale(dlo, dhi, rlo, rhi) {
    { kind: "linear", domain: [float(dlo), float(dhi)], "range": [float(rlo), float(rhi)] }
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
    { kind: "log", domain: [float(dlo), float(dhi)], "range": [float(rlo), float(rhi)], base: float(b) }
}

pub fn sqrt_scale(dlo, dhi, rlo, rhi) {
    { kind: "sqrt", domain: [float(dlo), float(dhi)], "range": [float(rlo), float(rhi)] }
}

pub fn band_scale(categories, rlo, rhi, pad) {
    let n = len(categories);
    let range_span = float(rhi) - float(rlo);
    let total_padding = float(pad) * float(n + 1);
    let band_w = if (n > 0) (range_span - total_padding) / float(n) else range_span;
    { kind: "band", domain: categories, "range": [float(rlo), float(rhi)], bandwidth: band_w, padding: float(pad) }
}

pub fn point_scale(categories, rlo, rhi, pad) {
    let n = len(categories);
    let range_span = float(rhi) - float(rlo);
    let pad_total = float(pad) * 2.0;
    let step = if (n > 1) (range_span - pad_total) / float(n - 1) else 0.0;
    { kind: "point", domain: categories, "range": [float(rlo), float(rhi)], step: step, padding: float(pad) }
}

pub fn ordinal_scale(categories, range_values) {
    { kind: "ordinal", domain: categories, "range": range_values }
}

// ============================================================
// Dispatch: scale_apply
// ============================================================

pub fn scale_apply(sc, value) {
    if sc.kind == "linear" {
        let t = util.inv_lerp(sc.domain[0], sc.domain[1], float(value));
        util.lerp(sc.range[0], sc.range[1], t)
    } else if sc.kind == "log" {
        let log_lo = log(sc.domain[0]) / log(sc.base);
        let log_hi = log(sc.domain[1]) / log(sc.base);
        let log_val = log(float(value)) / log(sc.base);
        let t = util.inv_lerp(log_lo, log_hi, log_val);
        util.lerp(sc.range[0], sc.range[1], t)
    } else if sc.kind == "sqrt" {
        let sqrt_lo = sqrt(abs(sc.domain[0]));
        let sqrt_hi = sqrt(abs(sc.domain[1]));
        let sv = sqrt(abs(float(value)));
        let t = util.inv_lerp(sqrt_lo, sqrt_hi, sv);
        util.lerp(sc.range[0], sc.range[1], t)
    } else if sc.kind == "band" {
        let categories = sc.domain;
        let n = len(categories);
        let range_lo = sc.range[0];
        let pad = sc.padding;
        let band_w = sc.bandwidth;
        let idx = (for (i in 0 to (n - 1))
            if (categories[i] == value) i else null) that (~ != null);
        (if (len(idx) > 0) (let i = idx[0], range_lo + pad + float(i) * (band_w + pad))
        else range_lo)
    } else if sc.kind == "point" {
        let categories = sc.domain;
        let n = len(categories);
        let range_lo = sc.range[0];
        let pad = sc.padding;
        let step = sc.step;
        let idx = (for (i in 0 to (n - 1))
            if (categories[i] == value) i else null) that (~ != null);
        (if (len(idx) > 0) (range_lo + pad + float(idx[0]) * step)
        else range_lo)
    } else if sc.kind == "ordinal" {
        let categories = sc.domain;
        let range_values = sc.range;
        let n = len(categories);
        let idx = (for (i in 0 to (n - 1))
            if (categories[i] == value) i else null) that (~ != null);
        (if (len(idx) > 0) range_values[idx[0] % len(range_values)]
        else range_values[0])
    } else if sc.kind == "sequential-color" {
        let t = util.inv_lerp(float(sc.domain[0]), float(sc.domain[1]), float(value));
        color.sequential_color(sc.scheme, t)
    } else {
        null
    }
}

// ============================================================
// Dispatch: scale_ticks
// ============================================================

pub fn scale_ticks(sc, count) {
    if sc.kind == "linear" {
        util.nice_ticks(sc.domain[0], sc.domain[1], count)
    } else if sc.kind == "log" {
        let log_lo = log(sc.domain[0]) / log(sc.base);
        let log_hi = log(sc.domain[1]) / log(sc.base);
        let lo_exp = int(floor(log_lo));
        let hi_exp = int(ceil(log_hi));
        for (e in lo_exp to hi_exp) sc.base ** float(e)
    } else if sc.kind == "sqrt" {
        util.nice_ticks(sc.domain[0], sc.domain[1], count)
    } else if sc.kind == "band" {
        sc.domain
    } else if sc.kind == "point" {
        sc.domain
    } else if sc.kind == "ordinal" {
        sc.domain
    } else if sc.kind == "sequential-color" {
        util.nice_ticks(sc.domain[0], sc.domain[1], count)
    } else {
        []
    }
}

// ============================================================
// Dispatch: scale_invert
// ============================================================

pub fn scale_invert(sc, pixel) {
    if sc.kind == "linear" {
        let t = util.inv_lerp(sc.range[0], sc.range[1], float(pixel));
        util.lerp(sc.domain[0], sc.domain[1], t)
    } else {
        null
    }
}

// ============================================================
// Scale inference from encoding
// ============================================================

pub fn infer_scale(channel, data, rlo, rhi) {
    let field_name = channel.field;
    let data_type = channel.dtype;
    let values = data | ~[field_name];

    if data_type == "quantitative" {
        let include_zero = if (channel.stack) true
            else if (channel.zero != null) channel.zero
            else false;
        linear_scale_nice(values, rlo, rhi, include_zero)
    } else if data_type == "temporal" {
        let unix_vals = values | float(~);
        linear_scale_nice(unix_vals, rlo, rhi, false)
    } else {
        let cats = util.unique_vals(values);
        band_scale(cats, rlo, rhi, 4.0)
    }
}

pub fn infer_color_scale(channel, data) {
    let field_name = channel.field;
    let data_type = channel.dtype;
    let values = data | ~[field_name];
    let scheme_name = if (channel.scale and channel.scale.scheme) channel.scale.scheme
        else null;

    if data_type == "quantitative" {
        let scheme = if (scheme_name) color.get_scheme(scheme_name) else color.blues;
        let ext = util.extent(values);
        { kind: "sequential-color", domain: [ext[0], ext[1]], scheme: scheme }
    } else {
        let cats = util.unique_vals(values);
        let scheme = if (scheme_name) color.get_scheme(scheme_name) else color.category10;
        ordinal_scale(cats, scheme)
    }
}
