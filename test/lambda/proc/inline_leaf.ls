// T30-5: leaf functions emitted at their call sites must behave exactly like
// the call: early returns, null element reads in conditions, sentinel ints,
// float division, and reads the caller's dense loop never proved.
pn has_all(a: bool[], b: bool[], r: int, c: int) int {
    if (a[r] and b[c + r] and b[c - r + 7]) {
        return 1
    }
    return 0
}

pn eval_a(i: int, j: int) float {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn classify(x: int, y: int) int {
    if (x < y) {
        if (x < 0) {
            return 0 - 1
        }
        return x
    } else {
        if (y == x) {
            return 100
        }
    }
    return y * 2
}

pn pick(a: int[], i: int, flag: bool) int {
    if (flag and a[i] > 10) {
        return i + 1
    }
    return 0
}

pn scaled(x: float, k: int) float {
    return x * float(k) - 0.5
}

pn main() {
    var rows: bool[] = [true, false, true, true, true, true, true, true]
    var diag: bool[] = fill(16, true)
    var hits: int = 0
    var r: int = 0
    while (r < 8) {
        var c: int = 0
        while (c < 8) {
            hits = hits + has_all(rows, diag, r, c)
            c = c + 1
        }
        r = r + 1
    }
    print("hits=" ++ hits ++ "\n")
    // index 20 and -3 are out of range: the read is null, the test false
    print("oob=" ++ has_all(rows, diag, 2, 18) ++ " " ++ has_all(rows, diag, 5, 0 - 3) ++ "\n")
    var short_rows: bool[] = [true, true]
    var s: int = 0
    var k: int = 0
    while (k < 8) {
        s = s + has_all(short_rows, diag, k, 1)
        k = k + 1
    }
    print("short=" ++ s ++ "\n")
    var total: float = 0.0
    var i: int = 0
    while (i < 5) {
        var j: int = 0
        while (j < 5) {
            total = total + eval_a(i, j) * 2.0
            j = j + 1
        }
        i = i + 1
    }
    print("total=" ++ total ++ "\n")
    print("classify=" ++ classify(1, 2) ++ " " ++ classify(0 - 4, 2) ++ " " ++
        classify(3, 3) ++ " " ++ classify(5, 2) ++ "\n")
    var big: int = 9007199254740991
    var pinf: int = big + 1
    print("sentinel=" ++ classify(pinf, 2) ++ " " ++ classify(2, pinf) ++ " " ++ eval_a(pinf, 0) ++ "\n")
    var nums: int[] = [5, 20, 30]
    print("pick=" ++ pick(nums, 1, true) ++ " " ++ pick(nums, 0, true) ++ " " ++
        pick(nums, 2, false) ++ " " ++ pick(nums, 9, true) ++ "\n")
    print("nested=" ++ classify(classify(1, 2), pick(nums, 2, true)) ++ "\n")
    print("scaled=" ++ scaled(1.5, 4) ++ " " ++ (scaled(2.0, 3) + scaled(0.0, 0)) ++ "\n")
}
