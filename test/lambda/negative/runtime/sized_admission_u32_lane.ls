// S11.4.1v3 (LR03-11): the JIT's native u32 boundary rejected -1 with a bare
// error and no diagnostic; the rejection now reports like every other boundary.
let xs = [-1]
let x: u32 = xs[0]
"bound: " ++ string(x)
