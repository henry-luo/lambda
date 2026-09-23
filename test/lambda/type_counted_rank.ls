// Counted array ranks (S11.1.1v3): repeated array postfixes preserve rank and
// apply left to right, so `int[2][3]` is three arrays of two and `int[][3]` is
// three arrays of any length. Only an empty pair could follow an array suffix,
// so both failed with E103 -- a rule from when `T[n]` was an occurrence count;
// S11.1.6v2 made it an array and moved the run count to `T{n}`.

type Grid = int[2][3]
type Rows = int[][3]
fn third(g: int[2][3]) { g[2] }
fn shape(v) { match v { case int[2][3]: "3 x 2" case int[3][2]: "2 x 3" default: "other" } }

let m = [[1, 2], [3, 4], [5, 6]]

'1. every spelling parses and means the same grid'
"1.1"; [m is Grid, m is int[2][3], m is (int[2])[3], m is Rows, m is int[2][]]

'2. each layer counts its own axis'
"2.1"; [[[1, 2], [3, 4]] is Grid, [[1, 2, 3], [4, 5, 6]] is Grid, [[1, 2, 3], [4, 5, 6]] is int[3][2]]
"2.2"; [[[1, 2], [3, 4], [5, 6, 7]] is Grid, [1, 2, 3] is Grid, [] is Grid]
"2.3"; [[[1], [2, 3], []] is Rows, [[1, 2], [3, 4]] is Rows, [[1, 2], [3]] is int[2][]]
"2.4"; [[[[1, 2]]] is int[2][1][1], [[[1, 2]]] is int[2][1][2], [[]] is int[0][1]]

'3. N-D arrays, other leaves, and nullable layers'
"3.1"; [reshape([1, 2, 3, 4, 5, 6], [3, 2]) is Grid, reshape([1, 2, 3, 4, 5, 6], [2, 3]) is Grid]
"3.2"; [[["a", "b"], ["c", "d"], ["e", "f"]] is string[2][3], [["a", "b"], ["c"]] is string[2][2]]
"3.3"; [null is Grid?, 5 is Grid?, [[1, null], [null, 2], [3, 4]] is int?[2][3]]
"3.4"; [[[1, 2], null, [3, 4]] is int[2]?[3], [[1, 2], [3]] is int[2]?[2]]

'4. annotations, parameters and match arms'
let g: int[2][3] = [[1, 2], [3, 4], [5, 6]]
"4.1"; [g, third(m), third([[7, 8], [9, 10], [11, 12]])]
"4.2"; [shape(m), shape([[1, 2, 3], [4, 5, 6]]), shape([1, 2])]
