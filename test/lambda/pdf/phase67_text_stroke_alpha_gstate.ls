// Phase 67 - ExtGState CA applies to stroked text SVG opacity.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: { ExtGState: { S40: { CA: 0.4 } } } } }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 12] },
        { op: "Tm", operands: [1.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "Tr", operands: [1] },
        { op: "gs", operands: [name("S40")] },
        { op: "Tj", operands: [str("Stroke") ] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.texts[0], 'xml')
    print({
        count: len(r.texts),
        stroke_mode: has(xml, "fill=\"none\"") and has(xml, "stroke=\"rgb(0,0,0)\""),
        stroke_alpha: has(xml, "stroke-opacity=\"0.4\""),
        kept_text: has(xml, "Stroke")
    })
}
