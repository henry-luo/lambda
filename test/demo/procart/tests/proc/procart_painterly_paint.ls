// stamping blends every stroke into the canvas and replays exactly; the PNG decodes back.
import P: ~~.~~.mod_painterly

pn main() {
    let src = P.source(input("test/demo/photo/assets/coast.jpg", 'binary')^, 120)^
    let field = P.analyze(src)
    let n = src.width * src.height
    var results = []
    for (mode in ["single", "oriented"]) {
        let plan = P.plan(field, 2026, mode, 1)
        let canvas = P.paint(plan, P.brush("bristle"))
        let again = P.paint(plan, P.brush("bristle"))
        let other = P.paint(plan, P.brush("round"))
        let image = P.encode(plan, canvas)^
        let decoded = load({data:image.bytes})^
        results = results ++ [[mode, len(canvas) == 3 * n, min(canvas) >= 0.0, max(canvas) <= 1.15,
            canvas == again, canvas != other, len([for (v in canvas where v != 0.85) v]) > n,
            slice(image.bytes, 0, 4) == b'\x89504E47', shape(decoded) == [src.height, src.width, 4],
            starts_with(image.uri, "data:image/png;base64,")]]
    }
    return results
}
