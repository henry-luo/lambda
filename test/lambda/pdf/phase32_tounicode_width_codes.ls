// Phase 32 — ToUnicode text advances by source glyph code widths.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn hex(s) { { kind: "hex", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let cmap = map(["1", "A", "2", "B"])
    let page = {
        MediaBox: [0, 0, 120, 120],
        dict: { Resources: { Font: {
            F1: { BaseFont: "Helvetica", FirstChar: 1, LastChar: 2,
                  Widths: [1000, 250], to_unicode: cmap }
        } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 20] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [hex("0001")] },
        { op: "Tj", operands: [hex("0002")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 120.0)
    let xml0 = format(r.texts[0], 'xml')
    let xml1 = format(r.texts[1], 'xml')
    print({
        count: len(r.texts),
        decoded_a: has(xml0, ">A</text>"),
        decoded_b: has(xml1, ">B</text>"),
        first_at_10: has(xml0, "x=\"10\""),
        second_uses_code_width: has(xml1, "x=\"30\"")
    })
}