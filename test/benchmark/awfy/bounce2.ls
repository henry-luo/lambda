// AWFY Benchmark: Bounce (Typed version)
// Expected result: 1331

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

pn random_next(seed_arr) {
    var s: int = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

pn benchmark() {
    var seed_arr = [74755]
    var ball_count: int = 100
    var bounces: int = 0

    var bx = make_array(ball_count, 0)
    var by = make_array(ball_count, 0)
    var bxv = make_array(ball_count, 0)
    var byv = make_array(ball_count, 0)

    var i: int = 0
    while (i < ball_count) {
        bx[i] = random_next(seed_arr) % 500
        by[i] = random_next(seed_arr) % 500
        bxv[i] = (random_next(seed_arr) % 300) - 150
        byv[i] = (random_next(seed_arr) % 300) - 150
        i = i + 1
    }

    i = 0
    while (i < 50) {
        var j: int = 0
        while (j < ball_count) {
            bx[j] = bx[j] + bxv[j]
            by[j] = by[j] + byv[j]

            var bounced: int = 0
            if (bx[j] > 500) {
                bx[j] = 500
                var axv: int = abs(bxv[j])
                bxv[j] = 0 - axv
                bounced = 1
            }
            if (bx[j] < 0) {
                bx[j] = 0
                var axv2: int = abs(bxv[j])
                bxv[j] = axv2
                bounced = 1
            }
            if (by[j] > 500) {
                by[j] = 500
                var ayv: int = abs(byv[j])
                byv[j] = 0 - ayv
                bounced = 1
            }
            if (by[j] < 0) {
                by[j] = 0
                var ayv2: int = abs(byv[j])
                byv[j] = ayv2
                bounced = 1
            }
            bounces = bounces + bounced
            j = j + 1
        }
        i = i + 1
    }
    return bounces
}

pn main() {
    let result = benchmark()
    if (result == 1331) {
        print("Bounce: PASS\n")
    } else {
        print("Bounce: FAIL result=")
        print(result)
        print("\n")
    }
}
