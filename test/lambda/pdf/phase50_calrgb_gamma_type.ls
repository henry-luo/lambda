// Phase 50 - CalRGB Gamma only applies numeric entries from an array.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            ScalarGamma: ["CalRGB", { Gamma: 2.0 }],
            MixedGamma: ["CalRGB", { Gamma: ["bad", 2.0, 0.5] }]
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("ScalarGamma")] },
        { op: "sc", operands: [0.5, 0.5, 0.5] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("MixedGamma")] },
        { op: "sc", operands: [0.5, 0.5, 0.25] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml0 = format(r.paths[0], 'xml')
    let xml1 = format(r.paths[1], 'xml')
    print({
        count: len(r.paths),
        scalar_gamma_ignored: has(xml0, "fill=\"rgb(128,128,128)\""),
        mixed_gamma_applied: has(xml1, "fill=\"rgb(128,64,128)\""),
        not_scalar_red_gamma: not has(xml0, "fill=\"rgb(64,128,128)\""),
        not_bad_entry_gamma: not has(xml1, "fill=\"rgb(255,64,128)\"")
    })
}