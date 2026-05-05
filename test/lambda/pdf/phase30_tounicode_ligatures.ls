// Phase 30 — ToUnicode ligature decomposition parity.

import font:   lambda.package.pdf.font
import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn hex(s) { { kind: "hex", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let cmap = map(["1", chr(64257), "2", chr(64258), "258", chr(64259)])
    let info = { to_unicode: cmap, encoding: null }
    let cid_pair = font.decode_hex_with_font("00010002", info)
    let greedy = font.decode_hex_with_font("0102", info)

    let page = {
        MediaBox: [0, 0, 120, 120],
        dict: { Resources: { Font: { F1: { BaseFont: "Helvetica", to_unicode: cmap } } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [hex("00010002")] },
        { op: "Tj", operands: [hex("0102")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 120.0)
    let xml0 = format(r.texts[0], 'xml')
    let xml1 = format(r.texts[1], 'xml')
    print({
        cid_pair: cid_pair == "fifl",
        greedy: greedy == "ffi",
        render_count: len(r.texts),
        render_pair: has(xml0, ">fifl</text>"),
        render_greedy: has(xml1, ">ffi</text>")
    })
}
