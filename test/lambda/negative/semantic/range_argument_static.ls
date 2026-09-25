// S11.1.3 (LR03-14): a range value can never be a member of a range type, so
// passing one is a compile error; the JIT used to admit it.
fn f(x: 1 to 5) { x }
f(1 to 5)
