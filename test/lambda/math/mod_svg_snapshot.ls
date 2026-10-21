// Retain font declarations and geometry; resource tests verify URLs and custom bytes.
import util: lambda.doc.math.util
import font_files: lambda.doc.font

type FontSource = \("src:url(data:" ("\a" | "\d" | "/" | "-" | "." | "+")+ ";base64," ("\a" | "\d" | "+" | "/" | "=")+ ")")
// retained delimiters occupy odd slots (S17.1.1); do not rematch large font data.
pub fn normalize(html) {
    let quoted = format(font_files.resource_path(""), 'json')^
    let relative = replace(html, slice(quoted, 1, len(quoted) - 1), "BUNDLED_PACKAGE/");
    join([for (i, part in split(relative, FontSource, true))
        if (i % 2 == 1) slice(part, 0, index_of(part, ";base64,") + 8) ++ "FONT_PAYLOAD)"
        else part], "")
}

// inspect painted glyphs without counting accessible titles or embedded font data.
pub fn painted_text(node, ignore_kind = null) {
    if (not (node is element)) ""
    else if (ignore_kind != null and node["data-math-kind"] == ignore_kind) ""
    else if (name(node) == 'text') util.text_of(node)
    else if (name(node) == 'g' or name(node) == 'svg') util.children_text(node, (child) => painted_text(child,ignore_kind))
    else ""
}

pub fn nodes(node, tag) => if (not (node is element)) [] else
    [*if (name(node) == tag) [node] else [], *[for (child in content(node), found in nodes(child,tag)) found]]

// Resolve the renderer's translate/scale groups so tests inspect painted baselines.
pub fn geometry(node, x = 0.0, y = 0.0, sx = 1.0, sy = 1.0) {
    if (not (node is element)) []
    else {
        let transform = string(node.transform or "")
        let values = if (transform == "") [] else [for (part in split(slice(transform, index_of(transform, "(") or 0, len(transform)), " "))
            float(replace(replace(part, "(", ""), ")", ""))]
        let tx = if (starts_with(transform, "translate(")) x + sx * values[0] else x
        let ty = if (starts_with(transform, "translate(")) y + sy * (values[1] or 0.0) else y
        let ax = if (starts_with(transform, "scale(")) sx * values[0] else sx
        let ay = if (starts_with(transform, "scale(")) sy * (values[1] or values[0]) else sy;
        [*if (name(node) == 'text' or name(node) == 'rect' or name(node) == 'path' or name(node) == 'image')
            [{node:node, x:tx + ax * (node.x or 0.0), y:ty + ay * (node.y or 0.0), sx:ax, sy:ay,
                text:if (name(node) == 'text') util.text_of(node) else ""}] else [],
         *[for (child in content(node), found in geometry(child, tx, ty, ax, ay)) found]]
    }
}
