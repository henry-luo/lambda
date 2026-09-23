// S11.1.1v3: rank is part of an array type. A 2-D reshape is an `int[][]`
// whose elements are its rows; `int[]` admitted it because its flat leaf lane
// holds ints. The checker only defers a dynamic source, so the runtime check
// decides it, on every tier.
fn dyn(v) => v
let e: int[] = dyn(reshape([1, 2, 3, 4], [2, 2]))
"bound: " ++ string(e)
