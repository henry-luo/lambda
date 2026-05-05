// Phase 51 - numeric-only Pattern scn/SCN does not invent a gray tint.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: {} } }
    let ops = [
        { op: "cs", operands: [name("Pattern")] },
        { op: "scn", operands: [0.5] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "CS", operands: [name("Pattern")] },
        { op: "SCN", operands: [0.25] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "S", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let fill_xml = format(r.paths[0], 'xml')
    let stroke_xml = format(r.paths[1], 'xml')
    print({
        count: len(r.paths),
        fill_black: has(fill_xml, "fill=\"rgb(0,0,0)\""),
        stroke_black: has(stroke_xml, "stroke=\"rgb(0,0,0)\""),
        not_gray_fill: not has(fill_xml, "fill=\"rgb(128,128,128)\""),
        not_gray_stroke: not has(stroke_xml, "stroke=\"rgb(64,64,64)\"")
    })
}
