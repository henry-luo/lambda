// Counted array ranks (S11.1.1v3): repeated array postfixes preserve rank and
// apply left to right, so `int[2][3]` is three arrays of two and `int[][3]` is
// three arrays of any length. Only an empty pair could follow an array suffix,
// so both failed with E103 -- a rule from when `T[n]` was an occurrence count;
// S11.1.6v2 made it an array and moved the run count to `T{n}`.

type Grid = int[2][3]
type Rows = int[][3]
fn third(g: int[2][3]) { g[2] }
fn shape(v) { match v { case int[2][3]: "3 x 2" case int[3][2]: "2 x 3" default: "other" } }
fn dyn(v) => v
fn ok3(x: int[3]) { x }
fn any_len(x: int[]) { len(x) }
fn rows(g: int[2][3]) { [len(g), len(g[0])] }

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

'5. typed boundaries admit exactly what `is` accepts'
// A dynamic source leaves every count to the runtime check. Its lane, rank
// and certificate fast paths once proved `int[3]` from the leaf lane alone;
// the wrong-length cases are the negative array_count_* fixtures.
let a3: int[3] = dyn([1, 2, 3])
let f2: float[2] = dyn([1.5, 2.5])
let s2: string[2] = dyn(["a", "b"])
let n2: int?[2] = dyn([1, null])
let y2: any[2] = dyn([1, "x"])
let b1: bool[1] = dyn([true])
let r3: int[3] = dyn(1 to 3)
let e0: int[0] = dyn([])
"5.1"; [a3, f2, s2, n2, y2, b1, r3, e0]
let gd: int[2][3] = dyn([[1, 2], [3, 4], [5, 6]])
let nd: int[2][3] = dyn(reshape([1, 2, 3, 4, 5, 6], [3, 2]))
let open_rows: int[][3] = dyn([[1], [2, 3], []])
let open_count: int[2][] = dyn([[1, 2], [3, 4], [5, 6], [7, 8]])
"5.2"; [gd, nd, open_rows, open_count]
// a certificate for `int[]` never proves `int[3]`, nor the reverse: each
// crossing re-checks the length it needs
let u: int[] = dyn([7, 8, 9])
"5.3"; [ok3(a3), ok3(u), any_len(a3), rows(gd), rows(nd)]
