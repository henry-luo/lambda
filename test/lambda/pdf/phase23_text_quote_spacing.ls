// Phase 23 — text showing advances and double-quote spacing operands.

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
        { op: "TL", operands: [12] },
        { op: "Td", operands: [10, 100] },
        { op: "Tj", operands: [str("AB")] },
        { op: "Tj", operands: [str("C")] },
        { op: "\"", operands: [7, 3, str("A B")] },
        { op: "ET", operands: [] }
    ]
    let r = drive(ops, 300.0)
    print({
        emit_count: len(r.emits),
        first: format(r.emits[0], 'xml'),
        second: format(r.emits[1], 'xml'),
        third: format(r.emits[2], 'xml'),
        final_x: r.state.tm[4],
        final_y: r.state.tm[5],
        tw: r.state.word_space,
        tc: r.state.char_space
    })
}
