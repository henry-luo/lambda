// Bounded Babel language names and HTML language tags.
import util: ~~.util

// LGR's Latin alphabet mapping follows babel-greek's documented transliteration.
let LGR_LOWER = {
    a: "α", b: "β", g: "γ", d: "δ", e: "ε", z: "ζ", h: "η", j: "θ",
    i: "ι", k: "κ", l: "λ", m: "μ", n: "ν", x: "ξ", o: "ο", p: "π",
    r: "ρ", s: "σ", c: "ς", t: "τ", u: "υ", f: "φ", q: "χ", y: "ψ", w: "ω"
}

let LGR_UPPER = {
    A: "Α", B: "Β", G: "Γ", D: "Δ", E: "Ε", Z: "Ζ", H: "Η", J: "Θ",
    I: "Ι", K: "Κ", L: "Λ", M: "Μ", N: "Ν", X: "Ξ", O: "Ο", P: "Π",
    R: "Ρ", S: "Σ", T: "Τ", U: "Υ", F: "Φ", Q: "Χ", Y: "Ψ", W: "Ω"
}

fn lgr_letter(ch) {
    let lowercase = LGR_LOWER[ch]
    if (lowercase != null) lowercase else LGR_UPPER[ch]
}

fn modifier(ch, polytonic) =>
    ch == "'" or ch == "`" or ch == "<" or ch == ">" or ch == "\"" or
    (ch == "~" and polytonic)

fn vowel(ch) =>
    ch == "a" or ch == "e" or ch == "h" or ch == "i" or ch == "o" or
    ch == "u" or ch == "w" or ch == "A" or ch == "E" or ch == "H" or
    ch == "I" or ch == "O" or ch == "U" or ch == "W"

fn modifier_run(source, at, polytonic, marks) {
    if (at >= len(source) or not modifier(slice(source, at, at + 1), polytonic))
        {next: at, marks: marks}
    else modifier_run(source, at + 1, polytonic,
        marks ++ slice(source, at, at + 1))
}

fn marks_unicode(marks) {
    let breathing = if (contains(marks, "<")) "\u0314"
        else if (contains(marks, ">")) "\u0313" else ""
    let diaeresis = if (contains(marks, "\"")) "\u0308" else ""
    let accent = if (contains(marks, "'")) "\u0301"
        else if (contains(marks, "`")) "\u0300"
        else if (contains(marks, "~")) "\u0342" else ""
    breathing ++ diaeresis ++ accent
}

fn final_sigma(source, next_at) {
    if (next_at >= len(source)) true
    else lgr_letter(slice(source, next_at, next_at + 1)) == null
}

fn transliterate_at(source, at, polytonic, acc) {
    if (at >= len(source)) acc
    else {
        let ch = slice(source, at, at + 1)
        let run = modifier_run(source, at, polytonic, "")
        let next_ch = if (run.next < len(source))
            slice(source, run.next, run.next + 1) else ""
        if (slice(source, at, at + 2) == "''")
            transliterate_at(source, at + 2, polytonic, acc ++ "\u2019")
        else if (slice(source, at, at + 2) == "``")
            transliterate_at(source, at + 2, polytonic, acc ++ "\u2018")
        else if (len(run.marks) > 0 and vowel(next_ch)) {
            let base = lgr_letter(next_ch)
            let suffix_iota = run.next + 1 < len(source) and
                slice(source, run.next + 1, run.next + 2) == "|"
            let composed = normalize(base ++ marks_unicode(run.marks) ++
                (if (suffix_iota) "\u0345" else ""), 'nfc')
            transliterate_at(source, run.next + (if (suffix_iota) 2 else 1),
                polytonic, acc ++ composed)
        } else if (ch == "s")
            transliterate_at(source, at + 1, polytonic,
                acc ++ (if (final_sigma(source, at + 1)) "ς" else "σ"))
        else if (lgr_letter(ch) != null)
            transliterate_at(source, at + 1, polytonic, acc ++ lgr_letter(ch))
        else if (ch == "?" or ch == ";")
            transliterate_at(source, at + 1, polytonic,
                acc ++ (if (ch == "?") ";" else "·"))
        else transliterate_at(source, at + 1, polytonic, acc ++ ch)
    }
}

pub fn lgr_to_unicode(source, polytonic) =>
    transliterate_at(source, 0, polytonic, "")

fn greek_attribute(node) {
    if (not (node is element)) null
    else if (string(name(node)) == "languageattribute" and
        trim(util.text_of_child(node, 0)) == "greek")
        trim(util.text_of_child(node, 1))
    else greek_attribute_children(node, 0)
}

fn greek_attribute_children(node, at) {
    if (at >= len(node)) null
    else {
        let found = greek_attribute(node[at])
        if (found != null) found else greek_attribute_children(node, at + 1)
    }
}

