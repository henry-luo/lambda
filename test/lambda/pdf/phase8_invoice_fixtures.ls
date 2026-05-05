// Phase 8 — real invoice fixtures.
//
// Smoke-tests both real invoice PDFs through the Lambda PDF package and
// checks for stable structural markers rather than comparing full HTML.

import pdf: lambda.package.pdf.pdf

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn summarize(path: string) {
    let doc^err = input(path, 'pdf')
    let html = format(pdf.pdf_to_html(doc, { show_label: false }), 'html')
    return {
        pages: pdf.pdf_page_count(doc),
        has_svg: has(html, "<svg"),
        has_text: has(html, "Invoice"),
        has_clip: has(html, "clip-path"),
        has_gradient: has(html, "linearGradient"),
        has_pattern_fill: has(html, "url(#clippat"),
        has_stroke_style: has(html, "stroke-linecap")
    }
}


pn main() {
    print({
        invoice: summarize("test/input/invoice.pdf"),
        invoice_sample: summarize("test/input/invoicesample.pdf")
    })
}
