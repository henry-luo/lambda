// document-wide SVG references must retain the page and embedding namespaces.
import pdf: lambda.pdf.pdf
import interp: lambda.pdf.interp

fn has(s: string, needle: string) => index_of(s, needle) != null

pn main() {
    let page = { MediaBox: [0, 0, 100, 100], Resources: {} }
    let ops = [
        { op: "re", operands: [0, 0, 20, 20] },
        { op: "W", operands: [] }, { op: "n", operands: [] },
        { op: "re", operands: [0, 0, 100, 100] }, { op: "f", operands: [] }
    ]
    let a = format(interp.render_page_with_prefix({}, page, ops, 100, [], "first-clip").paths, 'xml')
    let b = format(interp.render_page_with_prefix({}, page, ops, 100, [], "second-clip").paths, 'xml')
    let parsed = input("test/pdf/PrinceCatalogue.pdf", 'pdf') ^ { null }
    let html = format(pdf.pdf_to_html(parsed, { id_prefix: "catalogue", show_label: false }), 'html')
    print({
        first_clip: has(a, "id=\"first-clip0\"") and has(a, "url(#first-clip0)"),
        second_clip: has(b, "id=\"second-clip0\"") and has(b, "url(#second-clip0)"),
        separate: not has(a, "second-clip") and not has(b, "first-clip"),
        page0: has(html, "id=\"catalogue-page-0-clip0\""),
        page1: has(html, "id=\"catalogue-page-1-clip0\""),
        pattern: has(html, "id=\"catalogue-page-1-clippat1\""),
        no_unscoped: not has(html, "id=\"clip0\"") and not has(html, "url(#clip0)")
    })
}
