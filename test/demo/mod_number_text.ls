// Fixed-point number text for live readouts and CSS values: integer pieces avoid the general
// numeric formatter's exponent forms and significant-digit rounding, and a value that rounds to
// zero never prints as "-0".
pub fn fixed(value, places) {
    let factor = 10 ** places
    let whole = int(round(abs(value) * factor))
    let fraction = string(whole % factor);
    (if (value < 0 and whole > 0) "-" else "") ++ string(whole div factor) ++
        (if (places == 0) "" else "." ++ join(fill(max(0, places - len(fraction)), "0"), "") ++ fraction)
}
