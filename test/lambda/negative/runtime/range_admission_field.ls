// S11.1.3 (LR03-18): a range-typed map field admits its members only. The
// field was laid out as a range pointer, so admitting even an in-range int
// read the int as a pointer and crashed.
fn dyn(v) => v
fn f(p: {a: 1 to 5}) { p.a }
"bound: " ++ string(f(dyn({a: 9})))
