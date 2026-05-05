// pdf/font.ls — PDF font handling
//
// Phase 2 scope:
//   - Map the PDF Standard 14 base fonts to web font-family stacks +
//     style/weight metadata so the SVG/HTML output looks broadly correct
//     without embedding the original font program.
//   - Provide a `resolve_font(pdf, page, name)` helper that returns a
//     normalised record:
//         { name, family, weight, style, size_default, to_unicode, encoding,
//           widths, first_char, last_char }
//     where `to_unicode` is the per-glyph CMap already resolved by the C
//     side (or null when not present).
//   - Provide `decode_hex(hex_str, to_unicode)` for `<XX..>` strings used
//     by composite (CID) fonts.

import resolve: lambda.package.pdf.resolve
import util:    lambda.package.pdf.util

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
    let stripped = _strip_subset(name)

    // exact Standard-14 hit?
    let s14 = standard14(stripped)
    if (s14.family != _UNKNOWN.family) { return s14 }

    let is_bold = _contains(stripped, "Bold") or _contains(stripped, "bold") or
                   _contains(stripped, "_700wght") or _contains(stripped, "_800wght") or
                   _contains(stripped, "_900wght") or _contains(stripped, "Black") or
                   _contains(stripped, "Heavy")
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

fn _encoding_or_null(dict) {
    if (dict.Encoding is string) { dict.Encoding }
    else { null }
}

fn _widths_or_null(pdf, dict) {
    if (dict.Widths == null) { null }
    else {
        let w = resolve.deref(pdf, dict.Widths)
        if (w is array) { w } else { null }
    }
}

fn _first_char_or_zero(dict) {
    util.int_or(dict.FirstChar, 0)
}

fn _last_char_or_zero(dict) {
    util.int_or(dict.LastChar, 0)
}

