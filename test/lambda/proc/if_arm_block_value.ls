// S16.4.1v3: a braced arm is a block whose value is its last expression, in
// every expression position. MIR Direct discarded a boxed proc-mode `if`
// that was neither a tail value nor an initializer -- an operand, a call
// argument, a returned array element -- and produced null (2026-09-07).
pn f(i: int) any {
    let ints = [10, 20, 30]
    let a = (if (i == i) { ints[0] } else { ints[1] }) + 1
    let b = (if (i > 100) 5 else { ints[2] }) + 1
    let c = if (i == i) { ints[1] } else { 0 }
    let d = string(if (i >= 0) { "pos" } else { "neg" }) ++ "!"
    return [a, b, c, d, (if (i == 0) { 1 } else { 2 }) * 10]
}
pn main() {
    var i = 0
    var out = ""
    while (i < 7) { out = out ++ string(f(i)) ++ ";"; i = i + 1 }
    print(out, "\n")
}
