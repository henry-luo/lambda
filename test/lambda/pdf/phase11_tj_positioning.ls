// Phase 11 — TJ arrays preserve large text-position adjustments.
//
// C++ renders TJ arrays as text segments, flushing when a large negative
// adjustment creates a visible word/column gap. Lambda should not collapse
// such arrays into one text node at the original x position.

import text: lambda.package.pdf.text
import util: lambda.package.pdf.util

fn name(s) { { kind: "name", value: s } }
fn str(s)  { { kind: "string", value: s } }
fn arr(xs) { { kind: "array", value: xs } }

fn xml_at(xs, i) {
    if (i < len(xs)) { format(xs[i], 'xml') }
    else { "" }
}


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
    let page_h = 300.0
    let ops = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tm", operands: [1, 0, 0, 1, 10, 20] },
        { op: "TJ", operands: [arr([str("Hello"), -700, str("World")])] },
        { op: "ET", operands: [] }
    ]
    let r = drive(ops, page_h)
    print({
        emit_count: len(r.emits),
        first: xml_at(r.emits, 0),
        second: xml_at(r.emits, 1),
        final_x: r.state.tm[4]
    })
}