// AWFY Benchmark: Queens (N-Queens problem)
// Expected result: true (solved 10 times)
// 8-Queens backtracking solver

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn get_row_column(free_rows, free_maxs, free_mins, r, c) {
    if (free_rows[r] and free_maxs[c + r] and free_mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn set_row_column(free_rows, free_maxs, free_mins, r, c, v) {
    free_rows[r] = v
    free_maxs[c + r] = v
    free_mins[c - r + 7] = v
}

pn place_queen(free_rows, free_maxs, free_mins, queen_rows, c) {
    var r = 0
    while (r < 8) {
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
        r = r + 1
    }
    return 0
}

pn queens() {
    var free_rows = make_array(8, true)
    var free_maxs = make_array(16, true)
    var free_mins = make_array(16, true)
    var queen_rows = make_array(8, -1)
    return place_queen(free_rows, free_maxs, free_mins, queen_rows, 0)
}

pn benchmark() {
    var result = 1
    var i = 0
    while (i < 10) {
        if (queens() != 1) {
            result = 0
        }
        i = i + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    if (result == 1) {
        print("Queens: PASS\n")
    } else {
        print("Queens: FAIL\n")
    }
}
