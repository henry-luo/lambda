// S11.1.1v3: rank is part of an array type on the element-wise admission path
// too. A contract with no exact packed lane admits a view or an N-D array by
// presenting its elements -- a 2-D array's elements are its rows, never its
// leaves -- so `number[]` still rejects a matrix, on every tier.
fn dyn(v) => v
let e: number[] = dyn(reshape([1, 2, 3, 4], [2, 2]))
"bound: " ++ string(e)
