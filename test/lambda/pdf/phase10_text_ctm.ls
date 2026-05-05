// Phase 10 — text operators share the graphics-state CTM walk.

import interp: lambda.package.pdf.interp

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }
fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn xml_at(items, i) { if (len(items) > i) format(items[i], 'xml') else "" }

pn main() {
    let pdf = {}
    let page = { MediaBox: [0, 0, 300, 300], Resources: {} }
    let fonts = [{
        name: "F1",
        info: {
            name: "Helvetica",
            family: "Helvetica, Arial, sans-serif",
            weight: "normal",
            style: "normal",
            to_unicode: null
        }
    }]
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [1, 0, 0, 1, 100, 50] },
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [str("Inside")] },
        { op: "ET", operands: [] },
        { op: "Q", operands: [] },
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [str("Outside")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page_with_fonts(pdf, page, ops, 300, fonts)
    let inside_xml = xml_at(r.texts, 0)
    let outside_xml = xml_at(r.texts, 1)
    print({
        text_count: len(r.texts),
        inside_ctm: has(inside_xml, "x=\"110\"") and has(inside_xml, "y=\"230\"") and has(inside_xml, "Inside"),
        outside_restored: has(outside_xml, "x=\"10\"") and has(outside_xml, "y=\"280\"") and has(outside_xml, "Outside"),
        no_paths: len(r.paths) == 0
    })
}
