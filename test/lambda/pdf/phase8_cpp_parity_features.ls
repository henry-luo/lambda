// Phase 8 — C++ PDF view parity feature fixture.
//
// Covers features that existed in the C++ view path and were added to the
// Lambda PDF package: named ColorSpace lookup for Indexed/DeviceGray and
// SVG emission of line cap/join/miter/dash stroke state.

import pdf: lambda.package.pdf.pdf

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc^err = input("test/input/pdf_cpp_parity_features.pdf", 'pdf')
    let svg = format(pdf.pdf_to_svg(doc, 0, { show_label: false }), 'xml')
    print({
        pages: pdf.pdf_page_count(doc),
        indexed_first: has(svg, "fill=\"rgb(65,66,67)\""),
        indexed_second: has(svg, "fill=\"rgb(68,69,70)\""),
        device_gray: has(svg, "fill=\"rgb(128,128,128)\""),
        linecap_square: has(svg, "stroke-linecap=\"square\""),
        linejoin_round: has(svg, "stroke-linejoin=\"round\""),
        miterlimit: has(svg, "stroke-miterlimit=\"3.5\""),
        dasharray: has(svg, "stroke-dasharray=\"12,4\""),
        text: has(svg, "CPP parity feature PDF")
    })
}
