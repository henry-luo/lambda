// S11.1.1v3 on a packed N-D array: `int[2][3]` needs a 3 x 2 shape. A 3 x 3
// reshape has the right rank and leaf lane and passes the outer count, so
// only the inner axis of its shape rejects it.
fn dyn(v) => v
let m: int[2][3] = dyn(reshape([1, 2, 3, 4, 5, 6, 7, 8, 9], [3, 3]))
"bound: " ++ string(m)
