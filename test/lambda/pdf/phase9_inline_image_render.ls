// Phase 9 — inline image rendering.
//
// Parses a tiny unfiltered BI..ID..EI inline image and drives the normal
// interpreter path. Supported 8-bit RGB inline images should render as
// SVG pixel rects instead of the older placeholder rectangle.

import stream: lambda.package.pdf.stream
import interp: lambda.package.pdf.interp

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let cs = ("q\n"
     ++ "2 0 0 1 10 20 cm\n"
     ++ "BI\n"
     ++ "/W 2 /H 1 /CS /RGB /BPC 8 ID\n"
     ++ "ABCDEF\n"
     ++ "EI\n"
     ++ "Q\n")
    let ops = stream.parse_content_stream(cs)
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: {} } }
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        op_count: len(ops),
        path_count: len(r.paths),
        has_first_pixel: has(xml, "fill=\"rgb(65,66,67)\""),
        has_second_pixel: has(xml, "fill=\"rgb(68,69,70)\""),
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        xml: xml
    })
}
