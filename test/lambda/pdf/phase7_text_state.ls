// Phase 7 — text-state operators (Tc/Tw/Ts/Tr).
//
// Synthetic op stream exercising:
//   - Tc 2.0 — character spacing (state mutation only)
//   - Tw 5.0 — word spacing (state mutation only)
//   - Ts 4.0 — text rise: positive shifts baseline up in PDF, so SVG y
//              should be (page_h - (Tm[5] + 4)).
//   - Tr 1   — stroked-only text: emitted <text> gets fill="none"
//              and stroke=<current stroke color>.
//   - Tr 3   — invisible text: no <text> emitted.

import text: lambda.package.pdf.text
import util: lambda.package.pdf.util

fn name(s)   { { kind: "name",   value: s } }
fn str(s)    { { kind: "string", value: s } }

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
    return {
        state: {
            tc: st.char_space, tw: st.word_space,
            ts: st.rise, tr: st.render_mode,
            stroke: st.stroke
        },
        emit_count: len(emits),
        emits: emits
    }
}

pn main() {
    let page_h = 800.0

    // Block A: set Tc/Tw/Ts then draw at (10, 100). Default Tr=0 (fill).
    let opsA = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 12] },
        { op: "Tc", operands: [2.0] },
        { op: "Tw", operands: [5.0] },
        { op: "Ts", operands: [4.0] },
        { op: "Td", operands: [10, 100] },
        { op: "Tj", operands: [str("rise")] },
        { op: "ET", operands: [] }
    ]
    let rA = drive(opsA, page_h)

    // Block B: Tr 1 — stroked-only text.
    let opsB = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tr", operands: [1] },
        { op: "Td", operands: [20, 200] },
        { op: "Tj", operands: [str("S")] },
        { op: "ET", operands: [] }
    ]
    let rB = drive(opsB, page_h)

    // Block C: Tr 3 — invisible text.
    let opsC = [
        { op: "BT", operands: [] },
        { op: "Tf", operands: [name("F1"), 10] },
        { op: "Tr", operands: [3] },
        { op: "Td", operands: [30, 300] },
        { op: "Tj", operands: [str("I")] },
        { op: "ET", operands: [] }
    ]
    let rC = drive(opsC, page_h)

    print({
        A_state: rA.state,
        A_emits: (for (e in rA.emits) format(e, 'xml')),
        B_emits: (for (e in rB.emits) format(e, 'xml')),
        C_emit_count: rC.emit_count
    })
}
