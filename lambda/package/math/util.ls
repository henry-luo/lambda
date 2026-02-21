// math/util.ls — Numeric and string helpers for the math package

// ============================================================
// Number formatting
// ============================================================

// format a float to a fixed number of decimal places as string
pub fn fmt_num(x, decimals) {
    let factor = 10.0 ** decimals
    string(round(x * factor) / factor)
}

// format a number as em units: "0.5em"
pub fn fmt_em(x) => fmt_num(x, 5) ++ "em"

// format a number as percentage: "70%"
pub fn fmt_pct(x) => fmt_num(x * 100.0, 1) ++ "%"

// ============================================================
// Math helpers
// ============================================================

// clamp a number to [lo, hi]
pub fn clamp(x, lo, hi) => max(lo, min(hi, x))

// ============================================================
// String helpers
// ============================================================

// repeat a string n times
pub fn str_repeat(s, n) {
    if (n <= 0) ""
    else if (n == 1) s
    else s ++ str_repeat(s, n - 1)
}

// join an array of strings with a separator
pub fn str_join(arr, sep) {
    if (len(arr) == 0) ""
    else if (len(arr) == 1) string(arr[0])
    else string(arr[0]) ++ sep ++ str_join(slice(arr, 1, len(arr)), sep)
}

// check if string starts with a prefix
pub fn starts_with(s, prefix) {
    if (len(prefix) > len(s)) false
    else slice(s, 0, len(prefix)) == prefix
}

// ============================================================
// Element helpers
// ============================================================

// get element attribute with default
pub fn attr_or(el, key, default_val) {
    let val = el[key]
    if (val == null) default_val else val
}

// get text content of a leaf element
pub fn text_of(el) {
    if (el is string) el
    else if (len(el) > 0) string(el[0])
    else ""
}

// ============================================================
// Constants
// ============================================================

pub PT_PER_EM = 10.0
pub SCRIPT_SPACE = 0.05
