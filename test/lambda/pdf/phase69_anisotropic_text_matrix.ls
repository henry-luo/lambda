// Phase 69 - anisotropic and sheared PDF text matrices preserve glyph scale.

import text: lambda.package.pdf.text
import util: lambda.package.pdf.util

fn name(s) { { kind: "name", value: s } }
fn str(s)  { { kind: "string", value: s } }

pn drive(ops, page_h) {
    var st = text.new_state(null)
    var emits = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let r = text.apply_op(st, util.IDENTITY, ops[i].op, ops[i].operands, page_h)
        st = r.state
        if (r.emit != null) {
            var k = 0
            let m = len(r.emit)
            while (k < m) {
                emits = emits ++ [r.emit[k]]
                k = k + 1
            }
        }
        i = i + 1
    }
    return emits
}

pn main() {
    let stretch_ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [3, 0, 0, 1, 20, 40] },
        { op: "Tj", operands: [str("A")] },
        { op: "ET", operands: [] }
    ]
    let shear_ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0.2, 1, 30, 50] },
        { op: "Tj", operands: [str("I")] },
        { op: "ET", operands: [] }
    ]
    let stretch = drive(stretch_ops, 100.0)
    let shear = drive(shear_ops, 100.0)
    print({ stretch: format(stretch[0], 'xml'), shear: format(shear[0], 'xml') })
}
