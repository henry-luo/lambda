// AWFY Benchmark: Mandelbrot
// Expected result: 191 (for size 500)
// Mandelbrot set fractal computation with bitwise operations

pn mandelbrot() {
    var sz = 500
    var sum = 0
    var byte_acc = 0
    var bit_num = 0
    var y = 0

    while (y < sz) {
        var ci = (2.0 * y / sz) - 1.0
        var x = 0

        while (x < sz) {
            var zrzr = 0.0
            var zi = 0.0
            var zizi = 0.0
            var cr = (2.0 * x / sz) - 1.5

            var z = 0
            var not_done = 1
            var escape = 0
            while (not_done == 1 and z < 50) {
                var zr = zrzr - zizi + cr
                zi = 2.0 * zr * zi + ci
                zrzr = zr * zr
                zizi = zi * zi
                if (zrzr + zizi > 4.0) {
                    not_done = 0
                    escape = 1
                }
                z = z + 1
            }

            byte_acc = shl(byte_acc, 1) + escape
            bit_num = bit_num + 1

            var did_flush = 0
            if (bit_num == 8) {
                sum = bxor(sum, byte_acc)
                byte_acc = 0
                bit_num = 0
                did_flush = 1
            }
            if (did_flush == 0) {
                if (x == sz - 1) {
                    byte_acc = shl(byte_acc, 8 - bit_num)
                    sum = bxor(sum, byte_acc)
                    byte_acc = 0
                    bit_num = 0
                }
            }
            x = x + 1
        }
        y = y + 1
    }
    return sum
}

pn benchmark() {
    var r = mandelbrot()
    return r
}

pn main() {
    let result = benchmark()
    if (result == 191) {
        print("Mandelbrot: PASS\n")
    } else {
        print("Mandelbrot: FAIL result=")
        print(result)
        print("\n")
    }
}
