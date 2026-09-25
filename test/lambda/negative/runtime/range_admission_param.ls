// S11.1.3 (LR03-14): a range type admits the integers between its bounds. A
// parameter typed `1 to 5` statically rejected every integer instead, since
// the type wore the range VALUE tag (D3.1.1v4).
fn dyn(v) => v
fn f(x: 1 to 5) { x }
"bound: " ++ string(f(dyn(9)))
