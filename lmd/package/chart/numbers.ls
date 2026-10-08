// Shared numeric labels for axes, legends, text, and hover titles.

fn fixed(value, digits) {
    // Large integral floats cannot pass through i64; expand their decimal exponent instead.
    if (value >= 9223372036854775808.0) {
        let rounded = significant(value, 15);
        decimal_shift(fixed(rounded.mantissa, 14), rounded.exponent) ++
            (if (digits > 0) "." ++ join([for (i in 1 to digits) "0"], "") else "")
    } else {
        let factor = 10.0 ** float(digits);
        let rounded = round(value * factor);
        let whole = floor(rounded / factor);
        let remainder = string(i64(round(rounded - whole * factor)));
        string(i64(whole)) ++ (if (digits > 0) "." ++
            join([for (i in 0 to (digits - len(remainder) - 1)) "0"], "") ++ remainder else "")
    }
}

fn exponent(value) => if (value == 0) 0 else int(floor(math.log10(value)))

// Scale in two steps so subnormal values never require an overflowing power of ten.
fn significant(value, precision) {
    let raw = exponent(value);
    let major = max([-300, min([300, raw])]);
    let mantissa = (value / (10.0 ** float(major))) / (10.0 ** float(raw - major));
    let factor = 10.0 ** float(precision - 1);
    let rounded = round(mantissa * factor) / factor;
    if (rounded >= 10) {mantissa: rounded / 10.0, exponent: raw + 1}
    else {mantissa: rounded, exponent: raw}
}

fn decimal_shift(text, places) {
    let parts = split(text, ".");
    let digits = join(parts, "");
    let point = len(parts[0]) + places;
    if (point <= 0) "0." ++ join([for (i in 1 to (0 - point)) "0"], "") ++ digits
    else if (point >= len(digits)) digits ++ join([for (i in 1 to (point - len(digits))) "0"], "")
    else slice(digits, 0, point) ++ "." ++ slice(digits, point)
}

fn scientific(value, digits) {
    let rounded = significant(value, digits + 1);
    fixed(rounded.mantissa, digits) ++ "e" ++
        (if (rounded.exponent >= 0) "+" else "-") ++ string(abs(rounded.exponent))
}

fn grouped(text) {
    let parts = split(text, ".");
    let whole = parts[0];
    join([for (i in 0 to (len(whole) - 1))
        (if (i > 0 and (len(whole) - i) % 3 == 0) "," else "") ++ slice(whole, i, i + 1)], "") ++
        (if (len(parts) > 1) "." ++ parts[1] else "")
}

fn trim_fraction(text) {
    if (ends_with(text, "0") and contains(text, ".")) trim_fraction(slice(text, 0, len(text) - 1))
    else if (ends_with(text, ".")) slice(text, 0, len(text) - 1) else text
}

pub fn format_number(value, pattern) {
    let kind = slice(pattern, len(pattern) - 1);
    let supported = contains(["f", "%", "e", "g", "r", "s", "d"], kind);
    if (not supported or not (value is number) or value is nan or value == inf or value == -inf) string(value)
    else {
        let dot = index_of(pattern, ".");
        let raw_precision = if (dot != null) int(replace(slice(pattern, dot + 1, len(pattern) - 1), "~", "")) else null;
        let significant_kind = kind == "g" or kind == "r" or kind == "s";
        let precision = if (raw_precision != null) raw_precision else 6;
        if (raw_precision is error or precision < (if (significant_kind) 1 else 0) or precision > 15) string(value)
        else {
            let magnitude = abs(float(value)) * (if (kind == "%") 100.0 else 1.0);
            let rounded = if (significant_kind) significant(magnitude, precision) else null;
            let si_exponent = if (kind == "s") max([-24, min([24, int(floor(float(rounded.exponent) / 3.0)) * 3])]) else 0;
            let suffix = if (kind == "%") "%" else if (kind == "s")
                ["y", "z", "a", "f", "p", "n", "µ", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y"][int(si_exponent / 3) + 8] else "";
            let text = if (kind == "e") scientific(magnitude, precision)
                else if (kind == "g" and (rounded.exponent < -6 or rounded.exponent >= precision)) scientific(magnitude, precision - 1)
                else if (significant_kind) decimal_shift(fixed(rounded.mantissa, precision - 1), rounded.exponent - si_exponent)
                else fixed(magnitude, if (kind == "d") 0 else precision);
            let concise = if (contains(pattern, "~")) (
                let parts = split(text, "e"),
                trim_fraction(parts[0]) ++ (if (len(parts) > 1) "e" ++ parts[1] else "")) else text;
            let sign = if (value < 0) (if (contains(pattern, "(")) "(" else "-")
                else if (contains(pattern, "+")) "+" else if (starts_with(pattern, " ")) " " else "";
            sign ++ (if (contains(pattern, "$")) "$" else "") ++
                (if (contains(pattern, ",") and not contains(concise, "e")) grouped(concise) else concise) ++ suffix ++
                (if (value < 0 and contains(pattern, "(")) ")" else "")
        }
    }
}
