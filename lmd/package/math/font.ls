// Immutable, per-render font facts. Resource reads stay in Lambda IO (D7.1.2v2).
import radiant
import sym: .symbols
import util: .util
import fallback: .fallback
import bundled: .bundled

pub let UNITS = 1000.0

// Unicode mathematical alphabets describe semantic variants, not a font's cmap.
let alphabets = {
    italic: [0x1D434, 0x1D44E, 0], bold: [0x1D400, 0x1D41A, 0x1D7CE],
    bolditalic: [0x1D468, 0x1D482, 0], script: [0x1D49C, 0x1D4B6, 0],
    fraktur: [0x1D504, 0x1D51E, 0], double: [0x1D538, 0x1D552, 0x1D7D8],
    sans: [0x1D5A0, 0x1D5BA, 0x1D7E2], mono: [0x1D670, 0x1D68A, 0x1D7F6]
}
// Unicode preserves these alphabet members in the Letterlike Symbols block.
let exceptions = {
    italic: {h: 0x210E},
    script: {B: 0x212C, E: 0x2130, F: 0x2131, H: 0x210B, I: 0x2110,
        L: 0x2112, M: 0x2133, R: 0x211B, e: 0x212F, g: 0x210A, o: 0x2134},
    fraktur: {C: 0x212D, H: 0x210C, I: 0x2111, R: 0x211C, Z: 0x2128},
    double: {C: 0x2102, H: 0x210D, N: 0x2115, P: 0x2119, Q: 0x211A, R: 0x211D, Z: 0x2124}
}

fn effective_style(ch, style) => if (style == "auto")
    (if (ord(ch) >= 0x391 and ord(ch) <= 0x3A9) "normal" else "italic") else style

pub fn variant(ch, style) {
    let cp = ord(ch)
    let actual = effective_style(ch, style)
    let alphabet = alphabets[if (actual == "cal") "script" else actual]
    let special = exceptions[if (actual == "cal") "script" else actual][ch]
    let greek = if (actual == "italic") 0x1D6E2 else if (actual == "bold") 0x1D6A8
        else if (actual == "bolditalic") 0x1D71C else null
    let greek_special = index_of([0x2207, 0x2202, 0x3F5, 0x3D1, 0x3F0, 0x3D5, 0x3F1, 0x3D6], cp)
    if (special != null) special
    else if (alphabet != null and cp >= 65 and cp <= 90) alphabet[0] + cp - 65
    else if (alphabet != null and cp >= 97 and cp <= 122) alphabet[1] + cp - 97
    else if (alphabet != null and alphabet[2] > 0 and cp >= 48 and cp <= 57) alphabet[2] + cp - 48
    else if (greek != null and cp >= 0x391 and cp <= 0x3A9) greek + cp - 0x391
    else if (greek != null and cp >= 0x3B1 and cp <= 0x3C9) greek + 26 + cp - 0x3B1
    else if (greek != null and cp == 0x3F4) greek + 17
    else if (greek != null and greek_special != null) greek + (if (greek_special == 0) 25 else 50 + greek_special)
    else cp
}

let styled_commands = {
    '\\mathrm': "normal", '\\mathbf': "bold", '\\boldsymbol': "bolditalic",
    '\\mathit': "italic", '\\mathcal': "cal", '\\mathscr': "script",
    '\\mathfrak': "fraktur", '\\mathbb': "double", '\\mathsf': "sans",
    '\\mathtt': "mono", '\\operatorname': "normal"
}

pub fn command_variant(cmd) => styled_commands[cmd]

fn collect(node) {
    if (node is string or node is symbol) {
        let text = string(node)
        let command = if (slice(text, 0, 1) == "\\") slice(text, 1, len(text)) else text
        text ++ (sym.lookup_symbol(text) or "") ++ (sym.get_accent(command) or "")
    } else if (node is array) util.str_join([for (child in node) collect(child)], "")
    else if (node is element) {
        let attrs = ["value", "name", "cmd", "text", "base", "sub", "sup", "numer", "denom",
            "radicand", "index", "arg", "content", "body", "left", "right", "above", "below",
            "label", "over", "under", "delim", "annotation", "upper", "lower", "target"];
        util.str_join([for (attr in attrs) collect(node[attr])], "") ++ collect(content(node))
    } else ""
}

