// D7.1.2v2/D7.5.3: IO owns bytes; Radiant returns copied glyph geometry.
import radiant
import c: .common
import symbols: .glyphs
import font_files: lambda.doc.font

pub let TEXT_FAMILY = "LambdaMusicText"
pub fn prepare() map^ {
    let base = font_files.resource_path("")
    let symbol_path = base ++ "music/fonts/Bravura.otf"
    let text_path = base ++ "doc/latex/fonts/Serif/cmunrm.woff2"
    let faces = [{font_family: "LambdaMusicSymbols", src: symbol_path, data: input(symbol_path, 'binary')^},
        {font_family: TEXT_FAMILY, src: text_path, data: input(text_path, 'binary')^}]
    let names = [for (k,v in symbols.codepoints) string(k)]
    let metrics = radiant.math_metrics({font_family: "LambdaMusicSymbols", font_size: 4.0},
        [for (n in names) symbols.codepoints[n]], faces)
    let checked = c.require(metrics != null and all([for (g in metrics.glyphs) g != null]), "cannot load the complete Bravura glyph profile")^
    let metadata = input(base ++ "music/fonts/Bravura.json", 'json')^;
    {faces: faces, glyphs: map([for (i,n in names) (n, metrics.glyphs[i])]),
        anchors: metadata.glyphsWithAnchors, rules: metadata.engravingDefaults}
}
pub fn glyph(profile, key) map^ {
    let result = profile.glyphs[key]
    let checked = c.require(result != null, "unsupported music glyph: " ++ string(key))^
    result
}
pub fn measure(profile, labels, size = 1.4) array^ {
    let result = radiant.measure_text([for (label in labels) {text: label,
        font: {font_family: TEXT_FAMILY, font_size: size, font_weight: 400, font_style: "normal"}}], profile.faces)
    let checked = c.require(result != null and len(result) == len(labels), "text measurement failed")^
    result
}
