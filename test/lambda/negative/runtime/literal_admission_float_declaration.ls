// S11.2.1 / S11.4.1v3 (LR07-38): as with an int literal, the float lane does not
// prove `2.5`; the JIT bound 1.5.
let f: 2.5 = 1.5
"bound: " ++ string(f)
