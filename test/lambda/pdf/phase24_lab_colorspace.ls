// Phase 24 — Lab color spaces use the native C++ approximate RGB fallback.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: {
            Resources: {
                ColorSpace: { LabCS: ["Lab", {}] }
            }
        }
    }
    let ops = [
        { op: "cs", operands: [name("LabCS")] },
        { op: "sc", operands: [25, 127, -128] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        lab_fill: has(xml, "fill=\"rgb(64,255,0)\""),
        not_rgb_clamp: not has(xml, "fill=\"rgb(255,255,0)\""),
        xml: xml
    })
}
