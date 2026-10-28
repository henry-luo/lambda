// shared pure color parsing and HSV conversion (S12.1.1v3, PG12.4).
import hex: lambda.doc.pdf.util
fn finite(v) bool => v is number and abs(v) <= 3.4028234e38
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
    if (len(parts)!=(if (alpha) 4 else 3) or not all(values |> finite(~) and ~>=0.0 and ~<=1.0)) null
    else if (alpha) values else [*values,1.0]
}

pub fn parse(value, path="value") array^ {
    let text = if (value is string) lower(value) else ""
    let numeric = value is array and len(value) == 4 and all([for (v in value) finite(v) and v >= 0.0 and v <= 1.0])
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
    } else raise error("color: " ++ path ++ ": " ++ "expected hex, rgb/rgba(), transparent or normalized RGBA[4]")
}


// HSV operates on encoded sRGB channels; geometry rendering retains its own linear conversion.
pub fn hsv(rgba) map {
    let rgb = slice(rgba,0,3)
    let high = max(rgb), low = min(rgb), delta = high-low
    let hue = if (delta == 0) 0 else if (high == rgb[0]) 60*((rgb[1]-rgb[2])/delta)
        else if (high == rgb[1]) 60*((rgb[2]-rgb[0])/delta+2) else 60*((rgb[0]-rgb[1])/delta+4);
    {h:if (hue < 0) hue+360 else hue,s:if (high == 0) 0 else delta/high,v:high}
}
pub fn rgb(h,s,v,alpha=1.0) array {
    // avoid changing palette byte rounding by adding/subtracting 360 from in-range hues.
    let hue = if (h>=0 and h<360) h else ((h%360)+360)%360
    let chroma = v*s, x = chroma*(1-abs((hue/60)%2-1))
    let channels = [[chroma,x,0],[x,chroma,0],[0,chroma,x],[0,x,chroma],[x,0,chroma],[chroma,0,x]][int(floor(hue/60))];
    [*[for (channel in channels) channel+v-chroma],alpha]
}
let digits = "0123456789abcdef"
pub fn hex_byte(v) => slice(digits,v div 16,v div 16+1) ++ slice(digits,v%16,v%16+1)
pub fn hex(rgba) string => "#" ++ join([for (v in slice(rgba,0,3)) hex_byte(int(round(max(0,min(1,v))*255)))],"")
