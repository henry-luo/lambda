// pdf/font.ls — PDF font handling
//
// Phase 2 scope:
//   - Map the PDF Standard 14 base fonts to web font-family stacks +
//     style/weight metadata so the SVG/HTML output looks broadly correct
//     without embedding the original font program.
//   - Provide a `resolve_font(pdf, page, name)` helper that returns a
//     normalised record:
//         { name, family, weight, style, size_default, to_unicode }
//     where `to_unicode` is the per-glyph CMap already resolved by the C
//     side (or null when not present).
//   - Provide `decode_hex(hex_str, to_unicode)` for `<XX..>` strings used
//     by composite (CID) fonts.

import resolve: lambda.package.pdf.resolve

// ============================================================
// Standard 14 mapping
// ============================================================
//
// Each entry: family stack, weight ("normal"/"bold"), style
// ("normal"/"italic").  PDF font names are case-sensitive.

// helvetica family
let _HELVETICA          = { family: "Helvetica, Arial, sans-serif",         weight: "normal", style: "normal" }
let _HELVETICA_BOLD     = { family: "Helvetica, Arial, sans-serif",         weight: "bold",   style: "normal" }
let _HELVETICA_OBLIQUE  = { family: "Helvetica, Arial, sans-serif",         weight: "normal", style: "italic" }
let _HELVETICA_BOBLQ    = { family: "Helvetica, Arial, sans-serif",         weight: "bold",   style: "italic" }

// times family
let _TIMES_ROMAN        = { family: "Times, 'Times New Roman', serif",      weight: "normal", style: "normal" }
let _TIMES_BOLD         = { family: "Times, 'Times New Roman', serif",      weight: "bold",   style: "normal" }
let _TIMES_ITALIC       = { family: "Times, 'Times New Roman', serif",      weight: "normal", style: "italic" }
let _TIMES_BOLDITALIC   = { family: "Times, 'Times New Roman', serif",      weight: "bold",   style: "italic" }

// courier family
let _COURIER            = { family: "Courier, 'Courier New', monospace",    weight: "normal", style: "normal" }
let _COURIER_BOLD       = { family: "Courier, 'Courier New', monospace",    weight: "bold",   style: "normal" }
let _COURIER_OBLIQUE    = { family: "Courier, 'Courier New', monospace",    weight: "normal", style: "italic" }
let _COURIER_BOBLQ      = { family: "Courier, 'Courier New', monospace",    weight: "bold",   style: "italic" }

// symbol fonts (no good web equivalent — fall back to serif and let the
// to_unicode CMap do the heavy lifting)
let _SYMBOL             = { family: "'Symbol', serif",                      weight: "normal", style: "normal" }
let _ZAPFDINGBATS       = { family: "'Zapf Dingbats', serif",               weight: "normal", style: "normal" }

// generic fallback
let _UNKNOWN            = { family: "sans-serif",                           weight: "normal", style: "normal" }

// Look up a Standard 14 base font by exact name. Returns _UNKNOWN when the
// name isn't recognised (callers should still apply BaseFont heuristics).
pub fn standard14(name: string) {
    if (name == "Helvetica")              { _HELVETICA }
    else if (name == "Helvetica-Bold")    { _HELVETICA_BOLD }
    else if (name == "Helvetica-Oblique") { _HELVETICA_OBLIQUE }
    else if (name == "Helvetica-BoldOblique") { _HELVETICA_BOBLQ }
    else if (name == "Times-Roman")       { _TIMES_ROMAN }
    else if (name == "Times-Bold")        { _TIMES_BOLD }
    else if (name == "Times-Italic")      { _TIMES_ITALIC }
    else if (name == "Times-BoldItalic")  { _TIMES_BOLDITALIC }
    else if (name == "Courier")           { _COURIER }
    else if (name == "Courier-Bold")      { _COURIER_BOLD }
    else if (name == "Courier-Oblique")   { _COURIER_OBLIQUE }
    else if (name == "Courier-BoldOblique") { _COURIER_BOBLQ }
    else if (name == "Symbol")            { _SYMBOL }
    else if (name == "ZapfDingbats")      { _ZAPFDINGBATS }
    else                                  { _UNKNOWN }
}

// ============================================================
// BaseFont heuristics
// ============================================================

// Substring helpers (Lambda has no built-in `contains`).

pn _starts_at(s: string, needle: string, i: int) {
    let m = len(needle)
    var k = 0
    while (k < m) {
        if (s[i + k] != needle[k]) { return false }
        k = k + 1
    }
    return true
}

pn _contains(s: string, needle: string) {
    let n = len(s)
    let m = len(needle)
    if (m == 0) { return true }
    if (n < m)  { return false }
    var i = 0
    let limit = n - m
    while (i <= limit) {
        if (_starts_at(s, needle, i)) { return true }
        i = i + 1
    }
    return false
}

