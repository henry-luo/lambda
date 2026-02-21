// math/atoms/spacing.ls — Spacing commands (\quad, \qquad, \, \: \; \! \hspace \kern)

import box: .lambda.package.math.box
import css: .lambda.package.math.css

// ============================================================
// Spacing command rendering
// ============================================================

pub fn render(node, context, render_fn) {
    let cmd = if (node.cmd != null) string(node.cmd) else ""

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
        default: (let width_em = parse_dimension(node), box.skip_box(width_em))
    }
}

// ============================================================
// Dimension parsing
// ============================================================

// parse a dimension from an hspace/kern node
// node may have: sign, value, unit attributes
fn parse_dimension(node) {
    let val = if (node.value != null) float(string(node.value)) else 0.0
    let sign_mul = if (node.sign != null and string(node.sign) == "-") -1.0 else 1.0
    let unit = if (node.unit != null) string(node.unit) else "em"

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
