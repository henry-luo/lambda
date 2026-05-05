// Phase 40 — inline images resolve named page ColorSpace resources.

import interp: lambda.package.pdf.interp

fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let info = {
        kind: "inline_image",
        dict: [
            { key: "W", value: 1 },
            { key: "H", value: 1 },
            { key: "CS", value: { kind: "name", value: "ICC4" } },
            { key: "BPC", value: 8 }
        ],
        data: chr(0) ++ chr(255) ++ chr(255) ++ chr(0)
    }
    let doc = { objects: [], pages: [] }
    let page = { dict: { Resources: { ColorSpace: { ICC4: { N: 4 } } } } }
    let ops = [
        { op: "q", operands: [] },
        { op: "cm", operands: [1.0, 0.0, 0.0, 1.0, 9.0, 10.0] },
        { op: "inline_image", operands: [info] },
        { op: "Q", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        resolved_icc: has(xml, "fill=\"rgb(255,0,0)\""),
        no_placeholder: not has(xml, "rgba(200,200,200,0.4)"),
        kept_ctm: has(xml, "transform=\"matrix(1 0 0 1 9 10)\"")
    })
}
