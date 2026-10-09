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
