// R7RS Benchmark: mbrot
// Mandelbrot set generation
// Adapted from r7rs-benchmarks/src/mbrot.scm: 1 iteration on 75x75 grid
// Expected: count at (0,0) = 5

pn count(r, i, step, x, y) {
    var max_count = 64
    var radius2 = 16.0
    var cr = r + x * step
    var ci = i + y * step
    var zr = cr
    var zi = ci
    var c = 0
    while (c < max_count) {
        var zr2 = zr * zr
        var zi2 = zi * zi
        if (zr2 + zi2 > radius2) {
            return c
        }
        var new_zr = zr2 - zi2 + cr
        zi = 2.0 * zr * zi + ci
        zr = new_zr
        c = c + 1
    }
    return max_count
}

pn mbrot(var matrix, r, i, step, n) {
    var y = n - 1
    while (y >= 0) {
        var x = n - 1
        while (x >= 0) {
            matrix[x][y] = count(r, i, step, float(x), float(y))
            x = x - 1
        }
        y = y - 1
    }
}

pn test(n) {
    // Create n x n matrix
    var matrix = fill(n, null)
    var idx = 0
    while (idx < n) {
        matrix[idx] = fill(n, 0)
        idx = idx + 1
    }
    mbrot(matrix, -1.0, -0.5, 0.005, n)
    var row0 = matrix[0]
    return row0[0]
}

pn benchmark() {
    var result = test(75)
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 5) {
        print("mbrot: PASS\n")
    } else {
        print("mbrot: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ elapsed ++ "\n")
}
