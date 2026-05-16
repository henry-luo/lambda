// Phase 70 - PDF line width 0 emits an explicit SVG hairline.

import interp: lambda.package.pdf.interp

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: {} } }
    let ops = [
        { op: "w", operands: [0.0] },
        { op: "m", operands: [10.0, 20.0] },
        { op: "l", operands: [80.0, 20.0] },
        { op: "S", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        path_count: len(r.paths),
        width: has(xml, "stroke-width=\"1\""),
        vector_effect: has(xml, "vector-effect=\"non-scaling-stroke\"")
    })
}
