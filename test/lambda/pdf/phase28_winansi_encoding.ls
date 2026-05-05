// Phase 28 — WinAnsi font encoding fallback for text without ToUnicode.

import font:   lambda.package.pdf.font
import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn hex(s) { { kind: "hex", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let left = chr(8220)
    let right = chr(8221)
    let expected = left ++ "Hi" ++ right
    let expected_xml = "&#xe2;&#x80;&#x9c;Hi&#xe2;&#x80;&#x9d;"
    let encoded = chr(147) ++ "Hi" ++ chr(148)
    let info = { to_unicode: null, encoding: "WinAnsiEncoding" }
    let direct_lit = font.decode_literal_with_font(encoded, info)
    let direct_hex = font.decode_hex_with_font("93 48 69 94", info)
    let page = {
        MediaBox: [0, 0, 100, 100],
        dict: { Resources: { Font: { F1: { BaseFont: "Helvetica", Encoding: "WinAnsiEncoding" } } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [str(encoded)] },
        { op: "Tj", operands: [hex("93486994")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 100.0)
    let xml0 = format(r.texts[0], 'xml')
    let xml1 = format(r.texts[1], 'xml')
    print({
        direct_lit: direct_lit == expected,
        direct_hex: direct_hex == expected,
        count: len(r.texts),
        lit_rendered: has(xml0, expected_xml),
        hex_rendered: has(xml1, expected_xml)
    })
}
