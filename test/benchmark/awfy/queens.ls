// AWFY Benchmark: Queens (N-Queens problem)
// Expected result: true (solved 10 times, each placement validated)
// 8-Queens backtracking solver
// The board tables are `var` params: plain params are snapshots (S9.1.3), so
// a write-through solver must borrow them or it silently stops searching.

pn get_row_column(free_rows, free_maxs, free_mins, r, c) {
    if (free_rows[r] and free_maxs[c + r] and free_mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn set_row_column(var free_rows, var free_maxs, var free_mins, r, c, v) {
    free_rows[r] = v
    free_maxs[c + r] = v
    free_mins[c - r + 7] = v
}

pn place_queen(var free_rows, var free_maxs, var free_mins, var queen_rows, c) {
    for r in 0 to 7 {
        if (get_row_column(free_rows, free_maxs, free_mins, r, c) == 1) {
            queen_rows[r] = c
            set_row_column(free_rows, free_maxs, free_mins, r, c, false)
            if (c == 7) {
                return 1
            }
            if (place_queen(free_rows, free_maxs, free_mins, queen_rows, c + 1) == 1) {
                return 1
            }
            set_row_column(free_rows, free_maxs, free_mins, r, c, true)
        }
    }
    return 0
}

// a placement is valid when every row holds one queen and no two share a
// column or a diagonal; `result == 1` alone cannot tell a solver that
// stopped searching from one that solved the board
pn is_valid(queen_rows) {
    var r1 = 0
    while (r1 < 8) {
        let c1 = queen_rows[r1]
        if (c1 < 0 or c1 > 7) {
            return 0
        }
        var r2 = r1 + 1
        while (r2 < 8) {
            let c2 = queen_rows[r2]
            if (c1 == c2 or c1 - c2 == r2 - r1 or c2 - c1 == r2 - r1) {
                return 0
            }
            r2 = r2 + 1
        }
        r1 = r1 + 1
    }
    return 1
}

pn queens() {
    var free_rows = fill(8, true)
    var free_maxs = fill(16, true)
    var free_mins = fill(16, true)
    var queen_rows = fill(8, -1)
    if (place_queen(free_rows, free_maxs, free_mins, queen_rows, 0) != 1) {
        return 0
    }
    return is_valid(queen_rows)
}

pn benchmark() {
    var result = 1
    for i in 0 to 9 {
        if (queens() != 1) {
            result = 0
        }
    }
    return result
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 1) {
        print("Queens: PASS\n")
    } else {
        print("Queens: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
