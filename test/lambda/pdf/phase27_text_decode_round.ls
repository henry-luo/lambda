// Phase 27 — parser/font text decoding parity round.

import font:   lambda.package.pdf.font
import interp: lambda.package.pdf.interp
import stream: lambda.package.pdf.stream

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let cmap = map(["1", "A", "2", "B", "258", "Z"])
    let spaced_hex = font.decode_hex("41 42\n43", null)
    let odd_hex = font.decode_hex("412", null)
    let cid_hex = font.decode_hex("00010002", cmap)
    let greedy_hex = font.decode_hex("0102", cmap)

    let tokens = stream.parse_content_stream("/Trim) <41 42> Tj")
    let name_ok = tokens[0].operands[0].value == "Trim"
    let hex_ok = tokens[0].operands[1].value == "4142"

    let page = {
        MediaBox: [0, 0, 100, 100],
        dict: { Resources: { Font: { F1: { BaseFont: "Helvetica", to_unicode: cmap } } } }
    }
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [{ kind: "name", value: "F1" }, 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "Tj", operands: [{ kind: "hex", value: "00010002" }] },
        { op: "ET", operands: [] }
    ]
    let r = interp.render_page({}, page, ops, 100.0)
    let xml = format(r.texts[0], 'xml')
    print({
        spaced_hex: spaced_hex,
        odd_hex: odd_hex,
        cid_hex: cid_hex,
        greedy_hex: greedy_hex,
        name_ok: name_ok,
        hex_ok: hex_ok,
        rendered: has(xml, ">AB</text>"),
        xml: xml
    })
}
