// Retain MIME type, font declarations and geometry; resource tests verify actual bytes.
import util: lambda.doc.math.util

type FontSource = \("src:url(data:" ("\a" | "\d" | "/" | "-" | "." | "+")+ ";base64," ("\a" | "\d" | "+" | "/" | "=")+ ")")
pub fn normalize(html) => join([for (part in split(html, FontSource, true))
    if (part is FontSource) slice(part, 0, index_of(part, ";base64,") + 8) ++ "FONT_PAYLOAD)"
    else part], "")

// inspect painted glyphs without counting accessible titles or embedded font data.
pub fn painted_text(node) {
    if (not (node is element)) ""
    else if (name(node) == 'text') util.text_of(node)
    else if (name(node) == 'g' or name(node) == 'svg') util.children_text(node, painted_text)
    else ""
}

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
        [*if (name(node) == 'text' or name(node) == 'rect' or name(node) == 'path')
            [{node:node, x:tx + ax * (node.x or 0.0), y:ty + ay * (node.y or 0.0), sx:ax, sy:ay,
                text:if (name(node) == 'text') util.text_of(node) else ""}] else [],
         *[for (child in content(node), found in geometry(child, tx, ty, ax, ay)) found]]
    }
}
