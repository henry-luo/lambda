// S11.2.1 / S11.4.1v3 (LR07-38): a literal parameter contract checks the
// argument's value; the JIT's native int argument skipped it.
fn g(x: 1) { x }
"bound: " ++ string(g(2))
