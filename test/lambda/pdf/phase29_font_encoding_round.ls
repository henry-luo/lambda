// Phase 29 — native-backed font encoding parity round.

import font:   lambda.package.pdf.font
import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn str(s) { { kind: "string", value: s } }
fn hex(s) { { kind: "hex", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let mac_info = { to_unicode: null, encoding: "MacRomanEncoding" }
    let symbol_info = { to_unicode: null, encoding: "SymbolEncoding" }

    let mac_lig = font.decode_literal_with_font(chr(222) ++ chr(223), mac_info)
    let mac_hex = font.decode_hex_with_font("80 DB", mac_info)
    let symbol_lit = font.decode_literal_with_font("ab", symbol_info)
    let symbol_hex = font.decode_hex_with_font("61 62", symbol_info)

    let page = {
        MediaBox: [0, 0, 120, 120],
        dict: { Resources: { Font: {
            F1: { BaseFont: "Helvetica", Encoding: "MacRomanEncoding" },
            F2: { BaseFont: "Symbol" }
        } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [str(chr(222) ++ chr(223))] },
        { op: "Tf", operands: [name("F2"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 40] },
        { op: "Tj", operands: [hex("6162")] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 120.0)
    let xml0 = format(r.texts[0], 'xml')
    let xml1 = format(r.texts[1], 'xml')
    print({
        mac_ligature: mac_lig == "fifl",
        mac_upper: mac_hex == (chr(196) ++ chr(8364)),
        symbol_lit: symbol_lit == (chr(945) ++ chr(946)),
        symbol_hex: symbol_hex == (chr(945) ++ chr(946)),
        render_count: len(r.texts),
        render_mac: has(xml0, ">fifl</text>"),
        render_symbol: has(xml1, "&#xce;&#xb1;&#xce;&#xb2;")
    })
}
