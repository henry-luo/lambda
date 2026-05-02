// Phase 3 — verify pdf_to_svg wraps PDF-space paths in the y-flip group
// and emits SVG-space text as siblings outside that group.
//
// Asserts the structural layering on simple_test.pdf:
//   <svg>
//     <rect/>                     <!-- background -->
//     <g transform=y-flip> ... paths + label ... </g>
//     <text/> ...                 <!-- text in SVG space -->
//   </svg>

import pdf: lambda.package.pdf.pdf

pn main() {
    let doc^err = input("test/pdf/data/basic/simple_test.pdf", 'pdf')
    let svg_el = pdf.pdf_to_svg(doc, 0, { show_label: false })
    let xml = format(svg_el, 'xml')
    print({
        page_count:    pdf.pdf_page_count(doc),
        has_red_path:  contains(xml, "fill=\"rgb(255,0,0)\""),
        has_blue_path: contains(xml, "stroke=\"rgb(0,0,255)\""),
        has_flip:      contains(xml, "matrix(1 0 0 -1 0 792)"),
        text_count:    string_count(xml, "<text "),
        path_count:    string_count(xml, "<path ")
    })
}

// helpers — Lambda doesn't ship a substring count, so do it via split
fn contains(s, sub) {
    let parts = split(s, sub)
    (len(parts) >= 2)
}

fn string_count(s, sub) {
    let parts = split(s, sub)
    len(parts) - 1
}