// Infer family/weight/style from arbitrary BaseFont name (e.g.
// "ABCDEF+TimesNewRomanPSMT-Bold"). The leading "ABCDEF+" subset prefix
// is ignored.
pub pn from_basefont(name: string) {
    // strip subset prefix "XXXXXX+"
    var stripped = name
    if (len(name) > 7 and name[6] == "+") {
        var t = ""
        var k = 7
        while (k < len(name)) { t = t ++ name[k]; k = k + 1 }
        stripped = t
    }

    // exact Standard-14 hit?
    let s14 = standard14(stripped)
    if (s14 != _UNKNOWN) { return s14 }

    let is_bold = _contains(stripped, "Bold") or _contains(stripped, "bold")
    let is_ital = _contains(stripped, "Italic") or _contains(stripped, "italic") or
                   _contains(stripped, "Oblique") or _contains(stripped, "oblique")
    let weight = if (is_bold) { "bold" } else { "normal" }
    let style  = if (is_ital) { "italic" } else { "normal" }
    var family = "Helvetica, Arial, sans-serif"
    if (_contains(stripped, "Times") or _contains(stripped, "times") or
        _contains(stripped, "Serif") or _contains(stripped, "serif")) {
        family = "Times, 'Times New Roman', serif"
    }
    else if (_contains(stripped, "Courier") or _contains(stripped, "courier") or
             _contains(stripped, "Mono") or _contains(stripped, "mono")) {
        family = "Courier, 'Courier New', monospace"
    }
    return { family: family, weight: weight, style: style }
}

// ============================================================
// Public resolver
// ============================================================

// Look up the font referenced by `name` on `page`'s resource dict and
// produce a normalised descriptor. Returns _UNKNOWN with no to_unicode
// when the font isn't declared.
pub pn resolve_font(pdf, page, name: string) {
    let dict = resolve.page_font(pdf, page, name)
    if (dict == null) {
        return { name: name, family: _UNKNOWN.family, weight: _UNKNOWN.weight,
                 style: _UNKNOWN.style, to_unicode: null }
    }
    let basefont = if (dict.BaseFont) string(dict.BaseFont) else name
    let info = from_basefont(basefont)
    let to_uni = if (dict.to_unicode) dict.to_unicode else null
    return { name: name, family: info.family, weight: info.weight,
             style: info.style, to_unicode: to_uni }
}

// ============================================================
// Hex string decoding via to_unicode CMap
// ============================================================
//
// Hex strings in Tj/TJ are pairs of nibbles, e.g. `<0048>` → glyph
// IDs/codes 0x00, 0x48 (depending on font CMap width). For Phase 2 we
// support 2-hex-digit single-byte codes only — composite fonts with
// 4-digit CIDs need multi-byte CMap traversal which we'll add later.

fn _hex_val(c: string) {
    let k = ord(c)
    if ((k >= 48) and (k <= 57))     { k - 48 }
    else if ((k >= 65) and (k <= 70)) { k - 55 }
    else if ((k >= 97) and (k <= 102)) { k - 87 }
    else { 0 }
}

// Convert a hex string (without delimiters) to a list of byte codes.
fn _hex_to_codes(hex: string) {
    let n = len(hex)
    let pairs = (n / 2)
    if (pairs == 0) { [] }
    else {
        for (k in 0 to (pairs - 1)) (_hex_val(hex[k * 2]) * 16) + _hex_val(hex[k * 2 + 1])
    }
}

// Decode a single byte code through a to_unicode CMap. The C side
// produces a Map keyed by stringified hex code → unicode string (e.g.
// `{ "0048": "H" }`). When the lookup misses we return the literal byte
// as a 1-char string (best-effort fallback).
fn _decode_code(code, cmap) {
    let hi = code / 16
    let lo = code % 16
    let hex_chars = "0123456789ABCDEF"
    let key = hex_chars[hi] ++ hex_chars[lo]
    if (cmap and cmap[key]) { cmap[key] }
    else { chr(code) }
}

// Decode a Tj/TJ hex operand to a unicode string.
pub fn decode_hex(hex: string, to_unicode) {
    let codes = _hex_to_codes(hex)
    let parts = (for (c in codes) _decode_code(c, to_unicode))
    parts | join("")
}

// Decode a literal-string Tj operand. Without a CMap PDF literal strings
// are interpreted as PDFDocEncoding (basically Latin-1 for our purposes);
// with a CMap each byte is looked up just like hex.
pub fn decode_literal(s: string, to_unicode) {
    if (to_unicode == null) { s }
    else {
        let n = len(s)
        let parts = (for (i in 0 to (n - 1)) _decode_code(ord(s[i]), to_unicode))
        parts | join("")
    }
}
