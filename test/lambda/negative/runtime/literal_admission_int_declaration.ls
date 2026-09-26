// S11.2.1 / S11.4.1v3 (LR07-38): a one-value literal contract checks the value,
// not its carrier. The JIT took the int lane as proof, so `let m: 1 = k` bound
// 2 there while the interpreter rejected it.
let k = 2
let m: 1 = k
"bound: " ++ string(m)
