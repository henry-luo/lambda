// AWFY Benchmark: Queens (Typed version)
// Expected result: true (solved 10 times)

pn get_row_column(free_rows, free_maxs, free_mins, r: int, c: int) {
    if (free_rows[r] and free_maxs[c + r] and free_mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn set_row_column(free_rows, free_maxs, free_mins, r: int, c: int, v) {
    free_rows[r] = v
    free_maxs[c + r] = v
    free_mins[c - r + 7] = v
}

pn place_queen(free_rows, free_maxs, free_mins, queen_rows, c: int) {
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

pn queens() {
    var free_rows = fill(8, true)
    var free_maxs = fill(16, true)
    var free_mins = fill(16, true)
    var queen_rows = fill(8, -1)
    return place_queen(free_rows, free_maxs, free_mins, queen_rows, 0)
}

pn benchmark() {
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
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