pub fn greek_polytonic(ast) {
    let attribute = greek_attribute(ast)
    attribute == "polutoniko" or attribute == "ancient"
}
pub fn code(language) {
    match language {
        case "english": "en"
        case "german": "de"
        case "ngerman": "de"
        case "french": "fr"
        case "spanish": "es"
        case "russian": "ru"
        case "greek": "el"
        case "thai": "th"
        case "vietnamese": "vi"
        default: null
    }
}

pub fn supported(language) => code(language) != null

pub fn babel_default(options) {
    if (options == null) "english"
    else {
        let choices = [for (key, value at options
            where supported(string(key))) string(key)]
        if (len(choices) == 0) "english" else choices[len(choices) - 1]
    }
}

// Polyglossia's font declarations occur in both braced and control-sequence forms.
fn font_declaration(node, following) {
    if (not (node is element)) null
    else {
        let tag = string(name(node))
        if (tag != "newfontfamily" and tag != "renewfontfamily") null
        else {
            let braced_font = util.raw_argument(node, "required", 1)
            let braced_name = util.raw_argument(node, "required", 0)
            let target = if (braced_font != null) braced_name
                else if (following is element) "\\" ++ string(name(following)) else null
            let font = if (braced_font != null) braced_font
                else if (following is element) util.raw_argument(following, "required", 0)
                else null
            if (target == null or font == null or not starts_with(target, "\\")) null
            else {key: slice(target, 1, len(target)), val: trim(font),
                  offset: node.source_offset}
        }
    }
}

fn fonts_in_children(node, at) {
    if (at >= len(node)) []
    else {
        let child = node[at]
        let following = if (at + 1 < len(node)) node[at + 1] else null
        let declaration = font_declaration(child, following);
        (if (declaration == null) [] else [declaration]) ++
        (if (child is element) fonts_in_children(child, 0) else []) ++
        fonts_in_children(node, at + 1)
    }
}

fn find_default(node) {
    if (not (node is element)) null
    else if (string(name(node)) == "setdefaultlanguage") node
    else find_default_child(node, 0)
}

fn find_default_child(node, at) {
    if (at >= len(node)) null
    else {
        let found = find_default(node[at])
        if (found != null) found else find_default_child(node, at + 1)
    }
}

pub fn polyglossia_profile(ast) {
    let declaration = find_default(ast)
    let selected = if (declaration == null) "english"
        else trim(util.raw_argument(declaration, "required", 0))
    let opts = if (declaration == null) {}
        else util.parse_kv_options(util.optional_raw(declaration))
    {default: selected, numerals: opts.numerals,
     fonts: fonts_in_children(ast, 0), declaration: declaration}
}

pub fn font_for(profile, command) =>
    if (profile == null) null else util.lookup(profile.fonts, command)

fn valid_font_chars(font, at) {
    if (at >= len(font)) true
    else {
        let ch = slice(font, at, at + 1)
        if (index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -", ch) == null)
            false
        else valid_font_chars(font, at + 1)
    }
}

pub fn font_style(profile, command, fallback) {
    let requested = font_for(profile, command)
    let chosen = if (requested != null and valid_font_chars(requested, 0))
        "'" ++ requested ++ "'," else ""
    "font-family:" ++ chosen ++ fallback ++ ";"
}

pub fn polyglossia_issues(profile, package_offset) {
    let source_offset = if (profile.declaration == null) package_offset
        else profile.declaration.source_offset;
    (if (not supported(profile.default))
        [util.diagnostic("unsupported-language", "polyglossia", profile.default,
            "Unsupported polyglossia language " ++ profile.default, source_offset)] else []) ++
    (if (profile.numerals != null and
         not (profile.default == "thai" and profile.numerals == "thai"))
        [util.diagnostic("unsupported-language-numerals", "polyglossia", profile.numerals,
            "Unsupported polyglossia numeral system " ++ profile.numerals, source_offset)] else []) ++
    [for (entry in profile.fonts)
        util.diagnostic("font-substitution", "polyglossia", entry.val,
            "Requested font is not bundled; using system font or fallback: " ++ entry.val,
            entry.offset)]
}

pub fn target_issues(profile, target, offset) {
    if (profile == null or profile.default != "thai" or
        (target != "svg" and target != "pdf")) []
    else [util.diagnostic("complex-script-layout-approximation", "polyglossia", "thai",
        "Native layout places glyphs per character; Thai mark shaping and dictionary line breaking need a shared text shaper",
        if (profile.declaration == null) offset else profile.declaration.source_offset)]
}

let THAI_DIGITS = ["๐", "๑", "๒", "๓", "๔", "๕", "๖", "๗", "๘", "๙"]

fn thai_digits_at(source, at, acc) {
    if (at >= len(source)) acc
    else {
        let ch = slice(source, at, at + 1)
        let digit = int(ch) ^ { null }
        thai_digits_at(source, at + 1,
            acc ++ (if (digit != null and digit >= 0 and digit <= 9)
                THAI_DIGITS[digit] else ch))
    }
}

pub fn display_number(source, profile) =>
    if (source == null or profile == null or profile.default != "thai" or
        profile.numerals != "thai") source
    else thai_digits_at(string(source), 0, "")