pub fn prepare(ast, options) map | error {
    let use_bundled = options.font_family == null and options.fonts == null
    let family = options.font_family or bundled.FAMILY
    let faces = if (options.fonts != null) options.fonts else if (options.font_family != null) null
        else bundled.faces()^
    let variant_families = if (use_bundled) bundled.VARIANT_FAMILIES else {sans: "sans-serif", mono: "monospace"}
    // One batch owns all glyphs used by this formula; no mutable last-font state.
    let source = collect(ast)
    let chars = unique(split(source ++ "()[]{}|‖⌈⌉⌊⌋⟨⟩√̂̃̄⃗̇̈⏞⏟←→↔− /", ""))
    let styles = ["normal", "auto", "italic", "bold", "bolditalic", "script", "fraktur", "double", "sans", "mono"]
    let points = unique([for (ch in chars, style in styles) variant(ch, style)])
    let native = radiant.math_metrics({font_family: family, font_size: UNITS}, points, faces)
    if (native == null) error("math: cannot read selected font: " ++ family)
    else {
    let facts = {*:native, constants: if (native.has_math) native.constants else fallback.constants(native.font_metrics)}
    // Ordinary fonts put italic/bold letters in separate faces, not Unicode math alphabets.
    // Only request style faces used by this formula; each query owns its font resources.
    let needed_styles = unique(["italic", *[for (cmd, style in styled_commands where contains(source, string(cmd))) style],
        *[for (entry in [{cmd: "\\textbf", style: "bold"}, {cmd: "\\textit", style: "italic"},
            {cmd: "\\emph", style: "italic"}, {cmd: "\\textsf", style: "sans"}, {cmd: "\\texttt", style: "mono"}]
            where contains(source, entry.cmd)) entry.style]])
    let style_faces = if (native.has_math) [] else [for (style in needed_styles where style != "normal")
        {style: style, facts: radiant.math_metrics({font_family: variant_families[style] or family, font_size: UNITS,
            font_weight: if (style == "bold" or style == "bolditalic") 700 else 400,
            font_style: if (style == "italic" or style == "bolditalic") "italic" else "normal"}, points, faces)}]
    // Resolve only absent source characters; never replace a Latin variable with
    // a different font's mathematical-alphabet glyph just to obtain italics.
    let fallback_points = [for (ch in chars where lookup({points: points}, native, ord(ch)) == null) ord(ch)]
    let fallback_facts = if (len(fallback_points) == 0) null else
        radiant.math_metrics({font_family: if (use_bundled) bundled.SYMBOL_FAMILIES else family,
            font_size: UNITS, fallback: true}, fallback_points, faces)
    if (facts.constants.script_percent_scale_down <= 0 or facts.constants.script_script_percent_scale_down <= 0)
        error("math: selected font has invalid script scale constants")
    else {facts: facts, points: points, family: facts.font_family,
        font_metrics: facts.font_metrics, style_faces: style_faces,
        fallback_points: fallback_points, fallback_facts: fallback_facts}
    }
}

fn lookup(profile, facts, cp) {
    let index = index_of(profile.points, cp)
    if (index != null) facts.glyphs[index] else null
}

pub fn glyph(profile, cp) map | error {
    let result = lookup(profile, profile.facts, cp) or
        lookup({points: profile.fallback_points}, profile.fallback_facts, cp)
    if (result == null) error("math: selected font lacks glyph U+" ++ string(cp) ++ " (" ++ profile.family ++ ")")
    else result
}

pub fn character(profile, ch, style) map | error {
    let cp = variant(ch, style)
    let preferred = lookup(profile, profile.facts, cp)
    let actual = effective_style(ch, style)
    let styled = [for (entry in profile.style_faces where entry.style == actual) entry.facts][0]
    let ordinary = lookup(profile, styled or profile.facts, ord(ch))
    if (preferred != null) preferred
    else if (ordinary != null) ordinary
    else glyph(profile, ord(ch))^
}

pub fn scale(profile, style) {
    if (style == "script") profile.facts.constants.script_percent_scale_down / 100.0
    else if (style == "scriptscript") profile.facts.constants.script_script_percent_scale_down / 100.0
    else 1.0
}
