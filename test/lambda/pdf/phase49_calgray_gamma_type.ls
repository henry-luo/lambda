// Phase 49 - CalGray Gamma only applies when it is numeric like native C++.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            Good: ["CalGray", { Gamma: 2.0 }],
            ArrayGamma: ["CalGray", { Gamma: [2.0] }],
            BadGamma: ["CalGray", { Gamma: "bad" }]
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("Good")] },
        { op: "sc", operands: [0.5] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("ArrayGamma")] },
        { op: "sc", operands: [0.5] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("BadGamma")] },
        { op: "sc", operands: [0.5] },
        { op: "re", operands: [40, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml0 = format(r.paths[0], 'xml')
    let xml1 = format(r.paths[1], 'xml')
    let xml2 = format(r.paths[2], 'xml')
    print({
        count: len(r.paths),
        numeric_gamma_applied: has(xml0, "fill=\"rgb(64,64,64)\""),
        array_gamma_ignored: has(xml1, "fill=\"rgb(128,128,128)\""),
        bad_gamma_ignored: has(xml2, "fill=\"rgb(128,128,128)\""),
        not_array_gamma_applied: not has(xml1, "fill=\"rgb(64,64,64)\"")
    })
}