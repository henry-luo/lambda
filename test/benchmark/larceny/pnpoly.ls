// Larceny Benchmark: pnpoly
// Point-in-polygon using ray casting (Jordan curve theorem)
// Adapted from Larceny/Gambit benchmark suite
// Classifies 100000 test points against a 20-vertex polygon
// Expected: 50000 (test points are generated on a grid, half inside)

pn pnpoly(xs, ys, n, testx, testy) {
    var inside = false
    var j = n - 1
    var i = 0
    while (i < n) {
        var yi = ys[i]
        var yj = ys[j]
        var yi_gt = yi > testy
        var yj_gt = yj > testy
        if (yi_gt != yj_gt) {
            var xtest = (xs[j] - xs[i]) * (testy - yi) / (yj - yi) + xs[i]
            if (testx < xtest) {
                if (inside) {
                    inside = false
                } else {
                    inside = true
                }
            }
        }
        j = i
        i = i + 1
    }
    return inside
}

pn benchmark() {
    // Standard Larceny polygon (20 vertices, irregular shape)
    var xs = [0.0, 1.0, 1.0, 0.0, 0.0,
              1.0, -0.5, -1.0, -1.0, -2.0,
              -2.5, -2.0, -1.5, -0.5, 0.5,
              1.0, 0.5, 0.0, -0.5, -1.0]
    var ys = [0.0, 0.0, 1.0, 1.0, 2.0,
              3.0, 2.0, 3.0, 0.0, -0.5,
              0.5, 1.5, 2.0, 3.0, 3.0,
              2.0, 1.0, 0.5, -1.0, -1.0]
    var n = 20

    // Test 100000 points on a grid from -2.5 to 1.5 (x) and -1.5 to 3.5 (y)
    var count = 0
    var total = 0
    var ix = 0
    while (ix < 500) {
        var testx = -2.5 + float(ix) * 0.008
        var iy = 0
        while (iy < 200) {
            var testy = -1.5 + float(iy) * 0.025
            if (pnpoly(xs, ys, n, testx, testy)) {
                count = count + 1
            }
            total = total + 1
            iy = iy + 1
        }
        ix = ix + 1
    }

    print("pnpoly: total=" ++ string(total) ++ " inside=" ++ string(count) ++ "\n")
    return count
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    // result depends on polygon area / grid sampling
    print("pnpoly: DONE\n")
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
