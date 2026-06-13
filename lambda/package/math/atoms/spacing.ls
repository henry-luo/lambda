// math/atoms/spacing.ls — Spacing commands (\quad, \qquad, \, \: \; \! \hspace \kern)

import box: lambda.package.math.box
import css: lambda.package.math.css

// ============================================================
// Spacing command rendering
// ============================================================

pub fn render(node, context, render_fn) {
    // space_command nodes use 'value' field; hspace/kern use 'cmd'
    let cmd = if (node.cmd != null) string(node.cmd)
              else if (node.value != null) string(node.value)
              else ""

    // named spacing commands → CSS class
    match cmd {
        case "\\quad": box.box_cls(css.QUAD, 0.0, 0.0, 1.0, "skip")
        case "\\qquad": box.box_cls(css.QQUAD, 0.0, 0.0, 2.0, "skip")
        case "\\enspace": box.box_cls(css.ENSPACE, 0.0, 0.0, 0.5, "skip")
        case "\\,": box.box_cls(css.THIN, 0.0, 0.0, 0.16667, "skip")
        case "\\thinspace": box.box_cls(css.THIN, 0.0, 0.0, 0.16667, "skip")
        case "\\:": box.box_cls(css.MEDIUM, 0.0, 0.0, 0.22222, "skip")
        case "\\medspace": box.box_cls(css.MEDIUM, 0.0, 0.0, 0.22222, "skip")
        case "\\;": box.box_cls(css.THICK, 0.0, 0.0, 0.27778, "skip")
        case "\\thickspace": box.box_cls(css.THICK, 0.0, 0.0, 0.27778, "skip")
        case "\\!": box.box_cls(css.NEG_THIN, 0.0, 0.0, -0.16667, "skip")
        case "\\negthinspace": box.box_cls(css.NEG_THIN, 0.0, 0.0, -0.16667, "skip")
        default: (let width_em = parse_dimension(node), mspace_box(width_em))
    }
}

fn mspace_box(width_em) => {
    element: <span class: "lm_mspace", style: "margin-left:" ++ fmt_dim(width_em) ++ "em">,
    height: 0.0,
    depth: 0.0,
    width: width_em,
    type: "skip",
    italic: 0.0,
    skew: 0.0,
    suppress_hbox_text_depth: true
}

// ============================================================
// Dimension parsing
// ============================================================

// parse a dimension from an hspace/kern node
// node may have: sign, value, unit attributes
fn parse_dimension(node) {
    let raw = if (node.value != null) string(node.value) else ""
    let dim = dimension_from_string(raw)
    let val = dim.value
    let sign_mul = dim.sign
    let unit = dim.unit

    let val_em = if (unit == "em") val
        else if (unit == "ex") val * 0.431
        else if (unit == "pt") val / 10.0
        else if (unit == "pc") val * 12.0 / 10.0
        else if (unit == "mu") val / 18.0
        else if (unit == "cm") val * 28.452756 / 10.0
        else if (unit == "mm") val * 2.8452756 / 10.0
        else if (unit == "in") val * 72.27 / 10.0
        else val

    sign_mul * val_em
}

fn dimension_from_string(raw) {
    let start = find_number_start(raw, 0)
    if (start >= len(raw)) {
        {value: 0.0, sign: 1.0, unit: "em"}
    } else {
        let end = find_number_end(raw, start)
        let unit_end = find_unit_end(raw, end)
        let num_text = slice(raw, start, end)
        let unit_text = if (unit_end > end) slice(raw, end, unit_end) else "em"
        let sign = if (len(num_text) > 0 and slice(num_text, 0, 1) == "-") -1.0 else 1.0
        let abs_start = if (len(num_text) > 0 and (slice(num_text, 0, 1) == "-" or slice(num_text, 0, 1) == "+")) 1 else 0
        let abs_text = slice(num_text, abs_start, len(num_text))
        {value: float(abs_text), sign: sign, unit: unit_text}
    }
}

fn find_number_start(s, i) {
    if (i >= len(s)) i
    else if (is_number_start_char(slice(s, i, i + 1))) i
    else find_number_start(s, i + 1)
}

fn find_number_end(s, i) {
    if (i >= len(s)) i
    else if (is_number_char(slice(s, i, i + 1))) find_number_end(s, i + 1)
    else i
}

fn find_unit_end(s, i) {
    if (i >= len(s)) i
    else if (is_unit_char(slice(s, i, i + 1))) find_unit_end(s, i + 1)
    else i
}

fn is_number_start_char(ch) {
    ch == "-" or ch == "+" or ch == "." or is_digit_char(ch)
}

fn is_number_char(ch) {
    ch == "-" or ch == "+" or ch == "." or is_digit_char(ch)
}

fn is_digit_char(ch) {
    ch == "0" or ch == "1" or ch == "2" or ch == "3" or ch == "4" or
    ch == "5" or ch == "6" or ch == "7" or ch == "8" or ch == "9"
}

fn is_unit_char(ch) {
    ch == "a" or ch == "b" or ch == "c" or ch == "d" or ch == "e" or
    ch == "f" or ch == "g" or ch == "h" or ch == "i" or ch == "j" or
    ch == "k" or ch == "l" or ch == "m" or ch == "n" or ch == "o" or
    ch == "p" or ch == "q" or ch == "r" or ch == "s" or ch == "t" or
    ch == "u" or ch == "v" or ch == "w" or ch == "x" or ch == "y" or
    ch == "z"
}

fn fmt_dim(v) {
    let rounded = round(v * 100.0) / 100.0
    string(rounded)
}
