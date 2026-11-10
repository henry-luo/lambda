// D6.3.1 / S9.1.2: retained font facts survive GC and remain independent values.
import radiant

pn main() {
    let bytes = input("test/lambda/math/fonts/NotoSansMath-Regular.ttf", 'binary')^
    let faces = [{font_family: "CachedMath", data: bytes}]
    let style = {font_family: "CachedMath", font_size: 20}
    let points = [ord("x"), ord("(")]
    let original = radiant.math_metrics(style, points, faces)
    var changed = radiant.math_metrics(style, points, faces)
    changed.glyphs[0].path = "changed"
    changed.glyphs[1].vertical.variants[0].path = "changed variant"
    let repeated = radiant.math_metrics(style, points, faces)
    let large = radiant.math_metrics({*:style, font_size: 40}, points, faces)
    let replacement = [{font_family: "CachedMath",
        data: input("lmd/package/doc/latex/fonts/Serif/cmunrm.woff2", 'binary')^}]
    let other = radiant.math_metrics(style, points, replacement)
    let restored = radiant.math_metrics(style, points, faces)
    print([repeated == original,
        repeated.glyphs[0].path != changed.glyphs[0].path,
        repeated.glyphs[1].vertical.variants[0].path != changed.glyphs[1].vertical.variants[0].path,
        abs(large.glyphs[0].advance - 2 * original.glyphs[0].advance) < 0.001,
        original.has_math and not other.has_math,
        original.glyphs[0].path != other.glyphs[0].path,
        restored == original]); print("\n")
}
