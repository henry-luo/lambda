// sRGB mixing for the common xcolor A!percent!B form.
import pdf_util: lambda.pdf.util

pub fn valid_hex(hex, i) {
    if (i >= len(hex)) true
    else if (not pdf_util.is_hex_digit(hex[i])) false
    else valid_hex(hex, i + 1)
}

fn rgb(hex) {
    if (hex == null) null
    else if (len(hex) == 7 and starts_with(hex, "#") and valid_hex(hex, 1))
        [pdf_util.hex_digit_value(hex[1]) * 16 + pdf_util.hex_digit_value(hex[2]),
         pdf_util.hex_digit_value(hex[3]) * 16 + pdf_util.hex_digit_value(hex[4]),
         pdf_util.hex_digit_value(hex[5]) * 16 + pdf_util.hex_digit_value(hex[6])]
    else if (starts_with(hex, "rgb(") and ends_with(hex, ")")) {
        let parts = split(slice(hex, 4, len(hex) - 1), ",")
        let channels = if (len(parts) == 3) [for (part in parts) int(trim(part)) ^ { null }] else []
        if (len(channels) == 3 and all([for (channel in channels)
            channel != null and channel >= 0 and channel <= 255])) channels
        else null
    } else null
}

pub fn mix(first, second, percent) {
    let a = rgb(first)
    let b = rgb(second)
    let p = float(percent) ^ { null }
    if (a == null or b == null or p == null or p < 0 or p > 100) null
    else {
        let q = p / 100.0
        let r = int(a[0] * q + b[0] * (1.0 - q))
        let g = int(a[1] * q + b[1] * (1.0 - q))
        let blue = int(a[2] * q + b[2] * (1.0 - q))
        "rgb(" ++ r ++ "," ++ g ++ "," ++ blue ++ ")"
    }
}

pub fn supported_model(model) {
    model == "HTML" or model == "html" or model == "rgb" or
    model == "RGB" or model == "gray" or model == "named"
}
