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

// Build a CSS font-family stack: actual font name first (so the OS
// can use the real font when installed) + generic-family fallback.
// `gen` is one of "serif", "sans-serif", "monospace".
// When `condensed` is true, prepend platform condensed fallbacks so the
// SVG renderer can pick a narrow face (PDFs often use *Condensed/Narrow
// fonts whose embedded subset has no Unicode cmap; without narrow
// fallbacks, glyphs render too wide and overlap their PDF-positioned x).
fn _family_stack(name: string, gen: string) {
    "'" ++ name ++ "', " ++ gen
}

fn _family_stack_with_stretch(name: string, gen: string, condensed: bool) {
    if (condensed and gen == "sans-serif") {
        "'" ++ name ++ "', 'Arial Narrow', 'Helvetica Neue Condensed', " ++
        "'Avenir Next Condensed', 'Roboto Condensed', sans-serif"
    }
    else if (condensed and gen == "serif") {
        "'" ++ name ++ "', 'PT Serif Caption', 'Times New Roman', serif"
    }
    else { _family_stack(name, gen) }
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
    // Generic-family inference from common name fragments. Anything not
    // matched falls back to sans-serif (PDF gives us no flag here).
    var gen = "sans-serif"
    if (_contains(stripped, "Times") or _contains(stripped, "times") or
        _contains(stripped, "Serif") or _contains(stripped, "serif") or
        _contains(stripped, "Georgia") or _contains(stripped, "georgia") or
        _contains(stripped, "Cambria") or _contains(stripped, "cambria") or
        _contains(stripped, "Gelasio") or _contains(stripped, "gelasio") or
        _contains(stripped, "Garamond") or _contains(stripped, "garamond") or
        _contains(stripped, "Palatino") or _contains(stripped, "palatino") or
        _contains(stripped, "Book") or _contains(stripped, "Minion")) {
        gen = "serif"
    }
    else if (_contains(stripped, "Courier") or _contains(stripped, "courier") or
             _contains(stripped, "Mono") or _contains(stripped, "mono") or
             _contains(stripped, "Consolas") or _contains(stripped, "consolas")) {
        gen = "monospace"
    }
    let is_condensed = _contains(stripped, "Condensed") or _contains(stripped, "condensed") or
                       _contains(stripped, "Narrow")    or _contains(stripped, "narrow") or
                       _contains(stripped, "Compressed")
    return { family: _family_stack_with_stretch(stripped, gen, is_condensed),
             weight: weight, style: style }
}

// ============================================================
// Public resolver
// ============================================================

// Helper: extract BaseFont string with `let x = if` lifted out of pn
// (vibe/Lambda_Issues5.md #15 — let-bound `if` returns null in pn).
fn _basefont_or(name: string, dict) {
    if (dict.BaseFont) string(dict.BaseFont) else name
}

fn _to_unicode_or_null(dict) {
    if (dict.to_unicode) dict.to_unicode else null
}

// Strip a leading "XXXXXX+" subset prefix from a BaseFont name. Done as
// `fn` so it can be called from `resolve_font` (which avoids nested pn
// dispatch — see the note above resolve_font).
fn _strip_subset(name: string) {
    if (len(name) > 7 and name[6] == "+") {
        let parts = (for (k in 7 to (len(name) - 1)) name[k])
        parts | join("")
    }
    else { name }
}

// Build the descriptor record. Done in `fn` so map-spread + field access
// behave deterministically; calling pn helpers (from_basefont) from
// inside `resolve_font` proved to silently lose the weight/style fields
// when chained through let-bindings.
fn _make_descriptor(name, info, to_uni) {
    { name: name, family: info.family, weight: info.weight,
      style: info.style, to_unicode: to_uni,
      font_data_uri: null, font_format: null, embedded_family: null }
}

fn _make_descriptor_embedded(name, info, to_uni, uri, fmt, emb_family) {
    { name: name, family: info.family, weight: info.weight,
      style: info.style, to_unicode: to_uni,
      font_data_uri: uri, font_format: fmt, embedded_family: emb_family }
}

fn _unknown_descriptor(name) {
    { name: name, family: _UNKNOWN.family, weight: _UNKNOWN.weight,
      style: _UNKNOWN.style, to_unicode: null,
      font_data_uri: null, font_format: null, embedded_family: null }
}

// Look up the font referenced by `name` on `page`'s resource dict and
// produce a normalised descriptor. Returns _UNKNOWN with no to_unicode
// when the font isn't declared.
// Pick the descriptor: prefer Standard-14 (fn-only path); fall back to
// the heuristic only when needed. Lifted to `fn` because `let x = if`
// returns null inside `pn` (vibe/Lambda_Issues5.md #15).
fn _pick_info(s14_info, fallback_info) {
    if (s14_info != _UNKNOWN) s14_info else fallback_info
}

// Re-derive the CSS family stack using FontDescriptor.Flags when the
// name-based heuristic was inconclusive. PDF Flags (Table 123):
//   bit 1 (val 1)  = FixedPitch  → monospace
//   bit 2 (val 2)  = Serif       → serif
// We only override when the existing `info.family` ends in "sans-serif"
// (the heuristic's catch-all default), so an explicit name-based serif
// match is never downgraded.
fn _has_flag(flags, mask) {
    // Lambda has no bitwise ops in the path used here; integer
    // arithmetic via mod/div is sufficient for single-bit checks.
    let q = flags / mask
    (q - ((q / 2) * 2)) == 1
}

