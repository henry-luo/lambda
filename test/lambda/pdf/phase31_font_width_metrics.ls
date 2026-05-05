// Phase 31 — font /Widths metrics affect text advance.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let page = {
        MediaBox: [0, 0, 120, 120],
        dict: { Resources: { Font: {
            F1: { BaseFont: "Helvetica", FirstChar: 65, LastChar: 66, Widths: [1000, 250] }
        } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 20] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [str("A")] },
        { op: "Tj", operands: [str("B")] },
        { op: "Tj", operands: [str("C")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 120.0)
    let xml0 = format(r.texts[0], 'xml')
    let xml1 = format(r.texts[1], 'xml')
    let xml2 = format(r.texts[2], 'xml')
    print({
        count: len(r.texts),
        first_at_10: has(xml0, "x=\"10\""),
        second_uses_width_a: has(xml1, "x=\"30\""),
        third_uses_width_b: has(xml2, "x=\"35\""),
        fallback_width_c: has(xml2, ">C</text>")
    })
}