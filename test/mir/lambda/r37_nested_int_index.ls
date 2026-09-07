// Result37 (triangl2 1.65x): `board[mfrom[mi]] and board[mover[mi]] and (not board[mto[mi]])`
// `mfrom`/`mto` are declared `int[]` (their element reads carry the `int?` contract),
// `mover` is an untyped literal array (the benchmark's own shape).
pn scan(board: bool[], mfrom: int[], mto: int[], n: int) int {
    var mover = [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2]
    var hits = 0
    var mi = 0
    while (mi < n) {
        if (board[mfrom[mi]] and board[mover[mi]] and (not board[mto[mi]])) { hits = hits + 1 }
        board[mto[mi]] = false
        mi = mi + 1
    }
    return hits
}
pn main() {
    var board: bool[] = fill(15, true)
    var mfrom: int[] = fill(36, 1)
    var mto: int[] = fill(36, 3)
    print(scan(board, mfrom, mto, 36), "\n")
}
