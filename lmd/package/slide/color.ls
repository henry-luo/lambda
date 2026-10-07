import c: .common
import ease: .easing
import hex: lambda.pdf.util

fn byte(text, offset, short) {
    let a = hex.hex_digit_value(text[offset]);
    (if (short) a * 17 else a * 16 + hex.hex_digit_value(text[offset + 1])) / 255.0
}

pub fn parse(value, path) array^ {
    let text = if (value is string) lower(value) else ""
    let numeric = value is array and len(value) == 4 and all([for (v in value) c.finite(v) and v >= 0.0 and v <= 1.0])
    let valid = starts_with(text, "#") and contains([4, 5, 7, 9], len(text)) and
        all([for (i in 1 to len(text) - 1) hex.is_hex_digit(text[i])])
    if (numeric) value
    else if (text == "transparent") [0.0, 0.0, 0.0, 0.0]
    else if (valid) {
        let short = len(text) <= 5
        let step = if (short) 1 else 2;
        [byte(text, 1, short), byte(text, 1 + step, short), byte(text, 1 + 2 * step, short),
            if (len(text) == 5 or len(text) == 9) byte(text, 1 + 3 * step, short) else 1.0]
    } else raise c.fail(path, "interpolated colors require #RGB[A], #RRGGBB[AA], transparent or normalized RGBA[4]")
}

// sRGB channels with premultiplied alpha; transparent RGB has no contribution.
pub fn sample(a, b, t) {
    let alpha = ease.lerp(a[3], b[3], t);
    [*[for (i in 0 to 2) if (alpha == 0.0) 0.0 else ease.lerp(a[i] * a[3], b[i] * b[3], t) / alpha], alpha]
}

pub fn css(rgba) => "rgba(" ++ c.fmt(rgba[0] * 255.0) ++ "," ++ c.fmt(rgba[1] * 255.0) ++ "," ++
    c.fmt(rgba[2] * 255.0) ++ "," ++ c.fmt(rgba[3]) ++ ")"
