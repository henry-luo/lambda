// Phase 36 — Indexed color spaces resolve named device bases recursively.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let lookup = chr(255) ++ chr(255) ++ chr(255) ++ chr(0) ++
                 chr(0) ++ chr(255) ++ chr(255) ++ chr(0)
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            Base4: "DeviceCMYK",
            Idx: ["Indexed", "Base4", 1, lookup]
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("Idx")] },
        { op: "sc", operands: [1] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        indexed_named_base: has(xml, "fill=\"rgb(255,0,0)\""),
        not_unresolved_base: not has(xml, "fill=\"rgb(0,0,255)\"")
    })
}
