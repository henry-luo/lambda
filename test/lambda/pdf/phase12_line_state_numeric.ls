// Phase 12 — line-state numeric operands match native parser behavior.
//
// PDF integer-valued operators such as J and j still use PDF numbers, so
// producers may emit 2.0 J / 1.0 j. The C++ parser casts those numeric
// operands to int; Lambda should honor them rather than ignoring floats.

import path: lambda.package.pdf.path

fn arr(items) { { kind: "array", value: items } }

pn run_ops(ops) {
    var st = path.new_state()
    var out = []
    var i = 0
    let n = len(ops)
    while (i < n) {
        let r = path.apply_op(st, ops[i].op, ops[i].operands)
        st = r.state
        if (len(r.emit) > 0) {
            var k = 0
            let m = len(r.emit)
            while (k < m) {
                out = out ++ [r.emit[k]]
                k = k + 1
            }
        }
        i = i + 1
    }
    return { state: st, emit: out }
}

pn main() {
    let ops = [
        { op: "w", operands: [3.0] },
        { op: "J", operands: [2.0] },
        { op: "j", operands: [1.0] },
        { op: "M", operands: [4.5] },
        { op: "d", operands: [arr([6.0, 2.0]), 1.0] },
        { op: "m", operands: [0, 0] },
        { op: "l", operands: [20, 0] },
        { op: "S", operands: [] }
    ]
    let r = run_ops(ops)
    print({
        count: len(r.emit),
        line_cap: r.state.line_cap,
        line_join: r.state.line_join,
        xml: format(r.emit[0], 'xml')
    })
}