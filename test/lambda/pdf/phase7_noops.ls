// Phase 7 — marker / metadata no-op operators.
//
// Verifies that BMC, EMC, MP, DP, BDC, BX, EX, ri, i, d0, d1 are
// silently accepted (no fallthrough to the text catch-all and no
// effect on rendering output).

import interp:  lambda.package.pdf.interp
import resolve: lambda.package.pdf.resolve

pn main() {
    let doc^err = input("test/input/test.pdf", 'pdf')
    let page = resolve.page_at(doc, 0)

    let ops = [
        { op: "MP",  operands: [{kind: "name", value: "Tag"}] },
        { op: "DP",  operands: [{kind: "name", value: "Tag"}, {kind: "dict"}] },
        { op: "BMC", operands: [{kind: "name", value: "Tag"}] },
        { op: "BDC", operands: [{kind: "name", value: "Tag"}, {kind: "dict"}] },
        { op: "EMC", operands: [] },
        { op: "BX",  operands: [] },
        { op: "EX",  operands: [] },
        { op: "ri",  operands: [{kind: "name", value: "Perceptual"}] },
        { op: "i",   operands: [1.5] },
        { op: "d0",  operands: [100, 200] },
        { op: "d1",  operands: [100, 200, 0, 0, 200, 200] }
    ]
    let r = interp.render_page(doc, page, ops, 800.0)
    print({
        path_count: len(r.paths),
        text_count: len(r.texts)
    })
}
