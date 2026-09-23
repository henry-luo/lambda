// S11.1.1v3: postfixes apply left to right, so `int[2][3]` is three arrays of
// two, and every axis is counted. Three rows pass the outer count; the middle
// row's length fails the inner one, which the rank-only fast paths never read.
fn dyn(v) => v
let g: int[2][3] = dyn([[1, 2], [3, 4, 5], [6, 7]])
"bound: " ++ string(g)
