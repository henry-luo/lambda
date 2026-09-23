// Array rank (S11.1.1v3): `T[]` holds when every element is a T, and repeated
// postfixes preserve rank -- `T[][]` is an array of `T[]`, never a flattened
// leaf array. So a nested array is not a flat `int[]`, and a flat array is not
// an `int[][]`. Nested numeric literals and `reshape` build N-D arrays whose
// elements are row views over one flat leaf lane; that lane is not the
// elements. `[[1]] is int[]` and `[1] is int[][]` had both held, and a 2-D
// reshape was admitted by `int[]` while `int[][]` rejected it.

fn dyn(v) => v
fn second(a: int[][]) { a[1] }
fn kind(v) { match v { case int[]: "flat" case int[][]: "nested" default: "other" } }

'1. a nested array is not a flat array'
"1.1"; [[[1]] is int[], [[1, 2]] is int[], [[1, 2], [3, 4]] is int[]]
"1.2"; [[1] is int[][], [1, 2] is int[][], [[1], 2] is int[][]]
"1.3"; [[[1]] is int[][], [[1, 2], [3]] is int[][], [[1, 2], [3, 4]] is int[][]]
"1.4"; [[] is int[][], [[]] is int[][], [[]] is int[]]
"1.5"; [[[1.5]] is float[], [[1.5]] is float[][], [[1]] is int?[], [[1, null]] is int?[][]]

'2. the rows of an N-D array are its elements'
let m = [[1, 2], [3, 4]]
let r = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
"2.1"; [m is int[][], m is int[], m[0] is int[], m[0] is int[][]]
"2.2"; [r is int[][][], r is int[][], r is int[], r[1] is int[][], r[1][0] is int[]]
"2.3"; [reshape([1.5, 2.5], [1, 2]) is float[][], reshape([1.5, 2.5], [1, 2]) is float[]]

'3. boundaries preserve rank'
let t: int[][] = [[1, 2], [3]]
let u: int[][] = dyn(m)
let v: int[][][] = dyn(r)
let w: int[] = dyn(r[1][0])
"3.1"; [t, u, v[1], w]
"3.2"; [second([[1], [2, 3]]), second(dyn(m)), second(dyn(reshape([5, 6, 7, 8], [2, 2])))]
"3.3"; [kind([1, 2]), kind([[1, 2]]), kind(m), kind(r[0]), kind(r), kind([["a"]])]
