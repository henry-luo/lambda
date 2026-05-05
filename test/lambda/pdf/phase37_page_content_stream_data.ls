// Phase 37 — page /Contents accepts stream_data bytes like other PDF streams.

import pdf: lambda.package.pdf.pdf

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

fn ref(n) { { type: "indirect_ref", object_num: n, gen_num: 0 } }
fn stream_obj(n, dict, data) {
    { object_num: n, gen_num: 0, content: { dictionary: dict, stream_data: data } }
}


pn main() {
    let content = "BT /F1 12 Tf 1 0 0 1 30 40 Tm (StreamPage) Tj ET"
    let font = { Type: "Font", Subtype: "Type1", BaseFont: "Helvetica" }
    let page = { dict: {
        MediaBox: [0, 0, 200, 100],
        Contents: ref(10),
        Resources: { Font: { F1: font } }
    } }
    let doc = { objects: [stream_obj(10, { Length: len(content) }, content)], pages: [page] }
    let svg = format(pdf.pdf_to_svg(doc, 0, { show_label: false }), 'xml')
    print({
        has_text: has(svg, "StreamPage"),
        used_stream_data: has(svg, "StreamPage"),
        no_label: not has(svg, "Page 1")
    })
}