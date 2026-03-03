// Larceny Benchmark: puzzle
// N-Queens n=10 — count all solutions via backtracking
// Adapted from the classic Gabriel/Larceny "puzzle" benchmark
// Expected: 724 solutions

let BOARD_SIZE = 10

pn solve(row, cols, diag1, diag2, n) {
    if (row == n) {
        return 1
    }
    var count = 0
    var col = 0
    while (col < n) {
        var d1 = row + col
        var d2 = row - col + n - 1
        if ((not cols[col]) and (not diag1[d1]) and (not diag2[d2])) {
            cols[col] = true
            diag1[d1] = true
            diag2[d2] = true
            count = count + solve(row + 1, cols, diag1, diag2, n)
            cols[col] = false
            diag1[d1] = false
            diag2[d2] = false
        }
        col = col + 1
    }
    return count
}

pn benchmark() {
    var cols = fill(BOARD_SIZE, false)
    var diag1 = fill(BOARD_SIZE * 2, false)
    var diag2 = fill(BOARD_SIZE * 2, false)

    var result = solve(0, cols, diag1, diag2, BOARD_SIZE)
    return result
}

pn main() {
    let result = benchmark()
    if (result == 724) {
        print("puzzle: PASS\n")
    } else {
        print("puzzle: FAIL result=")
        print(result)
        print("\n")
    }
}
