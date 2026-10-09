// D5.4.2, D7.1.2v2: native query reuse owns bytes and respects snapshot identity.
import radiant

let regular = input("lmd/package/latex/fonts/Serif/cmunrm.woff2", 'binary')^
let italic = input("lmd/package/latex/fonts/Serif/cmunti.woff2", 'binary')^
let style = {font_family: "SnapshotSerif", font_size: 1000}
fn faces(data, slant = "normal") => [{font_family: "SnapshotSerif", font_style: slant, data: data}]
fn metrics(data) => radiant.math_metrics(style, [ord("x"), ord("(")], faces(data))

let first = metrics(regular)
// Reconstructed arrays and independent binary reads must reuse equivalent facts.
let repeated = [for (i in 1 to 12) metrics(input("lmd/package/latex/fonts/Serif/cmunrm.woff2", 'binary')^)]
let changed_bytes = metrics(italic)
let changed_style = radiant.math_metrics({*:style, font_style: "italic"}, [ord("x")], faces(italic, "italic"))
let invalid = metrics(b'00010203')
let restored = metrics(regular)
let invalid_size = radiant.math_metrics(style, [ord("x")],
    [{font_family: "SnapshotSerif", font_size: 0, data: regular}])
let text = radiant.measure_text([{text: "x", font: style}], faces(regular))
let font = radiant.font_metrics(style, faces(regular))
let small = radiant.math_metrics({*:style, font_size: 500}, [ord("x")], faces(regular))
let checks = [
    {name: "equal snapshots preserve all glyph facts", ok: len([for (m in repeated where m != first) m]) == 0},
    {name: "same family with changed bytes changes glyphs", ok: changed_bytes.glyphs[0].path != first.glyphs[0].path},
    {name: "style metadata participates in snapshot identity", ok: changed_style.glyphs[0].font_style == "italic"},
    {name: "corrupt replacement is rejected", ok: invalid == null},
    {name: "returning to a previous snapshot preserves facts", ok: restored == first},
    {name: "invalid face descriptor cannot hit cache", ok: invalid_size == null},
    {name: "text and font queries share the same measured face", ok: abs(text[0].advance - first.glyphs[0].advance) < 0.00001 and
        font == first.font_metrics},
    {name: "query size remains independent of cached resources", ok: abs(small.glyphs[0].advance * 2 - first.glyphs[0].advance) < 0.00001},
    {name: "missing glyph remains missing", ok: radiant.math_metrics(style, [0x10FFFF], faces(regular)).glyphs[0] == null}
];
[for (check in checks where check.ok != true) check.name]
