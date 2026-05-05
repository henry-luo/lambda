// Phase 13 — scaled rotated text matrices keep native effective font size.
//
// The native C++ path computes text size as
//   Tf_size * sqrt(Tm.a^2 + Tm.b^2)
// so a scaled 90-degree text matrix [0 2 -2 0 ...] renders at 2x size.

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
    return { state: st, emits: emits }
}


pn main() {
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [0, 2, -2, 0, 10, 20] },
        { op: "Tj", operands: [str("R")] },
        { op: "ET", operands: [] }
    ]
    let r = drive(ops, 300.0)
    print({ count: len(r.emits), xml: format(r.emits[0], 'xml') })
}
