// AWFY Benchmark: Mandelbrot (Typed version)
// Expected result: 191 (for size 500)

pn mandelbrot() {
    var sz: int = 500
    var sum: int = 0
    var byte_acc: int = 0
    var bit_num: int = 0
    var y: int = 0

    while (y < sz) {
        var ci: float = (2.0 * y / sz) - 1.0
        var x: int = 0

        while (x < sz) {
            var zrzr: float = 0.0
            var zi: float = 0.0
            var zizi: float = 0.0
            var cr: float = (2.0 * x / sz) - 1.5

            var z: int = 0
            var not_done: int = 1
            var escape: int = 0
            while (not_done == 1 and z < 50) {
                var zr: float = zrzr - zizi + cr
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

            var did_flush: int = 0
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
    var r: int = mandelbrot()
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