fn _ends_with(s: string, suffix: string) {
    let n = len(s); let m = len(suffix)
    if (n < m) { false }
    else {
        // compare last m chars; AND-reduce equality across the range
        let hits = (for (i in 0 to (m - 1)) (s[n - m + i] == suffix[i]))
        let bad  = (for (b in hits where b == false) b)
        len(bad) == 0
    }
}

fn _override_family_from_flags(pdf, stripped: string, dict, info) {
    let fd = if (dict.FontDescriptor) resolve.deref(pdf, dict.FontDescriptor) else null
    if (fd == null or fd.Flags == null) { info }
    else {
        let flags = if (fd.Flags is int) fd.Flags else 0
        let is_mono  = _has_flag(flags, 1)
        let is_serif = _has_flag(flags, 2)
        let tail_sans = _ends_with(info.family, "sans-serif")
        if (is_mono and tail_sans) {
            { family: _family_stack(stripped, "monospace"),
              weight: info.weight, style: info.style }
        }
        else if (is_serif and tail_sans) {
            { family: _family_stack(stripped, "serif"),
              weight: info.weight, style: info.style }
        }
        else { info }
    }
}

// Wraps the pick + flags-override into a single fn so it can be called
// from `resolve_font` (pn) without hitting the `let x = if` → null
// gotcha (vibe/Lambda_Issues5.md #15).
fn _final_info(pdf, stripped, dict, s14, fb) {
    let info_pre = _pick_info(s14, fb)
    if (s14 == _UNKNOWN) _override_family_from_flags(pdf, stripped, dict, info_pre)
    else info_pre
}

// Look up the embedded font program info attached by the C-side
// post-processor (input-pdf-postprocess.cpp `encode_embedded_fonts`).
// Returns { uri, fmt } or null.
fn _embedded_font_info(pdf, dict) {
    let fd = if (dict.FontDescriptor) resolve.deref(pdf, dict.FontDescriptor) else null
    if (fd == null) { null }
    else if (fd.font_data_uri == null) { null }
    else { { uri: fd.font_data_uri, fmt: fd.font_format } }
}

// When the font program is embedded, ensure the (subset-stripped)
// basefont name appears exactly once at the head of the CSS family
// stack as a quoted family. This is the CSS family name we'll declare
// in @font-face below, so the browser matches and uses our extracted
// glyphs instead of OS fallback. `from_basefont` already prepends the
// same quoted name on non-Standard-14 inputs, so we strip any leading
// `'name', ` prefix before re-adding to avoid `'X', 'X', serif` dupes.
fn _strip_quoted_prefix(family: string, name: string) {
    let prefix = "'" ++ name ++ "', "
    let plen = len(prefix)
    let flen = len(family)
    if (flen <= plen) { family }
    else {
        // build the leading slice as a string and compare
        let head = (for (i in 0 to (plen - 1)) family[i]) | join("")
        if (head == prefix) {
            (for (i in plen to (flen - 1)) family[i]) | join("")
        }
        else { family }
    }
}

fn _info_with_embedded(info, emb_family) {
    let tail = _strip_quoted_prefix(info.family, emb_family)
    { family: "'" ++ emb_family ++ "', " ++ tail,
      weight: info.weight, style: info.style }
}

pub pn resolve_font(pdf, page, name: string) {
    let dict = resolve.page_font(pdf, page, name)
    if (dict == null) { return _unknown_descriptor(name) }
    let basefont = _basefont_or(name, dict)
    // NOTE: calling from_basefont (pn) from within resolve_font (pn)
    // returned a stale/default record (Lambda nested-pn corruption);
    // workaround: try standard14 (fn) first, only fall back to the
    // pn heuristic when needed.
    let stripped = _strip_subset(basefont)
    let s14 = standard14(stripped)
    let fb = from_basefont(basefont)
    // FontDescriptor.Flags can override the name-based generic-family
    // guess (bit 1 = FixedPitch, bit 2 = Serif). Only applied when the
    // basefont is non-Standard-14 (s14 hit means we already trust the
    // canonical metrics).
    let info0 = _final_info(pdf, stripped, dict, s14, fb)
    let to_uni = _to_unicode_or_null(dict)
    // Embedded font program?  Pull the data URI off the FontDescriptor
    // and weave the unsubsetted family name into the CSS stack so the
    // browser matches our @font-face declaration.
    let emb = _embedded_font_info(pdf, dict)
    if (emb == null) {
        return _make_descriptor(name, info0, to_uni)
    }
    let info1 = _info_with_embedded(info0, stripped)
    return _make_descriptor_embedded(name, info1, to_uni,
                                     emb.uri, emb.fmt, stripped)
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

// Decode a single byte code through a to_unicode CMap. The C-side
// post-processor (input-pdf-postprocess.cpp `add_unicode_mapping`)
// stores keys via `snprintf("%u", cid)` which Lambda surfaces as
// STRING map keys ("1", "2", ...). The Lambda printer renders those
// keys without quotes when the string parses as an integer, which
// previously misled this code into using integer indexing — the
// lookup always missed and bytes fell through to chr(), producing
// gibberish text.
//
// When the lookup misses we return the literal byte as a 1-char
// string (best-effort fallback).
fn _decode_code(code, cmap) {
    let key = string(code)
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
