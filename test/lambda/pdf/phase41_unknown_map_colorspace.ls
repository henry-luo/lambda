// Phase 41 - unknown map color spaces fall back to DeviceGray like native C++.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: { MapCS: { Foo: "Bar" } } } }
    }
    let ops = [
        { op: "cs", operands: [name("MapCS")] },
        { op: "sc", operands: [0.25, 1.0, 0.0] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        count: len(r.paths),
        map_defaults_gray: has(xml, "fill=\"rgb(64,64,64)\""),
        not_rgb_count: not has(xml, "fill=\"rgb(64,255,0)\"")
    })
}
