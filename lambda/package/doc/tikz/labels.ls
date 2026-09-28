// Text and math labels share one positioned HTML representation.
import math: lambda.doc.math.math

pub fn prepare(source) map^ {
    let value = trim(source)
    if (starts_with(value, "$") and ends_with(value, "$") and len(value) >= 2) {
        let ast = parse(slice(value, 1, len(value) - 1), {type: "math"})^
        let measured = math.render_box(ast, {display: false})
        {element: math.render_box_element(measured),
            width_em: measured.width, height_em: measured.height,
            depth_em: measured.depth}
    } else {
        // CSS measures ordinary text at paint time; the geometry caller must
        // reserve space independently before it admits long labels.
        {element: <span value>, width_em: null, height_em: null, depth_em: null}
    }
}

pub fn positioned(prepared, x, y, extra_style = "") {
    let style = "position:absolute;left:" ++ string(x) ++ "px;top:" ++ string(y) ++
        "px;white-space:nowrap;transform:translate(-50%,-50%);" ++ extra_style;
    <span class: "tikz-label", style: style, prepared.element>
}
