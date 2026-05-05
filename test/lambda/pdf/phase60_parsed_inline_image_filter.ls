// Phase 60 - parsed BI/EI inline image filters are preserved and gated.

import stream: lambda.package.pdf.stream
import interp: lambda.package.pdf.interp

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let cs = ("q\n"
     ++ "BI\n"
     ++ "/W 1 /H 1 /CS /RGB /BPC 8 /Filter /FlateDecode ID\n"
     ++ "ABC\n"
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
        has_placeholder: has(xml, "rgba(200,200,200,0.4)"),
        not_raw_rgb: not has(xml, "fill=\"rgb(65,66,67)\"")
    })
}
