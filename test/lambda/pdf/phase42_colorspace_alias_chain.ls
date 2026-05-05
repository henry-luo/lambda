// Phase 42 - named ColorSpace aliases resolve recursively like native C++.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            A: "B",
            B: "DeviceCMYK"
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("A")] },
        { op: "sc", operands: [0.0, 1.0, 1.0, 0.0] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        alias_chain: has(xml, "fill=\"rgb(255,0,0)\""),
        not_rgb_fallback: not has(xml, "fill=\"rgb(0,255,255)\"")
    })
}
