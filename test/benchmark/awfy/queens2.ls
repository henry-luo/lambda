// AWFY Benchmark: Queens (Typed version)
// Expected result: true (solved 10 times)

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain: int = n - sz
        var extra = [val]
        var esz: int = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

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
    var r: int = 0
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
    var result: int = 1
    var i: int = 0
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
