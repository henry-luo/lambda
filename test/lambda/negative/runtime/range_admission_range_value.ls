// S11.1.3 (LR03-14): a range value is never a member of a range type. The JIT
// admitted one at a range-typed parameter and returned it.
fn dyn(v) => v
fn f(x: 1 to 5) { x }
"bound: " ++ string(f(dyn(1 to 5)))
