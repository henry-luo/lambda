// Phase 1 smoke test for the Lambda PDF package.
// Loads a small fixture PDF, renders the first page as standalone <svg>,
// and emits the result so we can verify the page becomes one <svg> with
// the right viewBox.
//
// Run as a procedural script (./lambda.exe run …) because the pdf API
// is `pn` (it drives the content-stream interpreter via var/while).

import pdf: lambda.package.pdf.pdf

pn main() {
    let doc^err = input("test/pdf/data/basic/test.pdf", 'pdf')
    let svg_el = pdf.pdf_to_svg(doc, 0, null)
    print({
        page_count: pdf.pdf_page_count(doc),
        svg: format(svg_el, 'xml')
    })
}
