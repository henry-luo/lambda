// S11.4.5 / S11.4.1v3 (LR03-11): an out-of-range `i8` declaration is rejected
// on every tier; the interpreter had bound 300 as 44.
fn dyn(v) => v
let x: i8 = dyn(300)
"bound: " ++ string(x)
