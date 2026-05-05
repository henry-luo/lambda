// Phase 47 - unsupported ICCBased component counts render black like native C++.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            ICC2: { N: 2 },
            ICC5: ["ICCBased", { N: 5 }]
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("ICC2")] },
        { op: "sc", operands: [0.5, 0.25] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("ICC5")] },
        { op: "sc", operands: [0.0, 1.0, 0.0, 0.0, 0.0] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml0 = format(r.paths[0], 'xml')
    let xml1 = format(r.paths[1], 'xml')
    print({
        count: len(r.paths),
        map_icc_black: has(xml0, "fill=\"rgb(0,0,0)\""),
        array_icc_black: has(xml1, "fill=\"rgb(0,0,0)\""),
        not_gray_fallback: not has(xml0, "fill=\"rgb(128,128,128)\""),
        not_rgb_fallback: not has(xml1, "fill=\"rgb(0,255,0)\"")
    })
}
