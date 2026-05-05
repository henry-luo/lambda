// Phase 26 — literal strings decode native C++-style octal escapes.

import interp: lambda.package.pdf.interp
import stream: lambda.package.pdf.stream

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let bytes = "BT /F1 10 Tf 1 0 0 1 10 20 Tm (\\101\\040B) Tj ET"
    let ops = stream.parse_content_stream(bytes)
    let page = { MediaBox: [0, 0, 100, 100], Resources: {} }
    let r = interp.render_page({}, page, ops, 100.0)
    let txt = ops[3].operands[0].value
    let xml = format(r.texts[0], 'xml')
    print({
        parsed: txt,
        parsed_ok: txt == "A B",
        text_count: len(r.texts),
        rendered: has(xml, ">A B</text>"),
        xml: xml
    })
}
