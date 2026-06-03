// Phase 67 - generated PDF HTML has page chrome and no page labels by default.

import pdf: lambda.package.pdf.pdf

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc^err = input("test/input/invoice.pdf", 'pdf')
    let html_default = format(pdf.pdf_to_html(doc, null), 'html')
    let html_label = format(pdf.pdf_to_html(doc, { show_label: true }), 'html')
    print({
        has_doc_bg: has(html_default, "background: #f1f3f5"),
        has_page_border: has(html_default, "border: 1px solid #d9dee3"),
        has_doc_padding: has(html_default, "padding: 16px"),
        has_page_margin: has(html_default, "margin: 0 auto 16px auto"),
        has_page_max_width: has(html_default, "max-width: 100%"),
        has_page_width: has(html_default, "style=\"width: 595.276px;\""),
        hides_label: not has(html_default, ">Page 1</text>"),
        opt_in_label: has(html_label, ">Page 1</text>")
    })
}