fn _implicit_encoding(stripped: string, explicit) {
    if (explicit != null) { explicit }
    else if (stripped == "Symbol") { "SymbolEncoding" }
    else if (stripped == "ZapfDingbats") { "ZapfDingbatsEncoding" }
    else { null }
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
fn _make_descriptor(name, info, to_uni, enc, widths, first_char, last_char) {
    { name: name, family: info.family, weight: info.weight,
        style: info.style, to_unicode: to_uni, encoding: enc,
    widths: widths, first_char: first_char, last_char: last_char,
      font_data_uri: null, font_format: null, embedded_family: null }
}

fn _make_descriptor_embedded(name, info, to_uni, enc, widths, first_char, last_char, uri, fmt, emb_family) {
    { name: name, family: info.family, weight: info.weight,
        style: info.style, to_unicode: to_uni, encoding: enc,
    widths: widths, first_char: first_char, last_char: last_char,
      font_data_uri: uri, font_format: fmt, embedded_family: emb_family }
}

fn _unknown_descriptor(name) {
    { name: name, family: _UNKNOWN.family, weight: _UNKNOWN.weight,
        style: _UNKNOWN.style, to_unicode: null, encoding: null,
    widths: null, first_char: 0, last_char: 0,
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
    let enc = _implicit_encoding(stripped, _encoding_or_null(dict))
    let widths = _widths_or_null(pdf, dict)
    let first_char = _first_char_or_zero(dict)
    let last_char = _last_char_or_zero(dict)
    // Embedded font program?  Pull the data URI off the FontDescriptor
    // and weave the unsubsetted family name into the CSS stack so the
    // browser matches our @font-face declaration.
    let emb = _embedded_font_info(pdf, dict)
    if (emb == null) {
        return _make_descriptor(name, info0, to_uni, enc, widths, first_char, last_char)
    }
    let info1 = _info_with_embedded(info0, stripped)
    return _make_descriptor_embedded(name, info1, to_uni, enc, widths, first_char, last_char,
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

// Convert a hex string (without delimiters) to a list of byte codes.
fn _hex_to_codes(hex: string) {
    let clean = util.clean_hex(hex)
    let n = len(clean)
    let pairs = (n + 1) / 2
    if (pairs == 0) { [] }
    else {
        for (k in 0 to (pairs - 1)) util.hex_byte_at(clean, k * 2)
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
fn _winansi_special(code) {
    if (code == 128) { chr(8364) }
    else if (code == 130) { chr(8218) }
    else if (code == 131) { chr(402) }
    else if (code == 132) { chr(8222) }
    else if (code == 133) { chr(8230) }
    else if (code == 134) { chr(8224) }
    else if (code == 135) { chr(8225) }
    else if (code == 136) { chr(710) }
    else if (code == 137) { chr(8240) }
    else if (code == 138) { chr(352) }
    else if (code == 139) { chr(8249) }
    else if (code == 140) { chr(338) }
    else if (code == 142) { chr(381) }
    else if (code == 145) { chr(8216) }
    else if (code == 146) { chr(8217) }
    else if (code == 147) { chr(8220) }
    else if (code == 148) { chr(8221) }
    else if (code == 149) { chr(8226) }
    else if (code == 150) { chr(8211) }
    else if (code == 151) { chr(8212) }
    else if (code == 152) { chr(732) }
    else if (code == 153) { chr(8482) }
    else if (code == 154) { chr(353) }
    else if (code == 155) { chr(8250) }
    else if (code == 156) { chr(339) }
    else if (code == 158) { chr(382) }
    else if (code == 159) { chr(376) }
    else { null }
}

let _MAC_ROMAN_UPPER = [
    196, 197, 199, 201, 209, 214, 220, 225,
    224, 226, 228, 227, 229, 231, 233, 232,
    234, 235, 237, 236, 238, 239, 241, 243,
    242, 244, 246, 245, 250, 249, 251, 252,
    8224, 176, 162, 163, 167, 8226, 182, 223,
    174, 169, 8482, 180, 168, 8800, 198, 216,
    8734, 177, 8804, 8805, 165, 181, 8706, 8721,
    8719, 960, 8747, 170, 186, 937, 230, 248,
    191, 161, 172, 8730, 402, 8776, 8710, 171,
    187, 8230, 160, 192, 195, 213, 338, 339,
    8211, 8212, 8220, 8221, 8216, 8217, 247, 9674,
    255, 376, 8260, 8364, 8249, 8250, 64257, 64258,
    8225, 183, 8218, 8222, 8240, 194, 202, 193,
    203, 200, 205, 206, 207, 204, 211, 212,
    63743, 210, 218, 219, 217, 305, 710, 732,
    175, 728, 729, 730, 184, 733, 731, 711
]

let _SYMBOL_MAP = map([
    "34", 8704, "36", 8707, "39", 8715, "42", 8727, "45", 8722,
    "64", 8773, "65", 913, "66", 914, "67", 935, "68", 916,
    "69", 917, "70", 934, "71", 915, "72", 919, "73", 921,
    "74", 977, "75", 922, "76", 923, "77", 924, "78", 925,
    "79", 927, "80", 928, "81", 920, "82", 929, "83", 931,
    "84", 932, "85", 933, "86", 962, "87", 937, "88", 926,
    "89", 936, "90", 918, "92", 8756, "94", 8869,
    "96", 175, "97", 945, "98", 946, "99", 967, "100", 948,
    "101", 949, "102", 966, "103", 947, "104", 951, "105", 953,
    "106", 981, "107", 954, "108", 955, "109", 956, "110", 957,
    "111", 959, "112", 960, "113", 952, "114", 961, "115", 963,
    "116", 964, "117", 965, "118", 982, "119", 969, "120", 958,
    "121", 968, "122", 950, "126", 8764,
    "160", 8364, "161", 978, "162", 8242, "163", 8804, "164", 8260,
    "165", 8734, "166", 402, "167", 9827, "168", 9830, "169", 9829,
    "170", 9824, "171", 8596, "172", 8592, "173", 8593, "174", 8594,
    "175", 8595, "176", 176, "177", 177, "178", 8243, "179", 8805,
    "180", 215, "181", 8733, "182", 8706, "183", 8226, "184", 247,
    "185", 8800, "186", 8801, "187", 8776, "188", 8230, "189", 9168,
    "190", 9135, "191", 8629, "192", 8501, "193", 8465, "194", 8476,
    "195", 8472, "196", 8855, "197", 8853, "198", 8709, "199", 8745,
    "200", 8746, "201", 8835, "202", 8839, "203", 8836, "204", 8834,
    "205", 8838, "206", 8712, "207", 8713, "208", 8736, "209", 8711,
    "210", 174, "211", 169, "212", 8482, "213", 8719, "214", 8730,
    "215", 8901, "216", 172, "217", 8743, "218", 8744, "219", 8660,
    "220", 8656, "221", 8657, "222", 8658, "223", 8659, "224", 9674,
    "225", 9001, "226", 174, "227", 169, "228", 8482, "229", 8721,
    "230", 9115, "231", 9116, "232", 9117, "233", 9121, "234", 9122,
    "235", 9123, "236", 9127, "237", 9128, "238", 9129, "239", 9130,
    "241", 9002, "242", 8747, "243", 8992, "244", 9134, "245", 8993,
    "246", 9118, "247", 9119, "248", 9120, "249", 9124, "250", 9125,
    "251", 9126, "252", 9131, "253", 9132, "254", 9133
])

fn _decode_unicode_code(code) {
    if (code == 64256) { "ff" }
    else if (code == 64257) { "fi" }
    else if (code == 64258) { "fl" }
    else if (code == 64259) { "ffi" }
    else if (code == 64260) { "ffl" }
    else { chr(code) }
}

fn _decode_unicode_string(s) {
    if (s == chr(64256)) { "ff" }
    else if (s == chr(64257)) { "fi" }
    else if (s == chr(64258)) { "fl" }
    else if (s == chr(64259)) { "ffi" }
    else if (s == chr(64260)) { "ffl" }
    else { s }
}

fn _mac_roman_special(code) {
    if (code < 128) { code }
    else if (code <= 255) { _MAC_ROMAN_UPPER[code - 128] }
    else { code }
}

fn _symbol_special(code) {
    let key = string(code)
    if (_SYMBOL_MAP[key]) { _SYMBOL_MAP[key] }
    else { code }
}

fn _decode_byte_with_encoding(code, encoding) {
    if (encoding == "WinAnsiEncoding") {
        let sp = _winansi_special(code)
        if (sp != null) { sp }
        else { _decode_unicode_code(code) }
    }
    else if (encoding == "MacRomanEncoding") { _decode_unicode_code(_mac_roman_special(code)) }
    else if (encoding == "SymbolEncoding") { _decode_unicode_code(_symbol_special(code)) }
    else { _decode_unicode_code(code) }
}

fn _decode_code_with_encoding(code, cmap, encoding) {
    let key = string(code)
    if (cmap and cmap[key]) { _decode_unicode_string(cmap[key]) }
    else { _decode_byte_with_encoding(code, encoding) }
}

fn _decode_code(code, cmap) {
    _decode_code_with_encoding(code, cmap, null)
}

fn _decode_hex_cmap_at(hex: string, i: int, cmap, encoding) {
    let n = len(hex)
    if (i >= n) { "" }
    else {
        let c8 = if (i + 8 <= n) util.hex_code_at(hex, i, 8) else -1
        let h8 = if (c8 >= 0 and cmap and cmap[string(c8)]) cmap[string(c8)] else null
        if (h8 != null) { _decode_unicode_string(h8) ++ _decode_hex_cmap_at(hex, i + 8, cmap, encoding) }
        else {
            let c6 = if (i + 6 <= n) util.hex_code_at(hex, i, 6) else -1
            let h6 = if (c6 >= 0 and cmap and cmap[string(c6)]) cmap[string(c6)] else null
            if (h6 != null) { _decode_unicode_string(h6) ++ _decode_hex_cmap_at(hex, i + 6, cmap, encoding) }
            else {
                let c4 = if (i + 4 <= n) util.hex_code_at(hex, i, 4) else -1
                let h4 = if (c4 >= 0 and cmap and cmap[string(c4)]) cmap[string(c4)] else null
                if (h4 != null) { _decode_unicode_string(h4) ++ _decode_hex_cmap_at(hex, i + 4, cmap, encoding) }
                else {
                    let c2 = util.hex_byte_at(hex, i)
                    _decode_code_with_encoding(c2, cmap, encoding) ++ _decode_hex_cmap_at(hex, i + 2, cmap, encoding)
                }
            }
        }
    }
}

// Decode a Tj/TJ hex operand to a unicode string.
pub fn decode_hex(hex: string, to_unicode) {
    let clean = util.clean_hex(hex)
    if (to_unicode != null) { _decode_hex_cmap_at(clean, 0, to_unicode, null) }
    else {
        let codes = _hex_to_codes(clean)
        let parts = (for (c in codes) _decode_code(c, null))
        parts | join("")
    }
}

pub fn decode_hex_with_font(hex: string, font_info) {
    let cmap = if (font_info) font_info.to_unicode else null
    let enc = if (font_info) font_info.encoding else null
    let clean = util.clean_hex(hex)
    if (cmap != null) { _decode_hex_cmap_at(clean, 0, cmap, enc) }
    else {
        let codes = _hex_to_codes(clean)
        let parts = (for (c in codes) _decode_code_with_encoding(c, null, enc))
        parts | join("")
    }
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

pub fn decode_literal_with_font(s: string, font_info) {
    let cmap = if (font_info) font_info.to_unicode else null
    let enc = if (font_info) font_info.encoding else null
    let n = len(s)
    if (n == 0) { "" }
    else {
        let parts = (for (i in 0 to (n - 1)) _decode_code_with_encoding(ord(s[i]), cmap, enc))
        parts | join("")
    }
}
