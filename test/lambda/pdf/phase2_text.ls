// Phase 2 — text rendering pipeline (stream → interp → svg).
//
// Renders test.pdf's first page and asserts the decoded text strings
// and their SVG y-coordinates (computed via page_h - Tm[5]).

import pdf:     lambda.package.pdf.pdf
import resolve: lambda.package.pdf.resolve
import stream:  lambda.package.pdf.stream
import interp:  lambda.package.pdf.interp
import coords:  lambda.package.pdf.coords

pn main() {
    let doc^err = input("test/pdf/data/basic/test.pdf", 'pdf')
    let page = resolve.page_at(doc, 0)
    let rect = coords.media_box_rect(page)
    let bytes = resolve.page_content_bytes(doc, page)
    let ops = stream.parse_content_stream(bytes)
    let elements = interp.render_page(doc, page, ops, rect.h)

    var info = []
    var i = 0
    let n = len(elements)
    while (i < n) {
        let el = elements[i]
        info = info ++ [{
            x: el.x,
            y: el.y,
            family: el['font-family'],
            size: el['font-size'],
            text: format(el, 'xml')
        }]
        i = i + 1
    }

    print({
        page_w: rect.w,
        page_h: rect.h,
        text_count: n,
        elements: info
    })
}
