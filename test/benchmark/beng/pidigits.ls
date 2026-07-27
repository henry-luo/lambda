// BENG Benchmark: pidigits
// Compute digits of Pi using the unbounded spigot algorithm (Gibbons 2004)
// Uses integer values with n suffix for unlimited precision arithmetic
// n=30 expected: "3141592653\t:10\n5897932384\t:20\n6264338327\t:30\n"

let NUM_DIGITS = 30

// integer division for decimals: floor(a/b) = (a - a % b) / b
pn idiv(a, b) {
    return (a - a % b) / b
}

pn main() {
    var __t0 = clock()
    // LFT state: (q, r, s, t) as arbitrary-precision decimals
    var q = 1n
    var r = 0n
    var s = 0n
    var t = 1n
    var k = 0n
    var i = 0
    var digits = ""

    while (i < NUM_DIGITS) {
        k = k + 1n
        var k2 = k * 2n + 1n

        // compose: multiply LFT by next term
        var new_q = q * k
        var new_r = (2n * q + r) * k2
        var new_s = s * k
        var new_t = (2n * s + t) * k2
        q = new_q
        r = new_r
        s = new_s
        t = new_t

        // can we extract a digit?
        if (q <= r) {
            var fd3 = idiv(3n * q + r, 3n * s + t)
            var fd4 = idiv(4n * q + r, 4n * s + t)
            if (fd3 == fd4) {
                digits = digits ++ fd3
                i = i + 1

                // output 10 digits per line
                if (i % 10 == 0) {
                    print(digits ++ "\t:" ++ i ++ "\n")
                    digits = ""
                }

                // reduce: eliminate the extracted digit
                r = (r - fd3 * t) * 10n
                q = q * 10n
            }
        }
    }

    // handle last partial line (fewer than 10 digits)
    if (len(digits) > 0) {
        // pad with spaces to 10 characters
        while (len(digits) < 10) {
            digits = digits ++ " "
        }
        print(digits ++ "\t:" ++ i ++ "\n")
    }
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
