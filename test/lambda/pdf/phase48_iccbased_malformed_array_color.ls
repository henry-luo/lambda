// Phase 48 - malformed ICCBased arrays keep native black fallback.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: { Resources: { ColorSpace: {
            MissingStream: ["ICCBased"],
            NonMapStream: ["ICCBased", "DeviceRGB"],
            InvalidN: ["ICCBased", { N: "bad" }]
        } } }
    }
    let ops = [
        { op: "cs", operands: [name("MissingStream")] },
        { op: "sc", operands: [0.0, 1.0, 0.0] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("NonMapStream")] },
        { op: "sc", operands: [1.0, 0.0, 0.0] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("InvalidN")] },
        { op: "sc", operands: [0.5, 0.5, 0.5] },
        { op: "re", operands: [40, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml0 = format(r.paths[0], 'xml')
    let xml1 = format(r.paths[1], 'xml')
    let xml2 = format(r.paths[2], 'xml')
    print({
        count: len(r.paths),
        missing_stream_black: has(xml0, "fill=\"rgb(0,0,0)\""),
        non_map_black: has(xml1, "fill=\"rgb(0,0,0)\""),
        invalid_n_black: has(xml2, "fill=\"rgb(0,0,0)\""),
        not_missing_rgb: not has(xml0, "fill=\"rgb(0,255,0)\""),
        not_non_map_rgb: not has(xml1, "fill=\"rgb(255,0,0)\""),
        not_invalid_gray: not has(xml2, "fill=\"rgb(128,128,128)\"")
    })
}
