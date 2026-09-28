// Text and math labels share one positioned HTML representation.
import math: lambda.doc.math.math

pub fn prepare(source) map^ {
    let value = trim(source)
    let textcolor_prefix = "\\textcolor{"
    if (starts_with(value, textcolor_prefix)) {
        let color_start = len(textcolor_prefix)
        let color_end = index_of(slice(value, color_start, len(value)), "}")
        let body_open = if (color_end == null) null else color_start + color_end + 1
        let valid = body_open != null and slice(value, body_open, body_open + 1) == "{" and
            ends_with(value, "}")
        if (not valid) raise error("malformed TikZ textcolor label")
        else {
            let color = slice(value, color_start, body_open - 1)
            let body = slice(value, body_open + 1, len(value) - 1)
            let supported_color = len([for (candidate in ["black", "blue", "red", "green",
                "darkgreen", "orange", "purple", "gray"] where candidate == color) candidate]) > 0
            if (not supported_color) raise error("unsupported TikZ textcolor: " ++ color)
            else {
                let prepared = prepare(body)^;
                {element: <span style: "color:" ++ color, prepared.element>,
                    width_em: prepared.width_em, height_em: prepared.height_em,
                    depth_em: prepared.depth_em}
            }
        }
    } else if (starts_with(value, "$") and ends_with(value, "$") and len(value) >= 2) {
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

pub fn plain_title(source) string^ {
    // PGFPlots title font declarations affect style, not the displayed words.
    let text = replace(replace(trim(source), "\\large", ""), "\\bfseries", "")
    if (contains(text, "\\")) raise error("unsupported PGFPlots title command")
    else replace(replace(text, "{", ""), "}", "")
}
