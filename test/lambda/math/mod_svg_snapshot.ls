// Retain MIME type, font declarations and geometry; resource tests verify actual bytes.
type FontSource = \("src:url(data:" ("\a" | "\d" | "/" | "-" | "." | "+")+ ";base64," ("\a" | "\d" | "+" | "/" | "=")+ ")")
pub fn normalize(html) => join([for (part in split(html, FontSource, true))
    if (part is FontSource) slice(part, 0, index_of(part, ";base64,") + 8) ++ "FONT_PAYLOAD)"
    else part], "")
