// chart/util.ls — Math utilities for the chart library
// Nice number calculations, tick generation, and numeric helpers.

// ============================================================
// Constants
// ============================================================
pub PI = 3.141592653589793
pub TAU = 6.283185307179586

// ============================================================
// Nice number calculation
// ============================================================

// round a number to a "nice" value (1, 2, 5, 10, 20, 50, ...)
// used for axis tick spacing and domain rounding
pub fn nice_num(val, rounding) {
    let exponent = floor(log10(abs(float(val))));
    let fraction = float(val) / (10.0 ^ exponent);
    let nice_fraction = if (rounding)
        (if (fraction < 1.5) 1.0
        else if (fraction < 3.0) 2.0
        else if (fraction < 7.0) 5.0
        else 10.0)
    else
        (if (fraction <= 1.0) 1.0
        else if (fraction <= 2.0) 2.0
        else if (fraction <= 5.0) 5.0
        else 10.0);
    nice_fraction * (10.0 ^ exponent)
}

// ============================================================
// Tick generation
// ============================================================

// generate nice tick values for a given domain [lo, hi]
pub fn nice_ticks(dlo, dhi, count: int) {
    let range_span = float(dhi) - float(dlo);
    if range_span == 0.0 { [float(dlo)] }
    else {
        let rough_step = range_span / float(if (count > 1) count - 1 else 1);
        let step = nice_num(rough_step, true);
        let nice_lo = floor(float(dlo) / step) * step;
        let nice_hi = ceil(float(dhi) / step) * step;
        let n = int((nice_hi - nice_lo) / step) + 1;
        (for (i in 0 to (n - 1))
            (let v = nice_lo + float(i) * step,
            round(v * 1e10) / 1e10))
    }
}

// extend domain to nice boundaries
pub fn nice_domain(dlo, dhi) {
    let lo_f = float(dlo);
    let hi_f = float(dhi);
    let range_span = hi_f - lo_f;
    if range_span == 0.0 {
        if lo_f == 0.0 { [0.0, 1.0] }
        else {
            let abs_lo = abs(lo_f);
            let abs_hi = abs(hi_f);
            [lo_f - abs_lo * 0.1, hi_f + abs_hi * 0.1]
        }
    } else {
        let step = nice_num(range_span / 5.0, false);
        [floor(lo_f / step) * step, ceil(hi_f / step) * step]
    }
}

// ============================================================
// Numeric helpers
// ============================================================

// linear interpolation
pub fn lerp(a, b, t) {
    float(a) + (float(b) - float(a)) * float(t)
}

// inverse lerp: given a value in [a, b], return t in [0, 1]
pub fn inv_lerp(a, b, v) {
    let af = float(a);
    let bf = float(b);
    let vf = float(v);
    if (af == bf) 0.0
    else (vf - af) / (bf - af)
}

// clamp value to [lo, hi]
pub fn clamp_val(v, lo, hi) {
    let vf = float(v);
    let lof = float(lo);
    let hif = float(hi);
    if (vf < lof) lof
    else if (vf > hif) hif
    else vf
}

// convert degrees to radians
pub fn deg_to_rad(deg) {
    float(deg) * PI / 180.0
}

// convert radians to degrees
pub fn rad_to_deg(rad) {
    float(rad) * 180.0 / PI
}

// format a number for display: remove trailing zeros from floats
pub fn fmt_num(value) string {
    if (value is int) string(value)
    else string(round(float(value) * 1000000.0) / 1000000.0)
}

// ============================================================
// Collection helpers
// ============================================================

// unique helper: check if arr contains val (recursive linear scan)
fn has_val(arr, val, n) {
    if (n <= 0) false
    else if (arr[n - 1] == val) true
    else has_val(arr, val, n - 1)
}

// unique helper: build result array recursively
fn unique_build(arr, i, result) {
    if (i >= len(arr)) result
    else if (has_val(result, arr[i], len(result))) unique_build(arr, i + 1, result)
    else unique_build(arr, i + 1, [*result, arr[i]])
}

// replacement for builtin unique() which is broken for strings
pub fn unique_vals(arr) {
    unique_build(arr, 0, [])
}

// extract unique values from an array by a key function
pub fn unique_by(arr, key_fn) {
    unique_vals(arr | key_fn(~))
}

// group an array by a field name, returns list of {key, items} maps
pub fn group_by(arr, field: string) {
    let all_keys = unique_vals(arr | ~[field]);
    (for (k in all_keys)
        {key: k, items: (arr that ~[field] == k)})
}

// find extent (min, max) of numeric values in an array
pub fn extent(arr) {
    [min(arr), max(arr)]
}

// find extent of a field in an array of maps
pub fn field_extent(arr, field: string) {
    let vals = arr | ~[field];
    [min(vals), max(vals)]
}

// get unique values of a field
pub fn field_values(arr, field: string) {
    unique_vals(arr | ~[field])
}
