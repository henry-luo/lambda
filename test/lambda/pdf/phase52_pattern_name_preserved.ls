// Phase 52 - named shading Pattern scn still uses Lambda's pattern support.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: {
            Pattern: { P1: { PatternType: 2, Shading: {
                ShadingType: 2,
                ColorSpace: "DeviceRGB",
                Coords: [0, 0, 10, 0],
                Function: { C0: [1.0, 0.0, 0.0], C1: [0.0, 0.0, 1.0], N: 1.0 }
            } } }
        } }
    }
    let ops = [
        { op: "cs", operands: [name("Pattern")] },
        { op: "scn", operands: [name("P1")] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let svg = format(r.paths, 'xml')
    print({
        count: len(r.paths),
        has_gradient: has(svg, "linearGradient"),
        has_pattern_fill: has(svg, "fill=\"url(#"),
        no_black_rect: not has(svg, "fill=\"rgb(0,0,0)\"")
    })
}
