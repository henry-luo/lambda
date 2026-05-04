// Phase 3 — full pipeline on simple_test.pdf.
//
// simple_test.pdf uses these operators: BT, Tf, Td, Tj, ET, rg, re, f,
// RG, w, S — exactly the Phase 3 surface (filled rect + stroked rect +
// text). Asserts:
//   - paths: a filled red rect + a stroked black rectangle outline
//   - texts: at least one decoded string
//   - the stroked rectangle keeps the line-width set by `w`

import resolve: lambda.package.pdf.resolve
import stream:  lambda.package.pdf.stream
import interp:  lambda.package.pdf.interp
import coords:  lambda.package.pdf.coords

pn main() {
    let doc^err = input("test/pdf/data/basic/simple_test.pdf", 'pdf')
    let page = resolve.page_at(doc, 0)
    let rect = coords.media_box_rect(page)
    let bytes = resolve.page_content_bytes(doc, page)
    let ops = stream.parse_content_stream(bytes)
    let r = interp.render_page(doc, page, ops, rect.h)

    var path_xml = []
    var i = 0
    let np = len(r.paths)
    while (i < np) {
        path_xml = path_xml ++ [format(r.paths[i], 'xml')]
        i = i + 1
    }

    print({
        page_w:      rect.w,
        page_h:      rect.h,
        path_count:  np,
        text_count:  len(r.texts),
        paths:       path_xml
    })
}
