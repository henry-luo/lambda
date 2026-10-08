import c: .common
import ease: .easing
import hex: lambda.pdf.util

fn byte(text, offset, short) {
    let a = hex.hex_digit_value(text[offset]);
    (if (short) a * 17 else a * 16 + hex.hex_digit_value(text[offset + 1])) / 255.0
}
fn functional(text) {
    let alpha=starts_with(text,"rgba(");
    let prefix=if (alpha) 5 else 4;
    let parts=split(slice(text,prefix,len(text)-1),",") |> trim(~);
    let values=[for (i,part in parts,let percent=ends_with(part,"%"),
        let component=float(if (percent) slice(part,0,len(part)-1) else part))
        component/(if (percent) 100.0 else if (i<3) 255.0 else 1.0)];
    if (len(parts)!=(if (alpha) 4 else 3) or not all(values |> c.finite(~) and ~>=0.0 and ~<=1.0)) null
    else if (alpha) values else [*values,1.0]
}

pub fn parse(value, path) array^ {
    let text = if (value is string) lower(value) else ""
    let numeric = value is array and len(value) == 4 and all([for (v in value) c.finite(v) and v >= 0.0 and v <= 1.0])
    let valid = starts_with(text, "#") and contains([4, 5, 7, 9], len(text)) and
        all([for (i in 1 to len(text) - 1) hex.is_hex_digit(text[i])])
    let rgba=if ((starts_with(text,"rgb(") or starts_with(text,"rgba(")) and ends_with(text,")")) functional(text) else null;
    if (numeric) value
    else if (rgba!=null) rgba
    else if (text == "transparent") [0.0, 0.0, 0.0, 0.0]
    else if (valid) {
        let short = len(text) <= 5
        let step = if (short) 1 else 2;
        [byte(text, 1, short), byte(text, 1 + step, short), byte(text, 1 + 2 * step, short),
            if (len(text) == 5 or len(text) == 9) byte(text, 1 + 3 * step, short) else 1.0]
    } else raise c.fail(path, "interpolated colors require hex, rgb/rgba(), transparent or normalized RGBA[4]")
}

// sRGB channels with premultiplied alpha; transparent RGB has no contribution.
pub fn sample(a, b, t) {
    let alpha = ease.lerp(a[3], b[3], t);
    [*[for (i in 0 to 2) if (alpha == 0.0) 0.0 else ease.lerp(a[i] * a[3], b[i] * b[3], t) / alpha], alpha]
}

pub fn css(rgba) => "rgba(" ++ c.fmt(rgba[0] * 255.0) ++ "," ++ c.fmt(rgba[1] * 255.0) ++ "," ++
    c.fmt(rgba[2] * 255.0) ++ "," ++ c.fmt(rgba[3]) ++ ")"
