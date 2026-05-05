// Phase 68 - ExtGState line-state entries update stroked path output.

import interp: lambda.package.pdf.interp

fn name(s) { { kind: "name", value: s } }
fn has(s: string, needle: string) { (index_of(s, needle) >= 0) }

pn main() {
    let doc = { objects: [], pages: [] }
    let page = {
        dict: {
            Resources: {
                ExtGState: {
                    GSLine: { LW: 3.0, LC: 1, LJ: 2, ML: 4.5, D: [[5, 2], 1] }
                }
            }
        }
    }
    let ops = [
        { op: "gs", operands: [name("GSLine")] },
        { op: "m", operands: [10.0, 20.0] },
        { op: "l", operands: [80.0, 20.0] },
        { op: "S", operands: [] }
    ]
    let r = interp.render_page(doc, page, ops, 100.0)
    let xml = format(r.paths[0], 'xml')
    print({
        path_count: len(r.paths),
        width: has(xml, "stroke-width=\"3\""),
        cap: has(xml, "stroke-linecap=\"round\""),
        join: has(xml, "stroke-linejoin=\"bevel\""),
        miter: has(xml, "stroke-miterlimit=\"4.5\""),
        dash: has(xml, "stroke-dasharray=\"5,2\""),
        offset: has(xml, "stroke-dashoffset=\"1\"")
    })
}
