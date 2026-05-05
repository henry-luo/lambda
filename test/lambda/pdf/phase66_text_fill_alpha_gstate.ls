// Phase 66 - ExtGState ca applies to filled text SVG opacity.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: { ExtGState: { A25: { ca: 0.25 } } } } }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 12] },
        { op: "Tm", operands: [1.0, 0.0, 0.0, 1.0, 10.0, 20.0] },
        { op: "gs", operands: [name("A25")] },
        { op: "Tj", operands: [str("Alpha")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.texts[0], 'xml')
    print({
        count: len(r.texts),
        text_alpha: has(xml, "fill-opacity=\"0.25\""),
        no_stroke_alpha: not has(xml, "stroke-opacity"),
        kept_text: has(xml, "Alpha")
    })
}
