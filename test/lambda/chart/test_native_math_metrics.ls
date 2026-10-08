import radiant

let faces = [
    {font_family: "MathSans", data: input("test/lambda/math/fonts/NotoSansMath-Regular.ttf", 'binary')^}
]
let points = [ord("𝑓"), ord("("), ord("√"), ord("̂")]
let sans = radiant.math_metrics({font_family: "MathSans", font_size: 1000}, points, faces)
let small = radiant.math_metrics({font_family: "MathSans", font_size: 20}, [ord("𝑓")], faces)
let ordinary_faces = [{font_family: "Ordinary", data: input("lmd/package/latex/fonts/Serif/cmunrm.woff2", 'binary')^}]
let ordinary = radiant.math_metrics({font_family: "Ordinary", font_size: 20}, [ord("x"), ord("(")], ordinary_faces)
let ordinary_large = radiant.math_metrics({font_family: "Ordinary", font_size: 40}, [ord("x")], ordinary_faces)
let missing = radiant.math_metrics({font_family: "Ordinary", font_size: 20}, [ord("∑")], ordinary_faces)
let fallback_symbol = radiant.math_metrics({font_family: "Ordinary", font_size: 20, fallback: true}, [ord("∑")], ordinary_faces)
let checks = [
    {name: "sans axis", ok: sans.constants.axis_height == 278},
    {name: "font-specific glyphs", ok: sans.glyphs[1].advance / 50 != ordinary.glyphs[1].advance},
    {name: "scale font units once", ok: abs(small.constants.axis_height - 5.56) < 0.001},
    {name: "percent stays dimensionless", ok: small.constants.script_percent_scale_down == sans.constants.script_percent_scale_down},
    {name: "italic correction", ok: sans.glyphs[0].italic > 0},
    {name: "accent attachment", ok: sans.glyphs[0].accent != null},
    {name: "outlined glyph", ok: len(sans.glyphs[0].path) > 0},
    {name: "delimiter variants", ok: len(sans.glyphs[1].vertical.variants) > 1},
    {name: "delimiter assembly", ok: len(sans.glyphs[1].vertical.parts |: ~.extender) > 0},
    {name: "radical variants", ok: len(sans.glyphs[2].vertical.variants) > 1},
    {name: "MATH capability is explicit", ok: sans.has_math and ordinary.has_math == false},
    {name: "ordinary font still supplies glyphs", ok: ordinary.has_math == false and ordinary.constants == null and
        ordinary.glyphs[0].advance > 0 and len(ordinary.glyphs[0].path) > 0},
    {name: "ordinary glyph size scales", ok: abs(ordinary_large.glyphs[0].advance - ordinary.glyphs[0].advance * 2) < 0.001},
    {name: "ordinary font has no invented constructions", ok: ordinary.glyphs[1].vertical.variants == [] and ordinary.glyphs[1].vertical.parts == []},
    {name: "ordinary script and rule metrics", ok: ordinary.font_metrics.superscript_y_offset > 0 and ordinary.font_metrics.underline_thickness > 0},
    {name: "symbol fallback reports its actual face", ok: missing.glyphs == [null] and fallback_symbol.glyphs[0].advance > 0 and
        fallback_symbol.glyphs[0].font_family != ordinary.font_family and len(fallback_symbol.glyphs[0].path) > 0},
    {name: "invalid codepoint", ok: radiant.math_metrics({font_family: "MathSans"}, [1114112], faces) == null}
];
[for (check in checks where check.ok != true) check.name]
