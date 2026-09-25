// S11.4.5 / S11.4.1v3 (LR03-11): a sized-int boundary admits by value. The
// interpreter coerced -1 into a `u8` parameter as 255, and the JIT's rejection
// named the contract "num_sized".
fn dyn(v) => v
fn f(x: u8) { x }
"bound: " ++ string(f(dyn(-1)))
