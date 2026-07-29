// BENG Benchmark: mandelbrot (Typed version)
// Generate Mandelbrot set as PBM bitmap.
// Since Lambda cannot output raw binary bytes, we compute a byte-level
// checksum of what the PBM P4 body would contain (XOR of all packed bytes).
// The checksum can be verified against the reference PBM file.
// N=500 expected checksum: see mandelbrot.txt

let N = 500

pn main() {
    var __t0 = clock()
    var sz: int = N
    var checksum: int = 0
    var byte_acc: int = 0
    var bit_num: int = 0
    var y: int = 0

    while (y < sz) {
        var ci: float = (2.0 * float(y) / float(sz)) - 1.0
        var x: int = 0

        while (x < sz) {
            var zr: float = 0.0
            var zi: float = 0.0
            var zrzr: float = 0.0
            var zizi: float = 0.0
            var cr: float = (2.0 * float(x) / float(sz)) - 1.5

            var iter: int = 0
            var escape: int = 0
            while (iter < 50 and escape == 0) {
                var new_zr: float = zrzr - zizi + cr
                zi = 2.0 * zr * zi + ci
                zr = new_zr
                zrzr = zr * zr
                zizi = zi * zi
                if (zrzr + zizi > 4.0) {
                    escape = 1
                }
                iter = iter + 1
            }

            // in the set → bit 1, escaped → bit 0 (PBM P4 convention: 1=black)
            if (escape == 0) {
                byte_acc = bor(shl(byte_acc, 1), 1)
            } else {
                byte_acc = shl(byte_acc, 1)
            }
            bit_num = bit_num + 1

            if (bit_num == 8) {
                checksum = bxor(checksum, byte_acc)
                byte_acc = 0
                bit_num = 0
            } else {
                if (x == sz - 1) {
                    byte_acc = shl(byte_acc, 8 - bit_num)
                    checksum = bxor(checksum, byte_acc)
                    byte_acc = 0
                    bit_num = 0
                }
            }
            x = x + 1
        }
        y = y + 1
    }
    print(checksum ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
