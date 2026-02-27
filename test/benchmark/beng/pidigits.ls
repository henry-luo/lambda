// BENG Benchmark: pidigits
// Compute digits of Pi using the unbounded spigot algorithm (Gibbons 2004)
// Uses decimal with N suffix for unlimited precision arithmetic
// N=30 expected: "3141592653\t:10\n5897932384\t:20\n6264338327\t:30\n"

let NUM_DIGITS = 30

// integer division for decimals: floor(a/b) = (a - a % b) / b
pn idiv(a, b) {
    return (a - a % b) / b
}

pn main() {
    // LFT state: (q, r, s, t) as arbitrary-precision decimals
    var q = 1N
    var r = 0N
    var s = 0N
    var t = 1N
    var k = 0N
    var i = 0
    var digits = ""

    while (i < NUM_DIGITS) {
        k = k + 1N
        var k2 = k * 2N + 1N

        // compose: multiply LFT by next term
        var new_q = q * k
        var new_r = (2N * q + r) * k2
        var new_s = s * k
        var new_t = (2N * s + t) * k2
        q = new_q
        r = new_r
        s = new_s
        t = new_t

        // can we extract a digit?
        if (q <= r) {
            var fd3 = idiv(3N * q + r, 3N * s + t)
            var fd4 = idiv(4N * q + r, 4N * s + t)
            if (fd3 == fd4) {
                digits = digits ++ string(fd3)
                i = i + 1

                // output 10 digits per line
                if (i % 10 == 0) {
                    print(digits ++ "\t:" ++ string(i) ++ "\n")
                    digits = ""
                }

                // reduce: eliminate the extracted digit
                r = (r - fd3 * t) * 10N
                q = q * 10N
            }
        }
    }

    // handle last partial line (fewer than 10 digits)
    if (len(digits) > 0) {
        // pad with spaces to 10 characters
        while (len(digits) < 10) {
            digits = digits ++ " "
        }
        print(digits ++ "\t:" ++ string(i) ++ "\n")
    }
}
