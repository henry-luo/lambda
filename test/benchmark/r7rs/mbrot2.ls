// R7RS Benchmark: mbrot (Typed version)
// Mandelbrot set generation
// Adapted from r7rs-benchmarks/src/mbrot.scm: 1 iteration on 75x75 grid
// Expected: count at (0,0) = 5

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

pn count(r: float, i: float, step: float, x: float, y: float) {
    var max_count: int = 64
    var radius2: float = 16.0
    var cr: float = r + x * step
    var ci: float = i + y * step
    var zr: float = cr
    var zi: float = ci
    var c: int = 0
    while (c < max_count) {
        var zr2: float = zr * zr
        var zi2: float = zi * zi
        if (zr2 + zi2 > radius2) {
            return c
        }
        var new_zr: float = zr2 - zi2 + cr
        zi = 2.0 * zr * zi + ci
        zr = new_zr
        c = c + 1
    }
    return max_count
}

pn mbrot(matrix, r: float, i: float, step: float, n: int) {
    var y: int = n - 1
    while (y >= 0) {
        var x: int = n - 1
        while (x >= 0) {
            var row = matrix[x]
            row[y] = count(r, i, step, float(x), float(y))
            x = x - 1
        }
        y = y - 1
    }
}

pn test(n: int) {
    var matrix = make_array(n, null)
    var idx: int = 0
    while (idx < n) {
        matrix[idx] = make_array(n, 0)
        idx = idx + 1
    }
    mbrot(matrix, -1.0, -0.5, 0.005, n)
    var row0 = matrix[0]
    return row0[0]
}

pn benchmark() {
    var result: int = test(75)
    return result
}

pn main() {
    let result = benchmark()
    if (result == 5) {
        print("mbrot: PASS\n")
    } else {
        print("mbrot: FAIL result=")
        print(result)
        print("\n")
    }
}
