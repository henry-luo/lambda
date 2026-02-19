// R7RS Benchmark: fft (Typed version)
// Fast Fourier Transform from "Numerical Recipes in C"
// Adapted from r7rs-benchmarks/src/fft.scm: 1 iteration on 4096-element vector
// Expected: 0.0

let PI2 = 6.28318530717959

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

pn four1(data, n: int) {
    // Bit-reversal section
    var i: int = 0
    var j: int = 0
    while (i < n) {
        if (i < j) {
            var temp = data[i]
            data[i] = data[j]
            data[j] = temp
            temp = data[i + 1]
            data[i + 1] = data[j + 1]
            data[j + 1] = temp
        }
        var m: int = n / 2
        while (m >= 2 and j >= m) {
            j = j - m
            m = m / 2
        }
        j = j + m
        i = i + 2
    }

    // Danielson-Lanczos section
    var mmax: int = 2
    while (mmax < n) {
        var theta = PI2 / mmax
        var sin_half = sin(0.5 * theta)
        var wpr = -2.0 * sin_half * sin_half
        var wpi = sin(theta)

        var wr = 1.0
        var wi = 0.0
        var m2: int = 0
        while (m2 < mmax) {
            var ii: int = m2
            while (ii < n) {
                var jj: int = ii + mmax
                var tempr = wr * data[jj] - wi * data[jj + 1]
                var tempi = wr * data[jj + 1] + wi * data[jj]
                data[jj] = data[ii] - tempr
                data[jj + 1] = data[ii + 1] - tempi
                data[ii] = data[ii] + tempr
                data[ii + 1] = data[ii + 1] + tempi
                ii = ii + mmax + mmax
            }
            var new_wr = wr * wpr - wi * wpi + wr
            wi = wi * wpr + wr * wpi + wi
            wr = new_wr
            m2 = m2 + 2
        }
        mmax = mmax * 2
    }
}

pn benchmark() {
    var data = make_array(4096, 0.0)
    four1(data, 4096)
    var result = data[0]
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 0.0) {
        print("fft: PASS  ")
    } else {
        print("fft: FAIL result=")
        print(result)
        print(" ")
    }
    print(elapsed)
    print(" ms\n")
}
