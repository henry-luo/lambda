// AWFY Benchmark: Queens (Typed version)
// Expected result: true (solved 10 times, each placement validated)

pn get_row_column(free_rows: bool[], free_maxs: bool[], free_mins: bool[], r: int, c: int) int {
    if (free_rows[r] and free_maxs[c + r] and free_mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn set_row_column(var free_rows: bool[], var free_maxs: bool[], var free_mins: bool[],
                  r: int, c: int, v: bool) {
    free_rows[r] = v
    free_maxs[c + r] = v
    free_mins[c - r + 7] = v
}

pn place_queen(var free_rows: bool[], var free_maxs: bool[], var free_mins: bool[],
               var queen_rows: int[], c: int) int {
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
pn is_valid(queen_rows: int[]) int {
    var r1: int = 0
    while (r1 < 8) {
        let c1: int = queen_rows[r1]
        if (c1 < 0 or c1 > 7) {
            return 0
        }
        var r2: int = r1 + 1
        while (r2 < 8) {
            let c2: int = queen_rows[r2]
            if (c1 == c2 or c1 - c2 == r2 - r1 or c2 - c1 == r2 - r1) {
                return 0
            }
            r2 = r2 + 1
        }
        r1 = r1 + 1
    }
    return 1
}

pn queens() int {
    var free_rows: bool[] = fill(8, true)
    var free_maxs: bool[] = fill(16, true)
    var free_mins: bool[] = fill(16, true)
    var queen_rows: int[] = fill(8, -1)
    if (place_queen(free_rows, free_maxs, free_mins, queen_rows, 0) != 1) {
        return 0
    }
    return is_valid(queen_rows)
}

pn benchmark() int {
    var result: int = 1
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
