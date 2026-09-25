// S11.4.5 (LR03-11): a `u64` declaration admits by value; the interpreter had
// wrapped -1 to 2^64 - 1.
fn dyn(v) => v
let a: u64 = dyn(-1)
"bound: " ++ string(a)
