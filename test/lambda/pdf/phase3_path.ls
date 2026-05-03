// Phase 3 — path construction & painting smoke test.
//
// Uses path.ls directly with synthetic operator records so the test
// is fully deterministic (no PDF fixture dependency).
//
// Exercises:
//   - color: rg, RG, g
//   - rect:  re + f          (filled rectangle)
//   - line:  w + m + l + S   (stroked diagonal)
//   - curve: c               (cubic Bezier as part of a stroked path)
//   - h:     close subpath

import path:  lambda.package.pdf.path
import color: lambda.package.pdf.color

// helper to build an operand record matching stream.ls's shape
fn name(s)     { { kind: "name",   value: s } }
fn arr(items)  { { kind: "array",  value: items } }

pn run_ops(st, ops) {
    var s = st
    var out = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let r = path.apply_op(s, ops[i].op, ops[i].operands)
        s = r.state
        if (len(r.emit) > 0) {
            var k = 0
            let me = len(r.emit)
            while (k < me) {
                out = out ++ [r.emit[k]]
                k = k + 1
            }
        }
        i = i + 1
    }
    return { state: s, emit: out }
}

pn main() {
    // Sequence A: filled red rectangle
    let st0 = path.set_fill_color(path.new_state(), color.rgb(1.0, 0.0, 0.0))
    let opsA = [
        { op: "re", operands: [10, 20, 100, 50] },
        { op: "f",  operands: [] }
    ]
    let rA = run_ops(st0, opsA)

    // Sequence B: black 2pt stroked diagonal line
    let opsB = [
        { op: "w", operands: [2.0] },
        { op: "m", operands: [0, 0] },
        { op: "l", operands: [50, 50] },
        { op: "S", operands: [] }
    ]
    let rB = run_ops(rA.state, opsB)

    // Sequence C: cubic Bezier triangle, closed + filled+stroked (B operator)
    let opsC = [
        { op: "m", operands: [0, 0] },
        { op: "c", operands: [10, 20, 30, 20, 40, 0] },
        { op: "l", operands: [40, 40] },
        { op: "h", operands: [] },
        { op: "B", operands: [] }
    ]
    let rC = run_ops(rB.state, opsC)

    // Sequence D: f* (evenodd) on an empty path produces no element
    let rD = run_ops(rC.state, [{ op: "f*", operands: [] }])

    // Final state line-width should remain 2.0 (last w was 2.0)
    print({
        a_count:    len(rA.emit),
        a_xml:      format(rA.emit[0], 'xml'),
        b_count:    len(rB.emit),
        b_xml:      format(rB.emit[0], 'xml'),
        c_count:    len(rC.emit),
        c_xml:      format(rC.emit[0], 'xml'),
        d_count:    len(rD.emit),
        line_width: rD.state.line_width,
        fill_color: rD.state.fill_color
    })
}
