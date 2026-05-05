// Phase 25 — CalGray/CalRGB color spaces apply native C++ gamma fallback.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }
fn xml_at(items, i) { if (len(items) > i) format(items[i], 'xml') else "" }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: {
            Resources: {
                ColorSpace: {
                    CG: ["CalGray", { Gamma: 2.0 }],
                    CR: ["CalRGB", { Gamma: [2.0, 1.0, 0.5] }]
                }
            }
        }
    }
    let ops = [
        { op: "cs", operands: [name("CG")] },
        { op: "sc", operands: [0.5] },
        { op: "re", operands: [0, 0, 10, 10] },
        { op: "f", operands: [] },
        { op: "cs", operands: [name("CR")] },
        { op: "sc", operands: [0.5, 0.5, 0.25] },
        { op: "re", operands: [20, 0, 10, 10] },
        { op: "f", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let gray_xml = xml_at(r.paths, 0)
    let rgb_xml = xml_at(r.paths, 1)
    print({
        count: len(r.paths),
        cal_gray_gamma: has(gray_xml, "fill=\"rgb(64,64,64)\""),
        cal_rgb_gamma: has(rgb_xml, "fill=\"rgb(64,128,128)\""),
        not_plain_gray: not has(gray_xml, "fill=\"rgb(128,128,128)\""),
        gray: gray_xml,
        rgb: rgb_xml
    })
}
